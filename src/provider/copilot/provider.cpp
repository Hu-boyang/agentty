// provider.cpp — GitHub Copilot 提供商（openai 传输层 上的薄包装器）。
//
// 参见provider.hpp。副驾驶说的是 OpenAI-Chat 传输，所以每个回合是：
//   1.fresh_token()→有效代理令牌+per-account推理主机。
//   2.构建Endpoint(主机来自endpoints.api+编辑器头文件)。
//   3、下provider::Request→openai::Request，将token标记为auth。
//   4.委托给openai::run_stream_sync。
// 在401 (代理令牌提前撤销）上，我们无效+刷新一次和重试。

#include "agentty/provider/copilot/provider.hpp"
#include "agentty/domain/bundled_catalog.hpp"

#include <algorithm>
#include <cstdint>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include "agentty/provider/copilot/copilot_oauth.hpp"
#include "agentty/provider/dialect.hpp"
#include "agentty/provider/openai/transport.hpp"
#include "agentty/provider/responses/responses.hpp"
#include "agentty/io/http.hpp"
#include "agentty/util/dbglog.hpp"
#include "agentty/util/gen_cache.hpp"

#include <maya/runtime.hpp>


namespace agentty::provider::copilot {
namespace {

// 分割"https://host[:port]"→(主机、端口、tls)。 Copilot 始终使用 https。
struct HostPort { std::string host; std::uint16_t port = 443; bool tls = true; };
HostPort parse_api_base(const std::string& base) {
    HostPort hp;
    std::string_view s = base;
    if (s.rfind("https://", 0) == 0) { s.remove_prefix(8); hp.tls = true; }
    else if (s.rfind("http://", 0) == 0) { s.remove_prefix(7); hp.tls = false; hp.port = 80; }
    // 剥离任何尾随路径
    if (auto slash = s.find('/'); slash != std::string_view::npos) s = s.substr(0, slash);
    if (auto colon = s.find(':'); colon != std::string_view::npos) {
        hp.host = std::string{s.substr(0, colon)};
        try { hp.port = static_cast<std::uint16_t>(std::stoi(std::string{s.substr(colon + 1)})); }
        catch (...) { util::dbglog("copilot.api_base.parse", base); }
    } else {
        hp.host = std::string{s};
    }
    if (hp.host.empty()) hp.host = "api.githubcopilot.com";
    return hp;
}

// editor-identification 头文件 阻止 Copilot 对每个请求的要求。
std::vector<std::pair<std::string, std::string>> copilot_headers() {
    return {
        {"copilot-integration-id", "vscode-chat"},
        {"editor-version", client_editor_version()},
        {"editor-plugin-version", client_plugin_version()},
        {"openai-intent", "conversation-panel"},
        {"user-agent", client_user_agent()},
    };
}

using Models = std::vector<ModelInfo>;
// 在 invalidate_model_cache()之前开始的获取不得将其 now
// -stale 列表存储在其上； GenCache 放弃该商店。
util::GenCache<Models>& models_cache() { static util::GenCache<Models> c; return c; }

} // 命名空间结束。

provider::openai::Endpoint CopilotProvider::make_endpoint(const std::string& api_base) {
    auto hp = parse_api_base(api_base);
    provider::openai::Endpoint ep;
    ep.host          = hp.host;
    ep.port          = hp.port;
    ep.use_tls       = hp.tls;
    ep.path          = "/chat/completions";
    ep.models_path   = "/models";
    ep.label         = "copilot";
    ep.extra_headers = copilot_headers();
    return ep;
}

namespace {
// 合成的"Auto"模型id agentty在选择面板中公开。
// 选择它可以让 GitHub 的服务器将每个回合路由到帐户可能使用的最佳模型。
constexpr const char* kAutoId = "copilot-auto";

// gpt-4o-family 基本模型通过 DIRECT 聊天请求在每
// 个计划上运行 — 它们必须通过自动会话（未列出它们）强制不执行。
bool is_base_direct(const std::string& id) {
    return id == "gpt-4o" || id == "gpt-4.1" || id == "gpt-4o-mini"
        || id == "gpt-4o-copilot" || id == "gpt-4.1-mini"
        || id.rfind("gpt-4o-mini", 0) == 0;
}

// 某些汽车模型仅说话/响应（mai-code-*)，agentty的OpenAI
// -Chat 传输层无法驾驶。更喜欢 chat-completions 型号。
//
// NOTE这个现在只能用来挑选CHAT-dialect 回退。
// 由于存在下面的响应站点，mai-code-* 选择不再是死胡同：
// dialect_for()将其路由到/responses而不是跳过它。
bool auto_chat_compatible(const std::string& id) {
    return id.rfind("mai-code", 0) != 0;
}

// ── THIS模式会讲哪一种方言?. ────────────────────────────
//
// Copilot 是第一个真正的 MIXED 提供者：同一帐户，在同一主机上，
// 在 /chat/completions 上提供一些模型，在 /responses 上
// 提供其他模型。使用自动会话针对 api.individual.githubcopilot.com
// 进行测量（请参阅提交中的探测注释）：
//
//   gpt-5-mini 两者 — /responses 返回 真实 推理摘要 (SSE
//                             response.reasoning_summary_text.delta)
//   mai-code-1.1-flash /responses 仅 (聊天上 400 unsupported_api_for_model)
//   claude-*, gpt-4.1 /chat/completions 仅 (/responses 上 400)
//
// 两个硬性规则，均测量：来自自动会话的 `copilot-session-token` 是
// REQUIRED (甚至没有 gpt-5-mini 400s model_not_supported)，
// 并且仅接受 available_models[] 中列出的会话的模型。
//
// 因此，当我们举行支持模型的自动会话时，使用响应。
// 其他一切都保持聊天路径——无需猜测，除了 SERVER 告诉我们它
// 可以路由的内容外，没有硬编码 允许清单。
//
// Chat/Responses 枚举本身现在位于provider/dialect.hpp中
// ——当Copilot是唯一的混合-dialect主机时，它被复制到这里。

[[nodiscard]] bool session_lists_model(const AutoSession& as,
                                       const std::string& model) {
    for (const auto& m : as.available_models)
        if (m == model) return true;
    return false;
}


// Auto会话数据流的具体型号是什么？
//
// ONE实现，由BOTH方言路径使用——响应分支必须
// 解析与聊天路径相同的slug，否则在它们之间切换的
// 用户将默默地获得不同的模型。
//
// CONCRETE请求被接受或者是FAILS。它永远不会悄悄地交换为另
// 一个模型：固定 `gpt-5.6-luna` 并默默地获得
// `gpt-5.3-codex` 的用户（或智能模式角色槽）会得到错误的延迟、
// 错误的成本、错误的功能 - 并且，当替代品是 Responses-only
// 但聊天路径被采用时，会出现彻底的 400。只有当用户要求我们选择 i.e 时，
// 替代才是正确的。用于合成自动输入。
//
// 当无法服务具体请求时，返回""；调用者将其转变为
// 命名模型的明显错误。
[[nodiscard]] std::string pick_auto_model(const AutoSession& as,
                                          const std::string& requested) {
    if (requested != kAutoId) {
        for (const auto& m : as.available_models)
            if (m == requested) return requested;
        return {};   // 已固定但不可用 — 这么说，不要重新路由
    }
    // 自动：用户委托选择。更喜欢服务器自己选择，然后是任何 chat
    // -capable 模型，然后是提供的任何内容。
    //
    // 空性检查很重要：auto_chat_compatible("")
    // 为真（空字符串不以"mai-code"开头），
    // 因此不返回selected_model的会话否则将解析为EMPTY
    // 模型段和数据流根本没有模型的请求。
    if (!as.selected_model.empty() && auto_chat_compatible(as.selected_model))
        return as.selected_model;
    for (const auto& m : as.available_models)
        if (auto_chat_compatible(m)) return m;
    return as.available_models.empty() ? std::string{}
                                       : as.available_models.front();
}

// ── 副驾驶反应网站 ──────────────────────────────────
//
// 副驾驶的/响应需要每回合解析ONCE的凭据（代理令牌
// +自动会话，两者均可刷新）。 stream() 解决
// 它们并将构建的目标直接交给编解码器。
struct ResponsesTurn {
    std::string host;          // 本场推理主持人
    std::string token;         // 代理令牌(Bearer)
    std::string session_token; // 副驾驶-session-token —REQUIRED，测量
    std::string model;         // 服务器-blessed 蛞蝓
    std::string initiator = "user";   // x-initiator:"user"或"agent"
};

std::expected<provider::responses::Target, std::string>
copilot_target(const ResponsesTurn& t) {
    if (t.token.empty() || t.session_token.empty())
        return std::unexpected(std::string{
            "copilot: no Auto session for the Responses API — sign in again "
            "with `agentty login` and choose GitHub Copilot"});
    provider::responses::Target target;
    target.host = t.host.empty() ? std::string{"api.githubcopilot.com"} : t.host;
    target.port = 443;
    target.path = "/responses";
    target.model = t.model;
    target.headers = {
        {"authorization", "Bearer " + t.token},
        {"content-type", "application/json"},
        {"accept", "text/event-stream"},
        // Auto-session令牌是解锁/响应的根本：没有它，每个模型——包括
        // gpt-5-mini——400s model_not_supported。
        {"copilot-session-token", t.session_token},
        {"x-github-api-version", client_api_version()},
        // 只有用户轮次才算作高级请求；工具轮是特工的。
        {"x-initiator", t.initiator},
    };
    for (auto& h : copilot_headers()) target.headers.push_back({h.first, h.second});
    return target;
}

// 本站自身授权无转弯上下文； stream() 永远不会通过它（
// 而是传递 copilot_target 的结果）。
std::expected<provider::responses::Target, std::string>
copilot_authorize(const maya::unlocked&, provider::Request&) {
    return std::unexpected(std::string{
        "copilot: no Auto session for the Responses API — sign in again "
        "with `agentty login` and choose GitHub Copilot"});
}

void copilot_decorate_body(nlohmann::json& body, const provider::Request&) {
    // Copilot 是用于响应的有状态服务器-side；
    // 与 Codex 后端不同，我们不执行 发送 store:false
    // 或要求加密推理（它拒绝包含）。 `summary: auto`
    // — 已由共享构建器设置 — 是使推理文本实际返回的原因。
    body.erase("store");
    body.erase("include");
}

std::string copilot_explain_http_error(int status, std::string_view body) {
    std::string detail;
    try {
        auto j = nlohmann::json::parse(body);
        if (j.contains("error")) {
            const auto& e = j["error"];
            if (e.is_string()) detail = e.get<std::string>();
            else if (e.is_object())
                detail = e.value("message", e.value("code", std::string{}));
        }
    } catch (const std::exception& e) {
        util::dbglog("copilot.http_error.decode", e.what());
    } catch (...) {}
    std::string msg = "copilot returned HTTP " + std::to_string(status);
    if (!detail.empty()) msg += ": " + detail;
    if (status == 401 || status == 403)
        msg += " — sign in again with `agentty login` and choose GitHub Copilot";
    return msg;
}

const provider::responses::Site kCopilotSite{
    .id                 = "copilot",
    .authorize          = &copilot_authorize,
    .decorate_body      = &copilot_decorate_body,
    .explain_http_error = &copilot_explain_http_error,
};

// 自动端点携带会话令牌+CAPI API-version，
// 因此服务器接受free/limited计划只能通过自动到达的模型。
provider::openai::Endpoint make_auto_endpoint(const std::string& api_base,
                                              const std::string& session_token) {
    auto hp = parse_api_base(api_base);
    provider::openai::Endpoint ep;
    ep.host        = hp.host;
    ep.port        = hp.port;
    ep.use_tls     = hp.tls;
    ep.path        = "/chat/completions";
    ep.models_path = "/models";
    ep.label       = "copilot";
    ep.extra_headers = copilot_headers();
    ep.extra_headers.push_back({"x-github-api-version", client_api_version()});
    ep.extra_headers.push_back({"copilot-session-token", session_token});
    return ep;
}
} // 命名空间结束。

provider::StreamResult CopilotProvider::stream(const maya::unlocked& cap,
                                               provider::Request req,
                                               provider::EventSink sink) {
    auto tok = fresh_token(cap);
    if (!tok) {
        sink(StreamError{"GitHub Copilot is not signed in (or the token could "
                         "not be refreshed) — run `agentty login` and choose "
                         "GitHub Copilot."});
        return provider::StreamResult::failed("copilot: not authenticated");
    }
    if (!tok->chat_enabled) {
        sink(StreamError{"This GitHub account has no Copilot Chat entitlement."});
        return provider::StreamResult::failed("copilot: chat not enabled");
    }

    const std::string requested = req.model;
    // 当所选择的模型是合成自动条目或者账户只能通过会话到达的
    // 高级模型时，通过自动进行路由。基础 gpt-4o
    // 系列型号运行 DIRECT（它们不在自动设置中），
    // 我们直接确认工作的型号也会跳过自动。
    //
    // 当模型更喜欢响应方言时，ALSO通过自动路由。自动会话的
    // `copilot-session-token` 是 凭据 /响应
    // REQUIRES（测量：甚至没有 gpt-5-mini 400s
    // model_not_supported），因此已经在聊天路径上工作
    // 的 gpt-5 模型将永远不会获得会话 - 并且永远不会显示推理。
    // Responses-only模型(mai-code-*)出于同样的原因需要它。
    const bool wants_auto = (requested == kAutoId)
        || (!is_base_direct(requested) && !is_supported_model(cap, requested))
        || prefers_responses_dialect(requested);

    std::optional<AutoSession> as;
    if (wants_auto) as = auto_session(cap);

    auto run = [&](const CopilotToken& t) -> provider::StreamResult {
        // ── 回复叉 ─────────────────────────────────────────
        // MUST在lower_shared()之前出现，其中MOVES
        // model/messages/tools从`req`出来进入chat-shaped请求。
        // 分叉后，它将向响应编解码器传递一个已删除的请求 - 空输入[]，空指令 -
        // Copilot 会以 `One of "input" … must be provided`
        // 拒绝该请求。 （确实如此，直到测量为止。）
        //
        // 自动会话正是 凭据/responses 所要求的，它告诉我们服务器将
        // 路由哪些模型。当选择其中之一时，更喜欢 Responses 方言：这是
        // Copilot 返回推理 TEXT 的 仅 方式（测量 — gpt-
        // 5-mini 数据流 response.reasoning_summary_text.delta，
        // /chat/completions 上没有任何内容），
        // 并且它是我们过去完全跳过的 Responses
        // -only 模型 (mai-code-*) 的唯一路径。
        //
        // 聊天对于其他一切都保持默认，因此 Claude/GPT-4.1
        // 回合是 字节-identical 到此分叉存在之前。
        if (as && as->valid()) {
            const std::string picked = pick_auto_model(*as, requested);
            // 自动会话未列出的具体引脚不是 错误和 不 交换模型的提示：
            // 帐户可能有权在 DIRECT 聊天端点上使用它。
            // 这正是官方 VS 代码客户端 数据流 e.g 的方式。
            // Claude Opus — 具体的 -picked 模型使用
            // 代理令牌直接到达 /chat/completions，
            // 而不是通过 /models/session 路由。因此，
            // 当 `picked` 对于具体 pin 为空时，我们就会失败：
            // 下面的聊天块使 `picked` 为空 ⇒ make_endpoint（直接，
            // 无会话令牌）和 数据流 正是所请求的模型。如果该帐户没有被授权，
            // 服务器会回答 model_not_supported，
            // 如下所示 - 与官方客户端将显示的失败相同。 （智能模式
            // 角色引脚在这里逐字保留，绝不会默默地替换。）
            const bool responses_capable =
                !picked.empty() && session_lists_model(*as, picked)
                && prefers_responses_dialect(picked);
            if (responses_capable) {
                const ResponsesTurn rt{
                    .host          = parse_api_base(as->endpoint_api).host,
                    .token         = t.token,
                    .session_token = as->session_token,
                    .model         = picked,
                    .initiator     = provider::openai::initiator_for(req.messages),
                };
                provider::Request rr = req;   // 这里还是INTACT
                rr.model = picked;
                return provider::responses::stream(kCopilotSite, copilot_target(rt),
                                                   std::move(rr), sink);
            }
        }

        provider::openai::Request oreq;
        provider::lower_shared(oreq, req);
        oreq.context_window = req.context_window;
        oreq.session_key    = req.session_key;
        oreq.affinity_key   = req.affinity();
        if (as && as->valid()) {
            // 响应分支使用相同的解析器——一种实现。
            // 上面已经证明了具体的引脚可用（该路径提前返回），
            // 因此这里的 `picked` 要么是引脚本身，
            // 要么是 Auto 自己的选择。无法替代，因此不另行通知。
            const std::string picked = pick_auto_model(*as, requested);
            if (picked.empty()) {
                // Auto 根本没有什么可提供的——回退到直接
                // 端点而不是 流式传输 一个空的模型段。
                oreq.endpoint = make_endpoint(t.endpoint_api);
            } else {
                oreq.model    = picked;
                oreq.endpoint = make_auto_endpoint(as->endpoint_api, as->session_token);
            }
        } else {
            oreq.endpoint = make_endpoint(t.endpoint_api);
        }
        oreq.auth = auth::BearerHeader{t.token};
        return provider::openai::run_stream_sync(std::move(oreq), sink, req.cancel);
    };

    auto result = run(*tok);
    // 代理令牌被撤销mid-life→刷新+重试一次。
    if (!result.ok() && (result.http_status == 401 || result.http_status == 403)) {
        invalidate_cached_token();
        if (auto fresh = fresh_token(cap)) result = run(*fresh);
    }
    // 自动会话expired/stale→刷新+重试一次。
    if (!result.ok() && wants_auto && result.http_status == 400) {
        invalidate_auto_session(cap);
        as = auto_session(cap);
        if (as) result = run(*tok);
    }
    // LEARN直接模型支持（仅对直接-requested模型有意义，
    // 对自动伪-id没有意义）。
    if (requested != kAutoId) {
        // 这是通过自动会话转数据流，还是直接转？会话仅在列出
        // 模型时才提供模型；其他任何内容（会话省略的具体引脚，
        // 或者根本没有会话）直接针对 /chat/completions.
        // 进行流式传输
        const bool via_session =
            as && as->valid() && session_lists_model(*as, requested);
        if (result.http_status == 400 && result.error
            && result.error->find("not supported") != std::string::npos) {
            note_unsupported_model(cap, requested);
            invalidate_model_cache();
        } else if (result.ok() && !via_session) {
            // DIRECT 数据流成功——要么是已经-direct的模型，
            // 要么是自动会话未列出帐户有权直接访问的固定（VS代码路径）。
            // 记住这一点，以便下一回合跳过自动探测并立即直接 数据流。
            // 保留到磁盘，因此它可以在重新启动后继续存在。需要会话（通过 /
            // responses 进行 gpt-5 推理）的模型通过
            // prefers_responses_dialect 保持
            // wants_auto 设置，因此这永远不会让他们陷入聊天困境。
            note_supported_model(cap, requested);
        }
    }
    return result;
}

// ── Model 列名 ────────────────────────────────────────────────────────────
static std::vector<ModelInfo> bundled_models() {
    // 单捆目录；一旦可以联系到帐户，实时 目录 (
    // list_models) 就会取代它。
    return catalog::bundled("copilot");
}

std::vector<ModelInfo> list_models(Io io, const maya::unlocked& cap) {
    if (auto hit = models_cache().get(); !hit.empty())
        return hit;
    const std::uint64_t gen = models_cache().gen();
    auto tok = fresh_token(cap);
    if (!tok || !tok->chat_enabled) {
        // 没有模型，或只剩过期模型。
        // 报告：从未获取实时目录。说出原因 - 没有 CAPI
        // 令牌与禁用聊天的帐户（组织策略） - 因此共享日志可以
        // 区分 Sign-in 问题和权利问题。
        AGT_LOG(Auth, Warn, "copilot.models.fallback",
                "reason={} -> bundled catalog",
                !tok ? "no_capi_token" : "chat_disabled");
        return bundled_models();
    }

    // 直接获取 /models，以便我们可以读取通用 OpenAI 解析器 丢弃的
    // Copilot 丰富的 per-model 元数据（policy.state、
    // 功能、model_picker_category）。这就是让我们展示帐户可以在
    // 上面使用 ACTUALLY 的模型。
    std::string host = tok->endpoint_api;
    if (host.rfind("https://", 0) == 0) host = host.substr(8);
    if (auto slash = host.find('/'); slash != std::string::npos) host = host.substr(0, slash);

    http::Request req;
    req.method  = http::HttpMethod::Get;
    req.host    = host;
    req.port    = 443;
    req.path    = "/models";
    req.headers = {
        {"authorization", "Bearer " + tok->token},
        {"copilot-integration-id", "vscode-chat"},
        {"editor-version", "vscode/1.104.3"},
        {"accept", "application/json"},
    };
    req.max_body_bytes = 4ull * 1024 * 1024;

    auto resp = http::default_client(io).send(req);
    if (!resp || resp->status < 200 || resp->status >= 300) {
        AGT_LOG(Net, Warn, "copilot.models.fetch_failed",
                "host={} status={} err={} -> bundled catalog", host,
                resp ? resp->status : 0,
                resp ? std::string_view{resp->body}.substr(0, 512)
                     : std::string_view{"transport error"});
        return bundled_models();
    }

    // AUTHORITATIVE权利：该账户的计费层是否包括高级型号系列？
    // 在 Copilot Free (premium_available=false) 上，
    // 仅运行基本 gpt-4o-family 型号 - 每个高级型号 400s。
    // 我们获取一次该帐户无法使用的模型和 HIDE。
    const Entitlement ent = account_entitlement(cap);
    const bool hide_premium = ent.known && !ent.premium_available;

    // AUTO会话：可通过服务器-side路由到达的每-account模型集（
    // free/limited计划运行高级模型的ONLY方式）。
    // 即使 DIRECT 请求 400 秒，这些也会成为第一个 -class 可用条目。
    auto as = auto_session(cap);
    std::set<std::string> auto_ok;
    if (as) for (auto& m : as->available_models)
        if (auto_chat_compatible(m)) auto_ok.insert(m);   // 跳过/responses-only

    // 当模型不在基本 free-tier 行中时，它是 PREMIUM (需要
    // premium_interactions 配额)。基础 = 每个计划中包含的当前 -gen
    // gpt-4o / gpt-4.1 型号 GitHub 文档（包括 Copilot Free）。
    // 这些上的`policy`块只是术语-acceptance，而不是高级门。
    auto is_base_family = [](const std::string& id) {
        return id == "gpt-4o" || id == "gpt-4.1" || id == "gpt-4o-mini"
            || id == "gpt-4o-copilot" || id == "gpt-4.1-mini"
            || id.rfind("gpt-4o-mini", 0) == 0;
    };

    struct Row { ModelInfo info; int rank = 0; };
    std::vector<Row> rows;
    try {
        auto j = nlohmann::json::parse(resp->body);
        const auto& data = j.contains("data") ? j["data"] : j;
        for (const auto& m : data) {
            std::string id = m.value("id", "");
            if (id.empty()) continue;
            const auto& caps = m.value("capabilities", nlohmann::json::object());
            if (caps.value("type", "") != "chat") continue;   // 跳过embeddings/search
            // 跳过 Copilot 的内部路由别名 — 不是 user-facing 型号。
            if (id.find("-picker") != std::string::npos
                || id.find("-secondary") != std::string::npos
                || id.find("-tertiary") != std::string::npos
                || id.rfind("exec-agent", 0) == 0
                || id.rfind("copilot-search", 0) == 0
                || id.rfind("trajectory-", 0) == 0
                || id.rfind("oswe-", 0) == 0) continue;
            // 跳过固定日期 快照 (gpt-4o-2024-11-20、gpt-4-0613、…)：
            // 规范id (gpt-4o，gpt-4)已经出现。
            {
                auto is_date_tail = [&](std::size_t pos) {
                    if (pos == std::string::npos) return false;
                    bool has_digit = false;
                    for (std::size_t k = pos + 1; k < id.size(); ++k) {
                        if (id[k] == '-') continue;
                        if (id[k] < '0' || id[k] > '9') return false;
                        has_digit = true;
                    }
                    return has_digit;
                };
                auto dash = id.find("-20");
                if (dash != std::string::npos && is_date_tail(dash)) continue;
                if (id.size() >= 5) {
                    auto tail = id.rfind('-');
                    if (tail != std::string::npos && id.size() - tail == 5
                        && is_date_tail(tail)) continue;
                }
            }

            const bool has_policy   = m.contains("policy") && m["policy"].is_object();
            const bool base         = is_base_family(id);   // 保单=条款，而非保费
            const bool learned_bad  = is_unsupported_model(cap, id);
            const bool learned_good = is_supported_model(cap, id);
            const bool auto_usable  = auto_ok.count(id) > 0;   // 可通过自动到达

            // 模型是溢价的（抽取溢价配额），除非它是基本系列模型。
            // 已确认-good覆盖（我们实际上已经运行了它）。
            const bool premium = !base && !learned_good && !auto_usable;

            // FILTER:隐藏该帐户无法运行的模型。
            //   •我们已确认的任何内容 400s (learned_bad) — 始终隐藏，
            //     UNLESS可通过自动会话到达。
            //   ·当计划没有保费权利时的保费型号AND
            //     它们不在自动集中。
            if (learned_bad && !auto_usable) continue;
            if (hide_premium && premium) continue;

            ModelInfo info;
            info.id           = ModelId{id};
            info.display_name = m.value("name", id)
                              + std::string{auto_usable && !base ? " (auto)" : ""};
            info.provider     = "copilot";
            // ★我们的账户可以使用CONFIDENT的车型：base family、confirmed-good、
            // Auto-reachable、或者（premium plan）任何premium。
            info.favorite     = base || learned_good || auto_usable
                              || (!hide_premium && !has_policy);
            if (caps.contains("limits"))
                info.context_window =
                    caps["limits"].value("max_context_window_tokens", 200000);
            if (caps.contains("supports")) {
                // 仅EXPLICIT tool_calls:false保留工具。
                // 省略密钥的一行是 UNKNOWN，并非不受支持 - 并且
                // 未知必须发送工具，因为失败在另一个方向上是无声的：转弯运行，
                // 模型只是从未看到工具并回答 "I can't read files"。
                // GitHub 已发送带有 `supports` 对象的行，
                // 该对象仅列出 streaming/vision，
                // 而 `.value("tool_calls", true)`
                // 正在猜测其中的每一个。
                const auto& sup = caps["supports"];
                if (sup.contains("tool_calls") && sup["tool_calls"].is_boolean())
                    info.supports_tools = sup["tool_calls"].get<bool>();
                // else:保留nullopt-未知，工具已被广告。

                // 图像的相同 tri-state。 GitHub 的
                // 行在此对象中携带 `vision`（上面的注释
                // 指出了列出 仅 streaming/vision
                // 的行），因此这是一个真正的声明而不是推论 - 这是
                // 唯一值得采取行动的类型。缺席保持 nullopt：
                // 未知 SENDS，因为我们剥离其图像
                // 的视觉模型与看到它们的视觉模型无法区分并且
                // 没有帮助。
                if (sup.contains("vision") && sup["vision"].is_boolean())
                    info.supports_vision = sup["vision"].get<bool>();
            }

            const std::string cat = m.value("model_picker_category", "");
            int cat_w = cat == "powerful" ? 0 : cat == "versatile" ? 1
                      : cat == "lightweight" ? 2 : 3;
            // 首先确认-good/base，然后是Auto-reachable，然后是其余的。
            int tier = (learned_good || base) ? 0 : auto_usable ? 10 : 100;
            rows.push_back({std::move(info), tier + cat_w});
        }
    } catch (...) { return bundled_models(); }

    // 预先考虑合成"Auto"模型——首选。选择它可以让服务
    // 器将每次路由路由到帐户可能使用的最佳模型（与 "Auto"
    // VS 代码提供的相同）。仅在会话实际可用时显示。
    if (as && as->valid()) {
        ModelInfo autom;
        autom.id           = ModelId{kAutoId};
        autom.display_name = "Auto (best available)";
        autom.provider     = "copilot";
        autom.favorite     = true;
        autom.context_window = 200000;
        autom.supports_tools = true;
        rows.insert(rows.begin(), {std::move(autom), -1});   // 排名-1=非常顶级
    }

    if (rows.empty()) return bundled_models();
    std::stable_sort(rows.begin(), rows.end(),
        [](const Row& a, const Row& b) {
            if (a.rank != b.rank) return a.rank < b.rank;
            return a.info.display_name < b.info.display_name;
        });

    std::vector<ModelInfo> out;
    out.reserve(rows.size());
    for (auto& r : rows) out.push_back(std::move(r.info));

    (void)models_cache().store_if(out, gen);
    return out;
}

std::string default_model(Io io, const maya::unlocked& cap) {
    // 优选适用于每个副驾驶层的基本-allowlist模型，
    // 因此新的标志-in永远不会落在第一回合就400秒的模型上。
    for (const char* id : {"gpt-4o", "gpt-4.1", "gpt-4o-mini"})
        if (!is_unsupported_model(cap, id)) return id;
    auto ms = list_models(io, cap);
    return ms.empty() ? std::string{"gpt-4o"} : ms.front().id.value;
}

// ── 隔膜选择(公开;测量的表格见provider.hpp) ──
bool prefers_responses_dialect(const std::string& model) {
    // 副驾驶不再拥有该答案。它是 首次 混合 -dialect 主机，
    // 所以表住在这里 - 但 "which dialect does this model speak"
    // 一般是关于 OpenAI-family 型号的问题，
    // 保留私有副本意味着 注册表 行表示 OpenAIChat，
    // 而此函数悄悄地将 gpt-5* 路由到其他地方。这与错误标记的
    // `openai` 行所带来的漂移相同，只是隐藏在函数而不是字段中。
    //
    // 共享谓词知道模型系列；真正剩下的 Copilot
    // -specific 是其聊天端点拒绝的家庭，OR-ed 位于顶部。
    if (chat_dialect_unsupported(model)) return true;
    return dialect_for("copilot", model) == Dialect::Responses;
}

bool chat_dialect_unsupported(const std::string& model) {
    // 家人知晓：mai-code-*从未接受/chat/completions.
    return !auto_chat_compatible(model);
}

std::string pick_auto_model_for_test(
        const std::vector<std::string>& available_models,
        const std::string& selected_model,
        const std::string& requested) {
    AutoSession as;
    as.available_models = available_models;
    as.selected_model   = selected_model;
    return pick_auto_model(as, requested);
}

void invalidate_model_cache() {
    models_cache().clear();
}

} // namespace agentty::provider::copilot
