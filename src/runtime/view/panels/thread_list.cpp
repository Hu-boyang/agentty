// thread_list.cpp - 说 线程 切换器面板( E) ^J : 选, ^D 删除, ^R 重名为.
//
// 纯适配器: 构建 maya::Panel::Config 发件人 Model 状态;
// 部件拥有每个铬决定。 共享脚手架 : panels_prologue.hpp.

#include "panels_prologue.hpp"
#include "thread_time.hpp"
#include "agentty/runtime/panel/thread_filter.hpp"   // visible_threads + 模糊
#include "agentty/runtime/panel/thread_peek.hpp"
#include "agentty/foreign/names.hpp"

#include <maya/widget/peek_card.hpp>

#include <filesystem>
#include <optional>

namespace agentty::ui {

namespace {

// 文件夹的自有名称, 用于标题或行 : "/home/a/agentty" -> "agentty".
[[nodiscard]] std::string workspace_name(std::string_view path) {
    std::filesystem::path p{std::string{path}};
    if (!p.has_filename()) p = p.parent_path();   // 后侧斜线
    std::string n = p.filename().string();
    return n.empty() ? std::string{path} : n;
}

using PeekLine = maya::PeekCard::Line;
using PeekKind = maya::PeekCard::Kind;

[[nodiscard]] PeekLine to_card(const ui::PeekLine& l) {
    using K = ui::PeekLine::Kind;
    return {l.who, l.text, l.kind == K::User ? PeekKind::User
                         : l.kind == K::Tool ? PeekKind::Tool : PeekKind::Reply};
}

// 预览 线程 光标下。从转弯处创建,当转弯时缓存 已
// 加载 几十个字符串复制品 a 帧 ,从来没有 解析.
// 是什么使得一个 线程 可识别的先行: 它的名称,何时,多久,
// 它运行在哪里, 以及你最初要求的。 转弯滚动以填充其余的。
[[nodiscard]] maya::PeekCard::Config peek_for(const Model& m, const pn::ThreadList& p,
                                              const Thread& meta, int rows) {
    maya::PeekCard::Config pc;
    pc.rows  = rows;
    pc.title = std::string{ui::shown_title(meta)};
    const auto now = m.wall_at(maya::anim_now());
    pc.facts = short_age(meta.updated_at, now) + " ago";
    if (meta.message_count > 0) pc.facts += " \xc2\xb7 " + std::to_string(meta.message_count) + " msgs";
    std::vector<std::string> sub;
    if (!meta.workspace.empty()) sub.push_back(workspace_name(meta.workspace));
    if (!meta.forked_from.empty()) sub.push_back("\xe2\x91\x82 fork");   // ⑂
    if (meta.imported) sub.push_back("from " + foreign::display_name(meta.imported->source));
    if (meta.auto_titled && !meta.user_named() && !meta.imported) sub.push_back("auto-named");
    for (std::size_t i = 0; i < sub.size(); ++i)
        pc.subtitle += (i ? " \xc2\xb7 " : "") + sub[i];
    // Enter 在一个会议中,从另一个工具带入;请说。
    const std::string enter = meta.foreign_only ? "Enter continue here" : "Enter open";

    const auto loaded = std::ranges::find(m.ui.peek_cache, meta.id,
                                          &Model::UI::PeekThread::id);
    // 开场白被钉在顶端: 最快的知道这是哪个对话。
    // 从索引看, 加载 已经退出。
    std::string asked = meta.preview;
    if (asked.empty() && loaded != m.ui.peek_cache.end() && !loaded->turns.empty()
        && !loaded->turns.front().empty()
        && loaded->turns.front().front().kind == ui::PeekLine::Kind::User)
        asked = loaded->turns.front().front().text;
    // 已经是标题时不是(无标题或断裂的-nameXX线程).
    if (asked == pc.title) asked.clear();
    pc.asked = std::move(asked);

    if (loaded == m.ui.peek_cache.end()) {
        pc.lines.push_back({"", "loading\xe2\x80\xa6", PeekKind::Note});
        pc.hint = enter;
        return pc;
    }
    const auto& turns = loaded->turns;
    if (turns.empty()) {
        pc.lines.push_back({"", "nothing here yet", PeekKind::Note});
        pc.hint = enter;
        return pc;
    }
    const int n = static_cast<int>(turns.size());
    const int last = n - 1 - std::clamp(p.peek_turn, 0, n - 1);
    // 牌上有记录的线,因为牌本身算上它们。
    const int room = maya::PeekCard::transcript_rows(pc);
    // 从卷轴的-to转回,直到卡满为止,
    // 所以最新的转折在视线上坐到卡底.
    std::vector<PeekLine> lines;
    for (int ti = last; ti >= 0 && static_cast<int>(lines.size()) < room; --ti) {
        std::vector<PeekLine> chunk;
        for (const auto& l : turns[static_cast<std::size_t>(ti)]) chunk.push_back(to_card(l));
        if (ti != last) chunk.push_back({"", "", PeekKind::Gap});
        // 转动比卡高 显示它的开始:问题。
        if (ti == last && static_cast<int>(chunk.size()) > room)
            chunk.resize(static_cast<std::size_t>(std::max(0, room)));
        lines.insert(lines.begin(), chunk.begin(), chunk.end());
    }
    if (static_cast<int>(lines.size()) > room)
        lines.erase(lines.begin(), lines.end() - std::max(0, room));
    while (!lines.empty() && lines.front().kind == PeekKind::Gap) lines.erase(lines.begin());
    pc.lines.insert(pc.lines.end(), lines.begin(), lines.end());
    pc.hint = "\xe2\x86\x90 \xe2\x86\x92 turn " + std::to_string(last + 1) + "/"
            + std::to_string(n) + " \xc2\xb7 " + enter;
    return pc;
}

} // 命名空间结束。

Element thread_list_panel(const Model& m) {
    auto* picker = m.ui.panel.get<pn::ThreadList>();
    if (!picker) return nothing();
    const auto visible = ui::visible_threads(m.d.threads, picker->query, m.env.cwd,
                                             picker->all_workspaces, picker->only_foreign);
    const bool all = ui::effective_all(m.d.threads, m.env.cwd, picker->all_workspaces);
    const int  n_foreign = ui::foreign_thread_count(m.d.threads);
    const int  here = ui::workspace_thread_count(m.d.threads, m.env.cwd);
    const bool renaming = !picker->renaming.empty();

    Panel::Config cfg;
    // 标题名称为生效范围;Tab提示下一个区域的名称.
    const std::string here_name = workspace_name(m.env.cwd);
    const std::string scope_name = picker->only_foreign ? std::string{"Codex & Claude"}
                                 : all ? std::string{"all"} : here_name;
    cfg.title      = " Threads \xc2\xb7 " + scope_name + " ";
    cfg.accent     = info;
    cfg.min_width  = kPanelStandard;
    cfg.viewport_h = panel_viewport_h();
    cfg.scroll     = &m.ui.thread_list_scroll;
    cfg.selected   = -1;   // 下列,过去的日子 头文件

    // 预览: 被过滤器规则固定的框形卡。 光标下的所有 线
    // 程 都相同高度, 所以下面的列表从不移动。
    // 面板有一个固定高度( 必须符合 帧) , 因此卡片从
    // 列表中取出其行, 保留列表中至少 kMinList 的行。
    // 外观 > 线程 预览会关闭它。
    constexpr int kPeekRows = 12, kMinList = 6;
    const int list_rows = cfg.viewport_h;
    const int peek_rows = std::min(kPeekRows, list_rows - kMinList);
    std::optional<maya::PeekCard::Config> peek;
    if (m.d.ui().thread_preview && !renaming && peek_rows >= 7
        && !picker->peek_id.empty()) {
        const auto meta = std::ranges::find(m.d.threads, picker->peek_id,
                                            [](const Thread& t) { return t.id.value; });
        if (meta != m.d.threads.end()) {
            // 上面的规则要花一行;卡片盖住下面的空白行,
            // 所以它和它所花的行一样高.
            peek = peek_for(m, *picker, *meta, peek_rows);
            cfg.viewport_h = list_rows - peek_rows;
            cfg.fixed_viewport = true;   // 列表也保持其高度
        }
    }
    if (renaming) {
        // 草稿取而代之: 一条亮线, 带有REAL 光标,
        // 与设置的加-prompt相同。 下面的突出显示的行将
        // 保持点亮, 以便您看到它重命名的线程。
        std::size_t at = std::string::npos;
        const std::string shown = maya::panel::detail::with_caret(
            picker->rename_input,
            static_cast<std::size_t>(std::max(0, picker->rename_cursor)),
            panel_terminal_cols() - 14, &at);
        const auto caret = Style{}.with_fg(info).with_conceal()
                                  .with_caret_anchor().with_caret_shape(1);
        cfg.header.push_back(h(
            text("  rename  ", fg_of(info)),
            text(shown.substr(0, at), fg_bold(fg)),
            text(shown.substr(at, 3), caret),
            text(shown.substr(at + 3), fg_bold(fg))
        ).build());
        cfg.header.push_back(sep);
    } else if (!m.d.threads.empty()) {
        cfg.header.push_back(filter_header(picker->query, "threads"));
        cfg.header.push_back(sep);   // 过滤器下的规则, 和每选择面板一样
        if (peek) cfg.header.push_back(maya::PeekCard{std::move(*peek)}.build());
    }

    if (m.d.threads.empty()) {
        cfg.prebuilt.push_back(text(
            m.s.threads_loading ? "  Loading conversations…"
                                : "  No threads yet.",
            fg_italic(muted)));
    } else if (visible.empty()) {
        std::string why = picker->query.empty()
            ? std::string{"  no Codex or Claude Code sessions here \xc2\xb7 Tab for more"}
            : "  no thread matches \"" + picker->query + "\"";
        cfg.prebuilt.push_back(text(std::move(why), fg_italic(muted)));
    } else {
        cfg.items.reserve(visible.size() + 5);
        const auto now = m.wall_at(maya::anim_now());
        // 日头文件只出现在平板榜:按对齐进行搜索排位,
        // 乐队会将排名分成两部分.
        const bool bands = picker->query.empty();
        std::optional<ui::DayGroup> band;
        for (int i = 0; i < static_cast<int>(visible.size()); ++i) {
            const auto& t = m.d.threads[static_cast<std::size_t>(visible[static_cast<std::size_t>(i)])];
            if (bands) {
                const auto g = ui::day_group(t.updated_at, now);
                if (g != band) {
                    cfg.items.push_back(section_header(
                        SectionHeader{ui::day_group_label(g), muted}));
                    band = g;
                }
            }
            const bool is_current = (t.id == m.d.current.id);
            const bool confirming = (picker->confirm_remove == t.id.value);
            const bool on_cursor  = (i == picker->index);
            Panel::Item row;
            // "● " 标记 线程 你是,你是个 IN, "⑂ " 叉子,
            // 其他人拿两块 -space 沟渠,标题保留栏 -aligned.
            const char* mark = is_current            ? "\xe2\x97\x8f "    // ●
                             : !t.forked_from.empty() ? "\xe2\x91\x82 "   // ⑂
                             :                          "  ";
            row.leading        = std::string{mark} + std::string{ui::shown_title(t)};
            // 线程 TITLES 和前导规则一样,
            // model/provider 选择面板.
            row.leading_style  = is_current ? fg_bold(info) : fg_of(fg);
            // 显示为何一行匹配。 偏移由前面的标记移位。
            if (!picker->query.empty())
                if (auto fm = fuzzy::score(ui::shown_title(t), picker->query); fm.matched()) {
                    const int shift = static_cast<int>(std::string_view{mark}.size());
                    for (int& p : fm.positions) p += shift;
                    row.highlight    = std::move(fm.positions);
                    row.highlight_fg = info;
                }
            if (confirming) {
                row.badge       = "\xe2\x9a\xa0";           // ⚠
                row.badge_style = fg_of(warn);
                row.leading_style = fg_bold(warn);
                row.trailing       = "press ^D again to confirm";
                row.trailing_style = fg_of(warn);
            } else {
                // 一个模型 -written 名字是这么说的,所以你不会被传出去
                // 带类型的. ^R 或者重命名就成为你的了
                std::string tail = t.auto_titled && !t.user_named() && !t.imported
                                       ? "auto \xc2\xb7 " : "";
                if (t.message_count > 0) tail += std::to_string(t.message_count) + " msgs \xc2\xb7 ";
                row.trailing       = tail + ui::short_age(t.updated_at, now);
                row.trailing_style = fg_dim(muted);
                // 其来处: "codex" / "claude" 数数之前。
                // 尚未开的会话是暗淡的,是开的平原。
                if (t.imported) {
                    row.badge       = t.foreign_only ? "\xe2\x86\x97" : "";   // ↗
                    row.badge_style = fg_dim(info);
                    row.trailing = foreign::display_name(t.imported->source)
                                 + " \xc2\xb7 " + row.trailing;
                    if (t.foreign_only && !is_current)
                        row.leading_style = fg_of(text_secondary);
                }
            }
            // 电话 TITLE 时间戳是参考数据,
            // 并在狭义上先输出 终端.
            row.trailing_secondary = true;
            if (on_cursor) cfg.selected = static_cast<int>(cfg.items.size());
            cfg.items.push_back(std::move(row));
        }
    }

    cfg.footer.push_back(text(""));
    // 位置读取器——相同 "k/N" 编号 ^←→ / Alt
    // 土司显示,所以两个导航表面 说一个坐标系统。
    if (!visible.empty()) {
        cfg.footer.push_back(text(
            "  " + std::to_string(picker->index + 1) + "/"
                + std::to_string(visible.size())
                + (picker->query.empty() ? std::string{}
                       : " of " + std::to_string(m.d.threads.size())),
            fg_dim(muted)));
    }
    // 武装删除为 a MODE : 在注解行中提示它,
    // 完全像账户列表和设置添加 -prompt ,所以每两 -step
    // 应用程序中确认的读法相同。
    if (!picker->confirm_remove.empty())
        cfg.note = "^D confirm delete \xc2\xb7 any other key cancels";
    if (renaming) {
        // 保存空草稿清空后,返回自动草稿;
        // 在草稿空时说,所以键可以找到.
        const bool emptied = picker->rename_input.empty();
        cfg.footer.push_back(key_hints({
            {"type", "edit title", 5},
            {"\xe2\x86\x90\xe2\x86\x92", "move", 4},   // left/right 箭头
            {"Enter", emptied ? "use automatic name" : "save", 5},
            {"Esc", "cancel", 5},
        }));
        return Panel{std::move(cfg)}.build();
    }
    std::vector<Hint> keys{
        {"type", "filter", 5},
        {"\xe2\x86\x91\xe2\x86\x93", "move", 5},        // ↑↓
        {"Home/End", "jump", 2},
    };
    // Tab 步骤 \ 此文件夹 \\ \ \ Codex & Claude ,
    // 跳过空的;提示名称。
    {
        std::vector<std::string> ring{"all"};
        if (here > 0) ring.push_back("this folder");
        if (n_foreign > 0) ring.push_back("Codex & Claude");
        if (ring.size() > 1) {
            const std::string now = picker->only_foreign ? "Codex & Claude"
                                  : all ? "all" : "this folder";
            auto it = std::ranges::find(ring, now);
            const std::size_t i = it == ring.end() ? 0 : static_cast<std::size_t>(it - ring.begin());
            keys.push_back({"Tab", ring[(i + 1) % ring.size()], 3});
        }
    }
    keys.push_back({"Enter", "open", 5});
    keys.push_back({"^N", "new", 3});
    keys.push_back({"^D", picker->confirm_remove.empty() ? "remove" : "confirm", 3});
    keys.push_back({"^R", "rename", 3});
    keys.push_back({"Esc", picker->query.empty() ? "close" : "clear", 4});
    cfg.footer.push_back(key_hints(std::move(keys)));

    return Panel{std::move(cfg)}.build();
}


} // namespace agentty::ui
