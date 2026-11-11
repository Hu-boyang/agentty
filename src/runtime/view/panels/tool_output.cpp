// tool_output.cpp ——工具 -output 查看器( 显示器) ^O : 列表 + 正文 阶段。
//
// 纯适配器: 构建 maya::Panel::Config 发件人 Model 状态;
// 部件拥有每个铬决定。 共享脚手架 : panels_prologue.hpp.

#include "panels_prologue.hpp"

#include <maya/element/text.hpp>          // decode_utf8
#include <maya/text/unicode_width.hpp>   // char_width
#include <maya/widget/tool_body_preview.hpp>

#include "agentty/runtime/panel/tool_output.hpp"

namespace agentty::ui {

namespace {
// 将一行分割成最多一行 `w` 列。仅空格中断开(输出为代码、
// 路径和 标志 ,其中连字符是符号的一部分),
// 硬 -splits 超过一行的标志
std::vector<std::string_view> wrap_rows(std::string_view s, int w) {
    std::vector<std::string_view> out;
    if (w < 1) w = 1;
    std::size_t start = 0;
    while (start < s.size()) {
        int cols = 0;
        std::size_t i = start, last_space = std::string_view::npos;
        while (i < s.size()) {
            std::size_t next = i;
            const char32_t cp = maya::decode_utf8(s, next);
            const int cw = maya::unicode::char_width(cp);
            if (cols + cw > w) break;
            cols += cw;
            if (cp == U' ') last_space = next;
            i = next;
        }
        if (i >= s.size()) { out.push_back(s.substr(start)); break; }
        std::size_t cut = (last_space != std::string_view::npos && last_space > start)
                          ? last_space : i;
        if (cut == start) {   // 零-room 行: 取一个字符
            std::size_t next = start;
            (void)maya::decode_utf8(s, next);
            cut = next;
        }
        out.push_back(s.substr(start, cut - start));
        start = cut;
    }
    if (out.empty()) out.push_back(std::string_view{});
    return out;
}

// `s` 切为( C) `w` 列, 与 "…" 被剪掉的时候
std::string fit_cols(std::string_view s, int w) {
    if (w <= 0) return {};
    if (string_width(s) <= w) return std::string{s};
    std::size_t i = 0;
    int cols = 0;
    while (i < s.size()) {
        std::size_t next = i;
        const char32_t cp = maya::decode_utf8(s, next);
        const int cw = maya::unicode::char_width(cp);
        if (cols + cw > w - 1) break;
        cols += cw;
        i = next;
    }
    std::string out{s.substr(0, i)};
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out + "\xe2\x80\xa6";
}
} // 命名空间结束。

Element tool_output_panel(const Model& m) {
    const auto* o = m.ui.panel.get<pn::ToolOutput>();
    if (!o) return nothing();

    const int sz = static_cast<int>(o->entries.size());
    const int cur = std::clamp(o->index, 0, std::max(0, sz - 1));

    Panel::Config cfg;
    // 故意的 不 共有的小组* 的一层。 覆盖面板 长线供应
    // 所有可用的纵队,并用一个很大的最小度强迫 选择面板 过去
    // phone/SSH 终端 边框和剪接它的右侧; 只保留边框的结构底,
    // 让划条弹性来完成其余部分。 抓取的输出是任意的。
    // -width 内容,所以 "how narrow may this get"
    // 由 终端 ,而不是通过标签栏的形状。
    cfg.min_width  = 1;
    cfg.viewport_h = panel_viewport_h();
    cfg.scroll     = &m.ui.tool_viewer_scroll;

    // "n/N"位置——在两个阶段都显示,这样用户总是
    // 知道他们在输出堆栈中的位置.
    const std::string pos =
        std::to_string(cur + 1) + "/" + std::to_string(sz);

    if (!o->viewing) {
        // ── LIST 阶段 ──
        cfg.title    = " Tool Outputs \xc2\xb7 " + std::to_string(sz) + " ";
        cfg.accent   = highlight;
        cfg.selected = sz == 0 ? -1 : cur;
        // LIST 铬是边框+铺设 (4) 加上空白+闪点页脚 (2).
        // 在真正很小的高度,放下可选的页脚,
        // 并用每行剩下的时间来选择可选的输出.
        const int list_term_rows = panel_terminal_rows();
        const bool compact_list = list_term_rows < 6;
        // 尾端的适合环线 缩小到什么 终端 已经。
        cfg.viewport_h = std::clamp(static_cast<int>(o->entries.size()), 1, kViewportH);

        // 徽章柱宽度:条目间显示名称最长,
        // 因此每个细节行从同一栏开始,
        // 徽章胡同读作垂直颜色条.
        int badge_w = 0;
        for (const auto& e : o->entries)
            badge_w = std::max(badge_w, e.is_live ? string_width("\xe2\x97\x8f LIVE")
                                                  : string_width(e.title));
        ++badge_w;   // 名字最宽处在细节之前还有空白
        // 详细房间: 覆盖面板 4列,然后是边框(2)、垫(4)、
        // 卷条沟(1)、光标栏(2)、徽章加其1 -column 差距,
        // 以及2 -column 状态之前的空档。
        // 细节会让位, 状态永远不会被削减。
        const int row_w = std::max(10, panel_terminal_cols() - 4 - 2 - 4 - 1 - 2 - 1);
        auto fit_detail = [&](const std::string& lead, const std::string& trail) {
            return fit_cols(lead, row_w - badge_w - string_width(trail) - 2);
        };

        cfg.items.reserve(o->entries.size());
        for (int i = 0; i < sz; ++i) {
            const auto& e = o->entries[static_cast<std::size_t>(i)];
            Panel::Item row;
            const Color cat_hue = tool_category_color(e.name);
            if (e.is_live) {
                // 现 况 -running 工具,将工具绑在顶部。
                // 一个亮点 "● LIVE" 工具的徽章读作 "streaming now" ;
                // 细节行带有工具名,所以行仍然是自定义的
                // -describing.
                std::string badge = "\xe2\x97\x8f LIVE";
                const int live_w = string_width(badge);
                if (live_w < badge_w)
                    badge.append(static_cast<std::size_t>(badge_w - live_w), ' ');
                row.badge          = std::move(badge);
                row.badge_style    = fg_bold(cat_hue);
                row.leading        = fit_detail(e.title + (e.detail.empty() ? "" : "  " + e.detail),
                                                e.trailing);
                row.leading_style  = fg_of(fg);
                row.trailing       = e.trailing;
                row.trailing_style = fg_dim(cat_hue);
                cfg.items.push_back(std::move(row));
                continue;
            }
            row.badge = e.title;
            const int title_w = string_width(e.title);
            if (title_w < badge_w)
                row.badge.append(static_cast<std::size_t>(badge_w - title_w), ' ');
            // 类别 hue — 使用的同一种颜色身份,
            // 因此"which tool was that?"
            // 在读到标签前通过 hue 回答。 失败者在徽章
            // 上也会红: 状态排在类别之上。
            row.badge_style    = e.failed ? fg_bold(danger)
                                          : fg_bold(cat_hue);
            row.leading        = e.detail.empty() ? std::string{"\xe2\x80\xa6"}
                                                  : fit_detail(e.detail, e.trailing);
            row.leading_style  = e.failed ? fg_of(danger) : fg_of(fg);
            row.trailing       = e.trailing;
            row.trailing_style = e.failed ? fg_of(danger) : fg_dim(muted);
            cfg.items.push_back(std::move(row));
        }
        if (!compact_list) {
            cfg.footer.push_back(text(""));
            cfg.footer.push_back(key_hints({
                {"\xe2\x86\x91\xe2\x86\x93", "move", 5},   // ↑↓
                {"Enter", "view", 6},
                {"/", "find", 4},
                {"y", "copy", 4},
                {"Esc", "close", 3},
            }));
        }
        // 与正文相同的规则:从来没有比覆盖面板拥有的房间高.
        const int budget = std::max(3, list_term_rows - 2);
        const int cols   = std::max(10, panel_terminal_cols() - 4);
        Element out;
        for (int pass = 0; pass < 8; ++pass) {
            Panel::Config c = cfg;
            out = Panel{std::move(c)}.build();
            const int h = maya::measure_element(out, cols).height.value;
            if (h <= budget) break;
            const int over = h - budget;
            if (cfg.viewport_h > 3) cfg.viewport_h = std::max(3, cfg.viewport_h - over);
            else if (!cfg.tight) cfg.tight = true;
            else if (!cfg.footer.empty()) cfg.footer.clear();
            else if (cfg.viewport_h > 1) cfg.viewport_h = std::max(1, cfg.viewport_h - over);
            else break;
        }
        return out;
    }

    // ── BODY 阶段 ──
    const auto& e = o->entries[static_cast<std::size_t>(cur)];
    const Color tool_hue = e.failed ? danger : tool_category_color(e.name);
    cfg.title    = (e.is_live ? std::string(" \xe2\x97\x8f LIVE \xc2\xb7 ") + e.title
                              : " " + e.title)
                 + " \xc2\xb7 " + pos + " ";
    cfg.accent   = tool_hue;
    cfg.selected = -1;   // 读作-only – 无光标行; 手动滚动规则
    // 普通类 BODY 铬为9行:边框+铺设(4),
    // 头文件 +分离器(2),空白+距离+ints页脚(3)。
    // 终端 列没有空间选择的染色体, 因此边框标题带有身份,
    // 其余的列都输出。 这样可以保留 弹出面板 里面所有可行
    // 的 终端 高度( 5+ 行) 而不是继承共享 选择面板
    // 4岁 -row 可视区域 楼层。
    const int body_term_rows = panel_terminal_rows();
    const bool compact_body = body_term_rows < 6;   // 合适的循环处理其余的
    // 从整个终端开始;下面的适合环会把铬脱落.
    cfg.viewport_h = std::max(1, body_term_rows);

    // 头文件:工具名,它运行的,以及它是如何运行的。
    // 状态从不剪辑;细节先让位。
    if (!compact_body) {
        cfg.header.push_back(
            hstack().width(Dimension::percent(100))(
                text(" " + e.title, fg_bold(tool_hue)) | clip | shrink(1.0f),
                text(e.detail.empty() ? "" : "  " + e.detail, fg_of(fg))
                    | clip | grow(1.0f) | shrink(50.0f),
                text("  " + e.trailing + " ", e.failed ? fg_of(danger) : fg_dim(muted))
                    | clip | shrink(1.0f)
            ).build());
        cfg.header.push_back(sep);
    }

    // 正文 行:每行一次(入口,宽度,查询),然后窗口化.
    //
    // A 256 KiB 输出为千行,所以每行 帧 只
    // 复制可见的切片。每个缓存行完全是一个屏幕行:长行被
    // 包裹在这里,在 正文 宽度,所以滚动数学数数是所绘制的,
    // 没有越过正确边缘的东西丢失。
    struct BodyCache {
        const void*   entries_key = nullptr;
        int           index       = -1;
        const void*   bytes_key   = nullptr;
        std::size_t   bytes_len   = 0;
        std::uint64_t call_key    = 0;
        int           width       = -1;
        std::string   query;
        std::vector<Element> rows;
        std::vector<int>     hit_rows;   // 每种匹配的行, 按顺序
        int           out_lines   = 0;   // 产出项目(前-wrap)
        bool          plain       = false;
    };
    static BodyCache cache;   // UI 线程 仅与其他 选择面板 的静态一样

    // 文本宽度 面板 : 数据 帧 及其垫装需要6列,
    // 同时搜索1列 -column 边距标记当前匹配值。
    const int pane_w   = std::max(20, panel_terminal_cols() - 6 - (o->query.empty() ? 0 : 1));
    const void* entries_key = static_cast<const void*>(o->entries.data());
    const void* bytes_key   = static_cast<const void*>(e.output.data());
    const auto call_key     = e.call.compute_render_key();
    if (cache.entries_key != entries_key || cache.index != cur
        || cache.bytes_key != bytes_key || cache.bytes_len != e.output.size()
        || cache.call_key != call_key || cache.width != pane_w
        || cache.query != o->query) {
        cache.entries_key = entries_key;
        cache.index       = cur;
        cache.bytes_key   = bytes_key;
        cache.bytes_len   = e.output.size();
        cache.call_key    = call_key;
        cache.width       = pane_w;
        cache.query       = o->query;
        cache.rows.clear();
        cache.hit_rows.clear();
        cache.out_lines = 0;
        cache.plain     = false;

        const Style hit_st = Style{}.with_fg(Color::slot(ThemeSlot::Background))
                                    .with_bg(Color::slot(ThemeSlot::Warning)).with_bold();
        // 文本的一列屏幕:基色,输出符在顶部(当被问到时),
        // 然后在一切的顶部找到点击.
        auto line_row = [&](std::string_view s, Style base, bool tokens) {
            std::vector<maya::StyledRun> runs;
            std::vector<Style> per;
            const auto spans = tool_output::hit_spans(s, o->query);
            if (tokens || !spans.empty()) {
                per.assign(s.size(), base);
                if (tokens)
                    for (const auto& r : maya::ToolBodyPreview::output_tokens(s)) {
                        const auto end = std::min(s.size(), r.byte_offset + r.byte_length);
                        for (auto k = r.byte_offset; k < end; ++k) {
                            Style st = per[k];
                            if (r.style.fg) st.fg = r.style.fg;
                            st.bold = st.bold || r.style.bold;
                            st.underline = r.style.underline;
                            per[k] = st;
                        }
                    }
                for (auto [off, len] : spans)
                    for (auto k = off; k < std::min(s.size(), off + len); ++k) per[k] = hit_st;
                for (std::size_t k = 0; k < per.size();) {
                    std::size_t j = k + 1;
                    while (j < per.size() && per[j] == per[k]) ++j;
                    runs.push_back({k, j - k, per[k]});
                    k = j;
                }
            }
            return Element{maya::TextElement{
                .content = std::string{s},
                .style   = base,
                .wrap    = maya::TextWrap::TruncateEnd,
                .runs    = std::move(runs),
            }};
        };
        auto note_hits = [&](std::string_view s) {
            if (!o->query.empty() && !tool_output::hit_spans(s, o->query).empty())
                cache.hit_rows.push_back(static_cast<int>(cache.rows.size()));
        };

        // 命令 -running 工具首先显示其全部命令: 头
        // 文件 复制的就是一条剪线
        if (e.call.args.is_object()) {
            std::string command;
            if (auto it = e.call.args.find("command");
                it != e.call.args.end() && it->is_string())
                command = it->get<std::string>();
            if (!command.empty()) {
                const Color cmd_hue = e.failed ? danger : tool_hue;
                bool first = true;
                std::string_view rest{command};
                while (!rest.empty() && (rest.back() == '\n' || rest.back() == ' '))
                    rest.remove_suffix(1);
                for (std::size_t p = 0; p <= rest.size();) {
                    auto nl = rest.find('\n', p);
                    if (nl == std::string_view::npos) nl = rest.size();
                    for (auto v : wrap_rows(rest.substr(p, nl - p), std::max(10, pane_w - 2))) {
                        note_hits(v);
                        cache.rows.push_back(hstack().width(Dimension::percent(100))(
                            text(first ? "$ " : "  ", fg_bold(cmd_hue)),
                            line_row(v, fg_of(fg), false) | grow(1.0f) | shrink(1.0f)
                        ).build() | height(1) | overflow(Overflow::Hidden));
                        first = false;
                    }
                    if (nl == rest.size()) break;
                    p = nl + 1;
                }
                cache.rows.push_back(sep);
            }
        }

        using Kind = maya::ToolBodyPreview::Kind;
        auto bp = tool_body_preview_config(e.call);
        // 结构化预览(diff, file, todo)已经读得很好;
        // 只要搜索,一切都是纯文本,可以标记匹配.
        const bool structured = o->query.empty() && !e.failed
            && (bp.kind == Kind::EditDiff || bp.kind == Kind::GitDiff
                || bp.kind == Kind::FileRead || bp.kind == Kind::FileWrite
                || bp.kind == Kind::TodoList);
        const std::string shown = tool_output::display_text(e);

        if (structured) {
            bp.show_all   = true;       // 输出,无"⋯ N more"
            bp.tail_only  = e.is_live;  // 现场:最底层的最新
            bp.show_streaming_placeholder = false;
            Element body = maya::ToolBodyPreview{std::move(bp)}.build();
            // 一列的vstack:拆分,这样窗口就可以处理行.
            // 每个孩子被扣在一个屏幕上,所以max_y保持精确.
            if (auto* box = maya::as_box(body);
                box && box->layout.direction == maya::FlexDirection::Column
                && !box->children.empty()) {
                cache.rows.reserve(cache.rows.size() + box->children.size());
                for (auto& child : box->children)
                    cache.rows.push_back(std::move(child) | height(1)
                                         | overflow(Overflow::Hidden));
            } else {
                cache.rows.push_back(std::move(body) | height(1)
                                     | overflow(Overflow::Hidden));
            }
        } else if (!shown.empty()) {
            cache.plain = true;
            const Color pipe_hue = e.failed ? danger : tool_hue;
            std::vector<std::string_view> lines;
            for (std::size_t p = 0; p <= shown.size();) {
                auto nl = shown.find('\n', p);
                if (nl == std::string::npos) nl = shown.size();
                lines.emplace_back(shown.data() + p, nl - p);
                if (nl == shown.size()) break;
                p = nl + 1;
            }
            cache.out_lines = static_cast<int>(lines.size());
            const int gutter_w = static_cast<int>(std::to_string(lines.size()).size());
            // " N " 沟渠,那么 "│ ".
            const int text_w = std::max(10, pane_w - gutter_w - 4);
            const Style base = e.failed ? fg_of(danger) : fg_of(fg);
            const std::string blank_gutter(static_cast<std::size_t>(gutter_w) + 2, ' ');
            cache.rows.reserve(cache.rows.size() + lines.size());
            for (std::size_t i = 0; i < lines.size(); ++i) {
                std::string num = std::to_string(i + 1);
                num.insert(0, static_cast<std::size_t>(gutter_w) - num.size(), ' ');
                const bool cut = lines[i].starts_with("\xe2\x8b\xaf ");   // ⁇  注释
                bool first = true;
                for (auto v : wrap_rows(lines[i], text_w)) {
                    note_hits(v);
                    cache.rows.push_back(hstack().width(Dimension::percent(100))(
                        text(first ? " " + num + " " : blank_gutter, fg_dim(muted)),
                        text(first ? "\xe2\x94\x82 " : "\xe2\x94\x8a ", fg_dim(pipe_hue)),   // │ ┊
                        (cut ? line_row(v, fg_italic(muted), false)
                             : line_row(v, base, !e.failed))
                            | grow(1.0f) | shrink(1.0f)
                    ).build() | height(1) | overflow(Overflow::Hidden));
                    first = false;
                }
            }
        } else if (!e.is_live) {
            cache.rows.push_back(text("  (no output)", fg_italic(muted))
                                 | height(1) | overflow(Overflow::Hidden));
        }
    }

    // 将缓存行窗口进入 面板。该词 面板 永远不能比 终端 (
    // a) 与《公约》第2条有关的 -tall 帧 滚动到 回滚缓冲区 ),
    // 以及 头文件 ,状态和提示行可以以一定的宽度进行包装,
    // 所以不要猜测我们构建、测量和给予的铬 正文 少一点,
    // 直到它适合。
    const int total_rows = static_cast<int>(cache.rows.size());
    auto& sc = m.ui.tool_viewer_scroll;
    const int nhits = static_cast<int>(cache.hit_rows.size());
    const int cur_hit = nhits > 0 ? ((o->hit % nhits) + nhits) % nhits : -1;
    const int cur_row = cur_hit >= 0 ? cache.hit_rows[static_cast<std::size_t>(cur_hit)] : -1;
    // 查找:将当前匹配的画面带入一次每次跳跃,
    // 下行方向的三分之一,这样上面就出现了背景.
    static int seen_seq = -1;
    const bool jump_to_hit = cur_hit >= 0 && seen_seq != o->hit_seq;
    seen_seq = o->hit_seq;
    const int y_before = sc.y;

    const Panel::Config base = std::move(cfg);
    auto assemble = [&](int vh, bool hints, bool header) {
        Panel::Config c = base;
        if (!header) c.header.clear();
        c.scroll = nullptr;
        sc.max_y = std::max(0, total_rows - vh);
        sc.viewport_bounds.h = vh;   // PgUp /PgDn 的页码
        sc.y = jump_to_hit ? cur_row - vh / 3 : y_before;
        // 活条目会跟踪其最新输出, 除非用户滚动。
        if (e.is_live && m.ui.tool_viewer_tail && o->query.empty()) sc.y = sc.max_y;
        sc.y = std::clamp(sc.y, 0, sc.max_y);
        const int first = sc.y;
        const int last  = std::min(total_rows, first + vh);
        for (int i = first; i < last; ++i) {
            // 在搜索时,一个 -column 边距标记了当前匹配的值.
            if (!o->query.empty())
                c.prebuilt.push_back(hstack().width(Dimension::percent(100))(
                    text(i == cur_row ? "\xe2\x96\xb8" : " ", fg_bold(warn)),   // ▸
                    cache.rows[static_cast<std::size_t>(i)] | grow(1.0f) | shrink(1.0f)
                ).build() | height(1) | overflow(Overflow::Hidden));
            else
                c.prebuilt.push_back(cache.rows[static_cast<std::size_t>(i)]);
        }
        if (total_rows == 0)
            c.prebuilt.push_back(text("  waiting for output\xe2\x80\xa6", fg_italic(muted))
                                 | height(1) | overflow(Overflow::Hidden));
        if (compact_body) return c;

        // 状态线:发现提示词或结果,然后我们在哪里.
        std::vector<Element> status;
        if (o->typing || !o->query.empty()) {
            status.push_back(text("  /", fg_bold(warn)) | clip);
            status.push_back(text(o->query, fg_of(fg)) | clip | shrink(5.0f));
            if (o->typing) status.push_back(text("\xe2\x96\x8f", fg_of(warn)) | clip);   // 光标 国 家
            std::string res = o->query.empty() ? ""
                : nhits == 0 ? "  no matches"
                : "  " + std::to_string(cur_hit + 1) + " of " + std::to_string(nhits);
            status.push_back(text(res, nhits == 0 && !o->query.empty()
                                           ? fg_of(danger) : fg_dim(muted)) | clip);
        }
        std::string where;
        if (total_rows > vh) {
            const int pct = sc.max_y == 0 ? 100 : (100 * sc.y) / sc.max_y;
            where = sc.y == 0 ? "top" : sc.y >= sc.max_y ? "end" : std::to_string(pct) + "%";
        }
        if (cache.plain && cache.out_lines > 0) {
            if (!where.empty()) where += " \xc2\xb7 ";
            where += std::to_string(cache.out_lines)
                   + (cache.out_lines == 1 ? " line" : " lines");
        }
        if (e.is_live && m.ui.tool_viewer_tail && o->query.empty())
            where += where.empty() ? "following" : " \xc2\xb7 following";
        status.push_back(spacer());
        status.push_back(text(where + "  ", fg_dim(e.is_live ? tool_hue : muted)) | clip);
        c.footer.push_back(hstack().width(Dimension::percent(100))(std::move(status)).build()
                           | height(1) | overflow(Overflow::Hidden));
        if (!hints) return c;

        std::vector<Hint> viewer_hints;
        if (o->typing) {
            viewer_hints = {
                {"Enter", "keep", 5},
                {"\xe2\x86\x91\xe2\x86\x93", "prev/next match", 4},   // ↑↓
                {"Esc", "clear", 3},
            };
        } else {
            viewer_hints = {
                {"\xe2\x86\x91\xe2\x86\x93", "scroll", 6},        // ↑↓
                {"PgUp/PgDn", "page", 3},
                {"/", "find", 5},
            };
            if (!o->query.empty()) viewer_hints.push_back({"n/N", "next/prev", 5});
            if (e.is_live) viewer_hints.push_back({"End", "follow", 4});
            viewer_hints.push_back({"\xe2\x86\x90\xe2\x86\x92", "other output", 4});   // ←→
            if (!e.is_live) viewer_hints.push_back({"y", "copy", 4});
            viewer_hints.push_back({"Esc", o->query.empty() ? "back" : "clear", 3});
        }
        c.footer.push_back(key_hints(std::move(viewer_hints)));
        return c;
    };

    // 电话 覆盖面板 将两行浮在底部,比下方窄4列 终端 (
    // compose_panel / maya::Overlay ),所以这就是房间。
    const int budget = std::max(3, body_term_rows - 2);
    const int cols   = std::max(10, panel_terminal_cols() - 4);
    // 永远不再 可视区域 而不是输出,所以每个适切的传球都会
    // 删除真正存在的行.
    int  vh = std::clamp(base.viewport_h, 1, std::max(1, total_rows));
    bool hints = true, header = true, tight = false;
    Element out;
    for (int pass = 0; pass < 8; ++pass) {
        auto c = assemble(vh, hints, header);
        c.tight = tight;
        out = Panel{std::move(c)}.build();
        const int h = maya::measure_element(out, cols).height.value;
        if (h <= budget) break;
        const int over = h - budget;
        // 在有铬还剩待降时,至少保留3行输出.
        if (vh > 3)          vh = std::max(3, vh - over);
        else if (!tight)     tight = true;                  // 然后是帧的空白行
        else if (hints)      hints = false;                 // 然后键提示
        else if (header)     header = false;                // 然后是, 头文件
        else if (vh > 1)     vh = std::max(1, vh - over);   // 最后,输出本身
        else break;
    }
    return out;
}


} // namespace agentty::ui
