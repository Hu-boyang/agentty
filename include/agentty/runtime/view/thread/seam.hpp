#pragma once
// agentty::ui —每个 inter-turn 接口边界 元素的 一个 定义。
//
// 冻结构建器(frozen.cpp/freeze_range)和
// live-tail构建器(conversation.cpp/build_live_tail)
// 必须为相同输入产生字节-identical行序列：在冻结时刻，
// 活动行简单地re-labelled为冻结，
// 并且任何1-row形状增量将已经提交给本机终端 回滚缓冲区的行移动
// 对抗 re-layout——经典的 freeze-seam 幽灵。
//
// 历史上，每个构建器都有这些元素的 自身 副本以及
// "MUST stay byte-identical to the other file" 注释。
// 这是一个约定，而不是保证——一次漂移的编辑=一个 回滚缓冲区 幽灵。
// 此 头文件 用单个定义站点替换了约定：两个构建器 调用 具有相同的功能，
// 因此 接口边界 不会出现分歧。
// （INLINE_SCROLLBACK.md 引脚 #3——分频器对称性——
// 现在由 链接器 强制执行，而不是通过审查。）

#include <cstddef>
#include <string>
#include <vector>

#include <maya/dsl.hpp>
#include <maya/element/element.hpp>
#include <maya/element/text.hpp>   // maya::string_width
#include <maya/render/cache_id.hpp>
#include <maya/widget/conversation.hpp>
#include <maya/widget/turn.hpp>

#include "agentty/domain/conversation.hpp"
#include "agentty/domain/ui_live.hpp"   // compact_turns:接口边界的高度
#include "agentty/foreign/names.hpp"     // import_seam_row的"from Codex"
#include "agentty/runtime/view/palette.hpp"

namespace agentty::ui {

// Inter-turn 接口边界 在每对相邻匝之间 — 一个空白行，
// 暗淡 — 规则，然后是另一个空白行。在每个固定回合之前、
// live-tail 回合之间以及冻结↔活动边界处推送。
//
// COMPACT 删除两个空白行并保留规则，因此转弯仍然分开，
// 但花费一行而不是三行。在 30-row 终端 上，
// 长会话将屏幕的十分之一花在空白上；这是一种真正的交易，
// 而不是一种装饰性的交易，这就是为什么它是一种背景。
//
// ── 高度和元素 必须 同意,我们不做任何决定 ────────────────────────────
// Frozen 回滚缓冲区用显式行计数密封每个元素(
// push_frozen(m，gap_row()，kGapRows))；
// 如果两者意见不一致，账本就会漂移，画布就会撕裂。因此，
// 计数是元素读取的相同首选项的 FUNCTION — gap_rows()，
// 而不是必须记住的常量 — 并且两者在同一时刻进行评估。
inline maya::Element gap_row() {
    using namespace maya::dsl;
    if (ui_prefs::current().compact_turns)
        return maya::Conversation::divider();
    return v(blank(),
             maya::Conversation::divider(),
             blank()).build();
}
[[nodiscard]] inline int gap_rows() noexcept {
    return ui_prefs::current().compact_turns ? 1 : 3;
}
// 保留为 调用 站点的 NON-compact 高度，专门考虑到​​宽敞的布局。
// 任何密封 gap_row() 的东西都必须使用 gap_rows
// () 来代替——见上文。
inline constexpr int kGapRows = 3;

// Compaction-boundary分隔符：单
//   个-row居中标记规则┈┈┈
// ┈┈ ≡ 压缩对话┈┈┈┈┈在压缩边界上开始的运行之前发出。
// 渲染为真正的水平分隔符（不是带有悬挂左导轨的扬声器
// Turn，读起来为破碎的空消息），
// 因此边界看起来像分节符。在视觉上与 inter
// -turn `─` 规则不同：标签侧面有较浅的
// QUAD-DASH 规则 (┈)，`≡` 字形带有品牌口音，
// 标签本身的色调比规则更明亮，因此 事件 一目了然，
// 而不是溶解到镀铬中。
//
// MUST准确地停留在ONE行：freeze_range (
// frozen.cpp)和build_live_tail (conversation.cpp)
// 都在边界处发出该信号，并且冻结接口边界处的任何高度增量都会导致回滚缓冲区
// 行（INLINE_SCROLLBACK.md引脚#3）。单个 -row
// h() 样式段通过构造保持 接口边界 高度 -stable - 并且两个
// 构造器 调用 此 都起作用，因此 字节 也不会漂移。
//
// 标签文字"Conversation compacted"是加载-bearing:接口边界
// 测试(midrun_seam_test) grep以使其对分隔符副本进行计数。
inline maya::Element compaction_divider_row() {
    using namespace maya::dsl;
    return maya::detail::component([](int w, int /*h*/) -> maya::Element {
        if (w <= 0) return blank().build();
        const std::string glyph = "\xe2\x89\xa1";              // ≡ (1列)
        const std::string label = "Conversation compacted";    // ASCII
        // 存储单元:字形(1)+空格(1)+标签+每侧两个空格
        const int label_cells = 2 + static_cast<int>(label.size());
        const int rule_total  = w - label_cells - 4;
        if (rule_total < 4) {
            // 侧翼太窄——左-aligned紧凑形式。
            return h(
                text("   "),
                text(glyph + " ", maya::Style{}.with_fg(accent)),
                text(label, maya::Style{}.with_fg(text_secondary).with_dim())
            ).build();
        }
        const int left  = rule_total / 2;
        const int right = rule_total - left;
        std::string lrule, rrule;
        lrule.reserve(static_cast<std::size_t>(left) * 3);
        rrule.reserve(static_cast<std::size_t>(right) * 3);
        for (int i = 0; i < left; ++i)  lrule += "\xe2\x94\x88";   // ┈
        for (int i = 0; i < right; ++i) rrule += "\xe2\x94\x88";
        return h(
            text(std::move(lrule),
                 maya::Style{}.with_fg(muted).with_dim()),
            text("  "),
            text(glyph, maya::Style{}.with_fg(accent)),
            text(" "),
            text(label, maya::Style{}.with_fg(text_secondary).with_dim()),
            text("  "),
            text(std::move(rrule),
                 maya::Style{}.with_fg(muted).with_dim())
        ).build();
    })
    .hash_id(maya::CacheIdBuilder{}
        .add(std::string_view{"agentty.compaction.divider.v2"})
        .build());
}

// 真当且仅当从消息索引`idx`开始的运行在压缩边界
// 上打开（并且因此必须在分隔符之前）。共享后，
// 冻结和实时构建者就 WHEN 分隔线的出现达成一致，
// 而不仅仅是它的外观。
inline bool compaction_boundary_at(
        const std::vector<Thread::CompactionRecord>& recs,
        std::size_t idx, std::size_t total) {
    for (const auto& rec : recs) {
        if (rec.up_to_index == idx && rec.up_to_index > 0
            && rec.up_to_index <= total) return true;
    }
    return false;
}

// 当从另一个工具导入的线程结束并且agentty接管时：
// 从 Codex 转入 agentty 后继续。
// 一排，与压实分隔器相同的规则（两个构建器都是 调用，所以 字节 不能漂移）。
inline maya::Element import_seam_row(std::string source_name) {
    using namespace maya::dsl;
    return maya::detail::component([source_name](int w, int /*h*/) -> maya::Element {
        if (w <= 0) return blank().build();
        const std::string glyph = "\xe2\x86\x92";   // →
        const std::string label = "continued in agentty";
        const std::string from  = "from " + source_name;
        const int label_cells = 2 + static_cast<int>(label.size()) + 3
                              + static_cast<int>(maya::string_width(from));
        const int rule_total  = w - label_cells - 4;
        if (rule_total < 4) {
            return h(text("   "),
                     text(glyph + " ", maya::Style{}.with_fg(accent)),
                     text(label, maya::Style{}.with_fg(text_secondary).with_dim())).build();
        }
        const int left  = rule_total / 2;
        const int right = rule_total - left;
        std::string lrule, rrule;
        for (int i = 0; i < left; ++i)  lrule += "\xe2\x94\x88";   // ┈
        for (int i = 0; i < right; ++i) rrule += "\xe2\x94\x88";
        return h(
            text(std::move(lrule), maya::Style{}.with_fg(muted).with_dim()),
            text("  "),
            text(glyph, maya::Style{}.with_fg(accent)),
            text(" "),
            text(label, maya::Style{}.with_fg(text_secondary).with_dim()),
            text(" \xc2\xb7 ", maya::Style{}.with_fg(muted).with_dim()),
            text(from, maya::Style{}.with_fg(muted).with_dim()),
            text("  "),
            text(std::move(rrule), maya::Style{}.with_fg(muted).with_dim())
        ).build();
    })
    .hash_id(maya::CacheIdBuilder{}
        .add(std::string_view{"agentty.import.seam.v1"})
        .add(std::string_view{source_name})
        .build());
}

// 导入接口边界位于之前的消息索引，或者无SIZE_MAX：
// 导入后添加的第一条消息，一旦有的话。
inline std::size_t import_seam_index(const Thread& t) noexcept {
    if (!t.imported || t.imported->messages == 0) return SIZE_MAX;
    const std::size_t at = t.imported->messages;
    return at < t.messages.size() ? at : SIZE_MAX;
}

} // namespace agentty::ui
