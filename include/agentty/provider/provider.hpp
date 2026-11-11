#pragma once
// agentty::provider：能够流式生成聊天回复的提供方。
// 的抽象。  Provider 是域，而不是基础设施：对话与 Provider
// 进行对话，而 Provider 恰好与 Anthropic 端点（或者
// 在测试中与确定性 in-memory 脚本）进行 HTTP+SSE 对话。
//
// 运行时从不命名具体类型；任何满足这个概念的东西都会
// 被接受。

#include <concepts>
#include <functional>
#include <optional>
#include <string_view>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "agentty/auth/auth.hpp"
#include "agentty/domain/conversation.hpp"
#include "agentty/domain/reasoning_off.hpp"
#include "agentty/io/http.hpp"
#include "agentty/provider/selection.hpp"
#include "agentty/runtime/msg.hpp"

namespace agentty::provider {

struct ToolSpec {
    std::string name;
    std::string description;
    nlohmann::json input_schema;
    // Anthropic的fine-grained工具流式传输 标志 —
    // 完整情况参见tool/registry.hpp中的ToolDef。设置后，每个工具在 传输
    // 上镜像为 `eager_input_streaming: true`。
    bool eager_input_streaming = false;
    // 按需(#98)。 Anthropic：当 tool_search 返回
    // tool_reference 时，与 `defer_loading: true`、
    // 已加载 一起发送。其他线路永远不会看到延迟工具：规划器将其遗漏。
    bool defer_loading = false;
};

// Per-request输出上限与Claude代码v2.1.113的main-loop配置相匹配。
// 二进制程序 的文档明确警告 `max_tokens > ~16000` 会将
// 流量置于较慢的路径上，存在 SDK HTTP 超时 的风险 - 早期的 64000
// 默认值与 -stream 中的 20-30 秒暂停在较长的 write/edit
// 调用 上相关。
//
// Trade-off：`content`字段超过文件正文的~12-13k代币
// 的单个tool_use将达到上限并被截断（模型燃烧其预算流式传输 input_json_delta，
// 然后stop_reason = max_tokens）。
// 对于大多数edits/writes来说这很好；已知 -huge 代的每 -request
// 的碰撞。
inline constexpr int kSafeMaxTokens = 16384;

struct Request {
    std::string model;
    std::string system_prompt;
    std::vector<Message> messages;
    std::vector<ToolSpec> tools;
    int max_tokens = kSafeMaxTokens;

    // Model的真实上下文窗口（输入+输出令牌预算）。由 Ollama
    // 传输层 用于设置 options.num_ctx，这样长的代理对话
    // 就不会默默地被截断到 Ollama 的微小默认窗口。 0 = 未知（
    // 传输层 回退到安全代理-sized 默认值）。托管提供商会忽略它。
    // 由所选 ModelInfo 中的 launch_stream 设置。
    int context_window = 0;

    // 带类型的 凭据-变体臂命名为传输 头文件。 AuthHeader
    // 定义参见 agentty/auth/auth.hpp；
    // 传输层 从未见过宽松的（头文件，款式）一双。
    auth::AuthHeader auth;

    // 可选的取消手柄。从 UI 线程 上触发令牌会在约 200
    // 毫秒内撕毁 in-flight 数据流。 Null 表示不可取消。
    http::CancelTokenPtr cancel;

    // 0-based THIS轮的先前失败尝试计数(源ctx的
    // transient_retries)。历史上，传输 上显示为 x
    // -stainless-retry-count — 头文件（Anthropic
    // JS SDK 指纹 的一部分）是 NO LONGER 发送的，
    // 因为 agentty 标识为自身，而不是冒充 SDK/CLI.
    // 保留用于内部 重试 簿记，并可供任何需要它的提供商使用。
    int retry_count = 0;

    // Weak-model JSON-protocol模式(agent-zero风格)。
    // 由 launch_stream 设置，用于 Ollama 本机端点上的小型
    // 本地模型：传输层 删除本机 `tools` 数组，并将工具 目录 内联到 提示词 中，
    // 期望返回 一个 {tool_name,tool_args} JSON 对象。
    // 被未实现它的提供商忽略(Anthropic、OpenAI-compat)。
    bool json_protocol = false;

    // 推理努力(output_config.effort 传输值："low" | "medium"
    // | "high" | "xhigh" | "max")。空=省略——不思考，默认。
    // 已经由launch_stream限制到模型的能力（参见
    // effort_wire_for)。当 Anthropic 传输层
    // 不是 -empty 时，它还可以实现自适应思维。其他传输层忽略它。
    std::string effort;

    // 用户是否想要型号的reasoning/thinking SHOWN(型号
    // 选择面板中的全局Settings.show_reasoning、^R)。
    // 当为 true 时，Anthropic 传输层 请求 可见 思维（添加
    // interleaved-thinking beta、无需 redact
    // -thinking），因此其思维增量达到 传输 而不是被编辑。
    // 其他传输层已经无条件数据流他们的推理文本；这个 标志 不会改变他们的请求——它
    // 只控制视图处理的 DISPLAY。默认关闭（dead-air-free 传输）。
    bool show_reasoning = false;

    // 简短实用调用（线程标题，摘要标签）：答案就是几个字，
    // 所以一个思维模型不应该思考。 传输层 可以向服务器说
    // "don't think"；其他人则忽略它。
    bool quick = false;

    // 用户转向推理OFF (不仅仅是"no ladder")，
    // 以及该模型如何说关闭（ReasoningOff，原始；0=未知）。
    // 对于默认情况下思考的模型来说，忽略努力并不是关闭的，因此可以发送显式关闭
    // 的 传输层 可以这样做。
    bool         no_reasoning  = false;
    std::uint8_t reasoning_off = 0;

    // 稳定的主机-conversation身份。具有本机服务器-side
    // 线程（特别是Codex app-server）的提供者
    // 使用它来保留提供者线程，而不是将整个转录序列化到每个提示词中。
    // 空表示呼叫者没有持久的会话身份。
    std::string session_key;

    // 该请求所属的会话，作为 `x-session-id` 发送，
    // 因此路由网关（bifrost，OpenRouter）将其保留在最后
    // 为其提供服务的后端上。默认为session_key；子代理在此处设置其
    // 父代理的 线程 id，以便整个运行粘在一起（#118）。
    std::string affinity_key;
    [[nodiscard]] const std::string& affinity() const noexcept {
        return affinity_key.empty() ? session_key : affinity_key;
    }

    // 该请求的提供者是BUILT：其模型ID、认证和系统提示词都来自该选择。
    // dispatch_stream 通过它进行路由，因此轮流运行时
    // 提供者切换无法将该提供者的模型和 凭据 发送到新的 -picked
    // 提供者。 nullopt 意味着 "the active provider"，
    // 对于不针对特定调用者进行构建的调用者。
    std::optional<Selection> selection;
};

using EventSink = std::function<void(Msg)>;

// ── 统一请求降低 ─────────────────────────────────────────────
//
// `provider::Request`是每个适配器接收到的SUPERSET请求。
// 每个 HTTP 传输层 仍然有自己的本地 `Request`（anthropic::Request、
// openai::Request），仅携带 传输 所需的字段。从历史上看，
// 每个适配器都会手动将 provider::Request 逐个字段降低到本地
// 适配器中 - 这正是字段被悄悄删除的方式（openai 复制了 context_window；
// 人类忘记了 session_key）。
//
// `lower_shared`将EVERY 传输层共享的字段复制在一处，
// 由(member-of-dst=member-of-src)对保护，因此拼写错误不能编译。
// 适配器变为：`lower_shared(treq, req);`，然后设置该 传输（端点，
// json_protocol）特有的 1-2 个字段。在这里添加一个共享字段一次，
// 每个 传输层 都会继承它 - 任何适配器都不会忘记它。
//
// `effort`也被共享：它是模型-agnostic推理层（当模型无法推理时已经被
// effort_wire_for门控到""）。每个 传输 只是 ENCODES 不同（
// Anthropic thinking.budget_tokens、OpenAI-Chat
// reasoning_effort、响应 reasoning.effort） - 值 是单个 -source。
// （响应 传输层 直接从抽象请求中读取 req.effort，
// 而不是 调用 lower_shared，因此此处未将其列为消费者。）
//
// 在目的地上模板化，因此它适用于公开相同-named成员的任何传输层请求；
// require-clause 使缺失的成员在 调用
// 站点上出现一个明显的错误，而不是深入实例化。
template <class TReq>
    requires requires(TReq t, Request r) {
        t.model = std::move(r.model);
        t.system_prompt = std::move(r.system_prompt);
        t.messages = std::move(r.messages);
        t.tools = std::move(r.tools);
        t.max_tokens = r.max_tokens;
        t.auth = std::move(r.auth);
        t.retry_count = r.retry_count;
        t.effort = std::move(r.effort);
    }
void lower_shared(TReq& dst, Request& src) {
    dst.model         = std::move(src.model);
    dst.system_prompt = std::move(src.system_prompt);
    dst.messages      = std::move(src.messages);
    dst.tools         = std::move(src.tools);
    dst.max_tokens    = src.max_tokens;
    dst.auth          = std::move(src.auth);
    dst.retry_count   = src.retry_count;
    dst.effort        = std::move(src.effort);
    // 可选字段：只有Anthropic 传输层需要
    // show_reasoning（要求VISIBLE思维）；
    // 其他传输层 数据流无条件推理文本并且不声明它。如果目的地有它，
    // 则进行复制，因此这将保留 一个 降低站点，而不会强制每个 传输
    // 的本地请求增长它忽略的字段。
    if constexpr (requires { dst.show_reasoning = src.show_reasoning; })
        dst.show_reasoning = src.show_reasoning;
    if constexpr (requires { dst.quick = src.quick; })
        dst.quick = src.quick;
    if constexpr (requires { dst.reasoning_off = src.reasoning_off; }) {
        dst.no_reasoning  = src.no_reasoning;
        dst.reasoning_off = src.reasoning_off;
    }
}

// 流式回合如何结束，作为值（在 stream_epilogue.hpp 中定义，
// 每个 传输层 已经包含该值以结束其回合）。 Forward-declared
// 在这里，这样这个概念就可以命名它，而无需这个 头文件 拉入尾声。
struct StreamResult;

// ── 合同的每个提供者都满足 ────────────────────────────
// `stream`驱动一回合：它通过`sink`推送流式消息，
// AND返回命名该回合如何结束的StreamResult。
// 两者从来没有不一致——终端 Msg 与返回值来自相同的分类。
//
// 轮次在不持有锁的工作线程上运行，并且它阻塞(网络、
// 子进程、令牌刷新)，因此它被交给工作线程的`unlocked`。
// 该类型证明 锁 正文 内部没有调用者 数据流。
template <class P>
concept Provider = requires(P& p, const maya::unlocked& cap, Request req, EventSink sink) {
    { p.stream(cap, std::move(req), std::move(sink)) } -> std::same_as<StreamResult>;
};

} // namespace agentty::provider
