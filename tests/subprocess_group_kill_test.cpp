// subprocess_group_kill_test——timeout/cancel必须杀死整个源码树.
//
// Subprocess::run 用于仅表示 `sh -c`
// 儿童的背景 `sleep 60 &` 或管道阶段幸存
// 下来 超时 工具返回后,他继续运行,跑者现在生下孩子
// session/process 组和信号组。

#include "agtest.hpp"

#include "agentty/tool/util/subprocess.hpp"

#if !defined(_WIN32)
#include <cerrno>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;
using agentty::tools::util::Subprocess;
using agentty::tools::util::SubprocessOptions;

namespace {

fs::path pid_file(const char* tag) {
    return fs::temp_directory_path()
         / ("agentty_group_kill_" + std::string{tag} + "_"
            + std::to_string(::getpid()));
}

// 等到~2秒时 `pid` 以消失为目的 ESRCH 不见了。
bool gone(pid_t pid) {
    for (int i = 0; i < 40; ++i) {
        if (::kill(pid, 0) != 0 && errno == ESRCH) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
    }
    return false;
}

pid_t read_pid(const fs::path& p) {
    std::ifstream in(p);
    long v = 0;
    in >> v;
    return static_cast<pid_t>(v);
}

} // 命名空间结束。

TEST_CASE("subprocess timeout kills backgrounded children") {
    const auto pf = pid_file("timeout");
    std::error_code ec;
    fs::remove(pf, ec);

    SubprocessOptions opts;
    // 睡着就把烟斗打开,从来不写,所以闲着 超时 火,
    // 在修补睡眠之前, 时间已经过长 超时.
    // 3s,不是1s:在平行测试下,贝壳需要更长的时间,
    // 仅仅叉开睡眠并写出它的皮德,a 超时 火力首先没有爆炸,
    // REQUIRE (bg > 0) -- -- 片面,而不是结论。
    // WHO 死,不是多快,是多快,我快死了,我快疯了
    opts.command = SubprocessOptions::Shell{
        "sleep 60 & echo $! > '" + pf.string() + "'; wait"};
    opts.timeout = std::chrono::seconds{3};
    auto r = Subprocess::run(std::move(opts));

    const pid_t bg = read_pid(pf);
    REQUIRE(bg > 0);
    CHECK(r.timed_out);
    const bool dead = gone(bg);
    if (!dead) ::kill(bg, SIGKILL);   // 别把它泄露到测试中
    CHECK_MESSAGE(dead, "the backgrounded sleep must die with its group");
    fs::remove(pf, ec);
}

TEST_CASE("subprocess cancel kills backgrounded children") {
    const auto pf = pid_file("cancel");
    std::error_code ec;
    fs::remove(pf, ec);

    const auto start = std::chrono::steady_clock::now();
    SubprocessOptions opts;
    opts.command = SubprocessOptions::Shell{
        "sleep 60 & echo $! > '" + pf.string() + "'; wait"};
    opts.timeout = std::chrono::seconds{60};
    opts.stop_requested = [start] {
        return std::chrono::steady_clock::now() - start
             > std::chrono::milliseconds{500};
    };
    auto r = Subprocess::run(std::move(opts));

    const pid_t bg = read_pid(pf);
    REQUIRE(bg > 0);
    CHECK(!r.timed_out);
    const bool dead = gone(bg);
    if (!dead) ::kill(bg, SIGKILL);
    CHECK_MESSAGE(dead, "cancel must take the whole group down");
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds{10});
    fs::remove(pf, ec);
}

TEST_CASE("subprocess normal exit is unaffected") {
    SubprocessOptions opts;
    opts.command = SubprocessOptions::Shell{"echo hi; exit 3"};
    opts.timeout = std::chrono::seconds{10};
    auto r = Subprocess::run(std::move(opts));
    CHECK(r.started);
    CHECK(r.exit_code == 3);
    CHECK(r.output.find("hi") != std::string::npos);
}
#endif

#if !defined(_WIN32)
// ── 墙壁 -clock 上限 ───────────────────────────────────────────────
//
// 闲着的 看门狗 盖子 SILENCE ,所以一个从未停止
// 印刷的孩子会永远地滚滚,并且永远没有收获。通过输出封顶,
// 读者会抛弃它所读的内容,所以 内存 也不绑起来: 一
// `yes` 留下一个旋转的过程和一个卡住了 工作线程 其余时间。
// agentty 安装其运行器为 mcp-cpp 东道主 沙箱
// 任何时候 沙箱 已经开始了,所以 mcp-cpp 默认配置
// 中绕过自己的上限——这个也在这里存在.
//
// 重要的平衡:这不得 工作在印刷过程中合法
// 地持续了很长时间,这是正常的情况,
// 必须生存下去。

TEST_CASE("subprocess: a chatty runaway is reaped by the wall clock") {
    SubprocessOptions opts;
    // 永不沉默,因此闲置的看门狗永远不能开火:没有这个XX00120
    // 的天花板,这个调用就不会返回.
    opts.command = SubprocessOptions::Shell{
        "while :; do echo spinning; done"};
    opts.timeout      = std::chrono::seconds{30};   // 闲置: 永远不旅行
    opts.hard_timeout = std::chrono::seconds{2};
    opts.max_bytes    = 4096;                       // 几乎一击即中

    const auto t0 = std::chrono::steady_clock::now();
    auto r = Subprocess::run(std::move(opts));
    const auto elapsed = std::chrono::steady_clock::now() - t0;

    CHECK(r.started);
    CHECK_MESSAGE(r.timed_out, "stopping it is still a timeout outcome");
    CHECK_MESSAGE(r.hard_capped,
                  "and it must be attributable to the wall clock, not to "
                  "silence -- the two want opposite advice");
    CHECK_MESSAGE(elapsed < std::chrono::seconds{20},
                  "reaped at its own ceiling, not the idle one");
}

TEST_CASE("subprocess: a long chatty job is NOT cut off") {
    // 方便保证。 每100 ms 输出一次, 输出为 ~1.2 s,
    // 闲置窗口比 运行时 总窗口短得多 : 保持打印
    // 的建筑无论需要多长时间都必须完成。
    SubprocessOptions opts;
    opts.command = SubprocessOptions::Shell{
        "for i in 1 2 3 4 5 6 7 8 9 10 11 12; do echo step $i; "
        "sleep 0.1; done; exit 0"};
    opts.timeout   = std::chrono::seconds{1};   // 短于运行时
    opts.max_bytes = 64 * 1024;

    auto r = Subprocess::run(std::move(opts));

    CHECK(r.started);
    CHECK_MESSAGE(!r.timed_out,
                  "progress keeps rolling the idle window forward");
    CHECK_MESSAGE(!r.hard_capped,
                  "and the derived ceiling (20x idle, 10 min floor) is "
                  "nowhere near a job like this");
    CHECK(r.exit_code == 0);
    CHECK(r.output.find("step 12") != std::string::npos);
}

TEST_CASE("subprocess: the derived ceiling is generous, not tight") {
    // 20x闲置预算, 10分钟就铺平了。 这是因为它从来不会向
    // 真正的工作开火 -- 如果有人调调下这个调子
    // "something reasonable" 长会开始死亡。
    SubprocessOptions opts;
    opts.command   = SubprocessOptions::Shell{"echo quick"};
    opts.timeout   = std::chrono::seconds{60};   // 常见默认
    opts.max_bytes = 4096;
    auto r = Subprocess::run(std::move(opts));
    CHECK(r.started);
    CHECK_FALSE(r.hard_capped);
    // 闲置的60s将得出最大值(1 200s,600s)=20分钟。
    static_assert(60 * 20 > 600, "the floor must not be the binding term here");
}
#endif
