#pragma once
// Git-based 工作区 检查点 — Zed-agent "restore checkpoint" 机制。
// 在每个用户将 运行时 快照 和 整个 工作树状态（已跟踪+未跟踪，.
// gitignore 受到尊重）作为固定在
// refs/agentty/checkpoints/<id> 下的无父 git 提交之前。
// 恢复使工作树 字节-identical 变为 快照：从
// 删除 快照 以来创建的 源码树 和 文件重写文件（
// 忽略的文件 - 构建目录等 - 永远不会被触及）。
//
// 快照 永远不会移动 HEAD，永远不会触及用户的真实索引，
// 也永远不会创建存储条目：一切都针对从真实索引的副本中播种的一次性
// GIT_INDEX_FILE 运行（因此 `add -A` 仅
// re-hashes 更改了文件，而不是整个 仓库）。
//
// 所有函数对于 git 仓库 之外的 调用 都是安全的——它们返回
// false/nullopt 并且不执行任何操作。

#include <optional>
#include <stop_token>
#include <string>

#include "agentty/util/io.hpp"

namespace agentty::workspace {

// 当且仅当 工作区 根位于 git 仓库 内时为真。在第一个 调用 之后缓
// 存（仓库-ness 不会改变 mid-session）。
[[nodiscard]] bool in_git_repo(Io);

// Non-blocking 变体 为 UI/reducer 线程。
// 仅当 仓库 发现已强制 ALREADY（通过 prewarm_repo_info
// () 或较早的阻塞调用者）时，才返回缓存的答案；当 缓存 仍然冷时返回
// nullopt，因此调用者可以跳过本回合的 git 工作，而不是在 高频
// 路径 上生成 `git`。启动后的第一个 Enter（在启动预热着陆
// 之前）采用此 nullopt 路径，使 Windows 上的击键保持即时。
[[nodiscard]] std::optional<bool> in_git_repo_if_ready(Io);

// 预热缓存的 仓库 发现；在启动时在后台任务上运行它。
// 否则，第一个 调用 到 in_git_repo()/
// create_checkpoint() 会在提交路径上生成两个阻塞的
// `git rev-parse` 子进程——在 POSIX 上很便宜，
// 但进程创建在 Windows 上很重，并且恰好落在第一回合的关键路径上。
// 当用户仍在输入花费 高频路径 的动作时启动它。 幂等；
// 一旦请求 `stop`，就不执行任何操作。
void prewarm_repo_info(std::stop_token stop = {});

// 工作树已更改的 one-glance 摘要 SINCE
// 采取了检查点 - 固定的 快照 源码树 和当前工作 源码树
// 之间的差异（跟踪 + 新的 -added；忽略的文件从未计数）。
// 为 选择面板 的预览提供倒带功能，因此倒带永远不会盲目。
// 当不在 仓库 中时，全部为零/有效==假，引用消失，
// 或者没有任何改变。
struct CheckpointDiff {
    bool valid          = false;   // 摘要已成功计算
    int  files_changed  = 0;
    int  insertions     = 0;
    int  deletions      = 0;
};
[[nodiscard]] CheckpointDiff checkpoint_summary(const std::string& id);

// 快照 当前工作树并将其固定在
// refs/agentty/checkpoints/<id>。当不在 仓库
// 中或任何 git 步骤失败时返回 false。还修剪超出固定 keep
// -count 的最旧的检查点引用，以便引用不会永远累积。
bool create_checkpoint(const std::string& id);

// 当且仅当 refs/agentty/checkpoints/<id> 解析为提交时为真。
[[nodiscard]] bool checkpoint_exists(const std::string& id);

// 使工作树与检查点完全匹配：重写快照中记录的每个文件，
// 并删除拍摄时​​不存在的非-ignored文件。失败时返回
// false，如果 `error` 不是 -null，
// 则在其中写入一个简短的 human-readable 原因。
bool restore_checkpoint(const std::string& id, std::string* error = nullptr);

} // namespace agentty::workspace
