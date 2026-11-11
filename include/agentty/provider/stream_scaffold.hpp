#pragma once

// ── Provider 数据流 脚手架:共享的"how a streamed turn RUNS"层 ────
//
// stream_epilogue.hpp拥有如何转ENDS。此 头文件 在 START 和 MIDDLE
// 处拥有镜像 -image 重复项：每个 流式传输 传输层（Anthropic、OpenAI-compat、
// Ollama-native、ChatGPT/Codex/Copilot 响应）hand-rolled
// 相同的 StreamHandler 脚手架 —
//
//   ·捕获错误路径上的HTTP状态、解析 重试-After
//   ·将传输层活动转发为传输层-only StreamHeartbeats
//   ·转发 TCP-buffered 作为 StreamBufferedWait 发送
//   ·转储传输通道上的每个块，用方言标记
//   ·成功时喂送方言解析器；错误时累积 正文
//     在 64 KB 上限下（行为不当的边缘 流式传输 和无界 4xx
//     正文 不得是 OOM 错误路径）
//   • 循环后，记录 一个 end-of-turn 结果行 + 原始错误 正文
//
// 他们漂移了，正如尾声的传输层曾经所做的那样：Ollama丢失了
// on_buffered_wait（缓冲的-send停顿显示为死气沉沉），
// 错误-path谓词发散（is_success vs状态> =
// 400），并且end-of-turn日志线呈现三种不同的形状。
// 这个 头文件 是唯一的事实来源； 传输层 提供了 仅 的真正不同之处：
// 方言标签、解析器提要以及（可选地）等待多长时间。
//
// 故意将头文件-only和分配-free放在高频路径上，
// 与尾声相同。

#include <algorithm>
#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "agentty/io/http.hpp"
#include "agentty/provider/debug.hpp"
#include "agentty/provider/provider.hpp"
#include "agentty/provider/stream_epilogue.hpp"   // parse_retry_after
#include "agentty/runtime/msg.hpp"
#include "agentty/util/logx.hpp"

namespace agentty::provider {

// 错误 正文 被缓冲用于诊断，但有上限：每个 传输层 之间的奇偶校验，
// 由 provider_conformance_test 固定。
inline constexpr std::size_t kErrorBodyCap = 64 * 1024;

// 标准流式传输 超时梯子。值是 measured/argued
// 共识（请参阅每个 传输层 中过去的长评论）：
//   连接10s-TCP+TLS到健康主机<1s； 10 s 涵盖了一条缓慢
//                    的企业路径，没有楔入初创公司-adjacent 调用。
//   总共30分钟——响应网关可以每SSE 帧 缓冲区；
//                    给予合理的大转弯充足的时间，但永远不要永远
//                    楔入。
//   ping 15秒-HTTP/2 PING探针保持半-open TCP可检测，
//                    并且当网关处于活动状态但抑制DATA时产生
//                    传输层-only心跳。
//   空闲90秒——健康的托管数据流连续发出deltas/keepalives；
//                    90年代的沉默是死的传输层（沉默对端，
//                    代理停顿，半-open TCP）。分类瞬态 →
//                    auto-retried 和 退避。
//
// `idle`是一个参数，因为它是具有合法的per
// -transport故事的一个旋钮：LOCAL模型服务器(llama.cpp、
// Ollama)在提示词处理期间发送NOTHING，并且消费类硬件上的大模型
// 可以在第一个令牌之前研磨几分钟——90年代切割的中期
// -processing迫使重试与re-processes相同提示词 从头开始，永远。
[[nodiscard]] inline http::Timeouts stream_timeouts(
        std::chrono::milliseconds idle = std::chrono::milliseconds(90'000)) {
    http::Timeouts tos;
    tos.connect = std::chrono::milliseconds(10'000);
    tos.total   = std::chrono::minutes(30);
    tos.ping    = std::chrono::milliseconds(15'000);
    tos.idle    = idle;
    return tos;
}

// LOCAL服务器的天梯（普通http：llama.cpp，Ollama，
// vLLM）：空闲时间更长，并且没有总上限，因为家用GPU上的大模型
// 可以数据流一次回复超过30分钟（#108）。空闲仍然捕获死服务器，
// 并且 Esc 仍然取消。
[[nodiscard]] inline http::Timeouts local_stream_timeouts(
        std::chrono::milliseconds idle = std::chrono::milliseconds(600'000)) {
    http::Timeouts tos = stream_timeouts(idle);
    tos.total = std::chrono::milliseconds(0);
    return tos;
}

// Per-turn支架状态+统一StreamHandler。 Stack-allocate每run_stream_sync、
// 传输 `handler()`进入HTTP客户端，并读取post-loop
// / finish_stream 调用中捕获的字段。
struct StreamScaffold {
    // 传输层提供什么。
    const char* dialect;            // "anthropic-messages"、"openai-chat"、……
    EventSink   sink;               // 心跳/buffered-wait降落在这里
    // 将一个 success-path 块馈送到方言 解析器。
    // 返回 false 以停止读取（e.g。正文 已触发其 终端 事件 — 响应
    // 编解码器在 `response.completed` 后故意中止）。
    std::function<bool(std::string_view chunk)> feed;

    // 支架针对 post-loop 捕获什么。
    int         http_status = 0;
    std::string error_body;         // 上限为kErrorBodyCap
    std::optional<std::chrono::seconds> retry_after_hint;

    [[nodiscard]] bool ok() const noexcept {
        return http_status >= 200 && http_status < 300;
    }

    // 服务器的退避:重试-After 头文件，否则错误正文中指定的延迟(
    // Gemini仅发送该延迟)。 finish_stream 得到什么。
    [[nodiscard]] std::optional<std::chrono::seconds> retry_after() const noexcept {
        if (retry_after_hint) return retry_after_hint;
        if (ok()) return std::nullopt;
        return parse_retry_after_body(error_body);
    }

    // 制作制服 处理函数。 `this` 必须比 数据流 调用 存在得更久（它存在
    // 于 run_stream_sync 的堆栈上；HTTP 客户端是同步的）。
    [[nodiscard]] http::StreamHandler handler() {
        http::StreamHandler h;
        h.on_headers = [this](int status, const http::Headers& hh) {
            http_status = status;
            AGT_LOG(Wire, Debug, "stream.response", "dialect={} status={}",
                    dialect, status);
            if (ok()) return;
            retry_after_hint = parse_retry_after(hh);
        };
        h.on_activity = [this] {
            sink(StreamHeartbeat{.transport_only = true});
        };
        h.on_buffered_wait = [this] { sink(StreamBufferedWait{}); };
        h.on_chunk = [this](std::string_view chunk) -> bool {
            // VERBATIM，未截断：解析器通常是受到怀疑的东西，
            // 因此被剪裁的帧恰好隐藏了重要的字节（携带工具
            // 参数的Copilot `.done` 事件在
            // 真正的转弯中位于任何合理的截断点之上）。
            dbg_chunk(dialect, chunk);
            if (ok()) return feed(chunk);
            if (error_body.size() < kErrorBodyCap)
                error_body.append(chunk.data(),
                    std::min(chunk.size(), kErrorBodyCap - error_body.size()));
            return true;   // 继续将错误 正文 排除到上限
        };
        return h;
    }

    // 统一的end-of-turn对：一个Debug摘要+转弯失败时Warn处的原始
    // 错误正文。 该 行是 "why did my provider fail?" 的
    // 共享日志答案。 `transport_ok` = HTTP 客户端返回成功；
    // `detail` = 可选方言-specific 计数器(
    // "thinking_deltas=3")，已格式化。
    void log_result(bool transport_ok, std::string_view transport_err,
                    std::string_view detail = {}) const {
        AGT_LOG(Wire, Debug, "stream.result",
                "dialect={} status={} transport={} {}",
                dialect, http_status,
                transport_ok ? std::string_view{"ok"} : transport_err, detail);
        if ((!transport_ok || !ok()) && !error_body.empty())
            AGT_LOG(Wire, Warn, "stream.error.body", "dialect={} status={} raw={}",
                    dialect, http_status, error_body);
    }
};

} // namespace agentty::provider
