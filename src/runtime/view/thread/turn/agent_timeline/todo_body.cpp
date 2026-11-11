// tool_body_preview — `todo` 工具 正文.
//
// 结构化复选框列表( E) TodoList 拥有图标。当参数不携带非
// 时返回假 -empty `todos` 数组,让 分派器 离职
// Kind::None.
//
// NOTE 命令时: 在 分派器 选中此选项 之后 共享失败
// 回退 ,所以一个 FAILED 待办事宜 渲染 是一个普通
// 错误 正文 ,而不是复选框列表——匹配原始的单曲控制流.

#include "tool_body_common.hpp"

#include <string>
#include <utility>

namespace agentty::ui::detail {

bool todo_body(const ToolUse& tc, maya::ToolBodyPreview::Config& out) {
    using Kind = maya::ToolBodyPreview::Kind;
    if (!tc.args.is_object()) return false;
    auto it = tc.args.find("todos");
    if (it == tc.args.end() || !it->is_array() || it->empty()) return false;

    using Status = maya::ToolBodyPreview::TodoItem::Status;
    out.kind = Kind::TodoList;
    out.todos.reserve(it->size());
    for (const auto& td : *it) {
        if (!td.is_object()) continue;
        auto string_field = [&](const char* key) -> std::string {
            auto field = td.find(key);
            return field != td.end() && field->is_string()
                ? field->get<std::string>() : std::string{};
        };
        auto content = string_field("content");
        if (content.empty()) continue;
        Status s = Status::Pending;
        auto st = string_field("status");
        if      (st == "completed")   s = Status::Completed;
        else if (st == "in_progress") s = Status::InProgress;
        out.todos.push_back({std::move(content), s});
    }
    return true;
}

} // namespace agentty::ui::detail
