// tool_body_preview — `web_fetch` 工具 正文.
//
// JSON (非常漂亮,非常漂亮, -printed key/value。
// 仅限 a DONE web_fetch 生成一个 正文 ; 否则返回错误, 以陷入共同的失败 回退.

#include "tool_body_common.hpp"

#include <string>

#include "agentty/runtime/view/palette.hpp"

namespace agentty::ui::detail {

bool web_fetch_body(const ToolUse& tc, maya::ToolBodyPreview::Config& out) {
    using Kind = maya::ToolBodyPreview::Kind;
    if (!tc.is_done()) return false;
    const std::string_view body = card_output(tc);
    if (!body.empty()) {
        // 页面从顶部(标题,引文段落); 末尾 脚的铬。
        // JSON 保留为 JSON.
        const auto first = body.find_first_not_of(" \n\t");
        const bool json = first != std::string_view::npos
                       && (body[first] == '{' || body[first] == '[');
        out.kind = json ? Kind::Json : Kind::CodeBlock;
        out.text = clean_output(body);
        // 段落之间的空行每行花费5-row预览一行.
        if (!json) {
            std::string dense;
            dense.reserve(out.text.size());
            for (std::size_t p = 0; p <= out.text.size();) {
                auto e = out.text.find('\n', p);
                if (e == std::string::npos) e = out.text.size();
                std::string_view ln{out.text.data() + p, e - p};
                if (ln.find_first_not_of(" \t") != std::string_view::npos) {
                    dense.append(ln);
                    dense += '\n';
                }
                if (e == out.text.size()) break;
                p = e + 1;
            }
            if (!dense.empty()) dense.pop_back();
            out.text = std::move(dense);
        }
        out.text_color = text_primary;
        out.tail_only = false;
        out.code_head = 5;
        out.code_tail = 0;
    }
    return true;
}

} // namespace agentty::ui::detail
