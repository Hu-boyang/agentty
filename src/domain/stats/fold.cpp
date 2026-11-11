// fold.cpp — 一条消息 → 事实。 ONLY处的任何东西都算在内。
//
// 面板中的每个统计数据都是fold()中的一行。这就是设计所依据的 DRY
// 声明：添加 "how many tokens did subagents burn"
// 是事实上的一个字段和此处的一行，而不是带有自己的 缓存 和
// 自己对转弯是什么的想法的记录的第二步。

#include "agentty/domain/stats/facts.hpp"

#include <chrono>

namespace agentty::stats {
namespace {

// 第一个'-'或'/'之前的所有内容——"claude-sonnet-4-6"
// →"claude"，"openai/gpt-5"→"openai"。
// 提示，而不是权威：传输 id 是回合携带的唯一出处，在这里询问
// 注册表 会将 归并 耦合到重新加载的 线程 可能没有的实时模型状态。
[[nodiscard]] std::string_view provider_hint(std::string_view id) {
    const auto cut = id.find_first_of("-/");
    return cut == std::string_view::npos ? id : id.substr(0, cut);
}

void push_capped(std::vector<double>& v, double x) {
    if (v.size() >= Facts::kSeriesCap) v.erase(v.begin());
    v.push_back(x);
}

}  // 命名空间结束。

void Facts::fold(const Message& msg) {
    if (msg.role == Role::User) {
        ++session.user_turns;
        if (msg.proactive) {
            ++retrieval.injections;
            if (msg.proactive->confidence) {
                ++retrieval.with_confidence;
                retrieval.confidence_sum += *msg.proactive->confidence;
            }
        }
        return;
    }
    if (msg.role != Role::Assistant) return;

    // 智能模式自己的路由消息是簿记的，而不是轮流的。
    // 计算它们会夸大面板上的每个分母。
    if (msg.smart_routing) return;

    ++session.assistant_turns;
    if (msg.error)            ++session.errors;
    if (msg.is_compact_summary) ++session.compact_summaries;

    // ── 模型 ──────────────────────────────────────────────────────────
    if (!msg.served_model.empty()) {
        models.by_model.add(msg.served_model.value);
        models.by_provider_hint.add(provider_hint(msg.served_model.value));
    }

    // ── 智能模式 ──────────────────────────────────────────────────────
    //
    // 桶分裂是加载-bearing。没有 served_role
    // 的转弯使用智能模式 OFF 运行；
    // 将其算作战略会夸大旗舰产品的份额，而放弃它会低估分母。
    // `delegated`仅超过ROUTED轮，
    // 这使得它成为对特征的判断而不是对会话的描述。
    if (msg.served_role) {
        ++smart.routed;
        smart.by_role.add(smart::role_display_name(*msg.served_role));
        if (!msg.served_model.empty()) smart.by_model.add(msg.served_model.value);
        if (*msg.served_role != smart::ModelRole::Strategic) ++smart.delegated;
    } else {
        ++smart.unrouted;
    }

    // ── 理由说明 ─────────────────────────────────────────────
    if (msg.reasoning_ms > 0 || !msg.thinking_blocks.empty()
        || !msg.thinking.empty()) {
        ++reasoning.turns;
        if (msg.reasoning_ms > 0) {
            const auto ms = static_cast<std::uint64_t>(msg.reasoning_ms);
            reasoning.ms += ms;
            reasoning.per_turn_ms.add(static_cast<std::uint32_t>(
                ms > 0xFFFFFFFFull ? 0xFFFFFFFFull : ms));
            push_capped(reasoning.ms_series, static_cast<double>(ms));
            if (ms > reasoning.longest_ms)
                reasoning.longest_ms = static_cast<std::size_t>(ms);
        }
        reasoning.blocks += msg.thinking_blocks.size();
        // 这个想法是否导致了ACTION？经过深思熟虑
        // 然后调用工具的回合利用其推理来决定要做什么；
        // 一个深思熟虑并只回答推理而不是采取行动的人。
        // 两者都没有错，但比率表明努力是购买
        // 决策还是购买散文。
        if (!msg.tool_calls.empty()) ++reasoning.thought_then_acted;
    }

    // ── 工具 ───────────────────────────────────────────────────────────
    for (const auto& tc : msg.tool_calls) {
        ++tools.total;
        tools.by_name.add(tc.name.value);
        // 状态名称是用户的词汇，而不是变体的：
        // 这些逐字地显示为行标签。
        const char* st = std::visit([](const auto& s) -> const char* {
            using T = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<T, ToolUse::Done>)     return "done";
            else if constexpr (std::is_same_v<T, ToolUse::Failed>)   return "failed";
            else if constexpr (std::is_same_v<T, ToolUse::Rejected>) return "rejected";
            else return "pending";
        }, tc.status);
        tools.by_status.add(st);
        // 持续时间=EXECUTION窗口(分派→终端)，而不是卡的存在。
        // started_at 被标记为 StreamToolUseStart，
        // 并故意覆盖 arg-streaming、权限 等待和 分派 排队（问题
        // #40）；折叠 该 会将 "how long the human took to approve"
        // 和 "how long args streamed" 记入工具时间，
        // 停在 权限 提示词 的会话将在 "where the time went"
        // 的工具切片中显示小时数。
        //
        // Done/Failed携带从运行时复制的executing_since。
        // exec_window_at()读取它WITHOUT回落到started_at：
        // 从未发送的工具（已拒绝、抢救-as-"not run"、
        // args-parse失败、#40 线程之前）没有窗口，
        // 并且对其card-birth标记进行计费将re-create正是该拆分旨在删除的2h20m谎言。
        // 零表示 "no window"，是一个陈述，
        // 而不是一个样本。
        const auto began    = tc.exec_window_at();
        const auto finished = tc.finished_at();
        if (began && finished.time_since_epoch().count() != 0
            && finished > *began) {
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                finished - *began).count();
            tools.latency.add(static_cast<std::uint32_t>(ms < 0 ? 0 : ms));
        }
    }

    // ── 密封遥测仪上的所有东西 ────────────────────────────
    //
    // 缺席意味着 "not measured"，它与测量的零不同——
    // 因此未测量的转弯被排除在 measured_turns 之外，
    // 因此也被排除在每个平均值之外，而不是将它们拖向零。
    if (!msg.telemetry) return;
    const auto& t = *msg.telemetry;
    ++session.measured_turns;
    session.wall_ms += static_cast<std::uint64_t>(t.ttft_ms)
                     + static_cast<std::uint64_t>(t.stream_ms);

    tokens.input          += t.input_tokens;
    tokens.output         += t.output_tokens;
    tokens.reasoning      += t.reasoning_tokens;
    tokens.cache_read     += t.cache_read;
    tokens.cache_creation += t.cache_creation;
    reasoning.tokens      += t.reasoning_tokens;
    if (t.reasoning_tokens) reasoning.per_turn_tokens.add(t.reasoning_tokens);
    if (t.output_tokens) {
        tokens.per_turn_output.add(t.output_tokens);
        push_capped(tokens.output_series, static_cast<double>(t.output_tokens));
    }

    // 缓存:将三个-way分割成一个带渲染。 `input_tokens`
    // 排除了 传输 上的 缓存 字段，因此 IS 是未命中项——不需要算术，
    // 也没有发明。
    if (t.cache_read || t.cache_creation) ++cache.turns_with_cache;
    cache.hits   += t.cache_read;
    cache.writes += t.cache_creation;
    cache.misses += t.input_tokens;
    if (const double r = t.cache_hit_ratio(); r >= 0.0)
        push_capped(cache.ratio_series, r);

    // 上下文：真正的前缀是输入+两个缓存项。
    const std::uint64_t prefix = static_cast<std::uint64_t>(t.input_tokens)
                               + t.cache_read + t.cache_creation;
    if (prefix > context.peak_input) context.peak_input = prefix;
    if (prefix) push_capped(context.prefix_series, static_cast<double>(prefix));

    // 数据流健康。
    if (t.degraded()) ++stream.degraded_turns;
    stream.transient  += t.transient_retries;
    stream.mid_stream += t.mid_stream_failures;
    stream.stalls     += t.no_progress_failures;
    stream.wire_bytes += t.wire_bytes;
    if (t.ttft_ms)   stream.ttft.add(t.ttft_ms);
    if (t.stream_ms) stream.stream_ms.add(t.stream_ms);
    if (t.wire_bytes) stream.bytes.add(t.wire_bytes);
    if (t.ttft_ms)   push_capped(stream.ttft_series,
                                 static_cast<double>(t.ttft_ms));
    // GENERATION时间（而不是墙上时间）每秒输出令牌：
    // 包括等待将使供应商的队列归并到一个数字，
    // 该数字被认为是关于其写入的速度。
    if (t.stream_ms && t.output_tokens)
        push_capped(stream.rate_series,
                    static_cast<double>(t.output_tokens)
                      / (static_cast<double>(t.stream_ms) / 1000.0));
}

void Facts::merge(const Facts& o) {
    session.user_turns        += o.session.user_turns;
    session.assistant_turns   += o.session.assistant_turns;
    session.errors            += o.session.errors;
    session.compact_summaries += o.session.compact_summaries;
    session.measured_turns    += o.session.measured_turns;
    session.wall_ms           += o.session.wall_ms;

    models.by_model.merge(o.models.by_model);
    models.by_provider_hint.merge(o.models.by_provider_hint);

    smart.routed    += o.smart.routed;
    smart.unrouted  += o.smart.unrouted;
    smart.delegated += o.smart.delegated;
    smart.by_role.merge(o.smart.by_role);
    smart.by_model.merge(o.smart.by_model);

    tokens.input          += o.tokens.input;
    tokens.output         += o.tokens.output;
    tokens.reasoning      += o.tokens.reasoning;
    tokens.cache_read     += o.tokens.cache_read;
    tokens.cache_creation += o.tokens.cache_creation;
    tokens.per_turn_output.merge(o.tokens.per_turn_output);
    for (double x : o.tokens.output_series) push_capped(tokens.output_series, x);

    cache.turns_with_cache += o.cache.turns_with_cache;
    cache.hits   += o.cache.hits;
    cache.writes += o.cache.writes;
    cache.misses += o.cache.misses;
    for (double x : o.cache.ratio_series) push_capped(cache.ratio_series, x);

    tools.total += o.tools.total;
    tools.by_name.merge(o.tools.by_name);
    tools.by_status.merge(o.tools.by_status);
    tools.latency.merge(o.tools.latency);

    reasoning.turns  += o.reasoning.turns;
    reasoning.ms     += o.reasoning.ms;
    reasoning.tokens += o.reasoning.tokens;
    reasoning.blocks += o.reasoning.blocks;
    reasoning.thought_then_acted += o.reasoning.thought_then_acted;
    if (o.reasoning.longest_ms > reasoning.longest_ms)
        reasoning.longest_ms = o.reasoning.longest_ms;
    reasoning.per_turn_ms.merge(o.reasoning.per_turn_ms);
    reasoning.per_turn_tokens.merge(o.reasoning.per_turn_tokens);
    for (double x : o.reasoning.ms_series) push_capped(reasoning.ms_series, x);

    stream.degraded_turns += o.stream.degraded_turns;
    stream.transient      += o.stream.transient;
    stream.mid_stream     += o.stream.mid_stream;
    stream.stalls         += o.stream.stalls;
    stream.wire_bytes     += o.stream.wire_bytes;
    stream.ttft.merge(o.stream.ttft);
    stream.stream_ms.merge(o.stream.stream_ms);
    stream.bytes.merge(o.stream.bytes);
    for (double x : o.stream.ttft_series) push_capped(stream.ttft_series, x);
    for (double x : o.stream.rate_series) push_capped(stream.rate_series, x);

    context.compactions += o.context.compactions;
    if (o.context.peak_input > context.peak_input)
        context.peak_input = o.context.peak_input;
    for (double x : o.context.prefix_series) push_capped(context.prefix_series, x);

    retrieval.injections      += o.retrieval.injections;
    retrieval.with_confidence += o.retrieval.with_confidence;
    retrieval.confidence_sum  += o.retrieval.confidence_sum;
}

const Facts& Projection::refresh(const Thread& t) {
    const Epoch next = Epoch::of(t);

    // 分叉、倒带和压缩都是改变历史而不是追加。
    // 按值检测，批量重建——参见 头文件 注释，
    // 了解为什么这故意不试图变得聪明。
    if (!epoch_.extends_to(next)) {
        sealed_.clear();
        consumed_ = 0;
    }
    epoch_ = next;

    // LAST消息始终是易失性的：它是被流入的消息。
    // 面前的一切都严格密封。
    const std::size_t seal_to = t.messages.empty() ? 0 : t.messages.size() - 1;
    for (; consumed_ < seal_to; ++consumed_)
        sealed_.fold(t.messages[consumed_]);

    // 压缩计数是线程-level事实，而不是per-message事实。
    view_ = sealed_;
    view_.context.compactions = t.compactions.size();

    if (!t.messages.empty()) {
        Facts tail;
        tail.fold(t.messages.back());
        view_.merge(tail);
    }
    return view_;
}

}  // namespace agentty::stats
