#pragma once
// agentty::mcp-SERVER侧：暴露agentty自己的原生工具OVER MCP。
//
// client.hpp的逆。 client.hpp让agentty CONSUME远程MCP服务器的
// 工具作为本地ToolDefs；这让 agentty BE 成为 MCP 服务器，因此任何 MCP 客户端（
// Claude 桌面、另一个代理、IDE）都可以使用 调用 agentty 的本机工具 — `read`、
// `edit`、`bash`、`grep`、`glob`、`list_dir`、 `web_fetch`、
// `web_search`、`find_definition`、`diagnostics`、git_* 系列…
// … — 超过标准 传输层。
//
//   ┌──────────────┐   tools/list           ┌───────────────────────────┐
//   │ MCP 客户端   │ ─────────────────────► │ agentty mcp-serve        │
//   │ (Claude/IDE) │   tools/call read {…}   │   tools::registry()       │
//   │              │ ◄───────────────────── │   → DynamicDispatch::exec  │
//   └──────────────┘   CallToolResult        └───────────────────────────┘
//
// 这是ONE诚实地放置"all agentty tools through MCP"
// 的地方：进程边界是真实的，所以JSON是真实的合约。内部代理循环在
// -process ToolDef路径中保持快速（FileChange round
// -trips本来就在那里）；远程调用者只是以文本形式接收差异，
// 这对于该边界来说是正确的。
//
// PERF CONTRACT:这个头文件是mcp-cpp-FREE(没有模板成本泄漏)。
// 所有重型机械都位于 src/mcp/serve.cpp 中，与桥的其余部分一样，
// 编译到 agentty_mcp 对象 库 中。入口点仅在用户调用
// `agentty mcp-serve`时运行；正常的 TUI/ACP 路径
// 永远不会触及它，因此冷启动不受影响。

#include <maya/capability.hpp>   // tiny:仅能力类型

namespace agentty::mcp {

// 通过stdio将agentty作为MCP服务器运行（stdin/stdout上的换行-delimited
// JSON-RPC；在stderr上进行诊断，以便协议通道保持干净）。
// 注册tools::registry()中的每个工具并阻止服务请求直到stdin关闭。
// 返回进程退出代码。 provider/subagent/sandbox 接口
// 边界 必须已由调用者连接（main.cpp 执行此操作），因此 `task` 和
// `bash` 等工具的行为与 TUI 中的行为完全相同。它会阻塞，
// 直到标准输入关闭并加入其读取器：`unlocked`。
[[nodiscard]] int serve_stdio(const maya::unlocked&);

} // namespace agentty::mcp
