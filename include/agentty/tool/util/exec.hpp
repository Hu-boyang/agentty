#pragma once
// agentty::tools::util-ONE处agentty运行另一个程序。
//
// mcp-cpp询问(mcp::tools::Exec); jaal 提供原
// 语（maya::platform::posix_process，其出口是反应堆监视的句柄）。
// 这是介于两者之间的部分：政策。
//
// 原因 此 FILE EXISTS AT 全部
//
// 曾经有两个轮询循环——一个在mcp-cpp，一个在这里—
// —并且它们发生了漂移。一个安装了绝对的墙壁-clock天花板，
// 另一个只保留了一个闲置的天花板，并且由于每当沙箱打开时，
// agentty都会将ITS滑道安装到mcp-cpp中，
// 因此发货的配置是没有天花板的配置。从未停止打印的命令永远不会被收获。
// 该错误并不在任何一个循环中；那里有两个。
//
// 所以有一个，就在这里，以前重复的东西现在被WHO KNOWS
// THEM分割了：
//
//   jaal如何产生、如何等待、退出如何变得可读。
//            平台事实、一个一致性套件、多个后端。
//   这里等待多长时间，用完后做什么，保留多少输出。
//            策略，每个调用者都不同，
//            属于拥有该用户的应用程序。
//   mcp-cpp什么也没有。它表明了意图并显示为 字节。
//
// 该 两个 CLOCKS，这就是政策有趣的全部原因：
//
//   空闲界限SILENCE。每个 字节 都会重置，
//         因此无论运行多长时间，保持打印的构建都不会被中断。
//         这是一个很棘手的问题。
//   墙限制TOTAL TIME并且永不重置。这是一个捕获保持
//         喋喋不休的失控的时钟的时钟——空闲时钟在结构上不能
//         做到这一点，因为输出正是让它保持活力的原因。
//
// 单独两者都不够，并且这不是调整问题，
// 而是两者测量的内容。今晚的错误是只有第一个。

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <mcp/tools/host.hpp>

namespace agentty::tools::util {

/// 当请求未设置预算时应用默认值。
///
/// 故意慷慨。实际工作中的上限教会人们在任何地方都提高它，
/// 而每个人都禁用的限制并不是限制。
/// 墙默认值是从空闲墙衍生而来的，而不是固定的，
/// 因此请求长时间静默的调用者隐含地要求长时间运行。
///
/// `$AGENTTY_TOOL_HARD_TIMEOUT_SECS`
/// 覆盖该过程的挂钟默认值（0完全禁用挂钟）。
/// 没人能提高的上限本身就是一种破碎：合法工作需要四十
/// 分钟的一次会议不应该需要重建。
struct ExecDefaults {
    std::chrono::seconds idle{120};
    /// 墙=空闲×这个，地板在`wall_floor`。
    int                  wall_multiple = 20;
    std::chrono::seconds wall_floor{600};
    std::size_t          max_output_bytes = 8u * 1024u * 1024u;
    /// SIGTERM，然后这么长，然后SIGKILL。足够长，
    /// 足以让贝壳冲走并收获自己的孩子；足够短，不会显得悬挂。
    std::chrono::milliseconds kill_grace{2000};
};

/// 构建执行能力agentty交给mcp-cpp。
///
/// 当沙箱处于活动状态时被沙箱化。两种方式进入，一种观看方式：
/// claybin 克隆具有命名空间 标志，安装 seccomp 过滤器
/// 并进入 cgroup——其中没有一个可以表示为
/// "fork then exec this argv"——因此它产生，
/// 并且 jaal 采用它返回的 pid 和 pidfd。无论孩子以哪种方式开始，
/// 该行之后的所有内容都是相同的，包括固定退出语义的一致性套件。
///
/// 当策略委托系统调用时，seccomp代理在SAME
/// 反应器中被服务，等待作为输出和退出。内核会阻止来宾，
/// 直到有人回答代理的 调用，因此未轮询的侦听器是
/// 挂起而不是较弱的墙 - 并且已经涵盖三种就绪
/// 状态的等待是不为其增长 线程 的原因。
[[nodiscard]] std::shared_ptr<::mcp::tools::Exec>
make_exec(ExecDefaults defaults = {});

/// 在maya::scope上运行fn(0..n-1)，调用者获得份额0；
/// 每股在返回之前都会被加入。 库'分离器后面的风扇-out。
void scope_fan_out(std::size_t n, const std::function<void(std::size_t)>& fn);

// ── run_child: 监督环线的 ───────────────────────────────────────────
//
// agentty 运行完成的一切都经过这里：工具命令、
// 用于检查点的 git、密钥环助手、沙箱 探针、钩子。
// jaal 生成（或采用 沙箱 生成的内容）并使退出成为句柄；
// 这个循环是围绕它的策略：两个时钟，SIGTERM 然后 SIGKILL，
// 输出上限，进度，取消，seccomp 代理。

/// 沙箱开始了一个孩子，供run_child收养。 沙箱 获取 stdout
/// +stderr 的一个管道的写入端，并将其余部分交回。
struct AdoptedChild {
    int pid = -1;
    int pidfd = -1;
    int supervisor_fd = -1;                 ///< seccomp 侦听器，如果没有则为 -1
    std::function<bool()> service_broker;   ///< 回答一 通知
    std::function<bool()> kill_tree;        ///< cgroup.kill; false = 后退
    /// 沙箱必须在孩子的整个生命周期中保持活动的资源：
    /// 过滤的网络代理拥有访客的netns中的侦听套接字，
    /// 并且删除它会关闭访客下的该套接字。
    std::shared_ptr<void> keepalive;
    bool leads_own_group = true;
    std::string error;                      ///< non-empty = 从未开始
};

struct ChildRun {
    /// argv[0]为程序；除非 argv 这么说，否则没有 shell。
    std::vector<std::string> argv;
    /// Windows:命令行逐字(cmd.exe /S /C "...")，
    /// 而不是引用argv。其他地方被忽略了。
    std::string windows_command_line;
    std::string cwd;                                          ///<空=我们的
    std::vector<std::pair<std::string, std::string>> env;     ///< 分层在我们的之上
    /// 字节写入孩子的stdin，然后关闭。空 = /dev/null.
    std::string stdin_data;

    std::chrono::milliseconds idle{120'000};   ///<沉默预算； 0 = 无
    std::chrono::milliseconds wall{0};         ///<产卵上限； 0 = 无
    std::chrono::milliseconds kill_grace{2000};
    std::size_t max_output_bytes = 30'000;

    /// 到目前为止整个缓冲区，最多每~80毫秒一次并且在最后一次。
    std::function<void(std::string_view)> on_progress;
    /// 每次唤醒轮询（至少每100毫秒）； true 阻止了孩子。
    std::function<bool()> stop_requested;
    /// 给出迄今为止的输出； true 将子进程停止为 "done early"。
    std::function<bool(std::string_view)> stop_when;
    /// 相反，在 沙箱 内生成：接收输出管道的写入端（stdout
    /// 和 stderr 两者），返回其开始的内容。
    std::function<AdoptedChild(int out_fd)> spawn_adopted;
};

struct ChildResult {
    bool started = false;
    std::string start_error;
    std::string output;               ///< 标准输出+标准错误，原始 字节
    bool truncated = false;
    /// 它是如何结束的。其中之一恰好描述了这一点。
    bool exited = false;   int exit_code = 0;     ///<跑回来
    bool signalled = false; int signal = 0;       ///< 被我们或 OS 杀死
    bool timed_out_idle = false;
    bool timed_out_wall = false;
    bool cancelled = false;
    bool stopped_early = false;
    std::chrono::milliseconds elapsed{0};
};

[[nodiscard]] ChildResult run_child(const ChildRun& run);

/// 值得制作多少股：硬件线程。
[[nodiscard]] std::size_t fan_out_width() noexcept;

/// 分离器agentty将其交给mcp-cpp进行并行扫描(grep、结构搜索、
/// 仓库图、extract/aggregate)。
[[nodiscard]] ::mcp::tools::Splitter make_splitter();

}  // namespace agentty::tools::util
