// model_caps_test — ModelCapabilities::from_id 弱项 -model 推论.
//
// 电话 weak_tool_use 高压决定了哪个模型变瘦 提示词 +
// 末日 -loop 卫兵. 锁 分类如此 目录 编辑不能默默翻转
// hosted/strong 模型变成退化模式(或副模式) -versa).

#include <algorithm>
#include <chrono>
#include <string>
#include <string_view>
#include <vector>

#include "agtest.hpp"

#include "agentty/domain/capkey.hpp"

#include "agentty/domain/catalog.hpp"
#include "agentty/provider/selection.hpp"
#include "agentty/provider/registry.hpp"

using namespace agentty;



static bool weak(std::string_view id) { return is_weak_model(id); }

TEST_CASE("max output: a declared cap beats the family table") {
    // 表上说海库是8192;提供方说128,000. 提供方获胜.
    CatalogBatch b;
    b.set_max_output("maxoutprov/claude-haiku-9-9", 128000);
    b.merge_max_output("maxout-bare-model", 32768);
    b.commit();
    set_caps_provider_scope("maxoutprov");
    CHECK(max_output_tokens_for("claude-haiku-9-9") == 128000);
    // [1m]变体型号相同.
    CHECK(max_output_tokens_for("claude-haiku-9-9[1m]") == 128000);
    CHECK(max_output_tokens_for("maxout-bare-model") == 32768);
    // 巨额申报被封顶 所以转弯时,不计费384千元
    set_catalog_max_output("maxoutprov/huge-out-model", 384000);
    CHECK(max_output_tokens_for("huge-out-model") == 128000);
    // 没有申报:桌子仍然答覆。
    CHECK(max_output_tokens_for("claude-haiku-4-5") == 8192);
    set_caps_provider_scope("");
}

TEST_CASE("catalog batch: same facts as the per-fact setters, published once") {
    // 独特的ID,所以这个测试不能与其它人在此过程中发生碰撞.
    CatalogBatch b;
    b.set_reasoning("batchprov/batch-model-a", true);
    b.set_effort_set("batchprov/batch-model-a", 0b0110);
    b.set_context_window("batchprov/batch-model-a", 128000);
    b.set_context_window("batchprov/batch-model-z", 0);       // 被忽略, 像是设置器
    // 赤色钥匙:两个来源同意,第三个赤色钥匙不同意 ~中毒.
    b.merge_reasoning("batch-model-agree", true);
    b.merge_reasoning("batch-model-agree", true);
    b.merge_reasoning("batch-model-clash", true);
    b.merge_reasoning("batch-model-clash", false);
    b.merge_context_window("batch-model-agree", 32000);
    b.merge_endpoint_context_window("http://localhost:5199/v1", "batch-local", 8192);

    const auto epoch = caps_epoch();
    CHECK(catalog_reasoning_for("batch-model-a", "batchprov") == -1);   // 还没
    b.commit();
    CHECK(caps_epoch() != epoch);                // 缓存 看见了变化

    CHECK(catalog_reasoning_for("batch-model-a", "batchprov") == 1);
    CHECK(catalog_effort_set_for("batch-model-a", "batchprov") == 0b0110);
    CHECK(catalog_context_window_for("batch-model-a", "batchprov") == 128000);
    CHECK(catalog_context_window_for("batch-model-z", "batchprov") == 0);
    CHECK(catalog_reasoning_for("batch-model-agree") == 1);
    CHECK(catalog_reasoning_for("batch-model-clash") == -1);   // 下毒了,我给药了
    CHECK(catalog_context_window_for("batch-model-agree") == 32000);
    CHECK(endpoint_context_window_for("batch-local", "127.0.0.1", 5199) == 8192);
}

TEST_CASE("catalog batch: an 80k-fact load is linear, not quadratic") {
    // models.devXX 加载号机车曾经按事实复制了整注册表号机,
    // 启动时将一个芯片钉上几分钟,一次批次复制.
    CatalogBatch b;
    for (int i = 0; i < 80000; ++i)
        b.set_reasoning("bulkprov/bulk-model-" + std::to_string(i), (i & 1) != 0);
    const auto t0 = std::chrono::steady_clock::now();   // stopwatch: perf 守护
    b.commit();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    CHECK(catalog_reasoning_for("bulk-model-79999", "bulkprov") == 1);
    CHECK(catalog_reasoning_for("bulk-model-0", "bulkprov") == 0);
    // 慷慨为 Debug/sanitizers 4倍数是分钟。
    CHECK_MESSAGE(ms < 5000, "80k-fact commit took " << ms << " ms");
}

TEST_CASE("claude never weak") {
    // 每个主机 Claude 家庭很强壮,世代相传.
    CHECK(!weak("claude-opus-4-5"));
    CHECK(!weak("claude-sonnet-4-5-20250101"));
    CHECK(!weak("claude-haiku-4-5"));
    CHECK(!weak("claude-3-5-haiku-20241022"));
    CHECK(!weak("claude-opus-4-5[1m]"));      // 前面的后缀被剥掉
}

TEST_CASE("small local coder weak") {
    // 虫子报告模型及其小弟妹.
    CHECK(weak("qwen2.5-coder:7b"));
    CHECK(weak("qwen2.5-coder:3b"));
    CHECK(weak("qwen2.5:7b"));
    CHECK(weak("codellama:7b"));
    CHECK(weak("deepseek-coder:6.7b"));
    CHECK(weak("phi3:3.8b"));
    CHECK(weak("gemma2:9b"));
    CHECK(weak("starcoder2:7b"));
    CHECK(weak("tinyllama:1.1b"));
    CHECK(weak("smollm2:1.7b"));
}

TEST_CASE("large models strong") {
    // 大型模型,带有 NO 弱项 -family 信号跟随工具 模式定义 可靠无碍.
    CHECK(!weak("llama3.1:70b"));
    CHECK(!weak("mixtral:8x22b"));            // 22b匹配,没有弱家
}

TEST_CASE("weak family wins over size") {
    // 已知弱 / 编码-only 家庭 JSON 泄露工具 ANY大小—
    // —弱 -family信号击败生 QQ 14B大小快捷键.
    CHECK(weak("qwen2.5-coder:14b"));         // 现场案件
    CHECK(weak("qwen2.5-coder:32b"));
    CHECK(weak("codellama:34b"));
    CHECK(weak("deepseek-coder:33b"));
}

TEST_CASE("tool trained families strong") {
    // Tool-trained 家庭即使规模较小,也十分强大。
    CHECK(!weak("qwen3:8b"));
    CHECK(!weak("llama3.1:8b"));
    CHECK(!weak("llama3.3:70b"));
    CHECK(!weak("mistral:7b"));
    CHECK(!weak("mistral-small:24b"));
    CHECK(!weak("ministral:8b"));
    CHECK(!weak("command-r:35b"));
    CHECK(!weak("hermes3:8b"));
    CHECK(!weak("firefunction-v2"));
    CHECK(!weak("functionary-small:7b"));
    CHECK(!weak("devstral:24b"));
    CHECK(!weak("codestral:22b"));
    CHECK(!weak("granite3.1-dense:8b"));
    CHECK(!weak("deepseek-v3"));
    CHECK(!weak("deepseek-r1:32b"));
}

TEST_CASE("tiny strong family still weak") {
    // 即使是一个工具-trained家族,在明显很小的时候(QQ 3B)也是不可靠的.
    CHECK(weak("llama3.1:1b"));
    CHECK(weak("qwen3:1.7b"));               // 1b 匹配自 "1.7b" 看见注释
    CHECK(weak("granite3.1-moe:3b"));
}

TEST_CASE("unknown id defaults strong") {
    // 主持人 OpenAI-family 编号为无 size/family 信号是假设有能力的。
    CHECK(!weak("gpt-4o"));
    CHECK(!weak("gpt-4o-mini"));
    CHECK(!weak("o1-preview"));
    CHECK(!weak("grok-2"));
    CHECK(!weak(""));
    CHECK(!weak("some-random-hosted-model"));
}

TEST_CASE("bare size signal") {
    // 没有被认出的家庭,但规模小的标签 ~ 弱;大 ~ 强。
    CHECK(weak("mystery:7b"));
    CHECK(!weak("mystery:70b"));
    // 一个单词中的'b'(不是参数标记)不得被错误读取.
    CHECK(!weak("turbo-model"));             // 'b' 输入 "turbo" 忽略
    CHECK(!weak("bigbird"));                 // 无主要数字
}

TEST_CASE("max output tokens") {
    // Claude 4.x Sonnet/Opus + 64 000(编辑) -truncation 固定).
    CHECK(max_output_tokens_for("claude-sonnet-4-5") == 64000);
    CHECK(max_output_tokens_for("claude-opus-4-5") == 64000);
    CHECK(max_output_tokens_for("claude-sonnet-4-5-20250101") == 64000);
    CHECK(max_output_tokens_for("claude-opus-4-5[1m]") == 64000);
    // 每一代的海库都8千米
    CHECK(max_output_tokens_for("claude-haiku-4-5") == 8192);
    CHECK(max_output_tokens_for("claude-3-5-haiku-20241022") == 8192);
    // 遗存 3.x Sonnet/Opus 封顶8k.
    CHECK(max_output_tokens_for("claude-3-5-sonnet-20241022") == 8192);
    CHECK(max_output_tokens_for("claude-3-opus-20240229") == 8192);
    // 无 -Claude (当地/ OpenAI-compat /未知) ^ 保守默认.
    CHECK(max_output_tokens_for("qwen2.5-coder:7b") == 16384);
    CHECK(max_output_tokens_for("gpt-4o") == 16384);
    CHECK(max_output_tokens_for("") == 16384);
    // 2026号旗舰航道( Fable/Mythos 5: opus (英语).
    // -class 最高额64千 不 16k 非 类型 -Claude 默认值。中位的后退守护
    // -turn 由下列因素导致的截断: "fable" 不是一个公认的家庭象征。
    CHECK(max_output_tokens_for("claude-fable-5") == 64000);
    CHECK(max_output_tokens_for("claude-mythos-5") == 64000);
    CHECK(max_output_tokens_for("claude-fable-5[1m]") == 64000);
}

TEST_CASE("flagship lane caps") {
    using agentty::ModelCapabilities;
    // Fable/Mythos 5必须解码为 KNOWN Claude 家庭(因为
    // 她们会被视为 unknown/non-Claude :输出上限错误,不努力,不β).
    for (const char* id : {"claude-fable-5", "claude-mythos-5"}) {
        const auto c = ModelCapabilities::from_id(id);
        CHECK(c.is_known_family());
        CHECK(c.is_flagship());
        CHECK(c.generation == 5);
        CHECK(c.generation_4_or_later);   // 门上下文-management β
        CHECK(!c.is_weak_tool_user());
        // 努力控制旗舰航道为GA,全梯.
        CHECK(c.supports_effort());
        CHECK(c.supports_effort_max());
        CHECK(c.supports_effort_xhigh());
    }
    // [1m]扩展的-context后缀在解码前被剥离.
    CHECK(ModelCapabilities::from_id("claude-fable-5[1m]").is_fable());
    CHECK(ModelCapabilities::from_id("claude-fable-5[1m]").extended_context_1m);
}

TEST_CASE("gpt5 codex caps") {
    using agentty::ModelCapabilities;
    using agentty::Effort;
    using agentty::effort_wire_for;

    // gpt-5.x (英语: ChatGPT/Codex 答复)解码为 Family::Gpt 努力地努力着
    {
        const auto c = ModelCapabilities::from_id("gpt-5.6-sol");
        CHECK(c.is_gpt());
        CHECK(c.is_known_family());
        CHECK(c.generation == 5);
        CHECK(c.revision == 6);
        CHECK(c.supports_effort());
        CHECK(c.supports_effort_xhigh());
        CHECK(c.supports_effort_max());     // gpt-5.6旗舰线最大
        CHECK(!c.is_weak_tool_user());
    }
    // gpt-5.4 支持最高为x高但最大为 不 的梯度。
    {
        const auto c = ModelCapabilities::from_id("gpt-5.4");
        CHECK(c.is_gpt());
        CHECK(c.generation == 5);
        CHECK(c.revision == 4);
        CHECK(c.supports_effort());
        CHECK(c.supports_effort_xhigh());
        CHECK(!c.supports_effort_max());
        // A 过期 `max` 选择降解为 NEAREST 支持的等级——
        // 此处为x高,而不是400(或以上) -shooting 下至高处).
        CHECK(effort_wire_for(Effort::Max, c) == "xhigh");
        CHECK(effort_wire_for(Effort::Xhigh, c) == "xhigh");
    }
    // 平原`gpt-5`(无修订)仍在努力.
    {
        const auto c = ModelCapabilities::from_id("gpt-5");
        CHECK(c.is_gpt());
        CHECK(c.generation == 5);
        CHECK(c.supports_effort());
    }
    // `minimal` 谓 OpenAI 的 gpt-5+ 下层。 它必须是在
    // 派生 Gpt 家族的梯子(gen QQ 5),地图到 "minimal" 传输 值,
    // 请坐 下方 低点,这样 Minimal 请求永远不要向上转。
    {
        const auto c = ModelCapabilities::from_id("gpt-5.6-sol");
        CHECK(effort_wire_for(Effort::Minimal, c) == "minimal");
        const auto ladder = agentty::available_efforts(c);
        // 自动 + 关闭 + 最小 + 低 + 中 + 高 + x 高 → 最大
        REQUIRE(ladder.size() >= 4);
        CHECK(ladder[0] == Effort::Auto);
        CHECK(ladder[1] == Effort::None);
        CHECK(ladder[2] == Effort::Minimal);
        CHECK(ladder[3] == Effort::Low);
        // A Minimal 请求保持最小值( 接近 -or-below —— 报告内容。
        CHECK(agentty::clamp_effort(Effort::Minimal, c) == Effort::Minimal);
    }
    // 缩写:Gpt-5.4 -mini 输出上限为大 -output 64k(不是16k非) (中文(简体) ).
    // -Claude 默认).
    CHECK(agentty::max_output_tokens_for("gpt-5.4-mini") == 64000);
    CHECK(agentty::max_output_tokens_for("gpt-5.6-sol") == 64000);

    // CRITICAL 无 -regression : gpt-4o / gpt 4.1
    // / gbt-3.5 必须不是 被Gpt-5家族逻辑所抓住, OpenAI-compat
    // (无功,16k 盖) 老gpt* 过 OpenAI-compat 传输层 依靠这个
    for (const char* legacy : {"gpt-4o", "gpt-4.1", "gpt-4o-mini", "gpt-3.5-turbo"}) {
        const auto c = ModelCapabilities::from_id(legacy);
        CHECK(!c.is_gpt());
        CHECK(!c.supports_effort());
        CHECK(agentty::max_output_tokens_for(legacy) == 16384);
    }
}

// Anthropic 思维 -mode 选择: 4.5 -and-earlier 需要启用的
// 遗留 + budget_tokens 正文; 4.6+/4.7/4.8 +旗舰5需要适应性.
// 发送适应性为4.5400s("adaptive thinking is not supported on this model")
// ——这个针的准确bug.
TEST_CASE("anthropic thinking: which models need an explicit off") {
    using MC = agentty::ModelCapabilities;
    CHECK(MC::from_id("claude-opus-5").needs_explicit_thinking_off());
    CHECK(!MC::from_id("claude-opus-5-5").needs_explicit_thinking_off());
    CHECK(MC::from_id("claude-opus-5-5").thinking_always_on());
    CHECK(MC::from_id("claude-fable-5").thinking_always_on());
    CHECK(!MC::from_id("claude-opus-5").thinking_always_on());
    CHECK(!MC::from_id("claude-haiku-5-5").thinking_always_on());
    CHECK(!MC::from_id("claude-opus-4-8").thinking_always_on());
    // 只有Sonnet 5.5 取between_tools;其余400 上.
    CHECK(MC::from_id("claude-sonnet-5-5").supports_between_tools_thinking());
    CHECK(!MC::from_id("claude-opus-5-5").supports_between_tools_thinking());
    CHECK(!MC::from_id("claude-haiku-5-5").supports_between_tools_thinking());
    // 前缀-bound思维块:与Zed相同的列表.
    for (auto id : {"claude-opus-5-5", "claude-sonnet-5-5", "claude-haiku-5-5",
                    "claude-fable-5-1"})
        CHECK(MC::from_id(id).binds_thinking_to_prefix());
    for (auto id : {"claude-opus-5", "claude-fable-5", "claude-mythos-5-1",
                    "claude-opus-4-8"})
        CHECK(!MC::from_id(id).binds_thinking_to_prefix());
    CHECK(MC::from_id("claude-haiku-5-5").needs_explicit_thinking_off());
    // 松网5认为当田野被忽略(检查为直播).
    CHECK(MC::from_id("claude-sonnet-5").needs_explicit_thinking_off());
    CHECK(!MC::from_id("claude-sonnet-5-5").needs_explicit_thinking_off());
    CHECK(MC::from_id("claude-haiku-6").needs_explicit_thinking_off());
    // 默认关闭旧模型: 将球场放出去就够了.
    CHECK(!MC::from_id("claude-opus-4-8").needs_explicit_thinking_off());
    CHECK(!MC::from_id("claude-sonnet-4-6").needs_explicit_thinking_off());
    // 寓言 / 神话 默认认为,但400 在"disabled".
    CHECK(!MC::from_id("claude-fable-5").needs_explicit_thinking_off());
    CHECK(!MC::from_id("claude-mythos-5").needs_explicit_thinking_off());
    CHECK(!MC::from_id("gpt-5.5").needs_explicit_thinking_off());
    CHECK(MC::from_id("claude-mythos-preview").thinking_always_on());
}

// 电话 回退 规则必须匹配 Anthropic 单位 -model 表格(文件:思维)
// 排查说明中的“各模型的思考支持”。
TEST_CASE("anthropic thinking: id rules match the documented table") {
    using MC = agentty::ModelCapabilities;
    enum Off { Omit, Disabled, Between, AlwaysOn };
    auto how = [](const char* id) {
        const auto c = MC::from_id(id);
        if (c.needs_explicit_thinking_off())     return Disabled;
        if (c.supports_between_tools_thinking()) return Between;
        if (c.thinking_always_on())              return AlwaysOn;
        return Omit;
    };
    for (auto id : {"claude-fable-5-1", "claude-mythos-5-1", "claude-fable-5",
                    "claude-mythos-5", "claude-opus-5-5"})
        CHECK(how(id) == AlwaysOn);
    for (auto id : {"claude-opus-5", "claude-sonnet-5", "claude-haiku-5-5"})
        CHECK(how(id) == Disabled);
    CHECK(how("claude-sonnet-5-5") == Between);
    for (auto id : {"claude-opus-4-8", "claude-opus-4-7", "claude-opus-4-6",
                    "claude-sonnet-4-6", "claude-opus-4-5", "claude-haiku-4-5",
                    "claude-sonnet-4-5"})
        CHECK(how(id) == Omit);
}

// /v1/models宣布的比id规则更强,两者皆有.
TEST_CASE("anthropic thinking: declared types win over the id") {
    using MC = agentty::ModelCapabilities;
    MC c = MC::from_id("claude-opus-5-5");          // 说: 永远在
    c.thinking_types = MC::kThinkDeclared | MC::kThinkAdaptive | MC::kThinkDisabled;
    CHECK(c.needs_explicit_thinking_off());
    CHECK(!c.thinking_always_on());
    c = MC::from_id("claude-sonnet-5");              // id说: 已禁用
    c.thinking_types = MC::kThinkDeclared | MC::kThinkAdaptive | MC::kThinkBetweenTools;
    CHECK(!c.needs_explicit_thinking_off());
    CHECK(c.supports_between_tools_thinking());
    c.thinking_types = MC::kThinkDeclared | MC::kThinkAdaptive;
    CHECK(c.thinking_always_on());
    // 仅延长(4.5):遗留预算模式,因省略而取消。
    c = MC::from_id("claude-opus-4-5");
    c.thinking_types = MC::kThinkDeclared | MC::kThinkEnabled | MC::kThinkDisabled;
    CHECK(!c.uses_adaptive_thinking());
    CHECK(!c.thinking_always_on());
}

// 声明已送达 resolved_caps 人类的范畴,
// 永远 -on 模型没有 "off" 穿在梯子上,
TEST_CASE("anthropic thinking: declared + learned facts reach resolved_caps") {
    using MC = agentty::ModelCapabilities;
    set_anthropic_decls({{"claude-sonnet-7",
        AnthropicDecl{static_cast<std::uint8_t>(MC::kThinkDeclared | MC::kThinkAdaptive),
                      static_cast<std::uint8_t>(effort_bit(Effort::Low) | effort_bit(Effort::High))}}});
    const auto c = resolved_caps("claude-sonnet-7", "anthropic");
    CHECK(c.thinking_always_on());
    const auto ladder = available_efforts(c);
    CHECK(std::find(ladder.begin(), ladder.end(), Effort::None) == ladder.end());
    CHECK(std::find(ladder.begin(), ladder.end(), Effort::Medium) == ladder.end());
    CHECK(clamp_effort(Effort::None, c) == Effort::Low);
    CHECK(effort_wire_for(Effort::None, c) == "low");
    // 不适用于另一供应商的同一身份证明。
    CHECK(!resolved_caps("claude-sonnet-7", "openrouter").thinking_declared());

    // 拒绝 "disabled" 名称 between_tools 已经学会了。
    const auto base = MC::from_id("claude-haiku-7");
    CHECK(base.needs_explicit_thinking_off());
    learn_anthropic_thinking("claude-haiku-7",
        static_cast<std::uint8_t>(MC::kThinkDeclared | MC::kThinkAdaptive | MC::kThinkDisabled),
        MC::kThinkDisabled, MC::kThinkBetweenTools);
    const auto l = resolved_caps("claude-haiku-7", "anthropic");
    CHECK(!l.needs_explicit_thinking_off());
    CHECK(l.supports_between_tools_thinking());
    CHECK(!l.thinking_always_on());
}

// 总是 -on 模特儿永远不给 所以不能靠手来挑
TEST_CASE("anthropic thinking: no off on a model that always thinks") {
    for (auto id : {"claude-opus-5-5", "claude-fable-5", "claude-mythos-5-1"}) {
        const auto c = resolved_caps(id, "anthropic");
        const auto ladder = available_efforts(c);
        CHECK(!ladder.empty());
        CHECK(std::find(ladder.begin(), ladder.end(), Effort::None) == ladder.end());
        CHECK(clamp_effort(Effort::None, c) != Effort::None);
    }
    // 能够关闭它的模式仍然提供。
    for (auto id : {"claude-opus-5", "claude-sonnet-5-5", "claude-opus-4-8"}) {
        const auto ladder = available_efforts(resolved_caps(id, "anthropic"));
        CHECK(std::find(ladder.begin(), ladder.end(), Effort::None) != ladder.end());
    }
}

TEST_CASE("anthropic thinking mode: adaptive vs enabled by revision") {
    using agentty::ModelCapabilities;
    auto adaptive = [](const char* id) {
        return ModelCapabilities::from_id(id).uses_adaptive_thinking();
    };
    // LEGACY 启用+ budget_tokens (适应400):
    CHECK(!adaptive("claude-opus-4-5"));
    CHECK(!adaptive("claude-opus-4-5[1m]"));
    CHECK(!adaptive("claude-sonnet-4-5"));
    CHECK(!adaptive("claude-opus-4-1"));
    CHECK(!adaptive("claude-sonnet-4-0"));
    // ADAPTIVE (enabled/budget_tokens 将400:
    CHECK(adaptive("claude-opus-4-6"));
    CHECK(adaptive("claude-opus-4-7"));
    CHECK(adaptive("claude-opus-4-8"));
    CHECK(adaptive("claude-opus-4-8[1m]"));
    CHECK(adaptive("claude-sonnet-4-6"));
    // 旗舰5号航道适应性强.
    CHECK(adaptive("claude-opus-5"));
    CHECK(adaptive("claude-fable-5"));
    CHECK(adaptive("claude-mythos-5"));

    // COMPOSITION INVARIANT : Opus 4.5 是一个扩展的
    // -thinking-only (已启用) -mode ) 模型的这个 ALSO
    // 支持 output_config.effort - 电话 传输层 努力创造 WITH
    // budget_tokens 将两个事实都钉住,这样一个顶点不能默默地降低工作级别。
    {
        const auto c45 = ModelCapabilities::from_id("claude-opus-4-5");
        CHECK(!c45.uses_adaptive_thinking());  // 启用 + budget_tokens
        CHECK(c45.supports_effort());          // 和 努力构成
        // 启用了 Sonnet 4.5 -mode 却有 不 支助努力——因此 传输层 必须
        // 发送 budget_tokens 无需 output_config.effort.
        const auto s45 = ModelCapabilities::from_id("claude-sonnet-4-5");
        CHECK(!s45.uses_adaptive_thinking());
        CHECK(!s45.supports_effort());
    }
}

TEST_CASE("compat reasoning effort (chat wire)") {
    using agentty::ModelCapabilities;
    using agentty::Effort;
    using agentty::effort_wire_for;
    using agentty::cycle_effort;

    // OpenAI-Chat-wire 推理模型是: 不 输入 Claude/GPT 家的梯子,
    // 但揭开顶端 -level reasoning_effort 枚举 (减) | 药 | ——高处).
    for (const char* id : {"mistral-small-latest", "mistral-medium-latest",
                           "mistral-small-3-2", "mistral-medium-3-5",
                           "deepseek-reasoner", "deepseek-r1",
                           "grok-4", "grok-4-fast", "grok-3-mini",
                           "gemini-2.5-flash-thinking",
                           "gpt-oss-120b", "gpt-oss:20b",
                           "o1", "o3", "o4-mini"}) {
        const auto c = ModelCapabilities::from_id(id);
        CHECK(c.reasoning_compat);
        CHECK(c.supports_effort());
        // 3-level 枚举 仅此值——此值为传输没有最大值/x高值.
        CHECK(!c.supports_effort_max());
        CHECK(!c.supports_effort_xhigh());
        // `minimal` 是一个gpt-5, -only 级- 相配模型不得 揭开它,
        // 将有四百个,他们的梯子是低的, | 中号 | ——高处).
        CHECK((agentty::effort_set_of(c) & agentty::effort_bit(Effort::Minimal)) == 0);
        CHECK(!c.is_known_family());   // 垂直地保持到家庭阶梯
        // A 过期 Max/Xhigh 选择降解为 `high` 而不是400宁。
        CHECK(effort_wire_for(Effort::Max, c)    == "high");
        CHECK(effort_wire_for(Effort::Xhigh, c)  == "high");
        CHECK(effort_wire_for(Effort::None, c)   == "");
        // 选择面板可以循环利用这些模型。
        CHECK(cycle_effort(Effort::None, +1, c) != Effort::None);
    }

    // 无 -Mistral 模拟模型保持全低 | 中号 | 高档 枚举.
    for (const char* id : {"deepseek-reasoner", "grok-3-mini", "o3",
                           "gemini-2.5-flash-thinking", "gpt-oss-120b"}) {
        const auto c = ModelCapabilities::from_id(id);
        CHECK(!c.effort_high_only);
        CHECK(effort_wire_for(Effort::Medium, c) == "medium");
        CHECK(effort_wire_for(Effort::Low, c)    == "low");
    }

    // 雾 -platform 模型为 a BINARY 枚举 - {无,高}只;
    // API 400s 开着 low/medium ("supported values: [high, none]"。
    // 任何活动级必须倒塌到 `high` 高高低,高高低低,夹住高低
    for (const char* id : {"mistral-small-latest", "mistral-medium-latest",
                           "mistral-medium-3-5", "mistral-small-3-2"}) {
        const auto c = ModelCapabilities::from_id(id);
        CHECK(c.effort_high_only);
        CHECK(effort_wire_for(Effort::Low, c)    == "high");
        CHECK(effort_wire_for(Effort::Medium, c) == "high");
        CHECK(effort_wire_for(Effort::High, c)   == "high");
        CHECK(effort_wire_for(Effort::Max, c)    == "high");
        CHECK(effort_wire_for(Effort::None, c)   == "");
        CHECK(agentty::clamp_effort(Effort::Medium, c) == Effort::High);
        CHECK(agentty::clamp_effort(Effort::None, c)   == Effort::None);
        // 梯子会带路 `auto` 然后,那一个,
        // ON 运行此模型接受。
        const auto ladder = agentty::available_efforts(c);
        REQUIRE(ladder.size() == 3);
        CHECK(ladder[0] == Effort::Auto);
        CHECK(ladder[1] == Effort::None);
        CHECK(ladder[2] == Effort::High);
        // 在一个 -rung 中点梯度 IS 汽车是高的
        CHECK(agentty::auto_base_effort(c) == Effort::High);
        CHECK(effort_wire_for(Effort::Auto, c) == "high");
    }

    // 无 -reasoning / 本地语 -reasoning 共享前缀的行不得 亮点: grok
    // -code-fast (编码器),深层搜索器 -chat ,双子座2.5 -pro (没有思想后缀),
    // 以及——关键是——大法官*,原因何在. NATIVELY 财务报告和已审计财务报表
    // REJECTS reasoning_effort (422.) 代码是一种代码模型,没有推理.
    for (const char* id : {"grok-code-fast-1", "magistral-medium-latest",
                           "magistral-small-latest", "codestral-latest",
                           "deepseek-chat", "gemini-2.5-pro"}) {
        const auto c = ModelCapabilities::from_id(id);
        CHECK(!c.reasoning_compat);
        CHECK(!c.supports_effort());
        CHECK(effort_wire_for(Effort::High, c) == "");
    }

    // Claude / GPT 家为家 -gated - 共和党人 - 共通党人 标志
    // 一定要留下来 OFF 他们虽然得享受, DO 支持通过自己的梯子努力。
    for (const char* id : {"claude-opus-4-5", "gpt-5.6-sol"}) {
        const auto c = ModelCapabilities::from_id(id);
        CHECK(!c.reasoning_compat);
        CHECK(c.supports_effort());   // 家庭,而不是同族的标志
    }
}

TEST_CASE("per-model reasoning override registry") {
    using agentty::ModelCapabilities;
    using agentty::resolved_caps;
    using agentty::set_reasoning_override;
    using agentty::clear_reasoning_override;
    using agentty::clear_reasoning_overrides;
    using agentty::reasoning_override_for;

    clear_reasoning_overrides();

    // 没有覆盖 ~ resolved_caps 匹配纯推论。
    //   代码 -latest : 一个代码模型, 不 理由 而不是比喻 说得没错
    //   misttral-small-latest: Misttral Small 4,可调整推理 → comparate.
    CHECK(!resolved_caps("codestral-latest").reasoning_compat);
    CHECK(resolved_caps("mistral-small-latest").reasoning_compat);
    CHECK(reasoning_override_for("codestral-latest") == -1);

    // 强制 ON 模型的 目录 已经 不 承认为理性者。
    set_reasoning_override("codestral-latest", true);
    CHECK(reasoning_override_for("codestral-latest") == 1);
    {
        const auto c = resolved_caps("codestral-latest");
        CHECK(c.reasoning_compat);
        CHECK(c.supports_effort());
        CHECK(!c.supports_effort_max());   // 仍为3-level 枚举
    }

    // 强制 OFF 模型推论 WOULD 亮( 弥撒小4)。
    set_reasoning_override("mistral-small-latest", false);
    CHECK(reasoning_override_for("mistral-small-latest") == 0);
    {
        const auto c = resolved_caps("mistral-small-latest");
        CHECK(!c.reasoning_compat);
        CHECK(!c.supports_effort());
    }

    // 强制不得 泄露到家里去 说不定是被你害的 -gated Claude/GPT 车道。
    set_reasoning_override("claude-opus-4-5", false);
    {
        const auto c = resolved_caps("claude-opus-4-5");
        CHECK(!c.reasoning_compat);
        CHECK(c.supports_effort());   // 家族梯级胜出; 忽略了覆盖
    }

    // 清点一个可以恢复对ID的推断
    clear_reasoning_override("mistral-small-latest");
    CHECK(reasoning_override_for("mistral-small-latest") == -1);
    CHECK(resolved_caps("mistral-small-latest").reasoning_compat);
    // 另一处覆盖仍然保留(强制打开代码)。
    CHECK(resolved_caps("codestral-latest").reasoning_compat);

    clear_reasoning_overrides();   // 将全局状态清除用于其他测试
    CHECK(reasoning_override_for("codestral-latest") == -1);
}

TEST_CASE("live-catalog reasoning declaration") {
    using agentty::resolved_caps;
    using agentty::set_catalog_reasoning;
    using agentty::catalog_reasoning_for;
    using agentty::set_reasoning_override;
    using agentty::clear_reasoning_overrides;
    using agentty::Effort;

    clear_reasoning_overrides();

    // 密斯特尔现场直播 目录 说那段错误 -medium-2505 已经 不 原因(
    // 400s on) 时间: reasoning_effort )尽管id 子字符串匹配
    // from_id's "mistral-medium" 规则—— 目录 一定要赢
    set_catalog_reasoning("mistral-medium-2505", false);
    CHECK(catalog_reasoning_for("mistral-medium-2505") == 0);
    CHECK(!resolved_caps("mistral-medium-2505").supports_effort());

    // 和法官们 -medium-latest 执行 现在接受吧,
    // 虽然 from_id 不包括治安法官——再次包括 目录 赢了。
    set_catalog_reasoning("magistral-medium-latest", true);
    CHECK(catalog_reasoning_for("magistral-medium-latest") == 1);
    {
        const auto c = resolved_caps("magistral-medium-latest");
        CHECK(c.supports_effort());
        // 仍然是一个迷雾 -platform 编号 ^ 二进制程序 {没有,高}梯子。
        CHECK(c.effort_high_only);
        CHECK(agentty::effort_wire_for(Effort::Medium, c) == "high");
    }

    // 用户的明显值( =% 1) -model 覆盖的级别高于 目录.
    set_reasoning_override("magistral-medium-latest", false);
    CHECK(!resolved_caps("magistral-medium-latest").supports_effort());
    clear_reasoning_overrides();
    CHECK(resolved_caps("magistral-medium-latest").supports_effort());

    // 重置目录条目,所以其他测试都可以看到推论行为.
    set_catalog_reasoning("mistral-medium-2505", true);
    set_catalog_reasoning("magistral-medium-latest", false);
}

TEST_CASE("models.dev bare-key collision poisons to no-info") {
    using agentty::merge_catalog_reasoning;
    using agentty::catalog_reasoning_for;
    using agentty::resolved_caps;

    // 真正的错误: models.dev 将 ~50 个提供者合并为一个文档,
    // agentty 记录每个模型的推理事实 BARE 末尾 也 (
    // 十字架) (十字架 -provider 查找名称空间。
    // 不同的聚合符对 相同 赤色的身份证 -- -- e.g 将一个旧的米斯
    // 特拉尔大号列为推理模型 犬科供应商将其列为指示 -only 最后
    // 一个 -writer-wins 记录点燃了幻影 off/high 努力
    // 标签 3号迷雾大号上从未出现过任何想法
    const std::string bare = "mistral-large-2402";

    // 第一来源:指示-only(真实的)。
    merge_catalog_reasoning(bare, false);
    CHECK(catalog_reasoning_for(bare) == 0);

    // 第二个来源 DISAGREES (声称原因) —— 钥匙被毒死到
    // "no info" (-1) 而不是翻转成幻象 -reasoning 没错
    merge_catalog_reasoning(bare, true);
    CHECK(catalog_reasoning_for(bare) == -1);

    // 毒药一旦下毒,它就一直模糊不清——后来的作家们无法复活它,
    // 所以一个流浪的集合器永远无法重用-light的标签.
    merge_catalog_reasoning(bare, true);
    merge_catalog_reasoning(bare, false);
    CHECK(catalog_reasoning_for(bare) == -1);

    // 没有 目录 声明状态,解析为 id -inference:
    // "mistral-large" 谓 不 输入 infer_reasoning_compat
    // 推理列表,所以努力 标签 正确缺席。
    CHECK(!resolved_caps(bare).supports_effort());

    // 同意来源 不 毒药——一个有共识支持的事实得以存在。
    const std::string agree = "deepseek-v4-flash";
    merge_catalog_reasoning(agree, true);
    merge_catalog_reasoning(agree, true);
    CHECK(catalog_reasoning_for(agree) == 1);
}

TEST_CASE("capability tiers") {
    using T = ModelCapabilities::Tier;
    auto tier = [](std::string_view id) { return ModelCapabilities::tier_for(id); };
    // Anthropic 车道订购与供应商自己的命名相符。
    CHECK(tier("claude-haiku-4-5")        == T::Cheap);
    CHECK(tier("claude-3-5-haiku-20241022") == T::Cheap);
    CHECK(tier("claude-sonnet-4-5-20250101") == T::Mid);
    CHECK(tier("claude-opus-4-5")         == T::Flagship);
    // OpenAI: mini/nano 降级到廉价车道;全格普-5.x为 flagship/mid.
    CHECK(tier("gpt-5-mini")   == T::Cheap);
    CHECK(tier("gpt-4o-mini")  == T::Cheap);
    CHECK(tier("gpt-5-nano")   == T::Cheap);
    CHECK(tier("gpt-5.6")      == T::Flagship);
    // 当地模式薄弱,即使有廉价的-ish id,也从未处于高于弱的级别。
    CHECK(tier("qwen2.5-coder:7b") == T::Weak);
    // 点菜是真正的总订单
    CHECK(T::Weak < T::Cheap);
    CHECK(T::Cheap < T::Mid);
    CHECK(T::Mid < T::Flagship);
}

static ModelInfo mi(std::string_view id) {
    ModelInfo m;
    m.id = ModelId{std::string{id}};
    return m;
}

TEST_CASE("router hardening nonchat and oseries") {
    using T = ModelCapabilities::Tier;
    // 无 -chat 资产 a 原始资产 OpenAI /v1/models 垃圾列表必须 绝不 被选中。
    for (const char* asset : {"text-embedding-3-small", "text-embedding-3-large",
                              "dall-e-3", "whisper-1", "tts-1", "tts-1-hd",
                              "omni-moderation-latest", "gpt-4o-transcribe",
                              "gpt-4o-realtime-preview", "gpt-image-1"}) {
        CHECK(!is_dispatchable_model(asset));
    }
    // 真正的聊天模式仍然可以发送。
    for (const char* chat : {"gpt-5", "gpt-5-mini", "claude-haiku-4-5",
                             "claude-opus-4-5", "o1", "o3-mini", "gpt-4o"}) {
        CHECK(is_dispatchable_model(chat));
    }
    // o-series 基数 o1/o3 价格推理模型(旗舰,从未被选为 "cheap"
    // (b) 与《京都议定书》有关的 -mini 变体 真正便宜的。
    CHECK(ModelCapabilities::tier_for("o1")      == T::Flagship);
    CHECK(ModelCapabilities::tier_for("o3")      == T::Flagship);
    CHECK(ModelCapabilities::tier_for("o1-mini") == T::Cheap);
    CHECK(ModelCapabilities::tier_for("o3-mini") == T::Cheap);

    // End-to-end : 一个现实的原始 OpenAI。路由器必须跳过每件
    // 资产 + o1 并真正降落在 -cheap 长5 -mini 而不是嵌入式的。
    std::vector<ModelInfo> raw = {
        mi("gpt-5"), mi("gpt-5-mini"), mi("o1"),
        mi("text-embedding-3-small"), mi("dall-e-3"), mi("whisper-1"),
        mi("gpt-4o-realtime-preview")};
    for (auto& m : raw)
        if (m.id.value.rfind("gpt-5", 0) == 0) m.context_window = 272000;
    const std::string picked = cheapest_capable_model("gpt-5", raw);
    CHECK(picked == "gpt-5-mini");
    CHECK(is_dispatchable_model(picked));

    // 如果 仅 便宜的考生没有 -chat 资产,
    // 保留母体(不回归,从不向嵌入模型路径).
    std::vector<ModelInfo> assets_only = {
        mi("gpt-5"), mi("text-embedding-3-large"), mi("dall-e-3")};
    CHECK(cheapest_capable_model("gpt-5", assets_only) == "gpt-5");
}

TEST_CASE("cheapest capable router") {
    // 典型的Anthropic 池:母体Opus,更便宜的Sonnet + Haiku 可用.
    std::vector<ModelInfo> pool = {
        mi("claude-opus-4-5"), mi("claude-sonnet-4-5"), mi("claude-haiku-4-5")};
    // 读取 -only 角色底线默认为便宜 ~ 选择 CHEAPEST 有能力的一。
    CHECK(cheapest_capable_model("claude-opus-4-5", pool) == "claude-haiku-4-5");
    // 父母已经便宜了 完全便宜了 父母也不要离开
    CHECK(cheapest_capable_model("claude-haiku-4-5", pool) == "claude-haiku-4-5");
    // Single-model 提供商 – 没有变化,没有回归.
    std::vector<ModelInfo> solo = {mi("claude-opus-4-5")};
    CHECK(cheapest_capable_model("claude-opus-4-5", solo) == "claude-opus-4-5");
    // 空 池 – 保留父 键。
    CHECK(cheapest_capable_model("claude-opus-4-5", {}) == "claude-opus-4-5");
    // 当地模式在 池 从未被选中。
    std::vector<ModelInfo> mixed = {
        mi("claude-opus-4-5"), mi("qwen2.5-coder:7b"), mi("claude-sonnet-4-5")};
    CHECK(cheapest_capable_model("claude-opus-4-5", mixed) == "claude-sonnet-4-5");
    // 报告没有工具支持的探测器使候选人丧失了资格。
    std::vector<ModelInfo> notools = {mi("claude-opus-4-5"), mi("claude-haiku-4-5")};
    notools[1].supports_tools = false;
    CHECK(cheapest_capable_model("claude-opus-4-5", notools) == "claude-opus-4-5");

    // REGRESSION : 路由代码必须 绝不 携带 `[1m]`/`[2m]`
    // 扩展上下文 选择面板 标记,这个标记是 传输层 发送 `context-1m` β,
    // 400 代表整个亚剂在 a 上请求 事件订阅 长的不合格
    // -context β( 双倍)
    // "long context beta is not yet available for this subscription"（当前订阅尚未开放长上下文测试功能。）
    // 子代理从不需要1M窗口,所以路由器会剥离它——无论是在选择更
    // 便宜的候选人时还是在 `[1m]` 父目录被保留为 -is.
    std::vector<ModelInfo> pool_1m = {
        mi("claude-opus-4-5"), mi("claude-sonnet-4-5"), mi("claude-haiku-4-5")};
    CHECK(cheapest_capable_model("claude-opus-4-5[1m]", pool_1m) == "claude-haiku-4-5");
    // parent-kept路径(严格来说没有更便宜的)仍然会降下标记.
    std::vector<ModelInfo> solo_1m = {mi("claude-opus-4-5[1m]")};
    CHECK(cheapest_capable_model("claude-opus-4-5[1m]", solo_1m) == "claude-opus-4-5");
    CHECK(cheapest_capable_model("claude-opus-4-5[1m]", {}) == "claude-opus-4-5");
}

// 副代理必须 绝不 携带 选择面板-only `[1m]`/`[2m]` 延长 -context
// 标记: 它会使 传输层 发送应享待遇 -gated 上下文-1m β,400s(
// "long context beta is not yet available" 杀死整个扇子 -out.
// run_one_completion 来源 req.model 以
// `wire_model_id(<cheapest-or-parent>)` ; 此选项 锁 标记在每一个
// 分支中消失(便宜) -found ,保留 -parent ,写 -role 单 -model).
TEST_CASE("subagent model never 1m") {
    using agentty::wire_model_id;
    auto has_1m = [](std::string_view s) {
        return s.find("[1m]") != std::string_view::npos
            || s.find("[2m]") != std::string_view::npos;
    };

    // 父母在1M 变体上,便宜的NON-1M型号可用(read -only角色):
    // 路由器选取更便宜的清洁的ID, wire_model_id 进一步拒绝 -op.
    std::vector<ModelInfo> pool = {
        mi("claude-opus-4-5"), mi("claude-sonnet-4-5"), mi("claude-haiku-4-5")};
    auto ro = wire_model_id(cheapest_capable_model("claude-opus-4-5[1m]", pool));
    CHECK(!has_1m(ro));
    CHECK(ro == "claude-haiku-4-5");

    // 单身 -model 1M 的供应商 变体 : 路由器 KEEPS 父母
    // (带有标记)—— wire_model_id 这正是以前400个
    // 案子的准确数字
    std::vector<ModelInfo> solo = {mi("claude-opus-4-5")};
    auto kept = wire_model_id(cheapest_capable_model("claude-opus-4-5[1m]", solo));
    CHECK(!has_1m(kept));
    CHECK(kept == "claude-opus-4-5");

    // Write-role 路径( 选项) req.model = cfg.model 仍然被剥光了
    CHECK(!has_1m(wire_model_id(std::string{"claude-sonnet-4-5[1m]"})));
    CHECK(wire_model_id(std::string{"claude-sonnet-4-5[1m]"}) == "claude-sonnet-4-5");

    // 平原(非-1M)母体未受影响端-to-end.
    CHECK(wire_model_id(cheapest_capable_model("claude-sonnet-4-5", pool))
          == "claude-haiku-4-5");
}

TEST_CASE("context window detection") {
    using agentty::ModelCapabilities;
    auto win = [](std::string_view id) {
        return ModelCapabilities::from_id(id).context_window();
    };
    auto suffix = [](std::string_view id) {
        return ModelCapabilities::from_id(id).supports_1m_suffix();
    };

    // supports_1m_suffix (:镜像 Claude 代码 目录
    // `supports_1m_suffix` 标志 ——松内-4线,奥普斯-4线,海库4.5线可提供a.
    // `[1m]` 变体 ; genQQ3模型可能不会.
    CHECK(suffix("claude-sonnet-4-5"));
    CHECK(suffix("claude-sonnet-4-5-20250101"));
    CHECK(suffix("claude-sonnet-4"));
    CHECK(suffix("claude-opus-4-5"));
    CHECK(suffix("claude-haiku-4-5"));
    CHECK(!suffix("claude-3-5-sonnet-20241022"));   // gen 3, no 1M 变体 电源
    CHECK(!suffix("claude-3-opus"));
    // 下一个旗舰 -gen 车道( 车道) ( Fable/Mythos 5+).
    CHECK(suffix("claude-fable-5"));
    CHECK(suffix("claude-mythos-5"));

    // context_window (:每名已知者200k基数) Claude 型号;
    // 1M 仅 当明白的时候, `[1m]` 变体 选中( 匹配) Claude 代码,
    // 使底码保持在200k,并绑定 1M 与 `<id>[1m]` 选择面板 ). 条目.
    CHECK(win("claude-sonnet-4-5") == 200000);        // 基准 id − 200k
    CHECK(win("claude-sonnet-4-5[1m]") == 1000000);   // [1米] 变体 − 1米
    CHECK(win("claude-opus-4-5") == 200000);
    CHECK(win("claude-opus-4-5[1m]") == 1000000);
    CHECK(win("claude-haiku-4-5") == 200000);
    CHECK(win("claude-haiku-4-5[1m]") == 1000000);
    // 未知/ 本地家庭报告 0, 所以呼叫者更喜欢一个被探测的窗口。
    CHECK(win("qwen2.5-coder:7b") == 0);
    CHECK(win("some-random-model") == 0);

    // COMPACTION INVARIANT:收缩路径条条将
    // `[1m]`标记(wire_model_id),所以传输窗口是BASE,
    // 然后将载荷切至该基座——NOT至65%的1M,将超过200K请求.
    // context_max_for_model 由这些组成;在此申明纯等值:
    // 窗口 STRIPPED id总是200K的基地,从不1M.
    {
        using agentty::wire_model_id;
        CHECK(win(wire_model_id("claude-opus-4-5[1m]")) == 200000
              && "compaction runs on the base window, not the parent's 1M");
        CHECK(win(wire_model_id("claude-sonnet-4-5[1m]")) == 200000);
        // 非-100 父母关系不变。
        CHECK(win(wire_model_id("claude-opus-4-5")) == 200000);
    }
}

TEST_CASE("wire model id strip") {
    using agentty::wire_model_id;
    // 电话 `[1m]`/`[2m]` 选择面板 标记必须 绝不 到达 传输 (无) API 404开
    // `claude-sonnet-5[1m]`). wire_model_id 剥光它, 镜像 Claude
    // 代码 Yu().
    CHECK(wire_model_id("claude-sonnet-5[1m]") == "claude-sonnet-5");
    CHECK(wire_model_id("claude-opus-4-8[1m]") == "claude-opus-4-8");
    CHECK(wire_model_id("claude-sonnet-4-5[2m]") == "claude-sonnet-4-5");
    // 赤色的ID( 无标记) 返回不变。
    CHECK(wire_model_id("claude-opus-4-8") == "claude-opus-4-8");
    CHECK(wire_model_id("gpt-5.6-sol") == "gpt-5.6-sol");
    CHECK(wire_model_id("qwen2.5-coder:7b") == "qwen2.5-coder:7b");
    // 标记 中-string(防御)仍被移除.
    CHECK(wire_model_id("claude-opus-4-8[1m]-preview") == "claude-opus-4-8-preview");
    CHECK(wire_model_id("") == "");
}

TEST_CASE("model picker ordering") {
    using agentty::ModelInfo;
    using agentty::ModelId;
    using agentty::model_picker_less;

    auto mi = [](std::string_view id) {
        return ModelInfo{ModelId{std::string{id}}, std::string{id}, "anthropic"};
    };

    // 退转 : Fable/Mythos 是一个 NEWEST 旗舰
    // -tier 车道, 并且永远不能沉到下面 Opus/Sonnet/Haiku
    // 是因为一个固定的家庭 -name 桶单上最后列出它们,
    // 喂饱它 Anthropic 返回(任意 / v1/models
    // 排序。
    std::vector<ModelInfo> v = {
        mi("claude-opus-4-8"),   mi("claude-opus-4-7"),
        mi("claude-sonnet-4-6"), mi("claude-opus-4-6"),
        mi("claude-opus-4-5"),   mi("claude-haiku-4-5"),
        mi("claude-sonnet-4-5"), mi("claude-fable-5"),
    };
    std::stable_sort(v.begin(), v.end(), model_picker_less);

    // 5号线(旗舰级,第5代)必须带领每条Opus(旗舰一级,第4.
    // x代)——最新旗舰获胜,而不是最后的-in-bucket.
    CHECK(v.front().id.value == "claude-fable-5");

    // 每一个旗舰 -tier 模型( Opus + Fable) 排序于每个中点之前
    // -tier 模型(Sonnet),每便宜之前排序 -tier 模型 (海库).
    auto flagship_end = std::find_if(v.begin(), v.end(), [](const ModelInfo& m) {
        return ModelCapabilities::from_id(m.id.value).tier()
            != ModelCapabilities::Tier::Flagship;
    });
    auto sonnet_end = std::find_if(flagship_end, v.end(), [](const ModelInfo& m) {
        return ModelCapabilities::from_id(m.id.value).tier()
            != ModelCapabilities::Tier::Mid;
    });
    CHECK(std::distance(v.begin(), flagship_end) == 5);   // 4 Opus + 1 寓言
    CHECK(std::distance(flagship_end, sonnet_end) == 2);  // 2 声纳
    CHECK(sonnet_end == v.end() - 1);                     // 一条海库,跟随

    // 在旗舰级内,最新一代领先(5号线 > Opus 4.8号线 > OPus 4.7号线 > 奥
    // 普斯 4.6号线 > Apus 4.5号线).
    std::vector<std::string> flagship_order;
    for (auto it = v.begin(); it != flagship_end; ++it)
        flagship_order.push_back(it->id.value);
    CHECK((flagship_order == std::vector<std::string>{
        "claude-fable-5", "claude-opus-4-8", "claude-opus-4-7",
        "claude-opus-4-6", "claude-opus-4-5"}));
}

// ── 警棍:警犬能力-key学科 ──────────────────────
// 雾 -medium-3-5 3,5,2号 -ladders 错误是
// KEYING bug:一个模型的两种拼写通过不同的键解决不同的事实.
// 盖基使这一类无法体现——这些测试是锁的三条规则.
TEST_CASE("capkey: one spelling (norm_model folds separators, never alnums)") {
    using agentty::capkey::norm_model;
    using agentty::capkey::norm_tail;
    // 拼写 变体 会 议 日 程 和 议 程 相同 型号 = 一个键。 (案件直接来自
    // models.dev 体积: 269个真多 -spelling 团体统一这个方式。 )
    CHECK(norm_model("mistral-medium-3.5") == "mistral-medium-3-5");
    CHECK(norm_model("Kimi-K2.5")          == "kimi-k2-5");
    CHECK(norm_model("kimi k2.5")          == "kimi-k2-5");
    CHECK(norm_model("GLM_5.2")            == "glm-5-2");
    CHECK(norm_model("gpt-oss:20b")        == "gpt-oss-20b");
    // 字母数字是NEVER改变的——不同的模型不能合并.
    CHECK(norm_model("gpt-5-5") != norm_model("gpt-55"));
    // 分隔器会崩溃;边缘会修剪。
    CHECK(norm_model("--a..b__c--")        == "a-b-c");
    // 供应商前缀条通过norm_tail.
    CHECK(norm_tail("mistralai/Mistral-Medium-3.5") == "mistral-medium-3-5");
    CHECK(norm_tail("bare-model")                    == "bare-model");
}

TEST_CASE("capkey: one provider namespace (identity-anchored dev mapping)") {
    using agentty::capkey::resolve_dev_provider;
    using agentty::capkey::dev_scope;
    // 锚号1:准确的ID.
    CHECK(resolve_dev_provider("mistral", "MISTRAL_API_KEY", "") == "mistral");
    CHECK(resolve_dev_provider("groq", "GROQ_API_KEY", "") == "groq");
    // 锚定器2:主舱Var- models.dev "google" 携带 GOOGLE_API_KEY 首先,
    // 这不是 agentty 双胞胎列的主线( main) GEMINI_API_KEY ),
    // 所以它确实如此 不 透过洞匹配... 但是 "togetherai" ↔ TOGETHER_API_KEY 已经。
    CHECK(resolve_dev_provider("togetherai", "TOGETHER_API_KEY", "")
          == "together");
    // 锚3: API 主机( github ) -copilot 针头 api.githubcopilot.com).
    CHECK(resolve_dev_provider("github-copilot", "GITHUB_TOKEN",
                               "https://api.githubcopilot.com") == "copilot");
    // 未知集合器在惰性外国命名空间中降落。
    CHECK(dev_scope("requesty", "REQUESTY_API_KEY", "https://api.requesty.ai")
          == "dev:requesty");
}

TEST_CASE("capkey: registry keys normalise on write AND read") {
    using agentty::set_catalog_effort_set;
    using agentty::catalog_effort_set_for;
    using agentty::Effort;
    using agentty::effort_bit;
    // Write 使用一个拼写, 读另一个—— 同一个事实。
    // 这是回归 锁 两人 -ladders 截图错误。
    const std::uint8_t hi = effort_bit(Effort::High);
    set_catalog_effort_set("mistral/regr-model-3.5", hi);
    CHECK(catalog_effort_set_for("Regr-Model-3-5", "mistral")
          == static_cast<int>(hi));
    CHECK(catalog_effort_set_for("regr_model_3.5", "mistral")
          == static_cast<int>(hi));
}

TEST_CASE("capkey: [1m]/[2m] context markers fold out of capability keys") {
    using agentty::capkey::norm_model;
    // 能力事实属于 MODEL ,而不是上下文 -window 变体:
    // 事实,从中汲取的教训 传输 ID必须击中下 选择面板 标记为 id.
    CHECK(norm_model("claude-opus-4-8[1m]") == "claude-opus-4-8");
    CHECK(norm_model("claude-sonnet-5[2m]") == "claude-sonnet-5");
    CHECK(norm_model("claude-opus-4-8")     == "claude-opus-4-8");
    // 括号,即 不 标记没有被破坏
    CHECK(norm_model("weird[3x]model") == "weird[3x]model");
}

TEST_CASE("clamp_effort: empty/unknown ids never wipe the user's tier") {
    using agentty::clamp_effort;
    using agentty::resolved_caps;
    using agentty::Effort;
    // resolved_caps("" )没有努力支持——盲夹会使
    // 用户的级别崩溃到无。 -switch 站点在空 ID 上守护;
    // 此 锁 盖子 -side 守门员们的行为
    CHECK(clamp_effort(Effort::High, resolved_caps("")) == Effort::None);
}

// ── 能力-scope 稳健度 ───────────────────────────────────────
// 能力事实已按键 "provider/model" 所以,我们 相同
// 由两个主机提供的光模 id , 可以搭载不同的梯子。 这些针头可以
// 将两个标注在 不变量 这使得这个安全 在每个提供者。

TEST_CASE("caps scope: learned facts are per-provider, never cross-talk") {
    // 两位主机服务于同一赤裸的ID 不得共享一个学习的梯子.
    const std::string id = "mistral-small-latest";
    set_learned_effort_set(scoped_caps_key(id, "mistral"),
                           static_cast<int>(effort_bit(Effort::Low) | effort_bit(Effort::High)));
    set_learned_effort_set(scoped_caps_key(id, "openrouter"),
                           static_cast<int>(effort_bit(Effort::Medium)));

    const auto a = resolved_caps(id, "mistral");
    const auto b = resolved_caps(id, "openrouter");
    CHECK(a.effort_set_known);
    CHECK(b.effort_set_known);
    CHECK(a.effort_set != b.effort_set);   // 主机之间没有流血

    // 未知的主机倒回了,而不是继承邻居的设定.
    const auto c = resolved_caps(id, "some-unconfigured-host");
    CHECK(c.effort_set != a.effort_set);
}

TEST_CASE("caps scope: [1m] variant shares the base model's facts") {
    // norm_model 归并 上下文标记 PURPOSE ——能力事实属于模型,
    // 而不是窗口. 变体 - 所以这个事实已经... 传输 ID在下面
    // 检查时必须击打 选择面板's [1m]-marked 编号.
    // (Row) IDENTITY 保留标记;见 norm_row_id.)
    const std::string base = "claude-opus-4-8";
    const std::string one_m = "claude-opus-4-8[1m]";
    CHECK(capkey::norm_model(base) == capkey::norm_model(one_m));
    CHECK(capkey::norm_row_id(base) != capkey::norm_row_id(one_m));
    // 同一个范围的关键 事实既服务于两个。
    CHECK(scoped_caps_key(base, "anthropic")
          == scoped_caps_key(one_m, "anthropic"));
}

TEST_CASE("caps scope: provider::select publishes the SAME id lookups use") {
    // 环境上限范围必须等于 源码树 调用 提供者 ID (
    // modal.cpp's active_provider_id
    // 或事实在一个密钥下归档,在另一个密钥下查证,这些事实永远
    // 无法再找到。 ACP 特工们公布了 DEFAULT
    // 编号( "anthropic" 作为其范围,
    // 而每 目录 / 最近的/ 引信行由代理 ID 键入。
    {
        provider::Selection s;
        s.kind = provider::Kind::ExternalAcp;
        s.acp_agent_id = "my-acp-agent";
        provider::select(::agentty::IoAccess::grant(), s);
        CHECK(caps_provider_scope() == "my-acp-agent");
    }
    {
        provider::Selection s;
        s.kind = provider::Kind::OpenAI;
        s.openai_endpoint.label = "groq";
        provider::select(::agentty::IoAccess::grant(), s);
        CHECK(caps_provider_scope() == "groq");
    }
    {
        provider::Selection s;
        s.kind = provider::Kind::Anthropic;
        provider::select(::agentty::IoAccess::grant(), s);
        CHECK(caps_provider_scope() == std::string{provider::default_provider_id()});
    }
}

TEST_CASE("wire dialect reasoning-text transmissibility") {
    using provider::wire_streams_reasoning_text;
    // 副驾驶是 MIXED ,所以答案是按 MODEL 与实时自动会话相对比
    // api.individual.githubcopilot.com:
    //   长5 -mini / mai 语 -code-*  数据流 答复的更正
    //                            API ,哪个 执行 返回推理
    //   摘要文本 Claude- * / gpt-4.x 是聊天 -only ,
    // 该返回无 agentty 引导第一组人通过/ 答复准确,
    // 所以思维是可见的,所以 选择面板 老实说 "✦ shown" 说着
    // "n/a on this API" 下方一排 下方是一排
    CHECK(wire_streams_reasoning_text("copilot", "gpt-5-mini"));
    CHECK(wire_streams_reasoning_text("copilot", "mai-code-1.1-flash"));
    CHECK(!wire_streams_reasoning_text("copilot", "claude-haiku-4.5"));
    CHECK(!wire_streams_reasoning_text("copilot", "gpt-4.1"));
    // 没有叫"粗糙的提供者"的模型 -level 问题(A/C.5/56/L.61)
    // "could ANY model here show reasoning?" ),对于副驾驶来说,现在是肯定的。
    CHECK(wire_streams_reasoning_text("copilot"));
    // agentty的`openai`行DEFAULTS至api.openai.com/v1/chat/completions,
    // 但该行现在也广告了/v1/responses,
    // 所以——与上面的副驾驶完全一样——答案变成了MODEL而不是每行.
    //
    // 这种说法以前是平房 `!...("openai")`。
    // 这是对的,当行可以 仅 拨号聊天(在此之前,这是错的,
    // 原因相反:该行被错贴了标签) Wire::OpenAIResponses
    // 和方言 -only 上游响应 "yes" 诚实
    // 的回答是:
    //   gpt-4o –聊天,没有关于传输
    //   gpt-5+的推理 – 路由/响应准确如此思维可见.
    CHECK(!wire_streams_reasoning_text("openai", "gpt-4o"));
    CHECK(wire_streams_reasoning_text("openai", "gpt-5"));
    // GPT -5.4+不仅在答复上更受青睐——聊天
    // REJECTS 工具 调用 所以必须让特工转身去
    CHECK(wire_streams_reasoning_text("openai", "gpt-5.4"));
    // 没有叫"粗糙的提供者"的模型 -level 问,对于Openai来说,
    // 现在是“是”,原因与“副驾驶”相同: SOME 模型可以
    // 显示推理。 ( 选择面板 在选择模型前询问此事项。 )
    CHECK(wire_streams_reasoning_text("openai"));
    CHECK(wire_streams_reasoning_text("chatgpt"));
    // Anthropic 数据流 思考在本土。
    CHECK(wire_streams_reasoning_text("anthropic"));
    // OpenAI-COMPAT 服务器上 相同 方言语 DO 居中
    // reasoning_content (DeepSeek-R1 密斯特拉尔 OpenRouter )——
    // 规则按规定执行. -host 单人 -dialect.
    CHECK(wire_streams_reasoning_text("mistral"));
    CHECK(wire_streams_reasoning_text("openrouter"));
    // 未知/ 自定义主机:假设是而非隐藏一个真实的特性.
    CHECK(wire_streams_reasoning_text("localhost:8080"));
}

// ── 跑步的步口盖 适用于我们无法保证的模型 ───────────
//
// 盖(25个工具转动而无文字回答)是针对末日循环的最后后卫。
// 上面的两个盖键在 IDENTICAL 参数上 — — 同样的失败
// 的 调用 3x, 同样的继承的 调用 6x — — 因此,
// 一个通过 不同 调用 循环的模型同时逃脱,只有这个模型被留下。
//
// 曾经只有当 `is_weak_model(id)` 说对了,这倒置了极性:
// LESS agentty 有个模特儿 LESS 高压是用来调节主机和
// Ollama 命名( 编号( N) ) `:7b` 标记,已知的家族前缀),
// 所以每个 llama.cpp GGUF 文件名和每个 LM 工作室
// `publisher/model` 键读作 "not weak" 跑得没有被抓住,
// 这就是... "loops forever in tool calls" 报告。
//
// 这些 锁 编号 CLASSIFICATION 大门是建在...
// cmd_factory (全文为: provider::active
// 此处所钉的,是旧门错的。

TEST_CASE("local model ids are NOT recognised as weak") {
    // 完全就是本地服务器报告的形状。 每个都是一个能在当地运行的模型,
    // 每个都读不出来-weak — 因此, is_weak_model
    // () 上盖的盖子绝不为其中任何一个开火。
    //
    // 这不是一个错误在 is_weak_model : 它回答问题
    // "is this a SMALL model I recognise" ,这些都不是。使用这个答案时,
    // 它是一个错误。 "is this model safe to run uncapped".
    CHECK(!weak("qwen3-30b-a3b"));                  // llama.cpp --alias 国 务 院
    CHECK(!weak("Qwen3-30B-A3B-Q4_K_M.gguf"));      // llama.cpp 默认 id ( -m 路径)
    CHECK(!weak("gpt-oss-20b"));
    CHECK(!weak("GLM-4.6-Q4_K_M.gguf"));
    CHECK(!weak("Devstral-Small-2507-Q8_0.gguf"));
    CHECK(!weak("qwen/qwen3.5-9b"));                // LM 工作室密钥形状
    CHECK(!weak("igpu/laguna-xs-2.1"));             // 私人网关,来自80-turn报告
}

TEST_CASE("the ollama-shaped ids the heuristic DOES catch") {
    // 未被删除: 在Ollama本土路径上, 这些标记是真实的,
    // 并且高压是正确的。 关键是, 唯一的形状是它覆盖的,
    // 这就是为什么它不能成为唯一的大门。
    CHECK(weak("qwen2.5-coder:7b"));
    CHECK(weak("codellama:7b"));
    CHECK(weak("phi3:3.8b"));
}

TEST_CASE("provider rows know which backends are local") {
    // 正确门键上的事实。 运行于此机器的一行无法保证其
    // 模型的循环行为, 以及主机后端能够达到的循环行为。
    // 因此它得到了后台, 不管ID是什么样子。
    const auto* ollama = provider::preset_for("ollama");
    REQUIRE(ollama != nullptr);
    CHECK(ollama->is_local);

    const auto* anthropic = provider::preset_for("anthropic");
    REQUIRE(anthropic != nullptr);
    CHECK(!anthropic->is_local);

    const auto* openai = provider::preset_for("openai");
    REQUIRE(openai != nullptr);
    CHECK(!openai->is_local);
}

// ── 点缀版本代号(GitHub 副驾驶/模具拼写) ──────────────
//
// 副驾驶返回 `claude-opus-4.7` ,
// 没有 `claude-opus-4-7` 家谱相符,所以 `is_opus()`
// 是真的,而且,我爱她 无内容 看上去很破碎,但这个版本 解析 需要 QQ
// 2 字符的符号( Q)。 '4','.','7' 每一个读着这个的门
// 都保留了0. NUMBER 然则,你应当静静地回答, "old model"
// 目标:努力 max/xhigh 全体副驾驶被扣 Claude 显示标签
// 倒塌到空洞 "Opus" 以相同方式提出 选择面板 排行数.
//
// 两种拼写必须完全一样解码.
TEST_CASE("dotted and dashed version ids decode identically") {
    using agentty::ModelCapabilities;
    struct Pair { const char* dotted; const char* dashed; };
    for (auto p : {Pair{"claude-opus-4.7",   "claude-opus-4-7"},
                   Pair{"claude-opus-4.5",   "claude-opus-4-5"},
                   Pair{"claude-sonnet-4.6", "claude-sonnet-4-6"},
                   Pair{"claude-haiku-4.5",  "claude-haiku-4-5"},
                   Pair{"claude-fable-5.1",  "claude-fable-5-1"}}) {
        const auto a = ModelCapabilities::from_id(p.dotted);
        const auto b = ModelCapabilities::from_id(p.dashed);
        CHECK(a.family     == b.family);
        CHECK(a.generation == b.generation);
        CHECK(a.revision   == b.revision);
        CHECK(a.generation_4_or_later == b.generation_4_or_later);
        CHECK(a.supports_effort()       == b.supports_effort());
        CHECK(a.supports_effort_max()   == b.supports_effort_max());
        CHECK(a.supports_effort_xhigh() == b.supports_effort_xhigh());
        CHECK(ModelCapabilities::tier_for(p.dotted)
              == ModelCapabilities::tier_for(p.dashed));
    }
}

TEST_CASE("copilot dotted claude ids reach the full effort ladder") {
    using agentty::ModelCapabilities;
    // 用户-visible症状:x高运出与Opus 4.7,
    // 一个副驾驶会话永远无法选择,因为id解码为生成 0.
    const auto o47 = ModelCapabilities::from_id("claude-opus-4.7");
    CHECK(o47.is_opus());
    CHECK(o47.generation == 4);
    CHECK(o47.revision   == 7);
    CHECK(o47.generation_4_or_later);
    CHECK(o47.supports_effort());
    CHECK(o47.supports_effort_max());
    CHECK(o47.supports_effort_xhigh());

    // 不应将虚构的修订误认为 下一个 符号修改 :
    // `claude-haiku-4.5-latest` 4,5,以及 4,6,6,5,6,7,6,8,6,9,6,4,6 6,6 , 6,7 6,8 6,5 , 7,6 7,7,7 7,8,7,8 , 5,6 和 `latest` 是一个 变体 词
    const auto h = ModelCapabilities::from_id("claude-haiku-4.5-latest");
    CHECK(h.is_haiku());
    CHECK(h.generation == 4);
    CHECK(h.revision   == 5);

    // 两位 -digit 修改范围不变。
    const auto s = ModelCapabilities::from_id("claude-sonnet-5.10");
    CHECK(s.generation == 5);
    CHECK(s.revision   == 10);
}

TEST_CASE("version parse rejects dates and malformed versions") {
    using agentty::ModelCapabilities;
    // 守旧的 <= 2 length check existed for. An 8-digit date must never read as a generation — 20241022 would pass every `>
    // =4 ' 门。
    for (const char* id : {"claude-3-5-sonnet-20241022",
                           "claude-3-opus-20240229"}) {
        const auto c = ModelCapabilities::from_id(id);
        CHECK(c.generation == 0);
        CHECK(!c.generation_4_or_later);
    }
    // 格式不规范的点表不是版本:跟随点,引导点,
    // 第二个点都让版本没有设定,而不是猜测.
    for (const char* id : {"claude-opus-4.", "claude-opus-.7",
                           "claude-opus-4.7.1"}) {
        const auto c = ModelCapabilities::from_id(id);
        CHECK(c.is_opus());          // 家族仍然解码
        CHECK(c.generation == 0);    // 但没有发明版本
    }
    // gpt分支的边界相同,所以一个日期
    // -shaped的代号也不能成为一代.
    CHECK(ModelCapabilities::from_id("gpt-20250514").generation == 0);
    // 而5前的ID则保持未知(generic OpenAI compat),和以前一样.
    CHECK(!ModelCapabilities::from_id("gpt-4o").is_gpt());
    CHECK(!ModelCapabilities::from_id("gpt-4.1").is_gpt());
}

TEST_CASE("the [1m] suffix survives a dotted version") {
    using agentty::ModelCapabilities;
    const auto c = ModelCapabilities::from_id("claude-opus-4.5[1m]");
    CHECK(c.is_opus());
    CHECK(c.generation == 4);
    CHECK(c.revision   == 5);
    CHECK(c.extended_context_1m);
    CHECK(c.context_window() == 1000000);
}

TEST_CASE("every live copilot claude id decodes a version") {
    using agentty::ModelCapabilities;
    // 真正的模特儿们 载荷。每个都是 Claude-family
    // 其版本为点;其中任何一种上的一代0表示 选择面板
    // 归并 兄弟姐妹们排成一排,努力梯子被扣住。
    for (const char* id : {"claude-opus-4.7", "claude-opus-4.8",
                           "claude-opus-5",   "claude-opus-5.5",
                           "claude-sonnet-4.6", "claude-sonnet-5",
                           "claude-sonnet-5.5", "claude-haiku-4.5",
                           "claude-fable-5",  "claude-fable-5.1"}) {
        const auto c = ModelCapabilities::from_id(id);
        CHECK(c.is_known_family());
        CHECK(c.generation >= 4);
        CHECK(c.generation_4_or_later);
    }
}
