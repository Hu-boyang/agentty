// 在-app登录弹出面板 状态更新函数兵种。位于 update.cpp 之外，因为 OAuth
// 流程拖入了 `auth/auth.hpp` + `cmd_factory.hpp`
// 的依赖项，而 update.cpp 的其余部分不需要这些依赖项。
//
// 弹出面板是封闭总和(`ui::login::State`)：已封闭|拣选
// | OAuthCode | OAuth交换| ApiKeyInput |失败。
// 这里的每个手臂要么通过 `std::visit` 调度到活动替代方案，
// 要么在 弹出面板 不处于接受 Msg 的状态时发送短-circuits
// — 带类型的 状态机保证我们永远不会从 ApiKeyInput 弹出
// 面板 等读取 OAuthCode 字段。

#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/app/update.hpp"   // app::update (LoginBack 或 -entry)

#include <chrono>
#include <utility>
#include <variant>

#include <maya/core/overload.hpp>

#include "agentty/auth/auth.hpp"
#include "agentty/auth/accounts.hpp"
#include "agentty/io/clipboard.hpp"
#include "agentty/provider/chatgpt/codex_oauth.hpp"
#include "agentty/provider/registry.hpp"
#include "agentty/provider/selection.hpp"
#include "agentty/provider/credentials.hpp"
#include "agentty/provider/auth_state.hpp"
#include "agentty/provider/openai/transport.hpp"
#include "agentty/auth/vault.hpp"
#include "agentty/runtime/app/cmd_factory.hpp"
#include "agentty/util/logx.hpp"
#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/view/helpers.hpp"
#include "agentty/tool/subagent.hpp"

namespace agentty::app::detail {

using maya::overload;
namespace login = agentty::ui::login;

namespace {

// 坚持+live-install 凭据，然后关闭弹出面板。单点，
// 因此 OAuth 和 ApiKey 路径不会漂移 - 两者都在此结束。 保存、
// 帐户 快照 和 头文件 安装是主机 副作用（SaveCredentials，
// 然后是 InstallAuth），在 归并 之后按该顺序运行。
[[nodiscard]] Cmd install_and_close(Model& m, auth::Credentials creds,
                                    bool as_new_account = false) {
    m.ui.login = login::Closed{};
    m.s.status = "logged in";
    m.s.status_until = m.now
                     + std::chrono::seconds{4};
    return Cmd::batch(
        Cmd(SaveCredentials{std::move(creds), as_new_account}),
        Cmd(InstallAuth{.provider = "anthropic"}));
}

} // 命名空间结束。

// 使用给定的显示`label`启动`provider` (注册表
// id)的本机设备-flow OAuth登录。因此，选择面板、
// 登录菜单和帐户管理器都以相同的方式启动设备登录。
//
// 返回 NO Cmd：工作线程 现在是 SUBSCRIPTION，以这次尝试为关键。
// subscribe()看到DeviceWaiting状态并要求jaal
// 运行轮询循环；当状态消失时（Esc，成功，更新的尝试），密钥随之消失，
// jaal 触发 正文 的 stop_token。这就是为什么
// "close the modal" 和 "stop the worker" 是同一行为，
// 而不是两件必须保持步调一致的事情——它们不是，而且 Esc 泄露了 线程
// 的 900 年代预算剩余部分的民意调查。
Cmd launch_device_login(Model& m, std::string provider, std::string label) {
    m.ui.login = login::DeviceWaiting{
        .provider = std::move(provider),
        .provider_label = std::move(label),
        .attempt_id = cmd::next_codex_login_attempt_id(),
    };
    return Cmd::none();
}

Cmd open_login(Model& m) {
    m.ui.login = login::Picking{};
    return Cmd::none();
}

Cmd close_login(Model& m) {
    // 离开等待状态IS，取消:subscribe()停止返回登录
    // 数据流的密钥，因此jaal激发工作线程的stop_token。
    // 没有 标志 可以绊倒——两者过去是分开的，而 标志 是
    // 停止被读取的一半。
    m.ui.login = login::Closed{};
    return Cmd::none();
}

// 来自登录 sub-modal 的 Esc：使用 sub-state 的记录来源
// — 第一个 -class 父 帧 弹出 一个 级别，因此每个弹出用其 完整 上下文
// （哪个提供者的帐户，哪个 带类型的 主机）重建父。一访，无方-channels。
// 没有父状态 (Nowhere) 或 async-waiting 状态 (Esc =
// 取消) 的状态会落入 close_login。
Cmd login_back(Model& m) {
    // 重建记录的父代。 `pop` OWNS 是原点，因此它可以将
    // provider/spec 移出原点进入重建状态。
    auto pop = [&](login::Origin origin) -> Cmd {
        return std::visit([&](auto&& o) -> Cmd {
            using T = std::decay_t<decltype(o)>;
            namespace og = login::origin;
            if constexpr (std::is_same_v<T, og::Providers>) {
                m.ui.login = login::Closed{};
                return providers_update(m, msg::ProvidersMsg{OpenProviders{}});
            } else if constexpr (std::is_same_v<T, og::Models>) {
                m.ui.login = login::Closed{};
                return models_update(m, msg::ModelsMsg{OpenModels{}});
            } else if constexpr (std::is_same_v<T, og::Accounts>) {
                // 返回到SPECIFIC提供者的账户列表——帧
                // 携带提供者，所以这不能漂移到活动的帐户列表。
                m.ui.login = login::Closed{};
                return login_update(
                    m, msg::LoginMsg{OpenAccounts{std::move(o.provider)}});
            } else if constexpr (std::is_same_v<T, og::Method>) {
                login::Picking p;
                p.provider = std::move(o.provider);
                // 提供者-scoped方法菜单仅从该提供者的帐户列表中进入，
                // 因此将其恢复为ITS父级——下一个
                // Esc继续走相同的堆栈而不是关闭。
                if (!p.provider.empty())
                    p.origin = login::origin::Accounts{p.provider};
                m.ui.login = std::move(p);
                return Cmd::none();
            } else if constexpr (std::is_same_v<T, og::HostInput>) {
                // 恢复 带类型的 规范，以便退出 key/probe
                // 提示词 不会丢弃用户刚刚输入的主机。
                login::CustomHostInput ch;
                ch.host_input = std::move(o.spec);
                ch.cursor     = static_cast<int>(ch.host_input.size());
                ch.origin     = login::origin::Providers{};
                m.ui.login    = std::move(ch);
                return Cmd::none();
            } else {   // og::Nowhere
                return close_login(m);
            }
        }, std::move(origin));
    };

    if (auto* ch = std::get_if<login::CustomHostInput>(&m.ui.login))
        return pop(std::move(ch->origin));
    if (auto* hp = std::get_if<login::HostProbing>(&m.ui.login)) {
        // 探测期间的 Esc：放弃它（尝试 ID 使任何迟到的
        // HostProbed 成为 no-op）并将 带类型的 规范放回到输入中，
        // 为 下一个 Esc 保留探测器自己的父级 帧。
        auto spec   = std::move(hp->spec);
        auto origin = std::move(hp->origin);
        login::CustomHostInput ch;
        ch.host_input = std::move(spec);
        ch.cursor     = static_cast<int>(ch.host_input.size());
        ch.origin     = std::move(origin);
        m.ui.login    = std::move(ch);
        return Cmd::none();
    }
    if (auto* api = std::get_if<login::ApiKeyInput>(&m.ui.login))
        return pop(std::move(api->origin));
    if (auto* p = std::get_if<login::Picking>(&m.ui.login))
        return pop(std::move(p->origin));
    // 帐户列表总是到达FROM提供者选择面板(提供者行上的Enter)，
    // 因此Esc在那里步骤BACK——保持帐户→提供者
    // →聊天分层。
    if (std::holds_alternative<login::AccountList>(m.ui.login))
        return pop(login::origin::Providers{});
    // 每个其他 sub-state (OAuth 等待、失败):Esc 保持其原始含义
    // — cancel/close 完全。
    return close_login(m);
}

// 无密钥定制主机的异步探测结果。成功：保留主机（空密钥 -
// 选择面板 的 已保存-row 存储）并使用 DETECTED
// 方言提交交换机。失败：返回到输入，恢复规格并在状态行中
// 显示原因。 过期 结果（Esc 已/重新提交的探测）因
// 尝试-id 不匹配而被丢弃。
Cmd host_probed(Model& m, HostProbed r) {
    auto* hp = std::get_if<login::HostProbing>(&m.ui.login);
    if (!hp || hp->attempt_id != r.attempt_id) return Cmd::none();
    auto origin = std::move(hp->origin);
    if (!r.ok) {
        // 401/403表示ADDRESS WAS RIGHT并且服务器想要密钥。
        // 让用户返回正确地编辑URL他们带类型的是错误的举动，
        // 并且这是最常见的付费-provider加入路径：
        // 输入主机，得到401。直接转到已绑定
        // 主机的密钥提示词。
        if (r.needs_key) {
            login::ApiKeyInput api;
            api.provider       = r.spec;
            api.provider_label = r.spec;    // 自定义主机IS自己的标签
            api.key_required   = true;
            api.origin         = std::move(origin);
            m.ui.login         = std::move(api);
            auto toast = set_status_toast(
                m, r.spec + " needs an API key \xe2\x80\x94 paste it below",
                std::chrono::seconds{6});
            return toast;
        }
        login::CustomHostInput ch;
        ch.host_input = std::move(r.spec);
        ch.cursor     = static_cast<int>(ch.host_input.size());
        ch.origin     = std::move(origin);
        m.ui.login    = std::move(ch);
        auto toast = set_status_toast(m, "host check failed: " + r.error,
                                      std::chrono::seconds{6});
        return toast;
    }

    const std::string spec = std::move(r.spec);
    // 决定 之前 持久化：no_provider_configured 读取
    // provider_keys，下面的插入将使其看到该主机并且永远不会采用。
    const bool adopt = no_provider_configured(m);
    // PERSIST 无密钥主机（空密钥），因此它在下一个会话中具有 选择面板 行 —
    // saved_custom_hosts() 从 provider_keys 派生行。
    Cmd save = Cmd::none();
    {
        if (m.d.persisted.provider_keys.find(spec)
                == m.d.persisted.provider_keys.end()) {
            m.d.persisted.provider_keys[spec] = "";
            save = save_record(m);
        }
    }
    m.ui.login = login::Closed{};
    // SETTING UP A HOST IS 不 SWITCHING
    // TO IT。这过去以 commit_provider_switch 结束，
    // 因此添加一个您打算稍后使用的 llama.cpp 框将活动后端移离
    // 您正在处理的 和 更改的模型（交换机必须为新提供程序选择一个）。
    // 第一次运行是例外：没有任何配置就没有
    // 要保护的会话，并且在用户添加主机之后
    // 立即不进行配置会更糟。
    Cmd switch_cmd = Cmd::none();
    if (adopt)
        switch_cmd = commit_provider_switch(m, spec,
                                       provider::provider_display_name(
                                           provider::parse_selection(spec)));
    else
        // commit_provider_switch会刷新这一点。
        // 选择面板 读取 AuthView 来决定哪些行是 "yours"，
        // 因此如果没有它，您刚刚添加的主机将位于 不 CONFIGURED 下，直到重新启动。
        switch_cmd = Cmd(LoadAuthView{});
    // 用探针 FOUND —"it just works" 时刻来丰富
    // switch toast：方言、模型计数、延迟。
    //
    // 以及CONTEXT WINDOW，这实际上决定
    // 长会话是否在该主机上工作。三种诚实的状态，
    // 而不是一种普遍的成功：
    //
    //   · 32k ctx 我们询问服务器，它告诉我们。相信它。
    //   ·ctx:auto我们问过，它没有说。型号 选择面板
    //                      中的 ^W 覆盖是您的设置方式。
    //   ·ctx没有检查我们确实问了NOT，因为主机没有看self
    //                      -hosted。这是唯一一个带有 REMEDY 的州，
    //                      并以此命名。
    //
    // 最后一行的存在是因为一个真实的报告：有人在他们的
    // GPU 盒子上运行 llama-server 通过我们
    // 分类为远程的名称到达它，所以 运行时 探测被保留，
    // 他们的窗口默默地错误。告诉他们 "not checked"
    // 加上一个修复它的变量胜过隐藏差距的成功祝酒。
    std::string ctx;
    if (r.window_tokens > 0) {
        // SAY WHERE 该 NUMBER CAME FROM。
        // 窗口要么是端点 TOLD 我们的东西，要么是我们默认的东西，
        // 这两者值得读者给予不同的信心（这是 Observed
        // <T> 与 lens.hpp 的区别，在 UI 中出现）。
        //
        // GitHub Copilot的运行时以同样的方式进行建模
        // ——
        // 其模型记录携带max_context_window_tokens/max_prompt_tokens/max_output_tokens作为单独的广告限制，
        // 与会话最终使用的任何内容不同。单个未标记的
        // 数字会导致错误窗口无法调试：您无法区分服务
        // 器承诺的 131k 和我们假设的 131k。
        ctx = " \xc2\xb7 " + ui::context_window_label(r.window_tokens)
            + " ctx (advertised)";
    } else if (r.probe_skipped) {
        // 这里故意没有按键绑定。这个 选择面板 已经拒绝隐藏隐藏和弦后面的
        // per-model 设置（参见 on_model_picker：
        // 隐藏控件与可见控件互相矛盾，会使行为更难理解。
        // 而教和弦的祝酒词与较短的生命周期
        // 也有同样的错误。
        //
        // 改为命名该地点。提供者 选择面板 是该
        // 主机的行所在的位置，因此想要更改主机某些
        // 内容的人已经在该位置。
        ctx = " \xc2\xb7 ctx not checked \xe2\x80\x94 set AGENTTY_PROBE_HOSTS="
            + provider::parse_selection(spec).openai_endpoint.host;
    } else {
        // 没有任何广告，也没有任何要探测的内容：目录默认携带此。
        // 这么说，而不是打印一个裸露的数字，
        // 用户会合理地读取测量值。
        ctx = " \xc2\xb7 ctx: default";
    }

    // 位于公共-looking地址的主机，ANSWERED、运行时路由是self
    // -hosted——/props、/api/ps和loaded_instances不是托管API服务的东西。
    // 保留它，以便以后的每次模型刷新也会探测
    // 它。
    //
    // 这就是设置 切换 的用途，以及为什么没有
    // 设置：服务器已经告诉我们它是什么。
    // 要求用户确认就等于要求他们重复我们的答案。
    Cmd probe_save = Cmd::none();
    if (!r.learned_self_hosted.empty()) {
        if (m.d.persisted.probe_hosts.insert(r.learned_self_hosted).second) {
            probe_save = save_record(m);
            provider::openai::install_probe_hosts(m.d.persisted.probe_hosts);
        }
    }
    // 探测什么FOUND，无论哪种方式：方言、模型计数、延迟、ctx。
    // 当我们进行 不 切换时，同一行还必须说明主机只是准备好并
    // 命名开始使用它的方式——否则
    // "added a host, nothing happened" 与静默故障无法区分。
    std::string detail =
        std::string{"\xe2\x9c\x93 "} + std::to_string(r.model_count)
        + (r.model_count == 1 ? " model" : " models") + " \xc2\xb7 "
        + (r.native_api ? "ollama native" : "openai-compatible") + " \xc2\xb7 "
        + std::to_string(r.latency_ms) + "ms" + ctx;
    auto found = adopt
        ? set_status_toast(m, std::move(detail),
                           std::chrono::seconds{r.probe_skipped ? 12 : 6})
        : set_status_toast(m,
              spec + " added \xc2\xb7 " + std::move(detail)
                  + "  \xc2\xb7  ^P to switch",
              std::chrono::seconds{r.probe_skipped ? 12 : 8});
    return Cmd::batch(std::move(save), std::move(probe_save),
                      std::move(switch_cmd), std::move(found));
}

// 用于提供者选择的规范account/registry id —镜像下面的
// account_provider_id()帮助器，但是是self-contained，
// 因此sign_out（在其上面定义）不需要跨链接边界的前向引用。
static std::string signout_provider_id(const provider::Selection& sel) {
    // 注册表行携带IS的标识。只有无法识别的自定义
    // 主机才没有行，并且它在端点标签上键入其帐户。
    if (const auto id = sel.provider_id(); !id.empty()) return std::string{id};
    if (sel.kind == provider::Kind::OpenAI) return sel.openai_endpoint.label;
    return {};
}

Cmd sign_out(Model& m) {
    // 清除ACTIVE提供商的凭据，因此"Sign out"以用户当前登录的任何内容为目标。
    // 对应的 存储（Anthropic 的 credentials.json、
    // OAuth 令牌文件、provider_keys 条目）是保管库的问题 -
    // 每个提供者一个描述符，这里没有每个 -provider 梯子（请参阅
    // auth/vault.hpp）。
    const auto& sel = m.d.selection;
    const std::string pid = signout_provider_id(sel);
    std::string what = pid.empty() ? std::string{"credentials"}
                                   : provider::provider_display_name(sel);
    // SettingsKey通过INJECTED设置门清楚地存储
    // （托管密钥+自定义主机），因此写入对于测试的in-memory存根可见，
    // 并且与每个状态更新函数写入相同的持久化路径。
    // File-backed存储(Anthropic/OAuth令牌文件)是保管库的。
    Cmd key_save = Cmd::none();
    if (!pid.empty()) {
        if (auth::vault::of(pid).kind == auth::vault::Kind::SettingsKey) {
            if (m.d.persisted.provider_keys.erase(pid) > 0)
                key_save = save_record(m);
        } else {
            key_save = Cmd::batch(
                Cmd(AccountOp{.kind = AccountOp::Kind::SignOut, .provider = pid}),
                Cmd(LoadAuthView{}));
        }
    }

    if (sel.kind == provider::Kind::Anthropic) {
        // NOTE：这里不再需要说明-arm。 1M 权利事实以（提供商、
        // ACCOUNT、型号）为关键 — 请参阅 domain/entitlement.hpp
        // — 因此被删除的帐户永远不会代表下一个帐户。这用于清除全局 bool，
        // 该全局 bool 在另一个方向上是有损的：在授权帐户
        // 和无授权帐户之间 ping-ponging 在每一跳上
        // re-discovered 相同的 400。
        //
        // 在账户被删除的情况下，通过entitlement::forget_account
        // 处理正版标志-OUT(不是开关)。
    }

    // 将实时验证头文件归零，因此下一回合不能重复使用now
    // -revoked 凭据。
    Cmd clear_auth = Cmd(InstallAuth{.provider = pid, .clear = true});

    // 如果 ANOTHER 提供商仍然经过身份验证，请回退到该提供商，
    // 而不是在 -in 弹出面板 标志处将用户转储 — 注销多个帐户之一
    // 应该可以让您继续工作，而不是让您陷入困境。首选最近使用的 still
    // -authed 提供程序（MRU 顺序），否则第一个经过验证的 注册表 行。
    // 只有当什么都没有的时候，我们才打开-in 弹出面板的牌子。
    {
        const std::string just_left = signout_provider_id(sel);
        const auto& settings = m.d.persisted;
        // 仅回退到具有 真实 凭据 (已保存 或 env) 的提供者 — 绝不会默默地跳到
        // 用户从未选择的 Always-on 本地后端 (Ollama)。
        // 这将是一个令人惊讶的 "I signed out but I'm suddenly on some local model"
        // 跳跃。
        auto is_usable = [&](std::string_view pid) {
            if (pid == just_left || pid.empty()) return false;
            const auto* p = provider::preset_for(pid);
            if (!p) return true;   // 已保存 自定义主机（具有 provider_keys 条目）
            const auto src = provider::auth_source(*p, settings, m.d.auth);
            return src == provider::AuthSource::Saved
                || src == provider::AuthSource::Env;
        };
        std::string fallback;
        for (const auto& ref : m.d.recent_models)   // MRU，最新的在前
            if (is_usable(ref.provider_id)) { fallback = ref.provider_id; break; }
        if (fallback.empty())
            for (const auto& p : provider::providers())
                if (is_usable(p.id)) { fallback = std::string{p.id}; break; }
        if (!fallback.empty()) {
            m.ui.login = login::Closed{};
            m.s.status = "signed out of " + what + " \xe2\x80\x94 switched to "
                       + provider::provider_display_name(
                             provider::parse_selection(fallback));
            m.s.status_until = m.now
                             + std::chrono::seconds{5};
            return Cmd::batch(
                std::move(key_save), std::move(clear_auth),
                commit_provider_switch(
                    m, fallback,
                    provider::provider_display_name(
                        provider::parse_selection(fallback))));
        }
    }

    // 没有进行任何其他验证——将用户直接放入sign-in。
    m.ui.login = login::Picking{};
    m.s.status = "signed out of " + what + " \xe2\x80\x94 sign in to continue";
    m.s.status_until = m.now
                     + std::chrono::seconds{5};
    return Cmd::batch(std::move(key_save), std::move(clear_auth));
}

namespace {

// 活动提供商帐户的规范注册表 ID。 Anthropic、ChatGPT、
// GitHub副驾驶有文件-backed 凭据存储账户层可以快照；
// OpenAI-family 键已通过 选择面板 切换到
// -endpoint。
std::string account_provider_id(const provider::Selection& sel) {
    if (const auto id = sel.provider_id(); !id.empty()) return std::string{id};
    // 任何OpenAI-family端点——托管的API-key PRESET(
    // mistral/groq/…)OR用户-added CUSTOM HOST——在
    // 端点标签上键入其帐户==其承载密钥存在于provider_keys中的提供商ID。
    // 现在两者都支持通过同一个账户管理器进行多个账户。
    if (sel.kind == provider::Kind::OpenAI) {
        const std::string label = sel.openai_endpoint.label;
        // 无密钥本地服务器(ollama/llama.cpp)没有要管理的帐户。
        // `is_local`现在准确地意味着"runs on this machine"，
        // 因此旧的`&& !oauth_native`防护（其存在只是因为
        // ChatGPT在本地错误地标记了自己）消失了。
        const auto* p = provider::preset_for(label);
        if (p && p->is_local) return {};
        return label;
    }
    return {};   // 该提供商没有帐户切换（local/ACP）
}

// 账户行为`provider`，来自Model的AuthView。
// 尚未在 注册表（旧版单一登录）中的实时登录在其派生标签下显示为一行；
// 打开列表将其注册（参见 open_accounts）。
login::AccountList build_account_list(const Model& m, std::string provider,
                                      std::string provider_label) {
    login::AccountList al;
    al.provider       = std::move(provider);
    al.provider_label = std::move(provider_label);
    if (al.provider.empty()) return al;
    const auto* pa = m.d.auth.find(al.provider);
    if (!pa) return al;
    std::vector<std::string> labels = pa->accounts;
    std::string active = pa->active_account;
    if (labels.empty() && !pa->unregistered_label.empty()) {
        labels.push_back(pa->unregistered_label);
        active = pa->unregistered_label;
    }
    for (auto& l : labels) {
        login::AccountRow row;
        row.provider = al.provider;
        row.label    = l;
        row.active   = (l == active);
        al.rows.push_back(std::move(row));
    }
    // 将光标置于活动行上，因此 "open, hit enter" 是 no-op。
    for (int i = 0; i < static_cast<int>(al.rows.size()); ++i)
        if (al.rows[static_cast<std::size_t>(i)].active) { al.cursor = i; break; }
    return al;
}

login::AccountList build_account_list(const Model& m,
                                      const provider::Selection& sel) {
    return build_account_list(m, account_provider_id(sel),
                              provider::provider_display_name(sel));
}

} // 命名空间结束。

Cmd open_accounts(Model& m, const std::string& provider_id = {}) {
    // 如果给定的话，以所请求的提供者为目标(提供者行上的Enter)，
    // 否则为活动的提供者。从解析的选择构建意味着我们 DON'不必
    // 首先切换到提供程序 - 没有 model-picker 弹出
    // - 帐户列表立即显示用户按下 Enter 的任何行。
    const auto sel = provider_id.empty() ? m.d.selection
                                         : provider::parse_selection(provider_id);
    auto al = build_account_list(m, sel);
    if (al.provider.empty()) {
        // Provider没有可切换帐户——
        // 回退到正常的sign-in/add-key流程而不是显示空列表。
        m.ui.login = login::Picking{};
        return Cmd::none();
    }
    // 不在注册表中的实时登录已经显示为一行；注册它，
    // 以便可以像其他任何东西一样切换到和删除它。
    Cmd reg = Cmd::none();
    if (const auto* pa = m.d.auth.find(al.provider);
        pa && pa->accounts.empty() && !pa->unregistered_label.empty())
        reg = Cmd::batch(
            Cmd(AccountOp{.kind = AccountOp::Kind::Register,
                          .provider = al.provider,
                          .label = pa->unregistered_label}),
            Cmd(LoadAuthView{}));
    m.ui.login = std::move(al);
    return reg;
}

Cmd account_move(Model& m, int delta) {
    if (auto* al = std::get_if<login::AccountList>(&m.ui.login)) {
        const int n = static_cast<int>(al->rows.size()) + 1;   // +1 添加-new行
        if (n > 0) al->cursor = ((al->cursor + delta) % n + n) % n;
        al->confirm_remove.clear();
    }
    return Cmd::none();
}

Cmd account_select(Model& m) {
    auto* al = std::get_if<login::AccountList>(&m.ui.login);
    if (!al) return Cmd::none();
    const int add_row = static_cast<int>(al->rows.size());

    // 尾随"+ Add another account…"行。将延续范围保持在
    // 用户管理的提供程序范围内：ChatGPT 和 Copilot 各有一个本机
    // OAuth 方法，而 Anthropic 提供其 API-key/OAuth 选择。
    if (al->cursor >= add_row) {
        const std::string provider = al->provider;
        // UNIFORM add-account路由：凭据层表示该
        // 提供者HOW采用新帐户(API密钥与OAuth设备与无)。
        switch (provider::credentials::add_method(provider)) {
        case provider::credentials::AddMethod::ApiKey:
            // 粘贴此端点的密钥（托管密钥预设 OR 自定义主机）。
            // ApiKeyInput先提交快照当前密钥，
            // 所以这个ADDS而不是被替换。
            m.ui.login = login::ApiKeyInput{
                .provider       = provider,
                .provider_label = al->provider_label,
                // 帧携带THIS提供者-Esc可证明返回到ITS账户列表，
                // 而不是活动提供者的。
                .origin         = login::origin::Accounts{provider},
            };
            return Cmd::none();
        case provider::credentials::AddMethod::None:
            // 本地服务器——无需添加账户；留在原地。
            return Cmd::none();
        case provider::credentials::AddMethod::OAuthDevice:
            break;   // 处理如下（provider-specific启动）
        }
        // OAuth登录，从REGISTRY CAPABILITIES而不是
        // 提供者名称路由。顺序很重要，首先是 SPECIFIC 标志：
        //
        //   device_login —共享通用设备-code启动器(Copilot，
        //                   Kimi)。
        //   oauth_native：后端通过登录认证，并使用自身长期保持的传输连接。
        //                   这是 Copilot 和 Kimi 的 TRUE
        //                   以及 ChatGPT（credentials.cpp
        //                   正是这样读的），所以 不 确实可以自己
        //                   识别 Codex 流。
        //
        // 检查oauth_native首先使得每个设备-flow提供商启动ChatGPT/Codex浏览器流程：
        // 从提供商选择面板挑选"GitHub Copilot"或
        // "Kimi"打开OpenAI标志-in面板。 标志 自己的评论
        // 声称 "today only ChatGPT sets this"，
        // 当添加 Copilot 和 Kimi 后，这不再是真的，
        // 并且该分支信任评论而不是行。
        const auto* prow = provider::preset_for(provider);
        if (prow && prow->device_login)
            return launch_device_login(m, provider,
                                       std::string{prow->label});
        if (prow && prow->codex_login()) {
            // 仅状态-subscribe()运行工作线程，在attempt_id上键入。
            m.ui.login = login::ChatGptWaiting{
                .attempt_id = cmd::next_codex_login_attempt_id(),
                .device_auth = provider::chatgpt::codex_device_auth_preferred(),
            };
            return Cmd::none();
        }
        // 否则：方法菜单(OAuth 事件订阅 vs API键)。
        m.ui.login = login::Picking{
            .provider = provider,
            .origin   = login::origin::Accounts{provider}};
        return Cmd::none();
    }

    const auto& row = al->rows[static_cast<std::size_t>(al->cursor)];
    if (row.active) {                         // 已有此账号
        m.ui.login = login::Closed{};
        return Cmd::none();
    }

    const std::string provider = row.provider;
    const std::string label    = row.label;
    // 将槽复制到主机上的实时存储中，然后重新-read视图。
    // 这些看到切换后的存储后批处理的所有东西。
    Cmd activate = Cmd::batch(
        Cmd(AccountOp{.kind = AccountOp::Kind::Activate,
                      .provider = provider, .label = label}),
        Cmd(LoadAuthView{}));

    // 如果所选帐户属于ISN'T当前活动的提供者（Enter在非
    // -active提供者行→其帐户列表），则也选择该提供者的帐户
    // SWITCHES——提供者+帐户+认证+模型调用+重新获取，
    // 全部原子地——并直接返回到聊天（open_panel=假，因此没有模型
    // -picker弹出）。召回的型号为 pre-stashed，
    // 因此 commit_provider_switch 无法打开 选择面板。
    {
        const std::string active_pid =
            m.d.selection.kind == provider::Kind::OpenAI
                ? m.d.selection.openai_endpoint.label
                : std::string{provider::default_provider_id()};
        // 对于非-OpenAI类型，活动提供者的id是注册表默认值—
        // —比较id，而不是名称。 （旧形式special-cased
        // "anthropic"，因为当活动类型不是OpenAI时，
        // active_pid会回退到默认id；IS是默认id，
        // 所以比较已经涵盖了它。）
        const bool provider_changed = provider != active_pid;
        if (provider_changed) {
            m.ui.login = login::Closed{};
            const auto* p = provider::preset_for(provider);
            const std::string plabel = p ? std::string{p->label} : provider;
            std::string recalled =
                m.d.persisted.provider_models.count(provider)
                    ? m.d.persisted.provider_models.at(provider)
                    : std::string{};
            return Cmd::batch(
                std::move(activate),
                commit_provider_switch(m, provider, plabel, recalled,
                                       /*open_panel=*/false));
        }
    }
    // Re-install来自now-swapped活动商店的实时验证头文件。
    // 如果我们要切换 TO 的帐户有一个已过期或即将失效的 OAuth 令牌（
    // 切到另一个长期闲置的账号后无法使用。
    // 错误），请以启动和作曲家相同的方式启动后台刷新 — 现在安装 on
    // -disk 头文件，然后让异步刷新通过 TokenRefreshed
    // 替换它。 Non-blocking：没有 UI-thread 网络往返，
    // 并且 submit_message 在刷新进行时排队发送 (
    // oauth_refresh_in_flight)，因此第一次打开
    // 新帐户时无法使用 过期 承载者进行触发。
    Cmd refresh_cmd = Cmd::none();
    // UNIFORM:通过中央解析器为now-active账户安装实时头文件。
    // Anthropic/custom-host解析出真正的头文件； oauth
    // -native 传输层 (ChatGPT/Copilot/Kimi) 在下一轮从
    // just-swapped 存储中读取它们的令牌，因此 resolve() 返回空，
    // 并且空 CLEARS 缓存的 头文件 — 正是旧 per-provider 分支所做的
    // "force a fresh read"。主机在这个归并之后解决了它。
    Cmd install = Cmd(InstallAuth{.provider = provider});

    // Anthropic-SPECIFIC额外内容（合法地不统一）：long
    // -idle OAuth令牌可以是过期——启动后台主动刷新，
    // 因此第一次打开切换的-to账户不能在持有者失效的情况下触发；
    // 以及 re-arm 1M-context 发现（该区块是为 PREVIOUS
    // 帐户获悉的；这可能是有资格的）。
    if (const auto* prow = provider::preset_for(provider);
        prow && prow->oauth_proactive_refresh) {
        // 防止并发踢（作曲家的30秒探测/初始化）：
        // 两次刷新工作线程将交换回-to-back——无害，
        // 因为文件锁将它们序列化并且第二次re-reads是最新鲜的令牌，
        // 但是浪费并且它使in-flight 标志加倍-toggles。
        if (!m.s.oauth_refresh_in_flight) {
            m.s.oauth_refresh_in_flight = true;
            refresh_cmd = cmd::refresh_oauth_if_due();
        }
        // 这里没有权利re-arm：事实由（提供商、ACCOUNT、
        // 模型）键入，因此我们刚刚离开的帐户不能代表我们刚刚输入的帐户——并且，
        // 与旧的global-bool重置不同，我们了解到的关于
        // OUTGOING帐户的信息在用户切换回来时仍然存在。
        // 参见 domain/entitlement.hpp.
    }
    const std::string provider_label = al->provider_label;
    m.ui.login = login::Closed{};
    m.s.status = "switched " + provider_label + " to " + label;
    m.s.status_until = m.now
                     + std::chrono::seconds{4};

    // 我们持有的 目录 被作为 OUTGOING 账户获取。
    // 权利是account-scoped（Pro和Max 事件订阅服务于
    // 不同的型号列表），所以现在是错误的。删除其新鲜度标记并重新获取：
    // 如果没有这个，60 年代的 kCatalogTtlMs 防护会使 ^/ 跳过重新
    // 获取并浏览先前帐户的模型。参见domain/entitlement.hpp.
    for (auto& c : m.d.provider_catalogs)
        if (c.provider_id == provider) {
            c.loaded_at_ms = 0;
            break;
        }
    // 对于ACTIVE提供者来说，经历fetch_models(m)：它
    // 填充d.available_models(作曲家和活动行读取的内容)以及融合的目录。
    // fetch_models_for 仅发送 FusedCatalogLoaded，
    // 这会将 available_models 留在旧
    // 帐户的列表中。
    const bool is_active = provider == m.d.selection.catalog_id();
    Cmd refetch = is_active ? cmd::fetch_models(m)
                            : cmd::fetch_models_for(provider);
    if (is_active) m.s.models_loading = true;

    return Cmd::batch(std::move(activate), std::move(install),
                      std::move(refresh_cmd), std::move(refetch));
}

Cmd account_remove(Model& m) {
    auto* al = std::get_if<login::AccountList>(&m.ui.login);
    if (!al) return Cmd::none();
    const int add_row = static_cast<int>(al->rows.size());
    if (al->cursor >= add_row) return Cmd::none();   // add-new 行：没有要删除的内容

    const auto row = al->rows[static_cast<std::size_t>(al->cursor)];

    // 破坏性行为是故意的两个-step。杂散的 Backspace
    // 或 vim `d` 绝不能无路可退地擦除 已保存 刷新令牌。
    if (al->confirm_remove != row.label) {
        al->confirm_remove = row.label;
        return Cmd::none();
    }

    const bool was_active = row.active;
    const std::string provider_label = al->provider_label;
    al->confirm_remove.clear();
    Cmd remove = Cmd(AccountOp{.kind = AccountOp::Kind::Remove,
                               .provider = row.provider, .label = row.label});

    // 为 此 帐户了解到的权利事实永远无法再次查阅——
    // 并且稍后的 re-login 可能会落在不同的
    // 事件订阅 层上，因此保留它们将是错误的，也是死的。
    // 这是一个遗忘是正确的地方：移除，而不是切换。
    Cmd forget_save = Cmd::none();
    {
        auto& s = m.d.persisted;
        const auto before = s.entitlements.size();
        domain::entitlement::forget_account(s.entitlements, row.provider,
                                            row.label);
        if (s.entitlements.size() != before) forget_save = save_record(m);
    }

    // 如果我们删除了当前授权的帐户，则 注册表 会升级剩余的最新帐户；
    // 让它活起来。 Model 的视图说明了哪个是（与
    // accounts::remove 相同的规则：最新的在前）。
    std::vector<std::string> rest;
    if (const auto* pa = m.d.auth.find(row.provider))
        for (const auto& l : pa->accounts)
            if (l != row.label) rest.push_back(l);
    Cmd after = Cmd::none();
    if (was_active && !rest.empty()) {
        after = Cmd::batch(
            Cmd(AccountOp{.kind = AccountOp::Kind::Activate,
                          .provider = row.provider, .label = rest.front()}),
            Cmd(InstallAuth{.provider = row.provider}));
        m.s.status = "removed " + row.label + " \xc2\xb7 switched to " + rest.front();
    } else if (was_active) {
        // 注册表现在是空的。也清除实时 凭据（文件 OR
        // provider_keys[spec]）；
        // 否则帐户列表会再次发现它并复活用户刚刚删除的帐户。
        after = Cmd::batch(
            Cmd(AccountOp{.kind = AccountOp::Kind::ClearActive,
                          .provider = row.provider}),
            Cmd(InstallAuth{.provider = row.provider, .clear = true}));
        m.s.status = "removed the last " + provider_label + " account";
    } else {
        m.s.status = "removed " + row.label;
    }
    m.s.status_until = m.now
                     + std::chrono::seconds{4};
    // 该列表保持开放； AuthViewLoaded 重建其行。
    return Cmd::batch(std::move(remove), std::move(forget_save),
                      std::move(after), Cmd(LoadAuthView{}));
}

Cmd login_pick_method(Model& m, char32_t key) {
    const auto* picking = std::get_if<login::Picking>(&m.ui.login);
    if (!picking && !std::holds_alternative<login::Failed>(m.ui.login))
        return Cmd::none();
    // 该提供商是否提供 CHOICE 身份验证方法？ 注册表 标志，
    // 不是名称：第二个 OAuth-or-key 提供者通过设置来获取菜单。
    const auto* mrow = picking ? provider::preset_for(picking->provider) : nullptr;
    const bool anthropic_only = mrow && mrow->method_menu;
    if (key == U'2') {
        // OAuth:PKCE对是随机的，因此Cmd铸造了它；
        // 回复 (LoginOAuthMinted) 移至 OAuthCode 并打开浏览器。
        return cmd::mint_oauth_login();
    }
    if (key == U'1') {
        login::ApiKeyInput api;
        // Esc=返回THIS方法菜单，范围为同一提供者。
        api.origin = login::origin::Method{
            picking ? picking->provider : std::string{}};
        m.ui.login = std::move(api);
        return Cmd::none();
    }
    if (key == U'3' && !anthropic_only) {
        // 本机 ChatGPT OAuth。本地终端使用浏览器+环回回调；
        // SSH 终端 自动使用 OpenAI 设备身份验证并通过
        // CodexDeviceCodeReady 接收 one-time 代码。
        //
        // 仅状态-subscribe()运行工作线程，在attempt_id上键入。
        m.ui.login = login::ChatGptWaiting{
            .attempt_id = cmd::next_codex_login_attempt_id(),
            .device_auth = provider::chatgpt::codex_device_auth_preferred(),
        };
        return Cmd::none();
    }
    if (key == U'4' && !anthropic_only) {
        // 自定义OpenAI-compatible主机（llama.cpp、
        // vLLM、LM Studio或任何远程服务器）。重用提供者 选择
        // 面板 的 "Custom host…" 行打开的相同
        // CustomHostInput 流程 — 用户键入主机[:端口]，
        // TLS 主机 提示词 获取 API 密钥，本地主机无密钥提交。
        // 当提供者切换提交时，模型 选择面板 自动打开。
        {
            login::CustomHostInput ch;
            // Esc=返回THIS方法菜单，范围为同一提供者。
            ch.origin = login::origin::Method{
                picking ? picking->provider : std::string{}};
            m.ui.login = std::move(ch);
        }
        return Cmd::none();
    }
    if (key == U'5' && !anthropic_only) {
        // 本机GitHub Copilot OAuth (设备流）。
        return launch_device_login(m, "copilot", "GitHub Copilot");
    }
    if (key == U'6' && !anthropic_only) {
        // 本机Kimi OAuth(设备流程)。
        return launch_device_login(m, "kimi", "Kimi");
    }
    return Cmd::none();
}

Cmd login_char_input(Model& m, char32_t ch) {
    auto utf8 = ui::utf8_encode(ch);
    std::visit(overload{
        [&](login::OAuthCode& s) {
            s.code_input.insert(s.cursor, utf8);
            s.cursor += static_cast<int>(utf8.size());
        },
        [&](login::ApiKeyInput& s) {
            s.key_input.insert(s.cursor, utf8);
            s.cursor += static_cast<int>(utf8.size());
        },
        [&](login::CustomHostInput& s) {
            s.host_input.insert(s.cursor, utf8);
            s.cursor += static_cast<int>(utf8.size());
        },
        [](auto&) {},
    }, m.ui.login);
    return Cmd::none();
}

Cmd login_backspace(Model& m) {
    std::visit(overload{
        [](login::OAuthCode& s) {
            if (s.cursor > 0 && !s.code_input.empty()) {
                int p = ui::utf8_prev(s.code_input, s.cursor);
                s.code_input.erase(p, s.cursor - p);
                s.cursor = p;
            }
        },
        [](login::ApiKeyInput& s) {
            if (s.cursor > 0 && !s.key_input.empty()) {
                int p = ui::utf8_prev(s.key_input, s.cursor);
                s.key_input.erase(p, s.cursor - p);
                s.cursor = p;
            }
        },
        [](login::CustomHostInput& s) {
            if (s.cursor > 0 && !s.host_input.empty()) {
                int p = ui::utf8_prev(s.host_input, s.cursor);
                s.host_input.erase(p, s.cursor - p);
                s.cursor = p;
            }
        },
        [](auto&) {},
    }, m.ui.login);
    return Cmd::none();
}

Cmd login_paste(Model& m, std::string text) {
    std::visit(overload{
        [&](login::OAuthCode& s) {
            s.code_input.insert(s.cursor, text);
            s.cursor += static_cast<int>(text.size());
        },
        [&](login::ApiKeyInput& s) {
            s.key_input.insert(s.cursor, text);
            s.cursor += static_cast<int>(text.size());
        },
        [&](login::CustomHostInput& s) {
            s.host_input.insert(s.cursor, text);
            s.cursor += static_cast<int>(text.size());
        },
        [](auto&) {},
    }, m.ui.login);
    return Cmd::none();
}

Cmd login_cursor_left(Model& m) {
    std::visit(overload{
        [](login::OAuthCode& s) {
            s.cursor = ui::utf8_prev(s.code_input, s.cursor);
        },
        [](login::ApiKeyInput& s) {
            s.cursor = ui::utf8_prev(s.key_input, s.cursor);
        },
        [](login::CustomHostInput& s) {
            s.cursor = ui::utf8_prev(s.host_input, s.cursor);
        },
        [](auto&) {},
    }, m.ui.login);
    return Cmd::none();
}

Cmd login_cursor_right(Model& m) {
    std::visit(overload{
        [](login::OAuthCode& s) {
            s.cursor = ui::utf8_next(s.code_input, s.cursor);
        },
        [](login::ApiKeyInput& s) {
            s.cursor = ui::utf8_next(s.key_input, s.cursor);
        },
        [](login::CustomHostInput& s) {
            s.cursor = ui::utf8_next(s.host_input, s.cursor);
        },
        [](auto&) {},
    }, m.ui.login);
    return Cmd::none();
}

Cmd login_submit(Model& m) {
    if (auto* ch = std::get_if<login::CustomHostInput>(&m.ui.login)) {
        // 通过 一个 共享助手进行规范化（修剪 + 删除尾部 '/'）
        // ——与 CLI --provider 路径应用相同的规范化，
        // 因此两个入口点永远不能为同一端点创建不同的设置键（
        // custom-host PR 系列背后的根本原因）。
        std::string spec = provider::canonical_spec(ch->host_input);
        if (spec.empty()) {
            m.ui.login = login::Failed{"no host entered"};
            return Cmd::none();
        }

        // 无论是否TLS，先探测。许多远程主机不需要密钥（RunPod
        // 或隧道 llama.cpp），并且探测器的 401/403
        // 是对 "does it want one" 的诚实答案（
        // host_probed 然后打开密钥 提示词）。已有 已保存 密钥的
        // 主机会直接转到 提示词，以便可以检查或更改该密钥。
        std::string existing_key;
        if (auto it = m.d.persisted.provider_keys.find(spec);
            it != m.d.persisted.provider_keys.end())
            existing_key = it->second;
        if (!existing_key.empty()) {
            std::string label = provider::provider_display_name(
                provider::parse_selection(spec));
            // 捕获大小BEFORE的移动：指定的初始值设定项按声明顺序求值（key_input
            // 在光标之前），因此当.cursor读取.size()时，
            // std::move(existing_key)进入.
            // key_input将留下existing_key移动-from。
            const int key_len = static_cast<int>(existing_key.size());
            m.ui.login = login::ApiKeyInput{
                .key_input      = std::move(existing_key),
                .cursor         = key_len,
                .provider       = spec,
                .provider_label = std::move(label),
                // Esc=re-edit主机； 帧 带有 TYPED 规范，
                // 因此用户输入的任何内容都不会丢失。
                .origin         = login::origin::HostInput{spec},
            };
            return Cmd::none();
        }

        // 无 已保存 键：提交前探测 — 在
        // 工作线程 (配置路径 → /v1/models →
        // Ollama /api/tags) 上拨打模型列表，
        // 并且仅使用 DETECTED 方言提交答案。 弹出面板
        // 显示 "probing…"；失败返回此处，并恢复规范并指定原因。
        // 不再盲目地致力于死端点并在第一个 提示词 时发现它。
        {
            const auto attempt_id = cmd::next_codex_login_attempt_id();
            auto origin = std::move(ch->origin);
            m.ui.login = login::HostProbing{
                .spec = spec, .attempt_id = attempt_id,
                .origin = std::move(origin)};
            return cmd::probe_host_async(spec, attempt_id);
        }
    }
    if (auto* api = std::get_if<login::ApiKeyInput>(&m.ui.login)) {
        std::string key = std::move(api->key_input);
        std::string provider = api->provider;
        std::string provider_label = api->provider_label;
        // 修剪尾随空白——粘贴处理函数可能包括杂散换行符，
        // 这取决于终端粘贴行为。
        while (!key.empty() && (key.back() == '\r' || key.back() == '\n'
                              || key.back() == ' ' || key.back() == '\t'))
            key.pop_back();
        if (key.empty()) {
            // 自定义主机可能根本不需要密钥：无需密钥即可检查它，
            // 而不是弹回到sign-in菜单。
            if (!provider.empty() && !provider::preset_for(provider)
                && !api->key_required
                && provider::parse_selection(provider).kind == provider::Kind::OpenAI) {
                const auto attempt_id = cmd::next_codex_login_attempt_id();
                m.ui.login = login::HostProbing{
                    .spec = provider, .attempt_id = attempt_id,
                    .origin = login::origin::HostInput{provider}};
                return cmd::probe_host_async(std::move(provider), attempt_id);
            }
            if (api->key_required) {
                // 留在提示词：主机说需要一个。
                return set_status_toast(m, provider_label + " needs an API key",
                                        std::chrono::seconds{4});
            }
            m.ui.login = login::Failed{"no key entered"};
            return Cmd::none();
        }

        // 没有选择提供商？让 KEY 说出其中一个。
        //
        // 该弹出面板通过两种方式达到。来自提供者 选择面板，
        // 其中 `provider` 已设置，这不会执行任何操作。在 首次 RUN 上，
        // 尚未选择任何内容，`provider` 是空的，而空始终意味着
        // "Anthropic" — 因此，粘贴到提供 "any provider"
        // 的 提示词 中的 Groq 密钥被存储为 Anthropic 凭据，
        // 并且在命名错误供应商的第一回合中失败（问题 #68）。
        //
        // 只有明确的前缀才算数。裸露的 `sk-` 保持为空并
        // 保留 Anthropic 默认值，这对于方法菜单以
        // Claude 开头的工具来说是正确的猜测 - 修复是
        // 它现在是为不明确的情况保留的猜测，而不是所有情况的
        // 答案。
        if (provider.empty()) {
            if (const auto detected = provider::provider_for_api_key(key);
                !detected.empty() && detected != "anthropic") {
                provider = std::string{detected};
                const auto* row = provider::preset_for(detected);
                provider_label = row ? std::string{row->label} : provider;
            }
        }

        // OpenAI-family键：保留在Settings.provider_keys
        // [id]下，然后提交延迟的选择面板的实时提供商切换。 Anthropic
        // 路径（空提供程序）继续使用下面的 credentials.json。
        if (!provider.empty()) {
            // 决定BEFORE插入密钥：no_provider_configured
            // 读取provider_keys，因此在插入之后
            // 询问将总是看到该密钥并且永远不会采用——第一次
            // 运行将登录但仍然指向它无法使用的提供者。
            const bool adopt = no_provider_configured(m);
            // 将粘贴的密钥保留为该提供者的活动帐户（快照首先是任何先前的密钥，
            // 因此它是ADDS，而不是替换）。
            // 密钥现在进入记录； AddKey 在主机上
            // 执行 注册表 快照，LoadAuthView 将
            // 保管库恢复原状。
            m.d.persisted.provider_keys[provider] = key;
            Cmd add = Cmd::batch(
                Cmd(AccountOp{.kind = AccountOp::Kind::AddKey,
                              .provider = provider, .key = key}),
                Cmd(LoadAuthView{}));
            m.ui.login = login::Closed{};
            // PASTING A KEY IS 不 SWITCHING。
            // 在 Claude 上的 mid-task 期间注册
            // Groq 密钥用于移动活动后端以及随之而来的模型
            // - 交换机必须为新提供商选择一个密钥。仅当尚未配置任何
            // 内容且没有要保护的会话时才采用。
            if (!adopt)
                return Cmd::batch(
                    std::move(add), save_record(m),
                    provider_ready_without_switch(m, provider_label));
            m.d.persisted.provider = provider;
            return Cmd::batch(
                std::move(add), save_record(m),
                commit_provider_switch(m, provider, provider_label));
        }

        return install_and_close(
            m, auth::Credentials{auth::cred::ApiKey{std::move(key)}},
            std::holds_alternative<login::origin::Accounts>(api->origin));
    }
    if (auto* oc = std::get_if<login::OAuthCode>(&m.ui.login)) {
        std::string code_raw = std::move(oc->code_input);
        while (!code_raw.empty() && (code_raw.back() == '\r' || code_raw.back() == '\n'
                                   || code_raw.back() == ' ' || code_raw.back() == '\t'))
            code_raw.pop_back();
        if (code_raw.empty()) {
            // 留在 OAuthCode — 保持验证器完整，以便用户可以
            // re-paste 而无需重新打开浏览器。
            return Cmd::none();
        }
        auto verifier = std::move(oc->verifier);
        auto state    = std::move(oc->state);
        // OAuthCode不携带来源，因此在交换时无法将故意的
        // "+ Add another account" OAuth登录与re-login区分开。
        // 默认为 REUSE（as_new_account = false）：
        // 常见情况是同一帐户的 re-auth，并且两个 Anthropic
        // OAuth 登录无论如何都是内容 -indistinguishable（传输
        // 上没有 email/subject）——因此将它们折叠到一个 "OAuth login"
        // 插槽上是诚实的，而不是有损的。具有真实身份的 Multi-account
        // 由 API 密钥/ChatGPT 提供服务。
        m.ui.login = login::OAuthExchanging{/*as_new_account=*/false};
        return cmd::oauth_exchange(auth::OAuthCode{std::move(code_raw)},
                                std::move(verifier), std::move(state));
    }
    return Cmd::none();
}

Cmd login_copy_auth_url(Model& m) {
    // 复制验证码URL。可在 OAuth-code 屏幕和任何设备-flow 弹出面板
    // (Copilot/Kimi) 上工作 — 一条路径，因此每个提供商的行为
    // 都是相同的。
    std::string url;
    if (auto* oc = std::get_if<login::OAuthCode>(&m.ui.login)) url = oc->authorize_url;
    else if (auto* dw = std::get_if<login::DeviceWaiting>(&m.ui.login))
        url = dw->browser_url.empty() ? dw->authorize_url : dw->browser_url;
    if (url.empty()) return Cmd::none();
    (void)write_clipboard_text(url);   // 原生的 pbcopy/wl-copy/xclip
    auto write_cmd = cmd::write_clipboard(url);
    auto toast = set_status_toast(m, "authorize URL copied to clipboard",
                                  std::chrono::seconds{3});
    return Cmd::batch(std::move(write_cmd), std::move(toast));
}

Cmd login_copy_code(Model& m) {
    // 复制one-time CODE(用户在浏览器中输入的内容)。
    // 仅设备流 - 终端 text-selection 无法抓取它，
    // 因为 弹出面板 re-renders 每次轮询都会擦除任何选择，
    // 因此此击键是进入剪贴板的可靠方式。每个设备提供商都有一条路径。
    auto* dw = std::get_if<login::DeviceWaiting>(&m.ui.login);
    if (!dw || dw->user_code.empty()) return Cmd::none();
    auto code = dw->user_code;
    (void)write_clipboard_text(code);
    auto write_cmd = cmd::write_clipboard(code);
    auto toast = set_status_toast(m, "code " + code + " copied to clipboard",
                                  std::chrono::seconds{3});
    return Cmd::batch(std::move(write_cmd), std::move(toast));
}

Cmd login_open_browser_again(Model& m) {
    std::string url;
    if (auto* oc = std::get_if<login::OAuthCode>(&m.ui.login)) url = oc->authorize_url;
    else if (auto* dw = std::get_if<login::DeviceWaiting>(&m.ui.login))
        url = dw->browser_url.empty() ? dw->authorize_url : dw->browser_url;
    if (url.empty()) return Cmd::none();
    auto open_cmd = cmd::open_browser_async(std::move(url));
    auto toast = set_status_toast(m,
        "opening browser\xe2\x80\xa6",
        std::chrono::seconds{2});
    return Cmd::batch(std::move(open_cmd), std::move(toast));
}

Cmd login_exchanged(Model& m, auth::TokenResult result) {
    auto* xchg = std::get_if<login::OAuthExchanging>(&m.ui.login);
    if (!xchg)
        return Cmd::none();
    const bool as_new = xchg->as_new_account;
    if (!result) {
        m.ui.login = login::Failed{result.error().render()};
        return Cmd::none();
    }
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        m.wall_now().time_since_epoch()).count();
    auto& tok = *result;
    return install_and_close(m, auth::Credentials{auth::cred::OAuth{
        std::move(tok.access_token),
        std::move(tok.refresh_token),
        tok.expires_in_s ? now_ms + tok.expires_in_s * 1000 : 0,
    }}, as_new);
}

Cmd login_codex_device_code_ready(Model& m, std::uint64_t attempt_id,
                                   std::string verification_url,
                                   std::string user_code) {
    auto* waiting = std::get_if<login::ChatGptWaiting>(&m.ui.login);
    if (!waiting || waiting->attempt_id != attempt_id)
        return Cmd::none();
    waiting->device_auth = true;
    waiting->authorize_url = std::move(verification_url);
    waiting->user_code = std::move(user_code);
    return Cmd::none();
}

Cmd login_codex_done(Model& m, std::uint64_t attempt_id,
    std::expected<provider::chatgpt::CodexCredentials, auth::OAuthError> result)
{
    auto* waiting = std::get_if<login::ChatGptWaiting>(&m.ui.login);
    if (!waiting || waiting->attempt_id != attempt_id)
        return Cmd::none();
    // 没有 取消 会跳闸：下面的每个臂都离开 ChatGptWaiting，
    // 这会掉落 数据流 的钥匙并停止 工作线程。
    if (!result) {
        m.ui.login = login::Failed{result.error().render()};
        return Cmd::none();
    }
    // 持久化仅在尝试上述身份检查之后发生。因此，
    // 废弃或被取代的 工作线程 既不能切换提供商，
    // 也不能覆盖活动的 凭据 存储。写入本身是主机运行的
    // 副作用（信用证锁下的加密文件IO不能在状态更新函数中运行）；
    // BEFORE是批量交换机，而副作用是批量按顺序运行的，
    // 所以交换机看到的是已保存 凭据。
    m.ui.login = login::Closed{};
    m.s.status = "signed in to ChatGPT";
    m.s.status_until = m.now + std::chrono::seconds{4};
    return Cmd::batch(Cmd(SaveCodexCredentials{std::move(*result)}),
                      commit_provider_switch(m, "chatgpt", "ChatGPT"));
}

Cmd login_device_code_ready(Model& m, std::string provider,
                             std::uint64_t attempt_id,
                             std::string verification_url,
                             std::string browser_url,
                             std::string user_code) {
    auto* waiting = std::get_if<login::DeviceWaiting>(&m.ui.login);
    if (!waiting || waiting->provider != provider
                 || waiting->attempt_id != attempt_id)
        return Cmd::none();
    waiting->authorize_url = std::move(verification_url);   // 裸露(码域)
    if (browser_url.empty()) browser_url = waiting->authorize_url;
    waiting->browser_url = browser_url;
    waiting->user_code = std::move(user_code);
    // Best-effort:打开PRE-FILLED URL，因此用户不必键入代码。
    // 如果不能也无害 (SSH/headless) — 面板显示用于手动
    // 输入的裸网址 + 代码。
    return cmd::open_browser_async(std::move(browser_url));
}

Cmd login_device_done(Model& m, std::string provider, std::string provider_label,
                       std::uint64_t attempt_id,
                       std::optional<std::string> error) {
    auto* waiting = std::get_if<login::DeviceWaiting>(&m.ui.login);
    if (!waiting || waiting->provider != provider
                 || waiting->attempt_id != attempt_id)
        return Cmd::none();
    // 没有 取消 跳闸 — 参见 login_codex_done。
    if (error) {
        m.ui.login = login::Failed{std::move(*error)};
        return Cmd::none();
    }
    // 工作线程已持久化令牌； 传输层 在第一回合时会
    // 懒惰地读取它。立即切换活动提供商。
    m.ui.login = login::Closed{};
    m.s.status = "signed in to " + provider_label;
    m.s.status_until = m.now + std::chrono::seconds{4};
    return commit_provider_switch(m, provider, provider_label);
}

namespace {
// 既然已经解决，则发送在OAuth refresh/probe后面排队的第一条消息。
// 当轮流正在运行或没有任何队列时，会出现 no-op。
Cmd drain_queued_after_refresh(Model& m) {
    if (!m.s.is_idle() || m.ui.composer.queued.empty()) return Cmd::none();
    auto& head = m.ui.composer.queued.front();
    m.ui.composer.text        = std::move(head.text);
    m.ui.composer.attachments = std::move(head.attachments);
    m.ui.composer.cursor      = static_cast<int>(m.ui.composer.text.size());
    m.ui.composer.queued.erase(m.ui.composer.queued.begin());
    return submit_message(m);
}
} // 命名空间结束。

Cmd token_refreshed(Model& m, auth::TokenResult result) {
    // 背景-refresh结果。与 login_exchanged 不同：此路径是
    // 由 `init()`（磁盘上的 过期-but-refreshable 令牌）
    // 或由 StreamError 处理函数 对 mid-session 401
    // 做出反应而启动的（请参阅 stream.cpp、ErrorClass::Auth
    // 分支）。无论如何，弹出面板 状态都不会改变；
    // 停在 retry::Scheduled 的 数据流 ctx 告诉我们欠一辆 RetryStream。
    m.s.oauth_refresh_in_flight = false;

    // 数据流 是否已停放等待此刷新？如果是这样，我们要么恢复它（成功），
    // 要么将其关闭到空闲（失败）。通过活动 ctx 上的
    // retry::Scheduled 在结构上进行检测 — 在没有
    // RetryStream 已在运行的情况下，阶段达到该状态的唯一方法是
    // stream.cpp 中的 auth-refresh 分支。
    const bool stream_parked = m.s.in_scheduled();

    if (!result) {
        // BENIGN RACE，不是故障：账户切换替换了凭据
        // 商店后刷新落地。 Switched-to 帐户的令牌
        // 已经生效（激活安装它），并且刷新的令牌属于
        // Switched-AWAY 帐户 - 安装它会跨越
        // -wire 帐户，并且错误 toast 会通过他们看不到
        // 的 竞态 向用户发出警报。如果 数据流 在此刷新时停放，
        // 则 重试：它将拾取实时（已切换 -to）头文件。
        if (result.error().kind == auth::OAuthErrorKind::Superseded) {
            AGT_LOG(Auth, Info, "auth.refresh.superseded_dropped",
                    "stream_parked={}", stream_parked ? 1 : 0);
            if (stream_parked)
                return Cmd::after(std::chrono::milliseconds{0},
                                        Msg{RetryStream{}});
            return drain_queued_after_refresh(m);
        }
        // 刷新失败 — 在底行中显示 带类型的 错误。
        // "error:"前缀触发shortcut_row.cpp的
        // 危险样式。 6s给了用户在toast过期之前阅读的时间；
        // Cmd::after 哨兵 auto-clears 因此稍后的
        // 状态写入不会得到 pre-empted。
        std::string text = std::string{"error: token refresh failed: "}
                         + result.error().render();

        // 如果 数据流 停在这次刷新上，请将其拆除：没有
        // 新的令牌到来，因此 重试 将再次返回 401。
        // 降至空闲状态并完成任何 in-flight 工具 调用，
        // 以便可以通过登录 弹出面板 干净地恢复会话。
        if (stream_parked) {
            auto now = m.now;
            if (!m.d.current.messages.empty()
                && m.d.current.messages.back().role == Role::Assistant) {
                auto& last = m.d.current.messages.back();
                last.error = text;
                for (auto& tc : last.tool_calls) {
                    // 通常仍然是Pending/Approved——数据流停在
                    // 分派之前——所以这是确定MUST没有记录窗口。
                    // 参见 ToolUse::settle_failed。
                    if (!tc.is_terminal())
                        tc.settle_failed(now, "auth refresh failed");
                    std::string{}.swap(tc.args_streaming);
                }
                if (last.text.empty() && last.tool_calls.empty()) {
                    m.d.current.messages.pop_back();
                }
            }
            end_turn(m.s.phase);
        }

        auto cmd = set_status_toast(m, std::move(text),
                                    std::chrono::seconds{6});
        // 保留任何排队的作曲家文本——用户可以重新提交(在通过登录
        // 弹出面板重新-authenticating之后)，
        // 而无需重新键入。该状态下的第一次手动发送将命中 过期
        // -token 401 路径，但 in-app 登录 弹出
        // 面板 是恢复表面。
        return cmd;
    }

    // 刷新OK。工作人员已经持久化（在 cross
    // -process 锁下，在服务器不旋转时保留旧的
    // 刷新令牌）并检查存储仍然持有该令牌；
    // 切换后的商店返回为上面的“已取代”。剩下的就是安装标头，
    // 主机在折叠后从存储中解析该标头。
    Cmd install = Cmd(InstallAuth{.provider = "anthropic"});

    auto toast_cmd = set_status_toast(m, "OAuth token refreshed",
                                      std::chrono::seconds{3});

    // 数据流被停放等待该刷新（StreamError 处理函数的
    // Auth分支在流式传输{retry::Scheduled}中离开了阶段）。
    // 通过调度RetryStream来恢复——现有的RetryStream
    // 臂将重试翻转回新鲜和调用 launch_stream，
    // 其从Deps拾取新鲜的-installed承载者。
    // 排空刷新进行时用户排队的任何文本。
    if (stream_parked) {
        return Cmd::batch(
                std::move(install), std::move(toast_cmd),
                Cmd::after(std::chrono::milliseconds{0},
                                Msg{RetryStream{}}));
    }
    return Cmd::batch(std::move(install), std::move(toast_cmd),
                      drain_queued_after_refresh(m));
}

// ============================================================================
// login_update — 状态更新函数 对于 `msg::LoginMsg`
// ============================================================================
// 比上面的 per-arm 助手薄 分派； 带类型的 状态
// -machine 保证帮助者看到处于正确状态的弹出面板。

Cmd login_update(Model& m, msg::LoginMsg lm) {
    return std::visit(overload{
        [&](OpenLogin)              -> Cmd { return open_login(m); },
        [&](CloseLogin)             -> Cmd { return close_login(m); },
        [&](LoginBack)              -> Cmd { return login_back(m); },
        [&](HostProbed& e)          -> Cmd { return host_probed(m, std::move(e)); },
        [&](SignOut)                -> Cmd { return sign_out(m); },
        [&](OpenAccounts& e)        -> Cmd { return open_accounts(m, e.provider); },
        [&](AccountMove& e)         -> Cmd { return account_move(m, e.delta); },
        [&](AccountSelect)          -> Cmd { return account_select(m); },
        [&](AccountRemove)          -> Cmd { return account_remove(m); },
        [&](LoginPickMethod& e)     -> Cmd { return login_pick_method(m, e.key); },
        [&](LoginCharInput& e)      -> Cmd { return login_char_input(m, e.ch); },
        [&](LoginBackspace)         -> Cmd { return login_backspace(m); },
        [&](LoginPaste& e)          -> Cmd { return login_paste(m, std::move(e.text)); },
        [&](LoginCursorLeft)        -> Cmd { return login_cursor_left(m); },
        [&](LoginCursorRight)       -> Cmd { return login_cursor_right(m); },
        [&](LoginSubmit)            -> Cmd { return login_submit(m); },
        [&](LoginCopyAuthUrl)       -> Cmd { return login_copy_auth_url(m); },
        [&](LoginCopyCode)          -> Cmd { return login_copy_code(m); },
        [&](LoginOpenBrowserAgain)  -> Cmd { return login_open_browser_again(m); },
        [&](LoginExchanged& e)      -> Cmd { return login_exchanged(m, std::move(e.result)); },
        [&](LoginOAuthMinted& e)    -> Cmd {
            // 仅当所询问的方法菜单仍然存在时； Esc 或
            // 同时选择的第二个选项使此回复为 过期。
            if (!std::holds_alternative<login::Picking>(m.ui.login)
                && !std::holds_alternative<login::Failed>(m.ui.login))
                return Cmd::none();
            if (!e.error.empty()) {
                m.ui.login = login::Failed{
                    "could not start secure login: " + e.error};
                return Cmd::none();
            }
            login::OAuthCode oc;
            oc.verifier      = std::move(e.verifier);
            oc.state         = std::move(e.state);
            oc.authorize_url = e.authorize_url;
            m.ui.login = std::move(oc);
            return cmd::open_browser_async(std::move(e.authorize_url));
        },
        [&](CodexDeviceCodeReady& e) -> Cmd {
            return login_codex_device_code_ready(m, e.attempt_id,
                std::move(e.verification_url), std::move(e.user_code));
        },
        [&](CodexLoginDone& e)      -> Cmd {
            return login_codex_done(m, e.attempt_id,
                                    std::move(e.result));
        },
        [&](DeviceCodeReady& e) -> Cmd {
            return login_device_code_ready(m, std::move(e.provider),
                e.attempt_id, std::move(e.verification_url),
                std::move(e.browser_url), std::move(e.user_code));
        },
        [&](DeviceLoginDone& e)    -> Cmd {
            return login_device_done(m, std::move(e.provider),
                std::move(e.provider_label), e.attempt_id, std::move(e.error));
        },
        [&](TokenRefreshed& e)      -> Cmd { return token_refreshed(m, std::move(e.result)); },
        [&](AuthViewLoaded& e)      -> Cmd {
            m.d.auth = std::move(e.view);
            // 金库是宿主的；按现在的样子拿走钥匙。
            m.d.persisted.provider_keys = std::move(e.provider_keys);
            // 由本回复之前的按键建立了由同一开关打开的型号选择面板；
            // 重建它，以便显示 just-added 主机。
            refresh_open_model_picker(m);
            // 开立账户列表反映注册表；将其重建到位，
            // 使光标保持在原来的位置附近。
            if (auto* al = std::get_if<login::AccountList>(&m.ui.login)) {
                const int cur = al->cursor;
                auto rebuilt = build_account_list(m, al->provider,
                                                  al->provider_label);
                rebuilt.cursor = std::min(cur,
                    static_cast<int>(rebuilt.rows.size()));
                rebuilt.confirm_remove = std::move(al->confirm_remove);
                m.ui.login = std::move(rebuilt);
            }
            return Cmd::none();
        },
        [&](OAuthRefreshNotDue)     -> Cmd {
            m.s.oauth_refresh_in_flight = false;
            // 当探测器外出时发送的消息在其后面排队。
            // 没有任何内容刷新，立即发送；如果没有这个，它会保持为
            // "1 queued" 直到下一回合结束。
            return drain_queued_after_refresh(m);
        },
    }, lm);
}

} // namespace agentty::app::detail
