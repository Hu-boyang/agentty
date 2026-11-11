// todo.cpp 环境待办事宜 弹出面板 (渲染 UNDER ),"("时间档").
//
// 纯适配器: 构建 maya::Panel::Config 发件人 Model 状态;
// 部件拥有每个铬决定。 共享脚手架 : panels_prologue.hpp.

#include "panels_prologue.hpp"

namespace agentty::ui {

Element todo_panel(const Model& m) {
    if (!pick::is_open(m.ui.todo.open)) return nothing();

    Panel::Config cfg;
    cfg.title      = " Plan ";
    cfg.accent     = info;
    cfg.min_width  = kPanelNarrow;
    cfg.viewport_h = panel_viewport_h();
    cfg.scroll     = &m.ui.todo_scroll;
    // 待办事宜中没有选择光标 弹出面板 ——读 -only。通过 -1 ,
    // 所以自动 -scroll-to-selection 是一个不
    // -op 并且用户的手动滚动位置被充分尊重.
    cfg.selected   = -1;

    if (m.ui.todo.items.empty()) {
        cfg.prebuilt.push_back(text("  No tasks yet.", fg_italic(muted)));
        cfg.prebuilt.push_back(text("  The agent will create tasks as it works.", fg_dim(muted)));
    } else {
        // PlanView 返回一个元素,包含所有任务。
        // 它会生活在可滚动区域,这样,当任务清单超过时,
        // 它会干净地保存长长的任务列表页。 可视区域.
        maya::PlanView plan;
        for (const auto& item : m.ui.todo.items) {
            maya::TaskStatus ts;
            switch (item.status) {
                case TodoStatus::Pending:    ts = maya::TaskStatus::Pending; break;
                case TodoStatus::InProgress: ts = maya::TaskStatus::InProgress; break;
                case TodoStatus::Completed:  ts = maya::TaskStatus::Completed; break;
            }
            plan.add(item.content, ts);
        }
        cfg.prebuilt.push_back(plan.build());

        int total = static_cast<int>(m.ui.todo.items.size());
        int done_count = 0;
        for (const auto& item : m.ui.todo.items)
            if (item.status == TodoStatus::Completed) ++done_count;
        cfg.footer.push_back(text(""));
        cfg.footer.push_back(h(
            text("  " + std::to_string(done_count) + "/" + std::to_string(total),
                 fg_bold(done_count == total ? success : info)),
            text(" completed", fg_dim(muted))
        ).build());
    }

    cfg.footer.push_back(text(""));
    cfg.footer.push_back(h(
        text("Esc", fg_of(fg)), text(" close", fg_dim(muted))
    ).build());

    return Panel{std::move(cfg)}.build();
}


} // namespace agentty::ui
