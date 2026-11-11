// form_view.cpp 项目a form::Form 打开 maya::Panel::Config.
//
// 此文件故意沉闷, 问题在于: 它绘制了控制一个字段的
// 地图 -for-one 并创造 NO 显示决定。 光标 ,
// 栏和浮动菜单属于 maya::Panel 中。
// 如果此处的更改开始为 Unicode 逃逸或颜色而到达,
// 则它属于部件。
//
// 这里执行的一个实质性规则:一个秘密被预测为LENGTH,
// 从来就没有它的价值. maya::panel::Secret没有字段可以持有平话,
// 因此凭据根本没有跨越渲染边界.

#include "agentty/runtime/view/form_panel.hpp"

#include <algorithm>
#include <filesystem>
#include <utility>

namespace agentty::ui {

namespace {

namespace ff = agentty::form::field;

// 探测路径字段, 使部件可以标记它 exists/missing.
// 完成这里( 视图每秒建一次) 帧 离开热门 传输 而不是在路径中
// 状态更新函数 ,必须保持纯洁。
[[nodiscard]] maya::panel::Path::State probe_path(const ff::Path& p) {
    if (p.value.empty()) return maya::panel::Path::State::Unknown;
    std::error_code ec;
    namespace fs = std::filesystem;
    const bool ok = p.want_dir ? fs::is_directory(p.value, ec)
                               : fs::is_regular_file(p.value, ec);
    return ok ? maya::panel::Path::State::Exists
              : maya::panel::Path::State::Missing;
}

[[nodiscard]] maya::PanelControl control_for(const agentty::form::Field& f,
                                            bool editing_this_row) {
    const std::size_t caret = agentty::form::caret_offset(f.value, editing_this_row);

    return std::visit([&](const auto& v) -> maya::PanelControl {
        using T = std::decay_t<decltype(v)>;

        if constexpr (std::is_same_v<T, ff::Toggle>) {
            return maya::panel::Toggle{v.on};
        }
        else if constexpr (std::is_same_v<T, ff::Choice>) {
            return maya::panel::Choice{std::string{v.label()}};
        }
        else if constexpr (std::is_same_v<T, ff::Number>) {
            // 在编辑时, 显示确切的 带类型的( 可能是空的 ):
            // 已清除的端口读作空域,而不是最小域。
            if (editing_this_row)
                return maya::panel::Text{agentty::form::number_text(v), caret,
                                         std::to_string(v.min) + "\xe2\x80\x93"
                                             + std::to_string(v.max)};
            return maya::panel::Number{v.value, caret};
        }
        else if constexpr (std::is_same_v<T, ff::Slider>) {
            return maya::panel::Slider{v.value, v.min, v.max, v.decimals};
        }
        else if constexpr (std::is_same_v<T, ff::Text>) {
            return maya::panel::Text{v.value, caret, {}};
        }
        else if constexpr (std::is_same_v<T, ff::Secret>) {
            // LENGTH 仅 —— 字典从未到达部件。
            return maya::panel::Secret{agentty::form::secret_filled(v), caret, v.placeholder};
        }
        else if constexpr (std::is_same_v<T, ff::Path>) {
            return maya::panel::Path{v.value, caret, probe_path(v), {}};
        }
        else if constexpr (std::is_same_v<T, ff::Pick>) {
            return maya::panel::Pick{v.label, v.placeholder};
        }
        else if constexpr (std::is_same_v<T, ff::Header>) {
            return maya::panel::Header{};
        }
        else if constexpr (std::is_same_v<T, ff::Action>) {
            maya::panel::Action a;
            a.status = v.status;
            a.hint   = v.hint;
            switch (v.tone) {
                case ff::Action::Tone::Busy:    a.tone = maya::panel::Action::Tone::Busy;  break;
                case ff::Action::Tone::Good:    a.tone = maya::panel::Action::Tone::Good;  break;
                case ff::Action::Tone::Bad:     a.tone = maya::panel::Action::Tone::Bad;   break;
                case ff::Action::Tone::Neutral: a.tone = maya::panel::Action::Tone::Neutral; break;
            }
            return a;
        }
        else {
            return maya::panel::Toggle{false};
        }
    }, f.value);
}

} // 命名空间结束。

maya::Panel::Config form_config(const agentty::form::Form& f, maya::Color accent,
                               maya::ScrollState* scroll, int viewport_h,
                               int max_width) {
    maya::Panel::Config cfg;
    cfg.title    = f.title;
    cfg.subtitle = f.subtitle;
    cfg.error    = f.error;
    // ── 脚印:每表面板(SSOT)的ONE语法 ───────────
    // 键行来源于表单 MODE 这里,不是拼写 面板 ——三 面板 每一个措辞,
    // 他们自己的脚印是如何 "no surprises" 死 个A 面板
    // 's `note` 只携带 面板-specific 额外( rag )
    // "a advanced" ,备注;注解 note_replaces_grammar
    // (武装清除)单独显示。
    {
        std::string line;
        if (!f.note.empty() && f.note_replaces_grammar) {
            line = f.note;
        } else {
            std::string grammar;
            // 语法描述了键盘ACTUALLY的功能.
            // 没有 保存 键: 留下一个字段来写,以及 Esc 叶子,
            // 编辑说 "↵ done" 因为... IS 写作。
            if (f.editing())        grammar = "type · ←→ caret · ↵ done · esc cancel";
            else if (f.choosing())  grammar = "↑↓ pick · ↵ choose · esc keep";
            else                    grammar = "↑↓ rows · type to edit · esc back";
            if (f.dirty && !f.editing() && !f.choosing())
                grammar += "  ·  unsaved";
            line = f.note.empty() ? grammar : (f.note + "   ·   " + grammar);
        }
        cfg.note = std::move(line);

        // maya REPLACES 说明本身
        // "editing · ↵ done · ↑↓ next field"
        // 每当一排直播, 除非呼叫者提供 editing_note。
        // 没有这个,任何 面板-specific 脚印在打字时悄悄
        // 消失--正是在打字的时候 沙箱 列表编辑器需要显示其完成候选人,
        // 以及为什么建议看起来从未出现过 渲染 一点都没有
        //
        // 只有在 面板 ASKED 以拥有此线( E) note_replaces_grammar).
        // 面板 仅仅预留了一笔额外,仍然得到 maya 编辑提示,
        // 因为这个提示是正确的话 当编辑和压倒它时,
        // 任何地方都会丢失 "↵ done".
        if (!f.note.empty() && f.note_replaces_grammar)
            cfg.editing_note = f.note;
    }
    cfg.selected = f.cursor;
    cfg.accent   = accent;
    cfg.scroll   = scroll;
    if (viewport_h > 0) cfg.viewport_h = viewport_h;
    // 一分钟 -width 宽于 终端 并非最基本,而是多余的。
    // 编号 覆盖面板 将面板中, 将多余部分分割开
    // 两者 边缘和标签栏消失。 帧 + 键
    // 覆盖面板 永远不要在地板下 继续显示一排
    if (max_width > 0)
        cfg.min_width = std::max(24, std::min(cfg.min_width, max_width - 6));

    cfg.items.reserve(f.fields.size());
    for (std::size_t i = 0; i < f.fields.size(); ++i) {
        const auto& src = f.fields[i];
        const bool on_row = (static_cast<int>(i) == f.cursor);
        // 光标 自动调试 FOCUSED 编辑会话是否开始。
        // 键入 -to-edit 表示行已经接受密钥——会话是从第
        // 一个字符开始的操作细节。 Hideing the
        // secretion 光标 直到随后公布了一种键盘不再具有的
        // 模式:用户看到了 "read-only row" 并按下键
        // Enter 改为 "start editing" ,
        // 这正是被移除的仪式。锁好的小排从来就没有获得过(它们没有钥匙)。
        const bool caret_row = on_row && src.is_text_like()
                            && src.editable() && !src.locked;

        maya::Panel::Item row;
        row.leading       = src.label;
        row.help          = src.help;
        row.origin        = src.origin;
        row.locked        = src.locked;
        row.locked_reason = src.locked_reason;
        row.error         = src.error;
        row.control       = control_for(src, caret_row);
        cfg.items.push_back(std::move(row));
    }

    // 开放的下拉,如果有的话。其内容来自 相同 函数 状态更新函数
    // 解决了对一个犯罪, 所以什么显示什么,
    // 什么 Enter 选择不能分开。
    if (const auto* d = f.dropdown()) {
        if (const auto* row = f.focused(); row && row->is_choice()) {
            const auto& c = std::get<ff::Choice>(row->value);
            const auto opts = agentty::form::visible_options(c);

            maya::Panel::Menu menu;
            menu.options     = opts.labels;
            menu.hints       = opts.hints;
            menu.highlighted = d->highlighted;
            // 电话 COMMITTED 值,与光标不同:列表标记 "what is saved" 与
            // ____________________和__________________________________
            // "where you are" 与QQ,并且用户移动时它们会有所区别.
            menu.current     = c.normalized(c.index);
            menu.scroll      = d->scroll;
            menu.viewport    = agentty::form::kDropdownViewport;

            cfg.menu     = std::move(menu);
            cfg.menu_row = f.cursor;
        }
    }

    return cfg;
}

} // namespace agentty::ui
