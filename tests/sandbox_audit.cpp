// 怎样? agentty ACTUALLY 执行,能力逐个,在这个主机上?
//
// 政策不刮伤,真正的姿态 build_claybin_posture ( ) 产生,
// 所以答案是: agentty 而不是一只手, 而不是一只小手, -written 实例。
// 每一个 `none` 这里要么是刻意交易,要么是空白,
// 工具的要点是不要猜测哪个。
//
// 摸摸的时候就跑 沙箱:
//   cmake --build build --target sandbox_audit && ./build/sandbox_audit
#include "agentty/util/io.hpp"
#include "agentty//tool/util/sandbox_claybin.hpp"
#include "agentty/tool/util/sandbox.hpp"
#include "agentty/domain/sandbox_config.hpp"

#include <claybin/plan/compile.hpp>
#include <claybin/policy/policy.hpp>
#if defined(_WIN32)
#include <claybin/windows/backend.hpp>
#endif
#if defined(__APPLE__)
#include <claybin/macos/backend.hpp>
#endif

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace cb = agentty::tools::util::sandbox::claybin_backend;
namespace cfgn = agentty::sandbox_cfg;
namespace sb = agentty::tools::util::sandbox;

namespace agentty::tools::util { void set_workspace_root(Io, std::filesystem::path p); }

namespace {

const char* strength(::clay::Enforcement e) {
    switch (e) {
        case ::clay::Enforcement::strong:   return "strong  ";
        case ::clay::Enforcement::partial:  return "partial ";
        case ::clay::Enforcement::advisory: return "advisory";
        case ::clay::Enforcement::none:     return "NONE    ";
    }
    return "?       ";
}

// 没有 HostCapabilities 参数: 它一直被使用( Q) `(void)host` ),
// 一旦探测器成为平台 -dependent 绝对不可能 一个 类型无论如何 -- linux 已经
// clay::HostCapabilities, macOS 已经 clay::macos::HostCapabilities.
// describe() 内容 -probes 内部通过
// 将实际运行此政策的后端,这是这里唯一正确答案.
void audit(const char* label, const cb::Posture& p) {
    std::printf("\n=== %s\n", label);
    auto rep = cb::describe(p);
    if (!rep.ok) {
        std::printf("  compile refused: %s\n", rep.detail.c_str());
        return;
    }
    for (const auto& w : rep.walls)
        std::printf("  %-22s %s %s\n", w.name.c_str(),
                    w.strength == "strong"      ? "strong  "
                    : w.strength == "partial"   ? "partial "
                    : w.strength == "advisory"  ? "advisory"
                                                : "NONE    ",
                    w.mechanism.c_str());
}

void audit_cfg(const char* label, const cfgn::Config& cfg) {
    sb::reset_config_for_test();
    sb::set_config(cfg);
    audit(label, sb::claybin_posture_for_test());
}

}  // 命名空间结束。

int main(int argc, char** argv) {
    const char* ws = "/tmp/agentty-audit-ws";
    std::filesystem::create_directories(ws);
    agentty::tools::util::set_workspace_root(::agentty::IoAccess::grant(), ws);

    // 各个平台的主办方事实。 Linux 探测器无条件地在
    // mac上引起误解:每个字段读 0(没有命名空间,
    // 没有地锁,也没有croups),看起来像一个主机,
    // 里面有 无内容 ,而下面的能力表则报告安全带墙做真正的工作。
    // 在一个屏幕上有两个相互矛盾的答案,第一个是错误的
    // 答案。
#if defined(__APPLE__)
    const auto mhost = ::clay::macos::probe_host();
    std::printf("host: darwin=%u seatbelt=%d\n", mhost.darwin_major,
                (int)mhost.seatbelt);
    std::printf("      sbpl: paths=%d net=%d proc=%d\n",
                (int)mhost.sbpl_path_filters, (int)mhost.sbpl_network_filters,
                (int)mhost.sbpl_process_filters);
    std::printf("      rlimits=%d\n", (int)mhost.rlimits);
    // 理由与下文kvm相同:"we do not implement a microvm"和
    // "this host could not run one anyway"之间的差值。
    std::printf("      hypervisor=%d%s\n", (int)mhost.hypervisor,
                mhost.hypervisor ? "  (a microvm backend COULD run here)" : "");
#else
    const auto host = ::clay::probe_host();
    std::printf("host: userns=%d mountns=%d pidns=%d netns=%d\n",
                (int)host.user_namespaces, (int)host.mount_namespaces,
                (int)host.pid_namespaces, (int)host.net_namespaces);
    std::printf("      landlock_abi=%u seccomp=%d notify=%d\n",
                host.landlock_abi, (int)host.seccomp,
                (int)host.seccomp_user_notif);
    std::printf("      cgroups=%d mem=%d pids=%d cpu=%d\n",
                (int)host.cgroups, (int)host.cgroup_memory,
                (int)host.cgroup_pids, (int)host.cgroup_cpu);
    // kvm 不被进程后端使用, 它被打印, 因为它是"we do not implement a microvm"
    // 和"this host could not run one anyway"的
    // 区别 -- 两个非常不同的答案,
    // 为什么host.kernel_isolation是无的。
    std::printf("      kvm=%d%s\n", (int)host.kvm,
                host.kvm ? "  (a microvm backend COULD run here)" : "");
#endif

    // 1. 决 议 SHIPPED 默认: 从未打开过 面板 得到。 获得。
    //    这是最重要的数字, 因为几乎每个人
    //    都在运行。
    // 1. 用户如果从未打开面板,会得到什么:
    //    默认,通过 相同 配置 - > 姿态构建器 运行时 使用时。
    {
        const cfgn::Config def{};
        audit_cfg("shipped default (no pane visit)", def);
    }

    // 2. 每一个被命名的姿态,再次通过真正的建设者而不是一个
    //    镜像。如果两个标签倒塌到同一墙壁上,
    //    产品预设的梯子也倒塌了。
    for (const auto posture : {cfgn::Posture::Permissive,
                               cfgn::Posture::Balanced,
                               cfgn::Posture::Hardened,
                               cfgn::Posture::Airgapped}) {
        auto cfg = cfgn::apply_posture(cfgn::Config{}, posture);
        cfg.configured = true;
        const std::string label = std::string{"preset: "} + cfgn::to_string(posture);
        audit_cfg(label.c_str(), cfg);
    }

    // 3. 每一项目 面板 上限:如果
    //    能力是: 能力是 . `none` 此处, agentty 根本无法实现,
    //    而这是真正的差距,而不是默认。
    {
        cfgn::Config cfg;
        cfg.configured = true;
        cfg.syscall_mode = cfgn::SyscallMode::Strict;
        cfg.net_mode = cfgn::NetMode::None;
        cfg.wx_protect = true;
        cfg.scope_ipc = true;
        cfg.close_inherited_fds = true;
        cfg.tmp_mb = 64;
        cfg.memory_mb = 512;
        cfg.max_procs = 256;
        cfg.cpu_percent = 100;
        cfg.max_open_files = 1024;
        cfg.cpu_secs = 60;
        cfg.wall_clock_secs = 120;
        cfg.fake_hostname = true;
        audit_cfg("everything the pane can ask for", cfg);
    }

    sb::reset_config_for_test();

#if defined(_WIN32)
    // 4. LIVE 产卵,通过运出路径, 与运出姿态。
    // 职等以上审计 编译-time 墙壁,这解答了墙壁不能的问题:
    // 真的孩子在墙壁上生存吗? 此 主机? 执行链失败( dll
    // init 期间为 0xc000142) 从未在任何中出现。
    // 编译-time 因此审计现在衡量的是 运行时 也一样。
    //
    // argv[1] 可选择将ONE变量隔离到每跑( 其余的则保留已发送
    // 的默认值 ) :
    //   no-net net_mode = 无(无信号中的能力SID) No
    //   -wx wx_protect = 假
    const char* isolate = argc > 1 ? argv[1] : "";
    {
        sb::set_config(cfgn::Config{});
        auto posture = sb::claybin_posture_for_test();
        if (std::string_view{isolate} == "no-net") posture.net_mode = 1;
        if (std::string_view{isolate} == "no-wx") posture.wx_protect = false;
        std::printf("\n=== live spawn (shipped posture%s%s)\n",
                    *isolate ? " isolate=" : "", isolate);
        std::fflush(stdout);
        // 探测器的判断 IN 此 PROCESS 在产卵前... caps_print
        // (独立)报告 restricted_tokens =0 刺伤,
        // 而下面的产卵 运行与 rt_used =1;此标记 对应的 调用 不同意这一点。
        {
            const auto pv = ::clay::windows::probe_host();
            std::printf("  probe: ac=%d rt=%d mit=%d build=%u\n",
                        pv.app_container, pv.restricted_tokens,
                        pv.mitigation_policies, pv.build);
        }
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        HANDLE r = nullptr, w = nullptr;
        if (!::CreatePipe(&r, &w, &sa, 0)) {
            std::printf("  pipe: gle=%lu\n", ::GetLastError());
            return 1;
        }
        const int wfd = static_cast<int>(reinterpret_cast<intptr_t>(w));
        auto res = cb::spawn_argv(posture,
            {"cmd.exe", "/d", "/s", "/c", "echo audit-live"}, wfd, wfd, {});
        ::CloseHandle(w);  // 写完之后,读者就永远看不到EOF
        if (!res.started) {
            std::printf("  spawn failed: %s\n", res.start_error.c_str());
            ::CloseHandle(r);
            return 1;
        }
        std::printf("  pid=%d\n", res.pid);
        char buf[512]{};
        DWORD n = 0;
        std::string out;
        while (::ReadFile(r, buf, sizeof buf, &n, nullptr) && n) out.append(buf, n);
        ::CloseHandle(r);
        DWORD code = 0;
        HANDLE ph = reinterpret_cast<HANDLE>(
            static_cast<intptr_t>(res.pidfd));
        ::WaitForSingleObject(ph, 20000);
        ::GetExitCodeProcess(ph, &code);
        std::printf("  exit=%lu (%s) output='%.*s'\n", code,
                    code == 0 ? "RUNS" : "DIED  (0x hex below)",
                    static_cast<int>(out.size()), out.c_str());
        std::printf("  exit-hex=0x%08lx\n", code);
        if (res.kill_tree) (void)res.kill_tree();
        // NOTE : pidfd是上面等待时借用的原始手柄; agentty
        // 适配器通过 job/kill_tree 真实运行中关闭。
        // 审计是一个工具, 不是泄漏 -free 绳子。
    }
#endif

    std::filesystem::remove_all(ws);
}
