#pragma once
// agentty::ui::panel_detail ——共享的构件 覆盖面板 选择面板.
//
// pickers.cpp 那时是2000年 -line 编译单元 持有
// 每 选择面板 视图 和 将视图分割为 .cpp 兄弟姐妹文件( )
// model_picker.cpp, nav_pickers.cpp ,
// )指那些帮手需要一个每个兄弟可以包括的家——这个 头文件。
// 这里的一切 `inline` (头文件-only )纯度,
// 且无单数 -picker 状态,所以 选择面板 文件获得一个一致的列表形状
// ( $ 可视区域 尺寸,徽章宽度),一个一致的语义调色板(层次色调),共享推理脚,
// 以及一个统一的部分 -divider 头文件 包括这个

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <maya/dsl.hpp>
#include <maya/widget/panel.hpp>
#include <maya/platform/io.hpp>

#include "agentty/runtime/model.hpp"
#include "agentty/domain/ui_live.hpp"   // panel_rows( ): 密度驱动面板高度
#include "agentty/domain/catalog.hpp"   // resolved_caps 努力, caps_provider_scope
#include "agentty/runtime/view/helpers.hpp"
#include "agentty/runtime/view/palette.hpp"
#include "agentty/provider/registry.hpp"    // wire_streams_reasoning_text
#include "agentty/provider/selection.hpp"

namespace agentty::ui::panel_detail {

using namespace maya;
using namespace maya::dsl;

// ── 路径格式化( 线程 列表 / 提及调色板) ───────────────────────

// "src/runtime/foo.cpp" → ("foo.cpp", "src/runtime/").
// 回返( "foo.cpp", "" 用于一个赤裸的文件名。
[[nodiscard]] inline std::pair<std::string_view, std::string_view>
split_name_dir(std::string_view path) {
    auto slash = path.find_last_of('/');
    if (slash == std::string_view::npos) return {path, {}};
    return {path.substr(slash + 1), path.substr(0, slash + 1)};
}

// 压缩目录路径到它的 IMMEDIATE 父路径 – 也就是用户实际
// 扫描的离散器。 截断段本身则留给 maya (`| clip`) ,
// 因此这仅仅是执行语义步骤( "/home/.../Best Of Kumar Sanu/"
// – "Kumar Sanu/" )。
[[nodiscard]] inline std::string parent_segment(std::string_view dir) {
    if (dir.empty()) return {};
    auto inner = dir;
    if (inner.back() == '/') inner.remove_suffix(1);
    auto slash = inner.find_last_of('/');
    auto last = (slash == std::string_view::npos)
        ? inner : inner.substr(slash + 1);
    std::string out{last};
    out.push_back('/');
    return out;
}

// ── 面板宽度下限(由每层 覆盖面板 说不尽心 -sized) ──
//
// 3个命名大小,而不是每个编号 面板 宽度已漂移到45/50/52/54/60/64,
// 跨12个 覆盖面板 分歧背后没有任何故事——
// 这是权责分明,而不是决定。
//
// 准确的说出这能买到什么,因为它比看起来要少。 FLOORS ,
// 和 maya::Panel 处理 min_width 仍
// 伸展到集装箱上的盒子: 终端 宽于地板 面板 渲染 宽度相同,
// 无论大小, NARROWER 电话员还是会把它扣下来的(
// 见 form_view.cpp ,该盖 min_width
// 时间 max_width 6 个比屏幕宽的地板无法将标签
// 柱从两边推开。所以旧的数字大多是惰性的,今天将它们统一
// 起来不会改变像素。
//
// 它能买到的: 新的 面板 选择一个 NAME 说它的内容是什么,
// 而不是发明一个数字,没有人可以后来证明它是合理的,然后,
// 就把地板放在狭长的、狭长的。 终端 在那里它确实束缚。

// 短文一栏——计划项目,模式名称。足够读取句子
// 片段而不在电话上包装 -sized 终端.
inline constexpr int kPanelNarrow = 48;

// 默认:标签栏加value/meta栏. 线程,提供商,检查站,
// 命令调色板——任何形状的"name … detail". Matches
// maya::Panel::Config的自默认,所以一个说什么也得不到的面板.
inline constexpr int kPanelStandard = 60;

// 三列,或一列载道. 符号 代码 -block 选择
// 面板 显示文件: 与主题并列, 设置列表中包含
// 一个值加出处标记。
inline constexpr int kPanelWide = 68;

// ── 可视区域 大小( 由每个 选择面板 滚动列表) ────────────

// 可视区域 高度( rows) , 每张 选择面板 列表可滚动。
// 单个常数。 选择面板 共享相同的形状; 超出其范围的项目可以通过滚动栏到达,
// 选择总是通过部件的自动显示 -scroll-to-selection
// 逻辑
inline constexpr int kViewportH = 14;

// 选择面板 可滚动列表周围的铬:顶边框+标题行+空白+两个 -row
// 页脚( 宽+提示行) + 底边框, 加 AppLayout
// 外挂式 ~7行 , 上面有 选择面板 浮在底部 -pinned
// 在一个扎克 越过基座; maya 扩展 帧 至高者,分两层。
// 如果 选择面板's TOTAL 高度( 列表 + 铬) 超过
// 终端 可视区域 ,打开它滚动 基地的顶端行成为本土 回滚缓冲区
// 无法收回的,所以每个 open/close 将欢迎字
// 标记的复制件串起。将列表粘贴到 整个 选择面板 适合避免溢出。
inline constexpr int kPickerChromeRows = 7;

// 电话 终端 视图构建时的大小 : view() 安装一个 RenderContext
// 从 Model 大小( 4) TerminalResized 在任何
// 面板建造之前,这些面板读作:永远不要显示tty。在视图(测试直接叫面板
// 建造者)之外,没有上下文: COLUMNS/LINES ,这就是这些测试
// 如何说 他们的宽度 渲染 at. at (英语).
[[nodiscard]] inline int panel_terminal_rows() {
    if (maya::have_render_context()) return maya::available_height();
    if (const char* lines_env = std::getenv("LINES"))   // launch-env: 仅在视图外部读取，例如测试直接调用面板构造函数
        if (const int n = std::atoi(lines_env); n > 0) return n;
    return 40;
}

[[nodiscard]] inline int panel_viewport_h() {
    const int term_rows = panel_terminal_rows();
    // 留一个小呼吸空间,所以 选择面板 顶端边框位于
    // 极低的 可视区域 上面是基地的后面,还有...
    const int avail = term_rows - kPickerChromeRows - 1;
    // 上限是用户的 DENSITY ,而不是编译 -in kviewportH: 页面存档备份,存于互联网档案馆, 存于互联网络的存檔, 存檔,存档日期2011-09-02. :
    // 紧凑10/正常14/室22. 它保持一个上限——一个20个
    // 房间的室间设置. -row 终端 仍然产生什么 终端 已经,
    // 所以预览 永远不能推一个面板关闭 -screen。
    // 四行的底数将保留 选择面板 可以用一个小术语(它卷轴).
    return std::clamp(avail, 4, agentty::ui_prefs::panel_rows());
}

// 终端 WIDTH,解决方式与panel_terminal_rows()解决高度相同.
[[nodiscard]] inline int panel_terminal_cols() {
    if (maya::have_render_context()) return maya::available_width();
    if (const char* c = std::getenv("COLUMNS"))   // launch-env: 仅在视图外部读取，例如测试直接调用面板构造函数
        if (const int n = std::atoi(c); n > 0) return n;
    return 80;
}

// 提供商徽章可能占用多少列。徽章是平面十字内的分组信号。
// -provider 列表,所以它必须保持清晰的可读性
// ——但它与模型竞争 NAME 用户正在读取。
//
// 帽子曾经是固定的 8/12/16 栏次 终端 宽度,
// 打破了整个布局, 此时自定义主机将标签从16列之后
// 增长( e.g. `ollama.com [seaventures]`
// 24: 部件 渲染 以自然宽度表示徽章,
// 这样一行比其他行宽,模型列则被划上。 终端 宽度相反,
// 让柱子长成符合最长的实际标签,但绝不让柱子吃
// 超过一半的 选择面板 水平空间,所以模型名称列
// 仍然有 可用空间在窄处 终端.
[[nodiscard]] inline int panel_badge_max_cols() {
    const int cols = panel_terminal_cols();
    // 至少保留模型宽度的一半 NAME。在非常狭小的 终端 (
    // ⁇ 四十科)徽章盖底 出八(旧. "abbreviate hard"
    // 楼层); 宽度 终端 追踪到一半 终端 ,
    // 所以一个漫长的习惯 -host 标签从来不是出现乱划
    // 的对齐的原因——对齐只有在病理宽的主机(== 一半)时才会中断。
    // 终端 ) 完全没有给模型名称留下任何空间.
    if (cols <= 40) return 8;            // 微小: 缩写为硬
    const int cap = cols / 2;
    return cap < 8 ? 8 : cap;            // 从小到小 -terminal 楼层
}

// ── 能力 -tier 车型( 型号) 选择面板 徽章)  ⁇ . ─────────────────────────────

// Provider-badge 被模型的能力所锁定 TIER。
// 浏览列表命令最强 -first 颜色使隐形命令清晰可见。
// 花蕾来自共享调色板的语义坡道,强度 -ordered 因此,
// 它读作渐变(亮重音 → 蓝色 → 青色 → 灰色)。 BADGE ,
// 从名称,也从唯一的信号(命令+QQ标记在无颜色的情况下说同样
// 的话)—— WCAG 1.4.1).
[[nodiscard]] inline maya::Color tier_hue(ModelCapabilities::Tier t) {
    using T = ModelCapabilities::Tier;
    switch (t) {
        case T::Flagship: return role_brand_alt;  // 亮色红衣——顶端车道
        case T::Mid:      return role_info;        // 蓝色-工作马道
        case T::Cheap:    return code_path;        // 亮色青色——fast/small
        case T::Weak:     return muted;            // 灰色 - 工具-use 不可靠
    }
    return muted;
}


// 一节是一组选择面板列表的一个标签带. `hue` 标注了标签; `count`
// (当 > 0) 渲染 dim + right-pinned 这样一个乐队就
// 可以携带一个静态的stat,而不与标签竞争.
struct SectionHeader {
    std::string label;                 // 已执行 UPPERCASED
    maya::Color hue   = fg;
    int         count = 0;             // 0=未显示数
};

// 构建一个 `is_header` 从一行起 SectionHeader.
//
// 标签通过时为 -is : 面板大写 a 头文件 并将此
// 规则引向正确的边缘。在这里这样做也是两个业主两次采用的同样的转变,
// 因为上层没有危险。 -casing 谓 幂等.
[[nodiscard]] inline Panel::Item section_header(SectionHeader h) {
    Panel::Item hdr;
    hdr.control       = maya::panel::Header{};
    hdr.leading       = std::move(h.label);
    hdr.leading_style = fg_of(h.hue);
    if (h.count > 0) {
        hdr.trailing       = std::to_string(h.count);
        hdr.trailing_style = fg_italic(muted);
    }
    return hdr;
}

// ── 理由说明 -effort 页脚( 模型) 选择面板) ────────────────────────────────

// 推理 -effort 控制脚印,每个模型表面共享,所以它们 渲染 IDENTICALLY
// 财务报告和已审计财务报表 read/write 编号 相同 状态( 4) : m.d.effort ,
// 每笔 -model reasoning_override, m.d.show_reasoning).
// `model_id` 显示的行模式; `scope` 其提供方( 空 = 活动
// 提供方) 因此, 引信行的梯度 反映 该 主机合同。返回行以附加到 a 选择面板 '
// pinter( 可能是空的)。 请输入 一个 位置,使表面永远不能分化(
// "off in one, on in the other" 错误).
[[nodiscard]] inline std::vector<Element>
reasoning_effort_footer(const Model& m, std::string_view model_id,
                        std::string_view scope = {}) {
    std::vector<Element> out;
    if (model_id.empty()) return out;

    const std::string hi_id{model_id};
    const auto caps = resolved_caps(hi_id, scope);

    // 电话 ^R show/hide 切换 曾经住在这里, 在线上。
    //
    // 因为它是 相同 USER-FACING QUESTION
    // 外观 "Thinking" 排,问在两个地方
    // 可能会有异议: ^R 说 "shown" 外观上说
    // "Hidden" ,结果就是请求,支付,然后投在地上
    // ——每张都自信地报告相反的两张屏幕.
    //
    // 努力 STAYS. "How hard should this model think"
    // 就是你选的模型的地产, 这正是这个 选择面板 是为我
    // "Do I want to look at it" 是你的财产 你
    // 喜欢你 UI ,这就是外表 面板 只有一个问题,一个家

    if (effort_capable(caps)) {
        // 一行:工作阶梯(目前方括号内为“ ⁇ ”和“ ⁇ ”),
        // 然后是全球的“^R show/hide 切换”。
        std::vector<Element> parts;
        parts.push_back(text("reasoning ", fg_dim(muted)));
        // 这个模型会给用户带来什么
        const Effort shown = clamp_effort(m.d.effort, caps);
        for (Effort lvl : available_efforts(caps)) {
            const std::string lbl{effort_label(lvl)};
            if (lvl == shown) {
                parts.push_back(text("\xe2\x80\xb9", fg_of(accent)));      // ‹
                parts.push_back(text(lbl, fg_bold(accent)));
                parts.push_back(text("\xe2\x80\xba ", fg_of(accent)));   // ›
            } else {
                parts.push_back(text(lbl + " ", fg_dim(muted)));
            }
        }
        out.push_back(h(std::move(parts)).build());
    } else {
        // 无法控制这个模型, 也无法说出这里
        // 的推理 — — 展示生命在外观中。
        // 空脚的行只是铬, 声称没有特征。
    }
    return out;
}

}  // namespace agentty::ui::panel_detail
