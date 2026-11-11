#pragma once
// agentty::mcp —Model 上下文协议客户端集成。
//
// "MCP as a capability provider"(架构论文，
// §2/§10)：MCP服务器的工具作为ORDINARY agentty工具暴露给代理。
// 该模型将它们视为与 `read`/`edit`/`bash` 相同的平面工具列表，
// 而 CANNOT 将远程 MCP 工具与本地工具区分开来 — 这就是重点。
// agentty的能力接口边界已经是`tools::ToolDef`(名称+input_schema
// +副作用+一个execute(JSON)->ExecResult闭包)； MCP 工具只是又一个
// ToolDef，其 execute() round-trips 和 `tools/call`
// over JSON-RPC 到生成的服务器进程。删除 MCP 不会改变任何其他内容 - 接口边界
// 是 注册表，而不是 MCP。
//
// PERF CONTRACT (加载-bearing —agentty 保持 sub-ms 冷启动/~9MB):
//   ·LAZY+OPT-IN。如果没有 `.agentty/mcp.json`（或 AGENTTY_MCP_CONFIG），
//     mcp_tools()返回已完成ZERO工作的{}——没有进程生成，
//     没有握手——因此不使用MCP的用户在启动时无需支付任何费用。
//   ·重的模板-rich mcp-cpp 头文件被限制在.cpp中
//     文件(src/mcp/*.cpp)；这个 头文件 是 mcp-cpp-FREE，
//     因此它的实例化成本永远不会泄漏到 源码树 的其余部分中。下面的所有内容都是一个forward
//     -declared句柄或一个普通的ToolDef。
//   ·同步调用路径：工具在cmd 工作线程 线程上运行并在
//     带有超时的std::future::get()——没有协程运行时，
//     没有将事件循环移植到render/stream循环上。

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "agentty/mcp/plugin_model.hpp"   // PluginModel值快照
#include "agentty/tool/registry.hpp"   // ToolDef

namespace agentty::mcp {

// ── PluginModel : 真相的唯一来源 plugin/tool 状态 ─────────
// 任何 UI 都可以在不与现场 池 比赛的情况下保持值 快照 (plugin_model())。
// 值类型现在位于独立的 头文件 中，因此 TEA Model 可以拥有 快照，
// 而无需通过 tools::ToolDef 进行包含循环。
// 参见docs/design/plugin-model.md.

// 当前模型作为值快照——在并发重新加载下跨池
// 交换可以安全地保持。便宜：读取实时 池 + 配置一次。
// 权威查看设置选择面板 渲染。
[[nodiscard]] PluginModel plugin_model();

// 每个连接的服务器的握手`instructions` (其工具的用途、
// 何时使用它们)，按服务器名称。每台服务器有上限；空的省略。
[[nodiscard]] std::vector<std::pair<std::string, std::string>> mcp_server_instructions();

// 不透明，参考-counted句柄使所有连接的MCP服务器在进程的生命周期期间保持活动。
// mcp_tools() 返回的 ToolDef execute()
// 闭包捕获了该副本（shared_ptr），因此连接（每个连接都
// 拥有一个生成的子进程+其 stdio 传输层 + mcp::Client）
// 只要仍然可以调用某些工具，就会一直存在。
// 定义于src/mcp/bridge.cpp；调用者将其视为他们持有的令牌，但从不检查。
struct ConnectionPool;
using PoolHandle = std::shared_ptr<const ConnectionPool>;

// 连接到配置中指定的每个 MCP 服务器并为每个服务器广告的每个工具合成
// ToolDef (通过 `initialize` + `tools/list`)。
//
// 配置解析（第一个存在的获胜；无 → 返回 {} 不执行任何操作）：
//   1.$AGENTTY_MCP_CONFIG(JSON文件的显式路径)
//   2../.agentty/mcp.json(项目-local)
//   3.~/.agentty/mcp.json(用户-global)
//
// 配置形状(Claude-Desktop-compatible):
//   {
//     "mcpServers"：
//       {"github"：{"command"："mcp-server-github"，"args"：["--repo"，
//                   "x"]，"env"：{"TOKEN"："..."}}，
//       "fs"：{"command"："npx"，"args"： ["-y","@mcp/server-filesystem","/tmp"] }
//     }
//   }
//
// Tool命名：远程工具被命名为`mcp__<server>__<tool>`，
// 因此两个服务器可以公开同名的工具而不会发生冲突，
// 并且用户可以一眼看出能力来自哪里（出处，文章§6）。
//
// `out_pool`接收keep-alive句柄；
// 将其存储在进程 生命周期 的某个位置（注册表 执行此操作）。
// 永不抛出：无法生成/握手的服务器将被跳过并记录警告，
// 并且其工具根本不存在。返回的向量可能为空。
[[nodiscard]] std::vector<tools::ToolDef> mcp_tools(PoolHandle& out_pool);

// 当存在 MCP 配置文件时为真（廉价统计数据，无连接）。
// 让 注册表 决定是否尝试 mcp_tools()。纯I/O探头。
[[nodiscard]] bool mcp_config_present();

struct ServerLaunch {
    enum class Transport : std::uint8_t { Stdio, Http, Sse };
    std::string name;
    Transport transport = Transport::Stdio;
    std::string command;
    std::vector<std::string> args;
    std::vector<std::pair<std::string, std::string>> env;
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
};

// 以适合 ACP session/new.mcpServers. 工作区-local
// 配置的协议-neutral形状返回受信任的配置服务器，遵循与直接 MCP 消耗相同
// 的显式 AGENTTY_MCP_ALLOW_PROJECT 门。
[[nodiscard]] std::vector<ServerLaunch> configured_servers_for_delegation();

// ── 动态工具表面(tools/list_changed) ────────────────────────────
// MCP服务器可能随时发出`notifications/tools/list_changed`。
// 连接 池 保留了合成 ToolDefs 的 LIVE 快照，每当任何服务
// 器的工具列表发生更改时都会重新构建。 `mcp_tools_live()`
// 返回进程-wide 池（填充的mcp_tools()）的当前快照。
// 当 MCP 未配置或未连接任何内容时为空。
//
// 每当 快照 变化时，`mcp_generation()` 就会发生碰撞；
// 调用者可以通过比较缓存的工具列表来知道何时重新-pull。两者都是 O(1) 和
// 锁-guarded — 可以安全地从 render/turn 循环进行轮询。
[[nodiscard]] std::vector<tools::ToolDef> mcp_tools_live();
[[nodiscard]] unsigned long mcp_generation() noexcept;

// 碰撞代WITHOUT接触连接池。向 注册表 的 传输 缓存 发出信号，以在下次访问时
// re-project 工具表面（其中 re-reads 是 project_tools
// 中的实时 tools.exclude）——per-tool enable/disable
// 切换 后面的便宜的 re-spawn-free 路径。
void mcp_bump_generation() noexcept;

// 立即拆除所有连接的MCP服务器（关闭标准输入+
// 终止子进程），而不是在main()之后将其静态销毁。
// 在退出时调用，以便井-behaved服务器立即退出并且楔入服务器受到限制，
// 而不是使关闭看起来像挂起。
void release_servers() noexcept;

// CONNECTED服务器以裸名称（没有mcp__前缀，没有排除过滤）广告
// 的每个工具。设置 选择面板 的 per-plugin 工具列表的单一
// 权威来源：enabled-vs-disabled 纯粹是从配置的
// tools.exclude 派生的，因此工具永远不会落在
// "live catalog" 和 "config" 视图之间（消失的 -row 错误）。
// 如果未连接服务器则为空。
[[nodiscard]] std::vector<std::string> mcp_server_tools(const std::string& server);

// 实时重新加载：从磁盘重新-read MCP 配置并重建进程范围的
// 连接池（生成新的-added 服务器，删除已删除的服务器）。
// 由 TUI 的 `agentty plugin add` 流程使用，
// 因此插件无需重新启动即可在 当前 会话中使用。
// 碰撞 mcp_generation()，以便下次访问时注册表的
// wire-cache re-projects 工具表面。
// 只要保留的工具快照引用 旧 池的提供程序，它们就会保持活动状态（可安全
// 防止 in-flight 工具调用）。返回重新加载后连接的服务器数量。
// 阻塞（与启动相同的连接握手+截止时间）；取消 UI 线程。
[[nodiscard]] std::size_t mcp_reload();

// MCP服务器与session/new一起发送的ACP客户端(Zed，JetBrains)，
// 以mcp.json形状：{"name":{"command":...,"args":[...],
// "env":{...}}}或{"name":{"type":"http"， "url"：...，
// "headers"：{...}}}。他们与 mcp.json 一起加入 池，
// 并在名字冲突中获胜。跨会话合并（一位编辑者，一份列表）；仅当发生变化时才重新连接。
void set_client_servers(const nlohmann::json& servers);
[[nodiscard]] std::vector<std::string> configured_server_names_from_client();

// 编辑者可以打开 URL-mode 引出：将它们通告到 MCP
// 服务器（此后建立连接）并将每个完成的引出报告给 `done`。
void set_url_elicitation(std::function<void(const std::string& elicitation_id)> done);

// ── 资源( MCP resources/*) ──────────────────────────────────────────
// 代理可以通过URI读取的资源（文件、数据库行、API文档、……）。
struct ResourceInfo {
    std::string uri;
    std::string name;
    std::string title;        // 人类标签（回到名字）
    std::string description;
    std::string mime_type;
    std::string server;       // 产地("mcp:github")
};
[[nodiscard]] std::vector<ResourceInfo> mcp_resources();
// 读取资源。成功时返回扁平文本（文本内容连接；blob 内容汇
// 总为 "[blob <mime> <bytes>B]"）。
// 当失败时，可选为空并且`err`携带人工消息。
[[nodiscard]] std::optional<std::string> mcp_read_resource(const std::string& uri, std::string& err);

// ── Prompts (MCP prompts/*) ──────────────────────────────────────────────
struct PromptArgInfo { std::string name; std::string description; bool required = false; };
struct PromptInfo {
    std::string name;          // 暴露的（可能命名空间的）名称
    std::string title;
    std::string description;
    std::vector<PromptArgInfo> arguments;
    std::string server;
};
[[nodiscard]] std::vector<PromptInfo> mcp_prompts();
// 渲染 a 提示词 为扁平字符串（角色-tagged消息加入）。
// `args` 是 提示词 模板参数的名称→值映射。失败时返回空可
// 选 + `err`。
[[nodiscard]] std::optional<std::string> mcp_get_prompt(
    const std::string& name,
    const std::vector<std::pair<std::string, std::string>>& args,
    std::string& err);

// 渲染 `/name a b c` 当名称为服务器提示词时：其
// 参数按照名称后面的单词的顺序填充，最后一个占据该行的其余部分。
// 当它不是 提示词 时为 nullopt；失败 渲染 作为短信。
// 与服务器对话：调用，它关闭了UI 线程。
//
// 离开该行的必需参数永远不会到达服务器 - 它的
// "missing required arguments" 拒绝既没有
// 命名该参数，也没有命名该行的形状，因此在此处构建了使用说明。
// 该情况设置 `usage_out`（当非 -null 时），
// 让呼叫者显示注释而不将其作为消息提交。
[[nodiscard]] std::optional<std::string> mcp_expand_prompt(std::string_view line,
                                                          bool* usage_out = nullptr);

} // namespace agentty::mcp
