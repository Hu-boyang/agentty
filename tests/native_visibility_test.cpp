// native_visibility_test - 没什么 agentty 漆在下边隐形
// theme::native.
//
// ── 错误 ─────────────────────────────────────────────────────────────
//
// agentty #45: 推理块 接近 -black 黑色的 黑色的,
// 黑色的,蓝色的,黑色 终端 ——无形,但可选,所以文字明显存在,
// 被报道为推理错误,不是推理错误.
//
// A LitColor 谓 PAINTABLE 但不一定要这样 NUMERIC 仅
// 选择 Kind::Rgb 频道; 命名和索引保存 PALETTE INDEX 输入
// 时间(_R): 字节 以 g_ 和 b_ 0 表示,默认完全无任何。 maya
// '混合助手读取 r()/g()/b() 无条件,所以调色板索引变成了红色频道:
//
//     bright_black - > 命名(8) - > lerp - > rgb(8,0) - > "38;2;8;0;0"
//
// theme::native 将每个槽都声明为命名或默认 ON
// PURPOSE ——这就是用户自己的调色板,已经对比了
// -checked 由它们到达屏幕。这使得本地主题成为程序中每个
// 混合不健全的主题,也是没有开发者运行的主题,因为每个人都会选择
// 一个方案。
//
// ── 电话 不变量 ───────────────────────────────────────────────────────
//
// 本地人, , , (美国) 无内容 agentty 渲染 可能
// 发出真彩色 SGR 选择 "should mostly avoid"
// —— 38;2 原生物的序列按构造表示用户没有选择的颜色,
// 从 字节 而不是频道 副作用 无法计算者必须降解为 NO EFFECT.
//
// third_party/maya/tests/test_blend_safety.cpp
// 将帮助者隔离。此选项将属性结束 -to-end ,真实的 agentty 帧 ,
// 而45级实际上被报告为。
#include "agtest.hpp"

#include <string>
#include <vector>

#include <maya/core/animation.hpp>
#include <maya/core/anim_clock.hpp>   // advance_anim_clock_ms (reveal frames)
#include <maya/render/canvas.hpp>
#include <maya/render/renderer.hpp>
#include <maya/style/theme.hpp>
#include <maya/widget/app_layout.hpp>
#include <nlohmann/json.hpp>

#include "agentty/domain/ui_theme.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/view/thread/thread.hpp"
#include "agentty/runtime/view/changes_strip.hpp"
#include "agentty/runtime/view/composer.hpp"
#include "agentty/runtime/view/status_bar/status_bar.hpp"
#include "agentty/runtime/view/palette.hpp"   // theme_owns_canvas

using namespace agentty;
using namespace maya;
using json = nlohmann::json;

namespace {

// 每一个插槽 `native` ,在参数上混合到其他的,
// 实际是去访问。返回违法的对。
std::vector<std::string> native_blend_offenders() {
    const Theme& th = theme::native;
    std::vector<std::string> bad;
    for (std::size_t i = 0; i < kThemeSlotCount; ++i) {
        const auto si = static_cast<ThemeSlot>(i);
        const LitColor a = th.resolve(Color::slot(si));
        for (std::size_t j = 0; j < kThemeSlotCount; ++j) {
            const auto sj = static_cast<ThemeSlot>(j);
            const LitColor b = th.resolve(Color::slot(sj));
            for (double t : {0.0, 0.25, 0.5, 0.75, 1.0}) {
                if (anim::lerp(a, b, t).kind() == ColorKind::Rgb)
                    bad.push_back(std::string(slot_field_name(si)) + " x "
                                  + std::string(slot_field_name(sj)));
            }
        }
    }
    return bad;
}

} // 命名空间结束。

TEST_CASE("native: every slot states a palette colour, never an RGB literal") {
    const Theme& th = theme::native;
    for (std::size_t i = 0; i < kThemeSlotCount; ++i) {
        const auto s = static_cast<ThemeSlot>(i);
        const LitColor c = th.resolve(Color::slot(s));
        const std::string name{slot_field_name(s)};
        const std::string lit_msg =
            "native slot '" + name + "' is an RGB literal — native exists to "
            "let the user's own palette through, so every slot must be Named "
            "or Default";
        CHECK(c.kind() != ColorKind::Rgb, lit_msg);
        const std::string ch_msg =
            "native slot '" + name + "' reports channels; arithmetic on it "
            "would read a palette index as colour";
        CHECK(!c.has_channels(), ch_msg);
    }
}

TEST_CASE("native: no pairwise slot blend fabricates a truecolor triple") {
    const auto bad = native_blend_offenders();
    const std::string msg =
        "native slot blend(s) fabricated an RGB triple — this is the agentty "
        "#45 shape (first: " + std::string(bad.empty() ? "-" : bad.front())
        + ")";
    CHECK(bad.empty(), msg);
}

TEST_CASE("native: bright_black stays SGR 90, never a truecolor triple") {
    // 问题的确切复制,作为一个价值。 正文 决心,
    // 而固定的路径 混合它自己 在每行,
    // 所以一个平面的混合 必须 成为身份。
    const LitColor muted = theme::native.resolve(Color::slot(ThemeSlot::Muted));
    CHECK(muted.fg_sgr() == "90", "native muted must be bright-black (SGR 90)");
    for (double t : {0.0, 0.5, 1.0}) {
        const LitColor got = anim::lerp(muted, muted, t);
        const std::string msg =
            "a flat blend emitted '" + got.fg_sgr() + "' — expected the "
            "palette code 90, not arithmetic on the index";
        CHECK(got.fg_sgr() == "90", msg);
    }

    // 默认(`text`槽)也必须存活下来:它根本没有渠道,
    // 而用麻黄来将其变成字面黑色.
    const LitColor text = theme::native.resolve(Color::slot(ThemeSlot::Text));
    CHECK(text.fg_sgr() == "39", "native text must be the terminal default");
    CHECK(anim::lerp(text, text, 0.5).fg_sgr() == "39",
          "blending the default colour with itself must stay the default");
}

namespace {

// 渲染 原生的模型,并返回每个真彩色 存储单元 找到他们了
//
// 本地人没有频道 所以 任意 显示 Rgb
// 前景或背景 帧 计算自 字节 将罪犯送回
// 监狱 而不是一个小鸟 使故障信息能够
// 命名一行一列, 这正是最初的诊断 快速。
// diff调色板是原生语言下允许的ONE系列文字.
//
// diff在故意(maya::diff_palette):
// 绿色表示添加,红色表示去除,这是内容而不是他们
// 所带的染色体——相同的豁免语法突出化得到.
// 标注两边也是在用户调色板对我们来说无法读取时保证
// 对比的唯一方法,这正是原生的制作方式.
//
// 明确列出,而不是按区域略去,因此 新 任何
// 地方的文字都仍然无法清除
[[nodiscard]] bool is_diff_palette(LitColor c) {
    for (const maya::Color& allowed : {
            maya::diff_palette::add_bg,   maya::diff_palette::add_rail,
            maya::diff_palette::add_fg,   maya::diff_palette::add_fg_hi,
            maya::diff_palette::rem_bg,   maya::diff_palette::rem_rail,
            maya::diff_palette::rem_fg,   maya::diff_palette::rem_fg_hi,
            maya::diff_palette::hunk_bg,  maya::diff_palette::hunk_fg })
        if (theme::native.resolve(allowed).fg_sgr() == c.fg_sgr()) return true;
    return false;
}

std::vector<std::string> truecolor_cells(Model& m, int w = 100, int h = 400) {
    auto root = maya::AppLayout{{
        .thread        = ui::thread_config(m),
        .changes_strip = ui::changes_strip_config(m),
        .composer      = ui::composer_config(m),
        .status_bar    = ui::status_bar_config(m),
        .overlay       = std::nullopt,
    }}.build();

    maya::StylePool pool;
    maya::Canvas canvas(w, h, &pool);
    canvas.clear();
    maya::render_tree(root, canvas, pool, maya::theme::native, true);

    std::vector<std::string> bad;
    const int max_row = canvas.max_content_row();
    for (int y = 0; y <= max_row; ++y) {
        for (int x = 0; x < w; ++x) {
            const auto& cell = canvas.get(x, y);
            const maya::Style& st = pool.get(cell.style_id);
            for (const auto* c : {&st.fg, &st.bg}) {
                if (!c->has_value()) continue;
                const LitColor lit = theme::native.resolve(**c);
                if (lit.kind() != ColorKind::Rgb) continue;
                if (is_diff_palette(lit)) continue;
                bad.push_back("row " + std::to_string(y) + " col "
                              + std::to_string(x) + " -> " + lit.fg_sgr());
            }
        }
    }
    return bad;
}

// 每一个 存储单元 墨水是怎样的 相同 COLOUR 当作下面的纸
//
// 这是真彩色扫描无法看到的失败, 也是实际运出失败。 在本地版下, 23个主题槽会
// 倒塌到 16 上。 ANSI 颜色 终端 定义——如此 `success` 财务
// 报告和已审计财务报表 `diff_added` 都一样 SGR 32,
// `text` 财务报告和已审计财务报表 `inverse_text` 都一样 SGR 39,
// 一个配对两个的部件 画绿色 -on-green 默认 -on-default.
//
// 两种颜色都是完全合法的调色板条目。 没有任何东西是捏造的,
// 没有频道被误读, Themed门满意, 文本是无形的。
// Contrast 是该( 块) 的属性。 -pair ,
// 主题)组合,只有主题知道——所以必须检查一个提供 帧 ,
// 而不是在 调用 网站。
std::vector<std::string> invisible_cells(Model& m, int w = 100, int h = 400) {
    auto root = maya::AppLayout{{
        .thread        = ui::thread_config(m),
        .changes_strip = ui::changes_strip_config(m),
        .composer      = ui::composer_config(m),
        .status_bar    = ui::status_bar_config(m),
        .overlay       = std::nullopt,
    }}.build();

    maya::StylePool pool;
    maya::Canvas canvas(w, h, &pool);
    canvas.clear();
    maya::render_tree(root, canvas, pool, maya::theme::native, true);

    std::vector<std::string> bad;
    const int max_row = canvas.max_content_row();
    for (int y = 0; y <= max_row; ++y) {
        for (int x = 0; x < w; ++x) {
            const auto& cell = canvas.get(x, y);
            // 空闲 存储单元 墨水没了,都一样 -colour 罚款( 正常:
            // 乐队的垫子就是这个 IS).
            if (cell.character == U' ' || cell.character == 0) continue;
            const maya::Style& st = pool.get(cell.style_id);
            if (!st.fg.has_value() || !st.bg.has_value()) continue;

            const LitColor fg = theme::native.resolve(*st.fg);
            const LitColor bg = theme::native.resolve(*st.bg);
            // 比较 BYTES 到达 终端 而不是插槽。
            // 两个不同的槽名可以是分辨率后的一个颜色,
            // 这正是错误.
            if (fg.fg_sgr() != bg.fg_sgr()) continue;

            std::string glyph;
            if (cell.character < 128) glyph = std::string(1, char(cell.character));
            bad.push_back("row " + std::to_string(y) + " col "
                          + std::to_string(x) + " '" + glyph
                          + "' fg==bg==SGR " + fg.fg_sgr());
        }
    }
    return bad;
}

Message reasoning_msg(bool done) {
    Message a;
    a.role = Role::Assistant;
    a.id   = MessageId{"a1"};
    a.thinking = "Let me start by exploring the repository structure.\n\n"
                 "The user wants:\n\n"
                 "1. An audit of the **bwrap** implementation\n"
                 "2. A blueprint of what it would take\n\n"
                 "This is a blueprint task, no code editing.\n\n"
                 "The cwd is /home/jon/sandbox. Let me check the structure";
    if (done) a.text = "Here is the audit.";
    return a;
}

} // 命名空间结束。

TEST_CASE("native: a rendered reasoning turn paints no truecolor cell") {
    // End-to-end, SETTLED : 州 首个截图显示.
    ui_prefs::publish_theme(theme::native);

    Model m;
    m.d.show_reasoning = true;
    m.d.current.messages.push_back(reasoning_msg(/*完成=*/true));
    m.s.phase = phase::Idle{};

    const auto bad = truecolor_cells(m);
    const std::string msg =
        "cell(s) painted a truecolor value under theme::native — native states "
        "no RGB, so every triple here was computed from bytes that were not "
        "channels (agentty #45). First: "
        + std::string(bad.empty() ? "-" : bad.front());
    CHECK(bad.empty(), msg);
}

TEST_CASE("native: a LIVE streaming reasoning block stays visible") {
    // 问题所在的截图 ACTUALLY 显示: 中 -stream,
    // 头文件 读取 "Thinking", 正文 这与定居的道路不同, 方式很重要。
    // ReasoningStream 应用其 "stream of consciousness"
    // GRADIENT 活着, 消失 body_fg 改为
    // body_fg_bright 街坊下方,这是每块 -line 两
    // 个主题档间发热, i.e。精确的操作产生 rgb(8,0,0)。
    //
    // 单是确定测试就通过 梯度仍然破损,因为一个确定
    // 块 渲染 平面,报告 -state 这里的覆盖率
    // 不是可选的。
    ui_prefs::publish_theme(theme::native);

    Model m;
    m.d.show_reasoning = true;
    m.d.current.messages.push_back(reasoning_msg(/*完成=*/false));
    m.s.phase = phase::Streaming{phase::Active{}};

    const auto bad = truecolor_cells(m);
    const std::string msg =
        "a LIVE reasoning block painted truecolor under native — the live "
        "gradient blends two slots per line, which is the #45 operation. "
        "First: " + std::string(bad.empty() ? "-" : bad.front());
    CHECK(bad.empty(), msg);
}

TEST_CASE("native: the reveal animation paints no truecolor") {
    // 逐步显示 谓 ON 默认时,在任何地方,这是另一个 -cell
    // 颜色 副作用 程序中的: decorate_text_reveal
    // 鬼魂和滑翔机新鲜 -arrived 格言,
    // 还有 decorate_end_caret 以划分背景来构建背景 光标
    // 彩色的频道是四倍。 两者都是彩色的算术, 都在直播中运行。 末尾 ,
    // 而且既无 由定居 渲染.
    ui_prefs::publish_theme(theme::native);
    ::setenv("AGENTTY_REVEAL", "1", 1);

    Model m;
    m.d.show_reasoning = true;
    m.d.current.messages.push_back(reasoning_msg(/*完成=*/false));
    m.s.phase = phase::Streaming{phase::Active{}};

    // 几部动画 帧 : 数据 逐步显示 窗口移动,所以一个 帧
    // 并不是它的样本。
    std::vector<std::string> bad;
    for (int frame = 0; frame < 8 && bad.empty(); ++frame) {
        maya::testing::advance_anim_clock_ms(16);
        bad = truecolor_cells(m);
    }
    ::unsetenv("AGENTTY_REVEAL");

    const std::string msg =
        "the reveal animation painted truecolor under native. First: "
        + std::string(bad.empty() ? "-" : bad.front());
    CHECK(bad.empty(), msg);
}

// ── 扫荡 ────────────────────────────────────────────────────
//
// 上面三个案例都包含推理部件, 因为这就是 #
// 45 命名的。 现在, 同一个错误在 a 中出现了两次。
// 不同 无人在原生地制造的部件—— 逐步显示 动画,
// 然后是diff乐队和状态横幅。每次固定是局部的,
// 每次下个实例已经坐在那里。
//
// 别再追逐部件了,把地产 伸到 UI :将
// 每个主要表面立即放上屏幕,并需要整个 帧
// 以在本地状态下没有真彩色。在添加颜色的当天,
// 一个硬码的新部件会在这里失效,
// 没有人记得为它写一个测试,这是在写它的人
// 继续工作之后唯一一种覆盖。
TEST_CASE("native: no widget anywhere paints truecolor") {
    ui_prefs::publish_theme(theme::native);

    Model m;
    m.d.show_reasoning = true;

    // 用户转动,所以 user/assistant 拆分( E) 渲染.
    Message u;
    u.role = Role::User;
    u.id   = MessageId{"u1"};
    u.text = "audit the sandbox and show me the diff";
    m.d.current.messages.push_back(std::move(u));

    // 助理转身 携带推理, 传言, 和 工具 调用 工具时间表、
    // 工具时间表、 正文 预览, 和其中的 diff 带。
    Message a = reasoning_msg(/*完成=*/true);
    a.text =
        "Here is the audit.\n\n"
        "- `bwrap` is invoked per tool call\n"
        "- the profile is **fixed** at startup\n\n"
        "```cpp\nint main() { return 0; }\n```\n";

    ToolUse shell;
    shell.id     = ToolCallId{"call_1"};
    shell.name   = ToolName{"shell"};
    shell.args   = json{{"command", "ls -la"}};
    shell.status = ToolUse::Done{.output = "total 4\ndrwxr-xr-x  2 jon jon\n"};
    a.tool_calls.push_back(std::move(shell));

    // 写 + 统一 diff: 带硬码的两条路径 GitHub
    // -dark 绿的和珊瑚红的。
    ToolUse write;
    write.id     = ToolCallId{"call_2"};
    write.name   = ToolName{"write"};
    write.args   = json{{"file_path", "/tmp/x.cpp"}, {"content", "int x = 1;\n"}};
    write.status = ToolUse::Done{.output = "wrote 1 line"};
    a.tool_calls.push_back(std::move(write));

    ToolUse diff;
    diff.id     = ToolCallId{"call_3"};
    diff.name   = ToolName{"git_diff"};
    diff.args   = json{{"path", "."}};
    diff.status = ToolUse::Done{.output =
        "diff --git a/x.cpp b/x.cpp\n"
        "@@ -1,3 +1,4 @@\n"
        " context line\n"
        "-removed line\n"
        "+added line\n"};
    a.tool_calls.push_back(std::move(diff));

    // ...编辑,一个 THIRD 带状路径, 带有自己的颜色决定。
    ToolUse edit;
    edit.id   = ToolCallId{"call_4"};
    edit.name = ToolName{"edit"};
    // 顶层 old_text/new_text，输出中没有 ```diff 围栏。
    // 组合就是达到 Kind::EditDiff 财务报告和已审计财务报表
    // push_diff_side 乐队路径。 `edits` ARRAY
    // 或任何带有二维栅栏的输出, 渲染 以 Kind::GitDiff 而不是
    // 碰这个代码, 这就是为什么这个测试的第一个版本 与bug 在场。
    edit.args = json{{"path", "src/x.cpp"},
                     {"old_text", "int old_line = 1;\nint stays = 2;\n"},
                     {"new_text", "int new_line = 1;\nint stays = 2;\n"}};
    edit.status = ToolUse::Done{.output = "edited src/x.cpp"};
    a.tool_calls.push_back(std::move(edit));

    m.d.current.messages.push_back(std::move(a));
    m.s.phase = phase::Idle{};

    // diff乐队是 ON 由本地人统治,这是故意的。
    //
    // 他们在这里被短暂关闭, 理论是,原住民没有画布,所以没有什么可以填补。
    // 调用 绿色 -means-added 四十年的肌肉,四十年的肌肉,
    // 年长的肌肉,四十年的肌肤, 年久的肌肉, 四年的肌肉 内存 ,
    // 并扔下乐队 丢掉整个视觉语言 一个diff 解决一个墨水问题。
    //
    // 一个二位是 CONTENT ,而不是被套用过的铬——相同的免
    // 责语法突出化得到。它拥有 两者 其对齐的两侧( 3)
    // maya's diff_palette ),这是当我们无法读
    // 取用户调色板时保证对比的唯一方法。这就是为什么它甚至在这里
    // 是真颜色,为什么下面的扫描可以免除它。
    CHECK(maya::ToolBodyPreview::diff_bands_ok_for_test(),
          "a 256-colour terminal must get diff bands — they are content, "
          "not chrome, and diff_palette states both sides so contrast does "
          "not depend on a palette we cannot read");

    // 三种类型的地位标志—— crimson/amber/
    // 硬化调色板, 直至此任务完成。 这种调色板从
    // 状态中分类。 TEXT ,所以驱动它的方式应用
    // 而不是设置 枚举 后面的分类员的背面。
    for (const char* status : {"error: the request failed",
                              "retrying (upstream cut off)",
                              "indexing the workspace"}) {
        m.s.status = status;
        m.s.status_until = {};   // 无过期, 因此 status_active( ) 是真实的
        const auto bad = truecolor_cells(m, 120, 600);
        const std::string msg =
            "a widget painted truecolor under theme::native — native states "
            "no RGB, so this colour is the widget's own palette overriding "
            "the user's. First: "
            + std::string(bad.empty() ? "-" : bad.front());
        CHECK(bad.empty(), msg);
    }
}

// ── 颜色相同 ─────────────────────────────────────
//
// 上面的扫荡问 "did anyone invent a colour"。
// 这向观看屏幕的人提出了一个实际重要的问题:
// "can you read it".
//
// 它们是不同的问题,第一个问题并不意味着第二个问题。
// 修复发明 -colour 通过将部件移动到主题槽上,
// 错误引入了此选项: 在本地状态下, 23个槽会崩溃到 16 个
// ANSI 颜色 终端 拥有,所以 `success` 财务报告和已审计
// 财务报表 `diff_added` 都一样 SGR 32和diff乐队
// 成为绿色的文字 在绿色的街区。 源码树 满意了——没有频道误读,
// 没有文字,没有真彩——文本是看不见的。
//
// 一个想要填充的带子的部件需要询问主题是否拥有一个
// 画布来填充。 本地版不是设计上的,
// 所以带子会降为彩色。 TEXT 平原上总是读到
// diff和状态线 终端.
TEST_CASE("native: a filled chip picks ink that reads on its band") {
    // 模型徽章在填充的块上涂上供应商的名字——墨水来自
    // `inverse_text` ,从模范家庭的呼声中画出来。
    // inverse_text 默认值: 终端's ORDINARY 前面,
    // 在做灯光的鬼魂身上 -lavender "Anthropic"
    // 闪亮的阳光 -magenta 标签.
    //
    // 问题在于 不 以放弃填充—— 标签 挣到钱,
    // 就是赚到钱 "provider" 改为容器 "model"
    // 修补是从中选择墨水 BAND :测量其亮度,
    // 回答黑白。所以测试是每个填充的 存储单元 带有所选择的墨水,
    // 在不懂得用户调色板的情况下,可以检查。
    ui_prefs::publish_theme(theme::native);
    CHECK(!ui::theme_owns_canvas(),
          "native states Default for background — it paints no canvas, which "
          "is the whole point of it");

    Model m;
    m.d.show_reasoning = true;
    m.d.current.messages.push_back(reasoning_msg(/*完成=*/true));
    m.s.phase = phase::Idle{};

    auto root = maya::AppLayout{{
        .thread        = ui::thread_config(m),
        .changes_strip = ui::changes_strip_config(m),
        .composer      = ui::composer_config(m),
        .status_bar    = ui::status_bar_config(m),
        .overlay       = std::nullopt,
    }}.build();

    maya::StylePool pool;
    maya::Canvas canvas(120, 400, &pool);
    canvas.clear();
    maya::render_tree(root, canvas, pool, maya::theme::native, true);

    std::string first;
    int bad = 0;
    for (int y = 0; y <= canvas.max_content_row(); ++y) {
        for (int x = 0; x < 120; ++x) {
            const auto& cell = canvas.get(x, y);
            if (cell.character == U' ' || cell.character == 0) continue;
            const maya::Style& st = pool.get(cell.style_id);
            if (!st.bg.has_value() || !st.fg.has_value()) continue;

            const LitColor bg = theme::native.resolve(*st.bg);
            const LitColor fg = theme::native.resolve(*st.fg);
            // 默认 bg 是 "the terminal's own" —— 无填充, 无可检查。
            if (bg.kind() == ColorKind::Default) continue;

            // 在真正的波段上,墨水必须是ink_for()所选择的.
            // 其它东西都是在没有看乐队的情况下摘的,
            // 这就是默认最终在亮色的红衣上被取走的原因.
            const LitColor want = ink_for(bg);
            if (fg.fg_sgr() == want.fg_sgr()) continue;

            ++bad;
            if (first.empty())
                first = "row " + std::to_string(y) + " col "
                      + std::to_string(x) + " bg=SGR " + bg.fg_sgr()
                      + " fg=SGR " + fg.fg_sgr()
                      + " (wanted SGR " + want.fg_sgr() + ")";
        }
    }

    const std::string msg =
        "a filled cell under theme::native carries ink that was not chosen "
        "from its band. inverse_text is Default here, i.e. the terminal's "
        "ordinary foreground, so it cannot be trusted on a colour — use "
        "ui::ink_on(band). First: "
        + std::string(first.empty() ? "-" : first);
    CHECK(bad == 0, msg);
}

TEST_CASE("native: no text is painted in its own background colour") {
    ui_prefs::publish_theme(theme::native);

    Model m;
    m.d.show_reasoning = true;

    Message u;
    u.role = Role::User;
    u.id   = MessageId{"u1"};
    u.text = "show me the diff";
    m.d.current.messages.push_back(std::move(u));

    Message a = reasoning_msg(/*完成=*/true);
    a.text = "Here is the audit.";

    // diff路径:写(All-adds bod)和真正的统一diff.
    ToolUse write;
    write.id     = ToolCallId{"call_w"};
    write.name   = ToolName{"write"};
    write.args   = json{{"file_path", "/tmp/x.cpp"}, {"content", "int x = 1;\n"}};
    write.status = ToolUse::Done{.output = "wrote 1 line"};
    a.tool_calls.push_back(std::move(write));

    ToolUse diff;
    diff.id     = ToolCallId{"call_d"};
    diff.name   = ToolName{"git_diff"};
    diff.args   = json{{"path", "."}};
    diff.status = ToolUse::Done{.output =
        "diff --git a/x.cpp b/x.cpp\n"
        "@@ -1,3 +1,4 @@\n"
        " context line\n"
        "-removed line\n"
        "+added line\n"};
    a.tool_calls.push_back(std::move(diff));

    // EDIT是第三条单独的带状路径——push_diff_side,
    // 不是git_diffXX 解析器,它自留了硬编码的颜色,很久以后,其他两个带状的带状,
    // 它就一直保持了自己的硬编码颜色. Expanish that 渲染
    // only write,git_diff通过,而编辑的带仍然是绿色的-on-green,
    // 这正是所发生的.
    ToolUse edit;
    edit.id   = ToolCallId{"call_e"};
    edit.name = ToolName{"edit"};
    // 顶层 old_text/new_text，输出中没有 ```diff 围栏。
    // 组合就是达到 Kind::EditDiff 财务报告和已审计财务报表
    // push_diff_side 乐队路径。 `edits` ARRAY
    // 或任何带有二维栅栏的输出, 渲染 以 Kind::GitDiff 而不是
    // 碰这个代码, 这就是为什么这个测试的第一个版本 与bug 在场。
    edit.args = json{{"path", "src/x.cpp"},
                     {"old_text", "int old_line = 1;\nint stays = 2;\n"},
                     {"new_text", "int new_line = 1;\nint stays = 2;\n"}};
    edit.status = ToolUse::Done{.output = "edited src/x.cpp"};
    a.tool_calls.push_back(std::move(edit));

    m.d.current.messages.push_back(std::move(a));
    m.s.phase = phase::Idle{};

    // 和状态旗, 其填充的乐队有相同的bug。
    for (const char* status : {"error: the request failed",
                              "retrying (upstream cut off)",
                              "indexing the workspace"}) {
        m.s.status = status;
        m.s.status_until = {};
        const auto bad = invisible_cells(m, 120, 600);
        const std::string msg =
            "text painted in its own background colour under theme::native — "
            "both are valid palette entries, and the result is unreadable. "
            "A band needs a canvas the theme actually owns; native has none, "
            "so it must degrade to coloured text. First: "
            + std::string(bad.empty() ? "-" : bad.front());
        CHECK(bad.empty(), msg);
    }
}
