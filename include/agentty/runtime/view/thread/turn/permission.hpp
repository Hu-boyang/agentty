#pragma once
#include <maya/widget/permission.hpp>
#include "agentty/runtime/model.hpp"

namespace agentty::ui {

// 从 待处理 权限 +其门控工具 调用 构建 权限 小部件配置。
// 纯粹的数据提取——没有 Element 工作。
[[nodiscard]] maya::Permission::Config inline_permission_config(
    const PendingPermission& pp, const ToolUse& tc);

} // namespace agentty::ui
