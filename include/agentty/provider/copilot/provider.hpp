#pragma once
// agentty::provider::copilot::CopilotProvider —本机 GitHub Copilot 提供商。
//
// Copilot 的聊天 API 是 OpenAI-Chat-compatible，
// 因此这是共享 openai 传输层 上的 THIN 包装器：在每一回合中，它确保新的
// Copilot 代理令牌（来自持久化 GitHub 令牌的 auto-exchanged/refreshed），
// 构建 per-account 推理端点（来自令牌的 `endpoints.api`
// 的主机，加上强制editor-identification 头文件），将令牌标记为
// 请求身份验证，并将 流式传输 委托给 openai::OpenAIProvider。
// 来自代理的 401（令牌提前撤销）触发一次强制刷新 + 重试。
//
// 它是 LongLived 提供者（拥有代币 缓存)——与 Anthropic/ChatGPT
// 提供者一样，在 main() 中构建一次。

#include <string>
#include <vector>

#include "agentty/domain/catalog.hpp"       // ModelInfo
#include "agentty/provider/provider.hpp"
#include "agentty/provider/openai/transport.hpp"  // openai::Endpoint
#include "agentty/provider/stream_epilogue.hpp"
#include "agentty/util/io.hpp"

namespace agentty::provider::copilot {

class CopilotProvider {
public:
    CopilotProvider() = default;

    provider::StreamResult stream(const maya::unlocked& cap, provider::Request req,
                                  provider::EventSink sink);

    // 为给定主机（从令牌交换）构建Copilot推理端点。由 stream()
    // 和 list_models() 共享。
    static provider::openai::Endpoint make_endpoint(const std::string& api_base);
};

static_assert(provider::Provider<CopilotProvider>);

// 已签名的-in Copilot帐户的实时模型目录（GET {
// endpoints.api}/模型）。该设置取决于帐户的权利（gpt-4o、
// o-series、Claude、Gemini …），因此 必须 会列出，而不是硬编码。
// 当离线/未登录时，回退到小的捆绑列表。
// 令牌交换/自动会话上的块(采用它们的等级-ordered 锁)，
// 因此调用者必须不持有锁。
[[nodiscard]] std::vector<ModelInfo> list_models(Io, const maya::unlocked& cap);

// 帐户的默认模型段（第一个 目录 条目）或安全 回退。
[[nodiscard]] std::string default_model(Io, const maya::unlocked& cap);

// 丢弃缓存的模型目录，以便下一个list_models()re
// -ranks具有任何新学习的per-model支持（在一轮的400/200结果之后）。
void invalidate_model_cache();

// TEST SEAM:解析自动会话实际上将数据流的模型。
//
// CONCRETE `requested` 被兑现或返回 ""
// — 从未交换为不同的型号。智能模式为每个角色槽固定一个模型，
// 并且静默替换会花费错误的模型预算，并且可以将
// Responses-only slug 路由到聊天路径上（直接
// 400 命名用户从未选择的模型）。只有合成自动输入代表了选择。
[[nodiscard]] std::string pick_auto_model_for_test(
    const std::vector<std::string>& available_models,
    const std::string& selected_model,
    const std::string& requested);

// ── 辨识选择(测试时使用) ──────────────────────────
//
// Copilot 是 agentty 的唯一 MIXED-dialect 提供者：
// 同一帐户在同一主机上为 /chat/completions 上的一些模型提供服务，
// 而在响应 API 上提供其他模型。模型在现场汽车会话中所说的哪一个是 MEASURED，
// 不是猜测的：
//
//   gpt-5-mini 两者 — 但 仅 /responses 仅返回推理文本 mai-code-1.1-flash
//   /responses (聊天 400s unsupported_api_for_model) claude-*,
//   gpt-4.1 /chat/completions 仅 (400 on /responses)
//
// 当模型应该通过响应方言流式传输时，自动会话将其列出的 GIVEN 是正确
// 的（会话的 copilot-session-token 是 /responses
// 的硬性要求 — 没有它，甚至 gpt-5-mini 也会被
// model_not_supported 拒绝）。
[[nodiscard]] bool prefers_responses_dialect(const std::string& model);

// 当模型已知为REJECT/chat/completions时确实如此，
// 因此响应是达到它的唯一方法（今天的mai-code-*系列）。
// 此类型号过去曾被完全从 选择面板 中过滤掉。
[[nodiscard]] bool chat_dialect_unsupported(const std::string& model);

} // namespace agentty::provider::copilot
