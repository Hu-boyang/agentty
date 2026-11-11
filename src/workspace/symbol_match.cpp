// 汉代 ⁇ (Hand-written 符号)配对者:旧的std::
// regex套装的8个模式匹配,顺序相同(第一对局胜出),没有后路引擎.
//
// 每一个模式都是 ^ -anchored ,所以每次检查
// 都是从线路开始的向前走。C函数模式是昂贵的;
// 这里由左侧单键决定。 -to-right 扫描而不是回溯
// 搜索。
#include "agentty/workspace/symbols.hpp"

#include <string_view>

namespace agentty {
namespace {

using sv = std::string_view;

constexpr bool is_word(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9') || c == '_';
}
// \s 在 ECMAScript 中: 空格,\t\n\v\f\r。
constexpr bool is_space(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

std::size_t skip_space(sv s, std::size_t i) noexcept {
    while (i < s.size() && is_space(s[i])) ++i;
    return i;
}
// 一个或多个\s; 如果没有, 则npos。
std::size_t need_space(sv s, std::size_t i) noexcept {
    const std::size_t j = skip_space(s, i);
    return j == i ? sv::npos : j;
}
// \w+ 起始于 i: 返回单词( 如果无则为空)。
sv word_at(sv s, std::size_t i) noexcept {
    std::size_t j = i;
    while (j < s.size() && is_word(s[j])) ++j;
    return s.substr(i, j - i);
}
// `kw` 之后我有一个非 -word 字符( 或结束)。 返回结束或npos。
std::size_t keyword(sv s, std::size_t i, sv kw) noexcept {
    if (s.substr(i, kw.size()) != kw) return sv::npos;
    return i + kw.size();
}

// 关键词后:\s+(\w+). 注意类似正则x的 `class\s+(\w+)` 也接受
// `classes foo` 没有 : `class` 一定要跟随\s,所以 `classes`
// 失败于\s+。 keyword() 不检查边界; need_space ()确实如此。
sv name_after(sv s, std::size_t i) noexcept {
    if (i == sv::npos) return {};
    const std::size_t j = need_space(s, i);
    if (j == sv::npos) return {};
    return word_at(s, j);
}

// 尝试每个关键字 i; 第一个 `\s+(\w+)` 匹配的胜者, 按顺序。
template <std::size_t N>
sv first_kw(sv s, std::size_t i, const sv (&kws)[N]) noexcept {
    for (const sv kw : kws)
        if (auto w = name_after(s, keyword(s, i, kw)); !w.empty()) return w;
    return {};
}

// 1.  ⁇ *(?:阶级) | 结构 |枚举 (?:\s+类)? | 工会 | 命名空间)\s+(\w+)
sv p_c_type(sv s) noexcept {
    const std::size_t i = skip_space(s, 0);
    static constexpr sv kws[] = {"class", "struct"};
    if (auto w = first_kw(s, i, kws); !w.empty()) return w;
    // 枚举 (?:\s+类)\s+(\w+):正则ex尝试 "enum class X" 首先,
    // 然后回到 "enum X" (它捕捉到) "class" 费用 `enum class`
    // 之后没有名字。
    if (const auto e = keyword(s, i, "enum"); e != sv::npos) {
        if (const auto sp = need_space(s, e); sp != sv::npos) {
            if (auto w = name_after(s, keyword(s, sp, "class")); !w.empty()) return w;
            if (auto w = word_at(s, sp); !w.empty()) return w;
        }
    }
    static constexpr sv rest[] = {"union", "namespace"};
    return first_kw(s, i, rest);
}

// 2. + + = * typedef\s+.+?\s+(\w+)\s*;
// 惰性 + ? 然后\s+(\w+)\s *; : 首次 位置 pQQStart+1 ,
// 其中\s+ (\w+)\s*; 匹配。 单词是贪婪的\w+,
// 但必须后面是\s* ; 所以它是全部的单词字符。
sv p_typedef(sv s) noexcept {
    std::size_t i = skip_space(s, 0);
    const auto k = keyword(s, i, "typedef");
    if (k == sv::npos) return {};
    if (k >= s.size() || !is_space(s[k])) return {};
    // 匹配必须在\s* 中结束; 因此关键词后没有 ';' 的行
    // 永远无法匹配。 在搜索前, 几乎拒绝 O(n) 中的每行。
    if (s.find(';', k) == sv::npos) return {};
    // regex 是类型def +.+?
    // \s+(\w+)\s*;引擎先尝试贪婪者 \s+ LONGEST 首先,
    // 然后对每个长度 惰性 .+? SHORTEST 首先,
    // 然后是第二个\s+ 时间最长,然后是(\w+)时间最长。按字面顺序重播;
    // 每一步是 O(1),所以整个过程在实践上保持线性。
    std::size_t run_end = k;
    while (run_end < s.size() && is_space(s[run_end])) ++run_end;
    for (std::size_t a = run_end; a > k; --a) {          // 首个\s+ 结束
        for (std::size_t p = a + 1; p <= s.size(); ++p) { // 结束 .+ ? (% 1 字符)
            if (p >= s.size() || !is_space(s[p])) continue;
            // 第二\s+:贪婪,最长的先,至少一个.
            std::size_t q_max = p;
            while (q_max < s.size() && is_space(s[q_max])) ++q_max;
            for (std::size_t q = q_max; q > p; --q) {
                // (\w+) 最长的第一, 至少一个。
                std::size_t w_max = q;
                while (w_max < s.size() && is_word(s[w_max])) ++w_max;
                for (std::size_t w = w_max; w > q; --w) {
                    const std::size_t semi = skip_space(s, w);
                    if (semi < s.size() && s[semi] == ';') return s.substr(q, w - q);
                }
            }
        }
    }
    return {};
}

// 3. ^[\w:][\w\s\*\&<>:]*?\s(\w+)\s*\([^;]*?(?:\)\s*\{|\)\s*$)
//
// 结构:允许字符 H = [\w\s ⁇ ] (第一个 [\w:]), 后为一个 xs,
// 然后为 (\w+), 然后为\s *, 然后为。 '(' 编辑 末尾.
//
// 末尾 `\([^;]*?(?:\)\s*\{|\)\s*$)` 继承iff,
// 在'('之后, 有一个')' 可以在不跨越';
// '的情况下到达, 后面是\s ⁇ , 或者由\s * 到达终点。 这只取决于'(' 的位置,而不是名称。
//
// 电话 惰性 头指正格克斯尝试 SHORTEST 先头 i.e
// 左边的H -run 其后面的\w+( 粗略, 所有单词字符) 后面
// 的\s *( 带有好字) 末尾。因为 `\w+` 贪婪,
// 然后需要\s* (, 名称总是在\s* 之前运行的最大化单词 ) (。
// 回溯到一个较短的\w+ 永远不会有帮助( 单词的前缀后面是单词字符,
// 而不是\s 或) '(').
bool good_tail(sv s, std::size_t open) noexcept {
    // ? 懒惰,然后\\\\ \\\}\\\%\\\=\\\$
    for (std::size_t q = open + 1; q < s.size(); ++q) {
        if (s[q] == ';') return false;
        if (s[q] != ')') continue;
        const std::size_t r = skip_space(s, q + 1);
        if (r == s.size() || s[r] == '{') return true;
    }
    return false;
}
constexpr bool is_head(char c) noexcept {
    return is_word(c) || is_space(c) || c == '*' || c == '&' || c == '<'
        || c == '>' || c == ':';
}
sv p_c_function(sv s) noexcept {
    if (s.empty() || !(is_word(s[0]) || s[0] == ':')) return {};
    // 位置 1. . 在 is_head 持有时位于头部运行中。
    // 名称前的\\ 可能是带有 s[p] a 空格和 s[1.
    // p-1] 的所有头字符(\s本身也是头字符) 的任何位置 p% 1。
    for (std::size_t p = 1; p < s.size(); ++p) {
        if (!is_head(s[p - 1]) && p - 1 >= 1) return {};   // 头部在p之前断裂
        if (!is_space(s[p])) continue;
        const std::size_t w = p + 1;
        const sv name = word_at(s, w);
        if (name.empty()) continue;
        const std::size_t open = skip_space(s, w + name.size());
        if (open < s.size() && s[open] == '(' && good_tail(s, open)) return name;
    }
    return {};
}

// 4. QQs*(?:def|类)\s+(\w+)
sv p_python(sv s) noexcept {
    static constexpr sv kws[] = {"def", "class"};
    return first_kw(s, skip_space(s, 0), kws);
}

// 5. QQs*(?: export\s+)? (?: async\s+?)? (或: 函数) | 类 | 康斯特语Name | 开始 | 瓦尔省 | 类型 | 接口)\s+(\w+)
sv p_js(sv s) noexcept {
    static constexpr sv kws[] = {"function", "class", "const", "let", "var",
                                 "type", "interface"};
    const std::size_t i0 = skip_space(s, 0);
    // 可选团体被试现-first,与regex顺序相匹配.
    const auto try_from = [&](std::size_t i) -> sv {
        // (?:async\s+)? (?) ? (?: async&s+) (英语:
        if (const auto a = keyword(s, i, "async"); a != sv::npos)
            if (const auto sp = need_space(s, a); sp != sv::npos)
                if (auto w = first_kw(s, sp, kws); !w.empty()) return w;
        return first_kw(s, i, kws);
    };
    if (const auto e = keyword(s, i0, "export"); e != sv::npos)
        if (const auto sp = need_space(s, e); sp != sv::npos)
            if (auto w = try_from(sp); !w.empty()) return w;
    return try_from(i0);
}

// 6. ^func(?\s ⁇ ([^] ⁇ )])?\s+(\w+)
sv p_go_func(sv s) noexcept {
    const auto f = keyword(s, 0, "func");
    if (f == sv::npos) return {};
    // 接收者先为\ s ⁇ ( [^] )\\\\ \\\,然后为\ s+(\w+)
    if (const auto sp = need_space(s, f); sp != sv::npos && sp < s.size() && s[sp] == '(') {
        const auto close = s.find(')', sp + 1);
        if (close != sv::npos)
            if (auto w = name_after(s, close + 1); !w.empty()) return w;
    }
    return name_after(s, f);
}

// 7. ^type\s+(\w+)
sv p_go_type(sv s) noexcept { return name_after(s, keyword(s, 0, "type")); }

// 8. QQs*(?:pub(?\(^))?\s+)? (?:fn) | 结构 |枚举| 特性 | 类型 | 调 | 康斯特语Name | 静态)\s+(\w+)
sv p_rust(sv s) noexcept {
    static constexpr sv kws[] = {"fn", "struct", "enum", "trait", "type", "mod",
                                 "const", "static"};
    const std::size_t i0 = skip_space(s, 0);
    if (const auto p = keyword(s, i0, "pub"); p != sv::npos) {
        // 酒吧(...) ? 然后是\s+
        std::size_t after = p;
        if (after < s.size() && s[after] == '(') {
            const auto close = s.find(')', after + 1);
            // 与组:需要\s+ 后')'.
            if (close != sv::npos)
                if (const auto sp = need_space(s, close + 1); sp != sv::npos)
                    if (auto w = first_kw(s, sp, kws); !w.empty()) return w;
        }
        if (const auto sp = need_space(s, p); sp != sv::npos)
            if (auto w = first_kw(s, sp, kws); !w.empty()) return w;
    }
    return first_kw(s, i0, kws);
}

}  // 命名空间结束。

std::string_view symbol_on_line(std::string_view line) noexcept {
    // 甚长的行是精密化或生成的代码,而不是声明,
    // 而匹配者除了线性工作之外唯一可以做的工作.
    if (line.size() > 512) return {};
    // 注释 -only 线条被跳过,所以 `// fn foo()` 例子不算数。
    std::size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '/') return {};
    // 廉价的拒绝:每个模式都需要一个字Char。
    using F = sv (*)(sv) noexcept;
    static constexpr F kMatchers[] = {p_c_type, p_typedef, p_c_function, p_python,
                                      p_js, p_go_func, p_go_type, p_rust};
    for (const F f : kMatchers)
        if (const sv w = f(line); !w.empty()) return w;
    return {};
}

}  // namespace agentty
