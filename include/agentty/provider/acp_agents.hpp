#pragma once

// ── agentty::provider ——外部 ACP 代理代理 注册表 (配置表面) ─────────
//
// 将提供者规范id (任何显式配置-defined id)
// 映射到外部ACP代理子进程的SPAWN所需的argv/env/ cwd。
// 这是 ExternalAcpBackend 功能的配置部分：registry.hpp 知道规范 EXISTS
// 并且是 Kind::ExternalAcp；该文件知道 HOW 来启动它。
//
// 这是 agentty 相当于 Zed 的 `agent_servers`
// 配置：ACP 机制是 GENERIC — 它驱动您指定的任何外部 ACP 代理子进程。
// 不存在built-ins或per-agent硬编码行。您可以在 acp-agents.json
// 中按名称注册代理，就像在 Zed 中一样。
//
// 解析顺序（先命中者获胜），镜像.agentty/mcp.json：
//   1.$AGENTTY_ACP_AGENTS-JSON文件的显式路径（可信）。
//   2.~/.agentty/acp-agents.json-用户-global(可信)。
//   3. ./.agentty/acp-agents.json — 工作区-local (门控：可以生成
//      任意命令，因此需要AGENTTY_ACP_ALLOW_PROJECT=1)。
//
// JSON 形状（由规格 ID 键入的对象）：
//   {
//     "acpAgents"：{
//       "claude-agent-acp"：{
//         "command"："claude-agent-acp"，
//         "args"：["--stdio"]，
//         "env"：{"FOO"："bar"}，
//         "cwd"："/optional/working/dir"
//       }
//     }
//   }
// ("agents"被接受作为"acpAgents"
// 的别名。)在没有配置文件的情况下，注册表为空并且没有外部
// 进程可选。

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace agentty::provider {

// 用于一个外部ACP代理的完全-resolved启动规范。
struct AcpAgentSpec {
    std::string              id;       // 规范规范 ID（与 注册表 行匹配）
    std::string              command;  // argv[0] — 二进制程序 名称或绝对路径
    std::vector<std::string> args;     // argv[1..]
    std::vector<std::pair<std::string, std::string>> env;  // 额外环境
    std::string              cwd;      // 工作目录（""=继承进程cwd）

    // spawn_acp_agent()的完整argv（命令+参数）。
    [[nodiscard]] std::vector<std::string> argv() const {
        std::vector<std::string> v;
        v.reserve(args.size() + 1);
        v.push_back(command);
        for (const auto& a : args) v.push_back(a);
        return v;
    }
};

// 解析已配置的提供者规范ID的启动规范。如果未显式配置 id，
// 则返回 std::nullopt。
[[nodiscard]] std::optional<AcpAgentSpec> resolve_acp_agent(std::string_view id);

// 当规范 ID 命名已配置的外部 ACP 代理时为真。由 parse_selection
// 用于将规范路由到 Kind::ExternalAcp。
[[nodiscard]] bool is_acp_agent_id(std::string_view id) noexcept;

// 枚举动态提供者-picker行的已配置ACP代理。
// 空配置意味着空结果。
[[nodiscard]] std::vector<AcpAgentSpec> enumerate_acp_agents();

} // namespace agentty::provider
