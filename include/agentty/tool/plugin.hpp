#pragma once
// agentty::tools::plugin-MCP插件管理器(`agentty plugin â€¦`)。
//
// agentty中的"plugin"、IS中的MCP服务器条目在mcp.json中
// ——不存在单独的插件运行时、注册表或清单格式。该模块是这个事实
// 之上的薄层人体工程学层：它干净地编辑 mcp.json，
// 因此用户永远不会手握 -write JSON，并提供常见运行时的配方：
//
//   agentty 插件添加天气 --uvx mcp-weather # PyPI，通过 uv agentty
//   插件添加 mytool --python tools/my_mcp.py # 本地脚本 agentty
//   agentty plugin add fs --npx @modelcontextprotocol/server-filesystem
//   agentty 插件添加自定义 -- ./bin/server --flag x # 原始
//   argv agentty
//   插件列表 agentty 插件删除天气
//
// 范围：--user（默认）编辑~/.agentty/mcp.json；
// --project 编辑 ./.agentty/mcp.json.
// 项目配置还需要现有的 AGENTTY_MCP_ALLOW_PROJECT=1 信任门，
// 然后 agentty 将 CONNECT 发送给它们（始终允许写入条目；
// 该门可防止克隆的 -repo 配置运行代码，而不是防止用户自己的编辑）。
//
// 编辑合同（由plugin_config_test固定）：
//   · Round-trip 安全：该模块不拥有的每个密钥（其他服务器，"servers" 拼写，
//     未知的 top-level 密钥，per-server env/url/â
//     …）都以 JSON 值形式保留 字节-for-byte。
//   添加现有名称失败，除非 --force（无静默破坏）。
//   删除不存在的名字是一个独特的"not found"结果。
//   首次添加时会创建丢失的文件（带有父目录）。

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>
#include <maya/runtime.hpp>
#include "agentty/util/io.hpp"
#include "agentty/util/lock_levels.hpp"

namespace agentty::tools::plugin {

struct ServerSpec {
    std::string              name;
    std::string              command;
    std::vector<std::string> args;
    std::string              url;   // HTTP/SSE 传输层（无命令）
    std::string              type;  // "http" | "sse" | "passthrough" | ""（url 默认为 http）
    // type == "passthrough"：要注册 分派 的外部工具
    // 名称（模型端点前面的代理通告其 模式定义；agentty 通过将参数
    // POSTing 到 `url` 来执行）。
    std::vector<std::string> passthrough;
};

enum class EditResult : std::uint8_t {
    Ok,
    AlreadyExists,   // 添加不带 --force 的现有名称
    NotFound,        // 删除不存在的名字
    ParseError,      // 现有文件无效 JSON — 拒绝触及它
    IoError,         // 无法read/write路径
};

// 将`spec`添加到`path`处的JSON文件的
// mcpServers对象，如果不存在则创建该文件。保留所有其他密钥。
// `force` 覆盖同名的现有条目。
[[nodiscard]] EditResult add_server(Io, const lock_levels::below_file_lane&, const std::filesystem::path& path,
                                    const ServerSpec& spec, bool force);

// 逐字添加RAW服务器条目，保留其携带的每个密钥。
//
// add_server()从ServerSpec构建其条目，其仅对agentty自己的
// `plugin add`流可以设置的字段（command/args/url/type)进行建模。
// 这对于 CLI 来说是正确的，其中人类键入参数，WRONG 用于采用：
// `mcp.json` 是跨工具共享的一个 模式定义，因此我们从另一客户端复制的
// 条目可以合法地携带 `env`、`headers`、`timeoutMs`、
// `connectTimeoutMs` 或 `tools.exclude`——
// 所有这些都是 agentty 自己的连接路径读取的。 Re-deriving 一个
// 规范并从中重建一个条目会默默地删除它们。
//
// 丢弃`env`是其中最糟糕的。在其环境块中配置了 API
// 密钥的服务器导入看起来很完整，然后在生成时失败，
// 并出现未命名任何内容的身份验证错误。有损导入比被拒绝更糟糕。
//
// 所以导入并不翻译。它会验证（调用者检查是否存在命令或 URL）并复制。
// 与 add_server 相同的 no-clobber 合同。
[[nodiscard]] EditResult add_server_raw(Io, const lock_levels::below_file_lane&, const std::filesystem::path& path,
                                        const std::string& name,
                                        const nlohmann::json& entry,
                                        bool force);

// 就地编辑 EXISTING 条目，保留规范不拥有的每个密钥（环境块、头文件、
// timeoutMs、tools.exclude——用户手上的任何 -wrote）。
// add_server(force=true) 替换条目 WHOLESALE，
// 这对于 CLI 的 re-add 是正确的，但对于表单的 保存 是错误的：保存
// URL 调整不得删除其旁边的 env 块。仅重写ServerSpec型号（
// command/args/url/type/passthrough）的字段；
// absent-in-spec 传输层 密钥仅当 KIND 拥有它们时才会被删除（
// stdio 规范清除 url；http 规范清除 command/args）。
[[nodiscard]] EditResult update_server(Io, const lock_levels::below_file_lane&, const std::filesystem::path& path,
                                       const ServerSpec& spec);

// 移除指定服务器。保留其他一切。
[[nodiscard]] EditResult remove_server(Io, const lock_levels::below_file_lane&, const std::filesystem::path& path,
                                       const std::string& name);

// Enable/disable WHOLE服务器而不删除它——在
// mcp.json中保留为服务器的top-level `disabled`
// 标志（桥已经在连接时跳过禁用的服务器）。这是插件行上的主要
// Enter操作：可逆的on/off，与破坏性删除不同。No-op-Ok，
// 如果已经在期望中状态。
[[nodiscard]] EditResult set_server_disabled(Io, const lock_levels::below_file_lane&, const std::filesystem::path& path,
                                             const std::string& name,
                                             bool disabled);

// 当命名服务器在mcp.json中携带`"disabled": true`时为真。
[[nodiscard]] bool is_server_disabled(const std::filesystem::path& path,
                                      const std::string& name);

// 列出文件中的服务器(missing/invalid文件上为空)。
[[nodiscard]] std::vector<ServerSpec>
list_servers(const std::filesystem::path& path);

// 服务器的Enable/disable ONE工具，作为服务器的
// `tools.exclude`列表持久保存在mcp.json中（桥
// 已经尊重它）。下次重新加载时，已禁用的工具将从 传输 目录 中删除。
// `bare`是工具的简称(e.g."current_date"、不、
// mcp__server__tool形式)。如果已处于所需状态，则为 No-op-Ok。
[[nodiscard]] EditResult set_tool_enabled(Io, const lock_levels::below_file_lane&, const std::filesystem::path& path,
                                          const std::string& server,
                                          const std::string& bare,
                                          bool enabled);

// 将一个工具固定或取消固定：`tools.pin`在mcp.json中。
// 固定工具始终是 已加载，从不按需 (#98)。形状与上面相同，没有 -op 规则。
[[nodiscard]] EditResult set_tool_pinned(Io, const lock_levels::below_file_lane&, const std::filesystem::path& path,
                                         const std::string& server,
                                         const std::string& bare,
                                         bool pinned);

// 当 `bare` 位于服务器的 tools.exclude 中时为真（i.e.禁用）。
[[nodiscard]] bool is_tool_disabled(const std::filesystem::path& path,
                                    const std::string& server,
                                    const std::string& bare);

// 服务器的 tools.exclude 列表中的裸名称（其禁用的工具）。
// 如果 server/list 不存在，则为空。
// 让 UI 显示禁用的工具（这些工具从实时 池 中删除，否则看不见）。
[[nodiscard]] std::vector<std::string>
disabled_tools(const std::filesystem::path& path, const std::string& server);

// 范围的配置路径。用户→~/.agentty/mcp.json，
// 项目→./.agentty/mcp.json.
[[nodiscard]] std::filesystem::path config_path(Io, bool project);
// 同样，给定用户root，调用者已经解决。纯的。
[[nodiscard]] std::filesystem::path config_path(bool project, const std::filesystem::path& user_root);

// ——项目-config信任（RCE门） －－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－
// －－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－
// －－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－
// －－－＞-local ./.agentty/mcp.json可以骑在克隆体上并产生任意命令，
// 因此它的服务器在人类为其担保之前不会连接：AGENTTY_MCP_ALLOW_PROJECT=1，
// 或文件的批准CONTENT HASH记录在这里。编辑文件会更改哈希值并重新-gates它（MCPoison
// 修复）。批准仍然存在~/.agentty，因此克隆既不能批准自己，也不能在其字节更改后保持批准有效。
//
// is_project_config_trusted() – 项目配置现在可以连接吗？
// approve_project_config()—记录对CURRENT
//                               项目配置内容的信任；如果没有项目配置或商店
//                               无法写入，则返回 false。
[[nodiscard]] bool is_project_config_trusted(Io);
[[nodiscard]] bool approve_project_config(Io, const lock_levels::below_file_lane&);

// Per-server信任——比整个-file门更精细。信任与ONE服务
// 器的规范（其命令+参数）绑定，因此批准`date`不会祝福后来的
// -added `db`，并且编辑`date`的命令re-gates仅
// `date`。如果毯子环境，服务器被视为可信opt-in 已设置，
// OR 整个项目文件已获得批准，OR 该服务器自己的规范哈希已获得批准。
//   is_server_trusted(名称) – 此 项目服务器现在可以连接吗？
//   approve_server(name) — 记录对此服务器当前规范的信任；
//                             如果 server/config 不存在或不可写，则为 false。
[[nodiscard]] bool is_server_trusted(Io, const std::filesystem::path& path,
                                     const std::string& name);
[[nodiscard]] bool approve_server(Io, const lock_levels::below_file_lane&,
                                  const std::filesystem::path& path, const std::string& name);

// 一台服务器规范（命令+url+args）的spawn-identity散列
// ——实际运行的字节。桥接器的连接循环根据 LIVE 规范构建这些内容，
// 并根据批准存储检查结果，因此 file-side 和
// live-side 在身份上达成一致。当命令 和 url 均
// 为空时为空（没有可信任的内容）。
[[nodiscard]] std::string server_spec_hash(const std::string& command,
                                           const std::string& url,
                                           const std::vector<std::string>& args);

// `agentty plugin` CLI:动词∽{添加、删除、列表}，动词后带有argv 末尾。
// 返回进程退出代码。打印 results/errors，并在成功添加后打印一条简短
// 的 "restart to connect / trust gate" 注释。
int cli(Io, const maya::unlocked&, const std::vector<std::string>& argv);

} // namespace agentty::tools::plugin
