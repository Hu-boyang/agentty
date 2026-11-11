// stream_liveness_test — UX-critical 回归:
// 活体 -edge 光标 (maya reveal_fx )必须 绝不 模型
// 响应正在飞行时看起来冷冻。 用户 -visible 失败此 锁 出局:一个慢
// 模式在三角洲(中间~470 ms,最差秒)和打字机之间暂停 光标 停止
// 脉冲中点 -sentence 读作 "the stream died".
//
// 根源历史(承诺498ec9e → a9760e7): RAF 内容 -arm 门进入
// turn.cpp's cached_markdown_for 是个固定的
// 字节-recency 超时 (250毫秒,然后是3秒). 任意 固定 超时
// 可能已经出来了 -run 由慢的模型或拉吉 链接。坚固的门键关闭
// m.s.is_streaming () —— 国家 变体-backed phase::Streaming 信号,
// 所以 光标 武器在战争期间无条件保留 传输 打开, 仅在相位离开时才
// 下降 流式传输.
//
// 此处所称合同,通过 maya::detail::animation_requested_
// (无) 线程-local 说 request_animation_frame ()
// 设定和运行环路读取为下个~16 ms 的排程 重绘):
//
//   1. 阶段 流式传输 信息已经存在 字节, 每个
//      视图构建再 -arms 动画 帧 ——即使模拟10次之后.
//      -second 与零新数据的差距 字节 (出处) -runs 任何 超时
//      任何人可能再次出现。
//   2. 其后 数据流 结账(阶段 ) —— 离散, streaming_text 排水量
//      ,部件完成 ~200 ms 的斜坡最后完成 STOPS 内容 -arming ——闲圈必须回到零醒(合同的另一半:没有冻结)
//      —— 零醒(零醒) ——零醒(0醒) : 零醒 (0醒) - —— —— 空圈 —— 循环 —— 3醒 —— 4醒 ——0醒 —
//      — — 零醒 —— - —— - - ——- —— = - - - - = - == - - 3醒 - - -= - - 光标 ,
//      但同样没有 60 FPS 空闲时燃烧。
//
// 如果测试失败了(1), 有人重新 -introduced a 超时 竞态 - 电话
// "stream looks dead" 错误又回来了,如果失败在 (2) 上,
// 则 光标 永远不能解除和闲置 CPU 烧伤。

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "agtest.hpp"

#include <maya/device/frame_request.hpp>  // maya::detail::animation_requested_
#include <maya/core/anim_clock.hpp>     // maya::testing::advance_anim_clock_ms
#include <maya/render/canvas.hpp>
#include <maya/render/renderer.hpp>
#include <maya/style/theme.hpp>
#include <maya/widget/thread.hpp>

#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/view/thread/thread.hpp"

using agentty::Model;
using agentty::Message;
using agentty::Role;

// 渲染 编号 CONVERSATION 仅限区域(冻+活)
// 末尾 )并报告其中是否有部件要求动画 帧 建设期间。
// 镜像运行环: 清除 标志 ,构建 + 油漆,读取 标志.
//
// 故意的 不 完整 AppLayout : 作曲家闲置光标
// 闪烁( ) maya composer.hpp ) 有关
// -arms 动画 帧 将掩盖了减速 光标 测试中解除。
// 光标 试验中的生命 线程 区域; 精确地探测。
static bool frame_requests_animation(const Model& m,
                                     int width = 100, int height = 40) {
    maya::detail::animation_requested_ = false;

    auto root = maya::Thread{agentty::ui::thread_config(m)}.build();

    maya::StylePool pool;
    maya::Canvas canvas(width, height, &pool);
    canvas.clear();
    maya::render_tree(root, canvas, pool, maya::theme::native, true);

    return maya::detail::animation_requested_;
}

// 决断 逐步显示 排水沟 maya 逐步显示 光标前进
// anim_now_ms() (maya/core/anim_clock.hpp ),
// 所以测试驱动它通过 ADDING 输入测试 -only
// 动画"不要睡在墙上" -clock 最后期限——旧期限
// sleep_for (16米)+ 宽度 steady_clock::now
// (a) 期限模式下的碎片 CPU 饥饿(在进程解时墙壁
// 时间燃烧,坡道结束前最后期限将到期)。 `step`
// (必须 调用 build() 被束缚在...
// `max_steps` 帧 (默认600=9.6秒的动画时间,
// 远过任何斜坡) 所以真的卡住了 逐步显示 仍然终止循环
// 与上游真,绊倒 呼叫者 CHECK.
template <class Pred, class Step>
static void drain_reveal(Pred still_animating, Step step,
                         int max_steps = 600) {
    for (int i = 0; i < max_steps && still_animating(); ++i) {
        maya::testing::advance_anim_clock_ms(16);
        step();
    }
}

// （1）无论增量间隔多长，光标都必须保持启用，
//        三角差距,而相位则表示流式传输.  ⁇ .
TEST_CASE("caret armed across delta gap") {
    std::printf("test_caret_armed_across_delta_gap\n");

    Model m;
    m.d.current.id = agentty::ThreadId{"liveness"};
    Message u; u.role = Role::User; u.text = "explain something";
    m.d.current.messages.push_back(std::move(u));
    agentty::app::detail::clear_frozen(m);
    agentty::app::detail::freeze_through(m, 1);

    // 内 -flight 助理消息: 三角洲第一时间到 传输 仍然开放。
    // 正文 保留 SHORT 所以,我们 逐步显示 光标快速追上到
    // 活边上 —— 下面的漏洞探测器必须隔离 RAF 大门,
    // 以及 reveal_in_progress (cursor
    // 仍然滑翔积压) 帧 隔着它,它会掩盖一个破门而入的。
    Message a; a.role = Role::Assistant;
    a.streaming_text = "Hi";
    m.d.current.messages.push_back(std::move(a));
    m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};

    // 帧 三角洲之后:必须携带武器( 字节 刚刚长大了).
    CHECK(frame_requests_animation(m),
          "caret armed on the frame after a delta lands");

    // 让打字机到达直播边缘(2 cp at 30 cps QQ 70 ms;
    // 宽限期). reveal_in_progress () 虚假的,
    // 仅 合法武装源左侧,而 传输 正在打开的 是
    // 测试中的相位门。
    // NB : 输入在固定(活)和定居地图之间迁移,作为其生命周期的翻转,
    // 在两个中 -map ViewCache 迁移是一个节点传输,
    // 它使任何长的 -lived 引用。所以排水层重新 -fetches
    // 通过 NON-migrating peek() 每一步骤,而不是缓冲一个
    // 条目和交叉 frame_requests_animation 调用.
    const auto rip = [&] {
        const auto* mc =
            m.ui.view_cache.peek(m.d.current.id, m.d.current.messages.back().id);
        return mc && mc->streaming && mc->streaming->reveal_in_progress();
    };
    drain_reveal(rip, [&] { (void)frame_requests_animation(m); });  // build() 前进光标
    CHECK(!rip(), "reveal cursor reached the live edge (test precondition)");

    // 模拟杀手的缺口 : NO 新建( N) 字节, 传输 仍然开放。
    // 两个 探测器必须关闭时钟才能诚实地:
    //   • 部件的内部调整窗口(maya reveal_fx,
    //     age_at_tail_ms 250毫秒保持它自己 -arming 每建造一座大楼)
    //     ——被推进动因钟击败(Anim clock). maya 读取 anim_now_ms);
    //   • agentty's 缓存 惯性窗口(kRevealAactiveMs = 3 s) -
    //     agentty 读取 真实 steady_clock 上面,
    //     直接背影印章, 即慢模的漏洞对它有何影响。
    // 穿过两扇窗子 maya 军政府的武器每架只一次 33/100 毫
    // 斯相位桶- 不 每隔一段时间 帧 - 如此连续 -bucket
    // 建立虚假的返回,除非 agentty 相位门( 3)
    // wire_streaming_here 持有 光标 装备齐全,
    // 那是正在测试的大门
    maya::testing::advance_anim_clock_ms(400);
    m.ui.view_cache
        .message_md_live(m.d.current.id, m.d.current.messages.back().id)
        .last_grow_tick_ms =
        maya::anim_now_ms() - 10000;   // 模拟 10 s 间 -delta 差额

    // 6个回来 -to-back 帧 差距内—— 每个 一个人必须重新 -arm.
    // A 超时-only 门没变 -phase-bucket 帧 无人武装,
    // 但在此失败(通过破坏大门进行验证) 超时-only).
    for (int f = 0; f < 6; ++f) {
        CHECK(frame_requests_animation(m),
              "caret STILL armed mid-gap (400 ms + simulated 10 s, "
              "zero new bytes, phase=Streaming) — a timeout race fails here");
        maya::testing::advance_anim_clock_ms(2);
    }

    // 圣洁:大门是相位,而不是后期的时钟。将相位翻转到Idle
    // 同时 字节 仍未解决( 取消-like —— 边 逐步
    // 显示 光标可能仍然滑动 其积压, 因此武装可能持续 短暂,
    // 但 UNCONDITIONAL 保证已经不存在了。
    // (无) CHECK 暂时滑翔是合法的 裁军的强硬主张是
    // test_caret_disarms_after_settle.)
}

// （2）收尾后，完成过渡结束时光标必须关闭，
//        完成 —— 闲置不能永远烧帧。 @ @ info: whatsthis
TEST_CASE("caret disarms after settle") {
    std::printf("test_caret_disarms_after_settle\n");

    Model m;
    m.d.current.id = agentty::ThreadId{"liveness2"};
    Message u; u.role = Role::User; u.text = "explain something";
    m.d.current.messages.push_back(std::move(u));
    agentty::app::detail::clear_frozen(m);
    agentty::app::detail::freeze_through(m, 1);

    Message a; a.role = Role::Assistant;
    a.streaming_text = "Short reply body.";
    m.d.current.messages.push_back(std::move(a));
    m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};
    CHECK(frame_requests_animation(m), "armed while streaming");

    // 数据流 完成 : 状态更新函数 移动 streaming_text 文本,阶段 ——闲置。
    {
        Message& back = m.d.current.messages.back();
        back.text = std::move(back.streaming_text);
        back.streaming_text.clear();
        back.pending_stream.clear();
    }
    m.s.phase = agentty::phase::Idle{};

    // 部件运行一个~200 ms 的最终斜拉桥(
    // request_finalize (200) 滑翔机
    // 逐步显示 光标到活边; 帧 正当的重新 -arm。
    // 推进动画钟 帧 由 帧 直至它解除了武器,而且它远在斜坡上。
    // 光标 (60个) FPS 将永远在闲置之中奔跑 CHECK
    // 火力下方 火力以下
    bool disarmed = false;
    for (int i = 0; i < 600 && !disarmed; ++i) {
        if (!frame_requests_animation(m)) { disarmed = true; break; }
        maya::testing::advance_anim_clock_ms(16);
    }
    CHECK(disarmed,
          "caret disarms after settle + finalize ramp (idle must not "
          "burn animation frames)");

    // 并保持解除武装:迅速安顿下来 -path 绝不能重 -arm.
    if (disarmed) {
        for (int f = 0; f < 3; ++f) {
            CHECK(!frame_requests_animation(m),
                  "caret stays disarmed on settled frames");
        }
    }
}

// （3）逐字显示仍在运行时，绝不能触发延迟收尾冻结。
//        中点 -glide ——员额结构保障. -stream
//        回滚缓冲区 重复/ 幽灵。 live_tail_reveal_settled
//        ()是大门 meta.cpp 冻结前检查; 当打字机仍在
//        动画时必须返回假字( 所以冻结等待) , 并且只有在部件
//        自动翻转后才真实( 所以 快照 等于 off 时
//        -screen 帧 = maya 缓存 HIT =0个 -emit。
//        重制截图错误类:静态冻结 -animating 转弯
//        快照 与活体不同的形状 帧 输入 maya's prev_cells ,
//        关于 -emitting 整个转身 承诺 回滚缓冲区.
//        ────────────────────
TEST_CASE("freeze gated on reveal drain") {
    std::printf("test_freeze_gated_on_reveal_drain\n");

    Model m;
    m.d.current.id = agentty::ThreadId{"freezegate"};
    Message u; u.role = Role::User; u.text = "write a longish reply";
    m.d.current.messages.push_back(std::move(u));
    agentty::app::detail::clear_frozen(m);
    agentty::app::detail::freeze_through(m, 1);

    // 助理转身 逐步显示 中间 -glide 带有最后
    // 斜坡的装备——确切部件说明冻结门必须拒绝 快照.
    // 一个 长段( NO 空白 -line 块边界: 整个 正文 留在未
    // 承诺的 逐步显示 末尾 ,所以,我说 reveal_cp 以 0 开始,
    // 打字机有滑翔的真距离。 (A\ n\ n) -delimited
    // 正文 将每个块都输入 set_content ,和所承诺的 -snap
    // 跳跃 reveal_cp 直指边缘——没有动画观察. )
    std::string body;
    for (int i = 0; i < 80; ++i)
        body += "word" + std::to_string(i) + " ";
    Message a; a.role = Role::Assistant; a.text = body;
    m.d.current.messages.push_back(std::move(a));
    m.s.phase = agentty::phase::Idle{};

    // 手动构建部件,然后我们开它 逐步显示 DIRECTLY 下图。
    auto& cache = m.ui.view_cache.message_md(
        m.d.current.id, m.d.current.messages.back().id);
    if (!cache.streaming)
        cache.streaming = std::make_shared<maya::StreamingMarkdown>();
    cache.streaming->set_reveal_fx(true);
    cache.streaming->set_reveal_pacing(30.0, 0.25);
    cache.streaming->set_content(body);
    cache.streaming->set_live(true);
    cache.streaming->request_finalize(200);
    // 驱动部件 DIRECTLY (不通则. frame_requests_animation
    // / 视图 : cached_markdown_for finish() 是因为a
    // SETTLED 首个 帧 ,它会把最后的斜坡翻开, 并成为孤儿(
    // advance_reveal_cursor_ 早点 -outs 一次,生活,所以,
    // 我说, finalize_deadline_ 永远无法澄清 is_finalizing
    // () 永远坚持真实。 建造部件可以直接让斜坡跑到完成,然后自己自动翻转(Live_off)
    // —— 完全就是自己。 -drain 试验说,(生产不再武装一个哨所) -settle 滑翔;
    // settle_message_md 调用 finish() 直截了当的。
    // request_finalize 被保存在这里只是为了锻炼大门的
    // is_finalizing () 名词与众隔绝. )
    (void)cache.streaming->build();  // 将 逐步显示 光标提前一次

    // 和当时 逐步显示 中间 -glide 大门 必须 封锁冻结。
    CHECK(cache.streaming->reveal_in_progress()
              || cache.streaming->is_finalizing(),
          "reveal is animating (test precondition)");
    CHECK(!agentty::app::detail::live_tail_reveal_settled(m),
          "freeze BLOCKED while the reveal is still animating — freezing "
          "a mid-glide turn is the post-stream duplication root cause");

    // 排水 下水 下水道 逐步显示 完成( 完成) build()
    // 推进光标; 完成的斜坡翻转在光标到达边缘后启动。
    drain_reveal(
        [&] {
            return cache.streaming->reveal_in_progress()
                   || cache.streaming->is_finalizing()
                   || cache.streaming->is_live()
                   || cache.streaming->is_parsing();
        },
        [&] { (void)cache.streaming->build(); });

    // 现在,部件已经翻转了 活... 关闭自己和活 末尾 已经
    // 画了固定的形状——大门必须 OPEN 冰冻可以带来 字
    // 节-identical 快照.
    CHECK(agentty::app::detail::live_tail_reveal_settled(m),
          "freeze ALLOWED once the reveal fully drained (settled shape "
          "is on screen — snapshot is a maya cache HIT)");

    // 转动的转动仍然 UNCOMMITTED 传输 字
    // 节 永远无法自由,不管 逐步显示 州政府... 还没到
    m.d.current.messages.back().streaming_text = "trailing";
    CHECK(!agentty::app::detail::live_tail_reveal_settled(m),
          "freeze BLOCKED while streaming_text still has unsettled bytes");
    m.d.current.messages.back().streaming_text.clear();
}

// （4）组件仍处于 LIVE、但尚未满足其他条件时，光标必须保持启用。
//        阶段已离开 流式传输 (e.g 中间 -run 工具回合 -trip:
//        流式传输 → ExecutingTool → 流式传输) 和 编号 逐步显示 光
//        标已锁定到活边。 -but-live 窗口 render_live_panel_
//        仍然动画 scramble/gradient/ 脉冲 光标 每隔一段时间 帧 ,
//        所以,请 RAF 一定要继续射击,否则转弯会冷冻 -response.
//        is_streaming ()此处虚实,故相门( ⁇ ). wire_streaming_here
//        已经 不 盖上; is_live () 术语必须。
//        流速较慢或为工具暂停时，Markdown 在中途显得卡住。
//        ──────────────────
TEST_CASE("caret armed when live but phase not streaming") {
    std::printf("test_caret_armed_when_live_but_phase_not_streaming\n");

    Model m;
    m.d.current.id = agentty::ThreadId{"liveness4"};
    Message u; u.role = Role::User; u.text = "do some work";
    m.d.current.messages.push_back(std::move(u));
    agentty::app::detail::clear_frozen(m);
    agentty::app::detail::freeze_through(m, 1);

    // 内 -flight 助理消息, 短句 正文
    // 标记它通过一个 流式传输 渲染 先说
    Message a; a.role = Role::Assistant;
    a.streaming_text = "Ok";
    m.d.current.messages.push_back(std::move(a));
    m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};
    CHECK(frame_requests_animation(m),
          "armed on the first streaming frame (sets the widget live_)");

    // 将逐步显示光标排到活边,所以reveal_in_progress()不能
    // 是下面的帧的武器. Re-fetch通过非-migratingXXXXpeek
    // ()的每个步骤:frame_requests_animation将条目迁移到pinned/settled地图之间,
    // 使任何持有的条目
    // 无效.
    const auto live_state = [&] {
        const auto* mc =
            m.ui.view_cache.peek(m.d.current.id, m.d.current.messages.back().id);
        return mc ? mc->streaming.get() : nullptr;
    };
    drain_reveal(
        [&] { auto* s = live_state(); return s && s->reveal_in_progress(); },
        [&] { (void)frame_requests_animation(m); });
    CHECK(live_state() && live_state()->is_live(),
          "widget still live after reaching the edge (precondition)");
    CHECK(live_state() && !live_state()->reveal_in_progress(),
          "reveal cursor reached the live edge (precondition)");

    // 电话 传输 短叶 流式传输 用于工具回合 -trip。
    // 助理信息是: 不 已解决( streaming_text 仍然坚持着未
    // 解决的 字节 )所以部件保持为 live_。在 kRevealAactiveMs
    // ( 3 s) 之后的后置时钟显示 since_grow 窗口也不可能是
    // 哪个武器。 仅 编号 is_live (a) 任期未定。
    m.s.phase = agentty::phase::ExecutingTool{agentty::phase::Active{}};
    m.ui.view_cache
        .message_md_live(m.d.current.id, m.d.current.messages.back().id)
        .last_grow_tick_ms =
        maya::anim_now_ms() - 10000;   // 模拟 10 s 间 -delta 差额

    // 将动画钟提前过 maya 自己的静脉窗口 所以它的微弱制度
    // 只有自己 -arms 每阶段桶一次——连续相同
    // -bucket 建筑会变回假的,没有 agentty's
    // is_live () 大门。
    maya::testing::advance_anim_clock_ms(400);

    for (int f = 0; f < 6; ++f) {
        CHECK(frame_requests_animation(m),
              "caret STILL armed while live_ but phase=ExecutingTool, "
              "cursor at edge, recency window expired — the is_live() RAF "
              "term must keep the typewriter caret breathing");
        maya::testing::advance_anim_clock_ms(2);
    }
}

// （5）尾部 Markdown 组件仍为 live_ 时，冻结条件必须阻止冻结。
//        即便在 逐步显示 光标位于边缘,没有其它动画( reveal_in_progress
//        / is_finalizing / is_parsing 都是假的 都是
//        假的 我... 都说错了 live_tail_reveal_settled
//        ( )必须是 EXACT 镜像 build_live_tail's
//        `reveal_settled` (散列) -stamp 门),
//        哪些 Ors is_live () 如果冻结门掉下来 is_live () 将
//        绿化 -light 在一个生活状态下冻结 末尾 拒绝盖章可缓存
//        assistant_run_hash_id - 这么说吧 freeze_range
//        将刻出一个键 现场 帧 从未涂过( N) maya 缓存 MISS ,
//        重建运行在 FrozenBuildScope (show_all )在可能不同的高度,
//        并用一个复制件插入 回滚缓冲区。这个 锁 两座城门的上游集成,所以它们不能漂移。
TEST_CASE("freeze gate blocks while widget live") {
    std::printf("test_freeze_gate_blocks_while_widget_live\n");

    Model m;
    m.d.current.id = agentty::ThreadId{"freezegate_live"};
    Message u; u.role = Role::User; u.text = "q";
    m.d.current.messages.push_back(std::move(u));
    agentty::app::detail::clear_frozen(m);
    agentty::app::detail::freeze_through(m, 1);

    // 解析助理转弯:文本 FINAL ,没有 流式传输 字节 飞行中(即
    // `!streaming_text.empty()` 守卫不可能是堵住大门的...
    // 我们需要隔离 is_live () 任期。
    const std::string body = "Short settled reply body.";
    Message a; a.role = Role::Assistant; a.text = body;
    m.d.current.messages.push_back(std::move(a));
    m.s.phase = agentty::phase::Idle{};

    auto& cache = m.ui.view_cache.message_md(
        m.d.current.id, m.d.current.messages.back().id);
    cache.streaming = std::make_shared<maya::StreamingMarkdown>();
    // reveal_fx OFF 制造 reveal_in_progress (a) 无条件的虚假;
    // 不 呼叫 request_finalize 保留 is_finalizing () 虚假;
    // 无自动同步保存 is_parsing ()虚假的。 仅 无 -settled 左侧
    // 是 is_live (,这里武装 恰好是现场) -tail 渲染 拿着它 流式
    // 传输 还没有的消息 finish() 还没有,艾德。
    cache.streaming->set_reveal_fx(false);
    cache.streaming->set_content(body);
    cache.streaming->set_live(true);

    // 先决条件: 隔离 is_live()-only 州立大学.
    CHECK(cache.streaming->is_live(), "widget live_ (precondition)");
    CHECK(!cache.streaming->reveal_in_progress(),
          "reveal_in_progress false with reveal_fx off (precondition)");
    CHECK(!cache.streaming->is_finalizing(),
          "no finalize ramp armed (precondition)");
    CHECK(!cache.streaming->is_parsing(), "no async parse (precondition)");

    // 该 CONTRACT : 部件在直播时冻结门块。
    CHECK(!agentty::app::detail::live_tail_reveal_settled(m),
          "freeze BLOCKED while the md widget is still live_ — the gate "
          "must mirror build_live_tail's hash-stamp condition (is_live "
          "term) or freeze_range stamps an unkeyed run and strands a "
          "duplicate");

    // finish() 滴滴的LIVE... (另外三个已经是假的),
    // 所以现在 build_live_tail WOULD 印章可缓存密钥——
    // 冻结门必须锁上,这样交接就是 maya 缓存 HIT.
    cache.streaming->finish();
    CHECK(!cache.streaming->is_live(),
          "finish() dropped live_ (precondition)");
    CHECK(agentty::app::detail::live_tail_reveal_settled(m),
          "freeze ALLOWED once finish() settles the widget (live tail has "
          "stamped the cacheable key — freeze is a byte-identical cache HIT)");
}

// （6）深层运行：实时边界位于多个稳定子轮次下方。报告中的
//        低级 -CPU 拖放: 在长的自动驾驶中运行 流式传输 边缘
//        是一个新的占位符消息 下方 数十个已解决的工具子 -turns.
//        turn_config_for_assistant_run 包起来
//        SETTLED 子目录 -turn 自己被刺伤 -keyed 赤转(
//        R) maya 破碎,平价),但 必须 保持安静
//        -streaming 边缘是内在的 所以它 cached_markdown_for
//        内容 -arms 动画 帧 如果边缘被刺入键盘( 或 键盘) ,则 e.g .
//        因为其文本前缀看起来已经确定), maya 将粉碎缓存组件,
//        cached_markdown_ 永远也不会重新 -run ,
//        则 RAF 永远也不会重新 -arm ,打字机会冻结中 -turn ~0%
//        时 CPU 以至不相关的散列轴翻转。此选项表示边缘保持武装。 @ @ info
TEST_CASE("deep run live edge stays armed") {
    std::printf("test_deep_run_live_edge_stays_armed\n");

    Model m;
    m.d.current.id = agentty::ThreadId{"liveness6"};
    Message u; u.role = Role::User; u.text = "do a long series of edits";
    m.d.current.messages.push_back(std::move(u));
    agentty::app::detail::clear_frozen(m);
    agentty::app::detail::freeze_through(m, 1);

    // 多个 SETTLED 工具 -only 子目录 -turns (每个都发自助理
    // 消息)——深跑的quiescent前缀. Tool 终端 ,没有旁白。
    for (int e = 0; e < 40; ++e) {
        Message a; a.role = Role::Assistant;
        agentty::ToolUse tc;
        tc.id   = agentty::ToolCallId{"e" + std::to_string(e)};
        tc.name = agentty::ToolName{"edit"};
        tc.args = {{"path", "src/f" + std::to_string(e) + ".cpp"}};
        auto now = std::chrono::steady_clock::now();
        tc.status = agentty::ToolUse::Done{
            now - std::chrono::milliseconds{5}, now, "edited"};
        a.tool_calls.push_back(std::move(tc));
        m.d.current.messages.push_back(std::move(a));
    }

    // 电话 LIVE EDGE : 新鲜 流式传输 活传的占位符 字节
    // 中间 -tool-execution 阶段( 传输 左边 流
    // 式传输 用于工具回合 -trip。这是必须保持内置+武装的信息。
    Message edge; edge.role = Role::Assistant;
    edge.streaming_text = "Now I will summarize the edits I performed";
    m.d.current.messages.push_back(std::move(edge));
    m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};
    CHECK(frame_requests_animation(m, 100, 4000),
          "armed on the first streaming frame of the deep-run edge");

    // 边缘不断接收 字节 (实数) 数据流 ),所以它保持活泼.
    // 数个 帧 包括复时钟在 kRevealAactiveMs
    // 和相位叶后 流式传输 用于工具回合 -trip 边缘的建筑
    // 必须重新 -arm 动画 帧 每个 时间,
    // 如果深处 -run 键入边缘 隐藏在赤转弯,
    // cached_markdown_for 将停止运行,手臂将下降(低点) -CPU ) ,
    // 以"冻结"为主.
    m.s.phase = agentty::phase::ExecutingTool{agentty::phase::Active{}};
    for (int f = 0; f < 6; ++f) {
        // 进食a 字节 故边缘毫不含糊地仍然 流式传输 ,
        // 并返回到 -message 静静的钟表 since_grow
        // 窗口不能是哪个臂——只有内线 逐步显示 路径可以。
        m.d.current.messages.back().streaming_text += ".";
        auto& edge_cache = m.ui.view_cache.message_md(
            m.d.current.id, m.d.current.messages.back().id);
        edge_cache.last_grow_tick_ms =
            maya::anim_now_ms() - 10000;   // 模拟 10 s 间 -delta 差额
        CHECK(frame_requests_animation(m, 100, 4000),
              "deep-run live edge STILL armed (40 settled sub-turns above, "
              "phase=ExecutingTool, recency expired) — the streaming edge "
              "must be built inline, never stably keyed");
        maya::testing::advance_anim_clock_ms(2);
    }
}


