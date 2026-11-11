#pragma once
// agentty::ui::panel::Sandbox — 沙箱 面板。
//
// 类似于外观和检索的表格面板：面板保存form::Form和行ID，
// 因此navigation/dropdowns/editing保持共享状态更新函数的工作。
// 真正具体的东西在这里存在。
//
// ── 为何这样 面板 和所有特工的不一样 沙箱 配置 ───
//
// 每个人的 沙箱 设置都是您设置一次后就再也不会看到的开关列表，这就是实际的问题。
// 你无法观察到的边界就是你不能信任的边界：故障模式不是"the switch was wrong"，
// 而是"the switch said active and nothing was enforced"。
// agentty 已经发布了该错误（问题＃21："sandbox: active"，
// 而每个命令都因 uid_map 拒绝而终止），
// claybin 自己的探测器也是如此，直到它被修复。
//
// 所以这个 面板 做了其他代理没有做的三件事，
// 它们是它存在的原因而不是设置行：
//
//   1. IT SHOWS 该 WALLS，LIVE。 claybin 编译 将政策纳入计划并
//      根据能力报告，强制执行是`strong`、`partial`还是
//      `none`——并且WHICH机制得到了它（内锁ABI 6、
//      cgroup2 memory.max、seccomp-bpf）。
//      该报告是证明-carrying：后端不能声称其未安装的保证。
//      面板 渲染 它，所以改变一行会显示墙移动。没有其他人可以
//      做到这一点，因为你掏出的 沙箱 没有报告可提供。
//
//   2. IT SHOWS 内容 WAS BLOCKED。 seccomp-notify 让主管看到
//      发生时拒绝系统调用。因此 面板 可以说
//      "3 blocked: ptrace, clone(CLONE_NEWUSER), /etc/shadow"，（三项被阻止：ptrace、clone(CLONE_NEWUSER)、/etc/shadow。）
//      而不是让您猜测构建失败的原因。出现故障的 沙箱 会默默地训练
//      人们将其关闭；一个讲述它停止了什么的人告诉他们他们的工具实际上
//      是做什么的。
//
//   3. IT CAN LEARN A POLICY。在观察下运行您的工作负载，并且
//      沙箱记录它真正需要的——哪些路径、哪些端口、
//      哪些系统调用——然后提供仍然有效的MINIMAL策略。
//      这是人们运行非沙盒代理的真正原因的答案：
//      并不是他们想要冒险，而是手工编写正确的 允许清单
//      很痛苦，并且首先会破坏他们的构建三次。
//
// ── 这里的一切 claybin ───────────────────────────────────────────
// Per-port 网络、系统调用过滤、cgroup2 上限和
// 保证报告都是只有当您是 COMPILING 保单时才能要求的东西。
// bwrap 作为后端仍然可用，但它无法回答这些问题，
// 因此 面板 诚实地说明它可以强制执行哪些行，并按行说明。

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "agentty/domain/sandbox_config.hpp"
#include "agentty/runtime/panel/form.hpp"
#include "agentty/runtime/panel/filtered_picker.hpp"   // 路径完成
#include "agentty/workspace/files.hpp"                 // 候选集

namespace agentty::ui::panel {

// 行ID。构建和读取站点上的命名常量，
// 原因是 appearance.hpp 给出：两个匹配的字符串文字之间
// 的拼写错误是一个无声的死亡设置，编译器 无法看到它。
//
// 故意存在NO `mode`行。关闭 沙箱 是一个启动决定 (`--sandbox off`)，
// 而不是一个设置：已保存 "off" 是一个 foot-gun，它可以在重新启动后
// 默默地生存，并且 面板 无论如何都无法更改实时边界（策略在启动时被密封 -
// 请参阅 tool/util/sandbox.hpp）。不能存在的行的行 ID 是
// 对 传输 的邀请，它是 "for consistency"，所以在这里找不到它。
// 请参阅 docs/design/ sandbox-boundary.md
// §3。
inline constexpr std::string_view kSbPosture     = "posture";
inline constexpr std::string_view kSbFsScope     = "fs_scope";
inline constexpr std::string_view kSbReadPaths   = "read_paths";
inline constexpr std::string_view kSbWritePaths  = "write_paths";
inline constexpr std::string_view kSbDenyPaths   = "deny_paths";
inline constexpr std::string_view kSbNetMode     = "net_mode";
inline constexpr std::string_view kSbPorts       = "allow_ports";
inline constexpr std::string_view kSbHosts       = "allow_hosts";
inline constexpr std::string_view kSbSyscalls    = "syscall_mode";
inline constexpr std::string_view kSbWxProtect   = "wx_protect";
inline constexpr std::string_view kSbMemoryMb    = "memory_mb";
inline constexpr std::string_view kSbMaxProcs    = "max_procs";
inline constexpr std::string_view kSbCpuPercent  = "cpu_percent";
inline constexpr std::string_view kSbOpenFiles   = "max_open_files";
inline constexpr std::string_view kSbWallClock   = "wall_clock_secs";
inline constexpr std::string_view kSbCpuSecs     = "cpu_secs";
inline constexpr std::string_view kSbTmpMb       = "tmp_mb";
inline constexpr std::string_view kSbScopeIpc    = "scope_ipc";
inline constexpr std::string_view kSbCloseFds    = "close_fds";
inline constexpr std::string_view kSbFakeHost    = "fake_hostname";
inline constexpr std::string_view kSbMaskDepth   = "mask_scan_depth";
// 捍卫逃逸类的行没有其他人捍卫。
// 请参阅 domain/sandbox_provenance.hpp 了解为什么它是它自己的控件。
inline constexpr std::string_view kSbHandoff     = "handoff";

// ── 活墙报告 ────────────────────────────────────────────────
//
// 一种能力以及实际执行它的内容，直接来自claybin的GuaranteeReport。
// `mechanism` 是 加载-bearing 字段："strong" 单独是一项索赔，
// "strong via landlock abi 10" 是附有收据的索赔。
struct Wall {
    std::string name;       // "filesystem.write"，"network.isolation"，……
    std::string strength;   // "strong" | "partial" | "none"
    std::string mechanism;  // "landlock abi 10"、"cgroup2 memory.max"、……

    // 对于该能力，政策从来没有ASKED，所以这里的`none`是
    // 一个选择而不是一个差距。
    //
    // 如果没有这个，页脚就会在默认策略上列出
    // `resource.memory: none (none)`——其中
    // memory_mb = 0 意味着 "no cap wanted"，
    // 自从 sandbox_config.hpp 决定正确的上限是机器的属性以来。
    // 三个这样的行位于真正的降级旁边，并教会用户将整行视为噪音，
    // 这与墙上报告的用途相反。
    bool not_requested = false;
};

// 面板了解当前策略，每当行改变时就重新计算。这是 COMPILE，
// 而不是衍生：claybin 的 compile() 是纯粹的
// 所描述的主机，因此 面板 可以在不运行任何内容的情况下
// 显示编辑的结果。
struct Preview {
    bool                compiled = false;
    std::string         error;        // 为什么不编译时！
    std::vector<Wall>   walls;
    std::size_t         plan_ops = 0; // 沙箱执行多少次操作
    // 该主机无法强制执行的策略 ASKED 的功能。永远不要默默
    // 地清空——如果用户在具有内锁 ABI 3 的内核上请求
    // per-port 网络，面板 会这么说，而不是悄悄降级。
    std::vector<std::string> unenforceable;
};

// ── 故意还没到 ─────────────────────────────────────────────
//
// 这两个中的一个已经被构建，并且注释被更新而不是被删除，
// 因为推理仍然是规则：
//
//   观察 — "learning mode" 许可地运行并记录 授权
//                   所需要的内容，因此 `npm install`
//                   一次将 允许清单 交给您，而不是一系列失败。
//                   尚未实现。
//   BlockedEvent — 沙箱实际停止的内容的提要。 BUILT：它
//                   位于 tool/util/sandbox_broker.hpp
//                   （seccomp-notify 否认），其同级位于
//                   tool/util/handoff_gate.hpp，
//                   用于信任切换，以及 面板 页脚 渲染。它没有在这里声明，
//                   因为生产者拥有该类型 - 面板 读取提要，但没有定义它。
//
// 将结构保持在该 头文件 之外直到有东西填充它们的规则：具有 状态更新函数
// 臂且没有生产者的类型与 kSbMode 的陷阱相同。它读起来就像是
// 一个已交付的功能给下一个人，这使得 面板 看起来像是它报告了它不能报告的事情。
// 有关学习模式的实施说明，请参阅 docs/design/sandbox-boundary.md
// §13；有关提要，请参阅 §14。
//
// 学习模式一旦落地就会发出警告：运行时是WEAKER 沙箱，
// 无声地削弱边界的模式比没有模式更糟糕。
// 它必须是响亮的，并且不能是默认的。

// 主机可以做什么，ACTUALLY现在正在执行什么。
//
// ── 为什么这是个结构而不是三个参数 ────────────────────
//
// 因为将其中的两个混为一谈带来了这个 面板 所具有的最严重的错误。
// `build_sandbox_form`过去只接受`claybin_available`，
// 所以范围内唯一的事实是SELECTED引擎以及主机COULD是否运行它。
// 副标题是由这两个构建的 - 因此在您移动该行时立即宣布
// "claybin \xc2\xb7 landlock abi 10 \xc2\xb7 seccomp \xc2\xb7 cgroup2"，
// 该进程仍在运行 bwrap 并且无法更改。
//
// 这正是问题#21，re-entered穿过前门：一个面板，
// 旨在阻止"sandbox: active"毫无意义，声称没有竖起的墙。
// 缺失的事实从未在视图可以看到的任何地方进行计算——
// `SandboxPane::backend`持有它并且根本没有读者。
//
// 因此，正在运行的引擎是能力探测器旁边的NAMED FIELD，
// 并且每个调用者都必须提供它。没有任何重载可以忽略它。
struct HostFacts {
    // claybin可以从这里开始吗？一个真正的探针（它分叉并尝试 uid_map
    // 写入），而不是猜测。
    bool claybin_available = false;
    std::uint32_t landlock_abi = 0;
    // 引擎可以按端口过滤网络吗？ Linux：内陆 ABI 4+。
    // macOS:安全带端点过滤器。
    bool per_port_net = false;
    // 抽象套接字和信号是否保存在沙箱内部？ Linux：
    // 内陆ABI 6+。 macOS：安全带范围信号并且没有抽象
    // 套接字。
    bool ipc_scoped = false;

    // 什么是执行RIGHT 当前。在启动时密封，
    // 并在进程的生命周期内密封 不可变 ——这就是它必须与选择
    // 分开的全部原因：行可以更改，而行不能更改。
    sandbox_cfg::LinuxBackend running = sandbox_cfg::LinuxBackend::Claybin;

    // 当根本不存在后端时为假（探测失败，或--sandbox关闭）。
    // `running`则毫无意义，并且副标题必须这么说，
    // 而不是命名不限制任何东西的引擎。
    bool sandbox_active = false;

    // 用户通过了`--sandbox off`。
    //
    // 与`!sandbox_active`不同，
    // 区别在于：FAILED到沙箱是一个需要报告的问题的主机，
    // 而要求不被沙箱化的用户是一个值得尊重的选择。 面板 对每
    // 个都说了不同的事情，并且切换门仅在第二个时才会关闭。
    bool mode_off = false;

    // 工作区是用户的主目录。那么每个点文件都是可写的，
    // 包括登录文件和~/.local/bin，面板也是如此。
    bool workspace_is_home = false;

    // 最后的 trust-handoff 快照 达到其进入上限，
    // 因此不会报告低于削减的写入。之前已登录；现在显示在行上。
    bool handoff_truncated = false;
};

// ── 路径列表编辑器 ────────────────────────────────────────
//
// 路径行曾经是包含 "a, b, c" 的单个文本字段。
// 这是一种伪装成接口的序列化格式：条目没有可见的边界，
// 杂散逗号默默地重写策略，到达四个路径中的第三个意味
// 着光标会越过其他三个路径。对于一个其全部目的是
// 易读性的安全控件来说，它的形状是错误的。
//
// 这是作为LIST的列表：每行一个条目，添加和删除作为离散行为，
// 每个条目都有其自己的可编辑字段。
//
// 它重用 form::Form 而不是发明一个小部件，因此导航、
// 文本编辑、光标 和 commit-on-exit 都是免费的，
// 并且行为与其他地方完全相同。 ARE 行是条目——它们
// 后面没有模型会漂移不同步。
struct SandboxListPane {
    // 哪个父行正在被编辑，因此提交知道要写回哪里。
    // 通过 id 而不是索引：每次重新项目
    // 都会重建父表单，之后索引将寻址不同的行。
    std::string row_id;
    std::string title;      // "Also readable"，"Masked"，……
    std::string help;       // 该列表的含义，显示在条目上方

    // 每个条目一个字段，加上尾随"add"操作行。编辑在这里进行，
    // 而不是在其他地方进行；父行在提交之前保持不变，
    // 这使得 Esc 成为真正的 取消。
    form::Form form;

    // 条目ARE是什么。决定 add-row 提示词、
    // 实时 per-entry 提示、回读规范化以及是否运行路径完成。
    //   路径自由文本，工作区-path完成端口数字1-65535，
    //   带类型的进入文本行，因此它显示光标 Host主机名或*.
    //   通配符，由代理使用的SAME函数（
    //         netproxy::normalize_host_entry）规范化，
    //         因此编辑器接受的正是强制执行的
    enum class ListKind : std::uint8_t { Path, Port, Host };
    ListKind kind = ListKind::Path;
    [[nodiscard]] bool numeric() const noexcept { return kind == ListKind::Port; }

    // ── 完成 ──────────────────────────────────────────────
    //
    // 手动键入路径是该控制实际上让人失败的地方：
    // 键入错误的路径不会出错，它默默地不匹配，
    // 并且当命令在几周后无法读取文件时用户会发现它。
    // 在这里，完成并不是一种方便，而是列表如何不再是猜测。
    //
    // 重用ui::FilteredPicker而不是开发私有匹配器：
    // `@`提到，`#` 符号和调色板都已经共享它，
    // 并且filtered_picker.hpp的头文件列出了来自
    // hand-copying这个逻辑的三个错误。 记忆缓存、cold-open
    // 笔芯和光标夹均随附。
    //
    // 建议是 OFF，除非聚焦的条目具有文本：
    // 提供整个 工作区 的空行是噪音墙，并且该行也是
    // 您添加要完整键入的路径的方式。
    bool completing = false;
    ui::FilteredPicker<std::string> complete;

    SandboxListPane();
};

// 声明HERE而不是在共享visual_parts.hpp中，因为该类型具有
// user-provided构造函数（选择面板需要其源+过滤器），
// 因此大括号-arity探针读取0并且完整性证明需要显式opt-in存在于它所描述的事物旁边
// ——与mention::
// Open和AppearancePane::ThemePicker相同。
//
// `completing`和`complete`都是视觉的：它们决定建议
// 列表是否在屏幕上以及其中有什么。 row_id/title/help/numeric
// 是针对编辑器的 生命周期 固定的，因此它们无法驱动 重绘。
inline auto visual_parts(const SandboxListPane& p) {
    return std::make_tuple(visual::exempt, visual::exempt, visual::exempt,
                           visual::ref(p.form),
                           visual::exempt,
                           p.completing,
                           visual::ref(p.complete));
}

// 为一个父行构建编辑器。 `values` 是当前列表。
[[nodiscard]] SandboxListPane build_sandbox_list(std::string_view row_id,
                                                std::string title,
                                                std::string help,
                                                const std::vector<std::string>& values,
                                                SandboxListPane::ListKind kind);
// Ports-or-paths简写，为早于主机的许多呼叫者保留。
[[nodiscard]] inline SandboxListPane build_sandbox_list(std::string_view row_id,
                                                       std::string title,
                                                       std::string help,
                                                       const std::vector<std::string>& values,
                                                       bool numeric) {
    return build_sandbox_list(row_id, std::move(title), std::move(help), values,
                              numeric ? SandboxListPane::ListKind::Port
                                      : SandboxListPane::ListKind::Path);
}

// `kind`列表的尾随"+"行。 一个 位置拼写了添加 提示词，
// 由 build_sandbox_list 和 状态更新函数 的增长路径使用。
[[nodiscard]] form::Field list_add_row(std::string_view id, SandboxListPane::ListKind kind);

// 一个条目行。与 状态更新函数 共享，因此 GROWING 列表和 BUILDING
// 不能漂移 - 增长路径附加一行必须看起来与 build_sandbox_list
// 所做的完全相同，或者编辑器会在 -edit 中更改形状。
[[nodiscard]] form::Field list_entry_text(std::string_view id, std::string label,
                                          std::string help);
[[nodiscard]] form::Field list_entry_number(std::string_view id, std::string label,
                                            std::string help);

// 在行集改变之后对可见标签（"1."、"2."、……）重新编号。
//
// 这些数字是纯粹的装饰——顺序来自字段顺序，
// 而不是来自它们——但是在追加之后读取1、2、
// 2的列表看起来很糟糕，并且标签是追加行时唯一需要修复的东西。
void renumber_sandbox_list(SandboxListPane& p);

// 将编辑器读回到列表中，删除空白。
//
// Blank-dropping是"add"可以插入空行以及
// 删除只是清除空行的原因：两者变成相同的操作，并且用户永远不必
// 找到单独的删除手势。
[[nodiscard]] std::vector<std::string> read_sandbox_list(const SandboxListPane& p);

// 面板。命名为 SandboxPane，而不是 沙箱，因为 panel/slot.hpp
// 需要包含它的 SLOT 的裸名称 - 与 AppearancePane 的分割相同。
struct SandboxPane {
    form::Form form;

    // 每次编辑时都会重新计算，因此墙会跟踪行。
    Preview preview;

    // 下一个 LAUNCH是否将实际传送行所描述的内容，
    // 如果不是，为什么。空意味着承诺是好的。
    //
    // 由 状态更新函数 与预览一起计算，出于同样的原因存储 `facts`：
    // 视图无法探测，并且它不能重新配置 -derive 来找出答案。
    // 参见 restart_outcome()。
    std::string restart_note;

    // 配置行是投影OF。
    //
    // 需要，因为一些设置根本不再存在于表单中：路径和端口列表是
    // 显示计数的选择行，其中真实值保存在此处并在单独的面板中进行编辑。
    // read_sandbox_form() 从其 `base`
    // 参数传递这些内容，因此基础必须是此 面板 正在构建的
    // 配置 - 而不是 已保存 策略，后者是 旧 策略，
    // 并且会默默地丢弃下一个 重绘 上的每个列表编辑。
    sandbox_cfg::Config working;

    // 对于头文件线，实际使用的是哪个后端。在 bwrap
    // 下运行时提供 per-port 网络的 面板 是撒谎的，
    // 因此需要 claybin 的行被标记为不可用而不是
    // 隐藏 - 隐藏它们将使限制不可见。
    std::string backend;        // "claybin" | "none"
    bool claybin_available = false;

    // 所测量的主机，由REDUCER在打开时和每次重新投影
    // 时捕获。
    //
    // 存储而不是按需探测，因为视图必须保持纯粹——它不能
    // 分叉 uid_map 探测 mid-render。
    // 它必须在这里，而不是在 `backend`/
    // `claybin_available` 的视图中重建，
    // 因为重建它正是副标题描述不向上的边界的方式（§17）：
    // 视图将从仅近似它的场中推断现实。
    HostFacts facts;

    // 保存已经落在磁盘上，但是LIVE边界仍然是该过程开始时的边界。
    // 驱动 "applies on restart" 页脚。
    //
    // 需要，因为该面板的保存是agentty中不进行
    // 保存的一处副作用：沙箱策略在启动时被密封（
    // 参见tool/util/sandbox.hpp的set_config）。
    // 不用说，收紧系统调用配置文件并继续工作的用户会
    // 相信在他们重新启动之前不会出现的墙——这正是
    // 面板 存在的目的是为了防止的谎言，只是指向未来而不是
    // 现在。
    bool saved_pending_restart = false;
};

// (这个 面板 的 visual_parts 位于 panel/visual_parts.hpp 中，
// 与所有其他面板一起——头文件 是保存门证据的地方。)

// 发动机排可以处于的三种状态。
//
// 和类型而不是导致该错误的两个布尔值，
// 因为panel/form.hpp给出了大约FormFocus：
// 编码三个状态的两个布尔值使得不变量仅存在于注释中，
// 并且该文件现在已两次传送该故障（kSbMode和副标题）。
// 拼写出状态意味着视图不会意外地描述不向上的边界。
enum class EngineStatus : std::uint8_t {
    // 选定正在运行的发动机IS。只有在这种状态下，
    // 屏幕上的任何内容才可以用现在时表达。
    InForce,
    // 该选择与正在运行的不同。下次发射时，保存 会取代 副作用；
    // 下面描述的墙壁是 FORECAST。
    AppliesOnRestart,
    // 选择了claybin，但主机拒绝了。保存仍然是合法
    // 的（用户明天可能会在另一台机器上），但这里不会强制
    // 执行任何操作。
    CannotStart,
};

[[nodiscard]] EngineStatus engine_status(const sandbox_cfg::Config& cfg,
                                         const HostFacts& facts);

// 该配置在下次启动时实际上会起作用吗？
//
// ── 为什么这是它自己的职能 ───────────────────────────────────
//
// 因为面板可能会说"saved \xc2\xb7 applies on restart"
// 并且是错误的。答案的每一个组成部分都已经计算出来了
// ——编译结果、不可执行的列表、主机探测——没有任何
// 东西将它们加入到页脚所做的一个声明中。对未来的承诺，
// 背后没有任何检查，其形状与该子系统中的所有其他错误相同：
// "sandbox: active" (§1)，不执行任何操作
// 的交接行 (§14)，未向上的字幕命名墙 (§17)。
// 这里的谎言只是指向未来而不是现在。
//
// 三件事可以使重新启动 不 提供行所描述的内容：
//   1.保单根本没有编译（claybin拒绝而不是
//      有辱人格，所以这是一个硬不）
//   2.所选引擎无法在此主机上启动
//   3.它编译并启动，但某些能力悄然变弱
//      比问——`unenforceable`名单
//
// 当重新启动确实将传递所描述的策略时，返回空。
// 非-empty字符串是页脚必须说的纯粹承诺的INSTEAD。
[[nodiscard]] std::string restart_outcome(const sandbox_cfg::Config& cfg,
                                          const HostFacts& facts,
                                          const Preview& preview);

// 一行描述目前什么是 ACTUALLY 限制命令，
// 仅从 `facts` 派生——它看不到选择，
// 因此它不能被用来描述未运行的策略。
[[nodiscard]] std::string describe_running(const HostFacts& facts);

// 从配置构建表单。纯粹：没有 I/O，没有探测——调用者提供主机
// 可以做什么，所以这可以在没有内核的情况下进行测试。
[[nodiscard]] form::Form build_sandbox_form(const sandbox_cfg::Config& cfg,
                                            const HostFacts& facts);

// 将表格读回到配置中。与上述相反，也是唯一知道
// 映射的地方 - 因此重命名的行会在一处中断。
[[nodiscard]] sandbox_cfg::Config read_sandbox_form(const form::Form& f,
                                                    const sandbox_cfg::Config& base);

// 编译配置并描述所产生的墙，没有生成。
[[nodiscard]] Preview preview_sandbox(const sandbox_cfg::Config& cfg);

// 将挂墙报告附加到ROWS，并验证用户带类型的的内容。
//
// ── 为什么这是个单独的通行证 ─────────────────────────────────────────
//
// 因为诚实只存在于编译之后。 build_sandbox_form() 知道配置和主机，
// 这足以 LOCK 一行 ("this kernel cannot do per-port network")，
// 但不足以注释一行 ("your network rule is enforced by landlock abi 10")。
// 第二个事实来自 claybin 的 GuaranteeReport，
// 它需要完成的配置。所以：构建，然后 编译，
// 然后注释。
//
// ── 为何 -row 不只是脚印 ────────────────────────────────
//
// 页脚已经包含了所有这些，这就是问题所在。十二个功能合并到一个 run-on
// 行中就是一个段落，而一个段落就是您跳过的内容。用户的实际问题从来不是
// 目前有哪些隔离边界？
// 而是 "is the thing I just changed real"——这是
// 关于 一个 行的问题。在该行旁边回答它，将一堵文字墙变成一瞥，而且不需要任何成本：
// `Field::origin`已经渲染暗淡和正确的-aligned，
// 正是这个（"where did this value come from"）在另一个面板上。
//
// 出于相同的原因，验证在同一遍中进行。 `Field::error`从一
// 开始就在表单模型中，没有面板设置它；同时，这个 面板 可以接受带有空
// 端口列表的 `net_mode = ports`，其中 编译 到一个
// 拒绝一切的隔离命名空间 - 用户请求 "these ports" 并
// 默默地得到 "nothing"。 self-defeating 的行应该
// 在 带类型的 的位置如此表示。
void annotate_sandbox_form(form::Form& f, const Preview& preview,
                           const sandbox_cfg::Config& cfg, EngineStatus status);

// 依赖于HOST而不是配置的警告：工作区是$HOME
// （每个点文件可写），或者最后的切换快照达到其上限。
// 此外，审计发现误导性的配置警告：allow_ports在完全下不执行任何操作，
// 并且端口仅覆盖TCP。在 annotate_sandbox_form 之后运行。
void annotate_host_risks(form::Form& f, const sandbox_cfg::Config& cfg,
                         const HostFacts& facts);

}  // namespace agentty::ui::panel

namespace agentty::visual {
// SandboxListPane具有user-provided构造函数(选择面板需要其源
// 和过滤器)，因此brace-arity探针读取0并且parts_cover_all无法机械地验证元组。
// 同样的opt-in mention::Open和AppearancePane::ThemePicker使用，
// 并且同样的义务：上面的visual_parts是由
// HAND检查的，所以添加成员意味着更新它。
template <> inline constexpr bool trusted_parts<ui::panel::SandboxListPane> = true;
}  // namespace agentty::visual
