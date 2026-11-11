// exec_policy_test——两钟,钉住.
//
// ── 原因 ──────────────────────────────────────────────────────────────────
//
// 此会话以一个永远运行的命令开始。 超时 是个 IDLE 看门狗 ,
// 每张重置 字节 ,所以任何能保持印刷的 东西,都会让自己活下来;
// 过了输出封面,读者就会丢弃它读到的东西,所以 内存
// 也没有绑起来,一个 `yes` 留下一个旋转的过程和
// 一个卡住了 工作线程 其余时间。
//
// 固定的号码并不大。沉默和总计 运行时 是一个不同的问题,
// 需要单独的时钟 :
//
//   由输出闲置重置。 捕获 HANG。 无法捕获聊天的运行,
//         因为输出正是它能活着的。
//   墙壁永远无法重设, 只能抓住逃跑的路程, 必须是慷慨的,
//         否则它会照常发生的建筑来射击。
//
// 两者都一样,或者两者都无效。这是这些检查要保存的属性,
// 因为单读时钟就看不见,而且很明显 "simplification"
// - 一个 超时 ——静静地恢复了错误.
//
// 另一半是诚实 对应的 钟点响了。 "Timed out"
// 命令完成后,你就会去寻找绞刑 需要更长的时间,
// 所以结果就把预算命名了, 并且两人得到相反的建议。
#include "agtest.hpp"

#include "agentty/tool/util/exec.hpp"
#include "agentty/tool/util/sandbox.hpp"
#include "agentty/tool/tool.hpp"
#include "agentty/tool/registry.hpp"

#include <nlohmann/json.hpp>
#include <csignal>
#include <cstdio>
#include <unistd.h>
#include <fcntl.h>

#include <filesystem>

#include <chrono>
#include <string>
#include <thread>
#include <variant>

using namespace std::chrono_literals;
namespace mt = mcp::tools;
using json = nlohmann::json;

namespace {

// 注意这有什么作用 不 需要:无 jaal 头文件。
// 该能力在 mcp-cpp 词汇表并执行完毕 jaal 后面的 接口边界 ,
// 所以一个呼叫者——包括这个测试—— 从来没有看到平台层。
std::shared_ptr<mt::Exec> exec() {
    static auto e = agentty::tools::util::make_exec();
    return e;
}

mt::ExecResult sh(std::string script, mt::Budgets b) {
#if defined(_WIN32)
    // 月台外壳 Windows 谓 cmd.exe (什么壳的工具和
    // process_start 因此,下面的脚本有: cmd.exe 此处的拼写 :
    // `ping -n N 127.0.0.1 >nul` 睡觉~N-1s, `for /l %i in (0,0,1)`
    // 永远循环, `<nul set /p "=x"` 打印时没有新行。
    return exec()->run(mt::Call{}, {.program = {"cmd.exe", {"/S", "/C", std::move(script)}},
                        .budgets = b,
                        .max_output_bytes = 64u * 1024u});
#else
    return exec()->run(mt::Call{}, {.program = {"/bin/sh", {"-c", std::move(script)}},
                        .budgets = b,
                        .max_output_bytes = 64u * 1024u});
#endif
}

bool timed_out_on(const mt::ExecResult& r, mt::TimedOut::budget which) {
    const auto* t = std::get_if<mt::TimedOut>(&r.outcome);
    return t && t->which == which;
}

} // 命名空间结束。

TEST_CASE("exec: a chatty runaway is reaped by the wall clock") {
    // 该 错误。 输出时钟会永远重置, 所以只有绝对的
    // 上限才能结束。 没有一个 调用 永远不回来。
    const auto t0 = std::chrono::steady_clock::now();
#if defined(_WIN32)
    auto r = sh("for /l %i in (0,0,1) do @echo spin", {.idle = 30s, .wall = 2s});
#else
    auto r = sh("while :; do echo spin; done", {.idle = 30s, .wall = 2s});
#endif
    const auto elapsed = std::chrono::steady_clock::now() - t0;

    CHECK_MESSAGE(timed_out_on(r, mt::TimedOut::budget::wall),
                  "a runaway that never goes quiet can only be caught by the "
                  "clock that output does not reset");
    CHECK_MESSAGE(elapsed < 15s, "reaped at its own ceiling, not the idle one");
    CHECK_MESSAGE(r.truncated,
                  "it blew the output cap -- and the cap must not be what "
                  "stops the reaping");
}

TEST_CASE("exec: a long chatty job is NOT cut off") {
    // 方便保证,而闲置时钟的理由是默认而不是墙钟。
    // 这里的运行时超过闲置预算数倍;
    // 进步是它维持生命的,
    // 这是正确的。
#if defined(_WIN32)
    // cmd.exe 没有子 -second 睡眠不足~300毫秒(每分钟)
    // PowerShell 开始,所以每一步睡眠~1s(正) -n 2),
    // 闲置预算为3s而不是1s. 财产不变:共计. 运行时 (~6s)是
    // 闲置预算的两倍,只有输出在两者之间才能保持其生命.
    auto r = sh("for /l %i in (1,1,6) do @(echo step%i& ping -n 2 127.0.0.1 >nul)",
                {.idle = 3s, .wall = 60s});
#else
    auto r = sh("for i in 1 2 3 4 5 6; do echo step$i; sleep 0.2; done",
                {.idle = 1s, .wall = 60s});
#endif

    CHECK_MESSAGE(r.ok(), "progress keeps rolling the idle window forward");
    CHECK_MESSAGE(r.output.find("step6") != std::string::npos,
                  "and every byte it printed survived");
}

TEST_CASE("exec: a silent hang is caught by the idle clock") {
#if defined(_WIN32)
    auto r = sh("ping -n 31 127.0.0.1 >nul", {.idle = 1s, .wall = 60s});
#else
    auto r = sh("sleep 30", {.idle = 1s, .wall = 60s});
#endif
    CHECK(timed_out_on(r, mt::TimedOut::budget::idle));
    CHECK_FALSE(timed_out_on(r, mt::TimedOut::budget::wall));
}

TEST_CASE("exec: which budget fired is part of the answer") {
    // 两人需要相反的建议 - "it hung, go look at why" 相对
    // 原本运行正常，可以多等一会或转到后台执行。
    // 单人 timed_out 标志 强迫呼叫者猜测,
    // 它猜错一半的时间。
#if defined(_WIN32)
    auto hung    = sh("ping -n 31 127.0.0.1 >nul", {.idle = 1s, .wall = 60s});
    auto runaway = sh("for /l %i in (0,0,1) do @echo x", {.idle = 30s, .wall = 2s});
#else
    auto hung    = sh("sleep 30", {.idle = 1s, .wall = 60s});
    auto runaway = sh("while :; do echo x; done", {.idle = 30s, .wall = 2s});
#endif

    const auto* a = std::get_if<mt::TimedOut>(&hung.outcome);
    const auto* b = std::get_if<mt::TimedOut>(&runaway.outcome);
    REQUIRE(a);
    REQUIRE(b);
    CHECK_MESSAGE(a->which != b->which,
                  "two different failures must not report as one");
}

TEST_CASE("exec: output printed before the exit survives it") {
    // 出口说 字节 已经写完了,不是说我们有它们:
    // 在它还在烟斗里之前写的东西。 jaal 符合
    // 性套件为平台贴上这个;这个贴上政策层尊重它.
#if defined(_WIN32)
    auto r = sh("<nul set /p \"=last-bytes\"& exit /b 3", {.idle = 5s, .wall = 30s});
#else
    auto r = sh("printf 'last-bytes'; exit 3", {.idle = 5s, .wall = 30s});
#endif

    const auto* e = std::get_if<mt::Exited>(&r.outcome);
    REQUIRE(e);
    CHECK(e->code == 3);
    CHECK(r.output == "last-bytes");
    CHECK_FALSE(r.ok());
}

TEST_CASE("exec: a program that never ran is not a program that failed") {
    // 赤裸的退出代码不能表示区别,结果的原因就是总和.
    // "127" 可能是贝壳不是 -found 或程序本身的选择;
    // StartFailed 两者都不可能
    auto r = exec()->run(mt::Call{}, {.program = {"/definitely/not/a/program", {}}});

    CHECK(std::holds_alternative<mt::StartFailed>(r.outcome));
    CHECK_FALSE(r.ok());
    const auto* f = std::get_if<mt::StartFailed>(&r.outcome);
    REQUIRE(f);
    CHECK_MESSAGE(!f->reason.empty(), "and it says why");
}

TEST_CASE("exec: a clean command reports no timeout at all") {
    // 负面的个案,所以一个错误,每一次报告一次时间值为定时-out,
    // 不会隐藏在以上检查的背后.
#if defined(_WIN32)
    // `set /p` 时间 EOF 离开错误级别 1, 因此是明确的干净退出。
    auto r = sh("<nul set /p \"=ok\"& exit /b 0", {.idle = 5s, .wall = 30s});
#else
    auto r = sh("printf 'ok'", {.idle = 5s, .wall = 30s});
#endif
    CHECK(r.ok());
    CHECK_FALSE(std::holds_alternative<mt::TimedOut>(r.outcome));
    CHECK_FALSE(r.truncated);
    CHECK(r.output == "ok");
}

// ── 沙盒路径 ──────────────────────────────────────────────────
//
// 上面的检查可以锻炼直接产卵。 另一条是: 使用命名
// 空间标志的claybin克隆,安装seccomp过滤器
// 并进入一个c组,然后是 jaal ADOPTS 将它
// 手伸回来。 在那条线之后,两条路径是同一个对象,
// 所以需要证明的是, 手-off 完全有效,
// 并且政策仍然适用于上面, 因为一个沙箱 无法观察退出,
// 将有理由保持第二个循环。
//
// 跳过无法建 沙箱 的地方, 因为一个无声的通过一个未
// 关闭的主机的检查比没有检查更糟糕。
TEST_CASE("exec: the sandboxed path spawns, is watched, and still obeys the clocks") {
    namespace sb = agentty::tools::util::sandbox;
    if (!sb::init(sb::Mode::On) || !sb::is_active()) return;

    // 壳是平台的:沙箱合同不是"linux only",
    // 而是"whatever shell this host runs the command with".
#if defined(_WIN32)
    const std::vector<std::string> echo_cmd = {"cmd.exe", "/d", "/s", "/c", "echo inside"};
    const std::vector<std::string> spin_cmd = {"cmd.exe", "/d", "/s", "/c",
                                               "for /l %i in () do @echo s"};
#else
    const std::vector<std::string> echo_cmd = {"/bin/sh", "-c", "echo inside"};
    const std::vector<std::string> spin_cmd = {"/bin/sh", "-c", "while :; do echo s; done"};
#endif

    auto e = agentty::tools::util::make_exec();

    // 运行在全程中 -- -- 即 adopt() 手执 -off 工作,我们的工作。
    auto ran = e->run(mt::Call{}, {.program = {echo_cmd[0], {echo_cmd.begin() + 1, echo_cmd.end()}},
                       .budgets = {.idle = 10s, .wall = 30s}});
    CHECK_MESSAGE(ran.ok(), "a sandboxed command still runs and is reaped");
    CHECK(ran.output.find("inside") != std::string::npos);

    // 2. 出口是可观察的 -- -- 这是adopt()的位值。
    //    被我们看不出来,会像墙一样出现 -clock 超时
    //    而不是干净的退出,所以 ok() 上面已经证明了这一点;
    //    也直接表明了这一点,因为这一推论并不明显。
    CHECK(std::holds_alternative<mt::Exited>(ran.outcome));

    // 墙上钟仍然能收敛出逃 内部 编号 沙箱 闲置
    //    时钟不能(输出持续重置),一个沙盒式的孩子
    //    正是一个未重置的离家出走者最难发现的地方.
    const auto t0 = std::chrono::steady_clock::now();
    auto runaway = e->run(mt::Call{}, {.program = {spin_cmd[0], {spin_cmd.begin() + 1, spin_cmd.end()}},
                           .budgets = {.idle = 30s, .wall = 2s},
                           .max_output_bytes = 32u * 1024u});
    const auto elapsed = std::chrono::steady_clock::now() - t0;

    CHECK(timed_out_on(runaway, mt::TimedOut::budget::wall));
    CHECK_MESSAGE(elapsed < 15s, "reaped at its ceiling, not left spinning");
}

TEST_CASE("exec: node runs sandboxed under the default policy") {
    // V8 需求 RWX 页面。严格a JIT 命令获取 W ^X 解除 关闭。
    namespace sb = agentty::tools::util::sandbox;
#if defined(__linux__)
    if (::access("/usr/bin/node", X_OK) != 0) return;
    if (!sb::init(sb::Mode::On) || !sb::is_active()) return;
    auto e = agentty::tools::util::make_exec();
    auto r = e->run(mt::Call{}, {.program = {"/bin/sh", {"-c", "node -e 'console.log(6*7)'"}},
                                 .budgets = {.idle = 20s, .wall = 30s}});
    CHECK_MESSAGE(r.output.find("42") != std::string::npos, "node said: " << r.output);
    // 且非 国家 -JIT 命令仍然得到 RWX 墙壁。
    auto w = e->run(mt::Call{}, {.program = {"/bin/sh", {"-c",
                     "python3 -c 'import mmap; mmap.mmap(-1,4096,prot=7); print(\"MAPPED\")' 2>&1"}},
                                 .budgets = {.idle = 20s, .wall = 30s}});
    CHECK_MESSAGE(w.output.find("PermissionError") != std::string::npos, "python got RWX: " << w.output);
#endif
}

TEST_CASE("exec: the request's env reaches a sandboxed child") {
    // 用于不带儿童的设置工具 -interactive (GIT_EDITOR 是真的,
    // 我对这个很感兴趣 PAGER 猫, GIT_TERMINAL_PROMPT =0),沙盒产卵用来降下.
    namespace sb = agentty::tools::util::sandbox;
    if (!sb::init(sb::Mode::On) || !sb::is_active()) return;
    auto e = agentty::tools::util::make_exec();
#if defined(_WIN32)
    auto r = e->run(mt::Call{}, {.program = {"cmd.exe", {"/d", "/s", "/c", "echo %AGT_PROBE%"}},
                                 .env = {{"AGT_PROBE", "from-request"}},
                                 .budgets = {.idle = 10s, .wall = 20s}});
    CHECK(r.ok());
    CHECK_MESSAGE(r.output.find("from-request") != std::string::npos,
                  "child saw: " << r.output);
#else
    auto r = e->run(mt::Call{}, {.program = {"/bin/sh", {"-c", "printf '%s|%s' \"$AGT_PROBE\" \"${PATH:+path}\""}},
                                 .env = {{"AGT_PROBE", "from-request"}},
                                 .budgets = {.idle = 10s, .wall = 20s}});
    CHECK(r.ok());
    // 分层在母巢上,而不是它: PATH存活.
    CHECK_MESSAGE(r.output == "from-request|path", "child saw: " << r.output);
#endif
}

// 沙箱方案下的背景会议使一个经中介的syscall(杀).
// 内核会把客人停在等候主管回答的地方 所以
// 没有人能通过这个会话
TEST_CASE("exec: a sandboxed background session gets its brokered syscalls answered") {
    namespace sb = agentty::tools::util::sandbox;
#if !defined(__linux__)
    // 经纪人是 seccomp-notify : linux 键 -only
    // 窗口上 沙箱 报告 syscall.filter 以 `partial`
    // (缓解政策,没有可编程过滤器) 且没有收听者服务——python3
    // 's kill() 运行时没有中介,测试的前提不适用.
    return;
#else
    if (!sb::init(sb::Mode::On) || !sb::is_active()) return;
    if (sb::config_snapshot()->syscall_mode == agentty::sandbox_cfg::SyscallMode::Off)
        return;   // 没有过滤器,所以没有进行代理

    auto e = agentty::tools::util::make_exec();
    // 给自己和自己孩子的信号: 普通的建构或 `timeout`
    // 制造的。 经纪人从客人自己的 pid 中决定两者。
    // 命名空间内的编号，而非宿主 PID。
    const std::string cmd =
        "python3 -c 'import os,subprocess; os.kill(os.getpid(), 0); "
        "c = subprocess.Popen([\"sleep\", \"5\"]); os.kill(c.pid, 15); c.wait()' "
        " && echo after-kill";
    // 前景第一:运行环路服务 经纪人.
    auto fg = e->run(mt::Call{}, {.program = {"/bin/sh", {"-c", cmd}},
                                  .budgets = {.idle = 10s, .wall = 20s}});
    CHECK_MESSAGE(fg.output.find("after-kill") != std::string::npos, "foreground: " << fg.output);

    auto s = e->start(mt::Call{}, {.program = {"/bin/sh", {"-c", cmd}}});
    REQUIRE(s.has_value());

    std::string out;
    bool ended = false;
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < 10s) {
        auto r = (*s)->read(200ms, 64 * 1024);
        out += r.output;
        if (!r.running && r.eof) { ended = true; break; }
    }
    (*s)->stop();
    CHECK_MESSAGE(out.find("after-kill") != std::string::npos, "the guest got past kill: " << out);
    CHECK_MESSAGE(ended, "and exited on its own");
#endif  // __linux__
}

// ── 开会:开始于这里,稍后进行投票 ───────────────────────────────
//
// process_start/poll/stop 曾经
// 拥有一个 线程 每个会话和产物都通过与其它工具不同的
// 代码路径——在brap除去后意味着一个背景过程没有被任何限制,
// 而一个前缘过程则被限制在 claybin .
// 现在它通过同样的能力, 所以有一个边界和一个地方 懂得
// 如何看孩子。
//
// 开车穿过真实的 注册表 而不是直接具备的能力:
// 重要的是,这三种工具仍然在构成,因为 生命周期 横跨
// 三个独立区域 调用.
TEST_CASE("process session: start, poll, stop through the registry") {
#if defined(_WIN32)
    constexpr const char* kTicks =
        "for /l %i in (1,1,3) do @(echo tick%i& ping -n 1 127.0.0.1 >nul)& echo done";
#else
    constexpr const char* kTicks = "for i in 1 2 3; do echo tick$i; sleep 0.15; done; echo done";
#endif
    auto start = agentty::tool::DynamicDispatch::execute(
        agtest::cap(), "process_start",
        json{{"command", kTicks}});
    if (!start) MESSAGE("process_start failed: " << start.error().detail);
    REQUIRE(start.has_value());

    const auto at = start->text.find("proc-");
    REQUIRE(at != std::string::npos);
    std::string id = start->text.substr(at);
    id = id.substr(0, id.find_first_of(" )\n"));

    // 投票直到结束或者我们没有耐心。会话持续产生~
    // 450ms,所以一些民意测验应该看到产出,然后结束。
    std::string seen = start->text;
    bool ended = false;
    for (int i = 0; i < 40 && !ended; ++i) {
        auto p = agentty::tool::DynamicDispatch::execute(
            agtest::cap(), "process_poll", json{{"id", id}, {"wait_ms", 100}});
        REQUIRE(p.has_value());
        seen += p->text;
        if (p->text.find("exited") != std::string::npos
            || p->text.find("not running") != std::string::npos) ended = true;
    }

    CHECK_MESSAGE(seen.find("tick1") != std::string::npos,
                  "output produced between polls must survive to the next one "
                  "-- the host drains continuously so a full pipe never "
                  "stalls the child");
    CHECK_MESSAGE(seen.find("done") != std::string::npos,
                  "and the tail written just before exit is not lost");

    // 停止一个已经的-finished 会话不是一个错误。
    auto stop = agentty::tool::DynamicDispatch::execute(
        agtest::cap(), "process_stop", json{{"id", id}});
    CHECK(stop.has_value());
}

// 产卵工具不能继承主机的文件描述符.
//
// 访问权限附加到打开的文件 DESCRIPTION ,
// 而不是路径,所以一个描述器 主持人持有打开的 执行和无效 沙
// 箱 '文件政策:孩子可以读取它被拒绝的文件,甚至一个不再有路径的文件.
// agentty 持有 凭据 商店和日志在运行工具时打开,
// 所以执行官必须关闭除了孩子自己的三件之外的一切.
// (从 mcp-cpp's fd_leak_test 当产卵移到 agentty.)
#if defined(__linux__)
TEST_CASE("exec: a child cannot read the host's open descriptors") {
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path() / ("fd_leak_" + std::to_string(::getpid()));
    fs::create_directories(dir);
    const auto secret = dir / "secret.txt";
    {
        std::FILE* f = std::fopen(secret.c_str(), "w");
        REQUIRE(f);
        std::fputs("FLAG{fd-inherited}\n", f);
        std::fclose(f);
    }
    // 产卵前打开的 故意不打开 O_CLOEXEC 站着呆了很久
    // -lived 机主机FD我们控制不了。
    const int fd = ::open(secret.c_str(), O_RDONLY);
    REQUIRE(fd >= 0);
    const std::string peek = "cat /proc/self/fd/" + std::to_string(fd) + " 2>/dev/null; true";

    auto r = sh(peek, mt::Budgets{10s, 10s});
    CHECK_MESSAGE(r.output.find("FLAG{fd-inherited}") == std::string::npos,
                  "child read an inherited fd");

    // 尖锐的情节:没有关联,所以没有路径规则可以命名.
    fs::remove(secret);
    auto r2 = sh(peek, mt::Budgets{10s, 10s});
    CHECK_MESSAGE(r2.output.find("FLAG{fd-inherited}") == std::string::npos,
                  "child read an UNLINKED file through an inherited fd");

    // 孩子自己的描述仍然有效: Stdout联系我们,
    // Stdin也在 EOF 而不是吊(扫荡关闭) 0/1/2
    // 将打破每个工具,通过上面的检查。
    auto r3 = sh("echo alive; cat; echo done", mt::Budgets{10s, 10s});
    CHECK(r3.output.find("alive") != std::string::npos);
    CHECK(r3.output.find("done")  != std::string::npos);

    ::close(fd);
    std::error_code ec;
    fs::remove_all(dir, ec);
}
#endif

// 运行中的工具输出到达 UI 因为它是生产的, Esc 都别这样,
// 两趟都去 调用's CallContext ;
// 执行器必须尊重它们。 从10月7日到测试存在为止,
// 它都没有: 弹壳输出只到达尾端, Esc 等待命令自己完成.
TEST_CASE("exec: a tool call streams progress and stops on cancel") {
    using agentty::tool::DynamicDispatch;
    int calls = 0;
    {
        agentty::tools::CallContext ctx;
        ctx.progress = [&](std::string_view) { ++calls; };
#if defined(_WIN32)
        // ~1s 相隔( 平 -n 2) 所以每个勾点都是单独的进度 事件.
        constexpr const char* kStream =
            "for /l %i in (1,1,3) do @(echo tick%i& ping -n 2 127.0.0.1 >nul)";
#else
        constexpr const char* kStream = "for i in 1 2 3; do echo tick$i; sleep 0.2; done";
#endif
        auto r = DynamicDispatch::execute(
            agtest::cap(), "shell", json{{"command", kStream}}, ctx);
        CHECK(r.has_value());
    }
    CHECK_MESSAGE(calls >= 3, "output streamed while the command ran");

    std::stop_source src;
    std::jthread trip([&] { std::this_thread::sleep_for(300ms); src.request_stop(); });
    const auto t0 = std::chrono::steady_clock::now();
    {
        agentty::tools::CallContext ctx;
        ctx.cancel.push_back(src.get_token());
#if defined(_WIN32)
        constexpr const char* kLong = "ping -n 21 127.0.0.1 >nul& echo done";
#else
        constexpr const char* kLong = "sleep 20; echo done";
#endif
        (void)DynamicDispatch::execute(agtest::cap(), "shell", json{{"command", kLong}}, ctx);
    }
    const auto elapsed = std::chrono::steady_clock::now() - t0;
    std::fprintf(stderr, "[cancel-test] execute took %lld ms\n",
                 static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()));
    CHECK_MESSAGE(elapsed < 5s, "Esc stopped the command");
}
