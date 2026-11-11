#pragma once
// agentty::provider::dbg — 原始 传输 转储，现在是 logx 上的薄垫片。
//
// 这曾经是它自己的记录器，具有它自己的环境变量(AGENTTY_DEBUG_API，
// AGENTTY_DEBUG_FILE)和它自己的文件。
// 那是 EIGHT 日志旋钮之一，成本不是代码——而是没有人记得
// 要设置哪一个，所以实际上没有设置，错误是通过猜测来诊断的。
//
// 现在所有内容都落在单个结构化日志的`wire`通道上：
//
//   • non-release 构建 — 由 DEFAULT 捕获，根本没有环境变量
//   • 发布版本 — AGENTTY_LOG=传输=trace（或 `trace` 表示所有内容）
//
// 一个旋钮(AGENTTY_LOG)，一档(AGENTTY_LOG_FILE，
// 否则~/.agentty/logs/agentty.log)。
// 传输 字节 与来自同一回合的 auth、工具和 smart-mode 事件 内嵌，
// 这就是当回合行为不当时您真正想要的 - 旧的单独文件无法与任何
// 内容关联。
//
// 级别选择：块是Trace（量大，只有在阅读协议时才需要），
// request/response元数据是Debug。

#include <string_view>

#include "agentty/util/logx.hpp"

namespace agentty::provider {

// 真实当且仅当将会记录传输-level倾销。 调用 在构
// 建昂贵的消息之前；宏已经对自己进行了门控。
[[nodiscard]] inline bool debug_log() noexcept {
    return logx::enabled(logx::Channel::Wire, logx::Level::Trace);
}

// 转储一个原始 数据流 块、VERBATIM 且未截断，并用产生它的方言标记（"openai-chat"、
// "openai-responses"、"anthropic-messages"、
// "ollama-native"）。
//
// 逐字重要：该代码库所发送的每个传输错误都是与服务器实际发送的内容
// 不一致的解析器，并且解析器是受到怀疑的东西。漂亮的 -printed
// 或剪裁的 帧 隐藏了证据 - 携带工具参数的副驾驶
// `response.function_call_arguments.done`
// 事件（其遗漏使每个工具 调用 都以 "invalid args" 的
// 形式失败）在真正的转弯中超过了旧的 2 KB 截断点。
//
// 命名方言也很重要：一个帐户可以在 /chat/completions
// 上提供某些模型，而在 /responses 上提供其他模型，因此没有标签
// 的跟踪会让您猜测应该归咎于哪个编解码器。
inline void dbg_chunk(const char* dialect, std::string_view chunk) noexcept {
    AGT_LOG(Wire, Trace, "wire.chunk", "dialect={} bytes={} raw={}",
            dialect, chunk.size(), chunk);
}

} // namespace agentty::provider
