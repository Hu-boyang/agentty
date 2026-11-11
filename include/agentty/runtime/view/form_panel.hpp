#pragma once
// agentty::ui-将form::Form投影到maya::Panel::Config上。
//
// 每个配置面板的整个渲染表面是该ONE功能。
// 面板 的视图变为：
//
//   maya::Form{form_config(pane.form，信息)}.build()
//
// 并且面板本身不贡献镀铬——外观完全是maya::Form的，
// 因此面板不能像五手-written 选择
// 面板那样彼此漂移。

#include <maya/style/color.hpp>
#include <maya/widget/panel.hpp>

#include "agentty/runtime/panel/form.hpp"

namespace agentty::ui {

// 设置面板随着注册表增长，因此WILL增长超过了短终端。
// `scroll` 必须比返回的配置元素的寿命更长（它位于
// Model::UI 中）；传递 null 会禁用滚动，只有当调用者
// 可以保证行适合时，这才是正确的。
//
// `max_width`将面板的min-width钳位到终端实际具有的值。
// 较窄的 终端 中的固定 60 列使面板比屏幕更宽，
// 并且 覆盖面板 将其居中，因此左半部分（每个标签）会从
// -screen 上滑落。 0 表示 "no clamp"。
// 它位于 HOST 一侧，因为主机知道 终端 大小；小部件必须从
// 布局 回调 中学习它，并且这些布局必须声明它们无法知道的高度。
[[nodiscard]] maya::Panel::Config form_config(const agentty::form::Form& f,
                                             maya::Color accent,
                                             maya::ScrollState* scroll = nullptr,
                                             int viewport_h = 0,
                                             int max_width = 0);

} // namespace agentty::ui
