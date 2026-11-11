#pragma once
// agentty待办事项域——`todo_write`工具的跟踪
// 任务列表的值类型。  UI 中的 弹出面板 和工具实现共享
// 相同的词汇表。

#include <cstdint>
#include <string>

namespace agentty {

enum class TodoStatus : std::uint8_t { Pending, InProgress, Completed };

struct TodoItem {
    std::string content;
    TodoStatus  status = TodoStatus::Pending;
};

} // namespace agentty
