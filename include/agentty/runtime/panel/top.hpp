#pragma once
// agentty::ui::panel —该 用于面板优先级的路由功能。
//
// 覆盖面板状态本身存在于ONE 变体槽(m.ui.panel-参见
// overlay_state.hpp)中：开放是指派，排他性是结构性的。
// 这个 头文件 回答了插槽无法单独回答的一个剩余问题：WHO
// OWNS 该 KEYBOARD，当插槽的 覆盖面板 与三个故意的非
// -members 共存时 —
//
//   登录（它自己的状态机；验证控制一切）权限（域状态由
//   数据流 状态更新函数 提升到 -turn 中）待办事项
//   （在 UNDER 插槽中保持打开状态的环境 面板）
//
// top(m)按照规范的优先级顺序组成这四个源。
// subscribe.cpp将键路由至顶部(m)； view.cpp
// 渲染 顶部（米）。相同的功能 ⇒ 键和像素不能分开。
// 两个消费者都对 Kind 进行了详尽的开关，因此没有武器的新 覆盖面板
// 是 -Wswitch 警告，而不是无声的死钥匙或隐形的 弹出面板。

#include "agentty/runtime/model.hpp"
#include "agentty/runtime/login.hpp"
#include "agentty/runtime/panel/slot.hpp"

namespace agentty::ui::panel {

// 活动的（最上面的）覆盖面板，按照规范的优先级顺序：
//   1.登录——拥有整个键盘直到认证完成。
//   2.权限——被阻止的工具的y/n必须对任何事情负责。
//   3.插槽——任意打开的专用面板（最多一个，由
//                    施工）。
//   4.todo——环境；优先级最低，大多数键都会失败。
[[nodiscard]] inline Kind top(const Model& m) noexcept {
    if (login::is_open(m.ui.login))         return Kind::Login;
    if (m.d.pending_permission.has_value()) return Kind::Permission;
    if (const Kind k = kind_of(m.ui.panel); k != Kind::None) return k;
    if (pick::is_open(m.ui.todo.open))      return Kind::Todo;
    return Kind::None;
}

} // namespace agentty::ui::panel
