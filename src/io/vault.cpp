// agentty::auth::vault-ONE表回答每个-provider认证问题。
// 请参阅 vault.hpp 了解设计原理。

#include "agentty/auth/vault.hpp"

#include "agentty/auth/accounts.hpp"          // derive_current_label
#include "agentty/io/persistence.hpp"         // load/save_settings
#include "agentty/provider/credentials.hpp"   // 中央resolve()
#include "agentty/provider/chatgpt/codex_oauth.hpp"
#include "agentty/provider/chatgpt/responses.hpp"
#include "agentty/provider/copilot/copilot_oauth.hpp"
#include "agentty/provider/kimi/kimi_oauth.hpp"

namespace agentty::auth::vault {

namespace {

// ── 共同行为 ────────────────────────────────────────────────────
// 中央凭据解析器已经针对每个提供者正确分派——每个描述符都委托给它，
// 因此WIRE故事保持单一-source（
// 保险库在其周围添加storage/lifecycle故事，它不分叉认证）。
auth::AuthHeader resolve_central(Io io, const std::string& p) {
    return provider::credentials::resolve(io, p);
}

std::string label_from_registry(Io io, const std::string& p) {
    return auth::accounts::derive_current_label(io, p);
}

// SettingsKey家族(托管API密钥AND定制主机):凭据是
// Settings.provider_keys[p]; signed-in iff 存在
// 非 -empty 条目（自定义 LOCAL 主机 保存 为空值并且可以无密钥使用，
// 但那是 Kind::None 领域 - provider_is_authed 处理 选择面板
// 的细微差别；金库的问题是 "is a secret SAVED here"）。
bool key_signed_in(Io io, const std::string& p) {
    auto s = persistence::load_settings(io);
    auto it = s.provider_keys.find(p);
    return it != s.provider_keys.end() && !it->second.empty();
}
void key_clear(Io io, const maya::unlocked& cap, const std::string& p) {
    auto s = persistence::load_settings(io);
    if (s.provider_keys.erase(p) > 0) persistence::save_settings(io, cap, s);
}

void noop_after(Io, const maya::below<maya::leaf>&, const std::string&) {}

// ── 人数 -kind 行数 ────────────────────────────────────────────────────────
constexpr Desc kAnthropic{
    .id           = "anthropic",
    .kind         = Kind::AnthropicFile,
    .is_signed_in = [](Io io, const std::string&) { return auth::anthropic_signed_in(io); },
    .clear        = [](Io io, const maya::unlocked&, const std::string&) {
        (void)auth::clear_credentials(io);
    },
    .resolve      = resolve_central,
    .current_label = label_from_registry,
    .after_activate = [](Io io, const maya::below<maya::leaf>&, const std::string&) {
        // Re-seal进入密钥库，因此下一个resolve()读
        // 取切换的-to账户，而不是过期 缓存。
        if (auto c = auth::load_credentials(io)) auth::save_credentials(io, *c);
    },
};

constexpr Desc kChatGpt{
    .id           = "chatgpt",
    .kind         = Kind::OAuthFile,
    .is_signed_in = [](Io /*io*/, const std::string&) {
        return provider::chatgpt::responses_available();
    },
    .clear = [](Io /*io*/, const maya::unlocked& cap, const std::string&) {
        (void)provider::chatgpt::clear_codex_credentials(cap);
    },
    .resolve      = resolve_central,   // 空：传输层拥有令牌
    .current_label = label_from_registry,
    .after_activate = [](Io /*io*/, const maya::below<maya::leaf>& cap, const std::string&) {
        if (auto c = provider::chatgpt::load_codex_credentials())
            provider::chatgpt::save_codex_credentials(cap, *c);
    },
};

constexpr Desc kCopilot{
    .id           = "copilot",
    .kind         = Kind::OAuthFile,
    .is_signed_in = [](Io /*io*/, const std::string&) {
        return provider::copilot::signed_in();
    },
    .clear = [](Io io, const maya::unlocked& cap, const std::string&) {
        (void)provider::copilot::clear_credentials(io, cap);
    },
    .resolve      = resolve_central,
    .current_label = label_from_registry,
    .after_activate = [](Io /*io*/, const maya::below<maya::leaf>&, const std::string&) {
        provider::copilot::invalidate_cached_token();
    },
};

constexpr Desc kKimi{
    .id           = "kimi",
    .kind         = Kind::OAuthFile,
    .is_signed_in = [](Io /*io*/, const std::string&) {
        return provider::kimi::signed_in();
    },
    .clear = [](Io io, const maya::unlocked& cap, const std::string&) {
        (void)provider::kimi::clear_credentials(io, cap);
    },
    .resolve      = resolve_central,
    .current_label = label_from_registry,
    .after_activate = [](Io /*io*/, const maya::below<maya::leaf>&, const std::string&) {
        provider::kimi::invalidate_cached_token();
    },
};

// EVERY托管-key预设(groq/mistral/openrouter/…)和每个自定义主机：
// 一个共享行——所传递的提供者id选择密钥槽。
constexpr Desc kSettingsKey{
    .id           = "",                 // 合成的； id 是参数
    .kind         = Kind::SettingsKey,
    .is_signed_in = key_signed_in,
    .clear        = key_clear,
    .resolve      = resolve_central,
    .current_label = label_from_registry,
    .after_activate = noop_after,
};

} // 命名空间结束。

const Desc& of(std::string_view provider_id) {
    if (provider_id.empty() || provider_id == "anthropic") return kAnthropic;
    if (provider_id == "chatgpt" || provider_id == "codex-cli") return kChatGpt;
    if (provider_id == "copilot") return kCopilot;
    if (provider_id == "kimi")    return kKimi;
    return kSettingsKey;   // 托管密钥+自定义主机，统一
}

} // namespace agentty::auth::vault
