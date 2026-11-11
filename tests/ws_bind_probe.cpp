// ws_bind_probe - 做吗? SAVED 沙箱 保险真的到达墙壁?
//
// 有两个问题,当有问题被打破时,他们有不同的答案:
//
//   1. ~/.agentty/settings.json 回合-trip 至load_settings()是否?
//   2. load_settings() 返回时的配置是否实际塑造了
//      沙箱 一个真实 `bash` 调用 着陆?
//
// 驱动在两个计数上通过SHIPPED路径:persistence::load_settings
// ()然后sandbox::set_config()在
// main.cpp使用的顺序中,然后是run_shell_command().
// 镜像的姿态将证明什么也没有——被调查的bug正是已保存块和活墙不同意.

#include "agentty/tool/util/sandbox.hpp"
#include "agentty/tool/util/fs_helpers.hpp"
#include "agentty/domain/sandbox_config.hpp"
#include "agentty/util/user_root.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace sb = agentty::tools::util::sandbox;
namespace sc = agentty::sandbox_cfg;

namespace {

const char* net_name(sc::NetMode m) {
    switch (m) {
        case sc::NetMode::Full:  return "Full";
        case sc::NetMode::None:  return "None";
        case sc::NetMode::Ports: return "Ports";
    }
    return "?";
}

const char* scope_name(sc::FsScope s) {
    switch (s) {
        case sc::FsScope::Minimal:   return "Minimal";
        case sc::FsScope::Toolchain: return "Toolchain";
        case sc::FsScope::HostReadable:  return "HostReadable";
    }
    return "?";
}

}  // 命名空间结束。

// 镜像 persistence.cpp's 沙箱 读取器。
// persistence.cpp 拖动整个 thread/auth/registry
// 后面的图表; 此文件只需要一个块。 如果两者有分歧 该 本身
// 就是发现——所以每个字段都用相同的键读取,同样用相同的键来读取
// 回退-to-current-value 规则。
sc::Config read_saved_sandbox() {
    sc::Config c;
    std::ifstream ifs(agentty::util::user_root() / "settings.json");
    if (!ifs) { std::printf("  (no settings.json)\n"); return c; }
    nlohmann::json j;
    try { ifs >> j; } catch (...) { std::printf("  (unparseable)\n"); return c; }
    if (!j.contains("sandbox") || !j["sandbox"].is_object()) {
        std::printf("  (no sandbox block)\n");
        return c;
    }
    const auto& b = j["sandbox"];
    c.configured = b.value("configured", true);
    c.fs_scope = static_cast<sc::FsScope>(
        b.value("fs_scope", static_cast<int>(c.fs_scope)));
    c.net_mode = static_cast<sc::NetMode>(
        b.value("net_mode", static_cast<int>(c.net_mode)));
    c.syscall_mode = static_cast<sc::SyscallMode>(
        b.value("syscall_mode", static_cast<int>(c.syscall_mode)));
    c.memory_mb       = b.value("memory_mb", c.memory_mb);
    c.max_procs       = b.value("max_procs", c.max_procs);
    c.cpu_secs        = b.value("cpu_secs", c.cpu_secs);
    c.wall_clock_secs = b.value("wall_clock_secs", c.wall_clock_secs);
    c.mask_scan_depth = b.value("mask_scan_depth", c.mask_scan_depth);
    if (b.contains("allow_ports") && b["allow_ports"].is_array()) {
        c.allow_ports.clear();
        for (const auto& v : b["allow_ports"])
            if (v.is_number_integer()) c.allow_ports.push_back(v.get<int>());
    }
    return c;
}

int main(int argc, char** argv) {
    const std::string ws = argc > 1 ? argv[1]
                                    : std::filesystem::current_path().string();
    agentty::tools::util::set_workspace_root(::agentty::IoAccess::grant(), ws);

    // ── 1. 什么是 ON DISK ──────────────────────────────────────────────
    const auto saved = read_saved_sandbox();
    std::printf("SAVED policy (~/.agentty/settings.json)\n");
    std::printf("  configured     %s\n", saved.configured ? "true" : "false");
    std::printf("  fs_scope       %s\n", scope_name(saved.fs_scope));
    std::printf("  net_mode       %s\n", net_name(saved.net_mode));
    std::printf("  memory_mb      %d\n", saved.memory_mb);
    std::printf("  max_procs      %d\n", saved.max_procs);
    std::printf("  cpu_secs       %d\n", saved.cpu_secs);
    std::printf("  wall_clock_secs %d\n", saved.wall_clock_secs);
    std::printf("  mask_scan_depth %u\n", saved.mask_scan_depth);

    // ── 2. 封印,确切地说 main.cpp 已经 ────────────────────────────
    sb::set_config(saved);
    sb::init(sb::Mode::On);

    const auto live = sb::config();
    std::printf("\nLIVE policy (sandbox::config() after set_config + init)\n");
    std::printf("  net_mode       %s\n", net_name(live.net_mode));
    std::printf("  memory_mb      %d\n", live.memory_mb);
    std::printf("  state          %s\n", sb::describe_state().c_str());
    std::printf("  enforceable    %s\n",
                sb::config_enforceable() ? "yes" : "NO (backend cannot apply it)");

    const bool match = live.net_mode == saved.net_mode
                    && live.memory_mb == saved.memory_mb
                    && live.max_procs == saved.max_procs;
    std::printf("\n  saved == live  %s\n", match ? "YES" : "NO  <-- the bug");

    // ── 3. LIVE政策是否真正形成真正的命令? ──────────
    std::printf("\nOBSERVED in a real run_shell_command child\n");
    struct Probe { const char* label; const char* cmd; };
    const Probe probes[] = {
        {"network reachable", "curl -sS -m 5 -o /dev/null -w '%{http_code}' "
                              "https://example.com 2>&1 | tail -1"},
        {"memory cap",        "cat /sys/fs/cgroup/memory.max 2>/dev/null "
                              "|| echo '(no cgroup memory.max)'"},
        {"pid cap",           "cat /sys/fs/cgroup/pids.max 2>/dev/null "
                              "|| echo '(no cgroup pids.max)'"},
    };
    for (const auto& p : probes) {
        auto r = sb::run_shell_command(p.cmd, 8192, std::chrono::seconds{20});
        std::string out = r.output;
        if (const auto at = out.find("\n[sandbox]"); at != std::string::npos)
            out.resize(at);
        while (!out.empty() && (out.back() == '\n' || out.back() == ' '))
            out.pop_back();
        std::printf("  %-18s %s\n", p.label, out.c_str());
    }
    return match ? 0 : 1;
}
