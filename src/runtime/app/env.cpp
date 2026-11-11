// src/runtime/app/env.cpp — 在 init 处读取启动环境 一次。
//
// 这是 调用 getenv 对于事实 update() 所依赖的应用层中的唯一位置。
// init() 调用 read_launch_env()并将结果存入Model::env；
// 每个 状态更新函数 都会读取 Model。请参阅 Model::Env
// 上的评论，了解为什么 状态更新函数 绝不能读取环境本身。

#include "agentty/runtime/app/env.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>

#include <maya/terminal/ansi.hpp>
#include <maya/terminal/scrollback.hpp>
#include <maya/terminal/tmux.hpp>

#include "agentty/util/home_dir.hpp"
#include "agentty/util/user_root.hpp"
#include "agentty/util/update.hpp"   // self_update_possible
#include "agentty/rag/embed_backend.hpp"   // apply_env
#include "agentty/runtime/view/helpers.hpp"      // max_context_tokens_from_env
#include "agentty/runtime/view/host_escape.hpp"  // detect_integration

namespace agentty::app {

namespace {

bool set(const char* name) noexcept { return std::getenv(name) != nullptr; }

// 标志变量在存在时是ON，非-empty，并且不是"0"。
bool flag(const char* name) noexcept {
    const char* v = std::getenv(name);
    return v && v[0] && v[0] != '0';
}

// "1"、"t"/"T"、"y"/"Y"——AGENTTY_FROZEN_COLLAPSE
// 一直接受的真实拼写。保持精确，这样现有设置就不会改变含义。
bool truthy(const char* name) noexcept {
    const char* v = std::getenv(name);
    if (!v || !*v) return false;
    return v[0] == '1' || v[0] == 't' || v[0] == 'T'
        || v[0] == 'y' || v[0] == 'Y';
}

// 取消设定→开启； "0"/"false"/"off"/"no"（任何情况）→ 关闭；还有什么→ 上。
bool on_unless_off(const char* name) noexcept {
    const char* v = std::getenv(name);
    if (!v || !*v) return true;
    std::string s;
    for (const char* p = v; *p; ++p)
        s += static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
    return !(s == "0" || s == "false" || s == "off" || s == "no");
}

bool detect_remote() noexcept {
    // 顺序是优先级，与此替换的谓词没有变化：
    // 显式的force-on击败了opt-out，这击败了检测。
    //
    // 显式FORCE-ON：环境嗅探器看不到的会话（定制
    // 远程包装器、串行控制台、滞后容器附加）选择进入
    // 远程节奏。
    if (flag("AGENTTY_FORCE_REMOTE")) return true;
    // 逃生舱口：快速LAN SSH跳跃不需要节流。
    if (flag("AGENTTY_NO_SSH_THROTTLE")) return false;
    // 直接SSH：sshd将这些导出到远程shell中。
    if (set("SSH_CONNECTION") || set("SSH_TTY") || set("SSH_CLIENT"))
        return true;
    // mosh:SSH_*变量是通过mosh-server(它是-execs)
    // 传播的NOT，但是MOSH_*是——并且mosh总是远程的。
    if (set("MOSH_CONNECTION") || set("MOSH_KEY") || set("MOSH_SERVER_PID"))
        return true;
    // 永恒终端将ET_VERSION导出到远程shell中。
    if (set("ET_VERSION")) return true;
    return false;
}

// 重播打印多少历史是回滚缓冲区设置(
// scrollback_replay_limits)。以下是围绕它的发布事实：
// AGENTTY_REPLAY_ROWS (0=重放关闭，N=正好N行)击败了该设置，
// 并且tmux的history-limit对其进行了限制，因为tmux无论
// 如何都会丢弃超过该设置的任何内容。其他 终端 不报告他们的。
void read_replay_caps(Model::Env& e) noexcept {
    e.replay_rows_override = -1;
    e.replay_max_bytes = std::numeric_limits<std::size_t>::max();
    if (const char* v = std::getenv("AGENTTY_REPLAY_ROWS"); v && *v) {
        char* end = nullptr;
        const long n = std::strtol(v, &end, 10);
        if (end && *end == '\0' && n >= 0)
            e.replay_rows_override = static_cast<int>(std::min<long>(n, std::numeric_limits<int>::max()));
    }
    e.terminal_history_rows = maya::tmux::history_limit();
    // 仅上面的 Env + tmux 探针：没有文件，没有生成。
    const auto fast = maya::terminal::scrollback_fast();
    e.auto_scrollback_rows = fast.unlimited ? std::numeric_limits<int>::max() : fast.rows;
}

}  // 命名空间结束。

Model::Env read_launch_env(Io io) noexcept {
    Model::Env e;
    e.remote              = detect_remote();
    // 仅限普通 SSH。作曲家的剪贴板路径关键是这个而不是 `remote`，
    // 因为问题是 "does OSC 52 ride an SSH pty"，
    // 而不是 "are frames slow" — 它们始终是两个单独的检查。
    e.ssh                 = set("SSH_CONNECTION") || set("SSH_TTY");
    e.no_reveal_glide     = flag("AGENTTY_NO_REVEAL_GLIDE");
    e.synchronized_output = maya::ansi::env_supports_synchronized_output();
    e.frozen_collapse     = truthy("AGENTTY_FROZEN_COLLAPSE");
    e.no_update_check     = set("AGENTTY_NO_UPDATE_CHECK");
    e.no_auto_update      = set("AGENTTY_NO_AUTO_UPDATE");
    e.max_context_tokens  = ui::max_context_tokens_from_env();
    e.reveal              = on_unless_off("AGENTTY_REVEAL");
    e.reveal_typewriter   = on_unless_off("AGENTTY_REVEAL_TYPEWRITER");
    e.reveal_decorate     = on_unless_off("AGENTTY_REVEAL_DECORATE");
    e.painted_caret       = set("AGENTTY_PAINTED_CARET");
    e.host_integration    = ui::host::detect_integration();
    read_replay_caps(e);
    e.terminal            = ui_prefs::detect(/*tty=*/true);
    e.settings            = settings::registry::read_env(io);
    {
        std::error_code ec;
        const auto cwd = std::filesystem::current_path(ec);
        if (!ec) e.cwd = cwd.string();
        e.home = util::home_dir().string();
    }
    // 仅在重要时进行探测：探测在我们的 二进制程序 旁边写入一个文件，
    // 并且选择退出 auto-update 的安装根本不
    // 应该触及其目录。
    if (!e.no_auto_update)
        e.self_update_ok = update::self_update_possible(e.self_update_reason);
    rag::embed::apply_env(io, e.embed_defaults);
    e.user_root = util::user_root().string();
    if (const char* v = std::getenv("AGENTTY_RAG_PROACTIVE"); v && v[0]) {
        std::string s{v};
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        e.rag_proactive_override = (s == "1" || s == "true" || s == "on" || s == "yes");
    }
    return e;
}

}  // namespace agentty::app
