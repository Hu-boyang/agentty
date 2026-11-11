#pragma once
// #符号 选择面板 — 当用户在字边界键入 `#` 时在编辑器上方打开。
// 与 `@` 选择面板 形状相同，在 工作区 符号 上而不是文件路径。
// 符号 = (name, path, line) — 我们在
// select 上附加的 标签 包含所有三个，因此 Submit
// -time 扩展可以在声明周围拼接文件的摘录。
//
// 状态是FilteredPicker (panel/filtered_picker.hpp):查询、
// 游标、快照、过滤器记忆缓存和冷-open填充均继承自该原语。
// 此 头文件 仅声明 `#` 特有的内容。
//
// 产生SymbolEntry (和过滤助手）的工作区扫描仪位于`workspace/symbols.hpp`；
// 这个头文件是UI-state-only。

#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include "agentty/runtime/panel/filtered_picker.hpp"
#include "agentty/runtime/visual.hpp"
#include "agentty/workspace/symbols.hpp"  // SymbolEntry

namespace agentty {

namespace symbol {

struct Closed {};

[[nodiscard]] inline ui::SnapshotSource<SymbolEntry> symbol_source() {
    return {
        .ready = [] { return symbols_ready(); },
        .fetch = [] { return list_workspace_symbols(); },
    };
}

[[nodiscard]] inline ui::FilterFn<SymbolEntry> symbol_filter() {
    return [](const std::vector<SymbolEntry>& entries, std::string_view q) {
        return filter_symbols(entries, q);
    };
}

struct Open {
    Open() : picker(symbol_source(), symbol_filter()) {}

    ui::FilteredPicker<SymbolEntry> picker;
};

// 参见mention.hpp：选择面板拥有每个可见轴，
// 并且Open的user-provided构造函数使其成为非-aggregate，
// 因此部件列表是明确的并且信任声明在其覆盖的类型旁边陈述。
inline auto visual_parts(const Open& p) {
    return std::make_tuple(visual::ref(p.picker));
}

} // namespace symbol

namespace visual {
template <> inline constexpr bool trusted_parts<symbol::Open> = true;
} // namespace visual

using SymbolState = std::variant<symbol::Closed, symbol::Open>;

[[nodiscard]] inline bool symbol_palette_is_open(const SymbolState& s) noexcept {
    return std::holds_alternative<symbol::Open>(s);
}
[[nodiscard]] inline       symbol::Open* symbol_palette_opened(SymbolState& s)       noexcept { return std::get_if<symbol::Open>(&s); }
[[nodiscard]] inline const symbol::Open* symbol_palette_opened(const SymbolState& s) noexcept { return std::get_if<symbol::Open>(&s); }

} // namespace agentty
