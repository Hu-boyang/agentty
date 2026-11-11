#pragma once
// agentty::tools::policy — 权限 的 唯一 事实来源。
// 地图(副作用×个人资料)→决定。每次门控都检查运行时 调用这个；
// 每个工具都会注册一个 EffectSet 并且门控相同。
// 没有定制的 per-tool lambda，没有惊喜。
//
// 该策略有意是纯constexpr函数：它没有状态，没有IO，没有模型依赖性—
// —易于阅读top-to-bottom，易于彻底测试，
// 易于推理（
// 什么情况下模型可以不经询问发起网络调用？
//

#include "agentty/domain/profile.hpp"
#include "agentty/tool/effects.hpp"

namespace agentty::tools::policy {

enum class Decision : std::uint8_t {
    Allow,    // 无条件运行
    Prompt,   // 显示权限卡，等待用户
};

// 全表：
//
//   Profile::Write→允许一切（自主模式）。
//   Profile::Ask → 提示词 用于执行/WriteFs/网络。
//                       允许ReadFs/Pure(read
//                       -only检查是代理循环的面包-and-butter
//                       ——提示per-read使得Ask不可用)。
//   Profile::Minimal → 提示词 用于涉及外部世界的一切
//                       （Exec、WriteFs、Net、ReadFs)。
//                       仅允许纯的。
//
// "Exec is more dangerous than WriteFs"是类型-theoretic的权利要求：
// `bash`让模型创作副作用，因此它支配我们
// 已经门控的任何单个文件系统突变。
[[nodiscard]] constexpr Decision permission(EffectSet effects,
                                            Profile profile) noexcept {
    if (profile == Profile::Write) return Decision::Allow;

    if (effects.has(Effect::Exec))    return Decision::Prompt;
    if (effects.has(Effect::WriteFs)) return Decision::Prompt;
    if (effects.has(Effect::Net))     return Decision::Prompt;

    if (profile == Profile::Minimal) {
        if (effects.has(Effect::ReadFs)) return Decision::Prompt;
        return Decision::Allow;       // 纯净
    }
    // Profile::Ask:ReadFs和Pure可信。
    return Decision::Allow;
}

// 友好的one-liner解释WHY工具正在被门控。
// 呈现为 权限 卡的 正文，以便用户知道他们
// 被要求扩展什么信任。
[[nodiscard]] inline std::string_view reason(EffectSet effects, Profile p) noexcept {
    if (p == Profile::Write)          return "auto-approved (Write profile)";
    if (effects.has(Effect::Exec))    return "wants to run an arbitrary subprocess";
    if (effects.has(Effect::WriteFs)) return "will modify files on disk";
    if (effects.has(Effect::Net))     return "will reach the network";
    if (p == Profile::Minimal && effects.has(Effect::ReadFs))
                                      return "wants to read from disk (Minimal profile)";
    return "no side effects (auto-approved)";
}

// ── 编译-time 信任矩阵的证明 ──────────────────────────────
// 整个保单是constexpr，所以我们可以在编译时间进行验证。
// EffectSet是4-bit位集(ReadFs/WriteFs/Net/Exec)——
// 16个不同的集——并且有3个配置文件，因此存储单元空间正好是16×3=48
// 存储单元。下面的块涵盖了其中的 全部：一个单独的 `expected_decision()`
// 函数以 prose-style Short-circuit 形式对策略规范进行编码，
// 并且一个详尽的 constexpr 循环在每个 存储单元 上检查
// `permission(e, p) == expected_decision(e, p)`。
// 如果有人以破坏任何 存储单元 的方式调整表，或者在不更新规范的情况下
// 扩展 EffectSet，那么 static_assert 就会被触发；
// 无需试运行。
//
// （此区块早些时候列出了约 20 个点 -checks，对抗名为 EffectSets。
// 很好地记录了意图，但仅验证了 48 个 存储单元 中的 ~10 个；
// four-effect 组合和几个 副作用-only-with-
// 非 -matching-profile 案例在结构上是由政策函数
// 的 Short-circuit 形状暗示的，而不是实际断言的。
// 下面的循环弥补了这一差距。）
namespace proofs {

inline constexpr EffectSet kPure  {};
inline constexpr EffectSet kRead  {Effect::ReadFs};
inline constexpr EffectSet kWrite {Effect::WriteFs};
inline constexpr EffectSet kNet   {Effect::Net};
inline constexpr EffectSet kExec  {Effect::Exec};
inline constexpr EffectSet kEdit  {Effect::ReadFs, Effect::WriteFs};

// 策略的独立re-statement。 Lives 与
// `permission()` 是分开的，因此两者被写为同一规则的
// 并行表达式 - 对任一 -handed 的更改都会引发下面的
// 详尽检查。添加 副作用 或 Profile 臂：延伸 两者 侧。
[[nodiscard]] constexpr Decision expected_decision(EffectSet e, Profile p) noexcept {
    // Profile::Write是完全自主的：允许每个副作用，
    // 从来不显示提示词。
    if (p == Profile::Write) return Decision::Allow;

    // 任何改变状态、运行代码或在任何非
    // -Write配置文件下访问网络提示词的东西。
    // Exec 是最大的能力——一个拥有 提示词 的工具，
    // 无论它还携带什么。
    if (e.has(Effect::Exec))    return Decision::Prompt;
    if (e.has(Effect::WriteFs)) return Decision::Prompt;
    if (e.has(Effect::Net))     return Decision::Prompt;

    // Profile::Minimal 提示词甚至用于read-only检查——仅纯工具
    // auto-allow。 Profile::Ask 信任 ReadFs，因此代理循环
    // 的 read-file / grep / glob 在每个步骤中都不会 提示词。
    if (p == Profile::Minimal && e.has(Effect::ReadFs)) return Decision::Prompt;
    return Decision::Allow;
}

// 每隔(EffectSet，Profile)存储单元行走并要求策略符合规范。
// 4 副作用 位 × 3 个配置文件 = 48 存储单元，全部覆盖。
[[nodiscard]] constexpr bool exhaustive_matches_spec() noexcept {
    constexpr Profile kProfiles[] = {
        Profile::Write, Profile::Ask, Profile::Minimal,
    };
    constexpr std::uint8_t kAllEffectBits =
        static_cast<std::uint8_t>(Effect::ReadFs)
        | static_cast<std::uint8_t>(Effect::WriteFs)
        | static_cast<std::uint8_t>(Effect::Net)
        | static_cast<std::uint8_t>(Effect::Exec);
    for (std::uint16_t bits = 0; bits <= kAllEffectBits; ++bits) {
        // 跳过包括未分配位的组合——如果副作用 枚举增长到
        // 非-contiguous值，则保持扫描诚实；
        // 下面的 static_assert 引脚位集宽度。
        if ((bits & ~static_cast<std::uint16_t>(kAllEffectBits)) != 0) continue;
        EffectSet e{static_cast<std::uint8_t>(bits)};
        for (Profile p : kProfiles) {
            if (permission(e, p) != expected_decision(e, p)) return false;
        }
    }
    return true;
}
static_assert(exhaustive_matches_spec(),
              "policy::permission disagrees with expected_decision on at "
              "least one cell — update both sides together.");

// 固定位集宽度：如果添加第五个 副作用 标签，则会触发并提醒您更新
// kAllEffectBits + expected_decision()。
// 如果没有它，上面的循环将不会默默地扫描新位。
static_assert(static_cast<std::uint8_t>(Effect::Exec) == (1u << 3),
              "Effect bit assignment changed — review exhaustive_matches_spec()");

// 下面的Named-cell Spot-checks是KEPT作为文档
// （它们读起来比循环更好），但现在对于详尽的证明来说
// 是多余的。不要相信它们是覆盖信号——上面的循环就是。
static_assert(permission(kPure,  Profile::Write)   == Decision::Allow);
static_assert(permission(kEdit,  Profile::Write)   == Decision::Allow);
static_assert(permission(kRead,  Profile::Ask)     == Decision::Allow);
static_assert(permission(kWrite, Profile::Ask)     == Decision::Prompt);
static_assert(permission(kExec,  Profile::Ask)     == Decision::Prompt);
static_assert(permission(kRead,  Profile::Minimal) == Decision::Prompt);
static_assert(permission(kPure,  Profile::Minimal) == Decision::Allow);

} // namespace proofs

} // namespace agentty::tools::policy
