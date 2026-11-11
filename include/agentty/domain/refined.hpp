#pragma once
// agentty::domain::refined-细化类型：
// `Refined<T, Predicate>`是类型`T`的值，
// 其携带某些谓词成立的*type-level*证明。一旦你有了一个，
// 下游代码就可以读取内部值，而无需re-checking——构造它的唯一
// 方法是通过`try_make`，它验证并返回`expected`。
//
// 这是通过新类型加上智能构造函数的 Rust
// "make invalid states unrepresentable" 的
// C++ 类似物。顶部的每个“将”接受 `std::string` 和 re
// -validate "is this empty?" 的地方都接受
// `NonEmpty<std::string>` ——验证已经在构造时发生，类型系统证明了这一点。
//
// `Predicate`是具有
// `static constexpr bool check(const T&)`的
// 无状态函子（因此对于文字输入，验证可以是`consteval`）。检查失败会返回
// 带类型的 原因 - 调用者决定如何处理它。相同的谓词类型是细化的“身份”：
// `NonEmpty<string>` 和 `NotBlank<string>`
// 是不同的类型，即使两者都包装 `string`。

#include <concepts>
#include <expected>
#include <format>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace agentty::domain {

// ── 优先概念 ────────────────────────────────────────────────────
template <class P, class T>
concept RefinementPredicate = requires(const T& v) {
    { P::check(v) } -> std::convertible_to<bool>;
    { P::what()  } -> std::convertible_to<std::string_view>;
};

// ── 已精炼 <T, Predicate> ────────────────────────────────────────────────
// 持有-by-value新类型。 `value()` 返回内部 T。
// 唯一的构造函数是：defaulted（当 T 为 default
// -constructible 和 时，默认值满足 P）和静态 `try_make` 工厂。
//
// 对于文字输入，存在 `try_make` 的 `consteval` 重载；
// 它使 "non-empty literal string" 成为 编译-time 检查。
// 传递 `std::string_view` 文字，在持续评估期间，您将出现 编译
// 或失败-with- `static_assert`-ish 错误。
template <class T, class Predicate>
    requires RefinementPredicate<Predicate, T>
class Refined {
    T value_{};

    explicit constexpr Refined(T v) noexcept(std::is_nothrow_move_constructible_v<T>)
        : value_(std::move(v)) {}

public:
    using value_type = T;
    using predicate  = Predicate;

    [[nodiscard]] constexpr const T& value() const noexcept { return value_; }
    [[nodiscard]] constexpr operator const T&() const noexcept { return value_; }

    // 当T是字符串-like时透明转换到std::string_view。
    // C++最多允许隐式链中的一个user-defined转换，
    // 因此上面的`operator const T&`不会在直接采用它的调用
    // 站点处组成string_view(正则表达式构造函数，
    // string_view-typed参数)。提供此重载可以绕过链限制 - 调
    // 用 与 `is_literal_pattern(refined_string)`
    // 一样可以在没有显式 `.value()` 的情况下工作。
    // 受限于 `T` 可转换为 `string_view`，因此这是用于整体或容器
    // -typed 精炼的 no-op。
    [[nodiscard]] constexpr operator std::string_view() const noexcept
        requires std::convertible_to<const T&, std::string_view> {
        return std::string_view{value_};
    }

    // 验证工厂。返回失败时的 带类型的 原因，以便调用者可以构建自己的 带类型的
    // 错误 (e.g.ToolError::invalid_args(...))。
    struct Error {
        std::string_view what;        // = Predicate::what()
        std::string      detail;      // 可选上下文（caller-supplied)
    };

    [[nodiscard]] static constexpr std::expected<Refined, Error>
    try_make(T v, std::string_view detail_context = {}) {
        if (Predicate::check(v))
            return Refined{std::move(v)};
        return std::unexpected(Error{Predicate::what(), std::string{detail_context}});
    }

    // consteval 工厂：与 try_make 相同，
    // 但只能在常量评估上下文中调用。用于构建精炼的文字；
    // 通过 `Predicate::check` 约束失败，故障会变成硬 编译 错误。
    [[nodiscard]] static consteval Refined make_unchecked(T v) {
        // 调用者断言谓词成立；失败是UB。
        return Refined{std::move(v)};
    }

    [[nodiscard]] friend constexpr bool operator==(
        const Refined& a, const Refined& b) noexcept(noexcept(a.value_ == b.value_)) {
        return a.value_ == b.value_;
    }

    // 数据流插入——当存在时遵循T的运算符<<。让精确值流经
    // `std::ostream` / `std::ostringstream`，
    // 而不需要 `.value()` 调用，
    // 因为到 `const T&` 的隐式转换不会启动模板化运算符 << 查找。
    template <class CharT, class Traits>
        requires requires(std::basic_ostream<CharT, Traits>& os, const T& v) {
            { os << v } -> std::same_as<std::basic_ostream<CharT, Traits>&>;
        }
    friend std::basic_ostream<CharT, Traits>&
    operator<<(std::basic_ostream<CharT, Traits>& os, const Refined& r) {
        return os << r.value_;
    }
};

// ── 预测数 ───────────────────────────────────────────────────────────

// `NonEmpty`用于具有`.empty()`的任何容器类型。
struct NonEmptyPred {
    template <class T>
        requires requires(const T& t) { { t.empty() } -> std::convertible_to<bool>; }
    [[nodiscard]] static constexpr bool check(const T& v) noexcept {
        return !v.empty();
    }
    [[nodiscard]] static constexpr std::string_view what() noexcept {
        return "must be non-empty";
    }
};

template <class T> using NonEmpty = Refined<T, NonEmptyPred>;

// `Bounded<T, Lo, Hi>`——积分T约束为[Lo，Hi]。 Lo/Hi
// 是嵌入到类型标识中的非 -type 模板参数，
// 因此 `Bounded<int, 0, 100>` 和 `Bounded<int, 0, 1000>` 是不同的类型。
template <std::integral T, T Lo, T Hi>
struct InRangePred {
    static_assert(Lo <= Hi, "bounded range must satisfy Lo <= Hi");
    [[nodiscard]] static constexpr bool check(T v) noexcept {
        return v >= Lo && v <= Hi;
    }
    [[nodiscard]] static constexpr std::string_view what() noexcept {
        return "must be within bounded range";
    }
};

template <std::integral T, T Lo, T Hi>
using Bounded = Refined<T, InRangePred<T, Lo, Hi>>;

// `Positive<T>` —严格> 0。足够常见，值得拥有自己的别名。
template <std::integral T>
struct PositivePred {
    [[nodiscard]] static constexpr bool check(T v) noexcept { return v > 0; }
    [[nodiscard]] static constexpr std::string_view what() noexcept {
        return "must be > 0";
    }
};
template <std::integral T> using Positive = Refined<T, PositivePred<T>>;

// `NonNegative<T>`—≥0.积分符号空间的另一半；对于合法允许
// 0 ("skip nothing") 的偏移量很有用，
// 其中 `Positive` 过于严格。
template <std::integral T>
struct NonNegativePred {
    [[nodiscard]] static constexpr bool check(T v) noexcept { return v >= 0; }
    [[nodiscard]] static constexpr std::string_view what() noexcept {
        return "must be ≥ 0";
    }
};
template <std::integral T> using NonNegative = Refined<T, NonNegativePred<T>>;

// `NonBlank`-non-empty AND包含至少一个non-whitespace 字节。
// 比`NonEmpty`严格：拒绝全是空格的字符串。
// 用于 user/model-supplied 标记（搜索模式、glob 表达式、
// 提交消息），其中 "   " 是 UI-survivable-but-semantically-empty 输入，
// 如果接受，它会做一些无用的事情。
struct NonBlankPred {
    template <class T>
        requires requires(const T& t) {
            { t.begin() } -> std::input_iterator;
            { t.end()   } -> std::input_iterator;
        }
    [[nodiscard]] static constexpr bool check(const T& v) noexcept {
        for (auto c : v) {
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r' && c != '\f' && c != '\v')
                return true;
        }
        return false;
    }
    [[nodiscard]] static constexpr std::string_view what() noexcept {
        return "must contain at least one non-whitespace character";
    }
};
template <class T> using NonBlank = Refined<T, NonBlankPred>;

// `MaxLen<N>`-size()≤N。NonEmpty的另一侧：防止传输
// -or-模型-supplied斑点被吹过理智的天花板。绑定的 N 是非 -type 模板参数，
// 因此 `MaxLen<std::string, 1024>` 和
// `MaxLen<std::string, 65536>` 是不同的类型。
template <std::size_t N>
struct MaxLenPred {
    template <class T>
        requires requires(const T& t) { { t.size() } -> std::convertible_to<std::size_t>; }
    [[nodiscard]] static constexpr bool check(const T& v) noexcept {
        return v.size() <= N;
    }
    [[nodiscard]] static constexpr std::string_view what() noexcept {
        return "exceeds maximum length";
    }
};
template <class T, std::size_t N>
using MaxLen = Refined<T, MaxLenPred<N>>;

// ── 编译-time 改进机制试验 ───────────────────────
namespace tests {
static_assert(NonEmpty<std::string>::try_make(std::string{"x"}).has_value());
static_assert(!NonEmpty<std::string>::try_make(std::string{}).has_value());
static_assert(Bounded<int, 0, 10>::try_make(5).has_value());
static_assert(!Bounded<int, 0, 10>::try_make(11).has_value());
static_assert(!Bounded<int, 0, 10>::try_make(-1).has_value());
static_assert(Positive<int>::try_make(1).has_value());
static_assert(!Positive<int>::try_make(0).has_value());
static_assert(NonNegative<int>::try_make(0).has_value());
static_assert(NonNegative<int>::try_make(5).has_value());
static_assert(!NonNegative<int>::try_make(-1).has_value());
static_assert(NonBlank<std::string>::try_make(std::string{"x"}).has_value());
static_assert(NonBlank<std::string>::try_make(std::string{" x "}).has_value());
static_assert(!NonBlank<std::string>::try_make(std::string{"   "}).has_value());
static_assert(!NonBlank<std::string>::try_make(std::string{"\t\n"}).has_value());
static_assert(MaxLen<std::string, 4>::try_make(std::string{"abcd"}).has_value());
static_assert(!MaxLen<std::string, 4>::try_make(std::string{"abcde"}).has_value());
} // namespace tests

} // namespace agentty::domain

// std::formatter直通-Refined<T,P>格式与T完全相同，
// 因此当`offset`从`int`升级到`Positive<int>`时，
// 现有的`std::format("{}", a.offset)` 调用继续工作。
// 如果没有这个，格式的引用-taking make_format_args
// ()不会看到隐式的`operator const T&`，并且调用无法看到编译。
// 位于命名空间 std（该标准要求针对 user-type 格式化程序专门化）。
template <class T, class P, class CharT>
struct std::formatter<agentty::domain::Refined<T, P>, CharT>
    : std::formatter<T, CharT>
{
    template <class FormatContext>
    auto format(const agentty::domain::Refined<T, P>& r, FormatContext& ctx) const {
        return std::formatter<T, CharT>::format(r.value(), ctx);
    }
};
