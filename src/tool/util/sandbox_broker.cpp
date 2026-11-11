// 监督员的决定政策。 头文件 为何这种
// 限制是结构性的,而不是简化的。

#include "agentty/tool/util/sandbox_broker.hpp"

#include <maya/runtime.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#if defined(__linux__)
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <signal.h>
#include <fcntl.h>          // 打开( 正在读 /) proc/<pid>/...)
#include <unistd.h>         // 读取, 关闭
#include <dirent.h>         // opendir/readdir  (/proc/<pid> /任务)
#include <cstdio>           // 短短的指纹f
#include <cstdlib>          // 斯特罗尔
#include <cstring>
#include <vector>
#endif

namespace agentty::tools::util::sandbox::broker {

namespace {

#if defined(__linux__)

// 追踪(2) 请求我们愿意解释。 其余请求被拒绝,
// 不是因为每个请求都很危险,而是因为允许其未考虑
// 请求的主管并不是一项政策。
//
// PTRACE_TRACEME 有趣的是,孩子就是这样的 ASKS 由自己
// 的父母追踪, 这正是什么 `strace prog` 调试器 -under-
// 测试是的,它 授权 追踪器没有新的权威 -- -- 儿童正在志愿工作。
[[nodiscard]] const char* ptrace_request_name(std::uint64_t req) {
    switch (req) {
        case PTRACE_TRACEME:     return "TRACEME";
        case PTRACE_ATTACH:      return "ATTACH";
        case PTRACE_SEIZE:       return "SEIZE";
        case PTRACE_PEEKDATA:    return "PEEKDATA";
        case PTRACE_POKEDATA:    return "POKEDATA";
        case PTRACE_CONT:        return "CONT";
        case PTRACE_DETACH:      return "DETACH";
        case PTRACE_GETREGSET:   return "GETREGSET";
        case PTRACE_SETOPTIONS:  return "SETOPTIONS";
        default:                 return "?";
    }
}

[[nodiscard]] std::string signal_name(std::uint64_t sig) {
    switch (sig) {
        case 0:        return "0";        // 存在探测器,没有发送任何信息
        case SIGTERM:  return "SIGTERM";
        case SIGKILL:  return "SIGKILL";
        case SIGINT:   return "SIGINT";
        case SIGHUP:   return "SIGHUP";
        case SIGUSR1:  return "SIGUSR1";
        case SIGUSR2:  return "SIGUSR2";
        case SIGCHLD:  return "SIGCHLD";
        case SIGSTOP:  return "SIGSTOP";
        case SIGCONT:  return "SIGCONT";
        default:       return std::to_string(sig);
    }
}

// 已经 `target` 进程 REQUESTER 可能发出信号... i.e .本身或其后代 -- -
// - 在何处 `target` 是一个pid as the REQUESTER 看见了吗?
//
// 该 GAP 此 CLOSES ,这是两个空隙堆叠。
//
// (1) 以下两项目标规则都曾是对guest_pid/guest_pgid的平等测试,
//     答案是"is the target the group LEADER".
//     儿童既不是:有自己的皮条,继承了领导人的皮条,因此,每次杀人(孩子,
//     sig)都被否认 -- -- 而两行之外的评论已经称之为例行公事(
//     例如 make 失败后的清理、timeout，以及测试运行器回收工作进程。
//
//
// (2) 这些数字甚至不是在同一空间。 claybin 给客人自己的 PID
//     NAMESPACE 客人被罚进监狱 被罚进医院 kill() 名称空间
//     -local. `guest_pid` 是那个 HOST-side
//     pid from (原始内容存档于2018-09-21).
//     spawn() 比较它们是一个不为人知的分类错误,因为它总是说 "no"
//     :测量,嘉宾将自己视为pid 3, pgid 1,
//     而其孩子则视为pid4,而主管则在对比数十万中的主持人pid.
//
// 观察: `timeout 1 sleep 5` 5秒后就跑完 时间
// 点火 死亡被拒绝 EPERM ,而creutils 默默放弃 -
// 所以一个挂在命令在 沙箱 从未被杀 任何使用 `timeout`
// 作为 看门狗 没有任何。
//
// 修补是停止猜想,以REQUESTER自身条件问内核.
// /proc/<requester>/task/<tid> 孩子们是宿主
// -pid 清单 线程 直系子女; 转过路来给后代设定。 将客人的命名空间转动。
// -local 目标输入主机的 pid 我们读 NSPID 出/
// proc/<host_pid> 身份,其国籍 最后 字段是最内侧
// 命名空间中的pid——正好是宾客使用的号码。
//
// 失败 CLOSED 在每个步骤: 无法读取的 Proc 文件、 缺失的 NSpid 线条,
// 或 超过其约束的行走 "not a descendant".
//
// 故意受了伤,走是... DoS 表面,否则:一个客人,
// 叉开深处 源码树 就能让 SUPERVISOR 做无限制的
// 工作 在民意调查循环 被封杀的客人。
[[nodiscard]] bool is_descendant_of(std::uint32_t requester_host_pid,
                                    std::int32_t  target_ns_pid) noexcept {
    if (target_ns_pid <= 0 || requester_host_pid == 0) return false;

    constexpr int kMaxVisit = 4096;    // 慷慨的建造, 以炸弹为界

    // 读取一个完整的小 proc 文件。 任何失败时返回空。
    const auto slurp = [](const char* path) -> std::string {
        const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) return {};
        std::string out;
        char buf[4096];
        for (;;) {
            const auto n = ::read(fd, buf, sizeof buf);
            if (n <= 0) break;
            out.append(buf, static_cast<std::size_t>(n));
            if (out.size() > 64 * 1024) break;     // 孩子们列出这个大块头就是炸弹
        }
        ::close(fd);
        return out;
    };

    // 蜘蛛头 `host_pid` 在其《公约》中 INNERMOST 名称空间,
    // 来自NSpid。没有NSpid(前4.1)的内核报告什么也没有,我们没有关闭。
    const auto ns_pid_of = [&](std::uint32_t host_pid) -> std::int32_t {
        char path[64];
        std::snprintf(path, sizeof path, "/proc/%u/status", host_pid);
        const std::string st = slurp(path);
        const auto at = st.find("\nNSpid:");
        if (at == std::string::npos) return -1;
        const auto eol = st.find('\n', at + 1);
        const std::string line = st.substr(at + 1, eol - at - 1);
        // "NSpid:\t<outer>\t<...>\t<innermost>" - 拿着 最后 字段中选择
        const auto sp = line.find_last_of(" \t");
        if (sp == std::string::npos) return -1;
        return static_cast<std::int32_t>(std::strtol(line.c_str() + sp + 1,
                                                     nullptr, 10));
    };

    // 宽度走着请求者的后代 -first ,
    // 并询问每个人它认为它有什么。 GUEST 数字
    // 空间是这个正确之处 不管有多少个命名空间是嵌入的
    std::vector<std::uint32_t> frontier{requester_host_pid};
    int visited = 0;
    while (!frontier.empty() && visited < kMaxVisit) {
        const std::uint32_t pid = frontier.back();
        frontier.pop_back();
        ++visited;

        if (pid != requester_host_pid && ns_pid_of(pid) == target_ns_pid)
            return true;

        // 儿童人数 -THREAD ,因此,这一进程的每一项任务都作出了贡献。
        char tdir[64];
        std::snprintf(tdir, sizeof tdir, "/proc/%u/task", pid);
        DIR* d = ::opendir(tdir);
        if (!d) continue;
        while (const dirent* e = ::readdir(d)) {
            if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
            char cpath[320];
            std::snprintf(cpath, sizeof cpath, "/proc/%u/task/%s/children",
                          pid, e->d_name);
            const std::string kids = slurp(cpath);
            const char* p = kids.c_str();
            while (*p) {
                while (*p == ' ' || *p == '\n') ++p;
                if (!*p) break;
                char* end = nullptr;
                const long v = std::strtol(p, &end, 10);
                if (end == p) break;
                if (v > 0) frontier.push_back(static_cast<std::uint32_t>(v));
                p = end;
            }
        }
        ::closedir(d);
    }
    return false;
}

// 请求者自己 的皮迪和皮迪 IT 看见它们(最内在的命名空间),
// 从 NSpid/NSpgid. 目标 客人的名字在这个数字空间,
// 所以 "is this me / my group" 一定要问那里,而不是反对宿主的皮条客。
// {1, -1} 当/proc不能说;呼叫者然后返回到主机值.
struct NsIds { std::int32_t pid = -1, pgid = -1; };
[[nodiscard]] NsIds ns_ids_of(std::uint32_t host_pid) noexcept {
    NsIds out;
    char path[64];
    std::snprintf(path, sizeof path, "/proc/%u/status", host_pid);
    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return out;
    std::string st;
    char buf[4096];
    for (;;) {
        const auto n = ::read(fd, buf, sizeof buf);
        if (n <= 0) break;
        st.append(buf, static_cast<std::size_t>(n));
        if (st.size() > 64 * 1024) break;
    }
    ::close(fd);
    // "Key:\t<outer>\t...\t<innermost>" : 带 最后 字段中选择
    const auto last_field = [&](std::string_view key) -> std::int32_t {
        const auto at = st.find(key);
        if (at == std::string::npos) return -1;
        const auto eol = st.find('\n', at + 1);
        const std::string line = st.substr(at + 1, eol - at - 1);
        const auto sp = line.find_last_of(" \t");
        if (sp == std::string::npos) return -1;
        return static_cast<std::int32_t>(std::strtol(line.c_str() + sp + 1, nullptr, 10));
    };
    out.pid  = last_field("\nNSpid:");
    out.pgid = last_field("\nNSpgid:");
    return out;
}

#endif  // __linux__

}  // 命名空间结束。

namespace {

// 被封锁的-activity饲料储存.
//
// `maya::snapshot<T>` 而不是生的 而不是生下来的 std::mutex
// 或被看守的 <T> : 种子是一个小值, 在所有拒绝上被替换 整个被读者复制,
// 所以 锁-free 不可变 值完全适合 -- -- 没有级别,没有能力,
// `record()` (从经纪人的主管那里得到的) 线程 永远也不必证明
// 它持有什么。 "Forgot the lock" 无法代表: 唯一的写作
// 是 `update()` ,它构建了 新 向量从旧的。
//
// 函数 -local 静态的初始化顺序不能咬: 第一个拒绝可以从
// 一个 工作线程 线程 启动时, 和一个文件 -scope 全球将是
// 一个 竞态 和它自己的建筑师。
maya::snapshot<std::vector<Event>>& feed() {
    static maya::snapshot<std::vector<Event>> f;
    return f;
}

// 有多少个不同的事件要保留.
//
// 被围起来是因为客人在环绕中敲打一个被拒绝的音响,
// 否则会毫无限制地扩大这种音响 -- -- 主管拒绝为自己
// 的主机提供服务。 READER ,不用于 内存: a
// 面板 显示超过屏蔽的否认 已经停止沟通, 和下面的集合
// 意味着64个不同 事件 覆盖了64个以上的音响。
constexpr std::size_t kMaxEvents = 64;

}  // 命名空间结束。

void record(const Event& ev) {
    // 允许不是被屏蔽的活动。经纪人日志(这个日志是一种
    // 追踪)都存在,但是列出每个允许的线程的种子是一条线索,
    // 而其中最重要的一个否认会淹没在其中。
    if (ev.allowed) return;

    (void)feed().update([](const std::vector<Event>& old, const Event& in) {
        std::vector<Event> events = old;
        // 复制一次,检查到 最后 事件 而不是
        // 整个饲料:一个二位代表否认的建筑 调
        // 用 显示两个互换的计数器, 因为顺序是信息,
        // 它告诉你工具链在做什么。
        if (!events.empty()) {
            auto& last = events.back();
            if (last.syscall == in.syscall &&
                last.detail == in.detail) {
                // 饱和而不是包。一个倒数为 0 的计数会读作
                // "this never happened",
                // 这是审计表面最糟糕的谎言。
                if (last.count < UINT32_MAX) ++last.count;
                return events;
            }
        }

        Event incoming = in;
        incoming.count = 1;
        events.push_back(std::move(incoming));

        // 放下枪 OLDEST 想要冲洗早期否认
        // 证据的客人必须推64 DISTINCT 否认,
        // 每一个都记录下来了——所以尝试比它会
        // 隐藏的东西要响亮。
        if (events.size() > kMaxEvents) events.erase(events.begin());
        return events;
    }, ev);
}

std::vector<Event> blocked_feed() {
    // 一份,故意的,打来电话的人在另一张 线程。
    // 该词 快照 值为 不可变 所以抄袭不需要 锁 一点都没有
    return feed().get();
}

void clear_blocked_feed() {
    feed().store({});
}

std::vector<std::uint32_t> brokered_syscalls() {
#if defined(__linux__) && defined(SYS_ptrace) && defined(SYS_kill)
    // 正好是两个, 这份清单的短处就是设计。
    //
    // 仅当ALL:
    //   - 标语的论据带有裁决(无指针,无TOCTOU)
    //   - 有时,完全否认是完全错误的(因此,中间商交易买断了)
    //     兼容性,而不仅仅是一个日志行)
    //   - 平面许可有时确实是错误的(因此有决定);
    //     (完全可以)
    //
    // 追踪和杀死三个目标。任何涉及路径的东西都不会发生,因为路径是指针;
    // 这些属于地锁。任何涉及套接字的东西都不可能 --
    // socket() 正在显示 -decidable 财务报告和已审计
    // 财务报表 claybin 连一个帮手都上船了 但网络命名空间
    // 已经更强大了 竞态-free 答案,一个边界的两个机制是它们如何漂移。
    return {static_cast<std::uint32_t>(SYS_ptrace),
            static_cast<std::uint32_t>(SYS_kill)};
#else
    return {};
#endif
}

Verdict decide(std::uint32_t nr, const std::uint64_t args[6],
               std::uint32_t guest_pid, std::uint32_t guest_pgid,
               Event* out_event) {
    auto record = [&](const char* sc, std::string detail, bool allowed) {
        if (out_event) {
            out_event->syscall = sc;
            out_event->detail = std::move(detail);
            out_event->allowed = allowed;
        }
        return allowed ? Verdict::Allow : Verdict::Deny;
    };

#if defined(__linux__) && defined(SYS_ptrace) && defined(SYS_kill)
    if (nr == static_cast<std::uint32_t>(SYS_ptrace)) {
        const std::uint64_t request = args[0];
        const auto target = static_cast<std::uint32_t>(args[1]);
        const char* rname = ptrace_request_name(request);
        std::string detail = std::string{rname} +
                             " pid=" + std::to_string(target);

        // 拒绝未知请求 REGARDLESS 目标。
        //
        // 在目标规则之前检查过, 而命令是这个文件被发送的错误的
        // 修复 : 目标检查是先来的, 因此允许针对我们自己的组的
        // 未识别请求。 有~40 ptrace请求, 其中几个(
        // PTRACE_SETREGS, PTRACE_POKEUSER)
        // 是您如何重写一个跟踪进程执行的,
        // 而"it is one of ours" 不是一个允许操作的理由,
        // 没有任何理由说明。 允许请求的主管不理解它不是
        // 政策。
        if (std::string_view{rname} == "?")
            return record("ptrace", std::move(detail), false);

        // TRACEME : 打电话者自愿者由自己的父母追踪。
        // 没有新的权威来管理其他事务,而是如何 `strace prog`
        // 内在工作,允许
        if (request == PTRACE_TRACEME)
            return record("ptrace", std::move(detail), true);

        // 其他的都叫TARGET,目标就是Scalar,
        // 所以这正是这个形状的中介是声音。
        //
        // 仅在我们自己的进程组内允许。 连接在自己孩子上的调试
        // 器是普通的; 到达组外是逃跑的形状 — 而pid 1 在
        // 一个 pid namespace 中的是 沙箱 自己
        // 的内装箱,这是容器 -escape 原始的而不是调试
        // 步骤.
        //
        // 故意的 不 "same pid namespace" :
        // 我们不能从寄存器值中询问内核,猜想会是那种几乎 -right
        // 检查此文件的存在以避开。 进程组是我们确定知道的东西,
        // 因为我们已经设置它。
        if (target == 0 || target == guest_pid)
            return record("ptrace", std::move(detail), true);
        if (guest_pgid != 0 && target == guest_pgid)
            return record("ptrace", std::move(detail), true);
        if (const auto me = ns_ids_of(guest_pid);
            me.pid > 0 && static_cast<std::int32_t>(target) == me.pid)
            return record("ptrace", std::move(detail), true);
        // ..还有一个 DESCENDANT ,这是上面段落描述的普通调试器案例(
        // 调试器附加到自己启动的子进程。
        // 光是平等是永远无法表达的:孩子的病症既不是领袖的病症,
        // 也不是小病症。
        if (is_descendant_of(guest_pid, static_cast<std::int32_t>(target)))
            return record("ptrace", std::move(detail), true);
        return record("ptrace", std::move(detail), false);
    }

    if (nr == static_cast<std::uint32_t>(SYS_kill)) {
        // 双双双双的
        const auto target = static_cast<std::int32_t>(
            static_cast<std::int64_t>(args[0]));
        const std::uint64_t sig = args[1];
        std::string detail = signal_name(sig) + " pid=" + std::to_string(target);

        // 自我,和自己的-group形式(0=我的团体,负=那个团体).
        // 杀死自己的孩子是例行公事: `make` 失败时,
        // `timeout` 测试跑者收获 工作线程.
        //
        // 目标在客人的名片间 所以 "is it me / my group"
        // 以编号要求( NSpid/NSpgid。
        // 东道主 -pid 测试一个没有零点名位的客人, 两人同意。
        const auto me = ns_ids_of(guest_pid);
        if (target == 0 || target == static_cast<std::int32_t>(guest_pid)
            || (me.pid > 0 && target == me.pid))
            return record("kill", std::move(detail), true);
        if (target < 0) {
            const auto grp = static_cast<std::uint32_t>(-target);
            if (grp == guest_pgid || grp == guest_pid
                || (me.pgid > 0 && static_cast<std::int32_t>(grp) == me.pgid))
                return record("kill", std::move(detail), true);
            // ...或一个团体 LED BY 我们的后代之一。
            // `timeout` 完全是这样: putpgid() 将命令输入
            // 自己的组,然后发出信号杀死( -pgid ),所以它经过的
            // pgid是孩子的命名空间 -local pid.
            // 上面的等同测试都是主机 -side ,所以这个表格总是被拒绝
            // -- 这就是为什么 `timeout 1 sleep 5` 5秒后,
            // 我还跑完 -pid 案件开始工作。
            //
            // 安全的原因与原因相同 每个人 -pid
            // 案例是:我们后裔领导的团体只包含后裔可以自己
            // 表示的流程,不能将一个外国流程移动到它所
            // 领导的团体中.
            if (is_descendant_of(guest_pid, static_cast<std::int32_t>(grp)))
                return record("kill", std::move(detail), true);
            return record("kill", std::move(detail), false);
        }
        if (guest_pgid != 0 && static_cast<std::uint32_t>(target) == guest_pgid)
            return record("kill", std::move(detail), true);
        if (me.pgid > 0 && target == me.pgid)   // 团体领导人,按人数分列
            return record("kill", std::move(detail), true);
        // 我们集团中的一个进程 -- i.e 我们的后代之一,
        // 上面的评论和平等测试不能做到: `timeout`
        // 杀死它所监管的指挥, `make` 杀死一个失败的食谱,
        // 一个跑者收获它 工作线程.
        if (is_descendant_of(guest_pid, target))
            return record("kill", std::move(detail), true);

        // 杀死( 1, sig) 是 "every process I may signal"。 在一个 pid 中
        // 命名空间中的范围有限，但不能假设始终存在这一边界，
        // (见第3a节:在seccomp仍在工作时可能拒绝使用命名空间),
        // 因此被拒绝。
        return record("kill", std::move(detail), false);
    }
#else
    (void)nr; (void)args; (void)guest_pid; (void)guest_pgid;
#endif

    // 我们要求进行一个系统调用,然后却不处理。
    // 拒绝是唯一安全的默认 — — 允许意味着被调用的清单,
    // 而这个功能可以悄悄地向允许的方向飘移。
    return record("?", "unhandled brokered syscall", false);
}

}  // namespace agentty::tools::util::sandbox::broker
