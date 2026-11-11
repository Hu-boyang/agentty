// appearance.cpp - 外观 面板 (主题,密度,运动,铬).
//
// 一种观点中的两个表面:
//
//   1. 决 议 面板 本身——一种平坦的形式,通过共同的
//      form_config 完全如智能模式和插件编辑器。
//      这里没有铬显示 -specific ;这是共享层的
//      点。
//
//   2. 主题浏览器——一个浮动列表,已渲染 OVER 编号 面板 相当
//      而不是它。一个主题 选择面板 覆盖屏幕 它正在
//      沉闷 让你用它的名字来判断一个方案 所以这个方案是
//      故意的 狭窄和底部 -anchored : 笔录,
//      作曲家和 面板 后面的列表在突出显示的
//      方案中全部重新刷漆,列表是它自己预览中的一个标题.

#include "panels_prologue.hpp"
#include "agentty/runtime/view/form_panel.hpp"
#include "agentty/runtime/panel/appearance.hpp"

namespace agentty::ui {

namespace {

// 浏览器, 浮动。 故意缩小( kPannelNarow):
// 它不取的每个列都是在您所看到的方案中可见的 UI
// 的真列。
[[nodiscard]] Element theme_browser(const pn::Appearance& o) {
    const auto& pick = o.pane.picker.picker;

    Panel::Config cfg;
    cfg.title      = " Theme ";
    cfg.accent     = info;
    cfg.min_width  = kPanelNarrow;
    // 通常的可视区域. 列表是屏幕上内容的较小的
    // 一半——它预览的内容是更大的.
    cfg.viewport_h = std::max(6, panel_viewport_h() / 2);

    // WINDOWED IN 该 CALLER ,这就是什么 maya
    // 专家团要求名单如此长的任何人: `items` 正文 是不透明
    // 的( 从未虚拟化) 和 measure_body () 限制其行账,
    // 因此将其全部615项计划交给它 和 一个滚动指针是一个矛盾
    // ——面板不能滚动它拒绝窗口的内容。这就是这个文件的缺陷:光
    // 标从底部走下来,而偏移则从未移动过。
    //
    // 所以我们把它完全交给了可见的片段, 完全没有卷轴状态。
    // 切片来自选择面板,它来源于它夹住的同一光标
    // ——窗口可以证明包含选择,因为两者都是一个值,
    // 而不是两个必须一致的值.
    const auto win     = pick.visible(cfg.viewport_h);
    const auto visible = pick.visible_entries(cfg.viewport_h);
    cfg.scroll   = nullptr;
    cfg.selected = win.cursor;

    cfg.header.push_back(filter_header(pick.query()));
    cfg.header.push_back(sep);

    if (visible.empty()) {
        cfg.prebuilt.push_back(text("  no scheme matches", fg_italic(muted)));
    } else {
        cfg.items.reserve(visible.size());
        for (const std::string* name_p : visible) {
            const std::string& name = *name_p;
            Panel::Item row;
            // 空名称 IS 本地—— 当地 终端 自己颜色,
            // 唯一选择是正确 终端, a 16-colour
            // 终端 和一朵一样的单色,它这么说的,
            // 因为 "native" 自己读的只是另一种方案
            const bool is_native = name.empty();
            row.leading       = is_native ? "native" : name;
            row.leading_style = is_native ? fg_bold(fg) : fg_of(fg);
            if (is_native) {
                row.trailing       = "your terminal's own colours";
                row.trailing_style = fg_dim(muted);
            }
            // 调色板本身,也就是行中实际涉及的东西。
            // 一个名字是色彩方案最不讲究的属性。
            // "Rose Pine Dawn" 绝不然,
            // 那确是轻的, "Spacedust" 并不是说它很
            // 温暖——所以,大多数拼写一个方案特征的六个插槽都画
            // 在它旁边:你经常看到的三种口音,然后是状态花胡。
            //
            // 原生的,是故意留下空白的。无论颜色如何,
            // 终端 也就是说,一个表是索赔 maya
            // 无法做到;后面的文字用文字来表示。
            if (!is_native) {
                if (const maya::Theme* t = ui_prefs::find_scheme(name)) {
                    row.swatch = {t->primary, t->accent, t->info,
                                  t->success, t->warning, t->error};
                }
            }
            if (!pick.query().empty() && !is_native) {
                auto fm = fuzzy::score(name, pick.query());
                if (fm.matched()) { row.highlight = std::move(fm.positions);
                                    row.highlight_fg = highlight; }
            }
            cfg.items.push_back(std::move(row));
        }
    }

    cfg.footer.push_back(sep);
    cfg.footer.push_back(text(
        "  Moving applies it \xe2\x80\x94 you are looking at the preview.",
        fg_dim(muted)));
    cfg.footer.push_back(key_hints({
        {"\xe2\x86\x91\xe2\x86\x93", "preview", 5},   // ↑↓
        {"Enter", "keep", 3},
        {"Esc", "revert", 4},
    }));

    return Panel{std::move(cfg)}.build();
}

}  // 命名空间结束。

Element appearance_panel(const Model& m) {
    auto* o = m.ui.panel.get<pn::Appearance>();
    if (!o) return nothing();
    if (o->pane.picking) return theme_browser(*o);
    return maya::Panel{form_config(o->pane.form, info,
                                   &m.ui.appearance_scroll,
                                   panel_viewport_h(),
                                   panel_terminal_cols())}.build();
}

}  // namespace agentty::ui
