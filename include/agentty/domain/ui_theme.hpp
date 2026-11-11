#pragma once
// agentty::ui_prefs — 将偏好解析为 渲染器 使用的内容。
//
// Prefs 说出用户 WANTS 的内容。这说明了他们
// GET，无论偏好是 "auto" 或要求 终端 无法做到的事情，
// 这都是不同的。将两者分开很重要：设置面板并排显示愿望
// 和解析值（"auto — truecolor"），
// 这是用户可以区分工作默认值和损坏检测的唯一方法。

#include <algorithm>
#include <atomic>
#include <iterator>
#include <string_view>

#include <maya/device.hpp>       // app_set_theme—渲染器-side水槽
#include <maya/style/schemes.hpp>
#include <maya/style/theme.hpp>

#include "agentty/domain/ui_prefs.hpp"

namespace agentty::ui_prefs {

// 在检测和钳位之后，终端将实际得到什么。
struct Resolved {
    const maya::Theme* theme = &maya::theme::native;
    maya::theme::ColorTier tier = maya::theme::ColorTier::Ansi16;
    maya::theme::Polarity  polarity = maya::theme::Polarity::Unknown;
    // 当该值来自检测而非用户时为真。专家组诚实地需要
    // 渲染 "auto — truecolor"。
    bool tier_detected = true;
    bool polarity_detected = true;
};

// 通过名称查找built-in方案。当名称未知时为
// Null，这不是错误：命名此构建未携带的
// 方案的配置应回退到本机而不是拒绝启动。
//
// BINARY搜索，不是线性的。 resolve() 调用 这个和
// view() 调用 resolve() 每个 帧，所以扫描在整个表上每个
// 帧 运行一次 - 在 57 个方案中很好，1.25 us/frame 在
// 615 中，对于在 帧 之间不能改变的答案。 schemas[]
// 是按排序发出的，而且确实是 static_asserts，因此通过构造
// 而不是按照惯例，搜索是正确的。
[[nodiscard]] inline const maya::Theme* find_scheme(std::string_view name) {
    if (name.empty()) return nullptr;
    const auto* first = std::begin(maya::theme::schemes);
    const auto* last  = std::end(maya::theme::schemes);
    const auto* it = std::lower_bound(
        first, last, name,
        [](const maya::theme::NamedTheme& s, std::string_view n) {
            return std::string_view{s.name} < n;
        });
    if (it != last && name == std::string_view{it->name}) return it->theme;
    return nullptr;
}

[[nodiscard]] inline maya::theme::ColorTier to_maya(ColorTier t) {
    switch (t) {
        case ColorTier::TrueColor: return maya::theme::ColorTier::TrueColor;
        case ColorTier::Ansi256:   return maya::theme::ColorTier::Ansi256;
        case ColorTier::Ansi16:    return maya::theme::ColorTier::Ansi16;
        case ColorTier::Mono:      return maya::theme::ColorTier::Mono;
        case ColorTier::Auto:      break;
    }
    return maya::theme::ColorTier::Ansi16;
}

// 终端报告关于其自身的内容。从环境中读取，该环境固定在 exec 处，
// 因此在启动时检测一次并保留它（Model::env）。
struct Detected {
    maya::theme::ColorTier tier     = maya::theme::ColorTier::Ansi16;
    maya::theme::Polarity  polarity = maya::theme::Polarity::Unknown;
};

// 读取 TERM/COLORTERM/COLORFGBG/NO_COLOR。 `tty` 是标准
// 输出是否是 终端。
[[nodiscard]] inline Detected detect(bool tty) {
    return {maya::theme::detect_tier(tty), maya::theme::detect_polarity()};
}

// 解决。纯：终端 的一侧为 `d`。
[[nodiscard]] inline Resolved resolve(const Prefs& p, const Detected& d) {
    Resolved r;

    // 层：检测，除非被覆盖。
    if (p.tier == ColorTier::Auto) {
        r.tier = d.tier;
        r.tier_detected = true;
    } else {
        r.tier = to_maya(p.tier);
        r.tier_detected = false;
    }

    // 极性：COLORFGBG，除非被覆盖。当什么也没说
    // 时保持未知 - 这里猜测是整个功能要修复的错误。
    if (p.polarity == Polarity::Auto) {
        r.polarity = d.polarity;
        r.polarity_detected = true;
    } else {
        r.polarity = (p.polarity == Polarity::Light)
                         ? maya::theme::Polarity::Light
                         : maya::theme::Polarity::Dark;
        r.polarity_detected = false;
    }

    // 主题。命名方案规定文本 RGB 包括背景，
    // 因此只有在 终端 可以显示它的地方才有意义：低于
    // 256 种颜色，方案的调色板将被量化为泥，
    // 而本机（只是用户自己的 16 种颜色）绝对更好。
    // 因此，低层会回落，而不是 渲染（德古拉的糟糕近似）。
    const maya::Theme* named = find_scheme(p.theme);
    const bool can_paint = r.tier == maya::theme::ColorTier::TrueColor
                        || r.tier == maya::theme::ColorTier::Ansi256;
    r.theme = (named && can_paint) ? named : &maya::theme::native;
    return r;
}

// 这个主题有自己的背景光吗？
//
// 方案声明文字RGB，因此其极性是关于它的FACT
// 而不是偏好——这就是当用户选择的方案与他们所处的终端
// 不一致时让resolve()注意到的原因。
//
// ── 只有当背景有频道时 ────────────────────
//
// std::nullopt表示"this theme has no opinion":
// theme::native将其背景声明为默认(SGR 49)，
// 并且Named/Indexed背景是调色板槽，其RGB只有终端知道。
// 通过 to_rgb() 进行投影会发明一个答案 - 它返回白色作为默认值，
// 因此本机通常将自己报告为亮度为 1.0 的 LIGHT 方案，
// 并且每个调用者将其与检测到的极性进行比较都会得到一个确信的错误答案。
//
// 不拥有画布的主题不能与终端的主题冲突，
// 因此"no opinion"是真实结果并且调用者跳过比较。
[[nodiscard]] inline std::optional<bool>
scheme_is_light(const maya::Theme& t) noexcept {
    const maya::LitColor bg = t.background;
    if (!bg.has_channels()) return std::nullopt;
    return (0.2126 * bg.r() + 0.7152 * bg.g() + 0.0722 * bg.b()) / 255.0 > 0.5;
}

// 对于设置行的详细信息行，为什么所选主题未被使用，
// 或者关于它的警告。使用时是空的，不起眼。
[[nodiscard]] inline std::string_view theme_override_reason(const Prefs& p,
                                                            const Resolved& r) {
    if (p.theme.empty()) return {};
    if (!find_scheme(p.theme)) return "unknown scheme — using native";
    if (r.theme == &maya::theme::native)
        return "needs 256 colors — using native";

    // 极性不匹配。拥有画布的方案会在 终端 上绘制自己的背景，
    // 因此浅色 终端 上的深色方案不是 "wrong" —
    // 它有效，它只是使周围的 终端 镀铬（选项卡栏、
    // 分割边框、任何 agentty 不绘制的内容）与 面板
    // 发生冲突。
    //
    // 这是背景偏好能够真正影响的ONLY事物。
    // 它过去根本不会影响任何东西：它被检测、解析、
    // 保留和显示——一个完整的往返——而没有消费者读过它，
    // 并且该行自己的帮助承诺它是
    // 同时具备这两项能力的配色方案才会读取。
    // 而没有方案是这样。不进行任何更改的设置比缺失设置更糟糕，
    // 因为用户合理地得出结论，他们想要的东西是不可能的。
    //
    // NOTE，不是覆盖：用户在该终端上选择了该
    // 方案并且被允许表达它。因为启发式不同意而默默
    // 地交换他们的主题正是问题#37 所涉及的猜测。
    if (r.polarity != maya::theme::Polarity::Unknown) {
        const bool term_light = r.polarity == maya::theme::Polarity::Light;
        // nullopt = 该方案不绘制自己的画布（本机），
        // 因此 终端 的极性没有什么不同意的。
        if (const auto light = scheme_is_light(*r.theme);
            light && *light != term_light)
            return term_light ? "dark scheme on a light terminal"
                              : "light scheme on a dark terminal";
    }
    return {};
}

// ── 解决的主题,从观点上看是可读的 ───────────────────────────
//
// 相同接口边界，原因与上述首选项相同。 maya 发布了自己的主题插槽，
// 因此 RENDERER 可以用它进行绘画，但 agentty 的视图构建器会选择颜色，
// 而 BUILDING 源码树 - `fg_of(accent)` 会在任何
// 渲染器 看到主题之前运行很长时间 - 因此他们在构建时需要此处的调色板。
//
// 如果没有这个，应用程序中的每种颜色都是 `inline constexpr`
// ANSI 文字，并且选择主题不会改变任何你看不到的内容。
// 主题浏览器从方案表中重新绘制了自己的样本，其他 493 个 调用
// 站点继续绘制 bright_white。

namespace detail {
// POINTER，而不是副本：每个主题都存在于具有静态存储的 constexpr 表中，
// 因此插槽只是其中一个的 re-seated 视图 — 读取侧是
// 普通的原子 加载，而不是每个样式跨度上的 24 种颜色的锁定副本。
// （对比首选项，其中包含 std::string，
// 因此需要上面的 互斥量。）
inline std::atomic<const maya::Theme*>& theme_atom() noexcept {
    static std::atomic<const maya::Theme*> t{&maya::theme::native};
    return t;
}
}  // namespace detail

// 始终将主题发布到 两者 接收器。
//
// 有两种，它们满足不同的消费者：
//
//   ·该原子 — agentty 的 `ui::` 令牌，在 BUILDING 时读取
//                       元素 源码树 (fg()、静音、重音…)。
//   ·app_set_theme —maya 的 渲染器 插槽，并通过 on_theme_changed
//                       每个PROJECTED调色板：Markdown的
//                       37种平面颜色，交叉-frame组件缓存，
//                       StylePool的SGR 缓存。
//
// 它们曾经是 调用 站点上的两个 调用，这是没人能遵守的
// 规则：外观 状态更新函数 成为其中之一，
// 而 re-sealed 相对于调色板 maya 的整个
// 记录​​尚未被告知，因此深色 → 浅色交换在即将发布的主题
// 墨水中留下了每个较旧的转弯。一个入口点意味着这对不能分开——
// 调用者不会忘记第二个接收器，因为没有 调用 可以省略。
//
// 幂等且便宜：原子是指针存储，app_set_theme按值进行比较，
// 并在没有任何移动时提前返回，这是常见情况（view() re
// -publishes每个帧都有相同的主题，因此`auto`
// 可以遵循tmux分离或ssh跳跃）。
inline void publish_theme(const maya::Theme& t) noexcept {
    // 每个方案都以应用对比底色的方式发布：方案自身背景上
    // 低于4.5:1（静音为3:1）的墨槽变得更亮或更暗，
    // 保持色调，因此链接、标题和提示在所有615个方案中读取。
    // Native 不变地回来了。 maya 按值
    // 实习生实时主题，因此它的指针是解除副本的稳定位置。
    maya::app_set_theme(maya::theme::with_contrast_floor(t));
    detail::theme_atom().store(&maya::theme::live(), std::memory_order_relaxed);
}

// 主题生效。本机直到第一个 publish_theme() 为止，
// 因此在第一个 帧（直接构建元素的 单元测试）之前运行的
// 使用者将获得安全的 终端-native 调色板，
// 而不是空 deref。
[[nodiscard]] inline const maya::Theme& theme() noexcept {
    return *detail::theme_atom().load(std::memory_order_relaxed);
}

}  // namespace agentty::ui_prefs
