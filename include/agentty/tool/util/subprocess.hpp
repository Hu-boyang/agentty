#pragma once
// Cross-platform子进程运行器。
//
// 为什么使用 class-with-Options 而不是自由函数：我们
// 已经有了三个重叠的运行程序（shell/argv/legacy-string-wrapper），
// 每个运行程序都有自己的截断/超时/进度逻辑。通过合并到一个入口点，
// 每个工具都可以选择它所关心的旋钮，而无需为每个 调用 站点
// 复制 200 行 Win32 管道管道。
//
// 平台细节：
//   Windows→CreateProcessW，其中标准输入
//             重定向到NUL(防止孩子窃取击键)并且标准输入的控制台
//             模式已保存+恢复(防止孩子重置ENABLE_LINE_INPUT
//             破坏TUI输入)。读取器 线程 耗尽管道，
//             因此继承 stdout 的孙子不能等待 死锁。
//   jaal的posix_process上的POSIX→run_child(
//             exec.hpp)：工具使用相同的监督循环。外壳形式为sh -c；
//             argv 直接形成执行程序。空闲和挂钟，然后SIGTERM，恩典，
//             SIGKILL到源码树。 stdin 是 /dev/null 和 stdout
//             +stderr 共享一个管道。
//
// 两条路径数据流最多每~80ms通过线程-local进度接收器（
// 参见agentty/tool/registry.hpp)捕获字节，
// 因此UI显示实时输出而不淹没事件队列。

#include <chrono>
#include <functional>
#include <optional>
#include <variant>
#include <string>
#include <string_view>
#include <vector>

namespace agentty::tools::util {

struct SubprocessOptions {
    // WHAT运行——恰好是一种形式，由类型选择。
    //
    //   Argv{...} 直接执行，无 shell：路径、
    //              引用、提交消息和格式字符串完好无损。
    //   Shell{...}遍历cmd.exe/sh并获取其引用规则。
    //
    // 这是两个独立的选项，
    // 带有 "exactly one of shell_command / argv must be set"
    // 的注释。两个选项描述了 FOUR 状态 - 两个集合和两个集合都
    // 同样可构造 - 并且没有任何强制执行规则：运行者是
    // `if (shell) … else if (argv) …` 链，
    // 因此 "both" 默默地运行外壳形式，而 "neither"
    // 掉到底部。对于产生进程的 API 来说，shell 和 exec
    // 之间的区别是安全边界，不应该被含糊地表达。
    struct Argv  { std::vector<std::string> args; };
    struct Shell { std::string command; };
    using Command = std::variant<Argv, Shell>;
    Command command{Argv{}};

    // 为了方便读者，只关心一种形式的调用
    // 网站不再手工解压变体。
    [[nodiscard]] const std::vector<std::string>* argv_if() const noexcept {
        if (auto* a = std::get_if<Argv>(&command)) return &a->args;
        return nullptr;
    }
    [[nodiscard]] const std::string* shell_if() const noexcept {
        if (auto* s = std::get_if<Shell>(&command)) return &s->command;
        return nullptr;
    }

    // `timeout`是IDLE 看门狗：它仅在SILENCE的这么
    // 多秒之后触发，因此保持打印进度的健谈的build/test永远不会在-flight中期被杀死。
    // 这是编码代理的正确默认值——20-minute
    // 测试运行应该完成。
    //
    // 但沉默并不是上吊的唯一方法。永远保持闲聊的失控（`yes`，
    // `tail -f`，进度循环）在每个 字节 上将空闲截止
    // 日期向前滚动，并且永远不会收获，一旦达到输出上限，
    // 读取器就会丢弃它读取的所有内容，因此它甚至不受 内存 的限制。
    // `hard_timeout` 是出生后永不重置的绝对上限。
    // 0 派生出一个慷慨的（20 倍空闲，在 10 分钟时停止），
    // 因此仅设置 `timeout` 的调用者仍然可以获得后退，
    // 而没有任何实际的构建或测试套件接近它。
    //
    // 这反映了mcp-cpp的赛跑者场地。每当 沙箱 打开时，
    // agentty 都会将 ITS 运行器安装为
    // mcp-cpp 的主机 沙箱，因此默认配置中会绕过那里写的
    // 上限 - 两者必须达成一致，否则保护仅存在于纸面上。
    std::chrono::seconds timeout{120};
    std::chrono::seconds hard_timeout{0};
    // 未签名，因为负上限没有意义，并且每个比较位点已经是
    // RHS 上的 `size_t`；旧的`int`导致
    // 混合-sign促销和偶尔的sign-compare警告。
    std::size_t          max_bytes = 30'000;

    // 在 best-effort 油门上用完全累积的 缓冲区 (不是
    // 增量) 来调用。每次传递整个 缓冲区 意味着跨越管道的 multi
    // -byte UTF-8 序列在下一次刷新时仍然正确读取 渲染
    // — 不需要在调用方进行 delta-aware 拆分。
    std::function<void(std::string_view snapshot)> on_progress;

    // 可选的协作停止探针。  运行程序在每次 Supervisor
    // -loop 迭代中最多检查一次，并在 true 时终止子进程。
    // 这使得诸如提供商桥接器之类的调用者能够响应UI 取消，
    // 而不将该低-level实用程序耦合到特定令牌类型。
    std::function<bool()> stop_requested;

    // fork/exec步骤ONLY的可选替换。
    //
    // 沙箱是LIBRARY而不是二进制程序，不能被表达为argv
    // 前缀：claybin必须分叉，
    // 在子项中应用namespace/landlock/seccomp计划，
    // 并在那里执行。但之后的一切——轮询管道、限制进度、强制执行空闲期限、
    // SIGTERM 然后 SIGKILL、收获——与正常路径相同，
    // 并且是容易出错的部分。每个后端复制它意味着硬半部分的
    // 两次实现。
    //
    // 因此，后端仅提供spawn：它接收运行器已经创建的
    // 管道的写入端，将其连接到子进程的stdout+stderr，
    // 并返回pid。跑步者保留读取结束、
    // 截止日期和收获的所有权。
    //
    // 返回pid为-1(设置了`error`)是启动失败并且表面为start
    // ==false，与posix_spawn失败完全相同。
    struct SpawnedChild {
        int pid{-1};
        int pidfd{-1};   // 获取：设置后，跑步者通过它收获
        std::string error;

        // 系统调用主管，当沙箱代理调用时。
        //
        // 存在这种情况是因为 seccomp user
        // -notification 使 CHILD 的进度取决于
        // PARENT 应答：内核阻止代理的系统调用，
        // 直到有人在侦听器上做出响应，因此没有被轮询的主管是挂起，而不是较弱的墙。
        // 下面的监督循环已经拥有子进程的生命周期并且已经进行轮询，
        // 因此这是监听器必须居住的地方——其他地方的第二个
        // 循环将为同一个子进程提供竞态这个循环。
        //
        // 两个字段而不是 回调 占用 fd，因此运行程序可以将描述
        // 符放入其自己的 pollfd 集中，并且在实际可读时仅
        // 将 调用 `service` 放入。在接收上阻塞的 回调
        // 会停止标准输出的耗尽和空闲的 看门狗。
        //
        //   当来宾 线程 在代理系统调用上被阻止时，
        //                  supervisor_fd 准确可读。 -1 表示
        //                  没有任何中介，这是常见情况并且不需要任何费用。
        //   服务答复ONE 待处理 通知。当侦听器完成时
        //                  返回 false（来宾消失，fd 关闭），
        //                  之后运行程序停止轮询它。
        //
        // 运行者从不解释其中任何一个：决策意味着
        // 什么是沙箱的业务，并且将该知识保留在子流程
        // 层之外是阻止其成为第二策略引擎的原因。
        int supervisor_fd{-1};
        std::function<bool()> service;

        // 只要子进程运行，生成器就必须保持活动的任何东西。
        //
        // claybin后端的过滤网络代理是这种情况：
        // 它拥有访客网络命名空间中的监听套接字，
        // 并且如果最后一个引用死亡，则套接字关闭并且访客
        // 的连接开始失败——这正是当生成器lambda
        // 返回并将其扔到地板上时发生的情况。
        //
        // 故意不透明(shared_ptr<void>)：跑步者的
        // 工作是握住它，而不是知道它是什么。当孩子被收割时，
        // 连同其他一切都被释放。
        std::shared_ptr<void> keepalive;

        // 如果生成器有比向进程组发送信号更好的方法，
        // 则停止整个进程源码树。
        //
        // killpg是跑步者落回的地方，它有一个洞：调用setsid
        // ()离开群体并幸存的后代。 shell re-parented
        // 的任何操作也是如此。将 guest 虚拟机放入自己的
        // cgroup 中的 沙箱 可以准确地关闭该漏洞 -
        // cgroup.kill 是整个子树上的单个原子内核操作，
        // 没有 PID 列表到 竞态 并且无处可逃。
        //
        // 如果无法做到这一点，则返回 false，然后运行程序像以前
        // 一样使用进程组。空意味着 "nothing better available"，
        // 这是未沙盒生成的正常情况。
        std::function<bool()> kill_tree;
    };
    // (命令，pipe_write_fd)->子。 `command` 是已解析的
    // shell 字符串或 argv，已由上面的 变体 选择。
    std::function<SpawnedChild(const SubprocessOptions&, int pipe_write_fd)> spawner;
};

struct SubprocessResult {
    std::string output;                // 捕获stdout+stderr，UTF-8有效
    int  exit_code   = 0;
    bool timed_out   = false;
    // 当是 WALL-CLOCK 天花板而不是沉默阻止它时，确实如此。
    // 两者想要相反的建议：空闲超时意味着"it hung, look at why"，
    // 硬上限意味着"it was still working, give it longer or run it in the background"。
    // 对两者都说 "timed out" 会向模型
    // 发送调试命令，该命令没问题。
    bool hard_capped = false;
    bool truncated   = false;
    bool started     = true;           // false iff 生成本身失败
    std::string start_error;           // 启动时填充==false
};

struct Subprocess {
    [[nodiscard]] static SubprocessResult run(SubprocessOptions opts);
};

// ── 周遭的方便包装 Subprocess::run ─────────────────────────
//
// `run_command_s`采用shell字符串，`run_argv_s`采用pre
// -built argv(无shell)。 `_s` 后缀是对 -refactor 之前时代的回溯，
// 其中非 `_s` 版本返回 "legacy_format" 后缀字符串形状；保留为 调用
// -site grep-ability。

[[nodiscard]] SubprocessResult run_command_s(
    const std::string& cmd,
    std::size_t          max_bytes = 30'000,
    std::chrono::seconds timeout   = std::chrono::seconds{120});

[[nodiscard]] SubprocessResult run_argv_s(
    const std::vector<std::string>& argv,
    std::size_t          max_bytes = 30'000,
    std::chrono::seconds timeout   = std::chrono::seconds{120});

// 将SubprocessResult展平为遗留后缀-marker字符串形状：
//   <output>[\n[输出被截断]][\n[Xs 后超时] | \n[退出代码
// N]] 解析 从捕获的 blob 中退出代码的工具（e.g.git_commit 的
// `out.find("[exit code")` 防护）依赖于此格式 - 在未
// 审核每个调用者的情况下请勿更改它。
[[nodiscard]] std::string legacy_format(const SubprocessResult& r,
                                        std::chrono::seconds timeout);

[[nodiscard]] std::string run_command(
    const std::string& cmd,
    std::size_t          max_bytes = 30'000,
    std::chrono::seconds timeout   = std::chrono::seconds{120});

[[nodiscard]] std::string run_argv(
    const std::vector<std::string>& argv,
    std::size_t          max_bytes = 30'000,
    std::chrono::seconds timeout   = std::chrono::seconds{120});

} // namespace agentty::tools::util
