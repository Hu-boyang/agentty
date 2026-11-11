// plugin_disabled_tools_test — 一个已禁用的服务器保留其工具列表.
//
// 错误: 使工具显示的插件失效 整个 工具列表已消失( 已禁用服务
// 器的广告为无生命), 而启动的服务器已禁用, 或单个的服务器
// -excluded 工具,留着,修好 缓存 每个服务器上一个
// -advertised 工具( 工具) s_seen 输入
// plugin_model ),当服务器没有连接时,倒回它——所以 源
// 码树 穿过一个禁用回合继续居住 -trip.
//
// 这驱动器 真实 制作人:连接工作 Stdio 服务器( 集成 缓存),
// 快照 ,禁用它,再 -snapshot 并坚持工具的坚持。

#include "agtest_cap.hpp"
#include "agentty/tool/registry.hpp"
#include "agentty/tool/tool_loading.hpp"
#include "agentty/mcp/client.hpp"
#include "agentty/tool/plugin.hpp"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h>

namespace fs = std::filesystem;
using namespace agentty;

namespace {  // 归并: TU-local (捆绑成 agentty_standalone_tests)
static int g_fails = 0;
static void check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++g_fails;
}

static std::size_t tool_count_for(const std::string& server) {
    auto m = mcp::plugin_model();
    for (const auto& s : m.servers)
        if (s.name == server) return s.tools.size();
    return static_cast<std::size_t>(-1);   // 服务器不存在
}

}  // 名称空间( 归并)

int main() {
#ifdef SIGPIPE   // Windows 没有 SIGPIPE : 管道损坏是写错误
    std::signal(SIGPIPE, SIG_IGN);
#endif
    ::setenv("AGENTTY_MCP_CONNECT_TIMEOUT_MS", "5000", 1);

    // 总是用真品 -connecting date_server 实例
    // 二进制程序。一个脚本的 stdio 服务器证明了在测试的产卵封内连接的片段;
    // 构建的示例是真正的握手的可靠来源。 如果它没有被构建,
    // 则跳过干净( )。 e.g . 部分离职)。
    const char* bin = std::getenv("AGENTTY_DATE_SERVER");
    fs::path date_server = bin && *bin
        ? fs::path{bin}
        : fs::path{"build/third_party/mcp-cpp/examples/date-mcp/date_server"};
    if (!fs::exists(date_server)) {
        // 尝试相对 源码树 根猜想。该 子模块 住在一个 third_party/ ,
        // 所以,我说 agentty ' 正在建设 源码树 镜像那条路;
        // mcp-cpp 自己的独立建筑 源码树 是一个第二家庭。
        for (const char* c : {"third_party/mcp-cpp/examples/date-mcp/date_server",
                              "../build/third_party/mcp-cpp/examples/date-mcp/date_server",
                              "third_party/mcp-cpp/build/examples/date-mcp/date_server",
                              "../third_party/mcp-cpp/build/examples/date-mcp/date_server"}) {
            if (fs::exists(c)) { date_server = c; break; }
        }
    }
    if (!fs::exists(date_server)) {
        std::printf("  (skip: date_server example not built — set "
                    "AGENTTY_DATE_SERVER)\nAll disabled-tools tests passed.\n");
        return 0;
    }
    date_server = fs::absolute(date_server);

    auto tmp = fs::temp_directory_path()
             / ("agentty_disabled_tools_" + std::to_string(::getpid()));
    fs::remove_all(tmp);
    fs::create_directories(tmp / ".agentty");
    // 隔绝通过 AGENTTY_HOME, 不 $HOME.
    // user_root () 决 定 $AGENTTY_HOME 首次
    // 只会回到 $HOME ——和折叠式独立主机组. $AGENTTY_HOME
    // 给自己的钱 沙箱 整个 二进制程序。所以,测试只重定向 $HOME 写
    // 它 mcp.json 输入一个没有读过的目录: 服务器从未出现在模型中,
    // 这里的每个断言都报告 "server absent" 哨兵
    // ( 数) size_t )-1 而不是真正的工具数。设置两者,
    // 因此测试是否正确,是否带针 AGENTTY_HOME.
    ::setenv("AGENTTY_HOME", (tmp / ".agentty").c_str(), 1);
    ::setenv("HOME", tmp.c_str(), 1);
    ::unsetenv("USERPROFILE");

    const fs::path cfg = tmp / ".agentty" / "mcp.json";
    {
        std::ofstream f(cfg);
        f << R"({"mcpServers":{"demo":{"command":")" << date_server.string()
          << R"("}}})";
    }
    // date_server 广告宣传2种工具( current_date, days_between).
    constexpr std::size_t kN = 2;

    // 1. 连接 – 服务器发布其工具广告;快照记录这些工具.
    tools::warm_registry();          // 触发器connect_initial_mcp
    (void)tools::reload_mcp_plugins(); // 确保新连接的池
    const std::size_t connected = tool_count_for("demo");
    check(connected == kN,
          "connected server advertises its tools (got "
          + std::to_string(connected) + ")");

    // 2. 禁用整个服务器,重新装入(不再连接).
    check(tools::plugin::set_server_disabled(::agentty::IoAccess::grant(), agtest::cap(), cfg, "demo", true)
              == tools::plugin::EditResult::Ok, "disable succeeds");
    (void)tools::reload_mcp_plugins();

    // 3. 该 FIX : 已禁用的服务器仍然列出其工具(来自 缓存),
    //    而不是崩溃为零。
    const std::size_t disabled = tool_count_for("demo");
    check(disabled == kN,
          "disabled server KEEPS its tools in the snapshot (got "
          + std::to_string(disabled) + ") — no vanishing tree");

    // 4. Re-enable → 恢复生活,仍为 kN.
    check(tools::plugin::set_server_disabled(::agentty::IoAccess::grant(), agtest::cap(), cfg, "demo", false)
              == tools::plugin::EditResult::Ok, "re-enable succeeds");
    (void)tools::reload_mcp_plugins();
    const std::size_t reenabled = tool_count_for("demo");
    check(reenabled == kN,
          "re-enabled server shows its tools again (got "
          + std::to_string(reenabled) + ")");

    // 5. A DISABLED 服务器的缓存工具正在显示 -only : 他们从不计数
    //    以 已加载 或按要求(#98),且注释者从未在它们上留下标记.
    {
        auto m_on = mcp::plugin_model();
        tools::tool_loading::annotate(m_on);
        (void)tools::plugin::set_server_disabled(::agentty::IoAccess::grant(), agtest::cap(), cfg, "demo", true);
        (void)tools::reload_mcp_plugins();
        auto m_off = mcp::plugin_model();
        tools::tool_loading::annotate(m_off);
        check(m_off.enabled_mcp() + kN == m_on.enabled_mcp(),
              "disabling drops the plugin tool count by its tool count "
              "(on=" + std::to_string(m_on.enabled_mcp()) + " off="
              + std::to_string(m_off.enabled_mcp()) + ")");
        bool any_marked = false;
        for (const auto& s : m_off.servers)
            if (s.name == "demo")
                for (const auto& t : s.tools) if (t.on_demand) any_marked = true;
        check(!any_marked, "a disabled server's tools are never marked on demand");
        (void)tools::plugin::set_server_disabled(::agentty::IoAccess::grant(), agtest::cap(), cfg, "demo", false);
        (void)tools::reload_mcp_plugins();
    }

    // 6. 命令 -less 服务器表面 an ERROR 而不是永恒的 "connecting…".
    {
        std::ofstream f(cfg);
        f << R"({"mcpServers":{"broken":{}}})";
        f.close();
        (void)tools::reload_mcp_plugins();
        auto m = mcp::plugin_model();
        bool found = false;
        for (const auto& s : m.servers)
            if (s.name == "broken") { found = true;
                check(!s.error.empty() && !s.connected,
                      "command-less server is an error, not connecting forever");
            }
        check(found, "command-less server still appears in the model");
    }

    fs::remove_all(tmp);
    if (g_fails == 0) { std::printf("\nAll disabled-tools tests passed.\n"); return 0; }
    std::printf("\n%d disabled-tools test(s) FAILED.\n", g_fails);
    return 1;
}
