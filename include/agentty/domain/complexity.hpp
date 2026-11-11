#pragma once
// agentty::smart-用于Smart-Mode努力扩展的转向-complexity分类。
//
// 研究-backed杠杆（Anthropic、
// "Building a multi-agent research system":*规模努力查询复杂性*；RouteLLM/级联调查：
// 前期 intent/complexity 路由，
// *对于不明确的查询保守路由*)。一个微小的本地分类器在约 0
// 延迟的情况下，对于 well-defined 层达到约 90%
// 的准确率——没有模型 调用，没有网络。我们将用户的消息分为四层，
// 并让编排器相应地扩展战略模型的推理工作（并驱动系统 提示词
// 广告的委托预算）。
//
// 设计上保守：DEFAULT是标准的，并且强复杂性信号升级，而只有清晰的
// -trivial消息de-escalates。从 -routing 到
// "think harder" 的价格远比 -thinking 硬转便宜，
// 因此关系向上突破。

#include <cstdint>
#include <string>
#include <string_view>

#include "agentty/domain/smart_tuning.hpp"   // kComplexDefault

namespace agentty::smart {

enum class Complexity : std::uint8_t {
    Trivial,   // "yes"、"thanks"、"run it"、"commit" — 1 名特工，没有思考
    Simple,    // 定向查找/one-liner修复
    Standard,  // 默认工作轮次（保守的回退）
    Complex,   // 架构/设计/调试/multi-file/"why"
};

[[nodiscard]] constexpr std::string_view to_string(Complexity c) noexcept {
    switch (c) {
        case Complexity::Trivial:  return "trivial";
        case Complexity::Simple:   return "simple";
        case Complexity::Standard: return "standard";
        case Complexity::Complex:  return "complex";
    }
    return "standard";
}

// 对用户回合的文本进行分类。纯净，分配-light，外壳-insensitive。
//
// 这是一个小的ADDITIVE FEATURE SCORE，
// 不是关键字查找：三个正交的、主要是语言-agnostic信号家族各自贡献权重，
// 并且总和被阈值化为一个层。这修复了旧的 all-or-nothing
// 行为（一个杂散的 "design" 强制 Complex）并概括了
// 固定的英语词典：
//   ·STRUCTURAL (语言-agnostic):枚举询问，conjunction/clause
//     密度、代码-token密度、问题形状、字形长度。
//     请求的复杂性主要在于其 STRUCTURE，而不是其动词。
//   ·LEXICAL (多语言）：跨域的加权"hard"/"trivial"关键词集
//     主要语言——ADDS权重的证据，绝不是硬覆盖。
//   ·MORPHOLOGICAL:令牌-shape品种(散文与标识符与路径)。
// 保守：关系向上突破（低于
// -thinking的硬转向成本高于超过-thinking的廉价转向）。
[[nodiscard]] Complexity classify_complexity(
        std::string_view text,
        int complex_min = tuning::kComplexDefault) noexcept;

// 评分分类：层PLUS连续分数和到最近层边界的余量（0=在阈值上，
// 较大=更有信心）。 classify_with_context
// 使用边距平滑地混合 follow-up，
// 而不是在层之间对齐。分数单位是任意的，
// 但复杂度为单调递增的。
struct ComplexityScore {
    Complexity tier   = Complexity::Standard;
    int        score  = 0;   // 附加特征得分
    int        margin = 0;   // 到最近层边界的距离
};
// 复杂切割是用户-tunable，因此它是在分类器内读取的
// PARAMETER而不是getenv()：从此处的环境读取的
// 值看不到用户在设置UI中设置的阈值。默认情况下，每个现有的调用者
// （包括整个测试套件）均保持不变；路由路径经过
// `m.d.smart.complex_threshold`。
[[nodiscard]] ComplexityScore classify_score(
        std::string_view text,
        int complex_min = tuning::kComplexDefault) noexcept;

// Context-aware分类。 classify_complexity
// 是回合 -local，因此复杂分解后的短暂跟随 -up（
// 现在对另一个模块做同样的修改。
// "and the tests?"）分类为简单，而 DROPS 努力一步
// - 即使它继承了前一回合的复杂性。这会将非 -Trivial 延续部分
// -way 提升回上一回合的等级：follow-up 仍然比产生它的回合便宜，
// 但不会被错误分类为一次性的 -liner。一个简单的回合（"thanks"，
// "commit it"）总是从表面上看——一个确认就是一个确认，
// 无论之前发生了什么。文本中新鲜的复杂信号总是会赢得胜利。
[[nodiscard]] inline Complexity classify_with_context(
        std::string_view text, Complexity prev,
        int complex_min = tuning::kComplexDefault) noexcept {
    const Complexity self = classify_complexity(text, complex_min);
    if (self == Complexity::Trivial || self == Complexity::Complex) return self;
    if (static_cast<int>(prev) <= static_cast<int>(self))           return self;
    // 继承前一回合以下的ONE等级（继续努力通常比原始任务
    // 容易一点），永远不要低于文本自己的等级。
    // prev 是复杂⇒提升到标准；上一个是标准 ⇒ 保持不变。
    const auto inherited =
        static_cast<Complexity>(static_cast<int>(prev) - 1);
    return static_cast<int>(inherited) > static_cast<int>(self) ? inherited : self;
}

// 评分上下文-aware分类：ComplexityScore为一个回合，
// 针对Complex-ish前任进行调整，与classify_with_context
// 调整等级完全相同。当 follow-up 通过继承成为 LIFTED 层时，
// 其 score/margin 将被带到提升层的边界（边距 0），
// 因此 下游 连续工作扩展将继承层视为 "just barely in" 而不是
// "deep in" — 继承的转弯不应该获得它从未在其自己的文本上获得的 deep
// -band 额外步骤。
[[nodiscard]] inline ComplexityScore classify_score_with_context(
        std::string_view text, Complexity prev,
        int complex_min = tuning::kComplexDefault) noexcept {
    ComplexityScore s = classify_score(text, complex_min);
    // SAME剪切，或者这个调用将撤消上面的：classify_with_context
    // 重新-derives文本中的层并用它覆盖`s.tier`，
    // 因此这里的默认阈值默默地丢弃配置的阈值并使
    // classify_turn对设置完全不敏感。
    const Complexity lifted = classify_with_context(text, prev, complex_min);
    if (lifted == s.tier) return s;            // 无继承变更
    s.tier   = lifted;
    s.margin = 0;                              // 位于AT界线，不深
    return s;
}

// PAYLOAD-aware细化。分类器读取作曲家 TEXT，
// 但文本通常仅包含 标签 占位符 - 真正的 载荷（500
// -line 粘贴、从运行中捕获的错误日志、屏幕截图）位于附件中。
// "fix this"+巨大的粘贴是NOT的简单转弯；将其从 -routes
// 下的 8-char 文本中分类，正好是模型需要深度的转弯（出于同样的原因，
// RouteLLM-family 路由器使用查询 length/structure
// 作为核心功能）。 字节 的纯函数很重要，因此它保持单位-testable：
//   ·任何非-trivial附件载荷举升平凡→简单（确认
//     不携带膏体；如果是，则不是确认），
//   ·大量载荷 (≥4 KB —大约一屏code/log)
//     升降机简单→标准，
//   ·大载荷 (≥32 KB)或任何图像提升标准→复杂是NOT
//     自动——仅体积并不能证明硬度——但它增加了分数，
//     因此边界-Standard转弯，大载荷提示超过阈值。
// 绝不降低等级；边距重置为电梯的边界（转弯没有从
// 自己的文本中获得深度）。
[[nodiscard]] inline ComplexityScore refine_with_payload(
        ComplexityScore s, std::size_t attachment_bytes, int images) noexcept {
    const bool any    = attachment_bytes > 256 || images > 0;
    const bool medium = attachment_bytes >= 4  * 1024;
    const bool large  = attachment_bytes >= 32 * 1024 || images > 0;
    if (!any) return s;
    auto lift_to = [&](Complexity floor_tier) {
        if (static_cast<int>(s.tier) < static_cast<int>(floor_tier)) {
            s.tier   = floor_tier;
            s.margin = 0;
        }
    };
    lift_to(Complexity::Simple);
    if (medium) lift_to(Complexity::Standard);
    if (large) {
        s.score += 2;   // 证据，而非推翻——可能提示标准→复杂
        if (s.tier == Complexity::Standard && s.margin <= 1) {
            s.tier   = Complexity::Complex;
            s.margin = 0;
        }
    }
    return s;
}

// 这个follow-up用户消息是否用上一轮的结果——i.e表达DISSATISFACTION。
// 路由后悔之前学过的应该注意什么（偏向于转-class的功夫了）？
// 故意缩小范围：它不得 触发重定向 ("wrong file, look elsewhere")、
// 附加请求 ("actually, also add tests") 或赞扬 (
// "actually that's perfect") — 这些并不是对路
// 由器花费了多少精力的抱怨。 Pure +turn-local 因此结果
// -learning循环可以是unit-tested。只有开头的 48 个字符很重要。
[[nodiscard]] bool is_routing_correction(std::string_view text) noexcept;

// 这个短转弯是CONTINUATION COMMAND ("continue"、
// "retry"、"go ahead"、"do it")而不是终端确认
// ("thanks"、"lgtm")吗？这种区别对于动量很重要：确认结束了
// 交换（琐碎是正确的），但是继续 RESUMES 上一轮的工作 - 将其固定
// 到 Trivial/Effort::None 使模型以零推理预算继续执行复杂的任务。
// 纯的;在 ACK 表旁边的 complexity.cpp 中定义。
[[nodiscard]] bool is_continuation_cue(std::string_view text) noexcept;

// ── 该 组成回合分类器 ────────────────────────────────────
// 两个路由站点（launch_stream和🧠卡构建器）MUST
// 都使用单一入口点，因此所显示的路由永远不会与传输不一致。
// 按顺序组成：
//   1.文字得分+等级动量(classify_score_with_context)，
//   2、CONTINUATION电梯：Standard/Complex之后裸"continue"/"retry"
//      工作继承了前一轮以下的一层，而不是固定到琐碎的
//      工作——恢复努力工作不是确认，
//   3.载荷升降机(refine_with_payload)，
//   4.CORRECTION楼层：表示"it's still broken"从不路由的转弯
//      BELOW标准——在失败的重试上减少努力
//      是总是错误的一个方向。
[[nodiscard]] inline ComplexityScore classify_turn(
        std::string_view text, Complexity prev,
        std::size_t attachment_bytes, int images,
        int complex_min = tuning::kComplexDefault) noexcept {
    ComplexityScore s = classify_score_with_context(text, prev, complex_min);
    if (s.tier == Complexity::Trivial
        && static_cast<int>(prev) >= static_cast<int>(Complexity::Standard)
        && is_continuation_cue(text)) {
        s.tier   = static_cast<Complexity>(static_cast<int>(prev) - 1);
        s.margin = 0;   // 继承-勉强进入，不深入
    }
    s = refine_with_payload(s, attachment_bytes, images);
    if (is_routing_correction(text)
        && static_cast<int>(s.tier) < static_cast<int>(Complexity::Standard)) {
        s.tier   = Complexity::Standard;
        s.margin = 0;
    }
    return s;
}

} // namespace agentty::smart
