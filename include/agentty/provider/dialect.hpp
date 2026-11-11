#pragma once
// agentty::provider — 对应的 传输 DIALECT (提供商、模型)说话吗？
//
// ─────────────────────────────────────────────────────────────────────────
// 该文件为何存在
// ─────────────────────────────────────────────────────────────────────────
// 提供商行携带DEFAULT方言（`ProviderDescriptor::wire`)，
// 但主机并不总是一种方言。相同的 OpenAI API 密钥同时到达 /
// v1/chat/completions 和 /v1/responses；同一 Copilot
// 会话仅在聊天中为 claude-* 提供服务，在响应中为 mai-code-* 提供服务。
// 因此 "which dialect" 是（提供者，模型）PAIR 的属性，而不是单独的行的属性。
//
// 该问题先前在三个不同意的地方得到了回答：
//
//   1.该行的`wire`字段（静态的，并且它撒了谎）
//   2.copilot::prefers_responses_dialect()(per-provider，硬编码)
//   3.wire_streams_reasoning_text()(UI:"can we show ^R?")
//
// (2)和(3)是不同呼叫者提出的SAME问题——一个选择URL，
// 一个决定思考的面板是否可以承诺输出。当他们漂移时，
// UI 承诺推理 传输 永远不会发送，这正是错误标记
// 的 `openai` 行带来的错误。这个 头文件 使
// 它们成为一种功能，因此漂移是无法表示的。
//
// ─────────────────────────────────────────────────────────────────────────
// 为什么答案是MATTERS（这不是风格偏好）
// ─────────────────────────────────────────────────────────────────────────
// 根据 OpenAI 自己的推理指南，关于聊天完成：
//
//   ·从GPT-5.4开始，工具调用是UNSUPPORTED和任何reasoning_effort
//     `none`除外；
//   ·GPT-6-class型号-calling完全放弃聊天功能。
//
// 根据定义，代理是工具-calling推理器，因此对于这些模型，
// 聊天方言不是"degraded"，而是400。响应另外在工具轮次
// 之间保持推理状态（`encrypted_content`，
// 我们的编解码器已经重播），这就是阻止长工具链从re
// -deriving其每一跳的计划的原因。
//
// ─────────────────────────────────────────────────────────────────────────
// 其遵循的设计约束
// ─────────────────────────────────────────────────────────────────────────
// ·NO用户-visible表面。用户选择模型，而不是协议。那里
//   没有选择面板行、没有标志、没有弹出面板。他们唯一观察
//   到的是思维出现并且工具链保持一致。
// ·Model-name匹配ROTS。这里的每张表都是关于移动的启发式
//   目标，因此错误的猜测一定是SURVIVABLE，绝不会是致命的：
//   运行时记录观察到的故障（`note_dialect_rejected`)
//   并且下一转弯路由相反的方向。该表是先验的； 传输 是事实。
// • 答案是纯粹+便宜的：呼叫者按 帧（^R 可供性
//   当 选择面板 打开时询问）。

#include <cstdint>
#include <string>
#include <string_view>

namespace agentty::provider {

// 具体轮次应发送的方言。
enum class Dialect : std::uint8_t {
    Chat,        // OpenAI /chat/completions (或兼容主机的等效物)。
    Responses,   // OpenAI/响应。
    Native,      // 不是 OpenAI-family 方言（Anthropic、Ollama、ACP)。
};

[[nodiscard]] constexpr std::string_view dialect_name(Dialect d) noexcept {
    switch (d) {
        case Dialect::Chat:      return "chat";
        case Dialect::Responses: return "responses";
        case Dialect::Native:    return "native";
    }
    return "";
}

// ── 该 上游 ────────────────────────────────────────────────────────
//
// `provider_id`上的`model`应该通过哪种方言进行流式传输？
// 按顺序查询：行的广告功能（无 responses_path → 聊天）、
// 下面的 per-turn 运行时 证据（已经拒绝方言的主机永远不会重试它），
// 然后是前面的 model-family。
//
// `model`可以为空，意味着"does this provider EVER prefer Responses?"
// ——选择面板在选择模型之前询问的提供者-level问题。
[[nodiscard]] Dialect dialect_for(std::string_view provider_id,
                                  std::string_view model) noexcept;

// 当（提供者、模型）能够实际将推理TEXT传递到将被拨叫
// 的方言上的UI时，这是正确的。 ^R 可供性和思考 面板
// 都在此门上，因此 必须 是选择 URL 的同一权威
// - 因此它的家在这里而不是在 注册表 桌子旁边。
[[nodiscard]] bool streams_reasoning_text(std::string_view provider_id,
                                          std::string_view model) noexcept;

// ── 运行时 更正(反对) -rot 机制) ──────────────────────────
//
// 上面的系列表是 PRIOR，并且在新系列发货的那周，
// 每个先前的型号名称都是 过期。这两个调用让
// 传输修正了表格而无需发布：
//
//   ·主机在/responses上回答404/400/`unsupported_api_for_model`
//     在该过程的其余部分中将该（提供者、模型）降级为聊天；
//   ·拒绝推理模型聊天工具-calls的主机对其进行推广。
//
// 按(提供者，模型)记录，因此一个坏模型永远不会禁用其邻居的方言。
// Process-scoped 的设计：这是对
// 当今部署的观察，而不是值得保留到配置文件中的偏好，
// 然后用户必须理解并取消-set。
void note_dialect_rejected(std::string_view provider_id,
                           std::string_view model, Dialect rejected) noexcept;

// 测试接口边界：丢弃所有习得的拒绝，因此情况从先前的情况开始。
void reset_dialect_observations() noexcept;

// ── Model-family 先前(测试时暴露于+副驾驶shim) ───────────
//
// 当模型是 REASONING 模型时为真，其工具调用在聊天
// 完成上被降级或彻底拒绝——必须在响应上的群体。
// 与每个系列旁边的 measured/documented
// 基本原理保持为一个功能，因此该表有一个可以更新的位置。
[[nodiscard]] bool model_requires_responses(std::string_view model) noexcept;

// 当模型在响应上返回推理SUMMARY文本时为真。
// 模型可以要求响应而不发出可见的摘要，因此这是一个
// 比上面的问题严格范围更窄的问题。
[[nodiscard]] bool model_emits_reasoning_summaries(std::string_view model) noexcept;

} // namespace agentty::provider
