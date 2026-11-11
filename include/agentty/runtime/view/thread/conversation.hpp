#pragma once
#include <maya/widget/conversation.hpp>
#include "agentty/runtime/model.hpp"

namespace agentty::ui {

// 构建对话配置——Turn::Configs列表（视图窗口中的
// 每个可见消息一个）加上可选的底部in-flight指示符。
// 当 线程 为非 -empty 时，从 thread_config
// () 调用。
[[nodiscard]] maya::Conversation::Config conversation_config(const Model& m);

} // namespace agentty::ui
