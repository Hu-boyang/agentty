#pragma once
// @file提及选择面板——当用户在空的缓冲区上键入`@`
// 时在编辑器上方打开。
//
// 状态是 工作区 文件路径上的 FilteredPicker：查询、游标、
// 快照、过滤器 记忆缓存 和冷-open 填充全部来自该一个原语（参见
// panel/filtered_picker.hpp)，而不是这里的 hand
// -rolled。这个 头文件 仅声明了 `@` 特有的内容：哪个 快照 为其
// 提供数据以及哪个过滤器对其进行排名。
//
// 曾经住在这里的——一个 `cached_matches / cached_query / cached_valid`
// 三重奏加上一个免费的 `mention_filtered()` 访问器——是
// symbol.hpp 的 字节-level 克隆，而这对已经从调色板中分离出来，
// 根本没有得到 记忆缓存。笔芯更糟糕：它位于 状态更新函数 的
// "printable typed" 臂中，因此冷打开 `@` 并按 ↓ 或
// Backspace 将 选择面板 永久留在 "indexing…" 上。

#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include "agentty/runtime/panel/filtered_picker.hpp"
#include "agentty/runtime/visual.hpp"
#include "agentty/workspace/files.hpp"

namespace agentty {

namespace mention {

struct Closed {};

// `@` 选择面板的来源+排名，在一处。从此处（而不是在每个构建站
// 点）的免费函数构建源代码使 "which snapshot does `@` read"
// 保持为单一事实。
[[nodiscard]] inline ui::SnapshotSource<std::string> file_source() {
    return {
        .ready = [] { return files_ready(); },
        .fetch = [] { return list_workspace_files(); },
    };
}

[[nodiscard]] inline ui::FilterFn<std::string> file_filter() {
    return [](const std::vector<std::string>& files, std::string_view q) {
        return filter_files(files, q);
    };
}

struct Open {
    Open() : picker(file_source(), file_filter()) {}

    ui::FilteredPicker<std::string> picker;
};

// 选择面板拥有每个可见轴（查询+光标），因此面板的零件列表只是选择面板的。
// 在这里声明而不是在共享的 visual_parts.hpp 中声明，
// 因为 Open 现在有一个 user-provided 构造函数—
// —brace-arity 探针读取 0，所以完整性证明需要显式的
// opt-in，而 opt-in 属于它所描述的事物的旁边。
inline auto visual_parts(const Open& p) {
    return std::make_tuple(visual::ref(p.picker));
}

} // namespace mention

namespace visual {
template <> inline constexpr bool trusted_parts<mention::Open> = true;
} // namespace visual

using MentionState = std::variant<mention::Closed, mention::Open>;

[[nodiscard]] inline bool mention_palette_is_open(const MentionState& s) noexcept {
    return std::holds_alternative<mention::Open>(s);
}
[[nodiscard]] inline       mention::Open* mention_palette_opened(MentionState& s)       noexcept { return std::get_if<mention::Open>(&s); }
[[nodiscard]] inline const mention::Open* mention_palette_opened(const MentionState& s) noexcept { return std::get_if<mention::Open>(&s); }

} // namespace agentty
