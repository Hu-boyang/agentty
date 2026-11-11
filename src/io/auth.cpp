#include "agentty/auth/auth.hpp"
#include "agentty/version.hpp"
#include "agentty/util/lock_levels.hpp"

#include "agentty/util/logx.hpp"

// OAuth配置与其所属的提供者一起存在； `using` 让下面
// 现有的 OAuthConfig:: 引用保持简短。
#include "agentty/provider/anthropic/oauth.hpp"
#include "agentty/provider/chatgpt/codex_oauth.hpp"
#include "agentty/provider/copilot/copilot_oauth.hpp"
// Pasted-key路由：注册表拥有前缀表，凭据::per-provider存储写入，
// 持久化设置活动提供者。该 objlib 中的
// io/account_switch.cpp 已经达到了相同的三个，
// 因此没有新的分层边缘。
#include "agentty/provider/registry.hpp"
#include "agentty/provider/credentials.hpp"
#include "agentty/io/persistence.hpp"
#include "agentty/io/shared_file.hpp"
#include "agentty/util/dbglog.hpp"
#include "agentty/util/home_dir.hpp"
#include "agentty/util/user_root.hpp"

#include <maya/runtime.hpp>
#include <algorithm>
#include <mutex>
#include <cerrno>
#include <chrono>
#include <concepts>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include "agentty/auth/cred_crypt.hpp"
#include "agentty/auth/keystore.hpp"
#include "agentty/io/http.hpp"
#include "agentty/util/env.hpp"

#ifdef _WIN32
#  include <io.h>
#  include <windows.h>
#  include <shellapi.h>
#  include <aclapi.h>          // SetNamedSecurityInfoW，SetEntriesInAclW
#  include <sddl.h>            // (SID帮手)
#else
#  include <fcntl.h>
#  include <sys/file.h>
#  include <sys/stat.h>
#  include <unistd.h>
#  include "agentty/tool/util/exec.hpp"   // run_child:浏览器开启器
#endif

namespace agentty::auth {

using OAuthConfig = agentty::provider::anthropic::OAuthConfig;

namespace fs = std::filesystem;
using json = nlohmann::json;

// ---------------------------------------------------------------------------
// 凭据访问器-从变体导出的视图。
// std::visit+通用 lambda 给出详尽的匹配：在
// 处理之前添加新的替代方案将无法在此处 编译。
// ---------------------------------------------------------------------------

static std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

bool is_valid(const Credentials& c) noexcept {
    return std::visit([](const auto& v) noexcept -> bool {
        using T = std::decay_t<decltype(v)>;
        if      constexpr (std::same_as<T, cred::None>)   return false;
        else if constexpr (std::same_as<T, cred::ApiKey>) return !v.key.empty();
        else /* cred::OAuth */                            return !v.access_token.empty();
    }, c);
}

bool is_expired(const Credentials& c) noexcept {
    return std::visit([](const auto& v) noexcept -> bool {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::same_as<T, cred::OAuth>) {
            if (v.expires_at_ms == 0) return false;
            return now_ms() >= v.expires_at_ms;
        } else {
            return false;   // 无/ApiKey永不过期
        }
    }, c);
}

std::string header_value(const Credentials& c) {
    return std::visit([](const auto& v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        if      constexpr (std::same_as<T, cred::None>)   return {};
        else if constexpr (std::same_as<T, cred::ApiKey>) return v.key;
        else /* cred::OAuth */ return std::string{"Bearer "} + v.access_token;
    }, c);
}

Style style(const Credentials& c) noexcept {
    return std::holds_alternative<cred::OAuth>(c) ? Style::Bearer : Style::ApiKey;
}

AuthHeader make_auth_header(const Credentials& c) {
    return std::visit([](const auto& v) -> AuthHeader {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::same_as<T, cred::None>) {
            // 空的ApiKeyHeader是典型的"unauthenticated"
            // 臂——is_empty(...)在传输层刻度盘之前抓住它。
            return AuthHeader{ApiKeyHeader{}};
        } else if constexpr (std::same_as<T, cred::ApiKey>) {
            return AuthHeader{ApiKeyHeader{v.key}};
        } else /* cred::OAuth */ {
            return AuthHeader{BearerHeader{v.access_token}};
        }
    }, c);
}

bool is_empty(const AuthHeader& h) noexcept {
    return std::visit([](const auto& a) noexcept {
        using T = std::decay_t<decltype(a)>;
        if constexpr (std::same_as<T, ApiKeyHeader>) return a.value.empty();
        else /* BearerHeader */                      return a.token.empty();
    }, h);
}

std::string bearer_token(const AuthHeader& h) {
    return std::visit([](const auto& a) -> std::string {
        using T = std::decay_t<decltype(a)>;
        if constexpr (std::same_as<T, ApiKeyHeader>) return a.value;
        else /* BearerHeader */                      return a.token;
    }, h);
}

std::string_view persist_tag(const Credentials& c) noexcept {
    return std::visit([](const auto& v) noexcept -> std::string_view {
        using T = std::decay_t<decltype(v)>;
        if      constexpr (std::same_as<T, cred::None>)   return "none";
        else if constexpr (std::same_as<T, cred::ApiKey>) return "api_key";
        else /* cred::OAuth */                            return "oauth";
    }, c);
}

std::string OAuthError::render() const {
    std::string_view kind_str;
    switch (kind) {
        case OAuthErrorKind::Network:      kind_str = "network";      break;
        case OAuthErrorKind::BadResponse:  kind_str = "bad response"; break;
        case OAuthErrorKind::ApiError:     kind_str = "api error";    break;
        case OAuthErrorKind::MissingToken: kind_str = "missing token";break;
        case OAuthErrorKind::Superseded:   kind_str = "superseded";   break;
    }
    // 机器-readable "status=NNN "前缀仅用于分类。
    std::string_view shown = detail;
    if (shown.starts_with("status="))
        if (auto sp = shown.find(' '); sp != std::string_view::npos) shown.remove_prefix(sp + 1);
    return "[" + std::string{kind_str} + "] " + std::string{shown};
}

// ---------------------------------------------------------------------------
// 路径
// ---------------------------------------------------------------------------

fs::path config_dir() {
    // 综合：凭据存储位于ONE per-user根下——~/.agentty/credentials
    // ——而不是$XDG_CONFIG_HOME/agentty。
    // 请参阅 util/user_root.hpp 了解完整的 single-root
    // 基本原理（为什么 .config 是 WRONG 令牌 + 线程 的主目录）以及将
    // 旧版 ~/.config/agentty 安装移至此处的 one-time 迁移。
    // 其13个调用站点保留名称`config_dir`；从语义上讲，
    // 它始终是 "where credentials live"。
    return util::user_credentials_dir();
}

fs::path credentials_path() { return config_dir() / "credentials.json"; }

// ---------------------------------------------------------------------------
// Pre-warm:当用户仍在打字时打开TCP+TLS+
// h2到api.anthropic.com，因此第一个真实请求跳过握手。
// http::Client 的 池 保持连接直到使用或直到 90 秒空闲 TTL 过去。
// ---------------------------------------------------------------------------
void prewarm_anthropic() {
    static std::once_flag once;
    bool first = false;
    maya::once(once, [&first] { first = true; });
    if (!first) {
        AGT_LOG(Perf, Debug, "prewarm.anthropic", "skip=already_started");
        return;
    }
    AGT_LOG(Perf, Debug, "prewarm.anthropic", "fire=1");
    const auto& ov = http::agentty_api_host_override();
    http::default_client(::agentty::IoAccess::grant()).prewarm("api.anthropic.com", 443,
                                   ov.active() ? ov.host : std::string{},
                                   ov.active() ? ov.port : uint16_t{0});
}

// ---------------------------------------------------------------------------
// Load/save/clear 凭据
// ---------------------------------------------------------------------------

static void restrict_perms(const fs::path& p) {
#ifdef _WIN32
    // 凭据文件已经被密封并绑定到该(机器、用户)AES-256-GCM，
    // 因此副本在密码上在其他地方是无用的。这是 POSIX
    // 0600 的匹配 defence-in-depth：将文件
    // 继承的 DACL 替换为 授权 完整 控制到 仅 当前用户
    // 的 SID（令牌所有者）的文件，这样另一个本地帐户甚至无法读取密文。
    // Best-effort — 任何失败都会留下继承的 ACL，
    // 而加密仍然是真正的障碍。
    HANDLE token = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) return;
    DWORD len = 0;
    ::GetTokenInformation(token, TokenOwner, nullptr, 0, &len);
    if (len == 0) { ::CloseHandle(token); return; }
    std::vector<unsigned char> buf(len);
    if (!::GetTokenInformation(token, TokenOwner, buf.data(), len, &len)) {
        ::CloseHandle(token);
        return;
    }
    ::CloseHandle(token);
    PSID owner = reinterpret_cast<TOKEN_OWNER*>(buf.data())->Owner;
    if (!owner) return;

    EXPLICIT_ACCESSW ea{};
    ea.grfAccessPermissions = GENERIC_ALL;
    ea.grfAccessMode        = SET_ACCESS;
    ea.grfInheritance       = NO_INHERITANCE;
    ea.Trustee.TrusteeForm  = TRUSTEE_IS_SID;
    ea.Trustee.TrusteeType  = TRUSTEE_IS_USER;
    ea.Trustee.ptstrName    = reinterpret_cast<LPWSTR>(owner);

    PACL dacl = nullptr;
    if (::SetEntriesInAclW(1, &ea, nullptr, &dacl) != ERROR_SUCCESS) return;
    // PROTECTED_DACL_SECURITY_INFORMATION剥离了继承的ACE，
    // 因此ONLY我们的显式所有者-only条目适用。
    ::SetNamedSecurityInfoW(
        const_cast<LPWSTR>(p.wstring().c_str()), SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
        nullptr, nullptr, dacl, nullptr);
    if (dacl) ::LocalFree(dacl);
#else
    ::chmod(p.c_str(), S_IRUSR | S_IWUSR);
#endif
}

// 秘密诞生于0600 (无chmod窗口)并被原子地替换。
static bool write_private(const fs::path& p, const std::string& content) {
    return persistence::write_atomic(IoAccess::grant(), p, content,
                                     {.durability = persistence::Durability::Durable,
                                      .mode = 0600});
}

std::optional<Credentials> load_credentials(Io) {
    std::string raw;
    // 当启用时优选OS密钥库——它保存密封的信封。
    // 当密钥库关闭、为空或项目丢失时（e.g.选择加入后首次运行），
    // 回退到 on-disk 文件。
    if (keystore::available()) {
        std::string ks;
        if (keystore::retrieve("credentials", ks) == keystore::Status::Ok
            && !ks.empty()) {
            raw = std::move(ks);
        }
    }
    if (raw.empty()) {
        std::ifstream ifs(credentials_path(), std::ios::binary);
        if (!ifs) return std::nullopt;
        raw.assign((std::istreambuf_iterator<char>(ifs)),
                    std::istreambuf_iterator<char>());
    }
    if (raw.empty()) return std::nullopt;

    // 加密-at-rest (SECURITY_AUDIT#1)。
    // 新文件采用密封信封； pre-encryption 文件是纯
    // 文本 JSON。检测到哪个，解密密封的情况，并进入 相同 解析
    // 器 两者 — 旧版纯文本文件被透明地接受，
    // 并在下次 save_credentials 运行时获取 re-encrypted
    // （令牌刷新/re-login），因此不需要显式迁移步骤。
    std::string body;
    if (crypt::looks_sealed(raw)) {
        auto pt = crypt::unseal(raw);
        if (!pt) return std::nullopt;   // 被篡改/错误的机器/损坏
        body = std::move(*pt);
    } else {
        body = std::move(raw);          // 遗留明文
    }

    try {
        json j = json::parse(body);
        auto m = j.value("method", "");
        if (m == "oauth") {
            cred::OAuth o;
            o.access_token  = j.value("access_token", "");
            o.refresh_token = j.value("refresh_token", "");
            o.expires_at_ms = j.value("expires_at", std::int64_t{0});
            if (o.access_token.empty()) return std::nullopt;
            return Credentials{std::move(o)};
        }
        if (m == "api_key") {
            cred::ApiKey k;
            k.key = j.value("access_token", "");    // 遗留字段名称
            if (k.key.empty()) return std::nullopt;
            return Credentials{std::move(k)};
        }
        return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

// 便宜的、缓存的 "is there an Anthropic credential on disk?"
// 用于热路径，如提供者-picker VIEW（打开时每个 帧 渲染一次）。
// 幼稚的方法 - 调用 load_credentials() → keystore/disk
// 读取 + AES-unseal + JSON 解析 - 每个 帧 都会花费实际工作；
// 这个 缓存 是 凭据 文件（mtime，size）上的布尔值。统计数据约为 1us，
// 并且在登录、注销（文件删除）、令牌刷新和 cross-process 更改时正确无效，
// 因为即使启用密钥库，save_credentials 也始终（重新）写入加密文件。
// 镜子 provider::copilot::signed_in()。
bool anthropic_signed_in(Io io) {
    struct Cache {
        bool           ok = false;
        std::int64_t   mtime = 0;   // file_time_type 刻度：可发送键
        std::uintmax_t size = static_cast<std::uintmax_t>(-1);
    };
    // 锁-free:整个(ok、mtime、size)三元组被批量替换，
    // 并且视图在渲染路径上到达此，因此不需要能力。
    static maya::snapshot<Cache> cache;

    std::error_code ec;
    const fs::path p = credentials_path();
    const std::int64_t mtime = fs::last_write_time(p, ec).time_since_epoch().count();
    const auto size  = ec ? 0 : fs::file_size(p, ec);
    if (ec) {   // 丢失/无法读取 → 已注销；便宜地记住
        cache.store(Cache{});
        return false;
    }
    {
        const auto c = cache.load();
        if (c->mtime == mtime && c->size == size) return c->ok;
    }
    // 修改：解封+解析，然后发布。
    const bool ok = load_credentials(io).has_value();
    cache.store(Cache{ok, mtime, size});
    return ok;
}

bool save_credentials(Io, const Credentials& c) {
    json j;
    j["method"] = std::string{persist_tag(c)};
    std::visit([&](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::same_as<T, cred::ApiKey>) {
            j["access_token"]  = v.key;
            j["refresh_token"] = "";
            j["expires_at"]    = std::int64_t{0};
        } else if constexpr (std::same_as<T, cred::OAuth>) {
            j["access_token"]  = v.access_token;
            j["refresh_token"] = v.refresh_token;
            j["expires_at"]    = v.expires_at_ms;
        } else {
            j["access_token"]  = "";
            j["refresh_token"] = "";
            j["expires_at"]    = std::int64_t{0};
        }
    }, c);
    fs::path p = credentials_path();
    // 静态加密(SECURITY_AUDIT#1)。如果密封失败
    // （硬 OpenSSL 错误），则拒绝保留而不是默默地写
    // 入明文令牌 - in-memory 凭据 仍然适用于此会话，
    // 并且调用者会显示 保存 失败。
    std::string payload = j.dump(2);
    auto sealed = crypt::seal(payload);
    if (!sealed) return false;
    // 当启用OS密钥库时，它成为密封信封的主要存储库。
    // 我们仍然会写入加密文件，因此稍后在禁用密钥库（或无法
    // 解锁的钥匙串）的情况下运行仍然可以 加载。
    // 该文件已经是 machine-bound-encrypted，
    // 因此这不是降级 - 它与密钥库之前提供的保护相同。
    if (keystore::available())
        keystore::store("credentials", *sealed);
    if (!write_private(p, *sealed)) return false;
    restrict_perms(p);
    return true;
}

bool clear_credentials(Io) {
    if (keystore::available())
        keystore::remove("credentials");
    std::error_code ec;
    fs::remove(credentials_path(), ec);
    return !ec;
}

// ---------------------------------------------------------------------------
// PKCE 帮手
// ---------------------------------------------------------------------------

std::string base64url_no_pad(const unsigned char* data, size_t len) {
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    size_t i = 0;
    while (i + 3 <= len) {
        uint32_t v = (data[i] << 16) | (data[i+1] << 8) | data[i+2];
        out.push_back(tbl[(v >> 18) & 0x3f]);
        out.push_back(tbl[(v >> 12) & 0x3f]);
        out.push_back(tbl[(v >> 6) & 0x3f]);
        out.push_back(tbl[v & 0x3f]);
        i += 3;
    }
    if (i < len) {
        uint32_t v = data[i] << 16;
        if (i + 1 < len) v |= data[i+1] << 8;
        out.push_back(tbl[(v >> 18) & 0x3f]);
        out.push_back(tbl[(v >> 12) & 0x3f]);
        if (i + 1 < len) out.push_back(tbl[(v >> 6) & 0x3f]);
    }
    return out;
}

// 加密-secure URL-safe随机字符串(SECURITY_AUDIT:PKCE)。
// OAuth code_verifier和反-CSRF状态MUST是不可预测的
// ——非-CSPRNG (mt19937)可从少数输出中恢复，这将使攻击者
// 伪造验证者或预测状态。 RAND_bytes 是 OpenSSL CSPRNG
// （已链接；用于 cred_crypt.cpp）。我们为每个输出字符绘制一个
// 安全的 字节，并将 归并 绘制到 64-char 字母表中：64 可以均匀地
// 除以 256，因此模数是无偏的（每个字符的可能性相同）。
// RAND_bytes 失败被视为致命的 — 中止登录比发出低 -entropy 秘密更好。
std::string random_urlsafe(Io, size_t n) {
    static const char charset[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    // 字符集有64个可用字符(sizeof-1为NUL)； 256 %
    // 64 == 0 因此屏蔽统一 字节 的低 6 位本身就是统一的——没有偏差，
    // 不需要拒绝循环。
    static_assert(sizeof(charset) - 1 == 64, "alphabet must be 64 for unbiased mask");
    std::vector<unsigned char> buf(n);
    if (n > 0 && RAND_bytes(buf.data(), static_cast<int>(n)) != 1) {
        // CSPRNG不可用——NOT是否回退到弱生成器。
        throw std::runtime_error("RAND_bytes failed: no secure entropy source");
    }
    std::string out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) out.push_back(charset[buf[i] & 0x3f]);
    return out;
}

std::string sha256_hex(const std::string& s) {
    unsigned char md[SHA256_DIGEST_LENGTH];
    ::SHA256(reinterpret_cast<const unsigned char*>(s.data()), s.size(), md);
    std::ostringstream oss;
    oss << std::hex;
    for (unsigned char c : md) oss << (c < 16 ? "0" : "") << (int)c;
    return oss.str();
}

std::string code_challenge_s256(const std::string& verifier) {
    unsigned char md[SHA256_DIGEST_LENGTH];
    ::SHA256(reinterpret_cast<const unsigned char*>(verifier.data()),
             verifier.size(), md);
    return base64url_no_pad(md, SHA256_DIGEST_LENGTH);
}

// ---------------------------------------------------------------------------
// HTTP 帮助程序（针对 OAuth 端点形成 -encoded POST）
// ---------------------------------------------------------------------------

namespace {

// RFC 3986无保留集通过；其他一切都得到%HH-encoded。
// 镜像同一输入集的curl_easy_escape的行为。
std::string url_escape(std::string_view s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() + s.size() / 4);
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
         || (c >= '0' && c <= '9')
         || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[(c >> 4) & 0xF]);
            out.push_back(hex[c & 0xF]);
        }
    }
    return out;
}

std::string form_urlencode(const std::vector<std::pair<std::string,std::string>>& kv) {
    std::string out;
    for (size_t i = 0; i < kv.size(); ++i) {
        if (i) out += '&';
        out += url_escape(kv[i].first);
        out += '=';
        out += url_escape(kv[i].second);
    }
    return out;
}

// 我们访问的一小组 OAuth URL 的端点 解析器。我们不会引入一般的 URL
// 解析器：我们使用的每个 URL 都是 https://,，没有用户信息，没有片段。
struct ParsedUrl {
    std::string host;
    uint16_t    port = 443;
    std::string path;   // 包括前导 '/' 和任何查询字符串
};

std::expected<ParsedUrl, std::string> parse_https_url(std::string_view url) {
    constexpr std::string_view scheme = "https://";
    if (url.substr(0, scheme.size()) != scheme)
        return std::unexpected(std::string{"missing https:// scheme"});
    url.remove_prefix(scheme.size());
    auto slash = url.find('/');
    std::string_view authority = url.substr(0, slash);
    ParsedUrl p;
    p.path = std::string{slash == std::string_view::npos ? "/" : url.substr(slash)};
    auto colon = authority.find(':');
    if (colon == std::string_view::npos) {
        p.host = std::string{authority};
    } else {
        p.host = std::string{authority.substr(0, colon)};
        try {
            int port_int = std::stoi(std::string{authority.substr(colon + 1)});
            if (port_int <= 0 || port_int > 65535)
                return std::unexpected(std::string{"port out of range"});
            p.port = static_cast<uint16_t>(port_int);
        } catch (...) { return std::unexpected(std::string{"bad port"}); }
    }
    if (p.host.empty()) return std::unexpected(std::string{"empty host"});
    return p;
}

// 内部HTTP形成-POST结果。与公共 TokenResult 不同，因此 带类型
// 的 映射（HTTP 错误 → OAuthError）恰好发生在一处（下面的
// parse_token_json）。
struct FormPostResult {
    int         status = 0;
    std::string body;
    std::string transport_error;   // non-empty iff请求从未到达服务器
};

FormPostResult http_post_form(const std::string& url,
    const std::vector<std::pair<std::string,std::string>>& fields) {
    FormPostResult r;
    auto parsed = parse_https_url(url);
    if (!parsed) {
        r.transport_error = "bad url: " + url + " (" + parsed.error() + ")";
        return r;
    }

    http::Request hreq;
    hreq.method = http::HttpMethod::Post;
    hreq.host   = parsed->host;
    hreq.port   = parsed->port;
    // Air-gapped用户通过SSH隧道传输OAuth主机（令牌交换+刷新），
    // 就像他们隧道API主机一样——参见include/agentty/io/http.hpp.中的AGENTTY_OAUTH_HOST。
    // 如果没有这个，当访问令牌在-session中期过期时，
    // OAuth刷新就会在getaddrinfo上终止。
    if (const auto& ov = http::agentty_oauth_host_override(); ov.active()) {
        hreq.dial_host = ov.host;
        hreq.dial_port = ov.port;
    }
    hreq.path   = parsed->path;
    hreq.headers = {
        {"content-type", "application/x-www-form-urlencoded"},
        {"accept",       "application/json"},
        {"user-agent",   ::agentty::user_agent()},
    };
    hreq.body = form_urlencode(fields);
    // OAuth令牌exchange/refresh响应是微小的JSON对象(
    // access_token、refresh_token、expires_in、
    // ～1KB)。收紧一元 正文 上限，以便 runaway/misbehaving OAuth
    // 代理无法在我们注意到之前向我们返回 数据流 GB。
    hreq.max_body_bytes = 1ull * 1024 * 1024;

    http::Timeouts tos;
    tos.connect = std::chrono::milliseconds(10'000);
    tos.total   = std::chrono::milliseconds(30'000);

    auto resp = http::default_client(::agentty::IoAccess::grant()).send(hreq, tos);
    if (!resp) { r.transport_error = resp.error().render(); return r; }
    r.status = resp->status;
    r.body   = std::move(resp->body);
    return r;
}

} // 命名空间结束。

// ---------------------------------------------------------------------------
// 令牌交换/刷新——HTTP/JSON故障形状被映射到带类型的
// OAuthErrorKind的唯一地方。 下游 呼叫者在 `kind` 上
// 只能看到 TokenResult 和 分派。
// ---------------------------------------------------------------------------

static TokenResult parse_token_json(const std::string& body, int http_status) {
    json j;
    try {
        j = json::parse(body);
    } catch (const std::exception& e) {
        return std::unexpected(OAuthError{
            OAuthErrorKind::BadResponse,
            std::string{"json parse failed: "} + e.what()});
    }
    if (http_status >= 400) {
        return std::unexpected(OAuthError{
            OAuthErrorKind::ApiError,
            j.value("error_description",
                j.value("error",
                    std::string{"HTTP "} + std::to_string(http_status)))});
    }
    OAuthToken tok;
    tok.access_token  = j.value("access_token", "");
    tok.refresh_token = j.value("refresh_token", "");
    tok.expires_in_s  = j.value("expires_in", std::int64_t{0});
    if (tok.access_token.empty()) {
        return std::unexpected(OAuthError{
            OAuthErrorKind::MissingToken,
            "200 OK but no access_token in response"});
    }
    return tok;
}

std::string oauth_authorize_url(const PkceVerifier& verifier,
                                const OAuthState&   state) {
    std::string challenge = code_challenge_s256(verifier.value);
    std::ostringstream url;
    url << OAuthConfig::authorize_url
        << "?response_type=code"
        << "&client_id="             << OAuthConfig::client_id
        << "&redirect_uri="          << url_escape(OAuthConfig::redirect_uri)
        << "&scope="                 << url_escape(OAuthConfig::scopes)
        << "&state="                 << url_escape(state.value)
        << "&code_challenge="        << url_escape(challenge)
        << "&code_challenge_method=S256"
        << "&code=true";
    return url.str();
}

TokenResult exchange_code(const OAuthCode& code,
                          const PkceVerifier& verifier,
                          const OAuthState& state) {
    // Claude的回调返回"<code>#<state>"加入。
    // 分为两半：'#' 之前的所有内容都是授权码，
    // 之后的所有内容都是 IdP 回显的状态。
    std::string actual_code = code.value;
    std::string returned_state;
    if (auto hash = actual_code.find('#'); hash != std::string::npos) {
        returned_state = actual_code.substr(hash + 1);
        actual_code    = actual_code.substr(0, hash);
    }

    // Anti-CSRF (SECURITY_AUDIT)：验证回显状态
    // 与我们为 此 登录生成的状态是否匹配。不匹配意味着粘贴的代码属于
    // 不同的授权请求（CSRF / mix-up / 过期 粘贴） -
    // 拒绝交换它。 Constant-time 进行比较，
    // 这样我们就不会通过计时泄漏预期值。我们仅在 IdP 实际回显状态（
    // returned_state 非-empty）时强制执行；省略它的旧
    // 回调 页面仍然依赖于 PKCE 绑定 验证程序的代码。
    if (!returned_state.empty()) {
        const std::string& expected = state.value;
        bool ok = returned_state.size() == expected.size();
        unsigned char diff = ok ? 0u : 1u;
        for (size_t i = 0; i < expected.size() && i < returned_state.size(); ++i)
            diff |= static_cast<unsigned char>(expected[i] ^ returned_state[i]);
        if (!ok || diff != 0) {
            return std::unexpected(OAuthError{
                OAuthErrorKind::BadResponse,
                "state mismatch — the pasted code does not match this login "
                "request (possible CSRF or a stale/foreign code). Re-run login."});
        }
    }

    auto r = http_post_form(OAuthConfig::token_url, {
        {"grant_type",    "authorization_code"},
        {"code",          actual_code},
        {"client_id",     OAuthConfig::client_id},
        {"redirect_uri",  OAuthConfig::redirect_uri},
        {"code_verifier", verifier.value},
        {"state",         state.value},
    });
    if (!r.transport_error.empty())
        return std::unexpected(OAuthError{
            OAuthErrorKind::Network, r.transport_error});
    return parse_token_json(r.body, r.status);
}

TokenResult refresh_access_token(Io /*io*/, const RefreshToken& refresh_token) {
    auto r = http_post_form(OAuthConfig::token_url, {
        {"grant_type",    "refresh_token"},
        {"client_id",     OAuthConfig::client_id},
        {"refresh_token", refresh_token.value},
    });
    // 刷新失败向用户呈现为 "suddenly logged out"
    // 或静默 401 循环，屏幕上没有任何内容对其进行解释。
    // 刷新 TOKEN 永远不会到达日志（编辑无论如何都会将其删除）；
    // 只有结果才会如此。
    if (!r.transport_error.empty()) {
        AGT_LOG(Auth, Warn, "auth.refresh", "result=network_error err={}",
                r.transport_error);
        return std::unexpected(OAuthError{
            OAuthErrorKind::Network, r.transport_error});
    }
    auto parsed = parse_token_json(r.body, r.status);
    AGT_LOGL(Auth, parsed ? ::agentty::logx::Level::Info
                          : ::agentty::logx::Level::Warn,
             "auth.refresh", "result={} status={}",
             parsed ? "ok" : "rejected", r.status);
    return parsed;
}

namespace {
// OAuth 信用上的 过期-check 具有 60 年代早期-refresh 偏差。
bool oauth_is_stale(const cred::OAuth& o) {
    return o.expires_at_ms != 0 && now_ms() >= o.expires_at_ms - 60'000;
}
// 从已经-valid on-disk OAuth信用建立TokenResult成功，
// 因此丢失cross-process 竞态的调用者获得成功者的令牌。
TokenResult token_result_from(const cred::OAuth& o) {
    OAuthToken tr;
    tr.access_token  = o.access_token;
    tr.refresh_token = o.refresh_token;
    tr.expires_in_s  = o.expires_at_ms
                     ? std::max<std::int64_t>(0, (o.expires_at_ms - now_ms()) / 1000)
                     : 0;
    return tr;
}
} // 命名空间结束。

namespace {
TokenResult refresh_in_section(Io io, bool cross, std::string rt) {
    // 锁下的Re-read：如果对端已经刷新，则采用其令牌
    // 而不是激发我们的令牌。 Anthropic 轮换刷新令牌，
    // 因此第二次刷新将使 对端 无效并旋转刷新循环。
    if (cross) {
        if (auto loaded = load_credentials(io)) {
            if (auto* o = std::get_if<cred::OAuth>(&*loaded)) {
                if (!oauth_is_stale(*o) && !o->access_token.empty())
                    return token_result_from(*o);   // 对端 已刷新
                if (!o->refresh_token.empty())
                    rt = o->refresh_token;          // 使用最新的on-disk令牌
            }
        }
    }

    auto tr = refresh_access_token(io, RefreshToken{rt});
    if (!tr) return tr;

    // 帐户切换写入商店时没有该锁，因此商店现在可能
    // 持有不同的帐户。如果其刷新令牌既不是我们发送
    // 的也不是我们返回的，请不要破坏它：报告已取代。
    if (auto now_on_disk = load_credentials(io)) {
        if (auto* o = std::get_if<cred::OAuth>(&*now_on_disk)) {
            if (!o->refresh_token.empty() && o->refresh_token != rt
                && o->refresh_token != tr->refresh_token) {
                AGT_LOG(Auth, Warn, "auth.refresh.superseded",
                        "store swapped mid-refresh (account switch); "
                        "discarding refreshed token");
                return std::unexpected(OAuthError{
                    OAuthErrorKind::Superseded,
                    "account switched during refresh"});
            }
        }
    }

    // 已保存在该部分内，因此下一个进程的re-read可以看到它。
    // 失败的 保存 会在磁盘上留下旋转的 -away 令牌；记录它。
    Credentials refreshed{cred::OAuth{
        tr->access_token,
        tr->refresh_token.empty() ? rt : tr->refresh_token,
        tr->expires_in_s ? now_ms() + tr->expires_in_s * 1000 : 0,
    }};
    if (!save_credentials(io, refreshed))
        util::dbglog("auth.refresh.save",
                     "failed to persist rotated OAuth token; next launch may "
                     "require re-login (keystore locked / disk full / perms?)");
    return tr;
}
}  // 命名空间结束。

TokenResult refresh_access_token_locked(Io, const locks::below_cred_file& cap,
                                        const RefreshToken& refresh_token) {
    return persistence::with_shared_file(cap, persistence::cred_lane(credentials_path()), credentials_path(),
        [](const maya::held<locks::cred_file>&, persistence::Held h, std::string rt) {
            return refresh_in_section(IoAccess::grant(), h.cross_process, std::move(rt));
        }, refresh_token.value);
}

// ---------------------------------------------------------------------------
// 启动时解决
// ---------------------------------------------------------------------------

// Single-shot邮箱：在TUI启动之前由resolve()在主线程上写入，
// 在maya::run启动后由同一主线程上的AgenttyApp::init()读取。
// Single-producer / single-consumer，
// 因此普通的 std::optional 就足够了——不需要原子。
namespace {
std::optional<std::string> g_pending_refresh{};
}

void set_pending_refresh(std::string refresh_token) {
    g_pending_refresh = std::move(refresh_token);
}

std::optional<std::string> take_pending_refresh() {
    auto out = std::move(g_pending_refresh);
    g_pending_refresh.reset();
    return out;
}

std::optional<std::string>
oauth_proactive_refresh_token(Io io, std::int64_t window_ms) {
    // Env-injected OAuth令牌(CLAUDE_CODE_OAUTH_TOKEN)
    // 没有过期或refresh_token，并且env/CLI API密钥根本
    // 不可刷新——跳过这些密钥的磁盘读取，因此我们永远不会接触它们的网络。
    using namespace agentty::util;
    if (env::get_or_null<env::Var::AnthropicApiKey>()
     || env::get_or_null<env::Var::ClaudeOAuthToken>())
        return std::nullopt;

    auto loaded = load_credentials(io);
    if (!loaded) return std::nullopt;
    auto* o = std::get_if<cred::OAuth>(&*loaded);
    if (!o) return std::nullopt;                 // API-key/无：无事可做
    if (o->expires_at_ms == 0) return std::nullopt;   // 无到期信息
    if (o->refresh_token.empty()) return std::nullopt; // 没什么可刷新的
    // 在主动窗口（即将失效）OR内已经失效。
    // 已过期通常会在启动时/反应性地捕获，但将其返回到此处也是
    // 无害的 - 调用者会限制并控制 in-flight 状态。
    if (now_ms() < o->expires_at_ms - window_ms) return std::nullopt; // 依然新鲜
    return o->refresh_token;
}

Credentials resolve(Io io, const std::string& cli_api_key) {
    if (!cli_api_key.empty())
        return Credentials{cred::ApiKey{cli_api_key}};
    using namespace agentty::util;
    if (const char* k = env::get_or_null<env::Var::AnthropicApiKey>())
        return Credentials{cred::ApiKey{std::string{k}}};
    if (const char* t = env::get_or_null<env::Var::ClaudeOAuthToken>())
        return Credentials{cred::OAuth{std::string{t}, "", 0}};

    auto loaded = load_credentials(io);
    if (!loaded) return Credentials{cred::None{}};

    // Non-blocking:expired-with-refresh隐藏刷新令牌，
    // 以供状态更新函数在挂载TUI后拾取；网络往返发生在启动路径之外。
    // Expired-without-refresh 返回 None，
    // 因此 init.cpp 打开登录 弹出面板。
    return std::visit([&](auto v) -> Credentials {
        using T = std::decay_t<decltype(v)>;
        if constexpr (!std::same_as<T, cred::OAuth>) {
            return Credentials{std::move(v)};
        } else {
            const bool expired = v.expires_at_ms != 0
                              && now_ms() >= v.expires_at_ms;
            if (!expired) return Credentials{std::move(v)};
            if (v.refresh_token.empty()) {
                // 没有什么可以刷新的； in-TUI 登录 弹出面板
                // 将向用户显示empty-creds 状态。
                return Credentials{cred::None{}};
            }
            // 将刷新令牌交给状态更新函数的init Cmd。
            // 过期的access_token被返回，因此检查信用(
            // e.g.Deps中的auth_header)的任何人都看到pre
            // -refresh状态，直到后台任务登陆；
            // submit_message 控制用户在
            // oauth_refresh_in_flight 标志 上发送，因此不会使用已知的
            // -stale 令牌触发请求。
            set_pending_refresh(v.refresh_token);
            return Credentials{std::move(v)};
        }
    }, *loaded);
}

// ---------------------------------------------------------------------------
// 同步保鲜凭据(安全工作线程-thread)
// ---------------------------------------------------------------------------

namespace {
AuthHeader fresh_auth_header_locked(Io io, const maya::held<locks::auth_refresh>& cap,
                                    const AuthHeader& fallback);
}

AuthHeader fresh_auth_header(Io /*io*/, const locks::below_auth_refresh& cap,
                             const AuthHeader& fallback) {
    using namespace agentty::util;

    // CLI/env API键是静态的——无需刷新。 env OAuth 令牌 (
    // CLAUDE_CODE_OAUTH_TOKEN) 是不透明的，没有刷新令牌，
    // 因此也返回为 -is。
    if (env::get_or_null<env::Var::AnthropicApiKey>()
     || env::get_or_null<env::Var::ClaudeOAuthToken>())
        return fallback;

    // 序列化刷新：当捕获的OAuth令牌过期时，多个子代理工作线程
    // 线程可以立即登陆这里。第一个在刷新+持续；其余的 re
    // -read 是新鲜的 -saved 令牌。
    static maya::guarded<bool, locks::auth_refresh> lane;
    return lane.with(cap,
        [](const maya::held<locks::auth_refresh>& h, bool&, AuthHeader fb) {
            // 受保护的正文不能接受呼叫者的I/O(它不是可发送的)，
            // 但是这在有一个的fresh_auth_header下运行。
            return fresh_auth_header_locked(IoAccess::grant(), h, fb);
        },
        fallback);
}

namespace {
AuthHeader fresh_auth_header_locked(Io io, const maya::held<locks::auth_refresh>& cap,
                                    const AuthHeader& fallback) {
    auto loaded = load_credentials(io);
    if (!loaded) return fallback;

    auto* oauth = std::get_if<cred::OAuth>(&*loaded);
    if (!oauth) {
        // 已保存 API密钥：交还其头文件（还涵盖自启动
        // 以来盘上信用改变的情况）。
        return make_auth_header(*loaded);
    }

    // 刷新得有点早（60秒偏差），因此在-request中期过期的令牌不会出现401。
    // 没有过期信息（expires_at_ms==0）→信任它为-is。
    auto is_stale = [](const cred::OAuth& o) {
        return o.expires_at_ms != 0 && now_ms() >= o.expires_at_ms - 60'000;
    };
    if (!is_stale(*oauth) || oauth->refresh_token.empty())
        return make_auth_header(*loaded);

    // Cross-process 序列化 + double-checked re
    // -read 存在于 refresh_access_token_locked 中：它
    // 一次通过一次刷新来汇集每个 agentty 实例，并采用 对端 的 freshly
    // -saved 令牌，而不是触发冗余（refresh-token-rotating，
    // 因此相互-invalidating）刷新 — 这是要防范的 multi-instance 刷新循环。
    // 它还将新的信用信息保存到磁盘上。
    auto tr = refresh_access_token_locked(io, cap, RefreshToken{oauth->refresh_token});
    if (!tr) {
        // 刷新失败（网络/撤销）。归还我们所拥有的一切；
        // 传输层 将显示最终的 401 并带有登录提示。
        return make_auth_header(*loaded);
    }

    Credentials refreshed{cred::OAuth{
        std::move(tr->access_token),
        tr->refresh_token.empty() ? oauth->refresh_token
                                  : std::move(tr->refresh_token),
        tr->expires_in_s ? now_ms() + tr->expires_in_s * 1000 : 0,
    }};
    return make_auth_header(refreshed);
}
}  // 命名空间结束。

// ---------------------------------------------------------------------------
// 浏览器启动
// ---------------------------------------------------------------------------

void open_browser(const std::string& url) {
#ifdef _WIN32
    // ShellExecuteA将URL作为单个参数传递给shell的URL
    // 处理函数——没有组成命令行，因此不存在shell-quoting表面。
    // 安全为-is。
    ::ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    // 启动平台开启器，其中 URL 作为不同的 argv 元素，
    // 不 插入到 `/bin/sh -c` 字符串中。这会从代码路径中完全
    // 删除 shell：无论 字节 携带什么，URL 都不能是 word-split、
    // globbed、command-substituted 或中断引用。
    // （今天的 OAuth URL 都是 url-escaped，
    // 所以这是 defence-in-depth ——但它可以通过构造
    // 而不是运气来保证将来在此处传递任意 URL 的调用者的安全。）
#  if defined(__APPLE__)
    const char* opener = "open";
#  else
    const char* opener = "xdg-open";
#  endif
    // 标准输入是/dev/null并且其输出被捕获并丢弃，
    // 因此它不能在终端 agentty所拥有的上乱写。
    // 开启者fork真实浏览器并立即退出；浏览器会继续运行。
    tools::util::ChildRun run;
    run.argv = {opener, url};
    run.idle = std::chrono::seconds{10};
    run.wall = std::chrono::seconds{20};
    run.max_output_bytes = 4096;
    (void)tools::util::run_child(run);
#endif
}

// ---------------------------------------------------------------------------
// 子命令
// ---------------------------------------------------------------------------

int cmd_login(Io io, const maya::unlocked& cap) {
    // 显示您已经登录的内容，因此切换帐户是显而易见的——选择与
    // add/replace、`agentty logout` 不同的选项
    // 来删除一个。
    {
        namespace codex = provider::chatgpt;
        bool any = false;
        std::error_code ec;
        if (fs::exists(credentials_path(), ec)) {
            std::cout << "Currently signed in: claude.ai / Anthropic\n";
            any = true;
        }
        if (codex::load_codex_credentials()) {
            std::cout << "Currently signed in: ChatGPT / Codex\n";
            any = true;
        }
        if (provider::copilot::signed_in()) {
            std::cout << "Currently signed in: GitHub Copilot\n";
            any = true;
        }
        if (any) std::cout << "\n";
    }
    std::cout << "agentty — sign in (bring your own model)\n\n"
              << "  1) Paste an API key (Anthropic sk-ant-..., or any provider)\n"
              << "  2) OAuth via claude.ai (Pro/Max subscription)\n"
              << "  3) OAuth via ChatGPT (Codex / Plus / Pro)\n"
              << "  4) OAuth via GitHub Copilot (Individual / Business / Enterprise)\n"
              << "\nChoice [1/2/3/4]: " << std::flush;
    std::string choice;
    std::getline(std::cin, choice);
    for (auto& c : choice) c = (char)std::tolower((unsigned char)c);

    // GitHub Copilot OAuth — GitHub 的设备流程（在任何地方都有效，
    // 包括 SSH）：显示代码，用户在 github.com/login/device. 输入它
    if (choice == "4" || choice == "copilot" || choice == "github") {
        std::cout << "\nRequesting a GitHub device code\xE2\x80\xA6\n" << std::flush;
        auto r = provider::copilot::login(
            cap, 900,
            [](const provider::copilot::DeviceCode& dc) {
                std::cout << "\nOpen this link in a browser on any device:\n  "
                          << dc.verification_uri
                          << "\n\nEnter this one-time code (expires in "
                          << (dc.expires_in / 60) << " minutes):\n  "
                          << dc.user_code
                          << "\n\nNote: GitHub will label the app \"GitHub "
                             "Copilot Plugin\" on the consent page. That is "
                             "expected \xE2\x80\x94 agentty signs in as VS "
                             "Code's Copilot client because the API's model "
                             "allowlist is keyed to that identity (see "
                             "docs/design/copilot-provider.md)."
                          << "\n\nWaiting for approval\xE2\x80\xA6\n" << std::flush;
                open_browser(dc.verification_uri);
            },
            /*已取消=*/std::stop_token{});
        if (!r) {
            std::cerr << "GitHub Copilot sign-in failed: " << r.error().render() << "\n";
            return 1;
        }
        std::cout << "\n\xE2\x9C\x93 Signed in to GitHub Copilot. Saved to "
                  << provider::copilot::credentials_path().string() << "\n"
                  << "  Run agentty with `--provider copilot` to use it.\n";
        return 0;
    }

    // ChatGPT OAuth。 SSH会话自动使用OpenAI的
    // device-code流；本地会话保持浏览器+环回回调。
    if (choice == "3" || choice == "chatgpt" || choice == "codex"
        || choice == "openai") {
        const bool device_auth = provider::chatgpt::codex_device_auth_preferred();
        if (device_auth) {
            std::cout << "\nRequesting a ChatGPT device code for this SSH session…\n"
                      << std::flush;
        } else {
            std::cout << "\nOpening browser to sign in to ChatGPT…\n"
                      << "(a local page on http://localhost:1455 will confirm)\n\n"
                      << std::flush;
        }
        auto r = provider::chatgpt::codex_login(
            io, cap, 900, [](const provider::chatgpt::CodexDeviceCode& code) {
                std::cout << "\nOpen this link in a browser on any device:\n  "
                          << code.verification_url
                          << "\n\nEnter this one-time code (expires in 15 minutes):\n  "
                          << code.user_code
                          << "\n\nWaiting for approval…\n" << std::flush;
            });
        if (!r) {
            std::cerr << "ChatGPT sign-in failed: " << r.error().render() << "\n";
            return 1;
        }
        if (!provider::chatgpt::save_codex_credentials(cap, *r)) {
            std::cerr << "ChatGPT sign-in succeeded but credentials could not be saved.\n";
            return 1;
        }
        std::cout << "\n\xE2\x9C\x93 Signed in to ChatGPT. Saved to "
                  << provider::chatgpt::codex_credentials_path().string() << "\n"
                  << "  Run agentty with `--provider chatgpt` to use it.\n";
        return 0;
    }

    // 默认(空/"1"/"api"/"key")为API-key路径；
    // OAuth 是显式的 "2"/"oauth" opt-in。 分派 位于字符串上，
    // 而不是位置顺序上，因此上面显示的编号是presentation-only。
    const bool want_oauth = (choice == "2" || choice == "oauth"
                             || choice == "claude");
    if (!want_oauth) {
        std::cout << "\nPaste API key: " << std::flush;
        std::string key;
        std::getline(std::cin, key);
        while (!key.empty() && (key.back() == '\r' || key.back() == '\n'
                                || key.back() == ' ')) key.pop_back();
        if (key.empty()) { std::cerr << "No key entered.\n"; return 1; }

        // 将密钥路由到提供者的 PREFIX 名称，而不是 Anthropic。
        //
        // 这个提示词总是说"or any provider"，
        // 然后将每个密钥存储在Anthropic 凭据存储中(问题
        // #68)。 Groq 或 OpenRouter 密钥被接受，
        // 已保存，报告为成功，然后在第一回合失败，并出现名为 Anthropic
        // 的身份验证错误 - 因此用户针对他们没有选择的供应商调试
        // 了正确的 凭据。
        //
        // provider_for_api_key是故意保守的：
        // 仅当注册表行明确声明前缀时它才应答。裸露的 `sk-`
        // 可能是 OpenAI、DeepSeek 或自定义端点，
        // 因此它返回空，我们会进入 Anthropic 存储 -
        // 这是历史行为，也是方法菜单以 Claude 开头的工具
        // 的正确默认值。不同之处在于，现在它是针对真正不明确
        // 案例的 FALLBACK，而不是针对每个案例的答案。
        const std::string_view detected = provider::provider_for_api_key(key);
        if (!detected.empty() && detected != "anthropic") {
            const auto* row = provider::preset_for(detected);
            const std::string_view label = row ? row->label : detected;
            if (!provider::credentials::add_key(io, cap, detected, key)) {
                std::cerr << "Failed to save the " << label << " key.\n";
                return 1;
            }
            // 也使其成为主动提供者：粘贴打算使用 Groq
            // 的 Groq 密钥的用户，并将选择留在其他地方将
            // 以更安静的形式带来相同类别的惊喜。
            auto s = persistence::load_settings(io);
            s.provider = std::string{detected};
            persistence::save_settings(io, cap, s);
            // "an OpenRouter key"，"a Groq key"（OpenRouter 或 Groq 的密钥。）
            // ——标签是固定的集合，因此元音检查是这里需要的整个语法。
            const bool vowel = !label.empty()
                && (label[0] == 'A' || label[0] == 'E' || label[0] == 'I'
                    || label[0] == 'O' || label[0] == 'U');
            std::cout << "Recognised a" << (vowel ? "n " : " ") << label
                      << " key and saved it as your active provider.\n";
            return 0;
        }

        Credentials c{cred::ApiKey{std::move(key)}};
        if (!save_credentials(io, c)) {
            std::cerr << "Failed to save credentials.\n"; return 1;
        }
        std::cout << "Saved API key to " << credentials_path().string() << "\n";
        return 0;
    }

    // OAuth PKCE流程
    PkceVerifier verifier{random_urlsafe(io, 128)};
    OAuthState   state{random_urlsafe(io, 32)};
    std::string  auth_url = oauth_authorize_url(verifier, state);

    std::cout << "\nOpening browser to authorize agentty...\n"
              << auth_url << "\n\n";
    open_browser(auth_url);

    std::cout << "After logging in, paste the code shown on the callback page: "
              << std::flush;
    std::string code_raw;
    std::getline(std::cin, code_raw);
    while (!code_raw.empty() && (code_raw.back() == '\r' || code_raw.back() == '\n'
                                 || code_raw.back() == ' ')) code_raw.pop_back();
    if (code_raw.empty()) { std::cerr << "No code entered.\n"; return 1; }

    auto tr = exchange_code(OAuthCode{std::move(code_raw)}, verifier, state);
    if (!tr) {
        std::cerr << "Token exchange failed: " << tr.error().render() << "\n";
        return 1;
    }
    Credentials c{cred::OAuth{
        std::move(tr->access_token),
        std::move(tr->refresh_token),
        tr->expires_in_s ? now_ms() + tr->expires_in_s * 1000 : 0
    }};
    if (!save_credentials(io, c)) {
        std::cerr << "Failed to save credentials.\n"; return 1;
    }
    std::cout << "\n\xE2\x9C\x93 Logged in. Saved to " << credentials_path().string() << "\n";
    return 0;
}

int cmd_logout(Io io, const maya::unlocked& cap) {
    namespace codex = provider::chatgpt;
    // 每个签名的-in账户一个条目。无需 2-way 特殊情况即可
    // 扩展到 N 个提供商（不包括 Copilot）。
    struct Account {
        std::string label;
        std::string path;
        std::function<bool()> clear;
    };
    std::vector<Account> accounts;

    auto p = credentials_path();
    std::error_code ec;
    if (fs::exists(p, ec))
        accounts.push_back({"claude.ai / Anthropic", p.string(),
                            [io] { return clear_credentials(io); }});
    if (codex::load_codex_credentials())
        accounts.push_back({"ChatGPT / Codex", codex::codex_credentials_path().string(),
                            [&cap] { return codex::clear_codex_credentials(cap); }});
    if (provider::copilot::signed_in())
        accounts.push_back({"GitHub Copilot", provider::copilot::credentials_path().string(),
                            [io, &cap] { return provider::copilot::clear_credentials(io, cap); }});

    if (accounts.empty()) { std::cout << "No saved credentials.\n"; return 0; }

    // 正好有一个登录→删除它，没有提示词。
    if (accounts.size() == 1) {
        if (!accounts[0].clear()) {
            std::cerr << "Failed to remove " << accounts[0].path << "\n"; return 1;
        }
        std::cout << "Removed " << accounts[0].path << "\n";
        return 0;
    }

    // 多人登录 → 为所有人选择一个（按号码）或 `a`。
    std::cout << "Signed in to more than one account:\n";
    for (std::size_t i = 0; i < accounts.size(); ++i)
        std::cout << "  " << (i + 1) << ") " << accounts[i].label
                  << "  (" << accounts[i].path << ")\n";
    std::cout << "  a) all of them\n"
              << "\nSign out of [1-" << accounts.size() << "/a]: " << std::flush;
    std::string choice;
    std::getline(std::cin, choice);
    for (auto& c : choice) c = (char)std::tolower((unsigned char)c);

    const bool all = (choice == "a" || choice == "all");
    int rc = 0, removed = 0;
    for (std::size_t i = 0; i < accounts.size(); ++i) {
        const bool pick = all || choice == std::to_string(i + 1);
        if (!pick) continue;
        if (accounts[i].clear()) {
            std::cout << "Removed " << accounts[i].path << "\n"; ++removed;
        } else {
            std::cerr << "Failed to remove " << accounts[i].path << "\n"; rc = 1;
        }
    }
    if (removed == 0 && rc == 0) std::cout << "Nothing removed.\n";
    return rc;
}

int cmd_status(Io io, const maya::unlocked& cap) {
    std::cout << "Credentials file: " << credentials_path().string() << "\n";
    using namespace agentty::util;
    if (env::get_or_null<env::Var::AnthropicApiKey>()) {
        std::cout << env::name<env::Var::AnthropicApiKey>()
                  << ": set (will be used, overrides file)\n";
    }
    if (env::get_or_null<env::Var::ClaudeOAuthToken>()) {
        std::cout << env::name<env::Var::ClaudeOAuthToken>()
                  << ": set (OAuth via env)\n";
    }
    auto loaded = load_credentials(io);
    if (!loaded) {
        std::cout << "Saved credentials: (none)\n";
    } else {
        std::cout << "Saved method: " << persist_tag(*loaded) << "\n";
        if (auto* o = std::get_if<cred::OAuth>(&*loaded)) {
            if (o->expires_at_ms) {
                auto remaining_s = (o->expires_at_ms - now_ms()) / 1000;
                if (remaining_s <= 0) std::cout << "Token: expired\n";
                else std::cout << "Token expires in " << remaining_s << "s\n";
            } else {
                std::cout << "Token: no expiration info\n";
            }
            std::cout << "Refresh token: "
                      << (o->refresh_token.empty() ? "(none)" : "present") << "\n";
        }
        // At-rest 凭据文件的加密模式。
        std::cout << "At-rest encryption: aes-256-gcm ("
                  << (crypt::passphrase_active()
                        ? "machine + passphrase"
                        : "machine-bound; set AGENTTY_PASSPHRASE or "
                          "AGENTTY_ENCRYPT_PASSPHRASE=1 to add a passphrase")
                  << ")\n";
        // OS密钥库支持(通过AGENTTY_USE_KEYSTORE的opt-in)。
        std::cout << "OS keystore: " << keystore::backend_name();
        if (!keystore::available())
            std::cout << " (set AGENTTY_USE_KEYSTORE=1 to enable)";
        std::cout << "\n";
    }

    // ── ChatGPT / Codex OAuth( 单独存储) (OAAAA) ──────────────────────────
    {
        namespace codex = provider::chatgpt;
        std::cout << "\nChatGPT (Codex) file: "
                  << codex::codex_credentials_path().string() << "\n";
        if (auto cc = codex::load_codex_credentials()) {
            std::cout << "Signed in via ChatGPT OAuth\n";
            if (!cc->account_id.empty())
                std::cout << "Account id: " << cc->account_id << "\n";
            if (cc->expires_at_ms) {
                auto remaining_s = (cc->expires_at_ms - now_ms()) / 1000;
                if (remaining_s <= 0)
                    std::cout << "Token: expired (auto-refreshes on next use)\n";
                else
                    std::cout << "Token expires in " << remaining_s << "s\n";
            }
            std::cout << "Refresh token: "
                      << (cc->refresh_token.empty() ? "(none)" : "present")
                      << "\n";
        } else {
            std::cout << "ChatGPT: (not signed in — `agentty login` → 3)\n";
        }
    }

    // ── GitHub 副驾驶 OAuth( 单独存储) ───────────────────────
    {
        std::cout << "\nGitHub Copilot file: "
                  << provider::copilot::credentials_path().string() << "\n";
        if (provider::copilot::signed_in()) {
            std::cout << "Signed in via GitHub Copilot OAuth\n";
            // 探测代理令牌，以便状态反映真实可用性+主机。
            if (auto t = provider::copilot::fresh_token(cap)) {
                if (!t->chat_enabled)
                    std::cout << "Chat: NOT entitled on this account\n";
                else if (t->quota_exhausted)
                    std::cout << "Chat: quota exhausted (free tier)\n";
                else
                    std::cout << "Chat: available\n";
                if (!t->sku.empty())
                    std::cout << "Plan (sku): " << t->sku << "\n";
                if (!t->endpoint_api.empty())
                    std::cout << "Inference host: " << t->endpoint_api << "\n";
                if (t->expires_at_ms) {
                    auto remaining_s = (t->expires_at_ms - now_ms()) / 1000;
                    std::cout << "Session token "
                              << (remaining_s <= 0
                                    ? std::string{"stale (auto-refreshes on next use)"}
                                    : "refreshes in " + std::to_string(remaining_s) + "s")
                              << "\n";
                }
            } else {
                std::cout << "Session token: could not exchange — the GitHub sign-"
                             "in may be revoked or lack a Copilot subscription\n";
            }
        } else {
            std::cout << "GitHub Copilot: (not signed in — `agentty login` → 4)\n";
        }
    }
    return 0;
}

} // namespace agentty::auth
