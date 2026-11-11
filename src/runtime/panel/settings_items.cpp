// settings_items.cpp——从装货机上搭建每个类别的行直播.

#include "agentty/runtime/panel/settings/items.hpp"
#include "agentty/util/home_dir.hpp"
#include "agentty/util/user_root.hpp"
#include "agentty/io/shared_file.hpp"

#include "agentty/runtime/model.hpp"
#include "agentty/domain/profile.hpp"
#include "agentty/domain/web_search_config.hpp"   // 网络搜索行摘要
#include <mcp/tools/web_search.hpp>                // 该摘要的服务标签
#include "agentty/domain/ui_theme.hpp"
#include "agentty/tool/plugin.hpp"
#include "agentty/tool/registry.hpp"
#include "agentty/tool/commands.hpp"
#include "agentty/tool/hooks.hpp"
#include "agentty/tool/util/sandbox.hpp"   // is_active/describe_state/config_enforceable

#include "agentty/runtime/fuzzy.hpp"      // 设置搜索评分

#include <algorithm>
#include <climits>
#include <cstdio>
#include <filesystem>
#include <cctype>
#include <cstdlib>
#include <fstream>

#include "agentty/mcp/client.hpp"   // PluginModel, plugin_model()
#include "agentty/tool/tool_plan.hpp"   // 工具加载模式 (# 98)
#include <sstream>

namespace fs = std::filesystem;

namespace agentty::settings {

namespace {

std::vector<Item> general(const Model& m) {
    std::vector<Item> out;

    // 权限 简介.
    {
        Item i;
        i.primary   = "Permission profile";
        i.secondary = std::string("current: ") +
                      std::string(to_string(m.d.profile));
        i.hint      = "Enter: cycle";
        i.action    = Action::CycleProfile;
        out.push_back(std::move(i));
    }
    // 聪明的模范。
    //
    // 两者 分支描述设置 执行。。 关闭的分支曾经是赤裸的字
    // "off" ,读成一行,其描述已丢失—— 和关闭是
    // DEFAULT ,所以这是每个新用户所看到的第一件事。
    // 状态词不是描述:行必须回答
    // 开启此选项会改变什么？
    // 因为它是唯一一个重要的时刻 答案是最重要的
    {
        Item i;
        i.primary   = "Smart Mode";
        i.secondary = m.d.smart.enabled
            ? "on \xc2\xb7 each turn routed to the cheapest model that can do it"
            : "off \xc2\xb7 every turn goes to the main model";
        i.hint      = "Enter: configure";
        i.action    = Action::OpenSmart;
        out.push_back(std::move(i));
    }
    // RAG 主动检索。
    {
        Item i;
        i.primary   = "Proactive retrieval (RAG)";
        i.secondary = "pre-turn context injection";
        i.hint      = "Enter: configure";
        i.action    = Action::OpenRag;
        out.push_back(std::move(i));
    }
    // 更改条。显示偏好,而不是动作,它决定是否 持久化
    // 横幅在编辑后坐落在作曲家之上。它曾经生活在旁边的调色板
    // 中 "Accept all changes" ,
    // 它把一个持久的设置放在两个不可逆动词旁边。
    {
        Item i;
        i.primary   = "Changes strip";
        i.secondary = m.d.show_changes_strip
            ? "shown · persistent \"N changes\" banner after edits"
            : "hidden · ^R still reviews";
        i.hint      = "Enter: toggle";
        i.action    = Action::ToggleChangesStrip;
        out.push_back(std::move(i));
    }
    // 聊天名称。每个新聊天需要一个便宜的请求, 并且向公用模式发送第一个信息,
    // 所以它是一个可见的切换,而不是一个 settings.json
    // -only 键。
    {
        Item i;
        i.primary   = "Chat names";
        i.secondary = m.d.persisted.auto_title
            ? "automatic \xc2\xb7 the utility model names a chat after its first reply"
            : "first line \xc2\xb7 named after the first message";
        i.hint      = "Enter: toggle";
        i.action    = Action::ToggleAutoTitle;
        out.push_back(std::move(i));
    }
    {
        Item i;
        i.primary   = "Auto-compact";
        i.secondary = m.d.persisted.auto_compact
            ? "on \xc2\xb7 summarises older turns when the context nears its limit"
            : "off \xc2\xb7 only /compact does it; old turns are trimmed to fit";
        i.hint      = "Enter: toggle";
        i.action    = Action::ToggleAutoCompact;
        out.push_back(std::move(i));
    }
    {
        // 98,每个模式都做什么,排成一行,所以没人要查
        const auto mode = agentty::tools::plan::loading_from(m.d.persisted.tool_loading);
        Item i;
        i.primary   = "MCP tool loading";
        switch (mode) {
            case agentty::tools::plan::Loading::Auto:
                i.secondary = "auto \xc2\xb7 loads plugin tools that fit, the model "
                              "searches for the rest";
                break;
            case agentty::tools::plan::Loading::All:
                i.secondary = "all \xc2\xb7 loads every plugin tool, up to the "
                              "provider's limit";
                break;
            case agentty::tools::plan::Loading::OnDemand:
                i.secondary = "on demand \xc2\xb7 loads only pinned and used tools, "
                              "the model searches for the rest";
                break;
        }
        i.hint      = "Enter: cycle";
        i.action    = Action::CycleToolLoading;
        out.push_back(std::move(i));
    }
    // 外观。 A 面板, 不是行: 它被分组并有自己的
    // 主题浏览器, 并且不能持有此平面列表。
    // 该行显示正在使用的主题, 因此打开它的最常见原因不打开它
    // 就被解答。
    {
        Item i;
        i.primary   = "Appearance";
        i.secondary = m.d.ui().theme.empty()
            ? "native \xc2\xb7 theme, density, motion, scrollback"
            : (m.d.ui().theme + " \xc2\xb7 theme, density, motion, scrollback");
        i.hint      = "Enter: configure";
        i.action    = Action::OpenAppearance;
        out.push_back(std::move(i));
    }

    // 沙箱。a。。 也请检查a。 面板 :它
    // 载有活墙报告和十几条组合行。第二行是值得从
    // 这里说的一个东西—— 后端实际上是执行的,
    // 因为那决定了里面的行是否做任何事情。
    // claybin 可以在打开。
    // 面板 并配置惰性的东西。
    {
        Item i;
        i.primary = "Sandbox";
        namespace sb = tools::util::sandbox;
        // describe_state()已经是渲染 "sandbox: active (claybin)"
        // 和n-backend的案例;从枚举中剥去它的前缀而不是re
        // -deriving的文本,因此这行不能与启动的横幅有分歧.
        std::string st = sb::describe_state();
        if (const auto c = st.find(": "); c != std::string::npos) st = st.substr(c + 2);
        if (!sb::is_active()) {
            i.secondary = st + " \xc2\xb7 commands run unconfined";
        } else if (sb::config_enforceable()) {
            i.secondary = st + " \xc2\xb7 files, network, syscalls, limits";
        } else {
            i.secondary = st + " \xc2\xb7 saved policy not enforced (needs claybin)";
        }
        i.hint      = "Enter: configure";
        i.action    = Action::OpenSandbox;
        out.push_back(std::move(i));
    }
    // 网易搜索. web_search 政策和它要求的服务。
    // 第二线说明目前的政策 执行 ,所以,
    // 我说 "why didn't it search?" 财务报告和已审计财务报表
    // "who answers?" 从列表中解答,而不打开 面板 从持续的记录中读取,
    // 其价值与 面板 编辑和工具层被递交。
    {
        const auto& s = m.d.persisted.web_search;
        Item i;
        i.primary = "Web Search";
        const auto mode = web_search_cfg::mode(s);
        if (mode == web_search_cfg::Mode::Off) {
            i.secondary = "off";
        } else {
            // 自动名称本身而不是一个计数:计数行没有在那里应用,
            // 所以显示一个可以描述一个无所作为
            // 的设置.
            i.secondary = mode == web_search_cfg::Mode::Auto
                ? std::string{"auto"}
                : "on \xc2\xb7 " + std::to_string(s.count) + " results";
            // 服务先问其菜单名。 此建筑的 id 并
            // 不知道它没有命名( 计划也跳过它 )。
            if (const auto* svc = ::mcp::tools::find_web_search_service(s.primary))
                i.secondary += " \xc2\xb7 " + svc->label;
            const auto n = web_search_cfg::exclude_list(s.exclude_sites).size();
            if (n == 1)      i.secondary += " \xc2\xb7 1 site excluded";
            else if (n > 1)  i.secondary += " \xc2\xb7 " + std::to_string(n)
                                          + " sites excluded";
        }
        i.hint   = "Enter: configure";
        i.action = Action::OpenWebSearch;
        out.push_back(std::move(i));
    }

    // 粘贴:如何粘贴 Ctrl +V 读取剪贴板。第二行命名了运行的路径,
    // 所以 "why didn't my image paste?" 从这里开始。
    {
        const auto& p = m.d.persisted.paste;
        Item i;
        i.primary = "Paste";
        std::string routes;
        auto add = [&](bool on, const char* name) {
            if (!on) return;
            if (!routes.empty()) routes += ", ";
            routes += name;
        };
        add(p.forwards, "laptop over ssh");
        add(p.display, "this machine");
        add(p.terminal != "off", "terminal");
        i.secondary = routes.empty() ? std::string{"every route off"}
                                     : routes + " \xc2\xb7 up to " + std::to_string(p.max_mb) + " MB";
        i.hint   = "Enter: configure";
        i.action = Action::OpenPaste;
        out.push_back(std::move(i));
    }

    // 列表的门。每张摘要回答"anything here?"
    // 而不打开。
    const auto door = [&](Category c, std::string secondary) {
        Item i;
        i.primary   = label(c);
        i.secondary = std::move(secondary);
        i.hint      = "Enter: open";
        i.action    = Action::OpenList;
        i.target    = c;
        out.push_back(std::move(i));
    };
    {
        const auto& pm = m.ui.plugins;
        std::size_t live = 0;
        for (const auto& s : pm.servers) if (s.connected) ++live;
        std::string s;
        if (m.ui.plugins_loading)    s = "connecting\xe2\x80\xa6";
        else if (pm.servers.empty()) s = "none \xc2\xb7 MCP servers";
        else s = std::to_string(pm.servers.size()) + " MCP servers \xc2\xb7 "
               + std::to_string(live) + " connected";
        door(Category::Plugins, std::move(s));
    }
    {
        const auto n = m.ui.library.commands.size();
        door(Category::Commands, n == 0 ? std::string("none \xc2\xb7 slash commands")
                                        : std::to_string(n) + " slash commands");
    }
    {
        const auto n = m.ui.library.project_agents.size();
        door(Category::Agents, n == 0 ? std::string("built-ins only")
                                      : "built-ins + " + std::to_string(n) + " project");
    }
    {
        const auto& lib = m.ui.library;
        door(Category::Hooks, lib.hooks_file.empty() ? std::string("none")
                            : lib.hooks_pending      ? std::string("not approved \xc2\xb7 hooks will not run")
                                                     : std::string("approved \xc2\xb7 active"));
    }
    return out;
}

std::vector<Item> plugins(const agentty::mcp::PluginModel& model, bool loading,
                          const std::vector<std::string>& expanded) {
    // 纯度投影 PluginModel 快照 OWNED BY 该
    // MODEL (m.ui.plugins) — 不 活下来 plugin_model
    // () 调用。这可以保持 items_for (因此,观点) Model ,
    // 所以,请 渲染 闸门 + 更新循环可以看到每个变化。
    // 这里没有调节 : 快照 已统一配置 vs 已存在 目录.
    std::vector<Item> out;

    if (model.servers.empty()) {
        Item i;
        if (loading) {
            i.primary   = "connecting to plugins…";
            i.secondary = "reading mcp.json and handshaking servers";
        } else {
            i.primary   = "(no plugins configured)";
            i.secondary = "press a to add one, or agentty plugin add <name> …";
        }
        i.hint      = "docs: plugins";
        out.push_back(std::move(i));
        return out;
    }

    // 装载摘要(counts, mode, cost)是视图绘制的
    // 面板 HEADER,因此它永远不会将光标或卷轴带走.

    for (const auto& s : model.servers) {
        Item i;
        i.primary   = s.name;
        i.on        = !s.disabled;
        i.arg       = s.name;
        // 证明: 标记范围并携带配置目录 remove/toggle
        // 编辑 此 服务器实际 mcp.json ,并不总是用户文件。
        // 用户 -scope 服务器不需要徽章(普通的大小写)。
        i.scope_label = std::string{agentty::mcp::to_string(s.origin)};
        i.config_dir  = s.config_dir;
        i.untrusted   = s.untrusted;
        const std::string scope_tag =
            (s.origin == agentty::mcp::Origin::User) ? "" : (i.scope_label + " · ");
        // 传阅服务器不会执行本地任何操作 —— 它们转发一个代理
        // -advertised 调用 ' 参数为 a URL
        // 标记行所以区别于 spawned/MCP 服务器可一目了然。
        const std::string kind_tag = s.passthrough ? "⇄ passthrough · " : "";
        // 一个工具是 EFFECTIVELY 除非其插件启用,
        // 否则不活动 和 连接——已禁用或仍在使用 -connecting 插件
        // 不能运行任何内容, 所以它的整个子树 渲染 暗淡(但保持单个状态).
        const bool subtree_inactive = s.disabled || !s.connected;
        if (s.disabled) {
            i.secondary = scope_tag + kind_tag + "disabled · "
                        + std::to_string(s.tools.size())
                        + (s.tools.size() == 1 ? " tool" : " tools")
                        + " — Enter to enable";
            i.status    = Item::Status::Neutral;   // 故意关闭——不是错误
        } else if (!s.error.empty()) {
            i.secondary = scope_tag + kind_tag + s.error;
            i.status    = Item::Status::Bad;
        } else if (!s.connected) {
            i.secondary = scope_tag + kind_tag + "connecting…";
            i.status    = Item::Status::Pending;
        } else if (s.passthrough) {
            // "active" ,没有 "connected" - 没有握手;
            // 工具 分派 编号 URL 显示 WHERE 调用 开始:
            // URL 代表本条目的全部信任故事。
            i.secondary = scope_tag + kind_tag
                        + std::to_string(s.enabled_count()) + " of "
                        + std::to_string(s.tools.size())
                        + (s.tools.size() == 1 ? " tool" : " tools")
                        + " → " + s.url;
            i.status    = Item::Status::Ok;
        } else {
            // 这个服务器现在的贡献是: 有多少工具,
            // 有多少模型在不搜索的情况下看到,什么是关闭的。
            std::size_t on_demand = 0, pinned = 0, skipped = 0;
            for (const auto& t : s.tools) {
                if (!t.enabled) continue;
                if (!t.skipped.empty()) { ++skipped; continue; }
                if (t.on_demand) ++on_demand;
                if (t.pinned) ++pinned;
            }
            const std::size_t enabled = s.enabled_count();
            const std::size_t off = s.tools.size() - enabled;
            std::string d = std::to_string(enabled) + (enabled == 1 ? " tool" : " tools");
            if (on_demand > 0) d += ", " + std::to_string(on_demand) + " on demand";
            if (pinned > 0)    d += ", " + std::to_string(pinned) + " pinned";
            if (off > 0)       d += ", " + std::to_string(off) + " off";
            if (skipped > 0)   d += ", " + std::to_string(skipped) + " skipped";
            i.secondary = scope_tag + d;
            i.status    = skipped > 0 ? Item::Status::Bad : Item::Status::Ok;
        }
        // Enter 切换 编号 整个 插件 on/off (可逆转)。
        // 删除是故意的 `d` 关键——破坏性行动不是默认 Enter.
        // 但是,一个 UNTRUSTED 工程服务器在配置得到保证之前,
        // 根本无法切换成生命, 所以那里 Enter APPROVES 换成是的
        if (s.untrusted) {
            i.action = Action::ApprovePlugin;
            i.hint   = "trust & enable";
        } else {
            i.action = Action::TogglePlugin;
            i.hint   = s.disabled ? "enable" : "disable";
        }
        out.push_back(std::move(i));

        // 工具崩溃,直到打开服务器:一个75-tool服务器是一行,
        // 而你滚动的列表是服务器列表.
        const bool open = std::find(expanded.begin(), expanded.end(), s.name) != expanded.end();
        if (!s.tools.empty()) {
            Item& srv = out.back();
            srv.expandable = true;
            srv.expanded   = open;
        }
        if (!open) continue;
        for (const auto& t : s.tools) {
            Item ti;
            ti.primary   = t.name;
            if (!subtree_inactive && t.enabled) {
                // 被打的脚步声 -demand : 针头总是 加载.
                if (!t.skipped.empty())
                    ti.secondary = "skipped: bad schema";
                else if (t.pinned)
                    ti.secondary = "pinned";
                else if (t.on_demand)
                    ti.secondary = "on demand";
            }
            // 工具的描述, 在焦点行下, 您可以在关闭一个前从
            // op_13 中得知 op_12。 一个跳过的
            // 工具显示的是 原因。
            ti.detail    = t.skipped.empty() ? t.description
                                             : "left out of requests: " + t.skipped;
            if (!t.skipped.empty()) ti.status = Item::Status::Bad;
            ti.action    = Action::ToggleTool;
            ti.arg       = s.name;
            ti.arg2      = t.name;
            ti.config_dir = s.config_dir;   // 将排除编辑到 此 服务器文件
            ti.indented  = true;
            ti.on        = t.enabled;
            ti.pinned    = t.pinned;
            ti.inactive  = subtree_inactive;
            // 在已禁用的插件下, 拖动一个工具毫无意义 —— 先启用插件。
            // 说吧, 而不是 "disable/enable"。
            ti.hint      = subtree_inactive ? ""
                                            : (t.enabled ? "disable" : "enable");
            out.push_back(std::move(ti));
        }
    }
    return out;
}

std::vector<Item> commands(const Library& lib) {
    std::vector<Item> out;
    for (const auto& c : lib.commands) {
        Item i;
        i.primary   = "/" + c.name;
        i.secondary = c.description;
        i.hint      = c.source;   // 项目 | 用户
        out.push_back(std::move(i));
    }
    if (out.empty()) {
        Item i;
        i.primary   = "(no slash commands)";
        i.secondary = "author one: .agentty/commands/<name>.md";
        i.hint      = "docs: slash-commands";
        out.push_back(std::move(i));
    }
    return out;
}

std::vector<Item> agents(const Library& lib) {
    std::vector<Item> out;
    // 建成-ins先(永远可用),然后是这个项目的自有.
    for (const char* b : {"explorer", "reviewer", "tester", "coder", "general"}) {
        Item i;
        i.primary   = b;
        i.secondary = "built-in";
        out.push_back(std::move(i));
    }
    for (const auto& a : lib.project_agents) {
        Item i;
        i.primary   = a;
        i.secondary = "project \xc2\xb7 .agentty/agents/" + a + ".md";
        out.push_back(std::move(i));
    }
    // 用户代理通过任务后端发现; 显示作者路径, 从而显示 面板
    // 属于自身 -documenting 甚至在任何存在之前
    Item hint;
    hint.primary   = "+ user subagents";
    hint.secondary = "author one: .agentty/agents/<name>.md "
                     "(frontmatter: tools, read-only)";
    hint.hint      = "docs: subagents";
    out.push_back(std::move(hint));
    return out;
}

std::vector<Item> hooks(const Library& lib) {
    std::vector<Item> out;
    const std::string& file = lib.hooks_file;
    if (file.empty()) {
        Item i;
        i.primary   = "(no hooks file)";
        i.secondary = "author one: .agentty/hooks.json "
                      "(pre_tool / post_tool)";
        i.hint      = "docs: hooks";
        out.push_back(std::move(i));
        return out;
    }
    Item i;
    i.primary = file;
    if (lib.hooks_pending) {
        i.secondary = "NOT APPROVED — hooks will not run";
        i.hint      = "Enter: review & approve";
        i.action    = Action::ApproveHooks;
        i.status    = Item::Status::Bad;
    } else {
        i.secondary = "approved — active";
        i.hint      = "";
        i.status    = Item::Status::Ok;
    }
    out.push_back(std::move(i));
    return out;
}



} // 命名空间结束。

std::vector<Item> items_for(const Model& m, Category cat) {
    switch (cat) {
        case Category::General:  return general(m);
        case Category::Plugins: {
            static const std::vector<std::string> none;
            const auto* o = m.ui.panel.get<ui::panel::SettingsList>();
            return plugins(m.ui.plugins, m.ui.plugins_loading, o ? o->expanded : none);
        }
        case Category::Commands: return commands(m.ui.library);
        case Category::Agents:   return agents(m.ui.library);
        case Category::Hooks:    return hooks(m.ui.library);
    }
    return {};
}

const char* pane_label(ListOpen::Pane p) noexcept {
    switch (p) {
        case ListOpen::Pane::Rag:        return "Retrieval";
        case ListOpen::Pane::Smart:      return "Smart Mode";
        case ListOpen::Pane::Appearance: return "Appearance";
        case ListOpen::Pane::Sandbox:    return "Sandbox";
        case ListOpen::Pane::WebSearch:  return "Web Search";
        case ListOpen::Pane::Paste:      return "Paste";
    }
    return "";
}

namespace {

// 一行的当前值作为短文本,即面板将显示的方式.
std::string value_text(const form::FieldValue& v) {
    namespace ff = form::field;
    return std::visit([](const auto& x) -> std::string {
        using T = std::decay_t<decltype(x)>;
        if constexpr (std::is_same_v<T, ff::Toggle>) return x.on ? "on" : "off";
        else if constexpr (std::is_same_v<T, ff::Choice>) return std::string{x.label()};
        else if constexpr (std::is_same_v<T, ff::Number>) return std::to_string(x.value);
        else if constexpr (std::is_same_v<T, ff::Slider>) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%.*f", x.decimals, x.value);
            return buf;
        }
        else if constexpr (std::is_same_v<T, ff::Text> || std::is_same_v<T, ff::Path>)
            return x.value;
        else if constexpr (std::is_same_v<T, ff::Secret>)
            return x.value.empty() ? std::string{} : std::string{"set"};   // 永远不设值
        else if constexpr (std::is_same_v<T, ff::Pick>) return x.label;
        else return {};
    }, v);
}

std::string trim(std::string s) {
    const auto b = s.find_first_not_of(' ');
    return b == std::string::npos ? std::string{} : s.substr(b);
}

bool contains_ci(std::string_view hay, std::string_view needle) {
    const auto lc = [](char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; };
    return std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
                       [&](char a, char b) { return lc(a) == lc(b); }) != hay.end();
}

// 查询的单词。 每个单词必须匹配行以计数。
std::vector<std::string_view> words_of(std::string_view query) {
    std::vector<std::string_view> words;
    for (std::size_t i = 0; i < query.size();) {
        while (i < query.size() && query[i] == ' ') ++i;
        const auto j = query.find(' ', i);
        const auto end = j == std::string_view::npos ? query.size() : j;
        if (end > i) words.push_back(query.substr(i, end - i));
        i = end;
    }
    return words;
}

}  // 命名空间结束。

std::vector<int> label_match(std::string_view label, std::string_view query) {
    std::vector<int> out;
    for (const auto w : words_of(query))
        if (auto fm = fuzzy::score(label, w); fm.matched())
            out.insert(out.end(), fm.positions.begin(), fm.positions.end());
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

void index_form(std::vector<ListOpen::Hit>& out, const form::Form& f,
                ListOpen::Pane pane) {
    std::string section;
    for (const auto& row : f.fields) {
        if (row.is_header()) { section = trim(row.label); continue; }
        if (row.is_action()) continue;   // 按钮("Test","Save")不是设置
        ListOpen::Hit h;
        h.label   = trim(row.label);
        h.help    = row.help;
        h.value   = value_text(row.value);
        h.section = section;
        h.pane    = pane;
        h.row     = row.id;
        out.push_back(std::move(h));
    }
}

std::vector<int> search(const std::vector<ListOpen::Hit>& index,
                        std::string_view query) {
    // 查询的每个单词必须匹配行中的某个地方;标签数最多,
    // 所以"port"将一个端口列排在它帮助的仅提及端口
    // 的端口之上.
    const auto words = words_of(query);
    if (words.empty()) return {};
    std::vector<std::pair<int, int>> scored;   // (分数,指数)
    for (int i = 0; i < static_cast<int>(index.size()); ++i) {
        const auto& h = index[static_cast<std::size_t>(i)];
        const std::string where = std::string{pane_label(h.pane)} + " " + h.section;
        int total = 0;
        bool all = true;
        for (const auto w : words) {
            const auto l = fuzzy::score(h.label, w);
            const auto s = fuzzy::score(where, w);
            // 帮助只算作一个简单的下弦:一个句子上的模糊度几乎
            // 与任何事物都匹配.
            const bool in_help = contains_ci(h.help, w);
            int best = INT_MIN;
            if (l.matched()) best = l.score * 3;
            if (s.matched()) best = std::max(best, s.score);
            if (in_help)     best = std::max(best, 1);
            if (best == INT_MIN) { all = false; break; }
            total += best;
        }
        if (all) scored.emplace_back(total, i);
    }
    std::stable_sort(scored.begin(), scored.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<int> out;
    out.reserve(scored.size());
    for (const auto& [s, i] : scored) out.push_back(i);
    return out;
}

namespace {

// Whitespace-split 一条线成符号.
std::vector<std::string> split_ws(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    std::string tok;
    while (in >> tok) out.push_back(std::move(tok));
    return out;
}

// A plugin/command NAME 符号 : [A] -Za-z0-9_ :-],无 -empty 长度 -capped.
// `:` 这里允许, 因为它可以命令命名空间( git: fixup)
// 和插件名称; create_starter () 将其映射到一个子目录,
// 而不是将其写入一个文件名(在下面是非法的) Windows).
constexpr std::size_t kMaxNameLen = 96;   // 整个光谱
constexpr std::size_t kMaxSegLen  = 64;   // 结肠之间的一个路径段

bool valid_name(const std::string& n) {
    if (n.empty() || n.size() > kMaxNameLen) return false;
    // 一个领先者 '-' 是一个 标志 用户错误( E) e.g. `--http url` ,
    // 而不是一个名为“没有名称”的插件名称——拒绝而不是薄荷
    // "--http".
    if (n.front() == '-') return false;
    for (char c : n)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_'
              || c == '-' || c == ':'))
            return false;
    return true;
}

// 验证 a create_starter 名称 和 将其分割为路径段 ':'.
// 每个段必须是安全的文件名片段:n-empty、long -capped、
// 无线索点/破折号(避免隐藏文件和选项-looking名称),
// 绝不是"."/".."(路径逃逸)。
std::vector<std::string> starter_segments(const std::string& name) {
    if (name.empty() || name.size() > kMaxNameLen) return {};
    std::vector<std::string> segs;
    std::string cur;
    auto flush = [&]() -> bool {
        if (cur.empty() || cur.size() > kMaxSegLen) return false;
        if (cur == "." || cur == "..") return false;
        if (cur.front() == '.' || cur.front() == '-') return false;
        for (char c : cur)
            if (!(std::isalnum(static_cast<unsigned char>(c))
                  || c == '_' || c == '-'))
                return false;
        segs.push_back(std::move(cur));
        cur.clear();
        return true;
    };
    for (char c : name) {
        if (c == ':') { if (!flush()) return {}; }
        else          cur.push_back(c);
    }
    if (!flush()) return {};
    if (segs.size() > 3) return {};   // 匹配装填器的 kMax 深度嵌入
    return segs;
}

} // 命名空间结束。

AddResult add_plugin_from_line(Io io, const lock_levels::below_file_lane& cap, const std::string& line) {
    auto tok = split_ws(line);
    // 拖动可选 --project 标志 任何一行的,其余的就是... ...
    // <name> <recipe> 说吧,说吧
    bool project = false;
    std::vector<std::string> pos;
    for (auto& t : tok) {
        if (t == "--project") project = true;
        else pos.push_back(std::move(t));
    }
    if (pos.size() < 2)
        return {false, "usage: <name> <command> [args…]  ·  <name> --http <url>  ·  "
                       "<name> --passthrough <url> <tool>[,<tool>…]  ·  "
                       "<name> --python file.py / --uvx pkg / --npx pkg  "
                       "[--project]"};
    const std::string name = pos[0];
    if (!valid_name(name))
        return {false, "plugin name may use letters, digits, _ - : only"};

    tools::plugin::ServerSpec spec;
    spec.name = name;
    const std::string& recipe = pos[1];
    std::vector<std::string> rest(pos.begin() + 2, pos.end());

    if (recipe == "--python") {
        if (rest.empty()) return {false, "--python needs a script path"};
        spec.command = "python3";
        std::error_code ec;
        fs::path abs = fs::absolute(rest[0], ec);
        // 拒绝非 -existent 脚本上方的- 否则插件会
        // 写到 mcp.json 并且只在下次重装时在产卵时间
        // 不透明地失败,没有提示路径是twico.
        if (!ec && !fs::is_regular_file(abs, ec))
            return {false, "no such script: " + abs.string()};
        if (!ec) rest[0] = abs.string();
        spec.args = std::move(rest);
    } else if (recipe == "--uvx") {
        if (rest.empty()) return {false, "--uvx needs a package name"};
        spec.command = "uvx";
        spec.args = std::move(rest);
    } else if (recipe == "--npx") {
        if (rest.empty()) return {false, "--npx needs a package name"};
        spec.command = "npx";
        spec.args.push_back("-y");
        for (auto& t : rest) spec.args.push_back(std::move(t));
    } else if (recipe == "--http" || recipe == "--sse") {
        // 远程 传输层 url,没有本地命令,没有信任 -gated
        // 接下来的重载上连接起来 接下来的再重载上
        if (rest.empty()) return {false, std::string{recipe} + " needs a url"};
        if (rest[0].rfind("http://", 0) != 0 && rest[0].rfind("https://", 0) != 0)
            return {false, "url must start with http:// or https://"};
        spec.url  = rest[0];
        spec.type = (recipe == "--sse") ? "sse" : "http";
    } else if (recipe == "--passthrough") {
        // 外国工具a proxy/gateway 向模型发布广告( e.g.
        // LiteLLM + 头部注射 `headroom_retrieve`
        // : 登记这些表格 DISPATCH — agentty 邮递员 调用
        // 参数为 <url> 返回 正文 - 不作广告(代理拥有 模式定义).
        if (rest.size() < 2)
            return {false, "--passthrough needs <url> <tool>[,<tool>…]"};
        if (rest[0].rfind("http://", 0) != 0 && rest[0].rfind("https://", 0) != 0)
            return {false, "url must start with http:// or https://"};
        spec.url  = rest[0];
        spec.type = "passthrough";
        // 工具:剩余符号,每个可能为逗号-separated.
        for (std::size_t r = 1; r < rest.size(); ++r) {
            std::string_view sv{rest[r]};
            while (!sv.empty()) {
                const auto comma = sv.find(',');
                std::string_view tn = sv.substr(0, comma);
                if (!tn.empty()) spec.passthrough.emplace_back(tn);
                if (comma == std::string_view::npos) break;
                sv.remove_prefix(comma + 1);
            }
        }
        if (spec.passthrough.empty())
            return {false, "--passthrough needs at least one tool name"};
    } else {
        spec.command = recipe;
        spec.args = std::move(rest);
    }

    const auto path = tools::plugin::config_path(io, project);
    switch (tools::plugin::add_server(io, cap, path, spec, /*力=*/false)) {
        case tools::plugin::EditResult::Ok:
            return {true, "added " + std::string{project ? "project " : ""}
                          + "plugin '" + name + "'"};
        case tools::plugin::EditResult::AlreadyExists:
            return {false, "'" + name + "' already exists"};
        case tools::plugin::EditResult::ParseError:
            return {false, "mcp.json is not valid JSON — fix it by hand"};
        default:
            return {false, "could not write mcp.json"};
    }
}

AddResult create_starter(Io io, Category cat, const std::string& name) {
    // 校验 和 拆分开 ':' 输入安全路径段。 command/agent 装填器
    // 映射a `:`-namespaced 名称改为 a SUBDIRECTORY
    // (git: 修补 - > git/fixup.md ),所以将缩写逐字写入
    // 一个文件名中,两者都失败了 Windows (`:` 非法),并不符合装载器。
    const std::vector<std::string> segs = starter_segments(name);
    if (segs.empty())
        return {false, "name: letters/digits/_/- per segment, ':' to nest, "
                       "≤3 levels, no leading dot or dash"};
    const fs::path uroot = util::user_root();
    if (uroot.empty()) return {false, "no HOME to write under"};

    const char* sub = nullptr;
    std::string tmpl;
    if (cat == Category::Commands) {
        sub = "commands";
        tmpl = "---\ndescription: " + name + " command\n"
               "argument-hint: <args>\n---\n"
               "Do the thing for $ARGUMENTS.\n";
    } else if (cat == Category::Agents) {
        sub = "agents";
        tmpl = "---\ndescription: " + name + " agent\n"
               "read-only: false\n"
               "# tools: read grep glob list_dir   # optional allowlist\n"
               "---\nYour role: " + name +
               ". Complete the delegated task end-to-end, then report.\n";
    } else {
        return {false, "create_starter only supports commands/agents"};
    }

    // 构建 <user-root>/<sub>/<seg1>/<seg2>/<leaf> .
    // md 从经验证的段段(全部) -but-last 属于目录。 -safe (无)
    // '.'/'..' /分离者),所以不可能有路径逃逸. user_root 荣誉
    // $AGENTTY_HOME 和同一基础 skills/commands 扫描梯度解析
    // ( E) scope::Env::user_native_base).
    fs::path dir = uroot / sub;
    for (std::size_t i = 0; i + 1 < segs.size(); ++i) dir /= segs[i];
    const fs::path file = dir / (segs.back() + ".md");

    // 创建了整体,并且只有在名称是免费的时:没有存在-then-write 竞态,
    // 有一个编辑器或第二个agentty制作相同的文件.
    if (!persistence::create_exclusive(io, file, tmpl)) {
        std::error_code ec;
        if (fs::exists(file, ec)) return {false, "already exists: " + file.string()};
        return {false, "could not create " + file.string()};
    }

    // 强制重新扫描, 新的条目会在下一个打开时显示。
    if (cat == Category::Commands) tools::commands::invalidate_cache();
    return {true, "created " + file.string() + " — edit it, then reopen"};
}

} // namespace agentty::settings
