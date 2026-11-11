#pragma once
// agentty::session —单个 in-flight LLM 请求的每个 -turn 状态。
// 完整的设计原理参见docs/design/streaming.md。

#include <array>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include <maya/core/animation.hpp>
#include <maya/core/motion.hpp>   // anim::Motion (自身-ticking disp_rate)
#include <maya/widget/spinner.hpp>

#include "agentty/domain/compaction_style.hpp"
#include "agentty/domain/complexity.hpp"
#include "agentty/domain/id.hpp"
#include "agentty/domain/smart_mode.hpp"

namespace agentty::http { class CancelToken; }

namespace agentty {

// ── 重试 / 摊位 看门狗 状态机器 ──────────────────────
// 曾经是两个并行的布尔值(`stall_dispatched`，
// `retry_pending`)，它们一起编码一个3-state机器； 不变量 仅限评论，
// 而 -maintained 则跨 状态更新函数 中的约 6 个站点。现在是正确的
// 和类型：
//
//   新鲜——数据流活着，看门狗武装。
//   StallFired-看门狗触发了取消令牌，合成StreamError正在飞行中。
//                  工作线程 线程 的晚期 StreamError(
//                  "cancelled") 必须将 re-classified 作为瞬态，
//                  而不是 user-cancel。
//   已安排 — StreamError 处理函数 通过 Cmd::after
//                  安排了 RetryStream。等待期间到达的第二个
//                  StreamError 不得 安排另一个 重试。
//
// 转变：
//   新鲜 → StallFired (勾选 看门狗 检测到死亡 数据流)
//   新鲜 → 预定 (StreamError 直接激发，非 -stall)
//   StallFired → 预定 (合成 StreamError 安排 重试)
//   预定 → 新鲜 (RetryStream 激发，新鲜 数据流 开始) 任意
//   → 新鲜 (CancelStream， StreamStarted复位）
namespace retry {
struct Fresh      {};
struct StallFired {};
struct Scheduled  {};
} // namespace retry
using RetryState = std::variant<retry::Fresh, retry::StallFired, retry::Scheduled>;

namespace phase {

// Per-turn 流式传输上下文：每当请求生命周期处于运行状态时就处于
// 活动状态（流式传输 → AwaitingPermission →
// ExecutingTool → 流式传输 → … → 空闲）。嵌入在每个非
// -Idle 阶段 变体 替代项中，因此当 FSM 空闲时，DO 不 EXIST
// 下面的字段。从 Idle 读取它们现在是类型错误，而不是被默认
// -zero值掩盖的逻辑错误。
//
// 上下文流过合法转换：流式传输→等待-权限保留相同的`cancel`令牌、
// 相同的`started`标记、相同的重试计数器。下面的转换函数通过
// `&&` 获取源，并通过 -wrap 获取目标 变体 内的 `ctx`
// — C++ 中没有用于
// 一个可选的 Active 对象，跟随当前恰好处于活动状态的阶段。
// 的插槽，因此 FSM 本身
// 携带它。
struct Active {
    std::shared_ptr<agentty::http::CancelToken> cancel;

    // 转动启动（设置为空闲→流式传输）并且观察到事件
    // -time-of-last--activity（在每个SSE 事件上碰撞）。
    // 它们一起驱动 elapsed-time 标签 和 120-s 档位 看门狗。
    std::chrono::steady_clock::time_point started{};
    std::chrono::steady_clock::time_point last_event_at{};

    // 当 socket/TLS 层有效但 model/SSE
    // 帧 不一定逃脱中间体时设置。这将失败标记为 mid-stream，
    // 而不将代理 PING 流量视为语义模型进度。
    bool transport_activity = false;

    // Per-turn重试计数器。 truncation_retries：当 数据流
    // EOF 中 -tool-args 时，静默 re-launches。
    // transient_retries：5xx/网络/超载/429。独立预算。
    //
    // transient_retries每回合纯粹是NOT
    // 单调递增的：只要传输证明健康，它就重置为0——第一内容
    // 增量OR心跳(SSE ping/thinking_delta)。
    // 数据流 连接、ping，然后在每次尝试爬梯子的任何
    // 内容之前停止，并在会话终止时达到 kMaxRetries；
    // 心跳重置为每个健康的-then-stalled尝试提供了一个新的预算，
    // 以便长会话恢复而不是锁定终端。
    int truncation_retries = 0;
    int transient_retries  = 0;

    // 当回合将其全部输出预算用于推理并且正在以最低努力
    // 重新-sent时设置。任何提供商；每回合一次。
    bool reasoning_starved_retry = false;

    // Mid-stream 故障计数 — 每次为 之后 发生的故障
    // 安排 重试 时都会增加，传输 在本回合中证明自己还活着（停顿 看门狗 触发，
    // 或者内容增量已经到达）。与 `transient_retries` 不同，
    // 这是通过心跳或第一个 -delta 重置的 绝不：持续交付一个
    // byte/ping 然后死亡的连接是真正的中断，并且重置该单个 字节
    // 上的预算将使其永远 重试。 mid-stream 重试 上限 (
    // provider::max_retries_for(klass,
    // mid_stream=true)) 对照 此 计数器进行检查，
    // 因此 "only N attempts once we've seen bytes"
    // 保证实际上锁存 终端，而不是在摆动的 传输 上循环。 Connect
    // -time（pre-delta、pre-stall）故障不会影响它 -
    // 它们受完整 transient_retries 预算的控制。
    int mid_stream_failures = 0;

    // 最后 transient/stall 故障的 Wall-clock 标记。
    // 重试 决定会衰减预算：如果上一次失败的时间超过
    // kRetryDecayWindow 之前，则连接在此期间
    // 一直正常，因此 transient_retries 在
    // 计数此之前重置。阻止 multi-hour 会话累积不
    // 相关的 Brown-out 故障而导致永久预算耗尽。
    std::chrono::steady_clock::time_point last_failure_at{};

    // 重试上的HARD上限与ZERO模型进度。上面的衰减窗口
    // 有一个盲点：重试 BACKOFF WAIT本身计入90秒窗口，
    // 因此接受连接但每个请求都失败的服务器（
    // 本地llama.cpp和坏path/model）可以交替
    // 失败→长退避→衰减-reset→永远失败 - 报告的
    // "dead loop"。该计数器在 每个 计划的 重试 上递增，
    // 并通过真实模型输出（非 -empty 内容增量或结构化工具
    // 调用）重置 仅 — 绝不是通过心跳，也绝不是通过衰减窗口。
    // 当它超过 kMaxNoProgressFailures 时，
    // 无论类别如何，失败都会被视为 终端：N 次连续尝试，
    // 没有单一内容 字节 是一个损坏的端点，而不是棕色-out。
    int no_progress_failures = 0;

    // 现场tok/s车速表——text/json Delta的字节，并不是很少见
    // 的使用领域。 first_delta_at 将 TTFT 从速率除数中排除。
    // 在每个StreamStarted (sub-turn)重置，
    // 但在单个sub-turn的增量上累积。
    std::size_t live_delta_bytes = 0;
    std::chrono::steady_clock::time_point first_delta_at{};
    std::chrono::steady_clock::time_point rate_last_sample_at{};
    std::size_t rate_last_sample_bytes = 0;

    // Retry/stall机器状态——参见上面的RetryState。
    RetryState retry = retry::Fresh{};

    // 当本回合的发射等待延迟检索时非零；只有具有此 ID 的
    // ProactiveContextReady 才能启动它。
    std::uint64_t awaiting_retrieval = 0;
};

// ── 阶段类型 ──────────────────────────────────────────────────────
// Idle 不包含任何内容——没有 in-flight 请求来携带上下文。
// 流式传输 / AwaitingPermission / ExecutingTool
// 各携带一个Active块；它们之间唯一的区别是标签。
//
// 为什么这三个都需要 相同 上下文（而不是每个都
// 只剥离它关心的字段）：
//   ·取消-Esc必须在每个活动阶段工作。甚至
//                    AwaitingPermission
//                    需要实时令牌，因此用户Esc取消底层
//                    SSE 数据流（请求仍然打开，
//                    等待下一个工具参数）。
//   ·last_event_at-停顿看门狗在每个活动阶段运行；
//                    在ExecutingTool期间沉默的
//                    数据流仍然是停滞的数据流。
//   ·重试计数器/retry_state —错误重试可以从任何
//                    活动阶段（当我们处于 AwaitingPermission、
//                    e.g 时，StreamError 可以着陆。
//                    当用户正在读取 权限 提示词 时，
//                    服务器终止了请求）并且需要保留尝试计数。
struct Idle               {};
struct Streaming          { Active ctx; };
struct AwaitingPermission { Active ctx; };
struct ExecutingTool      { Active ctx; };

} // namespace phase

using Phase = std::variant<phase::Idle, phase::Streaming,
                           phase::AwaitingPermission, phase::ExecutingTool>;

// ── 活动-context访问器 ─────────────────────────────────────────
// 过去在平板StreamState上触摸`m.s.cancel`
// /`m.s.last_event_at`/等的60-odd阅读器站点现在通过
// `active_ctx(m.s.phase)`。当阶段处于空闲
// 状态时返回 nullptr — 仅在活动阶段运行的读取器
// 可以无条件取消引用；可能从空闲状态运行的读者（Tick 看门狗、
// status-bar 小部件）首先检查 null。单次访问取代了约
// 60 手 -written `std::get_if` 链。
[[nodiscard]] inline phase::Active* active_ctx(Phase& p) noexcept {
    return std::visit([](auto& v) -> phase::Active* {
        if constexpr (requires { v.ctx; }) return &v.ctx;
        else                               return nullptr;
    }, p);
}
[[nodiscard]] inline const phase::Active* active_ctx(const Phase& p) noexcept {
    return std::visit([](const auto& v) -> const phase::Active* {
        if constexpr (requires { v.ctx; }) return &v.ctx;
        else                               return nullptr;
    }, p);
}

// 消耗源阶段的ctx；如果空闲则返回 nullopt。
// 由 ctx 从旧阶段流向新阶段的过渡站点使用：调用者在每个站点进行
// 4-arm 访问，而不是 hand-rolling
//
//     auto ctx = take_active_ctx(std::move(m.s.phase));
//     m.s.phase=phase::ExecutingTool{std::move(ctx).value()}；
//
// .value()断言"we expected an active source here"
// ——来自空闲泄漏到流式传输
// -only转换站点的错误中止而不是默默地破坏默认-constructed ctx。
[[nodiscard]] inline std::optional<phase::Active> take_active_ctx(Phase&& p) noexcept {
    return std::visit([](auto&& v) -> std::optional<phase::Active> {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::same_as<T, phase::Idle>) return std::nullopt;
        else                                        return std::move(v.ctx);
    }, std::move(p));
}

// 下降到空闲状态，并在途中的 取消 回合的 数据流 上。仅分配空闲就会使
// 工作线程 带着实时令牌运行，并且其后期的 事件 会到达下一个回合开始的位置。
// 在 cmd_factory.cpp 中定义（需要 http.hpp）。
void end_turn(Phase& p) noexcept;

// ── 重排重排剪贴板 ───────────────────────────────────
// 重试站点都执行相同的舞蹈：将ctx拉出阶段，撞击其重试簿记，
// 并使用预定哨兵重新-enter 流式传输。 Hand-rolled，
// 该舞蹈需要 `.value()` - 注释中包含的证明义务（
// "phase IS active here"）；如果出错（取消
// 进入空闲状态后出现后期错误），状态更新函数 会抛出
// bad_optional_access → std::terminate。
// 该组合器使操作 TOTAL：空闲情况返回 false（阶段恢复
// 到空闲）而不是中止，并且调用者在布尔上分支。在阶段为 re
// -seated 之前，变异器在真实的 ctx 上只运行一次。
template <class F>
    requires std::invocable<F&, phase::Active&>
[[nodiscard]] inline bool reschedule_streaming(Phase& p, F&& mutate) noexcept(
    std::is_nothrow_invocable_v<F&, phase::Active&>) {
    auto ctx = take_active_ctx(std::move(p));
    if (!ctx) { p = phase::Idle{}; return false; }
    mutate(*ctx);
    p = phase::Streaming{std::move(*ctx)};
    return true;
}

[[nodiscard]] constexpr std::string_view to_string(const Phase& p) noexcept {
    return std::visit([](const auto& v) -> std::string_view {
        using T = std::decay_t<decltype(v)>;
        if      constexpr (std::same_as<T, phase::Idle>)               return "idle";
        else if constexpr (std::same_as<T, phase::Streaming>)          return "streaming";
        else if constexpr (std::same_as<T, phase::AwaitingPermission>) return "permission";
        else                                                           return "working";
    }, p);
}

// ── 带类型的 阶段过渡 ───────────────────────────────────────
// 下面的每个函数命名两个阶段之间的SPECIFIC合法转换。
// 签名按值-move消耗源并返回目的地；非法转换（空闲
// → ExecutingTool 等）根本没有定义函数，
// 因此任何使用 带类型的 API 的 状态更新函数 站
// 点都不能将 调用 视为非法转换 — 函数查找无法找到
// 编译。
//
// 合法转移图（闭集参见下面的phase::
// PhaseKind）：
//
//                  start() land_perm() exec_tool() 空闲 ────────
//   ────▶ 流式传输 ──────────▶ AwaitingPermission ──────────▶ ExecutingTool
//   ▲                       │                       │                              │
//   │ finish()              │                       │ reject()                     │ done_tool()
//   │                       │                       ▼                              ▼
//   └──────────────────────────────────── Idle ←────────────────────────────────────
//                                                                                  │
//                  resume_stream() (工具完成后) │ 流式传输 ◀───────────
//   ───────────────────────────────────────────────────────┘
//
// abort()是特殊的——将ANY活动阶段折叠回空闲状态。由 Esc /
// 取消 / 终端 错误 处理函数 使用。
namespace phase {

// 闭集的相恒等式。与 变体 相替代品以 1:1 配对；
// 下面的合法性矩阵在此 枚举 上建立索引。
enum class PhaseKind : std::uint8_t {
    Idle,
    Streaming,
    AwaitingPermission,
    ExecutingTool,
};

[[nodiscard]] constexpr PhaseKind kind_of(const Phase& p) noexcept {
    return std::visit([](const auto& v) -> PhaseKind {
        using T = std::decay_t<decltype(v)>;
        if      constexpr (std::same_as<T, Idle>)               return PhaseKind::Idle;
        else if constexpr (std::same_as<T, Streaming>)          return PhaseKind::Streaming;
        else if constexpr (std::same_as<T, AwaitingPermission>) return PhaseKind::AwaitingPermission;
        else                                                    return PhaseKind::ExecutingTool;
    }, p);
}

// 真当且仅当（从、到）转换是上面的合法图的一部分。
// abort()被编码为"any → Idle"(每个从-phase都可以达到Idle)。
[[nodiscard]] constexpr bool is_legal_transition(PhaseKind from, PhaseKind to) noexcept {
    using K = PhaseKind;
    // 每个阶段都可以中止至空闲。
    if (to == K::Idle) return true;
    // 空闲→流式传输：开始新一轮。
    if (from == K::Idle               && to == K::Streaming)          return true;
    // 流式传输→AwaitingPermission:型号发出tool_use，等待用户。
    if (from == K::Streaming          && to == K::AwaitingPermission) return true;
    // AwaitingPermission→ExecutingTool：用户批准。
    if (from == K::AwaitingPermission && to == K::ExecutingTool)      return true;
    // ExecutingTool→流式传输:工具完成，恢复打开数据流。
    if (from == K::ExecutingTool      && to == K::Streaming)          return true;
    return false;
}

// ── 过渡职能(每个法律边缘一个) ───────────────────────
// 每个通过值-move获取源并返回目的地。
// 在状态更新函数站点逐步采用这些意味着非法转换（e.g.
// 空闲→ExecutingTool)是编译错误—
// —不存在具有该签名的函数。

// 空闲→流式传输:品牌-new转，新鲜活跃上下文。
[[nodiscard]] inline Streaming start(Idle&&, Active a) noexcept {
    return Streaming{std::move(a)};
}

// 流式传输→AwaitingPermission:tool_use到达，询问用户。
// Ctx流不变(取消、重试，时间戳保留)。
[[nodiscard]] inline AwaitingPermission land_perm(Streaming&& s) noexcept {
    return AwaitingPermission{std::move(s.ctx)};
}

// AwaitingPermission→ExecutingTool：用户批准。
[[nodiscard]] inline ExecutingTool exec_tool(AwaitingPermission&& p) noexcept {
    return ExecutingTool{std::move(p.ctx)};
}

// ExecutingTool→流式传输:工具完成，恢复打开数据流。
[[nodiscard]] inline Streaming resume_stream(ExecutingTool&& e) noexcept {
    return Streaming{std::move(e.ctx)};
}

// 流式传输→空闲：自然完成(StreamFinished)或终端错误。
[[nodiscard]] inline Idle finish(Streaming&&) noexcept { return Idle{}; }

// AwaitingPermission→空闲：用户拒绝了工具提示词。
[[nodiscard]] inline Idle reject(AwaitingPermission&&) noexcept { return Idle{}; }

// ExecutingTool→空闲：工具最终失败并且不可能
// 恢复（非常罕见-状态更新函数更喜欢拒绝→流式传输→空闲，
// 因此模型获得tool_result且is_error = true）。
// 提供完整性以便中止路径具有正确的名称。
[[nodiscard]] inline Idle done_tool(ExecutingTool&&) noexcept { return Idle{}; }

// 中止-将任何活动阶段折叠回空闲状态。由 Esc /
// CancelStream / 终端-error 处理函数 使用，
// 它们不关心它们从哪个阶段中止。
[[nodiscard]] inline Idle abort(Streaming&&)          noexcept { return Idle{}; }
[[nodiscard]] inline Idle abort(AwaitingPermission&&) noexcept { return Idle{}; }
[[nodiscard]] inline Idle abort(ExecutingTool&&)      noexcept { return Idle{}; }

// ── 编译-time 转换图的证明 ────────────────────
namespace proofs {

// 上面的每个转换函数都具有正确的签名：源按值-move
// (右值引用)，目的地按值。这是使类型状态强制执行工作
// 的契约 - 如果没有右值源，旧的阶段值会保留下来，
// 并且类型状态 "erases" 仅按照惯例为 pre
// -transition 状态。
static_assert(std::is_same_v<decltype(start(std::declval<Idle&&>(),
                                            std::declval<Active>())),
                             Streaming>);
static_assert(std::is_same_v<decltype(land_perm(std::declval<Streaming&&>())),
                             AwaitingPermission>);
static_assert(std::is_same_v<decltype(exec_tool(std::declval<AwaitingPermission&&>())),
                             ExecutingTool>);
static_assert(std::is_same_v<decltype(resume_stream(std::declval<ExecutingTool&&>())),
                             Streaming>);
static_assert(std::is_same_v<decltype(finish(std::declval<Streaming&&>())),
                             Idle>);
static_assert(std::is_same_v<decltype(reject(std::declval<AwaitingPermission&&>())),
                             Idle>);
static_assert(std::is_same_v<decltype(done_tool(std::declval<ExecutingTool&&>())),
                             Idle>);

// 穷举扫描：对于每个(从，到)对，
// 合法性矩阵is_legal_transition
// ()与转移函数的存在一致。矩阵是规格；功能就是实现。
// 它们之间的漂移 = bug。
//
// 我们不能直接通过 SFINAE 询问 "does a function exist from X to Y"，
// 而不命名每一个，因此证明是合法边的并行 re-statement 和
// static_assert，它等于每个 存储单元 上的矩阵。
// 添加新阶段需要更新 两者 这个证明和 is_legal_transition，
// 镜像 tool/policy.hpp. 中的 spec::policy
// 证明
consteval bool transition_graph_matches_spec() {
    using K = PhaseKind;
    constexpr K kAll[] = {K::Idle, K::Streaming, K::AwaitingPermission, K::ExecutingTool};

    // Hand-rolled规格：在上面的转换函数中编码的
    // 相同边缘。正面列出（合法）——除了通用 "X → Idle"
    // 中止边缘之外，其他所有 存储单元 都是非法的。
    auto spec = [](K f, K t) {
        if (t == K::Idle) return true;
        if (f == K::Idle               && t == K::Streaming)          return true;
        if (f == K::Streaming          && t == K::AwaitingPermission) return true;
        if (f == K::AwaitingPermission && t == K::ExecutingTool)      return true;
        if (f == K::ExecutingTool      && t == K::Streaming)          return true;
        return false;
    };

    for (auto f : kAll)
        for (auto t : kAll)
            if (is_legal_transition(f, t) != spec(f, t)) return false;
    return true;
}
static_assert(transition_graph_matches_spec(),
              "is_legal_transition disagrees with the transition-function "
              "graph above — update both together when changing phases");

// 固定 close-set 大小，因此添加阶段需要更新下面的每个 存储
// 单元-by-cell 证明站点。
static_assert(std::variant_size_v<Phase> == 4,
              "Phase variant size changed — update PhaseKind, the "
              "transition functions, and is_legal_transition together");

// Named-edge抽查：比上面的循环更好地阅读并固定
// 人类-meaningful 不变量，以防矩阵失去理智。
static_assert( is_legal_transition(PhaseKind::Idle, PhaseKind::Streaming));
static_assert(!is_legal_transition(PhaseKind::Idle, PhaseKind::ExecutingTool),
              "Idle → ExecutingTool must skip through Streaming; the user "
              "hasn't sent a turn yet");
static_assert(!is_legal_transition(PhaseKind::Idle, PhaseKind::AwaitingPermission),
              "Idle → AwaitingPermission is impossible — there's no tool "
              "to authorise without a model turn first");
static_assert( is_legal_transition(PhaseKind::Streaming, PhaseKind::AwaitingPermission));
static_assert( is_legal_transition(PhaseKind::AwaitingPermission, PhaseKind::ExecutingTool));
static_assert( is_legal_transition(PhaseKind::ExecutingTool, PhaseKind::Streaming));
static_assert(!is_legal_transition(PhaseKind::ExecutingTool, PhaseKind::AwaitingPermission),
              "ExecutingTool → AwaitingPermission is impossible — a tool "
              "that needs a follow-up permission goes through Streaming first");
static_assert( is_legal_transition(PhaseKind::Streaming,          PhaseKind::Idle));
static_assert( is_legal_transition(PhaseKind::AwaitingPermission, PhaseKind::Idle));
static_assert( is_legal_transition(PhaseKind::ExecutingTool,      PhaseKind::Idle));

} // namespace proofs

} // namespace phase

struct StreamState {
    Phase phase = phase::Idle{};
    std::uint64_t retrieval_seq = 0;   // Active::awaiting_retrieval id 的来源
    std::chrono::steady_clock::time_point last_tick{};
    // 我们发送给模型的最近请求的 Wall-clock（稳定）—
    // —在 数据流 启动时标记。驱动空闲的 缓存-lapse
    // 压缩触发器（参见 should_compact_on_idle）：
    // 使深度前缀便宜的 提示词缓存 只能在空闲的 ~kCacheTtl
    // 中生存，因此一旦我们空闲了近那么长时间 和 前缀就很大，
    // 我们压缩 当前 （虽然摘要仍然是便宜的 缓存 命中），
    // 而不是让下一个用户消息以新的输入速率 re-price
    // 整个前缀缓存 失效后。零直到会话的第一个请求。
    std::chrono::steady_clock::time_point last_wire_at{};
    // 当创作者最后请求提供者预热并且最后探测OAuth令牌
    // 是否需要主动刷新时。限制，因此 key-repeat
    // 无法向拨号或令牌端点发送垃圾邮件。他们住在函数
    // -local静态里面更新，直到Elm清理：声明
    // 状态更新函数读取和写入属于Model，或者重放和一个进程
    // （测试）中的第二个应用程序实例默默地共享它。
    std::chrono::steady_clock::time_point last_prewarm_at{};
    std::chrono::steady_clock::time_point last_refresh_probe_at{};
    int tokens_in   = 0;
    int tokens_out  = 0;
    // Reasoning/thinking代币模型在THIS轮的回复上花费了，
    // 如提供商所报告的（OpenAI
    // completion_tokens_details/output_tokens_details.reasoning_tokens)。
    // INCLUDED 已经在 tokens_out 中 - 单独保存只是为了
    // 在状态栏中显示故障。 0 当 传输 不报告时（Anthropic、
    // Ollama、非 -reasoning 型号）。每回合替换（不累积）如
    // tokens_out； 传输 值是每个 -request 的总数。
    int reasoning_tokens = 0;
    // 提示词-cache遥测用于LAST完成的定价（每回合）。
    // cache_hit_ratio = cache_read / (输入 +
    // cache_read + cache_creation) — 1.0 表示
    // 整个前缀是从 缓存 提供的（便宜 + 快速 TTFT），~0 表示前缀是
    // re-priced 冷（对于大上下文是 10-20x TTFT）。
    // 会话内部回合的 LOW 比率是一个 bug 信号：某些东西（工具-catalog
    // 重新排序、MCP 重新加载、提示词 不确定性）改变了缓存的前缀并烧毁了 缓存。
    // 出现在`perf`频道(AGENTTY_LOG=perf=debug)； -1 = 尚无数据。
    double cache_hit_ratio = -1.0;
    int    cache_read_tokens     = 0;
    int    cache_creation_tokens = 0;

    // 每当当前的线程被交换为另一个时被调用。
    // 使用编号属于生成它们的 线程； `prompt` 是新 线
    // 程 的最后报告的 提示词（0 表示新的）。
    void reset_usage_for_thread(int prompt) noexcept {
        tokens_in             = prompt;
        tokens_out            = 0;
        reasoning_tokens      = 0;
        cache_hit_ratio       = -1.0;
        cache_read_tokens     = 0;
        cache_creation_tokens = 0;
    }
    int context_max = 200000;
    // Self-calibrating对本地字节-based令牌估计的修正。
    //
    // estimate_wire_tokens()
    // 是由主动auto-compaction触发器使用的粗略bytes/3.5猜测。
    // 在 tool-heavy 上，它的 OVERSHOOTS 成绩很糟糕（JSON
    // 信封 + 工具输出 tokenize 远比 3.5 bytes/token
    // 更密集），它用于在数万个 token 的净空空间仍然空闲的情况下进行
    // 压缩 - 估计显示 "183k"，而模型报告的真实前缀只有 ~120k。
    //
    // 在每次真正的转弯之后，我们知道BOTH、
    // 地面-truth前缀大小(`tokens_in`来自
    // StreamUsage)以及估计器对于相同的传输视图会说什么。
    // 它们的比率（真实/估计）是一个实时校正因子：触发器将
    // 原始估计乘以它，以便主动检查跟踪实际的分词器而不是固定常数。
    // 每回合以 1.0（原始估计）和 EMA-smoothed 播种，
    // 因此单个异常回合无法摆动它。固定在正常的带上，
    // 因此损坏的读数不会禁用安全触发器。
    double est_calibration = 1.0;

    // 在 auto-compaction 触发之前我们填充的窗口的一部分。
    // 我们使用 DEEP — 95 % — 尽可能多地保留原始上下文，
    // 并且仅在真正接近天花板时进行总结：200 k 窗口在 190 k 时触发，
    // 1 M 窗口在 950 k 时触发。
    //
    // 为什么深ALSO便宜（计数器-intuitive部分）：重复的前缀
    // 是从提示词缓存提供的，而不是在全输入速率下的re-priced。
    // 在 Anthropic 上，稳定的 1-hour 锚点断
    // 点（wire_body.cpp，每约 20 条消息前进一次），
    // 在 OpenAI 上，固定的 prompt_cache_key 将对话
    // 前缀保持为跨轮的 CACHE HIT — 缓存-read 约为新
    // 输入的 10%。因此，大的实时前缀每回合的成本很低。真正昂贵的 事件
    // 是 COMPACTION（它移动锚 → 下一个回合是完整的 缓存，
    // 错过 和，它运行摘要请求），因此触发 LESS OFTEN 是保持总代
    // 币消耗较低的原因。 1 M 窗口的 95 % 而不是 25 % 意味着
    // 昂贵的 缓存-reset+总结 事件 的数量减少了大约 4 倍。
    // 深度触发 + 有界摘要切片 (
    // wire_messages_for_compaction) = 最大上下文 和 最小消耗。
    static constexpr int kSoftFillPct = 95;
    // 输出余量的绝对下限（以令牌为单位），无论百分比如何，
    // 必须 都保持空闲，因此下一回合的回复始终有空间。
    // 在小窗口上，95%可能留下太少的内容而无法得到完整的答案；
    // 在 1 M 窗口上，5 % 已经是 50 k — 远远超过任何单个回复的
    // 需求 — 因此有效阈值是 min（窗口的 95 %，窗口 - 楼层）。
    static constexpr int kMinOutputHeadroom = 20000;

    // 后台 auto-compaction 应当触发的令牌计数：
    // min(kSoftFillPct% of context_max, context_max
    // - kMinOutputHeadroom)，所以我们深入但总是为回复留出空间。
    // 0 = 窗口未知 → 从不触发。
    [[nodiscard]] int compaction_threshold() const noexcept {
        if (context_max <= 0) return 0;
        const long soft =
            static_cast<long>(context_max) * kSoftFillPct / 100;
        const long reserve_cap =
            static_cast<long>(context_max) - kMinOutputHeadroom;
        long thr = soft < reserve_cap ? soft : reserve_cap;
        if (thr < 0) thr = 0;
        return static_cast<int>(thr);
    }

    // ── 闲置 缓存-lapse 预览 -compaction ──────────────────────────────────
    // 深入（95%)WOULD使价格飙升的一种情况是：使大型实时前缀
    // 变得便宜的提示词缓存仅在有限的空闲窗口中存活（Anthropic
    // 的扩展TTL是1小时；OpenAI的提示词-cache重用类似地衰减）。
    // 骑行到 950 k，走过 TTL，然后您的下一条消息 re
    // -prices 以 完整 输入速率全部为 950 k — 正是
    // 用户永远不想要的 "price went up"。
    //
    // 修复：空闲时，如果前缀足够大以至于冷的re-price会伤害AND，
    // 我们正在接近缓存 TTL、紧凑的PRE-EMPTIVELY。
    // 摘要请求现在运行，而前缀是STILL，是温暖的缓存命中
    // （廉价），并且用户返回到小的温暖上下文而不是巨大的冷上下文。
    // 网：昂贵的冷re-price永远不会发生。
    //
    // 有界，因此它在 活动 工作期间永远不会提前触发（该路径是 95%
    // 阈值）：它需要真正大的前缀 (kIdleCompactMinTokens)
    // 和近 -TTL 空闲间隙。
    static constexpr int kIdleCompactMinTokens = 200000;
    static constexpr std::chrono::seconds kCacheTtl{3600};       // 1 h
    // 在 TTL 的 80% 处触发，以便摘要请求完成并在旧
    // 缓存 失效之前重新-warms（现在很小）前缀。
    static constexpr std::chrono::seconds kIdleCompactAfter{2880}; // 48分钟

    // 我们是否应该预先-emptively紧凑，因为我们已经闲置了
    // 足够长的时间以至于前缀缓存即将失效并且冷的re-price隐现？
    // `est_prefix_tokens`是当前转录本的校准传输估计。
    // 当idle-timing未知（还没有请求）或前缀很小（<
    // 200k的冷re-price不值得进行总结回合）时，
    // 返回false。
    [[nodiscard]] bool should_compact_on_idle(
        std::chrono::steady_clock::time_point now,
        int est_prefix_tokens) const noexcept {
        if (last_wire_at.time_since_epoch().count() == 0) return false;
        if (est_prefix_tokens < kIdleCompactMinTokens) return false;
        return (now - last_wire_at) >= kIdleCompactAfter;
    }
    // 当压实轮在飞行中时确实如此：包括合成的"summarise per spec"
    // 提示词的请求已被调度并且助手是流式传输，其摘要到下面的off
    // -transcript `compaction_buffer`中。
    // StreamFinished 的压缩分支读取此 标志，
    // 从 缓冲区 中提取摘要，推送描述 [0,
    // compaction_target_index) 前缀的
    // Thread::CompactionRecord，并重置 标志。
    // 压缩过程中的StreamError清除标志并丢弃缓冲区；
    // `messages` 在 EITHER 结果中未受影响。 不变量：
    // 仅当阶段!=空闲时才为真（转换与
    // 压缩数据流的启动配对）。
    bool compacting = false;
    // 智能模式CASCADE反馈（会话-scoped，
    // 不保留）。复杂性启发式预先设置每个回合的努力 FLOOR；
    // 这种有符号偏差，在努力-steps中，是在其之上的环路
    // 修正——路由研究比纯粹的前期路由更喜欢的级联。它是根据协调器
    // 的 实际 行为在 finalize_turn 进行调整的：
    // 重委托在-rated下的启发式转弯上将其提升；对 over-rated
    // 转弯的简单 no-delegation 回复会将其推低。每回合它都会朝着
    // 0 方向 DECAYS，因此单个异常永远不会粘住。固定在 [-2,
    // +2]（对称：持续的干净转身可以放松努力，只要持续的硬转身可以提高努力）。
    // 由 build_smart_routing_card / launch_stream
    // 阅读。
    int smart_effort_bias = 0;
    // 分类器分配给 IN-FLIGHT 回合的复杂性，在启动时隐藏，
    // 因此 finalize_turn 可以将其与模型实际执行的操作（委托计数）
    // 进行比较并更新 smart_effort_bias。
    smart::Complexity smart_turn_complexity = smart::Complexity::Standard;
    // IN-FLIGHT轮实际被调度的模型+角色。一旦角色确定，
    // 由 launch_stream 撰写，在助理消息稳定后阅读，
    // 以便 头文件 可以说出其真正的作者（参见
    // Message::served_model）。
    //
    // MODEL针对每个实时转弯（无论是否路由）进行设置：在
    // 智能模式关闭的情况下，它只是选择，这仍然是关于THIS转弯的事实。
    // （它曾经是空的，所以一个简单的会话不会标记任何内容，
    // 并且切换模型会追溯性地重新标记成绩单 - #120。）
    // ROLE 保持空，除非智能模式实际上路由了转弯。
    // 当回合结束时两者都被清除，因此状态标记在
    // 空闲时回落到实时选择。
    ModelId                         smart_turn_model;
    std::optional<smart::ModelRole> smart_turn_role;
    // in-flight压实正在产生哪种概要形状。
    // 设置在 CompactContext/fork 启动，
    // 由 传输 构建者读取以选择摘要 提示词。默认为可恢复（原始行为）。
    CompactionStyle compaction_style = CompactionStyle::Recoverable;
    // `messages.size()`的快照在CompactContext开球时进行。
    // 压缩-finalize 处理函数将其作为`up_to_index`
    // 标记到新的Thread::CompactionRecord上，
    // 以便未来请求的传输 载荷确切地知道摘要替换哪个前缀。
    // 完成后重置为 0（成功 OR 错误）。
    //
    // 当合成摘要提示词+辅助占位符被附加到`messages`
    // 并且终结器需要恢复前-synth切片时，
    // 这过去被称为`compact_pre_synth_count`。
    // 压缩不再触及 `messages`，
    // 因此该字段唯一剩下的作用就是记住边界；
    // 现在的名字反映了这一点。
    std::size_t compaction_target_index = 0;
    // Off-transcript接收器用于压缩数据流的文本增量。
    // 每当 `compacting` 为真时，数据流 处理函数 将
    // `StreamTextDelta` 重定向到该 缓冲区 而不是
    // `messages.back().pending_stream`，
    // 因此摘要答复绝不会污染用户-visible 记录。在 StreamFinished
    // 处耗尽 + 清除（成功时：解析为新的
    // CompactionRecord）或 StreamError（失败时：丢弃）。
    std::string compaction_buffer;
    // 压缩切片的自适应收缩上限，在TOKENS。 0 = 使用默认值（最小值
    // （context_max 的 65%，kCompactionSliceCap））。
    // 当压缩请求返回 "prompt is too long: X > Y" 时，
    // StreamError 处理函数 设置 下方 Y 和 重试
    // — 因此远大于窗口的分叉或 /compact 副本会不断
    // 修剪（247k → 适合）而不是失败。压缩结束时重置为 0。
    int compaction_ceiling = 0;
    // Rapid-refill断路器。当 auto-compaction
    // 在前一辅助轮的 `kRapidRefillTurns` 辅助轮内触发时，
    // `recent_compacts` 递增；在更安静的循环中它会重置。
    // 交叉 `kRapidRefillCount` 翻转 `autocompact_disabled`，
    // 因此我们停止颠簸——用户看到状态提示，表明他们减少了 Too-large
    // 工具输出。镜像 Claude 代码的 `LC7=3 / qP8=3 / VC7=3`
    // 三元组（二进制程序 靠近偏移 112623088）。
    int recent_compacts = 0;
    int turns_since_last_compact = 1000000;   // 启动时有效为 ∞
    bool autocompact_disabled = false;
    // 确实在`init()`启动后台OAuth刷新和`TokenRefreshed`
    // 登陆之间。设置后，`submit_message` 将用户的文本
    // 排队到 `composer.queued`，而不是分派 数据流
    // — Deps 仍然保留 pre-refresh（已过期）身份验证 头文件，
    // 现在触发的请求将返回 401。一旦新的信用生效，
    // TokenRefreshed 处理函数 就会清除 标志 并排空队列。
    bool oauth_refresh_in_flight = false;
    // 确实，从`init()`启动的后台线程-history
    // 加载仍在运行。 线程 选择面板 视图向 渲染
    // 查询 "loading…" 占位符而不是空列表。
    // 由 `ThreadsLoaded` 处理函数 清零。
    bool threads_loading = false;
    // Codex/Claude代码会话扫描正在运行。
    bool foreign_loading = false;
    // 填充旧线程'选择面板摘要的后台批处理正在运行。
    bool summaries_filling = false;
    // 针对该 线程 的标题请求已发出（参见 generate_thread_title)。
    ThreadId titling;
    // 当背景单曲 -thread JSON 加载 正在飞行时确实如此（由
    // `ThreadListSelect` 启动）。
    // 状态栏显示"loading thread…" 标签；之前的 线程 保持可见，
    // 直到 `ThreadLoaded` 着陆并交换 `m.d.current`。
    // 抑制额外的 ThreadListSelect 调度，因此混合的
    // Enter 无法将多个 加载 排列在彼此后面。
    bool thread_loading = false;
    // in-flight 加载是针对哪个线程的。当其他内容首先
    // 替换成绩单 (^N) 时清除，因此较晚的结果将被丢弃。
    ThreadId loading_thread;
    // 真实而背景模型-listfetch 正在进行中（由init(),
    // OpenModels、提供商切换或登录）。
    // 模型选择器视图参考此进行渲染"Loading models…" vs.
    // "No models available"——没有它，
    // 抛出或返回空列表的提取将使选择面板永远卡在加载动画上（
    // available_models列表保持为空）。 始终 由
    // `ModelsLoaded` 处理函数 清除，
    // fetch_models() 在 两者 成功和失败时调度。
    bool models_loading = false;
    // 为什么活动提供者的最后模型获取失败或者为空。保留在这里是
    // 因为在 选择面板 构建其 per-provider 目录
    // 之前获取可以落地。
    std::string models_error;

    // ── 自行-update信号 ──────────────────────────────────
    // 由UpdateCheckDone填充(后台释放检查)。 Non-empty
    // update_latest ⇒ 状态栏显示不显眼的 "⬆ vX.Y.Z" 标签
    // 和调色板表面 "Update agentty"。下载运行时，
    // update_in_flight 从调色板中门控 double-launch。
    std::string update_latest;    // "" = 最新/未检查
    std::string update_url;       // 吐司发布页面
    bool        update_in_flight = false;
    // 一旦将self-update设置为WRITTEN TO DISK。
    // 正在运行的进程仍然是旧映像 - 经典 Unix：新的 二进制程序
    // 位于同一路径，该进程保留其启动的 inode - 因此用户必须重新
    // 启动才能获取它。
    //
    // 这曾经在状态行中说过一次，长-running会话在几秒钟内滚动过去。
    // 然后 "⬆ vX.Y.Z" 标签 也被清除了，
    // 所以唯一发生更新的证据就是一条已经消失的消息。
    // 人们继续运行旧的 二进制程序 几天，
    // 并报告了已经修复的错误。
    //
    // 将版本保留在这里可以让状态栏显示持久化
    // "↺ restart for vX.Y.Z" 标签：信号一直存在，
    // 直到它要求的事情实际发生为止。
    std::string update_pending_restart;   // ""=没有上演任何内容
    // 当最后一次发布检查运行时。零 = 启动检查拥有第一个间隔。
    // 在 Tick 状态更新函数 中驱动每小时的 re-poll，
    // 因此 long-lived 会话会注意到释放而无需重新启动。
    std::chrono::steady_clock::time_point last_update_poll{};

    std::string status;
    // `status`的可选期满。设置后，一旦 now() 通过此点，
    // 状态栏就会隐藏横幅，并且 状态更新函数 将该字段视为空。用于 toast
    // -style 瞬时消息（重试、已取消、
    // 检查点-restore-not-implemented，...），
    // 因此它们不会永远徘徊。 default-constructed
    // time_point (epoch=0) 表示 "no expiry" — 状态
    // 将保持不变，直到有其他内容覆盖它。存在于 StreamState（不是
    // phase::Active）上，因为 终端-error 和 取消 处理函数 将其
    // 设置为 WHILE 转换为空闲，因此 Toast 必须比 ctx 存在得更久。
    std::chrono::steady_clock::time_point status_until{};

    // 当且仅当设置了`status`且AND没有到期或尚未到期时为真。
    [[nodiscard]] bool status_active(std::chrono::steady_clock::time_point now) const noexcept {
        if (status.empty()) return false;
        if (status_until.time_since_epoch().count() == 0) return true;
        return now < status_until;
    }
    maya::Spinner<maya::SpinnerStyle::Dots> spinner{};

    // Sparkline环缓冲区用于status-bar趋势字形。
    // 不 在 sub-turns 之间或跨活动→空闲边界重
    // 置 — 用户-visible 整个会话中生成率的跟踪。
    // 16个样本×500毫秒节奏≈8秒历史：小的固定
    // 宽度状态-bar迷你图（紧凑的趋势一瞥，
    // 而不是宽图）显示了它可以适合的最新后缀。
    static constexpr std::size_t kRateSamples = 16;
    std::array<float, kRateSamples> rate_history{};
    std::size_t rate_history_pos = 0;
    bool rate_history_full = false;

    // Spring-smoothed版本的BIG显示tok/s读数。
    // 原始瞬时速率（live_delta_bytes/4 ÷ 经过时间）每隔 帧
    // 就会重新计算一次，并且剧烈抖动 — 数字明显闪烁。 Tick 状态更新函数
    // 将此 Motion 重新定位为原始值； VIEW 读取 .get()，
    // 其中 self-ticks 与共享 帧 时钟相对应，并在滑行时 re
    // -requests 帧 — 无 hand-fed dt，并且滑行以 渲染
    // 节奏（平滑所属的位置）前进，而不是以较粗的 Tick 节奏前进。
    maya::anim::Motion<double> disp_rate =
        maya::anim::Motion<double>::spring(0.0, maya::anim::spring_presets::gentle);

    // ── 阶段上游 ─────────────────────────────────────────────
    [[nodiscard]] bool is_idle()                const noexcept { return std::holds_alternative<phase::Idle>(phase); }
    [[nodiscard]] bool is_streaming()           const noexcept { return std::holds_alternative<phase::Streaming>(phase); }
    [[nodiscard]] bool is_awaiting_permission() const noexcept { return std::holds_alternative<phase::AwaitingPermission>(phase); }
    [[nodiscard]] bool is_executing_tool()      const noexcept { return std::holds_alternative<phase::ExecutingTool>(phase); }

    // 导出："is anything actively in flight?"
    // ——每当会话处于任何非-Idle阶段时为真。曾经是一个并行的
    // `bool active` 字段，调用者必须将其保留在 锁-step 中；
    // 派生它消除了类型系统无法强制执行的 不变量 (
    // "active iff phase != Idle")。
    [[nodiscard]] bool active() const noexcept { return !is_idle(); }

    // 重试-state 谓词 — 通读 active_ctx()，
    // 这样它们就可以安全地从 Idle 中获取 调用（返回值相当于
    // "no retry pending,"，这也是调用者对 Idle 的期望：
    // 当没有 数据流 停止时，看门狗 门 `if (in_fresh()) check_for_stall()`
    // 正确地跳过）。
    [[nodiscard]] bool in_fresh() const noexcept {
        auto* c = active_ctx(phase);
        return !c || std::holds_alternative<retry::Fresh>(c->retry);
    }
    [[nodiscard]] bool in_stall_fired() const noexcept {
        auto* c = active_ctx(phase);
        return c && std::holds_alternative<retry::StallFired>(c->retry);
    }
    [[nodiscard]] bool in_scheduled() const noexcept {
        auto* c = active_ctx(phase);
        return c && std::holds_alternative<retry::Scheduled>(c->retry);
    }
};

} // namespace agentty
