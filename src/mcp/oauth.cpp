// agentty::mcp::oauth-MCP服务器的交互式OAuth 2.1登录。
//
// 协议逻辑（PKCE、RFC 9207 iss验证、DCR/CIMD、令牌解析)是mcp
// -cpp的<mcp/auth.hpp>。这个TU是I/O +生命周期：agentty
// 的HTTP客户端，EPHEMERAL端口上的跨-platform环回回调服务器（MCP
// 重定向URI是动态的，与Codex的固定1455不同），浏览器打开，
// 以及at-rest-encrypted per-server令牌存储。

#include "agentty/mcp/oauth.hpp"
#include "agentty/version.hpp"
#include "agentty/util/lock_levels.hpp"
#include "agentty/util/home_dir.hpp"
#include "agentty/util/user_root.hpp"

#include <maya/runtime.hpp>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <process.h>
#else
#  include <arpa/inet.h>
#  include <fcntl.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

#include <nlohmann/json.hpp>
#include <mcp/auth.hpp>

#include "agentty/auth/auth.hpp"
#include "agentty/auth/cred_crypt.hpp"
#include "agentty/io/http.hpp"
#include "agentty/io/shared_file.hpp"

namespace agentty::mcp::oauth {
namespace {
using json = nlohmann::json;
namespace fs = std::filesystem;

std::int64_t now_ms_impl() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// ── URL 拆分( E) scheme/host/port/path) ──────────────────────────────────
struct Url { std::string scheme, host, path; std::uint16_t port = 0; bool tls = true; bool ok = false; };

Url split_url(std::string_view u) {
    Url r;
    if (u.rfind("https://", 0) == 0) { r.scheme = "https"; r.tls = true;  u.remove_prefix(8); r.port = 443; }
    else if (u.rfind("http://", 0) == 0) { r.scheme = "http"; r.tls = false; u.remove_prefix(7); r.port = 80; }
    else return r;
    auto slash = u.find('/');
    std::string_view authority = slash == std::string_view::npos ? u : u.substr(0, slash);
    r.path = slash == std::string_view::npos ? "/" : std::string(u.substr(slash));
    auto colon = authority.find(':');
    if (colon == std::string_view::npos) { r.host = std::string(authority); }
    else {
        r.host = std::string(authority.substr(0, colon));
        try { r.port = static_cast<std::uint16_t>(std::stoul(std::string(authority.substr(colon + 1)))); }
        catch (...) { return r; }
    }
    r.ok = !r.host.empty();
    return r;
}

// ── 1 HTTP GET/POST通过agentty的客户端,返回解析的JSON ──────────
// 返回{状态，正文}。 传输层 故障时状态为 0（详细信息参见 正文）。
struct HttpOut { int status = 0; std::string body; std::string www_authenticate; };

HttpOut do_http(http::HttpMethod method, const std::string& url,
                const std::string& body = {}, bool form = false,
                const std::string& bearer = {}) {
    HttpOut out;
    Url u = split_url(url);
    if (!u.ok) { out.body = "invalid URL: " + url; return out; }
    http::Request req;
    req.method    = method;
    req.host      = u.host;
    req.port      = u.port;
    req.path      = u.path;
    req.plaintext = !u.tls;
    req.body      = body;
    req.max_body_bytes = 1ull * 1024 * 1024;   // metadata/token JSON 很小
    req.headers.push_back({"accept", "application/json"});
    req.headers.push_back({"user-agent", ::agentty::user_agent()});
    if (method == http::HttpMethod::Post)
        req.headers.push_back({"content-type",
            form ? "application/x-www-form-urlencoded" : "application/json"});
    if (!bearer.empty()) req.headers.push_back({"authorization", bearer});

    http::Timeouts tos; tos.total = std::chrono::milliseconds(20'000);
    auto res = http::default_client(::agentty::IoAccess::grant()).send(req, tos);
    // agentty的send()提供ANY HTTP状态——包括401
    // ——作为完整的头文件+正文的成功响应（仅传输层失败变成意外的HttpError）。
    // 因此，401 的 WWW-Authenticate 挑战是直接从
    // res->头文件 读取的；不需要well-known 回退，
    // 除非挑战是missing/unparseable或者服务器从不401。
    if (!res) {
        out.body = res.error().render();
        return out;
    }
    out.status = res->status;
    out.body   = res->body;
    for (const auto& h : res->headers) {
        std::string n = h.name;
        for (auto& c : n) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        if (n == "www-authenticate") out.www_authenticate = h.value;
    }
    return out;
}

// ── EPHEMERAL 端口的循环后台 回调 服务器 ──────────────────────────
struct Callback {
    std::uint16_t port = 0;   // 由open_loopback填写
    std::string   redirect_uri;
};

struct CallbackResult { std::string code, state, iss, error; };

#if defined(_WIN32)
using sock_t = SOCKET;
#else
using sock_t = int;
#endif

// 绑定127.0.0.1:0，返回socket+分配的端口。 k失败时无效。
sock_t open_loopback(std::uint16_t& out_port, std::string& err) {
#if defined(_WIN32)
    static std::once_flag wsa_once;
    maya::once(wsa_once, []{ WSADATA w; WSAStartup(MAKEWORD(2,2), &w); });
    const sock_t kInvalid = INVALID_SOCKET;
#else
    const sock_t kInvalid = -1;
#endif
    sock_t srv = ::socket(AF_INET, SOCK_STREAM, 0);
    if (srv == kInvalid) { err = "cannot create callback socket"; return kInvalid; }
    int yes = 1;
    ::setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0);                       // 临时
    if (::bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
#if defined(_WIN32)
        closesocket(srv);
#else
        ::close(srv);
#endif
        err = "cannot bind loopback callback port"; return kInvalid;
    }
    sockaddr_in bound{}; socklen_t blen = sizeof(bound);
    if (::getsockname(srv, reinterpret_cast<sockaddr*>(&bound), &blen) != 0) {
#if defined(_WIN32)
        closesocket(srv);
#else
        ::close(srv);
#endif
        err = "getsockname failed"; return kInvalid;
    }
    out_port = ntohs(bound.sin_port);
    if (::listen(srv, 1) != 0) {
#if defined(_WIN32)
        closesocket(srv);
#else
        ::close(srv);
#endif
        err = "listen failed"; return kInvalid;
    }
    return srv;
}

std::string query_param(std::string_view target, std::string_view key) {
    auto q = target.find('?');
    if (q == std::string_view::npos) return {};
    std::string_view qs = target.substr(q + 1);
    std::size_t i = 0;
    while (i < qs.size()) {
        auto amp = qs.find('&', i);
        std::string_view pair = qs.substr(i, amp == std::string_view::npos ? std::string_view::npos : amp - i);
        auto eq = pair.find('=');
        if (eq != std::string_view::npos && pair.substr(0, eq) == key) {
            std::string v(pair.substr(eq + 1));
            // 百分比-decode
            std::string dec; dec.reserve(v.size());
            for (std::size_t j = 0; j < v.size(); ++j) {
                if (v[j] == '%' && j + 2 < v.size()) {
                    auto hx = [](char c)->int{ if(c>='0'&&c<='9')return c-'0'; if(c>='a'&&c<='f')return c-'a'+10; if(c>='A'&&c<='F')return c-'A'+10; return -1; };
                    int hi = hx(v[j+1]), lo = hx(v[j+2]);
                    if (hi>=0 && lo>=0) { dec.push_back(char(hi*16+lo)); j += 2; continue; }
                }
                dec.push_back(v[j] == '+' ? ' ' : v[j]);
            }
            return dec;
        }
        if (amp == std::string_view::npos) break;
        i = amp + 1;
    }
    return {};
}

// 接受`srv`上的一个请求，提取code/state/iss，回复一页。
// 借用`srv`：调用者的SockGuard在每条路径上关闭它。
CallbackResult wait_for_callback(sock_t srv, int timeout_s) {
    CallbackResult out;
#if defined(_WIN32)
    const sock_t kInvalid = INVALID_SOCKET;
    auto close_sock = [](sock_t s){ closesocket(s); };
#else
    const sock_t kInvalid = -1;
    auto close_sock = [](sock_t s){ ::close(s); };
#endif
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_s);
    for (;;) {
        const auto left = deadline - std::chrono::steady_clock::now();
        if (left <= std::chrono::steady_clock::duration::zero()) {
            out.error = "login timed out";
            return out;
        }
        fd_set rfds; FD_ZERO(&rfds); FD_SET(srv, &rfds);
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(left).count();
        timeval tv{static_cast<decltype(tv.tv_sec)>(us / 1000000),
                   static_cast<decltype(tv.tv_usec)>(us % 1000000)};
        if (::select(static_cast<int>(srv + 1), &rfds, nullptr, nullptr, &tv) <= 0) continue;
        sock_t cli = ::accept(srv, nullptr, nullptr);
        if (cli == kInvalid) continue;

        // 等待带有select()的请求，因此连接并且不发送
        // 任何内容（浏览器预连接）的对端不能让我们超过截止时间。
        const auto read_until = std::min(deadline, std::chrono::steady_clock::now() + std::chrono::seconds(10));
        std::string reqbuf; char buf[4096];
        while (reqbuf.size() <= 16384) {
            const auto rleft = read_until - std::chrono::steady_clock::now();
            if (rleft <= std::chrono::steady_clock::duration::zero()) break;
            fd_set cfds; FD_ZERO(&cfds); FD_SET(cli, &cfds);
            const auto rus = std::chrono::duration_cast<std::chrono::microseconds>(rleft).count();
            timeval rtv{static_cast<decltype(rtv.tv_sec)>(rus / 1000000),
                        static_cast<decltype(rtv.tv_usec)>(rus % 1000000)};
            const int ready = ::select(static_cast<int>(cli + 1), &cfds, nullptr, nullptr, &rtv);
            if (ready < 0) break;
            if (ready == 0) continue;
#if defined(_WIN32)
            int n = ::recv(cli, buf, sizeof(buf), 0);
#else
            auto n = ::recv(cli, buf, sizeof(buf), 0);
#endif
            if (n <= 0) break;
            reqbuf.append(buf, static_cast<std::size_t>(n));
            if (reqbuf.find("\r\n\r\n") != std::string::npos) break;
        }
        std::string target;
        if (const auto sp = reqbuf.find(' '); sp != std::string::npos) {
            const auto sp2 = reqbuf.find(' ', sp + 1);
            if (sp2 != std::string::npos) target = reqbuf.substr(sp + 1, sp2 - sp - 1);
        }
        // 不是 回调 (空的预连接、图标)：丢弃它并继续等待。
        if (target.find('?') == std::string::npos) { close_sock(cli); continue; }
        out.code  = query_param(target, "code");
        out.state = query_param(target, "state");
        out.iss   = query_param(target, "iss");
        const std::string err = query_param(target, "error");

        const bool ok = !out.code.empty() && err.empty();
        const std::string page = ok
            ? "<html><head><title>agentty</title></head><body "
              "style='font-family:system-ui;background:#0b0d10;color:#e6e6e6;"
              "display:flex;align-items:center;justify-content:center;height:100vh'>"
              "<div style='text-align:center'><h2>&#10003; Authorized</h2>"
              "<p>You can close this tab and return to agentty.</p></div></body></html>"
            : "<html><body style='font-family:system-ui'><h2>Authorization failed</h2>"
              "<p>Return to agentty and try again.</p></body></html>";
        std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
                           "Content-Length: " + std::to_string(page.size())
                         + "\r\nConnection: close\r\n\r\n" + page;
        (void)::send(cli, resp.data(), static_cast<int>(resp.size()), 0);
        close_sock(cli);
        if (!ok) out.error = err.empty() ? "no authorization code in callback"
                                         : ("authorization denied: " + err);
        return out;
    }
}

// ── 托肯存储器( 每1个) -server ,加密,chmod 600) 互联网档案馆的存檔,存档日期2011-09-02. ─────────────────────────
fs::path tokens_dir() { return auth::config_dir() / "mcp_tokens"; }

fs::path token_path(const std::string& server) {
    // 将服务器名称清理为安全文件名。
    std::string safe;
    for (char c : server) safe.push_back((std::isalnum((unsigned char)c) || c=='-' || c=='_') ? c : '_');
    return tokens_dir() / (safe + ".json");
}

json to_json(const StoredToken& t) {
    return json{
        {"access_token",  t.access_token},
        {"token_type",    t.token_type},
        {"refresh_token", t.refresh_token},
        {"issuer",        t.issuer},
        {"resource",      t.resource},
        {"client_id",     t.client_id},
        {"expires_at_ms", t.expires_at_ms},
    };
}
StoredToken from_json(const json& j) {
    StoredToken t;
    t.access_token  = j.value("access_token", std::string{});
    t.token_type    = j.value("token_type", std::string{"Bearer"});
    t.refresh_token = j.value("refresh_token", std::string{});
    t.issuer        = j.value("issuer", std::string{});
    t.resource      = j.value("resource", std::string{});
    t.client_id     = j.value("client_id", std::string{});
    t.expires_at_ms = j.value("expires_at_ms", (std::int64_t)0);
    return t;
}

bool save_token(const std::string& server, const StoredToken& t) {
    auto sealed = auth::crypt::seal(to_json(t).dump());
    if (!sealed) return false;
    return persistence::write_atomic(::agentty::IoAccess::grant(), token_path(server), *sealed,
                                     {.durability = persistence::Durability::Durable,
                                      .mode = 0600});
}

std::optional<StoredToken> load_token(const std::string& server) {
    const fs::path p = token_path(server);
    std::ifstream f(p, std::ios::binary);
    if (!f) return std::nullopt;
    std::string raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (raw.empty()) return std::nullopt;
    std::string plain = raw;
    if (auth::crypt::looks_sealed(raw)) {
        auto un = auth::crypt::unseal(raw);
        if (!un) return std::nullopt;
        plain = *un;
    }
    try { return from_json(json::parse(plain)); } catch (...) { return std::nullopt; }
}

// ── 不错 -known 元数据 URL 衍生( 4) RFC 8414) ──────────────────────────
std::string as_metadata_url(const std::string& issuer) {
    // <issuer>/.well-known/oauth-authorization-server (剥离一尾随/)。
    std::string base = issuer;
    if (!base.empty() && base.back() == '/') base.pop_back();
    return base + "/.well-known/oauth-authorization-server";
}

// 就地刷新令牌。返回刷新的令牌，或失败时返回 nullopt。
std::optional<StoredToken> refresh(const StoredToken& tok) {
    if (tok.refresh_token.empty() || tok.client_id.empty() || tok.issuer.empty())
        return std::nullopt;
    // Re-fetch AS 令牌端点的元数据（便宜的，以及发行者-bound）。
    HttpOut md = do_http(http::HttpMethod::Get, as_metadata_url(tok.issuer));
    if (md.status != 200) return std::nullopt;
    ::mcp::auth::AuthServerMetadata as;
    try { as = ::mcp::auth::parse_authorization_server_metadata(json::parse(md.body)); }
    catch (...) { return std::nullopt; }

    ::mcp::auth::AccessToken at;
    at.refresh_token = tok.refresh_token;
    at.resource      = tok.resource;
    std::string form = ::mcp::auth::refresh_request_form(at, tok.client_id);
    HttpOut tr = do_http(http::HttpMethod::Post, as.token_endpoint, form, /*形式=*/true);
    if (tr.status != 200) return std::nullopt;
    try {
        auto nt = ::mcp::auth::parse_token_response(json::parse(tr.body), tok.issuer, tok.resource);
        StoredToken out = tok;
        out.access_token = nt.access_token;
        out.token_type   = nt.token_type;
        if (nt.refresh_token) out.refresh_token = *nt.refresh_token;
        out.expires_at_ms = nt.expires_in ? now_ms_impl() + *nt.expires_in * 1000 : 0;
        return out;
    } catch (...) { return std::nullopt; }
}

} // 命名空间结束。

std::int64_t StoredToken::now_ms() noexcept { return now_ms_impl(); }

LoginResult login(Io io, const std::string& server_name, const std::string& endpoint_url,
                  const std::string& metadata_url_in, const std::string& client_id,
                  int timeout_s) {
    LoginResult r;
    // 1.定位受保护的-resource-metadata URL。
    std::string metadata_url = metadata_url_in;
    if (metadata_url.empty()) {
        // 探测端点；预计会出现带有 WWW-Authenticate 挑战的 401。
        HttpOut probe = do_http(http::HttpMethod::Post, endpoint_url,
            R"({"jsonrpc":"2.0","id":1,"method":"server/discover","params":{}})");
        if (probe.status == 401 && !probe.www_authenticate.empty()) {
            if (auto u = ::mcp::auth::parse_challenge(probe.www_authenticate)) metadata_url = *u;
        }
        if (metadata_url.empty()) {
            // 回落到端点原点上的常规井-known。
            Url u = split_url(endpoint_url);
            if (!u.ok) { r.message = "invalid endpoint URL"; return r; }
            metadata_url = u.scheme + "://" + u.host +
                           (u.port && u.port != (u.tls?443:80) ? ":" + std::to_string(u.port) : "") +
                           "/.well-known/oauth-protected-resource";
        }
    }

    // 2.GET受保护的-resource元数据→授权服务器。
    HttpOut prm = do_http(http::HttpMethod::Get, metadata_url);
    if (prm.status != 200) {
        r.message = "could not fetch resource metadata (" + metadata_url +
                    "): HTTP " + std::to_string(prm.status);
        return r;
    }
    ::mcp::auth::ProtectedResourceMetadata pr;
    try { pr = ::mcp::auth::parse_protected_resource_metadata(json::parse(prm.body)); }
    catch (const std::exception& e) { r.message = std::string("bad resource metadata: ") + e.what(); return r; }

    const std::string issuer = pr.authorization_servers.front();
    const std::string resource = pr.resource.empty() ? endpoint_url : pr.resource;

    // 3.GETAS元数据。
    HttpOut asmd = do_http(http::HttpMethod::Get, as_metadata_url(issuer));
    if (asmd.status != 200) {
        r.message = "could not fetch authorization-server metadata for " + issuer;
        return r;
    }
    ::mcp::auth::AuthServerMetadata as;
    try { as = ::mcp::auth::parse_authorization_server_metadata(json::parse(asmd.body)); }
    catch (const std::exception& e) { r.message = std::string("bad AS metadata: ") + e.what(); return r; }

    // 4、打开环回回调 首次(需要用于重定向的端口URI)。
    std::uint16_t port = 0; std::string sockerr;
    sock_t srv = open_loopback(port, sockerr);
#if defined(_WIN32)
    if (srv == INVALID_SOCKET) { r.message = sockerr; return r; }
#else
    if (srv < 0) { r.message = sockerr; return r; }
#endif
    // 下面的每个返回都用于泄漏侦听器（注册失败、错误的 CIMD id、
    // 从 random_urlsafe 抛出...）并保持其端口绑定
    // for the life of the process. Own it here; wait_for_callback borrows it.
    struct SockGuard {
        sock_t s;
        explicit SockGuard(sock_t v) : s(v) {}
        ~SockGuard() {
#if defined(_WIN32)
            closesocket(s);
#else
            ::close(s);
#endif
        }
        SockGuard(const SockGuard&)            = delete;
        SockGuard& operator=(const SockGuard&) = delete;
    } srv_guard{srv};
    const std::string redirect_uri = "http://127.0.0.1:" + std::to_string(port) + "/callback";

    // 5、注册客户端。 2026 年 7 月 28 日的首选订单：来电者 -supplied
    //    CIMD https URL (无 AS round-trip — URL IS client_id)，
    //    否则呼叫者-supplied pre-registered 公共client_id，
    //    否则动态客户端注册(application_type=本机)。如果没有一个适用，我们就无法继续。
    ::mcp::auth::ClientRegistration client;
    if (!client_id.empty() && client_id.rfind("https://", 0) == 0) {
        // CIMD:client_id是绑定到`issuer`的稳定元数据-document URL。
        try { client = ::mcp::auth::cimd_client(client_id, issuer); }
        catch (const std::exception& e) { r.message = std::string("invalid CIMD client_id: ") + e.what(); return r; }
    } else if (!client_id.empty()) {
        // Pre-registered公共客户端(PKCE，token_endpoint_auth_method=无)。
        client.client_id = client_id;
        client.issuer    = issuer;
    } else if (as.registration_endpoint) {
        json body = ::mcp::auth::registration_request("agentty", redirect_uri);
        HttpOut rr = do_http(http::HttpMethod::Post, *as.registration_endpoint, body.dump());
        if (rr.status != 200 && rr.status != 201) {
            r.message = "dynamic client registration failed: HTTP " + std::to_string(rr.status);
            return r;
        }
        try { client = ::mcp::auth::parse_registration(json::parse(rr.body), issuer); }
        catch (const std::exception& e) { r.message = std::string("bad registration: ") + e.what(); return r; }
    } else {
        r.message = "authorization server offers no registration endpoint and no "
                    "client_id is configured for '" + server_name + "'. Set a \"client_id\" "
                    "on the server in mcp.json (an https:// value is used as a CIMD URL), "
                    "or pass --client-id <id|https-url>.";
        return r;
    }

    // 6.开始PKCE流程；打开浏览器；等待重定向。
    const std::string verifier_seed = auth::random_urlsafe(io, 48);   // >= 32 字节 熵
    const std::string state = auth::random_urlsafe(io, 24);
    ::mcp::auth::AuthSession sess = ::mcp::auth::begin(
        as, client, redirect_uri, resource, verifier_seed, state);
    const std::string url = sess.authorize_url(as.authorization_endpoint);

    std::fprintf(stderr, "\nOpening your browser to authorize agentty for '%s'.\n"
                         "If it doesn't open, visit:\n\n  %s\n\n", server_name.c_str(), url.c_str());
    auth::open_browser(url);

    CallbackResult cb = wait_for_callback(srv, timeout_s);
    if (!cb.error.empty()) { r.message = cb.error; return r; }

    // 7.验证(状态+RFC 9207 iss)并将代码交换为令牌。
    ::mcp::auth::RedirectParams rp;
    rp.code = cb.code; rp.state = cb.state;
    if (!cb.iss.empty()) rp.iss = cb.iss;
    json token_req;
    try { token_req = ::mcp::auth::redeem(sess, rp); }
    catch (const std::exception& e) { r.message = std::string("authorization rejected: ") + e.what(); return r; }

    HttpOut tr = do_http(http::HttpMethod::Post, token_req["token_endpoint"].get<std::string>(),
                         token_req["__form"].get<std::string>(), /*形式=*/true);
    if (tr.status != 200) { r.message = "token exchange failed: HTTP " + std::to_string(tr.status); return r; }

    ::mcp::auth::AccessToken at;
    try { at = ::mcp::auth::parse_token_response(json::parse(tr.body), issuer, resource); }
    catch (const std::exception& e) { r.message = std::string("bad token response: ") + e.what(); return r; }

    StoredToken st;
    st.access_token = at.access_token;
    st.token_type   = at.token_type;
    if (at.refresh_token) st.refresh_token = *at.refresh_token;
    st.issuer    = issuer;
    st.resource  = resource;
    st.client_id = client.client_id;
    st.expires_at_ms = at.expires_in ? now_ms_impl() + *at.expires_in * 1000 : 0;

    if (!save_token(server_name, st)) { r.message = "authorized, but failed to save the token"; return r; }
    r.ok = true;
    r.message = "authorized '" + server_name + "' (issuer " + issuer + ")";
    return r;
}

namespace {
// 刷新本身，在令牌文件的部分内：首先是re-read，
// 因此使用刚刚刷新（和旋转）的另一进程的令牌，而不是替换。
std::optional<std::string> bearer_for_locked(const maya::held<locks::auth_refresh>& cap,
                                             const std::string& server_name) {
    auto tok = load_token(server_name);
    if (!tok) return std::nullopt;
    if (tok->expired()) {
        tok = persistence::with_shared_file(cap, persistence::cred_lane(token_path(server_name)), token_path(server_name),
            [](const maya::held<locks::cred_file>&, persistence::Held,
               std::string name, StoredToken t) -> std::optional<StoredToken> {
                if (auto disk = load_token(name); disk && !disk->expired()) return disk;
                if (auto fresh = refresh(t)) { (void)save_token(name, *fresh); return fresh; }
                // 过期令牌：服务器将返回401并且传输层说明
                // 如何再次登录。
                return t;
            }, server_name, *tok);
        if (!tok) return std::nullopt;
    }
    if (tok->access_token.empty()) return std::nullopt;
    return tok->authorization_header();
}
}  // 命名空间结束。

std::optional<std::string> bearer_for(const locks::below_auth_refresh& cap,
                                      const std::string& server_name) {
    // 一次一次刷新：看到相同过期令牌的两个调用者都将刷新，
    // 并且轮换刷新令牌的服务器拒绝第二个。
    // Guarded<bool> 就是 锁；作品在其中运行。
    static maya::guarded<bool, locks::auth_refresh> lane;
    return lane.with(cap,
        [](const maya::held<locks::auth_refresh>& h, bool&, std::string name)
            -> std::optional<std::string> {
            return bearer_for_locked(h, name);
        },
        server_name);
}


bool logout(const std::string& server_name) {
    std::error_code ec;
    fs::remove(token_path(server_name), ec);
    return true;
}

bool has_token(const std::string& server_name) {
    std::error_code ec;
    return fs::exists(token_path(server_name), ec);
}

namespace {
// 从 mcp.json 解析 <server> → 端点 URL (与 bridge.cpp 的优先级相同：
// $AGENTTY_MCP_CONFIG，然后./.agentty/mcp.json，然后~/.agentty/mcp.json)。
fs::path resolve_mcp_config() {
    std::error_code ec;
    if (const char* env = std::getenv("AGENTTY_MCP_CONFIG"); env && env[0]) {
        fs::path p(env);
        return fs::is_regular_file(p, ec) ? p : fs::path{};
    }
    if (auto local = fs::path{".agentty"} / "mcp.json"; fs::is_regular_file(local, ec)) return local;
    // 单个 per-user 根（~/.agentty 或 $AGENTTY_HOME
    // — 参见 util/user_root.hpp) — 与桥 + 插件写入它的位置相同。
    if (auto user = util::user_root() / "mcp.json";
        fs::is_regular_file(user, ec)) {
        return user;
    }
    return {};
}

json load_mcp_servers() {
    fs::path cfg = resolve_mcp_config();
    if (cfg.empty()) return json::object();
    std::ifstream f(cfg, std::ios::binary);
    if (!f) return json::object();
    try {
        json doc = json::parse(f);
        // 服务器可以位于顶层或"mcpServers"/"servers"之下。
        if (doc.contains("mcpServers") && doc["mcpServers"].is_object()) return doc["mcpServers"];
        if (doc.contains("servers") && doc["servers"].is_object())     return doc["servers"];
        return doc.is_object() ? doc : json::object();
    } catch (...) { return json::object(); }
}

std::string endpoint_of(const json& servers, const std::string& name) {
    if (!servers.contains(name)) return {};
    const json& s = servers[name];
    if (!s.is_object()) return {};
    return s.value("url", std::string{});
}

// 解析服务器的pre-registered/CIMD client_id，如果有的话：服务器的
// "client_id"字段在mcp.json中，否则$AGENTTY_MCP_CLIENT_ID。
std::string client_id_of(const json& servers, const std::string& name) {
    if (servers.contains(name) && servers[name].is_object()) {
        std::string cid = servers[name].value("client_id", std::string{});
        if (!cid.empty()) return cid;
    }
    if (const char* env = std::getenv("AGENTTY_MCP_CLIENT_ID"); env && env[0]) return env;
    return {};
}
} // 命名空间结束。

int cmd_mcp_login(Io io, const std::string& server_name, const std::string& metadata_url,
                  const std::string& client_id) {
    if (server_name.empty()) {
        std::fprintf(stderr, "usage: agentty mcp-login <server> [--metadata <url>] [--client-id <id|https-url>]\n");
        return 2;
    }
    json servers = load_mcp_servers();
    const std::string endpoint = endpoint_of(servers, server_name);
    if (endpoint.empty()) {
        std::fprintf(stderr,
            "mcp-login: no HTTP server named '%s' in mcp.json (need a \"url\" entry).\n",
            server_name.c_str());
        return 1;
    }
    std::fprintf(stderr, "Authorizing agentty for MCP server '%s' (%s)...\n",
                 server_name.c_str(), endpoint.c_str());
    // --client-id胜；否则服务器的 mcp.json "client_id"；否则环境变量。
    std::string cid = client_id.empty() ? client_id_of(servers, server_name) : client_id;
    LoginResult res = login(io, server_name, endpoint, metadata_url, cid);
    if (res.ok) {
        std::fprintf(stderr, "\n\xE2\x9C\x93 %s\n", res.message.c_str());
        return 0;
    }
    std::fprintf(stderr, "\nmcp-login failed: %s\n", res.message.c_str());
    return 1;
}

int cmd_mcp_logout(const std::string& server_name) {
    if (server_name.empty()) {
        std::fprintf(stderr, "usage: agentty mcp-logout <server>\n");
        return 2;
    }
    const bool had = has_token(server_name);
    logout(server_name);
    std::fprintf(stderr, had ? "removed stored token for '%s'\n"
                             : "no stored token for '%s'\n", server_name.c_str());
    return 0;
}

int cmd_mcp_status() {
    json servers = load_mcp_servers();
    if (servers.empty()) { std::fprintf(stderr, "no MCP servers configured\n"); return 0; }
    std::fprintf(stderr, "MCP servers:\n");
    for (auto it = servers.begin(); it != servers.end(); ++it) {
        const std::string& name = it.key();
        const auto& spec = it.value();
        const std::string url = spec.is_object() ? spec.value("url", std::string{}) : std::string{};
        const std::string type = spec.is_object() ? spec.value("type", std::string{}) : std::string{};
        const char* kind = (!url.empty() || type == "http" || type == "streamable-http")
            ? "http" : type == "sse" ? "sse" : "stdio";
        const bool disabled = spec.is_object() && spec.value("disabled", false);
        const bool trusted = spec.is_object() && spec.value("trustAnnotations", false);
        const int timeout = spec.is_object() ? spec.value("timeoutMs", 60'000) : 60'000;
        std::size_t pinned = 0;
        if (spec.is_object()) {
            if (auto tools = spec.find("tools"); tools != spec.end() && tools->is_object()) {
                if (auto pin = tools->find("pin"); pin != tools->end() && pin->is_array())
                    pinned = pin->size();
            }
        }
        const char* auth = has_token(name) ? " authorized" : "";
        std::fprintf(stderr, "  %-24s %-5s %s timeout=%dms annotations=%s pinned=%zu%s\n",
            name.c_str(), kind, disabled ? "disabled" : "enabled", timeout,
            trusted ? "trusted" : "untrusted", pinned, auth);
    }
    return 0;
}

} // namespace agentty::mcp::oauth
