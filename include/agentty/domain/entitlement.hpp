#pragma once
// agentty::domain::entitlement — ACCOUNT-scoped 事实，键入正确。
//
// THEORY:docs/IDENTITY_CAPABILITY_ENTITLEMENT.md解释了该头文件实现的三个-layer分割、
// 其退出的故障模式(补偿丢失的关键轴的手动
// 重置钩子)，以及如何确定新事实属于哪一层。
// 如果您要添加事实，请先阅读该内容。
//
// ── 将这个 头文件 层层存在以明确 ─────────────────────
//
// agentty区分了容易混淆的三件事：
//
//   IDENTITY (提供者，型号）——IS是一个什么样的型号。
//   CAPABILITY (提供者、模型）-模型 CAN 做什么。
//   ENTITLEMENT (提供商、账户、模型）-THIS SUBSCRIPTION可以做什么。
//
// 身份和能力是account-blind ON PURPOSE。
// `claude-opus-4-5` 是相同的模型，在您的工作帐户和个人
// 帐户上具有相同的分词器、上下文和工具语法。将帐户折叠到型号标识中将使您的型号选择、
// MRU、智能模式引脚和每次帐户切换时的 per-provider 调用
// 无效 - 显然是错误的，
// 这也是 docs/PROVIDER_HETEROGENEITY.md 将帐户保留在型号行之外的原因。
//
// 但一些事实确实ARE账户-scoped：事件订阅层决定了你可以USE什么，
// 而不是存在什么。 Anthropic 的 1M-context 测试版是典型情况
// - OAuth 令牌没有权利字段，因此了解它的唯一方法就是尝试并被拒绝 (HTTP
// 400
// "long context beta is not yet available for this subscription")。（当前订阅尚未开放长上下文测试功能。）
//
// ── 为什么 注册表 而不是一个鸟 ─────────────────────────────────────
//
// 这以 `Settings::context_1m_blocked` 开始，一个全局布尔值。
// 这就是 account-blind 框中的 account-scoped 真相，
// 代码知道这一点：account-switch 状态更新函数 手动 CLEARED 标志
// 以及注释 "the block was learned FOR the account being dropped"。
// Invalidation-on-switch 是有损的 - Max 帐户（1M
// 有效）和 Pro 帐户（已阻止）之间的 ping-pong re-discovers
// 在每一跳上都是相同的 400，永远，因为答案每次都会被丢弃，而不是记住每
// 个帐户。
//
// 键控存储修复了AND删除特殊情况的问题：在交换
// 机上没有什么可重置的，因为其他帐户的事实从来不妨碍。
// 所学的-effort 注册表已经使用相同的规则（capkey
// -folded键，因此拼写变体不会错过）——键上多一个轴。
//
// ── 密钥形状 ────────────────────────────────────────────────────────────
//
//     "<provider>\x1f<account>\x1f<folded-model>" 型号
//     -scoped 事实 "<provider>\x1f<account>" 帐户-wide 事实
//
// US (0x1f)作为分隔符，而不是'/'：提供商ID是注册表-controlled，
// 但是ACCOUNT LABELS ARE USER-TYPED("work/personal"，
// "acct #2")，并且用户可以键入的分隔符是等待发生的关键-collision错误。
// US 无法输入帐户-label 提示词，并且永远不会出现在型号 ID 中。
//
// 帐户组件是该提供商（auth::accounts::active_label)
// 的注册表的活动标签。未命名/single-account 提供
// 程序解析为 "" — 一个完美的关键组件，
// 仅意味着 "the only account"，因此 single-account
// 用户获得与零迁移相同的行为。

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

#include "agentty/domain/capkey.hpp"

namespace agentty::domain::entitlement {

// 键分隔符——参见头文件注释。故意不打字。
inline constexpr char kSep = '\x1f';

// 权利事实agentty可以得知。事实上有一个枚举器，因此新的枚举器是
// 枚举 条目 + `key_for` 调用 站点，从来没有新的“设置”字段，
// 也从来没有新的手动重置。
//
// 加一：扩展这个枚举，扩展`tag`，并记录在学习的站点上。
// 存储、密钥、持久化 和 account-switch
// 行为都是免费的。
enum class Fact : std::uint8_t {
    // Anthropic的context-1m-2025-08-07测试
    // 版因该事件订阅(HTTP 400)而被拒绝。 Model-scoped：权利
    // 是针对每个型号系列的，未来的帐户可能有权使用一种型号，但不能使用另一种型号。
    Context1M,

    // 帐户的ORGANISATION已禁用图像输入——副驾驶回答
    // "vision is not enabled for this organization"（当前组织未启用图像理解。）
    // （其自己的措辞，读出GitHub CLI 二进制程序）。
    //
    // ACCOUNT-WIDE，因此记录有空的model_id。
    // 这就是它是一个事实而不是 ModelInfo 字段的全部原因：
    // 该模型完全能够看到图像，并且在您登录到授权帐户时就会这样做。
    // 将其存储为模型功能会责怪模型，为每个帐户毒害模型，
    // 并在修复它的开关中幸存下来 - 这正是 account
    // -blind `context_1m_blocked`
    // bool 在 注册表 存在之前所做的事情（请参阅上面的
    // 头文件 注释）。
    VisionOrgPolicy,
};

[[nodiscard]] constexpr std::string_view tag(Fact f) noexcept {
    switch (f) {
        case Fact::Context1M:       return "ctx1m";
        case Fact::VisionOrgPolicy: return "vision_org";
    }
    return "?";
}

// 构建事实的存储密钥。
//
// `model_id` 空 ⇒ ACCOUNT-WIDE 事实（无模型组件）。
// 模型组件是 capkey::norm_model-folded，
// 因此 "3.5" / "3-5" / 案例 变体 解析为 一个 键 - 与 注册表
// 的所有其他功能使用的规则相同，这使得拼写 -based 在结构上不可能错过。
//
// `account`空是合法且有意义的："the only account"。
[[nodiscard]] inline std::string key_for(Fact f,
                                         std::string_view provider,
                                         std::string_view account,
                                         std::string_view model_id = {}) {
    std::string k;
    k.reserve(tag(f).size() + provider.size() + account.size()
              + model_id.size() + 3);
    k += tag(f);
    k += kSep;
    k += provider;
    k += kSep;
    k += account;
    if (!model_id.empty()) {
        k += kSep;
        k += capkey::norm_model(model_id);
    }
    return k;
}

// 持久存储：密钥→被阻止。仅记录 NEGATIVE 事实 -
// "this subscription may not" -
// 因为默认情况下权利是允许的，而我们只能通过拒绝来学习。
// 缺少密钥意味着 "not known to be blocked"，
// 这对于新帐户、全新安装和从不拒绝任何内容的提供商来说正是正确的默认值。
using Store = std::map<std::string, bool>;

[[nodiscard]] inline bool blocked(const Store& s, Fact f,
                                  std::string_view provider,
                                  std::string_view account,
                                  std::string_view model_id = {}) {
    const auto it = s.find(key_for(f, provider, account, model_id));
    return it != s.end() && it->second;
}

// 记录习得的拒绝。当这是 新 信息时返回 true（
// 因此调用者可以在没有任何更改时跳过设置写入）。
inline bool record_blocked(Store& s, Fact f,
                           std::string_view provider,
                           std::string_view account,
                           std::string_view model_id = {}) {
    auto [it, inserted] = s.insert_or_assign(
        key_for(f, provider, account, model_id), true);
    return inserted;
}

// 放弃每一个事实（提供商、帐户）——SIGN-OUT钩子，
// 不是账户-switch钩。切换账户必须NOT忘记：这
// 就是键控的全部意义。不过要签个账号OUT意味着它的事实
// 永远无法再被查阅，并且以后的重述-login可能会登陆不同的订阅级别，
// 因此石板确实已经过时了。
inline void forget_account(Store& s, std::string_view provider,
                           std::string_view account) {
    std::string prefix;
    prefix += kSep;
    prefix += provider;
    prefix += kSep;
    prefix += account;
    std::erase_if(s, [&](const auto& kv) {
        // 精确匹配"<tag>\x1f<provider>\x1f<account>"或
        // 作为型号-scoped密钥("…\x1f<model>")的前缀。锚定在分隔符上，
        // 以便帐户 "work" 永远不会与帐户 "work2" 匹配。
        const std::string& k = kv.first;
        const auto at = k.find(prefix);
        if (at == std::string::npos) return false;
        const auto end = at + prefix.size();
        return end == k.size() || k[end] == kSep;
    });
}

} // namespace agentty::domain::entitlement
