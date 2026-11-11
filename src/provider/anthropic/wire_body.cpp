// agentty::provider::anthropic — 传输 正文 建造者。
//
// 从 transport.cpp 中逐字提取（字节-identity 由 wire_golden_test
// 保护）。这是 request-body 序列化程序：流式传输 string-backed
// message-array writer（json_write_* 帮助程序、CachePin 断点模型、
// per-block writer）和 messages_json_string() /
// build_messages()。纯粹的 序列化 — 没有 SSE，没有网络，没有 StreamCtx。
//
// 作为其自己的TU保留，因此transport.cpp的高频路径的最大
// 块存在于集中模块中，与ChatGPT提供商的分割相匹配。

#include "agentty/provider/anthropic/transport.hpp"

#include "agentty/provider/wire.hpp"            // wire::scrub_utf8/cap_tool_result_aged
#include "agentty/provider/msg_shared.hpp"      // wire::is_assistant_with_results
#include "agentty/provider/wire_supersede.hpp"  // wire::superseded_read_ids/kSupersededReadPointer
#include "agentty/runtime/composer_attachment.hpp"  // attachment::expand
#include "agentty/util/base64.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace agentty::provider::anthropic {

using nlohmann::json;
using wire::scrub_utf8;

// ── 流式传输 字符串-backed 消息-array 撰稿人 ─────────────────────────
//
// 通过nlohmann::json构建消息数组是请求高频路径中最大的隐藏分配。
// `{"input", tc.args.is_object() ? tc.args : json::object()}`
// 初始值设定项-list 深-copies tc.args；对于 `write` 工具，
// 其 `content` 字段是 1 MiB 文件 正文，这是一个 1
// MiB 递归 JSON 克隆，后跟另一个 1+ MiB 分配（当
// `body.dump()` re-serializes 时）。每个请求两份大副本，
// 每 重试 再次付款。
//
// `messages_json_string`将消息数组直接写入std::string、
// 缓冲区、JSON-escaping内联。胜利落在 tc.args 上：
// tc.args_dump()已经缓存序列化形式（由权限卡的视图使用），
// 因此我们将那些字节逐字拼接到"input"字段中。没有克隆，
// 没有re-parse，没有re-dump。对于不可恢复的边缘情况
// （不知何故丢失其转储 缓存 的 args 对象），我们回退到
// on-demand 转储，而不是通过 JSON 进行复制。
//
// 缓存-breakpoint固定（改变最后+第二个
// -to-last消息的最后内容块的`pin_last_block`
// 帮助器）现在在写入期间内联完成——我们首先对消息进行计数，
// 然后提前知道哪些是pin-eligible消息。
namespace {

// 每当辅助消息携带ANY tool_calls时为真。 Anthropic
// 要求在下一条消息中每个 `tool_use` 块后面都跟有一个
// 匹配的 `tool_result` — 发送没有配对的 tool_use
// 将返回 HTTP 400 (
// "`tool_use` ids were found without `tool_result` blocks immediately after")，（发现 tool_use 标识，但后方没有紧接对应 tool_result 块。）
// 并且由于在后续的每个回合中都会重播损坏的记录，
// 因此会话会被楔入。因此，只要有 tool_use 需要配对，
// 我们就会发出 follow-up 用户轮流，即使其中一些仍
// 处于非 -terminal 状态（待处理/已批准/正在运行）。
// 非-terminal分支获得合成占位符结果下游，因此传输保持有效；
// in-memory ToolUse 状态保持不变。
[[nodiscard]] inline bool is_assistant_with_results(const Message& m) noexcept {
    return wire::is_assistant_with_results(m);
}

// 当且仅当消息携带至少一个可发送图像时为真。代表共享 SSOT (
// wire::has_wire_message_image)，因此空-bytes 跳过、
// 大小上限和角色门位于跨所有方言的 一个 位置。 `max_side` 是请求-wide
// 上限（参见wire::wire_image_count）。
[[nodiscard]] inline bool has_wire_image(const Message& m, unsigned max_side) noexcept {
    return wire::has_wire_message_image(m, max_side);
}

// 字节不能不受影响地通过JSON字符串文字。
// 其他所有内容（可打印的ASCII和每个UTF-8延续字
// 节)被逐字复制，因此逃生者可以一次移动它们中的整个RUNS。
[[nodiscard]] inline const bool* json_escape_table() noexcept {
    static const auto t = [] {
        std::array<bool, 256> v{};
        for (int i = 0; i < 0x20; ++i) v[static_cast<std::size_t>(i)] = true;
        v[static_cast<unsigned char>('"')]  = true;
        v[static_cast<unsigned char>('\\')] = true;
        return v;
    }();
    return t.data();
}

void json_write_escaped_string(std::string& out, std::string_view s) {
    // 复制转义之间的长段，每个附加一个，而不是每个 字节 一个
    // push_back。 Tool 输出和文件内容几乎完全是纯 字节，
    // 因此这几乎是每次运行的 memcpy；整个 正文 编码大约有
    // 25% 的折扣，每轮都需要长 线程 支付。
    // per-byte 版本的输出为 字节-identical。
    //
    // 旧的 per-call `out.reserve(out.size() + s.size() + 2)`
    // 已经消失了：它在 SHARED 上运行，
    // 每个字符串累积 缓冲区 一次，因此在一千个 -message
    // 正文 上，它重新-asked 以获得已经存在的容量（并且，
    // 当它确实增长时，增长到恰好需要的大小而不是几何形状 —
    // 击败了保持追加循环线性的摊销增长）。呼叫者预先保留 正文。
    static const bool* escape = json_escape_table();
    out.push_back('"');
    std::size_t run = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (!escape[c]) { ++run; continue; }
        if (run) { out.append(s.data() + i - run, run); run = 0; }
        switch (c) {
            case '"':  out.append("\\\"", 2); break;
            case '\\': out.append("\\\\", 2); break;
            case '\b': out.append("\\b",  2); break;
            case '\f': out.append("\\f",  2); break;
            case '\n': out.append("\\n",  2); break;
            case '\r': out.append("\\r",  2); break;
            case '\t': out.append("\\t",  2); break;
            default: {
                // \u00XX为剩余控制字节。
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out.append(buf, 6);
                break;
            }
        }
    }
    if (run) out.append(s.data() + s.size() - run, run);
    out.push_back('"');
}

void json_write_field(std::string& out, std::string_view key,
                      std::string_view value, bool& first) {
    if (!first) out.push_back(',');
    first = false;
    json_write_escaped_string(out, key);
    out.push_back(':');
    json_write_escaped_string(out, value);
}

// 将原始 pre-serialized JSON 拼接到值槽中（无转义）。
void json_write_raw_field(std::string& out, std::string_view key,
                          std::string_view raw_value, bool& first) {
    if (!first) out.push_back(',');
    first = false;
    json_write_escaped_string(out, key);
    out.push_back(':');
    out.append(raw_value);
}

void json_write_bool_field(std::string& out, std::string_view key,
                           bool v, bool& first) {
    json_write_raw_field(out, key, v ? "true" : "false", first);
}

// 用于 提示词 缓存的 缓存-control 标记。 编译-time 字符串，因此我们不会
// 在每个断点上支付 re-serialization 费用。两个 TTL：
//   kCacheCtlJsonRaw —默认 5-minute 临时。用于ROLLING
//     断点（最新消息），无论如何它每回合都会改变。
//   kCacheCtl1hJsonRaw — 1-hour 临时（需要beta_extended_cache_ttl）。
//     用于 STABLE 断点（对话 -prefix 锚点），
//     因此长空闲窗口不会强制对整个前缀进行完整的 -price re-cache。
constexpr std::string_view kCacheCtlJsonRaw   = R"({"type":"ephemeral"})";
constexpr std::string_view kCacheCtl1hJsonRaw = R"({"type":"ephemeral","ttl":"1h"})";

// 缓存选择TTL为断点。 NotPinned = 根本没有cache_control。
enum class CachePin : std::uint8_t { NotPinned, Ttl5m, Ttl1h };

[[nodiscard]] constexpr std::string_view cache_ctl_for(CachePin p) noexcept {
    switch (p) {
        case CachePin::Ttl1h: return kCacheCtl1hJsonRaw;
        case CachePin::Ttl5m: return kCacheCtlJsonRaw;
        default:              return {};
    }
}

void json_write_cache_control(std::string& out, CachePin pin, bool& first) {
    if (pin == CachePin::NotPinned) return;
    json_write_raw_field(out, "cache_control", cache_ctl_for(pin), first);
}

void write_text_block(std::string& out, std::string_view text, CachePin pin) {
    out.push_back('{');
    bool first = true;
    json_write_field(out, "type", "text", first);
    json_write_field(out, "text", text, first);
    json_write_cache_control(out, pin, first);
    out.push_back('}');
}

// Anthropic图像内容块：
//   {"type":"image","source":{"type":
//                              "base64", "media_type":"image/png",
// "data":"..."}} `data` 是标准 RFC-4648 base64 (不
// base64url)。我们在写入时对 字节 进行一次编码 - 将它们保留在
// `ImageContent.bytes` 中可以避免运行模型状态下 +33% 的 内存 开销。
void write_image_block(std::string& out, const ImageContent& img,
                       CachePin pin) {
    out.push_back('{');
    bool first = true;
    json_write_field(out, "type", "image", first);
    if (!first) out.push_back(',');
    first = false;
    out.append(R"("source":{"type":"base64",)");
    out.append(R"("media_type":)");
    json_write_escaped_string(out, wire::wire_media_type(img));
    out.append(R"(,"data":)");
    json_write_escaped_string(out, img.base64());
    out.push_back('}');
    json_write_cache_control(out, pin, first);
    out.push_back('}');
}

void write_tool_use_block(std::string& out, const ToolUse& tc, CachePin pin) {
    out.push_back('{');
    bool first = true;
    json_write_field(out, "type", "tool_use", first);
    json_write_field(out, "id",   tc.id.value, first);
    json_write_field(out, "name", tc.name.value, first);
    // 逐字拼接缓存的参数转储。 args_dump() 保证
    // 有效的 JSON-object 字符串（最小 "{}"，
    // 绝不为空）；如果出于任何原因 缓存 处于意外形状（防御性 -
    // 在实践中不应该开火），则退回到新的转储处。
    std::string_view dump = tc.args_dump();
    std::string fallback;
    if (dump.empty() || dump.front() != '{') {
        fallback = tc.args.is_object() ? tc.args.dump() : std::string{"{}"};
        dump = fallback;
    }
    json_write_raw_field(out, "input", dump, first);
    json_write_cache_control(out, pin, first);
    out.push_back('}');
}

// Age-tiered工具-result 传输预算(Anthropic "tool result clearing")。
// 完整成绩单为不可变；只有每个 tool_result 的 传输 副本的大小
// 取决于 RECENT 和 调用 的大小。该策略——预算、新近度窗口、
// 错误-never-fade/short-ships-verbatim 不变量，
// 以及头部+末尾上限本身——在wire::cap_tool_result_aged
// 中存在一次（由所有三个传输层共享）。对于 线程 中最新的 终端 工具结果，
// `recency_rank` 为 0，并向最旧的方向增长。
void write_tool_result_block(std::string& out, const ToolUse& tc,
                             CachePin pin, int recency_rank,
                             bool superseded, unsigned max_side) {
    out.push_back('{');
    bool first = true;
    json_write_field(out, "type", "tool_result", first);
    json_write_field(out, "tool_use_id", tc.id.value, first);
    // Non-terminal工具(待处理/已批准/正在运行)尚未携带输出。
    // 我们仍然 必须 为它们发出 tool_result — 请参阅上面
    // 的 is_assistant_with_results 了解 传输
    // -shape 的基本原理 — 因此合成一个 `is_error: true`
    // 占位符。将其标记为错误告诉模型 调用 实际上并未产生结果，
    // 这是对 "the previous turn died before this tool finished."
    // Empty Done 输出的真实读取，保留为合法产生
    // 任何内容的工具的历史 "(no output)"
    // 占位符（不是错误）。
    auto raw_output = tc.output();
    const bool non_terminal = !tc.is_terminal();
    const bool is_error = non_terminal || tc.is_failed() || tc.is_rejected();
    if (non_terminal) {
        json_write_field(out, "content",
            "(tool call did not complete \u2014 previous turn ended before this tool produced a result)",
            first);
    } else if (raw_output.empty()) {
        json_write_field(out, "content", "(no output)", first);
    } else if (superseded) {
        // LATER 转 read/edited/wrote 这个相同的文件，
        // 所以这个早期读取的 正文 是 过期 — 该模型已经具有更新的状态。
        // 将其折叠为确定性的one-line指针NOW，
        // 而不是等待age-fading(kFullResultWindow
        // 转动)来收缩它。在 read-heavy 编码循环中，
        // 相同的文件会被重复触及，因此这会回收死 传输 权重的单一最大来源。
        // 指针文本是 FIXED（没有 字节 计数/位置），
        // 因此给定的被取代的读取始终以相同的方式序列化 - 没有 缓存 搅动。
        json_write_field(out, "content",
            std::string{wire::kSupersededReadPointer}, first);
    } else if (const auto refs = wire::tool_search_references(tc); !refs.empty()) {
        // Anthropic 传输上的tool_search:用tool_reference块回答，
        // API将其扩展为延迟工具的模式定义本身。 仅 参考：API 400s
        // 和 tool_result，将它们与文本块 (
        // "Tool definitions ... cannot be mixed with other content") 混合，
        // 在实时端点上测量。
        if (!first) out.push_back(',');
        first = false;
        out.append("\"content\":[");
        bool block_first = true;
        for (const auto& name : refs) {
            if (!block_first) out.push_back(',');
            block_first = false;
            out.push_back('{');
            bool rf = true;
            json_write_field(out, "type", "tool_reference", rf);
            json_write_field(out, "tool_name", name, rf);
            out.push_back('}');
        }
        out.push_back(']');
    } else if (!wire::wire_tool_result_images(tc).empty()) {
        // 视觉tool_result:Anthropic接受用于tool_result
        // 的内容块的ARRAY，因此发出文本(ged-budgeted)，
        // 其后跟随每个所呈现图像的一个图像块。模型按顺序读取它们——
        // 首先是文本注释，然后是图片。图像 字节 永远不会褪色（它们
        // 是 调用 的重点），只有文本会褪色。
        std::string cap_buf, scrub_buf;
        const std::string_view capped =
            wire::cap_tool_result_aged_view(raw_output, recency_rank, is_error, cap_buf);
        const std::string_view text = wire::scrub_utf8_view(capped, scrub_buf);
        if (!first) out.push_back(',');
        first = false;
        out.append("\"content\":[");
        bool block_first = true;
        // 文本块(如果工具根本没有产生文本则跳过)。
        if (!text.empty()) {
            out.push_back('{');
            bool tf = true;
            json_write_field(out, "type", "text", tf);
            json_write_field(out, "text", text, tf);
            out.push_back('}');
            block_first = false;
        }
        for (const auto* imgp : wire::wire_tool_result_images(tc, max_side)) {
            if (!block_first) out.push_back(',');
            block_first = false;
            write_image_block(out, *imgp, CachePin::NotPinned);
        }
        out.push_back(']');
    } else {
        // 从最近的时间中选择 传输 预算。最近的结果（以及任何年龄
        // 的 全部 错误结果）保留全部预算，以便模型可以对其采取行动；
        // 过期 成功的结果逐渐淡入紧张的头脑+末尾，
        // 因此从 30 调用 之前读取的 60 KiB 会停止每轮完整重播。
        std::string cap_buf, scrub_buf;
        const std::string_view capped =
            wire::cap_tool_result_aged_view(raw_output, recency_rank, is_error, cap_buf);
        json_write_field(out, "content",
                         wire::scrub_utf8_view(capped, scrub_buf), first);
    }
    json_write_bool_field(out, "is_error", is_error, first);
    json_write_cache_control(out, pin, first);
    out.push_back('}');
}

} // 命名空间结束。

[[nodiscard]] std::string messages_json_string(const Thread& t,
                                               bool include_thinking) {
    // Read-collapse分析：较早读取其稍后再次触及的文件是传输
    // 上的过期，并获得one-line指针而不是其完整的正文(参见
    // wire::superseded_read_ids)。确定性，
    // 因此它永远不会扰动 提示词缓存。
    const auto superseded = wire::superseded_read_ids(t);

    // Many-image上限是REQUEST的属性，
    // 而不是任何单个图片的属性，因此它在整个线程上计算一次并向下
    // 传递到每个门。相反，计算 per-message 会让
    // 20-image 会话单独对每个回合应用宽松的上限，
    // 并且仍然发送提供商拒绝的请求。
    const unsigned max_side = util::wire_max_side(wire::wire_image_count(t.messages));

    // 第一遍：找出 缓存 断点所在的位置。 cli.js 引脚 两者
    // 最后一个和第二个-to-last *发出*消息的最后一个内容块（
    // 滚动 缓存 重用 — 第 N 轮的最后一个变成第 N+1 轮的第二个
    // -to-last）。这里的 "message" 是输出数组中的任何内容，
    // 因此带有 终端 tool_calls 的助手轮会贡献 两个
    // 消息（助手 + tool_results 跟随 -up）。
    int total_msgs = 0;
    // 还计算线程携带了多少个终端工具结果，因此我们可以为
    // write_tool_result_block中的age-tiered
    // 传输预算分配每个新近度排名（0=最新）。
    // 只有非-empty输出的终端结果参与衰落；计数是一个上限，
    // 等级是按照我们在下面发出的方式分配的。
    int total_tool_results = 0;
    for (const auto& m : t.messages) {
        const bool has_images = (m.role == Role::User && has_wire_image(m, max_side));
        if (!m.text.empty()
         || has_images
         || (m.role == Role::Assistant && !m.tool_calls.empty())) {
            ++total_msgs;
        }
        if (is_assistant_with_results(m)) {
            ++total_msgs;
            total_tool_results += static_cast<int>(m.tool_calls.size());
        }
    }
    const int pin_last       = total_msgs - 1;
    const int pin_second_last = total_msgs - 2;

    // ── 稳定1 -hour 锁定断点 ─────────────────────────────────
    // 最后/第二个-to-last每轮都引脚ROLL：第N轮的最后
    // 一个块变成第N+1轮的第二个-to-last，因此这两个断点仅在
    // 缓存两个最新消息中出现。只有当某个较早回合的断点仍然覆盖它时，
    // 所有 之前 都是 缓存 HIT - 并且在 5-minute
    // 默认 TTL 下，覆盖范围会在用户暂停时消失。结果：在每个空闲
    // 间隙之后，整个历史记录 末尾 都会有一个长会话 re-pays
    // full-price 缓存-creation。
    //
    // 修复(Claude代码的`Dt6`形状)：在历史深处的STABLE
    // 位置处植入THIRD断点，并赋予其1-HOUR ttl。
    // 因为它几乎不移动，所以直到它的整个前缀在长时间空闲窗口的 和
    // 回合中保持 缓存 命中。为了防止它每回合都移动（这会违背目的），
    // 我们将其位置QUANTIZE设置为多个kAnchorStep
    // 消息——每个kAnchorStep新消息它只前进一次，
    // 并且每个kAnchorStep消息最多前进一次re-caches。
    //
    // 锚必须严格在pin_second_last之前着陆，
    // 因此三个断点是不同的（Anthropic总共允许最多4个
    // cache_control块：系统+工具+这些）；
    // 当历史太短而无法容纳不同的锚点时，我们就不会发出锚点（滚动对已经
    // 涵盖了所有内容）。
    constexpr int kAnchorStep = 20;
    const int pin_anchor = [&]() -> int {
        if (total_msgs < 2 * kAnchorStep) return -1;   // 太短：没有明显的锚点
        // 锚定在 kAnchorStep 的最大倍数处，在滚动对之前
        // 留下至少一整步的净空空间。关闭 (total_msgs
        // - kAnchorStep) 而不是 (total_msgs
        // - 2) 意味着下限值仅在每 完整 增长步前进一次 -
        // 附加一圈永远不会移动它，因此直到锚点的缓存前缀在这些
        // 圈中保持不变。当它前进时，它会重新-caches一步。
        int a = ((total_msgs - kAnchorStep) / kAnchorStep) * kAnchorStep;
        if (a <= 0 || a >= pin_second_last) return -1;
        return a;
    }();

    std::string out;
    // 根据该 线程 实际容纳的尺寸来确定 缓冲区 的尺寸，
    // 而不是扁平的 64 KiB。一个长会话的 正文 会运行到几个 MB，
    // 所以旧的猜测意味着每一轮都会有少量的重新分配-and-copy
    // 传递到 multi-MB 缓冲区 — 现在，
    // 转义器不再为每个字符串保留，这是调整它大小的地方。
    // 对我们即将编写的尺寸进行一次廉价的传递，再加上约 40% 的
    // JSON 转义和块脚手架，然后让几何增长覆盖任何遗漏。
    {
        std::size_t want = 1024;
        for (const auto& m : t.messages) {
            want += m.text.size();
            want += m.thinking.size() + m.thinking_signature.size();
            for (const auto& b : m.thinking_blocks)
                want += b.text.size() + b.signature.size() + b.redacted_data.size();
            for (const auto& tc : m.tool_calls)
                want += tc.args_dump().size() + tc.output().size() + 128;
        }
        out.reserve(want + want / 2);
    }
    out.push_back('[');

    int emitted = 0;
    // 迄今为止发出的工具结果的运行计数（最早的在前）。下一个结果的新近度
    // 等级为 total_tool_results-1-emitted，
    // 因此 最后-emitted（最新）结果的等级为 0。
    int tool_results_emitted = 0;
    auto emit_msg_open = [&] {
        if (emitted > 0) out.push_back(',');
        ++emitted;
    };
    // Anthropic允许每个请求有4个cache_control断点
    // 的MAXIMUM；额外的内容会从 FRONT（最旧的优先）中悄悄删除，
    // 这将牺牲我们最有价值的引脚 - 系统 提示词。完整的 正文
    // 已经在稳定前缀（系统 + 工具）上使用了 4 个插槽中的 2 个，
    // 剩下 2 个用于消息数组。所以：
    //   ·存在锚→锚(1h)+最后一个(5m)[2个槽——丢弃
    //     第二-to-last擀面杖；锚点已经涵盖了所有内容，
    //     因此该图钉几乎什么也没买]
    //   •无锚→最后+第二-to-last(5m)【经典滚动】
    //     对，
    // 仍然是2个槽]任一分支都将消息数组保持在≤2个断点→≤4个断点处。
    const bool have_anchor = (pin_anchor >= 0);
    auto pinning_for = [&](int idx) -> CachePin {
        // 锚点是STABLE、long-lived断点→1-hour TTL。
        if (idx == pin_anchor)  return CachePin::Ttl1h;
        // 最新消息：始终滚动 5-minute 引脚。
        if (idx == pin_last)    return CachePin::Ttl5m;
        // 仅当存在 NO 锚点时才使用 Second-to-last
        // 擀面杖（否则会将请求推送到 5 个断点并从 缓存 中
        // 逐出系统 提示词）。
        if (!have_anchor && idx == pin_second_last) return CachePin::Ttl5m;
        return CachePin::NotPinned;
    };
    // NOT是固定消息的最后一个块的块必须保持取消固定；帮助程序 归并、
    // 只有消息的最后一个块携带该标记。
    // 规则以及 per-message TTL 选择。
    auto pin_if_last = [](CachePin msg_pin, bool last_block) -> CachePin {
        return last_block ? msg_pin : CachePin::NotPinned;
    };

    for (const auto& m : t.messages) {
        // ── 主信件( 如果是助手, 则文本+ tool_use 块) ──
        const bool has_text   = !m.text.empty();
        const bool has_images = (m.role == Role::User && has_wire_image(m, max_side));
        const bool has_tools  = (m.role == Role::Assistant && !m.tool_calls.empty());
        // 在也承载真实内容（文本或tool_use)的助理回合上重
        // 放捕获的思维块。 Anthropic 要求块在
        // tool_use 之前的回合中逐字存在，或者请求 400 秒。
        // 交错思维可以为每条消息生成 SEVERAL 签名块——
        // `thinking_blocks` 是 -order
        // 捕获中的权威；遗留（思考，
        // thinking_signature）对涵盖线程在multi
        // -block分裂之前持续存在。仅重放 SIGNED 块（拒绝未
        // 签名的思考块），根据启用思考的请求 (include_thinking)
        // 进行门控。
        int signed_blocks = 0;
        if (include_thinking && m.role == Role::Assistant) {
            for (const auto& tb : m.thinking_blocks)
                if (!tb.signature.empty() || !tb.redacted_data.empty())
                    ++signed_blocks;
            if (signed_blocks == 0 && !m.thinking_signature.empty())
                signed_blocks = 1;  // 旧单-pair 回退
        }
        const bool has_thinking = signed_blocks > 0 && (has_text || has_tools);
        if (has_text || has_images || has_tools) {
            const int my_idx   = emitted;
            const CachePin do_pin = pinning_for(my_idx);
            emit_msg_open();
            out.push_back('{');
            out.append(R"("role":)");
            out.append(m.role == Role::User ? R"("user")" : R"("assistant")");
            out.append(R"(,"content":[)");
            // Anthropic接受混合内容数组。发出图像 首次，
            // 以便引用它们的散文 (
            // "describe this screenshot")
            // 遵循相同的内容数组 - 模型按数组顺序读取，
            // 并受益于在 提示词 文本之前拥有视觉上下文 已加载。
            // 然后是文本块，然后是任何 tool_use 块（助理轮流）。
            // 空-bytes图像被完全跳过：杂散的空
            // ImageContent(e.g。
            // 其字节已耗尽的草稿附件)将序列化为空的base64
            // "data"字段和400整个请求。
            // 使用下面的发射循环使用的门来计算将发射
            // ACTUALLY的图像。这两者必须完全一致：
            // `blocks`决定哪个块是最后一个，最后一个块是
            // 固定缓存断点的地方。如果计数包含发射器丢弃的图像（尺寸过大），
            // 则 `block_emitted` 永远不会达到
            // `blocks`，不会固定任何块，并且请求会默默地丢失
            // 其 缓存 断点。以前，这会计算 non-empty 字节，
            // 而发射器也应用了尺寸上限 - 它们可能会发散。
            const int wire_images =
                has_images
                    ? static_cast<int>(wire::wire_message_images(m, max_side).size())
                    : 0;
            int blocks = (has_thinking ? signed_blocks : 0)
                       + wire_images
                       + (has_text ? 1 : 0)
                       + (has_tools ? static_cast<int>(m.tool_calls.size()) : 0);
            int block_emitted = 0;
            // 思维块进入 首次——模型在其 text/tool_use
            // 之前发出它们，并且重放顺序必须匹配。它们绝不是 缓存 引脚（
            // 内容始终跟随它们）。 JSON(...).dump()
            // JSON-encodes（可能为空）思考文本+不透明签名；
            // 思维障碍上没有 cache_control。
            if (has_thinking) {
                auto write_thinking = [&](const std::string& txt,
                                          const std::string& sig) {
                    if (block_emitted++ > 0) out.push_back(',');
                    out.append(R"({"type":"thinking","thinking":)");
                    out.append(json(scrub_utf8(txt)).dump());
                    out.append(R"(,"signature":)");
                    out.append(json(sig).dump());
                    out.push_back('}');
                };
                bool any_vec = false;
                for (const auto& tb : m.thinking_blocks) {
                    if (!tb.redacted_data.empty()) {
                        // Safety-redacted块：不透明，
                        // 作为其自己的内容类型逐字重放（思考-shaped重放400秒）。
                        if (block_emitted++ > 0) out.push_back(',');
                        out.append(R"({"type":"redacted_thinking","data":)");
                        out.append(json(tb.redacted_data).dump());
                        out.push_back('}');
                        any_vec = true;
                    } else if (!tb.signature.empty()) {
                        write_thinking(tb.text, tb.signature);
                        any_vec = true;
                    }
                }
                if (!any_vec)  // 旧单-pair 回退
                    write_thinking(m.thinking, m.thinking_signature);
            }
            if (has_images) {
                for (const auto* imgp : wire::wire_message_images(m, max_side)) {
                    const auto& img = *imgp;
                    if (block_emitted++ > 0) out.push_back(',');
                    const bool last_block = (block_emitted == blocks);
                    write_image_block(out, img, pin_if_last(do_pin, last_block));
                }
            }
            if (has_text) {
                if (block_emitted++ > 0) out.push_back(',');
                const bool last_block = (block_emitted == blocks);
                // 将 标签 占位符 (\x01ATT:N
                // \x01) 展开到其附件 正文 中，
                // 以便模型看到实际粘贴的文本/文件内容。
                // 成绩单渲染器为用户保留标签表格；
                // 只有 传输 载荷 才能看到完整的 字节。
                // 当m.attachments为空时，
                // No-op(不需要扩展，也不需要分配)。
                std::string expanded;
                const std::string_view wire_text =
                    m.attachments.empty()
                        ? std::string_view{m.text}
                        : std::string_view{expanded =
                              attachment::expand(m.text, m.attachments)};
                std::string scrub_buf;
                write_text_block(out, wire::scrub_utf8_view(wire_text, scrub_buf),
                                 pin_if_last(do_pin, last_block));
            }
            if (has_tools) {
                for (const auto& tc : m.tool_calls) {
                    if (block_emitted++ > 0) out.push_back(',');
                    const bool last_block = (block_emitted == blocks);
                    write_tool_use_block(out, tc, pin_if_last(do_pin, last_block));
                }
            }
            out.append("]}");
        }

        // ── Tool-result 随其后 -up (合成用户转) ──
        // 每个 tool_use 发出一个 tool_result，
        // 无论是否是终端。 Anthropic 强制执行的线形是成对的（每个
        // tool_use id 必须在下一条消息中显示为 tool_use_id），
        // 因此我们不能有选择地删除非 -terminal 的 id —
        // 这正是触发 HTTP 400 循环的原因。
        if (is_assistant_with_results(m)) {
            const int my_idx   = emitted;
            const CachePin do_pin = pinning_for(my_idx);
            emit_msg_open();
            out.append(R"({"role":"user","content":[)");
            const int total_results = static_cast<int>(m.tool_calls.size());
            int result_emitted = 0;
            for (const auto& tc : m.tool_calls) {
                if (result_emitted++ > 0) out.push_back(',');
                const bool last_block = (result_emitted == total_results);
                const int recency_rank =
                    total_tool_results - 1 - tool_results_emitted;
                ++tool_results_emitted;
                const bool is_superseded = superseded.count(tc.id.value) != 0;
                write_tool_result_block(out, tc, pin_if_last(do_pin, last_block),
                                        recency_rank, is_superseded, max_side);
            }
            out.append("]}");
        }
    }

    out.push_back(']');
    return out;
}

// 兼容性垫片：公共签名仍然返回JSON(transport.cpp的高频路径之外的调用者可能依赖它)。
// 高频路径 直接使用
// `messages_json_string`。
json build_messages(const Thread& t) {
    return json::parse(messages_json_string(t, /*include_thinking=*/false));
}

} // namespace agentty::provider::anthropic
