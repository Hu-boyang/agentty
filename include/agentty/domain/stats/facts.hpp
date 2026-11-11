#pragma once
// agentty::stats::Facts-每一项统计数据，折叠在一次增量传递中。
//
// 成本模型，其设计为：
//
//   一个令牌流式传输 0 — 根本没有统计代码运行 帧，
//   面板关闭 0 — 事实从未构建 帧,
//   线程 已解决 0 归并 帧,
//   流式传输 1 归并 (实时 末尾) 面板
//   在 800 转时打开 1 O(n)
//   整数行走增加了分叉/倒回/压缩一O(n) 重建
//
// ── 为什么是递增的,为什么是 末尾 特殊 ─────────────────────────
//
// 消息[0，尺寸-1)是SEALED：仅折叠一次，永远。
// 最后一条消息是 始终 易失性的——它是被流入的消息——
// 因此它被折叠成一个临时总数，每次刷新时都会重新计算并在读取时添加。
// 这就是使面板在没有 re-walking 历史记录
// 的 数据流 期间存活的原因，并且它取代了投影中的错误：
// {message_count，thread_id}标记看不到末尾变异，
// 因此其下方的令牌计数器将冻结mid-turn。
//
// ── 为什么要在三个行动上全面重建 ───────────────────────────────
//
// 分叉、倒带和压缩MUTATE历史而不是追加到它。
// 增量协调是产生错误和可信的数字的优化。这三个都是罕见的，
// 并且是 user-initiated，
// 因此下面的 Epoch 通过值检测它们并重置光标。

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "agentty/domain/conversation.hpp"
#include "agentty/domain/smart_mode.hpp"

namespace agentty::stats {

// ── 塔利语Name ────────────────────────────────────────────────────────────────
//
// 小型有序计数器。有意进行线性扫描：会话中不同的模型 < 20，
// 不同的工具 < 40，并且在该大小下，扫描在时间和 分配
// 上都优于哈希图。它还保持 INSERTION 顺序稳定，
// 因此当 数据流 运行时，行不会将 帧 重新洗牌为
// 帧 — 这读取为 渲染 错误而不是数据。
class Tally {
public:
    void add(std::string_view key, std::uint64_t n = 1) {
        for (auto& [k, v] : rows_)
            if (k == key) { v += n; return; }
        rows_.emplace_back(std::string{key}, n);
    }
    [[nodiscard]] std::uint64_t total() const noexcept {
        std::uint64_t t = 0;
        for (const auto& [k, v] : rows_) t += v;
        return t;
    }
    [[nodiscard]] std::size_t size()  const noexcept { return rows_.size(); }
    [[nodiscard]] bool        empty() const noexcept { return rows_.empty(); }
    [[nodiscard]] const std::vector<std::pair<std::string, std::uint64_t>>&
    rows() const noexcept { return rows_; }

    // 最大的在前；关系保持插入顺序 (stable_sort)，
    // 因此相等的计数不会在 帧 之间交换位置。
    [[nodiscard]] std::vector<std::pair<std::string, std::uint64_t>>
    ranked() const {
        auto out = rows_;
        std::stable_sort(out.begin(), out.end(),
                         [](const auto& a, const auto& b) { return a.second > b.second; });
        return out;
    }
    void merge(const Tally& other) {
        for (const auto& [k, v] : other.rows_) add(k, v);
    }
    void clear() { rows_.clear(); }

private:
    std::vector<std::pair<std::string, std::uint64_t>> rows_;
};

// ── 赫特语Name ─────────────────────────────────────────────────────────────────
//
// log2延迟直方图：24个桶覆盖96字节中的1ms…~4h。
// p50/p95不存储每个调用的持续时间——该版本在
// 长时间会话中无限制地增长，对于没有人读取到三位
// 有效数字的数字。
class Hist {
public:
    // 每二的幂四个子-buckets。普通 log2 在每一步都会加倍，
    // 因此位于同一八度音程中任何位置的 p50 和 p95 都会报告
    // 相同 数字 - 在真实的 线程 上测量："Median 4.1s, p95 4.1s"，
    // 这读取为错误并不会告诉用户任何信息。将每个八度音程
    // 四等分可以将两者真正不同的地方分开，但代价是多了
    // 96 个 字节。
    static constexpr int kSub     = 4;
    static constexpr int kOctaves = 24;
    static constexpr int kBuckets = kOctaves * kSub;

    void add(std::uint32_t ms) {
        ++count_;
        sum_ += ms;
        if (ms > max_) max_ = ms;
        ++bucket_[static_cast<std::size_t>(index_of(ms))];
    }
    [[nodiscard]] std::uint64_t count() const noexcept { return count_; }
    [[nodiscard]] std::uint64_t sum()   const noexcept { return sum_; }
    [[nodiscard]] std::uint32_t max()   const noexcept { return max_; }
    [[nodiscard]] double mean() const noexcept {
        return count_ ? static_cast<double>(sum_) / static_cast<double>(count_) : 0.0;
    }
    // 分位数落在桶的上边缘。
    // 报告为桶的上限而不是插值：直方图
    // 确实不知道样本位于桶内的哪个位置，
    // 并且发明点估计会声称它不具有精度。
    [[nodiscard]] double quantile(double q) const noexcept {
        if (!count_) return 0.0;
        const auto want = static_cast<std::uint64_t>(
            static_cast<double>(count_) * q);
        std::uint64_t seen = 0;
        for (int b = 0; b < kBuckets; ++b) {
            seen += bucket_[static_cast<std::size_t>(b)];
            // 严格大于，而不是>=。在边界处 - 95 个快速
            // 样本和 5 个慢速样本，要求 p95 -
            // `>=` 返回 FAST 存储桶，报告最慢的 5
            // % 是快速的。 末尾 正是 p95 所要求的，
            // 因此隐藏它的规则会使数字比不显示更糟糕。
            if (seen > want && bucket_[static_cast<std::size_t>(b)]) {
                // 桶的上边缘，实际上是OBSERVED。
                // 报告边缘是诚实的——直方图不知道样本位于
                // 桶内的哪个位置——但是超过最大值的边缘会
                // 打印出 p95 LARGER，
                // 而不是最慢的 调用，这是一个不可能真实的数字，
                // 并且破坏了对它旁边的每个数字的信任。
                const double edge = static_cast<double>(bucket_floor(b + 1));
                const double top  = static_cast<double>(max_);
                return edge < top ? edge : top;
            }
        }
        return static_cast<double>(max_);
    }
    void merge(const Hist& o) {
        count_ += o.count_;
        sum_   += o.sum_;
        if (o.max_ > max_) max_ = o.max_;
        for (int b = 0; b < kBuckets; ++b)
            bucket_[static_cast<std::size_t>(b)] += o.bucket_[static_cast<std::size_t>(b)];
    }
    void clear() { *this = Hist{}; }

    // 桶本身。 p50/p95 是两个读数 OF 一个分布，
    // 而分布不是两个数字：95 个快速 调用 和 5 个
    // 慢速数字与 100 个中等数字具有相同的平均值，
    // 并且意味着完全不同的东西。形状已经被存储 - 不显示
    // 它就会丢弃面板所拥有的最丰富的信息。
    [[nodiscard]] const std::array<std::uint32_t, kBuckets>& buckets()
        const noexcept { return bucket_; }

    // 包括桶b的下边缘，在add()ed的单元中。
    //
    // 桶是八度音阶的四分之一：桶b存在于八度音程b/kSub中，
    // 并且其下边缘是该八度音阶的基部加上八度音阶宽度的
    // 四分之一的b%kSub。
    [[nodiscard]] static std::uint32_t bucket_floor(int b) noexcept {
        if (b <= 0) return 0;
        const int oct = b / kSub;
        const int sub = b % kSub;
        const std::uint64_t base = 1ull << oct;
        return static_cast<std::uint32_t>(base + base * sub / kSub);
    }

    // 样本落在哪个桶中。bucket_floor()的逆，
    // 并保持在其旁边，以便两者不会漂移。
    [[nodiscard]] static int index_of(std::uint32_t v) noexcept {
        if (v <= 1) return 0;
        int oct = 0;
        for (std::uint32_t t = v; t > 1 && oct < kOctaves - 1; t >>= 1) ++oct;
        const std::uint64_t base = 1ull << oct;
        int sub = static_cast<int>((v - base) * kSub / base);
        if (sub < 0) sub = 0;
        if (sub >= kSub) sub = kSub - 1;
        const int b = oct * kSub + sub;
        return b >= kBuckets ? kBuckets - 1 : b;
    }

    // 占用范围，因此渲染器可以跳过空头和末尾，
    // 而不是抽取24个桶，其中20个桶为零。
    [[nodiscard]] std::pair<int, int> occupied() const noexcept {
        int lo = kBuckets, hi = -1;
        for (int b = 0; b < kBuckets; ++b)
            if (bucket_[static_cast<std::size_t>(b)]) {
                if (b < lo) lo = b;
                hi = b;
            }
        return {lo, hi};
    }

private:
    std::array<std::uint32_t, kBuckets> bucket_{};
    std::uint64_t count_ = 0;
    std::uint64_t sum_   = 0;
    std::uint32_t max_   = 0;
};

// ── 事实 ────────────────────────────────────────────────────────────────
//
// 每个选项卡一个结构，因此"where does this number live"
// 具有不需要读取归并的答案。
struct Facts {
    struct SessionF {
        std::size_t user_turns      = 0;
        std::size_t assistant_turns = 0;
        std::size_t errors          = 0;
        std::size_t compact_summaries = 0;
        // 完全携带遥测数据的转弯。以下每个平均值的分母 -
        // 平均超过 全部 圈将默默地将 -telemetry
        // 之前的历史记录为零 -cost。
        std::size_t measured_turns  = 0;
        std::uint64_t wall_ms       = 0;   // 总结ttft+数据流
    } session;

    struct ModelsF {
        Tally by_model;                    // 每位助理轮流
        Tally by_provider_hint;            // 第一个'-'之前的前缀/'/'
    } models;

    struct SmartF {
        std::size_t routed   = 0;          // served_role 存在
        std::size_t unrouted = 0;          // 智能模式已关闭
        Tally by_role;
        Tally by_model;                    // 仅路由转弯
        // 运行BELOW战略路线的转弯。它自己的计数器
        // 而不是派生的，因为分母是 `routed` 并且
        // 在视图上派生它是两个消费者最终不一致的原因。
        std::size_t delegated = 0;
    } smart;

    struct TokensF {
        std::uint64_t input     = 0;
        std::uint64_t output    = 0;
        std::uint64_t reasoning = 0;       // 内部输出，从未添加到其中
        std::uint64_t cache_read     = 0;
        std::uint64_t cache_creation = 0;
        Hist          per_turn_output;
        // 最后 N 圈输出，用于趋势图。因此 2000
        // -turn 线程 无法无限制地增长投影。
        std::vector<double> output_series;
    } tokens;

    struct CacheF {
        std::size_t   turns_with_cache = 0;
        std::uint64_t hits   = 0;          // cache_read代币
        std::uint64_t writes = 0;          // cache_creation代币
        std::uint64_t misses = 0;          // 未缓存的输入令牌
        // 每-turn命中率。标题利率是会话平均值，
        // 隐藏了重要的形状：缓存 和 WARMED（
        // 先低后高）以及保持相同平均值的缺失。
        std::vector<double> ratio_series;
    } cache;

    struct ToolsF {
        Tally by_name;
        Tally by_status;                   // 完成/失败/拒绝/待处理
        Hist  latency;
        std::size_t total = 0;
    } tools;

    struct ReasoningF {
        std::size_t   turns  = 0;          // 彻底改变这个想法
        std::uint64_t ms     = 0;
        std::uint64_t tokens = 0;
        std::size_t   blocks = 0;
        // LONG每个思考轮是如何思考的，花费了多少代币。
        // 平均 2.4 秒的两圈可能都考虑了 2.4 秒，
        // 或者一圈考虑了 200 毫秒，一圈考虑了 4.6 秒—
        // —只有第二个形状告诉你努力设置正在做任何事情。
        Hist          per_turn_ms;
        Hist          per_turn_tokens;
        // 转变这个想法，然后生产了 NO 工具 调用——纯粹是经过深思熟虑的。
        // 反对思想和行动的转变，这就是"reasoning about what to do"
        // 和"reasoning instead of doing"
        // 之间的分歧。
        std::size_t   thought_then_acted = 0;
        std::size_t   longest_ms = 0;
        // 每-turn的思考时间，对于趋势。在困难转弯时努力程度会增加，
        // 在简单转弯时努力程度会减少；扁平线意味着它
        // 根本没有适应。
        std::vector<double> ms_series;
    } reasoning;

    struct StreamF {
        std::size_t   degraded_turns  = 0; // 幸存下来的转弯
        std::uint64_t transient       = 0;
        std::uint64_t mid_stream      = 0;
        std::uint64_t stalls          = 0;
        std::uint64_t wire_bytes      = 0;
        Hist          ttft;
        Hist          stream_ms;
        // 每回合 帧 载荷。 字节 和 TOKENS 回答不同的问题，
        // 并且仅对其中一个进行计费：在 200 帧 中流式传输
        // 40 KB 的回合和在 4000 中流式传输 40 KB
        // 的回合具有相同的令牌计数和非常不同的 传输层 行为。
        Hist          bytes;
        // Per-turn系列，面向内联趋势。
        // 平均答案 "how fast on average"；迷你图回答
        // "is it getting worse"，这是有人打开此选项卡时提出的问题。
        std::vector<double> ttft_series;
        std::vector<double> rate_series;    // 每秒输出令牌数
    } stream;

    struct ContextF {
        std::size_t compactions = 0;
        std::uint64_t peak_input = 0;      // 看到的最大前缀
        std::vector<double> prefix_series;
    } context;

    struct RetrievalF {
        std::size_t injections   = 0;
        std::size_t with_confidence = 0;
        double      confidence_sum = 0.0;
    } retrieval;

    // 保持趋势系列的界限。一块地块的宽度约为 70 存储单元，
    // 因此无法抽取比这更多的样本 - 携带它们不会花费
    // 内存 到 渲染。
    static constexpr std::size_t kSeriesCap = 256;

    void fold(const Message& msg);
    void merge(const Facts& tail);
    void clear() { *this = Facts{}; }
};

// ── 爱波奇 ────────────────────────────────────────────────────────────────
//
// 三个-field后卫。按价值比较；任何不匹配的内容都会
// 将 归并 光标重置为 0。这里没有尝试将 补丁 替换为
// 突变的前缀。
struct Epoch {
    std::string thread_id;
    std::size_t message_count = 0;
    std::size_t compactions   = 0;

    [[nodiscard]] static Epoch of(const Thread& t) {
        return Epoch{t.id.value, t.messages.size(), t.compactions.size()};
    }
    // 当 `next` 只能由 APPENDING 从 `*this` 到达时为真。
    [[nodiscard]] bool extends_to(const Epoch& next) const noexcept {
        return thread_id == next.thread_id
            && compactions == next.compactions
            && next.message_count >= message_count;
    }
};

// 增量投影。拥有当前 末尾 的光标、
// 纪元和暂存总数。
class Projection {
public:
    // 在已结算的 线程 上按 帧 摊销 O(1)； O(1 条消息)，
    // 同时 流式传输；分叉/倒带/压缩时的O(n)。
    const Facts& refresh(const Thread& t);

    [[nodiscard]] const Facts& facts() const noexcept { return view_; }

private:
    Facts sealed_;      // 消息 [0, 消耗_)
    Facts view_;        // seal_ + 直播 末尾，来电者读到的内容
    Epoch epoch_;
    std::size_t consumed_ = 0;
};

}  // namespace agentty::stats
