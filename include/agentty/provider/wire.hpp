#pragma once
// agentty::provider::wire-每个传输层 (Anthropic SSE、
// OpenAI-compat SSE、Ollama本机NDJSON)用于re-implement行-for-line的共享字节-level
// 数据流帧。
//
// 在这个头文件之前，三个传输层各自携带着自己的完全相同的
// 循环的副本：
//
//     buf.append(数据，长度)；
//     while ((nl = buf.find('\n', read_pos)) != npos) {
//         行=buf.substr(read_pos，nl-read_pos)；
//         read_pos = nl + 1;
//         if (line.back() == '\r') line.remove_suffix(1);
//         ...分派线...
//     }
//     if (read_pos >= kCompactThreshold) { buf.erase(0, read_pos); read_pos = 0; }
//
// 另外，对于两个SSE 传输层，具有相同的4个MiB溢流帽的相同的
// `data:`/`event:`蓄能器。对其中任何一个问题（缓冲区
// -compaction 错误、溢出-cap 调整、`\r\n` 边缘情况）
// 的修复都必须在三个地方进行，或者默默地发散。现在它曾经在这里生活过。
//
// SCOPE — 故意缩小。这拥有 仅 传输层-agnostic 框架：
// 将 字节 数据流 分割成行，并且(对于 SSE)将行分组为 事件。
// 它对 JSON、工具 调用、抢救、使用 帧 或 StopReason
// 一无所知——所有这些都保留在每个 传输层 自己的 StreamCtx 中，
// 它是真正的提供者 -specific，并且 不 受到此重构的影响。
// 成帧器是 传输层 与其自己的 分派 回调 驱动的管道层。
//
// 头文件-only+在回调上模板化：框架是现场数据流最热门的内循环，
// 因此per-line 分派必须内联。没有虚拟，高频路径 上没有
// std::function。

#include <concepts>
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>

namespace agentty::provider::wire {

// ── 回调 合同 ────────────────────────────────────────────────────
// 成帧器在其 分派 回调 上进行模板化（它必须内嵌在 高频路径 上 —
// 没有 std::function）。这些概念固定每个成帧者期望的
// EXACT 调用 形状，因此传递错误的-arity / 错误的-type
// 可调用是 调用 站点上的干净的-line 概念-mismatch，
// 而不是feed() 内的深层模板-instantiation 错误。
// 以前仅存在于评论中的所需签名现在由 编译器 检查。
//
//   LineSink — 作为 on_line(std::string_view) 调用
//   EventSink — 作为 on_event 调用（std::string_view 名称，
//                                     std::string_view 数据，填充的 char*）
template <class F>
concept LineSink = std::invocable<F&, std::string_view>;

template <class F>
concept EventSink =
    std::invocable<F&, std::string_view, std::string_view, char*>;

// 尾随可读字节 SseFramer在每个已分派的`data:` 载荷结束
// 后保持有效。设置等于simdjson::SIMDJSON_PADDING（64），
// 这样传输层可以将累加器直接交给simdjson的按需解析器 无需 per
// -frame memcpy-into-a-padded-scratch舞蹈
// - simdjson只需要填充字节是*可读*（SIMD超过-reads容忍垃圾），
// 保留的累加器满足。保留为本地常量，因此 wire.hpp 不受
// simdjson 包含的影响； 传输层 中的 static_assert 将其固定到
// 真正的 SIMDJSON_PADDING。
inline constexpr std::size_t kSseSimdPadding = 64;

// 仅当 `read_pos` 穿过此时，紧凑 `buf`（擦除已经-consumed
// 前缀）——将 per-chunk 消耗从 O(缓冲 字节)
// 转变为热 数据流 上的摊销 O(1)，因为超过最后一行边界的 in
// -flight 末尾 总是很小。
inline constexpr std::size_t kDefaultCompactThreshold = 64 * 1024;

// 单个 SSE 事件 的 multi-line `data:` 累加器上的硬上限。
// 真正的帧是1-10 KB； 4 MiB 大大超过了有史以来观测到的最大的
// content_block_delta。行为不当的服务器 流式传输 和无界的
// `data:` 否则会填充累加器，直到 OOM——当上限跳闸时，
// 我们删除 in-flight 事件 并在下一个空白行重新同步。
inline constexpr std::size_t kDefaultSseDataAccumMax = 4 * 1024 * 1024;

// ── 线条帧 ──────────────────────────────────────────────────────────
//
// 将传入的 字节 数据流 分割成 `\r?\n`-terminated 行。
// 拥有增长缓冲区+读取光标；调用者拥有所有语义状态
// 并通过 回调 接收每个完整行（尾部 `\r` 已被剥离，
// 终止符除外）。最后未终止的部分行为下一个
// feed() 保持缓冲。
//
// 由NDJSON 传输层 (Ollama/api/chat)直接使用。 SSE 传输层 驱动它
// 通过下面的 SseFramer。
class LineFramer {
public:
    explicit LineFramer(std::size_t compact_threshold = kDefaultCompactThreshold)
        : compact_threshold_(compact_threshold) {
        buf_.reserve(64 * 1024);
    }

    // 每新-complete行附加`len` 字节和分派。
    // `on_line` 被调用为 `on_line(std::string_view)`；
    // 该视图仅在 调用 期间有效（它指向内部 缓冲区，
    // 之后可能会被压缩）。
    template <LineSink OnLine>
    void feed(const char* data, std::size_t len, OnLine&& on_line) {
        buf_.append(data, len);
        std::string_view buf{buf_};
        while (true) {
            const auto nl = buf.find('\n', read_pos_);
            if (nl == std::string_view::npos) break;
            std::string_view line = buf.substr(read_pos_, nl - read_pos_);
            read_pos_ = nl + 1;
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            on_line(line);
        }
        if (read_pos_ >= compact_threshold_) {
            buf_.erase(0, read_pos_);
            read_pos_ = 0;
        }
    }

    // 保留额外的缓冲区余量（传输层知道其事件大小在-reserve之前，
    // 以避开快速数据流上的realloc级联）。
    void reserve(std::size_t n) { buf_.reserve(n); }

private:
    std::string buf_;
    std::size_t read_pos_ = 0;
    std::size_t compact_threshold_;
};

// ── SSE 事件 帧器 ─────────────────────────────────────────────────────
//
// 将Server-Sent-Events分组规则分层在LineFramer之上：
//   ·`event: <name>`设置当前事件名称，
//   ·`data: <payload>`附加到当前事件的数据累加器
//     (根据 SSE 规范，由 '\n' 连接的 multi-line 数据)，
//   ·空行终止事件并分派（名称、数据），
//   ·`:`注释和未知字段被静默删除。
//
// 累积的`data:`超过`data_accum_max`的
// 单个事件被丢弃：累加器被清除并且该事件的后续行被忽略，
// 直到空白-line终止符重新同步。 `on_event` 仅在非
// -empty 事件（名称或存在的某些数据）时调用，与每个 传输层
// 手动执行的操作相匹配。
//
// 成帧器对NOT解释数据——它将原始累积的载荷交回，
// 以便Anthropic可以在事件名称上分派，并且OpenAI可以查找
// `[DONE]`/解析 JSON，与之前完全相同。
class SseFramer {
public:
    explicit SseFramer(std::size_t compact_threshold = kDefaultCompactThreshold,
                       std::size_t data_accum_max     = kDefaultSseDataAccumMax)
        : lines_(compact_threshold), data_accum_max_(data_accum_max) {
        // 保留载荷余量PLUS和simd填充末尾，因此公共single
        // -line `data:` 帧永远不会重新分配，并且累加器的
        // 存储总是具有在载荷之后可读的kSseSimdPadding 字节。
        data_accum_.reserve(8 * 1024 + kSseSimdPadding);
    }

    // 饲料原料字节; 分派 每个完整的 事件。 `on_event` 被调用为
    // `on_event(std::string_view event_name, std::string_view data, char* padded_data)`。
    //           这三个都指同一个 载荷：
    // `data`为读取的-only视图； `padded_data` 指向累加器
    // 存储中的 相同 字节，但保证 kSseSimdPadding 可读尾随
    // 字节（对于 in-place simdjson 按需 解析，
    // 无 memcpy）。仅对于退化空 载荷，`padded_data` 才是 nullptr。
    // 所有这些仅在调用的持续时间内有效。
    template <EventSink OnEvent>
    void feed(const char* data, std::size_t len, OnEvent&& on_event) {
        lines_.feed(data, len, [&](std::string_view line) {
            if (line.empty()) {
                if (!skip_event_ && (!data_accum_.empty() || !event_name_.empty())) {
                    // 确保kSseSimdPadding在载荷之后的字节可读。
                    // reserve()保证容量； [大小，
                    // 容量]中的字节是可读的（它们属于分配），
                    // 这就是simdjson的SIMD over-read所要求的。
                    // 一旦累加器增长到其稳定的-state 大小，
                    // 储备就是高频路径 上的no-op。
                    if (data_accum_.capacity() < data_accum_.size() + kSseSimdPadding)
                        data_accum_.reserve(data_accum_.size() + kSseSimdPadding);
                    char* padded = data_accum_.empty()
                        ? nullptr : data_accum_.data();
                    on_event(std::string_view{event_name_},
                             std::string_view{data_accum_},
                             padded);
                }
                event_name_.clear();
                data_accum_.clear();
                skip_event_ = false;
                return;
            }
            if (skip_event_) return;

            if (line.starts_with("event:")) {
                std::size_t s = 6;
                while (s < line.size() && line[s] == ' ') ++s;
                event_name_.assign(line.data() + s, line.size() - s);
            } else if (line.starts_with("data:")) {
                std::size_t s = 5;
                while (s < line.size() && line[s] == ' ') ++s;
                const std::size_t add = (line.size() - s)
                    + (data_accum_.empty() ? 0 : 1);
                if (data_accum_.size() + add > data_accum_max_) {
                    // 溢出：丢弃in-flight 事件并跳过
                    // 其其余数据行直到空白-line终止符，
                    // 因此部分字节不能累积新的并且分派作为
                    // 损坏的事件。
                    data_accum_.clear();
                    event_name_.clear();
                    skip_event_ = true;
                    return;
                }
                if (!data_accum_.empty()) data_accum_.push_back('\n');
                data_accum_.append(line.data() + s, line.size() - s);
            }
            // `:` 注释和未知字段默默删除（SSE 规范）。
        });
    }

    void reserve(std::size_t n) { lines_.reserve(n); }

private:
    LineFramer  lines_;
    std::string event_name_;
    std::string data_accum_;
    bool        skip_event_ = false;
    std::size_t data_accum_max_;
};

// ── Tool-result 传输 预算(年龄) -tiered "tool result clearing") ───────────
//
// 唯一 SOURCE OF TRUTH表示工具的输出有多少在传输上传送。
// 每个传输层 (Anthropic、OpenAI-compat、Ollama本
// 机)用于以不同方式调整工具结果的大小——Anthropic具有全头+末尾帽
// +年龄-tiering；其他公司在运送原始产出时仅带有粗略的全球擦洗上限。
// 因此，巨大的 `read`/`grep`/`bash` 转储会在
// OpenAI/Ollama 路径上的每个后续回合中重播 IN 完整，
// 从而在最不能承受的模型（本地+较小的-window）上燃烧上下文。
// 现在保单已经存在一次了，在这里，还有所有三个 传输层 调用。
//
// 完整的 TRANSCRIPT 永远不会被触及——只有每个工具结果的 传输 副本的
// 大小由 (a) 其 字节 长度和 (b) RECENT 和 调用 的大小决定。
// 原理，来自Anthropic的背景-engineering指导：
// 工具调用已经位于很早的历史消息中，代理为何还需要再次读取完整原始结果？
// Claude代码以完全保真度保持最
// -recent结果并淡化其余部分。
//
//   等级 0 .. kFullResultWindow-1 → 全部预算（活动
//   工作集）等级 >= kFullResultWindow → 紧头+末尾（回忆它返回的 内容，
//                                    而不是重播它）
//
// 对于 线程 中最新的 终端 工具结果，`recency_rank` 为 0，
// 并且向最旧的方向增长。两个不变量保留这个损失-free进行推理：
//   ·ERROR结果是NEVER褪色——模型需要完整的失败文本
//     恢复不过老。
//   ·结果已经小于逐字褪色的预算船，无论如何
//     年龄——触摸它们没有任何好处（cap_tool_result 是预算
//     内的 no-op）。

inline constexpr std::size_t kToolResultFullBudget  = 64u * 1024u;
inline constexpr std::size_t kToolResultFadedBudget = 2u  * 1024u;
inline constexpr int         kFullResultWindow      = 8;

// 缓存注意：淡入悬崖(rank>=kFullResultWindow)每回
// 合前进一位，但是这是NOT每-turn 缓存流失。一旦结果越过悬崖，
// 它就会被限制为 DETERMINISTIC 2 KiB 头 + 末尾，
// 仅取决于其原始 字节 - 因此它的 传输 形式是 字节-IDENTICAL，
// 位于第 8、9、10 级……唯一的突变是给定结果第一次穿过 8→
// faded 的单轮，这会 re-caches 一次。它之前（已经
// 褪色）和之后（仍然满）的所有内容都没有改变，因此锚点的缓存前缀保持不变。

// ── UTF-8 验证和洗涤 ────────────────────────────────────────────
//
// 流式模型输出可以携带无效的UTF-8字节序列——跨两个SSE、
// 帧和mis-stitched分割的multi-byte代码点、
// 超长编码或单独代理。将其直接序列化为 JSON 请求 正文 会使
// 下一个 请求为 400 ("invalid UTF-8")，
// 从而楔入整个对话。每个 传输层 都必须先清理文本，然后才能将其发送到
// 传输 上。所有三个都曾经携带他们的 自身 副本，并且他们出现了
// 分歧：两个（OpenAI，Ollama）仅检查延续-byte形状并默默地接受过长的序列和代理；
// 只有 Anthropic 拒绝了他们。这是唯一、严格的事实来源
// ——它验证了 min-codepoint（无超长）、
// 0x10FFFF 上限和 D800..DFFF 代理漏洞。
[[nodiscard]] inline bool is_valid_utf8(std::string_view in) noexcept {
    const auto* p = reinterpret_cast<const unsigned char*>(in.data());
    const auto* end = p + in.size();
    while (p < end) {
        unsigned char c = *p;
        if (c < 0x80) { ++p; continue; }
        int extra; unsigned char mask; std::uint32_t min_cp;
        if      ((c & 0xE0) == 0xC0) { extra = 1; mask = 0x1F; min_cp = 0x80; }
        else if ((c & 0xF0) == 0xE0) { extra = 2; mask = 0x0F; min_cp = 0x800; }
        else if ((c & 0xF8) == 0xF0) { extra = 3; mask = 0x07; min_cp = 0x10000; }
        else return false;
        if (p + extra >= end) return false;
        std::uint32_t cp = c & mask;
        for (int k = 1; k <= extra; ++k) {
            unsigned char d = p[k];
            if ((d & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (d & 0x3F);
        }
        if (cp < min_cp || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
            return false;
        p += extra + 1;
    }
    return true;
}

// 如果 UTF-8 已经有效，则返回 `in` 不变（常见情况，
// 快速路径上为 0-copy）；否则将每个 ill-formed
// byte/sequence 替换为 U+FFFD（\uFFFD，Unicode
// 替换字符）。严格：超长的编码和代理代码点被视为无效并被替换。
[[nodiscard]] inline std::string scrub_utf8(std::string_view in) {
    if (is_valid_utf8(in)) return std::string{in};

    std::string out;
    out.reserve(in.size());
    auto repl = [&] { out.append("\xEF\xBF\xBD"); };
    std::size_t i = 0;
    while (i < in.size()) {
        unsigned char c = static_cast<unsigned char>(in[i]);
        if (c < 0x80) { out.push_back(static_cast<char>(c)); ++i; continue; }
        int extra; unsigned char mask; std::uint32_t min_cp;
        if      ((c & 0xE0) == 0xC0) { extra = 1; mask = 0x1F; min_cp = 0x80; }
        else if ((c & 0xF0) == 0xE0) { extra = 2; mask = 0x0F; min_cp = 0x800; }
        else if ((c & 0xF8) == 0xF0) { extra = 3; mask = 0x07; min_cp = 0x10000; }
        else { repl(); ++i; continue; }
        if (i + static_cast<std::size_t>(extra) >= in.size()) { repl(); ++i; continue; }
        std::uint32_t cp = c & mask;
        bool ok = true;
        for (int k = 1; k <= extra; ++k) {
            unsigned char d = static_cast<unsigned char>(in[i + static_cast<std::size_t>(k)]);
            if ((d & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (d & 0x3F);
        }
        if (!ok || cp < min_cp || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            repl(); ++i; continue;
        }
        out.append(in.data() + i, static_cast<std::size_t>(extra + 1));
        i += static_cast<std::size_t>(extra) + 1;
    }
    return out;
}

// 将索引备份到其所在的UTF-8代码点的开头。`i`是预期切点；
// 如果它落在 multi-byte 序列中，我们向左走直到前导 字节
// （或 0）。以 3 个连续 字节 为界（最大 UTF-8
// 序列为 4 字节）。
[[nodiscard]] inline std::size_t utf8_floor(std::string_view s, std::size_t i) noexcept {
    if (i >= s.size()) return s.size();
    std::size_t steps = 0;
    while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80 && steps < 3) {
        --i; ++steps;
    }
    return i;
}

// 将索引向前推进到下一个UTF-8代码-point边界。
[[nodiscard]] inline std::size_t utf8_ceil(std::string_view s, std::size_t i) noexcept {
    std::size_t steps = 0;
    while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80 && steps < 3) {
        ++i; ++steps;
    }
    return i;
}

// 传输-only 字节-budget 单个工具结果的上限。一个 500
// KiB grep 或一个巨型文件的 `read` 否则会使 每个 后续请求膨胀
// （结果会在每轮重播），直到 auto-compaction 最终启动。
// Mirrors Zed 的 take_text_within_byte_budget
// (thread.rs)：限制 字节 预算，切割 UTF-8 边界，
// 拼接标记以便模型知道字节 被省略。我们保留 head + 末尾 （不仅仅是
// head），因为工具输出通常在 END 处携带操作摘要或错误（编译器 计数、
// "N matches"、退出状态）。转录内容未受影响——用户仍然可以看到完整的输出；
// 仅修剪 传输 副本。
[[nodiscard]] inline std::string cap_tool_result(std::string_view in, std::size_t budget) {
    if (in.size() <= budget) return std::string{in};

    const std::size_t elided = in.size() - budget;
    std::string marker = "\n\n...[" + std::to_string(elided)
                       + " bytes elided to fit wire budget; full output is "
                         "in the transcript]...\n\n";

    // 病态的小预算：硬-truncate在边界上的头。
    if (marker.size() + 16 >= budget) {
        return std::string{in.substr(0, utf8_floor(in, budget))};
    }

    const std::size_t body_budget = budget - marker.size();
    const std::size_t head_len    = (body_budget * 7) / 10;
    const std::size_t tail_len    = body_budget - head_len;

    const std::size_t head_cut  = utf8_floor(in, head_len);
    const std::size_t tail_from = utf8_ceil(in, in.size() - tail_len);

    std::string out;
    out.reserve(head_cut + marker.size() + (in.size() - tail_from));
    out.append(in.substr(0, head_cut));
    out.append(marker);
    out.append(in.substr(tail_from));
    return out;
}

// 给定新近度等级的工具结果的传输预算。错误使任何年龄的预算
// 都保持完整；最近的结果保留它； 过期 的成功结果淡出了
// 紧张的头脑+末尾 的预算。
[[nodiscard]] inline std::size_t faded_tool_budget(int recency_rank, bool is_error) noexcept {
    const bool faded = !is_error && recency_rank >= kFullResultWindow;
    return faded ? kToolResultFadedBudget : kToolResultFullBudget;
}

// 便利性：根据工具的使用年限来限制工具结果。每 传输层 共享一个 调用 — 从 (
// recency_rank, is_error) 中选择预算，然后向其添加 +末尾-cap。
[[nodiscard]] inline std::string cap_tool_result_aged(std::string_view raw,
                                                      int recency_rank,
                                                      bool is_error) {
    return cap_tool_result(raw, faded_tool_budget(recency_rank, is_error));
}

// 当没有任何东西需要移除时，盖WITHOUT复制。 正文 编码器
// 调用 对于每轮的每个工具结果都是如此，然后将结果转义到输出
// 缓冲区 中，因此当结果已经适合（常见情况）时，
// 中间 std::string 是纯粹的分配 -copy-free 搅动。
// 返回快速路径上 `in` 的视图；仅当实际必须构建上限时，
// `owned` 才保留 字节，并且必须比返回的视图寿命更长。
[[nodiscard]] inline std::string_view cap_tool_result_view(std::string_view in,
                                                           std::size_t budget,
                                                           std::string& owned) {
    if (in.size() <= budget) return in;
    owned = cap_tool_result(in, budget);
    return owned;
}

[[nodiscard]] inline std::string_view cap_tool_result_aged_view(std::string_view raw,
                                                                int recency_rank,
                                                                bool is_error,
                                                                std::string& owned) {
    return cap_tool_result_view(raw, faded_tool_budget(recency_rank, is_error), owned);
}

// UTF-8清理的想法相同：有效输入（几乎总是）作为视图
// 而不是副本返回。否则，`owned` 携带修复后的 字节。
[[nodiscard]] inline std::string_view scrub_utf8_view(std::string_view in,
                                                      std::string& owned) {
    if (is_valid_utf8(in)) return in;
    owned = scrub_utf8(in);
    return owned;
}

// ── 漏出-tool-call嗅探器 ─────────────────────────────────────────────────
// 弱本地模型（通过 OpenAI-compat 和 Ollama-native 路径提供
// 服务）经常将工具 调用 发出为 PLAIN TEXT，而不是使用本机 tool_calls
// 通道：裸 `{"name":...}` 对象、`[{...}]` 数组、
// `<tool_call>…` XML 标签或 ```json 围栏。两种传输实现都会暂存流式
// 文本返回（而不是将其显示为散文），只要它COULD仍然
// 解析成那些形状之一，然后在end
// -of-turn处将完成的斑点抢救成真正的工具调用。
//
// 这是共享的"is the held text still a plausible tool-call prefix?"
// 驱动持有的谓词。它是保守的OPENside：它返回 true，
// 而前缀仍然不明确（只看到空格，一个裸露的`{`之后什么都没有，
// 不完整`<tool_call>`标签，一个人
// ```` ``` ````），让传输继续等待，避免过早刷新出
// half-formed 调用为散文。当文本提交时它返回 false
// being prose (`{` followed by a non-key char, a ```` ```cpp ```` fence, any
// 其他主角)。
//
// openai 和 ollama 传输层 中都是 copy-pasted
// — 相同的逻辑，漂移的空白；对其中一个（e.g。新的包装形状）的修复必须手动镜像。
// 这是唯一的事实来源。纯 string_view→bool：
// 没有 StreamCtx，没有 JSON，没有 分配。
[[nodiscard]] inline bool could_be_tool_json(std::string_view s) noexcept {
    // 跳过前导空格。
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t'
                            || s[i] == '\n' || s[i] == '\r')) ++i;
    if (i >= s.size()) return true;  // 到目前为止只有空白，尚未决定

    std::string_view rest = s.substr(i);

    // 模式1：以`{`开头的裸JSON对象。大括号后面可能是第一个键之前的
    // whitespace/newlines（来自本地模型的 pretty-printed
    // JSON），因此接受 `{`、`{<ws>`、`{"`（键）和 `{}`（空）。
    if (rest.front() == '{') {
        if (rest.size() == 1) return true;  // 到目前为止只有 `{`
        std::string_view after = rest.substr(1);
        std::size_t k = 0;
        while (k < after.size() && (after[k] == ' ' || after[k] == '\t'
               || after[k] == '\n' || after[k] == '\r')) ++k;
        if (k == after.size()) return true;       // `{` 之后只有空格
        return after[k] == '"' || after[k] == '}'; // 按键启动或空物体
    }

    // 模式2：以`[{`开始的JSON阵列（或仍在解析的单独的`[`）。
    if (rest.starts_with("[{") || rest.starts_with("[ {")) return true;
    if (rest == "[" || rest == "[ ") return true;

    // 模式3：<tool_call> XML标签（及其任何不完整的前缀）。
    if (rest.starts_with("<tool_call>")) return true;
    constexpr std::string_view kTag = "<tool_call>";
    if (rest.size() < kTag.size() && rest.front() == '<') {
        for (std::size_t j = 0; j < rest.size(); ++j)
            if (rest[j] != kTag[j]) return false;  // 分歧
        return true;                                // 精确前缀匹配
    }

    // 模式 4：```json 围栏，或尚未确定类型的单独 ```，但不包括
    // 已经确定为非 JSON 的围栏，例如 ```cpp。
    if (rest.starts_with("```json")) return true;
    if (rest.starts_with("```")) {
        if (rest.size() <= 3) return true;  // just ``` so far
        std::string_view after = rest.substr(3);
        return after.empty() || after[0] == '\n' || after[0] == '\r'
            || after[0] == ' ' || after[0] == '\t' || after[0] == 'j';
    }

    // 任何其他的都是致力于散文。
    return false;
}

} // namespace agentty::provider::wire
