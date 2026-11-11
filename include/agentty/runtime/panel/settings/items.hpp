// settings_items.hpp —设置 选择面板 (Ctrl+K→
// Plugins/Commands/Agents/Hooks)的每-concern行模型。
// 从实时加载程序中重新构建每个 调用，因此 选择面板 始终镜像磁盘上的实际内容。

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "agentty/runtime/panel/settings/categories.hpp"
#include "agentty/runtime/panel/settings/list.hpp"
#include "agentty/runtime/panel/form.hpp"
#include <maya/runtime.hpp>
#include "agentty/util/io.hpp"
#include "agentty/util/lock_levels.hpp"

namespace agentty { struct Model; }

namespace agentty::settings {

// 激活行的作用。保持抽象，以便 状态更新函数 打开类型
// 并查看 渲染 提示，没有任何硬-coding 索引。
enum class Action : std::uint8_t {
    None,          // 信息行（无 Enter 操作）
    CycleProfile,  // 一般：Write→Ask→Minimal

    // ── 门 ─────────────────────────────────────────────────
    //
    // OPEN ANOTHER
    // PANE保持连续并由下面的kFirstDoor/kLastDoor括起来的动作。
    // 设置列表用→可供性而不是健康点来绘制这些，因为它们没有健康——
    // 它们会引导到某个地方。
    //
    // 连续，因为箭头曾经是手-listed开关，
    // 并且外观被排除在外：设置中打开最大面板的行
    // 是唯一没有把手的门，读取为碰巧在Enter
    // 上做某事的值行。范围检查不能是案例列表的
    // 一半-updated。
    OpenRag,       // 一般：开启RAG模式选择面板
    OpenAppearance,// 一般：打开外观面板(主题、密度、动作)
    OpenSmart,     // 一般：打开智能模式配置
    OpenSandbox,   // 一般：打开沙箱 面板（墙、系统调用、限制）
    OpenWebSearch, // 一般：开放网页搜索（web_search政策和服务）
    OpenPaste,     // 一般：打开粘贴（Ctrl+V如何读取剪贴板）
    OpenList,      // 一般：打开另一个设置列表(Item::target)

    ToggleChangesStrip, // 一般：show/hide、持久化、"N changes"横幅
    ToggleAutoTitle,    // 总则：用本实用新型命名新聊天记录
    ToggleAutoCompact,  // 一般：在窗口附近自行压缩上下文
    CycleToolLoading,   // 一般：MCP刀具自动/全部/按需装载（#98）
    // (没有RemovePlugin：移除是两个-step `d`→
    // SettingsListRemove流，从来不是Enter动作——一个一个
    // -press破坏性Enter和一个两个-step共存是一个陷阱。参见状态更新函数的激活臂。)
    TogglePlugin,  // 插件：enable/disable，WHOLE服务器（Enter，可逆）
    ToggleTool,    // 插件：enable/disable一个工具(arg=服务器，arg2=裸机)
    ApprovePlugin, // 插件：信任该项目配置，以便其服务器连接
    ApproveHooks,  // Hooks:批准活动的hooks文件

    // ── 外观 ──────────────────────────────────────────────────
    //
    // 每个旋钮一个动作而不是单个CycleUiField
    // (索引)：状态更新函数然后开启名称而不是数字，
    // 因此对行重新排序不能默默地将行重新指向另一设置。
    //
    // 这里的外观是NOT。它是这个列表中的一组循环行——
    // Enter到枚举，其中十一个在平坦的墙上——而且它的
    // 形状两次都是错误的：循环四次到SEE，选项比它们的列表更糟糕，
    // 十一个未分组的切换没有形状可读。现在是 面板 (
    // panel/appearance.hpp) 形式，这是分组、
    // per-row 帮助和出处已经存在的地方。
};

// 是否激活该行OPEN ANOTHER PANE？
//
// 提出一个问题而不是维护一个列表。设置列表使用
// 它来绘制→可供性；任何其他需要区分门和值的
// 东西都可以以同样的方式询问，而不是重述集合。
//
// 上方连续块上的范围，因此在括号之间添加一扇门
// 会拾取箭头，无需第二次编辑——这正是之前出错
// 的地方：外观被添加到枚举，连接到状态更新函数，
// 给出一行，并且离开绘制手柄的一个开关。
[[nodiscard]] constexpr bool opens_pane(Action a) noexcept {
    return a >= Action::OpenRag && a <= Action::OpenList;
}

struct Item {
    // 行 health/status — 驱动 LEADING 徽章，
    // 以便一行一目了然地传达其状态（像所有其他 agentty 状态表面一样），
    // 而不是在徽章中编码 Enter ACTION（健康服务器上的红色
    // ✕ "remove" 读取为错误）。可移除性是通过页脚而不是徽章
    // 来传达的。
    enum class Status : std::uint8_t {
        Neutral,   // 信息/无健康信号
        Ok,        // 健康——连接、批准、存在（●绿色）
        Pending,   // 正在进行中 — 连接…（◌ 暗淡）
        Bad,       // 失败/需要注意 — 错误，未批准（⚠ 琥珀色）
    };

    std::string primary;    // left/main 文本（名称）
    std::string secondary;  // 暗淡细节（命令行、路径、状态）
    std::string hint;       // 右-aligned action/CLI提示
    Action      action = Action::None;
    Status      status = Status::Neutral;
    std::string arg;        // 动作 载荷 (plugin/server 名称)
    std::string arg2;       // 辅助载荷（e.g.裸工具名称）
    Category    target = Category::General;  // 列表打开OpenList行
    bool        indented = false;  // 渲染作为子-row（插件工具）
    bool        on = true;         // 切换状态（对于ToggleTool行）
    bool        pinned = false;    // ToggleTool 行：tools.pin，始终 已加载 (#98)
    // 插件：其工具可以显示(→)或隐藏(←)的服务器行。
    bool        expandable = false;
    bool        expanded = false;
    // 较长的文本仅在其聚焦时显示在该行下方（工具的
    // 描述）。
    std::string detail;
    // 虽然 PRESENT 实际上处于不活动状态：DISABLED（或
    // 未连接）插件下的工具。其单独的 on/off 仍然显示（◉/○），
    // 因此您知道插件返回时会是什么，但整行变暗以表示
    // "this can't run right now." 与 `on`
    // （存储的 切换 状态）不同，因此当组读取为关闭时，切换 保持正确。
    // 仅在工具 sub-rows 上设置。
    bool        inactive = false;
    // plugin/server行的出处：它来自徽章的哪个范围("project"
    // /"user"/"explicit")，以及保存其mcp.json的.agentty目录，
    // 因此编辑(remove/toggle)路由到RIGHT文件而不是
    // 总是用户配置。非 -server 行上为空。
    std::string scope_label;
    std::string config_dir;
    // 其配置尚未受信任的项目-scope服务器——在获得批准之前
    // 不会连接。在这样的行上，Enter APPROVES（授权
    // 信任）而不是切换，并且视图显示了批准可供性。
    bool        untrusted = false;
};

// 一个类别的行，实时。 `m` 供应 profile/RAG/Smart 状态
// for the General category; the rest come from the tools:: loaders.
[[nodiscard]] std::vector<Item> items_for(const Model& m, Category cat);

// ── 设置搜索 ─────────────────────────────────────────────────
// 索引（每个表格面板的每一行）是在集线器打开时由状态更新函数
// 从每个面板自己的表格构建器构建的。这些是纯粹的一半。

// 将一个 面板 的形式转化为命中：每个非 -header 行，以及它所在的
// 头文件。
void index_form(std::vector<ListOpen::Hit>& out, const form::Form& f,
                ListOpen::Pane pane);

// 索引到与`query`匹配的`index`中，最好首先。匹配标签，
// 然后匹配部分、面板 名称和帮助。
[[nodiscard]] std::vector<int> search(const std::vector<ListOpen::Hit>& index,
                                      std::string_view query);

[[nodiscard]] const char* pane_label(ListOpen::Pane p) noexcept;

// 字节在`label`中偏移，其中查询的单词匹配，用于突出显示。
[[nodiscard]] std::vector<int> label_match(std::string_view label, std::string_view query);

// 该列表是否显示搜索结果？只有中心进行搜索，
// 仅对空格进行查询不是搜索。
[[nodiscard]] inline bool is_search(const ListOpen& o) noexcept {
    return o.concern == Category::General
        && o.query.find_first_not_of(' ') != std::string::npos;
}

// ── 以-TUI方式增加流量 ────────────────────────────────────────────────
// 添加尝试的结果：状态 toast 的 human-readable 消息（`ok`
// false ⇒ 这是一个错误）。
struct AddResult { bool ok; std::string message; };

// 插件：解析一个-line "name command [args…]"规格并通过tools::
// plugin将其写入用户mcp.json。 `--python foo.py` /
// `--uvx pkg` / `--npx pkg` 简写形式的扩展与 CLI 完全相同。
[[nodiscard]] AddResult add_plugin_from_line(Io, const lock_levels::below_file_lane&, const std::string& line);

// 命令/代理：使用最小模板在用户根目录（
// ~/.agentty/commands或~/.agentty/agents)下创建启动器
// <name>.md，以便用户可以打开+编辑它。成功时返回 `message` 中的路径。
// 拒绝覆盖现有文件。
[[nodiscard]] AddResult create_starter(Io, Category cat, const std::string& name);

} // namespace agentty::settings
