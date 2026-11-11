#pragma once
#include <set>
#include <span>
#include <string>
#include <unordered_map>

#include <maya/widget/tool_body_preview.hpp>
#include "agentty/domain/conversation.hpp"

namespace agentty::ui {

// 每回合构建一次的Cross-tool语义索引的tool_calls列表：
// 路径 → {Grep 标记的行号}。同一路径上的后续 FileRead
// 继承这些作为 `highlight_lines`，镜像 agent_session.cpp
// 的 grep_hits → FileRead 接线，因此时间线
// 正文 将用户的眼睛锚定在助手在同一轮中先前标记的线路上。
using GrepHits = std::unordered_map<std::string, std::set<int>>;

// 走tool_calls跨度，解析各完成Grep工具的mar​​kdown
// 结构输出（`## Matches in <path>` 头文件 +
// `### L<start>-<end>`块），并将（路径→起始行）累积到返回的映射中。
[[nodiscard]] GrepHits collect_grep_hits(std::span<const ToolUse> tool_calls);

// ToolUse→ToolBodyPreview::Config。根据工具名称+状态选择被识别的正文
// 类型（BashOutput / FileRead / FileWrite / JSON /
// EditDiff / GitDiff / GrepMatches / TodoList /
// CodeBlock / Failure / None）并提取相关数据。
//
// `grep_hits`（当提供时）让`read`工具拾取由同一路径
// 上的早期Grep标记的行号，在呈现的FileRead 正文
// 中将它们显示为`highlight_lines`。
[[nodiscard]] maya::ToolBodyPreview::Config tool_body_preview_config(
    const ToolUse& tc, const GrepHits* grep_hits = nullptr);

// 构建-phase 标志。设置 true（通过 FrozenBuildScope），
// 同时 freeze_range 构建冻结的 快照； false（默认），而实时 末尾
// 是在每个 帧 上构建的。
//
// HISTORY：这用于在FULL 渲染（冻结）和有界头+末尾 WINDOW
// （活动）之间切换工具-card 正文，以保持每-frame成本O（窗口）。
// 那是 REMOVED - 窗口活动卡使其将 不同 行提交到本机 回滚缓冲区，
// 而不是完全冻结的卡，在冻结切换时移动提交的前缀（duplicated/wiped-card
// 回滚缓冲区 损坏）。 正文 现在是 两者 阶段 字节-identical
// 中的 完整 内容，因此切换是纯粹的 缓存 命中。 Per-frame
// 成本以不同的方式保持界限：每张 终端 工具卡都带有内容寻址的
// per-event hash_id (agent_timeline.cpp)，
// 因此 maya 捕获其绘制的 存储单元 一次，并在随后的
// 帧 中捕获 BLITS — 现场 末尾 中的高固定卡是
// Paint-once，而不是 re-laid-out 帧。
//
// 标志被保留（仍然由freeze_range限定范围）作为no
// -op-by-默认钩子：其上不再有针对正文 CONTENT的分支，
// 但它为未来发散的-build路径提供了一个关闭位置，
// 而无需重新引入live/frozen 正文不匹配。
class FrozenBuildScope {
public:
    FrozenBuildScope() noexcept;
    ~FrozenBuildScope();
    FrozenBuildScope(const FrozenBuildScope&)            = delete;
    FrozenBuildScope& operator=(const FrozenBuildScope&) = delete;
private:
    bool prev_;
};
[[nodiscard]] bool building_frozen() noexcept;

} // namespace agentty::ui
