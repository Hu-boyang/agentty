// tests/openai_request_body_test.cpp - 电话 字节 我们真的派来了
//
// conformance.hpp 记录 对应的 字段将输入哪个终点和原因。
// 正文 建筑师被拉出 流式传输 函数没有检查是否同意该代码: 唯一能够
// 看到请求的方法是 传输 实时追踪日志 调用 ,所以,
// 我说 "do we send prompt_cache_key to a local server?"
// 被回答的读作a 高频路径.
//
// build_request_body ()纯度——请求进入,
// JSON 答案是肯定的,这些测试将层次从评论变为
// 合同。
//
// 将每个规则设置在下面的不对称: 发送一个
// 字段的终点拒绝是。 HARD 400/422
// 测试更关心我们 DON '不发.

#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include "agentty/domain/catalog.hpp"
#include "agentty/domain/reasoning_off.hpp"
#include "agentty/provider/error_class.hpp"
#include "agentty/provider/openai/transport.hpp"
#include "agentty/provider/ollama/transport.hpp"
#include "agentty/provider/registry.hpp"

using json = nlohmann::json;
namespace oa = agentty::provider::openai;

namespace {

// 主持人 TLS 终点( E) OpenAI 格鲁克,尤洛 -Auto, …).
oa::Endpoint hosted() {
    return oa::Endpoint{"api.example.com", 443, "/v1/chat/completions",
                        "/v1/models", true, "example"};
}

// 本地纯文本服务器( U) llama.cpp, LM 录音室,用笔记本电脑播放VLLM
oa::Endpoint local() {
    oa::Endpoint e{"localhost", 8080, "/v1/chat/completions",
                   "/v1/models", false, "llama.cpp"};
    return e;
}

// 赤裸裸的 Ollama 守护进程自己说 / api/chat 协议。
oa::Endpoint ollama_native() {
    oa::Endpoint e{"localhost", 11434, "/api/chat", "/api/tags", false, "ollama"};
    e.native_api = true;
    return e;
}

oa::Request base(oa::Endpoint ep) {
    oa::Request r;
    r.model         = "test-model";
    r.system_prompt = "you are a test";
    r.max_tokens    = 4096;
    r.endpoint      = std::move(ep);
    return r;
}

}  // 命名空间结束。

// ═══ 通用等级: 万物都得到了这些 ═══════════════════════════════

TEST_CASE("body: the universal fields go to every endpoint") {
    for (auto ep : {hosted(), local()}) {
        const json b = oa::build_request_body(base(ep));

        CHECK(b["model"] == "test-model");
        CHECK(b["stream"] == true);
        CHECK(b["max_tokens"] == 4096);
        // Ask 最终用途 帧 以上下文测量 流式传输 转身,
        // 终点不知道 IGNORE 而非误入歧途,
        // 故你应当广行(派遣), -Auto).
        CHECK(b["stream_options"]["include_usage"] == true);
        // 系统提示词引导对话.
        REQUIRE(b["messages"].is_array());
        CHECK(b["messages"][0]["role"] == "system");
    }
}

TEST_CASE("body: max_tokens, NOT max_completion_tokens") {
    // 该 DELIBERATE DIVERGENCE FROM OPENAI'S SPEC ,
    // 最有可能是,但这个 "fixed" 被别人读 OpenAI 医生的
    //
    // OpenAI 已贬值 max_tokens 聊天完成时的表示
    // max_completion_tokens 但VLLM, , ,
    // 我和他一起工作。 llama.cpp ,一起和大多数的共和家族接受 仅
    // max_tokens ——如此符合文件要求,打破了以文件命名的大
    // 多数生态系统. OpenAI 仍然接受旧名;一个在任何地方都可行
    // 的拼写比一个主机上有效的符合性要好.
    const json b = oa::build_request_body(base(hosted()));
    CHECK(b.contains("max_tokens"));
    CHECK(!b.contains("max_completion_tokens"));
}

TEST_CASE("body: a host that 400'd max_tokens gets max_completion_tokens") {
    // OpenAI's o-series 聊天时拒绝旧名。每个主机都会学到。
    using agentty::provider::OptionalField;
    auto ep = hosted();
    ep.host = "maxtok-rename.example.com";
    agentty::add_rejected_field(agentty::host_key(ep.host, ep.port),
                                agentty::provider::bit(OptionalField::MaxTokens));
    const json b = oa::build_request_body(base(ep));
    CHECK(!b.contains("max_tokens"));
    CHECK(b["max_completion_tokens"] == 4096);
}

TEST_CASE("body: the legacy tool shape stays dead") {
    auto req = base(hosted());
    req.tools.push_back({.name = "read_file",
                         .description = "read a file",
                         .input_schema = json::object()});
    const json b = oa::build_request_body(req);

    CHECK(b.contains("tools"));
    CHECK(b["tool_choice"] == "auto");
    // `functions` 将它送入遗留的路径,
    // 在仍然尊重它的终点上,分割工具处理。
    CHECK(!b.contains("functions"));
    CHECK(!b.contains("function_call"));
}

TEST_CASE("body: no tools means no tools key at all") {
    // 空的 空的 `tools: []` 与缺席不同——有些服务
    // 器拒绝空阵列,而且成本 提示词 别人身上的标志
    const json b = oa::build_request_body(base(hosted()));
    CHECK(!b.contains("tools"));
    CHECK(!b.contains("tool_choice"));
}

// ═══ 托管等级: 仅TLS ═══════════════════════════════════════════════

TEST_CASE("body: prompt_cache_key only where the row opts in (#93)") {
    // OpenAI 自动 -caches 前缀 QQ1024 符和稳定的键将对话插入到一个 缓存 节点,
    // 但是 OpenAI-COMPATIBLE 不是,是"我们",不是"我们" OpenAI:
    // 严格主机层 400 未知字段,转弯则死亡。
    //   格罗克:"property 'prompt_cache_key' is unsupported"
    //   Gemini 报告不认识 prompt_cache_key 字段。
    // TLS曾经是大门;它不是证据,注册表行是.
    auto with_key = [](oa::Endpoint ep) {
        auto r = base(std::move(ep));
        r.session_key = "thread-abc";
        return oa::build_request_body(r);
    };

    // 解决的正是 picker/CLI 解决提供者 ID。
    CHECK(with_key(oa::Endpoint::from_spec("openai"))["prompt_cache_key"] == "thread-abc");
    CHECK(with_key(oa::Endpoint::from_spec("openrouter"))["prompt_cache_key"] == "thread-abc");

    // 发行的两位主持人,加上其他严格的对等排.
    for (const char* id : {"groq", "gemini", "together", "cerebras", "deepseek",
                           "xai", "mistral", "fireworks", "kimi"}) {
        INFO("provider " << id);
        CHECK(!with_key(oa::Endpoint::from_spec(id)).contains("prompt_cache_key"));
    }

    // 尚未选择的托管端点( 客户端大于 TLS): 否。
    CHECK(!with_key(hosted()).contains("prompt_cache_key"));
    // 本地服务器:没有。
    CHECK(!with_key(local()).contains("prompt_cache_key"));

    // 没有会话键:没有字段,即使它被接受。
    CHECK(!oa::build_request_body(base(oa::Endpoint::from_spec("openai")))
               .contains("prompt_cache_key"));
}

// 两者 正文 从第93期开始, 逐字记录形。
TEST_CASE("error: the provider's sentence, whatever the body shape (#93)") {
    // 格罗克:通常的OpenAI对象.
    CHECK(oa::provider_error_message(
              R"({"error":{"message":"property 'prompt_cache_key' is unsupported",)"
              R"("type":"invalid_request_error"}})")
          == "property 'prompt_cache_key' is unsupported");

    // Gemini: ARRAY-wrapped 漂亮,我爱她 -printed ,
    // 带有 details/fieldViolations 源码树 曾经把全部都扔进记录
    const std::string gemini = R"([{
  "error": {
    "code": 400,
    "message": "Invalid JSON payload received. Unknown name \"prompt_cache_key\": Cannot find field.",
    "status": "INVALID_ARGUMENT",
    "details": [{"@type": "type.googleapis.com/google.rpc.BadRequest",
                 "fieldViolations": [{"description": "x"}]}]
  }
}])";
    CHECK(oa::provider_error_message(gemini)
          == "Invalid JSON payload received. Unknown name \"prompt_cache_key\": "
             "Cannot find field.");

    // 其他形状。
    CHECK(oa::provider_error_message(R"({"error":"rate limited"})") == "rate limited");
    CHECK(oa::provider_error_message(R"({"message":"nope"})") == "nope");
    // 没有 JSON : 封顶的 -line 节选,从不重复 -line 墙壁。
    const auto raw = oa::provider_error_message("<html>\n<body>bad\ngateway</body>\n</html>");
    CHECK(raw == "<html> <body>bad gateway</body> </html>");
    CHECK(oa::provider_error_message(std::string(5000, 'x')).size() == 300);
    CHECK(oa::provider_error_message("").empty());
}

// #91:一个本地模型(gpt) -oss 打开 llama.cpp (将代码
// 粘入答复,然后拒绝) "the entire codebase in a single response"。
// 该主题 提示词 说到时候 不 改为 调用 两种本地工具。
// 提示词 现在必须携带使其行动的规则:使用工具来写作,从不粘贴,
// 许多步骤是正常的,读到的[划 仍然有 字节 (#92).
TEST_CASE("local prompts tell the model to act, not paste (#91)") {
    const std::string rules{oa::local_model_action_rules()};
    CHECK(rules.find("NEVER paste the code") != std::string::npos);
    CHECK(rules.find("single response") != std::string::npos);
    CHECK(rules.find("[cached]") != std::string::npos);
    // 当地 提示词 嵌入 相同 块, 逐字记录。
    CHECK(oa::local_model_system_prompt().find(rules) != std::string::npos);
    CHECK(agentty::provider::ollama::system_prompt().find(rules) != std::string::npos);
}

// 每一个 OpenAI-compat 注册表 线条: 正文
// 我们送货 仅 字段中,符合表允许该行。在测试时,
// 不按用户的顺序,接下下一个 # 93 —— 任何漏给从未选择过
// 主机的字段。
TEST_CASE("body: no row is sent prompt_cache_key unless it opted in") {
    for (const auto& p : agentty::provider::kProviders) {
        if (!p.http_dialled() || p.native_api) continue;
        INFO("provider " << p.id);
        auto r = base(oa::Endpoint::from_spec(p.id));
        r.session_key = "t";
        CHECK(oa::build_request_body(r).contains("prompt_cache_key")
              == p.accepts_prompt_cache_key);
    }
}

// ═══ 已鉴定等级:只有证据 ═════════════════════════════════════

TEST_CASE("body: reasoning_effort is gated, never assumed") {
    // 雾 MAGISTRAL 原生原因 返回422 个字段,
    // 所以 目录 排除它,并且 `effort` 来了 空 给,
    // 那个 传输层 不重写 -derive ——它信任大门,不发任何邮件。
    auto h = base(hosted());
    h.effort = "high";
    CHECK(oa::build_request_body(h)["reasoning_effort"] == "high");

    // 缺勤额为上游 - >。
    auto gated = base(hosted());
    gated.effort = "";
    CHECK(!oa::build_request_body(gated).contains("reasoning_effort"));

    // A LOCAL 服务器也得到它。这曾经是相反的。
    // "local servers reject it outright" 服务器
    // 的确是如此,它写给这个固定器的,而且是假的: `local()` 标签
    // llama.cpp ,和 llama.cpp 文档 --reasoning-effort
    // 并接受请求字段。
    //
    // Ollama IS 拒绝它的服务器, 并且它从未达到此代码 :
    // native_api 通往/ 的路线 api/chat 分支,
    // 在 reasoning_effort 线(见下文的土著方言案例)。
    //
    // 关键是大门 `effort` 空的,这是决定的 上游
    // 由 resolved_caps ()——对于一个本地服务器来说,
    // 现在它读取的是服务器自己的能力声明,而不是从模型
    // 的ID中猜测. 传输层 加密从来不是模型的证据。
    auto l = base(local());
    l.effort = "high";
    CHECK(oa::build_request_body(l)["reasoning_effort"] == "high");

    // ...一个当地模特儿却一无所获
    auto l_gated = base(local());
    l_gated.effort = "";
    CHECK(!oa::build_request_body(l_gated).contains("reasoning_effort"));

    // 服务器 执行 拒绝从未看到过的领域——经过证明,
    // 在评论中也没有断言。 Ollama 本地分支建立自己的分支
    // 正文 并提前返回,所以努力= "high" 无法漏入/。
    // api/chat 即使聊天路径现在会发送它。
    auto n = base(ollama_native());
    n.effort = "high";
    CHECK(!oa::build_request_body(n).contains("reasoning_effort"));
}

// ═══ 本地方言不一样 传输 ══════════════════════════════

TEST_CASE("body: Ollama native speaks its own protocol") {
    // /api/chat 谓 不 OpenAI-shaped 漏水 OpenAI-only
    // 字段是本地守护进程如何在从未广告过的字段上开始400个字段的。
    auto req = base(ollama_native());
    req.session_key = "thread-abc";
    req.effort      = "high";
    const json b = oa::build_request_body(req);

    // 产出预算的乘数 options.num_predict — Ollama
    // '默认为~128令牌,它使代理器转折为无.
    CHECK(b["options"]["num_predict"] == 4096);
    CHECK(!b.contains("max_tokens"));

    // 没有一个 OpenAI-family 额外属于此 传输.
    CHECK(!b.contains("stream_options"));
    CHECK(!b.contains("prompt_cache_key"));
    CHECK(!b.contains("reasoning_effort"));

    // 但基本情况依然不变。
    CHECK(b["model"] == "test-model");
    CHECK(b["stream"] == true);
    CHECK(b["messages"][0]["role"] == "system");
}

// ═══ 纯洁 ══════════════════════════════════════════════════════════════

TEST_CASE("body: building is pure and repeatable") {
    // 没有全局,没有I/O,没有隐藏状态。如果失败的话,
    // 构建器中的某些东西会在其论据之外达到 — — 这正是使
    // 旧的内置版本无法测试的原因。
    auto req = base(hosted());
    req.session_key = "t1";
    req.effort      = "medium";
    CHECK(oa::build_request_body(req) == oa::build_request_body(req));
}

TEST_CASE("body: an empty system prompt adds no empty message") {
    auto req = base(hosted());
    req.system_prompt = "";
    const json b = oa::build_request_body(req);
    REQUIRE(b["messages"].is_array());
    for (const auto& m : b["messages"])
        CHECK(m["role"] != "system");
}

// ═══ 快速公用设备调用 ═════════════════════════════════════════════════════════════

TEST_CASE("body: a quick call asks a custom host not to think") {
    // 一个思考模式 llama.cpp 花费64美元 -token 标题预算
    // reasoning_content 并返回了文本。
    auto ep = oa::Endpoint::from_spec("https://pod-8888.proxy.runpod.net/v1");
    auto req = base(ep);
    req.quick = true;
    const json b = oa::build_request_body(req);
    CHECK(b["chat_template_kwargs"]["enable_thinking"] == false);

    req.quick = false;                                 // 正常转弯:未触动
    CHECK(!oa::build_request_body(req).contains("chat_template_kwargs"));

    req.quick  = true;                                 // 要求努力:遵守
    req.effort = "low";
    CHECK(!oa::build_request_body(req).contains("chat_template_kwargs"));
}

TEST_CASE("body: a quick call never sends chat_template_kwargs to a preset") {
    // 托管的模式定义是严格的;一个未知的字段是400.
    auto req = base(oa::Endpoint::from_spec("openai"));
    req.quick = true;
    CHECK(!oa::build_request_body(req).contains("chat_template_kwargs"));
}

// ═══ 原因关闭 ═════════════════════════════════════════════════════════════════════
// off必须是SENT:Quen3 / DeepSeek V3.1 / gpt-5.1+
// 当努力领域被忽略时思考.

namespace {
constexpr std::uint8_t off(agentty::ReasoningOff r) { return static_cast<std::uint8_t>(r); }
}

TEST_CASE("body: off on a custom host sends both off fields") {
    // SovereignEG 'vLLM 荣誉 reasoning_effort:"none" (计量:
    // 0 推理符对 33 带有模板切换符; llama.cpp
    // 仅包含模板切换器, 自定义主机得到两者.
    auto req = base(oa::Endpoint::from_spec("https://backend.sovereigneg.com/v1"));
    req.model         = "Qwen3.8-27B-FP8";
    req.no_reasoning  = true;
    req.reasoning_off = off(agentty::ReasoningOff::Toggle);
    const json b = oa::build_request_body(req);
    CHECK(b["reasoning_effort"] == "none");
    CHECK(b["chat_template_kwargs"]["enable_thinking"] == false);

    // 自定义主机上的未知模型:相同.
    req.reasoning_off = off(agentty::ReasoningOff::Unknown);
    CHECK(oa::build_request_body(req)["reasoning_effort"] == "none");

    // 得知这个主机拒绝"none"(每个主机为永久状态):
    // 模板开关，之后绝不能再使用被拒绝的值。
    const auto hk = agentty::host_key(req.endpoint.host, req.endpoint.port);
    agentty::add_rejected_field(hk, agentty::provider::bit(
        agentty::provider::OptionalField::EffortNone));
    const json t = oa::build_request_body(req);
    CHECK(!t.contains("reasoning_effort"));
    CHECK(t["chat_template_kwargs"]["enable_thinking"] == false);
    // 选择的等级是一个不同的值, 并且仍然会外出。
    req.effort = "low";
    CHECK(oa::build_request_body(req)["reasoning_effort"] == "low");
    req.effort.clear();
    agentty::set_rejected_fields({});

    // 还没关机(没有梯子自动):没有发件.
    req.no_reasoning = false;
    const json n = oa::build_request_body(req);
    CHECK(!n.contains("chat_template_kwargs"));
    CHECK(!n.contains("reasoning_effort"));
}

TEST_CASE("body: off on a model that takes effort none sends none") {
    auto req = base(oa::Endpoint::from_spec("mistral"));
    req.no_reasoning  = true;
    req.reasoning_off = off(agentty::ReasoningOff::EffortNone);
    const json b = oa::build_request_body(req);
    CHECK(b["reasoning_effort"] == "none");
    CHECK(!b.contains("chat_template_kwargs"));
}

TEST_CASE("body: off never puts unknown fields on a hosted preset") {
    auto req = base(oa::Endpoint::from_spec("groq"));
    req.no_reasoning  = true;
    req.reasoning_off = off(agentty::ReasoningOff::Toggle);
    const json b = oa::build_request_body(req);
    CHECK(!b.contains("chat_template_kwargs"));
    CHECK(!b.contains("reasoning_effort"));
}

TEST_CASE("body: off on OpenRouter uses its reasoning object") {
    auto req = base(oa::Endpoint::from_spec("openrouter"));
    req.model         = "qwen/qwen3-32b";
    req.no_reasoning  = true;
    req.reasoning_off = off(agentty::ReasoningOff::Toggle);
    const json b = oa::build_request_body(req);
    CHECK(b["reasoning"]["enabled"] == false);
    CHECK(!b.contains("chat_template_kwargs"));
}

TEST_CASE("body: a model that can't stop reasoning gets no fake off") {
    auto req = base(oa::Endpoint::from_spec("https://backend.sovereigneg.com/v1"));
    req.model         = "gpt-oss-20b";
    req.no_reasoning  = true;
    req.reasoning_off = off(agentty::ReasoningOff::Impossible);
    const json b = oa::build_request_body(req);
    CHECK(!b.contains("chat_template_kwargs"));
    CHECK(!b.contains("reasoning_effort"));
}

TEST_CASE("body: a chosen effort is never overridden by off") {
    auto req = base(oa::Endpoint::from_spec("https://backend.sovereigneg.com/v1"));
    req.effort        = "high";
    req.no_reasoning  = true;   // 过期 标志 绝不能赢
    req.reasoning_off = off(agentty::ReasoningOff::Toggle);
    const json b = oa::build_request_body(req);
    CHECK(b["reasoning_effort"] == "high");
    CHECK(!b.contains("chat_template_kwargs"));
}

TEST_CASE("body: the old quick-call switch still works on a custom host") {
    auto req = base(oa::Endpoint::from_spec("https://pod-8888.proxy.runpod.net/v1"));
    req.quick = true;
    const json b = oa::build_request_body(req);
    CHECK(b["chat_template_kwargs"]["enable_thinking"] == false);
}

TEST_CASE("off: a rejection naming reasoning_effort is not read as OpenRouter's reasoning") {
    using agentty::provider::OptionalField;
    using agentty::provider::parse_rejected_field;
    // 虚拟LLM -style 400个价值;必须达到这一努力 -none 手,不是一般的,
    // 会掉下来的 OpenRouter's `reasoning` 对象。
    CHECK(parse_rejected_field(
              "1 validation error: reasoning_effort: Input should be 'low', "
              "'medium' or 'high' [type=literal_error] (not supported)", 400)
          == OptionalField::None);
    CHECK(parse_rejected_field("Unrecognized request argument: reasoning", 400)
          == OptionalField::Reasoning);
    CHECK(parse_rejected_field("Unknown field: chat_template_kwargs", 400)
          == OptionalField::ChatTemplateKwargs);
}
