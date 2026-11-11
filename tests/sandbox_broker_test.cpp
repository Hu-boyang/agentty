// 系统调用主管的决定政策。
//
// 单位 -testable 因为 `decide()` 纯度: 分级 判断 ,
// 不 I/O 也没有客人 内存 这正是它被塑造成这样的原因 -- -- 有趣
// 的案例是 "attach to pid 1" 财务报告和已审计财务报表
// "signal outside the group" ,并且将那些用于对内核进行排列,
// 意味着进行真正的逃跑尝试。它们各有三行。
//
// 这是什么? canNOT 检查是主管被窃听、接受调查并及时回答。
// 一个完美决定且从未被问到的政策是绞刑。 sandbox_live_check
// 工作(9-11号案件),两者加在一起,是使经纪活动安全
// 进行航运的原因.

#include <doctest/doctest.h>

// Linux-only 因为测试中的东西是,经纪是建立在 seccomp 用户
// -notify ,下文中的案件按 PTRACE_ * 请求编号来自
// <sys/ptrace.h> - Darwin的 ptrace 是另一个接口与
// PT_ *常数与无 PTRACE_TRACEME 完全是这样的 TU 没有 编译
// 那边
//
// 这个警卫是当 macOS 变成一个真实的 沙箱
// 目标:在文件从不建在mac上,而且破损是隐形的。
// 守卫而不是移植,因为没有 macOS 要测试的经纪人 -- --
// 安全带后端报告 `syscall.filter: none`
// 恰恰是因为这个平台上不存在这种机制。
#if defined(__linux__)

#include "agentty/tool/util/sandbox_broker.hpp"

#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <signal.h>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace br = agentty::tools::util::sandbox::broker;

namespace {

// 嘉宾:pid 4242,领导自己的流程组(P. claybin 调
// 用 所以孩子领导一个群体,每个后代继承它,
// 这就是 "inside my own group" 可从寄存器值中解析).
constexpr std::uint32_t kGuest = 4242;
constexpr std::uint32_t kGroup = 4242;

br::Verdict ptrace_call(std::uint64_t request, std::uint64_t target,
                        br::Event* ev = nullptr) {
    const std::uint64_t args[6] = {request, target, 0, 0, 0, 0};
    br::Event local;
    return br::decide(static_cast<std::uint32_t>(SYS_ptrace), args, kGuest,
                      kGroup, ev ? ev : &local);
}

br::Verdict kill_call(std::int64_t target, std::uint64_t sig,
                      br::Event* ev = nullptr) {
    const std::uint64_t args[6] = {static_cast<std::uint64_t>(target), sig,
                                   0, 0, 0, 0};
    br::Event local;
    return br::decide(static_cast<std::uint32_t>(SYS_kill), args, kGuest,
                      kGroup, ev ? ev : &local);
}

}  // 命名空间结束。

TEST_CASE("broker: only scalar-decidable syscalls are brokered") {
    // 名单很短 BY DESIGN ,这说明为什么。
    // 只有一个斯卡勒带着决定, 任何走路径都是指针,
    // 读到客人 内存 核准 调用 是那个 TOCTOU
    // 内核文档警告要错误。 这些规则属于 Landlock,
    // 而内核执行时没有 竞态.
    const auto list = br::brokered_syscalls();
    REQUIRE(list.size() == 2);
    CHECK(list[0] == static_cast<std::uint32_t>(SYS_ptrace));
    CHECK(list[1] == static_cast<std::uint32_t>(SYS_kill));
}

TEST_CASE("broker: PTRACE_TRACEME is allowed") {
    // 儿童自愿由自己的父母追踪。 授权 没有任何权力 任何其它,
    // 它是如何 `strace prog` 内部工作 -- -- 工作
    // 编译器 配置文件 KILLS 追踪,所以没有中介 每一个调试者和
    // 每一个追踪 -under-test 彻底死亡。
    br::Event ev;
    CHECK(ptrace_call(PTRACE_TRACEME, 0, &ev) == br::Verdict::Allow);
    CHECK(ev.allowed);
    CHECK(ev.syscall == "ptrace");
    // 审计线上写着请求,因为 "ptrace denied" 没用的,
    // 而且 "ptrace TRACEME allowed" 是可以采取行动的。
    CHECK(ev.detail.find("TRACEME") != std::string::npos);
}

TEST_CASE("broker: attaching outside our own group is denied") {
    // pid 1 在一个 pid namespace 中是 沙箱 自己的init:
    // 附加在它上是一个 XX00120 的 容器 -escape 原始,
    // 而不是调试步骤. 目标是一个 SCALAR, 这正是为什么这个是音效可解—— 没有
    // 读到 内存 的宾, 所以没有 竞态 的窗口。
    br::Event ev;
    CHECK(ptrace_call(PTRACE_ATTACH, 1, &ev) == br::Verdict::Deny);
    CHECK(!ev.allowed);
    CHECK(ev.detail.find("ATTACH") != std::string::npos);
    CHECK(ev.detail.find("pid=1") != std::string::npos);

    // 也是个无关紧要的警察
    CHECK(ptrace_call(PTRACE_ATTACH, 99999) == br::Verdict::Deny);
    CHECK(ptrace_call(PTRACE_SEIZE, 99999) == br::Verdict::Deny);
}

TEST_CASE("broker: attaching within our own group is allowed") {
    // 与自己孩子相连的调试器是普通的。 gdb 下的 `cargo test`,
    // 测试套件可以追踪它所产下的帮助器。
    CHECK(ptrace_call(PTRACE_ATTACH, kGuest) == br::Verdict::Allow);
    CHECK(ptrace_call(PTRACE_ATTACH, kGroup) == br::Verdict::Allow);
    // 具体目标0意味着接受该目标的请求的"self"。
    CHECK(ptrace_call(PTRACE_ATTACH, 0) == br::Verdict::Allow);
}

TEST_CASE("broker: killing our own group is allowed") {
    // 例行公事:失败时 超时 (1) 试验跑者收获 工作线程.
    CHECK(kill_call(0, SIGTERM) == br::Verdict::Allow);               // 我的人 来
    CHECK(kill_call(kGuest, SIGTERM) == br::Verdict::Allow);          // 自己来
    CHECK(kill_call(-static_cast<std::int64_t>(kGroup), SIGKILL) ==
          br::Verdict::Allow);                                        // 我的人 来
}

TEST_CASE("broker: killing outside our group is denied") {
    br::Event ev;
    CHECK(kill_call(1, SIGKILL, &ev) == br::Verdict::Deny);
    CHECK(!ev.allowed);
    CHECK(ev.detail.find("SIGKILL") != std::string::npos);

    CHECK(kill_call(99999, SIGTERM) == br::Verdict::Deny);
    CHECK(kill_call(-99999, SIGTERM) == br::Verdict::Deny);

    // 杀死( 1) 是 "every process I may signal"。
    // 在一个小的命名空间内被包围,但§3a意味着我们可能不会 HAVE 一种 --
    // -- 命名空间可以被拒绝,同时 seccomp 仍然有效,
    // 所以假设一个是 完全的 -right 检查此政策是否有效。
    CHECK(kill_call(-1, SIGKILL) == br::Verdict::Deny);
}

TEST_CASE("broker: signal 0 follows the same rules as a real signal") {
    // kill( pid, 0) 发送无任何信息,
    // 并且是标准存在探测器。 它仍然是关于 外部进程的信息泄露。
    // 沙箱 ,它不花费什么 对待它像任何其他目标,
    // 所以规则是统一的而不是特殊 -cased 读者不必
    // 记得例外。
    CHECK(kill_call(kGuest, 0) == br::Verdict::Allow);
    CHECK(kill_call(1, 0) == br::Verdict::Deny);
}

TEST_CASE("broker: an unhandled brokered syscall is denied") {
    // 失败 -safe 方向。如果 brokered_syscalls
    // () 永远的成长 进入 decide() 两者不处理,两者已漂移—
    // —默认必须否认,因为另一种办法是无声地扩大,而没有任何试验会
    // 注意到。
    const std::uint64_t args[6] = {0, 0, 0, 0, 0, 0};
    br::Event ev;
    // SYS_getpid 从来没有被中介,所以这个代表
    // "a number decide() has no case for".（decide() 没有对应分支的数值。）
    CHECK(br::decide(static_cast<std::uint32_t>(SYS_getpid), args, kGuest,
                     kGroup, &ev) == br::Verdict::Deny);
    CHECK(!ev.allowed);
}

TEST_CASE("broker: an unknown ptrace request is denied") {
    // 这并不是因为每个未知请求都是危险的,而是因为允许请求的主管
    // 没有说明理由,这不是一项政策。有~40 ptrace请求,
    // 还有几个(PTRACE_SETREGS,PTRACE_POKEUSER)
    // 重写了追踪过程的执行。
    //
    // 检查目标 内部 我们自己的群体,因为这就是这个政策
    // 的第一个版本是错误的:目标规则在请求规则之前运行,
    // 所以允许针对我们自己的孩子的未经承认的请求.
    // "It is one of ours" 并不是允许
    // 进行一项行动的理由,
    CHECK(ptrace_call(0xdead, kGuest) == br::Verdict::Deny);
}

TEST_CASE("broker feed: only denials are recorded") {
    // 经纪人记录两种结果 -- -- 记录是追踪 -- --
    // 但记录显示, FEED 列出每个允许的线程的种子都是
    // 一条线索, 而其中最重要的否认就是它。
    br::clear_blocked_feed();

    br::Event allowed{"ptrace", "TRACEME pid=0", true, 1};
    br::Event denied{"ptrace", "ATTACH pid=1", false, 1};
    br::record(allowed);
    br::record(denied);

    const auto feed = br::blocked_feed();
    REQUIRE(feed.size() == 1);
    CHECK(feed[0].detail == "ATTACH pid=1");
    CHECK(!feed[0].allowed);
}

TEST_CASE("broker feed: identical repeats coalesce into a count") {
    // 一个环击击击打一个被拒绝的调用应该是一条可读的线,而不是4096.
    // 统计也是信息:一个流浪者 调用 循环是不同的问题,
    // "× 4096" 也就是他们之间的区别
    br::clear_blocked_feed();

    br::Event ev{"ptrace", "ATTACH pid=1", false, 1};
    for (int i = 0; i < 500; ++i) br::record(ev);

    const auto feed = br::blocked_feed();
    REQUIRE(feed.size() == 1);
    CHECK(feed[0].count == 500);
}

TEST_CASE("broker feed: interleaved denials stay separate") {
    // 合作是反对 最后 事件 仅限整个种子,
    // 一个换两个 调用 显示两个互换的计数器而不是两个,
    // 因为 ORDER 信息,
    // 它说工具链在做什么。
    br::clear_blocked_feed();

    br::Event a{"ptrace", "ATTACH pid=1", false, 1};
    br::Event b{"kill", "SIGKILL pid=1", false, 1};
    br::record(a);
    br::record(b);
    br::record(a);

    const auto feed = br::blocked_feed();
    REQUIRE(feed.size() == 3);
    CHECK(feed[0].syscall == "ptrace");
    CHECK(feed[1].syscall == "kill");
    CHECK(feed[2].syscall == "ptrace");
}

TEST_CASE("broker feed: the feed is bounded") {
    // 无限增长将拒绝主管对自己的东道主提供服务:客人只需要
    // 锤子 DISTINCT 拒绝 调用 在一个循环中。
    br::clear_blocked_feed();

    for (int i = 0; i < 500; ++i) {
        // 不同的细节,所以结合不能掩盖增长——这是对抗的形状,
        // 而不是友好的形状.
        br::Event ev{"kill", "SIGKILL pid=" + std::to_string(1000 + i), false, 1};
        br::record(ev);
    }

    const auto feed = br::blocked_feed();
    CHECK(feed.size() <= 64);

    // 还有 NEWEST 丢掉最年长的是一个刻意
    // 的:一个想要冲洗早期否认证据的客人必须把64
    // 个不同的否定推过去,每一个都记录下来——所以
    // 尝试比它会隐藏的东西要响亮。
    REQUIRE(!feed.empty());
    CHECK(feed.back().detail == "SIGKILL pid=1499");
}

TEST_CASE("broker feed: concurrent writers do not corrupt it") {
    // 饲料来自工具WORKER 线程, 并读自工具状态更新函数 线程,
    // 这是在 沙箱 层中真正的跨-thread 表面。
    // 在 TSan 下, 这件案子会捕捉到一个解锁的版本;
    // 没有它, 撕裂的 std::string 将会显示为垃圾细节。
    br::clear_blocked_feed();

    std::vector<std::thread> writers;
    for (int t = 0; t < 4; ++t) {
        writers.emplace_back([t] {
            for (int i = 0; i < 200; ++i) {
                br::Event ev{"kill",
                             "SIGKILL pid=" + std::to_string(t * 1000 + i),
                             false, 1};
                br::record(ev);
            }
        });
    }
    // 读者们在赛跑,因为 快照 中场 -write 完全就是
    // 如果 blocked_feed () 交回参考书。
    std::atomic<bool> stop{false};
    std::thread reader([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            for (const auto& e : br::blocked_feed()) {
                // 触摸那些堆积的成员,这样一串撕裂的弦就被贬低了.
                CHECK(!e.syscall.empty());
                CHECK(!e.detail.empty());
            }
        }
    });

    for (auto& w : writers) w.join();
    stop.store(true, std::memory_order_relaxed);
    reader.join();

    CHECK(br::blocked_feed().size() <= 64);
}

#endif  // __linux__
