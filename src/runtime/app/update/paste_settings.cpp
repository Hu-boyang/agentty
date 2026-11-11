// paste_settings.cpp—设置→粘贴：Ctrl+V如何读取剪贴板。
//
// 粘贴一张表格面板。*注册表行。提交的编辑写入
// m.d.persisted.paste，返回 保存，并从 Model 重建行。
// 告诉剪贴板阅读器是publish_derived的工作(PublishPasteConfig)，
// 与Web搜索的策略到达工具层的方式相同。

#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/app/update.hpp"

#include <string>
#include <utility>
#include <variant>

#include <maya/core/overload.hpp>

#include "agentty/runtime/settings_registry.hpp"

namespace pn  = agentty::ui::panel;
namespace reg = agentty::settings::registry;

namespace agentty::app::detail {

using maya::overload;

form::Form paste_settings_form(const paste_cfg::Config& c, const reg::EnvSnapshot& env) {
    form::Builder b{" Paste "};
    b.subtitle("how Ctrl+V reads the clipboard \xc2\xb7 `agentty paste doctor` shows the route it takes");
    bool first = true;
    auto last_group = reg::Group::PasteRoutes;
    // 九行：全部显示而不是隐藏高级的。
    reg::add_rows(b, c, env, reg::Owner::Paste, /*高级=*/true, first, last_group);
    return b.build();
}

form::Form paste_form_for_search(const Model& m) {
    return paste_settings_form(m.d.persisted.paste, m.env.settings);
}

namespace {

void rebuild(const Model& m, pn::PasteSettings& o) {
    form::rebuild(o.form, paste_settings_form(m.d.persisted.paste, m.env.settings));
}

} // 命名空间结束。

Cmd paste_settings_update(Model& m, msg::PasteSettingsMsg pm) {
    return std::visit(overload{
        [&](OpenPasteSettings& e) -> Cmd {
            pn::PasteSettings o;
            o.form = paste_settings_form(m.d.persisted.paste, m.env.settings);
            (void)form::focus_row(o.form, e.focus);
            m.ui.panel.descend(std::move(o));
            return Cmd::none();
        },
        [&](ClosePasteSettings) -> Cmd {
            ascend(m);
            return Cmd::none();
        },
        [&](PasteSettingsPaste& e) -> Cmd {
            if (auto* o = m.ui.panel.get<pn::PasteSettings>())
                (void)form::paste_into_focused(o->form, e.text);
            return Cmd::none();
        },
        [&](PasteSettingsKey& e) -> Cmd {
            auto* o = m.ui.panel.get<pn::PasteSettings>();
            if (!o) return Cmd::none();
            const auto applied = form::keys::apply(o->form, e.action);
            if (applied.close)
                return paste_settings_update(m, msg::PasteSettingsMsg{ClosePasteSettings{}});
            // Mid-edit击键必须不影响表格（参见web_search.cpp)。
            if (applied.settled < 0) return Cmd::none();

            paste_cfg::Config cfg = m.d.persisted.paste;
            reg::read_rows(cfg, o->form, reg::Owner::Paste);
            if (cfg == m.d.persisted.paste) {
                rebuild(m, *o);
                return Cmd::none();
            }
            m.d.persisted.paste = std::move(cfg);
            Cmd out = save_record(m);
            rebuild(m, *o);
            o->form.dirty = false;   // 只是已保存:不要说"unsaved"
            return out;
        },
    }, pm);
}

} // namespace agentty::app::detail
