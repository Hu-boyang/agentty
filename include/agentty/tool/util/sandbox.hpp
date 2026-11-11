#pragma once
// agentty::tools::util::sandbox：使用操作系统原生隔离。
// 沙箱层用于bash/诊断工具。
//
// fs_helpers.cpp门*声明的*路径中的工作区边界（`path` arg
// 到`read`、`write`、`edit`等）。它无法控制 shell 命令
// 在 运行时 实际执行的操作 - `bash "cd /etc && cat passwd"`
// 声明没有路径，运行通过 `Effect::Exec`，并且（在 Ask 下）
// 提示词 用户一次用于 "this bash call." 这是错误的粒度：
// 在一次批准之后，模型可以在OS过程边界内做任何
// 事情。
//
// 该模块将shell命令包装在OS-native 沙箱中，
// 因此即使批准的bash 调用也被限制于工作区 +系统库
// +网络。后端：
//
//   Linux — claybin (供应 库，无需安装 二进制程序)：可用
//             的用户命名空间，加上 landlock、seccomp-bpf 和
//             cgroup2。无需root，无需安装包。
//   macOS — 沙箱-exec (自 10.5 起内置)。 Apple 已
//             弃用公共文档，但 二进制程序 仍可在 14+ 上运行并采用
//             Scheme-style 配置文件。
//   Windows — 无 -op。
//             AppContainer + 作业对象是正确的基元，
//             但其表面足够大，因此正确使用值得拥有自己的里程碑。记录差距。
//
// 该模块是 opt-in 经由 `--sandbox=on` / opt-out 经由 `--sandbox=off`
// / smart-default `--sandbox=auto` (如果可用则使用，否则警告)。
// 未设置的CLI 标志是自动的。
//
// 工具 调用 `run_shell_command(...)` 直接代替
// `run_command_s(...)` — 与 SubprocessResult 形状相同，
// 但当 沙箱 处于活动状态时，shell 命令被透明包装。 `is_active()`
// / `describe_state()` 公开用于状态表面（横幅、"agentty status"、
// README 文档）。

#include <chrono>
#include <cstdint>
#include <expected>
#include <memory>   // shared_ptr — config_snapshot 的返回类型
#include <string>
#include <string_view>
#include <vector>

#include "agentty/domain/sandbox_config.hpp"
#include "agentty/tool/util/sandbox_claybin.hpp"
#include "agentty/tool/util/subprocess.hpp"

namespace agentty::tools::util::handoff { struct TrustedSnapshot; }

namespace agentty::tools::util::sandbox {

enum class Mode : std::uint8_t {
    Off,    // 明确禁用，从不换行
    Auto,   // 如果后端可用则换行，否则失败并发出警告
    On,     // 如果可用则换行，如果后端缺失则启动失败
};

enum class Backend : std::uint8_t {
    None,         // 未检测到后端/沙箱 已禁用
    Claybin,      // Linux，在-process中通过claybin 库
    SandboxExec,  // macOS 沙箱-exec
};

// 有ONE Linux发动机，其为claybin。
//
// bwrap曾经是第二个可选择的后端。它被删除了，
// 并且事实证明保留它的论点在它所描述的代码中无法幸存。
// 该论点是：两个引擎在 不同 主机上失败，因此在没有内锁的内核（旧 RHEL）上
// 删除 bwrap 会用较弱的 沙箱 换取无 沙箱。但 claybin 的可用性
// 下限是 `landlock_abi > 0 || seccomp` ——
// 任何一个都足够了 —— seccomp-bpf 落在 Linux 3.5 中，
// 而非特权用户命名空间 bwrap REQUIRES 落在 3.8 中。因此，
// 每个可以运行 bwrap 的主机都有 seccomp，因此有 claybin。
// 争论所辩护的主机不存在。
//
// claybin给出了bwrap路径从未做过的、测量的而不是争论的(
// tests/sandbox_audit.cpp)：
//
//     syscall.filter 使用 seccomp-bpf；bwrap 没有对应机制。
//     resource.mem/cpu/pids 使用 cgroup2，与 bwrap 的能力对比。
//     NONE filesystem.exec landlock vs bwrap 部分
//     ptrace/kill 经纪人 seccomp-notify vs bwrap NONE
//
// bwrap没有获胜的能力，这就是为什么它是回退而不是默认值，
// 以及为什么它的运行不会丢失任何东西。

// 安装用户的已保存 沙箱策略。 调用 一次，在启动时，
// 在 init() 之前：策略决定探测器应该检查什么，
// 因此请求此内核无法构建的墙的配置在启动时会被捕获，可以报告它，
// 而不是在看起来像损坏工具的 per-command 中。
//
// 第一个 调用 之后的 SEALED——稍后的 调用 被忽略并记录警告。
// 沙箱政策是agentty中NOT生效的一项设置，
// 并且这是一种故意交易而不是实施限制。
//
// 可以在-session中移动的边界仅在WEAKENING方向上有用地移动。
// 这种削弱是看不见的（放松策略会默默地放弃 seccomp 规则、
// cgroup 上限或秘密掩码）并且无法撤消，
// 因为 re-tightening 不会取消 -read
// 已留下的密钥。它还使得 沙箱 无法审计：一个会话中的两
// 个命令会获得两个不同的墙，没有任何记录哪个获得了哪个。
//
// 因此，面板写入磁盘以用于NEXT启动，并且重新启动是应用步骤。
// agentty 中的所有其他设置均保持有效。
void set_config(const sandbox_cfg::Config& cfg);

// 保单是否已被密封（i.e.set_config是否已运行）？
// 沙箱 面板 会询问，以便它可以告诉用户编辑将在重新启动时应用，而不是现在。
[[nodiscard]] bool config_sealed() noexcept;

// 打破封印。 TESTS 仅——生产代码没有必要调用这个。
//
// 值得精确地了解密封件是什么和不是什么，因此这个钩子
// 不会被误认为是其中的孔。该密封可防御特定的、真实的错误：
// 设置 状态更新函数 在实时进程上调用 set_config()，
// 这就是 面板 的运送方式，并且默默地移动了边界 mid-session。
// 这是在代码中强制执行的 不变量，因为与评论相同的规则持续了
// 一天。
//
// NOT是针对任意in-process代码的防御。
// 任何能够实现 调用 agentty 自身功能的东西都已经获胜—
// —它可以在完全没有 沙箱 的情况下产生。因此，一个明显的
// -named 测试钩子对于实际解决的威胁没有任何成本，
// 而替代方案（每个案例一个新的过程）将使密封行为本身无法测试。
//
// 命名为for_test，如下面的claybin_posture_for_test，
// 因此`_for_test`的grep找到仅针对该套件存在的每个接口边界。
void reset_config_for_test() noexcept;

// 该政策已生效。通过设置 面板 读取以播种其形式，
// 并通过任何想要报告姿势的内容读取。
//
// BY 值，不是通过引用，也不是可选的：当工具 线程
// 正在读取策略时，策略被重新发布（参见 set_config），
// 因此对实时值的引用将是带有延迟的悬空指针。
// 调用者是表单生成器和状态行；副本是三个小向量。
[[nodiscard]] sandbox_cfg::Config config();

// 该策略为共享，不可变 快照。
//
// 这是 WORKER 线程 上的任何内容都应该使用的内容，
// 以及生成路径使用的内容：在命令顶部取一个 快照 并从中
// 读取整个策略。这比 config()（引用计数，无副本）便宜，
// 并且是获得连贯读取的唯一方法 - 在构建姿势的过程
// 中 保存 着陆会将两个策略混合到一个 沙箱 中。
//
// 决不为空；在任何人之前 调用 set_config 它是默认配置，
// 这是当没有人收紧任何东西时 claybin 建立的姿势。
[[nodiscard]] std::shared_ptr<const sandbox_cfg::Config> config_snapshot() noexcept;

// ACTIVE后端是否能够实际执行已保存策略。
//
// 只有claybin可以。该策略的词汇表——系统调用配置文件、per-port 网络规则、
// W^X、cgroup2 caps、deny-under-grant 路径掩码
// ——是 claybin 的； macOS 沙箱-exec 大部分都没有拼写，
// 而 Windows 根本没有后端。因此，在 Linux 之外，面板 正在配置一些惰性
// 的东西。
//
// 这必须是SAID而不是默默地容忍：用户认为他们设置的并且
// 没有运行的安全控制比他们知道他们没有的安全控制更糟糕。
// main.cpp 在启动时发出警告，面板 将其显示为内联。
// 当没有要执行的策略（没有人配置策略）时返回真，
// 因为那时没有任何内容被忽略。
[[nodiscard]] bool config_enforceable() noexcept;

// 设置所请求的模式(从--sandbox CLI 标志)
// 并探测系统以获得可用的后端。 幂等；结果被缓存。
// 当 mode == On 并且未找到后端时返回 false —
// 调用者 (main.cpp) 应该大声失败而不是默默地放弃隔离。
[[nodiscard]] bool init(Mode requested);

[[nodiscard]] Mode    requested_mode() noexcept;
[[nodiscard]] Backend detected_backend() noexcept;

// 当 沙箱 和 和 都被请求时为真，后端可用 — i.e。
// 当 wrap_shell_command 真正换行时。
// 用于状态横幅和在较短路径上跳过换行（还没有，
// 但 标志 是明显要查询的路径）。
[[nodiscard]] bool is_active() noexcept;

// 工作区是用户的主目录吗？然后每个点文件都可以写入沙盒命令（登录文件，
// ~/.local/bin）。 沙箱 面板 发出警告。
[[nodiscard]] bool workspace_is_home();

// Single-line描述启动横幅/状态命令的当前状态。
// 示例：
//   "sandbox: active (claybin)"
//   "sandbox: off"
//   "sandbox: requested but no backend (kernel has neither landlock nor seccomp)"（请求启用沙箱，但内核既不支持 Landlock 也不支持 seccomp，没有可用后端。）
[[nodiscard]] std::string describe_state();

// 运行 shell 命令，将其包装在活动沙箱（如果存在）中。
// 与 util::run_command_s 形状相同 —
// drop-in 替代 bash 工具。当沙箱关闭/不可用时，
// 会进入正常的子进程运行程序，因此行为会被保留。
[[nodiscard]] SubprocessResult run_shell_command(
    std::string_view cmd,
    std::size_t max_bytes,
    std::chrono::seconds timeout);

// argv-form 变体 用于已经构建了 带类型的 argv (e.g.
// `diagnostics`调用`cmake --build build`)。
// 与 shell 变体 相同的换行策略：活动时应用 沙箱，否则不应用 -op。
// 不经过`sh -c`就换行，保留对于诸如带有引号/
// `$vars`的提交消息之类的事情重要的精确argv语义。
[[nodiscard]] SubprocessResult run_argv(
    const std::vector<std::string>& argv,
    std::size_t max_bytes,
    std::chrono::seconds timeout);

// 将用于安全插值的路径转义到macOS 沙箱-exec (
// SBPL)配置文件字符串中。暴露用于测试注入的-hardening：
// `\`和`"`是反斜杠-escaped；包含控制字符（SBPL
// 字符串文字无法表示）的路径会生成 空 字符串，
// 因此调用者会忽略该子句并导致关闭失败。不带特殊字符的路径将原样返回。
// 在非 -Apple 构建上，这是相同规则的简单传递（保留
// cross-platform，以便到处测试 编译）。
[[nodiscard]] std::string sbpl_escape(std::string_view path);

// 在活动的 沙箱 内生成 `argv`，数据流 都位于 `out_fd` 上。
//
// 姿势是构建 此处 而不是分发的，因为构建它是 沙箱 的工作，
// 而调用者组装自己的姿势将是第二个策略。 exec.cpp 曾经通过
// claybin_posture_for_test() 来执行此操作，
// 这有效但是一个谎言：生产路径中的 `_for_test` 接口边界。
//
// `cwd`被沙箱应用为真正的目录更改，而不是作为shell
// 字符串上的`cd ... &&`前缀——前缀是shell的re
// -parsed并被边界拒绝，这就是当相同的前台命令工作时后台会话因
// "cd: Operation not permitted"而终止的
// 方式。
//
// 当没有 claybin 后端处于活动状态时，返回started=false，并给出原因。
// `extra_roots`可在工作区(编辑器会话的附加文件
// 夹)之上写入。当调用者已经获取了 工作区 可信路径的 快照 时，
// `trusted` 会重新用于 read-only 绑定，
// 而不是再次遍历 源码树。
[[nodiscard]] claybin_backend::SpawnResult spawn_in_sandbox(
    const std::vector<std::string>& argv, std::string_view cwd, int out_fd,
    const std::vector<std::string>& extra_roots = {},
    const handoff::TrustedSnapshot* trusted = nullptr,
    const std::vector<std::pair<std::string, std::string>>& env = {});

#if defined(__APPLE__)
// macOS对应于jaal执行路径的spawn_in_sandbox。
// jaal 无法收养其他人在 darwin 上分叉的孩子（没有 pidfd，
// 因此没有可观察的退出），因此不是交还 pid，而是交还要生成的 argv：
// `/usr/bin/sandbox-exec -p <profile> <argv...>`。
// 当 claybin 是后端时，配置文件是 claybin 的编译姿势 - 与
// spawn_shell 相同的 read/write 路径、掩码和额外根 - 以及当 沙
// 箱-exec 时的旧版固定配置文件。当无法创建配置文件时出现错误（绝不是裸露的argv）：
// 调用者必须失败关闭。
[[nodiscard]] std::expected<std::vector<std::string>, std::string>
wrap_argv_for_exec(const std::vector<std::string>& argv, std::string_view cwd,
                   const std::vector<std::string>& extra_roots = {});
#endif

// 测试挂钩：该过程将从现在的密封配置快照构建REAL claybin姿势，
// 包括sandbox.cpp中的非-configurable掩模和工作区扫描。
// 非 -Linux 版本上的 Empty/default。
[[nodiscard]] claybin_backend::Posture claybin_posture_for_test();

// 当命令运行需要 RWX 页的 JIT 运行时 (node、npm、
// deno、bun、java、...)时为真。在严格以下，这样的命令会解除
// W^X，因为否则它无法启动。按基本名称匹配 shell 字符串的任何单词。
[[nodiscard]] bool command_needs_jit(std::string_view cmd) noexcept;

} // namespace agentty::tools::util::sandbox
