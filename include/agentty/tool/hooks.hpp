#pragma once
// agentty::tools::hooks — user-authored 生命周期挂钩
// 具有明确的同意门。
//
// 挂钩是用户配置为在生命周期事件运行的shell命令。
// v1支持最高的两个-value 事件：
//
//   工具执行之前的pre_tool。  在 stdin 上接收工具名称 + JSON args；
//               NON-ZERO 退出 BLOCKS 工具 调用（其标准输出成为模型
//               看到的错误）。使用：策略门（"never rm -rf"、
//               阻止编辑 prod.config。
//   工具执行后的post_tool。   在 stdin 上接收
//               名称 + 参数 + 结果；退出代码会被记录但不会阻塞。
//               用途：编辑后自动格式化、通知、审核日志。
//
// 配置文件(首先找到获胜)：.agentty/hooks.json，
// 然后是~/.agentty/hooks.json：
//
//   {
//     "pre_tool":[{"match":"shell","run":"./scripts/guard.sh"}]，
//     "post_tool":[{"match":"edit|write","run":"clang-format -i $FILE"}]
//   }
//
// `match`为与工具名称匹配的ERE； `run` 通过 sh -c 执行，
// 并在标准输入上使用 事件 载荷 和这些环境变量：
//   AGENTTY_HOOK_EVENT pre_tool | post_tool
//   AGENTTY_HOOK_TOOL 工具名称
//
// ── SECURITY:同意门 ─────────────────────────────────────────
// 挂钩是在每个匹配工具 调用 上运行 AUTOMATICALLY 的
// 任意 shell——代理主机可以提供的最危险的单一扩展表面（
// 恶意 仓库 可以运送 .agentty/hooks.json 并在第
// 一个 `read` 上渗漏）。 agentty 因此 绝不 运行
// 一个尚未明确批准的钩子文件：
//
//   • 看到钩子文件（或钩子文件的更改版本）的 首次 时间，
//     其挂钩DO 不 RUN。 agentty 会显示一个 -line 通知，
//     告诉用户运行 `agentty hooks approve` 来检查并批准它。
//   • `agentty hooks approve` 打印完整文件，要求明确
//     y/N对终端进行确认，
//     并将文件的SHA-256存储在~/.agentty/hooks_approved.json.中
//   • 对文件的任何 字节 更改都会使批准无效（哈希不匹配）
//     →返回not-running+通知）。没有"approve forever"。
//   ·AGENTTY_NO_HOOKS=1禁用整个子系统。
//
// 钩子命令通过作为 bash 工具的 相同 沙箱 包装器运行（启用时为
// bwrap/沙箱-exec），因此批准的 -but-compromised 钩子
// 仍然是 工作区-confined。

#include <optional>
#include <string>
#include <string_view>
#include "agentty/util/io.hpp"

#include <maya/runtime.hpp>

namespace agentty::tools::hooks {

struct PreToolDecision {
    bool        blocked = false;  // 非-zero钩退出→工具调用被阻挡
    std::string reason;           // hook stdout（模型看到的错误）
};

// 运行`tool`的每个批准+匹配的pre_tool挂钩。
// 返回第一个阻止决策，或一个空（允许）决策。从不投掷；
// 丢失/未经批准/不可解析的 hooks 文件会产生 "allowed"。
[[nodiscard]] PreToolDecision
run_pre_tool(std::string_view tool, const std::string& args_json);

// 运行每个批准+匹配的post_tool挂钩。 Fire-and-forget 语义：
// 失败被记录到挂钩日志中，从不传播。
void run_post_tool(std::string_view tool, const std::string& args_json,
                   const std::string& result_text);

// 当存在散列为NOT批准的钩子文件时为真（驱动one-line
// TUI通知）。便宜：统计+记忆哈希。
[[nodiscard]] bool pending_approval(Io);

// 活动钩子文件的路径(首先是项目，然后是用户)，如果没有则为空。
[[nodiscard]] std::string active_file(Io);

// `agentty hooks`子命令：`list`显示配置的钩子
// +审批状态； `approve` 打印文件并以交互方式批准它。
// 返回进程退出代码。
int cli(Io, const maya::unlocked&, const std::string& verb);

} // namespace agentty::tools::hooks
