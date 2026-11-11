// checkpoints.cpp - 倒车检查站 选择面板.
//
// 纯适配器: 构建 maya::Panel::Config 发件人 Model 状态;
// 部件拥有每个铬决定。 共享脚手架 : panels_prologue.hpp.

#include "panels_prologue.hpp"
#include <maya/core/anim_clock.hpp>   // anim_now : 数据 帧 时钟

namespace agentty::ui {


// 倒车检查站 选择面板。每个检查点用户转弯一行(最老的顶部,
// 最接近作曲者的最下部的最新——与记录的空间顺序相同)。
// 每行:转弯号+1 -line 提示词 预览(铅),
// 和 "Nm ago · diffstat" (拖累)
// diffstat显示工作树的改变 SINCE 倒影从不盲目
// Enter 倒转; 破坏性文件+拖放返回是已有的
// RestoreCheckpoint 流动。
Element checkpoints_panel(const Model& m) {
    auto* o = m.ui.panel.get<pn::Checkpoints>();
    if (!o) return nothing();

    // 相对 "time ago" 从墙上 -clock ms 印章 — 本地到视图;
    // 不存在共享帮助符, 这里的语法是 选择面板-specific.
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        m.wall_at(maya::anim_now()).time_since_epoch()).count();
    auto ago = [now](std::int64_t ts_ms) -> std::string {
        if (ts_ms <= 0) return {};
        std::int64_t s = (now - ts_ms) / 1000;
        if (s < 0)     s = 0;
        if (s < 45)    return "just now";
        if (s < 3600)  return std::to_string(s / 60)   + "m ago";
        if (s < 86400) return std::to_string(s / 3600) + "h ago";
        return std::to_string(s / 86400) + "d ago";
    };

    Panel::Config cfg;
    cfg.title      = " Rewind to Checkpoint ";
    cfg.accent     = warn;
    cfg.min_width  = kPanelStandard;
    cfg.viewport_h = panel_viewport_h();
    cfg.scroll     = &m.ui.checkpoints_scroll;
    cfg.selected   = o->entries.empty() ? -1 : o->index;

    cfg.items.reserve(o->entries.size());
    for (int i = 0; i < static_cast<int>(o->entries.size()); ++i) {
        const auto& e = o->entries[static_cast<std::size_t>(i)];
        Panel::Item row;
        row.leading = "#" + std::to_string(e.turn) + "  " + e.preview;
        row.leading_style = fg_of(fg);

        // 拖曳:"<time> · <diffstat>". diffstat是用
        // 亚眠填充;直到那时,一个微妙的椭圆形,这样一行就不会跳动.
        std::string when = ago(e.timestamp_ms);
        std::string stat;
        switch (e.diff_state) {
            case checkpoints::Entry::DiffState::Loading:
                stat = "\xe2\x80\xa6";   // …
                break;
            case checkpoints::Entry::DiffState::Failed:
                stat = "";
                break;
            case checkpoints::Entry::DiffState::Ready:
                if (e.clean) {
                    stat = "no changes";
                } else {
                    stat = std::to_string(e.files_changed)
                         + (e.files_changed == 1 ? " file" : " files");
                    if (e.insertions > 0) stat += " +" + std::to_string(e.insertions);
                    if (e.deletions  > 0) stat += " \xe2\x88\x92" + std::to_string(e.deletions); // −
                }
                break;
        }
        std::string trailing = when;
        if (!stat.empty())
            trailing += (when.empty() ? "" : " \xc2\xb7 ") + stat;
        row.trailing = std::move(trailing);
        // 绿色当真实的倒转时( 已改变), 暗淡时( 变换时) clean/no-stat.
        const bool has_changes =
            e.diff_state == checkpoints::Entry::DiffState::Ready && !e.clean;
        row.trailing_style = has_changes ? fg_of(success) : fg_dim(muted);
        cfg.items.push_back(std::move(row));
    }

    cfg.footer.push_back(text(""));
    cfg.footer.push_back(text(
        "  Restores files and rewinds the transcript here.",
        fg_dim(muted)));
    cfg.footer.push_back(key_hints({
        {"\xe2\x86\x91\xe2\x86\x93", "move", 5},   // ↑↓
        {"Enter", "rewind", 3},
        {"Esc", "cancel", 4},
    }));

    return Panel{std::move(cfg)}.build();
}


} // namespace agentty::ui
