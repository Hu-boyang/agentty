#pragma once
// provider::auth_state —
// "can I switch to this provider RIGHT NOW without signing in?" 作为 一个 在
// 注册表 上的可重用谓词，概括了提供者 选择面板 hand-rolled 内嵌的 per-provider 认证徽章。
//
// 融合的cross-provider模型选择面板需要将每个提供者分成"authed → show its models"
// 与"un-authed → show a single sign-in offer"。
// 每个提供商进行不同的身份验证（磁盘上的 Anthropic OAuth-or-key，
// -process OAuth 谓词中的 ChatGPT/Copilot/Kimi，
// 托管来自 env 或 Settings.provider_keys 的承载密钥
// OpenAI-family，本地后端无），因此单个 分派 点可防止 选择面板、
// 徽章和登录门漂移 — 注册表-as-SSOT 纪律。

#include <string>
#include <string_view>

#include "agentty/store/store.hpp"   // store::Settings
#include "agentty/domain/auth_view.hpp" // auth::AuthView 认证视图。
#include "agentty/util/io.hpp"

namespace agentty::provider {

struct ProviderDescriptor;   // registry.hpp

// 当回合可以立即针对 `p` 进行流式传输时为真：磁盘上的 OAuth 信用/in-process，
// 或可从 env 或 已保存 provider_keys 解析的 API 密钥，
// 或本地（no-auth）后端。 `settings` 提供 已保存 定制 -host / API
// -key 地图 (provider_keys)。 Network-free 且便宜（
// OAuth 谓词是 stat-cached）。
[[nodiscard]] bool provider_is_authed(Io, const ProviderDescriptor& p,
                                      const store::Settings& settings);

// 由提供商id键入的便利重载（在注册表中查找描述符）。未知 ID
// ⇒ 被视为 已保存 自定义主机：已验证当且仅当它具有
// provider_keys 条目（无密钥本地主机算作已验证）。
[[nodiscard]] bool provider_is_authed(Io, std::string_view id,
                                      const store::Settings& settings);

// WHERE提供者的凭据来自——让选择面板告诉用户"signed in"
// 来自"key from env"(后者不能在-app中注销，
// 所以^D没有-op，否则看起来会损坏)。
enum class AuthSource {
    None,        // 未授权
    Saved,       // 磁盘上的 OAuth/in-process，或粘贴的 provider_keys 条目
    Env,         // 从环境变量中解析了 仅
    Local,       // no-auth后端(Ollama，自定义http主机)
};
[[nodiscard]] AuthSource auth_source(Io, const ProviderDescriptor& p,
                                     const store::Settings& settings);

// 仅来自Model的相同答案：`view`携带存在于设置记录
// 之外的内容（凭据存储、环境、帐户），因此这些不读取文件且不读取环境。
// 状态更新函数 和视图使用这些。
[[nodiscard]] AuthSource auth_source(const ProviderDescriptor& p,
                                     const store::Settings& settings,
                                     const auth::AuthView& view);
[[nodiscard]] bool provider_is_authed(std::string_view id,
                                      const store::Settings& settings,
                                      const auth::AuthView& view);
// `id`的活动凭据是否存在(保管库的"signed in")？对于密钥提供者来说，
// 这意味着非 -empty 已保存 密钥；对于 OAuth，是存储的令牌。
[[nodiscard]] bool signed_in(std::string_view id,
                             const store::Settings& settings,
                             const auth::AuthView& view);

// 从磁盘和环境构建视图。仅 Host 侧。
[[nodiscard]] auth::AuthView load_auth_view(Io, const store::Settings& settings);

} // namespace agentty::provider
