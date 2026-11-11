// plugin_edit.cpp-插件detail/add形式的状态更新函数。
//
// 一个面板，两种模式（参见pn::PluginEdit):明细（服务器
// 非-empty）和添加（服务器空，kind-first）。一切都流经共享表单层；
// 这个状态更新函数只拥有插件-specific后果：
//   • 种类更改（添加模式）→ 重建字段集，保留 带类型的 文本
//   • 已启用 切换（详细信息）→ set_server_disabled + 重新加载，实时
//   ·工具：<name> 切换 → set_tool_enabled + 目录 失效，实时
//   • 保存 → 验证 → add_server / update_server →
//                               重新加载→举杯上升
//   • 批准 → approve_server (信任哈希) → 重新加载
//   • 移除 → 两个-step 臂（与列表中的`d` 相同的合同）
//
// enabled/tool 切换适用于IMMEDIATELY（它们是可逆的并且
// 该列表今天的行为方式如此）； 保存 仅拥有 identity/transport 字段，
// 因此翻转 切换 而从未翻转 保存 的用户仍然可以获得 面板 向他们显示的内容。
// 该表单的 `dirty` 标志 仅跟踪 保存-owned 行。

#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/app/update.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <maya/core/overload.hpp>

#include "agentty/mcp/client.hpp"                    // plugin_model
#include "agentty/runtime/app/cmd_factory.hpp"       // load_plugins_async
#include "agentty/runtime/panel/plugin_form.hpp"
#include "agentty/tool/plugin.hpp"
#include "agentty/tool/registry.hpp"                 // invalidate_mcp_catalog

namespace pn = agentty::ui::panel;
namespace pf = agentty::plugin_form;
namespace fs = std::filesystem;
namespace cmdf = agentty::app::cmd;   // cmd_factory（当地人名为`cmd`的影子）

namespace agentty::app::detail {

using maya::overload;

namespace {

using pf::kNoteRemoveArmed;

// ── 快照 − 格式输入 ────────────────────────────────────────────

[[nodiscard]] std::string kind_of(const mcp::ServerState& s) {
    if (s.passthrough)     return pf::kKindPassthrough;
    if (!s.url.empty())    return pf::kKindHttp;   // SSE 渲染为http；类型
    return pf::kKindStdio;                         // 保存于保存
}

// 为 Model 的 快照 中的一台服务器构建细节-mode输入。
// 当服务器消失(在面板下面移除)时返回假。
[[nodiscard]] bool inputs_for(const Model& m, const std::string& server,
                              pf::PluginFormInputs& in) {
    for (const auto& s : m.ui.plugins.servers) {
        if (s.name != server) continue;
        in.add_mode  = false;
        in.kind      = kind_of(s);
        in.name      = s.name;
        in.command   = s.command;
        in.url       = s.url;
        in.enabled   = !s.disabled;
        in.connected = s.connected;
        in.error     = s.error;
        in.scope_label = std::string{mcp::to_string(s.origin)};
        in.config_file = s.config_dir.empty()
            ? tools::plugin::config_path(false, m.env.user_root).string()
            : (fs::path{s.config_dir} / "mcp.json").string();
        in.untrusted = s.untrusted;
        for (const auto& t : s.tools)
            in.tool_rows.push_back({t.name, t.description, t.enabled});
        return true;
    }
    return false;
}

// ── 表单 − 数值 ───────────────────────────────────────────
// 字段读取通过表单层的SSOT访问器(form::text_of/
// toggle_of/choice_of)——这里没有本地变体-spelunking。
using form::text_of;
using form::toggle_of;
using form::choice_of;

[[nodiscard]] std::vector<std::string> split_list(std::string_view s) {
    std::vector<std::string> out;
    while (!s.empty()) {
        const auto cut = s.find_first_of(", ");
        std::string_view tok = s.substr(0, cut);
        if (!tok.empty()) out.emplace_back(tok);
        if (cut == std::string_view::npos) break;
        s.remove_prefix(cut + 1);
    }
    return out;
}

[[nodiscard]] bool valid_plugin_name(std::string_view n) {
    if (n.empty() || n.size() > 64 || n.front() == '-') return false;
    for (char c : n)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_'
              || c == '-' || c == ':'))
            return false;
    return true;
}

// 将 保存-owned 值从实时表单中拉出到 ServerSpec 中，
// 同时进行验证。失败时，标记有问题的行的 `error` 并
// 返回 false — 形式 渲染 是内联消息，正是修复所在的位置。
[[nodiscard]] bool spec_from_form(form::Form& f, const std::string& kind,
                                  std::string name, tools::plugin::ServerSpec& spec) {
    auto fail = [&](const char* id, std::string msg) {
        if (auto* fld = f.find(id)) fld->error = std::move(msg);
        return false;
    };
    // 清除过期错误——re-save不得显示上次尝试的重影。
    for (auto& fld : f.fields) fld.error.clear();

    if (name.empty()) name = text_of(f, pf::kName);
    if (!valid_plugin_name(name))
        return fail(pf::kName, "letters, digits, _ - : only");
    spec.name = std::move(name);

    if (kind == pf::kKindStdio) {
        spec.command = text_of(f, pf::kCommand);
        if (spec.command.empty())
            return fail(pf::kCommand, "a stdio server needs a command");
        spec.args = split_list(text_of(f, pf::kArgs));
        spec.type = "stdio";
    } else if (kind == pf::kKindPassthrough) {
        spec.url = text_of(f, pf::kUrl);
        if (spec.url.rfind("http://", 0) != 0
            && spec.url.rfind("https://", 0) != 0)
            return fail(pf::kUrl, "must start with http:// or https://");
        spec.type = "passthrough";
        spec.passthrough = split_list(text_of(f, pf::kTools));
        // 详细模式没有kTools行（工具为切换行）；
        // 通过将 spec.passthrough 保留为空来保留配置的现有列表 -
        // update_server 保留缺失的字段。
        if (f.find(pf::kTools) && spec.passthrough.empty())
            return fail(pf::kTools, "at least one tool name");
    } else {   // http/SSE
        spec.url = text_of(f, pf::kUrl);
        if (spec.url.rfind("http://", 0) != 0
            && spec.url.rfind("https://", 0) != 0)
            return fail(pf::kUrl, "must start with http:// or https://");
        spec.type = (kind == pf::kKindSse) ? "sse" : "http";
    }
    return true;
}

// 将ADD表格重建为新类型，继承用户带类型的。
void rebuild_add_form(pn::PluginEdit& o, const std::string& kind) {
    pf::PluginFormInputs in;
    in.add_mode = true;
    in.kind     = kind;
    in.name     = text_of(o.form, pf::kName);
    in.command  = text_of(o.form, pf::kCommand);
    in.args     = text_of(o.form, pf::kArgs);
    in.url      = text_of(o.form, pf::kUrl);
    in.tools    = text_of(o.form, pf::kTools);
    in.advertise= toggle_of(o.form, pf::kAdvertise);
    in.project  = toggle_of(o.form, pf::kScopeProject);
    form::rebuild(o.form, pf::build_form(in));
    o.built_kind = kind;
}

[[nodiscard]] fs::path config_target(const pn::PluginEdit& o, const Model& m) {
    if (o.server.empty())
        return tools::plugin::config_path(toggle_of(o.form, pf::kScopeProject), m.env.user_root);
    for (const auto& s : m.ui.plugins.servers)
        if (s.name == o.server && !s.config_dir.empty())
            return fs::path{s.config_dir} / "mcp.json";
    return tools::plugin::config_path(false, m.env.user_root);
}

} // 命名空间结束。

Cmd plugin_edit_update(Model& m, msg::PluginEditMsg pm) {
    return std::visit(overload{
        [&](OpenPluginEdit& e) -> Cmd {
            pf::PluginFormInputs in;
            if (e.server.empty()) {
                in.add_mode = true;
                in.project  = e.project;
            } else if (!inputs_for(m, e.server, in)) {
                return set_status_toast(m, "plugin '" + e.server + "' not found");
            }
            pn::PluginEdit pane{{}, pf::build_form(in), e.server,
                                e.project, in.kind};
            m.ui.panel.descend(std::move(pane));
            return Cmd::none();
        },

        [&](ClosePluginEdit) -> Cmd {
            ascend(m);   // Esc:回到打开这个的面板，或者关闭
            return Cmd::none();
        },

        [&](PluginEditPaste& e) -> Cmd {
            auto* o = m.ui.panel.get<pn::PluginEdit>();
            if (o) (void)form::paste_into_focused(o->form, e.text);   // 像打字一样开始编辑
            return Cmd::none();
        },

        [&](PluginEditKey& e) -> Cmd {
            auto* o = m.ui.panel.get<pn::PluginEdit>();
            if (!o) return Cmd::none();

            const auto* row = o->form.focused();
            const std::string row_id = row ? row->id : std::string{};

            const auto applied = form::keys::apply(o->form, e.action);

            if (applied.close)
                return plugin_edit_update(m, msg::PluginEditMsg{ClosePluginEdit{}});

            // 离开已武装的移除行会解除其武装——与设置列表的`d`
            // 相同的合同（任何导航都会取消待处理破坏性步骤；
            // 仅SAME行上的Enter-again触发）。
            if (o->form.note == kNoteRemoveArmed) {
                const auto* now_row = o->form.focused();
                if (!now_row || now_row->id != pf::kRemove) {
                    o->form.note.clear();
                    o->form.note_replaces_grammar = false;
                }
            }

            // ── 类型变化( 添加模式): 重建字段集 ─────────
            if (applied.changed && row_id == pf::kKind && o->server.empty()) {
                const std::string kind = choice_of(o->form, pf::kKind);
                if (kind != o->built_kind) rebuild_add_form(*o, kind);
                return Cmd::none();
            }

            // ── 生活切换( 详细模式) ────────────────────────────
            // 每次写入都是副作用 (cmd::edit_plugin)。其结果作为
            // PluginEdited 返回并在 plugin_edit_result() 中处理。
            if (applied.changed && !o->server.empty()) {
                const fs::path path = config_target(*o, m);
                if (row_id == pf::kEnabled) {
                    const bool on = toggle_of(o->form, pf::kEnabled);
                    return cmdf::edit_plugin(path, PluginEdited{
                        .kind   = PluginEdited::Kind::SetServerDisabled,
                        .from   = PluginEdited::From::EditPane,
                        .server = o->server,
                        .flag   = !on,
                        .project = o->project,
                    });
                }
                if (row_id.starts_with(pf::kToolPrefix)) {
                    const std::string bare =
                        row_id.substr(std::string_view{pf::kToolPrefix}.size());
                    return cmdf::edit_plugin(path, PluginEdited{
                        .kind   = PluginEdited::Kind::SetToolEnabled,
                        .from   = PluginEdited::From::EditPane,
                        .server = o->server,
                        .tool   = bare,
                        .flag   = toggle_of(o->form, row_id.c_str()),
                        .project = o->project,
                    });
                }
                // 任何其他改变（url文本等）是保存-owned；掉出来。
            }

            // 留下编辑字段IS和保存 (Applied::left_field)
            // —没有^S。仅详细模式：添加表单仍在构建中，
            // 因此提交不带 URL 的名称只会在用户尚未
            // 到达的行上标记错误；其显式 "Add plugin"
            // 操作行执行写入操作。 Field-exit
            // 提交保留 面板 OPEN（离开字段并不离开表单）；
            // 操作行关闭。
            const bool exit_commit = applied.left_field && !o->server.empty();
            const bool want_save = exit_commit
                || (applied.fired && row_id == pf::kSave);

            if (!applied.fired && !want_save) return Cmd::none();

            // ── 动作行 ───────────────────────────────────────────
            if (row_id == pf::kApprove && !o->server.empty()) {
                return cmdf::edit_plugin(config_target(*o, m), PluginEdited{
                    .kind   = PluginEdited::Kind::Approve,
                    .from   = PluginEdited::From::EditPane,
                    .server = o->server,
                    .project = o->project,
                });
            }

            if (row_id == pf::kRemove && !o->server.empty() && !want_save) {
                // Two-step:第一Enter武器(注释解释)，第二次开火。
                if (o->form.note != kNoteRemoveArmed) {
                    o->form.note = kNoteRemoveArmed;
                    o->form.note_replaces_grammar = true;
                    return Cmd::none();
                }
                return cmdf::edit_plugin(config_target(*o, m), PluginEdited{
                    .kind   = PluginEdited::Kind::Remove,
                    .from   = PluginEdited::From::EditPane,
                    .server = o->server,
                    .project = o->project,
                });
            }

            if (want_save) {
                const std::string kind = o->server.empty()
                    ? choice_of(o->form, pf::kKind) : o->built_kind;
                tools::plugin::ServerSpec spec;
                if (!spec_from_form(o->form, kind, o->server, spec))
                    return Cmd::none();   // 内联错误设置
                const bool add = o->server.empty();
                // `server`对回复所涉及的条目进行命名：
                // 用于添加的新名称，用于更新的现有名称。
                PluginEdited req{
                    .kind        = add ? PluginEdited::Kind::Add
                                       : PluginEdited::Kind::Update,
                    .from        = PluginEdited::From::EditPane,
                    .server      = spec.name,
                    .exit_commit = exit_commit,
                    .project     = o->project,
                };
                return cmdf::edit_plugin(config_target(*o, m), std::move(req),
                                         std::move(spec));
            }

            return Cmd::none();
        },
    }, pm);
}

// 写入所请求的编辑面板 (cmd::edit_plugin)的结果，
// 由settings_list的PluginEdited臂路由至此处。
// 用于遵循每个内联写入的所有内容：重新加载插件模型、吐司、关闭或保留 面板，
// 以及 add/update 可能产生的两个内联字段错误。
//
// 该 PANE MAY HAVE MOVED ON。
// 请求后回复会收到一条消息，此时用户可能已关闭 面板 或在
// 另一台服务器上打开它。因此，任何触及 面板 的内容都会首先
// 检查它在本回复所涉及的服务器上是否仍然打开； 过期 回复
// 仍然会重新加载插件模型并进行祝酒（写入 DID 发生），
// 它只是不会编辑不再是其来源的表单。
Cmd plugin_edit_result(Model& m, PluginEdited e) {
    using K = PluginEdited::Kind;
    auto* o = m.ui.panel.get<pn::PluginEdit>();
    // 相同的面板，相同的条目。对于添加，面板 在服务器 "" 上打开，
    // 并且回复命名新服务器，因此添加与打开的 add-form 匹配。
    const bool same_pane = o && o->project == e.project
        && (o->server == e.server || (e.kind == K::Add && o->server.empty()));

    if (!e.ok) {
        switch (e.kind) {
        case K::SetServerDisabled:
            return set_status_toast(m, "could not write mcp.json");
        case K::SetToolEnabled:
            return set_status_toast(m, "could not toggle '" + e.tool + "'");
        case K::SetToolPinned:
            return set_status_toast(m, (e.flag ? "could not pin '" : "could not unpin '")
                                       + e.tool + "'");
        case K::Approve:
            return set_status_toast(m, "approve failed");
        case K::Remove:
            return set_status_toast(m, "remove failed");
        case K::Add:
        case K::Update:
            if (same_pane) {
                if (e.already_exists) {
                    if (auto* fld = o->form.find(pf::kName))
                        fld->error = "'" + e.server + "' already exists";
                } else {
                    o->form.note = "write failed — is the file valid JSON?";
                }
                return Cmd::none();
            }
            return set_status_toast(m, "could not save '" + e.server + "'");
        }
        return Cmd::none();
    }

    switch (e.kind) {
    case K::SetServerDisabled:
        return Cmd::batch(cmdf::load_plugins_async(/*重新连接=*/true),
                          set_status_toast(m, e.flag ? "disabled" : "enabled"));
    case K::SetToolEnabled:
        // 除了写入之外，目录在工作线程上无效。
        return Cmd::batch(cmdf::load_plugins_async(/*重新连接=*/false),
                          set_status_toast(m, (e.flag ? "enabled '" : "disabled '")
                                              + e.tool + "'"));
    case K::SetToolPinned:
        return Cmd::batch(cmdf::load_plugins_async(/*重新连接=*/false),
                          set_status_toast(m, (e.flag ? "pinned '" : "unpinned '")
                                              + e.tool + "'"));
    case K::Approve:
        return Cmd::batch(
            cmdf::load_plugins_async(/*重新连接=*/true),
            set_status_toast(m, "approved '" + e.server + "'"),
            // 重新加载完成后重新打开，因此 面板 反映了
            // -approval 后的状态（受信任 → 连接）。
            // 只有当用户仍在查看它时 - 重新打开他们关闭的 面板 才会
            // 将他们拉回到它。
            same_pane ? Cmd::after(std::chrono::milliseconds{50},
                                   Msg{OpenPluginEdit{e.server, e.project}})
                      : Cmd::none());
    case K::Remove:
        if (same_pane) ascend(m);
        return Cmd::batch(cmdf::load_plugins_async(/*重新连接=*/true),
                          set_status_toast(m, "removed '" + e.server + "'"));
    case K::Add:
    case K::Update: {
        if (same_pane) o->form.dirty = false;   // 提交：页脚下降 "unsaved"
        if (e.exit_commit) {
            // Field-exit提交：书面+应用，面板保持开放。
            // 目录 重新加载运行，因此新值立即生效。
            return Cmd::batch(cmdf::load_plugins_async(/*重新连接=*/true),
                              set_status_toast(m, "saved '" + e.server + "'"));
        }
        if (same_pane) ascend(m);
        return Cmd::batch(
            cmdf::load_plugins_async(/*重新连接=*/true),
            set_status_toast(m, (e.kind == K::Add ? "added '" : "saved '")
                                + e.server + "'"));
    }
    }
    return Cmd::none();
}

} // namespace agentty::app::detail
