#pragma once
// agentty::provider-SHARED系统-prompt策略：
// agent/tool/RAG指令文本加上CLAUDE.md-tier和学习的-memory程序集，
// 由EVERY提供者使用(Anthropic，本机ChatGPT/Codex，托管OpenAI-compatible)。
//
// 这曾经存在于 provider::anthropic 下，这是一个分层
// 错误：Claude-namespaced 函数是所有线路的单一事实来源。
// 它现在位于provider:: root，因此SSOT不会被错误归档，
// 并且per-provider分歧通过prompt_overlay()（一个小的附加
// DELTA）而不是分叉整个基础。纯字符串构建 - 没有 传输，没有 SSE，没有网络。
// 提示词由C++编写并烘焙到二进制程序中； 运行时 处未从磁盘读取任何内容，
// 因此不存在 提示词-injection 表面。参见 prompt.cpp。

#include <string>
#include <string_view>
#include <vector>

#include "agentty/provider/provider.hpp"   // provider::ToolSpec
#include "agentty/util/io.hpp"

namespace agentty::provider {

using ToolSpec = provider::ToolSpec;

// 具有实时环境信息的共享基础系统提示词(OS/shell节、CLAUDE.md层、
// 学到的-memory块、技能目录)。
//
// `lean`为SUBAGENTS构建了修剪后的变体：它保留了操作规则(file
// -editing、shell、context-economy、
// big-codebases、in-house-languages、环境)，但是OMITS、
// 内存-tools协议和on-demand技能目录——子代理从不调用
// remember/forget/wipe（不在其 允许清单 中）并且不保留事实，
// 因此该大块在其前缀上纯粹记为 dead-weight。默认值 (false) 是完整父级 提示词。
[[nodiscard]] std::string default_system_prompt(Io, bool lean = false);

// Per-provider 提示词 OVERLAY — 附加到共享基础的一个小增量，
// 因此特定的（通常是 "pedantic"）模型可以获得额外的规则或音调调整 无
// 需 分叉整个 提示词。 `provider_id` 是规范提供商 ID（e.g、
// "anthropic"、"chatgpt"、"openai"、"ollama"）；
// 未知或 覆盖面板-less 提供程序返回空字符串。像底座一样烘烤到 二进制程序 中。
[[nodiscard]] std::string prompt_overlay(std::string_view provider_id);

// 应用了提供者的覆盖面板的基础提示词。知道提供商 ID 的调用者应该更喜欢这个
// 而不是 default_system_prompt()，因此 覆盖面板 采用 副作用；
// 策略层（prompt_policy.cpp）使用它。
[[nodiscard]] std::string system_prompt_with_overlay(Io, std::string_view provider_id,
                                                     bool lean = false);

// Tool规格对应于我们的本地工具实现。
[[nodiscard]] std::vector<ToolSpec> default_tools();

} // namespace agentty::provider
