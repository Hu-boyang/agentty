// 主题 -browser INPUT LAG 探测器。
//
// theme_switch_leak_probe 证明没有漏水:每个开关的成本
// 永远是平的。 -and-expensive 仍然感觉拉吉,如果钥匙到达的
// 速度比开关成本快——按键保持QQ -repeat 交付~30 keys/sec ,
// 他们每个人都被派遣和重新 -rendered.
//
// 测量用户实际体验的事物: 握住键数秒 N,
// 然后问其距离有多远 BEHIND 编号 UI 即时,
// 而不是吞吐量 —— 浏览器是瞬间感觉还是
// 模糊。
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/msg.hpp"
#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/panel/appearance.hpp"
#include "agentty/runtime/view/view.hpp"

#include "agentty/runtime/app/update/internal.hpp"   // rehydrate_frozen
#include "agentty/io/persistence.hpp"

#include <maya/core/render_context.hpp>
#include <maya/print.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace agentty;
using clk = std::chrono::steady_clock;

int main(int argc, char** argv) {
    // 典型 X11/wayland 键 -repeat: ~25-30 chars/sec 初约后.
    const int    keys_per_sec = argc > 1 ? std::atoi(argv[1]) : 30;
    const double hold_secs    = argc > 2 ? std::atof(argv[2]) : 2.0;
    const int    msgs         = argc > 3 ? std::atoi(argv[3]) : 100;

    app::install_deps(app::Deps{
        .stream        = [](const maya::unlocked&, provider::Request, provider::EventSink) {},
        .save_thread   = [](const Thread&) {},
        .delete_thread = [](const ThreadId&) {},
        .load_threads  = [] { return std::vector<Thread>{}; },
        .load_thread   = [](const ThreadId&) { return std::optional<Thread>{}; },
        .load_settings = [] { return store::Settings{}; },
        .save_settings = [](const store::Settings&) {},
        .new_thread_id = [] { return ThreadId{"t-lag"}; },
        .title_from    = [](std::string_view) { return std::string{"t"}; },
    });

    Model m;
    // A 真实 线程 当一个名称被命名(argv[4])或
    // AGENTTY_THREAD ),否则,它是一个合成的。区别比它看起来
    // 更重要: 冻结状态是由 rehydrate_frozen ,
    // 并且记录稿主要是 FROZEN 渲染 活体时从预建元素中 末尾 才不会呢
    // 衡量一切生活 将责怪 渲染器 工作时
    // 没有真正进行。
    const char* thread_path = argc > 4 ? argv[4] : std::getenv("AGENTTY_THREAD");
    if (thread_path && *thread_path) {
        auto t = persistence::load_thread_file(::agentty::IoAccess::grant(), thread_path);
        if (t) {
            m.d.current = std::move(*t);
            std::printf("loaded %s: %zu messages\n",
                        thread_path, m.d.current.messages.size());
        } else {
            std::printf("could not load %s (err %d) - falling back\n",
                        thread_path, (int)t.error().kind);
        }
    }
    if (m.d.current.messages.empty()) {
        for (int i = 0; i < msgs; ++i) {
            Message msg;
            msg.role = (i % 2) ? Role::Assistant : Role::User;
            msg.text = "## Heading " + std::to_string(i) +
                "\n\nSome prose with `code` and *emphasis*, long enough to wrap "
                "across a couple of lines in a normal terminal width.\n\n"
                "```cpp\nint f" + std::to_string(i) + "() { return 42; }\n```\n";
            m.d.current.messages.push_back(std::move(msg));
        }
    }
    m = app::update(std::move(m), Msg{OpenAppearance{}}).first;
    m = app::update(std::move(m), Msg{AppearancePickTheme{}}).first;

    // A 真实 其大部分记录被冻结: m.ui.frozen
    // 已经搁置 -built 元素,只有最后的几台屏幕是直播的
    // 末尾 重建每个 帧 离开 frozen_through
    // 零点开始 整个 记录片现场直播,它衡量一个用户从未进入的
    // 状态——并且以线性的方式夸大了对话长度的成本.
    //
    // rehydrate_frozen () 主题的改变 调用 ,
    // 它又会 -seals 预算从头到尾,所以,让它确定现实
    // 的分割而不是手 -seeding 将立即覆盖一个指数。
    // rehydrate_frozen () 主题的改变 调用 ,
    // 它又会 -seals 预算从头到尾都是这样 调用
    // 直接而不是希望一个无头的 渲染 确定拆分( 不;
    // 冻结由 运行时 承诺路径,没有探测器到达。
    app::detail::rehydrate_frozen(m);
    std::printf("frozen: through=%zu of %zu msgs, %zu prebuilt rows\n",
                m.ui.frozen_through, m.d.current.messages.size(),
                m.ui.frozen.size());

    // 一条路 事件 循环执行 : 分派 ,然后涂画。
    //
    // 故意分头走 maya 程序环路已连接 RENDERS — 分派
    // 仅设定 needs_render ,并有一个 帧 被涂抹到每个批次,
    // 但是 调用 drain_pending (单位) EVENT ,
    // 所以,请 状态更新函数 如果减少一半, 则会连结 渲染 买不到东西,
    // 修理必须在里面 状态更新函数 路径。
    double last_reduce_ms = 0, last_render_ms = 0;
    const auto one_key = [&] {
        auto r0 = clk::now();
        m = app::update(std::move(m), Msg{AppearanceThemeMove{+1}}).first;
        last_reduce_ms = std::chrono::duration<double, std::milli>(clk::now() - r0).count();
        auto p0 = clk::now();
        maya::RenderContext ctx{100, 40, maya::render_generation(), true};
        maya::RenderContextGuard g(ctx);
        auto el = ui::view(m);
        (void)maya::render_to_string(el, 100);
        last_render_ms = std::chrono::duration<double, std::milli>(clk::now() - p0).count();
    };

    // 温暖 缓存 所以我们衡量的是稳态,而不是第一 -paint.
    for (int i = 0; i < 5; ++i) one_key();

    auto t0 = clk::now();
    one_key();
    const double per_key_ms =
        std::chrono::duration<double, std::milli>(clk::now() - t0).count();

    const int    total_keys   = static_cast<int>(keys_per_sec * hold_secs);
    const double key_period_ms = 1000.0 / keys_per_sec;
    const double work_ms       = per_key_ms * total_keys;
    const double wall_ms       = hold_secs * 1000.0;

    std::printf("transcript=%d msgs   key-repeat=%d/s   hold=%.1fs "
                "(%d keys)\n\n", msgs, keys_per_sec, hold_secs, total_keys);
    std::printf("  cost of ONE \u2193 (reduce+render) : %.2f ms"
                "   (reduce %.2f + render %.2f)\n",
                per_key_ms, last_reduce_ms, last_render_ms);
    // 何谓连结 RENDERS 单独离开将: 状态更新函数 仍然运行每根键,
    // 所以这是地板任意 渲染-only 修补可以到达。
    std::printf("  if renders coalesced          : %.2f ms/key "
                "(reduce only)\n", last_reduce_ms);
    std::printf("  budget per key at %2d/s        : %.2f ms\n",
                keys_per_sec, key_period_ms);

    if (per_key_ms <= key_period_ms) {
        std::printf("\n  ✓ keeps up (%.0f%% of budget) — browser feels instant\n",
                    100.0 * per_key_ms / key_period_ms);
        return 0;
    }

    // 每把钥匙的成本都超过它的槽,所以只要钥匙被持有,积压的时间就会增加.
    // 此 是什么 说吧 "laggy after a while" 意思是:不是漏水,
    // 是排队.
    const double overrun_ms = work_ms - wall_ms;
    std::printf("\n  ✗ CANNOT keep up: %.1fx over budget\n",
                per_key_ms / key_period_ms);
    std::printf("    work queued in %.1fs hold  : %.0f ms\n", hold_secs, work_ms);
    std::printf("    wall time available        : %.0f ms\n", wall_ms);
    std::printf("    BACKLOG when you let go    : %.0f ms  <- felt as lag\n",
                overrun_ms);
    std::printf("    themes still to scroll past: %.0f\n",
                overrun_ms / per_key_ms);
    return 0;
}
