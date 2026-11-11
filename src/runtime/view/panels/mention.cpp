// mention.cpp ——这个 @ -file 调色板。
//
// 纯适配器: 构建 maya::Panel::Config 发件人 Model 状态;
// 部件拥有每个铬决定。 共享脚手架 : panels_prologue.hpp.

#include "panels_prologue.hpp"

namespace agentty::ui {

Element mention_panel(const Model& m) {
    auto* o = m.ui.panel.get<pn::Mention>();
    if (!o) return nothing();

    // 与您共享一张纪念过滤器的通过 状态更新函数 读到这里,
    // 也感冒了 -opened 快照 ,所以一个 选择面板
    // 箭头不是箭头 带类型的 继续填充作为指数着陆。
    const auto& matches = o->picker.filtered();
    const auto& files   = o->picker.entries();

    Panel::Config cfg;
    cfg.title      = " Mention File ";
    cfg.accent     = info;
    cfg.min_width  = kPanelStandard;
    cfg.viewport_h = panel_viewport_h();
    cfg.scroll     = &m.ui.mention_palette_scroll;
    cfg.selected   = matches.empty() ? -1 : o->picker.index();

    // "@" 是一个触发字符,所以 头文件 继续原样
    // 带类型的 词里写着什么 ORDER 而不是列表
    // 中包含的 —— 更改文件排序, 这是在开始打字
    // 之前值得知道的事情。
    cfg.header.push_back(
        filter_header(o->picker.query(), "(changed files first)", info, "@ "));
    cfg.header.push_back(sep);

    if (files.empty()) {
        // 区分 "still indexing" 发件人 "genuinely empty"
        // ——走行走行走在背景上 线程 如果它没有降落 选择面板 以
        // 空打开 快照. source_ready () 告诉他们分开。
        cfg.prebuilt.push_back(text(
            o->picker.source_ready()
                ? "  workspace empty (or no readable files)"
                : "  indexing workspace… (type to filter as it fills)",
            fg_italic(muted)));
    } else if (matches.empty()) {
        cfg.prebuilt.push_back(text("  no matches", fg_italic(muted)));
    } else {
        cfg.items.reserve(matches.size());
        for (int i = 0; i < static_cast<int>(matches.size()); ++i) {
            const auto& path = files[matches[static_cast<std::size_t>(i)]];
            auto [name, dir] = split_name_dir(path);
            Panel::Item row;
            // Git-status 徽章——工作区 -set 信号,
            // 颜色 -coded 因此您正在编辑的文件可以一目了然。
            // 添加到固定宽度, 从而引导文本在行间对齐。
            if (auto tag = file_git_tag(path); tag != GitTag::None) {
                auto label = git_tag_label(tag);
                row.badge = "● " + std::string{label};
                row.badge_style =
                    tag == GitTag::Modified          ? fg_of(ui::status_warn)
                  : tag == GitTag::Staged            ? fg_of(ui::status_ok)
                  : tag == GitTag::Untracked         ? fg_of(info)
                  : /* RecentlyCommitted */            fg_dim(muted);
            }
            row.leading        = std::string{name};
            row.leading_style  = fg_of(fg);
            // 点亮文件名中匹配的字符, 模糊的级别可以辨别(
            // re -score 名称在 -view 与查询相对;
            // 工作区 计分员排名但不返回位置).
            if (!o->picker.query().empty()) {
                auto fm = fuzzy::score(name, o->picker.query());
                if (fm.matched()) { row.highlight = std::move(fm.positions);
                                    row.highlight_fg = highlight; }
            }
            row.trailing       = parent_segment(dir);
            row.trailing_style = fg_dim(muted);
            cfg.items.push_back(std::move(row));
        }
    }

    // 位置指示器:尽管滚动条显示的视线相同,
    // 但作为文本N/total主锚仍然有用.
    if (static_cast<int>(matches.size()) > kViewportH) {
        cfg.footer.push_back(text(
            "  " + std::to_string(o->picker.index() + 1) + "/"
                + std::to_string(matches.size()),
            fg_dim(muted)));
    }

    // Enter/Esc/arrows 全部在这里工作( E)
    // on_mention_palette ),但从未被广告宣传,所以唯一学他们的方法就是猜测,
    // 语法与命令调色板相同.
    cfg.footer.push_back(key_hints({
        {"\xe2\x86\x91\xe2\x86\x93", "move", 5},   // ↑↓
        {"Enter", matches.empty() ? "" : "insert", 6},
        {"Esc", "close", 4},
    }));

    return Panel{std::move(cfg)}.build();
}


} // namespace agentty::ui
