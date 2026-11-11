#pragma once
// agentty::provider::anthropic — SSE 解析内部结构(
// 在transport.cpp的run_stream_sync/parse_sse_for_test和sse.cpp的解析器定义之间共享)。
// INTERNAL 头文件：不属于公共提供商表面的一部分。
//
// 每次 SSE 帧 变异（StreamCtx)时，保存 流式传输 上下文、
// feed_sse 入口点、以及由 两者、解析器 和请求驱动程序使用的 env-gated
// 调试记录器（dbg/debug_log)，因此它作为内联存在于此处以避免交叉-TU 定义。
// dispatch_event 和 simdjson 快速路径对 sse.cpp 保持
// 私有 — run_stream_sync 仅对 调用 feed_sse 保持私有。

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <string>

#include <simdjson.h>

#include "agentty/domain/catalog.hpp"        // StopReason
#include "agentty/provider/debug.hpp"        // 传输转储→logx `wire`通道
#include "agentty/provider/provider.hpp"     // EventSink
#include "agentty/provider/wire.hpp"         // wire::SseFramer
#include "agentty/runtime/msg.hpp"
#include "agentty/util/env.hpp"

namespace agentty::provider::anthropic {

// 由 解析器 (每个 -event 跟踪）和 run_stream_sync (
// 请求+状态跟踪）共享 — 内联，因此 编译单元 都看到一个定义。
// 传输转储经过ONE结构化日志（logx `wire`通道，
// 参见provider/debug.hpp)：在非-release构建中默认捕获，
// 并且在发布构建中通过AGENTTY_LOG=传输=trace捕获。 调用站点直接使用AGT_LOG。
using provider::debug_log;

// Per-stream解析状态。一个实例的生存时间为单个 /v1/messages
// 数据流；每个 SSE 帧 都会通过 dispatch_event（在 sse.cpp
// 中）突变它。 run_stream_sync 构建它，连接 ctx.sink，
// 然后驱动每个网络块 feed_sse 并读取之后的 终端 字段。
struct StreamCtx {
    EventSink sink;
    wire::SseFramer sse;
    // ── Tool-use 块,按键 传输 自己的块索引 ──────────
    //
    // Anthropic将`index`放在EVERY content_block_*帧上，
    // 并且该索引是块在其整个生命周期中的标识：开始、每个增量、
    // 停止。这曾经是一个 `current_tool_id` 插槽，
    // 增量路由到最后打开的 调用。
    //
    // 这是来自 docs/TOOL_CALL_ATTRIBUTION 的
    // `latest_tool_item` 形状——这个形状在 openai-python、
    // opik、strands、zed 中作为 bug 发布，在这里两次。
    // 当服务器严格一次发出一个块时，它恰好是安全的，就像 Claude 今天
    // 所做的那样。 "Happens to be safe today" 不是财产；
    // 这是对服务器的发射顺序的赌注，而该赌注的输方是一个 调用 的
    // 参数附加到另一个仍然有效的 JSON 的参数上，默默地发送。
    //
    // 因此，使用协议已经提供给我们的身份。 Small
    // -N 线性扫描：一个回合有几个并行块，扫描 2-4 个
    // 条目会击败哈希。
    struct ToolBlock {
        int         index = -1;   // 传输的content_block索引
        std::string id;
        std::string name;
    };
    std::vector<ToolBlock> tool_blocks;

    // 在`index`处找到打开的工具块，或者nullptr。命名而不是内联，
    // 因此增量可以在 一个 位置解析其所有者，
    // 并且没有 "the most recent one" 的拼写。
    [[nodiscard]] ToolBlock* tool_at(int index) noexcept {
        for (auto& b : tool_blocks)
            if (b.index == index) return &b;
        return nullptr;
    }
    // 当 TEXT 内容块在 传输 上（在其 content_block_start
    // 和 content_block_stop 之间）打开时确实如此。
    // 让 ContentBlockStop 在每个文本块中精确地发出 StreamTextBlockClosed 一次，
    // 并且仅针对文本（不是 tool_use / 思维）块 — 视图将其
    // 用作最早的 "prose finished" 信号，以在工具卡出现之前耗
    // 尽 逐步显示。
    bool text_block_open = false;
    // 终端-event 跟踪 — 必须触发 finished/errored 中的一个。
    bool terminated = false;
    // 从 message_delta 藏起来，以便我们可以将其交给 StreamFinished。
    // 让 状态更新函数 区分 "natural end" / "tool_use" 和
    // "max_tokens"（这会导致 in-flight tool_use 块被截断）。
    // 在 传输 边界处解析为 带类型的 枚举 — string-compare 仅存在于此处，
    // 而不存在于 状态更新函数 中。
    StopReason stop_reason = StopReason::Unspecified;
    // simdjson 解析器是有状态的，而缓存是跨iterate() 调用
    // 的划痕缓冲区——每个数据流重复使用一个可以避免每个SSE 帧的malloc。
    // 载荷不再需要单独的填充副本：SseFramer的累加器已经携带
    // simd填充末尾，因此我们就地迭代其缓冲区(参见
    // dispatch_content_block_delta_fast)。
    simdjson::ondemand::parser simd_parser;
    // 诊断：计算发出的模型的thinking-block增量，以便我们可以在调试
    // 日志中区分"model is reasoning silently"和
    // "wire is stalled"。当 数据流 完成时浮出水面。
    int thinking_deltas = 0;
};

// 馈送一个网络读取值SSE 字节。不拥有任何东西：在 ctx.sse (
// wire::SseFramer) 中进行帧+缓冲；每个完整的 事件 均通过
// dispatch_event (sse.cpp) 调度。在 sse.cpp 中定义。
void feed_sse(StreamCtx& ctx, const char* data, std::size_t len);

} // namespace agentty::provider::anthropic
