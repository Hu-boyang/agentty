#pragma once
// agentty::http-OpenSSL上的HTTP/2客户端，针对long-lived SSE 数据流进行调整。
//
// 镜像 Zed 的分层（reqwest_client → hyper → h2 → rustls），但在 C++ 中：
// tls_context (具有平台-root验证的OpenSSL SSL_CTX)
// 被打开一次，并且进程-wide客户端保持由（主机、端口）键入的HTTP/2连接的池。
// 每个请求从 池 获取一个“空闲”连接，在其 end-to-end 上运行
// 一个 数据流，然后返回该连接。尽管采用 h2 传输 格式，
// 但我们不会进行多路复用 — 在任何给定时间的任何给定连接上最多打开一个
// 数据流 ID。并发存在于 池（并行请求拨号/选择不同的连接）中，
// 而不是 h2 会话中。请参阅 http.cpp 中关于 trade
// -off 的 StreamCtx 注释：agentty 的请求形状在设计上是连续的，
// 因此我们本质上将 h2 运行为 h1.1 + keepalive
// + HPACK + PING。
//
// 取消是协作的：每个请求接受I/O循环在nghttp2 帧边界之间轮
// 询的CancelToken。触发令牌会用 RST_STREAM
// 关闭 数据流，并且阻塞 send()/stream() 调用 会立即返回，
// 因此 UI 中的 Ctrl-C 干净地终止回合。

#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include <maya/runtime.hpp>
#include "agentty/util/io.hpp"
#include "agentty/version.hpp"

namespace agentty::http {

// ---------------------------------------------------------------------------
// CancelToken —在调用者和I/O循环之间共享的协作中止信号。
// ---------------------------------------------------------------------------
// 从任何 线程 均可安全翻转。 I/O 循环在每个 nghttp2
// 帧 边界和 poll() 等待之间轮询它，因此行程会在几
// 毫秒内到达活动连接并立即到达空闲连接（套接字被关闭，
// 从而唤醒 poll()）。
//
// BACKED BY A std::stop_source，不是裸原子标志。
// 标志 只能是 POLLED，因此任何想要对 取消 做出反应的东西（而不是在自己
// 的循环内检查）都必须生成一个休眠并重新检查的 线程。 agentty 有
// 两个：ACP 服务器的工具等待（50 毫秒）和子代理 数据流 的父-cancel
// 桥接（20 毫秒）。 token() 发出真正的 std::stop_token，
// 因此这些变成 stop_callbacks，并且条件 -variable 等待，
// 在调用 cancel() 的瞬间唤醒。
//
// 成本在高频路径上不算什么。 is_cancelled() 与之前的
// 单个获取 加载 相同（stop_requested() 在每个
// 实现中都是一次原子读取），并且 I/O 循环继续像以前一样调用它。
class CancelToken {
public:
    CancelToken() = default;
    // 线程之间通过指针共享；在观察者下交换
    // 整个源将是 竞态，因此无法分配或复制它。
    CancelToken(const CancelToken&)            = delete;
    CancelToken& operator=(const CancelToken&) = delete;

    void cancel() noexcept { src_.request_stop(); }
    [[nodiscard]] bool is_cancelled() const noexcept {
        return src_.stop_requested();
    }
    /// 与std::stop_token相同的信号：在其上注册stop_callback，
    /// 或者将其交给maya::delay_for，而不是轮询。
    [[nodiscard]] std::stop_token token() const noexcept {
        return src_.get_token();
    }
private:
    std::stop_source src_;
};
using CancelTokenPtr = std::shared_ptr<CancelToken>;

// ---------------------------------------------------------------------------
// 请求/响应。
// ---------------------------------------------------------------------------
struct Header {
    std::string name;   // 按照惯例小写； nghttp2 强制执行 HTTP/2.
    std::string value;
};
using Headers = std::vector<Header>;

// SSE反-buffering指令三重奏。 Corporate/endpoint-security
// 网关通常会 缓冲区 或压缩 事件 数据流，直到积累大量 正文，因此短回复会
// 一次性到达，而长轮数则会导致 看门狗 ("no events for 120s") 停顿。
// 这三个头文件要求每一跳以增量且未压缩的方式通过帧。每个 流式传输
// 传输层（Anthropic、OpenAI-Chat、Codex/Responses）
// 都需要完全相同的三重奏 - 悬挂在这里，这样装置就不会在传输之间漂移。
// 见docs/website/proxies.md.
[[nodiscard]] inline Headers sse_no_buffer_headers() {
    return {
        {"cache-control",   "no-cache, no-transform"},
        {"pragma",          "no-cache"},
        {"accept-encoding", "identity"},
    };
}

// 将这三者附加到现有的头文件列表(对于用push_back增量
// 构建头文件的传输层)。
inline void append_sse_no_buffer(Headers& h) {
    for (auto& kv : sse_no_buffer_headers()) h.push_back(std::move(kv));
}

// 强烈-typedHTTP方法。 传输 拼写存在于一个地方 (`wire_name`)，
// 而 运行时 在此 接口边界 上从未看到 free-form 字符串 —
// "GET"、"Get" 和 "get" 在 调用 站点之间无法分歧。
enum class HttpMethod : std::uint8_t { Get, Post, Head };

[[nodiscard]] constexpr std::string_view wire_name(HttpMethod m) noexcept {
    switch (m) {
        case HttpMethod::Get:  return "GET";
        case HttpMethod::Post: return "POST";
        case HttpMethod::Head: return "HEAD";
    }
    return "GET";
}

struct Request {
    HttpMethod  method = HttpMethod::Get;
    std::string host;     // SNI + 证书验证 + Host 头文件 (e.g."api.anthropic.com")
    uint16_t    port = 443;
    // TCP-target 覆盖。  空/0→使用`host`/`port`。
    // 设置后，套接字会拨打此地址，但 TLS 仍固定在 `host` 上，
    // 因此篡改隧道端点无法 MITM 您。  使用案例：SSH 从 Air
    // -gapped 主机通过具有互联网的笔记本电脑建立反向隧道。
    std::string dial_host;
    uint16_t    dial_port = 0;
    std::string path;     // "/v1/messages"
    Headers     headers;
    std::string body;     // GET为空； UTF-8 字节 代表 POST。

    // 明文HTTP/1.1 —完全跳过TLS并通过原始套接字说出普通的HTTP/1.1。
    // 设置为本地 OpenAI-compatible 服务器（Ollama、
    // llama.cpp）超过 `http://localhost:PORT`，
    // 该服务器不服务 TLS，也不服务 h2。默认 false 将每个托管 上游 保留在
    // TLS + HTTP/2 路径上。当 true 时：`send` 执行 h1.1 一元交换，
    // `stream` 执行 h1.1 chunked-SSE 读取；均未触及 OpenSSL、
    // 连接 池 或 nghttp2。
    bool plaintext = false;

    // 缓冲的(一元、非-streaming)响应正文的硬上限。一旦超过，
    // 调用 返回 带类型的 `body_too_large` HttpError，
    // RST_STREAMs 数据流，所以我们不再继续阅读。默认值 16
    // MiB 对于 HTTP API 调用 来说是慷慨的，但也足够有限，
    // 以至于失控的 上游（测试工具、配置错误的代理、正文-replay
    // 循环）无法在任何人注意到之前 OOM 进程。
    //
    // 具有已知-tiny响应的呼叫者应该加强这一点：
    //   ·auth (OAuth令牌交换/刷新）：〜1 KB JSON → 1 MiB
    //   ·Anthropic/v1/models:~30KBJSON→1MiB
    //   ·web_fetch工具：任意用户-pasted URL→保留默认值
    //
    // 流式传输响应(Client::stream)完全绕过这
    // 一点——每个SSE块流过on_chunk 回调而没有缓冲。
    std::size_t max_body_bytes = 16ull * 1024 * 1024;

    // SSRF守卫在RESOLVED地址上。当为 true 时，拨号器 re-checks
    // 每 IP getaddrinfo() 返回 private/loopback/link-local/
    // cloud-metadata 范围和 REFUSES 以连接到其中任何一个 (带
    // 类型的 HttpError::connect "blocked address (SSRF guard)")。
    // 这将关闭 DNS 重新绑定：通过 name-based 过滤器但解析
    // 为 169.254.169.254 / 127.0.0.1 / RFC1918
    // 主机的公共主机名在套接字处被捕获，其中目标是权威且不可欺骗的。
    //
    // 默认为 false，因此提供商流量和 LOCAL 模型服务器（127.0.0.1 上的
    // Ollama / llama.cpp）不会受到影响 - 只有拨打任意 model-supplied
    // URL 的 Web 工具 (web_fetch / web_search)，将其设置为 true。
    bool ssrf_guard = false;
};

// 已解析 env-var-driven 拨号覆盖。  格式："host" 或 "host:port"
// — 端口默认为 443。当环境变量未设置或 ill-formed 时为空。
// 在第调用处缓存(进程生命周期)；环境变量在实践中永远不会改变，并且每个请求的 re
// -reading getenv 是浪费的。  通过将 `host` / `port`
// 复制到 `Request::dial_host` / `Request::dial_port`
// （以及 `Client::prewarm` 上的同一对）进行申请 - TLS / cert
// / Host-header 全部固定在真实的 上游 上，因此篡改隧道端点无法对您进行
// MITM。
struct DialOverride {
    std::string host;       // 空→无覆盖
    uint16_t    port = 0;
    [[nodiscard]] bool active() const noexcept { return !host.empty(); }
};

// 对数字 IP LITERAL (点分 -quad "127.0.0.1" 或 v6 文字，
// 可选地用方括号括起来)的 SSRF 范围检查。如果地址是环回/RFC1918私有/链接-local
// （包括169.254.169.254元数据）/CGNAT/ULA/多播-i.e，
// 则返回true。不受信任的网络 URL 不得到达的目标。
// 非-numeric或不可解析的输入返回true（失败关闭）。
// 当设置 Request::ssrf_guard 时，这与拨号程序应用于
// 每个 RESOLVED 地址的谓词相同；导出后，范围为 unit-testable，
// 没有实时 DNS 解析。
[[nodiscard]] bool ssrf_ip_blocked(std::string_view ip_literal) noexcept;

// 覆盖对`api.anthropic.com`的请求（聊天完成、模型列表）。
// 由 `AGENTTY_API_HOST` 驱动。
[[nodiscard]] const DialOverride& agentty_api_host_override();

// 覆盖对OAuth主机(`platform.claude.com`)的请求——
// 令牌交换和刷新。  由 `AGENTTY_OAUTH_HOST` 驱动。
// air-gapped 主机需要此和 `agentty_api_host_override`
// 才能在令牌过期后继续工作：刷新是单独的 上游，否则在 getaddrinfo
// 上会失败。
[[nodiscard]] const DialOverride& agentty_oauth_host_override();

// SOCKS5代理。  由`AGENTTY_SOCKS_PROXY`驱动；与命名覆盖相同的
// `host[:port]` 语法（端口默认为 443 — 但大多数 SSH-side SOCKS
// 设置需要像 `localhost:1080` 这样的显式端口）。
//
// 设置后，每个出站 TCP 拨号都通过该 SOCKS5 代理路由，
// 而不是直接路由 `getaddrinfo`-ing 目的地。
// 代理接收目的地作为域名 (ATYP=DOMAIN)，因此 DNS 发生在
// 代理端 - 这对于自己的 DNS 无法看到公共互联网的 air
// -gapped 主机来说是完全正确的。  TLS 仍然通过 SOCKS
// -tunnelled 套接字与*真实的*上游 协商 end-to-end；
// 代理无法 MITM 你。
//
// 战胜 AGENTTY_API_HOST / AGENTTY_OAUTH_HOST：单个
// 隧道转发每个目的地 — 聊天、OAuth 刷新、web_fetch、
// web_search — 因此您不必枚举主机。  `ssh -R 1080`（port
// -only 形式，无主机：端口）使 OpenSSH 本身成为远程上的 SOCKS 服务器，
// 因此不需要额外的守护进程。
[[nodiscard]] const DialOverride& agentty_socks_proxy();

struct Response {
    int     status = 0;
    Headers headers;
    std::string body;
};

// ---------------------------------------------------------------------------
// HttpError——Client::send/数据流返回的带类型的失败值。
// ---------------------------------------------------------------------------
// 替换`std::expected<T, std::string>`，使得下游呼叫者
// （特别是`provider::error_class::classify`)可以
// 在`kind`上分派而不是子串-matching释放-form消息。添加新的故障
// 模式是一个新的 枚举 条目 + `to_string` / `is_transient`
// 中的一个新开关臂 - 编译器 告诉您所有需要更改的地方。
//
// `http_status`仅当kind==Status时才有意义；否则为 0。
// `detail`用于人类阅读（日志、错误横幅）——
// 绝不用于编程分派。
enum class HttpErrorKind : std::uint8_t {
    Cancelled,    // CancelToken 跳闸 — 用户 -initiated (Esc) 或 看门狗
    Resolve,      // DNS/getaddrinfo 失败
    Connect,      // TCP 连接被拒绝/网络无法访问
    Tls,          // TLS握手失败（证书、ALPN等）
    Protocol,     // HTTP/2协议错误（nghttp2 帧违规）
    SocketHangup, // POLLHUP/POLLERR/EPIPE 中期-request
    Timeout,      // 连接、总计或空闲期限已过
    PeerClosed,   // 对端 半-closed 完成响应之前
    Status,       // 收到状态 >= 400 的响应
    Body,         // 正文 解析故障或超出大小限制
    Unknown,      // 意外的-不应该发生
};

struct HttpError {
    HttpErrorKind kind        = HttpErrorKind::Unknown;
    int           http_status = 0;       // 有效当且仅当种类==状态
    std::string   detail;                // 人类-readable
    // 一旦非-idempotent请求接收到成功响应头，则为真。
    // 自动重试可以重复接受的server-side工作。
    bool          non_replayable = false;

    // "[h2 timeout] no bytes for 45s"
    // — 当 UI 不关心类型分支时的默认字符串化。分层错误 调用
    // 当包装成它们自己的 带类型的 错误时。
    [[nodiscard]] std::string render() const;

    // 工厂用于一个-line返回-site习语。
    [[nodiscard]] static HttpError cancelled(std::string d = "cancelled")
        noexcept { return {HttpErrorKind::Cancelled, 0, std::move(d)}; }
    [[nodiscard]] static HttpError resolve(std::string d)
        noexcept { return {HttpErrorKind::Resolve, 0, std::move(d)}; }
    [[nodiscard]] static HttpError connect(std::string d)
        noexcept { return {HttpErrorKind::Connect, 0, std::move(d)}; }
    [[nodiscard]] static HttpError tls(std::string d)
        noexcept { return {HttpErrorKind::Tls, 0, std::move(d)}; }
    [[nodiscard]] static HttpError protocol(std::string d)
        noexcept { return {HttpErrorKind::Protocol, 0, std::move(d)}; }
    [[nodiscard]] static HttpError socket_hangup(std::string d)
        noexcept { return {HttpErrorKind::SocketHangup, 0, std::move(d)}; }
    [[nodiscard]] static HttpError timeout(std::string d)
        noexcept { return {HttpErrorKind::Timeout, 0, std::move(d)}; }
    [[nodiscard]] static HttpError peer_closed(std::string d)
        noexcept { return {HttpErrorKind::PeerClosed, 0, std::move(d)}; }
    [[nodiscard]] static HttpError status(int s, std::string d)
        noexcept { return {HttpErrorKind::Status, s, std::move(d)}; }
    [[nodiscard]] static HttpError body(std::string d)
        noexcept { return {HttpErrorKind::Body, 0, std::move(d)}; }
    [[nodiscard]] static HttpError unknown(std::string d)
        noexcept { return {HttpErrorKind::Unknown, 0, std::move(d)}; }

    // 如果 重试 可能成功（暂时的 传输层 条件），则为真。
    // 由调用者的 重试 策略使用。 取消、状态 4xx（408、
    // 429 除外）和 正文 错误被视为 终端。
    [[nodiscard]] bool is_transient() const noexcept;
};

[[nodiscard]] std::string_view to_string(HttpErrorKind k) noexcept;

// 公共带类型的结果别名——这些是调用者模式-match所使用的。
using HttpResult       = std::expected<Response, HttpError>;
using HttpStreamResult = std::expected<void,     HttpError>;

// 流式传输 回调。当响应 头文件 到达时，on_headers 触发一次；
// 每个 DATA/body 板都会触发 on_chunk 火灾。
// 从 on_chunk 返回 false 会中止 数据流。 on_activity
// 是实际入站 传输 字节 的节流活跃信号。 on_buffered_wait
// 报告显式有界状态，其中 HTTP 中介处于活动状态，但保留响应 DATA。
struct StreamHandler {
    std::function<void(int status, const Headers&)>     on_headers;
    std::function<bool(std::string_view body_chunk)>    on_chunk;
    std::function<void()>                               on_activity;
    std::function<void()>                               on_buffered_wait;
};

// ---------------------------------------------------------------------------
// 超时。
// ---------------------------------------------------------------------------
// `connect`/`total`是绝对的每-operation上限。
// `idle` 和 `ping` 是 long-lived 数据流 的活跃护栏，
// 因为沉默的 对端（半死的 TCP，代理停顿）不会产生 帧，也不会产生错误 -
// poll() 只是永远返回 0。我们需要一个 inter-event 空闲时钟
// + HTTP/2 PING 来检测这种情况并快速失败，以便调用者可以 重试 或
// 显示错误而不是挂起。
//
//   连接严格-完成TCP+TLS+h2前导码的时间。
//   整个请求的总绝对上限； 0 = 无界 (数据流)。
//   如果在这么长时间内没有收到 字节，则空闲错误输出； 0 = 禁用。
//   在没有入站 字节 的情况下，ping 发送 HTTP/2 PING，
//            以探测 对端 并诱导回复（如果连接仍然存在，
//            则 PING ACK 会重置空闲时钟）。 0 = 禁用。
//
// 对于一元请求，我们将 idle/ping 保留为 0 并依赖于 `total`。
// 对于 数据流，我们设置了两者，以便静默 对端 在已知范围内跳动 `idle`。
struct Timeouts {
    std::chrono::milliseconds connect{10'000};
    std::chrono::milliseconds total  {0};
    std::chrono::milliseconds idle   {0};
    std::chrono::milliseconds ping   {0};
    // 由连接、TLS、重试和正文读取共享的内部end-to-end上限。
    // Client::stream从总数中填充；呼叫者通常将其留空。
    std::optional<std::chrono::steady_clock::time_point> absolute_deadline;
};

// ---------------------------------------------------------------------------
// 客户端。一个实例 = 一个进程-wide HTTP/2 连接池。
// ---------------------------------------------------------------------------

// User-Agent来自agentty/version.hpp，这是构建版本存在
// 的唯一位置(版本碰撞重新编译一个文件)。 agentty 作为远程服务的
// 身份记录它 - 产品名称和主页 URL - 位于 util/identity.hpp 中，
// 这是应用程序归因发送的内容（请参阅
// ProviderDescriptor::attribution）。

class Client {
public:
    struct Config {
        // UA包括构建版本；覆盖测试。
        std::string user_agent = ::agentty::user_agent();
        // 设置时，跳过TLS链验证——传输与curl中的`-k`匹配。
        // 在 ctor 路径中自动遵循 AGENTTY_INSECURE=1 env。
        bool insecure = false;
    };

    Client();
    explicit Client(Config cfg);
    ~Client();

    Client(const Client&)            = delete;
    Client& operator=(const Client&) = delete;

    // 阻止一元请求。响应 正文 已完全缓冲。
    // `cancel`可以为空； non-null 令牌在 I/O 等待之间以及 帧 处于
    // -flight 期间轮询。失败时返回 带类型的 `HttpError` — 调用者在
    // `kind` 上返回 分派，而不是 substring-matching 详细信息。
    [[nodiscard]] HttpResult
    send(const Request& req,
         Timeouts timeouts = {},
         CancelTokenPtr cancel = {});

    // 阻止流式传输请求。当 对端 闭合 数据流、取消 跳闸或
    // on_chunk 返回 false 时返回。
    // 正文 块到达调用 线程（没有内部 线程 — 这意味着在拥有轮次
    // 生命周期 的 工作线程 内运行）。
    [[nodiscard]] HttpStreamResult
    stream(const Request& req,
           StreamHandler handler,
           Timeouts timeouts = {},
           CancelTokenPtr cancel = {});

    // Fire-and-forget:预热到分离的线程上的(主机，
    // 端口)的TLS连接，因此第一个实际请求跳过握手。对调用多次安全；
    // 第一次成功后的幂等。  `dial_host`/`dial_port`
    // 覆盖 TCP 目标，而 TLS 保持固定在 `host` 上（镜像请求
    // 字段 - 请参阅此处的 SSH-tunnel 用例）。
    void prewarm(std::string host, uint16_t port = 443,
                 std::string dial_host = "", uint16_t dial_port = 0);

    // 向每个未完成的prewarm() 线程发出信号以中止其拨盘和JOIN。
    // 在进程退出之前调用 必须：prewarm() 的 detached-thread
    // 模型否则会留下 线程 mid-SSL_connect，而 CRT /
    // OpenSSL 静态状态在其下被拆除 - 当应用程序在启动后几毫秒内退出时，
    // 数据 竞态 会显示为堆损坏 (Windows STATUS_HEAP_CORRUPTION
    // 0xC0000374) （e.g。直接管道-stdin EOF）。
    // 幂等;安全至 调用，飞行中无需预热。加入：`unlocked`。
    void join_prewarm(const maya::unlocked& cap) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Process-wide默认客户端-惰性，在首次访问时构建，
// 在所有调用站点之间共享。相当于Zed的`GlobalHttpClient`。
// 拨出是 IO，因此获取它需要 I/O。
[[nodiscard]] Client& default_client(Io);
// 如果已经创建了默认客户端，则为空。对于退出路径：制作
// 一个只是将 加载 加入到整个 CA 捆绑包中。
[[nodiscard]] Client* default_client_if_made() noexcept;

namespace test {
// 由成帧回归使用的纯增量HTTP/1块解码器接口边界。
[[nodiscard]] std::expected<std::string, std::string>
decode_chunked(const std::vector<std::string>& fragments);
[[nodiscard]] std::expected<bool, std::string>
is_chunked(const Headers& headers);
} // namespace test

} // namespace agentty::http

// CancelToken是共享可变对象，因此Sendable默认拒绝
// shared_ptr。共享是这里的重点：一侧绊倒它（Esc，新转弯，
// session/cancel），而 工作线程 观看它。它只容纳一个
// std::stop_source，正是为此而构建的，因此指针可能会交叉。
MAYA_SENDABLE(agentty::http::CancelTokenPtr);
// 出于同样的原因，它是同步：一个std::stop_source，
// 仅通过其线程-safe 调用使用，并且该类没有交换它的分配。
MAYA_SYNC(agentty::http::CancelToken);
