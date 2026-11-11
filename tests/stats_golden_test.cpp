// stats_golden_test — 字节-identity 守护每个
// 数据标签, 以改变其布局的每个宽度和高度。
//
// 原因 此 EXISTS
// ===============
// 数据查看器即将从自己的端口移植 StatSheet ——
// 重新分类 -implements 铬, 可视区域 ,宽度谈判和列布局,
// maya::Panel 已经拥有了—— 进入面板家族
// 的 Item/Control 词汇表。这是重写 渲染 路径,
// 重写就是修改 diff 读取无法校验。
//
// 三次不同的尝试来改进这个子系统 运出回归,
// 现有的测试都是绿色的: 吞噬自己值柱的条,
// 冻结在14的轨迹 存储单元 ,滚动来抵消 帧 门无法看到。
// 修复器每次在 diff 中都正确看。
// 缺少的是像素记录。 USED 成为。
//
// 这么一来,像素就被钉上了 "does it look reasonable" - 说不出来 - 但是
// 这些单元格必须完全一致；任何变化都要说明原因。
//
//
// 测试失败后的使用方法。
// ===========================
// 失败不是自动错误, 它说渲染 已经更改。
//   1. 运行 tests/stats_visual 以失败的宽度表示 SEE 零钱。
//   2. 如果改变是错误的,就予以纠正,这就是存在的情况。
//   3. 如果更改正确, 将 kGoldenHash 设置为 0, 运行一次打印
//      新的散列,并粘贴它回来-- 与变化的相同承诺,
//      所以diff显示一个像素变化是被打算和审查的.
//
// 人均数 -tab 特征断言如下: -pin :他们
// 说必须输出什么 CONTAIN 不论它的确切值多少 字节 ,
// 所以一个 -pinned 散列不能悄悄地掉下整节.

#include "agtest.hpp"

#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/msg.hpp"
#include "agentty/runtime/view/panels.hpp"
#include "agentty/domain/stats/tabs.hpp"

#include <maya/render/canvas.hpp>
#include <maya/render/renderer.hpp>
#include <maya/style/theme.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

namespace pn = agentty::ui::panel;
using namespace agentty;

namespace {

std::uint64_t fnv1a(std::string_view s) {
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    return h;
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

// A FIXED 记录。每个数字的面板打印都来自这个,
// 所以金色 字节 函数 渲染器 一个人
//
// 故意的有多种:几种型号,路由和无序转弯,工具失败,
// 缓存点击和失手,推理符,以及一个长的工具名称(
// git_status)——因为一张表格的标签栏
// 的条目最宽,
// 而4-character名称的固定不能行使长标签挤占赛道的情况.
Model golden_model() {
    Model m;
    m.d.current.id = ThreadId{"golden-stats"};

    auto turn = [&](const char* model, std::uint32_t in, std::uint32_t out,
                    std::uint32_t cache_r, std::uint32_t cache_w,
                    std::uint32_t ttft, std::uint32_t stream,
                    std::uint32_t think) {
        Message u;
        u.role = Role::User;
        u.text = "a question";
        m.d.current.messages.push_back(std::move(u));

        Message a;
        a.role         = Role::Assistant;
        a.served_model = ModelId{model};
        a.text         = "an answer with some prose in it";
        Message::Telemetry t;
        t.input_tokens     = in;
        t.output_tokens    = out;
        t.cache_read       = cache_r;
        t.cache_creation   = cache_w;
        t.ttft_ms          = ttft;
        t.stream_ms        = stream;
        t.reasoning_tokens = think;
        a.telemetry = t;
        return &m.d.current.messages.emplace_back(std::move(a));
    };

    turn("claude-opus-4-5",   8200, 1400,     0, 8200, 1200, 9400, 900);
    turn("claude-sonnet-4-5",  900,  600, 15800,    0,  240, 2100,   0);
    turn("claude-sonnet-4-5", 1100,  480, 16200,    0,  180, 1700,   0);
    turn("claude-haiku-4-5",   500,  260, 15900,    0,  120,  700,   0);
    return m;
}

// A LONG 切换到一个真实的形状
//
// 上面的固定是四个转弯, 和干净的数字, 和它产生的
// 每个值是短的: "4 turns", "16s",
// "2.1k" 实值 线程 工作会议截图显示2223
// 转和3h33m, 计数是四个数字, 持续时间带
// 两个单位, 面板中的每个字符串都比文件所显示的
// 要宽。
//
// 这一点很重要,因为宽度错误是由CONTENT宽而不是
// 终端宽度触发的,而且一个其值都短的固定点无法到达它们。
// 整洁的固定点不会测试布局;它测试布局最简单的情况,
// 并报告为通过。
//
// 故意尴尬:四 -digit 计数,小时 -scale 持续时间,
// 一个模型名称足够长,可以竞争标签道, 推理符号如此 "of which reasoning"
// 子目录 -row 出现。
Model heavy_model() {
    Model m;
    m.d.current.id = ThreadId{"heavy-stats"};

    auto turn = [&](const char* model, std::uint32_t in, std::uint32_t out,
                    std::uint32_t cache_r, std::uint32_t cache_w,
                    std::uint32_t ttft, std::uint32_t stream,
                    std::uint32_t think) {
        Message u;
        u.role = Role::User;
        u.text = "a question";
        m.d.current.messages.push_back(std::move(u));

        Message a;
        a.role         = Role::Assistant;
        a.served_model = ModelId{model};
        a.text         = "an answer with some prose in it";
        Message::Telemetry t;
        t.input_tokens     = in;
        t.output_tokens    = out;
        t.cache_read       = cache_r;
        t.cache_creation   = cache_w;
        t.ttft_ms          = ttft;
        t.stream_ms        = stream;
        t.reasoning_tokens = think;
        a.telemetry = t;
        return &m.d.current.messages.emplace_back(std::move(a));
    };

    // 转四圈够多了 -digit 计数和小时 -scale 总计。
    for (int i = 0; i < 400; ++i) {
        turn("claude-opus-4-5",    9100, 2300, 48000, 9100, 2400, 31000, 4100);
        turn("claude-sonnet-4-5",  7400, 1900, 52000,    0, 1100, 18000,    0);
        turn("claude-haiku-4-5",   3300,  850, 41000,    0,  420,  6200,    0);
    }
    return m;
}

// 将面板平整为文字, 并设定宽度和高度。
// 文本 STATs 面板有 CLIPPED AWAY ,横跨每个标签和宽度。
//
// 填充单位: render_at 时 帧 画,
// 在结尾处一次。 帧 无法从 帧 黄金散
// 列 外溢扫描 和每个功能检查读到什么
// LANDED 画布上,画布上无法显示
// 从未画过的东西。 -looking 帧
// 缺少一个数字——这正是一周宽度bug如何
// 在屏幕截图中明显地不为这个文件所覆盖。
std::vector<std::string> g_clipped;

std::string render_at(const Model& m, int w, int rows) {
    // 面板将自己的min_width夹在终端上,当没有tty
    // 时读作COLUMNS;LINES驱动可视区域的方式相同,
    // 两者必须设定或金色的字节是机器的一种功能,而不是代码.
    const std::string cols = std::to_string(w);
    const std::string lns  = std::to_string(rows);
    setenv("COLUMNS", cols.c_str(), 1);
    setenv("LINES",   lns.c_str(),  1);

    maya::StylePool pool;
    maya::Canvas canvas(w, rows, &pool);
    canvas.on_clip_overflow([w, rows](const maya::Canvas::ClipOverflow& o) {
        g_clipped.push_back(std::to_string(w) + "x" + std::to_string(rows)
                            + " dropped '" + std::string(o.text)
                            + "' (wanted " + std::to_string(o.wanted)
                            + " cols, edge at " + std::to_string(o.edge) + ")");
    });
    maya::render_tree(ui::stats_panel(m), canvas, pool, maya::theme::native,
                      /*auto_height=*/true);

    std::string out;
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < w; ++x) {
            const char32_t c = canvas.get(x, y).character;
            // UTF -8编码:数字是盲文和盒子 -drawing ,
            // 然后折叠到 '?' 使散列对这个面板所
            // 编的文字完全视而不见。
            const char32_t ch = c ? c : U' ';
            if (ch < 0x80) out += static_cast<char>(ch);
            else if (ch < 0x800) {
                out += static_cast<char>(0xC0 | (ch >> 6));
                out += static_cast<char>(0x80 | (ch & 0x3F));
            } else if (ch < 0x10000) {
                out += static_cast<char>(0xE0 | (ch >> 12));
                out += static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (ch & 0x3F));
            } else {
                out += static_cast<char>(0xF0 | (ch >> 18));
                out += static_cast<char>(0x80 | ((ch >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (ch & 0x3F));
            }
        }
        out += '\n';
    }
    return out;
}

// 每个标签,在改变布局的宽度和高度上.
//
// 表格所作决定的宽度:40个在任何拆分以下,
// 68个是报告的电话-pane宽度,76/90跨分分界线,
// 120/200是应当使用宽面而不是留空面的地方。
// 高度之所以重要,是因为列数是对VERTICAL压力
// 的反应——一个适合不分裂的标签,无论它的宽度如何.
//
// Model 正在移动 -only (拥有一个 回滚缓冲区 分类账
// 和视图 缓存 ),这是正确的 调用 对于这样大小的一类,
// 每一个案例都会建立自己的基础,而不是复制一个基础。固定的定型是决定性的,
// 所以重建相当于复制, 并且成本不值任何重要的东西。
std::string render_all() {
    const int widths[]  = {40, 60, 68, 76, 90, 120, 200};
    const int heights[] = {24, 40, 60};

    std::string all;
    for (const auto& desc : stats::kTabs) {
        for (int w : widths)
            for (int h : heights) {
                auto [opened, _] =
                    app::update(golden_model(), Msg{OpenStats{}});
                if (auto* o = opened.ui.panel.get<pn::Stats>())
                    o->tab = desc.id;
                all += "== ";
                all += stats::tab_title(desc.id);
                all += " " + std::to_string(w) + "x" + std::to_string(h) + " ==\n";
                all += render_at(opened, w, h);
            }
    }
    return all;
}

}  // 命名空间结束。

TEST_CASE("stats_golden: every tab renders byte-identically") {
    install_stub_deps();
    const std::string out = render_all();

    // STATS_DUMP=<tab> 打印已提供 帧 给一个标签和停止。
    //
    // 散列说 该 编号 渲染 更改; 此内容 内容 换成这个,
    // 这就是你实际遇到的问题,当它失败时。
    // 它重新使用这个绳子而不是生活在 stats_visual
    // 因为这里的固定是写在断言上反对的——第二套固定是
    // 第二套要保持真实的东西.
    //
    //   STATS_DUMP=Tools ctest -R stats_golden --output-on-failure
    //   STATS_DUMP 全部... 每个标签
    if (const char* want = std::getenv("STATS_DUMP")) {
        const std::string sel = want;
        std::size_t pos = 0;
        bool on = false;
        while (pos < out.size()) {
            const std::size_t nl = out.find('\n', pos);
            if (nl == std::string::npos) break;
            const std::string line = out.substr(pos, nl - pos);
            pos = nl + 1;
            if (line.rfind("== ", 0) == 0)
                on = (sel == "all") || line.find(sel) != std::string::npos;
            if (on) std::fprintf(stderr, "%s\n", line.c_str());
        }
    }

    // ── 板子丢掉的东西 ────────────────────────────────
    //
    // 文件中的每个其它支票都读取了所画的 帧 ,
    // 所以他们都看不到 存储单元。
    // 一个剪切的值会留下一个 帧 一周宽度的虫子
    // 如何幸存了这里的绿色套房
    //
    // ASSERTED 不只是报道 因为面板是剪辑
    // -clean 这张支票的第一行发现,
    // 金色的散列物 已经很幸福地被打翻了—— 三个
    // 字幕被砍死在40列没有椭圆的栏目上, 所以句子就停了,
    // 读者没有理由认为一个字不见了。
    //
    // 故意的截断会使NOT 跌倒:椭圆形的标签已经缩短了,
    // 以便适应,所以没有东西到达剪贴板的边缘。
    // 这就是这一说法所依据的区别——它以
    // 丢失的信息为火线。 无需 并且保持安静 信息,
    // 小组选择 缩写。
    if (!g_clipped.empty()) {
        std::fprintf(stderr, "\nstats: %zu clipped write(s) across the sweep\n",
                     g_clipped.size());
        std::size_t shown = 0;
        for (const auto& c : g_clipped) {
            if (shown++ >= 12) {
                std::fprintf(stderr, "  … and %zu more\n",
                             g_clipped.size() - 12);
                break;
            }
            std::fprintf(stderr, "  %s\n", c.c_str());
        }
    }
    check(g_clipped.empty(),
          "no stats text is cut off without an ellipsis");

    // 问的同样问题 LONG 届会。
    //
    // 宽度错误由内容宽度触发, 不是 终端 宽度,
    // 所以每个值都短的固定图( "4 turns",
    // "16s" ——它行使布局最简单的情况,并报告情况,
    // 作为通行。 线程 携带四 -digit 计数和
    // 小时 -scale 持续时间,这是弦实际竞争行的地方.
    //
    // 而不是故意的。 将1200 -turn 固定的 字节
    // 将使得每个不相关的格式改变为 re-pin 的 jore ,
    // 以示没有额外的安全; 值得断言的是财产,
    // 而财产是,没有读者被告知,任何东西都不会丢失。
    g_clipped.clear();
    // HEAVY_DUMP=<width> 以一个宽度打印重修饰,
    // 这是真实会话的形状 : 4 -digit 计数,
    // 小时 -scale 持续时间, 数值宽到可以对行进行竞争。
    if (const char* hw = std::getenv("HEAVY_DUMP")) {
        const int w = std::atoi(hw);
        for (const auto& desc : stats::kTabs) {
            auto [opened, _] = app::update(heavy_model(), Msg{OpenStats{}});
            if (auto* o = opened.ui.panel.get<pn::Stats>()) o->tab = desc.id;
            std::fprintf(stderr, "== %s %dx40 ==\n",
                         std::string{stats::tab_title(desc.id)}.c_str(), w);
            std::fprintf(stderr, "%s", render_at(opened, w, 40).c_str());
        }
    }
    for (const auto& desc : stats::kTabs) {
        for (int w : {40, 60, 76, 90, 120, 200}) {
            auto [opened, _] = app::update(heavy_model(), Msg{OpenStats{}});
            if (auto* o = opened.ui.panel.get<pn::Stats>())
                o->tab = desc.id;
            (void)render_at(opened, w, 40);
        }
    }
    if (!g_clipped.empty()) {
        std::fprintf(stderr, "\nstats (heavy session): %zu clipped write(s)\n",
                     g_clipped.size());
        std::size_t shown = 0;
        for (const auto& c : g_clipped) {
            if (shown++ >= 12) {
                std::fprintf(stderr, "  … and %zu more\n",
                             g_clipped.size() - 12);
                break;
            }
            std::fprintf(stderr, "  %s\n", c.c_str());
        }
    }
    check(g_clipped.empty(),
          "a long session loses no text either");

    // ── 特征断言 ──────────────────────────────────
    //
    // 输出必须进行什么 CONTAIN ,无论准确与否 字节
    // 这些是经过精心研究后存活下来的 -pin : 新的散列
    // 不能悄悄地掉下一节,因为这些仍然必须保持.
    check(out.find("Session") != std::string::npos, "the Session tab is drawn");
    check(out.find("Tokens")  != std::string::npos, "the Tokens tab is drawn");
    check(out.find("Cache")   != std::string::npos, "the Cache tab is drawn");
    check(out.find("claude-sonnet-4-5") != std::string::npos,
          "model names reach the Models tab");
    check(out.find("\xe2\x96\x88") != std::string::npos,
          "bars are drawn (a stats row without its chart is the frozen-track bug)");
    check(out.find("\xe2\x94\x82") != std::string::npos,
          "the panel frame is drawn");

    // 任何行都不得超过在 -- -- 时间 -- -- 所显示的宽度。
    // 帧-overdraw 分类, 也固定在此, 因为这支绳索可以看到每
    // 个标签的大小。 每个块前有一个 "== Tab WxH ==" 标记,
    // 因此,预期宽度在 数据流 自己来
    {
        int over = 0;
        int want = 0;
        std::size_t pos = 0;
        while (pos < out.size()) {
            const std::size_t nl = out.find('\n', pos);
            if (nl == std::string::npos) break;
            const std::string line = out.substr(pos, nl - pos);
            pos = nl + 1;

            if (line.rfind("== ", 0) == 0) {
                // "== Tab 76x40 ==" -- 取空格之间的宽度, 和 'x'.
                const std::size_t x = line.rfind('x');
                const std::size_t sp = line.rfind(' ', x);
                if (x != std::string::npos && sp != std::string::npos)
                    want = std::atoi(line.c_str() + sp + 1);
                continue;
            }
            if (want <= 0) continue;
            // 显示宽度, 不 字节 : 数字是多的 -byte.
            int cells = 0;
            for (std::size_t i = 0; i < line.size(); ) {
                const unsigned char c = static_cast<unsigned char>(line[i]);
                i += (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
                ++cells;
            }
            if (cells > want) ++over;
        }
        check(over == 0, "no row overruns the width it was rendered at");
    }

    // ── 字节 身份标识 ────────────────────────────────────────────────
    //
    // 设置为 0 到 ritrap: 运行打印散列到 stderr,
    // 粘贴到后面 IN 该 相同 COMMIT 因为它的改变 移动它。
    // 移动 2026- 11 : 页脚键条从手-spelled
    // cfg.note 移动到共享的 key_hints 帮助器,
    // 所以它现在像其他面板一样在宽度压力下放出键。
    // 只有页脚线改变了;剪辑扫描、 超时检查和每个特性断言在整个
    // 扫荡期间都保持绿色。
    const std::uint64_t kGoldenHash = 0x024089ffde43e434ull;
    const std::uint64_t got = fnv1a(out);

    if (kGoldenHash == 0) {
        std::fprintf(stderr,
            "GOLDEN stats hash = 0x%016llxull  (len=%zu)\n",
            static_cast<unsigned long long>(got), out.size());
    } else {
        check(got == kGoldenHash,
              "stats rendering is byte-identical to golden");
        if (got != kGoldenHash)
            std::fprintf(stderr,
                "stats golden MISMATCH: got 0x%016llxull want 0x%016llxull\n"
                "  Run ./build/stats_visual <width> <tab> to see the change.\n"
                "  If it is intended, re-pin the hash in the same commit.\n",
                static_cast<unsigned long long>(got),
                static_cast<unsigned long long>(kGoldenHash));
    }
}
