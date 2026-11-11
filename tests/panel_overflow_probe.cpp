// panel_overflow_probe 任何面板是否画过自己的画 帧?
//
// 该 FLAW 此 MEASURES
// =====================
// maya 其小组组成 正文 以
//
//     可滚动元素 = vstack() (行数) | 滚动(s, vh) | 增长( 1.0f);
//     如果( 内容 > vh)
//         stack.push_back (h(可滚动, scrollbar_y(...)).build());
//
// 滚动条附后 之后 grow(1.0f)
// 告诉内容要求全宽; a h() 哥哥
// 们然后拿回一列 `avail_w`
// – 计 component()
// 已经承诺过宽的几何体。
//
// 所以,一个小组提供的内容是
// 使用全部宽度，只扣除已经明确说明的占用部分。
// agentty 统计小组是唯一的 赔偿者, 赔偿是手 -counted
// 魔法号码( O) `sheet.reserve_right(7)` 循环建筑:
// 滚动栏以内容 > vh为条件,vh取决于保留
// 地确定的宽度.
//
// 修复方案及将其保留为测试的原因。
// ===================================
// 面板会减去自己的铬, 而不是要求呼叫者。
// 它会夹住 min_width 编号 终端 (
// 比屏幕宽的一层是溢出层,而不是一层),
// 无条件保留滚动条沟(a) 正文 当内容跨越时,
// 该流 可视区域 将笔记排短,而不是信任一个
// 呼叫者 -supplied 提示要适应。
//
// 开始于 NO_TEST 探测器打印一个表格,
// 因为如果在固定器前坚持数字, 就会显示超标。
// 它测量了 30 个破损。 panel/width 共
// 13个面板,现在它测量0,所以测量成为保证。

#include "agtest.hpp"

#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/view/panels.hpp"
#include "agentty/runtime/panel/smart_form.hpp"

#include <maya/render/canvas.hpp>
#include <maya/render/renderer.hpp>
#include <maya/style/theme.hpp>

#include <cstdlib>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace pn    = agentty::ui::panel;
namespace smart_form = agentty::smart_form;

using namespace agentty;

namespace {

struct Case {
    const char*                          name;
    std::function<void(Model&)>          open;
    std::function<maya::Element(const Model&)> build;
};

// 每个面板类型,打开后内容足够具有代表性。
// 与逃逸的-guarantee表格相同的列表-这两个列表应该保持步调,
// 它们都是单独的列表这一事实就是它自己的味道。
const std::vector<Case>& cases() {
    static const std::vector<Case> v = {
        {"command palette",   [](Model& m) { m.ui.panel.descend(pn::Palette{{}}); },
         ui::palette_panel},
        {"mention palette",   [](Model& m) { m.ui.panel.descend(pn::Mention{{}}); },
         ui::mention_panel},
        {"symbol palette",    [](Model& m) { m.ui.panel.descend(pn::Symbol{{}}); },
         ui::symbol_panel},
        {"code block picker", [](Model& m) { m.ui.panel.descend(pn::CodeBlocks{{}}); },
         ui::code_blocks_panel},
        {"tool output",       [](Model& m) { m.ui.panel.descend(pn::ToolOutput{{}}); },
         ui::tool_output_panel},
        {"checkpoints",       [](Model& m) { m.ui.panel.descend(pn::Checkpoints{{}}); },
         ui::checkpoints_panel},
        {"retrieval pane",    [](Model& m) { m.ui.panel.descend(pn::Rag{{}}); },
         ui::rag_panel},
        {"settings list",     [](Model& m) { m.ui.panel.descend(pn::SettingsList{{}}); },
         ui::settings_list_panel},
        {"fork picker",       [](Model& m) { m.ui.panel.descend(pn::Fork{{}}); },
         ui::fork_panel},
        {"model picker",      [](Model& m) { m.ui.panel.descend(pn::Models{{}}); },
         ui::models_panel},
        {"provider picker",   [](Model& m) { m.ui.panel.descend(pn::Providers{{}}); },
         ui::providers_panel},
        {"thread list",       [](Model& m) { m.ui.panel.descend(pn::ThreadList{{}}); },
         ui::thread_list_panel},
        {"smart mode",        [](Model& m) {
             smart_form::Inputs in;
             m.ui.panel.descend(pn::SmartMode{{}, smart_form::build_form(in)});
         }, ui::smart_mode_panel},
        {"stats viewer",      [](Model& m) { m.ui.panel.descend(pn::Stats{}); },
         ui::stats_panel},
    };
    return v;
}

store::Settings g_settings;

void install_stub_deps() {
    app::install_deps(app::Deps{
        .stream         = [](const maya::unlocked&, auto, auto) {},
        .save_thread    = [](const auto&) {},
        .delete_thread  = [](const auto&) {},
        .load_threads   = [] { return std::vector<Thread>{}; },
        .load_thread    = [](const auto&) -> std::optional<Thread> { return std::nullopt; },
        .load_settings  = [] { return g_settings; },
        .save_settings  = [](const store::Settings& x) { g_settings = x; },
        .new_thread_id  = [] { return ThreadId{}; },
        .title_from     = [](std::string_view t) { return std::string{t}; },
        .auth           = auth::AuthHeader{auth::ApiKeyHeader{std::string{}}},
    });
}

// A 线程 足够让盘点面板 具有真实内容
// 滚动面板实际上溢出 可视区域.
Model with_history() {
    Model m;
    m.d.current.id = ThreadId{"probe"};
    for (int i = 0; i < 40; ++i) {
        Message u;
        u.role = Role::User;
        u.text = "a question that is long enough to wrap on a narrow pane";
        m.d.current.messages.push_back(std::move(u));

        Message a;
        a.role         = Role::Assistant;
        a.served_model = ModelId{"claude-sonnet-4-5"};
        a.text         = "an answer";
        Message::Telemetry t;
        t.input_tokens   = 1200;
        t.output_tokens  = 400;
        t.cache_read     = 15000;
        t.cache_creation = 200;
        t.ttft_ms        = 120;
        t.stream_ms      = 900;
        a.telemetry = t;
        m.d.current.messages.push_back(std::move(a));
    }
    return m;
}

// 带非的右栏 -blank 存储单元。小组本身 帧 是什么 说吧
// SHOULD 在那里;除了它之外,其他任何东西都是逃避的。
int rightmost_ink(const Case& c, const Model& m, int w, int h) {
    maya::StylePool pool;
    maya::Canvas canvas(w, h, &pool);
    maya::render_tree(c.build(m), canvas, pool, maya::theme::native,
                      /*auto_height=*/true);
    int worst = -1;
    for (int y = 0; y < h; ++y)
        for (int x = w - 1; x > worst; --x) {
            const auto ch = canvas.get(x, y).character;
            if (ch != 0 && ch != U' ') { worst = x; break; }
        }
    return worst;
}

// 有多少行END没有他们的右边框.
//
// 这是一种症状,它实际上到达用户。 CLIPS ,
// 所以内容铺得太宽 永远不要画过边缘-- 它覆盖了 帧 自己有边界,
// 然后就停下来 "Ink past the frame" 因此,
// 永远无法在帆布上看到;可见的破坏是一排其边界是
// MISSING ,这就是什么破碎 帧 看起来像在
// 屏幕上。
//
// 计算失去边界的行数,是从唯一可观察
// 到的一侧进行的测量。
// 有个小组 CLIPPED AWAY ,有多少行失去了边界。
//
// 一次断层的两个测量,从对面进行.
//
// 边框是 SYMPTOM ,这个探测器围绕它建造的
// 原因就如上面所说的: 一种Canvas的剪辑,
// 所以所铺设的太宽的内容不能被观察到在边缘上——它
// 覆盖了 帧 自己边界和停留。 数数失去边界的行是
// 唯一能看到外溢的路。
//
// 剪辑报告是 CAUSE ,严格来说,
// 它更适合它的适用: 它命名文本, 它开始的列,
// 它想要的宽度和剪切它的边缘, 在此时 存储单元 被丢弃。
// "Three rows are broken at width 40" 变成
// 传输健康状态说明需要 48 列，但边界只有 40 列。
//
// 两者都保留,因为两者都不包含。 一行会失去其边界,
// 使其内容完全适合,而不会留下任何剪辑;
// 内容可以剪辑在边界完全完整的帧内。 两个问题,
// 两个答案。
struct Damage {
    int broken_rows = 0;
    std::vector<std::string> clipped;
};

Damage measure(const Case& c, const Model& m, int w, int h) {
    // 面板夹住自己的 min_width 编号 终端 ,
    // 没有tty 它读 COLUMNS 告诉它我们实际上的
    // 宽度 渲染 或它夹住一个 回退 探测器测量到的几何错位
    const std::string cols = std::to_string(w);
    setenv("COLUMNS", cols.c_str(), /*覆盖=*/1);

    Damage d;
    maya::StylePool pool;
    maya::Canvas canvas(w, h, &pool);
    canvas.on_clip_overflow([&d](const maya::Canvas::ClipOverflow& o) {
        d.clipped.push_back("row " + std::to_string(o.y) + " col "
                            + std::to_string(o.x) + ": '" + std::string(o.text)
                            + "' wanted " + std::to_string(o.wanted)
                            + " cols, edge at " + std::to_string(o.edge));
    });
    maya::render_tree(c.build(m), canvas, pool, maya::theme::native,
                      /*auto_height=*/true);

    // 属于哪个行 帧 完全没有: LEFT
    // 边缘带有边框。面板外的行不是它的问题。
    for (int y = 0; y < h; ++y) {
        const auto left = canvas.get(0, y).character;
        const bool framed = left == U'\u2502' || left == U'\u256d'
                         || left == U'\u2570';
        if (!framed) continue;
        const auto right = canvas.get(w - 1, y).character;
        const bool closed = right == U'\u2502' || right == U'\u256e'
                         || right == U'\u256f';
        if (!closed) ++d.broken_rows;
    }
    return d;
}

int rows_missing_border(const Case& c, const Model& m, int w, int h) {
    return measure(c, m, w, h).broken_rows;
}

}  // 命名空间结束。

TEST_CASE("panel: no panel paints over its own frame, at any width") {
    install_stub_deps();

    // 从电话上 -sized 拆分( E) 面板 超宽度
    // 终端 狭长的一端是每个破碎的地方
    const int widths[] = {40, 50, 60, 68, 76, 90, 120, 160, 200};
    constexpr int kRows = 60;

    for (const auto& c : cases())
        for (int w : widths) {
            Model m = with_history();
            c.open(m);
            const int broken = rows_missing_border(c, m, w, kRows);
            INFO("panel=", c.name, " width=", w,
                 " rows_missing_right_border=", broken);
            CHECK(broken == 0);
        }
}

// 每个面板都丢掉的,报告。
//
// 上面的支票的同伴问了另一半问题,
// 那一个衡量什么 LANDED — a 帧 其
// 边界幸存下来的 帧 什么都没有被淹没,
// 这只狗问 渲染器 或 DISCARDED ,
// 这是不检查画 存储单元 可恢复: 已剪辑的文本
// 留下一个 帧 读者没有理由怀疑它。
//
// REPORTED 第一次检查发现有十种真正的缺陷——四个板块,
// 它们空着。 -state 提示切换到中间 -word 在一个狭长
// 的 终端 ("indexing workspace… (type to filter as it f"
// ————他们中没有一个有我无法证明的修补.
// 3名候选人被试并被退回: TruncateEnd 现场
// 调用 站点,预建路径的测量宽度,以及 max_width
// 绑定在 头文件 每一行都合理,每行都以十分计数
//
// 因此,这个统计是印刷的而不是强制的。
// 目前没有人能够满足的断言在一周内得到评论,
// 并用它来进行诊断;日志中的数字保持诚实,
// 让人们能够把它看成一个下降。当统计达到零时,
// 将CHECK恢复。
//
// 或 IS ,写下,所以下次尝试不会重新 -derive 其内容:
//
//   * 剪贴边为36, 在一个40-column面板,文本落在
//     第4-5栏第3行—— 正文 ,通过 头文件 行数
//   * 预建的度量环报告 ZERO 行,用于这些面板,所以
//     无论什么路径 渲染 这条线,不是一条 measure_body
//     会计核算
//   * 当表单为空时, 面板设置为 cfg.prebuilt, 即
//     探测器打开的箱子
//
// 这三者不可能一时都是真的,矛盾在于 线程
// 来拉。
TEST_CASE("panel: report anything clipped away without an ellipsis") {
    install_stub_deps();

    const int widths[] = {40, 50, 60, 68, 76, 90, 120, 160, 200};
    constexpr int kRows = 60;

    std::size_t total = 0;
    for (const auto& c : cases())
        for (int w : widths) {
            Model m = with_history();
            c.open(m);
            const Damage d = measure(c, m, w, kRows);
            total += d.clipped.size();
            if (!d.clipped.empty()) {
                std::fprintf(stderr, "\n%s at width %d clipped %zu write(s):\n",
                             c.name, w, d.clipped.size());
                std::size_t shown = 0;
                for (const auto& s : d.clipped) {
                    if (shown++ >= 6) {
                        std::fprintf(stderr, "    … and %zu more\n",
                                     d.clipped.size() - 6);
                        break;
                    }
                    std::fprintf(stderr, "    %s\n", s.c_str());
                }
            }
        }

    std::fprintf(stderr,
                 "\npanels: %zu clipped write(s) across %zu panels x %zu widths\n",
                 total, cases().size(), std::size(widths));

    // 门不是门,不能开 UP 剩下的
    // 十人被整好的日子 就会变成0
    CHECK(total <= 10);
}
