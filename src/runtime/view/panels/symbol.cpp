// symbol.cpp —— 爱 -symbol 调色板。
//
// 纯适配器: 构建 maya::Panel::Config 发件人 Model 状态;
// 部件拥有每个铬决定。 共享脚手架 : panels_prologue.hpp.

#include "panels_prologue.hpp"

namespace agentty::ui {

Element symbol_panel(const Model& m) {
    auto* o = m.ui.panel.get<pn::Symbol>();
    if (!o) return nothing();

    const auto& matches = o->picker.filtered();
    const auto& entries = o->picker.entries();

    Panel::Config cfg;
    cfg.title      = " Symbol ";
    cfg.accent     = highlight;
    cfg.min_width  = kPanelWide;
    cfg.viewport_h = panel_viewport_h();
    cfg.scroll     = &m.ui.symbol_palette_scroll;
    cfg.selected   = matches.empty() ? -1 : o->picker.index();

    // 锡吉尔是触发字符,所以 头文件 将它
    // 视为过去 带类型的 作曲家的作品
    cfg.header.push_back(
        filter_header(o->picker.query(), "symbols", highlight, "# "));
    cfg.header.push_back(sep);

    if (entries.empty()) {
        cfg.prebuilt.push_back(text(
            o->picker.source_ready()
                ? "  no symbols indexed"
                : "  indexing symbols… (type to filter as it fills)",
            fg_italic(muted)));
    } else if (matches.empty()) {
        cfg.prebuilt.push_back(text("  no matches", fg_italic(muted)));
    } else {
        cfg.items.reserve(matches.size());
        for (int i = 0; i < static_cast<int>(matches.size()); ++i) {
            const auto& sym = entries[matches[static_cast<std::size_t>(i)]];
            auto [fname, dir] = split_name_dir(sym.path);
            Panel::Item row;
            // 组合 符号 名称 + 蝗虫进入主线 存储单元 如此长的
            // 家长 -dir 后面还有空间 渲染 ;
            // 缩写 "name  file:line" 配对是用户正在扫描的内容。
            row.leading        = sym.name + "  " + std::string{fname}
                               + ":" + std::to_string(sym.line_number);
            row.leading_style  = fg_of(fg);
            // 突出显示符号 NAME(是领先部分,
            // 因此其抵消直接映射到row.leading)的匹配字符.
            if (!o->picker.query().empty()) {
                auto fm = fuzzy::score(sym.name, o->picker.query());
                if (fm.matched()) { row.highlight = std::move(fm.positions);
                                    row.highlight_fg = highlight; }
            }
            row.trailing       = parent_segment(dir);
            row.trailing_style = fg_dim(muted);
            cfg.items.push_back(std::move(row));
        }
    }

    if (static_cast<int>(matches.size()) > kViewportH) {
        cfg.footer.push_back(text(
            "  " + std::to_string(o->picker.index() + 1) + "/"
                + std::to_string(matches.size()),
            fg_dim(muted)));
    }

    // 与@ 选择面板 的空档相同: 键有效, 没有任何内容。
    cfg.footer.push_back(key_hints({
        {"\xe2\x86\x91\xe2\x86\x93", "move", 5},   // ↑↓
        {"Enter", matches.empty() ? "" : "insert", 6},
        {"Esc", "close", 4},
    }));

    return Panel{std::move(cfg)}.build();
}


} // namespace agentty::ui
