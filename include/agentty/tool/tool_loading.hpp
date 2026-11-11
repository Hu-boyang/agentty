#pragma once
// agentty::tools::tool_loading —实时MCP工具-loading设置和tool_search工具
// (#98)。
//
// 与web_search_policy形状相同：该设置与settings.json
// 的其余部分一起保留并在TUI中编辑，但在工作线程轮上读取(TUI、`agentty run`、
// 子代理、ACP)，因此它存在于一个进程-wide槽中。

#include <string>
#include <unordered_set>
#include <vector>

#include "agentty/mcp/plugin_model.hpp"
#include "agentty/provider/provider.hpp"
#include "agentty/tool/registry.hpp"
#include "agentty/tool/tool_plan.hpp"
#include "agentty/util/io.hpp"

namespace agentty::tools::tool_loading {

// 加载 是 settings.json 的设置，除非已经安装了该设置。
void seed(Io io);
[[nodiscard]] plan::Loading current();
void install(plan::Loading l);

// tool_search ToolDef。调用时搜索实时 目录，
// 因此它会看到来自请求构建后连接的服务器的工具。
[[nodiscard]] const ToolDef& search_tool();

// 提供商+模型可以采取什么。 `anthropic` 选择延迟参考；
// `context_window` 0 = 未知。 `model` 决定是否
// 使用 on-demand 加载：不会搜索的小模型会获得适合的所有内容。
[[nodiscard]] plan::Limits limits_for(const provider::Selection& sel,
                                      std::size_t context_window,
                                      std::string_view model = {});

// 每个请求构建器使用的一个入口点：规划本轮的工具并将
// 它们作为传输规范附加到`out`。
//
//   目录所选工具（实时目录，在任何per-caller过滤器之后）已加载
//   tools::plan::loaded_in(线程)
//
// 返回计划，以便调用者可以记录它并且UI可以显示计数。
// 调用者将 `pool` 指向的 目录 保持为 调用 的活动状态。
struct Request {
    std::vector<const ToolDef*> pool;          // 该呼叫者可能使用的工具
    plan::Loaded loaded;
    plan::Limits limits;
    plan::Loading mode = plan::Loading::Auto;
    // 每个服务器的握手指令，显示在该服务器旁边的tool_search的
    // 描述中(mcp::mcp_server_instructions())。
    std::vector<std::pair<std::string, std::string>> server_instructions;
};
plan::Plan plan_into(const Request& r, std::vector<provider::ToolSpec>& out);

// 填充插件 面板 的加载视图（#98）：计数、
// 成本以及按需工具，就像 活动 提供商上的新 线程 会看到
// 它们一样。运行与请求构建器相同的计划器。
void annotate(mcp::PluginModel& m);

} // namespace agentty::tools::tool_loading
