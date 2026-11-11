// complexity_test - 轮到它了 -complexity 分类器( 4) smart::classify_complexity).
// 纯热度,不 I/O. 锁 保守校准: 标准是 回退 ,
// 只有强信号会转动到 Complex 或 Trivil。

#include "agentty/domain/complexity.hpp"

#include "agtest.hpp"

using namespace agentty::smart;

TEST_CASE("classify_complexity buckets prompts") {
    // 三角形:简短的确认/1 -word 绝对是必须的
    CHECK(classify_complexity("yes") == Complexity::Trivial, "yes → trivial");
    CHECK(classify_complexity("go ahead") == Complexity::Trivial, "go ahead → trivial");
    CHECK(classify_complexity("commit it") == Complexity::Trivial, "commit it → trivial");
    CHECK(classify_complexity("  thanks  ") == Complexity::Trivial, "trimmed thanks → trivial");
    CHECK(classify_complexity("") == Complexity::Trivial, "empty → trivial");

    // 复杂:design/debug/why词汇,长转转,或许多被列举的问.
    CHECK(classify_complexity("redesign the auth module") == Complexity::Complex,
          "redesign → complex");
    CHECK(classify_complexity("why does the retry loop deadlock?") == Complexity::Complex,
          "why+deadlock → complex");
    CHECK(classify_complexity("do a deep research on this and implement all state of the art way")
          == Complexity::Complex, "deep research + implement all → complex");
    CHECK(classify_complexity(
            "1. add a field\n2. persist it\n3. render it\n4. test it") == Complexity::Complex,
          "4 enumerated asks → complex");

    // 简单:一个短的单-clause请求(没有设计 vocab, 1行).
    CHECK(classify_complexity("fix the typo in README") == Complexity::Simple,
          "short fix → simple");
    CHECK(classify_complexity("rename foo to bar") == Complexity::Simple,
          "short rename → simple");

    // 标准:中,模棱两可的转弯的保守默认.
    CHECK(classify_complexity(
            "update the parser to also accept trailing commas in list literals")
          == Complexity::Standard, "medium request → standard");

    // 保守偏见:含糊不清 'why' 仍然在升级(从未低于) -thinks).
    CHECK(classify_complexity("explain why this test is flaky") == Complexity::Complex,
          "why → escalates (conservative upward)");

    // 上下文继承 classify_with_context：对前一条请求的简短追问，
    //    复杂转弯会保持一些重量而不是倒塌到简单。
    CHECK(classify_with_context("now do the same for the other module",
                                Complexity::Complex) == Complexity::Standard,
          "short follow-up after Complex → lifted to Standard");
    // 新鲜的Trivial 阿克总是被取 面值无论历史。
    CHECK(classify_with_context("thanks", Complexity::Complex) == Complexity::Trivial,
          "ack after Complex → still Trivial (an ack is an ack)");
    // 文本中一个全新的复杂信号直接获胜。
    CHECK(classify_with_context("redesign the whole thing", Complexity::Simple)
          == Complexity::Complex, "fresh Complex text wins over Simple history");
    // 永远 DROPS 低于文本本身的层次,当历史变弱时。
    CHECK(classify_with_context("fix the typo", Complexity::Trivial) == Complexity::Simple,
          "weaker history never lowers the text's own tier");
    // 标准历史 + 简单文本 保持简单(只包含复杂升降机).
    CHECK(classify_with_context("rename foo to bar", Complexity::Standard)
          == Complexity::Simple, "Standard history does not lift a Simple follow-up");

    // 与书写系统无关的长度下限：没有空格分词的长中文、日文或韩文请求，
    //    必须在structure/length上升级,而不是仅仅因为word_count
    //    看到~1白空符而崩溃为简单. ~450 glyphs of CJK +
    //    条款标记通过glyph-length +条款信号清除了 Complex 带.
    {
        std::string cjk;
        for (int i = 0; i < 150; ++i) cjk += "\xE6\x8E\xA2\xE7\xB4\xA2\xE3\x80\x81";  // 2 glyphs + , ), 高音 .
        CHECK(classify_complexity(cjk) == Complexity::Complex,
              "long structured CJK turn escalates (glyph length + clause density)");
    }
    // 空 / 白空 -only 输入是 Trivil, 绝不是崩溃。
    CHECK(classify_complexity("") == Complexity::Trivial, "empty → trivial");
    CHECK(classify_complexity("   \n\t ") == Complexity::Trivial, "whitespace → trivial");

    // ── 添加模型比旧的望台还好 ──
    // 单人 "design" 不再继续 HARD-forces 混凝土上的东西 UI
    // 问(旧的) `has(kComplexTerms)` 完全没有超能力。
    CHECK(classify_complexity("add a design token for button padding")
          != Complexity::Complex,
          "one keyword doesn't override structure on a short concrete ask");
    // 但是结构+词汇TOGETHER仍然在升级.
    CHECK(classify_complexity(
            "redesign the auth module: rework sessions, refresh tokens, and "
            "the login flow end to end") == Complexity::Complex,
          "vocabulary + multi-clause structure → complex");
    // 多个 -clause 连接密度升级 无需 任何硬关键字。
    CHECK(classify_complexity(
            "update the parser and the lexer and the formatter and re-run the "
            "golden tests then wire it into the build")
          == Complexity::Complex, "pure conjunction/clause density → complex");
    // 得分的API暴露了可用差值(自信在一明确空穴上).
    CHECK(classify_score("thanks").margin > 0, "trivial ack has positive margin");
    CHECK(classify_score("redesign everything end to end across the stack").tier
          == Complexity::Complex, "scored API agrees with tier API");

    // ── 复杂门槛值 ──────────────────────────────
    // 切成一个 PARAMETER 现在,不是 getenv()。必须是: 值是
    // 用户 -configurable (设置行)
    // "smart.complex_threshold" ),从这里从环境读到
    // 的一则读物无法看到在其中设定的阈值. UI。该词 运行时 解析
    // 内存 -over-stored 启动时一次 RoleConfig 传入.
    //
    // 所以,这个声称 CLASSIFIER 合同——即界限移动边界——直接,
    // 不经过一个过程 -global 以设置和未设置。
    {
        const char* probe = "update the parser and re-run the formatter";
        const Complexity base = classify_complexity(probe);
        CHECK(base != Complexity::Complex,
              "the probe is below the Complex cut at the shipped default");

        // 降低切度会升级到边线转弯...
        CHECK(classify_score(probe, /*complex_min=*/1).tier == Complexity::Complex,
              "lowering the threshold escalates a borderline turn");
        // ...而默认参数复制了运出的行为.
        CHECK(classify_score(probe).tier == base,
              "the default parameter is the shipped classification");
        // 把它提升到探测器的分数之外 也不能让它变得复杂
        CHECK(classify_score(probe, /*complex_min=*/8).tier != Complexity::Complex,
              "raising the threshold de-escalates");
    }

    // ── is_routing_correction : 结果 -learning 后悔信号 ─────────
    // TRUE = 不满随即出现 -up 转弯时应该偏差 -class
    // 努力起来,假的 -positive 案件是整点的解决。
    {
        using agentty::smart::is_routing_correction;
        // 诚悔之.
        CHECK(is_routing_correction("no, that doesn't work"), "'no, doesn't work' is a regret");
        CHECK(is_routing_correction("that's wrong"), "'that's wrong' is a regret");
        CHECK(is_routing_correction("undo that"), "'undo' is a regret");
        CHECK(is_routing_correction("revert it"), "'revert' is a regret");
        CHECK(is_routing_correction("still failing after your change"), "'still failing' is a regret");
        CHECK(is_routing_correction("actually that's not right"), "'actually not right' is a regret");
        CHECK(is_routing_correction("nope, still broken"), "'nope still broken' is a regret");
        CHECK(is_routing_correction("that broke the build"), "'that broke' is a regret");

        // 用来打击的天真前缀扫描——必须是FALSE——的假阳性。
        CHECK(!is_routing_correction("actually, let's also add tests"),
              "additive 'actually' is NOT a regret");
        CHECK(!is_routing_correction("actually that's perfect"),
              "praise 'actually that's perfect' is NOT a regret");
        CHECK(!is_routing_correction("wrong file, look at src/foo instead"),
              "redirection 'wrong file' is NOT a regret");
        CHECK(!is_routing_correction("no worries, carry on"),
              "'no worries' is NOT a regret");
        CHECK(!is_routing_correction("now add error handling"),
              "a plain new request is NOT a regret");
        CHECK(!is_routing_correction("thanks, that works"),
              "gratitude is NOT a regret");
        CHECK(!is_routing_correction("looks good, ship it"),
              "approval is NOT a regret");
    }
}
