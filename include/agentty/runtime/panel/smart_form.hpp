#pragma once
// agentty::smart_form-作为form::Form的智能模式。
//
// 智能模式的覆盖面板是手-built 选择面板，其具有自己的行枚举、
// 自己的游标算术、自己的键处理和自己的
// "this is pinned by an env var"特例。现在，
// 所有四个都来自共享表单层，因此它的行为与嵌入 面板 以及每个未来的
// 配置表面相同 - 一个键映射、一个 渲染器、一个交互模型。
//
// 这里留下的是唯一的Smart-Mode-specific知识：存在哪些行、
// 角色槽渲染如何以及行如何映射回RoleConfig。
//
// ── 三个插槽是 `Pick` ,没有 `Choice` ─────────────────────────────
// 角色的候选集是整个模型目录：大、提供者-scoped、
// 并且值得搜索。这正是下拉菜单不得 尝试提供服务的情况 -
// 因此插槽上的 Enter 将交给融合模型 选择面板，就像以前一样。
// `Choice` 保留用于真正的枚举。

#include <string>
#include <vector>

#include "agentty/domain/smart_mode.hpp"
#include "agentty/runtime/panel/form.hpp"
#include "agentty/runtime/settings_registry.hpp"   // EnvSnapshot
#include "agentty/store/store.hpp"   // 设置：高级行读取的内容

namespace agentty::smart_form {

// 字段 ids — 状态更新函数 按 id 寻址行，而不是按索引，
// 因为行集是派生的，并且索引会在发生变化时改变含义。
inline constexpr const char* kFieldEnabled  = "enabled";
inline constexpr const char* kFieldStrategic = "strategic";
inline constexpr const char* kFieldImpl      = "implementation";
inline constexpr const char* kFieldUtility   = "utility";

// 高级路由-policy行故意具有NO常量。它们是 注册表 行：它们的 id
// 存在于 settings_registry.hpp 的表中，
// 并通过 registry::find / Owner::Settings
// 遍历到达。 头文件 中的 "smart.complex_threshold"
// 的第二种拼写将是一个必须与表一致且无法对照表进行检查的字符串。

// 一个角色的解析显示标签，以及它是否被固定。
// 面板 计算这些（解析需要目录和活动提供者）；
// 这个头文件只说明了表格需要的内容。
struct SlotView {
    std::string label;     // 现在实际运行的是什么
    bool        pinned = false;
};

struct Inputs {
    bool     enabled = false;
    // Non-empty ⇒ 主开关由 AGENTTY_SMART_MODE 固定，
    // 并且该行必须 渲染 读取 -only 这么说。以前，
    // 切换 看起来很活跃，但默默地拒绝了 Enter，
    // 并举杯祝酒——谎称可编辑的一行比解释为什么不可编辑的一行更糟糕。
    std::string enabled_lock;
    SlotView strategic;
    SlotView implementation;
    SlotView utility;

    // 高级行从中投影的持久配置。 注册表 从中读取 值、
    // 范围、标签、帮助和 env-override
    // 状态 - 这就是为什么过去放在这里的三个整数和三
    // 个 锁 字符串消失了：它们中的每一个都是表
    // 已经知道的内容的副本。
    smart::RoleConfig smart{};

    // 环境变量覆盖哪些行（它们被 渲染 锁定）。
    settings::registry::EnvSnapshot env{};

    // 逐步显示高级行(`a`)。
    bool advanced = false;
};

// 构建面板。纯 - 单元-testable，没有目录，也没有终端。
[[nodiscard]] form::Form build_form(const Inputs& in);

// 角色的row id名称，或nullopt为主交换机。总计，
// 因此调用者不能默默地将 "not a slot" 视为插槽。
[[nodiscard]] std::optional<smart::ModelRole> role_of_field(std::string_view id) noexcept;

// 配置角色的字段id——role_of_field的逆。总计，
// 因此 "re-open the pane on the slot I just set"
// 不需要索引运算，并且当行集更改时不会落在错误的行上。
[[nodiscard]] std::string_view field_of_role(smart::ModelRole r) noexcept;

// 将光标置于配置`r`的行上。当行不存在时，返回 no-op，
// 这是诚实的行为：调用者要求一行，而不是索引，
// 因此缺失的行会将光标保留在原来的位置。
void focus_role(form::Form& f, smart::ModelRole r) noexcept;

// 从表格中读回主开关。
[[nodiscard]] bool enabled_from_form(const form::Form& f, bool fallback) noexcept;

} // namespace agentty::smart_form
