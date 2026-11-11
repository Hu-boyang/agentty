#pragma once
#include <maya/element/element.hpp>
#include "agentty/runtime/model.hpp"

namespace agentty::ui {

// 身份标签：来自maya的ModelBadge的彩色
// 模型标签 ("● Sonnet")，加上暗色提供商后缀（
// "· Anthropic")，因此BOTH活动模型和活动
// 后端一目了然。它位于作曲家的页脚行而不是状态栏：状态栏是
// TURN 的表面（阶段、吞吐量、上下文），并在 流式传输
// 时进行搅动，而模型是您将要 SEND 的内容的属性 -
// 因此它属于您输入的内容旁边，在那里它也不再与阶段 标签
// 竞争棚屋预算。返回一个构建的元素（在这里组成，而不是通过
// single-string 小部件，因此它可以携带这两个事实）。
[[nodiscard]] maya::Element model_badge_config(const Model& m);

} // namespace agentty::ui
