#pragma once
// agentty::provider::openai —与任何 OpenAI
// -compatible 聊天完成端点对话的 传输 层（OpenAI、Groq、OpenRouter、
// Together、Cerebras、本地 Ollama/llama.cpp 服务器等）。
//
// 为什么 "OpenAI-compatible" 是正确的抽象边界：这些后端中的每
// 一个都使用 `stream: true` 来表达 POST /
// v1/chat/completions 并发出 相同 SSE
// `data: {choices:[{delta:{...}}]}` 帧 形状。因此，一个 传输层，
// 参数化在基础 URL + 持有者密钥 + 型号 ID 上，点亮了整个家庭。差异归结为配置：
//
//   ·基础URL —api.openai.com/api.groq.com/openrouter.ai/本地主机
//   · auth — 授权：持有者 <key>（Ollama：无）
//   ·模型ID——provider-specific字符串
//
// 传输层将摘要provider::Request翻译成OpenAI JSON 正文，
// 读取SSE 数据流，并分派Anthropic 传输层执行的SAME agentty消息(
// StreamTextDelta/ StreamToolUseStart|Delta|End /
// StreamUsage / StreamFinished / StreamError /
// StreamHeartbeat）。 运行时、工具循环、retry/stall 看门狗 和 持久化 都是提供者
// -agnostic — 他们只看到这些消息。

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json_fwd.hpp>  // 下面仅JSON签名；全类型住在transport.cpp

#include "agentty/auth/auth.hpp"
#include "agentty/domain/catalog.hpp"
#include "agentty/domain/conversation.hpp"
#include "agentty/io/http.hpp"
#include "agentty/provider/openai/endpoint.hpp"
#include "agentty/provider/provider.hpp"
#include "agentty/runtime/msg.hpp"
#include "agentty/util/io.hpp"

namespace agentty::provider::openai {

using auth::AuthHeader;
using auth::BearerHeader;
using auth::ApiKeyHeader;
using auth::is_empty;

struct Request {
    std::string model;
    std::string system_prompt;
    std::vector<Message> messages;
    std::vector<provider::ToolSpec> tools;
    int max_tokens;
    AuthHeader auth;
    int retry_count = 0;
    Endpoint endpoint;
    // Model的真实上下文窗口(输入+输出令牌预算)，e.g。 32768 对于 qwen2.5
    // -coder:7b。从Ollama的/api/show
    // model_info探测并在ModelInfo.context_window上进行；
    // launch_stream 将其复制到此处。 Ollama 传输层 使用它来设置
    // `options.num_ctx`，因此较长的代理对话不会默默地被截断为 Ollama 的微小默认
    // 窗口 (~2k/4k)。 0 = 未知 → 传输层 回退到安全代理-sized 默认值。
    int context_window = 0;
    // 首先-class弱-model支持(agent-zero风格)。当 true 时，
    // Ollama 传输层 是否 不 发送本机 `tools` 数组；相反，它将工具 目录
    // 内联到系统 提示词 中，并指示模型使用 一个 JSON 对象
    // `{"thoughts":[...], "tool_name":"...", "tool_args":{...}}`
    // 进行应答。微小模型（<=8B）遵循 "emit one JSON object"
    // 比本机函数 -call 通道可靠得多，它们往往会忽略该通道或用格式错误的
    // JSON 填充。然后，salvage/extraction 路径（原谅first
    // -`{`..last-`}` 解析 和tool_name/tool_args
    // 别名）将该对象变成真正的工具调用。对于弱 Ollama 模型，
    // 按 launch_stream 设置 per-model。
    bool json_protocol = false;
    // 稳定的 per-conversation 身份（线程 id）。
    // 托管 OpenAI-family 端点将其作为 `prompt_cache_key`
    // 正文 字段进行回显，因此同一对话路由重复转向同一自动-prompt-cache
    // 节点（共享系统+工具+历史前缀上的 缓存-hit 速率较高）。
    // 空/本地(Ollama)端点省略它。
    std::string session_key;
    // 作为网关粘性发送为`x-session-id`；参见 provider::Request。
    std::string affinity_key;
    // 推理努力传输值("low"|"medium"|"high"|"xhigh"|"max")，
    // 或者没有推理为空。由 provider::lower_shared 复制。
    // OpenAI-Chat 传输层 当非-empty时将其编码为top-level
    // `reasoning_effort`；已经门控 上游（对于没有努力支持的模型，
    // effort_wire_for 返回 ""），因此 传输层 不需要 re-check 的功能。
    // Ollama 忽略它（本地型号没有 server-side 力度旋钮）。
    std::string effort;
    // 用户是否想要模型的推理SHOWN(全局^R 切换)。
    // 镜子provider::Request.show_reasoning和anthropic::Request。
    // show_reasoning，因此每个传输层读取相同的意图。
    // 在OpenAI-聊天传输上无条件推理文本数据流（reasoning_content），
    // 因此这仅门控DISPLAY（由视图处理）。 Ollama 传输层
    // 用它来决定是否发送 `think:true`（启用本机推理字段）。
    // 默认关闭使 dead-air-free 传输 保持不变。
    bool show_reasoning = false;
    // 镜像provider::Request::quick：要求服务器不思考。
    bool quick = false;
    // 镜子 provider::Request::no_reasoning/reasoning_off。
    bool         no_reasoning  = false;
    std::uint8_t reasoning_off = 0;
};

using EventSink = std::function<void(Msg)>;

// 当回合结束且没有文本且没有工具 调用 时 传输层 所写的内容，
// 因此气泡不是空白的。不是模型输出：只需要模型单词（线
// 程 标题）的调用者必须放弃它。
inline constexpr std::string_view kEmptyTurnText =
    "(the model returned an empty or unparseable response)";

// 在呼叫线程上同步运行流式传输 chat-completions请求。
// 每个 SSE 增量都会通过 `sink` 转换为 agentty Msg。
// 返回 StreamResult，命名回合如何结束。 `cancel` 在
// 帧 边界进行轮询。
provider::StreamResult run_stream_sync(Request req, EventSink sink,
                                       http::CancelTokenPtr cancel = {});

// 从我们的 线程 构建 OpenAI-shaped `messages` 阵列。暴露进行测试。
[[nodiscard]] nlohmann::json build_messages(const Thread& t);
// 同样，对于在没有自己的早期辅助转弯时需要 `reasoning_content`
// 的模型（provider::ReasoningEcho 作为其 字节，
// 来自 reasoning_echo_for；#117）：1=用工具 调用 转弯，2=每次转弯。
[[nodiscard]] nlohmann::json build_messages(const Thread& t, std::uint8_t reasoning_echo);

// 将我们的provider::ToolSpec列表翻译成OpenAI
// `tools: [{type:function, function:{name, description, parameters}}]`。暴露进行测试。
[[nodiscard]] nlohmann::json build_tools(const std::vector<provider::ToolSpec>& tools);

// 从端点的/v1/models.获取可用模型
//
// `force_probe` 运行 运行时-window 路由
// （/props、/api/ps、/api/v1/models)，
// 即使该地址看起来不是 self-hosted。在 ADD-HOST
// 时间使用，用户正在等待有关该主机的答案，并且额外的几毫秒可以区分真实
// 窗口和猜测之间的差异。例行刷新会将其设置为 false，因此托管的
// API 永远不会为其不提供服务的路由付费。
[[nodiscard]] std::vector<ModelInfo> list_models(Io io, const AuthHeader& auth,
                                                 const Endpoint& endpoint,
                                                 bool force_probe = false);

// 相同，以及为什么它返回空：当获取失败时，`error`被设置
// 为一句话（"HTTP 401: Invalid API key")，
// 并且当获取成功或主机以空列表应答时被清除。让呼叫者说出原因，
// 而不是显示空的 选择面板。
//
// out-parameter而不是thread_local这曾经是：
// 原因属于THIS 调用，并且线程-scoped侧通道都被禁止
// (jaal的并发lint)并且在两个提取共享工作线程时出错。
[[nodiscard]] std::vector<ModelInfo> list_models(Io io, const AuthHeader& auth,
                                                 const Endpoint& endpoint,
                                                 bool force_probe, std::string& error);

// 上下文窗口是/v1/models行DECLARES，或者当它声明无时为0。
//
// 暴露于测试，因为 0 是合同，而不是实现细节：它是
// "the gateway told us" 与 "nobody knows"
// 的区别，并且每个 下游 行为都与这种区别无关——选择面板 列仅在
// 0 上表示 "auto"，而 resolve_context_window
// () 仅落在 0 上的 models.dev/id
// 推断/200k 默认值。默默地解析为一个没有人声明的数字，
// 这是返回 0 来防止的错误。
//
// 行声明的内容并不总是服务器会遵守的内容，因此这里的非 -zero
// 答案并不是故事的结尾。对于 LOCAL 端点，list_models
// 仍会探测并采用两者中较小的一个：LM Studio 的 /v1
// 行通告 `max_context_length`（模型架构支持
// 的内容），而实例可能是 已加载 的一小部分，而 llama.cpp
// 的行携带所服务的 `meta.n_ctx` 和更大的
// `meta.n_ctx_train`。声明是有上限的；探测的是测量值。
[[nodiscard]] int advertised_context_window(const nlohmann::json& model_row);

// 窗口 llama-server ROUTER 行将获得一次 已加载，从路由器为其列出的启动
// 参数中读取（status.args:--ctx-size、--parallel、--kv-unified、
// --kv-unified-per-slot）。当 args 未固定大小时为 0。
//
// 未加载的路由器模型没有`meta`块，因此没有这个块，它们中的每一个都
// 根本不显示窗口。它是一个上限，而不是测量值：服务器还会将每个槽的上限
// 设置为模型的训练长度（server-context.cpp 中的
// n_ctx_slot()），而路由器未列出该长度。模型启动后，
// probe_loaded_window() 给出实数。
[[nodiscard]] int router_declared_window(const nlohmann::json& model_row);

// 服务器现在为`model_id`分配了ACTUALLY的窗口，
// 或者当它不能说时为0(型号不是已加载，主机不报告它)。
//
// 运行与目录探针相同的路由(llama.cpp /props、路由器/props?
// model=&autoload=false、LM Studio 已加载实例、
// Ollama /api/ps)，但仅返回测量的数字。切勿将 加载 模型用作 副作用。
// 意味着当路由器可能已经交换了驻留的模型时，在切换或转动之后
// 重新-checking一个模型。
[[nodiscard]] int probe_loaded_window(Io io, const AuthHeader& auth,
                                      const Endpoint& endpoint,
                                      const std::string& model_id);

// 该端点是否应该获得完整的运行时-window探测？
//
// "Local" 表示 SERVER YOU RUN，不是本机上的服务器：环回、
// RFC1918、链接-local、CGNAT、IPv6 ULA、*.local、
// 裸机单个 -label 主机或 AGENTTY_PROBE_HOSTS 中列出的
// 任何内容。其他一切都假设是其他人的 API，其中探测器的路由保证为 404。
//
// 暴露是因为它决定了NETWORK、BEHAVIOUR和UI，需要解释一下：
// 当主机应答但报告没有窗口时，我们ASKED是否是
// "this server doesn't say"和"we didn't check"的区别。
namespace detail {
[[nodiscard]] bool is_local_endpoint(const Endpoint& endpoint);
}

// 告知 传输层 用户选择进行探测的主机。
//
// 从 运行时 (Settings::probe_hosts)
// 推送 IN 而不是读出，因为该层不依赖于设置存储 —
// 这就是让 传输层 仅从端点和身份验证 头文件 构建的原因，
// 并且无需文件系统进行测试。 调用 启动时以及每当
// 设置更改时；取代之前的批发套装。
void install_probe_hosts(std::set<std::string> hosts);

// 该端点真的使用Ollama的本机/api/*协议吗？
//
// 11434是Ollama的PORT，不是其协议的证明——可以告诉llama.cpp
// 在那里服务，然后/api/chat每次404s，
// 而/v1/chat/completions工作正常(#73)。 /api/tags
// 的一个 GET，每个主机：端口缓存。无法访问算作本机，因此繁忙的守护
// 进程不会降级。
[[nodiscard]] bool endpoint_speaks_native(Io io, const Endpoint& ep);
// 已为该主机找到了答案endpoint_speaks_native，
// 无需询问它：当尚未询问时为nullopt。
[[nodiscard]] std::optional<bool> endpoint_speaks_native_cached(const Endpoint& ep);
// 测试：记录endpoint_speaks_native会为此主机找到什么。
void set_speaks_native_for_test(const Endpoint& ep, bool native);

// 添加到来自 LOCAL 服务器的 传输层 错误的提示。 "Is it running?"
// 仅当它从未应答时（`http_status` 0）；在它确实回答了名字之后，
// 出现了 超时 (#108)。当没有什么有用的东西可以添加时就为空。
[[nodiscard]] std::string local_transport_hint(const Endpoint& ep, int http_status,
                                               http::HttpErrorKind kind);

// ── Custom-host 方言探测器 ───────────────────────────────────
// 在提交定制主机之前，一个调用回答"what is actually running at this endpoint?"。
// 按顺序尝试：
//   1.端点配置的models_path(遵守显式前缀)
//   2./v1/models(默认OpenAI-dialect)
//   3./api/tags(Ollama的本机协议)
// 并报告哪个回答、它列出了多少型号以及往返时间。连接流程将其用于即时
// 弹出面板 反馈（"✓ 12 models · openai · 45ms"
// 与 "✗ nothing listening"）以及自动 -correct
// 端点（采用应答前缀；将 native_api 翻转为裸 Ollama
// 守护进程） — SOTA 加入移动：检测，不要询问用户。
struct HostProbe {
    enum class Dialect : std::uint8_t {
        None,          // 没有任何答复——主机关闭/端口错误
        OpenAiCompat,  // OpenAI-shape/型号已回答
        OllamaNative,  // /api/tags 已应答（裸 Ollama 守护进程）
    };

    // WHY探测失败，这不能单独从http_status推导出来。
    //
    // 三个截然不同的问题曾经作为同一消息到达：
    //
    //   ·200+HTML Web应用程序正在为该路径提供服务。粘贴仪表板
    //                  URL而不是API基是最常见的自定义-host错误，
    //                  并且真实提供商上的`https://host/models`
    //                  真正返回200 text/html——在yolo
    //                  -auto.com上验证。旧代码看到 200，然后失败到 解析，
    //                  并报告 "HTTP 200 — no model list at any known path"，
    //                  这没有告诉用户任何
    //                  实际错误。
    //   · 200+JSON可联系并且正在讲话JSON，但不是型号列表。
    //                  通常网关需要不同的前缀。
    //   ·401/403，端点是CORRECT并且想要密钥。这是一个
    //                  "did I type the host right?" 成功并且
    //                  绝不能读为 "nothing there"。
    //
    // 每一种都有不同的修复，因此每一种都需要不同的句子。
    enum class Failure : std::uint8_t {
        None,          // 有效
        Unreachable,   // 连接失败：主机错误、端口错误、服务器关闭
        NeedsKey,      // 401/403 — 右端点，缺少 凭据
        NotAnApi,      // 200但HTML:这是一个网页，不是API基地
        NoModelList,   // 200+JSON，但无法识别型号列表
        HttpError,     // 其他一些非200
    };

    Dialect     dialect      = Dialect::None;
    Failure     failure      = Failure::None;
    std::string models_path;     // 回答的路径（当无时为 ""）
    int         model_count  = 0;
    int         http_status  = 0;   // 最后看到的状态（0=连接失败）
    long        latency_ms   = 0;   // 应答请求的往返

    [[nodiscard]] bool ok() const noexcept { return dialect != Dialect::None; }

    // 用户可以根据一句话采取行动。将 此处 保留在分类法旁边，
    // 这样消息和分类就不会分开——因此 TUI 和任何未来
    // 的 CLI 表面读起来都是一样的。
    [[nodiscard]] std::string explain() const;
};
[[nodiscard]] HostProbe probe_host(Io io, const AuthHeader& auth,
                                   const Endpoint& endpoint);

// ── 请求正文, 纯粹的函数 ─────────────────────────────────
//
// 确切的 JSON agentty 放在 传输 上，源自请求，没有其他任何
// 东西 — 没有接收器、没有套接字、没有全局变量。
//
// 原因 IT IS OUT 此处：它过去是在 流式传输 函数内内联构建的，
// 所以查看我们发送的内容的唯一方法是 传输=实时 调用 上的跟踪日志。
// 这使得 "which fields go to which endpoints"
// 成为您通过阅读 高频路径 来回答的问题，
// 并将 conformance.hpp 的层保留为文档，没有任何强制执行的内容。
// 作为一个纯函数，正文 在 单元测试 中是可断言的，这就是将这些层从注释
// 变成合同的原因。
//
// 其实施的等级规则（每个的证据在conformance.hpp中）：
//   通用模型，数据流，max_tokens，stream_options，
//   仅托管prompt_cache_key — TLS的工具；本地服务器拒绝它
//   探测 reasoning_effort — 由 目录 到达 -gated 之前
[[nodiscard]] nlohmann::json build_request_body(const Request& req);

// 提供商的HTTP错误正文说的一句话，作为单行。
// 句柄{"error":{"message"}}、{"error":"..."}、
// {"message"}和Gemini的数组-wrapped[{"error":{...}}]；
// 回到有上限的原始摘录。暴露进行测试。
[[nodiscard]] std::string provider_error_message(std::string_view body);

// 公共请求头文件(accept/content-type/user-agent+授权)。
// 除非设置了 `endpoint.auth_header_name`，否则身份验证 头文件 为
// `authorization: Bearer <key>`，在这种情况下，密钥将以该名称为原始密钥。
// 暴露进行测试。
// "user"，当最新消息为用户带类型的时，否则为"agent"。
// 副驾驶的x-initiator：仅用户轮次被计费为额外请求。
[[nodiscard]] std::string initiator_for(const std::vector<Message>& msgs);

[[nodiscard]] http::Headers build_request_headers(const AuthHeader& auth,
                                                  const Endpoint& endpoint);

// 当存在时添加 `x-session-id: <key>`，
// 因此像 bifrost 或 OpenRouter 这样的路由网关会在最后为其提供
// 服务的后端上保持对话（#118）。在聊天中发送并回复 数据流。
void append_session_header(http::Headers& h, std::string_view key);

// 为OpenAI-compatible后端附加了额外的系统-prompt指南ONLY。
// 弱本地模型(Ollama qwen2.5-coder、
// llama.cpp模板)超过-call工具(e.g.在裸"hi"处发射
// `remember`)并且泄漏调用作为内容文本而不是结构化通道。
// 该附录促使他们在不需要工具时以纯文本方式聊天，并在需要工具时发出井 -formed 调用。
// 托管的 OpenAI/Groq 型号会无害地忽略它。
[[nodiscard]] std::string_view local_model_prompt_addendum();

// 用于本地/OpenAI-compat型号的全纤薄系统提示词。
// 替换（不 附加到）托管的 Claude 提示词：简短决策 -first
// 指令集 + 环境块 + 用户的 CLAUDE.md 内存 层
// + 技能 目录。冗长的 Claude 代理散文被省略，
// 因为它为小模型准备了 -call 工具并将其作为内容文本泄漏。
[[nodiscard]] std::string local_model_system_prompt();

// ACTION规则每一个本地-model 提示词携带（OpenAI-compat本地和Ollama本机共享这一块，
// 因此它们不能再次漂移）。存在是因为#91：llama.cpp上的gpt
// -oss得到了一个提示词，上面写着"call a tool ONLY when the task needs it"，
// 没有别的，所以在"implement UI.md"上，它将代码粘贴到回复中，
// 然后拒绝(
// "I can't write the entire codebase in a single response")（无法在单次回复中写出整个代码库。）
// ——从来没有人告诉过用工具编写文件，一次一个跨多个步骤，IS如何在这里完成
// 一项大任务。
[[nodiscard]] std::string_view local_model_action_rules();

// Test-only:通过现场数据流使用的同一解析器馈送完整的OpenAI
// SSE 字节 缓冲区并收集每个已调度的Msg。让 单元测试 验证增量→
// Msg 翻译（文本、工具-call 程序集、finish_reason、
// 用法、[DONE]），无需网络轮次-trip。
[[nodiscard]] std::vector<Msg> parse_sse_for_test(
    std::string_view sse_bytes,
    std::vector<std::string> known_tools = {},
    bool allow_memory_salvage = false,
    bool reason_by_default = false,
    // 镜子Request::show_reasoning。默认为 true，
    // 因此现有测试保持捕获语义；一致性套件驱动着这两个价值观。
    bool show_reasoning = true);

// 相同，对于本机Ollama/api/chat NDJSON路径(feed_ndjson)。
[[nodiscard]] std::vector<Msg> parse_ndjson_for_test(
    std::string_view ndjson_bytes,
    std::vector<std::string> known_tools = {},
    bool allow_memory_salvage = false);

} // namespace agentty::provider::openai
