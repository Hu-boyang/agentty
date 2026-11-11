#pragma once
// agentty::tools::commands——用户-authored斜线命令。
//
// 斜杠命令是用户从编辑器调用为 `/name [arguments]`
// 的降价 提示词 TEMPLATE。提交时，模板 正文（已
// 替换参数占位符）将替换 带类型的 文本，并且回合运行就像用户
// 拥有 带类型的 扩展的 提示词 一样。命令是 提示词
// MACROS — 它们补充了命令面板（它像 NewThread
// 一样调度主机 ACTIONS）而不是重叠它。
//
// 文件格式（Claude代码兼容-.claude/commands/<name>.
// md 加载不变）：
//
//     ---
//     描述：检查文件的安全问题。   # 可选参数
//     -hint: <file> [焦点] # 可选
//     ---
//     检查$1的安全问题，重点关注$2。
//     完整请求：$ARGUMENTS
//
// 占位符，在提交时替换：
//   $ARGUMENTS `/name ` 逐字 $1 …
//   $9 空白-split 位置词之后的所有内容（后面的词不是 re-joined：
//                "a b c" 的 $2 是 "b"）。不存在 → 空字符串。
//   $$ 字面美元符号（逃生舱口）
//
// 发现根源、优先顺序（首先击中名称获胜）——技能加
// 载器的精确镜像，因此两个生态系统感觉相同：
//
//   项目 <cwd>/.agentty/commands/<name>.
//   md 项目 <cwd>/.agents/commands/<name>.
//   md 项目 <cwd>/.claude/commands/<name>.md (Claude 代码兼容)
//   用户 ~/.agentty/commands/<name>.
//   md 用户 ~/.agents/commands/<name>.
//   md 用户~/.claude/commands/<name>.md（Claude代码兼容）
//
// 子目录命名空间命令名称采用`:`(Claude约定)：
// .claude/commands/git/fixup.md被调用为/git:fixup。
//
// 解析是LENIENT类似的技能：缺少frontmatter
// 则整个文件是正文；缺失的描述回溯到 正文 的第一行。

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "agentty/util/io.hpp"

namespace agentty::tools::commands {

struct Command {
    std::string name;          // 调用名称（以 ':' 加入的子目录）
    std::string description;   // frontmatter 或 正文-first-line 回退
    std::string argument_hint; // 前题`argument-hint`
    std::string body;          // 提示词模板
    std::string source;        // "project" | "user"
    std::string file;          // 绝对路径（诊断）
};

// 发现+解析项目+用户root下的每条命令。缓存的进程-wide以根目录+
// 文件的mtimes为键（编辑→下一个查找re-scans）。有界：每个
// 正文 的 kMaxCommands 条目、kMaxBodyBytes。
// 按值返回：缓存可以被另一个线程的重新扫描替换。
[[nodiscard]] std::vector<Command> all(Io);

// 精确-name查找。
[[nodiscard]] std::optional<Command> find(Io, std::string_view name);

// 使用原始参数字符串 `args` (用户 带类型的 在 `/name ` 之后的所有内容)
// 替换 `body` 中的 $ARGUMENTS / $1..$9 / $$。
[[nodiscard]] std::string expand(std::string_view body, std::string_view args);

// 如果`text`是斜杠-command调用（`/name`或`/name args`，
// 与发现的命令匹配的名称），则返回完全展开的提示词。
// 否则std::nullopt——文本提交不变。与 NO 命令
// 匹配的前导 `/` 失败（用户可以合法地使用 /
// etc/hosts 等路径启动消息）。
[[nodiscard]] std::optional<std::string> try_expand(Io, std::string_view text);
// 对于呼叫者已有的列表(状态更新函数的快照)也是如此：
// 没有发现，没有磁盘。
[[nodiscard]] std::optional<std::string> try_expand(std::string_view text,
                                                    const std::vector<Command>& known);

// 测试接口边界:在下一个all()上强制执行re-scan，而不管mtime 缓存如何。
void invalidate_cache();

} // namespace agentty::tools::commands
