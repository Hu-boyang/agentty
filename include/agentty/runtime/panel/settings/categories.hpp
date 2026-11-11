#pragma once
// settings_categories-配置问题表现为Ctrl+K行，
// 每行打开共享设置-list 选择面板。只是 枚举 + 标签；行数据位于
// settings_items.hpp，UI 状态位于 settings_list.hpp。

#include <cstdint>

namespace agentty::settings {

// 每个值是共享设置选择面板中的一个列表。
// 一般是根（"Settings"）：几个值加上门行到每隔一个面板和列表。
// 表格面板 (外观、沙箱、智能模式……)不是类别，
// 它们只能通过普通门到达。
enum class Category : std::uint8_t {
    General,   // 设置中心
    Plugins,   // MCP服务器(mcp.json):列出+删除
    Commands,  // 斜线命令：发现列表(read-only)
    Agents,    // 用户子代理:发现列表(read-only)
    Hooks,     // 生命周期挂钩：文件+批准状态+批准动作
};

[[nodiscard]] constexpr const char* label(Category c) noexcept {
    switch (c) {
        case Category::General:  return "Settings";
        case Category::Plugins:  return "Plugins";
        case Category::Commands: return "Commands";
        // "Subagents"，而不是 "Agents"，以消除与 AGENTS.md 项目指导
        // 标准 (agents.md) 的歧义。这些是任务工具的委托 PERSONAS
        // (.agentty/agents/*.md)； AGENTS.md 是一个项目文档。
        case Category::Agents:   return "Subagents";
        case Category::Hooks:    return "Hooks";
    }
    return "?";
}

// 每个选择面板有一个-line副标题（显示在标题下）。
[[nodiscard]] constexpr const char* subtitle(Category c) noexcept {
    switch (c) {
        case Category::General:  return "behaviour, look, safety and extensions";
        case Category::Plugins:  return "MCP servers \xc2\xb7 agentty plugin add \xe2\x80\xa6";
        case Category::Commands: return "slash commands \xc2\xb7 .agentty/commands/*.md";
        case Category::Agents:   return "subagents \xc2\xb7 .agentty/agents/*.md";
        case Category::Hooks:    return "lifecycle hooks \xc2\xb7 .agentty/hooks.json";
    }
    return "";
}

} // namespace agentty::settings
