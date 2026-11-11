#pragma once
// agentty::ui::view —顶部-level组成。
//
// 将 per-panel 视图组装到根元素中。  所有 覆盖面板 逻辑（模态、
// 选择面板）都位于此处，因此每个 -panel 文件保持专注。

#include <maya/maya.hpp>
#include "agentty/runtime/model.hpp"

namespace agentty::ui {

[[nodiscard]] maya::Element view(const Model& m);

} // namespace agentty::ui
