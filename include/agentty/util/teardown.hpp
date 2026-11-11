#pragma once
// agentty::util::teardown——进程-exit加入注册表。
//
// ── 设计缺陷就结束了 ──────────────────────────────────────────
//
// 多个子系统拥有后台线程。每个都必须在 CRT 运行静态析
// 构函数之前连接。今天，该义务由 main() 底部的
// 手 -written 列表履行：
//
//     join_workspace_prewarm();
//     join_workspace_symbols_prewarm();
//     persistence::flush_pending_saves();
//     provider::release_acp_agents();
//     ...
//
// 这样的列表是完全正确的，直到有人添加了一个子系统并忘记附加到它——这正是
// settings_cache 所发生的情况：它写了 `shutdown()`，
// 将其记录为 "called during teardown"，
// 并且从未添加过 调用。 工作线程 在函数 -local 静态中保持可连接，
// 并且 ~线程 在退出时触发 std::terminate。
//
// 缺陷在于，关于每个线程子系统，main()被询问到KNOW。
// 这是倒退的：main()是一个不知道子系统是什么的地方。颠倒它——每
// 个子系统在第一次启动 线程 时都会注册自己的关闭——使列表成为 self
// -maintaining，
// 并使 "I added a thread but forgot to join it" 对于构建在拥有
// 线程 的子系统上的任何东西都是不可能的。
//
// 排序：LIFO，同静态破坏。后来注册的子系统可能
// 依赖于先注册的子系统，因此它会先被拆除。
//
// 当没有注册时，`run()`是幂等并且对调用是安全的。
//
// ── 规则,对于任何开始 线程 ──────────────────────────
//
//     注册连接在CREATES的线程的函数中，
//     而不是在main()中。
//
// 将其包装在`std::call_once`中，这样重复启动寄存器一次，
// 并且仅当目标是进程-lifetime静态时才跳过`cancel()`（常见情况：
// 函数-local `Bg&`）。在其析构函数中任何较短的 -lived 必须 取消
// 或 run() 都会将 调用 放入已释放的 内存 中。
//
// 采用是部分的，并且在这里故意可见而不是暗示：
// settings_cache、modelsdev 和 blob_gc self
// -register； main() 中的其余连接（工作区 预热、符号 预热、
// 持久化 冲洗、ACP 药剂释放）仍然是手工拼写的，并且在触摸每个连接时
// 应移动到此处。 tests/teardown_registration_test.cpp
// 固定已移动的那些，因此它们不能默默地回归到旧的排列。

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include <maya/runtime.hpp>

namespace agentty::util::teardown {

// 关闭动作。它从 run() 运行，在 main 末尾调用，
// 没有 锁 持有，因此每个操作都会被交给 `unlocked`：它
// 可能会停止池和 工作线程 组，刷新，并按排名顺序采取任何 锁。
using Action = std::function<void(const maya::unlocked&)>;

// 在`name`下注册关闭动作（仅用于诊断）。
// 动作在精神上必须是 noexcept：任何异常都会被
// run() 吞噬，因为拆卸期间的抛出比它报告的失败更糟糕。
//
// 返回标识注册的TOKEN。 生命周期 比进程短的所有者必须
// 将其传递给析构函数中的 `cancel()` — 否则
// run() 将调用绑定到已释放的 内存 的 回调。真正永远存在
// 的调用者（function-local 静态，常见情况）可以
// 忽略令牌。
using Token = std::uint64_t;

Token on_shutdown(std::string name, Action action);

// 删除注册。 幂等，并且使用已运行操作的令牌是安全的。
// 这就是让具有有限 生命周期 的注册者保持安全的原因：
// 注册表 永远不会将 回调 保留在死对象中。
void cancel(Token token) noexcept;

// 运行每个注册的动作，首先运行最-recently-registered，
// 然后清除注册表。 幂等：第二个 调用 是 no-op，
// 除非在其间注册了新操作。从不抛出。需要`unlocked`：它加入。
void run(const maya::unlocked& cap) noexcept;

// 待处理有多少个动作。用于测试和诊断。
[[nodiscard]] std::size_t pending() noexcept;

} // namespace agentty::util::teardown
