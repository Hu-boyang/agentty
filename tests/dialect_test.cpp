// 测试 provider::dialect_for ——决定一个(提供方,
// 型号)是否转出的唯一权力机构./ chat/completions 或/回应。
//
// 为何这个文件值得它的重量: 方言选择不是可见 设计时(无)
// 选择面板 排队,不 标志 ),所以错误的答案从来不会作为一个
// 糟糕的场景出现——它会以400个中点出现. -turn ,
// 或者作为一个思考 面板 在一个频道上旋转, 传输 这些案件将
// 无法填补的行为归结为 UI 检查它。

#include <doctest/doctest.h>

#include "agentty/provider/dialect.hpp"
#include "agentty/provider/openai/responses_site.hpp"
#include "agentty/provider/registry.hpp"

using namespace agentty::provider;

namespace {
// 每个案子都从模型开始 -family 之前,从不从另一个案例
// 的学习拒绝(观察商店是过程) -global )),设计.
struct CleanSlate {
    CleanSlate()  { reset_dialect_observations(); }
    ~CleanSlate() { reset_dialect_observations(); }
};
} // 命名空间结束。

TEST_CASE("dialect: hosts without a Responses endpoint stay on chat") {
    CleanSlate _;
    // 这些行没有 responses_path 即使对于推理模型,
    // 答案也必须是查特——他们给出了与聊天相比的推理.
    // `reasoning_content` ,并发明/答复 URL 给他们404辆
    CHECK(dialect_for("deepseek", "deepseek-reasoner") == Dialect::Chat);
    CHECK(dialect_for("mistral",  "magistral-medium")  == Dialect::Chat);
    CHECK(dialect_for("groq",     "gpt-5")             == Dialect::Chat);
    // 用户的 OpenAI-compatible 基础 URL :未知的能力,
    // 而错误的猜测则花费了失败的转机对一个我们无法廉价探测的宿主.
    CHECK(dialect_for("localhost:8080", "gpt-5") == Dialect::Chat);
}

TEST_CASE("dialect: non-OpenAI wires answer structurally") {
    CleanSlate _;
    CHECK(dialect_for("anthropic", "claude-opus-4-5") == Dialect::Native);
    CHECK(dialect_for("ollama",    "qwen3:8b")        == Dialect::Native);
    // 排队的 DEFAULT 反应已经没有第二个方言可以选择了...
    // endpoints_consistent () 禁止携带两者。
    CHECK(dialect_for("chatgpt", "gpt-5-codex") == Dialect::Responses);
}

TEST_CASE("dialect: the GPT-5.4 tool-calling cutoff is a POINT release") {
    CleanSlate _;
    // 人数 OpenAI 推理指南 :
    // "Starting with GPT-5.4, Chat Completions does not support tool calling with reasoning_effort values other than none."（从 GPT-5.4 起，Chat Completions 的工具调用要求 reasoning_effort 为 none。）
    // agentty 总是发送工具,所以5.4+是 HARD 所需经费
    // ——同时 5.0/5.1 继续合法聊天(他们只是无法表现出思维).
    CHECK(!model_requires_responses("gpt-5"));
    CHECK(!model_requires_responses("gpt-5-mini"));
    CHECK(!model_requires_responses("gpt-5.1"));
    CHECK(model_requires_responses("gpt-5.4"));
    CHECK(model_requires_responses("gpt-5.4-mini"));
    CHECK(model_requires_responses("gpt-5.6-terra"));
    // GPT -6 / Astra 放弃聊天功能 -calling 完全... 完全没有注意
    CHECK(model_requires_responses("gpt-6-astra"));
    // 副驾驶的Mai -code-* 400s `unsupported_api_for_model` 聊天时
    CHECK(model_requires_responses("mai-code-1.1-flash"));
    // 聊天 -only 家庭必须 绝不 被拖放到回复中: 共400个。
    // 这是将打破每个回复的回归。 Claude 转身
    CHECK(!model_requires_responses("claude-haiku-4.5"));
    CHECK(!model_requires_responses("gpt-4o"));
    CHECK(!model_requires_responses("gpt-4.1"));
}

TEST_CASE("dialect: slug shape does not change the family answer") {
    CleanSlate _;
    // 网关的命名空间和弹壳不同; 家族是上次斜线之后的部分。
    // 前缀-only匹配会错过这些,
    // 默默地将GPT-6 转换到无法携带的方言上。
    CHECK(model_requires_responses("openai/gpt-6-astra"));
    CHECK(model_requires_responses("GPT-5.4"));
    CHECK(dialect_for("openrouter", "openai/gpt-5.4") == Dialect::Responses);
    // ...但是一个命名的Chalk-only模型 仍然保留。
    CHECK(dialect_for("openrouter", "anthropic/claude-haiku-4.5") == Dialect::Chat);
}

TEST_CASE("dialect: openai routes reasoning models, keeps gpt-4o on chat") {
    CleanSlate _;
    CHECK(dialect_for("openai", "gpt-4o")     == Dialect::Chat);
    CHECK(dialect_for("openai", "gpt-4.1")    == Dialect::Chat);
    CHECK(dialect_for("openai", "gpt-5")      == Dialect::Responses);
    CHECK(dialect_for("openai", "o3")         == Dialect::Responses);
    CHECK(dialect_for("openai", "gpt-6-astra")== Dialect::Responses);
    // Provider-level 问题(A/C.5/56/L.61) 选择面板 ,在选择模型之前)。
    CHECK(dialect_for("openai", "") == Dialect::Responses);
}

TEST_CASE("dialect: copilot keeps its measured per-model split") {
    CleanSlate _;
    // 行为副驾驶的私人表格曾经拥有, 现在由共享的
    // 上游解答。 以实时自动会话来测量。
    CHECK(dialect_for("copilot", "gpt-5-mini")        == Dialect::Responses);
    CHECK(dialect_for("copilot", "mai-code-1.1-flash")== Dialect::Responses);
    CHECK(dialect_for("copilot", "claude-haiku-4.5")  == Dialect::Chat);
    CHECK(dialect_for("copilot", "gpt-4.1")           == Dialect::Chat);
}

TEST_CASE("dialect: a rejection from the wire overrides the prior") {
    CleanSlate _;
    // 该 反 -rot。这里的每个表格都是关于模型名称的猜测,
    // 名称移动。 过期 猜想必须自己 -correct
    // 而不是需要释放
    REQUIRE(dialect_for("openai", "gpt-5") == Dialect::Responses);
    note_dialect_rejected("openai", "gpt-5", Dialect::Responses);
    CHECK(dialect_for("openai", "gpt-5") == Dialect::Chat);

    // 即使一个REQUIRED模型也必须屈服给一个说不的宿主:
    // 一个与模型自身错误交谈失败的转弯会比我们所知道的404要好.
    REQUIRE(dialect_for("openai", "gpt-6-astra") == Dialect::Responses);
    note_dialect_rejected("openai", "gpt-6-astra", Dialect::Responses);
    CHECK(dialect_for("openai", "gpt-6-astra") == Dialect::Chat);

    // 校正范围为ONE(提供方,型号):单一的坏模式
    // 不得使其邻居的方言失效,或者一个被腐烂的弹簧上的
    // 一个404会默默地将每个推理的开口都降级到账户.
    CHECK(dialect_for("openai", "o3") == Dialect::Responses);
    CHECK(dialect_for("copilot", "gpt-5-mini") == Dialect::Responses);

    // ...反方向会推动
    note_dialect_rejected("openrouter", "some-new-reasoner", Dialect::Chat);
    CHECK(dialect_for("openrouter", "some-new-reasoner") == Dialect::Responses);
}

TEST_CASE("dialect: the UI predicate and the URL agree") {
    CleanSlate _;
    // 电话 加载-bearing 不变量 这些都是两个
    // 不同的猜测,它们漂移: `openai` 排在
    // 拨号聊天时声称有反应,所以想法 面板 承诺输出 传输
    // 从来没有发送。 一个 回答,你应该知道
    const char* pairs[][2] = {
        {"openai",   "gpt-4o"},
        {"openai",   "gpt-5"},
        {"openai",   "gpt-5.4"},
        {"copilot",  "claude-haiku-4.5"},
        {"copilot",  "gpt-5-mini"},
        {"deepseek", "deepseek-reasoner"},
    };
    for (const auto& p : pairs) {
        const bool ui_says   = wire_streams_reasoning_text(p[0], p[1]);
        const bool on_resp   = dialect_for(p[0], p[1]) == Dialect::Responses;
        const bool summaries = model_emits_reasoning_summaries(p[1]);
        // 如果我们为摘要-emitting模式向答复路线,则第UI模式必须
        // 提供^R模式;如果我们为没有摘要的模型向答复路线向答复路线必须提供
        // NOT模式。 不论哪种方式,第UI模式不能要求超过第传输模式。
        if (on_resp) CHECK(ui_says == summaries);
    }

    // 主持人降职 运行时 还必须停止广告宣传思想——
    // UI 之后 URL 是一个共同权威的全部点。
    REQUIRE(wire_streams_reasoning_text("openai", "gpt-5"));
    note_dialect_rejected("openai", "gpt-5", Dialect::Responses);
    CHECK(!wire_streams_reasoning_text("openai", "gpt-5"));
}

TEST_CASE("dialect: the Responses endpoint resolves from the row") {
    CleanSlate _;
    openai::ResponsesEndpoint ep;
    // 广告第二方言手回一个完整的目的地的行.
    REQUIRE(openai::responses_endpoint_for("openai", ep));
    CHECK(ep.host == std::string{"api.openai.com"});
    CHECK(ep.path == std::string{"/v1/responses"});
    CHECK(ep.use_tls);

    REQUIRE(openai::responses_endpoint_for("openrouter", ep));
    CHECK(ep.path == std::string{"/api/v1/responses"});
    // 该行解决了COMPLETE目的地,而不仅仅是URL.
    // provider_id和extra_headers在之后曾留给
    // 呼叫者到补丁,这使得功能只有幸运地拥有一个用户才是正确的.
    //
    // 归因(问题74)是它为什么重要: OpenRouter 将
    // 请求贷记到按键上的应用程序页面 HTTP-Referer ,
    // 它携带一个 responses_path ,因此,
    // 一个与该方言路由的推理模型必须被记为与聊天模型相同的。
    // 如果聊天只带有 头文件 归因会默默地取决于你选的型号
    CHECK(ep.provider_id == std::string{"openrouter"});
    {
        const auto hdr = [&](std::string_view n) -> std::string {
            for (const auto& [k, v] : ep.extra_headers)
                if (k == n) return v;
            return {};
        };
        CHECK(hdr("http-referer") == std::string{"https://agentty.org"});
        CHECK(hdr("x-title")      == std::string{"agentty"});
    }

    // 要求不归属的一行话 也得不到任何关于这个方言的答案
    REQUIRE(openai::responses_endpoint_for("openai", ep));
    CHECK(ep.extra_headers.empty());

    // 列不包含列 REFUSE 而不是发明一种 URL - 打电话者的
    // 合同是 "false means stay on chat" ,
    // 而且一个没有宿主的虚构/反应是保证404中位数 -turn.
    CHECK(!openai::responses_endpoint_for("deepseek", ep));
    CHECK(!openai::responses_endpoint_for("groq", ep));
    // ChatGPT 响应 -native 却骑着它 自身
    // 欧奥特 传输层 并且没有终点柱;它不能通过这里到达。
    CHECK(!openai::responses_endpoint_for("chatgpt", ep));
    // 自定义主机规格( O) from_spec 将生的 URL 输入 `label`
    // )与无行匹配,所以它停留在聊天上——我们不能知道它会说回音.
    CHECK(!openai::responses_endpoint_for("https://gw.example.com/api", ep));
}

TEST_CASE("dialect: routing survives an endpoint that isn't there") {
    CleanSlate _;
    // 结束 -to-end 反 -rot 故事,在层次 传输层 叉子看见它:
    // 用于“反应”、主机404s和“反应”的推理模式路线 下一个
    // 转弯必须独自进行聊天。 如果没有这种对话, 用户将坐在
    // 一个故障循环中, 没有任何变化的设置。 方言没有 UI.
    REQUIRE(dialect_for("openai", "gpt-5") == Dialect::Responses);
    note_dialect_rejected("openai", "gpt-5", Dialect::Responses);
    CHECK(dialect_for("openai", "gpt-5") == Dialect::Chat);
    // 而UI停止在同一瞬间为它进行广告思考,
    // 所以面板从不等待这个转弯不会携带的频道.
    CHECK(!wire_streams_reasoning_text("openai", "gpt-5"));
}

TEST_CASE("dialect: every row advertising Responses is well-formed") {
    CleanSlate _;
    // endpoints_consistent ( ) 已经 static_asserts
    // 这儿 编译 时间; 重述为 运行时 失败者将违法的行命名为“
    // ” 而不是“ 崩溃”。 static_assert 留言。
    for (const auto& p : kProviders) {
        if (p.responses_path.empty()) continue;
        CAPTURE(p.id);
        CHECK(p.responses_path != p.path);
        CHECK(!p.host.empty());
        CHECK(p.wire != Wire::OpenAIResponses);   // 这将是两个答案
        const auto tail = p.responses_path.substr(p.responses_path.size() - 10);
        CHECK(tail == std::string_view{"/responses"});
    }
}
