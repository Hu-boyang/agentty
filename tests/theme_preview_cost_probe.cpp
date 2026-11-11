// 在一个主题-preview键盘成本,在两条路径上衡量。
//
// theme_input_lag_probe 减少措施+ 渲染 时间:
// SYNTHETIC 记录,在哪里 rehydrate_frozen 排
// 预算崩溃到 ~11 个预建行, 因此无法看到此修补消除的成本。
// 这可以直接测量两个重排路径 :
//
//   restyle_sealed_turns () 公布+无效+ REHYDRATE
//   FROZEN restyle_preview (d) 公布+失效(固定)
//
// 两者之间的差距是每个箭键用来支付的东西.

#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/msg.hpp"
#include "agentty/runtime/panel/appearance.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace agentty;
using clk = std::chrono::steady_clock;

namespace {

double ms_since(clk::time_point t0) {
    return std::chrono::duration<double, std::milli>(clk::now() - t0).count();
}

} // 命名空间结束。

int main(int argc, char** argv) {
    const int msgs = argc > 1 ? std::atoi(argv[1]) : 1000;

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
    for (int i = 0; i < msgs; ++i) {
        Message msg;
        msg.role = (i % 2) ? Role::Assistant : Role::User;
        msg.text = "## Heading " + std::to_string(i) +
            "\n\nSome prose with `code` and *emphasis*, long enough to wrap "
            "across a couple of lines in a normal terminal width.\n\n"
            "```cpp\nint f" + std::to_string(i) + "() { return 42; }\n```\n";
        m.d.current.messages.push_back(std::move(msg));
    }

    m = app::update(std::move(m), Msg{OpenAppearance{}}).first;
    m = app::update(std::move(m), Msg{AppearancePickTheme{}}).first;
    app::detail::rehydrate_frozen(m);

    std::printf("transcript: %d msgs, frozen_through=%zu\n\n",
                msgs, m.ui.frozen_through);

    constexpr int kIters = 20;

    // 电话 旧 百分比 -keystroke 成本。 成本。
    double full = 0;
    for (int i = 0; i < kIters; ++i) {
        m.d.ui().theme = (i % 2) ? "Dracula" : "Nord";
        const auto t0 = clk::now();
        app::detail::restyle_sealed_turns(m);
        full += ms_since(t0);
    }
    full /= kIters;

    // 电话 新 百分比 -keystroke 成本。 成本。
    double second = 0;
    for (int i = 0; i < kIters; ++i) {
        m.d.ui().theme = (i % 2) ? "Dracula" : "Nord";
        const auto t0 = clk::now();
        app::detail::restyle_sealed_turns(m);
        second += ms_since(t0);
    }
    second /= kIters;

    std::printf("  restyle_sealed_turns (was, per arrow) : %8.3f ms\n", full);
    std::printf("  restyle (2nd pass, warm cache)       : %8.3f ms\n", second);
    if (second > 0.0001)
        std::printf("  speedup                               : %8.1fx\n",
                    full / second);
    std::printf("\n  budget at 30 keys/sec                 : %8.3f ms\n", 33.333);
    std::printf("  old: %s   new: %s\n",
                full    < 33.333 ? "fits" : "OVER BUDGET",
                second < 33.333 ? "fits" : "OVER BUDGET");
    return 0;
}
