// SPDX-License-Identifier: Apache-2.0
//
// rehydrate_scrollback_test.cpp ——恢复.
// 线程 留下真实的历史 终端 本地人 : 回滚缓冲区 ,活画布保持小。
//
// 该 BUG 此 PINS. rehydrate_frozen () 使用过的 一个
// 预算用于两个不同的问题:恢复时需要画多少记录片。 线程 以及多少 保持生活为 -rendering.
// frozen.cpp 说老一行是安全的,
// "live on disk (recall via picker)" 并且曾经是,
// 内容在实时溢出时已提交到原生回滚缓冲区。
//
//
// 线程XXSWITCH也一样。 没有任何东西被溢出 -- --
// 线程刚刚从磁盘上读出 -- -- 而reset_inline()
// 首先发出\x1b[3J],它擦拭了那里的任何回滚缓冲区。
// 用一个真正的3326-message 线程来衡量:开关释放了1个3J,
// 然后是174条新线,画布则保留了82行。滚动显示没有显示任何东西。
// 用户有一个上下文,没有办法看到更多。
//
// 固定分离预算:补水种子 rehydrate_row_budget () (~
// 10个视图),以及现有帖子. -paint 修剪后,它会回到 frozen_row_budget
// () (~3个景点) 修剪是这个工作的原因—— drop_front 积聚每
// 个空地块的油漆 -recorded 高度为 ScrollbackDebt
// 财务报告和已审计财务报表 harvest() 薄荷a commit_scrollback 符号,
// 所以行物理滚动到 终端's 已保存-lines 而不是
// 被抛弃。
//
// 3 个属性, 如果出现退缩, 测试失败 :
//   1. 种子为: WIDER 而不是活的预算(有东西可以提供)
//      至 回滚缓冲区) (千兆吨)
//   2. 修剪使活画布回来 UNDER 实际预算(调整规模)
//      财务报告和已审计财务报表 Ctrl-L 成本,如以前成本)
//   3. 修饰 COMMITS 而不是放下它(各行)
//      可滚动,而非丢失)

#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/app/update.hpp"

#include <maya/print.hpp>

#include <cstdio>
#include <string>

namespace {

int failures = 0;
void check(bool ok, const char* what) {
    std::printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

using agentty::Model;
using agentty::Message;
using agentty::Role;

// 记录稿足够长,可以把预算都浪费掉 所以散步和修剪都有
// 真正的工作 正文 具有多 -line 所以排数是由
// 内容驱动的,而不是由铬驱动的.
void build_long_thread(Model& m, int turns) {
    m.d.current.messages.clear();
    for (int i = 0; i < turns; ++i) {
        Message u;
        u.role = Role::User;
        u.text = "user turn " + std::to_string(i) + " asking for something";
        m.d.current.messages.push_back(std::move(u));

        Message a;
        a.role = Role::Assistant;
        a.text = "assistant reply " + std::to_string(i) + "\n"
                 "second line of the reply\n"
                 "third line of the reply\n"
                 "fourth line to give this turn some height\n";
        m.d.current.messages.push_back(std::move(a));
    }
}

}  // 命名空间结束。

int rehydrate_scrollback_test_main() {
    std::printf("=== rehydrate_scrollback_test ===\n");

    namespace detail = agentty::app::detail;

    // 1. 两种预算是不同的,种子是更广泛的。
    //    检查了固定行数,所以断言不取决于终端,
    //    测试恰好在下方运行.
    const std::size_t live = detail::frozen_row_budget(40);
    const std::size_t seed = detail::rehydrate_row_budget(40);
    std::printf("  live canvas budget: %zu rows,  rehydrate seed: %zu rows\n",
                live, seed);
    check(seed > live,
          "rehydrate seeds MORE than the live canvas keeps");
    check(seed >= live * 2,
          "the seed is wide enough to be worth committing (>= 2x)");

    // 1b. 调用 状态更新函数 预算改为 终端 从 Model ,
    //     主机作为种子提供 TerminalResized 答案是,
    //     无论 Msg 说,从来没有什么测试 自己的tty碰巧是。
    {
        Model sized;
        (void)agentty::app::update(sized, agentty::msg::MetaMsg{agentty::TerminalResized{120, 40}});
        check(sized.ui.term_cols == 120 && sized.ui.term_rows == 40,
              "TerminalResized lands in the Model");
        check(detail::frozen_row_budget(sized) == live,
              "the live budget follows the Model's rows");
        (void)agentty::app::update(sized, agentty::msg::MetaMsg{agentty::TerminalResized{0, 0}});
        check(sized.ui.term_rows == 40,
              "a 0x0 report (detached tty) keeps the last good size");
    }

    // 2. 种子时间长 线程 填充画布 LIVE 预算 -- --
    //    i.e 修剪时需要你帮忙 回滚缓冲区.
    Model m;
    build_long_thread(m, 400);
    detail::rehydrate_frozen(m);

    const std::size_t seeded_rows = m.ui.frozen.row_total();
    std::printf("  seeded canvas: %zu entries, %zu rows\n",
                m.ui.frozen.size(), seeded_rows);
    check(seeded_rows > live,
          "the seeded canvas exceeds the live budget (trim has work)");
    // NOTE:pending_rehydrate_trim由ThreadLoadedXX
    // 状态更新函数武装,而不是rehydrate_frozen本身—
    // —这一测试是调用直接的,所以标志不是其合同的一部分.

    // 3. 立于第一漆.
    //
    //    分类账的 PROVABILITY GATE 这也是
    //    这个测试必须做到的原因: drop_front () 仅
    //    减少一个真实分类账高度的区块 -tagged 颜料有
    //    RECORDED ,在第一个没有记录的区块上停着。
    //    新封的区块没有记录的高度,所以,放下它会真正地流出。
    //    传输 累积零债务时行数 -- -- a -commit
    //    被困在地上的 重复的鬼魂 agentty 将修剪工作推迟到
    //    一个职位 -paint 滴答,不要在水分再放电
    //
    //    没有头,没有油漆, 所以每个街区都保持没有记录,
    //    和这里的修剪会正确 掉任何。 record_paint
    //    ( ) 何为 渲染器 调用 百分比 帧 ;
    //    称它使前缀可以完全作为第一个真实的 帧 将会。
    for (std::size_t k = 0; k < m.ui.frozen.size(); ++k)
        m.ui.frozen.record_paint(k, static_cast<int>(m.ui.frozen.block_rows(k)));

    // 4. 修剪后,画布重新回到现场预算之下 和 发送 a
    //    承诺而不是默默地放弃。 none() Cmd
    //    这里意味着这些行被丢弃了—— 这个测试的准确失败。
    const auto cmd = detail::trim_frozen_if_oversized(m);
    const std::size_t trimmed_rows = m.ui.frozen.row_total();
    std::printf("  after trim:    %zu entries, %zu rows\n",
                m.ui.frozen.size(), trimmed_rows);

    check(trimmed_rows < seeded_rows,
          "the trim SHRANK the canvas (rows left for scrollback)");
    check(!cmd.is_none(),
          "the trim COMMITS the dropped rows (scrollback, not /dev/null)");
    check(trimmed_rows <= live * 2,
          "the live canvas is bounded after the trim");

    // 5. 回滚缓冲区 重放: 宽度更改擦除后 回滚缓冲区 ,则
    //    历史源必须还回EVERY消息, 裁掉的,
    //    顺序, 并且没有在直播前缀中( 没有重复)。
    {
        const std::size_t live_start =
            static_cast<std::size_t>(m.ui.frozen.source(0)) - 1;
        check(m.ui.frozen.source(0) > 0, "live prefix blocks carry their source message");
        check(live_start > 0 && live_start < m.d.current.messages.size(),
              "the trim moved the live prefix past the start");

        const auto ledger_before   = m.ui.frozen.size();
        const auto through_before  = m.ui.frozen_through;
        const auto turn_before     = m.ui.frozen_turn;
        const auto blocks = detail::build_history_blocks(m);
        check(!blocks.empty(), "history is non-empty");
        check(m.ui.frozen.size() == ledger_before
              && m.ui.frozen_through == through_before
              && m.ui.frozen_turn == turn_before,
              "building history leaves the live prefix untouched");

        // 渲染 历史,并找到每个消息的文本顺序。
        std::string all;
        for (const auto& b : blocks) all += maya::render_to_string(b.element->front(), 100) + "\n";
        std::size_t at = 0;
        bool order = true;
        int seen = 0;
        for (std::size_t i = 0; i < live_start; ++i) {
            const auto& msg = m.d.current.messages[i];
            const std::string needle = msg.text.substr(0, msg.text.find('\n'));
            const auto p = all.find(needle, at);
            if (p == std::string::npos) { order = false; break; }
            at = p + needle.size();
            ++seen;
        }
        std::printf("  history: %zu blocks, %d of %zu dropped messages found in order\n",
                    blocks.size(), seen, live_start);
        check(order, "every dropped message is in the history, in order");
        const auto& first_live = m.d.current.messages[live_start];
        const std::string live_needle =
            first_live.text.substr(0, first_live.text.find('\n'));
        check(all.find(live_needle) == std::string::npos,
              "nothing from the live prefix is duplicated in the history");
        // 每个转弯块都搭载缓存键,因此第二次改码是便宜的.
        int keyed = 0;
        for (const auto& b : blocks) keyed += b.hash != 0 ? 1 : 0;
        check(keyed > 0, "history blocks are cache-keyed");
    }

    if (failures) {
        std::printf("FAILED (%d)\n", failures);
        return 1;
    }
    std::printf("PASS\n");
    return 0;
}
