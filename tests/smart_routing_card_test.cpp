// smart_routing_card_test ——卡片必须显示路线 传输 被带走了
//
// build_smart_routing_card 财务报告和已审计财务报表 launch_stream 转弯分类 INDEPENDENTLY.
// 代码对两者的评论 调用 说 "same call as launch_stream so card == wire" ,
// 但没有任何强制措施:两个站点之一的一次掉包的争论留下了一张卡片广告,
// 广告上有一个请求从未走过的路线,而套房却保持绿色。
// 这正是这个文件存在的无法捕捉的失败,它没有捕获到任何东西,直到它存在—
// —一个突变的掉落。
// `m.d.smart.complex_threshold` 在那家卡片店里通过了570次测试
//
// 牌是纯的,是用圆锥的 Model ,所以是( S)
// 传输 答案可以从相同的输入和比较字段中重算出来。
// 与smart::classify_turn/effort_for_score直接比较——
// 函数为launch_stream/调用——因此,如果EITHER站点漂移,则失败。

#include "agtest.hpp"

#include "agentty/domain/complexity.hpp"
#include "agentty/domain/catalog.hpp"
#include "agentty/domain/smart_mode.hpp"
#include "agentty/domain/smart_tuning.hpp"
#include "agentty/runtime/app/cmd_factory.hpp"
#include "agentty/runtime/model.hpp"

#include <string>

namespace {

using namespace agentty;
namespace sm = agentty::smart;

// 一个在转弯边缘的模型: Smart Mode 打开, 一个用户消息,
// 即将运行的分类器. `tuning` 是用户配置的政策.
Model turn_with(const std::string& text, sm::RoleConfig tuning) {
    Model m;
    m.d.smart = std::move(tuning);
    m.d.smart.enabled = true;
    Message user;
    user.role = Role::User;
    user.text = text;
    m.d.current.messages.push_back(std::move(user));
    return m;
}

} // 命名空间结束。

TEST_CASE("smart card: the card's complexity is the wire's complexity") {
    // 扫荡 CONFIGURED 其分类位于边界附近的一个转弯处,
    // 其整个范围阈值。在每次设定牌时,必须同意什么。
    // classify_turn - 电话 传输 属于自己的
    // 调用 – 为相同的输入制作。 忘记通过阈值的卡片站点
    // 会跟踪发送的默认值, 一旦用户移动它就会分化。
    const std::string probe = "update the parser and re-run the formatter";

    for (int cut = sm::tuning::kComplexMin; cut <= sm::tuning::kComplexMax; ++cut) {
        sm::RoleConfig cfg;
        cfg.complex_threshold = cut;
        const Model m = turn_with(probe, cfg);

        const auto card = app::cmd::build_smart_routing_card(m);
        REQUIRE(card.has_value());

        // 或 传输 将分类,计算的方式 launch_stream 已经。
        const auto wire = sm::classify_turn(probe, m.s.smart_turn_complexity,
                                            /*attachment_bytes=*/0, /*图像=*/0,
                                            cfg.complex_threshold);

        CHECK(card->smart_route_complexity == std::string{sm::to_string(wire.tier)});
    }
}

TEST_CASE("smart card: the card's effort is the wire's effort") {
    // 与“E”相同 OTHER 把手头 deep_margin
    // 决定其波段的深转是否获得额外的努力步骤,
    // 所以按默认差值分类的卡片会显示与请求所携带的卡片
    // 不同的努力.
    const std::string probe = "redesign the auth module end to end across services";

    for (int deep = sm::tuning::kDeepMarginMin;
         deep <= sm::tuning::kDeepMarginMax; ++deep) {
        sm::RoleConfig cfg;
        cfg.deep_margin = deep;
        const Model m = turn_with(probe, cfg);

        const auto card = app::cmd::build_smart_routing_card(m);
        REQUIRE(card.has_value());

        const auto wire_cx = sm::classify_turn(probe, m.s.smart_turn_complexity,
                                               0, 0, cfg.complex_threshold);
        const auto caps = ModelCapabilities::from_id(card->smart_route_model);
        // 牌上标出它所通往的模型; 努力梯级为每
        // -model ,所以比较必须使用 该 模型的能力。
        //
        // resolve_auto_effort 镜像什么
        // resolve_role 真实的 调用 站点 : `auto`
        // 默认值是现在,它会锁定每个模型, 所以重算跳过这个会比起一个
        // 不同的基数 传输 已使用。
        const auto base = resolve_auto_effort(m.d.effort, caps);
        const Effort wire_effort =
            sm::effort_for_score(base, wire_cx, caps, m.s.smart_effort_bias,
                                 cfg.deep_margin);

        CHECK(card->smart_route_effort
              == std::string{effort_label(wire_effort)});
    }
}

TEST_CASE("smart card: an inserted proactive block cannot change the route") {
    // 电话 加载-bearing 计算路线决定背后的假设
    // 一次 每转两圈。
    //
    // 发射的主动转弯是DEFERRED:submit_turn建卡,
    // 然后进行检索,将上下文信息插入记录,
    // ProactiveContextReady从不同的处理函数点火launch_stream.
    // 如果插入该分类可以移动,则在提交时计算并在发射时宣读的
    // 决定将是: 过期 - 以及这两个地点重新出现的原因
    // -derive 独立地今天是 -deriving 记录
    // 在突变中是微不足道的正确。
    //
    // 无法移动:两条扫描都往后走, is_proactive_context
    // () && !smart_routing &&
    // fork_note，且插入的块恰好是一条主动检索上下文用户消息。
    // 。这说明,而不是相信阅读。
    const std::string probe = "redesign the auth module end to end across services";

    // Model 正在移动 -only (拥有一个 ViewCache 和a 类 ScrollbackLedger ),
    // 因此,每个 变体 ,而不是复制。
    const auto route_of = [&](bool with_proactive, bool with_card) {
        Model m = turn_with(probe, sm::RoleConfig{});
        if (with_proactive) {
            // 确切的说 ProactiveContextReady
            // 切换到: 带大型、 复合的用户信息 -looking 载荷.
            // 如果扫描没有跳过,THIS就是分类.
            Message ctx;
            ctx.role = Role::User;
            ctx.proactive =
                Message::ProactiveContext{.confidence = 0.9, .expanded = false};
            ctx.text = "retrieved context: " + std::string(4096, 'x');
            m.d.current.messages.push_back(std::move(ctx));
        }
        if (with_card) {
            // 另一件在提交和发射之间降落的东西 -text 财务报告
            // 和已审计财务报表 Role::User ——分类它会
            // 产生空字符串.
            Message card;
            card.role = Role::User;
            card.smart_routing = true;
            m.d.current.messages.push_back(std::move(card));
        }
        auto c = app::cmd::build_smart_routing_card(m);
        REQUIRE(c.has_value());
        return std::tuple{c->smart_route_complexity, c->smart_route_effort,
                          c->smart_route_model};
    };

    const auto plain     = route_of(false, false);
    const auto proactive = route_of(true,  false);
    const auto both      = route_of(true,  true);

    CHECK(proactive == plain);
    CHECK(both == plain);
}

TEST_CASE("smart card: moving a knob actually moves the card") {
    // 如果卡片对调音不敏感,上面的测试就会空洞通过—
    // —两边只是一起错了。 因此:在幅度中至少有
    // 一个设置必须从默认值中产生DIFFERENT卡,
    // 证明管道完全带有信号。
    const std::string probe = "update the parser and re-run the formatter";

    sm::RoleConfig at_default;
    sm::RoleConfig at_low;
    at_low.complex_threshold = sm::tuning::kComplexMin;

    const auto a = app::cmd::build_smart_routing_card(turn_with(probe, at_default));
    const auto b = app::cmd::build_smart_routing_card(turn_with(probe, at_low));
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(a->smart_route_complexity != b->smart_route_complexity);
}
