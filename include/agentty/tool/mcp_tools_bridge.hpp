#pragma once
// mcp_tools_bridge-将mcp-cpp的电池-included工具组（
// mcp::tools::make_provider)改编回agentty的原生ToolDef表面。
//
//   第 1 层工具*实现*现在位于 mcp-cpp (read/write/edit/ list_dir、
//   bash、grep/glob/find_definition、诊断、git_*、web_*)。
//   agentty通过cap::CapabilityProvider和re-wraps来消耗它们，
//   每个广告工具作为ToolDef，其execute()闭包分派到提供者中，
//   并将每个结果的带类型的 副作用 + FileChange读回到agentty
//   的ToolOutput中。 diff-review UI、
//   change-strip 和 权限 策略始终准确地查看它们始终执行的类型。
//
//   Host-coupled工具(remember/forget/wipe、待办事项、技能、search_docs、
//   任务)是SHELLS，由mcp-cpp拥有； agentty 通过此处构建的
//   HostServices 适配器注入其后端。

#include <memory>
#include <string_view>
#include <vector>

#include "agentty/tool/registry.hpp"
#include "agentty/tool/host_call.hpp"

namespace agentty::tools {

// 从 mcp-cpp 提供商构建 agentty-facing ToolDef 列表。
// 由 build_registry() 调用一次。构造 HostServices 适配器
// (memory/ todo/skill/retriever/subagent/http)，
// 启动 make_provider()，使提供者在进程 生命周期 中保持活动状态（其 处理
// 函数 在返回的 ToolDef::execute 闭包中捕获），并为每个广告工具返回一个
// ToolDef。 Tool 订单遵循提供商的 list() 订单。
[[nodiscard]] std::vector<ToolDef> build_mcp_tool_defs();


} // namespace agentty::tools
