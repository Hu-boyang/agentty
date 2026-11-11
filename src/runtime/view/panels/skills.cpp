// skills.cpp - 读数 -only 技术观看者。
//
// 纯适配器: 构建 maya::Panel::Config 发件人 Model 状态;
// 部件拥有每个铬决定。 共享脚手架 : panels_prologue.hpp.
//
// ── 这个面板是用来做什么的 ───────────────────────────────────────────────
//
// 它在用户不离开TUI的情况下回答了四个问题:
//
//   装了什么?
//   它申报了什么?
//   副作用 标签 信封上写着什么?
//   agentty 发现,在详细页脚
//
// 不错 不 批准任何事情,这是故意的遗漏,而不是空隙:
// Anthropic 批准率93% Claude 代码 权限
// 提示词 ,而您已经滚动的列表中一个键盘的批复是实现惯用“是
// ”的最快途径。批复的后面是 `agentty skill approve NAME` ,
// 在同意屏幕上可以显示结果 和
// 编号 正文.
//
// ── 行布局 ───────────────────────────────────────────────────────────
//
//   名称 只有当某物需要人类后继执行官网时才使用
//   名称 PENDING
//
// 色彩带有严重性,但绝不是单独:每个州都有一个单词或格言,
// 因为一个色彩盲用户和一个`theme::native`
// 终端都不得不能够读到这个.

#include "panels_prologue.hpp"

#include "agentty/runtime/panel/skills.hpp"
#include "agentty/tool/skills.hpp"

namespace agentty::ui {

Element skills_panel(const Model& m) {
    auto* o = m.ui.panel.get<pn::Skills>();
    if (!o) return nothing();

    Panel::Config cfg;
    cfg.title      = " Skills ";
    cfg.min_width  = kPanelStandard;
    cfg.viewport_h = panel_viewport_h();
    cfg.scroll     = &o->scroll;
    cfg.selected   = o->rows.empty() ? -1 : o->index;

    // ACCENT遵循最糟糕的状态现今,因此边界本身
    // 在读完单行之前就携带了头条.
    const auto flagged = o->flagged_count();
    const auto pending = o->pending_count();
    cfg.accent = flagged ? danger : (pending ? warn : highlight);

    cfg.items.reserve(o->rows.size());
    for (const auto& r : o->rows) {
        Panel::Item row;

        // 格言,不只是一种颜色: "!!" 以示纪念,
        // "!" 未核准 -but-clean ,普通字的两处空格,
        // 所以名称在单空格列中保持对齐。
        const char* mark = r.has_critical          ? "!! "
                         : r.shadow_conflict       ? "!! "
                         : (r.gated && !r.trusted) ? " ! "
                                                   : "   ";
        row.leading = std::string{mark} + r.name;
        row.leading_style = (r.has_critical || r.shadow_conflict) ? fg_of(danger)
                          : (r.gated && !r.trusted) ? fg_of(warn)
                                                    : fg_of(fg);

        // 追踪:它所宣称的,然后是它的信任状态。 "prose"
        // 而不是一无所有——一栏空栏读作缺失数据,
        // "declares nothing" 是个真正的答案。
        std::string decl = tools::skills::effects_to_frontmatter(r.effects);
        if (decl.empty()) decl = "prose";

        std::string state;
        if (r.gated) state = r.trusted ? "approved" : "PENDING";

        std::string trailing = decl;
        if (!state.empty()) trailing += " \xc2\xb7 " + state;   // ·
        row.trailing = std::move(trailing);
        row.trailing_style = (r.gated && !r.trusted) ? fg_of(warn)
                                                     : fg_dim(muted);

        cfg.items.push_back(std::move(row));
    }

    // ── 选定行的详情 ──────────────────────────────────────
    // 列表的答案 "what is installed" ; 页脚回答
    // "what is THIS one" ,因此用户从不必离开面板来找出为什么
    // 一行被标记.
    cfg.footer.push_back(text(""));

    if (const auto* sel = o->selected()) {
        if (!sel->description.empty()) {
            // 扫描时已经消毒——安全 渲染。
            // (可以打开新线的描述可以
            // 冒充周围的标题。)
            cfg.footer.push_back(text("  " + sel->description, fg_dim(muted)));
            cfg.footer.push_back(text(""));
        }

        // 证义. "you wrote this" 数字
        // "this arrived from elsewhere" 是一个最决定 -relevant
        // 说到技能,所以它得到一个简单的句子,而不是一个字段标签。
        if (!sel->origin.empty())
            cfg.footer.push_back(text("  from " + sel->origin, fg_dim(muted)));
        else if (sel->source == "project")
            cfg.footer.push_back(text("  came with this repo", fg_dim(muted)));
        else
            cfg.footer.push_back(text("  you wrote this", fg_dim(muted)));

        cfg.footer.push_back(text("  " + sel->dir, fg_dim(muted)));

        // 一个名字碰撞隐藏了一个完整的技能,所以在发现之前,
        // 人们就直言不讳地说——用户无法以其他方式发现它.
        if (sel->shadow_conflict) {
            cfg.footer.push_back(text(""));
            cfg.footer.push_back(
                text("  another skill in this scope declares the same name.",
                     fg_of(danger)));
            cfg.footer.push_back(
                text("  one of them is not loaded \xe2\x80\x94 agentty skill list",
                     fg_dim(muted)));
        }

        // 观众看到什么?
        // CLI 显示完整的组合。
        if (!sel->findings.empty()) {
            cfg.footer.push_back(text(""));
            cfg.footer.push_back(text("  agentty noticed:", fg_of(fg)));

            int shown = 0;
            for (const auto& f : sel->findings) {
                if (shown++ >= 4) break;
                const bool crit =
                    f.severity == tools::skills::Finding::Severity::Critical;
                std::string line = "   ";
                line += crit ? "!! " : " ! ";
                if (f.line > 0) line += "line " + std::to_string(f.line) + "  ";
                line += f.detail;
                cfg.footer.push_back(
                    text(std::move(line), crit ? fg_of(danger) : fg_of(warn)));
            }
            if (sel->findings.size() > 4)
                cfg.footer.push_back(
                    text("   \xe2\x80\xa6 " +
                             std::to_string(sel->findings.size() - 4) +
                             " more \xe2\x80\x94 agentty skill list",
                         fg_dim(muted)));

            // 诚实的警告是,每次发现都显示。 没有这些,
            // 一份空白的发现清单就成为了健康方面的一个干净的法案。
            cfg.footer.push_back(text(""));
            cfg.footer.push_back(
                text("  pattern matching, not proof \xe2\x80\x94 and no findings"
                     " is not a clean bill.",
                     fg_dim(muted)));
        }

        // 动作,命名但不受约束,说 WHERE 决策的生命
        // 比一个使决策变得太容易的关键更有用。
        if (sel->gated && !sel->trusted) {
            cfg.footer.push_back(text(""));
            cfg.footer.push_back(
                text("  not approved \xe2\x80\x94 the agent cannot load it.",
                     fg_of(warn)));
            cfg.footer.push_back(
                text("  review and approve:  agentty skill approve "
                         + sel->name,
                     fg_dim(muted)));
        }
    } else {
        cfg.footer.push_back(
            text("  No skills installed.", fg_dim(muted)));
        cfg.footer.push_back(
            text("  Drop a SKILL.md under ~/.agentty/skills/, or:", fg_dim(muted)));
        cfg.footer.push_back(
            text("  agentty skill add ./path", fg_dim(muted)));
    }

    // 摘要行:头条数,只有在有东西需要计算时才会进行.
    // 沉默是健康集的正确输出.
    if (flagged || pending) {
        cfg.footer.push_back(text(""));
        std::string sum = "  ";
        if (flagged)
            sum += std::to_string(flagged)
                 + (flagged == 1 ? " skill flagged" : " skills flagged");
        if (flagged && pending) sum += ", ";
        if (pending)
            sum += std::to_string(pending) + " awaiting approval";
        cfg.footer.push_back(text(std::move(sum),
                                  flagged ? fg_of(danger) : fg_of(warn)));
    }

    cfg.footer.push_back(key_hints({
        {"\xe2\x86\x91\xe2\x86\x93", "move", 5},   // ↑↓
        {"Esc", "close", 4},
    }));

    return Panel{std::move(cfg)}.build();
}

} // namespace agentty::ui
