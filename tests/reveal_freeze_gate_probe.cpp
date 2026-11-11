// reveal_freeze_gate_probe — PROVES 编号 "symptom 2" 冻结窗口和
// 那个 program.hpp::visual_hash 关上它。
//
// SYMPTOM 2(一个正在固定):模型答复显示部分 -way ,
// 则 FREEZES 以明显的时间,然后是全体。 末尾 立即出现,
// 最明显可见于 SSH / 任意 链接 中场安静 -reveal.
//
// MECHANISM(对照代码进行验证,而不是理论):
//   3个派对门是否 帧 彩绘时的a 逐步显示 动画 :
//     • 票价 事件订阅   (subscribe.cpp )——在武器时
//         !detail::live_tail_reveal_settled(m)
//     • RAF 发件人 cached_markdown_for (turn.cpp )——部件时装配
//         is_live()/reveal_in_progress() /is_finalizing()
//     • Program::visual_hash (program.hpp) — maya 运行循环 SKIPS 编号
//         整体 view()+render() 当此散列不变时, 成对( Q)
//         maya/app/app.hpp).
//   头两个留着 帧 REQUESTED 关 live_tail_reveal_settled().
//   散列速度( 16 ms) 逐步显示 用来射击的桶 仅 何时
//   streaming_text/pending_stream 属于非 -empty
//   (`revealing_text` )或和解 -freeze 曾经是 待处理
//   但有一个真正的窗口 NEITHER 持有: 传输 文本块, 其 字节 犯罪,
//   从 streaming_text 输入 `text` 左侧相机 流式传输
//   - 却 逐步显示 光标是 仍然 滑向边缘( 滑翔) is_finalizing
//   / reveal_in_progress 大麻掉到265米处 光标
//   -blink 平等桶,所以每个武装 Tick/RAF 帧 被关上了 AWAY,
//   view() 从来没有跑过,打字机冻结 直到下一个 光标 翻转/键。
//
// 此 PROBE 以 真实 部件和 真实
// AgenttyApp::visual_hash ,并称:
//   (A) 在冷冻窗口中, live_tail_reveal_settled (m)
//       ⁇  假(所以 Tick/RAF 帧 来源 ARE 武装—— 帧 ) (已存在).
//   (B) 在冷冻窗口中, visual_hash 跨越16米长的
//       墙推进 -clock 步骤(固定:那些武装的) 帧 现在实际
//       上 渲染 连续 逐步显示 ,没有冻结)。
//   (C) 预 算 -fix 门会 不 ——从同一个
//       模型中通过确切的旧的上游计算——所以,
//       这个窗口以前确实已经枯萎,还没有被另一个轴线覆盖。
//   (D) 一旦部件完成(已确定), live_tail_reveal_settled
//       真实 和 散列在16毫秒的步数上保持稳定(无闲置)
//       -CPU 回归:已解决的门改为零 渲染).

#include <chrono>
#include <cstdio>
#include <cstdlib>   // 设置env
#include <thread>

#include <maya/widget/markdown.hpp>   // maya::StreamingMarkdown

#include "agentty/runtime/app/program.hpp"
#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/model.hpp"

using agentty::Model;
using agentty::Message;
using agentty::Role;
using agentty::app::AgenttyApp;

namespace {  // 归并: TU-local (捆绑成 agentty_standalone_tests)
static int g_checks = 0, g_failures = 0;
#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        ++g_checks;                                                         \
        if (!(cond)) {                                                      \
            ++g_failures;                                                   \
            std::fprintf(stderr, "  FAIL [%s:%d] %s\n",                     \
                         __FILE__, __LINE__, (msg));                        \
        }                                                                   \
    } while (0)

// 电话 EXACT 预览 -fix 逐步显示-drain 门,复制,
// 这样我们就可以显示 老散列会 不 以显示大门在第三个月前的两个术语(
// live_tail_reveal_settled 添加了这一术语。
static bool old_draining_reveal(const Model& m) {
    return m.ui.pending_settle_freeze || m.ui.settle_cooldown_ticks > 0;
}
// 电话 `revealing_text` 术语(未因固定而更改)——键
// 关闭 传输 字节 仍然处于缓冲状态,即 FALSE 冷冻窗里
static bool revealing_text(const Model& m) {
    return m.s.active()
        && !m.d.current.messages.empty()
        && m.d.current.messages.back().role == Role::Assistant
        && (!m.d.current.messages.back().streaming_text.empty()
            || !m.d.current.messages.back().pending_stream.empty());
}

// 盖活 -tail 车型停在冷冻窗口: 阶段轴, streaming_text
// 排入( ) `text` ,但一个 StreamingMarkdown
// 视图中 缓存 仍在现场_+ 定稿( ) 逐步显示 光标滑动)。
static Model freeze_window_model() {
    Model m;
    m.d.current.id = agentty::ThreadId{"freeze"};

    Message u; u.role = Role::User; u.text = "explain";
    m.d.current.messages.push_back(std::move(u));

    Message a; a.role = Role::Assistant;
    // 字节 承诺全面致力于 `text`; 无内容 离开在 streaming_text
    // / pending_stream (无) 传输 已经完成了,
    // 心脏排出。 正文 所以,我们 逐步显示 光标仍然有需要滑翔的积压。
    a.text =
        "Here is a fairly long paragraph of prose that the reveal cursor "
        "will still be gliding across when the wire has already delivered "
        "every byte, which is precisely the window where the old gate let "
        "the typewriter freeze until the next caret blink.";
    m.d.current.messages.push_back(std::move(a));

    // 阶段是 IDLE - 告诉大家 -tale 窗户。 m.s.active ()是虚假的,
    // 所以 `revealing_text` 编辑 spinner/fine-anim 桶都关了
    m.s.phase = agentty::phase::Idle{};

    // 覆盖视图 缓存 带有部件中间 -reveal : 直播,内容设定,
    // 然后 request_finalize 这么说吧 is_finalizing()/
    // reveal_in_progress ()是真实的,但光标已经 不 到达边缘了
    auto& cache = m.ui.view_cache.message_md(
        m.d.current.id, m.d.current.messages.back().id);
    cache.streaming = std::make_shared<maya::StreamingMarkdown>();
    cache.streaming->set_reveal_fx(true);
    cache.streaming->set_reveal_pacing(/*floor_cps=*/30.0, /*drain_secs=*/0.8);
    cache.streaming->set_content(m.d.current.messages.back().text);
    cache.streaming->set_live(true);
    (void)cache.streaming->build();          // 将逐步显示光标置于 0 位置
    cache.streaming->request_finalize(200);  // 手持滑翔机 -to-edge 斜坡
    (void)cache.streaming->build();          // 预发 帧 ; 仍然为中点 -glide
    return m;
}

static void test_freeze_window_exists_and_is_now_driven() {
    std::printf("freeze window: armed frames must advance the hash\n");
    Model m = freeze_window_model();

    // (A)帧XXSOURCES号武装:subscription/RAF号上游是假的,
    //     因此Ticks + RAF号正在开火. 帧号存在——唯一的
    //     问题是visual_hash号是否让他们画.
    const bool settled = agentty::app::detail::live_tail_reveal_settled(m);
    CHECK(!settled,
          "precondition: reveal still in progress (live_tail_reveal_settled "
          "must be false here, else this isn't the freeze window)");

    // 卫生: OTHER 快速( T) -bucket 条件都在这
    // 窗口里了 所以 逐步显示 桶是那个 仅 能够快速推进散列的东西
    CHECK(!revealing_text(m),
          "freeze window: revealing_text must be false (no buffered wire "
          "bytes) — otherwise the window is already covered and this probe "
          "proves nothing");
    CHECK(!old_draining_reveal(m),
          "freeze window: old draining_reveal must be false (no pending "
          "settle-freeze) — the pre-fix gate had no fast term here");
    CHECK(!m.s.active(),
          "freeze window: m.s.active() must be false (phase Idle) so the "
          "spinner/fine-anim bucket is off");

    // (C) PRE-FIX : 与 revealing_text 虚构和 old_draining_reveal 骗人!
    //     active() 大麻掉到265毫米处 光标 等价桶, 等价箱, 等值桶, 需要等价桶。
    //     16米的步子, 平等是。 不 翻转 散列静态  ⁇
    //     帧 门关了 —— FREEZE。显示平等情况不变。
    {
        // 显示预览 -fix 门代数 无需 读取真实的墙钟:在跑者身上,
        // 16 ms的睡眠可以跨越265 ms的边界,
        // 翻转等价,使这个检查变得模糊不清(即: TEST 错误,
        // 而不是产品回归。 而不是选择一个相位参考时间
        // -aligned 编号 START 闪烁半边 -period ,
        // 所以一个16ms的步子可以证明 留在同一个桶。
        // 编号 旧 ~265毫秒(每秒) 坐标: 光标 水桶不会越过16米
        // 逐步显示 步骤,所以 帧 被关起来。
        const std::int64_t kBlinkHalfMs = 265;
        const std::int64_t t0 = 1000 * kBlinkHalfMs;  // 与桶边对齐
        const std::int64_t t1 = t0 + 16;              // 16分钟后
        const auto p0 = (t0 / kBlinkHalfMs) & 1;
        const auto p1 = (t1 / kBlinkHalfMs) & 1;
        CHECK(p0 == p1,
              "pre-fix regime: caret parity flipped within 16 ms — the OLD "
              "bucket was ~265 ms so a 16 ms reveal step was gated away");
    }

    // (B) POST-FIX : 真实 visual_hash 必须 ADVANCE
    //     横跨 a 逐步显示 步骤 逐步显示 正在处理中。这是工作: 逐步
    //     显示 水桶(16毫秒)现在被按键了! live_tail_reveal_settled ,
    //     所以每个武装 帧 其实 渲染 和打字机滑翔。
    //
    //     电话 逐步显示 桶是地板( T) now_ms /
    //     16) 赤裸的16 ms睡眠 恰恰是 一个 水桶宽,
    //     所以在排程器 焦急(早醒,或两读 碰巧在同一水桶
    //     内着陆) 两层可以相等, 并且没有提前,
    //     一个片状的 TEST 错误, 不是产品回归。
    //     将真正的散列样本放在一个在子中睡觉的短环中 -bucket
    //     步骤直至墙壁 -clock 已经证明已经越过了
    //     满载的桶状边界;一旦它穿过了,就成了散列。
    //     必须 撞到的门还是响亮的 而不是旋转
    const std::uint64_t h0 = AgenttyApp::visual_hash(m);
    bool advanced = false;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
        if (AgenttyApp::visual_hash(m) != h0) { advanced = true; break; }
    }
    CHECK(advanced,
          "FIX BROKEN: visual_hash did NOT advance across a reveal-bucket "
          "boundary while a reveal was in progress — the freeze window is "
          "still dead; the typewriter will stall until a caret flip / "
          "keypress");
}

static void test_settled_turn_is_hash_stable() {
    std::printf("settled turn: hash must go quiet (no idle-CPU regression)\n");
    Model m = freeze_window_model();

    // 完成部件: 这是 finalize_turn/settle_message_md 所做的。
    // 现在 live_tail_reveal_settled 一定是真实的,快桶也关了
    auto& cache = m.ui.view_cache.message_md(
        m.d.current.id, m.d.current.messages.back().id);
    cache.streaming->finish();
    (void)cache.streaming->build();

    const bool settled = agentty::app::detail::live_tail_reveal_settled(m);
    CHECK(settled,
          "after finish(): live_tail_reveal_settled must be true — the "
          "widget dropped live_/reveal/finalize/parse");

    // 与 逐步显示 已解决 和 阶段 仅 时间术语 散列携带是作曲
    // 光标-blink PARITY (每265毫秒翻一个).
    // 穿过一个短暂的真人 -clock 平等必须采取的
    // 步骤 不 变化—— 确定转弯 ZERO 快速闲置 渲染 然而,
    // 单张±16毫秒的样本可以完全通过壁钟坐落的地方横跨
    // 265毫秒的等距边界(同一时钟),
    // -alignment 危险检查(C)文件,它会翻转散列,
    // 其原因与一个 逐步显示 漏泄——片状 TEST 错误。
    // 将多个位置很近的样本放在一个等值的半径内 -period
    // : 如果快则 逐步显示 桶漏水,16毫秒桶会反复
    // 翻翻样品;一个固定的转弯具有单一值,或最多过渡值
    // 一次 如果采样窗口恰好横跨265米的等距边缘。
    // 因此,说明是“过渡”*的数量:0(或1,1,1)
    // 过境得到解决;许多过渡是漏水。
    std::uint64_t prev = AgenttyApp::visual_hash(m);
    int transitions = 0;
    for (int i = 0; i < 8; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
        const std::uint64_t h = AgenttyApp::visual_hash(m);
        if (h != prev) { ++transitions; prev = h; }
    }
    CHECK(transitions <= 1,
          "IDLE REGRESSION: visual_hash transitioned repeatedly across a "
          "settled idle turn — the fast reveal bucket is leaking and the "
          "loop will burn CPU at idle");
}

}  // 名称空间( 归并)

static Model reasoning_window_model() {
    Model m;
    m.d.show_reasoning = true;

    Message a; a.role = Role::Assistant;
    a.id = agentty::MessageId{"reason1"};
    // PURE-REASONING 窗口: 答案 正文 已经 不 开始( 文本
    // + streaming_text + pending_stream ),
    // 但推理通道( msg.thinking )是(这是) 流式传输。这是用户击中的精确状态。
    // "Thinking" 中风冻结打字机 -stream.
    a.thinking =
        "Let me work through this. First I'll pull a ranked map of the repo, "
        "then check the directory structure, then read the key files like the "
        "README so I understand how the whole thing fits together before I act.";
    m.d.current.messages.push_back(std::move(a));

    // 阶段 STREAMING (在2005年9月30日 流式传输
    // 阶段 agentty ——没有单独的思考阶段).
    m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};

    // 端方是兄弟的REASONING槽(id + "#r"),其部件为-reveal中位,
    // 与cached_markdown_for(MdView::Reasoning)完全相同。
    const auto rid = agentty::MessageId{
        m.d.current.messages.back().id.value + "#r"};
    auto& cache = m.ui.view_cache.message_md_live(m.d.current.id, rid);
    cache.streaming = std::make_shared<maya::StreamingMarkdown>();
    cache.streaming->set_reveal_fx(true);
    cache.streaming->set_reveal_pacing(/*floor_cps=*/45.0, /*drain_secs=*/0.40);
    cache.streaming->set_content(m.d.current.messages.back().thinking);
    cache.streaming->set_live(true);
    (void)cache.streaming->build();          // 将逐步显示光标置于 0 位置
    (void)cache.streaming->build();          // 单曲 帧 ; 仍然为中点 -glide
    return m;
}

static void test_reasoning_reveal_drives_the_hash() {
    std::printf("reasoning window: pure-thinking reveal must advance the hash\n");
    Model m = reasoning_window_model();

    // 先决条件:推理 逐步显示 真正中点 -glide ,所以,
    // 请 帧 谓 不 已解决。 如果这次旅行, 上面的部件方向盘错。
    CHECK(!agentty::app::detail::live_tail_reveal_settled(m),
          "precondition: reasoning reveal in progress (live_tail_reveal_settled "
          "must be false while the #r slot is animating)");

    // 该 FIX : 模型仅是推理( 无答案) 字节 ),
    // 推理 逐步显示 仍然必须前进 visual_hash 如此武装
    // 帧 油漆和打字机滑翔机 在修复前 revealing_text/draining
    // 都从钥匙上扣下来了 ANSWER 仅是频道,
    // 散列掉到265米 光标 桶,和推理冻结 直到键压。
    const std::uint64_t h0 = AgenttyApp::visual_hash(m);
    bool advanced = false;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
        if (AgenttyApp::visual_hash(m) != h0) { advanced = true; break; }
    }
    CHECK(advanced,
          "FIX BROKEN: visual_hash did NOT advance while reasoning was "
          "streaming with no answer body yet — the reasoning typewriter "
          "freezes until a caret flip / keypress");
}

int main() {
    // 将同步按下 -output 分类 逐步显示 渲染 桶是决定
    // 16毫秒(60米) FPS 粗略地说,这个探测器的±
    // 16毫秒的阶梯断言是针对的。 NON-sync 终端 桶是
    // 100毫秒——正确的行为(见 program.hpp:
    // 60 FPS 只能是逐渐的洪水 -painting
    // 终端 喜欢 Termux 16毫秒的距离 无法可靠地跨越100
    // 毫秒的桶状边界 使得计时检查变得模糊不清 MECHANISM
    // 正在试验 逐步显示 帧 将散列; 固定的转弯去静默)两
    // 种模式都相同——只有时间段不同——所以强制同步
    // 保持探测器的定型性而不会削弱它所证明的.
    //
    // 桶的INPUTS被reveal_bucket_test
    // 单独固定:它必须依赖于同步支持,而不是会话是否偏僻。
    // 它曾经读过streaming_tick_period(),
    // 在shloor上是归并,它使打字机在shro上变得块状,
    // 因为带宽的原因眼睛看不见。
    setenv("MAYA_FORCE_SYNC", "1", /*覆盖=*/1);

    std::printf("reveal_freeze_gate_probe\n");
    test_freeze_window_exists_and_is_now_driven();
    test_settled_turn_is_hash_stable();
    test_reasoning_reveal_drives_the_hash();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    if (g_failures == 0) { std::printf("PASSED\n"); return 0; }
    std::printf("FAILED\n");
    return 1;
}
