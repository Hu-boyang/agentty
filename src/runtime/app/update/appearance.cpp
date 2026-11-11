// appearance_update-状态更新函数代表外观面板。
//
// agentty的外观如何：主题、颜色层、背景极性、密度、运动、
// 镶边。一个 面板，一条规则 — 每个 行已生效。
//
// ── 为什么没有应用步骤 ───────────────────────────────────────────
// 主题由LOOKING判断。如果选择和查看被 保存 击键
// 分开（更不用说重新启动），那么评估方案的唯一方法就是首先提交它，
// 这完全是倒退。因此，每一行都会通过更改它的击键进行写
// 入：改变 `m.d.ui()`，保留到用户存储，重建表单，
// 然后用它绘制下一个 帧。 `Form::dirty`
// 永远不会在这里设置，因为它永远不可能是真的。
//
// ── 为什么主题浏览器会浮在 面板 上 ───────────────────────────
// 它确实NOT取代了它。 面板 保持在下面绘制，
// 移动突出显示会立即应用该方案 - 因此该列表是它自己的预览，
// 您可以根据真实的 UI 而不是样本来判断主题。
// Esc 恢复你进来的任何内容； Enter 保留您所看到的内容。
//
// ── 为什么出现是USER设定 ─────────────────────────────────────
// 光终端是你的眼睛的属性，而不是仓库的属性。这些会进入用户商店并
// 在结账期间跟随您，
// 而不是项目 .agentty/settings.json.

#include "agentty/domain/ui_theme.hpp"
#include "agentty/i18n/startup.hpp"   // resolve_language 自动
#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/app/deps.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include <maya/core/overload.hpp>

#include "agentty/runtime/panel/appearance.hpp"
#include "agentty/runtime/panel/form_keys.hpp"

namespace pn = agentty::ui::panel;

namespace agentty::app::detail {

using maya::overload;
namespace up = agentty::ui_prefs;

namespace {

// 将首选项保留到USER存储。
//
// 遍历persist_settings而不是直接保存`m.d.persisted`：
// 只有`ui`被别名到记录中(Domain::ui()返回persisted.ui)。
// `effort`、`model_id`、`profile`、
// `smart`和活动提供者作为SEPARATE域字段存在，
// 因此记录的原始保存写入了在init()时间保存的所有内容，
// 并默默地恢复了此后所做的每一个更改——主题调整将撤消模型切换。
// persist_settings先将分散的字段同步到记录中，然后是保存，然后写回。
//
// 仍然在Deps 接口边界处写入-behind，因此状态更新函数永远
// 不会使磁盘上的帧停顿；这就是为什么每次击键都可以节省成本。
[[nodiscard]] Cmd persist(Model& m) {
    return persist_settings(m);
}

// 从首选项重建 面板 的行。该表格是 `m.d.ui()` 的
// PROJECTION，而不是它的第二个副本：状态更新函数 编辑
// 首选项和 re-derives，因此不存在行与事实不一致的路径。
// 光标和焦点被转移，因为用户在列表中的位置
// 不是改变的部分。
void reproject(Model& m) {
    auto* o = m.ui.panel.get<pn::Appearance>();
    if (!o) return;
    form::rebuild(o->pane.form, pn::build_appearance_form(m.d.ui(), m.env.terminal));
}

// 重建密封的抄本以便主题改变到达它。
//
// m.ui.frozen 容纳 BUILT 元素 —
// 密封一次后就不再重建，因为对于内联 回滚缓冲区 分类账，
// 它们的高度必须保持 字节-stable。颜色是在构建
// 元素时解析的，因此这些转变会保留密封时有效的任何
// 主题：选择一个新的方案，屏幕上有对话，镶边会重新样式，
// 而上面的文字则不会。 线程 越高，屏幕出现错误的部分就越多。
//
// 该 IS HISTORY。现在颜色已晚-bound（
// docs/LATE_BINDING.md）：密封元素带有符号槽并
// 在PAINT解析，因此它遵循实时主题而无需重建。保留上面的段落
// 是因为它准确地指出了失败，并且因为下面的推理 - 关于 re
// -seal 的成本 - 正是我们停止这样做的原因。
}  // 命名空间结束。

// 将 新 主题发布到屏幕上已有的所有内容。
//
// 在internal.hpp中声明：外部链接，因此其成本保持可测量（
// theme_preview_cost_probe)，因为这在主题浏览器中的EVERY箭头键上
// 运行——"moving applies it, you are looking at the preview"
// 是该面板的整体设计。
//
// ── 为什么现在这才是出版,而不是重建 ───────────────────────
//
// 它用于通过 rehydrate_frozen() 重新 -seal
// 冻结的前缀，并删除每个已确定回合的构建元素，因为两者都将传出调色板
// RESOLVED 放入其中，并且没有其他东西会使它们无效（主题切换
// 不会更改 字节，因此没有内容 -keyed 缓存 通知）。
//
// 支付该成本有两件事是错误的，
// 第二件事是整个方法被放弃的原因：
//
//   1. 绑定晚下无需。这些元素不是 过期。
//
//   2.它随文字记录缩放，并且预览击键没有
//      那么多时间。 Re-rendering 每个开关提交的内容在 100
//      -message 线程 上每次按键测量为 2.42 -> 4.65 毫秒，
//      并且随着长度的增加而增长；过去的一个 key-repeat 插槽（在 30/s 处
//      为 33 毫秒）键超出了 帧、帧 合并的速度，并且浏览器会明显更新您所经过的
//      每个 第二 主题。 "Slow" 和 "skips every other entry"
//      是一种症状，这也是其原因。
//
// 高度稳定性是 re-seal 危险而不仅仅是缓慢的原因：在不同高度
// 的重建会改变 maya 提交的 回滚缓冲区 前缀，
// check_scrollback 失败，帧 降级到 过期，
// 并且恢复是完整的 -viewport 重绘 (~33 KB 而不是 13 字节)。
// 在 30 keys/sec 处，即 ~1 MB/s，终端 无法复合。
// 根本不重建比小心重建既便宜又安全。
//
// STRUCTURAL pref (密度、紧凑转弯）在种类上
// 是不同的：它改变行HEIGHTS，这使账本记录的测量值而不是它们
// 的颜色无效。那些保持向前的-only——它们适用于从那时起渲染的回合，
// 并且故意不复古-applied，因为re-sealing在新的高度撕裂了账本。
// 请参阅下面的 pull_field。
void restyle_sealed_turns(Model& m) {
    // 发布主题。这就是 ALMOST 的全部工作。
    //
    // 颜色是 LATE-BOUND (docs/LATE_BINDING.md)：内
    // 置元素携带符号 `Color::slot(...)` 值，
    // 并且 maya 在 Style::to_sgr() 中针对 PAINT 时间
    // 存在的任何主题解析它们。因此，十个主题前构建的密封回合可以在当前主题下正确绘制，
    // 而不会被触及，并且 StylePool::retheme() re
    // -derives 交换上缓存的 SGR 字节 — id 保持有效，没有画布 存储单元
    // 需要重写。
    //
    // 该函数用于在每个箭头键上重建整个密封的记录，
    // 因为提交的元素确实已经将传出调色板烘焙到其中。
    // 每次击键的重建时间为 O（脚本），并且在较长的 线程 上，
    // 它的成本超过了 key-repeat 插槽 - 按键超过了 帧、
    // 帧 合并，并且浏览器明显更新了您通过的每个 第二 主题。
    // "Slow" 和 "skips entries"
    // 是一种症状。 绑定 后期删除了工作而不是优化它。
    //
    // publish_theme驱动BOTH接收器（参见ui_theme.hpp)：
    // agentty自己的令牌原子和maya::app_set_theme，
    // 其碰撞主题纪元并重新-derives每个投影调色板。只发布一半是它自己
    // 的错误 - 来自投影的 markdown 散文 渲染，因此当 chrome
    // 移动时，一半 -publish 将散文留在旧方案上。
    //
    // Cheap 和 幂等:view() 接下来 帧
    // 再次发布相同的值，并且发布是指针存储加上值比较。
    ui_prefs::publish_theme(*ui_prefs::resolve(m.d.ui(), m.env.terminal).theme);

    // 无内容 ELSE TO DO。
    //
    // 这是值得一读的部分，因为它曾经是四十行缓存手术，
    // 而手术是导致主题切换缓慢的原因。
    //
    // 它用于删除ViewCache::finalized (每个
    // 已确定消息的构建元素），然后删除调用
    // rehydrate_frozen()以重建密封前缀，
    // 因为两者都将具有传出调色板RESOLVED的元素保存到其中。
    // 过期 也不再是：这些元素带有符号槽，因此它们在绘制时实时绘制主题。
    // 删除并重建它们将在每次击键 O(transcript) 时重新
    // -derive，这已经是正确的了。
    //
    // 该路径有意对NOT处理的一件事是对行HEIGHTS（密度、
    // 紧凑转弯）的改变。这些是设计上的向前-only：re
    // -sealing在新高度的冻结转弯撕裂了回滚缓冲区账本，
    // 因此他们从那时起而不是追溯地在渲染的转弯上采取副作用。
    // 请参阅下面 pull_field 的注释。
}

// Re-render已经在屏幕上，对于改变内置元素的
// CONTENT的首选项，而不仅仅是其插槽解析为哪种颜色。
//
// 主题交换不需要这些（参见restyle_sealed_turns)：
// 槽在绘制时解析，因此存储的元素遵循新调色板不变。
// 这些行的种类不同：
//
//   ·层——决定方案渲染是否为真彩色、256、16或
//                 完全不是。这是解析值的 QUANTISATION，
//                 在构建元素时应用，因此已经-built
//                 的值保留旧的近似值。
//   ·极性——选择墨水位于画布的哪一侧，哪一侧
//                 完全改变resolve()交回的主题。
//   ·语法——re-tokenises将每个代码块转换成不同的跨度源码树。
//                 完全没有颜色变化；结构性的。
//
// 所有三个都达到相同的已经-built元素，并且所有三个用于在
// 下一回合重新绘制之前不进行任何改变。
void rebuild_rendered_content(Model& m) {
    ui_prefs::publish_theme(*ui_prefs::resolve(m.d.ui(), m.env.terminal).theme);
    m.ui.view_cache.clear_settled();
    if (m.ui.frozen_through == 0) return;
    rehydrate_frozen(m);
}

namespace {

// 将行的当前表单值应用回首选项。
//
// 通过构建器发出的 id 上的总切换来由行 id
// 键入：位置表必须手动保存在构建器的顺序中，
// 并且没有行携带的 id 是 编译器 无法看到的静默死设置。
// （因此两边都有命名的 kAp* 常数。）
//
// 返回改变的行是否影响RESOLVED COLOUR，
// 因此调用者知道重新-render屏幕上已经存在的内容。
// 主题并不是唯一这样的行：层决定方案 渲染 是否为真彩色、
// 256、16 或根本不；极性选择墨水位于画布的哪一侧；
// 并对每个代码块进行语法高亮 重绘。所有三个都达到了相同的
// 已经-built 主题元素，并且所有三个都没有进行任何更改，
// 直到下一个回合重新绘制。
//
// 结构行（密度、紧凑转弯）在该组中被故意设置为NOT —参见
// restyle_sealed_turns:它们改变行HEIGHTS，
// 并且重新-sealing，在新高度的冻结转弯撕裂了回滚缓冲区分类帐。
[[nodiscard]] bool pull_field(Model& m, const form::Field& f) {
    const auto idx = [&]() -> int {
        const auto* c = std::get_if<form::field::Choice>(&f.value);
        return c ? c->normalized(c->index) : 0;
    };
    const auto on = [&]() -> bool {
        const auto* t = std::get_if<form::field::Toggle>(&f.value);
        return t && t->on;
    };

    up::Prefs& p = m.d.ui();
    if (f.id == pn::kApTier)            { p.tier     = static_cast<up::ColorTier>(idx()); return true; }
    else if (f.id == pn::kApLang) {
        // LANGUAGE行，它是这里读取id而不是索引的一个地方。
        // 该选择带有 BCP-47 标记（""、"de"、
        // "pt-BR"），因此稍后将语言插入表中不能默默地更改
        // 现有 settings.json 的含义（索引会更改该
        // 含义）。
        if (const auto* c = std::get_if<form::field::Choice>(&f.value)) {
            const int i = c->normalized(c->index);
            p.lang = (i >= 0 && i < static_cast<int>(c->ids.size()))
                         ? c->ids[static_cast<std::size_t>(i)]
                         : std::string{};
        }
        // 应用NOW，而不是在重新启动时。 t() 是指针读取，
        // 因此切换是一个原子存储，下一个 帧 被翻译 - 沙箱 密封
        // 在这里没有类似物，因为语言不是边界（
        // docs/design/i18n.md）。
        //
        // 空意味着自动：来自环境的re-resolve而不是离开任何活动的，
        // 或者在德语之后选择自动将保持德语直到重新启动并读取为
        // "the setting did nothing"。
        i18n::Lang want{};
        if (p.lang.empty()) want = i18n::resolve_language({}, {});
        else if (!i18n::parse_tag(p.lang, want)) want = i18n::Lang::en;
        (void)i18n::set_active(want);   // 如果没有目录则拒绝；保持最新状态

        // FALSE，这是性能-critical排的一半。
        //
        // 返回 true 运行 rebuild_rendered_content()，
        // 它会丢弃每个已固定的元素并重新水化冻结的前缀。
        // 这对于 tier/polarity/syntax 来说是正确的，
        // 它的更改将 INTO 烘焙为内置元素。这是灾难性的，
        // 因为语言行是 CHOICE：按住 -> 循环它，因此每次击键
        // 都会针对整个转录本进行重建。
        //
        // 在 800-message 线程 上测量：每个密钥 43 毫秒，
        // 相对于 33 毫秒的密钥-repeat 插槽。过去的预算键
        // 超出了 帧、帧 合并，并且该行明显更新了您通过的每种 第二 语言
        // - 主题浏览器已经修复过一次的确切故障（请参阅
        // restyle_sealed_turns 及其上面的注释）。
        //
        // 面板本身仍然re-translates:reproject()
        // 在下一行上从新的目录重建形式，并且其由面板而不是抄本界定。
        // 保留在旧语言中的是已经解决的TRANSCRIPT
        // 文本——这是相同的forward-only规则密度，
        // 并且遵循compact-turns，出于同样的原因（
        // docs/design/i18n.md）：翻译的行具有不同
        // 的宽度并且通常具有不同的高度，
        // 因此re-sealing它在新的高度撕裂了回滚缓冲区分类帐。
        return false;
    }
    else if (f.id == pn::kApPolarity)   { p.polarity = static_cast<up::Polarity>(idx());  return true; }
    else if (f.id == pn::kApSyntax)     { p.syntax   = on();                              return true; }
    else if (f.id == pn::kApDensity)    p.density     = static_cast<up::Density>(idx());
    else if (f.id == pn::kApMotion)     p.motion      = static_cast<up::Motion>(idx());
    else if (f.id == pn::kApScrollback) {
        // 列出了 Auto-first，它不是 枚举 顺序：按 id 映射，如 kApLang 那样。
        if (const auto* c = std::get_if<form::field::Choice>(&f.value)) {
            const int i = c->normalized(c->index);
            const std::string_view chosen =
                (i >= 0 && i < static_cast<int>(c->ids.size())) ? std::string_view{c->ids[static_cast<std::size_t>(i)]}
                                                                 : std::string_view{};
            for (auto v : {up::Scrollback::Auto, up::Scrollback::Tiny, up::Scrollback::Small,
                           up::Scrollback::Big, up::Scrollback::Full})
                if (up::label(v) == chosen) p.scrollback = v;
        }
    }
    else if (f.id == pn::kApToolOutput) p.tool_output = static_cast<up::ToolOutput>(idx());
    else if (f.id == pn::kApThinking)   p.thinking    = static_cast<up::Thinking>(idx());
    else if (f.id == pn::kApTimestamps) p.timestamps  = static_cast<up::Timestamps>(idx());
    else if (f.id == pn::kApCompact)    p.compact_turns = on();
    else if (f.id == pn::kApThreadPeek) p.thread_preview = on();
    else if (f.id == pn::kApForeign)    p.foreign_sessions = on();
    else if (f.id == pn::kApProseWidth) {
        if (const auto* n = std::get_if<form::field::Number>(&f.value))
            p.prose_width = static_cast<int>(form::number_value(*n));
    }
    return false;
}

// 浏览器当前突出显示的主题。 Empty == native，
// 这也是空列表产生的结果 - 范围的安全端，因为 native
// 是每个 终端 上正确的一个选择。
//
// 读取选择面板自己的访问器：游标被夹在
// 过滤集的内部，因此没有第二个位置可以不
// 同意选择哪一行。
[[nodiscard]] std::string highlighted_theme(const pn::Appearance& o) {
    const std::string* sel = o.pane.picker.picker.selected();
    return sel ? *sel : std::string{};
}

// 移动浏览器的突出显示和LIVE-APPLY它所落在的位置。
// 应用是重点：仅在提交时预览的 选择面板 使您可以
// 采用该方案来了解它的样子。
//
// 只有 SEALED 转录本的重新设计成本很高——
// restyle_sealed_turns()放弃了已确定的视图 缓存
// 和 re-parses 各自保留消息的降价，在真实的 线程 上测量为
// 37-82 毫秒，因为随后的 渲染 必然是冷的。其他所有内容（浏览器本身、
// 作曲家、状态栏）重绘 来自已发布的主题，免费。
//
// 因此，当没有任何密封的重建内容时，重建被跳过，
// 这是人们实际浏览的常见情况：新会话，或者足够短以至于
// 没有任何东西被冻结的会话。主题以任何方式发布，
// 因此预览是相同的 - 只有 has-built 行的 catch
// -up 是有条件的。
void move_highlight(Model& m, int delta) {
    auto* o = m.ui.panel.get<pn::Appearance>();
    if (!o) return;
    // 选择面板围绕其OWN过滤计数进行环绕和钳制，
    // 因此状态更新函数从不导出列表几何结构。
    // 这就是防止光标和渲染窗口偏离的原因——只有其中之一。
    // 换行，因为回到615方案的顶部不应该花费
    // 614次击键。
    o->pane.picker.picker.move_wrapping(delta);

    std::string next = highlighted_theme(*o);
    // 登陆已经生效的计划是no-op，而不是重建。
    // 对于 615 行，换行或重复按键的频率足够重要。
    if (next == m.d.ui().theme) { reproject(m); return; }

    m.d.ui().theme = std::move(next);
    // Re-style屏幕上的所有内容，包括冻结的前缀——它是在每个帧绘制的，
    // 因此跳过它的预览将显示最新回合的新方案和上面的旧方案。
    // 由 frozen_row_budget() 限制到 ~3 个视口，
    // 因此这是 O（可见），而不是 O（脚本）：在 3000
    // -message 线程 上约为 0.64 毫秒。
    restyle_sealed_turns(m);
    reproject(m);
}

}  // 命名空间结束。

form::Form appearance_form_for_search(const Model& m) {
    return pn::build_appearance_form(m.d.ui(), m.env.terminal);
}

Cmd appearance_update(Model& m, msg::AppearanceMsg am) {
    return std::visit(overload{

        [&](OpenAppearance& e) -> Cmd {
            pn::Appearance o;
            o.pane.form = pn::build_appearance_form(m.d.ui(), m.env.terminal);
            (void)form::focus_row(o.pane.form, e.focus);
            m.ui.panel.descend(std::move(o));
            m.ui.appearance_scroll.y = 0;
            return Cmd::none();
        },

        [&](CloseAppearance&) -> Cmd {
            // Esc展开一级——至打开时隐藏的调色板快照，
            // 或至线程。出去时对 保存 没有任何影响：都是 已保存，
            // 因为它是 带类型的。
            ascend(m);
            return Cmd::none();
        },

        [&](AppearancePaste& e) -> Cmd {
            // 像打字一样落在聚焦行；编辑结束时提交。
            if (auto* o = m.ui.panel.get<pn::Appearance>(); o && !o->pane.picking)
                (void)form::paste_into_focused(o->pane.form, e.text);
            return Cmd::none();
        },
        [&](AppearanceKey& e) -> Cmd {
            auto* o = m.ui.panel.get<pn::Appearance>();
            if (!o) return Cmd::none();

            // 浏览器在键盘启动时拥有键盘。
            if (o->pane.picking) return Cmd::none();

            const auto applied = form::keys::apply(o->pane.form, e.action);

            if (applied.close)
                return appearance_update(m, msg::AppearanceMsg{CloseAppearance{}});

            // 挑选行请求其选择面板。只有一行是Pick，
            // 所以手-off不需要分派。
            if (applied.hand_off)
                return appearance_update(m, msg::AppearanceMsg{AppearancePickTheme{}});

            // 读回焦点行并在值稳定后写入：in-place改变
            // (←/→、下拉列表、切换)或带类型的编辑的结束。
            // 重新投影 mid-edit 在编号 带类型
            // 的 下重建表格。
            Cmd save = Cmd::none();
            if (applied.settled >= 0) {
                bool rebuild = false;
                rebuild = pull_field(m, o->pane.form.fields[
                    static_cast<std::size_t>(applied.settled)]);
                // 层/极性/语法改变已经-built元素的CONTENT，
                // 而不仅仅是其插槽解析为哪种颜色——量化、
                // 不同的解析主题或不同的跨度源码树。
                // 主题交换不需要重建（插槽在绘制时解析）；
                // 这些都可以。
                if (rebuild) rebuild_rendered_content(m);
                save = persist(m);
                reproject(m);
            }
            return save;
        },

        // ── 主题浏览器 ────────────────────────────────────────

        [&](AppearancePickTheme&) -> Cmd {
            auto* o = m.ui.panel.get<pn::Appearance>();
            if (!o) return Cmd::none();
            o->pane.picking      = true;
            o->pane.picker.picker.clear_query();
            // 打开 ON 正在使用的主题，因此列表显示的第
            // 一件事是您已经所在的位置 - 从那里指向箭头
            // 是比较而不是搜索。 jump_to 根据其自己
            // 的过滤计数夹在 选择面板 内部，而 可视区域
            // 源自同一个游标，因此我们打开的行是窗口包含的行。
            {
                const auto& pk  = o->pane.picker.picker;
                const auto& src = pk.entries();
                const auto& idx = pk.filtered();
                for (std::size_t i = 0; i < idx.size(); ++i) {
                    if (src[idx[i]] == m.d.ui().theme) {
                        pk.jump_to(static_cast<int>(i));
                        break;
                    }
                }
            }
            // 如果用户改变主意则恢复什么。隐藏在 面板
            // 上而不是消息中，因此 Esc 总是有
            // 东西可以返回，但浏览器已离开。
            o->pane.picker.restore = m.d.ui().theme;
            return Cmd::none();
        },

        [&](AppearanceThemeMove& e) -> Cmd {
            move_highlight(m, e.delta);
            return Cmd::none();
        },

        [&](AppearanceThemeQuery& e) -> Cmd {
            auto* o = m.ui.panel.get<pn::Appearance>();
            if (!o) return Cmd::none();
            if (e.text.empty()) o->pane.picker.picker.backspace();
            else                o->pane.picker.picker.type(e.text);
            // 选择面板在每个查询编辑上重置其自己的光标
            // ——一个地方，因此没有臂可以缩小列表并使
            // 光标指向现在意味着其他内容的行。
            //
            // Live-apply新的顶级比赛。键入会缩小 和 预览范围，
            // 因此 "dra" 无需第二次按键即可显示 Dracula。
            m.d.ui().theme = highlighted_theme(*o);
            restyle_sealed_turns(m);
            reproject(m);
            return Cmd::none();
        },

        [&](AppearanceThemeCommit&) -> Cmd {
            auto* o = m.ui.panel.get<pn::Appearance>();
            if (!o) return Cmd::none();
            // 主题已经应用（突出显示已应用它）；
            // Enter 只是结束浏览并使其持久。
            m.d.ui().theme = highlighted_theme(*o);
            o->pane.picking = false;
            // 全面重新设计，一次，就在出去。每次移动都已经
            // 浏览了 re-styled（该列表是它自己的预览），
            // 所以这就是解决方案：它将密封的成绩单放在您
            // 实际停止的任何方案下。
            restyle_sealed_turns(m);
            auto save = persist(m);
            reproject(m);
            return save;
        },

        [&](AppearanceThemeCancel&) -> Cmd {
            auto* o = m.ui.panel.get<pn::Appearance>();
            if (!o) return Cmd::none();
            // 真正的取消：放回我们打开的主题，
            // 包括在磁盘上——实时预览可能没有保留任何内容，
            // 但模型不能保留我们仅仅查看的最后一个主题。
            m.d.ui().theme = o->pane.picker.restore;
            o->pane.picking = false;
            // 退出时完全重新设计，与提交一样：冻结的
            // 前缀可能携带预览方案的颜色并且必须放回
            // 恢复的颜色之下。
            restyle_sealed_turns(m);
            auto save = persist(m);
            reproject(m);
            return save;
        },

    }, am);
}

}  // namespace agentty::app::detail
