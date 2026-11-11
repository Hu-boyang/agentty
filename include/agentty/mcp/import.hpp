#pragma once
// agentty::mcp::import_-将另一个工具的MCP服务器采用到我们的服务器中。
//
// ── 这个问题答案,为什么它不是一个读根 ────────────────
//
// #60：“读取 ~/.ai/mcp/mcp.json 中的 MCP 服务器配置。
// 这是 JetBrains IDE 使用的全局路径，希望能把自定义 MCP 服务
// 器集中在一处。”
//
// 文字询问是第六读取位置。实际的愿望与此相反，
// 区别在于整个设计：
//
//   如果该文件为agentty READ，则用户的服务器将位于TWO位置，
//   并且它们之间具有优先规则。他们要求 一个 的位置。
//
// 因此，听起来像请求的特征将传递其相反的内容。 Import 给出的正是
// 他们所说的：在 `agentty mcp import` 之后，
// 每个服务器都在 ~/.agentty/mcp.json 中，没有其他可跟踪的内容。
//
// ── 裁定为-vs-import的规则,对于任何外国人 ───────────
//
//     作用于该内容EXECUTE有什么吗？
//       否 → 现场阅读。陈旧是唯一的风险。
//       是→导入它。显示差异，写下来，拥有它。
//
// 指令、规则和技能都是数据：实时阅读另一个工具的副本的
// 代价并不比过时更糟糕。 `mcp.json` 产生进程。
// Live-reading 是一个我们不拥有、无法验证且我们从未观察
// 到的文件，它的编辑会让 ANOTHER TOOL 的配置改变我们执行
// 的内容——默默地，因为没有任何东西告诉我们它发生了变化。这是信任边界，
// 而不是路径查找。
//
// 导入还将模式定义问题压缩到一处。外部配置路径不同，嵌套AND拼写；
// 读取根需要一个 per-dialect 叶子和一个 per
// -dialect 解析器，此时 scope::plan 不再是
// Locus × Dialect 产品，而是变成带有标签的
// hand-written 路径列表。该清单正是该产品的发明目的。
//
// ── 在议题标题中的路径 ───────────────────────────────────────
//
// 没有~/.ai/mcp/mcp.json.
// JetBrains自己的文档将Junie的user-scope配置放在~/.junie/mcp/mcp.json
// (项目范围：.junie/mcp/mcp.json)，并且模式定义是相同的
// `mcpServers`对象agentty已经说话。记者记住了形状而不是目录——这
// 正是为什么 `agentty config mcp` 现在 PRINTS 它不读取的来源，
// 只有一个命令采用它们。

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <maya/runtime.hpp>
#include "agentty/util/io.hpp"
#include "agentty/util/lock_levels.hpp"

namespace agentty::mcp::import_ {

namespace fs = std::filesystem;

// MCP配置agentty可以采用的工具。
//
// 这是FOREIGN形状的注册表，
// 故意保留在agentty::scope之外。范围的轴（轨迹 ×
// 方言）描述 OUR 布局，其中仅目录名称不同；它们的嵌套和
// 文件名也有所不同，因此在那里建模会破坏产品。在这里它们只是数据。
enum class Tool : std::uint8_t {
    Junie,      // ~/.junie/mcp/mcp.json·.junie/mcp/mcp.json
    ClaudeCode, // ~/.claude.json（嵌套）·./.mcp.json
    Cursor,     // ~/.cursor/mcp.json·.cursor/mcp.json
    VsCode,     // ——·.vscode/mcp.json
    Codex,      // ~/.codex/config.toml·.codex/config.toml
};

[[nodiscard]] std::string_view name_of(Tool) noexcept;
[[nodiscard]] std::vector<Tool> all_tools();

// 解析 `name`作为工具ID("junie"、"claude"、"cursor"、"vscode"、"codex")。
[[nodiscard]] bool parse_tool(std::string_view, Tool& out) noexcept;

// 工具保存其配置的地方。如果工具没有这样的范围（VS 代码没有记录的用户
// -level mcp.json），则两者都可能为空。
struct Locations {
    fs::path user;
    fs::path project;   // 相对于项目根
};
[[nodiscard]] Locations locations_of(Tool, const fs::path& home,
                                     const fs::path& project);

// 在外部配置中发现一台服务器。
//
// `command`/`url`仅被解码，因此扫描可以SHOW你所发现的
// 内容并拒绝没有传输层的条目。导入本身会逐字复制 `entry`
// —— `mcp.json` 是跨工具的 模式定义，因此环境块、
// 自定义 头文件 或 超时 都会保留下来。翻译 field-by-field
// 是导入默默地丢失使服务器工作的 API 密钥的方式。
struct Found {
    std::string name;
    std::string command;     // 用于显示+验证
    std::string url;         // 用于显示+验证
    nlohmann::json entry;    // WHOLE源对象，复制为-is
    fs::path    from;        // 从中读取的文件
    // 当agentty已经具有该名称的服务器时设置。
    // 导入永远不会默默地替换一个：名称冲突是用户的 调用，
    // 猜错会将命令交换到已批准的名称下 -
    // MCPoison 形状，从前门到达。
    bool        conflicts = false;
    // 来自agentty无法表达的源的设置，因此扫描可以说出未
    // 遇到的内容（例如Codex的env-var承载令牌）。
    std::vector<std::string> notes;
};

// 工具的配置在两个范围内提供的一切。
struct Scan {
    Tool                 tool{};
    std::vector<fs::path> looked_in;   // 所探测的每条路径，存在或不存在
    std::vector<Found>    servers;
    std::string           error;       // 格式错误的配置：拒绝，不要猜测
    // 存在但不可导入的条目：没有命令或url，或者名称agentty
    // 无法管理。算起来，扫描可以 SAY 这样 -
    // "3 of 5 imported" 没有任何解释，有人就断定它已损坏。
    int                   skipped = 0;
};

// 读取工具的配置。 Pure-ish：仅涉及文件系统，
// 不写入任何内容。丢失的文件不是错误（它不会产生任何服务器）。
[[nodiscard]] Scan scan(Tool, const fs::path& home, const fs::path& project);

// 扫描每个已知的工具，跳过那些没有提供任何内容的工具。
[[nodiscard]] std::vector<Scan> scan_all(const fs::path& home,
                                         const fs::path& project);

struct Options {
    bool dry_run = false;   // 打印会发生什么，什么也不碰
    bool force   = false;   // 也采用冲突的名称（替换我们的）
};

struct Outcome {
    int imported  = 0;
    int skipped   = 0;      // 冲突不受影响
    int failed    = 0;
    fs::path into;          // 文件写入
};

// 将`servers`采用到agentty的USER配置(~/.agentty/mcp.json)中。
//
// 有意的用户范围：导入的服务器来自人类自己的全局工具配置，
// 因此它是他们的，而不是仓库的——将其写入
// 项目文件会将某人的个人设置提交到版本控制中，
// 并且还将其置于需要保证运行的位置。
[[nodiscard]] Outcome adopt(Io, const lock_levels::below_file_lane&, const std::vector<Found>&, const Options&);

// 命令行：`agentty mcp import [--from <tool>] [--dry-run] [--force]`。
// 在没有--from的情况下，扫描每个已知工具并报告其发现的内容。
int cli(Io, const maya::unlocked&, const std::vector<std::string>& argv);

}  // namespace agentty::mcp::import_
