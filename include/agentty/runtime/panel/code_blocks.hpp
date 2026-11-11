#pragma once
// 代码-block 选择面板 —"run the thing the AI just told me to run"
// 无副本-paste-then-clean舞蹈。
//
// Ctrl+G 扫描最近一条助手回复中的 ``` 围栏代码块，并弹出
// 上面的紧凑选择面板作曲家：每块一行（语言+first
// -line预览）。从那里：
//
//   Enter/1-9 → RUN 在 真实 终端 (maya
//                  暂停+/bin/sh -c + stdout/stderr
//                  三通)上本地交互地块。 sudo 密码 提示
//                  词 有效；实时输出 数据流。退出时，结果卡将打开，
//                  其中包含捕获的副本：附加到作曲家/复制/丢弃。
//   e→首先将CLEANED 正文放入作曲器中进行编辑。
//   y→将清理后的正文复制到剪贴板。
//
// "Cleaned"意味着：栅栏被剥离，并且——当该块是
// 粘贴的终端转录本，其中EVERY命令行携带`$ ` 提示
// 词时——提示词标记也被删除。 all-lines 规则很
// 重要：`#` 在真实的 shell 脚本中开始注释，
// 因此盲目地从各个行中删除 `# `/`$ ` 会损坏合法脚本。
// 只有统一的转录模式才是明确的。
//
// 运行仅限于当前平台具有解释器的块：
// POSIX通过/bin/sh运行sh/bash/zsh/shell/console/terminal
// (和裸栅栏)； Windows 运行 cmd/bat，并通过 cmd.exe 和
// powershell/pwsh/ps1 至 PowerShell 裸露围栏。
// 此平台无法运行的语言中的块（Linux 上的 e.g.`powershell`，
// 或任何地方的 `python`/`js` 片段）仍然提供 edit/copy，
// 但运行会显示 toast，而不是执行垃圾。参见 shell_for_language()。
//
// 该头文件仅是UI-state+纯提取物。 状态更新函数 接线位于 update/codeblock.cpp，
// 键 分派 位于 subscribe.cpp，
// 视图位于 view/pickers.cpp — 与另一个 选择面板 分割的文件相同。

#include <cstddef>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace agentty {

// 一个提取的围栏块。 `language` 是栅栏信息字符串（lower
// -cased，仅第一个字 — "sh"、"python"、"" 表示裸栅栏）。
// `body` 已经被清理（见上文）并且结束时
// 没有尾随换行符，
// 因此作曲家插入/剪贴板写入不会携带悬空空行。
struct CodeBlock {
    std::string language;
    std::string body;
    int         line_count = 0;
};

namespace code_blocks {

// 哪个解释器应该运行给定的栅栏语言——并且隐含地，该块是否可以在 此 平台上运行。
// 该集合是 platform-aware：`powershell` 块
// 可以在 Windows 上运行，但不能在 POSIX 上运行（没有什么理智的
// 东西可以交给它），`bash` 块可以在 POSIX 上运行，
// 但只能在 Windows 上机会性地运行（需要在 PATH、e.g、
// Git-Bash/WSL 上进行 bash - 我们不这样做）假设它）。
// 选择面板 的运行门和跑步者都参考了这个 枚举，所以他们从来没有不同意见。
enum class BlockShell {
    None,        // 无法在此平台上运行
    Posix,       // /bin/sh -c
    Cmd,         // Windows cmd.exe（默认外壳包装器）
    PowerShell,  // powershell -NoProfile -Command
};

// 将围栏信息字符串（已经是 lower-cased，
// 仅第一个单词）分类到应该在 当前 平台上运行它的解释器中。
[[nodiscard]] inline BlockShell shell_for_language(std::string_view lang) noexcept {
    const bool posixish =
        lang == "sh" || lang == "bash" || lang == "zsh" || lang == "shell"
        || lang == "console" || lang == "terminal" || lang == "posix"
        || lang == "shell-session" || lang == "shellsession";
    const bool powershellish =
        lang == "powershell" || lang == "pwsh" || lang == "ps" || lang == "ps1";
    const bool cmdish =
        lang == "cmd" || lang == "bat" || lang == "batch" || lang == "dos"
        || lang == "winbatch" || lang == "cmd.exe";

#if defined(_WIN32)
    // 在 Windows 上，裸围栏的“本地”外壳是 cmd.exe。
    // PowerShell块通过powershell运行；作为最大努力，
    // 显式的 posix 块也被传递给默认的 shell 包装器（
    // cmd.exe）——在 Windows 会话上发出 `bash` 的
    // 模型通常意味着 "the shell"，如果它真的是 bash-only，
    // 它只会出错，与任何错误的命令相同。这是用户明确要求的运行。
    if (powershellish)          return BlockShell::PowerShell;
    if (cmdish)                 return BlockShell::Cmd;
    if (posixish || lang.empty()) return BlockShell::Cmd;
    return BlockShell::None;
#else
    // POSIX：裸围栏和 posix 语言转到 /bin/sh. cmd/
    // powershell 块是 Windows-only — 没有什么可以运行它们。
    if (posixish || lang.empty()) return BlockShell::Posix;
    (void)powershellish;
    (void)cmdish;
    return BlockShell::None;
#endif
}

// 该块可以在当前平台上运行吗？ （跑门+微移
// 计数器都使用这个——跑步者的单一事实来源。）
[[nodiscard]] inline bool is_shell_language(std::string_view lang) noexcept {
    return shell_for_language(lang) != BlockShell::None;
}

// 从转录物-style块中剥离均匀的`$ `/`> `
// 提示词装饰。当每条非 -empty 行都以其中一个
// 标记（安全模式）开头时，应用 仅；混合行表示真实的
// 脚本内容（注释、输出行）并且保持不变。
[[nodiscard]] inline std::string strip_prompt_prefixes(std::string body) {
    // 第一遍：每个非-empty行是否以"$ "或"> "开始？
    bool uniform = true;
    bool any     = false;
    std::size_t pos = 0;
    while (pos <= body.size()) {
        std::size_t eol  = body.find('\n', pos);
        std::size_t len  = (eol == std::string::npos ? body.size() : eol) - pos;
        std::string_view line{body.data() + pos, len};
        if (!line.empty()) {
            any = true;
            if (!(line.size() >= 2
                  && (line[0] == '$' || line[0] == '>')
                  && line[1] == ' ')) {
                uniform = false;
                break;
            }
        }
        if (eol == std::string::npos) break;
        pos = eol + 1;
    }
    if (!uniform || !any) return body;
    // 第二遍：在没有2-byte标记的情况下重建。
    std::string out;
    out.reserve(body.size());
    pos = 0;
    while (pos <= body.size()) {
        std::size_t eol = body.find('\n', pos);
        std::size_t len = (eol == std::string::npos ? body.size() : eol) - pos;
        std::string_view line{body.data() + pos, len};
        if (!line.empty()) line.remove_prefix(2);
        out.append(line);
        if (eol == std::string::npos) break;
        out.push_back('\n');
        pos = eol + 1;
    }
    return out;
}

// 从 Markdown 文本中提取每个受防护的代码块。容忍
// 模型实际输出的两种围栏形式：``` 和 ~~~，可以附带
// 信息字符串。 EOF 处未终止的栅栏仍然会产生一个块（数据流
// 可能已在 -reply 中期被切断；用户仍然可以看到+运行
// 到达的内容）。可以识别缩进栅栏（最多 3 个空格，
// CommonMark 限制），因此不会错过嵌套在列表项下的块。
[[nodiscard]] inline std::vector<CodeBlock> extract_code_blocks(std::string_view text) {
    std::vector<CodeBlock> out;
    std::size_t pos = 0;
    bool        in_block = false;
    char        fence_ch = '`';
    std::string lang;
    std::string body;

    auto flush = [&] {
        // 修剪尾随换行符——关闭栅栏位于其自己的行上，
        // 因此 正文 始终以 1 结尾。
        while (!body.empty() && (body.back() == '\n' || body.back() == '\r'))
            body.pop_back();
        if (!body.empty()) {
            CodeBlock b;
            b.language = lang;
            b.body     = strip_prompt_prefixes(std::move(body));
            b.line_count = 1;
            for (char c : b.body) if (c == '\n') ++b.line_count;
            out.push_back(std::move(b));
        }
        body.clear();
        lang.clear();
    };

    while (pos <= text.size()) {
        std::size_t eol = text.find('\n', pos);
        std::size_t len = (eol == std::string_view::npos ? text.size() : eol) - pos;
        std::string_view line = text.substr(pos, len);
        // 去除尾随\r (CRLF-tolerant)。
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

        // 在栅栏之前允许最多3个前导空格(CommonMark)。
        std::string_view t = line;
        int indent = 0;
        while (!t.empty() && t.front() == ' ' && indent < 3) { t.remove_prefix(1); ++indent; }

        const bool is_fence = t.size() >= 3
            && (t[0] == '`' || t[0] == '~')
            && t[1] == t[0] && t[2] == t[0];

        if (!in_block) {
            if (is_fence) {
                in_block = true;
                fence_ch = t[0];
                // 信息字符串：围栏运行后的第一个单词，lower-cased。
                std::size_t i = 3;
                while (i < t.size() && t[i] == fence_ch) ++i;
                while (i < t.size() && t[i] == ' ') ++i;
                std::size_t start = i;
                while (i < t.size() && t[i] != ' ' && t[i] != '\t') ++i;
                lang.assign(t.substr(start, i - start));
                for (char& c : lang)
                    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
            }
        } else {
            // 结束栅栏必须与开始字符匹配，并且行上除了栅栏字符
            // 之外什么都没有（CommonMark允许尾随空格；
            // 信息字符串是opening-only）。
            bool is_close = is_fence && t[0] == fence_ch;
            if (is_close) {
                std::size_t i = 0;
                while (i < t.size() && t[i] == fence_ch) ++i;
                while (i < t.size() && t[i] == ' ') ++i;
                is_close = (i == t.size());
            }
            if (is_close) {
                in_block = false;
                flush();
            } else {
                body.append(line);
                body.push_back('\n');
            }
        }

        if (eol == std::string_view::npos) break;
        pos = eol + 1;
    }
    // EOF 处未端接的栅栏 — 保留我们所拥有的。
    if (in_block) flush();
    return out;
}

// Mid-stream-safe提取：与extract_code_blocks类似，
// 但是DROPS是栅栏尚未关闭的区块（仍在活动边缘的带类型的）。
// 运行 half-typed 命令是危险的——模型可能还没有
// 完成该行——因此，虽然回复仍然是 流式传输，但我们只提供块
// 结束 ``` 已到达的代码块。可选参数 complete_out 接收
// 尾随块是否仍然打开，因此调用者可以提示"a block is still streaming"
// 与完全没有代码块的情况区分。
[[nodiscard]] inline std::vector<CodeBlock>
extract_closed_code_blocks(std::string_view text, bool* had_open = nullptr) {
    // 通过re-scanning栅栏线检测未终止的尾随栅栏：
    // 奇数个fence-delimiter线(打开而没有匹配的
    // 关闭)意味着最后打开的块从未关闭。
    auto blocks = extract_code_blocks(text);
    bool open_tail = false;
    {
        std::size_t pos = 0;
        bool in_block = false;
        char fence_ch = '`';
        while (pos <= text.size()) {
            std::size_t eol = text.find('\n', pos);
            std::size_t len =
                (eol == std::string_view::npos ? text.size() : eol) - pos;
            std::string_view line = text.substr(pos, len);
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            std::string_view t = line;
            int indent = 0;
            while (!t.empty() && t.front() == ' ' && indent < 3) {
                t.remove_prefix(1); ++indent;
            }
            const bool is_fence = t.size() >= 3
                && (t[0] == '`' || t[0] == '~') && t[1] == t[0] && t[2] == t[0];
            if (!in_block) {
                if (is_fence) { in_block = true; fence_ch = t[0]; }
            } else if (is_fence && t[0] == fence_ch) {
                std::size_t i = 0;
                while (i < t.size() && t[i] == fence_ch) ++i;
                while (i < t.size() && t[i] == ' ') ++i;
                if (i == t.size()) in_block = false;   // 有效关闭
            }
            if (eol == std::string_view::npos) break;
            pos = eol + 1;
        }
        open_tail = in_block;
    }
    if (had_open) *had_open = open_tail;
    // extract_code_blocks最后附加了still-open块；放下它。
    if (open_tail && !blocks.empty()) blocks.pop_back();
    return blocks;
}

struct Closed {};

struct Open {
    // 阻止包含任何文档顺序的最新助理消息（首先是回复的顶部，
    // 因此 "block 1" 会根据屏幕
    // 上的内容自然地读取）。
    std::vector<CodeBlock> blocks;
    int                    index = 0;
};

// Post-run决定状态。交互式运行完成，
// 用户已经在真实的 终端 上观看了实时输出；现在他们选择
// CAPTURED 副本会发生什么：作为输出 标签 附加
// 到作曲家，将其干净地复制到剪贴板，或者丢弃（on-screen
// 转录本保留在 TUI 上方的本机 回滚缓冲区 中）。
// 持有字节 此处——而不是auto-staging
// ——是重点：作曲家仅接收用户明确要求的输出。
struct Result {
    std::string command;
    std::string output;
    int         exit_code = 0;
    bool        timed_out = false;
};

} // namespace code_blocks

using CodeBlocksState =
    std::variant<code_blocks::Closed, code_blocks::Open,
                 code_blocks::Result>;

[[nodiscard]] inline bool code_block_picker_is_open(const CodeBlocksState& s) noexcept {
    return std::holds_alternative<code_blocks::Open>(s);
}
[[nodiscard]] inline bool code_block_result_is_open(const CodeBlocksState& s) noexcept {
    return std::holds_alternative<code_blocks::Result>(s);
}
[[nodiscard]] inline       code_blocks::Open* code_block_picker_opened(CodeBlocksState& s)       noexcept { return std::get_if<code_blocks::Open>(&s); }
[[nodiscard]] inline const code_blocks::Open* code_block_picker_opened(const CodeBlocksState& s) noexcept { return std::get_if<code_blocks::Open>(&s); }
[[nodiscard]] inline       code_blocks::Result* code_block_result(CodeBlocksState& s)       noexcept { return std::get_if<code_blocks::Result>(&s); }
[[nodiscard]] inline const code_blocks::Result* code_block_result(const CodeBlocksState& s) noexcept { return std::get_if<code_blocks::Result>(&s); }

} // namespace agentty
