#pragma once
// agentty::provider::openai —响应 OpenAI-compat 系列的 -dialect
// 访问（api.openai.com、OpenRouter 以及 注册表 行带有 `responses_path`
// 的任何主机）。
//
// openai/transport.cpp中的聊天传输层继续拥有/chat/completions.
// 这个 头文件 是 OTHER 目的地，因为 provider::dialect_for
// () 路由到响应 — 这对于 GPT-5.4+ 和 GPT-6
// -class 模型来说不是优化，而是唯一接受工具 调用 的方言。
//
// 请参阅 responses_site.cpp 了解此文件为何如此薄：方言的实际工作
// （输入[]构造、加密推理重放、SSE 状态机）与 ChatGPT 和
// provider/responses/codec.cpp. 中的 Copilot 共享

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "agentty/provider/provider.hpp"
#include "agentty/provider/stream_epilogue.hpp"

namespace agentty::provider::openai {

// 应拨打响应轮次的地方，从注册表行解析。
struct ResponsesEndpoint {
    std::string   host;
    std::uint16_t port = 443;
    std::string   path;              // 该行的`responses_path`
    bool          use_tls = true;
    std::string   provider_id;       // 用于路由失败反馈
    // 该主机在每个调用上需要静态头文件(副驾驶的编辑器标识块；
    // 网关所需的额外内容)。在聊天路径上镜像
    // Endpoint::extra_headers。
    std::vector<std::pair<std::string, std::string>> extra_headers;
};

// 从提供商行的`responses_path`中填充`out`。
// 当该行没有通告任何响应端点时为 False — 调用者必须继续聊天。
[[nodiscard]] bool responses_endpoint_for(std::string_view provider_id,
                                          ResponsesEndpoint& out) noexcept;

// 数据流一翻Responses方言。
//
// 在将 ENDPOINT 识别为不可用的故障（404，
// 或命名 api/model 配对的 400）时，
// 这通过 note_dialect_rejected() 记录该事实，
// 因此该（提供商、模型）的下一回合被自动路由回聊天。 凭据 和rate
// -limit 失败被故意将NOT 视为方言证据。
[[nodiscard]] provider::StreamResult stream_responses(const ResponsesEndpoint& ep,
                                                      provider::Request req,
                                                      EventSink sink);

} // namespace agentty::provider::openai
