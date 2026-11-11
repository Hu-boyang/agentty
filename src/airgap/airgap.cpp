// `agentty airgap` 是一条命令即可启动的工具，通过 SSH 隧道中的
// SOCKS5 代理，让 agentty 在无法直接联网的主机上运行。
// 请从能够联网的笔记本或其他机器上执行，连接到无法联网的远程主机：
//
//   agentty airgap user@host                 # 连接并运行远程 agentty
//   agentty airgap --setup user@host         # 同时复制 ~/.agentty/credentials/credentials.json 到远程
//
// 工作原理：
//   - `ssh -R 1080` 仅指定端口，让 OpenSSH 在远程的
//     localhost:1080 上提供 SOCKS5 代理。
//     连接经由 SSH 隧道回到本机，再由本机的 OpenSSH 建立外部连接。
//     因此，远程 agentty 需要访问的所有 TCP 目标，
//     包括聊天接口 api.anthropic.com、OAuth 刷新
//     接口 platform.claude.com，以及 web_fetch /
//     web_search 使用的任意主机，都能经由这一条隧道访问公网，
//     无需逐个配置目标主机。
//   - 在远程 shell 中设置 AGENTTY_SOCKS_PROXY=localhost:1080，让
//     远程 agentty 的每次连接都经过 SOCKS 代理，
//     避免在无法联网的主机上直接解析目标
//     地址而失败。
//   - 远程的 TLS、证书校验和 HTTP Host 请求头
//     仍然对应真实的上游服务，SOCKS 代理无法实施中间人
//     攻击。
//   - 使用 --setup 时，通过 scp 将本机的 credentials.json 复制到远程，
//     并设置 chmod 600，省去手动传输步骤。
//
// 在 POSIX 上，最后一步用 exec() 切换到 ssh，让 Ctrl-C、
// SIGWINCH 等信号直接到达 ssh。Windows 支持目前只有
// 占位实现；现代 Windows 自带 OpenSSH 客户端，
// 相关编排留待具备 Win32 测试环境的贡献者完成。

#include "agentty/airgap/airgap.hpp"
#include "agentty/auth/auth.hpp"
#include "agentty/io/clipboard.hpp"
#include "agentty/util/env.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <optional>
#include <maya/runtime.hpp>

namespace fs = std::filesystem;

namespace agentty::airgap {

namespace {

// 以可移植方式解析用户主目录：POSIX 使用 $HOME，Windows 使用 %USERPROFILE%，
// 或 %HOMEDRIVE%%HOMEPATH%。均未设置时返回空字符串。
std::string home_dir() {
    if (const char* h = std::getenv("HOME"); h && *h) return h;
#if defined(_WIN32)
    if (const char* up = std::getenv("USERPROFILE"); up && *up) return up;
    const char* hd = std::getenv("HOMEDRIVE");
    const char* hp = std::getenv("HOMEPATH");
    if (hd && *hd && hp && *hp) return std::string{hd} + hp;
#endif
    return {};
}

void print_usage() {
    std::fprintf(stderr,
        "usage: agentty airgap [--setup] [--clipboard-relay] [--remote-agentty PATH] <user@host>\n"
        "       agentty airgap [--setup] <user@host> --acp [acp flags…]\n"
        "\n"
        "  Opens an SSH session to <user@host> with `-R 1080`, which makes\n"
        "  OpenSSH expose a SOCKS5 proxy on the remote's localhost:1080.\n"
        "  Connections to it are tunnelled back through SSH and dialed by\n"
        "  this laptop, so the remote agentty — pointed at the proxy via\n"
        "  AGENTTY_SOCKS_PROXY — can reach every destination it needs (chat,\n"
        "  OAuth refresh, web tools) over a single tunnel.  TLS / cert\n"
        "  verification stay pinned on the real upstream end-to-end, so\n"
        "  the network path between laptop and remote can't MITM you.\n"
        "\n"
        "  Trust boundary (read this before --setup): the remote ends up\n"
        "  with a copy of ~/.agentty/credentials/credentials.json, which contains\n"
        "  your OAuth refresh token (or API key). A compromised remote\n"
        "  can therefore exfiltrate your Anthropic credentials independent\n"
        "  of the tunnel. agentty airgap protects the *network* between\n"
        "  laptop and remote, not the remote itself — treat the remote as\n"
        "  a credential-bearing peer, not a sandboxed proxy.\n"
        "\n"
        "  --setup            Copy ~/.agentty/credentials/credentials.json from\n"
        "                     this laptop to the remote (chmod 600) before\n"
        "                     launching.  Run this once on first connect or\n"
        "                     after re-OAuthing locally.\n"
        "  --remote-agentty PATH Absolute path to agentty on the remote.  Default:\n"
        "                     `agentty` (resolved via remote PATH).\n"
        "  --acp [acp flags…] Don't launch the TUI — instead print a ready-to-\n"
        "                     paste Zed `agent_servers` config that runs the\n"
        "                     remote `agentty acp` over this same SSH+SOCKS\n"
        "                     tunnel, with Zed owning the process. Everything\n"
        "                     after --acp is forwarded to the remote acp agent\n"
        "                     (e.g. --acp -m claude-haiku-4-5 --profile ask).\n"
        "                     Must come AFTER <user@host>.\n"
        "  --clipboard-relay  Make Ctrl+V paste images and files on the remote.\n"
        "                     Starts the clipboard agent here and forwards a\n"
        "                     socket to it, so the remote reads THIS machine's\n"
        "                     clipboard. Not needed if you ran `agentty paste\n"
        "                     setup` here: then every ssh session carries it.\n"
        "\n"
        "  ssh and scp must be on this laptop's PATH.  Pass extra ssh args\n"
        "  via the AGENTTY_AIRGAP_SSH env var (e.g. -i, -p, -J).\n"
        "\n"
        "  Without a relay, paste still works for text, and for images in\n"
        "  terminals that share the clipboard (kitty). `agentty paste doctor`\n"
        "  on the remote shows which route a paste takes.\n");
}

// 使用给定 argv 同步启动 `argv[0]`，并等待进程结束。
// 继承标准输入、标准输出和标准错误，返回子进程退出码；启动失败返回 -1。
// 两个平台都通过 PATH 查找程序。使用与其他子进程相同的进程类型，
// 因此 Windows 也会获得显式句柄列表。
int run_sync(const std::vector<std::string>& argv) {
    if (argv.empty()) return -1;
    namespace pf = maya::platform;
    // ssh 会在当前终端中显示提示并交互，因此继承当前
    // 标准流和会话；创建新会话会失去控制终端。
    pf::process_spec spec;
    spec.argv        = argv;
    spec.stdin_from  = pf::stream_to::inherit;
    spec.stdout_to   = pf::stream_to::inherit;
    spec.stderr_to   = pf::stream_to::inherit;
    spec.new_session = false;
    auto p = pf::native_process::spawn(spec);
    if (!p) {
        std::fprintf(stderr, "agentty airgap: failed to spawn `%s`: %.*s\n",
                     argv[0].c_str(), static_cast<int>(p.error().what.size()),
                     p.error().what.data());
        return -1;
    }
    // 前台 CLI 步骤：阻塞等待退出句柄就绪。
    auto reactor = pf::native_reactor::create();
    if (!reactor) return -1;
    auto reg = reactor->watch(p->exit_handle().get(), pf::interest::read, 1);
    if (!reg) return -1;
    for (;;) {
        auto w = reactor->wait(std::nullopt);
        if (!w) return -1;
        if (w->count != 0) break;
    }
    auto st = p->reap();
    if (!st) return -1;
    return st->how == pf::exit_status::kind::exited ? st->code : 128 + st->code;
}

// 将本机的 credentials.json 复制到远程默认存储目录
// ~/.agentty/credentials/。依次执行 mkdir、scp、
// chmod；任一步失败立即停止，让用户能看到失败的具体步骤。
int copy_credentials(const std::string& remote) {
    const fs::path local = agentty::auth::credentials_path();
    std::error_code ec;
    if (!fs::exists(local, ec) || ec) {
        std::fprintf(stderr,
            "agentty airgap: no local credentials at %s\n"
            "             run `agentty login` on this machine first.\n",
            local.string().c_str());
        return 1;
    }

    std::fprintf(stderr, "agentty airgap: copying credentials -> %s …\n",
                 remote.c_str());

    // 1) 为远程配置目录执行 mkdir 和 chmod 700；-p 保证重复执行仍然有效。
    if (int rc = run_sync({
            "ssh", remote,
            "mkdir -p ~/.agentty/credentials && chmod 700 ~/.agentty ~/.agentty/credentials",
        }); rc != 0) {
        std::fprintf(stderr,
            "agentty airgap: remote mkdir failed (ssh exit %d).\n", rc);
        return rc < 0 ? 1 : rc;
    }

    // 2) 使用 scp 复制凭据文件。
    {
        std::string dest = remote + ":.agentty/credentials/credentials.json";
        if (int rc = run_sync({"scp", "-q", local.string(), dest});
            rc != 0) {
            std::fprintf(stderr,
                "agentty airgap: scp failed (exit %d).\n", rc);
            return rc < 0 ? 1 : rc;
        }
    }

    // 3) 设置 chmod 600。scp 跨主机复制时不能可靠保留权限，
    //    而加载器要求 OAuth 令牌文件权限为 0600。
    if (int rc = run_sync({
            "ssh", remote,
            "chmod 600 ~/.agentty/credentials/credentials.json",
        }); rc != 0) {
        std::fprintf(stderr,
            "agentty airgap: remote chmod failed (ssh exit %d).\n", rc);
        return rc < 0 ? 1 : rc;
    }

    std::fprintf(stderr, "agentty airgap: credentials copied.\n");
    return 0;
}

// 给 `v` 加上 shell 单引号，使其在远程 shell 中始终作为一个字面参数传递。
// 内部的 `'` 转换为 `'\''`：先闭合单引号，再转义引号，最后重新开启单引号。
std::string sh_squote(std::string_view v) {
    std::string out;
    out.reserve(v.size() + 2);
    out.push_back('\'');
    for (char c : v) {
        if (c == '\'') out.append("'\\''");
        else           out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

// 使用 `ssh -t -R 1080 user@host <remote-cmd>` 替换当前进程。
// 成功时不会返回。argv 在堆上构造，因为 execvp 会修改
// 其 argv 槽位。
//
// 剪贴板：启用后，还会把远程 Unix 套接字转发到本机的 richpaste 代理；
// 远程粘贴逻辑会在 /tmp 中找到它。
[[noreturn]] void exec_ssh(const std::string& remote,
                           const std::string& remote_agentty,
                           const ClipboardForward& clipboard) {
    // 远程命令：让 agentty 使用隧道中的 SOCKS5 代理，再通过 exec 执行。
    // `exec` 至关重要：否则用户的 $SHELL 会
    // 继续作为父进程存在，使信号转发多经过一层并产生问题。
    //
    // MAYA_FORCE_SYNC=1：强制 maya
    // 在每个内联帧前后输出 DEC 2026 同步更新序列。
    // 下方也会转发终端识别环境变量，供 maya 自动探测；
    // 但裸 xterm、st、urxvt 和自定义终端
    // 等常见环境可能不留下识别标记，即使支持 2026
    // 也无法被探测到。现代终端会忽略未知的私有 CSI 序列，
    // 因此这里强制开启对少数不支持的终端无影响，
    // 却能消除其他终端中“新轮次先出现在底部、
    // 随后重新对齐”的闪烁。
    std::string remote_cmd = "AGENTTY_SOCKS_PROXY=localhost:1080 MAYA_FORCE_SYNC=1";
    // 剪贴板转发无需额外的远程命令行参数：远程粘贴逻辑会自行寻找 /
    // tmp/richpaste-<user>-*.sock。

    // 把本机的终端识别环境变量传给远程 shell，
    // 让远程 agentty 判断用户终端是否
    // 支持 DEC 2026 同步输出。
    // 缺少同步时，compose_inline_frame
    // 的逐单元格更新会按字节显示，输入框每次重绘
    // 都会闪烁。SSH 默认不传递这些变量：
    // SendEnv 需要服务器
    // sshd_config 逐项配置 AcceptEnv。
    // 因此这里通过命令行传递，无需更改 sshd 配置。
    // 本机未设置的变量直接跳过；
    // 传空字符串也只会让远程探测将其视为不存在。
    static constexpr const char* kTerminalMarkers[] = {
        // 通过终端程序名称和版本识别。
        "TERM_PROGRAM", "TERM_PROGRAM_VERSION",
        // 各终端特有的明确标记。
        "KITTY_WINDOW_ID",
        "ALACRITTY_LOG", "ALACRITTY_WINDOW_ID",
        "GHOSTTY_RESOURCES_DIR",
        "WEZTERM_EXECUTABLE",
        "WT_SESSION",          // Windows Terminal 终端。
        "KONSOLE_VERSION",
        "VTE_VERSION",          // GNOME Terminal、Tilix 等终端。
        "ITERM_SESSION_ID",     // iTerm.app 终端。
        // SSH 通常已转发 TERM，但这里仍显式传递，
        // 以防用户的 sshd_config 未放行。
        "TERM",
        // COLORTERM 表示真彩色支持，供 agentty
        // 的颜色渲染使用。
        "COLORTERM",
    };
    for (const char* name : kTerminalMarkers) {
        if (const char* v = std::getenv(name); v && *v) {
            remote_cmd += ' ';
            remote_cmd += name;
            remote_cmd += '=';
            remote_cmd += sh_squote(v);
        }
    }

    remote_cmd += " exec " + remote_agentty;

    std::vector<std::string> argv;
    argv.push_back("ssh");
    argv.push_back("-t");                       // agentty 需要交互终端。
    // `-R <port>` 不带 `host:hostport` 尾部时，
    // OpenSSH 在远程 <port> 提供 SOCKS 代理，
    // 收到连接后由本机这一侧解析并建立连接。两端均要求 OpenSSH
    // ≥ 7.6，现代系统通常已经满足。
    argv.push_back("-R"); argv.push_back("1080");

    // 剪贴板：把远程 Unix 套接字转发到本地 richpaste 代理。
    // StreamLocalBindUnlink 允许重连时复用残留的套接字路径。
    if (!clipboard.remote.empty()) {
        // %r 是 ssh_config 中的替换标记；命令行场景由此处填入。
        // 远程用户名取 @ 前面的部分；没有指定时使用当前用户名。
        std::string user;
        if (auto at = remote.find('@'); at != std::string::npos) user = remote.substr(0, at);
        else if (const char* u = std::getenv("USER"); u && *u) user = u;
        std::string sock = clipboard.remote;
        if (auto p = sock.find("%r"); p != std::string::npos) sock.replace(p, 2, user);
        argv.push_back("-o"); argv.push_back("StreamLocalBindUnlink=yes");
        argv.push_back("-R");
        argv.push_back(sock + ":" + clipboard.local);
    }

    // 连接保活的默认选项都放在 AGENTTY_AIRGAP_SSH 之前，
    // 让用户后来传入的 `-o Foo=bar` 可以覆盖默认值；
    // 同一配置项采用最后一个 `-o`。
    //
    //   ServerAliveInterval=30：对 SSH
    //                            会话发送连接保活消息，避免轮次之间
    //                            长时间无流量导致连接失效。
    //   ServerAliveCountMax=3：连续 3 次保活
    //                            无响应后断开。
    //   TCPKeepAlive=yes：为承载连接启用内核级 TCP 保活。
    //   ConnectTimeout=10：限制初始 TCP 连接时间，
    //                            避免不稳定网络让用户等待两分钟。
    //   ExitOnForwardFailure=yes：远程无法绑定 -R
    //                            1080 反向监听端口时立即明确报错，
    //                            例如端口已被占用；否则 SOCKS
    //                            代理并未建立，agentty 却会在稍后给出
    //                            令人困惑的 getaddrinfo 错误。
    //
    // 默认不启用压缩。内联模式帧是短小且突发的增量，
    // 每次渲染约 200 字节至 2 KiB；zlib 更
    // 适合大块数据，对小块数据反而可能因等待同步标记增加延迟，
    // 造成明显界面卡顿。网页抓取和大量 bash 输出获得的
    // 压缩收益不足以抵消界面延迟。带宽受限时可主动启用：
    // `AGENTTY_AIRGAP_SSH="-o Compression=yes" agentty airgap …`。
    //
    auto add_o = [&](const char* spec) {
        argv.push_back("-o"); argv.push_back(spec);
    };
    add_o("ServerAliveInterval=30");
    add_o("ServerAliveCountMax=3");
    add_o("TCPKeepAlive=yes");
    add_o("ConnectTimeout=10");
    add_o("ExitOnForwardFailure=yes");

    // AGENTTY_AIRGAP_SSH 允许用户传入额外选项，如 `-i`、
    // `-p`、`-J`，或通过后置 `-o ...` 覆盖上述默认值。
    // 按空白字符拆分参数；方式简单，但足以满足常见场景。
    if (const char* extra = util::env::get_or_null<util::env::Var::AirgapSsh>()) {
        std::string buf;
        for (const char* p = extra; ; ++p) {
            if (*p == ' ' || *p == '\t' || *p == '\0') {
                if (!buf.empty()) { argv.push_back(std::move(buf)); buf.clear(); }
                if (*p == '\0') break;
            } else {
                buf.push_back(*p);
            }
        }
    }

    argv.push_back(remote);
    argv.push_back(std::move(remote_cmd));

    // 启动 ssh 并继承标准流，让远程 TUI 驱动当前终端；等待结束后使用
    // 其退出码退出。此前用 execvp() 替换当前进程，现改为启动子进程
    // 并转发退出码，从用户 shell 看行为相同，同时支持没有 execvp
    // 的 Windows。两个平台均通过 PATH 查找 ssh；
    // Windows 10 及更新版本自带 OpenSSH。
    int rc = run_sync(argv);
    if (rc < 0) {
        std::fprintf(stderr,
            "agentty airgap: failed to run `ssh`.\n"
            "             ensure the OpenSSH client is installed and on PATH.\n");
        std::exit(1);
    }
    std::exit(rc);
}

// 输出可直接粘贴的 Zed `agent_servers` 配置，
// 由 Zed 管理 airgap 隧道。`command` 为 `ssh`，
// 参数同时建立反向 SOCKS 隧道并在远程执行 `agentty acp`。
// ACP JSON-RPC 经由 ssh 标准输入输出传输，
// 无需单独维护 ssh -N、远程包装脚本或环境块。
// 进程由 Zed 启动和终止；这正是 --acp 的用途。
//
// `acp_extra` 是用户在 --acp 后提供的 agentty-acp 参数，
// 例如 "-m claude-haiku-4-5 --profile ask --workspace /"；
// 它会原样追加到远程 `agentty acp` 命令后。
void print_acp_config(const std::string& remote,
                      const std::string& remote_agentty,
                      const std::string& acp_extra) {
    // 远程命令：先设置隧道 SOCKS 代理，再通过 exec 执行
    // ACP 代理。这样信号能直接到达 agentty，Zed 关闭
    // 标准输入时 ssh 也能正确终止它。
    std::string remote_cmd =
        "AGENTTY_SOCKS_PROXY=localhost:1080 exec " + remote_agentty + " acp";
    if (!acp_extra.empty()) remote_cmd += " " + acp_extra;

    // ssh 参数以 JSON 数组表示。-T 不分配 PTY，
    // 因为 ACP 是字节协议，PTY 的回显和回车转换会破坏数据。
    // -R 1080 建立反向 SOCKS 代理，保活选项维持长期连接。
    auto jstr = [](const std::string& s) {
        std::string o = "\"";
        for (char c : s) { if (c == '\\' || c == '"') o += '\\'; o += c; }
        o += '"';
        return o;
    };
    const std::vector<std::string> ssh_args = {
        "-T",
        "-R", "1080",
        "-o", "ExitOnForwardFailure=yes",
        "-o", "ServerAliveInterval=30",
        "-o", "ServerAliveCountMax=3",
        remote,
        remote_cmd,
    };

    std::string args_json;
    for (std::size_t i = 0; i < ssh_args.size(); ++i) {
        if (i) args_json += ", ";
        args_json += jstr(ssh_args[i]);
    }

    // 尽力明确告诉用户应把配置粘贴到哪个文件。
    std::string settings_hint;
#if defined(_WIN32)
    if (const char* appdata = std::getenv("APPDATA"); appdata && *appdata)
        settings_hint = (fs::path{appdata} / "Zed" / "settings.json").string();
#else
    if (std::string home = home_dir(); !home.empty())
        settings_hint = (fs::path{home} / ".config" / "zed" / "settings.json").string();
#endif

    std::fprintf(stderr,
        "agentty airgap --acp: add this to Zed's settings.json%s%s\n"
        "  (the laptop's settings — NOT the remote's; Zed runs `ssh` locally\n"
        "   and the agent over its stdio, so this works for a LOCAL Zed\n"
        "   project too, not just an SSH-remote one):\n"
        "\n"
        "  \"agent_servers\": {\n"
        "    \"agentty (airgap)\": {\n"
        "      \"command\": \"ssh\",\n"
        "      \"args\": [%s]\n"
        "    }\n"
        "  }\n"
        "\n"
        "Then pick “agentty (airgap)” in Zed's agent panel. One ssh process is\n"
        "the tunnel, the agent, and the transport — nothing to keep running by\n"
        "hand. Make sure `%s` has agentty installed and its credentials\n"
        "(run `agentty airgap --setup %s` once to copy them over).\n",
        settings_hint.empty() ? "" : "\n  → ",
        settings_hint.c_str(),
        args_json.c_str(), remote.c_str(), remote.c_str());
}

} // namespace

int cmd_airgap(int argc, char** argv) {
    bool        setup_mode = false;
    bool        clipboard_relay = false;
    bool        acp_mode = false;
    std::string remote_agentty = "agentty";
    std::string remote;
    std::string acp_extra;   // --acp 后面的全部参数都转发给 `agentty acp`。

    for (int i = 0; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help") { print_usage(); return 0; }
        else if (a == "--setup")       { setup_mode = true; }
        else if (a == "--clipboard-relay") { clipboard_relay = true; }
        else if (a == "--acp") {
            // --acp 后面的所有内容都是 agentty-acp 的参数，
            // 包括模型、配置、工作区和沙箱等。原样收集剩余参数，
            // 不再解析本工具的选项。
            acp_mode = true;
            for (int j = i + 1; j < argc; ++j) {
                if (!acp_extra.empty()) acp_extra += ' ';
                acp_extra += argv[j];
            }
            break;
        }
        else if (a == "--remote-agentty" && i + 1 < argc) {
            remote_agentty = argv[++i];
        }
        else if (!a.empty() && a[0] != '-' && remote.empty()) {
            remote = std::move(a);
        }
        else {
            std::fprintf(stderr, "agentty airgap: unrecognized argument: %s\n\n",
                         a.c_str());
            print_usage();
            return 64;
        }
    }

    if (remote.empty()) { print_usage(); return 64; }

    if (setup_mode) {
        if (int rc = copy_credentials(remote); rc != 0) return rc;
    }

    // --acp 本身不启动进程：它输出把 ssh 配置为
    // 代理服务器的 Zed 配置后退出。之后由 Zed 管理进程
    // 生命周期。
    if (acp_mode) {
        print_acp_config(remote, remote_agentty, acp_extra);
        return 0;
    }

    ClipboardForward clipboard;
    if (clipboard_relay) {
        std::string why;
        clipboard = clipboard_ssh_forward(&why);
        if (clipboard.remote.empty())
            std::fprintf(stderr,
                "agentty airgap: clipboard relay off: %s\n"
                "             continuing; text paste still works.\n",
                why.c_str());
    }

    exec_ssh(remote, remote_agentty, clipboard);  // [[noreturn]]
}

} // namespace agentty::airgap
