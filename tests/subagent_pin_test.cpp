// subagent_pin_test ——必须尊重被钉住的智能模式插
// 槽 首次 不仅在成功模式之后 -catalog 获取。
//
// 错误( 报告针对 GitHub 副驾驶:用户将执行钉到特定模型上,
// 智能模式已启动,第一个 `task` 副代理发送方式完全不同,
// 提供商甚至可能无法在端点上服务(gpt-5.x)。
// -codex id,只有答复 API 代表团请接受,
// 所以代表团400人。
//
// 根因: 子代理路由器保持自己的进程 -global 快照 智能模式配置( 包含) (
// tools::subagent::Config::smart ),因为 `task` 运行在 a
// 工作线程 线程 无法进入 状态更新函数's Model. main() 调用 install()
// 带 DEFAULT-CONSTRUCTED RoleConfig (网易=假,
// 无槽)和 init() 水合物 m.d.smart 发件人 settings.json 永远不要推下去,
// 只有两条路 调用 set_smart():
//
//   1. persist_settings() — 当用户编辑此会话的智能模式时点火
//   2. 国家 ModelsLoaded 状态更新函数 手臂——只向一个 SUCCESSFUL 获取
//
// 所以一个已经是针头的用户 已保存 参加前一届会议的委员,
// 目录 获取失败/ 返回空 / 被一个提供者切换器取代( 三个都是早期
// 的) -returns 之前的 set_smart ),
// 代表使用access=假- i.e。针头被默默忽略, 并且是级自动
// -router 随便选什么
//
// 这个测试能驱动真正的 接口边界 : 安装配置方式 main()
// 并坚持解析 尊重针。

#include <string>
#include <vector>

#include "agtest.hpp"

#include "agentty/domain/smart_mode.hpp"
#include "agentty/tool/subagent.hpp"

using namespace agentty;

namespace {

ModelInfo mi(std::string id, int ctx = 200000) {
    ModelInfo m;
    m.id = ModelId{std::move(id)};
    m.context_window = ctx;
    return m;
}

// 代理键键入路由器所决定的。 `Implementation` 是角色为
// coder/tester/general 工作线程 映射到 — 错误报告中的一个。
smart::RoleConfig pinned_impl(const std::string& model) {
    smart::RoleConfig cfg;
    cfg.enabled = true;
    cfg.implementation.model = model;
    cfg.implementation.set   = true;
    return cfg;
}

} // 命名空间结束。

TEST_CASE("subagent honours a pinned Smart Mode slot from the first turn") {
    const std::vector<ModelInfo> candidates = {
        mi("gpt-5.3-codex"), mi("luna-2"), mi("gpt-4o-mini"),
    };

    // ── 解脱者本身是正确的:一个针直接获胜 ──────────
    // (保证合同的固定——如果这种情况再倒退,
    // 推倒配置是不够的).
    {
        const auto cfg = pinned_impl("luna-2");
        const auto p = smart::resolve_role(smart::ModelRole::Implementation,
                                           "gpt-5.3-codex", Effort::None,
                                           candidates, cfg);
        CHECK(p.model == "luna-2",
              "resolve_role: an explicit slot pin beats the parent model");
    }

    // ── 该 BUG:main()的install()离开`smart` 默认-constructed ──
    // 复制所报故障,无任何 UI : 安装精确的方式 main() ,
    // 然后询问路由器 工作线程 将会继续前进。
    {
        tools::subagent::install(tools::subagent::Config{
            .model = "gpt-5.3-codex",
            .installed = true,
        });
        tools::subagent::set_candidates(::agentty::IoAccess::grant(), candidates);

        const auto cfg = tools::subagent::current();
        CHECK(!cfg.smart.enabled,
              "precondition: install() alone leaves Smart Mode off in the "
              "subagent router — this is what the fix must close");

        // 使用启用=假名后无法看到此标针: 每个角色都会通过母模式。
        // 即观察到的bug – 工作线程运行于 gpt-5.3-codex
        // ( 对应 -only on Copilot) 而不是该标针。
        const auto p = smart::resolve_role(smart::ModelRole::Implementation,
                                           cfg.model, Effort::None,
                                           cfg.candidates, cfg.smart);
        CHECK(p.model == "gpt-5.3-codex",
              "with a stale RoleConfig the pin is ignored (the bug)");
    }

    // ── 该 FIX :启动将水分补充配置推向下 ───────────
    {
        tools::subagent::set_smart(::agentty::IoAccess::grant(), pinned_impl("luna-2"));
        const auto cfg = tools::subagent::current();
        REQUIRE(cfg.smart.enabled);

        const auto p = smart::resolve_role(smart::ModelRole::Implementation,
                                           cfg.model, Effort::None,
                                           cfg.candidates, cfg.smart);
        CHECK(p.model == "luna-2",
              "after set_smart the first delegation honours the pin");
        CHECK(cfg.smart.implementation.set, "the slot survived the push-down");
    }

    // ── 针头必须活下来 FAILED 目录 获取 ──────────────
    // 电话 ModelsLoaded arm 返回在错误 / 空 /
    // 提供者不匹配时提前, 所以它不能是唯一能保持路由器同步的事物。
    // 候选人要空( 失败的获取) 绝不能重现自动。 -router.
    {
        tools::subagent::set_candidates(::agentty::IoAccess::grant(), {});
        const auto cfg = tools::subagent::current();
        const auto p = smart::resolve_role(smart::ModelRole::Implementation,
                                           cfg.model, Effort::None,
                                           cfg.candidates, cfg.smart);
        CHECK(p.model == "luna-2",
              "an empty/failed catalog must not discard the user's pin");
    }

    // ── 即使是模特儿不在 也值得一提 `candidates` ───
    // 被绑定的ID是用户的明示指示,而不是排名输入:
    // resolve_role 不得 倒车 -router 是
    // 因为... 目录 接头没有降落(或者没有这个身份证明就降落),
    // 这是副驾驶报告的确切形状——针形符, 目录 不完整。
    {
        tools::subagent::set_smart(::agentty::IoAccess::grant(), pinned_impl("luna-2"));
        tools::subagent::set_candidates(::agentty::IoAccess::grant(), {mi("gpt-5.3-codex")});   // 缺少 pin 符号
        const auto cfg = tools::subagent::current();
        const auto p = smart::resolve_role(smart::ModelRole::Implementation,
                                           cfg.model, Effort::None,
                                           cfg.candidates, cfg.smart);
        CHECK(p.model == "luna-2",
              "a pin outranks the candidate list — it is an instruction, "
              "not a hint");
    }

    // ── 来,来,来 UNPINNED 仍然自动插槽 -routes (修复不是针) -everything) ──
    {
        smart::RoleConfig cfg;
        cfg.enabled = true;              // 打开,但没有插槽
        tools::subagent::set_smart(::agentty::IoAccess::grant(), cfg);
        tools::subagent::set_candidates(::agentty::IoAccess::grant(), candidates);
        const auto c = tools::subagent::current();
        const auto p = smart::resolve_role(smart::ModelRole::Utility,
                                           "gpt-5.3-codex", Effort::None,
                                           c.candidates, c.smart);
        CHECK(!p.model.empty(),
              "an unpinned role still resolves through the auto-router");
        CHECK(p.effort == Effort::None,
              "utility workers never carry a reasoning budget");
    }

    // ── install() 能够携带配置, 对于路径没有 Model ───
    // `agentty run`和`agentty acp`从未建造Model,
    // 因此它们从未达到init()的push-down——main()必须
    // 在安装时间上种子Smart Mode,或者无头代表团默默地忽略每一个针头.
    // 无头跑都是代表团,所以这是角色路线最重要的地方.
    {
        tools::subagent::install(tools::subagent::Config{
            .model = "gpt-5.3-codex",
            .installed = true,
            .smart = pinned_impl("luna-2"),
        });
        tools::subagent::set_candidates(::agentty::IoAccess::grant(), candidates);
        const auto cfg = tools::subagent::current();
        CHECK(cfg.smart.enabled,
              "install() carries Smart Mode for the Model-less entry points");
        const auto p = smart::resolve_role(smart::ModelRole::Implementation,
                                           cfg.model, Effort::None,
                                           cfg.candidates, cfg.smart);
        CHECK(p.model == "luna-2",
              "headless / ACP delegation honours the pinned slot");
    }

    // ── 针头是的 不 交叉提供者 ──────────────────────────────
    // 上面的镜像 和真实的 分派 错误: 模型 id 只对服务它的终点有意义。
    // Name 选择面板 拒绝 SELECT 十字架 -provider 划,
    // 但针 PERSISTS 改为 settings.json
    // - 这么一针 Anthropic 用户切换到另一个提供
    // 者后默默重播,发送了一个提供者从未听说过的ID。
    // 每转(和每个代表团)400秒,直到用户找到智能模式 覆盖面板.
    //
    // 固定记录提供者 ON 针和瞄准镜在它上重放,这是故意的 不 a
    // 目录-membership ——这将会打破
    // empty/stale-catalog 以上所述案件。
    {
        smart::RoleConfig cfg = pinned_impl("claude-opus-4-5");
        cfg.implementation.provider = "anthropic";

        // 同样的提供者 —— 针号适用, 完全如前。
        const auto same = smart::resolve_role(
            smart::ModelRole::Implementation, "claude-opus-4-5", Effort::None,
            candidates, cfg, "anthropic");
        CHECK(same.model == "claude-opus-4-5",
              "a pin still applies under the provider it was made on");

        // 不同的提供者 – 外国身份证明不得 到达 传输.
        const auto other = smart::resolve_role(
            smart::ModelRole::Implementation, "gpt-5.3-codex", Effort::None,
            candidates, cfg, "openai");
        CHECK(other.model != "claude-opus-4-5",
              "an Anthropic pin is never dispatched to another provider");
        CHECK(!other.model.empty(),
              "a foreign pin falls back to a real model, never to empty");

        // 来,来,来 UNKNOWN-provenance 针(
        // 针头是提供者之前的设置) -scoped ) 各地保持荣誉——
        // 升级用户不得默默丢失其胸针.
        smart::RoleConfig legacy = pinned_impl("luna-2");   // . 提供商 = ""
        const auto up = smart::resolve_role(
            smart::ModelRole::Implementation, "gpt-5.3-codex", Effort::None,
            candidates, legacy, "openai");
        CHECK(up.model == "luna-2",
              "a pre-upgrade pin with no recorded provider is still honoured");

        // 同样,当CALLER不知道提供者(空格):
        // 绝对是比以前少的惊喜,从来没有比以前多过.
        const auto unknown_caller = smart::resolve_role(
            smart::ModelRole::Implementation, "gpt-5.3-codex", Effort::None,
            candidates, cfg, "");
        CHECK(unknown_caller.model == "claude-opus-4-5",
              "an unknown active provider preserves the old honour-always path");

        // 副剂进入点范围相同——代表团是报告的症状,
        // 因此, 工作线程 路径, 不只是主转弯。
        const auto worker = smart::resolve_subagent_role(
            smart::ModelRole::Implementation, "gpt-5.3-codex", Effort::None,
            candidates, cfg, "openai");
        CHECK(worker.model != "claude-opus-4-5",
              "a delegation never inherits a foreign-provider pin either");
    }
}
