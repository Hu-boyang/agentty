// slash_menu.cpp - 作曲家的 - 编曲家的 `/` 菜单(参见 docs/design/SLASH_MENU.md).
//
// 纯视图 Model: slash::menu_rows 决定什么显示,
// 这只画它,作为一个 maya::CompletionMenu 直接
// 坐在作曲家的顶部 带类型的 文本永远不要离开作曲家。

#include "agentty/runtime/view/slash_menu.hpp"

#include <algorithm>
#include <string>

#include <maya/core/render_context.hpp>
#include <maya/widget/completion_menu.hpp>

#include "agentty/runtime/slash.hpp"
#include "agentty/runtime/view/palette.hpp"

namespace agentty::ui {

namespace {

[[nodiscard]] maya::SymKind kind_glyph(slash::Kind k) {
    switch (k) {
        case slash::Kind::Action:  return maya::SymKind::Function;
        case slash::Kind::Command: return maya::SymKind::File;
        case slash::Kind::Skill:   return maya::SymKind::Module;
        case slash::Kind::Prompt:  return maya::SymKind::Interface;
    }
    return maya::SymKind::File;
}

[[nodiscard]] const char* kind_tag(slash::Kind k) {
    switch (k) {
        case slash::Kind::Action:  return "action";
        case slash::Kind::Command: return "command";
        case slash::Kind::Skill:   return "skill";
        case slash::Kind::Prompt:  return "mcp";
    }
    return "";
}

// 一行,切合:描述来自用户文件.
[[nodiscard]] std::string one_line(std::string s, std::size_t cap) {
    for (char& c : s)
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    if (s.size() > cap) {
        std::size_t cut = cap;
        while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
        s.resize(cut);
        s += "\xe2\x80\xa6";   // …
    }
    return s;
}

}  // 命名空间结束。

bool slash_menu_open(const Model& m) {
    const auto cat = slash::catalog_of(m);
    return !slash::menu_rows(m, cat).empty();
}

maya::Element slash_menu(const Model& m) {
    const auto cat  = slash::catalog_of(m);
    const auto rows = slash::menu_rows(m, cat);
    if (rows.empty()) return maya::Element{};

    // 适合作曲者之上。菜单下方坐着作曲者(4行铬+其文本)、
    // 状态栏(3)和版面垫(2);菜单本身的边框
    // + "more" 线条 3, 提示 2。
    // 在短线上 终端 提示先行,然后排成三行。
    const int term_rows = maya::have_render_context() ? maya::available_height() : 40;
    const int composer_rows = 4 + std::max(1, static_cast<int>(
        std::ranges::count(m.ui.composer.text, '\n')) + 1);
    const int room = term_rows - composer_rows - 5 - 3;
    const bool show_hint = room >= 8;
    const int max_rows = std::clamp(room - (show_hint ? 2 : 0), 3, 8);

    maya::CompletionMenu menu;
    menu.max_rows = max_rows;
    menu.theme.border    = muted;
    menu.theme.pointer   = highlight;
    menu.theme.match     = highlight;
    menu.theme.sel_label = fg;
    for (const auto& r : rows) {
        const slash::Entry& e = *r.entry;
        maya::CompletionMenu::Item it;
        it.kind  = kind_glyph(e.kind);
        it.label = "/" + e.name;
        // 偏移到名字中, 转过 '/'。
        for (int h : r.hits) it.match.push_back(h + 1);
        std::string detail = one_line(e.description, 48);
        if (!detail.empty()) detail += "  ";
        detail += kind_tag(e.kind);
        it.detail = std::move(detail);
        // 列表下的帮助行,用于所选行:它当使用参数
        // 时的用法,否则其描述完整。每行都有一条,
        // 所以箭头永远不会改变菜单的高度。
        if (show_hint) {
            if (!e.hint.empty())             it.doc = "/" + e.name + " " + one_line(e.hint, 60);
            else if (!e.description.empty()) it.doc = one_line(e.description, 90);
            else                             it.doc = "/" + e.name;
        }
        menu.item(std::move(it));
    }
    const int sel = std::clamp(m.ui.composer.slash.index, 0, static_cast<int>(rows.size()) - 1);
    menu.select(sel);
    return menu.build();
}

}  // namespace agentty::ui
