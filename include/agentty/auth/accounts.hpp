#pragma once
// agentty::auth::accounts-命名为每个-provider
// 凭据槽，因此用户可以在同一提供商上持有MULTIPLE帐户并在
// -app中完全在它们之间切换（切勿仅仅为了改变他们的身份而花钱购买re
// -login）。
//
// 设计：agentty已经为每个提供商拥有一个"active" 凭据商店
//   -Anthropic:credentials.json(auth::save/load/clear_credentials)
//   -ChatGPT:Codex加密存储(provider::chatgpt::*_codex_*)
//   -OpenAI-fam:Settings.provider_keys[端点-label]
//
// 该模块对记录提供商的凭据的名为SNAPSHOTS的REGISTRY
// (accounts.json，
// 使用与credentials.json相同的crypt::seal机器静态加密)
// 进行分层。它永远不会替换 active-store 路径 - 切换帐户
// 只需将 快照 复制回活动存储并重新-installs 实时身份验证 头文件。
// 这使每个现有的 resolve()/refresh() 代码路径
// 保持不变，并且完全向后兼容：仅拥有旧版单个 凭据 的用户在第一次读
// 取时只能看到从中合成的一个帐户 ("default")。

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "agentty/util/io.hpp"
#include "agentty/util/lock_levels.hpp"

#include <maya/runtime.hpp>

namespace agentty::store { struct Settings; }

namespace agentty::auth::accounts {

// 一个已保存账户。 `secret` 是提供者-specific
// 凭据 blob，其序列化与该提供者的活动存储完全一样：
//   - "anthropic" → credentials.json 正文 (method/access_token/…)
//   -"chatgpt" → Codex 凭据 JSON
//   - openai ids → 原始 API 密钥字符串
// 注册表对其进行不透明处理；只有 account_switch.cpp 中的
// provider-specific 适配器知道如何安装它。
struct Account {
    std::string  provider;      // 规范提供商 ID（"anthropic"、"chatgpt"、"openai"，...）
    std::string  label;         // 用户-facing姓名（"work"、"personal"、电子邮件、……）
    std::string  secret;        // 不透明 凭据 斑点（见上文）
    std::int64_t saved_at_ms = 0;
};

// 所有提供商的每个账户，最新的-saved在前。读取 accounts.json；
// 如果没有 已保存，则返回 {}。从不抛出。
[[nodiscard]] std::vector<Account> list(Io);

// 仅一个提供商的账户。
[[nodiscard]] std::vector<Account> list_for(Io, const std::string& provider);

// 当前针对 `provider` 标记为活动的帐户的标签（
// 如果 注册表 知道的话）。当提供商没有注册帐户时为空。
[[nodiscard]] std::string active_label(Io, const std::string& provider);

// 整个注册表从一读。对于立即询问每个提供商的调用者（启动
// 身份验证视图）：list_for / active_label
// 再次读取并解封文件。
struct Snapshot {
    std::vector<Account>                            all;     // 最新第一
    std::vector<std::pair<std::string,std::string>> active;  // 提供商→标签
    [[nodiscard]] std::vector<std::string> labels_for(const std::string& provider) const;
    [[nodiscard]] std::string              active_for(const std::string& provider) const;
};
[[nodiscard]] Snapshot snapshot(Io);

// 用新的秘密插入或更新（提供者，标签）槽，将其标记为对于该
// 提供者有效，并且持续。成功登录后立即使用，以捕获名称下的
// just-authenticated 凭据。
bool upsert(Io, const maya::below<lock_levels::file_lane>&, const std::string& provider,
            const std::string& label, const std::string& secret);

// 获取特定槽的秘密。缺席时无效。
[[nodiscard]] std::optional<Account> get(Io, const std::string& provider,
                                         const std::string& label);

// 移除槽。如果它是活动帐户，则该提供商的最新剩余帐户（
// 如果有）将变为活动帐户。如果槽已被删除，则返回 true。
bool remove(Io, const maya::below<lock_levels::file_lane>&, const std::string& provider,
            const std::string& label);

// 记录哪个标签对于提供商来说是活动的（不改变秘密）。
bool set_active(Io, const maya::below<lock_levels::file_lane>&, const std::string& provider,
                const std::string& label);

// 注册表文件(config_dir()/accounts.json)。暴露进行测试。
[[nodiscard]] std::string path();

// ── 高 高 点 -level 百分比 -provider 适配器( 4) account_switch.cpp) ────────────────
// 这些将不透明的注册表桥接至每个提供商的ACTIVE 凭据存储。
// 他们知道如何将当前实时 凭据 读取为秘密 blob，
// 以及如何将 已保存 blob 安装回实时 凭据。

// 读取提供商的当前活动的凭据并将其存储在`label`下的注册表中
// （将其标记为活动的）。成功登录后立即显示 调用，因此通过名称捕获
// 了 just-authenticated 帐户。当提供者没有实时
// 凭据 到 快照 时返回 false。
bool snapshot_active(Io, const maya::below<lock_levels::file_lane>&, const std::string& provider,
                     const std::string& label);

// 将已保存(提供商、标签）凭据安装为提供商的活动凭据并在注册表中将其标记为活动。
// 不是否接触了 -process 中的实时验证 头文件 — 调用者
// (状态更新函数) 通过 make_auth_header 回复了
// -installs，因此该模块不受 runtime/app 依赖的影响。
// 当插槽不存在或安装失败时返回 false。
//
// TOKEN-ROTATION SAFETY:在覆盖活动存储之前，
// CURRENT活动凭据被重新-snapshotted插入到从(提供商
// 的注册表-active标签)切换到AWAY的账户的注册表槽中。
// 轮换刷新令牌（Anthropic在EVERY刷新时
// 轮换）的提供商使旧令牌服务器-side无效，因此在登录时
// 获取的注册表 快照在实时商店刷新时变为过期；
// 稍后切换回它会出现死刷新令牌，并且每次刷新都会被拒绝（
// invalid_grant）。 Re-snapshotting
// 即将推出，让每个老虎机都像直播的最后一刻一样新鲜。
// 采用`unlocked`:自定义-host开关重写settings.json(阻塞)。
bool activate(Io, const maya::unlocked&, const std::string& provider,
              const std::string& label);

// 用于当前存在于提供商的活动商店中但尚未注册的任何 凭据 的 Best
// -effort 人类标签（遗留 single-login 情况）。
// 未登录时返回 ""。用于在帐户 选择面板 首次打开时将 -existing 前
// 的 auto-register 登录为 "default"。
[[nodiscard]] std::string derive_current_label(Io, const std::string& provider);
// 同样，对于呼叫者已经已加载的设置（自定义-host键存在于那里），
// 因此提供商的循环不会每次都re-read settings.json。
[[nodiscard]] std::string derive_current_label(Io, const std::string& provider,
                                               const store::Settings& settings);

} // namespace agentty::auth::accounts
