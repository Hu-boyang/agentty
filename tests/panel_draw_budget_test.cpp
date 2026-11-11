// panel_draw_budget_test——其面板的抽取控制大小.
//
// 本测试补齐的验证缺口。
// ===================
// maya::panel 其项目部件 渲染 从自身值加 ItemCtx ,
// 其它的。这是模块化的保证,而且它是一个很好的保证——
// 一个项目部件实际上无法增强面板逻辑。
//
// 不过 ItemCtx 仅载有事实a TEXT 控制需要。一个控制,
// 其格言是一张图片-- 条,仪表,条条-- 无法得知它有多少房间,所以每个硬码都
// 用自己的尺寸。滑动画画 `constexpr int kCells = 12`
// 40号公路 -column 拆分( E) 面板 和200块 -column
// 终端 一样
//
// 对于一个不是化妆品问题的酒吧:一个酒吧的IS号决议的宽度.
// 12 存储单元 最小可见的步数是 8%,
// 所以两个设置是 20 个相隔的绘图。 冻结大小会冻结信息。
//
// ItemCtx::draw_budget 缺少的事实,来源于 min_width 完全
// 一样 edit_budget 和原因 原因 edit_budget 其评论给出:
// 问 LAYOUT 宽度多宽 存储单元 是在卷轴里 可视区域
// 以无限制的宽度衡量儿童,
// "control reports 2^24 columns" 类虫。
//
// 内容 IS PINNED  ⁇
// ==============
// 两者都不同 因为预算总是增长 和从未增长过的预算
// 一样错误:
//   • 扩大面板绘制宽条(bug)
//   • 双端(无限制增长)
//   • 未计量的预算仍在提取(在小组外使用)
//   • 条码的比例不变(它仍然是同一个数据)

#include "agtest.hpp"

#include <maya/widget/panel/context.hpp>
#include <maya/widget/panel/item/slider.hpp>
#include <maya/widget/panel/theme.hpp>

#include <maya/text/unicode_width.hpp>

#include <string>

using maya::panel::ItemCtx;
using maya::panel::Slider;

namespace {

const maya::panel::Theme& theme() {
    static const maya::panel::Theme t{};
    return t;
}

// 制成的滑块中的存储单元:格法为  ⁇ (on)和(off),
// 数值在两个空格后随行。
int bar_cells(const Slider& s, int draw_budget) {
    ItemCtx ctx{.theme = theme(), .draw_budget = draw_budget};
    const auto [text, _] = render(s, ctx);
    int n = 0;
    for (std::size_t i = 0; i + 2 < text.size(); i += 3) {
        const std::string g = text.substr(i, 3);
        if (g == "\xe2\x96\x86" || g == "\xe2\x96\x81") ++n;
        else break;
    }
    return n;
}

}  // 命名空间结束。

TEST_CASE("draw budget: a wider panel draws a wider bar") {
    const Slider s{.value = 0.5};
    // 报告缺陷:照片每宽都一样。
    CHECK(bar_cells(s, 10) < bar_cells(s, 20));
    CHECK(bar_cells(s, 20) < bar_cells(s, 24));
}

TEST_CASE("draw budget: the bar is bounded at both ends") {
    const Slider s{.value = 0.5};
    // 地板,因为下面的条子不再是一个尺度,
    // 变成纹理-- 狭小的面板应该画一个小的条子,而不是抹片。
    CHECK(bar_cells(s, 1) >= 8);
    CHECK(bar_cells(s, 4) >= 8);
    // 上限,因为一个定界比率是按比例来判断的,
    // 而不是衡量的。无界增长将一个设置列表变成条形图。
    CHECK(bar_cells(s, 200) <= 24);
    CHECK(bar_cells(s, 4096) <= 24);
}

TEST_CASE("draw budget: an unmeasured budget still draws") {
    // 在面板外构建的控件(预览、测试、
    // 尚未测量的主机)获得预算0. 它必须回到
    // 它的偏好而不是零。 未测量时消失的控件
    // 就是在没有人测试的情况下消失的控物。
    const Slider s{.value = 0.5};
    CHECK(bar_cells(s, 0) == 12);
}

TEST_CASE("draw budget: resizing does not change the value the bar encodes") {
    // 酒吧越来越宽,它必须说同样的话。 -full 滑
    // 动器是半 -full 如果预算的填充率随预算的变动而变化,
    // 那么,情况就会是关于基准的谎言。
    for (int budget : {0, 8, 12, 16, 20, 24, 64}) {
        const auto cells = bar_cells(Slider{.value = 0.5}, budget);
        ItemCtx ctx{.theme = theme(), .draw_budget = budget};
        const auto [text, _] = render(Slider{.value = 0.5}, ctx);
        int on = 0;
        for (std::size_t i = 0; i + 2 < text.size(); i += 3)
            if (text.substr(i, 3) == "\xe2\x96\x86") ++on; else break;
        INFO("budget=", budget, " cells=", cells, " on=", on);
        // 一半,到圆形内 一个离散的条必须这样做。
        CHECK(on * 2 >= cells - 1);
        CHECK(on * 2 <= cells + 1);
    }
}
