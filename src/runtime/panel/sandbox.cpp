// agentty::ui::panel::Sandbox — 形成构造, 重回,
// 以及活墙预览。
//
// 形式由配置和 HOST 能力,因为每行提供
// -port 内核上无地锁的网络 ABI 4是谎言。
// 这里无法执行的行会按原因锁定, 而不是隐藏 :
// 隐藏它们使得限制变得无形,这就是"sandbox: active"是
// 如何在21号问题中毫无意义的.

#include "agentty/runtime/panel/sandbox.hpp"
#include "agentty/runtime/panel/mention.hpp"   // file_source/file_filter,与@共享.
#include "agentty/i18n/i18n.hpp"              // t() — 行标签和帮助
#include "agentty/tool/util/sandbox_netproxy.hpp"   // normalize_host_entry 一条规则 面板 和代理服务器

#include <algorithm>
#include <string>
#include <unordered_set>   // path_source( ) 中的目录解禁

// claybin 需要一个 子模块 留下的警卫是 PLATFORM 仅限:
// compile()是可移植的,因此墙面预览在任意主机上都有效.
//
// POSIX 而不是 Linux 现在 macOS 也适用
// claybin 政策。 这条守护控制线 -- -- 系统调用配置、
// per-port 网络、 资源上限、 额外路径 授权 -- -
// - 是 SETTINGS , 并且只在 claybin
// 下方的平台下有意义。 将其留在 Linux 意味着 面板 静静
// 地将每个平台都丢弃: 该政策仍然被编译并被执行, 但用户没有
// 行可看到或更改它。
#if defined(__linux__) || defined(__APPLE__) || defined(_WIN32)
#include "claybin/policy/policy.hpp"
#include "claybin/policy/profiles.hpp"
#if defined(__APPLE__)
#include "claybin/macos/backend.hpp"
#elif defined(_WIN32)
#include "claybin/windows/backend.hpp"
#else
#include "claybin/plan/compile.hpp"
#endif
#include "agentty/tool/util/sandbox_claybin.hpp"   // host():缓存探测器
#endif

namespace agentty::ui::panel {

namespace {

form::Field header(std::string label) {
    form::Field f;
    f.label = std::move(label);
    f.value = form::field::Header{};
    return f;
}

// 挑一个 -one 划线
//
// `hints` 单位 -OPTION 和您可以操作的行与您
// 必须理解的行之间的区别。帮助行解释了行是什么;提示解释了您
// 正在查看的选项 执行 ,在您骑着它转弯时。 面板 输入
// agentty 大部分原因就是 被他们送进监狱 枚举 行读作
// 词组 : "toolchain / minimal / host-readable"
// 名字三件事,不说其中的任何一个
// 授权.
form::Field choice(std::string_view id, std::string label, std::string help,
                   std::vector<std::string> labels, int index,
                   std::vector<std::string> hints = {}) {
    form::Field f;
    f.id = std::string{id};
    f.label = std::move(label);
    f.help = std::move(help);
    form::field::Choice c;
    c.labels = std::move(labels);
    c.hints = std::move(hints);
    c.index = index;
    f.value = std::move(c);
    return f;
}

// 排行数 OPENS 列表编辑器,而不是持有逗号 -separated 弦乐。
//
// 以条目计数而不是内容表示: 行的工作是说列表
// 是否为空并让你进入,一个狭小的列中三条路径的
// 短短预览显示您在看起来像在告诉你更多时,
// 没有"3 paths"做.
form::Field list_row(std::string_view id, std::string label, std::string help,
                     std::size_t count, const char* noun) {
    form::Field f;
    f.id = std::string{id};
    f.label = std::move(label);
    f.help = std::move(help);
    form::field::Pick p;
    p.label = count == 0
        ? std::string{}
        : std::to_string(count) + " " + noun + (count == 1 ? "" : "s");
    p.placeholder = "none \xc2\xb7 Enter to add";
    f.value = std::move(p);
    return f;
}

form::Field text(std::string_view id, std::string label, std::string help,
                 std::string value) {
    form::Field f;
    f.id = std::string{id};
    f.label = std::move(label);
    f.help = std::move(help);
    form::field::Text t;
    t.value = std::move(value);
    f.value = std::move(t);
    return f;
}

// 一条布尔的线。
//
// `when_on` / `when_off` 是那个
// 切换 选择的答案是 -option 提示:帮助线描述的是 当前 州有,
// 你翻的时候会翻转 切换 渲染 只不过是 on/off ,
// 所以没有这个,唯一能知道 关闭它的成本是 关闭它,
// 然后发现—— 在安全控制上, 哪个是错误的回合。
//
// 被形容为是结果,而不是重述. "off: no filter" 教无所教;
// "off: a JIT works, and a bug can rewrite its own code" 是实际交易。
form::Field toggle(std::string_view id, std::string label, std::string help,
                   bool on, std::string when_on = {}, std::string when_off = {}) {
    form::Field f;
    f.id = std::string{id};
    f.label = std::move(label);
    const auto& state = on ? when_on : when_off;
    f.help = state.empty()
        ? std::move(help)
        : (help.empty() ? state : help + " \xc2\xb7 " + state);
    form::field::Toggle t;
    t.on = on;
    f.value = std::move(t);
    return f;
}

// 数字是一英寸64,有范围,没有单位字段——单位属于标签,
// 这也是其他面板所放置的单位.
form::Field number(std::string_view id, std::string label, std::string help,
                   std::int64_t value, std::int64_t lo, std::int64_t hi) {
    form::Field f;
    f.id = std::string{id};
    f.label = std::move(label);
    f.help = std::move(help);
    form::field::Number n;
    n.value = value;
    n.min = lo;
    n.max = hi;
    f.value = std::move(n);
    return f;
}

// 锁 列数 BY ID ,抬头看 调用 时间
//
// 电话 面板 整个说法是 它告诉你真相 是什么,
// 所以一行提供 墙内核不能建造 将抵消这一点。
//
// 而不是通过参考,即 加载-bearing
// 而不是风格: push_back 资源
// 行将错误的准确运出 -- 四个引用绑定,
// 三个再推,然后全部锁定,所以前三个写入
// 解禁 内存 和行数 UNLOCKED.
//
// 失败的最好方向是:
// "this setting applies" ,
// 而整个这一点 面板 无法进行身份检查,
// 所以危险是建筑而不是精心命令的。
void lock_row(form::Form& f, std::string_view id, std::string_view reason) {
    for (auto& fld : f.fields) {
        if (fld.id != id) continue;
        fld.locked = true;
        fld.locked_reason = std::string{reason};
        return;
    }
}

}  // 命名空间结束。

EngineStatus engine_status(const sandbox_cfg::Config& cfg, const HostFacts& facts) {
    const bool wants_claybin = cfg.backend == sandbox_cfg::LinuxBackend::Claybin;
    // Host 拒绝排在比较的后方 : claybin 无法从这里开始,
    // "applies on restart" 承诺是不会被任何东西所遵守的
    if (wants_claybin && !facts.claybin_available) return EngineStatus::CannotStart;
    if (!facts.sandbox_active) return EngineStatus::AppliesOnRestart;
    return cfg.backend == facts.running ? EngineStatus::InForce
                                        : EngineStatus::AppliesOnRestart;
}

std::string restart_outcome(const sandbox_cfg::Config& cfg,
                            const HostFacts& facts,
                            const Preview& preview) {
    // 订的最差的 -first ,因为脚印显示 一个 线条,
    // 用户需要最能使承诺无效的东西。
    // 编译 使无法执行的列表变得无关紧要;
    // 无法启动的引擎使 编译 结果无关紧要。

    // 1. 不存在 编译. claybin 拒绝而不是贬低,所以那里
    //    无效 "partly" 接下来的发射没有从这里得到任何政策。
    if (!preview.compiled)
        return "will NOT apply on restart \xc2\xb7 " + preview.error;

    // 2. 引擎不能从这里开始。 保存仍然合法( 用户 )
    //    可能明天在另一台机器上, 配置是可移植
    //    的) 但承诺必须命名 回退 而不是暗示下面的
    //    墙会升起。
    if (engine_status(cfg, facts) == EngineStatus::CannotStart)
        return "claybin cannot start on this host \xc2\xb7 commands will run "
               "unconfined";

    // 3. 国家 编译 开始,但有些东西 出比要求的更弱。
    //    这就是静态的例和函数存在的原因:行
    //    描述一个边界,重启时会交付大部分,
    //    不用说,所以用户相信沉默退化的部分.
    if (!preview.unenforceable.empty()) {
        std::string out = "applies on restart, but this host cannot enforce: ";
        for (std::size_t i = 0; i < preview.unenforceable.size(); ++i) {
            if (i) out += ", ";
            out += preview.unenforceable[i];
        }
        return out;
    }

    // 空:重启会如上所述地执行政策,
    // 脚步可以明确做出承诺。
    return {};
}

std::string describe_running(const HostFacts& facts) {
    // 读取 仅 `facts`。选中的引擎故意不是参数 :
    // 此函数回答"what is confining commands right now",
    // 而它存在的防止错误恰恰是用户刚刚移动的
    // 行中回答的问题。
    if (facts.mode_off)
        return "sandboxing is OFF (--sandbox off) \xc2\xb7 nothing below is enforced";
    if (!facts.sandbox_active)
        return "no sandbox \xc2\xb7 commands run unconfined";
#if defined(_WIN32)
    // landlock/seccomp/cgroup2是linux单词; 名称是窗口所设置的。
    return "claybin \xc2\xb7 appcontainer \xc2\xb7 job limits \xc2\xb7 mitigation policies";
#else
    return "claybin \xc2\xb7 landlock abi " + std::to_string(facts.landlock_abi) +
           " \xc2\xb7 seccomp \xc2\xb7 cgroup2";
#endif
}

form::Form build_sandbox_form(const sandbox_cfg::Config& cfg,
                              const HostFacts& facts) {
    const bool claybin_available = facts.claybin_available;
    const std::uint32_t landlock_abi = facts.landlock_abi;
    form::Form form;
    form.title = "Sandbox";

    // 1引擎:claybin. 下方每行只有在主机可以启动时
    // 才能工作.
    const bool claybin_live = claybin_available;
    const std::string why_locked{"claybin cannot start on this host"};

    // 字幕是那个 -line 答复( E) "what am I protected by RIGHT NOW" ,
    // 这就是一个问题 面板 存在的原因 问题
    // 就错了
    //
    // 由是建于东汉 `facts` 单枪匹马,从不从选中的引擎中找到。旧版本使用
    // `on_claybin && claybin_available` ,
    // 所以移动行 claybin 立即重新油漆,以声称 seccomp + 地锁+
    // cgroup2 正在运行的 brap 进程,无法在中间更改 -session
    // 是因为保险是启动时封存的。
    // "sandbox: active while nothing is enforced" 整个失败 面板 是在后来建造的。
    //
    // 当选择和现实有分歧时,分歧在这里陈述,
    // 而不是以赞成其中之一的方式解决:用户需要两种事实,
    // 以及哪种事实.
    form.subtitle = describe_running(facts);
    switch (engine_status(cfg, facts)) {
        case EngineStatus::InForce:
            break;   // 选择符合现实; 无可拒绝声明
        case EngineStatus::AppliesOnRestart:
            form.subtitle += "  \xc2\xb7  selected: ";
            form.subtitle += sandbox_cfg::to_string(cfg.backend);
            form.subtitle += " (applies on restart)";
            break;
        case EngineStatus::CannotStart:
            form.subtitle += "  \xc2\xb7  claybin cannot start here";
            break;
    }

    // ── 姿势 ───────────────────────────────────────────────────
    //
    // 第二行,直接放在引擎下,因为这是用户实际存在的问题。
    // -eight 个人 -correct 开关还是要交人
    // 不对 "tighter than this" -
    // 没有人能解释的控制权是没有人触及的控制权,对于安全环境来说,
    // 这意味着它已经关闭。
    //
    // 它可以写它,但它不是REPLACE。下面的一切都
    // 保持可见和可编辑,所以这是一个起点,而不是模式:
    // 预设的一瞬间隐藏了细节,它将会是另一个"sandbox: active"
    // ——一个标注在一块你再也检查不下去了的边界上.
    //
    // `Custom` 名单里有你选择的,但从来不是东西:
    // REPORTS 当配置匹配无预设时, 编辑 Hardened
    // 的一行 面板 选择它是一个不 -op 建筑工程(
    // apply_posture 返回未调整配置。
    const auto posture = sandbox_cfg::detect_posture(cfg);
    form.fields.push_back(header("Posture \xe2\x80\x94 a preset for every row below"));
    form.fields.push_back(choice(
        kSbPosture, "Preset",
        // 说出控制 IS 之前,它的成本。 "Posture"
        // 是一个词, 面板 使用时从未定义,而一个设置行,
        // 其标签你必须已经理解的是排人跳过。
        posture == sandbox_cfg::Posture::Custom
            ? std::string{"your own mix \xc2\xb7 pick one to overwrite every row below"}
            : std::string{"sets every row below in one go \xc2\xb7 "} +
                  sandbox_cfg::cost_of(posture),
        {"permissive", "balanced", "hardened", "airgapped", "custom"},
        static_cast<int>(posture),
        {"mount walls only \xc2\xb7 no syscall filter, no limits \xc2\xb7 for a "
         "toolchain that breaks under one",
         "the default \xc2\xb7 network on, compiler syscall profile, fork-bomb cap",
         "every wall claybin can build \xc2\xb7 limits a real build still survives",
         "hardened + no network \xc2\xb7 nothing can send your code anywhere",
         "not a preset \xc2\xb7 what the pane shows once you edit a row yourself"}));

    // ── 文件系统 ───────────────────────────────────────────────────────
    // 这四个是行 bwrap CAN 荣誉: 范围,
    // 额外读写和额外写作都是绑定的, 面具是一条空文件绑在路径上的绑定。
    // 因此,它们只能活在两个后端上 — 把它们绑在另一方向
    // 上将是一个谎言。
    //
    // 蒙面被锁在bwrap下面,直到bwraP路径学会了发射面具;
    // 它完全没有应用,所以行没有配置任何内容。现在两个后端都尊重
    // 它(见build_bwrap_argv)。
    form.fields.push_back(header("Filesystem"));
    form.fields.push_back(choice(
        kSbFsScope, "Readable scope",
        "what a command can READ. the workspace is always readable and "
        "writable; this is everything else.",
        {"toolchain", "minimal", "host-readable"},
        static_cast<int>(cfg.fs_scope),
        {"/usr, /bin, /lib, /etc + ~/.cargo, ~/.npm and friends \xc2\xb7 what a "
         "build needs",
         "system dirs only \xc2\xb7 no $HOME toolchains, so some builds break",
         "read on / \xc2\xb7 every secret outside the mask list is reachable"}));
    form.fields.push_back(
        list_row(kSbReadPaths, std::string{i18n::t("sandbox.read_paths")},
                 std::string{i18n::t("sandbox.help.read_paths")},
                 cfg.read_paths.size(), "path"));
    form.fields.push_back(
        list_row(kSbWritePaths, std::string{i18n::t("sandbox.write_paths")},
                 std::string{i18n::t("sandbox.help.write_paths")},
                 cfg.write_paths.size(), "path"));
    form.fields.push_back(
        list_row(kSbDenyPaths, std::string{i18n::t("sandbox.deny_paths")},
                 std::string{i18n::t("sandbox.help.deny_paths")},
                 cfg.deny_paths.size(), "path"));

    // ── 网络 ──────────────────────────────────────────────────────────
    form.fields.push_back(header("Network"));
    form.fields.push_back(choice(
        kSbNetMode, "Access",
        "whether a command can reach the network at all.",
        {"full", "none", "ports"}, static_cast<int>(cfg.net_mode),
        {"shares the host network \xc2\xb7 git push and npm install work, and so "
         "does exfiltration",
         "an empty network namespace \xc2\xb7 nothing can connect out",
         "only outgoing TCP to the ports below \xc2\xb7 DNS and other UDP are "
         "not filtered \xc2\xb7 no listening sockets \xc2\xb7 needs landlock abi 4+"}));
    // 人数 -port 需要锁定 ABI 4 关 Linux ,
    // 安全带端点过滤器 macOS 没有它,就说,而不是接受一个无所事事的场景。
    if (!claybin_live)
        lock_row(form, kSbNetMode, why_locked);
    else if (!facts.per_port_net)
        lock_row(form, kSbNetMode,
                 landlock_abi > 0
                     ? "per-port needs landlock abi 4, host has " +
                           std::to_string(landlock_abi)
                     : std::string{"this host cannot filter network by port"});

    form.fields.push_back(
        list_row(kSbPorts, std::string{i18n::t("sandbox.ports")},
                 std::string{i18n::t("sandbox.help.ports")},
                 cfg.allow_ports.size(), "port"));
    // 故意的 不 访问不时锁定 `ports`.
    //
    // 曾经是,根据一个理由,一个端口列表 意味着什么在下面
    // `full` 或 或 时 间 `none`。没错,
    // 它使这行无法到达: lock_row 制造 activate()
    // 什么都不还,所以 Enter 用户必须知道首先设置 Access,
    // 因为它要他们这样做, 但不会让他们采取行动。
    //
    // 故行住住住住,而无不为. EDITOR 任务时修正
    // 模式: 在访问不时添加端口 `ports` 切换它,
    // 因为添加一个端口是明确的意图声明。 帮助线说到这一点,
    // 而不是让它被发现。
    if (cfg.net_mode == sandbox_cfg::NetMode::Full)
        form.fields.back().help =
            "adding a port here switches Access to `ports` \xc2\xb7 "
            "443 https \xc2\xb7 80 http \xc2\xb7 22 git-ssh";
    else if (cfg.net_mode == sandbox_cfg::NetMode::None)
        form.fields.back().help = "only used when Access is `ports`";

    // 允许的主机:名称 允许清单 使用量=完全,由 沙箱 代理。
    // 基于同样的原因, 在所有模式中生活 Ports
    // 是: 无法打开的行是无人能填充的行。 帮助行名列表
    // IN 该 当前 MODE ,因为用户是这样看待的。
    form.fields.push_back(
        list_row(kSbHosts, std::string{i18n::t("sandbox.hosts")},
                 std::string{i18n::t("sandbox.help.hosts")},
                 cfg.allow_hosts.size(), "host"));
    if (cfg.allow_hosts.empty())
        form.fields.back().value = [] {
            form::field::Pick p;
            p.placeholder = "any public host \xc2\xb7 Enter to restrict";
            return p;
        }();
    if (cfg.net_mode == sandbox_cfg::NetMode::None)
        form.fields.back().help = "unused while Access is none \xc2\xb7 nothing is reachable";
    else if (cfg.net_mode == sandbox_cfg::NetMode::Ports)
        form.fields.back().help = "unused while Access is ports \xc2\xb7 set Access to full "
                                  "to filter by name";

    // ── 响铃 ─────────────────────────────────────────────────────────
    form.fields.push_back(header("Syscalls"));
    form.fields.push_back(choice(
        kSbSyscalls, "Filter",
        "which syscalls a command may make. the filter also decides ptrace and "
        "kill at runtime rather than killing the process.",
        {"off", "compiler", "strict"}, static_cast<int>(cfg.syscall_mode),
        {"no filter, and no ptrace/kill supervision either",
         "denies ptrace, mount, unshare, bpf and filters clone flags \xc2\xb7 "
         "what a compiler needs",
         "compiler, plus no sockets when Access is none"}));
    if (!claybin_live)
        lock_row(form, kSbSyscalls, why_locked);

    form.fields.push_back(
        toggle(kSbWxProtect, "W^X",
               "no page may be writable and executable at the same time",
               cfg.wx_protect,
               "on: a single writable+executable mapping is refused \xc2\xb7 "
               "writing a page and then making it executable still works \xc2\xb7 "
               "breaks JITs that ask for RWX (some node flags, any JVM)",
               "off: JITs work \xc2\xb7 a memory bug can map writable+executable "
               "memory directly"));
    if (!claybin_live) lock_row(form, kSbWxProtect, why_locked);
    // 每个调用配置文件都否认 RWX ,所以,请 切换 过滤器
    // 关闭时才重要
    else if (cfg.syscall_mode != sandbox_cfg::SyscallMode::Off) {
        lock_row(form, kSbWxProtect, "always on while the syscall filter is on");
        if (auto* t = std::get_if<form::field::Toggle>(&form.fields.back().value))
            t->on = true;
    }

    // ── 资源 ─────────────────────────────────────────────────────
    // 四个都是 cgroup2 (以rlip作为后盾) memory/procs ),
    // 以及 agentty 过brap 完全没有组合。
    // 这些是行 面板 已运出 UNGATED 他们活在包里,却一无所获,
    // 这正是失败的真相 面板 已经存在,以预防。
    form.fields.push_back(header("Resources"));
    form.fields.push_back(
        number(kSbMemoryMb, std::string{i18n::t("sandbox.memory")},
               std::string{i18n::t("sandbox.help.memory")},
               static_cast<std::int64_t>(cfg.memory_mb), 0, 131072));
    form.fields.push_back(
        number(kSbMaxProcs, std::string{i18n::t("sandbox.procs")},
               std::string{i18n::t("sandbox.help.procs")},
               static_cast<std::int64_t>(cfg.max_procs), 0, 65536));
    form.fields.push_back(
        number(kSbCpuPercent, "CPU (%)", "0 = no cap \xc2\xb7 100 = one core",
               static_cast<std::int64_t>(cfg.cpu_percent), 0, 1600));
    form.fields.push_back(
        number(kSbTmpMb, "/tmp size (MB)",
               "a runaway build hits ENOSPC inside the sandbox "
               "instead of filling host RAM",
               static_cast<std::int64_t>(cfg.tmp_mb), 16, 65536));
    form.fields.push_back(
        number(kSbOpenFiles, "Open files",
               "0 = no cap \xc2\xb7 a leak exhausts the host's file table "
               "without ever forking, so Processes does not bound it",
               static_cast<std::int64_t>(cfg.max_open_files), 0, 1048576));
    form.fields.push_back(
        number(kSbCpuSecs, "CPU seconds",
               "0 = no cap \xc2\xb7 total compute, so a process that sleeps "
               "forever is untouched",
               static_cast<std::int64_t>(cfg.cpu_secs), 0, 86400));
    form.fields.push_back(
        number(kSbWallClock, "Wall clock (s)",
               "0 = no cap \xc2\xb7 elapsed time, enforced by the sandbox \xc2\xb7 "
               "still applies to a child that ignores SIGTERM",
               static_cast<std::int64_t>(cfg.wall_clock_secs), 0, 86400));
    if (!claybin_live)
        for (const auto id : {kSbMemoryMb, kSbMaxProcs, kSbCpuPercent, kSbTmpMb,
                              kSbOpenFiles, kSbCpuSecs, kSbWallClock})
            lock_row(form, id, why_locked);

    // ── 硬化 ─────────────────────────────────────────────────────
    form.fields.push_back(header("Hardening"));
    form.fields.push_back(toggle(
        kSbScopeIpc, "Scope IPC",
        "abstract unix sockets and cross-boundary signals \xc2\xb7 these ignore "
        "the filesystem entirely, so nothing else here covers them",
        cfg.scope_ipc,
        "on: the command cannot talk to host processes over an abstract "
        "socket or signal them",
        "off: it can reach any listening host process, sandbox or not"));
    // 引擎决定了这一点,而不是用户:总是在有路锁的情况下 ABI
    // 6+或安全带下, 绝对不能下 ABI 6. 列报.
    if (!claybin_live)
        lock_row(form, kSbScopeIpc, why_locked);
    else {
        lock_row(form, kSbScopeIpc,
                 facts.ipc_scoped
                     ? std::string{landlock_abi > 0 ? "always on with landlock abi 6+"
                                                    : "always on under seatbelt"}
                     : "needs landlock abi 6, host has " + std::to_string(landlock_abi));
        if (auto* t = std::get_if<form::field::Toggle>(&form.fields.back().value))
            t->on = facts.ipc_scoped;
    }

    form.fields.push_back(toggle(
        kSbCloseFds, "Close inherited fds",
        "an inherited descriptor is authority the sandbox cannot revoke",
        cfg.close_inherited_fds,
        "on: the command starts with only stdin/stdout/stderr",
        "off: it inherits agentty's open files \xc2\xb7 a handle to a masked "
        "file still reads it"));
    if (!claybin_live) lock_row(form, kSbCloseFds, why_locked);

    // 主持人名. 不 控制控制,帮助说,
    // 而不是让它坐在墙壁之间, 客人不能升级。
    // 因为真正的主机名会泄露到建筑输出和测试中 快照 ,
    // 这使得这些不 -reproducible.
    form.fields.push_back(toggle(
        kSbFakeHost, "Report hostname as `sandbox`",
        "not a wall \xc2\xb7 a privacy and reproducibility setting",
        cfg.fake_hostname,
        "on: builds and test snapshots see `sandbox` instead of your "
        "machine name",
        "off: the real host name appears in build output and any snapshot "
        "that records it"));
    if (!claybin_live) lock_row(form, kSbFakeHost, why_locked);

    // 猎杀的深度 凭据 FILES 名称(.env, id_rsa , *.pem) (中文(简体) ).
    // 适用于两个后端,因为扫描会生成路径列表,
    // 并且两者都懂得如何遮掩路径.
    //
    // 成本行,不寻常的是:每个关卡都更高 stat()
    // 调用 并且扫荡在每一个贝壳命令的空闲路径上。
    // 说这些比选一个数字和隐藏交易要好。
    form.fields.push_back(
        number(kSbMaskDepth, std::string{i18n::t("sandbox.scan_depth")},
               std::string{i18n::t("sandbox.help.scan_depth")},
               static_cast<std::int64_t>(cfg.mask_scan_depth), 0, 8));

    // 信托 -handoff 排队 最后 因为它和
    // 其余的不一样。上面的一切都限制了过程;
    // 这部作品规范了代理商可能交给主机的后期运行,
    // 即2026年7月的"支柱"系列中的逃生课,
    // 代理商从未爆发过, 也无需如此。
    form.fields.push_back(header("Trust handoff"));
    form.fields.push_back(choice(
        kSbHandoff, "Agent writes host-executed files",
        "hooks, .vscode tasks, git config, venv interpreters. the sandbox "
        "cannot see this \xe2\x80\x94 the file runs later, outside it.",
        {"refuse", "warn", "allow"}, static_cast<int>(cfg.handoff),
        {"the write fails with a message telling the agent what to do instead",
         "the write happens and is recorded in the feed below",
         "no gate \xc2\xb7 the shape every escape in Pillar's 2026 series used"}));

    // 由手建,不是由 form::Builder ,因此应用相同的起始
    // 规则 build() 做:光标开始于真实行, 永远不要 头文件.
    form::settle_cursor(form);
    return form;
}

sandbox_cfg::Config read_sandbox_form(const form::Form& f,
                                      const sandbox_cfg::Config& base) {
    sandbox_cfg::Config cfg = base;
    cfg.configured = true;

    auto choice_of = [&](std::string_view id, int fallback) {
        if (const auto* fld = f.find(id); fld && !fld->locked)
            if (const auto* c = std::get_if<form::field::Choice>(&fld->value)) return c->index;
        return fallback;
    };
    // A LOCKED 行是报表,不是输入。多行显示的是 ENGINE
    // 而不是像这个配置那样 IPC 读取 "on" 安全带下,
    // 无论配置如何;W ^X 读取 "on" 当系统调用过滤器打开时,
    // 读取这些显示器会写下引擎的事实 INTO 配置: 选择允许(
    // E) scope_ipc 回来的假象 scope_ipc
    // = 实数,平衡数( wx_protect =false)
    // 重新实现,两个预设都读作自定义。 因此,一个锁定的行会保留
    // 基数——用户不可能更改它,所以没有改变任何内容。
    auto toggle_of = [&](std::string_view id, bool fallback) {
        if (const auto* fld = f.find(id); fld && !fld->locked)
            if (const auto* t = std::get_if<form::field::Toggle>(&fld->value)) return t->on;
        return fallback;
    };
    auto num_of = [&](std::string_view id, std::int64_t fallback) -> std::int64_t {
        if (const auto* fld = f.find(id); fld && !fld->locked)
            if (const auto* n = std::get_if<form::field::Number>(&fld->value)) return form::number_value(*n);
        return fallback;
    };

    // 一台引擎,所以没有读回它。
    cfg.backend = sandbox_cfg::LinuxBackend::Claybin;

    cfg.fs_scope = static_cast<sandbox_cfg::FsScope>(
        choice_of(kSbFsScope, static_cast<int>(base.fs_scope)));

    // 路径和端口LISTS不从表单读回.
    //
    // 这些行现在是 Picks —— 它们显示一个数并打开一个编辑器,
    // 所以表格没有列表数据可以读取。编辑器在进行时直接写入配置( )。
    // SandboxListClose ),以及 `base`
    // 带结果来 解析 选择的标签回到路径 将读取 UI
    // 自己的总结,好像它是模型。
    cfg.read_paths  = base.read_paths;
    cfg.write_paths = base.write_paths;
    cfg.deny_paths  = base.deny_paths;
    cfg.allow_ports = base.allow_ports;
    cfg.allow_hosts = base.allow_hosts;

    cfg.net_mode = static_cast<sandbox_cfg::NetMode>(
        choice_of(kSbNetMode, static_cast<int>(base.net_mode)));

    cfg.syscall_mode = static_cast<sandbox_cfg::SyscallMode>(
        choice_of(kSbSyscalls, static_cast<int>(base.syscall_mode)));
    cfg.wx_protect = toggle_of(kSbWxProtect, base.wx_protect);

    cfg.memory_mb = static_cast<std::uint64_t>(num_of(kSbMemoryMb, 0));
    cfg.max_procs = static_cast<std::uint32_t>(num_of(kSbMaxProcs, 0));
    cfg.cpu_percent = static_cast<std::uint32_t>(num_of(kSbCpuPercent, 0));
    cfg.tmp_mb = static_cast<std::uint64_t>(num_of(kSbTmpMb, 512));
    cfg.max_open_files = static_cast<std::uint32_t>(num_of(kSbOpenFiles, 0));
    cfg.cpu_secs = static_cast<std::uint32_t>(num_of(kSbCpuSecs, 0));
    cfg.wall_clock_secs = static_cast<std::uint32_t>(num_of(kSbWallClock, 0));
    cfg.mask_scan_depth = static_cast<std::uint32_t>(
        num_of(kSbMaskDepth, static_cast<std::int64_t>(base.mask_scan_depth)));
    cfg.fake_hostname = toggle_of(kSbFakeHost, base.fake_hostname);

    cfg.scope_ipc = toggle_of(kSbScopeIpc, base.scope_ipc);
    cfg.close_inherited_fds = toggle_of(kSbCloseFds, base.close_inherited_fds);
    cfg.handoff = static_cast<sandbox_cfg::HandoffPolicy>(
        choice_of(kSbHandoff, static_cast<int>(base.handoff)));

    // 电话 POSTURE 排队是故意的 不 读回这里。
    //
    // 无法,声音。 Readback 仅看到值, 且姿态行的值是配置
    // 的姿态 USED 改为 -- 因此,在用户选择硬化后,
    // 然后编辑 内存 ,行仍然读 "hardened" 配置是真实自定义。
    // 任何推断对等意图的规则都会在某一方向上出错:比较 -and-apply
    // 内容 -stamps 8192 超过用户的2048 下一场
    // 重绘 ,并应用 -always 更糟糕的是,我们没有
    //
    // 应用预设是ACTION,只有状态更新函数知道发生了一个
    // 动作——它有变化的行代号。所以`apply_posture`
    // 被叫做(update/sandbox.cpp,
    // kSbPosture arm),每移动一行,重新背面就保持了
    // 其他行的纯投影。该行的显示值来自建构时的`detect_posture`,
    // 它就是衍生出来的,因此不能漂移。
    return cfg;
}

// 候选人是: 工作区 文件 PLUS 他们的目录。
//
// 文件来自同一来源 `@` 选择面板。所以,
// 一个完成的路径就是存在的路径。目录是从它们中衍生出来的,
// 而不是分开走:已知文件的每个父前缀都是真正的目录,
// 它花费了一个经过我们已有的、而且不能去的列表
// 过期 独立地说
//
// 目录事项 MORE 而不是这里的文件。 "Also readable" 财务报告和已
// 审计财务报表 "Masked" 几乎总是指向 源码树 (`/opt/weird-sdk`,
// `node_modules` ),而一个只提供文件的完整器使得普通案件成为
// 你必须手抄的.
[[nodiscard]] ui::SnapshotSource<std::string> path_source() {
    return {
        .ready = [] { return agentty::files_ready(); },
        .fetch = [] {
            auto files = agentty::list_workspace_files();
            std::vector<std::string> all;
            all.reserve(files->size() * 2);
            // 键 值 ,不通过视图进入 `all`。这些观点将第一次
            // 纠缠 push_back 重新定位,
            // 和一组控件解锁 -then-reused 字节 默默地放下
            // 真实的目录而不是崩溃——最糟糕的错误。
            std::unordered_set<std::string> seen_dirs;
            for (const auto& f : *files) {
                all.push_back(f);
                // 每个父前缀, 解开。 后面的'/' 标记它为
                // 列表中的目录, 这也是用户需要告诉`src`
                // 从`src` 文件夹文件的提示。
                for (std::size_t i = 0; i < f.size(); ++i) {
                    if (f[i] != '/') continue;
                    std::string dir = f.substr(0, i + 1);   // 保留 '/'
                    if (seen_dirs.insert(dir).second)
                        all.push_back(std::move(dir));
                }
            }
            return util::Snapshot<std::vector<std::string>>{
                std::make_shared<const std::vector<std::string>>(std::move(all))};
        },
    };
}

SandboxListPane::SandboxListPane()
    : complete(path_source(), agentty::mention::file_filter()) {}

form::Field list_entry_text(std::string_view id, std::string label,
                            std::string help) {
    return text(id, std::move(label), std::move(help), {});
}

form::Field list_entry_number(std::string_view id, std::string label,
                              std::string help) {
    // A TEXT 排,不是数字排,区别在于 光标.
    //
    // maya::panel::Number 渲染 赤整数,无 光标 字段-
    // -它结构上无法显示一个。对于您用箭头向下移动的设置行来说,
    // 这样做是正确的( 内存, CPU 百分比),错误的a LIST 输入
    // 输入: 无法看到你在哪里, 或者该行甚至活着。
    // "I don't see a caret in the port list" 这正是。
    //
    // 数字在回读时执行( E) read_sandbox_list
    // 解析 并且放弃任何非港口的东西,所以限制在不计算成本的情况下继续存在。
    // 光标.
    return text(id, std::move(label), std::move(help), {});
}

void renumber_sandbox_list(SandboxListPane& p) {
    // 电话 最后 排行是额外负担,并保持其 "+" ;
    // 上面的一切都是真实的条目并获得其位置。 驱动的不是存储的索引,
    // 而是字段顺序, 因此附件不能让标签被留下。
    //
    // 只写什么 CHANGED。这在键盘上运行,
    // 填补后排的行,并无条件重新分配两个 std::strings
    // 每行意味着整个列表中分配的每个附件——这在列表足够长,
    // 需要重新编号时被感觉成是打入时差。
    // 比较一下是几个 字节 已经是了 -hot 字符串;
    // 它避免的任务是一个堆积的写作.
    for (std::size_t i = 0; i < p.form.fields.size(); ++i) {
        const bool is_add = (i + 1 == p.form.fields.size());
        auto& f = p.form.fields[i];
        // 说吧,一个不是一个的入口被关闭了 WHILE 输入--对于宿主,
        // 你应当说出那将是什么。 已保存 AS 当它与什么不同
        // 带类型的 (贴纸) URL 因此,改写是永远没有沉默的。
        //
        // 故意两条频道 A REJECTION 进去
        // `error` ,而面板画在行下,无论它是否聚焦:
        // 一个即将消失的条目,必须从列表的任何地方看错。
        // A NOTE ("saved as" ,最高提醒)进入 `help` ,
        // 只有焦点行显示—— 有用的地方,其他任何地方的噪音。
        std::string hint, bad;
        if (const auto* t = std::get_if<form::field::Text>(&f.value); t && !t->value.empty()) {
            const auto& v = t->value;
            if (p.kind == SandboxListPane::ListKind::Port) {
                const bool digits = !v.empty() && v.size() <= 5
                    && v.find_first_not_of("0123456789") == std::string::npos;
                if (!digits || std::stoul(v) == 0 || std::stoul(v) > 65535)
                    hint = "not a port (1-65535) \xc2\xb7 dropped on close";
            } else if (p.kind == SandboxListPane::ListKind::Host) {
                const auto ne = tools::util::sandbox::netproxy::normalize_host_entry(v);
                if (ne.value.empty())
                    bad = std::string{ne.why} + " \xc2\xb7 dropped on close";
                else if (ne.value != v)
                    hint = "saved as " + ne.value;
                else if (ne.value.starts_with("*."))
                    hint = "any subdomain \xc2\xb7 add " + ne.value.substr(2) + " too for the site itself";
            }
        }
        if (f.error != bad) f.error = std::move(bad);
        if (is_add) {
            if (f.label != "+") f.label = "+";
            std::string want = !hint.empty() ? std::move(hint) : list_add_row({}, p.kind).help;
            if (f.help != want) f.help = std::move(want);
        } else {
            auto want = std::to_string(i + 1) + ".";
            if (f.label != want) f.label = std::move(want);
            if (f.help != hint) f.help = std::move(hint);
        }
    }
}

form::Field list_add_row(std::string_view id, SandboxListPane::ListKind kind) {
    switch (kind) {
        case SandboxListPane::ListKind::Port:
            return list_entry_number(id, "+", "type a port");
        case SandboxListPane::ListKind::Host:
            // a 教两种形式的提示词,因为最高规则是
            // 人们错误的。
            return list_entry_text(id, "+", "type a host \xc2\xb7 github.com or *.github.com");
        case SandboxListPane::ListKind::Path:
        default:
            return list_entry_text(id, "+", "type a path");
    }
}

SandboxListPane build_sandbox_list(std::string_view row_id, std::string title,
                                   std::string help,
                                   const std::vector<std::string>& values,
                                   SandboxListPane::ListKind kind) {
    SandboxListPane p;
    p.row_id = std::string{row_id};
    p.title = std::move(title);
    p.help = std::move(help);
    p.kind = kind;

    // 每个条目一行。 ID 携带索引, 所以重读顺序
    // 是显示顺序。 保存重排路径的列表将是一个它自己
    // 的小背叛。
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto id = "e" + std::to_string(i);
        auto f = text(id, std::to_string(i + 1) + ".", {}, values[i]);
        // 光标 现场 END 数值,不为0。
        //
        // 文本字段默认其光标为 0, 因此移动到一个已经读出"/opt/sdk"
        // 的条目上, 输入字符XX00120 则将字符BEFORE 设置为路径。
        // 世界上每个文本编辑器都将光标 放置在文本停止的地方,
        // 而"append to what is here" 是编辑
        // 一个现有条目通常唯一需要的。
        std::get<form::field::Text>(f.value).cursor = values[i].size();
        p.form.fields.push_back(std::move(f));
    }

    // 一条尾端的空白行 IS 额外负担。
    //
    // 不是一个 "Add" 按钮:按钮需要自己的密钥处理、自己的焦点规则和
    // 创建条目的第二种方式。一个空行已被窗体的每个规则编辑,
    // read_sandbox_list 空空的,所以,
    // 我说 "type into the empty row" 增加,
    // 以及 "clear a row" 移除,没有新的学习手势,也没有任何办法最终会出现一个流浪的空条目.
    const auto add_id = "e" + std::to_string(values.size());
    p.form.fields.push_back(list_add_row(add_id, kind));

    p.form.title = p.title;
    return p;
}

std::vector<std::string> read_sandbox_list(const SandboxListPane& p) {
    std::vector<std::string> out;
    for (const auto& f : p.form.fields) {
        const auto* t = std::get_if<form::field::Text>(&f.value);
        if (!t) continue;
        auto v = t->value;
        // Trim:一条带有偏移空间的路径是默默不
        // 匹配的路径,这正是逗号格式的失败.
        while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());
        while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.pop_back();
        if (v.empty()) continue;   // 空白的 \ \ 两种都缺席 \ =

        if (p.kind == SandboxListPane::ListKind::Port) {
            // 端口为带类型的,为文字行,可以显示光标,
            // 因此数字-and-range规则由HERE执行,而不是由部件执行.
            // 任何不是在范围内的端口都会被丢弃而不是被夹
            // 住:默默地将"8O80"变成8080,
            // 将会是发明用户没有要求的规则.
            if (v.find_first_not_of("0123456789") != std::string::npos) continue;
            unsigned long n = 0;
            try { n = std::stoul(v); } catch (...) { continue; }
            if (n == 0 || n > 65535) continue;
            out.push_back(std::to_string(n));   // 已正常化: 滴入"0443"
        } else if (p.kind == SandboxListPane::ListKind::Host) {
            // 冠状体,通过代理的正常人: 已保存 列表为 字节-for-byte
            // 将执行什么,以及两个拼写一个主机( "GitHub.com",
            // "https://github.com/" ) 解析为
            // 下面的一个条目。
            auto ne = tools::util::sandbox::netproxy::normalize_host_entry(v);
            if (!ne.value.empty()) out.push_back(std::move(ne.value));
        } else {
            out.push_back(std::move(v));
        }
    }
    // 重复, 保留 首次 位置。。。
    //
    // 重复在这里永远不会有意义——两个相同的绑定是一绑定,
    // 两个相同的端口是一埠——但是很容易偶然地创建
    // (类型是路径,滚动,再键入),默默地保留两者,
    // 使得行数与策略的操作不相符。秩序是保存的,
    // 而不是排序的,因为用户 带类型的 命令,
    // 并重排他们的名单 保存 这是一种小背叛
    std::vector<std::string> uniq;
    uniq.reserve(out.size());
    for (auto& v : out)
        if (std::find(uniq.begin(), uniq.end(), v) == uniq.end())
            uniq.push_back(std::move(v));
    return uniq;
}

#if defined(__linux__) || defined(__APPLE__) || defined(_WIN32)

Preview preview_sandbox(const sandbox_cfg::Config& cfg) {
    using namespace ::clay;
    using namespace ::clay::literals;

    Preview out;

    auto d = Policy<Draft>{};
    // 只有改变WALLS的轴需要在这里忠实;
    // 精确绑定列表不会移动保证,所以预览会使用形状
    // 而不是完整的读集(产物路径构建了真实的东西).
#if defined(_WIN32)
    // Windows拒绝一条没有驱动器的路径,所以形状需要本土根.
    // 根不动墙 所以固定的根保持了这个纯净
    d = std::move(d).ro_bind("C:\\Windows", "C:\\Windows");
#else
    d = std::move(d).ro_bind("/usr", "/usr");
#endif
    d = std::move(d).proc_fs("/proc");
    d = std::move(d).dev_fs("/dev");
    d = std::move(d).tmpfs("/tmp", Bytes{cfg.tmp_mb * 1024 * 1024});

    switch (cfg.net_mode) {
        case sandbox_cfg::NetMode::Full:
            // 编号 相同 形状产卵路径 编译 : 完整网络是代理,而不是主机堆栈。
            // 预览赤裸连接( N) "" , 0) 制成的 , 编成 macOS
            // 已读行 "partial (endpoint allow-list)"
            // 有关事实上是一个回转端口的墙壁。端口是一个站点。 -in:
            // 保证是不会移动的
            d = std::move(d).connect("", 0).proxied_netns(3128);
            break;
        case sandbox_cfg::NetMode::None:
            break;  // 完全没有授权:空网格
        case sandbox_cfg::NetMode::Ports:
            for (auto p : cfg.allow_ports) d = std::move(d).connect("", p);
            break;
    }

    switch (cfg.syscall_mode) {
        case sandbox_cfg::SyscallMode::Off:
            // everything(), 不 "leave it alone".
            //
            // 默认 -constructed SyscallPolicy 杀死 -by-default
            // 没有规则,所以省略这个不是 "no filter" ——它是过滤器,
            // 在执事时杀死客人, claybin 拒绝 编译 其内容:
            // "empty allow-list would kill on exec".（允许清单为空会导致执行程序时被终止。）
            //
            // 预览片在它第一次运行时被抓住了。
            // 直列 LOOKED 像是一个明智的开关 并且
            // 会生产一个 沙箱 将每个命令都杀死。
            // 这就是一个现场预览的论据 — 错误在设置中是看不见的
            // UI 并且很明显的那一刻 编译 保险单
            d = std::move(d).syscall_profile(SyscallPolicy::everything());
            break;
        case sandbox_cfg::SyscallMode::Compiler:
            d = std::move(d).syscall_profile(profiles::compiler_with_network());
            break;
        case sandbox_cfg::SyscallMode::Strict:
            d = std::move(d).syscall_profile(profiles::with_filesystem());
            break;
    }

    if (cfg.memory_mb) d = std::move(d).memory(Bytes{cfg.memory_mb * 1024 * 1024});
    if (cfg.max_procs) d = std::move(d).processes(cfg.max_procs);
    if (cfg.cpu_percent) d = std::move(d).cpu_percent(cfg.cpu_percent);

    d = std::move(d).new_session();
    d = std::move(d).die_with_parent();

    // 编译 与会实际的后端 RUN 使用这个策略。
    // 编译器 在一个mac上, 更微妙的 一半相同的虫子与
    // Linux-gated 行数: 面板 已渲染,
    // 但每个它预测的墙都来自主机没有的命名空间和组,
    // 因此资源封顶读取 `none` 安全带实际上正在应用 rlimits。
    // 预览不同平台的 沙箱 不如没有预览。
    //
    // `plan_ops` 是一个 linux 概念( Name
    // claybin 计划是 字节 字符串; 字符串 macOS 后端将内核配置
    // 文件交接到 STRING ,所以没有行动计数,行保持0而不是报告一个虚构。
#if defined(__APPLE__)
    ::clay::macos::Options mopts;
    mopts.profile_name = "agentty";
    auto compiled = ::clay::macos::compile(std::move(d).seal(),
                                           tools::util::sandbox::claybin_backend::host(),
                                           mopts);
#elif defined(_WIN32)
    // 与产卵路径(sandbox_claybin.cpp spawn_argv)
    // 相同的选项: ACL 印章上,所以文件系统壁读取命令的真正得到的.
    // probe_host() 缓存 其判断,所以这需要 一个探测器每个过程。
    ::clay::windows::Options wopts;
    wopts.allow_host_acl_mutation = true;
    auto compiled = ::clay::windows::compile(std::move(d).seal(),
                                             ::clay::windows::probe_host(), wopts);
#else
    // 缓存探测器:这个在面板的每个键上运行,并探测叉.
    auto compiled = compile(std::move(d).seal(),
                            tools::util::sandbox::claybin_backend::host());
#endif
    if (!compiled) {
        out.compiled = false;
        out.error = std::string{compiled.error().mechanism};
        return out;
    }
    out.compiled = true;
#if defined(__linux__)
    out.plan_ops = compiled->plan.op_count();
#endif
    for (int i = 0; i < static_cast<int>(CapId::count_); ++i) {
        auto id = static_cast<CapId>(i);
        Wall w;
        w.name = cap_name(id);
        auto s = compiled->guarantees.strength(id);
        w.strength = s == Enforcement::strong     ? "strong"
                     : s == Enforcement::partial  ? "partial"
                     : s == Enforcement::advisory ? "advisory"
                                                  : "none";
        w.mechanism = compiled->guarantees.mechanism(id);
        // 保险单从未要求的能力不是其中的漏洞。 3个资源上限为 0 = "no cap"
        // 目的( 4月 4月) sandbox_config.hpp:
        // 正确的上限是机器的产物,所以没有这个
        // 默认的政策报告3`none`没有问题.
        w.not_requested =
            (id == CapId::mem_limit && cfg.memory_mb == 0) ||
            (id == CapId::cpu_limit && cfg.cpu_percent == 0) ||
            (id == CapId::pid_limit && cfg.max_procs == 0);
        out.walls.push_back(std::move(w));
    }
    return out;
}

void annotate_sandbox_form(form::Form& f, const Preview& preview,
                          const sandbox_cfg::Config& cfg, EngineStatus status) {
    // 所描述的墙壁,而所选引擎不是运行的墙壁是FORECAST,
    // 下面的每个注释都不得不如此表示.
    //
    // ⊿Τ硂 – -row §15中添加的诚实也成为了同样谎言,
    // "strong via seccomp-bpf" 接下来是,
    // 在目前的紧张状态下, 在一个过程上, seccomp
    // 过滤器不存在。后缀很短,因为它会走在每行注解上,
    // 而且它会走在每一行而不是在页脚上一次,正因为页脚是人们跳过的
    // 地方(§15)。
    const char* tense = status == EngineStatus::InForce ? "" : " (on restart)";
    // 哪个能力可以回答哪个行。 映射是函数的全部点, 也是其中唯一有趣
    // 的内容: 用户编辑的一行是 POLICY 中,
    // 并按 CAPABILITY 因此,如果没有这张桌子,
    // 诚实的答案就是一个翻译,而不是一个问题。 claybin's
    // cap_name ()字符串是键(见
    // claybin/core/witness.hpp).
    //
    // 不是每行都有一条, 空隙是诚实的,而不是缺失的:
    // `Also readable` 是一个约束性清单,而不是一种能力 -
    // - -- 管理它的墙是: filesystem.read 上面的
    // 瞄准镜栏上已经显示过 重复到四条路径栏上 就会发出声音 假装很严谨
    struct RowCap { std::string_view row; std::string_view cap; };
    static constexpr RowCap kRowCaps[] = {
        {kSbFsScope,    "filesystem.read"},
        {kSbNetMode,    "network.isolation"},
        {kSbSyscalls,   "syscall.filter"},
        {kSbMemoryMb,   "resource.memory"},
        {kSbMaxProcs,   "resource.pids"},
        {kSbCpuPercent, "resource.cpu"},
    };

    auto row_of = [&](std::string_view id) -> form::Field* {
        for (auto& fld : f.fields)
            if (fld.id == id) return &fld;
        return nullptr;
    };
    auto wall_of = [&](std::string_view cap) -> const Wall* {
        for (const auto& w : preview.walls)
            if (w.name == cap) return &w;
        return nullptr;
    };

    // ── 墙壁,在要求它们的行旁边 ────────────────────
    for (const auto& [row_id, cap] : kRowCaps) {
        auto* fld = row_of(row_id);
        if (!fld) continue;

        if (!preview.compiled) {
            // 保险单没有 编译 ,所以,我说 NO 排墙是已知的,
            // 说 "strong" 这里的一 过期 预览会是最糟糕的谎言:自信,
            // 每 -row ,而且错了。
            fld->origin = "\xe2\x80\x94";
            continue;
        }
        const auto* w = wall_of(cap);
        if (!w) continue;

        // 用户要求的上限不是作为CHOICE读,不是失败。
        // "none (none)" 旁边 内存 (MB
        // )=0在技术上准确,只告诉用户可能断裂的东西.
        if (w->not_requested) {
            fld->origin = "no cap set";
            continue;
        }

        // "strong via landlock abi 10" ,
        // 没有 "strong" 机制 IS 收据 -- -- 凭本身的金额即与不可伪造的索赔
        // 相同 "sandbox: active" ,这是错误的这个 面板 是在后来建造的。
        fld->origin = (w->mechanism.empty()
            ? w->strength
            : w->strength + " via " + w->mechanism) + tense;
    }

    // 姿势排队得到 SCORE 而不是一种机制:它不是一种能力,
    // 而是一整套能力。 "9/11 strong" 把
    // 您必须信任的名称中的预设转换成一个可以比较的数字,
    // 而它是使两种姿势可以一眼看就得到的注释。
    //
    // 内核隔离被排除在分母之外,而不是算作失败,
    // 因为任何进程后端都无法满足它(§13),
    // 离开它就会将每个姿态都限制在100%以下,
    // 原因与用户的选择无关。
    if (auto* fld = row_of(kSbPosture)) {
        if (!preview.compiled) {
            fld->origin = "will not compile";
        } else {
            std::size_t strong = 0, total = 0;
            for (const auto& w : preview.walls) {
                if (w.name == "host.kernel_isolation") continue;
                // 与页脚相同的推理:没有人要求的盖不是失败的墙。
                // 计算它使平衡得分 8/11 在一个主机上,
                // 每扇要求的墙都是坚固的,
                // 它读作 "three things are broken" 完全是假的 完全没有
                if (w.not_requested) continue;
                ++total;
                if (w.strength == "strong") ++strong;
            }
            if (total)
                fld->origin = std::to_string(strong) + "/" +
                              std::to_string(total) + " strong" + tense;
        }
    }

    // 空路径行 渲染 以空白行表示,该行读作破碎而非
    // "nothing added yet". `origin`
    // 暗淡的对 -hand 栏,所以有一个词说行是工作且空的,
    // 而不与有行上的壁标进行竞争。
    for (auto id : {kSbReadPaths, kSbWritePaths, kSbDenyPaths, kSbPorts}) {
        auto* fld = row_of(id);
        if (!fld || !fld->origin.empty()) continue;
        if (const auto* t = std::get_if<form::field::Text>(&fld->value))
            if (t->value.empty()) fld->origin = "none";
    }

    // ── 校验: 击败自己的行 ──────────────────────────
    //
    // 每一个都有一个背景 COMPILES 你们不要
    // 做任何恶行。那是危险的课,是错觉, claybin
    // 上面已经写着拒绝,但是一个隐秘的配置 意味着
    // 没有其他表面可以出现的东西

    // Per-port网络没有端口,是空名空间:用户要求
    // 允许特定的集,而不允许任何内容.
    if (cfg.net_mode == sandbox_cfg::NetMode::Ports && cfg.allow_ports.empty()) {
        if (auto* fld = row_of(kSbPorts))
            fld->error = "no ports listed, so this denies all network \xc2\xb7 "
                         "set Access to none if that is what you meant";
    }

    // Host-readable 这不是一个范围,而是没有范围。
    // 不是错误,它是一个仅想要写墙的用户的合法选择,
    // 但它决不能像列表上的另一个选项那样读。
    if (cfg.fs_scope == sandbox_cfg::FsScope::HostReadable) {
        if (auto* fld = row_of(kSbFsScope))
            fld->error = "grants read on / \xc2\xb7 secrets outside the mask list "
                         "are reachable";
    }

    // 关闭是一个真正的选择(一个用户,其工具链在过滤
    // 器下断开,应该能够说),但它也禁用 BROKERING ,
    // 因为经纪人是过滤器 决定在 运行时。关闭一行,
    // 静静关闭另一行,正是用户无法推断的组合。
    if (cfg.syscall_mode == sandbox_cfg::SyscallMode::Off) {
        if (auto* fld = row_of(kSbSyscalls))
            fld->error = "no filter, and no brokering either \xc2\xb7 "
                         "ptrace and kill stop being supervised";
    }

    // 一种不拒绝的交割政策是在此设定的,它扩大了爆炸
    // 半径而不是缩小爆炸半径,它这样做了。
    // OUTSIDE 编号 沙箱 上面没有墙壁报告,
    // 脚壁报告结构上不能提到它,所以排队必须携带自己的警告.
    if (cfg.handoff != sandbox_cfg::HandoffPolicy::Refuse) {
        if (auto* fld = row_of(kSbHandoff))
            fld->error = cfg.handoff == sandbox_cfg::HandoffPolicy::Allow
                ? "the agent may author files your host later executes \xc2\xb7 "
                  "this is the shape of every escape in Pillar's 2026 series"
                : "allowed and recorded \xc2\xb7 the write still happens";
    }

    // 路径进入 两者 a 授权 列表和面具列表是矛盾的,
    // 它是其中之一 编译器 无法捕获: 两种都生产有效的挂载,
    // 而哪种胜负降为排放顺序。 用户要求两种不兼容的东西,
    // 而政策默默地选择了一种。
    //
    // 选择赢家是有意的 "masked" 财务报告和已
    // 审计财务报表 "writable" 同样的读数
    // 相同的两行 - 所以 面板 说它看到什么,
    // 让用户决定他们的意思是哪一行.
    {
        auto clash = [&](const std::vector<std::string>& grants) {
            for (const auto& g : grants)
                for (const auto& d : cfg.deny_paths)
                    if (g == d) return g;
            return std::string{};
        };
        if (const auto bad = clash(cfg.write_paths); !bad.empty()) {
            if (auto* fld = row_of(kSbDenyPaths))
                fld->error = bad + " is also in Also writable \xc2\xb7 "
                             "remove it from one list";
        } else if (const auto bad2 = clash(cfg.read_paths); !bad2.empty()) {
            if (auto* fld = row_of(kSbDenyPaths))
                fld->error = bad2 + " is also in Also readable \xc2\xb7 "
                             "remove it from one list";
        }
    }

    // 三个列表中任何一个列表中的相对路径.
    //
    // 电话 沙箱 解决这些与 CHILD 是个杂碎,
    // 不是 面板 是因为... "build" 表示用户无法
    // 预测且通常完全匹配的东西。 它 编译 安静的不做它读的一样的,
    // 和每个同班 -port 以上行数.
    for (const auto* entry : {&cfg.read_paths, &cfg.write_paths, &cfg.deny_paths}) {
        std::string_view id = entry == &cfg.read_paths    ? kSbReadPaths
                            : entry == &cfg.write_paths   ? kSbWritePaths
                                                          : kSbDenyPaths;
        auto* fld = row_of(id);
        if (!fld || !fld->error.empty()) continue;
        for (const auto& p : *entry) {
            if (p.empty() || p.front() == '/' || p.front() == '~') continue;
            fld->error = "`" + p + "` is relative \xc2\xb7 resolved against the "
                         "command's working directory, so it may match nothing";
            break;
        }
    }

    // 完全关闭秘密面具 深度0仍然覆盖 工作区 根,
    // 所以这不是 "no masking" 说这比让用户
    // 从0猜到好
    if (cfg.mask_scan_depth == 0) {
        if (auto* fld = row_of(kSbMaskDepth))
            fld->error = "root only \xc2\xb7 a nested services/*/.env stays readable";
    }

    // 允许主机: 三个设置, 编译 和悄悄地表示某种意思
    // else. Ordered most-surprising first; one message per row.
    if (auto* fld = row_of(kSbHosts); fld && !cfg.allow_hosts.empty()) {
        namespace np = tools::util::sandbox::netproxy;
        std::size_t bad = 0;
        std::string first_bad;
        bool only_wild = true;
        for (const auto& h : cfg.allow_hosts) {
            const auto ne = np::normalize_host_entry(h);
            if (ne.value.empty()) { if (!bad++) first_bad = h; continue; }
            if (!ne.value.starts_with("*.")) only_wild = false;
        }
        // (1) 手 手 -edited settings.json 输入代理将忽略。
        if (bad)
            fld->error = "`" + first_bad + "` is not a host name \xc2\xb7 ignored" +
                         (bad > 1 ? " (and " + std::to_string(bad - 1) + " more)" : "");
        // (2)每个条目通配符: 网站THEMSELVES
        // 是无法到达的,这是最高规则,咬断不认识的人.
        else if (only_wild)
            fld->error = "only *.wildcards \xc2\xb7 the sites themselves (e.g. " +
                         np::normalize_host_entry(cfg.allow_hosts.front()).value.substr(2) +
                         ") are not included";
        // (3) 列表中不在此模式下做任何工作。
        else if (cfg.net_mode != sandbox_cfg::NetMode::Full)
            fld->error = "saved, but only enforced while Access is full";
        if (fld->origin.empty() && cfg.net_mode == sandbox_cfg::NetMode::Full)
            fld->origin = "via proxy";
    }
}

void annotate_host_risks(form::Form& f, const sandbox_cfg::Config& cfg,
                         const HostFacts& facts) {
    auto row_of = [&](std::string_view id) -> form::Field* {
        for (auto& fld : f.fields)
            if (fld.id == id) return &fld;
        return nullptr;
    };
    // 只有在没有更具体的信息时才补齐行警告.
    auto warn = [&](std::string_view id, std::string msg) {
        if (auto* fld = row_of(id); fld && fld->error.empty())
            fld->error = std::move(msg);
    };

    // Full 下的端口列表什么都不做。审计发现 [22,53,80,443]
    // 设置了 Access 的全版,并读作 允许清单; 不是一个。
    if (cfg.net_mode == sandbox_cfg::NetMode::Full && !cfg.allow_ports.empty())
        warn(kSbPorts, "ignored while Access is full \xc2\xb7 every port is open "
                       "\xc2\xb7 set Access to ports to use this list");

    // 工作区 = $HOME : 写墙是 工作区 ,因此每个点文件都是可写,
    // 包括登录文件以及 ~/.local/bin.
    if (facts.workspace_is_home)
        warn(kSbFsScope, "workspace is your home dir \xc2\xb7 every dotfile is "
                         "writable \xc2\xb7 start agentty in a project dir instead");

    // 交接费 快照 击出其盖子,所以切片下方的文字不会被报告。
    // -only 绑定什么 WAS 发现仍然坚守。
    if (facts.handoff_truncated)
        warn(kSbHandoff, "last check hit its 20000-entry cap \xc2\xb7 a write "
                         "deeper in the tree is not reported \xc2\xb7 use a smaller "
                         "workspace");
}

#else

Preview preview_sandbox(const sandbox_cfg::Config&) {
    // claybin 总是联系在一起的,但只适用以下政策: Linux, macOS
    // 财务报告和已审计财务报表 Windows。其他任何地方都没有这个 面板 预览。
    Preview out;
    out.compiled = false;
    out.error = "no sandbox backend on this platform";
    return out;
}

void annotate_sandbox_form(form::Form&, const Preview&,
                           const sandbox_cfg::Config&, EngineStatus) {
    // 没有任何说明: 没有墙壁可报告, 每行都已经锁定了
    // build_sandbox_form 在一个没有后端的东道主上。
    // -row "none" 旁边的一排已经说 为什么会是同样的
    // 事实两次。
}

void annotate_host_risks(form::Form&, const sandbox_cfg::Config&,
                         const HostFacts&) {}

#endif

}  // namespace agentty::ui::panel
