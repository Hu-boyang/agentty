// plugin_edit.cpp ——插件 detail/add 窗体面板(视图)。
//
// 纯适配器,形状与 smart_mode.cpp : 投影 面板
// '窗体在上方 maya::Panel 通过共享 form_config。
// 每个铬决定都属于部件;此文件只说明它使用的滚动槽和重音。

#include "panels_prologue.hpp"
#include "agentty/runtime/view/form_panel.hpp"

namespace agentty::ui {

Element plugin_edit_panel(const Model& m) {
    auto* o = m.ui.panel.get<pn::PluginEdit>();
    if (!o) return nothing();
    return maya::Panel{form_config(o->form, info,
                                  &m.ui.plugin_edit_scroll,
                                  panel_viewport_h(),
                                  panel_terminal_cols())}.build();
}

} // namespace agentty::ui
