// 面板导航是 STACK: Esc 走路回来的方式你进来。
//
// 故障的这个针被运出,
// 据报道是"Esc drops me all the way out instead of back to the panel I came from".
// 开一个面板有两个不同含义的拼写:
//
//     m.ui.panel.descend(pn::X {...}});// 隐藏父方 - Esc 解风
//     m.ui.panel.descend(pn::X{...});          // DROPS 说起来... Esc 完全离开
//
// 以及哪个a 状态更新函数 旧的是一个硬币翻转: 8
// Open* 处理函数 降级,分配13个。 Esc 离开
// 面板堆栈,而调色板 → 模型 → Esc 同样的姿态,
// 两种行为,决定你碰巧是哪个开阔的。
//
// 现在删除了任务,所以调用网站必须说descend()(
// 开放)或restore()(回击,包括链条)——但值得
// 打针的财产是行为,而不是综合。这些案例驱动REAL
// 状态更新函数通过真正的Msg类型,因为这是用户遇到的级别。

#include "agtest.hpp"

#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/panel/slot.hpp"

#include <print>
#include <optional>
#include <string>
#include <vector>

namespace {

namespace pn = agentty::ui::panel;
using agentty::Model;
using agentty::Msg;

agentty::store::Settings g_settings;

// 电话 状态更新函数 插入的覆盖区域 IO 海军的测试不希望发生任何事情
void install_stub_deps() {
    using namespace agentty;
    app::install_deps(app::Deps{
        .stream         = [](const maya::unlocked&, auto, auto) {},
        .save_thread    = [](const auto&) {},
        .delete_thread  = [](const auto&) {},
        .load_threads   = [] { return std::vector<Thread>{}; },
        .load_thread    = [](const auto&) -> std::optional<Thread> { return std::nullopt; },
        .load_settings  = [] { return g_settings; },
        .save_settings  = [](const store::Settings& x) { g_settings = x; },
        .new_thread_id  = [] { return ThreadId{}; },
        .title_from     = [](std::string_view t) { return std::string{t}; },
        .auth           = auth::AuthHeader{auth::ApiKeyHeader{std::string{}}},
    });
}

[[nodiscard]] Model step(Model m, Msg msg) {
    return std::move(agentty::app::update(std::move(m), std::move(msg)).first);
}

}  // 命名空间结束。

TEST_CASE("panel nav: Esc returns to the panel you came from") {
    install_stub_deps();
    // 被举报的错误, 结束为结束: 打开调色板, 打开其中的提供者,
    // 按 Esc 你应该回到调色板里,而不是 线程.
    Model m;
    m = step(std::move(m), Msg{agentty::OpenPalette{}});
    REQUIRE(m.ui.panel.is<pn::Palette>());
    CHECK(m.ui.panel.depth() == 1);

    m = step(std::move(m), Msg{agentty::OpenProviders{}});
    REQUIRE(m.ui.panel.is<pn::Providers>());
    // 深两块板子:调色板藏在下面.
    CHECK(m.ui.panel.depth() == 2);

    m = step(std::move(m), Msg{agentty::CloseProviders{}});
    CHECK(m.ui.panel.is<pn::Palette>());   // QQ 错误: 曾经是无
    CHECK(m.ui.panel.depth() == 1);

    // 一会儿 Esc 叶子,因为调色板是打开的 线程.
    m = step(std::move(m), Msg{agentty::ClosePalette{}});
    CHECK(!m.ui.panel.any_open());
    CHECK(m.ui.panel.depth() == 0);
}

TEST_CASE("panel nav: a panel opened directly still closes on Esc") {
    install_stub_deps();
    // 合同的另一半。 降级到无处隐藏什么,
    // 所以 Esc 关闭而不是试图恢复一个从未存在的父方。
    // 堆栈只能打开风, 就会困在面板中。
    Model m;
    m = step(std::move(m), Msg{agentty::OpenProviders{}});
    REQUIRE(m.ui.panel.is<pn::Providers>());
    CHECK(m.ui.panel.depth() == 1);

    m = step(std::move(m), Msg{agentty::CloseProviders{}});
    CHECK(!m.ui.panel.any_open());
}

TEST_CASE("panel nav: the chain unwinds one level per Esc") {
    // 深度是合同实际涉及的财产,
    // 因此直接断言而不是推断出合同上方是哪个面板.
    pn::State s;
    s.descend(pn::Palette{});
    s.descend(pn::Models{{0, ""}});
    s.descend(pn::Providers{{0}});
    CHECK(s.depth() == 3);

    CHECK(s.ascend());
    CHECK(s.is<pn::Models>());
    CHECK(s.depth() == 2);

    CHECK(s.ascend());
    CHECK(s.is<pn::Palette>());
    CHECK(s.depth() == 1);

    // 堆底:没有恢复,所以呼叫者关闭.
    CHECK(!s.ascend());
}

TEST_CASE("panel nav: re-opening the same panel rebuilds, it does not nest") {
    // A 状态更新函数 重新打开自己的面板以刷新它(
    // Smart Mode在每一个槽任务上都这样做) 的
    // 面板不能隐藏为自己的母—— Esc 一次剥一张一样的
    // 看上去好像没什么用
    pn::State s;
    s.descend(pn::Palette{});
    s.descend(pn::Models{{0, ""}});
    CHECK(s.depth() == 2);

    s.descend(pn::Models{{7, ""}});          // 同类,新内容
    CHECK(s.depth() == 2);                   // 不 3 3 导弹发射装置
    CHECK(s.get<pn::Models>()->index == 7);

    // ...继承的父子还是调色板
    CHECK(s.ascend());
    CHECK(s.is<pn::Palette>());
}

TEST_CASE("panel nav: restore puts a panel back without re-stashing it") {
    // descend() 财务报告和已审计财务报表 restore()
    // 只有两种方式可以使用,不能互换: 降下的恢复会把孩子藏起来
    // 当做自己的父母,所以 Esc 将循环在两本之间,而不是离开。
    pn::State s;
    s.descend(pn::Palette{});
    s.descend(pn::Models{{3, ""}});

    auto snapshot = *s.get<pn::Models>();
    s.restore(std::move(snapshot));
    CHECK(s.depth() == 2);                   // 不变,不是3个

    CHECK(s.ascend());
    CHECK(s.is<pn::Palette>());
    CHECK(!s.ascend());
}

TEST_CASE("panel nav: a sideways hop keeps where you came from") {
    // 第二链-droppingbug, 和比任务更微妙的。
    //
    // 跳跃在兄弟间 选择面板 (^P 从模型 选择面板 向提供者提供 选择面板 )
    // 拼写 `close<Models>(); descend(Providers{});`.
    // 写得像一个交换, 行为像一个短句: close() 空空的空位,
    // 所以, descend() 接下来是没有什么可以隐藏的,
    // 并且悄悄地放下了祖父母。 调色板 – 模型 – ^P →
    // Esc 然后将面板堆栈完全留下,而不是返回调色板。
    //
    // replace() 动作是写着 "sideways" :你离开
    // 的面板走,下面的面板没有.
    pn::State s;
    s.descend(pn::Palette{});
    s.descend(pn::Models{{0, ""}});
    CHECK(s.depth() == 2);

    s.replace(pn::Providers{{4}});
    CHECK(s.is<pn::Providers>());
    CHECK(s.depth() == 2);                   // 不 1 — 调色板存活下来
    CHECK(s.get<pn::Providers>()->index == 4);

    CHECK(s.ascend());
    CHECK(s.is<pn::Palette>());
}

TEST_CASE("panel nav: a hop from nothing is just an open") {
    // replace() 无人继承,所以它就落在了
    // descend() 将说:深一层, Esc 关上,兄弟兄弟开业时,
    // 必须先跳, 不仅仅是中场 -stack.
    pn::State s;
    s.replace(pn::Providers{{0}});
    CHECK(s.is<pn::Providers>());
    CHECK(s.depth() == 1);
    CHECK(!s.ascend());                      // 没有什么可以解开的
}

TEST_CASE("panel nav: the three moves are distinguishable") {
    // 降/ 替换/ 恢复不同 仅 母链上发生的事情
    // 恰恰就是其中的一部分 调用 站点总是出现错误。
    // 将所有三个都按同一起始堆栈键,这样差异就成为测试的主题,
    // 而不是一个 副作用 说到做到
    auto two_deep = [] {
        pn::State s;
        s.descend(pn::Palette{});
        s.descend(pn::Models{{1, ""}});
        return s;
    };

    auto down = two_deep();
    down.descend(pn::Providers{{0}});
    CHECK(down.depth() == 3);                // DOWN : 一个更深

    auto side = two_deep();
    side.replace(pn::Providers{{0}});
    CHECK(side.depth() == 2);                // SIDEWAYS: 深度相同

    auto up = two_deep();
    auto same = *up.get<pn::Models>();
    up.restore(std::move(same));
    CHECK(up.depth() == 2);                  // UP/in-place : 不变

    // 三者都留下一个堆叠,仍能解风到调色板上.
    for (auto* s : {&down, &side, &up}) {
        while (s->depth() > 1) CHECK(s->ascend());
        CHECK(s->is<pn::Palette>());
    }
}

// ── 寻呼语法 ────────────────────────────────────────────────────────
//
// Home/End/PageUp/PageDown 属于 SHARED 语法,
// 但只有一些 覆盖面板 绑定 a `jump`。其余的移动(±1000)
// 和移动(± page_step ),它只达到目的,
// 因为 ui::step 夹住一个多 -row 三角洲而不是包起来,
// 这是两个 -part 合同覆盖两个文件,所以它得到测试而不是评论。

TEST_CASE("panel nav: step wraps on single rows, clamps on jumps") {
    // 单步 WRAP :从最后一排下来的降落在第一排.
    CHECK(agentty::ui::step(4, +1, 5) == 0);
    CHECK(agentty::ui::step(0, -1, 5) == 4);
    // 页次 -sized 步骤 CLAMP - 从下到上跳下去
    // 会失去你的位置
    CHECK(agentty::ui::step(0, +10, 5) == 4);
    CHECK(agentty::ui::step(4, -10, 5) == 0);
    // 电话 Home/End 回退 费用 覆盖面板 没有
    // `jump` 如果有步骤包裹这些 End 将会降落在0号线上
    // Home 最后一行,正反,安静。
    CHECK(agentty::ui::step(2, +1000000, 5) == 4);
    CHECK(agentty::ui::step(2, -1000000, 5) == 0);
    // 排出式列表不得超出范围索引.
    CHECK(agentty::ui::step(0, +1, 0) == 0);
    CHECK(agentty::ui::step(0, +1000000, 1) == 0);
}

TEST_CASE("panel nav: Home/End reach the ends in a jump-less picker") {
    install_stub_deps();
    // 检查站 选择面板 绑定 `move` 财务报告和已审计财务报表 `page_step` 但没有
    // `jump` ,所以,它的,因为 Home/End 以巨大的动作到达。 End-to-end
    // 通过 状态更新函数 因为夹子是用来装的 step() 编辑 绑定 住在这里 nav.hpp.
    Model m;
    std::vector<agentty::checkpoints::Entry> entries;
    for (int i = 0; i < 7; ++i) {
        agentty::checkpoints::Entry e;
        e.turn    = i + 1;
        e.preview = "turn " + std::to_string(i + 1);
        entries.push_back(std::move(e));
    }
    m.ui.panel.descend(pn::Checkpoints{{std::move(entries), 3}});
    REQUIRE(m.ui.panel.get<pn::Checkpoints>()->index == 3);

    m = step(std::move(m), Msg{agentty::CheckpointsMove{+1000000}});
    CHECK(m.ui.panel.get<pn::Checkpoints>()->index == 6);   // End

    m = step(std::move(m), Msg{agentty::CheckpointsMove{-1000000}});
    CHECK(m.ui.panel.get<pn::Checkpoints>()->index == 0);   // Home

    // 从第0行到最后一行还有一步是WRAPS.
    m = step(std::move(m), Msg{agentty::CheckpointsMove{-1}});
    CHECK(m.ui.panel.get<pn::Checkpoints>()->index == 6);
}

TEST_CASE("panel nav: opening is spelled, never implied") {
    // 合成半。 指定曾经是第三种方式, 静静地放下链条;
    // 它被删除, 因此一个 调用 网站必须列出其中的哪个。
    // 如果有人恢复操作员=, 停止编译,
    // 上面的行为案例又变成了硬币翻转。
    static_assert(!std::is_assignable_v<pn::State&, pn::Palette>);
    static_assert(!std::is_assignable_v<pn::State&, pn::Models>);
    // 两种真正的拼写都可用.
    static_assert(requires (pn::State s) { s.descend(pn::Palette{}); });
    static_assert(requires (pn::State s) { s.restore(pn::Palette{}); });
    std::println("PASS\n");
}
