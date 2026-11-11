// i18n_test——翻译核心.
//
// 有两个东西值得测试 它们在实物上有很大的不同
//
// 电话 PLURAL RULES 是一个外部光谱的复制(
// Q) CLDR )风险并不在于密码在货币下是潜移错误
// 的—— 人类复制规则是不正确的,唯一的辩护是对照同一
// 规则的独立声明检查每个整数。下面的扫描就是这样做的:
// 所有20种语言的0.1000, 相对于规则的再修正
// -derived 从 CLDR 而不是从执行中得出。
//
// 一切,我都会说 ELSE (听着, 回退 标记 解析 ,即时交换)是
// 普通代码,并获得普通案例.
//
// 为何要进行扫荡而不是抽查:俄罗斯的规则是11-14有漏洞,
// 一个有理智的人在11时进行测试,然后在111时错过捷克的测试。
// `few` 2-4 与 3 键 NO 摩杜罗,
// 22岁是 `other` 那边,还有 `few` 俄语中——共享
// 执行得到准确的其中之一正确,而写错误的人选择的抽查会与错误一致.

#include <string>
#include <vector>

#include "agtest.hpp"

#include "agentty/i18n/i18n.hpp"
#include "agentty/i18n/plural.hpp"
#include "agentty/i18n/startup.hpp"

using namespace agentty::i18n;

namespace {

// 来,来,来 INDEPENDENT 发言 CLDR 规则,
// 从已公布的图表而不是从 plural.hpp 如果两者都错误,
// 那么测试是毫无价值的,所以这些是故意用不同的拼写:
// 执行开关在共享规则上 枚举 ,这个切换器按语言进行,
// 并重复。
Plural expected(Lang l, long long n) {
    const auto v   = static_cast<unsigned long long>(n < 0 ? -n : n);
    const auto d10 = v % 10;
    const auto d100 = v % 100;

    switch (l) {
        // 单表
        case Lang::zh_CN: case Lang::ja: case Lang::ko: case Lang::id:
            return Plural::Other;

        // f: i = 0,1 - > 1 个
        case Lang::fr:
            return (v == 0 || v == 1) ? Plural::One : Plural::Other;

        // 两种形式,i=1 - > 1
        case Lang::en: case Lang::de: case Lang::nl: case Lang::sv:
        case Lang::da: case Lang::no: case Lang::it: case Lang::es:
        case Lang::pt_BR: case Lang::hu: case Lang::tr:
            return (v == 1) ? Plural::One : Plural::Other;

        // ru, uk
        case Lang::ru: case Lang::uk:
            if (d100 >= 11 && d100 <= 14) return Plural::Many;
            if (d10 == 1) return Plural::One;
            if (d10 >= 2 && d10 <= 4) return Plural::Few;
            return Plural::Many;

        // pl
        case Lang::pl:
            if (v == 1) return Plural::One;
            if (d100 >= 12 && d100 <= 14) return Plural::Many;
            if (d10 >= 2 && d10 <= 4) return Plural::Few;
            return Plural::Many;

        // cs - 没有modulo, 这是整个问题
        case Lang::cs:
            if (v == 1) return Plural::One;
            if (v >= 2 && v <= 4) return Plural::Few;
            return Plural::Other;
    }
    return Plural::Other;
}

const Lang kAll[] = {
    Lang::en, Lang::zh_CN, Lang::es, Lang::pt_BR, Lang::ja, Lang::de,
    Lang::fr, Lang::ko, Lang::it, Lang::ru, Lang::tr, Lang::id,
    Lang::pl, Lang::nl, Lang::uk, Lang::sv, Lang::cs, Lang::da,
    Lang::no, Lang::hu,
};

}  // 命名空间结束。

TEST_CASE("i18n plural: every language, every n in 0..1000") {
    for (const Lang l : kAll) {
        for (long long n = 0; n <= 1000; ++n) {
            const auto got = select(l, n);
            const auto want = expected(l, n);
            if (got != want) {
                // 报告每种语言的ONCE, 而不是1 000
                // 种相同的失败, 埋葬了语言的破裂。
                INFO("lang " << std::string{english_name(l)} << " n=" << n
                             << " got " << std::string{key_of(got)}
                             << " want " << std::string{key_of(want)});
                CHECK(got == want);
                break;
            }
        }
    }
}

TEST_CASE("i18n plural: the cases that distinguish the rules") {
    // 俄国十几岁,11岁就结束了 不 `one`.
    CHECK(select(Lang::ru, 1)   == Plural::One);
    CHECK(select(Lang::ru, 11)  == Plural::Many);
    CHECK(select(Lang::ru, 21)  == Plural::One);
    CHECK(select(Lang::ru, 111) == Plural::Many);
    CHECK(select(Lang::ru, 121) == Plural::One);

    // 捷克人对俄罗斯人22分 该 分歧 -- -- 一个
    // 执行不能同时起作用,这就是我们没有尝试的对夫妇。
    CHECK(select(Lang::cs, 22) == Plural::Other);
    CHECK(select(Lang::ru, 22) == Plural::Few);

    // 法语:0是单数.
    CHECK(select(Lang::fr, 0) == Plural::One);
    CHECK(select(Lang::en, 0) == Plural::Other);

    // CJK + 印度尼西亚语:一种形式,无论计数.
    for (const Lang l : {Lang::ja, Lang::zh_CN, Lang::ko, Lang::id})
        for (long long n : {0, 1, 2, 5, 11, 100})
            CHECK(select(l, n) == Plural::Other);
}

TEST_CASE("i18n plural: has_category agrees with select") {
    // A类 select() 一定能生产出一个
    // 目录 需要携带,否则用纸需要表格 运行时
    // 绝不读经,但以建筑而非眼睛为凭据。
    for (const Lang l : kAll) {
        bool seen[4] = {};
        for (long long n = 0; n <= 1000; ++n)
            seen[static_cast<std::size_t>(select(l, n))] = true;
        for (const Plural p : {Plural::One, Plural::Few, Plural::Many,
                               Plural::Other}) {
            INFO("lang " << std::string{english_name(l)}
                         << " category " << std::string{key_of(p)});
            CHECK(seen[static_cast<std::size_t>(p)] == has_category(l, p));
        }
    }
}

TEST_CASE("i18n: tag parsing handles what environments actually set") {
    struct Case { const char* in; const char* want; };   // nullptr = refuse
    const Case cases[] = {
        {"en",            "en"},
        {"EN",            "en"},          // 案件-insensitive 数据
        {"de_DE.UTF-8",   "de"},          // 编号 POSIX 形状
        {"de-AT",         "de"},          // 不带 - > 基数
        {"pt-BR",         "pt-BR"},       // 确切地说
        {"pt-PT",         "pt-BR"},       // 足够近了,故意的
        {"zh-CN",         "zh-CN"},
        {"zh",            "zh-CN"},
        {"zh-Hans",       "zh-CN"},
        {"nb-NO",         "no"},          // CLDR 拆分数 nb/nn 我们没有
        {"nn",            "no"},
        {"es_MX@euro",    "es"},          // 已剥除的修改器
        // 传统汉语不是 简化方言。
        // 服务日文 -CN 这里看起来是错的,比英语还糟
        {"zh-TW",         nullptr},
        {"zh-HK",         nullptr},
        {"ar",            nullptr},       // 未发运: 需要bodi
        {"",              nullptr},
        {"klingon",       nullptr},
    };
    for (const auto& c : cases) {
        Lang got{};
        const bool ok = parse_tag(c.in, got);
        INFO("tag '" << std::string{c.in} << "'");
        if (c.want == nullptr) {
            CHECK(!ok);
        } else {
            REQUIRE(ok);
            CHECK(tag_of(got) == std::string_view{c.want});
        }
    }
}

TEST_CASE("i18n: lookup, fallback and the live swap") {
    const char* en = R"({
        "app.title": "Sandbox",
        "pane.read": {"text": "Also readable", "width_hint": 13},
        "turn.count": {"one": "{n} turn", "other": "{n} turns"}
    })";
    const char* ru = R"({
        "app.title": "Песочница",
        "pane.read": {"text": "Также читаемый"},
        "turn.count": {"one": "{n} ход", "few": "{n} хода", "many": "{n} ходов"}
    })";

    REQUIRE(install_catalog(Lang::en, en));
    REQUIRE(install_catalog(Lang::ru, ru));

    REQUIRE(set_active(Lang::en));
    CHECK(t("app.title") == "Sandbox");
    CHECK(t("pane.read") == "Also readable");
    CHECK(format_n("turn.count", 1, {}) == "1 turn");
    CHECK(format_n("turn.count", 2, {}) == "2 turns");

    // 该 LIVE SWAP。一个商店,下一个搜索被翻译
    // ——没有重建,也没有重启。这是设计所依赖的行为。
    REQUIRE(set_active(Lang::ru));
    CHECK(t("app.title") == "Песочница");
    CHECK(format_n("turn.count", 1, {})  == "1 ход");
    CHECK(format_n("turn.count", 2, {})  == "2 хода");
    CHECK(format_n("turn.count", 5, {})  == "5 ходов");
    CHECK(format_n("turn.count", 21, {}) == "21 ход");    // 否 "ходов" 数据
    CHECK(format_n("turn.count", 11, {}) == "11 ходов");  // 少年洞 少年洞

    // 失踪的ID 渲染 AS 该 ID。明显错误的跳动会空白:屏幕读取
    // "no.such.id" 是一个错误报表, 一个空行是一个用户想知道
    // 他们打破了什么。
    CHECK(t("no.such.id") == "no.such.id");

    // 切换到没有语言的语言 目录 保留当前一个,
    // 而不是空白 UI.
    const auto before = active();
    CHECK(!set_active(Lang::hu));
    CHECK(active() == before);

    REQUIRE(set_active(Lang::en));
}

TEST_CASE("i18n: interpolation is named, and survives a typo") {
    const char* en = R"({
        "greet": "Hello {name}, you have {count} messages",
        "braces": "a {{literal}} brace",
        "typo": "total: {tatol}"
    })";
    REQUIRE(install_catalog(Lang::en, en));
    REQUIRE(set_active(Lang::en));

    const Arg args[] = {{"name", "Ayush"}, {"count", "3"}};
    CHECK(format("greet", args) == "Hello Ayush, you have 3 messages");

    // 命名, 不定位: 以不同顺序排列的 相同 参数仍然工作,
    // 也就是让翻译者重新排列句子的属性。
    const Arg rev[] = {{"count", "3"}, {"name", "Ayush"}};
    CHECK(format("greet", rev) == "Hello Ayush, you have 3 messages");

    CHECK(format("braces", {}) == "a {literal} brace");

    // 一个未知的占位符被留下 VERBATIM 而不是被抹去。
    // 擦除它会留下一个没人能追踪的洞的句子; 这一点出现
    // 在屏幕上并被报告。
    const Arg total[] = {{"total", "9"}};
    CHECK(format("typo", total) == "total: {tatol}");
}

TEST_CASE("i18n: every language has a tag, an endonym and a plural rule") {
    // 表按指数排列 枚举 (static_assert 输入 i18n.cpp ),
    // 但仅证明 ORDER。这证明每行都有人 -- -- 空白的本地
    // 地名将 渲染 空的,一个空的 选择面板 仍然选择语言的行。
    for (const Lang l : kAll) {
        INFO("lang " << std::string{english_name(l)});
        CHECK(!tag_of(l).empty());
        CHECK(!endonym(l).empty());
        CHECK(!english_name(l).empty());
        // 语言上的本名 自身 脚本,所以是这里允许
        // 非的字符串 -ASCII 20个名字中的7个是的,
        // 或者我们两次错误地写了英文名字。
        Lang round{};
        REQUIRE(parse_tag(tag_of(l), round));
        CHECK(round == l);
    }
}

TEST_CASE("i18n: completeness is measured against English") {
    const char* en = R"({"a":"A","b":"B","c":"C","d":"D"})";
    const char* de = R"({"a":"A","b":"B"})";
    REQUIRE(install_catalog(Lang::en, en));
    REQUIRE(install_catalog(Lang::de, de));

    CHECK(completeness(Lang::en) == 1.0);
    CHECK(completeness(Lang::de) == 0.5);
    // 语言没有 目录 0,不是错误 -- -- , 选择面板
    // 显示它不是隐藏而是灰色的,所以用户可以看到缺少的东西。
    CHECK(completeness(Lang::hu) == 0.0);

    // A 目录 携带IDs的英语有: DROPPED 并非更完整;
    // 它确实 过期 . 以 en 的 id 设定进行测量,所以额外 id 不算.
    const char* de_stale = R"({"a":"A","b":"B","gone":"X","also_gone":"Y"})";
    REQUIRE(install_catalog(Lang::de, de_stale));
    CHECK(completeness(Lang::de) == 0.5);
}

// ── 外观一行, 结束到结束 ───────────────────────────────────────
//
// i18n 上方的所有测试都孤立地进行。
// 测试 PANE : 语言行存在, 带有标记而不是索引,
// 选择一个切换当前语言而不重新启动。
//
// 为什么标记和索引都不够重要, 以至于无法标出 : 选择通常会存储选中的
// INDEX ,所以插入语言到 kLangs 中会默默改变每个已有的
// settings.json 意思是 - - 某人的 - 说 已保存 "7"
// 成为升级时的一种不同语言。 Storing "de" 无法做到这一点。

#include "agentty/runtime/panel/appearance.hpp"
#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/model.hpp"

TEST_CASE("i18n: the appearance pane carries a language row") {
    const char* en = R"JSON({
        "lang.picker.title": {"text": "Language"},
        "lang.picker.help":  {"text": "agentty's own labels"},
        "lang.auto":         {"text": "Auto (follow system)"},
        "lang.incomplete":   {"text": "{pct}% translated"}
    })JSON";
    REQUIRE(install_catalog(Lang::en, en));
    REQUIRE(set_active(Lang::en));

    agentty::ui_prefs::Prefs p;          // 空朗 = 自动
    const auto form = agentty::ui::panel::build_appearance_form(p, agentty::ui_prefs::detect(false));

    const auto* row = form.find(agentty::ui::panel::kApLang);
    REQUIRE(row != nullptr);

    const auto* c = std::get_if<agentty::form::field::Choice>(&row->value);
    REQUIRE(c != nullptr);

    // 自动加各种语言。
    CHECK(c->labels.size() == static_cast<std::size_t>(kLangCount) + 1);
    REQUIRE(c->ids.size() == c->labels.size());

    // Auto是FIRST,其id是空的——与ui.lang
    // 存储的一样空的,所以从未选择的用户会一直跟着他们的环境.
    CHECK(c->ids[0].empty());

    // 每个其它的ID都是一个真正的标记,就是第-trips回合。
    // 一个索引在表格编辑过程中会不稳定;一个标记不能是。
    for (std::size_t i = 1; i < c->ids.size(); ++i) {
        INFO("row " << i << " id '" << c->ids[i] << "'");
        Lang parsed{};
        REQUIRE(parse_tag(c->ids[i], parsed));
        CHECK(tag_of(parsed) == c->ids[i]);
    }

    // Labels are ENDONYMS: a Japanese speaker scans for 日本語, not for our
    // 英文词,查了三个脚本,哪里弄错了,
    // 一个英文读者就看不见了.
    const auto has = [&](std::string_view needle) {
        for (const auto& l : c->labels)
            if (l.find(needle) != std::string::npos) return true;
        return false;
    };
    CHECK(has("日本語"));      // ja
    CHECK(has("Русский"));     // ru
    CHECK(has("한국어"));       // ko
    CHECK(has("Deutsch"));     // de
}

// ── 真实的 目录 ,真实的 面板 ──────────────────────────────────
//
// 上面的每个测试都用四 -string 固定,
// 这个 加载 编号 SHIPPED 德语 目录 并
// 绘制实际 面板 翻译是否超越了 帧.
//
// 量度过这个 目录 :德语每串为1.1-2.8x英语,最差的绝对增长+
// 32列( sandbox.help.read_paths ),
// 最差比率2.83x( "Custom" -> "Benutzerdefiniert"。
// 这些是布局必须吸收的数字, 也是为什么德语是第二语言而不是第十语言,
// 这也是其他拉丁语的压力。 -script 语言。
TEST_CASE("i18n: every shipped catalog is complete and sane") {
    REQUIRE(agentty::i18n::init("en", ""));

    // 每种语言都有目录 \\\ hpp。 不 这儿就是
    // 那个 选择面板 将显示% -- -- 这是诚实的,
    // 也是为什么允许存在不完整的翻译。 不 允许翻译,
    // 它声称被运走 并且沉默地有漏洞, 因为半个德语
    // 面板 读作破碎而非 "60% done".
    const Lang shipped[] = {Lang::de, Lang::zh_CN, Lang::es,
                            Lang::fr, Lang::ja, Lang::pt_BR, Lang::ru};

    for (const Lang l : shipped) {
    INFO("language " << std::string{english_name(l)});
    REQUIRE(set_active(l));
    REQUIRE(active() == l);

    // 100%反对英语,部分 目录 回到英语的空白,
    // 它沉默地隐藏着溢出 渲染 门正在寻找 -- 因此
    // 完整性是 PRECONDITION 门,
    // 不是好地方。
    CHECK(completeness(l) > 0.999);

    // 语言上的本名 自身 脚本。 检查是因为失败对一个
    // 英语读者来说是看不见的: 写 "Japanese" 地点
    // 日本語 belongs looks fine in review and is useless to the one person
    // 这是为。
    CHECK(!endonym(l).empty());

    // 无法翻译为空。 空字符串 渲染 以空行表示,
    // 该行仍可响应 Enter - 比一个没有翻译的还要糟
    // 至少说出它做了什么
    for (const auto& id : ids_of(l)) {
        INFO("id " << id);
        CHECK(!t(id).empty());
    }

    // 每一个ID英语,德语有。 -vs-English ;
    // 此检查英语 -vs-German 也就是另一半
    // 腐烂的一块 成为新的绳子
    for (const auto& id : ids_of(Lang::en)) {
        INFO("missing from " << std::string{tag_of(l)} << ": " << id);
        CHECK(t(id) != id);     // t() 回到英语,永远不要ID
    }

    // 多元分类必须匹配语言所能达到的 PRODUCE.
    // A 目录 声明 `few` 日语是翻译误读 模式
    // 定义 —— 数据 运行时 永远不会读到它,
    // 和一个省略 `many` 俄国人不能数到五,两者都值得失败;
    // 第一种是沉默中腐烂的。
    for (const Plural p : {Plural::One, Plural::Few, Plural::Many,
                           Plural::Other}) {
        bool producible = false;
        for (long long n = 0; n <= 200; ++n)
            if (select(l, n) == p) { producible = true; break; }
        INFO("category " << std::string{key_of(p)});
        CHECK(producible == has_category(l, p));
    }
    }

    REQUIRE(set_active(Lang::en));
}
