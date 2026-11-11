// effort_auto_test — `auto` 努力:允许分类器工作的默认值。
//
// 内容 WAS WRONG. `Effort::None` 做两个工作: "the user asked for no reasoning"
// 和 "the user never said anything" 那种混沌悄悄地打破了
// Smart Mode的核心前提.
//
// effort_for_complexity 将用户的努力视为 MIDPOINT 梯子的形状 :
// 标准保存它, 复杂步骤+1, 简单步骤−1. 这是对称的,
// 它需要双向的室。 但无一是 rung 0。 FLOOR。从那里转弯,
// 机密的复合体只能到达 `minimal` (一个跑步),
// 而同一个转弯从一个 `medium` 达到基准 `high` (
// 并且可以掉到 `low` 因此,在默认设置中,分类者的判决几乎是
// 惰性的:它可以得一个 提示词 复杂到它喜欢的, 和拨号无处可去。
//
// 这和以前在电脑上修过一次的bug一样 ROLE 轴线 — 见 resolve_turn_routing ,
// 其中角色是硬码 战略如此
// 分类器只调整推理力度。
// 镜像在努力轴上一直存在到现在
//
// 该 FIX ,以及这个文件的针:
//   1. 汽车是一个独特的状态,因此"never configured" "chose off".
//   2. 自动锚定在 MIDPOINT 模型暴露的梯子...
//      位置,不是级别,所以它是正确的1 -rung 模型和6号机
//      -rung 一个
//   3. 汽车在夹子中存活。 (微妙杀手:switch_to_model_ref夹子)
//      每一个模型跳,所以一个自动车崩溃 在那里会是正确
//      的 在第一跑 并退化为一长跑 之后。
//   4. 明确发出 "" 包括综合体
//   5. 特里维耶还很艰难 -off 无论基地如何,所以升起的锚
//      廉价的转弯不费什么

#include "agentty/domain/smart_mode.hpp"

#include "agtest.hpp"

#include <string>

using namespace agentty;
namespace sm = agentty::smart;
using E = Effort;
using Cx = agentty::smart::Complexity;

namespace {

// 一个模特儿 ON 梯子就是这个 `on`.
ModelCapabilities ladder_of(std::initializer_list<E> on) {
    ModelCapabilities c{};
    c.effort_set_known = true;
    std::uint8_t set = 0;
    for (E e : on) set |= effort_bit(e);
    c.effort_set = set;
    return c;
}

// 三个形状,实际上飞船, 加上堕落的。
const ModelCapabilities kGpt5   = ladder_of({E::Minimal, E::Low, E::Medium, E::High});
const ModelCapabilities kClaude = ladder_of({E::Low, E::Medium, E::High, E::Max});
const ModelCapabilities kBinary = ladder_of({E::High});
const ModelCapabilities kNone   = ladder_of({});

// 运行时所做的: 解决Auto在边缘,然后一步步地复杂.
E routed(E base, Cx tier, const ModelCapabilities& caps) {
    return sm::effort_for_complexity(resolve_auto_effort(base, caps), tier, caps);
}

} // 命名空间结束。

TEST_CASE("effort auto: the anchor is a POSITION, not a level") {
    // 中点 ON rugs. 不是一个硬码 "medium" - 这对
    // 一个唯一能用到的模型来说毫无意义 `high` ,错误的 在一个到达
    // `max`.
    CHECK(auto_base_effort(kGpt5)   == E::Low);     // {min, low, med, high} - > idx 1 数据
    CHECK(auto_base_effort(kClaude) == E::Medium);  // {低, 中, 高, max} - > idx 1。
    CHECK(auto_base_effort(kBinary) == E::High);    // {高} - > idx 0 数据

    // 完全没有梯子:没有锚,所以自动IS关闭.
    CHECK(auto_base_effort(kNone) == E::None);

    // `off` 故意的 故意的,故意的,是故意的,
    // 这是故意的 不 包括它会把中点拖到地板上,
    // 并重新引入存在非常不对称的线条。
    CHECK(auto_base_effort(kGpt5) != E::None);
}

TEST_CASE("effort auto: the classifier's verdict actually moves the dial") {
    // 该 REGRESSION 此 FILE EXISTS FOR
    // 测量拨号 RANGE。如果基数=无,则有2个不同的值(关闭,
    // 最小值),分类器是装饰性的。如果自动,则会跨过梯子。
    auto distinct = [](E base, const ModelCapabilities& caps) {
        E seen[4];
        int n = 0;
        for (Cx t : {Cx::Trivial, Cx::Simple,
                             Cx::Standard, Cx::Complex}) {
            const E got = routed(base, t, caps);
            bool dup = false;
            for (int i = 0; i < n; ++i) if (seen[i] == got) dup = true;
            if (!dup) seen[n++] = got;
        }
        return n;
    };

    // 旧的默认只能产生两个操作点.
    CHECK(distinct(E::None, kGpt5) == 2);
    // 新的默认值每一级产生一个。
    CHECK(distinct(E::Auto, kGpt5) == 4);
    CHECK(distinct(E::Auto, kClaude) == 4);
}

TEST_CASE("effort auto: per-tier routing on each ladder shape") {
    // 5 {最小,低,中,高} 低沉地锚定
    CHECK(routed(E::Auto, Cx::Trivial,  kGpt5) == E::None);
    CHECK(routed(E::Auto, Cx::Simple,   kGpt5) == E::Minimal);
    CHECK(routed(E::Auto, Cx::Standard, kGpt5) == E::Low);
    CHECK(routed(E::Auto, Cx::Complex,  kGpt5) == E::Medium);

    // ⁇  { 低,中,高,max}, 锚在中  ⁇  – 一个复杂的
    // 转弯着陆在 `high`, 错误报告就是如此.
    CHECK(routed(E::Auto, Cx::Trivial,  kClaude) == E::None);
    CHECK(routed(E::Auto, Cx::Simple,   kClaude) == E::Low);
    CHECK(routed(E::Auto, Cx::Standard, kClaude) == E::Medium);
    CHECK(routed(E::Auto, Cx::Complex,  kClaude) == E::High);

    // 一个二进制程序模型不能表示等级;它决不能发明这些等级.
    CHECK(routed(E::Auto, Cx::Trivial,  kBinary) == E::None);
    CHECK(routed(E::Auto, Cx::Standard, kBinary) == E::High);
    CHECK(routed(E::Auto, Cx::Complex,  kBinary) == E::High);

    // 非 国 家 -reasoning 模型在每一层都保持安静。
    for (Cx t : {Cx::Trivial, Cx::Simple,
                         Cx::Standard, Cx::Complex})
        CHECK(routed(E::Auto, t, kNone) == E::None);
}

TEST_CASE("effort auto: trivial is free, so the raised anchor costs nothing") {
    // 这使得改变违约保险。你不用缴纳底税,
    // 而靠挣得的转折支付。
    for (const auto& caps : {kGpt5, kClaude, kBinary, kNone})
        for (E base : {E::Auto, E::None, E::Low, E::Medium, E::High, E::Max})
            CHECK(routed(base, Cx::Trivial, caps) == E::None);
}

TEST_CASE("effort auto: explicit off is still off, everywhere") {
    // 使默认更改可以辩驳的属性。 选择的用户
    // `off` 安静 每个 包括综合体,永远。
    for (Cx t : {Cx::Trivial, Cx::Simple,
                         Cx::Standard}) {
        CHECK(routed(E::None, t, kGpt5)   == E::None);
        CHECK(routed(E::None, t, kClaude) == E::None);
    }
    // 由一个清晰的离静步骤(即步长的、
    // 有记录的老行为)而来的复杂性,但 传输 存储的值 "".
    CHECK(effort_wire_for(E::None, kGpt5)   == "");
    CHECK(effort_wire_for(E::None, kClaude) == "");
    CHECK(effort_wire_for(E::None, kBinary) == "");
}

TEST_CASE("effort auto: never reaches the wire as a literal") {
    // 自动为 a UI/persistence 州立大学. effort_wire_for
    // 谓 该 边缘,它解决在那里 所以没有 下游 (继子,
    // Anthropic 传输层 预算表)必须知道Auto的存在.
    CHECK(effort_wire_for(E::Auto, kGpt5)   == "low");
    CHECK(effort_wire_for(E::Auto, kClaude) == "medium");
    CHECK(effort_wire_for(E::Auto, kBinary) == "high");
    CHECK(effort_wire_for(E::Auto, kNone)   == "");

    // 也从来不是能学的 effort_bit 不得与真正的水平相冲突,
    // 或持续 learned_effort_sets 面具会腐烂的
    CHECK(effort_bit(E::Auto) == 0);
    for (E lv : {E::Minimal, E::Low, E::Medium, E::High, E::Xhigh, E::Max})
        CHECK((effort_bit(lv) & effort_bit(E::Auto)) == 0);
}

TEST_CASE("effort auto: survives a clamp (the model-switch killer)") {
    // switch_to_model_ref 夹子 m.d.effort 打开
    // 每个 provider/model hop. 如果夹子崩溃了自动到运行,
    // 此特性将完全正确, 在第一次运行时, 并且静静地降低用户第一次
    // 按下 ^P 失败将看起来像 "auto stopped working"
    // 没有什么好担心的 所以它有自己的案子
    for (const auto& caps : {kGpt5, kClaude, kBinary, kNone}) {
        CHECK(clamp_effort(E::Auto, caps) == E::Auto);
        CHECK(nearest_effort(E::Auto, effort_set_of(caps)) == E::Auto);
    }

    // Hoping gpt-5 - > claude - > 二进制程序 - > 背部必须全自动保存。
    E e = E::Auto;
    for (const auto& caps : {kGpt5, kClaude, kBinary, kGpt5})
        e = clamp_effort(e, caps);
    CHECK(e == E::Auto);
}

TEST_CASE("effort auto: persistence round-trip, and the unset-vs-off split") {
    // 迁居故事,如表.
    //
    // "" 是什么 说吧 每个 已有用户已: 字段已空 -by-default
    // 因为它的设置存在,所以它无法区分 "chose off" 发件人
    // "never touched it" 读它作为汽车,就是没有移民证就能完成修复;
    // 任何想要沉默的人都会选择 `off` 一次,现在一直作为 "none"
    // 圆形 -trips 完全正确,我们没有
    CHECK(effort_from_wire("")     == E::Auto);   // 从未配置
    CHECK(effort_from_wire("auto") == E::Auto);
    CHECK(effort_from_wire("none") == E::None);   // 关闭
    CHECK(effort_from_wire("off")  == E::None);   // 容忍的别名

    // 这样的价值 并不知道锚在自动,而不是沉默的意思 "off"
    // —— 人均数 -model 夹子能处理其余的
    CHECK(effort_from_wire("ultra") == E::Auto);

    // 实际水平没有变化。
    CHECK(effort_from_wire("minimal") == E::Minimal);
    CHECK(effort_from_wire("low")     == E::Low);
    CHECK(effort_from_wire("medium")  == E::Medium);
    CHECK(effort_from_wire("high")    == E::High);
    CHECK(effort_from_wire("xhigh")   == E::Xhigh);
    CHECK(effort_from_wire("max")     == E::Max);

    // 圆 -trip :每个州都活下来了 save/load 循环。
    // 这就是保持一个清晰的 `off` 从衰变回 `auto` 接下来的发射
    for (E e : {E::Auto, E::None, E::Minimal, E::Low, E::Medium, E::High,
                E::Xhigh, E::Max})
        CHECK(effort_from_wire(effort_to_wire_setting(e)) == e);

    // 这两支哨兵必须坚持 NON-EMPTY 字符串 : 持久化 空值(
    // 缩放) `if (!s.effort.empty())` ),
    // 所以是一个空的 "none" 将写入为缺席,并读回为自动——默默
    // 取消用户在每次重启时的明确选择。
    CHECK(!effort_to_wire_setting(E::None).empty());
    CHECK(!effort_to_wire_setting(E::Auto).empty());
}

TEST_CASE("effort auto: the picker can reach it and leave it") {
    // 自动引导列表,所以这是新用户看到的第一件事,
    // 其中的1个是`off`——断掉推理就只能保持一个键盘。
    const auto l = available_efforts(kGpt5);
    REQUIRE(l.size() >= 2);
    CHECK(l[0] == E::Auto);
    CHECK(l[1] == E::None);

    // 自行车必须访问汽车,然后回到它,在每一个形状上都有
    // 梯子——一个可以输入但不能离开的值(反之亦然)是一个陷阱.
    for (const auto& caps : {kGpt5, kClaude, kBinary}) {
        const int n = static_cast<int>(available_efforts(caps).size());
        E e = E::Auto;
        for (int i = 0; i < n; ++i) e = cycle_effort(e, +1, caps);
        CHECK(e == E::Auto);                       // 全循环返回家
        CHECK(cycle_effort(E::Auto, +1, caps) == E::None);   // 陆地上
    }

    // 没有梯子的模型没有自动: 一种 "auto" 只能决意放弃,
    // 那将是一个谎言。
    const auto none_list = available_efforts(kNone);
    REQUIRE(none_list.size() == 1);
    CHECK(none_list[0] == E::None);
}

TEST_CASE("effort auto: label and display") {
    CHECK(effort_label(E::Auto) == "auto");
    CHECK(effort_label(E::None) == "off");
    CHECK(effort_label(E::High) == "high");
}
