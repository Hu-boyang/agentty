#pragma once
// 设置选择面板-用于配置关注点的Ctrl+K行，
// 该配置关注点曾经是CLI-/file-only：插件、命令、代理、挂钩。
// 每个都是一个调色板条目，打开 此 一个 选择面板，由 `concern`
// 参数化。单个列表 弹出面板（与其他 选择面板 的形状相匹配）而不是
// 定制的 面板：从 Ctrl+K 中选择关注点，获得重点列表，连续执行。
//
// 这些行是从 settings::items_for(关注点)实时构建的——与
// 运行时 使用的加载器相同——因此 选择面板 总是镜像磁盘。可操作行（删除插件、
// 批准挂钩、打开 RAG/Smart）作用于 Enter；其余的都是信息性的。
// 按键（subscribe.cpp::on_settings_list）：↑↓/jk移动，
// Enter动作，Esc/q关闭。
//
// 仅UI-state； update/settings_list.cpp 中的 状态更新函数，
// 在 view/settings_list_view.cpp. 中查看

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "agentty/runtime/panel/settings/categories.hpp"   // settings::Category

namespace agentty {

namespace settings {

struct ListClosed {};
struct ListOpen {
    Category concern = Category::Plugins;
    int      index   = 0;
    // 内联add-mode：在关注点上按下`a`会在选择面板中打开一个
    // -line文本提示词。清空`input_active`==浏览列表；
    // true == 输入新条目。 提示词 的含义取决于关注点（参见
    // settings_list.cpp）：对于插件，
    // 它是 "name command args…"；对于 Commands/Agents，
    // 它是新文件的名称（创建起始文件 + 显示要编辑的路径）。
    bool        input_active = false;
    std::string input;          // 带类型的 缓冲区
    int         cursor = 0;     // 字节光标进入`input`
    // 插件：工具列表打开的服务器。默认情况下折叠，
    // 因此 75-tool 服务器在您打开之前是一行。
    std::vector<std::string> expanded;
    // 破坏性`d`(删除插件)是两个-step，如帐户删除：
    // 首先按ARMS（此处存储服务器名称+视图绘制"press d again to remove"行）；
    // 第二次按 相同 行提交。任何 move/other 操作
    // 都会解除它的武装。一次偶然的击键永远无法删除 hand
    // -tuned mcp.json 条目而无需撤消。
    std::string confirm_remove;
    // 仅设置中心：带类型的 在每个设置中搜索。空 = 正常的中心行。
    // `index_rows` 为每个设置保存一行，在集线器打开时从每
    // 个 面板 自己的表格中获取，因此搜索绝不会两次列出某个设置，
    // 也不会错过 面板 获得的设置。
    std::string query;
    // 搜索命中存在于哪种形式 面板 中。
    enum class Pane : std::uint8_t { Rag, Smart, Appearance, Sandbox, WebSearch, Paste };
    struct Hit {
        std::string label;      // 行的标签，如 面板 所示
        std::string help;
        std::string value;      // 当前值，作为文本
        std::string section;    // 它所在的 头文件，如果有的话
        Pane        pane = Pane::Rag;
        std::string row;        // 面板 打开的行 ID
    };
    std::vector<Hit> index_rows;
};

} // namespace settings

using SettingsListState =
    std::variant<settings::ListClosed, settings::ListOpen>;

[[nodiscard]] inline bool
settings_list_is_open(const SettingsListState& s) noexcept {
    return std::holds_alternative<settings::ListOpen>(s);
}
[[nodiscard]] inline settings::ListOpen*
settings_list_opened(SettingsListState& s) noexcept {
    return std::get_if<settings::ListOpen>(&s);
}
[[nodiscard]] inline const settings::ListOpen*
settings_list_opened(const SettingsListState& s) noexcept {
    return std::get_if<settings::ListOpen>(&s);
}

} // namespace agentty
