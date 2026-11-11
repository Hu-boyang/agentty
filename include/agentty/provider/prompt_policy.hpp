#pragma once
// 规范代理提示词策略。
//
// 提示词选择是运行时策略，而不是传输-transport关注的问题。
// 每个入口点（TUI、ACP 服务器、测试）调用 此功能，因此提供商交换机
// 无法通过重复的 if/else 源码树 默默地更改 tool/RAG 行为。

#include <string>

#include "agentty/util/io.hpp"

namespace agentty::provider {

struct Selection;

// 为选定的后端构建product-level系统提示词。
// 具有托管能力的模型共享完整代理 提示词；受约束的本地端点接收紧凑的
// 本地配置文件。外部ACP代理拥有自己的prompt/session.
[[nodiscard]] std::string system_prompt_for(Io, const Selection& selection);

// 当`selection`获得紧凑的local-model 提示词时为真：您运行的
// OpenAI-compat服务器（环回/LAN，普通HTTP）。共享，
// 因此主轮和子代理/`agentty run` 不能对模型获得哪个 提示词 存在分歧。
[[nodiscard]] bool uses_local_prompt(const Selection& selection);

// 选择 uses_local_prompt() 的紧凑 提示词 接受：
// Ollama 自己的本地 API，否则 OpenAI-compat 本地的。
// 两者都具有相同的操作规则。
[[nodiscard]] std::string local_system_prompt_for(const Selection& selection);

} // namespace agentty::provider
