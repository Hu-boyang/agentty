// form.cpp ——共享形式. 状态更新函数.
//
// 状态和行为: 没有格言, 没有颜色, 没有布局。 这里的一切都
// 是在形式上纯突变, 所以整个交互模型都是可以测试的,
// 没有一个 终端 ,和 maya::Panel 仍然是唯一的外表主人。
//
// UTF -8号没有文字处理 -shaping 依赖性:字段拥有
// 主机名称、模式 ID、文件路径和 API 键,用户粘贴一个非
// -ASCII 路径不能分割代码点并损坏 缓冲区。通过检查续读,
// 将光标移动和删除到整个码点上 字节 (0b10xxxx×) (
// 中文(简体) ).

#include "agentty/runtime/panel/form.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace agentty::form {

namespace {

[[nodiscard]] bool is_continuation(unsigned char c) noexcept {
    return (c & 0xC0u) == 0x80u;
}

[[nodiscard]] std::size_t prev_boundary(const std::string& s, std::size_t pos) {
    if (pos == 0) return 0;
    std::size_t i = pos - 1;
    while (i > 0 && is_continuation(static_cast<unsigned char>(s[i]))) --i;
    return i;
}

[[nodiscard]] std::size_t next_boundary(const std::string& s, std::size_t pos) {
    if (pos >= s.size()) return s.size();
    std::size_t i = pos + 1;
    while (i < s.size() && is_continuation(static_cast<unsigned char>(s[i]))) ++i;
    return i;
}

// 代码点在 [0] 中, byte_pos) — maya 渲染 以字符表示,我们存储 字节.
[[nodiscard]] std::size_t chars_before(const std::string& s, std::size_t byte_pos) {
    const std::size_t end = std::min(byte_pos, s.size());
    std::size_t n = 0;
    for (std::size_t i = 0; i < end; ++i)
        if (!is_continuation(static_cast<unsigned char>(s[i]))) ++n;
    return n;
}

[[nodiscard]] std::size_t char_count(const std::string& s) {
    return chars_before(s, s.size());
}

void encode_utf8(std::string& out, char32_t ch) {
    if (ch < 0x80) {
        out.push_back(static_cast<char>(ch));
    } else if (ch < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (ch >> 6)));
        out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
    } else if (ch < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (ch >> 12)));
        out.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (ch >> 18)));
        out.push_back(static_cast<char>(0x80 | ((ch >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
    }
}

// 这个代码点是否是 ? 带类型的 变成单曲 -line 字段吗 ?
// Control 字符( 数字) C0, DEL, C1 将生地在 已保存 配置值,
// 以及 U+10FFFF 的代理或任何信息都无效 UTF -8码
[[nodiscard]] bool typeable(char32_t ch) noexcept {
    if (ch < 0x20 || ch == 0x7F) return false;            // C0 + DEL
    if (ch >= 0x80 && ch <= 0x9F) return false;          // C1
    if (ch >= 0xD800 && ch <= 0xDFFF) return false;      // 代孕人员
    return ch <= 0x10FFFF;
}

// 贴子缩小到可以被拆为带类型的:解码为UTF-8,
// 其序列无效或超长,代词和控字符都掉落.
// 新建行和标签也去,因为每个窗体字段都是单的 -line。
// 规则与 typeable() 所以贴纸不能放进钥匙里
std::string clean_paste(std::string_view text) {
    std::string clean;
    clean.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        const std::size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3
                            : (c >> 3) == 0x1E ? 4 : 0;
        if (n == 0 || i + n > text.size()) { ++i; continue; }
        char32_t cp = n == 1 ? c : n == 2 ? (c & 0x1Fu) : n == 3 ? (c & 0x0Fu) : (c & 0x07u);
        bool ok = true;
        for (std::size_t k = 1; k < n; ++k) {
            const auto cc = static_cast<unsigned char>(text[i + k]);
            if ((cc & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        const bool overlong = (n == 2 && cp < 0x80) || (n == 3 && cp < 0x800)
                           || (n == 4 && cp < 0x10000);
        if (!ok || overlong) { ++i; continue; }
        if (typeable(cp)) encode_utf8(clean, cp);
        i += n;
    }
    return clean;
}

// 共享 正文 用于对(数值,光标)对 Text/Secret/Path 都带着
void insert_at(std::string& value, std::size_t& cursor, char32_t ch) {
    if (!typeable(ch)) return;
    if (cursor > value.size()) cursor = value.size();
    std::string enc;
    encode_utf8(enc, ch);
    value.insert(cursor, enc);
    cursor += enc.size();
}

void backspace_at(std::string& value, std::size_t& cursor) {
    if (cursor > value.size()) cursor = value.size();
    if (cursor == 0) return;
    const std::size_t start = prev_boundary(value, cursor);
    value.erase(start, cursor - start);
    cursor = start;
}

void delete_forward_at(std::string& value, std::size_t& cursor) {
    if (cursor >= value.size()) return;
    const std::size_t end = next_boundary(value, cursor);
    value.erase(cursor, end - cursor);
}

void move_at(const std::string& value, std::size_t& cursor, int delta) {
    if (cursor > value.size()) cursor = value.size();
    while (delta < 0 && cursor > 0)            { cursor = prev_boundary(value, cursor); ++delta; }
    while (delta > 0 && cursor < value.size()) { cursor = next_boundary(value, cursor); --delta; }
}

// 数字-only编辑在草稿字符串上,编辑结束后夹住一次.
//
// `fresh` 意思是这里是 首次 编辑会话的位数, 它替换而不是附加:
// 开始编辑时屏幕上的值是 SAVED 一种,不是用户的 带类型的 ,所以,
// 我说 "type 8 into a row showing 4096"
// 表示8,4-0-9-6仍然为4096型.
//
// 草案不是 被按住每根键。 每根键是错误 : 在分钟大于
// 0 (一个 1-65535 端口) 的行上, 清除后的值
// 会折叠到分钟, 每个后面的位数都会被附加到它上,
// 并且只能输入数字。 在 0-8 行上, 它被钉在最大值上,
// 并失聪。 草稿只封盖长度, 无法溢出; 范围被强制。
// number_commit.
constexpr std::size_t kNumberMaxDigits = 18;   // 装入64

void number_insert(field::Number& n, char32_t ch, bool fresh) {
    if (ch < U'0' || ch > U'9') return;
    if (!n.editing) { n.editing = true; n.draft = std::to_string(n.value); }
    if (fresh) n.draft.clear();
    if (n.draft == "0") n.draft.clear();          // 无主 0
    if (n.draft.size() >= kNumberMaxDigits) return;
    n.draft.push_back(static_cast<char>(ch));
}

// 草稿的值表示: 空值为"nothing typed",
// 使旧值保持; 否则数字, 被夹入范围。
std::int64_t number_from_draft(const field::Number& n) {
    if (n.draft.empty()) return n.value;
    std::int64_t v = 0;
    for (char c : n.draft) v = v * 10 + (c - '0');
    return std::clamp(v, n.min, n.max);
}

void number_commit(field::Number& n) {
    if (!n.editing) return;
    n.value = number_from_draft(n);
    n.draft.clear();
    n.editing = false;
}

}  // 命名空间结束。

std::string number_text(const field::Number& n) {
    return n.editing ? n.draft : std::to_string(n.value);
}

std::int64_t number_value(const field::Number& n) {
    return n.editing ? number_from_draft(n) : n.value;
}

namespace {

// 将突出点保留在列表内,滚动窗口则贴近它.
//
// 这是 HINT。真正的窗口只有在油漆时间才能知道(取决
// 于面板的真实高度),所以 maya::Panel
// 内容 -derives 将精确的算术转换为两个所有者,
// 一个测量结果—— 形状 回滚缓冲区 修剪漂移。
// 全部的,我爱的 状态更新函数 欠债是一个正常的起点。
void reclamp(focus::Choosing& d, int count) {
    if (count <= 0) { d.highlighted = 0; d.scroll = 0; return; }
    d.highlighted = std::clamp(d.highlighted, 0, count - 1);
    const int view = kDropdownViewport;
    if (d.highlighted < d.scroll)         d.scroll = d.highlighted;
    if (d.highlighted >= d.scroll + view) d.scroll = d.highlighted - view + 1;
    d.scroll = std::clamp(d.scroll, 0, std::max(0, count - view));
}

} // 命名空间结束。

// ── 查询 ───────────────────────────────────────────────────────────────

const Field* Form::find(std::string_view id) const noexcept {
    for (const auto& f : fields) if (f.id == id) return &f;
    return nullptr;
}
Field* Form::find(std::string_view id) noexcept {
    for (auto& f : fields) if (f.id == id) return &f;
    return nullptr;
}

// ── 投影帮助 ───────────────────────────────────────────────────

Options visible_options(const field::Choice& c) {
    Options out;
    for (int i = 0; i < c.count(); ++i) {
        const auto k = static_cast<std::size_t>(i);
        out.labels.push_back(c.labels[k]);
        out.hints.push_back(k < c.hints.size() ? c.hints[k] : std::string{});
    }
    return out;
}

std::size_t secret_filled(const field::Secret& s) noexcept {
    return char_count(s.value);
}

std::size_t caret_offset(const FieldValue& v, bool editing) noexcept {
    if (!editing) return std::string::npos;
    return std::visit([](const auto& f) -> std::size_t {
        using T = std::decay_t<decltype(f)>;
        // A BYTE 资源 maya 连结 光标 ,在数值上带有子字符串。
        if constexpr (std::is_same_v<T, field::Text>
                   || std::is_same_v<T, field::Path>)
            return std::min(f.cursor, f.value.size());
        // 秘密绘图点, 永远不要值; 任何非 -npos 表示 "editing".
        else if constexpr (std::is_same_v<T, field::Secret>)
            return chars_before(f.value, f.cursor);
        else if constexpr (std::is_same_v<T, field::Number>)
            return number_text(f).size();   // 结尾的位数附加
        else
            return std::string::npos;
    }, v);
}

// ── 编辑 ──────────────────────────────────────────────────────────────

void insert(FieldValue& v, char32_t ch, bool fresh) {
    std::visit([&](auto& f) {
        using T = std::decay_t<decltype(f)>;
        if constexpr (std::is_same_v<T, field::Text>
                   || std::is_same_v<T, field::Secret>
                   || std::is_same_v<T, field::Path>)
            insert_at(f.value, f.cursor, ch);
        else if constexpr (std::is_same_v<T, field::Number>)
            number_insert(f, ch, fresh);
    }, v);
}

void backspace(FieldValue& v) {
    std::visit([&](auto& f) {
        using T = std::decay_t<decltype(f)>;
        if constexpr (std::is_same_v<T, field::Text>
                   || std::is_same_v<T, field::Secret>
                   || std::is_same_v<T, field::Path>)
            backspace_at(f.value, f.cursor);
        else if constexpr (std::is_same_v<T, field::Number>) {
            // 关于草案: Backspace 删除最后 带类型的 数字并
            // 可以空出字段,所以一个清除的行会占用任何数字的下一个.
            if (!f.editing) { f.editing = true; f.draft = std::to_string(f.value); }
            if (!f.draft.empty()) f.draft.pop_back();
        }
    }, v);
}

void delete_forward(FieldValue& v) {
    std::visit([&](auto& f) {
        using T = std::decay_t<decltype(f)>;
        if constexpr (std::is_same_v<T, field::Text>
                   || std::is_same_v<T, field::Secret>
                   || std::is_same_v<T, field::Path>)
            delete_forward_at(f.value, f.cursor);
    }, v);
}

void move_cursor(FieldValue& v, int delta) {
    std::visit([&](auto& f) {
        using T = std::decay_t<decltype(f)>;
        if constexpr (std::is_same_v<T, field::Text>
                   || std::is_same_v<T, field::Secret>
                   || std::is_same_v<T, field::Path>)
            move_at(f.value, f.cursor, delta);
    }, v);
}

void cursor_home(FieldValue& v) {
    std::visit([&](auto& f) {
        using T = std::decay_t<decltype(f)>;
        if constexpr (std::is_same_v<T, field::Text>
                   || std::is_same_v<T, field::Secret>
                   || std::is_same_v<T, field::Path>)
            f.cursor = 0;
    }, v);
}

void cursor_end(FieldValue& v) {
    std::visit([&](auto& f) {
        using T = std::decay_t<decltype(f)>;
        if constexpr (std::is_same_v<T, field::Text>
                   || std::is_same_v<T, field::Secret>
                   || std::is_same_v<T, field::Path>)
            f.cursor = f.value.size();
    }, v);
}

void paste(FieldValue& v, std::string_view text) {
    // 脱 newlines/tabs : 这些是单 -line 字段,
    // 以及用于在手部意外提交周围形式贴上的新线条 -rolled
    // 输入此选项替换。
    const std::string clean = clean_paste(text);

    std::visit([&](auto& f) {
        using T = std::decay_t<decltype(f)>;
        if constexpr (std::is_same_v<T, field::Text>
                   || std::is_same_v<T, field::Secret>
                   || std::is_same_v<T, field::Path>) {
            if (f.cursor > f.value.size()) f.cursor = f.value.size();
            f.value.insert(f.cursor, clean);
            f.cursor += clean.size();
        } else if constexpr (std::is_same_v<T, field::Number>) {
            // 数字附加在草稿中,比如打字。 OPENS 编辑从空草稿开始(
            // paste_into_focused ),所以贴贴 "4096"
            // 表示4096;中间 -edit 伸展了它的意义 带类型的.
            for (char c : clean)
                if (c >= '0' && c <= '9')
                    number_insert(f, static_cast<char32_t>(c), /*新鲜=*/false);
        }
    }, v);
}

bool paste_into(Form& f, std::string_view text) {
    auto* row = f.focused();
    if (!f.editing() || !row || !row->editable() || row->locked) return false;
    paste(row->value, text);
    f.dirty = true;
    f.edit_dirty = true;   // 粘贴 字节 数到承诺 -on-exit
    return true;
}

bool paste_into_focused(Form& f, std::string_view text) {
    if (f.editing()) return paste_into(f, text);
    if (f.choosing()) return false;
    auto* row = f.focused();
    if (!row || !row->is_text_like() || !row->editable()) return false;
    // 打开会话只为将落地的东西的贴面. paste()
    // 条形 newlines/tabs 每一个 -line
    // 字段和数字行只取数字;没有一行接受的粘贴保持否 -op
    // 而不是在编辑时留下行 带类型的 (该半部分) -state
    // 类型 -to-edit 拒绝在数字行上写字母。
    const bool numeric = std::holds_alternative<field::Number>(row->value);
    const std::string clean = clean_paste(text);
    const bool lands = numeric
        ? std::any_of(clean.begin(), clean.end(), [](char c) { return c >= '0' && c <= '9'; })
        : !clean.empty();
    if (!lands) return false;
    begin_edit(f);
    // 粘贴取代数字 已保存 值,作为第一个 带类型的 数字是的。
    if (auto* n = std::get_if<field::Number>(&row->value)) n->draft.clear();
    return paste_into(f, text);
}

std::string text_of(const Form& f, std::string_view id) {
    const auto* row = f.find(id);
    if (!row) return {};
    if (const auto* t = std::get_if<field::Text>(&row->value))   return t->value;
    if (const auto* s = std::get_if<field::Secret>(&row->value)) return s->value;
    if (const auto* p = std::get_if<field::Path>(&row->value))   return p->value;
    return {};
}

bool toggle_of(const Form& f, std::string_view id) {
    const auto* row = f.find(id);
    if (!row) return false;
    const auto* t = std::get_if<field::Toggle>(&row->value);
    return t && t->on;
}

std::string choice_of(const Form& f, std::string_view id) {
    const auto* row = f.find(id);
    if (!row) return {};
    if (const auto* c = std::get_if<field::Choice>(&row->value))
        return std::string{c->id()};
    return {};
}

void clear(FieldValue& v) {
    std::visit([&](auto& f) {
        using T = std::decay_t<decltype(f)>;
        if constexpr (std::is_same_v<T, field::Text>
                   || std::is_same_v<T, field::Secret>
                   || std::is_same_v<T, field::Path>) {
            f.value.clear();
            f.cursor = 0;
        } else if constexpr (std::is_same_v<T, field::Number>) {
            // 中间 -edit ^U 空出草稿( 下一个数字
            // 开始新); 编辑后的外部清晰到最小。
            if (f.editing) f.draft.clear();
            else           f.value = f.min;
        } else if constexpr (std::is_same_v<T, field::Slider>) {
            f.value = f.min;
        }
    }, v);
}

void adjust(FieldValue& v, int dir) {
    std::visit([&](auto& f) {
        using T = std::decay_t<decltype(f)>;
        if constexpr (std::is_same_v<T, field::Toggle>) {
            f.on = !f.on;
        } else if constexpr (std::is_same_v<T, field::Choice>) {
            if (f.count() > 0) f.index = f.normalized(f.index + dir);
        } else if constexpr (std::is_same_v<T, field::Number>) {
            f.value = std::clamp<std::int64_t>(f.value + dir, f.min, f.max);
        } else if constexpr (std::is_same_v<T, field::Slider>) {
            // 与阶格相匹配,这样重复的±步不能累积浮
            // -point漂移到像0.6500000000000001这样的值.
            const double stepped = f.value + dir * f.step;
            const double snapped = (f.step > 0.0)
                ? std::round(stepped / f.step) * f.step
                : stepped;
            f.value = std::clamp(snapped, f.min, f.max);
        }
    }, v);
}

// ── 表-level 业务 ────────────────────────────────

void move(Form& f, int delta) {
    if (f.choosing()) return;          // 降下时拥有突出位置
    if (f.fields.empty()) return;
    const int n = static_cast<int>(f.fields.size());
    const int step = delta >= 0 ? 1 : -1;

    // 头文件是标签,而不是设置:停止在其中是一个死键压.
    // 跳过,但行走的行走 如此数到 -header
    // 窗体(或每行为 头文件 终止而不是旋转。
    int moved = 0;
    int guard = 0;
    while (moved < std::abs(delta) && guard < n) {
        f.cursor = ((f.cursor + step) % n + n) % n;
        ++guard;
        if (!f.fields[static_cast<std::size_t>(f.cursor)].is_header()) ++moved;
    }
}

bool focus_row(Form& f, std::string_view id) {
    if (id.empty()) return false;
    for (int i = 0; i < static_cast<int>(f.fields.size()); ++i)
        if (f.fields[static_cast<std::size_t>(i)].id == id) { f.cursor = i; return true; }
    return false;
}

void move_edge(Form& f, bool last) {
    // Home/End : 数据 first/last 无 -header 字段。
    // 纯扫描,不是 move() - 步行不能走 - 步行必须走 WRAP
    // (End 而不是扫荡到顶端)
    if (f.choosing()) return;
    const int n = static_cast<int>(f.fields.size());
    if (last) {
        for (int i = n - 1; i >= 0; --i)
            if (!f.fields[static_cast<std::size_t>(i)].is_header()) {
                f.cursor = i;
                return;
            }
    } else {
        for (int i = 0; i < n; ++i)
            if (!f.fields[static_cast<std::size_t>(i)].is_header()) {
                f.cursor = i;
                return;
            }
    }
}

void move_page(Form& f, int delta) {
    // PgUp/PgDn : 脚步 无需 包页——一个页面
    // -move 背向顶端的电传导口 正在失去方向 所以行走的夹在
    // 边缘上(降落在最后的不 -header 字段通过)。
    if (f.choosing() || f.fields.empty()) return;
    const int n    = static_cast<int>(f.fields.size());
    const int step = delta >= 0 ? 1 : -1;
    int remaining  = std::abs(delta);
    int best       = f.cursor;
    for (int i = f.cursor + step; i >= 0 && i < n && remaining > 0; i += step) {
        if (f.fields[static_cast<std::size_t>(i)].is_header()) continue;
        best = i;
        --remaining;
    }
    f.cursor = best;
}

Activated activate(Form& f) {
    Field* row = f.focused();
    if (!row || row->locked) return Activated::Nothing;

    if (row->is_action()) return Activated::FiredAction;

    // 选手的人选住另一个 覆盖面板 - 电话 面板 打开它,
    // 然后写回结果。表格中故意对他们一无所知。
    if (row->is_pick()) return Activated::HandOff;

    if (row->is_choice()) {
        // 打开下拉 无需 切换字段:突出点开始于当前选择,
        // 值仅在承诺时更改,所以 Esc 是个真人
        // 取消 而不是一个逆差。
        const auto& c = std::get<field::Choice>(row->value);
        focus::Choosing d;
        d.highlighted = c.normalized(c.index);
        reclamp(d, c.count());
        f.focus = d;
        return Activated::OpenedDropdown;
    }

    if (row->is_text_like()) {
        begin_edit(f);
        return Activated::StartedEditing;
    }

    adjust(row->value, +1);            // 切换 和任何其他可调整的
    f.dirty = true;
    return Activated::Changed;
}

void begin_edit(Form& f) {
    f.focus = focus::Editing{};
    f.edit_dirty = false;   // 新的会议——承诺 -on-exit 第一次更改时的臂膀
    if (auto* row = f.focused())
        if (auto* n = std::get_if<field::Number>(&row->value)) {
            n->editing = true;
            n->draft = std::to_string(n->value);
        }
}

void end_edit(Form& f) {
    if (auto* row = f.focused())
        if (auto* n = std::get_if<field::Number>(&row->value)) number_commit(*n);
    f.focus = focus::Browsing{};
}

void rebuild(Form& f, Form next, std::string_view prefer) {
    const Field* was = f.focused();
    const std::string id = !prefer.empty() ? std::string{prefer}
                         : was ? was->id : std::string{};
    const int at = f.cursor;
    const bool dirty = f.dirty;
    f = std::move(next);
    f.dirty = dirty;
    f.focus = focus::Browsing{};   // 编辑或下拉不能超过其行
    const int n = static_cast<int>(f.fields.size());
    if (n == 0) { f.cursor = 0; return; }
    // 同样的一行 以ID,如果它幸存重建。
    if (!id.empty())
        for (int i = 0; i < n; ++i)
            if (f.fields[static_cast<std::size_t>(i)].id == id
                && !f.fields[static_cast<std::size_t>(i)].is_header()) {
                f.cursor = i;
                return;
            }
    f.cursor = at;
    settle_cursor(f);
}

void settle_cursor(Form& f) {
    const int n = static_cast<int>(f.fields.size());
    if (n == 0) { f.cursor = 0; return; }
    // 距离同一位置最近的真行,先往下看.
    const int start = std::clamp(f.cursor, 0, n - 1);
    for (int d = 0; d < n; ++d)
        for (const int i : {start + d, start - d})
            if (i >= 0 && i < n && !f.fields[static_cast<std::size_t>(i)].is_header()) {
                f.cursor = i;
                return;
            }
    f.cursor = start;   // 每行都是头文件:没有比这更好的了
}

bool escape(Form& f) {
    if (f.choosing()) { f.focus = focus::Browsing{}; return false; }   // cancel
    if (f.editing())  { end_edit(f); return false; }                   // 离开字段
    return true;                                                       // 关闭窗体
}

void dropdown_move(Form& f, int delta) {
    auto* d = f.dropdown();
    const Field* row = f.focused();
    if (!d || !row || !row->is_choice()) return;
    const int n = std::get<field::Choice>(row->value).count();
    if (n <= 0) return;
    d->highlighted = ((d->highlighted + delta) % n + n) % n;
    reclamp(*d, n);
}

bool dropdown_commit(Form& f) {
    auto* d = f.dropdown();
    if (!d) return false;
    Field* row = f.focused();
    if (!row || !row->is_choice()) { f.focus = focus::Browsing{}; return false; }

    auto& c = std::get<field::Choice>(row->value);
    bool changed = false;
    if (c.count() > 0) {
        const int pick = std::clamp(d->highlighted, 0, c.count() - 1);
        changed = (c.normalized(c.index) != pick);
        c.index = pick;
    }
    f.focus = focus::Browsing{};
    if (changed) f.dirty = true;
    return changed;
}

void reset_field(Form& f, const FieldValue& dflt) {
    Field* row = f.focused();
    if (!row || row->locked) return;
    row->value = dflt;
    row->error.clear();
    f.dirty = true;
}

// ── 构建器 ──────────────────────────────────────────────────────────────

namespace {
void push(Form& form, std::string id, std::string label, std::string help, FieldValue v) {
    Field f;
    f.id    = std::move(id);
    f.label = std::move(label);
    f.help  = std::move(help);
    f.value = std::move(v);
    form.fields.push_back(std::move(f));
}
} // 命名空间结束。

Builder& Builder::toggle(std::string id, std::string label, bool on, std::string help) {
    push(form_, std::move(id), std::move(label), std::move(help), field::Toggle{on});
    return *this;
}

Builder& Builder::choice(std::string id, std::string label,
                         std::vector<std::string> labels,
                         std::vector<std::string> ids,
                         std::string_view selected,
                         std::string help,
                         std::vector<std::string> hints) {
    field::Choice c;
    c.labels = std::move(labels);
    c.ids    = std::move(ids);
    c.hints  = std::move(hints);
    if (!selected.empty()) c.select_id(selected);
    push(form_, std::move(id), std::move(label), std::move(help), std::move(c));
    return *this;
}

Builder& Builder::number(std::string id, std::string label, std::int64_t value,
                         std::int64_t min, std::int64_t max, std::string help) {
    push(form_, std::move(id), std::move(label), std::move(help),
         field::Number{std::clamp(value, min, max), min, max});
    return *this;
}

Builder& Builder::slider(std::string id, std::string label, double value,
                         double min, double max, double step, std::string help) {
    field::Slider s;
    s.value = std::clamp(value, min, max);
    s.min = min; s.max = max; s.step = step;
    push(form_, std::move(id), std::move(label), std::move(help), s);
    return *this;
}

Builder& Builder::text(std::string id, std::string label, std::string value,
                       std::string help) {
    const auto n = value.size();
    push(form_, std::move(id), std::move(label), std::move(help),
         field::Text{std::move(value), n});
    return *this;
}

Builder& Builder::secret(std::string id, std::string label, std::string value,
                         std::string help) {
    const auto n = value.size();
    push(form_, std::move(id), std::move(label), std::move(help),
         field::Secret{std::move(value), n});
    return *this;
}

Builder& Builder::path(std::string id, std::string label, std::string value,
                       std::string help, bool want_dir) {
    const auto n = value.size();
    push(form_, std::move(id), std::move(label), std::move(help),
         field::Path{std::move(value), n, want_dir});
    return *this;
}

Builder& Builder::action(std::string id, std::string label, std::string help,
                         std::string hint) {
    field::Action a;
    a.hint = std::move(hint);
    push(form_, std::move(id), std::move(label), std::move(help), std::move(a));
    return *this;
}

Builder& Builder::header(std::string label) {
    push(form_, std::string{"__header."} + label, std::move(label), {},
         field::Header{});
    return *this;
}

Builder& Builder::pick(std::string id, std::string label, std::string current,
                       std::string help) {
    field::Pick p;
    p.label = std::move(current);
    push(form_, std::move(id), std::move(label), std::move(help), std::move(p));
    return *this;
}

Builder& Builder::lock(std::string reason) {
    if (!form_.fields.empty()) {
        form_.fields.back().locked = true;
        form_.fields.back().locked_reason = std::move(reason);
    }
    return *this;
}

Builder& Builder::placeholder(std::string text) {
    if (!form_.fields.empty())
        if (auto* s = std::get_if<field::Secret>(&form_.fields.back().value))
            s->placeholder = std::move(text);
    return *this;
}

Builder& Builder::origin(std::string where) {
    if (!form_.fields.empty()) form_.fields.back().origin = std::move(where);
    return *this;
}

} // namespace agentty::form
