// provider.cpp — Kimi代码提供商(openai 传输层上的瘦包装器)。
//
// 参见provider.hpp。 Kimi 在固定底座 URL 上说 OpenAI-Chat 传输，
// 所以每一回合是：
//   1.fresh_token()→有效的OAuth访问令牌（如果过期则刷新）。
//   2.构建端点(主机api.kimi.com，路径/coding/v1/chat/completions)。
//   3、下provider::Request→openai::Request，将token标记为auth。
//   4.委托给openai::run_stream_sync。
// 在401（令牌提前撤销）上，我们无效+刷新一次和重试。

#include "agentty/provider/kimi/provider.hpp"
#include "agentty/domain/bundled_catalog.hpp"

#include <chrono>
#include <optional>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <maya/runtime.hpp>

#include "agentty/provider/kimi/kimi_oauth.hpp"
#include "agentty/provider/openai/transport.hpp"
#include "agentty/io/http.hpp"
#include "agentty/util/dbglog.hpp"
#include "agentty/util/gen_cache.hpp"

namespace agentty::provider::kimi {

namespace {
// Kimi代码推断API(packages/oauth/src/managed-usage.ts，e22479a6):
//   DEFAULT_KIMI_CODE_BASE_URL=https://api.kimi.com/coding/v1
constexpr const char* kApiHost   = "api.kimi.com";
constexpr const char* kBasePath  = "/coding/v1";

using Models = std::vector<ModelInfo>;
util::GenCache<Models>& models_cache() { static util::GenCache<Models> c; return c; }

// 查询/coding/v1/usages.在帐户用完积分时返回人工消息（即使/chat
// 500s，Kimi在这里报告429 resource_exhausted），
// 或者当使用情况看起来不错/探测不确定时返回nullopt。
std::optional<std::string> quota_error_message(Io io, const std::string& access_token) {
    http::Request r;
    r.method = http::HttpMethod::Get;
    r.host   = kApiHost;
    r.port   = 443;
    r.path   = std::string{kBasePath} + "/usages";
    r.headers = {
        {"accept", "application/json"},
        {"authorization", "Bearer " + access_token},
    };
    for (auto& h : device_headers()) r.headers.push_back({h.first, h.second});
    if (const auto& ov = http::agentty_api_host_override(); ov.active()) {
        r.dial_host = ov.host; r.dial_port = ov.port;
    }
    http::Timeouts tos;
    tos.connect = std::chrono::milliseconds(5000);
    tos.total   = std::chrono::milliseconds(8000);
    auto resp = http::default_client(io).send(r, tos);
    if (!resp) return std::nullopt;
    // 402/429或resource_exhausted/配额正文=学分用完。
    const bool http_quota = resp->status == 402 || resp->status == 429;
    bool body_quota = false;
    try {
        auto j = nlohmann::json::parse(resp->body);
        std::string code = j.value("code", std::string{});
        std::string msg  = j.value("message", std::string{});
        if (code == "resource_exhausted" || msg.find("balance") != std::string::npos
            || msg.find("Credits") != std::string::npos
            || msg.find("quota") != std::string::npos)
            body_quota = true;
    } catch (const std::exception& e) {
        util::dbglog("kimi.quota_probe.parse", e.what());
    } catch (...) {
        util::dbglog("kimi.quota_probe.parse", "non-std exception");
    }
    if (http_quota || body_quota)
        return std::string{
            "Kimi credits exhausted \xe2\x80\x94 your Kimi Code plan is out of "
            "balance. Top up your plan at kimi.ai, or switch providers with ^P."};
    return std::nullopt;
}
} // 命名空间结束。

provider::openai::Endpoint KimiProvider::make_endpoint() {
    provider::openai::Endpoint ep;
    ep.host        = kApiHost;
    ep.port        = 443;
    ep.use_tls     = true;
    ep.path        = std::string{kBasePath} + "/chat/completions";
    ep.models_path = std::string{kBasePath} + "/models";
    ep.label       = "kimi";
    // Kimi 的服务器期望 OAuth 流发送的 API 请求（聊天+模型）上有
    // 相同的 X-Msh-* device-identity 头文件 — 官方
    // kimi_code_cli 将它们附加到各处。如果没有它们，/models 目录
    // 将返回空，并且某些请求会被拒绝。 SSOT：kimi_oauth::device_headers
    // () 中的一名构建者。由 openai 传输层 的
    // build_request_headers 在聊天和模型路径上应用。
    ep.extra_headers = device_headers();
    return ep;
}

provider::StreamResult KimiProvider::stream(const maya::unlocked& cap,
                                            provider::Request req,
                                            provider::EventSink sink) {
    // 提供者数据流在回合的工作线程 (分派)上运行，而不是在状态更新函数上运行。
    const Io io = IoAccess::grant();
    auto tok = fresh_token(cap);
    if (!tok) {
        sink(StreamError{"Kimi is not signed in (or the token could not be "
                         "refreshed) — run `agentty login` and choose Kimi."});
        return provider::StreamResult::failed("kimi: not authenticated");
    }

    auto run = [&](const KimiToken& t) -> provider::StreamResult {
        provider::openai::Request oreq;
        provider::lower_shared(oreq, req);
        oreq.context_window = req.context_window;
        oreq.session_key    = req.session_key;
        oreq.affinity_key   = req.affinity();
        oreq.endpoint       = make_endpoint();
        oreq.auth           = auth::BearerHeader{t.access_token};
        return provider::openai::run_stream_sync(std::move(oreq), sink, req.cancel);
    };

    auto result = run(*tok);
    // 令牌被撤销mid-life→刷新+重试一次。
    if (!result.ok() && (result.http_status == 401 || result.http_status == 403)) {
        invalidate_cached_token();
        if (auto fresh = fresh_token(cap)) result = run(*fresh);
    }
    // 当账户用完积分时，Kimi的编码API返回裸HTTP 500 (
    // "server had an error")——/usages端点报告真正原因(429
    // resource_exhausted)。在 500/429 上，探测 /usages 并显示
    // 清晰的 "credits exhausted" 消息，而不是不透明的服务器错误。
    if (!result.ok() && (result.http_status == 500 || result.http_status == 429)) {
        if (auto q = quota_error_message(io, tok->access_token)) {
            sink(StreamError{*q});
            return provider::StreamResult::failed("kimi: " + *q);
        }
    }
    return result;
}

// ── Model 列名 ────────────────────────────────────────────────────────────
static std::vector<ModelInfo> bundled_models() {
    // 单捆扎目录;一旦可以联系到帐户，
    // 实时 目录 就会取代它。
    return catalog::bundled("kimi");
}

std::vector<ModelInfo> list_models(Io io, const maya::unlocked& cap) {
    if (auto hit = models_cache().get(); !hit.empty())
        return hit;
    const std::uint64_t gen = models_cache().gen();
    auto tok = fresh_token(cap);
    if (!tok) return bundled_models();

    auto ep = KimiProvider::make_endpoint();
    auto models = provider::openai::list_models(io, auth::BearerHeader{tok->access_token}, ep);
    if (models.empty()) return bundled_models();
    // Kimi的/models 载荷不携带上下文长度，
    // 因此通用OpenAI 解析器将每一行保留在ModelInfo的
    // 200k默认值上——比K2的真实256k窗口短56k，
    // 这使得上下文规范和压缩阈值提前触发。对我们知道的任何 id 采用捆绑窗口。
    const auto seed = bundled_models();
    for (auto& m : models) {
        m.provider = "kimi";
        for (const auto& b : seed)
            if (b.id.value == m.id.value) { m.context_window = b.context_window; break; }
    }

    (void)models_cache().store_if(models, gen);
    return models;
}

std::string default_model(Io io, const maya::unlocked& cap) {
    auto ms = list_models(io, cap);
    return ms.empty() ? std::string{"kimi-k2-turbo-preview"} : ms.front().id.value;
}

void invalidate_model_cache() {
    models_cache().clear();
}

} // namespace agentty::provider::kimi
