#pragma once
// agentty::ui::pick-面板光标状态机为和类型。
//
// 替换该 代码库 曾经具有的规范 anti-pattern：
//
//     struct ModelsState { bool open = false; int index = 0; };
//
// 其中，当`open == false`时，`index`无意义。
// 读者必须记住哪些字段在哪种组合中是有效的；作者必须记住在切换
// `open` 时重置（或不重置）`index`。
// 没有类型系统帮助。
//
// 现在：每个选择面板是一个封闭的`std::variant<Closed, Open{cursor…}>`。
// 游标数据由`Open`替代物拥有——当选择面板关闭时
// 它不能存在。添加新的选择面板形状（e.g.
// 多选）意味着新的变体臂+新的`pick::opened`过载；
// 编译器 告诉您所有需要更改的地方。
//
// 三种形状覆盖今天的选择面板：
//   OneAxis — 型号列表，线程 列表（单索引光标）
//   TwoAxis — 差异审查（文件 × 差异块
//   光标） 弹出面板 — 待办事项 覆盖面板（无光标；仅门 渲染）

#include <concepts>
#include <string>
#include <string_view>
#include <variant>

namespace agentty::ui::pick {

struct Closed {};

// One-axis 选择面板 (型号列表、线程列表）。
// `query` 是可选的增量 -search 缓冲区 — 目前只有型号 选择
// 面板 使用它； 选择面板 不碰它，将其留空并表现得与以前完全相同。
// `confirm_remove`携带待处理行的label/id一个两个-press删除
// (仅线程 选择面板，镜像SettingsListRemove/AccountRemove)。
// 任何move/select/new/close都会将其清除，因此流浪的`d`
// 永远无法删除线程而无法返回。
struct OpenAt {
    int index = 0;
    std::string query;
    std::string confirm_remove;
    // 仅融合模型选择面板：当非-empty时，
    // browse/filter列表的范围为THIS提供者ID（^/范围为
    // 所选行的提供者；再次按下^/将其清除）。空=每个提供商，默认值。
    std::string provider_scope;
};
using OneAxis = std::variant<Closed, OpenAt>;

// 二-axis 选择面板(差异审查：文件×差异块)。 `body_scroll` 是垂直
// 滚动偏移（在差异行中） 内部 聚焦 差异块 的加盖 正文 窗口 — ^D/^U/PgDn/PgUp
// 对其进行分页；任何光标移动都会将其重置为 0（新的 差异块 从其顶部开始）。
// `confirm_reject_all` 武装两个 -press ^X 守卫：
// 第一个^X设置它（页脚变成警告），第二个执行完全恢复；
// 任何其他关键解除 - 镜像 OpenAt 的
// confirm_remove，因此流浪的 ^X 永远不会丢弃整个变更集。
struct OpenAtCell {
    int file_index = 0;
    int hunk_index = 0;
    int body_scroll = 0;
    bool confirm_reject_all = false;
};
using TwoAxis = std::variant<Closed, OpenAtCell>;

// 弹出面板-style 选择面板即切换 open/close无游标(e.g.
// 待办事项 覆盖面板 — 项目与所属结构并存）。
struct OpenModal {};
using Modal = std::variant<Closed, OpenModal>;

// ── 帮助者 ───────────────────────────────────────────────────────────
// 选择面板状态是带有`Closed`替代方案的任何变体（以上所有三个形状都是如此）。
// 命名需求意味着 `is_open` 拒绝带有 one-line 概念
// 错误的非 -picker 类型，而不是 std::holds_alternative
// 深处的神秘故障。
template <class P>
concept PickerState = requires(const P& p) {
    { std::holds_alternative<Closed>(p) } -> std::same_as<bool>;
};

// 通用"is this variant in any non-Closed state?"-
// 适用于所有三种形状，因此调用站点统一读取：`if (pick::is_open(x))`。
template <PickerState P>
[[nodiscard]] inline bool is_open(const P& p) noexcept {
    return !std::holds_alternative<Closed>(p);
}

// 光标访问器。 选择面板关闭时返回nullptr；调用者使用
// `if (auto* c = pick::opened(p)) …` 将 open
// -check 与游标提取结合在一个表达式中 - 道德上相当于 Rust 中的
// `if let Open(c) = p`。
[[nodiscard]] inline OpenAt*       opened(OneAxis& p)       noexcept { return std::get_if<OpenAt>(&p); }
[[nodiscard]] inline const OpenAt* opened(const OneAxis& p) noexcept { return std::get_if<OpenAt>(&p); }
[[nodiscard]] inline OpenAtCell*       opened(TwoAxis& p)       noexcept { return std::get_if<OpenAtCell>(&p); }
[[nodiscard]] inline const OpenAtCell* opened(const TwoAxis& p) noexcept { return std::get_if<OpenAtCell>(&p); }

// 便利性：打开时索引，否则-1。仅在真正不关心 close
// -vs-open 区别（e.g。与列表大小相比）
// 的阅读网站上使用。尽可能选择 `opened()`。
[[nodiscard]] inline int index_or(const OneAxis& p, int fallback = -1) noexcept {
    if (auto* o = std::get_if<OpenAt>(&p)) return o->index;
    return fallback;
}

// 增量-search 选择面板使用的Case-insensitive子串测试。
// 空针匹配所有内容（空查询显示完整列表）。
// 仅限ASCII-fold——模型ID实际上是ASCII，并且完整的Unicode
// 情况-fold对于实时过滤器来说是过大的。
[[nodiscard]] inline bool fuzzy_contains(std::string_view hay,
                                         std::string_view needle) noexcept {
    if (needle.empty()) return true;
    if (needle.size() > hay.size()) return false;
    auto lower = [](char c) noexcept -> char {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
    };
    const std::size_t last = hay.size() - needle.size();
    for (std::size_t i = 0; i <= last; ++i) {
        std::size_t j = 0;
        for (; j < needle.size(); ++j)
            if (lower(hay[i + j]) != lower(needle[j])) break;
        if (j == needle.size()) return true;
    }
    return false;
}

} // namespace agentty::ui::pick
