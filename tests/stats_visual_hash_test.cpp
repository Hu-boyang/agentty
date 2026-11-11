// stats_visual_hash_test - 电话 渲染 门必须 SEE 统计的查看器。
//
// maya '运行循环跳过整个 view()+render()
// 组合当 AgenttyApp::visual_hash 门是便宜的,
// 它也是一个陷阱门: 任何视图读到的状态,但散列不混合 产生一个模型的
// 循环 调用 "visually identical" ,所以,
// 请 帧 跳过,更改只到屏幕上,当有 UNRELATED 散列轴翻转 --
// -- ~ 265 ms 光标-blink 等价键,或下一个键键键。
//
// 症状不是 "slow"。
// 这是 "the panel ignored me, then caught up when I typed":
// Esc 似乎没有工作(小组) HAS 关闭; 屏幕未被告知),
// 滚动记录器每几台按下一次, 标签切换器延迟降落。 更改日志中记录的正是此。
// 选择面板 ; 数据查看器运出时, 同一个孔。
//
// 看上去断断续续 因为有分页 FITS 编号 可视区域 将每
// 个卷轴夹在同一个y上,并且真的没有变化——所以只有高的标签
// 行为不当。
//
// 这些定着合同:用户改变的模型不能与他们
// 改变的模型相等。

#include "agtest.hpp"

#include "agentty/runtime/app/program.hpp"
#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/msg.hpp"

namespace pn = agentty::ui::panel;

using namespace agentty;
using agentty::app::AgenttyApp;

namespace {

// 观众打开了 线程 转弯足够多,可以使用多个标签,
// 而且比一个高 可视区域.
Model open_viewer() {
    Model m;
    Thread t;
    t.id = ThreadId{"vh"};
    for (int i = 0; i < 24; ++i) {
        Message u;
        u.role = Role::User;
        u.text = "ask";
        t.messages.push_back(std::move(u));

        Message a;
        a.role         = Role::Assistant;
        a.served_model = ModelId{"claude-sonnet-4-5"};
        a.text         = "answer";
        Message::Telemetry tel;
        tel.input_tokens   = 900;
        tel.output_tokens  = 300;
        tel.cache_read     = 12000;
        tel.ttft_ms        = 120;
        tel.stream_ms      = 800;
        a.telemetry = tel;
        t.messages.push_back(std::move(a));
    }
    m.d.current = std::move(t);

    auto [opened, _] = app::update(std::move(m), Msg{OpenStats{}});
    // 绑定滚动内: 渲染器 画完后再发表
    // 状态更新函数 夹住它。
    opened.ui.panel.get<pn::Stats>()->scroll.max_y = 20;
    // 结构-binding名称是该成员类型的LVALUE——在返回时没有隐含
    // 的移动(不同于一个命名的本地名称). Model是move-only
    // (其冻结的ScrollbackLedger是非-copyable),
    // 所以移动必须明确.
    return std::move(opened);
}

}  // 命名空间结束。

TEST_CASE("stats gate: scrolling advances the render hash") {
    Model m = open_viewer();
    const std::uint64_t before = AgenttyApp::visual_hash(m);

    auto [after, _] = app::update(std::move(m), Msg{StatsScroll{+3}});
    REQUIRE(after.ui.panel.get<pn::Stats>()->scroll.y == 3);  // 真的很动了

    CHECK(AgenttyApp::visual_hash(after) != before);
}

TEST_CASE("stats gate: switching tabs advances the render hash") {
    // 这个也放在卷轴修正之前: stats_panel::Open 在
    // 其标签中声明 visual_parts ,所以槽走已经覆盖了。
    // 无论如何,这是用户报告的一半, 并且零件列表是未来
    // 一个重构者可以悄悄地 掉下来的东西。
    Model m = open_viewer();
    const auto* before_tab = m.ui.panel.get<pn::Stats>();
    REQUIRE(before_tab != nullptr);
    const stats::Tab t0 = before_tab->tab;
    const std::uint64_t before = AgenttyApp::visual_hash(m);

    auto [after, _] = app::update(std::move(m), Msg{StatsTab{+1}});
    const auto* after_tab = after.ui.panel.get<pn::Stats>();
    REQUIRE(after_tab != nullptr);
    REQUIRE(after_tab->tab != t0);              // 标签真的更改了

    CHECK(AgenttyApp::visual_hash(after) != before);
}

TEST_CASE("stats gate: closing the viewer advances the render hash") {
    // 电话 Esc 症状。 关闭后已经更改了面板 变体 ,
    // 所以这个在固定之前也保持了下来—— 被固定是因为这是
    // 用户实际报告的财产, 并且一个未来的改变 槽走动不能
    // 悄悄地打破它。
    Model m = open_viewer();
    const std::uint64_t before = AgenttyApp::visual_hash(m);

    auto [after, _] = app::update(std::move(m), Msg{CloseStats{}});
    CHECK(AgenttyApp::visual_hash(after) != before);
}

TEST_CASE("stats gate: a scroll that changes nothing does not advance it") {
    // 另一半合同,门是用来便宜的 所以不行 -op
    // 一定要坚持不 -op :在顶部滚动是被夹在同一个y上,
    // 用不同的方式将它烧掉。 帧 每个按键压,
    // 没有像素。
    Model m = open_viewer();
    REQUIRE(m.ui.panel.get<pn::Stats>()->scroll.y == 0);
    const std::uint64_t before = AgenttyApp::visual_hash(m);

    auto [after, _] = app::update(std::move(m), Msg{StatsScroll{-5}});
    REQUIRE(after.ui.panel.get<pn::Stats>()->scroll.y == 0);  // 夹住

    CHECK(AgenttyApp::visual_hash(after) == before);
}
