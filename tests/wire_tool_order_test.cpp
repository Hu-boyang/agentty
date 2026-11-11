// 哪个工具在 传输 ,以及顺序( tools::plan, #98).
//
// 工具块是头部 提示词-cache 前缀,所以其顺序必须仅取决于 目录
// 编辑 线程's 已加载 设置: 永远不要在用户的什么上 带类型的 和。
// 和 : web_search 切换必须只删除 NATIVE 工具,
// 其余的都维持着他们的秩序。

#include "agtest.hpp"

#include "agentty/tool/registry.hpp"
#include "agentty/tool/tool_plan.hpp"

#include <string>
#include <vector>

using agentty::ToolName;
namespace tools = agentty::tools;
namespace tp = agentty::tools::plan;

namespace {

tools::ToolDef mcp_tool(std::string name, std::string desc) {
    tools::ToolDef t;
    t.name = ToolName{std::move(name)};
    t.description = std::move(desc);
    t.origin = tools::ToolOrigin::Mcp;
    t.origin_id = "srv";
    return t;
}

tools::ToolDef native_tool(const char* n) {
    tools::ToolDef t;
    t.name = ToolName{n};
    return t;
}

// 要求的目录, 政策应用了要求构建者的
// 方式( 持有的工具从未到达规划者)。
std::vector<std::string> wire(const std::vector<tools::ToolDef>& cat, bool web_search_on,
                              tp::Loading mode = tp::Loading::Auto) {
    std::vector<tools::ToolDef> pool;
    for (const auto& t : cat)
        if (!tools::withheld_by_policy(t, web_search_on)) pool.push_back(t);
    const auto p = tp::make_plan(pool, {}, nullptr, mode, {});
    std::vector<std::string> out;
    for (const auto& w : p.wire) out.push_back(w.tool->name.value);
    return out;
}

}  // 命名空间结束。

TEST_CASE("wire tool order is catalog order, whatever loads") {
    std::vector<tools::ToolDef> cat{native_tool("read"),
                                    mcp_tool("alpha_tickets", "jira tickets"),
                                    mcp_tool("beta_pages", "confluence pages"),
                                    mcp_tool("gamma_logs", "datadog logs")};
    const auto all = wire(cat, true);
    check(all == std::vector<std::string>{"read", "alpha_tickets", "beta_pages", "gamma_logs"},
          "natives first, then catalog order");
    // A 线程 说 已加载 编号 最后 工具仍然显示 目录 秩序。 秩序。
    const auto p = tp::make_plan(cat, {"gamma_logs"}, nullptr, tp::Loading::OnDemand, {});
    std::vector<std::string> got;
    for (const auto& w : p.wire) got.push_back(w.tool->name.value);
    check(got == std::vector<std::string>{"read", "gamma_logs"}, "loaded set, catalog order");
}

// 设置 ~ 网络搜索可以切换 web_search 关上,它必须离开 传输
// 无需 破坏留下的秩序, 并且只从中移除 NATIVE 工具: a MCP
// 服务器的自有 web_search 受以下规定制约: mcp.json.
TEST_CASE("web_search switch removes only the native tool, order intact") {
    std::vector<tools::ToolDef> cat;
    for (const char* n : {"read", "web_fetch", "web_search", "grep"}) cat.push_back(native_tool(n));
    cat.push_back(mcp_tool("web_search", "a plugin's own search"));

    check(wire(cat, true) == std::vector<std::string>{"read", "web_fetch", "web_search",
                                                      "grep", "web_search"},
          "on: everything ships, catalog order");
    check(wire(cat, false) == std::vector<std::string>{"read", "web_fetch", "grep",
                                                       "web_search"},
          "off: the native web_search is gone, the rest keep their order, "
          "and the MCP tool of the same name is untouched");
}

// `agentty mcp-serve` 注册表 native_registry () 减 withheld_by_policy():
// 编号 相同 请求构建者使用上游,所以另一个 MCP 客户端从未
// 提供工具 agentty 设置关闭了, 用于自己的模型。
TEST_CASE("mcp-serve and the wire withhold exactly the same tools") {
    std::vector<tools::ToolDef> cat;
    for (const char* n : {"read", "web_fetch", "web_search", "grep"}) cat.push_back(native_tool(n));
    cat.push_back(mcp_tool("web_search", "a plugin's own search"));

    for (bool enabled : {true, false}) {
        std::vector<std::string> served;
        for (const auto& t : cat)
            if (!tools::withheld_by_policy(t, enabled)) served.push_back(t.name.value);
        check(served == wire(cat, enabled), "mcp-serve's list and the wire agree, on and off");
    }
    check(tools::withheld_by_policy(cat[2], false), "off withholds native web_search");
    check(!tools::withheld_by_policy(cat[2], true), "on/auto serve it");
    check(!tools::withheld_by_policy(cat[1], false), "web_fetch is never governed");
    check(!tools::withheld_by_policy(cat[4], false), "nor a plugin's tool of the same name");
}
