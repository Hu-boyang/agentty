// settings_list_view.cpp 设置列表( : Ctrl +K − 设置,
// 以及 Plugins/Commands/Subagents/Hooks 列表打开。 覆盖面板 ,
// 建在房子上,我们 选择面板 部件是这样 frames/scrolls 和别人一样 选择面板.
//
// 两种模式, 都在这里 :
//   • 名单——从 settings::items_for 由一名协调员领导 HEALTH
//             徽章( 连接 / + 连接 或 注意 ) , 或者,
//             对于导航行, 是一个付薪箭头; ; Enter 行动在右边
//             -aligned 提示 + footer , 绝不是徽章。
//   • 增加——一个 -line 提示词 (头文件 与活的( ) 光标 ; 页脚显示
//             格式提示 + submit/cancel 键。

#include "agentty/runtime/view/panels.hpp"
#include "panels_common.hpp"   // kpanel * 宽度, panel_viewport_h

#include "agentty/runtime/view/helpers.hpp"
#include "agentty/runtime/view/hints.hpp"   // key_hints — 共享页脚平
#include "agentty/runtime/view/palette.hpp"
#include "agentty/runtime/panel/settings/list.hpp"
#include "agentty/runtime/panel/settings/items.hpp"
#include "agentty/runtime/fuzzy.hpp"   // 搜索点击突顯

#include <maya/widget/panel.hpp>
#include <maya/widget/panel/caret.hpp>

#include <algorithm>
#include <string>

namespace pn = agentty::ui::panel;

namespace agentty::ui {

using namespace maya;
using namespace maya::dsl;
namespace se = agentty::settings;

namespace {

struct Badge { std::string glyph; Color color; };

// 卫生徽章—— LEADING 上方的格言 -level 列,
// 一列显示其 STATE 浏览(连接/ 连接/ 失败), 每一个方式
// agentty 状态表面是。 Enter 动作( 移动/ 批准) 由页
// 脚 + 右侧 发送 -aligned 提示, 不 徽章——因此健康的
// 连接服务器从不穿恐怖的红色QQ.
Badge status_badge(se::Item::Status s) {
    switch (s) {
        case se::Item::Status::Ok:      return {"\xe2\x97\x8f", success};  // ● connected/healthy
        case se::Item::Status::Pending: return {"\xe2\x97\x8c", muted};    // 连接...
        case se::Item::Status::Bad:     return {"\xe2\x9a\xa0", warn};     // error/attention 国 家
        case se::Item::Status::Neutral:
        default:                        return {" ", muted};
    }
}

// 导航行( 4) RAG /Smart/外观/剖面),
// 跳到别处显示一个微妙的付得起的箭头而不是一个健康点——他们没有健康,
// 而是门. Return. std::nullopt 用于使用
// 状态徽章的行。
//
// 每一个动作 OPENS A PANE 外表不见了,
// 所以那排打开了最大的 面板 在设置中是唯一没有控件
// 的门——它读作一个值行,在你按下时碰巧做了一些事情
// Enter 箭头是付得起的 "this leads somewhere"
// 门没有它,只是惊喜。
std::optional<Badge> nav_badge(se::Action a) {
    // 从每扇门 枚举 自己的定义——不是重新列出的清单,
    // 而是任其漂移。
    if (se::opens_pane(a)) return Badge{"\xe2\x86\x92", highlight};   // →
    // 计算一个值不是门:它改变了什么 此处 ,所以它得到一个
    // 独特的格言 而不是一个意味着 "leads away".
    if (a == se::Action::CycleProfile) return Badge{"\xe2\x86\xbb", info};  // ↻
    return std::nullopt;
}

// 是否支持内置的`a`dd流量。
bool can_add(se::Category c) {
    return c == se::Category::Plugins
        || c == se::Category::Commands
        || c == se::Category::Agents;
}

// 电话 提示词 输入新条目时显示,以备关切。
const char* add_prompt(se::Category c) {
    switch (c) {
        case se::Category::Plugins:
            return "name command args\xe2\x80\xa6  (or --http url / --python f.py / "
                   "--uvx pkg / --npx pkg; add --project for the repo)";
        case se::Category::Commands: return "new command name (creates .agentty/commands/<name>.md)";
        case se::Category::Agents:   return "new subagent name (creates .agentty/agents/<name>.md)";
        default:                     return "";
    }
}

} // 命名空间结束。

Element settings_list_panel(const Model& m) {
    const auto* o = m.ui.panel.get<pn::SettingsList>();
    if (!o) return nothing();

    const bool adding = o->input_active;
    auto rows = se::items_for(m, o->concern);

    Panel::Config cfg;
    cfg.title      = std::string{" "} + se::label(o->concern) + " ";
    cfg.accent     = highlight;   // 青色,匹配命令调色板
    cfg.min_width  = panel_detail::kPanelWide;   // 数值+来源栏
    // 电话 SHARED 反应高度,不是硬码 14,一个比 终端
    // 滚动底部的顶部行为本地 回滚缓冲区 ——这是整个原因。
    // panel_viewport_h () 存在(
    // 见 pickers_common.hpp).
    cfg.viewport_h = panel_detail::panel_viewport_h();
    // 卷起像其他的滚 选择面板 ——插件 面板 可以超过任意
    // 可视区域 (服务器广告125工具为135+行),
    // 和卷轴 -less 面板画出溢出于 弹出面板 边框。
    cfg.scroll     = &m.ui.settings_list_scroll;
    cfg.selected   = adding ? -1 : o->index;

    // ── Header ─────────────────────────────────────────────────
    if (adding) {
        // 现场直播 提示词 让它拥有 面板 : 一个亮点
        // 提示词 与 真实 光标 ,其上方的格式提示暗淡。
        // cfg.selected  ⁇  -1已经删除列表突出,
        // 所以眼睛降落在这里.
        //
        // with_caret ,而不是尾部所附的格言: 光标 渲染
        // AT o > 曲线(%/%) 工作中间 -string ),
        // 下面有一个被长期粘贴的命令行窗口(QQ/QQ标记),
        // 而不是从右边缘走下来——与每个面板文本字段相同的机械.
        cfg.header.push_back(h(
            text("  "), text(add_prompt(o->concern), fg_dim(muted))
        ).build());
        std::size_t at = std::string::npos;
        const std::string shown = maya::panel::detail::with_caret(
            o->input, static_cast<std::size_t>(std::max(0, o->cursor)),
            panel_detail::panel_terminal_cols() - 12, &at);
        // 一样 光标 以每个面板字段: 终端 空白的闪烁块光标
        // 存储单元 ,颜色通过 OSC 12.
        const auto caret = Style{}.with_fg(highlight).with_conceal()
                                  .with_caret_anchor().with_caret_shape(1);
        cfg.header.push_back(h(
            text("  \xe2\x9c\x8e  ", fg_of(highlight)),         // 笔笔
            text(shown.substr(0, at), fg_bold(fg)),
            text(shown.substr(at, 3), caret),
            text(shown.substr(at + 3), fg_bold(fg))
        ).build());
    } else if (o->concern == se::Category::General) {
        // 枢纽搜索每个设置:过滤线为头文件.
        cfg.header.push_back(filter_header(o->query, "every setting", highlight));
    } else if (o->concern == se::Category::Plugins && !m.ui.plugins.servers.empty()) {
        // 模型从插件得到什么, 总是在视图( #98):
        // 数到第一行, 模式和成本。 不是列表行,
        // 所以它从不带走光标, 也从不卷走。
        const auto& pm = m.ui.plugins;
        const std::size_t total = pm.enabled_mcp();
        std::string line1, line2;
        if (total == 0)
            line1 = "no plugin tools enabled";
        else if (pm.on_demand_count == 0)
            line1 = std::to_string(total) + (total == 1 ? " tool" : " tools") + ", all loaded";
        else
            line1 = std::to_string(pm.loaded_count) + " loaded, "
                  + std::to_string(pm.on_demand_count) + " on demand";
        const std::string mode = pm.loading == "all"       ? "load all"
                               : pm.loading == "on_demand" ? "load on demand"
                                                           : "auto";
        line2 = mode;
        if (pm.schema_tokens > 0) {
            line2 += " \xc2\xb7 ~" + std::to_string((pm.schema_tokens + 500) / 1000) + "k tokens";
            if (pm.context_window > 0)
                line2 += " (" + std::to_string(pm.schema_tokens * 100 / pm.context_window)
                       + "% of context)";
        }
        if (pm.on_demand_count > 0) line2 += " \xc2\xb7 the model searches for the rest";
        cfg.header.push_back(h(text("  "), text(line1, fg_bold(fg))).build());
        cfg.header.push_back(h(text("  "), text(line2, fg_dim(muted))).build());
        if (pm.capped_by_provider)
            cfg.header.push_back(h(text("  "),
                text("this provider takes at most 128 tools at once; pin what you need with p",
                     fg_of(warn))).build());
    } else {
        cfg.header.push_back(h(
            text("  "), text(se::subtitle(o->concern), fg_dim(muted))
        ).build());
    }
    cfg.header.push_back(text(""));

    // ── 搜索结果( 带有查询的按钮) ─────────────────────────────
    if (se::is_search(*o)) {
        const auto hits = se::search(o->index_rows, o->query);
        if (hits.empty()) {
            cfg.prebuilt.push_back(text(
                "  no setting matches \"" + o->query + "\"", fg_italic(muted)));
        }
        for (int k = 0; k < static_cast<int>(hits.size()); ++k) {
            const auto& hit = o->index_rows[static_cast<std::size_t>(hits[static_cast<std::size_t>(k)])];
            Panel::Item row;
            row.badge       = "\xe2\x86\x92";   // ——打开面板.
            row.badge_style = fg_of(highlight);
            row.leading       = hit.label;
            row.leading_style = fg_of(fg);
            // 突出显示每个带类型的字,它点击标签的位置.
            row.highlight     = se::label_match(hit.label, o->query);
            row.highlight_fg  = highlight;
            // 目前的数值非常突出;它所住的地方保持安静。
            std::string where = se::pane_label(hit.pane);
            if (!hit.section.empty()) where += " \xc2\xb7 " + hit.section;
            if (!hit.value.empty()) {
                row.trailing       = hit.value;
                row.trailing_style = fg_of(info);
                row.origin         = where;
            } else {
                row.trailing       = where;
                row.trailing_style = fg_dim(muted);
            }
            row.help = hit.help;   // 部件仅在选定的行上显示
            cfg.items.push_back(std::move(row));
        }
        cfg.selected = hits.empty() ? -1 : o->index;
        cfg.footer.push_back(text(""));
        if (!hits.empty())
            cfg.footer.push_back(text(
                "  " + std::to_string(o->index + 1) + "/" + std::to_string(hits.size())
                    + " settings", fg_dim(muted)));
        cfg.footer.push_back(key_hints({{"\xe2\x86\x91\xe2\x86\x93", "move", 5},
                                        {"\xe2\x86\xb5", "open", 5},
                                        {"esc", "clear", 5}}));
        return Panel{std::move(cfg)}.build();
    }

    // ── 行( 添加时缩写, 以聚焦 提示词) ──────────
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const auto& it = rows[static_cast<std::size_t>(i)];
        // 顶级 -level 带一行的铅 HEALTH 徽章(状态),
        // 但纯导航行( RAG / 智能/ 外观/ 剖面图),它显示一个付
        // 薪箭头—— 见 nav_badge ,谁拥有该列表。
        const Badge b = nav_badge(it.action).value_or(status_badge(it.status));

        Panel::Item row;
        // 插件的工具行缩进到服务器下方 源码树 连接符(上一个为 QQ
        // / QQ), 暗号 on/off 复选框,以及淡化的文本—
        // —所以服务器/工具的层次顺序是一览读取,而不是一个相同的
        // 平面列表 -looking 点行。 顶端 -level 排行
        // 领先 health/nav 徽章和一个更亮的名字。
        if (it.indented) {
            // 上一个孩子 = 下一行是上-level行(或末端).
            const bool last =
                (i + 1 >= static_cast<int>(rows.size()))
                || !rows[static_cast<std::size_t>(i + 1)].indented;
            const char* elbow = last ? "\xe2\x94\x94\xe2\x94\x80 "   // └─
                                     : "\xe2\x94\x9c\xe2\x94\x80 ";  // ├─
            // 用来做一个坏的模式定义的工具 所以它不会被误认为是上。
            const bool bad    = it.status == se::Item::Status::Bad;
            const char* box   = bad ? "\xe2\x9a\xa0"
                                    : (it.on ? "\xe2\x97\x89" : "\xe2\x97\x8b"); // ⚠ / ◉ / ○
            row.badge       = std::string("  ") + elbow + box;
            // 连接器+框是结构的——暗;工具 NAME 携带 on/off。
            // 但当工具是 INACTIVE (其插件为 disabled/not
            // 连接) 整个 树的灰 名称和所有,所以它读作
            // "present but can't run," 无论每个
            // 工具的状态如何 `adding` 也使一切变得暗淡。
            const bool dim = adding || it.inactive;
            row.badge_style = fg_dim(muted);
            row.leading       = it.primary;
            row.leading_style = fg_of(dim ? muted : (it.on ? fg : muted));
            // 工具的描述,只在焦点行上,所以一个长的
            // 列表每个工具保留一行.
            if (i == o->index && !it.detail.empty())
                row.help = std::string{last ? "   " : "\xe2\x94\x82  "} + it.detail;   // │ keeps the tree
        } else {
            row.badge       = b.glyph;
            row.badge_style = fg_of(adding ? muted : b.color);
            // 带有工具的服务器显示其列表是否打开。
            row.leading       = it.expandable
                ? std::string{it.expanded ? "\xe2\x96\xbe " : "\xe2\x96\xb8 "} + it.primary   // ▾ / ▸
                : it.primary;
            row.leading_style = fg_of(adding ? muted : fg);
        }
        row.trailing       = it.secondary;
        row.trailing_style = fg_dim(muted);
        // 平板读作状态,而不是脚注: 重音颜色, 和其他的一样 。
        // "this is set\" 标记 面板.
        if (it.indented && it.pinned && !it.inactive) row.trailing_style = fg_of(highlight);
        if (it.indented && it.status == se::Item::Status::Bad)
            row.trailing_style = fg_of(warn);
        // 信息行( 无) Enter 动作) 运 PROSE 输入
        // `secondary` —
        // "press `a` to add one, or agentty plugin add <name>…"。（按 a 添加，或使用 agentty plugin add <name>。）
        // 作为线索 存储单元 说出正确的 -aligns 并争夺一行宽度(
        // 领先切换到 "(no plugins configure…"。
        // Prose属于 UNDER 相同,同为一行 Item::help
        // idiom调色板使用——全宽,显示在焦线上. 可操作行保持次要
        // 作为跟踪:它们的是短的参考数据(命令行,路径),而不是流派.
        if (it.action == se::Action::None && !it.indented) {
            row.help = it.secondary;
            row.trailing.clear();
        }
        // 武装驱离(第一次) `d` 把它画成一个明显的、
        // 可逆的危险 提示词 : 第二次 `d` 承诺,
        // 任何其他的裁军。
        if (!adding && !it.indented && !it.arg.empty()
            && o->confirm_remove == it.arg) {
            row.badge       = "\xe2\x9a\xa0";           // ⚠
            row.badge_style = fg_of(warn);
            row.leading_style = fg_bold(warn);
            row.trailing       = "press d again to remove";
            row.trailing_style = fg_bold(warn);
        }
        cfg.items.push_back(std::move(row));
    }

    // ── 页脚 ─────────────────────────────────────────────────
    if (adding) {
        // 注释行中的模式提示——与面板的编辑提示使用相同的槽,
        // 所以添加 提示词 读取方式与其它编辑模式相同(
        // 武装) -delete )),获得以下同等待遇.
        cfg.note = "typing \xc2\xb7 \xe2\x86\xb5 create \xc2\xb7 esc cancel";
    } else if (!o->confirm_remove.empty()) {
        cfg.note = "d confirm remove \xc2\xb7 any other key cancels";
    } else {
        cfg.footer.push_back(text(""));
        // 焦点行的动作提示,然后是键(+ `a add`).
        std::string act_hint;
        if (o->index >= 0 && o->index < static_cast<int>(rows.size()))
            act_hint = rows[static_cast<std::size_t>(o->index)].hint;
        if (!act_hint.empty())
            cfg.footer.push_back(h(
                text("  \xe2\x86\xb5 ", fg_of(highlight)),
                text(act_hint, fg_of(fg))
            ).build());

        // 共享的key_hints idom(hints.hpp),
        // 而不是 hand-built运行——每另一个选择面板的
        // 脚步都经过它,这个文件的漂移(它自己的间隔,它自己的修饰
        // 选择)正是帮助者所存在的来防止的. 有条件的动词保持有条件.
        std::vector<Hint> keys{{"\xe2\x86\x91\xe2\x86\x93", "move", 5},
                               {"\xe2\x86\xb5", "act", 5}};
        if (o->concern == se::Category::General) keys.push_back({"type", "search", 4});
        if (o->concern == se::Category::Plugins) {
            // 只有按键能做THIS行上的东西.
            const se::Item* cur = (o->index >= 0 && o->index < static_cast<int>(rows.size()))
                ? &rows[static_cast<std::size_t>(o->index)] : nullptr;
            const bool on_tool   = cur && cur->action == se::Action::ToggleTool;
            // 一个服务器行通常为TogglePlugin,
            // ApprovePlugin在一个不可信项目中. 状态更新函数接受
            // `e`和`d`在BOTH(settings_list.cpp)上,
            // 所以只检查TogglePlugin就把两个工作钥匙藏在一个用户最
            // 可能想要的地方 -- -- 决定如何处置一个未经批准的服务器.
            const bool on_server = cur
                && (cur->action == se::Action::TogglePlugin
                    || cur->action == se::Action::ApprovePlugin);
            if (cur && cur->expandable)
                keys.push_back({cur->expanded ? "\xe2\x86\x90" : "\xe2\x86\x92",   // ← / →
                                cur->expanded ? "hide tools" : "show tools", 4});
            if (on_tool && cur->on && !cur->inactive)
                keys.push_back({"p", cur->pinned ? "unpin" : "pin", 3});
            if (on_tool) keys.push_back({"\xe2\x86\x90", "back", 4});
            keys.push_back({"a", "add", 2, success});
            if (on_server) {
                keys.push_back({"e", "edit", 3});
                keys.push_back({"d", "remove", 3, warn});
            }
        } else if (can_add(o->concern)) {
            keys.push_back({"a", "add", 2, success});
        }
        keys.push_back({"esc", "close", 5});
        cfg.footer.push_back(key_hints(std::move(keys)));
    }

    return Panel{std::move(cfg)}.build();
}

} // namespace agentty::ui
