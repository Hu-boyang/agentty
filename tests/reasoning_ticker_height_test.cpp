// reasoning_ticker_height_test——现场推理块从未收缩.
//
// agentty 渲染 现场推理作为"thought ticker" 当外观
// QQ思潮(默认)崩溃时:ReasoningStream::Config:
// live_tail_rows =8,所以是一个长链 -of-thought 留一眼,
// 而不是把作曲家推倒。
//
// 窗口数到 ROWS 窗口线条窗口 -NODES 无法保持高度:
// 节点将一个可变数的行包成一个可变的行,因此每当一个较短的段落
// 成为最新的节点时,块就会被浸泡(宽度为50°8 ~ 13 ~
// 12 ~ 9). Every dipp is a short
// mid. -stream ,将作曲者置于用户光标下,
// 一旦行经 可视区域 顶部,重写 不可变 回滚缓冲区.
//
// 最多只能是跑一排,
// 最新线下永久空白行。行窗口不需要。 引擎铺设
// 正文 自然高度和高度的高度 可视区域 卷轴到底部,
// 所以高度是完全的min(内容,盖):它会长到盖子,
// 然后抱住,每行都携带真实的文字.
//
// 这针头是什么? 单调递增的 高度跨过 a 字节-by-byte
// 数据流 盖子本身,没有死排, 和定居的块释放窗口。

#include "agtest.hpp"

#include <maya/core/anim_clock.hpp>
#include <maya/print.hpp>
#include <maya/widget/reasoning.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace {

constexpr int kWidth = 50;
constexpr int kCap   = 8;     // agentty '已折叠的默认值( E) turn.cpp)

int rows_of(const std::string& s) {
    int n = 1;
    for (const char c : s) if (c == '\n') ++n;
    return n;
}

maya::ReasoningStream::Config ticker_cfg() {
    maya::ReasoningStream::Config cfg;
    cfg.live_tail_rows = kCap;
    return cfg;
}

// 渲染 一个活的滴答器结束 `src` 与 逐步显示 被允许追上,
// 并返回 帧.
std::string ticker_frame(const std::string& src) {
    maya::ReasoningStream rs{ticker_cfg()};
    rs.set_live(true);
    rs.set_content(src);
    // 电话 正文 是一个 逐步显示 部件: 让打字机到达边缘,
    // 以便测量已定值 -shape 内容, 不含半 -typed 帧.
    std::string out;
    for (int f = 0; f < 400; ++f) {
        out = maya::render_to_string(rs.build(), kWidth);
        maya::testing::advance_anim_clock_ms(33);
    }
    return out;
}

int ticker_rows(const std::string& src) { return rows_of(ticker_frame(src)); }

// 一系列思维,其形状使高度四处移动: 长段包络,短段之间,
// 列表,短段最后行(常见最坏的事例——
// 最后一段使整个块变短。
const std::vector<std::string>& paragraphs() {
    static const std::vector<std::string> p = {
        "This is a long first paragraph that will certainly wrap across "
        "several terminal rows because it keeps going and going with plenty "
        "of words in it.",
        "Here is a second long paragraph, also comfortably long enough to "
        "wrap onto multiple rows when rendered at a narrow width like this.",
        "Short one.",
        "A fourth long paragraph that likewise wraps over several rows, so "
        "the window is full of tall nodes while these are the newest.",
        "- a list item\n- another list item",
        "A sixth paragraph of roughly the same size as the others, which is "
        "what a real chain of thought looks like after a while of thinking.",
        "Short final.",
    };
    return p;
}

}  // 命名空间结束。

TEST_CASE("reasoning ticker: height never shrinks as paragraphs arrive") {
    maya::testing::freeze_anim_clock(0);

    std::string src;
    std::vector<int> heights;
    for (const auto& p : paragraphs()) {
        src += p;
        src += "\n\n";
        heights.push_back(ticker_rows(src));
    }

    maya::testing::unfreeze_anim_clock();

    std::string trace;
    for (std::size_t i = 0; i < heights.size(); ++i)
        trace += " p" + std::to_string(i + 1) + "=" + std::to_string(heights[i]);
    INFO("ticker heights:", trace);

    for (std::size_t i = 1; i < heights.size(); ++i)
        CHECK(heights[i] >= heights[i - 1]);

    // 就这么定了 ON 而不是在它上面漂浮。
    // 铬(Chrome) 头文件 + 街区的自筑) 骑着,
    // 所以允许它,但 正文 本身一定是窗口。
    CHECK(heights.back() <= kCap + 4);
}

TEST_CASE("reasoning ticker: height never shrinks byte by byte") {
    // 更严格的版本是同 不变量: 每个中间前缀,
    // 而不仅仅是段落边界。 这是提供者实际
    // 提供的形状。
    maya::testing::freeze_anim_clock(0);

    std::string all;
    for (const auto& p : paragraphs()) { all += p; all += "\n\n"; }

    int prev = 0;
    int shrinks = 0;
    int worst_at = 0;
    maya::ReasoningStream rs{ticker_cfg()};
    rs.set_live(true);
    for (std::size_t n = 1; n <= all.size(); n += 7) {
        const std::string src = all.substr(0, n);
        rs.set_content(src);
        const int rows = rows_of(maya::render_to_string(rs.build(), kWidth));
        maya::testing::advance_anim_clock_ms(33);
        if (rows < prev) { ++shrinks; if (!worst_at) worst_at = static_cast<int>(n); }
        prev = rows;
    }

    maya::testing::unfreeze_anim_clock();

    INFO("shrinks:", shrinks, " first at byte ", worst_at, " of ", all.size());
    CHECK(shrinks == 0);
}

TEST_CASE("reasoning ticker: the window spends its rows on text") {
    maya::testing::freeze_anim_clock(0);

    std::string src;
    for (const auto& p : paragraphs()) { src += p; src += "\n\n"; }
    const std::string frame = ticker_frame(src);

    maya::testing::unfreeze_anim_clock();

    std::vector<std::string> lines;
    std::string cur;
    for (const char c : frame) {
        if (c == '\n') { lines.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    if (!cur.empty()) lines.push_back(cur);

    const auto blank = [](const std::string& l) {
        return l.find_first_not_of(" \t\u2502\u2503") == std::string::npos;
    };

    // 窗口在行颗粒度时被剪切,所以它永远保留不能
    // 填充的行:任何地方都没有堆叠的空白行(这就是
    // 编程修正的故障模式),最多在最后一行文字
    // 下方的铬本身的几行.
    int run = 0, worst_run = 0, trailing = 0;
    for (const auto& l : lines) {
        if (blank(l)) { ++run; ++trailing; worst_run = std::max(worst_run, run); }
        else { run = 0; trailing = 0; }
    }
    INFO("frame:\n", frame);
    CHECK(worst_run <= 2);
    CHECK(trailing <= 2);

    // 最新文本是窗口中幸存下来的文字——滴答器停在底部,
    // 其中 逐步显示 光标 就是这样
    CHECK(frame.find("Short final.") != std::string::npos);
    // 并且最古老的文本已经卷了出来
    CHECK(frame.find("This is a long first paragraph") == std::string::npos);
}

TEST_CASE("reasoning ticker: one long paragraph is cut mid-wrap") {
    // 切开得落地 内部 节点, 不仅仅是节点之间:
    // 单段长于窗口是节点 -counted 窗口中
    // 无法打开窗口(一个节点=全部或全无)。
    maya::testing::freeze_anim_clock(0);

    const std::string one =
        "The first thing to establish is what the window actually promises, "
        "because the promise is what makes it safe to put above a composer. "
        "It promises a height: once the body is taller than the cap, the "
        "block is exactly the cap and stays there, no matter which words "
        "happen to be newest or how they wrap. The second thing is what it "
        "shows, which is simply the newest rows, so the reveal caret is "
        "always on screen at the bottom edge where the text is arriving.\n\n";

    const std::string frame = ticker_frame(one);
    maya::testing::unfreeze_anim_clock();

    INFO("frame:\n", frame);
    // 电话 末尾 高度是盖子加块的染色体
    // 高点是盖子和块的染料
    CHECK(frame.find("arriving") != std::string::npos);
    CHECK(frame.find("The first thing to establish") == std::string::npos);
    CHECK(rows_of(frame) <= kCap + 4);
    CHECK(rows_of(frame) >= kCap);
}

TEST_CASE("reasoning ticker: a delta ending on a paragraph break wastes no rows") {
    // 推理的到来 每一场爆炸都以 "\n\n" .
    // 用于吃两排窗子的最新线下最短的缺口——收割最
    // 老的一排,以支付一排,其中一无所有——以及在暂停期间。
    // 数据流 答案道从未有过这个
    // StreamingMarkdown 丢弃其后遗
    // 症的空穴; 推理窗口必须与之匹配。
    maya::testing::freeze_anim_clock(0);

    std::string src;
    for (const auto& p : paragraphs()) { src += p; src += "\n\n"; }

    const std::string mid   = ticker_frame(src + "and then the wire stopped");
    const std::string brk   = ticker_frame(src + "and then the wire stopped.\n\n");

    maya::testing::unfreeze_anim_clock();

    const auto trailing_blanks = [](const std::string& frame) {
        std::vector<std::string> lines;
        std::string cur;
        for (const char c : frame) {
            if (c == '\n') { lines.push_back(cur); cur.clear(); }
            else cur.push_back(c);
        }
        if (!cur.empty()) lines.push_back(cur);
        while (!lines.empty() && lines.back().empty()) lines.pop_back();
        int n = 0;
        for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
            if (it->find_first_not_of(" \t\u2502\u2503") == std::string::npos) ++n;
            else break;
        }
        return n;
    };

    INFO("mid-sentence frame:\n", mid, "\nparagraph-break frame:\n", brk);
    CHECK(trailing_blanks(mid) == 0);
    CHECK(trailing_blanks(brk) == 0);
    // 无论是身高还是身高, 休息都无所费。
    CHECK(rows_of(brk) == rows_of(mid));
    // ...最新的线路仍然是街区里最后的一条
    CHECK(brk.find("the wire stopped.") != std::string::npos);
}

TEST_CASE("reasoning: with no window at all, the block only grows") {
    // Thinking::Shown - 整块,
    // 没有盖子 单调递增的 唯一的问题是 下方的标记是否
    // -flows 中点短 -stream 绝对不能
    maya::testing::freeze_anim_clock(0);

    std::string all;
    for (const auto& p : paragraphs()) { all += p; all += "\n\n"; }

    maya::ReasoningStream::Config cfg;   // live_tail_rows 保留 0
    maya::ReasoningStream rs{cfg};
    rs.set_live(true);

    int prev = 0, shrinks = 0, worst_at = 0;
    for (std::size_t n = 1; n <= all.size(); n += 7) {
        rs.set_content(all.substr(0, n));
        const int rows = rows_of(maya::render_to_string(rs.build(), kWidth));
        maya::testing::advance_anim_clock_ms(33);
        if (rows < prev) { ++shrinks; if (!worst_at) worst_at = static_cast<int>(n); }
        prev = rows;
    }

    maya::testing::unfreeze_anim_clock();

    INFO("shrinks:", shrinks, " first at byte ", worst_at, " of ", all.size());
    CHECK(shrinks == 0);
    // ...它真的是整个街区,不是窗户。
    CHECK(prev > kCap * 2);
}

TEST_CASE("reasoning: the window is the host's call, not the live flag") {
    // 盖子以前是盖着的 set_live (所以一个块的安放 释放了窗口,并铺开整个链条)
    // -of-thought 将两个无关的问题连在一起:
    // "is the model thinking right now" (无) 头文件 和。
    // 和 简 况 "how much reasoning does the reader want" (窗户).
    // agentty 以预想来回答第二个问题,所以折叠后保留
    // 末尾 定居后,Shown没有通过任何封顶。
    maya::testing::freeze_anim_clock(0);

    std::string src;
    for (const auto& p : paragraphs()) { src += p; src += "\n\n"; }

    const int live_rows = ticker_rows(src);

    // 同样的盖子,固定: 仍然一眼,同样的高度。
    maya::ReasoningStream rs{ticker_cfg()};
    rs.set_content(src);
    rs.set_live(false);
    rs.finish();
    const std::string settled = maya::render_to_string(rs.build(), kWidth);

    // 无盖(Thinking::Shown):全称.
    maya::ReasoningStream::Config full_cfg;   // live_tail_rows 保留 0
    maya::ReasoningStream full{full_cfg};
    full.set_content(src);
    full.set_live(false);
    full.finish();
    const std::string all = maya::render_to_string(full.build(), kWidth);

    maya::testing::unfreeze_anim_clock();

    INFO("live:", live_rows, " settled:", rows_of(settled),
         " uncapped:", rows_of(all));
    CHECK(rows_of(settled) == live_rows);
    CHECK(settled.find("This is a long first paragraph") == std::string::npos);
    CHECK(rows_of(all) > live_rows);
    CHECK(all.find("This is a long first paragraph") != std::string::npos);
}
