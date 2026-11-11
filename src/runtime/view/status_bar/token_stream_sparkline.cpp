#include "agentty/runtime/view/status_bar/token_stream_sparkline.hpp"
#include <maya/core/anim_clock.hpp>   // anim_now : 数据 帧 时钟

#include <algorithm>
#include <chrono>
#include <vector>

#include "agentty/runtime/view/palette.hpp"

namespace agentty::ui {

namespace {

// 按时间顺序(最老 ) 将 缓冲区 调出。
std::vector<float> ordered_rate_history(const StreamState& s) {
    std::vector<float> out;
    out.reserve(StreamState::kRateSamples);
    if (s.rate_history_full) {
        for (std::size_t i = 0; i < StreamState::kRateSamples; ++i) {
            const auto idx = (s.rate_history_pos + i) % StreamState::kRateSamples;
            out.push_back(s.rate_history[idx]);
        }
    } else {
        for (std::size_t i = 0; i < s.rate_history_pos; ++i)
            out.push_back(s.rate_history[i]);
    }
    return out;
}

} // 命名空间结束。

maya::TokenStreamSparkline::Config token_stream_sparkline_config(const Model& m) {
    const bool is_streaming = m.s.is_streaming() && m.s.active();

    auto hist = ordered_rate_history(m.s);
    auto now  = maya::anim_now();
    // 联系当前子的活跃 ctx -turn 现场柜台;
    // nullptr 表示 "no active stream"
    // 回到历史的火花线上
    const phase::Active* a = active_ctx(m.s.phase);
    bool ever_streamed =
        a && a->first_delta_at.time_since_epoch().count() != 0;
    long long ts_ms = ever_streamed
        ? std::chrono::duration_cast<std::chrono::milliseconds>(
              now - a->first_delta_at).count()
        : 0;
    double approx_tok = a ? static_cast<double>(a->live_delta_bytes) / 4.0 : 0.0;

    // 选择显示的速率: 活在 流式传输 (在250毫秒的温暖之后)
    // -up 所以第一个 帧 不是一个鸿沟 -by-tiny-time ,
    // 在最近样本中另作选择(这样数字在工具执行中不会衰减),
    // 0在任何数据之前。 流式传输 我们展示的是动画
    // -smoothed 价值——。 get() 自己 -ticks
    // 共享的 帧 钟表和保持 帧 滑翔时武装着——所以读取滑翔器
    // 而不是滑翔器 帧-to-frame.
    float disp_rate;
    if (is_streaming && ts_ms >= 250) {
        disp_rate = static_cast<float>(m.s.disp_rate.get());
    } else if (!hist.empty()) {
        disp_rate = hist.back();
    } else {
        disp_rate = 0.0f;
    }

    maya::TokenStreamSparkline::Config cfg;
    cfg.rate    = disp_rate;
    cfg.total   = static_cast<int>(approx_tok);
    cfg.history = std::move(hist);
    cfg.color   = highlight;
    cfg.live    = is_streaming;
    return cfg;
}

} // namespace agentty::ui
