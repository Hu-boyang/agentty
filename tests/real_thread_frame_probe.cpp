// real_thread_frame_probe ——按 -frame a 费用 真实 长 线程.
//
// 合成人 long_session_bench 说 帧 760的活记录
// -message 显示 17-33 ms 视图。 加载 实际数 线程
// 冻结它的方式 TUI 已经,和时间 view() (真实的)
// AgenttyApp::view ,每个部件)加上颜料,反复,所以缺口可以归结.
// BENCH_PERF_LOOPS =N 重复为perf采样.
//
//   ./build/agentty_standalone_tests real_thread_frame_probe <thread> [width]

#include <agentty/io/persistence.hpp>
#include <agentty/io/thread_log.hpp>
#include <agentty/runtime/app/update/internal.hpp>
#include <agentty/runtime/model.hpp>
#include <agentty/runtime/view/view.hpp>
#include "agtest_cap.hpp"

#include <maya/render/canvas.hpp>
#include <maya/render/renderer.hpp>
#include <maya/style/theme.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>

using namespace agentty;
using Clock = std::chrono::steady_clock;

static double ms(Clock::duration d) {
    return std::chrono::duration<double, std::milli>(d).count();
}

int main(int argc, char** argv) {
    if (argc < 2) {
        // ctest 不传会话时，没有可探测内容，直接成功。
        std::printf("usage: %s <thread> [width]\n", argv[0]);
        return 0;
    }
    const std::filesystem::path p = argv[1];
    const int width = argc > 2 ? std::atoi(argv[2]) : 140;
    const int loops = std::getenv("BENCH_PERF_LOOPS") ? std::atoi(std::getenv("BENCH_PERF_LOOPS")) : 60;

    std::optional<Thread> loaded;
    if (p.extension() == ".jsonl") {
        if (auto log = ThreadLog::open_path(p)) loaded = log->load_thread();
    } else if (auto r = persistence::load_thread_file(::agentty::IoAccess::grant(), p)) {
        loaded = std::move(*r);
    }
    if (!loaded) { std::fprintf(stderr, "load failed\n"); return 2; }

    Model m;
    m.ui.term_cols = width;
    m.ui.term_rows = 50;
    m.d.current = std::move(*loaded);
    app::detail::rehydrate_frozen(m);
    // 以运行会话的方式锁定被冻结的前缀。
    for (int g = 0; g < 64; ++g)
        if (app::detail::trim_frozen_if_oversized(m).is_none()) break;
    std::printf("messages=%zu frozen=%zu frozen_through=%zu\n",
                m.d.current.messages.size(), m.ui.frozen.size(), m.ui.frozen_through);

    maya::StylePool pool;
    maya::Canvas canvas(width, 4000, &pool);
    std::vector<double> view_ms, paint_ms;
    for (int i = 0; i < loops; ++i) {
        const auto a = Clock::now();
        auto el = ui::view(m);
        const auto b = Clock::now();
        canvas.clear();
        maya::render_tree(el, canvas, pool, maya::theme::native, /*auto_height=*/true);
        const auto c = Clock::now();
        if (i == 0) {
            std::printf("first frame: view=%.2f ms paint=%.2f ms\n", ms(b - a), ms(c - b));
            continue;
        }
        view_ms.push_back(ms(b - a));
        paint_ms.push_back(ms(c - b));
    }
    const auto med = [](std::vector<double> v) {
        std::sort(v.begin(), v.end());
        return v.empty() ? 0.0 : v[v.size() / 2];
    };
    const auto mx = [](const std::vector<double>& v) {
        return v.empty() ? 0.0 : *std::max_element(v.begin(), v.end());
    };
    std::printf("steady view : median %.2f ms  max %.2f ms\n", med(view_ms), mx(view_ms));
    std::printf("steady paint: median %.2f ms  max %.2f ms\n", med(paint_ms), mx(paint_ms));
    return 0;
}
