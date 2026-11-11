// smart_mode.cpp 智能模式配置面板( ) ^S ),通过共享形式.
//
// 纯适配器: 构建 maya::Panel::Config 发件人 Model 状态;
// 部件拥有每个铬决定。 共享脚手架 : panels_prologue.hpp.

#include "panels_prologue.hpp"
#include "agentty/runtime/view/form_panel.hpp"

namespace agentty::ui {

// 智能模式配置 覆盖面板 : 已启用的主机 切换 + 三个角色槽,每个角色槽显示其
// RESOLVED 模型( 被绑定, 或自动) -fill。
// 见 docs/design/smart-mode.md.
Element smart_mode_panel(const Model& m) {
    auto* o = m.ui.panel.get<pn::SmartMode>();
    if (!o) return nothing();
    // 每一格莱弗属于 maya::Panel ; 此主机只将工程声明到它的配置上,
    // 确切的写为 Retrieval 面板 已经。
    return maya::Panel{form_config(o->form, success,
                                  &m.ui.smart_mode_scroll,
                                  panel_viewport_h(),
                                  panel_terminal_cols())}.build();
}


} // namespace agentty::ui
