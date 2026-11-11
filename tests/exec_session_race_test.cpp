// exec_session_race_test — agentty 'real Exec (英语:
// src/tool/util/exec.cpp )在同时使用下,建造的面积小到足以运行在TSan下.
//
// 几个 线程 开始背景课、调查、阻止 并删除最后的引用 排水 工作线程 可能还在写,
// 形状 process_start/poll/stop 驱动器。同时运行前景命令。
// A 锁 以错误顺序拍摄的中止( Q) jaal lock_order_error ;
// 数据 竞态 出现在下面 -fsanitize=thread ;
// 自定义 -join 掷出 EDEADLK.
//
// 沙箱 默认关闭: 四 沙箱 符号 被压在下面 所以这个 链接 exec.cpp 一个人,
// 不是 claybin。该结构为 exec_session_race_sandboxed
// (EXEC_RACE_REAL_SANDBOX ) 其为: 链接 真实 沙箱
// 改为在 claybin 和中介的syscalls。。

#include "agentty/tool/util/exec.hpp"
#include "agentty/tool/util/sandbox.hpp"

#include <mcp/tools/host.hpp>
#include <mcp/tools/state.hpp>

#include <atomic>
#include <barrier>
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#if !defined(EXEC_RACE_REAL_SANDBOX)
namespace agentty::tools::util::sandbox {
Backend detected_backend() noexcept { return Backend::None; }
bool is_active() noexcept { return false; }
claybin_backend::SpawnResult spawn_in_sandbox(const std::vector<std::string>&, std::string_view,
                                              int, const std::vector<std::string>&,
                                              const handoff::TrustedSnapshot*,
                                              const std::vector<std::pair<std::string, std::string>>&) {
    return {};
}
#if defined(__APPLE__)
std::expected<std::vector<std::string>, std::string>
wrap_argv_for_exec(const std::vector<std::string>& argv, std::string_view,
                   const std::vector<std::string>&) {
    return argv;   // 无法到达: is_active( ) 以上为假
}
#endif
}  // namespace agentty::tools::util::sandbox
#endif  // !EXEC_RACE_REAL_SANDBOX

namespace {
namespace mt = ::mcp::tools;
std::atomic<int> failures{0};
void check(bool ok, const char* what) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); failures.fetch_add(1); }
}

// 每个平台的壳上都具有相同的指令形状:快速退出,
// 睡眠短然后输出和n-zero退出,输出破裂,睡眠长.
// Windows 没有/ bin/sh; cmd.exe 盖上,
// 用 `ping -n N 127.0.0.1` 用作睡眠(N-1秒; -n 1是几毫秒),
// 因为 `timeout` 需要一个控制台 开会时没有。
struct Scripts {
    const char* quick;
    const char* short_then_fail;
    const char* burst;
    const char* long_sleep;
    const char* print_ok;
    const char* print_fail;
    const char* short_then_print;
};
#if defined(_WIN32)
constexpr Scripts kS{
    "echo x",
    "ping -n 1 127.0.0.1 >nul & echo y& exit /b 3",
    "for /l %i in (1,1,3000) do @echo yyyyyy",
    "ping -n 6 127.0.0.1 >nul",
    "echo a",
    "echo b& exit /b 1",
    "ping -n 1 127.0.0.1 >nul & echo z",
};

mt::ExecRequest sh(const std::string& script) {
    mt::ExecRequest r;
    r.program = {"cmd.exe", {"/S", "/C", script}};
    r.max_output_bytes = 64 * 1024;
    return r;
}
#else
constexpr Scripts kS{
    "echo x",
    // 自行杀死 -0: 当沙箱 开机时, 由中介进行syscall.
    "sleep 0.05; kill -0 $$; echo y; exit 3",
    "yes | head -c 20000",
    "sleep 5",
    "printf a",
    "printf b; exit 1",
    "sleep 0.02; echo z",
};

mt::ExecRequest sh(const std::string& script) {
    mt::ExecRequest r;
    r.program = {"/bin/sh", {"-c", script}};
    r.max_output_bytes = 64 * 1024;
    return r;
}
#endif

// 开始, 读到结束( 或放弃) , 停止, 放下。
// 会话的最后一个引用会以 线程 的哪个为对象终止。
void session_round(mt::Exec& exec, int i) {
    auto s = exec.start(mt::Call{}, sh(i % 3 == 0 ? kS.quick : i % 3 == 1 ? kS.short_then_fail
                                                              : kS.burst));
    check(s.has_value(), "session starts");
    if (!s) { std::fprintf(stderr, "  start error: %s\n", s.error().c_str()); return; }
    auto sess = *s;
    for (int k = 0; k < 40; ++k) {
        auto u = sess->read(std::chrono::milliseconds{i % 2 ? 0 : 20}, 1 << 20);
        if (!u.running && u.outcome) break;
    }
    if (i % 4 == 0) sess->stop();   // 停止一个完成的: 必须是 no-op
}

// 停止,当它还在运行,从另一个 线程 而不是读者。
void stop_while_polled(mt::Exec& exec) {
    auto s = exec.start(mt::Call{}, sh(kS.long_sleep));
    check(s.has_value(), "long session starts");
    if (!s) { std::fprintf(stderr, "  start error: %s\n", s.error().c_str()); return; }
    std::shared_ptr<mt::Session> sess = *s;
    std::thread poller([sess] {
        for (int k = 0; k < 30; ++k) {
            auto u = sess->read(std::chrono::milliseconds{10}, 1 << 20);
            if (!u.running) break;
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    sess->stop();
    poller.join();
}

void foreground(mt::Exec& exec, const mt::Call& call, int i) {
    auto r = exec.run(call, sh(i % 2 ? kS.print_ok : kS.print_fail));
    check(!std::holds_alternative<mt::StartFailed>(r.outcome), "foreground run starts");
    if (auto* f = std::get_if<mt::StartFailed>(&r.outcome))
        std::fprintf(stderr, "  start error: %s\n", f->reason.c_str());
}

}  // 命名空间结束。

int main() {
#if defined(EXEC_RACE_REAL_SANDBOX)
    {
        namespace sb = agentty::tools::util::sandbox;
        agentty::sandbox_cfg::Config cfg;
        cfg.configured = true;   // 默认态势: 过滤器 + 经纪人
        sb::set_config(cfg);
        if (!sb::init(sb::Mode::On) || !sb::is_active()) {
            std::puts("exec_session_race_sandboxed: no sandbox on this host, skipped");
            return 0;
        }
    }
#endif
    auto exec = agentty::tools::util::make_exec({});
    mt::Call call{};

    constexpr int kThreads = 8;
    constexpr int kRounds  = 25;
    std::barrier sync(kThreads);
    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t) {
        ts.emplace_back([&, t] {
            sync.arrive_and_wait();
            for (int r = 0; r < kRounds; ++r) {
                const int i = t * kRounds + r;
                if (r % 5 == 0) stop_while_polled(*exec);
                else if (r % 5 == 1) foreground(*exec, call, i);
                else session_round(*exec, i);
            }
        });
    }
    for (auto& th : ts) th.join();

    // 交接的会话 线程 掉在那里: 最后一个主人
    // 不是 线程 开始于此
    std::vector<std::shared_ptr<mt::Session>> pile;
    for (int i = 0; i < 16; ++i)
        if (auto s = exec->start(mt::Call{}, sh(kS.short_then_print))) pile.push_back(*s);
    std::thread dropper([p = std::move(pile)]() mutable { p.clear(); });
    dropper.join();

#if !defined(_WIN32)
    using clk = std::chrono::steady_clock;
    const auto ms_since = [](clk::time_point t0) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(clk::now() - t0).count();
    };

    // 一场的两位读者会变得不相干 字节 等于是说
    // 数据流 : 光标在相同范围内移动 锁 手伸出来
    // 字节.
    {
        auto s = exec->start(mt::Call{}, sh("i=0; while [ $i -lt 2000 ]; do echo $i; i=$((i+1)); done"));
        check(s.has_value(), "counting session starts");
        if (s) {
            auto sess = *s;
            std::string a, b;
            auto reader = [&sess](std::string& out) {
                for (;;) {
                    auto r = sess->read(std::chrono::milliseconds{20}, 1 << 20);
                    out += r.output;
                    if (!r.running && r.eof) { out += sess->read(std::chrono::milliseconds{0}, 1 << 20).output; return; }
                }
            };
            std::thread ta(reader, std::ref(a)), tb(reader, std::ref(b));
            ta.join(); tb.join();
            std::size_t expect = 0;
            for (int i = 0; i < 2000; ++i) expect += std::to_string(i).size() + 1;
            check(a.size() + b.size() == expect, "concurrent readers split the stream exactly");
        }
    }

    // stop() 扔下最后一个把手 永远挡不住 即使孩子
    // 忽略了 SIGTERM 故取满恩而死.
    {
        auto s = exec->start(mt::Call{}, sh("trap '' TERM; while :; do sleep 0.05; done"));
        check(s.has_value(), "stubborn session starts");
        if (s) {
            auto sess = *s;
            (void)sess->read(std::chrono::milliseconds{100}, 0);   // 让陷阱安装
            auto t0 = clk::now();
            sess->stop();
            check(ms_since(t0) < 50, "stop() returns at once");
            t0 = clk::now();
            sess.reset();   // 最后一把:泵单独开动
            check(ms_since(t0) < 50, "dropping the last handle never blocks");
        }
    }

    // stop() 唤醒泵:孩子得到 SIGTERM 很快,
    // 下次投票时就别提了
    {
        auto s = exec->start(mt::Call{}, sh(kS.long_sleep));
        check(s.has_value(), "sleeper starts");
        if (s) {
            auto sess = *s;
            (void)sess->read(std::chrono::milliseconds{50}, 0);
            const auto t0 = clk::now();
            sess->stop();
            for (;;) {
                auto r = sess->read(std::chrono::milliseconds{50}, 0);
                if (!r.running || ms_since(t0) > 3000) break;
            }
            check(ms_since(t0) < 1000, "stop reaches the child promptly");
        }
    }
#endif

    if (failures.load()) {
        std::fprintf(stderr, "exec_session_race_test: %d failure(s)\n", failures.load());
        return 1;
    }
    std::puts("exec_session_race_test: ok");
    return 0;
}
