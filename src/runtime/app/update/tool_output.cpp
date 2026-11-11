// tool_output.cpp ——工具 -output 查看面板 状态更新函数 (^O
// :  ⁇ list/body 双 -stage 导航、剪贴板副本,以及 末尾-follow 卷轴
//
// collect_viewer_entries/resync_live_tool_viewer
// 留在tool.cpp, 与他们读到的(在internal.hpp中宣布)
// 执行状态相邻——它们是专门小组与执行管道之间的共享的接口边界.

#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/app/update.hpp"

#include <algorithm>
#include <utility>

#include <maya/core/overload.hpp>

#include "agentty/io/clipboard.hpp"          // write_clipboard_text
#include "agentty/runtime/panel/tool_output.hpp"
#include "agentty/runtime/view/thread/turn/agent_timeline/tool_args.hpp"

namespace agentty::tool_output {

namespace {
// 注释 工具会离开它剪切输出的地方。 显示为 "⋯ …" 线条。
// 因为静音切开会让输出看起来完整。
bool is_cut_note(std::string_view t) {
    return t.starts_with("[output capped") || t.starts_with("[output truncated")
        || t.starts_with("[truncated")
        || (t.starts_with("[... ") && t.find(" elided") != std::string_view::npos);
}
std::string as_cut_marker(std::string_view t) {
    t.remove_prefix(1);
    if (!t.empty() && t.back() == ']') t.remove_suffix(1);
    while (t.starts_with(".") || t.starts_with(" ")) t.remove_prefix(1);
    return "\xe2\x8b\xaf " + std::string{t};   // ⋯
}
} // 命名空间结束。

std::string display_text(const Entry& e) {
    // 工具回响了它们自己作为第1行的描述;头文件已经说了.
    std::string_view s{e.output};
    if (e.call.args.is_object())
        for (const char* k : {"display_description", "description"})
            if (auto it = e.call.args.find(k); it != e.call.args.end() && it->is_string()) {
                const auto& d = it->get_ref<const std::string&>();
                if (!d.empty() && s.starts_with(d)
                    && (s.size() == d.size() || s[d.size()] == '\n')) {
                    s.remove_prefix(std::min(s.size(), d.size() + 1));
                    break;
                }
            }
    // 在清洁剂放下前,将剪辑的笔记转换成可见的标记.
    std::string marked;
    std::vector<std::string> tail_notes;
    marked.reserve(s.size());
    const auto last_fence = s.rfind("```");
    for (std::size_t p = 0; p <= s.size();) {
        auto nl = s.find('\n', p);
        if (nl == std::string_view::npos) nl = s.size();
        std::string_view ln = s.substr(p, nl - p);
        auto t = ln;
        while (!t.empty() && t.front() == ' ') t.remove_prefix(1);
        if (is_cut_note(t)) {
            // 关闭围栏后的说明将随围栏而投下。
            if (last_fence != std::string_view::npos && p > last_fence)
                tail_notes.push_back(as_cut_marker(t));
            else
                marked += as_cut_marker(t);
        } else {
            marked.append(ln);
        }
        if (nl == s.size()) break;
        marked += '\n';
        p = nl + 1;
    }
    // shell 类工具为模型把输出包在 ``` 围栏中。
    const bool shell = e.call.args.is_object() && e.call.args.contains("command");
    std::string out = ui::clean_output(shell ? ui::strip_bash_output_fence(marked)
                                             : marked);
    for (auto& n : tail_notes) {
        if (!out.empty()) out += '\n';
        out += n;
    }
    return out;
}

std::vector<std::pair<std::size_t, std::size_t>>
hit_spans(std::string_view line, std::string_view q) {
    std::vector<std::pair<std::size_t, std::size_t>> out;
    if (q.empty() || line.size() < q.size()) return out;
    auto low = [](char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; };
    for (std::size_t i = 0; i + q.size() <= line.size();) {
        std::size_t k = 0;
        while (k < q.size() && low(line[i + k]) == low(q[k])) ++k;
        if (k == q.size()) { out.push_back({i, q.size()}); i += q.size(); }
        else ++i;
    }
    return out;
}

} // namespace agentty::tool_output

namespace pn = agentty::ui::panel;

namespace agentty::app::detail {

using maya::overload;

Cmd tool_output_update(Model& m, msg::ToolOutputMsg tm) {
    return std::visit(overload{
        [&](OpenToolOutput) -> Cmd {
            auto entries = collect_viewer_entries(m);
            if (entries.empty()) {
                auto cmd = set_status_toast(m, "nothing to inspect yet");
                return cmd;
            }
            m.ui.panel.descend(pn::ToolOutput{{std::move(entries), 0, false}});
            m.ui.tool_viewer_scroll.y = 0;
            m.ui.tool_viewer_tail = true;
            return Cmd::none();
        },
        [&](CloseToolOutput) -> Cmd {
            // Esc 语义: 主动搜索先清除; 然后 正文 ~ 回列表;
            // 列表 ~ 取消一个关卡.
            if (auto* o = m.ui.panel.get<pn::ToolOutput>(); o && o->viewing) {
                if (!o->query.empty() || o->typing) {
                    o->query.clear();
                    o->typing = false;
                    return Cmd::none();
                }
                o->viewing = false;
                m.ui.tool_viewer_scroll.y = 0;
                return Cmd::none();
            }
            ascend(m);
            return Cmd::none();
        },
        [&](ToolOutputMove& e) -> Cmd {
            auto* o = m.ui.panel.get<pn::ToolOutput>();
            if (!o) return Cmd::none();
            if (o->viewing) {
                // 正文 级: 三角洲滚动 可视区域 ;
                // 一个页面是视图上显示的内容,减去重叠的行。
                auto& sc = m.ui.tool_viewer_scroll;
                const int page = std::max(1, sc.viewport_bounds.h - 1);
                sc.y = std::clamp(sc.y + e.delta + e.pages * page, 0, std::max(0, sc.max_y));
                // 现场直播 末尾-follow : 滚动 UP 离底部脱离
                // auto-tail so the user can read earlier output; scrolling
                // (或结束)回到下方 -engages 就是这样
                m.ui.tool_viewer_tail = (sc.y >= sc.max_y);
            } else {
                int sz = static_cast<int>(o->entries.size());
                if (sz > 0)
                    o->index = std::clamp(o->index + e.delta + e.pages * 10, 0, sz - 1);
            }
            return Cmd::none();
        },
        [&](ToolOutputSelect) -> Cmd {
            auto* o = m.ui.panel.get<pn::ToolOutput>();
            if (!o || o->viewing) return Cmd::none();
            if (o->index < 0
                || o->index >= static_cast<int>(o->entries.size()))
                return Cmd::none();
            o->viewing = true;
            m.ui.tool_viewer_scroll.y = 0;
            m.ui.tool_viewer_tail = true;
            return Cmd::none();
        },
        [&](ToolOutputStep& e) -> Cmd {
            // QQ/ – 在读取输出时: 跳到相邻条目的
            // 正文 直接。 粘贴在结尾( 不包 - 列表短且
            // 包 -around 离谱比帮助更严重。
            // 列表阶段: 否 -op.
            auto* o = m.ui.panel.get<pn::ToolOutput>();
            if (!o || !o->viewing) return Cmd::none();
            int sz = static_cast<int>(o->entries.size());
            if (sz <= 0) return Cmd::none();
            int next = std::clamp(o->index + e.delta, 0, sz - 1);
            if (next != o->index) {
                o->index = next;
                m.ui.tool_viewer_scroll.y = 0;
                // 搜索继续: 降落在这个输出的第一个对决上.
                if (!o->query.empty()) { o->hit = 0; ++o->hit_seq; }
            }
            return Cmd::none();
        },
        [&](ToolOutputFind& e) -> Cmd {
            auto* o = m.ui.panel.get<pn::ToolOutput>();
            if (!o || o->entries.empty()) return Cmd::none();
            using Op = ToolOutputFind::Op;
            auto jump = [&](int to) {
                o->hit = to;
                ++o->hit_seq;
                m.ui.tool_viewer_tail = false;   // 读,不遵循
            };
            switch (e.op) {
                case Op::Open:
                    if (!o->viewing) {   // / 在列表中搜索所选输出
                        o->viewing = true;
                        m.ui.tool_viewer_scroll.y = 0;
                    }
                    o->typing = true;
                    o->query.clear();
                    break;
                case Op::Type: {
                    if (!o->typing || e.ch < 0x20) break;
                    char buf[4];
                    const char32_t c = e.ch;
                    std::size_t n = 0;
                    if (c < 0x80) { buf[n++] = char(c); }
                    else if (c < 0x800) { buf[n++] = char(0xC0 | (c >> 6)); buf[n++] = char(0x80 | (c & 0x3F)); }
                    else if (c < 0x10000) { buf[n++] = char(0xE0 | (c >> 12)); buf[n++] = char(0x80 | ((c >> 6) & 0x3F)); buf[n++] = char(0x80 | (c & 0x3F)); }
                    else { buf[n++] = char(0xF0 | (c >> 18)); buf[n++] = char(0x80 | ((c >> 12) & 0x3F)); buf[n++] = char(0x80 | ((c >> 6) & 0x3F)); buf[n++] = char(0x80 | (c & 0x3F)); }
                    o->query.append(buf, n);
                    jump(0);
                    break;
                }
                case Op::Erase:
                    if (!o->typing) break;
                    while (!o->query.empty()) {   // 1 UTF-8 个字符
                        const unsigned char b = static_cast<unsigned char>(o->query.back());
                        o->query.pop_back();
                        if ((b & 0xC0) != 0x80) break;
                    }
                    if (!o->query.empty()) jump(0);
                    break;
                case Op::Commit:
                    o->typing = false;
                    break;
                case Op::Cancel:
                    o->typing = false;
                    o->query.clear();
                    break;
                case Op::Next:
                    if (!o->query.empty()) jump(o->hit + 1);
                    break;
                case Op::Prev:
                    if (!o->query.empty()) jump(o->hit - 1);
                    break;
            }
            return Cmd::none();
        },
        [&](ToolOutputCopy) -> Cmd {
            auto* o = m.ui.panel.get<pn::ToolOutput>();
            if (!o) return Cmd::none();
            if (o->index < 0
                || o->index >= static_cast<int>(o->entries.size()))
                return Cmd::none();
            // 读者所见,不是说明和栅栏 意为模型。
            std::string body = tool_output::display_text(
                o->entries[static_cast<std::size_t>(o->index)]);
            std::size_t lines = body.empty() ? 0 : 1;
            for (char c : body) if (c == '\n') ++lines;
            (void)write_clipboard_text(body);   // 原生的 pbcopy/wl-copy/xclip
            auto toast = set_status_toast(m, "copied " + std::to_string(lines)
                + (lines == 1 ? " line" : " lines"));
            return Cmd::batch(
                cmd::write_clipboard(std::move(body)),
                std::move(toast));
        },
    }, tm);
}

} // namespace agentty::app::detail
