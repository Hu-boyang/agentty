// stats.cpp 数据面板。 -driven : 无 -tab 树枝。
//
// 整个观点是:
//
//   用于当前标签中每个区域 TabDesc
//       extract()
//       其计量构建 一个 由区域Viz公司将卡片推入
//       cfg.prebuilt
//
// 添加标签来触摸此文件 不 AT 全部 . 这就是设计师的索赔,
// 并且这个文件要么是它持有的,要么是没有。
//
// 格式化 生活于 domain/stats/unit.hpp (一个)
// format() ),每个Viz地图为单独的 maya 部件( 唐纳、
// 火花线、 高盖、 BarChart 直方图, LineChart。
// 卡片会通过 maya::Panel's col_max_width 排版,
// 因此,这个文件拥有完全一样东西:哪个主题将域名槽地图颜色化到什么位置。

#include "panels_prologue.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <maya/widget/bar_chart.hpp>
#include <maya/element/grid.hpp>
#include <maya/widget/donut.hpp>
#include <maya/widget/gauge.hpp>
#include <maya/widget/histogram.hpp>
#include <maya/widget/line_chart.hpp>
#include <maya/widget/sparkline.hpp>
#include <maya/widget/tab_strip.hpp>

#include "agentty/domain/stats/tabs.hpp"
#include "agentty/runtime/panel/stats.hpp"

namespace agentty::ui {
namespace {

// 牌口音斜坡.
//
// 这些过去是九条硬码 RGB 值 - 主题无法看到一个私人调色板,
// 因此无论用户选择什么方案, STAS 面板都保留了自己的颜色。
// 保护它们的注释有半点: a STATUS 调色板(
// green=good, red=failed)对命名类别
// 网格带有错误的含义.
//
// 但答案是,我们... "status is the wrong axis"
// 绝对是斜坡,不是私人斜坡 maya::theme::series
// ()正是这个,它来自主题本身的角色槽,其顺序相交,
// 相邻的切片对比。同样的参数,一个层下,它跟随用户。
namespace hue {
[[nodiscard]] inline maya::Color at(std::size_t i) {
    return maya::theme::series(maya::theme::live(), i);
}
[[nodiscard]] inline maya::Color cyan()   { return at(0); }
[[nodiscard]] inline maya::Color dim()    {
    return maya::theme::series_other(maya::theme::live());
}
}  // namespace hue

// A CATEGORICAL 斜坡,对于其切片被命名
// 为事物而不是状态的图表——示例的调色板在它的循环顺序中,
// 所以相邻的切片对比而不是将光谱走进两个相邻的
// 蓝色.
[[nodiscard]] maya::Color series_hue(int slot, std::size_t i) {
    // 捕捉到的鱼 -all 切片画哑巴。 "other" 是
    // 那个 ABSENCE 一种分类,所以给它一个分类的颜色,
    // 它读起来又是一个—— 它做到了: 它与最大的切片相撞。
    //
    // 直通到这里 maya 而不是当地 `static const` 表格: 数据
    // 静态将建在第一个调用上,然后为这个过程冻结,
    // 而这正是私人坡道的缺陷。
    if (slot < 0) return maya::theme::series_other(maya::theme::live());
    return maya::theme::series(maya::theme::live(), i);
}

// ── card() 空标题区域,匹配 agent_stats 看着 ─────────
//
// 无边框:重音 -coloured 标题翻过内容
// IS 每张卡片的周围有一个盒子将一页数字转换成一个网格,
// 在两栏中,盒沿沟渠双向翻转。vstack +
// pading(0,1) 给呼吸室一个边框,
// 没有墨水。
//
// 每张卡都带有它的OWN口音,绕过坡道,
// 所以其中的网格读作不同的面板而不是同一颜色的一面墙
// ——这个东西使这个例子的仪表板可以一目了然.
[[nodiscard]] Element card(std::string_view heading, maya::Color tint,
                           std::vector<Element> body) {
    std::vector<Element> rows;
    rows.reserve(body.size() + 2);
    if (!heading.empty()) {
        rows.push_back(text(std::string{heading}, fg_bold(tint)));
        rows.push_back(blank());
    }
    for (auto& e : body) rows.push_back(std::move(e));
    return (dsl::v(std::move(rows)) | padding(0, 1)).build();
}

// 一张无话可说的小片仍画着标题,
// "measured, empty" 而不是说
// "forgot to render".
[[nodiscard]] Element empty_placeholder() {
    return text("\xe2\x80\x94", fg_dim(hue::dim()));   // - 破烂
}

// ── Viz 构建器 ──────────────────────────────────────────────────────────
//
// 每人拿取的度量衡,然后返回卡片 正文 (标题下的各项内容)
// PRESENTATION ,因此,当一个 Viz 被添加时,
// 这个文件会长出一个大小写,而从不长出一个标签。

// 条 — 每公尺一个水平条, 颜色为色循环。
// 缩放到最大值, 当公尺带有一个共同分母时,
// 缩放为 `of`( 整体的份额读成整体, 而不是相互对应 )。
[[nodiscard]] std::vector<Element> build_bars(const std::vector<stats::Metric>& ms) {
    std::vector<maya::Bar> bars;
    bars.reserve(ms.size());
    float peak = 0.0f;
    for (std::size_t i = 0; i < ms.size(); ++i) {
        const auto& mt = ms[i];
        const float v = static_cast<float>(mt.value);
        peak = std::max(peak, std::max(v, static_cast<float>(mt.of)));
        bars.push_back({.label = mt.label,
                        .value = v,
                        .color = series_hue(0, i)});
    }
    std::vector<Element> out;
    out.push_back(maya::BarChart{std::move(bars), peak}.build());
    return out;
}

// Spark——从系列中每公吨内含块火花,于 一个 线条:标签、
// 跟踪,然后是度量标准的格式值。 show_last 关闭,
// 因为它打印了最后的生源 SAMPLE ,这与度量标准的
// 格式值不同——显示两者都邀请读者进行调和 "280"
// 与 "702ms".
// Spark——一节的趋势。 与NO系列的衡量标准不是一个趋势:
// 恰巧是共用一节的平面数字("Turns using cache"
// 与"Hit rate"相邻),这些数字通过
// `figures`交还,以便呼叫者把它们放入READOUT栏,
// 在那里有空间——按下图表将它们压在图表高下,
// 并让读出栏半空。
[[nodiscard]] std::vector<Element> build_sparks(const std::vector<stats::Metric>& ms,
                                               std::vector<stats::Metric>* figures) {
    std::vector<Element> out;
    out.reserve(ms.size() * 2);
    // 有多少个衡量标准会在这里画出来——那些有一系列的。
    std::size_t drawn = 0;
    for (const auto& mt : ms) if (!mt.series.empty()) ++drawn;

    for (std::size_t i = 0; i < ms.size(); ++i) {
        const auto& mt = ms[i];

        if (mt.series.empty()) {
            if (figures) figures->push_back(mt);
            else {
                if (!out.empty()) out.push_back(blank());
                out.push_back(text(stats::format(mt.unit, mt.value),
                                   fg_bold(series_hue(0, i))));
                if (!mt.label.empty()) out.push_back(text(mt.label, fg_dim(hue::dim())));
            }
            continue;
        }

        std::vector<float> series;
        series.reserve(mt.series.size());
        for (double d : mt.series) series.push_back(static_cast<float>(d));
        // A RATE 转弯是曲线,不是条纹,火花是一条
        // 存储单元 每个样本,所以一个九 -turn 历史画
        // 了九个块——每个块的平稳~93%是全高,
        // 读起来是坚固的板块而不是趋势,大字纹图案填满了卡片,
        // 画了一条连接的线,并搭载了轴线,所以形状和关卡都能够
        // 辨别出来.
        if (mt.unit == stats::Unit::Ratio) {
            // 将 正文 共享到实际绘制的图表中。
            const int n = static_cast<int>(std::max<std::size_t>(1, drawn));
            const int h = std::clamp((panel_detail::panel_viewport_h() - 4) / n - 1,
                                     4, 14);
            maya::LineChart chart{std::move(series), h};
            chart.set_fill(true);
            chart.set_color(series_hue(0, i));
            if (!out.empty()) out.push_back(blank());
            out.push_back(chart.build());
            std::string cap = mt.label;
            if (mt.value != 0) cap += "  " + stats::format(mt.unit, mt.value);
            out.push_back(text(cap, fg_dim(hue::dim())));
            continue;
        }

        maya::SparklineConfig scfg{.color = series_hue(0, i), .show_last = false};
        maya::Sparkline spark{std::move(series), scfg};
        // 值在 LABEL 所以它坐落在追踪线上
        std::string label = mt.label;
        if (mt.value != 0 || mt.of != 0)
            label += "  " + stats::format(mt.unit, mt.value);
        spark.set_label(label);
        // 要求栏,一个部分报告自然宽度,灵活度就让它存在,
        // 所以9 -point 微量作为柱子的支点,
        // 宽度是它的三倍, 而它的旁边的直方图则充填了同样的空间。
        // grow() 将每个样本伸到整个区域
        // 存储单元 就会变得很脆弱
        out.push_back(spark.build() | dsl::grow(1.0f));
    }
    return out;
}

// 绘图——从它的系列中,每公尺一个盲文线图,
// 大小到面板的高度而不是固定的6行:一个图
// 通常是整张卡片,所以它不取的行是列中的死空.
[[nodiscard]] std::vector<Element> build_plots(const std::vector<stats::Metric>& ms) {
    std::vector<Element> out;
    out.reserve(ms.size() * 2);
    // 当一个区域携带数个时, 将 正文 共享到各块,
    // 并留有卡片的行驶、 空白和每个图片的字幕的空间。
    const int n = std::max<int>(1, static_cast<int>(ms.size()));
    const int avail = (panel_detail::panel_viewport_h() - 3) / n - 1;
    const int height = std::clamp(avail, 4, 20);
    for (std::size_t i = 0; i < ms.size(); ++i) {
        const auto& mt = ms[i];
        std::vector<float> series;
        series.reserve(mt.series.size());
        for (double d : mt.series) series.push_back(static_cast<float>(d));
        maya::LineChart chart{std::move(series), height};
        // 曲线下方的阴影:一个跨越高地图的孤零零的
        // 痕迹大多是空的,眼睛将空的读作数据.
        chart.set_fill(true);
        chart.set_color(series_hue(0, i));
        out.push_back(chart.build());
        // 标题带有 PEAK。图中最高的点是读者所达到的数字
        // ( "largest prefix" ),
        // 它生活在另一个栏的自己的牌上——一个数字在一个地方,
        // 另一个地方的形状产生它。在轴上,它是一个读数。
        if (!mt.label.empty()) {
            std::string cap = mt.label;
            double peak = 0;
            for (double d : mt.series) peak = std::max(peak, d);
            if (peak > 0) cap += "   peak " + stats::format(mt.unit, peak);
            out.push_back(text(cap, fg_dim(hue::dim())));
        }
    }
    return out;
}

// 乐队——是一个由1公尺零件组成的堆叠条;独立等价物是
// 这些零件中的BarChart,所以每个部分读作自己的标注条,
// 与整体相对.
[[nodiscard]] std::vector<Element> build_band(const std::vector<stats::Metric>& ms) {
    std::vector<maya::Bar> bars;
    float peak = 0.0f;
    for (const auto& mt : ms) {
        for (std::size_t i = 0; i < mt.parts.size(); ++i) {
            const auto& p = mt.parts[i];
            const float v = static_cast<float>(p.value);
            peak = std::max(peak, v);
            bars.push_back({.label = p.label,
                            .value = v,
                            .color = series_hue(p.hue, i)});
        }
    }
    std::vector<Element> out;
    if (bars.empty()) { out.push_back(empty_placeholder()); return out; }
    out.push_back(maya::BarChart{std::move(bars), peak}.build());
    return out;
}

// 甜甜圈(Donut) —— 整体的片段作为盲文环,每部分一个段.
// 公尺的细节坐落在中心(头条份额),其标签为标题.
// 每一公尺一个甜甜圈,因为一个甜甜饼 IS 是一个组成.
[[nodiscard]] std::vector<Element> build_donuts(const std::vector<stats::Metric>& ms) {
    std::vector<Element> out;
    for (const auto& mt : ms) {
        if (mt.parts.empty()) continue;
        maya::Donut d;
        for (std::size_t i = 0; i < mt.parts.size(); ++i) {
            const auto& p = mt.parts[i];
            d.segment(p.label, p.value, series_hue(p.hue, i));
        }
        if (!mt.detail.empty()) d.center(mt.detail);
        d.caption(mt.label);
        // 填充面板实际的高度,而不是固定的7行.
        // 电话 正文 谓 panel_viewport_h
        // ()高;脱下标题,卡片的走向和空白,一行松动,
        // 使戒指永远不把卡片推入卷轴中,一个甜甜圈是整张卡片,
        // 所以它不使用的高度就是列中的死空.
        const int avail = panel_detail::panel_viewport_h() - 4;
        d.rows(std::clamp(avail, 5, 16));
        out.push_back(d.build());
    }
    if (out.empty()) out.push_back(empty_placeholder());
    return out;
}

// hist / Dist——作为块柱的分布,每公尺一桶.
//
// 无标题: CARD 标题中已经指定了区域名称,
// 并重印了直方图 "First byte spread" 两次,
// 一张在另一行下。 牌拥有标题; 图表只是绘制。
[[nodiscard]] std::vector<Element> build_hist(
    std::string_view /*标题*/, const std::vector<stats::Metric>& ms) {
    maya::Histogram h;
    for (const auto& mt : ms) h.bucket(mt.label, mt.value);
    h.rows(6);
    std::vector<Element> out;
    out.push_back(h.build());
    return out;
}

// 一张卡的宽度可能自行确定。
//
// 小组措施a PREBUILT 正文 元素对一个无约束探测器(1
// ⁇ 14) 发现其高度——它只运行列流用于 `items`。
// 一张自控卡片 -sizing 图表( 4) BarChart
// 填充其槽,然后打印在最远的端值) 诚实的回答: 它的大小
// 一行到一万六千列,当卡片是时 PAINTED 真实的~36
// -column 正文 值是掉落到边缘的部分。
// "189 clipped writes" 黄金测试的捕获量。
//
// 所以卡片上写着自己的上限. terminal_cols
// () 宽度为: 帧 将真正被画在; Config::content_width
// ()脱去边框和铺垫,卷条沟也脱下,
// 因为 build() 放下那个 正文 和沟边并排。
// 牌的宽度 GRID ——真实的 正文 宽度,
// 所以 viewport() 将一个真实数字分为列,未绑定的测量
// 探测器无法充充电自定义 -sizing 图表通过它画的插槽。
[[nodiscard]] int body_width() {
    const int frame = maya::panel::detail::terminal_cols() > 0
                          ? maya::panel::detail::terminal_cols()
                          : 80;
    // 帧 + 贴贴 + 滚动条沟槽通过 content_width 和
    // kScrollbar Cols. 中 了 , 并 了。 card()
    // 然后各铺上 存储单元 单曲 存储单元 双方,以及 该 电网必须留有空间,
    // 没有电网,最后一栏的内容就流到沟渠下,正好是这两个。 存储单元.
    constexpr int kCardPadCols = 2;
    const int inner = maya::panel::Config::content_width(frame)
                      - maya::panel::Config::kScrollbarCols
                      - kCardPadCols;
    return std::max(16, inner);
}

// 一节 → 1张或 MANY 牌.
//
// A Kv/Hero 区域为 a LIST
// 独立数字的图案表是小卡的网格——每个图——这
// 就是给人们的 viewport() 够了 存储
// 单元 将整个区域的数字叠成一张高卡片,使网格宽两间,
// 屏幕半空,这正是这个例子的对照。所以,
// Scalar 类型 SPLIT :每个公尺成为自己的牌,
// 标有其标签并显示其价值。
//
// 图片类型( 4) Bars/Spark/Plot/Band/Donut/Hist 每人拿
// 一张牌——他们的衡量标准是: SERIES OF 一个 图表,而不是单独的数字。
//
// 斯卡尔人喜欢 不 每个牌,每个牌 SECTION
// 成为附件中的一组行 `text_groups` ;
// 然后调用者将这些组处理成几个文本列,所以一个标签是
// 图片加两个读出列而不是图表旁边的一个高的列.
void build_cards(const stats::Section& sec, const stats::Facts& f,
                 std::vector<stats::Metric>& scratch,
                 std::vector<std::vector<Element>>& text_groups,
                 std::vector<Element>& out,
                 std::vector<bool>& tall,
                 std::vector<std::string>& titles) {
    scratch.clear();
    sec.extract(f, scratch);

    const std::size_t slot = out.size();
    const maya::Color tint = series_hue(0, slot);

    // ── 斜拉线类型: 1 GROUP 线条, 稍后再分栏 ───
    if (sec.viz == stats::Viz::Kv || sec.viz == stats::Viz::Hero) {
        if (scratch.empty()) return;
        std::vector<Element> group;
        group.reserve(scratch.size() * 4 + 2);
        if (!sec.heading.empty()) {
            group.push_back(text(std::string{sec.heading}, fg_bold(tint)));
            group.push_back(blank());
        }
        for (std::size_t i = 0; i < scratch.size(); ++i) {
            const auto& mt = scratch[i];
            group.push_back(text(stats::format(mt.unit, mt.value), fg_bold(tint)));
            if (!mt.label.empty())  group.push_back(text(mt.label, fg_dim(hue::dim())));
            if (!mt.detail.empty()) group.push_back(text(mt.detail, fg_dim(hue::dim())));
            if (i + 1 < scratch.size()) group.push_back(blank());
        }
        text_groups.push_back(std::move(group));
        return;
    }

    if (scratch.empty()) {
        out.push_back(card(sec.heading, tint, {empty_placeholder()}));
        tall.push_back(false);
        titles.push_back(std::string{sec.heading});
        return;
    }

    // ── 图片类型: 整个区域一张牌 ──────────────────
    //
    // 火花部分也可以携带普通数字(无序列),
    // 这些数字会转到读出栏而不是图表下,
    // 它们在那里有空间,图表会保持高度。
    std::vector<stats::Metric> figures;

    // 闪光区域释放 一个 CARD PER TRACE ,而不是一张卡片,
    // 每张卡片都持有。然后,每张跟踪片与相同数量的分布成对:
    // 数据流 选项卡读作 "first byte, and its spread"
    // 边上 "output rate, and its spread"
    // 而不是在一栏中留下痕迹,在另一栏中则同时传播,这要求读者携带
    // 一个数字横跨面板,以满足自己的直方图.
    if (sec.viz == stats::Viz::Spark) {
        for (const auto& mt : scratch) {
            if (mt.series.empty()) { figures.push_back(mt); continue; }
            std::vector<stats::Metric> one{mt};
            auto one_body = build_sparks(one, nullptr);
            if (one_body.empty()) continue;
            out.push_back(card(mt.label, tint, std::move(one_body)));
            tall.push_back(false);
            titles.push_back(mt.label);
        }
        if (!figures.empty()) {
            std::vector<Element> group;
            group.reserve(figures.size() * 4);
            for (std::size_t i = 0; i < figures.size(); ++i) {
                const auto& mt = figures[i];
                group.push_back(text(stats::format(mt.unit, mt.value), fg_bold(tint)));
                if (!mt.label.empty())  group.push_back(text(mt.label, fg_dim(hue::dim())));
                if (!mt.detail.empty()) group.push_back(text(mt.detail, fg_dim(hue::dim())));
                if (i + 1 < figures.size()) group.push_back(blank());
            }
            text_groups.push_back(std::move(group));
        }
        return;
    }

    std::vector<Element> body;
    switch (sec.viz) {
        case stats::Viz::Bars:  body = build_bars(scratch);               break;
        case stats::Viz::Spark: body = {};                                break;
        case stats::Viz::Plot:  body = build_plots(scratch);              break;
        case stats::Viz::Band:  body = build_band(scratch);               break;
        case stats::Viz::Donut: body = build_donuts(scratch);             break;
        case stats::Viz::Hist:
        case stats::Viz::Dist:  body = build_hist(sec.heading, scratch);  break;
        case stats::Viz::Kv:
        case stats::Viz::Hero:  break;   // 以上处理
    }

    if (!figures.empty()) {
        std::vector<Element> group;
        group.reserve(figures.size() * 4);
        for (std::size_t i = 0; i < figures.size(); ++i) {
            const auto& mt = figures[i];
            group.push_back(text(stats::format(mt.unit, mt.value), fg_bold(tint)));
            if (!mt.label.empty())  group.push_back(text(mt.label, fg_dim(hue::dim())));
            if (!mt.detail.empty()) group.push_back(text(mt.detail, fg_dim(hue::dim())));
            if (i + 1 < figures.size()) group.push_back(blank());
        }
        text_groups.push_back(std::move(group));
    }

    // 每个度量衡都是个数字——没有了画,所以没有牌.
    if (body.empty() && !figures.empty()) return;

    if (body.empty()) body.push_back(empty_placeholder());
    out.push_back(card(sec.heading, tint, std::move(body)));
    tall.push_back(sec.viz == stats::Viz::Donut);
    titles.push_back(std::string{sec.heading});
}

}  // 命名空间结束。

Element stats_panel(const Model& m) {
    const auto* o = m.ui.panel.get<pn::Stats>();
    if (!o) return nothing();

    // 递增更新一次,在解决的 线程 这个 归并 无; 时 流式传输 来,
    // 来,来 归并 完全就是现场直播 末尾。
    // 见 domain/stats/facts.hpp 光标和时代守卫为何被塑造成这样
    const stats::Facts& f = o->projection.refresh(m.d.current);

    // 标签可以在用户下无法使用( 最后一个工具调用
    // 被重置 )。 登陆时会显示一个空面板,
    // 无法理解原因, 因此选择在此校正, 而不是在每
    // 个提取器中进行辩护。
    const auto visible = stats::visible_tabs(f);
    stats::Tab active = o->tab;
    if (!stats::tab_available(active, f)) active = visible.front();
    o->tab = active;

    maya::panel::Config cfg;
    cfg.title    = "Stats";
    cfg.subtitle = std::string{stats::tab_subtitle(active)};
    cfg.accent   = hue::cyan();

    // 标签是 WIDGET 铬 不是这个宿主 拥有
    // 垫子 选择的治疗方法 以及条状物在狭长的 帧 ,
    // 所以每个制表板 外观与构造相同。
    //
    // 编辑器标记,因为这些标签 PEERS 你转身,而不是 观点
    // 一个基本的东西 - " │ " 分隔符带有结构,没有下划线规则。
    // 填充是因为一个标记通过 CONTRAST 字条上的第一个
    // 标签没有写, a 标签 状态
    // "this is the live view" 独自一人
    //
    // 条条只显示 AVAILABLE 选项卡,所以从未运行
    // 过工具的会话没有工具 标签 以“在”为定位,
    // 空标签是承认它不应该被绘制的标签。
    cfg.tabs.reserve(visible.size());
    for (auto t : visible) cfg.tabs.emplace_back(stats::tab_title(t));
    cfg.tab_active = 0;
    for (std::size_t i = 0; i < visible.size(); ++i)
        if (visible[i] == active) { cfg.tab_active = static_cast<int>(i); break; }
    cfg.tab_mark = maya::TabMark::Editor;
    cfg.tab_fill = true;

    // 小组 正文 : 每一区一张牌,扇成响应列,由 maya::viewport
    // () ——布局相同 agent_stats 以实例为中心。
    // viewport() 从宽度中选择列数 CEILING (在以下
    // 时间段) max_width 是一个柱子,在它上,在它的两,
    // 两倍以上它三... ,填满左侧 -to-right 然后包装,
    // 并保持每张卡 响应它的 自身 栏而不是屏幕。
    //
    // 整个网格都像 一个 预建元素。面板只运行自己的列
    // 流 `items` ,它测量一个预建 正文 反射
    // 无界探测器(1 ⁇ 14),因此,电网被传送
    // 真实 宽度通过.width, 既是使柱子出对,
    // 也是阻止自我 -sizing 图表从缩小到16,000列,
    // 然后失去 末尾 真实的画 正文.
    std::vector<stats::Metric> scratch;
    scratch.reserve(32);

    const auto& sections = stats::tab_desc(active).sections;
    std::vector<std::vector<Element>> text_groups;   // 每个卡片区一个
    std::vector<Element> pictures;                   // 每张一张牌
    text_groups.reserve(sections.size());
    pictures.reserve(sections.size());
    std::vector<bool> tall_picture;
    std::vector<std::string> picture_title;
    for (const auto& sec : sections)
        build_cards(sec, f, scratch, text_groups, pictures, tall_picture, picture_title);

    // 上两个图表,共享 a 存储单元 - 但只有一个 FITS.
    //
    // 每一个图表都是自己的 存储单元 绘制图表 -heavy 选项卡,
    // 宽度太多的列,以及 a 存储单元 将陆地包裹在从下方开始的网格
    // 线上 正文 :读者从未见过的一节,一个火花和一个直方图可以
    // 舒适地分享一个专栏.
    //
    // 两位 DONUTS 不要,一打一打一对十四 -row 正文
    // ——将它们盲目对高,这是工具标签的成本 "calls by outcome"。
    // 一张图表将加入打开的 存储单元 只在运行高度离开空间时,
    // 否则它就会开始自己的高度。
    //
    // NOMINAL 高度,不是活的量度: 将包从
    // 终端 做一个 线程 将其图表在 渲染.
    if (pictures.size() > 2) {
        // A 存储单元 可能运行 TALLER 超过 正文
        // : 成对的跟踪 -and-spread 它们是值得
        // 一起看的,而后半段是卷轴的离场,它仍然比另一栏中要
        // 多一个数字,可以横跨面板。两个短图是合适的;
        // 一个甜甜圈仍然拒绝分享,因为其中两个是两倍。 正文.
        constexpr int kCellRows  = 16;
        constexpr int kDonutRows = 12;
        constexpr int kShortRows = 7;

        // 和那个 SPREAD OF 该 相同 QUANTITY 先说
        //
        // 按区段顺序包装,将两个痕迹都放在一个 存储单元 二者
        // 在接下来的一段时间里展开,所以读者们带着
        // "first byte 341ms" 横跨面板以达到
        // 自己的直方图。在主题词上匹配将每个故事保留在一个栏中:首先
        // 字节 及其扩散,然后输出率及其扩散。
        std::vector<std::size_t> order(pictures.size());
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::vector<bool> taken(pictures.size(), false);
        std::vector<std::size_t> seq;
        for (std::size_t i = 0; i < pictures.size(); ++i) {
            if (taken[i]) continue;
            taken[i] = true;
            seq.push_back(i);
            // 合伙人是下一个无名牌,其头衔与这位老板的首字母相同(第1行).
            // "First byte" -> "First byte spread").（从 First byte 切换到 First byte spread。）
            const std::string& mine = picture_title[i];
            const std::size_t cut = mine.find(' ');
            const std::string key = cut == std::string::npos ? mine : mine.substr(0, cut);
            if (key.empty()) continue;
            for (std::size_t k = i + 1; k < pictures.size(); ++k) {
                if (taken[k]) continue;
                if (picture_title[k].rfind(key, 0) == 0) { taken[k] = true; seq.push_back(k); break; }
            }
        }

        std::vector<Element> packed;
        std::vector<Element> cur;
        int cur_rows = 0;
        for (std::size_t idx : seq) {
            const int h = (idx < tall_picture.size() && tall_picture[idx])
                              ? kDonutRows : kShortRows;
            if (!cur.empty() && cur_rows + 1 + h > kCellRows) {
                packed.push_back(dsl::v(std::move(cur)).build());
                cur.clear();
                cur_rows = 0;
            }
            if (!cur.empty()) { cur.push_back(blank()); ++cur_rows; }
            cur_rows += h;
            cur.push_back(std::move(pictures[idx]));
        }
        if (!cur.empty()) packed.push_back(dsl::v(std::move(cur)).build());
        pictures = std::move(packed);
    }

    // 将平面图处理成 两个 栏,以 LINE COUNT ,
    // 所以一个标签是读出 +它的图片—— 存储单元 平分一个宽的面板,
    // 而不是一个高大的柱子,旁边有一个图表。
    //
    // 以行而不是按组进行平衡,
    // 因为只处理组类大小相似的组类: 原因标签
    // 是三行对十二行,所以无论哪个列取大一行
    // 是另一行的四倍,数字都从底部冲出.
    //
    // 割断的地上 BLANK ,这是唯一一个数字结束的地方。
    // 切换中点 -figure 孤负一个至亲的(至亲)。 "16%"
    // )在第二列的顶部,远离它有资格的值。
    std::vector<Element> text_a, text_b;
    if (!text_groups.empty()) {
        std::vector<Element> all;
        for (auto& g : text_groups) {
            if (!all.empty()) all.push_back(blank());
            for (auto& e : g) all.push_back(std::move(e));
        }

        const std::size_t half = all.size() / 2;
        std::size_t cut = all.size();
        for (std::size_t i = half; i < all.size(); ++i) {
            const auto* t = maya::as_text(all[i]);
            if (t && t->content.empty()) { cut = i; break; }
        }
        if (cut >= all.size()) {
            // 中间点或之后没有空白——反向看。
            for (std::size_t i = half; i-- > 0;) {
                const auto* t = maya::as_text(all[i]);
                if (t && t->content.empty()) { cut = i; break; }
            }
        }
        for (std::size_t i = 0; i < all.size(); ++i) {
            if (i == cut) continue;            // 放下我们剪的空白
            (i < cut ? text_a : text_b).push_back(std::move(all[i]));
        }
    }
    // 读取是它的 自身 双 -column 网格,在一个外层内筑巢 存储单元.
    //
    // 平滑图 + text_a + text_b
    // 变成三个兄弟 存储单元 使图表只显示一列三,
    // 因此随着读出量的增加而缩小。 两个 ——读出,
    // 以及旁边的照片——让读出再将自己的一半
    // 分成.
    //
    // 内网格是它的宽度为 HANDED。 它看不到外网的分割,
    // 因此对照无约束的探测器测量, 它要么倒塌到一列( 也就是
    // 它把“ 原因” 标签的数字保存在一个高的堆栈中) ,
    // 要么将它没有的宽度和剪辑的宽度分割出来。
    Element readout;
    bool has_readout = false;
    {
        std::vector<Element> text_cards;
        if (!text_a.empty()) text_cards.push_back(card({}, hue::cyan(), std::move(text_a)));
        if (!text_b.empty()) text_cards.push_back(card({}, hue::cyan(), std::move(text_b)));
        if (!text_cards.empty()) {
            const int n = static_cast<int>(text_cards.size());
            // 读出是 LEAD 存储单元 并持有两列文字,所以需要一半
            // 正文 ——图中将其余部分进行划分。将宽度平分于每个外侧
            // 存储单元 数字各20栏,并包裹标签 -phrase
            // (
            // "of generated / tokens were / reasoning").（已生成 token 中，有多少属于推理内容。）
            const int share = std::max(34, body_width() / 2);
            readout = maya::viewport(std::move(text_cards),
                                     maya::ViewportOpts{.max_width = std::max(16, share / n),
                                                        .max_cols  = n,
                                                        .min_width = 14,
                                                        .gap       = 2,
                                                        .gap_y     = 2,
                                                        .width     = share,
                                                        .flow      = maya::Flow::Row})
                          .build();
            has_readout = true;
        }
    }

    // 读取 LEADS 数字是必须首先读取的,
    // 图表说明了这些数字。
    std::vector<Element> cards;
    cards.reserve(pictures.size() + 1);
    if (has_readout) cards.push_back(std::move(readout));
    for (auto& p : pictures) cards.push_back(std::move(p));

    const int ncards = static_cast<int>(cards.size());
    cfg.prebuilt.push_back(
        maya::viewport(std::move(cards),
                       maya::ViewportOpts{// 标签通常是读出加
                                          // 一两张照片,
                                          // 所以让一列宽。
                                          .max_width = 56,
                                          // 永远不要要求比卡片多的列。
                                          // 没有卡片,
                                          // 宽度可以承受 三个
                                          // 标签有两张牌时的列,
                                          // 可视区域 认为这还不够充分,
                                          // max_width 并
                                          // 留下剩余部分, 右侧是死条。
                                          // 在卡片上被盖住的网格是满的,
                                          // 所以它把整个插槽和牌都
                                          // 完全分割开来。
                                          // FILL 就是这样
                                          .max_cols  = ncards,
                                          // 图表仍然需要其标签、
                                          // 条和值;在此下方,
                                          // 网格将保持一列,
                                          // 而不是将网格切换成两列。
                                          .min_width = 30,
                                          .gap       = 3,
                                          .gap_y     = 2,
                                          .width     = body_width(),
                                          .flow      = maya::Flow::Row})
            .build());

    // 无行选择:这是文档,不是选择面板。
    cfg.selected = -1;

    // 将卡片流到响应列。卡片需要~30列才能保持其
    // 标签和值整体;封顶为40,宽度非常大 终端
    // 不拉伸两条 -value 横跨半屏的瓦片,
    // 空白会让相邻的牌无法读为一体。
    // NOTE: col_max_width/col_min_width/col_gap
    // 已经 不 设置。面板只运行其列流用于 `items`; a `prebuilt`
    // 正文 是一个不透明的堆栈——这里,单 viewport() 上方的网格,它本身就执行列。

    // 标题没有例外。重音标题和列已分开各节;
    // 每栏各有一条规则将数字的一页
    // 变成一个表格。
    cfg.header_rule = false;

    cfg.scroll = &o->scroll;
    // 正文 高度: 每一个面板都使用同样的高度。
    // 与其兄弟姐妹匹配的面板值超过一个调值, 以适应它的内容;
    // 溢出卷轴, 卷轴条就是用来用的。
    cfg.viewport_h = panel_detail::panel_viewport_h();

    // 保持高度,而不是缩小 -wrapping。
    // 这些标签的长度不同,所以没有这个选项 帧 每次读者按下
    // 标签时都会调整大小。
    cfg.fixed_viewport = true;

    // 键通过共享 key_hints 帮手,
    // 不是帮手 -spelled 字符串输入 `note` :
    // 注释为下面的纯文本 正文 ,因此它不能在宽度压力下掉出键,
    // 其间隔从其他面板的脚上漂移. `tab` 只在有时出现 IS 换到
    // 另一个视图。
    std::vector<Hint> keys;
    if (visible.size() > 1) keys.push_back({"tab", "switch view", 3});
    keys.push_back({"\xe2\x86\x91\xe2\x86\x93", "scroll", 5});   // ↑↓
    keys.push_back({"esc", "close", 4});
    cfg.footer.push_back(key_hints(std::move(keys)));
    return maya::Panel{std::move(cfg)}.build();
}

}  // namespace agentty::ui
