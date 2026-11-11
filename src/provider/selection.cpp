// agentty::provider-活动-provider选择(过程-global)。

#include "agentty/provider/selection.hpp"

#include "agentty/util/logx.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "agentty/provider/registry.hpp"
#include "agentty/provider/auth_state.hpp"   // auth_source
#include "agentty/domain/bundled_catalog.hpp"
#include "agentty/provider/acp_agents.hpp"
#include "agentty/provider/anthropic/transport.hpp"
#include "agentty/provider/chatgpt/provider.hpp"
#include "agentty/provider/copilot/provider.hpp"
#include "agentty/provider/kimi/provider.hpp"
#include "agentty/provider/openai/transport.hpp"
#include "agentty/runtime/fuzzy.hpp"
#include "agentty/io/http.hpp"

#include <maya/runtime.hpp>

namespace agentty::provider {

namespace {
// 由UI 线程 (浏览量、状态更新函数)和工作线程读取，
// 由select()批量替换：已发布的不可变值，因此active()不需要能力。
maya::published<const Selection>& g_active() {
    static maya::published<const Selection> s;
    return s;
}

// --auth-header覆盖，""=承载。启动时写一次。
maya::snapshot<std::string>& g_auth_header() { static maya::snapshot<std::string> h; return h; }

// Persist-on-success:CLI custom-host规格等待证明(空
// 规格=无)。在启动时写入一次，由 ModelsLoaded 状态更新函数
// 在 UI 线程 上消耗。
struct Unproven { std::string spec; std::string model; };
// take()自动地交换它，因此恰好有一个消费者得到它。
maya::published<const Unproven>& g_unproven() {
    static maya::published<const Unproven> u;
    return u;
}

std::string env_or_empty(std::string_view name) {
    if (name.empty()) return {};
    const char* v = std::getenv(std::string{name}.c_str());
    return (v && *v) ? std::string{v} : std::string{};
}

// 当实时/模型获取返回为空时（还没有设置 API 密钥、网络关闭、
// 或者在身份验证之后门控其列表的提供者），
// 小 per-provider 种子 目录 使用 仅。
// 让新选择的托管提供商立即在 选择面板 中展示合理的模型；当帐户可访问时，
// 真正的 目录 就会取代它。键入 注册表 `id`（== 端点标签）。
// 故意简短——每个提供者有几个当前的agent-capable ID，
// 最新的在前（当用户没有给出-m时，front()成为默认值）。
std::vector<ModelInfo> bundled_models_for(std::string_view label) {
    // 代表到单个捆绑的 目录 (catalog::bundled)，
    // 因此每个提供商的离线楼层都位于 一个 位置 — 没有 per-site 漂移。
    return catalog::bundled(label);
}
} // 命名空间结束。

void set_unproven_spec(Io, std::string spec, std::string model_recall) {
    g_unproven().publish(std::make_shared<const Unproven>(
        Unproven{std::move(spec), std::move(model_recall)}));
}

std::optional<std::pair<std::string, std::string>> take_unproven_spec_at_launch(Io) {
    const std::shared_ptr<const Unproven> u = g_unproven().take();
    if (!u || u->spec.empty()) return std::nullopt;
    return std::make_pair(u->spec, u->model);
}

void set_custom_auth_header(Io, std::string name) {
    g_auth_header().store(std::move(name));
}

std::string custom_auth_header() {
    return g_auth_header().get();
}

Selection parse_selection(std::string_view spec) {
    Selection s;
    // 注册表-driven：空规范或类型为 Anthropic 的任何预设路由到
    // Anthropic 传输层。其他所有内容（OpenAI-family
    // 预设 和 原始 "host[:port]" 自定义端点）都会通过
    // OpenAI-compatible 传输层 以及匹配的端点。
    const ProviderPreset* p = spec.empty() ? preset_for(default_provider_id())
                                            : preset_for(spec);
    // 绑定行ONCE，在这里，在该函数的每条路径上。然后下游谓词从行
    // 中读取标识而不是重新读取-deriving它来自
    // `openai_endpoint.label`— 自定义的
    // 显示字符串-host流程覆盖，
    // 这就是副驾驶的方式-backed自定义提供程序过去常常失去其身份并落入通用/v1/models拿来。
    s.row = p;
    if (p && p->kind() == Kind::Anthropic) {
        s.kind = Kind::Anthropic;
        return s;
    }
    // 外部ACP代理：此处仅显式配置的可启动id路由。
    // 没有特权内置-ins。
    if ((p && p->kind() == Kind::ExternalAcp)
        || (!p && !spec.empty() && is_acp_agent_id(spec))) {
        s.kind         = Kind::ExternalAcp;
        s.acp_agent_id = std::string{spec};
        return s;
    }
    // removed/stale
    // ACP选择不得落入Endpoint::from_spec并被解释为OpenAI主机名。
    // 回退到本机默认设置，直到用户再次显式配置该代理。
    if (!p && spec.ends_with("-acp")) {
        s.kind = Kind::Anthropic;
        return s;
    }
    s.kind = Kind::OpenAI;
    s.openai_endpoint = openai::Endpoint::from_spec(
        spec.empty() ? default_provider_id() : spec);

    // CUSTOM HOST恰好命名了我们知道的提供者就是该提供者。
    //
    // "Custom host" 规范（原始 `host[:port]` 或完整的 URL）
    // 没有 注册表 行 — preset_for() 仅匹配规范 ID。因此，
    // 将 `api.githubcopilot.com` 添加为自定义提供程序的用户会
    // 获得 GENERIC OpenAI 默认值：/v1/models 而不是
    // Copilot 的 /models，并且没有任何 OAuth 会话处理。
    // 选择面板 根本没有显示任何模型，也没有错误，因为空的 目录 和失败的获取看起来是相同的。
    //
    // 在 HOST 上匹配，这是自定义规范的一部分，
    // 其不是用户可以重命名的显示字符串。仅采用我们实际特殊的
    // 主机-case（oauth_native后端）的行；
    // 通用托管提供商不会从采用中获得任何好处，并且使用户的端点与 带
    // 类型的 完全相同。
    if (!p && !s.openai_endpoint.host.empty()) {
        for (const auto& row : kProviders) {
            if (!row.oauth_native) continue;
            if (row.host != s.openai_endpoint.host) continue;
            // 采用行的身份AND其端点列——路径是
            // 使其成为提供者的一部分。
            s.row = &row;
            s.openai_endpoint = openai::Endpoint::from_spec(row.id);
            break;
        }
    }

    // 将会话的 --auth-header 覆盖标记到每个 OpenAI
    // -family 端点上，在这里而不是在 调用 站点上，以便实时提供者交换机
    // （选择面板/登录 状态更新函数）在不知道它存在的情况下保留它。
    s.openai_endpoint.auth_header_name = custom_auth_header();
    return s;
}

auth::AuthHeader resolve_auth_for(std::string_view spec,
                                  const auth::AuthHeader& anthropic_creds,
                                  std::string_view cli_key,
                                  std::string_view saved_key) {
    const ProviderPreset* p = spec.empty() ? preset_for(default_provider_id())
                                            : preset_for(spec);

    // Anthropic（或解析为 Anthropic 的未知规范）：使用从
    // `agentty login` 解析的信用。
    if ((p && p->kind() == Kind::Anthropic)
        || (!p && parse_selection(spec).kind == Kind::Anthropic)) {
        return anthropic_creds;
    }

    // 本地后端不需要密钥。
    if (p && p->auth == AuthStyle::None)
        return auth::AuthHeader{auth::ApiKeyHeader{std::string{}}};

    // OpenAI-family（预设或自定义主机）：承载密钥，
    //   优先级--key>已保存密钥（在-app粘贴）>env-var链>OPENAI_API_KEY。
    std::string key{cli_key};
    if (key.empty()) key = std::string{saved_key};
    if (key.empty() && p) {
        for (auto env : p->auth_env) {
            key = env_or_empty(env);
            if (!key.empty()) break;
        }
    }
    if (key.empty())
        key = env_or_empty("OPENAI_API_KEY");
    return auth::AuthHeader{auth::ApiKeyHeader{std::move(key)}};
}

void select(Io, Selection s) {
    // 将提供者ID发布为能力-key范围BEFORE交换选择，
    // 因此resolved_caps()在最坏的情况下切换切换
    // 看到OLD范围与OLD选择——绝不会是不匹配的对。
    //
    // 值 必须 与 源码树 调用 的其余部分匹配提供者 ID（modal.cpp
    // 的 active_provider_id()，
    // 每个 catalog/recents/fused 行由其键入）：OpenAI
    // -family → 端点标签，外部 ACP → 代理 ID，其他所有内容
    // → 默认 ID。将 ACP 折叠到默认 ID（就像这样）会在
    // "anthropic/..." 下归档其能力事实，而每次查找都要求
    // "<agent-id>/..."，因此 ACP 代理的学习努力阶梯永远不会再找到。
    set_caps_provider_scope(
        s.kind == Kind::OpenAI      ? s.openai_endpoint.label
      : s.kind == Kind::ExternalAcp ? s.acp_agent_id
                                    : std::string{default_provider_id()});
    // 记录后续每个回合将使用的WHICH后端。 select() 是启动路径和
    // 每个后续提供商切换都经过的 接口边界，因此这里的一行意味着日志始终可以
    // 回答 "which provider/endpoint was active when this broke"
    // — 这是在 custom-host 和 Copilot
    // 报告上花费最多时间的问题，因为答案是不可见的。
    // 端点列对于 OpenAI-family 传输 来说是有意义的 仅 —
    // Anthropic 和 ACP 将 openai_endpoint 保留在其结构默认值，
    // 并且打印这些（主机 = api.openai.com 对于 Anthropic
    // 轮次）会主动误导读取日志的人。
    const bool http_wire = s.kind == Kind::OpenAI;
    // 警告与启动横幅相同的保留原因：
    // 此前处于活动状态的提供方或端点。
    // 是每个报告上的第二个问题，并且每个会话几行是始终拥有它的公平价格。
    AGT_LOG(Wire, Warn, "provider.select",
            "provider={} kind={} endpoint={} agent={}",
            s.provider_id().empty()
                ? (s.openai_endpoint.label.empty() ? std::string{"custom"}
                                                   : s.openai_endpoint.label)
                : std::string{s.provider_id()},
            s.kind == Kind::Anthropic      ? "anthropic"
          : s.kind == Kind::ExternalAcp    ? "acp"
                                           : "openai",
            http_wire ? s.openai_endpoint.host + s.openai_endpoint.path
                      : std::string{"-"},
            s.kind == Kind::ExternalAcp ? s.acp_agent_id : std::string{"-"});

    g_active().publish(std::make_shared<const Selection>(std::move(s)));
}

Selection active() {
    // 快照副本；请参阅 头文件 了解它关闭的 竞态。
    const std::shared_ptr<const Selection> a = g_active().current();
    return a ? *a : Selection{};
}

std::string Selection::catalog_id() const {
    if (kind == Kind::OpenAI)      return openai_endpoint.label;
    if (kind == Kind::ExternalAcp) return acp_agent_id;
    return std::string{default_provider_id()};
}

std::string provider_display_name(const Selection& s) {
    if (s.kind == Kind::Anthropic) return "Anthropic";
    if (s.kind == Kind::ExternalAcp) {
        // ACP 代理没有硬编码 注册表 行（Zed-style：config
        // -driven），因此显示名称只是用户选择的代理 ID。
        return s.acp_agent_id.empty() ? std::string{"ACP agent"} : s.acp_agent_id;
    }
    // OpenAI-family：将端点标签（"groq"、"ollama"、
    // ……）映射到其注册表显示名称；回退到自定义主机的原始标签。
    const std::string& lbl = s.openai_endpoint.label;
    if (const auto* p = preset_for(lbl)) return std::string{p->label};
    if (lbl.empty()) return std::string{"OpenAI"};
    // URL-form 自定义主机（https://host/path,
    // http://host:port/path) 带有完整规范作为其标签（参见
    // Endpoint::from_spec）。徽章和交换机 toast 应显示主机[:端口]，而不是长 URL。
    // ep.label 本身未受影响 - 预设查找和 openai_transport_test
    // 标签 round-trip 断言仍然可以看到完整规范。
    if (lbl.starts_with("https://") || lbl.starts_with("http://")) {
        const bool tls = lbl.starts_with("https://");
        std::string_view auth = lbl;
        auth.remove_prefix(tls ? 8 : 7);   // "https://"=8，"http://"=7
        // 剥离路径首先为'/'(权限后的所有内容)。
        if (auto slash = auth.find('/'); slash != std::string_view::npos)
            auth = auth.substr(0, slash);
        std::string out;
        std::uint16_t port = tls ? 443 : 80;
        // IPv6文字被括起来("[::1]"/"[::1]:8080")
        // ——端口冒号是"]"之后的冒号，并且括号被保留在标签中，
        // 因此它围绕-trips作为有效权限。
        if (!auth.empty() && auth.front() == '[') {
            auto close = auth.find(']');
            if (close != std::string_view::npos) {
                out = std::string{auth.substr(0, close + 1)};   // 保持[...]
                auto rest = auth.substr(close + 1);
                if (rest.size() > 1 && rest.front() == ':') {
                    try {
                        int p = std::stoi(std::string{rest.substr(1)});
                        if (p > 0 && p <= 65535) port = static_cast<std::uint16_t>(p);
                    } catch (...) {}
                }
            } else {
                out = std::string{auth};
            }
        } else if (auto colon = auth.rfind(':'); colon != std::string_view::npos) {
            out = std::string{auth.substr(0, colon)};
            try {
                int p = std::stoi(std::string{auth.substr(colon + 1)});
                if (p > 0 && p <= 65535) port = static_cast<std::uint16_t>(p);
            } catch (...) {}
        } else {
            out = std::string{auth};
        }
        // 当端口是方案默认值时，省略该端口。
        if (port != (tls ? 443 : 80)) out += ":" + std::to_string(port);
        return out;
    }
    return lbl;
}

PrewarmTarget prewarm_target(const Selection& s) {
    // ACP子进程：没有HTTP层要预热。
    if (s.kind == Kind::ExternalAcp) return {};

    // 注册表-driven固定主机。不拨打端点的两个后端在其预设行上带有
    // `prewarm_host`：Anthropic（传输层 硬编码
    // api.anthropic.com）和 ChatGPT（与
    // chatgpt.com 通信，而其端点端口是 0 标记）。我们按选择解析的 id
    // 查找该行 — "anthropic" 代表 Anthropic 类型，
    // 否则为 OpenAI 端点标签（"chatgpt"、"groq"，...）。
    const std::string_view id =
        s.kind == Kind::Anthropic ? std::string_view{"anthropic"}
                                  : std::string_view{s.openai_endpoint.label};
    if (const ProviderPreset* p = preset_for(id); p && !p->prewarm_host.empty()) {
        PrewarmTarget t;
        t.host = std::string{p->prewarm_host};
        t.port = 443;
        // Anthropic 尊重 AGENTTY_API_HOST 拨号盘，
        // 因此热套接字以真正的 上游 为目标（排除在 注册表 之外：它是
        // per-run 环境覆盖，而不是静态提供者事实）。
        if (s.kind == Kind::Anthropic) {
            const auto& ov = http::agentty_api_host_override();
            if (ov.active()) {
                t.override_host = ov.host;
                t.override_port = ov.port;
            }
        }
        return t;
    }

    // 每个其他OpenAI-compat后端：预热其自己的端点主机。
    // 跳过本地 - 没有 TLS 握手需要摊销，
    // 并且端口可能是 0（哨兵）或尚未启动的本地开发服务器。
    const auto& ep = s.openai_endpoint;
    if (!ep.use_tls || ep.host.empty() || ep.host == "localhost"
        || ep.host == "127.0.0.1" || ep.port == 0)
        return {};
    PrewarmTarget t;
    t.host = ep.host;
    t.port = ep.port;
    return t;
}

void prewarm_active_provider(Io io) {
    // 跨本机后端统一：从活动选择中解析热目标（纯的，注册表-driven
    // —参见 prewarm_target）并打开套接字。
    // 没有任何提供商享有特权；路由表存在于一个可测试函数中，
    // 而不是这个 side-effecting 包装器中。
    const PrewarmTarget t = prewarm_target(active());
    if (!t.should_warm()) return;
    // 现在便宜：客户端不再预先构建其TLS上下文；
    // 表盘（池 作业）将其构建为启动路径。
    http::default_client(io).prewarm(t.host, t.port, t.override_host,
                                   t.override_port);
}

namespace {
// 一个正文用于两个过载。 `error` 收到 OpenAI
// -compat 获取失败的原因；其他后端要么抛出异常，要么没有值得
// 命名的故障模式，因此对它们来说它保持为空。
std::vector<ModelInfo> list_models_impl(Io io, const maya::unlocked& cap, const Selection& sel,
                                        const auth::AuthHeader& auth, std::string& error) {
    error.clear();
    // 分派在CARRIED提供商ID上，而不在re-derived标签上。
    // 三个 OAuth-native 后端获取自己的 目录，因为它们的端点
    // 公开了通用 解析器 丢弃的元数据（Copilot 的 policy/tier、
    // ChatGPT 的帐户行-up、Kimi 的编码模型）。
    //
    // 这个用来比较`openai_endpoint.label`，
    // 其中定制-host流程覆盖——所以副驾驶-backed自定义主机不匹配任何内容，
    // 落入通用/v1/models GET（副驾驶服务/模型），
    // 并且选择器空空如也，没有错误。
    if (sel.kind == Kind::ExternalAcp) return {};   // 代理自行挑选
    if (sel.is_copilot()) return copilot::list_models(io, cap);
    if (sel.is_kimi())    return kimi::list_models(io, cap);
    if (sel.is_chatgpt()) return chatgpt::list_models(io, cap);
    if (sel.kind == Kind::Anthropic) return anthropic::list_models(io, auth);

    auto models = openai::list_models(io, auth, sel.openai_endpoint, false, error);
    // 托管提供者在设置密钥之前（或者当获取失败时）不返回任何内容。
    // 来自捆绑的 目录 的种子，因此 选择面板 永远不会空着；
    // 自定义主机没有种子并且合法地保持为空。
    if (models.empty()) return bundled_models_for(sel.openai_endpoint.label);
    return models;
}
}  // 命名空间结束。

std::vector<ModelInfo> list_models_for(Io io, const maya::unlocked& cap, const Selection& sel,
                                       const auth::AuthHeader& auth) {
    std::string ignored;
    return list_models_impl(io, cap, sel, auth, ignored);
}

std::vector<ModelInfo> list_models_for(Io io, const maya::unlocked& cap,
    const Selection& sel, const auth::AuthHeader& auth, std::string& error) {
    return list_models_impl(io, cap, sel, auth, error);
}

std::vector<int> filter_provider_indices(std::string_view query) {
    const auto ps = providers();
    const int n = static_cast<int>(ps.size());
    std::vector<int> out;
    out.reserve(static_cast<std::size_t>(n));
    if (query.empty()) {
        for (int i = 0; i < n; ++i) out.push_back(i);
        return out;
    }
    // Fuzzy-score针对id、标签和简介；保留三者中最好的，
    // 以便 "kimi" 与 id 匹配，"grok" 与标签匹配，"wafer" 与
    // Cerebras 简介匹配。按分数排名（稳定，因此平局保持 注册表 顺序）。
    struct Scored { int idx; int score; };
    std::vector<Scored> scored;
    scored.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const auto& p = ps[static_cast<std::size_t>(i)];
        int best = -1;
        for (std::string_view field : {p.id, p.label, p.blurb}) {
            auto m = fuzzy::score(field, query);
            if (m.matched() && m.score > best) best = m.score;
        }
        if (best >= 0) scored.push_back({i, best});
    }
    std::stable_sort(scored.begin(), scored.end(),
        [](const Scored& a, const Scored& b) { return a.score > b.score; });
    for (const auto& s : scored) out.push_back(s.idx);
    return out;
}

FilteredProviders filter_provider_indices(std::string_view query,
                                          const store::Settings& settings,
                                          const auth::AuthView& view) {
    const auto ps = providers();
    FilteredProviders out;

    // 带类型的查询搜索WHOLE 注册表：与查询-only重载相同的排名，
    // 未改变。配置的行仍然领先，因此当您搜索 "o" 时，
    // 您自己的后端会排在您必须登录的后端之上，
    // 但每个供应商仍然可以通过名称进行访问。
    if (!query.empty()) {
        auto ranked = filter_provider_indices(query);
        std::vector<int> mine, rest;
        for (int i : ranked) {
            const auto& p = ps[static_cast<std::size_t>(i)];
            (auth_source(p, settings, view) != AuthSource::None ? mine : rest)
                .push_back(i);
        }
        out.n_configured = static_cast<int>(mine.size());
        out.indices = std::move(mine);
        out.indices.insert(out.indices.end(), rest.begin(), rest.end());
        return out;
    }

    // 空查询=ADMIN视图：仅您已配置的内容。 注册表 的其余部分并没有消失，
    // 只需按一下按键即可 - 呼叫者 渲染 n_hidden 作为
    // "N more providers, type to search" 行。
    for (int i = 0; i < static_cast<int>(ps.size()); ++i) {
        const auto& p = ps[static_cast<std::size_t>(i)];
        if (auth_source(p, settings, view) != AuthSource::None)
            out.indices.push_back(i);
        else
            ++out.n_hidden;
    }
    out.n_configured = static_cast<int>(out.indices.size());
    return out;
}

} // namespace agentty::provider
