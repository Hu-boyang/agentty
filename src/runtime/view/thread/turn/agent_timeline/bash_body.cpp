// tool_body_preview — `bash` / `diagnostics` 工具 正文.
//
// BashOutput (末尾-oriented 结构 -extraction 回
// 退 链条 maya 捡起口香糖 -style `N tests passed`
// 提要和 编译器-error 排队,否则掉到4号深处 -row 末尾).
// `failed` 被线用于内线 `· exit N` 后缀, 位于失败输出的最后一行。
// BashOutput 故障时(而不是去向) Kind::Failure ),
// 所以时间卡边框+状态图标上带有故障信号和 正文 保持冷静...
// agent_session.cpp's "no double-flagging" 纪律。

#include "tool_body_common.hpp"

#include <string>
#include <utility>

#include "agentty/runtime/view/palette.hpp"
#include "agentty/runtime/view/thread/turn/agent_timeline/tool_args.hpp"

namespace agentty::ui::detail {

bool bash_body(const ToolUse& tc, maya::ToolBodyPreview::Config& out) {
    using Kind = maya::ToolBodyPreview::Kind;

    if (tc.is_running() && !tc.progress_text().empty()) {
        out.kind = Kind::BashOutput;
        out.text = clean_output(tc.progress_text());
        out.text_color = text_primary;   // 纯输出读作普通文本
        out.is_streaming = true;
        return true;
    }
    if (tc.is_terminal()) {
        auto stripped = clean_output(tc.is_failed()
            ? strip_bash_output_fence(failure_body_text(tc))
            : strip_bash_output_fence(card_output(tc)));
        if (!stripped.empty()) {
            out.kind = Kind::BashOutput;
            out.text = std::move(stripped);
            out.text_color = text_primary;   // 纯输出读作普通文本
            out.failed = tc.is_failed();
        }
        return true;
    }
    return true;
}

} // namespace agentty::ui::detail
