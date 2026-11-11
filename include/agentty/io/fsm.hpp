#pragma once
// agentty::io::fsm —编译-time类型状态机用于IO/network生命周期。
//
// 该头文件是agentty的类型状态模式的GENERIC、
// 依赖性-free核心。它不携带 传输层 类型（没有 OpenSSL、
// 没有 nghttp2、没有套接字），因此它可以包含在任何地方 - 驱动真正
// 传输层 的具体状态结构在拥有这些类型的 编译单元 中定义（
// src/io/http.cpp 代表 HTTP/2 连接生命周期等）。
//
// 它给你什么
// -----------------
//   ·State<Tag>-move-only，non-copyable空标记基数a
//                             具体状态结构继承自。
//                             Move-only 所有权 就是
//                             重点：状态值是*功能令牌*。
//                             你消耗它来过渡；一旦消耗它就消失了，
//                             所以你不能驱动两次转换或乱序。
//
//   ·transition<From,To>-声明机器的合法边缘。专精
//                             `legal_edge<From,To>`(通过DECLARE_EDGE
//                             宏)将From→To标记为允许的。返回 `Result<To>`
//                             的转换方法由
//                             `static_assert(is_legal_edge_v<From,To>)` 门控，
//                             因此非法边缘无法到达 COMPILE — 诊断命名
//                             了确切的 From/To 对。
//
//   ·结果<S> — `std::expected<S, E>`，用于以下任一的转变：
//                             前进到状态S或因域错误E而失败。
//                             默认错误类型，因此每个子系统插入
//                             其自己的(HttpError等)。
//
// 为什么这是zero-cost
// ---------------------
// 每个状态结构体都是empty-or-payload-only和move-only；
// 没有虚函数表，没有类型擦除，没有堆。 `legal_edge` 是在
// 编译 时间解析的纯 type-level 特征 - 它不生成任何代码。
// `consteval` `assert_legal_edge` 在
// 编译 期间被评估并丢弃。在 运行时，类型状态管道与您手写的移动和
// `std::expected` 检查完全相同；这些类型只会使非法订单变得
// 无法代表。这保留了 agentty 的 speed/size 合约：
// 高频路径上没有每-event 分配、没有分派成本。
//
// 使用草图（具体状态位于拥有传输层的.cpp中）：
//
//     struct Dialing      : fsm::State<struct DialingTag>      { /* inputs */ };
//     struct TcpConnected : fsm::State<struct TcpConnectedTag> { socket_t fd; };
//     AGENTTY_FSM_EDGE(Dialing, TcpConnected);
//
//     fsm::Result<TcpConnected, HttpError> 连接(拨号&& s) {
//         fsm::assert_legal_edge<Dialing, TcpConnected>();
//         ... // 进行真正的拨号；关于成功：
//         return TcpConnected{ .fd = fd };
//     }
//
// 调用机器未声明的转换（e.g.拨号 → H2Ready 跳过 TLS）会
// 导致 static_assert 跳闸，而不会跳闸 链接。

#include <expected>
#include <type_traits>
#include <utility>

namespace agentty::io::fsm {

// ---------------------------------------------------------------------------
// State<Tag> —用于具体类型状态的 move-only 标记基础。
// ---------------------------------------------------------------------------
// 标签是一种独特的（通常不完整的）类型，即使当它们的载荷一致时，
// 它也将一个状态与另一个状态区分开来。继承State<Tag>，
// 得到具体的结构体：
//   ·non-copyable-能力令牌不能重复；
//   ·move-constructible——它可以被转换消耗（通过&&传递）；
//   ·default-constructible来自其自己的聚合初始值设定项。
//
// 基础不保存数据并且可以简单地重新定位，因此从它导出不需要
// 任何成本（应用empty-base优化）。
template <class Tag>
struct State {
    using tag_type = Tag;

    constexpr State() noexcept = default;

    State(const State&)            = delete;
    State& operator=(const State&) = delete;

    State(State&&) noexcept            = default;
    State& operator=(State&&) noexcept = default;

    // 公共、非-virtual：状态是值类型，永远不会通过基指针
    // 删除（没有人持有State*）。公共的，因此派生的聚合可以是大括号
    // -initialized——受保护的基dtor会阻止它。
    ~State() = default;
};

// 为真当且仅当S是我们的类型状态之一(源自State<S::tag_type>)。
template <class S, class = void>
inline constexpr bool is_state_v = false;
template <class S>
inline constexpr bool is_state_v<
    S, std::void_t<typename S::tag_type>> =
        std::is_base_of_v<State<typename S::tag_type>, S>;

// ---------------------------------------------------------------------------
// legal_edge<From, To> — 类型-level机器的邻接关系。
// ---------------------------------------------------------------------------
// 边From→To是合法的，当且仅当`From`在其声明的后继
// 者中命名`To`。状态用 in-class 类型列表声明其后继者：
//
//     struct TlsUp : fsm::State<TlsUpTag> {
//         using fsm_to = fsm::to<H2Ready>;   // single successor
//         ...
//     };
//     struct Routing : fsm::State<RoutingTag> {
//         using fsm_to = fsm::to<StateA, StateB>;  // a branch
//     };
//
// 这保留了整个声明IN-CLASS——没有out
// -of-line模板特化——因此即使当状态结构存在于匿名命名
// 空间中时它也能工作（其中将特化注入fsm::将是ill-formed）。
// 没有 `fsm_to` 的状态是 终端：没有边离开它。
template <class... To>
struct to {};

namespace detail {
template <class T, class List>
struct list_has : std::false_type {};
template <class T, class... Xs>
struct list_has<T, to<Xs...>>
    : std::bool_constant<(std::is_same_v<T, Xs> || ...)> {};

// Extract From 的后继者列表，或者如果它声明没有后继者列表，则为空的`to<>`。
template <class From, class = void>
struct successors { using type = to<>; };
template <class From>
struct successors<From, std::void_t<typename From::fsm_to>> {
    using type = typename From::fsm_to;
};
}  // namespace detail

template <class From, class To>
struct legal_edge
    : detail::list_has<To, typename detail::successors<From>::type> {};

template <class From, class To>
inline constexpr bool is_legal_edge_v = legal_edge<From, To>::value;

// 编译-time防护放置在每个转换函数正文的顶部。
// 如果从→到不是声明的边缘，则这是 ill-formed 并且构建
// 失败并出现命名两个状态的诊断。 Consteval ⇒ 在
// 编译 时间评估并丢弃；不发出任何代码。
template <class From, class To>
consteval void assert_legal_edge() noexcept {
    static_assert(is_state_v<From>, "FSM: From is not an fsm::State");
    static_assert(is_state_v<To>,   "FSM: To is not an fsm::State");
    static_assert(is_legal_edge_v<From, To>,
                  "FSM: illegal state transition — To is not among From's "
                  "declared successors. Add `To` to From's `using fsm_to = "
                  "fsm::to<...>` list if this transition is intended.");
}

// ---------------------------------------------------------------------------
// 结果<S, E>——转换结果：前进到状态S，或者失败并出现E。
// ---------------------------------------------------------------------------
// 错误类型是模板参数，因此每个子系统提供其自己的带类型的错误（
// agentty::http::HttpError、子进程错误……）。转换返回 Result
// <NextState, DomainError>。
template <class S, class E>
using Result = std::expected<S, E>;

}  // namespace agentty::io::fsm

