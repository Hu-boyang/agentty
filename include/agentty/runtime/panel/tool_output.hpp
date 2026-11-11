#pragma once
// Tool-output检查器-可滚动的覆盖面板，用于读取当前
// 线程中任何固定工具调用的FULL存储的输出。
//
// 为什么时间线卡上是 覆盖面板 而不是
// expand/collapse：记录的已提交行存在于本机 终端
// 回滚缓冲区 中，即 不可变 — 在适当位置生长卡将重写已
// 提交行并强制产生破坏性的 HardReset (冻结前缀
// 体系结构存在以防止的确切损坏类别)。 覆盖面板 严格覆盖实时
// 可视区域（与 选择面板 / code-block 结果卡
// 相同的机器），因此下面的成绩单永远不会被触及。
//
// 流程：Ctrl+O→最近工具输出列表（最新的在前）→Enter→
// 完整输出，可滚动→Esc返回→Esc关闭。 `y` 复制 正文。
//
// 条目快照在OPEN时间输出字节（有界：kMaxEntries项，
// kSnapshotBudget总计字节），因此覆盖面板不受其下面的
// 转录本突变的影响（流式传输轮附加消息，迟来的ToolExecOutput
// 解决卡）。 per-tool 存储的输出已经被限制为 256 KiB
// (update/tool.cpp kStoredOutputCap)，
// 因此 快照 很便宜。
//
// 该头文件仅是UI状态。 状态更新函数 接线位于
// update/tool_output.cpp，键 分派 位于 subscribe.cpp，
// 视图位于 view/pickers.cpp — 与另一个 选择面板 分割的文件相同。

#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "agentty/domain/conversation.hpp"

namespace agentty {

namespace tool_output {

// 快照预算：一旦任一上限跳闸，最新-first收集就停止。
inline constexpr std::size_t kMaxEntries     = 50;
inline constexpr std::size_t kSnapshotBudget = 4u * 1024 * 1024;  // 4MiB

struct Entry {
    std::string name;      // 原始工具名称 ("read") — 驱动类别颜色
    std::string title;     // 显示名称("Read"，"Git Commit")
    std::string detail;    // 一个-line细节("src/foo.cpp @120 · 450 lines")
    std::string trailing;  // "ok · 4.2s · 48 KB"或"failed · 0.3s"
    std::string output;    // 完全存储的输出（≤ 256 KiB by 上游 上限）
    bool        failed = false;
    // 实时条目：工具CURRENTLY正在运行，固定为第0行。
    // 其`output`是不断增长的progress_text (
    // stdout+stderr 末尾)而不是固定的快照，并且视图末尾
    // -anchors它+标记行"Live"。查看器打开时，
    // 根据每个工具 事件 上的记录进行重建，因此 数据流 就位。每个列表最多一个。
    bool        is_live = false;
    // 固定工具调用的快照。 正文 阶段 渲染 它通过时间线使用的 相同
    // maya::ToolBodyPreview 路径 (show_all)
    // - 因此 edit/write 显示彩色差异，read/write
    // 显示线 -number 装订线，git_diff 显示 +/-
    // 带等 - 而不是平面单色文本转储。对整个 ToolUse（名称
    // + 参数 + 终端 输出）进行快照可以使 覆盖面板 免受转录
    // 本突变的影响，并且副本受到 256 KiB 输出钳的限制。
    ToolUse     call;
};

struct Closed {};

struct Open {
    std::vector<Entry> entries;   // 最新第一
    int                index   = 0;
    bool               viewing = false;   // 假=列表，真=正文阶段
    // 在正文中查找(/然后n/N)。当您跳到另一个条目时，
    // 查询会保留下来，因此可以跨输出检查一个搜索。
    std::string        query;
    bool               typing  = false;   // /提示词已打开
    int                hit     = 0;       // 当前匹配（换行；视图解决它）
    int                hit_seq = 0;       // 每次跳跃时都会发生碰撞；视图滚动到它
};

// 观看者为条目显示和复制的内容：没有
// 工具写给模型的说明、ANSI 转义序列，或外层包裹的 ``` 围栏。
// 表示工具削减其输出的注释保留为 "⋯" 行。
[[nodiscard]] std::string display_text(const Entry& e);

// 字节跨越`line`中的`q`，忽略ASCII的情况(与卡的搜索
// 突出显示相同)。
[[nodiscard]] std::vector<std::pair<std::size_t, std::size_t>>
hit_spans(std::string_view line, std::string_view q);

} // namespace tool_output

using ToolOutputState = std::variant<tool_output::Closed, tool_output::Open>;

[[nodiscard]] inline bool tool_viewer_is_open(const ToolOutputState& s) noexcept {
    return std::holds_alternative<tool_output::Open>(s);
}
[[nodiscard]] inline tool_output::Open*
tool_viewer_opened(ToolOutputState& s) noexcept {
    return std::get_if<tool_output::Open>(&s);
}
[[nodiscard]] inline const tool_output::Open*
tool_viewer_opened(const ToolOutputState& s) noexcept {
    return std::get_if<tool_output::Open>(&s);
}

} // namespace agentty
