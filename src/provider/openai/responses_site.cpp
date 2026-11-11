// agentty::provider::openai-api.openai.com的响应-dialect站点
// (以及通告`responses_path`的任何OpenAI-compatible主机)。
//
// ─────────────────────────────────────────────────────────────────────────
// 为什么这个文件很小
// ─────────────────────────────────────────────────────────────────────────
// 响应方言的所有昂贵的东西——使用工具-call配对和图像部分构建`input[]`、
// 跨工具轮次重放推理`encrypted_content`以及SSE
// 状态机——已经存在于provider/responses/codec.cpp中并且逐字共享。
// 主机仅贡献三件事（responses::Site）：在哪里
// 拨号+如何进行身份验证、在中立的 正文 上分层的主机-specific
// 字段以及如何表述 HTTP 故障。这是第三个这样的主机，
// 编解码器不需要进行任何更改即可接受它 - 这是
// docs/PROVIDER_HETEROGENEITY.md
// 中的设计声明，现已执行。
//
// ─────────────────────────────────────────────────────────────────────────
// 为什么api.openai.com需要响应路径AT 全部
// ─────────────────────────────────────────────────────────────────────────
// 根据 OpenAI 的推理指南，关于聊天完成：从 GPT-5.4 起，
// 在除 `none` 之外的任何 reasoning_effort 上都不
// 支持工具调用，并且 GPT-6-class 模型完全放弃聊天功能调用。
// agentty 始终发送工具，因此在聊天方言中，这些模型根本无法运行代理轮。
// 路由由 provider::dialect_for() 决定——参见
// dialect.hpp；该文件只是目标。

#include "agentty/provider/openai/responses_site.hpp"
#include "agentty/version.hpp"

#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "agentty/auth/auth.hpp"
#include "agentty/io/http.hpp"
#include "agentty/provider/dialect.hpp"
#include "agentty/provider/openai/transport.hpp"
#include "agentty/provider/registry.hpp"
#include "agentty/provider/responses/responses.hpp"
#include "agentty/util/logx.hpp"

namespace agentty::provider::openai {
namespace {

using json = nlohmann::json;

// 本轮应拨打端点。它取决于活动的提供者行（openai vs
// openrouter vs 广告/响应的自定义主机），
// 因此 stream_responses() 从中构建目标并将其
// 直接传递给编解码器。
struct PendingTarget {
    std::string host;
    std::uint16_t port = 443;
    std::string path;
    bool use_tls = true;
    std::string provider_id;
    std::vector<std::pair<std::string, std::string>> extra_headers;
};
std::expected<responses::Target, std::string>
openai_target(const PendingTarget& p, const provider::Request& req) {
    // TLS主机必须携带凭据。 Local/plaintext 主机（
    // 一个 self-run vLLM 或讲响应的 llama.cpp）
    // 合法地没有，这反映了聊天 传输层 的规则，而不是发明第二个规则。
    const std::string key = auth::bearer_token(req.auth);
    if (p.use_tls && key.empty())
        return std::unexpected(
            std::string{"not authenticated — set the provider's API key "
                        "(e.g. OPENAI_API_KEY) or run `agentty login`"});

    responses::Target t;
    t.host = p.host;
    t.port = p.port;
    t.plaintext = !p.use_tls;
    t.path = p.path;
    t.headers = {
        {"content-type", "application/json"},
        {"accept",       "text/event-stream"},
        {"user-agent",   ::agentty::user_agent()},
        // 聊天路径以艰难的方式了解到这一点（问题＃30）：即使没有发送 Accept
        // -Encoding，网关 gzip 响应，并且 agentty 的 HTTP
        // 客户端在 流式传输 路径上没有 inflater。 Ask 代表身份。
        {"accept-encoding", "identity"},
    };
    if (!key.empty())
        t.headers.push_back({"authorization", "Bearer " + key});
    openai::append_session_header(t.headers, req.affinity());
    // Copilot-style 编辑器标识，或网关所需的附加功能。
    for (const auto& [k, v] : p.extra_headers)
        t.headers.push_back({k, v});

    // 呼叫者的模型在这里是权威的：与副驾驶的自动会话不同，
    // 没有服务器祝福该路径上的替代段。
    t.model = req.model;
    return t;
}

// 这里没有回合上下文； stream_responses() 永远不会通过它。
std::expected<responses::Target, std::string>
openai_authorize(const maya::unlocked&, provider::Request&) {
    return std::unexpected(std::string{"openai responses: no endpoint for this turn"});
}

void openai_decorate_body(json& body, const provider::Request&) {
    // agentty每次都会重播整个对话，因此服务器不需要
    // 记住任何内容。 store:false 还会将成绩
    // 单保留在 OpenAI 一侧，这是 终端 代理应该默认
    // 的行为 — 用户从未要求我们保留其代码 上游。
    body["store"] = false;

    // 返回每个推理项的encrypted_content，
    // 以便build_input()可以在下一轮重放它。 此 是使响应值得路
    // 由的功能：如果没有推理模型，re-derives 的每轮工具都会
    // 从头开始计划（更糟糕的答案，更多的标记，更糟糕的 缓存 命中）。
    // 在 store:false 下，它是 chain-of-thought 的唯一通道。
    //
    // 较新的 API 版本默认在 store:false
    // 下返回它；明确询问会被接受，并且在用户帐户
    // 可能固定的版本之间保持行为相同。
    body["include"] = json::array({"reasoning.encrypted_content"});
}

std::string openai_explain_http_error(int status, std::string_view body) {
    const std::string b{body};
    switch (status) {
        case 401:
        case 403:
            return "OpenAI rejected the API key (HTTP " + std::to_string(status)
                 + ") — check OPENAI_API_KEY or run `agentty login`";
        case 404:
            // 方言路由器将将此（提供者、模型）降级为聊天
            // for the rest of the process; say so, so the user understands why
            // 下一回合表现不同。
            return "this host has no /responses endpoint (HTTP 404) — "
                   "falling back to chat completions for this model";
        case 429:
            return "rate limited by OpenAI (HTTP 429)" +
                   (b.empty() ? std::string{} : " — " + b);
        default:
            if (status >= 500)
                return "OpenAI server error (HTTP " + std::to_string(status)
                     + ") — retrying usually works";
            return "OpenAI request failed (HTTP " + std::to_string(status) + ")"
                 + (b.empty() ? std::string{} : ": " + b);
    }
}

const responses::Site kOpenAiSite{
    .id                 = "openai-responses",
    .authorize          = &openai_authorize,
    .decorate_body      = &openai_decorate_body,
    .explain_http_error = &openai_explain_http_error,
};

// 命名 API/model 配对的 404 (没有这样的端点)或
// 400 意味着该主机将不会在响应上提供该模型。其他任何内容 - 401、
// 429、5xx、mid-stream 下降 - 与 凭据 或天气有关，
// 不 有关方言，并且不得降级真正需要响应的模型。
[[nodiscard]] bool indicates_dialect_unsupported(int status,
                                                 std::string_view body) noexcept {
    if (status == 404) return true;
    if (status != 400) return false;
    for (std::string_view needle : {"unsupported_api", "unsupported_api_for_model",
                                    "not supported", "unknown_url",
                                    "invalid_url", "must be used with"})
        if (body.find(needle) != std::string_view::npos) return true;
    return false;
}

} // 命名空间结束。

bool responses_endpoint_for(std::string_view provider_id,
                            ResponsesEndpoint& out) noexcept {
    const auto* row = preset_for(provider_id);
    if (!row || row->responses_path.empty()) return false;
    out.host = std::string{row->host};
    out.port = row->port;
    out.path = std::string{row->responses_path};
    out.use_tls = row->use_tls;
    // 填写WHOLE目的地，而不仅仅是URL。
    // 这两个被留给呼叫者，而一个呼叫者随后从其聊天端点中
    // 碰巧对他们进行了 补丁 - 因此该功能只有在拥有单
    // 个用户的情况下才正确，而第二个呼叫者将默默地拨号，
    // 没有身份，也没有路由反馈。归因使这一点变得具体：
    // OpenRouter是responses_path行，
    // 因此跳过这些头文件的响应回合将是主机上的UNCREDITED请求，
    // 其中被记入是重点。
    out.provider_id   = std::string{row->id};
    out.extra_headers = provider::attribution_headers(row);
    return true;
}

provider::StreamResult stream_responses(const ResponsesEndpoint& ep,
                                        provider::Request req,
                                        EventSink sink) {
    const PendingTarget pending{
        .host = ep.host, .port = ep.port, .path = ep.path,
        .use_tls = ep.use_tls, .provider_id = ep.provider_id,
        .extra_headers = ep.extra_headers,
    };
    auto target = openai_target(pending, req);
    const std::string provider_id = ep.provider_id;
    const std::string model = req.model;

    AGT_LOG(Wire, Info, "openai.responses",
            "dialling {}{} for {}", ep.host, ep.path, model);

    auto result = responses::stream(kOpenAiSite, std::move(target), std::move(req),
                                    std::move(sink));

    // ── 从传输的实际上说起路由器 ────────────────
    // dialect.cpp中的族表是关于型号NAMES的先前版本，
    // 并且名称移动得比版本快。回答 "no such endpoint"
    // 的主机会降级该确切的（提供商、模型），以便下一轮开始聊天，
    // 用户会看到一个正在工作的会话，而不是重复的失败。
    if (!result.ok() && indicates_dialect_unsupported(result.http_status,
                                                      result.error.value_or("")))
        note_dialect_rejected(provider_id, model, Dialect::Responses);

    return result;
}

} // namespace agentty::provider::openai
