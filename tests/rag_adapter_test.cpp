// rag_adapter_test — 锁 编号 agentty↔rag-cpp 适配器合同.
//
// agentty 其检索引擎是外部 rag-cpp 库 (rag::Engine )),
// 通过契约驱动. agentty::rag::Retriever 边界
// include/agentty/rag/rag_adapter.hpp.
// 测试驱动器 真实 边界结束至结束,完全结束 OFFLINE : 无法获取 Ollama
// 终点被探测一次,适配器运行 BM25 无需重复网络 超时.
//
// 将其余的属性固定在身上 agentty 取决于:
//   1. 索引文件文件夹,相关查询返回的段落排序
//      其 人 名 `source` 谓 "docs" 以及其国家 `path` 是文件(
//      证明在尿液圆中幸存) -trip).
//   2. 尖端查询的顶端段落是实际包含
//      答案( 排名不是随机的 )。
//   3. 空知识集报告"no knowledge configured"错误
//      而不是扔垃圾或运回垃圾
//   4. warm()/retrieve() 安全无虞 调用 重复( 次数) 幂等 )),"("新指数").
//   5. retrieve_code () 索引 cwd 源码树 找到一个 符号.

#include "agtest.hpp"

#include "agentty/rag/rag_adapter.hpp"
#include "agentty/tool/util/fs_helpers.hpp"   // workspace_root / set_workspace_root 数据

#include <chrono>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#if defined(_WIN32)
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <sys/time.h>
#  include <unistd.h>
#endif

namespace fs = std::filesystem;


static void write_file(const fs::path& p, const std::string& body) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << body;
}

namespace {

using json = nlohmann::json;
#if defined(_WIN32)
using socket_t = SOCKET;
constexpr socket_t kInvalidSocket = INVALID_SOCKET;
#else
using socket_t = int;
constexpr socket_t kInvalidSocket = -1;
#endif

void close_socket(socket_t s) noexcept {
    if (s == kInvalidSocket) return;
#if defined(_WIN32)
    ::closesocket(s);
#else
    ::close(s);
#endif
}

class LoopbackEmbeddingServer {
public:
    // `delay`:每个请求需要多长时间,因此测试可以
    // 看到请求重叠(以及同时可能的其他什么).
    explicit LoopbackEmbeddingServer(std::chrono::milliseconds delay = std::chrono::milliseconds{0})
        : delay_(delay) {
#if defined(_WIN32)
        WSADATA data{};
        if (::WSAStartup(MAKEWORD(2, 2), &data) != 0) return;
        winsock_started_ = true;
#endif
        listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listener_ == kInvalidSocket) return;
        int one = 1;
        (void)::setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR,
                           reinterpret_cast<const char*>(&one), sizeof(one));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (::bind(listener_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0
            || ::listen(listener_, 16) != 0) {
            close_socket(listener_);
            listener_ = kInvalidSocket;
            return;
        }
#if defined(_WIN32)
        int len = sizeof(addr);
#else
        socklen_t len = sizeof(addr);
#endif
        if (::getsockname(listener_, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
            close_socket(listener_);
            listener_ = kInvalidSocket;
            return;
        }
        port_ = ntohs(addr.sin_port);
        worker_ = std::thread([this] { serve(); });
    }

    ~LoopbackEmbeddingServer() {
        stop_.store(true, std::memory_order_release);
        if (listener_ != kInvalidSocket) {
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            addr.sin_port = htons(port_);
            socket_t wake = ::socket(AF_INET, SOCK_STREAM, 0);
            if (wake != kInvalidSocket) {
                (void)::connect(wake, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
                close_socket(wake);
            }
        }
        if (worker_.joinable()) worker_.join();
        // 每个连接都以自己的线程运行;等待飞行中的那些.
        for (auto& t : handlers_)
            if (t.joinable()) t.join();
        close_socket(listener_);
#if defined(_WIN32)
        if (winsock_started_) ::WSACleanup();
#endif
    }

    LoopbackEmbeddingServer(const LoopbackEmbeddingServer&) = delete;
    LoopbackEmbeddingServer& operator=(const LoopbackEmbeddingServer&) = delete;

    [[nodiscard]] bool valid() const noexcept { return listener_ != kInvalidSocket; }
    [[nodiscard]] std::uint16_t port() const noexcept { return port_; }
    [[nodiscard]] int requests() const noexcept { return requests_.load(std::memory_order_acquire); }
    // 多数请求在同一个时刻得到答复。
    [[nodiscard]] int peak_in_flight() const noexcept { return peak_.load(std::memory_order_acquire); }
    // Ollama-style 模式检查: 设定时, 只检查 `only_model`
    // 答案和其他模型在任何请求之前获得一个 404. 设定。
    void serve_only(std::string m) { only_model_ = std::move(m); }
    [[nodiscard]] int refused() const noexcept { return refused_.load(std::memory_order_acquire); }

private:
    std::string response_for(const std::string& body) {
        std::size_t count = 1;
        const auto request = json::parse(body, nullptr, false);
        if (request.is_object() && request.contains("input") && request["input"].is_array())
            count = request["input"].size();
        const std::string model = request.is_object() ? request.value("model", std::string{}) : std::string{};
        if (!only_model_.empty() && model != only_model_) {
            refused_.fetch_add(1, std::memory_order_acq_rel);
            return "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
        }
        json data = json::array();
        json matrix = json::array();
        for (std::size_t i = 0; i < count; ++i) {
            data.push_back({{"object", "embedding"}, {"index", i},
                            {"embedding", {1.0, 0.0, 0.0, 0.0,
                                             0.0, 0.0, 0.0, 0.0}}});
            matrix.push_back({1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0});
        }
        // 两条线: OpenAI 读取数据[], Ollama 读取嵌入。
        const std::string payload = json{{"object", "list"}, {"data", std::move(data)},
                                         {"embeddings", std::move(matrix)}}.dump();
        return "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n"
               "Content-Length: " + std::to_string(payload.size()) + "\r\n\r\n" + payload;
    }

    static bool send_all(socket_t fd, const std::string& bytes) {
        std::size_t sent = 0;
        while (sent < bytes.size()) {
            const auto left = bytes.size() - sent;
#if defined(_WIN32)
            const int n = ::send(fd, bytes.data() + sent, static_cast<int>(left), 0);
#else
            const auto n = ::send(fd, bytes.data() + sent, left, 0);
#endif
            if (n <= 0) return false;
            sent += static_cast<std::size_t>(n);
        }
        return true;
    }

    void handle(socket_t fd) {
        std::string request;
        char buf[4096];
        std::size_t header_end = std::string::npos;
        std::size_t content_length = 0;
        for (;;) {
#if defined(_WIN32)
            const int n = ::recv(fd, buf, sizeof(buf), 0);
#else
            const auto n = ::recv(fd, buf, sizeof(buf), 0);
#endif
            if (n <= 0) return;
            request.append(buf, static_cast<std::size_t>(n));
            if (header_end == std::string::npos) {
                header_end = request.find("\r\n\r\n");
                if (header_end != std::string::npos) {
                    // 头文件 名称为例 -insensitive (agentty 发送小写).
                    std::string headers = request.substr(0, header_end);
                    for (auto& ch : headers) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                    const auto marker = headers.find("content-length:");
                    if (marker != std::string::npos)
                        content_length = std::strtoull(headers.c_str() + marker + 15, nullptr, 10);
                }
            }
            if (header_end != std::string::npos
                && request.size() >= header_end + 4 + content_length)
                break;
        }
        requests_.fetch_add(1, std::memory_order_release);
        const int now = in_flight_.fetch_add(1, std::memory_order_acq_rel) + 1;
        int peak = peak_.load(std::memory_order_acquire);
        while (now > peak && !peak_.compare_exchange_weak(peak, now, std::memory_order_acq_rel)) {}
        if (delay_.count() > 0) std::this_thread::sleep_for(delay_);
        const auto body = request.substr(header_end + 4, content_length);
        const auto response = response_for(body);
        (void)send_all(fd, response);
        in_flight_.fetch_sub(1, std::memory_order_acq_rel);
    }

    void serve() {
        while (!stop_.load(std::memory_order_acquire)) {
#if defined(_WIN32)
            const socket_t client = ::accept(listener_, nullptr, nullptr);
#else
            const socket_t client = ::accept(listener_, nullptr, nullptr);
#endif
            if (client == kInvalidSocket) {
                if (stop_.load(std::memory_order_acquire)) break;
                continue;
            }
            // 连接且从未发送(或保存)的客户端 -alive
            // 套接字客户机忘了) 切勿绕过此选项 线程 :破坏者加入,
            // 一个卡住的加入会吊死cest而不是失败.
#if defined(_WIN32)
            const DWORD ms = 5000;
            (void)::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
                               reinterpret_cast<const char*>(&ms), sizeof(ms));
#else
            timeval tv{5, 0};
            (void)::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
            if (stop_.load(std::memory_order_acquire)) {
                close_socket(client);
                break;
            }
            // 一个真正的嵌入服务器的回答并排请求;这个也一样,
            // 这样一个测试就可以看到客户端的货币.
            handlers_.emplace_back([this, client] {
                handle(client);
                close_socket(client);
            });
        }
    }

    socket_t listener_ = kInvalidSocket;
    std::uint16_t port_ = 0;
    std::thread worker_;
    std::vector<std::thread> handlers_;   // 被工人摸过,然后加入
    std::atomic<bool> stop_{false};
    std::atomic<int> requests_{0};
    std::atomic<int> in_flight_{0};
    std::atomic<int> peak_{0};
    std::chrono::milliseconds delay_{0};
    std::string only_model_;
    std::atomic<int> refused_{0};
#if defined(_WIN32)
    bool winsock_started_ = false;
#endif
};

class EnvironmentRestore {
public:
    explicit EnvironmentRestore(std::initializer_list<const char*> names) {
        for (const char* name : names) {
            const char* value = std::getenv(name);
            saved_.emplace_back(name, value ? std::optional<std::string>{value} : std::nullopt);
        }
    }

    ~EnvironmentRestore() {
        for (const auto& [name, value] : saved_) {
#if defined(_WIN32)
            _putenv_s(name.c_str(), value ? value->c_str() : "");
#else
            if (value) ::setenv(name.c_str(), value->c_str(), 1);
            else ::unsetenv(name.c_str());
#endif
        }
    }

    EnvironmentRestore(const EnvironmentRestore&) = delete;
    EnvironmentRestore& operator=(const EnvironmentRestore&) = delete;

private:
    std::vector<std::pair<std::string, std::optional<std::string>>> saved_;
};

void set_test_env(const char* name, const std::string& value) {
#if defined(_WIN32)
    _putenv_s(name, value.c_str());
#else
    ::setenv(name, value.c_str(), 1);
#endif
}

}  // 命名空间结束。

TEST_CASE("rag auto embedder prefers qwen3-embedding when ollama has it") {
    ScopedEnvSandbox sandbox;
    EnvironmentRestore env({"AGENTTY_DOCS_DIR", "AGENTTY_OLLAMA_HOST", "AGENTTY_EMBED_BACKEND",
                            "AGENTTY_EMBED_ENDPOINT", "AGENTTY_EMBED_MODEL", "AGENTTY_RAG_PERSIST"});
    auto status_with = [&](const std::string& served) {
        LoopbackEmbeddingServer server;
        REQUIRE(server.valid(), "loopback embedding server starts");
        server.serve_only(served);
        set_test_env("AGENTTY_EMBED_BACKEND", "auto");
        set_test_env("AGENTTY_OLLAMA_HOST", "127.0.0.1:" + std::to_string(server.port()));
        set_test_env("AGENTTY_RAG_PERSIST", "0");
        agentty::rag::Retriever r;
        return r.embed_status();
    };
    const auto with_qwen = status_with("qwen3-embedding:0.6b");
    check(with_qwen.state == agentty::rag::Retriever::EmbedStatus::State::Ready
              && with_qwen.describe.find("qwen3-embedding") != std::string::npos,
          "auto picks qwen3-embedding when it's pulled");
    const auto without = status_with("nomic-embed-text");
    check(without.state == agentty::rag::Retriever::EmbedStatus::State::Ready
              && without.describe.find("nomic-embed-text") != std::string::npos,
          "auto falls back to nomic when qwen3 isn't there");
    // 用户名的模型被使用到现在,从未互换过。
    set_test_env("AGENTTY_EMBED_MODEL", "nomic-embed-text");
    LoopbackEmbeddingServer server;
    REQUIRE(server.valid(), "loopback embedding server starts");
    set_test_env("AGENTTY_OLLAMA_HOST", "127.0.0.1:" + std::to_string(server.port()));
    set_test_env("AGENTTY_EMBED_BACKEND", "ollama");
    agentty::rag::Retriever r;
    check(r.embed_status().describe.find("nomic-embed-text") != std::string::npos,
          "an explicit backend keeps its model");
}

TEST_CASE("rag cold retrieval serializes fan-out under the index lock") {
    ScopedEnvSandbox sandbox;
    EnvironmentRestore env({
        "AGENTTY_DOCS_DIR", "AGENTTY_OLLAMA_HOST", "AGENTTY_EMBED_BACKEND",
        "AGENTTY_EMBED_ENDPOINT", "AGENTTY_EMBED_MODEL", "AGENTTY_EMBED_PATH",
        "AGENTTY_EMBED_API_KEY", "AGENTTY_RAG_SKILLS", "AGENTTY_RAG_MEMORY",
        "AGENTTY_RAG_MCP", "AGENTTY_RAG_CONTEXTUAL", "AGENTTY_RAG_MMR",
        "AGENTTY_RAG_MMR_LAMBDA", "AGENTTY_RAG_STITCH", "AGENTTY_RAG_DEDUP",
        "AGENTTY_RAG_DEDUP_THRESHOLD", "AGENTTY_RAG_AUTOCUT",
        "AGENTTY_RAG_AUTOCUT_SENSITIVITY", "AGENTTY_RAG_TRACE", "AGENTTY_RAG_PRF",
        "AGENTTY_RAG_CORRECT", "AGENTTY_RAG_GRAPH", "AGENTTY_RAG_EXPAND",
        "AGENTTY_RAG_HYDE", "AGENTTY_RAG_PERSIST", "AGENTTY_RAG_LEARN",
        "AGENTTY_RAG_ADAPTIVE", "AGENTTY_RAG_PROACTIVE", "AGENTTY_RAG_FUSION"});
    // 150 ms a request: 足够长的时间,
    // 当批次同时运行时,请求明显重叠,一个读者可以在飞行时进入;
    // 短的时间,可以让测试保持在一秒或两秒内.
    LoopbackEmbeddingServer server{std::chrono::milliseconds{150}};
    REQUIRE(server.valid(), "loopback embedding server starts");

    TempDir tmp{"rag_fanout"};
    fs::create_directories(tmp.path() / ".git");
    const auto docs = tmp.path() / "docs";
    for (int i = 0; i < 96; ++i) {
        write_file(docs / ("doc" + std::to_string(i) + ".md"),
                   "# Fan-out fixture " + std::to_string(i) + "\n\n"
                   "This document exercises the cold embedding path while the "
                   "RAG index lock is held. fixture-token-" + std::to_string(i) + "\n");
    }
    // 阴性 工作区 root 是进程 -global : 将两者都
    // 放回所有出路,之前 TempDir 删除他们指向的目录,
    // 或者在此的下一个测试 二进制程序 从已删除的目录中运行。
    struct RestoreWorkspace {
        fs::path cwd = fs::current_path();
        fs::path ws  = ::agentty::tools::util::workspace_root();
        ~RestoreWorkspace() {
            std::error_code e;
            fs::current_path(cwd, e);
            ::agentty::tools::util::set_workspace_root(::agentty::IoAccess::grant(), ws);
        }
    };
    // 于 `tmp` 所以它首先在出门时运行。
    RestoreWorkspace restore;
    std::error_code ec;
    fs::current_path(tmp.path(), ec);
    REQUIRE(!ec, "temporary project becomes the current directory");
    ::agentty::tools::util::set_workspace_root(::agentty::IoAccess::grant(), tmp.path());

    set_test_env("AGENTTY_DOCS_DIR", docs.string());
    set_test_env("AGENTTY_EMBED_BACKEND", "openai");
    set_test_env("AGENTTY_EMBED_ENDPOINT",
                 "http://127.0.0.1:" + std::to_string(server.port()));
    set_test_env("AGENTTY_EMBED_MODEL", "issue-112-fixture");
    set_test_env("AGENTTY_EMBED_PATH", "/v1/embeddings");
    set_test_env("AGENTTY_EMBED_API_KEY", "test-key");
    for (const char* name : {"AGENTTY_RAG_SKILLS", "AGENTTY_RAG_MEMORY",
                             "AGENTTY_RAG_MCP", "AGENTTY_RAG_CONTEXTUAL",
                             "AGENTTY_RAG_MMR", "AGENTTY_RAG_STITCH",
                             "AGENTTY_RAG_DEDUP", "AGENTTY_RAG_AUTOCUT",
                             "AGENTTY_RAG_TRACE", "AGENTTY_RAG_PRF",
                             "AGENTTY_RAG_CORRECT", "AGENTTY_RAG_GRAPH",
                             "AGENTTY_RAG_EXPAND", "AGENTTY_RAG_HYDE",
                             "AGENTTY_RAG_LEARN", "AGENTTY_RAG_ADAPTIVE",
                             "AGENTTY_RAG_PROACTIVE"})
        set_test_env(name, "0");
    set_test_env("AGENTTY_RAG_PERSIST", "0");

    agentty::rag::Retriever retriever;
    // 寒冷的回流中,又来了 线程 一直问一个 锁-free 问题,
    // 读取索引(英文) UI '状态行这样做的每
    // 一个 帧。在“嵌入”下完成嵌入。
    // 锁 每一个都等着离开整个通道
    std::atomic<bool> done{false};
    std::atomic<int>  answered_during{0};
    std::thread prober([&] {
        while (!done.load(std::memory_order_acquire)) {
            if (server.requests() >= 2) {
                // snapshot_config () 取索引 锁 以
                // 一个经文为首,所以它只在没有通过的情况下才回答。 锁.
                const auto t0 = std::chrono::steady_clock::now();
                (void)retriever.snapshot_config();
                if (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds{100})
                    answered_during.fetch_add(1, std::memory_order_relaxed);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
    });
    const auto result = retriever.retrieve("fixture token cold retrieval", 5);
    done.store(true, std::memory_order_release);
    prober.join();

    check(result.error.empty(), "cold retrieval completes without a lock capability abort");
    check(!result.passages.empty(), "cold retrieval returns passages");
    check(server.requests() >= 4,
          "cold retrieval probes and embeds the corpus in multiple requests");
    check(server.peak_in_flight() >= 2,
          "embedding batches run side by side, not one at a time under the lock");
    check(answered_during.load() > 0,
          "the index stays readable while the corpus is being embedded");

    // 温暖:没有新内容可以嵌入,所以最多只能有一个查询向量.
    const int before = server.requests();
    const auto again = retriever.retrieve("fixture token cold retrieval", 5);
    check(again.error.empty() && !again.passages.empty(), "a warm retrieval answers");
    check(server.requests() - before <= 1, "a warm retrieval embeds nothing but the query");

    // 切换到另一个嵌入器(同型号名称,
    // 另一个服务器):旧向量不能被重复使用,
    // 索引重建在新的上,整个冷路运行时不接触旧服务器.
    LoopbackEmbeddingServer other{std::chrono::milliseconds{20}};
    REQUIRE(other.valid(), "second embedding server starts");
    auto cfg = retriever.snapshot_config();
    cfg.embed.host = "127.0.0.1";
    cfg.embed.port = other.port();
    retriever.apply_config(cfg);
    const int old_before = server.requests();
    const auto switched = retriever.retrieve("fixture token cold retrieval", 5);
    check(switched.error.empty() && !switched.passages.empty(), "retrieval answers after switching embedder");
    check(other.requests() >= 4, "the new embedder re-embeds the corpus");
    check(server.requests() == old_before, "the old embedder is not asked again");
}

// 已设定索引名称 `<stem><8-hex-identity>.ragdb` ,
// 在散列识别 EMBEDDER 矢量空间( 请参见
// rag/embed_backend.hpp ) 测试决不能硬编码
// 一个名称:身份随嵌入器而变化,这是整个点——在温暖指数之间切换后端开关,
// 而不是默默地重新打开一个与不兼容几何学构建的开关.
static fs::path find_index(const fs::path& root, std::string_view stem) {
    std::error_code ec;
    // 指数低于 .agentty/cache/ (生成的数据,
    // 可扫描) 旧的平面位置也检查过了,所以仍然在中途 -migration.
    for (const auto& dir : {root / ".agentty" / "cache", root / ".agentty"}) {
        if (!fs::is_directory(dir, ec)) continue;
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            const auto name = e.path().filename().string();
            if (name.rfind(std::string{stem}, 0) == 0
                && name.size() > 6
                && name.compare(name.size() - 6, 6, ".ragdb") == 0)
                return e.path();
        }
    }
    return {};
}

TEST_CASE("rag adapter") {
    std::printf("rag_adapter_test\n");

    // 隔离的暂时性 工作区: a docs/ 文件夹 + 一条内存指向它。
    auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path tmp = fs::temp_directory_path() /
                   ("agentty_rag_" + std::to_string(nonce));
    fs::path docs = tmp / "docs";
    fs::remove_all(tmp);
    write_file(docs / "auth.md",
               "# Authentication\n\n"
               "agentty stores OAuth credentials in an encrypted keystore. "
               "The token is refreshed automatically before every request. "
               "To log in run `agentty login` which opens the browser flow.\n");
    write_file(docs / "sandbox.md",
               "# Filesystem sandbox\n\n"
               "Every tool call is confined to the workspace root. Writes "
               "outside the project directory are refused by the sandbox "
               "boundary unless the path is explicitly allowlisted.\n");
    write_file(docs / "build.md",
               "# Building\n\n"
               "Run cmake to configure, then cmake --build to compile the "
               "binary. The test suite runs under ctest.\n");
    for (int i = 0; i < 8; ++i)
        write_file(docs / ("survey" + std::to_string(i) + ".md"),
                   "# Survey facet " + std::to_string(i) + "\n\n"
                   "orion broad survey shared topic facet" + std::to_string(i) + "\n");
    std::string huge = "# Giant reference\n";
    for (int i = 0; i < 3000; ++i)
        huge += "unrelated filler line " + std::to_string(i) + "\n";
    huge += "needle-budget exact relevant sentence\n";
    write_file(docs / "giant.md", huge);
    write_file(tmp / "src" / "auth_guard.cpp",
               "bool validate_bearer_token(const std::string& token) {\n"
               "  return token == \"valid\";\n}\n");
    // 做一个 沙箱 a PROJECT 不仅仅是目录
    //
    // 模糊的索引解析度 dirs::Root::Project ,从 cwd
    // 向上走到最近的项目标记( .git/.agentty/.hg/.svn )
    // — 步行就是让一个检查结果有一个索引,不管你从哪一个子目录(#61)中发射。
    // 一个无标记的临时目录没有标记可找到,所以步行一路跑到上面,并扎根于任何真实
    // 的目录上。 仓库 测试 二进制程序 恰巧从: MB rag_code *.
    // ragdb 登陆 agentty's 自身 .agentty/ 而不是在这里,
    // find_index (tmp,...) 什么都没有发现, 下一个街区
    // 扔上 `fs::path{""} + ".meta.json"`.
    //
    // 所以这才失败了 完整 运行:独自一人,Ctest的cwd已经坐在
    // 里面 仓库 是谁的? agentty 指数去,
    // 所以 find_index 碰巧看到一个预告 -existing 和
    // 意外通过的说法 标记使 沙箱 自己 -contained 不管是哪种方式。
    fs::create_directories(tmp / ".git");
    auto old_cwd = fs::current_path();
    fs::current_path(tmp);
    // 单是chdir就不是 够了。 project_anchor
    // ()开始标记行走 util::project_root (,
    // 即 WORKSPACE ROOT 夹子,不是活的cwd -
    // 和 工作区 root 是进程 -global 说明
    // 在 二进制程序 可能已经钉在别的地方(若干 调用
    // set_workspace_root 永远不要恢复它。
    // 沙箱 或锚定在任何目录中 留下的最后测试和索引着陆
    const auto old_ws = ::agentty::tools::util::workspace_root();
    ::agentty::tools::util::set_workspace_root(::agentty::IoAccess::grant(), tmp);

#if defined(_WIN32)
    _putenv_s("AGENTTY_DOCS_DIR", docs.string().c_str());
    // 强制离线路径: 一条有界探测器, 然后 BM25-only 检索。
    _putenv_s("AGENTTY_OLLAMA_HOST", "127.0.0.1:1");
    // 与排名隔离 AMBIENT 环境:开发者安装的技能和学到
    // 的知识 内存 否则会和小临时文件一起索引,然后可以退出
    // -rank 和A类 过期 .ragdb/反馈 TSV 下
    // 划线 仓库 属于自己的 .agentty/ 将破坏结果,
    // 使两个知识来源都失效, 持久化 ,以及决定排名
    // 断言的学习循环.
    _putenv_s("AGENTTY_RAG_SKILLS", "0");
    _putenv_s("AGENTTY_RAG_MEMORY", "0");
    _putenv_s("AGENTTY_RAG_PERSIST", "1");
    _putenv_s("AGENTTY_RAG_LEARN", "0");
    _putenv_s("AGENTTY_RAG_GRAPH", "0");
    _putenv_s("AGENTTY_RAG_PRF", "0");
#else
    ::setenv("AGENTTY_DOCS_DIR", docs.string().c_str(), 1);
    ::setenv("AGENTTY_OLLAMA_HOST", "127.0.0.1:1", 1);   // 定界探测器,然后 BM25
    ::setenv("AGENTTY_RAG_SKILLS", "0", 1);
    ::setenv("AGENTTY_RAG_MEMORY", "0", 1);
    ::setenv("AGENTTY_RAG_PERSIST", "1", 1);
    ::setenv("AGENTTY_RAG_LEARN", "0", 1);
    ::setenv("AGENTTY_RAG_GRAPH", "0", 1);
    ::setenv("AGENTTY_RAG_PRF", "0", 1);
#endif

    {
        agentty::rag::Retriever r;

        // (1)+(2):一个点查询返回文件 -sourced 说得没错 -ranked 段落。
        auto res = r.retrieve("how do I log in / authenticate", 5, /*skip_docs=*/false);
        check(res.error.empty(), "retrieve() succeeds on a populated docs folder");
        check(!res.passages.empty(), "retrieve() returns at least one passage");
        if (!res.passages.empty()) {
            const auto& top = res.passages.front();
            check(top.source == "docs", "top passage is source-tagged \"docs\"");
            check(top.path.find("auth") != std::string::npos,
                  "top passage for an auth query is auth.md (ranking works)");
            check(!top.text.empty(), "passage carries body text");
            check(!res.mode.empty(), "mode/provenance string is populated");
        }

        // (4):重复调用是安全的,保持温暖(没有重新索引churn / crash).
        auto res2 = r.retrieve("filesystem sandbox workspace root", 3);
        check(res2.error.empty(), "second retrieve() succeeds");
        check(!res2.passages.empty(), "second query returns passages");
        if (!res2.passages.empty())
            check(res2.passages.front().path.find("sandbox") != std::string::npos,
                  "sandbox query ranks sandbox.md first");
        check(r.warm(), "index reports warm after a build");

        // 满 -power 特性 使用: 模式字符串 广告丰富的
        // 管道为可读 FUNNEL (聚变方法+每 -stage 和 数
        // CRAG 产生了信心。
        {
            auto q = r.retrieve("how do I log in / authenticate", 5);
            check(q.mode.find("convex-fusion") != std::string::npos,
                  "mode advertises convex (TM2C2) fusion (rag-cpp default)");
            check(q.mode.find("funnel:") != std::string::npos,
                  "mode renders the retrieval funnel (engine workings are shown)");
            check(q.mode.find("confidence") != std::string::npos,
                  "mode reports a confidence signal");
            check(q.confidence >= 0.0 && q.confidence <= 1.0,
                  "confidence is a well-formed [0,1] signal");
        }

        // 需要写出一份真实的、持续的索引和验证清单。
        //
        // 文件名带有 EMBEDDER IDENTITY
        // 散列( 见 embed_backend.hpp :
        // a.ragdb 只能由一个产生相同矢量几何的嵌入器重用,
        // 而按键路径意味着在温暖指数之间切换后端开关,而不是默默地
        // 重新打开由不同嵌入器所建的开关。 SHAPE (
        // rag_docs.<8 hex> .ragdb + 其表),
        // 而不是固定名称.
        {
            std::error_code ec;
            const auto ragdb = find_index(tmp, "rag_docs.");
            check(!ragdb.empty() && fs::is_regular_file(ragdb, ec),
                  "persisted .ragdb is written under an identity-keyed name");
            if (!ragdb.empty()) {
                auto meta = fs::path{ragdb.string() + ".meta.json"};
                check(fs::is_regular_file(meta, ec),
                      "persisted source manifest is written");
            }
        }

        // 要求的宽度被尊重;纠正性检索决不能默默地崩溃一个宽k=
        // 8的请求,以达到它原先的3-strip默认值.
        {
            auto broad = r.retrieve("orion broad survey shared topic", 8);
            check(broad.error.empty(), "broad retrieval succeeds");
            check(broad.passages.size() >= 6, "broad retrieval is not capped at three passages");
        }

        // 正文总产出范围在12 KiB附近,
        // 并且从一个巨型来源的末尾保持相关跨度.
        {
            auto bounded = r.retrieve("needle-budget exact relevant sentence", 6);
            std::size_t bytes = 0;
            bool kept_needle = false;
            for (const auto& p : bounded.passages) {
                bytes += p.text.size();
                kept_needle = kept_needle || p.text.find("needle-budget") != std::string::npos;
            }
            check(bytes <= 12 * 1024, "retrieval passage bodies obey aggregate budget");
            check(kept_needle, "query-focused compression keeps the relevant span");
        }

        // 发电机 接口边界 可调用和驱动器 HyDE 启用时。
        {
#if defined(_WIN32)
            _putenv_s("AGENTTY_RAG_HYDE", "1");
#else
            ::setenv("AGENTTY_RAG_HYDE", "1", 1);
#endif
            bool gen_called = false;
            agentty::rag::Retriever r2;
            r2.set_generator([&](const std::string&, int n) {
                gen_called = true;
                std::vector<std::string> outs;
                for (int i = 0; i < (n > 0 ? n : 1); ++i)
                    outs.push_back("authenticate login oauth token browser flow");
                return outs;
            });
            auto hy = r2.retrieve("how to authenticate", 5);
            check(hy.error.empty(), "HyDE-enabled retrieve succeeds");
            check(gen_called, "generator seam is invoked when HyDE is on");
#if defined(_WIN32)
            _putenv_s("AGENTTY_RAG_HYDE", "0");
#else
            ::setenv("AGENTTY_RAG_HYDE", "0", 1);
#endif
        }

        // 学习是故意选择 -in :仅仅表面显示的结果不得
        // 产生系统惩罚的反馈 skills/memory.
        {
            std::error_code ec;
            auto fb = tmp / ".agentty" / "rag_feedback.tsv";
            fs::remove(fb, ec);
            (void)r.retrieve("filesystem sandbox workspace root", 3);
            agentty::rag::feedback::note_file_opened(agtest::cap(), "sandbox.md");
            check(!fs::exists(fb, ec), "implicit learning is disabled by default");
        }

        // 来源 -aware 代码索引返回定义 -shaped 块
        // 并更新一个更改的文件,而不丢弃整个程序表。
        {
            // 门 OPPORTUNISTIC 检索(结构零) -hit 线索( E) :
            // 冷——没有 -memory 指数,没有持续的破烂,
            // 必须报告 不 暖和的一面 -effect 查询无法触发冷建...
            check(!r.code_warm(), "code index reports cold before first build");
            auto code = r.retrieve_code("validate bearer token credentials", 5);
            check(code.error.empty() && !code.passages.empty(), "search_code finds source");
            // 明清之后就热身了
            check(r.code_warm(), "code index reports warm after explicit build");
            if (!code.passages.empty()) {
                check(code.passages.front().path.find("auth_guard.cpp") != std::string::npos,
                      "code result points at auth_guard.cpp");
                check(code.passages.front().text.find("validate_bearer_token") != std::string::npos,
                      "code-aware chunk preserves the function definition");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            write_file(tmp / "src" / "auth_guard.cpp",
                       "bool rotate_session_nonce(int nonce) { return nonce > 41; }\n");
            auto updated = r.retrieve_code("rotate session nonce", 5);
            check(updated.error.empty() && !updated.passages.empty(),
                  "incremental code refresh finds an edited file");
            if (!updated.passages.empty())
                check(updated.passages.front().text.find("rotate_session_nonce") != std::string::npos,
                      "edited definition replaces stale code content");

            // 磁盘 -verification :返回的段落文本必须是磁盘上
            // 的内容 RIGHT 当前 — read_disk_lines
            // 内容 -reads 引用范围,所以即使编辑后结果也是正确的
            // -on-bytes ,从来没有 过期 或 粘结的索引块。
            // 通过确认通过来证明它。 正文 是当前文件的逐字记录。
            if (!updated.passages.empty()) {
                std::ifstream in(tmp / "src" / "auth_guard.cpp", std::ios::binary);
                std::string disk((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
                const auto& body = updated.passages.front().text;
                // 吃第一个不吃 -empty 并需要将其逐字显示在磁盘上
                // (防控器) stale/garbled 文本),将删除。
                std::string first = body.substr(0, body.find('\n'));
                check(!first.empty() && disk.find(first) != std::string::npos,
                      "search_code passage is verified against live disk");
            }
        }
        // 更新了单一文件编辑,并替换了过期文件,
        // 而无需重建不相关的来源。
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            write_file(docs / "auth.md",
                       "# Authentication\n\nEncrypted OAuth keystore and browser login. "
                       "lattice-refresh-marker is now documented.\n");
            auto updated = r.retrieve("lattice refresh marker", 5);
            check(updated.error.empty() && !updated.passages.empty(),
                  "incremental docs refresh finds an edited document");
            if (!updated.passages.empty())
                check(updated.passages.front().text.find("lattice-refresh-marker") != std::string::npos,
                      "edited docs content replaces the stale passage");
            // 代码索引也覆盖 . md, 所以也刷新它。
            // 否则下面的温暖打开会正确地重写一个 过期 代码索引。
            (void)r.retrieve_code("rotate session nonce", 5);
        }
    }

    // 新的Retriever打开了恒定的体格,而不重写它。
    {
        auto db = find_index(tmp, "rag_docs.");
        std::error_code ec;
        auto before = fs::last_write_time(db, ec);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        agentty::rag::Retriever warm;
        auto res = warm.retrieve("encrypted OAuth keystore", 5);
        auto after = fs::last_write_time(db, ec);
        check(res.error.empty() && !res.passages.empty(), "fresh retriever opens persisted index");
        check(before == after, "warm open does not rewrite the persisted index");
        if (!res.passages.empty())
            check(res.passages.front().path.find("auth") != std::string::npos,
                  "warm-opened index preserves ranking");

        auto code_db = find_index(tmp, "rag_code.");
        // 绝不让一个MISSING索引降解为文件系统例外。
        // 用于在此中止的套件有“ 无法创建目录 :
        // 无效的参数“ 当索引在外部解决时 `tmp` ,
        // 它报告在一个无关的地方发生坠机事件,而不是点出真正的问题。
        // check() 说明缺少哪个索引,
        // 并保持其余的断言运行。
        REQUIRE(!code_db.empty(),
                "rag_code index exists under the sandbox anchor");
        auto code_before = fs::last_write_time(code_db, ec);
        auto code = warm.retrieve_code("rotate session nonce", 5);
        auto code_after = fs::last_write_time(code_db, ec);
        check(code.error.empty() && !code.passages.empty(),
              "fresh retriever opens persisted code index");
        check(code_before == code_after, "warm code open does not rewrite its index");
    }

    // 文件编辑时 agentty 关闭: 新的 Retriever
    // 加载 编号 已保存 索引和更新文件,所以找到新的文本,
    // 其余的(这里的调查文件)仍然在那里。
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        write_file(docs / "sandbox.md",
                   "# Sandbox\nThe quokka filter drops writes outside the jail.\n");
        write_file(tmp / "src" / "auth_guard.cpp",
                   "bool quokka_nonce_gate(int nonce) { return nonce > 7; }\n");
        agentty::rag::Retriever warm;
        auto res = warm.retrieve("quokka filter drops writes", 5);
        check(res.error.empty() && !res.passages.empty()
                  && res.passages.front().path.find("sandbox") != std::string::npos,
              "a doc edited while closed is found after reopen");
        auto old = warm.retrieve("encrypted OAuth keystore", 5);
        check(!old.passages.empty()
                  && old.passages.front().path.find("auth") != std::string::npos,
              "unchanged docs survive the incremental update");
        auto code = warm.retrieve_code("quokka nonce gate", 5);
        check(code.error.empty() && !code.passages.empty()
                  && code.passages.front().path.find("auth_guard") != std::string::npos,
              "a code file edited while closed is found after reopen");
        auto stale = warm.retrieve_code("rotate session nonce", 5);
        bool stale_hit = false;
        for (const auto& p : stale.passages)
            if (p.text.find("rotate_session_nonce") != std::string::npos) stale_hit = true;
        check(!stale_hit, "the old text of an edited code file is gone");
    }

    // Corrupt/truncated .meta.json 不可崩溃
    // 新 Retriever: 加载 路径 解析 内置元 try/catch
    // 回到一个干净的重建中,这个守护着... 持久化 硬化(原子元
    // 写作)——即使是半写元从一个被杀死的过程中恢复,也永远不会致命.
    {
        std::error_code ec;
        const auto docs_db = find_index(tmp, "rag_docs.");
        REQUIRE(!docs_db.empty(),
                "rag_docs index exists before the corrupt-meta probe");
        auto meta = fs::path{docs_db.string() + ".meta.json"};
        // 截断的 JSON 对象: 如果写作不是原子,
        // 被中断的写作会留下什么。
        write_file(meta, "{\"version\": 3, \"root\": \"/tmp\", \"docs_fp\":");
        agentty::rag::Retriever r;
        auto res = r.retrieve("encrypted OAuth keystore", 5);
        check(res.error.empty(), "corrupt .meta.json recovers via rebuild, no crash");
        check(!res.passages.empty(), "rebuild after corrupt meta still returns results");
        // 重建必须重写 VALID 元a( 可分割 JSON 对象).
        std::ifstream in(meta);
        std::string body((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
        check(!body.empty() && body.front() == '{' && body.back() == '}',
              "rebuild republishes a complete .meta.json");
    }

    // 甚处是. -large-corpus 边框: 超过文件盖的文档根,
    // 加上 vendored/build 双倍和超大小的块,必须指数a BOUNDED
    // 绝对不会坠毁,这保护着... OOM 即无约束 DirOptions (max_files=0,
    // 4 MB 文件,没有 _deps/build )在大代码库中引起,
    // 但 AGENTTY_DOCS_DIR 指向一个 仓库 根节。
    {
        fs::path big = tmp / "big_corpus";
        fs::remove_all(big);
        // 7000个小的docs( > 6000盖),所以行走必须切换.
        for (int i = 0; i < 7000; ++i)
            write_file(big / ("doc" + std::to_string(i) + ".md"),
                       "# Doc " + std::to_string(i) +
                       "\n\nalpha beta gamma delta term" + std::to_string(i) + "\n");
        // 销售商+ 构建输出, 省略 必须 , 不编制索引。
        write_file(big / "_deps" / "vendored.md", "should not be indexed\n");
        write_file(big / "build" / "generated.md", "should not be indexed\n");
        write_file(big / "node_modules" / "pkg.md", "should not be indexed\n");
        // 超大小的块数超过 1 个 MB 百分比 -file 盖子。
        std::string blob = "# Huge\n";
        blob.reserve(3 * 1024 * 1024);
        while (blob.size() < 3u * 1024 * 1024) blob += "filler filler filler\n";
        write_file(big / "huge.md", blob);

#if defined(_WIN32)
        _putenv_s("AGENTTY_DOCS_DIR", big.string().c_str());
#else
        ::setenv("AGENTTY_DOCS_DIR", big.string().c_str(), 1);
#endif
        agentty::rag::Retriever r;
        auto res = r.retrieve("alpha beta gamma delta", 5);
        check(res.error.empty(), "large-corpus retrieve() does not crash or error");
        check(!res.passages.empty(), "large-corpus retrieve() returns passages");
        for (const auto& p : res.passages) {
            check(p.path.find("_deps") == std::string::npos
                  && p.path.find("node_modules") == std::string::npos,
                  "vendored/build dirs are excluded from the docs corpus");
        }
    }

    // (3):空知 QQ优雅的"no knowledge"错误,不是崩溃.
    {
        fs::path empty_dir = tmp / "empty";
        fs::create_directories(empty_dir);
#if defined(_WIN32)
        _putenv_s("AGENTTY_DOCS_DIR", empty_dir.string().c_str());
        _putenv_s("AGENTTY_RAG_SKILLS", "0");
        _putenv_s("AGENTTY_RAG_MEMORY", "0");
#else
        ::setenv("AGENTTY_DOCS_DIR", empty_dir.string().c_str(), 1);
        ::setenv("AGENTTY_RAG_SKILLS", "0", 1);
        ::setenv("AGENTTY_RAG_MEMORY", "0", 1);
#endif
        agentty::rag::Retriever r;
        auto res = r.retrieve("anything at all", 5);
        check(!res.error.empty(), "empty knowledge set reports an error");
        check(res.passages.empty(), "empty knowledge set returns no passages");
    }

    fs::current_path(old_cwd);
    // 将进程交接 -global 工作区 边界 准确的回 因为我们找到了它:
    // 留下一个被删除的临时标记是打破了NEXT测试,
    // 而这个测试不应该传播它刚刚产生的危害.
    ::agentty::tools::util::set_workspace_root(::agentty::IoAccess::grant(), old_ws);
    fs::remove_all(tmp);
}

// 退转 : Retriever::shutdown () 必须中断一个 -flight
// 暖和的背景 PROMPTLY。合作社之前 -cancel 修补,温暖的Jthread
// 忽略了它 stop_token (抓获[ ⁇ ],不作[ ⁇ ]. stop_token ),
// 并且只在静态破坏时加入,阻断. ^C 4 -10 s, 嵌入式通行证完成时。
TEST_CASE("rag shutdown interrupts warm promptly") {
    namespace fs = std::filesystem;
    auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    auto tmp = fs::temp_directory_path() /
               ("agentty_rag_shutdown_" + std::to_string(nonce));
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    // 与主项目标记相同 沙箱 上面:没有它,这个cwd就没有锚,
    // 所以任何坚持在这里的 解决了 周围的真 仓库's
    // .agentty/ 换成是的
    fs::create_directories(tmp / ".git");
    auto old_cwd = fs::current_path();
    fs::current_path(tmp);
    // 单是chdir就不是 够了。 project_anchor
    // ()开始标记行走 util::project_root (,
    // 即 WORKSPACE ROOT 夹子,不是活的cwd -
    // 和 工作区 root 是进程 -global 说明
    // 在 二进制程序 可能已经钉在别的地方(若干 调用
    // set_workspace_root 永远不要恢复它。
    // 沙箱 或锚定在任何目录中 留下的最后测试和索引着陆
    const auto old_ws = ::agentty::tools::util::workspace_root();
    ::agentty::tools::util::set_workspace_root(::agentty::IoAccess::grant(), tmp);

    // 一大块 -ish 如此温暖的体格需要工作(因此,一个窗口,
    // 其中 取消 问题。
    fs::create_directories(tmp / "docs");
    for (int f = 0; f < 8; ++f) {
        std::ofstream o(tmp / "docs" / ("doc" + std::to_string(f) + ".md"));
        for (int i = 0; i < 400; ++i)
            o << "# section " << f << "-" << i
              << "\nsome searchable prose about topic " << (i % 7) << ".\n\n";
    }

    {
        agentty::rag::Retriever r;
        agentty::rag::Config cfg;
        cfg.docs_root = (tmp / "docs").string();
        r.apply_config(cfg);

        // 将背景打得暖和,然后立刻要求它停止。
        // shutdown() 一定要快点回来 因为 工作线程
        // 现在投票 取消 标志 / 在嵌入式构建之前。
        r.warm_async();
        const auto t0 = std::chrono::steady_clock::now();
        r.shutdown();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        // 优雅的天花板:甚至一个英寸 -flight 嵌入的批量在此之下;
        // 旧 整个多块被屏蔽 -second 装着炸弹
        check(ms < 2500, "shutdown() returns promptly, not after a full warm");

        // shutdown() 谓 幂等 并让回收机可用。
        r.shutdown();
        r.warm_async();
        r.shutdown();
        check(true, "re-warm + re-shutdown after a cancel is safe");
    }

    fs::current_path(old_cwd);
    // 将进程交接 -global 工作区 边界 准确的回 因为我们找到了它:
    // 留下一个被删除的临时标记是打破了NEXT测试,
    // 而这个测试不应该传播它刚刚产生的危害.
    ::agentty::tools::util::set_workspace_root(::agentty::IoAccess::grant(), old_ws);
    fs::remove_all(tmp);
}
