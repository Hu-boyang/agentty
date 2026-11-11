#pragma once
// agentty::i18n —CLDR复数类别选择。
//
// ── 为何这不是后缀规则 ────────────────────────────────────────
//
// 英语有两种形式并且人们达到`n == 1 ? "file" : "files"`。
// 对于我们提供的一半以上的语言来说，这是错误的。俄语：
//
//     1 файл 1 以 1 结尾，
//     但不是 11 2 файла 少数以 2-4 结尾，
//     但不是 12-14 5 файлов
//     很多其他的 21 файл 又是 1——决定的是 最后 DIGIT
//
// 因此，在声称有二十种语言的产品中，`"%d file(s)"`
// 不是可接受的答案，"add an s"也不是。
// 类别是语言的属性，它们的唯一正确来源是 CLDR。
//
// ── 为什么一个手-written开关而不是规则评估员 ───────────────────
//
// gettext将规则存储为C表达式并将其`eval`
// 存储在运行时处。这是一个 解析器 和一个解释器，
// 从磁盘上的文件中读取数据，以回答整个语言集中有五种可能形状的问题。
// 我们写下这五个形状：
//
//     一个 zh-CN ja ko id 完全没有复数区别 两个 en
//     de nl sv da no it es pt-BR
//     hu tr fr SLAVIC3 ru uk 1 / 2-4 / 休息，
//     不包括青少年 POLISH3 pl 形状相同，不同 `many`
//     CZECH3 cs 1 / 2-4 / 休息，NO 青少年排除
//
// 下面的每个规则都是从CLDR图表（cldr.unicode.org，
// 语言复数规则，基数）转录的，并且每个规则都携带其来自的
// 规则文本，因此读者可以在不离开文件的情况下对照源检查代码。
// 对所有 20 种语言进行了超过 0..1000 次详尽的测试，
// 这比信任我们无法检查的依赖项更强的验证。
//
// INTEGERS 仅。 CLDR 区分 1 和 1.0（`v` 和 `f`
// 操作数 - "1,0 dag" 在南非荷兰语中是 `other`，
// 而 "1 dag" 是 `one`）。 agentty 计算事物：回合、
// 文件、令牌、匹配。不存在将分数复数的路径，因此建模 `v`/`f`
// 将是假装严格的未使用代码。如果出现小数计数，它就会出现在这里，
// 下面的 static_assert 是提醒。

#include <cstdint>

#include "agentty/i18n/i18n.hpp"

namespace agentty::i18n {

// 我们使用的CLDR类别。 `zero` 和 `two` 存在于 CLDR（
// 阿拉伯语、威尔士语）中，并且故意不存在：我们 20 种语言中没有一种语言有它们，
// 而任何东西都无法产生的枚举器对于下一个添加语言的人来说是
// 一个陷阱。
enum class Plural : std::uint8_t { One, Few, Many, Other };

[[nodiscard]] constexpr std::string_view key_of(Plural p) noexcept {
    switch (p) {
        case Plural::One:  return "one";
        case Plural::Few:  return "few";
        case Plural::Many: return "many";
        case Plural::Other: return "other";
    }
    return "other";
}

// 五种规则形状。对每种语言进行命名而不是内联，
// 以便 "which languages share a rule"
// 通过读取一个表来响应，并且添加一种语言是该表中的一行而不是一个新分支。
enum class Rule : std::uint8_t { One_, Two, Slavic3, Polish3, Czech3 };

[[nodiscard]] constexpr Rule rule_of(Lang l) noexcept {
    switch (l) {
        // ── 无复数区分 ────────────────────────────────────────
        // CLDR：对于每n个"other"。东亚语言不会对数字名词
        // 进行屈折变化，而印度尼西亚语会重复使用 (buku-buku)
        // 而不是屈折变化，这是一种单词选择，而不是格式化
        // 程序选择的形式。
        case Lang::zh_CN: case Lang::ja: case Lang::ko: case Lang::id:
            return Rule::One_;

        // ── 两种表格 ────────────────────────────────────────────────────
        // CLDR:一->`i = 1 and v = 0`;其他 -> 其他一切。
        //
        // 法语在这里并且是奇数：CLDR为`one`给出fr
        // `i = 0,1`，所以0采用SINGULAR("0 fichier"，
        // 而不是"0 fichiers")。在 select() 中处理，
        // 而不是通过给 fr 自己的规则来处理，因为这是一个额外的比较，
        // 并且单一语言的第六个规则形状比注释更糟糕。
        case Lang::en: case Lang::de: case Lang::nl: case Lang::sv:
        case Lang::da: case Lang::no: case Lang::it: case Lang::es:
        case Lang::pt_BR: case Lang::hu: case Lang::tr: case Lang::fr:
            return Rule::Two;

        // ── 斯拉夫语,三种形式 ──────────────────────────────────────────
        case Lang::ru: case Lang::uk: return Rule::Slavic3;
        case Lang::pl:                return Rule::Polish3;
        case Lang::cs:                return Rule::Czech3;
    }
    return Rule::Two;
}

// 语言`l`中`n`的类别。
//
// NEGATIVES取绝对值。 CLDR 的操作数是根据
// 源数字的绝对值定义的，无论如何，这个 代码库 中的计数永远
// 不会是负数——但是 -1 默默地降落在 `other`
// 中将是一个仅在错误路径上显示的错误，这是最糟糕的地方。
[[nodiscard]] constexpr Plural select(Lang l, long long n) noexcept {
    const unsigned long long v =
        n < 0 ? static_cast<unsigned long long>(-(n + 1)) + 1ull
              : static_cast<unsigned long long>(n);

    const unsigned long long mod10  = v % 10;
    const unsigned long long mod100 = v % 100;

    switch (rule_of(l)) {
        case Rule::One_:
            return Plural::Other;

        case Rule::Two:
            // fr:CLDR `i = 0,1`->一。其他一切：`i = 1`。
            if (l == Lang::fr) return (v <= 1) ? Plural::One : Plural::Other;
            return (v == 1) ? Plural::One : Plural::Other;

        case Rule::Slavic3:
            // 俄罗斯，英国。 CLDR:
            //   一-> v = 0且i % 10 = 1且i % 100!= 11 少数
            //   -> v = 0且i % 10 = 2..4且i % 100!= 12..
            //   14 许多-> v = 0且(i % 10 = 0或i % 10 = 5..9
            //                      or i % 100 = 11..14)
            if (mod10 == 1 && mod100 != 11) return Plural::One;
            if (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14))
                return Plural::Few;
            return Plural::Many;

        case Rule::Polish3:
            // pl。与上面的斯拉夫语对相同的 一个 和 FEW；
            // CLDR 的 `many` 拼写不同，
            // 但涵盖相同的整数，因此存在拆分是为了文档
            // 而不是行为。保持分开是因为添加斯拉夫语言
            // 的 下一个 人需要看到它们不会自动相同。
            if (v == 1) return Plural::One;
            if (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14))
                return Plural::Few;
            return Plural::Many;

        case Rule::Czech3:
            // c. CLDR：
            //   一个-> i = 1且v =
            //   0 几个-> i = 2..
            //   4且v = 0 其他-> 其他所有
            //
            // NOTE 与斯拉夫语对的区别，这是人们会犯错的一个：捷克
            // 语的 `few` 是 2、3、4 仅——不是 22、23，
            // 24.没有模数。 22 在捷克语中是 `other`，在捷克语中是 `few`
            // 俄语，并且共享的实现使其中之一出错。
            if (v == 1) return Plural::One;
            if (v >= 2 && v <= 4) return Plural::Few;
            return Plural::Other;
    }
    return Plural::Other;
}

// 语言实际上可以产生哪些类别。
//
// lint 以两种方式使用这一点：它需要的 目录 MISSING
// 类别是不完整的（没有 `many` 的俄语不能数到五），
// 而它无法产生的 目录 SUPPLYING 是翻译者误解
// 了 模式定义（日语 `few` 永远不会被读取）。
// 两者都值得失败；第二个是否则会悄无声息地腐烂的。
[[nodiscard]] constexpr bool has_category(Lang l, Plural p) noexcept {
    switch (rule_of(l)) {
        case Rule::One_:    return p == Plural::Other;
        case Rule::Two:     return p == Plural::One || p == Plural::Other;
        case Rule::Slavic3:
        case Rule::Polish3: return p == Plural::One || p == Plural::Few
                                || p == Plural::Many;
        case Rule::Czech3:  return p == Plural::One || p == Plural::Few
                                || p == Plural::Other;
    }
    return p == Plural::Other;
}

// ── 编译-time 证据,例如: CLDR 本身出版 ────────────
//
// 这些不是测试套件（位于i18n_test.cpp
// 并扫描0..1000）。它们是少数将规则彼此区分开来的案例，
// 并断言规则是写在哪里的，因此两者不会发生偏差。

// 英文：无聊的一个，包含在内所以表格有基线。
static_assert(select(Lang::en, 1) == Plural::One);
static_assert(select(Lang::en, 0) == Plural::Other);
static_assert(select(Lang::en, 2) == Plural::Other);

// 法语：0是SINGULAR。 "0 fichier"。
static_assert(select(Lang::fr, 0) == Plural::One);
static_assert(select(Lang::fr, 1) == Plural::One);
static_assert(select(Lang::fr, 2) == Plural::Other);

// 日语：一种形式，始终。
static_assert(select(Lang::ja, 0) == Plural::Other);
static_assert(select(Lang::ja, 1) == Plural::Other);
static_assert(select(Lang::ja, 7) == Plural::Other);

// 俄语：青少年是陷阱。 11 是 `many`，而不是 `one`，即使它以 1 结尾。
// 21 又是 `one`。
static_assert(select(Lang::ru, 1)  == Plural::One);
static_assert(select(Lang::ru, 2)  == Plural::Few);
static_assert(select(Lang::ru, 5)  == Plural::Many);
static_assert(select(Lang::ru, 11) == Plural::Many);
static_assert(select(Lang::ru, 12) == Plural::Many);
static_assert(select(Lang::ru, 21) == Plural::One);
static_assert(select(Lang::ru, 22) == Plural::Few);
static_assert(select(Lang::ru, 25) == Plural::Many);
static_assert(select(Lang::ru, 111) == Plural::Many);
static_assert(select(Lang::ru, 121) == Plural::One);

// 乌股形状。
static_assert(select(Lang::uk, 1)  == Plural::One);
static_assert(select(Lang::uk, 3)  == Plural::Few);
static_assert(select(Lang::uk, 14) == Plural::Many);

// 波兰语。
static_assert(select(Lang::pl, 1)  == Plural::One);
static_assert(select(Lang::pl, 2)  == Plural::Few);
static_assert(select(Lang::pl, 5)  == Plural::Many);
static_assert(select(Lang::pl, 22) == Plural::Few);
static_assert(select(Lang::pl, 12) == Plural::Many);

// 捷克：THE分歧。 22在这里是`other`，在俄语中是`few`。
static_assert(select(Lang::cs, 1)  == Plural::One);
static_assert(select(Lang::cs, 2)  == Plural::Few);
static_assert(select(Lang::cs, 4)  == Plural::Few);
static_assert(select(Lang::cs, 5)  == Plural::Other);
static_assert(select(Lang::cs, 22) == Plural::Other);
static_assert(select(Lang::ru, 22) == Plural::Few);   // 对比，固定

// 负数取大小，因此-1表现为1，而不是偶然落入`other`。
// （被第一个 编译 上的这个断言所吸引：第一个版本声称
// en/-1 是 `other`，这将是错误路径上的无声
// "-1 files"。）
static_assert(select(Lang::ru, -1) == Plural::One);
static_assert(select(Lang::en, -1) == Plural::One);
static_assert(select(Lang::en, -2) == Plural::Other);
static_assert(select(Lang::ru, -11) == Plural::Many);

// 规则可以发出的每个类别都是目录需要携带的类别，反之亦然。断言成本低廉，
// 并且它可以使 has_category 相对于 select()
// 保持诚实。
static_assert(has_category(Lang::ru, Plural::Many));
static_assert(!has_category(Lang::ja, Plural::Few));
static_assert(!has_category(Lang::cs, Plural::Many));
static_assert(has_category(Lang::cs, Plural::Other));
static_assert(!has_category(Lang::ru, Plural::Other));

}  // namespace agentty::i18n
