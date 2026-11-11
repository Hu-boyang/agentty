#pragma once
#include <maya/widget/thread.hpp>
#include "agentty/runtime/model.hpp"

namespace agentty::ui {

// 从 Model 构建 线程 小部件配置。纯数据提取 - 小
// 部件拥有所有 渲染 镶边（欢迎屏幕、对话布局、
// in-flight 指示器、per-turn 导轨）。
[[nodiscard]] maya::Thread::Config thread_config(const Model& m);

} // namespace agentty::ui
