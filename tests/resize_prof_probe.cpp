// resize_prof_probe - 什么样的 终端 RESIZE 实际成本?
//
// 宽度变化是昂贵的种类: maya 无效的上一个 存储单元 网
// 格(每行的包头移动)和 HardResets ,所以下一个 帧
// 重绘 整个冰冻帆布——最多 frozen_row_budget
// ()行,即三观门.
//
// A 线程 切换 渲染 ~2 个屏幕 一次 调整大小 重
// 绘 ~3 视图,拖动会发出每个列的调整大小。此度量每列
// -resize 实际成本 终端 大小,分拆,答案是 对应的 部分来修正,
// 而不是仅仅 "it's slow".
//
//   ./build/agentty_standalone_tests resize_prof_probe <thread> [rows]
//
// 接受 . 接受 接受 的 接受 , 接受 同意 的 同意 , 同意 接受。 JSON 或.jsonl. 无论据 = 不 -op 过.

#include <agentty/domain/session.hpp>
#include <agentty/io/persistence.hpp>
#include <agentty/io/thread_log.hpp>
#include <agentty/runtime/model.hpp>
#include <agentty/runtime/view/thread/conversation.hpp>
#include <agentty/runtime/app/update/internal.hpp>

#include <maya/render/renderer.hpp>
#include <maya/widget/conversation.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace agentty;

namespace {

using clk = std::chrono::steady_clock;

double ms_since(clk::time_point t) {
    return std::chrono::duration<double, std::milli>(clk::now() - t).count();
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v.empty() ? 0.0 : v[v.size() / 2];
}

} // 命名空间结束。

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("resize_prof_probe: no thread given, nothing to do.\n"
                    "usage: %s <thread.json|thread.jsonl> [rows]\n", argv[0]);
        return 0;
    }
    const fs::path p = argv[1];
    const int rows = (argc > 2) ? std::atoi(argv[2]) : 60;

    std::optional<Thread> loaded;
    if (p.extension() == ".jsonl") {
        if (auto log = ThreadLog::open_path(p)) loaded = log->load_thread();
    } else {
        if (auto r = persistence::load_thread_file(::agentty::IoAccess::grant(), p)) loaded = std::move(*r);
    }
    if (!loaded) {
        std::fprintf(stderr, "load failed: %s\n", p.string().c_str());
        return 1;
    }

    Model m;
    m.d.current = std::move(*loaded);
    app::detail::rehydrate_frozen(m);

    std::printf("%s: %zu messages\n", p.filename().string().c_str(),
                m.d.current.messages.size());
    std::printf("  seeded canvas: %zu entries, %zu rows\n",
                m.ui.frozen.size(), m.ui.frozen.row_total());

    // Model 员额 -paint 修剪,因为调整大小时从未
    // 看到过种子布。 rehydrate_row_budget ()行;
    // 第一真经. 帧 画它们(即开关使用户感觉成本高,
    // 行滚动成原生) 回滚缓冲区 ),然后是滴答臂进入
    // meta.cpp 修剪为 frozen_row_budget
    // ()一旦每个街区都有记录的高度。 帧 继续画布是 SMALL 其中一个,
    // 这就是后来的每个调整大小 -lays-out.
    //
    // 头没漆,所以记录高处 渲染器 将(分类账的可证明
    // 性门拒绝投放一个从未记录过高度的区块 -- --
    // 见 scrollback_ledger.hpp).
    for (std::size_t k = 0; k < m.ui.frozen.size(); ++k)
        m.ui.frozen.record_paint(k, static_cast<int>(m.ui.frozen.block_rows(k)));
    (void)app::detail::trim_frozen_if_oversized(m);

    std::printf("  frozen canvas: %zu entries, %zu rows (terminal %d rows)"
                "  <- steady state, post-trim\n",
                m.ui.frozen.size(), m.ui.frozen.row_total(), rows);

    // 缩放是 WIDTH 更改: 每个缓存元素必须重新 -layout
    // 以新的宽度表示,以及 maya 组件 缓存 宽度键,
    // 所以没有东西被重用。 换位宽度会显示拖动模式, 其中宽度不会重复。
    std::printf("\n  per-frame cost at a NEW width (what a resize pays):\n");
    for (int w : {120, 100, 80, 160}) {
        std::vector<double> build_v, render_v;
        for (int i = 0; i < 5; ++i) {
            // 每个迭代都有一个新的宽度,这样缓存就打不
            // 中——拖动实际产生的悲观的例.
            const int width = w + i;
            auto t = clk::now();
            auto cfg = ui::conversation_config(m);
            auto el  = maya::Conversation{std::move(cfg)}.build();
            build_v.push_back(ms_since(t));

            t = clk::now();
            const std::string out = maya::render_to_string(el, width);
            render_v.push_back(ms_since(t));
        }
        std::printf("    width %3d: build %6.2f ms  render %6.2f ms  "
                    "total %6.2f ms\n",
                    w, median(build_v), median(render_v),
                    median(build_v) + median(render_v));
    }

    // 电话 相同 宽度 两次: 这就是组件 缓
    // 存 两者的间隔就是 大小调整后会丢掉的
    std::printf("\n  same width twice (cache warm) \u2014 the steady state:\n");
    {
        std::vector<double> warm;
        auto cfg0 = ui::conversation_config(m);
        auto el0  = maya::Conversation{std::move(cfg0)}.build();
        (void)maya::render_to_string(el0, 120);
        for (int i = 0; i < 5; ++i) {
            auto t = clk::now();
            auto cfg = ui::conversation_config(m);
            auto el  = maya::Conversation{std::move(cfg)}.build();
            const std::string out = maya::render_to_string(el, 120);
            warm.push_back(ms_since(t));
        }
        std::printf("    width 120: %6.2f ms\n", median(warm));
    }

    return 0;
}
