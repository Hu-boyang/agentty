// stats_refresh_bench ——每笔多少钱? -frame 投影复制成本?
//
// Projection::refresh () 重建 `view_` 发件人
// `sealed_` 打开 每个 调用 ,并显示数据面板 调用 每张两次 帧 (在视图中,
// 在 StatsTab 状态更新函数。 事实拥有 ~6 双系列, 封顶为 256 个
// 条目加数个字符串 -keyed 统计,所以这个任务是一个深层的复制,
// 而不是一个memcpy。
//
// 这不是行为测试,它为人类打印数字。 NO_TEST
// 探测器可以搭配套房 无法搭配 -rot 默然无声.

#include "agentty/domain/stats/facts.hpp"
#include "agentty/domain/conversation.hpp"
#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/msg.hpp"
#include "agentty/runtime/view/panels.hpp"

#include <maya/render/canvas.hpp>
#include <maya/render/renderer.hpp>
#include <maya/style/theme.hpp>

#include <chrono>
#include <cstdio>
#include <string>

using namespace agentty;

namespace {

Message assistant_turn(int i) {
    Message m;
    m.role         = Role::Assistant;
    m.id           = MessageId{"m" + std::to_string(i)};
    m.served_model = ModelId{"claude-sonnet-4-5"};
    m.text         = "some assistant prose that is long enough to be realistic";
    Message::Telemetry t;
    t.input_tokens   = 1200;
    t.output_tokens  = 400;
    t.cache_read     = 15000;
    t.cache_creation = 200;
    t.ttft_ms        = 100;
    t.stream_ms      = 900;
    m.telemetry = t;
    return m;
}

}  // 命名空间结束。

int main() {
    using namespace std::chrono;

    // 长篇: 复制最大,
    // 以及 用户打开面板看事情发生时,
    Thread t;
    t.id = ThreadId{"bench"};
    for (int i = 0; i < 400; ++i) t.messages.push_back(assistant_turn(i));

    stats::Projection p;
    p.refresh(t);                       // 温暖: 封住所有的东西一次

    constexpr int kIters = 2000;
    auto t0 = steady_clock::now();
    for (int i = 0; i < kIters; ++i) {
        const auto& f = p.refresh(t);
        asm volatile("" :: "r"(&f) : "memory");
    }
    auto t1 = steady_clock::now();

    const double us =
        duration_cast<nanoseconds>(t1 - t0).count() / 1000.0 / kIters;
    std::printf("settled thread, %zu messages\n", t.messages.size());
    std::printf("  refresh()      : %.1f us\n", us);
    std::printf("  x2 per frame   : %.1f us\n", us * 2);
    std::printf("  at 30fps       : %.1f%% of a 33ms frame\n",
                us * 2 / 33000.0 * 100.0);

    // 预测只是a的一部分 帧。用户实际上等待的是
    // 整个面板:构建元素 源码树 铺平 涂平 量度 该 ,
    // 或者上面的数字是一个事实 一个无关
    // 紧要的事情。
    Model m;
    m.d.current = t;
    auto [opened, _] = app::update(std::move(m), Msg{OpenStats{}});
    m = std::move(opened);

    constexpr int kFrames = 300;
    maya::StylePool pool;
    maya::Canvas canvas(120, 60, &pool);

    auto t2 = steady_clock::now();
    for (int i = 0; i < kFrames; ++i) {
        auto el = ui::stats_panel(m);
        maya::render_tree(el, canvas, pool, maya::theme::native,
                          /*auto_height=*/true);
    }
    auto t3 = steady_clock::now();

    const double frame_us =
        duration_cast<nanoseconds>(t3 - t2).count() / 1000.0 / kFrames;
    std::printf("\nfull panel build + render at 120x60\n");
    std::printf("  per frame      : %.0f us\n", frame_us);
    std::printf("  budget at 60fps: 16600 us  (%.1f%% used)\n",
                frame_us / 16600.0 * 100.0);
    std::printf("  keypress->paint: %.0f us of input latency\n", frame_us);
    return 0;
}
