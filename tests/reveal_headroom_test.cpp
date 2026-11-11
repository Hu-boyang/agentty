// reveal_headroom_test —— 注意授权进行下列操作的几何绑定: INSTANT
// 文本工具的工具卡 接口边界 (turn.cpp, cached_markdown_for).
//
// 背景: 表达流言时显示工具卡, 只有在未解决的情况下才安全。
// 末尾 无法证明是经过 可视区域 上到顶 不可变 回滚缓冲区.
// turn.cpp 证明这一点
//
//     est_hidden_rows + est_tail_rows + 铬+ 松动 < viewport_rows
//
// 地点 est_tail_rows 将非高处的渲染高度限制在
// -yet-revealed 字节。约束只是 PROOF 如果
// 它从来没有 UNDER-estimates : 低 -estimate
// 授权飞行中行数超过 可视区域 鬼魂在地上 闪烁着甘油 回滚缓冲区
// 无 重绘 永远可以修复它们。
//
// 上一版本使用了 ceil( Q) 字节 / cols),
// 默默地假定每 字节 新建线断开这个假设——在这里测量:40
// 个短列表项目占41行,但公式预测2, a 20x under
// -estimate ,以及通常在工具前的标记下形状(列表、
// 表格) 调用。该词 回滚缓冲区 先知已经做了 不 抓住它,
// 因为它的传言是长的 包在段落中 bytes/cols
// 恰巧是对的
//
// 这个测试针钉 不变量 直接: 对于一组下标形状, 宽度是几度,
// 估计值必须是 QQ 高度 maya 其实 渲染
// 给同一机构的 字节.

#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include <maya/core/anim_clock.hpp>
#include <maya/core/render_context.hpp>
#include <maya/render/canvas.hpp>
#include <maya/render/renderer.hpp>
#include <maya/style/theme.hpp>
#include <maya/widget/markdown.hpp>

using namespace maya;

namespace {  // 归并: TU-local (捆绑成 agentty_standalone_tests)

// EXACT 估计器副本 turn.cpp 头室证明。
// 与此测试的存在同步 : 如果 turn.cpp 公式变化
// 而不改变这个, 下面的断言开始失败 相对于真实的高度。
int est_tail_rows(std::string_view tail, int cols) {
    if (cols < 1) cols = 1;
    constexpr int kBlockInset = 2;
    const int wrap_w = std::max(1, cols - kBlockInset);
    int rows = 0;
    std::size_t line_start = 0;
    while (line_start <= tail.size()) {
        const std::size_t nl = tail.find('\n', line_start);
        const std::size_t len =
            (nl == std::string_view::npos ? tail.size() : nl) - line_start;
        rows += std::max<int>(
            1, static_cast<int>((len + static_cast<std::size_t>(wrap_w) - 1)
                                / static_cast<std::size_t>(wrap_w)));
        if (nl == std::string_view::npos) break;
        line_start = nl + 1;
    }
    return rows;
}

// EXACT复制了turn.cpp的kBlockChromeSlack.
//     生产证明是est_tail_rows
// +lax + + = 实际的,因此这个测试必须坚持同样的
// 总和——坚持光量估计器,而生产加减则测试一个公式生产不使用.
// 懒惰的吸收器 -level 标注,将行加到它自己的文字行之外。
// 在下面的元件上测量(5宽×17形状):最差的缺2行—
// — table_real ,在其中 maya 画一条
// 上下无源线的边框行——所以松动=4 持持,有2行边框.
// 如果出现 maya 标记下降的铬生长( 宽度更大的栅栏边框,
// 额外的桌子分隔器), 阳具在这里失效。 之前 编号 逐步显示
// 接口边界 能够授权将幽灵行串入的即时卡片 回滚缓冲区.
constexpr int kBlockChromeSlack = 4;

struct Shape { const char* name; std::string body; };

std::vector<Shape> corpus() {
    std::vector<Shape> s;
    auto rep = [](const char* unit, int n) {
        std::string out;
        for (int i = 0; i < n; ++i) out += unit;
        return out;
    };
    // 打破旧的bytes/cols束缚的形状.
    s.push_back({"short_list_40",  rep("- x\n", 40)});
    s.push_back({"table_20",       rep("| a | b |\n", 20)});
    s.push_back({"blank_lines_30", std::string(30, '\n')});
    s.push_back({"tight_numbers",  rep("1. i\n", 25)});
    // 摆出旧的绳子,保持这样 "fix" 无法退缩
    s.push_back({"one_long_line",  std::string(500, 'x')});
    s.push_back({"wrapping_paras", rep("This is a long paragraph that wraps "
                                       "across several terminal rows just "
                                       "like real assistant prose does.\n\n", 6)});
    s.push_back({"mixed",          "# Title\n\nSome prose here.\n\n"
                                   "- a\n- b\n- c\n\n> quote\n\n"
                                   "final paragraph that runs on for a while "
                                   "so it wraps at narrow widths too.\n"});
    // 块 -level 标记 CHROME 在其文字行之外添加行——
    // 形状为 kBlockChromeSlack 存在可以吸收。
    // 这些字条在之前没有出现在体中, 因此懒惰值 (4) 没有任何断言 :
    // a maya 标记下的变化,如果在四行之后 增加栅栏或
    // 桌子的铬 将会静静地打破 头室证明,而这个测试 保持绿色的传教。
    // -only 形状。
    s.push_back({"one_fence",      "```cpp\nint x = 1;\nint y = 2;\n```\n"});
    s.push_back({"two_fences",     "```cpp\na();\n```\ntext\n"
                                   "```py\nb()\n```\n"});
    s.push_back({"four_fences",    rep("```\ncode();\n```\n\n", 4)});
    s.push_back({"fence_empty",    "```\n```\n"});
    s.push_back({"quote_block",    "> quoted line one\n> quoted line two\n"
                                   "> three\n"});
    s.push_back({"table_real",     "| a | b |\n|---|---|\n| 1 | 2 |\n"
                                   "| 3 | 4 |\n"});
    s.push_back({"hrule",          "before\n\n---\n\nafter\n"});
    s.push_back({"heading_mix",    "# H1\ntext\n## H2\ntext\n### H3\ntext\n"});
    s.push_back({"fence_in_list",  "- item\n  ```\n  code\n  ```\n- item2\n"});
    s.push_back({"deep_quote",     "> outer\n> > inner\n> > > deepest\n"});
    return s;
}

int failures = 0;

int rows_of(const Canvas& c) { return c.max_content_row() + 1; }

void check_shape(const Shape& sh, int cols) {
    // 渲染 编号 相同 字节 穿过真正的部件,测量它的高度。
    StreamingMarkdown md;
    md.set_content(sh.body);
    md.set_live(false);             // 固定高度=已完全渲染的行

    StylePool pool;
    std::vector<layout::LayoutNode> nodes;
    RenderContext ctx{cols, 4000, render_generation(), true};
    RenderContextGuard guard(ctx);
    Canvas c(cols, 4000, &pool);
    c.clear();
    render_tree(md.build(), c, pool, theme::native, nodes, true);
    const int actual = rows_of(c);

    const int estimate = est_tail_rows(sh.body, cols);

    // 显示生产不平等:估计+松懈必须涵盖实际生产。 渲染。
    // 预计会出现赤字。 -chrome 形状;
    // 赤字 BEYOND 这正是鬼魂 -rows-in-scrollback
    // 授权错误。
    if (estimate + kBlockChromeSlack < actual) {
        std::fprintf(stderr,
            "FAIL: %-16s cols=%3d  estimate=%3d + slack=%d < actual=%3d  "
            "(bound UNDER-estimates → instant card could push %d ghosted "
            "row(s) into scrollback)\n",
            sh.name, cols, estimate, kBlockChromeSlack, actual,
            actual - estimate - kBlockChromeSlack);
        ++failures;
    } else {
        std::fprintf(stderr,
            "ok  : %-16s cols=%3d  estimate=%3d+%d >= actual=%3d\n",
            sh.name, cols, estimate, kBlockChromeSlack, actual);
    }
}

}  // 命名空间结束。

namespace {

// ── advance_reveal_floor 语义 ──────────────────────────────────────
// 原始的背后的人类 INSTANT-CARD 路径: 解析前缀,
// 保留 末尾 动画 守护三个属性 turn.cpp 依赖。
void check_partial_resolve() {
    const std::string body =
        "Line one of the reply.\nLine two of the reply.\n"
        "Line three of the reply.\nLine four of the reply.\n"
        "Line five of the reply.\nLine six of the reply.\n";

    StylePool pool;
    std::vector<layout::LayoutNode> nodes;
    RenderContext ctx{80, 4000, render_generation(), true};
    RenderContextGuard guard(ctx);

    StreamingMarkdown md;
    md.set_reveal_fx(true);
    md.set_live(true);
    md.set_content(body);
    // 慢慢地划,光标就明显是中点 -tail 而不是在边缘。
    md.set_reveal_pacing(/*floor_cps=*/40.0, /*drain_secs=*/0.8);

    auto paint = [&] {
        Canvas c(80, 4000, &pool);
        c.clear();
        render_tree(md.build(), c, pool, theme::native, nodes, true);
    };
    paint();
    maya::testing::advance_anim_clock_ms(16);
    paint();

    const double before = md.debug_reveal_cp();

    // 1. 申请的楼层预付款。
    const std::size_t floor_cp = 60;
    md.advance_reveal_floor(floor_cp);
    paint();
    const double after = md.debug_reveal_cp();
    if (after < static_cast<double>(floor_cp)) {
        std::fprintf(stderr,
            "FAIL: advance_reveal_floor(%zu) left cursor at %.1f (was %.1f)\n",
            floor_cp, after, before);
        ++failures;
    } else {
        std::fprintf(stderr, "ok  : floor advances cursor %.1f -> %.1f\n",
                     before, after);
    }

    // 2. PARTIAL——第末尾号经过该层的仍须剪接,第i.e号。
    //    部件已完成 不 整个贴上 正文。
    //    这是它与 snap_reveal_to_edge.
    const std::size_t total_cp = md.debug_source_size();
    if (md.debug_reveal_cp() >= static_cast<double>(total_cp)) {
        std::fprintf(stderr,
            "FAIL: cursor reached the edge (%.1f of %zu) — resolve was TOTAL, "
            "not partial; the tail would stop animating\n",
            md.debug_reveal_cp(), total_cp);
        ++failures;
    } else {
        std::fprintf(stderr, "ok  : partial — cursor %.1f < total %zu\n",
                     md.debug_reveal_cp(), total_cp);
    }

    // 3. MONOTONE ——下层不得倒挂(倒挂). -ghost 列数
    //    可能已经滚动到 不可变 回滚缓冲区).
    const double pinned = md.debug_reveal_cp();
    md.advance_reveal_floor(1);
    paint();
    if (md.debug_reveal_cp() < pinned) {
        std::fprintf(stderr,
            "FAIL: a lower floor REWOUND the cursor %.1f -> %.1f\n",
            pinned, md.debug_reveal_cp());
        ++failures;
    } else {
        std::fprintf(stderr, "ok  : monotone — lower floor did not rewind\n");
    }

    // 4. 仍然 ANIMATING——逐步显示必须继续推进后期的帧.
    //    如果出现 advance_reveal_floor 将微秒时钟印上,
    //    它会把每秒都零掉, 并在这里冻结, 这正是这个路径所消除的精确的停顿。
    const double t0 = md.debug_reveal_cp();
    for (int i = 0; i < 8; ++i) {
        maya::testing::advance_anim_clock_ms(16);
        paint();
    }
    const double t1 = md.debug_reveal_cp();
    if (!(t1 > t0)) {
        std::fprintf(stderr,
            "FAIL: reveal FROZE after advance_reveal_floor (%.1f -> %.1f over "
            "8 frames)\n", t0, t1);
        ++failures;
    } else {
        std::fprintf(stderr,
            "ok  : still animating — cursor %.1f -> %.1f over 8 frames\n", t0, t1);
    }
}

}  // 命名空间结束。

int main() {
    // 时间只通过advance_anim_clock_ms,从不通过墙钟.
    maya::testing::freeze_anim_clock(1'000'000);
    // 宽度横跨神谕的形状,包括一个窄的,
    // 包扎占主导,宽的,线条 COUNT 占优势
    const int widths[] = {46, 60, 80, 100};
    for (const auto& sh : corpus())
        for (int w : widths)
            check_shape(sh, w);

    std::fprintf(stderr, "\n-- advance_reveal_floor --\n");
    check_partial_resolve();

    if (failures) {
        std::fprintf(stderr,
            "\n%d bound violation(s): est_tail_rows must be an UPPER bound on "
            "rendered rows or turn.cpp's headroom proof is unsound.\n", failures);
        return 1;
    }
    std::fprintf(stderr, "\nPASS: est_tail_rows is an upper bound on all shapes/widths\n");
    return 0;
}
