// web_search.cpp — 设置 ~ Web 搜索面板,通过共享表单.
//
// 纯适配器: 构建 maya::Panel::Config 发件人 Model 状态;
// 部件拥有每个铬决定。 共享脚手架 : panels_prologue.hpp.

#include "panels_prologue.hpp"
#include "agentty/runtime/view/form_panel.hpp"

namespace agentty::ui {

Element web_search_panel(const Model& m) {
    auto* o = m.ui.panel.get<pn::WebSearch>();
    if (!o) return nothing();
    // 卷轴偏移是槽本身(pn::WebSearch::scroll),
    // 因此与面板一起散开,每次打开时从顶端开始.
    return maya::Panel{form_config(o->form, info,
                                  &o->scroll,
                                  panel_viewport_h(),
                                  panel_terminal_cols())}.build();
}

} // namespace agentty::ui
