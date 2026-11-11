// tool_body_preview — `git_diff` 工具 正文.
//
// GitDiff 单位:每名 -line +/-/@@ 颜色( N) GitDiff
// 拥有调色板。只有 a DONE git_diff 生成一个 正文 静态
// -running 失败者返回错误, 所以 分派器 都输给了共同的失败 回退.

#include "tool_body_common.hpp"

#include <string>

#include "agentty/runtime/view/palette.hpp"

namespace agentty::ui::detail {

bool git_diff_body(const ToolUse& tc, maya::ToolBodyPreview::Config& out) {
    using Kind = maya::ToolBodyPreview::Kind;
    if (!tc.is_done()) return false;
    const std::string_view body = card_output(tc);
    if (!body.empty() && body != "no changes") {
        out.kind = Kind::GitDiff;
        out.text = std::string{body};
        out.text_color = text_tertiary;
    }
    return true;
}

} // namespace agentty::ui::detail
