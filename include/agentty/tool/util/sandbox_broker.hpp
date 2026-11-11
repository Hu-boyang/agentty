#pragma once
// agentty::tools::util::sandbox::broker——系统调用主管。
//
// 这就是将沙箱从FILTER变成CAPABILITY SYSTEM的原因。
// 过滤器回答"may you call ptrace"；主管在 运行时 处回答
// "may you ptrace THIS pid"，并提供论据。
//
// ── 为什么这个文件很小, 一定要保持小块 ──────────────────────────
//
// seccomp user-notification 是 Linux 沙箱隔离 API 中最
// 锋利的枪，内核文档直截了当地说：
// 必须明确：seccomp 通知机制不能据此实现安全策略。
// — 特别是关于 SECCOMP_USER_NOTIF_FLAG_CONTINUE。
// 两种危险，而且都是结构性的，而不是需要小心
// 的错误。
//
// 1.指针参数上的TOCTOU。 通知 携带系统调用的
//    REGISTER值。客户机中的指针参数名称为 内存，
//    而客户机还有其他 线程：它可以在我们读取它和内核执行
//    操作之间重写该 内存。因此，从客户机读取路径然后允许
//    调用 是一个漏洞，而不是一个功能。
//    Checkpoint/ 恢复和容器运行时已多次出现此错误。
//
// 2.CONTINUE不是决定。告诉内核恢复系统调用
//    经过我们检查，将-runs与GUEST的凭据和客人的内存的视图进行了比较。
//    那时检查的所有内容都是过期。
//
// claybin的经纪人拒绝暴露任一危险：决策是允许、
// 拒绝或inject_fd（我们完成了工作，访客获得描述符）。
// 请求中没有 `continue`，也没有 `path` 字段。
// 因此，这个文件只有一项工作——从 SCALARS 决定——当这里
// 的规则想要查看路径时，该规则就属于内锁，内核在根本不使用 竞态
// 的情况下强制执行该规则。
//
// ── 何谓经纪? FOR 这儿 ──────────────────────────────────────────
//
// 不是文件系统规则：landlock 已经做到了这些，而且更好。
// 经纪业务在 调用 中占有一席之地，其中标量决策确实有用，
// 而一揽子拒绝确实是错误的：
//
//   ptrace附加到其OWN子级的调试器是正常的（测试下的gdb、
//             lldb、strace）。依附于任何其他事物是一种逃避的形式。
//             目标 pid 是 SCALAR，因此这是可确定的。
//   kill信号表示自己的进程组正常；发信号 pid 1
//             或 沙箱 之外的东西不是。也是标量。
//
// 目前，两者都是 编译器 配置文件中的平面拒绝，这是安全的，但偶尔会出错 — `cargo test`
// 在调试器下，这是 strace 本身的测试套件。经纪将 "denied, good luck"
// 变成
// "denied, and the log says it tried to attach to pid 1"。（调用被拒绝，日志说明它曾尝试附加到 pid 1。）
//
// ── 审计线索是半点 ───────────────────────────────────
//
// 每个决定都被记录。 "Your build failed"什么也没教；
// "cargo tried ptrace(PTRACE_ATTACH, 1) and was denied"（cargo 尝试 ptrace(PTRACE_ATTACH, 1)，并被拒绝。）
// 告诉您工具链的作用，并且是添加一条 允许清单 行和关闭 沙箱
// 之间的区别。

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace agentty::tools::util::sandbox::broker {

// 我们做出的一项决定，供记录。
struct Event {
    std::string syscall;   // "ptrace"，"kill"
    std::string detail;    // "PTRACE_ATTACH pid=1"，"SIGKILL pid=1"
    bool allowed = false;
    std::uint32_t count = 1;   // 合并重复相同的决定
};

// 主管决定的内容，用 agentty 的话来说。故意NOT
// claybin的决定：这个头文件被工具层包含了，
// 它与知道claybin的类型无关，映射是.cpp中的一个开关。
enum class Verdict : std::uint8_t {
    Allow,   // 仅当仅由标量决定时才发出声音
    Deny,    // EPERM;总是声音
};

// 政策。纯：标量输入，判决输出，没有 I/O 也没有来宾 内存。
//
// `nr`是HOST架构的系统调用号，`args`是其逐字的六个寄存器参数，
// `guest_pid`是构成调用的线程。
//
// 纯有意为之，因此无需内核即可测试：sandbox_broker_test
// 用精确的参数形状驱动它，形成真正的ptrace/kill携带的，
// 这是检查有趣的情况（附加到pid 1，
// 组外信号）而不安排真正的逃逸尝试的唯一方法。
[[nodiscard]] Verdict decide(std::uint32_t nr, const std::uint64_t args[6],
                             std::uint32_t guest_pid, std::uint32_t guest_pgid,
                             Event* out_event);

// 哪个系统调用agentty请求claybin进行代理。经纪关闭时为空。
//
// 作为数据返回而不是就地应用，因此态势构建器保留在
// 改变策略的单个位置，并且因此测试可以断言该列表
// 而根本不需要构建策略。
[[nodiscard]] std::vector<std::uint32_t> brokered_syscalls();

// ── 被封锁的-activity 种子 ─────────────────────────────────────
//
// 沙箱实际上停止了什么，最近的最后一个。这是改变行为的表面：
// "your build failed" 没有教任何东西，
// 而 "cargo tried ptrace(PTRACE_ATTACH, 1) and was denied"
// 教你的工具链做什么——这就是添加一条 允许清单 行和关闭 沙
// 箱 之间的区别，这就是当 沙箱 不透明时人们实际上所做的。
//
// 它是在它存在之前设计的：日志已经有了决定，但没有任何东西
// 可以读回它们，所以这个 头文件 的早期版本声明了一个没有生产
// 者的 BlockedEvent 结构，我删除了它（
// 参见§9a——未实现的控件读取为附带功能）。经纪人是让它成为
// 现实的原因，因为经纪人是唯一知道发生了拒绝的地方 AT 该
// MOMENT 它发生了，而且参数仍在手中。
//
// ── 线程,这是整个设计限制 ────────────────────
//
// 从工具WORKER写入线程(经纪人在Cmd::
// task_isolated内决定)并从REDUCER读取线程(即面板 渲染)。
// 因此，这确实是跨-thread，与沙箱层中的几乎所有
// 其他东西不同，并且它是故意的互斥量而不是锁-free环：
// 争用是每个命令有少量的事件，并且这里棘手的数据
// 结构将优化错误的事物，同时添加该子系统已经付出
// 过一次代价的一类错误。
//
// 有界，那是安全属性而不是整齐性。否则，在循环中敲击
// 被拒绝的系统调用的来宾将会无限制地增长它——主管在
// 其自己的主机上造成拒绝-of-service。
// 相同的连续 事件 合并成一个计数，这也使提要可读：
// "ptrace ATTACH pid=1 × 4096"是一行字，也是事实。

// 记录一个决定。从任何 线程 到 调用 都是安全的。
//
// 尽管代理记录了两者，但仅记录了DENIALS。允许并不是
// 沙箱 阻止的活动，并且显示每个允许的系统调用的提要是一个
// strace，而不是安全表面 - 信号会被淹没。
void record(const Event& ev);

// 馈送，最新的最后，作为 快照。
//
// 按值：呼叫者在另一个线程上，因此回交引用将是具有下一个
// 拒绝的竞态。复制每个 面板 帧 的小结构的有界向量不值得
// shared_ptr 跳舞。
[[nodiscard]] std::vector<Event> blocked_feed();

// 忘记记录的一切。用于测试，以及 面板 中未来的 "clear"
// 可供性。
void clear_blocked_feed();

}  // namespace agentty::tools::util::sandbox::broker
