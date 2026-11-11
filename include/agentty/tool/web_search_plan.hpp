#pragma once
// agentty::tools::web_search_plan —将用户的搜索设置转变为计划mcp
// -cpp的web_search所遵循的。
//
// 两块，分开，因此有趣的一块不需要IO：
//   build_plan()纯：设置+一键查找->WebSearchPlan
//   make_plan_source()将WebSearchPlanSource桥手
//                     mcp-cpp。它会读取 每个 搜索上的实时策略和密钥存储，
//                     因此设置中的更改会到达下一个 调用，无需重新启动。

#include "agentty/domain/web_search_config.hpp"

#include <mcp/tools/web_search.hpp>

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace agentty::tools::web_search_plan {

using KeyLookup = std::function<std::string(std::string_view service)>;

// 按主要、次要、回退的顺序。设置为 "none" 的插槽、
// 目录不知道的 id（hand-edited 文件、
// 添加的较新版本的服务），或者先前已命名的插槽的服务将被遗漏：
// 在一次搜索中询问同一服务两次只会花费其速率限制。
//
// 每个槽承载为ITS服务存储的拨号，以及当该服务需要拨号时的密钥。
// 没有密钥的密钥服务是故意保留在计划中的，因此搜索报告
// 可以显示 "no API key set"，
// 而不是默默地跳过用户选择的时段。
[[nodiscard]] ::mcp::tools::WebSearchPlan build_plan(const web_search_cfg::Config& c,
                                                const KeyLookup& key_of);

// 实时源：web_search_policy::current()+web_search_secret::load()。
[[nodiscard]] std::shared_ptr<::mcp::tools::WebSearchPlanSource> make_plan_source();

} // namespace agentty::tools::web_search_plan
