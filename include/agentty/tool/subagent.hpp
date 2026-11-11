#pragma once
// agentty::tools::subagent-`task`工具的注入接口边界。
//
// 子代理循环需要传输 凭据+默认模型，其存在于运行时层(
// deps())。工具层不能依赖于运行时，因此启动时会在
// 这里安装一个小的配置blob； `task` 工具在执行
// 时读取它。如果未安装任何内容（测试，没有默认型号的 ACP），
// 该工具将返回明确的 "unavailable"
// 错误而不是崩溃。

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <stop_token>
#include <optional>
#include <string>

#include "agentty/util/io.hpp"

#include "agentty/auth/auth.hpp"
#include "agentty/domain/catalog.hpp"
#include "agentty/domain/smart_mode.hpp"
#include "agentty/provider/provider.hpp"
#include "agentty/util/callback.hpp"
#include "agentty/util/sendable.hpp"

namespace agentty::tools::subagent {

// 子代理循环的运行时-installed配置。当用户在会话期间
// 切换提供商、帐户和模型时，Auth/model 会刷新。
struct Config {
    auth::AuthHeader auth;       // 传输 凭据为子-stream
    std::string      model;      // sub-agent 转的型号 ID
    bool             installed = false;

    // 活动提供商的可用模型与`model`一起更新。
    // 子代理运行器将READ-ONLY角色(explorer/reviewer)
    // 路由到该列表中最便宜的有能力的模型(catalog::cheapest_capable_model)，
    // 因为这些角色对小型模型手柄和旗舰进行繁重的工作。空（或者没有更
    // 便宜的东西）⇒角色在 `model` 上运行，因此单个 -model
    // 提供者看不到任何变化。切勿路由交叉-provider。
    std::vector<ModelInfo> candidates;

    // Provider-agnostic 数据流 接口边界 — 相同 分派 main.cpp 安装到
    // Deps::stream（调用 时间在 provider::active() 上的路由：
    // Anthropic/OpenAI-compat/Ollama本机）。
    // 与身份验证一起安装，以便子代理与 USER 选择的任何后端进行对话，
    // 而不是对 Anthropic 传输层 进行硬编码 - 以前 `task`
    // 在每个非 -Anthropic 提供商上都失败（错误的 传输，错误的身份验证）。
    // Null ⇒ 运行程序回退到 Anthropic 传输层（旧行为，
    // 保持仅安装 auth+model 的测试正常工作）。在子代理的 工作线程 上运行，
    // 该子代理不包含 锁：它获取 工作线程 的 `unlocked`。
    ::agentty::util::Callback<provider::StreamResult(const maya::unlocked&,
                                          provider::Request,
                                          provider::EventSink)> stream;

    // 相同的分派，但是到NAMED提供商而不是活动提供商。
    //
    // 上面的`stream`在调用时间关闭于provider::active()，
    // 这对于在父代理运行的地方运行的子代理是正确的——并且使得cross
    // -provider角色难以表达。正是由于这个原因，固定在另一个端点上的智能模式
    // 插槽必须被删除（docs/design/ smart-mode.md §
    // 9 称为 "per-role auth threading"）。
    //
    // 单独的 接口边界 而不是 `stream` 上的默认参数，
    // 因为两者具有不同的故障模式：分派到活动提供者不能
    // 找不到一个，而分派到指定提供者则可以（注销、删除、
    // 拼写错误）。不安装此功能的调用者将获得旧行为 -
    // 角色会回退到活动提供者自己的最便宜的功能模型，
    // 而不是 404。在子代理的 工作线程 上运行：它
    // 获取 工作线程 的 `unlocked`。
    ::agentty::util::Callback<provider::StreamResult(const maya::unlocked&,
                                          const std::string& provider_id,
                                          provider::Request,
                                          provider::EventSink)> stream_to;

    // 智能模式配置，从 Model 镜像，因此当 smart.subagent_routing
    // () 开启时，子代理运行器可以通过 ROLE (第 3b 层)解析
    // 工作线程 的模型。 Off/unconfigured ⇒ 现有的 read-only
    // 层 auto-router 代表。通过 set_smart() 刷新。
    smart::RoleConfig smart;
    // 当前调度父回合的提供者。固定插槽模型仅对为其提供服务的端点有意义，
    // 因此当其记录的提供程序与此匹配时，角色解析器会重播引脚 仅
    // （请参阅 smart::SlotOverride::provider）。
    // 与 `smart` 一起镜像，因为 `task`
    // 在 工作线程 线程 上运行，无法访问 Model。
    // 空⇒未知，并且每个引脚都受到尊重（pre-existing 行为）。
    std::string provider;
};

// 安装子代理配置（调用 在启动时、身份验证解析后一次）。
void install(Config cfg);

// 仅更新子代理循环使用的身份验证头文件。每当登录、注销、
// 帐户切换或提供商切换更改 运行时 身份验证时调用。
// 空对于传输层本身解析凭据的本地和本机
// -OAuth提供者有效。
void set_auth(Io, auth::AuthHeader auth);

// 仅更新子代理循环使用的模型，而不干扰认证或安装的标志。
// 当用户切换模型 mid-session（模型 选择面板）时调用，
// 以便子代理跟踪实时模型而不是 过期 启动默认值。
// 如果从未安装过配置，则为 No-op。
void set_model(Io, std::string model);

// 更新路由器从中选择廉价角色的提供商的可用-models列表。
// 每当模型列表（重新）已加载 或提供程序发生更改时，
// 都会与 set_model 一起调用，因此路由始终反映实时提供程序。
void set_candidates(Io, std::vector<ModelInfo> candidates);

// 更新子代理路由器所遵循的智能模式角色配置（第3b层）。
// 每当智能模式或型号列表改变时，与set_candidates一起调用。
// 如果从未安装过配置，则为 No-op。
void set_smart(Io, smart::RoleConfig smart);

// 更新父回合运行的提供者，因此固定槽的范围保持在可以实际服务它们的端点。
// 每当活动提供商发生变化时，就会与 set_smart /
// set_candidates 一起推送。
void set_provider(Io, std::string provider);

// 快照已安装的配置。 `installed == false` 直到 install() 运行。
[[nodiscard]] Config current();

// 最大嵌套深度。子代理本身可能会生成子代理，但只能达到这个深度
// ——超过这个深度，`task` 工具就会拒绝，
// 从而防止失控的分叉炸弹/无限制的代币支出。深度0为top-level代理；
// 第一个子代理在深度 1 运行。
inline constexpr int kMaxDepth = 2;

// 在循环force-stops之前最大sub-agent转动（模型
// 完成）并返回其所具有的任何内容。限制令牌花费+ wall-clock，
// 因此循环子代理不能无限期地楔入父代理。
//
// 这是所有角色的 CEILING——存在的界限是为了阻止逃跑者，
// 而不是目标。下面的 Per-role 预算是实际适用的；
// 保持这个 >= 其中最大的一个。
inline constexpr int kMaxTurns = 80;

// ── 人数 -role 转帐预算 ──────────────────────────────────────
// 一个全局上限不可能适合每个角色，因为这些角色在
// 结构上执行不同的工作：
//
//   READ-ONLY角色（探索者、审阅者）是有界扫描：读取、
//   grep、映射、总结。他们在几个回合中汇聚在一起，大笔预算除了
//   象征性支出之外什么也买不到——一个仍在 40 回合的探险者迷路了，
//   不彻底。
//
//   WRITE角色（编码员、测试员、一般）是ITERATIVE：
//   读取代码、进行编辑、构建、读取错误、修复、重新-build、
//   运行测试、再次修复。一个诚实的实施周期很容易是 6-10 轮，
//   而一项真正的任务是几个周期。在 24 圈时，编码员通常将其全部
//   预算花在定向上，并报告 "I ran out of turns"
//   — 此尺寸调整旨在防止的失败。 编译-heavy 工作（
//   这里是 C++）是最坏的情况：每个构建都是一个回合，每个错误列表
//   都是另一个读取。
//
// 这些是 CAPS，不是配额：在 5 回合内完成的子代理停止于
// 5.提高未达到的上限不会花费任何费用，因此成本
// 慷慨的行为只能通过运行来支付，否则上限为
// FAILED — 这正是我们想要的交易。
inline constexpr int kMaxTurnsReadOnly = 32;
inline constexpr int kMaxTurnsWrite    = 80;

// 角色的预算，考虑到它是否可以修改工作区。
[[nodiscard]] inline constexpr int max_turns_for(bool read_only) noexcept {
    return read_only ? kMaxTurnsReadOnly : kMaxTurnsWrite;
}

static_assert(kMaxTurnsReadOnly <= kMaxTurns);
static_assert(kMaxTurnsWrite    <= kMaxTurns);

// ── 运行期限, 继承到整个巢穴 ────────────────────────────
//
// 一次子代理运行的wall-clock上限。它的存在是因为 kMaxTurns
// 限制了 COMPLETIONS，而不是时间：在技术上保持
// 数据流 活跃但未完成的后端对下面的每一层都是健康的。
//
// NESTED运行取其自身预算的最小值以及高于其之上的运行的剩余部分，
// 因此上限适用于整个源码树而不是每个级别。深度和继承的
// 最后期限旅行用工具调用（tools::CallContext::depth
// /最后期限），而不是线程。
using RunClock    = std::chrono::steady_clock;
using RunDeadline = std::optional<RunClock::time_point>;


// ── 运行-run 注册表, 用于有限度的关闭 ──────────────────────
//
// 子代理在 Cmd::task_isolated 线程 和 jaal DETACHES 上运行：
// kernel/pool.hpp明确表示分离的线程是
// "asked to stop but never waited for"。现在请求确实
// 到达了（run_tool 的 取消 探针读取其 stop_token），
// 但没有任何东西等待答案 - 因此 main() 可以在子代理仍在其提供者 数据流 内部时返回，
// 并且 数据流 将提供者对象 BY REFERENCE 保留在主堆栈之外。
//
// Zed不存在这个问题，因为子代理在`running_subagents`
// 列表中有一个拥有的`Entity<Thread>`，并且取消父轮次
// 等待每一个(`turn.cancel()`返回`Task<()>`)。
// 我们不能在不重写 Cmd 层的情况下采用 所有权 模型，
// 但我们可以采用 REGISTRY：每次实时运行都会发布 取消
// 标志 加上 "finished" 锁存器，拆卸会使它们全部失效，
// 并等待 线程 离开提供者 调用 的有限时刻。
//
// 有意有界，而非无限期：楔入的系统调用不得使进程
// 保持打开状态（jaal首先分离的原因）。
// 胜利在于常见情况——数据流 在几十毫秒内注意到
// 取消——不再是针对 main 展开的 竞态。

// Per-run手柄。下面的 `run()` 保存了整个子代理循环的一个。
class RunRegistration {
public:
    RunRegistration();
    ~RunRegistration();
    RunRegistration(const RunRegistration&)            = delete;
    RunRegistration& operator=(const RunRegistration&) = delete;

    // 关机是否要求此运行停止？循环在轮次之间轮询它。
    [[nodiscard]] bool cancelled() const noexcept;

    // 与真实std::stop_token相同的信号，
    // 因此数据流的取消可以骑乘stop_callback而不是轮询线程。
    // 如果由于关闭已开始而拒绝运行，则已停止。
    [[nodiscard]] std::stop_token token() const noexcept;

    // 每-run 标志。公开，因为触发它们的 注册表 位于 .
    // cpp 中并且必须命名该类型；外部没有任何东西可以构建一个。
    struct State;

private:
    std::shared_ptr<State> state_;
};

// Ask每次实时运行停止并等待至`grace`他们离开。
// 返回当宽限期满时有多少仍在运行（0=干净）。
// 幂等;安全，飞行中不会跑动。在第一次运行启动时向 util::teardown 注册，
// 因此 main() 不需要知道这一点。
std::size_t shutdown_running(const maya::unlocked&,
                             std::chrono::milliseconds grace
                                 = std::chrono::milliseconds(1500)) noexcept;

// 任务卡的透明标签的特工角色的姓名来源："builtin" (explorer/reviewer/…)、
// "user" (~/.agentty/agents)或"project"
// (骑在仓库上的工作区-local .agentty/agents)。
// 对于未知名称返回 "builtin"（安全默认值 — 不显示标签）。
// PROJECT特工的角色提示词是通过克隆攻击者-controllable，
// 因此UI将其暴露出来；它被 不 阻止（它的工具像其他工具一样保持封闭状态）。
[[nodiscard]] std::string_view agent_origin(Io, std::string_view name) noexcept;

} // namespace agentty::tools::subagent

// 数据流 接口边界是util::Callback:在安装时设置并调用，
// 之后从未改变；可调用对象捕获共享的 线程-safe 提供程序句柄 (
// main.cpp)。其余的都是拥有的数据，因此 jaal 遍历结构本身。
