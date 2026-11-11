// settings_list_update — 状态更新函数 设置 选择
// 面板 (Ctrl+K → Plugins/Commands/Agents/Hooks。
// 一个列表 弹出面板 以它被打开的顾虑为参数。。。 Enter 运行焦点行动作;
// Esc 关闭。行分辨率为索引 -into-live (
// settings::items_for ),从不 过期 枚举 ——命令调
// 色板使用的相同正确性规则.

#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/app/deps.hpp"
#include "agentty/domain/ui_theme.hpp"
#include "agentty/runtime/app/cmd_factory.hpp"   // load_plugins_async

#include <algorithm>
#include <utility>

#include <maya/core/overload.hpp>

#include "agentty/runtime/panel/settings/items.hpp"
#include "agentty/runtime/panel/step.hpp"
#include "agentty/runtime/view/helpers.hpp"   // utf8_encode / utf8_prev 数据
#include "agentty/tool/plugin.hpp"
#include "agentty/tool/registry.hpp"   // reload_mcp_plugins

namespace pn = agentty::ui::panel;

namespace agentty::app::detail {

namespace {
// Re-enter整个-Msg 状态更新函数，就位。
//
// 这些行分派跨FIVE不同的域（配置文件、抹布、智能、外观、
// 元），这正是它们遍历top-level 状态更新函数
// 而不是每个命名域状态更新函数的原因：行的操作IS在分派表中。
// update() 仍然返回一对，所以在这里解压它——
// 一个地方而不是每一行。
[[nodiscard]] Cmd reenter(Model& m, Msg msg) {
    auto [next, cmd] = agentty::app::update(std::move(m), std::move(msg));
    m = std::move(next);
    return std::move(cmd);
}
} // 命名空间结束。


using maya::overload;
namespace se = agentty::settings;
namespace cmdf = agentty::app::cmd;   // cmd_factory (当地 vars 命名为 `cmd` 阴影的ns)

namespace {
// 电话 mcp.json 编辑于 此 列应该写。服务器列带。 agentty 其
// 内容来自( 镜像来源于) PluginModel ),
// 所以一个 remove/toggle 土地在 RIGHT 文件 – 一个工程服务器编辑工程配置,
// 一个用户服务器 用户配置。 空 config_dir (增加) (中文(简体) ).
// -mode ,或预 -provenance 行) 倒回到用户配置,历史默认.
[[nodiscard]] std::filesystem::path edit_target(const se::Item& row, const Model& m) {
    if (!row.config_dir.empty())
        return std::filesystem::path{row.config_dir} / "mcp.json";
    return tools::plugin::config_path(/*项目=*/false, m.env.user_root);
}

// 排行时为真 `i` 用户可以采取行动的( ) Enter/toggle/etc).
[[nodiscard]] bool actionable_at(const std::vector<se::Item>& rows, int i) {
    return i >= 0 && i < static_cast<int>(rows.size())
        && rows[static_cast<std::size_t>(i)].action != se::Action::None;
}

// 光标 -landing SSOT 设置列表: 用户可以使用的第一行索引 ACT 扫描
// `start` 输入 `dir` (+1 下/ -1 上), `start` 包含。
// 跳过 Action::None 行数( 4) 头文件 ,已建 -in 列表,
// 空 -state 如果不存在任何可操作的占位符,则返回 `start` 被夹住了,
// 所以完全 -informational 面板 (e.g . 建立代理人 -ins )
// 仍然有一个有效、稳定的光标, 并且从未降落到范围之外。 用于打开、 移动和插件
// -refresh 调和器是这样 "where the cursor sits" 现决定于
// 一个 地点。
[[nodiscard]] int first_actionable(const std::vector<se::Item>& rows,
                                   int start, int dir) {
    const int n = static_cast<int>(rows.size());
    if (n <= 0) return 0;
    start = std::clamp(start, 0, n - 1);
    const int step = dir < 0 ? -1 : 1;
    for (int i = start; i >= 0 && i < n; i += step)
        if (rows[static_cast<std::size_t>(i)].action != se::Action::None)
            return i;
    return start;
}

// 打开 面板 搜索击中了生活,在排队。
// Esc 随其查询返回搜索。
[[nodiscard]] Msg open_hit(const se::ListOpen::Hit& h) {
    using P = se::ListOpen::Pane;
    switch (h.pane) {
        case P::Rag:        return Msg{OpenRag{h.row}};
        case P::Smart:      return Msg{OpenSmartMode{h.row}};
        case P::Appearance: return Msg{OpenAppearance{h.row}};
        case P::Sandbox:    return Msg{OpenSandbox{h.row}};
        case P::WebSearch:  return Msg{OpenWebSearch{h.row}};
        case P::Paste:      return Msg{OpenPasteSettings{h.row}};
    }
    return Msg{OpenRag{h.row}};
}
}  // 命名空间结束。

// 每一种形式 面板 ,从每个 面板 自己的造型师。
std::vector<se::ListOpen::Hit> settings_search_index(const Model& m) {
    using P = se::ListOpen::Pane;
    std::vector<se::ListOpen::Hit> out;
    se::index_form(out, rag_form_for_search(m),        P::Rag);
    se::index_form(out, build_smart_form(m, true),     P::Smart);
    se::index_form(out, appearance_form_for_search(m), P::Appearance);
    se::index_form(out, sandbox_form_for_search(m),    P::Sandbox);
    se::index_form(out, web_search_form_for_search(m), P::WebSearch);
    se::index_form(out, paste_form_for_search(m), P::Paste);
    return out;
}

Cmd settings_list_update(Model& m, msg::SettingsListMsg sm) {
    return std::visit(overload{
        [&](OpenSettingsList& e) -> Cmd {
            // 将光标降落在第一个 ACTIONABLE 排行,不是头号 头文件 (插
            // 件数) 面板 以一个 "N tools on the wire" 信息行).
            // first_actionable 完全按住一个有效的索引
            // -informational / 空( 空) 面板 也一样。
            const int start =
                first_actionable(se::items_for(m, e.concern), 0, +1);
            // descend(),不是任务.
            //
            // 任务覆盖了任何开放的,所以 面板 Esc 来了
            // FROM 不见了,而且 Esc 关闭整个堆栈
            // 而不是退后。从 Ctrl +K 调色板的
            // adopt() 曾经用纸纸来印; 达到任何其他
            // 方式—— 直接的钥匙,另一个 面板 放弃——没有任何
            // 东西可以收养和 Esc 将用户丢弃于 线程.
            // descend() 将父存储在开放的站点,
            // 这个地方总是知道它正在替换什么。
            // 从列表(枢纽的门)打开的不同的列表是一步倒下,
            // 所以Esc返回枢纽;重新打开相同的列表是
            // 重建.
            const auto* cur = m.ui.panel.get<pn::SettingsList>();
            if (cur && cur->concern != e.concern)
                m.ui.panel.push(pn::SettingsList{{e.concern, start}});
            else
                m.ui.panel.descend(pn::SettingsList{{e.concern, start}});
            // 新的打开起始于顶端 —— 不要继承上次访问的滚动
            // 偏移( 部件保存) -selection-in-view
            // 将战斗 过期 员额员额 帧). PR #34.
            m.ui.settings_list_scroll.y = 0;
            if (e.concern == se::Category::General)
                if (auto* hub = m.ui.panel.get<pn::SettingsList>())
                    hub->index_rows = settings_search_index(m);
            // 打开插件面板是什么 CONNECTS 服务器(并
            // 刷新 快照 :连接是由更新循环驱动的,而不是一个 惰性
            // 副作用 工具的首页 调用。没有这个面板,
            // 面板显示 "connecting…" 直到你转弯为止 发射
            // 电源连接器 PluginsUpdated 土地,
            // 结果 m.ui.plugins.
            if (e.concern == se::Category::Plugins) {
                // 如果启动连接(从init)仍在飞行中,则不要发射a 第二 一张,
                // 只要显示它 加载动画。冗余的并行连接是
                // "date connected 4×" 症状和
                // 出现这种疾病的先决条件 池-swap 竞态。闲置时,
                // 打开面板就是(重新)连接的内容。
                if (!m.ui.plugins_loading) {
                    m.ui.plugins_loading = true;
                    return cmdf::load_plugins_async(/*重新连接=*/true);
                }
            }
            return Cmd::none();
        },
        [&](PluginsUpdated& e) -> Cmd {
            // 电话 connect/reload 完成于 a 工作线程. Store
            // 编号 快照 (这个) IS 三角洲模型, 重绘 小组讨论 -- --
            // visual_hash 覆盖 m.ui.plugins 清除所有错误 并清除 加载动画.
            //
            // 光标 IDENTITY ,而不是索引: 重装可
            // add/remove/reorder 列,所以原始索引夹会默默地将
            // 光标滑到邻居那里,然后 Enter/`d` 会在错误的服务器上行动。
            // 记住选择了 之前 的换行( 通过服务器+
            // 工具+ 动作), 然后将光标恢复到同一行。
            std::optional<std::tuple<std::string, std::string, se::Action>> sel;
            if (auto* o = m.ui.panel.get<pn::SettingsList>();
                o && o->concern == se::Category::Plugins) {
                auto before = se::items_for(m, o->concern);
                if (o->index >= 0 && o->index < static_cast<int>(before.size())) {
                    const auto& r = before[static_cast<std::size_t>(o->index)];
                    sel = std::make_tuple(r.arg, r.arg2, r.action);
                }
            }

            m.ui.plugins = std::move(e.model);
            m.ui.plugins_loading = false;

            if (auto* o = m.ui.panel.get<pn::SettingsList>();
                o && o->concern == se::Category::Plugins) {
                auto after = se::items_for(m, o->concern);
                const int cnt = static_cast<int>(after.size());
                int restored = -1;
                if (sel) {
                    for (int i = 0; i < cnt; ++i)
                        if (after[static_cast<std::size_t>(i)].arg == std::get<0>(*sel)
                            && after[static_cast<std::size_t>(i)].arg2 == std::get<1>(*sel)
                            && after[static_cast<std::size_t>(i)].action == std::get<2>(*sel)) {
                            restored = i; break;
                        }
                }
                // 发现的同一行 —— 跟着它; 否则 -land
                // 位于可操作行首 at/after 旧指数(从未 过期 头
                // 文件 )——它命名的行已消失(. e.g。已删除服务器).
                o->index = restored >= 0
                    ? restored
                    : first_actionable(after,
                          std::clamp(o->index, 0, std::max(0, cnt - 1)), +1);
            }
            return Cmd::none();
        },
        [&](CloseSettingsList) -> Cmd {
            // Esc 解风一个关卡:调色板 快照 藏在露天(
            // 清水和光标完好无损),或 线程 何时 ^S-style
            // 直接输入没有父母。 -reconstruction
            // 将类别映射回调色板命令; 快照 刚刚 IS
            // 用户左侧的调色板。
            ascend(m);
            return Cmd::none();
        },
        [&](SettingsListMove& e) -> Cmd {
            auto* o = m.ui.panel.get<pn::SettingsList>();
            if (!o) return Cmd::none();
            o->confirm_remove.clear();   // 将一行移走 解除 a 待处理 ^D
            if (se::is_search(*o)) {
                const int hits = static_cast<int>(se::search(o->index_rows, o->query).size());
                o->index = ui::step(o->index, e.delta, hits);
                return Cmd::none();
            }
            auto rows = se::items_for(m, o->concern);
            const int n = static_cast<int>(rows.size());
            if (n <= 0) { o->index = 0; return Cmd::none(); }

            // 只能放在行上, 并像每张列表一样在端部包裹。
            // 一张没有可操作行的 面板 仍然滚动。
            const int idx = std::clamp(o->index, 0, n - 1);
            bool any = false;
            for (int i = 0; i < n; ++i) any = any || actionable_at(rows, i);
            o->index = any
                ? ui::step_over(idx, e.delta, n,
                                [&](int i) { return !actionable_at(rows, i); })
                : ui::step(idx, e.delta, n);
            return Cmd::none();
        },
        [&](SettingsListActivate) -> Cmd {
            auto* o = m.ui.panel.get<pn::SettingsList>();
            if (!o) return Cmd::none();
            o->confirm_remove.clear();   // 任何 Enter 动作解析 a 待处理 ^D
            if (se::is_search(*o)) {
                const auto hits = se::search(o->index_rows, o->query);
                if (o->index < 0 || o->index >= static_cast<int>(hits.size()))
                    return Cmd::none();
                const auto& h = o->index_rows[static_cast<std::size_t>(
                    hits[static_cast<std::size_t>(o->index)])];
                return reenter(m, open_hit(h));
            }
            auto rows = se::items_for(m, o->concern);
            if (o->index < 0 || o->index >= static_cast<int>(rows.size()))
                return Cmd::none();
            const se::Item& row = rows[static_cast<std::size_t>(o->index)];

            switch (row.action) {
                case se::Action::CycleProfile:
                    return reenter(m, Msg{CycleProfile{}});
                case se::Action::OpenRag: {
                    // 降级,不是跳: OpenRag 运行时列表仍然打开,
                    // 所以 descend() 隐藏它——分类、
                    // 光标和它自己的母链——作为 面板's Esc 目标。
                    // 没有手 -stamped 起源,无所忘失。
                    return reenter(m, Msg{OpenRag{}});
                }
                case se::Action::OpenSmart: {
                    return reenter(m, Msg{OpenSmartMode{}});
                }
                case se::Action::OpenAppearance: {
                    return reenter(m, Msg{OpenAppearance{}});
                }
                case se::Action::OpenSandbox: {
                    return reenter(m, Msg{OpenSandbox{}});
                }
                case se::Action::OpenWebSearch: {
                    return reenter(m, Msg{OpenWebSearch{}});
                }
                case se::Action::OpenPaste: {
                    return reenter(m, Msg{OpenPasteSettings{}});
                }
                case se::Action::OpenList:
                    // OpenSettingsList 降,所以,(笑声) Esc 回来这里。
                    return reenter(m, Msg{OpenSettingsList{row.target}});
                case se::Action::ToggleChangesStrip: {
                    return reenter(m, Msg{ToggleChangesStrip{}});
                }
                case se::Action::ToggleAutoTitle: {
                    return reenter(m, Msg{ToggleAutoTitle{}});
                }
                case se::Action::ToggleAutoCompact: {
                    return reenter(m, Msg{ToggleAutoCompact{}});
                }
                case se::Action::CycleToolLoading: {
                    return reenter(m, Msg{CycleToolLoading{}});
                }
                // (有故意的) (有故意地,有故意的,有故意地) NO 激活( A) -arm 用于插件删除。
                // 删除是 `d` → SettingsListRemove ,
                // 这是 两个-step 手臂,然后确认,一个 Enter
                // -fired 移到这里就是了 -press 破坏行动;
                // 旧臂无法伸展——没有排行者曾发射过它——但无法伸展带活
                // 的密码 副作用 是一个陷阱,而不是一个特征。 )

                case se::Action::TogglePlugin: {
                    // 在connect/reload已经飞行时忽略一个切换
                    // ——快照(和这列的on/off)是中变,
                    // 因此可以以此为它写一个过期意图.
                    if (m.ui.plugins_loading)
                        return Cmd::none();
                    // Enter 插件行上翻转 整个 服务器 on/off - 一个可逆的
                    // `disabled` 标志 输入 mcp.json, 不 a 删除。
                    // 启用产卵 + 握手; 禁用连接。 无论哪种方式,
                    // 它都会改变运行程序设置, 所以重联= true,
                    // 关闭 UI 线程。
                    //
                    // 写作是副作用(cmd::edit_plugin);
                    // 其结果以PluginEdited到达,处理如下.
                    // plugins_loading在请求下上升了NOW,
                    // 因此上面的警卫在写作时忽略了第二个Enter,
                    // 而没有以过期的意图进行比赛.
                    m.ui.plugins_loading = true;
                    return cmdf::edit_plugin(edit_target(row, m), PluginEdited{
                        .kind   = PluginEdited::Kind::SetServerDisabled,
                        .from   = PluginEdited::From::SettingsList,
                        .server = row.arg,
                        .flag   = row.on,   // 关闭
                    });
                }
                case se::Action::ToggleTool: {
                    if (m.ui.plugins_loading)
                        return Cmd::none();
                    // 正在使用一个工具 DISABLED/disconnected
                    // 插件毫无意义——整个插件运行无任何。
                    // 引导用户启用插件( ) Enter
                    // 而不是默默编辑一个没有 副作用.
                    if (row.inactive)
                        return set_status_toast(m,
                            "'" + row.arg + "' is disabled — enable the plugin "
                            "first (Enter on it), then toggle its tools");
                    // Enable/disable 插件的一个工具: 坚持
                    // mcp.json's tools.exclude ,
                    // 然后取消 传输 目录 所以,它是这样的 -projects 与新过滤器。
                    // NO 服务器再 -spawn ——服务器保持连接,
                    // project_tools 内容 -reads 活的排除( 先前的重载)
                    // -on-toggle 内容 -spawned 服务器并挂载在快速禁用
                    // 状态上 {re} -enable).
                    //
                    // 写作和 目录 无效,它要求两者都发生在 工作线程 (
                    // cmd::edit_plugin);
                    // PluginEdited 带回结果。
                    m.ui.plugins_loading = true;
                    return cmdf::edit_plugin(edit_target(row, m), PluginEdited{
                        .kind   = PluginEdited::Kind::SetToolEnabled,
                        .from   = PluginEdited::From::SettingsList,
                        .server = row.arg,
                        .tool   = row.arg2,
                        .flag   = !row.on,   // toggle
                    });
                }
                case se::Action::ApprovePlugin: {
                    // Enter 用于不可信项目服务器 = 故意
                    // "trust this server" - 这条线被
                    // 明确标记 "trust & enable" ,
                    // 所以按键 IS 同意(不同于钩子,需要 y/N 批准,
                    // 只批准一项 此 服务器的光谱(不是整个文件),然后重新连接,
                    // 从而产生; 稍后 -added 服务器留下来 待处理.
                    if (m.ui.plugins_loading)
                        return Cmd::none();
                    m.ui.plugins_loading = true;
                    return cmdf::edit_plugin(edit_target(row, m), PluginEdited{
                        .kind   = PluginEdited::Kind::Approve,
                        .from   = PluginEdited::From::SettingsList,
                        .server = row.arg,
                    });
                }
                case se::Action::ApproveHooks:
                    // 同意 必须 做个深思熟虑 终端 行动——
                    // 选择面板 无法安全拥有 y/N 提示词 并用
                    // 它来控制屏幕。 CLI.
                    return set_status_toast(m,
                        "run `agentty hooks approve` in a shell to review "
                        "+ approve (consent is deliberate by design)");
                case se::Action::None:
                default:
                    return Cmd::none();
            }
        },
        [&](SettingsListExpand& e) -> Cmd {
            auto* o = m.ui.panel.get<pn::SettingsList>();
            if (!o || o->concern != se::Category::Plugins || o->input_active) return Cmd::none();
            auto rows = se::items_for(m, o->concern);
            if (o->index < 0 || o->index >= static_cast<int>(rows.size())) return Cmd::none();
            const se::Item& row = rows[static_cast<std::size_t>(o->index)];
            const std::string server = row.arg;
            if (server.empty()) return Cmd::none();
            auto& ex = o->expanded;
            const auto it = std::find(ex.begin(), ex.end(), server);
            if (e.open) {
                // 只有服务器行打开; ~ 在工具上什么也做不了.
                if (row.action != se::Action::TogglePlugin || !row.expandable) return Cmd::none();
                if (it == ex.end()) ex.push_back(server);
                return Cmd::none();
            }
            // QQ 关闭服务器行或任何工具的列表,
            // 光标会降落在服务器上,这样你就不会失去位置.
            if (it == ex.end()) return Cmd::none();
            ex.erase(it);
            const auto after = se::items_for(m, o->concern);
            for (int i = 0; i < static_cast<int>(after.size()); ++i)
                if (after[static_cast<std::size_t>(i)].action == se::Action::TogglePlugin
                    && after[static_cast<std::size_t>(i)].arg == server) {
                    o->index = i;
                    break;
                }
            return Cmd::none();
        },
        [&](SettingsListPin) -> Cmd {
            // `p` 工具行上: 总是 加载。固定工具永远不会被要求,
            // 所以模型不搜索就能看到它们。
            if (m.ui.plugins_loading) return Cmd::none();
            auto* o = m.ui.panel.get<pn::SettingsList>();
            if (!o || o->concern != se::Category::Plugins) return Cmd::none();
            auto rows = se::items_for(m, o->concern);
            if (o->index < 0 || o->index >= static_cast<int>(rows.size()))
                return Cmd::none();
            const se::Item& row = rows[static_cast<std::size_t>(o->index)];
            if (row.action != se::Action::ToggleTool || row.arg2.empty())
                return set_status_toast(m, "p pins a tool: move onto one of a plugin's tools");
            if (!row.on)
                return set_status_toast(m, "'" + row.arg2 + "' is disabled; enable it first");
            m.ui.plugins_loading = true;
            return cmdf::edit_plugin(edit_target(row, m), PluginEdited{
                .kind   = PluginEdited::Kind::SetToolPinned,
                .from   = PluginEdited::From::SettingsList,
                .server = row.arg,
                .tool   = row.arg2,
                .flag   = !row.pinned,
            });
        },
        [&](SettingsListRemove) -> Cmd {
            // `d` 删除突出显示的插件——故意的、破坏性的对应件 Enter
            // 不可逆变 on/off 切换。仅适用于服务器行(Plugins
            // concern, TogglePlugin 动作);
            // 工具子上忽略 -rows 以及其他类别
            if (m.ui.plugins_loading) return Cmd::none();
            auto* o = m.ui.panel.get<pn::SettingsList>();
            if (!o || o->concern != se::Category::Plugins)
                return Cmd::none();
            auto rows = se::items_for(m, o->concern);
            if (o->index < 0 || o->index >= static_cast<int>(rows.size()))
                return Cmd::none();
            const se::Item& row = rows[static_cast<std::size_t>(o->index)];
            // 服务器行是任意的 TogglePlugin (正常)或(正常)
            // ApprovePlugin (不信任的项目)——两者均可移动 `d`.
            // Tool 子目录 -rows 而其他类别的人都不是
            if ((row.action != se::Action::TogglePlugin
                 && row.action != se::Action::ApprovePlugin)
                || row.arg.empty())
                return Cmd::none();   // 不是服务器行

            // 两位 -step : 第一个 `d` 排行时 ARMS (存储名称);
            // 视图后涂色 "press d again to remove"。
            // 仅一秒钟 `d` 自动调试 相同 行会承诺——所以一个偏差键盘永远不会删除手
            // -tuned mcp.json 输入时没有撤销。
            if (o->confirm_remove != row.arg) {
                o->confirm_remove = row.arg;
                return Cmd::none();
            }
            o->confirm_remove.clear();

            m.ui.plugins_loading = true;
            return cmdf::edit_plugin(edit_target(row, m), PluginEdited{
                .kind   = PluginEdited::Kind::Remove,
                .from   = PluginEdited::From::SettingsList,
                .server = row.arg,
            });
        },
        // 要求从这份清单中写出的书面结果(cmd::edit_plugin)。
        // 从前所有跟随书写的人 都住在这
        // 重新装入插件模型, 土司, 将光标保留在范围内。
        // 请从 EDIT PANE 编辑的 是 状态更新函数 的处理方式。
        [&](PluginEdited& e) -> Cmd {
            // 叶子属于一个域 所以这个列表拥有每个域
            // PluginEdited ——包括编辑的作品.
            // 面板。这些将引导到编辑器 面板's 处理函数
            // 而不是给回答 两位主人。
            if (e.from == PluginEdited::From::EditPane)
                return plugin_edit_result(m, std::move(e));
            using K = PluginEdited::Kind;
            if (!e.ok) {
                m.ui.plugins_loading = false;   // 没有任何东西重新连接
                switch (e.kind) {
                case K::Approve:
                    return set_status_toast(m,
                        "could not record approval (no project mcp.json?)");
                case K::Remove:
                    return set_status_toast(m, "could not remove '" + e.server + "'");
                case K::SetToolEnabled:
                    return set_status_toast(m, "could not toggle '" + e.tool + "'");
                case K::SetToolPinned:
                    return set_status_toast(m, "could not pin '" + e.tool + "'");
                default:
                    return set_status_toast(m, "could not toggle '" + e.server + "'");
                }
            }
            // 一个工具 切换 或 针只更改过滤器:没有重生,
            // 但 快照 一定要反映,所以 -snapshot 现场
            // 直播 池 (重连接=假). 每个服务器 -level 更改
            // 会重新连接,因为它会改变运行的流程。
            const bool reconnect = e.kind != K::SetToolEnabled && e.kind != K::SetToolPinned;
            std::string toast;
            switch (e.kind) {
            case K::SetServerDisabled:
                toast = (e.flag ? "disabled plugin '" : "enabled plugin '")
                      + e.server + "'";
                break;
            case K::SetToolEnabled:
                toast = (e.flag ? "enabled tool '" : "disabled tool '")
                      + e.tool + "' on " + e.server;
                break;
            case K::SetToolPinned:
                toast = e.flag
                    ? "pinned '" + e.tool + "': always loaded"
                    : "unpinned '" + e.tool + "': loads when it fits, else on demand";
                break;
            case K::Approve:
                toast = "trusted project config — connecting…";
                break;
            case K::Remove:
                toast = "removed plugin '" + e.server + "'";
                break;
            default: break;
            }
            // 列表重建后将光标保留在真实行上(
            // 删除会缩短).
            if (auto* oo = m.ui.panel.get<pn::SettingsList>()) {
                const int n = static_cast<int>(se::items_for(m, oo->concern).size());
                oo->index = std::clamp(oo->index, 0, std::max(0, n - 1));
            }
            return Cmd::batch(cmdf::load_plugins_async(reconnect),
                              set_status_toast(m, std::move(toast)));
        },
        [&](SettingsListEditOpen& e) -> Cmd {
            auto* o = m.ui.panel.get<pn::SettingsList>();
            if (!o || o->input_active) return Cmd::none();
            if (o->concern != se::Category::Plugins) {
                // 其他关切没有编辑表; `a` 堕落到一个 -line
                // 启动器 提示词 他们已经使用了, `e` 是一个不 -op.
                if (e.add)
                    return reenter(m, Msg{SettingsListAddStart{}});
                return Cmd::none();
            }
            if (e.add) {
                // 添加表单:降级将本列表作为Esc目标.
                return reenter(m, Msg{OpenPluginEdit{{}, false}});
            }
            // 突出显示的服务器的详细格式。 Tool 行将服务器载入
            // `arg` 也一样,所以 `e` 任何一处在树下工作
            const auto rows = se::items_for(m, o->concern);
            if (o->index < 0 || o->index >= static_cast<int>(rows.size()))
                return Cmd::none();
            const auto& row = rows[static_cast<std::size_t>(o->index)];
            if (row.arg.empty()) return Cmd::none();   // 头文件 行数
            return reenter(m, Msg{OpenPluginEdit{row.arg, false}});
        },
        [&](SettingsListAddStart) -> Cmd {
            auto* o = m.ui.panel.get<pn::SettingsList>();
            if (!o) return Cmd::none();
            // 添加只对 file/config-backed 关注。
            if (o->concern != se::Category::Plugins
                && o->concern != se::Category::Commands
                && o->concern != se::Category::Agents)
                return Cmd::none();
            o->input_active = true;
            o->input.clear();
            o->cursor = 0;
            return Cmd::none();
        },
        [&](SettingsListChar& e) -> Cmd {
            auto* o = m.ui.panel.get<pn::SettingsList>();
            if (!o || !o->input_active) return Cmd::none();
            const std::string utf8 = ui::utf8_encode(e.ch);
            o->input.insert(static_cast<std::size_t>(o->cursor), utf8);
            o->cursor += static_cast<int>(utf8.size());
            return Cmd::none();
        },
        [&](SettingsListPaste& e) -> Cmd {
            auto* o = m.ui.panel.get<pn::SettingsList>();
            if (!o || !o->input_active) return Cmd::none();
            // 添加 -prompt 是一个单行( 插件的)
            // "name command args…" spec, 或新文件的名称。
            // 平整任意 newlines/tabs 将粘贴到一个多位 -line
            // 剪贴板不能将线条分解到其中 -line 字段或分割参数矢量。
            std::string clean;
            clean.reserve(e.text.size());
            for (char c : e.text)
                clean += (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
            o->input.insert(static_cast<std::size_t>(o->cursor), clean);
            o->cursor += static_cast<int>(clean.size());
            return Cmd::none();
        },
        [&](SettingsSearchInput& e) -> Cmd {
            auto* o = m.ui.panel.get<pn::SettingsList>();
            if (!o || o->concern != se::Category::General) return Cmd::none();
            if (o->query.empty() && e.ch == U' ') return Cmd::none();   // 无主空格
            o->query += ui::utf8_encode(e.ch);
            o->index = 0;                  // 最好先对齐
            m.ui.settings_list_scroll.y = 0;
            return Cmd::none();
        },
        [&](SettingsSearchClear) -> Cmd {
            auto* o = m.ui.panel.get<pn::SettingsList>();
            if (!o) return Cmd::none();
            o->query.clear();
            o->index = first_actionable(se::items_for(m, o->concern), 0, +1);
            m.ui.settings_list_scroll.y = 0;
            return Cmd::none();
        },
        [&](SettingsSearchBackspace) -> Cmd {
            auto* o = m.ui.panel.get<pn::SettingsList>();
            if (!o || o->query.empty()) return Cmd::none();
            o->query.erase(static_cast<std::size_t>(
                ui::utf8_prev(o->query, static_cast<int>(o->query.size()))));
            // 一旦搜索是空白的,就回到中心线: 降落在第
            // 一个你可以行动的地方。
            if (!se::is_search(*o)) o->query.clear();
            o->index = o->query.empty()
                ? first_actionable(se::items_for(m, o->concern), 0, +1) : 0;
            m.ui.settings_list_scroll.y = 0;
            return Cmd::none();
        },
        [&](SettingsListBackspace) -> Cmd {
            auto* o = m.ui.panel.get<pn::SettingsList>();
            if (!o || !o->input_active || o->cursor <= 0)
                return Cmd::none();
            int p = ui::utf8_prev(o->input, o->cursor);
            o->input.erase(static_cast<std::size_t>(p),
                           static_cast<std::size_t>(o->cursor - p));
            o->cursor = p;
            return Cmd::none();
        },
        [&](SettingsListCancelInput) -> Cmd {
            auto* o = m.ui.panel.get<pn::SettingsList>();
            if (!o) return Cmd::none();
            o->input_active = false;
            o->input.clear();
            o->cursor = 0;
            return Cmd::none();
        },
        [&](SettingsListSubmitInput) -> Cmd {
            auto* o = m.ui.panel.get<pn::SettingsList>();
            if (!o || !o->input_active) return Cmd::none();
            const std::string line = o->input;
            const se::Category concern = o->concern;
            o->input_active = false;
            o->input.clear();
            o->cursor = 0;
            if (line.empty()) return Cmd::none();   // empty = cancel
            // 创建时写入磁盘( T) ( mcp.json ,或启动文件),
            // 所以它是 副作用; SettingsAddDone 下方会接回答案
            return cmdf::settings_add(concern, line);
        },
        [&](SettingsAddDone& d) -> Cmd {
            std::string message = std::move(d.message);
            // 来啊 USABLE 当前 ,在重新启动后不会。对于插件,
            // 添加已写入 mcp.json ; 重新装入直播 池 OFF
            // 编号 UI 线程 (连接握手不得冻结) TUI 桥以最后
            // 期限为界。一个新的命令文件到达 Model 以电子
            // 邮件方式发送 库 重新装入。 任务中每个任务都会
            // 扫描代理 -tool 调用 已经活下来了
            Cmd reload = Cmd::none();
            if (d.ok && d.concern == se::Category::Plugins) {
                m.ui.plugins_loading = true;
                reload = cmdf::load_plugins_async(/*重新连接=*/true);
                message += " — connecting…";
            } else if (d.ok && d.concern == se::Category::Commands) {
                reload = cmdf::load_library();
            }
            // Re-clamp (可能成长) 列表为新行的顶级.
            if (auto* oo = m.ui.panel.get<pn::SettingsList>()) {
                const int cnt =
                    static_cast<int>(se::items_for(m, oo->concern).size());
                oo->index = std::clamp(oo->index, 0, std::max(0, cnt - 1));
            }
            return Cmd::batch(std::move(reload), set_status_toast(m, message));
        },
    }, sm);
}

} // namespace agentty::app::detail
