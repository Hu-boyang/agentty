// agentty的沙箱:后端选择,模型的告知,否定说明,
// 凭据口罩,以及claybin的保证.
//
// 曾经有第二个执行方式 mcp-cpp 同意这个观点
// mcp-cpp 不再运行任何内容( 每个命令都会经过)
// agentty 执行官,所以有一个 沙箱 没有什么可以保持步调。

#include "agtest.hpp"

#include "agentty/tool/util/sandbox.hpp"
#include "agentty/domain/sandbox_config.hpp"   // 配置默认后端
#include "agentty/provider/prompt.hpp"          // default_system_prompt - 电话 沙箱 时尚

// Linux-only ,喜欢 claybin 应用步骤。 下面的用户
// 大小写 编译 计划 DESCRIBED 机主机,它可移植,但它需要的政策类型
// 却不是。
#if defined(__linux__)
#include <claybin/plan/compile.hpp>
#include <claybin/policy/policy.hpp>
#include <claybin/policy/profiles.hpp>
#endif

#include <cstdlib>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace ag = agentty::tools::util::sandbox;
namespace sandbox_cfg = agentty::sandbox_cfg;

#if defined(__linux__)
TEST_CASE("sandbox: claybin does not need a user namespace to be useful") {
    // 电话 Ubuntu 24.04个案件( KhazAkar 问题 东道船是 AppArmor
    // 拒绝配置文件 uid_map 写入未关闭 二进制程序 ,所以brap死与
    // "setting up uid map: permission denied" 财务报告和已审计
    // 财务报表 agentty 报告根本没有后端 -- 默认为 bwrap, bwrapp 失败,
    // 没有进一步查看。
    //
    // claybin 复制 brap 先决条件的可用性( )
    // user_namespaces && mount_namespaces ),
    // 这意味着复制brap的失败,使第二个后端在需要它的主机上变得无用.
    // Landlock and seccomp 已经 UNPRIVILEGED
    // 机制:不需要命名空间,而且 AppArmor '用户限制不会触碰
    // 它们.
    //
    // 通过 compile() 测量到一个描述的主机,
    // 因为此机器有工作命名空间, 不能不使用。 用户关闭后还能生存
    // 到什么 :
    //
    //   filesystem.read/write/exec
    //   坚固的地锁 syscall.filter 强 seccomp
    //   -bpf resource.memory 强 cgroup2
    //   memory.max privilege.drop 强 no_new_privs + 空边框设置
    //
    // ...什么是没有的: network.isolation 财务报告和已审计
    // 财务报表 process.isolation ,这两个都确实需要命名空间。
    // 这是真正的降级,是每个能力报告而不是纸质。
    using namespace ::clay;

    auto host = probe_host();
    host.user_namespaces = false;
    host.mount_namespaces = false;
    host.pid_namespaces = false;
    host.net_namespaces = false;
    host.uts_namespaces = false;

    // 跳过机器无法回答问题的地方
    if (host.landlock_abi == 0 && !host.seccomp) return;

    auto d = Policy<Draft>{};
    d = std::move(d).ro_bind("/usr", "/usr");
    d = std::move(d).bind("/tmp", "/tmp");
    d = std::move(d).syscall_profile(profiles::compiler_with_network());

    auto compiled = compile(std::move(d).seal(), host);
    REQUIRE(compiled.has_value());

    // 文件系统边界是最重要的: 将一个核准的命令保存在外
    // ~/.ssh. 如果这是 `none`, claybin
    // 这样的主机上没有东西可提供,下面的可用性层是错的.
    if (host.landlock_abi > 0) {
        CHECK(compiled->guarantees.strength(CapId::fs_read) != Enforcement::none);
        CHECK(compiled->guarantees.strength(CapId::fs_write) != Enforcement::none);
    }
    if (host.seccomp)
        CHECK(compiled->guarantees.strength(CapId::syscall_filter) !=
              Enforcement::none);

    // 诚实的一段: 命名空间 -backed 墙壁已经消失,
    // 不是沉默地声称。 `strong` 这正是这个子
    // 系统 存在的防止的谎言
    CHECK(compiled->guarantees.strength(CapId::proc_isolation) == Enforcement::none);
}
#endif  // __linux__

#if defined(__linux__)
TEST_CASE("sandbox: a microvm request is REFUSED, never downgraded") {
    // 电话 不变量 这说明 "we do not implement kernel isolation"
    // 诚实的立场,而不是空隙。
    //
    // 如果出现 compile() 默默降级 Isolation::microvm
    // 向进程 沙箱 要求单独内核的呼叫者会得到一个命名空间 却
    // 一无所获 他们的威胁模式会说 "separate kernel"
    // 现实却说 "shared" 这是这个子系统最糟糕的失败,
    // 并且比没有这个特征还要糟糕。
    //
    // 被从 agentty 而不是信任,因为它是一个 -line 检查在 a 子模块 井的井,
    // 我们用它来保护它 -meaning "make the sandbox more forgiving"
    // 修改可以删除,而这一方面没有任何人会注意到。
    using namespace ::clay;
    using namespace ::clay::literals;

    auto d = Policy<Draft>{};
    d = std::move(d).ro_bind("/usr", "/usr");
    d = std::move(d).isolation(Isolation::microvm);

    auto compiled = compile(std::move(d).seal(), probe_host());
    REQUIRE(!compiled.has_value());
    // 并且原因将缺失的作品命名,因此错误是可以诉的,
    // 而不是赤裸裸的拒绝.
    CHECK(std::string_view{compiled.error().mechanism}.find("microvm") !=
          std::string_view::npos);
}

TEST_CASE("sandbox: the kernel-isolation gap says whose fault it is") {
    // host.kernel_isolation 谓 `none` 由於两个不同的原因,
    // 一个决定是否关心的电话商 值得真正的一个: "we did not build it" 相对
    // "this host could not run it anyway" 平房( 平房 )
    // "process-backend" 第一次邀请,这在多数情况下是错误的 CI 跑步者,我们是
    using namespace ::clay;

    auto report_with = [](bool kvm) {
        auto host = probe_host();
        host.kvm = kvm;
        // 政策完成后足以 编译。赤裸裸的 ro_bind 并非: 数据
        // claybin 拒绝一个没有根,没有/proc和没有工作目录的草稿,
        // 这是正确的,这是我在这里的第一个错误.
        auto d = Policy<Draft>{};
        d = std::move(d).ro_bind("/usr", "/usr");
        d = std::move(d).proc_fs("/proc");
        d = std::move(d).dev_fs("/dev");
        d = std::move(d).tmpfs("/tmp", Bytes{64ull << 20});
        d = std::move(d).workdir("/");
        d = std::move(d).syscall_profile(profiles::compiler_with_network());
        auto compiled = compile(std::move(d).seal(), host);
        REQUIRE(compiled.has_value());
        // 总是 `none` 在一个进程后端上,无论主机能做什么。该部分是机器
        // -checked 由 claybin 自己的模糊器也一样。
        CHECK(compiled->guarantees.strength(CapId::host_kernel_isolation) ==
              Enforcement::none);
        return std::string{
            compiled->guarantees.mechanism(CapId::host_kernel_isolation)};
    };

    CHECK(report_with(true).find("kvm available") != std::string::npos);
    CHECK(report_with(false).find("no kvm") != std::string::npos);
}
#endif  // __linux__

TEST_CASE("sandbox: claybin is the default, and bwrap is still the fallback") {
    // 默认的翻转到claybin, 因为差异是MEASURED, 没有争论: agentty
    // 的 bwrap路径 发音为 --seccomp, 没有 cgroup 限制和 landlock,
    // 因此 claybin 在 syscall.filter, 全部三个资源上限,
    // filesystem.exec 和 经纪 上严格地更强大。 bwrab 胜出的地方
    // 没有能力( tests/sandbox_audit.cpp 打印报告)。
    //
    // 本案的警卫是 OTHER 半 — 保留 brap 而不是删除。
    // 两台引擎失效。 不同 主机( T) :
    // 拒绝非特权用户时 brap 死亡( ) Ubuntu 24.04
    // AppArmor ),以及 claybin 需要锁定 + seccomp
    // + cgroup2 ,该 RHEL 内核大于5.13没有.
    // Delete 包和第二主机没有 claybin - 说得没错
    // Backend::None 交易弱者 沙箱 费用 NO 沙箱 21号故障在新帽子,
    // 所以 回退 属于安全财产,并经过测试。
    ag::reset_config_for_test();

    // 一个未变的配置要求claybin.
    const sandbox_cfg::Config fresh;
    CHECK(fresh.backend == sandbox_cfg::LinuxBackend::Claybin);

    // 而实际上选择的引擎从不 在一个可以运行的主机上 EITHER 后端,
    // 这是 回退 : 无论哪个引擎首选名称,
    // 一个可用的主机必须仍然被沙箱所覆盖。
    (void)ag::init(ag::Mode::Auto);
    if (ag::is_active())
        CHECK(ag::detected_backend() != ag::Backend::None);
}

TEST_CASE("sandbox: asking for claybin on a host that refuses it yields bwrap, not None") {
    // 电话 回退 DIRECTION ,这就是翻转默认值的
    // 完全理由是安全的。选择进入(或现在默认)更新后端时,
    // 绝不能让某人付出他们 沙箱 : 显示主机 claybin
    // 无法建造它的墙,必须靠墙壁而不是空洞。
    //
    // 检查通过公众表面而不是强迫探测器,
    // 因为探测器是真正的叉子uid_map尝试,
    // 并假冒它会测试假的.
    ag::reset_config_for_test();
    (void)ag::init(ag::Mode::Auto);

#if defined(__linux__)
    if (ag::is_active()) {
        const auto b = ag::detected_backend();
        // 完全一样 Linux 引擎。在活动时,永不出现,也永不出现 SandboxExec
        // 打开 Linux.
        CHECK(b == ag::Backend::Claybin);
    }
#endif
}

TEST_CASE("sandbox: the model is TOLD it is sandboxed") {
    // 一个不知道是沙盒误诊它所撞到的每一面墙的模型:EPERM变为
    // "your proxy is blocking this",（代理服务器阻止了此访问。）
    // 一个被封堵的连接变为"check your firewall",
    // 它要么发送用户追逐一个幽灵,要么重试是调用,内核永远不会允许.
    //
    // 所以,我们... <environment> 斯坦扎说
    // 边界是存在的。这把针是沙箱里 和同样重要的是,
    // 它会出现 不 当用户转身时 沙箱隔离 关,因为 提示词 声称
    // 没有墙壁的墙壁在另一方向上是同样的谎言。
    ag::reset_config_for_test();
    (void)ag::init(ag::Mode::Off);
    {
        const auto p = agentty::provider::default_system_prompt(::agentty::IoAccess::grant());
        CHECK(p.find("sandbox: ON") == std::string::npos);
    }

    ag::reset_config_for_test();
    (void)ag::init(ag::Mode::Auto);
    if (ag::is_active()) {
        const auto p = agentty::provider::default_system_prompt(::agentty::IoAccess::grant());
        CHECK(p.find("sandbox: ON") != std::string::npos);
        // 阻止误诊的三件事: 哪里写,
        // 否认就是 沙箱 ,该 重试 错的
        CHECK(p.find("writable") != std::string::npos);
        CHECK(p.find("not a broken tool") != std::string::npos);
        CHECK(p.find("Do NOT retry") != std::string::npos);
    }
}

TEST_CASE("sandbox: a denial is explained where the model is looking") {
    // 系统 提示词 以千兆符符符符值表示,
    // 当错误到达时。 OUTPUT ,
    // 这就是模型在决定是否 重试.
    //
    // 驱动通过真正的运行器,所以它覆盖了线条,
    // 而不仅仅是字符串:一个无法在任何策略下读取主机路径的命令.
    ag::reset_config_for_test();
    (void)ag::init(ag::Mode::Auto);
    if (!ag::is_active()) return;   // 这里没有后端; 没有注释

    auto r = ag::run_shell_command("cat /etc/shadow", 64 * 1024,
                                   std::chrono::seconds{20});
    if (r.exit_code != 0 &&
        r.output.find("[sandbox]") != std::string::npos) {
        // 当注解点火时,它必须携带指示,
        // 而不仅仅是诊断 -- -- 仅解释的注解仍然使模型
        // 自由到重试.
        CHECK(r.output.find("Do not retry") != std::string::npos);
        CHECK(r.output.find("firewall") != std::string::npos);
    }
    // 未断言:它 始终 是否为/ etc/shadow
    // 可读性是主机政策的一个属性,要求拒绝的测试
    // 会在配置不同的机器上失败,而不是找到错误。
}

TEST_CASE("sandbox: a SUCCEEDING command is never annotated") {
    // 注释必须保留很少的读取。 将它放在每个命令上,
    // 或者每个失败,否认或不否定, 便是模型学会
    // 跳过的解释变成噪音。
    ag::reset_config_for_test();
    (void)ag::init(ag::Mode::Auto);
    if (!ag::is_active()) return;

    auto r = ag::run_shell_command("echo hello", 64 * 1024,
                                   std::chrono::seconds{20});
    CHECK(r.output.find("[sandbox]") == std::string::npos);
}

TEST_CASE("sandbox masks: the credential name list covers what repos hold") {
    // 扫荡的走 工作区 并遮盖任何文件,其文件 BASENAME 是
    // 一个 凭据 名称。行走是正确的;名单很薄 -- -- 有五个条目,
    // 错过了.npmrc,.git -credentials ,
    // netrc,其余的env家族,五种ssh键型中的三种,
    // 以及云服务 -account 每一个教程都写着文件名。
    //
    // 开车经过 mask_paths () 与真实的 源码树 ,
    // 所以这个测试匹配者 和 而不是仅仅在阵列上
    namespace fs = std::filesystem;
    // 自己 -deleting : 帮助者存在的全部原因就是那只手 -rolled
    // 临时的Dirs是3537目录 泄露到这个盒子里的东西
    agtest::TempDir tmp{"masktest"};
    const auto root = tmp.path();
    fs::create_directories(root / "services" / "api");

    auto touch = [](const fs::path& p) { std::ofstream{p} << "x"; };
    for (const char* n : {".env", ".env.development", ".env.test",
                          ".npmrc", ".netrc", ".git-credentials",
                          "credentials.json", "service-account.json",
                          "id_ecdsa", "id_dsa", ".pgpass"})
        touch(root / "services" / "api" / n);
    // 后缀规则,只有行走才能解决.
    touch(root / "services" / "api" / "server.pem");
    touch(root / "services" / "api" / "prod.tfvars");
    // 和必须的东西 不 被遮掩:普通来源,
    // 看起来只是与规则相邻。
    touch(root / "services" / "api" / "main.cpp");
    touch(root / "services" / "api" / "env.example");

    sandbox_cfg::Config cfg;
    cfg.mask_scan_depth = 3;
    const auto masked = sandbox_cfg::mask_paths(cfg, root.string(), {});

    auto has = [&](std::string_view leaf) {
        return std::any_of(masked.begin(), masked.end(), [&](const auto& m) {
            return std::string_view{m}.ends_with(leaf);
        });
    };

    CHECK(has(".env"));
    CHECK(has(".env.development"));
    CHECK(has(".npmrc"));
    CHECK(has(".netrc"));
    CHECK(has(".git-credentials"));
    CHECK(has("credentials.json"));
    CHECK(has("service-account.json"));
    CHECK(has("id_ecdsa"));
    CHECK(has(".pgpass"));
    CHECK(has("server.pem"));      // 后缀规则
    CHECK(has("prod.tfvars"));     // 后缀规则

    // 普通文件可以读取。 抓获 main.cpp 的掩码
    // 列表会打破每个构建, 并显示它正在工作。
    CHECK_FALSE(has("main.cpp"));
    CHECK_FALSE(has("env.example"));
}

TEST_CASE("sandbox masks: a suffix rule needs its marker to be a suffix") {
    // `*.pem` 匹配 prod.pem ; 规则 `.pem` 只匹配一个
    // 字面上命名的文件 ".pem"。标记是明确的,而不是从形状推断出来的。
    // BECAUSE 推论在这里很危险: "starts with a dot, no second dot"
    // 默默地宣传env,npmrc和.netrc为后缀,所以 prod.env
    // 财务报告和已审计财务报表 scoped.npmrc 开始被蒙上
    // 面具 副作用 添加一个无关条目。意外扩大安全规则与缩小安全
    // 规则一样糟糕。
    namespace fs = std::filesystem;
    agtest::TempDir tmp{"sfxtest"};
    const auto root = tmp.path();

    auto touch = [](const fs::path& p) { std::ofstream{p} << "x"; };
    touch(root / "prod.env");       // 不.env -- -- 必须保持可读性
    touch(root / "scoped.npmrc");   // 不.npmrc - 必须保持可读性
    touch(root / "key.pem");        // IS a.pem -- -- 必须蒙上面具

    sandbox_cfg::Config cfg;
    cfg.mask_scan_depth = 1;
    const auto masked = sandbox_cfg::mask_paths(cfg, root.string(), {});
    auto has = [&](std::string_view leaf) {
        return std::any_of(masked.begin(), masked.end(), [&](const auto& m) {
            return std::string_view{m}.ends_with(leaf);
        });
    };

    CHECK(has("key.pem"));
    CHECK_FALSE(has("prod.env"));
    CHECK_FALSE(has("scoped.npmrc"));
}

#if defined(__linux__)
// ── 组必须实际抓住客人 ───────────────────────────
//
// describe() 报告 `resource.memory strong via cgroup2`
// 任何时候 compile() 成功创建了组。但是使用了产卵
// clay::spawn (),并向前 空 组改为 spawn_in
// ()——它只看它被交给的团体,它从来不看它所编译的姿态。
// 所以它只看一个宿主, 执行 代表,这个小组是创建的,报告过的,
// 然后没有放进去: 面板 广告上写着一堵从未竖起的墙
//
// 也可以是能够杀死一个逃跑的人和不能杀死他之间的区别。
// 签名程序组 想念任何召回的后代 setsid();
// cgroup.kill 那样是逃不掉的
//
// 只可检查主机代表子树的位置。 普通桌面系统会话 不 : 范围为根
// -owned 财务报告和已审计财务报表
// cgroup.subtree_control 无法写,
// 所以 claybin 探测器说,不可辨认, compile() 创建无组,
// 进程 -group 回退 容器和委托用户切片是测试工作所在。
//
// 以问客人住在哪里来衡量,因为这是唯一的答案,
// 报告路径不能假冒。
TEST_CASE("sandbox: a claybin guest runs inside its own cgroup") {
    if (clay::cgroup::probe().availability != clay::cgroup::Availability::delegated)
        return;   // 无法附加此主机; 见上文
    if (!ag::init(ag::Mode::On) || !ag::is_active()) return;
    if (ag::detected_backend() != ag::Backend::Claybin) return;

    std::ifstream self("/proc/self/cgroup");
    std::string ours((std::istreambuf_iterator<char>(self)),
                     std::istreambuf_iterator<char>());
    if (ours.empty()) return;   // c组 v1 或无 c组 fs

    auto r = ag::run_shell_command("cat /proc/self/cgroup", 4096,
                                   std::chrono::seconds{10});
    if (!r.started || r.output.empty()) return;   // 母体被封存在这个主机上

    const auto trim = [](std::string s) {
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        return s;
    };
    const std::string mine  = trim(ours);
    const std::string guest = trim(r.output);
    if (guest.find("0::") != 0) return;   // 不是一个cgroup2 答复

    CHECK_MESSAGE(guest != mine,
                  "the guest must not share agentty's own cgroup -- if it "
                  "does, spawn never attached it and both the limits and "
                  "cgroup.kill are fiction");
}
#endif
