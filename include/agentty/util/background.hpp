#pragma once
// agentty::util::background — fire-and-forget 工作仍然是 OWNED。
//
// 该 PRIMITIVE IS JAAL。 该 POLICY IS 此处。
//
// maya::pool是能力：作业在其可能永远不会返回时获得
// 其自己的线程，每个正文被包装，因此异常无法到达std::
// terminate，每个作业获得std::stop_token，
// 并且关闭是BOUNDED——隔离的作业在其线程存在之前被计数，
// 在宽限内等待，然后安全地放弃（池的状态是co-owned 由每个
// 工作线程 生成，因此最终返回的废弃作业仅涉及它仍然拥有的 内存）。
//
// 这个头文件增加了jaal无法知道的两件事：agentty具有
// ONE背景池，通过agentty的拆解注册表加入；
// 以及 per-job `where` 标签，使报告的投掷可追踪。
//
// ── 换成什么,它正在做的声称 ──────────────────────
//
// 这个文件曾经负责-roll整个事情，它自己的注释是错误报告：
// run_isolated_detached承诺了正文
// 通过回收线程等待自身结束，避免泄漏无法等待回收的操作系统线程句柄。
// 并且实现是`std::thread(...).detach()`。
// 没有收割者。旧代码实际上拥有的是异常隔离（真实的，并保留在下面），
// 没有其他任何东西：没有 stop_token，所以不能要求 工作线程 停止；
// 没有加入，所以在退出时这些 线程 仍在运行，而 CRT
// 破坏了它们下面的静态；没有限制，所以 "shutdown" 是 竞态，
// 没有人计时。
//
// 池保持隔离并供给三个缺失的半部。
//
// ── 选择形状 ────────────────────────────────────────────────────
//
//   在此 帧 返回 maya::scope 之前必须完成的工作（托儿所；
//                                                     [&]捕获是安全的，
//                                                     每个助手都加入）比
//   帧 寿命更长的工作，结果转到 Cmd::task / task_isolated
//     状态更新函数（留在 Msg 接口边界 上）比 帧 寿命更长的工作，
//   没有结果run_isolated_detached，
//                                                     如下
//
// OWNED、加入-on-destruction、线程这个头文件用来导出的没了：
// 它没有调用者，并且 maya::scope 正确地覆盖了该形状。
//
// ── 电话 正文 规则 ──────────────────────────────────────────────────────────────
//
// jaal的：作业正文是CAPTURELESS、`body(stop_token, args...)`，
// 并且每个参数都是可发送的。声明与其所有者的作业共享作为
// maya::co_owned<T> 传入，这要求 T 同步。所以一个工作
// 不能借用 `this` 或本地的并且比它更长久；这曾经是这里的评论。
//
// 正文可以获取`const maya::unlocked&` FIRST并获得
// 新的：作业在不持有锁的池 线程上开始，因此这就是后台工作获得获取锁 (
// agentty/util/lock_levels.hpp)的权利的地方。

#include <chrono>
#include <concepts>
#include <exception>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>

#include <maya/runtime.hpp>

#include "agentty/util/dbglog.hpp"
#include "agentty/util/lock_levels.hpp"
#include "agentty/util/teardown.hpp"

namespace agentty::util {

// 一个背景池，在第一次使用时创建并同时注册关闭——规则
// teardown.hpp规定：在CREATES和线程的函数中
// 注册加入，而不是在main()中注册。
//
// Process-lifetime，因此不需要teardown::
// cancel()(注册表记录的函数-local静态的豁免)。
//
// 池-level错误挂钩是LAST-RESORT网络。
// 下面的每个作业都有自己的 try/catch，
// 因此报告会命名该作业；任何过去的事情都在这里没有报告，
// 这仍然比进程死亡要好。
inline maya::pool& background_pool() {
    static maya::pool* const p = [] {
        static maya::pool inst{
            /*max_workers=*/0u,
            [](std::exception_ptr) {
                dbglog("util.background_pool",
                       "a job escaped its own handler (isolated — process "
                       "survives)");
            }};
        teardown::on_shutdown("util.background_pool", [](const maya::unlocked& cap) {
            // 受池的恩典约束。任何仍然卡住的东西都会被遗弃，
            // 这在结构上是安全的：工作线程 co-owns它所接触的状态，
            // 所以它不能比它更长久。
            if (const std::size_t stuck = inst.shutdown(cap); stuck > 0)
                dbglog("util.background_pool",
                       "abandoned " + std::to_string(stuck)
                           + " job(s) still running at the grace deadline");
        });
        return &inst;
    }();
    return *p;
}

namespace detail {

// 作业的标签，用于抛出时的报告。复制了 string_view、
// 不：每个 调用 站点都会传递一个文字。
struct Where { std::string_view name; };

// 调用是一项工作正文，如果它需要一个工作，则向其提供新的`unlocked`。
template <class Body, class... Args>
decltype(auto) call_body(std::stop_token st, Args&... args) {
    if constexpr (std::is_invocable_v<Body&, const maya::unlocked&, std::stop_token, Args...>) {
        return maya::thread_root([&](const maya::unlocked& cap) -> decltype(auto) {
            return Body{}(cap, std::move(st), std::move(args)...);
        });
    } else {
        return Body{}(std::move(st), std::move(args)...);
    }
}

// 运行正文，没有漏出任何东西。 正文 是无捕获 lambda 类型，
// 因此 正文{} 是相同的函数。
template <class Body, class... Args>
void run_guarded(std::stop_token st, Where where, Args... args) noexcept {
    try {
        (void)call_body<Body, Args...>(std::move(st), args...);
    } catch (const std::exception& e) {
        dbglog(where.name, e.what());
    } catch (...) {
        dbglog(where.name, "non-std exception (isolated \xe2\x80\x94 process survives)");
    }
}

}  // namespace detail
} // namespace agentty::util

// 对字符串文字的看法：静态存储，没有任何消亡。
MAYA_SENDABLE(agentty::util::detail::Where);

namespace agentty::util {

// 作业正文:无捕获，`body([const maya::unlocked&,] stop_token, args...)`，
// args可发送。
template <class Body, class... Args>
concept JobBody = maya::CheckedJob<Body, Args...>;

template <class Body, class... Args>
consteval void require_job() { maya::require_job<Body, Args...>(); }

// WARM 工作线程上的Fire-and-forget。对于将完成的有界工作：
// 文件系统遍历、解析、缓存刷新不能永远阻塞。
//
// 当保证 正文 返回时，优先选择此而不是 run_isolated_detached。
// 它重用了池化的 线程，因此突发需要队列推送和通知，
// 而不是每次生成 线程。
//
// 错误的代价：正文永远阻塞这里的OCCUPIES，
// 其中池的工作线程永久阻塞。如果它可能楔入，那就是孤立的工作。
template <class Body, class... Args>
void run_background(std::string_view where, Body, Args... args) {
    require_job<Body, Args...>();
    background_pool().post(&detail::run_guarded<Body, Args...>,
                           detail::Where{where}, std::move(args)...);
}

// Fire-and-forget，但是SAFELY:`body`
// 在其自己的线程上运行，不能通过抛出来关闭该进程，
// 被要求在关闭时停止，并且在被放弃之前在关闭宽限内等待。
//
// 隔离(专用线程)而不是排队，因为这里的呼叫者是MAY 绝不
// RETURN的作业：ACP转，网络拨号。这些不得占用共享的 工作线程。
// 如果 正文 有界，则使用上面的 run_background。
// 在令牌上使用maya::delay_for而不是在关机期间休眠。
template <class Body, class... Args>
void run_isolated_detached(std::string_view where, Body, Args... args) {
    require_job<Body, Args...>();
    background_pool().post_isolated(&detail::run_guarded<Body, Args...>,
                                    detail::Where{where}, std::move(args)...);
}

// ── WorkerGroup - 背景工作与 一个 对象的 生命周期 ────────
//
// maya::worker_group (jaal)是原语：long
// -lived对象的作业，以及stop()，它是没有恩典的硬屏障。当作业抛出时，
// 这仅添加 `where` 标签。
//
// stop()等待作业，这可能需要调用者持有的任何锁，
// 因此需要`unlocked`：从锁 正文内部停止组不会编译。
// WorkerGroup 也加入破坏，因此它被标记为 joins_on_destroy，
// 并且 maya::guarded<T> 不能拥有一个（jaal
// 拒绝该类型）：它的所有者将其保留在每个 锁 之外，
// 并首先使用能力阻止它。
class WorkerGroup {
  public:
    /// `where`在日志中标记该组。静态存储，与上面的生成
    /// 函数相同的契约。
    explicit WorkerGroup(std::string_view where) : where_(where) {}

    /// 在其自己的线程上运行`body([unlocked,] stop_token, args...)`。
    /// 一旦组停止就丢弃。
    template <class Body, class... Args>
    void post(Body, Args... args) {
        require_job<Body, Args...>();
        group_.post(&detail::run_guarded<Body, Args...>,
                    detail::Where{where_}, std::move(args)...);
    }

    /// 屏障：当其返回时，此处发布的作业没有正在运行。
    /// 故意没有截止日期，所以悬而未决的工作就挂了这个。
    /// 为作业赋予自己的 超时。 幂等；析构函数也加入。
    void stop(const maya::unlocked& cap) noexcept { group_.stop(cap); }

  private:
    const std::string_view where_;
    maya::worker_group     group_;
};

} // namespace agentty::util

// ~WorkerGroup加入其工作：从不在maya::guarded<T>内。
MAYA_JOINS_ON_DESTROY(agentty::util::WorkerGroup);
// 一个const标签加上一个worker_group，这是Sync:很多线程可能会发帖。
MAYA_SYNC(agentty::util::WorkerGroup);
