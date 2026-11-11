// plugins_in_model_test ——插件面板是该插件的纯函数 Model.
//
// 这个 锁 取代重现的建筑合同 "stuck on connecting… / laggy panel"
// 错误 :
//
//   1. 打开插件面板 plugins_loading 返回 a Cmd
//      (连接是 DRIVEN BY 该 UPDATE LOOP ,而不是一个 惰性 副作用).
//   2. PluginsUpdated{快照 储存 快照 输入 m.ui.plugins 财务报告和已审计财务报表
//      清除装入 标志 - 电话 状态更新函数 ,而不是一个 渲染-time 地球,
//      拥有真理。
//   3. items_for ( ⁇ . 渲染 m.ui.plugins 逐字记录( 纯投影):
//      空模型 + 装入 → "connecting…" ;人口密集的模型  ⁇
//      servers/tools ; 无 调用 现场直播 池.
//
// 因为 快照 生活状况 IN 编号 Model, visual_hash
// 盖盖(见 visual_hash_coverage_test )和面板 重绘
// 没有任何非黑客入侵的每个变化——即 不变量 是什么使得旧的虫类是不可能的。

#include "agtest.hpp"

#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/panel/settings/items.hpp"
#include "agentty/mcp/plugin_model.hpp"

#include <string>

namespace pn = agentty::ui::panel;

using namespace agentty;


TEST_CASE("plugins in model") {
    // ── 1: 打开插件装武器+返回连接Cmd ──
    {
        Model m;
        auto [m2, cmd] = app::update(
            std::move(m), Msg{OpenSettingsList{settings::Category::Plugins}});
        check(m2.ui.panel.is<pn::SettingsList>(),
              "open: Plugins panel is open");
        check(m2.ui.plugins_loading,
              "open: plugins_loading armed (connect is loop-driven)");
        check(!cmd.is_none(),
              "open: a connect Cmd was returned (not a lazy side effect)");
    }

    // ── 2: PluginsUpdated 存储 快照 + 清除装入 ──
    {
        Model m;
        m.ui.panel.descend(pn::SettingsList{{settings::Category::Plugins, 0}});
        m.ui.plugins_loading = true;

        mcp::PluginModel snap;
        mcp::ServerState s;
        s.name = "date";
        s.connected = true;
        s.tools.push_back({"current_date", "today's date", true, false});
        s.tools.push_back({"days_between", "day math", true, false});
        snap.servers.push_back(std::move(s));

        auto [m2, cmd] = app::update(std::move(m),
                                     Msg{PluginsUpdated{std::move(snap)}});
        check(!m2.ui.plugins_loading, "updated: loading flag cleared");
        check(m2.ui.plugins.servers.size() == 1,
              "updated: snapshot stored in m.ui.plugins");
        check(m2.ui.plugins.servers[0].connected,
              "updated: connected state preserved");
        check(m2.ui.plugins.servers[0].tools.size() == 2,
              "updated: advertised tools preserved");
    }

    // ── 3: items_for 是一个纯投影 m.ui.plugins ──
    {
        // 空 + 装入 → 一 "connecting…" 行。
        Model loading;
        loading.ui.plugins_loading = true;
        auto rows_loading = settings::items_for(loading, settings::Category::Plugins);
        check(rows_loading.size() == 1
              && rows_loading[0].primary.find("connecting") != std::string::npos,
              "projection: empty+loading renders 'connecting…'");

        // 空 + 不装入 ~ "no plugins" 提示。
        Model empty;
        auto rows_empty = settings::items_for(empty, settings::Category::Plugins);
        check(rows_empty.size() == 1
              && rows_empty[0].primary.find("no plugins") != std::string::npos,
              "projection: empty+idle renders the 'no plugins' hint");

        // 流行 ~ 服务器行; 打开后其工具显示。
        // 无内容读取 池 直播。
        Model populated;
        mcp::ServerState s;
        s.name = "date";
        s.connected = true;
        s.tools.push_back({"current_date", "d", true, false});
        populated.ui.plugins.servers.push_back(std::move(s));
        populated.ui.panel.descend(pn::SettingsList{});
        populated.ui.panel.get<pn::SettingsList>()->expanded = {"date"};
        auto rows = settings::items_for(populated, settings::Category::Plugins);
        bool saw_date = false, saw_tool = false;
        for (const auto& r : rows) {
            if (r.primary.find("date") != std::string::npos) saw_date = true;
            if (r.primary.find("current_date") != std::string::npos) saw_tool = true;
        }
        check(saw_date, "projection: populated model renders the server");
        check(saw_tool, "projection: populated model renders its tools");

        // CONNECTED服务器行必须读作健康(Status::Ok),
        // NOT佩戴看起来像错误的去除的-action徽章.
        for (const auto& r : rows) {
            if (r.primary == "date") {
                check(r.status == settings::Item::Status::Ok,
                      "badge: connected server is Status::Ok (healthy, not a red ✕)");
                // Enter TOGGLES 整个插件 on/off(可逆),
                // 它确实删除了 不 —— 破坏性删除是故意删除的 `d` 键.
                check(r.action == settings::Action::TogglePlugin,
                      "interaction: Enter on a plugin toggles it on/off");
                check(r.on && r.hint == "disable",
                      "interaction: an enabled plugin shows on + 'disable'");
            }
        }

        // A DISABLED 服务器读取为关闭( Neutral, 不是错误),
        // 提议重读 -enable ,和 KEEPS 其工具列出——但标记整个子树不活动(
        // 目前) -but-can't-run ),无 -tool 切换 提示,请说
        Model off;
        mcp::ServerState d;
        d.name = "date";
        d.disabled = true;
        d.tools.push_back({"current_date", "", true, false});
        d.tools.push_back({"days_between", "", true, false});
        off.ui.plugins.servers.push_back(std::move(d));
        off.ui.panel.descend(pn::SettingsList{});
        off.ui.panel.get<pn::SettingsList>()->expanded = {"date"};
        int tools_seen = 0, inactive_seen = 0;
        for (const auto& r : settings::items_for(off, settings::Category::Plugins)) {
            if (r.primary == "date") {
                check(!r.on && r.hint == "enable",
                      "interaction: a disabled plugin shows off + 'enable'");
                check(r.status == settings::Item::Status::Neutral,
                      "badge: a disabled plugin is Neutral (off on purpose, not an error)");
            }
            if (r.indented) {   // 禁用插件下的工具行
                ++tools_seen;
                if (r.inactive) ++inactive_seen;
                check(r.hint.empty(),
                      "interaction: a tool under a disabled plugin has no toggle hint");
            }
        }
        check(tools_seen == 2,
              "UX: a disabled plugin STILL lists its tools (got "
              + std::to_string(tools_seen) + ")");
        check(inactive_seen == 2,
              "UX: every tool under a disabled plugin is marked inactive (dimmed)");

        // 来,来,来 ENABLED ,连接的插件工具是 活动 (不淡化)
        // 和可切换。
        Model live;
        mcp::ServerState ls;
        ls.name = "date";
        ls.connected = true;
        ls.tools.push_back({"current_date", "", true, false});
        live.ui.plugins.servers.push_back(std::move(ls));
        live.ui.panel.descend(pn::SettingsList{});
        live.ui.panel.get<pn::SettingsList>()->expanded = {"date"};
        for (const auto& r : settings::items_for(live, settings::Category::Plugins)) {
            if (r.indented) {
                check(!r.inactive,
                      "UX: a tool under a connected plugin is active (not dimmed)");
                check(r.hint == "disable",
                      "interaction: an active enabled tool toggles to 'disable'");
            }
        }

        // 一个FAILED服务器读作Bad.
        Model failed;
        mcp::ServerState bad;
        bad.name = "broken";
        bad.connected = false;
        bad.error = "spawn failed: no such file";
        failed.ui.plugins.servers.push_back(std::move(bad));
        for (const auto& r : settings::items_for(failed, settings::Category::Plugins)) {
            if (r.primary == "broken")
                check(r.status == settings::Item::Status::Bad,
                      "badge: a failed server is Status::Bad (⚠)");
        }
    }

    // ── 4:范围来源、信任和 HTTP 行数( 员额) -scope-migration) ──
    {
        // A PROJECT-scope 服务器带有其范围; a USER
        // 服务器不是(普通的大小写读作干净)。
        Model m;
        mcp::ServerState proj;
        proj.name = "repo-tool";
        proj.connected = true;
        proj.origin = mcp::Origin::Project;
        proj.config_dir = ".agentty";
        proj.tools.push_back({"t", "", true, false});
        m.ui.plugins.servers.push_back(std::move(proj));

        mcp::ServerState usr;
        usr.name = "my-tool";
        usr.connected = true;
        usr.origin = mcp::Origin::User;
        usr.config_dir = "/home/u/.agentty";
        m.ui.plugins.servers.push_back(std::move(usr));

        for (const auto& r : settings::items_for(m, settings::Category::Plugins)) {
            if (r.primary == "repo-tool") {
                check(r.scope_label == "project", "scope: project row labelled 'project'");
                check(r.config_dir == ".agentty",
                      "scope: project row carries its config dir (edit routing)");
                check(r.secondary.find("project") != std::string::npos,
                      "scope: project badge shows in the row text");
            }
            if (r.primary == "my-tool") {
                check(r.secondary.find("project") == std::string::npos
                      && r.secondary.find("user") == std::string::npos,
                      "scope: a user row is NOT badged (clean common case)");
            }
        }

        // 来,来,来 UNTRUSTED 工程服务器 : Enter APPROVES
        // (无) 切换 ),该行被标记为不可信,提示邀请信任.
        Model u;
        mcp::ServerState untrusted;
        untrusted.name = "risky";
        untrusted.origin = mcp::Origin::Project;
        untrusted.untrusted = true;
        untrusted.error = "untrusted project config — approve to enable";
        untrusted.config_dir = ".agentty";
        u.ui.plugins.servers.push_back(std::move(untrusted));
        for (const auto& r : settings::items_for(u, settings::Category::Plugins)) {
            if (r.primary == "risky") {
                check(r.untrusted, "trust: untrusted flag set on the row");
                check(r.action == settings::Action::ApprovePlugin,
                      "trust: Enter on an untrusted server APPROVES, not toggles");
                check(r.hint == "trust & enable",
                      "trust: hint invites approval");
                check(r.status == settings::Item::Status::Bad,
                      "trust: untrusted reads as attention (Bad)");
            }
        }

        // HTTP/SSE服务器(url,无命令)连接精细,并悬挂NOT旗号为
        // 'no command'——假-error固定.
        Model h;
        mcp::ServerState http;
        http.name = "remote";
        http.url = "https://mcp.example.com";
        http.connected = true;
        h.ui.plugins.servers.push_back(std::move(http));
        for (const auto& r : settings::items_for(h, settings::Category::Plugins)) {
            if (r.primary == "remote") {
                check(r.status == settings::Item::Status::Ok,
                      "http: a connected url-only server is healthy");
                check(r.secondary.find("no \"command\"") == std::string::npos,
                      "http: a url-only server is NOT flagged 'no command'");
            }
        }
    }
}

TEST_CASE("settings nav skips non-actionable rows") {
    using settings::Action;
    auto opened = [](const Model& mm) {
        return mm.ui.panel.get<pn::SettingsList>();
    };

    // 构建一个人口化插件模型:一个服务器,有两个工具。
    // 摘要是面板 头文件(不是一行),所以第0行是服务器。
    auto make_model = [] {
        Model m;
        mcp::ServerState s;
        s.name = "date";
        s.connected = true;
        s.tools.push_back({"current_date", "d", true, false});
        s.tools.push_back({"days_between", "m", true, false});
        m.ui.plugins.servers.push_back(std::move(s));
        return m;
    };

    // 打开服务器上的地盘,第一个可以操作的行.
    {
        auto [m, cmd] = app::update(make_model(),
            Msg{OpenSettingsList{settings::Category::Plugins}});
        auto rows = settings::items_for(m, settings::Category::Plugins);
        auto* o = opened(m);
        REQUIRE(o != nullptr);
        check(rows.size() == 1, "tools are collapsed: one row per server");
        check(o->index == 0 && rows[0].action == Action::TogglePlugin,
              "open lands on the server row");
    }

    // 从第一行搬上来 不 掉到后面的边上 头文件 (上面没有
    // 可操作的行) 保持原样或平面 -steps 永远不要降落在
    // 死人身上 头文件 以作为可操作性开始时的休息点)。
    {
        auto [m0, c0] = app::update(make_model(),
            Msg{OpenSettingsList{settings::Category::Plugins}});
        auto rows = settings::items_for(m0, settings::Category::Plugins);
        auto [m1, c1] = app::update(std::move(m0), Msg{SettingsListMove{-1}});
        auto* o = opened(m1);
        REQUIRE(o != nullptr);
        check(rows[o->index].action != Action::None,
              "moving up from the top actionable row stays on an actionable row");
    }

    // 移动跳跃在可操作行间(头文件从不一个着陆点).
    {
        auto [m0, c0] = app::update(make_model(),
            Msg{OpenSettingsList{settings::Category::Plugins}});
        auto rows = settings::items_for(m0, settings::Category::Plugins);
        Model m = std::move(m0);
        for (int i = 0; i < 4; ++i) {
            auto [mn, cn] = app::update(std::move(m), Msg{SettingsListMove{1}});
            m = std::move(mn);
            auto* o = opened(m);
            REQUIRE(o != nullptr);
            check(rows[o->index].action != Action::None,
                  "every down-step lands on an actionable row");
        }
    }

    // + 打开服务器的工具, + 关闭工具行,
    // 将光标放回服务器上。
    {
        auto [m, c] = app::update(make_model(), Msg{OpenSettingsList{settings::Category::Plugins}});
        std::tie(m, c) = app::update(std::move(m), Msg{SettingsListExpand{true}});
        auto rows = settings::items_for(m, settings::Category::Plugins);
        check(rows.size() == 3 && rows[0].expanded, "→ shows the server's two tools");
        std::tie(m, c) = app::update(std::move(m), Msg{SettingsListMove{1}});
        check(opened(m)->index == 1 && rows[1].indented, "down moves onto a tool");
        std::tie(m, c) = app::update(std::move(m), Msg{SettingsListExpand{false}});
        rows = settings::items_for(m, settings::Category::Plugins);
        check(rows.size() == 1, "← on a tool hides the list");
        check(opened(m)->index == 0, "and the cursor returns to the server");
        // ~ 在没有工具的服务器上,什么也没有.
        Model bare;
        mcp::ServerState s;
        s.name = "empty";
        s.connected = true;
        bare.ui.plugins.servers.push_back(std::move(s));
        std::tie(bare, c) = app::update(std::move(bare), Msg{OpenSettingsList{settings::Category::Plugins}});
        std::tie(bare, c) = app::update(std::move(bare), Msg{SettingsListExpand{true}});
        check(opened(bare)->expanded.empty(), "a server with no tools doesn't open");
    }

    // 完全的,我爱的 -informational 面板 (代理=已建) -ins 全部,请检查date=中的日期值 (
    // 帮助) Action::None )必须仍给予有效,在 -range 光标, 永远不要在移动中崩溃。
    {
        auto [m0, c0] = app::update(Model{},
            Msg{OpenSettingsList{settings::Category::Agents}});
        auto rows = settings::items_for(m0, settings::Category::Agents);
        auto* o = opened(m0);
        REQUIRE(o != nullptr);
        check(o->index >= 0 && o->index < static_cast<int>(rows.size()),
              "agents pane: cursor in range even when all rows are informational");
        auto [m1, c1] = app::update(std::move(m0), Msg{SettingsListMove{1}});
        auto* o1 = opened(m1);
        REQUIRE(o1 != nullptr);
        auto rows1 = settings::items_for(m1, settings::Category::Agents);
        check(o1->index >= 0 && o1->index < static_cast<int>(rows1.size()),
              "agents pane: move keeps the cursor in range (scrolls, no stick)");
    }
}
