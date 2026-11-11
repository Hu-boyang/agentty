// fork_view.cpp - 叉子 - 叉头 选择面板 覆盖面板.
//
// 三行:如何积极主动 RAG 叉子上的行为,叉子总是开始 FRESH (
// 近处) -zero 上下文; 父记录可按要求读取, 因此唯一的选择是
// RAG 行为。 Enter 带有突出显示的行的叉。 自己 TU 匹配
// rag_settings_view.

#include "agentty/runtime/view/panels.hpp"
#include "panels_common.hpp"   // kpanel * 宽度, panel_viewport_h

#include "agentty/runtime/view/helpers.hpp"
#include "agentty/runtime/view/hints.hpp"   // key_hints
#include "agentty/runtime/view/palette.hpp"
#include "agentty/runtime/panel/fork.hpp"

#include <maya/widget/panel.hpp>

#include <string>

namespace pn = agentty::ui::panel;

namespace agentty::ui {

using namespace maya;
using namespace maya::dsl;
namespace fp = agentty::fork_panel;

namespace {

struct RowSpec { const char* label; const char* help; };

// 标签已按键 BY CHOICE ,而不是按位置排列。
// 位置表必须保持与该表相同的顺序。 枚举 ; 完全开关不能漂移,
// 添加一个选择 编译器 要求它贴上标签
[[nodiscard]] constexpr RowSpec row_spec(fp::Choice c) noexcept {
    switch (c) {
        case fp::Choice::RagPerTurn:
            return {"RAG per turn",   "fresh thread · retrieve context before every turn"};
        case fp::Choice::FirstTurnRag:
            return {"First-turn RAG", "fresh thread · retrieve once, up front"};
        case fp::Choice::RagOff:
            return {"RAG off",        "fresh thread · no retrieval"};
    }
    return {"RAG off", "fresh thread · no retrieval"};
}

} // 命名空间结束。

Element fork_panel(const Model& m) {
    const auto* o = m.ui.panel.get<pn::Fork>();
    if (!o) return nothing();

    Panel::Config cfg;
    cfg.title      = " Fork thread ";
    cfg.accent     = info;
    cfg.min_width  = panel_detail::kPanelStandard;
    // 身高是 高点是 CONTENT-sized ,而不是共享的
    // 可视区域 :这是固定的,四 -choice 菜单中没有滚动内容,
    // 所以将其按到可滚动列表的高度,则在最后的选择下留下死行。
    cfg.viewport_h = fp::kChoiceCount + 2;
    cfg.scroll     = nullptr;
    cfg.selected   = static_cast<int>(o->choice);

    // 将光标的点数移动到 – 一个列表, 没有并行的
    // 表格可以保持顺序。
    for (const fp::Choice c : fp::kChoices) {
        const RowSpec spec = row_spec(c);
        Panel::Item row;
        row.leading       = spec.label;
        row.leading_style = fg_of(fg);
        row.trailing      = spec.help;
        row.trailing_style = fg_dim(muted);
        cfg.items.push_back(std::move(row));
    }

    cfg.footer.push_back(text(""));
    cfg.footer.push_back(text(
        "  A fork starts FRESH (near-zero context) · the old transcript is "
        "readable on demand.",
        fg_dim(muted)));
    // 共享的 key_hints 平庸,不是手,不是手 手,
    // 我爱他 -built 运行:这个是拼写自己的间隔,
    // 在宽度压力下不能掉下键,所以在窄的 面板 而不是像其他
    // 的棋盘脚一样 放弃最重要的提示
    cfg.footer.push_back(key_hints({
        {"\xe2\x86\x91\xe2\x86\x93", "choose", 5},   // ↑↓
        {"Enter", "fork", 4},
        {"Esc", "close", 3},
    }));

    return Panel{std::move(cfg)}.build();
}

} // namespace agentty::ui
