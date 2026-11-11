#pragma once
// agentty::tools::web_search_policy-工具层遵循的web_search策略。
//
// 该策略与其余设置一起保留并在 Web 搜索 面板 中进行编辑，
// 但它是 READ 关闭 UI 线程：由请求构建器决定
// 传输 上的内容，并由在 工作线程 上运行该工具的 分派
// 闭包决定。所以它存在于一个进程-wide槽中。
//
// 写入者：在TUI、ONLY中主机的句柄(PublishWebSearchPolicy)，
// 分派 接口边界(publish_derived)在第一个归并以及每当Model的记录
// 改变时返回该句柄。 状态更新函数 绝不是 调用 install()。
// 其他地方（`agentty run`、ACP、`mcp serve`，没有 Model）入口
// 点 调用 种子（I/O）在启动时一次，它读取 settings.json（应用了环境覆盖）。

#include "agentty/domain/web_search_config.hpp"
#include "agentty/util/io.hpp"

namespace agentty::tools::web_search_policy {

// 加载是来自settings.json的策略，除非已经安装了该策略。
void seed(Io io);

// 现行政策；默认值，直到 seed() 或 install() 运行。
[[nodiscard]] web_search_cfg::Config current();

// 替换现行政策。将 副作用 用于构建下一个请求并
// 调度下一个 调用；已经运行的 调用 不受影响。
void install(web_search_cfg::Config c);

} // namespace agentty::tools::web_search_policy
