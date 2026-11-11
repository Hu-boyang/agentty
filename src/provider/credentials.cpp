// agentty::provider::credentials——实施。
// 设计见 credentials.hpp。每个提供商的存储后端：
//   • Anthropic → auth::credentials.JSON（OAuth 或x-api-key）。
//   • ChatGPT/Copilot/Kimi (oauth_native) → 他们自己的 凭据 文件，读取于
//     他们的 REQUEST 时间为 传输层； resolve() 返回
//     空的 AuthHeader（传输层 提供令牌）。我们仍然通过帐户
//     注册表 公开帐户管理，其中 快照 这些文件。
//   • 托管API-key 预设+自定义主机 → Settings.provider_keys[id],
//     静态密封在 auth::keys 保险库 (provider-keys.json) 中。
//   • 本地(AuthStyle::None) → 无凭据；仍然保持为空
//     无钥匙排，以便主持人 选择面板 保留其条目。

#include "agentty/provider/credentials.hpp"

#include <cstdlib>

#include "agentty/provider/registry.hpp"
#include "agentty/provider/selection.hpp"
#include "agentty/provider/chatgpt/codex_oauth.hpp"
#include "agentty/provider/copilot/copilot_oauth.hpp"
#include "agentty/provider/kimi/kimi_oauth.hpp"
#include "agentty/io/persistence.hpp"

namespace agentty::provider::credentials {

namespace {

std::string env_or_empty(std::string_view name) {
    if (name.empty()) return {};
    const char* v = std::getenv(std::string{name}.c_str());
    return v ? std::string{v} : std::string{};
}

// id 的提供者描述符（nullptr ⇒ 自定义主机/未知规范）。
const ProviderPreset* preset(std::string_view id) { return preset_for(id); }

bool is_anthropic(std::string_view id) {
    const auto* p = preset(id);
    return p ? p->kind() == Kind::Anthropic
             : parse_selection(id).kind == Kind::Anthropic;
}

// oauth_native 提供商在请求时管理自己的令牌。
bool is_oauth_native(std::string_view id) {
    const auto* p = preset(id);
    return p && p->oauth_native;
}

bool is_local(std::string_view id) {
    const auto* p = preset(id);
    return p && (p->is_local || p->auth == AuthStyle::None);
}

} // 命名空间结束。

auth::AuthHeader resolve(Io io, std::string_view provider_id) {
    // Anthropic：来自 凭据 文件的 OAuth 或 x-api-key。
    if (is_anthropic(provider_id)) {
        if (auto c = auth::load_credentials(io))
            return auth::make_auth_header(*c);
        return auth::AuthHeader{};   // 未登录
    }
    // oauth_native (ChatGPT/Copilot/Kimi)：传输层
    // 从此处读取实时令牌/不需要 头文件。检查了 之前 is_local，因为这些行带有
    // is_local=true（无网络密钥），但它们是 OAuth，而不是本地服务器。
    if (is_oauth_native(provider_id))
        return auth::AuthHeader{auth::ApiKeyHeader{std::string{}}};
    if (is_local(provider_id))
        return auth::AuthHeader{auth::ApiKeyHeader{std::string{}}};

    // 托管 OpenAI-family 密钥 OR 自定义主机：承载密钥，优先级 已保存
    //   provider_keys[id] > env-var 链 > OPENAI_API_KEY。
    std::string key;
    {
        auto s = persistence::load_settings(io);
        if (auto it = s.provider_keys.find(std::string{provider_id});
            it != s.provider_keys.end())
            key = it->second;
    }
    if (key.empty()) {
        if (const auto* p = preset(provider_id))
            for (auto env : p->auth_env)
                if (!(key = env_or_empty(env)).empty()) break;
    }
    if (key.empty()) key = env_or_empty("OPENAI_API_KEY");
    return auth::AuthHeader{auth::ApiKeyHeader{std::move(key)}};
}

bool needs_login(Io io, std::string_view provider_id) {
    if (is_local(provider_id)) return false;         // 无钥匙
    if (is_oauth_native(provider_id)) {
        // 登录当且仅当帐户 注册表 有一个活动帐户（
        // 传输层 读取文件；空 注册表 ⇒ 未登录）。
        return auth::accounts::list_for(io, std::string{provider_id}).empty()
            && auth::accounts::active_label(io, std::string{provider_id}).empty();
    }
    return auth::bearer_token(resolve(io, provider_id)).empty();
}

AddMethod add_method(std::string_view provider_id) {
    // oauth_native 首次：ChatGPT/Copilot/Kimi 带有 is_local=
    // true （它们不需要网络 API 密钥），但它们是 OAuth 提供商，而不是无密钥本地服务器。
    if (is_anthropic(provider_id) || is_oauth_native(provider_id))
        return AddMethod::OAuthDevice;
    if (is_local(provider_id)) return AddMethod::None;
    return AddMethod::ApiKey;   // 托管密钥+自定义主机
}

std::vector<auth::accounts::Account> list(Io io, std::string_view provider_id) {
    return auth::accounts::list_for(io, std::string{provider_id});
}

std::string active_label(Io io, std::string_view provider_id) {
    auto lbl = auth::accounts::active_label(io, std::string{provider_id});
    if (!lbl.empty()) return lbl;
    // 尚未注册帐户 - 从活动信用中获取实时标签。
    return auth::accounts::derive_current_label(io, std::string{provider_id});
}

// 锁 命令：activate/clear_active/add_key 写入 settings.json，
// 这告诉设置 缓存 耗尽（等待）——因此它们采用 `unlocked`。
// 它们的调用者是 副作用 主机的 AccountOp 处理函数 和 CLI 登录路径。
bool activate(Io io, const maya::unlocked& cap, std::string_view provider_id,
              std::string_view label) {
    return auth::accounts::activate(io, cap, std::string{provider_id}, std::string{label});
}

bool remove(Io io, const maya::unlocked& cap, std::string_view provider_id,
            std::string_view label) {
    return auth::accounts::remove(io, cap, std::string{provider_id}, std::string{label});
}

void clear_active(Io io, const maya::unlocked& cap, std::string_view provider_id) {
    const std::string id{provider_id};
    if (is_anthropic(id))          { auth::clear_credentials(io); return; }
    if (id == "chatgpt")           { chatgpt::clear_codex_credentials(cap); return; }
    if (id == "copilot")           { copilot::clear_credentials(io, cap); return; }
    if (id == "kimi")              { kimi::clear_credentials(io, cap); return; }
    // 托管密钥/自定义主机：凭据 IS provider_keys[id]。
    auto s = persistence::load_settings(io);
    s.provider_keys.erase(id);
    s.provider_models.erase(id);
    persistence::save_settings(io, cap, s);
}

bool add_key(Io io, const maya::unlocked& cap, std::string_view provider_id,
             std::string_view key) {
    const std::string id{provider_id};
    auto s = persistence::load_settings(io);
    // 保留任何现有密钥作为可切换帐户 之前 覆盖，
    // 因此 "add another account" 是添加而不是替换。
    if (auto it = s.provider_keys.find(id);
        it != s.provider_keys.end() && !it->second.empty()
        && it->second != std::string{key}) {
        if (auto prior = auth::accounts::derive_current_label(io, id); !prior.empty())
            auth::accounts::snapshot_active(io, cap, id, prior);
    }
    s.provider_keys[id] = std::string{key};
    persistence::save_settings(io, cap, s);
    if (auto lbl = auth::accounts::derive_current_label(io, id); !lbl.empty())
        auth::accounts::snapshot_active(io, cap, id, lbl);
    return true;
}

} // namespace agentty::provider::credentials
