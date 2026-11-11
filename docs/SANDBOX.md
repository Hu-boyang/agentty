<a id="sandboxing"></a>

# 沙箱隔离

agentty 执行的 Shell 命令都在操作系统强制实施的沙箱内运行，包括 bash 工具、diagnostics、Git、生命周期钩子和 ACP 终端。本文说明沙箱的边界、配置方式，以及明确不在其覆盖范围内的内容。

阅读本文时，请同时参考当前实现和可运行的审计程序。

- [沙箱配置](../include/agentty/domain/sandbox_config.hpp)、
  [执行边界](../src/tool/util/sandbox.cpp)与 [claybin 适配器](../src/tool/util/sandbox_claybin.cpp)说明安全预设、策略构造和后端执行方式。
- [沙箱设置面板](../src/runtime/panel/sandbox.cpp)展示如何通过控件反映
  宿主能力与配置。
- [`tests/sandbox_audit.cpp`](../tests/sandbox_audit.cpp) 是可运行的能力报告，
  构建并运行后，会输出当前机器实际执行的[能力表](#capabilities)。

---

<a id="the-one-property"></a>

## 核心安全属性

**已批准的命令也不能读取从未获准访问的凭据，不能写入工作区之外的位置。**

下文所有设计都服务于这一属性。此子系统重点防范的是边界报告已经启用、实际却没有执行限制的情况。agentty 曾出现过这类问题（issue #21 中显示 sandbox: active，但所有命令都因 uid_map 拒绝而失败），因此设计强调实际测量和验证。

---

<a id="quick-start"></a>

## 快速开始

| 目标 | 操作 |
|---|---|
| 查看当前实际执行的限制 | 查看启动提示或沙箱设置面板 |
| 同时收紧所有限制 | 沙箱面板 → Posture → `hardened` |
| 禁止命令访问网络 | Posture → `airgapped` |
| 只允许命令访问部分网站 | Network → Allowed hosts → `github.com`、`*.npmjs.org` 等 |
| 构建被过滤规则阻止 | Posture → `permissive`，仍保留挂载隔离 |
| 完全关闭沙箱 | `--sandbox off` |
| 强制要求沙箱，不可用时失败 | `--sandbox on` |

编辑设置后会保存，并在下次启动生效，详见[策略为何在启动后固定](#why-the-policy-is-sealed)。

---

<a id="backends"></a>

## 后端实现

| 后端 | 平台 | 选择方式 |
|---|---|---|
| `claybin` | Linux | 唯一引擎 |
| `claybin` | macOS | 默认 |
| `sandbox-exec` | macOS | 回退 |
| `claybin` | Windows | 默认 |
| — | Windows 回退后端 | 按设计不提供 |

Windows 不提供回退引擎或 bwrap。探测能通过 AppContainer 或作业对象建立隔离时，使用 claybin。否则启动提示会从 sandbox: active 变成 sandbox: unavailable, running unsandboxed，并明确说明命令将不受沙箱限制。未采用基于 Hyper-V 的 Windows Sandbox 产品，因为它需要数秒启动整个操作系统虚拟机，缺少管道标准输出模型和逐命令退出码，无法满足工具调用的启动、接管、流式读取和终止约定。AppContainer 在进程级实现相同策略，并按能力报告内核隔离的差距，见 [Windows 上的 claybin](#claybin-on-windows)。

<a id="why-claybin-is-the-only-linux-engine"></a>

### Linux 只使用 claybin 引擎的原因

bwrap 已被移除。原 bwrap 路径只提供挂载隔离，没有 seccomp、cgroup 资源限制或 Landlock。claybin 只要求 Landlock 或 seccomp 之一，而 seccomp 从 Linux 3.5 起可用，早于 bwrap 要求的 3.8 版非特权用户命名空间。因此能运行 bwrap 的宿主也能运行 claybin，无需保留该回退。

宿主无法启动 claybin 时，面板会明确说明并允许无沙箱运行。指定 --sandbox on 时则拒绝启动。

为兼容旧脚本，--sandbox-backend bwrap 仍可解析，但实际使用 claybin。面板不再提供 Backend 行。

<a id="claybin-on-macos"></a>

### macOS 上的 claybin

macOS 使用相同引擎和策略，在底层采用不同的隔离机制。claybin 将统一的边界描述按平台编译，因此读取集合、屏蔽规则和额外授权，与 Linux 使用相同代码路径并具有相同含义。

差异在于操作系统实际能够执行的限制。

| 能力 | Linux 上的 claybin | macOS 上的 claybin | sandbox-exec |
|---|---|---|---|
| 文件系统读写 | 强隔离（Landlock 与挂载命名空间） | 强隔离†（seatbelt） | 强隔离，固定配置 |
| 文件系统执行 | 强隔离（Landlock） | 强隔离†（seatbelt） | 强隔离，固定配置 |
| 网络隔离 | 强隔离（网络命名空间与代理） | 强隔离，仅开放代理的一个回环端口 | 无 |
| 系统调用过滤 | 强隔离（seccomp） | 无 | 无 |
| 内存与 CPU 限额 | 强限制（cgroup2） | 部分限制（rlimits） | 无 |
| 进程数上限 | 强限制（cgroup2） | 提示性限制，RLIMIT_NPROC 按 UID 计数 | 无 |
| ptrace 与 kill 代理 | 支持，使用 seccomp-notify | 无 | 无 |
| 面板中的逐路径授权 | 支持 | 支持 | 不支持 |
| 保证报告 | 提供 | 提供 | 不提供 |

† 对具有相同路径语义的策略可以报告 strong。agentty 的安全预设还包含 proc_fs、dev_fs、tmpfs 和屏蔽操作，这些操作的访问控制语义无法完全等同于目录树语义，因此计划保真度为 approximate，文件系统行报告 partial。实际测量输出见[能力表](#capabilities)。

两个 macOS 引擎都使用同一个内核机制。claybin 以编译后的配置调用 sandbox_init(3)，sandbox-exec 是 Apple 提供的命令行包装。区别在于配置如何传递。sandbox-exec 路径只拼接固定配置字符串，无法反映面板设置。claybin 编译实际配置，并逐项报告内核接受的限制。

普通 seatbelt 配置容易遗漏以下入口。每项行为都经过测量，并由 claybin 的 macos_escape_corpus_test 固定验证。测试在授权范围外放置探测文件，尝试 21 条越界路径。

- **网络只能经过代理。** macOS 没有私有
  网络命名空间，因此通过 (remote ip "localhost:PORT") 将来宾限制到 agentty 代理监听的唯一回环端口。端口由内核分配，避免被其他进程占用。其他端口、::1、局域网和互联网访问均返回 EPERM。命令通过 http_proxy / https_proxy / ALL_PROXY 获取代理地址，Node fetch 还设置 NODE_USE_ENV_PROXY=1。来宾不能直接使用 DNS，由代理负责解析，从而消除 DNS 旁路。
- **按名称允许必要系统服务。** 直接开放
  (allow mach-lookup) 会让没有主目录和网络授权的命令，通过沙箱外的守护进程读取剪贴板并列出钥匙串。这里只允许目录查询、日志、偏好设置和临时目录辅助服务。仅启用网络时增加 TLS 信任服务，始终不开放钥匙串守护进程 SecurityServer，因为 TLS 不需要它。
- **不允许全盘 stat。** 无限制的 (allow file-read-metadata) 会泄漏
  任意路径是否存在、大小和修改时间，例如 ~/.ssh/id_ed25519。元数据访问只允许根目录的直接子项，以及通向各授权路径的父目录，满足 realpath、getcwd 和 Git 仓库发现的需要。
- **支持 Xcode 工具链。** /usr/bin/git、cc 和 python3 是
  xcrun 包装入口，需要从当前开发者目录加载文件，并向 $TMPDIR（/var/folders/…，而非 /tmp）写缓存。因此授权这两个位置，还以只读方式授权 ~/.gitconfig，避免 Git 因配置不可读而失败。

seatbelt 无法限制只读的 launchctl print，因为所有进程都继承可访问的 bootstrap 端口，但会拒绝 launchctl submit。/bin/ps 等 setuid 程序则无法在任何 seatbelt 配置下执行。这些能力不在保证范围内。

还需要明确以下两个差距。

- **不提供系统调用过滤。** macOS 没有向非特权进程开放的
  seccomp 等价机制，因此请求 Isolation::hardened_process 会被拒绝，不会静默降级，也不提供系统调用代理。
- **rlimits 与 cgroup 的计数范围不同。** rlimit 按进程限制并被继承，
  cgroup 则统计整个进程树。四个各有 1 GB 限额的子进程合计可使用 4 GB。RLIMIT_NPROC 更弱，它统计整个 UID 的进程，其他终端窗口也会影响配额，因此报告 advisory。

macOS 上请求 claybin，但 seatbelt 不可用时，回退到 sandbox-exec，遵循明确的回退规则。

<a id="claybin-on-windows"></a>

### Windows 上的 claybin

Windows 同样使用统一的引擎和策略。纯函数 compile() 在每个平台执行单元测试。实际应用步骤位于 claybin/windows/spawn.cpp，为每组能力创建 AppContainer 配置，并可选地为授权路径添加 DACL 和完整性 ACE。未启用该操作时，报告会明确标为 partial。资源上限通过作业对象实施，系统调用安全预设转换为进程缓解策略。

| 能力 | Windows 上的 claybin | 说明 |
|---|---|---|
| 文件系统读写与执行 | 部分限制 | AppContainer 默认拒绝，并使用逐对象 ACE |
| 网络隔离 | Full / Ports 为部分限制，None 为强隔离 | 无网络能力就不能创建套接字 |
| 进程隔离 | 强隔离，使用 AppContainer 令牌 | 对应 Linux 用户与 PID 命名空间提供的边界 |
| 系统调用过滤 | 部分限制 | 采用缓解策略，无可编程过滤器，拒绝 hardened_process |
| 内存、CPU、进程数上限 | 强限制，使用作业对象 | 与 cgroup v2 一样强制实施 |
| 权限降低 | 取决于宿主 | 只有实际探测证明受限令牌与 AppContainer 可组合运行时才报告 strong |
| 设备隔离 | 部分限制 | 使用 AppContainer 对象命名空间 |
| 宿主内核隔离 | 无 | 这是进程级后端 |

<a id="the-apply-step-in-order"></a>

#### 按顺序应用限制

sandbox.cpp 构造与其他平台相同的 Posture，交给 claybin_backend::spawn_shell。在此平台会调用 spawn_argv({"cmd.exe", "/d", "/s", "/c", <command>})，随后依次执行以下步骤。

1. **build_policy(posture)** 生成与 Linux 和 macOS
   使用同一份固定策略。读取集合、屏蔽规则、网络模式和资源限制均为相同的数据值。
2. **::clay::windows::compile(sealed, probe_host(), opts)** 为每组
   能力生成独立 AppContainer 配置，并设置受限令牌、作业限制和缓解策略。配置名称包含能力集合哈希，避免不同策略共用一个配置。
3. **opts.allow_host_acl_mutation = true** 明确启用一次。ACL
   写入会修改宿主，因此 claybin API 要求主动选择启用。agentty 选择启用，使 AppContainer 能访问用户授权的路径。逐对象 ACL 与路径前缀模型并不等价，因此保证报告仍将文件系统标为 partial。
4. **环境变量**通过 GetEnvironmentStringsW 读取并转换为 UTF-8，
   再叠加各次调用的覆盖配置。
5. **spawn()** 返回 Spawned{pid, process, job}。SpawnResult 的两个 int
   字段保存 HANDLE。pidfd 是子进程 PROCESS 句柄，由 jaal 的 windows_process::adopt 接管并等待退出。kill_tree 持有 JOB 句柄。轮询、输出排空、空闲截止时间、宽限和回收仍由 subprocess.cpp / exec.cpp 统一处理，与无沙箱路径共用实现。
6. **没有系统调用代理。** supervisor_fd 和 service_broker 保持
   默认哨兵值，因为 Windows 没有 seccomp user-notify 等价机制，不提供 ptrace / kill 代理，详见[系统调用代理](#syscall-brokering)。

<a id="read-roots-and-the-header-floor"></a>

#### 读取根目录与头文件最低访问范围

Windows 的读取集合由 POSIX 策略转换而来，并从环境变量解析实际路径，因为系统盘不一定是 C:。

| 内容 | 来源 | 用途 |
|---|---|---|
| `%SystemRoot%` | 环境变量，回退到 `C:\Windows` | 加载器、DLL、System32 |
| `%ProgramFiles%`、`%ProgramFiles(x86)%` | 环境变量 | 已安装工具 |
| `%ProgramData%` | 环境变量 | 对应 /etc，保存软件包元数据和宿主 SSH 配置 |
| `%USERPROFILE%` 下的工具目录 | 环境变量 | `.cargo\bin`、`\go\bin`、`\.nvm`、`\.pyenv`、`\.rbenv`、`\.bun\bin`、`\.deno\bin`、`\.dotnet` |
| `%SystemDrive%\` | 环境变量，仅用于 HostReadable | 明确表示方便使用的较宽读取范围 |

首次 Windows 发布曾遇到头文件版本问题。claybin 在目标上公开设置 _WIN32_WINNT=0x0A00，因为 MinGW 默认使用 Windows 8 之前的头文件，会完全隐藏 AppContainer API。不设置时 spawn.cpp 缺少声明，无法编译。

<a id="two-behaviors-specific-to-this-platform-measured-not-assumed"></a>

#### 该平台通过实测确认的两项特有行为

- **降低权限的探测结果在进程生命周期内缓存。** 实测某些 Windows
  版本（Insider 26300）中，受限令牌与 AppContainer 的组合依赖进程预热状态。早期探测失败，后期可能成功，逐次启动重新探测可能生成导致来宾在 ntdll 初始化时以 0xC0000142 失败的限制。因此使用与生产相同的挂起创建、分配作业、指定句柄列表方式运行子进程三次，并在整个进程中保留最初、最冷状态的结论。
- **取消时先终止整个进程树。** jaal 接管的子进程不携带作业句柄，
  优雅停止只能终止主进程。cmd 中的 ping 等孙进程仍可能持有标准输出管道，导致两秒的 Esc 操作变成二十秒等待。执行循环先通过沙箱 kill_tree 调用 TerminateJobObject，再进行优雅停止，宽限阶段作为回退。kill_tree 闭包拥有作业句柄，析构时 CloseHandle 触发 KILL_ON_JOB_CLOSE，终止遗留进程。同一所有权机制也修复了每条沙箱命令泄漏作业句柄、导致正常退出后仍遗留后台进程的问题。

/proc 与 /dev 策略根映射到虚拟对象命名空间哨兵，不存在可写 ACL 的实际对象，因此跳过这些授权，由 AppContainer 默认拒绝提供隔离。Windows 上运行 sandbox_audit 查看实际限制列表，tests/sandbox_win_posture_test.cpp 验证端到端执行链。

<a id="the-settings-pane"></a>

#### 设置面板

preview_sandbox 使用与启动路径相同的 claybin Windows 编译器和 ACL 授权开关，预览面板实际会施加的限制。文件系统通过 AppContainer 与 DACL 报告 partial，内存和进程数通过作业对象报告 strong，系统调用过滤使用缓解策略并报告 partial。

停止和启动路径提供两个由环境变量控制的跟踪入口，用于诊断只在负载下出现、否则需要重编译整个代码库的故障。

| 变量 | 输出内容 | 位置 |
|---|---|---|
| `AGENTTY_STOP_TRACE=1` | 每次反应器等待的请求截止时间和实际时间、事件及令牌、停止与进程树强制终止顺序，以及 run_child 的排空和输入线程等待过程 | `src/tool/util/exec.cpp` |
| `CLAYBIN_TRACE=1` | 每次启动使用的 API、AppContainer 和受限令牌是否生效、能力数、资源限制、工作目录、标志，以及 CreateProcess 是否成功 | claybin 的 `src/windows/spawn.cpp` |

未设置时，两者每次调用只付出一次 GetEnvironmentVariable 的开销并保持静默。上面的取消故障正是通过它们定位，因此保留以便后续诊断。

<a id="availability-is-a-probe-not-a-which"></a>

### 通过实际探测判断可用性

Linux 的 available() 创建子进程并实际尝试写入 uid_map。macOS 创建子进程并真正调用 sandbox_init()。Windows 创建再删除 AppContainer 配置，并在实际编译出的限制下运行子进程，结论在进程生命周期内缓存，见 [Windows 上的 claybin](#claybin-on-windows)。仅能在 PATH 找到程序或符号，不代表它能成功启动命名空间或应用隔离。

macOS 必须在子进程探测，因为 seatbelt 配置不可逆，在当前进程内探测会把 agentty 自身永久限制在沙箱中。

---

<a id="postures"></a>

## 安全预设

对于只想整体收紧限制的用户，应提供安全预设。Posture 行通过预设修改其他所有设置行，同时保留 28 个具体开关。

| 安全预设 | 含义 | 代价 |
|---|---|---|
| `permissive` | 只保留挂载隔离 | 无系统调用过滤以及内存、CPU、时间限额 |
| `balanced` | 与默认策略完全一致 | 通过独立网络命名空间和 agentty 代理访问互联网，禁止宿主回环、局域网、云元数据和 tailnet |
| `hardened` | 启用 claybin 能提供的全部限制 | 依赖特殊系统调用的构建可能失败 |
| `airgapped` | hardened 加完全断网 | git push、npm install、curl 均无法使用网络 |
| `custom` | 不能直接选择 | 各行不匹配任何预设时自动报告 |

**预设会在重启后保持。** 选择 airgapped 再启动仍会显示 airgapped。曾有两个问题导致它变成 custom，现在都由 tests/sandbox_preset_roundtrip_test.cpp 固定验证。测试应用所有预设及其相互切换，写入真实配置文件，再按启动方式加载，并报告丢失的字段。

- 加载器的范围检查曾把 memory_mb、
  tmp_mb 等正的 64 位数值误判越界并重置，使 hardened 和 airgapped 丢失内存上限。
- 面板曾把锁定行的显示值写回配置。锁定行显示
  引擎实际强制执行的值，例如 seatbelt 下 Scope IPC 始终开启。回写这些值会让 permissive 选中后立即变成 custom。

旧版本保存过 hardened 或 airgapped 时，重新选择一次即可修正并保留预设。

**permissive 仍保留防止 fork 炸弹的进程数上限。** 它移除内存、CPU、文件描述符和时间限额，因为合理上限依赖机器，错误估计可能让 cargo build 因内存不足被终止。max_procs 是例外，4096 个并发进程足够正常构建，而 fork 炸弹需要数百万个。过去它也被清零，但边界上的 0 表示无限制，会同时关闭 rlimit 和 pids.max，重新引入 sandbox_audit 要检查的无限 fork 风险。放宽隔离不应让整台机器失去保护。

<a id="what-the-label-asserts--and-what-it-doesnt"></a>

### 状态标签所表达的保证范围

detect_posture 按固定点匹配预设，apply_posture 不写入的字段不属于预设定义。write_paths、allow_ports、allow_hosts 仍被实际执行，因此显示 Hardened 的配置仍可能包含用户添加的额外授权。

预设按约束不修改用户路径列表，避免试用预设时丢失项目的 read_paths。因此标签表示预设负责设置的限制已经生效，完整授权仍应查看全部 28 行。标签概括状态，设置行展示完整事实。sandbox_pane_test 固定验证这一含义。

airgapped 是唯一能让[威胁模型](#threat-model)中读取权限加网络即可外传数据这一前提不再成立的配置。

预设通过以下三个属性避免成为另一份权威状态。

1. **预设写入各行，保留所有 28 行供用户查看
   和编辑。** 不能用一个标签代替无法继续检查的实际边界。
2. **标签始终从配置推导。** detect_posture() 询问
   apply_posture() 各预设会生成什么配置，再进行比较。若单独存储标签，编辑一行后它就会过期。派生标签只能匹配某个预设或显示 Custom。
3. **预设不修改引擎或用户路径列表。** 在 claybin 无法启动的宿主上选择 hardened，
   不会静默切换后端，也不会因试用预设而清除项目 read_paths。

---

<a id="allowed-hosts"></a>

## 允许访问的主机

当 Access 为 `full` 时，所有连接都经过 agentty 的代理。代理已拒绝回环地址、私有网段、链路本地地址和云元数据地址。**Allowed hosts（允许的主机）** 一项可以进一步按主机名限制连接：

| 条目 | 匹配 | 不匹配 |
|---|---|---|
| `github.com` | `github.com` | `api.github.com`, `github.com.evil.io` |
| `*.github.com` | `api.github.com`, `a.b.github.com` | `github.com` 本身、`evilgithub.com` |

- **清单为空表示允许任意公网主机**，这也是目前的默认行为。
  界面会明确显示这一点（`any public host · Enter to restrict`，即“任意公网主机 · 按 Enter 限制”）。
- **`*.x` 只匹配 `x` 的子域名。** 如果要同时允许主域名和子域名，需要同时列出 `github.com` 和
  `*.github.com`；编辑器会提示这一点。如果所有条目都是通配域名，面板也会发出提醒。
- **IP 地址不能加入允许清单。** 设置清单后，`curl
  https://140.82.x.x` 会被拒绝，否则任何主机名规则都能被直接使用 IP 地址绕过。
- **解析前先检查命令请求的主机名**，随后由代理自行解析
  同一个名称。命令无法借助 DNS、`--resolve` 或伪造的 `Host:` 请求头改变目标；即使名称在允许清单中，如果解析到 `10.0.0.1`，地址过滤器仍会拒绝连接。
- **粘贴 URL 时会自动提取主机名**（`https://GitHub.com/x` →
  `github.com`），编辑器会在下方显示 `saved as …`（保存为……）。`.npmjs.org` 按 `*.npmjs.org` 处理。单独的 `*`、`*.com`、通配符不在首个标签的名称、IP 地址以及其他无效主机名都会被拒绝，并显示原因；关闭编辑器时会丢弃无效条目。
- **面板和代理使用同一套规则**（`netproxy::normalize_host_entry`），
  因此编辑器接受的条目就是实际执行的规则。手动编辑 `settings.json` 后，若某个条目无效，警告会指出该条目；如果清单中的条目全部无效，会阻止所有连接，避免意外放宽为“允许任意主机”。
- **只有支持代理的客户端才能联网。** curl、git、pip、npm、Python 的 urllib 和
  Node 的 `fetch` 都会遵守 agentty 设置的代理环境变量。忽略这些变量的工具将**无法联网**：底层内核规则只允许它连接代理，无法获得未经过滤的网络访问。

模型也能获知这份清单：系统提示词会列出允许的主机并解释 `*.` 规则。命令被拒绝时，例如 curl 显示 `CONNECT tunnel failed`、Python 显示 `Tunnel connection failed`，或代理返回 `403`，输出会附带 `[sandbox]` 说明，列出可访问的主机，帮助模型改用允许的镜像站。

配置保存在 `settings.json` 的 `sandbox.allow_hosts` 中：

```json
"sandbox": { "net_mode": 0, "allow_hosts": ["github.com", "*.github.com", "registry.npmjs.org"] }
```

---

<a id="capabilities"></a>

## 实际能力

下面按能力展示策略编译后的结果。请运行 `sandbox_audit` 查看当前主机的情况。在使用 claybin 的 Linux 上，各项隔离机制均可用：

```
=== everything the pane can ask for
  filesystem.read        strong  mount-ns
  filesystem.write       strong  mount-ns
  filesystem.exec        strong  landlock
  network.isolation      strong  netns+loopback
  process.isolation      strong  userns+pidns
  syscall.filter         strong  seccomp-bpf
  resource.memory        strong  cgroup2 memory.max
  resource.cpu           strong  cgroup2 cpu.max
  resource.pids          strong  cgroup2 pids.max
  privilege.drop         strong  no_new_privs+empty bounding set
  device.isolation       strong  dev allowlist
  host.kernel_isolation  NONE    process-backend, kvm available
```

在 macOS 上，同一策略有多项能力无法实现，报告会说明具体限制。以下是在 darwin 24 上将面板中各项设置调至最高后，实际运行 `sandbox_audit` 得到的输出：

```
host: darwin=24 seatbelt=1
      sbpl: paths=1 net=1 proc=1
      rlimits=1
      hypervisor=1  (a microvm backend COULD run here)

=== everything the pane can ask for
  filesystem.read        partial  seatbelt
  filesystem.write       partial  seatbelt
  filesystem.exec        partial  seatbelt
  network.isolation      strong   seatbelt (deny network*)
  process.isolation      partial  seatbelt (inherited profile, shared pid space)
  syscall.filter         partial  seatbelt operations (no syscall filter on macOS)
  resource.memory        partial  RLIMIT_AS (per-process)
  resource.cpu           partial  RLIMIT_CPU (per-process)
  resource.pids          advisory RLIMIT_NPROC (per-UID, not per-tree)
  privilege.drop         partial  seatbelt profile survives exec (no user namespace)
  device.isolation       partial  seatbelt /dev path filters
  host.kernel_isolation  NONE     shared host kernel (use a vm for isolation)
```

表中有三项需要仔细理解，因为报告在这些地方明确保留了能力限制：

- **`network.isolation: strong`**：这是 macOS 上与 Linux 一致的一项。
  `(deny network*)` 是内核精确执行的单条规则；就沙箱内进程可访问的网络而言，它等效于空网络命名空间。注意，只有在此处**关闭网络**时才报告 `strong`。端点允许清单只能报告 `partial`，因为 seatbelt 按地址匹配，而域名到地址的解析发生在用户空间，下一次 DNS 可能返回不同结果。
- **`resource.pids: advisory`**，而不是 `partial`。这一用词有明确原因：
  `RLIMIT_NPROC` 统计整个 UID 下的进程，其他终端窗口也会影响限额。它是沙箱内进程通常会遵守的数值限制，无法构成独立、严格的隔离边界，因此标为 `advisory`。
- **`filesystem.*: partial`**：原因并非 seatbelt 不够强，而是
  agentty 的文件系统布局包含 `proc_fs`、`dev_fs`、`tmpfs` 和路径遮蔽。单靠访问控制无法精确表达这些目录树语义。例如 tmpfs 只能近似为“允许在此写入”，却失去了“初始为空”和“容量受限”的保证。因此方案的还原程度标为 `approximate`，能力等级上限也随之降低。只使用相同路径映射的策略会报告 `strong`。

按定义，**所有**进程沙箱后端的 `host.kernel_isolation` 都是 `none`，因为沙箱内命令与宿主共享内核。微虚拟机需要单独的执行后端。当前的 [claybin 适配器](../src/tool/util/sandbox_claybin.cpp) 和[可运行的审计程序](../tests/sandbox_audit.cpp) 展示了已实现的边界。报告会注明 `kvm available` 或 `no kvm`，明确说明这一差距。

<a id="the-honesty-rule"></a>

### 如实报告能力的原则

面板只声明实际构建的隔离能力：

- 副标题通过主机探测描述**当前实际运行的状态**，
  而不是根据刚刚调整的选项推断。
- 后端或内核无法执行的选项会**锁定并显示原因**，
  保留可见，方便用户了解限制。
- 每一项都标注实际执行机制，例如 `strong via landlock abi 10`，
  让能力等级有可核实的依据。
- 尚未运行的引擎所提供的隔离能力标记为 `(on restart)`（重启后生效）。

---

<a id="secret-masking"></a>

## 敏感文件屏蔽

无论是否配置，凭据文件都会被遮蔽，即在其上绑定挂载空文件。这是始终启用的保护；若一个控制项能为了让构建通过而随意关闭，它就无法提供可靠保护。

遮蔽规则来自以下三处，按用户可干预程度由低到高排列：

1. **`kAlwaysMasked`**：`$HOME` 下的凭据路径，包括 `.ssh`、`.aws`、`.gnupg`、
   `.kube`、`.docker/config.json`、`.config/gh`、`.config/gcloud`、`.azure`、`.netrc`、`.npmrc`、`.pypirc`、`.cargo/credentials`、`.git-credentials` 以及 agentty 自己的凭据存储。不可配置。
2. **`kAlwaysMaskedNames`**：凭据文件的**基本文件名**，通过遍历工作区
   至 `mask_scan_depth` 指定的深度查找。除扫描深度外，不可配置。
3. **`deny_paths`**：在面板的 **Masked（遮蔽）** 一项中添加的路径。
   这些规则最后应用，确保内置规则不会撤销用户显式设置的拒绝规则。

<a id="why-the-walk-exists"></a>

### 遍历检查的必要性

仅有基本文件名还无法生成挂载，必须先找到文件所在位置。只遮蔽 `<workspace>/.env` 会漏掉 `services/api/.env`，而后者在单体仓库中很常见。

遍历有**深度上限**（`mask_scan_depth`，默认为 3），因为每次启动进程都会执行扫描。若无限遍历大型源码树，每条 shell 命令都会承受完整目录扫描的延迟。扫描跳过 `.git`、`node_modules`、`target`、`build` 和 `.venv`：这些目录通常很大，也不应是存放凭据的位置。扫描不会跟随目录符号链接。

<a id="name-rules-vs-suffix-rules"></a>

### 名称规则与后缀规则

默认按基本文件名精确匹配，例如 `.env`、`.npmrc`、`id_ed25519`、`credentials.json`。只有以 `*` 开头的条目才按后缀匹配，例如 `*.pem`、`*.tfvars`。

匹配方式由**显式标记**决定。如果根据“以点开头且没有第二个点”之类的形状推断规则，就会悄悄把 `.env` 和 `.npmrc` 变成后缀规则，使 `prod.env` 和 `scoped.npmrc` 也被遮蔽。这样添加无关条目就可能改变其他匹配结果。意外扩大安全规则的范围和缩小范围一样有问题，而且审查清单时很难发现。

---

<a id="syscall-brokering"></a>

## 系统调用代理

沙箱除了**过滤**系统调用，还会对其中两种调用**根据实参在运行时作出决定**。`ptrace` 和 `kill` 通过 seccomp 用户通知进行代理裁决：允许进程对自身执行 `PTRACE_TRACEME`，拒绝对 pid 1 执行 `PTRACE_ATTACH`，两种情况下进程都能继续运行，不会被直接终止。

这样，沙箱就能提供细粒度的能力控制：判断“能否对**这个** pid 执行 ptrace”，而不只是判断“能否执行 ptrace”。

<a id="why-the-broker-is-small-and-must-stay-small"></a>

### 代理需要保持精简的原因

seccomp 用户通知在 Linux 沙箱机制中极易误用，内核文档也明确指出了这一点。这里存在两种结构性风险：

1. **指针参数的检查与使用之间存在竞态（TOCTOU）。** 通知携带的是系统调用的
   **寄存器值**。指针指向沙箱内存，而沙箱内还有其他线程，可能在监督进程读取参数后、内核执行调用前改写内存。因此，从沙箱内读取路径后再放行调用会形成漏洞。
2. **`CONTINUE` 不能代替安全裁决。** 监督进程回复后，系统调用会重新执行，
   此前的检查结果却可能已经过时。

claybin 同时避开这两类问题：裁决仅允许放行或拒绝，请求也不携带路径字段。**策略只依据标量值作出决定。** 需要路径的规则应交给 Landlock，由内核解析路径。

只有同时满足以下三个条件的系统调用才适合代理裁决：可以用标量参数作出决定；一律拒绝在某些情况下不正确；一律允许在某些情况下也不正确。因此，目前只有两种调用需要这样处理。

---

<a id="the-trust-handoff-gate"></a>

## 信任交接检查

沙箱约束的是**进程**。它看不到实践中尤其重要的一条逃逸路径：

> 代理在获准写入的工作区中创建一个普通文件，
> 而沙箱**外部**的程序稍后执行这个文件。

例如 `.vscode/tasks.json`、Git 钩子、虚拟环境解释器或 `.envrc`。当 VS Code 执行该文件时，沙箱已经无法参与约束。Pillar Security 在 2026 年 7 月的一系列研究中，针对 Cursor、Codex、Gemini CLI 和 Antigravity 复现了此类问题；几乎所有案例都无需代理突破进程沙箱。

因此，防护设置在**工具调用**上，在写入字节前检查 `write`、`edit`、`apply_patch` 和 `move` 四种文件修改工具。对于 `move`，关键是**目标路径**：把原本无害的文件移动到会被执行的位置，正是攻击手法。

| 策略 | 行为 |
|---|---|
| `refuse`（默认） | 调用失败，消息说明触发的机制和替代做法 |
| `warn` | 允许写入，并在事件流中记录 |
| `allow` | 不执行拦截 |

默认采用 `refuse`，因为这里需要明确的边界控制。漏拦一次可能造成逃逸；误拦一次则只是拒绝一个工具调用，并告诉模型可以如何替代。

事件流也记录**获准的**信任交接。如果只记录拒绝事件，用户关闭拦截后，事件流恰好会变为空白，而此时更需要看清代理写入了什么。

`--sandbox off` 也会关闭这一防护。关闭就意味着完全关闭。

---

<a id="telling-the-model"></a>

## 向模型说明限制

如果模型不知道自己处于沙箱中，就会**误判遇到的隔离限制**：把 `EPERM` 解释成“公司代理阻止了访问”，把连接被拒绝解释成“请检查防火墙”。代理随后可能让用户排查不存在的问题，或反复尝试内核永远不会允许的调用。

为此提供两种机制，第二种尤为关键：

1. **系统提示词**会在 `<environment>` 中增加一段说明，与操作系统和
   shell 信息并列，作为同一类环境事实。说明会明确哪些操作**被允许**，而不仅仅列出禁止项；只给拒绝清单容易诱导模型试探边界，明确可写位置则有助于模型第一次就选对工作目录。
2. **为错误输出附加解释。** 当出现 `Permission denied` 时，系统提示词通常已经相隔数千个 token，
   模型注意的是眼前的命令输出。如果失败输出符合沙箱限制的特征，会附上 `[sandbox]` 说明，指出原因，并要求避免两种错误做法：原样重试，以及归咎于防火墙。

仅在命令失败且输出匹配时附加说明。若每条命令都带上相同提示，模型会逐渐忽略它，使解释失去作用。

---

<a id="why-the-policy-is-sealed"></a>

## 策略为何在启动后固定

面板把配置写入磁盘，供**下次**启动使用；重启才是应用配置的步骤。agentty 的其他设置会即时生效，沙箱策略是例外。

会话中途可调整的边界，往往只会为了完成操作而逐步放宽。即使随后收紧，也无法撤回已经泄露的密钥。此外，这会让沙箱难以审计：同一会话中的两条命令可能面对不同边界，却没有记录说明各自使用了哪套规则。

面板会说明这一点，并用检查验证重启后的结果。`restart_outcome()` 会报告下次启动**无法**实现面板设置的情况：

| 状态 | 页脚提示 |
|---|---|
| 无异常 | `applies on restart, verified against this host`（重启后生效，已根据当前主机验证） |
| 无法编译策略 | `will NOT apply on restart · <reason>`（重启后不会生效，并说明原因） |
| 引擎拒绝启动 | `claybin cannot start on this host · commands will run unconfined`（claybin 无法在此主机启动，命令将不受沙箱约束） |
| 能力降级 | `applies on restart, but this host cannot enforce: …`（重启后生效，但当前主机无法执行所列限制） |

---

<a id="threat-model"></a>

## 威胁模型

**能够阻止：** 已获批准的命令读取 `~/.ssh/id_rsa`、`~/.aws/credentials` 或工作区中的 `.env`；在工作区之外写入；创建 fork 炸弹；耗尽宿主内存；跟踪其他进程；映射同时可写可执行的页面；在禁用网络时访问网络；即使网络开启，访问宿主的回环服务、局域网、云元数据地址（169.254.169.254）或 tailnet；以及代理创建稍后会由宿主执行的文件。

**无法阻止：**

- **向公网外传数据，除非设置了主机清单。** 开启网络且
  **Allowed hosts** 为空时，命令只要能读取数据，也就能把数据发送到任意公网地址。代理会拒绝私有网段，因此攻击范围不包括用户自己的内网机器。设置[允许的主机](#allowed-hosts)后，只能向这些名称发送数据，但仍需考虑清单中的主机可能提供转发功能。这也是默认可读范围较小，并提供 `airgapped` 预设的原因。
- **代理本身的缺陷。** 拒绝私有网段的规则由 agentty 中的策略代码执行，
  其有效性取决于代码是否正确。**内核**保证的是更底层的边界：没有监督进程传入的文件描述符，沙箱就没有离开自身回环网络的路径。因此，此处发生故障时会默认阻断连接，而不会意外开放网络。
- **位于 `mask_scan_depth` 之外或被跳过目录中的秘密文件。** 扫描上限
  可以配置，相关取舍由用户决定。
- **尚未收录的凭据文件名。** 如果秘密保存在 `config.local.yaml` 中，
  仅靠现有基本文件名清单无法识别。
- **内核级逃逸。** 命令与宿主共享内核。内核本地提权漏洞可突破
  所有进程沙箱后端，这正是 `host.kernel_isolation: none` 所表达的限制。
- **agentty 自身的 API 流量。** 这些请求在应用进程内发起，不经过进程沙箱边界。

---

<a id="verifying"></a>

## 验证方法

验证分为三个层次：

| 验证项 | 能证明什么 | 运行方式 |
|---|---|---|
| `sandbox_audit` | 按能力检查当前主机实际执行的限制 | `cmake --build build --target sandbox_audit && ./build/sandbox_audit` |
| `sandbox_live_check` | 启动真实命令并读取结果，验证子进程实际受到的限制 | `cmake --build build --target sandbox_live_check && ./build/sandbox_live_check` |
| `exec_exit_probe` | 通过实际发布的进程启动路径运行任意命令清单 | `./build/exec_exit_probe /tmp/ws on 0 cmds.txt` · `PROBE_DUMP=1` 输出沙箱参数 · `PROBE_ALLOW_HOSTS=a,b` 设置主机清单 |
| ctest 测试套件 | 检查配置表和状态更新函数的各项性质，并通过真实重启验证全部预设 | `ctest --test-dir build -R 'sandbox\|netproxy'` |

之所以增加真实子进程检查，是因为“方案声明了某项限制”与“子进程实际受到该限制”之间往往隐藏着缺陷。系统调用规则曾漏掉 `mremap`，导致机器内存充足时 `curl` 仍报“内存不足”：配置表层面的测试全部通过，但沙箱内的 `realloc()` 却失败了。

启动横幅会说明后端，信息来自实际能力探测：

```
agentty: sandbox: active (claybin)
```

---

<a id="for-contributors"></a>

## 贡献者须知

请先阅读[配置类型](../include/agentty/domain/sandbox_config.hpp)、[执行边界](../src/tool/util/sandbox.cpp)和 [claybin 适配器](../src/tool/util/sandbox_claybin.cpp)。最关键的规则如下：

- **只能在启动阶段调用 `set_config`。** 如果需要在其他地方调用，
  应修改设计，而不是增加调用点。
- **功能尚未完整接通时，不要提前声明控制项。** 未实现功能的行标识、结构体
  或消息会让后续开发者误以为功能已经可用。这曾造成三类缺陷：`kSbMode`、连续数月没有真正执行限制的信任交接选项，以及先于生产方存在的两种事件流类型。
- **面板每新增一项，都必须在各后端能力表中补上对应条目和锁定原因。**
  如果无法准确说明某个后端的实际行为，这一项就尚未准备好。
- **所有描述当前状态的文字都应来自 `HostFacts`，
  而不是 `Config`。** 配置表达用户**想要的状态**，只有 `HostFacts` 才能说明用户**实际拥有的能力**。
- **在子进程中验证限制确实生效。**
