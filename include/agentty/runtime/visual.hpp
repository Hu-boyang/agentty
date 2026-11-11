#pragma once
// agentty::visual-STRUCTURAL 帧-hash步行。
//
// 该 PROBLEM 此 RETIRES。 visual_hash
// (program.hpp) 门 重绘：跳过哈希等于最后绘制的 帧
// 的 帧。它曾经是视图 渲染 的每个模型方面的 hand
// -maintained 枚举 - 视图依赖项集的 PARALLEL
// DESCRIPTION，没有任何东西保持两者相等。忘记一个方面会
// 产生一个签名错误类：状态更改、视图 WOULD 不同、哈希不移动、
// 帧 门吃掉 重绘（登录输入、两种形式、抹布探针判决、结果 -card
// 滚动 - 每个都是手动找到的，每个都是手动固定的）。
//
// 该 INVERSION。 mix_any(h, state) 导出哈希值 FROM 该 TYPE：
//
//   ·scalars/enums/bools→直接混合
//   • 字符串 → FNV 超过 字节 + 长度
//   ·变体→替代索引，则打开替代
//   • 选项 → 存在，然后是值
//   ·范围→大小，然后每个元素
//   ·聚合→DECOMPOSED成员-by-member(P1061)，所以
//                              成员添加的明天是明天的哈希值
//                              — 没有什么可记住的
//   ·其他一切→不编译←加载-bearing臂
//
// 不能是 auto-walked 的类型（私有成员、基 + 成员、
// 或者不得 被散列的字段）必须在其定义旁边定义
// `visual_parts(t)`：要遍历的分面的元组，使用
//
//   visual::ref(x)通过引用行走
//   x（无复制）visual::exempt这个方面是故意的NOT视觉任何值本身行走（长度、
//   计数、投影）
//
// 和 `static_assert(visual::parts_cover_all<T>)`
// 证明元组名称 恰好 与类型所具有的面一样多（基础 +
// 成员）：添加成员而不扩展部件列表，并且断言在类型上触发，
// 而不是在三个面板之后错过 重绘。因此，
// 豁免是明确的、可审查的行为；承保范围是默认的——与旧
// 合同相反。
//
// SECRETS。 field::Secret 和嵌入配置定义了 visual_parts，
// 它消化 LENGTH 仅 — 凭据 字节 永远不会达到任何哈希值。这是一个故意的、
// 断言的豁免，而不是枚举的意外（visual_hash_walk_test
// 将其固定：same-length 覆盖 = 相同的哈希值）。
// （秘密的占位符不是 凭据 — 它被绘制为 -is — 并且
// 是整个走的。）
//
// ELM FIT。 subscribe(m) 声明输入依赖项； view(
// m) 声明像素；两者都比 Model 纯净。 帧 门需要
// "did the visible state change" — 这是 FUNCTION
// OF 该 STATE TYPES，因此它是从它们派生的，就像插槽的 Kind
// 是从 变体 派生而不是在它旁边维护一样。

#include <cstdint>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace agentty::visual {

// ── 面额标记 ────────────────────────────────────────────────────────

// 故意-not-visual刻面。计入 parts_cover_all（
// 作者为 ACCOUNTED 为会员）；对哈希没有任何贡献。
struct Exempt {};
inline constexpr Exempt exempt{};

// 用于部件列表的Walk-by-reference包装器：
// `visual::ref(m.form)`遍历所指对象而不将其复制到元组中。
// （普通引用不能在 make_tuple 中使用；计算值的 forward_as_tuple
// 会悬空——这是明确的、安全的中间值。）
template <class T>
struct Ref { const T* p; };
template <class T>
[[nodiscard]] constexpr Ref<T> ref(const T& v) noexcept { return Ref<T>{&v}; }

// ── 总计 平均(基地+直接成员,作为支架) -init 插槽) 页面存档备份,存于互联网档案馆. ────────
namespace detail {
template <class Exclude>
struct AnyInit {
    // 排除防护阻止 copy/move 构造函数在非 -aggregates
    // 上满足 T{AnyInit}，这将为所有可复制的
    // 内容报告元数 1。
    //
    // DEFINED，不只是被声明，即使它仅在brace_constructible
    // 的requires-expression的未评估操作数中被命名。
    // 经典的习语对其没有定义；然后，clang 为每个探测到的类型
    // 报告 -Wundefined-inline，该类型的成员初始化通过它决定
    // 实例化的 constexpr 被调用者进行路由（std::optional
    // 的转换构造函数是通常的罪魁祸首）。 std::unreachable
    // () 正文 对于 任意 返回类型来说是很好的 -formed，
    // 没有任何成本，并且使 "never called" 契约变得明确，
    // 而不是使其成为 链接器 的问题。
    template <class T>
        requires(!std::is_same_v<std::remove_cvref_t<T>, Exclude>)
    constexpr operator T() const noexcept {
        std::unreachable();
    }
};
template <class T, std::size_t... I>
constexpr bool brace_constructible(std::index_sequence<I...>) {
    return requires { T{((void)I, AnyInit<T>{})...}; };
}
template <class T, std::size_t N = 0>
constexpr std::size_t arity_impl() {
    if constexpr (brace_constructible<T>(std::make_index_sequence<N + 1>{}))
        return arity_impl<T, N + 1>();
    else
        return N;
}
} // namespace detail

template <class T>
inline constexpr std::size_t arity =
    detail::arity_impl<std::remove_cvref_t<T>>();

// ── 定制点 ──────────────────────────────────────────────
// 由 ADL 发现：在 T 的命名空间中定义 `auto visual_parts(const T&)`，
// 返回 std::tuple 面（值、visual::ref(...)或
// visual::exempt）。每个基地和每个成员都必须有记录——用
// parts_cover_all 来证明。
template <class T>
concept HasParts = requires(const T& t) { visual_parts(t); };

// 完整性证明：部件元组的每个大括号-init槽（
// 基部或成员）具有EXACTLY一个条目。添加成员而不决定其
// 可见性在此处失败，在类型处。
//
// NON-AGGREGATES(
// 自定义ctor/dtor-e.g.maya::ScrollState
// 的注销析构函数)的数量为0：大括号-probing无法计算其成员，
// 因此无法检查计数。这曾经使证明通过了 VACUOUSLY，
// 这是安全网可能出现的最糟糕的故障模式——它是无声的，
// 并且精确地针对最可能需要它的类型。 maya::ScrollState
// 就是一个例子：它是应用程序中最常见的视图状态，并且证据无法证明
// 它是任何东西。
//
// 因此数量 0 现在失败 CLOSED。非 -aggregate
// 必须通过专门化 visual::trusted_parts 来选择加入，
// 这是一个经过深思熟虑的、可 grepable 的、经过审查的声明，
// 表明其部件列表是由人类检查的，而不是该类型声明方式的意外。
template <class T>
inline constexpr bool trusted_parts = false;

template <class T>
inline constexpr bool parts_cover_all =
    (arity<T> == 0 && trusted_parts<std::remove_cvref_t<T>>)
    || (arity<T> != 0
        && std::tuple_size_v<std::remove_cvref_t<
               decltype(visual_parts(std::declval<const T&>()))>> == arity<T>);

// ── 散步时 ─────────────────────────────────────────────────────────────
// H是采用std::uint64_t(累积混合器)的任何可调用的。

template <class H, class T>
constexpr void mix_any(H&& h, const T& v);

namespace detail {
// P1061的包结构绑定 (`auto&& [... xs] = v;`)曾经
// 住在这里。它是C++26——g++-14，这个仓库的主要CI工具链，不能解析它，
// 所以linux gcc + sanitizer的门自从落地以来就一直是红色的。
//
// 替换是固定的-arity结构的-binding梯子，在上面已经计算出的
// `arity<T>`上调度。每个梯级都准确命名 T 的大括号-init 所采用的成员，
// IN BRACE-INIT ORDER - 与 P1061 分解的顺序
// 相同 - 因此哈希 数据流 是包的 bit-identical。
//
// 旧形式的失效模式通过构造得以保留：
//   -具有基 和 成员或私有成员的类型不满足
//     任何梯级（或其中一个梯级失败）——它必须声明 visual_parts，
//     使其可见性决策明确；
//   -arity-0梯级hard-fails带有指令，因此非-decomposable
//     类型（自定义向量）永远不能默默地对散列做出任何贡献。
//     失败关闭，如前所述——现在带有一条说明要做什么的消息。
//
// 每个数量的梯级是样板文件，但非常明显；大于梯子的类型会在
// 分派 位置跳闸 static_assert，而不是无声跳过。
template <class H, class T>
constexpr void mix_decomposed(H&&, const T&, std::integral_constant<std::size_t, 0>) {
    static_assert(std::is_aggregate_v<T>,   // 发射时始终为假
                  "type reached the visual hash walk but cannot be brace-"
                  "decomposed (custom ctors, or bases+members / private "
                  "members): declare visual_parts(T) for it — see the "
                  "customization point above");
    // EMPTY聚合体分解为空包：不贡献任何东西，与它所取代的
    // P1061包完全相同。 （标记结构 — ui::panel::None
    // 等 — 按设计位于此处。）
}
#define AGENTTY_VISUAL_MIX_RUNG(n, ...)                                        \
    template <class H, class T>                                                \
    constexpr void mix_decomposed(H&& h, const T& v,                           \
                                  std::integral_constant<std::size_t, n>) {    \
        auto& [__VA_ARGS__] = v;                                               \
        std::apply([&](const auto&... xs) { (mix_any(h, xs), ...); },          \
                   std::forward_as_tuple(__VA_ARGS__));                        \
    }
AGENTTY_VISUAL_MIX_RUNG(1,  m0)
AGENTTY_VISUAL_MIX_RUNG(2,  m0, m1)
AGENTTY_VISUAL_MIX_RUNG(3,  m0, m1, m2)
AGENTTY_VISUAL_MIX_RUNG(4,  m0, m1, m2, m3)
AGENTTY_VISUAL_MIX_RUNG(5,  m0, m1, m2, m3, m4)
AGENTTY_VISUAL_MIX_RUNG(6,  m0, m1, m2, m3, m4, m5)
AGENTTY_VISUAL_MIX_RUNG(7,  m0, m1, m2, m3, m4, m5, m6)
AGENTTY_VISUAL_MIX_RUNG(8,  m0, m1, m2, m3, m4, m5, m6, m7)
AGENTTY_VISUAL_MIX_RUNG(9,  m0, m1, m2, m3, m4, m5, m6, m7, m8)
AGENTTY_VISUAL_MIX_RUNG(10, m0, m1, m2, m3, m4, m5, m6, m7, m8, m9)
AGENTTY_VISUAL_MIX_RUNG(11, m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10)
AGENTTY_VISUAL_MIX_RUNG(12, m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11)
#undef AGENTTY_VISUAL_MIX_RUNG
template <class>
inline constexpr bool is_ref = false;
template <class T>
inline constexpr bool is_ref<Ref<T>> = true;
} // namespace detail

template <class H, class T>
constexpr void mix_any(H&& h, const T& v) {
    using U = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<U, Exempt>) {
        // 占，无贡献
    } else if constexpr (detail::is_ref<U>) {
        mix_any(h, *v.p);
    } else if constexpr (HasParts<U>) {
        static_assert(parts_cover_all<U>,
                      "visual_parts(T) must account for every base and "
                      "member of T (use visual::exempt for non-visual ones)");
        std::apply([&](const auto&... parts) { (mix_any(h, parts), ...); },
                   visual_parts(v));
    } else if constexpr (std::is_same_v<U, bool>) {
        h(v ? 2ull : 1ull);
    } else if constexpr (std::is_enum_v<U>) {
        h(static_cast<std::uint64_t>(static_cast<std::int64_t>(v)) + 0x9e37ull);
    } else if constexpr (std::is_floating_point_v<U>) {
        // 量化：sub-1/8192摆动在存储单元分辨率下是不可见的。
        h(static_cast<std::uint64_t>(static_cast<std::int64_t>(v * 8192.0)));
    } else if constexpr (std::is_arithmetic_v<U>) {
        h(static_cast<std::uint64_t>(static_cast<std::int64_t>(v)));
    } else if constexpr (std::is_convertible_v<const U&, std::string_view>) {
        const std::string_view s{v};
        std::uint64_t fnv = 1469598103934665603ull;
        for (const unsigned char c : s) {
            fnv ^= c;
            fnv *= 1099511628211ull;
        }
        h(fnv);
        h(s.size());
    } else if constexpr (requires {
                             v.index();
                             std::visit([](const auto&) {}, v);
                         }) {
        h(static_cast<std::uint64_t>(v.index()) + 0x51ull);
        std::visit([&](const auto& alt) { mix_any(h, alt); }, v);
    } else if constexpr (requires {
                             v.has_value();
                             *v;
                         }) {
        h(v.has_value() ? 2ull : 1ull);
        if (v.has_value()) mix_any(h, *v);
    } else if constexpr (requires {
                             v.begin();
                             v.end();
                             v.size();
                         }) {
        h(static_cast<std::uint64_t>(v.size()) + 0xC0ull);
        for (const auto& e : v) mix_any(h, e);
    } else {
        static_assert(arity<T> <= 12,
                      "aggregate with more than 12 members hit the visual hash "
                      "walk: extend the mix_decomposed ladder in this header, "
                      "or declare visual_parts(T) for the type");
        detail::mix_decomposed(h, v,
                               std::integral_constant<std::size_t, arity<T>>{});
    }
}

} // namespace agentty::visual
