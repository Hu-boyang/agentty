// agentty::provider-SHARED系统-prompt+内存组件(
// SSOT针对每个提供商)。移出 provider::anthropic，
// 该资产被错误归档为 Claude-only 资产。纯字符串构建：CLAUDE.md 层、
// learn-memory 块、OS/shell 环境节、完整 vs 精益（子
// 代理）说明文本，以及 per-provider 覆盖面板 接口边界。烘烤成二进制程序。

#include "agentty/provider/prompt.hpp"

#include "agentty/provider/msg_shared.hpp"   // wire::read_capped_file/resolve_user_claude_md
#include "agentty/tool/memory_store.hpp"
#include "agentty/tool/registry.hpp"
#include "agentty/tool/skills.hpp"
#include "agentty/tool/util/fs_helpers.hpp"   // util::workspace_root/project_root — AGENTS.md锚定+步行开始
#include "agentty/tool/util/sandbox.hpp"      // is_active/config_snapshot — <environment> 沙箱 节
#include "agentty/domain/sandbox_config.hpp"  // NetMode
#include "agentty/util/dbglog.hpp"

#include <maya/runtime.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace agentty::provider {

namespace {

// 读取文本文件，吞掉任何 I/O 错误 — 在丢失文件或不可
// 读时返回空字符串。上限为 64 KiB（共享 wire::read_capped_file，
// 它还会修剪尾随空白，以便包装标签不会挤在空
// 行上）。
[[nodiscard]] std::string read_optional_memory(const std::filesystem::path& p) noexcept {
    return wire::read_capped_file(p);
}

// mtime-keyed 缓存 围绕 read_optional_memory 的包装器。每次都会读取
// CLAUDE.md 层次结构（3 个文件：~/CLAUDE.md、./CLAUDE.md、./CLAUDE.local.md），
// 每个文件的上限为 64 KiB。 Per-turn 是：
//
//   ·在本地SSD上：~3×stat+open+read=sub-millisecond
//   ·在NFS家：工作线程之前〜3×往返≈30-100毫秒
//     甚至开始构建请求
//
// 内容在回合之间变化的情况很少（用户编辑他们的CLAUDE.md mid
// -session）。 缓存 通过文件系统 mtime: stat 一次得到
// last_write_time;如果与缓存的条目匹配，则交回缓存的正文。
// 进程-lifetime 缓存；以 collect_memory_blocks
// 所查看的 3 个路径和 64 KiB-per-file 内容上限为界，
// 因此最坏情况是进程 内存 的 ~192 KiB。
//
// 并发性：系统提示词建立在数据流 工作线程 线程（
// Cmd::stream的任务正文）之上； 缓存 是 锁-free 快照。
[[nodiscard]] std::string read_memory_cached(const std::filesystem::path& p) {
    // 以纯值(路径字符串、实时时间刻度)为键，因此它们作为可发送
    // 参数进入guarded<T>。
    struct Entry {
        std::int64_t mtime = 0;
        std::string  content;
    };
    using Cache = std::unordered_map<std::string, Entry>;
    // 读取-mostly，每个条目替换：锁-free 快照，
    // 因此构建提示词不需要能力。
    static maya::snapshot<Cache> cache;

    std::string     key = p.string();
    std::error_code ec;
    const auto      mt = std::filesystem::last_write_time(p, ec);

    if (ec) {
        // 文件丢失/不可读-删除任何先前的缓存条目，以便观察到稍
        // 后的re-creation。
        if (cache.load()->contains(key))
            (void)cache.update([](const Cache& old, const std::string& k) {
                Cache c = old;
                c.erase(k);
                return c;
            }, std::move(key));
        return {};
    }
    const std::int64_t now_mtime = mt.time_since_epoch().count();

    {
        const auto c = cache.load();
        auto it = c->find(key);
        if (it != c->end() && it->second.mtime == now_mtime) return it->second.content;
    }

    // 缓存未命中或过期 —在没有保存锁的情况下读取，
    // 因此缓慢的NFS读取不会阻止其他路径的缓存命中。
    std::string body = read_optional_memory(p);
    (void)cache.update([](const Cache& old, const std::string& k, const Entry& e) {
        Cache c = old;
        c[k] = e;
        return c;
    }, std::move(key), Entry{now_mtime, body});
    return body;
}

// CLAUDE.md 内存层次结构，与Claude代码相同的路径(参见wire::
// resolve_user_claude_md)：
//
//   用户~/.claude/CLAUDE.md个人，所有项目(~/CLAUDE.md 回退)项目<cwd>/
//   CLAUDE.md + <cwd>/.claude/CLAUDE.md本地
//   <cwd>/CLAUDE.local.md gitignored，个人-to-this-project
//
// 每层都包裹在其自己的标签中，因此模型可以区分它们。
// 空的/缺失的层被默默地省略。无论如何，传输 成本每约 5 分钟 cache_control
// TTL 窗口支付一次（系统-prompt cache_control
// 断点捕获结果）；磁盘成本通过 read_memory_cached
// 进行记忆，因此每个 -turn 占用空间是 3× stat() +
// 缓存的 正文 的 memcpy，而不是 3× 完整读取。
[[nodiscard]] std::string collect_memory_blocks(Io) {
    const auto user_path = wire::resolve_user_claude_md();
    std::string user    = user_path.empty() ? std::string{} : read_memory_cached(user_path);
    std::string project = wire::join_project_claude_md(
        read_memory_cached(std::filesystem::path{wire::kProjectClaudeMd[0]}),
        read_memory_cached(std::filesystem::path{wire::kProjectClaudeMd[1]}));
    std::string local   = read_memory_cached(std::filesystem::path{"CLAUDE.local.md"});

    // Agent-authored 内存(由`remember`工具编写，
    // 由`forget`删除)。 已加载 与 per-scope JSONL 存储中的 末尾-N 相同，
    // 因此即使 on-disk 文件增长，提示词 仍保持有限状态。
    auto learned_user    = tools::memory::load_recent_user();
    auto learned_project = tools::memory::load_recent_project();

    if (user.empty() && project.empty() && local.empty()
        && learned_user.empty() && learned_project.empty()) return {};

    std::ostringstream m;
    m << "\n\n<memory>\n"
      << "Project-specific guidance the user has authored. Treat these "
         "as persistent context for THIS workspace and user; lower tiers "
         "(local, then project, then user) win on conflicting rules.\n";
    if (!user.empty())    m << "<user-memory>\n"    << user    << "\n</user-memory>\n";
    if (!project.empty()) m << "<project-memory>\n" << project << "\n</project-memory>\n";
    if (!local.empty())   m << "<local-memory>\n"   << local   << "\n</local-memory>\n";
    auto emit_learned = [&](const char* tag, std::vector<tools::memory::Record> rs) {
        if (rs.empty()) return;
        // 对块进行预算，以便大型 内存 商店无法使每个系统 提示词 膨胀。
        // select_for_prompt 保留所有固定记录
        // + kPromptByteBudget 内最高的 -signal 余数，
        // 剪掉任何超过 -long 的记录；其余的保留在磁盘上
        // （可调用，仍然可编辑），我们记下省略的计数，
        // 以便模型知道存储的内容比显示的要多。
        auto picked = tools::memory::select_for_prompt(std::move(rs));
        if (picked.records.empty() && picked.dropped == 0) return;
        m << "<learned-memory scope=\"" << tag << "\">\n"
          << "Facts you previously stored via the `remember` tool. Each "
             "line is prefixed with the record id — pass that id to "
             "`forget` if the fact is no longer true.\n";
        for (const auto& r : picked.records)
            m << tools::memory::render_for_prompt(r) << "\n";
        if (picked.dropped > 0)
            m << "[+" << picked.dropped << " more stored fact(s) not shown "
                 "here to keep the prompt small — they remain on disk; ask "
                 "about a topic and recall surfaces them, or pin the ones "
                 "that should always be visible.]\n";
        m << "</learned-memory>\n";
    };
    emit_learned("user",    std::move(learned_user));
    emit_learned("project", std::move(learned_project));
    m << "</memory>";
    return m.str();
}

// AGENTS.md —用于project-scoped代理指导的开放AAIF/Linux-Foundation标准
// （https://agents.md). Project-scoped仅根据已发布的规范：
// 无用户层，无本地层。根文件位于<project_root>
// /AGENTS.md。
//
// 这是共享 wire::agents_md_block 帮助程序上的瘦适配器
// — 3-tier 分辨率（全局 → 根 → 最近嵌套）和 monorepo
// 遍历的 唯一 事实来源，与 OpenAI/Ollama 提示词 共享。
// 唯一的 Anthropic-specific 位是我们注入的文件
// -reader：read_memory_cached（通用 mtime-cached
// 读取器 - 缓存 键是路径字符串，因此每个 AGENTS.md 层都有自己独立于
// CLAUDE.md 层的条目）。这使得每个层都从 缓存 开始提供服务，因此每次重建系统
// 提示词 都会花费一些 stat() + 缓存的 正文 的 memcpy，
// 而不是 N 个完整文件读取。
//
// 传输形状(优先级低→高)：<agents-md-global>(~/.agentty/AGENTS.md，
// 可选)→<agents-md>(<workspace_root>/AGENTS.md)
// →<agents-md-package>(最近的嵌套文件，可选；为根文件时跳过)。
// 所有这些都被注入 之前 和 <memory> 块，因此标准化公共项目
// 指南在视觉上与个人 CLAUDE.md 层保持不同，并且模型可以正确应用优先级。
// 位于与 collect_memory_blocks 相同的
// Anthropic cache_control 断点后面。
[[nodiscard]] std::string collect_agents_md_block(Io) {
    // workspace_root()是固定访问边界（根AGENTS.md
    // 所在的位置）； project_root() 是固定在该边界内的代理的
    // cwd — 用于查找最近的嵌套 AGENTS.md 的步行起点。
    return wire::agents_md_block(
        "Project guidance following the open AGENTS.md standard "
        "(agents.md, stewarded by the Agentic AI Foundation under the "
        "Linux Foundation). Treat as authoritative public project "
        "conventions.",
        tools::util::workspace_root(),
        tools::util::project_root(),
        wire::resolve_global_agents_md(),
        &read_memory_cached);
}

// 模型需要了解关于 沙箱 或 ""（当不存在时）的信息。
//
// ── 为什么模特儿会被告知 ───────────────────────────────────
//
// 因为不知道它的模型在它撞到的每堵墙上都被沙箱化了MISDIAGNOSES。
// 该故障在各个代理中均有详细记录：路径上的 EPERM 变为
// 公司代理阻止了此访问。
// 阻塞的连接变为 "check your firewall / CrowdStrike"，
// 代理要么向用户发送徒劳无功的信息，要么在循环中向 重试 发送相同的拒绝
// 的 调用。两者都浪费了一个回合，而且都无法成功，因为边界是内核决定的，
// 没有多少 重试 可以移动它。
//
// 每个成熟实现所解决的问题都是相同的：说边界存在，
// 说它在哪里，然后说要做什么。那么否认就是模型
// 可以绕过的 FACT，而不是需要调查的谜团。
//
// ── 为何在 <environment> ─────────────────────────────────────
//
// 这是一个环境事实，就像OS和外壳一样：与
// 当前在 Windows 上，请不要使用 ls。
// 是同一类东西。将其放在单独的部分中会使其被视为需要推理的政策；
// 把它放在这里使它成为一个需要在其中工作的约束。
//
// ── 为什么它说什么 ALLOWED 不只是被封锁的东西 ───────────────
//
// 拒绝列表邀请探测边缘。声明 工作区 是可写的，
// 其余部分是可读取的。-only 告诉模型在 首次 尝试中应该
// 在哪里工作，这就是重点：更少的撞墙，而不是之后更好的解释。
[[nodiscard]] std::string sandbox_stanza() {
    namespace sb = tools::util::sandbox;
    if (!sb::is_active()) return {};

    const auto cfg = sb::config_snapshot();
    std::string out = "  sandbox: ON \xe2\x80\x94 shell commands run inside an "
                      "OS-level sandbox\n";

    // 工作区是始终可写的一件事，并且这样说是在其他地方发生一个EPERM之后
    // 停止"I cannot write anywhere"恐慌的原因。
    out += "    writable:  the workspace (cwd above) and /tmp\n";
    out += "    readable:  system dirs and your toolchain; "
           "credential files are masked\n";

    switch (cfg->net_mode) {
        case sandbox_cfg::NetMode::Full:
            if (cfg->allow_hosts.empty()) {
                out += "    network:   public internet via a sandbox proxy; private, "
                       "loopback and cloud-metadata addresses are refused\n";
            } else {
                // 预先列出名称，因此模型选择ALLOWED的镜像，
                // 而不是一次发现列表403。
                out += "    network:   ONLY these hosts (via a sandbox proxy): ";
                for (std::size_t i = 0; i < cfg->allow_hosts.size() && i < 24; ++i)
                    out += (i ? ", " : "") + cfg->allow_hosts[i];
                if (cfg->allow_hosts.size() > 24) out += ", \xe2\x80\xa6";
                out += "\n               (*.x means subdomains of x, not x itself). "
                       "Any other host or raw IP fails with a proxy/403 error.\n";
            }
            break;
        case sandbox_cfg::NetMode::None:
            // 值得直言不讳的是：这个破坏了代理大多数情况下所达到的命令（
            // git push、npm install、curl），
            // 并且不知道的模型将烧毁诊断 DNS 的回合。
            out += "    network:   BLOCKED \xe2\x80\x94 git push, npm/pip installs "
                   "and curl will fail. Do not retry them or blame DNS.\n";
            break;
        case sandbox_cfg::NetMode::Ports:
            out += "    network:   restricted to specific ports; an unlisted "
                   "port fails to connect\n";
            break;
    }

    // 指示，而不仅仅是事实。如果没有这个，模型就知道它是沙盒的，
    // 并且仍然是 重试，因为没有任何东西告诉它 重试 是
    // 错误的举动。
    out += "    If a command fails with a permission or network error, that is "
           "the sandbox, not a broken tool (a proxy/tunnel/403 error is the "
           "sandbox's own proxy).\n"
           "    Do NOT retry it unchanged and do NOT tell the user to check "
           "their firewall. Work inside the workspace, or say plainly which "
           "wall you hit and what you would need.\n";
    return out;
}

} // 命名空间结束。

std::string default_system_prompt(Io io, bool lean) {
#if defined(_WIN32)
    constexpr const char* os_name  = "Windows";
    constexpr const char* shell    = "cmd.exe (Windows Command Prompt)";
    constexpr const char* shell_hint =
        "Prefer native Windows equivalents: `dir` / `where` / `systeminfo` / "
        "`type` / `findstr` / `powershell -c`. Do NOT use POSIX-only tools "
        "like `uname`, `cat /etc/os-release`, `sw_vers`, `ls`, `grep`, `sed`, "
        "`awk`, or shell heredocs (`<<EOF`) — they will fail. "
        "Commands chain with `&&` and `||` under cmd.exe, but path separators "
        "are backslashes and paths with spaces must be quoted.";
#elif defined(__APPLE__)
    constexpr const char* os_name  = "macOS (Darwin)";
    constexpr const char* shell    = "sh";
    constexpr const char* shell_hint =
        "Use POSIX tools; `sw_vers` gives macOS version, `uname -a` gives kernel.";
#else
    constexpr const char* os_name  = "Linux";
    constexpr const char* shell    = "sh";
    constexpr const char* shell_hint =
        "Use POSIX tools; `/etc/os-release` gives distro info, `uname -a` gives kernel.";
#endif

    std::string cwd;
    // current_path().string()通过活动代码页（ANSI)缩小
    // 宽Windows路径，因此非-ASCII路径变成无效UTF-8并且会毒害JSON
    // 传输上的整个系统提示词。 u8string() 将宽路径直接转换为 UTF
    // -8 — 无有损 ANSI round-trip。 （POSIX 上的 No-op，
    // 其中本机编码已经是 UTF-8。）
    try {
        auto u8 = std::filesystem::current_path().u8string();
        cwd.assign(reinterpret_cast<const char*>(u8.data()), u8.size());
    } catch (const std::exception& e) {
        util::dbglog("anthropic.system_prompt.cwd", e.what());
    } catch (...) {
        util::dbglog("anthropic.system_prompt.cwd", "non-std exception");
    }

    std::ostringstream oss;
    oss << "You are agentty, a terminal coding assistant. Act, don't ask. "
        << "When the user says something vague (\"edit it\", \"make it "
        << "better\", \"improve it\", \"make it interesting\", \"fix it\"), "
        << "make a reasonable improvement yourself with `edit` — do NOT "
        << "respond with a list of options or clarifying questions. Keep "
        << "prose short; let tool cards speak for themselves. The user "
        << "already sees every tool's output in its card, so don't repeat "
        << "it back unless you need to point at something in it.\n\n"
        << "<file-editing>\n"
        << "  - For ANY change to a file that already exists, use `edit`. "
        << "If the file is in conversation history (you wrote it, or you "
        << "read it earlier), construct `edit.old_text` from memory — do "
        << "NOT re-read it.\n"
        << "  - `write` is for creating NEW files. If the file exists, "
        << "use `edit` — calling `write` on an existing file dumps the "
        << "entire body over the wire and stalls the stream; it is the "
        << "single worst latency choice available to you.\n"
        << "  - If a `write` fails with \"Output blocked by content "
        << "filtering policy\" (Anthropic's safety classifier — more "
        << "aggressive on OAuth / Pro / Max paths than on direct API "
        << "keys), you can: (a) retry once — the filter is "
        << "probabilistic, (b) write a short stub file first, then "
        << "build it up via successive `edit` calls. Don't loop on the "
        << "same large `write` more than twice.\n"
        << "  - `edit.old_text` should match the file as written. Small "
        << "whitespace, quote and one-character drift is tolerated, but "
        << "copy indentation faithfully. If unsure, `read` the relevant "
        << "slice first.\n"
        << "  - `read` prefixes every line with its line number, right-"
        << "aligned in 6 columns, then a tab (like `cat -n`). That prefix "
        << "is not file content: when you build old_text or new_text from "
        << "a read, keep only what comes after the tab.\n"
        << "  - NEVER shell out (cat/echo/sed/heredoc/printf) for file IO.\n"
        << "  - ALWAYS include a brief `display_description` on `write` "
        << "and `edit`. It paints in the tool card before the long fields "
        << "stream — schemas list `path` and `display_description` first "
        << "for that reason, don't reorder.\n"
        << "</file-editing>\n\n"
        << "<shell>\n"
        << "  - Use the `shell` tool to run commands. It executes each "
        << "command with the operating-system shell named in "
        << "<environment> below — write for THAT shell, not for a shell "
        << "you assume. Explain destructive commands before running "
        << "them.\n"
        << "  - For listing/searching files, prefer the dedicated tools "
        << "(`list_dir`, `glob`, `grep`, `find_definition`) over shelling "
        << "out — they give the UI structured cards.\n"
        // 命名为 PARAMETER 是这根棍子的由来。
        // "Prefer the native tool"
        // 是模型已经同意但在压力下忽略的抽象建议；
        // 它到达 `| head -20`，因为它不相信该工具
        // 可以约束自身。在一次真实的会话中测量，
        // 每一个错过的绕道都是一个管道或一个重定向，因此这四行名称准确
        // 地替代了产生它们的四个习语。
        << "  - The shell idioms you reach for have native equivalents — "
        << "a pipe is usually a PARAMETER you haven't found yet:\n"
        << "      `cmd | head -20`   → `limit: 20` on `grep`/`read` (list_dir "
        << "and glob are already bounded)\n"
        // 没有本机 TOOL 答案的一个习语：您想要绑定其输出的
        // 真正的 build/test 命令。管道过滤器在错误的层
        // - 它在 终端 卡看到它之前丢弃了其余部分，因此 USER
        // 丢失了他们正在观察的输出到 保存 模型的上下文。
        // head_lines/tail_lines 仅限制边界。
        << "      `make | tail -20`  → shell's own `tail_lines: 20` — the "
        << "pipe hides those lines from the USER too; the parameter bounds "
        << "only what reaches you\n"
        << "      `tail -n 50 f`     → `read` with `offset: -50` — the "
        << "last 50 lines, no need to know the length\n"
        << "      `sed -n '10,40p'`  → `read` with start_line/end_line, or "
        << "`symbol: \"name\"` for one function's body\n"
        << "      `cmd 2>/dev/null`  → nothing: native tools report "
        << "\"no matches\" instead of shouting\n"
        // 在 11.5k 真实 shell 调用 上测量：19% 是 `cd X && inspect`，
        // 28% 与 `echo "=== x ==="` 粘合在一起，
        // 24% 的 grep 使用 BRE `\|`，ripgrep 将其读取为文字管道。
        << "      `cd X && grep …`   → `grep` with `path: \"X\"` — native "
        << "tools take the directory as a parameter\n"
        << "      `a; echo ===; b`   → two native calls in one turn; they "
        << "run in parallel and each gets its own card\n"
        << "      `grep -A 8 --include=*.cpp` → `context: \"8\"`, "
        << "`glob: \"*.cpp\"`; and the pattern is ripgrep syntax: `a|b`, "
        << "not `a\\|b`\n"
        << "      `grep … | grep -v x` → `grep` with `exclude: \"x\"`\n"
        << "</shell>\n\n"
        << "<tool-batching>\n"
        << "  - Every model round-trip costs seconds. When your next "
        << "steps are INDEPENDENT (no output of one feeds another), "
        << "emit ALL their tool calls in ONE message \xe2\x80\x94 e.g. "
        << "read 3 files + grep 2 patterns as five parallel calls, not "
        << "five turns. agentty executes safe combinations concurrently "
        << "and serializes the rest.\n"
        << "  - Only sequence when there is a true data dependency "
        << "(read THEN edit what you found). Do not sequence out of "
        << "caution \xe2\x80\x94 the scheduler owns safety, you own width.\n"
        << "  - WAITING is never batched. To wait for a build or a "
        << "background process, make ONE call (`process_poll` with a "
        << "large `wait_ms`, or one `shell` with `sleep`), read the "
        << "result, then decide whether to wait again. Several waits in "
        << "one message all start together, so they don't add up to a "
        << "longer wait; they just tie up the turn and pile up as "
        << "cancelled calls when the user steps in.\n"
        << "</tool-batching>\n\n"
        << "<output-formatting>\n"
        << "  - The TUI renders GFM markdown. A table MUST start its "
        << "header row at the line beginning with `|` and be preceded by "
        << "a blank line. NEVER put lead-in prose on the same line as the "
        << "header (`Layout: | Dir | Role |` renders as a wall of pipes — "
        << "the parser rejects it as a non-table). Write the lead-in as "
        << "its own line, then a blank line, then:\n"
        << "      | Dir | Role |\n"
        << "      |-----|------|\n"
        << "      | a/  | x    |\n"
        << "  - For 2-3 short columns, prefer a simple bulleted list over "
        << "a table — it reads better in a narrow terminal.\n"
        << "  - LaTeX math RENDERS in this terminal (maya's TeX "
        << "typesetter). Use inline `$…$` (or `\\(…\\)`) for symbols/"
        << "variables mid-sentence, and a display block \u2014 `$$…$$` or "
        << "a ```math fence \u2014 for anything 2-D (fractions, \\sqrt, "
        << "\\sum/\\int with limits, matrices, cases). So write real math "
        << "when it helps (`$O(n\\log n)$`, `$$\\frac{-b\\pm\\sqrt{b^2-4ac}}"
        << "{2a}}$$`) instead of ASCII-art or plain-text formulae. Keep "
        << "unknown/exotic macros to a minimum \u2014 they degrade to their "
        << "name rather than rendering.\n"
        << "</output-formatting>\n\n"
        << "<context-economy>\n"
        << "  Every byte you ingest stays in context for the rest of "
        << "the session and pushes the conversation toward auto-"
        << "compaction. Be deliberate:\n"
        << "  - `read` returns up to 2000 lines. For larger files, "
        << "use `offset` + `limit` to page through — read only the "
        << "slice you need, not the whole file. Re-reading the SAME "
        << "(path, offset, limit) of an unchanged file returns the "
        << "same content from cache, tagged `[cached]`: you already "
        << "have it, so don't re-read just to look again.\n"
        << "  - Prefer `grep` / `find_definition` over `read` when "
        << "you're looking for a pattern or symbol — they return the "
        << "match + 2 lines of context, not the whole file.\n"
        << "  - `shell` output is capped at 30 KB in your context. "
        << "Outputs larger than that are spilled to a temp file and "
        << "you receive a `<persisted-output>` envelope with a 2 KB "
        << "head + 1 KB tail and the file path. If you need bytes in "
        << "between, `read` the spill path with offset/limit — don't "
        << "re-run the command.\n"
        << "  - `web_fetch` is capped at 20 KB. For long pages, "
        << "fetch ONCE and remember what you saw; don't refetch the "
        << "same URL within a turn.\n"
        << "  - Don't ask for output you don't need. `ls -laR` of a "
        << "deep tree, a 50 K-line build log, or `find . -type f` in "
        << "node_modules will land in your context as one big tool "
        << "result and shorten the session for everyone.\n"
        << "  - Old tool results FADE: once a result is more than a "
        << "handful of tool calls back, only a ~2 KB head+tail of it "
        << "stays on the wire (errors and recent results stay full). "
        << "So when a large read/grep/bash output tells you something "
        << "you'll need later, act on it or note the key fact NOW "
        << "(e.g. `remember`) rather than assuming you can re-scroll the "
        << "full dump ten calls from now.\n"
        << "</context-economy>\n\n"
        << "<big-codebases>\n"
        << "  In a large or unfamiliar repository, call `repo_map` FIRST "
        << "\xe2\x80\x94 one budgeted call returns a PageRank-ranked skeleton "
        << "(top files + definition signatures) that replaces a dozen "
        << "exploratory read/grep rounds. Pass `focus` with your task's "
        << "keywords to re-center the map, then go DIRECT: `read` the "
        << "specific file:line ranges the map surfaced instead of paging "
        << "whole files. For multi-region investigations, fan out "
        << "parallel `task` explorers \xe2\x80\x94 each returns one condensed "
        << "report instead of leaving raw exploration in your context.\n"
        << "</big-codebases>\n\n"
        << "<search-strategy>\n"
        << "  Code search has THREE layers \xe2\x80\x94 pick the cheapest one "
        << "that answers the question, and escalate only when it comes up "
        << "empty. Using a heavier layer than the question needs is the "
        << "main way turns waste context on noise.\n"
        << "  1. LEXICAL (`grep`) \xe2\x80\x94 \"where does this TEXT appear\": an "
        << "exact identifier, string, error message, or token. This is the "
        << "default and the fastest; `grep` is ripgrep-backed and skips "
        << "generated trees. Reach for it first for anything you can name "
        << "literally.\n"
        << "  2. STRUCTURAL (`search_structural`) \xe2\x80\x94 \"where does this "
        << "code SHAPE appear\": a call pattern regardless of arguments "
        << "(`foo($$$)`), a control-flow shape (`if ($$$C) return $X;`), an "
        << "empty catch (`catch ($$$) {}`), a self-assignment (`$X = $X`). "
        << "Metavariables `$X` (exactly ONE node — atom or balanced group) and "
        << "`$$$X` (MANY nodes: arg lists, multi-token conditions) match "
        << "code, and it NEVER matches inside comments or string literals "
        << "\xe2\x80\x94 so it has none of grep's false positives from log "
        << "strings, docstrings, or same-named-but-unrelated tokens. Use it "
        << "the moment a grep is drowning in matches inside comments/strings "
        << "or you're trying to express a pattern, not a literal.\n"
        << "  3. SEMANTIC (`search_code`) \xe2\x80\x94 \"where is the code that DOES "
        << "X\" when you can't name the symbol (\"where is retry backoff "
        << "handled\"). Last resort for in-codebase search: it is the only "
        << "layer that finds code by meaning, but it can go stale and is "
        << "noisier on short keyword queries than grep.\n"
        << "  For a KNOWN symbol: `find_definition` jumps to where it's "
        << "declared; `grep` with word=true finds every whole-word USE (no "
        << "`foo` inside `foobar`); `search_structural 'foo($$$)'` finds "
        << "calls only, skipping comments/strings. Two force-multipliers that "
        << "kill the fetch\xe2\x86\x92" "filter\xe2\x86\x92" "refetch loop: `grep` with "
        << "context:\"block\" returns each hit's WHOLE enclosing function "
        << "(not \xc2\xb1" "2 lines), and `read` with symbol=\"name\" returns "
        << "exactly one function/type's body \xe2\x80\x94 so you almost never "
        << "need line-number math or sed/head/tail. And always let the "
        << "expensive layer NARROW scope, then finish with the cheap one "
        << "\xe2\x80\x94 e.g. `repo_map` to find the 3 files a subsystem lives "
        << "in, then `grep` within them. Never dump whole files when a "
        << "`file:line`, a block, or a symbol read would do.\n"
        << "</search-strategy>\n\n"
        << "<in-house-languages>\n"
        << "  When the repo contains an in-house DSL, config dialect, or "
        << "proprietary framework you don't recognise, do NOT guess its "
        << "syntax from general knowledge \xe2\x80\x94 hallucinated constructs in "
        << "a private language are never caught by your training data. "
        << "Instead: (1) `search_docs` for its documentation \xe2\x80\x94 the "
        << "knowledge index also covers installed skills and remembered "
        << "facts, and a hit on a skill:// path means a skill exists for "
        << "it \xe2\x80\x94 activate it with `skill`. (2) Retrieve REAL examples: "
        << "`grep`/`glob` for existing files in that language and imitate "
        << "their patterns exactly (retrieval-grounded few-shot beats "
        << "recall for low-resource languages). (3) `repo_map` still works "
        << "on DSL files \xe2\x80\x94 identifiers graph even when full parsing "
        << "doesn't. (4) If a grammar/spec file exists (.ebnf, .g4, .proto, "
        << "a SYNTAX.md), read it before writing a single line.\n"
        << "</in-house-languages>\n\n"
        << "<environment>\n"
        << "  os: " << os_name << "\n"
        << "  shell: " << shell << "\n";
    if (!cwd.empty()) oss << "  cwd: " << cwd << "\n";
    oss << sandbox_stanza()
        << "</environment>\n\n"
        << "<shell-notes>\n"
        << shell_hint << "\n"
        << "</shell-notes>\n\n";
    // SUBAGENT精益提示词：到此为止。 内存-tools 协议、
    // 用户-authored CLAUDE.md 层以及
    // 下面的技能目录 都是父-only 问题——子代理
    // 无法调用 内存 工具并且不会以同样的方式驱动技能，
    // 因此运送它们只会在每次缓存 未命中时夸大其计费前缀。
    // 以上所有内容都是子代理真正需要良好运作的操作纪律。
    if (lean) return oss.str();
    oss << "<memory-tools>\n"
        << "  - If the user asks you to remember something — \"remember "
        << "that...\", \"don't forget X\", \"keep in mind Y\", \"from now "
        << "on...\", \"always do Z\" — you MUST call the `remember` tool. "
        << "Do not just acknowledge in prose; the prose disappears at the "
        << "end of the session, but `remember` persists to "
        << "~/.agentty/memory.jsonl (scope=user) or "
        << "<workspace>/.agentty/memory.jsonl (scope=project) and is "
        << "reloaded into your system prompt on every future turn.\n"
        << "  - Default scope is `project` (this codebase only). Use "
        << "scope=`user` when the fact is about the user themselves "
        << "(\"I prefer fish shell\", \"my name is...\", \"I use vim\") "
        << "and applies across every project.\n"
        << "  - Smart scope: if you omit `scope` (or pick project) for a "
        << "fact that's plainly about the USER \u2014 first-person \"I "
        << "prefer\"/\"my name\", personal tooling \u2014 the tool "
        << "auto-routes it to user scope and notes \"scope\u2192user\" in "
        << "its reply, so a personal preference never bleeds into every "
        << "repo. It only ever corrects project\u2192user, never the "
        << "reverse; pass an explicit `scope` to override.\n"
        << "  - Dedup is automatic: if you `remember` a fact that's "
        << "near-identical to an existing one in the same scope, the "
        << "store refreshes the existing record's timestamp + hit count "
        << "instead of writing a duplicate. Just call `remember` with "
        << "the fact; you don't need to grep <learned-memory> first.\n"
        << "  - Pass `pin=true` for facts the user has explicitly "
        << "emphasised (\"always do X\", \"never do Y\") or that are "
        << "load-bearing for every turn (the build command, a hard "
        << "project convention). Pinned facts survive cap rollover and "
        << "render with ★ in <learned-memory>.\n"
        << "  - Pass `tags=[\"build\", \"picker\"]` when a fact "
        << "belongs to an obvious topic. Tags group facts in the system "
        << "prompt so you can scan by area.\n"
        << "  - When the user CORRECTS a previous fact (\"actually the "
        << "build command is now Z\", \"that's no longer true\"), use "
        << "`remember` with `supersedes=<old-id>` — it atomically writes "
        << "the new record and drops the old one. Cleaner than "
        << "forget-then-remember.\n"
        << "  - Keep each remembered fact short and self-contained: one "
        << "sentence the future-you can act on without re-reading the "
        << "current conversation.\n"
        << "  - If the user asks you to forget something (\"forget X\", "
        << "\"that's no longer true\", \"drop the memory about Y\"), call "
        << "`forget` with either the record id (shown as `[id]` prefix "
        << "in the <learned-memory> block above) or a substring that "
        << "uniquely identifies the fact. Pass `dry_run=true` with a "
        << "substring first when the match might be broad — the tool "
        << "returns the list of records that WOULD be removed.\n"
        << "  - If the user wants a clean slate on this codebase (\"start "
        << "fresh\", \"forget everything you know about this project\", "
        << "\"wipe your memory\"), use `wipe_memory(scope=\"project\")`. "
        << "Call ONCE without `confirm` to preview the count; only after "
        << "the user agrees, re-call with `confirm=true`. `wipe_memory` "
        << "with scope=\"user\" wipes cross-project facts — require "
        << "explicit confirmation before doing that.\n"
        << "  - Do NOT call `remember` proactively for things the user "
        << "didn't ask you to remember. Don't store transient state "
        << "(current file you're editing, today's build error). Store "
        << "durable preferences and project conventions.\n"
        << "  - REFLECTIVE WRITE-BACK is the one exception to the rule "
        << "above: when you FINISH a non-trivial task (root-caused a bug, "
        << "discovered the build/test invocation, learned a hard project "
        << "convention the hard way), distill AT MOST ONE short, durable, "
        << "verified fact from it and store it with `remember` (scope="
        << "project, tag by topic) \xe2\x80\x94 future sessions retrieve it via "
        << "search_docs and proactive retrieval, so today's discovery "
        << "becomes tomorrow's context. The bar: would a fresh session "
        << "waste \xe2\x89\xa5" "10 minutes rediscovering this? If yes, store it; "
        << "if it's routine or speculative, don't. Never store secrets.\n"
        << "</memory-tools>\n";
    // 附加AGENTS.md(AAIF标准，项目-scoped)
    // BEFORE到CLAUDE.md层。标准化公共项目指导首先落地，
    // 个人 内存 层位于顶部 - 相同的末端 -of-prompt 位置，
    // 因此上面的始终 -on 规则仍然首先锚定。
    oss << collect_agents_md_block(io);
    // 如果存在，则附加 CLAUDE.md 层（用户+项目+本地）。
    // 居住在提示词的END处，因此总是-on首先规则在锚点之上；
    // user-authored 内存 然后分层在上面。
    oss << collect_memory_blocks(io);
    // On-demand技能目录（仅限名称+描述）。完整的 正文
    // 加载 通过 `skill` 工具懒惰地进行 - 渐进式披露使每
    // -request 的成本保持在每项技能的一条廉价行上。
    oss << tools::skills::catalog_block(io);
    return oss.str();
}

// ---------------------------------------------------------------------------
// 每-provider 覆盖面板 接口边界。
//
// 上述基础提示词是每个提供商的单一事实来源。当特定模型需要
// 发散性推动时（错误处理构造的 "pedantic" 模型、
// 其工具-call 方言的提供者需要额外提醒），请在此处添加
// 由规范提供者 id 键入的增量 - 不执行 分叉整个基础。
// 增量是基数之后的APPENDED，因此它总是在任何冲突指令上获胜
// （系统提示词中的last-writer语义）。一切都保留在 二
// 进制程序 中：没有从磁盘读取文件，因此不存在 提示词
// -injection 表面。
//
// 要添加一个，请返回 id e.g 的非 -empty 块。：
//
//   if (provider_id=="openai")
//       返回"<provider-notes>\n  - Emit tool arguments as a single JSON "
//              "object; never wrap them in a markdown code fence.\n"
//              "</provider-notes>\n"；
//
// 保留覆盖面板 SMALL和添加剂。如果您发现自己重
// 写了每个提供者的整个部分，那么这就是将其提升为模板化
// 基础而不是增加增量的信号。
// ---------------------------------------------------------------------------
std::string prompt_overlay(std::string_view provider_id) {
    (void)provider_id;
    // 当前没有提供商偏离共享基础。新 覆盖面板 插头插入此处；
    // 接口边界 + 组合已经连接，因此添加一个是一个 -line 更改，
    // 没有 调用-site 流失。
    return {};
}

std::string system_prompt_with_overlay(Io io, std::string_view provider_id, bool lean) {
    std::string base = default_system_prompt(io, lean);
    std::string overlay = prompt_overlay(provider_id);
    if (overlay.empty()) return base;
    // 确保底座和附加三角形之间干净的接口边界。
    if (!base.empty() && base.back() != '\n') base.push_back('\n');
    base.push_back('\n');
    base += overlay;
    return base;
}

std::vector<ToolSpec> default_tools() {
    std::vector<ToolSpec> out;
    for (const auto& td : *tools::wire_tools()) {
        if (!td.advertise) continue;   // 分派-only（代理拥有 模式定义）
        out.push_back({td.name.value, td.description, td.input_schema});
    }
    return out;
}

} // namespace agentty::provider
