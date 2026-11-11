// 外观设置: 存在行, Enter 改变它们,
// 改变既持续,也到达 渲染器.
#include "agtest.hpp"
#include "agtest_fx.hpp"

#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/app/update/internal.hpp"   // persist_settings
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/panel/settings/items.hpp"
#include "agentty/runtime/panel/form_keys.hpp"
#include "agentty/domain/ui_theme.hpp"
#include "agentty/domain/ui_live.hpp"
#include "agentty/runtime/view/palette.hpp"
#include <maya/style/schemes.hpp>
#include "agentty/runtime/view/thread/seam.hpp"
#include <maya/core/motion.hpp>
#include "agentty/runtime/panel/appearance.hpp"
#include "agentty/runtime/view/panels.hpp"

#include <maya/core/render_context.hpp>
#include <maya/print.hpp>

#include <cstdio>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace agentty;
namespace se = agentty::settings;

namespace {

// 住进的设置存储器 内存 ,所以测试可以断言一行按
// 实际 WROTE 而不是仅仅改变 手头的模型。
store::Settings g_saved;

void install_stub_deps() {
    g_saved = store::Settings{};
    agentty::app::install_deps(agentty::app::Deps{
        .stream        = [](const maya::unlocked&, provider::Request, provider::EventSink) {},
        .save_thread   = [](const agentty::Thread&) {},
        .delete_thread = [](const agentty::ThreadId&) {},
        .load_threads  = [] { return std::vector<agentty::Thread>{}; },
        .load_thread   = [](const ThreadId&) { return std::optional<agentty::Thread>{}; },
        .load_settings = [] { return g_saved; },
        .save_settings = [](const store::Settings& s) { g_saved = s; },
        .new_thread_id = [] { return ThreadId{"t"}; },
        .title_from    = [](std::string_view t) { return std::string{t}; },
        .auth          = {},
    });
}

// 应用任何副作用 步骤返回到 stub 商店。
//
// 保存是一个 EFFECT 现在: 状态更新函数 返回描述
// 写作和写的值 HOST 测试结果 `g_saved`,
// i.e .关于商店的到来,所以他们必须扮演主持人的角色——
// 这也是重点:a 保存 只有手臂真的还回来时 才会出现在这里
void apply_fx(const Cmd& c) {
    agtest::fx::Store s;
    s.settings = g_saved;
    agtest::fx::run(c, s);
    g_saved = s.settings;
}

// 打开"外观面板",将模型拿回来.
Model opened() {
    auto [m, cmd] = app::update(Model{}, Msg{OpenAppearance{}});
    apply_fx(cmd);
    return std::move(m);
}

ui::panel::Appearance& pane(Model& m) {
    auto* o = m.ui.panel.get<ui::panel::Appearance>();
    REQUIRE(o != nullptr);
    return *o;
}

// 将光标放在行上 `id` 并发送一个关键动作,
// 恰好是按键键。 处理行 BY ID 而不是指数 和
// 本学科一样 状态更新函数 使用——每次进行数行断裂
// 的测试 面板 生长一节,无实故也.
Model press(Model m, std::string_view id, form::keys::Action a) {
    auto& f = pane(m).pane.form;
    int idx = -1;
    for (int i = 0; i < (int)f.fields.size(); ++i)
        if (f.fields[(size_t)i].id == id) { idx = i; break; }
    REQUIRE(idx >= 0);
    f.cursor = idx;
    auto [next, cmd] = app::update(std::move(m), Msg{AppearanceKey{a}});
    apply_fx(cmd);
    return std::move(next);
}

}  // 命名空间结束。

TEST_CASE("appearance: the pane opens on a setting, not a header") {
    install_stub_deps();
    Model m = opened();
    const auto& f = pane(m).pane.form;
    REQUIRE(!f.fields.empty());
    // 区域上停放的光标 头文件 读作破损 面板 - 你
    // 按的第一件事 什么也没做。
    CHECK(!f.fields[(size_t)f.cursor].is_header());
}

TEST_CASE("appearance: adjusting in place wraps") {
    install_stub_deps();
    Model m = opened();
    CHECK(m.d.ui().density == ui_prefs::Density::Normal);
    m = press(std::move(m), ui::panel::kApDensity, form::keys::Action{form::keys::Intent::AdjustUp});
    CHECK(m.d.ui().density == ui_prefs::Density::Roomy);
    m = press(std::move(m), ui::panel::kApDensity, form::keys::Action{form::keys::Intent::AdjustUp});
    CHECK(m.d.ui().density == ui_prefs::Density::Compact);
    m = press(std::move(m), ui::panel::kApDensity, form::keys::Action{form::keys::Intent::AdjustUp});
    CHECK(m.d.ui().density == ui_prefs::Density::Normal);   // 包起来
}

TEST_CASE("appearance: a toggle is a toggle") {
    install_stub_deps();
    Model m = opened();
    const bool was = m.d.ui().syntax;
    m = press(std::move(m), ui::panel::kApSyntax, form::keys::Action{form::keys::Intent::Activate});
    CHECK(m.d.ui().syntax == !was);
    m = press(std::move(m), ui::panel::kApSyntax, form::keys::Action{form::keys::Intent::Activate});
    CHECK(m.d.ui().syntax == was);
}

TEST_CASE("appearance: a change is persisted, not just held") {
    install_stub_deps();
    Model m = opened();
    m = press(std::move(m), ui::panel::kApMotion, form::keys::Action{form::keys::Intent::AdjustUp});
    // 电话 面板 没有应用步骤,所以 "what I see" 财务报告和已审计
    // 财务报表 "what is saved" 决不能是两件不同的事情——即使是按键。
    CHECK(g_saved.ui.motion == m.d.ui().motion);
    CHECK(m.d.ui().motion != ui_prefs::Motion::Full);
}

// 电话 回滚缓冲区 列选择重放打印的行数。自动(默认)是 终端 长度;
// AGENTTY_REPLAY_ROWS 击败每关; tmux 的
// 历史 -limit 盖住他们。
TEST_CASE("appearance: scrollback sets the replay row cap") {
    install_stub_deps();
    Model m = opened();
    CHECK(m.d.ui().scrollback == ui_prefs::Scrollback::Auto);

    // 探测器前自动:env-only猜,否则安全5k.
    m.env.auto_scrollback_rows = 0;
    CHECK(app::detail::scrollback_replay_limits(m).first == 5'000);
    m.env.auto_scrollback_rows = 2'000;        // e.g . 小猫的默认
    CHECK(app::detail::scrollback_replay_limits(m).first == 2'000);
    // 启动探测器的答案( 它读作kitty.conf): 获胜。
    m = app::update(std::move(m), Msg{TerminalScrollbackProbed{7'654, false, true, "kitty.conf"}}).first;
    CHECK(app::detail::scrollback_replay_limits(m).first == 7'654);
    m = app::update(std::move(m), Msg{TerminalScrollbackProbed{0, true, true, "kitty.conf"}}).first;
    CHECK(app::detail::scrollback_replay_limits(m).first == std::numeric_limits<int>::max());
    // 探测器什么也学不到 猜想就由我们来负责
    m = app::update(std::move(m), Msg{TerminalScrollbackProbed{0, false, false, ""}}).first;
    CHECK(app::detail::scrollback_replay_limits(m).first == 2'000);

    // 固定水平忽略了终端.
    m.d.ui().scrollback = ui_prefs::Scrollback::Small;
    CHECK(app::detail::scrollback_replay_limits(m).first == 5'000);
    {
        // 打开小号上的面板, 排从那里开始; 上方是大号。
        Model start;
        start.d.ui().scrollback = ui_prefs::Scrollback::Small;
        Model p = app::update(std::move(start), Msg{OpenAppearance{}}).first;
        p = press(std::move(p), ui::panel::kApScrollback,
                  form::keys::Action{form::keys::Intent::AdjustUp});
        CHECK(p.d.ui().scrollback == ui_prefs::Scrollback::Big);
        CHECK(g_saved.ui.scrollback == ui_prefs::Scrollback::Big);
        CHECK(app::detail::scrollback_replay_limits(p).first == 20'000);
    }
    m.d.ui().scrollback = ui_prefs::Scrollback::Full;
    CHECK(app::detail::scrollback_replay_limits(m).first == std::numeric_limits<int>::max());
    m.d.ui().scrollback = ui_prefs::Scrollback::Tiny;
    CHECK(app::detail::scrollback_replay_limits(m).first == 1'000);

    // tmux 的行数已经超过它的-limit,所以永远不要打印了.
    m.d.ui().scrollback = ui_prefs::Scrollback::Full;
    m.env.terminal_history_rows = 2'000;
    CHECK(app::detail::scrollback_replay_limits(m).first == 2'000);

    // 内幕覆盖器会战胜每个级别
    m.env.replay_rows_override = 0;
    CHECK(app::detail::scrollback_replay_limits(m).first == 0);
    m.env.replay_rows_override = 500;
    CHECK(app::detail::scrollback_replay_limits(m).first == 500);
}

// 行列出自动, 而不是 枚举 顺序: 选择必须存储
// 显示的级别, 而不是该级别 枚举 索引。
TEST_CASE("appearance: scrollback choices map to the level shown") {
    install_stub_deps();
    Model m = opened();
    const ui_prefs::Scrollback order[] = {
        ui_prefs::Scrollback::Auto, ui_prefs::Scrollback::Tiny, ui_prefs::Scrollback::Small,
        ui_prefs::Scrollback::Big, ui_prefs::Scrollback::Full, ui_prefs::Scrollback::Auto};
    for (int i = 1; i < 6; ++i) {
        m = press(std::move(m), ui::panel::kApScrollback,
                  form::keys::Action{form::keys::Intent::AdjustUp});
        CHECK(m.d.ui().scrollback == order[i]);
    }
}

TEST_CASE("appearance: the theme row hands off to a browser, not a dropdown") {
    install_stub_deps();
    Model m = opened();
    CHECK(!pane(m).pane.picking);
    m = press(std::move(m), ui::panel::kApTheme, form::keys::Action{form::keys::Intent::Activate});
    CHECK(pane(m).pane.picking);
}

TEST_CASE("appearance: moving in the browser previews the theme live") {
    install_stub_deps();
    Model m = opened();
    CHECK(m.d.ui().theme.empty());              // 默认本地
    m = press(std::move(m), ui::panel::kApTheme, form::keys::Action{form::keys::Intent::Activate});

    // 移动 APPLIES - 列表是它自己的预览,所以你判断一个方案
    // 在真实的 UI 而不是在它的名字上。
    {
        auto [n, fxc] = app::update(std::move(m), Msg{AppearanceThemeMove{+1}});
        apply_fx(fxc);
        m = std::move(n);
    }
    CHECK(!m.d.ui().theme.empty());

    // Esc 是个真人 取消 :它让我们在模型
    // 中重新打开 和 光是光线就一无所有了
    {
        auto [n, fxc] = app::update(std::move(m), Msg{AppearanceThemeCancel{}});
        apply_fx(fxc);
        m = std::move(n);
    }
    CHECK(m.d.ui().theme.empty());
    CHECK(g_saved.ui.theme.empty());
    CHECK(!pane(m).pane.picking);
}

TEST_CASE("appearance: Enter in the browser keeps what you are looking at") {
    install_stub_deps();
    Model m = opened();
    m = press(std::move(m), ui::panel::kApTheme, form::keys::Action{form::keys::Intent::Activate});
    {
        auto [n, fxc] = app::update(std::move(m), Msg{AppearanceThemeMove{+1}});
        apply_fx(fxc);
        m = std::move(n);
    }
    const std::string previewing = m.d.ui().theme;
    {
        auto [n, fxc] = app::update(std::move(m), Msg{AppearanceThemeCommit{}});
        apply_fx(fxc);
        m = std::move(n);
    }
    CHECK(m.d.ui().theme == previewing);
    CHECK(g_saved.ui.theme == previewing);
    CHECK(!pane(m).pane.picking);
}

TEST_CASE("appearance: typing filters and previews the top match") {
    install_stub_deps();
    Model m = opened();
    m = press(std::move(m), ui::panel::kApTheme, form::keys::Action{form::keys::Intent::Activate});
    for (const char* c : {"d", "r", "a"}) {
        auto [n, fxc] = app::update(std::move(m), Msg{AppearanceThemeQuery{c}});
        apply_fx(fxc);
        m = std::move(n);
    }
    CHECK(pane(m).pane.picker.picker.query() == "dra");
    // 缩小 PREVIEWS 也... 也别忘了... "dra" 让你看看德古拉
    // 没有第二个键盘可以移动到它上.
    CHECK(!m.d.ui().theme.empty());
}

TEST_CASE("appearance: resolution never leaves the user unable to read") {
    // 一个方案需要256种颜色才能值得绘画;低于这个原生的—
    // —用户自己的调色板——严格来说比一个量化的近似值更好,
    // 所以resolve()必须倒回去而不是服从.
    ui_prefs::Prefs p;
    p.theme = "Dracula";
    p.tier  = ui_prefs::ColorTier::TrueColor;
    CHECK(ui_prefs::resolve(p, agentty::ui_prefs::detect(true)).theme != &maya::theme::native);
    p.tier  = ui_prefs::ColorTier::Ansi16;
    CHECK(ui_prefs::resolve(p, agentty::ui_prefs::detect(true)).theme == &maya::theme::native);
    // 未知名称是配置类型,不是拒绝开始的理由.
    p.theme = "Nope";
    p.tier  = ui_prefs::ColorTier::TrueColor;
    CHECK(ui_prefs::resolve(p, agentty::ui_prefs::detect(true)).theme == &maya::theme::native);
}


TEST_CASE("appearance form: rows, groups and provenance") {
    install_stub_deps();
    ui_prefs::Prefs p;
    const auto f = ui::panel::build_appearance_form(p, ui_prefs::detect(true));

    // 每个把手都有排,还有 面板 被组合起来,而不是平面墙。
    CHECK(f.fields.size() >= 11);
    int headers = 0;
    for (const auto& r : f.fields)
        if (std::holds_alternative<form::field::Header>(r.value)) ++headers;
    CHECK(headers >= 4);

    // 主题行领先,是Pick——57种方案是可搜索的集,
    // 而不是滴水.
    bool found_pick = false;
    for (const auto& r : f.fields)
        if (r.id == ui::panel::kApTheme)
            found_pick = std::holds_alternative<form::field::Pick>(r.value);
    CHECK(found_pick);
}

TEST_CASE("appearance: theme search is a fuzzy subsequence") {
    using ui::panel::matching_themes;
    // 空查询是一个可浏览目录, 本地先。
    const auto all = matching_themes("");
    CHECK(all.size() > 20);
    CHECK(all.front().empty());              // 本地语言

    // 子序,而非子弦:一个模糊的选择面板的点.
    const auto gvd = matching_themes("gvd");
    bool has_gruvbox_dark = false;
    for (const auto& n : gvd) if (n == "Gruvbox Dark") has_gruvbox_dark = true;
    CHECK(has_gruvbox_dark);

    // 大小写 -insensitive.
    CHECK(!matching_themes("DRACULA").empty());
    // 查询意味着一个方案不会拖动本地程序。
    const auto dr = matching_themes("dracula");
    CHECK(!dr.empty());
    CHECK(!dr.front().empty());
}

// ── 其实是小把手 DO 东西 ─────────────────────────────────
//
// 设置面板, 其行不变, 但变化并不比面板更糟糕: 这是对
// 您的许诺 UI 安静地打破。 SEAM ——一个已出版的
// 预告达到读它时的代码——而不是重新读到它.
// -testing 每一个消费者自己 渲染 ,它自己的测试覆盖。

TEST_CASE("appearance: publishing a pref reaches its consumers") {
    ui_prefs::Prefs p;

    p.density = ui_prefs::Density::Compact;
    ui_prefs::publish(p);
    CHECK(ui_prefs::panel_rows() == 10);
    p.density = ui_prefs::Density::Roomy;
    ui_prefs::publish(p);
    CHECK(ui_prefs::panel_rows() == 22);

    // 动议是两个问题,不是问题之一: 任何事物是否在移动,
    // 以及是否 DECORATIVE 层移动。减少后保持渐进式
    // 逐步显示 (进步是信息) 并放下甘油的churn。
    p.motion = ui_prefs::Motion::Full;
    ui_prefs::publish(p);
    CHECK(ui_prefs::animations_on());
    CHECK(ui_prefs::reveal_decoration_on());

    p.motion = ui_prefs::Motion::Reduced;
    ui_prefs::publish(p);
    CHECK(ui_prefs::animations_on());
    CHECK(!ui_prefs::reveal_decoration_on());

    p.motion = ui_prefs::Motion::Off;
    ui_prefs::publish(p);
    CHECK(!ui_prefs::animations_on());
    CHECK(!ui_prefs::reveal_decoration_on());

    ui_prefs::publish(ui_prefs::Prefs{});   // 离开空档干净
}

TEST_CASE("appearance: reduce-motion freezes maya's animation primitives") {
    // 大门位于第maya动画堆的BOTTOM,
    // 每台spinner/blink/frame计数器下,
    // 所以第"nothing moves"是从未听说过这种设置的部件——包括后来写的部件——的真谛.
    maya::anim::set_reduce_motion(false);
    CHECK(!maya::anim::reduce_motion());

    maya::anim::set_reduce_motion(true);
    CHECK(maya::anim::reduce_motion());
    // 帧 0是每艘帧定型的maya舰的休眠格莱弗,
    // 因此一个冷冻的加载动画显示的是稳定的标记,
    // 而不是在关闭时时恰好在哪个格莱弗上.
    CHECK(maya::anim::frame_index(10, 90.0, /*请求=*/false) == 0u);
    // A 光标 举行 ON : 粘贴 OFF 光标会完全丢失,
    // 这将是比不眨眼更糟糕的结果。
    CHECK(maya::anim::blink(530.0));
    // A "breathing" 持有的数值 MIDDLE 冷冻在槽里,
    // 它会读作 渲染 虫子,你这混蛋
    CHECK(maya::anim::wave(1400.0) == doctest::Approx(0.5));

    maya::anim::set_reduce_motion(false);   // 保存进程干净
}

TEST_CASE("appearance: thread preview turns the ^J preview off") {
    install_stub_deps();
    Model m = opened();
    CHECK(m.d.ui().thread_preview);
    m = press(std::move(m), ui::panel::kApThreadPeek,
              form::keys::Action{form::keys::Intent::AdjustUp});
    CHECK_FALSE(m.d.ui().thread_preview);
    CHECK_FALSE(g_saved.ui.thread_preview);   // 并且它曾经是 已保存

    // 关机了 ^J 没有绘制预览, 并且 加载 排队没什么
    Thread t;
    t.id = ThreadId{"t-1"};
    t.title = "some thread";
    t.message_count = 4;
    m.d.threads.push_back(t);
    auto [open, cmd] = app::update(std::move(m), Msg{OpenThreadList{}});
    // 排行没有安排:没有定时器,没有 加载 飞行中。
    int timers = 0;
    agtest::fx::for_each(cmd, [&](const auto& e) {
        if constexpr (requires { e.delay; }) ++timers;
    });
    CHECK(timers == 0);
    CHECK(open.ui.peek_loading.empty());
    maya::RenderContext ctx{100, 40, maya::render_generation(), true};
    maya::RenderContextGuard g(ctx);
    const std::string screen = maya::render_to_string(ui::thread_list_panel(open), 100);
    CHECK(screen.find("Preview") == std::string::npos);
    CHECK(screen.find("some thread") != std::string::npos);
}

TEST_CASE("appearance: compact turns changes the seam's height AND its rows") {
    // 冻 回滚缓冲区 将每个元素都用明确的行数封存;
    // 如果该元素和该元素不同意分类账漂移和画布泪水。
    // 因此,这两个元素必须一起移动—— 组合是全部原因。
    // gap_rows ()是函数而非常数.
    ui_prefs::Prefs p;

    p.compact_turns = false;
    ui_prefs::publish(p);
    CHECK(ui::gap_rows() == 3);
    CHECK(maya::render_to_string(ui::gap_row(), 40).find('\n') != std::string::npos);

    p.compact_turns = true;
    ui_prefs::publish(p);
    CHECK(ui::gap_rows() == 1);

    ui_prefs::publish(ui_prefs::Prefs{});
}



TEST_CASE("appearance: the palette follows the chosen theme") {
    // 该 返回此文件存在。 每种颜色都包含 agentty
    // 曾经是 `inline constexpr` ANSI
    // 字面意思,所以主题浏览器是谎言: 你可以选择德古拉,
    // 观看名称的改变,而不是一个 存储单元 显示在屏幕上。
    // 符号是主题读到的,这个表示它的唯一方法就是捕获原始错误——
    // 比较符号 RESOLVE TO 跨主题变化。

    // 土著: 终端 颜色,没有文字,所以 `fg` RESOLVES
    // 编号 终端 默认, 而不是我们为用户选择的颜色。
    ui_prefs::publish_theme(maya::theme::native);
    const maya::LitColor native_fg     = ui::fg();
    const maya::LitColor native_accent = ui::accent();
    CHECK(native_fg.kind() == maya::LitColor::Kind::Default);

    // 现在的图谋是用文字 RGB 相同标志,不同颜色。
    const maya::Theme* dracula = nullptr;
    for (const auto& s : maya::theme::schemes)
        if (std::string_view{s.name} == "Dracula") dracula = s.theme;
    REQUIRE(dracula != nullptr);

    ui_prefs::publish_theme(*dracula);
    // 调用 明确符号: 槽转换为两个 LitColor 颜色,所以赤裸的
    // `ui::fg != x` 意思是说不清的 `ui::fg()` 给读数起名
    CHECK(ui::fg() != native_fg);
    CHECK(ui::accent() != native_accent);
    CHECK(ui::fg().kind() == maya::LitColor::Kind::Rgb);

    // 颜色转换保留 SYMBOLIC - 这是晚 -binding
    // 不变量 (docs/LATE_BINDING.md。
    // 投入样式的符号必须仍然指定一个位置,这样下定决心就发生于
    // PAINT ,这就是让存储的元素遵循后继主题切换。在此将使用
    // Rgb 来维护错误。
    CHECK(maya::Color{ui::fg}.kind() == maya::ColorKind::Slot);

    // 每一个标志都追踪到 相同 主题——不仅仅是我们所看到的。
    // 与RESOLVED值相比,因为这是一个主题字段所持有的值。
    // 实际操作的是这个方案 及其对比的底线。
    const maya::Theme fd = maya::theme::with_contrast_floor(*dracula);
    CHECK(ui::danger()    == fd.error);
    CHECK(ui::success()   == fd.success);
    CHECK(ui::muted()     == fd.muted);
    CHECK(ui::highlight() == fd.primary);
    CHECK(ui::code_path() == fd.link);

    // 回到原生,所以其他套房的油漆都按其预期.
    ui_prefs::publish_theme(maya::theme::native);
    CHECK(ui::fg() == native_fg);
}

TEST_CASE("appearance: publishing a theme reaches the PROJECTED palettes too") {
    // 同样的回归的后半部分, 以及实际运出
    // 的破损。
    //
    // 有两个主题汇。 agentty 原子输入 `ui::` 符号,
    // 在读时 BUILDING a 元素。 maya's app_set_theme
    // 输入 RENDERER ——并通过 on_theme_changed ,
    // 每个调色板是 PROJECTED 而不是从主题读取 -frame : 降标
    // 保持37种颜色平坦(其 渲染 路径是热的,它的 解析 工作线程 关闭 -thread ),
    // 该部分 缓存 已经擦了点 -resolved 像素,还有 StylePool
    // 缓存 SGR 字节.
    //
    // 上面的测试只是第一次 水槽,所以没有发现外表 状态更新函数
    // 出版一个,而不是另一个。 状态更新函数 内容 -seals
    // 整个记录 IMMEDIATELY 出版之后,
    // 所以 过期 投影不是一个 -frame 闪烁器 —— 它被
    // 烤成密封元素。 在黑暗的光线交换中, 将每一个老转弯留在了
    // 即将发售的主题的墨水中( 被洗净) -out 流言,
    // 暗码 -span 光帆上有板块),而最新消息则建于 view() 追上来了,
    // 看起来没错
    //
    // 将组合置于其源头: 一个 发布必须移动 maya
    // 也使用现场主题。 将它们再次分割的未来重构失败。
    const maya::Theme* dracula = nullptr;
    const maya::Theme* rose = nullptr;
    for (const auto& s : maya::theme::schemes) {
        if (std::string_view{s.name} == "Dracula") dracula = s.theme;
        if (std::string_view{s.name} == "Rose Pine Dawn") rose = s.theme;
    }
    REQUIRE(dracula != nullptr);

    ui_prefs::publish_theme(*dracula);
    // 实际操作的是这个方案 及其对比的底线。
    CHECK(maya::theme::live() == maya::theme::with_contrast_floor(*dracula));
    CHECK(ui_prefs::theme() == maya::theme::live());

    // 一个光线方案,因为极性是使bug可见的:从暗色
    // 主题上留下的光墨在光线画布上是无法读取的.
    if (rose != nullptr) {
        ui_prefs::publish_theme(*rose);
        CHECK(maya::theme::live() == maya::theme::with_contrast_floor(*rose));
        // 两汇相互一致,这是整个观点.
        CHECK(maya::theme::live() == ui_prefs::theme());
    }

    ui_prefs::publish_theme(maya::theme::native);
    CHECK(maya::theme::live() == maya::theme::native);
}

TEST_CASE("appearance: every scheme's ink reads on its own background") {
    // 三分之一的捆绑计划船 `primary`/`accent`
    // /`error` 不到4.5:1 在自己的画布上,
    // 所以 链接 ,标题和子弹很难读。地板上会抬起它们;
    // 检查每个方案是否都保留了,而且已经读过的一个插槽是按方案设置的。
    int lifted = 0, checked = 0;
    for (const auto& s : maya::theme::schemes) {
        const maya::Theme& t = *s.theme;
        if (!t.background.has_channels()) continue;
        const maya::Theme f = maya::theme::with_contrast_floor(t);
        const std::pair<maya::LitColor, maya::LitColor> slots[] = {
            {t.primary, f.primary}, {t.link, f.link},
            {t.error, f.error},     {t.accent, f.accent}};
        for (const auto& [raw, out] : slots) {
            if (!raw.has_channels()) continue;
            ++checked;
            const double before = maya::theme::detail::contrast(raw, t.background);
            const double after  = maya::theme::detail::contrast(out, t.background);
            if (before >= 4.5) {
                CHECK_MESSAGE(out == raw, s.name);
            } else {
                ++lifted;
                // 中间 -grey 画布可能根本不允许4.5,
                // 但地板绝不能使槽更坏。
                CHECK_MESSAGE(after >= std::min(4.5, before) - 1e-9, s.name);
            }
        }
    }
    CHECK(checked > 1000);
    CHECK(lifted > 100);   // 地板真的在工作吗?
}

TEST_CASE("appearance: only a theme that names a background owns the canvas") {
    // agentty 谓 Mode::Inline - 电话 帧 是一个窗口,
    // 在上方 回滚缓冲区 而不是整个屏幕 "paint the theme's background"
    // 是一个有实际后果的决定,它的关键在于 DATA (
    // 主题是否为背景?)而不是在名称或表格索引上。

    // 当地没有:没有画,画一个猜想会破坏
    // 终端 自己的背景,包括透明度和背景图像。
    // 这是本土的全部要点。
    CHECK_FALSE(maya::theme::owns_canvas(maya::theme::native));
    CHECK(maya::theme::native.background.kind()
          == maya::Color::Kind::Default);

    // 每个运出的方案都写了一个真实的背景,因为其前景是相对
    // 的 -checked 反对 该 画布,一半 -applying
    // 德古拉的墨水在用户的白色上 终端 - 这正是
    // "theme looks broken" 失败此替换。
    int owning = 0;
    for (const auto& s : maya::theme::schemes) {
        CHECK_MESSAGE(maya::theme::owns_canvas(*s.theme), s.name);
        ++owning;
    }
    CHECK(owning > 0);
}

TEST_CASE("appearance: usable on the terminals issue #37 reported") {
    // github.com/agentty-org/agentty#37 —
    // 非黑色背景终端中的外观很差，几乎无法使用。
    // 轻 -grey 背景、黑色字体和合理的反对 TERM 被忽略了,我被忽略了。
    //
    // 辩护方是DEFAULT主题完全没有颜色,
    // 因此它从未假设过的背景是不会错的。
    // 这走过了记者的实际环境,并断言了这一点。

    struct Env { const char* term; const char* colorterm;
                 const char* fgbg; const char* no_color; };

    auto with_env = [](const Env& e, auto&& fn) {
        // A 终端 名称( 3) Windows 终端's WT_SESSION,
        // ConEmuANSI, TERM_PROGRAM 排名 TERM
        // 输入 detect_tier ,所以,请 HOST 运行套房会渗入每个案件。
        // 在持续时间内清除,然后恢复。
        const char* host_vars[] = {"WT_SESSION", "ConEmuANSI", "TERM_PROGRAM", "CLICOLOR_FORCE"};
        std::vector<std::pair<std::string, std::optional<std::string>>> saved;
        for (const char* k : host_vars) {
            const char* v = std::getenv(k);
            saved.emplace_back(k, v ? std::optional<std::string>{v} : std::nullopt);
            ::unsetenv(k);
        }
        ::unsetenv("TERM"); ::unsetenv("COLORTERM");
        ::unsetenv("COLORFGBG"); ::unsetenv("NO_COLOR"); ::unsetenv("MAYA_COLOR");
        if (e.term)      ::setenv("TERM", e.term, 1);
        if (e.colorterm) ::setenv("COLORTERM", e.colorterm, 1);
        if (e.fgbg)      ::setenv("COLORFGBG", e.fgbg, 1);
        if (e.no_color)  ::setenv("NO_COLOR", e.no_color, 1);
        fn();
        ::unsetenv("TERM"); ::unsetenv("COLORTERM");
        ::unsetenv("COLORFGBG"); ::unsetenv("NO_COLOR"); ::unsetenv("MAYA_COLOR");
        for (const auto& [k, v] : saved)
            if (v) ::setenv(k.c_str(), v->c_str(), 1);
    };

    const Env envs[] = {
        {"rxvt-unicode",   nullptr,     "0;15", nullptr},  // 报告: 轻 bg
        {"xterm",          nullptr,     "0;7",  nullptr},  // 轻 bg, 其他索引
        {"vt100",          nullptr,     nullptr, nullptr}, // "TERM=vt100 solves it"
        {"dumb",           nullptr,     nullptr, nullptr},
        {"xterm-256color", "truecolor", nullptr, "1"},     // NO_COLOR
        {"konsole",        nullptr,     nullptr, nullptr},
        {"screen",         nullptr,     nullptr, nullptr},
    };

    for (const Env& e : envs) {
        with_env(e, [&] {
            // 默认:原生,阶梯自动,极性自动.
            const auto r = ui_prefs::resolve(ui_prefs::Prefs{}, agentty::ui_prefs::detect(true));

            // 该 不变量. 无论检测到什么,默认主题既不是前缘,
            // 也不是背景,所以在光终端上可以辨识,
            // 是暗色的,是单色的,也是一样的.
            CHECK(r.theme->text.kind() == maya::Color::Kind::Default);
            CHECK(r.theme->background.kind() == maya::Color::Kind::Default);
            // 而它从不填满画布,因此用户自己的背景(
            // 及其透明度)得以存活.
            CHECK_FALSE(maya::theme::owns_canvas(*r.theme));
        });
    }

    // TERM 真正受到咨询的是——记者的具体申诉。
    // 不同的TERMS必须产生不同的层次,或者检测是戏剧.
    with_env({"dumb", nullptr, nullptr, nullptr}, [&] {
        CHECK(ui_prefs::resolve(ui_prefs::Prefs{}, agentty::ui_prefs::detect(true)).tier
              == maya::theme::ColorTier::Mono);
    });
    with_env({"vt100", nullptr, nullptr, nullptr}, [&] {
        CHECK(ui_prefs::resolve(ui_prefs::Prefs{}, agentty::ui_prefs::detect(true)).tier
              == maya::theme::ColorTier::Ansi16);
    });
    with_env({"xterm-256color", nullptr, nullptr, nullptr}, [&] {
        CHECK(ui_prefs::resolve(ui_prefs::Prefs{}, agentty::ui_prefs::detect(true)).tier
              == maya::theme::ColorTier::Ansi256);
    });
    with_env({"xterm-256color", "truecolor", nullptr, nullptr}, [&] {
        CHECK(ui_prefs::resolve(ui_prefs::Prefs{}, agentty::ui_prefs::detect(true)).tier
              == maya::theme::ColorTier::TrueColor);
    });
    // 宽度超过 sh : COLORTERM 跳下去就活不下来了 TERM 已经。
    with_env({"konsole", nullptr, nullptr, nullptr}, [&] {
        CHECK(ui_prefs::resolve(ui_prefs::Prefs{}, agentty::ui_prefs::detect(true)).tier
              == maya::theme::ColorTier::TrueColor);
    });

    // 淡背景为DETECTED,不猜测,当COLORFGBG说到.
    with_env({"xterm", nullptr, "0;15", nullptr}, [&] {
        CHECK(ui_prefs::resolve(ui_prefs::Prefs{}, agentty::ui_prefs::detect(true)).polarity
              == maya::theme::Polarity::Light);
    });

    // 如果需要黑白终端怎么办？
    // ——明确单项请求获得满足,仍留下可读,不涂的. 帧.
    with_env({"xterm-256color", "truecolor", nullptr, nullptr}, [&] {
        ::setenv("MAYA_COLOR", "none", 1);
        const auto r = ui_prefs::resolve(ui_prefs::Prefs{}, agentty::ui_prefs::detect(true));
        CHECK(r.tier == maya::theme::ColorTier::Mono);
        CHECK_FALSE(maya::theme::owns_canvas(*r.theme));
    });

    // 一个名字 RGB 方案为 不 应用在256以下的颜色:
    // 将德古拉量化为16比用户自己的调色板更糟糕.
    with_env({"vt100", nullptr, nullptr, nullptr}, [&] {
        ui_prefs::Prefs p; p.theme = "Dracula";
        const auto r = ui_prefs::resolve(p, agentty::ui_prefs::detect(true));
        CHECK(r.theme == &maya::theme::native);
    });
}

// 每一个变换的把手 RESOLVED COLOUR 必须使已经
// 实现的无效,而不仅仅是主题行。
//
// 记录被缓存为 BUILT 要件:已定转折 ViewCache ,
// 密封在 m.ui.frozen。颜色在构建时解决,
// 其中没有 缓存 以外观键入, 因此一个旋钮可以改变颜色
// 的解析方式而不使它们无效, 它会留下整个可见的对话框。
// 只有新转弯才看起来对, 就是它所建的调色板。
// "it only half-changed" 报告。
//
// 极性与极性是容易错过的:它们永远不会触碰 `ui.theme` ,
// 所以一个支票 "did the theme change"
// 看不到,但级决定 是否计划 渲染 以真彩 / 256 /
// 16 / 单极和极性决定画布的哪一面是墨水。
TEST_CASE("appearance: a colour knob invalidates what is already rendered") {
    install_stub_deps();

    // 已公布的记录记录有固定的条目:它保存着
    // 一个已解决其颜色的已建成元素。
    const auto seed_cache = [](Model& m) {
        m.ui.view_cache.message_md(ThreadId{"t"}, MessageId{"m1"}).finalized =
            std::make_shared<maya::Element>();
    };
    const auto cached = [](Model& m) {
        const auto* c = m.ui.view_cache.peek(ThreadId{"t"}, MessageId{"m1"});
        return c != nullptr && c->finalized != nullptr;
    };

    struct Knob { std::string_view id; std::string what; };
    for (const Knob k : {Knob{ui::panel::kApTier,     "colour tier"},
                         Knob{ui::panel::kApPolarity, "background polarity"},
                         Knob{ui::panel::kApSyntax,   "syntax highlighting"}}) {
        Model m = opened();
        seed_cache(m);
        REQUIRE(cached(m));
        m = press(std::move(m), k.id, form::keys::Action{form::keys::Intent::AdjustUp});
        CHECK_MESSAGE(!cached(m),
                      k.what << " changed but the rendered transcript was kept");
    }

    // 反例,和原因 这是一个列表而不是 "always invalidate":
    // a STRUCTURAL 手势不得 密度和紧凑度转换行
    // HEIGHTS 和一个密封的转弯的高度是 加载-bearing
    // 内置线性 回滚缓冲区 分类账——再 -sealing 新的高度
    // 将它撕裂,这些设置是向前的 -only 设计
    for (const Knob k : {Knob{ui::panel::kApDensity, "density"},
                         Knob{ui::panel::kApCompact, "compact turns"}}) {
        Model m = opened();
        seed_cache(m);
        m = press(std::move(m), k.id, form::keys::Action{form::keys::Intent::AdjustUp});
        CHECK_MESSAGE(cached(m),
                      k.what << " is structural and must not re-seal the ledger");
    }
}

// ── 两者 保存 道路不能争吵 ──────────────────────────────
//
// agentty 保存 设置两种不同的方式:
//
//   A. 加载-modify-save 通过 接口边界  (ToggleChangesStrip ,
//   和~20 以上) B. persist_settings (m),种子来自: m.d.persisted 写回去
//
// B只看到A留下的东西 `m.d.persisted`。
// A 语句 -style 写着 接口边界 但没有留下两人的分歧,
// 下B——主题调侃,模型切换,布 保存 ,或退出 ——写下 过期 记录在顶端。
// "settings don't persist" : 写作土地,
// 然后是后来无关的 保存 默然还原.
TEST_CASE("settings: a toggle survives a later whole-record save") {
    install_stub_deps();

    Model m;
    m.d.persisted = g_saved;   // 以 init() 种子,种子,种子

    auto [m2, toggle_cmd] = app::update(std::move(m), Msg{ToggleChangesStrip{}});
    apply_fx(toggle_cmd);
    m = std::move(m2);

    // 切换本身是有效的——它既可以到达模型,也可以到达商店.
    CHECK(m.d.show_changes_strip == true);
    CHECK(g_saved.show_changes_strip == true);

    // ..它一定还在后面 任意 其他项目 保存 运行。
    apply_fx(agentty::app::detail::persist_settings(m));
    CHECK(g_saved.show_changes_strip == true);
}

TEST_CASE("settings: Esc steps back, it does not close everything") {
    install_stub_deps();
    // 用户到达的方式 : Ctrl +K,然后是设置行。
    auto [m1, _1] = app::update(Model{}, Msg{OpenPalette{}});
    REQUIRE(m1.ui.panel.get<ui::panel::Palette>() != nullptr);

    auto [m2, _2] = app::update(std::move(m1),
                                Msg{OpenSettingsList{agentty::settings::Category::General}});
    REQUIRE(m2.ui.panel.get<ui::panel::SettingsList>() != nullptr);

    // Esc 必须回到调色板上——我们从那里走出来的一步——而且
    // 只有一秒钟 Esc 到达 线程.
    auto [m3, _3] = app::update(std::move(m2), Msg{CloseSettingsList{}});
    CHECK(m3.ui.panel.get<ui::panel::Palette>() != nullptr);
}
