// entitlement_test — ACCOUNT-scoped 事实, 正确记录。
//
// 此层的bug 已退出 : `Settings::context_1m_blocked`
// 是一个全球的单鸟 持有一个 ACCOUNT-scoped 事实
// Anthropic's 1M-context β是一个 事件订阅 因为盒子
// 是账户 -blind ,账户 -switch 状态更新函数 不得不这样
// CLEAR 意思是说, -ponging 标题(最大)和无标题(Pro)账户
// 之间重新 -discovered 同样的 HTTP 每跳400,永远,
// 因为答案被丢掉,而不是记住每个账户。
//
// 这些测试将使键正确性定点 :
//   • 事实是逐一的(提供者、帐户、模型),绝不跨越任何轴线泄露
//   • AWAY和BACK开关记起——Ping-pong案,直接说明
//   • 账户部分为a USER-TYPED 字符串,所以键形建筑必须是
//     注射量 -proof (分离者之所以如此) US ,没有 '/')
//   • 忘记了发生的情况 REMOVAL 只有手术

#include "agtest.hpp"

#include "agentty/domain/entitlement.hpp"

#include <string>

using namespace agentty;
using ent = domain::entitlement::Fact;
namespace E = domain::entitlement;

TEST_CASE("entitlement: absent ⇒ not blocked (permissive default)") {
    E::Store s;
    // 新的安装、新的账户和从未拒绝任何事物的提供者必须
    // 都采取同样的行为:任何事物都不得阻挡,
    // 除非被拒绝者教导我们别的做法。
    CHECK(!E::blocked(s, ent::Context1M, "anthropic", "work", "claude-opus-4-5"));
    CHECK(!E::blocked(s, ent::Context1M, "anthropic", "", "claude-opus-4-5"));
}

TEST_CASE("entitlement: a fact is scoped to its account, not the provider") {
    E::Store s;
    E::record_blocked(s, ent::Context1M, "anthropic", "pro", "claude-opus-4-5");

    CHECK(E::blocked(s, ent::Context1M, "anthropic", "pro", "claude-opus-4-5"));
    // 电话 OTHER 同一提供者的账户没有受到
    // 影响——这是整个问题。 `true` 并
    // 剥离了1行 和马克斯账户 有权使用它们。
    CHECK(!E::blocked(s, ent::Context1M, "anthropic", "max", "claude-opus-4-5"));
    // ...不同的供应商和不同的模型也一样
    CHECK(!E::blocked(s, ent::Context1M, "openai", "pro", "claude-opus-4-5"));
    CHECK(!E::blocked(s, ent::Context1M, "anthropic", "pro", "claude-sonnet-4-6"));
}

TEST_CASE("entitlement: switch away and back REMEMBERS (the ping-pong bug)") {
    E::Store s;
    // 打开Pro账户的1号:传输拒绝β,我们学习.
    REQUIRE(E::record_blocked(s, ent::Context1M, "anthropic", "pro",
                              "claude-opus-4-5"));
    // 切换到Max——没有被清除(再也没有重设的钩子存在),
    // Max被正确解锁.
    CHECK(!E::blocked(s, ent::Context1M, "anthropic", "max", "claude-opus-4-5"));
    // 切换 BACK 改为专业人员 -fix 这是干净的,
    // 用户又吃了400+ 回退 往返,现在人们记得了
    CHECK(E::blocked(s, ent::Context1M, "anthropic", "pro", "claude-opus-4-5"));
}

TEST_CASE("entitlement: model component is capkey-folded") {
    E::Store s;
    // 拼写 变体 备注 一个 模式必须决心 一个 关键——与所学的
    // 一样的学科 -effort 注册表 使用。没有这个,
    // 学习块在 "mistral-medium-3.5" 将会错过
    // "mistral-medium-3-5" 用户会吃两次拒绝。
    E::record_blocked(s, ent::Context1M, "anthropic", "a", "Claude-Opus-4.5");
    CHECK(E::blocked(s, ent::Context1M, "anthropic", "a", "claude-opus-4-5"));
    CHECK(E::blocked(s, ent::Context1M, "anthropic", "a", "CLAUDE_OPUS_4_5"));
}

TEST_CASE("entitlement: account-wide facts are distinct from model-scoped") {
    E::Store s;
    E::record_blocked(s, ent::Context1M, "anthropic", "work");   // 无型号
    CHECK(E::blocked(s, ent::Context1M, "anthropic", "work"));
    // 账户 -wide 事实不得 回答一个模型 -scoped
    // 问题(反之亦然)——它们是不同含义的不同的键.
    CHECK(!E::blocked(s, ent::Context1M, "anthropic", "work", "claude-opus-4-5"));
}

TEST_CASE("entitlement: user-typed account labels cannot forge a key") {
    E::Store s;
    // 账户标签为 USER-TYPED ("work/personal",
    // "acct #2" ),所以用户可以输入的分隔符会让一个账户的
    // 标签溢出到另一个密钥的模型组件中。分隔符是 US ,
    // 不能是“零”或“零” 带类型的 输入标签 提示词.
    E::record_blocked(s, ent::Context1M, "anthropic", "work/claude-opus-4-5");
    // 刀锋 -bearing 标签是它的 自身 账户, 并且没有意外创建
    // 模型 -scoped 事实说明 "work".
    CHECK(E::blocked(s, ent::Context1M, "anthropic", "work/claude-opus-4-5"));
    CHECK(!E::blocked(s, ent::Context1M, "anthropic", "work", "claude-opus-4-5"));
    CHECK(!E::blocked(s, ent::Context1M, "anthropic", "work"));
}

TEST_CASE("entitlement: forget_account is surgical and prefix-anchored") {
    E::Store s;
    E::record_blocked(s, ent::Context1M, "anthropic", "work",  "claude-opus-4-5");
    E::record_blocked(s, ent::Context1M, "anthropic", "work",  "claude-sonnet-4-6");
    E::record_blocked(s, ent::Context1M, "anthropic", "work2", "claude-opus-4-5");
    E::record_blocked(s, ent::Context1M, "anthropic", "home",  "claude-opus-4-5");
    E::record_blocked(s, ent::Context1M, "openai",    "work",  "gpt-5");

    E::forget_account(s, "anthropic", "work");

    // 都一样 "work" 模式 -scoped 事实已经不复存在了...
    CHECK(!E::blocked(s, ent::Context1M, "anthropic", "work", "claude-opus-4-5"));
    CHECK(!E::blocked(s, ent::Context1M, "anthropic", "work", "claude-sonnet-4-6"));
    // . 不过... "work2" 不得 被天真前缀匹配所覆盖(
    // 原因) forget_account 锁定在分隔符上,或者另一个账户,
    // 或者在另一个提供者上设置相同的标签.
    CHECK(E::blocked(s, ent::Context1M, "anthropic", "work2", "claude-opus-4-5"));
    CHECK(E::blocked(s, ent::Context1M, "anthropic", "home",  "claude-opus-4-5"));
    CHECK(E::blocked(s, ent::Context1M, "openai",    "work",  "gpt-5"));
}

TEST_CASE("entitlement: record_blocked reports novelty") {
    E::Store s;
    // 呼叫者在没有任何变化时跳过一个设置写入——拒绝,每转一次都重复,
    // 不能每转一次重写settings.json.
    CHECK(E::record_blocked(s, ent::Context1M, "anthropic", "a", "m"));
    CHECK(!E::record_blocked(s, ent::Context1M, "anthropic", "a", "m"));
}

TEST_CASE("entitlement: the empty account label is a legitimate key") {
    E::Store s;
    // 单人 -account 提供者没有名称账户; "" 表示
    // "the only account"。这就是为什么移民是单身的。
    // -account 用户: 用户在 "" 继续工作
    E::record_blocked(s, ent::Context1M, "ollama", "", "llama3");
    CHECK(E::blocked(s, ent::Context1M, "ollama", "", "llama3"));
    CHECK(!E::blocked(s, ent::Context1M, "ollama", "named", "llama3"));
}
