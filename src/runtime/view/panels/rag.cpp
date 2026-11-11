// rag_settings_view.cpp - 电话 RAG 模式 选择面板 覆盖面板.
//
// 三行:在/只进行第一次/关闭——如何积极主动(前) -turn 检索行为。
// 当前模式已标记; Enter/Space 选择,
// 一个故意的微小 面板 :一个决定,而不是墙壁 切换.

#include "agentty/runtime/view/panels.hpp"

#include "agentty/runtime/view/form_panel.hpp"
#include "agentty/runtime/view/helpers.hpp"
#include "agentty/runtime/view/palette.hpp"
#include "agentty/runtime/panel/rag.hpp"
#include "panels_common.hpp"

#include <maya/widget/panel.hpp>
#include <maya/widget/panel.hpp>
#include <maya/platform/io.hpp>

#include <algorithm>
#include <string>

namespace pn = agentty::ui::panel;

namespace agentty::ui {

using namespace maya;
using namespace maya::dsl;
namespace rs = agentty::rag_settings;

Element rag_panel(const Model& m) {
    const auto* o = m.ui.panel.get<pn::Rag>();
    if (!o) return nothing();

    // 一个 面板, 总是形式. Mode 和 嵌入器是同一问题的两行;
    // 每个glyph属于maya::Panel, 而这个主机除了在它的配置
    // 上显示项目状态之外,什么也做不了.
    //
    // 电话 注册表 做这个 面板 高过短 终端 ,
    // 所以它卷轴——同样 可视区域 算术 选择面板 使用时。
    return maya::Panel{form_config(o->embed.form, info,
                                  &m.ui.rag_settings_scroll,
                                  panel_detail::panel_viewport_h(),
                                  panel_detail::panel_terminal_cols())}.build();
}

} // namespace agentty::ui
