// src/tool/util/exec.cpp - 一次循环,结束 jaal.
//
// 形状是反应堆,有两个期限。
// 最短的就是没有硬部件在这里:产卵、
// 使出口可以观察、
// 把手 生命周期 已经 jaal
// 政策是应该读的。

#include "agentty/tool/util/exec.hpp"

#include <algorithm>
#include <thread>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

#include <maya/runtime.hpp>
#include "agentty/util/background.hpp"   // util::WorkerGroup, run_isolated_detached

#include <mcp/tools/util/utf8.hpp>
#include "agentty/tool/util/utf8.hpp"   // progress_tail

#include "agentty/tool/util/sandbox.hpp"
#include "agentty/tool/util/sandbox_claybin.hpp"
#include "agentty/tool/host_call.hpp"   // 编号 分派器 单位 -call 数据

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#if !defined(_WIN32)
#  include <fcntl.h>
#  include <unistd.h>
#  include <cerrno>
#endif

namespace agentty::tools::util {
namespace {

namespace pf = maya::platform;
namespace mt = ::mcp::tools;
using process_t = pf::native_process;
using reactor_t = pf::native_reactor;

using clock_t_ = std::chrono::steady_clock;

constexpr std::uint64_t kExitToken = 1;
constexpr std::uint64_t kOutToken  = 2;
constexpr std::uint64_t kBrokerToken = 3;
constexpr std::uint64_t kInToken   = 4;

// AGENTTY_STOP_TRACE =1 日志执行停止序列为 stderr,
// 每行前缀有 ms 之后 `since`.
bool stop_trace_on() {
    static const bool on = [] {
        const char* v = std::getenv("AGENTTY_STOP_TRACE");
        return v && v[0] == '1';
    }();
    return on;
}

[[gnu::format(printf, 2, 3)]]
void stop_trace(clock_t_::time_point since, const char* fmt, ...) {
    if (!stop_trace_on()) return;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        clock_t_::now() - since).count();
    std::fprintf(stderr, "[exec] t=%lldms ", static_cast<long long>(ms));
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::fputc('\n', stderr);
}


#if defined(__linux__) || defined(_WIN32)
/// 电话 沙箱 进入,或空,当没有 沙箱 正在激活。 Linux 财务报告
/// 和已审计财务报表 Windows 仅:收养外国儿童需要一个临时的或程序处理器。
/// 达尔文通过包扎方格来限制(见JaalExec::confine).
///
/// 使用命名空间的claybin克隆人 标志 安装了seccomp 过滤器,
/// 并进入一个croup- 没有一个可以表示为"fork then exec"
/// - 从而产生结果, run_child 采纳结果。 在那条线之后,
/// 两条路径是同一个对象, 后面有相同的符合套件。
[[nodiscard]] std::function<AdoptedChild(int)>
sandbox_spawner(const std::vector<std::string>& argv, const std::string& cwd,
                std::vector<std::string> extra_roots = {},
                const handoff::TrustedSnapshot* trusted = nullptr,
                std::vector<std::pair<std::string, std::string>> env = {}) {
    namespace sb = agentty::tools::util::sandbox;
    if (sb::is_active() && sb::detected_backend() == sb::Backend::Claybin) {
        // `trusted` 是那个 分派器 活下来的 调用 产卵在其中运行
        // 调用 (start_child ),以后也不会.
        return [argv, cwd, extra_roots, trusted, env = std::move(env)](int out_fd) {
            auto r = sb::spawn_in_sandbox(argv, cwd, out_fd, extra_roots, trusted, env);
            AdoptedChild c;
            if (!r.started) { c.error = r.start_error; return c; }
            c.pid            = r.pid;
            c.pidfd          = r.pidfd;
            c.supervisor_fd  = r.supervisor_fd;
            c.service_broker = std::move(r.service_broker);
            c.keepalive      = std::move(r.net_proxy);
            c.kill_tree      = std::move(r.kill_tree);
            return c;
        };
    }
    return {};
}
#endif

struct started {
    process_t            proc;
    int                  supervisor_fd = -1;
    std::function<bool()> service_broker;
    std::function<bool()> kill_tree;
    // 无论沙箱需要什么在孩子运行时保持生命力(
    // 过滤过的网络代理拥有客人的监听套接字).
    std::shared_ptr<void> keepalive;
};

/// 生了,还是让 沙箱 产卵和收养。 Stdout和stderr
/// 共用一个管道: 交接a 终端 就会显示
[[nodiscard]] std::expected<started, std::string>
start_child(const ChildRun& run) {
    if (run.argv.empty()) return std::unexpected(std::string{"empty command"});
#if !defined(_WIN32)
    if (run.spawn_adopted) {
        int fds[2];
#if defined(__linux__)
        if (::pipe2(fds, O_CLOEXEC) != 0)
            return std::unexpected(std::string{"pipe: "} + std::strerror(errno));
#else
        // 没有管子2 macOS/BSD 设置 CLOEXEC 事实之后 竞
        // 态 与叉并排的,每非叉子相同 -Linux 产卵人接受;
        // jaal 关闭所有fd以上 的 Stderr 的孩子反正。
        if (::pipe(fds) != 0)
            return std::unexpected(std::string{"pipe: "} + std::strerror(errno));
        ::fcntl(fds[0], F_SETFD, FD_CLOEXEC);
        ::fcntl(fds[1], F_SETFD, FD_CLOEXEC);
#endif
        auto a = run.spawn_adopted(fds[1]);
        ::close(fds[1]);
        if (!a.error.empty() || a.pid <= 0) {
            ::close(fds[0]);
            return std::unexpected(a.error.empty() ? std::string{"sandbox did not start"} : a.error);
        }
        pf::posix_process::adopted_child c;
        c.pid             = a.pid;
        c.pidfd           = a.pidfd;
        c.stdout_fd       = fds[0];
        c.merged          = true;
        c.leads_own_group = a.leads_own_group;
        auto adopted = pf::posix_process::adopt(c);
        if (!adopted) return std::unexpected(std::string{adopted.error().what});
        return started{std::move(*adopted), a.supervisor_fd,
                       std::move(a.service_broker), std::move(a.kill_tree),
                       std::move(a.keepalive)};
    }
#endif

#if defined(_WIN32)
    if (run.spawn_adopted) {
        // 一种匿名烟斗; 沙箱 得到可继承的写入端,
        // 我们保留读端。 形状与 posix 臂相同 -- -
        // - 一个合并 数据流 , 交替脱落的a 终端 将会显示,
        // 但手掌, 因为这就是每个窗口 API 带子
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        HANDLE out_r = nullptr, out_w = nullptr;
        if (!::CreatePipe(&out_r, &out_w, &sa, 0))
            return std::unexpected(std::string{"pipe: "} +
                                   std::to_string(::GetLastError()));
        auto a = run.spawn_adopted(
            static_cast<int>(reinterpret_cast<intptr_t>(out_w)));
        // 产卵人重复它所需要的; OUR 写完就走 否则下面
        // 的读者就看不到 EOF.
        ::CloseHandle(out_w);
        if (a.error.empty() && a.pid <= 0) a.error = "sandbox did not start";
        if (!a.error.empty()) {
            ::CloseHandle(out_r);
            return std::unexpected(a.error);
        }
        pf::windows_process::adopted_child c;
        c.pid         = a.pid;
        c.process     = reinterpret_cast<void*>(static_cast<intptr_t>(a.pidfd));
        c.stdout_pipe = out_r;
        auto adopted = pf::windows_process::adopt(std::move(c));
        if (!adopted) {
            ::CloseHandle(out_r);
            return std::unexpected(std::string{adopted.error().what});
        }
        return started{std::move(*adopted), -1 /*窗口上没有 seccomp*/,
                       std::move(a.service_broker), std::move(a.kill_tree),
                       std::move(a.keepalive)};
    }
#endif

    pf::process_spec spec;
    spec.argv         = run.argv;
    spec.windows_command_line = run.windows_command_line;
    spec.cwd          = run.cwd;
    spec.env          = run.env;
    spec.merge_stderr = true;
    spec.new_session  = true;
    if (!run.stdin_data.empty()) spec.stdin_from = pf::stream_to::pipe;
    auto p = process_t::spawn(spec);
    if (!p) return std::unexpected(std::string{p.error().what} + " (" +
                                   std::to_string(p.error().native) + ")");
    return started{std::move(*p), -1, nullptr, nullptr};
}

/// 读至`cap`XXXX 字节而不封堵. 0与`eof`=写手关闭.
std::size_t read_ready(pf::borrowed_handle h, char* buf, std::size_t cap, bool& eof) {
#if defined(_WIN32)
    return pf::read_some(h, buf, cap, eof);
#else
    eof = false;
    for (;;) {
        const auto n = ::read(h.get(), buf, cap);
        if (n > 0) return static_cast<std::size_t>(n);
        if (n < 0 && errno == EINTR) continue;
        if (n == 0) eof = true;
        return 0;   // EAGAIN : 无剩
    }
#endif
}

/// Write 并且不会挡住管子的路 `closed` 读者不见了
std::size_t write_ready(pf::borrowed_handle h, const char* buf, std::size_t len, bool& closed) {
#if defined(_WIN32)
    return pf::write_some(h, buf, len, closed);
#else
    closed = false;
    for (;;) {
        const auto n = ::write(h.get(), buf, len);
        if (n > 0) return static_cast<std::size_t>(n);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
        closed = true;   // EPIPE
        return 0;
    }
#endif
}

/// 读一下所有现有的信息。永远不要挡道: 把手是非-blocking,
/// 我们只因为反应堆说准备好了才到这里。
/// `closed`(可选)是当编剧关闭管子时设定的.
std::size_t drain_into(pf::borrowed_handle h, std::string& out, std::size_t cap,
                       bool& truncated, bool* closed = nullptr) {
    char buf[64 * 1024];
    std::size_t got = 0;
    for (;;) {
        bool eof = false;
        const auto n = read_ready(h, buf, sizeof buf, eof);
        if (eof && closed) *closed = true;
        if (n > 0) {
            got += n;
            // 过了我们所保持的帽子 READING
            // 停止排水管会使孩子被堵在一条满管上,
            // 这就是一个产生过多输出的命令变成一个从未完成
            // 的命令。
            if (out.size() < cap) {
                const auto room = cap - out.size();
                const auto take = std::min(room, n);
                out.append(buf, take);
                if (take < n) truncated = true;
            } else {
                truncated = true;
            }
            continue;
        }
        break;   // 什么都没有,或者 EOF
    }
    return got;
}

std::optional<std::chrono::seconds> env_wall_override() {
    const char* e = std::getenv("AGENTTY_TOOL_HARD_TIMEOUT_SECS");
    if (!e || !e[0]) return std::nullopt;
    char* end = nullptr;
    const long v = std::strtol(e, &end, 10);
    if (end == e || v < 0) return std::nullopt;
    return std::chrono::seconds{v};
}

class JaalSession;   // 下面

namespace session_detail {
// 或 沙箱 与孩子一起返回: seccomp 听者必须回答,
// 而且 cgroup.kill。没有时空 沙箱.
struct SandboxHooks {
    int                   supervisor_fd = -1;
    std::function<bool()> service_broker;   // 答复 1 通知
    std::function<bool()> kill_tree;        // 假 = 返回组
    std::shared_ptr<void> keepalive;        // 编号 沙箱 自己的资源
};
}  // namespace session_detail
}  // 命名空间结束。
}  // namespace agentty::tools::util
// 曾经移动到泵中,从未共享:关闭拥有他们捕获的东西(
// 听众和croup,到shared_ptr只持有).
MAYA_SENDABLE(agentty::tools::util::session_detail::SandboxHooks);
namespace agentty::tools::util {
namespace {

namespace session_detail {
// 由谁可以触摸什么来划分背景反应器:
//   * adopt():一次,由会话构造器在泵存在前进行
//     (因此,在任何其他会议之前) 线程 需要一个 PumpKey 也一样。
//   * wake() : 任意 线程 ,随时(反应堆概念使其
//     线程-safe 自动同步 -signal-safe)
//   * drive() 需要一个 PumpKey ,并且仅 JaalSession 能够制造一个,
//     它确实在 pump() 所以登记和等待 发生在泵的 线
//     程 和其他地方,没有其他。
// 分裂就是它同步的原因(名称空间下的Otto-in)。
class PumpKey {
    PumpKey() = default;
    friend class ::agentty::tools::util::JaalSession;
};
class PumpReactor {
  public:
    PumpReactor() = default;
    // 从未复制或重播。
    PumpReactor(const PumpReactor&)            = delete;
    PumpReactor& operator=(const PumpReactor&) = delete;
    void adopt(PumpKey, reactor_t r) { r_.emplace(std::move(r)); }
    void wake() const noexcept { if (r_) r_->waker().wake(); }
    [[nodiscard]] bool valid() const noexcept { return r_.has_value(); }
    reactor_t& drive(PumpKey) noexcept { return *r_; }
  private:
    std::optional<reactor_t> r_;
};
}  // namespace session_detail
}  // 命名空间结束。
}  // namespace agentty::tools::util
// wake() 谓 线程-safe; drive() 只能从泵中达到( PumpKey).
MAYA_SYNC(agentty::tools::util::session_detail::PumpReactor);
namespace agentty::tools::util {
namespace {

/// 开始的节目,为后期的民意调查:演员.
///
/// 一个 线程, 泵拥有孩子, 管道和反应堆。 它是输出的唯一写法。
/// 手柄( 本类) 和泵共享 `Core` : 一个被守卫的
/// 数据流( 输出的唯一副本, 加上读光标和末端状态) ,
/// 一个停机的标志, 以及反应堆的醒钟。 Core是
/// co_owned 和 Sync, 因此手柄和泵可以互相比对方活,
/// 任何东西都不能在其中释放。
///
///   * read():一个独家的区段取新东西,移动光标,
///     并列读数会分解 字节。
///   * stop() : 设置 标志 并唤醒泵通过醒悟器。
///     块,泵送 SIGTERM ,则 SIGKILL 以恩典为后.
///   * 销毁: stop() 泵完成杀,保持
///     排水到 EOF ,收成,然后自己退出,它作为孤立的背景工作运行,
///     所以它从不与一个 池 工作线程 ,和 agentty
///     拆卸在出口处就被束缚了
///
/// 锁令:数据流是LEAF,其正文只移动字节和标志.
/// read() 是一个 mcp-cpp 虚拟签名 库 修正,所以它 -states
/// `unlocked` 与 thread_root 条目(列于
/// tests/lint/allowlist.txt ); 声音因为 mcp-cpp
/// 调用 只能从工具中 正文 , 外边 agentty 锁. stop() 完全没有能力:
/// 叶子 锁 唯一能触摸的东西
class JaalSession final : public mt::Session {
  public:
    JaalSession(process_t p, std::size_t window, session_detail::SandboxHooks hooks = {}) {
        // 核心拥有反应堆: 把手和泵 -own 核心,
        // 所以反应器(以及它的醒士背后的事件)一直存在,
        // 直到最后的它们消失. stop() 永远也醒不过
        // 来了 关上电源的电源就交给了这里 竞态 就是这样
        if (auto r = reactor_t::create()) core_->reactor.adopt(PumpKey{}, std::move(*r));
        if (!core_->reactor.valid()) {
            // 没有反应器:没有什么东西可以看着孩子。杀了它,
            // 立即报告结局,所以一个打电话的人不会永远离开投票。
            (void)p.stop(pf::stop_mode::forceful, pf::stop_scope::tree);
            mt::ExecOutcome oc = mt::StartFailed{"no reactor"};
            if (auto st = p.reap()) oc = mt::Signalled{st->code};
            maya::thread_root([&](const maya::unlocked& c) {
                core_->s.with(c, [](Stream& s, mt::ExecOutcome o) {
                    s.end(std::move(o));
                    s.eof = true;
                }, std::move(oc));
            });
            return;
        }
        ::agentty::util::run_isolated_detached("exec.session.pump",
            [](const maya::unlocked& c, std::stop_token st, process_t proc,
               std::size_t win, core_t core, session_detail::SandboxHooks hk) {
                pump(c, std::move(st), std::move(proc), win, std::move(core), std::move(hk));
            },
            std::move(p), window, core_, std::move(hooks));
    }
    ~JaalSession() override { stop(); }

    [[nodiscard]] Read read(std::chrono::milliseconds wait, std::size_t max) override {
        return maya::thread_root([&](const maya::unlocked& c) {
            auto [t, _] = core_->s.wait_with_for(c, wait,
                [](const Stream& s, std::size_t) { return s.fresh() || s.ended; },
                [](Stream& s, std::size_t m) { return s.take(m); }, max);
            Read r;
            r.output  = std::move(t.output);
            r.dropped = t.dropped;
            r.running = t.running;
            r.eof     = t.eof;
            r.outcome = std::move(t.outcome);
            r.uptime  = std::chrono::duration_cast<std::chrono::seconds>(
                (t.running ? clock_t_::now() : t.ended_at) - core_->started);
            return r;
        });
    }

    void stop() noexcept override {
        // 一个原子 标志 和一则事件fd写道: 两者都安全,
        // 不会有任何 线程 ,在任意 锁 所以
        // 它永远不会挡住
        if (core_->stop.exchange(true)) return;
        core_->reactor.wake();
    }

  private:
    // 泵式反应堆:见舱位上方的PumpReactor.
    using PumpKey = session_detail::PumpKey;
    using PumpReactor = session_detail::PumpReactor;

    // 输出、光标和结束:所有 read() 和泵两个触摸,
    // 守卫 <T> : 仅在a内可达到 锁 正文 ,没有东西可以逃脱,
    // 只有拥有价值。
    struct Stream {
        std::string  window;            // `cap` 字节 最新版本
        std::size_t  base = 0;          // 窗口绝对值的冲抵[0]
        std::size_t  seen = 0;          // 决 议 read() 已经发出
        std::size_t  cap  = 128 * 1024;
        bool         eof   = false;     // 管道关闭: 不再输出
        bool         ended = false;     // 儿童收获
        mt::ExecOutcome outcome = mt::Exited{0};
        clock_t_::time_point ended_at{};

        [[nodiscard]] bool fresh() const noexcept { return base + window.size() > seen; }

        void append(std::string_view bytes) {
            window.append(bytes);
            if (window.size() > cap) {
                const auto cut = window.size() - cap;
                window.erase(0, cut);
                base += cut;
            }
        }
        void end(mt::ExecOutcome o) {
            ended    = true;
            outcome  = std::move(o);
            ended_at = clock_t_::now();
        }

        // read()需要的:普通的拥有价值。
        struct Taken {
            std::string  output;
            std::size_t  dropped = 0;
            bool         running = true;
            bool         eof = false;
            std::optional<mt::ExecOutcome> outcome;
            clock_t_::time_point ended_at{};
        };

        Taken take(std::size_t max) {
            Taken r;
            const auto end = base + window.size();
            auto from = std::max(seen, base);
            r.dropped = from - seen;                      // 向外滚动
            if (end - from > max) { r.dropped += (end - from) - max; from = end - max; }
            r.output  = window.substr(from - base, end - from);
            seen      = end;
            r.running = !ended;
            r.eof     = eof;
            if (ended) r.outcome = outcome;
            r.ended_at = ended_at;
            return r;
        }
    };

    // 手柄和泵都分享什么, 双双的 co_owned, 和Sync:
    //   输出 数据流 : 被看守,
    //   一个叶子停在柄边, 读到泵反应堆 泵
    //   的反应堆,拥有 此处 所以,事件 背后的醒悟 生命,
    //            只要任何人都可以 调用 stop() 总计,
    // 所以 jaal 检查每个字段都是Sync.
    struct Core {
        maya::guarded<Stream>              s;
        std::atomic<bool>                  stop{false};
        const clock_t_::time_point         started = clock_t_::now();
        PumpReactor                        reactor;
    };
    using core_t = maya::co_owned<Core>;

    static void pump(const maya::unlocked& cap, std::stop_token pool_stop, process_t proc,
                     std::size_t window, core_t core, session_detail::SandboxHooks hooks) {
        // 泵是唯一的,我只知道这个 线程 等待或登记在反应堆上;
        // 核心拥有它,所以醒悟器仍然有效,用于 stop().
        auto& reactor = core->reactor.drive(PumpKey{});
        core->s.with(cap, [](Stream& s, std::size_t w) { s.cap = w; }, window);
        auto exit_reg = reactor.watch(proc.exit_handle().get(), pf::interest::read, kExitToken);
        if (!exit_reg) {
            // 规则与 run_child : 没人看的退出是永
            // 无止境的会话。停止它并结束 数据流 现在
            (void)proc.stop(pf::stop_mode::forceful, pf::stop_scope::tree);
            core->stop.store(true);
        }
        std::optional<reactor_t::registration> out_reg, broker_reg;
        if (auto h = proc.stdout_handle())
            if (auto r = reactor.watch(h->get(), pf::interest::read, kOutToken))
                out_reg = std::move(*r);
        // 电话 seccomp 听者,内核会给客人停车 线程 每一个中介的
        // syscall直到它得到答案, 所以这必须被监视 只要孩子活着,
        // 和 run_child 已经这样做。 POSIX fd: 数据输入系统
        // Windows 没有经纪人,它的反应堆 手表,不是内幕。
#if !defined(_WIN32)
        if (hooks.supervisor_fd >= 0 && hooks.service_broker)
            if (auto r = reactor.watch(hooks.supervisor_fd, pf::interest::read, kBrokerToken))
                broker_reg = std::move(*r);
#endif
        // SIGKILL 整个 源码树: cgroup.kill 当 沙箱 给
        // 我们一个(a) setsid() 后代不能离开它),否则就是群体.
        auto kill_all = [&] {
            if (!(hooks.kill_tree && hooks.kill_tree()))
                (void)proc.stop(pf::stop_mode::forceful, pf::stop_scope::tree);
        };

        constexpr auto kGrace = std::chrono::seconds{2};
        bool exited = false, eof = !out_reg;
        std::optional<clock_t_::time_point> kill_at;
        // 停止后, 如果有后代离开进程组, 管道可以永远打开。
        // 退出后这么长时间, 不要等待EOF。
        std::optional<clock_t_::time_point> give_up_eof;

        while (!(exited && eof)) {
            // 停止: 由手柄询问, 否则进程正在关闭。
            const bool stopping = core->stop.load() || pool_stop.stop_requested();
            if (stopping && !kill_at && !exited) {
#if defined(_WIN32)
                // 优雅的停机只结束了这里的领导, 一个幸存的孙子
                // 坚持stdout打开, 所以先杀了工作。
                if (hooks.kill_tree) (void)hooks.kill_tree();
#endif
                (void)proc.stop(pf::stop_mode::graceful, pf::stop_scope::tree);
                kill_at = clock_t_::now() + kGrace;
            }
            if (kill_at && !exited && clock_t_::now() >= *kill_at) {
                kill_all();
                kill_at = clock_t_::time_point::max();   // 一次
            }
            if (exited && stopping && !give_up_eof)
                give_up_eof = clock_t_::now() + std::chrono::milliseconds{500};
            if (give_up_eof && clock_t_::now() >= *give_up_eof) break;

            std::optional<std::chrono::milliseconds> wait;   // 无:直到事件
            if (kill_at && *kill_at != clock_t_::time_point::max())
                wait = std::chrono::ceil<std::chrono::milliseconds>(*kill_at - clock_t_::now());
            if (give_up_eof)
                wait = std::chrono::ceil<std::chrono::milliseconds>(*give_up_eof - clock_t_::now());
            if (wait && wait->count() < 0) wait = std::chrono::milliseconds{0};

            auto res = reactor.wait(wait);
            if (!res) break;
            for (std::uint8_t i = 0; i < res->count; ++i) {
                const auto& r = res->ready[i];
                if (r.token == kOutToken && (r.readable || r.hangup)) {
                    std::string chunk;
                    bool lost = false, closed = false;
                    if (auto h = proc.stdout_handle())
                        drain_into(*h, chunk, std::numeric_limits<std::size_t>::max(), lost, &closed);
                    if (!chunk.empty())
                        core->s.with(cap, [](Stream& s, std::string c) { s.append(c); },
                                     std::move(chunk));
                    // 排水和闭塞:停止观看,
                    // 或者反应堆报告每个等待和循环旋转上的悬浮.
                    if (closed) { out_reg.reset(); eof = true; }
                } else if (r.token == kExitToken && (r.readable || r.hangup)) {
                    exited = true;
                    exit_reg = {};
                } else if (r.token == kBrokerToken && (r.readable || r.hangup)) {
                    // 假: 听者已经玩完了( 客人已经走了 )。 停止观看,
                    // 或者一个 hung-up 的 fd 永远唤醒这个循环。
                    if (!hooks.service_broker()) broker_reg.reset();
                }
            }
            if (exited && !eof) {
                // 现在公布结尾;输出可能来自继承管子的孩子,
                // 而read()则同时报告.
                publish_exit(cap, proc, core);
            }
        }
        if (!exited) {
            // 反应堆故障,或 EOF 给 -up 与孩子活着:
            // 确保它死了,然后给内核一个时间使它可以收获(
            // reap() 拒绝而不是封锁)。
            kill_all();
            for (int i = 0; i < 100; ++i) {
                publish_exit(cap, proc, core);
                if (core->s.read(cap, [](const Stream& s) { return s.ended; })) break;
                (void)maya::delay_for(cap, std::stop_token{}, std::chrono::milliseconds{20});
            }
        }
        publish_exit(cap, proc, core);
        core->s.with(cap, [](Stream& s) {
            s.eof = true;
            // 仍然无法收获 超过2秒 SIGKILL 报告
            // 结局了 所以没有打电话的人会永远等待
            if (!s.ended) s.end(mt::ExecOutcome{mt::Signalled{9}});
        });
    }

    // 复试一次并公布结果. 幂等.
    static void publish_exit(const maya::unlocked& cap, process_t& proc, const core_t& core) {
        if (core->s.read(cap, [](const Stream& s) { return s.ended; })) return;
        mt::ExecOutcome oc = mt::Signalled{0};
        if (auto s = proc.reap())
            oc = (s->how == pf::exit_status::kind::exited)
                     ? mt::ExecOutcome{mt::Exited{s->code}}
                     : mt::ExecOutcome{mt::Signalled{s->code}};
        else
            return;   // 尚未可收获
        core->s.with(cap, [](Stream& s, mt::ExecOutcome o) { s.end(std::move(o)); },
                     std::move(oc));
    }

    core_t core_ = core_t::make();
};

class JaalExec final : public mt::Exec {
  public:
    explicit JaalExec(ExecDefaults d) : d_(d) {}

    // 放下枪 沙箱 和跑步和开始一样 调用 额外的根
    // ( 一个编辑器会话文件夹) 可以在 沙箱 也一样,
    // 在达文 jaal 不能收养外国孩子(没有婴儿),
    // 所以 沙箱 转而进入方格( 转录为
    // 4) ( 沙箱-exec 返回它为什么不能:
    // 失败关闭, 我们不能建立一个配置。
    [[nodiscard]] static std::optional<std::string> confine(const mt::Call& call, ChildRun& run) {
        std::vector<std::string> roots;
        for (const auto& r : call.scope_extra_roots) roots.push_back(r.string());
#if defined(__linux__) || defined(_WIN32)
        run.spawn_adopted = sandbox_spawner(run.argv, run.cwd, roots,
                                            ::agentty::tools::host_call(call.host).trusted,
                                            run.env);
#elif defined(__APPLE__)
        namespace sb = agentty::tools::util::sandbox;
        if (sb::is_active()) {
            auto wrapped = sb::wrap_argv_for_exec(run.argv, run.cwd, roots);
            if (!wrapped) return "sandbox: " + wrapped.error();
            run.argv = std::move(*wrapped);
        }
#endif
        return std::nullopt;
    }

    [[nodiscard]] mt::ExecResult run(const mt::Call& call, const mt::ExecRequest& req) override {
        ChildRun run;
        run.argv.reserve(req.program.args.size() + 1);
        run.argv.push_back(req.program.exe);
        for (const auto& a : req.program.args) run.argv.push_back(a);
        run.cwd = req.cwd.value_or(std::string{});
        run.env = req.env;

        const std::chrono::seconds idle = req.budgets.idle.value_or(d_.idle);
        run.idle = idle;
        run.wall = [&]() -> std::chrono::seconds {
            if (const auto o = env_wall_override()) return *o;
            if (req.budgets.wall) return *req.budgets.wall;
            return std::max(idle * d_.wall_multiple, d_.wall_floor);
        }();
        run.kill_grace       = d_.kill_grace;
        run.max_output_bytes = req.max_output_bytes.value_or(d_.max_output_bytes);
        if (req.stop_when)
            run.stop_when = [&req](std::string_view so_far) { return req.stop_when(so_far); };
        if (auto bad = confine(call, run)) {
            mt::ExecResult er;
            er.outcome = mt::StartFailed{std::move(*bad)};
            return er;
        }
        // 工具调用的活输出和取消:其输出到哪里,
        // 用户是否点击Esc. 只有末尾的卡片显示被复制和检查,
        // 所以长指令不会每勾点一次re-scan兆字节.
        if (call.progress)
            run.on_progress = [&call](std::string_view raw) {
                call.progress(::mcp::tools::util::to_valid_utf8(
                    std::string{progress_tail(raw)}));
            };
        if (call.stop.stop_possible())
            run.stop_requested = [st = call.stop] { return st.stop_requested(); };

        auto r = run_child(run);

        mt::ExecResult out;
        out.output    = ::mcp::tools::util::to_valid_utf8(std::move(r.output));
        out.truncated = r.truncated;
        out.elapsed   = r.elapsed;
        if (!r.started)            out.outcome = mt::StartFailed{r.start_error};
        else if (r.timed_out_idle) out.outcome = mt::TimedOut{mt::TimedOut::budget::idle};
        else if (r.timed_out_wall) out.outcome = mt::TimedOut{mt::TimedOut::budget::wall};
        else if (r.stopped_early)  out.outcome = mt::StoppedEarly{};
        else if (r.cancelled)      out.outcome = mt::Cancelled{};
        else if (r.exited)         out.outcome = mt::Exited{r.exit_code};
        else                       out.outcome = mt::Signalled{r.signal};
        return out;
    }

    [[nodiscard]] std::expected<std::shared_ptr<mt::Session>, std::string>
    start(const mt::Call& call, const mt::ExecRequest& req) override {
        ChildRun run;
        run.argv.reserve(req.program.args.size() + 1);
        run.argv.push_back(req.program.exe);
        for (const auto& a : req.program.args) run.argv.push_back(a);
        run.cwd = req.cwd.value_or(std::string{});
        run.env = req.env;
        if (auto bad = confine(call, run)) return std::unexpected(std::move(*bad));

        auto st = start_child(run);
        if (!st) return std::unexpected(st.error());
        // 泵回答经纪人,并拥有 cgroup.kill ,作为 run_child 已经。
        session_detail::SandboxHooks hooks{st->supervisor_fd, std::move(st->service_broker),
                                           std::move(st->kill_tree),
                                           std::move(st->keepalive)};
        return std::static_pointer_cast<mt::Session>(
            std::make_shared<JaalSession>(std::move(st->proc),
                                          req.max_output_bytes.value_or(
                                              d_.max_output_bytes),
                                          std::move(hooks)));
    }

    [[nodiscard]] bool stops_whole_tree() const noexcept override {
        // Windows : 工作对象持有 源码树. POSIX : 进程组不是
        // 源码树 ,因为一个后裔 调用 setsid() 离开它。
#if defined(_WIN32)
        return true;
#else
        return false;
#endif
    }

  private:
    ExecDefaults d_;
};

}  // 命名空间结束。

std::shared_ptr<mt::Exec> make_exec(ExecDefaults defaults) {
    return std::make_shared<JaalExec>(defaults);
}

ChildResult run_child(const ChildRun& run) {
    ChildResult r;
    const auto started_at = clock_t_::now();
    auto stamp = [&] {
        r.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(clock_t_::now() - started_at);
    };

    auto st = start_child(run);
    if (!st) { r.start_error = st.error(); stamp(); return r; }
    auto& proc = st->proc;

    auto reactor = reactor_t::create();
    if (!reactor) {
        (void)proc.stop(pf::stop_mode::forceful, pf::stop_scope::tree);
        r.start_error = "reactor: " + std::string{reactor.error().what};
        stamp();
        return r;
    }
    r.started = true;

    // 出口 出口 门 必须 被监视:下面的循环只是通过
    // 这个注册得知的,所以没有它,每次跑步都会一直到最后期限.
    // jaal 永远不要返回一个不能发射的流程,
    // 所以这种失败意味着反应堆本身拒绝了;说到做到,阻止孩子.
    auto exit_reg = reactor->watch(proc.exit_handle().get(), pf::interest::read, kExitToken);
    if (!exit_reg) {
        (void)proc.stop(pf::stop_mode::forceful, pf::stop_scope::tree);
        r.started = false;
        r.start_error = "cannot watch the child's exit: " + std::string{exit_reg.error().what};
        stamp();
        return r;
    }
    std::optional<reactor_t::registration> out_reg, in_reg, broker_reg;
#if defined(_WIN32)
    ::agentty::util::WorkerGroup feeder{"exec.stdin_feed"};   // 收获后就被摧毁了
    bool feeder_started = false;   // 一条支线工作正在运行,所以有东西可以加入
#endif
    if (auto h = proc.stdout_handle())
        if (auto reg = reactor->watch(h->get(), pf::interest::read, kOutToken))
            out_reg = std::move(*reg);
#if !defined(_WIN32)
    // 电话 seccomp 经纪人,当政策代表 调用。
    // 必须 服务:内核屏蔽客人直到有人回答.
    if (st->supervisor_fd >= 0 && st->service_broker)
        if (auto reg = reactor->watch(st->supervisor_fd, pf::interest::read, kBrokerToken))
            broker_reg = std::move(*reg);
#endif

    // 斯丁从这个循环中被喂食,一个块每醒, 所以一个孩子
    // 在读之前写作不能 死锁 对抗我们
    std::size_t fed = 0;
    auto feed = [&] {
        const auto h = proc.stdin_handle();
        if (!h) return;
        while (fed < run.stdin_data.size()) {
            bool closed = false;
            const auto n = write_ready(*h, run.stdin_data.data() + fed,
                                       run.stdin_data.size() - fed, closed);
            if (n > 0) { fed += n; continue; }
            if (!closed) return;   // 满: 等待再次被写入
            break;                 // 关闭了它的臭味,没有更多的可以给
        }
        in_reg.reset();
        proc.close_stdin();
    };
    if (auto h = proc.stdin_handle()) {
#if defined(_WIN32)
        // 无法写出匿名管道, 因此该种子会作为工作运行,
        // 并带有它自己的手柄副本。 循环会同时排出输出;
        // 一旦孩子结束, 写不出来, 任务结束。
        // 收获后的feeder.stop()为加盟.
        if (auto dup = maya::platform::duplicate_handle(h->get())) {
            proc.close_stdin();
            feeder_started = true;
            feeder.post([](const maya::unlocked&, std::stop_token, maya::platform::owned_handle own,
                           std::string data) {
                std::size_t off = 0;
                while (off < data.size()) {
                    bool closed = false;
                    const auto n = maya::platform::write_some(
                        maya::platform::borrowed_handle{own.get()},
                        data.data() + off, data.size() - off, closed);
                    if (closed || n == 0) break;
                    off += n;
                }
            }, maya::platform::owned_handle{*dup}, run.stdin_data);   // `own` 关闭结尾 : EOF 儿童事务
        } else {
            proc.close_stdin();
        }
#else
        ::fcntl(h->get(), F_SETFL, ::fcntl(h->get(), F_GETFL) | O_NONBLOCK);
        if (auto reg = reactor->watch(h->get(), pf::interest::write, kInToken))
            in_reg = std::move(*reg);
        feed();
#endif
    }

    auto last_progress = clock_t_::time_point{};
    auto progress = [&](bool force) {
        if (!run.on_progress) return;
        const auto now = clock_t_::now();
        if (!force && now - last_progress < std::chrono::milliseconds{80}) return;
        last_progress = now;
        run.on_progress(r.output);
    };

    // ── 两个钟 ──────────────────────────────────────────────────
    const bool has_idle = run.idle.count() > 0;
    const bool has_wall = run.wall.count() > 0;
    const auto wall_at  = started_at + run.wall;
    auto       idle_at  = started_at + run.idle;
    // 取消 自己没有唤醒来源,所以至少唤醒这个经常问;
    // 没有 stop_requested 期限由自己决定。
    constexpr std::chrono::milliseconds kCancelTick{100};

    bool stopping = false;   // 已发出SIGTERM
    std::optional<clock_t_::time_point> kill_at;
    std::optional<clock_t_::time_point> give_up_at;   // 之后 SIGKILL
    constexpr std::chrono::seconds kAfterKill{5};
    bool exited = false;

    for (;;) {
        const auto now = clock_t_::now();
        auto next = now + std::chrono::hours{24};
        if (has_idle && !stopping) next = std::min(next, idle_at);
        if (has_wall && !stopping) next = std::min(next, wall_at);
        if (kill_at) next = std::min(next, *kill_at);
        if (give_up_at) next = std::min(next, *give_up_at);
        if (run.stop_requested && !stopping) next = std::min(next, now + kCancelTick);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            next > now ? next - now : std::chrono::milliseconds::zero());

        auto res = reactor->wait(ms);
        if (!res) break;

        bool saw_output = false;
        for (std::uint8_t i = 0; i < res->count; ++i) {
            const auto& e = res->ready[i];
            if (e.token == kOutToken && (e.readable || e.hangup)) {
                std::size_t got = 0;
                if (auto h = proc.stdout_handle())
                    got = drain_into(*h, r.output, run.max_output_bytes, r.truncated);
                if (got > 0) saw_output = true;
                // 吊起来排水:停止监视,否则反应堆报告每个
                // 等待时间的吊挂,这个循环旋转直到退出.
                else if (e.hangup) out_reg.reset();
            } else if (e.token == kExitToken && (e.readable || e.hangup)) {
                exited = true;
            } else if (e.token == kInToken && (e.writable || e.hangup)) {
                feed();
            } else if (e.token == kBrokerToken && e.readable) {
                if (st->service_broker && !st->service_broker()) broker_reg.reset();
            }
        }
        // 输出重排闲置时钟,只排闲置的时钟:
        // 聊天的跑步让那个永远移动,所以墙钟结束.
        if (saw_output) {
            idle_at = clock_t_::now() + run.idle;
            progress(false);
        }
        if (saw_output && !stopping && run.stop_when && run.stop_when(r.output)) {
            r.stopped_early = true;
            (void)proc.stop(pf::stop_mode::forceful, pf::stop_scope::tree);
            break;
        }
        if (exited) break;

        const auto t = clock_t_::now();
        if (!stopping) {
            if (run.stop_requested && run.stop_requested()) r.cancelled = true;
            else if (has_idle && t >= idle_at)              r.timed_out_idle = true;
            else if (has_wall && t >= wall_at)              r.timed_out_wall = true;
            if (r.cancelled || r.timed_out_idle || r.timed_out_wall) {
                // 优雅的停机只结束领导者在Windows,
                // 一个幸存的孙子 持有sdout打开, 所以先杀死工作。
                bool tree_killed = false;
#if defined(_WIN32)
                tree_killed = st->kill_tree && st->kill_tree();
#endif
                // Ask 首选项: 给定的 shell SIGTERM 收其子孙.
                (void)proc.stop(pf::stop_mode::graceful, pf::stop_scope::tree);
                stopping = true;
                kill_at  = t + run.kill_grace;
                stop_trace(started_at, "stop cancelled=%d idle=%d wall=%d tree_kill=%d",
                           int(r.cancelled), int(r.timed_out_idle), int(r.timed_out_wall),
                           int(tree_killed));
                continue;
            }
        }
        if (kill_at && t >= *kill_at) {
            // 整个源码树. kill_tree是claybin的cgroup.kill,
            // 孙子中的setsid()是无法逃脱的;
            // 群信号是回退.
            const bool kt = st->kill_tree && st->kill_tree();
            stop_trace(started_at, "force tree_kill=%d", int(kt));
            if (!kt)
                (void)proc.stop(pf::stop_mode::forceful, pf::stop_scope::tree);
            kill_at.reset();
            // SIGKILL被发送;出口应该随即出现。
            // 如果它不(在D州的任务,一个没有降落的杀手),
            // 就停止等待,而不是在反应堆里睡觉,而根本没有最后期限。
            give_up_at = t + kAfterKill;
        }
        if (give_up_at && t >= *give_up_at) {
            (void)proc.stop(pf::stop_mode::forceful, pf::stop_scope::tree);
            break;
        }
    }

    // 出口说 字节 已经办好了,不是说我们有他们。
    stop_trace(started_at, "loop ended");
    if (auto h = proc.stdout_handle())
        drain_into(*h, r.output, run.max_output_bytes, r.truncated);
    stop_trace(started_at, "drained");
    progress(true);

    if (auto status = proc.reap()) {
        if (status->how == pf::exit_status::kind::exited) { r.exited = true; r.exit_code = status->code; }
        else                                              { r.signalled = true; r.signal = status->code; }
    } else {
        r.signalled = true;   // 不见了,没有告诉我们如何
    }
    stop_trace(started_at, "reaped");
#if defined(_WIN32)
    // 孙子仍能持住stdin管的读端,
    // 使支线在WriteFile中被屏蔽. 结束工作关闭每份拷贝,
    // 所以写作失败,下面的屏障无法挂.
    (void)proc.stop(pf::stop_mode::forceful, pf::stop_scope::tree);
    // run_child 是阻叶辅助器(它 锁 其呼唤者是工具 正文,
    // mcp-cpp 执行, keystore/clipboard CLI
    // 探测器,还有 RAG 文档转换器。最后一个在 RAG 指数 锁 ,
    // 不能重新加入 ,无法重新加入 -stated (# 112) ,
    // 但不会通过 stdin, 所以没有支线可以加入。 只有启动的运行会加入它,
    // 而所有来自外部的都来自每个 agentty 锁.
    if (feeder_started) {
        maya::thread_root([&](const maya::unlocked& c) { feeder.stop(c); });
        stop_trace(started_at, "feeder joined");
    }
#endif
    stamp();
    return r;
}

// 扫描风扇 -out mcp-cpp 和 时 rag-cpp '搜索工具' 调用 通过
// 其分拆器(a) std::function 其签名 库。 加入帮助者需要 `unlocked` ;
// 内容是 -stated 这里,一个是审查的 root 用于拆分器( )
// tests/lint/allowlist.txt。声音: 库 调用 工具上的这个 正文
// 永远不要从里面 agentty 锁 正文 ,和 `fn` (按每笔捐款计)
// -share 扫描)不 agentty 锁.
//
// 永远不要将这个分解器交给在内部运行的物体 锁 正文 : 数据 RAG 索引构建和
// 搜索控件 rag_index ,所以它的引擎得到内置的分解器( )
// rag/adapter.cpp make_engine_config, #112).
void scope_fan_out(std::size_t n, const std::function<void(std::size_t)>& fn) {
    if (n <= 1) { if (n) fn(0); return; }
    maya::thread_root([&](const maya::unlocked& cap) {
        maya::parallel_for(cap, n, n, fn);   // 返回前加入每股
    });
}

std::size_t fan_out_width() noexcept {
    const unsigned hc = std::thread::hardware_concurrency();
    return hc == 0 ? 4 : hc;
}

mt::Splitter make_splitter() {
    return mt::Splitter{scope_fan_out, fan_out_width()};
}

}  // namespace agentty::tools::util
