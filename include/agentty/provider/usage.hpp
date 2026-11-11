#pragma once
// agentty::provider::usage-每个OpenAI-family 传输
// 层用于打开-code的共享令牌-usage提取器。
//
// 每个 流式传输 提供商以自己的 传输 形状报告令牌计数，
// 但整个 代码库 中只有 三个 个不同的形状，其中几个在 传输层 之间
// 复制了 -pasted (Ollama-native
// `prompt_eval_count`/`eval_count` 对
// 出现了 三个 次 — 一次在 openai 传输层 的本机中路径并在
// ollama 传输层 中两次）。对其中任何一个的调整（e.g。显示新的缓存
// -token字段）都必须手动镜像或默默漂移。现在每个形状都在一个地方被解析。
//
// 所有这三个都映射到单个StreamUsage结构（runtime/msg.hpp)上。
// 当对象根本不携带令牌计数时，它们返回 std::nullopt，因此调用者可以
// 保留其现有的 "only sink if non-empty" 防护：
//
//     if (auto su = usage::from_openai(j["usage"])) ctx.sink(*su);
//
// 仅限 SCOPE — 解析。提取者对框架、停止原因或水槽一无所知；他们将一个
// JSON 对象变成一个可选的 StreamUsage。 Anthropic
// 的原生形状在 传输层 中保持内联，因为 StreamUsage 的字段名称 ARE
// Anthropic 传输 名称 (input_tokens / output_tokens
// / cache_creation_input_tokens /
// cache_read_input_tokens) — 直接 `.value()` 读取，无需翻译，
// 因此助手只会添加间接。

#include <algorithm>
#include <optional>

#include <nlohmann/json.hpp>

#include "agentty/runtime/msg.hpp"

namespace agentty::provider::usage {

using json = nlohmann::json;

// 当StreamUsage携带至少一个非-zero计数时为真——共享
// "worth sinking"测试手动应用的传输层。
[[nodiscard]] inline bool any(const StreamUsage& su) noexcept {
    return su.input_tokens || su.output_tokens
        || su.cache_creation_input_tokens || su.cache_read_input_tokens;
}

// OpenAI/v1/chat/completions `usage`对象：
//   {"prompt_tokens":N, "completion_tokens":N,
//    "prompt_tokens_details":{"cached_tokens":N,
// "cache_write_tokens":N}} prompt_tokens INCLUDES
// cached_tokens (OpenAI, DeepSeek, OpenRouter, vLLM,
// llama.cpp，Strata都同意：缓存是子集)并且OpenRouter 的 cache_write_tokens。
// StreamUsage 位于 Anthropic 之后，其中 input_tokens 不包括 缓存，
// 因此这些部分将移出此处的输入。将它们加起来然后给出真实的 提示词 大小，而不是对 缓存 进行两次计数 (#96)。
[[nodiscard]] inline std::optional<StreamUsage> from_openai(const json& u) {
    if (!u.is_object()) return std::nullopt;
    StreamUsage su;
    const int prompt = std::max(u.value("prompt_tokens", 0), 0);
    su.output_tokens = u.value("completion_tokens", 0);
    int cached = 0, written = 0;
    if (u.contains("prompt_tokens_details")
        && u["prompt_tokens_details"].is_object()) {
        const auto& d = u["prompt_tokens_details"];
        cached  = d.value("cached_tokens", 0);
        written = d.value("cache_write_tokens", 0);
    }
    cached  = std::clamp(cached, 0, prompt);
    written = std::clamp(written, 0, prompt - cached);
    su.input_tokens                = prompt - cached - written;
    su.cache_read_input_tokens     = cached;
    su.cache_creation_input_tokens = written;
    // reasoning_tokens嵌套在聊天传输 (o-series/gpt-5/兼容推理模型）
    // 上的completion_tokens_details下。
    // 已计入 completion_tokens — 仅供显示。
    if (u.contains("completion_tokens_details")
        && u["completion_tokens_details"].is_object()) {
        su.reasoning_output_tokens =
            u["completion_tokens_details"].value("reasoning_tokens", 0);
    }
    return any(su) ? std::optional{su} : std::nullopt;
}

// OpenAI响应API `usage`对象（ChatGPT/Codex路径）：
//   {"input_tokens":N, "output_tokens":N,
//    "input_tokens_details":{"cached_tokens":N,
// "cache_write_tokens":N}} 命名类似于 Anthropic，但计数类似于 OpenAI：input_tokens
// 是整个 提示词，cached/written 代币是其中的一部分（codex-rs） non_cached_input())。
// 按照与from_openai相同的方式将它们分开。
[[nodiscard]] inline std::optional<StreamUsage> from_responses(const json& u) {
    if (!u.is_object()) return std::nullopt;
    StreamUsage su;
    const int prompt = std::max(u.value("input_tokens", 0), 0);
    su.output_tokens = u.value("output_tokens", 0);
    int cached = 0, written = 0;
    if (u.contains("input_tokens_details")
        && u["input_tokens_details"].is_object()) {
        const auto& d = u["input_tokens_details"];
        cached  = d.value("cached_tokens", 0);
        written = d.value("cache_write_tokens", 0);
    }
    cached  = std::clamp(cached, 0, prompt);
    written = std::clamp(written, 0, prompt - cached);
    su.input_tokens                = prompt - cached - written;
    su.cache_read_input_tokens     = cached;
    su.cache_creation_input_tokens = written;
    // reasoning_tokens嵌套在响应传输上的output_tokens_details下。
    // 已计入 output_tokens — 仅供显示。
    if (u.contains("output_tokens_details")
        && u["output_tokens_details"].is_object()) {
        su.reasoning_output_tokens =
            u["output_tokens_details"].value("reasoning_tokens", 0);
    }
    return any(su) ? std::optional{su} : std::nullopt;
}

// Ollama本机/api/chat最终帧:
//   {"done":true, "prompt_eval_count":N,
// "eval_count":N, ...} 计数是 DONE 帧 上的 top-level 字段，
// 而不是嵌套的 `usage` 对象 — 所以这直接从 帧 读取。
// 无缓存-token概念（本地KV 缓存为前缀-automatic且未报告）。
[[nodiscard]] inline std::optional<StreamUsage> from_ollama(const json& frame) {
    if (!frame.is_object()) return std::nullopt;
    StreamUsage su;
    su.input_tokens  = frame.value("prompt_eval_count", 0);
    su.output_tokens = frame.value("eval_count", 0);
    return any(su) ? std::optional{su} : std::nullopt;
}

} // namespace agentty::provider::usage
