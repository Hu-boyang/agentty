#pragma once
// 统计查看器——一个选项卡式、read-only窗口，显示会话所做的事情。
//
// 从命令选项板(Ctrl+K→"Stats")打开它。
// Tab/Shift-Tab（或←/→）切换选项卡，Esc关闭。
// 这里没有任何东西会改变任何东西：面板是转录本的投影，
// 所以没有出错的状态，也没有设计的确认。
//
// ── 住在哪里吗? ─────────────────────────────────────────────────────
//
//   domain/stats/facts.hpp计数器+增量归并
//   domain/stats/unit.hpp ONE数字→
//   文本函数domain/stats/metric.hpp行词汇domain/stats/tabs.hpp
//   kTabs-THE SSOT用于order/titles/availability
//   view/panels/stats.cpp面板：表-driven，无per-tab分支
//
// 添加选项卡是枚举器、kTabs中的行和提取器。
// 它不涉及视图代码、按键处理和面板代码。
//
// ── 为什么投影会停留在公开的面板上 ───────────────────────────
//
// 它是CACHE，而不是第二个事实来源：其中的
// 每个数字都源自转录本并且可以随时从中重建。
// 它之所以坐在这里，是因为它的 生命周期 正是面板
// 的——打开面板开始折叠，关闭它可以释放一切——
// 而且因为比面板寿命更长的投影将成为从未打开它的
// 用户支付的后台成本。
//
// `mutable`，因为视图在 渲染 期间刷新它，
// 这是通过构造对模型进行常量操作：refresh()仅
// 重新计算记录已经说过的内容。

#include "agentty/domain/stats/facts.hpp"
#include "agentty/domain/stats/tabs.hpp"

#include <maya/core/scroll_state.hpp>

namespace agentty::stats_panel {

struct Open {
    // 哪个选项卡是活动的。 可变，因为当所选选项
    // 卡不可用时视图会更正它（最后一个工具 调用
    // 已回退） - 登陆不再绘制的选项卡将显示
    // 一个空面板，无法理解原因。
    mutable stats::Tab tab = stats::Tab::Session;

    // 正文的滚动偏移，OWNED BY 该 PANEL。
    //
    // 它曾经和十六个兄弟姐妹一起生活在Model::UI上，并且那个包使得
    // 面板没有响应，直到输入文字后才追上。
    // 错误成为可能。将其移至此处会产生两件事：
    //
    //   ·帧门免费看到它。 visual_parts(开盘)以下必须
    //     每个成员的帐户，并且 parts_cover_all 在类型上证明
    //     了这一点 - 因此视图读取的卷轴不能再是门忽略的卷轴。
    //     在 Model::UI 上，只能通过 program.hpp
    //     中的 hand-written mix() 线路到达，而 hand
    //     -written 线路是有人忘记的线路。这个面板就是他们所做的。
    //
    //   ·其生命周期成为面板的。一个比过去更长久的卷轴
    //     面板意味着重新打开查看器恢复了自上次关闭的
    //     偏移量——针对不同的选项卡、
    //     不同的文字记录和描述两者的max_y。
    //     该错误需要在开臂中进行显式重置；现在不能写了。
    //
    // 可变与`tab`的原因相同：渲染器的写回在
    // 绘制期间发布max_y，这是通过构造对模型
    // 进行的常量操作。
    mutable maya::ScrollState scroll = [] {
        // auto_dispatch关闭：状态更新函数拥有该偏移量(StatsScroll)，
        // 并且让maya ALSO将原始箭头送入其中意味着两个写入器在一个字段上
        // 竞速——"press it four times, it moves once"
        // 症状与选择面板命中。
        maya::ScrollState s;
        s.auto_dispatch = false;
        return s;
    }();

    // 增量归并。每 帧 摊销 O(1)：一个已结算的 线程 归并 什么都没有，
    // 一个 流式传输 一个 归并 正是实时的 末尾。有关光标和纪元保护，
    // 请参阅 domain/stats/facts.hpp。
    mutable stats::Projection projection;
};

}  // namespace agentty::stats_panel
