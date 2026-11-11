#pragma once
#include <maya/maya.hpp>
#include "agentty/runtime/model.hpp"

namespace agentty::ui {

// 渲染在-app登录弹出面板。当 `m.ui.login` 关闭
// 时返回空文本元素；调用者的覆盖面板逻辑使用
// `ui::login::is_open`来决定是否绘制它。
[[nodiscard]] maya::Element login_modal(const Model& m);

} // namespace agentty::ui
