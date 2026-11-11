// cmd_code_block_run.cpp-Ctrl+G代码-block流的EFFECT一半。
//
// update/code_blocks.cpp是状态更新函数：它决定WHICH块
// 运行并返回cmd::run_code_block(...)。该文件是 运行时 运行
// 时 Cmd 所做的事情 - 带有 T 恤的真实 tty 上的 fork/exec
// (POSIX)，或捕获的子进程运行程序 (Windows)。它位于 状态更新函数
// 文件内，该文件在 update/ 下放置了约 500 行 fork、
// termios、信号和时钟读取：不是从 归并 调用（它始终作为任务运行），但可读，
// 就好像它是一样，并且对 elm_purity lint 的文件边界不可见。
//
// 除了通过Cmd之外，无法到达这里。请参阅 update/code_blocks.cpp
// 的用户-facing 设计（交互式-first，仅根据请求捕获
// 到作曲家，Windows 降级）。

#include "agentty/runtime/app/cmd_code_block_run.hpp"

#include "agentty/runtime/app/update/internal.hpp"

#if !defined(_WIN32)
    #include <cerrno>
    #include <csignal>
    #include <cstdio>
    #include <cstring>
    #include <ctime>
    #include <poll.h>
    #include <sys/ioctl.h>
    #include <termios.h>
    #include <unistd.h>
    #include <maya/runtime.hpp>
#else
    #include <chrono>
    #include <cstdio>
    #include <io.h>
    #include <stop_token>
    #include <maya/runtime.hpp>
#endif

#include <string>
#include <utility>

#include <maya/style/theme.hpp>

#include "agentty/runtime/panel/code_blocks.hpp"
#include "agentty/runtime/win_shell_encode.hpp"
#include "agentty/tool/util/subprocess.hpp"

namespace agentty::app::cmd {

namespace cbp = agentty::code_blocks;

namespace {
// 真实的 终端 与 stdout/stderr T 恤 — 完全交互，捕获输出。
// Windows：non-interactive 捕获的运行程序（子进程），
// 与 bash 工具相同。
#if !defined(_WIN32)

// 在 真实 终端 上通过 /bin/sh -c 运行 `command`（
// 我们在 maya 的挂起内：cooked tty，TUI 逃逸被拆除）。
// stdin 是继承的——实际的 tty——所以行编辑/密码读取行为；
// 无论哪种方式，sudo 都会直接与 /dev/tty 对话。 stdout
// +stderr 被 dup2'd 到父 tee 的管道上：每个读取块
// 都直接写入 tty（实时输出）并附加到捕获 缓冲区。返回完成的 Msg。
// 小助手，因此无论标准输出是否是真正的 终端，成绩单看起来都相同。
// Colour/heartbeat 是 TTY-only（`agentty ... | tee`
// 的管道输出保持干净），但命令 echo + status 始终打印。
namespace runner_ui {
    // 这里的颜色不能贯穿主题，这不是疏忽：
    // maya此时是SUSPENDED（熟的tty，TUI被拆掉）
    // 并且我们直接在终端处写入字节，所以没有画布，没有样式池并且
    // 没有帧可以绘制。正是出于这个原因，这些是 agentty 中
    // 唯一的 hand-written 逃脱。
    //
    // 他们MUST仍然尊重的是用户是否想要颜色。
    // 这曾经是一个纯粹的 isatty() 检查，它默默地忽略了 NO_COLOR
    // 和 TERM=哑巴——所以 `NO_COLOR=1 agentty`
    // 在除了这里之外的任何地方都保持干净，这是用户无法主题化的地方。
    // maya::theme::detect_tier()与渲染器使用
    // 相同的检测器，因此答案在整个挂起边界上是一致的。
    inline bool tty() {
        static const bool on = ::isatty(STDOUT_FILENO) == 1
            && maya::theme::detect_tier(/*tty=*/true)
                   != maya::theme::ColorTier::Mono;
        return on;
    }
    inline const char* dim()   { return tty() ? "\x1b[2m"  : ""; }
    inline const char* bold()  { return tty() ? "\x1b[1m"  : ""; }
    inline const char* green() { return tty() ? "\x1b[32m" : ""; }
    inline const char* red()   { return tty() ? "\x1b[31m" : ""; }
    inline const char* yellow(){ return tty() ? "\x1b[33m" : ""; }
    inline const char* cyan()  { return tty() ? "\x1b[36m" : ""; }
    inline const char* reset() { return tty() ? "\x1b[0m"  : ""; }
    inline void emit(const std::string& s) {
        (void)!::write(STDOUT_FILENO, s.data(), s.size());
    }
    // 清除当前行（用于在实际输出或最终状态到达其顶
    // 部之前擦除in-place心跳）。
    inline void clear_line() { if (tty()) emit("\r\x1b[2K"); }
    // 设置/恢复终端窗口标题(OSC 2)。这是标题栏中的
    // 始终-ON elapsed-time 读数，
    // 因此即使输出是 流式传输，它也会每秒更新一次，而不会触及
    // 回滚缓冲区 区域。 No-op 关闭 TTY。
    inline void set_title(const std::string& t) {
        if (tty()) emit("\x1b]2;" + t + "\x07");
    }
    inline void restore_title() { if (tty()) emit("\x1b]2;\x07"); }

    // ── 压在底部 -line 状态( 4) DECSTBM) ──────────────────────────────
    // 用于terminals/multiplexers的通用回退，
    // 其删除OSC-2窗口标题：保留底部屏幕行作为固定状态行，
    // 使得经过的-time读数即使在输出数据流时也是可见的ALWAYS。
    // 由tee循环写入的输出在第1..h-1行（DECSTBM边距）内滚动；
    // 我们从 重绘 排 h 出来 -of-band。这里的每个转义都是
    // TTY-gated 并被 end_status() 完全拆除，
    // 因此管道输出和捕获的 缓冲区 永远看不到任何它。
    inline int term_rows() {
        struct winsize ws{};
        if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 2)
            return ws.ws_row;
        return 0;   // 未知/太小——呼叫者禁用状态行
    }
    // Enter状态模式：将滚动区域设置为行1..
    // (rows-1)，保留行`rows`。光标停在滚动区域的顶部。
    inline void begin_status(int rows) {
        if (!tty() || rows < 3) return;
        std::string s;
        s += "\x1b[1;" + std::to_string(rows - 1) + "r";  // DECSTBM保证金
        s += "\x1b[" + std::to_string(rows - 1) + ";1H";  // 公园光标
        emit(s);
    }
    // 重绘保留底行与`text`(已经样式化)，
    // 然后将光标返回到滚动区域，以便输出继续在上面。
    inline void paint_status(int rows, const std::string& text) {
        if (!tty() || rows < 3) return;
        std::string s;
        s += "\x1b[s";                                    // 保存光标
        s += "\x1b[" + std::to_string(rows) + ";1H";      // 到状态行
        s += "\x1b[2K";                                   // 清除它
        s += text;
        s += "\x1b[u";                                    // 恢复光标
        emit(s);
    }
    // 离开状态模式：将滚动区域重置为全屏并清除保留行，
    // 以便记录结束干净。
    inline void end_status(int rows) {
        if (!tty() || rows < 3) return;
        std::string s;
        s += "\x1b[r";                                    // 重置DECSTBM
        s += "\x1b[" + std::to_string(rows) + ";1H";
        s += "\x1b[2K";
        s += "\x1b[" + std::to_string(rows - 1) + ";1H";
        emit(s);
    }
    // 递给孩子一个干净的屏幕。 maya 的 inline-mode 挂起
    // 仅在拆卸时发出 "\r\n" — 它将光标移动到新行，但留下整个最后的
    // TUI 帧（转录，作曲家，选择面板）仍然绘制 上方 它。运行 头文件
    // + 子输出，然后向上滚动 INTO 到 过期 帧 并重叠它 (
    // 已有文字在输入时发生重叠。
    // 清除可见的 可视区域 并将光标置于主页，
    // 以便子项在空白屏幕上开始。
    //
    // ED2 (\x1b[2J) 仅清除 VIEWPORT； 回滚缓冲区 未受
    // 影响（需要 ED3 / \x1b[3J，我们故意通过 不 发送），
    // 因此用户滚动的对话历史记录保留在本机 回滚缓冲区 中。
    // TTY-gated，所以 piped/captured 输出永远看不到它。
    inline void reset_screen() {
        if (!tty()) return;
        emit("\x1b[2J\x1b[H");   // ED2 清除 可视区域 + 光标主页
    }
}

[[nodiscard]] CodeBlockRunFinished run_on_real_tty(const std::string& command) {
    namespace ui = runner_ui;
    CodeBlockRunFinished fin;
    fin.command = command;

    // 递给孩子一个CLEAN屏幕。 maya 的内联挂起将最后一个
    // TUI 帧 绘制在光标上方；如果没有这个，头文件 和子输出将
    // 向上滚动到其中并重叠。 reset_screen()
    // 清除 可视区域（保留 回滚缓冲区）并恢复光标。
    ui::reset_screen();

    // 框架运行头文件：清晰的-delimited横幅，因此用户可以一眼
    // 看到运行开始的WHERE（TUI刚刚被撕毁，因此裸露的"$ cmd"
    // 线很容易丢失），并且至关重要的是，Ctrl-C停止了运行。
    // 该命令读起来仍然像 shell 提示词，因此记录是 copy
    // -paste-faithful。
    {
        std::string header;
        header += ui::dim();
        // 无前导 \n — reset_screen() 已将光标定位到第 1 行。
        header += "\xe2\x95\xad\xe2\x94\x80 running \xe2\x94\x80 Ctrl-C to stop \xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80";
        header += ui::reset();
        header += "\n";
        header += ui::cyan();
        header += "$ ";
        header += ui::reset();
        header += ui::bold();
        header += command;
        header += ui::reset();
        header += "\n";
        ui::emit(header);
    }

    // 由jaal开始：无继承描述符，子级中的默认信号。
    // 它停留在 OUR 会话中并保留我们的标准输入，
    // 因为它是 tty 上用户 终端: Ctrl-C 上的前台
    // 作业必须到达它，并且交互式命令必须能够读取密钥。
    namespace pf = maya::platform;
    pf::process_spec spec;
    spec.argv         = {"/bin/sh", "-c", command};
    spec.stdin_from   = pf::stream_to::inherit;
    spec.stdout_to    = pf::stream_to::pipe;
    spec.merge_stderr = true;
    spec.new_session  = false;
    auto spawned = pf::posix_process::spawn(spec);
    if (!spawned) {
        fin.output    = std::string{"[failed to start: "} + std::strerror(spawned.error().native) + "]";
        fin.exit_code = -1;
        return fin;
    }
    auto& proc = *spawned;
    const int out_fd  = proc.stdout_handle()->get();
    const int exit_fd = proc.exit_handle().get();

    // 父进程沿用经典 system() 语义：子进程运行期间忽略 INT/QUIT。
    // child 运行，因此 Ctrl+C 仅终止命令，而不终止 agentty。
    struct sigaction ign{}, old_int{}, old_quit{};
    ign.sa_handler = SIG_IGN;
    ::sigaction(SIGINT,  &ign, &old_int);
    ::sigaction(SIGQUIT, &ign, &old_quit);

    // Tee循环：直播到tty，捕获到缓冲区。有界捕获 (2 MB)，
    // 因此失控命令无法 OOM 作曲家 — SCREEN
    // 仍然显示所有内容；只有 缓冲区 停止生长。
    //
    // 我们使用 1s 超时 而不是裸露的阻塞读取来轮询（2），
    // 因此 SILENT 命令（npm install fetching、
    // 网络等待、`sleep`）仍然显示心跳 —
    // "⠿ running… 12s (Ctrl-C to stop)" 就地重
    // 绘 — 而不是冻结的 -looking 屏幕。心跳是TTY-only，
    // 并在即时实际输出或退出状态到达时被擦除，因此它永远不会污染捕获的缓冲区或管道输出。
    constexpr std::size_t kCaptureMax = 2u * 1024 * 1024;
    bool truncated = false;
    char buf[8192];
    const auto started = ::time(nullptr);
    long last_status_secs = -1;
    int  spin = 0;
    static constexpr const char* kSpin[] = {"⣷","⣯","⣟","⡿","⣾","⣽","⣻","⣷"};
    // 计时器的短命令标签（第一标记，被剪裁）。
    std::string label = command.substr(0, command.find_first_of(" \t\n"));
    if (label.size() > 24) label.resize(24);

    // 两个always-on经过的读数，带-and-suspenders，
    // 因此SOMETHING是可见的，无论终端怪癖如何：
    //   1.OSC-2窗口标题-非-intrusive，但一些多路复用器将其丢弃。
    //   2、DECSTBM-pinned底部状态行——通用；输出卷轴
    //      其上方。仅当我们可以读取 终端 高度时才启用。
    const int rows = ui::term_rows();
    ui::begin_status(rows);
    auto tick = [&] {
        const long secs = static_cast<long>(::time(nullptr) - started);
        if (secs == last_status_secs) return;
        last_status_secs = secs;
        ui::set_title("● " + std::to_string(secs) + "s — " + label + " — agentty");
        if (rows >= 3) {
            std::string bar = ui::dim();
            bar += kSpin[spin++ % 8];
            bar += " running… " + std::to_string(secs) + "s · " + label
                 + " · Ctrl-C to stop";
            bar += ui::reset();
            ui::paint_status(rows, bar);
        }
    };
    tick();
    // 退出条件，按其重要顺序：
    //   ·管道上的EOF——正常情况（儿童退出，无幸存者）。
    //   ·CHILD DEAD + PIPE QUIET —hang-proof 情况。 EOF 需要
    //     EVERY写入端关闭，命令留下的孙子（`npm run dev &`，
    //     守护进程构建，
    //     观察者）继承stdout/stderr并且可以在子退出后保持管道打开FOREVER。
    //     如果没有这个手臂，TUI 就会无限期地暂停——
    //     所报告的 "^G sometimes hangs"。
    //     所以：在循环内部不阻塞的情况下收割子进程；
    //     一旦它死了并且完整的轮询间隔过去但没有数据，则停止。
    //     稍后写入的仍然-attached背景幸存者获得EPIPE/SIGPIPE
    //     ——恢复UI的价格； WANTS 幸存者应重
    //     定向其输出的命令 (`… > log &`)。
    bool child_dead = false;
    for (;;) {
        struct pollfd pfds[2] = {{out_fd, POLLIN, 0}, {exit_fd, POLLIN, 0}};
        const int pr = ::poll(pfds, child_dead ? 1 : 2, 1000);
        tick();   // 刷新计时器，无论输出是否到达此滴答
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (!child_dead && (pfds[1].revents & (POLLIN | POLLHUP))) child_dead = true;
        if (!(pfds[0].revents & (POLLIN | POLLHUP))) {
            if (child_dead && pr <= 1) break;   // 死+安静≥1s→完成
            continue;
        }
        const ssize_t n = ::read(out_fd, buf, sizeof buf);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            break;
        }
        if (n == 0) break;
        (void)!::write(STDOUT_FILENO, buf, static_cast<std::size_t>(n));
        if (fin.output.size() < kCaptureMax) {
            const auto room = kCaptureMax - fin.output.size();
            fin.output.append(buf, std::min(static_cast<std::size_t>(n), room));
            if (static_cast<std::size_t>(n) > room) truncated = true;
        } else {
            truncated = true;
        }
    }
    ui::end_status(rows);
    ui::restore_title();

    if (!child_dead) {
        pollfd pe{exit_fd, POLLIN, 0};
        while (::poll(&pe, 1, -1) < 0 && errno == EINTR) {}
    }
    const auto status = proc.reap();
    ::sigaction(SIGINT,  &old_int,  nullptr);
    ::sigaction(SIGQUIT, &old_quit, nullptr);

    // 将用户中断（Ctrl-C/Ctrl-\)与真正的命令
    // 失败区分开——"failed exit 130"读起来
    // 就像当用户实际上故意停止命令时命令中断了。
    bool interrupted = false;
    if (!status) {
        fin.exit_code = -1;
    } else if (status->how == pf::exit_status::kind::exited) {
        fin.exit_code = status->code;
    } else {
        interrupted = (status->code == SIGINT || status->code == SIGQUIT);
        fin.exit_code = 128 + status->code;
    }
    if (truncated) fin.output += "\n[capture truncated at 2 MB — full output was shown on screen]";

    // 完成页脚：颜色-coded状态行，
    // 因此success/failure/中断一目了然，带有经过的时间。
    {
        const long secs = static_cast<long>(::time(nullptr) - started);
        const bool ok = (fin.exit_code == 0);
        std::string tail = "\n";
        if (interrupted)   { tail += ui::yellow(); tail += "╰─ ■ stopped"; }
        else if (ok)       { tail += ui::green();  tail += "╰─ ✓ done"; }
        else               { tail += ui::red();    tail += "╰─ ✗ failed"; }
        tail += ui::reset();
        tail += ui::dim();
        tail += "  exit " + std::to_string(fin.exit_code)
              + "  ·  " + std::to_string(secs) + "s";
        tail += ui::reset();
        tail += "\n";
        ui::emit(tail);
    }

    // Keypress-to-continue：将记录保留在屏幕上
    // 直到用户确认，因此快速命令的输出在可以读取之前不会被重新绘制。
    // 仅当 stdout 和 stdin 是真正的 终端 时（这样
    // 我们实际上可以读取密钥）； piped/redirected运行跳过它。
    // 提示词 位于其自己的行上并随后被清除，因此恢复的 TUI
    // 从干净的行开始。任意键继续。
    if (ui::tty() && ::isatty(STDIN_FILENO) == 1) {
        std::string prompt = ui::dim();
        prompt += "   press any key to return to agentty…";
        prompt += ui::reset();
        ui::emit(prompt);
        // Best-effort原始single-key读取：禁用规范模式+回显，
        // 以便单独按键（无Enter）继续，然后恢复。
        struct termios oldt{}, raw{};
        const bool have_termios = (::tcgetattr(STDIN_FILENO, &oldt) == 0);
        if (have_termios) {
            raw = oldt;
            raw.c_lflag &= ~(ICANON | ECHO);
            raw.c_cc[VMIN]  = 1;
            raw.c_cc[VTIME] = 0;
            ::tcsetattr(STDIN_FILENO, TCSANOW, &raw);
        }
        char c;
        // EINTR 重试; EOF (0) 和真正的错误（
        // 挂断后的 EIO）会进入恢复路径——这个门只能对
        // 尚未按下按键的人的实时 tty 停止，这是它的工作。
        while (::read(STDIN_FILENO, &c, 1) < 0 && errno == EINTR) {}
        if (have_termios) ::tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        ui::clear_line();
    }
    return fin;
}

[[nodiscard]] Cmd run_block_cmd(std::string command, cbp::BlockShell /*外壳*/) {
    return Cmd::suspend(
        [cmd = std::move(command)]() -> Msg {
            return Msg{run_on_real_tty(cmd)};
        });
}

#else  // _WIN32 — non-interactive 回退 通过共享子进程运行程序

#include <maya/runtime.hpp>

// 为所选择的Windows解释器包装块正文。 cmd.exe 是 run_command_s
// 的默认 shell（它包装在 `cmd.exe /S /C "..."` 中），
// 因此 Cmd 块会逐字传递。将 PowerShell 块交给 powershell
// -NoProfile -Command；我们通过 -EncodedCommand 对其
// 进行 base64-encode，因此任意 quoting/newlines
// 在 cmd.exe 包装中完好无损（通过 cmd /C 的嵌套引号是经典的破坏）。
// Multi-line 脚本可以正常工作，因为整个 正文 是一个编码参数。
[[nodiscard]] std::string wrap_for_windows_shell(cbp::BlockShell shell,
                                                 const std::string& body) {
    if (shell != cbp::BlockShell::PowerShell) return body;  // Cmd: verbatim
    // PowerShell -EncodedCommand合约(UTF-16LE的
    // base64)存在于共享平台-independent 头文件中，因此可以在Windows
    // 上对其进行验证——头文件携带编译-time已知-answer static_assert块，
    // 如果转换出现倒退，则该块将导致THIS构建失败。
    return win_shell::powershell_command(body);
}

// Windows跑步者的任务BODY。 jaal 需要任务 正文 不捕获任何内容（
// `TaskBody<Body, Msg, Args...>` 解析为普通函数指针），
// 因此它需要的所有内容都作为可发送参数在 stop_token 之后到达。
// 这曾经是一个捕获 lambda，这就是为什么 msys2 分支（唯一的 CI 通道，
// 具有足够新的 编译器 来实例化 jaal 的检查）失败并显示
// "a task body must not capture anything"，而 MSVC
// 报告了与可发送 static_assert 相同的情况。
//
// 下面的console-echo帮助器还特意将NOT命名为`out`：
// Sink参数拥有该名称，并且遮蔽它使得`out.send(...)`
// 解析为回显lambda。
static void run_block_body(const maya::unlocked& cap, maya::Sink<Msg> out, std::stop_token stop,
                           std::string cmd, cbp::BlockShell shell) {
    const std::string wrapped = wrap_for_windows_shell(shell, cmd);

    // 运行过程中发生了什么？
    // 的 Windows 奇偶校验：共享子进程运行程序 (
    // run_command_s) 正在阻塞并且非 -streaming — 输出仅
    // 出现在最后的结果卡中 — 因此我们无法实时查看。但我们 CAN 显示运行还活着：
    // 打印 头文件，旋转背景 线程，它会滴答过去的 -time
    // 心跳（OSC-2 标题 + in-place 行；
    // 现代 Windows 终端 / conhost 与 VT
    // 处理 渲染 两者），运行命令，然后运行页脚。所有装饰都是
    // console-only，并且永远不会进入捕获的缓冲区。
    const bool con = (::_isatty(_fileno(stdout)) != 0);
    auto echo = [](const std::string& s) {
        std::fputs(s.c_str(), stdout); std::fflush(stdout);
    };
    if (con) {
        echo("\x1b[2m\n╭─ running ─ (output shown when it finishes) ─────\x1b[0m\n"
             "\x1b[36m$ \x1b[0m\x1b[1m" + cmd + "\x1b[0m\n");
    }

    std::string label = cmd.substr(0, cmd.find_first_of(" \t\n"));
    if (label.size() > 24) label.resize(24);

    // 心跳在maya::scope中运行，而不是在裸露的std::thread中运行。
    //
    // 旧形状是`std::thread ticker; ... ticker.join()`，
    // 其间有阻挡run_command_s。有两个问题，在它们咬住之前
    // 都是看不见的：如果该窗口中抛出任何东西，〜线程在可连接的线程上运行，
    // 并且进程称为std::terminate（没有堆栈，没有消息）；
    // 并且助手通过引用捕获了 `done_flag` 和 `label`，
    // 这只是安全的，因为没有人保证能够到达相同的连接。
    //
    // scope()加入EVERY退出路径上的每个帮助者
    // ——正常返回、提前返回、异常——这使得[&]捕获
    // 当地人是合法的而不是幸运的。也没有 detach()
    // 可以到达，因此助手不能比它借用的 帧 活得更久。
    //
    // 任务自己的stop_token作为父代被传入，因此Esc（
    // 或关闭）通过与其他所有任务相同的路径取消心跳，而不需要其自己的标志。
    // done_flag 停留在正常的
    // "the command finished" 停止位置，这不是 取消。
    struct Outcome {
        tools::util::SubprocessResult r;
        long                          secs = 0;
    };

    // 心跳与这个线程共享什么：一站标志。同步（const stop_source），
    // 由 co_owned 持有，因此双方都不能在另一方下释放它。
    // request_stop() 在标准中是非 -const（只有
    // libstdc++ 将其标记为 const），因此它会经历一个副本：
    // 副本共享停止状态，并且允许从任何 线程 复制 const 源。
    struct Beat { const std::stop_source done; };
    auto beat_state = maya::co_owned<Beat>::make();

    auto outcome = maya::scope(cap, stop, [&](maya::nursery& n) {
        // 帮助者自己的令牌来自作用域的，因此 Esc 也到达 加载动画；
        // 将 回调 归并 改成 `done`。
        auto beat = n.spawn([](const maya::unlocked& hcap, std::stop_token st, maya::co_owned<Beat> b, std::string label,
                               bool con) {
            if (!con) return;
            const std::stop_callback on_stop(st, [&b] { std::stop_source(b->done).request_stop(); });
            static constexpr const char* kSpin[] =
                {"⣷","⣯","⣟","⡿","⣾","⣽","⣻","⣷"};
            int spin = 0;
            const auto start = std::chrono::steady_clock::now();
            do {
                const long secs = static_cast<long>(
                    std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::steady_clock::now() - start).count());
                std::string s = "\x1b]2;\u25cf " + std::to_string(secs)
                              + "s \u2014 " + label + " \u2014 agentty\x07";
                s += "\r\x1b[2K\x1b[2m" + std::string(kSpin[spin++ % 8])
                   + " running\u2026 " + std::to_string(secs) + "s\x1b[0m";
                std::fputs(s.c_str(), stdout); std::fflush(stdout);
            } while (!maya::delay_for(hcap, b->done.get_token(), std::chrono::milliseconds(250)));
        }, beat_state, label, con);

        // 然而，命令结束（或抛出），在句柄加入它之前停止
        // 加载动画。
        struct StopBeat {
            const Beat& b;
            ~StopBeat() { std::stop_source(b.done).request_stop(); }
        } stop_beat{*beat_state};

        const auto start = std::chrono::steady_clock::now();
        Outcome o;
        o.r = tools::util::run_command_s(wrapped);
        o.secs = static_cast<long>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start).count());

        std::stop_source(beat_state->done).request_stop();
        beat.join();          // 明确的，所以这里有一个投掷助手
        return o;
    });

    auto r          = std::move(outcome.r);
    const long secs = outcome.secs;

    CodeBlockRunFinished fin;
    fin.command   = cmd;   // 卡中显示ORIGINAL 正文
    fin.timed_out = r.timed_out;
    if (!r.started) {
        fin.output    = "[failed to start: " + r.start_error + "]";
        fin.exit_code = -1;
    } else {
        fin.output    = std::move(r.output);
        fin.exit_code = r.exit_code;
        if (r.truncated) fin.output += "\n[output truncated]";
    }

    if (con) {
        const bool ok = (fin.exit_code == 0 && !r.timed_out);
        std::string tail = "\r\x1b[2K\x1b]2;\x07";   // 擦除hb+恢复标题
        tail += ok ? "\x1b[32m╰─ ✓ done"
             : r.timed_out ? "\x1b[33m╰─ ■ timed out"
             : "\x1b[31m╰─ ✗ failed";
        tail += "\x1b[0m\x1b[2m  exit " + std::to_string(fin.exit_code)
              + "  ·  " + std::to_string(secs) + "s\x1b[0m\n";
        echo(tail);
    }

    out.send(Msg{std::move(fin)});
}

[[nodiscard]] Cmd run_block_cmd(std::string command, cbp::BlockShell shell) {
    // 正文是普通函数上的无捕获lambda，因此jaal将工作线程
    // 的`unlocked`交给它（心跳范围加入：它需要一个）；
    // 命令和 shell 作为可发送参数一起运行。
    return Cmd::task_isolated(
        [](const maya::unlocked& cap, maya::Sink<Msg> out, std::stop_token stop,
           std::string cmd, cbp::BlockShell sh) {
            run_block_body(cap, std::move(out), std::move(stop), std::move(cmd), sh);
        },
        std::move(command), shell);
}

#endif
} // 命名空间结束。

Cmd run_code_block(std::string command, cbp::BlockShell shell) {
    return run_block_cmd(std::move(command), shell);
}

} // namespace agentty::app::cmd
