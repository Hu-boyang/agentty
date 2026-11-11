// agentty::provider::anthropic —SSE 事件 解析器。
//
// 从 transport.cpp 中逐字提取（由
// anthropic_sse_golden_test 保护的行为）。拥有per-event
// 分派：simdjson content_block_delta快速路径，SSE 事件
// -kind封闭和+其编译-time双射证明，dispatch_event和feed_sse。
// StreamCtx 和共享的 dbg/debug_log 记录器位于 sse.hpp，
// 因此 run_stream_sync（位于 transport.cpp）可以驱动 数据流。纯 解析 — 无请求构建，
// 无网络。

#include "agentty/provider/anthropic/sse.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>
#include <simdjson.h>

#include "agentty/provider/error_class.hpp"

namespace agentty::provider::anthropic {

namespace {

using json = nlohmann::json;

// 快速路径：content_block_delta支配数据流卷（每个输出令牌一个）。
// simdjson 的 ondemand 在 -place 中遍历 字节，
// 获取我们需要的两个字符串，然后返回，而不会出现 按需加载 和 DOM。
// 如果出现任何意外情况（未知 delta.type），则返回呼叫者。
// 如果完全处理了事件，则返回真。
//
// `padded`指向SseFramer自己的累加器，这保证了wire::
// kSseSimdPadding可读尾随字节 (== SIMDJSON_PADDING)，
// 因此我们用ZERO对每个-frame副本进行原地迭代——旧的memcpy-into-simd_scratch舞蹈已经消失了。
// simd_scratch 仅作为 simdjson 解析器 拥有的工作 缓冲区 (
// ctx.simd_parser) 存在，而不是 载荷 副本。
//
// simdjson按需UNESCAPES字符串就位，改变`padded`(其为
// 呼叫者的`data`起别名)。所以结果是 tri-state，而不是 bool：
//
//   不可解析-iterate()/object-shape失败，BEFORE提取
//                  了任何字符串，因此`data`仍然是原始的，并且调用者可以安全地
//                  用nlohmann重新-parse它。
//   已识别——一个井-formed content_block_delta，
//                  其`type`我们不渲染（或无法读取）；已经被丢弃，
//                  并且 `data` 可能被弄脏 - 调用者不得 re-parse。
//                  匹配 nlohmann 分支，该分支也会默默地忽略未知类型。
//   已处理——发出增量；完毕。
//
// 这也使旧的双-parse退役：用于落入相同字节的第二个完整
// nlohmann 解析的non-text/tool增量。
enum class FastDelta { Unparseable, Recognized, Handled };
static_assert(wire::kSseSimdPadding == simdjson::SIMDJSON_PADDING,
              "wire::kSseSimdPadding must equal simdjson::SIMDJSON_PADDING so "
              "SseFramer's accumulator tail is a valid simdjson pad region");
FastDelta dispatch_content_block_delta_fast(StreamCtx& ctx, std::string_view data,
                                            char* padded) {
    if (!padded) return FastDelta::Unparseable;
    const std::size_t cap = data.size() + simdjson::SIMDJSON_PADDING;

    simdjson::ondemand::document doc;
    if (ctx.simd_parser.iterate(padded, data.size(), cap).get(doc))
        return FastDelta::Unparseable;

    simdjson::ondemand::object root;
    if (doc.get_object().get(root)) return FastDelta::Unparseable;

    // 读取 `index` 之前 `delta`。 simdjson On
    // -Demand 是一个向前-only 游标：一旦我们下降到 `delta`
    // 对象并使用其字段，根的剩余键将不再可访问。在每次 帧 Anthropic
    // 发出时，`index` 都在 `delta` 之前，因此将其带到
    // 这里不需要任何成本，并且可以供下面的工具 -delta 臂使用。
    //
    // -1意味着帧没有携带索引——符合的
    // content_block_delta没有携带索引，
    // 并且工具臂将其视为不可归因而不是猜测。
    std::int64_t block_index = -1;
    if (root["index"].get_int64().get(block_index)) block_index = -1;

    simdjson::ondemand::object delta;
    if (root["delta"].get_object().get(delta)) return FastDelta::Unparseable;

    // 从这里开始，simdjson可能转义为`padded`/`data`；
    // 在此之后永远不会返回 Unparseable —— 格式错误的 delta 被识别（丢弃）。
    std::string_view delta_type;
    if (delta["type"].get_string().get(delta_type)) return FastDelta::Recognized;

    if (delta_type == "text_delta") {
        std::string_view text;
        if (delta["text"].get_string().get(text)) return FastDelta::Recognized;
        ctx.sink(StreamTextDelta{std::string{text}});
        return FastDelta::Handled;
    }
    if (delta_type == "input_json_delta") {
        std::string_view partial;
        if (delta["partial_json"].get_string().get(partial)) return FastDelta::Recognized;
        // 这些字节属于哪个区块？ 帧 是这么说的—
        // —阅读它而不是假设最近打开的 调用。
        auto* blk = ctx.tool_at(static_cast<int>(block_index));
        if (!blk) {
            // 我们从未见过开放的区块的增量。删除是唯一诚实的
            // 举动：附加到其他一些 调用 会产生有效的
            // JSON 和无提示的错误工具调用。而是这样说。
            AGT_LOG(Wire, Warn, "anthropic.tool_delta_unattributable",
                    "index={} open_blocks={}", block_index,
                    ctx.tool_blocks.size());
            return FastDelta::Recognized;
        }
        ctx.sink(StreamToolUseDelta{ToolCallId{blk->id}, std::string{partial}});
        return FastDelta::Handled;
    }
    // 思维块与渲染没有任何关系，但它们ARE证明该模型正在积极工作。
    // 通过 StreamHeartbeat 冲击 状态更新函数
    // 的活跃时钟——如果没有这个，长推理过程（扩展思维模型可以
    // 在可见增量之间运行 60-120 秒）会使 状态更新函数
    // 的停顿 看门狗 跳闸，并引发虚假的 "stream stalled" 错误，
    // 即使 传输 是健康的，并且模型正在生成我们
    // 选择不生成 渲染 的思维令牌。
    if (delta_type == "thinking_delta") {
        // 捕获推理文本。默认（已编辑）自适应思维下的清空；
        // NON-empty 一旦请求要求显示：
        // "summarized"（^R 显示-reasoning 路径），
        // 这就是为可见推理块提供数据的内容。兼作活跃心跳 - 状态更新函数
        // 也在 Msg 上碰撞 last_event_at。
        std::string_view text;
        if (delta["thinking"].get_string().get(text)) return FastDelta::Recognized;
        ++ctx.thinking_deltas;
        ctx.sink(StreamThinkingDelta{std::string{text}, {}});
        return FastDelta::Handled;
    }
    if (delta_type == "signature_delta") {
        std::string_view sig;
        if (delta["signature"].get_string().get(sig)) return FastDelta::Recognized;
        ++ctx.thinking_deltas;
        ctx.sink(StreamThinkingDelta{{}, std::string{sig}});
        return FastDelta::Handled;
    }
    // Well-formed delta，我们不输入渲染 (forward-compat)。
    // nlohmann 分支也会丢弃这些 - 不要浪费第二个 解析 re-discovering。
    return FastDelta::Recognized;
}

// ── SSE 事件-kind 已关闭的金额 ─────────────────────────────────────
// Anthropic 发出的每个 事件 名称，
// 加上前向 -compat 情况的未知哨兵（Anthropic 在
// agentty 知道之前添加了一个新的 事件 — 我们默默地丢弃它，
// 而不是崩溃）。 枚举 是封闭的 分派 曲面；
// kSseEvents 表是从 传输 字符串 → 枚举 的单点转换。
// 添加新的事件=添加一个手臂+一行；如果两者不同步，
// 底部的双射证明就会导致构建失败。
//
// 先前：`name == "..."` strcmps的长if/else
// if链，新的事件默默地从末端脱落。
// 现在：kind_of_event() 查找一次，分派器 打开 枚举，
// 并且未知臂是显式放置站点。
enum class SseEventKind : std::uint8_t {
    Unknown,
    Ping,
    MessageStart,
    ContentBlockStart,
    ContentBlockDelta,
    ContentBlockStop,
    MessageDelta,
    MessageStop,
    Error,
};

struct SseEventSpec {
    SseEventKind     kind;
    std::string_view wire_name;
};

inline constexpr std::array kSseEvents = {
    SseEventSpec{SseEventKind::Ping,              "ping"},
    SseEventSpec{SseEventKind::MessageStart,      "message_start"},
    SseEventSpec{SseEventKind::ContentBlockStart, "content_block_start"},
    SseEventSpec{SseEventKind::ContentBlockDelta, "content_block_delta"},
    SseEventSpec{SseEventKind::ContentBlockStop,  "content_block_stop"},
    SseEventSpec{SseEventKind::MessageDelta,      "message_delta"},
    SseEventSpec{SseEventKind::MessageStop,       "message_stop"},
    SseEventSpec{SseEventKind::Error,             "error"},
};

[[nodiscard]] constexpr SseEventKind kind_of_event(std::string_view name) noexcept {
    for (const auto& s : kSseEvents)
        if (s.wire_name == name) return s.kind;
    return SseEventKind::Unknown;
}

// 编译-time双射：每个Kind臂(除了Unknown)在表中恰好有一行。
// 添加没有 wire_name 的新种类 - 或复制 wire_name
// - 会导致 static_assert 处的构建失败。
namespace sse_proofs {
consteval bool kinds_in_table() {
    constexpr SseEventKind kAll[] = {
        SseEventKind::Ping,
        SseEventKind::MessageStart,
        SseEventKind::ContentBlockStart,
        SseEventKind::ContentBlockDelta,
        SseEventKind::ContentBlockStop,
        SseEventKind::MessageDelta,
        SseEventKind::MessageStop,
        SseEventKind::Error,
    };
    if (std::size(kAll) != kSseEvents.size()) return false;
    for (auto k : kAll) {
        int hits = 0;
        for (const auto& s : kSseEvents) if (s.kind == k) ++hits;
        if (hits != 1) return false;
    }
    return true;
}
static_assert(kinds_in_table(),
              "SseEventKind and kSseEvents must be in bijection — every "
              "non-Unknown Kind needs exactly one row whose wire_name "
              "matches the Anthropic SSE event identifier");
consteval bool names_unique() {
    for (std::size_t i = 0; i < kSseEvents.size(); ++i)
        for (std::size_t j = i + 1; j < kSseEvents.size(); ++j)
            if (kSseEvents[i].wire_name == kSseEvents[j].wire_name) return false;
    return true;
}
static_assert(names_unique(), "duplicate wire_name in kSseEvents");
static_assert(kind_of_event("message_stop")     == SseEventKind::MessageStop);
static_assert(kind_of_event("who_knows")        == SseEventKind::Unknown);
} // namespace sse_proofs

void dispatch_event(StreamCtx& ctx, std::string_view name, std::string_view data,
                    char* padded) {
    if (data.empty() || data == "[DONE]") return;
    // `wire`通道上的Per-event转储。宏的门在任何格式化之前
    // 都是short-circuits，因此禁用的日志花费一个原子加载，
    // 并且在此高频路径中没有字符串构造。
    AGT_LOG(Wire, Trace, "anthropic.event", "event={} data={}", name, data);

    // 高频路径首先——在流式传输回合期间事件的〜95%。
    // simdjson 快速路径 解析 位于成帧器的衬垫 缓冲区 上方。
    // 只有真正的 UNPARSEABLE 帧 (在任何弦被触及之前 解析 错误，
    // 因此 `data` 仍然是原始的）才落入 nlohmann 手中；
    // 已处理和已识别（well-formed，但未渲染类型）均返回 -
    // re-parsing，已理解的增量 simdjson 纯粹是浪费。
    if (name == "content_block_delta") {
        switch (dispatch_content_block_delta_fast(ctx, data, padded)) {
            case FastDelta::Handled:
            case FastDelta::Recognized: return;
            case FastDelta::Unparseable: break;   // 落入nlohmann
        }
    }

    const SseEventKind kind = kind_of_event(name);

    // ping 事件是心跳保活——Anthropic将它们交错，
    // 因此代理不会杀死long-poll（通常每10-15秒）。
    // 作为 StreamHeartbeat 前进，因此 状态更新函数 的失速 看门狗 可以区分
    // "wire is silent but alive" 和 "wire is wedged."
    // 状态更新函数 的 处理函数 仅碰撞 last_event_at — 没有 渲染，没有状态。
    if (kind == SseEventKind::Ping) { ctx.sink(StreamHeartbeat{}); return; }

    json j;
    try { j = json::parse(data); } catch (const std::exception& e) {
        // 两个解析器都失败了——这个帧即将成为DROPPED。默默地这样做
        // 永远是不好的：丢失的 content_block_start
        // 读作 "the model stopped talking"，
        // 而不是 解析 错误。警告：发布时默认捕获+保存在坠机飞行记录器中。
        AGT_LOG(Wire, Warn, "anthropic.frame_unparseable",
                "event={} err={} bytes={} head={}",
                name, e.what(), data.size(), data.substr(0, 256));
        return;
    }

    switch (kind) {
        case SseEventKind::Unknown:
            // Forward-compat接收器：添加了我们尚不知道的新
            // 事件 Anthropic。丢弃的是正确的 BEHAVIOUR
            // （未知的 事件 通常是结构性的），但它一定不能是不可见的——上面
            // 的 传输 跟踪转储仅在启用跟踪时才存在，
            // 而 "a new event we ignored" 正是现场报告需要命名的。
            // 进行调试，以便共享 AGENTTY_LOG=debug 日志显示它。
            AGT_LOG(Wire, Debug, "anthropic.unknown_event",
                    "event={} bytes={}", name, data.size());
            break;

        case SseEventKind::Ping:
            // 在上面的快速路径中处理；在交换机中无法访问。
            break;

        case SseEventKind::MessageStart: {
            ctx.sink(StreamStarted{});
            if (j.contains("message") && j["message"].contains("usage")) {
                const auto& u = j["message"]["usage"];
                StreamUsage su;
                su.input_tokens                = u.value("input_tokens", 0);
                su.output_tokens               = u.value("output_tokens", 0);
                su.cache_creation_input_tokens = u.value("cache_creation_input_tokens", 0);
                su.cache_read_input_tokens     = u.value("cache_read_input_tokens", 0);
                ctx.sink(su);
            }
            break;
        }

        case SseEventKind::ContentBlockStart: {
            auto block = j.value("content_block", json::object());
            auto type = block.value("type", "");
            if (type == "tool_use") {
                // 将块记录在传输给它的索引处。
                // 该索引涉及每个增量和停止点，因此它是该块
                // 整个生命周期的标识。
                const int idx = j.value("index", -1);
                StreamCtx::ToolBlock b;
                b.index = idx;
                b.id    = block.value("id", "");
                b.name  = block.value("name", "");
                // 重述索引意味着该时隙的前一个块已经
                // 结束并且服务器重新使用它。
                // 替换而不是累积，因此长循环不能增长无界列表。
                if (auto* prior = ctx.tool_at(idx)) *prior = b;
                else ctx.tool_blocks.push_back(b);
                ctx.sink(StreamToolUseStart{ToolCallId{b.id}, ToolName{b.name}});
            } else if (type == "text") {
                // 将文本块标记为打开，以便其匹配的content_block_stop
                // 发出StreamTextBlockClosed。
                ctx.text_block_open = true;
            } else if (type == "thinking") {
                // NEW思维块正在启动。交错思维为每个响应
                // 生成几个独立的-signed块；
                // 状态更新函数 必须在该块的增量到达之前密封前一个（文本、
                // 签名）对，否则签名会交叉 -wired
                // 并重放 400 秒。
                ctx.sink(StreamThinkingDelta{{}, {}, /*block_boundary=*/true});
            } else if (type == "redacted_thinking") {
                // 安全-redacted思考：整个块以不透明的`data` 载荷的形式
                // 到达HERE（后面没有增量）。必须在本回合的 tool_use 或
                // 后续 -up 400 秒（
                // "Expected thinking or redacted_thinking but found tool_use"）（预期 thinking 或 redacted_thinking，却遇到 tool_use。）
                // 之前逐字重播。也是一个活性证明。
                ++ctx.thinking_deltas;
                StreamThinkingDelta e;
                e.block_boundary = true;
                e.redacted_data  = block.value("data", "");
                ctx.sink(std::move(e));
            }
            break;
        }

        case SseEventKind::ContentBlockDelta: {
            // 上述simdjson快速路径处理常见情况；
            // 对于任何无法做到的事情，都回到 nlohmann 路径。
            auto delta = j.value("delta", json::object());
            auto type = delta.value("type", "");
            if (type == "text_delta") {
                ctx.sink(StreamTextDelta{delta.value("text", "")});
            } else if (type == "input_json_delta") {
                // 与上述快速路径相同的归属规则。该手臂仅在
                // simdjson 拒绝 帧 时运行，因此它必须得出
                // 相同的结论 - "which call owns these bytes"
                // 的两个拼写是发散类的开始
                // 方式。
                const int idx = j.value("index", -1);
                if (auto* blk = ctx.tool_at(idx)) {
                    ctx.sink(StreamToolUseDelta{
                        ToolCallId{blk->id}, delta.value("partial_json", "")});
                } else {
                    AGT_LOG(Wire, Warn, "anthropic.tool_delta_unattributable",
                            "index={} open_blocks={} path=slow",
                            idx, ctx.tool_blocks.size());
                }
            } else if (type == "thinking_delta") {
                // 捕获推理文本以供重播；也是一个活跃信号。
                ++ctx.thinking_deltas;
                ctx.sink(StreamThinkingDelta{delta.value("thinking", ""), {}});
            } else if (type == "signature_delta") {
                ++ctx.thinking_deltas;
                ctx.sink(StreamThinkingDelta{{}, delta.value("signature", "")});
            }
            break;
        }

        case SseEventKind::ContentBlockStop: {
            // 止损点携带其关闭的块的索引。关闭 该 一个 —
            // 对于并行块，关闭 "the current tool"
            // 会结束最后打开的那个，在交错输出上是错误
            // 的 调用，并让正确的那个永远打开。
            const int stop_idx = j.value("index", -1);
            if (auto* blk = ctx.tool_at(stop_idx)) {
                ctx.sink(StreamToolUseEnd{ToolCallId{blk->id}});
                std::erase_if(ctx.tool_blocks, [stop_idx](const auto& b) {
                    return b.index == stop_idx;
                });
            } else if (ctx.text_block_open) {
                // 刚刚结束的散文块——这个ALWAYS先于tool_use
                // content_block_start，
                // 所以它是最早的权威"model finished typing"信号。
                // 该视图将 逐步显示 光标排到其边缘，
                // 因此工具卡上的强制硬-snap 是 no-op（无突发）。
                ctx.text_block_open = false;
                ctx.sink(StreamTextBlockClosed{});
            }
            break;
        }

        case SseEventKind::MessageDelta: {
            if (j.contains("usage")) {
                const auto& u = j["usage"];
                StreamUsage su;
                su.input_tokens                = u.value("input_tokens", 0);
                su.output_tokens               = u.value("output_tokens", 0);
                su.cache_creation_input_tokens = u.value("cache_creation_input_tokens", 0);
                su.cache_read_input_tokens     = u.value("cache_read_input_tokens", 0);
                ctx.sink(su);
            }
            if (j.contains("delta") && j["delta"].contains("stop_reason")
                && j["delta"]["stop_reason"].is_string()) {
                ctx.stop_reason = parse_stop_reason(
                    j["delta"]["stop_reason"].get<std::string_view>());
            }
            break;
        }

        case SseEventKind::MessageStop: {
            // 关闭服务器保持打开的任何内容。抽签，
            // 所以顺序并不重要，也没有什么可错的——与响应
            // 方言中的 OpenCalls::drain
            // () 的推理相同。否则，没有停止的块将永远留下
            // 工具 调用 待处理。
            for (const auto& b : ctx.tool_blocks)
                ctx.sink(StreamToolUseEnd{ToolCallId{b.id}});
            ctx.tool_blocks.clear();
            ctx.sink(StreamFinished{ctx.stop_reason});
            ctx.terminated = true;
            break;
        }

        case SseEventKind::Error: {
            // Mid-stream SSE 错误 事件。 传输 载荷 只是
            // `error.type` + `error.message` -
            // Anthropic 在这里没有出现 重试-After（这是 HTTP
            // -header 的东西，我们已经通过了 头文件 阶段进入 SSE 正文）。
            // 保留 retry_after 未设置，让 运行时 恢复到其自己
            // 的时间表。
            auto err = j.value("error", json::object());
            // 传输 带类型的 这对于我们来说（
            // `rate_limit_error`，`overloaded_error`，
            // ……）。将其转换为等效的 HTTP 状态，
            // 以便 状态更新函数 通过经过验证的分类（HttpError）
            // 表进行分类，而不是嗅探人类消息 - 这是一个可重试的重载，
            // 我们无法识别其散文，否则将被读取为 终端 并杀死回合。
            // 0=无法识别的类型，其保留现有字符串路径。
            const int status =
                provider::wire_error_type_status(err.value("type", ""));
            AGT_LOG(Wire, Warn, "anthropic.error_event",
                    "type={} status={} msg={}", err.value("type", ""),
                    status, err.value("message", ""));
            ctx.sink(StreamError{err.value("message", "unknown error"),
                                 std::nullopt, status});
            ctx.terminated = true;
            break;
        }
    }
}

} // 命名空间结束。

void feed_sse(StreamCtx& ctx, const char* data, std::size_t len) {
    // Per-network-read边界标记。仅靠冗长的
    // `<< event=...` 行无法区分突发的 传输（许多在
    // 一个 中交付的 text_deltas 在间隙后读取）和 渲染
    // -loop 停顿（增量稳定滴落但屏幕滞后）。该行用 字节 长度标记
    // 每次读取，因此 下一个 `-- chunk` 之前的 `<< event=`
    // 行的计数恰好是一起到达的 SSE 事件 的数量。
    AGT_LOG(Wire, Trace, "anthropic.chunk", "bytes={}", len);
    // 共享成帧器拥有字节 缓冲区、`event:`/`data:`累积、
    // multi-line数据连接、4MiB溢出上限以及
    // 分摊缓冲区压缩。我们仅分派各完成事件。
    ctx.sse.feed(data, len, [&](std::string_view name, std::string_view payload,
                                char* padded) {
        dispatch_event(ctx, name, payload, padded);
    });
}

} // namespace agentty::provider::anthropic
