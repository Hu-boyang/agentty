#include "agentty/tool/util/sandbox.hpp"

#include "agentty/tool/util/fs_helpers.hpp"
#include "agentty/util/logx.hpp"   // AGT_LOG - 封存的门 -policy 拒绝

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>   // shared_ptr - 配置图 快照
#include <optional>
#include <string_view>

#include <maya/runtime.hpp>
#include <set>
#include <string>
#include <vector>

// 政策类型是 PRODUCT 概念,而不是 claybin
// 其一: 建楼没有 claybin 仍然有一个 沙箱 姿态,
// 它只是无法执行大部分。所以这包括无条件的,执行是被保护的,
// 否则 "what is my policy" 将是一个问题 只有
// 一些建筑可以回答, 和设置 面板 两者都要合作
#include "agentty/domain/sandbox_config.hpp"

// claybin 需要一个 子模块 所以,唯一剩下的问题就是 PLATFORM : 已经
// 编译 计划,任何地方, 并且可以 APPLY 一个在 Linux (名称空间, 地锁,
// seccomp, cgroup2 ) 和 继续 macOS (系安全带,
// rlimits). Windows 编译 保险单上写着拒绝执行 所以内应留在外面
#if defined(__linux__) || defined(__APPLE__) || defined(_WIN32)
#include "agentty/tool/util/sandbox_claybin.hpp"
#include "agentty/tool/util/handoff_gate.hpp"   // snapshot_trusted: 一个 路径表
#endif

namespace agentty::tools::util::sandbox {

namespace fs = std::filesystem;

// 逃出一条安全插入的路径 macOS 沙箱-exec (SBPL ) 配置字符串。 工作区 根为
// user/attacker-influenceable (通过) --workspace / cwd)
// 和 双双双双 macOS/APFS 许可
// 目录名中的双引号、反斜杠和控制字符必须转义；直接插入路径会提前结束 subpath 字符串。
// …
// 余下内容会被重新解析为 SBPL，轻则配置被拒绝，重则注入 allow 规则，因此必须转义反斜杠和引号。
// 使字面惰性; 控制字符不能在 SBPL 字符串, 这样路径会
// 生成空 ^ 调用者忽略了条款, 并且失败了。 导出( 不是
// 动因) -namespace )所以这是单位 -tested.
std::string sbpl_escape(std::string_view path) {
    std::string out;
    out.reserve(path.size() + 8);
    for (char c : path) {
        if (c == '\0' || c == '\n' || c == '\r' || c == '\t')
            return {};                       // 不可清洁 + 关闭失败
        if (c == '\\' || c == '"') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

namespace {

// 单身 -process 状态。设置于 init() 开始时,
// 每一次的点击读取 调用 原子在之后是不需要的(一次设定,
// 主机交接后再也没有翻转过) maya ),但费用不高,
// 并记录了读数。 -many 生命周期。
std::atomic<Mode>    g_mode{Mode::Auto};
std::atomic<Backend> g_backend{Backend::None};

// 哪个 Linux 后端更喜欢两边都行
//
// claybin 默认值。 回退 下方表示这是 PREFERENCE
// 而不是要求 -- -- 无法构建的宿主 claybin 墙壁仍然被打碎,
// 永远没有。
//
// 曾经是空洞的, 理由是十年的...
// 上游 硬化的节拍日,升级不应该默默地移动任何人的边界。
// 衡量方法证实了这一论点: agentty 正在将路径分隔为
// --seccomp ,没有群限和无落地锁,所以反对 claybin
// 绝对是弱到不行 syscall.filter (无对强),所有三个资源上限(
// 无对强,), filesystem.exec (宾馆对地锁)和经纪(无宾馆对)

// 用户的 已保存 沙箱 策略。已写入 一次 启动时,然后冻结。
//
// 密封而不是可重塑, 而那个封印是 面板
// 安全模式 - 看 set_config
// ()用于充分推理。 -session
// 弱者是看不见的,无法被解除,它使弱者 沙箱
// 无法听懂,因为一个会话中有两个命令得到两
// 个不同的壁.
//
// 这是 仅 设置在 agentty 设置商店中的其他所有
// 设备都是通过设计来运行的; 主题通过查看来判断。
// Name 沙箱 判断它时,它读它的承诺,
// 所以它交易 即时的财产,这里没有其他需要。
//
// 还是个出版商 shared_ptr-to-const 而不是
// 一个简单的全球:写过一次,但是 READ 从工具 工作线程
// 线程 编辑 UI 线程 读者在阅读时, 锁.
//
// 默认复制了今天的行为,所以一个从未打开过 面板
// 得到准确的 沙箱 他们已经做了。
maya::published<const sandbox_cfg::Config> g_cfg;

// 保险单被封存了吗? set_config() 调用.
//
// 平庸的原子波澜 而不是 "g_cfg != nullptr" ,
// 因为两者不是同一个问题: 封印即使有人有默认封印,也必须锁紧.
// -constructed 装配,然后把它们装配起来,
// 让第二 调用 每当第一次发表什么恶作剧的时候 都会被揭穿
std::atomic<bool> g_cfg_sealed{false};

// 只有 macOS 沙箱-exec 后端需要。 "can we run this binary?"
// 探测器 : Linux 请求brap 以获取一个真实的命名空间(见下文),
// Windows/unsupported 枝头硬的 -codes Backend::None.
#if defined(__APPLE__)

// 一个 -shot 探测器:尝试产卵 `<exe> --version`
// 并观察结果。 PATH 随行 实际语义检查 我们关心(
// 是否能运行这个二进制程序？
// 结果被丢弃了——退出代码0只是表示 二进制程序 存在并开始。
[[nodiscard]] bool can_invoke(const char* exe) {
    SubprocessOptions opts;
    opts.command = SubprocessOptions::Argv{{exe, "--version"}};
    opts.timeout = std::chrono::seconds{2};
    opts.max_bytes = 4096;
    auto r = Subprocess::run(std::move(opts));
    return r.started && r.exit_code == 0;
}

#endif // __APPLE__

#if defined(__linux__)

// A GENUINE 最小值( E) -sandbox 探测器。 `bwrap --version`
// (旧支票通过 can_invoke )仅证明: BINARY EXISTS ——它从未创建命名空间,
// 因此它传递到brap安装但非特权用户命名空间所在的主机上. BLOCKED: Ubuntu 24.04's
// AppArmor `userns` 限制, RHEL/hardened
// `kernel.unprivileged_userns_clone=0` 或 或 时 间
// `user.max_user_namespaces=0` ,并且很多 container/CI 主持人。
//     每一个 真实 命令随 brap 一起死亡: 设置 uid 映射 : 权限
// 拒绝时 agentty 很高兴接到报告 "sandbox: active (bwrap)" — GitHub 21号问题。
//
// 所以我们运行 相同 命名空间 解析一个针对小于 /
// bin/true 并需要它实际开始 和 出口 0。
// 这使得探测器成为了对 run_wrapped (:如果它不能在
// 这里建立命名空间,它也不能为 shell 建立它,我们报告没有后端,
// 所以Auto 降解为无色框,在表面会有一个清晰,可操作的错误,
// 而不是让每个命令失败.

[[nodiscard]] Backend probe() {
    // 一个 Linux 引擎,请输入 available() 是一个忠实的预测者,
    // spawn() -- probe_host (三) 叉子和图谋
    // uid_map 写而不是读音节-所以 "usable" 表示会成功,
    // 而不是内核广告发布该功能.
    //
    // 地上是地锁 OR seccomp ,但其中不需要用户命名空间。
    // 这就是一个引擎足够了的原因: Ubuntu 24.04 起
    // 促使第二个后端(a) AppArmor 拒绝配置文件
    // uid_map 写入, 直接杀死 bwrap ) 仍然在此
    // 获得文件系统边界和syscall 过滤器。
    // 墙壁报告显示每个能力都有一个特定主机没有达到的, 因此一个降低的
    // 姿态是可见的而不是沉默的。
#if defined(__linux__)
    if (claybin_backend::available()) return Backend::Claybin;
#endif
    return Backend::None;
}

// 构建 bwrap argv 前缀。 工作区 读取 -write 约束本身;
// 系统 dirs 绑定为 -only 这样贝壳就可以找到 / bin/sh ,
// 自由语, / etc/resolv.conf 等; /tmp是新鲜的
// tmpfs(没有从主机/tmp中渗入/); /proc和/dev是最小的;
// 网络命名空间是共享的(所以) `git push` /
// `npm install` / `curl` 继续工作... 沙箱隔离 合法
// 代理流动用户希望能够工作。
//
// 意图是
// bash 可在工作区内操作、联网和读取系统库，但不能修改 /etc、其他项目主目录、~/.ssh 或 /opt 等位置。
// 这就是我们实际要抵御的威胁模式: 一个被破坏或草率的
// 模型运行 `rm -rf ~` 或 或 时 间
// `cat ~/.ssh/id_rsa` 在一个用户批准后。
//
// `--die-with-parent` 确保 沙箱 死亡,如果 agentty 死亡
// ——没有脱落的僵尸. `--unshare-pid` 给孩子一个干净的 PID
// 命名空间，使终止操作可靠生效。--new-session 防止子进程
// 偷控制Tty。
// ---------------------------------------------------------------------------
// 该 READ SET.
//
// 命名列表而不是内嵌墙壁 --ro-bind-try ,
// 因为这是安全边界,因此应该作为一个可以读取。每个条目都是为了一个
// 明确的理由而在这里的;没有列出的任何东西都不是 可从内部读取 沙箱.
//
// 保留为数据而不是打开 -coded 因为第二个后端曾经用手重现了这个集,
// 却弄错了 -- -- 授权读到 "/" , 已经交接 ~/.ssh,
// ~/.aws 财务报告和已审计财务报表 ~/.config 任何经核准的
// `bash` 调用 读 + 过滤器, 准确的说 沙箱 存在以关闭,
// 并且隐形, 因为它仍然被报告 "sandbox: active"。一个列表,
// 所以未来的后端会扩展它而不是用参数来解释它。
//
// 可见 两者 平台武器,因为 claybin 后端会消耗
// 它们 Linux 继续 macOS 这也是清单的
// 要点:一个边界定义,扩大而不是解释,无论许多后端读到它。
#endif  // __linux__(在下文重新开放)

#if defined(__linux__) || defined(__APPLE__)

// 系统根:工具链和共享 库 读取 -only ,没有/etc ——参见
// kEtcReadable.
constexpr const char* kSystemReadRoots[] = {
    "/usr", "/bin", "/lib", "/lib64", "/sbin", "/opt",
#if defined(__APPLE__)
    // Darwin的工具链是NOT under /usr:/usr/bin/git,cc,
    // python3,make是 xcrun shims,从活动开发者 dir 中
    // dlopen libxcrun, Re-exec 才是那里真正的工具。没有这些工具,
    // 每个工具就与 "xcrun: error: unable to load libxcrun"
    // 一起死去 -- -- 默认姿态,因为 沙箱 首次打开是 macOS。
    // 开发者 dire 是在姿态时间解决的( xcode-select
    // 可以在任何地方指向它); 这是它生活的两个固定地点。
    "/System", "/Library/Developer/CommandLineTools",
    // 整个捆绑,不只是 Contents/Developer : xcrun (中文(简体) ).
    // 加载 SharedFrameworks 财务报告和已审计财务报表 SystemFrameworks 旁边
    "/Applications/Xcode.app/Contents",
    "/private/var/select",
    // 当地时间(3):每 `date` , 木和蟒蛇日期时间。
    "/private/var/db/timezone",
#endif
};

// /etc: 特定文件 a shell + 工具链 + 名称分辨率需要,
// 不 源码树. 绑定 /etC全部向拥有网络的指令公开主机机密(/etc/shadow可读,
// krb5 keytabs, company config).
constexpr const char* kEtcReadable[] = {
    "/etc/resolv.conf",   // DNS
    "/etc/hosts",
    "/etc/nsswitch.conf", // NSS 解析顺序
    "/etc/host.conf",
    "/etc/passwd",        // uid - > 名称( git, shells)
    "/etc/group",
    "/etc/localtime",     // 时间戳
    "/etc/ssl",           // TLS 信托存储( E) curl/git 超过 https 时
    "/etc/pki",           // RHEL/Fedora信托商店
    "/etc/ca-certificates",
    "/etc/ca-certificates.conf",
    "/etc/gitconfig",     // 系统 git 配置
    "/etc/profile",
    "/etc/alternatives",  // Debian 工具链链接
};

// 用户 -local 工具链( 工具链) GitHub 21 期。许多工具链安装 OUTSIDE /
// usr - 电话 webinstall.dev 滴数 go/gofmt/node 下级 ~/.local/opt
// 财务报告和已审计财务报表 链接 进入 ~/.local/bin; rustup/cargo 去吧,
// Nvm,pyenv,rbenv, 灰尘,包子,deno和sdkman都住在下面 $HOME。
// 没有这些 "approved" 狂欢吧 调用 找不到用户要求代理运行的工具。
//
// 故意的 EXCLUDES 混合到 secrets/app-data: ~/.local/share (应用数据),
// ~/.npm (可以,可以,但可以,可以) 缓存 自动符号, ~/.config (红桃),
// ~/.local/state (logs/history ——当然 ~/.ssh 财务报告和已审计财务报表 ~/.aws.
constexpr const char* kHomeToolSubdirs[] = {
    "/.local/bin", "/.local/opt", "/.local/lib", // webinstall.dev 等 语 文
    // 韦宾斯图尔把一些包放在下面 ~/.local/xbin
    // 而不是 bin - 同一工具,不同的目录, 和代理找不到
    // 的工具与损坏的工具是无法区分的 沙箱 从用户的侧面。
    "/.local/xbin",
    "/.cargo/bin", "/.rustup",   // rust( 仅从. cargo 中选择)
    "/go/bin", "/.go",           // 前进( G) GOPATH 宾式 + Webinstall)
    "/.nvm",                     // 节点版本管理器
    "/.pyenv", "/.rbenv", "/.asdf", // 版本管理器
    "/.bun/bin", "/.deno/bin",   // 包/ 德诺
    "/.dotnet", "/.sdkman/candidates", // .NET / JVM
    // git 的用户配置: 身份,别名,默认分支. git 处理 an UNREADABLE
    // ~/.gitconfig 致命( 4) "unable to access" ),
    // 而不是缺席,所以没有它 每一个git命令在内部 沙箱 已死亡 -- -- 状态和日志包含。
    // 已读 -only ,并显示 凭据 存储(.git) -credentials ,
    // h's(英语: hosts.yml 保持面具:一名助手 NAMED 这儿还有没有东西
    // 要读。
    "/.gitconfig", "/.config/git",
};
#endif  // __linux__ || __APPLE__

// Windows 三个名单的名字,在建立时按态势解决:
// 工具链和 shell 生活在系统根和程序文件下,用户配置文件是其中的 凭据 卫生仍然
// 受到侵扰(a) Windows .sh 位于% USERPROFILE ⁇   ⁇  ,
// ⁇  以% %为单位 USERPROFILE ⁇ ,就像POSix一样。
// claybin '窗口 ' 编译器 带 NATIVE 路径; 这些是本地的。
#if defined(_WIN32)
// ⁇ ,  ⁇ , 回退}. Windows 并不是总在C上: 所以读一下真正的根
constexpr std::pair<const char*, const char*> kSystemReadRootsWin[] = {
    {"SystemRoot", "C:\\Windows"},                  // 系统32装载机,DLLs
    {"ProgramFiles", "C:\\Program Files"},           // 已安装的工具
    {"ProgramFiles(x86)", "C:\\Program Files (x86)"},
};
// "readable machine config" 百分比 ProgramData 一半的 /etc 上是 posix。
constexpr std::pair<const char*, const char*> kEtcReadableWin[] = {
    {"ProgramData", "C:\\ProgramData"},  // 软件包管理器元数据, 主机 ssh 配置
};
[[nodiscard]] std::string win_dir(const std::pair<const char*, const char*>& d) {
    const char* v = std::getenv(d.first);
    return v && *v ? std::string{v} : std::string{d.second};
}
constexpr const char* kHomeToolSubdirsWin[] = {
    "\\.cargo\\bin", "\\.rustup",   // 锈
    "\\go\\bin",                     // Go
    "\\.nvm",                        // 节点版本管理器
    "\\.pyenv", "\\.rbenv",          // 版本管理器
    "\\.bun\\bin", "\\.deno\\bin",   // 包/ 德诺
    "\\.dotnet",                     // .NET
};
#endif  // _ WIN32 语句

// 回到每个 -platform 手持共享的读取集中断。
#if defined(__linux__)


// 读数集,交给 claybin 以 DATA 而不是在它的后端重复。
//
// 此功能使两个后端保持诚实。 两者都使用 kSystemReadRoots
// / kEtcReadable / kHomeTooLSubdirs,
// 所以边界的更改既适用于两个, 也适用于两个。 先前的后端手动重现了设定, 并最后被允许读取。
// `/` -- 递递 ~/.ssh 财务报告和已审计财务报表 ~/.aws 任何经
// 批准的狂欢 调用 仍在报告 "sandbox: active".
//
// POSIX-wide 而不是说 Linux-only : 数据
// macOS claybin 后端会消耗相同的 Posture。
// 这就是将边界描述为数据而不是 argv 的回报 — 读取的集合、
// 口罩和 面板 额外 授权 跨越平台线不变,只有执行这些线的
// 机制不同。
//
// 紧接着下面的#endif,关闭了 Linux 臂上打开的: 必须对此函数
// 可见 两者 编号 Linux 编辑 macOS run_wrapped ()
// 和(c) 百分比 -platform 手臂在它之后就重新打开了。
#endif  // __linux__(在下文重新开放)

// 严格下方, a JIT 命令获取 W ^X 解除或根本无法启动。
// 严格(Hardened, Airgapped)保留墙壁,建筑则失败.
void relax_wx_for_jit(claybin_backend::Posture& p, std::string_view cmd) {
    if (p.wx_protect && p.syscall_mode != 2 && command_needs_jit(cmd))
        p.wx_protect = false;
}
[[maybe_unused]] std::string join_argv(const std::vector<std::string>& argv) {
    std::string s;
    for (const auto& a : argv) { s += a; s += ' '; }
    return s;
}

#if defined(__linux__) || defined(__APPLE__) || defined(_WIN32)
// 其中之一下可信任的文件的目录通过或无选择保护。
// 这些文件包含许多文件(并运行) 二进制程序 ),
// 所以一个读 -only 目录绑定替换每个文件绑定。
[[nodiscard]] std::optional<std::string> trusted_dir_of(const std::string& path,
                                                        const std::string& workspace) {
    static constexpr std::string_view kDirs[] = {
        ".local/bin", ".config/autostart", ".config/systemd",
        ".config/fish", ".config/environment.d", ".config/hypr",
        ".config/sway", ".config/i3", ".config/plasma-workspace",
    };
    for (auto rel : kDirs) {
        std::string dir = workspace;
        if (!dir.empty() && dir.back() != '/') dir += '/';
        dir += rel;
        if (path.size() > dir.size() && path.starts_with(dir) && path[dir.size()] == '/')
            return dir;
    }
    return std::nullopt;
}

[[nodiscard]] claybin_backend::Posture build_claybin_posture(
        const handoff::TrustedSnapshot* trusted = nullptr) {
    claybin_backend::Posture p;

    // 用户的 已保存 策略,或未打开 面板.
    // `configured` 是什么阻止了升级 改变
    // 任何人的边界。
    //
    // 一个 快照 ,用于整个函数。此操作在一个工具上 工作线程
    // 线程 时 状态更新函数 线程 可能正在发布一项新政策,
    // 所以 -reading 将两种政策合并为一种
    // 政策 沙箱 - 读旧的路径,新来的syscall简介。
    // 持有 快照 也意味着a 保存 无法更改
    // 正在构建的命令的边界。
    const auto snap = config_snapshot();
    const auto& cfg = *snap;

    // ── 内容,按范围分列 ────────────────────────────────────────
    //
    // Minimal 故意省略家用工具链 dirs: 对于
    // 自己 -contained 仓库 家庭目录里的任何东西都是可疑的
    // HostReadable 是一个方便的答案, 其它代理,
    // 诚实地命名 -- 它仍然被安装器的 DACLs 和 面具列表所限制,
    // 但是您的其他项目可以被一个批准的命令读取。 Windows 列表为原生
    // 路径(kSystemReadRootsWin等); 家居 var为
    // USERPROFILE 那边,还有 HOME 其他地方都无处可去。
#if defined(_WIN32)
    for (const auto& r : kSystemReadRootsWin) p.system_read_roots.push_back(win_dir(r));
    for (const auto& f : kEtcReadableWin) p.etc_readable.push_back(win_dir(f));
    if (cfg.fs_scope == sandbox_cfg::FsScope::Toolchain) {
        if (const char* home = std::getenv("USERPROFILE"); home && *home) {
            for (const char* sub : kHomeToolSubdirsWin)
                p.home_tool_dirs.emplace_back(std::string{home} + sub);
        }
    } else if (cfg.fs_scope == sandbox_cfg::FsScope::HostReadable) {
        if (const char* drive = std::getenv("SystemDrive"); drive && *drive)
            p.system_read_roots.emplace_back(std::string{drive} + "\\");
        else
            p.system_read_roots.emplace_back("C:\\");
    }
#else
    for (const char* r : kSystemReadRoots) p.system_read_roots.emplace_back(r);
    for (const char* f : kEtcReadable) p.etc_readable.emplace_back(f);
    if (cfg.fs_scope == sandbox_cfg::FsScope::Toolchain) {
        if (const char* home = std::getenv("HOME"); home && *home) {
            for (const char* sub : kHomeToolSubdirs)
                p.home_tool_dirs.emplace_back(std::string{home} + sub);
        }
    } else if (cfg.fs_scope == sandbox_cfg::FsScope::HostReadable) {
        // `/` 单独: 绑定 /usr 等,在读数下 -only `/`
        // 未能创建它们的挂点, 杀死每一个产卵。
        p.system_read_roots = {"/"};
        p.etc_readable.clear();
    }
#endif
    // 无论用户在上面添加什么,都绝对( claybin 忽略 `~`
    // 和相对路径。
    p.workspace = workspace_root().string();
    {
#if defined(_WIN32)
        const char* h = std::getenv("USERPROFILE");
#else
        const char* h = std::getenv("HOME");
#endif
        const std::string_view home = h ? h : "";
        for (const auto& r : cfg.read_paths)
            if (auto a = sandbox_cfg::resolve_user_path(r, p.workspace, home); !a.empty())
                p.read_paths.push_back(std::move(a));
        for (const auto& w : cfg.write_paths)
            if (auto a = sandbox_cfg::resolve_user_path(w, p.workspace, home); !a.empty())
                p.write_paths.push_back(std::move(a));
    }

#if defined(__APPLE__)
    // 达文人/ -user 临时和 缓存 日数(/ var/folders/xx/yyy/{T,C}).
    // 这里 $TMPDIR 点, 不 /tmp: mktemp, python 的临时文件,
    // xcrun 的工具 缓存 , crangg 模块 缓存 快速和每个可可工具 写在那里,
    // 没有它 usr/bin/git 失败, cc 失败 "couldn't create cache file"。
    // 该tmp -equivalent 会 议 日 程 和 议 程 Linux
    // tmpfs: 私下给这个uid, 并且持有任何命令都不可能自己写出来.
    for (int which : {_CS_DARWIN_USER_TEMP_DIR, _CS_DARWIN_USER_CACHE_DIR}) {
        char buf[1024];
        const std::size_t n = ::confstr(which, buf, sizeof buf);
        if (n > 1 && n <= sizeof buf) {
            std::string d{buf};
            while (d.size() > 1 && d.back() == '/') d.pop_back();
            p.write_paths.push_back(std::move(d));
        }
    }
#endif

    p.workspace = workspace_root().string();
    p.cwd = p.workspace;

    // ── 秘密,总是,加上用户自己的面具 ───────────────
    //
    // kAlwaysMasked 无法配置,这就是要点:
    // 用户可以关闭的控件来完成他们的构建工作,就是关闭的控制.
    // 电话 面板 能够 ADD 面具,永远不要去掉这些。
    //
    // 电话 LIST 与 brap 路径共享( Q) sandbox_cfg::mask_paths ),
    // 所以后端不能对所覆盖的内容有异议。 Windows 家前缀不同(%)
    // USERPROFILE 而不是% (%) $HOME );
    // 列表为 POSIX 词汇,所以每个平台都会在同一个名称上预先填上自己的家.
    {
#if defined(_WIN32)
        const char* home = std::getenv("USERPROFILE");
#else
        const char* home = std::getenv("HOME");
#endif
        auto masks = sandbox_cfg::mask_paths(cfg, p.workspace,
                                            home ? home : "");
        p.masked.insert(p.masked.end(),
                        std::make_move_iterator(masks.begin()),
                        std::make_move_iterator(masks.end()));
    }

#if !defined(_WIN32)
    // ── /dev/tty : 控制 终端 谓 agentty 自己的屏幕 ────
    //
    // 不是一个 凭据 ,所以它不是在 kAlwaysMasked
    // (该列表是 $HOME 秘密)——但出于同样的原因,
    // 它被蒙上了每个有设备的后端. 源码树. Windows
    // 没有/ dev/tty 节点:控制台是孩子继承(不在此)或
    // 打开的控件 CONIN$/CONOUT 被安装员的默认
    // 封锁了 -deny 因此,没有什么可以掩盖的,也没有省略的。
    //
    // claybin 的dertmpfs 绑定 HOST /dev/tty
    // 输入 沙箱 ,在bigbrap的节点列表之后。该节点将解析调用
    // 过程的控制 终端 ,所以一个打开它的孩子 直接写到屏幕上 - 绕
    // 过管道 字节 clean_capture () 擦洗剂。
    // `printf '\033[2J\033[3J' > /dev/tty` 擦掉记录
    //
    // 遮罩是正确的机制,而不是放弃节点:缺 /
    // dev/tty 更改用于探测某程序的行为 终端 ,
    // 而一个蒙面的仍然打开并接受写作——它们只是无处可去。
    // stdout/stderr 无人知晓。
    p.masked.emplace_back("/dev/tty");
#endif

    // ── 信任( E) -handoff 预防:约束宿主 -trusted 路径已读取 -only ─────
    //
    // 电话 PREVENTION 探测( handoff::review_trusted
    // 报告之后的文字 字节 着陆和工作在每个主机上;这停止了写作,
    // 需要一个挂载命名空间.
    //
    // 重用( E) handoff::snapshot_trusted
    // 所以两个半人不能不同意 对应的 路径为主机 -trusted -
    // 上面的掩码列表对brap vs 的记录的同样推理 claybin ,
    // 其中两张漂移,其中一张不存在。
    //
    // 仅 EXISTING 路径:没有失败的路径的束缚,
    // 以及 bind_try 容忍这与想要的不一样。一个值得信赖
    // 的道路 CREATED 测试是用来检测的。
    //
    // 当允许此策略时跳过——一个关闭了门的用户说
    // 这些文件是他们的编辑,默默地让它们读
    // -only将是欺骗他们的配置.
    if (cfg.handoff != sandbox_cfg::HandoffPolicy::Allow && !p.workspace.empty()) {
        // 电话 分派器 已经走过 工作区 用于此 调用
        // 重复使用它,而不是再次走动, 当它走这根。
        if (trusted && (trusted->roots.empty() || trusted->roots.front() != p.workspace))
            trusted = nullptr;
        handoff::TrustedSnapshot walked;
        if (!trusted) walked = handoff::snapshot_trusted(p.workspace);
        const auto& snap = trusted ? *trusted : walked;
        p.handoff_ro.reserve(snap.entries.size());
        // a 下的文件 .git/hooks 或 或 时 间
        // .git/info 内脏的绑定是一罗,而不是一个。 仓库 拥有上千个
        std::set<std::string> dirs;
        // 蒙面的路径必须蒙面,交接的行走匹配 agentty 钩子
        // -config 迪尔,所以 工作区 时间 $HOME
        // 发现 ~/.agentty/memory.jsonl 并绑
        // 定真实文件 -only 将任何蒙面或戴面具的物体都省略。
        auto under_mask = [&](const std::string& path) {
            for (const auto& m : p.masked) {
                if (path == m) return true;
                if (path.size() > m.size() && path.starts_with(m)
                    && path[m.size()] == '/')
                    return true;
            }
            return false;
        };
        for (const auto& e : snap.entries) {
            if (!e.existed) continue;
            if (under_mask(e.path)) continue;
            // 束缚沿循链接,使其无法保护,而指出源码树的链接
            // 使得claybin的封闭开放失败(EXDEV).
            std::error_code lec;
            if (fs::is_symlink(fs::symlink_status(e.path, lec))) continue;
            const fs::path parent = fs::path{e.path}.parent_path();
            const auto pname = parent.filename();
            if ((pname == "hooks" || pname == "info")
                && parent.parent_path().filename() == ".git"
                && !fs::is_symlink(fs::symlink_status(parent, lec))) {
                if (dirs.insert(parent.string()).second && !under_mask(parent.string()))
                    p.handoff_ro.push_back(parent.string());
                continue;
            }
            // 登录和 PATH 迪尔:一个罗绑,一个文件一个。
            // 绑定 文件需要 claybin 打开它以写入,
            // 但它失败了 ETXTBSY 时间: 二进制程序 输入
            // ~/.local/bin 正在运行中, 并且它使每一个产卵都失败。
            if (const auto d = trusted_dir_of(e.path, p.workspace)) {
                if (dirs.insert(*d).second && !under_mask(*d)
                    && !fs::is_symlink(fs::symlink_status(*d, lec)))
                    p.handoff_ro.push_back(*d);
                continue;
            }
            p.handoff_ro.push_back(e.path);
        }
    }

    // ── 网络 ──────────────────────────────────────────────────────────
    p.net_mode = static_cast<int>(cfg.net_mode);
    p.allow_ports = cfg.allow_ports;
    p.allow_hosts = cfg.allow_hosts;

    // ── 响尾蛇和盖子 ────────────────────────────────────────────────
    p.syscall_mode = static_cast<int>(cfg.syscall_mode);
    p.wx_protect = cfg.wx_protect;
    p.scope_ipc = cfg.scope_ipc;
    p.close_inherited_fds = cfg.close_inherited_fds;
    p.tmp_bytes = cfg.tmp_mb * 1024ull * 1024;
    p.memory_bytes = cfg.memory_mb * 1024ull * 1024;
    p.max_processes = cfg.max_procs;
    p.cpu_percent = cfg.cpu_percent;
    p.max_open_files = cfg.max_open_files;
    p.wall_clock_secs = cfg.wall_clock_secs;
    p.cpu_secs = cfg.cpu_secs;
    p.fake_hostname = cfg.fake_hostname;
    // 每当过滤器在时, 都会进行经纪, 因为跑者总是可以向
    // 听众投票, —— 它已经拥有了输出管道的民意测验循环。
    // 放在过滤器上,而不是单独排行:中介 IS
    // 音响过滤器决定在 运行时 而不是放在前面,
    // 所以一个关闭过滤器的用户已经说过他们也不要。
    p.broker = cfg.syscall_mode != sandbox_cfg::SyscallMode::Off;
    return p;
}
#endif  // __linux__ || __APPLE__ || _ WIN32 语句

// 回到每个 -platform 手持姿态建造器中断。
//
// Linux 取第一个分支( 其) run_wrapped), macOS 第二次,
// 我问他 Windows 第三,警卫必须留在林纳 -ONLY: a `|| _WIN32`
// 这里会带林克斯的分支打开 Windows 编辑 probe() 定义从不会
// 编译 - 唯一一个 init() 需要帮助。
#if defined(__linux__)

[[nodiscard]] SubprocessResult run_wrapped(std::string_view cmd,
                                           std::size_t max_bytes,
                                           std::chrono::seconds timeout) {
    SubprocessOptions opts;
    opts.max_bytes = max_bytes;
    opts.timeout = timeout;

#if defined(__linux__) || defined(__APPLE__)
    if (detected_backend() == Backend::Claybin) {
        // claybin 是一个 库 ,所以没有argv前缀可以构建。它叉,
        // 应用计划,执行自己——但只有 SPAWN ; 监督循环( 弹出、
        // 进展、 闲置的截止时间, SIGTERM/SIGKILL ,
        // 收获)留在 Subprocess::run ,与其它路径共享.
        //
        // 两个平台上都一样, 也就是保持后端的监管循环的
        // 完整回报: Linux 手拿回来一个短发和一个
        // seccomp-notify 联邦, macOS
        // 手也不要,跑者不需要分枝来分辨——哨兵值已经说了.
        //
        // `command`仍然携带着 shell 字符串,
        // 所以跑者记录和错误消息读取两个后端都是一样的.
        opts.command = SubprocessOptions::Shell{std::string{cmd}};
        std::string shell_cmd{cmd};
        opts.spawner = [shell_cmd](const SubprocessOptions&,
                                   int pipe_write_fd) -> SubprocessOptions::SpawnedChild {
            SubprocessOptions::SpawnedChild out;
            auto posture = build_claybin_posture();
            relax_wx_for_jit(posture, shell_cmd);
            // 两者 数据流 跑者已经做的管子上
            auto r = claybin_backend::spawn_shell(posture, shell_cmd, pipe_write_fd,
                                                  pipe_write_fd);
            if (!r.started) {
                out.error = r.start_error;
                return out;
            }
            out.pid   = r.pid;
            out.pidfd = r.pidfd;   // jaal 收获的:没有零食 -reuse 竞态
            // 将线程上的监督交给跑者,后者在输出管道旁进行投票。
            // 它必须被调查到孩子的整个生命:内核中
            // 一个中间的线程上监督块,直到有人回答。
            out.supervisor_fd = r.supervisor_fd;
            out.service = std::move(r.service_broker);
            // 过滤过的网络代理拥有在嘉宾网内播放的套接字。
            // 下方不需要知道这一点; 它只需要超过孩子,
            // 否则嘉宾的连接会开始失败。
            out.keepalive = std::move(r.net_proxy);
            // 除非姿态实际得到一个带有cgroup.kill
            // 的cgroup,否则空空。 运行者返回到进程组,
            // 进程组更弱( setsid 逃脱) , 但只有它。
            out.kill_tree = std::move(r.kill_tree);
            return out;
        };
        return Subprocess::run(std::move(opts));
    }
#endif

    // 无后端 : run_wrapped 仅在 沙箱 活动,
    // 并运行 Linux "active" 表示 claybin
    // 到达这里意味着探测器说是,产卵路径不同意,
    // 所以拒绝而不是运行未关闭的命令——这个命令是建立在它
    // 将被装箱的理解基础上获得批准的,决不能悄悄地逃脱盒子。
    SubprocessResult unconfined;
    unconfined.started     = false;
    unconfined.start_error = "sandbox is active but no backend could wrap "
                             "this command; refusing to run it unconfined";
    return unconfined;
}

// 参数v -form : 与 shell 形式相同的包件, 用户的 argv 完全按照给定
// 的给定到达孩子—— 不 `sh -c` 间接到 re -parse 引用或
// `$vars`.
[[nodiscard]] SubprocessResult run_wrapped_argv(const std::vector<std::string>& user_argv,
                                                std::size_t max_bytes,
                                                std::chrono::seconds timeout) {
    if (user_argv.empty()) {
        SubprocessResult r;
        r.started = false; r.start_error = "empty argv";
        return r;
    }

    SubprocessOptions opts;
    opts.max_bytes   = max_bytes;
    opts.timeout     = timeout;

#if defined(__linux__) || defined(_WIN32)
    if (detected_backend() == Backend::Claybin) {
        opts.command = SubprocessOptions::Argv{user_argv};
        std::vector<std::string> argv = user_argv;
        opts.spawner = [argv](const SubprocessOptions&,
                              int pipe_write_fd) -> SubprocessOptions::SpawnedChild {
            SubprocessOptions::SpawnedChild out;
            auto posture = build_claybin_posture();
            relax_wx_for_jit(posture, join_argv(argv));
            auto r = claybin_backend::spawn_argv(posture, argv, pipe_write_fd,
                                                 pipe_write_fd);
            if (!r.started) { out.error = r.start_error; return out; }
            out.pid           = r.pid;
            out.pidfd         = r.pidfd;
            out.supervisor_fd = r.supervisor_fd;
            out.service       = std::move(r.service_broker);
            out.keepalive     = std::move(r.net_proxy);
            out.kill_tree     = std::move(r.kill_tree);
            return out;
        };
        return Subprocess::run(std::move(opts));
    }
#endif

    SubprocessResult unconfined;
    unconfined.started     = false;
    unconfined.start_error = "sandbox is active but no backend could wrap "
                             "this command; refusing to run it unconfined";
    return unconfined;
}

#elif defined(__APPLE__)

[[nodiscard]] Backend probe() {
    // claybin 首先,当被问及时 和 可用。
    //
    // 这里的两台引擎都是安全带... claybin 调用 sandbox_init
    // (d) 直接从汇编的政策中得出, 沙箱-exec 苹果的,
    // 你来 CLI 围绕同一个内核机制的包裹-- 所以这不是一个力量的选择。
    // 而是对于旅途中生存下来的事物的选择。
    //
    // build_profile ()下面缩写固定的剖面字符串,
    // 以便它能够确切地表达它写来表达的内容: 工作区 ,/tmp, 缩写为: 缓存。
    // 该词 claybin 路径 编译 同样的姿态 Linux 后端会消耗,
    // 所以 面板's read_paths, write_paths 面具和资源
    // 帽 意味着在mac上的东西 而不是是 Linux-only 设置,
    // 但在此不作任何静默。它也生成保证报告,这是值得的:
    // "filesystem.read strong via seatbelt" 以
    // 某种方式可检查 "sandbox: active" 才没有呢
    //
    // 优先选择与 Linux 而不是给第二个把手。用户的回答是:
    // "which engine applies my policy" 当它们移动到mac和
    // a之间时,不会改变 Linux 框中选择一个选项。
    //
    // claybin 首先,在它可以建造的地方,
    // 沙箱-exec 两者都是真正的界限;顺序只是倾向于
    // 政策词汇的词组: 面板 说得没错
    if (claybin_backend::available()) return Backend::Claybin;
    if (can_invoke("sandbox-exec")) return Backend::SandboxExec;
    // 另一方向,与对称的 Ubuntu 24.04个案例: 沙箱
    // -exec 不知何故缺失( Q) SIP 损害, 剥去的影像,
    // claybin 仍然直接和内核对话,不需要 二进制程序.
    if (claybin_backend::available()) return Backend::Claybin;
    return Backend::None;
}

// 生成最小值 沙箱-exec 配置文件。允许阅读范围很广,
// 限制写到 工作区 + tmp + 系统 缓存 ,允许网络(与
// bwrap 相同的理由:代理) -typical 命令需要它)。
//
// 苹果在公开文件中贬值了`sandbox-exec`,
// 但二进制程序仍然有效。 配置语言是Scheme-ish; 我们保持小幅,
// 因此未来的苹果删除很容易被发现。
[[nodiscard]] std::string build_profile(std::string_view workspace) {
    std::string p;
    p += "(version 1)\n";
    p += "(deny default)\n";
    // 过程/信号
    p += "(allow process-exec)\n";
    p += "(allow process-fork)\n";
    p += "(allow signal (target same-sandbox))\n";
    // 读取:宽. 凭据路径在剖析图结尾处被拒绝.
    p += "(allow file-read*)\n";
    // 写作 : 工作区 + tmp/cache 仅限区域。 工作区 路径是
    // SBPL-escaped (见A/C.5/61/L.48,第16段)
    // sbpl_escape 如果无法安全地代表(控制字符),则 工作区 写入条款为
    // OMITTED 完全而不是冒险a broken/injected 配置文件 -
    // 沙盒式的击打后得不到 工作区 写入访问, 安全( 未关闭) , 而不是绕行。
    if (std::string ws = sbpl_escape(workspace); !ws.empty())
        p += "(allow file-write* (subpath \"" + ws + "\"))\n";
    p += "(allow file-write* (subpath \"/tmp\"))\n";
    p += "(allow file-write* (subpath \"/private/tmp\"))\n";
    p += "(allow file-write* (subpath \"/private/var/folders\"))\n";   // 用户 缓存
    p += "(allow file-write* (subpath \"/dev/null\"))\n";
    // /dev/tty 故意的 故意的,故意的,是故意的,这是故意的 不 允许
    // ——参见bwrap后端上的长条注解 --dev-bind /
    // dev/null /dev/tty. 控制着我们 终端, i.e.
    // agentty 绕过管子 clean_capture ()擦擦。一个孩子写作
    // \ 033[2J\033][3J] 擦拭记录。 stdout/stderr
    // 不受影响;这只会消除儿童涂抹 UI 直接来
    // 网络: 打开。 限制会打破 git 推/ 卷/ npm。
    p += "(allow network*)\n";
    p += "(allow system-socket)\n";
    // 制表服务 BY NAME 裸露( 低沉的泥浆) -lookup 让
    // 一位嘉宾阅读贴板,并通过运行在外侧的启动守护进程列出键链 沙箱
    // 相同 允许清单 claybin's macOS 后端发件 :
    // 目录查看,日志,预览,加上自网络开放以来的DNS/TLS.
    p += "(allow mach-lookup\n"
         "  (global-name \"com.apple.system.opendirectoryd.libinfo\")\n"
         "  (global-name \"com.apple.system.logger\")\n"
         "  (global-name \"com.apple.logd\")\n"
         "  (global-name \"com.apple.diagnosticd\")\n"
         "  (global-name \"com.apple.PowerManagement.control\")\n"
         "  (global-name \"com.apple.cfprefsd.daemon\")\n"
         "  (global-name \"com.apple.cfprefsd.agent\")\n"
         "  (global-name \"com.apple.dnssd.service\")\n"
         "  (global-name \"com.apple.mDNSResponder\")\n"
         "  (global-name \"com.apple.SystemConfiguration.DNSConfiguration\")\n"
         "  (global-name \"com.apple.SystemConfiguration.configd\")\n"
         "  (global-name \"com.apple.networkd\")\n"
         "  (global-name \"com.apple.trustd\")\n"
         "  (global-name \"com.apple.trustd.agent\")\n"
         "  (global-name \"com.apple.ocspd\"))\n";
    p += "(allow pseudo-tty)\n";
    p += "(allow file-read* file-write* file-ioctl (literal \"/dev/ptmx\"))\n";
    p += "(allow ipc-posix-sem)\n";
    p += "(allow iokit-open)\n";
    p += "(allow sysctl-read)\n";
    // 凭据 路径, 相同的列表 claybin 口罩。最后,
    // 因为后一条规则获胜 SBPL 编辑 工作区 上面写允许的话, 将会重新打开它们。
    {
        const char* home = std::getenv("HOME");
        const auto snap = config_snapshot();
        for (const auto& m : sandbox_cfg::mask_paths(*snap, workspace, home ? home : ""))
            if (std::string esc = sbpl_escape(m); !esc.empty())
                p += "(deny file-read* file-write* (subpath \"" + esc + "\"))\n";
    }
    return p;
}

[[nodiscard]] SubprocessResult run_wrapped(std::string_view cmd,
                                           std::size_t max_bytes,
                                           std::chrono::seconds timeout) {
    SubprocessOptions opts;
    opts.max_bytes = max_bytes;
    opts.timeout = timeout;

    if (detected_backend() == Backend::Claybin) {
        // 不 claybin 产卵+收养,如: Linux 达文没有皮德,
        // 所以 jaal's posix_process::adopt 拒绝每
        // 个外国儿童( "the exit is not watchable"
        // 拒绝的来了 之后 产卵,所以客人跑 无人监督,而呼叫者得到了128
        // 出口 没有输出 - 每个沙箱钩和 ACP 终端 命令无声失败。
        // 改为: claybin '通过 argv 编译为 profile 的文件
        // 沙箱-exec ,恰好是击打工具所做的( JaalExec::confine):
        // 同样的姿势,孩子是我们的叉子,所以它可以观察。
        auto wrapped = wrap_argv_for_exec({"/bin/sh", "-c", std::string{cmd}}, {}, {});
        if (!wrapped) {
            SubprocessResult r;
            r.started = false;
            r.start_error = "sandbox: " + wrapped.error();
            return r;
        }
        opts.command = SubprocessOptions::Argv{std::move(*wrapped)};
        return Subprocess::run(std::move(opts));
    }

    auto profile = build_profile(workspace_root().string());
    opts.command = SubprocessOptions::Argv{{
        "sandbox-exec", "-p", std::move(profile),
        "/bin/sh", "-c", std::string{cmd}
    }};
    return Subprocess::run(std::move(opts));
}

[[nodiscard]] SubprocessResult run_wrapped_argv(const std::vector<std::string>& user_argv,
                                                std::size_t max_bytes,
                                                std::chrono::seconds timeout) {
    if (user_argv.empty()) {
        SubprocessResult r;
        r.started = false; r.start_error = "empty argv";
        return r;
    }
    SubprocessOptions opts;
    auto profile = build_profile(workspace_root().string());
    std::vector<std::string> argv{"sandbox-exec", "-p", std::move(profile)};
    for (const auto& a : user_argv) argv.push_back(a);
    opts.command = SubprocessOptions::Argv{std::move(argv)};
    opts.max_bytes = max_bytes;
    opts.timeout = timeout;
    return Subprocess::run(std::move(opts));
}

#elif defined(_WIN32)

// A 真实 能力探测器,不是表格搜索: claybin's
// probe_host () 创建和删除一个安装器配置文件,
// 证明一个受限制的符号,由 RUNNING ,并初始化减轻属性列表。
// 地板是一面工作墙 — — 安装器或工作对象,因此,
// 只有当真正没有从中建出边界时,Auto才会降解。
[[nodiscard]] Backend probe() {
    if (claybin_backend::available()) return Backend::Claybin;
    return Backend::None;
}

[[nodiscard]] SubprocessResult run_wrapped(std::string_view cmd,
                                           std::size_t max_bytes,
                                           std::chrono::seconds timeout) {
    SubprocessOptions opts;
    opts.command = SubprocessOptions::Shell{std::string{cmd}};
    opts.max_bytes = max_bytes;
    opts.timeout   = timeout;

    if (detected_backend() == Backend::Claybin) {
        opts.spawner = [shell_cmd = std::string{cmd}](
                           const SubprocessOptions&,
                           int pipe_write_fd) -> SubprocessOptions::SpawnedChild {
            SubprocessOptions::SpawnedChild out;
            auto posture = build_claybin_posture();
            relax_wx_for_jit(posture, shell_cmd);
            // 两者 数据流 跳跃者已经做的管道上。
            // HANDLE 跑者用烟斗 HANDLE );
            // 适配器将其映射到孩子的 stdout/stderr.
            auto r = claybin_backend::spawn_shell(posture, shell_cmd, pipe_write_fd,
                                                  pipe_write_fd);
            if (!r.started) { out.error = r.start_error; return out; }
            out.pid = r.pid;
            // HANDLES, 不是描述符: 过程处理符是等待退出,
            // 任务是 kill switch。 跑者收养既消耗了两个功能。
            out.pidfd = r.pidfd;
            out.kill_tree = std::move(r.kill_tree);
            return out;
        };
    }
    return Subprocess::run(std::move(opts));
}

// 参数v -form : 用户的 argv 到达 claybin 产卵器完全如给定的——没有 cmd.exe
// /S/C 有关 -parsing 中引用或% vars% 的输入。与 linux 和% varc 的
// 合同相同。 macOS 手举起来
[[nodiscard]] SubprocessResult run_wrapped_argv(const std::vector<std::string>& user_argv,
                                                std::size_t max_bytes,
                                                std::chrono::seconds timeout) {
    if (user_argv.empty()) {
        SubprocessResult r;
        r.started = false; r.start_error = "empty argv";
        return r;
    }
    SubprocessOptions opts;
    opts.command = SubprocessOptions::Argv{user_argv};
    opts.max_bytes = max_bytes;
    opts.timeout   = timeout;

    if (detected_backend() == Backend::Claybin) {
        opts.spawner = [argv = user_argv](
                           const SubprocessOptions&,
                           int pipe_write_fd) -> SubprocessOptions::SpawnedChild {
            SubprocessOptions::SpawnedChild out;
            auto posture = build_claybin_posture();
            relax_wx_for_jit(posture, join_argv(argv));
            auto r = claybin_backend::spawn_argv(posture, argv, pipe_write_fd,
                                                 pipe_write_fd);
            if (!r.started) { out.error = r.start_error; return out; }
            out.pid = r.pid;
            out.pidfd = r.pidfd;
            out.keepalive = std::move(r.net_proxy);
            out.kill_tree = std::move(r.kill_tree);
            return out;
        };
    }
    return Subprocess::run(std::move(opts));
}

#else // Windows / 不支持

[[nodiscard]] Backend probe() { return Backend::None; }

[[nodiscard]] SubprocessResult run_wrapped(std::string_view cmd,
                                           std::size_t max_bytes,
                                           std::chrono::seconds timeout) {
    // 永远不要叫——is_active()在这个
    // 平台上返回虚假——但防御性地掉进未遮挡的路径.
    return run_command_s(std::string{cmd}, max_bytes, timeout);
}

[[nodiscard]] SubprocessResult run_wrapped_argv(const std::vector<std::string>& user_argv,
                                                std::size_t max_bytes,
                                                std::chrono::seconds timeout) {
    return run_argv_s(user_argv, max_bytes, timeout);
}

#endif

} // 命名空间结束。

bool init(Mode requested) {
    g_mode.store(requested, std::memory_order_release);
    auto found = (requested == Mode::Off) ? Backend::None : probe();
    g_backend.store(found, std::memory_order_release);
    if (requested == Mode::On && found == Backend::None) {
        // 严格模式 + 无后端 = init 失败。
        // 呼叫者默默决定是中止启动还是降级。
        return false;
    }
    return true;
}

Mode    requested_mode()   noexcept { return g_mode.load(std::memory_order_acquire); }
Backend detected_backend() noexcept { return g_backend.load(std::memory_order_acquire); }


void set_config(const sandbox_cfg::Config& cfg) {
    // SEALED 之后 该 首次 CALL 稍后时间 调用 被忽略了,说到做到。
    //
    // 这是整个安全模式的 面板 所以,这值得 明确声明。
    // agentty 正在运行: 更改主题和下一个 帧 被涂上了,
    // A 被涂上,被涂上了 被涂入了A 被漆成A 沙箱
    // 因为它的配置是 BOUNDARY ,
    // 而一个可以移动的边界 过程运行 有三个问题 没有多少护理修复:
    //
    //   1. 只能从得到 WEAKER 很有用,加紧中线 -session 谓
    //      放松是攻击者想要的,
    //      而 提示词-injected 能够到达设置的
    //      代理 状态更新函数 到底是谁想要的。
    //
    //   2. 弱化是: 不可见 财务报告和已审计财务报表 IRREVERSIBLE 切换 claybin ->
    //      静静地放下 seccomp 过滤器, croup 盖 和 非 国 家
    //      -configurable 秘密口罩 -- -- 测量:相同 工作区 .
    //      env 读回为零 字节 下级 claybin 财务报告和已审计财务报表
    //      `SECRET=leaked` 重新分类 -tightening 之后不会 -read
    //      已经留下的密钥。
    //
    //   3. 一个会话的两个命令得到两个不同的边界,即:
    //      记录里没有写出什么 也就是说 沙箱
    //      无法听懂,比弱者还糟糕 沙箱 这至少是
    //      已知的一件事。
    //
    // 因此,政策在启动时固定下来,从已保存设置加上CLI,
    // 以及用于下一次发射的面板XXWRITESTODISK。
    // 重新启动是应用步骤。这是一个真正的成本 —
    // — 无法尝试更严格的配置,它购买了这里唯一
    // 重要的财产:无论在本次会议上限制的第
    // 一个命令是否限制最后一个命令。
    //
    // 以守则而不是评论方式执行。 "call before init()"
    // 和 和 数据 面板 在一个存在日内破解它。
    if (g_cfg_sealed.exchange(true, std::memory_order_acq_rel)) {
        AGT_LOG(Tool, Warn, "sandbox.seal",
                "refused a live sandbox policy change: the boundary is sealed "
                "at startup and only a restart applies a new one");
        return;
    }

    // 第一届 调用 : 发布其中的 快照 这一过程将永远使用。
    //
    // 还不是可变 已出版 快照 而不是一个简单的全球。 READ 从工具 工作线程 线程 时 UI 线程
    // 也读它, shared_ptr-to-const
    // 读者如何获得一个连贯的视图,没有 锁 . (它也保持
    // 了真实用途) -after-free 出处 源码树
    // 重现时的功能: 配置 有三个向量, 并排的读数下分配一个
    // 是解脱的 -buffer 步行( 步行)。
    auto next = std::make_shared<const sandbox_cfg::Config>(cfg);
    g_cfg.publish(std::move(next));

    // 政策上带有ENGINE,所以封印它也封印了后端.
    //
    // 仅在用户实际选择时:未配置的配置符
}

bool config_sealed() noexcept {
    return g_cfg_sealed.load(std::memory_order_acquire);
}

void reset_config_for_test() noexcept {
    // 命令很重要,先放下配置,然后把封印放下,这样一个同时阅读的读者就永远
    // 看不到 "unsealed but still holding the old policy"
    // - 这个国家会让第二 set_config 读取器在中
    // 点时着陆 -snapshot.
    (void)g_cfg.take();
    g_cfg_sealed.store(false, std::memory_order_release);
}

std::shared_ptr<const sandbox_cfg::Config> config_snapshot() noexcept {
    auto snap = g_cfg.current();
    // 永远不要将空手还手, 因此呼叫者可以不检查就调试。
    // 在任何人设置一个配置之前, 默认配置是正确的答案: 它
    // 正是 bwrap argv 所构建的状态。
    if (!snap) {
        static const auto kDefault =
            std::make_shared<const sandbox_cfg::Config>();
        return kDefault;
    }
    return snap;
}

sandbox_cfg::Config config() {
    // BY 值 成本值得争论的
    // 地方
    //
    // 曾经是回来的 `const Config&` 将这个
    // 值重新在读者栏下公布,所以不能保证这个参考值的安全:
    // 将参考文献传送到 快照 意思是说 快照 最后
    // 一个所有者可以在调用者仍然持有引用时放下,
    // 调用者无法知道。 调用到共享、 可互换状态的引用是
    // 拖曳的指针, 上面有延缓。
    //
    // 所以,我们... UI-facing 用户名。调用者是窗体
    // 构建者和状态行——每个键键为少数,每个键键有三个小向量。
    // 高频路径 (每条指令一次产卵路径) config_snapshot
    // 且只付再计一分钱。
    return *config_snapshot();
}

bool config_enforceable() noexcept {
    const auto cfg = config_snapshot();
    // 没有配置 QQ 任何可忽略的。 默认配置正是 bwrap
    // argv 已经构建的姿态, 所以两个后端都 "enforce"。
    if (!cfg->configured) return true;
#if defined(__linux__) || defined(__APPLE__) || defined(_WIN32)
    // 关闭不是不执行 而是选择不执行 沙箱
    // 旗子已经自己报道了
    if (requested_mode() == Mode::Off) return true;
    return detected_backend() == Backend::Claybin;
#else
    return false;
#endif
}

bool is_active() noexcept {
    return requested_mode() != Mode::Off
        && detected_backend() != Backend::None;
}

bool workspace_is_home() {
    const char* home = std::getenv("HOME");
    if (!home || !*home) return false;
    std::error_code ec;
    return std::filesystem::equivalent(workspace_root(), home, ec) && !ec;
}

std::string describe_state() {
    auto m = requested_mode();
    auto b = detected_backend();
    if (m == Mode::Off) return "sandbox: off";
    const char* tag = nullptr;
    switch (b) {
        case Backend::Claybin:     tag = "claybin";      break;
        case Backend::SandboxExec: tag = "sandbox-exec"; break;
        case Backend::None:        tag = nullptr;        break;
    }
    if (tag) {
        // --workspace / rw 时间 -binds 整个
        // 文件系统: 仍然被包, 但没有文件系统封存。 老实说。
        std::error_code wec;
        auto ws = fs::weakly_canonical(workspace_root(), wec);
        if (wec) ws = workspace_root();
        if (ws == ws.root_path())
            return std::string{"sandbox: degraded ("} + tag
                 + ", --workspace / gives no filesystem containment)";
        return std::string{"sandbox: active ("} + tag + ")";
    }
    if (m == Mode::On)
        return "sandbox: requested but no backend "
#if defined(__linux__)
               "(the kernel offers neither landlock nor seccomp \xe2\x80\x94 "
               "nothing left to build a boundary from; run with --sandbox off)";
#elif defined(__APPLE__)
               "(sandbox-exec missing \xe2\x80\x94 system integrity issue)";
#elif defined(_WIN32)
               "(no sandbox wall could be built on this host \xe2\x80\x94 "
               "appcontainer profiles unavailable and no job objects; "
               "run with --sandbox off)";
#else
               "(unsupported on this platform)";
#endif
    // Mode::Auto + 无后端 → 掉进未装箱的
    return "sandbox: unavailable, running unsandboxed "
#if defined(__linux__)
           "(the kernel offers neither landlock nor seccomp)";
#elif defined(__APPLE__)
           "(sandbox-exec missing)";
#elif defined(_WIN32)
           "(no sandbox wall available: appcontainer unavailable)";
#else
           "(no backend on this platform)";
#endif
}

// 是否像SANDBOX拒绝的东西?
//
// ── 为什么是名副其实 ───────────────────────────────────────────
//
// 赤裸裸的 "Permission denied" 无法与真正的
// 错误区分开来, 模型读取它以完全自信来得出错误的结论: 它
// 责备一个公司代理, 告诉用户检查 CrowdStrike ,
// 或 ,或 重试 拒绝的同样 调用 每一个废物都转弯,没有人能起作用,
// 因为边界是内核决定。
//
// 系统 提示词 说 沙箱 已存在( 见
// provider/prompt.cpp 以至有帮助的,
// 但当错误降临的时候,它确是千兆的。 OUTPUT 把它放在
// 模型实际所看到的地方, 在它必须决定下一步要做什么的时刻。
// 这就是成熟的规律 沙箱 运行时间逐渐接近,
// 一半时间停止 重试 循环。
//
// ── 这里为什么图案-matching是声音 ──────────────────────────────
//
// 不能精确:内核否认、 libc 字符串,
// 程序会打印它喜欢的。 假 POSITIVE 模型
// 已经处理的错误额外花费一句; 错误 NEGATIVE
// 也不可能破坏任何东西,所以,广义的匹配是正确的交易,
// 这也是为什么它从来不声称 沙箱 DID 否认,
// 但有可能是这种原因。
[[nodiscard]] bool looks_like_denial(std::string_view out) {
    static constexpr std::string_view kSigns[] = {
        "Permission denied",
        "Operation not permitted",
        "Read-only file system",
        "Network is unreachable",
        "Could not resolve host",
        "Temporary failure in name resolution",
        "Connection refused",
        "Bad system call",            // a seccomp 杀死,就像贝壳报告的那样
        // 电话 沙箱 代理拒绝主机或地址。每个客户都不同,
        // 没有人说 "sandbox" : 卷曲、 基特和 pip
        // 报告失败 CONNECT 隧道,python 是一个隧道错误,
        // 两者都显示代理发送的403.
        "CONNECT tunnel failed",
        "Tunnel connection failed",
        "refused by the sandbox proxy",
        "Received HTTP code 403 from proxy",
    };
    for (auto s : kSigns)
        if (out.find(s) != std::string_view::npos) return true;
    return false;
}

// 附加在已拒绝的命令输出上的注释。
//
// 给 MODEL 并保持简短:它命名了原因,
// 禁止两个错误的动作( 重试 ,责备网络,并说要做什么。
// 只解释一个说明仍然让模型自由使用。 重试.
[[nodiscard]] std::string denial_note() {
    const auto cfg = config_snapshot();
    std::string n =
        "\n\n[sandbox] This command ran inside an OS-level sandbox, so a "
        "permission or network error above is most likely the sandbox "
        "refusing it \xe2\x80\x94 not a broken tool or the user's firewall. A "
        "proxy/tunnel/403 error is the sandbox's own network proxy.";
    if (cfg->net_mode == sandbox_cfg::NetMode::None)
        n += " Network access is BLOCKED in this session.";
    else if (cfg->net_mode == sandbox_cfg::NetMode::Ports)
        n += " Only specific ports are reachable in this session.";
    else if (!cfg->allow_hosts.empty()) {
        // 列表名称 : "a host was refused"
        // 发送模型猎取镜像; 列表告诉它主机 CAN 使用,或问什么。
        n += " Only these hosts are reachable: ";
        for (std::size_t i = 0; i < cfg->allow_hosts.size() && i < 12; ++i)
            n += (i ? ", " : "") + cfg->allow_hosts[i];
        if (cfg->allow_hosts.size() > 12) n += ", \xe2\x80\xa6";
        n += ". Any other host, and every IP address, is refused.";
    } else
        n += " Private, loopback and cloud-metadata addresses are refused.";
    n += " The workspace is writable; most paths outside it are not. "
         "Do not retry the same command unchanged \xe2\x80\x94 work inside the "
         "workspace, or tell the user which wall you hit and what access you "
         "would need.";
    return n;
}

SubprocessResult run_shell_command(std::string_view cmd,
                                   std::size_t max_bytes,
                                   std::chrono::seconds timeout) {
    if (!is_active())
        return run_command_s(std::string{cmd}, max_bytes, timeout);
    auto r = run_wrapped(cmd, max_bytes, timeout);
    // 只有在FAILURE上, 并且只有在输出看起来像墙时。
    // 每个命令的注释都会是噪声, 模型会学会跳过,
    // 这就是解释停止工作的方式。
    if (r.exit_code != 0 && looks_like_denial(r.output))
        r.output += denial_note();
    return r;
}

SubprocessResult run_argv(const std::vector<std::string>& argv,
                          std::size_t max_bytes,
                          std::chrono::seconds timeout) {
    if (!is_active())
        return run_argv_s(argv, max_bytes, timeout);
    auto r = run_wrapped_argv(argv, max_bytes, timeout);
    // 与壳道相同的注释: 这就是钩子和 ACP 终端 穿过,
    // 一个挂在墙上的钩子 很容易被误诊
    // 就像一击 调用.
    if (r.exit_code != 0 && looks_like_denial(r.output))
        r.output += denial_note();
    return r;
}

claybin_backend::SpawnResult spawn_in_sandbox(
        const std::vector<std::string>& argv, std::string_view cwd, int out_fd,
        const std::vector<std::string>& extra_roots,
        const handoff::TrustedSnapshot* trusted,
        const std::vector<std::pair<std::string, std::string>>& env) {
#if defined(__linux__) || defined(_WIN32)
    if (detected_backend() == Backend::Claybin) {
        auto posture = build_claybin_posture(trusted);
        relax_wx_for_jit(posture, join_argv(argv));
        if (!cwd.empty()) posture.cwd = std::string{cwd};
        // A 调用 被扩展到更多文件夹( 编辑会话的额外根)
        // 可能也会写到那里, 如 工作区.
        for (const auto& r : extra_roots)
            if (!r.empty()) posture.write_paths.push_back(r);
        return claybin_backend::spawn_argv(posture, argv, out_fd, out_fd, env);
    }
#else
    (void)argv; (void)cwd; (void)out_fd; (void)extra_roots; (void)trusted; (void)env;
#endif
    claybin_backend::SpawnResult r;
    r.started = false;
    r.start_error = "no claybin backend active";
    return r;
}

#if defined(__APPLE__)
std::expected<std::vector<std::string>, std::string>
wrap_argv_for_exec(const std::vector<std::string>& argv, std::string_view cwd,
                   const std::vector<std::string>& extra_roots) {
    if (argv.empty()) return std::unexpected(std::string{"empty argv"});
    std::string profile;
    std::vector<std::string> proxy_env;
    if (detected_backend() == Backend::Claybin) {
        auto posture = build_claybin_posture();
        relax_wx_for_jit(posture, join_argv(argv));
        if (!cwd.empty()) posture.cwd = std::string{cwd};
        for (const auto& r : extra_roots)
            if (!r.empty()) posture.write_paths.push_back(r);
        std::string err;
        profile = claybin_backend::compile_profile(posture, err);
        if (profile.empty()) return std::unexpected(std::move(err));
        proxy_env = claybin_backend::proxy_env(posture);
    } else {
        profile = build_profile(workspace_root().string());
        // 固定的配置文件没有额外根的概念; 附加它们的方式与
        // 它写工作区的方式相同(逃脱,如果不安全则省略).
        for (const auto& r : extra_roots)
            if (std::string e = sbpl_escape(r); !e.empty())
                profile += "(allow file-write* (subpath \"" + e + "\"))\n";
    }
    std::vector<std::string> out{"/usr/bin/sandbox-exec", "-p", std::move(profile)};
    // 客人唯一的出路是代理端口 所以必须 TOLD 地址: 内
    // 置 沙箱 ,所以它适用于准确的这个命令 和它运行的每
    // 一个工具,没有在 agentty 自己的洞穴 内脏
    if (!proxy_env.empty()) {
        out.emplace_back("/usr/bin/env");
        for (auto& kv : proxy_env) out.push_back(std::move(kv));
    }
    out.insert(out.end(), argv.begin(), argv.end());
    return out;
}
#endif

claybin_backend::Posture claybin_posture_for_test() {
#if defined(__linux__) || defined(__APPLE__) || defined(_WIN32)
    return build_claybin_posture();
#else
    return {};
#endif
}

bool command_needs_jit(std::string_view cmd) noexcept {
    static constexpr std::string_view kJit[] = {
        "node", "nodejs", "npm", "npx", "pnpm", "yarn", "corepack", "deno",
        "bun", "bunx", "tsx", "ts-node", "vite", "next", "jest", "vitest",
        "eslint", "tsc", "prettier", "electron", "java", "javac", "gradle",
        "gradlew", "mvn", "mvnw", "kotlin", "kotlinc", "sbt", "scala",
        "clojure", "lein", "dotnet", "luajit", "pypy", "pypy3", "julia",
    };
    // 运行包装符:命令词会追到他们和他们的选项.
    static constexpr std::string_view kWrap[] = {
        "env", "exec", "command", "builtin", "time", "nice", "nohup", "timeout",
        "stdbuf", "ionice", "setsid", "taskset", "xargs", "then", "do", "else",
        "!", "{",
    };
    auto base = [](std::string_view w) {
        if (auto s = w.find_last_of("/\\"); s != std::string_view::npos) w = w.substr(s + 1);
        for (std::string_view ext : {".exe", ".cmd", ".bat"})
            if (w.size() > ext.size() && w.ends_with(ext)) w.remove_suffix(ext.size());
        return w;
    };
    auto is_jit = [&](std::string_view w) {
        w = base(w);
        // 版本名称:节点22、java17、pypy3.10
        while (!w.empty() && ((w.back() >= '0' && w.back() <= '9') || w.back() == '.' || w.back() == '-'))
            w.remove_suffix(1);
        for (auto k : kJit)
            if (w == k) return true;
        return false;
    };

    // 拆分为词(引文已删除)和运算符. 引文为数据,
    // 因此`echo "node"`从未达到命令位置.
    struct Tok { std::string text; bool op; };
    std::vector<Tok> toks;
    std::string cur;
    bool have = false;
    auto flush = [&] { if (have) toks.push_back({cur, false}); cur.clear(); have = false; };
    // sh 以反斜拉片逃生;cmd.exe(Windows)将其当作路径字符.
#if defined(_WIN32)
    constexpr bool kBackslashEscapes = false;
#else
    constexpr bool kBackslashEscapes = true;
#endif
    for (std::size_t i = 0; i < cmd.size(); ++i) {
        const char c = cmd[i];
        if (kBackslashEscapes && c == '\\' && i + 1 < cmd.size()) { cur += cmd[++i]; have = true; }
        else if (c == '\'') {
            have = true;
            while (++i < cmd.size() && cmd[i] != '\'') cur += cmd[i];
        } else if (c == '"') {
            have = true;
            while (++i < cmd.size() && cmd[i] != '"') {
                if (cmd[i] == '\\' && i + 1 < cmd.size()) ++i;
                cur += cmd[i];
            }
        } else if (c == ' ' || c == '\t') flush();
        else if (c == ';' || c == '&' || c == '|' || c == '(' || c == ')' || c == '`'
                 || c == '\n') {
            flush();
            toks.push_back({std::string(1, c), true});
        } else if (c == '$' && i + 1 < cmd.size() && cmd[i + 1] == '(') {
            flush();
            toks.push_back({"$(", true});
            ++i;
        } else { cur += c; have = true; }
    }
    flush();

    bool at_cmd = true, after_wrap = false;
    for (std::size_t k = 0; k < toks.size(); ++k) {
        const auto& t = toks[k];
        if (t.op) { at_cmd = true; after_wrap = false; continue; }
        if (!at_cmd) continue;
        const std::string_view w = t.text;
        // FOO =1厘米(厘米)
        if (auto eq = w.find('='); eq != std::string_view::npos && eq > 0
                && w.substr(0, eq).find('/') == std::string_view::npos)
            continue;
        // 包装选项和参数:-n 10,30s
        if (after_wrap && !w.empty() && (w[0] == '-' || (w[0] >= '0' && w[0] <= '9')))
            continue;
        const auto b = base(w);
        if (std::find(std::begin(kWrap), std::end(kWrap), b) != std::end(kWrap)) {
            after_wrap = true;
            continue;
        }
        // 嘘 -c '...' : 脚本是自己的命令行
        if (b == "sh" || b == "bash" || b == "dash" || b == "zsh" || b == "ash") {
            for (std::size_t m = k + 1; m < toks.size() && !toks[m].op; ++m)
                if (toks[m].text == "-c" || (toks[m].text.size() > 1 && toks[m].text[0] == '-'
                                            && toks[m].text.back() == 'c')) {
                    if (m + 1 < toks.size() && !toks[m + 1].op
                        && command_needs_jit(toks[m + 1].text))
                        return true;
                    break;
                }
        }
        if (is_jit(w)) return true;
        at_cmd = false;
        after_wrap = false;
    }
    return false;
}

} // namespace agentty::tools::util::sandbox
