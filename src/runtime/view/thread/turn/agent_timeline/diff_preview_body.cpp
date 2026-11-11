// tool_body_preview：replace / rewrite_structural 的试运行及应用后预览正文。
//
// 这两个工具按命中位置生成带真实行号的 -old / +new 预览，
// 但原始输出带缩进、路径或 L<n>: 前缀，不是统一差异格式。
// maya 的 GitDiff 根据行首 + / - 识别内容，因此需要先转换。
// 此处将原始预览整理为 GitDiff 能理解的最小统一差异：
//
//   replace:              rewrite_structural:
//     src/a.cpp:12 (2 hits)   src/a.cpp:
//       - foo(bar)              L12: - foo(bar)
//       + foo(baz)              L12: + foo(baz)
//
//   →  +++ b/src/a.cpp
//      @@ 12 @@              （行号锚点，由 GitDiff 淡色显示）
//      -foo(bar)
//      +foo(baz)
//
// 开头的 DRY RUN 摘要会被移除，因为命中数已经显示在事件标题中。
// 如果没有可解析的 -/+ 预览，例如没有匹配项，则回退为淡色代码块。
//
//
//

#include "tool_body_common.hpp"
#include "agentty/util/digits.hpp"

#include <string>
#include <string_view>

#include "agentty/runtime/view/palette.hpp"

namespace agentty::ui::detail {

namespace {

// 引领到头 `L<digits>:` 关闭 a rewrite_structural
// 预览行 正文 ,返回行号(或 0)并推进 `s` 过它。
int take_line_prefix(std::string_view& s) {
    auto t = s;
    while (!t.empty() && t.front() == ' ') t.remove_prefix(1);
    if (t.empty() || t.front() != 'L') return 0;
    t.remove_prefix(1);
    int n = 0;
    bool got = false;
    while (!t.empty() && t.front() >= '0' && t.front() <= '9') {
        n = ::agentty::util::push_digit(n, t.front());
        t.remove_prefix(1);
        got = true;
    }
    if (!got || t.empty() || t.front() != ':') return 0;
    t.remove_prefix(1);
    s = t;               // 消费 `L<n>:`
    return n;
}

// 这是... `path:` 或 或 时 间 `path:line  (N hits)` 文件
// -header 线条 返回路径( 不跟踪) `:` /数)当是.
std::string_view file_header(std::string_view line) {
    // 拒绝显然为-/+预览或空白标记的行。
    auto t = line;
    while (!t.empty() && t.front() == ' ') t.remove_prefix(1);
    if (t.empty()) return {};
    if (t.front() == '-' || t.front() == '+' || t.front() == 'L') return {};
    // 头文件以`:`为结束,可选后依次为`<line>  (N hit[s])`.
    auto colon = line.find(':');
    if (colon == std::string_view::npos) return {};
    // 第一条结肠之前的一切就是路径(路径在这里从不包含结肠——
    // 它们就是. 工作区-relative).
    return line.substr(0, colon);
}

} // 命名空间结束。

bool diff_preview_body(const ToolUse& tc, maya::ToolBodyPreview::Config& out) {
    using Kind = maya::ToolBodyPreview::Kind;
    if (!tc.is_done()) return false;
    const std::string_view raw = card_output(tc);
    if (raw.empty()) return true;

    std::string diff;
    diff.reserve(raw.size());
    std::string_view current_path;
    int pending_line = 0;
    bool any = false;

    std::size_t pos = 0;
    while (pos < raw.size()) {
        const auto nl = raw.find('\n', pos);
        const auto end = (nl == std::string_view::npos) ? raw.size() : nl;
        std::string_view line(raw.data() + pos, end - pos);
        pos = (nl == std::string_view::npos) ? raw.size() : nl + 1;

        // 调整一个跟踪 '\r'.
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

        // 文件头文件吗?
        if (auto path = file_header(line); !path.empty()) {
            current_path = path;
            diff += "+++ b/";
            diff += path;
            diff += '\n';
            // 如果 头文件 带行号( 替换 : `path:line  …` ),
            // 放出一个锚,这样第一个 差异块 显示其位置。
            auto colon = line.find(':');
            int hl = 0;
            for (std::size_t i = colon + 1; i < line.size()
                 && line[i] >= '0' && line[i] <= '9'; ++i)
                hl = hl * 10 + (line[i] - '0');
            pending_line = hl;
            continue;
        }

        // 预览 正文 线条:可选缩进,可选 `L<n>:` ,则 -/+.
        std::string_view body = line;
        int lno = take_line_prefix(body);   // rewrite_structural
        while (!body.empty() && body.front() == ' ') body.remove_prefix(1);
        if (body.empty()) continue;

        if (body.front() == '-' || body.front() == '+') {
            const char sign = body.front();
            body.remove_prefix(1);
            if (!body.empty() && body.front() == ' ') body.remove_prefix(1);
            // 在某组的FIRSTXXXX'-'之前,将一个差异块锚点发送,
            // 这样读者就能看到行号. 替换后在头文件(pending_line)上载;
            // 重写后再载它(-line(lno)).
            if (sign == '-') {
                int at = lno ? lno : pending_line;
                if (at > 0) {
                    diff += "@@ L";
                    diff += std::to_string(at);
                    diff += " @@\n";
                }
                pending_line = 0;
            }
            diff += sign;
            diff += body;
            diff += '\n';
            any = true;
        }
        // 其他一切(摘要行,"[capped]",空格注)均跳过.
    }

    if (!any) {
        // 没有可解析预览——保留消息可读性,头-anchored.
        out.kind = Kind::CodeBlock;
        out.text = std::string{raw};
        out.text_color = text_tertiary;
        out.tail_only = false;
        return true;
    }
    out.kind = Kind::GitDiff;
    out.text = std::move(diff);
    out.text_color = text_tertiary;
    // 点亮这些工具的字面内容, 在-/+行内搜索,
    // 所以针头在周围的周围环境中突出出来, 在每行。
    out.match_needle = args_match_needle(tc.args);
    return true;
}

} // namespace agentty::ui::detail
