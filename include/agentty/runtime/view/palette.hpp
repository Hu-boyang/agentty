#pragma once
// agentty::ui — 语义调色板，根据实时主题解析。
//
// agentty中的每种颜色都来自这里，这里的每个标记都是对
// 用户在“设置”→“外观”中选择的主题的读取。默认主题是
// `native`，它仅规定 终端-default 和十六种
// 名为 ANSI 的颜色 - 因此开箱即用的旧约束仍然完全有效：
// 我们选择语义名称，用户的终端决定色调。

#include <maya/style/color.hpp>
#include <maya/style/style.hpp>

#include "agentty/domain/ui_theme.hpp"        // ui_prefs::theme()
#include "agentty/runtime/panel/palette.hpp"   // PaletteContext，命令
#include "agentty/runtime/model.hpp"            // Model

namespace agentty::ui {

// ── 语义调色板( 名称) ANSI 仅 -- 终端 主题胜出) ──────────────
//
// 下面两层：遗留"brand"名称（具有现有小部件配置的back-compat）
// 和由轴组织的新令牌层。规则是 "one hue = one axis"：
// 状态颜色 (green/yellow/red) 仅 表示状态 (ok/warn/error)，
// 代码 -reference 青色 仅 表示
// 文件系统路径或代码标识符。
// 品牌洋红色 仅 表示 agentty 身份（Write 配置文件、队列 标签）、
// 信息蓝色 仅 表示信息/上下文 -shift (线程、 VCS）。 Tool
// 类别压缩为色调调色板，因此长期运行的spectr-class 工具读起来就像
// 一个平静的灰色列表，其中洋红色编辑和青色重击作为有意义的操作脱颖而出。
//
// ── 为什么这些是函数,不是常数 ───────────────────────────────
//
// 它们是 `inline constexpr` ANSI 文字，
// 这使得整个外观主题 选择面板 成为一个谎言：你可以浏览 57 个方案，
// 观看名称更改，而 UI 从未移动，因为应用程序中的每种颜色都是在
// 编译 时间烘焙的。选择德古拉并没有重新粉刷任何东西。
//
// 主题由 LOOKING 判断，因此标记必须是对有效主题的读取而不是文字。
// `ui_prefs::theme()` 是指向 constexpr
// 表的指针的一个宽松的原子 加载 — 足够便宜，可以放在每个样式跨度的
// 高频路径 上，并由 view() 为每个 帧 re-seated 一次。
//
// NAMES和调用站点保持EXACTLY原样——`fg_of(muted)`
// 仍然读取`fg_of(muted)`。每个令牌都是一个微小的代理，
// 可以通过读取实时主题转换为颜色，因此大约 493 个使用站点根本不需要编辑。
// 这与流失无关：多个令牌（`info`、`success`、
// `fg`)与这些文件中的普通本地共享名称，因此机械重命名将是
// 无声的-breakage机器。
//
// 下面的轴规则（文本层/状态/代码/角色）
// 没有改变——每个标记仍然意味着一件事，
// 它只是从活动方案而不是从文字中获取其色调。
//
// `native`将每个槽映射回终端的OWN十六种颜色，
// 因此该改变之后的默认行为正是其之前的行为：用户的调色板，
// 而不是我们的调色板。

namespace detail {

// 现场主题中的命名槽。在使用时转换为颜色，
// 这使得它可以替代它所替换的常量。
//
// TWO的转换，它们之间的区别是整个不变量：
//
//   ·操作员Color——将SYMBOLIC槽收回。什么都没有解决。
//     maya在PAINT时间在Style::to_sgr
//     ()中解决它，无论主题是现场THEN。这是重要的转换：
//     Style::fg 是可选的 <Color>，
//     因此今天构建的小部件会在绘制时的活动主题下进行绘制，
//     而不是在构建时的活动主题下进行绘制。因此，可以为会话的其余部分存储元素，
//     并且仍然遵循主题切换。
//
//   ·运营商LitColor-针对实时主题NOW进行解析。需要的地方
//     调用者想要CHANNELS (混合、对比度检查）或者必须分配
//     到主题字段中，该字段根据定义保存LitColor。
//     这是 EARLY BINDING：无论您将结果放入什么，
//     都将固定到今天的调色板，并且必须在主题更改时重新构建。
//
// 早期的 绑定 使得现场主题切换看起来断断续续。
// 提交的 markdown 块是一个已存储的、
// 已经-rendered 的元素；当它的颜色在构建时解决时，
// 它会保留构建时的调色板，并且以后的切换无法到达它 - 字节
// 永远不会改变，因此不会变脏，并且每个 缓存 层都会重播相同的元素。
// 每个 帧（作曲家、加载动画、欢迎屏幕）重建的任何
// 内容都正确遵循新主题，这正是使失败看起来随机的原因。
//
// 所以：优选颜色转换。仅当您需要频道时才接触 LitColor，
// 然后仅接触较短的 -lived。
//
// 代价是，一个纯粹的 `ui::fg == c` 是不明确的——两次 user
// -defined 转换，两者都不好。将其拼写为 `ui::fg() == c`
// 以命名读取；这是调用者唯一注意到的地方，而且无论如何它读起来更好。
struct Slot {
    // 象征性的身份。这是插槽本身，而不是颜色。
    maya::ThemeSlot slot;
    // Resolve-now逃逸舱口，保留为函数指针，因此令牌保持
    // 结构-NTTP-friendly文字类型。
    maya::LitColor (*read)() noexcept;

    [[nodiscard]] operator maya::LitColor() const noexcept { return read(); }  // NOLINT(google-explicit-constructor)
    // 故意的 LATE-BOUND — 见上文。不要将 "simplify" 转为 read()。
    [[nodiscard]] operator maya::Color() const noexcept {  // NOLINT(google-explicit-constructor)
        return maya::Color::slot(slot);
    }
    [[nodiscard]] maya::LitColor operator()() const noexcept { return read(); }
    // 符号形式，明确拼写为 调用 站点，
    // 这些站点希望明确他们正在请求哪个 绑定。
    [[nodiscard]] maya::Color symbolic() const noexcept {
        return maya::Color::slot(slot);
    }
};

[[nodiscard]] inline const maya::Theme& thm() noexcept { return ui_prefs::theme(); }

// 字段→ThemeSlot，从maya的OWN表生成。
//
// AGENTTY_THEME_SLOT(x)用主题FIELD名称(
// `thm().x`)拼写，但是象征性颜色需要匹配ThemeSlot
// ENUMERATOR。从 MAYA_THEME_SLOTS 为
// 每个字段派生一个常量意味着令牌的两半（它现在解析的内容以及它
// 象征性命名的内容）保证是同一个槽。命名 maya 没有的字段
// 是 编译 错误，而不是默默地在 下游 某处绘制错误的颜色。
#define X(f, E) inline constexpr maya::ThemeSlot slot_of_field_##f = maya::ThemeSlot::E;
MAYA_THEME_SLOTS(X)
#undef X

}  // namespace detail

// 不变量，固定在可以读取的位置。
//
// 令牌的颜色转换是小部件配置默认的和
// Style::with_fg() 选择的，因此它是最终在
// 存储的元素（提交的降价块、冻结的 回滚缓冲区 账本）内的颜色转换。
// 如果它得到解决，今天构建的每个小部件都会冻结今天的调色板，
// 并且实时主题切换将停止到达已确定的内容 - 这正是此形状要
// 防止的错误。 LitColor 转换故意保留在 -bound 早期，
// 仅用于需要通道的混合/Theme-field 分配。
static_assert(
    std::is_same_v<decltype(static_cast<maya::Color>(std::declval<detail::Slot>())),
                   maya::Color>,
    "ui::Slot must convert to a SYMBOLIC maya::Color");
static_assert(maya::binding::no_early_binding_v<maya::Color>,
              "and that Color must itself be late-bound — see "
              "maya/style/binding.hpp");

}  // namespace agentty::ui

// ui::Slot是主题TOKEN，而不是字面意思：每次读取
// 都会返回到实时主题，因此它跟踪主题切换并且永远不能固定颜色。
// maya::Themed拒绝任何形状像文字的东西（即它存在的目的
// 是防止调色板错误），并且Slot通过LitColor进行转换，
// 所以它必须说明它是什么。该专业化命名为 TYPE，因此任何文字
// 都无法潜入其中。
template <>
struct maya::is_theme_token<::agentty::ui::detail::Slot> : std::true_type {};

namespace agentty::ui {

// 从主题FIELD名称定义令牌。匹配的 ThemeSlot 枚举器是
// 从 maya 自己的字段↔枚举器表 (MAYA_THEME_SLOTS)
// 中查找的，因此符号标识和解析-now 路径永远不能命名不同的
// 槽 - maya 中有一个列表，并且两半都读取它。
#define AGENTTY_THEME_SLOT(field) \
    ::agentty::ui::detail::Slot{ ::agentty::ui::detail::slot_of_field_##field, \
        []() noexcept { \
            return ::agentty::ui::detail::thm().field; } }

// 用户实际上是READING的散文映射到主题的`text`槽，
// 其本机解析为终端的默认前景——与他们实际拥有的任何背景
// 的最大对比度，而不是在光终端上不可见的字面bright_white。
// Chrome和元数据仍然使用`muted` /
// `with_dim()`来后退。
inline constexpr auto fg        = AGENTTY_THEME_SLOT(text);
inline constexpr auto muted     = AGENTTY_THEME_SLOT(muted);
inline constexpr auto accent    = AGENTTY_THEME_SLOT(accent);   // 品牌/Write简介
inline constexpr auto info      = AGENTTY_THEME_SLOT(info);     // Ask简介/线程
inline constexpr auto success   = AGENTTY_THEME_SLOT(success);  // 已接受/运行OK
inline constexpr auto warn      = AGENTTY_THEME_SLOT(warning);  // 待处理/琥珀色
inline constexpr auto danger    = AGENTTY_THEME_SLOT(error);    // 错误/被拒绝
inline constexpr auto highlight = AGENTTY_THEME_SLOT(primary);  // 命令调色板/提及

// ── 托肯层( 轴 -disciplined)。 优先使用新代码。 ────────────

// 文本层次结构——从初级散文到chrome的三层。
inline constexpr auto text_primary   = AGENTTY_THEME_SLOT(text);       // 散文、头条新闻
inline constexpr auto text_secondary = AGENTTY_THEME_SLOT(secondary);  // mid-tone元数据
inline constexpr auto text_tertiary  = AGENTTY_THEME_SLOT(muted);      // 页脚、提示、空白
// ON填充徽章的文本墨水——主题自己对
// "what reads against my accent colours"的答案，
// 而不是当方案的徽章色调变暗时就消失的字面黑色。
//
// 仅 VALID WHEN 该 THEME OWNS A CANVAS。首先是 Ask theme_owns_canvas()。
// 在 theme::native 下，inverse_text 是
// 默认值——相同 颜色与普通文本一样——因为本机不绘制自己的背景，
// 因此没有第二个画布可以反转。将其放在填充的 标签 上，
// 您会看到 终端 在亮带上的正常前景：light-on-light，
// 被洗掉，这就是 Ghostty 上 Opus 徽章的样子。
inline constexpr auto text_inverse   = AGENTTY_THEME_SLOT(inverse_text);

// 实时主题是否绘制自己的背景？
//
// 每个方案都是如此。 theme::native 故意不执行 —
// 它为 background/surface 指定默认值，以便用户自己的
// 终端 颜色到达屏幕，这就是它的全部意义。
//
// 这个单一的事实决定FILLED元素是否是一个好主意。
// 乐队或 标签 需要一块画布和与其形成对比的墨水；如果没有画布，
// 就没有有意义的 "inverse"，因此诚实的 回退
// 在用户自己的背景上被着色为 TEXT — 正是状态行和
// 差异在普通 终端 上始终读取的方式。
[[nodiscard]] inline bool theme_owns_canvas() noexcept {
    return detail::thm().background.kind() != maya::ColorKind::Default;
}

// 标签：主题自己的文本颜色，色调为`hue`。
//
// 两种情况，因为只有其中一种是可测量的。
//
// RGB色调(每个命名方案)。保留主题的 `text`
// 作为墨水并移动 HUE 直到在其上读取文本。对于标签来说，
// 这是正确的方式：根据 标签 变化的墨水是眼睛需要
// 分辨的第二件事，而保持散文颜色的墨水使 标签 读取
// 为带有色调的文本，这就是徽章。
//
// PALETTE色调(theme::native)。我们无法测量
// 它 - 终端 拥有这 16 个条目 - 并且我们不能投影它，
// 因为投影会丢弃用户实际选择的颜色。他们的 `text` 也没有帮助：
// 在本机下它是默认的，终端的ORDINARY前景，
// 因此将其与调色板带配对是light-on-light，
// 这又是agentty#45。
//
// 因此，将问题交给能够回答它的一方。
// SGR 7 告诉 终端 交换自己的前景和背景：
// 带子变成用户的调色板颜色，墨水变成用户的背景，
// 他们一起选择的一对，终端 确切地知道。
[[nodiscard]] inline maya::Style chip_style(maya::Color hue) noexcept {
    const maya::Theme& th = detail::thm();
    const maya::LitColor lit = th.resolve(hue);

    if (lit.kind() != maya::ColorKind::Rgb)
        return maya::Style{}.with_fg(lit).with_inverse();

    const maya::LitColor ink    = th.resolve(maya::Color::slot(maya::ThemeSlot::Text));
    const maya::LitColor canvas = th.resolve(maya::Color::slot(maya::ThemeSlot::Background));
    return maya::Style{}
        .with_bg(maya::band_for(lit, ink, canvas))
        .with_fg(maya::Color::slot(maya::ThemeSlot::Text));
}

// 状态——严重性/结果ONLY。从来都不是一个类别的颜色。
inline constexpr auto status_ok    = AGENTTY_THEME_SLOT(success);
inline constexpr auto status_info  = AGENTTY_THEME_SLOT(info);
inline constexpr auto status_warn  = AGENTTY_THEME_SLOT(warning);
inline constexpr auto status_error = AGENTTY_THEME_SLOT(error);

// 代码参考——文件路径、标识符、命令参数。
inline constexpr auto code_path = AGENTTY_THEME_SLOT(link);  // 文件路径、标识符
inline constexpr auto code_text = AGENTTY_THEME_SLOT(info);  // 代码-block 正文内容

// 角色/身份——持久化标识符颜色。每个角色一种色调。
inline constexpr auto role_brand     = AGENTTY_THEME_SLOT(accent);   // 品牌，Write简介
inline constexpr auto role_brand_alt = AGENTTY_THEME_SLOT(primary);  // 队列标签，替代品牌
inline constexpr auto role_info      = AGENTTY_THEME_SLOT(info);     // Ask简介，线程，vcs

// ── 样式预设 — 除非被覆盖 终端 默认 fg ─────────────────
inline maya::Style dim()    { return maya::Style{}.with_dim(); }
inline maya::Style bold()   { return maya::Style{}.with_bold(); }
inline maya::Style italic() { return maya::Style{}.with_italic(); }

inline maya::Style fg_of(maya::Color c)         { return maya::Style{}.with_fg(c); }
inline maya::Style fg_bold(maya::Color c)       { return maya::Style{}.with_fg(c).with_bold(); }

// `fg_dim`返回"dim color"——但是已经-muted颜色
// (bright_black/灰色)上的`with_dim()`
// 在深色/低-contrast主题(true-black背景、
// OLED调色板、一些Solarized 变体)上折叠到可读底板以下。
// `fg_dim(muted)` 的本意是 "subdued secondary text,"，
// 而柔和的墨水 ALONE 已经在每个合理的主题上扮演了这个
// 角色 - 将 SGR `dim` 属性堆叠在上面只会牺牲可读性。
// 因此，当颜色已经 IS 柔和的墨水时，抑制 `with_dim()`；
// 保留它用于其他一切，其中调暗明亮的颜色正是我们想要的
// 有意义的信号（品牌颜色的柔和形式等）。
//
// maya::theme::is_muted是该问题的共同定义。
// 这用于手动测试 `Named && index == BrightBlack`，
// 这与检查 6 个 maya 小部件各自私下增长的情况相同 - 对于
// 任何静音槽不是字面意义上的 bright_black（i.e。
// 所有 57 个内置 -in 方案）的主题来说，这悄然不再适用。
inline maya::Style fg_dim(maya::Color c) {
    return maya::theme::is_muted(c)
        ? maya::Style{}.with_fg(c)
        : maya::Style{}.with_fg(c).with_dim();
}
inline maya::Style fg_italic(maya::Color c)     { return maya::Style{}.with_fg(c).with_italic(); }

// ── 调色板行可见度 ─────────────────────────────────────
//
// 行是 GATED —Review/Accept-all 需要 待处理 更改、
// 运行 -code-block 隔离回复、更新发布 — 以及 三个
// 位置必须就哪些可见位置达成一致：渲染 列表的 VIEW、将游标解析为命令
// 的 REDUCER、以及在 Esc 之后恢复游标的 back_to()。
// 当他们不同意时，调色板显示一个列表，而光标索引另一个列表，
// 因此 Enter 会触发邻居，Esc 会恢复错误的行。
//
// 它被写了两次——在palette.cpp和nav_pickers.cpp
// ——每个人都将"the SAME predicate"评论为对方。
// 一个定义使得这一评论是真实的而不是抱负的。
//
// 住在这里而不是在命令表旁边，因为它需要Model，
// 而command_palette.hpp故意是Model-free。
[[nodiscard]] PaletteContext palette_context(const Model& m);

} // namespace agentty::ui
