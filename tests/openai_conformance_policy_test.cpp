// tests/openai_conformance_policy_test.cpp -我们寄给谁的
//
// "Make it 100% conform to the OpenAI spec" 目标很明显,而目标是错误的。
// OpenAI DEPRECATED `max_tokens` 聊天完成时的表示 `max_completion_tokens` ;
// vLLM, ), 维基月球在线解说- 维基百科中的相关条目: 维基语录链接:名人名言 - 文学作品 - 谚语 - 谚文 -
// 谚語 - 谚言 - 谚名 - 谚中文 - 谚音 - 谚 llama.cpp ,一起和大多数的共和家族接受 仅
// `max_tokens` 与文件保持一致打破了文件名称的生态系统的大部分。
//
// 因此,目标不是"match the spec",
// 而是"be correct for the endpoint we are actually talking to"
// ——即-endpoint决定。 conformance.hpp将这些决定用证据记录下来;
// 这为表的可信度定下了标准,并声称,agentty故意
// 与OpenAI文件有出入的两个地方。

#include <doctest/doctest.h>

#include <string_view>

#include "agentty/provider/openai/conformance.hpp"

namespace cf = agentty::provider::openai::conformance;

TEST_CASE("conformance: every field rule carries its evidence") {
    // 无端的分级任务就是货物 -culted 由下一个人来解释理由。
    // IS 文物,就是这里 "we tried that and it 422'd on Mistral"
    // 活下来了
    for (const auto& r : cf::kRequestFields) {
        CHECK(!r.field.empty());
        CHECK(!r.rationale.empty());
        // 足够长的时间说一些东西,不是"needed".
        CHECK(r.rationale.size() > 20);
    }
    for (const auto& r : cf::kOmittedFields) {
        CHECK(!r.field.empty());
        CHECK(!r.rationale.empty());
        CHECK(r.rationale.size() > 20);
    }
}

TEST_CASE("conformance: a field is sent or omitted, never both") {
    // 两种表格是分割的,两个字段都意味着政策本身和
    // 正文 建筑师就是断线者——这恰恰是隐含的
    // -decision 麻烦你 头文件 已存在以删除。
    for (const auto& sent : cf::kRequestFields)
        for (const auto& omitted : cf::kOmittedFields)
            CHECK(sent.field != omitted.field);
}

TEST_CASE("conformance: max_tokens is the DELIBERATE divergence") {
    // 该 LOAD-BEARING DECISION。如果有人 "fixes"
    // 符合 OpenAI '当前文档, 每张 vLLM/llama.cpp/Together
    // 终点开始400,这个测试是绊线
    bool sends_max_tokens = false, omits_max_completion_tokens = false;

    for (const auto& r : cf::kRequestFields)
        if (r.field == "max_tokens") {
            sends_max_tokens = true;
            CHECK(r.tier == cf::Tier::Universal);
        }
    for (const auto& r : cf::kOmittedFields)
        if (r.field == "max_completion_tokens") omits_max_completion_tokens = true;

    CHECK(sends_max_tokens);
    CHECK(omits_max_completion_tokens);
}

TEST_CASE("conformance: endpoint-specific fields are not Universal") {
    // 发送一个字段的终点拒绝是 HARD 400/422。
    // 省略一个会让一个变数变优化。不对称设置默认值,
    // 所以已知被拒绝的任何东西必须到某个地方去。
    // 不 普世化为.
    for (const auto& r : cf::kRequestFields) {
        if (r.field == "prompt_cache_key") {
            // OpenAI-only 路由提示, 门每 注册表 划线 不 托管 :
            // 严格主机的相配层(Groq, Gemini) 400 it (# 93).
            CHECK(r.tier == cf::Tier::Probed);
        }
        if (r.field == "reasoning_effort") {
            // Mistral MAGISTRAL 以本土为理由,为此返回422.
            // 证据会打开它,不是能力表。
            CHECK(r.tier == cf::Tier::Probed);
        }
    }
}

TEST_CASE("conformance: the legacy tool shape stays dead") {
    // `functions` 由“E”取代。 `tools`
    // 发送它会选择在仍然尊重它的终点上留下的路径,
    // 将工具处理分成两个。
    bool omits_functions = false;
    for (const auto& r : cf::kOmittedFields)
        if (r.field == "functions") omits_functions = true;
    CHECK(omits_functions);

    bool sends_tools = false;
    for (const auto& r : cf::kRequestFields)
        if (r.field == "tools") {
            sends_tools = true;
            CHECK(r.tier == cf::Tier::Universal);
        }
    CHECK(sends_tools);
}
