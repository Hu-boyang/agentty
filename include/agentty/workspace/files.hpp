#pragma once
// 工作区 文件枚举 — 遍历活动的 工作区 根并返回适合@提及
// 选择面板、附件解析以及任何其他呈现
// 用户可能想引用的文件。
// 的 UI 的相对路径。
//
// 存在于 workspace/ 而不是 runtime/ 下，
// 因为它是纯文件系统 I/O（与 tool/util/fs_helpers.cpp
// 属于同一家族）：运行时 拥有 UI 状态（mention::Open
// 持有捕获的 快照），但遍历磁盘的行为是一个单独的问题。

#include <cstddef>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include "agentty/util/snapshot.hpp"

namespace agentty {

// 遍历工作区，返回至`cap` 工作区-relative文件路径，
// 其中二进制程序文件/公共构建和VCS目录被过滤掉。
//
// 返回 SNAPSHOT:O(1) 来分发（引用计数碰撞，
// 而不是每个路径的深层副本），并且只要调用者持有它，返回的句柄就保持其
// 缓冲区 处于活动状态 - 因此后台重新发布永远无法从读取器下面拉出地毯。
// 参见util/snapshot.hpp.
[[nodiscard]] util::Snapshot<std::vector<std::string>>
list_workspace_files(std::size_t cap = 5000);

// 构建工作区文件列表(单个-flight，重复调用安全)。
// 同步：启动时在后台任务上运行。落地后，
// list_workspace_files() 和 选择面板 的第一次打开是即时的。
// 一旦请求 `stop`，就提前返回，不发布任何内容。
void prewarm_workspace_files(std::stop_token stop = {}, std::size_t cap = 5000);

// 将CALLING 线程降低到平台提供的最低调度优先级（Linux上的
// SCHED_IDLE，其他地方的nice+19）。仅适用于 SPECULATIVE
// 工作 — `@`/`#` 预热为 选择面板 构建 缓存，用户可能永远
// 不会打开，因此它们必须在其他不需要的内核上运行。
//
// 存在是因为 "background thread" 只适用于 THREAD：
// 符号 扫描以全优先级扇出到 min(cores, 12) 工作线程，
// 并在启动时将每个核心固定约 6 秒（在 2864-file 源码树 上
// 测量的 1129%），这是第 38 期和第 36 期的一半。优先级，
// 而不是 线程，才是使工作真正成为背景的因素。
void deprioritize_prewarm_thread() noexcept;

// Non-blocking：文件列表已经建立了吗？作曲家打开 `@` 选择面板
// INSTANTLY 并显示 "indexing…" 提示，直到这是真的。
[[nodiscard]] bool files_ready();

// 记录用户引用了`path`(在选择面板中选择了它)，因此后来的`@`
// 将其排列在顶部附近。频率 — 最近-used-first。
void note_file_referenced(std::string_view path);

// ── Git 意识 ────────────────────────────────────────
// 文件与 git 的关系——最强的
// "this file matters right now" 信号。预热时计算一次；
// 驱动排名（脏文件浮动到空白 `@` 的顶部）和行标记 选择面板 渲染。
enum class GitTag {
    None,
    Modified,           // 工作树改变（脏）——最高优先级
    Staged,             // 索引更改（git add'd）
    Untracked,          // 新文件，尚未跟踪
    RecentlyCommitted,  // 在最后约 20 次提交中触及（较弱）
};

// 工作区-relative路径的git标签（当clean/unknown或信号步行尚未落地时无）。
// 廉价的地图查找，在任何 线程 上都是安全的。
[[nodiscard]] GitTag file_git_tag(std::string_view path);

// 针对当前项目根重建git-status映射SYNCHRONOUSLY。
// 在调用 线程 上运行两个 `git` 子进程（每个 8 s 超时），
// 因此这是用于预热步行和刚刚将固定装置 仓库 - 绝不
// 突变为 状态更新函数 的测试。
void refresh_git_signals();

// 在后台任务上重建 git-status 地图。 该 形成 状态更新函数 想要：
// 它立即返回，并且在重建时到达的请求已经在
// COALESCES 中飞行，而不是产生竞争对手。
//
// 合并就是重点。作曲家的 `@` 处理函数 使用从裸露的独立 线程 到 调用
// 的同步形式，因此按住 `@` 每次击键都会产生 线程 和两个 `git` 进程，
// 所有进程都竞相发布到一张地图中。 Single-flight 现在位于
// Model::UI::git_refresh_inflight 中，
// 而不是每个呼叫者都必须记住的规则中。
void refresh_git_signals_async();

// one-word标签+该路径对于选择面板的渲染行是否是"hot"(
// dirty/staged/untracked)。空标签⇒没有标签。
[[nodiscard]] std::string_view git_tag_label(GitTag tag);

// Case-insensitive 对路径列表进行
// 子串过滤。返回的索引指向原始 `files` 向量
// - 分派器 使用一个来解析游标→路径，
// 与视图呈现行的方式相同。
[[nodiscard]] std::vector<std::size_t>
filter_files(const std::vector<std::string>& files, std::string_view query);

} // namespace agentty
