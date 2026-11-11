#pragma once
// agentty::slash-提示词中的前导`/name`意味着什么。
//
// 每个斜杠名称有一个 目录 (内置 -in 动作、用户命令、技能、
// MCP 提示词)以及其上的一个解析器。 TUI 作曲家菜单、TUI 提交、
// ACP 和 `agentty run` 都使用这些，因此它们就
// `/name` 是什么达成一致。纯粹：没有IO，没有发现。调用者根据已有的
// 已加载 构建 目录。

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace agentty::slash {

enum class Kind : std::uint8_t { Action, Command, Skill, Prompt };

// 内置-in有意义的动作带类型的。每个都运行一个主机操作，而不是 提示词。
// `takes_args` 操作使用该行的其余部分 (`/rename x`)。
enum class Action : std::uint8_t {
    New,
    Compact,
    Model,
    Rename,
    Fork,
    Rewind,
    Review,
    Threads,
    Settings,
    Tools,      // 打开插件 面板：什么是 已加载，按需，固定（#98）
    Login,
    Quit,
};

struct Entry {
    std::string name;          // 无前导'/'
    std::string description;
    std::string hint;          // 参数提示，无时为 ""
    Kind        kind = Kind::Command;
    Action      action = Action::New;   // 仅限Kind::Action
    bool        takes_args = false;     // 仅限Kind::Action
};

struct Catalog {
    std::vector<Entry> entries;   // 按名称、优先顺序进行重复数据删除
};

// 内置-in动作，按照菜单列出它们的顺序。
[[nodiscard]] const std::vector<Entry>& builtin_actions();

// 将源合并到一个 目录 中。早期消息来源赢得了名称冲突：
// 动作，然后命令，然后技能，然后MCP 提示词。
struct Source {
    std::string name, description, hint;
};
[[nodiscard]] Catalog make_catalog(const std::vector<Source>& commands,
                                   const std::vector<Source>& skills,
                                   const std::vector<Source>& prompts,
                                   bool with_actions = true);

// 文本以`/name`开始，及其后面的内容。
struct Head {
    std::string_view name;   // 没有'/'，可以为空("/")
    std::string_view args;   // 第一个空白之后，在前面修剪
    bool             has_space = false;   // 名称后面有一个空格
};
// nullopt 除非文本以 '/' 开头（没有前导空格）。
[[nodiscard]] std::optional<Head> head(std::string_view text);

// 提交`text`应该做什么。
struct Resolved {
    enum class What : std::uint8_t { Plain, Action, Command, Skill, Prompt };
    What        what = What::Plain;
    const Entry* entry = nullptr;   // 进入目录;纯文本为空
    std::string args;               // 其余行
};
[[nodiscard]] Resolved resolve(std::string_view text, const Catalog& cat);

// ── 作曲菜单 ────────────────────────────────────────────────────

// 菜单是否应显示该文本和光标，以及其过滤所依据的查询。
// 它显示，当 光标 位于第一行的前导 `/name` 标记内
// 时：还没有空格 带类型的，没有第二个 '/'，前面没有换行符。
struct MenuQuery {
    std::string_view query;   // 到目前为止的名称 带类型的（可能为空）
};
[[nodiscard]] std::optional<MenuQuery> menu_query(std::string_view text, int cursor);

struct Match {
    const Entry* entry = nullptr;
    int          score = 0;
    std::vector<int> hits;   // 字节 偏移到匹配的条目->名称
};
// `query`的条目，最好首先：准确名称、名称前缀、
// 模糊名称，然后是描述命中。空查询以 目录 顺序列出所有内容。
[[nodiscard]] std::vector<Match> filter(const Catalog& cat, std::string_view query);

// 完成`entry`后的文本变成`text`(替换
// `/name`令牌，添加空格，保留令牌之后的任何内容)
// 和新的光标，就在空格之后。
struct Completed {
    std::string text;
    int         cursor = 0;
};
[[nodiscard]] Completed complete(std::string_view text, const Entry& entry);

}  // namespace agentty::slash

// ── Model-facing 帮助者( TUI) ───────────────────────────────────────
namespace agentty { struct Model; }

namespace agentty::slash {

// 目录用于TUI：内置-ins，然后是库的命令和技能，
// 然后是插件快照所持有的MCP 提示词。
[[nodiscard]] Catalog catalog_of(const Model& m);

// 当前作曲家的菜单行，或者当其隐藏时为空（没有前导/名称标记，
// 以 Esc 消除，或者没有任何匹配）。
[[nodiscard]] std::vector<Match> menu_rows(const Model& m, const Catalog& cat);

}  // namespace agentty::slash

// ── 无头呼叫者( Name ACP, `agentty run`) ───────────────────────────────
#include "agentty/util/io.hpp"

namespace agentty::slash {

// 盘中的目录+现场MCP、池:命令、技能、MCP、提示词。
// `with_actions`添加了TUI的built-ins（无头呼叫者在询问
// 之前处理其自己的几个，如ACP的/new和/compact）。
[[nodiscard]] Catalog discover(Io io, bool with_actions = false);

// 当命名命令、技能或 MCP 提示词 时，`text` 应
// 发送为：扩展模板、技能指令加上任务或渲染的 提示词。如果不是 1，
// 则 nullopt（将 `text` 发送为 带类型的）。
// `skill_already_active`让具有转录本的呼叫者
// 跳过re-sending中已经存在的技能正文。
//
// 当线路无法发送时，设置 `usage`：MCP 提示词 缺少必需的参数。
// 返回的文本是给用户的 one-line 注释 (
// "/srv:p needs query — try /srv:p <query>")，而不是给模型的消息。
struct Expanded {
    std::string text;
    bool        usage = false;
};
[[nodiscard]] std::optional<Expanded> expand_checked(
    Io io, std::string_view text, const Catalog& cat,
    bool skill_already_active = false);
[[nodiscard]] std::optional<std::string> expand(
    Io io, std::string_view text, const Catalog& cat,
    bool skill_already_active = false);

}  // namespace agentty::slash
