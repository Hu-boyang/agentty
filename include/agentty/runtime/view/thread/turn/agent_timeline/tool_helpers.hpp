#pragma once
// Per-tool view-side 帮助程序。 不 适用于任何单个小部件的适
// 配器 - 这些是 AgentTimeline 适配器使用的纯映射（显示名称、
// 类别 color/label、事件 状态、one-line 详细信息）。
//
// 纯数据——没有Element工作，没有Config构造。

#include <string>
#include <string_view>

#include <maya/style/color.hpp>
#include <maya/widget/agent_timeline.hpp>

#include "agentty/domain/conversation.hpp"

namespace agentty::ui {

[[nodiscard]] std::string             tool_display_name(const std::string& name);
[[nodiscard]] maya::Color             tool_category_color(const std::string& name);
[[nodiscard]] std::string_view        tool_category_label(const std::string& name);
[[nodiscard]] maya::AgentEventStatus  tool_event_status(const ToolUse& tc);
[[nodiscard]] std::string             tool_timeline_detail(const ToolUse& tc);

} // namespace agentty::ui
