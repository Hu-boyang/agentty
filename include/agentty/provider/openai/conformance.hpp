#pragma once
// agentty/provider/openai/conformance.hpp——我们发送什么以及发送给谁。
//
// ── 原因 "100% SPEC-CONFORMANT" IS 该 WRONG TARGET ───────────────────────
//
// 这听起来显然是正确的目标，并且采用它会在它所对话的大
// 多数端点上破坏 agentty。具体来说：
//
//   OpenAI DEPRECATED `max_tokens`
//   聊天完成有利于 `max_completion_tokens`。
//   严格遵守OpenAI的文件意味着发送新名称。但 vLLM、llama.cpp、
//   Together、Fireworks 和大多数兼容系列都接受 仅
//   `max_tokens` — 因此，符合该规范会破坏该规范命名的大部分生态系统。
//
// 没有单一的权威机构来符合TO(参见lens.hpp)：OpenAI自己的
// openapi文档和vLLM的服务器是两个de-facto来源，并且他们不同意。
// 所以目标不是"match the document"，而是：
//
//        必须根据当前实际通信的端点确保行为正确。
//
// 这是一个 per-endpoint 决策，并且这个 头文件 是那些决策
// 被写下的地方，而不是由埋在 正文 构建器中的 `if (use_tls)`
// 暗示的。
//
// ── 该 RULE ─────────────────────────────────────────────────────────────
//
// 请求字段落入三层之一。该层规定了谁可以接收它，
// 并且下面的每个层分配都引用了证据。
//
//   通用的每个OpenAI-shaped端点都接受它。总是发送。
//   托管大托管API拿它；本地服务器拒绝或阻塞。
//               在 TLS 上门控，这是 "not a laptop" 的诚实代理。
//   探测仅当端点为OBSERVED时才发送以支持它。
//               声称支持的注册表行不是证据（参见lens.hpp中的
//               Observed<T>和PR#53)。
//
// 故障模式不是对称的，这就是设置默认值的原因：
// 发送端点拒绝的字段是杀死回合的HARD 400/422；
// 省略一个字段会导致优化成本。因此，如有疑问，请省略。
//
// ── 和 TOLERANCE VARIES, SO IT CANNOT BE ASSUMED ────────────────────────
//
// 在 Yolo-Auto (计费代理后面的 vllm-0.29.1，2026-09-23)上测量：
// 它接受 `prompt_cache_key`、`reasoning_effort`
// 和 `max_completion_tokens` — 所有 HTTP 200、未知字段均被默默忽略。
// vLLM默认是允许的。
//
// 这正是从"did it work somewhere"派生出NOT层的原因。
// 一个容忍端点与严格端点没有任何关系：Mistral MAGISTRAL
// 对于该主机耸肩的同一个 `reasoning_effort` 返回 422。
// 仅当字段在我们支持的 STRICTEST 端点上工作时，
// 它才是通用的，绝不会因为最宽松的端点而让它通过。
//
// GitHub副驾驶的运行时从对方得到同样的结论：
// 其模型记录携带`supported_endpoints`和per-model
// `supportsReasoningEffort`，
// 并且它运送整个`reasoning_effort_fallback`路径(configured_effort
// →resultingEffort和primary_reason)，而不是假设配置的工作是可交付的。
// 能力是每个-endpoint数据，而不是全局常数。

#include <cstdint>
#include <string_view>

namespace agentty::provider::openai::conformance {

enum class Tier : std::uint8_t {
    Universal,  // 发送至一切
    Hosted,     // 仅TLS端点
    Probed,     // 仅在有确凿证据的情况下
};

struct FieldRule {
    std::string_view field;
    Tier             tier;
    std::string_view rationale;   // 证据，而非意见
};

// request-side决定，作为数据。向 正文 添加一个字段而不在此处添加，
// 这就是 6 个月后 422 on one 提供商如何成为一个谜的
// 原因。
inline constexpr FieldRule kRequestFields[] = {
    {"model", Tier::Universal,
     "required by every implementation"},

    {"messages", Tier::Universal,
     "the payload; universal"},

    {"stream", Tier::Universal,
     "universal. agentty always streams."},

    {"max_tokens", Tier::Universal,
     "DELIBERATELY the deprecated OpenAI spelling. OpenAI prefers "
     "max_completion_tokens, but vLLM / llama.cpp / Together accept only "
     "max_tokens, and OpenAI still accepts it. One name that works "
     "everywhere beats conformance that works on one host."},

    {"stream_options.include_usage", Tier::Universal,
     "added by OpenAI later, widely adopted. Verified present on Yolo-Auto "
     "(vllm-0.29.1, 2026-09-23). Endpoints that do not know it IGNORE it "
     "rather than erroring, so it is safe to send broadly."},

    {"tools", Tier::Universal,
     "the modern shape; `functions` is long dead. Verified round-tripping "
     "on Yolo-Auto (finish_reason=tool_calls, well-formed arguments)."},

    {"tool_choice", Tier::Universal,
     "sent as \"auto\", which is the default everywhere; harmless."},

    {"prompt_cache_key", Tier::Probed,
     "OpenAI prompt-cache routing, sent ONLY where the registry row opts in "
     "(ProviderPreset::accepts_prompt_cache_key: OpenAI, OpenRouter). It "
     "was Hosted (= every TLS endpoint) until #93: Groq ('property "
     "prompt_cache_key is unsupported') and Gemini ('Unknown name') are "
     "hosted AND strictly validated, so every turn on them died with a "
     "400. Local servers reject it too, and their KV cache is "
     "prefix-automatic anyway."},

    {"reasoning_effort", Tier::Probed,
     "o-series / DeepSeek-R1 / Grok / Mistral Small-Medium take it. Mistral "
     "MAGISTRAL reasons natively and REJECTS it with 422 — which is why the "
     "catalog gates it per-model and it arrives empty here rather than "
     "being re-checked at the wire. Evidence, not a capability table."},
};

// 字段agentty故意发送NOT，以及为什么。之所以保留，
// 是因为 "we tried that and it broke X" 是知识，否则只能存在于 git 日志中。
inline constexpr FieldRule kOmittedFields[] = {
    {"max_completion_tokens", Tier::Probed,
     "OpenAI's replacement for max_tokens. NOT sent: most of the compat "
     "family does not accept it, and OpenAI still honours max_tokens. "
     "Revisit only if OpenAI removes the old name."},

    {"temperature", Tier::Universal,
     "not sent. A coding agent wants the model's default sampling; pinning "
     "it fights per-model tuning, and some reasoning models REJECT a "
     "non-default temperature outright."},

    {"n", Tier::Universal,
     "not sent. One completion. n>1 multiplies cost for no agent benefit."},

    {"functions", Tier::Universal,
     "superseded by `tools` years ago. Sending it would opt into the legacy "
     "path on endpoints that still honour it."},
};

} // namespace agentty::provider::openai::conformance
