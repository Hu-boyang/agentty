// command_palette_test — 锁 编号 Ctrl +K 调色板行 目录 + 过滤器 UX.
//
// 调色板是纯数据( kCommands) + 一个纯函数( ) filtered_commands ),
// 所以这是便宜的单位 -testable 没有任何 UI/runtime.
// 双胞胎都穿这个 不变量 使每行都能够工作,并且可以发现:
//   1. CATALOG COMPLETENESS——每一命令枚举的价值完全一行。
//      缺少行 = 已死调色板条目; 分派器 开关时仍
//      会 编译 但命令永远无法被选中。
//   2. FILTER UX——匹配的标注段段+描述段+快捷键(发现者为:
//      标签点击率在描述值之上 -only点击率

#include "agtest.hpp"

#include "agentty/runtime/panel/palette.hpp"

#include <array>
#include <string_view>

using namespace agentty;


// 每一个 枚举 价值,这样我们就可以断言 目录 每一件都盖一次
static constexpr std::array kAll = {
    Command::NewThread, Command::ReviewChanges,
    Command::AcceptAll,
    Command::RejectAll, Command::CycleProfile, Command::OpenModels,
    Command::SwapModel,
    Command::OpenProviders, Command::OpenThreads, Command::OpenPlan,
    Command::RunCodeBlock, Command::InspectToolOutputs, Command::CompactContext,
    Command::RewindCheckpoint, Command::ForkThread,
    Command::OpenGeneralSettings,
    Command::OpenStats, Command::OpenSkills, Command::OpenLogin,
    Command::UpdateAgentty, Command::Quit,
};

static bool has_id(const std::vector<const CommandDef*>& v, Command id) {
    for (const auto* c : v) if (c->id == id) return true;
    return false;
}

TEST_CASE("command palette") {
    // ── 1. 目录 完整性( 4) 枚举 命令双弹出) ──────────────
    check(kCommands.size() == kAll.size(),
          "kCommands has exactly one row per Command enum value");
    for (Command id : kAll) {
        int n = 0;
        for (const auto& c : kCommands) if (c.id == id) ++n;
        check(n == 1, "each Command id appears exactly once in kCommands");
    }
    // 每行都有非-empty标签+描述(无空白调色板行).
    for (const auto& c : kCommands) {
        check(c.label && *c.label, "row has a label");
        check(c.description && *c.description, "row has a description");
        check(c.shortcut != nullptr, "row has a (possibly empty) shortcut");
    }

    // ── 2. 空查询返回每行, 按目录顺序 ────────────────
    {
        auto all = filtered_commands("");
        check(all.size() == kCommands.size(), "empty query returns all rows");
        bool ordered = true;
        for (std::size_t i = 0; i < all.size(); ++i)
            if (all[i]->id != kCommands[i].id) ordered = false;
        check(ordered, "empty query preserves catalog order");
    }

    // ── 3. 标签匹配 ────────────────────────────────────────────────────
    {
        auto r = filtered_commands("thread");
        check(has_id(r, Command::NewThread) && has_id(r, Command::OpenThreads),
              "\"thread\" matches New thread + Open threads");
    }

    // ── 4. DESCRIPTION 匹配(通过意图发现) ────────────────────────
    {
        // "diff" 只在“审查”修改中进行。 DESCRIPTION 而不是它的标签。
        auto r = filtered_commands("diff");
        check(has_id(r, Command::ReviewChanges),
              "\"diff\" finds Review changes via its description");
    }
    {
        // "summary"仅在Compact语境描述中出现.
        auto r = filtered_commands("summary");
        check(has_id(r, Command::CompactContext),
              "\"summary\" finds Compact context via description");
    }

    // ── 5. SHORTCUT 匹配( E) ─────────────────────────────────────────────────
    {
        auto r = filtered_commands("ctrl+g");
        check(has_id(r, Command::RunCodeBlock),
              "\"ctrl+g\" finds Run code block via its shortcut");
    }

    // ── 6. 案件-insensitive ───────────────────────────────────────────────
    {
        auto r = filtered_commands("QUIT");
        check(has_id(r, Command::Quit), "filter is case-insensitive");
    }

    // ── 6b. a 面板 下一层还剩 FINDABLE ───────────────────────
    // 外观没有调色板行( 一个门: 设置 ~ 外观 )。
    // 只有人们输入的词在门上找到时才有效。
    // 如果有人将设置描述剪切回"the live toggles",
    // 则失败 — 这就是重点。
    {
        for (std::string_view q : {"theme", "appearance", "colors", "motion",
                                   "density"}) {
            auto r = filtered_commands(q);
            check(has_id(r, Command::OpenGeneralSettings),
                  "a word for the Appearance pane reaches Settings");
        }
    }

    // ── 7. 标签点击级别 上方 说明 -only 点击数 ────────────────────
    {
        // "changes" 是在,在 LABELS 备注 AcceptAll/RejectAll/ReviewChanges 以及《
        // 公约》第2条 DESCRIPTION 备注 RewindCheckpoint ("...conversation...").
        // 无论什么标签匹配,都必须在任何描述-only匹配之前.
        auto r = filtered_commands("change");
        // 查找第一个描述 -only 匹配, 并显示最后的标签匹配;
        // 声明在描述后没有标签匹配。 -only 一个
        bool seen_desc_only = false, ok = true;
        for (const auto* c : r) {
            std::string_view lab{c->label};
            bool label_hit = false;
            std::string low;
            for (char ch : lab) low.push_back(
                static_cast<char>(std::tolower((unsigned char)ch)));
            label_hit = low.find("change") != std::string_view::npos;
            if (!label_hit) seen_desc_only = true;
            else if (seen_desc_only) ok = false;   // 标签在 desc-only 之后被击中
        }
        check(ok, "label matches rank above description-only matches");
    }

    // ── 8. 无对 = 空 ──────────────────────────────────────────────
    check(filtered_commands("zznotacommandzz").empty(),
          "a non-matching query returns no rows");
}

TEST_CASE("command palette — categories, gating, danger") {
    // ── every row has a category; danger rows are exactly the destructive set
    {
        // 丢弃工作或变异工作树的命令.
        auto is_expected_danger = [](Command c) {
            return c == Command::RejectAll || c == Command::RewindCheckpoint;
        };
        for (const auto& c : kCommands) {
            check(c.danger == is_expected_danger(c.id),
                  "danger flag matches the destructive-action set");
            // category_label 仅为普通( 退出 / 更新) 空。
            bool general = (c.category == Category::General);
            check(category_label(c.category).empty() == general,
                  "only General rows have no category badge");
        }
    }

    // ── 类别名称可以搜索(按区域发现) ────────────────
    {
        auto r = filtered_commands("changes");
        // 变化组群以组形式显示。
        check(has_id(r, Command::ReviewChanges) && has_id(r, Command::AcceptAll)
              && has_id(r, Command::RejectAll),
              "\"changes\" surfaces the whole Changes category");
    }

    // ── 可见度 : 已隐藏的 BULK 行, 输入点 保留 ──
    {
        // 没有 待处理 接受 = 接受 – 接受 i 接受 o i i o o diff = -all 拒绝 -all 消失,
        // 但,我还是要... "Review changes" (入口)仍然可以发现。
        PaletteContext no_diff;
        no_diff.has_pending_changes = false;
        auto r = filtered_commands("", no_diff);
        check(!has_id(r, Command::AcceptAll) && !has_id(r, Command::RejectAll),
              "no pending changes hides the bulk Accept/Reject-all");
        check(has_id(r, Command::ReviewChanges),
              "Review changes stays visible (entry point, toasts when empty)");
        check(has_id(r, Command::NewThread), "unrelated commands stay visible");

        // 有了分数 散装动作就会回来
        PaletteContext with_diff;
        with_diff.has_pending_changes = true;
        check(has_id(filtered_commands("", with_diff), Command::AcceptAll),
              "pending changes shows Accept-all");
    }
    {
        // 没有栅栏回复 – 隐藏运行代码块。
        PaletteContext no_block;
        no_block.has_code_block = false;
        check(!has_id(filtered_commands("", no_block), Command::RunCodeBlock),
              "no code block hides Run code block");
    }
    {
        // 没有可用的更新 – 更新 agentty 隐藏( 前) -existing 合同)。
        PaletteContext no_update;
        no_update.update_available = false;
        check(!has_id(filtered_commands("", no_update), Command::UpdateAgentty),
              "no update hides Update agentty");
    }

    // ── command_visible 上游同意过滤器 ─────────────────
    {
        PaletteContext ctx;
        ctx.has_pending_changes = false;
        for (const auto& c : kCommands) {
            bool in_filter = has_id(filtered_commands("", ctx), c.id);
            check(in_filter == command_visible(c, ctx),
                  "filter visibility == command_visible predicate");
        }
    }
}

TEST_CASE("command palette — scored fuzzy ranking") {
    auto rank_of = [&](const std::vector<CommandMatch>& v, Command id) -> int {
        for (int i = 0; i < (int)v.size(); ++i) if (v[(std::size_t)i].cmd->id == id) return i;
        return -1;
    };

    // ── "re" 标签 PREFIX 命中值在描述值上方 -only 匹配( E) ──────
    {
        auto r = match_commands("re", PaletteContext{});
        int rev = rank_of(r, Command::ReviewChanges);
        int rew = rank_of(r, Command::RewindCheckpoint);
        int rej = rank_of(r, Command::RejectAll);
        int nt  = rank_of(r, Command::NewThread);   // 匹配 desc 中的 "f-re-sh"
        check(rev >= 0 && rew >= 0 && rej >= 0, "the Re* commands all match");
        check(nt < 0 || (rev < nt && rew < nt && rej < nt),
              "label 're' prefix hits outrank a description-only 're'");
    }

    // ── 缩略语打字:"rcb" – 通过字段边界运行代码块 ───────
    {
        auto r = match_commands("rcb", PaletteContext{});
        check(!r.empty() && r.front().cmd->id == Command::RunCodeBlock,
              "'rcb' acronym-matches 'Run Code Block' first");
    }

    // ── 匹配标签字符的位置点 ──────────────────
    {
        auto r = match_commands("new", PaletteContext{});
        check(!r.empty() && r.front().cmd->id == Command::NewThread,
              "'new' → New thread first");
        // "New thread": 'n'、'e'、'w', 单位:千美元。
        const auto& pos = r.front().positions;
        check(pos.size() == 3 && pos[0] == 0 && pos[1] == 1 && pos[2] == 2,
              "positions mark the matched leading characters");
    }

    // ── 准确标签胜过一个也匹配的更长标签 ──────────
    {
        auto r = match_commands("quit", PaletteContext{});
        check(!r.empty() && r.front().cmd->id == Command::Quit,
              "'quit' → Quit ranks first (exact label)");
    }

    // ── 空查询: 目录 顺序, 无位置 ───────────────────────
    {
        auto r = match_commands("", PaletteContext{});
        check(r.front().cmd->id == kCommands.front().id,
              "empty query preserves catalog order");
        check(r.front().positions.empty(), "empty query has no highlight positions");
    }
}
