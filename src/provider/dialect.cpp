// agentty::provider — 方言选择。请参阅 dialect.hpp 了解基本原理。

#include "agentty/provider/dialect.hpp"
#include "agentty/util/digits.hpp"

#include <maya/runtime.hpp>
#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <utility>

#include "agentty/domain/catalog.hpp"   // ModelCapabilities：MODEL原因吗
#include "agentty/provider/registry.hpp"
#include "agentty/util/logx.hpp"

namespace agentty::provider {
namespace {

// Case/punctuation-insensitive家庭配套。 Model 段头有多种形状，
// 适合 相同 系列 - "gpt-5"、"openai/gpt-5-mini"、
// "gpt-5.4-2026"、"GPT-5" - 因此请以标准化形式而不是文字前缀进行匹配。
[[nodiscard]] std::string normalise(std::string_view id) {
    std::string out;
    out.reserve(id.size());
    // 删除任何供应商前缀 ("openai/gpt-5" → "gpt-5")：
    // OpenRouter 和网关名称空间的 slugs，系列是斜杠后面的部分。
    if (const auto slash = id.rfind('/'); slash != std::string_view::npos)
        id.remove_prefix(slash + 1);
    for (const char c : id)
        out.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(c))));
    return out;
}

[[nodiscard]] bool starts_with(std::string_view s, std::string_view p) noexcept {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

// GPT-5 代号，如果这是 gpt-5 型号："gpt-5.4-mini" → 54、"gpt-5"
// → 50、"gpt-5-mini" → 50。对于非 -gpt-5 段返回 -1。
//
// 需要，因为聊天工具-calling截止是POINT RELEASE（5.4），
// 而不是整个系列：gpt-5和gpt-5.1仍然是聊天工具-call，5.4+则不是。
[[nodiscard]] int gpt5_generation(std::string_view m) noexcept {
    if (!starts_with(m, "gpt-5")) return -1;
    auto rest = m.substr(5);
    if (rest.empty() || rest.front() != '.') return 50;   // 普通 "gpt-5*"
    rest.remove_prefix(1);
    int minor = 0;
    std::size_t i = 0;
    for (; i < rest.size() && std::isdigit(static_cast<unsigned char>(rest[i])); ++i)
        minor = ::agentty::util::push_digit(minor, rest[i]);
    if (i == 0) return 50;
    return 50 + std::min(minor, 9);
}

// ── 已学习的拒绝 ───────────────────────────────────────────────────
using Key = std::pair<std::string, std::string>;   // （提供商、型号）

// 每个（提供商、模型）拒绝的内容是在 运行时 学到的。
struct Observations {
    std::set<Key> rejected_responses;
    std::set<Key> rejected_chat;
};
// 阅读每个路由决策，仅在主机拒绝方言时写入：锁
// -free 快照，因此路由不需要任何功能。
maya::snapshot<Observations>& observations() {
    static maya::snapshot<Observations> o;
    return o;
}

// 主机拒绝了这对中的哪两个，作为普通布尔值。
struct Rejected { bool responses = false; bool chat = false; };
[[nodiscard]] Rejected rejected_for(std::string_view p, std::string_view m) {
    const auto o = observations().load();
    const Key k{std::string{p}, std::string{m}};
    return Rejected{o->rejected_responses.count(k) > 0,
                    o->rejected_chat.count(k) > 0};
}

} // 命名空间结束。

bool model_requires_responses(std::string_view model) noexcept {
    const std::string m = normalise(model);
    if (m.empty()) return false;

    // ── GPT-5.4 及以后各页 ────────────────────────────────────────────────
    // OpenAI的推理指南：
    // "Starting with GPT-5.4, Chat Completions does not support tool calling with reasoning_effort values other than none."（从 GPT-5.4 起，Chat Completions 只在 reasoning_effort 为 none 时支持工具调用。）
    // agentty总是发送工具，所以对于代理来说这是一个硬性要求。
    if (const int gen = gpt5_generation(m); gen >= 54) return true;

    // ── GPT-6 / 气态生成 ─────────────────────────────────────────
    // "Use the Responses API for function calling. Chat Completions does not support function calling with GPT-6 Astra."（GPT-6 Astra 的函数调用应使用 Responses API，Chat Completions 不支持。）
    // 不费力的警告：聊天方言根本
    // 无法为该家庭提供代理轮流。
    if (starts_with(m, "gpt-6") || m.find("astra") != std::string::npos)
        return true;

    // ── 副驾驶的Mai -code-* ─────────────────────────────────────────────
    // MEASURED 针对实时自动会话：聊天回答 400
    // unsupported_api_for_model。响应是实现这些目标的唯一途径。
    if (starts_with(m, "mai-code")) return true;

    // 早期推理家族（gpt-5.0/5.1、o1、o3、o4）在聊天中
    // 仍然是 LEGAL — 他们只是无法在那里展示自己的想法。
    // 它们在下面作为首选项进行处理，而不是必需的，因此 chat-only 网关仍然有效。
    return false;
}

bool model_emits_reasoning_summaries(std::string_view model) noexcept {
    const std::string m = normalise(model);
    if (m.empty()) return false;
    // MEASURED：/响应 数据流 上的 gpt-5*
    // 真实 response.reasoning_summary_text.delta 帧。
    // o-series 和 gpt-6 系列是相同方言的推理模型，并且行为相似。
    if (gpt5_generation(m) >= 0) return true;
    if (starts_with(m, "gpt-6") || m.find("astra") != std::string::npos) return true;
    if (starts_with(m, "o1") || starts_with(m, "o3") || starts_with(m, "o4")) return true;
    if (starts_with(m, "mai-code")) return true;
    // 明确NOT声称：claude-*和gpt-4.
    // x不返回任何摘要，即使主机将在响应上接受它们。否则，
    // 面板 的思维就会在空荡荡的通道上永远旋转。
    return false;
}

Dialect dialect_for(std::string_view provider_id, std::string_view model) noexcept {
    const auto* row = preset_for(provider_id);

    // 非-OpenAI方言上的行在结构上回答：Anthropic讲消息，
    // ACP是子进程，Ollama的本机API是其自己的协议。
    if (row) {
        if (row->wire == Wire::AnthropicMessages || row->wire == Wire::Acp)
            return Dialect::Native;
        if (row->native_api) return Dialect::Native;
        // DEFAULT为响应的行没有第二方言可供选择（
        // endpoints_consistent禁止）-e.g。 ChatGPT/Codex.
        if (row->wire == Wire::OpenAIResponses) return Dialect::Responses;
        // 没有通告响应端点→没有可路由到的内容。这是 DeepSeek、Mistral、
        // Groq、xAI、Together、Cerebras 和自定义
        // llama.cpp 主机的答案：它们对聊天的
        // `reasoning_content` 进行推理，并且没有/响应可提供。
        if (row->responses_path.empty()) return Dialect::Chat;
    } else {
        // Unknown/custom主机(用户自己的OpenAI
        // -compatible底座URL)。我们无法知道它有/响应，
        // 并且猜测错误会导致我们无法廉价探测的主机失败。继续聊天。
        return Dialect::Chat;
    }

    // ── 排队 CAN 两者兼有 现在由模特决定了 ──────────────────────
    if (model.empty()) {
        // Provider-level问题("could anything here use Responses?")。
        return Dialect::Responses;
    }

    const bool required = model_requires_responses(model);

    // 运行时证据在BOTH方向上优先于先前的证据。
    {
        const Rejected r = rejected_for(provider_id, model);
        if (r.responses) {
            // 主持人告诉我们没有。即使是 "required" 型号也值得尊敬：
            // 聊天失败打开严格来说比某个 404 更好，
            // 并且用户从模型而不是从我们那里得到错误。
            return Dialect::Chat;
        }
        if (r.chat) return Dialect::Responses;
    }

    if (required) return Dialect::Responses;

    // 不是必需的，但当模型具有值得展示的思想并且主机可以承载它
    // 时是首选：更好的工具使用、更好的缓存命中、并且推理通过
    // encrypted_content跨工具轮次存活。
    if (model_emits_reasoning_summaries(model)) return Dialect::Responses;

    // 其他一切（Copilot 上的 gpt-4o、claude-*、
    // 通用聊天模型）都保留其行所针对的方言。将 chat-only 系列
    // 拖到响应 400 上 — 该回归会破坏 Copilot 上的每个
    // Claude 转动，这就是为什么此默认值是保守的。
    return Dialect::Chat;
}

bool streams_reasoning_text(std::string_view provider_id,
                            std::string_view model) noexcept {
    const auto* row = preset_for(provider_id);
    if (!row) return true;   // unknown/custom主机：假设是，如前所述。

    if (row->wire == Wire::AnthropicMessages) return true;
    if (row->wire == Wire::Acp) return false;

    // ONE权限：询问轮将实际发送到哪里，
    // 然后回答THAT方言。这是曾经是第二次猜测的
    // 连接 - 错误标记的行可能会教 UI 承诺推理
    // 传输 永远不会实现的原因。
    switch (dialect_for(provider_id, model)) {
        case Dialect::Responses:
            // 响应携带推理摘要，但仅适用于实际产生它们的模型。
            // 空模型=provider-level "can anything here?"问题，
            // 这是肯定的。
            return model.empty() || model_emits_reasoning_summaries(model);
        case Dialect::Native:
            // Ollama和朋友：本地推理模型发出<think>跨度，
            // 传输层无条件地路由该跨度。
            return true;
        case Dialect::Chat:
            break;
    }

    // ── 聊天:谁放弃推理,为什么它不是提供商的事实 ──
    //
    // 两个主机在/chat/completions.上没有返回任何推理，
    // 两次拒绝都是MEASURED，并且都是在主机的OWN型号上测量的：
    //
    //   openai api.openai.com对于它所服务的任何事物都不进行任何努力
    //            （无论如何其推理模型是Responses-only）。
    //   副驾驶claude-*和gpt-4.x在那里没有返回任何内容。
    //
    // openai确实是主机-wide：该行上的每个模型都是第一个-party
    // OpenAI模型，因此"the host drops it"和
    // "these models drop it"是相同的陈述。
    //
    // 副驾驶的不是。该行是 GATEWAY——它与 OpenAI 一起服务
    // 于 Anthropic、Google、xAI、DeepSeek 和
    // Qwen 模型，并且在 claude-* 上进行的测量没有提及 deepseek-r1，
    // 它填充 reasoning_content 的方式与在 DeepSeek
    // 自己的端点上的填充方式完全相同。拒绝每个 PROVIDER 告诉
    // 混合主机上的每个模型的 UI "no thinking here"，
    // 因此推理模型的可见思维被扔在地板上，而 传输层 是 解析 很好。
    //
    // 所以：询问MODEL是否有理由，并仅针对实际接受的
    // 家庭保留拒绝。 supports_effort() 与
    // 选择面板 的 ✦ 标记和已读取的工作量阶梯具有相同的功能，
    // 因此没有新的列表可供漂移 - 无法推理的模型也无法
    // 推理 数据流，无论是谁为它提供服务。
    if (provider_id == "openai") return false;
    if (provider_id == "copilot") {
        if (model.empty()) return true;   // 提供商-level "anything?"→是
        const auto caps = ModelCapabilities::from_id(model);
        if (!caps.supports_effort()) return false;   // 不是推理模型
        // 尽管正在推理-capable，但在该路径上测得沉默。
        if (caps.is_opus() || caps.is_sonnet() || caps.is_haiku()) return false;
        return true;
    }

    // 该方言上的每个其他OpenAI-COMPAT主机(DeepSeek、Mistral、
    // OpenRouter的聊天路径、vLLM、Ollama)都填充reasoning_content，
    // 其中传输层无条件地填充解析。
    return true;
}

void note_dialect_rejected(std::string_view provider_id,
                           std::string_view model, Dialect rejected) noexcept {
    if (model.empty()) return;
    try {
        (void)observations().update(
            [](const Observations& old, const Key& k, const bool& responses) {
                Observations o = old;
                (responses ? o.rejected_responses : o.rejected_chat).insert(k);
                return o;
            },
            Key{std::string{provider_id}, std::string{model}},
            rejected == Dialect::Responses);
    } catch (...) {
        return;   // 此处的 分配 故障决不能中断转弯。
    }
    AGT_LOG(Wire, Info, "provider.dialect",
            "{}/{} rejected {} — routing the other way",
            provider_id, model, dialect_name(rejected));
}

void reset_dialect_observations() noexcept {
    observations().store(Observations{});
}

} // namespace agentty::provider
