#pragma once
// 作曲家的`/`菜单：拾取的行slash::menu_rows，
// 绘制在作曲家框的正上方。菜单隐藏时为空元素。
#include <maya/element/element.hpp>
#include "agentty/runtime/model.hpp"

namespace agentty::ui {

[[nodiscard]] maya::Element slash_menu(const Model& m);

// 菜单将显示的行（隐藏时为0）。作曲家的提示行读
// 取它以切换到菜单的键。
[[nodiscard]] bool slash_menu_open(const Model& m);

}  // namespace agentty::ui
