#pragma once
// agentty::ui::panel::Appearance-外观-and-feel 面板。
//
// 类似于检索和智能模式的表格面板：面板保存form::Form而
// 没有其他，因此导航、下拉菜单和编辑是共享的状态更新函数的工作
// 并且maya::Form拥有每个字形。这里存在的是真正的 面板
// -specific 事物——存在哪些行 ID，因此 状态更新函数
// 可以按名称而不是按位置读取值。
//
// ── 为什么主题行是选择,其余是选择 ─────────────────
//
// form.hpp精确地划清了界限：Choice是您可以在注释中命名的
// SMALL CLOSED 枚举，Pick是需要搜索的大的或动态的集合。
// 颜色、背景、密度、运动是三到五个值的枚举，因此它们以←/→循环
// 并在Enter上打开一个简短的下拉菜单。主题有 57 个，
// 并且每当生成器为 re-run 时就会增长，因此它们是一个将交给
// 真正可搜索的 选择面板 的选择 - 智能模式遵循模型的相同规则。

#include <string>
#include <vector>
#include <string_view>

#include "agentty/domain/ui_prefs.hpp"
#include "agentty/domain/ui_theme.hpp"
#include "agentty/runtime/panel/form.hpp"

#include "agentty/runtime/panel/filtered_picker.hpp"

namespace agentty::ui::panel {

// 行ID。在构建站点和读取站点上都使用命名常量而不是
// 文字：两个匹配字符串之一中的拼写错误会导致静默死亡设置，
// 并且 编译器 无法看到它。
inline constexpr std::string_view kApTheme      = "theme";
// UI语言。选择而不是选择：二十个-one条目（二十种
// 语言加上自动）超出了form.hpp所说的属于
// 下拉列表的点，并且使方案成为选择面板的推理相同。
inline constexpr std::string_view kApLang       = "lang";
inline constexpr std::string_view kApTier       = "tier";
inline constexpr std::string_view kApPolarity   = "polarity";
inline constexpr std::string_view kApDensity    = "density";
inline constexpr std::string_view kApProseWidth = "prose_width";
inline constexpr std::string_view kApCompact    = "compact_turns";
inline constexpr std::string_view kApThreadPeek = "thread_preview";
inline constexpr std::string_view kApForeign    = "foreign_sessions";
inline constexpr std::string_view kApMotion     = "motion";
inline constexpr std::string_view kApScrollback = "scrollback";
inline constexpr std::string_view kApSyntax     = "syntax";
inline constexpr std::string_view kApToolOutput = "tool_output";
inline constexpr std::string_view kApThinking   = "thinking";
inline constexpr std::string_view kApTimestamps = "timestamps";

// ── 提供主题选择面板的 ─────────────────────────────────────────
//
// 方案列表是进程-lifetime常量（maya的内置-in表），
// 因此"snapshot"总是准备好并且从不重新填充。它仍然会经历
// SnapshotSource，因为这是 接口边界 FilteredPicker
// 读取的内容，而具有统一形状的 选择面板 是该文件的重点。
[[nodiscard]] ui::SnapshotSource<std::string> theme_source();

// 方案名称上的模糊子序列匹配，最好首先——应用相同的排名
// `matching_themes`，现在表示为选择面板的过滤器，
// 因此列表和光标不能对"row 4"的含义有分歧。
[[nodiscard]] ui::FilterFn<std::string> theme_filter();

// 面板的状态。命名为 `AppearancePane` 而不是 `Appearance`，
// 因为 slot.hpp 中的面板 SLOT 类型是 运行时 的其余部分在表示
// "the Appearance overlay" 时所说的内容——继承了这一点并
// 添加了parent-snapshot mixin，并且在一个名称空间中具有一个名称的两
// 种类型是一种重新定义，而不是一种方便。
struct AppearancePane {
    form::Form form;

    // 主题选择面板，当打开时超过此面板。选择行交给真正的 覆盖面板，
    // 而不是将下拉菜单增长为更糟糕的 选择面板。
    //
    // 这是FilteredPicker，而不是hand-rolled{查询、
    // 索引、滚动}包，这就是重点。捆绑包是破坏的原因：状态更新函数
    // 领先于 `index`，而 `scroll` 则坐在它旁边未读，
    // 因此亮点从 615 方案的底部消失，而 Down 似乎什么也没做。
    // 选择面板 拥有查询，游标 和 和派生的 可视区域 一起（参见
    // visible()/visible_entries()），
    // 因此没有第二个字段可以留下。
    struct ThemePicker {
        ThemePicker() : picker(theme_source(), theme_filter()) {}

        ui::FilteredPicker<std::string> picker;

        // 浏览器打开时使用的主题。将突出显示的 APPLIES
        // 移动到一个方案（该列表是它自己的预览），
        // 因此 Esc 需要放回某个地方 - 如果没有这个，
        // 取消会让你穿着你刚刚看到的最后一件东西。从未绘制过，
        // 因此它不受 帧 哈希的影响。
        std::string restore;
    };
    bool        picking = false;
    ThemePicker picker;
};

// 选择面板拥有每个可见轴(查询+游标)； `restore` 是
// Esc 撤消存储，永远不会到达屏幕。
inline auto visual_parts(const AppearancePane::ThemePicker& p) {
    return std::make_tuple(visual::ref(p.picker), visual::exempt);
}

}  // namespace agentty::ui::panel

namespace agentty::visual {
// 经审查的权利要求。 ThemePicker 有一个 user-provided
// 构造函数（它使用其源 + 过滤器为 FilteredPicker 播种），
// 因此大括号-arity 探针读取 0，并且结构证明本身无法检查这一点。
// 上面说明了两个成员：`picker` 是可见状态，`restore`
// 是从未绘制的 Esc 撤消存储。
template <>
inline constexpr bool
    trusted_parts<ui::panel::AppearancePane::ThemePicker> = true;
}  // namespace agentty::visual

namespace agentty::ui::panel {
static_assert(visual::parts_cover_all<AppearancePane::ThemePicker>);

// 根据首选项和 终端 报告的内容构建 面板 的行。纯的。
[[nodiscard]] form::Form build_appearance_form(const ui_prefs::Prefs& p,
                                               const ui_prefs::Detected& d);

// 查询匹配的方案，最相关的首先。由 选择面板 的视图及其
// 状态更新函数 共享，因此列出的内容和 Enter 选择的
// 内容不能有分歧 - 表单选项遵循相同的规则。
//
// 返回对进程-lifetime 记忆缓存的引用：单个
// 箭头键询问该问题三次（环绕索引、命名登陆行、绘制），
// 并且按值返回使得每个重建615个字符串。
[[nodiscard]] const std::vector<std::string>& matching_themes(std::string_view query);

}  // namespace agentty::ui::panel
