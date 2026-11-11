// SPDX-License-Identifier: Apache-2.0
//
// compaction_threshold_test.cpp - 自动 -compaction 触发数学。
//
// StreamState::compaction_threshold
// ()将模型上下文窗口转换为背景紧凑火的符号计数。 DEEP
// (kSoftFillPct = 95%) 所以我们尽量保持最
// 严格的现场环境 尽可能少的紧凑——每个紧凑是昂贵的 事件 (
// 它执行一个摘要请求) 和 重排该 提示词缓存 因此,
// 射击很少是使总的符号燃烧率低的原因。唯一的钳子是输出。
// -headroom FLOOR 所以下个回合的回答总是有空间的
//
// 锁定在此的 不变量 :
//   • 阈值=分钟(窗口的95%,窗口-20k);
//   · 190k的200k窗火,950k的1M窗火(深-不早);
//   • 产出-headroom层从未被侵犯;
//   · 更大的窗口 更高(从未更早)的绝对触发器;
//   • 0表示"window unknown, never fire"。

#include "agtest.hpp"

#include "agentty/domain/session.hpp"

#include <cassert>

using agentty::StreamState;

namespace {

int threshold_for(int context_max) {
    StreamState s;
    s.context_max = context_max;
    return s.compaction_threshold();
}

} // 命名空间结束。

TEST_CASE("compaction threshold") {
    constexpr int kFloor = StreamState::kMinOutputHeadroom;  // 20000
    constexpr int kPct   = StreamState::kSoftFillPct;        // 95

    // ── 200k窗口:95%=190k, 预备队=180k=190k < 180k?否 ─────────
    // 200k=1900;窗口-地板=180 000;min=180 000。
    // (在这么大的面积上, 头室地板绑起来, 给180k. )
    {
        assert(threshold_for(200000) == 180000);
        std::puts("threshold: 200k window fires at 180k (headroom-floor bound)");
    }

    // ── 400k窗口:95%=380k,备用=380k=380k – 380k ───────────────────
    {
        // 400k=380000的95%;窗口 - 20k=380 000;min=380 000。
        assert(threshold_for(400000) == 380000);
        std::puts("threshold: 400k window fires at 380k (95%)");
    }

    // ── 1M 窗口: 骑 DEEP 至950k,(千米),(千分之一) 不 压缩时间提前 ──────────────
    {
        // 1M=950000;窗口 - 20k=980000;min=950000.
        assert(threshold_for(1000000) == 950000);
        // 圣洁:这是很深的——已经过了一半,接近天花板。
        assert(threshold_for(1000000) > 900000);
        std::puts("threshold: 1M window rides deep to 950k (95%)");
    }

    // ── 2M窗口:95%=1.9M ────────────────────────────────────────────
    {
        assert(threshold_for(2000000) == 1900000);
        std::puts("threshold: 2M window fires at 1.9M (95%)");
    }

    // ── 产出-headroom 层层始终得到尊重 ────────────────────────
    {
        // 门槛永远不离开小于 kFloor 的窗口自由。
        for (int w : {50000, 100000, 200000, 500000, 1000000, 2000000}) {
            assert(threshold_for(w) <= w - kFloor);
        }
        // 在小窗口上,地板绑定:100k = 95% = 95k, 保留地= 80k.
        assert(threshold_for(100000) == 100000 - kFloor);
        std::puts("threshold: output-headroom floor always respected");
    }

    // ── 单调递增的 : 更大的窗口永不开火 SOONER (绝对的标志) ──
    {
        int prev = -1;
        for (int w : {100000, 200000, 400000, 1000000, 2000000}) {
            const int t = threshold_for(w);
            assert(t >= prev);
            prev = t;
        }
        std::puts("threshold: monotonic non-decreasing in window size");
    }

    // ── 未知窗口永不开火 ───────────────────────────────────────
    {
        assert(threshold_for(0) == 0);
        assert(threshold_for(-5) == 0);
        std::puts("threshold: unknown/zero window never triggers");
    }

    // ── 闲置 缓存-lapse 预览 -compaction ──────────────────────────────────
    // 安全网 一个 深层 -ride 价格会涨高 闲着
    // 过 缓存 TTL 上面有巨大的前缀,只有在 两者
    // 前缀很大,空闲的空隙接近 TTL ,而且从未在届
    // 会的第一次请求之前提出。
    {
        using namespace std::chrono;
        StreamState s;
        const auto t0 = steady_clock::time_point{} + hours{5};  // 任意基础

        // 至今没有发出任何请求 永远不要开火,无论 size/idle.
        assert(!s.should_compact_on_idle(t0 + hours{2}, 500000));

        s.last_wire_at = t0;

        // 大前缀+长闲(48+min) → FIRES.
        assert(s.should_compact_on_idle(t0 + StreamState::kIdleCompactAfter,
                                        300000));
        assert(s.should_compact_on_idle(t0 + minutes{55}, 300000));

        // 大型前缀但不变 ACTIVELY 工作(短期闲置)
        // □ 不 火——在现场会话期间不提前收紧.
        assert(!s.should_compact_on_idle(t0 + minutes{5}, 900000));
        assert(!s.should_compact_on_idle(t0 + minutes{30}, 900000));

        // 长期闲置但SMALL前缀 ~ 不值一回合的总结.
        assert(!s.should_compact_on_idle(t0 + hours{2},
                                         StreamState::kIdleCompactMinTokens - 1));

        // 恰好在Min-tokens边界+穿越空闲的缺口 – 火灾。
        assert(s.should_compact_on_idle(t0 + hours{2},
                                        StreamState::kIdleCompactMinTokens));
        std::puts("idle-compaction: fires only on large prefix + near-TTL idle");
    }
}
