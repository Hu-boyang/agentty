#pragma once

// ── agentty::provider — ExternalAcpBackend → Provider 适配器 ────────────────
//
// 将外部 ACP 代理子进程呈现为普通 `stream(Request, EventSink)`
// Provider，因此 运行时 的 一个 分派 接口边界 (main.cpp
// `stream_fn`) 可以使用单个分支路由到它 — 与本机 Anthropic /
// ChatGPT 提供程序完全相同。没有新的 `Kind` 交换机在 代码库 上扇出；
// 特工-specific ACP的怪癖留在ExternalAcpBackend内。
//
// 职责：
//   • 延迟 SPAWN（和 缓存）代理子进程由其规范 ID 键入，使用
//     acp_agents.hpp的发射规范。每个代理有一个 long-lived
//     子进程为每个请求提供服务 - 其 ACP 会话在各轮中重复使用。
//   ·平移一轮`prompt()`：acp::SessionUpdate → agentty Msg
//     (StreamTextDelta / StreamThinkingDelta / StreamToolUse* /
//     StreamUsage)，然后从 TurnResult 开始结算 StreamFinished 或 StreamError。
//   ·取消:请求不携带取消令牌，因此它拥有一个并且
//     将其接线，以便Esc mid-stream到达代理。
//
// 线程-safety:产卵缓存被守卫； stream() 可以从提供者
// 工作线程 线程 调用。缓存的子进程在进程退出时（或调用
// release_acp_agents() 时）被拆除。

#include <string>

#include <maya/runtime.hpp>

#include "agentty/provider/provider.hpp"   // 请求，EventSink，StreamResult

namespace agentty::provider {

// 对于一个请求，驱动由`agent_id` (诸如"claude-agent-acp"
// /"codex-acp"的注册表规格ID，或config-defined id)
// 命名的外部ACP代理，将流式传输规范化消息到`sink`。首次使用时生成 +
// 缓存 子进程。在 spawn/launch 故障上，会发出 StreamStarted
// + StreamError，因此 UI 显示干净的错误，而不是静默挂起。
// 返回 StreamResult，命名回合如何结束（从 ACP 后端自己的
// TurnResult 映射），以便 ACP 臂通过与本机结果相同的值报告其结果。
StreamResult stream_external_acp(const maya::unlocked& cap,
                                 const std::string&    agent_id,
                                 Request               req,
                                 EventSink             sink);

// 拆除每个缓存的代理子进程(幂等)。关机时调用； per-agent
// 拆卸使用 ExternalAcpBackend 的强化 看门狗，
// 因此楔入的代理无法挂起退出路径。它等待子进程：`unlocked`。
void release_acp_agents(const maya::unlocked& cap) noexcept;

} // namespace agentty::provider
