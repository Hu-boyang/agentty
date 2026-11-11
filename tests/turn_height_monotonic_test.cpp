// turn_height_monotonic_test ——活转弯的高度只会不断增大.
//
// 电话 线程 已打印 上方 作曲家,
// 所以一个 帧 将作曲和状态栏拖到用户光标下;
// 可视区域 上面是属于 终端 属于自己的
// 回滚缓冲区 ,在无 重绘 心理医生
// 不会摇摆不定,而是腐败
//
// 每一个部件都有理由转弯 单调递增的 他们每逢是被考验的,
// 是他们所居住的地方。 md_shape_sweep 以倾斜
// 的形状发誓, reasoning_ticker_height_test
// 用于思维勾选器的行窗口,工具 正文 '行界限在他们自己的测试中。
// 此测试是询问用户实际体验的问题: 驱动 一个 转身
// 状态更新函数 和真实的 ui::view 提供商的
// 做法——推理,然后是工具 调用 与 流式传输 输出,
// 然后是动画 帧 在那之间,你们应当观察所画的高度。
//
// 失败会显示高度下降的步数和多少。
// 两次探测器,当它发现时: TRACE_ROWS =1 记录每个高度变化
// 及其阶下名称, DUMP_STEP=<prefix> 打印整个涂鸦 帧
// 相匹配的阶梯上, 都在这里找到真正的错误。

#include "agtest.hpp"

#include <maya/core/anim_clock.hpp>
#include <maya/core/render_context.hpp>
#include <maya/render/canvas.hpp>
#include <maya/render/renderer.hpp>
#include <maya/print.hpp>
#include <maya/style/theme.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/msg.hpp"
#include "agentty/runtime/view/view.hpp"

namespace A = agentty;
namespace D = agentty::app::detail;

namespace {

constexpr int kWidth  = 72;
constexpr int kTermH  = 24;
constexpr std::uint64_t kExecSeq = 1;

A::Model apply(A::Model m, A::msg::StreamMsg event) {
    auto [next, cmd] = D::step(D::stream_update, std::move(m), std::move(event));
    (void)cmd;
    return std::move(next);
}

A::Model apply_tool(A::Model m, A::msg::ToolMsg event) {
    auto [next, cmd] = D::step(D::tool_update, std::move(m), std::move(event));
    (void)cmd;
    return std::move(next);
}

// 绘制 真实 视图并报告其占用的行。
int view_rows(const A::Model& m) {
    maya::RenderContext ctx{kWidth, kTermH, maya::render_generation(),
                            /*auto_height=*/true};
    maya::RenderContextGuard guard(ctx);
    maya::StylePool pool;
    maya::Canvas c(kWidth, 8000, &pool);
    c.clear();
    std::vector<maya::layout::LayoutNode> nodes;
    maya::render_tree(A::ui::view(m), c, pool, maya::theme::native, nodes,
                      /*auto_height=*/true);
    return c.max_content_row() + 1;
}

std::string view_text(const A::Model& m) {
    maya::RenderContext ctx{kWidth, kTermH, maya::render_generation(),
                            /*auto_height=*/true};
    maya::RenderContextGuard guard(ctx);
    return maya::render_to_string(A::ui::view(m), kWidth);
}

struct Watch {
    const A::Model& m;
    int   prev = 0;
    int   drops = 0;
    int   worst = 0;
    std::string worst_step;
    // 持续时间的滑动: 运行高峰以下行超过一个 帧 或两只。 maya
    // 内含 渲染器 桥a TRANSIENT 1-2行 接口边界 粘贴
    // ( 活动) -indicator ——第一个 -revealed-char
    // 交接),并有运行 -max 抓住,所以类在应用中是隐形的。
    // 没有比较大的或更长的,而这些都是用户看到的收缩。
    int   peak = 0;
    int   below_peak_frames = 0;
    int   worst_run = 0;
    std::string worst_run_step;

    void frame(const std::string& step) {
        const int rows = view_rows(m);
        if (const char* want = std::getenv("DUMP_STEP");
            want && step.rfind(want, 0) == 0)
            std::fprintf(stderr, "=== %s (%d rows) ===\n%s\n",
                         step.c_str(), rows, view_text(m).c_str());
        if (std::getenv("TRACE_ROWS") && rows != prev)
            std::fprintf(stderr, "  %-28s %3d -> %3d\n", step.c_str(), prev, rows);
        if (rows < prev) {
            ++drops;
            if (prev - rows > worst) { worst = prev - rows; worst_step = step; }
        }
        if (rows < peak) {
            ++below_peak_frames;
            if (below_peak_frames > worst_run) {
                worst_run = below_peak_frames;
                worst_run_step = step;
            }
        } else {
            below_peak_frames = 0;
            peak = rows;
        }
        prev = rows;
    }
    void tick(const std::string& step) {
        maya::testing::advance_anim_clock_ms(20);
        frame(step);
    }
};

// 提供商提供它的原因: 每三角洲几句, 段落中断。
// 足够长的时间滑过窗口的封顶, 并形状包括一个节点
// 崩溃的大小写 -counted 窗口—— a
// SHORT 段落在长节后成为最新的节点。
const std::vector<std::string>& reasoning_deltas() {
    static const std::vector<std::string> d = {
        "Let me look at what the user is asking for here, because there are "
        "two separate questions bundled together.",
        "\n\nFirst, the height question. The composer sits below the thread, "
        "so anything that shrinks mid-stream drags it up under the cursor.",
        "\n\nShort note.",
        "\n\nSecond, the continuity question: reasoning, then tools, then "
        "prose should read as one flow rather than three widgets taking "
        "turns to appear.",
        "\n\nOk.",
        "\n\nSo the plan is to make the window row-exact and keep every "
        "other block append-only, then pin it with a test that drives the "
        "whole turn rather than one widget.",
        "\n\nThe window is the interesting half. A node window cannot hold a "
        "height, because a paragraph wraps to however many rows it wraps to "
        "and the count changes as the newest node changes.",
        "\n\nRight.",
        "\n\nA row window can: cut the body at a row boundary, splitting a "
        "paragraph's wrapped lines if that is where the boundary falls, and "
        "the height becomes exactly the cap.",
        "\n\nOne more.",
        "\n\nThen the tool cards and the prose need the same promise, which "
        "they already have through their own row bounds, so the remaining "
        "work is a test that watches the whole turn at once.",
        "\n\nDone thinking.",
    };
    return d;
}

}  // 命名空间结束。

static void sweep_one_turn(A::ui_prefs::Thinking thinking, const std::string& label) {
    maya::testing::freeze_anim_clock(0);

    A::Model m;
    m.d.current.id = A::ThreadId{"monotonic"};
    m.d.persisted.ui.thinking = thinking;   // view() 发布此选项
    D::clear_frozen(m);

    // 一个定居的用户转过数据流,
    // 所以直播内容有它制作方式的强制前缀.
    {
        A::Message u;
        u.role = A::Role::User;
        u.text = "Walk me through how the turn is rendered while it streams.";
        m.d.current.messages.push_back(std::move(u));
    }

    A::Message a;
    a.role = A::Role::Assistant;
    m.d.current.messages.push_back(std::move(a));
    m.s.phase = A::phase::Streaming{A::phase::Active{}};

    Watch w{m};
    w.frame("open");

    // ── 1. 理由 ────────────────────────────────────────────────────
    for (std::size_t i = 0; i < reasoning_deltas().size(); ++i) {
        m = apply(std::move(m), A::StreamThinkingDelta{reasoning_deltas()[i]});
        w.tick("reasoning" + std::to_string(i));
        // 动画 帧 没有新内容 字节 : 数据 逐步显示
        // 移动它自己的时钟上的活边,这也不能改变高度。
        for (int f = 0; f < 3; ++f) w.tick("reasoning" + std::to_string(i) + "-anim");
    }

    // ── 2. 工具调用,产出成片 ───────────────
    m = apply(std::move(m), A::StreamToolUseStart{A::ToolCallId{"call-1"},
                                                  A::ToolName{"shell"}});
    w.tick("tool-start");
    m = apply(std::move(m), A::StreamToolUseSnapshot{
        A::ToolCallId{"call-1"}, R"({"command":"cmake --build build -j12"})"});
    w.tick("tool-args");
    m = apply(std::move(m), A::StreamToolUseEnd{A::ToolCallId{"call-1"}});
    w.tick("tool-end");

    // 运行,因为执行者会离开它, 所以进步 快照 下方是真实
    // 的 状态更新函数。该词 PHASE 动作也: 工具正在执行,
    // 而不是 phase::Streaming ——而
    // 观点读取阶段来决定推理块是什么.
    {
        auto& calls = m.d.current.messages.back().tool_calls;
        REQUIRE(!calls.empty());
        const auto now = std::chrono::steady_clock::now();
        calls.back().status = A::ToolUse::Running{now, {}, {}, now, kExecSeq};
    }
    m.s.phase = A::phase::ExecutingTool{A::phase::Active{}};
    w.tick("tool-running");

    // 输出一个建筑实际到达的方式:许多线条,交替长(宽度为这个宽度
    // 的包)和短线。 LINE-counted 窗口在文本中
    // WRAPS 和节点窗口一样的滑动, 所以窗口必须滑动,
    // 检查才意味着什么。 Name 传输 发送一个 SNAPSHOT 每次,
    // 不是三角洲。
    std::vector<std::string> out;
    for (int i = 1; i <= 24; ++i) {
        if (i % 3 == 0) {
            out.push_back("ok\n");
        } else if (i % 3 == 1) {
            out.push_back("[" + std::to_string(i) +
                          "/24] Building CXX object CMakeFiles/agentty.dir/src/"
                          "runtime/view/thread/turn/agent_timeline/tool_args.cpp.o\n");
        } else {
            out.push_back("[" + std::to_string(i) + "/24] Building main.cpp.o\n");
        }
    }
    std::string snapshot;
    for (std::size_t i = 0; i < out.size(); ++i) {
        snapshot += out[i];
        m = apply_tool(std::move(m), A::ToolExecProgress{
            A::ToolCallId{"call-1"}, snapshot, kExecSeq});
        w.tick("tool-output" + std::to_string(i));
        w.tick("tool-output" + std::to_string(i) + "-anim");
    }

    // 工具( E) FINISHES。卡片停止了。
    // 渲染 生活进步与开始 渲染 以及这两个不同的渲染器,
    // 有两个不同的行政策—— 移交不能失去行。
    {
        A::ToolExecOutput done{A::ToolCallId{"call-1"}, snapshot};
        done.exec_seq = kExecSeq;
        m = apply_tool(std::move(m), std::move(done));
    }
    for (int f = 0; f < 4; ++f) w.tick("tool-done");

    // 其结果回到模型,然后下一个请求打开.
    m.s.phase = A::phase::Streaming{A::phase::Active{}};
    for (int f = 0; f < 4; ++f) w.tick("sub-turn-2-open");

    // ── 2b. 更多的推理,工具结果 之后 ──────────────────────
    // 互换思维和多种 -item 答复 数据流 一次子程序时
    // 提供更多推理 -turn 已经行动,而这是正常的 代理转动形状。
    // 帧 中间的闪烁是: 逐步显示 闲在那里, 和任何键
    // 街区的形状关闭 "is the typewriter animating"
    // 变化间隙中的形状。
    for (std::size_t i = 0; i < 4; ++i) {
        m = apply(std::move(m), A::StreamThinkingDelta{
            i == 0 ? "\n\nThe build came back green, so the next thing to "
                     "check is whether the window still holds its height "
                     "when reasoning resumes after a tool call."
                   : (i == 1 ? "\n\nShort."
                   : (i == 2 ? "\n\nThat is the case the single-phase sweep "
                               "never rendered, because it never let the "
                               "reveal go idle and then fed it more bytes."
                             : "\n\nChecking now."))});
        w.tick("reasoning2-" + std::to_string(i));
        // 摊位:没有新货 字节 暂时, 帧 还在画画
        for (int f = 0; f < 25; ++f)
            w.tick("reasoning2-" + std::to_string(i) + "-quiet");
    }

    // ── 3. 答 问 ──────────────────────────────────────────────────
    const char* prose[] = {
        "The build is green.",
        " The turn renders top to bottom: the reasoning block first, then a "
        "card per tool call, then the answer prose.",
        "\n\nEach one is append-only while it streams.",
        "\n\nShort.",
        "\n\n- the reasoning window is cut by rows\n- the tool cards carry a "
        "row bound\n- the markdown keeps its shape across every prefix",
    };
    for (std::size_t i = 0; i < std::size(prose); ++i) {
        m = apply(std::move(m), A::StreamTextDelta{prose[i]});
        w.tick("prose" + std::to_string(i));
        for (int f = 0; f < 3; ++f) w.tick("prose" + std::to_string(i) + "-anim");
    }

    // ── 4. 解决 ──────────────────────────────────────────────────────
    {
        auto& b = m.d.current.messages.back();
        b.text += b.streaming_text;
        b.streaming_text.clear();
        D::settle_message_md(m, b);
    }
    m.s.phase = A::phase::Idle{};
    for (int f = 0; f < 10; ++f) w.tick("settle");

    maya::testing::unfreeze_anim_clock();

    INFO(label, ": ", w.drops, " dips, worst -", w.worst,
         " rows at '", w.worst_step, "'; longest below-peak run ",
         w.worst_run, " frames at '", w.worst_run_step, "'");
    // 合同:没有比2更深的浸泡 -row 接口边界 交接
    // maya 桥上没有 LASTS 真正的心理医生(窗口释放
    // 中点) -turn 13到32行,但两者都失败了。
    CHECK(w.worst <= 2);
    CHECK(w.worst_run <= 2);

    // 不空虚:转弯真的 渲染 ,所有三条车道都在 帧。
    // 一个没有画画的视图会通过上面的支票。
    const std::string frame = view_text(m);
    INFO(label, " final frame:\n", frame);
    CHECK(w.prev > 20);
    CHECK(frame.find("Short.") != std::string::npos);          // 回答流言
    CHECK(frame.find("row bound") != std::string::npos);       // 答题列表
    CHECK(frame.find("cmake --build") != std::string::npos);   // 工具卡
}

TEST_CASE("turn: a live turn's height never shrinks, from reasoning to prose") {
    // Thinking::Collapsed是默认:推理块是一个
    // 排行窗口,它会成长到它的盖子,然后保持.
    sweep_one_turn(A::ui_prefs::Thinking::Collapsed, "collapsed");
}

TEST_CASE("turn: the same holds with reasoning shown in full") {
    // Thinking::Shown 根本没有窗口,
    // 所以这个街区 继续增长 -- 另一条路是 单调递增的 ,
    // 而那个强调下方的标记而不是窗口。
    sweep_one_turn(A::ui_prefs::Thinking::Shown, "shown");
}
