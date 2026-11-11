#pragma once
// 叉 选择面板 —
// "branch this conversation into a fresh thread; pick how RAG behaves in the fork."
//
// 从命令选项板打开它（Ctrl+K → "Fork thread"）。
// 分叉ESCAPES一个完整的上下文窗口：新的线程以near
// -zero上下文开始FRESH（NOT是父级的副本或摘要）。
// 父级的完整转录本会导出到磁盘，并且分叉带有一个指向它的指针，
// 模型为 READS ON DEMAND — 因此，无论父级有多大，
// 分叉在令牌中的复杂度都是 O(1)。一个 面板，新 线程 的 RAG 行为
// 的三种选择：
//
//   RAG 每转叉；每次转弯时主动检索
//   First-turn RAG 前叉；
//   主动检索第一轮仅 RAG 关闭叉；没有主动注入（搜索工具仍然有效）
//
// Enter分叉：当前线程是已保存未受影响，创建新的线程（新id，
// forked_from=父代），用fork-note指针作为种子，
// 并且其RAG模式被设置为所选择的行为。原始的 线程 从未被修改过。
// Esc 关闭。完整设计请参见 docs/FORK.md。
//
// 仅UI-state； 状态更新函数 在 update/fork.cpp 中，键 分派 在 subscribe.cpp 中，
// 视图在 view/fork_view.cpp. 中

#include <variant>

namespace agentty {

namespace fork_panel {

// 三行，按显示顺序。
enum class Choice {
    RagPerTurn,     // 每回合鲜叉+RAG
    FirstTurnRag,   // 鲜叉+RAG首转而已
    RagOff,         // 新鲜叉子，无RAG
};

// 显示顺序。 一个 位置记录了布局：视图在其中移动
// 并且光标在其中移动，因此没有第二个列表可以漂移。
// （这取代了 枚举 内的 `Count_` 哨兵——每个交换机都必须
// 记住不要处理的假成员，这使得 `Choice` 成为包含非
// -choice 的集合。）
inline constexpr Choice kChoices[] = {
    Choice::RagPerTurn, Choice::FirstTurnRag, Choice::RagOff,
};
inline constexpr int kChoiceCount =
    static_cast<int>(sizeof(kChoices) / sizeof(kChoices[0]));

// 光标移动在枚举上封闭：环绕是类型的属性，
// 因此没有 调用 站点拥有可能出错的模数。
[[nodiscard]] constexpr Choice next_choice(Choice c, int delta) noexcept {
    const int n = kChoiceCount;
    const int i = ((static_cast<int>(c) + delta) % n + n) % n;
    return kChoices[i];
}

struct Closed {};
// 光标是CHOICE，而不是某人必须保持同步的列表的索引。
// 这里的 int 是远离智能模式错误的一个重构：重新排序或调整行大小，
// 并且每个 `static_cast<Choice>(index)` 默默地
// 意味着其他东西。
struct Open { Choice choice = Choice::RagPerTurn; };

} // namespace fork_panel

using ForkState = std::variant<fork_panel::Closed, fork_panel::Open>;

[[nodiscard]] inline bool fork_picker_is_open(const ForkState& s) noexcept {
    return std::holds_alternative<fork_panel::Open>(s);
}
[[nodiscard]] inline fork_panel::Open* fork_picker_opened(ForkState& s) noexcept {
    return std::get_if<fork_panel::Open>(&s);
}
[[nodiscard]] inline const fork_panel::Open*
fork_picker_opened(const ForkState& s) noexcept {
    return std::get_if<fork_panel::Open>(&s);
}

} // namespace agentty
