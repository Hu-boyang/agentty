#include "agentty/tool/util/subprocess.hpp"
#include "agentty/tool/registry.hpp"
#include "agentty/tool/util/utf8.hpp"
#include "agentty/io/fsm.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <string>
#include <vector>

#include "agentty/tool/util/exec.hpp"   // run_child: 监督环线的
#include <maya/runtime.hpp>                // 窗口命令-line 帮助

namespace agentty::tools::util {

namespace {

// 终端 线条 -discipline + UTF - 8次修理,
// 适用于 每个 字节 使子进程运行者——实时进程 快照 和最终捕获
// 输出相同。活路是用来泄露原始逃生的路径: 击打进度 快照 直接
// 去了那个 UI 牌,所以一个孩子认为它拥有一个tty(ls)
// --color 货物,顶部 -b 任何调查 DECSTBM/SGR
// ) 涂上其图案 CSI 参数 字节 作为字面格( 字母)
// "[1;24r" □ 流浪汉 "r" 存储单元 ),之后承诺本土
// 回滚缓冲区 ——报告的每笔 -frame 使用打击工具时的腐败。
std::string clean_capture(std::string s) {
    return to_valid_utf8(strip_terminal_controls(s));
}

} // 命名空间结束。

namespace {

// 每个孩子一个监督循环 agentty 运行 : run_child (exec.cpp
// 继续 jaal 本地进程。 仅此选项会覆盖整个区域。 `win_cmdline`
// 设定时,是逐字记录 Windows 命令行。
SubprocessResult run_via_child(std::vector<std::string> argv, const SubprocessOptions& opts,
                               std::string win_cmdline = {}) {
    ChildRun run;
    run.argv = std::move(argv);
    run.windows_command_line = std::move(win_cmdline);
    run.idle = opts.timeout;
    run.wall = [&]() -> std::chrono::seconds {
        // 整个过程的$AGENTTY_TOOL_HARD_TIMEOUT_SECS超载;
        // 0切换天花板.
        if (const char* e = std::getenv("AGENTTY_TOOL_HARD_TIMEOUT_SECS"); e && e[0]) {
            char* end = nullptr;
            const long v = std::strtol(e, &end, 10);
            if (end != e && v >= 0) return std::chrono::seconds{v};
        }
        if (opts.hard_timeout.count() > 0) return opts.hard_timeout;
        if (opts.timeout.count() <= 0)     return std::chrono::seconds::zero();
        return std::max<std::chrono::seconds>(opts.timeout * 20, std::chrono::minutes{10});
    }();
    run.max_output_bytes = opts.max_bytes;
    run.stop_requested   = opts.stop_requested;
    if (opts.on_progress)
        run.on_progress = [&opts](std::string_view raw) { opts.on_progress(clean_capture(std::string{raw})); };
#if !defined(_WIN32)
    if (opts.spawner) {
        run.spawn_adopted = [&opts](int out_fd) {
            auto c = opts.spawner(opts, out_fd);
            AdoptedChild a;
            a.pid   = c.pid;
            a.pidfd = c.pidfd;
            if (c.pid < 0) a.error = c.error.empty() ? "sandbox spawner failed" : c.error;
            a.supervisor_fd  = c.supervisor_fd;
            a.service_broker = std::move(c.service);
            a.keepalive      = std::move(c.keepalive);
            a.kill_tree      = std::move(c.kill_tree);
            return a;
        };
    }
#endif

    auto c = run_child(run);
    SubprocessResult r;
    r.started     = c.started;
    r.start_error = c.start_error;
    r.output      = clean_capture(std::move(c.output));
    r.truncated   = c.truncated;
    r.timed_out   = c.timed_out_idle || c.timed_out_wall;
    r.hard_capped = c.timed_out_wall;
    r.exit_code   = c.exited ? c.exit_code : 128 + c.signal;
    return r;
}

} // 命名空间结束。

SubprocessResult Subprocess::run(SubprocessOptions opts) {
    SubprocessResult r;

    // 构建适合本平台的终极命令行.
#ifdef _WIN32
    namespace pf = maya::platform;
    std::string cmdline;
    if (const std::string* sh = opts.shell_if()) {
        cmdline = pf::cmd_command_line(*sh);
    } else {
        const std::vector<std::string>& av = *opts.argv_if();
        if (av.empty()) {
            r.started = false; r.start_error = "empty command"; return r;
        }
        // CreateProcess 无法启动 .cmd/.bat (npx,npm,纱);这些经过.
        // cmd.exe.
        cmdline = pf::join_command_line(av);
        if (pf::resolves_to_batch(av[0])) cmdline = pf::cmd_command_line(cmdline);
    }
    // 无跟踪 "no command specified" 臂: 手提键 变体 有两个选择,
    // 所以 shell_if () 无效 argv_if () 否。 (中文)
    const std::string prog = opts.shell_if() ? std::string{"cmd.exe"} : (*opts.argv_if())[0];
    return run_via_child({prog}, opts, std::move(cmdline));
#else
    // 完全有两个选择,所以一个IS的无效检查是另一个:
    // 不存在 "neither form set" 臂可以写入, 因为此状态
    // 已经不存在。 旧的链条以 `"no command specified"`
    // 运行时 错误结束, 因为它的条件类型现在排除了。
    if (const std::string* sh = opts.shell_if()) {
        // Shell 窗体: 将整个命令串作为单 sh 传出去
        // -c 参数。 运行者设置了 sh "-c"
        // "<cmd>" 本身;不需要额外的引用(我们不希望有任何
        // ——用户通过 shell 语法期望它被解析为逐字).
        return run_via_child({"sh", "-c", *sh}, opts);
    }
    const std::vector<std::string>& av = *opts.argv_if();
    if (av.empty()) {
        r.started = false; r.start_error = "empty command"; return r;
    }
    // argv形式: 直接执行, 循环中没有 shell。
    // 保存每个 arg 的每 字节, 这是像 git_commit (
    // 承诺 的 $vars / 引文 / 新的行) 这样的调用者实际需要的。
    return run_via_child(av, opts);
#endif
}

// ── 方便包装器 ────────────────────────────────────────────────
//
// 用于 agentty 自己的助手( 用于检查点、 键盘、 钩子)。
// 没有现场进展 : 这些不是工具 调用 所以没人看卡片

SubprocessResult run_command_s(const std::string& cmd,
                               std::size_t max_bytes,
                               std::chrono::seconds timeout) {
    SubprocessOptions opts;
    opts.command     = SubprocessOptions::Shell{cmd};
    opts.max_bytes   = max_bytes;
    opts.timeout     = timeout;
    return Subprocess::run(std::move(opts));
}

SubprocessResult run_argv_s(const std::vector<std::string>& argv,
                            std::size_t max_bytes,
                            std::chrono::seconds timeout) {
    SubprocessOptions opts;
    opts.command     = SubprocessOptions::Argv{argv};
    opts.max_bytes   = max_bytes;
    opts.timeout     = timeout;
    return Subprocess::run(std::move(opts));
}

std::string legacy_format(const SubprocessResult& r, std::chrono::seconds timeout) {
    if (!r.started) return "[" + r.start_error + "]";
    std::string o = r.output;
    if (r.truncated) o += "\n[output truncated]";
    if (r.hard_capped) {
        // 停止生产时它仍在生产输出, 所以下一步有用
        // 的是更多的时间或背景会议-- 不是寻找
        // "timed out"会发送模型的吊销。
        o += "\n[stopped at the wall-clock ceiling while still producing "
             "output. It was not stuck. Re-run with a larger `timeout`, or "
             "start it with `process_start` and poll it, which is the right "
             "shape for anything long-running.]";
    } else if (r.timed_out) {
        o += "\n[no output for " + std::to_string(timeout.count())
           + "s, so it was stopped. The clock measures SILENCE, not total "
             "runtime -- a long build that keeps printing is never cut off. "
             "If it is legitimately quiet for a while, raise `timeout`.]";
    } else if (r.exit_code != 0) {
        o += "\n[exit code " + std::to_string(r.exit_code) + "]";
    }
    return o;
}

std::string run_command(const std::string& cmd,
                        std::size_t max_bytes,
                        std::chrono::seconds timeout) {
    return legacy_format(run_command_s(cmd, max_bytes, timeout), timeout);
}

std::string run_argv(const std::vector<std::string>& argv,
                     std::size_t max_bytes,
                     std::chrono::seconds timeout) {
    return legacy_format(run_argv_s(argv, max_bytes, timeout), timeout);
}

} // namespace agentty::tools::util
