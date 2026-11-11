// reasoning_render_test.cpp ——推理(. "Thinking"
// 块 渲染 提供方和生命周期状态。
//
// 说明案文来源 每个 提供者漏斗进入 Message::thinking 途经 StreamThinkingDelta
// (Anthropic thinking_delta, Codex reasoning_summary_text .
// 德尔塔, OpenAI-compat reasoning_content ),
// 以及 Message::reasoning_display_text ()是关闭视图键的单一统一存取器。此针 :
//   1. 统一存取者优先(思考,否则为reasoning_summary),
//   2. 说明a SETTLED 推理转弯 渲染 编号 完整 推理文本( 实际文本)
//      不 归并 改为: -line 内容提要) "Reasoned" 头文件,
//      上方 答案是,
//   3. 转弯时 NO 推理 渲染 也没有。

#include "agtest.hpp"

#include <functional>
#include <maya/widget/reasoning.hpp>

#include <cstdlib>   // 设置env—— 逐步显示 测试针 副作用 明确

#include "agentty/runtime/model.hpp"
#include "agentty/domain/ui_live.hpp"   // 思维准备打开这个街区
#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/view/thread/thread.hpp"
#include "agentty/runtime/view/changes_strip.hpp"
#include "agentty/runtime/view/composer.hpp"
#include "agentty/runtime/view/status_bar/status_bar.hpp"

#include <maya/widget/app_layout.hpp>
#include <maya/render/canvas.hpp>
#include <maya/render/renderer.hpp>
#include <maya/core/anim_clock.hpp>
#include <maya/style/theme.hpp>

#include <print>
#include <string>
#include <vector>

using namespace agentty;

namespace {

// 渲染 整个应用程序(包括图文)到平原 ASCII 文本。镜像 midrun_seam_test's
// render_rows 系系; 非 -ASCII 格言( QQ/ 符号)
// 映射到 '?' 因此,断言针对的是 ASCII 语句.
std::string render_text(const Model& m, int width = 100, int height = 4000) {
    auto root = maya::AppLayout{{
        .thread        = ui::thread_config(m),
        .changes_strip = ui::changes_strip_config(m),
        .composer      = ui::composer_config(m),
        .status_bar    = ui::status_bar_config(m),
        .overlay       = std::nullopt,
    }}.build();

    maya::StylePool pool;
    maya::Canvas canvas(width, height, &pool);
    canvas.clear();
    maya::render_tree(root, canvas, pool, maya::theme::native, true);

    std::string out;
    const int max_row = canvas.max_content_row();
    for (int y = 0; y <= max_row; ++y) {
        for (int x = 0; x < width; ++x) {
            char32_t ch = canvas.get(x, y).character;
            if (ch == 0) ch = U' ';
            out.push_back(ch < 128 ? static_cast<char>(ch) : '?');
        }
        out.push_back('\n');
    }
    return out;
}

bool has(const std::string& s, std::string_view needle) {
    return s.find(needle) != std::string::npos;
}

Message assistant(std::string text) {
    Message a;
    a.role = Role::Assistant;
    a.id   = MessageId{"a1"};
    a.text = std::move(text);
    return a;
}

} // 命名空间结束。

TEST_CASE("reasoning: unified accessor precedence") {
    Message a = assistant("answer");
    check(a.reasoning_display_text().empty(), "no reasoning by default");
    check(!a.has_reasoning(), "has_reasoning false when empty");

    a.reasoning_summary = "legacy summary";
    check(a.reasoning_display_text() == "legacy summary",
          "falls back to reasoning_summary");

    a.thinking = "primary thinking";
    check(a.reasoning_display_text() == "primary thinking",
          "thinking (the unified stream field) wins over reasoning_summary");
    check(a.has_reasoning(), "has_reasoning true when thinking present");
}

TEST_CASE("reasoning: settled turn shows the full reasoning, not a fold") {
    Model m;
    // Thinking::Collapsed (默认) SHOWS 块; 只有隐藏来压制它。
    Message a = assistant("Here is the FINAL_ANSWER_MARKER.");
    // 理由已经到达(任何提供者),答案是在场的。 SETTLED:
    // 渲染 编号 完整 流式推理( 无) 归并 ),a项下 "Reasoned" 头文件.
    a.thinking = "Analyze the request FIRST_LINE_MARKER\n"
                 "then a SECOND_LINE_MARKER that must ALSO stay visible.";
    m.d.current.messages.push_back(std::move(a));
    m.s.phase = phase::Idle{};

    const std::string out = render_text(m);
    check(has(out, "Reasoned"),
          "settled reasoning renders a 'Reasoned' header");
    check(has(out, "token"),
          "the settled header names an approximate token count");
    check(has(out, "FINAL_ANSWER_MARKER"),
          "the answer still renders alongside the reasoning block");
    // 块 STAYS FULLY EXPANDED 和解后——无 归并 以一瞥见.
    check(has(out, "FIRST_LINE_MARKER"),
          "settled reasoning keeps its first line");
    check(has(out, "SECOND_LINE_MARKER"),
          "settled reasoning keeps ALL lines — it does not fold to a summary");
}

TEST_CASE("reasoning: the meter footer lines up with the body text") {
    // 脚印以前是满的 CHIP 乐队自己带着一个 -column 垫,
    // 所以 "✦ Reasoned" 开始一个列
    // RIGHT 相同的盒子,同样的垫子,两个不同的左边缘。
    // 将列平按:页脚是标签 正文 ,并有一个标签,
    // 该标签不共享 正文 '边值读作一条偏差缩进的线条.
    Model m;
    Message a = assistant("answer");
    a.thinking = "BODYMARKER reasoning line that the footer sits under.";
    m.d.current.messages.push_back(std::move(a));
    m.s.phase = phase::Idle{};

    const std::string out = render_text(m);

    auto indent_of = [&out](std::string_view needle) -> int {
        const std::size_t at = out.find(needle);
        if (at == std::string::npos) return -1;
        const std::size_t bol = out.rfind('\n', at) + 1; // ⁇   ⁇  ○  ⁇  开始
        int col = 0;
        for (std::size_t i = bol; i < out.size() && out[i] != '\n'; ++i) {
            if (out[i] != ' ') break;
            ++col;
        }
        return col;
    };

    const int body   = indent_of("BODYMARKER");
    const int footer = indent_of("Reasoned");
    check(body >= 0, "body line rendered");
    check(footer >= 0, "footer line rendered");
    check(body == footer,
          "footer indent must equal the body indent (no chip pad shift)");
}

// 扫描的计数器, 检查部件, 而不是通过 render_text( ):
// 扫描完全由每-character fg 颜色进行,
// 文本堆放处没有颜色。走建构的块进行测量,
// 并算出不同的墨水。
TEST_CASE("reasoning: the live meter carries a per-character sweep") {
    auto meter_runs = [](bool sweep) {
        maya::ReasoningStream::Config cfg;
        cfg.boxed        = true;
        cfg.meter_below  = true;
        cfg.meter_sweep  = sweep;
        cfg.meter_rate   = true;
        maya::ReasoningStream rs{cfg};
        rs.set_live(true);
        rs.set_char_hint(600);
        rs.set_elapsed_ms(6200);
        rs.set_content("some reasoning");

        // 块是一个边框,其行数为正文 / 空白/ 公尺.
        // 计数器就是那个 TextElement 保持现场直播
        maya::Element e = rs.build();
        std::vector<maya::StyledRun> found;
        std::function<void(const maya::Element&)> walk =
            [&](const maya::Element& el) {
                if (const auto* t = maya::as_text(el)) {
                    if (t->content.find("Thinking") != std::string::npos)
                        found = t->runs;
                    return;
                }
                if (const auto* b = maya::as_box(el))
                    for (const auto& c : b->children) walk(c);
                if (const auto* l = maya::as_list(el))
                    for (const auto& c : l->items) walk(c);
            };
        walk(e);
        return found;
    };

    const auto plain = meter_runs(false);
    const auto swept = meter_runs(true);
    check(!plain.empty(), "the meter row was found and is styled");
    check(swept.size() > plain.size(),
          "sweep explodes the meter into per-character runs");

    // 峰顶是梯度,不是两层 -tone 翻转。只有 # # # # 2 被
    // 固定在: RGB 主题混合是平滑的(许多墨水),
    // 但下 theme::native 停止没有渠道 lerp_color SNAPS 设计,
    // 所以乐队是两个 -tone 两者都正确,但数数不正确。
    std::vector<maya::Color> inks;
    for (const auto& r : swept) {
        if (!r.style.fg) continue;
        bool seen = false;
        for (const auto& c : inks) seen = seen || (c == *r.style.fg);
        if (!seen) inks.push_back(*r.style.fg);
    }
    check(inks.size() >= 2,
          "the band varies ink across the row, it is not one flat color");
}

TEST_CASE("reasoning: no block when the turn never reasoned") {
    Model m;
    m.d.current.messages.push_back(assistant("PLAIN_ANSWER no reasoning here."));
    m.s.phase = phase::Idle{};
    const std::string out = render_text(m);
    check(!has(out, "Reasoned"),
          "no reasoning block when the turn produced no reasoning");
    check(has(out, "PLAIN_ANSWER"), "the plain answer still renders");
}

// 外观的思考行是 一个 切换到推理区
//
// 用来设置 m.d.show_reasoning = 虚假和 调用 来,来,
// 来 "the global switch (the default)"。
// 该 标志 背后没有控制力 ^R 打开 diff 复议, 则未发送
// ModelsToggleShowReasoning ,所以价值被冻结在
// 任何 settings.json 测试通过,功能无法达到——它把一个用户无法
// 操作的门钉住,这就是一个 过期 `true` 最终向那些选择隐藏的人展示推理.
TEST_CASE("reasoning: the Appearance switch hides the block even when text exists") {
    Model m;
    // publish() 不只是,我们 Model 字段 :
    // render_text () 直接构建配置,永远不要 调用 view() ,
    // 通常会重新添加每个预告 帧。在结尾恢复,所以槽不会漏入同一
    // 测试中的另一个测试中 二进制程序.
    const auto saved = agentty::ui_prefs::current();
    auto prefs = saved;
    prefs.thinking = agentty::ui_prefs::Thinking::Hidden;
    agentty::ui_prefs::publish(prefs);
    m.d.persisted.ui.thinking = agentty::ui_prefs::Thinking::Hidden;
    Message a = assistant("Here is the ANSWER_STILL_SHOWN.");
    a.thinking = "lots of hidden reasoning the user chose not to see";
    m.d.current.messages.push_back(std::move(a));
    m.s.phase = phase::Idle{};
    const std::string out = render_text(m);
    agentty::ui_prefs::publish(saved);

    check(!has(out, "Reasoned"),
          "reasoning block is suppressed when Thinking is Hidden");
    check(!has(out, "hidden reasoning"),
          "reasoning text never reaches the screen when Thinking is Hidden");
    check(has(out, "ANSWER_STILL_SHOWN"),
          "the answer renders normally regardless of the reasoning switch");
}

// 推理 正文 必须 REVEAL 渐进式地像正常流体文本一样,不是同时出现。
// 我们逐渐增长。 msg.thinking 帧 由 帧 (作为 传输 并
// 申明可见的推理文本正在增长 GRADUALLY 而不是跳到满座上 帧
// 编号 字节 到了,这个是针头 流式传输-reveal 路径( 选项)
// turn.cpp MdView::Reasoning +
// subturn_stably_keyable 推理 -slot 动画检查).
TEST_CASE("reasoning: body reveals incrementally, not all at once") {
    // (本节 逐步显示 被绑定 ON 整个 二进制程序 输入 test_main.cpp
    // ——它的默认值是 终端-derived 不得漏入套房中.
    // ) 长而单的. -paragraph 正文 所以,我们 逐步显示 光标有
    // 许多字符需要通过( 字词) -boundary 标记,我们可以计算出来。
    std::string full;
    for (int i = 0; i < 60; ++i)
        full += "word" + std::to_string(i) + " ";

    Model m;
    m.s.phase = phase::Streaming{phase::Active{}};

    Message a;
    a.role = Role::Assistant;
    a.id   = MessageId{"stream1"};
    // 尚未回答文本:纯-reasoning阶段(用户击中的病例).
    m.d.current.messages.push_back(a);

    auto visible_words = [&](const std::string& out) {
        int n = 0;
        for (int i = 0; i < 60; ++i)
            if (has(out, "word" + std::to_string(i) + " ")) ++n;
        return n;
    };

    // 给它喂食 整个 推理 正文 立即(因为大三角洲即将到来),
    // 渲染 横跨许多动画 帧 如果 逐步显示 工作,
    // 可见的字数攀升 帧 结束 帧 而不是立刻打到60分 而不是
    // 马上打到60
    m.d.current.messages[0].thinking = full;

    int first_frame_words = -1;
    int max_words = 0;
    for (int frame = 0; frame < 40; ++frame) {
        const std::string out = render_text(m);
        const int w = visible_words(out);
        if (first_frame_words < 0) first_frame_words = w;
        max_words = std::max(max_words, w);
        maya::testing::advance_anim_clock_ms(33); // ~30英尺(千米)
    }

    // 电话 逐步显示 光标应当 不 整个都丢掉 正文 第一次见面 帧.
    check(first_frame_words < 60,
          "reasoning does not appear fully on the first frame (it reveals)");
    check(first_frame_words < max_words,
          "reasoning reveals MORE text over subsequent frames (it animates)");
    // 最终它揭示一切。
    check(max_words >= 55,
          "reasoning reveal eventually reaches the full body");
}

// 推理 逐步显示 必须 GLIDE 即使最后的推理三角洲
// 之后 答案立即开始(常见) Anthropic 实例 :
// 归纳推理 one/two 如果有的话,那么,
// 逐步显示 整个推理区 "appears at once"
// 我们检查#r槽的 逐步显示 直接光标。
TEST_CASE("reasoning: reveal glides, not cut short when the answer follows") {
    std::string full;
    for (int i = 0; i < 80; ++i) full += "tok" + std::to_string(i) + " ";

    Model m;
    m.s.phase = phase::Streaming{phase::Active{}};
    Message a; a.role = Role::Assistant; a.id = MessageId{"glide1"};
    m.d.current.messages.push_back(a);

    const auto rid = MessageId{std::string{"glide1"} + "#r"};
    auto reveal_clip = [&]() -> long long {
        const auto* mc = m.ui.view_cache.peek(m.d.current.id, rid);
        if (!mc || !mc->streaming) return -1;
        const auto clip = mc->streaming->debug_reveal_byte_clip();
        return clip == static_cast<std::size_t>(-1)
             ? -1 : static_cast<long long>(clip);
    };

    // 理性一时到达(一个大总结三角洲).
    m.d.current.messages[0].thinking = full;
    // 几个 帧 滑翔的飞行 逐步显示 光标应该是 MID-body 而不是
    // 在结尾。
    long long clip_after_a_few = -1;
    for (int f = 0; f < 3; ++f) {
        (void)render_text(m);
        maya::testing::advance_anim_clock_ms(33);
    }
    clip_after_a_few = reveal_clip();
    check(clip_after_a_few >= 0 &&
          clip_after_a_few < static_cast<long long>(full.size()),
          "reveal cursor is still mid-body a few frames in (it is gliding)");

    // 当前。 答案立即开始( settle 触发)。
    // 逐步显示 不得 到了最后,就解决了 帧 - 它应该继续滑翔
    m.d.current.messages[0].streaming_text = "Here is the answer.";
    (void)render_text(m);
    maya::testing::advance_anim_clock_ms(33);
    const long long clip_at_settle = reveal_clip();
    // 光标应该只是渐进推进,而不是跳跃到满。
    // (一响就会把它放在 full.size (即时和解)
    check(clip_at_settle < 0 ||
          clip_at_settle <= clip_after_a_few + static_cast<long long>(full.size()),
          "reveal is not force-snapped to the end when the answer begins");
}
