// extract.cpp—事实→指标。每个部分一个功能，没有别的。
//
// 这里的每个函数都是投影：它读取计数器并写入行。
// 在该文件中没有发生计数（即fold.cpp)并且没有格式化
// （即unit.hpp)——这使得新的统计数据成为归并行
// 加上此处的一行，并且视图保持不变。

#include "agentty/domain/stats/tabs.hpp"

namespace agentty::stats {
namespace {

// 色调槽。 Metric::Part 上的 int 而不是
// maya::Color，因为 domain/ 不依赖于 渲染器 —
// 面板将它们映射到主题上。如此命名，以便提取器的读者可以了解该带的含义。
enum Hue { kNeutral = 0, kGood = 1, kWarn = 2, kBad = 3, kAccent = 4,
           // 分类图表的catch-all切片。它自己的插槽，
           // 因为 "other" 是某个类别的 ABSENCE
           // - 它不得使用使其看起来像其中一个的颜色绘制。
           kOther = -1 };

void ranked_rows(const Tally& t, std::vector<Metric>& out, Unit u = Unit::Count) {
    const double total = static_cast<double>(t.total());
    for (const auto& [label, n] : t.ranked())
        out.push_back(Metric{.label = label,
                             .unit  = u,
                             .value = static_cast<double>(n),
                             .of    = total,
                             .detail = format(Unit::Ratio,
                                              total > 0 ? n / total : 0.0)});
}

void kv(std::vector<Metric>& out, std::string label, Unit u, double v,
        std::string detail = {}) {
    out.push_back(Metric{.label = std::move(label), .unit = u, .value = v,
                         .of = 0, .detail = std::move(detail)});
}

// ── 会话 ─────────────────────────────────────────────────────────────

void session_hero(const Facts& f, std::vector<Metric>& out) {
    const auto turns = f.session.assistant_turns;
    // 英雄是SENTENCE，不是一行：先是数字，然后是说明其
    // 含义的子句。 "5 turns"本身就让读者寻找判决；
    // "— no errors" 是判决。
    // 标题现在是它自己的LINE，而不是数字后面的子句，
    // 因此它以单词而不是长破折号开头——以标点符号
    // 开头的行读起来就像丢失了前半部分的片段。
    std::string caption;
    if (f.session.errors)
        caption = std::to_string(f.session.errors)
                + (f.session.errors == 1 ? " ended in an error"
                                         : " ended in errors");
    else if (turns)
        caption = "none ended in an error";
    else
        caption = "nothing has run in this thread yet";
    out.push_back(Metric{
        .label = turns == 1 ? "1 turn" : std::to_string(turns) + " turns",
        .unit  = Unit::Count,
        .value = static_cast<double>(turns),
        .detail = std::move(caption)});
}

void session_counts(const Facts& f, std::vector<Metric>& out) {
    kv(out, "You asked",       Unit::Count, static_cast<double>(f.session.user_turns));
    kv(out, "Agent replied",   Unit::Count, static_cast<double>(f.session.assistant_turns));
    if (f.session.errors)
        kv(out, "Errors",      Unit::Count, static_cast<double>(f.session.errors));
    if (f.tools.total)
        kv(out, "Tool calls",  Unit::Count, static_cast<double>(f.tools.total));
    if (f.session.compact_summaries)
        kv(out, "Compactions", Unit::Count,
           static_cast<double>(f.session.compact_summaries));
}

// 提供者-facing时间去了哪里：等待第一个字节、
// 生成和工具执行。会话的总时间是面板上最不可行的数字
// - "1m05s" 没有告诉您任何可以修复的内容。
// SPLIT 确实如此：三个阶段具有三种不同的原因和三
// 种不同的补救措施。
//
// 测量为CONSECUTIVE机器时间，而不是wall
// -clock：每一轮贡献ttft+生成，每个固定工具贡献
// 其自己的执行窗口。并发工具的窗口重叠，因此三个切片的
// 总和可以是 MORE，而不是会话的 wall-clock
// ——这是关于机器工作去向的声明，标题是这么说的，
// 而不是暗示 wall-clock 账单。
void session_where_time_went(const Facts& f, std::vector<Metric>& out) {
    const double ttft  = static_cast<double>(f.stream.ttft.sum());
    const double gen   = static_cast<double>(f.stream.stream_ms.sum());
    const double tools = static_cast<double>(f.tools.latency.sum());
    const double all   = ttft + gen + tools;
    if (all <= 0) return;
    Metric m;
    m.label  = "where the time went";
    m.unit   = Unit::Millis;
    m.value  = all;
    m.detail = format(Unit::Millis, all);
    // 时间环相同：份额为百分比，持续时间如下。
    auto share = [&](double v) { return format(Unit::Ratio, v / all); };
    m.parts = {
        {"wait "  + share(ttft),  ttft,  kWarn},
        {"gen "   + share(gen),   gen,   kAccent},
        {"tools " + share(tools), tools, kGood},
    };
    out.push_back(std::move(m));
}

void session_time(const Facts& f, std::vector<Metric>& out) {
    if (!f.session.measured_turns) return;
    // 上面的环显示为份额的绝对持续时间。作为一个单独的部分保留，
    // 以便戒指保持可读性：同时带有 "48%" 和
    // "59s" 的图例是没有人读完的图例。
    if (f.stream.ttft.sum())
        kv(out, "Waiting",    Unit::Millis, static_cast<double>(f.stream.ttft.sum()));
    if (f.stream.stream_ms.sum())
        kv(out, "Generating", Unit::Millis, static_cast<double>(f.stream.stream_ms.sum()));
    if (f.tools.latency.sum())
        kv(out, "Tools",      Unit::Millis, static_cast<double>(f.tools.latency.sum()));
    // 每转：流式传输阶段(ttft+生成，
    // 相同的术语wall_ms已经求和)加上转过-tripped的工具时间。
    // 并发窗口重叠，因此这是机器时间，并且可以在并行
    // -heavy轮上超过wall-clock——这里更大的数字
    // 是更繁忙的会话，而不是关于用户等待多长时间的谎言。
    kv(out, "Average turn",     Unit::Millis,
       (static_cast<double>(f.session.wall_ms)
          + static_cast<double>(f.tools.latency.sum()))
         / static_cast<double>(f.session.measured_turns));
    if (f.session.measured_turns < f.session.assistant_turns)
        kv(out, "Measured turns", Unit::Count,
           static_cast<double>(f.session.measured_turns),
           "older turns predate telemetry");
}

// ── 模型 ──────────────────────────────────────────────────────────────

void models_by_model(const Facts& f, std::vector<Metric>& out) {
    ranked_rows(f.models.by_model, out);
}
void models_by_provider(const Facts& f, std::vector<Metric>& out) {
    // 只有当多个提供者实际提供服务时才值得一个部分
    // ——否则它会用更少的行重述上面的部分。
    if (f.models.by_provider_hint.size() < 2) return;
    ranked_rows(f.models.by_provider_hint, out);
}

// ── 智能模式 ──────────────────────────────────────────────────────────

void smart_hero(const Facts& f, std::vector<Metric>& out) {
    const double share = f.smart.routed
        ? static_cast<double>(f.smart.delegated) / static_cast<double>(f.smart.routed)
        : 0.0;
    // 分母是句子的一部分。 内容 的 "62%"
    // 是分享邀请的第一个问题，没有回答的英雄
    // 会将读者带到下表来重建它。
    out.push_back(Metric{
        .label = format(Unit::Ratio, share),
        .unit  = Unit::Ratio,
        .value = share,
        .detail = "of " + std::to_string(f.smart.routed)
                + " routed turns ran below the Strategic model"});
}

void smart_by_role(const Facts& f, std::vector<Metric>& out) {
    ranked_rows(f.smart.by_role, out);
    if (f.smart.unrouted)
        kv(out, "Smart Mode off", Unit::Count,
           static_cast<double>(f.smart.unrouted),
           "not counted in the share above");
}

void smart_by_model(const Facts& f, std::vector<Metric>& out) {
    ranked_rows(f.smart.by_model, out);
}

// ── 脚本 ──────────────────────────────────────────────────────────────

void tokens_totals(const Facts& f, std::vector<Metric>& out) {
    const double total = static_cast<double>(f.tokens.input + f.tokens.output);
    kv(out, "In",  Unit::Tokens, static_cast<double>(f.tokens.input));
    kv(out, "Out", Unit::Tokens, static_cast<double>(f.tokens.output));
    if (f.tokens.reasoning)
        kv(out, "…of which reasoning", Unit::Tokens,
           static_cast<double>(f.tokens.reasoning),
           format(Unit::Ratio,
                  f.tokens.output ? static_cast<double>(f.tokens.reasoning)
                                      / static_cast<double>(f.tokens.output)
                                  : 0.0));
    kv(out, "Total", Unit::Tokens, total);
    // Per-turn数字作为总计存在于SAME部分中：
    // 它们共享单位，因此它们共享条的比例，
    // 并且查看"largest turn"与"total"
    // 是表明一回合是否主导线程的比较。分成两个部分，
    // 每个部分都有自己的规模，这种关系消失了。
    const auto& h = f.tokens.per_turn_output;
    if (h.count()) {
        kv(out, "Average turn", Unit::Tokens, h.mean());
        kv(out, "Largest turn", Unit::Tokens, static_cast<double>(h.max()));
    }
}

void tokens_plot(const Facts& f, std::vector<Metric>& out) {
    if (f.tokens.output_series.size() < 2) return;
    out.push_back(Metric{.label  = "output tokens per turn",
                         .unit   = Unit::Tokens,
                         .series = f.tokens.output_series});
}

// ── Cache ───────────────────────────────────────────────────────────────

void cache_band(const Facts& f, std::vector<Metric>& out) {
    const double total = static_cast<double>(f.cache.hits + f.cache.writes
                                           + f.cache.misses);
    if (total <= 0) return;
    Metric m;
    m.label = "input tokens by origin";
    m.unit  = Unit::Tokens;
    m.value = total;
    // 命中率位于环的中心：它是标签的答案，
    // 并且看环的读者已经在看孔。
    m.detail = format(Unit::Ratio,
                      static_cast<double>(f.cache.hits) / total) + " hit";
    // 图例中的百分比，而不是原始计数。一个环已经将比例
    // 显示为 ANGLES——将它们重新表述为令牌计数，
    // 使读者可以进行图表刚刚为他们所做的除法。
    // 绝对数字位于下一节，可以在其中进行精确比较。
    auto pct = [&](std::uint64_t n) {
        return format(Unit::Ratio, static_cast<double>(n) / total);
    };
    m.parts = {
        {"hit "   + pct(f.cache.hits),   static_cast<double>(f.cache.hits),   kGood},
        {"write " + pct(f.cache.writes), static_cast<double>(f.cache.writes), kWarn},
        {"miss "  + pct(f.cache.misses), static_cast<double>(f.cache.misses), kBad},
    };
    out.push_back(std::move(m));
}

void cache_rates(const Facts& f, std::vector<Metric>& out) {
    const double prefix = static_cast<double>(f.cache.hits + f.cache.writes
                                            + f.cache.misses);
    if (prefix <= 0) return;
    // 命中率具有其自身的per-turn趋势。
    // 会话平均值隐藏了决定如何处理它的形状：缓存 和
    // WARMED（先低后高，健康情况）和每回合都丢失的
    // 一个可以报告相同的数字。
    out.push_back(Metric{
        .label  = "Hit rate",
        .unit   = Unit::Ratio,
        .value  = static_cast<double>(f.cache.hits) / prefix,
        .detail = "per turn \xe2\x86\x92",
        .series = f.cache.ratio_series});
    kv(out, "Turns using cache", Unit::Count,
       static_cast<double>(f.cache.turns_with_cache));
}

// 环的绝对计数显示为角度。它有自己的部分，因为戒指的图
// 例带有百分比——一把同时持有 "80%" 和 "93k"
// 的钥匙是没有人读完的——而且因为这三个共享一个单位，
// 所以它们在这里有一个共同的条形刻度，可以精确比较。
void cache_tokens(const Facts& f, std::vector<Metric>& out) {
    if (f.cache.hits + f.cache.writes + f.cache.misses == 0) return;
    // 缓存读取大约是每个提供它们的提供商的新
    // 输入价格的10%，因此第一行是长线程成本的
    // 最大杠杆。
    kv(out, "Served from cache", Unit::Tokens, static_cast<double>(f.cache.hits));
    kv(out, "Written to cache",  Unit::Tokens, static_cast<double>(f.cache.writes));
    kv(out, "Sent uncached",     Unit::Tokens, static_cast<double>(f.cache.misses));
}

// ── 工具 ───────────────────────────────────────────────────────────────

// 与 RING 相同。一个环回答 "what is this session MADE of"，
// 这在真正的 线程 上是重要的读数 - 458 shell
// 调用 对 40 个读数是一种工作方式，
// 并且它像两个楔子一样清晰，其中必须计算一列数字。
//
// 曾经位于其下方的排名条说了两次相同的事情：相同的计数、
// 相同的百分比、三倍的高度。该环带有顶部切片
// 及其在图例中的份额，而 末尾 则崩溃了——无论如何，
// 低于百分之一的楔形与其相邻的楔形都无法区分，
// 因此单独列出它并没有什么意义。
void tools_mix(const Facts& f, std::vector<Metric>& out) {
    const double total = static_cast<double>(f.tools.by_name.total());
    if (total <= 0) return;
    constexpr std::size_t kMaxSlices = 5;
    const auto ranked = f.tools.by_name.ranked();

    Metric m;
    m.label  = "calls by tool";
    m.unit   = Unit::Count;
    m.value  = total;
    m.detail = format(Unit::Count, total);
    // Tool名称，而不是说明：切片需要可区分的颜色，
    // 而不是good/bad读数。
    m.categorical = true;

    double shown = 0;
    for (std::size_t i = 0; i < ranked.size() && i < kMaxSlices; ++i) {
        const double v = static_cast<double>(ranked[i].second);
        shown += v;
        m.parts.push_back({ranked[i].first + " " + format(Unit::Ratio, v / total),
                           v, static_cast<int>(i)});
    }
    if (const double rest = total - shown; rest > 0)
        // k其他，不是下一个斜坡槽。 "other" 是
        // 一个类别的 ABSENCE，给它一个类别的颜色使
        // 它看起来像又一个工具 - 它明显做到了：它绘制了与
        // 最大切片相同的洋红色。
        m.parts.push_back({"other " + format(Unit::Ratio, rest / total),
                           rest, kOther});
    out.push_back(std::move(m));
}

void tools_status(const Facts& f, std::vector<Metric>& out) {
    const double total = static_cast<double>(f.tools.by_status.total());
    if (total <= 0) return;
    Metric m;
    m.label  = "calls by outcome";
    m.unit   = Unit::Count;
    m.value  = total;
    m.detail = format(Unit::Count, total);
    for (const auto& [label, n] : f.tools.by_status.rows()) {
        const int hue = label == "done"     ? kGood
                      : label == "failed"   ? kBad
                      : label == "rejected" ? kWarn
                                            : kNeutral;
        m.parts.push_back({label + " " + format(Unit::Ratio,
                                               static_cast<double>(n) / total),
                           static_cast<double>(n), hue});
    }
    out.push_back(std::move(m));
}

void tools_latency(const Facts& f, std::vector<Metric>& out) {
    const auto& h = f.tools.latency;
    if (!h.count()) return;
    kv(out, "Median",  Unit::Millis, h.quantile(0.5));
    kv(out, "p95",     Unit::Millis, h.quantile(0.95));
    kv(out, "Slowest", Unit::Millis, static_cast<double>(h.max()));
    kv(out, "Total",   Unit::Millis, static_cast<double>(h.sum()));
}

// 直方图的每个占用桶一行。桶的 RANGE 是标签，
// 它的计数是值 - 因此读者看到的是样本
// 实际落在的位置，而不是代表形状的两个分位数。
//
// 取单位是因为直方图是单位-agnostic:相同的log2桶
// 保存毫秒、字节或令牌，并且只有调用者知道是哪一个。
// Hard-coding Millis 以下是 字节 价差最终被标记的方式
// "16ms–32ms".
//
// `stride`合并相邻的子-buckets。
// Hist 存储 QUARTER-octaves，
// 以便同一八度音阶中的 p50 和 p95 报告不同的数字 -
// 但图表需要比分位数更粗糙的分箱：面板 中的 30 个分箱使
// 每个条形有 2 个 存储单元 宽，这是纹理而不是分布。
// 合并回整个八度音阶进行显示可以保持分位数精确且图片清晰，
// 这就是两者单独计算的全部原因。
void dist_rows(const Hist& h, std::vector<Metric>& out,
               Unit u = Unit::Millis, int stride = Hist::kSub) {
    if (!h.count()) return;
    const auto [lo, hi] = h.occupied();
    if (hi < lo) return;
    if (stride < 1) stride = 1;
    // 将第一仓与八度音阶边界对齐，使得小节的范围是整数——"64ms–128ms"，
    // 而不是"80ms–160ms"。
    const int first = (lo / stride) * stride;
    const double total = static_cast<double>(h.count());

    struct Bin { double n; int lo_b; };
    std::vector<Bin> bins;
    for (int b = first; b <= hi; b += stride) {
        double n = 0;
        for (int k = b; k < b + stride && k <= hi; ++k)
            n += h.buckets()[static_cast<std::size_t>(k)];
        bins.push_back({n, b});
    }
    double peak = 0;
    for (const auto& bn : bins) peak = std::max(peak, bn.n);

    for (const auto& bn : bins) {
        // 保留占据范围的空箱INSIDE：间隙是
        // 双峰分布的真实特征（快速本地调用，
        // 慢速网络）并且将其关闭正好隐藏了这一点。
        const auto floor_v = Hist::bucket_floor(bn.lo_b);
        const auto ceil_v  = Hist::bucket_floor(bn.lo_b + stride);
        out.push_back(Metric{
            .label  = format(u, floor_v) + "\xe2\x80\x93" + format(u, ceil_v),
            .unit   = Unit::Count,
            .value  = bn.n,
            .of     = peak,
            .detail = bn.n > 0 ? format(Unit::Ratio, bn.n / total) : ""});
    }
}

void tools_latency_dist(const Facts& f, std::vector<Metric>& out) {
    dist_rows(f.tools.latency, out);
}

void stream_ttft_dist(const Facts& f, std::vector<Metric>& out) {
    dist_rows(f.stream.ttft, out);
}

// 每转的帧 载荷有多大。 字节 和 TOKENS
// 回答不同的问题，并且只对其中一个进行计费 -
// 供应商 帧 一致表示价差很小，双峰则表示简短
// 的确认和长世代通过相同的路径到达，
// 这就是停滞的 -looking 数据流 通常的情况。
void stream_bytes_dist(const Facts& f, std::vector<Metric>& out) {
    dist_rows(f.stream.bytes, out, Unit::Bytes);
}

// ── 理由说明 ────────────────────────────────────────────────────────────

// 一审判决。 "Is extended thinking earning its keep"
// 是关于 SHARE 的问题：输出的哪一部分用于用户从未
// 读取的推理标记。低于 10% 的努力几乎没有吸引力；
// 超过 50% 的模型将大部分预算用于审议。
void reasoning_hero(const Facts& f, std::vector<Metric>& out) {
    if (!f.reasoning.turns) return;
    const double share = f.tokens.output
        ? static_cast<double>(f.reasoning.tokens)
            / static_cast<double>(f.tokens.output)
        : 0.0;
    out.push_back(Metric{
        .label  = format(Unit::Ratio, share),
        .unit   = Unit::Ratio,
        .value  = share,
        .detail = "of generated tokens were reasoning"});
}

void reasoning_rows(const Facts& f, std::vector<Metric>& out) {
    kv(out, "Turns that thought", Unit::Count,
       static_cast<double>(f.reasoning.turns),
       format(Unit::Ratio,
              f.session.assistant_turns
                ? static_cast<double>(f.reasoning.turns)
                    / static_cast<double>(f.session.assistant_turns)
                : 0.0));
    // 分裂说明了FOR的想法。深思熟虑然后
    // 调用工具就是推理要做什么；深思熟虑而唯一
    // 的回答是推理而不是行动。两者都没有错，
    // 但这个比率是努力购买决策和努力购买散文
    // 之间的差异。
    if (f.reasoning.turns) {
        kv(out, "…then acted", Unit::Count,
           static_cast<double>(f.reasoning.thought_then_acted),
           format(Unit::Ratio,
                  static_cast<double>(f.reasoning.thought_then_acted)
                    / static_cast<double>(f.reasoning.turns)));
        kv(out, "…then answered", Unit::Count,
           static_cast<double>(f.reasoning.turns
                             - f.reasoning.thought_then_acted));
    }
    if (f.reasoning.blocks) {
        kv(out, "Thinking blocks", Unit::Count,
           static_cast<double>(f.reasoning.blocks));
        // 它自己的ROW，上面没有注释。
        //
        // 注释存储单元采用该值之后剩下的任何内容，在分割列上是一些
        // 存储单元-"1 per turn"被截断为"1 per…"，
        // 甚至"1/turn"被截断为"1/tu…"。
        // 不能说自己单位的费率不值存储单元；作为一行，它像其他所有内容
        // 一样有一个标签、一个条形图和完整的值列。
        if (f.reasoning.turns)
            kv(out, "Blocks per turn", Unit::Count,
               static_cast<double>(f.reasoning.blocks)
                 / static_cast<double>(f.reasoning.turns));
    }
}

void reasoning_time(const Facts& f, std::vector<Metric>& out) {
    if (!f.reasoning.ms) return;
    kv(out, "Total",   Unit::Millis, static_cast<double>(f.reasoning.ms));
    kv(out, "Average", Unit::Millis, f.reasoning.per_turn_ms.mean());
    kv(out, "Median",  Unit::Millis, f.reasoning.per_turn_ms.quantile(0.5));
    kv(out, "Longest", Unit::Millis,
       static_cast<double>(f.reasoning.longest_ms));
}

void reasoning_tokens(const Facts& f, std::vector<Metric>& out) {
    if (!f.reasoning.tokens) return;
    kv(out, "Reasoning tokens", Unit::Tokens,
       static_cast<double>(f.reasoning.tokens));
    if (f.reasoning.per_turn_tokens.count()) {
        kv(out, "Average per turn", Unit::Tokens,
           f.reasoning.per_turn_tokens.mean());
        kv(out, "Deepest turn", Unit::Tokens,
           static_cast<double>(f.reasoning.per_turn_tokens.max()));
    }
}

// 努力应该是ADAPT：困难的转弯需要更长的时间，
// 简单的转弯几乎没有。这里的平线意味着它没有适应，
// 这是将用户发送到智能模式设置的一个发现。
void reasoning_trend(const Facts& f, std::vector<Metric>& out) {
    if (f.reasoning.ms_series.size() < 2) return;
    out.push_back(Metric{.label  = "thinking time per turn",
                         .unit   = Unit::Millis,
                         .series = f.reasoning.ms_series});
}

// 思考时间实际落地的地方。平均 2.4 秒可能是 2.4
// 秒的两圈，或者 200 毫秒的一圈和 4.6 秒的一圈，
// 只有第二个形状表示努力设置正在做任何事情。
void reasoning_dist(const Facts& f, std::vector<Metric>& out) {
    dist_rows(f.reasoning.per_turn_ms, out);
}

// ── Stream ──────────────────────────────────────────────────────────────

void stream_health(const Facts& f, std::vector<Metric>& out) {
    // 主导判决。零列表就是答案 "everything is fine"，
    // 其拼写方式可以让读者找到答案。
    //
    // "Troubled turns" 而不是
    // "Turns that hit trouble"：标签在其仪表旁边有一
    // 列的空间，而句子 -length 则将其全部用完然后截断。
    // 名词短语表达了同样的意思，并留下了图片的宽度。
    const bool clean = f.stream.degraded_turns == 0;
    kv(out, "Troubled turns", Unit::Count,
       static_cast<double>(f.stream.degraded_turns),
       clean ? "clean session" : "");
    if (f.stream.transient)
        kv(out, "Retries",        Unit::Count, static_cast<double>(f.stream.transient));
    if (f.stream.mid_stream)
        kv(out, "Mid-stream drops", Unit::Count, static_cast<double>(f.stream.mid_stream));
    if (f.stream.stalls)
        kv(out, "Stalls",         Unit::Count, static_cast<double>(f.stream.stalls));
}

// RATES，各有其各自的per-turn趋势。
// 速率是平均值具有误导性的一种数字：快速启动且性能
// 下降的会话中的 "1.0k/s" 是一个从未发生过的数字，
// 迷你图显示了这一点。
void stream_rates(const Facts& f, std::vector<Metric>& out) {
    if (!f.stream.ttft_series.empty()) {
        out.push_back(Metric{
            .label  = "First byte",
            .unit   = Unit::Millis,
            .value  = f.stream.ttft.mean(),
            .detail = "avg",
            .series = f.stream.ttft_series});
    }
    if (!f.stream.rate_series.empty()) {
        double sum = 0;
        for (double v : f.stream.rate_series) sum += v;
        out.push_back(Metric{
            .label  = "Output rate",
            .unit   = Unit::Rate,
            .value  = sum / static_cast<double>(f.stream.rate_series.size()),
            .detail = "tok/s",
            .series = f.stream.rate_series});
    }
}

void stream_timing(const Facts& f, std::vector<Metric>& out) {
    // TTFT和生成时间回答不同的问题——一个是
    // 提供方是否让用户等待？
    // 另一个是"was generation slow"——并且它们有不同的修复，
    // 因此它们永远不会被求和。不过，它们共享一个 UNIT，这使得面板可以在一个
    // 条上缩放它们，并使分割一目了然。
    if (f.stream.ttft.count()) {
        kv(out, "First byte",     Unit::Millis, f.stream.ttft.mean());
        kv(out, "First byte p95", Unit::Millis, f.stream.ttft.quantile(0.95));
    }
    if (f.stream.stream_ms.count()) {
        kv(out, "Generation",     Unit::Millis, f.stream.stream_ms.mean());
        kv(out, "Generation p95", Unit::Millis, f.stream.stream_ms.quantile(0.95));
    }
}

void stream_throughput(const Facts& f, std::vector<Metric>& out) {
    // 它自己的部分，因为它是RATE：在上面的计时条上，
    // 它将是相对于毫秒缩放的字节-per-second数字，
    // 这是没有任何意义的比较。
    const double secs = static_cast<double>(f.stream.stream_ms.sum()) / 1000.0;
    if (secs <= 0 || !f.stream.wire_bytes) return;
    kv(out, "Bytes delivered", Unit::Bytes,
       static_cast<double>(f.stream.wire_bytes));
    kv(out, "Throughput", Unit::Rate,
       static_cast<double>(f.stream.wire_bytes) / secs);
}

// ── 内容 ─────────────────────────────────────────────────────────────

void context_rows(const Facts& f, std::vector<Metric>& out) {
    // 不 "Largest prefix"。它是趋势图绘制的系列中的
    // PEAK，并且该图的标题说明了它 (
    // "prefix tokens per turn peak 17k") - 因此，
    // 作为一行，它是相同的数字两次，并且选项卡花费了一整列来在已经说明的图表
    // 旁边保留一个数字。
    if (f.context.compactions)
        kv(out, "Compactions", Unit::Count,
           static_cast<double>(f.context.compactions),
           "history summarised to fit");
}

void context_plot(const Facts& f, std::vector<Metric>& out) {
    if (f.context.prefix_series.size() < 2) return;
    out.push_back(Metric{.label  = "prefix tokens per turn",
                         .unit   = Unit::Tokens,
                         .series = f.context.prefix_series});
}

// ── 检索 ───────────────────────────────────────────────────────────

void retrieval_rows(const Facts& f, std::vector<Metric>& out) {
    kv(out, "Context injections", Unit::Count,
       static_cast<double>(f.retrieval.injections));
    if (f.retrieval.with_confidence) {
        const double avg = f.retrieval.confidence_sum
                         / static_cast<double>(f.retrieval.with_confidence);
        // 置信度是 KNOWN 整体 (1.0) 的份额，
        // 因此与部分 -scaled 条不同，该条是针对真实
        // 上限进行测量的 — "82% confident" 相
        // 对于完整仪表表示漏斗是否确定，其中该部分最大比率的 82
        // % 始终会读为满。
        out.push_back(Metric{.label = "Average confidence",
                             .unit  = Unit::Ratio,
                             .value = avg,
                             .of    = 1.0});
    }
}

}  // 命名空间结束。

namespace sections {

// DONUT LEADS，它无处不在。
//
// 环是标签上一眼就回答"what is this made of"
// 的一个图形，并且必须滚动经过三个表格才能到达它的读者已经
// 以艰难的方式阅读了表格。它也有一个固定的高度，
// 而它周围的表格随着 线程 的增长而增长，因此除了顶部之外的任何地方，
// 它都会在会话运行时向下移动到页面上。
//
// 每个具有以下选项卡的规则相同：会话、缓存、工具。
// HERO领先。这是选项卡的答案，读者一眼就应该
// 从顶部的-left 中看出——下图是工作内容，
// 而不是标题。它在结构上也必须是第一位的：面板将
// 领先的英雄排除在列流之外，而不是第一个英雄像
// 任何其他 存储单元 一样流动，并落在平衡放置的
// 任何地方，在此选项卡上是左列的 FOOT。
const std::array<Section, 4> session{{
    {"",          Viz::Hero,  &session_hero},
    {"",          Viz::Donut, &session_where_time_went},
    {"Activity",  Viz::Kv,    &session_counts},
    {"Time",      Viz::Kv,    &session_time},
}};

const std::array<Section, 2> models{{
    {"By model",    Viz::Bars, &models_by_model},
    {"By provider", Viz::Bars, &models_by_provider},
}};

const std::array<Section, 3> smart{{
    {"",         Viz::Hero, &smart_hero},
    {"By role",  Viz::Bars, &smart_by_role},
    {"By model", Viz::Bars, &smart_by_model},
}};

const std::array<Section, 2> tokens{{
    {"Totals", Viz::Kv,   &tokens_totals},
    {"Trend",  Viz::Plot, &tokens_plot},
}};

const std::array<Section, 3> cache{{
    {"",       Viz::Donut, &cache_band},
    {"Tokens", Viz::Kv,    &cache_tokens},
    {"Rates",  Viz::Spark, &cache_rates},
}};

// 两种工具故障都是RINGS，并且混合领先。
//
// 曾经坐在这里的排名酒吧说了两次与混合环相同的
// 事情：相同的计数、相同的百分比、三倍的高度。
// `calls by outcome`是两种或三种状态的组合，
// 这是环最能体现的——带子花费整个宽度来绘制4%的条子，
// 楔形物使该条子变得明显。
const std::array<Section, 4> tools{{
    {"",             Viz::Donut, &tools_mix},
    {"",             Viz::Donut, &tools_status},
    {"Latency",      Viz::Kv,    &tools_latency},
    {"Distribution", Viz::Hist,  &tools_latency_dist},
}};

const std::array<Section, 6> reasoning{{
    {"",             Viz::Hero,  &reasoning_hero},
    {"Turns",        Viz::Kv,    &reasoning_rows},
    {"Time",         Viz::Kv,    &reasoning_time},
    {"Tokens",       Viz::Kv,    &reasoning_tokens},
    {"Trend",        Viz::Plot,  &reasoning_trend},
    {"Spread",       Viz::Hist,  &reasoning_dist},
}};

const std::array<Section, 6> stream{{
    {"Health",            Viz::Kv,    &stream_health},
    {"Rates",             Viz::Spark, &stream_rates},
    {"Timing",            Viz::Kv,    &stream_timing},
    {"Throughput",        Viz::Kv,    &stream_throughput},
    {"First byte spread", Viz::Hist,  &stream_ttft_dist},
    {"Frame size spread", Viz::Hist,  &stream_bytes_dist},
}};

const std::array<Section, 2> context{{
    {"",      Viz::Kv,   &context_rows},
    {"Trend", Viz::Plot, &context_plot},
}};

const std::array<Section, 1> retrieval{{
    {"", Viz::Bars, &retrieval_rows},
}};

}  // namespace sections
}  // namespace agentty::stats
