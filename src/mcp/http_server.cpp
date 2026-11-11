// agentty::mcp - HttpServerProvider:Streamable HTTP 传输层 上的 MCP
// CapabilityProvider (MCP 规范 2025-03-26+/2025-11-25)。
//
//   Streamable HTTP 传输层将旧的HTTP+SSE对折叠成ONE
//   端点URL：
//     “客户端”“服务器：HTTP POST，具有单个JSON-RPC消息（或批量）。
//       服务器回复：
//         -内容-Type:application/json--一个JSON-RPC响应，或者
//         -内容-Type:text/event-stream——SSE 数据流，其`data:`
//           事件各自携带JSON-RPC消息(响应，
//           加上属于它的任何服务器-客户端requests/notifications)。
//     Mcp-Session-Id：如果服务器在初始化时返回此标头，
//       则客户端 必须 在每个后续请求中回显它。我们捕捉+回放它。
//     协议版本：初始化后，我们根据规范在每个请求上发送 MCP
//       -Protocol-Version。
//
//   对于 agentty 的其余部分，这是一个 字节 通道，就像孩子的 stdio
//   一样：对端 的写入变成了 POST（在 工作线程 上，因此写入者永远不会阻塞网络），
//   并且回复中的每个 帧（普通 JSON 或 SSE 事件）都被推送到 in
//   -memory 管道中，即 对端读。然后，同一个 mcp::Connection
//   对其运行握手、列表和 调用。
//
//   该文件位于 agentty（而不是 mcp-cpp）中，因为它构建在
//   agentty 自己的 HTTP/2 客户端（TLS、池、取消）上，而不是将新的
//   HTTP 依赖项拉入 SDK。

#include "agentty/mcp/http_server.hpp"
#include "agentty/version.hpp"
#include "agentty/mcp/oauth.hpp"

#include "agentty/io/http.hpp"
#include "agentty/util/dbglog.hpp"

#include <maya/runtime.hpp>
#include <optional>
#include <mcp/auth.hpp>
#include <mcp/protocol.hpp>

#include "agentty/mcp/connection.hpp"
#include "agentty/rpc/peer.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "agentty/util/background.hpp"

#include <nlohmann/json.hpp>

namespace agentty::mcp {

namespace {

using json = nlohmann::json;

// 解析 单个绝对 URL 变为（方案、主机、端口、路径）。 Minimal – MCP 端点对于本地主机
// 来说是普通的 https://host[:port]/path 或 http://Ã¢â‚¬Â¦。
struct ParsedUrl {
    bool        ok        = false;
    bool        tls       = true;
    std::string host;
    std::uint16_t port    = 443;
    std::string path      = "/";
};

ParsedUrl parse_url(const std::string& url) {
    ParsedUrl u;
    std::string_view s{url};
    if (s.starts_with("https://"))      { u.tls = true;  u.port = 443; s.remove_prefix(8); }
    else if (s.starts_with("http://"))  { u.tls = false; u.port = 80;  s.remove_prefix(7); }
    else return u;   // 不支持的方案

    auto slash = s.find('/');
    std::string_view authority = slash == std::string_view::npos ? s : s.substr(0, slash);
    u.path = slash == std::string_view::npos ? std::string{"/"} : std::string{s.substr(slash)};

    auto colon = authority.find(':');
    if (colon == std::string_view::npos) {
        u.host = std::string{authority};
    } else {
        u.host = std::string{authority.substr(0, colon)};
        try {
            unsigned long port_val = std::stoul(std::string{authority.substr(colon + 1)});
            if (port_val == 0 || port_val > 65535) return u;   // 超出范围 - 好的
            u.port = static_cast<std::uint16_t>(port_val);
        }
        catch (...) { return u; }
    }
    if (u.host.empty()) return u;
    u.ok = true;
    return u;
}

// case-insensitive 头文件 查找 agentty 的 http::Headers。
std::string header_value(const http::Headers& hh, std::string_view name) {
    auto eq_ci = [](std::string_view a, std::string_view b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            char x = a[i], y = b[i];
            if (x >= 'A' && x <= 'Z') x = char(x + 32);
            if (y >= 'A' && y <= 'Z') y = char(y + 32);
            if (x != y) return false;
        }
        return true;
    };
    for (const auto& h : hh) if (eq_ci(h.name, name)) return h.value;
    return {};
}

// POST 工作线程 与 传输层 共享的内容：构建时
// 固定的端点配置、会话（受保护）、活跃度 标志、
// 取消 源和管道的馈送端。同步，以便 工作线程 可以容纳它。
struct Conn {
    // 此出站 帧 是请求（需要响应）还是 通知（fire
    // -and-forget，预计不会响应）？
    static bool is_request(const json& j) {
        return j.is_object() && j.contains("id") && j.contains("method");
    }

    void fail_request(const maya::unlocked& cap, const std::string& frame, std::string_view reason) noexcept {
        alive_.store(false, std::memory_order_release);
        util::dbglog("mcp.http_transport.worker", reason);
        try {
            const auto parsed = json::parse(frame);
            if (!is_request(parsed)) return;
            json err = {
                {"jsonrpc", "2.0"},
                {"id", parsed.value("id", json(nullptr))},
                {"error", {{"code", -32003}, {"message",
                    std::string{"http transport worker: "} + std::string{reason}}}},
            };
            feed(cap, jsonrpc::dump_frame(err));
        } catch (...) {
            // 传输层 已经被标记为死亡；如果构建错误
            // 响应失败，引擎截止时间是最终的 回退。
        }
    }

    void post_and_feed(const maya::unlocked& cap, const std::string& frame) {
        json parsed;
        bool expects_response = false;
        try { parsed = json::parse(frame); expects_response = is_request(parsed); }
        catch (...) { /* 出站格式错误 - 只是 POST 它是原始的 */ }

        http::Request req;
        req.method    = http::HttpMethod::Post;
        req.host      = url_.host;
        req.port      = url_.port;
        req.path      = url_.path;
        req.plaintext = !url_.tls;
        req.body      = frame;
        req.headers.push_back({"content-type", "application/json"});
        req.headers.push_back({"accept", "application/json, text/event-stream"});
        // MCP 2026-07-28 头文件-based 路由 (SEP-2243)：
        // 将 JSON-RPC 方法（对于 tools/call，工具名称）显示为
        // 头文件，因此边缘网关可以在没有 解析 和 正文 的情况下实现 route/authorize。
        // 对于忽略它们的服务器无害。仅对请求 帧 有意义。
        if (parsed.is_object() && parsed.contains("method") && parsed["method"].is_string()) {
            const std::string m = parsed["method"].get<std::string>();
            req.headers.push_back({"mcp-method", m});
            if (m == "tools/call" && parsed.contains("params") &&
                parsed["params"].is_object() && parsed["params"].contains("name") &&
                parsed["params"]["name"].is_string()) {
                req.headers.push_back({"mcp-name", parsed["params"]["name"].get<std::string>()});
            }
        }
        {
            const Session sess = session_.get();
            if (!sess.id.empty())               req.headers.push_back({"mcp-session-id", sess.id});
            if (!sess.protocol_version.empty()) req.headers.push_back({"mcp-protocol-version", sess.protocol_version});
        }
        for (const auto& h : extra_headers_) req.headers.push_back(h);
        // MCP 2026-07-28 授权：如果用户为此服务器运行 `agentty mcp-login`，
        // 请附加新的不记名令牌（到期时为 auto-refreshed）。
        // extra_headers_ 中的 statically-configured 授权
        // 头文件 获胜（如果不存在，我们仅添加我们的授权）。
        if (!server_name_.empty()) {
            bool has_auth = false;
            for (const auto& h : req.headers) {
                std::string n = h.name;
                for (auto& c : n) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
                if (n == "authorization") { has_auth = true; break; }
            }
            if (!has_auth)
                if (auto bearer = oauth::bearer_for(cap, server_name_))
                    req.headers.push_back({"authorization", *bearer});
        }

        auto cancel = cancel_;

        http::Timeouts tos;
        tos.connect = std::chrono::milliseconds(10'000);
        tos.total   = timeout_;
        tos.idle    = timeout_;

        // 数据流 响应，因此我们处理 application/json（单个 正文）和
        // text/event-stream（SSE），而不缓冲无限的 数据流。
        std::string content_type;
        std::string sse_buf;       // 跨块累积 SSE 字节
        std::string json_buf;      // 累积非-SSE 正文
        bool is_sse = false;
        // 此请求为 401（如果有）。每个请求：同时运行多个 POST。
        bool        http_status_401_ = false;
        std::string resource_metadata_url_;
        int         http_status = 0;

        http::StreamHandler handler;
        handler.on_headers = [&](int status, const http::Headers& hh) {
            http_status = status;
            content_type = header_value(hh, "content-type");
            is_sse = content_type.find("text/event-stream") != std::string::npos;
            // 捕获新的-issued会话ID（仅在初始化时，
            // 但服务器可以在任何响应中发送它——只要存在就捕获）。
            if (auto sid = header_value(hh, "mcp-session-id"); !sid.empty()) {
                (void)session_.update([](const Session& s, const std::string& id) {
                    Session o = s;
                    if (o.id.empty()) o.id = id;
                    return o;
                }, sid);
            }
            if (status == 404 || status == 410) {
                // 会话 expired/unknown — 将其删除，以便下一个 调用 重新-inits。
                (void)session_.update([](const Session& s) { Session o = s; o.id.clear(); return o; });
            }
            if (status == 401) {
                // MCP 2026-07-28 授权 (RFC 9728)：服务器在
                // OAuth 后面进行门控。 解析 和 WWW-Authenticate 挑战，
                // 这样我们就可以发现可操作的错误，而不是纯粹的 传输层 故障。
                // mcp::auth::parse_challenge 定位受保护的
                // -resource-metadata URL。
                http_status_401_ = true;
                const std::string wa = header_value(hh, "www-authenticate");
                if (auto url = ::mcp::auth::parse_challenge(wa))
                    resource_metadata_url_ = *url;
            }
        };
        // 缓冲响应总量上限 字节。 io/http.cpp 中的 流式
        // 传输 路径将每个 DATA 帧 转发到 on_chunk 无需，
        // 强制执行 Request::max_body_bytes（该上限仅保护一元路径），
        // 因此恶意或有缺陷的 MCP 服务器可能会 数据流 GB — 一个无限的 json_buf，
        // 或者一个SSE 正文 永远不会发出 "\n\n" 分隔符，
        // 因此 drain_sse 永远不会擦除 — 直到进程 OOM。从 on_chunk
        // 返回 false 会中止 数据流 (RST) 并显示 HttpError。
        constexpr std::size_t kMaxResponseBytes = 64ull * 1024 * 1024;
        std::size_t total_bytes = 0;
        handler.on_chunk = [&](std::string_view chunk) -> bool {
            total_bytes += chunk.size();
            if (total_bytes > kMaxResponseBytes) {
                util::dbglog("mcp.http_transport.on_chunk",
                             "response exceeded 64 MiB cap Ã¢â‚¬â€ aborting");
                return false;
            }
            if (is_sse) {
                sse_buf.append(chunk);
                drain_sse(cap, sse_buf);
                // 即使在总上限之下，单个未终止的 SSE
                // 事件 也不得无限制地增长。
                if (sse_buf.size() > kMaxResponseBytes) {
                    util::dbglog("mcp.http_transport.on_chunk",
                                 "unterminated SSE event exceeded cap");
                    return false;
                }
            } else {
                json_buf.append(chunk);
            }
            return true;
        };

        auto result = http::default_client(::agentty::IoAccess::grant()).stream(req, std::move(handler), tos, cancel);

        if (!result) {
            // 传输层 失败。如果预期会得到响应，
            // 请合成 JSON-RPC 错误，以便等待的未来得到解决，
            // 而不是挂起直到引擎的最后期限。
            alive_.store(false, std::memory_order_release);
            if (expects_response) {
                json err = {
                    {"jsonrpc", "2.0"},
                    {"id", parsed.value("id", json(nullptr))},
                    {"error", {{"code", -32003}, {"message",
                        std::string{"http transport: "} + result.error().render()}}},
                };
                feed(cap, jsonrpc::dump_frame(err));
            }
            return;
        }

        // 一个非 2xx 回复，其 正文 不是 JSON-RPC（
        // "Unsupported MCP-Protocol-Version"，
        // 一个 nginx 错误页面）曾经被作为不可解析的 帧 丢弃，
        // 让 调用 等待其截止日期。现在用错误来回答它，携带状态和正文。
        if (http_status >= 400 && !is_sse && !http_status_401_ && expects_response) {
            bool is_rpc = false;
            try {
                const auto j = json::parse(json_buf);
                is_rpc = j.is_object() && (j.contains("error") || j.contains("result"));
            } catch (...) {}
            if (!is_rpc) {
                std::string body = json_buf.substr(0, 300);
                while (!body.empty() && (body.back() == '\n' || body.back() == '\r')) body.pop_back();
                json err = {
                    {"jsonrpc", "2.0"},
                    {"id", parsed.value("id", json(nullptr))},
                    {"error", {{"code", -32003}, {"message",
                        "HTTP " + std::to_string(http_status) + (body.empty() ? "" : ": " + body)},
                        {"data", {{"httpStatus", http_status}}}}},
                };
                feed(cap, jsonrpc::dump_frame(err));
                return;
            }
        }

        if (is_sse) {
            drain_sse(cap, sse_buf, /*齐平=*/true);
        } else if (!json_buf.empty()) {
            // 单个 JSON 正文 — 可能是一条消息，也可能是一个批处理数组。
            feed(cap, json_buf);
        } else if (expects_response) {
            // 202 以空 正文 接受请求是协议错误，
            // 但要采取防御措施：解决未来的错误。
            std::string msg;
            if (http_status_401_) {
                // MCP 2026-07-28授权：服务器需要OAuth。
                // 给用户一些可操作的东西，而不是通用的202。
                msg = "MCP server requires authorization (HTTP 401).";
                if (!resource_metadata_url_.empty())
                    msg += " Protected-resource metadata: " + resource_metadata_url_ +
                           ". Configure a bearer token in the server's headers, or use an"
                           " OAuth-authenticated endpoint.";
                else
                    msg += " Configure a bearer token in the server's headers.";
            } else {
                msg = "empty HTTP response to request";
            }
            json err = {
                {"jsonrpc", "2.0"},
                {"id", parsed.value("id", json(nullptr))},
                {"error", {{"code", http_status_401_ ? -32020 : -32603}, {"message", msg}}},
            };
            feed(cap, jsonrpc::dump_frame(err));
        }
        // 通知 与 202/empty 正文：没有什么可喂的——正确。
    }

    // 解析从`buf`中完成SSE 事件并将每个事件的`data:` 载
    // 荷供给到发动机中。 SSE 事件 以空行 ("\n\n") 结束。
    void drain_sse(const maya::unlocked& cap, std::string& buf, bool flush = false) {
        for (;;) {
            auto sep = buf.find("\n\n");
            if (sep == std::string::npos) {
                // 也容忍CRLF帧。
                sep = buf.find("\r\n\r\n");
                if (sep == std::string::npos) break;
                feed_event(cap, buf.substr(0, sep));
                buf.erase(0, sep + 4);
                continue;
            }
            feed_event(cap, buf.substr(0, sep));
            buf.erase(0, sep + 2);
        }
        if (flush && !buf.empty()) { feed_event(cap, buf); buf.clear(); }
    }

    void feed_event(const maya::unlocked& cap, const std::string& event) {
        // SSE 事件是一组`field: value`线。
        // 我们只关心`data:`行；多条数据线与'\n'连接。
        std::string data;
        std::size_t pos = 0;
        while (pos < event.size()) {
            auto nl = event.find('\n', pos);
            std::string line = event.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            pos = nl == std::string::npos ? event.size() : nl + 1;
            std::string_view lv{line};
            if (lv.starts_with("data:")) {
                lv.remove_prefix(5);
                if (!lv.empty() && lv.front() == ' ') lv.remove_prefix(1);
                if (!data.empty()) data += '\n';
                data.append(lv);
            }
            // 忽略 事件:、id:、重试: 和注释 (':') 行
        }
        if (!data.empty()) feed(cap, data);
    }

    // 将一个帧(一条消息或一批）交给对端。它读取行，因此 Pretty
    // -printed 正文 被压缩为 1。
    void feed(const maya::unlocked& cap, const std::string& payload) {
        std::string line;
        try { line = jsonrpc::dump_frame(json::parse(payload)); }
        catch (...) {
            util::dbglog("mcp.http_transport.feed", "unparseable frame");
            return;
        }
        line.push_back('\n');
        (void)feed_.write(cap, line);
    }

    const ParsedUrl                 url_;
    const std::vector<http::Header> extra_headers_;
    const std::chrono::milliseconds timeout_;
    const std::string               server_name_;   // 用于 oauth::bearer_for 查找
    const rpc::Channel              feed_;          // 我们的终点：我们推帧的地方
    struct Session { std::string id; std::string protocol_version; };
    // 整体替换，在每个POST上读取：有快照，没有锁。
    maya::snapshot<Session>         session_;
    std::atomic<bool>               alive_{true};
    // 传输层寿命的一个令牌：stop()取消飞行中的每个POST。
    const http::CancelTokenPtr      cancel_ = std::make_shared<http::CancelToken>();
};

} // 命名空间结束。
} // namespace agentty::mcp

namespace agentty::mcp {
namespace {

using ConnRef = maya::co_owned<Conn>;

// HttpTransport ………………………………………………………………………………………………………………………………………………………………………………………………………………………… ?????????????????????????????????????????????????????????????????????????????????????? ——…………………………………………………………………………………………………………………………………………………………………………………………………… ������������������������������������������������������������������������������������������������������������������� ❗️❗️❗️❗️❗️❗️❗️❗️❗️❗️❗️❗️❗️❗️❗️❗️❗️❗️❗️❗️❗️❗️❗️❗️❗️❗️❗️❗️❗️❗️❗️ ………………………………………………………………………………………………………………………………………………………………………………………………………………………………………… ～～～～～～～～～～～～～～～～～～ �������������������������������������
// �������������������������������������������������������������������������������� channel()
// 是 对端 运行的对象：在 工作线程 上写 POST 帧，读取则耗尽回复所携带的 帧。
class HttpTransport {
public:
    HttpTransport(ParsedUrl url, std::vector<http::Header> extra_headers,
                  std::chrono::milliseconds timeout, std::string server_name = {}) {
        auto [peer_end, ours] = rpc::channel_pair();
        inbound_ = std::move(peer_end);
        conn_.emplace(ConnRef::make(std::move(url), std::move(extra_headers), timeout,
                                    std::move(server_name), std::move(ours)));
    }

    void set_protocol_version(std::string v) {
        (void)(*conn_)->session_.update(
            [](const Conn::Session& s, const std::string& pv) { Conn::Session o = s; o.protocol_version = pv; return o; },
            std::move(v));
    }

    [[nodiscard]] bool alive() const noexcept {
        return (*conn_)->alive_.load(std::memory_order_acquire);
    }

    void stop(const maya::unlocked& cap) {
        // 准入是WorkerGroup的工作：workers_.stop()
        // 拒绝在池自己的锁下的进一步职位。 alive_ 是提供者读取
        // 的普通活性 标志。
        auto& c = **conn_;
        c.alive_.store(false, std::memory_order_release);
        c.cancel_->cancel();
        // 硬屏障：当这返回没有POST时，工作线程正在运行。
        workers_.stop(cap);
        if (c.feed_.interrupt) c.feed_.interrupt(cap);   // 对端的读者看到EOF
    }

    // 对端运行的通道。在 传输层 存在期间有效。
    rpc::Channel channel() {
        rpc::Channel ch;
        ch.read      = inbound_.read;
        ch.write     = [this](const maya::unlocked&, std::string_view bytes) {
            // 对端写入整行；每一个都是 JSON-RPC 帧。
            std::size_t start = 0;
            while (start < bytes.size()) {
                auto nl = bytes.find('\n', start);
                auto line = bytes.substr(start, nl == std::string_view::npos ? std::string_view::npos : nl - start);
                if (!line.empty()) dispatch(std::string{line});
                if (nl == std::string_view::npos) break;
                start = nl + 1;
            }
            return alive();
        };
        ch.interrupt = [this](const maya::unlocked& c) { stop(c); };
        return ch;
    }

private:
    void dispatch(std::string frame) {
        if (!alive()) return;   // 停止
        // 将 POST 放到其他线程执行：这里是对端的写入线程，慢服务器
        // 不能阻塞其他所有帧。
        workers_.post([](const maya::unlocked& cap, std::stop_token, ConnRef c, std::string frame) {
            try {
                c->post_and_feed(cap, frame);
            } catch (const std::exception& error) {
                c->fail_request(cap, frame, error.what());
            } catch (...) {
                c->fail_request(cap, frame, "unknown transport exception");
            }
        }, *conn_, std::move(frame));
    }

    rpc::Channel           inbound_;   // 对端的底线：帧的回复进行
    std::optional<ConnRef> conn_;      // 在构造函数中设置，之后绝不为空
    // POST 工作线程。最后声明，停止时最先等待其结束。
    util::WorkerGroup      workers_{"mcp.http_transport.worker"};
};

} // 命名空间结束。

std::shared_ptr<::mcp::cap::CapabilityProvider>
make_http_provider(const std::string& name, const HttpConfig& cfg, std::string& err) {
    ParsedUrl url = parse_url(cfg.url);
    if (!url.ok) { err = "invalid or unsupported URL: '" + cfg.url + "'"; return nullptr; }

    std::vector<http::Header> headers;
    for (const auto& [k, v] : cfg.headers) headers.push_back({k, v});

    ConnectionConfig cc;
    cc.name              = name;
    cc.client_info       = ::mcp::Implementation{"agentty", ::agentty::version()};
    cc.handshake_timeout = cfg.handshake_timeout;
    cc.call_timeout      = cfg.call_timeout;
    if (!cfg.workspace_root.empty()) {
        std::string uri = "file://" + cfg.workspace_root;
#ifdef _WIN32
        if (cfg.workspace_root.size() > 1 && cfg.workspace_root[1] == ':') uri = "file:///" + cfg.workspace_root;
#endif
        cc.roots.push_back(::mcp::Root{std::move(uri), std::string{"workspace"}, nlohmann::json::object()});
    }
    Link link;
    link.open = [url, headers, timeout = cfg.call_timeout, name]() -> Link::Opened {
        auto t = std::make_shared<HttpTransport>(url, headers, timeout, name);
        t->set_protocol_version(std::string(::mcp::kProtocolVersion));
        return {t->channel(), t};
    };
    link.alive = [](const std::shared_ptr<void>& h) {
        return h && static_cast<HttpTransport*>(h.get())->alive();
    };
    link.set_protocol_version = [](const std::shared_ptr<void>& h, std::string v) {
        if (h) static_cast<HttpTransport*>(h.get())->set_protocol_version(std::move(v));
    };
    try {
        return std::make_shared<Connection>(std::move(cc), std::move(link));
    } catch (const std::exception& e) {
        err = std::string{"http MCP server '"} + name + "' failed: " + e.what();
    } catch (...) {
        err = std::string{"http MCP server '"} + name + "' failed (unknown)";
    }
    return nullptr;
}

} // namespace agentty::mcp
