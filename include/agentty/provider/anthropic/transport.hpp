#pragma once
// agentty::provider::anthropic-与api.anthropic.com对话的传输层。
// 在 传输 上模拟 Claude 代码 CLI，以便接受 OAuth 令牌；
// 完整的 header/body 合同请参见 memory/project_claude_code_wire.md。

#include <functional>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json_fwd.hpp>  // 下面仅JSON签名；全类型住在transport.cpp

#include "agentty/auth/auth.hpp"
#include "agentty/domain/catalog.hpp"
#include "agentty/domain/conversation.hpp"
#include "agentty/io/http.hpp"
#include "agentty/provider/prompt.hpp"  // default_system_prompt/default_tools (提供商-neutral)
#include "agentty/provider/provider.hpp"   // provider::ToolSpec、provider::Request、lower_shared
#include "agentty/runtime/msg.hpp"
#include "agentty/util/io.hpp"

namespace agentty::provider {
// Forward-decl:共享数据流-epilogue结果(stream_epilogue.hpp)。
struct StreamResult;
}

namespace agentty::provider::anthropic {

// Re-export来自auth域的带类型的和凭据。将其保留在 `auth::`
// 中（此处不重复）可以让抽象 `provider::Request` 保留它，
// 而无需将人类 -specific 头文件 拖入其中。
using auth::AuthHeader;
using auth::ApiKeyHeader;
using auth::BearerHeader;
using auth::make_auth_header;
using auth::is_empty;

// 工具规格是提供者-agnostic：每个传输上具有相同的形状（名称、描述、
// JSON 模式定义、提前加载-streaming 标志）。别名为共享
// provider::ToolSpec 而不是 re-declared，因此抽象请求的工具直接
// 移至 anthropic::Request，无需 per-field 转换（请参阅
// provider::lower_shared）。一种类型，一种真理来源。
using ToolSpec = provider::ToolSpec;

struct Request {
    std::string model;
    std::string system_prompt;
    std::vector<Message> messages;
    std::vector<ToolSpec> tools;
    // 始终由AnthropicProvider从provider::Request
    // （拥有默认值）设置。这里没有初始化程序 - 保留一个风险相同的静默覆盖错误，
    // 该错误掩盖了长 write/edit 调用 的上限。
    int max_tokens;

    // 带类型的 凭据。 变体 手臂 IS 头文件 名称；无法发送（Bearer
    // -token，"x-api-key"），反之亦然，
    // 因为 build_request_headers 发送 std::visit 并且两
    // 个手臂发出不同的 头文件 名称。
    AuthHeader auth;

    // 每-turn 重试尝试计数，从provider::Request复制。
    // 在 传输 上标记为 x-stainless-retry-count。默认 0。
    int retry_count = 0;

    // 推理工作传输值("low".."max")，
    // 从provider::Request复制并且已经固定到模型的能力。
    // 空=没有思考（默认）。 Non-empty使run_stream_sync发送自适应思维
    // +output_config.effort并重放思维块。
    std::string effort;
    // 从 provider::Request 复制：当真正的 和 努力开始时，
    // 请求 可见 思维（interleaved-thinking beta），
    // 以便思维增量达到 传输 而不是被编辑。参见 beta_values_for。
    bool show_reasoning = false;
};

using EventSink = std::function<void(Msg)>;

// 在调用方 线程 上同步运行 流式传输 请求。每个 SSE 事件
// 通过 `sink` 作为 Msg 调度。返回 StreamResult，
// 命名回合如何结束（终端 Msg 源自同一分类）。
// `cancel`在帧边界被轮询；如果没有清洁器 终端 事件 首先到达，
// 则触发它会发送 RST_STREAM 并在大约 200 毫秒内返回 StreamError
// ("cancelled")。
provider::StreamResult run_stream_sync(Request req, EventSink sink,
                                       http::CancelTokenPtr cancel = {});

// 从我们的 线程 构建 Anthropic-shaped 消息数组。
[[nodiscard]] nlohmann::json build_messages(const Thread& t);

// 与 build_messages 相同，但直接发出原始 JSON 字符串（高频路径；
// 避免 JSON 源码树 round-trip）。在工具输出上应用 per
// -result 传输 字节-budget 上限。暴露进行测试。
[[nodiscard]] std::string messages_json_string(const Thread& t,
                                               bool include_thinking = false);

// default_system_prompt()+default_tools()现在存在于提供商
// -neutral prompt.cpp中。 Re-included 在这里，所以每个现有的
// `transport.hpp` 消费者仍然看到相同的公共表面 - 分裂是内部的。
// (参见agentty/provider/prompt.hpp)

// 从 Anthropic API 获取可用模型。采用 带类型的 AuthHeader，
// 因此 model-list 端点与 流式传输 路径共享相同的 头文件
// -vs-token 规则。
[[nodiscard]] std::vector<ModelInfo> list_models(Io io, const AuthHeader& auth);

// TEST SEAM：直接驱动SSE 事件 解析器。通过实时 on_chunk
// 路径使用的相同 dispatch_event() 馈送每个（事件，数据）对，
// 并返回 状态更新函数 将看到的确切 Msg 序列。让黄金测试固定 SSE→
// Msg 行为，以便 解析器 可以通过 byte/shape 保证进行重构（e.g。
// 拆分为自己的模块）——镜像 ChatGPT / OpenAI 提供商的
// parse_sse_for_test。
[[nodiscard]] std::vector<Msg> parse_sse_for_test(
    const std::vector<std::pair<std::string, std::string>>& events);

} // namespace agentty::provider::anthropic
