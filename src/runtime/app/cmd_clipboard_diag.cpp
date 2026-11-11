// cmd::diagnose_clipboard——为什么剪贴板读取没有得到答复？
//
// 回答行走/proc（mosh-server是我们的祖先之一，
// 还是附加的tmux客户端之一？）并向tmux服务器询问其配置，
// 因此它在工作线程上运行并回复ClipboardDiagnosed。
// 状态更新函数 仅显示消息。

#include "agentty/runtime/app/cmd_factory.hpp"

#include <string>

#if defined(__linux__)
#include <fstream>
#include <sstream>
#include <unistd.h>
#endif

#include <maya/terminal/tmux.hpp>

#include "agentty/io/clipboard.hpp"

namespace agentty::app::cmd {

namespace {

#if defined(__linux__)
// 从 `pid` 向上穿过 /proc 寻找 mosh-server。
// mosh 没有将可靠的环境标记导出到其子 shell 中，因此祖先是唯一的信号。
bool mosh_above(int pid) {
    for (int hop = 0; hop < 12 && pid > 1; ++hop) {
        const std::string base = "/proc/" + std::to_string(pid);
        if (std::ifstream comm(base + "/comm"); comm) {
            std::string name;
            std::getline(comm, name);
            if (name.find("mosh-server") != std::string::npos) return true;
        }
        std::ifstream stat(base + "/stat");
        if (!stat) break;
        // stat:pid(comm)状态ppid…——comm可能包含空格，
        // 因此解析来自于LAST之后的')'。
        std::string line;
        std::getline(stat, line);
        const auto rp = line.rfind(')');
        if (rp == std::string::npos) break;
        std::istringstream tail(line.substr(rp + 2));
        char state = 0;
        int ppid = 0;
        tail >> state >> ppid;
        if (ppid <= 1) break;
        pid = ppid;
    }
    return false;
}
#endif

bool mosh_session() {
#if defined(__linux__)
    if (mosh_above(static_cast<int>(::getppid()))) return true;
    // 祖先错过持久化 tmux:当tmux服务器由systemd
    // 或较旧的登录启动时，mosh-server是服务器的兄弟，
    // 而不是我们的祖先。改为步行附加的客户端。
    if (maya::tmux::active())
        if (const int cpid = maya::tmux::client_pid(); cpid > 1)
            return mosh_above(cpid);
#endif
    return false;
}

std::string diagnose(bool in_ssh) {
    const bool in_mosh = mosh_session();
    const bool in_tmux = maya::tmux::active();
    // tmux能力是根据-client和maya记忆探针的，
    // 因此从另一个终端重新连接留下过期结论。一次读取刚刚失败，
    // 这值得一轮-trip到re-check。
    if (in_tmux) (void)maya::tmux::refresh_if_client_changed();
    std::string msg;
    if (in_mosh) {
        msg = "clipboard: mosh drops terminal clipboard replies and can't "
              "forward the paste agent \xe2\x80\x94 use plain ssh (with "
              "`agentty paste setup` on your laptop) to paste images";
    } else if (in_tmux) {
        // 命名ACTUAL阻断剂而不是猜测。
        // 顺序很重要：这些按照 tmux 应用它们的
        // 顺序进行检查，因此报告的第一个故障是用户必须
        // 修复的第一个故障。
        if (!maya::tmux::clipboard_reads_relayed()) {
            // THE常见情况，并且直到现在都保持沉默：
            // tmux的`get-clipboard`
            // 默认为`buffer`，因此tmux回答从其
            // OWN粘贴缓冲区的剪贴板读取并且从不询问终端。
            // 缓冲区 粘贴包含 TEXT — 无论外部 终端
            // 支持什么，图像都永远无法通过它返回。
            msg = "clipboard: tmux answers reads from its own paste "
                  "buffer (text only) \xe2\x80\x94 run `tmux set -g "
                  "get-clipboard both` so it asks your terminal, "
                  "then retry";
        } else if (!maya::tmux::passthrough_allowed()) {
            msg = "clipboard: tmux is dropping the request \xe2\x80\x94 run "
                  "`tmux set -g allow-passthrough on` (it is OFF by "
                  "default), then retry";
        } else if (!maya::tmux::has_feature(
                       maya::tmux::Feature::Clipboard)) {
            msg = "clipboard: tmux reports your outer terminal has no "
                  "clipboard support \xe2\x80\x94 add `set -ga terminal-features "
                  "\",*:clipboard\"` if it does, or run `agentty paste setup` on your laptop";
        } else {
            msg = "clipboard: no reply through tmux \xe2\x80\x94 passthrough and "
                  "clipboard are on, so the outer terminal refused the "
                  "read (in kitty: add read-clipboard to clipboard_control "
                  "in kitty.conf and restart kitty)";
        }
    } else if (in_ssh) {
        msg = "clipboard: can't reach your laptop's clipboard \xe2\x80\x94 run "
              "`agentty paste setup` on the laptop once and reconnect "
              "(`agentty paste doctor` here shows why)";
    } else {
        msg = "clipboard: nothing to paste here and the terminal didn't "
              "answer \xe2\x80\x94 `agentty paste doctor` shows why";
    }
    return msg;
}

}  // 命名空间结束。

Cmd diagnose_clipboard(bool in_ssh) {
    return Cmd::task(
        [](maya::Sink<Msg> out, std::stop_token, bool in_ssh) {
            std::string message;
            try {
                message = diagnose(in_ssh);
            } catch (...) {
                message = "clipboard: no reply from the terminal";
            }
            out.send(Msg{msg::ComposerMsg{ClipboardDiagnosed{std::move(message)}}});
        },
        in_ssh);
}

namespace {

// Ctrl+V读取的一个步骤。 `resume` 错误开始行走；
// 真正的移动 `walk` 超过了保持安静（或拒绝）的 终端。
// 独立运行：步骤可以等待显示服务器、ssh 往返或 tmux 询问 终端。
void clipboard_step(maya::Sink<Msg> out, bool resume, ClipboardWalk walk, std::string why, bool refused) {
    ClipboardRead r;
    try {
        auto c  = resume ? ::agentty::resume_clipboard(std::move(walk), std::move(why), refused)
                         : ::agentty::read_clipboard();
        settle_clipboard_items(c.items);
        r.items = std::move(c.items);
        r.miss  = c.miss;
        r.error = std::move(c.error);
        r.ask   = c.ask;
        r.walk  = std::move(c.walk);
    } catch (...) {
        r = ClipboardRead{};
        r.miss  = ClipboardMiss::NoRoute;
        r.error = "clipboard read failed";
    }
    out.send(Msg{msg::ComposerMsg{std::move(r)}});
}

}  // 命名空间结束。

Cmd read_clipboard() {
    return Cmd::task_isolated([](maya::Sink<Msg> out, std::stop_token) {
        clipboard_step(std::move(out), false, ClipboardWalk{}, std::string{}, false);
    });
}

Cmd resume_clipboard(ClipboardWalk walk, std::string why, bool refused) {
    return Cmd::task_isolated([](maya::Sink<Msg> out, std::stop_token, ClipboardWalk w, std::string y, bool no) {
        clipboard_step(std::move(out), true, std::move(w), std::move(y), no);
    }, std::move(walk), std::move(why), refused);
}

Cmd settle_clipboard(std::vector<ClipboardItem> items) {
    return Cmd::task(
        [](maya::Sink<Msg> out, std::stop_token, std::vector<ClipboardItem> its) {
            ClipboardRead r;
            try { settle_clipboard_items(its); r.items = std::move(its); }
            catch (...) { r.error = "saving the pasted file failed"; }
            out.send(Msg{msg::ComposerMsg{std::move(r)}});
        },
        std::move(items));
}

} // namespace agentty::app::cmd
