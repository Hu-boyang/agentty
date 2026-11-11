// complexity.cpp —转-complexity分类器（参见complexity.hpp)。
//
// ADDITIVE FEATURE SCORE，不是关键字查找。
// 三个正交信号族各自贡献权重；总和被划分为一个等级。这是故意设计的一个微小的线性模型，
// 具有 hand-set 权重——没有 ML 运行时，纯粹的，分配-light，
// 确定性的，sub-microsecond——但它是 GENERALISES，
// 而查找表却不能：
//
//   ·STRUCTURAL特征是语言-agnostic(枚举问、连词
//     以及子句密度、代码-token密度、问题形状、字形长度）。
//     请求的复杂性主要在于其STRUCTURE，而不是其动词，
//     因此这些在任何语言中都承载着大部分的权重和工作。
//   ·LEXICAL特征是跨越主要语言的加权关键字集
//     (en/es/fr/de/pt/it+一些CJK标记)。关键词ADD证据；没有单一命中是硬覆盖，
//     因此 "design system button" 不再单独对单词
//     "design" 强制使用 Complex。
//   ·MORPHOLOGICAL特征捕获令牌-shape品种(散文与标识符
//     与路径）—"this touches code structure"的代理。
//
// 结构保守：标准是分数带在零附近，平局向上突破（在
// -thinking以下的硬转弯比在-thinking以上的廉价转弯花费更多）。
//

#include "agentty/domain/complexity.hpp"
#include "agentty/domain/smart_tuning.hpp"

#include <array>
#include <cctype>
#include <cstdint>

namespace agentty::smart {

namespace {

// ── Lexing primitives (ASCII-lowercase; UTF-8 continuation bytes pass through
//    未受影响，因此多字节脚本仍然存在）────────────────────────────────────
constexpr char lower(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
}

bool has(std::string_view hay_lower, std::string_view needle) noexcept {
    if (needle.empty() || needle.size() > hay_lower.size()) return false;
    for (std::size_t i = 0; i + needle.size() <= hay_lower.size(); ++i) {
        std::size_t j = 0;
        for (; j < needle.size(); ++j)
            if (hay_lower[i + j] != needle[j]) break;
        if (j == needle.size()) return true;
    }
    return false;
}

// 空白-run字数(ASCII-segmented语言)。
std::size_t word_count(std::string_view s) noexcept {
    std::size_t n = 0;
    bool in = false;
    for (char c : s) {
        const bool ws = (c == ' ' || c == '\t' || c == '\n' || c == '\r');
        if (!ws && !in) { ++n; in = true; }
        else if (ws) in = false;
    }
    return n;
}

// UTF-8 字形计数（仅前导 字节）— 脚本 -agnostic 长度
// 不会像 字节 长度那样超过 -count 多字节字符。
std::size_t glyph_count(std::string_view s) noexcept {
    std::size_t n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++n;   // 不是延续字节
    return n;
}

// 加权词法信号集合。每项为 (needle, weight)，支持多语言。
//    哪里便宜；即使未命中，结构特征也能实现非 -English 转弯。 ────────
//    ────────────────────────────────────────────
struct Term { std::string_view s; int w; };

// "Hard"词汇——设计/调试/cross-cutting/multi-step意图。
constexpr std::array kHard = {
    // 英语
    Term{"architect", 3},  Term{"design", 2},      Term{"redesign", 3},
    Term{"refactor", 2},   Term{"debug", 2},       Term{"root cause", 3},
    Term{"why ", 2},       Term{" why", 2},        Term{"trade-off", 2},
    Term{"tradeoff", 2},   Term{"strategy", 2},    Term{"approach", 1},
    Term{"end to end", 3}, Term{"end-to-end", 3},  Term{"across the", 2},
    Term{"investigate", 2},Term{"diagnose", 2},    Term{"optimize", 2},
    Term{"optimise", 2},   Term{"race condition", 3}, Term{"deadlock", 3},
    Term{"compare", 2},    Term{"evaluate", 2},    Term{"review", 1},
    Term{"state of the art", 3}, Term{"deep", 1},  Term{"implement all", 2},
    Term{"rewrite", 2},    Term{"migrate", 2},     Term{"whole", 1},
    Term{"entire", 1},     Term{"analyze", 2},     Term{"analyse", 2},
    // 西班牙语/葡萄牙语
    Term{"por qu", 2},     Term{"porqu", 2},       Term{"disea", 2},
    Term{"refactor", 2},   Term{"arquitect", 3},   Term{"investiga", 2},
    Term{"depura", 2},     Term{"optimiza", 2},
    // 法语
    Term{"pourquoi", 2},   Term{"concevoir", 2},   Term{"architectur", 3},
    Term{"refactoris", 2}, Term{"dboguer", 2},     Term{"optimis", 2},
    // 德语
    Term{"warum", 2},      Term{"entwerf", 2},     Term{"architektur", 3},
    Term{"refaktor", 2},   Term{"debuggen", 2},    Term{"optimier", 2},
    // 意大利语
    Term{"perch", 2},      Term{"progetta", 2},    Term{"architett", 3},
};

// "Trivial"致谢/一个-word祈使句（与WHOLE
// 修剪转弯相匹配，因此它们不能在较长的句子内假-fire）。
constexpr std::array kTrivialExact = {
    // 英语
    std::string_view{"yes"}, std::string_view{"no"}, std::string_view{"ok"},
    std::string_view{"okay"}, std::string_view{"yep"}, std::string_view{"yeah"},
    std::string_view{"thanks"}, std::string_view{"thank you"}, std::string_view{"ty"},
    std::string_view{"go"}, std::string_view{"go ahead"}, std::string_view{"do it"},
    std::string_view{"run it"}, std::string_view{"continue"}, std::string_view{"proceed"},
    std::string_view{"commit"}, std::string_view{"commit it"}, std::string_view{"push"},
    std::string_view{"stop"}, std::string_view{"cancel"}, std::string_view{"sure"},
    std::string_view{"nice"}, std::string_view{"perfect"}, std::string_view{"great"},
    std::string_view{"lgtm"}, std::string_view{"undo"}, std::string_view{"retry"},
    std::string_view{"again"}, std::string_view{"next"},
    // 其他语言(常见ack)
    std::string_view{"si"}, std::string_view{"oui"}, std::string_view{"ja"},
    std::string_view{"gracias"}, std::string_view{"merci"}, std::string_view{"danke"},
    std::string_view{"vale"}, std::string_view{"claro"}, std::string_view{"bien"},
};

// ── 结构特征提取器(所有语言-agnostic) ────────────────────

// 枚举询问：line-leading列表标记("1." "2)" "- " "* ")
// -显式multi-part请求的代理。
std::size_t enumerated_asks(std::string_view s) noexcept {
    std::size_t n = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const bool bol = (i == 0) || s[i - 1] == '\n';
        if (bol) {
            char c = s[i];
            if (c == '-' || c == '*') ++n;
            else if (c >= '1' && c <= '9'
                     && i + 1 < s.size() && (s[i + 1] == '.' || s[i + 1] == ')'))
                ++n;
        }
    }
    return n;
}

// Clause/conjunction密度：sentence/clause分隔符加上一些常见的并列连词。
// 更多条款 ⇒ 更多 sub-tasks。计数 ASCII 和
// 几个 CJK 分隔符。
std::size_t clause_markers(std::string_view low) noexcept {
    std::size_t n = 0;
    for (std::size_t i = 0; i < low.size(); ++i) {
        char c = low[i];
        if (c == ';' || c == ':') ++n;
        // CJK逗号(、=E3 80 81)/表意句号(。=E3 80 82)
        else if (static_cast<unsigned char>(c) == 0xE3 && i + 2 < low.size()
                 && static_cast<unsigned char>(low[i + 1]) == 0x80
                 && (static_cast<unsigned char>(low[i + 2]) == 0x81
                     || static_cast<unsigned char>(low[i + 2]) == 0x82)) { ++n; i += 2; }
    }
    // 并列连词(word-ish、多语言、廉价子串)。
    for (auto w : {std::string_view{" and "}, std::string_view{" then "},
                   std::string_view{" also "}, std::string_view{" plus "},
                   std::string_view{" y "},    std::string_view{" et "},
                   std::string_view{" und "},  std::string_view{" e "}})
        for (std::size_t i = 0; i + w.size() <= low.size(); ++i)
            if (low.compare(i, w.size(), w) == 0) { ++n; i += w.size() - 1; }
    return n;
}

// Code-token密度：看起来像代码的字符的分数（以十分之为单位）（路径分隔符、
// dotted/underscored标识、括号、camelCase驼峰）。
int code_density_tenths(std::string_view s) noexcept {
    if (s.empty()) return 0;
    std::size_t codey = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '/' || c == '_' || c == '(' || c == ')' || c == '{' || c == '}'
            || c == '[' || c == ']' || c == '<' || c == '>' || c == '=')
            ++codey;
        else if (c == '.' && i + 1 < s.size() && std::isalnum((unsigned char)s[i + 1]))
            ++codey;   // 点式标识，不是刑期
        else if (c >= 'A' && c <= 'Z' && i > 0
                 && std::islower((unsigned char)s[i - 1]))
            ++codey;   // camelCase驼峰
    }
    return static_cast<int>(codey * 10 / s.size());
}

} // 命名空间结束。

ComplexityScore classify_score(std::string_view text, int complex_min) noexcept {
    // 修剪。
    std::size_t b = 0, e = text.size();
    while (b < e && std::isspace((unsigned char)text[b])) ++b;
    while (e > b && std::isspace((unsigned char)text[e - 1])) --e;
    std::string_view trimmed = text.substr(b, e - b);

    if (trimmed.empty())
        return {Complexity::Trivial, -100, 100};

    std::string low;
    low.reserve(trimmed.size());
    for (char c : trimmed) low.push_back(lower(c));

    const std::size_t words  = word_count(trimmed);
    const std::size_t glyphs = glyph_count(trimmed);
    const bool has_q         = trimmed.find('?') != std::string_view::npos
                            || has(low, "\xEF\xBC\x9F");   // 全角？
    const std::size_t enums  = enumerated_asks(trimmed);
    const std::size_t clauses= clause_markers(low);
    const int code_tenths    = code_density_tenths(trimmed);

    // 简单请求的快速路径：整个短轮次是任意语言中已知的确认语，
    //    不是疑问句。这是一条硬性规则——确认就是确认。 ──────────
    if (words <= 3 && glyphs <= 24 && !has_q) {
        for (auto t : kTrivialExact)
            if (low == t)
                return {Complexity::Trivial, -100, 100};
    }

    // ── 附加分数 结构主导; 词汇和形态精细化. ──
    int score = 0;

    // 尺寸（字形-based⇒脚本-agnostic)。饱和带。
    if (glyphs >= 400)      score += 4;
    else if (glyphs >= 220) score += 3;
    else if (glyphs >= 120) score += 2;
    else if (glyphs >= 60)  score += 1;
    else if (glyphs <= 18)  score -= 2;   // 非常短⇒可能简单

    // 字数统计(English-ish)加强了存在空格的地方的大小。
    if (words >= 45)      score += 2;
    else if (words >= 24) score += 1;
    else if (words <= 5)  score -= 1;

    // 显式的multi-part结构是最强的单个信号。
    if (enums >= 3)      score += 4;
    else if (enums == 2) score += 2;
    else if (enums == 1) score += 1;

    // 子句密度⇒倍数子-tasks。
    if (clauses >= 4)      score += 3;
    else if (clauses >= 2) score += 2;
    else if (clauses == 1) score += 1;

    // 代码-token密度⇒触及结构（但单独的代码墙
    // 并不一定*难*思考，因此限制其贡献）。
    if (code_tenths >= 3)      score += 2;
    else if (code_tenths >= 1) score += 1;

    // 问题形状：问题通常是explain/why工作-轻度碰撞。
    if (has_q) score += 1;

    // 词汇证据（加权的、多语言的、附加的——绝不是覆盖）。
    // 限制总词汇贡献，
    // 因此关键字-stuffed短转弯不能仅在词汇上直接跃至顶部。
    int lex = 0;
    for (auto t : kHard)
        if (has(low, t.s)) { lex += t.w; if (lex >= 6) { lex = 6; break; } }
    score += lex;

    // 按阈值划分等级，并记录距最近边界的余量。Complex
    //    剪切是用户-tunable(设置行"smart.complex_threshold"
    //    或AGENTTY_SMART_COMPLEX_THRESHOLD)：
    //    标准是其正下方的带，简单化其上或下方的所有内容。关系通过边界位置
    //    向上断裂。传入而不是从此处的环境中读取 - 请参阅声明。 ──────
    //    ──────────────────────────────────
    const int kComplexMin  = complex_min;                  // >= 这个 ⇒ 复杂
    const int kSimpleMax   = kComplexMin - 3;               // <= 这个 ⇒ 简单
                                                            // (标准是2
                                                            //  -wide之间的频带)

    Complexity tier;
    int margin;
    if (score <= kSimpleMax) {
        tier   = Complexity::Simple;
        margin = kSimpleMax - score;                 // 低于边界多远
    } else if (score < kComplexMin) {
        tier   = Complexity::Standard;
        margin = std::min(score - kSimpleMax, kComplexMin - score);
    } else {
        tier   = Complexity::Complex;
        margin = score - kComplexMin;
    }
    return {tier, score, margin};
}

Complexity classify_complexity(std::string_view text, int complex_min) noexcept {
    return classify_score(text, complex_min).tier;
}

bool is_routing_correction(std::string_view text) noexcept {
    // 仅小写开头——情绪存在于 follow-up 的开始处，
    // 并且有界扫描使该情绪在热缩减路径上保持便宜。
    std::string low;
    low.reserve(48);
    for (char c : text) {
        if (low.size() >= 48) break;
        low.push_back((c >= 'A' && c <= 'Z') ? char(c + 32) : c);
    }
    auto starts = [&](std::string_view p){ return low.rfind(p, 0) == 0; };
    auto has    = [&](std::string_view p){
        return low.find(p) != std::string_view::npos; };

    // 明确的赞扬否决了彻底的纠正("actually that's perfect")。
    const bool positive =
           has("perfect") || has("great") || has("thanks") || has("thank you")
        || has("looks good") || has("lgtm") || has("nice") || has("works now")
        || has("that works") || has("awesome") || has("exactly");
    if (positive) return false;

    // 明确的不满——匹配开场白中出现的任何地方。
    const bool hard_negative =
           starts("that's wrong") || starts("thats wrong")
        || starts("that's not")  || starts("thats not")
        || starts("that broke")  || starts("you broke")
        || starts("undo") || starts("revert") || starts("not quite")
        || has("doesn't work") || has("does not work") || has("didn't work")
        || has("still broken") || has("still fail") || has("still doesn't")
        || has("that's incorrect") || has("is wrong") || has("was wrong");
    if (hard_negative) return true;

    // 软开场数具有确凿的负面提示的 仅 — 因此，
    // 裸露的 "actually, also add tests" (添加剂) 或
    // "no worries" 不会对先验产生影响。 "wrong" 是故意将 不 软开启（
    // "wrong file" 是重定向）；真正的错误-ness在hard_negative上面。
    const bool soft_opener =
           starts("no,") || starts("no ") || low == "no" || starts("nope")
        || starts("actually");
    const bool soft_negative_cue =
           has(" not ") || has("n't") || has("broke") || has("fail")
        || has("error") || has("still") || has("instead") || has("bug");
    return soft_opener && soft_negative_cue;
}

bool is_continuation_cue(std::string_view text) noexcept {
    // 修剪+小写（短）转弯。只有 ack-length 才符合资格——
    // 任何不再携带自己信号的信号都由 classify_score 处理。
    std::size_t b = 0, e = text.size();
    while (b < e && std::isspace((unsigned char)text[b])) ++b;
    while (e > b && std::isspace((unsigned char)text[e - 1])) --e;
    std::string_view t = text.substr(b, e - b);
    if (t.empty() || t.size() > 24) return false;
    std::string low;
    low.reserve(t.size());
    for (char c : t) low.push_back(lower(c));
    // RESUME 起作用的命令（相对于 终端 应答，如 "thanks"/"lgtm"）。
    // "commit"/"push"是故意缺席的：它们以廉价的机械动作
    // 结束任务，该机械动作是Trivial正确的路线。
    for (std::string_view cue : {
             std::string_view{"continue"}, std::string_view{"go on"},
             std::string_view{"keep going"}, std::string_view{"go ahead"},
             std::string_view{"do it"}, std::string_view{"proceed"},
             std::string_view{"retry"}, std::string_view{"try again"},
             std::string_view{"again"}, std::string_view{"next"},
             std::string_view{"run it"}, std::string_view{"finish it"},
             std::string_view{"keep at it"}})
        if (low == cue) return true;
    return false;
}

} // namespace agentty::smart
