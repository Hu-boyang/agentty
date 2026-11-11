// palette.cpp 命令调色板( ) ^K + + 键 一个 palette_context().
//
// 纯适配器: 构建 maya::Panel::Config 发件人 Model 状态;
// 部件拥有每个铬决定。 共享脚手架 : panels_prologue.hpp.

#include "panels_prologue.hpp"

namespace agentty::ui {

Element palette_panel(const Model& m) {
    auto* o = m.ui.panel.get<pn::Palette>();
    if (!o) return nothing();

    // 现场可见度上下文——字面上下文中的函数与 状态更新函数 使用,
    // 所以行号为 分派器 永远拒绝 渲染 (没有死) -all
    // )和光标,视图显示列表中的索引 状态更新函数 决断。
    const PaletteContext pctx = ui::palette_context(m);
    const auto& scored = match_commands(o->query, pctx);
    std::vector<const CommandDef*> matches;
    matches.reserve(scored.size());
    for (const auto& s : scored) matches.push_back(s.cmd);

    Panel::Config cfg;
    cfg.title      = " Command Palette ";
    cfg.accent     = highlight;
    cfg.min_width  = kPanelStandard;
    cfg.viewport_h = panel_viewport_h();
    cfg.scroll     = &m.ui.command_palette_scroll;
    cfg.selected   = matches.empty() ? -1 : o->index;

    cfg.header.push_back(
        filter_header(o->query, {}, highlight, "\xe2\x8c\x98 "));   // ⌘
    // 过滤器下的规则——相同 `sep` 彼此间 选择面板 绘图,
    // 因此查询框与它的结果在任何地方都完全相同。
    cfg.header.push_back(sep);

    // 每个类别都有一个色调,所以平面列表读作彩色带;
    // 徽章保留其色调在选定的行上( 选择面板
    // 常规行没有徽章( Quit/Update 不需要
    // 一个区域 标签).
    auto category_hue = [](Category c) -> Color {
        switch (c) {
            case Category::Thread:   return info;
            case Category::Changes:  return success;
            case Category::Navigate: return highlight;
            case Category::Config:   return warn;
            case Category::Account:  return muted;
            case Category::General:  return muted;
        }
        return muted;
    };

    if (matches.empty()) {
        cfg.prebuilt.push_back(text(
            o->query.empty() ? "  no commands available"
                             : "  no command matches \"" + o->query + "\"",
            fg_italic(muted)));
    } else {
        cfg.items.reserve(matches.size() + 6);
        // 上场 空 查询我们 渲染 真实 SECTION HEADERS
        // 类别之间——真正的巢穴, VS 代码/ Raycast 样式。
        // 用户键入的那一刻, 头文件 名单消失,名单就平了 -with-ranking
        // (空分类是噪音,标签) -hit 排名重排总而言之。
        // 头文件 非为 -selectable 行; 光标(o->index)索引 头文件-FREE
        // `matches` ,所以我们只设置 Item::selected 真实行和点数
        // cfg.selected 现场 头文件-adjusted 显示位置——该
        // reducer/dispatch 完全留下来 头文件-unaware.
        const bool show_headers = o->query.empty();
        Category   last_cat     = Category::General;
        bool       first_group  = true;
        int        display_row  = 0;   // 职位 INCLUDING 头文件 滚动
        int        sel_display  = -1;

        for (int i = 0; i < static_cast<int>(matches.size()); ++i) {
            const auto& cmd = *matches[static_cast<std::size_t>(i)];

            const bool group_start =
                show_headers && (first_group || cmd.category != last_cat);
            if (group_start) {
                if (auto lab = category_label(cmd.category); !lab.empty()) {
                    // 电话 相同 帮助其他组 选择面板 手卷起来
                    // a 头文件 调色板最后是怎样 自己插上括号的
                    // -and-spine 模型时分组 选择
                    // 面板 有着简单的规则——一种想法有两个设计.
                    cfg.items.push_back(section_header(
                        SectionHeader{std::string{lab}, category_hue(cmd.category)}));
                    ++display_row;
                }
                last_cat    = cmd.category;
                first_group = false;
            }

            Panel::Item row;

            // ── 标签, 以 toggle/mode 状态折叠在 ──
            row.leading = std::string{cmd.label};
            row.leading_style = cmd.danger ? fg_of(danger) : fg_of(fg);
            // 突出模糊-matched字符(Raycast-style),
            // 所以排名可以辨别:使用"re"带类型的,
            // Review/Reject灯光中的"Re",位置来自得分匹配者.
            if (!o->query.empty()) {
                row.highlight    = scored[static_cast<std::size_t>(i)].positions;
                row.highlight_fg = cmd.danger ? danger : highlight;
            }

            // 在聚焦行下方显示 Item::help 说明，与 RAG 面板风格一致。
            // Prose 属于下面的 – 它得到面板的全宽,
            // 并且只有光标所在的 渲染。
            // 拖曳只保留 SHORTCUT : 短距离的参考数据,
            // 仅能存活到狭长距离( 3)。 phone/SSH) 终端 标记
            // SECONDARY 让它在标签之前就发芽
            row.help = cmd.description;
            // 如果有的话,和弦,另外是带类型的XX`/name`,
            // 所以调色板在下一次作曲中教打什么.
            const char* trail = (cmd.shortcut && *cmd.shortcut) ? cmd.shortcut
                                                                 : slash_name_for(cmd.id);
            if (trail && *trail) {
                row.trailing           = trail;
                row.trailing_style     = fg_dim(muted);
                row.trailing_secondary = true;
            }
            if (i == o->index) sel_display = display_row;
            cfg.items.push_back(std::move(row));
            ++display_row;
        }
        // 滚动跟踪 头文件-adjusted 光标位置。
        cfg.selected = sel_display;
    }

    cfg.footer.push_back(text(""));
    // 滚动列表时计算锚点——与@ /# 选择面板 相同的语法.
    if (static_cast<int>(matches.size()) > kViewportH) {
        cfg.footer.push_back(text(
            "  " + std::to_string(o->index + 1) + "/"
                + std::to_string(matches.size()) + " commands",
            fg_dim(muted)));
    }
    cfg.footer.push_back(key_hints({
        {"\xe2\x86\x91\xe2\x86\x93", "move", 5},   // ↑↓
        {"type", "filter", 3},
        {"Enter", "run", 6},
        {"Esc", "close", 3},
    }));

    return Panel{std::move(cfg)}.build();
}


// 电话 一个 线条 -visibility 上游 已宣布为
// view/palette.hpp ; 以这种观点要求 渲染 列表,
// 由调色板 状态更新函数 以定义光标到命令, back_to ()在
// Esc 这三个国家必须编制同样的索引——见 头文件 因为他们没有犯错。
PaletteContext palette_context(const Model& m) {
    PaletteContext ctx;
    ctx.update_available    = !m.s.update_latest.empty();
    ctx.has_pending_changes = !m.d.pending_changes.empty();
    ctx.has_code_block      = [&] {
        for (auto it = m.d.current.messages.rbegin();
             it != m.d.current.messages.rend(); ++it) {
            if (it->role != Role::Assistant || it->text.empty()) continue;
            if (!code_blocks::extract_code_blocks(it->text).empty())
                return true;
        }
        return false;
    }();
    return ctx;
}


} // namespace agentty::ui
