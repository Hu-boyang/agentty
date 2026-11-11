// thread_switch_prof_probe - 在哪里进行? 线程 SWITCH
// 实际花时间,在 UI 线程?
//
// 电话 加载 已经运行在 a 工作线程 (cmd_factory::load_thread_async
// 使用 task_isolated ),所以, 解析 成本为 不 用户等待的,
// 他们等待的就是 ThreadLoaded 状态更新函数 加上第
// 一个 渲染 ,这两个项目都位于 UI 线程 两者都在屏幕被冻结
// 时运行
//
// 直接路径的测量: 移动 a 已加载 线程 进入模型,
// 清除 缓存 ,将冷冻前缀补水,构建对话元素,
// 以及 渲染 ——顺序相同,顺序相同,与 状态更新函数。
// 每个阶段单独报告是重点;一个总数并不能
// 说明需要解决的哪一部分。
//
//   ./build/agentty_standalone_tests thread_switch_prof_probe <thread> [width]
//
// 接受 . 接受 接受 的 接受 , 接受 同意 的 同意 , 同意 接受。 JSON 或
// Jsonl这样一样 线程 迁移的两边都可以测量。 -op 过.

#include <agentty/domain/session.hpp>
#include <agentty/io/persistence.hpp>
#include <agentty/io/thread_log.hpp>
#include <agentty/runtime/model.hpp>
#include <agentty/runtime/view/thread/conversation.hpp>
#include <agentty/runtime/app/update/internal.hpp>

#include <maya/render/renderer.hpp>
#include <maya/widget/conversation.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <algorithm>
#include <vector>

namespace fs = std::filesystem;
using namespace agentty;

namespace {

using clk = std::chrono::steady_clock;

double ms_since(clk::time_point t) {
    return std::chrono::duration<double, std::milli>(clk::now() - t).count();
}

// 少数运行的中位数——一个毫秒
// -scale操作的样本大多是排程噪音.
double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v.empty() ? 0.0 : v[v.size() / 2];
}

} // 命名空间结束。

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("thread_switch_prof_probe: no thread given, nothing to do.\n"
                    "usage: %s <thread.json|thread.jsonl> [width]\n", argv[0]);
        return 0;
    }
    const fs::path p = argv[1];
    const int width = (argc > 2) ? std::atoi(argv[2]) : 120;

    // ── 离UI 线程:加载. 时光,但NOT部分等待. ──
    auto t = clk::now();
    std::optional<Thread> loaded;
    if (p.extension() == ".jsonl") {
        if (auto log = ThreadLog::open_path(p)) loaded = log->load_thread();
    } else {
        if (auto r = persistence::load_thread_file(::agentty::IoAccess::grant(), p)) loaded = std::move(*r);
    }
    const double load_ms = ms_since(t);
    if (!loaded) {
        std::fprintf(stderr, "load failed: %s\n", p.string().c_str());
        return 1;
    }

    std::printf("%s\n  %zu messages, %.1f MB on disk\n",
                p.filename().string().c_str(), loaded->messages.size(),
                static_cast<double>(fs::file_size(p)) / 1e6);
    std::printf("\n  worker thread (user does NOT wait):\n");
    std::printf("    load + parse            %7.2f ms\n", load_ms);

    // ── 上场 UI 线程 : 一切,我们 状态更新函数 已经。 ────────────────
    std::vector<double> swap_v, rehyd_v, cfg_v, build_v, render_v;
    constexpr int kRuns = 5;
    for (int i = 0; i < kRuns; ++i) {
        Model m;
        Thread copy = *loaded;          // 编号 状态更新函数 移动自有的 线程

        t = clk::now();
        m.d.current = std::move(copy);
        m.ui.view_cache.clear();
        swap_v.push_back(ms_since(t));

        t = clk::now();
        app::detail::rehydrate_frozen(m);
        rehyd_v.push_back(ms_since(t));

        t = clk::now();
        auto cfg = ui::conversation_config(m);
        cfg_v.push_back(ms_since(t));

        t = clk::now();
        auto el = maya::Conversation{std::move(cfg)}.build();
        build_v.push_back(ms_since(t));

        t = clk::now();
        const std::string out = maya::render_to_string(el, width);
        render_v.push_back(ms_since(t));
        if (i == 0)
            std::printf("\n  UI thread (this IS the wait), width %d:\n", width);
    }

    const double swap   = median(swap_v);
    const double rehyd  = median(rehyd_v);
    const double cfg    = median(cfg_v);
    const double build  = median(build_v);
    const double render = median(render_v);

    std::printf("    model swap + cache drop %7.2f ms\n", swap);
    std::printf("    rehydrate_frozen        %7.2f ms\n", rehyd);
    std::printf("    conversation_config     %7.2f ms\n", cfg);
    std::printf("    element build           %7.2f ms\n", build);
    std::printf("    render                  %7.2f ms\n", render);
    std::printf("    %-23s %7.2f ms  <- perceived switch cost\n",
                "TOTAL on UI thread", swap + rehyd + cfg + build + render);

    // 决定窗口是否读取的号码值得: UI-thread 成本比额表
    // THREAD 长度而不是 SCREEN 大小。
    // rehydrate_frozen 已设定了边框; 模式交换是一个动作加一个
    // 缓存 清空,O(1)两个 -ish ;其余为每 -visible-row.
    // 决定窗口是否读取的号码值得: UI-thread
    // 成本比额表 THREAD 长度而不是 SCREEN
    // 大小。
    {
        Model m;
        m.d.current = *loaded;
        app::detail::rehydrate_frozen(m);
        std::printf("\n  frozen: %zu entries, %zu rows, through %zu of %zu "
                    "messages\n", m.ui.frozen.size(), m.ui.frozen.row_total(),
                    m.ui.frozen_through, loaded->messages.size());
    }
    return 0;
}
