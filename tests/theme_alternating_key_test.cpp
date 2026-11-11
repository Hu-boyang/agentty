// theme_alternating_key_test ——实际复制的姿态.
//
// 该 REPORT. 按下然后上(或上然后下)不更新屏幕,
// 按SAME方向两次。Enter然后承诺一个
// 从未显示过直播的主题。
//
// 原因 每个 EXISTING PROBE MISSED IT.
// theme_wire_cost_probe, theme_lag_repro 和
// 所有的朋友们 渲染 结束 `maya::render_to_string(el, w)` ,
// 它构建了 FRESH StylePool 和新组件 缓存 百分比 调用 丢掉
// 虫子所生活的东西: 状态 BETWEEN 帧. A 缓存 空的 每一个 帧 永远
// 不能为一个 过期 输入,所以这些探测器 结构上无法 失败在这个。
//
// 这个测试保持 一个 渲染器 横跨 帧 ——真实
// 应用的方式——并驱动报告准确的交替姿态.
//
// 内容 ALTERNATING VS REPEATING
// TELLS US。这是一个签名。 记忆缓存 键入到未包含主题的上下。
// 下 \ Up 返回光标到已建并缓存的行 PREVIOUS
// 调色板,所以 缓存 答案和新颜色永远无法到达 传输.
// Down – 下方的地盘上还没有条目,所以它被新调色板下新造出来,
// 看起来起作用了。同样的键盘计数,相反的结果——这正是一个
// 内容-keyedXX 缓存,没有键盘中的主题所做的事情。

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include <maya/maya.hpp>
#include <maya/render/canvas.hpp>
#include <maya/render/renderer.hpp>
#include <maya/style/theme.hpp>

#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/msg.hpp"
#include "agentty/runtime/app/program.hpp"
#include "agentty/runtime/view/view.hpp"
#include "agentty/io/persistence.hpp"

using namespace agentty;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

constexpr int kW = 100;
constexpr int kH = 40;

// A 渲染器 其 人 名 池 和构成部分 缓存 PERSIST 横跨 帧 ,这就是什么 `maya::run`
// 做和什么 做,什么 做 render_to_string 才不会呢
class Screen {
public:
    Screen() : canvas_(kW, kH, &pool_) {}

    // 渲染 one 帧 并返回存储单元作为文本+颜色,
    // 因此比较后即使格子完全相同也会看到调色板的变化.
    std::string frame(const Model& m) {
        maya::RenderContext ctx{kW, kH, maya::render_generation(), true};
        maya::RenderContextGuard g(ctx);

        // view() 出版调色板( 3) ui_prefs::publish_theme
        // )作为a. 副作用 建筑, 与真正的主人一样。
        auto el = ui::view(m);

        // 真正的循环顺序: 池 学习主题交换,
        // 然后 源码树 被制成与上次相同的画布 帧.
        const bool swapped = pool_.retheme();
        canvas_.clear();
        std::vector<maya::layout::LayoutNode> nodes;
        maya::render_tree(el, canvas_, pool_, maya::theme::live(), nodes,
                          /*auto_height=*/true);
        last_retheme_ = swapped;
        last_epoch_   = maya::theme::live_epoch();
        last_live_    = &maya::theme::live();

        // 序列化 存储单元风格,而不仅仅是格言:整个问题在于
        // COLOURS是否移动.
        std::string out;
        out.reserve(static_cast<std::size_t>(kW) * kH * 4);
        for (int y = 0; y < canvas_.height(); ++y) {
            for (int x = 0; x < canvas_.width(); ++x) {
                const auto c = canvas_.get(x, y);
                out += static_cast<char>('a' + (c.style_id % 26));
                out += static_cast<char>(
                    c.character < 128 ? static_cast<char>(c.character) : '?');
            }
        }
        return out;
    }

    [[nodiscard]] bool retheme_fired() const noexcept { return last_retheme_; }
    [[nodiscard]] unsigned epoch() const noexcept { return last_epoch_; }
    [[nodiscard]] const void* live() const noexcept { return last_live_; }

private:
    maya::StylePool pool_;
    maya::Canvas    canvas_;
    bool            last_retheme_ = false;
    unsigned        last_epoch_   = 0;
    const void*     last_live_    = nullptr;
};

Model seeded() {
    app::install_deps(app::Deps{
        .stream = [](const maya::unlocked&, provider::Request, provider::EventSink) {},
        .save_thread = [](const Thread&) {},
        .load_threads = [] { return std::vector<Thread>{}; },
        .load_thread = [](const ThreadId&) { return std::optional<Thread>{}; },
        .load_settings = [] { return store::Settings{}; },
        .save_settings = [](const store::Settings&) {},
        .new_thread_id = [] { return ThreadId{"probe"}; },
        .title_from = [](std::string_view) { return std::string{"t"}; },
    });

    Model m;
    // 真彩色 终端 ,在 Model : 主题仅画于
    // truecolor/256 ,视图读取了 m.env.
    m.env.terminal.tier = maya::theme::ColorTier::TrueColor;
    for (int i = 0; i < 40; ++i) {
        Message msg;
        msg.role = (i % 2 == 0) ? Role::User : Role::Assistant;
        msg.text = "message " + std::to_string(i)
                 + " with some **bold** text and `code` in it";
        m.d.current.messages.push_back(std::move(msg));
    }
    app::detail::rehydrate_frozen(m);
    return m;
}

} // 命名空间结束。

int main() {
    // 强制执行 之前 级 任何决定主题的事物。
    //
    // 没有这个测试是空虚的,看起来像个虫子: 在cest stdout
    // 下是一根管子, detect_tier 回答摩诺,
    // 和 ui_prefs::resolve's `can_paint`
    // 闸门坍塌 每个 命名方案为本地。 帧 那样的话,
    // 就合法地 渲染 测试报告一个屏幕永远不会改变 - 而真实的 终端
    // 色是真色的,没事的 MAYA_COLOR 检查高于其他信号的(
    // detect_tier 步骤0),因此这让绳子问它的意思是问:
    // 给一个调色板 终端 CAN 颜料, 键盘键 重绘 ?
#if defined(_WIN32)
    _putenv_s("MAYA_COLOR", "truecolor");
#else
    ::setenv("MAYA_COLOR", "truecolor", 1);
#endif

    std::printf("=== theme_alternating_key_test ===\n\n");
    std::printf("one renderer across frames (pool + component cache persist)\n\n");

    // ── 1. 报告的姿态:交替方向 ─────────────────
    {
        Screen scr;
        Model m = seeded();
        m = app::update(std::move(m), Msg{OpenAppearance{}}).first;
        m = app::update(std::move(m), Msg{AppearancePickTheme{}}).first;
        std::string prev = scr.frame(m);

        std::printf("alternating  \u2193 \u2191 \u2193 \u2191 \u2193 \u2191\n");
        int dead = 0;
        int gated = 0;
        std::uint64_t prev_hash = app::AgenttyApp::visual_hash(m);
        for (int i = 0; i < 6; ++i) {
            const int dir = (i % 2 == 0) ? +1 : -1;
            m = app::update(std::move(m), Msg{AppearanceThemeMove{dir}}).first;

            // 该 GATE ,其他探测器通过呼叫跳过 view()
            // 直接来 maya 渲染 仅在此移动时;
            // 重复的散列表示 帧 被丢弃,无论多么不同
            // view() 本来是的。
            const std::uint64_t h = app::AgenttyApp::visual_hash(m);
            const bool hash_moved = (h != prev_hash);
            prev_hash = h;

            std::string cur = scr.frame(m);
            const bool moved = (cur != prev);
            std::printf("    %-4s theme=%-22s hash=%s screen %s\n",
                        dir > 0 ? "down" : "up",
                        m.d.ui().theme.empty() ? "native" : m.d.ui().theme.c_str(),
                        hash_moved ? "moved " : "SAME  ",
                        moved ? "changed" : "DID NOT CHANGE");
            if (!moved) ++dead;
            if (!hash_moved) ++gated;
            prev = std::move(cur);
        }
        check(gated == 0, "the frame gate never eats an alternating press");
        check(dead == 0, "every alternating press repaints");
        std::printf("\n");
    }

    // ── 2. 控制:两次相同的方向 ────────────────────────
    // 报告说这个能行 如果它能通过而上面的失败 区别在于
    // 缓存 ,而不是 状态更新函数.
    {
        Screen scr;
        Model m = seeded();
        m = app::update(std::move(m), Msg{OpenAppearance{}}).first;
        m = app::update(std::move(m), Msg{AppearancePickTheme{}}).first;
        std::string prev = scr.frame(m);

        std::printf("repeating    \u2193 \u2193 \u2193 \u2193 \u2193 \u2193\n");
        int dead = 0;
        for (int i = 0; i < 6; ++i) {
            m = app::update(std::move(m), Msg{AppearanceThemeMove{+1}}).first;
            std::string cur = scr.frame(m);
            const bool moved = (cur != prev);
            std::printf("    down theme=%-22s screen %s\n",
                        m.d.ui().theme.empty() ? "native" : m.d.ui().theme.c_str(),
                        moved ? "changed" : "DID NOT CHANGE");
            if (!moved) ++dead;
            prev = std::move(cur);
        }
        check(dead == 0, "every repeated press repaints");
        std::printf("\n");
    }

    // ── 3. 返回OLD调色板下看到的一排 ────────────────
    // 错误的最小形状: 访问 N 列, 移动,
    // 通过移动更改调色板, 返回。 如果有 记忆缓存 键入时
    // 没有主题的键入是服务于 N 行, 即 帧 画着旧的颜色
    {
        Screen scr;
        Model m = seeded();
        m = app::update(std::move(m), Msg{OpenAppearance{}}).first;
        m = app::update(std::move(m), Msg{AppearancePickTheme{}}).first;

        scr.frame(m);
        const std::string theme_at_start = m.d.ui().theme;

        m = app::update(std::move(m), Msg{AppearanceThemeMove{+1}}).first;
        const std::string away_frame  = scr.frame(m);
        const std::string theme_away  = m.d.ui().theme;

        m = app::update(std::move(m), Msg{AppearanceThemeMove{-1}}).first;
        const std::string back_frame  = scr.frame(m);
        const std::string theme_back  = m.d.ui().theme;

        std::printf("leave a row and come back\n");
        std::printf("    start=%s  away=%s  back=%s\n",
                    theme_at_start.empty() ? "native" : theme_at_start.c_str(),
                    theme_away.empty()     ? "native" : theme_away.c_str(),
                    theme_back.empty()     ? "native" : theme_back.c_str());
        check(theme_back == theme_at_start,
              "the model returns to the starting theme");
        check(back_frame != away_frame,
              "the screen returns too (not stuck on the row we left)");
        std::printf("\n");
    }

    if (failures == 0) {
        std::printf("PASS -- alternating and repeating both repaint\n");
        return 0;
    }
    std::printf("FAILED: %d check(s) -- bug reproduced\n", failures);
    return 1;
}
