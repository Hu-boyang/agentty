// copilot_oauth.cpp —GitHub副驾驶设备-flow登录+代理-token交换。
//
// 参见copilot_oauth.hpp。结构镜chatgpt/codex_oauth.cpp；
// 流程更简单（GitHub的普通设备流程：没有PKCE，没有环回回调服务器）。
//
// 常量和怪癖是针对 VS Code 自己的 vscode-copilot-chat
// 令牌管理器的 cross-referenced 以及十几个third-party 集成的
// 失败报告（参见 docs/design/copilot-provider.md）：
//   • CLIENT_ID 必须 为 VS 代码的 GitHub 应用 ID — 交易所仅接受
//     应用程序(ghu_)令牌，并且服务器将其MODEL ALLOWLIST键入到该id。
//   ·将推断路由到令牌响应的`endpoints.api`，绝不是
//     硬编码主机(Individual/Business/Enterprise不同；猜测404s)。
//   • Editor-Version + Copilot-Integration-Id 头文件 对每个
//     调用 — 但那些存在于 传输层 端点上，而不是在这里。

#include "agentty/provider/copilot/copilot_oauth.hpp"
#include "agentty/util/lock_levels.hpp"

#include <chrono>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <thread>

#ifndef _WIN32
#include <unistd.h>   // getpid — 每个进程的唯一临时名称
#else
#include <process.h>  // _getpid
#define getpid _getpid
#endif

#include <nlohmann/json.hpp>

#include "agentty/auth/auth.hpp"
#include "agentty/auth/cred_crypt.hpp"
#include "agentty/util/base64.hpp"
#include "agentty/util/user_root.hpp"   // user_cache_dir——支撑缓存
#include "agentty/io/http.hpp"
#include "agentty/io/shared_file.hpp"
#include "agentty/util/dbglog.hpp"
#include "agentty/util/logx.hpp"

#include <maya/runtime.hpp>

#ifndef _WIN32
#  include <sys/stat.h>   // chmod
#endif


namespace agentty::provider::copilot {
namespace {
using json = nlohmann::json;
using auth::OAuthError;
using auth::OAuthErrorKind;
namespace fs = std::filesystem;

// ── 核实的常数(docs/design/copilot-provider.md§2) ───────────────
constexpr const char* kClientId    = "Iv1.b507a08c87ecfe98";   // VS 代码 GitHub 应用程序
constexpr const char* kGithubHost  = "github.com";
constexpr const char* kApiHost     = "api.github.com";
constexpr const char* kDevicePath  = "/login/device/code";
constexpr const char* kTokenPath   = "/login/oauth/access_token";
constexpr const char* kExchangePath= "/copilot_internal/v2/token";
constexpr const char* kScope       = "read:user";
constexpr const char* kGrantType   = "urn:ietf:params:oauth:grant-type:device_code";
// Editor/plugin/user-agent身份现在作为运行时-可重写访问器（
// client_editor_version()等）存在于copilot_oauth.hpp
// 中——副驾驶门模型在这些访问器上的推出，因此它们必须是可调整的而无需重建。
constexpr const char* kIntegration = "vscode-chat";
// 当令牌响应省略endpoints.api时回退推理基础。
constexpr const char* kDefaultApi  = "https://api.githubcopilot.com";

std::int64_t now_ms_impl() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string form_encode(const std::vector<std::pair<std::string, std::string>>& kv) {
    static const char* hex = "0123456789ABCDEF";
    auto esc = [](std::string_view s) {
        std::string out;
        for (unsigned char c : s) {
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
             || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
                out.push_back(static_cast<char>(c));
            else { out.push_back('%'); out.push_back(hex[(c>>4)&0xF]); out.push_back(hex[c&0xF]); }
        }
        return out;
    };
    std::string out;
    for (std::size_t i = 0; i < kv.size(); ++i) {
        if (i) out += '&';
        out += esc(kv[i].first); out += '='; out += esc(kv[i].second);
    }
    return out;
}

struct HttpResult { int status = 0; std::string body; std::string transport_error; };

// 一个HTTPS请求。 `host` 是 github.com (device/token) 或
// api.github.com（交换）。 GitHub 希望接受：application/json 以获得 JSON，而不是形成 正文。
HttpResult request(http::HttpMethod method, std::string_view host,
                   std::string_view path,
                   std::vector<std::pair<std::string, std::string>> headers,
                   std::string body = {}) {
    HttpResult r;
    http::Request req;
    req.method = method;
    req.host   = std::string{host};
    req.port   = 443;
    req.path   = std::string{path};
    req.headers = {
        {"accept",       "application/json"},
        {"user-agent",   client_user_agent()},
        {"editor-version", client_editor_version()},
        {"editor-plugin-version", client_plugin_version()},
    };
    for (auto& h : headers) req.headers.push_back({h.first, h.second});
    if (const auto& ov = http::agentty_oauth_host_override(); ov.active()) {
        req.dial_host = ov.host;
        req.dial_port = ov.port;
    }
    req.body = std::move(body);
    req.max_body_bytes = 1ull * 1024 * 1024;

    http::Timeouts tos;
    tos.connect = std::chrono::milliseconds(10'000);
    tos.total   = std::chrono::milliseconds(30'000);

    auto resp = http::default_client(::agentty::IoAccess::grant()).send(req, tos);
    if (!resp) { r.transport_error = resp.error().render(); return r; }
    r.status = resp->status;
    r.body   = std::move(resp->body);
    return r;
}

// ── Persistence ───────────────────────────────────────────────────────────
// 信用文件。每次读取和读取-modify-write都在store().with内部运行，
// 因此两个线程永远不会在其上交错。
struct CredsFile {};
maya::guarded<CredsFile>& store() { static maya::guarded<CredsFile> f; return f; }

// signed_in()缓存，以文件的(mtime，大小)为关键字。 锁-free。
struct SignedInCache {
    long long      mtime      = 0;
    std::uintmax_t size       = static_cast<std::uintmax_t>(-1);
    bool           signed_in  = false;
};
maya::snapshot<SignedInCache>& signed_in_cache() {
    static maya::snapshot<SignedInCache> s{SignedInCache{}};
    return s;
}

struct Stored {
    GithubToken github;
    // 缓存的代理令牌(best-effort；re-exchanged，如果missing/stale)。
    CopilotToken proxy;
};

fs::path creds_path() { return auth::config_dir() / "copilot_credentials.json"; }

std::optional<Stored> load_unlocked() {
    std::ifstream ifs(creds_path(), std::ios::binary);
    if (!ifs) return std::nullopt;
    std::string raw{std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>()};
    if (raw.empty()) return std::nullopt;
    if (auth::crypt::looks_sealed(raw)) {
        auto pt = auth::crypt::unseal(raw);
        if (!pt) return std::nullopt;
        raw = std::move(*pt);
    }
    try {
        auto j = json::parse(raw);
        Stored s;
        s.github.access_token = j.value("github_token", std::string{});
        s.github.token_type   = j.value("token_type", std::string{"bearer"});
        s.github.scope        = j.value("scope", std::string{});
        if (s.github.access_token.empty()) return std::nullopt;
        if (j.contains("proxy") && j["proxy"].is_object()) {
            const auto& p = j["proxy"];
            s.proxy.token         = p.value("token", std::string{});
            s.proxy.expires_at_ms = p.value("expires_at_ms", std::int64_t{0});
            s.proxy.endpoint_api  = p.value("endpoint_api", std::string{});
            s.proxy.sku           = p.value("sku", std::string{});
            s.proxy.chat_enabled  = p.value("chat_enabled", true);
        }
        return s;
    } catch (...) { return std::nullopt; }
}

bool save_unlocked(const Stored& s) {
    json j;
    j["github_token"] = s.github.access_token;
    j["token_type"]   = s.github.token_type;
    j["scope"]        = s.github.scope;
    if (s.proxy.valid()) {
        j["proxy"] = {
            {"token",         s.proxy.token},
            {"expires_at_ms", s.proxy.expires_at_ms},
            {"endpoint_api",  s.proxy.endpoint_api},
            {"sku",           s.proxy.sku},
            {"chat_enabled",  s.proxy.chat_enabled},
        };
    }
    std::string payload = j.dump();
    std::string to_write = payload;
    if (auto sealed = auth::crypt::seal(payload)) to_write = std::move(*sealed);
    return persistence::write_atomic(::agentty::IoAccess::grant(), creds_path(), to_write,
                                     {.durability = persistence::Durability::Durable,
                                      .mode = 0600});
}

// ── 设备流 ─────────────────────────────────────────────────────────────
std::expected<DeviceCode, OAuthError> request_device_code(std::string& out_device_code,
                                                          int& out_interval) {
    auto r = request(http::HttpMethod::Post, kGithubHost, kDevicePath,
        {{"content-type", "application/x-www-form-urlencoded"}},
        form_encode({{"client_id", kClientId}, {"scope", kScope}}));
    if (!r.transport_error.empty())
        return std::unexpected(OAuthError{OAuthErrorKind::Network, r.transport_error});
    if (r.status >= 400)
        return std::unexpected(OAuthError{OAuthErrorKind::ApiError,
            "GitHub device-code request failed (HTTP " + std::to_string(r.status) + ")"});
    try {
        auto j = json::parse(r.body);
        DeviceCode dc;
        dc.verification_uri = j.value("verification_uri", "https://github.com/login/device");
        dc.user_code        = j.value("user_code", std::string{});
        dc.expires_in       = j.value("expires_in", 900);
        out_device_code     = j.value("device_code", std::string{});
        out_interval        = j.value("interval", 5);
        if (dc.user_code.empty() || out_device_code.empty())
            return std::unexpected(OAuthError{OAuthErrorKind::BadResponse,
                "incomplete device-code response"});
        return dc;
    } catch (const std::exception& e) {
        return std::unexpected(OAuthError{OAuthErrorKind::BadResponse, e.what()});
    }
}

std::expected<GithubToken, OAuthError>
poll_for_token(const maya::unlocked& cap, const std::string& device_code, int interval_s,
               int timeout_s, const CancelProbe& cancelled) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_s);
    int interval = interval_s > 0 ? interval_s : 5;
    while (std::chrono::steady_clock::now() < deadline) {
        if (cancelled.stop_requested())
            return std::unexpected(OAuthError{OAuthErrorKind::Network, "login cancelled"});
        // 服务器的间隔，在登录时刻缩短为已取消。
        if (maya::delay_for(cap, cancelled, std::chrono::seconds(interval)))
            return std::unexpected(OAuthError{OAuthErrorKind::Network, "login cancelled"});
        auto r = request(http::HttpMethod::Post, kGithubHost, kTokenPath,
            {{"content-type", "application/x-www-form-urlencoded"}},
            form_encode({{"client_id", kClientId},
                         {"device_code", device_code},
                         {"grant_type", kGrantType}}));
        if (!r.transport_error.empty())
            return std::unexpected(OAuthError{OAuthErrorKind::Network, r.transport_error});
        try {
            auto j = json::parse(r.body);
            if (j.contains("access_token")) {
                GithubToken t;
                t.access_token = j.value("access_token", std::string{});
                t.token_type   = j.value("token_type", std::string{"bearer"});
                t.scope        = j.value("scope", std::string{});
                if (t.access_token.empty())
                    return std::unexpected(OAuthError{OAuthErrorKind::MissingToken,
                        "empty access_token"});
                return t;
            }
            const std::string err = j.value("error", std::string{});
            if (err == "authorization_pending") continue;         // 继续等待
            if (err == "slow_down") { interval += 5; continue; }  // 退后
            if (err == "expired_token")
                return std::unexpected(OAuthError{OAuthErrorKind::ApiError,
                    "the device code expired — start sign-in again for a new code"});
            if (err == "access_denied")
                return std::unexpected(OAuthError{OAuthErrorKind::ApiError,
                    "sign-in was denied"});
            if (!err.empty())
                return std::unexpected(OAuthError{OAuthErrorKind::ApiError, err});
        } catch (...) { /* 瞬态 解析 光点 — 继续轮询 */ }
    }
    return std::unexpected(OAuthError{OAuthErrorKind::Network,
        "device-code login timed out; start sign-in again for a new code"});
}

// ── 托肯交换( ghu_ 代理代用符) ─────────────────────────────────────
std::expected<CopilotToken, OAuthError> exchange(const GithubToken& gh) {
    // NOTE:GitHub想要这里的`Authorization: token <ghu_>` (不 "Bearer")。
    auto r = request(http::HttpMethod::Get, kApiHost, kExchangePath,
        {{"authorization", "token " + gh.access_token},
         {"copilot-integration-id", kIntegration}});
    if (!r.transport_error.empty())
        return std::unexpected(OAuthError{OAuthErrorKind::Network, r.transport_error});
    if (r.status == 401 || r.status == 403)
        return std::unexpected(OAuthError{OAuthErrorKind::ApiError,
            "GitHub rejected the Copilot token exchange (HTTP " + std::to_string(r.status)
            + ") — your GitHub account may not have a Copilot subscription, "
              "or the sign-in was revoked. Re-run `agentty login`."});
    if (r.status == 404)
        return std::unexpected(OAuthError{OAuthErrorKind::ApiError,
            "Copilot token exchange returned 404 — this account has no Copilot "
            "entitlement on this endpoint."});
    if (r.status >= 400)
        return std::unexpected(OAuthError{OAuthErrorKind::ApiError,
            "Copilot token exchange failed (HTTP " + std::to_string(r.status) + ")"});
    auto tok = parse_token_envelope(r.body, now_ms_impl());
    if (!tok)
        return std::unexpected(OAuthError{OAuthErrorKind::MissingToken,
            "no token in Copilot exchange response"});
    return *tok;
}

std::atomic<bool> g_force_refresh{false};

} // 命名空间结束。

std::int64_t CopilotToken::now_ms() noexcept { return now_ms_impl(); }

std::optional<CopilotToken>
parse_token_envelope(std::string_view json_body, std::int64_t now_ms) {
    try {
        auto j = json::parse(json_body);
        CopilotToken t;
        t.token = j.value("token", std::string{});
        if (t.token.empty()) return std::nullopt;
        // Skew-safe LOCAL到期：偏好refresh_in(VS
        // Code自己的策略)并添加松弛；回退到expires_at；否则是保守的默认值。
        if (const auto refresh_in = j.value("refresh_in", std::int64_t{0}); refresh_in > 0)
            t.expires_at_ms = now_ms + (refresh_in + 60) * 1000;
        else if (const auto exp = j.value("expires_at", std::int64_t{0}); exp > 0)
            t.expires_at_ms = exp * 1000;
        else
            t.expires_at_ms = now_ms + 25 * 60 * 1000;
        // 权威推理主机-Individual/Business/Enterprise不同。
        if (j.contains("endpoints") && j["endpoints"].is_object())
            t.endpoint_api = j["endpoints"].value("api", std::string{});
        if (t.endpoint_api.empty()) t.endpoint_api = kDefaultApi;
        t.sku          = j.value("sku", std::string{});
        t.chat_enabled = j.value("chat_enabled", true);
        if (j.contains("limited_user_quotas") && j["limited_user_quotas"].is_object())
            t.quota_exhausted = j["limited_user_quotas"].value("chat", 1) == 0;
        return t;
    } catch (...) { return std::nullopt; }
}

// ── 公共 API ──────────────────────────────────────────────────────────────
fs::path credentials_path() { return creds_path(); }

std::optional<GithubToken> load_github_token(const maya::below<maya::leaf>& cap) {
    return store().with(cap, [](CredsFile&) -> std::optional<GithubToken> {
        auto s = load_unlocked();
        if (!s) return std::nullopt;
        return s->github;
    });
}

bool save_github_token(const maya::below<maya::leaf>& cap, const GithubToken& gh) {
    return store().with(cap, [](CredsFile&, GithubToken g) {
        Stored s;
        if (auto cur = load_unlocked()) s = *cur;
        s.github = std::move(g);
        s.proxy = {};   // 新的标志-in使任何缓存的代理令牌无效
        return save_unlocked(s);
    }, gh);
}

bool clear_credentials(Io, const maya::below<maya::leaf>& cap) {
    return store().with(cap, [](CredsFile&) {
        std::error_code ec;
        return fs::remove(creds_path(), ec) || !ec;
    });
}

bool signed_in() {
    // 每次呈现的帧由提供者-picker VIEW调用一次。幼稚的实现（读取
    // 文件 → 解封 → JSON 解析）在 选择面板 打开时，
    // 每个 帧 都会消耗磁盘 + AES/KDF 工作量。 缓存 文件的布尔值
    // （mtime、大小）：统计数据约为 1μs，并且在 sign-in、
    // sign-out 和 cross-process 凭据 更改上正确无效。
    // 锁-free（视图不能容纳能力）：文件的读取不需要锁，
    // 因为写入者用原子重命名来替换它。
    std::error_code ec;
    const auto p = creds_path();
    const auto t = fs::last_write_time(p, ec);
    const auto size = ec ? 0 : fs::file_size(p, ec);
    if (ec) {   // missing/unreadable→已注销；记得便宜
        signed_in_cache().store(SignedInCache{});
        return false;
    }
    const long long mtime = static_cast<long long>(t.time_since_epoch().count());
    const auto cur = signed_in_cache().get();
    if (mtime == cur.mtime && size == cur.size) return cur.signed_in;
    auto s = load_unlocked();
    const bool in = s && !s->github.access_token.empty();
    signed_in_cache().store(SignedInCache{mtime, size, in});
    return in;
}

void invalidate_cached_token() { g_force_refresh.store(true); }

// ── 人数 -account 模型 -support 学习 ─────────────────────────────────
namespace {
// 支持文件；它的 read-modify-writes 在 support_file().with 内部运行。
struct SupportFile {};
maya::guarded<SupportFile>& support_file() { static maya::guarded<SupportFile> f; return f; }
// cache/、不 credentials/.
//
// 这是可重新获取的学习状态——该状态模型被告知该帐户可以
// 使用——并且它登陆到秘密目录中纯粹是因为该文件已经在范围
// 内具有auth::config_dir()。有两点错误：
// 这是 0600 审计应该没有发现异常的一个目录（该文件位于七个 0600 同级文件
// 中的 0644），并且 "delete your cache" 绝不应该意味着
// "go near your tokens"。
//
// 没有任何东西迁移旧副本：它是缓存，因此丢失的文件需要在下
// 一个请求时重新学习，并且将过期留在0700目录中不会有
// 任何损害。该 blob 保存模型 ID，而不是 凭据。
fs::path support_path() {
    return ::agentty::util::user_cache_dir() / "copilot_model_support.json";
}

struct SupportSets { std::set<std::string> unsupported, supported; };

SupportSets load_support() {
    SupportSets s;
    std::ifstream ifs(support_path());
    if (!ifs) return s;
    try {
        auto j = json::parse(std::string{std::istreambuf_iterator<char>(ifs),
                                          std::istreambuf_iterator<char>()});
        for (auto& e : j.value("unsupported", json::array()))
            if (e.is_string()) s.unsupported.insert(e.get<std::string>());
        for (auto& e : j.value("supported", json::array()))
            if (e.is_string()) s.supported.insert(e.get<std::string>());
    } catch (const std::exception& e) {
        util::dbglog("copilot.support_cache.parse", e.what());
    } catch (...) {}
    return s;
}

void save_support(const SupportSets& s) {
    json j;
    j["unsupported"] = json::array();
    for (auto& id : s.unsupported) j["unsupported"].push_back(id);
    j["supported"] = json::array();
    for (auto& id : s.supported) j["supported"].push_back(id);
    const std::string payload = j.dump();
    // A 缓存：丢失的写入只是重新学习该集合，因此没有 fsync。
    (void)persistence::write_atomic(::agentty::IoAccess::grant(), support_path(), payload,
                                    {.durability = persistence::Durability::Atomic});
}
} // 命名空间结束。

void note_unsupported_model(const maya::below<maya::leaf>& cap, const std::string& model_id) {
    if (model_id.empty()) return;
    support_file().with(cap, [](SupportFile&, std::string id) {
        auto s = load_support();
        s.supported.erase(id);
        if (s.unsupported.insert(id).second) save_support(s);
    }, model_id);
}
bool is_unsupported_model(const maya::below<maya::leaf>& cap, const std::string& model_id) {
    return support_file().with(cap, [](SupportFile&, std::string id) {
        return load_support().unsupported.count(id) > 0;
    }, model_id);
}
void note_supported_model(const maya::below<maya::leaf>& cap, const std::string& model_id) {
    if (model_id.empty()) return;
    support_file().with(cap, [](SupportFile&, std::string id) {
        auto s = load_support();
        bool changed = s.unsupported.erase(id) > 0;
        changed |= s.supported.insert(id).second;
        if (changed) save_support(s);
    }, model_id);
}
bool is_supported_model(const maya::below<maya::leaf>& cap, const std::string& model_id) {
    return support_file().with(cap, [](SupportFile&, std::string id) {
        return load_support().supported.count(id) > 0;
    }, model_id);
}

std::expected<GithubToken, OAuthError>
login(const maya::unlocked& cap, int timeout_s, DeviceCodeSink on_device_code,
      CancelProbe cancelled) {
    if (cancelled.stop_requested())
        return std::unexpected(OAuthError{OAuthErrorKind::Network, "login cancelled"});

    std::string device_code;
    int interval = 5;
    auto dc = request_device_code(device_code, interval);
    if (!dc) return std::unexpected(dc.error());

    if (on_device_code) on_device_code(*dc);

    auto tok = poll_for_token(cap, device_code, interval, timeout_s, cancelled);
    if (!tok) return std::unexpected(tok.error());

    // 保留持久的GitHub令牌；代理令牌在首次使用时通过 fresh_token
    // () 进行延迟交换。
    save_github_token(cap, *tok);
    return *tok;
}

namespace {
std::optional<CopilotToken> fresh_token_locked(const maya::held<locks::auth_refresh>& h);
}

std::optional<CopilotToken> fresh_token(const locks::below_auth_refresh& cap) {
    // 一次刷新一次。存储（叶子）仅保存文件 IO，
    // 从不跨网络保存 调用。 锁顺序，由编译器检查：刷新
    // 通道（auth.refresh），然后是存储（叶子）。
    struct RefreshLane {};
    static maya::guarded<RefreshLane, locks::auth_refresh> one_at_a_time;
    return one_at_a_time.with(cap, [](const maya::held<locks::auth_refresh>& h, RefreshLane&) {
        return fresh_token_locked(h);
    });
}

namespace {
std::optional<CopilotToken> exchange_in_section(const maya::held<locks::cred_file>& h,
                                                bool cross, bool force, Stored loaded) {
    // 在锁下，对端可能已经被交换；采用其代币。
    if (cross && !force) {
        auto disk = store().with(h, [](CredsFile&) { return load_unlocked(); });
        if (disk && disk->proxy.valid()
            && !disk->proxy.expired(/*skew_ms=*/5 * 60 * 1000)) {
            return disk->proxy;   // 对端 在我们等待时刷新
        }
    }

    auto exchanged = exchange(loaded.github);
    if (!exchanged) return std::nullopt;

    store().with(h, [](CredsFile&, Stored s) {
        if (auto cur = load_unlocked()) s.github = cur->github;   // 采用最新ghu_
        (void)save_unlocked(s);
    }, Stored{loaded.github, *exchanged});
    return *exchanged;
}

std::optional<CopilotToken> fresh_token_locked(const maya::held<locks::auth_refresh>& h) {
    auto loaded = store().with(h, [](CredsFile&) { return load_unlocked(); });
    if (!loaded || loaded->github.access_token.empty()) return std::nullopt;

    const bool force = g_force_refresh.exchange(false);
    if (!force && loaded->proxy.valid()
        && !loaded->proxy.expired(/*skew_ms=*/5 * 60 * 1000))
        return loaded->proxy;

    return persistence::with_shared_file(h, persistence::cred_lane(creds_path()), creds_path(),
        [](const maya::held<locks::cred_file>& c, persistence::Held held, bool f, Stored s) {
            return exchange_in_section(c, held.cross_process, f, std::move(s));
        }, force, *loaded);
}
} // 命名空间结束。

Entitlement account_entitlement(const maya::below<maya::leaf>& cap) {
    // 短于-process 缓存:选择面板可在调用此重复打开时。
    // 叶，仅被保持以复制入或复制出：网络获取在读取和
    // 写入之间运行，从不在锁下运行。
    struct Cached { Entitlement e; std::int64_t fetched_at = 0; };
    static maya::guarded<Cached> cache;
    if (auto hit = cache.read(cap, [](const Cached& c) -> std::optional<Entitlement> {
            if (c.e.known && now_ms_impl() - c.fetched_at < 5 * 60 * 1000) return c.e;
            return std::nullopt;
        }))
        return *hit;
    Entitlement e;   // 默认情况下已知 = false → 允许 回退
    auto gh = load_github_token(cap);
    if (!gh) return e;

    // GET api.github.com/copilot_internal/user，使用 ghu_ 令牌获取
    // 每个账号的权威套餐与配额快照。
    auto r = request(http::HttpMethod::Get, kApiHost, "/copilot_internal/user",
        {{"authorization", "token " + gh->access_token},
         {"copilot-integration-id", kIntegration}});
    if (!r.transport_error.empty() || r.status < 200 || r.status >= 300)
        return e;
    try {
        auto j = json::parse(r.body);
        e.plan         = j.value("copilot_plan", "");
        e.sku          = j.value("access_type_sku", "");
        e.chat_enabled = j.value("chat_enabled", true);
        // 高级型号(Claude/Gemini/GPT-5.x/o-series)
        // 从premium_interactions配额中抽取。 entitlement
        // ==0 && !unlimited && 无配额 ⇒ 此帐户无法运行它们（
        // 免费层）。任何积极的权利，无限制或允许的超额⇒可用保费。
        e.premium_available = true;
        if (j.contains("quota_snapshots") && j["quota_snapshots"].is_object()) {
            const auto& qs = j["quota_snapshots"];
            if (qs.contains("premium_interactions") && qs["premium_interactions"].is_object()) {
                const auto& p = qs["premium_interactions"];
                const bool unlimited = p.value("unlimited", false);
                const double ent     = p.value("entitlement", 0.0);
                const bool overage   = p.value("overage_permitted", false);
                e.premium_available = unlimited || ent > 0.0 || overage;
            }
        }
        e.known = true;
    } catch (...) { return Entitlement{}; }

    cache.with(cap, [](Cached& c, Entitlement fresh) {
        c.e = std::move(fresh);
        c.fetched_at = now_ms_impl();
    }, e);
    return e;
}

namespace {
// 在不验证的情况下从 JWT 中提取 `exp` (unix 秒)
// 声明（服务器对其进行签名；我们只需要到期时间来知道何时刷新）。
std::int64_t jwt_exp_ms(const std::string& jwt) {
    auto a = jwt.find('.');
    if (a == std::string::npos) return 0;
    auto b = jwt.find('.', a + 1);
    if (b == std::string::npos) return 0;
    std::string payload = jwt.substr(a + 1, b - a - 1);
    for (auto& c : payload) { if (c == '-') c = '+'; else if (c == '_') c = '/'; }
    while (payload.size() % 4) payload.push_back('=');
    try {
        auto raw = util::base64_decode(payload);
        auto j = json::parse(raw);
        if (auto e = j.value("exp", std::int64_t{0}); e > 0) return e * 1000;
    } catch (const std::exception& e) {
        util::dbglog("copilot.jwt_exp.parse", e.what());
    } catch (...) {}
    return 0;
}

// 自动会话缓存及其失败退避。 auto_session()
// 故意在其网络 调用 上保留这一点，因此并发轮次共享一个探针。
//
// 锁顺序：copilot.auto_session(12)，
// 然后是fresh_token的刷新通道(auth.refresh，15)，
// 然后是信用存储(叶)。 正文 占用刷新通道，因此 锁 必须 排名低于它 - 它
// 曾经是默认叶子，从叶子 正文 中取出 auth.refresh 是启动中止。
// 该形状现在是编译错误：auto_session_locked只能与给定的hold
// <copilot_auto>一起调用 fresh_token，仅因为12
// < 15才转换为低于<auth_refresh>。
struct AutoState {
    AutoSession  cache;
    std::int64_t failed_until_ms = 0;   // 0 = 未配备 退避
};
using AutoLock = maya::guarded<AutoState, locks::copilot_auto>;
AutoLock& auto_state() { static AutoLock s; return s; }

std::optional<AutoSession> auto_session_locked(const maya::held<locks::copilot_auto>& h,
                                               AutoState& st);
} // 命名空间结束。

std::optional<AutoSession> auto_session(const locks::below_copilot_auto& cap) {
    return auto_state().with(cap, [](const maya::held<locks::copilot_auto>& h, AutoState& st) {
        return auto_session_locked(h, st);
    });
}

namespace {
std::optional<AutoSession> auto_session_locked(const maya::held<locks::copilot_auto>& h,
                                               AutoState& st) {
    if (st.cache.valid()) return st.cache;

    // NEGATIVE CACHE。下面的所有内容都阻止在自动 锁 下举行的网络
    // I/O，并且 FAILED 尝试用于 缓存 没有任何内容 - 因此无法打开自动会话
    // 的帐户（无权利，组织政策，片状 5xx）在 每个 轮上 re-dialled
    // /models/session，并且每次轮次都在第一次之前支付了完整的
    // TLS 握手 + round-trip令牌。
    // 那就是 "weirdly hangs for a second" 报告。
    //
    // 相反，后退：记住上次失败并直接从 内存 回答空选择以获得短窗口。
    // 足够长的时间使得轮次永远不会在已知的-bad端点上等待，
    // 足够短的时间使得修复计划或等待突发事件的用户无需重新启动即可恢复。
    // invalidate_auto_session() 会清除它，
    // 因此立即进行显式帐户切换或 401 重试 re-probes。
    const auto now = CopilotToken::now_ms();
    if (now < st.failed_until_ms) return std::nullopt;
    constexpr std::int64_t kBackoffMs = 60'000;
    // 任何低于以下的提前返回均属失败；将 退避 放在前面，
    // 这样任何路径都不会忘记它。
    st.failed_until_ms = now + kBackoffMs;

    auto tok = fresh_token(h);
    if (!tok || !tok->chat_enabled) {
        AGT_LOG(Auth, Warn, "copilot.auto_session.skipped",
                "reason={} backoff_ms=60000",
                !tok ? "no_capi_token" : "chat_disabled");
        return std::nullopt;
    }

    std::string host = tok->endpoint_api;
    if (host.rfind("https://", 0) == 0) host = host.substr(8);
    if (auto slash = host.find('/'); slash != std::string::npos) host = host.substr(0, slash);

    // POST {api}/models/session with the CAPI api-version — the server returns
    // 每个账号可用的模型，以及签名后的会话令牌。
    auto r = request(http::HttpMethod::Post, host, "/models/session",
        {{"authorization", "Bearer " + tok->token},
         {"copilot-integration-id", kIntegration},
         {"x-github-api-version", client_api_version()},
         {"content-type", "application/json"}},
        R"({"auto_mode":{"model_hints":["auto"]}})");
    if (!r.transport_error.empty() || r.status < 200 || r.status >= 300) {
        // 该故障会带来负面的缓存（见上文）——账户将默默
        // 地为退避窗口运行WITHOUT自动会话，
        // 这改变了哪些dialect/models是可到达的。
        // 该状态切换必须在共享日志中可见，并附有服务器的原因。
        AGT_LOG(Auth, Warn, "copilot.auto_session.failed",
                "host={} status={} transport={} body={}", host, r.status,
                r.transport_error.empty() ? "-" : r.transport_error,
                std::string_view{r.body}.substr(0, 512));
        return std::nullopt;
    }
    try {
        auto j = json::parse(r.body);
        AutoSession s;
        s.session_token  = j.value("session_token", "");
        s.selected_model = j.value("selected_model", "");
        s.endpoint_api   = tok->endpoint_api;
        if (j.contains("available_models") && j["available_models"].is_array())
            for (auto& m : j["available_models"])
                if (m.is_string()) s.available_models.push_back(m.get<std::string>());
        if (s.session_token.empty()) {
            AGT_LOG(Auth, Warn, "copilot.auto_session.no_token",
                    "status={} body={}", r.status,
                    std::string_view{r.body}.substr(0, 512));
            return std::nullopt;
        }
        s.expires_at_ms = jwt_exp_ms(s.session_token);
        st.cache = s;
        st.failed_until_ms = 0;   // 成功 — 解除 退避 的武装
        return s;
    } catch (const std::exception& e) {
        AGT_LOG(Auth, Warn, "copilot.auto_session.parse", "err={}", e.what());
        return std::nullopt;
    } catch (...) { return std::nullopt; }
}
} // 命名空间结束。

void invalidate_auto_session(const locks::below_copilot_auto& cap) {
    // 还清除负缓存:显式失效(账户切换，401重试)
    // 意味着世界改变了，所以立即重新-probe。
    auto_state().with(cap, [](AutoState& st) { st = {}; });
}

} // namespace agentty::provider::copilot
