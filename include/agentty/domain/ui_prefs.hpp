#pragma once
// agentty::ui_prefs-以一种值类型改变agentty LOOKS方式
// 的每个旋钮。
//
// ── 为何这是它自己关心的 ──────────────────────────────────────────
//
// 外观是属于PERSON的配置的一部分，而不是项目。
// 权限 配置文件是您所在的 仓库 的属性；
// 终端 灯是您的眼睛和办公室的财产。因此，这些内容存在
// 于用户存储中，并在结账期间跟随您，并且这里没有任何
// 内容写入项目 .agentty/.
//
// ── 为什么每个领域都有明确的"auto" ───────────────────────────────
//
// 其中两个——颜色层和背景极性——是 agentty 通常可以
// DETECT 的东西。检测几乎总是正确的，但错误却无法恢复：
// 一个关于 COLORTERM 的终端，或者一个从未设置过
// COLORFGBG 的终端，会让用户盯着一个不可读的屏幕，
// 无法说出 "no, it's light"。自动是默认值，覆盖是逃生舱口，
// 这是 agentty 问题 #37 的全部内容。

#include <cstdint>
#include <string>
#include <string_view>

namespace agentty::ui_prefs {

// ── 主题 ────────────────────────────────────────────────────────────────

// 哪个调色板绘制了UI。
//
// 空意味着NATIVE:agentty表示没有其自己的
// 颜色并且用户自己的终端调色板出现(maya::theme::native)。
// 这是默认设置，因为它是在浅色 终端、
// 深色和单色上唯一正确的选择。
//
// 任何其他值命名maya的内置-in方案之一("Dracula"、
// "Gruvbox Dark"、…)。选择一个是用户说 "paint it like this"，
// 然后 agentty 就拥有包括背景在内的整个表面。
using ThemeName = std::string;

// agentty可能发出什么。
//
// 自动运行maya的检测堆栈(NO_COLOR、TERM、COLORTERM、
// tty)。其余的则强制一个等级，对于错误报告的 终端，或者对于那些只是想要
// 黑色 -and-white 屏幕的人，Mono 为他们提供了这一点。
enum class ColorTier : std::uint8_t { Auto, TrueColor, Ansi256, Ansi16, Mono };

// 终端是浅色还是深色。
//
// 仅当命名主题同时具有变体时才进行咨询； `native`不在乎，
// 这就是重点。自动读取 COLORFGBG，如果失败，
// 则将其保留为未知而不是猜测。
enum class Polarity : std::uint8_t { Auto, Dark, Light };

// ── 密度 ──────────────────────────────────────────────────────────────

// 面板的可滚动正文可以有多高。
//
// 共享的默认值是14行，如此选择使得选择面板适合短的终端。
// 在一个高大的、不必要的狭窄的地方——几个统计选项卡滚动
// 缺少六行——所以这提高了上限。它是 MAXIMUM：
// 面板仍然固定在 终端 的实际位置上。
enum class Density : std::uint8_t { Compact, Normal, Roomy };

[[nodiscard]] constexpr int viewport_rows(Density d) noexcept {
    switch (d) {
        case Density::Compact: return 10;
        case Density::Normal:  return 14;
        case Density::Roomy:   return 22;
    }
    return 14;
}

// ── 动议 ───────────────────────────────────────────────────────────────

// 是否有物体移动。
//
// 在成为偏好之前，关闭是可访问性设置：前庭障碍
// 使打字机逐步显示确实令人不愉快，而慢速SSH上
// 的加载动画链接只是传输上的噪音。关闭会使流式
// 文本整行显示，并用静态标记替换每个 加载动画。
enum class Motion : std::uint8_t { Full, Reduced, Off };

// ── Scrollback ───────────────────────────────────────────────────────────

// 当 线程 打开或窗口大小调整时，有多少对话被重新绘制到 终端
// 的 回滚缓冲区 中。 重绘 是最大的 内存 和长 线程 的时间成本，
// 并且超过 终端 自己的 回滚缓冲区 限制的行无论如何都会被丢弃。
// 自动匹配 终端 的 回滚缓冲区 长度 (
// maya/terminal/scrollback.hpp)。完全重绘一切。
enum class Scrollback : std::uint8_t { Tiny, Small, Big, Full, Auto };

// 行为固定级别； 0 = 无限制。 Auto 由调用者解决。
[[nodiscard]] constexpr int scrollback_rows(Scrollback s) noexcept {
    switch (s) {
        case Scrollback::Tiny:  return 1'000;
        case Scrollback::Small: return 5'000;
        case Scrollback::Big:   return 20'000;
        case Scrollback::Full:  return 0;
        case Scrollback::Auto:  return 5'000;   // 直到已知终端
    }
    return 5'000;
}

// ── 颜色 ───────────────────────────────────────────────────────────────

// 在您要求更多之前，会显示工具的输出量。
enum class ToolOutput : std::uint8_t { Collapsed, Preview, Full };

// 模型的推理是否被展示。
enum class Thinking : std::uint8_t { Shown, Collapsed, Hidden };

// 回合是否携带时间以及以什么形式携带时间。
enum class Timestamps : std::uint8_t { Off, Relative, Absolute };

// ── 整个 ──────────────────────────────────────────────────────

struct Prefs {
    ThemeName  theme;                              // empty = native
    ColorTier  tier        = ColorTier::Auto;
    Polarity   polarity    = Polarity::Auto;

    // UI语言，作为BCP-47标签("de"，"pt-BR")。 空 的意思是 "follow the environment"
    // -- $LC_ALL / $LC_MESSAGES / $LANG，然后是英文。
    //
    // Empty-as-auto而不是默认为"en"，
    // 出于同样的原因`theme`是空-as-native：
    // 从未打开选择面板的用户应该获得他们的系统语言，
    // 并且存储的"en"将与明确选择的英语无法区分。
    // 故意选择英语的人会写出 "en"，然后后面的
    // `LANG=de_DE` 不会覆盖它们。
    //
    // 这仅是agentty的CHROME。模型回答的语言取决于
    // 用户输入的内容，并且故意不与之耦合 - 请参阅
    // docs/design/i18n.md.
    std::string lang;

    Density    density     = Density::Normal;
    // 助理散文的阅读测量，分栏。长队很难追溯到下一行的起点；
    // 0 表示 "no cap"。
    int        prose_width = 0;
    bool       compact_turns = false;
    // ^J中突出显示的线程的加框预览。 Off 给出
    // 整个面板的列表。
    bool       thread_preview = true;
    // 列出 ^J 中的 Codex/Claude 代码会话，以便可以在此处继续它们。
    bool       foreign_sessions = true;

    Motion     motion      = Motion::Full;
    Scrollback scrollback  = Scrollback::Auto;

    bool       syntax      = true;
    ToolOutput tool_output = ToolOutput::Preview;
    Thinking   thinking    = Thinking::Collapsed;
    Timestamps timestamps  = Timestamps::Off;

    bool operator==(const Prefs&) const = default;
};

// ── 标签 ───────────────────────────────────────────────────────────────
//
// 每个值一个拼写，由设置行AND使用任何其他报告状态的内容，
// 因此两者永远不会漂移。

[[nodiscard]] constexpr std::string_view label(ColorTier t) noexcept {
    switch (t) {
        case ColorTier::Auto:      return "auto";
        case ColorTier::TrueColor: return "truecolor";
        case ColorTier::Ansi256:   return "256 colors";
        case ColorTier::Ansi16:    return "16 colors";
        case ColorTier::Mono:      return "monochrome";
    }
    return "?";
}

[[nodiscard]] constexpr std::string_view label(Polarity p) noexcept {
    switch (p) {
        case Polarity::Auto:  return "auto";
        case Polarity::Dark:  return "dark";
        case Polarity::Light: return "light";
    }
    return "?";
}

[[nodiscard]] constexpr std::string_view label(Density d) noexcept {
    switch (d) {
        case Density::Compact: return "compact";
        case Density::Normal:  return "normal";
        case Density::Roomy:   return "roomy";
    }
    return "?";
}

[[nodiscard]] constexpr std::string_view label(Motion m) noexcept {
    switch (m) {
        case Motion::Full:    return "full";
        case Motion::Reduced: return "reduced";
        case Motion::Off:     return "off";
    }
    return "?";
}

[[nodiscard]] constexpr std::string_view label(Scrollback s) noexcept {
    switch (s) {
        case Scrollback::Tiny:  return "tiny";
        case Scrollback::Small: return "small";
        case Scrollback::Big:   return "big";
        case Scrollback::Full:  return "full";
        case Scrollback::Auto:  return "auto";
    }
    return "?";
}

[[nodiscard]] constexpr std::string_view label(ToolOutput t) noexcept {
    switch (t) {
        case ToolOutput::Collapsed: return "collapsed";
        case ToolOutput::Preview:   return "preview";
        case ToolOutput::Full:      return "full";
    }
    return "?";
}

[[nodiscard]] constexpr std::string_view label(Thinking t) noexcept {
    switch (t) {
        case Thinking::Shown:     return "shown";
        case Thinking::Collapsed: return "collapsed";
        case Thinking::Hidden:    return "hidden";
    }
    return "?";
}

[[nodiscard]] constexpr std::string_view label(Timestamps t) noexcept {
    switch (t) {
        case Timestamps::Off:      return "off";
        case Timestamps::Relative: return "relative";
        case Timestamps::Absolute: return "absolute";
    }
    return "?";
}

// ── 自行车 ──────────────────────────────────────────────────────────────
//
// Enter连续前进。每个 枚举 都会换行，因此一行是一个循环，
// 用户可以随时返回 — 永远不会在最后一个值处出现死胡同。

template <typename E, std::uint8_t N>
[[nodiscard]] constexpr E cycle(E v) noexcept {
    return static_cast<E>((static_cast<std::uint8_t>(v) + 1) % N);
}

[[nodiscard]] constexpr ColorTier  next(ColorTier v)  noexcept { return cycle<ColorTier, 5>(v); }
[[nodiscard]] constexpr Polarity   next(Polarity v)   noexcept { return cycle<Polarity, 3>(v); }
[[nodiscard]] constexpr Density    next(Density v)    noexcept { return cycle<Density, 3>(v); }
[[nodiscard]] constexpr Motion     next(Motion v)     noexcept { return cycle<Motion, 3>(v); }
[[nodiscard]] constexpr Scrollback next(Scrollback v) noexcept { return cycle<Scrollback, 5>(v); }
[[nodiscard]] constexpr ToolOutput next(ToolOutput v) noexcept { return cycle<ToolOutput, 3>(v); }
[[nodiscard]] constexpr Thinking   next(Thinking v)   noexcept { return cycle<Thinking, 3>(v); }
[[nodiscard]] constexpr Timestamps next(Timestamps v) noexcept { return cycle<Timestamps, 3>(v); }

// ── 实地表格 ──────────────────────────────────────────────────────
//
// EVERY首选项字段，声明为ONCE。 settings.json 编写器、
// 其加载器和 保存-time 合并都遵循此原则；他们都没有命名一个字段。
//
// 原因 此 EXISTS
// ===============
// 这三个人过去常常用手拼出这个列表，但没有什么能让他们达成一致。
// 每次遗漏都会以不同的方式失败：写入器缺失意味着设置从未到达磁盘；
// 加载程序中丢失意味着它已被写入，然后在重新启动时被忽略；
// 合并中缺失意味着第二个 agentty 会话会在下一个
// 保存 上破坏它。最后一个就是 "settings don't persist"
// 在实践中的样子。
//
// 其他设置块（智能、碎布、web_search、
// 粘贴）通过settings_registry.hpp已经具有这种形状一段时间了。
// `ui` 早于它。它不会移动 ONTO 到 注册表，因为 注册表 将
// 枚举 存储为 STRING（"convex"、"auto"），
// 而 ui 存储写为整数的 带类型的 枚举 —— 迁移将更改 on-disk 格式，
// 让每个现有用户购买下表未提供的任何内容。
//
// 这里的字段是`member pointer + json key`。
// 默认值来自 `Prefs{}` 本身，通过成员指针，
// 因此没有第二个副本可以漂移：该表不能与关于默认值的结构不一致，
// 因为它从未声明过默认值。

// 访问每个偏好字段，将(成员-pointer，JSON-key)传递给`f`。
//
// 通用 lambda 将每个字段视为其 自身 类型，因此访问者可以使用
// `if constexpr` 来打开它——int 保持 int，枚举 保持其 枚举，
// 并且通过 变体 或字符串没有任何 round-trips。
//
// ADDING A FIELD：添加到Prefs，这里添加一行。编写器、
// 加载器和合并器无需编辑即可拾取它。忘记该行会被
// tests/lint/prefs_sync.py 捕获，它会将此表与结构进行比较。
template <typename F>
constexpr void for_each_field(F&& f) {
    f(&Prefs::theme,            std::string_view{"theme"});
    f(&Prefs::lang,             std::string_view{"lang"});
    f(&Prefs::tier,             std::string_view{"tier"});
    f(&Prefs::polarity,         std::string_view{"polarity"});
    f(&Prefs::density,          std::string_view{"density"});
    f(&Prefs::prose_width,      std::string_view{"prose_width"});
    f(&Prefs::compact_turns,    std::string_view{"compact_turns"});
    f(&Prefs::thread_preview,   std::string_view{"thread_preview"});
    f(&Prefs::foreign_sessions, std::string_view{"foreign_sessions"});
    f(&Prefs::motion,           std::string_view{"motion"});
    f(&Prefs::scrollback,       std::string_view{"scrollback"});
    f(&Prefs::syntax,           std::string_view{"syntax"});
    f(&Prefs::tool_output,      std::string_view{"tool_output"});
    f(&Prefs::thinking,         std::string_view{"thinking"});
    f(&Prefs::timestamps,       std::string_view{"timestamps"});
}

}  // namespace agentty::ui_prefs
