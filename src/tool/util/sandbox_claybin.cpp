// agentty::tools::util::sandbox - 电话 claybin 后端。
//
// 与brap后端相同的边界, 由 . claybin's 库
// API 而不是执行 二进制程序。 保留在自己的 编译单元 如此,
// brap路径是没有被触及的:brap在用户的机器上有十年的
// 坚硬和明显的工作, claybin 时间已过,
// 两者之间的选择属于用户,地点为 运行时 而不是编辑此文件的任何人。
//
// 原因 两者,具体如下:
//   - 没有 `bwrap` 打开 PATH ,因此没有安装泡泡包的主机
//     仍然得到一个沙箱。
//   - 三面墙壁的包今天什么也给不了,因为我们通过它了没有
//     --seccomp fd 和无 cgroup : 一个 syscall 过滤器, 定位锁作为
//     第二个文件系统壁, 以及真实 cgroup2 memory/pids 盖子。
//   - 一份保证报告,所以 `describe_state()` 可以说 对应的 墙壁都竖起来了
//     而不是""active""这个词.
//
// 内容 不得 DIVERGE :读集. kSystemReadRoots,
// kEtcreadable 和 kHomeTooLSubdirs 居住于此。
// sandbox.cpp 并在这里被使用 参考 - 不 先前的后端手动重述,最后被授予 `/` ,
// 已经交接 ~/.ssh 财务报告和已审计财务报表 ~/.aws 任何经批准的狂欢 调用
// 仍在报告 "sandbox: active" . 1个列表,2个后端.
#include "agentty/tool/util/sandbox_claybin.hpp"
#include "agentty/tool/util/sandbox_broker.hpp"
#include "agentty/util/logx.hpp"   // AGT_LOG——经纪人审计线索.

// POSIX ,没有 Linux-only. claybin 计划 编译器 便携式,
// 从 macOS 财务报告和已审计财务报表 Windows 后端现在有三个平台可以 APPLY
// 政策:
//
//   Linux - 命名空间, 地锁, seccomp, cgroup2  (
//   claybin/plan/spawn.hpp) macOS 安全带+rl限值( claybin/macos/backend.hpp)
//   Windows ——装配,限制信物,工作,( claybin/windows/spawn.cpp
//            (a) 缓解政策
//
// `build_policy()` 下方是它们之间共享的,
// 这是整个要点 claybin 其设计: 一条边界描述,
// 每个平台都编译,并附有一份保证报告,说明每个主机实际能够执行什么。
// 三种产卵路径不同,因为机制不同; POLICY 才不会呢
#if defined(__linux__) || defined(__APPLE__) || defined(_WIN32)

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <sys/wait.h>
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <map>
// 达尔文的 <unistd.h> 并不声明环境(glibc在下方) _GNU_SOURCE).
extern "C" char** environ;
#endif

#include "claybin/plan/compile.hpp"
#include "claybin/policy/policy.hpp"
#include "claybin/policy/profiles.hpp"

#if defined(__linux__)
#include <arpa/inet.h>
#include <netinet/in.h>
#include "claybin/broker/notify.hpp"
#include "claybin/plan/spawn.hpp"
#elif defined(__APPLE__)
#include "claybin/macos/backend.hpp"
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "claybin/windows/backend.hpp"
#endif

namespace agentty::tools::util::sandbox::claybin_backend {

namespace {

using namespace ::clay;
using namespace ::clay::literals;

// 客人代理听的地方,在其OWN网络命名空间内.
//
// A FIXED 端口,由我们选择,而不是内核:宾客的环境(
// http_proxy 和朋友),必须在执行前点出地址,
// 命名空间在初始化前不存在，因此不会发生冲突。
// 一个新的命名空间根本没有其他套接字。
constexpr std::uint16_t kProxyPort = 3128;   // 乌贼港,为熟悉

// 电话 内存 THROTTLE ,以在用户配置无硬盖时应用。
//
// 原因 A DEFAULT IS POSSIBLE 此处 和 不 FOR
// memory.max。硬盖必须正确:太低和合法 `cargo build`
// 谓 OOM-killed ,看起来像 agentty 打破了建筑,
// 没有数字 两者都适合8 GB 膝上型电脑和一台128 GB 工作站
// 工作站 memory.high 不同种类的--通过它让客人承受了回收压力,
// 所以病理案例爬行,而一个短暂的峰值的正常建筑却只是减慢了。
// 没有东西会死亡,所以一个错误的猜测成本是吞吐量而不是正确。
//
// 占总数的75% RAM : 足够高,没有真正的建房通知(a)
// 链接 步数达到12 GB 3号公路上 GB 框下,低到主机保留
// 四分之一的 内存 用于编辑器、浏览器和 agentty 是从机器
// 中而不是硬码中衍生出来的,因为那是原版
// 不存在适合所有人笔记本的统一数值。
// 反对得到正确。
//
// 0=无法读取机器的内存,所以不应用.
[[nodiscard]] std::uint64_t default_memory_high_bytes() noexcept {
#if defined(__linux__)
    const long pages = ::sysconf(_SC_PHYS_PAGES);
    const long page_size = ::sysconf(_SC_PAGESIZE);
    if (pages <= 0 || page_size <= 0) return 0;
    const auto total = static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(page_size);
    // ~2以下 GB 算术不再是好人了,别管它了,别烦它 OOM 杀手
    // 得分( ==== oom_score_adj =500,下设) 做工.
    if (total < (2ull << 30)) return 0;
    return total / 4 * 3;
#else
    return 0;
#endif
}

}  // 命名空间结束。

// 客人继承的 agentty 自己环境,拒绝名单,而不是 允许清单
// : 构建读取任意 vars ( CC, GOPATH, JAVA_HOME,
// npm_config_ *)和1 (中文(简体) ).
// 允许清单 打破下一条没有列出的工具链。 凭据 agentty 本身
// 拥有:
//   - 会话和桌面套接字:dbus(Systemd -run,特勤),
//     wayland/X11 (键盘、截图) ssh/gpg 代理,
//     tmux, 猫咪和 hyprland 控制套接字。
//     大多数也指向了盒子无法看到的路径, 但仅地址会泄露主机布局,
//     抽象套接字根本不需要路径。
//   - 提供商的钥匙agentty用来与模型交谈。
//     经营与支付代理的钥匙无关
bool box_env_keeps(std::string_view key) noexcept {
    static constexpr std::string_view kDrop[] = {
        "DBUS_SESSION_BUS_ADDRESS", "DBUS_SYSTEM_BUS_ADDRESS",
        "WAYLAND_DISPLAY", "WAYLAND_SOCKET", "DISPLAY", "XAUTHORITY",
        "SSH_AUTH_SOCK", "SSH_AGENT_PID", "GPG_AGENT_INFO",
        "TMUX", "TMUX_PANE", "KITTY_LISTEN_ON", "KITTY_PUBLIC_KEY", "KITTY_WINDOW_ID",
        "HYPRLAND_INSTANCE_SIGNATURE", "SWAYSOCK", "I3SOCK", "NIRI_SOCKET",
        "XDG_SESSION_COOKIE", "GNOME_KEYRING_CONTROL", "KDE_SESSION_UID",
        "AT_SPI_BUS_ADDRESS", "PULSE_SERVER", "PIPEWIRE_REMOTE",
        "ANTHROPIC_API_KEY", "ANTHROPIC_AUTH_TOKEN", "CLAUDE_CODE_OAUTH_TOKEN",
        "OPENAI_API_KEY", "AGENTTY_API_KEY",
    };
    for (auto d : kDrop)
        if (key == d) return false;
    // 每一个 _API_KEY agentty 提供方 注册表 原文为( GROQ_API_KEY,
    // GEMINI_API_KEY ,...),后缀匹配,所以覆盖了一个新的提供者.
    if (key.size() > 8 && key.ends_with("_API_KEY")) return false;
    return true;
}

#if defined(__APPLE__)

// 达云代理. 一个 百分比 agentty 进程,而不是每个命令 Linux:
// macOS 客人们,请通过 沙箱-exec 参数中( 无收养,
// 见 run_shell_command ),所以没有 -spawn 生命周期 挂载代理服务器。
// 它听一个 KERNEL-chosen 循环端口, 不同于 Linux 固定的3128号:
// 这里的客人分享主持人的回路, 固定的端口可能已经属于
// 别人了—— 不管是谁拥有它, 都会是客人的网关。
// 故意泄露: 撕掉一个 WorkerGroup
// 静态销毁时 竞态 记录员报告。
//
// KEYED BY ALLOWLIST : 列表是每个代理服务器固定的( 已
// 读) 锁-free 编号 工作线程 ),用户可以在设置中间编辑它 -session。
// 每个不同的列表都得到自己的代理,所以当一个命令生效时,它总是会与它运行
// 的列表相对应。 STARTED - 在实践中,每个过程一两个清单。
//
// 电话 注册表 是一个 maya::snapshot ,而不是一个 互斥量 (货币 禁用清单
// 保持原始状态 锁 调出 agentty : 读 锁-free ,并发布于 update().
// 两个命令在 a 上竞速 新 列表中每个都可以启动代理;
// update() 保持第一个发布,失败者是 闲置
// 的听器 剩下的过程 - 一个套接字,一次,
// 在一个 竞态 需要两个同时产卵之间的列表变化。 锁 每一个产卵。
using ProxyPorts = std::map<std::vector<std::string>, std::uint16_t>;
maya::snapshot<ProxyPorts>& proxy_ports() {
    static maya::snapshot<ProxyPorts> s;
    return s;
}

std::uint16_t macos_proxy_port(const std::vector<std::string>& allow_hosts) {
    {
        const auto cur = proxy_ports().load();
        if (auto it = cur->find(allow_hosts); it != cur->end()) return it->second;
    }
    const std::uint16_t port = [&] () -> std::uint16_t {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return 0;
        ::fcntl(fd, F_SETFD, FD_CLOEXEC);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = 0;
        socklen_t len = sizeof a;
        if (::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 ||
            ::listen(fd, 128) != 0 ||
            ::getsockname(fd, reinterpret_cast<sockaddr*>(&a), &len) != 0) {
            ::close(fd);
            return 0;
        }
        const std::uint16_t p = ntohs(a.sin_port);
        auto* proxy = new netproxy::Proxy{};   // 泄漏,见上文
        if (!proxy->start(fd, allow_hosts)) {
            AGT_LOG(Tool, Warn, "sandbox.proxy.start_failed", "darwin port={}", p);
            return 0;
        }
        AGT_LOG(Tool, Debug, "sandbox.proxy.up", "darwin port={} hosts={}", p,
                allow_hosts.size());
        return p;
    }();
    // 失败不是 缓存: 下一个命令 重试 而不是
    // 会话其余部分的每一个命令 运行时没有网络。
    if (port == 0) return 0;
    const auto published = proxy_ports().update(
        [](const ProxyPorts& old, const std::vector<std::string>& key, std::uint16_t p) {
            ProxyPorts next = old;
            next.emplace(key, p);   // 嵌入保留一个已有条目: 首度发布胜负
            return next;
        },
        allow_hosts, port);
    const auto it = published->find(allow_hosts);
    return it != published->end() ? it->second : port;
}

std::vector<std::string> proxy_env(const Posture& p) {
    std::vector<std::string> out;
    if (p.net_mode != 0 /* 满 */) return out;
    const auto port = macos_proxy_port(p.allow_hosts);
    if (port == 0) return out;
    const std::string addr = "http://127.0.0.1:" + std::to_string(port);
    for (const char* k : {"http_proxy", "https_proxy", "HTTP_PROXY",
                          "HTTPS_PROXY", "ALL_PROXY", "all_proxy"})
        out.push_back(std::string{k} + "=" + addr);
    // 节点的建-in fetch/undici忽略了 + proxy 除非被告知(
    // node  ⁇  24);没有这个,每`fetch`在剧本中就与ENOTFOUND
    // 一起死去,因为嘉宾没有DNS. npm本身读 vars ,不需要什么.
    out.emplace_back("NODE_USE_ENV_PROXY=1");
    // 继承 no_proxy === == 翻译: github.com 将会
    // 发送这些主机 DIRECT ,安全带规则拒绝: 混淆失败,永远不要绕道.
    out.emplace_back("no_proxy=");
    out.emplace_back("NO_PROXY=");
    return out;
}

#endif

namespace {

// 构建映射 build_bwrap_argv() 姿态的密封策略。
//
// 秩序与包装一样重要: 工作区 绑起来 最后 从而,
// 工作区 生活在/tmp下,不会被为其安装的/tmp tmpfs所阴影.
// claybin 上面的图是按申报顺序执行的 原因
// 与泡泡包装相同
[[nodiscard]] Policy<Sealed> build_policy(const Posture& p) {
    auto d = Policy<Draft>{};

    // ── 已读集,来自SHARED列表 ───────────────────────────────
    for (const auto& root : p.system_read_roots) {
        // /usr和/bin必须存在,才能运行一个 shell , 其余的最好
        // -effort 匹配 --ro-bind-try.
        if (root == "/usr" || root == "/bin")
            d = std::move(d).ro_bind(root, root);
        else
            d = std::move(d).bind_try(root, root);
    }
    for (const auto& f : p.etc_readable) d = std::move(d).bind_try(f, f);
    for (const auto& h : p.home_tool_dirs) d = std::move(d).bind_try(h, h);
    // 额外 授权 从 面板. read_paths 改为
    // -only; write_paths 写作是不同的
    // 决定 和读取, 和一个综合列表 使危险之一 容易。
    for (const auto& r : p.read_paths) d = std::move(d).bind_try(r, r);
    // write_paths 绑定在下面的 /tmp tmpfs 之后, 如 工作区:
    // 否则,它将涵盖在/tmp项下。

    // ── 秘密,蒙着面 MOUNTS ──────────────────────────────────────
    //
    // 山顶,不是地锁, 和这种区别让我一个下午。地锁没有负面的
    // 规则: add_rule () 零权利为: ENOMSG (按
    // ABI 10),省略规则意味着路径 INHERITS 其祖先的 授权。
    // 所以,我们想让您知道 "bind $HOME, deny $HOME/.aws"
    // 完全无法在地锁上表达出来 -- 我写了,看着规则到达计划,
    // 读了 凭据 反正我还是会去的
    //
    // 或 执行 工作正在使路径不是文件: 将空的东西绑在它上。
    // tmpfs 用于目录, 空文件用于文件。 客人看到空文件
    // ~/.aws 而不是一个被拒绝的, 这也是更好的失败模式
    // -- -- 一个读取它的工具得到不 凭据 而不是一个
    // EACCES 可能报告为虫子。
    //
    // bind_try 语义学:用户没有的秘密不是错误,
    // 而且 mask() 默认情况下是可选的,原因正是这个。
    for (const auto& m : p.masked) d = std::move(d).mask(m);

    // ── 伪-filesystems ───────────────────────────────────────────
    // 新的文件只显示我们自己的名片空间 dev_fs 绑定程序需要的一小撮
    // 设备节点,而不是安装dvtmpfs,所以 / dev/mem
    // 朋友们 ABSENT 而不是现 -and-denied.
    d = std::move(d).proc_fs("/proc");
    d = std::move(d).dev_fs("/dev");

    // ── /tmp,然后是工作区 ───────────────────────────────────────
    // ⁇   ⁇  τ ⁇   ⁇ SIZED: claybin 执行
    // 它作为挂载选项, 所以运行的建设点击 ENOSPC 内侧 沙箱
    // 而不是填补主机的 内存 . brap 无法表达这一点。
    d = std::move(d).tmpfs("/tmp", Bytes{p.tmp_bytes});
    for (const auto& w : p.write_paths) {
        d = std::move(d).bind_try(w, w, false);
        // 与命令规则相同 工作区 :在新装订下必须
        // 再次使用口罩,否则装订后会发现这些口罩。
        for (const auto& m : p.masked)
            if (m.starts_with(w)) d = std::move(d).mask(m);
    }
    if (!p.workspace.empty()) {
        d = std::move(d).bind(p.workspace, p.workspace);
        d = std::move(d).workdir(p.cwd.empty() ? p.workspace : p.cwd);

        // 调 -apply 任何活的面具,都一样 内部 编号 工作区
        // 因为上面的束缚刚刚重挂在它上面
        //
        // 我运出后测到的订货错误: 口罩在~94行使用,
        // 工作区 将土地绑在这里, 并绑定 工作区 根覆盖它下面的
        // 每一座山。 `<workspace>/.env`
        // 被蒙上面具,然后没有 -masked 之后一行,
        // 和一个植入的.env回来可读 IN 完整 都放在后端下方
        // 每张桌子 -level 检查一直通过 - 面具真的在保险单里,
        // 它只是失去了一个 竞态 与一个较晚的山峰。
        //
        // 秩序是唯一的固定:每个可以覆盖它的约束后都必须有一个面具。
        // $HOME的面具没有受到影响(没有重新封装$HOME),
        // 这就是为什么~/.ssh是安全的,而.env不是。
        for (const auto& m : p.masked) {
            if (m.starts_with(p.workspace)) d = std::move(d).mask(m);
        }

        // 交接会绑定 完全一样的顺序:读 -only 约束
        // <workspace>/.vscode/tasks.json
        // 包含在 工作区 上面绑着,所以它必须追上, 和面具一样的虫子形状,
        // 相同的固定。
        //
        // bind_try 以 ro=真数 : TRY 因为一条路径 仓库
        // 不存在任何错误(大多数错误), 仓库 不存在.vscode),
        // 以及. RO 因为客人必须静静 READ 这些 - git
        // 读取它自己在每次引用时的配置。 EROFS . 这是一个真实的错误,
        // 而不是混乱; EACCES a 权限 拒绝会给。
        //
        // 这是预防;检测(handoff::snapshot_trusted)
        // 不管如何运行,因为这堵墙需要一个挂载的命名空间,
        // 并且只是没有在拒绝非特权用户的主机上。
        for (const auto& ro : p.handoff_ro) {
            if (ro.starts_with(p.workspace))
                d = std::move(d).bind_try(ro, ro, /*ro=*/true);
        }

        // 面具会一直粘着, 紧紧地粘着 build_claybin_posture
        // 已经保留了隐藏路径 handoff_ro 令
        // 本身也安全了 所以以后再也无法再被绑起来
        if (!p.handoff_ro.empty())
            for (const auto& m : p.masked)
                if (m.starts_with(p.workspace)) d = std::move(d).mask(m);
    }

    // ── 网络 ─────────────────────────────────────────────────────────
    //
    // 三种模式:
    //
    //   客人可以全程上网,但可以通过它的OWN网络
    //         命名空间与由本进程提供的回环代理。此前
    //         表示( 表示) "share agentty's namespace" ,
    //         这根本不是一个边界: 沙箱 审计的一个高发现是,
    //         代理运行的命令可以到达主机自身的回路服务以及整个 LAN 现在内
    //         核给盒子没有 自己的回路,和 sandbox_netproxy
    //         决定每个连接可拨打什么( 没有主机回转, 没有) RFC1918 ,
    //         没有 链接-local 元数据,无 CGNAT/tailnet).
    //         `strong`.
    //   无无无无 授权 完全没有,所以 claybin 令净土空. `strong`.
    //   港号每端口1授权,由Landlock(ABI 4+)强制.
    //         `strong`,以及代理-based允许清单不能声称的东西:
    //         代理不能被说服绕过内核规则. 共享主机命名空间,
    //         因为一个端口规则是关于到达真实主机.
    switch (p.net_mode) {
        case 1: break;  // 无
        case 2:
            for (auto port : p.allow_ports) d = std::move(d).connect("", port);
            break;
        default:
#if defined(__APPLE__)
            // 达文上没有私人网:客人被安全带绑住 一个 循环端口,
            // 哪个 agentty 自己的代理服务器服务。 如果代理无法启动,
            // 则失败 CLOSED (无网络) -- -- 编译赤线
            // 连接( N) "" , 0)将是(允许网络*),
            // i.e。该代理服务器将关闭的确切孔。
            if (const auto port = macos_proxy_port(p.allow_hosts); port != 0)
                d = std::move(d).connect("", 0).proxied_netns(port);
#else
            d = std::move(d).connect("", 0).proxied_netns(kProxyPort);
#endif
            break;  // 满
    }

    // ── 进程硬化 ──────────────────────────────────────────────
    // new_session :孩子不能偷控制 tty(brap's
    //   --new-session ,以及为什么 TIOCSTI 注射是无法到达的。
    // die_with_parent : 如果 agentty 死吧 死吧 我... 死吧 你个混蛋
    d = std::move(d).new_session();
    d = std::move(d).die_with_parent();

    // ── 墙壁没有被给任何 ────────────────────────────
    //
    // 系统调用过滤器,加上任何中介的调用.
    //
    // 一体建设 SyscallPolicy 应用 一次 ,
    // 因为草案没有暴露其要求修正的循环政策 --
    // `syscall_profile()` 因此,通知规则必须在
    // 这里分层,在保险单交接之前,而不是在之后添加。
    SyscallPolicy sp = [&] {
        switch (p.syscall_mode) {
            // 关 everything(), 不 "no profile"
            // : 默认 -constructed SyscallPolicy
            // 杀死 -by-default 没有规则,什么 编译 改为:
            // 沙箱 杀死了执行官的客人 claybin 和各种设置,
            // 面板 现场预览片第一次出现
            case 0: return SyscallPolicy::everything();
            // 严格是 compiler() 等没有网络时,
            // 贝壳仍然可以执行。 with_filesystem ()
            // 没有执行并杀死每个命令。 网络需要套接字。 调用.
            case 2: return p.net_mode == 1 ? profiles::compiler()
                                           : profiles::compiler_with_network();
            default: return profiles::compiler_with_network();
        }
    }();

    // 配置文件总是否认 RWX 绘图。在过滤器关闭后,当 W 时添加一条规则。
    // ^X 开始吧
    //
    // 继续 STRICT 才要真实一点: PR_SET_MDWE
    // 拒绝 mmap( W) - > mreagr (X) 转换,
    // 参数规则无法看到( 仅用参数规则衡量: 该序列成功)。
    // 它被封在 严格 上, 因为每次都断开。 JIT --
    // node/V8 将不从它开始,平衡必须运行一个正常的工具链。
    // 硬化和空降已经接受一个异常的系统调制失败的建筑,
    // 所以这是他们选择的相同交易。
    if (p.wx_protect && p.syscall_mode == 2 /* 严格 */)
        d = std::move(d).deny_write_execute();
#if defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__))
#if defined(__x86_64__)
    constexpr clay::SysNr kMmap = 9, kMprotect = 10;
#else
    constexpr clay::SysNr kMmap = 222, kMprotect = 226;
#endif
    constexpr std::uint64_t kProtWriteExec = 0x2 | 0x4;
    if (p.syscall_mode == 0 && p.wx_protect) {
        sp.deny_arg_all(kMmap, 2, kProtWriteExec, clay::SysAction::errno_, 1);
        sp.deny_arg_all(kMprotect, 2, kProtWriteExec, clay::SysAction::errno_, 1);
    }
    // W^X 关闭: 解除侧写规则,或者 node/V8 无法开始。
    if (p.syscall_mode != 0 && !p.wx_protect) {
        sp.drop_arg_rules(kMmap, 2);
        sp.drop_arg_rules(kMprotect, 2);
    }
#endif

    // 经纪人的电话:决定 运行时 由主管而不是过滤器进行。
    // 之后 因为我们的档案为这些设定了
    // 统一的行动 经纪必须赢
    //
    // 只有在电话人给主管打电话时 才能有经纪人听
    // HANGS 宾客 - 内核阻断 线程 直到
    // 有人回答 - 所以这个是门上 `p.broker`
    // 而不是无条件的 倒着把硬化的特征变成 死锁
    // 每一个小孔。
    //
    // 也跳过 syscall_mode 关闭 : everything()
    // 谓 "no filter" ,并在其中增加通知规则,将意味着在
    // 沙箱 用户要求完全不过滤。
    //
    // Linux-only : 中介活动建立在 seccomp 用户
    // -notify ,哪个 macOS 编译,而不是没有 -op
    // 已经 运行时 ,所以未来的读者 不必想出 `p.broker`
    // 达尔文有不同的意思 完全没有意义 保证报告说
    // `syscall.filter: none` 来匹配。
#if defined(__linux__)
    if (p.broker && p.syscall_mode != 0) {
        for (auto nr : broker::brokered_syscalls()) sp.notify(nr);
    }
#endif

    // socket() :只有地址家庭使用真正的工具。
    // AF_NETLINK 读出宿主的路线, 链接 和地址(主
    // 网在全天下共享), AF_VSOCK 达到超视星等-
    // -无论是建设还是测试都无用。 -bit 内。
#if defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__))
    if (p.syscall_mode != 0) {
        static constexpr std::uint32_t kFamilies[] = {
            1,   // AF_UNIX
            2,   // AF_INET
            10,  // AF_INET6
        };
#if defined(__x86_64__)
        constexpr clay::SysNr kSocket = 41;
#else
        constexpr clay::SysNr kSocket = 198;
#endif
        sp.allow_arg32_only(kSocket, 0 /* 家庭 */,
                            std::span<const std::uint32_t>{kFamilies},
                            clay::SysAction::errno_, 97 /* EAFNOSUPPORT */);
    }
#endif
    d = std::move(d).syscall_profile(std::move(sp));

    // 资源上限。 当东道代表担任后卫时,为cgroup2;
    // 报告区分了两者,而不是两者。
    if (p.memory_bytes) d = std::move(d).memory(Bytes{p.memory_bytes});
    else if (const std::uint64_t high = default_memory_high_bytes(); high)
        // 没有配置硬盖。 节流阀仍然可以安全默认, 因为它不能杀死一个
        // 建筑 — 见 default_memory_high_bytes。
        d = std::move(d).memory_high(Bytes{high});
    if (p.max_processes) d = std::move(d).processes(p.max_processes);
    if (p.cpu_percent) d = std::move(d).cpu_percent(p.cpu_percent);
    else
        // 未配置配额。 A WEIGHT 仍然可以安全默认: 它
        // 不会改变任何在闲置的机器上的任何内容( 客人得到每个核心),
        // 在争议下, 客人在默认 100 时会失去任何东西 -
        // - -- 您的编辑器, 您的浏览器, agentty
        // 即是十二个 -way 建造无法再让桌面无法使用,
        // 而不能拖慢唯一运行的建造速度。 配额不能使交易完成。
        d = std::move(d).cpu_weight(25);
    // 描述符:与进程分开的杠杆,因为漏出fds的
    // 运行会使主机的文件表耗尽,而永不放弃.
    if (p.max_open_files) d = std::move(d).open_files(p.max_open_files);
    // 两个不同的天花板, 故意两者都有。 cpu_time
    // 完全计算,所以一个永远沉睡的过程是没有被改变的;
    // wall_clock 时间已经过去,所以永远无法阻挡。
    // 无法包含另一个, 工具层本身也无法覆盖 超时 被一个
    // 孩子所忽略 被困在了 SIGTERM ——这些不能).
    if (p.cpu_secs)
        d = std::move(d).cpu_time(Nanos{std::uint64_t{p.cpu_secs} * 1'000'000'000ull});
    if (p.wall_clock_secs)
        d = std::move(d).wall_clock(Nanos{std::uint64_t{p.wall_clock_secs} * 1'000'000'000ull});

    // 主机名。 不是封存 - 客人不能升级。 但真实主机名会泄露到构
    // 建输出中, 测试 快照 和任何能发射到 `hostname` ,
    // 这使得这些不 -reproducible 并且有轻微的指纹
    // claybin 已默认其 PolicyData 改为
    // "sandbox" ;这是当一个建筑真正依赖它时,
    // 让用户保留真实的一行. claybin 默认为 "sandbox" ,
    // 所以必须清除;空名跳过设置主机名和新名 UTS
    // 命名空间仍保留宿主自身的名称。
    d = std::move(d).hostname(p.fake_hostname ? "sandbox" : "");

    // 当 OOM 凶手必须选,它应该选沙箱建筑,
    // 而不是用户的编辑器或 agentty 平衡无 内存 盖,
    // 所以这是站在一个离家出走的建筑和桌面之间的唯一东西。
    d = std::move(d).oom_score_adj(500);

    // claybin关闭继承的fd,除非被告知不关闭.
    if (!p.close_inherited_fds) d = std::move(d).unsafe_inherit_fds();

    return std::move(d).seal();
}

#if defined(__linux__)
// 执行官的检查,在产卵前完成的: claybin 客人来了 调用 执行。
// 带有斜线的名字已经是一个路径; 任何找不到的东西都会保留给定,
// 所以客人的执行报告真实错误。 Linux-only : 数据 macOS
// 后端产卵穿过 posix_spawnp ,哪个搜索 PATH 自己来
std::string resolve_on_path(const std::string& name) {
    if (name.empty() || name.find('/') != std::string::npos) return name;
    const char* path = std::getenv("PATH");
    std::string_view rest = (path && *path) ? path : "/usr/local/bin:/usr/bin:/bin";
    while (true) {
        const auto colon = rest.find(':');
        std::string dir{rest.substr(0, colon)};
        if (dir.empty()) dir = ".";
        std::string full = dir + "/" + name;
        if (::access(full.c_str(), X_OK) == 0) return full;
        if (colon == std::string_view::npos) break;
        rest.remove_prefix(colon + 1);
    }
    return name;
}
#endif

}  // 命名空间结束。

// 主持人的 沙箱 检测一次(叉子),然后再利用:它们不会改变,
// 而 agentty 运行,和 沙箱 面板 问每个钥匙。
#if defined(__APPLE__)
const ::clay::macos::HostCapabilities& host() {
    static const ::clay::macos::HostCapabilities h = ::clay::macos::probe_host();
    return h;
}
std::uint32_t landlock_abi() { return 0; }
#else
const ::clay::HostCapabilities& host() {
    static const ::clay::HostCapabilities h = ::clay::probe_host();
    return h;
}
std::uint32_t landlock_abi() { return host().landlock_abi; }
#endif

#if defined(__APPLE__)

// ─────────────────────────── macOS ────────────────────────────────────
//
// 比那个短 Linux 路径,因为安全带是一个机制而不是四个,并且因为没有经纪
// 人: macOS 没有 seccomp 用户 -notify ,所以没有什么
// 可以监督 `SpawnResult::service_broker` 保持空闲。

bool available() {
    // probe_host ()叉子和实际上 调用
    // sandbox_init () 以丢弃的剖面图,而不是测试 符号。
    // 与 Linux 探测器和同样的原因:读作现在但使用时
    // 失败的能力比缺勤更糟糕,因为Auto随后会默默的降解而不是
    // 大声的降解.
    //
    // 内 -process 探测在此不是选项。 安全带配置文件是
    // IRREVERSIBLE - 输入后,一个过程不能离开它 - 所以
    // 一个应用它的探测器会限制 agentty 自己来
    return host().seatbelt;
}

Report describe(const Posture& p) {
    Report r;
    auto sealed = build_policy(p);
    auto compiled = ::clay::macos::compile(sealed, host());
    if (!compiled) {
        r.ok = false;
        r.detail = std::string{compiled.error().mechanism};
        return r;
    }
    r.ok = true;
    for (int i = 0; i < static_cast<int>(CapId::count_); ++i) {
        auto id = static_cast<CapId>(i);
        auto s = compiled->guarantees.strength(id);
        Wall w;
        w.name = ::clay::cap_name(id);
        w.strength = s == Enforcement::strong    ? "strong"
                     : s == Enforcement::partial ? "partial"
                     : s == Enforcement::advisory ? "advisory"
                                                  : "none";
        // 收据,与 Linux: "filesystem.read strong via seatbelt"
        // 以某种方式可检查 "sandbox: active"
        // 机制字符串因是事实而不同。
        w.mechanism = compiled->guarantees.mechanism(id);
        r.walls.push_back(std::move(w));
    }
    return r;
}

SpawnResult spawn_shell(const Posture& p, const std::string& shell_cmd, int stdout_fd,
                        int stderr_fd) {
    SpawnResult out;

    auto sealed = build_policy(p);
    ::clay::macos::Options opts;
    opts.profile_name = "agentty";
    // 产卵时的活探测器:这个指令得到的,现在必须与内核匹配.
    auto compiled = ::clay::macos::compile(sealed, ::clay::macos::probe_host(), opts);
    if (!compiled) {
        out.start_error =
            "claybin: compile failed: " + std::string{compiled.error().mechanism};
        return out;
    }

    // /bin/sh -c ,匹配包和 Linux-claybin 路径:管道、
    // 方向和光圈是批准击打的一部分 调用 说吧
    const char* argv[] = {"/bin/sh", "-c", shell_cmd.c_str(), nullptr};
    // 与linux和窗口(box_env_keeps)相同的过滤环境.
    std::vector<std::string> env_store;
    for (char** e = ::environ; e && *e; ++e) {
        const std::string_view kv{*e};
        if (box_env_keeps(kv.substr(0, kv.find('=')))) env_store.emplace_back(kv);
    }
    std::vector<const char*> envp;
    envp.reserve(env_store.size() + 1);
    for (const auto& s : env_store) envp.push_back(s.c_str());
    envp.push_back(nullptr);
    ::clay::macos::SpawnRequest req;
    req.program = "/bin/sh";
    req.argv = argv;
    req.envp = envp.data();
    req.stdout_fd = stdout_fd;
    req.stderr_fd = stderr_fd;
    if (!p.cwd.empty())
        req.workdir = p.cwd;
    else if (!p.workspace.empty())
        req.workdir = p.workspace;

    auto spawned = ::clay::macos::spawn(*compiled, req);
    if (!spawned) {
        out.start_error = "claybin: spawn failed: " +
                          std::string{spawned.error().mechanism} + " (errno " +
                          std::to_string(spawned.error().sys_errno) + ")";
        return out;
    }

    out.started = true;
    out.pid = spawned->pid;
    // 达文没有被通缉 也没有经纪人可以提供服务 macOS
    // 没有 seccomp 用户 -notify 而不是假冒
    // 所以跑者是 `supervisor_fd >= 0`
    // 测试是正确之举 不需要知道它在哪一个平台上
    return out;
}

std::string compile_profile(const Posture& p, std::string& error) {
    auto sealed = build_policy(p);
    ::clay::macos::Options opts;
    opts.profile_name = "agentty";
    auto compiled = ::clay::macos::compile(sealed, host(), opts);
    if (!compiled) {
        error = "claybin: compile failed: " + std::string{compiled.error().mechanism};
        return {};
    }
    if (!compiled->use_seatbelt || compiled->profile.empty()) {
        error = "claybin: compiled policy has no seatbelt profile";
        return {};
    }
    return std::move(compiled->profile);
}

#elif defined(_WIN32)

// ─────────────────────────── Windows ──────────────────────────────
//
// 安装器+限制符号+工作对象+缓解政策,通过 claybin
// 窗口后端。 编译器 一样 build_policy
// ()另外两个平台运行;只有应用步骤不同,
// 保证报告说每个主机实际竖起的墙壁是哪个.
//
// 打电话的人回来的就是 HANDLES ,而不是 fds: 进程+任务,两者都已打开,
// 均由呼叫者拥有. jaal's windows_process::adopt 消耗;
// 任务为 kill 开关( 切换) KILL_ON_JOB_CLOSE ),
// 该流程处理等待退出。

bool available() {
    // 探测器是真实的, 和其他每个后端一样: appcontainer
    // profile 创建和删除, 限制的符符符是PROVEN
    // 通过运行一个孩子在一个下, 通过初始化列表来减轻属性。
    // 一个读现但使用失败的能力比缺席更糟糕, 因为Auto
    // 之后会默默地降解而不是大声的降级。
    const auto host = ::clay::windows::probe_host();
    // 地板是一面真正的墙:安装者标志或工作对象.
    return host.app_container || host.job_objects;
}

Report describe(const Posture& p) {
    Report r;
    auto sealed = build_policy(p);
    auto compiled = ::clay::windows::compile(sealed, ::clay::windows::probe_host());
    if (!compiled) {
        r.ok = false;
        r.detail = std::string{compiled.error().mechanism};
        return r;
    }
    r.ok = true;
    for (int i = 0; i < static_cast<int>(CapId::count_); ++i) {
        auto id = static_cast<CapId>(i);
        auto s = compiled->guarantees.strength(id);
        Wall w;
        w.name = ::clay::cap_name(id);
        w.strength = s == Enforcement::strong    ? "strong"
                     : s == Enforcement::partial ? "partial"
                                                 : "none";
        // 收条,每个平台的真相: "filesystem.read partial via appcontainer+DACL"
        // 可检查; "sandbox active" 才没有呢
        w.mechanism = compiled->guarantees.mechanism(id);
        r.walls.push_back(std::move(w));
    }
    return r;
}

SpawnResult spawn_argv(const Posture& p, const std::vector<std::string>& argv_in,
                       int stdout_fd, int stderr_fd,
                       const std::vector<std::pair<std::string, std::string>>& env) {
    SpawnResult out;

    if (argv_in.empty()) {
        out.start_error = "claybin: empty argv";
        return out;
    }

    auto sealed = build_policy(p);
    ::clay::windows::Options opts;
    opts.profile_name = "agentty";
    // ACL 印记为主机 -side 变异,所以它保持选择 -in
    // 现场 API 职等 -- -- 和 agentty 选择
    // 加入 此处 一次,故意:替代的是一个无法到达任何用户要求的
    // 命令的辅助设备,这使得 沙箱 没用的,而不是安全的。
    // 编译 报告仍诚实地对墙进行评级(部分,按 -object 语义学.
    opts.allow_host_acl_mutation = true;

    auto compiled = ::clay::windows::compile(sealed, ::clay::windows::probe_host(), opts);
    if (!compiled) {
        out.start_error = "claybin: compile failed: " +
                          std::string{compiled.error().mechanism};
        return out;
    }

    // 呼叫者的 argv 完全按照给定的 —— argV[ 0] 程序
    // NAME ,所有三个后端共享的 POSix 常规,
    // 以及 claybin 窗口输出器重建命令行。 无法重排 shell。
    // -parsing 输入的用户数据。
    std::vector<const char*> argv;
    argv.reserve(argv_in.size() + 1);
    for (const auto& a : argv_in) argv.push_back(a.c_str());
    argv.push_back(nullptr);

    // PATH分辨率:CreateProcessW像execvp
    // 一样搜索加载器路径,但只有在没有斜线存在时;保持给定的调用程序.
    const std::string program = argv_in.front();

    // 请求的内涵, 层层在 FILTERED 我们的复制件( box_env_keeps).
    // 窗口加载器需要一个排序的双-NUL块;claybin从下面的字符*列表中(
    // case-insensitive排序,"NAME=VALUE\0"对)中建起来.
    std::vector<std::string> env_store;
    std::vector<const char*> envp;
    {
        // 抢先抢先抢,所以打来的 抢先夺地,再赢
        if (wchar_t* wbase = ::GetEnvironmentStringsW()) {
            for (const wchar_t* e = wbase; *e;) {
                const std::wstring wide{e};
                e += wide.size() + 1;
                // 只跳过 "NAME=VALUE" 形状, 跳过奇异的特殊条目
                const auto eq = wide.find(L'=');
                if (eq == std::wstring::npos || eq == 0) continue;
                const std::string name(wide.begin(), wide.begin() + eq);
                {
                    std::string upper = name;  // 窗口名称为大小写-blind
                    for (char& c : upper)
                        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
                    if (!box_env_keeps(upper)) continue;
                }
                // 由下面的呼叫者提供被覆盖的名字; 保留名称侧面 ASCII
                // -faithful (env var 名称为 ASCII ,
                // 并用“实际”转换无损失的值。 UTF-8
                bool overridden = false;
                for (const auto& [k, v] : env)
                    if (_stricmp(k.c_str(), name.c_str()) == 0) { overridden = true; break; }
                if (overridden) continue;
                const int n = ::WideCharToMultiByte(
                    CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
                if (n > 0) {
                    std::string line(n - 1, '\0');
                    ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1,
                                          line.data(), n, nullptr, nullptr);
                    env_store.push_back(std::move(line));
                }
            }
            ::FreeEnvironmentStringsW(wbase);
        }
        for (const auto& [k, v] : env) env_store.push_back(k + "=" + v);
        for (const auto& line : env_store) envp.push_back(line.c_str());
        envp.push_back(nullptr);
    }

    ::clay::windows::SpawnRequest req;
    req.program = program.c_str();
    req.argv = argv.data();
    req.envp = envp.data();
    // 呼叫者的管子从这里传来 FdMap (他们是) HANDLES。
    // 下面的监视器字段覆盖 spawn_shell 路径,
    // 没有提供地图:继承 stdout/stderr,
    // NUL 上楼梯。
    req.stdin_fd = ::clay::windows::SpawnRequest::kDevNull;
    if (stdout_fd >= 0 || stderr_fd >= 0) {
        int n = 0;
        if (stdout_fd >= 0)
            req.fds[n++] = {reinterpret_cast<void*>(static_cast<intptr_t>(stdout_fd)), 1};
        if (stderr_fd >= 0)
            req.fds[n++] = {reinterpret_cast<void*>(static_cast<intptr_t>(stderr_fd)), 2};
        req.fd_count = n;
        req.stdout_fd = -1;  // 明确标注在以上
        req.stderr_fd = -1;
    } else {
        req.stdout_fd = -1;
        req.stderr_fd = -1;
    }
    if (!p.cwd.empty())
        req.workdir = p.cwd;
    else if (!p.workspace.empty())
        req.workdir = p.workspace;

    auto spawned = ::clay::windows::spawn(*compiled, req);
    if (!spawned) {
        out.start_error = "claybin: spawn failed: " +
                          std::string{spawned.error().mechanism} + " (errno " +
                          std::to_string(spawned.error().sys_errno) + ")";
        return out;
    }

    out.started = true;
    out.pid = spawned->pid;
    // HANDLES ,而不是描述符。 SpawnResult 将
    // 它们当作英寸(其形状与多管径共享); jaal 其通过重新解释它们。
    // 任务就是杀死开关, 过程处理等待退出 —— 两者都归
    // 这里的呼叫者所有。
    out.pidfd = static_cast<int>(reinterpret_cast<intptr_t>(spawned->process));
    // 关闭拥有工作手柄, 丢弃它会关闭工作,
    // KILL_ON_JOB_CLOSE 将留下的命令全部删除。
    std::shared_ptr<void> job{spawned->job, [](void* h) {
        if (h) ::CloseHandle(static_cast<HANDLE>(h));
    }};
    out.kill_tree = [job = std::move(job)]() -> bool {
        if (!job) return false;
        return ::TerminateJobObject(static_cast<HANDLE>(job.get()), 1) != FALSE;
    };
    // 窗口上没有经纪人:seccomp用户-notify没有等效,
    // 因此supervisor_fd和service_broker保持其哨兵默认值.
    return out;
}

SpawnResult spawn_shell(const Posture& p, const std::string& shell_cmd, int stdout_fd,
                        int stderr_fd) {
    return spawn_argv(p, {"cmd.exe", "/d", "/s", "/c", shell_cmd}, stdout_fd, stderr_fd);
}

#else  // __linux__

bool available() {
    // 包探头同样提出的问题,也是出于同样的原因:
    // 读作现在但使用时失败的能力比缺勤更糟糕,
    // 因为Auto随后会默默地而不是大声地降解.
    //
    // claybin的probe_host()叉并尝试了uid_map
    // 写而不是读作sysctls——所以这是spawn()的忠实预测器,
    // 而不是对内核特征列表的猜测.
    const auto& host = claybin_backend::host();

    // 不 门在用户命名空间上,这就是全部原因 claybin
    // 值得作为第二个后端。
    //
    // 这曾经需要 `user_namespaces && mount_namespaces` ,
    // 它使 claybin 完全没有用,对最需要它的主机来说。 Ubuntu 24.04 舰只
    // a AppArmor 拒绝此选项的配置文件 uid_map 写入未关闭 二进制程序 ,
    // 所以 brap 失败 "setting up uid map: permission denied"
    // 财务报告和已审计财务报表 agentty 报告根本没有后端(问题来自 KhazAkar
    // 复制 brap 的前提条件意味着复制 braw 的失败。
    //
    // 但是,地锁和 seccomp 已经 UNPRIVILEGED 机制。
    // 不需要命名空间,而且 AppArmor 用户限制不会触碰它们。 因此,
    // 有关主机 claybin 仍然交付,通过 compile():
    //
    //   filesystem.read/write/exec
    //   坚固的地锁 syscall.filter 强 seccomp
    //   -bpf resource.memory 强 cgroup2
    //   memory.max privilege.drop 强 no_new_privs +
    //   空边框设置 network.isolation 无( 需要网
    //   关) process.isolation 无( 需要用户 + pidns)
    //
    // 这是真的 沙箱 brap 中没有: 文件系统边界 --
    // -- 保存已核准命令的边界 ~/.ssh - 幸存下来
    // 失去网络和零散隔离是一种真正的降级,
    // 面板的墙壁报告说,每个能力都是如此,这是运送部分边界
    // 的诚实方式.
    //
    // 地板是地锁 OR seccomp。没有任何东西可以强制执行
    // 和要求 沙箱 整个子系统都是用来避免的, 所以我们拒绝,
    // 让呼叫者回到brap(或者在下方出现响亮的故障) --sandbox
    // ) (中文(简体) ).
    return host.landlock_abi > 0 || host.seccomp;
}

Report describe(const Posture& p) {
    Report r;
    auto sealed = build_policy(p);
    auto compiled = compile(sealed, host());
    if (!compiled) {
        r.ok = false;
        r.detail = std::string{compiled.error().mechanism};
        return r;
    }
    r.ok = true;
    for (int i = 0; i < static_cast<int>(CapId::count_); ++i) {
        auto id = static_cast<CapId>(i);
        auto s = compiled->guarantees.strength(id);
        Wall w;
        w.name = ::clay::cap_name(id);
        w.strength = s == Enforcement::strong    ? "strong"
                     : s == Enforcement::partial ? "partial"
                                                 : "none";
        // 收据 收条 claybin 已经知道这一点; 复制它不是区别于 "the sandbox is active"
        // 财务报告和已审计财务报表 "filesystem.read is strong via landlock abi 10" ,
        // 只有第二个可以检查。
        w.mechanism = compiled->guarantees.mechanism(id);
        r.walls.push_back(std::move(w));
    }
    return r;
}

SpawnResult spawn_argv(const Posture& p, const std::vector<std::string>& argv_in,
                       int stdout_fd, int stderr_fd,
                       const std::vector<std::pair<std::string, std::string>>& env) {
    SpawnResult out;

    if (argv_in.empty()) {
        out.start_error = "claybin: empty argv";
        return out;
    }

    auto sealed = build_policy(p);
    // 产卵时的活探测器:这个指令得到的,现在必须与内核匹配.
    auto compiled = compile(sealed, probe_host());
    if (!compiled) {
        out.start_error = "claybin: compile failed: " + std::string{compiled.error().mechanism};
        return out;
    }

    // "No network" 是用户选择的保证(即: 面板 毫无道理,
    // 紧紧的姿势, `run --no-network`). claybin
    // 保留它时使用空网子,拒绝非特权用户命名空间的主机则获得无: compile()
    // 仍然成功,它只是,它只是 DEGRADES network.isolation ,
    // 而嘉宾会和主机的网络运行。拒绝--失败关闭,并说原因。
    if (p.net_mode == 1 /* 无 */
        && compiled->guarantees.strength(CapId::net_isolation) != Enforcement::strong) {
        out.start_error =
            "sandbox: network is set to None but this host can't isolate it "
            "(no network namespace -- unprivileged user namespaces are likely "
            "disabled); refusing to run with the network open";
        return out;
    }

    // 呼叫者的方格V完全按照给定的到达孩子。
    // `sh -c` 这里:带有引文的 承诺信息或 `$`
    // 绝对不能重回 -parsed 也就是因为这个
    // argv形式存在于壳体旁边
    std::vector<const char*> argv;
    argv.reserve(argv_in.size() + 1);
    for (const auto& a : argv_in) argv.push_back(a.c_str());
    argv.push_back(nullptr);
    // claybin 调用执行器,它没有PATH的检索,所以一个赤裸的
    // "git"回来作为出口 127. 在这里解决它,在母的,执行的方式。
    const std::string program = resolve_on_path(argv_in.front());
    // 请求的内涵, 层层在 FILTERED 我们的复制品,作为一个准备的封面:
    // 无法建造一个( 它可能无法分配)。 总是在这里建造 -- -
    // - 一个空洞会把客人交给 agentty 整个环境
    std::vector<std::string> env_store;
    std::vector<const char*> envp;
    {
        for (char** e = ::environ; e && *e; ++e) {
            const std::string_view kv{*e};
            const auto key = kv.substr(0, kv.find('='));
            if (!box_env_keeps(key)) continue;
            bool overridden = false;
            for (const auto& [k, v] : env) if (k == key) { overridden = true; break; }
            if (!overridden) env_store.emplace_back(kv);
        }
        for (const auto& [k, v] : env) env_store.push_back(k + "=" + v);
        // 宾客的路线从自己的回路中脱落,每拼写都是因为工具链不一致而设定
        // 的:卷曲读为http_proxy/https_proxy和ALL_PROXY,
        // git读为http_proxy,
        // npm/pip/cargo想要大写表,去读为HTTP(S)_PROXY.
        // 在所有地址下都有一个,所以没有工具通过不询问
        // 来绕过代理.
        //
        // 没有 NO_PROXY 本地主机: 内置
        // 本地主机 IS 并且那里可以免责 而不是
        // 打开一个洞 因为其他的都听不到这个名字空间
        if (p.net_mode != 1 /* 无 */) {
            const std::string addr = "http://127.0.0.1:" + std::to_string(kProxyPort);
            for (const char* k : {"http_proxy", "https_proxy", "HTTP_PROXY",
                                  "HTTPS_PROXY", "ALL_PROXY", "all_proxy"}) {
                bool have = false;
                for (const auto& [ek, ev] : env) if (ek == k) { have = true; break; }
                if (!have) env_store.push_back(std::string{k} + "=" + addr);
            }
        }
        envp.reserve(env_store.size() + 1);
        for (const auto& s : env_store) envp.push_back(s.c_str());
        envp.push_back(nullptr);
    }
    Command cmd{program.c_str(), argv.data(), envp.data()};
    // 标定从 / dev/null 明确而非继承的 -
    // 能读取用户的名客 终端 能够 提示词 工具不能运行。
    cmd.stdin_fd = Command::kDevNull;
    cmd.stdout_fd = stdout_fd;
    cmd.stderr_fd = stderr_fd;

    // 只要客人跑步,就保持编译的姿势 OWNS 组合:让
    // 它死会关闭控件,使组合成 Rmdir,这既会降低极限,
    // 也会失去杀死该组合的唯一可靠方式。 源码树.
    // shared_ptr 因为下面的钩子活度超过这个功能.
    using Compiled = std::remove_reference_t<decltype(*compiled)>;
    auto held = std::make_shared<Compiled>(std::move(*compiled));

    // spawn_in, 不 产卵。 spawn() 转发 空 团体,
    // 和 spawn_in 只能看它被交出的团体 绝不看它所编译的姿势
    // 所以平原 调用 完全没有给客人留一个组 describe() 报告
    // 期间 `resource.memory strong via cgroup2`
    // 换成一样 编译 ,所以,请 面板 广告上写着一堵从未竖起的墙。
    // 调用 ,两件事固定了: 极限现在约束, 和群体存在杀死。
    auto spawned = spawn_in(held->plan, cmd, held->cgroup);
    if (!spawned) {
        out.start_error = "claybin: spawn failed: " + std::string{spawned.error().mechanism} +
                          " (errno " + std::to_string(spawned.error().sys_errno) + ")";
        return out;
    }

    out.started = true;
    out.pid = spawned->pid;
    out.pidfd = spawned->pidfd;

    // 客家出道网 过滤.
    //
    // claybin 将框放在自己的网络命名空间中,
    // 打开一个回路的听器; 从这里我们接受这个描述符,
    // 并决定每个连接可以拨号什么。 没有这个框, 则没有任何路径,
    // 所以启动失败失败失败。 -CLOSED (客人的
    // 联系被拒)而不是失败 -open.
    //
    // 生命周期 和经纪人的听众一样 坐在闭路电视上
    // SpawnResult 复制,代理服务器不复制,所以 shared_ptr 是什么使 使
    // 命令结果对象销毁时，代理也随之结束。
    // 真正的,没有跑者必须知道它的存在。
    if (spawned->proxy_listen_fd >= 0) {
        auto proxy = std::make_shared<netproxy::Proxy>();
        if (proxy->start(spawned->proxy_listen_fd, p.allow_hosts)) {
            AGT_LOG(Tool, Debug, "sandbox.proxy.up", "port={} pid={} netns={}",
                    spawned->proxy_port, spawned->pid, spawned->proxy_netns_ino);
            out.net_proxy = std::move(proxy);
        } else {
            AGT_LOG(Tool, Warn, "sandbox.proxy.start_failed", "pid={}", spawned->pid);
        }
    }

    // 杀死 源码树 当内核给我们手段时 cgroup.kill ,
    // 5.14+),否则会空空而出,使跑者保持其过程.
    // -group 回退 而不是相信一个杀人 却什么都没做。
    if (held->cgroup.valid() && held->cgroup.kill_supported()) {
        out.kill_tree = [held]() -> bool { return held->cgroup.kill().has_value(); };
    }

    // 电话 seccomp 听众,当政策经纪人 调用。
    //
    // claybin 将听众的手放在 CALLER ,
    // 而打电话者必须回答每 通知 :内核阻挡客人 线
    // 程 直到有人回应,所以无控听者是绞刑而不是弱墙.
    // 子进程选手投票 `supervisor_fd` 与输出管并列,
    // 调用 `service_broker` 当其可读.
    if (spawned->notify_fd >= 0) {
        out.supervisor_fd = spawned->notify_fd;

        // 听众住在关闭,由 shared_ptr ,
        // 因为 SpawnResult 是可以复制的,而听者则不是。
        // 当最后一份副本被复制时,它就会关闭。 回调 死亡,这是正确的
        // 生命周期 : 跑者将掉落 回调 当命令结束时。
        auto listener = std::make_shared<::clay::broker::Listener>(
            ::clay::OwnedFd{spawned->notify_fd});
        // 嘉宾自己的pid双人作为自己的流程组: claybin 调用
        // setsid() (new_session ),所以孩子领导
        // 自己的群体,每个后代继承它。 "inside my own group"
        // 由标尺决定的问题。
        const std::uint32_t guest_pid = static_cast<std::uint32_t>(spawned->pid);

        out.service_broker = [listener, guest_pid]() -> bool {
            auto req = listener->next();
            if (!req) return false;   // 客人走了,或者关闭了FD

            broker::Event ev;
            const auto verdict =
                broker::decide(req->nr, req->args, req->pid, guest_pid, &ev);

            // 该 CHECK 每个 IMPLEMENTATION GETS WRONG ,
            // 在回复前立即完成。
            //
            // A 通知 id一旦恢复使用,即可重新使用 线程 已经走了
            // 如果客人来了 线程 死亡时间介于 `next()` 回答是,
            // 不同 连环呼唤都比我们判断的要强 内核没有其他方法可以关闭它
            // 竞态 ,这就是为什么 seccomp_unotify
            // (2) 证明文件 ID_VALID ioctl作为
            // 强制性而不是咨询性.
            //
            // 以死人的本性归真, 通知 :听众
            // 本身仍然很好,只有这个请求是 过期
            // 拆掉这里的主管 就会拖着他 调用.
            if (!listener->still_valid(*req)) {
                AGT_LOG(Tool, Debug, "sandbox.broker",
                        "notification went stale before the response; dropped");
                return true;
            }

            const auto decision = verdict == broker::Verdict::Allow
                                      ? ::clay::broker::Decision::allow_it()
                                      : ::clay::broker::Decision::deny_it(EPERM);

            // 记录了每个结果,因为这是审计线索,使 沙箱 教得无碍.
            // "your build failed" 什么都不说;
            // "cargo tried ptrace ATTACH pid=1 and was denied"（cargo 尝试用 ptrace 附加 pid=1，操作被拒绝。）
            // 说工具链做什么 以及如果你不同意的话允许什么
            AGT_LOG(Tool, Info, "sandbox.broker", "{} {} {}",
                    ev.syscall, ev.detail, ev.allowed ? "allowed" : "DENIED");

            // 进入到食物中 环境 面板 读。
            // record() 忽略允许,所以这只是拒绝路径——日志是跟踪,
            // 种子是安全表面.
            //
            // 从工具调用 WORKER 线程 ; 饲料需要一个 锁
            // 这正是这个原因。
            broker::record(ev);

            if (!listener->respond(*req, decision)) {
                // 答题失败, 通常意味着客人在我们决定的时候死亡。
                // 不是一个值得拆卸的错误;
                // 下一个 next() 将报告听众完成。
                AGT_LOG(Tool, Debug, "sandbox.broker", "respond failed");
            }
            return true;
        };
    }
    return out;
}

// 壳体形式是前部有壳体的argv形式,管道,
// 转动和光滑是壳体工具的一部分,
// 所以这是真正的方便,而不是第二次执行——有一个产卵路径,
// 它位于上面。
SpawnResult spawn_shell(const Posture& p, const std::string& shell_cmd, int stdout_fd,
                        int stderr_fd) {
    return spawn_argv(p, {"/bin/sh", "-c", shell_cmd}, stdout_fd, stderr_fd);
}

#endif  // __APPLE__ / __linux__ 数据

}  // namespace agentty::tools::util::sandbox::claybin_backend

#endif  // __linux__ || __APPLE__
