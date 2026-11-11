// local_stream_cap_test ——108:本地模式服务
// 器可以 数据流 一份答复,答复时间超过主办日期30次 -minute
// 打开真实的 HTTP 客户端对接一个微调的回转服务器 SSE ,
// 随着时钟缩成秒:主机梯形(一个总盖)将其剪切,局部梯形(没有总
// 盖)让其完成.

#include "agtest.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

#include "agentty/io/http.hpp"
#include "agentty/provider/stream_scaffold.hpp"
#include "agentty/util/io.hpp"

#if defined(_WIN32)
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
#endif

using namespace agentty;
using namespace std::chrono_literals;

namespace {

#if defined(_WIN32)
using socket_t = SOCKET;
constexpr socket_t kInvalid = INVALID_SOCKET;
void close_socket(socket_t s) { if (s != kInvalid) ::closesocket(s); }
#else
using socket_t = int;
constexpr socket_t kInvalid = -1;
void close_socket(socket_t s) { if (s != kInvalid) ::close(s); }
#endif

// 连接: 回答 200 个, 并发送一个符号 `gap`, `n` 时间。
class TrickleServer {
public:
    TrickleServer(int n, std::chrono::milliseconds gap) : n_(n), gap_(gap) {
#if defined(_WIN32)
        WSADATA d{};
        (void)::WSAStartup(MAKEWORD(2, 2), &d);
#endif
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (fd_ == kInvalid
            || ::bind(fd_, reinterpret_cast<const sockaddr*>(&a), sizeof(a)) != 0
            || ::listen(fd_, 4) != 0) {
            close_socket(fd_);
            fd_ = kInvalid;
            return;
        }
#if defined(_WIN32)
        int len = sizeof(a);
#else
        socklen_t len = sizeof(a);
#endif
        (void)::getsockname(fd_, reinterpret_cast<sockaddr*>(&a), &len);
        port_ = ntohs(a.sin_port);
        worker_ = std::thread([this] { serve(); });
    }
    ~TrickleServer() {
        stop_.store(true);
        close_socket(fd_);   // 如果客户端从未来过, 则解锁accept()
        if (worker_.joinable()) worker_.join();
#if defined(_WIN32)
        ::WSACleanup();
#endif
    }
    [[nodiscard]] bool valid() const { return fd_ != kInvalid; }
    [[nodiscard]] std::uint16_t port() const { return port_; }

private:
    void send_all(socket_t c, const std::string& s) {
        std::size_t off = 0;
        while (off < s.size()) {
#if defined(_WIN32)
            const auto r = ::send(c, s.data() + off, static_cast<int>(s.size() - off), 0);
#else
            // 被封顶的客户端在中途挂断 -stream ;这不能 SIGPIPE
            // 整个测试 二进制程序.
            const auto r = ::send(c, s.data() + off, s.size() - off, MSG_NOSIGNAL);
#endif
            if (r <= 0) return;
            off += static_cast<std::size_t>(r);
        }
    }
    void serve() {
        const socket_t c = ::accept(fd_, nullptr, nullptr);
        if (c == kInvalid) return;
        char buf[8192];
        (void)::recv(c, buf, sizeof(buf), 0);   // 请求; 内容无关
        send_all(c, "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n"
                    "Transfer-Encoding: chunked\r\nConnection: close\r\n\r\n");
        auto chunk = [](const std::string& s) {
            char hex[16];
            std::snprintf(hex, sizeof(hex), "%zx", s.size());
            return std::string{hex} + "\r\n" + s + "\r\n";
        };
        for (int i = 0; i < n_; ++i) {
            // 睡在台阶上 这样一个测试 客户已经放弃了
            // 并不会让毁灭者加入整个缺口
            const auto until = std::chrono::steady_clock::now() + gap_;
            while (!stop_.load() && std::chrono::steady_clock::now() < until)
                std::this_thread::sleep_for(20ms);
            if (stop_.load()) break;
            send_all(c, chunk("data: tok\n\n"));
        }
        if (!stop_.load()) send_all(c, chunk("data: [DONE]\n\n") + "0\r\n\r\n");
        close_socket(c);
    }

    int n_;
    std::chrono::milliseconds gap_;
    socket_t fd_ = kInvalid;
    std::uint16_t port_ = 0;
    std::thread worker_;
    std::atomic<bool> stop_{false};
};

struct Outcome { bool ok; int tokens; std::string err; };

Outcome run(std::uint16_t port, http::Timeouts tos) {
    http::Request req;
    req.method    = http::HttpMethod::Post;
    req.host      = "127.0.0.1";
    req.port      = port;
    req.path      = "/v1/chat/completions";
    req.plaintext = true;
    req.body      = "{}";
    int tokens = 0;
    http::StreamHandler h;
    h.on_headers = [](int, const http::Headers&) {};
    h.on_chunk   = [&](std::string_view s) {
        for (auto p = s.find("tok"); p != std::string_view::npos; p = s.find("tok", p + 1))
            ++tokens;
        return true;
    };
    auto r = http::default_client(IoAccess::grant()).stream(req, std::move(h), tos, {});
    return {bool(r), tokens, r ? std::string{} : r.error().render()};
}

// 真正的梯子,每段时间从分钟缩到十分之一秒,
// 所以测试在一秒左右进行。闲置的时间远远超过差距。
http::Timeouts shrink(http::Timeouts t) {
    if (t.total.count() > 0) t.total = 600ms;   // "30 min" - > 0.6 s
    t.idle = 2s;
    t.ping = 0ms;
    return t;
}

} // 命名空间结束。

TEST_CASE("local stream: no total cap, so a long reply finishes (#108)") {
    // 10个符,0.1个差:~1 s of 流式传输,通过缩窄的0.6 s 盖.
    TrickleServer s(10, 100ms);
    REQUIRE(s.valid(), "loopback server starts");
    const auto out = run(s.port(), shrink(provider::local_stream_timeouts()));
    CHECK(out.ok);
    CHECK(out.tokens == 10);
}

TEST_CASE("local stream: the hosted ladder's cap would have cut it") {
    // 控制器: 相同的服务器, 托管的梯形。 这就是 # 108 击中的。
    TrickleServer s(10, 100ms);
    REQUIRE(s.valid(), "loopback server starts");
    const auto out = run(s.port(), shrink(provider::stream_timeouts()));
    CHECK(!out.ok);
    CHECK(out.err.find("deadline exceeded") != std::string::npos);
    CHECK(out.tokens < 10);
}

TEST_CASE("local stream: a silent server is still caught by idle") {
    // 任何总的盖子都不得意味着永远挂起。 服务器会静静地运行 5 s;
    // 2 s 闲置的守护者必须结束它, 远在服务器会关闭之前。
    TrickleServer s(1, 5000ms);
    REQUIRE(s.valid(), "loopback server starts");
    const auto t0 = std::chrono::steady_clock::now();
    const auto out = run(s.port(), shrink(provider::local_stream_timeouts()));
    const auto took = std::chrono::steady_clock::now() - t0;
    CHECK(!out.ok);
    CHECK(out.err.find("idle") != std::string::npos);
    CHECK(took < 3500ms);
}
