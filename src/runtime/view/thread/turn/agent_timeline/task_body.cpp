// tool_body_preview — `task` (水手)工具 正文.
//
// 运行时提供活性反馈, 结算时提供压缩报告。 反馈被流到 progress_text
// (通过) progress::emit (转动/工具) 调用 / 结果/流式文本;
// 终端 输出为已收获 "Subagent report".
// BashOutput 给 末尾-oriented "watch it work"
// 运行时的外观; CodeBlock 显示完整报告已解决。

#include "tool_body_common.hpp"

#include <cstddef>
#include <string>
#include <string_view>

#include "agentty/runtime/view/palette.hpp"

namespace agentty::ui::detail {

bool task_body(const ToolUse& tc, maya::ToolBodyPreview::Config& out) {
    using Kind = maya::ToolBodyPreview::Kind;

    // 纸牌 正文 上限: LIVE 提供空间增加(这是进入副
    // 代理商所从事工作的唯一窗口)——QQ 调用 ,“结果”,
    // “ 重试 - 并且它是一个短暂的,而 SETTLED
    // 报告要仔细看 -able 报告全文载于记录和 Ctrl
    // +O 查看器.
    constexpr std::size_t kTaskLiveRows   = 8;
    constexpr std::size_t kTaskReportRows = 5;
    if (tc.is_running()) {
        if (!tc.progress_text().empty()) {
            out.kind = Kind::BashOutput;
            // BashOutput 谓 末尾-oriented
            // (底部最新活动)——保留最后几条支线,
            // 所以运行卡显示 当前 步号,不是 过期 头文件.
            out.text = tail_window(tc.progress_text(), kTaskLiveRows);
            out.text_color = text_primary;   // 纯输出读作普通文本
            out.is_streaming = true;
        }
        return true;
    }
    if (tc.is_failed()) return true;   // generic_lifecycle_body 拥有失败
    if (tc.is_terminal() && !tc.output().empty()) {
        // 脱去多余的 "Subagent report (type, N turns):"
        // 头文件 + 后面的空白行——卡片细节
        // 已经显示类型和转动计数,所以 正文 应以
        // 实际结果线为首。
        std::string_view body = tc.output();
        if (auto nl = body.find('\n'); nl != std::string_view::npos
            && body.substr(0, nl).find("Subagent report") != std::string_view::npos) {
            body.remove_prefix(nl + 1);
            while (!body.empty() && (body.front() == '\n' || body.front() == ' '))
                body.remove_prefix(1);
        }
        out.kind = Kind::CodeBlock;
        // 保留内容行+ 1 "⋯ N more" 标记 = 天花板行最大值。
        out.text = head_window(body, kTaskReportRows - 1);
        out.text_color = text_primary;   // 纯输出读作普通文本
        // 我们先 -sliced 升至最高点; 渲染 逐字记录(无
        // 其他标题+) 末尾 消灭会与我们的标记抗衡的
        out.show_all = true;
    }
    return true;
}

} // namespace agentty::ui::detail
