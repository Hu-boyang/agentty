#pragma once
// agentty::sandbox_cfg-用户-facing 沙箱策略，作为域类型。
//
// 内容 此 IS FOR。 agentty的沙箱姿势曾经是sandbox.cpp中的一组常量：
// 哪些系统根目录可读，网络是否共享，/tmp的大小是多少。
// 合理的默认值，但不是用户可以声明的策略 -
// 该代理能否访问互联网？
// 正是用户有意见但无法表达的问题。
//
// 这里的一切都是由 claybin 强制执行的。这就是选择 库
// 而不是 二进制程序 的要点：bwrap 可以表达挂载，
// 但是 per-port 网络 授权、系统调用过滤器和 内存 上限是
// 只有在编译策略而不是组装 argv 时才能要求的东西。
//
// ── 形状跟随smart::RoleConfig ──────────────────────────────────
// DOMAIN 结构由 store::Settings 组成整体，
// 因此设置 注册表 可以将行直接绑定到 运行时 读取的内容中。另一种选择——
// 设置上的平坦字段——使 "network off but allowlist populated"
// 可表示，并强制在每个边界处进行 hand-written 映射，
// 这是一个忘记字段的地方。 （store/store.hpp 大声说出
// 了 RoleConfig 替换的 11 个字段。）
//
// ── 为何缺省是他们是什么 ──────────────────────────────────
// 它们准确地再现了TODAY的行为。用户升级获得他们已经拥有的沙箱；
// 每个更严格的设置都是 opt-in。升级时破坏
// `npm install` 的安全默认设置不会保持打开状态，
// 它会与面板中的其他所有内容一起关闭。

#include <cstdint>
#include <string>
#include <vector>

#include "agentty/domain/sandbox_provenance.hpp"

namespace agentty::sandbox_cfg {

// 沙箱暴露了read-only的文件系统的多少。
//
// 这是每个代理 沙箱 在同一方向上出错的轴：方便的答案是 "bind / read-only"，
// 它移交 ~/.ssh、~/.aws 和机器上的每个其他项目。
// agentty 自己的历史有一个——一个后端在报告
// "sandbox: active" 的同时授予对 `/` 的读取权限。
enum class FsScope : std::uint8_t {
    // /usr、/bin、/库、/etc的特定文件，以及$HOME工具链目录(
    // ~/.cargo、~/.nvm、~/.local/bin...)。构建需要什么，
    // 仅此而已。这就是今天的行为。
    Toolchain,
    // 工作区、加载器和/usr。没有工具链目录 - 对于 self
    // -contained 仓库，其中 $HOME 下的任何内容都是可疑的。
    Minimal,
    // /下的所有内容都可读。方便答案的诚实名称：
    // 仍然是一个挂载命名空间，在 工作区 之外仍然没有写入，
    // 但是您的密钥可以通过批准的命令读取。
    HostReadable,
};

[[nodiscard]] constexpr const char* to_string(FsScope s) noexcept {
    switch (s) {
        case FsScope::Toolchain:    return "toolchain";
        case FsScope::Minimal:      return "minimal";
        case FsScope::HostReadable: return "host-readable";
    }
    return "toolchain";
}

// 沙盒命令可以在网络上到达什么。
//
// 研究共识（Claude代码的允许清单代理、
// Codex的工作区-write网络切换、innoq的代理
// 撰写）是，一揽子on/off太粗略：代理合法地需要注册表和git流量，
// 这也是渗漏路径。每个人都伸手去拿 允许清单。
//
// claybin可以强制执行该IN 该 KERNEL，
// 而不是通过代理，因为内锁（ABI 4+）调解每个端口的TCP连接。
// 这是一个真正比代理更强大的边界，代理可以绕过它——但
// 它是 PORT 粒度，而不是主机名，所以它诚实地说是一个
// 不同的工具而不是一个更好的代理。
enum class NetMode : std::uint8_t {
    // 共享主机的网络命名空间。今天的行为： git Push、
    // npm install 和curl 都可以工作，其他任何事情也是如此。
    Full,
    // 空的网络命名空间。根本没有套接字，甚至没有环回 DNS。
    // 最严格的设置，并且它破坏任何提取的命令。
    None,
    // 仅`allow_ports`中的端口，由内陆强制执行。
    // 需要内锁 ABI >= 4；回到完整状态，保证报告上写着 `partial`，
    // 而不是默默地假装。
    Ports,
};

[[nodiscard]] constexpr const char* to_string(NetMode m) noexcept {
    switch (m) {
        case NetMode::Full:  return "full";
        case NetMode::None:  return "none";
        case NetMode::Ports: return "ports";
    }
    return "full";
}

// 系统调用被过滤的程度如何。
//
// bwrap接受--seccomp FD并且agentty什么也没传递给它，
// 所以直到现在根本不存在系统调用过滤器。这些是 claybin 的个人资料。
enum class SyscallMode : std::uint8_t {
    // 无过滤器。 agentty 在 claybin 之前所做的事情，
    // 让工具链在过滤器下崩溃的用户有一个不是 "sandbox off" 的
    // 地方可以着陆。
    Off,
    // profiles::compiler_with_network()。文件、子进程、套接字；
    // 拒绝 ptrace、mount、unshare、setns、bpf、kexec 和模块 调用；
    // 通过 ARGUMENT 过滤克隆的命名空间 标志； W^X 于 mmap/mprotect；
    // ioctl 简化为allow-list，不包括TIOCSTI 击键-injection 系列。
    Compiler,
    // 当Access为none时，编译器减去套接字调用。
    // （以前是with_filesystem()，它没有execve，无法运行shell。）
    Strict,
};

[[nodiscard]] constexpr const char* to_string(SyscallMode m) noexcept {
    switch (m) {
        case SyscallMode::Off:      return "off";
        case SyscallMode::Compiler: return "compiler";
        case SyscallMode::Strict:   return "strict";
    }
    return "compiler";
}

// NEVER暴露的路径，无论范围如何。
//
// 今天，这些是偶然缺失的：工具链范围绑定 ~/.cargo/bin
// 而不是 ~/.cargo，因此 凭据 永远不会出现。
// 这是仔细阅读后的正确结果，但它很脆弱——它之所以成立，
// 是因为有人选择了狭窄的路径，而下一个人为某些工具的配置添加 "~/.config"
// 时会带走 ~/.config/gh。
//
// 出于这个原因（"no ambient production secrets")，
// 2026 沙箱文献中的每个来源都将秘密卫生列为单独的控制，
// 因此它变成一个：每个授权应用AFTER的拒绝列表，
// 包括HostReadable并将用户带类型的纳入read_paths的任何内容。
//
// Landlock 使得这既便宜又精确——子树上的拒绝规则击败了它
// 上面的 授权——这是 mount-only 沙箱 在不使用绑定屏蔽每个
// 路径的情况下无法做到的事情。
inline constexpr const char* kAlwaysMasked[] = {
    // 凭据，大致按您对失去它们的介意程度排序
    "/.ssh",
    "/.aws",
    "/.gnupg",
    "/.kube",
    "/.docker/config.json",
    "/.config/gh",           // github cli令牌
    "/.config/gcloud",
    "/.azure",
    "/.netrc",
    "/.npmrc",               // npm 身份验证令牌
    "/.pypirc",
    "/.cargo/credentials",
    "/.cargo/credentials.toml",
    "/.git-credentials",
    // 其他代理和CLI的sign-ins:明文OAuth令牌和账户ID
    "/.codex/auth.json",
    "/.claude/.credentials.json",
    "/.claude.json",
    "/.config/github-copilot",
    "/.config/op",                        // 1password CLI
    "/.config/rclone/rclone.conf",
    "/.terraform.d/credentials.tfrc.json",
    "/.m2/settings.xml",
    "/.gradle/gradle.properties",
    "/.password-store",
    // 密钥环和浏览器配置文件：已加密，
    // 但密钥材料及其解锁的数据将一起留下以进行离线攻击
    "/.local/share/keyrings",
    "/.config/google-chrome",
    "/.config/chromium",
    "/.config/BraveSoftware",
    "/.mozilla",
    // shell 和 REPL 历史记录：粘贴的令牌和密码在此处结束
    "/.bash_history",
    "/.zsh_history",
    "/.local/share/fish/fish_history",
    "/.python_history",
    "/.psql_history",
    "/.mysql_history",
    "/.node_repl_history",
    // agentty自己的状态：内存存储可以包含用户告诉它的任何内容，
    // 并且凭据存储显然很重要。 凭据 位于目录 (
    // ~/.agentty/credentials/) 中；
    // 旧的 single-file 路径将保留用于移动之前的安装。
    "/.agentty/memory.jsonl",
    "/.agentty/credentials",
    "/.agentty/credentials.json",
};

// 基本名称在出现的任何地方都被屏蔽，包括在 工作区 内部。
//
// 仓库中的.env是真正的秘密所在的最常见的地方，并且工作区是
// READ-WRITE——所以与上面的列表不同，这里的范围不属于保存。
// 与基本名称匹配，因为 .env 存在于框架放置的任何位置
// it.
inline constexpr const char* kAlwaysMaskedNames[] = {
    ".env",
    ".env.local",
    ".env.production",
    ".envrc.local",
    // .env家族的其余部分。这些并不奇怪：带有 .
    // env.production 的 仓库 旁边几乎总是有 .
    // env.development，而在留下其他的同时掩盖其中一个是一堵墙，旁边有一扇门。
    ".env.development",
    ".env.staging",
    ".env.test",
    ".env.dev",
    ".env.prod",
    "id_rsa",
    "id_ed25519",
    // 其他SSH钥匙类型。 id_rsa 和 id_ed25519 是
    // 常见的两个，但文件名是由 ssh-keygen -t 选择的，
    // 而屏蔽五种密钥类型中的两种的 沙箱 则取决于用户选择的算法。
    "id_ecdsa",
    "id_dsa",
    // 包-manager和VCS令牌，WORKSPACE副本。
    //
    // $HOME 版本位于上面的 kAlwaysMasked 中，
    // 这是它们通常存在的地方——但是带有身份验证令牌的项目 -local .
    // npmrc 在 monorepo 中是正常的事情，而签入工作树的 .
    // git-credentials 是真正的（坏的、常见的）模式。
    // 扫荡者沿着工作区行走并会找到它们；仅缺少 NAME 列表。
    ".npmrc",
    ".pypirc",
    ".netrc",
    ".git-credentials",
    ".dockercfg",
    ".pgpass",
    // 云服务-account密钥。文件名是常规的而不是强制的，
    // 但这两个是每个 GCP/AWS 教程编写的内容以及
    // CI 模板复制的内容。
    "credentials.json",
    "service-account.json",
    // Terraform变量文件通常携带提供者秘密，
    // 并且.tfvars后缀是工具本身记录的约定。
    "*.tfvars",  // SUFFIX:匹配prod.tfvars，而不是名为".tfvars"的文件
    "*.pem",     // SUFFIX：原来的，现在拼写明确
};

// 哪个Linux引擎应用该策略。
//
// Linux发动机。一个值保留为 枚举，而不是直接删除，因此 已保存
// 配置保持 self-describing，而第二个引擎（比如 Windows
// 引擎）有地方可以着陆。
//
// bwrap是另一个值。之所以如此，是因为保留它的论点没有在代码中幸存下来：它
// 说 claybin 需要内锁，因此旧内核将一无所有 - 但 claybin
// 的底层是 `landlock || seccomp`，并且 seccomp
// 早于 (3.5) bwrap 所需的非特权用户命名空间 (3.8)。
// 每个可以运行 bwrap 的主机都有 claybin。
enum class LinuxBackend : std::uint8_t {
    // in-process 库：可用的命名空间，加上seccomp过滤器、
    // landlock和cgroup2限制——i.e。
    // 面板 的其他行描述的所有内容，这就是策略词汇表是它的原因。
    Claybin,
};

[[nodiscard]] constexpr const char* to_string(LinuxBackend b) noexcept {
    switch (b) {
        case LinuxBackend::Claybin: return "claybin";
    }
    return "claybin";
}

// 一种命名姿态：整个政策作为一个决定。
//
// ── 为什么有这种东西 ────────────────────────────────────────────────
//
// 面板有28行。它们中的每一个都是真实的、强制的、
// 单独合理的控制——要求用户从 28 个原语中组合出一个
// 安全边界仍然是一个错误的问题。没有人打开此屏幕想要选择
// pid cgroup 限制。他们打开它想要大约四件事中的一件，
// 而诚实的 UI 就是问哪一件的人。
//
// 这与 面板 的其他人已经学到的关于诚实的教训相同，
// 应用于努力：无人能推理的控制是无人使用的控制，
// 并且未使用的安全控制被关闭。永远保持默认状态的
// 28 个正确开关的价值还不及用户实际做出的
// 一个选择。
//
// ── 为什么预先设定而不是一个巫师 ──────────────────────────────────
//
// 因为行保留。预设 WRITES 包含 28 个值，
// 并且每个值都可见且可编辑——这是一个起点，而不是一种模式。
// 一旦预设隐藏了细节，它就会变成另一个 "sandbox: active"：
// 一个代表您无法再检查的边界的标签。
//
// 所以 `Custom` 不是您选择的预设，它是当配置与
// 其他配置都不匹配时的 面板 REPORTS。调整一排硬化
// 不会默默地继续声称硬化。
enum class Posture : std::uint8_t {
    // 没有超出安装命名空间的沙箱。对于工具链在过滤器下
    // 真正崩溃的用户来说，否则他们会冒充 --sandbox，
    // 这绝对是更糟糕的。
    Permissive,
    // 默认值，以及`configured = false`的含义：工具链-readable、
    // 网络开启、编译器系统调用配置文件、fork-bomb上限。所以选择了普通的
    // `cargo build` / `npm install`作品原封不动。
    Balanced,
    // 每面墙 claybin 都可以建造，资源上限仍然让真正的
    // 建造完成。如果您正在运行您尚未阅读过的代理，
    // 则应选择该姿势。
    Hardened,
    // 根本没有网络，并且有最严格的过滤。阅读和编辑代码而不让任何东西泄露出去—
    // —这种姿势使得 "read access plus network is read plus exfiltrate"
    // (§11) 不真实。
    Airgapped,
    // 不可选择。当行与预设不匹配时 面板 会显示什么内容。
    Custom,
};

[[nodiscard]] constexpr const char* to_string(Posture p) noexcept {
    switch (p) {
        case Posture::Permissive: return "permissive";
        case Posture::Balanced:   return "balanced";
        case Posture::Hardened:   return "hardened";
        case Posture::Airgapped:  return "airgapped";
        case Posture::Custom:     return "custom";
    }
    return "custom";
}

// 一行关于姿势放弃的内容，为该行的帮助文本。
// 表述为 COST 而不是好处：每一个都是安全的-sounding，
// 用户在选择之前需要知道什么会停止工作。
[[nodiscard]] constexpr const char* cost_of(Posture p) noexcept {
    switch (p) {
        case Posture::Permissive:
            return "no syscall filter, no caps \xc2\xb7 only the mount walls";
        case Posture::Balanced:
            return "network is open, so read access is also exfiltration";
        case Posture::Hardened:
            return "a build that needs an unusual syscall may fail";
        case Posture::Airgapped:
            return "git push, npm install and curl all stop working";
        case Posture::Custom:
            return "your own mix \xc2\xb7 the rows below are the truth";
    }
    return "";
}

struct Config {
    // ── 引擎 ────────────────────────────────────────────────────────
    // 第一个字段，因为它限制了大多数其他字段的含义。
    // 首先是 面板 渲染，然后是 锁 所选后端无法强制
    // 执行的行，而不是接受不执行任何操作的设置。
    //
    // 默认为claybin：它在每个测量的能力上都严格更强，
    // 并且不能运行它的主机回退到bwrap而不是
    // 什么都没有。
    LinuxBackend backend = LinuxBackend::Claybin;

    // ── 文件系统 ───────────────────────────────────────────────────
    FsScope fs_scope = FsScope::Toolchain;

    // 额外路径，超出上述范围。除非在 `write_paths` 中列出，
    // 否则请读取 -only。具有 工作区 之外的 monorepo 依赖
    // 项或共享 缓存 的用户需要这个，并且没有其他方式可以表达它。
    std::vector<std::string> read_paths;
    // 工作区之外的可写路径。故意与 read_paths
    // 分开：授予写入权限与授予读取权限是不同的决定，
    // 单个列表将使危险的列表变得容易。
    std::vector<std::string> write_paths;
    // 即使在授权范围内也要屏蔽的路径。
    // "toolchain scope, but not ~/.cargo/credentials" 的逃生舱口。
    std::vector<std::string> deny_paths;

    // ── 网络 ──────────────────────────────────────────────────────────
    NetMode net_mode = NetMode::Full;
    // NetMode::Ports下允许的端口。 443和80覆盖HTTPS和HTTP；
    // 22 是 git-over-ssh； 53 是 DNS，很容易忘记，
    // 一旦忘记就会破坏一切。
    std::vector<std::uint16_t> allow_ports{443, 80, 22, 53};
    // Host 命名 Full-mode 代理可能到达的名称。 空 =任何公共
    // 主机（代理仍然拒绝环回，RFC1918，链接-local和元数据地址，
    // 无论这是什么）。 Non-empty = 仅这些名称：
    // `github.com`准确，`*.github.com`任何子域但不是顶点。
    // 每当设置该值时，IP文字都会被拒绝。由代理对命令所要求的名称强制执行，
    // 然后代理解析相同的名称 - 因此无法使用 DNS 来引导它。
    // 仅限 Proxy-aware 客户端：忽略 http_proxy
    // 的工具根本无法获取网络，永远不会获得未经过滤的网络。
    std::vector<std::string> allow_hosts;

    // ── 响铃 ─────────────────────────────────────────────────────────
    SyscallMode syscall_mode = SyscallMode::Compiler;
    // 拒绝既可写又可执行的映射。便宜，而且它打破了依赖一个
    // RWX 页面的一整类漏洞利用 - 但 JIT（具有一些
    // 标志 的节点，任何 JVM 的节点）确实需要它。
    bool wx_protect = true;

    // ── 资源 ────────────────────────────────────────────────────
    // cgroup2当主机委托时，rlimit作为后备，
    // 否则。担保报告将两者区分开来，而不是两者都声称。
    //
    // 内存和CPU默认为NO CAP，有意为之：正确的上限是机器和
    // 构建的属性，错误的上限会将工作中的`cargo build`
    // 变成看起来像是agentty的错误的OOM杀戮。
    // 我们无法为别人的 8-core 笔记本电脑和 64-core 工作站
    // 选择编号。
    std::uint64_t memory_mb   = 0;
    std::uint32_t cpu_percent = 0;   // 100=一核

    // 进程是DIFFERENT，并且附带实际上限。
    //
    // 审核(tests/sandbox_audit.cpp)显示resource.pids
    // 在默认安装上为`none`，这意味着来自经批准的命令的分叉炸弹是无
    // 限制的——并且与内存不同，IS是一个在任何地方都是安全的数字。
    // 没有合法的构建需要 4096 个并发进程； 64-core 盒子上的
    // `make -j` 峰值为数百，而病理情况不是 "a big build"，
    // 而是 `:(){ :|:& };:`，需要数百万美元。
    //
    // 因此，这是可以默认打开而无需猜测主机的一个资源墙。
    // 其他人则保持沉默，直到用户选择一个数字，
    // 因为对他们来说，猜测比什么都没有更糟糕。
    std::uint32_t max_procs   = 4096;
    // 打开描述符(RLIMIT_NOFILE)。与 max_procs
    // 不同的杠杆：泄漏 fds 的失控会耗尽主机的文件表而不会分叉，
    // 因此 pid 上限不会限制它。
    std::uint32_t max_open_files = 0;
    // Wall-clock 一个命令的上限，以秒为单位。
    // 与工具层自己的 超时 不同：这是由 沙箱 强制执行的，
    // 因此它仍然适用于在系统调用中忽略 SIGTERM 或楔形的子级。
    std::uint32_t wall_clock_secs = 0;
    // CPU-time天花板 （RLIMIT_CPU），以秒为单位。
    // 限制总计算而不是经过的时间，因此当繁忙循环被终止时，
    // 永远休眠的进程不受影响。分开于cpu_percent，
    // 它会限制而不是杀死。
    std::uint32_t cpu_secs = 0;
    // /tmp大小。作为安装选项强制执行，因此失控构建会在 沙箱 内
    // 命中 ENOSPC，而不是填充主机的 RAM。 bwrap
    // 根本无法表达这一点。
    std::uint64_t tmp_mb = 512;

    // ── 硬化 ────────────────────────────────────────────────────────
    // Landlock作用域(ABI 6+):关闭抽象unix
    // 套接字和cross-boundary信号。它们完全忽略文件系统——
    // 抽象套接字没有路径——所以这个结构中没有其他内容涵盖它们。
    // Off表示"rely on the network namespace"，
    // 当net_mode满时停止工作。
    bool scope_ipc = true;
    // 从agentty继承的关闭描述符。继承的 fd 是 沙箱
    // 无法看到或撤销的权限；气泡包装纸在这里泄漏了一个。
    bool close_inherited_fds = true;

    // 对客人隐藏主人的身份。
    //
    // 文件系统的一个明显问题：主机名和客户的明显的
    // uid/gid 泄漏到构建输出、测试装置和任何
    // `id` 的内容中。两者都不是功能，
    // 所以这是关于可重复性和指纹识别而不是遏制——这就是为什么
    // 它是一个单独的行并且默认为关闭。
    bool fake_hostname = false;   // 声称"sandbox"而非真实主机

    // 凭据-name扫描的深度(kAlwaysMaskedNames)。
    //
    // 这些是BASENAMES——`.env`、`id_rsa`——
    // 并且在不知道文件在哪里的情况下不能将名称规则转变为安装，因此必须通过
    // 步行来找到路径。之前只扫描了 工作区 根，错过了任何 monorepo
    // 中的常见情况：`services/api/.env`。
    //
    // 有界而不是递归-to-the-leaves，并且界限是一个设置，
    // 因为正确的值取决于仓库。每个额外的级别在每次生成时都会有
    // 更多的 stat() 调用，并且大 源码树 的无限行走将
    // 在每个 shell 命令的延迟路径中放置目录扫描。
    //
    // 0=仅根（旧行为）。 3 涵盖了实际发生的布局——services/* /.env、
    // packages/* /.env、apps/* /web/.env.
    std::uint32_t mask_scan_depth = 3;

    // 用户是否曾经进行过已保存 沙箱设置？虽然错误，但 运行时 仍保持其出厂状态，
    // 因此升级不会改变任何内容，除非经过深思熟虑的编辑 - 与
    // RagConfig::configured 的合同相同。
    bool configured = false;

    // ── 信托交割 ────────────────────────────────────────────────────
    //
    // 当代理写入 HOST 稍后执行的文件时要做什么——钩子配置、.
    // vscode 任务、git 配置、venv 解释器。
    // 这就是 Pillar Security 2026 年 7
    // 月系列（CVE-2026-48124 等）中每次越狱的形式：代理
    // 从未破坏过 沙箱，它只是编写了稍后运行的 沙箱 之外的可信组件。
    //
    // 默认情况下拒绝，这比这里的其他行为更严格。
    // 批准 提示词 适用于 "this might be risky"；
    // 信任切换是已知转义类的形式，因此答案从“否”开始。
    HandoffPolicy handoff = HandoffPolicy::Refuse;

    // 默认平等，因此沙箱 面板可以询问
    // "is the saved policy different from the one actually enforcing?"并如实回答。
    //
    // 该问题仅存在，因为实时策略在启动时是 SEALED (参见
    // tool/util/sandbox.hpp)：会话期间的
    // 保存 进入磁盘并且不占用 副作用，因此 面板 有两个配置要比较，
    // 并且必须在它们出现分歧时告诉用户。在封印之前只有一个。
    //
    // 故意默认而不是hand-written：忘记字段的
    // hand-rolled比较将报告确实改变的策略的
    // "no change"，这是安全控制失败的错误方向。
    // 通过这种方式向结构添加字段可以保持比较完整。
    [[nodiscard]] bool operator==(const Config&) const = default;
};

// 对于给定的配置和工作区，必须屏蔽的每个绝对路径。
//
// ONE函数，由BOTH后端使用。这就是要点：凭据 列表是策略
// 中最不可协商的部分，并且由一个后端而不是另一个后端应用的
// 列表是 `.env` 在 bwrap 下泄漏一周而在
// claybin 下读回空的原因。如果这是错误的，那么到处都是错误的，
// 这一点更容易被注意到。
//
// 三个来源，按照它们是用户业务的程度排列：
//
//   1. kAlwaysMasked — $HOME 凭据 路径。不可配置。
//   2. kAlwaysMaskedNames — 凭据 BASENAMES，通过步行找到
//                           工作区至cfg.mask_scan_depth。
//                           除深度外不可配置。
//   3.cfg.deny_paths——用户添加的任何内容。最后，所以明确
//                           否认不能由我们之一撤销。
//
// 为什么存在该遍历：在不知道文件在哪里的情况下，基本名称规则不能成为安装。
// 仅屏蔽 `<workspace>/.env` 错过了
// `services/api/.env`，这是它实际存在于任何单一存储库中的位置。
//
// 为什么是BOUNDED：这在每个生成上运行，因此大源码树的
// 无界行走将在每个shell命令的延迟路径中放置完整目录扫描。
// Depth-limited，它会跳过那些总是很大且从不
// 包含秘密的目录（.git、node_modules、目标、
// 构建）——跳过这些目录是一个性能决定，而且它是安全的，
// 只是因为它们都不是 凭据 文件合法存在的地方。
// 重新审视一下列表是否会增长一个目录。
//
// `$HOME`作为参数进来，而不是从环境中读取，
// 因此这保持纯粹且可测试，而不触及真实的主目录。
[[nodiscard]] std::vector<std::string> mask_paths(
    const Config& cfg, std::string_view workspace, std::string_view home);

// 用户带类型的的路径设为绝对：`~`和`~/x`扩展到主目录，
// 相对路径针对工作区进行解析，尾部`/`被丢弃。
// 无法解析时为空(e.g.`~`无家)。
[[nodiscard]] std::string resolve_user_path(std::string_view path, std::string_view workspace,
                                            std::string_view home);

// ── 安全预设 ───────────────────────────────────────────────────

// Write姿势的值超过配置，保留USER的
// 部分而不是姿势的部分。
//
// 幸存下来的是：路径列表（read/write/deny)和引擎。
// 姿势是关于墙壁有多紧的声明，而不是关于这个特定项目需要哪个
// 额外的依赖目录——擦除某人的 `read_paths`，
// 因为他们尝试了 Hardened 会使预设变得难以使用。
//
// NOT触摸的是`backend`什么姿势。在 claybin
// 不可用的主机上选择 Hardened 不得静默切换引擎；
// 面板 锁 该行并报告它，这是诚实的答案。
[[nodiscard]] Config apply_posture(const Config& base, Posture p);

// 这个配置IS是哪个姿势，还是Custom。
//
// 通过比较而不是存储来导出，这就是 加载-bearing
// 选择：当用户编辑一个姿势字段时，STORED 姿势字段将从行中漂移，
// 然后 面板 将声明配置不再描述的边界 - 与
// "sandbox: active" 相同的谎言。推导出来就意味着标签不会错，
// 只能是"Custom"。
[[nodiscard]] Posture detect_posture(const Config& cfg);

}  // namespace agentty::sandbox_cfg
