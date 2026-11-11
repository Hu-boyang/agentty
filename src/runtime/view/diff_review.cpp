#include "agentty/runtime/view/diff_review.hpp"
#include "agentty/util/digits.hpp"

#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include <maya/widget/markdown/highlight.hpp>
#include <maya/widget/tab_strip.hpp>
#include <maya/widget/tool_body_preview.hpp>   // diff带, terminal_color_level
#include <maya/style/theme.hpp>

#include "agentty/domain/ui_live.hpp"
#include "agentty/runtime/view/palette.hpp"
#include "agentty/runtime/view/hints.hpp"

namespace pn = agentty::ui::panel;

namespace agentty::ui {

using namespace maya;
using namespace maya::dsl;
namespace syntax = maya::syntax;

namespace {

// Red/green 排行带, 如编辑卡。 关闭在 256 个
// 颜色以下, 乐队的颜色不能显示, 普通符号读得更好。
[[nodiscard]] bool diff_bands_on() noexcept {
    return maya::terminal_color_level() >= 2;
}

// ── 文件路径扩展名的语言 ────────────────────────────────
[[nodiscard]] syntax::Lang lang_of(std::string_view path) {
    auto dot = path.rfind('.');
    if (dot == std::string_view::npos) return syntax::Lang::Generic;
    return syntax::lang_from_tag(path.substr(dot + 1));
}

// 用户所想的路径:相对于工作区, 换句话说, ~/...
[[nodiscard]] std::string shown_path(const std::string& p) {
    const auto roots = agentty::ui_prefs::path_roots();
    auto under = [&](const std::string& base) {
        return !base.empty() && p.size() > base.size() &&
               p.compare(0, base.size(), base) == 0 && p[base.size()] == '/';
    };
    if (under(roots.cwd))  return p.substr(roots.cwd.size() + 1);
    if (under(roots.home)) return "~/" + p.substr(roots.home.size() + 1);
    return p;
}

// ── 状态格言 ────────────────────────────────────────────────────────────────────────
// 编辑已经放在磁盘上,所以选择是保留或撤消,不接受或拒绝;
// 这些词是这样说的.
[[nodiscard]] const char* status_dot(Hunk::Status s) {
    switch (s) {
        case Hunk::Status::Accepted: return "\xe2\x9c\x93";  // ✓
        case Hunk::Status::Rejected: return "\xe2\x9c\x97";  // ✗
        case Hunk::Status::Pending:  return "\xe2\x97\x8b";  // ○
    }
    return " ";
}
[[nodiscard]] const char* status_word(Hunk::Status s) {
    switch (s) {
        case Hunk::Status::Accepted: return "kept";
        case Hunk::Status::Rejected: return "undone";
        case Hunk::Status::Pending:  return "to review";
    }
    return "";
}
[[nodiscard]] Color status_color(Hunk::Status s) {
    switch (s) {
        case Hunk::Status::Accepted: return success;
        case Hunk::Status::Rejected: return danger;
        case Hunk::Status::Pending:  return warn;
    }
    return muted;
}

// 解析的diff行:它的标志,代码文本(签名剥离),
// 以及它的旧和新-side行号(0=该侧无).
struct DiffLine { char sign; std::string code; int oldno; int newno; };

// 文件文本中的行,编辑者计算的方式.
[[nodiscard]] int line_count(const std::string& s) {
    int n = static_cast<int>(std::count(s.begin(), s.end(), '\n'));
    return (!s.empty() && s.back() != '\n') ? n + 1 : n;
}

// `old_lines`/`new_lines` 将空的"line" diff::compute
// 发射出经过一条后继的新线;它不在文件中.
[[nodiscard]] std::vector<DiffLine> parse_hunk(std::string_view patch,
                                               int old_lines = 1 << 30,
                                               int new_lines = 1 << 30,
                                               int old_start = 1, int new_start = 1) {
    std::vector<DiffLine> out;
    // diff::compute '补丁没有带 QQ 头文件 ,所以从 差异块
    int oldno = old_start, newno = new_start;
    std::size_t i = 0;
    while (i < patch.size()) {
        std::size_t nl = patch.find('\n', i);
        std::string_view line = patch.substr(i, (nl == std::string_view::npos ? patch.size() : nl) - i);
        i = (nl == std::string_view::npos) ? patch.size() : nl + 1;
        if (line.empty()) continue;
        if (line.starts_with("@@")) {
            // 种子两个计数器都来自 "-start" 财务报告和已审计财务报表 "+start".
            auto seed = [&](char c) {
                int v = 0;
                if (auto p = line.find(c, 2); p != std::string_view::npos)
                    for (std::size_t k = p + 1; k < line.size() && line[k] >= '0' && line[k] <= '9'; ++k)
                        v = ::agentty::util::push_digit(v, line[k]);
                return v;
            };
            oldno = seed('-');
            newno = seed('+');
            continue;   // 头文件 本身不是作为代码线提供的
        }
        char sign = line[0];
        std::string code{line.substr(1)};
        int o = 0, n = 0;
        if (sign == '-' || sign == ' ') o = oldno++;
        if (sign == '+' || sign == ' ') n = newno++;
        const bool past_end = code.empty() && (o == 0 || o > old_lines) && (n == 0 || n > new_lines);
        if (past_end) continue;
        out.push_back({sign, std::move(code), o, n});
    }
    return out;
}

// 语法-highlight 1条代码线成风格的运行,为它的diff符号而有锡.
// Added/removed 线条保持语法颜色,但带有偏暗 add/remove
// 通过沟槽+标志进行洗涤;上下文线被暗化,因此眼部跟踪变化.
[[nodiscard]] Element diff_code_line(const DiffLine& dl, syntax::Lang lang,
                                     int gutter_w) {
    const bool add = dl.sign == '+', del = dl.sign == '-';
    Color sign_c = add ? success : del ? danger : muted;

    // 构建 整个 以一个字符串形式行 + per -byte-range
    // StyledRuns ,所以它会 渲染 作为 唯一 剪切其文本元素
    // 末尾 干净的在 面板 宽度——每秒的重磅 -span 文本 存储单元
    // 将相反地在线条溢出时挤压每个跨度(缠绕)。 maya's
    // DiffView 保持干净,应用我们的语法突出。
    std::string content;
    std::vector<StyledRun> runs;

    // 古特:新旧行号,右-aligned,然后是标志.
    auto num = [gutter_w](int n) {
        std::string s = n > 0 ? std::to_string(n) : "";
        s.insert(s.begin(), static_cast<std::size_t>(std::max(0, gutter_w - (int)s.size())), ' ');
        return s;
    };
    std::string gut = num(dl.oldno) + " " + num(dl.newno) + " ";
    const bool on_band = diff_bands_on() && (add || del);
    const maya::Color rail = add ? maya::diff_palette::add_rail : maya::diff_palette::rem_rail;
    const maya::Color rail_fg = add ? maya::diff_palette::add_fg_hi : maya::diff_palette::rem_fg_hi;
    runs.push_back({content.size(), gut.size(),
                    on_band ? Style{}.with_fg(rail_fg).with_bg(rail).with_bold()
                            : Style{}.with_fg(muted).with_dim()});
    content += gut;
    std::string sign_s = add ? "+ " : del ? "- " : "  ";
    runs.push_back({content.size(), sign_s.size(),
                    on_band ? Style{}.with_fg(rail_fg).with_bg(rail).with_bold()
                            : Style{}.with_fg(sign_c)});
    content += sign_s;

    // 语法 -highlighted 代码。在一个带子上,
    // 每个符号颜色被调高,直到它读到为止;上下文线条 渲染 眼线变了,所以变了
    const std::size_t code_off = content.size();
    content += dl.code;
    auto spans = syntax::highlight(dl.code, lang);
    const auto& theme = syntax::themes::terminal;
    const bool banded = on_band;
    const auto& lt = maya::theme::live();
    const maya::LitColor band = lt.resolve(add ? maya::diff_palette::add_bg
                                               : maya::diff_palette::rem_bg);
    const maya::LitColor plain = lt.resolve(add ? maya::diff_palette::add_fg
                                                : maya::diff_palette::rem_fg);
    auto push = [&](std::size_t from, std::size_t to, syntax::Capture cap) {
        if (to <= from) return;
        Style st = theme.style_for(cap);
        if (banded) {
            maya::LitColor fg = st.fg ? maya::theme::measure(lt.resolve(*st.fg)) : plain;
            if (!fg.has_channels()) fg = plain;
            st = st.with_fg(maya::readable_on(fg, band, 6.0)).with_bg(band);
        } else if (dl.sign == ' ') {
            st = st.with_dim();
        }
        runs.push_back({code_off + from, to - from, st});
    };
    std::size_t pos = 0;
    for (const auto& sp : spans) {
        if (sp.start > pos) push(pos, sp.start, syntax::Capture::None);
        push(sp.start, std::min<std::size_t>(sp.start + sp.len, dl.code.size()),
             sp.cap);
        pos = sp.start + sp.len;
    }
    if (pos < dl.code.size()) push(pos, dl.code.size(), syntax::Capture::None);

    return Element{TextElement{.content = std::move(content),
                               .wrap = TextWrap::TruncateEnd,
                               .runs = std::move(runs)}};
}

} // 命名空间结束。

Element diff_review(const Model& m) {
    auto* cursor = m.ui.panel.get<pn::DiffReview>();
    if (!cursor || m.d.pending_changes.empty()) return nothing();
    const auto& files = m.d.pending_changes;
    const int fidx = std::min<int>(cursor->file_index, static_cast<int>(files.size()) - 1);
    const auto& fc = files[static_cast<std::size_t>(fidx)];

    // ── 每个文件的总计 ──
    int total = 0, kept = 0, undone = 0, added = 0, removed = 0;
    for (const auto& f : files) {
        added += f.added;
        removed += f.removed;
        for (const auto& hk : f.hunks) {
            ++total;
            if (hk.status == Hunk::Status::Accepted) ++kept;
            if (hk.status == Hunk::Status::Rejected) ++undone;
        }
    }
    const int decided = kept + undone;
    // 编号 状态更新函数 关闭 面板 上个决定,所以这个持续一个 帧
    const bool all_done = decided == total && total > 0;

    auto rule = [](Color c) {
        return component([c](int w, int) -> Element {
            std::string s;
            for (int i = 0; i < w; ++i) s += "\xe2\x94\x80";   // ─
            return text(std::move(s), fg_dim(c));
        });
    };
    auto plural = [](int n, const char* one, const char* many) {
        return std::format("{} {}", n, n == 1 ? one : many);
    };

    std::vector<Element> rows;

    // ── 是什么,在一行, 和它的大小 ──
    {
        const std::string size = plural(static_cast<int>(files.size()), "file", "files")
                               + std::format("  +{} \xe2\x88\x92{}", added, removed);
        rows.push_back(component([size, added, removed, nfiles = files.size(), plural](int w, int) -> Element {
            const std::string longer = "Already saved. Keep or undo each change.";
            const std::string shorter = "Keep or undo each change.";
            const int sw = string_width(size) + 2;
            const std::string& msg = string_width(longer) + sw <= w ? longer : shorter;
            std::vector<Element> p;
            if (string_width(msg) + sw <= w) p.push_back(text(msg, fg_of(fg)));
            p.push_back(spacer());
            p.push_back(text(plural(static_cast<int>(nfiles), "file", "files") + "  ", fg_dim(muted)));
            p.push_back(text(std::format("+{}", added), fg_of(success)));
            p.push_back(text(" "));
            p.push_back(text(std::format("\xe2\x88\x92{}", removed), fg_of(danger)));
            return h(std::move(p)).build();
        }));
    }
    rows.push_back(text(""));

    // ── 每个文件一个标签 ──
    // 标签标记是文件的状态: ^ 尚有需要审查的东西, \\\ 全部保存,
    // \\\\ 有些未撤销。 TabStrip 幻灯片可以让当前
    // 文件在文件不匹配时查看。
    {
        TabStrip tabs;
        // 编辑-style 文件标签:打开的文件是填充的标签
        tabs.marker(TabMark::Editor);
        tabs.indent = 0;
        tabs.theme.active    = fg;
        tabs.theme.idle      = muted;
        tabs.theme.accent    = accent;
        tabs.theme.active_bg = accent;
        for (const auto& f : files) {
            bool anyrej = false, anypend = false;
            for (const auto& hk : f.hunks) {
                anyrej  |= hk.status == Hunk::Status::Rejected;
                anypend |= hk.status == Hunk::Status::Pending;
            }
            const Hunk::Status roll = anypend ? Hunk::Status::Pending
                                    : anyrej  ? Hunk::Status::Rejected
                                              : Hunk::Status::Accepted;
            std::string name = f.path;
            if (auto sl = name.rfind('/'); sl != std::string::npos) name = name.substr(sl + 1);
            tabs.tab(std::move(name))
                .dot(status_color(roll), std::string(status_dot(roll)))
                .detail(std::format("+{} \xe2\x88\x92{}", f.added, f.removed), muted);
        }
        tabs.active(fidx);
        rows.push_back(tabs.build());
    }
    rows.push_back(text(""));

    // ── 当前文件: 完整路径, 然后更改 ──
    {
        std::string dir, base = shown_path(fc.path);
        if (auto sl = base.rfind('/'); sl != std::string::npos) {
            dir = base.substr(0, sl + 1);
            base = base.substr(sl + 1);
        }
        const int n = static_cast<int>(fc.hunks.size());
        rows.push_back(h(
            text(dir, fg_dim(muted)),
            text(base, fg_bold(fg)),
            spacer(),
            text(n > 1 ? std::format("change {} of {}", cursor->hunk_index + 1, n)
                       : std::string{"1 change"},
                 fg_dim(muted))
        ).build());
        rows.push_back(rule(muted));
    }

    const syntax::Lang lang = lang_of(fc.path);
    int max_ln = 1;
    for (const auto& hk : fc.hunks)
        max_ln = std::max({max_ln, hk.new_start + hk.new_len, hk.old_start + hk.old_len});
    const int gut_w = std::max(2, static_cast<int>(std::to_string(max_ln).size()));
    const int old_n = line_count(fc.original_contents);
    const int new_n = line_count(fc.new_contents);
    bool focused_overflows = false;

    for (int hi = 0; hi < static_cast<int>(fc.hunks.size()); ++hi) {
        const auto& hk = fc.hunks[static_cast<std::size_t>(hi)];
        const bool sel = hi == cursor->hunk_index;
        auto lines = parse_hunk(hk.patch, old_n, new_n, hk.old_start, hk.new_start);
        int hadd = 0, hrem = 0, first = 0, last = 0;
        for (const auto& dl : lines) {
            if (dl.sign == '+') ++hadd; else if (dl.sign == '-') ++hrem;
            if (dl.sign != ' ') {
                const int ln = dl.newno ? dl.newno : dl.oldno;
                if (!first) first = ln;
                last = ln;
            }
        }
        const std::string where = first == last ? std::format("line {}", first)
                                                : std::format("lines {}\xe2\x80\x93{}", first, last);

        // 每变一行:位置,大小,以及你决定的.
        // 重点的标注在下面并打开。
        rows.push_back(h(
            text(sel ? "\xe2\x96\x8e " : "  ", fg_bold(accent)),
            text(status_dot(hk.status), fg_of(status_color(hk.status))),
            text(" "),
            text(where, sel ? fg_bold(fg) : fg_of(muted)),
            text("   "),
            text(std::format("+{}", hadd), hadd ? fg_of(success) : fg_dim(muted)),
            text(" "),
            text(std::format("\xe2\x88\x92{}", hrem), hrem ? fg_of(danger) : fg_dim(muted)),
            spacer(),
            text(status_word(hk.status), sel || hk.status != Hunk::Status::Pending
                                             ? fg_of(status_color(hk.status))
                                             : fg_dim(muted))
        ).build());
        if (!sel) continue;

        // 重点变化的线条, 封顶了,一个巨大的一个不能推 键关闭屏幕;
        // ^D/^U 页面通过它。
        constexpr int kMaxRows = 24;
        const int nlines = static_cast<int>(lines.size());
        const int off = std::clamp(cursor->body_scroll, 0, std::max(0, nlines - kMaxRows));
        focused_overflows = nlines > kMaxRows;
        rows.push_back(text(""));
        if (off > 0)
            rows.push_back(text(std::format("    \xe2\x86\x91 {} more above", off), fg_dim(muted)));
        int shown = 0;
        for (int li = off; li < nlines && shown < kMaxRows; ++li, ++shown) {
            const auto& dl = lines[static_cast<std::size_t>(li)];
            auto row = h(diff_code_line(dl, lang, gut_w)) | grow(1.0f);
            if (diff_bands_on() && dl.sign == '+') row = std::move(row) | bgc(maya::diff_palette::add_bg);
            else if (diff_bands_on() && dl.sign == '-') row = std::move(row) | bgc(maya::diff_palette::rem_bg);
            rows.push_back((h(std::move(row)) | padding(0, 0, 0, 2)).build());
        }
        if (off + shown < nlines)
            rows.push_back(text(std::format("    \xe2\x86\x93 {} more below", nlines - off - shown),
                                fg_dim(muted)));
        rows.push_back(text(""));
    }

    // ── 进步:一个能取出数的酒吧, 和平庸的数 ──
    rows.push_back(component([kept, undone, decided, total](int w, int) -> Element {
        std::string counts = std::format("{} of {} reviewed", decided, total);
        const std::string ks = std::format("\xe2\x9c\x93 {} kept", kept);
        const std::string us = std::format("\xe2\x9c\x97 {} undone", undone);
        const int full = string_width(counts) + 3 + string_width(ks) + 3 + string_width(us);
        const bool tallies = full + 2 + 8 <= w;
        const int used = (tallies ? full : string_width(counts)) + 2;
        const int bar_w = std::clamp(w - used, 0, 24);
        const int fill_k = total ? (kept * bar_w + total / 2) / total : 0;
        const int fill_u = total ? std::min(bar_w - fill_k, (undone * bar_w + total / 2) / total) : 0;
        std::string bk, bu, br;
        for (int i = 0; i < fill_k; ++i) bk += "\xe2\x94\x81";   // ━
        for (int i = 0; i < fill_u; ++i) bu += "\xe2\x94\x81";
        for (int i = fill_k + fill_u; i < bar_w; ++i) br += "\xe2\x94\x81";
        std::vector<Element> p;
        if (bar_w > 0) {
            p.push_back(text(bk, fg_of(success)));
            p.push_back(text(bu, fg_of(danger)));
            p.push_back(text(br, fg_dim(muted)));
            p.push_back(text("  "));
        }
        p.push_back(text(std::move(counts), fg_of(fg)));
        if (tallies) {
            p.push_back(text("   "));
            p.push_back(text(ks, kept ? fg_of(success) : fg_dim(muted)));
            p.push_back(text("   "));
            p.push_back(text(us, undone ? fg_of(danger) : fg_dim(muted)));
        }
        return h(std::move(p)).build();
    }));
    rows.push_back(text(""));

    // ── 键 ──
    if (cursor->confirm_reject_all) {
        rows.push_back(h(
            text("\xe2\x9a\xa0 ", fg_bold(danger)),
            text("press ^X again to undo every change in every file", fg_bold(danger)),
            text("  \xc2\xb7  any other key cancels", fg_dim(muted))
        ).build());
    } else {
        std::vector<Hint> hints = {
            {"y", "keep", 9, success},
            {"n", "undo", 9, danger},
            {"\xe2\x86\x91\xe2\x86\x93", "change", 7},
        };
        if (files.size() > 1) hints.push_back({"\xe2\x86\x90\xe2\x86\x92", "file", 6});
        if (focused_overflows) hints.push_back({"^D/^U", "scroll", 3});
        hints.push_back({"^A", "keep all", 4, success});
        hints.push_back({"^X", "undo all", 4, danger});
        // Esc 关闭; 任何未决定的都保持磁盘上的位置
        hints.push_back({"Esc", decided == 0 ? "keep all, close" : "close, keep rest", 8});
        rows.push_back(key_hints(std::move(hints)));
    }

    return (v(std::move(rows))
            | padding(1, 2)
            | border(BorderStyle::Round)
            | bcolor(all_done ? success : accent)
            | btext(" Review changes ", BorderTextPos::Top, BorderTextAlign::Start)
            ).build();
}

} // namespace agentty::ui
