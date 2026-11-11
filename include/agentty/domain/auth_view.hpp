#pragma once
// AuthView
// -Model对存在于设置记录之外的凭据的了解。
//
// 提供商的sign-in状态分布在credentials.json、
// 提供商-owned OAuth令牌文件、环境变量和帐户注册表上。
// 状态更新函数和视图用于直接询问磁盘，这使得更新不纯粹并且每个帧重新
// -statted那些文件。
//
// 主机在启动时以及在每次改变凭据的副作用之后将它们读入该值(
// LoadAuthView 副作用，回复AuthViewLoaded)。
// 粘贴到设置中的键在这里是NOT：那些是m.d.persisted.provider_keys。
// provider::auth_source(p，设置，视图)将两者结合起来。

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace agentty::auth {

struct ProviderAuth {
    // 该提供商自己的商店中的凭据：Anthropic的credentials.json，
    // ChatGPT/Copilot/Kimi.的令牌文件
    bool                     stored = false;
    // 来自设置的提供者的auth_env的第一个环境变量，如果有的话。
    std::string              env_var;
    // 该提供商的账户注册表，最新的在前，以及哪个账户是
    // 活动的。
    std::vector<std::string> accounts;
    std::string              active_account;
    // 凭据尚未在注册表中（传统单次登录）。
    // 帐户列表打开时将其注册在此标签下。
    std::string              unregistered_label;

    bool operator==(const ProviderAuth&) const = default;
};

struct AuthView {
    std::map<std::string, ProviderAuth, std::less<>> providers;
    // 假直到主机有已加载一次。
    bool loaded = false;

    [[nodiscard]] const ProviderAuth* find(std::string_view id) const {
        auto it = providers.find(id);
        return it == providers.end() ? nullptr : &it->second;
    }
    [[nodiscard]] bool stored(std::string_view id) const {
        const auto* p = find(id);
        return p && p->stored;
    }
    [[nodiscard]] std::string env_var(std::string_view id) const {
        const auto* p = find(id);
        return p ? p->env_var : std::string{};
    }
    [[nodiscard]] std::string active_account(std::string_view id) const {
        const auto* p = find(id);
        return p ? p->active_account : std::string{};
    }
    bool operator==(const AuthView&) const = default;
};

} // namespace agentty::auth
