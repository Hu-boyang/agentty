// stats_scroll_test ——统计器的卷轴偏移决不能
// 离开它自己的范围 不变量 索赔。
//
// 观众读了 -only ,所以, QQ/ == 行动在
// VIEWPORT 而不是光标, 和抵消的生命 Model::UI
// 既使面板被束缚, max_y ,属于 渲染器 写回的,
// 只有在涂料后才会变成真实的。
//
// 分裂是整个危险 状态更新函数 运行 之前
// 编号 帧 将公布新内容,故一切经手计算,
// 都是用最后的。 帧 几何。旧代码是:
//
//     y QQ 三角形; 如果 (y < 0) y = 0; // 仅下方绑定
//
// 哪个 End (导航合成器在面板传输时移动(+1000) `move`
// 但没有 `jump` ) 存储 y = 1000 000,并留在模型中,
// 直到油漆夹住它。 -correcting 显示在屏幕上,但还是错误:
// 模型持有一种价值,它自己的合同禁止,任何 状态更新函数 读你之前
// 的油漆 - a StatsScroll 输入批次与输入批次相同
// StatsTab ,这是快 终端 交货 一个 读 - 读一个从不
// 合法的数字。
//
// 这些是合同的定点,不是算术: 任意 信件序列, 0 QQ
// y QQ max_y.

#include "agtest.hpp"

#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/msg.hpp"
#include "agentty/runtime/view/panels.hpp"

#include <maya/render/canvas.hpp>
#include <maya/render/renderer.hpp>
#include <maya/style/theme.hpp>

#include <cstdlib>

namespace pn = agentty::ui::panel;

using namespace agentty;

namespace {

// 打开了数据查看器的模型和 正文 高过它 可视区域 ,
// 所以有地方可以滚动 TO. max_y 画作本来
// 会刊出什么, 状态更新函数 一定要尊重它,不能重算
// it.
Model with_stats_open(int max_y) {
    Model m;
    // A 线程 有足够的转弯 充填面板 并溢出它。
    //
    // 来,来,来 空 模型足以进行上述抵消测试——它们
    // 断言: 状态更新函数 算术,它不在乎屏幕上的内容。
    // 光做以下高度测试是不够的: 几乎无任何内容的面板比
    // 它自己的短 可视区域 ,因此其高度被内容所支撑,
    // 完全无法对滚动做出回应。
    // "the height never changes" 只有一个
    // 原因 使得这个断言变得毫无意义 并且一个故意注射的虫子直接穿过它
    for (int i = 0; i < 60; ++i) {
        Message u;
        u.role = Role::User;
        u.text = "a question about the code";
        m.d.current.messages.push_back(std::move(u));

        Message a;
        a.role         = Role::Assistant;
        a.served_model = ModelId{"claude-sonnet-4-5"};
        a.text         = "an answer with enough prose to occupy a row or two";
        Message::Telemetry t;
        t.input_tokens   = 7400;
        t.output_tokens  = 1900;
        t.cache_read     = 52000;
        t.ttft_ms        = 1100;
        t.stream_ms      = 18000;
        a.telemetry = t;
        m.d.current.messages.push_back(std::move(a));
    }
    auto [opened, _] = app::update(std::move(m), Msg{OpenStats{}});
    // 抵消生命 ON 面板,所以约束也一样。
    opened.ui.panel.get<pn::Stats>()->scroll.max_y = max_y;
    // 结构-binding名称是该成员类型的LVALUE——在返回时没有隐含
    // 的移动(不同于一个命名的本地名称). Model是move-only
    // (其冻结的ScrollbackLedger是非-copyable),
    // 所以移动必须明确.
    return std::move(opened);
}

// 面板-owned的偏移,或-1在收视器关闭时.
int scroll_y(const Model& m) {
    const auto* o = m.ui.panel.get<pn::Stats>();
    return o ? o->scroll.y : -1;
}

void set_max_y(Model& m, int v) {
    if (auto* o = m.ui.panel.get<pn::Stats>()) o->scroll.max_y = v;
}

int scroll_after(Model m, std::initializer_list<Msg> msgs) {
    for (const auto& msg : msgs) {
        auto [next, _] = app::update(std::move(m), msg);
        m = std::move(next);
    }
    return scroll_y(m);
}

// 画高 统计帧:第一卡行至最后一卡.
//
// 画画而不是量度 因为测试所问的问题是
// READER 看见了 A 可视区域 数字,
// 在绘制的框改变大小时保持不变,正是需要抓取的分歧。
[[nodiscard]] int frame_height(const Model& m) {
    setenv("COLUMNS", "100", 1);
    setenv("LINES", "40", 1);
    maya::StylePool pool;
    maya::Canvas canvas(100, 200, &pool);
    canvas.clear();
    maya::render_tree(ui::stats_panel(m), canvas, pool, maya::theme::native,
                      /*auto_height=*/true);
    int top = -1, bottom = -1;
    for (int y = 0; y < 200; ++y) {
        bool inked = false;
        for (int x = 0; x < 100 && !inked; ++x) {
            const auto ch = canvas.get(x, y).character;
            if (ch && ch != U' ') inked = true;
        }
        if (inked) { if (top < 0) top = y; bottom = y; }
    }
    return top < 0 ? 0 : bottom - top + 1;
}

}  // 命名空间结束。

TEST_CASE("stats scroll: End settles at the bottom, not past it") {
    // 导航转弯 End 用于任何传输面板的移动(+1000) `move`
    // 无 `jump` 厂房 - 数据查看器所操作,因为它没有光标可跳。
    // 状态更新函数 不得不视之为 "as far as possible"
    // 而不是储存它。
    const int max_y = 12;
    CHECK(scroll_after(with_stats_open(max_y), {Msg{StatsScroll{+1000000}}})
          == max_y);
}

TEST_CASE("stats scroll: Home settles at the top, not before it") {
    CHECK(scroll_after(with_stats_open(12), {Msg{StatsScroll{+5}},
                                             Msg{StatsScroll{-1000000}}})
          == 0);
}

TEST_CASE("stats scroll: the offset stays in range through any sequence") {
    // 电话 不变量 本身,用三角洲导航驱动,可以实际产生:±1(窄),
    // ±10( PgUp/PgDn 途经 page_step),
    // ±1000000 (Home/End ),与制表开关互换。
    const int max_y = 9;
    Model m = with_stats_open(max_y);

    const int deltas[] = {+1, +10, -1, +1000000, -10, -1000000, +1, +10, +10};
    int i = 0;
    for (int d : deltas) {
        auto [next, _] = app::update(std::move(m), Msg{StatsScroll{d}});
        m = std::move(next);
        INFO("after delta[", i++, "] = ", d);
        CHECK(scroll_y(m) >= 0);
        CHECK(scroll_y(m) <= max_y);

        // 卷轴之间的标签切换:这是相同的-input-batch例,
        // 它也不得使抵消搁浅。
        auto [tabbed, __] = app::update(std::move(m), Msg{StatsTab{+1}});
        m = std::move(tabbed);
        CHECK(scroll_y(m) == 0);
        set_max_y(m, max_y);
    }
}

TEST_CASE("stats scroll: switching tabs returns to the top") {
    // 在一个高额标签的折角处打开的短标签显示一个空白的正文,
    // 没有明显的原因.
    Model m = with_stats_open(20);
    CHECK(scroll_after(std::move(m), {Msg{StatsScroll{+15}},
                                      Msg{StatsTab{+1}}}) == 0);
}

TEST_CASE("stats scroll: reopening the viewer starts at the top") {
    // 面板是新鲜的每张开放,但 SCROLL 过着幸福的生活
    // Model::UI 并且超过它-- 所以没有重新明确
    // 设置 重设重置的抵消 从上次关闭, max_y 说明
    // 中两个字都没有。
    Model m = with_stats_open(20);
    auto [scrolled, _]  = app::update(std::move(m), Msg{StatsScroll{+15}});
    auto [closed, __]   = app::update(std::move(scrolled), Msg{CloseStats{}});
    auto [reopened, ___] = app::update(std::move(closed), Msg{OpenStats{}});
    CHECK(scroll_y(reopened) == 0);
}

// ── 电话 帧 读的时候不要动 ──────────────────────────
//
// 上面的每个测试都写在卷轴上 OFFSET ,
// 这是 状态更新函数 '是算术的。没有画,
// 所以没有画能够看到读者实际注意到的东西:一个改变的面板
// SIZE 因为它的内容在它内部移动。 帧 滚过时会长出
// 一行 归并 ,或者在底部丢失一个,读作布局逐渐瓦解—
// —而抵消是始终正确的.
//
// 这是这个面板中真正的风险,而不是假设。 正文 整块和
// 不透明 可视区域 剪切时,滚动条沟槽保留,
// 不管是否绘制了条,面板测量自己的内容以决定 可视区域 高度,
// 每个都有一个地方 "how tall am I"
// 可以根据 "where am I scrolled to"
// 答案必须是否定的。
//
// 绘画,不是测量,因为问题在于读者所看到的。
// 这里的高度是 帧 的插图范围:第一行用墨水
// 画到最后,这正是屏幕上绘制的盒子。
TEST_CASE("stats scroll: the frame keeps its height at every offset") {
    // Model 正在移动 -only ,所以每个案件都从同一个固定位置建立自己的,
    // 而不是将一个放在一边。
    const int at_top = frame_height(with_stats_open(60));
    CHECK(at_top > 0);

    // 横跨整个范围,包括经过终点:夹子是使最后
    // 一个屏幕不成为短屏幕的原因.
    for (int step : {1, 3, 7, 15, 30, 60, 120}) {
        auto [scrolled, _] =
            app::update(with_stats_open(60), Msg{StatsScroll{step}});
        INFO("scrolled by ", step, " -> offset ", scroll_y(scrolled));
        CHECK(frame_height(scrolled) == at_top);
    }

    // 后退,这是另一条通过夹子的路径。
    {
        auto [down, _] = app::update(with_stats_open(60), Msg{StatsScroll{+40}});
        auto [up, __]  = app::update(std::move(down), Msg{StatsScroll{-40}});
        CHECK(frame_height(up) == at_top);
    }

    // 极端,到达了面板到达的方式: End 财务报告和已审计
    // 财务报表 Home 它们是夹子吸收的大三角洲。
    // 两者必须仍然是同一个框 — 文件的最后屏幕不是较短的。
    {
        auto [ended, _] =
            app::update(with_stats_open(60), Msg{StatsScroll{+1000000}});
        CHECK(frame_height(ended) == at_top);
        auto [homed, __] =
            app::update(std::move(ended), Msg{StatsScroll{-1000000}});
        CHECK(frame_height(homed) == at_top);
    }
}

// 同样的保证 TABS.
//
// 切换视图不是滚动的,而是在一下改变面板
// 内容的另一种方式 帧 高额标签,后面加一个短标签,
// 正是一个面板大小到其内容的大小明显缩小的情况,
// 而且 可视区域 高度是属于人类的
// 终端 而不是标签。
TEST_CASE("stats scroll: the frame keeps its height across tabs") {
    Model m = with_stats_open(60);
    const int first = frame_height(m);
    CHECK(first > 0);

    for (int i = 0; i < 8; ++i) {
        auto [tabbed, _] = app::update(std::move(m), Msg{StatsTab{+1}});
        m = std::move(tabbed);
        INFO("after ", i + 1, " tab switches");
        CHECK(frame_height(m) == first);
    }
}
