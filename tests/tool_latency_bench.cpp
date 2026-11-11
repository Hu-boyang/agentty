// tool_latency_bench - 墙 -clock 热力的耐久性 -in 工具。
//
// 驱动每个工具通过 注册表( 相同的 ToolDef::execute 代理环 调用) ,
// 而对应 源码树, N 运行, 并打印 p50 / p90 / 最大微秒。
// 不是 pass/fail 测试: 用于 "is the tool layer fast?"
// 的测量棒。 将其指向 源码树 ,
//   与 agentty_standalone_tests tool_latency_bench
// [工作区] [运行] (默认: 当前目录, 30 运行)。

#include "agtest_cap.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "agentty/tool/mcp_tools_bridge.hpp"
#include "agentty/tool/registry.hpp"
#include "agentty/tool/tool.hpp"
#include "agentty/tool/util/fs_helpers.hpp"
#include "agentty/tool/util/sandbox.hpp"
#include "agentty/domain/sandbox_config.hpp"

namespace fs = std::filesystem;
using nlohmann::json;
using namespace agentty;

namespace {

struct Case { const char* label; const char* tool; json args; };

long pct(std::vector<long> v, double p) {
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, static_cast<std::size_t>(p * v.size()))];
}

} // 命名空间结束。

int main(int argc, char** argv) {
    const fs::path root = fs::canonical(argc > 1 ? argv[1] : ".");
    const int runs = argc > 2 ? std::atoi(argv[2]) : 30;
    fs::current_path(root);   // 代理运行时的 cwd = 工作区
    ::setenv("AGENTTY_OLLAMA_HOST", "127.0.0.1:1", 1);
    ::unsetenv("AGENTTY_MCP_CONFIG");
    tools::util::set_workspace_root(::agentty::IoAccess::grant(), root);
    // AGENTTY_BENCH_SANDBOX =1: 默认措施 沙箱 ,
    // 当用户运行时(每个程序工具都通过) claybin).
    if (const char* s = std::getenv("AGENTTY_BENCH_SANDBOX"); s && *s == '1') {
        namespace sb = tools::util::sandbox;
        sandbox_cfg::Config cfg;
        cfg.configured = true;
        sb::set_config(cfg);
        if (!sb::init(sb::Mode::On)) { std::puts("no sandbox backend"); return 1; }
        std::printf("%s\n", sb::describe_state().c_str());
    }
    tools::warm_registry();

    // 在其中选择一个真实的文件 源码树 费用 read/outline-style 案件数量。
    std::string some_file = "README.md";
    if (!fs::exists(root / some_file)) {
        for (auto& e : fs::recursive_directory_iterator(root))
            if (e.is_regular_file() && e.path().extension() == ".cpp") {
                some_file = fs::relative(e.path(), root).string(); break;
            }
    }

    const std::vector<Case> cases = {
        {"shell echo",        "shell",    {{"command", "echo hi"}}},
        {"shell sed",         "shell",    {{"command", "sed -n 1,20p " + some_file}}},
        {"read 200 lines",    "read",     {{"path", some_file}, {"limit", 200}}},
        {"grep literal",      "grep",     {{"pattern", "namespace"}, {"output", "count"}}},
        {"grep regex+ctx",    "grep",     {{"pattern", "std::(vector|string)<"}, {"context", "0"}}},
        {"glob *.cpp",        "glob",     {{"pattern", "*.cpp"}}},
        {"list_dir",          "list_dir", {{"path", "."}}},
        {"find_definition",   "find_definition", {{"symbol", "main"}}},
        {"outline",           "outline",  {{"path", some_file}}},
        {"git_status",        "git_status", json::object()},
    };

    std::printf("tool_latency_bench  root=%s  runs=%d\n", root.c_str(), runs);
    std::printf("%-18s %9s %9s %9s  %s\n", "case", "p50 us", "p90 us", "max us", "ok");
    const char* only = std::getenv("AGENTTY_BENCH_ONLY");   // 剖析一个案例
    for (const auto& c : cases) {
        if (only && std::string_view{c.label} != only) continue;
        const auto td = tools::find(c.tool);
        if (!td) { std::printf("%-18s  (tool not registered)\n", c.label); continue; }
        std::vector<long> us;
        bool ok = true;
        (void)agentty::tool::DynamicDispatch::execute_with(agtest::cap(), td, td->name.value, c.args, agentty::tools::CallContext{});   // 温室:第-touch 缓存号,第缓存页
        for (int i = 0; i < runs; ++i) {
            const auto t0 = std::chrono::steady_clock::now();
            auto r = agentty::tool::DynamicDispatch::execute_with(agtest::cap(), td, td->name.value, c.args, agentty::tools::CallContext{});
            const auto t1 = std::chrono::steady_clock::now();
            if (!r.has_value() && ok)
                std::printf("  [%s] %s\n", c.label,
                            r.error().render().substr(0, 160).c_str());
            ok = ok && r.has_value();
            us.push_back(std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
        }
        std::printf("%-18s %9ld %9ld %9ld  %s\n", c.label, pct(us, .5), pct(us, .9),
                    pct(us, 1.0), ok ? "ok" : "ERR");
    }
    return 0;
}
