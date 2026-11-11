#pragma once
// agentty::tools::util::sandbox::claybin_backend — 库
// -based 沙箱 后端，与 bwrap 后端并排。
//
// 头文件 保留了 claybin 自己的 头文件，因此 agentty 的其余部分不需要
// claybin 的包含路径，因此使用 AGENTTY_HAVE_CLAYBIN
// 关闭 编译 这个文件的构建完全消失。
//
// 接口被故意缩小：输入一个Posture（绑定什么、限制什么），
// 输出一个pid。关于 SUPERVISING 子进程的所有内容——轮询、
// 耗尽、空闲截止日期、SIGTERM/SIGKILL、
// 收割——都保留在 subprocess.cpp 中，它已经对沙盒和
// 非沙盒路径正确执行了该操作。还拥有监督循环的 沙箱 后端将是困难部分
// 的两种实现。
//
// 平台说明：在 Windows 上，SpawnResult 的两个 int 句柄字段携带句柄，
// 而不是描述符——`pidfd` 是子进程的 PROCESS 句柄（可等待退出；
// jaal 的 windows_process::adopt 消耗它），并且 `kill_tree`
// 靠近 JOB 句柄（KILL_ON_JOB_CLOSE = 源码树）杀）。

#include <cstdint>
#include <functional>
#include <utility>
#include <string>
#include <vector>

#include "agentty/tool/util/sandbox_netproxy.hpp"

namespace agentty::tools::util::sandbox::claybin_backend {

// 来自agentty自己的环境的环境变量是否到达沙盒子项。
// 删除 desktop/session 套接字和提供者 API 密钥。
[[nodiscard]] bool box_env_keeps(std::string_view key) noexcept;

// 构建什么。从 sandbox.cpp 中的 SHARED read-set
// 常量填充，而不是在此处重述 - 请参阅 sandbox_claybin.cpp
// 顶部有关重述它们并授予在 `/` 上读取权限的后端的注释。
struct Posture {
    std::vector<std::string> system_read_roots;  // kSystemReadRoots
    std::vector<std::string> etc_readable;       // kEtc可读
    std::vector<std::string> home_tool_dirs;     // $HOME + kHomeToolSubdirs
    // 用户在范围顶部的 面板 中添加额外的 read/write 授权。
    std::vector<std::string> read_paths;
    std::vector<std::string> write_paths;
    // 在每个授权之后屏蔽绝对路径。 凭据、代理状态以及用户添加到 deny_paths
    // 的任何内容。请参阅 kAlwaysMasked。
    std::vector<std::string> masked;
    // Host-trusted EXIST和必须读取-only的路径：.vscode/tasks.json、.git/hooks/*、.git/config、
    // hooks.json。 PREVENTION 是信任切换
    // 门的一半。
    //
    // 不被屏蔽并且不被拒绝--将其自身绑定到read-only，
    // 因此访客读取真实内容并在写入时获得EROFS。屏蔽会破坏每个合法
    // 的阅读器（git 需要它自己的配置）；内锁拒绝根本无法在可写
    // 工作区 下表达 "this path only"（测量：在一个规则
    // 集权利 INHERIT 内，并且更深更严格的规则不会减去 -
    // docs/design/shell-write-gate.md）。
    // read-only 绑定是精确的，不需要语法，并且没有引用技巧可以逃避它，
    // 因为墙就是文件系统。
    //
    // 需要安装命名空间，因此这是拒绝非特权用户的主机上
    // 的ABSENT——这就是检测一半存在并且无论如何都在
    // 各处运行的原因。
    std::vector<std::string> handoff_ro;
    std::string workspace;                       // 读取-write，最后绑定
    std::string cwd;                             // shell开始的地方

    // ── 网络 ──────────────────────────────────────────────────────────
    // 将 sandbox_cfg::NetMode 镜像为 int，
    // 因此该 头文件 不受域类型的影响，并且没有 claybin 的构建不需要它。
    //   0=完全（共享主机网络），1=无，2=端口
    int net_mode{0};
    std::vector<std::uint16_t> allow_ports;
    // Full-mode代理的主机-name 允许清单 (sandbox_cfg::Config::allow_hosts)。
    std::vector<std::string> allow_hosts;

    // ── 调音 ─────────────────────────────────────────────────────────
    // sandbox_cfg::SyscallMode作为整数：0=关闭，1=编译器，2=严格。
    int syscall_mode{1};
    bool wx_protect{true};

    // ── 硬化 ────────────────────────────────────────────────────────
    bool scope_ipc{true};
    bool close_inherited_fds{true};

    // ── 资源 ────────────────────────────────────────────────
    std::uint64_t tmp_bytes{512ull * 1024 * 1024};
    std::uint64_t memory_bytes{0};   // 0=无上限
    std::uint64_t max_processes{0};  // 0=无上限
    std::uint32_t cpu_percent{0};    // 0=无上限，100=一核
    std::uint32_t max_open_files{0}; // 0=无上限(RLIMIT_NOFILE)
    std::uint32_t wall_clock_secs{0};// 0=无上限
    std::uint32_t cpu_secs{0};       // 0=无上限(RLIMIT_CPU)

    // ── 身份 ────────────────────────────────────────────────
    // 不是遏制——客人不能以任何方式升级。
    // 这是关于泄漏到构建输出和测试装置中的内容。
    bool fake_hostname{false};

    // ── 所谓的经纪活动 ────────────────────────────────────────
    // 根据它们的标量参数在运行时处决定ptrace和kill，而不是断然拒绝它们。
    // 参见 sandbox_broker.hpp。
    //
    // FLAG而不是总是-on，因为代理使子进程的
    // 进度取决于父进程的应答：内核阻塞代理的系统调用直到
    // 有人响应，因此没有被轮询的监管者是挂起的。
    // spawn_shell 仅当它可以将侦听器交给将轮询
    // 它的运行器时才设置它。
    bool broker{false};
};

// 一堵墙以及其执行力度如何，直接来自 claybin 的保证报告。暴露出来，
// 这样 `describe_state()` 就可以命名实际发生的事情，
// 而不是打印 "active"。
struct Wall {
    std::string name;
    std::string strength;  // "strong" | "partial" | "none"
    // WHICH机构到达那里："landlock abi 10"、"cgroup2 memory.max"、
    // "seccomp-bpf"、"mount-ns"。
    //
    // 不是装饰。"strong"单独是一种主张；
    // "strong via landlock abi 10"是
    // 附有收据的索赔，差异很重要，因为SAME通过具有不同故障模式的
    // 两条不同路线可以增强能力——filesystem.read正在安装
    // -ns命名空间何时工作，何时不工作则锁定，调试拒绝的用户
    // 需要知道是哪一个。虽然设置窗格已经报告了它，但它在这里丢失了，
    // 因此两个表面在他们知道多少方面存在分歧。
    std::string mechanism;
};

struct Report {
    bool ok{false};
    std::string detail;  // 为什么不，当！ok
    std::vector<Wall> walls;
};

struct SpawnResult {
    bool started{false};
    int pid{-1};
    int pidfd{-1};
    std::string start_error;

    // 当策略代理系统调用时，seccomp监听器。否则-1。
    //
    // CALLER拥有它，并且只要子进程运行，
    // MUST就会轮询它：代理的系统调用会阻塞内核中的来宾线程，
    // 直到有人应答，因此未轮询的侦听器是挂起而不是较弱的墙。
    // 下面是`service_broker`如何回答之一。
    int supervisor_fd{-1};

    // 回答 一个 待处理 通知。当侦听器完成时返回
    // false（来宾消失，fd 关闭），之后调用方停止轮询。
    //
    // 回调而不是暴露监听器，因此工具层从不包括
    // claybin 头文件：决策策略、TOCTOU复查
    // 和审计记录都存在于这个调用后面。
    std::function<bool()> service_broker;

    // 访客的过滤出站网络，当姿势给它一个时。
    //
    // 由 shared_ptr 持有，因为 SpawnResult
    // 被复制并且正在运行的代理不可复制。将其保留在这里是将代理的生命
    // 周期与命令的生命周期联系起来的：此结果的最后一个副本消失会阻止它。
    // 跑步者永远不会碰它。
    std::shared_ptr<netproxy::Proxy> net_proxy;

    // SIGKILL客人和每一个后代，原子地，当姿势得到了cgroup。
    // 如果没有（没有 cgroup2、没有委派或低于 5.14 且
    // 没有 cgroup.kill 的内核），则为空，
    // 然后调用者会退回到向进程组发出信号。
    //
    // 该组本身是不够的：claybin将访客置于新会话中，
    // 但是没有什么可以阻止后代再次呼叫setsid()并退出它。
    // cgroup 没有这样的出口。
    std::function<bool()> kill_tree;
};

// 该主机真的可以构建沙箱吗？分叉并尝试 uid_map 写入，
// 因此它预测 spawn() 而不是从 sysctls 猜测。探查过一次。
[[nodiscard]] bool available();

// 该主机的锁ABI，不存在时为0。与 available() 相同的缓存探针。
[[nodiscard]] std::uint32_t landlock_abi();

}  // namespace agentty::tools::util::sandbox::claybin_backend

// 主机的能力，每个进程探测一次（探测分叉）。
// 用于预览和 选择面板；生成路径实时探测。
#if defined(__APPLE__)
#include "claybin/macos/backend.hpp"
namespace agentty::tools::util::sandbox::claybin_backend {
[[nodiscard]] const ::clay::macos::HostCapabilities& host();
}
#elif defined(__linux__)
#include "claybin/plan/compile.hpp"
namespace agentty::tools::util::sandbox::claybin_backend {
[[nodiscard]] const ::clay::HostCapabilities& host();
}
#endif

namespace agentty::tools::util::sandbox::claybin_backend {

// 编译姿势并报告其屈服的墙壁，而不运行任何
// 东西。
[[nodiscard]] Report describe(const Posture& p);

// 分叉，应用沙箱，执行`argv`。 stdout_fd/stderr_fd
// 是 CALLER 的描述符（通常是一个管道的两端）；他们在 沙箱
// 的近距离扫射中幸存下来并降落在客机的 1 和 2 上。
// 一旦子进程运行就返回——调用者等待。
//
// argv 完全按照给定的方式到达子级，这就是要点：其中带有引号
// 或 `$` 的提交消息在传入时一定不能是 re-parsed。
[[nodiscard]] SpawnResult spawn_argv(const Posture& p,
                                     const std::vector<std::string>& argv,
                                     int stdout_fd, int stderr_fd,
                                     const std::vector<std::pair<std::string, std::string>>& env = {});

// 同样的，前面有一个壳：`/bin/sh -c shell_cmd`。管道、
// 重定向和 glob 是 shell 工具的一部分。实现为 调用 到
// spawn_argv，因此只有一条生成路径而不是两条。
[[nodiscard]] SpawnResult spawn_shell(const Posture& p, const std::string& shell_cmd,
                                      int stdout_fd, int stderr_fd);

#if defined(__APPLE__)
// 编译成安全带轮廓文本的姿势——正是spawn_shell手中的sandbox_init()。
// 对于无法使用 claybin 的 fork 的生成器（达尔文上的 jaal
// 没有 pidfd 来收养外国孩子），因此它们通过 /
// usr/bin/sandbox-exec 运行相同的配置文件。失败时为空 + `error`：
// 调用者必须在失败时关闭，决不能回退到无限制运行。
[[nodiscard]] std::string compile_profile(const Posture& p, std::string& error);

// 达尔文网络网关：agentty在内核-chosen 127.0.0.1
// 端口上的过滤代理，在第一次使用时启动并由每个访客共享。如果无法启动，
// 则为 0，在这种情况下，完整网络 编译 到 NO 网络。
[[nodiscard]] std::uint16_t macos_proxy_port(const std::vector<std::string>& allow_hosts);
// http_proxy/https_proxy/ALL_PROXY在`p`
// 需求（"K=V")下分配访客，为空，除非状态是完整网络并且代理已启动。
[[nodiscard]] std::vector<std::string> proxy_env(const Posture& p);
#endif

}  // namespace agentty::tools::util::sandbox::claybin_backend
