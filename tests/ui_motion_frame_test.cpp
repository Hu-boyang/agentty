// ui_motion_frame_test ——现场外观变化达到.
// 每个 流式传输 部件,不只是第一个
//
// ── 错误 ─────────────────────────────────────────────────────────────
//
// turn.cpp 内容 -applies 编号 逐步显示-fx 政策改为
// a 流式传输 部件新鲜时的部件 OR 动作刚刚改变,第二个词就是
// "turn motion off" 带 副作用 中点 -stream 而不是
// 在下一个消息。
//
// 比较一下 -and-reset 本地锁定为 cached_markdown_for():
//
//     static Motion last_motion = current().motion;
//     const bool motion_changed = current().motion != last_motion;
//     last_motion = current() .motion; // <-- 消耗 此处
//
// 不过 cached_markdown_for () 运行 SEVERAL 每
// 分钟 帧 ——每期一次 流式传输 部件。在转弯时,既思考又回答,
// 即推理道(即: "#r" 缓存 和答题道,加1 调用 每分机 -turn 在
// 一个多 -message 转身 reasoning_slot() 渲染 答案
// 上方的 正文 ,所以推理道被先到达, 消耗了变化, 每一个后来的部件都在同一
// 帧 读 `motion_changed == false`.
//
// 净额 副作用 : 移动中 -stream 应用到推理块并留下答案 正文 动画
// turn.cpp 已包含关于此确切失败的段落警告( Q)
// 策略应逐组件实例应用，而非每进程一次，确保每条消息都获得策略。
// )——锁通过后门
// 重新提出.
//
// ── 为什么是固定的 -widget 而不是一个 帧 标志 ──────────────────────
//
// 第一次尝试是一个过程 -global "motion changed this frame"
// 标志 设定 publish()。它能修复共享,而且更糟: 标志 谓
// STICKY 任何不出版的 帧 ,所以它从这个测试文件泄露出来,
// 并打破了 midrun_wire_test (种子) reveal_fx
// OFF 以便测量一个高地,如果它被一个假的、 -apply).
//
// 老实说,从来就没有这么简单 帧。
// 这是 "does this widget's configured policy differ from the one in force"
// ——按 -widget 状态,这是什么 MessageMdCache::applied_motion 别动,
// 不要叫,不要叫 帧 语义,无漏.
//
// 这个测试针钉 DECISION RULE 对抗那个模型。

#include "agtest.hpp"

#include "agentty/domain/ui_live.hpp"

using agentty::ui_prefs::Motion;

namespace {

// 规则是 turn.cpp 应用:关于 -apply 当部件是新鲜的,
// 或它持有时 KNOWABLY 与现行办法不同,然后在适用办法上盖章。
// applied_motion < 0 表示数 "never set by us"
// -没有 过期 - 所以它本身并不强迫 -apply (从外部引种的插槽
// 保留它得到的保险单).
struct Widget {
    int applied_motion = -1;            // -1=从未盖过章
    bool fresh = true;                  // 真实的,只写在 帧 我们创造它
    int applies = 0;                    // 政策设定的几倍

    void frame(Motion in_force) {
        const int now = static_cast<int>(in_force);
        const bool changed = applied_motion >= 0 && applied_motion != now;
        if (fresh || changed) {
            ++applies;
            applied_motion = now;
        }
        fresh = false;
    }
};

}  // 命名空间结束。

TEST_CASE("ui motion: every widget in the frame sees the change") {
    // 双向 一个 转弯:推理 渲染 先是答案,然后是答案。
    Widget reasoning, answer;

    // 完全状态——每个配置一次在它的首个帧上.
    reasoning.frame(Motion::Full);
    answer.frame(Motion::Full);
    CHECK(reasoning.applies == 1);
    CHECK(answer.applies == 1);

    // 用户转动OFF 中-stream. BOTH车道必须在
    // 相同的帧中重新换乘-apply. 在旧共享的挂链下,
    // 推理车道消耗了变化,答车道停留在1号——bug.
    reasoning.frame(Motion::Off);
    answer.frame(Motion::Off);
    CHECK(reasoning.applies == 2);
    CHECK(answer.applies == 2);
}

TEST_CASE("ui motion: a steady pref does not re-apply every frame") {
    // 情况之所以存在,原因有: -applying 重建部件,
    // 所以总是 -true 测试会重建每个 流式传输 在每个
    // 部件上 帧 每一个转弯。
    Widget w;
    for (int i = 0; i < 20; ++i) w.frame(Motion::Full);
    CHECK(w.applies == 1);
}

TEST_CASE("ui motion: every level transition re-applies") {
    // 逐步显示政策在所有三个级别上都有所不同(减量保留
    // 了进步剪辑并放下装饰性churn),因此任何一对
    // 都不能作为等效物.
    const Motion levels[] = {Motion::Full, Motion::Reduced, Motion::Off};
    for (const Motion from : levels) {
        for (const Motion to : levels) {
            if (from == to) continue;
            Widget w;
            w.frame(from);
            const int before = w.applies;
            w.frame(to);
            CHECK(w.applies == before + 1);
        }
    }
}

TEST_CASE("ui motion: a widget seeded from outside keeps its policy") {
    // 直接构建的槽( E) midrun_wire_test 种子
    // reveal_fx OFF 测量一个固定高度)不是我们重新配置的。
    // 未加标记的级别意味着 "never set by us" ,
    // 因此它不能读作 过期 并获得覆盖——这正是这个固定的早期版本所做的。
    Widget seeded;
    seeded.fresh = false;              // 无法由视图创建此 帧
    seeded.frame(Motion::Full);
    CHECK(seeded.applies == 0);

    // ... 但是一旦前置的 MOVES 仍然没有什么可比较的,
    // 所以它不会被移动。 部件只有在视图创建时
    // 才被采用。
    seeded.frame(Motion::Off);
    CHECK(seeded.applies == 0);
}

TEST_CASE("ui motion: a late-created widget adopts the level in force") {
    // 一个子 -turn 创建其部件 之后 移动前置
    // 文件必须从当前级别配置自己,而不是继承 过期
    // 新鲜性是病情的第一个条件,所以这个条件有效
    Widget late;
    late.frame(Motion::Off);
    CHECK(late.applies == 1);
    CHECK(late.applied_motion == static_cast<int>(Motion::Off));
    // 之后它不会重新 -apply 下一个 帧.
    late.frame(Motion::Off);
    CHECK(late.applies == 1);
}
