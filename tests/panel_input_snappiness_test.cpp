// 面板输入snappiness + 爆发正确性.
//
// 两个问题,两个问题都问到钥匙到来后会怎样 FASTER 超过
// 帧:
//
//   1. LATENCY 键盘里有键盘吗? -repeat 插槽( ~33 ms at)
//      每个面板上都有30/s, 不只是主题浏览器?
//
//   2. CORRECTNESS 爆炸后,一个快餐 终端 提供完整的密钥 -repeat
//      运行在 一个 read() ,和 maya 发送次数 事件 因此,
//      N箭头必须降落在与N分离完全相同的状态上 帧 一定会的,
//      如果不是的话,那就是 "input race"
//      类: 连结的爆裂, 以单曲结束 -stepped 跑不会的
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/msg.hpp"
#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/panel/appearance.hpp"
#include "agentty/runtime/view/view.hpp"
#include "agentty/runtime/panel/form_keys.hpp"

#include <maya/core/render_context.hpp>
#include <maya/print.hpp>

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace agentty;
using clk = std::chrono::steady_clock;
namespace pn = agentty::ui::panel;

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("  FAIL: " __VA_ARGS__); \
    std::printf("\n"); ++g_fail; } } while (0)

namespace {

Model seeded(int msgs) {
    Model m;
    for (int i = 0; i < msgs; ++i) {
        Message msg;
        msg.role = (i % 2) ? Role::Assistant : Role::User;
        msg.text = "## Heading " + std::to_string(i) +
            "\n\nProse with `code` and *emphasis*, long enough to wrap across "
            "a couple of lines.\n\n```cpp\nint f" + std::to_string(i) +
            "() { return 42; }\n```\n";
        m.d.current.messages.push_back(std::move(msg));
    }
    app::detail::rehydrate_frozen(m);

    // Model 员额 -paint 修剪,或测量用户从未对它
    // 进行打字。将种子补水,宽的窗口恢复 线程 真实 回滚缓冲区 ;
    // 第一个 帧 画出来,然后 meta.cpp 手臂的
    // 剪切到 frozen_row_budget ()
    // 一旦每个街区都有记录的高度。 渲染 编号 SMALL 帆布,
    // 这就是这个测试的目的。
    //
    // 没有头,没有油漆,所以记录高度 渲染器 将拒绝
    // 投下一个从未记录其高度的区块,
    // scrollback_ledger.hpp 其可推广性门.
    for (std::size_t k = 0; k < m.ui.frozen.size(); ++k)
        m.ui.frozen.record_paint(k, static_cast<int>(m.ui.frozen.block_rows(k)));
    (void)app::detail::trim_frozen_if_oversized(m);
    return m;
}

void paint(const Model& m) {
    maya::RenderContext ctx{100, 40, maya::render_generation(), true};
    maya::RenderContextGuard g(ctx);
    auto el = ui::view(m);
    (void)maya::render_to_string(el, 100);
}

// ONE键的成本:减少+批次后的涂料.
double key_ms(Model& m, const Msg& key) {
    auto t0 = clk::now();
    m = app::update(std::move(m), Msg{key}).first;
    paint(m);
    return std::chrono::duration<double, std::milli>(clk::now() - t0).count();
}

const char* verdict(double ms, double budget) {
    return ms <= budget ? "snappy" : "SLOW";
}

}  // 命名空间结束。

int main() {
    app::install_deps(app::Deps{
        .stream        = [](const maya::unlocked&, provider::Request, provider::EventSink) {},
        .save_thread   = [](const Thread&) {},
        .delete_thread = [](const ThreadId&) {},
        .load_threads  = [] { return std::vector<Thread>{}; },
        .load_thread   = [](const ThreadId&) { return std::optional<Thread>{}; },
        .load_settings = [] { return store::Settings{}; },
        .save_settings = [](const store::Settings&) {},
        .new_thread_id = [] { return ThreadId{"t-snap"}; },
        .title_from    = [](std::string_view) { return std::string{"t"}; },
    });

    constexpr double kBudget = 33.3;   // 30/s时段的1个关键值-repeat
    const int msgs = 800;              // 长会
    std::printf("transcript=%d msgs   budget=%.1f ms/key (30/s key repeat)\n\n",
                msgs, kBudget);

    // ── 1. 人数 -panel 延迟 ────────────────────────────────────────────
    std::printf("%-26s %10s  %s\n", "panel / action", "ms/key", "verdict");

    {   // 主题浏览器:每动一个有615行和活的Re-seal的浏览器.
        Model m = seeded(msgs);
        m = app::update(std::move(m), Msg{OpenAppearance{}}).first;
        m = app::update(std::move(m), Msg{AppearancePickTheme{}}).first;
        for (int i = 0; i < 5; ++i) (void)key_ms(m, Msg{AppearanceThemeMove{+1}});
        double worst = 0;
        for (int i = 0; i < 40; ++i)
            worst = std::max(worst, key_ms(m, Msg{AppearanceThemeMove{+1}}));
        std::printf("%-26s %10.2f  %s\n", "theme browser  ↓", worst, verdict(worst, kBudget));
        CHECK(worst <= kBudget, "theme browser %.1f ms > %.1f budget", worst, kBudget);
    }

    {   // 在主题过滤器中打字:re-filters每键键盘615个名称.
        Model m = seeded(msgs);
        m = app::update(std::move(m), Msg{OpenAppearance{}}).first;
        m = app::update(std::move(m), Msg{AppearancePickTheme{}}).first;
        double worst = 0;
        for (const char* c : {"d","r","a","c","u","l","a"}) {
            AppearanceThemeQuery q; q.text = c;
            worst = std::max(worst, key_ms(m, Msg{q}));
        }
        std::printf("%-26s %10.2f  %s\n", "theme filter   type", worst, verdict(worst, kBudget));
        CHECK(worst <= kBudget, "theme filter %.1f ms > %.1f budget", worst, kBudget);
    }

    {   // 外观FORM本身(row nailing, no 浏览器).
        Model m = seeded(msgs);
        m = app::update(std::move(m), Msg{OpenAppearance{}}).first;
        double worst = 0;
        for (int i = 0; i < 20; ++i) {
            AppearanceKey k;
            k.action = form::keys::Action{form::keys::Intent::MoveNext, 0};
            worst = std::max(worst, key_ms(m, Msg{k}));
        }
        std::printf("%-26s %10.2f  %s\n", "appearance form \u2193", worst, verdict(worst, kBudget));
        CHECK(worst <= kBudget, "appearance form %.1f ms > %.1f budget", worst, kBudget);
    }

    // 电话 LANGUAGE 排成一行,循环而不是通过。
    //
    // 这是最重的一排 面板 并且是唯一一个成本 以
    // 其他东西 建造它 调用 completeness()
    // 每种语言,每个语言都行英语 目录。
    // 即每块20步的窗体, 窗体重建在每块键盘上。
    // 面板.
    //
    // 以3.8美元衡量,我们全部为33毫秒预算的0.02%,这就是为什么它
    // 作为一个直径而不是一个 缓存。本案在此,所以真实性不变:未来身份
    // -count 数以百计,或 completeness() 慢了点,
    // 就在这里出现一个数字,而不是 "the settings pane feels sticky"
    // 在一个错误报告。
    //
    // ALSO 应用的行 LIVE。每个周期互换 目录
    // 指针和在新语言中重建形式,因此这可以测量整个切换路径,
    // 而不仅仅是行.
    {
        Model m = seeded(msgs);
        m = app::update(std::move(m), Msg{OpenAppearance{}}).first;
        {
            AppearanceKey k;
            k.action = form::keys::Action{form::keys::Intent::MoveFirst, 0};
            m = app::update(std::move(m), Msg{k}).first;
        }
        double worst = 0;
        for (int i = 0; i < 20; ++i) {
            AppearanceKey k;
            // - > 在选择行上:实际的姿态
            // SWITCHES 语言,所以每个迭代互换
            // 目录 指针并重建新语言中的窗体。
            k.action = form::keys::Action{form::keys::Intent::AdjustUp, 0};
            worst = std::max(worst, key_ms(m, Msg{k}));
        }
        std::printf("%-26s %10.2f  %s\n", "language row cycle",
                    worst, verdict(worst, kBudget));
        CHECK(worst <= kBudget, "language row %.1f ms > %.1f budget",
              worst, kBudget);
    }

    // 其它面板中, 用户会持有箭头。 预算相同, 原因相同 :
    // 每一个 重绘 记录在后面,所以慢的会出现在这里,
    // 而不是出现在错误报告。
    {
        Model m = seeded(msgs);
        m = app::update(std::move(m), Msg{OpenPalette{}}).first;
        double worst = 0;
        for (int i = 0; i < 20; ++i)
            worst = std::max(worst, key_ms(m, Msg{PaletteMove{+1}}));
        std::printf("%-26s %10.2f  %s\n", "palette        \u2193", worst, verdict(worst, kBudget));
        CHECK(worst <= kBudget, "palette %.1f ms > %.1f budget", worst, kBudget);
    }
    {
        Model m = seeded(msgs);
        m = app::update(std::move(m), Msg{OpenThreadList{}}).first;
        double worst = 0;
        for (int i = 0; i < 20; ++i)
            worst = std::max(worst, key_ms(m, Msg{ThreadListMove{+1}}));
        std::printf("%-26s %10.2f  %s\n", "thread list    \u2193", worst, verdict(worst, kBudget));
        CHECK(worst <= kBudget, "thread list %.1f ms > %.1f budget", worst, kBudget);
    }
    {
        Model m = seeded(msgs);
        m = app::update(std::move(m),
                        Msg{OpenSettingsList{settings::Category::General}}).first;
        double worst = 0;
        for (int i = 0; i < 20; ++i)
            worst = std::max(worst, key_ms(m, Msg{SettingsListMove{+1}}));
        std::printf("%-26s %10.2f  %s\n", "settings list  \u2193", worst, verdict(worst, kBudget));
        CHECK(worst <= kBudget, "settings list %.1f ms > %.1f budget", worst, kBudget);
    }
    // 模型( 模型) 选择面板 :面板用户报告为 laggy。
    // 它将每个认证提供者的 目录 ,所以两者 状态更新函数 其
    // 观点是: 选择面板.
    {
        Model m = seeded(msgs);
        auto t0 = clk::now();
        m = app::update(std::move(m), Msg{OpenModels{}}).first;
        paint(m);
        const double open_ms = std::chrono::duration<double, std::milli>(clk::now() - t0).count();
        double worst = 0;
        for (int i = 0; i < 20; ++i)
            worst = std::max(worst, key_ms(m, Msg{ModelsMove{+1}}));
        double type_worst = 0;
        for (char32_t c : {U'c', U'l', U'a', U'u'})
            type_worst = std::max(type_worst, key_ms(m, Msg{ModelsFilterInput{c}}));
        std::printf("%-26s %10.2f  %s   (open %.1f ms, %zu rows)\n", "model picker   \u2193",
                    worst, verdict(worst, kBudget), open_ms, m.d.fused_rows.size());
        std::printf("%-26s %10.2f  %s\n", "model filter   type", type_worst, verdict(type_worst, kBudget));
        CHECK(worst <= kBudget, "model picker %.1f ms > %.1f budget", worst, kBudget);
        CHECK(type_worst <= kBudget, "model filter %.1f ms > %.1f budget", type_worst, kBudget);
    }

    // ── 2. 伯斯特单曲 -step (无) 竞态 检查) ────────────────────────
    //
    // 斋戒 终端 提供密钥 -repeat 运行在 一个 读,
    // 和 maya 发送次数 事件 20支箭作为爆破的箭
    // 一定降落在20支箭的地方 -per-frame 陆地。
    // 其它任何意味着着陆点取决于终端的时点.
    std::printf("\nburst vs single-step (20 \u2193):\n");
    {
        const int kN = 20;

        Model burst = seeded(msgs);
        burst = app::update(std::move(burst), Msg{OpenAppearance{}}).first;
        burst = app::update(std::move(burst), Msg{AppearancePickTheme{}}).first;
        for (int i = 0; i < kN; ++i)   // 全部( E) 事件, THEN 一张油漆
            burst = app::update(std::move(burst), Msg{AppearanceThemeMove{+1}}).first;
        paint(burst);

        Model step = seeded(msgs);
        step = app::update(std::move(step), Msg{OpenAppearance{}}).first;
        step = app::update(std::move(step), Msg{AppearancePickTheme{}}).first;
        for (int i = 0; i < kN; ++i) { // 彩绘在每张相间 事件
            step = app::update(std::move(step), Msg{AppearanceThemeMove{+1}}).first;
            paint(step);
        }

        const auto* ob = burst.ui.panel.get<pn::Appearance>();
        const auto* os = step.ui.panel.get<pn::Appearance>();
        std::printf("  index  burst=%d  stepped=%d\n",
                    ob ? ob->pane.picker.picker.index() : -1,
                    os ? os->pane.picker.picker.index() : -1);
        std::printf("  theme  burst=%s  stepped=%s\n",
                    burst.d.ui().theme.c_str(), step.d.ui().theme.c_str());
        CHECK(ob && os && ob->pane.picker.picker.index() == os->pane.picker.picker.index(),
              "burst landed on a different row than single-stepping");
        CHECK(burst.d.ui().theme == step.d.ui().theme,
              "burst landed on a different THEME than single-stepping");
    }

    // 包装也必须是时间-independent:615个主题,走过结尾.
    {
        Model a = seeded(8), b = seeded(8);
        for (Model* p : {&a, &b}) {
            *p = app::update(std::move(*p), Msg{OpenAppearance{}}).first;
            *p = app::update(std::move(*p), Msg{AppearancePickTheme{}}).first;
        }
        const int kWrap = 620;   // 过列表末尾
        for (int i = 0; i < kWrap; ++i)
            a = app::update(std::move(a), Msg{AppearanceThemeMove{+1}}).first;
        for (int i = 0; i < kWrap; ++i) {
            b = app::update(std::move(b), Msg{AppearanceThemeMove{+1}}).first;
            paint(b);
        }
        const auto* oa = a.ui.panel.get<pn::Appearance>();
        const auto* ob = b.ui.panel.get<pn::Appearance>();
        std::printf("  wrap(%d) burst=%d stepped=%d\n", kWrap,
                    oa ? oa->pane.picker.picker.index() : -1, ob ? ob->pane.picker.picker.index() : -1);
        CHECK(oa && ob && oa->pane.picker.picker.index() == ob->pane.picker.picker.index(),
              "wrapping past the end depends on paint timing");
        CHECK(a.d.ui().theme == b.d.ui().theme, "wrap landed on a different theme");
    }

    // ── 3. 路线 竞态 : 批次, 即: CHANGES 哪个面板拥有密钥 ──
    //
    // 碞琌硂 状态更新函数-level 测试无法看到。
    // maya 每一个键通过 事件订阅 建自该 当前 模型,
    // 一个快 终端 交付 "Enter ↓ ↓ ↓"。
    // 如果循环重用预览 -Enter 事件订阅 对于箭头,
    // 它们会沿着下面的窗体(移动它的光标),而不是浏览器,
    // Enter 刚刚打开的,一个显着无所作为的按键。
    //
    // maya 内容 -fetches 每一个排水消息之后的子,正因为这个原因( app.hpp:
    // 必须在事件之间重新路由，不能只在读取批次之间路由。
    // Model在此:面板在开封电文之后
    // 必须说明以下电文的对应解释。
    std::printf("\nrouting across a panel change (open + 3 \u2193 in one batch):\n");
    {
        Model m = seeded(64);
        m = app::update(std::move(m), Msg{OpenAppearance{}}).first;

        // Enter 主题行打开浏览器——面板 -changing
        // 事件 之后的一切,在同一读数中,必须降落在 BROWSER
        // 而不是表单。
        m = app::update(std::move(m), Msg{AppearancePickTheme{}}).first;
        const auto* opened = m.ui.panel.get<pn::Appearance>();
        CHECK(opened && opened->pane.picking,
              "Enter did not open the browser");
        const int form_cursor_before = opened ? opened->pane.form.cursor : -1;

        for (int i = 0; i < 3; ++i)
            m = app::update(std::move(m), Msg{AppearanceThemeMove{+1}}).first;

        const auto* o = m.ui.panel.get<pn::Appearance>();
        std::printf("  browser index=%d   form cursor %d\u2192%d\n",
                    o ? o->pane.picker.picker.index() : -1,
                    form_cursor_before, o ? o->pane.form.cursor : -1);
        CHECK(o && o->pane.picker.picker.index() == 3,
              "arrows after the opening key did not reach the browser");
        CHECK(o && o->pane.form.cursor == form_cursor_before,
              "arrows leaked into the form behind the browser");
    }

    std::printf("\n%s\n", g_fail == 0 ? "panel_input_snappiness: OK"
                                      : "panel_input_snappiness: FAILURES");
    return g_fail == 0 ? 0 : 1;
}
