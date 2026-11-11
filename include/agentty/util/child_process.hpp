// agentty/util/child_process.hpp-long-lived子进程，其stdio
// 为std::istream/std::ostream。
//
// 用于MCP服务器、ACP代理和抹布对端。由 maya 的
// native_process（posix 或 windows）启动，
// 因此它得到与其他子项相同的保证。只有streambufs下的管道IO因平台而异。
// `cmd /c <payload>` argv将载荷传递到所写的cmd.exe。
#pragma once

#if defined(__unix__) || defined(__APPLE__) || defined(_WIN32)
#  define AGENTTY_HAVE_CHILD_PROCESS 1
#else
#  define AGENTTY_HAVE_CHILD_PROCESS 0
#endif

#if AGENTTY_HAVE_CHILD_PROCESS

#include <atomic>
#include <chrono>
#include <istream>
#include <memory>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <maya/runtime.hpp>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <cerrno>
#  include <poll.h>
#  include <unistd.h>
#endif

namespace agentty::util {

// 孩子本身，以及它的结局。
//
// stop()、reap()和close_stdin()绝不能竞
// 态：在另一个线程的收割之后运行的停止可以发出重复使用的pid的信号，
// 并且接近的竞赛结束将使-close的句柄加倍。那曾经是锁。
// 现在它是 一个 原子字，其中 "in use" 位由compare
// -and-swap声明，因为这些操作是从无法赋予 锁 功能的位置（析构函数；
// std::streambuf::sync 覆盖 库 调用）到达的，
// 并且在那里获取的 锁 将位于 编译-time 顺序之外。
// 失败的索赔根本不会执行任何操作，这对于这里的每个操作都是正确的：
//
//   收割输给另一收割->另一收割记录状态止损输给任何
//   ->跳过； terminate() 重试 有最后期限，
//                                     无论如何收割都会结束，
//   几乎失去任何东西 -> 重试，直到索赔免费或该
//                                     过程被收割（EOF 无论如何）
//
// 无需等待，没有锁，订单看不到任何内容。
class child_proc {
public:
    explicit child_proc(maya::platform::native_process p) : proc_(std::move(p)) {}

    [[nodiscard]] bool reaped() const noexcept {
        return (st_.load(std::memory_order_acquire) & kReaped) != 0;
    }
    [[nodiscard]] std::optional<int> exit_code() const noexcept {
        // kReaped被设置之前发布(release)，之后读取(acquire)。
        if (!reaped()) return std::nullopt;
        return code_;
    }

    /// 一旦退出句柄被激发，就收获。当孩子离开时确实如此。
    bool reap() noexcept {
        if (!claim()) return reaped();
        if (!(st_.load(std::memory_order_relaxed) & kReaped)) {
            if (auto st = proc_.reap())
                code_ = st->how == maya::platform::exit_status::kind::exited ? st->code
                                                                             : 128 + st->code;
            release(kReaped);
        } else {
            release(0);
        }
        return true;
    }

    /// 向 源码树 发出信号，除非它已经被收获（或忙：跳过）。
    void stop(maya::platform::stop_mode mode) noexcept {
        if (!claim()) return;
        if (!(st_.load(std::memory_order_relaxed) & kReaped))
            (void)proc_.stop(mode, maya::platform::stop_scope::tree);
        release(0);
    }

    /// 孩子的标准输入上的EOF。 幂等。
    void close_stdin() noexcept {
        for (;;) {
            if (st_.load(std::memory_order_acquire) & (kReaped | kStdinClosed)) return;
            if (claim()) {
                if (!(st_.load(std::memory_order_relaxed) & kStdinClosed)) proc_.close_stdin();
                release(kStdinClosed);
                return;
            }
            std::this_thread::yield();   // stop/reap持有系统调用的声明
        }
    }

private:
    static constexpr unsigned kBusy        = 1u;
    static constexpr unsigned kReaped      = 2u;
    static constexpr unsigned kStdinClosed = 4u;

    bool claim() noexcept {
        unsigned cur = st_.load(std::memory_order_acquire);
        while (!(cur & kBusy))
            if (st_.compare_exchange_weak(cur, cur | kBusy, std::memory_order_acq_rel)) return true;
        return false;
    }
    void release(unsigned set) noexcept {
        st_.fetch_or(set, std::memory_order_release);
        st_.fetch_and(~kBusy, std::memory_order_release);
    }

    maya::platform::native_process proc_;
    std::optional<int>             code_;
    std::atomic<unsigned>          st_{0};
};


// 孩子的标准输出作为streambuf。停放在 underflow() 的读卡器
// 可以被 interrupt() 唤醒，而无需关闭其下方的手柄。
class child_out_buf final : public std::streambuf {
public:
    explicit child_out_buf(maya::platform::borrowed_handle h) : h_(h) {
        setg(in_, in_ + sizeof(in_), in_ + sizeof(in_));
        auto r = maya::platform::native_reactor::create();
        if (!r) throw std::runtime_error("child_process: reactor");
        reactor_.emplace(std::move(*r));
        auto reg = reactor_->watch(h.get(), maya::platform::interest::read, 1);
        if (!reg) throw std::runtime_error("child_process: watch stdout");
        reg_.emplace(std::move(*reg));
    }
    void interrupt() noexcept {
        interrupted_.store(true, std::memory_order_release);
        if (reactor_) reactor_->waker().wake();
    }

protected:
    int_type underflow() override {
        for (;;) {
            if (interrupted_.load(std::memory_order_acquire)) return traits_type::eof();
#if defined(_WIN32)
            bool eof = false;
            const auto n = maya::platform::read_some(h_, in_, sizeof(in_), eof);
            if (n > 0) { setg(in_, in_, in_ + n); return traits_type::to_int_type(in_[0]); }
            if (eof) return traits_type::eof();
#else
            const auto n = ::read(h_.get(), in_, sizeof(in_));
            if (n > 0) { setg(in_, in_, in_ + n); return traits_type::to_int_type(in_[0]); }
            if (n == 0) return traits_type::eof();
            if (errno == EINTR) continue;
            if (errno != EAGAIN && errno != EWOULDBLOCK) return traits_type::eof();
#endif
            if (!reactor_->wait(std::nullopt)) return traits_type::eof();
        }
    }

private:
    maya::platform::borrowed_handle h_;
    std::optional<maya::platform::native_reactor> reactor_;
    std::optional<maya::platform::native_reactor::registration> reg_;
    std::atomic<bool> interrupted_{false};
    char in_[4096]{};
};

// 子进程的stdin作为streambuf:阻塞写入；失败（孩子关闭了
// 它）在 ostream 上表现为 badbit。
//
// close() 竞态是另一个线程上的作家。在写入时关闭
// fd 将使新打开的文件重用该编号并获取 字节，
// 因此仅当没有写入正在进行时才关闭 fd。 close() 也
// 绝不能等待写入器：写入器可能会在停止读取的子进程的完整
// 管道上被阻塞，并且在杀死该子进程的过程中调用 close()。
// 因此，close()将数据流标记为关闭，并且如果写入正在进行，
// 则将fd留给该写入器，该写入器在其离开时将其关闭。
//
// 两个标志存在于ONE原子字中并且每个转换都是单个比较-and-swap。
// sync() 是 std::streambuf 覆盖
// 库 调用，因此不能为其提供 锁 功能；声明 锁-free
// 使此类完全处于 锁 顺序之外。
class child_in_buf final : public std::streambuf {
public:
    child_in_buf(child_proc& p, std::optional<maya::platform::borrowed_handle> h)
        : p_(p), h_(h) { setp(out_, out_ + sizeof(out_)); }
    void close() noexcept {
        // 设置`closed`。如果没有写入正在进行，则我们可以关闭 fd；
        // 否则，作者在离开并关闭它时会看到 `closed`。
        unsigned cur = st_.load(std::memory_order_acquire);
        for (;;) {
            if (cur & kClosed) return;
            if (st_.compare_exchange_weak(cur, cur | kClosed, std::memory_order_acq_rel))
                break;
        }
        if (!(cur & kWriting)) close_fd();
    }

protected:
    int_type overflow(int_type ch) override {
        if (sync() != 0) return traits_type::eof();
        if (!traits_type::eq_int_type(ch, traits_type::eof())) {
            *pptr() = traits_type::to_char_type(ch);
            pbump(1);
        }
        return traits_type::not_eof(ch);
    }
    int sync() override {
        const char* p = pbase();
        std::size_t left = static_cast<std::size_t>(pptr() - pbase());
        if (left == 0) return 0;
        // 声明fd：当设置了`writing`时没有人关闭它。
        unsigned cur = st_.load(std::memory_order_acquire);
        for (;;) {
            if (cur & (kClosed | kWriting)) return -1;
            if (st_.compare_exchange_weak(cur, cur | kWriting, std::memory_order_acq_rel))
                break;
        }
        // 在生成时复制；直到 close_fd() 为止有效，现在无法运行。
        if (!h_) { release_claim(); return -1; }
        const int rc = write_all(*h_, p, left);
        if (release_claim()) { close_fd(); return -1; }   // close() 留给我们了
        if (rc == 0) setp(out_, out_ + sizeof(out_));
        return rc;
    }

private:
    static constexpr unsigned kClosed  = 1u;
    static constexpr unsigned kWriting = 2u;   // 写入正在进行中；它拥有FD

    // 放弃写声明；当 close() 同时运行时为真（fd 是我们的）。
    bool release_claim() noexcept {
        return (st_.fetch_and(~kWriting, std::memory_order_acq_rel) & kClosed) != 0;
    }

    void close_fd() noexcept { p_.close_stdin(); }

    static int write_all(maya::platform::borrowed_handle h, const char* p, std::size_t left) {
        while (left > 0) {
#if defined(_WIN32)
            DWORD put = 0;
            if (!::WriteFile(static_cast<HANDLE>(h.get()), p, static_cast<DWORD>(left), &put, nullptr)
                || put == 0)
                return -1;
            p += put;
            left -= put;
#else
            const auto n = ::write(h.get(), p, left);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) return -1;
            p += n;
            left -= static_cast<std::size_t>(n);
#endif
        }
        return 0;
    }

    child_proc&                                      p_;
    const std::optional<maya::platform::borrowed_handle> h_;
    std::atomic<unsigned> st_{0};
    char out_[4096]{};
};

// 通过其 stdin/stdout. stderr 进行对话的 long-lived 子项
// （MCP 服务器、ACP 代理、rag 对端）被继承：服务器记录在那里。生成失败时，
// 构造会抛出 std::runtime_error。
class ChildProcess {
public:
    struct Spawn {
        std::string              command;   // 可执行文件(PATH-resolved)
        std::vector<std::string> args;      // 不 包括 argv[0]
        std::vector<std::string> env_kv;    // 额外的"KEY=VALUE"条目
        std::string              cwd;        // 空继承父cwd
        bool                     merge_stderr = false;
    };

    explicit ChildProcess(const Spawn& s) {
        namespace pf = maya::platform;
        pf::process_spec spec;
        spec.argv.reserve(s.args.size() + 1);
        spec.argv.push_back(s.command);
        for (const auto& a : s.args) spec.argv.push_back(a);
        for (const auto& kv : s.env_kv) {
            const auto eq = kv.find('=');
            if (eq == std::string::npos) continue;
            spec.env.emplace_back(kv.substr(0, eq), kv.substr(eq + 1));
        }
        spec.cwd          = s.cwd;
        spec.stdin_from   = pf::stream_to::pipe;
        spec.stdout_to    = pf::stream_to::pipe;
        spec.stderr_to    = s.merge_stderr ? pf::stream_to::pipe : pf::stream_to::inherit;
        spec.merge_stderr = s.merge_stderr;
        spec.new_session  = true;
        auto p = pf::native_process::spawn(spec);
        if (!p) throw std::runtime_error("cannot start '" + s.command + "': "
                                     + std::string{p.error().what} + " ("
                                     + std::to_string(p.error().native) + ")");
        // 在进程转到锁后面之前，句柄被复制出来。
        // 只要进程对象存在，它们就保持有效（确实如此）。
        exit_h_ = p->exit_handle();
        pid_    = static_cast<int>(p->id());
        const auto out = p->stdout_handle();
        const auto in  = p->stdin_handle();
        if (!out) throw std::runtime_error("child_process: no stdout pipe");
        proc_ = std::make_unique<child_proc>(std::move(*p));
        out_buf_    = std::make_unique<child_out_buf>(*out);
        in_buf_     = std::make_unique<child_in_buf>(*proc_, in);
        out_stream_ = std::make_unique<std::istream>(out_buf_.get());
        in_stream_  = std::make_unique<std::ostream>(in_buf_.get());
    }

    ~ChildProcess() { terminate(); }

    ChildProcess(const ChildProcess&)            = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;

    [[nodiscard]] std::istream& out() noexcept { return *out_stream_; }   // 儿童标准输出
    [[nodiscard]] std::ostream& in()  noexcept { return *in_stream_; }    // 儿童标准输入
    [[nodiscard]] int pid() const noexcept { return pid_; }

    [[nodiscard]] bool alive() noexcept {
        if (exited()) return false;
        return !wait_exit(std::chrono::milliseconds::zero());
    }

    // 0..255表示退出，128+N表示信号N死亡；运行时nullopt。
    [[nodiscard]] std::optional<int> exit_code() const noexcept { return proc_->exit_code(); }

    // 孩子的标准输入上的EOF:well-behaved服务器开始退出。
    // 数据流 保持活力（对端 可能仍保留着它们）；现在写入失败。
    void close_stdin() noexcept { in_buf_->close(); }

    // 唤醒停放在孩子标准输出上的读者；它看到 EOF。
    void interrupt_output() noexcept { out_buf_->interrupt(); }

    // Ask它完成，然后制作它。首先是标准输入 EOF；合作的孩子在
    // `grace` 内退出。然后优雅地停止它的源码树，然后用力停止。
    void terminate(std::chrono::milliseconds grace = std::chrono::milliseconds{500}) noexcept {
        if (exited()) return;
        namespace pf = maya::platform;
        close_stdin();
        if (wait_exit(grace)) return;
        stop(pf::stop_mode::graceful);
        if (wait_exit(std::chrono::milliseconds{2000})) return;
        stop(pf::stop_mode::forceful);
        (void)wait_exit(std::chrono::milliseconds{2000});
    }

    void shutdown() noexcept {
        terminate();
        interrupt_output();
    }

private:
    [[nodiscard]] bool exited() const noexcept { return proc_->reaped(); }

    // 声称，因此收获后无法着陆。
    void stop(maya::platform::stop_mode m) noexcept { proc_->stop(m); }

    // 退出句柄是否在`d`内发出信号？ Level-triggered，
    // 因此任何 线程 都可能会在没有共享反应器的情况下询问。
    [[nodiscard]] bool exit_ready(std::chrono::milliseconds d) noexcept {
#if defined(_WIN32)
        return ::WaitForSingleObject(static_cast<HANDLE>(exit_h_.get()),
                                     static_cast<DWORD>(d.count())) == WAIT_OBJECT_0;
#else
        pollfd pfd{exit_h_.get(), POLLIN, 0};
        int rc;
        do rc = ::poll(&pfd, 1, static_cast<int>(d.count())); while (rc < 0 && errno == EINTR);
        return rc > 0;
#endif
    }

    // 等待 `d` 退出，一旦触发就收获（在 锁 下，
    // 所以正好一次）。当孩子离开时确实如此。
    bool wait_exit(std::chrono::milliseconds d) noexcept {
        if (exited()) return true;
        if (!exit_ready(d)) return false;
        while (!proc_->reap()) std::this_thread::yield();   // 止损短暂保留索赔
        return true;
    }

    // 在指针后面：in-buf保存引用。
    std::unique_ptr<child_proc>                proc_;
    maya::platform::borrowed_handle            exit_h_{};
    int                                        pid_ = 0;
    std::unique_ptr<child_out_buf> out_buf_;
    std::unique_ptr<child_in_buf>  in_buf_;
    std::unique_ptr<std::istream>  out_stream_;
    std::unique_ptr<std::ostream>  in_stream_;
};

} // namespace agentty::util

#endif // AGENTTY_HAVE_CHILD_PROCESS
