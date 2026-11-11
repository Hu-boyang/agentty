// provider::auth_state —参见 auth_state.hpp。

#include "agentty/provider/auth_state.hpp"

#include <cstdlib>

#include "agentty/provider/registry.hpp"
#include "agentty/auth/auth.hpp"
#include "agentty/auth/vault.hpp"
#include "agentty/auth/accounts.hpp"
#include "agentty/provider/chatgpt/responses.hpp"
#include "agentty/provider/copilot/copilot_oauth.hpp"
#include "agentty/provider/kimi/kimi_oauth.hpp"

namespace agentty::provider {

namespace {

bool env_has(std::string_view name) {
    if (name.empty()) return false;
    const char* v = std::getenv(std::string{name}.c_str());
    return v != nullptr && *v != '\0';
}

} // 命名空间结束。

bool provider_is_authed(Io io, const ProviderDescriptor& p,
                        const store::Settings& settings) {
    // 本地/no-auth后端始终可用。
    if (p.is_local || p.auth == AuthStyle::None) return true;

    // 保险库DESCRIPTOR上的分派（该提供者使用哪个
    // 存储系列），但从INJECTED设置回答SettingsKey
    // 问题——该函数是对其输入的纯粹查询（测试注入
    // in-memory设置；保险库自己的访问器读取磁盘，
    // 并且会破坏该密封性和DI规则）。
    switch (auth::vault::of(p.id).kind) {
        case auth::vault::Kind::AnthropicFile:
            if (auth::anthropic_signed_in(io)) return true;
            // 粘贴的-key重叠：provider_keys["anthropic"]。
            return settings.provider_keys.count(std::string{p.id}) != 0;
        case auth::vault::Kind::OAuthFile:
            return auth::vault::signed_in(io, std::string{p.id});
        case auth::vault::Kind::SettingsKey:
        case auth::vault::Kind::None:
            break;
    }
    // 托管密钥：注入设置中的已保存条目，或环境变量。
    if (auto it = settings.provider_keys.find(std::string{p.id});
        it != settings.provider_keys.end() && !it->second.empty())
        return true;
    for (auto env : p.auth_env)
        if (env_has(env)) return true;
    return false;
}

bool provider_is_authed(Io io, std::string_view id, const store::Settings& settings) {
    if (const ProviderDescriptor* p = preset_for(id))
        return provider_is_authed(io, *p, settings);
    // 不是 注册表 预设 ⇒ 已保存 自定义主机。已验证，
    // 当且仅当它具有 provider_keys 条目（无密钥本地主机是 已保存，
    // 具有空值并且仍然可用）。
    return settings.provider_keys.count(std::string{id}) != 0;
}

AuthSource auth_source(Io io, const ProviderDescriptor& p,
                       const store::Settings& settings) {
    if (p.is_local || p.auth == AuthStyle::None) return AuthSource::Local;
    // 与provider_is_authed相同的分派规则：库类型选择存储族；
    // SettingsKey 答案来自 INJECTED 设置。
    switch (auth::vault::of(p.id).kind) {
        case auth::vault::Kind::AnthropicFile:
            if (auth::anthropic_signed_in(io)) return AuthSource::Saved;
            if (auto it = settings.provider_keys.find(std::string{p.id});
                it != settings.provider_keys.end() && !it->second.empty())
                return AuthSource::Saved;
            return AuthSource::None;
        case auth::vault::Kind::OAuthFile:
            return auth::vault::signed_in(io, std::string{p.id})
                 ? AuthSource::Saved : AuthSource::None;
        case auth::vault::Kind::SettingsKey:
        case auth::vault::Kind::None:
            break;
    }
    if (auto it = settings.provider_keys.find(std::string{p.id});
        it != settings.provider_keys.end() && !it->second.empty())
        return AuthSource::Saved;
    // Env-only 凭据：可用，但NOT可在-app中移除。
    for (auto env : p.auth_env)
        if (env_has(env)) return AuthSource::Env;
    return AuthSource::None;
}

AuthSource auth_source(const ProviderDescriptor& p,
                       const store::Settings& settings,
                       const auth::AuthView& view) {
    if (p.is_local || p.auth == AuthStyle::None) return AuthSource::Local;
    const std::string id{p.id};
    switch (auth::vault::of(p.id).kind) {
        case auth::vault::Kind::AnthropicFile:
            // Non-empty，与下面的SettingsKey分支规则相同。
            // count() 将空-string 条目计为凭据，
            // 即无钥匙自定义主机写入的确切空-key 形状。
            if (view.stored(id)) return AuthSource::Saved;
            if (auto it = settings.provider_keys.find(id);
                it != settings.provider_keys.end() && !it->second.empty())
                return AuthSource::Saved;
            return AuthSource::None;
        case auth::vault::Kind::OAuthFile:
            return view.stored(id) ? AuthSource::Saved : AuthSource::None;
        case auth::vault::Kind::SettingsKey:
        case auth::vault::Kind::None:
            break;
    }
    if (auto it = settings.provider_keys.find(id);
        it != settings.provider_keys.end() && !it->second.empty())
        return AuthSource::Saved;
    if (!view.env_var(id).empty()) return AuthSource::Env;
    return AuthSource::None;
}

bool provider_is_authed(std::string_view id, const store::Settings& settings,
                        const auth::AuthView& view) {
    if (const ProviderDescriptor* p = preset_for(id))
        return auth_source(*p, settings, view) != AuthSource::None;
    return settings.provider_keys.count(std::string{id}) != 0;
}

bool signed_in(std::string_view id, const store::Settings& settings,
               const auth::AuthView& view) {
    const std::string pid{id};
    // 自定义主机没有注册表行。保存它（即使使用空密钥，
    // 对于不需要任何密钥的服务器）是使它可用的原因。
    const ProviderDescriptor* row = preset_for(id);
    if (!row && !id.empty()) return settings.provider_keys.count(pid) != 0;
    if (row && (row->is_local || row->auth == AuthStyle::None)) return true;
    switch (auth::vault::of(id).kind) {
        case auth::vault::Kind::AnthropicFile:
        case auth::vault::Kind::OAuthFile:
            return view.stored(pid);
        case auth::vault::Kind::SettingsKey:
        case auth::vault::Kind::None:
            break;
    }
    auto it = settings.provider_keys.find(pid);
    return it != settings.provider_keys.end() && !it->second.empty();
}

auth::AuthView load_auth_view(Io io, const store::Settings& settings) {
    auth::AuthView v;
    v.loaded = true;
    // 为每个提供商读取一个注册表，以及为自定义-host密钥提供的设置。
    // Per-provider 调用 每次重新-read（并解封）两个
    // 文件：提供程序约 1.8 毫秒，每次启动约 25 毫秒。
    const auto reg = auth::accounts::snapshot(io);
    auto fill = [&](const std::string& id, const ProviderDescriptor* p) {
        auth::ProviderAuth a;
        switch (auth::vault::of(id).kind) {
            case auth::vault::Kind::AnthropicFile:
            case auth::vault::Kind::OAuthFile:
                a.stored = auth::vault::signed_in(io, id);
                break;
            default: break;
        }
        if (p)
            for (auto env : p->auth_env)
                if (env_has(env)) { a.env_var = std::string{env}; break; }
        a.accounts       = reg.labels_for(id);
        a.active_account = reg.active_for(id);
        if (a.accounts.empty())
            a.unregistered_label = auth::accounts::derive_current_label(io, id, settings);
        v.providers.emplace(id, std::move(a));
    };
    for (const auto& p : providers()) fill(std::string{p.id}, &p);
    for (const auto& [spec, key] : settings.provider_keys)
        if (!preset_for(spec)) fill(spec, nullptr);
    return v;
}

} // namespace agentty::provider
