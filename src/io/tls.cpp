#include "agentty/io/tls.hpp"
#include "agentty/util/env.hpp"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <maya/runtime.hpp>

#if defined(__APPLE__)
#  include <Security/Security.h>
#  include <CoreFoundation/CoreFoundation.h>
#elif defined(_WIN32)
// 在 windows.h 之前声明以安静 min/max + cert cruft。
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <wincrypt.h>
#  ifdef _MSC_VER
#    pragma comment(lib, "crypt32.lib")
#  endif
#endif

namespace agentty::tls {

namespace {

// ALPN广告：h2第一，http/1.1为回退。 Anthropic
// 的边缘总是协商 h2，但广告 1.1 使我们与用户可能建立的
// 任何代理（公司 TLS-intercepting 中间盒）兼容。
// 传输格式：根据RFC 7301的长度-prefixed协议ID。
constexpr unsigned char kAlpn[] = {
    2, 'h', '2',
    8, 'h', 't', 't', 'p', '/', '1', '.', '1',
};

// --------------------------------------------------------------------------
// Client-side TLS会话恢复。
//
// SSL_SESS_CACHE_CLIENT启用OpenSSL的内部客户端缓存，
// 但是在客户端OpenSSL在SSL_new上执行NOT auto-attach缓存会话—
// —在握手之前你必须自己调用 SSL_set_session()，并且你必须CAPTURE
// 将会话OpenSSL交给你(TLS) 1.3 门票在 -handshake
// NewSessionTicket 帧 后通过新 -session 回调 到达。
// 如果没有两半，每个转盘都会进行一次完整的握手——恢复永远不会触发。
// 我们在一个微小的进程-wide映射中为每个SNI主机保留一个最-recent
// SSL_SESSION；单个 -endpoint 客户端 (
// api.anthropic.com) 仅具有几个不同的主机，因此平面防护地图就足够了。
//
// 恢复到 api.anthropic.com 是 1-RTT (TLS 1.3)
// 缩写握手，而不是完整的 2-RTT 交换 — 在连接 -pool 空闲 TTL
// 逐出活动套接字之后，每个新拨号上约 30-150 毫秒 已保存。
// --------------------------------------------------------------------------
// 一个拥有的对SSL_SESSION的参考。移动-only；在掉落时释放其引用。
class Ticket {
public:
    Ticket() = default;
    explicit Ticket(SSL_SESSION* s) noexcept : s_(s) {}
    Ticket(Ticket&& o) noexcept : s_(std::exchange(o.s_, nullptr)) {}
    Ticket& operator=(Ticket&& o) noexcept {
        if (this != &o) { reset(); s_ = std::exchange(o.s_, nullptr); }
        return *this;
    }
    // 复制=第二拥有的引用（OpenSSL会话引用计数
    // 是原子的），因此可以复制票证映射以用于快照更新。
    Ticket(const Ticket& o) noexcept : s_(o.s_) { if (s_) SSL_SESSION_up_ref(s_); }
    Ticket& operator=(const Ticket& o) noexcept {
        if (this != &o) { Ticket t{o}; *this = std::move(t); }
        return *this;
    }
    ~Ticket() { reset(); }

    // 对同一会话的第二拥有的引用。
    [[nodiscard]] Ticket share() const noexcept {
        if (s_) SSL_SESSION_up_ref(s_);
        return Ticket{s_};
    }
    [[nodiscard]] SSL_SESSION* get() const noexcept { return s_; }

private:
    void reset() noexcept { if (s_) SSL_SESSION_free(s_); s_ = nullptr; }
    SSL_SESSION* s_ = nullptr;
};
} // 命名空间结束。
} // namespace agentty::tls

// 票证拥有其自己的引用（OpenSSL会话引用计数是原子的），
// 因此它可以在线程之间移动。
MAYA_SENDABLE(agentty::tls::Ticket);
// 冻结：已建立的SSL_SESSION在捕获后不会被我们变异； OpenSSL
// 记录会议一旦分发即可在 SSL objects/threads 之间共享，
// 并且每个持有者只能阅读它（SSL_set_session up-refs）。
MAYA_FROZEN(agentty::tls::Ticket);

namespace agentty::tls {
namespace {
using Tickets = std::unordered_map<std::string, Ticket>;
// 故意泄露：退出时释放会话可以竞态 OpenSSL自己拆解。
//
// maya::snapshot，
// 而不是锁:wrap_client/new_session_cb在网络代码内部运行，
// 该网络代码本身是从排名锁 正文(e.g。auth_refresh通道在HTTPS上
// 刷新令牌)到达的，因此这里的锁将需要通过每个http 调用线程化的能力。
// 该映射由少量主机组成，每次握手都会更改一次：读取器执行一个原子 加载，写入器复制
// -and-publish (CAS 重试)。没有任何东西被持有，所以没有什么可以死锁。
maya::snapshot<Tickets>& tickets() {
    static auto* t = new maya::snapshot<Tickets>;
    return *t;
}

// ex_data槽携带SNI主机字符串指针直至new_session_cb(其仅
// 获取SSL*，并且SSL_get_servername仅是服务器-side)。
int sni_ex_index() {
    static int idx = SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
    return idx;
}

// 当建立可用的session/ticket时，由OpenSSL调用。
// 获取 `sess` 的 所有权（返回 1）并将其存储为该主机的最新版本，
// 释放任何先前的主机。 线程-safe。
int new_session_cb(SSL* ssl, SSL_SESSION* sess) {
    const auto* host = static_cast<const std::string*>(
        SSL_get_ex_data(ssl, sni_ex_index()));
    if (!host) return 0;   // 不是我们要保留的 — 让 OpenSSL 释放它
    // 我们现在拥有一名裁判（裁判员给我们一名裁判以保留）；旧的掉了。
    (void)tickets().update([](const Tickets& old, const std::string& h, const Ticket& t) {
        Tickets m = old;
        m[h] = t;
        return m;
    }, *host, Ticket{sess});
    return 1;
}

// --------------------------------------------------------------------------
// 平台根存储加载器。我们将 加载 置于 OpenSSL 的默认路径
// 之上（已涵盖常见的 Linux 情况），因此最终的 X509_STORE
// 是联合体 - 腰带和背带。
// --------------------------------------------------------------------------

#if defined(__APPLE__)
// macOS:枚举管理员+系统钥匙串信任设置。  任何标有
// kSecTrustSettingsResultTrustRoot 或 TrustAsRoot
// 的证书都会进入 OpenSSL 的存储。  并非所有根都具有显式信任设置（系统根使用来自
// /System/Library/Keychains/SystemRootCertificates
// 的隐式信任），因此我们也直接提取该钥匙串。
bool load_system_roots(SSL_CTX* ctx) {
    X509_STORE* store = SSL_CTX_get_cert_store(ctx);
    if (!store) return false;

    auto add_cfdata = [&](CFDataRef data) {
        const unsigned char* bytes =
            reinterpret_cast<const unsigned char*>(CFDataGetBytePtr(data));
        long len = static_cast<long>(CFDataGetLength(data));
        X509* x = d2i_X509(nullptr, &bytes, len);
        if (x) {
            // 忽略 success/failure — 重复是良性的，
            // 单个错误的证书不应破坏整个商店。
            X509_STORE_add_cert(store, x);
            X509_free(x);
        }
    };

    auto copy_from_domain = [&](SecTrustSettingsDomain domain) {
        CFArrayRef certs = nullptr;
        if (SecTrustSettingsCopyCertificates(domain, &certs) != errSecSuccess || !certs)
            return;
        CFIndex n = CFArrayGetCount(certs);
        for (CFIndex i = 0; i < n; ++i) {
            SecCertificateRef cert =
                static_cast<SecCertificateRef>(
                    const_cast<void*>(CFArrayGetValueAtIndex(certs, i)));
            if (CFDataRef data = SecCertificateCopyData(cert)) {
                add_cfdata(data);
                CFRelease(data);
            }
        }
        CFRelease(certs);
    };

    copy_from_domain(kSecTrustSettingsDomainSystem);
    copy_from_domain(kSecTrustSettingsDomainAdmin);
    copy_from_domain(kSecTrustSettingsDomainUser);
    return true;
}

#elif defined(_WIN32)
// Windows:将每个证书从系统ROOT存储中取出并添加到OpenSSL。
// 我们使用 CertEnumCertificatesInStore 而不是旧的 A-variant，
// 因此 unicode 证书名称不会让我们感到困惑。
bool load_system_roots(SSL_CTX* ctx) {
    X509_STORE* store = SSL_CTX_get_cert_store(ctx);
    if (!store) return false;

    HCERTSTORE sys = CertOpenSystemStoreW(0, L"ROOT");
    if (!sys) return false;

    PCCERT_CONTEXT ctxt = nullptr;
    while ((ctxt = CertEnumCertificatesInStore(sys, ctxt)) != nullptr) {
        const unsigned char* enc = ctxt->pbCertEncoded;
        long n = static_cast<long>(ctxt->cbCertEncoded);
        X509* x = d2i_X509(nullptr, &enc, n);
        if (x) {
            X509_STORE_add_cert(store, x);
            X509_free(x);
        }
    }
    CertCloseStore(sys, 0);
    return true;
}

#else
// Linux/BSD:OpenSSL的默认路径已经涵盖了这一点。  一些发行版
// 在奇怪的路径下运送可信的 CA 捆绑包（Alpine：/etc/ssl/cert.pem，
// 一些容器基地：/etc/pki/tls/certs/ca-bundle.crt）；
// SSL_CTX_set_default_verify_paths 处理通常的情况。
// 我们还尊重curl通常读取的两个环境变量，因此拥有公司代理证书的
// 用户可以向我们指出它，而无需更改代码。
bool load_system_roots(SSL_CTX* ctx) {
    if (SSL_CTX_set_default_verify_paths(ctx) != 1) {
        // 不是致命的：环境覆盖仍然可以在下面进行拍摄。
    }
    if (const char* f = std::getenv("SSL_CERT_FILE"); f && *f) {
        SSL_CTX_load_verify_locations(ctx, f, nullptr);
    }
    if (const char* d = std::getenv("SSL_CERT_DIR"); d && *d) {
        SSL_CTX_load_verify_locations(ctx, nullptr, d);
    }
    if (const char* f = std::getenv("CURL_CA_BUNDLE"); f && *f) {
        SSL_CTX_load_verify_locations(ctx, f, nullptr);
    }
    return true;
}
#endif

// --------------------------------------------------------------------------
// Opt-in SPKI公共-key固定(SECURITY_AUDIT)。
//
// 标准链+主机名验证已经可以防御随机的错误证书。固定还可防御
// API 主机的错误-issued-but-validly-
// 链式证书（受损或强制的 CA）。我们默认使用 不 固定 -
// 静默服务器 -side 密钥轮换会阻塞每个无法及时更新的客户端。
// 相反，通过 env 固定为 OPT-IN：
//
//   AGENTTY_TLS_PINS="base64sha256spki[,base64sha256spki...]"
//
// 每个引脚是 base64(SHA-256(DER SubjectPublicKeyInfo))
// — 与 HPKP "pin-sha256" /
// `openssl ... | openssl dgst -sha256 -binary | openssl enc -base64`
// 配方的值相同。如果配置的 任意 引脚与叶证书的 SPKI 匹配，则连接继续；
// 如果没有匹配，即使链在其他方面有效，握手也会被拒绝。套件包括一个备用销钉。
// --------------------------------------------------------------------------
const std::vector<std::string>& configured_pins() {
    static const std::vector<std::string> pins = [] {
        std::vector<std::string> out;
        const char* raw = std::getenv("AGENTTY_TLS_PINS");
        if (!raw || !*raw) return out;
        std::string s{raw};
        size_t start = 0;
        while (start <= s.size()) {
            size_t comma = s.find(',', start);
            std::string tok = s.substr(start,
                comma == std::string::npos ? std::string::npos : comma - start);
            // 修剪 ASCII 空白
            size_t a = tok.find_first_not_of(" \t\r\n");
            size_t b = tok.find_last_not_of(" \t\r\n");
            if (a != std::string::npos) out.push_back(tok.substr(a, b - a + 1));
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
        return out;
    }();
    return pins;
}

bool pinning_enabled() { return !configured_pins().empty(); }

// 叶证书的base64(SHA-256(DER SubjectPublicKeyInfo))，
// 匹配HPKP引脚格式。如果出现任何编码错误，则返回空。
std::string spki_pin_of(X509* leaf) {
    if (!leaf) return {};
    unsigned char* der = nullptr;
    int len = i2d_X509_PUBKEY(X509_get_X509_PUBKEY(leaf), &der);
    if (len <= 0 || !der) return {};
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(der, static_cast<size_t>(len), digest);
    OPENSSL_free(der);

    // base64-encode通过OpenSSL的EVP编码器摘要32-byte。
    int enc_len = 4 * ((SHA256_DIGEST_LENGTH + 2) / 3);
    std::string b64(static_cast<size_t>(enc_len), '\0');
    int n = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(b64.data()),
                            digest, SHA256_DIGEST_LENGTH);
    if (n <= 0) return {};
    b64.resize(static_cast<size_t>(n));
    return b64;
}

// OpenSSL验证回调：针对链中的每个证书运行。我们让 库 自己的
// chain/hostname 决策站（preverify_ok），然后，
// 只有当配置了 pinning 时，ALSO 才需要叶子（深度 0）来匹配 pin。
// 引脚缺失将否则-OK验证翻转为硬故障。
int pin_verify_cb(int preverify_ok, X509_STORE_CTX* store_ctx) {
    if (!preverify_ok) return 0;                 // 连锁店已经拒绝了
    if (!pinning_enabled()) return preverify_ok; // 固定 — 无需添加
    if (X509_STORE_CTX_get_error_depth(store_ctx) != 0)
        return preverify_ok;                     // 止针叶

    X509* leaf = X509_STORE_CTX_get_current_cert(store_ctx);
    std::string got = spki_pin_of(leaf);
    if (got.empty()) {
        X509_STORE_CTX_set_error(store_ctx, X509_V_ERR_APPLICATION_VERIFICATION);
        return 0;
    }
    for (const auto& want : configured_pins())
        if (got == want) return 1;               // 引脚匹配——接受

    // 没有配置的引脚与所呈现的叶密钥匹配。
    X509_STORE_CTX_set_error(store_ctx, X509_V_ERR_APPLICATION_VERIFICATION);
    return 0;
}

// --------------------------------------------------------------------------
// One-time SSL_CTX 构建。两种上下文：一种具有 对端 验证，
// 一种不安全（对于 AGENTTY_INSECURE=1 / --insecure）。
// 惰性-initialized下有互斥量；第一个调用者支付设置成本，其余调用者看到缓存的 ptr。
// 在进程退出时泄漏-SSL_CTX_free将竞态任何在主程序清理结束后仍存在的in-flight
// SSL。
// --------------------------------------------------------------------------
SSL_CTX* build_ctx(bool insecure) {
    init_openssl();
    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) return nullptr;

    // TLS 1.2 最低 — 任何较旧的都是合规红色 标志 和 Anthropic
    // 无论如何都不会协商它。  TLS 1.3 是首选（默认），
    // 它为我们提供了 1-RTT 握手，通过会话票证提供 0-RTT 恢复。
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION);

    // 会议缓存+门票。这是巨大的性能胜利：连接-pool TTL（90秒）
    // 后重新拨号会跳过完整的TLS握手并执行1-RTT恢复 - 将一个RTT
    // 保存到api.anthropic.com（〜30-150毫秒）。
    //
    // SSL_SESS_CACHE_CLIENT启用缓存模式，但是在CLIENT
    // 侧OpenSSL既没有auto-stores票据，也没有auto
    // -attaches在下一个SSL_new上。两半都明确连接：new_session_cb
    // 捕获每个 SNI 主机的每个 NewSessionTicket（如下），
    // 并在连接之前将 wrap_client 调用 SSL_set_session
    // () 与 已保存 捕获。仅依靠 缓存 模式（正如前面的评论所声称的那样）
    // 每次都会默默地进行完整的握手。
    SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_CLIENT);
    // 我们自己捕获TLS 1.3 NewSessionTicket会话
    // ——客户端永远不会单独咨询内部缓存（参见new_session_cb)。
    SSL_CTX_sess_set_new_cb(ctx, new_session_cb);
    // I/O模式标志，对于我们的泵-send循环至关重要：
    //   ENABLE_PARTIAL_WRITE_SSL_write可能返回小
    //                             于请求的正值；调用者处理部分进度（我们
    //                             已经通过 `off += put` 做到了）。
    //   ACCEPT_MOVING_WRITE_BUF-在WANT_READ/WANT_WRITE之后，
    //                             调用者可以具有*不同的*缓冲区指针和长度的重试。必需的，
    //                             因为 nghttp2 的 `mem_send`
    //                             在每个 调用 上返回一个新的 pointer/len
    //                             — 如果没有这个 标志，OpenSSL 会在
    //                             重试 上引发 SSL_R_BAD_LENGTH
    //                             (0x0A00010F)，这正是 TLS 恢复
    //                             后第一个 POST 上 Windows 构建在
    //                             SSL_write 上命中的失败。
    //   AUTO_RETRY-对端-initiated重新协商/后-handshake票据的透明处理；
    //                             在 OpenSSL 1.1.1
    //                             + 中默认打开，
    //                             为了清晰和可移植性而明确设置。
    SSL_CTX_set_mode(ctx,
                     SSL_MODE_ENABLE_PARTIAL_WRITE
                     | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER
                     | SSL_MODE_AUTO_RETRY);
    // 通告ALPN，以便服务器代表我们协商h2。
    SSL_CTX_set_alpn_protos(ctx, kAlpn, sizeof(kAlpn));

    if (insecure) {
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
    } else {
        // 链+主机名验证，加上通过回调分层的opt-in SPKI固定（除非
        // 设置了AGENTTY_TLS_PINS，否则没有-op）。
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, pin_verify_cb);
        load_system_roots(ctx);
    }
    return ctx;
}

struct SharedCtx {
    SSL_CTX* verifying = nullptr;
    SSL_CTX* insecure  = nullptr;
};

SharedCtx& shared() {
    // 故意泄漏-进程生命周期，避免破坏命令竞态。
    static SharedCtx* s = new SharedCtx{};
    static std::once_flag once;
    maya::once(once, [] {
        s->verifying = build_ctx(false);
        // 环境门：AGENTTY_INSECURE=1将两个句柄折叠成不
        // 安全的句柄，因此请求"verified"句柄的代码路径
        // 仍然遵循用户的显式opt-out。
        //
        // ── AGENTTY_INSECURE 生命周期( 在导出 var 之前读此内容) ──
        //
        // 环境变量被精确地采样到ONCE——这里，在第一个调用到
        // shared()——并通过call_once为进程的其余部分缓存。
        // 副作用 表示:
        //
        //   ·在CLI运行中（唯一预期用途）：设置AGENTTY_INSECURE = 1
        //     在启动`agentty`之前，第一次TLS握手读
        //     取它并且两个上下文都变得不安全。按预期工作。
        //
        //   ·mid-process setenv("AGENTTY_INSECURE"，"1"，1)默默地
        //     被忽略——我们已经构建了验证上下文。
        //     与 unsetenv() 相同。这打破了明显的测试模式：
        //
        //       setenv("AGENTTY_INSECURE", "1", 1);
        //       run_request();           // verifies (already cached)
        //       unsetenv("AGENTTY_INSECURE");
        //
        //     如果您需要该流程，请将 var 之前 设置为进程的第一个
        //     http 请求 — 通常在 main() 或测试装置
        //     的 SetUp() 中，而不是在断言之间。
        //
        //   ·如果agentty曾经作为库嵌入，则调用者不能
        //     在第一次SSL握手之后改变验证姿势，
        //     但不重新启动该过程。如果您采用该路线，
        //     请在嵌入 接口边界 处记录这一点。
        //
        // 为什么每个shared_context()调用是one-shot而不是re-reading：
        // SSL_CTX是重量级对象（加载系统CA捆绑包，
        // 验证信任存储）并且已经针对缓存指针建立的
        // 连接无论如何都不会迁移到新的-built，
        // 因此"live"旋钮将在其范围上撒谎。
        if (const char* e = util::env::get_or_null<util::env::Var::Insecure>();
            e && *e == '1') {
            s->insecure  = build_ctx(true);
            s->verifying = s->insecure;
        } else {
            s->insecure = build_ctx(true);
        }
    });
    return *s;
}

} // 命名空间结束。

void init_openssl() noexcept {
    OPENSSL_init_ssl(OPENSSL_INIT_NO_LOAD_CONFIG | OPENSSL_INIT_LOAD_SSL_STRINGS
                         | OPENSSL_INIT_LOAD_CRYPTO_STRINGS,
                     nullptr);
}

SSL_CTX* shared_context(bool insecure) {
    auto& s = shared();
    return insecure ? s.insecure : s.verifying;
}

SSL* wrap_client(int fd, std::string_view sni_host) {
    SSL_CTX* ctx = shared_context(false);
    if (!ctx) return nullptr;
    SSL* ssl = SSL_new(ctx);
    if (!ssl) return nullptr;

    // 用于部分-built SSL的RAII支架：直到我们最后commit()，
    // 任何提前返回都会释放SSL和我们在ex_data中藏匿的拥有的SNI主机密钥。
    // 此前SSL_set_fd故障路径必须交由-unwind
    // `delete host_key; SSL_free(ssl)`；
    // 如果未来的 Early-return 添加到上面，它可能会泄漏其中之一或两者。
    // 守卫使 half-built 处理 move-only 资源并自行清理。
    struct Building {
        SSL*         ssl  = nullptr;
        std::string* host = nullptr;   // 拥有 ex_data 密钥，除非提交否则释放
        int          ex   = -1;
        bool         live = true;
        Building(SSL* s, int ex_idx) : ssl(s), ex(ex_idx) {}
        Building(const Building&)            = delete;
        Building& operator=(const Building&) = delete;
        ~Building() {
            if (!live) return;
            if (host) { if (ex >= 0) SSL_set_ex_data(ssl, ex, nullptr); delete host; }
            if (ssl)  SSL_free(ssl);
        }
        SSL* commit() noexcept { live = false; return ssl; }   // 所有权 逃脱
    } building{ssl, sni_ex_index()};

    // SNI：virtual-hosted 边缘（Anthropic 的 Cloudflare 前端）需要。
    // null-terminate 通过本地字符串 — string_view 没有保证。
    std::string host{sni_host};
    SSL_set_tlsext_host_name(ssl, host.c_str());
    // 主机名验证-在已配置SSL_CTX的链验证之上，
    // 对照证书检查CN/SAN。
    SSL_set1_host(ssl, host.c_str());
    SSL_set_hostflags(ssl, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);

    // 恢复：附上该主机的最新票证（如果有的话），因此即将到来的握手是1-RTT恢复。
    // 还将主机拥有的副本存储在 ex_data 中，
    // 以便 new_session_cb 知道将新票证归档到哪个密钥下（它会
    // 触发 post-handshake，只能看到 SSL*）。该字符串在顺利路径
    // 上的 free_ssl 中释放，或在失败时通过 ~Building 释放。
    building.host = new std::string{host};
    SSL_set_ex_data(ssl, sni_ex_index(), building.host);
    // 共享引用，因此并发替换无法在 -attach 中期释放它。
    // SSL_set_session有其自己的参考号；我们的掉落在范围末端。
    // 已加载 快照使地图（及其会话）在持续时间内保持活动；
    // SSL_set_session 有自己的参考号。
    {
        const auto snap = tickets().load();
        if (auto it = snap->find(std::string{host}); it != snap->end() && it->second.get())
            SSL_set_session(ssl, it->second.get());
    }

    if (SSL_set_fd(ssl, fd) != 1) {
        return nullptr;   // 〜构建释放主机密钥+SSL
    }
    SSL_set_connect_state(ssl);
    return building.commit();
}

void free_ssl(SSL* ssl) noexcept {
    if (!ssl) return;
    // 释放我们在 ex_data (wrap_client) 中保存的拥有的 SNI 主机密钥。
    if (auto* host_key = static_cast<std::string*>(
            SSL_get_ex_data(ssl, sni_ex_index()))) {
        SSL_set_ex_data(ssl, sni_ex_index(), nullptr);
        delete host_key;
    }
    // Best-effort双向关机。  如果 对端 没有 ACK，
    // 我们就不会阻止它——fd 无论如何都会关闭。
    (void)SSL_shutdown(ssl);
    SSL_free(ssl);
}

std::string last_error(SSL* ssl) {
    std::string out;
    if (ssl) {
        int code = SSL_get_error(ssl, -1);
        if (code == SSL_ERROR_SYSCALL) {
            // 实际原因往往在err队列上；跌倒。
            out = "syscall";
        } else if (code == SSL_ERROR_ZERO_RETURN) {
            out = "peer closed (SSL_ERROR_ZERO_RETURN)";
        }
    }
    unsigned long e;
    char buf[256];
    while ((e = ERR_get_error()) != 0) {
        ERR_error_string_n(e, buf, sizeof(buf));
        if (!out.empty()) out += "; ";
        out += buf;
    }
    if (out.empty()) out = "unknown openssl error";
    return out;
}

} // namespace agentty::tls
