#pragma once
// 检查点选择面板-"rewind the workspace + transcript to any earlier point in this thread"，
// 不仅仅是最后一个。
//
// 在 git 仓库 内提交的每个用户轮次都会在该轮次上方固定一个工作树
// 快照（参见 workspace/checkpoint.hpp）和
// 渲染 个检查点分隔符。这些分隔线标榜可恢复点；这个 选择面板 使得
// 其中的 全部 可以访问（调色板的旧 "Rewind" 只出现在最新的）。
// 从命令面板打开它（Ctrl+K → "Rewind to checkpoint"）；
// 没有专用密钥绑定（Ctrl+R为OpenDiffReview）：
//
//   ↑↓ / j k 在检查点转弯之间移动（最新的在底部，
//              最接近作曲家 - 与抄本相同的空间
//              顺序）。
//   Enter 倒回到突出显示的回合：工作树
//              文件恢复到抄本截断到其之前的 快照 和，
//              并在作曲器中重新填充旧的 提示词。
//   Esc关闭，无变化。
//
// 每行显示回合数、提示词的one-line预览、相对时间戳，
// 以及——异步填充，因此即使在大的仓库上打开也是即时
// 的——工作树在那一点改变了SINCE的内容的
// "N files · +A −D"差异摘要，因此倒回永远不会盲目。
//
// 这个头文件只是UI-state。 状态更新函数 接线位于
// update/checkpoint.cpp，关键 分派 位于 subscribe.cpp，
// 视图位于 view/pickers.cpp — 与其他 选择面板 具有相同的分割。

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "agentty/domain/id.hpp"

namespace agentty {

namespace checkpoints {

// 一个可恢复点——检查点用户回合。
struct Entry {
    CheckpointId id;
    // 线程中用户轮次中的1-based 序号(转录本
    // 标记的轮次)。纯粹为了展示。
    int          turn = 0;
    // 提示词、标签占位符的第一行已经展平至
    // 其可见标签，针对行被截断。
    std::string  preview;
    // 自回合提交纪元起的 Wall-clock 毫秒 — 呈现
    // 为相对 "3m ago"，以便用户可以按时间和文本锚定。
    std::int64_t timestamp_ms = 0;

    // ── async diff摘要(由CheckpointDiffLoaded填充) ──────────
    // diff_state推进加载→Ready|每个条目仅失败一次。
    // 直到Ready，该行显示出细微的"…"；失败的行只是省略
    // 统计信息（检查点可能已被 git gc 修剪）。
    enum class DiffState : std::uint8_t { Loading, Ready, Failed };
    DiffState    diff_state    = DiffState::Loading;
    int          files_changed = 0;
    int          insertions    = 0;
    int          deletions     = 0;
    // 当就绪时为真，并且自此时起没有任何改变——呈现为
    // "no changes"，因此用户知道这里的倒带是磁盘上
    // 的no-op(它仍然截断转录)。
    bool         clean         = false;
};

struct Closed {};

struct Open {
    // Newest-last，因此列表读取top-to-bottom最旧→最新，
    // 匹配其上面的记录；光标在最后（最新）行上打开。
    std::vector<Entry> entries;
    int                index = 0;
};

} // namespace checkpoints

using CheckpointsState =
    std::variant<checkpoints::Closed, checkpoints::Open>;

[[nodiscard]] inline bool checkpoint_picker_is_open(
        const CheckpointsState& s) noexcept {
    return std::holds_alternative<checkpoints::Open>(s);
}
[[nodiscard]] inline checkpoints::Open*
checkpoint_picker_opened(CheckpointsState& s) noexcept {
    return std::get_if<checkpoints::Open>(&s);
}
[[nodiscard]] inline const checkpoints::Open*
checkpoint_picker_opened(const CheckpointsState& s) noexcept {
    return std::get_if<checkpoints::Open>(&s);
}

} // namespace agentty
