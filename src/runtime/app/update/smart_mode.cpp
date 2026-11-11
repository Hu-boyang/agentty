// smart_mode.cpp 智能模式 面板's 状态更新函数:
// open/close (descend/ 升华,先进者 -rows 切换 键 分派
// 通过共享形式层,槽手 -off 切换到模型 选择面板 ,并重设档位。
//
// build_smart_form / apply_smart 留在原地,继续 meta.cpp (与 models.cpp 财务报告和
// 已审计财务报表 ascend() ; 已宣布为 internal.hpp.

#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/app/update.hpp"

#include <algorithm>
#include <type_traits>
#include <utility>
#include <variant>

#include <maya/core/overload.hpp>

#include "agentty/runtime/panel/smart_form.hpp"
#include "agentty/runtime/settings_registry.hpp"   // 调制行写入-back

namespace pn = agentty::ui::panel;

namespace agentty::app::detail {

using maya::overload;

Cmd smart_mode_update(Model& m, msg::SmartModeMsg sm) {
    return std::visit(overload{
        [&](OpenSmartMode& e) -> Cmd {
            // descend() : 任何打开的现在( 盘子、
            // 设置列表、 无) 都变成这样 面板's Esc
            // 目标自动 —— 不再使用来源的呼叫器邮票。
            // 高级行的搜索会随显示的行开始。
            const auto* d = settings::registry::find(e.focus);
            const bool advanced = d && d->tier == settings::registry::Tier::Advanced;
            m.ui.panel.descend(pn::SmartMode{{}, build_smart_form(m, advanced), advanced});
            (void)form::focus_row(m.ui.panel.get<pn::SmartMode>()->form, e.focus);
            return Cmd::none();
        },
        [&](SmartModeAdvanced) -> Cmd {
            // Reveal/hide 路径 -policy 排行,排行,
            // 排行 SET 变化,所以形式重建;光标被保存和夹住,
            // 因为隐藏行可以将其离开尾端.
            auto* o = m.ui.panel.get<pn::SmartMode>();
            if (!o) return Cmd::none();
            o->advanced = !o->advanced;
            form::rebuild(o->form, build_smart_form(m, o->advanced));
            return Cmd::none();
        },
        [&](CloseSmartMode) -> Cmd {
            // Esc 解风 一个 关卡,打开它就打开它 面板
            // — ^S 从 线程 关闭; 调色板行或设置
            // 列表会恢复完整状态( query, 光标)。
            ascend(m);
            return Cmd::none();
        },
        [&](SmartModePaste& e) -> Cmd {
            // 粘贴在编辑会话中;当编辑结束时,它会承诺,
            // 与带类型的数字相同。
            auto* o = m.ui.panel.get<pn::SmartMode>();
            if (o) form::paste_into_focused(o->form, e.text);
            return Cmd::none();
        },
        [&](SmartModeKey& e) -> Cmd {
            auto* o = m.ui.panel.get<pn::SmartMode>();
            if (!o) return Cmd::none();

            // 光标上的内容 之前 共享的 状态更新函数 运行——
            // 激活可能会交接,我们需要知道要求的哪一行.
            const auto* row = o->form.focused();
            const std::string row_id = row ? row->id : std::string{};
            const auto role = smart_form::role_of_field(row_id);

            const auto applied = form::keys::apply(o->form, e.action);

            if (applied.close) {
                // 路线通过 CloseSmartMode 而不是
                // 在这里关闭: 读出源,所以直接关闭会发出 Esc
                // 编号 线程 不论如何 面板 打开了,
                // 关闭一个的两种方式 面板 两种行为是保持步调一致。
                return smart_mode_update(m, msg::SmartModeMsg{CloseSmartMode{}});
            }

            // 一旦一个数值结算, 提交( 在 -place 更改,
            // 或结束于 带类型的 编辑。提交到每个数字上重建中间的窗体
            // -edit 并抛弃了编辑。
            const std::string settled_id = applied.settled >= 0
                ? o->form.fields[static_cast<std::size_t>(applied.settled)].id
                : std::string{};
            const auto settled_role = smart_form::role_of_field(settled_id);
            if (!settled_id.empty() && !settled_role) {
                if (const auto* d = settings::registry::find(settled_id)) {
                    smart::RoleConfig cfg = m.d.smart;
                    if (const auto* f = o->form.find(settled_id))
                        (void)settings::registry::read_row(cfg, *d, *f);
                    auto save = apply_smart(m, std::move(cfg));
                    form::rebuild(o->form, build_smart_form(m, o->advanced));
                    return save;
                }
                if (settled_id != smart_form::kFieldEnabled) return Cmd::none();
                // 师傅 切换。通过 apply_smart
                // 类似其他的配置变化,所以副剂路由器也被推了——
                // 在一时打开智能模式 `task` 被排队用来离开
                // 这个 工作线程 路由仿佛它仍然关闭。
                smart::RoleConfig cfg = m.d.smart;
                cfg.enabled = smart_form::enabled_from_form(o->form,
                                                            m.d.smart.enabled);
                auto save = apply_smart(m, std::move(cfg));
                // 槽的锁定状态取决于开关,所以重建.
                form::rebuild(o->form, build_smart_form(m, o->advanced));
                return Cmd::batch(std::move(save), set_status_toast(m,
                    m.d.smart.enabled ? "Smart Mode on" : "Smart Mode off"));
            }

            // 插槽排 ~ 交给模型 选择面板。候选者是整个目录,
            // 这正是一个 `Pick` 行表示:
            // 太大了,而且太动态了,不能内在的下降。
            if (applied.hand_off && role) {
                // descend() : 数据 整个 SmartMode
                // 面板 ——窗体,光标,高级 标志 ,它自己的母链——在
                // 选择面板's `from` 快照; Esc 或者一个完整的针
                // 头可以逐字恢复它。没有停在 Model::UI 再来一次
                pn::Models picker{{0, ""}, {}, *role};
                m.ui.panel.descend(std::move(picker));
                return models_update(m, msg::ModelsMsg{OpenModels{}});
            }

            // 'x' 将焦点槽重置为自动.
            if (e.action.intent == form::keys::Intent::ResetField && role) {
                return smart_mode_update(m, msg::SmartModeMsg{SmartModeClearSlot{}});
            }
            return Cmd::none();
        },
        [&](SmartModeClearSlot) -> Cmd {
            auto* o = m.ui.panel.get<pn::SmartMode>();
            if (!o) return Cmd::none();
            // `x` 只在槽里写着什么 role_of_field
            // 返回主开关的无选择,所以 "not a slot"
            // 无法像一个内在比较那样陷入一个槽。
            const auto* row = o->form.focused();
            if (!row || row->locked) return Cmd::none();
            const auto role = smart_form::role_of_field(row->id);
            if (!role) return Cmd::none();
            // 重置以自动通过 一个 输入点,所以子剂路由
            // 器也会丢失标针。 工作线程 上面的路由和错误
            // 是同一类的 保存-without-apply.
            smart::RoleConfig cfg = m.d.smart;
            cfg.slot(*role) = smart::SlotOverride{};
            auto save = apply_smart(m, std::move(cfg));
            // 显示 RESOLVED 模型,它刚刚改变了。
            form::rebuild(o->form, build_smart_form(m, o->advanced));
            return Cmd::batch(std::move(save),
                              set_status_toast(m, "slot reset to auto"));
        },
    }, sm);
}

} // namespace agentty::app::detail
