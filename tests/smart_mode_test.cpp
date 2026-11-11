// smart_mode_test — 角色解析器(Smart Mode Step1).
//
// 纯绘图:角色+母模型+努力+ 目录 + 配置 + RoleProfile.
// 没有 I/O ,没有 传输。验证零 -config 自动 -fill , 超音速,
// 关闭通道 -through 单曲 -tier 无 -regression 保证。
#include "agtest.hpp"

#include "agentty/domain/smart_mode.hpp"
#include "agentty/domain/catalog.hpp"   // ModelCapabilities

namespace sm = agentty::smart;
using agentty::Effort;
using agentty::ModelInfo;
using agentty::ModelId;
using agentty::ModelCapabilities;

static ModelInfo mi(const char* id, int ctx = 200000) {
    ModelInfo m;
    m.id = ModelId{id};
    m.context_window = ctx;
    m.supports_tools = true;
    return m;
}

TEST_CASE("smart_mode") {
    // 一个现实 Claude 目录 : Opus (旗舰), Sonnet (中舰), Haiku (廉价).
    std::vector<ModelInfo> claude = {
        mi("claude-opus-4-20250514"),
        mi("claude-sonnet-4-20250514"),
        mi("claude-haiku-4-20250514"),
    };
    const std::string parent = "claude-opus-4-20250514";

    // 1. 智能模式 OFF ~ 每个角色都是传球 ~ -through 给父母的
    {
        sm::RoleConfig cfg;   // enabled=false
        auto s = sm::resolve_role(sm::ModelRole::Strategic, parent, Effort::High, claude, cfg);
        auto i = sm::resolve_role(sm::ModelRole::Implementation, parent, Effort::High, claude, cfg);
        auto u = sm::resolve_role(sm::ModelRole::Utility, parent, Effort::High, claude, cfg);
        CHECK(s.model == parent, "off: strategic = parent");
        CHECK(i.model == parent, "off: impl = parent");
        CHECK(u.model == parent, "off: utility = parent");
    }

    // 2. 智能模式 ON 零,零,无 -config 自动 -fill.
    {
        sm::RoleConfig cfg; cfg.enabled = true;
        auto s = sm::resolve_role(sm::ModelRole::Strategic, parent, Effort::High, claude, cfg);
        auto i = sm::resolve_role(sm::ModelRole::Implementation, parent, Effort::High, claude, cfg);
        auto u = sm::resolve_role(sm::ModelRole::Utility, parent, Effort::High, claude, cfg);

        CHECK(s.model == parent, "on: strategic = parent (flagship)");
        CHECK(i.model.find("sonnet") != std::string::npos, "on: impl = the mid (sonnet) model");
        CHECK(u.model.find("haiku") != std::string::npos, "on: utility = the cheap (haiku) model");
        CHECK(u.effort == Effort::None, "on: utility runs with NO reasoning budget");
        // Claude 4 Sonnet/Opus 不要暴露一个推理
        // -effort 控制,所以解密者诚实地 夹住每个角色的
        // 努力 目录 ——它从来不要求付出400个模型上的努力.
        CHECK(s.effort == Effort::None, "on: effort clamps to None for a non-reasoning model (honest)");
    }

    // 2b. 努力推动 EFFORT-CAPABLE 目录 (o-series /gpt-5.x) (中文(简体) ).
    //     验证战略,保持用户的努力 和Impl步骤一个下。
    {
        std::vector<ModelInfo> gpt = {
            mi("gpt-5-pro"),      // 旗舰( 最大功率)
            mi("gpt-5"),          // 中场工作马
            mi("gpt-5-nano"),     // 便宜的
        };
        const std::string gparent = "gpt-5-pro";
        sm::RoleConfig cfg; cfg.enabled = true;
        auto s = sm::resolve_role(sm::ModelRole::Strategic, gparent, Effort::High, gpt, cfg);
        auto i = sm::resolve_role(sm::ModelRole::Implementation, gparent, Effort::High, gpt, cfg);
        // 只能断言 STEP RELATIONSHIP 如果
        // 模型实际努力; 目录 报告没有努力支持,既不给任何人施压,
        // 也毫无结果。
        const bool s_thinks = s.effort != Effort::None;
        if (s_thinks) {
            CHECK(s.effort == Effort::High, "gpt: strategic keeps the user's High effort");
            CHECK(static_cast<int>(i.effort) <= static_cast<int>(s.effort),
                  "gpt: impl effort is <= strategic (stepped down or equal)");
        } else {
            CHECK(i.effort == Effort::None, "gpt: no effort support → impl also None (consistent)");
        }
    }

    // 3. 明确胜于自动-fill(型号总是;努力被压制)。
    {
        sm::RoleConfig cfg; cfg.enabled = true;
        cfg.utility.set = true;
        cfg.utility.model = "claude-sonnet-4-20250514";
        cfg.utility.effort = Effort::Low;
        auto u = sm::resolve_role(sm::ModelRole::Utility, parent, Effort::High, claude, cfg);
        CHECK(u.model.find("sonnet") != std::string::npos, "override: utility uses the pinned model");
        // 松网4不费力,所以低钳子对无——被钉
        // 的努力只被钉在模型支持的顶端.
        CHECK(u.effort == Effort::None, "override: pinned effort clamped to what the model supports");
    }

    // 4. Single-model账户 – 不回归:每个角色都留在父母身上.
    {
        std::vector<ModelInfo> solo = { mi("claude-opus-4-20250514") };
        sm::RoleConfig cfg; cfg.enabled = true;
        auto i = sm::resolve_role(sm::ModelRole::Implementation, parent, Effort::High, solo, cfg);
        auto u = sm::resolve_role(sm::ModelRole::Utility, parent, Effort::High, solo, cfg);
        CHECK(i.model == parent, "solo account: impl stays on parent (no mid tier)");
        CHECK(u.model == parent, "solo account: utility stays on parent (nothing cheaper)");
    }

    // 5. 三层行为门独立,均在`enabled`以下.
    {
        sm::RoleConfig cfg;
        // 三层是主开关——没有每层 -layer 标志。
        // 用户无法关闭“智能模式”的组合,
        // 只能使用 AGENTTY_SMART_NO_ * 开发者逃生舱可以做到这一点,
        // 它们被包围 -only 设计
        CHECK(!cfg.internal_routing() && !cfg.orchestration() && !cfg.subagent_routing(),
              "layers: disabled master → all layers inactive");
        cfg.enabled = true;
        CHECK(cfg.internal_routing() && cfg.orchestration() && cfg.subagent_routing(),
              "layers: enabled master → all three active, no sub-toggles");
        cfg.enabled = false;
        CHECK(!cfg.internal_routing() && !cfg.orchestration() && !cfg.subagent_routing(),
              "layers: master off is the single source of truth");

        // 逃生舱内每个舱门都挤压一个层,
        // 让其他舱门运行——这就是三个舱门的要害.
        cfg.enabled = true;
        cfg.no_orchestrate = true;
        CHECK(cfg.internal_routing() && !cfg.orchestration() && cfg.subagent_routing(),
              "escape hatch: NO_ORCHESTRATE disables only orchestration");
        cfg.no_orchestrate = false;
        cfg.no_internal = true;
        CHECK(!cfg.internal_routing() && cfg.orchestration() && cfg.subagent_routing(),
              "escape hatch: NO_INTERNAL disables only internal routing");
        cfg.no_internal = false;
        cfg.no_subagents = true;
        CHECK(cfg.internal_routing() && cfg.orchestration() && !cfg.subagent_routing(),
              "escape hatch: NO_SUBAGENTS disables only subagent routing");
        cfg.no_subagents = false;
        CHECK(cfg.internal_routing() && cfg.orchestration() && cfg.subagent_routing(),
              "escape hatches are not sticky");
    }

    // 6. 连带努力偏差:积极偏差步骤努力 UP 负数 DOWN,
    //    被夹在模型上; 三角形没有留下任何痕迹。
    {
        const auto caps = ModelCapabilities::from_id("gpt-5");   // 支持努力
        // 标准 @ 中基,+1 偏差 → 高; -1 偏数 → 低.
        auto up = sm::effort_for_complexity(Effort::Medium, sm::Complexity::Standard, caps, +1);
        auto dn = sm::effort_for_complexity(Effort::Medium, sm::Complexity::Standard, caps, -1);
        auto mid = sm::effort_for_complexity(Effort::Medium, sm::Complexity::Standard, caps, 0);
        CHECK(static_cast<int>(up) > static_cast<int>(mid), "cascade: +bias raises effort");
        CHECK(static_cast<int>(dn) < static_cast<int>(mid), "cascade: -bias lowers effort");
        // 特里维亚尔忽略了积极的偏差——一个"一锤子"就是"一锤".
        auto triv = sm::effort_for_complexity(Effort::High, sm::Complexity::Trivial, caps, +2);
        CHECK(triv == Effort::None, "cascade: trivial stays None despite +bias");
    }

    // 8. CONTINUOUS 努力规模( 百分比) effort_for_score : 转弯 DEEP 输入
    //    复杂波段得到的精力比一个勉强进入它,分层边界与离散的
    //    effort_for_complexity完全吻合.
    {
        const auto caps = ModelCapabilities::from_id("gpt-5");
        // 边界平分:浅-Complex分(差值0) ===离散路径.
        sm::ComplexityScore shallow{sm::Complexity::Complex, 3, 0};
        sm::ComplexityScore deep   {sm::Complexity::Complex, 8, 5};
        auto e_shallow = sm::effort_for_score(Effort::Medium, shallow, caps, 0);
        auto e_deep    = sm::effort_for_score(Effort::Medium, deep,    caps, 0);
        auto e_tier    = sm::effort_for_complexity(Effort::Medium, sm::Complexity::Complex, caps, 0);
        CHECK(e_shallow == e_tier, "scored: shallow-Complex matches the discrete tier step");
        CHECK(static_cast<int>(e_deep) > static_cast<int>(e_shallow),
              "scored: deep-Complex thinks harder than shallow-Complex");
        // 深处 -Simple 下降比浅 -Simple.
        sm::ComplexityScore sh_simple{sm::Complexity::Simple, 0, 0};
        sm::ComplexityScore dp_simple{sm::Complexity::Simple, -4, 4};
        auto s_sh = sm::effort_for_score(Effort::High, sh_simple, caps, 0);
        auto s_dp = sm::effort_for_score(Effort::High, dp_simple, caps, 0);
        CHECK(static_cast<int>(s_dp) <= static_cast<int>(s_sh),
              "scored: deep-Simple drops at least as far as shallow-Simple");
        // 三角形仍然没有钉上,不管怎样 score/bias.
        sm::ComplexityScore triv_s{sm::Complexity::Trivial, -100, 100};
        CHECK(sm::effort_for_score(Effort::High, triv_s, caps, +2) == Effort::None,
              "scored: trivial pins to None");
    }

    // 7. resolve_subagent_role: a 工作线程 从来没有比父母更难思考
    //    复读器 – 策略返回父模式时, 母努力; 子剂包装器必须将其粘贴到 + 父( ) 不
    //    变量 : 副代理努力 ____________________;
    //    _____________________________ 工作线程 3月24日,纽约
    {
        sm::RoleConfig cfg; cfg.enabled = true;
        const char* parent = "gpt-5";   // 支持努力
        std::vector<ModelInfo> g5 = { mi("gpt-5"), mi("gpt-5-mini") };
        // 家长思维低:战略-routed审查者不得超过低.
        auto rev = sm::resolve_subagent_role(sm::ModelRole::Strategic, parent,
                                             Effort::Low, g5, cfg);
        CHECK(static_cast<int>(rev.effort) <= static_cast<int>(Effort::Low),
              "subagent: reviewer effort clamped to ≤ parent (Low)");
        // 父母努力 OFF ⁇  – 每一个 工作线程 角色也取消了。
        for (auto role : {sm::ModelRole::Strategic, sm::ModelRole::Implementation,
                          sm::ModelRole::Utility}) {
            auto p = sm::resolve_subagent_role(role, parent, Effort::None, g5, cfg);
            CHECK(p.effort == Effort::None,
                  "subagent: parent effort None ⇒ worker effort None");
        }
    }

    // ── classify_turn : 数据 一个 编组分类器 ──────────────────
    {
        using C = sm::Complexity;
        // 继续升空: "continue" 复杂工作必须完成之后 不 针头到Trivial
        // ( 这条路由 ) ( 返回的硬性任务 : Effort::None).
        auto cont = sm::classify_turn("continue", C::Complex, 0, 0);
        CHECK(cont.tier == C::Standard,
              "classify_turn: 'continue' after Complex inherits Standard");
        // ...但是一个 终端 阿克留在特里维奥尔——一个阿克就是一个".
        auto ack = sm::classify_turn("thanks", C::Complex, 0, 0);
        CHECK(ack.tier == C::Trivial,
              "classify_turn: 'thanks' after Complex stays Trivial");
        // ...还有续作 NO 硬背景也保持Trivial。
        auto cold = sm::classify_turn("continue", C::Simple, 0, 0);
        CHECK(cold.tier == C::Trivial,
              "classify_turn: 'continue' after Simple stays Trivial");
        // 校正层:"no, still broken" 永远不低于标准线.
        auto corr = sm::classify_turn("no, still broken", C::Simple, 0, 0);
        CHECK(static_cast<int>(corr.tier) >= static_cast<int>(C::Standard),
              "classify_turn: a correction floors at Standard");
        // 载荷 升起的作曲: "fix this" + 40KB 粘贴 → 复杂 -ish.
        auto pay = sm::classify_turn("fix this", C::Standard, 40 * 1024, 0);
        CHECK(static_cast<int>(pay.tier) >= static_cast<int>(C::Standard),
              "classify_turn: big payload lifts a chip-text turn");
        // is_continuation_cue : 恢复命令是, 终端 285)\fsp1}没有
        CHECK(sm::is_continuation_cue("retry"), "cue: retry resumes");
        CHECK(sm::is_continuation_cue("go ahead"), "cue: go ahead resumes");
        CHECK(!sm::is_continuation_cue("thanks"), "cue: thanks is terminal");
        CHECK(!sm::is_continuation_cue("commit"), "cue: commit is terminal");
        CHECK(!sm::is_continuation_cue(
                  "continue refactoring the parser and add tests"),
              "cue: long turns carry their own signal");
    }

    // ── AGENTTY_SMART_MODE 会话标 ────────────────────────
    // 解析只:域不再读取环境.
    // settings::registry::read_env 调用 盖恩夫的答案
    {
        CHECK(!sm::tuning::parse_enabled(nullptr).has_value(),
              "env pin: unset → no override (settings govern)");
        CHECK(!sm::tuning::parse_enabled("").has_value(),
              "env pin: empty → no override");
        CHECK(sm::tuning::parse_enabled("1") == std::optional<bool>{true},
              "env pin: 1 → forced on");
        CHECK(sm::tuning::parse_enabled("0") == std::optional<bool>{false},
              "env pin: 0 → forced off");
        CHECK(sm::tuning::parse_enabled("false") == std::optional<bool>{false},
              "env pin: 'false' → off");
        CHECK(sm::tuning::parse_enabled("on") == std::optional<bool>{true},
              "env pin: any non-falsy value → on");
        CHECK(sm::tuning::parse_disabled("1") && !sm::tuning::parse_disabled("off")
                  && !sm::tuning::parse_disabled(nullptr),
              "escape hatch: on unless unset or falsy");
    }

    // ── 数字路由政策: 解析, 夹住 ───────────
    //   1. unset QQ 虚构,所以调用者会掉入存储值.
    //   2. 套件值被夹在行的范围之内。
    //   3. 错误的值读作未设置, 所以在 shell 配置文件中存在一个字节
    //      设置所配置的值,而不是重置。
    {
        namespace d = sm::tuning::detail;
        using sm::tuning::kDeepMarginMin;
        using sm::tuning::kDeepMarginMax;
        CHECK(!d::parse_int(nullptr, kDeepMarginMin, kDeepMarginMax).has_value(),
              "tuning: unset ⇒ no override, the stored value governs");
        CHECK(d::parse_int("5", kDeepMarginMin, kDeepMarginMax) == std::optional<int>{5},
              "tuning: a set value is read");
        CHECK(d::parse_int("999", kDeepMarginMin, kDeepMarginMax)
                  == std::optional<int>{kDeepMarginMax},
              "tuning: above range clamps to the max");
        CHECK(d::parse_int("-7", kDeepMarginMin, kDeepMarginMax)
                  == std::optional<int>{kDeepMarginMin},
              "tuning: below range clamps to the min");
        CHECK(!d::parse_int("not-a-number", kDeepMarginMin, kDeepMarginMax).has_value(),
              "tuning: a malformed value reads as unset, not as a reset");
    }

    // ── RoleConfig 执行已解决的政策 ────────────────────────
    // 域名从配置读取这些,而不是从geenv读取,
    // 这就是让设置中设定的值 UI 默认值必须匹配调制音符,
    // 或者新调制音源 Model 与赤裸裸的路线不同 调用.
    {
        const sm::RoleConfig fresh;
        CHECK(fresh.deep_margin       == sm::tuning::kDeepMarginDefault,
              "RoleConfig: deep_margin defaults to the shipped value");
        CHECK(fresh.bias_clamp        == sm::tuning::kBiasClampDefault,
              "RoleConfig: bias_clamp defaults to the shipped value");
        CHECK(fresh.complex_threshold == sm::tuning::kComplexDefault,
              "RoleConfig: complex_threshold defaults to the shipped value");
    }

    // ── 调制参数实际上 MOVE 路线决定 ─────────
    // 一种长期存在的环境, 渲染 圆形 -trips 但没有改变比没有设置更糟糕。
    // 传输 路径( 选项) classify_turn / effort_for_score ),
    // 即对子 launch_stream 财务报告和已审计财务报表
    // build_smart_routing_card 两者 调用 -
    // 这么说吧 "card == wire" 现在覆盖调音。在运算默认值时,
    // 将卡片分类为 传输 使用用户的门槛值可以广告请求从未走过的路线.
    {
        const auto gpt = ModelCapabilities::from_id("gpt-5");
        const char* probe = "update the parser and re-run the formatter";

        // complex_threshold 移动阶梯边界。 classify_turn
        // — 该 两者均由编成的分类器组成 launch_stream 和路由卡 调
        // 用 - 而不是继续 classify_score 因为切口必须活下来
        // momentum/continuation/correction 层叠叠在它的上面。
        //
        // 曾经没有 classify_score 敬请赏赏,
        // classify_with_context 之后重写
        // -derived 等级 DEFAULT 并夸大其辞,故天台永存,
        // 而成周; -tripped 却一无所获 占领基地 调用 本来会很幸福的
        const auto at_default = sm::classify_turn(
            probe, sm::Complexity::Standard, 0, 0, sm::tuning::kComplexDefault);
        const auto at_low = sm::classify_turn(
            probe, sm::Complexity::Standard, 0, 0, /*complex_min=*/1);
        CHECK(at_low.tier == sm::Complexity::Complex,
              "WIRE: a lower cut escalates a borderline turn");
        CHECK(at_default.tier != at_low.tier,
              "WIRE: the threshold parameter survives the composed classifier");
        CHECK(sm::classify_turn(probe, sm::Complexity::Standard, 0, 0).tier
                  == at_default.tier,
              "WIRE: the default parameter is the shipped classification");

        // deep_margin 决定深波段是否获得额外阶梯。
        // "deep" 3,而不是8, 以比值表示, 而不是8,
        const sm::ComplexityScore deep{sm::Complexity::Complex, 12, 4};
        const Effort with_small = sm::effort_for_score(Effort::Low, deep, gpt, 0,
                                                       /*深处=*/3);
        const Effort with_large = sm::effort_for_score(Effort::Low, deep, gpt, 0,
                                                       /*深处=*/8);
        CHECK(static_cast<int>(with_small) > static_cast<int>(with_large),
              "WIRE: a smaller deep-band margin buys the extra effort step");
    }

    // 10. 该 EFFORT LADDER IS PER-MODEL, 不 GLOBAL.
    //
    //     返回到一个沉默的,严重的错误: 复杂度的缩放器沿着一个硬码的梯子 {无,
    //     低,中,高,X高,最大 } , OMITTED `minimal`
    //     Gpt-5的底部推理级。因此,一个在gpt-5上的用户在最小的努力下,
    //     每个标准都决心 Effort::None : 整个
    //     工作是缩放推理的特征是默默切换推理 OFF。镜像错误击中
    //     Claude ,它已经 NO `minimal` rung:
    //     从 Low 上下一步 Minimal ,
    //     和 clamp_effort 把它折回来 UP 至 Low,
    //     因此执行从未从 low 父母中真正退出。
    //
    //     电话 不变量 两者都违反:一步只能降落在模型
    //     实际接受的一跑上,而非 -Trivial 绝
    //     不能默默地消灭用户明确要求的各种努力。
    {
        const auto gpt = ModelCapabilities::from_id("gpt-5");
        // o3有一个 真实 缺少的梯子 `minimal` (低)中(高)
        // ——暴露幽灵的形状. -rung 一半的虫子,你给我看看 Claude
        // 4是第三个形状:完全没有努力梯级(仅关闭).
        const auto o3      = ModelCapabilities::from_id("o3");
        const auto claude4 = ModelCapabilities::from_id("claude-sonnet-4-20250514");

        // 房舍 如果这些地方不再保管 剩下的案子就空荡荡的了
        REQUIRE(agentty::effort_set_of(gpt) & agentty::effort_bit(Effort::Minimal));
        REQUIRE(agentty::effort_set_of(o3) & agentty::effort_bit(Effort::Low));
        REQUIRE(!(agentty::effort_set_of(o3) & agentty::effort_bit(Effort::Minimal)));
        REQUIRE(agentty::effort_set_of(claude4) == 0);

        // (a) 标准转弯为: NO-OP 每一个小块,包括底块。这是最纯洁的虫子,
        //     必须坚持 effort_for_score — 该 传输 PATH.
        //     launch_stream 财务报告和已审计财务报表
        //     build_smart_routing_card 两者 调用 和那个不
        //     一样 effort_for_complexity (简称: -circuits
        //     标准改为 `base` 它无条件地评价步骤(基地, tier_step
        //     + 额外),随着旧固定梯子,步(最小,0)从梯子上掉到索引 0 ==
        //     否:一个gpt-5用户谁选 `minimal` 是有道理的 说吧
        //     SILENTLY DISABLED 每一个正常的转弯。
        for (Effort base : {Effort::Minimal, Effort::Low, Effort::Medium,
                            Effort::High}) {
            const sm::ComplexityScore std_turn{sm::Complexity::Standard, 0, 0};
            CHECK(sm::effort_for_score(base, std_turn, gpt, 0) == base,
                  "WIRE: standard turn leaves effort untouched on every rung");
            CHECK(sm::effort_for_complexity(base, sm::Complexity::Standard, gpt, 0) == base,
                  "standard turn leaves effort untouched on every rung");
        }

        // (a2) 深处同样 -band 路径: 宽度大的标准
        //      转弯仍然能完全解决 `base`.
        {
            const sm::ComplexityScore deep_std{sm::Complexity::Standard, 0, 9};
            CHECK(sm::effort_for_score(Effort::Minimal, deep_std, gpt, 0) == Effort::Minimal,
                  "WIRE: a deep Standard turn still does not move effort");
        }

        // (b) 简单的转弯步骤 DOWN 完全通过一个RUNG的 自己的梯子。
        //     `None` (关闭) IS 一个合法的下边rung——
        //     用户可以在 选择面板 如此微小的 关闭 是正确的步骤,
        //     而不是错误。 错误是标准 默默地这样做, 上面已经覆盖了。
        CHECK(sm::effort_for_complexity(Effort::Minimal, sm::Complexity::Simple, gpt, 0)
                  == Effort::None,
              "simple turn steps down exactly one rung (minimal → off)");

        // (b2) 复合开关: gpt-5 from `minimal`
        //      达到 `low` 梯子的底部是行走的,不是跳的。
        {
            const sm::ComplexityScore cx_turn{sm::Complexity::Complex, 0, 0};
            CHECK(sm::effort_for_score(Effort::Minimal, cx_turn, gpt, 0) == Effort::Low,
                  "WIRE: complex turn steps minimal → low");
        }

        // (c) 步履走动 MODEL 运行,所以同一请求解决
        //     每个提供者的不同——这就是点。在gpt-5上,
        //     从Low下调一步是 `minimal` 低步直线
        //     关闭 而在固定的O3 下台之前 低步就产生了幻影
        //     Minimal ,哪个 clamp_effort
        //     断开 UP 至低级——因此,执行从未从低级父母中下台.
        CHECK(sm::detail::effort_step_down(Effort::Low, gpt) == Effort::Minimal,
              "gpt-5: one step down from low is minimal");
        CHECK(sm::detail::effort_step_down(Effort::Low, o3) == Effort::None,
              "o3: low steps to off, not to a minimal rung it does not have");
        CHECK(static_cast<int>(sm::detail::effort_step_down(Effort::Low, o3))
                  < static_cast<int>(Effort::Low),
              "o3: a step down from low actually descends (no phantom rung)");
        CHECK(sm::detail::effort_step_down(Effort::Medium, o3) == Effort::Low,
              "o3: medium steps down to low, its true adjacent rung");

        // (d) 模式与 NO 努力梯子总是会在每个层次
        //     上都落空,而且会偏见——智能模式绝不能为拒绝
        //     这个领域的模式创造推理水平。
        for (auto tier : {sm::Complexity::Trivial, sm::Complexity::Simple,
                          sm::Complexity::Standard, sm::Complexity::Complex})
            for (int b : {-2, 0, +2})
                CHECK(sm::effort_for_complexity(Effort::High, tier, claude4, b)
                          == Effort::None,
                      "a model without an effort ladder is always off");

        // (e) 每一可达到的阶梯地在某一水平上,模式 ACCEPTS。
        //     外部的一级 effort_set_of 将在提供
        //     方处写400(或默写)。
        for (Effort base : {Effort::None, Effort::Minimal, Effort::Low,
                            Effort::Medium, Effort::High, Effort::Xhigh,
                            Effort::Max})
            for (int n = -3; n <= 3; ++n)
                for (const auto& caps : {gpt, o3, claude4}) {
                    const Effort got = sm::detail::effort_step(base, n, caps);
                    CHECK((got == Effort::None
                           || (agentty::effort_set_of(caps) & agentty::effort_bit(got))),
                          "a step never lands on a rung the model lacks");
                }

        // (f) 单调递增的 : 不断提高, 不断降低努力, 不断提高。
        for (Effort base : {Effort::Minimal, Effort::Low, Effort::Medium, Effort::High}) {
            CHECK(static_cast<int>(sm::detail::effort_step(base, +1, gpt))
                      >= static_cast<int>(base), "step up never lowers");
            CHECK(static_cast<int>(sm::detail::effort_step(base, -1, gpt))
                      <= static_cast<int>(base), "step down never raises");
        }
    }
}

// ── 干线-turn 路线:梯子及其地板 ───────────────────────────
//
// 在现实存在之前,主要转向战略 分派 路径,所以分类器的判决只能
// 移动 EFFORT 拨号,永远不要用模特儿,577 -turn
// 追踪:角色=战略501,角色=无76,执行和效用从未,
// ZERO `task` 调用 (唯一能到达他们的路)——而其中
// 142个转弯(25%)是得分的 trivial/simple.
TEST_CASE("smart_mode: a pinned --model wins every role") {
    // #70:`agentty run --model X`将主转弯和别无其他.
    // 缩合和读取 -only 子代理仍然询问了阶梯路由器,
    // 所以一个电源被开具了它从未命名的模型的账单——而记者的支出
    // 限制就是它抓住的,而不是任何输出 agentty 生产。
    //
    // 智能模式的答案 "optimise cost for me" (
    // 首选) 针头答案 "is --model binding" (合同),
    // 它们是一个开关, 这就是为什么转动智能模式 OFF 并没有停止路
    // 由:它掉到阶梯路由器上。
    std::vector<agentty::ModelInfo> catalog;
    catalog.push_back({.id = agentty::ModelId{"claude-opus-4-5"}});
    catalog.push_back({.id = agentty::ModelId{"claude-haiku-4-5"}});

    const std::string parent = "claude-opus-4-5";

    // 解禁:廉价 -floor 角色路线 DOWN。这是默认,并保留默认
    // -- -- 即 TUI 永远不通过 --model.
    {
        sm::RoleConfig cfg;
        cfg.enabled = false;
        CHECK(sm::utility_model(parent, catalog, cfg) == "claude-haiku-4-5");
    }

    // 平板:每个角色都运行在用户命名上.
    {
        sm::RoleConfig cfg;
        cfg.enabled = false;
        cfg.pinned_model = true;
        CHECK_MESSAGE(sm::utility_model(parent, catalog, cfg) == parent,
                      "compaction honours the pin");
        CHECK_MESSAGE(sm::role_model(parent, catalog, cfg,
                                     agentty::ModelCapabilities::Tier::Mid) == parent,
                      "read-only subagents honour the pin");
        CHECK_MESSAGE(sm::role_model(parent, catalog, cfg,
                                     agentty::ModelCapabilities::Tier::Cheap) == parent,
                      "the cheapest floor honours it too");
    }

    // 标点比一个明确被钉住的智能模式槽要高:
    // 命令行比已保存偏好更具体.
    {
        sm::RoleConfig cfg;
        cfg.enabled = true;
        cfg.pinned_model = true;
        cfg.utility.set = true;
        cfg.utility.model = "claude-haiku-4-5";
        CHECK(sm::utility_model(parent, catalog, cfg) == parent);

        // 没有了针头,那个插槽还是赢的 ADDS 规则,
        // 它不会取代 Smart Mode。
        cfg.pinned_model = false;
        CHECK(sm::utility_model(parent, catalog, cfg) == "claude-haiku-4-5");
    }

    // resolve_role ()也必须尊重针头,这是手臂 最初的固定 MISSED
    // - 被添加到... - 被加到... role_model () 仅限。
    //
    // 这让虫子活在两条最重要的道路上 因为 resolve_role
    // 是什么,这是 MAIN TURN 通关( E)
    // cmd_factory's resolve_turn_routing
    // )和Lale 3b对副剂角色的用途。主转弯并不总是要求战略:
    // main_turn_role () 分数转弯,所以微不足道
    // 提示词 解析为执行或工具 -- -- 使用智能模式,
    // 解析为 sonnet/haiku 用户一直坚持着行动。 完全就是报告症状,
    // 幸存了本来应该结束的状态。
    //
    // 每个角色,被固定,有Smart Mode ON和
    // 针对它的插槽:答案是母,总是.
    {
        sm::RoleConfig cfg;
        cfg.enabled = true;
        cfg.pinned_model = true;
        cfg.implementation.set = true;
        cfg.implementation.model = "claude-sonnet-4-5";
        cfg.implementation.provider = "anthropic";
        cfg.utility.set = true;
        cfg.utility.model = "claude-haiku-4-5";
        cfg.utility.provider = "anthropic";

        for (const auto role : {sm::ModelRole::Strategic,
                                sm::ModelRole::Implementation,
                                sm::ModelRole::Utility}) {
            const auto p = sm::resolve_role(role, parent, Effort::None,
                                            catalog, cfg, "anthropic");
            CHECK_MESSAGE(p.model == parent,
                          "resolve_role honours --model for every role");
            CHECK_MESSAGE(!p.cross_provider(),
                          "a pinned role never retargets the provider");
        }

        // 未经标记,与Smart Mode 意图相同的配置
        // 路线 -- -- 证明上述断言是关于 PIN 而不是
        // 一个完全停止路线的解脱者
        cfg.pinned_model = false;
        CHECK(sm::resolve_role(sm::ModelRole::Implementation, parent,
                               Effort::None, catalog, cfg, "anthropic").model
              == "claude-sonnet-4-5");
        CHECK(sm::resolve_role(sm::ModelRole::Utility, parent,
                              Effort::None, catalog, cfg, "anthropic").model
              == "claude-haiku-4-5");
    }

    // 单人 -model 账户不受影响, 无论是哪种方式 -- -- 路由
    // 器从不运行路由 UP ,所以没有什么可反对的。
    {
        std::vector<agentty::ModelInfo> only;
        only.push_back({.id = agentty::ModelId{"claude-opus-4-5"}});
        sm::RoleConfig cfg;
        CHECK(sm::utility_model(parent, only, cfg) == parent);
        cfg.pinned_model = true;
        CHECK(sm::utility_model(parent, only, cfg) == parent);
    }

    // AGENTTY_SMART_NO_INTERNAL 现作为《京都议定书》
    // 压缩与标题生成继续使用主模型。
    // 它曾经只打开智能模式的插槽,而留下了级 回退 运行中——所以这个承诺是假的,
    // 而记者设定它完全期望着这个.
    {
        sm::RoleConfig cfg;
        cfg.enabled = true;
        cfg.no_internal = true;   // AGENTTY_SMART_NO_INTERNAL1 套装
        CHECK_MESSAGE(sm::utility_model(parent, catalog, cfg) == parent,
                      "NO_INTERNAL keeps utility turns on the main model");
        cfg.no_internal = false;
        CHECK_MESSAGE(sm::utility_model(parent, catalog, cfg)
                          == "claude-haiku-4-5",
                      "and unsetting it restores the downgrade");
    }
}

TEST_CASE("smart_mode: the main turn follows its complexity") {
    using sm::Complexity;
    using sm::ModelRole;

    // 梯子本身.
    CHECK(sm::role_for_complexity(Complexity::Trivial)  == ModelRole::Utility);
    CHECK(sm::role_for_complexity(Complexity::Simple)   == ModelRole::Utility);
    CHECK(sm::role_for_complexity(Complexity::Standard) == ModelRole::Implementation);
    CHECK(sm::role_for_complexity(Complexity::Complex)  == ModelRole::Strategic);

    // 默认下层( 通用) 允许全阶梯通过。
    {
        const auto f = ModelRole::Utility;
        CHECK(sm::main_turn_role(Complexity::Trivial,  f, true) == ModelRole::Utility);
        CHECK(sm::main_turn_role(Complexity::Standard, f, true) == ModelRole::Implementation);
        CHECK(sm::main_turn_role(Complexity::Complex,  f, true) == ModelRole::Strategic);
    }

    // 执行的一层楼拒绝比执行便宜——一个想要
    // 一个有能力的模型的人 -word 转身
    {
        const auto f = ModelRole::Implementation;
        CHECK(sm::main_turn_role(Complexity::Trivial,  f, true) == ModelRole::Implementation,
              "floor clamps a trivial turn UP to the floor");
        CHECK(sm::main_turn_role(Complexity::Simple,   f, true) == ModelRole::Implementation);
        CHECK(sm::main_turn_role(Complexity::Complex,  f, true) == ModelRole::Strategic,
              "the floor never clamps DOWN");
    }

    // 战略楼层完全恢复了旧行为
    {
        const auto f = ModelRole::Strategic;
        for (auto c : {Complexity::Trivial, Complexity::Simple,
                       Complexity::Standard, Complexity::Complex})
            CHECK(sm::main_turn_role(c, f, true) == ModelRole::Strategic,
                  "strategic floor = every turn on the flagship");
    }

    // 师开关打地.
    for (auto c : {Complexity::Trivial, Complexity::Complex})
        CHECK(sm::main_turn_role(c, ModelRole::Utility, /*路线=*/false)
                  == ModelRole::Strategic,
              "routing off pins the main turn to Strategic");
}

TEST_CASE("smart_mode: main-turn floor parses from settings") {
    sm::RoleConfig cfg;
    CHECK(cfg.main_turn_floor_role() == sm::ModelRole::Utility,
          "default is the full ladder");

    cfg.main_turn_floor = "implementation";
    CHECK(cfg.main_turn_floor_role() == sm::ModelRole::Implementation);
    cfg.main_turn_floor = "strategic";
    CHECK(cfg.main_turn_floor_role() == sm::ModelRole::Strategic);

    // 手-edited设置文件不得静默禁用路由.
    cfg.main_turn_floor = "nonsense";
    CHECK(cfg.main_turn_floor_role() == sm::ModelRole::Utility,
          "an unrecognised floor falls back to the full ladder");
    cfg.main_turn_floor.clear();
    CHECK(cfg.main_turn_floor_role() == sm::ModelRole::Utility);
}

// ── 自定义主机( O) Ollama / OpenAI-compat : 字段错误 ──────────────────
//
// 用户在一周内烧毁了两家Ollama账户:
// Utility 和 Implementation 模型都未使用；无论怎样调环境变量，主要工作仍由最昂贵模型完成。
// 有两个独立的原因,
// 都在这里:
//
//   1. tier() 返回( E) Tier::Mid 费用 每个 不知名的家庭,所以一个480B和一个
//      4B是同一级的,Utity要求的模型严格在父母的级以下;
//      没有东西在中层以下,所以它倒回到了父母身上.
//   2. 执行需要严格-Mid模式,并空还原
//      或,而打电话者读作 "use the parent".
//      A 目录 一个大型号加上小型号(没有14-69B带)的
//      -escalation 路径。
TEST_CASE("smart_mode: custom hosts de-escalate") {
    using Tier = ModelCapabilities::Tier;

    // ID的大小标记是自定义主机给我们的唯一的强度信号.
    CHECK(ModelCapabilities::tier_for("qwen3-coder:480b") == Tier::Flagship,
          "480b is flagship-class");
    CHECK(ModelCapabilities::tier_for("kimi-k2:1t") == Tier::Flagship,
          "trillion-scale tags parse (1t == 1000b)");
    CHECK(ModelCapabilities::tier_for("qwen3:32b") == Tier::Mid,
          "32b is the mid workhorse lane");
    CHECK(ModelCapabilities::tier_for("qwen3:8b") == Tier::Cheap,
          "8b is the cheap lane");
    // 宿主身份证明没有大小标记,必须不受所有这一切的影响.
    CHECK(ModelCapabilities::tier_for("claude-opus-4-5") == Tier::Flagship,
          "hosted flagship unchanged");
    CHECK(ModelCapabilities::tier_for("claude-sonnet-4-5") == Tier::Mid,
          "hosted mid unchanged");

    sm::RoleConfig cfg;
    cfg.enabled = true;

    // A:大母型+真中型+小型.
    {
        std::vector<ModelInfo> cat = {
            mi("qwen3-coder:480b", 128000), mi("qwen3:32b", 128000),
            mi("qwen3:8b", 128000),
        };
        const char* parent = "qwen3-coder:480b";
        auto s = sm::resolve_role(sm::ModelRole::Strategic, parent, Effort::High, cat, cfg);
        auto i = sm::resolve_role(sm::ModelRole::Implementation, parent, Effort::High, cat, cfg);
        auto u = sm::resolve_role(sm::ModelRole::Utility, parent, Effort::High, cat, cfg);
        CHECK(s.model == parent, "strategic stays on the parent");
        CHECK(i.model != parent, "implementation DE-ESCALATES off the parent");
        CHECK(u.model != parent, "utility DE-ESCALATES off the parent");
        CHECK(i.model == "qwen3:32b", "implementation takes the mid tier");
    }

    // B:完全没有中层——没有de-escalation路径的形状.
    {
        std::vector<ModelInfo> cat = {
            mi("qwen3-coder:480b", 128000), mi("qwen3:8b", 128000),
            mi("qwen3:4b", 128000),
        };
        const char* parent = "qwen3-coder:480b";
        auto i = sm::resolve_role(sm::ModelRole::Implementation, parent, Effort::High, cat, cfg);
        auto u = sm::resolve_role(sm::ModelRole::Utility, parent, Effort::High, cat, cfg);
        CHECK(i.model != parent, "no mid tier still de-escalates implementation");
        CHECK(u.model != parent, "no mid tier still de-escalates utility");
        // 执行绝不应 WEAKER 与工具值相同。 -tier 链
        // 接曾经降为字母比较, 由此可见。 `4b` 之前的
        // `8b` 并颠倒了这两个角色。
        CHECK(i.model == "qwen3:8b", "implementation takes the STRONGEST below parent");
    }

    // C:一个单一的固定模型必须仍然降解到"everything on the parent"
    // ——没有发明的路线,没有回归.
    {
        std::vector<ModelInfo> cat = { mi("qwen3-coder:480b", 128000) };
        const char* parent = "qwen3-coder:480b";
        for (auto r : {sm::ModelRole::Strategic, sm::ModelRole::Implementation,
                       sm::ModelRole::Utility}) {
            auto p = sm::resolve_role(r, parent, Effort::High, cat, cfg);
            CHECK(p.model == parent, "single-model catalog pins every role");
        }
    }

    // D: 角色可能永远无法走过 UP。这不是变量
    // 执行 回退 安全无虞.
    {
        std::vector<ModelInfo> cat = {
            mi("deepseek-v3.1:671b", 128000), mi("qwen3:32b", 128000),
        };
        // 父母是SMALLER模式;绝对不能选择更大的模式。
        auto i = sm::resolve_role(sm::ModelRole::Implementation, "qwen3:32b",
                                  Effort::High, cat, cfg);
        auto u = sm::resolve_role(sm::ModelRole::Utility, "qwen3:32b",
                                  Effort::High, cat, cfg);
        CHECK(i.model != "deepseek-v3.1:671b", "implementation never routes up");
        CHECK(u.model != "deepseek-v3.1:671b", "utility never routes up");
    }
}
