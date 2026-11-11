#include "agentty/tool/util/partial_json.hpp"

#include <cctype>
#include <cstring>
#include <vector>

namespace agentty::tools::util {

// 一点 JSON 手语法中知道它在哪里的行者,所以切片可以关闭成有效 JSON
// 不论它落在什么上,都有一个钥匙 `{"co` ),一个没有结肠的密钥(
// `{"a"` ),半升(含" ⁇ "," ⁇ "). `tr`,
// `nul` ),半数字( `-`, `1.`, `1e` , 表示一个折叠的逗号,
// 或打开字符串。规则是保留每个完整的成员并放下正在写入的,但一个值字符串
// 切中 -text ,它保留了它的文本,这样现场预览就可以显示它。
std::string close_partial_json(std::string_view raw) {
    // 下一个容器在等着什么
    enum class Want : char {
        KeyOrEnd,    // `{` 刚刚打开的: 密钥或 `}`
        Key,         // 在对象中的`,`之后
        Colon,       // 键后
        Value,       // 之后 `:` 或 或 时 间 `,` / 数组中 `[` (或 减 减 ) `]`)
        CommaOrEnd,  // 成员已填满
    };
    struct Frame { char open; Want want; };
    std::vector<Frame> stack;
    // 字节 减 减: `out` 以切回写入成员被
    // 丢弃时的键:就在键(对象)或其主逗号(阵列)之前。
    std::vector<std::size_t> member_start;

    std::string out;
    out.reserve(raw.size() + 16);
    bool top_done = false;      // 已写入整个顶值 -level 值

    // 打开的标志 EOF : 字符串( 键或值) , 或 空 literal/number.
    enum class Tok { None, KeyString, ValueString, Bare };
    Tok tok = Tok::None;
    bool escape = false;
    std::size_t bare_from = 0;  // 光线标志从那里开始

    auto in_array = [&] { return !stack.empty() && stack.back().open == '['; };
    auto value_done = [&] {
        if (stack.empty()) top_done = true;
        else stack.back().want = Want::CommaOrEnd;
    };
    auto end_bare = [&] {
        if (tok == Tok::Bare) { tok = Tok::None; value_done(); }
    };

    for (char c : raw) {
        if (tok == Tok::KeyString || tok == Tok::ValueString) {
            out.push_back(c);
            if (escape) { escape = false; continue; }
            if (c == '\\') { escape = true; continue; }
            if (c == '"') {
                if (tok == Tok::KeyString) stack.back().want = Want::Colon;
                else value_done();
                tok = Tok::None;
            }
            continue;
        }
        if (tok == Tok::Bare) {
            const bool part = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z')
                           || c == '-' || c == '+' || c == '.' || c == 'E';
            if (part) { out.push_back(c); continue; }
            end_bare();
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { out.push_back(c); continue; }

        const Want want = stack.empty() ? Want::Value : stack.back().want;
        switch (c) {
            case '{': case '[':
                if (want == Want::Value || want == Want::KeyOrEnd) {
                    if (in_array() && want == Want::Value && member_start.size() < stack.size())
                        member_start.push_back(out.size());
                    out.push_back(c);
                    stack.push_back({c, c == '{' ? Want::KeyOrEnd : Want::Value});
                }
                break;
            case '}': case ']':
                if (!stack.empty() && stack.back().open == (c == '}' ? '{' : '[')) {
                    out.push_back(c);
                    stack.pop_back();
                    if (member_start.size() > stack.size()) member_start.resize(stack.size());
                    value_done();
                }
                break;
            case ':':
                if (want == Want::Colon) { out.push_back(c); stack.back().want = Want::Value; }
                break;
            case ',':
                if (want == Want::CommaOrEnd) {
                    // 成员在此逗号完成之前; 开始一个新的逗号
                    if (member_start.size() == stack.size()) member_start.back() = out.size();
                    else member_start.push_back(out.size());
                    out.push_back(c);
                    stack.back().want = stack.back().open == '{' ? Want::Key : Want::Value;
                }
                break;
            case '"':
                if (want == Want::KeyOrEnd || want == Want::Key) {
                    if (member_start.size() < stack.size()) member_start.push_back(out.size());
                    tok = Tok::KeyString;
                } else {
                    if (in_array() && member_start.size() < stack.size())
                        member_start.push_back(out.size());
                    tok = Tok::ValueString;
                }
                out.push_back(c);
                break;
            default:
                if (want == Want::Value) {
                    if (in_array() && member_start.size() < stack.size())
                        member_start.push_back(out.size());
                    bare_from = out.size();
                    tok = Tok::Bare;
                    out.push_back(c);
                }
                break;   // 其它的都是我们无法定位的噪音,放下它
        }
    }

    // ── 关闭在 EOF 打开的 ──────────────────────────────────────
    // 光彩的标志只有在已经是一个完整的字面或数字时才保留。
    if (tok == Tok::Bare) {
        const std::string_view t{out.data() + bare_from, out.size() - bare_from};
        const bool literal = t == "true" || t == "false" || t == "null";
        const bool number = !t.empty()
            && (std::isdigit(static_cast<unsigned char>(t.back())) != 0)
            && t.find_first_not_of("0123456789+-.eE") == std::string_view::npos;
        if (literal || number) {
            value_done();
        } else {
            out.resize(bare_from);   // `tr`, `-`, `1.` : 尚未为值
        }
        tok = Tok::None;
    }
    if (tok == Tok::ValueString) {
        if (escape) out.pop_back();          // 孤独 `\` 切开时
        // the cut can fall inside a multi-byte character (`é`, `日`); its
        // 仅领导字节就无效UTF-8,所以放弃它们
        {
            std::size_t k = out.size(), cont = 0;
            while (k > 0 && cont < 3
                   && (static_cast<unsigned char>(out[k - 1]) & 0xC0) == 0x80) { --k; ++cont; }
            if (k > 0) {
                const auto lead = static_cast<unsigned char>(out[k - 1]);
                const std::size_t need = lead >= 0xF0 ? 3 : lead >= 0xE0 ? 2 : lead >= 0xC0 ? 1 : 0;
                if (need > cont) out.resize(k - 1);
            }
        }
        // 内部切开 \u\\\\ 留下无效的越狱: 放下它
        if (const auto u = out.rfind("\\u"); u != std::string::npos
            && out.size() - u < 6
            && out.find('"', u) == std::string::npos) {
            std::size_t bs = 0;
            for (std::size_t k = u; k > 0 && out[k - 1] == '\\'; --k) ++bs;
            if (bs % 2 == 0) out.resize(u);   // 编号 `\` 真正开始逃跑
        }
        // 高代孕(\uD800-\uDBFF)需要它的低半身后;
        // 两人之间切开一个没有解析器接受的逃生, 所以放弃它
        if (out.size() >= 6 && out[out.size() - 6] == '\\' && out[out.size() - 5] == 'u') {
            std::size_t bs = 0;
            for (std::size_t k = out.size() - 6; k > 0 && out[k - 1] == '\\'; --k) ++bs;
            const char h0 = static_cast<char>(std::tolower(static_cast<unsigned char>(out[out.size() - 4])));
            const char h1 = static_cast<char>(std::tolower(static_cast<unsigned char>(out[out.size() - 3])));
            if (bs % 2 == 0 && h0 == 'd' && h1 >= '8' && h1 <= 'b') out.resize(out.size() - 6);
        }
        out.push_back('"');
        value_done();
    }
    // 中键切除 -string 离开它的 帧 想要一把钥匙,
    // 但钥匙是 字节 正在输入 `out` 标记它未完成,所以下面的切片会去掉它.
    const bool key_open = tok == Tok::KeyString;
    tok = Tok::None;

    bool innermost = true;
    while (!stack.empty()) {
        Frame& f = stack.back();
        const bool has_partial_member = member_start.size() == stack.size();
        // 未完成 = 除完整成员或空容器外的任何东西。
        // 惟内之内 帧 可以是中间 -member 外部;
        // 外部 帧 中间 -value ,而这个值是内容器被关闭。
        const bool unfinished = innermost
            && (key_open || (f.want != Want::CommaOrEnd && f.want != Want::KeyOrEnd));
        if (unfinished && has_partial_member) out.resize(member_start.back());
        innermost = false;
        // 沿白空间和现在的-dangling 逗号修剪
        while (!out.empty() && (out.back() == ' ' || out.back() == '\t'
                                || out.back() == '\n' || out.back() == '\r'))
            out.pop_back();
        if (!out.empty() && out.back() == ',') out.pop_back();
        out.push_back(f.open == '{' ? '}' : ']');
        stack.pop_back();
        if (member_start.size() > stack.size()) member_start.resize(stack.size());
    }

    if (!top_done && out.find_first_not_of(" \t\r\n") == std::string::npos) return "null";
    return out;
}

namespace {

// snow 的共享前缀行:先是 `"key"` 前进,
// 然后是 `:` 和 `"` 打开。返回刚刚通过开口报价的索引,
// 如果预期前缀还没有,则返回 std::string_view::npos。
//
// 标注: 此操作在 流式传输 高频路径 (称为每个预览勾选符的别名,
// ~8x/sec 先前的执行工作已形成一个框架。 `"key"` 针
// 头作为堆积 -allocated std::string 并
// 把它交给了 std::string_view::find ——每
// 台O(N)扫描 调用 ,加一个 maloc+ 免费 调用 我们
// 现在扫描 缓冲区 曾经寻找开场 `"` ,然后对着键。 分配 ;
// 内环是单 字节 比较第一个字符上通常失败的值。
std::size_t sniff_prefix(std::string_view raw, std::string_view key) {
    if (key.empty() || raw.size() < key.size() + 4) return std::string_view::npos;
    const char* const base = raw.data();
    const std::size_t n    = raw.size();
    const std::size_t klen = key.size();
    // 最后一个索引 `"key"` 仍然可以开始:需要 klen+2
    // 字节 引用键加至少 `:` `"` 之后,我们再谈吧。
    const std::size_t last_start = n - (klen + 3);
    for (std::size_t i = 0; i <= last_start; ++i) {
        if (base[i] != '"') continue;
        if (base[i + klen + 1] != '"') continue;
        if (std::memcmp(base + i + 1, key.data(), klen) != 0) continue;
        std::size_t p = i + klen + 2;
        while (p < n && (base[p] == ' ' || base[p] == '\t' || base[p] == '\n')) ++p;
        if (p >= n || base[p] != ':') continue;
        ++p;
        while (p < n && (base[p] == ' ' || base[p] == '\t' || base[p] == '\n')) ++p;
        if (p >= n || base[p] != '"') continue;
        return p + 1;
    }
    return std::string_view::npos;
}

// 将JSON(`\X`)逃逸应用到输出缓冲区.
// 未知逃逸物降解为字面字节——对流式传输预览的罚款,
// 我们倾向于良性显示而不是例外.
void apply_escape(char n, std::string& out) {
    switch (n) {
        case 'n':  out += '\n'; break;
        case 't':  out += '\t'; break;
        case 'r':  out += '\r'; break;
        case '"':  out += '"';  break;
        case '\\': out += '\\'; break;
        case '/':  out += '/';  break;
        case 'b':  out += '\b'; break;
        case 'f':  out += '\f'; break;
        default:   out += n;    break;
    }
}

} // 命名空间结束。

std::optional<std::string>
sniff_string(std::string_view raw, std::string_view key) {
    std::size_t p = sniff_prefix(raw, key);
    if (p == std::string_view::npos) return std::nullopt;
    std::string out;
    out.reserve(64);
    while (p < raw.size()) {
        char c = raw[p];
        if (c == '\\') {
            if (p + 1 >= raw.size()) return std::nullopt; // 等待下一个三角洲
            apply_escape(raw[p + 1], out);
            p += 2;
        } else if (c == '"') {
            return out;
        } else {
            out.push_back(c);
            ++p;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t>
locate_string_value(std::string_view raw, std::string_view key) {
    std::size_t p = sniff_prefix(raw, key);
    if (p == std::string_view::npos) return std::nullopt;
    return p;
}

std::string
decode_string_from(std::string_view raw, std::size_t offset) {
    std::string out;
    if (offset >= raw.size()) return out;
    out.reserve(raw.size() - offset);
    std::size_t p = offset;
    while (p < raw.size()) {
        char c = raw[p];
        if (c == '\\') {
            if (p + 1 >= raw.size()) break; // 半个 -escape 时间 缓冲区 边
            apply_escape(raw[p + 1], out);
            p += 2;
        } else if (c == '"') {
            return out;
        } else {
            out.push_back(c);
            ++p;
        }
    }
    return out;
}

bool
decode_string_append(std::string_view raw,
                     std::size_t* through,
                     std::string& out,
                     bool* closed)
{
    // 关机后 `"` 之后被人看见了 调用 不得将其后的事物
    // 解码(下), JSON 成员),输入值。
    if (closed && *closed) return true;
    if (!through || *through >= raw.size()) return false;
    std::size_t p = *through;
    // 保留新猜测 字节 - 大部分越狱者是1人 - 大部分是1人
    // -byte 扩建( `\n` ~新线)所以 raw.size ()
    // - p是解码生长上紧的上方束缚.
    if (out.capacity() - out.size() < raw.size() - p)
        out.reserve(out.size() + (raw.size() - p));
    while (p < raw.size()) {
        char c = raw[p];
        if (c == '\\') {
            // 半个 -escape 现场 缓冲区 边缘 - 停止而不
            // 消耗 `\` 下一个 调用 (有较多) 字节 )会
            // 看到完整的逃生对子,并恢复干净.
            if (p + 1 >= raw.size()) break;
            apply_escape(raw[p + 1], out);
            p += 2;
        } else if (c == '"') {
            // 价值已关闭。 `"` 未来 调用 立即返回(
            // 时间: 幂等 员额 -close).
            ++p;
            *through = p;
            if (closed) *closed = true;
            return true;
        } else {
            out.push_back(c);
            ++p;
        }
    }
    *through = p;
    return false;
}

std::optional<std::string>
sniff_string_progressive(std::string_view raw, std::string_view key) {
    auto off = locate_string_value(raw, key);
    if (!off) return std::nullopt;
    return decode_string_from(raw, *off);
}

bool
ended_inside_string(std::string_view raw) noexcept {
    // 状态机器与 close_partial_json '主循环,
    // 减去发射 缓冲区 我们只需要最后的 `in_string` 标志.
    bool in_string = false;
    bool escape    = false;
    for (char c : raw) {
        if (in_string) {
            if (escape)         { escape = false; continue; }
            if (c == '\\')      { escape = true;  continue; }
            if (c == '"')       { in_string = false; continue; }
            continue;
        }
        if (c == '"') in_string = true;
    }
    // 一个落后的孤独 `\` 弦的边缘也是半 -finished
    // 价值( 5) close_partial_json 合成
    // 前先放下它 `"` ); 视之为 "ended inside"
    // 所以打电话的人拒绝同样的救生
    return in_string;
}

} // namespace agentty::tools::util
