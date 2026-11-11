#include "agentty/io/http.hpp"

#include "agentty/util/logx.hpp"
#include "agentty/io/fsm.hpp"
#include "agentty/io/inflate.hpp"
#include "agentty/io/tls.hpp"
#include "agentty/util/env.hpp"

// 当预热拨盘楔入阻塞getaddrinfo()中时，预热拨盘可以在关机时为DETACHED
// (参见join_prewarm())。但更常见的 竞态 是一个预热 TLS 握手，
// 当进程退出时，仍在后台 线程 中运行注册机：main() 通过 join_prewarm
// () 耗尽它，但触发预热且从不耗尽的独立 二进制程序 (e.g。一个测试) 让 线
// 程 比 main() 和 竞态 更长寿。 C-runtime atexit
// 处理函数 — mimalloc 的 mi_process_detach（释放 线程
// 分配的堆）和 OpenSSL 的 OPENSSL_cleanup（破坏
// CRYPTO rwlocks EVP_PKEY_generate 读取）。
// 任一故障都是关闭 SIGSEGV。两个缓解措施：
//   1.mi_option_destroy_on_exit=0(下面的加载-time ctor)——mimalloc离开
//      OS的堆，而不是在实时分离的表盘下释放它。
//   2. AGENTTY_NO_PREWARM=1（在Client::prewarm中检查）——测试工具
//      选择完全退出拨号，因此不会产生不排水网络线程。
//      测试不需要延迟时间；该应用程序仍在预热。
#if defined(AGENTTY_USE_MIMALLOC)
#  include <mimalloc.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <random>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <limits>
#include <optional>
#include <string>
#include <stop_token>
#include <thread>

#include <maya/runtime.hpp>
#include <unordered_map>
#include <utility>
#include <vector>

// MSVC不提供ssize_t (POSIX-only)。 nghttp2.h 将其用于由
// NGHTTP2_NO_SSIZE_T 保护的传统读取 回调 typedef。在包含之前从
// BaseTsd 开始定义它，所以 nghttp2.h 编译 干净地定义，而不跳过 typedef。
#if defined(_MSC_VER) && !defined(__clang__)
#  include <BaseTsd.h>
   using ssize_t = SSIZE_T;
#endif
#include <nghttp2/nghttp2.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

// ---------------------------------------------------------------------------
// 平台套接字垫片。  shim 下面的所有内容都将套接字视为支持
// read/write/poll/close 的 `int`； BSD +
// Winsock 表面上一致，但它们在 头文件 和一些生命周期 调用 上有所不同。
// ---------------------------------------------------------------------------
#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
#  ifdef _MSC_VER
#    pragma comment(lib, "ws2_32.lib")
#  endif
using socket_t = SOCKET;
constexpr socket_t kBadSocket = INVALID_SOCKET;
static int sock_last_err() { return WSAGetLastError(); }
static int sock_close(socket_t s) { return ::closesocket(s); }
static int sock_poll(pollfd* fds, unsigned n, int ms) { return ::WSAPoll(fds, n, ms); }
// errno-equivalent 常量用于错误-classifying 重试。
static bool sock_in_progress(int e) { return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
static bool sock_intr(int e) { return e == WSAEINTR; }
static void sock_set_nonblock(socket_t s) {
    u_long nb = 1; ::ioctlsocket(s, FIONBIO, &nb);
}
#else
#  include <arpa/inet.h>
#  include <errno.h>
#  include <fcntl.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <poll.h>
#  include <pthread.h>
#  include <signal.h>
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <unistd.h>
using socket_t = int;
constexpr socket_t kBadSocket = -1;
static int sock_last_err() { return errno; }
static int sock_close(socket_t s) { return ::close(s); }
static int sock_poll(pollfd* fds, unsigned n, int ms) { return ::poll(fds, n, ms); }
static bool sock_in_progress(int e) { return e == EINPROGRESS || e == EWOULDBLOCK || e == EAGAIN; }
static bool sock_intr(int e) { return e == EINTR; }
static void sock_set_nonblock(socket_t s) {
    int fl = ::fcntl(s, F_GETFL, 0);
    if (fl >= 0) ::fcntl(s, F_SETFL, fl | O_NONBLOCK);
}
#endif

namespace agentty::http {

using clock_t_ = std::chrono::steady_clock;
using ms_t     = std::chrono::milliseconds;

#if defined(AGENTTY_USE_MIMALLOC)
namespace {
// 当这个TU 加载时运行一次。关闭时分离的预热拨号 — 或者独立 二
// 进制程序 中的一个从不 调用 join_prewarm() —
// 继续从堆中分配，而 mimalloc 的 atexit 处理函数 (
// mi_process_detach) 释放它，在释放的元数据上出错。
// 将堆留给 OS 在真正的进程退出时回收，这是分离路径已经假设的。
const bool mimalloc_no_destroy_on_exit = [] {
    mi_option_set(mi_option_destroy_on_exit, 0);
    return true;
}();
} // 命名空间结束。
#endif

// ---------------------------------------------------------------------------
// HttpError实施。静态工厂内联在头文件中；
// 这些是需要切换手臂的 渲染 + 重试-policy 助手。
// ---------------------------------------------------------------------------

std::string_view to_string(HttpErrorKind k) noexcept {
    switch (k) {
        case HttpErrorKind::Cancelled:    return "cancelled";
        case HttpErrorKind::Resolve:      return "resolve";
        case HttpErrorKind::Connect:      return "connect";
        case HttpErrorKind::Tls:          return "tls";
        case HttpErrorKind::Protocol:     return "h2";
        case HttpErrorKind::SocketHangup: return "hangup";
        case HttpErrorKind::Timeout:      return "timeout";
        case HttpErrorKind::PeerClosed:   return "peer_closed";
        case HttpErrorKind::Status:       return "status";
        case HttpErrorKind::Body:         return "body";
        case HttpErrorKind::Unknown:      return "unknown";
    }
    return "unknown";
}

std::string HttpError::render() const {
    std::string out = "[";
    out += to_string(kind);
    out += "] ";
    out += detail;
    if (kind == HttpErrorKind::Status && http_status > 0) {
        out += " (HTTP ";
        out += std::to_string(http_status);
        out += ")";
    }
    return out;
}

bool HttpError::is_transient() const noexcept {
    switch (kind) {
        // Re-issue-safe 传输层 打嗝。
        case HttpErrorKind::Resolve:
        case HttpErrorKind::Connect:
        case HttpErrorKind::Tls:
        case HttpErrorKind::Protocol:
        case HttpErrorKind::SocketHangup:
        case HttpErrorKind::Timeout:
        case HttpErrorKind::PeerClosed:
            return true;
        // 服务器-side 加载脱落/速率限制/one-shot瞬变。
        case HttpErrorKind::Status:
            return http_status == 408 || http_status == 429
                || http_status == 502 || http_status == 503
                || http_status == 504 || http_status == 529;
        // 用户-driven或形状错误——从不re-issue。
        case HttpErrorKind::Cancelled:
        case HttpErrorKind::Body:
        case HttpErrorKind::Unknown:
            return false;
    }
    return false;
}

namespace {

// -----------------------------------------------------------------------
// One-time Winsock初始化。  匹配的 WSACleanup 被
// 故意跳过 — process-lifetime，并在 工作线程 以难以诊断
// 的方式耗尽 -flight 请求中的杀戮时拆除 Winsock。
// -----------------------------------------------------------------------
void ensure_net_init() {
#if defined(_WIN32)
    static std::once_flag once;
    maya::once(once, []{
        WSADATA data;
        (void)WSAStartup(MAKEWORD(2, 2), &data);
    });
#endif
}

// -----------------------------------------------------------------------
// 超时 助手：截止日期前剩余的毫秒数，
// 限制在合理的上限，因此当总计==无界时，
// 我们的轮询循环仍然及时检查 取消 令牌。
// -----------------------------------------------------------------------
int remaining_ms(std::optional<clock_t_::time_point> deadline, int cap_ms) {
    if (!deadline) return cap_ms;
    auto now = clock_t_::now();
    if (now >= *deadline) return 0;
    auto rem = std::chrono::duration_cast<ms_t>(*deadline - now).count();
    return static_cast<int>(std::min<int64_t>(rem, cap_ms));
}

// -----------------------------------------------------------------------
// 端点 — 池 键。  身份是（主机，端口，dial_host，
// dial_port）；对同一逻辑 API 的两个请求会折叠到同一
// 池 插槽中。  拨号字段是密钥的一部分，因为具有相同 SNI
// 主机但不同 TCP 目标（e.g。一个直接，一个通过 SSH
// 隧道）的两个端点是物理上不同的连接。
// -----------------------------------------------------------------------
struct Endpoint {
    std::string host;       // SNI + 证书 + Host 头文件
    uint16_t    port = 443;
    std::string dial_host;  // TCP目标覆盖；空=使用主机
    uint16_t    dial_port = 0; // TCP端口覆盖； 0 = 使用端口
    // Re-validate在connect()之前针对SSRF
    // 黑名单解析了IP。从 Request::ssrf_guard 携带。
    // 下面的 池 身份的 不 部分（对同一主机的受保护和不受
    // 保护的请求无论如何都会解析为相同的地址；保护只会加强，
    // 并且 web-tool 端点不会与提供商流量共享 池）。
    bool        ssrf_guard = false;

    [[nodiscard]] std::string_view tcp_host() const noexcept {
        return dial_host.empty() ? std::string_view{host} : std::string_view{dial_host};
    }
    [[nodiscard]] uint16_t tcp_port() const noexcept {
        return dial_port == 0 ? port : dial_port;
    }

    bool operator==(const Endpoint& o) const {
        return port == o.port && host == o.host
            && dial_port == o.dial_port && dial_host == o.dial_host;
    }
};
struct EndpointHash {
    size_t operator()(const Endpoint& e) const noexcept {
        // 主机上的 djb2 + dial_host 加上两个端口 — 对于
        // agentty 曾经与之通信的少数端点来说已经足够好了。
        size_t h = 5381;
        for (char c : e.host) h = ((h << 5) + h) + static_cast<unsigned char>(c);
        for (char c : e.dial_host) h = ((h << 5) + h) + static_cast<unsigned char>(c);
        h ^= static_cast<size_t>(e.port) * 0x9E3779B97F4A7C15ull;
        h ^= static_cast<size_t>(e.dial_port) * 0xBF58476D1CE4E5B9ull;
        return h;
    }
};

// Forward-declare使得StreamCtx可以将back-pointer携带到其连接。
// on_frame_recv 回调使用它来记录连接上的GOAWAY状态（
// 其比任何单个请求都长）——参见下面的Connection::is_alive和
// on_frame_recv的GOAWAY臂。
class Connection;

// -----------------------------------------------------------------------
// StreamCtx — 当 数据流 正在飞行时，每个 -request 由连接方拥有。
// Single-stream-per-connection 的设计：我们放弃了
// h2 的主要功能（在一个连接上复用 N 数据流），并以 h1.1 + keepalive
// + HPACK + PING 的形式运行 h2。  具体来说：
//
//   ·池由（主机、端口）键入。  一个请求占用一个空闲连接，
//     运行一次数据流 end-to-end，并返回连接。
//     第二个并发请求等待空闲连接或拨打新连接。
//   · 传输 上的 h2 会话仅在某一时刻有一个打开的 数据流 ID
//     时间。  每字节我们支付框架+HPACK费用；
//     我们兑现了零的多重效益，这些成本旨在摊销。
//
// 为什么这对于 agentty 来说很好（与我们选择它的原因不同）：
//   agentty的请求形状本质上是顺序的——一次
//   聊天数据流，在助理回合结束后连续调度工具调用。
//   运行时中的任何内容都不会并行地向同一提供者请求两个数据流，
//   因此未使用的多路复用器实际上不会花费我们
//   任何超出每个请求百分之几的帧开销的成本。
//
// 我们为什么选择它（诚实的版本）：
//   这是廉价的默认值。  一个 h2 会话上的 Multi-stream
//   生命周期意味着通过共享连接簿记来路由每个 -stream 唤醒、
//   中止信号和 头文件 状态 — 这并不困难，但对于不需要它的工作负载来说却是
//   真正的工作。  之前的评论引用了"the CURLSH-class bug"
//   作为理由；这是真的（数据流 上的共享手柄状态是一把已知的枪，
//   libcurl 的 CURLSH 目录中有一半 -dozen 变体），
//   但这是 -hoc 后的理由，而不是触发器。  触发器是
//   不需要并发，就不应为并发搭建结构。
//
//
// 为什么是h2而不是h1.1：
//   Anthropic的优势服务于两者。  Claude 代码协商 h2，agentty
//   的 传输层 模仿 Claude 代码的 传输 形状，因此 OAuth
//   令牌被接受（请参阅 project_claude_code_wire 内存）。
//   h1.1 之上的边际胜利（HPACK 的较小 重试 头文件、
//   PING-based keepalive 原语）很便宜；真正的原因是传输
//   -format奇偶校验。
//
// 如果未来的工作负载需要并发数据流，则提升这一点的成本是真实的（每
// 个-stream 取消路由、数据流-id→会话锁下的呼叫者映射、
// 围绕RST_STREAM与飞行中的头文件的排序规则）。
// 在您付费之前，请勿重复 Multiplex-is-free 民间传说。
// -----------------------------------------------------------------------
struct StreamCtx {
    // 请求方
    const std::string* body = nullptr;   // POST body; null for GET
    size_t             body_off = 0;

    // 响应方——完全缓冲（发送）或流式传输（数据流)
    int                 status = 0;
    Headers             headers;
    std::string         buffered_body;
    StreamHandler*      handler = nullptr;  // stream()的非-null
    bool                handler_aborted = false;
    bool                headers_delivered = false;
    // 一旦真实响应 DATA 块已被交给调用者（on_chunk 用 正文
    // 字节 触发），则为真。这是 数据流 的 *语义 * 提交点 - 与
    // headers_delivered 不同，这仅意味着 :status
    // + 头文件 块到达。收到 :status 但在任何 SSE 载荷
    // 之前被 RST_STREAMed / GOAWAY 处理的 数据流
    // 仍然是重播-safe：没有 content_block 到达 状态更新函数，
    // 因此 re-dialing 无法复制 tool_use 块或文本。
    // 数据流 重试 循环关闭此而不是 headers_delivered，
    // 因此 过期 池连接在 头文件 块之后的边缘 RST 会获得透明的
    // re-dial 而不是 user-visible 错误。
    bool                data_delivered = false;

    // 生命周期
    int32_t stream_id = -1;
    bool    completed = false;
    bool    reset     = false;   // 对端 发送 RST_STREAM
    // 结果传播——如果我们在 数据流 期间发生中止，
    // 我们将保留原因以交给调用者。
    std::string error;

    // Per-request 一元 正文 上限。镜子Request::max_body_bytes；
    // 由 run() 复制，因此 on_data_chunk 回调（仅
    // 看到 user_data → StreamCtx）可以强制执行
    // per-call 限制，而无需返回请求引用。
    std::size_t max_body_bytes = 16ull * 1024 * 1024;

    // 返回-pointer到所属连接。 on_frame_recv 回调（
    // 通过 user_data → StreamCtx 查看会话）使用它
    // 来将连接 -level 事件（当前只是 GOAWAY 的
    // last_stream_id）转发到超出请求寿命的状态。
    Connection* conn = nullptr;
};

// -----------------------------------------------------------------------
// 连接：一个TCP+TLS+nghttp2会话。  一次仅由一个请求使用；
// 数据流 关闭后返回到 池。  非-copyable； unique_ptr
// -owned。
// -----------------------------------------------------------------------
class Connection {
public:
    Connection(socket_t fd, tls::SSL* ssl, nghttp2_session* session,
               Endpoint ep)
        : fd_(fd), ssl_(ssl), session_(session), endpoint_(std::move(ep)) {}

    ~Connection() {
        if (session_) nghttp2_session_del(session_);
        tls::free_ssl(ssl_);
        if (fd_ != kBadSocket) sock_close(fd_);
    }

    Connection(const Connection&)            = delete;
    Connection& operator=(const Connection&) = delete;

    [[nodiscard]] socket_t fd() const { return fd_; }
    [[nodiscard]] nghttp2_session* session() { return session_; }
    [[nodiscard]] const Endpoint& endpoint() const { return endpoint_; }

    // 如果该连接刚刚从 池 (重复使用的套接字)发出而不是新拨出，
    // 则为真。 数据流 重试 循环读取此内容来决定早期的
    // RST/GOAWAY 是否是值得免费透明的 re-dial
    // 的 过期-pool 工件，而不是应计入预算的真正新鲜的
    // -dial 故障。由 Pool::acquire 设置，
    // 释放时清除（从下一个收单方的角度来看，返回 池 的
    // 连接再次启动 "fresh"）。
    [[nodiscard]] bool was_pooled() const noexcept { return was_pooled_; }
    void set_pooled(bool v) noexcept { was_pooled_ = v; }

    // 在该获取之前，该连接在 池 中空闲了多长时间，
    // 以毫秒为单位（0 表示新拨号）。仅用于诊断：-stream
    // 中期挂起，其空闲年龄聚集在固定值是 proxy/edge 空闲 超时，
    // 可以通过尽早逐出来修复；随机年龄是普通的网络丢失。
    [[nodiscard]] long long idle_ms() const noexcept { return idle_ms_; }
    void set_idle_ms(long long v) noexcept { idle_ms_ = v; }

    // 池重用的活性。两个阶段：
    //   (1)nghttp2认为会话有工作要做或者至少
    //       是可读的。如果两者都不是，则会话结束。
    //   (2)GOAWAY门。 对端 发送 last_stream_id = N 的
    //       GOAWAY 后，RFC 7540 §6.8 禁止客户端发起任何
    //       id > N 的 数据流 — 服务器已承诺 RST。较早的检查 (
    //       nghttp2_session_want_read || want_write)
    //       本身就足够 不 了：接收 GOAWAY 的会话在 -flight
    //       数据流' 帧 中耗尽时仍然是 wants_read，并且在它 ACK
    //       SETTINGS 或回显 PING 时仍然是 wants_write。
    //       连接在协议级别看起来是活动的，但是对于新的数据流
    //       来说是死的-walking。
    //
    //       如果没有这个门，来自池的下一次获取将返回这个尸体，
    //       新请求得到RST_STREAM，
    //       重试层吃掉一轮-trip(正确性完好，性能成本)。
    //       在供应商限电情况下咬得最厉害，其中GOAWAY
    //       是主要信号。
    [[nodiscard]] bool is_alive() const {
        if (!session_) return false;
        if (!nghttp2_session_want_read(session_) &&
            !nghttp2_session_want_write(session_)) {
            return false;
        }
        if (goaway_received_) {
            // nghttp2_session_get_next_stream_id
            // 返回我们在下一个nghttp2_submit_request、
            // NOT上分配的ID，WILL是我们最后使用的ID。
            // 直接与对端的last_stream_id进行比较；如果相等，
            // 我们仍然被允许（对端 致力于通过该 id 进行处理），> 表示拒绝。
            const int32_t next = nghttp2_session_get_next_stream_id(session_);
            if (next > goaway_last_stream_id_) return false;
        }
        return true;
    }

    // 当 GOAWAY 帧 到达时从 on_frame_recv
    // 回调 呼叫。 幂等 — 如果有多个 GOAWAY 到达（
    // 允许 对端 发送一个紧缩的 GOAWAY），
    // 我们保留最小的 last_stream_id，这是真正限制我们的那个。
    void mark_goaway(int32_t last_stream_id) noexcept {
        if (!goaway_received_) {
            goaway_received_ = true;
            goaway_last_stream_id_ = last_stream_id;
        } else if (last_stream_id < goaway_last_stream_id_) {
            goaway_last_stream_id_ = last_stream_id;
        }
    }

    // 在此连接上执行单个request/stream。
    // 连接在进入时必须处于空闲状态；干净返回后即可再次使用。
    std::expected<void, HttpError> run(
        const Request& req,
        StreamCtx& sctx,
        Timeouts tos,
        CancelTokenPtr cancel);

    // 驱动会话直到事件沿一个方向刷新所有待处理 帧，
    // 由攻击者用来完成SETTINGS交换。
    std::expected<void, HttpError> pump_initial(Timeouts tos);

private:
    socket_t         fd_      = kBadSocket;
    tls::SSL*        ssl_     = nullptr;
    nghttp2_session* session_ = nullptr;
    Endpoint         endpoint_;
    // 来自先前 `nghttp2_session_mem_send` 的待出站字节，
    // SSL_write 无法完全刷新（WANT_WRITE，部分返回）。
    // 我们*必须*在提取下一个块之前耗尽这些数据 - nghttp2 使下一个
    // mem_send 调用时的先前指针无效，并且 OpenSSL 在重试时
    // 引发 SSL_R_BAD_LENGTH，其长度相对于挂起的写入而言会缩小。
    // 保留为（ptr 进入 nghttp2-owned 缓冲区、偏移量、大小）。
    const uint8_t*   pend_buf_ = nullptr;
    size_t           pend_off_ = 0;
    size_t           pend_len_ = 0;
    // GOAWAY状态，当对端发出关闭意图信号时由on_frame_recv设置。
    // 保留在连接上（不是 per-request StreamCtx），
    // 因此 标志 在 run() 调用中仍然存在：池 的 is_alive
    // 门在每次重用时都会咨询它。
    bool             goaway_received_       = false;
    int32_t          goaway_last_stream_id_ = 0;
    // 重用标记——参见was_pooled()。默认 false（新鲜拨号）；
    // Pool::acquire 在发出连接之前将其翻转为 true。
    bool             was_pooled_            = false;
    long long        idle_ms_               = 0;
};

// -----------------------------------------------------------------------
// nghttp2 回调。  全静态；会话的 user_data 持有 StreamCtx*。
// 每个会话一次仅一个请求，因此单个指针就足够了（相对于stream_id
// →ctx映射）。
// -----------------------------------------------------------------------
static int on_frame_recv(nghttp2_session* /*s*/, const nghttp2_frame* frame,
                         void* user_data) {
    auto* sc = static_cast<StreamCtx*>(user_data);
    if (!sc) return 0;
    // GOAWAY是连接-scoped（传输上的stream_id =
    // = 0）-在下面的per-stream过滤器之前处理，
    // 否则早期返回会丢弃它。将 last_stream_id 转发到连接，
    // 以便 池 的 is_alive 门可以在下一次获取时拒绝此
    // 连接（否则我们将向调用者提供一个保证连接到 RST 任何
    // 新的 -submitted 数据流 的连接）。
    if (frame->hd.type == NGHTTP2_GOAWAY) {
        if (sc->conn) sc->conn->mark_goaway(frame->goaway.last_stream_id);
        return 0;
    }
    if (frame->hd.stream_id != sc->stream_id) return 0;
    if (frame->hd.type == NGHTTP2_HEADERS
        && frame->headers.cat == NGHTTP2_HCAT_RESPONSE) {
        if (sc->handler && sc->handler->on_headers && !sc->headers_delivered) {
            sc->handler->on_headers(sc->status, sc->headers);
            sc->headers_delivered = true;
        }
    }
    return 0;
}

static int on_data_chunk(nghttp2_session* s, uint8_t /*标志*/,
                         int32_t stream_id, const uint8_t* data, size_t len,
                         void* user_data) {
    auto* sc = static_cast<StreamCtx*>(user_data);
    if (!sc || stream_id != sc->stream_id) return 0;
    if (sc->handler) {
        // 流式传输模式：将块直接交给用户。  一个假的
        // return from on_chunk aborts the stream; we close it below so
        // nghttp2停止泵送数据。
        if (sc->handler->on_chunk) {
            // 交给调用者的非-empty块是语义提交点：
            // SSE内容(message_start/content_block)
            // 现在在状态更新函数手中并且重放将复制它。
            // 空块（不应该发生，但要防御）不要提交。
            if (len > 0) sc->data_delivered = true;
            if (!sc->handler->on_chunk(
                    std::string_view{reinterpret_cast<const char*>(data), len})) {
                sc->handler_aborted = true;
                nghttp2_submit_rst_stream(s, NGHTTP2_FLAG_NONE, stream_id,
                                          NGHTTP2_CANCEL);
                return 0;
            }
        }
    } else {
        // 缓冲（一元）响应正文上的Per-request上限——参见策略
        // Request::max_body_bytes和per-caller默认值。
        // 超出上限的失控上游为RST_STREAMed，
        // 调用返回带类型的 正文-too-large HttpError；
        // 我们确实 不 尝试恢复部分 正文 因为调用者
        // 要求有界响应。
        if (sc->buffered_body.size() + len > sc->max_body_bytes) {
            sc->error = "response body exceeded "
                      + std::to_string(sc->max_body_bytes) + " bytes";
            sc->handler_aborted = true;
            nghttp2_submit_rst_stream(s, NGHTTP2_FLAG_NONE, stream_id,
                                      NGHTTP2_CANCEL);
            return 0;
        }
        sc->buffered_body.append(reinterpret_cast<const char*>(data), len);
    }
    return 0;
}

static int on_stream_close(nghttp2_session* /*s*/, int32_t stream_id,
                           uint32_t error_code, void* user_data) {
    auto* sc = static_cast<StreamCtx*>(user_data);
    if (!sc || stream_id != sc->stream_id) return 0;
    sc->completed = true;
    if (error_code != NGHTTP2_NO_ERROR) {
        sc->reset = true;
        if (sc->error.empty())
            sc->error = "stream reset (" + std::to_string(error_code) + ")";
    }
    return 0;
}

static int on_header(nghttp2_session* /*s*/, const nghttp2_frame* frame,
                     const uint8_t* name, size_t nlen,
                     const uint8_t* value, size_t vlen,
                     uint8_t /*标志*/, void* user_data) {
    auto* sc = static_cast<StreamCtx*>(user_data);
    if (!sc || frame->hd.stream_id != sc->stream_id) return 0;
    std::string_view n{reinterpret_cast<const char*>(name),  nlen};
    std::string_view v{reinterpret_cast<const char*>(value), vlen};
    if (n == ":status") {
        sc->status = std::atoi(std::string{v}.c_str());
    } else {
        sc->headers.push_back({std::string{n}, std::string{v}});
    }
    return 0;
}

// POST 机构的数据提供者。  当 nghttp2 准备好发出字节时，
// 从这里提取字节；我们直接从 sctx->body 中读取，
// 并在结束时标记 EOF。
static ssize_t data_read_cb(nghttp2_session* /*s*/, int32_t /*stream_id*/,
                            uint8_t* buf, size_t length, uint32_t* data_flags,
                            nghttp2_data_source* source, void* /*user_data*/) {
    auto* sc = static_cast<StreamCtx*>(source->ptr);
    if (!sc || !sc->body) {
        *data_flags = NGHTTP2_DATA_FLAG_EOF;
        return 0;
    }
    const size_t remaining = sc->body->size() - sc->body_off;
    const size_t n = std::min(remaining, length);
    if (n) std::memcpy(buf, sc->body->data() + sc->body_off, n);
    sc->body_off += n;
    if (sc->body_off >= sc->body->size())
        *data_flags = NGHTTP2_DATA_FLAG_EOF;
    return static_cast<ssize_t>(n);
}

// -----------------------------------------------------------------------
// DNS+连接。  Non-blocking 连接，因此我们可以兑现连接 超时；
// poll()s 直到可写或截止日期。
// -----------------------------------------------------------------------
// 对已经-TCP-connected、非-blocking的fd进行SOCKS5协商。
// 告诉代理使用ATYP = DOMAIN打开到(dest_host，
// dest_port)的隧道，因此DNS发生proxy-side(这
// 是air-gapped盒子上的全部要点，其自己的DNS无法看到
// 公共互联网)。  成功返回后，调用者的 fd 会将不透明的 字节
// 数据流 携带到目的地 - TLS 像直接连接一样位于其之上。
//
// 连接-failure响应代码(REP)被映射到HttpError种类：
// 主机-unreachable/网络-unreachable/TTL-expired->解决；
// 连接-refused -> 连接；其他所有内容（
// 包括版本不匹配和禁止）-> Tls（配置错误的代理比
// 传输层 更接近信任失败）。
namespace {
std::expected<void, HttpError>
socks5_send_all(socket_t fd, const unsigned char* data, size_t len,
                clock_t_::time_point deadline, CancelToken* cancel) {
    while (len > 0) {
        if (cancel && cancel->is_cancelled())
            return std::unexpected(HttpError::cancelled("socks5 send"));
#if defined(_WIN32)
        int n = ::send(fd, reinterpret_cast<const char*>(data),
                       static_cast<int>(len), 0);
#else
        ssize_t n = ::send(fd, data, len, MSG_NOSIGNAL);
#endif
        if (n > 0) { data += n; len -= static_cast<size_t>(n); continue; }
        int e = sock_last_err();
        if (sock_intr(e)) continue;
        if (!sock_in_progress(e))
            return std::unexpected(HttpError::tls(
                "socks5 send: errno=" + std::to_string(e)));
        int rem = remaining_ms(deadline, 200);
        if (rem <= 0)
            return std::unexpected(HttpError::timeout("socks5 send timed out"));
        pollfd pfd{ fd, POLLOUT, 0 };
        int pr = sock_poll(&pfd, 1, rem);
        if (pr < 0 && !sock_intr(sock_last_err()))
            return std::unexpected(HttpError::tls(
                "socks5 poll: errno=" + std::to_string(sock_last_err())));
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
            return std::unexpected(HttpError::tls("socks5 send: poll hangup"));
    }
    return {};
}

std::expected<void, HttpError>
socks5_recv_exact(socket_t fd, unsigned char* out, size_t want,
                  clock_t_::time_point deadline, CancelToken* cancel) {
    while (want > 0) {
        if (cancel && cancel->is_cancelled())
            return std::unexpected(HttpError::cancelled("socks5 recv"));
#if defined(_WIN32)
        int n = ::recv(fd, reinterpret_cast<char*>(out),
                       static_cast<int>(want), 0);
#else
        ssize_t n = ::recv(fd, out, want, 0);
#endif
        if (n > 0) { out += n; want -= static_cast<size_t>(n); continue; }
        if (n == 0)
            return std::unexpected(HttpError::peer_closed(
                "socks5: proxy closed during handshake"));
        int e = sock_last_err();
        if (sock_intr(e)) continue;
        if (!sock_in_progress(e))
            return std::unexpected(HttpError::tls(
                "socks5 recv: errno=" + std::to_string(e)));
        int rem = remaining_ms(deadline, 200);
        if (rem <= 0)
            return std::unexpected(HttpError::timeout("socks5 recv timed out"));
        pollfd pfd{ fd, POLLIN, 0 };
        int pr = sock_poll(&pfd, 1, rem);
        if (pr < 0 && !sock_intr(sock_last_err()))
            return std::unexpected(HttpError::tls(
                "socks5 poll: errno=" + std::to_string(sock_last_err())));
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
            return std::unexpected(HttpError::tls("socks5 recv: poll hangup"));
    }
    return {};
}

std::expected<void, HttpError>
socks5_negotiate(socket_t fd, std::string_view dest_host, uint16_t dest_port,
                 clock_t_::time_point deadline, CancelToken* cancel) {
    if (dest_host.empty() || dest_host.size() > 255)
        return std::unexpected(HttpError::tls(
            "socks5: destination hostname empty or > 255 bytes"));

    // 1.问候语：ver=5，1方法，NO_AUTH。  'remote SOCKS' 那个
    //    `ssh -R <port>`公开只讲NO_AUTH(
    //    SSH会话本身已经是信任边界)，所以我们不费心去宣传
    //    USERPASS。
    {
        unsigned char greet[3] = { 0x05, 0x01, 0x00 };
        if (auto r = socks5_send_all(fd, greet, 3, deadline, cancel); !r)
            return r;
        unsigned char gresp[2];
        if (auto r = socks5_recv_exact(fd, gresp, 2, deadline, cancel); !r)
            return r;
        if (gresp[0] != 0x05 || gresp[1] != 0x00)
            return std::unexpected(HttpError::tls(
                "socks5: proxy refused NO_AUTH (resp "
                + std::to_string(gresp[0]) + "/" + std::to_string(gresp[1]) + ")"));
    }

    // 2.CONNECT请求具有ATYP=DOMAIN，因此DNS登陆代理-side。
    {
        std::vector<unsigned char> req;
        req.reserve(7 + dest_host.size());
        req.push_back(0x05);                                          // 版本
        req.push_back(0x01);                                          // CMD = CONNECT
        req.push_back(0x00);                                          // RSV
        req.push_back(0x03);                                          // ATYP = DOMAIN
        req.push_back(static_cast<unsigned char>(dest_host.size()));
        req.insert(req.end(), dest_host.begin(), dest_host.end());
        req.push_back(static_cast<unsigned char>(dest_port >> 8));
        req.push_back(static_cast<unsigned char>(dest_port & 0xff));
        if (auto r = socks5_send_all(fd, req.data(), req.size(), deadline, cancel); !r)
            return r;
    }

    // 3.CONNECT回复：4-byte头+变量BND.ADDR+2-byteBND.PORT。
    //    我们不关心绑定地址——我们只需要耗尽它，
    //    以便 fd 上的下一次读取从隧道 字节 数据流 开始。
    unsigned char head[4];
    if (auto r = socks5_recv_exact(fd, head, 4, deadline, cancel); !r)
        return r;
    if (head[0] != 0x05)
        return std::unexpected(HttpError::tls(
            "socks5: bad reply version " + std::to_string(head[0])));
    if (head[1] != 0x00) {
        const char* msg;
        switch (head[1]) {
            case 0x01: msg = "general SOCKS server failure"; break;
            case 0x02: msg = "connection not allowed by ruleset"; break;
            case 0x03: msg = "network unreachable"; break;
            case 0x04: msg = "host unreachable"; break;
            case 0x05: msg = "connection refused"; break;
            case 0x06: msg = "TTL expired"; break;
            case 0x07: msg = "command not supported"; break;
            case 0x08: msg = "address type not supported"; break;
            default:   msg = "unknown reply code";
        }
        std::string detail = "socks5 CONNECT to ";
        detail.append(dest_host).append(":").append(std::to_string(dest_port))
              .append(" rejected: ").append(msg)
              .append(" (REP=").append(std::to_string(head[1])).append(")");
        // 将网络-shape故障映射回等效的传输层 HttpErrorKinds，
        // 以便重试/报告代码路径保持统一。
        if (head[1] == 0x03 || head[1] == 0x04 || head[1] == 0x06)
            return std::unexpected(HttpError::resolve(std::move(detail)));
        if (head[1] == 0x05)
            return std::unexpected(HttpError::connect(std::move(detail)));
        return std::unexpected(HttpError::tls(std::move(detail)));
    }

    int bnd_addr_len;
    switch (head[3]) {
        case 0x01: bnd_addr_len = 4;  break; // IPv4
        case 0x04: bnd_addr_len = 16; break; // IPv6
        case 0x03: {                         // DOMAIN:1个长度字节然后八位位组
            unsigned char l;
            if (auto r = socks5_recv_exact(fd, &l, 1, deadline, cancel); !r)
                return r;
            bnd_addr_len = l;
            break;
        }
        default:
            return std::unexpected(HttpError::tls(
                "socks5: bad reply ATYP " + std::to_string(head[3])));
    }
    std::vector<unsigned char> drop(static_cast<size_t>(bnd_addr_len) + 2);
    if (auto r = socks5_recv_exact(fd, drop.data(), drop.size(), deadline, cancel); !r)
        return r;
    return {};
}

// SSRF 守卫：这个 RESOLVED 地址是一个不可信任的（型号 -supplied）
// URL 绝对不能到达的地址吗？涵盖环回、RFC1918 专用、链接-local（包括
// 169.254.169.254 cloud-metadata 端点）、CGNAT 和
// v6 等效项（包括 IPv4-mapped ::ffff:a.b.c.d，
// 其中 re-checked 作为其 v4 地址，因此 ::ffff:127.0.0.1无法
// 通过 v6 臂偷偷环回）。它运行 之后 getaddrinfo()，在我们即将
// connect() 的确切 sockaddr 上，因此 DNS-rebinds 到内部 IP 的
// 公共主机名在套接字处被捕获，其中目标是权威的。仅当设置 Endpoint::ssrf_guard
// 时才查阅（网络工具）；提供商 + local-model 流量永远不会受到限制。
[[nodiscard]] bool blocked_sockaddr(const sockaddr* sa) noexcept {
    auto v4_blocked = [](uint32_t h /*主机订购*/) noexcept {
        const uint8_t a = (h >> 24) & 0xFF, b = (h >> 16) & 0xFF;
        if (a == 0)   return true;                       // 0.0.0.0/8 "this host"
        if (a == 127) return true;                       // 环回
        if (a == 10)  return true;                       // RFC1918
        if (a == 172 && (b & 0xF0) == 16) return true;    // 172.16/12
        if (a == 192 && b == 168) return true;            // 192.168/16
        if (a == 169 && b == 254) return true;            // 链接-local + 元数据
        if (a == 100 && (b & 0xC0) == 64) return true;    // 100.64/10 CGNAT
        if (a >= 224) return true;                        // 多播/保留
        return false;
    };
    if (!sa) return true;                                 // unknown = refuse
    if (sa->sa_family == AF_INET) {
        const auto* s = reinterpret_cast<const sockaddr_in*>(sa);
        return v4_blocked(ntohl(s->sin_addr.s_addr));
    }
    if (sa->sa_family == AF_INET6) {
        const auto* s = reinterpret_cast<const sockaddr_in6*>(sa);
        const uint8_t* b = s->sin6_addr.s6_addr;
        // IPv4-mapped::ffff:a.b.c.d — re-check 作为 v4 地址。
        static const uint8_t mapped[12] =
            {0,0,0,0, 0,0,0,0, 0,0,0xFF,0xFF};
        if (std::memcmp(b, mapped, 12) == 0) {
            uint32_t h = (uint32_t(b[12]) << 24) | (uint32_t(b[13]) << 16)
                       | (uint32_t(b[14]) << 8) | uint32_t(b[15]);
            return v4_blocked(h);
        }
        if (b[0] == 0xFE && (b[1] & 0xC0) == 0x80) return true;  // fe80::/10 链接-local
        if ((b[0] & 0xFE) == 0xFC) return true;                  // fc00::/7 ULA
        if (b[0] == 0xFF) return true;                           // ff00::/8多播
        // ::1环回和::未指定。
        bool all_zero = true;
        for (int i = 0; i < 15; ++i) if (b[i]) { all_zero = false; break; }
        if (all_zero && (b[15] == 0 || b[15] == 1)) return true;
        return false;
    }
    return true;   // non-IP 系列 (AF_UNIX 等) — 绝不用于网络拨号
}
} // 命名空间结束。

std::expected<socket_t, HttpError>
dial_tcp(const Endpoint& ep, Timeouts tos, CancelToken* cancel) {
    ensure_net_init();
    // SOCKS5 Short-circuit：拨打代理地址(不是上游)，
    // 然后协商到上游的tcp_host()/tcp_port()的隧道。
    // 我们返回的fd看起来像是到上游的直接连接，为TLS做好准备——下面
    // 的wrap_client一如既往地使用ep.host作为SNI。
    const auto& sx = agentty_socks_proxy();

    addrinfo hints{}; hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    const std::string conn_host =
        sx.active() ? sx.host : std::string{ep.tcp_host()};
    const uint16_t    conn_port = sx.active() ? sx.port : ep.tcp_port();
    char port_buf[12]; std::snprintf(port_buf, sizeof(port_buf), "%u", conn_port);
    int gai = ::getaddrinfo(conn_host.c_str(), port_buf, &hints, &res);
    if (gai != 0 || !res) {
        return std::unexpected(HttpError::resolve(
            std::string{"getaddrinfo: "} + gai_strerror(gai)));
    }

    // getaddrinfo() 是一个没有 取消 钩子的阻塞
    // 解析——如果当我们在其中时关闭触发了令牌（e.g。
    // 预热拨号与快速管道-EOF退出），现在中止而不是继续进入连接循环。
    // 防止 join_prewarm() 等待无人想要的拨号。
    if (cancel && cancel->is_cancelled()) {
        ::freeaddrinfo(res);
        return std::unexpected(HttpError::cancelled("cancelled after resolve"));
    }

    auto deadline = clock_t_::now() + tos.connect;
    if (tos.absolute_deadline && *tos.absolute_deadline < deadline)
        deadline = *tos.absolute_deadline;

    // TCP连接成功后，如果配置了代理，则完成拨号：
    // SOCKS5协商（因此返回的fd是到ep的
    // 逻辑上游的隧道）；否则通过-through。
    auto finalize = [&](socket_t fd) -> std::expected<socket_t, HttpError> {
        if (!sx.active()) return fd;
        if (auto r = socks5_negotiate(fd, ep.tcp_host(), ep.tcp_port(),
                                      deadline, cancel); !r) {
            sock_close(fd);
            return std::unexpected(std::move(r).error());
        }
        return fd;
    };

    // SSRF防护通过SOCKS代理是没有意义的：我们拨打(受信任的，
    // 用户-configured)代理，它远程解析上游，
    // 因此`res`中的地址是PROXY的，而不是目标的。
    // 仅在直接拨号上应用已解析的-IP 阻止列表。
    const bool ssrf_check = ep.ssrf_guard && !sx.active();

    std::string last_err;
    for (addrinfo* p = res; p; p = p->ai_next) {
        // Post-resolution SSRF门：
        // 拒绝将型号-supplied主机名解析为的internal/loopback/link-local/元数据地址
        // （DNS重新绑定）。在这里检查，在我们 connect() 的确切 sockaddr 上。
        if (ssrf_check && blocked_sockaddr(p->ai_addr)) {
            last_err = "blocked address (SSRF guard)";
            continue;
        }
        socket_t fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd == kBadSocket) { last_err = "socket() failed"; continue; }
#if !defined(_WIN32)
        // 降低Nagle+保持数据流响应。  Anthropic
        // 的SSE 帧小而突发；我们希望它们立即刷新到用户空间。
        int one = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        ::setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
#  if defined(__linux__) && defined(TCP_QUICKACK)
        // 禁用延迟ACK。仅限 Linux — 默认情况下，
        // 内核会合并 ACK 以摊销唤醒，这会在接收器未发回数据
        // 时为每个 SSE 帧 轮 -trip 增加 40 毫秒。
        // 对于pure-receive 数据流(即SSE)，
        // QUICKACK在每个段上激发ACK，提示发送方立即
        // 推送下一个窗口段。 副作用：突发增量上约 10-40
        // 毫秒更紧的令牌节奏。
        ::setsockopt(fd, IPPROTO_TCP, TCP_QUICKACK, &one, sizeof(one));
#  endif
#else
        BOOL one = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
                     reinterpret_cast<const char*>(&one), sizeof(one));
        ::setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE,
                     reinterpret_cast<const char*>(&one), sizeof(one));
        // Windows默认为64位KiB接收缓冲区;对于 Anthropic
        // 的 SSE 突发来说，这是很拥挤的，其中单个 content_block_delta
        // 块可以是 8-32 KiB，并且限制接收端的 TCP 窗口迫使
        // 发送方暂停以等待 round-trip ACKs mid-frame。
        // 1 MiB 缓冲区 让内核吸收 10+ back-to-back 段，
        // 而不会造成 flow-control 停顿；可在墙壁-clock
        // TTFT 上测量较长的完工。发送 缓冲区 与请求 正文
        // 类似（bash 工具输出，大编辑 载荷 返回到 API）。
        int rcvbuf = 1 << 20;            // 1MiB
        int sndbuf = 256 << 10;          // 256KiB
        ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF,
                     reinterpret_cast<const char*>(&rcvbuf), sizeof(rcvbuf));
        ::setsockopt(fd, SOL_SOCKET, SO_SNDBUF,
                     reinterpret_cast<const char*>(&sndbuf), sizeof(sndbuf));
#endif
        sock_set_nonblock(fd);
        int r = ::connect(fd, p->ai_addr, static_cast<int>(p->ai_addrlen));
        if (r == 0) { ::freeaddrinfo(res); return finalize(fd); }
        int e = sock_last_err();
        if (!sock_in_progress(e)) {
            sock_close(fd);
            last_err = "connect() errno=" + std::to_string(e);
            continue;
        }
        // 等待直到可写或截止日期。  切片很短，因此 取消 落地速度很快。
        while (true) {
            if (cancel && cancel->is_cancelled()) {
                sock_close(fd); ::freeaddrinfo(res);
                return std::unexpected(HttpError::cancelled("during connect"));
            }
            int rem = remaining_ms(deadline, 200);
            if (rem <= 0) {
                sock_close(fd);
                last_err = "connect: timed out";
                break;
            }
            pollfd pfd{ fd, POLLOUT, 0 };
            int pr = sock_poll(&pfd, 1, rem);
            if (pr < 0) {
                if (sock_intr(sock_last_err())) continue;
                sock_close(fd);
                last_err = "poll: errno=" + std::to_string(sock_last_err());
                break;
            }
            if (pr == 0) continue;
            if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                sock_close(fd);
                last_err = "connect: poll hangup";
                break;
            }
            int soerr = 0;
#if defined(_WIN32)
            int sl = sizeof(soerr);
            ::getsockopt(fd, SOL_SOCKET, SO_ERROR,
                         reinterpret_cast<char*>(&soerr), &sl);
#else
            socklen_t sl = sizeof(soerr);
            ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl);
#endif
            if (soerr != 0) {
                sock_close(fd);
                last_err = "connect: SO_ERROR=" + std::to_string(soerr);
                break;
            }
            ::freeaddrinfo(res);
            return finalize(fd);
        }
    }
    ::freeaddrinfo(res);
    return std::unexpected(HttpError::connect(
        last_err.empty() ? std::string{"no address worked"} : last_err));
}

// -----------------------------------------------------------------------
// TLS握手循环。  SSL 已附加到 fd；我们驱动 SSL_connect 并
// 将 WANT_READ/WANT_WRITE 转换为 poll() 等待，
// 遵守相同的连接截止日期。
// -----------------------------------------------------------------------
std::expected<void, HttpError>
tls_handshake(socket_t fd, tls::SSL* ssl, Timeouts tos, CancelToken* cancel) {
    auto deadline = clock_t_::now() + tos.connect;
    if (tos.absolute_deadline && *tos.absolute_deadline < deadline)
        deadline = *tos.absolute_deadline;
    while (true) {
        int r = SSL_connect(ssl);
        if (r == 1) return {};
        int e = SSL_get_error(ssl, r);
        if (e != SSL_ERROR_WANT_READ && e != SSL_ERROR_WANT_WRITE)
            return std::unexpected(HttpError::tls(tls::last_error(ssl)));
        if (cancel && cancel->is_cancelled())
            return std::unexpected(HttpError::cancelled("during tls handshake"));
        int rem = remaining_ms(deadline, 200);
        if (rem <= 0) return std::unexpected(HttpError::timeout("tls handshake timed out"));
        pollfd pfd{ fd, (e == SSL_ERROR_WANT_READ) ? (short)POLLIN : (short)POLLOUT, 0 };
        int pr = sock_poll(&pfd, 1, rem);
        if (pr < 0) {
            if (sock_intr(sock_last_err())) continue;
            return std::unexpected(HttpError::tls(
                "poll errno=" + std::to_string(sock_last_err())));
        }
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
            return std::unexpected(HttpError::tls("poll hangup"));
    }
}

// 泵输出帧:将字节从nghttp2拉开并写入TLS。
// 如果 TLS 无法写入整个 mem_send() 块，则返回 WANT_*。
//
// 不变量：连接上的 `pend_*` 保留前一个 调用 未刷新的任何 字节。
// 我们必须在从 nghttp2 拉出新块之前耗尽这些数据 —
// mem_send 会使下一个 调用 上的先前指针无效并推进内部状态，
// 因此部分 -written 帧 将丢失（一旦 待处理-write
// 长度在其下发生变化，OpenSSL 将拒绝带有
// SSL_R_BAD_LENGTH 的 重试）。
enum class PumpOut { Idle, WantRead, WantWrite, Error };
static PumpOut pump_send_impl(tls::SSL* ssl, nghttp2_session* session,
                              const uint8_t*& pend_buf, size_t& pend_off,
                              size_t& pend_len, std::string* err) {
    auto drain_pending = [&]() -> PumpOut {
        while (pend_off < pend_len) {
            int chunk = static_cast<int>(pend_len - pend_off);
            int r = SSL_write(ssl, pend_buf + pend_off, chunk);
            if (r > 0) { pend_off += static_cast<size_t>(r); continue; }
            int e = SSL_get_error(ssl, r);
            if (e == SSL_ERROR_WANT_WRITE) return PumpOut::WantWrite;
            if (e == SSL_ERROR_WANT_READ)  return PumpOut::WantRead;
            if (err) *err = "SSL_write: " + tls::last_error(ssl);
            return PumpOut::Error;
        }
        pend_buf = nullptr;
        pend_off = 0;
        pend_len = 0;
        return PumpOut::Idle;
    };

    // 首先，刷新上次 nghttp2 交给我们的任何内容。
    if (pend_buf && pend_off < pend_len) {
        auto s = drain_pending();
        if (s != PumpOut::Idle) return s;
    }

    // 拉出并刷新连续的块，直到nghttp2没有更多块或TLS块。
    while (true) {
        const uint8_t* data = nullptr;
        ssize_t n = nghttp2_session_mem_send(session, &data);
        if (n < 0) {
            if (err) *err = std::string{"nghttp2_session_mem_send: "}
                          + nghttp2_strerror(static_cast<int>(n));
            return PumpOut::Error;
        }
        if (n == 0) return PumpOut::Idle;
        pend_buf = data;
        pend_off = 0;
        pend_len = static_cast<size_t>(n);
        auto s = drain_pending();
        if (s != PumpOut::Idle) return s;
    }
}

// 泵输入字节:从TLS读取，馈送到nghttp2。  当 TLS 没有更多缓冲时，
// 返回 WANT_*。 `bytes_in` 累积成功传递到 nghttp2
// 的明文 字节 的总数 - 调用者使用它来刷新空闲-timeout 时钟，
// 因此*任何*入站活动（DATA、PING ACK、
// WINDOW_UPDATE）都算作活跃度。
enum class PumpIn { Idle, WantRead, WantWrite, Closed, Error };
static PumpIn pump_recv(tls::SSL* ssl, nghttp2_session* session,
                        std::string* err, size_t* bytes_in) {
    // 64个KiB读取块——在单个SSL_read+
    // nghttp2_session_mem_recv对中耗尽典型的SSE
    // 突发(跨3-4个TCP段的10-30个KiB文本增量)，
    // 而不是使poll/read循环循环3-4次。 保存 在突发 流式传输 上约
    // 为 2 毫秒/突发，并且在空闲连接上不产生任何费用（当没有数据可用时，
    // SSL_read 立即返回）。 Stack-allocated，没有流失。
    uint8_t buf[64 * 1024];
    while (true) {
        int r = SSL_read(ssl, buf, sizeof(buf));
        if (r > 0) {
            if (bytes_in) *bytes_in += static_cast<size_t>(r);
            ssize_t rv = nghttp2_session_mem_recv(session, buf, static_cast<size_t>(r));
            if (rv < 0) {
                if (err) *err = std::string{"nghttp2_session_mem_recv: "}
                              + nghttp2_strerror(static_cast<int>(rv));
                return PumpIn::Error;
            }
            continue;
        }
        int e = SSL_get_error(ssl, r);
        if (e == SSL_ERROR_WANT_READ)   return PumpIn::WantRead;
        if (e == SSL_ERROR_WANT_WRITE)  return PumpIn::WantWrite;
        if (e == SSL_ERROR_ZERO_RETURN) return PumpIn::Closed;
        if (err) *err = "SSL_read: " + tls::last_error(ssl);
        return PumpIn::Error;
    }
}

std::expected<void, HttpError>
Connection::pump_initial(Timeouts tos) {
    // 驱动第一个 SETTINGS/window-update 交换，
    // 以便在我们从 dial_* 返回连接之前该连接完全可用。
    // 以传入的连接截止日期为限。
    auto deadline = clock_t_::now() + tos.connect;
    if (tos.absolute_deadline && *tos.absolute_deadline < deadline)
        deadline = *tos.absolute_deadline;
    std::string err;
    while (nghttp2_session_want_write(session_)) {
        auto s = pump_send_impl(ssl_, session_, pend_buf_, pend_off_,
                                pend_len_, &err);
        if (s == PumpOut::Error) return std::unexpected(HttpError::protocol(err));
        if (s == PumpOut::Idle) break;
        int rem = remaining_ms(deadline, 200);
        if (rem <= 0) return std::unexpected(HttpError::timeout("h2 initial send timed out"));
        pollfd pfd{ fd_, (s == PumpOut::WantRead) ? (short)POLLIN : (short)POLLOUT, 0 };
        int pr = sock_poll(&pfd, 1, rem);
        if (pr < 0 && !sock_intr(sock_last_err()))
            return std::unexpected(HttpError::protocol(
                "h2 poll errno=" + std::to_string(sock_last_err())));
    }
    return {};
}

std::expected<void, HttpError>
Connection::run(const Request& req, StreamCtx& sctx, Timeouts tos,
                CancelTokenPtr cancel) {
    // ---建立头文件列表。  HTTP/2. 中的所有名称都必须小写 --- 按照惯例，:
    // method、:scheme、:authority、:path 首先出现。
    std::string authority = endpoint_.host;
    if (endpoint_.port != 443) {
        authority += ':';
        authority += std::to_string(endpoint_.port);
    }
    std::vector<nghttp2_nv> nvs;
    nvs.reserve(req.headers.size() + 4);
    auto make_nv = [](std::string_view k, std::string_view v) {
        // 值通过指针引用；调用者确保字符串比提交的 调用
        // 更长寿。
        return nghttp2_nv{
            const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(k.data())),
            const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(v.data())),
            k.size(), v.size(), NGHTTP2_NV_FLAG_NONE,
        };
    };
    nvs.push_back(make_nv(":method",    wire_name(req.method)));
    nvs.push_back(make_nv(":scheme",    "https"));
    nvs.push_back(make_nv(":authority", authority));
    nvs.push_back(make_nv(":path",      req.path));
    for (const auto& h : req.headers) nvs.push_back(make_nv(h.name, h.value));

    // ---提交---
    sctx.body    = req.body.empty() ? nullptr : &req.body;
    sctx.body_off = 0;
    nghttp2_data_provider dp{};
    dp.source.ptr   = &sctx;
    dp.read_callback = data_read_cb;
    nghttp2_data_provider* dpp = req.body.empty() ? nullptr : &dp;

    // 重新绑定user_data，以便静态回调找到该请求的ctx。
    // 还要在后面标记-pointer，以便on_frame_recv
    // 可以将连接-scoped 事件（当前是GOAWAY）转发到连接上。
    sctx.conn = this;
    sctx.max_body_bytes = req.max_body_bytes;
    nghttp2_session_set_user_data(session_, &sctx);

    int32_t sid = nghttp2_submit_request(session_, nullptr,
                                         nvs.data(), nvs.size(), dpp, &sctx);
    if (sid < 0)
        return std::unexpected(HttpError::protocol(
            std::string{"nghttp2_submit_request: "} + nghttp2_strerror(sid)));
    sctx.stream_id = sid;

    // --- I/O 循环 ---
    //
    // 请求正文是单个并发select-pump：每次迭代刷新待处理出站帧，
    // 排出入站帧，然后在poll()中等待下一个就绪边缘。它确实是一个循环，
    // 而不是一个线性状态序列——
    // send/recv/PING/idle-clock/cancel在每次迭代中都处于活动状态
    // ——所以它仍然是一个循环。 WAS 分散在两个枚举 (PumpOut/PumpIn)
    // 和 ad-hoc 布尔值中的内容被折叠成 一个 显式的 per
    // -iteration 结果 `PumpStep`，由下面的 step_io
    // () 评估。循环分派该单个值，而不是来自五个单独的 标志 的
    // 重新推导当前是否完成、阻塞或已经终止，
    // 因此每个退出原因都会被命名并总计（开关是详尽的）。
    enum class PumpStep {
        Progress,    // 取得了进展（或仅仅会-block）；继续前进
        StreamDone,  // 数据流干净地关闭(sctx.completed)-解决
        PeerClosed,  // 对端 在 -stream 中期关闭了 TLS 会话 — 出错
        WantWrite,   // TLS无法刷新所有出站字节；添加POLLOUT
    };

    std::optional<clock_t_::time_point> deadline = tos.absolute_deadline;
    if (!deadline && tos.total.count() > 0)
        deadline = clock_t_::now() + tos.total;

    // 活跃时钟。每当 SSL_read 返回纯文本 字节 时，`last_rx`
    // 就会被刷新（DATA、PING ACK、WINDOW_UPDATE — 任何
    // 帧 都被视为连接处于活动状态的证明）。 `last_ping` 限制我们
    // 探测静默 对端 的频率。
    const auto start_at = clock_t_::now();
    auto last_rx   = start_at;
    auto last_ping = start_at;
    auto last_activity_report = start_at - std::chrono::seconds{1};
    auto last_buffered_report = start_at;

    std::string err;

    // 一次泵迭代：冲出出站、排出入站、分类。在硬协议失败时返回 带
    // 类型的 PumpStep 或 带类型的 HttpError。
    // 当入站 字节 到达时刷新 last_rx。纯 传输层 — poll()
    // /liveness/cancel 簿记保留在下面的循环中。
    auto step_io = [&]() -> std::expected<PumpStep, HttpError> {
        auto out = pump_send_impl(ssl_, session_, pend_buf_, pend_off_,
                                  pend_len_, &err);
        if (out == PumpOut::Error) return std::unexpected(HttpError::protocol(err));
        size_t bytes_in = 0;
        auto in = pump_recv(ssl_, session_, &err, &bytes_in);
        if (in == PumpIn::Error)   return std::unexpected(HttpError::protocol(err));
        if (bytes_in > 0) {
            last_rx = clock_t_::now();
            if (sctx.handler && sctx.handler->on_activity
                && last_rx - last_activity_report >= std::chrono::seconds{1}) {
                last_activity_report = last_rx;
                sctx.handler->on_activity();
            }
            if (sctx.handler && sctx.handler->on_buffered_wait
                && !sctx.data_delivered
                && last_rx - start_at >= std::chrono::seconds{30}
                && last_rx - last_buffered_report >= std::chrono::seconds{15}) {
                last_buffered_report = last_rx;
                sctx.handler->on_buffered_wait();
            }
        }
        if (in == PumpIn::Closed) {
            // 对端结束了TLS会话。  如果 数据流 也关闭了，
            // 我们就完成了；否则表面上会出现错误。
            return sctx.completed ? PumpStep::StreamDone : PumpStep::PeerClosed;
        }
        if (sctx.completed) return PumpStep::StreamDone;
        if (out == PumpOut::WantWrite) return PumpStep::WantWrite;
        return PumpStep::Progress;
    };

    while (!sctx.completed) {
        if (cancel && cancel->is_cancelled()) {
            nghttp2_submit_rst_stream(session_, NGHTTP2_FLAG_NONE, sid,
                                      NGHTTP2_CANCEL);
            sctx.error = "cancelled";
        }

        // 如果我们已经沉默足够长的时间，则用 PING 戳 对端。
        // ACK（或死套接字上的 SSL_write 故障）告诉我们连接
        // 是否实际上处于活动状态。不透明数据为nghttp2-supplied。
        if (tos.ping.count() > 0) {
            auto since_rx = clock_t_::now() - last_rx;
            auto since_ping = clock_t_::now() - last_ping;
            if (since_rx >= tos.ping && since_ping >= tos.ping) {
                nghttp2_submit_ping(session_, NGHTTP2_FLAG_NONE, nullptr);
                last_ping = clock_t_::now();
            }
        }

        auto step = step_io();
        if (!step) return std::unexpected(std::move(step).error());
        bool want_write_block = false;
        switch (*step) {
            case PumpStep::StreamDone:
                break;                       // 循环保护/post-loop 解决
            case PumpStep::PeerClosed:
                return std::unexpected(HttpError::peer_closed(
                    "connection closed by peer mid-stream"));
            case PumpStep::WantWrite:
                want_write_block = true;
                break;
            case PumpStep::Progress:
                break;
        }

        if (sctx.completed) break;

        // *在*排空本次迭代到达的任何内容之后强制执行
        // 空闲护栏——当我们处理其 字节 时，一个爱说话的
        // 对端 不应该被超过截止日期的时钟杀死。
        if (tos.idle.count() > 0) {
            auto since_rx = clock_t_::now() - last_rx;
            if (since_rx >= tos.idle) {
                nghttp2_submit_rst_stream(session_, NGHTTP2_FLAG_NONE, sid,
                                          NGHTTP2_CANCEL);
                auto secs = std::chrono::duration_cast<std::chrono::seconds>(since_rx).count();
                return std::unexpected(HttpError::timeout(
                    "h2 idle timeout (no bytes for "
                    + std::to_string(secs) + "s)"));
            }
        }

        // 决定轮询掩码。  总是想要 POLLIN，这样我们就可以及时捕获不请自来的
        // 帧（WINDOW_UPDATE、PING）。  仅当 nghttp2 有 字节
        // 准备发送，或者 TLS 在本次迭代 (PumpStep::WantWrite) 中
        // 留下了出站 帧 half-flushed 时，才添加 POLLOUT。
        short mask = POLLIN;
        if (nghttp2_session_want_write(session_) || want_write_block)
            mask |= POLLOUT;
        if (!nghttp2_session_want_read(session_)
            && !nghttp2_session_want_write(session_))
            break;

        // 限制投票等待，以便 取消 代币快速落地，因此我们遵守设定的总体截止日期，
        // 因此我们按计划重新 -check 和 idle/ping 时钟。
        // 200 毫秒匹配 Zed 的 取消-latency 配置文件；
        // 我们进一步限制了最近的活跃期限，因此 90 秒的空闲防护实际上在
        // t=90 秒时触发，而不是 t=90 s+200 ms*N。
        int rem = remaining_ms(deadline, 200);
        if (rem == 0 && deadline) {
            nghttp2_submit_rst_stream(session_, NGHTTP2_FLAG_NONE, sid,
                                      NGHTTP2_CANCEL);
            return std::unexpected(HttpError::timeout("request timed out"));
        }
        auto clamp_to = [&](std::chrono::milliseconds budget,
                            clock_t_::time_point marker) {
            if (budget.count() <= 0) return;
            auto now = clock_t_::now();
            auto until = marker + budget;
            if (until <= now) { rem = 0; return; }
            auto left = std::chrono::duration_cast<ms_t>(until - now).count();
            if (left < rem) rem = static_cast<int>(left);
        };
        clamp_to(tos.idle, last_rx);
        clamp_to(tos.ping, last_rx);
        if (rem < 1) rem = 1;  // 总是给民意调查一些可以咀嚼的东西
        pollfd pfd{ fd_, mask, 0 };
        int pr = sock_poll(&pfd, 1, rem);
        if (pr < 0) {
            if (sock_intr(sock_last_err())) continue;
            return std::unexpected(HttpError::protocol(
                "h2 poll errno=" + std::to_string(sock_last_err())));
        }
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            return std::unexpected(HttpError::socket_hangup("h2 socket hangup"));
        }
    }

    // 清除 ctx 指针，以便 数据流 关闭后的后期 对端 帧（不太
    // 可能）的 回调 不会触及死的 StreamCtx。
    nghttp2_session_set_user_data(session_, nullptr);

    if (sctx.handler_aborted) {
        if (sctx.error == "cancelled")
            return std::unexpected(HttpError::cancelled("stream aborted by caller"));
        return std::unexpected(HttpError::cancelled(
            sctx.error.empty() ? std::string{"stream aborted by caller"} : sctx.error));
    }
    if (sctx.reset) {
        if (sctx.error == "cancelled")
            return std::unexpected(HttpError::cancelled("stream reset"));
        return std::unexpected(HttpError::protocol(
            sctx.error.empty() ? std::string{"stream reset"} : sctx.error));
    }
    return {};
}

// =======================================================================
// 连接-lifecycle型状态机。
// =======================================================================
// fresh-dial路径是严格线性的生命周期：
//
//     拨号──连接──▶ TcpConnected ──tls──▶ TlsUp ──h2──▶ H2Ready
//
// 每个箭头都是唯一合法的前进方式，并且每个状态都是 OWNS 和
// 传输层 处理迄今为止获取的 move-only *能力令牌*（套接字 fd，
// 然后是 SSL*，然后是 nghttp2 会话）。拥有状态的析构函数释放它
// 仍然保留的任何内容，因此 任意 步骤上的早期
// `std::unexpected` 返回会自动解除部分-built
// 连接 - 没有 per-error-path `sock_close`/
// `free_ssl` 梯子，并且如果将来的编辑在步骤之间添加返回，则不会泄漏。
//
// 转换函数按值(sink)消耗先前的状态并返回
// `fsm::Result<NextState, HttpError>`。
// 它们以 `fsm::assert_legal_edge<From,To>()` 开始，
// 一个保守的守卫：调用机器未声明的转换（e.g。跳过 TLS）失败到 COMPILE。
// 边通过下面的 AGENTTY_FSM_EDGE 声明。
//
// 这是 syscalls/library 调用 的 相同 序列，
// 作为旧的直 -line dial_new —正文 被逐字移动到过渡中。
// 这些类型添加了 编译-time 排序保证和异常 -free
// RAII 清理，零 运行时 成本（空 move-only 基数，
// 无 vtable，无堆）。
// -----------------------------------------------------------------------
namespace {

// 转发声明，以便每个州可以在 `fsm_to` 中命名其继任者。
struct Dialing;
struct TcpConnected;
struct TlsUp;
struct H2Ready;

// 状态0-输入已收集，尚未获取任何内容。
struct Dialing : io::fsm::State<struct DialingTag> {
    using fsm_to = io::fsm::to<TcpConnected>;
    const Endpoint& ep;
    Timeouts        tos;
    CancelToken*    cancel;   // 借用； 生命周期 归调用者所有
};

// 状态1-TCP(和SOCKS5隧道，如果配置的话)建立。 OWNS FD。
struct TcpConnected : io::fsm::State<struct TcpConnectedTag> {
    using fsm_to = io::fsm::to<TlsUp>;
    socket_t        fd = kBadSocket;
    const Endpoint& ep;
    Timeouts        tos;
    CancelToken*    cancel;

    TcpConnected(socket_t f, const Endpoint& e, Timeouts t, CancelToken* c)
        : fd(f), ep(e), tos(t), cancel(c) {}
    TcpConnected(TcpConnected&& o) noexcept
        : fd(std::exchange(o.fd, kBadSocket)),
          ep(o.ep), tos(o.tos), cancel(o.cancel) {}
    TcpConnected& operator=(TcpConnected&&) = delete;
    ~TcpConnected() { if (fd != kBadSocket) sock_close(fd); }

    [[nodiscard]] socket_t release_fd() noexcept {
        return std::exchange(fd, kBadSocket);
    }
};

// 状态2-TLS握手完成AND ALPN-confirmed h2。 OWNS fd + ssl。
struct TlsUp : io::fsm::State<struct TlsUpTag> {
    using fsm_to = io::fsm::to<H2Ready>;
    socket_t        fd  = kBadSocket;
    tls::SSL*       ssl = nullptr;
    const Endpoint& ep;
    Timeouts        tos;

    TlsUp(socket_t f, tls::SSL* s, const Endpoint& e, Timeouts t)
        : fd(f), ssl(s), ep(e), tos(t) {}
    TlsUp(TlsUp&& o) noexcept
        : fd(std::exchange(o.fd, kBadSocket)),
          ssl(std::exchange(o.ssl, nullptr)),
          ep(o.ep), tos(o.tos) {}
    TlsUp& operator=(TlsUp&&) = delete;
    ~TlsUp() {
        if (ssl) tls::free_ssl(ssl);
        if (fd != kBadSocket) sock_close(fd);
    }
};

// 状态3-终端:完全-built、SETTINGS-exchanged连接。
// nghttp2 会话、ssl 和 fd 已全部交给 Connection，
// 现在由 Connection 拥有它们。该令牌只是将其带出管道。
// 无 `fsm_to` — H2Ready 是 终端 状态，没有边离开它。
struct H2Ready : io::fsm::State<struct H2ReadyTag> {
    std::unique_ptr<Connection> conn;
};

// 拨号─连接─▶ TcpConnected。  TCP 连接 + 可选 SOCKS5 隧道。
io::fsm::Result<TcpConnected, HttpError> connect_tcp(Dialing&& s) {
    io::fsm::assert_legal_edge<Dialing, TcpConnected>();
    auto fd_or = dial_tcp(s.ep, s.tos, s.cancel);
    if (!fd_or) return std::unexpected(std::move(fd_or).error());
    return TcpConnected{ *fd_or, s.ep, s.tos, s.cancel };
}

// TcpConnected─tls─▶ TlsUp。  TLS握手，则要求ALPN=h2。  发生任何故障时，
// 返回的 `unexpected` 都会传播，并且 ~TcpConnected 会关闭 fd。
io::fsm::Result<TlsUp, HttpError> negotiate_tls(TcpConnected&& s) {
    io::fsm::assert_legal_edge<TcpConnected, TlsUp>();

    // SOCKET在Win64上是64-bit句柄，但是OpenSSL的
    // BIO_new_socket采用int。内核只给出很小的值（远低于
    // 2^31），因此截断是安全的——明确地沉默 C4244。
    tls::SSL* ssl = tls::wrap_client(static_cast<int>(s.fd), s.ep.host);
    if (!ssl) return std::unexpected(HttpError::tls("SSL_new failed"));

    // 将fd+ssl交给下一个状态令牌NOW，使得下面的任何错误通过~
    // TlsUp（释放两者）展开并且~TcpConnected变成no-op。
    const Endpoint& ep = s.ep;
    Timeouts tos = s.tos;
    CancelToken* cancel = s.cancel;
    TlsUp up{ s.release_fd(), ssl, ep, tos };

    if (auto r = tls_handshake(up.fd, up.ssl, tos, cancel); !r)
        return std::unexpected(std::move(r).error());

    // 要求对端已通过ALPN协商h2。  Anthropic的边缘确实如此；
    // 将 ALPN 剥离回 http/1.1 的代理需要单独的支持，
    // 这里大声错误比无声的 nghttp2 协议错误要好。
    const unsigned char* alpn = nullptr; unsigned int alpn_len = 0;
    SSL_get0_alpn_selected(up.ssl, &alpn, &alpn_len);
    if (alpn_len != 2 || !alpn || alpn[0] != 'h' || alpn[1] != '2')
        return std::unexpected(HttpError::tls("peer did not negotiate h2 (ALPN)"));

    return up;
}

// TlsUp ─h2─▶ H2就绪。  构建nghttp2会话，提交SETTINGS，推动初始交换。
// 一旦 make_unique<Connection> 成功，连接就拥有 fd+
// ssl+session，因此我们将它们从 TlsUp 令牌中清除，以保持 ~TlsUp
// 免受 double-freeing 的影响。
io::fsm::Result<H2Ready, HttpError> establish_h2(TlsUp&& s) {
    io::fsm::assert_legal_edge<TlsUp, H2Ready>();

    // --- nghttp2 会话 ---
    nghttp2_session_callbacks* cbs = nullptr;
    nghttp2_session_callbacks_new(&cbs);
    nghttp2_session_callbacks_set_on_frame_recv_callback       (cbs, on_frame_recv);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback  (cbs, on_data_chunk);
    nghttp2_session_callbacks_set_on_stream_close_callback     (cbs, on_stream_close);
    nghttp2_session_callbacks_set_on_header_callback           (cbs, on_header);

    nghttp2_session* session = nullptr;
    int rc = nghttp2_session_client_new(&session, cbs, /*user_data=*/nullptr);
    nghttp2_session_callbacks_del(cbs);
    if (rc != 0 || !session)
        return std::unexpected(HttpError::protocol(
            std::string{"nghttp2_session_client_new: "} + nghttp2_strerror(rc)));

    // 客户前言+SETTINGS。  我们将 INITIAL_WINDOW_SIZE
    // 提高到 8 MiB，因此长响应 正文（SSE 对于 32k
    // -token 消息）不会在流量控制上停滞。  默认的 64 KiB
    // 正是在 libcurl 下咬住 agentty 的脚枪——我们在这里
    // 明确修复它，而不是信任实现细节。
    nghttp2_settings_entry iv[] = {
        { NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE,    8 * 1024 * 1024 },
        { NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 100 },
        { NGHTTP2_SETTINGS_ENABLE_PUSH,            0 },
    };
    nghttp2_submit_settings(session, NGHTTP2_FLAG_NONE, iv,
                            sizeof(iv) / sizeof(iv[0]));
    // 也提高连接-level窗口——默认值是相同的64 KiB。
    constexpr int32_t kConnectionWindow = 32 * 1024 * 1024;
    nghttp2_session_set_local_window_size(session, NGHTTP2_FLAG_NONE, 0,
                                          kConnectionWindow);

    // 将 fd+ssl 的 所有权 传输到连接中。此后，~TlsUp 不得
    // 释放它们——将它们从令牌中删除。 `session` 是我们新的；
    // 如果 make_unique 抛出（它不会：noexcept 路径），
    // 它就会泄漏，但 分配 失败无论如何都会中止该过程。
    Timeouts tos = s.tos;
    socket_t fd  = std::exchange(s.fd, kBadSocket);
    tls::SSL* ssl = std::exchange(s.ssl, nullptr);
    auto conn = std::make_unique<Connection>(fd, ssl, session, s.ep);
    if (auto r = conn->pump_initial(tos); !r)
        return std::unexpected(std::move(r).error());
    return H2Ready{ .conn = std::move(conn) };
}

} // 命名空间结束。

// -----------------------------------------------------------------------
// 拨打新连接：TCP、TLS、nghttp2前导码+SETTINGS。
// 返回一个ready-to-go连接。  驱动上面的 typestate 管道：
// 每个 `and_then` 恰好前进一个声明的边缘，并且在任何步骤 Short
// -circuits 处失败并出现 带类型的 错误，而 move-only
// 状态令牌则展开所获取的任何内容。
// -----------------------------------------------------------------------
std::expected<std::unique_ptr<Connection>, HttpError>
dial_new(const Endpoint& ep, Timeouts tos, CancelTokenPtr cancel) {
    return connect_tcp(Dialing{ {}, ep, tos, cancel.get() })
        .and_then(negotiate_tls)
        .and_then(establish_h2)
        .transform([](H2Ready&& r) { return std::move(r.conn); });
}

// -----------------------------------------------------------------------
// 明文HTTP/1.1 —对于
// http://localhost:PORT.上的本地OpenAI-compatible服务器(Ollama，llama.cpp)，没有TLS，没有h2，没有池。普通阻塞-ish套接字I/O超过非-blocking
// fd dial_tcp()返回，poll()
// -driven以遵守取消 +截止日期。两个切入点：
//   ·plain_unary_send — 一个 request/response (GET/v1/models)
//   ·plain_stream-分块SSE读取馈送handler.on_chunk
//                           (POST/v1/chat/completions，数据流:真)
// -----------------------------------------------------------------------
namespace {

// 原始套接字 write-all、poll-driven、取消+截止日期感知。
std::expected<void, HttpError>
plain_send_all(socket_t fd, const char* data, size_t len,
               clock_t_::time_point deadline, CancelToken* cancel) {
    size_t off = 0;
    while (off < len) {
        if (cancel && cancel->is_cancelled())
            return std::unexpected(HttpError::cancelled("h1 plain write"));
#if defined(_WIN32)
        int n = ::send(fd, data + off, static_cast<int>(len - off), 0);
#else
        ssize_t n = ::send(fd, data + off, len - off, MSG_NOSIGNAL);
#endif
        if (n > 0) { off += static_cast<size_t>(n); continue; }
        int e = sock_last_err();
        if (sock_intr(e)) continue;
        if (!sock_in_progress(e))
            return std::unexpected(HttpError::connect(
                "h1 plain send: errno=" + std::to_string(e)));
        int rem = remaining_ms(deadline, 200);
        if (rem <= 0)
            return std::unexpected(HttpError::timeout("h1 plain write timed out"));
        pollfd pfd{ fd, POLLOUT, 0 };
        int pr = sock_poll(&pfd, 1, rem);
        if (pr < 0 && !sock_intr(sock_last_err()))
            return std::unexpected(HttpError::connect(
                "h1 plain write poll: errno=" + std::to_string(sock_last_err())));
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
            return std::unexpected(HttpError::socket_hangup("h1 plain write hangup"));
    }
    return {};
}

// 构建针对明文服务器的HTTP/1.1请求头（+正文)。
// `Connection: close`，因此对于一元，
// 正文以EOF结束；对于 流式传输，
// 我们保持连接打开并依靠服务器的分块编码/其自身的关闭来结束 SSE。
std::string plain_build_request(const Endpoint& ep, const Request& req,
                                bool keep_alive) {
    std::string out;
    out += wire_name(req.method);
    out += ' ';
    out += req.path.empty() ? "/" : req.path;
    out += " HTTP/1.1\r\n";
    out += "host: ";
    out += ep.host;
    if (ep.port != 80) { out += ':'; out += std::to_string(ep.port); }
    out += "\r\n";
    for (const auto& h : req.headers) {
        if (h.name == "host" || h.name == "content-length" ||
            h.name == "connection")
            continue;
        out += h.name; out += ": "; out += h.value; out += "\r\n";
    }
    out += "content-length: "; out += std::to_string(req.body.size()); out += "\r\n";
    out += keep_alive ? "connection: keep-alive\r\n\r\n"
                      : "connection: close\r\n\r\n";
    out += req.body;
    return out;
}

// 解析 "HTTP/1.1 200 OK\r\nHeader: v\r\n...\r\n\r\n"进入状态+头文件。
// 返回刚刚经过\r\n\r\n的字节偏移量，如果不完整则返回npos。
std::size_t plain_parse_head(std::string_view in, int& status, Headers& headers) {
    auto hdr_end = in.find("\r\n\r\n");
    if (hdr_end == std::string_view::npos) return std::string_view::npos;
    std::string_view head = in.substr(0, hdr_end);
    auto eol = head.find("\r\n");
    std::string_view status_line =
        head.substr(0, eol == std::string_view::npos ? head.size() : eol);
    status = 0;
    if (auto sp = status_line.find(' '); sp != std::string_view::npos) {
        // HTTP状态正好是3位数字；限制累积，以便发送长数字运行的
        // 敌对或损坏的服务器不会溢出 `int`（有符号的溢出是
        // UB）。停在第一个非 -digit 或第 4 位数字处。
        for (size_t k = sp + 1; k < status_line.size() && k <= sp + 3
                 && status_line[k] >= '0' && status_line[k] <= '9'; ++k)
            status = status * 10 + (status_line[k] - '0');
    }
    size_t pos = (eol == std::string_view::npos) ? head.size() : eol + 2;
    while (pos < head.size()) {
        auto nl = head.find("\r\n", pos);
        std::string_view line =
            head.substr(pos, (nl == std::string_view::npos ? head.size() : nl) - pos);
        pos = (nl == std::string_view::npos) ? head.size() : nl + 2;
        auto colon = line.find(':');
        if (colon == std::string_view::npos) continue;
        std::string name{line.substr(0, colon)};
        for (auto& c : name) c = static_cast<char>(std::tolower((unsigned char)c));
        size_t vb = colon + 1;
        while (vb < line.size() && (line[vb] == ' ' || line[vb] == '\t')) ++vb;
        headers.push_back({ std::move(name), std::string{line.substr(vb)} });
    }
    return hdr_end + 4;
}

enum class TransferFraming { Identity, Chunked, Invalid };

[[nodiscard]] TransferFraming transfer_framing(const Headers& headers) {
    bool saw = false;
    for (const auto& h : headers) {
        if (h.name != "transfer-encoding") continue;
        if (saw) return TransferFraming::Invalid;
        saw = true;
        std::string_view value = h.value;
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
            value.remove_prefix(1);
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
            value.remove_suffix(1);
        // 我们请求身份并且仅实现标准的最终分块编码。
        // 拒绝编码堆栈，而不是将编码的 字节 泄漏到 SSE
        // 解析器 中。
        if (value.empty() || value.find(',') != std::string_view::npos)
            return TransferFraming::Invalid;
        std::string lower{value};
        for (char& c : lower)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower != "chunked") return TransferFraming::Invalid;
    }
    return saw ? TransferFraming::Chunked : TransferFraming::Identity;
}

// 根据其`content-encoding`就地解码响应正文。 agentty
// 要求 `identity`，但网关无论如何都可以 gzip（z.ai 的
// /models 可以 — 问题 #30）；如果没有这个，压缩的 字节 会
// 作为垃圾到达 JSON 解析器，并且模型列表会默默地返回为空。成功后，
// 正文 会被替换为膨胀的 字节，头文件 会被重写为 `identity`，
// 因此没有消费者会使用 double-decodes 或错误的 -reports 长度。
// 失败时（未知编码或损坏的 数据流），原始 正文 为 LEFT UNTOUCHED，
// 并记录一条警告 - 降级为之前的行为而不是丢弃响应。
// 64 MiB 帽内装有减压炸弹（API JSON 要小得多）。
void content_decode_body(Headers& headers, std::string& body) {
    constexpr std::size_t kMaxInflated = 64ull * 1024 * 1024;
    for (auto& h : headers) {
        if (h.name != "content-encoding") continue;
        if (h.value.empty()) return;
        auto decoded = io::compress::decode_content_encoding(
            h.value, body, kMaxInflated);
        if (!decoded) {
            AGT_LOG(Net, Warn, "http.content_encoding.undecodable",
                    "encoding={} bytes={}", h.value, body.size());
            return;   // 保留原始的 正文 — 调用者仍然可以挽救它
        }
        body = std::move(*decoded);
        h.value = "identity";   // 下游 看得很清楚，正确的是-sized 字节
        return;
    }
}

[[nodiscard]] bool parse_content_length(const Headers& headers,
                                        std::optional<size_t>& out) {
    out.reset();
    for (const auto& h : headers) {
        if (h.name != "content-length") continue;
        if (h.value.empty()) return false;
        size_t value = 0;
        for (char c : h.value) {
            if (c < '0' || c > '9'
                || value > (std::numeric_limits<size_t>::max()
                            - static_cast<size_t>(c - '0')) / 10)
                return false;
            value = value * 10 + static_cast<size_t>(c - '0');
        }
        if (out && *out != value) return false;
        out = value;
    }
    return true;
}

// 增量de-chunker。当它们到达时，喂它生的 正文 字节；
// 它将完全-decoded 载荷附加到`out`，并在调用上保持部分-chunk状态。
// 对格式错误的块头文件返回假。当看到终止 0-length 块时，
// 设置 `done`。
struct ChunkDecoder {
    std::string pending;
    bool awaiting_trailers = false;
    bool done = false;

    bool feed(std::string_view bytes, std::string& out) {
        if (done) return bytes.empty();
        pending.append(bytes);
        for (;;) {
            if (awaiting_trailers) {
                // 空拖车部分为CRLF；否则等待尾部
                // 块终止符。将其设置为响应 头文件。
                if (pending.starts_with("\r\n")) {
                    pending.erase(0, 2);
                    done = true;
                    return pending.empty();
                }
                const auto end = pending.find("\r\n\r\n");
                if (end == std::string::npos)
                    return pending.size() <= 64 * 1024;
                pending.erase(0, end + 4);
                done = true;
                return pending.empty();
            }

            const auto ln = pending.find("\r\n");
            if (ln == std::string::npos)
                return pending.size() <= 1024; // chunk-size线帽
            const std::string_view line{pending.data(), ln};
            const auto semi = line.find(';');
            const std::string_view digits = line.substr(0, semi);
            if (digits.empty()) return false;

            size_t sz = 0;
            for (char c : digits) {
                const int d = (c >= '0' && c <= '9') ? c - '0'
                            : (c >= 'a' && c <= 'f') ? c - 'a' + 10
                            : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
                if (d < 0 || sz > (static_cast<size_t>(1) << 28))
                    return false;
                sz = sz * 16 + static_cast<size_t>(d);
            }
            const size_t data_start = ln + 2;
            if (sz == 0) {
                pending.erase(0, data_start);
                awaiting_trailers = true;
                continue;
            }
            if (sz > (static_cast<size_t>(1) << 28)) return false;
            if (data_start > pending.size()
                || sz > pending.size() - data_start
                || pending.size() - data_start - sz < 2) {
                return true; // 不完整的块
            }
            const size_t data_end = data_start + sz;
            if (pending[data_end] != '\r' || pending[data_end + 1] != '\n')
                return false;
            out.append(pending, data_start, sz);
            pending.erase(0, data_end + 2);
        }
    }
};

} // 命名空间结束。


// 明文 HTTP/1.1 流式传输 SSE 读取。镜像 Client::stream
// 对 h2 的作用，但通过原始套接字：拨号、写入请求、读取直到头部完成（触发
// on_headers），然后 数据流 正文 字节（如果需要 de-chunking）
// 到 on_chunk，直到服务器关闭或发送 终端 0-chunk。
std::expected<void, HttpError>
plain_stream(const Endpoint& ep, const Request& req, StreamHandler& handler,
             Timeouts tos, CancelToken* cancel, bool& accepted_post) {
    accepted_post = false;
    auto fd_or = dial_tcp(ep, tos, cancel);
    if (!fd_or) return std::unexpected(std::move(fd_or).error());
    socket_t fd = *fd_or;
    auto cleanup = [&] { if (fd != kBadSocket) sock_close(fd); };

    // 流式传输:无总上限(tos.total==0用于聊天)。
    // 使用惰轮作为楔形防护装置；回到慷慨的固定写入截止日期。
    auto write_deadline = clock_t_::now() + std::chrono::milliseconds{30'000};
    if (tos.absolute_deadline && *tos.absolute_deadline < write_deadline)
        write_deadline = *tos.absolute_deadline;
    std::string head_wire = plain_build_request(ep, req, /*keep_alive=*/false);
    if (auto r = plain_send_all(fd, head_wire.data(), head_wire.size(),
                               write_deadline, cancel); !r) {
        cleanup(); return std::unexpected(std::move(r).error());
    }

    std::string inbuf;
    bool head_done = false;
    int  status = 0;
    Headers headers;
    bool chunked = false;
    std::optional<size_t> content_length;
    size_t body_received = 0;
    ChunkDecoder dechunk;
    bool delivered_any = false;
    auto last_rx = clock_t_::now();
    auto last_activity_report = last_rx - std::chrono::seconds{1};

    char buf[64 * 1024];
    while (true) {
        if (cancel && cancel->is_cancelled()) {
            cleanup(); return std::unexpected(HttpError::cancelled("h1 plain stream"));
        }
        if (tos.absolute_deadline && clock_t_::now() >= *tos.absolute_deadline) {
            cleanup();
            return std::unexpected(HttpError::timeout(
                "h1 plain stream operation deadline exceeded"));
        }
#if defined(_WIN32)
        int r = ::recv(fd, buf, sizeof(buf), 0);
#else
        ssize_t r = ::recv(fd, buf, sizeof(buf), 0);
#endif
        if (r > 0) {
            last_rx = clock_t_::now();
            if (handler.on_activity
                && last_rx - last_activity_report >= std::chrono::seconds{1}) {
                last_activity_report = last_rx;
                handler.on_activity();
            }
            if (!head_done) {
                inbuf.append(buf, static_cast<size_t>(r));
                auto body_at = plain_parse_head(inbuf, status, headers);
                if (body_at != std::string_view::npos) {
                    head_done = true;
                    const auto framing = transfer_framing(headers);
                    chunked = framing == TransferFraming::Chunked;
                    if (status == 0
                        || framing == TransferFraming::Invalid
                        || !parse_content_length(headers, content_length)
                        || (chunked && content_length)) {
                        cleanup();
                        return std::unexpected(HttpError::protocol(
                            "h1 plain invalid or ambiguous response framing"));
                    }
                    if (handler.on_headers) handler.on_headers(status, headers);
                    accepted_post = req.method == HttpMethod::Post
                                 && status >= 200 && status < 300;
                    if (!chunked && content_length && *content_length == 0) {
                        cleanup();
                        return {};
                    }
                    std::string body0 = inbuf.substr(body_at);
                    inbuf.clear();
                    if (!body0.empty()) {
                        std::string payload;
                        if (chunked && !dechunk.feed(body0, payload)) {
                            cleanup();
                            return std::unexpected(HttpError::protocol(
                                "h1 plain malformed chunk framing"));
                        }
                        if (!chunked) payload = std::move(body0);
                        body_received += payload.size();
                        if (content_length && body_received > *content_length) {
                            cleanup();
                            return std::unexpected(HttpError::protocol(
                                "h1 plain body exceeds Content-Length"));
                        }
                        if (!payload.empty() && handler.on_chunk) {
                            delivered_any = true;
                            if (!handler.on_chunk(payload)) { cleanup(); return {}; }
                        }
                        if (!chunked && content_length
                            && body_received == *content_length) {
                            cleanup();
                            return {};
                        }
                        if (chunked && dechunk.done) { cleanup(); return {}; }
                    }
                }
                continue;
            }
            std::string payload;
            if (chunked && !dechunk.feed(
                    std::string_view{buf, static_cast<size_t>(r)}, payload)) {
                cleanup();
                return std::unexpected(HttpError::protocol(
                    "h1 plain malformed chunk framing"));
            }
            if (!chunked) payload.assign(buf, static_cast<size_t>(r));
            body_received += payload.size();
            if (content_length && body_received > *content_length) {
                cleanup();
                return std::unexpected(HttpError::protocol(
                    "h1 plain body exceeds Content-Length"));
            }
            if (!payload.empty() && handler.on_chunk) {
                delivered_any = true;
                if (!handler.on_chunk(payload)) { cleanup(); return {}; }
            }
            if (!chunked && content_length
                && body_received == *content_length) {
                cleanup();
                return {};
            }
            if (chunked && dechunk.done) { cleanup(); return {}; }
            continue;
        }
        if (r == 0) {
            cleanup();
            if (!head_done)
                return std::unexpected(HttpError::protocol(
                    "h1 plain peer closed before response headers"));
            if (chunked && !dechunk.done)
                return std::unexpected(HttpError::peer_closed(
                    "h1 plain peer closed mid-chunked stream"));
            if (!chunked && content_length && body_received != *content_length)
                return std::unexpected(HttpError::peer_closed(
                    "h1 plain peer closed before Content-Length body completed"));
            return {};
        }
        int e = sock_last_err();
        if (sock_intr(e)) continue;
        if (!sock_in_progress(e)) {
            cleanup();
            return std::unexpected(HttpError::socket_hangup(
                "h1 plain recv: errno=" + std::to_string(e)));
        }
        // will-block：与空闲守卫进行轮询。
        int rem = 200;
        if (tos.absolute_deadline) {
            rem = remaining_ms(tos.absolute_deadline, rem);
            if (rem <= 0) {
                cleanup();
                return std::unexpected(HttpError::timeout(
                    "h1 plain stream operation deadline exceeded"));
            }
        }
        if (tos.idle.count() > 0) {
            auto since = clock_t_::now() - last_rx;
            if (since >= tos.idle) {
                cleanup();
                return std::unexpected(HttpError::timeout("h1 plain idle timeout"));
            }
        }
        pollfd pfd{ fd, POLLIN, 0 };
        int pr = sock_poll(&pfd, 1, rem);
        if (pr < 0 && !sock_intr(sock_last_err())) {
            cleanup();
            return std::unexpected(HttpError::socket_hangup(
                "h1 plain poll: errno=" + std::to_string(sock_last_err())));
        }
        if (pfd.revents & (POLLERR | POLLNVAL)) {
            cleanup();
            return std::unexpected(HttpError::socket_hangup("h1 plain poll hangup"));
        }
        (void)delivered_any;
    }
}

// 明文HTTP/1.1一元(本地服务器上的GET/v1/models)。
std::expected<Response, HttpError>
plain_unary_send(const Endpoint& ep, const Request& req, Timeouts tos,
                 CancelToken* cancel) {
    auto fd_or = dial_tcp(ep, tos, cancel);
    if (!fd_or) return std::unexpected(std::move(fd_or).error());
    socket_t fd = *fd_or;
    auto cleanup = [&] { if (fd != kBadSocket) sock_close(fd); };

    auto deadline = clock_t_::now() +
        (tos.total.count() > 0 ? tos.total : std::chrono::milliseconds{30'000});
    std::string wire = plain_build_request(ep, req, /*keep_alive=*/false);
    if (auto r = plain_send_all(fd, wire.data(), wire.size(), deadline, cancel); !r) {
        cleanup(); return std::unexpected(std::move(r).error());
    }

    std::string in;
    char buf[16 * 1024];
    while (true) {
        if (cancel && cancel->is_cancelled()) {
            cleanup(); return std::unexpected(HttpError::cancelled("h1 plain unary"));
        }
        if (in.size() > req.max_body_bytes + (64u << 10)) {
            cleanup();
            return std::unexpected(HttpError::body(
                "h1 plain response exceeds max_body_bytes"));
        }
#if defined(_WIN32)
        int r = ::recv(fd, buf, sizeof(buf), 0);
#else
        ssize_t r = ::recv(fd, buf, sizeof(buf), 0);
#endif
        if (r > 0) { in.append(buf, static_cast<size_t>(r)); continue; }
        if (r == 0) break;   // EOF — 连接：关闭
        int e = sock_last_err();
        if (sock_intr(e)) continue;
        if (!sock_in_progress(e)) {
            if (!in.empty()) break;
            cleanup();
            return std::unexpected(HttpError::socket_hangup(
                "h1 plain recv: errno=" + std::to_string(e)));
        }
        int rem = remaining_ms(deadline, 200);
        if (rem <= 0) { cleanup(); return std::unexpected(HttpError::timeout("h1 plain read timed out")); }
        pollfd pfd{ fd, POLLIN, 0 };
        int pr = sock_poll(&pfd, 1, rem);
        if (pr < 0 && !sock_intr(sock_last_err())) {
            cleanup();
            return std::unexpected(HttpError::socket_hangup(
                "h1 plain poll: errno=" + std::to_string(sock_last_err())));
        }
    }
    cleanup();

    int status = 0;
    Headers headers;
    auto body_at = plain_parse_head(in, status, headers);
    if (body_at == std::string_view::npos)
        return std::unexpected(HttpError::protocol("h1 plain: no header terminator"));
    std::string body = in.substr(body_at);
    const auto framing = transfer_framing(headers);
    if (framing == TransferFraming::Invalid)
        return std::unexpected(HttpError::protocol(
            "h1 plain unsupported Transfer-Encoding"));
    if (framing == TransferFraming::Chunked) {
        ChunkDecoder d;
        std::string decoded;
        if (!d.feed(body, decoded) || !d.done)
            return std::unexpected(HttpError::protocol(
                "h1 plain malformed or truncated chunk framing"));
        body = std::move(decoded);
    }
    content_decode_body(headers, body);
    return Response{ status, std::move(headers), std::move(body) };
}

// -----------------------------------------------------------------------
// HTTP/1.1一元回退。
//
// 池化核心传输层更喜欢h2来进行流量控制和复用。
// 一元请求和流式传输请求均具有用于从ALPN移除h2的TLS中介的one
// -connection HTTP/1.1 回退。
//
// 但是，air-gapped主机通过SOCKS/SSH隧道到达互联网，
// 并且该隧道的远端经常是TLS-intercepting企业代理，
// 其将ALPN降级到http/1.1.。透明隧道保留ALPN end-to-end；
// 拦截者仅终止 TLS 和 re-offers http/1.1. 当发生这种情况时，
// OAuth 令牌刷新（一个微小的一元 POST）因
// "peer did not negotiate h2 (ALPN)" 失败，会话无法刷新，
// 并且 - 因为失败被分类为 Tls，而不是 Auth - mid-session
// 刷新-retry 永远不会触发。
//
// 一元回退低于缓冲区一个完整响应。此文件后面的 流式传输 回退
// 逐渐 de-chunks SSE 并使用绝对期限，
// 因为 HTTP/1.1 没有协议 -level PING。
// 正确，当且仅当 `e` 是 dial_new 中引发的特定
// "peer negotiated something other than h2" 故障 — h1 回退 所针对的一种情况。
// 匹配在下面的单个拒绝站点处设置的详细子串。
bool is_alpn_h2_failure(const HttpError& e) noexcept {
    return e.kind == HttpErrorKind::Tls &&
           e.detail.find("negotiate h2 (ALPN)") != std::string::npos;
}

std::expected<Response, HttpError>
h1_unary_send(const Endpoint& ep, const Request& req, Timeouts tos,
              CancelToken* cancel) {
    auto fd_or = dial_tcp(ep, tos, cancel);
    if (!fd_or) return std::unexpected(std::move(fd_or).error());
    socket_t fd = *fd_or;

    tls::SSL* ssl = tls::wrap_client(static_cast<int>(fd), ep.host);
    if (!ssl) { sock_close(fd); return std::unexpected(HttpError::tls("SSL_new failed")); }
    auto cleanup = [&] { tls::free_ssl(ssl); sock_close(fd); };

    if (auto r = tls_handshake(fd, ssl, tos, cancel); !r) {
        cleanup(); return std::unexpected(std::move(r).error());
    }

    // 截止日期总计-request。  tos.total==0 表示
    // "no cap"；回落到宽敞的固定上限，因此楔形代理无法永远挂起刷新。
    auto deadline = clock_t_::now() +
        (tos.total.count() > 0 ? tos.total : std::chrono::milliseconds{60'000});

    // ── 构建请求头 ───────────────────────────────────────
    std::string out;
    out += wire_name(req.method);
    out += ' ';
    out += req.path.empty() ? "/" : req.path;
    out += " HTTP/1.1\r\n";
    out += "host: ";
    out += ep.host;
    if (ep.port != 443) { out += ':'; out += std::to_string(ep.port); }
    out += "\r\n";
    for (const auto& h : req.headers) {
        // 我们拥有主机/内容-length/连接——跳过任何调用者欺骗，
        // 因此传输请求保持良好-formed。
        if (h.name == "host" || h.name == "content-length" ||
            h.name == "connection")
            continue;
        out += h.name; out += ": "; out += h.value; out += "\r\n";
    }
    out += "content-length: "; out += std::to_string(req.body.size()); out += "\r\n";
    out += "connection: close\r\n\r\n";
    out += req.body;

    // ── 全部写( 不) -blocking SSL + 民意测验,镜像 tls_handshake) ──
    {
        size_t off = 0;
        while (off < out.size()) {
            if (cancel && cancel->is_cancelled())
                { cleanup(); return std::unexpected(HttpError::cancelled("h1 write")); }
            int chunk = static_cast<int>(std::min<size_t>(out.size() - off, 1 << 20));
            int r = SSL_write(ssl, out.data() + off, chunk);
            if (r > 0) { off += static_cast<size_t>(r); continue; }
            int e = SSL_get_error(ssl, r);
            if (e != SSL_ERROR_WANT_READ && e != SSL_ERROR_WANT_WRITE)
                { cleanup(); return std::unexpected(HttpError::tls("h1 SSL_write: " + tls::last_error(ssl))); }
            int rem = remaining_ms(deadline, 200);
            if (rem <= 0) { cleanup(); return std::unexpected(HttpError::timeout("h1 write timed out")); }
            pollfd pfd{ fd, (e == SSL_ERROR_WANT_READ) ? (short)POLLIN : (short)POLLOUT, 0 };
            int pr = sock_poll(&pfd, 1, rem);
            if (pr < 0 && !sock_intr(sock_last_err()))
                { cleanup(); return std::unexpected(HttpError::tls("h1 write poll: errno=" + std::to_string(sock_last_err()))); }
            if (pfd.revents & (POLLERR | POLLNVAL))
                { cleanup(); return std::unexpected(HttpError::socket_hangup("h1 write hangup")); }
        }
    }

    // ── 读至EOF(服务器荣誉) ─────────────
    std::string in;
    bool eof = false;
    while (!eof) {
        if (cancel && cancel->is_cancelled())
            { cleanup(); return std::unexpected(HttpError::cancelled("h1 read")); }
        if (in.size() > req.max_body_bytes + (64u << 10))   // 头文件 + 正文 帽
            { cleanup(); return std::unexpected(HttpError::tls("h1 response exceeds max_body_bytes")); }
        char buf[16 * 1024];
        int r = SSL_read(ssl, buf, sizeof(buf));
        if (r > 0) { in.append(buf, static_cast<size_t>(r)); continue; }
        int e = SSL_get_error(ssl, r);
        if (e == SSL_ERROR_ZERO_RETURN) { eof = true; break; }     // 清洁TLS关闭
        if (e == SSL_ERROR_SYSCALL && r == 0) { eof = true; break; } // 原始 FIN w/o close_notify
        if (e != SSL_ERROR_WANT_READ && e != SSL_ERROR_WANT_WRITE) {
            // 有些服务器在完整的正文之后没有close_notify
            // 就断开连接；如果我们已经有完整的-looking响应，
            // 请将其视为EOF而不是错误。
            if (!in.empty()) { eof = true; break; }
            cleanup(); return std::unexpected(HttpError::tls("h1 SSL_read: " + tls::last_error(ssl)));
        }
        int rem = remaining_ms(deadline, 200);
        if (rem <= 0) { cleanup(); return std::unexpected(HttpError::timeout("h1 read timed out")); }
        pollfd pfd{ fd, (e == SSL_ERROR_WANT_WRITE) ? (short)POLLOUT : (short)POLLIN, 0 };
        int pr = sock_poll(&pfd, 1, rem);
        if (pr < 0 && !sock_intr(sock_last_err()))
            { cleanup(); return std::unexpected(HttpError::tls("h1 read poll: errno=" + std::to_string(sock_last_err()))); }
    }
    cleanup();

    // ── 解析 状态行 + 头文件 + 正文 ────────────────────────────
    auto hdr_end = in.find("\r\n\r\n");
    if (hdr_end == std::string::npos)
        return std::unexpected(HttpError::protocol("h1: no header terminator"));
    std::string_view head_sv{in.data(), hdr_end};
    std::string body = in.substr(hdr_end + 4);

    auto eol = head_sv.find("\r\n");
    std::string_view status_line = head_sv.substr(0, eol == std::string_view::npos ? head_sv.size() : eol);
    // "HTTP/1.1 200 OK" → 200
    int status = 0;
    {
        auto sp = status_line.find(' ');
        if (sp != std::string_view::npos) {
            // 3位上限：HTTP状态代码为3位，
            // 并且无界累加器在broken/hostile状态行上溢出`int`
            // (UB)。
            for (size_t k = sp + 1; k < status_line.size() && k <= sp + 3
                     && status_line[k] >= '0' && status_line[k] <= '9'; ++k)
                status = status * 10 + (status_line[k] - '0');
        }
    }
    if (status == 0)
        return std::unexpected(HttpError::protocol("h1: malformed status line"));

    Headers headers;
    size_t pos = (eol == std::string_view::npos) ? head_sv.size() : eol + 2;
    while (pos < head_sv.size()) {
        auto nl = head_sv.find("\r\n", pos);
        std::string_view line = head_sv.substr(pos, (nl == std::string_view::npos ? head_sv.size() : nl) - pos);
        pos = (nl == std::string_view::npos) ? head_sv.size() : nl + 2;
        auto colon = line.find(':');
        if (colon == std::string_view::npos) continue;
        std::string name{line.substr(0, colon)};
        for (auto& c : name) c = static_cast<char>(std::tolower((unsigned char)c));
        size_t vb = colon + 1;
        while (vb < line.size() && (line[vb] == ' ' || line[vb] == '\t')) ++vb;
        headers.push_back({ std::move(name), std::string{line.substr(vb)} });
    }

    const auto framing = transfer_framing(headers);
    std::optional<size_t> content_length;
    if (framing == TransferFraming::Invalid
        || !parse_content_length(headers, content_length)
        || (framing == TransferFraming::Chunked && content_length)) {
        return std::unexpected(HttpError::protocol(
            "h1 unary invalid or ambiguous response framing"));
    }
    if (framing == TransferFraming::Chunked) {
        ChunkDecoder decoder;
        std::string decoded;
        if (!decoder.feed(body, decoded) || !decoder.done) {
            return std::unexpected(HttpError::protocol(
                "h1 unary malformed or truncated chunk framing"));
        }
        body = std::move(decoded);
    } else if (content_length && body.size() != *content_length) {
        return std::unexpected(HttpError::peer_closed(
            "h1 unary Content-Length body was truncated"));
    }

    content_decode_body(headers, body);
    return Response{ status, std::move(headers), std::move(body) };
}

// TLS HTTP/1.1 流式传输 回退 用于从 ALPN 中
// 剥离 h2 的公司网关。 SSE 不需要 HTTP/2：分块
// HTTP/1.1 保留相同的 事件 边界。与直接 h2 路径不同，
// h1 中介无法使用 PING 帧 进行探测，并且可能会故意 缓冲
// 区 正文，因此该路径在套接字保持打开状态时发出有界等待心跳，
// 并依赖绝对请求截止时间作为其最终楔形保护。
std::expected<void, HttpError>
h1_tls_stream(const Endpoint& ep, const Request& req, StreamHandler& handler,
              Timeouts tos, CancelToken* cancel, bool& accepted_post) {
    accepted_post = false;
    auto fd_or = dial_tcp(ep, tos, cancel);
    if (!fd_or) return std::unexpected(std::move(fd_or).error());
    socket_t fd = *fd_or;

    tls::SSL* ssl = tls::wrap_client(static_cast<int>(fd), ep.host);
    if (!ssl) {
        sock_close(fd);
        return std::unexpected(HttpError::tls("SSL_new failed"));
    }
    auto cleanup = [&] { tls::free_ssl(ssl); sock_close(fd); };
    if (auto r = tls_handshake(fd, ssl, tos, cancel); !r) {
        cleanup();
        return std::unexpected(std::move(r).error());
    }

    const unsigned char* alpn = nullptr;
    unsigned int alpn_len = 0;
    SSL_get0_alpn_selected(ssl, &alpn, &alpn_len);
    const bool h1 = alpn_len == 0
        || (alpn_len == 8 && alpn
            && std::memcmp(alpn, "http/1.1", 8) == 0);
    if (!h1) {
        cleanup();
        return std::unexpected(HttpError::tls(
            "HTTP/1.1 fallback negotiated an unexpected ALPN protocol"));
    }

    const auto deadline = tos.absolute_deadline.value_or(
        clock_t_::now() + (tos.total.count() > 0 ? tos.total
                                                 : std::chrono::minutes{30}));
    std::string wire = plain_build_request(ep, req, /*keep_alive=*/false);
    size_t off = 0;
    while (off < wire.size()) {
        if (cancel && cancel->is_cancelled()) {
            cleanup();
            return std::unexpected(HttpError::cancelled("h1 TLS stream write"));
        }
        const int n = SSL_write(ssl, wire.data() + off,
            static_cast<int>(std::min<size_t>(wire.size() - off, 1 << 20)));
        if (n > 0) {
            off += static_cast<size_t>(n);
            continue;
        }
        const int e = SSL_get_error(ssl, n);
        if (e != SSL_ERROR_WANT_READ && e != SSL_ERROR_WANT_WRITE) {
            cleanup();
            return std::unexpected(HttpError::tls(
                "h1 TLS stream write: " + tls::last_error(ssl)));
        }
        const int rem = remaining_ms(deadline, 200);
        if (rem <= 0) {
            cleanup();
            return std::unexpected(HttpError::timeout(
                "h1 TLS stream write timed out"));
        }
        pollfd pfd{fd, e == SSL_ERROR_WANT_READ ? short(POLLIN)
                                                : short(POLLOUT), 0};
        const int pr = sock_poll(&pfd, 1, rem);
        if (pr < 0 && !sock_intr(sock_last_err())) {
            cleanup();
            return std::unexpected(HttpError::socket_hangup(
                "h1 TLS stream write poll failed"));
        }
        if (pfd.revents & (POLLERR | POLLNVAL)) {
            cleanup();
            return std::unexpected(HttpError::socket_hangup(
                "h1 TLS stream write hangup"));
        }
    }

    std::string inbuf;
    bool head_done = false;
    int status = 0;
    Headers headers;
    bool chunked = false;
    std::optional<size_t> content_length;
    size_t body_received = 0;
    ChunkDecoder dechunk;
    auto last_activity = clock_t_::now() - std::chrono::seconds{15};
    char buf[64 * 1024];

    for (;;) {
        if (cancel && cancel->is_cancelled()) {
            cleanup();
            return std::unexpected(HttpError::cancelled("h1 TLS stream read"));
        }
        if (clock_t_::now() >= deadline) {
            cleanup();
            return std::unexpected(HttpError::timeout(
                "h1 TLS stream reached its absolute deadline"));
        }

        const int n = SSL_read(ssl, buf, sizeof(buf));
        if (n > 0) {
            const auto now = clock_t_::now();
            if (handler.on_activity
                && now - last_activity >= std::chrono::seconds{1}) {
                last_activity = now;
                handler.on_activity();
            }
            if (!head_done) {
                inbuf.append(buf, static_cast<size_t>(n));
                if (inbuf.size() > 64 * 1024) {
                    cleanup();
                    return std::unexpected(HttpError::protocol(
                        "h1 TLS response headers exceed 64 KiB"));
                }
                const auto body_at = plain_parse_head(inbuf, status, headers);
                if (body_at == std::string_view::npos) continue;
                head_done = true;
                const auto framing = transfer_framing(headers);
                chunked = framing == TransferFraming::Chunked;
                if (status == 0
                    || framing == TransferFraming::Invalid
                    || !parse_content_length(headers, content_length)
                    || (chunked && content_length)) {
                    cleanup();
                    return std::unexpected(HttpError::protocol(
                        "h1 TLS invalid or ambiguous response framing"));
                }
                if (handler.on_headers) handler.on_headers(status, headers);
                accepted_post = req.method == HttpMethod::Post
                             && status >= 200 && status < 300;
                if (!chunked && content_length && *content_length == 0) {
                    cleanup();
                    return {};
                }
                std::string initial = inbuf.substr(body_at);
                inbuf.clear();
                if (!initial.empty()) {
                    std::string payload;
                    if (chunked && !dechunk.feed(initial, payload)) {
                        cleanup();
                        return std::unexpected(HttpError::protocol(
                            "h1 TLS malformed chunk framing"));
                    }
                    if (!chunked) payload = std::move(initial);
                    body_received += payload.size();
                    if (content_length && body_received > *content_length) {
                        cleanup();
                        return std::unexpected(HttpError::protocol(
                            "h1 TLS body exceeds Content-Length"));
                    }
                    if (!payload.empty() && handler.on_chunk
                        && !handler.on_chunk(payload)) {
                        cleanup();
                        return {};
                    }
                    if (!chunked && content_length
                        && body_received == *content_length) {
                        cleanup();
                        return {};
                    }
                    if (chunked && dechunk.done) {
                        cleanup();
                        return {};
                    }
                }
                continue;
            }

            std::string payload;
            if (chunked && !dechunk.feed(
                    std::string_view{buf, static_cast<size_t>(n)}, payload)) {
                cleanup();
                return std::unexpected(HttpError::protocol(
                    "h1 TLS malformed chunk framing"));
            }
            if (!chunked) payload.assign(buf, static_cast<size_t>(n));
            body_received += payload.size();
            if (content_length && body_received > *content_length) {
                cleanup();
                return std::unexpected(HttpError::protocol(
                    "h1 TLS body exceeds Content-Length"));
            }
            if (!payload.empty() && handler.on_chunk
                && !handler.on_chunk(payload)) {
                cleanup();
                return {};
            }
            if (!chunked && content_length
                && body_received == *content_length) {
                cleanup();
                return {};
            }
            if (chunked && dechunk.done) {
                cleanup();
                return {};
            }
            continue;
        }

        const int e = SSL_get_error(ssl, n);
        if (e == SSL_ERROR_ZERO_RETURN || (e == SSL_ERROR_SYSCALL && n == 0)) {
            cleanup();
            if (!head_done)
                return std::unexpected(HttpError::protocol(
                    "h1 TLS peer closed before response headers"));
            if (chunked && !dechunk.done)
                return std::unexpected(HttpError::peer_closed(
                    "h1 TLS peer closed mid-chunked stream"));
            if (!chunked && content_length && body_received != *content_length)
                return std::unexpected(HttpError::peer_closed(
                    "h1 TLS peer closed before Content-Length body completed"));
            return {};
        }
        if (e != SSL_ERROR_WANT_READ && e != SSL_ERROR_WANT_WRITE) {
            cleanup();
            return std::unexpected(HttpError::tls(
                "h1 TLS stream read: " + tls::last_error(ssl)));
        }

        const int rem = remaining_ms(deadline, 200);
        pollfd pfd{fd, e == SSL_ERROR_WANT_WRITE ? short(POLLOUT)
                                                 : short(POLLIN), 0};
        const int pr = sock_poll(&pfd, 1, rem);
        if (pr < 0 && !sock_intr(sock_last_err())) {
            cleanup();
            return std::unexpected(HttpError::socket_hangup(
                "h1 TLS stream read poll failed"));
        }
        if (pfd.revents & (POLLERR | POLLNVAL)) {
            cleanup();
            return std::unexpected(HttpError::socket_hangup(
                "h1 TLS stream read hangup"));
        }

        // HTTP/1没有协议PING。这并不要求 传输
        // 活动：它报告显式有界缓冲状态，因此 UI 可以
        // 解释为什么 DATA 可能仅在完成时到达，
        // 而绝对截止日期仍然是最终护栏。
        const auto now = clock_t_::now();
        if (handler.on_buffered_wait
            && now - last_activity >= std::chrono::seconds{15}) {
            last_activity = now;
            handler.on_buffered_wait();
        }
    }
}

// -----------------------------------------------------------------------
// 连接 池。  每个端点的简单 LIFO 堆栈具有两个年龄上限：
// 空闲 TTL (条目在 N 秒内未使用) 和总 生命周期 (
// 条目已存在 N 分钟，无论是否使用)。 生命周期 上限的
// 存在是因为 Anthropic 边缘附近的中间代理通常会在 5
// -15 分钟的 TTL 上运行自己的连接消耗 — 我们的连接
// 看起来还处于活动状态（POLLIN 表示没有 待处理，
// nghttp2 认为它是健康的），但代理将在下一个请求时执行 RST。
// 10分钟主动回收避免了~罕见的-but-fatal情况。
// -----------------------------------------------------------------------
// Direct-connect默认:90秒。  Anthropic 的边缘
// 保持空闲保持活动状态已超过一分钟，但我们会主动回收，以便 池 不会停滞在
// dead-but-silent 套接字上。
//
// 通过AGENTTY_SOCKS_PROXY (air-gapped-via-SSH路径)，
// 每次重新连接都“更加”昂贵：SSH通道上的SOCKS5握手
// +代理主机上的DNS+隧道字节 数据流上的TLS握手。
// 每次拨号总共需要 200-500 毫秒的开销。  用户看到的
// "after a few turns" 逐渐减速主要是 90 年代
// 空闲的 TTL 在作曲家喘息空间期间驱逐连接，以及下一个回合在第
// 一个 SSE 字节 之前支付该开销。  当 SOCKS 处于活动状态时，
// 保持连接的时间延长 5 倍可以避开这一点 -
// kMaxLifetime 上限（10 分钟）仍然限制 proxy
// -drain 安全的使用期限，因此我们不会永远保留连接。
// AGENTTY_POOL_IDLE_TTL（秒）覆盖两个默认值。
//
// 默认的90秒比许多公司代理、VPN和CDN边缘强制执行的空闲超时长(
// 30-60秒是常见的)。当网络首先终止连接时，agentty 仅在写入时
// 才发现尸体 - 如果该写入已经生成了 2xx，则 数据流 在语义上已提交，
// 因此可以重播 CANNOT，而不会冒重复助理轮流的风险。
// 用户看到
// `h2 socket hangup (server accepted request; not replayed automatically)`
// mid-answer。
//
// 设置该BELOW，网络的超时将该可见的故障转换成不可见的新拨号：
// 池逐出该条目本身而不是发现它已死亡。 `http.stream_hangup`
// 记录每次挂断的空闲时间，因此正确的值是可测量的而不是猜测
// 的 — 如果挂断聚集在 60000 毫秒附近，
// 请将其设置为 45。
//
// 0 完全禁用池化（每个请求都重新拨号）——这是敌对网络
// 上最安全的设置，但代价是每轮 TLS 次握手。
inline auto idle_ttl() noexcept -> std::chrono::seconds {
    static const std::chrono::seconds v = [] {
        if (const char* e = std::getenv("AGENTTY_POOL_IDLE_TTL"); e && *e) {
            char* end = nullptr;
            const long n = std::strtol(e, &end, 10);
            if (end != e && n >= 0 && n <= 3600)
                return std::chrono::seconds(n);
        }
        return agentty_socks_proxy().active() ? std::chrono::seconds(450)
                                              : std::chrono::seconds(90);
    }();
    return v;
}
constexpr auto kMaxLifetime = std::chrono::minutes(10);

struct PooledConn {
    std::unique_ptr<Connection>          conn;
    clock_t_::time_point                  created_at;
    clock_t_::time_point                  released_at;
};

// 限制条目-per-endpoint，因此行为不当的调用者（
// e.g.auto-retry循环）不能无限制地增长池并耗尽文件描述符。
// 16涵盖了单个CLI会话的任何实际并发性——
// agentty当前从未对api.anthropic.com有超过一个in-flight请求。
constexpr std::size_t kMaxPoolEntriesPerEndpoint = 16;

// 检测TCP-level closed/reset套接字而不消耗任何字节。
// nghttp2的`want_read/write` 标志仅反映协议
// -state机器——尚未处理的对端 FIN仍会报告"alive"。
// 我们非-blocking poll()为POLLHUP/POLLERR/POLLNVAL/POLLIN;
// POLLIN 和 0-byte MSG_PEEK recv 意味着 对端 完全关闭。没有消耗任何数据。
[[nodiscard]] bool socket_is_alive(socket_t fd) {
    if (fd == kBadSocket) return false;
    pollfd pfd{ fd, POLLIN, 0 };
    int r = sock_poll(&pfd, 1, 0);
    if (r < 0) {
        if (sock_intr(sock_last_err())) return true;  // EINTR — 假设还活着
        return false;
    }
    if (r == 0) return true;  // 没有发生任何事情——很好
    if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) return false;
    if (pfd.revents & POLLIN) {
        // 窥视 1 字节。 EOF (0) → 对端 关闭；数据→精细； EAGAIN → 很好。
        char b;
#if defined(_WIN32)
        int n = ::recv(fd, &b, 1, MSG_PEEK);
#else
        ssize_t n = ::recv(fd, &b, 1, MSG_PEEK | MSG_DONTWAIT);
#endif
        if (n == 0) return false;
        if (n < 0 && !sock_in_progress(sock_last_err())) return false;
    }
    return true;
}

} // 命名空间结束。
} // namespace agentty::http

// 连接单独拥有其套接字、SSL和nghttp2会话（dtor释放所有三个）；
// pend_buf_ 指向同一会话。只有 run()
// 成功的连接才会被池化，而 run() 在成功时会清除会话的
// user_data，因此池化的连接不会指向其自身之外的任何内容。
MAYA_SENDABLE(agentty::http::Connection);

namespace agentty::http {
namespace {

// 锁-free是设计的，不是受保护的地图：acquire()/
// release()在本身从排名锁 正文到达的网络代码内部运行
// （auth_refresh通道在保持排名15的同时在HTTPS上刷新令牌），
// 并且释放过期连接运行SSL_shutdown->new_session_cb
// （TLS 票 快照）。这里的 锁 需要通过程序中的每个
// http 调用 线程化的功能。相反，池 是一个固定的插槽数组，
// 每个插槽都由在其状态字上赢得 CAS 的人拥有：
//
//   空 --CAS--> 忙 (填充者写入 载荷) --store--> 满 满
//   --CAS--> 忙 (声明者 inspects/takes) --store--> 空 | 满
//
// 没有人曾经在繁忙时隙上WAITS——失败的索赔只是跳过它
// ——所以这里没有任何东西可以阻止或死锁。代价是，
// 另一个 线程 正在检查的连接在这些微秒内是不可见的，
// 而呼叫者会重新拨打：对于 缓存 来说是无害的。
class Pool {
    // 槽将其连接交给接下来声明它的那个线程。这就是上面的交叉
    // -thread移动MAYA_SENDABLE(连接)所担保的；
    // 在这里检查它，在它被依赖的地方，这样声明就不会腐烂。
    static_assert(maya::Sendable<PooledConn>,
                  "a pooled connection crosses threads; it must be Sendable");

    enum : int { kEmpty = 0, kBusy = 1, kFull = 2 };
    // Per-endpoint上限为kMaxPoolEntriesPerEndpoint；
    // agentty 与少数端点通信，因此 64 个插槽永远不会限制实际使用。
    // 如果它们已满，release() 将逐出最旧的条目。
    static constexpr std::size_t kSlots = 64;

    struct Slot {
        std::atomic<int> state{kEmpty};
        PooledConn       entry;   // 由设置kBusy的线程拥有
    };

    bool claim(Slot& s) noexcept {
        int expected = kFull;
        return s.state.compare_exchange_strong(expected, kBusy,
                                               std::memory_order_acquire);
    }

public:
    std::unique_ptr<Connection> acquire(const Endpoint& ep) {
        // 过期条目在被发现时被释放；此类中的任何地方都没有举行 锁，
        // 因此 SSL_shutdown -> new_session_cb 就可以了。
        const auto now = clock_t_::now();
        // 最新的在前，就像旧的 LIFO 堆栈一样：最近
        // 发布的套接字最有可能仍然是热的。
        for (;;) {
            Slot* best = nullptr;
            for (auto& s : slots_) {
                if (!claim(s)) continue;
                if (!(s.entry.conn->endpoint() == ep)) {
                    s.state.store(kFull, std::memory_order_release);
                    continue;
                }
                if (!best || s.entry.released_at > best->entry.released_at) {
                    if (best) best->state.store(kFull, std::memory_order_release);
                    best = &s;
                } else {
                    s.state.store(kFull, std::memory_order_release);
                }
            }
            if (!best) return nullptr;
            PooledConn p = std::move(best->entry);
            best->entry = {};
            best->state.store(kEmpty, std::memory_order_release);
            // Four-stage活性检查：总生命周期->空闲TTL-
            // >nghttp2协议状态->
            // socket-levelFIN/RST.每个阶段都很便宜并且捕获前一个阶段错过的一类过期连接；
            // 生命周期 特别捕获了 client-side
            // 检查无法检测到的 proxy-drain 情况。
            if (now - p.created_at  > kMaxLifetime
                || now - p.released_at > idle_ttl()
                || !p.conn->is_alive()
                || !socket_is_alive(p.conn->fd())) {
                continue;   // `p`在此被释放；寻找下一个-newest
            }
            p.conn->set_pooled(true);   // 重用套接字——参见 was_pooled()
            p.conn->set_idle_ms(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - p.released_at).count());
            return std::move(p.conn);
        }
    }

    void release(std::unique_ptr<Connection> c) {
        if (!c || !c->is_alive()) return;
        c->set_pooled(false);   // 回到池——下一个收单方re-marks
        const Endpoint ep = c->endpoint();

        // 此端点的完整：删除最旧的、最有可能在我们到达
        // 时已被 对端 关闭的端点。在并发情况下计数为
        // best-effort（看不到中间-claim
        // 的插槽），这可能会超出赛车释放者数量的上限。
        std::size_t same = 0;
        Slot* oldest = nullptr;
        for (auto& s : slots_) {
            if (!claim(s)) continue;
            if (s.entry.conn->endpoint() == ep) {
                ++same;
                if (!oldest || s.entry.released_at < oldest->entry.released_at) {
                    if (oldest) oldest->state.store(kFull, std::memory_order_release);
                    oldest = &s;
                    continue;
                }
            }
            s.state.store(kFull, std::memory_order_release);
        }
        std::unique_ptr<Connection> evicted;
        if (oldest) {
            if (same >= kMaxPoolEntriesPerEndpoint) {
                evicted = std::move(oldest->entry.conn);
                oldest->entry = {};
                oldest->state.store(kEmpty, std::memory_order_release);
            } else {
                oldest->state.store(kFull, std::memory_order_release);
            }
        }
        evicted.reset();

        // 释放时间代表拨号时间； 生命周期 上限大约是 池 的任期，
        // 而不是套接字年龄。
        const auto now = clock_t_::now();
        for (int attempt = 0; attempt < 2; ++attempt) {
            for (auto& s : slots_) {
                int expected = kEmpty;
                if (!s.state.compare_exchange_strong(expected, kBusy,
                                                     std::memory_order_acquire))
                    continue;
                s.entry = PooledConn{std::move(c), now, now};
                s.state.store(kFull, std::memory_order_release);
                return;
            }
            // 占用的每个槽：逐出总体上最旧的条目，然后是重试。
            Slot* victim = nullptr;
            for (auto& s : slots_) {
                if (!claim(s)) continue;
                if (!victim || s.entry.released_at < victim->entry.released_at) {
                    if (victim) victim->state.store(kFull, std::memory_order_release);
                    victim = &s;
                } else {
                    s.state.store(kFull, std::memory_order_release);
                }
            }
            if (!victim) break;
            PooledConn gone = std::move(victim->entry);
            victim->entry = {};
            victim->state.store(kEmpty, std::memory_order_release);
        }
        // 丢失每个竞态：只需删除`c`（关闭套接字）。 池 是 缓存。
    }

private:
    std::array<Slot, kSlots> slots_{};
};

} // 命名空间结束。
} // namespace agentty::http

// 每个成员只能通过CAS-claimed时隙到达（见上文）。
MAYA_SYNC(agentty::http::Pool);

namespace agentty::http {

namespace test {
std::expected<std::string, std::string>
decode_chunked(const std::vector<std::string>& fragments) {
    ChunkDecoder decoder;
    std::string output;
    for (const auto& fragment : fragments) {
        if (!decoder.feed(fragment, output))
            return std::unexpected("malformed chunk framing");
    }
    if (!decoder.done)
        return std::unexpected("truncated chunk framing");
    return output;
}

std::expected<bool, std::string> is_chunked(const Headers& headers) {
    const auto framing = transfer_framing(headers);
    if (framing == TransferFraming::Invalid)
        return std::unexpected("invalid transfer encoding");
    return framing == TransferFraming::Chunked;
}
} // namespace test

// ---------------------------------------------------------------------------
// Client::Impl
// ---------------------------------------------------------------------------
// 池是锁-free槽位阵列，因此其他线程上的拨号可以共享。
using PoolRef = maya::co_owned<Pool>;

struct Client::Impl {
    Config  cfg;
    PoolRef pool_ref = PoolRef::make();
    Pool&   pool     = *pool_ref;

    // 杰出的prewarm()表盘。 OWNED（未分离），
    // 因此 join_prewarm() 可以 取消 并在 CRT/OpenSSL
    // 拆卸之前等待它们，关闭在快速退出时表现为堆损坏的 exit-race
    // UAF。等待是 maya::pool 的：它是有界的，
    // 因此真正楔入阻塞 getaddrinfo() 的表盘在截止日期前被放弃，
    // 而不是悬挂拆卸。
    // 预热拨号在此运行。替换 <thread> + per-dial `done` 原子
    // + join_prewarm() 中的 10 ms 轮询循环：池 已经执行
    // 请求停止，在条件变量上等待有界宽限期，超时后放弃等待。
    // 无需轮询，并且其放弃是安全的，因为 工作线程 co-owns
    // 是 池 的状态。一个 工作线程 不是表盘上的上限 - 每个
    // 表盘都是独立放置的，因为表盘可以楔入阻塞的
    // getaddrinfo() 并且不得占用共享的 工作线程。
    maya::pool                      prewarm_pool{/*max_workers=*/1};
};

Client::Client() : Client(Config{}) {}

Client::Client(Config cfg) : impl_(std::make_unique<Impl>()) {
    if (const char* e = util::env::get_or_null<util::env::Var::Insecure>();
        e && *e == '1')
        cfg.insecure = true;
    impl_->cfg = std::move(cfg);
    ensure_net_init();
    // TLS上下文（其中解析是整个系统CA束，约15毫秒）是在wrap_client
    // 第一次使用时在其自己的once-guard下构建的。在这里构建它会在每次
    // 发布时都会如此，甚至是 `agentty --version`。
}

// 无显式连接：不能将`unlocked`交给析构函数。
// 预热 池 自己的析构函数执行相同的有界 stop-and-wait。
// 默认客户端被泄露且从未被销毁； main() 加入了一个能力。
Client::~Client() = default;

// 助手：从池获取连接或重新拨号。
static std::expected<std::unique_ptr<Connection>, HttpError>
acquire_or_dial(Pool& pool, const Endpoint& ep, Timeouts tos, CancelTokenPtr cancel) {
    if (auto c = pool.acquire(ep)) {
        AGT_LOG(Perf, Debug, "http.acquire", "pool=hit host={}", ep.host);
        return c;
    }
    AGT_LOG(Perf, Debug, "http.acquire", "pool=miss host={} dial=fresh", ep.host);
    return dial_new(ep, tos, std::move(cancel));
}

// -----------------------------------------------------------------------
// 透明的重试政策。
// -----------------------------------------------------------------------
// 池的三-stage活动检查（TTL、nghttp2、套接字查看）在获取
// 时捕获大多数过期条目，但是连接仍然可能在"looked alive"和
// "first SSL_write"之间的窗口中终止——代理通常以奇数
// 间隔杀死空闲的h2 conns，并且TCP FIN可以在偷看。
// 表面上认为UI中的`http: h2: socket hangup`是
// user-hostile；正确的行为是悄悄地 re-dial 并重试。
//
// 当呼叫者观察到*什么*：没有响应：状态、没有on_headers 回调、
// 没有on_chunk 字节时，我们仅重试。一旦其中任何一个被触发，
// 数据流 在语义上就会被提交——重试 将复制 SSE 事件
// 或为 状态更新函数 提供第二套工具 调用。
//
// 3次尝试×小线性退避给出了病理情况下约300ms额外
// 延迟的硬上限。 过期-pool 尝试一次就够了；
// 两人经历了短暂的 DNS/TLS 光点；三是松弛。
constexpr int kMaxAttempts = 3;

static bool is_cancelled(const CancelTokenPtr& c) {
    return c && c->is_cancelled();
}

// 尝试之间的退避。尝试 0 → 不等待（常见 过期-pool 情况的最快路径）。
// 尝试 N → 在 [0, base·2^(N-1)] 中随机等待，base =
// 100 ms，i.e。 完整 JITTER 在指数增长的窗口中（尝试 1
// → [0,100] ms，尝试 2 → [0,200] ms）。抖动不是表面的：
// agentty扇出并行子代理数据流并且多个实例击中相同的边缘，
// 因此固定的退避使得每个重试器re-dial步调一致，
// 并且re-synchronise正是触发重试 (惊群)的overload/RST。
// 随机化每个服务员的延迟可以使他们去相关。窗口保持较小（kMaxAttempts
// =3 时≤ 200 毫秒），因此最坏的-case 增加的延迟是有限的。
// 可取消：如果令牌在等待期间跳闸，则返回 false，
// 因此调用者无需再次尝试即可退出。
static bool backoff_sleep(int attempt, const CancelTokenPtr& cancel) {
    if (attempt <= 0) return true;
    // 指数增长上限：100·2^(attempt-1) ms，有上限，
    // 因此未来达到 kMaxAttempts 不会超出延迟预算。
    constexpr long kBaseMs = 100;
    constexpr long kCapMs  = 2'000;
    long ceiling = kBaseMs << (attempt - 1);   // 100, 200, 400, ...
    if (ceiling > kCapMs || ceiling <= 0) ceiling = kCapMs;
    // 全抖动：在[0，上限]中均匀。 thread_local RNG 因此
    // 并行 stream/send 循环不会在共享生成器或全局 锁 上发生竞争。
    thread_local std::mt19937_64 rng{[] {
        std::random_device rd;
        return (static_cast<std::uint64_t>(rd()) << 32) ^ rd()
             ^ static_cast<std::uint64_t>(
                 std::chrono::steady_clock::now().time_since_epoch().count());
    }()};
    std::uniform_int_distribution<long> dist(0, ceiling);
    const auto budget = std::chrono::milliseconds(dist(rng));
    // 请求为已取消时立即唤醒；没有令牌意味着简单的等待。
    // backoff_for，而不是delay_for：令牌刷新在该请求
    // 中保持其单个-flight通道，并且delay_for在任何锁下中止
    // （缓慢刷新期间的^C因SIGABRT而崩溃）。暂停时间已被限制。
    return !maya::backoff_for(cancel ? cancel->token() : std::stop_token{}, budget);
}

HttpResult
Client::send(const Request& req, Timeouts tos, CancelTokenPtr cancel) {
    Endpoint ep{ req.host, req.port, req.dial_host, req.dial_port, req.ssrf_guard };
    HttpError last_err = HttpError::unknown("send: no attempts made");

    // 明文本地服务器(Ollama/llama.cpp)-无TLS，无h2，无池。
    if (req.plaintext) {
        auto r = plain_unary_send(ep, req, tos, cancel.get());
        if (!r)
            AGT_LOG(Net, Warn, "http.local_failed", "host={}:{} err={}",
                    ep.host, ep.port, r.error().render());
        return r;
    }

    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        if (is_cancelled(cancel)) return std::unexpected(HttpError::cancelled());
        if (!backoff_sleep(attempt, cancel))
            return std::unexpected(HttpError::cancelled());

        // 第一次尝试从池拉出；随后的尝试会带来新鲜感——如果
        // 池 曾经给过我们一具尸体，它可能还会给我们另一具尸体。
        auto conn_or = (attempt == 0)
            ? acquire_or_dial(impl_->pool, ep, tos, cancel)
            : dial_new(ep, tos, cancel);
        if (!conn_or) {
            last_err = std::move(conn_or).error();
            // 连接失败是最常见的 "agentty is broken"
            // 报告（端口错误、本地服务器失效、代理、DNS、
            // TLS）。每次尝试都会记录它所尝试的端点，
            // 因此日志会命名主机，而不是用户猜测它。
            AGT_LOG(Net, Warn, "http.connect_failed",
                    "host={}:{} attempt={}/{} err={}",
                    ep.host, ep.port, attempt + 1, kMaxAttempts,
                    last_err.render());
            // ALPN 降级不会在 重试 上自行修复 — 立即
            // 保释到低于 回退 的 h1，而不是消耗 重试 预算。
            if (is_alpn_h2_failure(last_err)) break;
            continue;
        }
        auto conn = std::move(*conn_or);

        StreamCtx sctx{};
        auto ok = conn->run(req, sctx, tos, cancel);
        if (ok) {
            impl_->pool.release(std::move(conn));
            content_decode_body(sctx.headers, sctx.buffered_body);
            return Response{ sctx.status, std::move(sctx.headers),
                             std::move(sctx.buffered_body) };
        }
        last_err = std::move(ok).error();

        // 如果服务器实际产生：状态，则请求到达应用程序
        // 级别——失败是语义上的（500，超时 mid-body），
        // 而不是传输层和重试，POST将重复副作用。
        if (sctx.status != 0) break;
        if (is_cancelled(cancel)) break;
    }

    // h2不可达，因为对端仅通过此处缓冲的h1路径提供HTTP/1.1.一元请求重试；
    // Client::stream 使用下面的增量
    // h1 TLS 路径。
    if (is_alpn_h2_failure(last_err) && !is_cancelled(cancel)) {
        if (auto h1 = h1_unary_send(ep, req, tos, cancel.get()); h1)
            return std::move(*h1);
        else
            last_err = std::move(h1).error();
    }

    return std::unexpected(std::move(last_err));
}

HttpStreamResult
Client::stream(const Request& req, StreamHandler handler, Timeouts tos,
               CancelTokenPtr cancel) {
    Endpoint ep{ req.host, req.port, req.dial_host, req.dial_port, req.ssrf_guard };
    std::optional<clock_t_::time_point> operation_deadline;
    if (tos.total.count() > 0)
        operation_deadline = clock_t_::now() + tos.total;
    const auto remaining_timeouts = [&]() -> std::optional<Timeouts> {
        Timeouts current = tos;
        if (!operation_deadline) return current;
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            *operation_deadline - clock_t_::now());
        if (left.count() <= 0) return std::nullopt;
        current.total = left;
        current.connect = std::min(current.connect, left);
        current.absolute_deadline = operation_deadline;
        return current;
    };

    // 明文本地服务器(Ollama/llama.cpp)-原始套接字上的普通HTTP/1.1分块SSE。
    // 没有 TLS，没有 h2，没有连接 池。
    if (req.plaintext) {
        auto plain_tos = remaining_timeouts();
        if (!plain_tos)
            return std::unexpected(HttpError::timeout(
                "stream operation deadline exceeded"));
        bool accepted_post = false;
        auto plain = plain_stream(ep, req, handler, *plain_tos, cancel.get(),
                                  accepted_post);
        if (!plain && accepted_post) {
            plain.error().non_replayable = true;
            plain.error().detail +=
                " (server accepted request; not replayed automatically)";
        }
        // 本地端点采用这个单独的 no-retry 路径，因此 重试 循环的日志
        // 记录永远不会看到它们 — 然而
        // Ollama/llama.cpp 是否确实在该端口运行？
        // 是所有故障中最常见的本地故障。记录尝试过的端点。
        if (!plain)
            AGT_LOG(Net, Warn, "http.local_failed", "host={}:{} err={}",
                    ep.host, ep.port, plain.error().render());
        return plain;
    }

    HttpError last_err = HttpError::unknown("stream: no attempts made");
    // 在多次尝试中保持不变，以便合成的 on_headers（
    // 如果我们从未得到真正的）携带*最后一次尝试收集的任何元数据。
    int     last_status  = 0;
    Headers last_headers;
    bool    committed    = false;   // 一旦真正的SSE DATA到达呼叫者，则为真

    // 重试预算被分割：在传送任何数据之前RSTs的“重用”池化
    // 连接绝大多数是过期-pool尸体（边缘半-closed在闲置时它，
    // 并且我们的acquire-time活跃度抢占了FIN/GOAWAY）。
    // 那些 re-dials 必须将 不 计入较小的 传输层 预算，
    // 否则汇集死连接的会话将在同一个新拨号上烧毁所有 3 次尝试，
    // 并无论如何都会显示错误。我们允许专门针对
    // "reused conn died before data"（复用连接在返回数据前已断开。）
    // 情况进行额外的尝试，但有上限，
    // 因此真正的死端点仍然会收敛而不是循环。
    constexpr int kMaxStalePoolRedials = 2;
    int stale_pool_redials = 0;

    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        if (is_cancelled(cancel)) { last_err = HttpError::cancelled(); break; }
        auto attempt_tos = remaining_timeouts();
        if (!attempt_tos) {
            last_err = HttpError::timeout("stream operation deadline exceeded");
            break;
        }
        if (!backoff_sleep(attempt, cancel)) { last_err = HttpError::cancelled(); break; }
        attempt_tos = remaining_timeouts();
        if (!attempt_tos) {
            last_err = HttpError::timeout("stream operation deadline exceeded");
            break;
        }

        const bool from_pool = (attempt == 0 && stale_pool_redials == 0);
        auto conn_or = from_pool
            ? acquire_or_dial(impl_->pool, ep, *attempt_tos, cancel)
            : dial_new(ep, *attempt_tos, cancel);
        if (!conn_or) {
            last_err = std::move(conn_or).error();
            // ALPN降级在重试期间是稳定的。 SSE 在 HTTP/1.1
            // 上工作，因此进入下面的 TLS h1 流式传输 路径。
            if (is_alpn_h2_failure(last_err)) break;
            continue;
        }
        auto conn = std::move(*conn_or);
        const bool was_reused = from_pool && conn->was_pooled();

        StreamCtx sctx{};
        sctx.handler = &handler;
        auto ok = conn->run(req, sctx, *attempt_tos, cancel);
        const bool accepted_non_idempotent =
            req.method == HttpMethod::Post
            && sctx.headers_delivered
            && sctx.status >= 200 && sctx.status < 300;
        if (sctx.data_delivered || accepted_non_idempotent) committed = true;
        last_status  = sctx.status;
        last_headers = std::move(sctx.headers);
        if (ok) {
            impl_->pool.release(std::move(conn));
            return {};
        }
        last_err = std::move(ok).error();
        if (accepted_non_idempotent) {
            last_err.non_replayable = true;
            last_err.detail += " (server accepted request; not replayed automatically)";
            // 用户-visible "socket hangup … not replayed"横幅。
            // 记录 HOW LONG 连接在终止前处于空闲状态，
            // 以及是否来自 池：在固定空闲年龄处集群的挂断是
            // proxy/edge 空闲 超时（可通过将 池
            // TTL 降低到其下方来修复），而随机年龄则是真正的
            // 网络丢失。仅从横幅上看不到这种区别。
            AGT_LOG(Net, Warn, "http.stream_hangup",
                    "host={}:{} reused={} idle_ms={} status={} err={}",
                    ep.host, ep.port, was_reused ? 1 : 0,
                    was_reused ? conn->idle_ms() : 0,
                    sctx.status, last_err.render());
        }

        // 一旦DATA到达，或者2xx响应证明非-idempotent
        // POST被接受，在此重放可能重复模型工作。
        // 让 provider/runtime 对中断进行分类。
        if (committed) break;
        if (is_cancelled(cancel)) break;

        // 透明的 过期-pool 恢复：在传送数据之前死亡的重用连接（
        // RST_STREAM / GOAWAY / 套接字挂断 / 对端
        // -closed）是一具尸体，acquire-time 活动
        // 检查无法捕获 - FIN/GOAWAY 与我们的 MSG_PEEK 竞争。
        // Re-dial FRESH 不收取 传输层 尝试预算，
        // 因此用户永远不会看到纯粹 池-staleness 工件的横幅。
        // dial_new() 绕过 池，因此重拨会获得有保证的
        // -fresh 套接字。
        if (was_reused && stale_pool_redials < kMaxStalePoolRedials) {
            ++stale_pool_redials;
            --attempt;   // 不消耗传输层-budget槽
        }
    }

    if (is_alpn_h2_failure(last_err) && !is_cancelled(cancel)) {
        if (auto h1_tos = remaining_timeouts()) {
            bool accepted_post = false;
            auto h1 = h1_tls_stream(ep, req, handler, *h1_tos, cancel.get(),
                                    accepted_post);
            if (!h1 && accepted_post) {
                h1.error().non_replayable = true;
                h1.error().detail +=
                    " (server accepted request; not replayed automatically)";
            }
            return h1;
        }
        return std::unexpected(HttpError::timeout(
            "stream operation deadline exceeded before HTTP/1.1 fallback"));
    }

    // 如果我们从未传递数据，则合成 on_headers，以便调用者的合约（
    // "fires at least once"）仍然有效。
    if (!committed && handler.on_headers) {
        handler.on_headers(last_status, last_headers);
    }
    return std::unexpected(std::move(last_err));
}

void Client::prewarm(std::string host, uint16_t port,
                     std::string dial_host, uint16_t dial_port) {
    // AGENTTY_NO_PREWARM完全抑制延迟-hiding拨号。
    // 由测试工具设置：预热是与真实主机的独立后台 TLS 握手，以及一个独立的 二
    // 进制程序，它永远不会 调用 join_prewarm() 让它比 main()
    // 和 竞态 libcrypto/allocator atexit
    // 拆卸（关闭 SIGSEGV）更长。测试不需要延迟时间，所以选择退出。
    if (const char* e = util::env::get_or_null<util::env::Var::NoPrewarm>();
        e && *e == '1')
        return;
    // 预热拨号竞态进程退出：在快速拆卸时(立即管道-stdin EOF)，
    // 当该拨号处于mid-SSL_connect时，main()可以返回，
    // 并且CRT/OpenSSL静态状态在其下被释放——堆损坏。
    // 因此，该作品是OWNED，由池组成，未分离，并带有取消令牌；
    // shutdown join_prewarm() 会触发令牌（
    // 关闭拨号套接字以唤醒 poll()），并且 池 在拆卸之前等待。
    // 吞下错误——机会主义。
    //
    // 不再有`done` 标志：它存在，因此join_prewarm
    // ()的轮询循环可以区分固定拨号和楔形拨号，并且池从其自己的
    // 记帐中回答该问题而无需轮询。
    auto cancel = std::make_shared<CancelToken>();
    impl_->prewarm_pool.post_isolated(
        [](std::stop_token st, PoolRef pool, CancelTokenPtr cancel,
           std::string host, uint16_t port, std::string dial_host, uint16_t dial_port) {
#if !defined(_WIN32)
        // 阻止每个信号，以便SIGWINCH/SIGINT/SIGTERM路由到主线程的处理函数，
        // 而不是在SSL_connect中期传递到这里。在 glibc 上，
        // 这是belt-and-suspenders； on musl
        // -static 在 OpenSSL 的 cert-chain
        // 行走期间建立杂散信号是气隙段错误的候选原因之一。
        sigset_t all;
        sigfillset(&all);
        pthread_sigmask(SIG_BLOCK, &all, nullptr);
#endif
        // 将 池 的停止桥接至应用程序的 取消 令牌，
        // 因此即使从未调用 join_prewarm()，
        // 关闭也会到达拨号的轮询循环 — 这正是本文件顶部记录的独立
        // -binary 情况。
        const std::stop_callback cancel_on_stop(st, [cancel] {
            cancel->cancel();
        });
        if (cancel->is_cancelled() || st.stop_requested()) return;
        Endpoint ep{ std::move(host), port,
                     std::move(dial_host), dial_port };
        // 短的、有限的连接预算。预热是纯粹的延迟-hiding——它
        // 绝不能在退出时楔入进程。默认 10 秒连接 超时，
        // 拨号盘卡在 TCP/TLS 将使 join_prewarm
        // () 在快速管道-EOF 出口上阻塞长达 10 秒；对其进行硬限制，
        // 以便关闭等待立即返回（并且拨号盘的轮询在该窗口内每 200
        // 毫秒循环 re-check 取消 令牌）。
        Timeouts warm_tos{};
        warm_tos.connect = std::chrono::milliseconds{4'000};
        auto r = dial_new(ep, warm_tos, cancel);
        if (r && !cancel->is_cancelled()) pool->release(std::move(*r));
    }, impl_->pool_ref, std::move(cancel), std::move(host), port,
       std::move(dial_host), dial_port);
}

void Client::join_prewarm(const maya::unlocked& cap) noexcept {
    // shutdown()请求在等待之前在每个拨号盘上停止，并且每个拨号
    // 盘将其转换成其取消令牌(上面的stop_callback)，
    // 因此拨号盘在它们的下一个轮询片处中止。
    // 一个调用取代了手-rolled有界等待：池请求在每个刻度盘上停止，
    // 等待条件变量的宽限期，并放弃仍然卡住的任何东西。最后一种情况是
    // 表盘楔入阻塞的 getaddrinfo() 中 -
    // hostile/offline 解析器可能会停滞几秒钟 -
    // 放弃它是安全的，原因与旧的分离相同：default_client
    // () 是故意的 -leaked 单例，因此它的 池 / OpenSSL
    // 静态永远不会被破坏，并且 still-running 表盘
    // 不能 竞态 析构函数。 OS 在进程退出时回收 线程。
    //
    // 旧版本所做的相反：每个-dial `done`原子，
    // 以及一次休眠10毫秒的循环，直到决定是否加入或分离之前
    // 的2秒截止时间。这是一个拼写为轮询的条件变量 -
    // 缓慢退出时每个拨号最多 200 次唤醒，
    // 以及无法注意到拨号早于下一个 10 毫秒滴答完成的关闭。
    (void)impl_->prewarm_pool.shutdown(cap);
}

namespace {
std::atomic<Client*> g_default_client{nullptr};
}  // 命名空间结束。

Client& default_client(Io) {
    // 故意泄露：进程生命周期。  避免破坏 -order 竞态 与
    // maya 的 工作线程 线程（可能在出口处的 -request 中间）。
    static Client* c = [] {
        auto* made = new Client{};
        g_default_client.store(made, std::memory_order_release);
        return made;
    }();
    return *c;
}

Client* default_client_if_made() noexcept {
    return g_default_client.load(std::memory_order_acquire);
}

namespace {
// 解析将`host[:port]` env-var值转换成DialOverride。
// 空/未设置/ ill-formed 值会默默降级为 "no override"
// — 用户可以修复环境变量并重新启动。  在已经恢复的场景中，
// 硬失败会很不友好（air-gapped 主机，无法直接到达
// API）。
DialOverride parse_dial_env(const char* var_name) {
    const char* v = std::getenv(var_name);
    if (!v || !*v) return {};
    std::string_view s{v};
    auto colon = s.find(':');
    if (colon == std::string_view::npos) {
        // 仅主机 → 默认端口 443。
        return DialOverride{ std::string{s}, 443 };
    }
    auto host_sv = s.substr(0, colon);
    auto port_sv = s.substr(colon + 1);
    if (host_sv.empty() || port_sv.empty()) return {};
    // 严格整数解析：port_sv的每个字符必须是数字，
    // 并且结果必须适合uint16_t(≤65535)。
    unsigned long port_val = 0;
    for (char c : port_sv) {
        if (c < '0' || c > '9') return {};
        port_val = port_val * 10 + static_cast<unsigned long>(c - '0');
        if (port_val > 65535) return {};
    }
    if (port_val == 0) return {};
    return DialOverride{ std::string{host_sv},
                         static_cast<uint16_t>(port_val) };
}
} // 命名空间结束。

// 对数字 IP LITERAL 进行导出的、可测试的 SSRF 范围检查。
// 镜像每个 RESOLVED 地址上拨号器使用的匿名-namespace blocked_sockaddr
// ()（两者均由 ssrf_guard_test 固定，因此它们不能漂移）：环回、
// RFC1918 专用、链接-local 包括。 169.254.169.254 元数据、
// CGNAT、ULA、多播和 IPv4-mapped IPv6 re-checked 作为其
// v4 地址。无法关闭任何不可解析的数字 IP 的内容。
bool ssrf_ip_blocked(std::string_view ip_literal) noexcept {
    auto v4_blocked = [](uint32_t h /*主机订购*/) noexcept {
        const uint8_t a = (h >> 24) & 0xFF, b = (h >> 16) & 0xFF;
        if (a == 0)   return true;                        // 0.0.0.0/8
        if (a == 127) return true;                        // 环回
        if (a == 10)  return true;                        // RFC1918
        if (a == 172 && (b & 0xF0) == 16) return true;     // 172.16/12
        if (a == 192 && b == 168) return true;             // 192.168/16
        if (a == 169 && b == 254) return true;             // 链接-local + 元数据
        if (a == 100 && (b & 0xC0) == 64) return true;     // 100.64/10 CGNAT
        if (a >= 224) return true;                         // 多播/保留
        return false;
    };

    std::string s{ip_literal};
    if (s.size() >= 2 && s.front() == '[' && s.back() == ']')
        s = s.substr(1, s.size() - 2);
    if (s.empty()) return true;

    in_addr a4{};
    if (::inet_pton(AF_INET, s.c_str(), &a4) == 1)
        return v4_blocked(ntohl(a4.s_addr));

    in6_addr a6{};
    if (::inet_pton(AF_INET6, s.c_str(), &a6) == 1) {
        const uint8_t* b = a6.s6_addr;
        static const uint8_t mapped[12] = {0,0,0,0, 0,0,0,0, 0,0,0xFF,0xFF};
        if (std::memcmp(b, mapped, 12) == 0) {
            uint32_t h = (uint32_t(b[12]) << 24) | (uint32_t(b[13]) << 16)
                       | (uint32_t(b[14]) << 8) | uint32_t(b[15]);
            return v4_blocked(h);
        }
        if (b[0] == 0xFE && (b[1] & 0xC0) == 0x80) return true;  // fe80::/10
        if ((b[0] & 0xFE) == 0xFC) return true;                  // fc00::/7 ULA
        if (b[0] == 0xFF) return true;                           // ff00::/8
        bool all_zero = true;
        for (int i = 0; i < 15; ++i) if (b[i]) { all_zero = false; break; }
        if (all_zero && (b[15] == 0 || b[15] == 1)) return true; // :: / ::1
        return false;
    }
    return true;   // 不是数字 IP — 关闭失败
}

// 下面的三个覆盖解析，它们的环境变量EXACTLY 一次，
// 变成函数-local静态，并且缓存是进程生命周期的结果。
// 这是故意的，而不是疏忽：改变mid
// -session的拨号目标将在-flight连接重用中默默地重新-route
// （池在逻辑主机上键入，而不是拨号的主机），这比在启动时
// 简单固定的值更难调试。在启动 agentty 之前设置这些；
// 之后更改它们直到重新启动才会出现 副作用。
// 根据 [stmt.dcl]/4，初始化为 线程-safe。
const DialOverride& agentty_api_host_override() {
    static const DialOverride cached =
        parse_dial_env(util::env::name<util::env::Var::ApiHost>().data());
    return cached;
}

const DialOverride& agentty_oauth_host_override() {
    static const DialOverride cached =
        parse_dial_env(util::env::name<util::env::Var::OAuthHost>().data());
    return cached;
}

const DialOverride& agentty_socks_proxy() {
    static const DialOverride cached =
        parse_dial_env(util::env::name<util::env::Var::SocksProxy>().data());
    return cached;
}

} // namespace agentty::http
