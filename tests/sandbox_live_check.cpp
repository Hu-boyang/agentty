// 现场执法检查,驾驶 agentty's 自身 claybin 产卵路径。
//
// 结束 -to-end 路线( 4) agentty 运行 - >
// LLM - > shell工具)在提供者打嗝时不断死去,反正它也是错误的
// 工具:它证明了模型可以 调用 工具,而不是那个 沙箱 强制 ,
// 这个 调用 同样的 claybin_backend::spawn_shell
// () 贝壳工具 调用 手建的姿势 看看孩子的手艺
//
// 两项索赔:
//   1. 下 编译器 剖析器, realloc() 作品( 硬币修复)
//   2. 与 net_mode =无,一个套接字到一个生 IP 无法连接( 净规则)
#include "agentty/tool/util/sandbox_claybin.hpp"
#include "agentty/tool/util/sandbox.hpp"
#include "agentty/domain/sandbox_config.hpp"   // 总是被破坏 *
#include "agentty/util/io.hpp"

#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <filesystem>
#include <fstream>   // 交接时 -prevention 案件读取 HOST 返回文件
#include <string>
#include <string_view>
#include <system_error>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

namespace cb = agentty::tools::util::sandbox::claybin_backend;

// 树枝的设置者 sandbox_config_race_stubs.cpp。
// 在这里宣布,而不是从 fs_helpers.hpp ,
// 因为包括 头文件 这正是这个目标所避免的(它伸入mcp,拖动工具层)。
namespace agentty::tools::util { void set_workspace_root(Io, std::filesystem::path p); }

namespace {

// 沙箱的工作区为本支票。创建于main();缺失的一款使每座
// 产卵在山上发生故障(ENOENT)。
constexpr const char* kWorkspace = "/tmp/agentty-sandbox-live-check";

int failures = 0;
void expect(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

// 读集 agentty 授权 默认时,足够 / bin/sh + 蟒蛇3  ⁇ .
cb::Posture base_posture() {
    cb::Posture p;
    p.system_read_roots = {"/usr", "/bin", "/sbin", "/lib", "/lib64", "/opt"};
    p.etc_readable      = {"/etc/ld.so.cache", "/etc/ld.so.conf", "/etc/ld.so.conf.d",
                           "/etc/resolv.conf", "/etc/hosts", "/etc/nsswitch.conf",
                           "/etc/passwd", "/etc/group", "/etc/localtime",
                           "/etc/ssl", "/etc/ca-certificates", "/etc/pki"};
    p.workspace         = kWorkspace;
    p.cwd               = kWorkspace;
    p.syscall_mode      = 1;   // 编译器 - 默认值, 错误在哪里
    p.net_mode          = 0;   // 满
    p.tmp_bytes         = 512ull * 1024 * 1024;

    // 非 国 家 -configurable 口罩,
    // 完全一样 build_claybin_posture ()设置它们。
    // 镜像而非空:一个空的面具列表是下面第一个版本的.env检查的原因。
    // "failed" 错误的原因——它测量了没有人会产生的那种姿态。
    if (const char* home = std::getenv("HOME"); home && *home) {
        for (const char* m : agentty::sandbox_cfg::kAlwaysMasked)
            p.masked.emplace_back(std::string{home} + m);
    }
    for (const char* n : agentty::sandbox_cfg::kAlwaysMaskedNames) {
        // 跳过 SUFFIX 规则( `*.pem`, `*.tfvars`
        // ) 后缀不是一条路径,在不知磁盘上是什么的情况下不能变成一条
        // 路径——产卵路径通过走来找到那些,这就是行走存在的全部原因。
        // 按键在磁盘上。 `*` 标记而不是硬码 ".pem" ,
        // 所以在列表中添加后缀不能默默地在其中放置一个无稽之谈的面具.
        // `<workspace>/*.tfvars` 给
        if (n[0] == '*') continue;
        // 每一个 NAME 规则,不只是点文件。 `n[0] != '.'`
        // 过滤器是列表中一个碰巧都是点文件的文物;现在它默默地滴入
        // `credentials.json` 财务报告和已审计
        // 财务报表 `service-account.json` ,
        // 所以这种控制会比产卵路径构建的姿态 更弱, 并且进行现场检查,
        // 测量错误的姿态就是上面评论已经警告过的失败模式。
        p.masked.emplace_back(std::string{kWorkspace} + "/" + n);
    }
    return p;
}

// 在姿态下运行一个 shell 命令, 捕捉它的输出, 返回退出代码。
int run(const cb::Posture& p, const std::string& cmd, std::string& out) {
    int fds[2];
    if (::pipe(fds) != 0) { std::printf("  pipe failed\n"); return -1; }

    auto sp = cb::spawn_shell(p, cmd, fds[1], fds[1]);
    ::close(fds[1]);
    if (!sp.started) {
        // 产卵失败就是 HARD 整个检查失败,不是安静的 -1
        // 打电话者可能读作 "blocked"。这个区别已经比比
        // 方说: 工作区 缺少目录, claybin 挂载失败(
        // M) ENOENT ),孩子从未逃跑,
        // "did not reach the network" 假设通过,
        // 因为没有达到任何标准。当主体从未执行时,一个负面测试比没有
        // 测试更糟糕。
        std::printf("  spawn failed: %s\n", sp.start_error.c_str());
        ++failures;
        ::close(fds[0]);
        return -1;
    }
    // 排水管 AND服务 主管,在一个民意调查。
    //
    // 两者,在同一圈,原因 生产跑者这样做:
    // 被中介的烟囱 将客人堵在内核 直到有人接听为止
    // 所以先看管子 后看听器 死锁 ——孩子等待决定,
    // 我们等待它不能产生的结果,直到它有一个。
    // 这个工具必须反映这一点,否则下面的中介案件将悬而不下,
    // 悬而不下。 "the test is broken".
    std::string chunk;
    bool eof = false;
    while (!eof) {
        struct pollfd pfds[2];
        pfds[0] = {fds[0], POLLIN, 0};
        const bool brokering = sp.supervisor_fd >= 0 && sp.service_broker;
        if (brokering) pfds[1] = {sp.supervisor_fd, POLLIN, 0};

        // 边框等待而非 - 1 : 停止回答的bug
        // 将表观为失败的断言, 而不是永远不返回的绳索。
        if (::poll(pfds, brokering ? 2 : 1, 10'000) <= 0) break;

        if (brokering && (pfds[1].revents & (POLLIN | POLLHUP | POLLERR))) {
            if (!sp.service_broker()) {
                sp.supervisor_fd = -1;
                sp.service_broker = nullptr;
            }
        }
        if (pfds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            char buf[4096];
            const ssize_t n = ::read(fds[0], buf, sizeof buf);
            if (n > 0) out.append(buf, static_cast<std::size_t>(n));
            else if (n == 0) eof = true;
            else if (errno != EINTR && errno != EAGAIN) eof = true;
        }
    }
    ::close(fds[0]);

    int status = 0;
    ::waitpid(sp.pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -2;
}

}  // 命名空间结束。

int main() {
    if (!cb::available()) {
        std::printf("claybin unavailable on this host (no user/mount namespaces) -- skipping\n");
        return 0;
    }

    // 电话 工作区 一定要这样 EXIST 之前的 claybin
    // 能够绑定它 - 否则每个产卵都会在山上死亡( ) ENOENT )
    // 和下面的负测试通过时空。这里创建的而不是假设,所以检查是自我的。
    // -contained 并且不能被清理/tmp的人打破。
    {
        std::error_code ec;
        std::filesystem::create_directories(kWorkspace, ec);
        if (ec) {
            std::printf("cannot create workspace %s: %s\n", kWorkspace,
                        ec.message().c_str());
            return 1;
        }
    }

    // build_bwrap_argv ( ) 约束
    // workspace_root ,所以根必须指向刚刚制作的目录。
    // 没有这个,包件将绑定 /tmp 和 mask /
    // tmp/.env ——然后通过,因为金丝雀在寻找其他生活。
    // 一个错误原因的检查是,这种绳索已经两次撞击了失败模式。
    agentty::tools::util::set_workspace_root(::agentty::IoAccess::grant(), kWorkspace);

    // ── net_mode = 点: 已列出 TCP 只打开端口, 不监听 ──────────
    std::printf("net_mode=Ports with 443 only:\n");
    {
        auto p = base_posture();
        p.net_mode = 2;
        p.allow_ports = {443};
        std::string out;
        run(p, "python3 - <<'EOF' 2>&1\n"
               "import socket\n"
               "s=socket.socket(); s.settimeout(2)\n"
               "try: s.connect(('127.0.0.1', 80)); print('tcp80 ok')\n"
               "except PermissionError: print('tcp80 denied')\n"
               "except Exception as e: print('tcp80 other', e)\n"
               "s=socket.socket()\n"
               "try: s.bind(('127.0.0.1', 0)); s.listen(); print('bind ok')\n"
               "except PermissionError: print('bind denied')\n"
               "EOF", out);
        expect(out.find("tcp80 denied") != std::string::npos, "an unlisted port is refused");
        expect(out.find("bind denied") != std::string::npos, "listening is refused");
        // UDP。此模式下。端口 SHARES 和
        // Landlock 的端口规则覆盖 TCP 仅 -
        // - UDP 是一个单独的访问权限,添加到 ABI 10,
        // 其中,“ claybin 无法处理。所以,
        // 这是客人可以向任何地方发送数据图的一种姿态,
        // 以及检查记录, 而不是站立的墙。
        out.clear();
        run(p,
            "python3 -c \"import socket\n"
            "for label, addr in (('internet', ('1.1.1.1', 53)), ('lan', ('192.168.1.1', 53))):\n"
            "    try:\n"
            "        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.settimeout(2)\n"
            "        s.sendto(b'x', addr); print('udp', label, 'SENT')\n"
            "    except Exception as e: print('udp', label, type(e).__name__)\" 2>&1",
            out);
        std::printf("  udp under Ports: %s", out.c_str());
        // ABI不是expect():在ABI以下的宿主上,
        // 10个地锁不能模拟UDP,而且没有什么可以断言的。只要它能够,
        // 这就是真正的墙,差距是真实的 -- -- 在固定之前测量到的SENT。
        if (out.find("PermissionError") != std::string::npos) {
            expect(out.find("udp internet PermissionError") != std::string::npos
                       && out.find("udp lan PermissionError") != std::string::npos,
                   "UDP to an unlisted port is refused under Ports (abi 10)");
            // 另一半是:一个港口,呼叫者 ALLOWED
            // 得工作过来 UDP 或允许53个休息 DNS。
            // 这是修复可能造成的回归。
            auto with_dns = base_posture();
            with_dns.net_mode = 2;
            with_dns.allow_ports = {53};
            out.clear();
            run(with_dns,
                "python3 -c \"import socket\n"
                "s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.settimeout(2)\n"
                "try:\n"
                "    s.sendto(b'x', ('1.1.1.1', 53)); print('dns53 SENT')\n"
                "except Exception as e: print('dns53', type(e).__name__)\" 2>&1",
                out);
            std::printf("  udp on an allowed port: %s", out.c_str());
            expect(out.find("dns53 SENT") != std::string::npos,
                   "UDP on an ALLOWED port still works (DNS keeps resolving)");
        }
    }

    // ── 严格仍运行一个 shell, W^X 应用时关闭过滤器 ──
    // 严格用于映射到with_filesystem(), 它没有执行,
    // 因此Hardened和Airgapped预设失败了每个命令。
    std::printf("syscall_mode=Strict runs a command:\n");
    for (int net : {0, 1}) {
        auto p = base_posture();
        p.syscall_mode = 2;
        p.net_mode = net;
        std::string out;
        const int rc = run(p, "echo strict-ok", out);
        expect(rc == 0 && out.find("strict-ok") != std::string::npos,
               net ? "strict + no network runs echo" : "strict + network runs echo");
    }
    std::printf("W^X with the filter off:\n");
    {
        auto p = base_posture();
        p.syscall_mode = 0;
        p.wx_protect = true;
        std::string out;
        run(p, "python3 -c 'import mmap; mmap.mmap(-1,4096,prot=7); print(\"MAPPED\")' 2>&1", out);
        expect(out.find("PermissionError") != std::string::npos, "an RWX mapping is refused");
        p.wx_protect = false;
        out.clear();
        run(p, "python3 -c 'import mmap; mmap.mmap(-1,4096,prot=7); print(\"MAPPED\")' 2>&1", out);
        expect(out.find("PermissionError") == std::string::npos, "W^X off allows an RWX mapping");
    }

    // ── 1. 重置 编译器 配置文件 ────────────────────────
    // 回归: 硬币没有出现在 base() ,所以,我说 realloc() 返回
    // 的大型块 NULL 和卷轴报告 "out of memory" 免费的机器上
    // RAM.
    //
    // 以蟒蛇而不是编译的探测器完成,所以检查本身没有
    // 建构步骤,在工作区之外也没有任何东西可留下.
    // c 类型 调用 直接的,然后是 分配器 路径卷曲命中。
    std::printf("realloc under syscall_mode=Compiler:\n");
    {
        auto p = base_posture();
        std::string out;
        int rc = run(p,
            "python3 -c 'import ctypes, os\n"
            "libc = ctypes.CDLL(None, use_errno=True)\n"
            "libc.mmap.restype = ctypes.c_void_p\n"
            "libc.mremap.restype = ctypes.c_void_p\n"
            "small, big = 1 << 20, 4 << 20\n"
            "p = libc.mmap(None, ctypes.c_size_t(small), 3, 0x22, -1, 0)\n"
            "assert p not in (0, 2**64 - 1), \"mmap failed\"\n"
            "print(\"mmap ok\")\n"
            "ctypes.set_errno(0)\n"
            "q = libc.mremap(ctypes.c_void_p(p), ctypes.c_size_t(small), ctypes.c_size_t(big), 1)\n"
            "e = ctypes.get_errno()\n"
            "print(\"mremap ok\" if (q or 0) != 2**64 - 1 else \"mremap FAILED errno=%d (%s)\" % (e, os.strerror(e)))\n"
            "b = bytearray(small)\n"
            "b.extend(bytes(big))            # realloc past the mmap threshold\n"
            "print(\"realloc ok\" if len(b) == small + big else \"realloc FAILED\")' 2>&1",
            out);
        std::printf("  exit=%d, child said: %s", rc, out.c_str());
        expect(rc == 0, "probe exits 0");
        expect(out.find("mremap ok") != std::string::npos, "mremap allowed");
        expect(out.find("realloc ok") != std::string::npos, "realloc works");
    }

    // ── 2. net_mode 没有任何隔离 ───────────────────────────────────────
    std::printf("connect() with net_mode=None:\n");
    {
        auto p = base_posture();
        p.net_mode = 1;  // 无
        std::string out;
        // 一个生的 IP ,因此此操作套接字层而不是 DNS.
        int rc = run(p,
            "python3 -c 'import socket,sys\n"
            "s=socket.socket(); s.settimeout(3)\n"
            "try:\n"
            "    s.connect((\"1.1.1.1\",443)); print(\"CONNECTED\")\n"
            "except OSError as e:\n"
            "    print(\"blocked:\", e)' 2>&1", out);
        std::printf("  exit=%d, child said: %s", rc, out.c_str());
        expect(out.find("CONNECTED") == std::string::npos, "did not reach the network");
    }

    // ── 3. net_mode 仍然有效,但只能通过代理 ───────
    // 另一半说法:孤立是 CHOICE 因此,
    // 允许设置必须实际允许。 沙箱 阻止一切不是执行政策,
    // 而是被打破。
    //
    // 或 "permit" MEANS 与审计 # 1 一起更改。
    // 直接连接失败( 框没有自己的回路—— 边界) ,
    // 而真正的联系是通过代理, 每个工具都通过内存找到代理。
    // 因此,这说明两个半部分: 原始套接字被封锁, 代理的获取工程。
    std::printf("net_mode=Full: direct vs proxied:\n");
    {
        auto p = base_posture();
        p.net_mode = 0;
        std::string out;
        int rc = run(p,
            "python3 -c 'import socket\n"
            "s=socket.socket(); s.settimeout(5)\n"
            "try:\n"
            "    s.connect((\"1.1.1.1\",443)); print(\"CONNECTED\")\n"
            "except OSError as e:\n"
            "    print(\"blocked:\", e)' 2>&1", out);
        std::printf("  direct exit=%d, child said: %s", rc, out.c_str());
        expect(out.find("CONNECTED") == std::string::npos,
               "a raw socket cannot leave the box, even on Full (#1)");
        out.clear();
        // 和那条路 IS 本来是想工作
        rc = run(p, "curl -sS --max-time 20 -o /dev/null -w net=%{http_code} "
                    "https://example.com/ 2>&1", out);
        std::printf("  proxied exit=%d, child said: %s\n", rc, out.c_str());
        expect(out.find("net=200") != std::string::npos,
               "a real fetch through the proxy works (#1)");
    }

    // ── 4. 面具是: 不 可配置 ────────────────────────
    // kAlwaysMasked/kAlwaysMaskedNames
    // 用户无法关闭是政策的一部分,因此,用户最值得衡量而不是信任。
    // 工作区 .env是最尖锐的案例: 工作区 绑定
    // READ-WRITE 由两个后端,所以范围不 保存
    // 我们和面具是唯一 站在特工和真正秘密之间的东西
    std::printf("workspace .env is masked:\n");
    {
        const std::string envp = std::string{kWorkspace} + "/.env";
        {
            std::FILE* f = std::fopen(envp.c_str(), "w");
            if (!f) { std::printf("  cannot plant .env\n"); ++failures; }
            else { std::fputs("SECRET=canary\n", f); std::fclose(f); }
        }
        auto p = base_posture();
        std::string out;
        int rc = run(p, "cat .env 2>&1", out);
        std::printf("  exit=%d, child said: %s", rc, out.c_str());
        expect(out.find("SECRET=canary") == std::string::npos,
               "the secret did NOT reach the child");
        std::error_code ec;
        std::filesystem::remove(envp, ec);
    }

    // ── 5. 卷曲,具体 ─────────────────────────────────────
    // 它本身的情况是因为卷曲报告EVERY早期的失败
    // 是CURLE_OUT_OF_MEMORY(27),
    // 它会让你在没有原因时查看内存的封顶和网络规则. python3
    // 在这个精确的姿态下获得相同的URL的罚款,所以这里卷曲
    // 失败是关于卷曲的启动,而不是边界.
    //
    // 以检查而不是注记的方式,因为卷曲是用户想要的,
    // "the sandbox broke curl" 无法区分
    // "the sandbox is broken" 从外而上.
    std::printf("curl reaches the network (net_mode=Full):\n");
    {
        auto p = base_posture();
        std::string out;
        int rc = run(p,
            "curl -sS -m 5 -o /dev/null -w 'H:%{http_code}' https://example.com 2>&1",
            out);
        std::printf("  exit=%d, child said: %s\n", rc, out.c_str());
        expect(out.find("H:200") != std::string::npos, "curl got a 200");
    }

    // ── 7. 秘密问题 下方 编号 工作区 根号 ──────────────────────────
    // 边框扫荡的间隙关闭。 kAlways MaskedNames 是
    // BASENAMES ,而名称在不知晓文件所在的情况下不能成为挂载 --
    // 因此只遮盖 `<workspace>/.env` 覆盖了无人使用的布局。
    // `services/api/.env`是它实际生活在单列波的地方.
    //
    // 还要检查一下扫荡必须完成的两件事 不 do: 沿着目录的符号链接
    // 离开 工作区 ,然后走进 node_modules.
    std::printf("nested secrets are masked (sweep depth):\n");
    {
        const std::filesystem::path root{kWorkspace};
        std::error_code ec;
        std::filesystem::create_directories(root / "services" / "api", ec);
        std::filesystem::create_directories(root / "node_modules" / "pkg", ec);

        auto plant = [&](const std::filesystem::path& p, const char* body) {
            std::FILE* f = std::fopen(p.c_str(), "w");
            if (f) { std::fputs(body, f); std::fclose(f); }
        };
        plant(root / "services" / "api" / ".env", "NESTED=canary\n");
        plant(root / "services" / "api" / "key.pem", "PEMBODY=canary\n");
        // 跳过目录内 : 不 蒙面,这是故意的 扫荡用它
        // 来换取40千块 -file 源码树 每一个产卵。
        // 如此一来,交易是显而易见的,而不是假设的。
        plant(root / "node_modules" / "pkg" / ".env", "SKIPPED=canary\n");

        auto p = base_posture();
        // 真实列表,从共享函数中——相同的列表,
        // 两个后端都消耗,因此在生产不同的情况下,这个列表不能通过.
        {
            agentty::sandbox_cfg::Config c;
            c.configured = true;
            const char* home = std::getenv("HOME");
            p.masked = agentty::sandbox_cfg::mask_paths(
                c, kWorkspace, home ? home : "");
        }

        std::string out;
        int rc = run(p, "cat services/api/.env services/api/key.pem 2>&1", out);
        std::printf("  exit=%d, child said: %s", rc, out.c_str());
        expect(out.find("NESTED=canary") == std::string::npos,
               "a nested .env is masked");
        expect(out.find("PEMBODY=canary") == std::string::npos,
               "a nested *.pem is masked (the suffix rule)");

        std::filesystem::remove_all(root / "services", ec);
        std::filesystem::remove_all(root / "node_modules", ec);
    }

    // ── 8. 默认的pid 盖实际上约束了叉炸弹 ──────────────
    // sandbox_audit 报告 resource.pids 以
    // `none` 默认安装,这意味着一个经核准的命令可以无限制地叉开。
    // max_procs 现在船位为4096, 是一个资源墙,
    // 每个主机上都有安全的数字(不需要4096个同时进行建造; 叉子炸弹需要
    // 数百万个)。
    //
    // 已经检查过 CHILD 而不是在报告中,
    // 因为 "cgroup2 pids.max" 在一个担保表中,
    // 以及 "the fork actually fails" 属于不同的索赔,这个子系统已经
    // 运出第一个,但没有第二个。
    std::printf("the default pid cap bounds a fork bomb:\n");
    {
        auto p = base_posture();
        p.max_processes = agentty::sandbox_cfg::Config{}.max_procs;
        std::string out;
        // 叉子直到失效,请打印它有多远。 沙箱 报告一个
        // 数字靠近上限;一个未绑定的号码运行到顶点。
        // HOST 痛苦是值得永远不运输的结果。
        int rc = run(p,
            "python3 -c 'import os,sys\n"
            "n=0\n"
            "try:\n"
            "    for _ in range(20000):\n"
            "        if os.fork()==0: os._exit(0)\n"
            "        n+=1\n"
            "except OSError:\n"
            "    pass\n"
            "print(\"forked\", n)' 2>&1", out);
        std::printf("  exit=%d, child said: %s", rc, out.c_str());
        // 盖子是4096,循环尝试20000,所以一个工作盖会让它短。
        // "did not reach 20000" 而不是确切的数字 :
        // 内核计数 线程 贝壳和蟒蛇已经握住,所以精确的天花板是
        // 主机 -dependent 并把它钉住, 这将是片状的。
        expect(out.find("forked 20000") == std::string::npos,
               "the fork loop hit a limit");
    }

    // ── 9. 单一名称的经纪活动决定而不是断然否认 ──────────
    // 整个子系统中最糟糕的失败模式的特征: 中介的
    // syscall 阻断了客人 IN 该 KERNEL
    // 如此一来,没有接受调查的经纪人就只能是绞刑而不是弱墙。
    // 如果情况恶化,那么每个案件都会被拖延而不是失败,
    // 这就是为什么他们要跟一个真正的孩子和真正的听众一起跑。
    std::printf("brokering: a self-ptrace is allowed\n");
    {
        auto p = base_posture();
        p.broker = true;
        std::string out;
        // PTRACE_TRACEME:儿童志愿者由自己的父母追踪。
        // 授权 没有权力来管理任何事物, `strace prog` 需要它,我需要它,你需要它,但你需要它。
        // 平面拒绝打破每个调试器; 经纪人允许它从scalars中中断。
        int rc = run(p,
            "python3 -c 'import ctypes,os\n"
            "libc=ctypes.CDLL(None,use_errno=True)\n"
            "ctypes.set_errno(0)\n"
            "r=libc.ptrace(0,0,0,0)\n"           // ⁇ 0  ⁇   ⁇  PTRACE_TRACEME
            "print(\"traceme\", r, ctypes.get_errno())' 2>&1", out);
        std::printf("  exit=%d, child said: %s", rc, out.c_str());
        // 决定是最重要的,而不是回报值: TRACEME 可能
        // 由于无关的原因失败(已经追踪到). EPERM
        // (1)是经纪人否认;任何其他意味着它通向内核.
        expect(out.find("errno=1") == std::string::npos &&
               out.find(" 1)") == std::string::npos,
               "TRACEME was not denied by the broker");
    }

    std::printf("brokering: attaching to pid 1 is denied\n");
    {
        auto p = base_posture();
        p.broker = true;
        std::string out;
        // PTRACE_ATTACH至pid 1 — 沙箱在
        // pidnamespace内自有的init, 以及一个容器-escape原始而不是调试步骤。
        // 目标是一个SCALAR, 正因为如此, 这一点才可以音速决定:
        // 没有读到 内存 的宾客, 所以没有 TOCTOU 窗口。
        int rc = run(p,
            "python3 -c 'import ctypes,os\n"
            "libc=ctypes.CDLL(None,use_errno=True)\n"
            "ctypes.set_errno(0)\n"
            "r=libc.ptrace(16,1,0,0)\n"          // 16  ⁇  PTRACE_ATTACH
            "e=ctypes.get_errno()\n"
            "print(\"attach\", r, e, os.strerror(e) if e else \"\")' 2>&1", out);
        std::printf("  exit=%d, child said: %s", rc, out.c_str());
        expect(out.find("attach -1") != std::string::npos,
               "ATTACH to pid 1 failed");
    }

    std::printf("brokering: the child still finishes (no hang)\n");
    {
        // 将伤害最大的回归。如果主管停止接受调查,
        // 或者回答一个问题。 通知 而不是下一个 —
        // 孩子永远挡住,命令在困难的最后期限上死去。
        // 只有检查决定的测试会通过,而每个真正的命令都
        // 完全取下 超时.
        auto p = base_posture();
        p.broker = true;
        std::string out;
        int rc = run(p,
            "python3 -c 'import ctypes\n"
            "libc=ctypes.CDLL(None,use_errno=True)\n"
            "for i in range(50): libc.ptrace(16,1,0,0)\n"
            "print(\"survived 50 brokered calls\")' 2>&1", out);
        std::printf("  exit=%d, child said: %s", rc, out.c_str());
        expect(out.find("survived 50") != std::string::npos,
               "50 brokered calls in a row did not hang");
        expect(rc == 0, "the child exited cleanly");
    }

    // ── 9. 决 定 真实 进入点,不是我们自己建立起来的姿态 ──────
    //
    // 驱动器上的所有设备 claybin::spawn 使用
    // Pposure 来构建此文件。这证明 claybin 执行它所被告知的,
    // 它确实 不 证明姿态 agentty 建筑也说同样的话
    //
    // 差距是真实的, build_claybin_posture
    // ()读取密封的配置,应用 kAlwaysMasked,
    // 运行 工作区 扫描,并决定音响描述。在测试中,
    // 逻辑的镜像是 第二 执行, 且两个漂移过一次( 包件在
    // 文件镜显示时, 根本没有发出面具)。
    //
    // 调用 和`bash` 工具调用 的函数相同,
    // 配置安装方式与main.cpp 安装方式相同,
    // 并读取孩子实际得到的东西。任何经过这里的功能都是对所运产品的真实的。
    std::printf("\nthe REAL path (run_shell_command, production posture):\n");
    {
        namespace sb = agentty::tools::util::sandbox;
        agentty::tools::util::set_workspace_root(::agentty::IoAccess::grant(), kWorkspace);
        sb::reset_config_for_test();
        agentty::sandbox_cfg::Config cfg;
        cfg.configured = true;            // 默认值 = 平衡态势
        sb::set_config(cfg);
        sb::init(sb::Mode::On);

        if (!sb::is_active()) {
            std::printf("  no backend on this host -- skipped\n");
        } else {
            std::printf("  %s\n", sb::describe_state().c_str());

            // 植入每枚蒙面的SHAPE中一个:平原名,
            // 巢名(需要扫荡),以及两个后缀规则.
            const std::string ws{kWorkspace};
            std::filesystem::create_directories(ws + "/services/api");
            auto plant = [&](const std::string& rel, const char* body) {
                std::FILE* f = std::fopen((ws + "/" + rel).c_str(), "w");
                if (f) { std::fputs(body, f); std::fclose(f); }
            };
            plant(".env",                 "ROOT_SECRET=should_never_appear\n");
            plant(".npmrc",               "//r/:_authToken=npm_should_never\n");
            plant("credentials.json",     "{\"key\":\"gcp_should_never\"}\n");
            plant("server.pem",           "PEM_should_never\n");
            plant("prod.tfvars",          "tf_should_never\n");
            plant("services/api/.env",    "NESTED_should_never=1\n");
            plant("main.cpp",             "int main(){}\n");

            auto r = sb::run_shell_command(
                "cat .env .npmrc credentials.json server.pem prod.tfvars "
                "services/api/.env 2>&1; echo ---; cat main.cpp",
                64 * 1024, std::chrono::seconds{30});

            // 任何秘密都不可能出现 `should_never` 是一个共享的标记,
            // 所以一个单 find() 盖六根皆有.
            const bool leaked = r.output.find("should_never") != std::string::npos;
            if (leaked)
                std::printf("  LEAKED: %s\n", r.output.c_str());
            expect(!leaked, "no masked secret reached a real bash call");

            // 还有 沙箱 未中断普通工作: a un
            // -secret 打开该目录的“文件”。
            // main.cpp 将经过上面的测试,打破每一个建筑。
            expect(r.output.find("int main") != std::string::npos,
                   "an ordinary workspace file is still readable");

            // Host-readable 授权 读在/, 和面具必须保持。
            const char* real_home = std::getenv("HOME");
            const std::string saved_home = real_home ? real_home : "";
            const std::string fake = std::string{kWorkspace} + "-home";
            std::filesystem::create_directories(fake + "/.ssh");
            if (std::FILE* f = std::fopen((fake + "/.ssh/id_ed25519").c_str(), "w")) {
                std::fputs("SSH_should_never\n", f);
                std::fclose(f);
            }
            ::setenv("HOME", fake.c_str(), 1);
            sb::reset_config_for_test();
            agentty::sandbox_cfg::Config hr;
            hr.configured = true;
            hr.fs_scope = agentty::sandbox_cfg::FsScope::HostReadable;
            sb::set_config(hr);
            sb::init(sb::Mode::On);
            auto h = sb::run_shell_command(
                "cat .env " + fake + "/.ssh/id_ed25519 2>&1; echo ---; head -c 4 /etc/os-release",
                64 * 1024, std::chrono::seconds{30});
            ::setenv("HOME", saved_home.c_str(), 1);
            std::filesystem::remove_all(fake);
            expect(h.started, "host-readable starts a command");
            expect(h.output.find("should_never") == std::string::npos,
                   "host-readable still masks .env and ~/.ssh");
            expect(h.output.find("---\n") != std::string::npos
                       && h.output.size() > h.output.find("---\n") + 4,
                   "host-readable can read outside the toolchain dirs");
            sb::reset_config_for_test();
            sb::set_config(agentty::sandbox_cfg::Config{});
            sb::init(sb::Mode::Auto);
        }

        // 硬化预设必须仍然运行一个命令(Strict曾经没有执行),
        // 并且 `~/` 读取路径必须 授权 其名称。
        if (sb::is_active()) {
            const char* home = std::getenv("HOME");
            const std::string dir = std::string{home ? home : "/tmp"} + "/.agentty-live-read";
            std::filesystem::create_directories(dir);
            if (std::FILE* f = std::fopen((dir + "/x").c_str(), "w")) {
                std::fputs("tilde-read-ok\n", f);
                std::fclose(f);
            }
            sb::reset_config_for_test();
            auto hard = agentty::sandbox_cfg::apply_posture(
                agentty::sandbox_cfg::Config{}, agentty::sandbox_cfg::Posture::Hardened);
            hard.configured = true;
            hard.fs_scope = agentty::sandbox_cfg::FsScope::Minimal;
            hard.read_paths = {"~/.agentty-live-read"};
            sb::set_config(hard);
            sb::init(sb::Mode::On);
            auto r = sb::run_shell_command("cat ~/.agentty-live-read/x",
                                           64 * 1024, std::chrono::seconds{30});
            expect(r.output.find("tilde-read-ok") != std::string::npos,
                   "Hardened runs a command and a ~/ read path is granted");
            std::filesystem::remove_all(dir);
            sb::reset_config_for_test();
            sb::set_config(agentty::sandbox_cfg::Config{});
            sb::init(sb::Mode::Auto);
        }

        // ── 为什么交接门不能是一个 沙箱 墙 ───────────────
        //
        // 明显反对信托 -handoff 门是
        // claybin 已约束进程，为何代理仍能写入 .vscode/tasks.json？
        // 这是答案,是衡量而不是论证。
        //
        // A 沙箱 墙壁答案 "may this PROCESS touch this PATH"。
        // 用于在 工作区 答案是正当的 YES - 必须是的,
        // 否则无法创建一个对象文件。 tasks.json 并不危险,
        // 因为 WHERE 危险是因为... VS 代码执行它
        // LATER ,在主机,任何外边 沙箱 我们安装了。
        //
        // 因此,禁闭和交接是不同的问题,
        // 这对检查针:墙上有禁闭的地方,而不会在禁闭的地方。
        // 如果第一个开始失败,大门就变得多余;
        // 如果第二个开始失败,就意味着: 沙箱
        // 打破了普通的工作。
        {
            std::printf("\nconfinement vs. handoff (why the gate exists):\n");
            const std::string ws{kWorkspace};
            std::filesystem::create_directories(ws + "/.vscode");

            // OUTSIDE 编号 工作区 : 数据 沙箱 一定要拒绝 这是禁闭 这是
            // claybin 工作就是工作 工作就是干这个的
            //
            // 不 / tmp. 姿态上挂一个私人的 tmpfs 在那里(
            // 参见 build_policy:
            // 确保不会泄漏到宿主 /tmp。
            // 所以写给 /tmp SUCCEEDS 在客人内部,永远没有到达主机——
            // 这看起来像是逃到天真的探测器,事实上是控制器起作用。我先写了这个探测器,
            // 它报告了 ESCAPED , 而主机文件并不存在。
            //
            // $HOME 诚实的考验:真实的,在 工作区 ,
            // 而不是虚拟化的姿态。
            const char* home = std::getenv("HOME");
            const std::string victim =
                std::string{home ? home : "/root"} + "/agentty_confinement_probe";
            auto out = sb::run_shell_command(
                "echo escaped > " + victim + " && echo wrote || echo denied",
                4096, std::chrono::seconds{15});
            // 客人自己的报告不是证据 虚拟化的目标会说 "wrote" 也一样。
            // Ask 编号 HOST 文件系统。
            std::error_code hec;
            const bool on_host = std::filesystem::exists(victim, hec);
            if (on_host) std::printf("  ESCAPED to %s\n", victim.c_str());
            expect(!on_host, "a write OUTSIDE the workspace never reaches the host");
            std::error_code rec;
            std::filesystem::remove(victim, rec);

            // 内部 编号 工作区 : 数据 沙箱 ALLOWS
            // 这儿没有墙可以靠着, 这就是为什么交接门是
            // 一个独立的机制。
            //
            // ORDINARY文件,而不是.vscode/tasks.json.
            // 此检查用于使用可信任路径,它当时是正确的选择 -- -- 要点是
            // 进程隔离本来就允许此操作，因此信任交接必须由另一层阻止。
            // 现在,预防将可信任路径绑定为-only,
            // 拼写将断言该门是NOT的工作。
            // 被封存的财产没有改变:平原 工作区
            // 写作是允许的 所以墙不是阻止危险的东西
            auto in = sb::run_shell_command(
                "echo hello > ordinary-file.txt && echo wrote || echo denied",
                4096, std::chrono::seconds{15});
            expect(in.output.find("wrote") != std::string::npos,
                   "a write INSIDE the workspace is allowed -- so confinement "
                   "cannot be what stops a trust handoff");
        }

        // ── 交接 PREVENTION : 读数 -only 绑定可信任的路径 ───
        //
        // 检测报告在 字节 停止了,这个机制是读数
        // -only 束缚路径凌驾于自身之上,
        // 这是用来掩盖秘密的同样诡计,也是出于同样的原因:
        // 地锁没有负面规则,所以你让路径不能被书写,
        // 而不能否认.
        //
        // 需要一个挂载命名空间, 因此整个块是有条件的。
        // 在拒绝非特权用户的主机上, 墙没有显示,
        // 检测是唯一的层 — — 这是诚实的结果,
        // 不是默默的降级。
        {
            std::printf("\nhandoff prevention (read-only bind):\n");
            const std::string ws{kWorkspace};
            std::filesystem::create_directories(ws + "/.vscode");
            // 必须 EXIST 在建立姿态之前:无法制成一条
            // 缺失路径的束缚,这就是检测覆盖的.
            {
                std::ofstream f(ws + "/.vscode/tasks.json");
                f << "{\"version\":\"2.0.0\"}\n";
            }

            // 调 -init 所以现在的姿势就变了 -existing 信任的路径。
            (void)sb::init(sb::Mode::Auto);

            auto w = sb::run_shell_command(
                "echo PWNED > .vscode/tasks.json && echo wrote || echo denied",
                4096, std::chrono::seconds{15});

            // 客人自己的报告不是证据,读一下 HOST 文档。
            std::string after;
            {
                std::ifstream f(ws + "/.vscode/tasks.json");
                std::getline(f, after);
            }
            const bool clobbered = after.find("PWNED") != std::string::npos;

            if (clobbered) {
                std::printf("  guest said: %s\n", w.output.c_str());
                expect(false,
                       "a host-trusted path must be READ-ONLY inside the "
                       "sandbox when mount namespaces exist");
            } else {
                expect(true, "a write to a host-trusted path is refused");
                // 肯定还是 READABLE - –  ⁇ –––– ⁇ –– ⁇  ⁇ – ⁇ – ⁇  ⁇  ⁇ – – ⁇ –  ⁇  ⁇ ––  ⁇   ⁇ – ⁇  – ⁇   ⁇  – – р ⁇ – ⁇ а ⁇   ⁇  ⁇   ⁇ ,  ⁇  / ⁇   ⁇ - ⁇   ⁇  -  ⁇ – р  ⁇  а
                // ⁇  –– ⁇  а  ⁇  р ⁇   ⁇ а ⁇ а ⁇ , р ⁇ а ⁇ и ⁇   ⁇ р ⁇  а) – а ⁇  ⁇  –р ⁇  –а ⁇  р а ⁇ а  ⁇ а – т ⁇   ⁇ е ⁇   ⁇ и ⁇ а ⁇ )  ⁇  τ ⁇   ⁇ т ⁇  –
                auto r = sb::run_shell_command(
                    "cat .vscode/tasks.json", 4096, std::chrono::seconds{15});
                expect(r.output.find("2.0.0") != std::string::npos,
                       "...and is still readable, not masked");
            }
        }
    }

    // ── 沙箱 审计 2026-10-17 ───────────────────────────────────────────────
    std::printf("\naudit 2026-10-17 regressions:\n");
    {
        // 9号:网路和vsock拒绝,unix/inet仍然很好.
        auto p = base_posture();
        std::string out;
        run(p,
            "python3 -c 'import socket\n"
            "for fam,name in ((16,\"netlink\"),(40,\"vsock\"),(1,\"unix\"),(2,\"inet\")):\n"
            "    try:\n"
            "        s=socket.socket(fam,socket.SOCK_DGRAM if fam!=1 else socket.SOCK_STREAM); s.close(); print(name,\"ok\")\n"
            "    except OSError as e: print(name,\"refused\",e.errno)' 2>&1", out);
        std::printf("  child said: %s", out.c_str());
        expect(out.find("netlink refused") != std::string::npos, "AF_NETLINK socket refused");
        expect(out.find("vsock refused") != std::string::npos, "AF_VSOCK socket refused");
        expect(out.find("unix ok") != std::string::npos, "AF_UNIX still works");
        expect(out.find("inet ok") != std::string::npos, "AF_INET still works");
    }
    {
        // #7: fake_hostname 假的保持真实的名字。
        char host[256] = {};
        ::gethostname(host, sizeof host - 1);
        auto p = base_posture();
        p.fake_hostname = false;
        std::string out;
        run(p, "uname -n", out);
        std::printf("  host=%s child(off)=%s", host, out.c_str());
        expect(out.find(host) != std::string::npos && out.find("sandbox") == std::string::npos,
               "fake_hostname=false shows the real host name");
        p.fake_hostname = true;
        out.clear();
        run(p, "uname -n", out);
        std::printf("  child(on)=%s", out.c_str());
        expect(out.find("sandbox") != std::string::npos,
               "fake_hostname=true shows `sandbox`");
    }
    {
        // #1: 交接的罗绑不得露出面具. 工作区 =一个假的 $HOME 持有
        // ~/.agentty/memory.jsonl (口罩) 里面。。
        // agentty (一个交割目录),审计打出的确切形状.
        namespace sb = agentty::tools::util::sandbox;
        const std::string fake_home = std::string{kWorkspace} + "-home";
        std::error_code ec;
        std::filesystem::create_directories(fake_home + "/.agentty", ec);
        std::filesystem::create_directories(fake_home + "/.config/autostart", ec);
        {
            std::ofstream f(fake_home + "/.agentty/memory.jsonl");
            f << "{\"text\":\"MEMORY_SHOULD_NEVER_APPEAR\"}\n";
        }
        {
            std::ofstream f(fake_home + "/.agentty/hooks.json");
            f << "{}\n";
        }
        {
            std::ofstream f(fake_home + "/.config/autostart/x.desktop");
            f << "[Desktop Entry]\n";
        }
        const char* old_home = std::getenv("HOME");
        const std::string saved = old_home ? old_home : "";
        ::setenv("HOME", fake_home.c_str(), 1);
        agentty::tools::util::set_workspace_root(::agentty::IoAccess::grant(), fake_home);
        sb::reset_config_for_test();
        agentty::sandbox_cfg::Config cfg;
        cfg.configured = true;
        sb::set_config(cfg);
        (void)sb::init(sb::Mode::On);
        if (sb::is_active()) {
            expect(sb::workspace_is_home(), "workspace == $HOME is detected");
            auto r = sb::run_shell_command("cat .agentty/memory.jsonl; echo END",
                                           4096, std::chrono::seconds{15});
            expect(r.output.find("MEMORY_SHOULD_NEVER_APPEAR") == std::string::npos,
                   "a masked file inside a handoff dir stays masked");
            auto w = sb::run_shell_command(
                "echo PWNED > .config/autostart/x.desktop && echo wrote || echo denied",
                4096, std::chrono::seconds{15});
            std::string after;
            {
                std::ifstream f(fake_home + "/.config/autostart/x.desktop");
                std::getline(f, after);
            }
            expect(after.find("PWNED") == std::string::npos,
                   "an autostart entry is read-only (#3)");
        } else {
            std::printf("  no backend -- skipped\n");
        }
        if (saved.empty()) ::unsetenv("HOME"); else ::setenv("HOME", saved.c_str(), 1);
        std::filesystem::remove_all(fake_home, ec);
    }

    // ── claybin-sandbox-audit.md #2#7,从一个真实的盒子里观察到 ──
    {
        auto p = base_posture();
        std::string out;
        // 2:/dev/shm是它自己的tmpfs带有盖,/dev本身是微小的.
        run(p, "df -B1 --output=size,target /dev/shm /dev 2>&1 | tail -n +2", out);
        std::printf("  df: %s", out.c_str());
        expect(out.find("67108864 /dev/shm") != std::string::npos,
               "/dev/shm is capped at 64 MiB (#2)");
        expect(out.find("1048576 /dev") != std::string::npos, "/dev is capped at 1 MiB (#2)");
        out.clear();
        // #3:盒子是OOM杀手的首选.
        run(p, "cat /proc/self/oom_score_adj", out);
        expect(out.find("500") != std::string::npos, "oom_score_adj is 500 (#3)");
        out.clear();
        // 5号:xattr读工作,所以保持安静.
        run(p, "ls -l /usr >/dev/null && echo ls-clean", out);
        expect(out.find("ls-clean") != std::string::npos &&
                   out.find("Operation not permitted") == std::string::npos,
               "ls -l prints no xattr EPERM noise (#5)");
        out.clear();
        // #6: 点缀 PR_SET_NAME 工作; 工作; PR_SET_MM / PR_CAP_AMBIENT 别
        // Python 无需编译器即可调用 prctl，因此用它作为探测解释器。
        run(p,
            "python3 -c 'import ctypes;l=ctypes.CDLL(None);"
            "print(\"name\",l.prctl(15,b\"box\",0,0,0));"
            "print(\"mm\",l.prctl(35,0,0,0,0));"
            "print(\"pers\",l.personality(0xffffffff)>=0, l.personality(0x0040000))' 2>&1",
            out);
        std::printf("  prctl: %s", out.c_str());
        if (out.find("No such file") == std::string::npos) {
            expect(out.find("name 0") != std::string::npos, "prctl(PR_SET_NAME) works (#6)");
            expect(out.find("mm -1") != std::string::npos, "prctl(PR_SET_MM) refused (#6)");
            expect(out.find("pers True -1") != std::string::npos,
                   "personality query ok, ADDR_NO_RANDOMIZE refused (#6)");
        }
        out.clear();
        // #7:拥有croup命名空间,没有核心堆放.
        run(p, "cat /proc/self/cgroup; ulimit -c", out);
        std::printf("  cgroup/core: %s", out.c_str());
        expect(out.find("0::/\n") != std::string::npos, "cgroup namespace hides the host path (#7)");
        expect(out.find("\n0\n") != std::string::npos || out.ends_with("0\n"),
               "RLIMIT_CORE is 0 (#7)");
        out.clear();
        // #4:会话套接字和提供商密钥无法到达嘉宾.
        ::setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/run/user/1000/bus", 1);
        ::setenv("WAYLAND_DISPLAY", "wayland-1", 1);
        ::setenv("GROQ_API_KEY", "gsk_should_never", 1);
        ::setenv("MY_BUILD_FLAG", "kept", 1);
        run(p, "env", out);
        expect(out.find("DBUS_SESSION_BUS_ADDRESS") == std::string::npos &&
                   out.find("WAYLAND_DISPLAY") == std::string::npos,
               "desktop/session sockets are not inherited (#4)");
        expect(out.find("gsk_should_never") == std::string::npos,
               "provider API keys are not inherited (#4)");
        expect(out.find("MY_BUILD_FLAG=kept") != std::string::npos,
               "ordinary env vars still are (#4)");
        ::unsetenv("GROQ_API_KEY");
        ::unsetenv("MY_BUILD_FLAG");
    }

    // ── 网络边界( 审计 # 1) ──────────────────────────────────
    //
    // 审计的发现是: "network allowed" 用来
    // 表示共享 agentty 'namespace,
    // 这样命令可以到达主机的回路服务以及整个 LAN。现在,
    // 这个盒子有它自己的命名空间和一个代理, 这些是它提出的主张。
    {
        auto p = base_posture();
        std::string out;
        // 主办单位 -only 服务框不得 能够到达,
        // 在主机回转时, 打开这个盒子。
        int hostsrv = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        int on = 1;
        ::setsockopt(hostsrv, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
        sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_port = htons(48231);
        sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        const bool have_srv = hostsrv >= 0
            && ::bind(hostsrv, reinterpret_cast<sockaddr*>(&sa), sizeof sa) == 0
            && ::listen(hostsrv, 4) == 0;

        // 它自己的命名空间:新鲜的有回路,没有其他的.
        run(p, "cat /proc/net/dev | awk 'NR>2 {print $1}' | tr -d ' :' | sort | tr '\\n' ' '", out);
        expect(out.find("lo") != std::string::npos && out.find("eth") == std::string::npos
                   && out.find("wl") == std::string::npos,
               "the box has its own netns: loopback only (#1)");
        out.clear();
        // 工具链的代理内涵在每句拼写中都读取.
        run(p, "env | grep -c -i '^\\(http\\|https\\|all\\)_proxy=127\\|^\\(HTTP\\|HTTPS\\|ALL\\)_PROXY=http'", out);
        expect(out.find("6") != std::string::npos,
               "all six proxy env spellings point at the box's loopback (#1)");
        out.clear();
        if (have_srv) {
            // 直接:主机的服务完全不在这个命名空间中.
            run(p, "curl -sS --max-time 5 --noproxy '*' -o /dev/null "
                   "-w direct=%{http_code} http://127.0.0.1:48231/ 2>&1", out);
            expect(out.find("direct=000") != std::string::npos
                       || out.find("Could not connect") != std::string::npos,
                   "a host loopback service is unreachable from the box (#1)");
            out.clear();
            // 通过代理:拒绝在已解决的地址上.
            run(p, "curl -sS --max-time 5 -o /dev/null -w loop=%{http_code} "
                   "-x http://127.0.0.1:3128 http://127.0.0.1:48231/ 2>&1", out);
            expect(out.find("loop=403") != std::string::npos,
                   "the proxy refuses the host's loopback (#1)");
            out.clear();
        }
        run(p, "curl -sS --max-time 5 -o /dev/null -w lan=%{http_code} "
               "-x http://127.0.0.1:3128 http://192.168.1.1/ 2>&1", out);
        expect(out.find("lan=403") != std::string::npos, "the proxy refuses RFC1918 (#1)");
        out.clear();
        run(p, "curl -sS --max-time 5 -o /dev/null -w meta=%{http_code} "
               "-x http://127.0.0.1:3128 http://169.254.169.254/latest/meta-data/ 2>&1", out);
        expect(out.find("meta=403") != std::string::npos,
               "the proxy refuses cloud metadata (#1)");
        out.clear();
        if (hostsrv >= 0) ::close(hostsrv);
    }

    // ── W^X:参数规则与真实的墙 ─────────────────────────────
    //
    // 电话 seccomp 规则否认 mmap/mprotect 与 PROT_WRITE
    // |PROT_EXEC 输入 一个 调用 无法看到真实的 shellcode 使用
    // 的序列 -- -- 映射它可以写、写到它、保护它可以执行 -- 因为每个都 调
    // 用 自己是无辜的 PR_SET_MDWE 是一个过程属性,所以它确实如此。
    //
    // 墙壁每间都断了 JIT (node/V8
    // 此处将主张两半的交易:默认的框
    // 仍然可以进行转换,而严格框则不能。
    {
        const char* probe =
            "python3 -c \"import ctypes\n"
            "l=ctypes.CDLL(None, use_errno=True)\n"
            "l.mmap.restype=ctypes.c_void_p\n"
            "l.mmap.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_int,ctypes.c_int,"
            "ctypes.c_int,ctypes.c_long]\n"
            "l.mprotect.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_int]\n"
            "F=ctypes.c_void_p(-1).value\n"
            "p=l.mmap(None,4096,1|2|4,2|32,-1,0)\n"
            "print('oneshot', 'REFUSED' if p==F else 'ALLOWED')\n"
            "q=l.mmap(None,4096,1|2,2|32,-1,0)\n"
            "print('upgrade', 'nomap' if q==F else "
            "('REFUSED' if l.mprotect(ctypes.c_void_p(q),4096,1|4)!=0 else 'ALLOWED'))\" 2>&1";
        std::string out;
        auto p = base_posture();
        run(p, probe, out);
        std::printf("  default: %s", out.c_str());
        expect(out.find("oneshot REFUSED") != std::string::npos,
               "W+X in one mmap is refused by default");
        expect(out.find("upgrade ALLOWED") != std::string::npos,
               "the W->X transition is ALLOWED by default, so JITs run");
        out.clear();
        auto strict = base_posture();
        strict.syscall_mode = 2;   // 严格:硬化/空加装
        run(strict, probe, out);
        std::printf("  strict:  %s", out.c_str());
        expect(out.find("oneshot REFUSED") != std::string::npos,
               "W+X in one mmap is refused on Strict");
        expect(out.find("upgrade REFUSED") != std::string::npos,
               "the W->X transition is refused on Strict (MDWE)");
    }

    // ── 默认资源节流 ─────────────────────────────────
    //
    // 内存和CPU曾报告NONE默认安装,因为唯一出价
    // 的把手是硬帽和配额-没有数字既适合8GB笔记本电脑,
    // 也不符合128GB工作站,所以错误的猜测将一个
    // 工作楼变成OOM杀机.
    //
    // memory.high 财务报告和已审计财务报表
    // cpu.weight 两者在实物上是不同的:一节油而不是杀戮,
    // 另一节油只能决定谁在争议中输掉。 两者都安全地违约,
    // 而且两半的债权都在这里核对——限制确实生效, 和 闲置机器不会被它们拖慢
    {
        auto p = base_posture();
        std::string out;
        // 框通过/proc读取自己的crogle,因为/sys缺席.
        run(p, "cat /proc/self/cgroup", out);
        const bool own_cgroup = out.find("0::/") != std::string::npos;
        expect(own_cgroup, "the box is in its own cgroup");
        out.clear();
        // 12个平行工作, 即形状 cpu.weight
        // 如果配额是,那么会慢一些。 沙箱.
        const char* work =
            "i=0; while [ $i -lt 8 ]; do python3 -c 'sum(range(8000000))' & "
            "i=$((i+1)); done; wait; echo done";
        const auto t0 = std::chrono::steady_clock::now();
        run(p, work, out);
        const auto boxed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - t0).count();
        expect(out.find("done") != std::string::npos, "parallel work completes in the box");
        std::printf("  8 parallel jobs in a box: %lldms\n", static_cast<long long>(boxed_ms));
        // 不是一个硬门槛 -- -- a 已加载 CI 机器会弄得一团糟
        // 索赔内容是: COMPLETED ,
        // 25%的配额将超过4x和a 内存 盖尔可能已经彻底杀死。
    }

    // ── 编号 JIT 内存 路径(节点为何死亡) ───────────────────────────
    //
    // `node -e` 死在一个盒子里 V8's
    // `Check failed: 12 == errno` 时值 claybin
    // 包括本周任何一次工作之前的修改。 --jitless 运行良好, 这使得失败出现
    // 在代码上 -memory 路径。这个走路径 调用 由 调用 所以答案是输出线而不是
    // 猜测。
    {
        auto p = base_posture();
        std::string out;
        run(p,
            "python3 -c \"import ctypes, errno\n"
            "l = ctypes.CDLL(None, use_errno=True)\n"
            "l.mmap.restype = ctypes.c_void_p\n"
            "l.mmap.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int,"
            "ctypes.c_int, ctypes.c_int, ctypes.c_long]\n"
            "l.mprotect.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int]\n"
            "F = ctypes.c_void_p(-1).value\n"
            "def nm(e): return errno.errorcode.get(e, str(e))\n"
            "def step(label, fn):\n"
            "    ctypes.set_errno(0)\n"
            "    rc = fn(); e = ctypes.get_errno()\n"
            "    bad = rc == F or (isinstance(rc, int) and rc < 0)\n"
            "    print(label, 'FAILED', nm(e)) if bad else print(label, 'ok')\n"
            "    return rc\n"
            "c = step('cage', lambda: l.mmap(None, 128<<20, 0, 2|0x20|0x4000, -1, 0))\n"
            "if c != F:\n"
            "    step('rw', lambda: l.mprotect(ctypes.c_void_p(c), 4096, 3))\n"
            "    step('rx', lambda: l.mprotect(ctypes.c_void_p(c), 4096, 5))\n"
            "    step('none', lambda: l.mprotect(ctypes.c_void_p(c), 4096, 0))\n"
            "    step('fixed', lambda: l.mmap(ctypes.c_void_p(c), 4096, 3, 2|0x20|0x10, -1, 0))\n"
            "f = step('memfd', lambda: l.syscall(319, b'p', 0))\n"
            "if isinstance(f, int) and f >= 0:\n"
            "    step('ftruncate', lambda: l.syscall(77, f, 4096))\n"
            "    step('memfd_rx', lambda: l.mmap(None, 4096, 5, 2, f, 0))\" 2>&1",
            out);
        std::printf("  JIT memory path in a box:\n");
        for (std::size_t at = 0; at < out.size();) {
            const auto nl = out.find('\n', at);
            const auto line = out.substr(at, nl == std::string::npos ? nl : nl - at);
            if (!line.empty()) std::printf("    %s\n", line.c_str());
            if (nl == std::string::npos) break;
            at = nl + 1;
        }
        // 笼子和 RW/RX 循环是什么样的 JIT 需要;
        // 其中任何一个都有一个失败是节点错误, 并命名 对应的 关键是一
        expect(out.find("cage ok") != std::string::npos,
               "a JIT can reserve its code cage");
        expect(out.find("rx ok") != std::string::npos,
               "a JIT can flip a page to executable (W->X is allowed by default)");
        // node/V8 需求 RWX 页面,所以它死 每当W ^X 已经开机了,
        // W型 ^X 离开它,必须运行, 即使在下面 编译器 过滤器。
        {
            auto np = base_posture();
            np.wx_protect = false;
            std::string nout;
            run(np, "command -v node >/dev/null || { echo NODE_OK; exit; }; "
                    "node -e 'console.log(\"NODE_OK\")' 2>&1 | tail -2", nout);
            expect(nout.find("NODE_OK") != std::string::npos,
                   "node runs under Compiler with W^X off");
        }
        // /proc/sys 可见( 3) V8 读 / proc/sys/vm )但读 -only.
        std::string sys;
        run(base_posture(), "cat /proc/sys/kernel/core_pattern | wc -c; "
                            "ls /proc/sys/vm >/dev/null && echo VM_OK; "
                            "echo 1 > /proc/sys/vm/drop_caches 2>/dev/null && echo SYS_WRITABLE; "
                            "echo x > /proc/sys/kernel/core_pattern 2>/dev/null && echo CORE_WRITABLE",
            sys);
        expect(sys.find("VM_OK") != std::string::npos, "/proc/sys/vm is readable");
        expect(sys.find("SYS_WRITABLE") == std::string::npos &&
                   sys.find("CORE_WRITABLE") == std::string::npos,
               "/proc/sys is read-only");
        expect(sys.rfind("0\n", 0) == 0, "core_pattern still masked");
    }

    std::printf("\n%s\n", failures ? "LIVE CHECK FAILURES" : "all live checks passed");
    return failures ? 1 : 0;
}
