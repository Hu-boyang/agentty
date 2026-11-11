#pragma once
// agentty::provider::credentials —THE中央，
// 提供商-uniform 凭据层。每个需要提供商身份验证的子系统（数据流 接口边界、
// 切换漏斗、帐户管理器 UI）都会经过此处，因此每个提供商都有 一个 解析器和 一个 帐户
// 模型 — Anthropic（OAuth 或密钥）、OpenAI-family 托管密钥、
// ChatGPT / Copilot / Kimi（OAuth），以及用户添加的
// 自定义主机和本地服务器（Ollama / llama.cpp，无身份验证）。
//
// 设计（镜像Anthropic的账号-manager的流量，做了统一）：
//   ·提供商的ACTIVE 凭据通过resolve(provider_id)来解析——
//     存储账户→env-var链→空。没有 per-providerspecial-casing 泄漏给调用者（旧
//     的 resolve_auth_for 的 `anthropic_creds` side-channel
//     消失了；Anthropic 只是
//     凭据存储位于 credentials.json 的提供方。
//   ·当每个提供者的物理秘密LIVES不同时（凭据
//     文件，Settings.provider_keys[规格]，或者本地没有任何
//     内容）——这是 仅 变化的东西，在 backend_for() 内部捕获。
//   ·Multi-account统一：列出/active_label/激活/删除全部
//     关键在于提供者ID，因此相同的帐户-manager UI驱动每个
//     提供者。
//
// 该头文件声明接口边界；该实现位于 src/provider/credentials.cpp 中，
// 并通过帐户 注册表 将存储委托给 auth::* (Anthropic)、
// 提供者 oauth 模块 (chatgpt/copilot/kimi) 和
// Settings.provider_keys（托管密钥 + 自定义主机）。

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "agentty/auth/auth.hpp"
#include "agentty/auth/accounts.hpp"
#include "agentty/util/io.hpp"
#include <maya/runtime.hpp>

namespace agentty::provider::credentials {

// 提供者如何获取NEW账户——驱动账户-manager "add"流。
enum class AddMethod : std::uint8_t {
    ApiKey,       // 粘贴bearer/API密钥(托管OpenAI-family，自定义主机)。
    OAuthDevice,  // browser/device OAuth (Anthropic、ChatGPT、副驾驶、Kimi)。
    None,         // 本地服务器(Ollama/llama.cpp)-没有帐户。
};

// 解析`provider_id`的ACTIVE 凭据的授权头文件。
// 单一解析器，在每个提供商中都是统一的：
//   存储的活动账户(文件OR provider_keys)→env-var链→空。
// 空（对于需要认证的提供商）意味着"not signed in"-呼叫者路由
// 到账户管理器。 `AuthStyle::None` 提供商始终解析为空。
[[nodiscard]] auth::AuthHeader resolve(Io, std::string_view provider_id);

// 当 `provider_id` 需要用户尚未提供的 凭据 时为真（resolve()
// 为空 和 提供者不是无密钥本地服务器）。
// 驱动器 "route to login on switch"。
[[nodiscard]] bool needs_login(Io, std::string_view provider_id);

// 该提供商提供的add-account方法。纯：注册表 查找，没有 凭据 读取，
// 因此 状态更新函数 可能是 调用 它。
[[nodiscard]] AddMethod add_method(std::string_view provider_id);

// ── 账户管理——每个供应商统一使用 ──────────────────────────
// 已保存帐户用于提供商（可以为空）。镜像 auth::accounts，
// 但由规范提供商 ID 键入，并且与 file/key/local 后端保持一致。
[[nodiscard]] std::vector<auth::accounts::Account> list(Io, std::string_view provider_id);

// 活动帐户的标签（"OAuth · me@x"、"key …Ddmj"、"local"，...）。
[[nodiscard]] std::string active_label(Io, std::string_view provider_id);

// 将提供商的活动帐户切换到`label`（将其秘密写入提供
// 商的实时存储+更新注册表）。如果未知则返回 false。
bool activate(Io, const maya::unlocked&, std::string_view provider_id, std::string_view label);

// 删除一个 已保存 帐户。当它是活动的时，升级下一个（或者
// 清除活动的 凭据，如果没有剩余）。
bool remove(Io, const maya::unlocked&, std::string_view provider_id, std::string_view label);

// 完全擦除提供商的LIVE活动凭据（其文件或其provider_keys
// [规格]条目）——完整的标志-out。当最后一个帐户被删除时使用，
// 这样 build_account_list 就无法重新发现并复活它。
void clear_active(Io, const maya::unlocked&, std::string_view provider_id);

// 保留新的-obtained API密钥作为该提供商的活动帐户（
// 快照首先是任何先前的活动帐户，因此它是ADDS，而不是替换）。
bool add_key(Io, const maya::unlocked&, std::string_view provider_id, std::string_view key);

} // namespace agentty::provider::credentials
