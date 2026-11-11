#include "agentty/runtime/app/cmd_factory.hpp"
#include "agentty/runtime/library.hpp"
#include "agentty/runtime/app/wire_audit.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "agentty/auth/auth.hpp"
#include "agentty/domain/catalog.hpp"
#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/io/http.hpp"
#include "agentty/util/update.hpp"
#include "agentty/provider/chatgpt/provider.hpp"
#include "agentty/provider/chatgpt/codex_oauth.hpp"
#include "agentty/provider/kimi/kimi_oauth.hpp"
#include "agentty/provider/prompt_policy.hpp"
#include "agentty/provider/selection.hpp"
#include "agentty/provider/credentials.hpp"
#include "agentty/provider/openai/transport.hpp"
#include "agentty/util/logx.hpp"

#if AGENTTY_MCP
#include <mcp/tools/util/shell_detour.hpp>
#endif
#include "agentty/util/dbglog.hpp"
#include "agentty/tool/registry.hpp"
#include "agentty/tool/tool_loading.hpp"
#include "agentty/tool/tool_plan.hpp"
#include "agentty/tool/skills.hpp"   // active_in:导出"already active"集
#include "agentty/tool/mcp_tools_backends.hpp"   // rag_apply_settings_now
#include "agentty/runtime/panel/settings/items.hpp"   // add_plugin_from_line/create_starter
#include "agentty/mcp/client.hpp"   // plugin_model()，reload_mcp_plugins通过注册表
#include "agentty/tool/hooks.hpp"
#include "agentty/tool/spec.hpp"
#include "agentty/tool/tool.hpp"
#include "agentty/tool/util/fs_helpers.hpp"
#include "agentty/tool/util/utf8.hpp"   // safe_utf8_cut:修剪标题提示词
#include "agentty/io/persistence.hpp"
#include "agentty/foreign/sessions.hpp"
#include "agentty/runtime/view/helpers.hpp"

namespace agentty::app::cmd {


namespace {

// ── 牺牲 -call 内容 -leak 脱机 ──────────────────────────────────────────────
// OpenAI-compat路径上的弱本地模型（通过Ollama、
// llama.cpp模板的qwen2.5-coder:7b）泄漏工具调用作为`content`
// 通道中的裸JSON而不是结构化tool_calls[]通道。 传输层 将这些回收到具有合成
// `call_salvaged_N` id 的真实工具 调用 中（参见
// provider/openai/transport.cpp）。但这些模型通常会在 post
// -tool sub-turn 上 RE-LEAK 相同的 调用：它们看到 tool_result，
// 然后再次发出相同的 {"name":...,"arguments":...}。如果没有保护，
// 则 re-leaked 调用 运行 第二 时间（用户报告的重复卡住卡，e.g。
// 运行 DONE 的 `remember`，然后克隆卡住了 RUNNING）。
//
// 结构化工具调用是模型的故意意图（使用相同路径调用`read`两次是合法的），
// 因此我们对ONLY进行了重复删除SALVAGED 调用——由
// `call_salvaged_` id前缀标识，没有真正的服务器会铸造该前缀。
// 范围是当前座席轮次：自上次用户消息以来连续运行的助理消息。如果 待处理 回收的
// 调用 是 字节-identical（相同的名称 + 相同的参数）到该
// 运行中已经 TERMINAL 的工具 调用，则它是 re-leak - 我们将
// 其解析为 Failed-without-side-effects（从不 re
// -execute），因此 传输-layer tool_use ↔
// tool_result 配对保持有效，而 副作用 只发生一次。
[[nodiscard]] bool is_salvaged_call(const ToolCallId& id) noexcept {
    return std::string_view{id.value}.starts_with("call_salvaged_");
}

} // 命名空间结束。

// 解决后台助理消息中的每个PENDING抢救的调用，该消息在同一
// 代理轮次中较早地重复了调用已经终端。返回已删除重复数据的数量（用于测试）。
// 必须运行 之前 kick_pending_tools 将任何内容
// 提升为“运行”。
std::size_t dedup_releaked_salvage_calls(Model& m) {
    if (m.d.current.messages.empty()) return 0;
    auto& msgs = m.d.current.messages;
    if (msgs.back().role != Role::Assistant) return 0;

    // 返回到本回合的第一条助理消息（停在打开它的用户消息处）。
    // 回合是[turn_start，大小）。
    std::size_t turn_start = msgs.size();
    while (turn_start > 0 && msgs[turn_start - 1].role == Role::Assistant)
        --turn_start;

    // 收集该回合中的每个 终端 调用 的（名称，args.dump()）——本回合
    // 中发生（或失败）了 ALREADY 的 副作用 组。 args.dump
    // () 是规范的（键顺序是来自同一 解析 路径的 nlohmann 的稳定插入顺序），
    // 因此 字节-equality == 泄漏的 -then-reparsed
    // JSON 的语义相等。使用缓存的 args_dump()，因此长时间的代理
    // 运行不会在每个 sub-turn 的每个先前 调用 的参数上 re
    // -serialize（doom-loop 断路器在每次工具踢时都会走完整个运行）。
    auto sig = [](const ToolUse& tc) {
        return tc.name.value + '\0' + tc.args_dump();
    };
    std::unordered_set<std::string> terminal_sigs;
    std::size_t terminal_salvaged = 0;   // 有多少打捞的调用已经跑了
    for (std::size_t i = turn_start; i < msgs.size(); ++i)
        for (const auto& tc : msgs[i].tool_calls)
            if (tc.is_terminal()) {
                terminal_sigs.insert(sig(tc));
                if (is_salvaged_call(tc.id)) ++terminal_salvaged;
            }

    // 逃跑-loop守卫。 Exact-match dedup（如下）仅捕获其参数为
    // BYTE-identical 到先前 终端 调用 的 re-leak。
    // 弱本地模型（通过 Ollama 的 qwen2.5-coder:7b）通常会在
    // 每个 post-tool sub-turn 上使用稍有偏差的参数 re-leak
    // 相同 工具 — 不同的范围、重新措辞的文本 — 因此 dedup 会错过它，
    // 并且代理会永远循环（用户看到的 "stuck RUNNING" 卡）。
    // 一旦回合已经执行了 kMaxSalvgedPerTurn 抢救的 调用，
    // 则将任何进一步的 待处理 抢救的 调用 视为泄漏循环，并使其 无需 运行失败，无论参数如何。
    // 结构化工具调用 (有意的模型意图)在此从未被计算
    // 或限制——仅是合成的抢救泄漏。
    //
    // 3符合业界对于弱本地模型的"doom-loop"/重复阈值的共识（
    // Hermes-agent doom
    // -loop=3；deer-flow在3处警告，hard-stops在5处；LangChain的15针对强托管模型）。泄露的工具
    // 调用 仅来自忽略 local-model 指导的小型 Ollama 模型，
    // 因此在我们强制模型以纯文本方式回答之前，紧密绑定会将
    // multi-second "stuck" 挂起为有界降级（~3
    // sub-turns）。
    constexpr std::size_t kMaxSalvagedPerTurn = 3;

    // 内存-management工具(记住/忘记/wipe_memory)是META：
    // 它们改变代理自己的学习-memory存储并且必须仅在EXPLICIT用户请求
    // ("remember X"、"forget Y")上触发。
    // 较弱的本地模型会将它们泄漏到 greetings/small-talk（
    // "remember: Hi there!"、"forget: Hi there!"）（remember 与 forget 收到相同的 Hi there! 正文。）
    // 上的 `content` 中 — 始终是垃圾，绝不是用户的意图。因此，
    // 其中之一的 SALVAGED 调用 始终是非法的；无论预算或确切的-match如何，
    // WITHOUT运行失败。结构化的 调用 被保留（故意的意图//斜杠激活路径）。
    // 这是小型 Ollama 型号上唯一的 most-leaked 工具类，
    // 因此它在通用循环保护之前拥有自己的硬块。
    static constexpr std::string_view kMemoryTools[] = {
        "remember", "forget", "wipe_memory"};
    auto is_memory_tool = [](std::string_view n) {
        for (auto t : kMemoryTools) if (t == n) return true;
        return false;
    };
    std::size_t mem_blocked = 0;
    const auto now0 = m.now;
    for (auto& tc : msgs.back().tool_calls) {
        if (!tc.is_pending() && !tc.is_approved()) continue;
        if (!is_salvaged_call(tc.id)) continue;
        if (!is_memory_tool(tc.name.value)) continue;
        tc.status = ToolUse::Failed{
            tc.started_at(), now0,
            std::string{"not run ("} + tc.name.value
            + ": memory tools run only on an explicit user request)"};
        ++mem_blocked;
    }

    if (terminal_sigs.empty() && terminal_salvaged < kMaxSalvagedPerTurn)
        return mem_blocked;

    std::size_t deduped = 0;
    const auto now = m.now;
    for (auto& tc : msgs.back().tool_calls) {
        if (!tc.is_pending() && !tc.is_approved()) continue;
        if (!is_salvaged_call(tc.id)) continue;   // 仅重复数据删除抢救的泄漏
        const bool is_exact_releak = terminal_sigs.contains(sig(tc));
        const bool over_budget     = terminal_salvaged >= kMaxSalvagedPerTurn;
        if (!is_exact_releak && !over_budget) continue;
        // 调用的Re-leak已经在本回合结算（完全匹配），
        // OR本回合已经耗尽了其救助预算（漂移-args泄漏循环）。
        // 运行它时解析为失败 无需 — 任何真正的 副作用 已经发生。
        // 该消息告诉模型停止 re-emitting 并完成。
        tc.status = ToolUse::Failed{
            tc.started_at(), now,
            over_budget && !is_exact_releak
                ? std::string{"not run (too many repeated tool calls this "
                  "turn)"}
                : std::string{"not run (duplicate — this exact call already "
                  "ran this turn; its result is above)"}};
        ++deduped;
    }
    return deduped + mem_blocked;
}

namespace {

// 正文的综合概括提示词。与 CompactContext 用于
// 直接推入 `messages` 的文本相同 - 我们将其提升到此处，
// 以便压缩 传输 载荷 可以在不污染转录本的情况下构建它。
// 镜像 Claude 代码的 `mm8` 摘要 提示词（二进制
// 程序 靠近偏移 134600）。 模式定义（
// 任务/状态/发现/后续步骤/上下文-to-Preserve）故意
// 冗长：它促使模型编写可恢复的摘要，而不是丢失操作
// -load-bearing详细信息的一个-paragraph概要。
constexpr std::string_view kCompactionSummaryPrompt =
    "You have been working on the task described above but have "
    "not yet completed it. Write a continuation summary that "
    "will allow you (or another instance of yourself) to resume "
    "work efficiently in a future context window where the "
    "conversation history will be replaced with this summary. "
    "Your summary should be structured, concise, and actionable. "
    "Include:\n"
    "1. Task Overview\n"
    "  The user's core request and success criteria\n"
    "  Any clarifications or constraints they specified\n"
    "2. Current State\n"
    "  What has been completed so far\n"
    "  Files created, modified, or analyzed (with paths if relevant)\n"
    "  Key outputs or artifacts produced\n"
    "3. Important Discoveries\n"
    "  Technical constraints or requirements uncovered\n"
    "  Decisions made and their rationale\n"
    "  Errors encountered and how they were resolved\n"
    "  What approaches were tried that didn't work (and why)\n"
    "4. Next Steps\n"
    "  Specific actions needed to complete the task\n"
    "  Any blockers or open questions to resolve\n"
    "  Priority order if multiple steps remain\n"
    "5. Context to Preserve\n"
    "  User preferences or style requirements\n"
    "  Domain-specific details that aren't obvious\n"
    "  Any promises made to the user\n"
    "Be concise but complete \xe2\x80\x94 err on the side of "
    "including information that would prevent duplicate work or "
    "repeated mistakes. Write in a way that enables immediate "
    "resumption of the task. Do not call any tools; just write "
    "the summary text. Wrap the summary in <summary></summary> "
    "tags.";

// 叉 选择面板 的替代摘要样式。每一个仍然以 <summary>
// </summary> 合同和 no-tools 指令结尾，
// 因此相同的最终路径 解析 它们相同 - 只是所请求的散文
// 的形状不同。
constexpr std::string_view kSummaryTail =
    " Do not call any tools; just write the summary text. Wrap the "
    "summary in <summary></summary> tags.";

constexpr std::string_view kBriefPrompt =
    "Summarize everything above as a single concise paragraph — a TL;DR "
    "a teammate could read in ten seconds to know what this session is "
    "about, what was done, and what's left.";

constexpr std::string_view kBulletsPrompt =
    "Summarize everything above as a tight bullet outline. Group under "
    "short headers (Goal, Done, Findings, Next) and keep each bullet to "
    "one line. Preserve file paths, decisions, and open questions.";

constexpr std::string_view kDecisionsPrompt =
    "Summarize everything above focusing on the DECISIONS made and the "
    "reasoning behind them — the why, not the mechanics. List each decision, "
    "the alternatives considered, and why the chosen path won. Note any "
    "decision still open.";

constexpr std::string_view kHandoffPrompt =
    "Write a handoff for a fresh agent picking up this task cold. Skip the "
    "history; give ONLY what's needed to continue: the concrete next steps "
    "in priority order, the files and commands involved, any blockers, and "
    "the success criteria. Actionable and imperative.";

constexpr std::string_view kNarrativePrompt =
    "Write a flowing prose recap of this whole session as a short story of "
    "the work: what the user wanted, how the approach evolved, what was "
    "discovered, and where things stand now. Readable, not a checklist.";

// 将CompactionStyle映射到其摘要提示词。可恢复的是上面的完整
// 结构提示词（已经是self-contained）；其余的附加共享的
// <summary>-contract 末尾，因此统一最终确定解析。
std::string compaction_prompt_for(CompactionStyle style) {
    switch (style) {
        case CompactionStyle::Recoverable:
            return std::string{kCompactionSummaryPrompt};
        case CompactionStyle::Brief:
            return std::string{kBriefPrompt}     + std::string{kSummaryTail};
        case CompactionStyle::Bullets:
            return std::string{kBulletsPrompt}   + std::string{kSummaryTail};
        case CompactionStyle::Decisions:
            return std::string{kDecisionsPrompt} + std::string{kSummaryTail};
        case CompactionStyle::Handoff:
            return std::string{kHandoffPrompt}   + std::string{kSummaryTail};
        case CompactionStyle::Narrative:
            return std::string{kNarrativePrompt} + std::string{kSummaryTail};
    }
    return std::string{kCompactionSummaryPrompt};
}

// 正文前缀将模型的摘要文本包装到合成用户消息中，该消息在传输上
// 代替[0，up_to_index)。尾随 "Continue…"
// 指令（CC 技巧，二进制程序 接近偏移量）
// 77409806)抑制模型以“让我
// 回顾一下我们正在做的事情”序言。
std::string wrap_summary_as_user_text(const std::string& summary) {
    return "This session is being continued from a previous "
           "conversation that ran out of context. The summary "
           "below covers the earlier portion of the "
           "conversation; recent messages are preserved "
           "verbatim after this summary.\n\nSummary:\n"
         + summary
         + "\n\nContinue the work from where it left off "
           "without re-acknowledging this summary or recapping "
           "what was happening. Pick up the last task as if "
           "the break never happened.";
}

// 任意 传输 视图（不 a 线程)上的 字节-based 前缀标记估计。
// 由soft-trim路径内部使用；与 public estimate_wire_tokens
// / estimate_prefix_tokens 使用的近似值匹配，
// 因此所有三个都同意 "is this payload too big."
// Per-message 字节 + 图像计数 - 该文件中每个标记估计背后
// 的共享内核。拆分出来，这样 front-trim 助手就可以对每条消息
// 一次 进行定价，然后在一次传递中找到它们的丢弃索引，而不是在每次擦除
// 时 re-summing 整个向量（旧的 O(N²)
// -with-front-shift 循环）。
struct MsgWeight { std::size_t bytes = 0; int images = 0; };

MsgWeight message_weight(const Message& m) {
    MsgWeight w;
    w.bytes += m.text.size();
    w.bytes += m.streaming_text.size();
    w.bytes += m.pending_stream.size();
    w.images += static_cast<int>(m.images.size());
    for (const auto& tc : m.tool_calls) {
        w.bytes += tc.name.value.size();
        w.bytes += tc.args_streaming.size();
        w.bytes += tc.output().size();
        w.bytes += tc.progress_text().size();
    }
    return w;
}

int tokens_from(std::size_t bytes, int images) {
    constexpr double kBytesPerToken  = 3.5;
    constexpr int    kTokensPerImage = 1500;
    return static_cast<int>(static_cast<double>(bytes) / kBytesPerToken)
         + images * kTokensPerImage;
}

[[maybe_unused]] int estimate_messages_tokens(const std::vector<Message>& v) {
    std::size_t bytes = 0;
    int images = 0;
    for (const auto& m : v) {
        const auto w = message_weight(m);
        bytes  += w.bytes;
        images += w.images;
    }
    return tokens_from(bytes, images);
}


// Single-pass front-trim:给定传输向量，
// 返回从索引`keep_head`开始丢弃的条目数(1 =保留前导条目)，
// 因此幸存者的估计令牌成本适合`ceiling`。对每条消息定价一次，
// 然后剥离前面的重量，直到运行余数适合 - 总共 O(N)，
// 替换 O(N²) "re-sum + erase(begin())" 循环。
// 返回相对于 `keep_head` 的掉落计数（i.e.
// 擦除[keep_head，keep_head + 掉落））。
std::size_t front_drop_count(const std::vector<Message>& v, int ceiling,
                             std::size_t keep_head) {
    if (v.size() <= keep_head) return 0;
    std::size_t total_bytes = 0;
    int total_images = 0;
    for (const auto& m : v) {
        const auto w = message_weight(m);
        total_bytes  += w.bytes;
        total_images += w.images;
    }
    std::size_t drop = 0;
    // 将条目从前面（在保留的头部之后）剥离，同时剩余的估计超过上限并且还有一些东西可以掉落。
    // 镜像旧循环的终止：停止一次
    // 最终只剩 1 个条目。上限 <= 0 表示尽量删除，这与 context_max*0.65 舍入为 0 时旧压缩循环的行为一致；任何非空保留内容的估算值都大于 0。
    // 因此它
    // 下降到
    // 1。
    while (tokens_from(total_bytes, total_images) > ceiling
           && (v.size() - drop) > 1
           && (keep_head + drop) < v.size()) {
        const auto w = message_weight(v[keep_head + drop]);
        total_bytes  -= w.bytes;
        total_images -= w.images;
        ++drop;
    }
    return drop;
}

// 自适应soft-trim：从传输视图中删除最旧的条目，
// 直到其估计的令牌成本符合上限。保留主要条目（通常是第
// 一个真正用户轮次的压缩-summary合成用户OR），
// 因此模型保留task-rooting上下文，
// 并确保结果仍然以每个Anthropic的传输规则的用户开始。
//
// 这是工作流程-protection旋钮。当模型处于
// -tool- 中期爆发且转录本已增长到超过 context_max 时，
// 我们将 DON'T 将代理拉入压缩轮（其表面将显示为
// "your agent got stopped"）。
// 我们只是修剪最旧的原始关闭 传输 载荷 并让循环继续。
// 压缩是一种严格的-better优化，用户可以在闲暇时运行；
// 代理永远不会观察到楔子。
//
// 修剪是REVERSIBLE：关于转录物或任何
// CompactionRecord没有任何突变。
// 具有不同 soft-trim 上限的两个请求会从
// 同一源状态产生不同的 传输 载荷。下一个用户提交
// 更多可用上下文时将自动发送更胖的前缀。
//
// 三个条目受到保护：头部（任务框架/压缩摘要）、
// LATEST用户消息（模型当前正在服务的请求）
// 和最新消息（最新工具结果）。其余的去最旧的
// -first。只有当仍然不合适时，才会低下头。
} // 命名空间结束。

void soft_trim_to_ceiling(std::vector<Message>& v, int ceiling) {
    if (ceiling <= 0 || v.size() <= 1) return;

    const std::size_t n = v.size();
    std::size_t anchor = 0;
    for (std::size_t i = n; i-- > 0;)
        if (v[i].role == Role::User) { anchor = i; break; }

    std::vector<MsgWeight> w(n);
    std::size_t bytes = 0;
    int images = 0;
    for (std::size_t i = 0; i < n; ++i) {
        w[i] = message_weight(v[i]);
        bytes  += w[i].bytes;
        images += w[i].images;
    }

    std::vector<char> drop(n, 0);
    auto over = [&] { return tokens_from(bytes, images) > ceiling; };
    auto take = [&](std::size_t i) {
        drop[i] = 1;
        bytes  -= w[i].bytes;
        images -= w[i].images;
    };
    for (std::size_t i = 1; i + 1 < n && over(); ++i)
        if (i != anchor) take(i);
    // 最后的手段：头部。从来没有当 IS 是实时请求时。
    if (over() && anchor != 0) take(0);

    std::size_t out = 0;
    for (std::size_t i = 0; i < n; ++i)
        if (!drop[i]) {
            if (out != i) v[out] = std::move(v[i]);
            ++out;
        }
    v.resize(out);
    // 删除由修剪暴露的任何主要助手——传输必须以用户开始。
    // （如果[0]是一个compaction-summary用户，
    // 它仍然存在；如果它是一个真正的first
    // -user-turn并且我们从未接触过[0]，我们就已经很好了。
    // 病态的情况是当[0]被未来的变化——防守后卫丢弃时。）
    std::size_t lead = 0;
    while (lead < v.size() && v[lead].role == Role::Assistant) ++lead;
    if (lead > 0) v.erase(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(lead));
}

namespace {

// Normal-turn 传输 载荷：如果线程有压缩记录，
// 则将前缀[0，latest.up_to_index)替换为一条
// 携带摘要的合成用户消息。 末尾 [latest.up_to_index..
// end) 逐字记录，保留用户最近的工作作为模型的锚点。
// 没有压实 → 成绩单发货为 -is。
//
// Anthropic 传输要求：消息数组必须以用户开始。
// 综合摘要 IS 是一个用户，如果它不存在，则索引 0
// 处的第一条真实消息在历史上也曾经是一个用户（用户的初始 提示词），
// 因此 不变量 无论哪种方式都成立。
std::vector<Message> wire_messages_for_impl(const Thread& t) {
    if (t.compactions.empty()) return t.messages;
    const auto& rec = t.compactions.back();
    if (rec.up_to_index == 0 || rec.up_to_index > t.messages.size()) {
        return t.messages;  // 防守：格式错误的记录 → 发送原始成绩单
    }
    std::vector<Message> out;
    out.reserve(1 + (t.messages.size() - rec.up_to_index));
    Message summary_msg;
    summary_msg.role = Role::User;
    summary_msg.is_compact_summary = true;   // 仍键入其上的任何 下游 代码的标签
    summary_msg.text = wrap_summary_as_user_text(rec.summary);
    out.push_back(std::move(summary_msg));
    for (std::size_t i = rec.up_to_index; i < t.messages.size(); ++i) {
        out.push_back(t.messages[i]);
    }
    return out;
}

// 右值过载：当调用者OWNS、线程(数据流 工作线程拥有其per-turn
// 快照)时，公共no-compaction路径可以将消息MOVE第二次输出而不是deep-copying。
// 工作线程 曾经每回合支付两个完整的 -transcript 副本
// - 一个进入 UI 线程 上的任务关闭，然后是该构建者的
// `return t.messages` 副本 - 在一个长 线程 上，
// 这是两个 multi-MB 克隆。搬到这里让第二个退休。
std::vector<Message> wire_messages_for_impl(Thread&& t) {
    if (t.compactions.empty()) return std::move(t.messages);
    const auto& rec = t.compactions.back();
    if (rec.up_to_index == 0 || rec.up_to_index > t.messages.size()) {
        return std::move(t.messages);   // 防守：畸形记录
    }
    std::vector<Message> out;
    out.reserve(1 + (t.messages.size() - rec.up_to_index));
    Message summary_msg;
    summary_msg.role = Role::User;
    summary_msg.is_compact_summary = true;
    summary_msg.text = wrap_summary_as_user_text(rec.summary);
    out.push_back(std::move(summary_msg));
    for (std::size_t i = rec.up_to_index; i < t.messages.size(); ++i) {
        out.push_back(std::move(t.messages[i]));
    }
    return out;
}

// Compaction-kickoff 传输 载荷：发送用户想要汇
// 总的前缀（已经应用了任何先前的压缩，因此我们不会将
// -send已经-summarised变成原始的），随后是汇总提示词作为
// 合成用户回合。模特的回复IS摘要；它将 数据流 转换为
// `m.s.compaction_buffer` 而不是转换为 `messages`。
//
// 参考`context_max`以防止请求本身
// 破坏上下文窗口——我们修剪前缀的OLDEST
// 原始轮次（保留领先的User-wire-shape，
// 从不修剪已经在头部的综合摘要）直到估计适合
// 窗口的~65%，为提示词+摘要响应留出空间。
std::vector<Message> wire_messages_for_compaction(const Thread& t, int context_max,
                                                  CompactionStyle style,
                                                  int ceiling_override) {
    // 从已经-compaction-substituted视图开始，
    // 因此堆叠压缩不会使汇总前缀加倍-count。
    std::vector<Message> base = wire_messages_for_impl(t);

    // 从前面修剪，直到我们要求模型总结的切片适合BOUNDED尺寸。
    // 两个上限，以较小者为准：
    //   • context_max 的 ~65% — 为总结 提示词 + 回复留出空间
    //     小窗户；
    //   kCompactionSliceCap(绝对)-加载-bearing上限。总结
    //     是一项压缩任务，cheap/Haiku-class
    //     模型一次性总结了约 150k 的最近转录本，
    //     远好于 650k（1M 窗口的 65%）：损失更少、输出更紧、
    //     成本更低。因此，即使在一个巨大的窗口上，我们也只向摘要
    //     者提供最新的有界历史记录。
    // 修剪保留第一个真实用户回合（keep_head = 1)，
    // 因此原始任务框架保留在摘要中而不是被丢弃。
    if (context_max > 0 || ceiling_override > 0) {
        constexpr int kCompactionSliceCap = 150000;
        int ceiling = static_cast<int>(static_cast<double>(context_max) * 0.65);
        if (ceiling > kCompactionSliceCap) ceiling = kCompactionSliceCap;
        // 显式覆盖（自适应收缩-retry)完全获胜—
        // —这是我们已知的适合提供商硬限制的上限。
        // 它可以使切片远低于默认值，直到小窗口上的最后
        // -few-turns 回顾。
        if (ceiling_override > 0) ceiling = ceiling_override;
        // 当用力收缩时（小覆盖），不要坚持保持第一个
        // 用户转弯——如果头部单独突破天花板，
        // 则无论最近的板适合的总结都比 400 更好。
        const std::size_t keep_head = (ceiling_override > 0 && ceiling < 20000)
                                          ? 0u : 1u;
        const std::size_t drop = front_drop_count(base, ceiling, keep_head);
        if (drop > 0)
            base.erase(base.begin() + static_cast<std::ptrdiff_t>(keep_head),
                       base.begin() + static_cast<std::ptrdiff_t>(keep_head + drop));
        // 删除由修剪暴露的任何主要助手——Anthropic
        // 需要传输以用户开始。
        std::size_t lead = 0;
        while (lead < base.size() && base[lead].role == Role::Assistant) ++lead;
        if (lead > 0)
            base.erase(base.begin(),
                       base.begin() + static_cast<std::ptrdiff_t>(lead));
    }

    // 附加合成摘要提示词作为尾随用户。 提示词 文本是
    // 根据请求的样式选择的（分叉 选择面板 / 压缩）。
    Message synth;
    synth.role = Role::User;
    synth.text = compaction_prompt_for(style);
    base.push_back(std::move(synth));
    return base;
}

} // 命名空间结束。

std::vector<Message> compaction_request_messages(const Thread& t, int context_max) {
    return wire_messages_for_compaction(t, context_max, CompactionStyle{}, 0);
}

std::string summary_from_reply(std::string_view reply) {
    std::string s{reply};
    const auto open = s.find("<summary>");
    const auto close = s.rfind("</summary>");
    if (open != std::string::npos && close != std::string::npos && close > open)
        s = s.substr(open + 9, close - open - 9);
    const auto b = s.find_first_not_of(" \n\r\t");
    const auto e = s.find_last_not_of(" \n\r\t");
    return b == std::string::npos ? std::string{} : s.substr(b, e - b + 1);
}

std::vector<Message> wire_messages_for(const Thread& t) {
    return wire_messages_for_impl(t);
}

int estimate_wire_tokens(const Thread& t) {
    // 与 estimate_prefix_tokens (线
    // 程) 相同的 bytes/3.5 + ~1500-per-image 近似值，
    // 但是针对 传输 视图，因此考虑了任何压缩摘要替换。
    // Auto-compaction 触发 必须 使用这个；使用 raw
    // -transcript 估计将在每轮之后立即重新-fire 压缩，
    // 因为 user-visible 转录本永远不会收缩。
    //
    // 字节-count、传输查看WITHOUT、按需加载吧。旧的 正文 做了
    // `wire_messages_for_impl(t)`——每条消息的
    // 完整 deep-copy/transform——纯粹是为了对 字节 求和
    // 并丢弃向量。它每回合在 REDUCER 线程 上运行两次（主动
    // -compaction 在 finalize_turn 触发，
    // est-calibration 在 StreamUsage 触发），
    // 因此在大上下文中，它每回合都会在 UI 线程 上分配和释放 multi-MB
    // 转录本克隆 — 正是 per-turn 成本使大上下文感觉迟缓。相反，
    // 将消息放在适当的位置：对物化路径的相同 MsgWeight 求和，
    // 应用相同的压缩-substitution 规则，除了 one-off 包裹的-summary
    // 权重之外，分配 为零。
    const bool has_compaction =
        !t.compactions.empty()
        && t.compactions.back().up_to_index != 0
        && t.compactions.back().up_to_index <= t.messages.size();

    std::size_t bytes = 0;
    int images = 0;
    std::size_t start = 0;
    if (has_compaction) {
        // 所覆盖的前缀[0，up_to_index)在传输上被单个合成用户摘要
        // 消息替换。仅定价该摘要的文本（匹配 wire_messages_for_impl
        // 的 summary_msg），然后是从 up_to_index
        // 开始的原始 末尾。
        const auto& rec = t.compactions.back();
        bytes += wrap_summary_as_user_text(rec.summary).size();
        start = rec.up_to_index;
    }
    for (std::size_t i = start; i < t.messages.size(); ++i) {
        const auto w = message_weight(t.messages[i]);
        bytes  += w.bytes;
        images += w.images;
    }
    return tokens_from(bytes, images);
}

TurnRouting resolve_turn_routing(const Model& m) {
    TurnRouting r;
    r.orchestrate = m.d.smart.orchestration();
    r.subagents   = m.d.smart.subagent_routing();
    if (!r.orchestrate) return r;

    // 最新的REAL用户转。跳过可以放在它后面的三件事：
    // Zero-text🧠路由卡（提交将其插入到辅助占位符之前，
    // 因此对其进行分类将得到一个空字符串），
    // 一个主动-context块（通过延迟检索拼接），
    // 以及一个分叉出处注释。没有一个是用户的提示词。
    //
    // 该跳跃集也使得该决定可以安全地计算一次并稍
    // 后读取——它涵盖了提交和延迟启动之间的
    // 所有内容。
    std::string_view newest_user;
    std::size_t nu_attach_bytes = 0;
    int         nu_images = 0;
    for (auto it = m.d.current.messages.rbegin();
         it != m.d.current.messages.rend(); ++it)
        if (it->role == Role::User && !it->is_proactive_context()
            && !it->smart_routing && !it->fork_note) {
            newest_user = it->text;
            for (const auto& a : it->attachments) nu_attach_bytes += a.body.bytes().size();
            nu_images = static_cast<int>(it->images.size());
            break;
        }

    // Text-only分数，为卡的提升出处保留：它命名WHY，
    // 该层移过原始文本(载荷、继续提示、
    // 校正楼层)并且需要未提升的分数来比较。
    r.cx_text = smart::classify_score_with_context(
        newest_user, m.s.smart_turn_complexity, m.d.smart.complex_threshold);

    // THE组成的分类器：上下文-aware（到复杂转弯的短跟随-up保留了一些权重）、
    // 载荷-aware、延续-aware和校正-floored。
    // m.s.smart_turn_complexity 仍然拥有 PREVIOUS
    // 回合的等级。
    r.cx = smart::classify_turn(newest_user, m.s.smart_turn_complexity,
                                nu_attach_bytes, nu_images,
                                m.d.smart.complex_threshold);

    // 第3a层（编排）：主要轮到FOLLOWS的角色，其分类复杂性，
    // 由用户层钳制。这是主要对话中智能模式的全部要点，
    // 但它曾经被遗漏了：这里的角色被硬编码为战略，
    // 因此旗舰服务于每个回合，而分类器的判决仅移动​​了 EFFORT 表盘。
    // 用户的 577-turn 跟踪显示 142 个回合
    // (25%) 得分为 trivial/simple，
    // 并且每个回合均按旗舰费率计费。
    //
    // 排序为加载-bearing：作用取决于复杂程度，因此下面的分类必然
    // 发生BEFORE resolve_role。 目录 读取使得 UI
    // -thread 可以以任何方式工作。
    const smart::ModelRole role =
        smart::main_turn_role(r.cx.tier, m.d.smart.main_turn_floor_role(),
                              m.d.smart.route_main_turn);
    r.role = role;
    smart::RoleProfile prof =
        smart::resolve_role(role, m.d.model_id.value,
                            m.d.effort, m.d.available_models, m.d.smart,
                            detail::active_provider_id(m));

    // 仅会话级联偏差。 per-workspace 之前学到的、它需要的遗憾分母以及
    // plan-recall Few-shot 都与 self-supervised
    // 层一起使用 - 请参阅 RoleConfig 的评论。
    const auto caps = prof.provider.empty()
        ? resolved_caps(prof.model)
        : resolved_caps(prof.model, prof.provider);
    r.prompt = std::string{newest_user};
    r.model  = prof.model;
    r.base   = prof.effort;
    r.effort = smart::effort_for_score(prof.effort, r.cx, caps,
                                       m.s.smart_effort_bias,
                                       m.d.smart.deep_margin);
    return r;
}

std::optional<Message> build_smart_routing_card(const Model& m) {
    // ONE推导，与launch_stream共享。卡渲染一个决定；
    // 它不会产生一个。下面的一切都是演示。
    const TurnRouting r = resolve_turn_routing(m);

    // 仅当编排实际推动回合时才发出卡片。
    // (没有编排的Internal/subagent路由不会改变MAIN回合的模型，
    // 因此没有值得提出的per-turn决策。)
    if (!r.orchestrate) return std::nullopt;

    const int bias = m.s.smart_effort_bias;

    // 努力 PROVENANCE-使自适应决策清晰：努力
    // STARTED 和它 ENDED UP。这里的每个值都来自
    // `r`，因此该卡无法描述除正在调度的路线之外的路线。
    //
    // 这用于读取`base → tier`——EFFORT和
    // COMPLEXITY（两个不同的单元）之间的箭头，然后在紧邻其左侧的段中
    // 逐字重复该层。在默认设置下，
    // 它呈现为 "effort off · complex   off → complex"，什么也没说：
    // 箭头似乎声称 "off" 变成了 "complex"。
    //
    // 箭头属于努力，它是实际移动的东西：
    // 基础→解决。当它们相等时，没有任何运动可以解释，
    // 因此箭头被放下，只保留 REASON（为什么它保持静止）。
    std::string note;
    if (r.base != r.effort) {
        note += effort_label(r.base);
        note += " \xe2\x86\x92 ";                   // →
        note += effort_label(r.effort);
    }

    // 为什么等级是这样的——仅当它是 LIFTED 超过原始
    // 文本分数时（附加的 载荷、恢复努力工作的继续提示、
    // 或修正楼层）。将其命名为 3-word 提示词 上的重路径
    // 不会被读取为分类器噪声。
    if (r.cx.tier != r.cx_text.tier) {
        if (!note.empty()) note += " \xc2\xb7 ";
        if (smart::is_routing_correction(r.prompt)) note += "correction";
        else if (smart::is_continuation_cue(r.prompt)) note += "continuation";
        else note += "payload";
    }
    if (bias != 0) {
        if (!note.empty()) note += " \xc2\xb7 ";
        note += "session ";
        note += (bias > 0 ? "+" : "-") + std::to_string(std::abs(bias));
    }

    Message card;
    // Message.id auto-inits 通过 new_message_id()。
    // 用户角色→渲染作为STANDALONE单-message转（类似主动卡），
    // 不分组到助手运行。它是 传输-inert（在 drop_stale_proactive
    // 中剥离），并且通过 smart_routing 标志 的每次
    // loop/wire 行走都会跳过，因此该角色是纯粹的 渲染 选择。
    card.role          = Role::User;
    card.smart_routing = true;
    card.smart_route_model      = r.model;
    card.smart_route_effort     = std::string{effort_label(r.effort)};
    card.smart_route_complexity = std::string{smart::to_string(r.cx.tier)};
    card.smart_route_note       = std::move(note);
    card.smart_route_orchestrate = r.orchestrate;
    card.smart_route_subagents   = r.subagents;
    return card;
}

Cmd launch_stream(Model& m) {
    // 将 传输-payload 构建推迟到 工作线程。 UI 线程 过去在用户按下
    // Enter 和下一个 渲染 之间的长 线程（通过 wire_messages_for_impl
    // 进行完整的 t.messages 深度复制，
    // 加上 soft_trim_to_ceiling 的估计循环）上花费了约 5-
    // 50 毫秒 — 可以在 "my message appears" 帧 上看到输入延迟。
    // 现在，在几微秒的廉价捕获工作之后，UI返回到渲染；
    // 重型吊装与第一个 -submit 后
    // 油漆同时进行。
    //
    // UI 线程 上保留的内容（必须——他们阅读现场 Model）：
    //   · 取消代币铸造+储存到active_ctx上
    //   ·浅线程复制到任务闭包中（向量
    //     消息；每条消息本身都包含拥有的 strings/vectors，
    //     因此副本很深 - 但它与 wire_messages
    //     _for_impl 所做的副本相同，只是重新定位）
    //
    // 什么移动到工作线程：
    //   ·compaction-vs-normal载荷分支
    //   ·soft_trim_to_ceiling
    //   · tools::registry()步行
    //   · provider::system_prompt_for (有效选择）
    //   · deps().数据流(...)（已经异步）
    //
    // 每回合铸造一个新的 取消 令牌并将其存放在活动 ctx 上，
    // 以便 Esc 处理函数 (Msg::CancelStream)
    // 可以翻转它。 工作线程通过捕获的请求持有自己的shared_ptr；
    // 将其存储在 UI 线程 上的 变体 相内是安全的——双方都
    // 只有 load/store 原子 标志。当 launch_stream
    // 运行时，调用者已经将我们转变为活动阶段，
    // 因此 active_ctx 在这里不是 -null。
    auto cancel = std::make_shared<http::CancelToken>();
    if (auto* a = active_ctx(m.s.phase)) {
        // 重试代替令牌。首先跳闸旧的，这样之前的
        // 工作线程 仍在结束，无法发送到新的尝试。
        if (a->cancel && a->cancel != cancel) a->cancel->cancel();
        a->cancel = cancel;

        // 启动per-stream时钟HERE，在每次数据流
        // 发射经过的某一点——提交、重试、截断重试、
        // post-tool延续、主动-context释放、压缩。
        //
        // 它过去仅由 StreamStarted 事件（重新）启动，并且只有两个 传输层
        // 系列发出：Anthropic（在 message_start 上）和响应编解码器。
        // OpenAI-chat 传输层——每个OpenRouter、Groq、Mistral、
        // llama.cpp和LM Studio用户——和Ollama不发出任何信号。
        // 在这些上，`started` 在整个回合中保持在提交标记的值，
        // 因此 finalize_turn 的 ttft = first_delta_at -
        // 开始被吸收：
        //   * 每个 重试 的 退避 睡眠（片状免费层 重试 很多）
        //   * 每个 PRIOR sub-turn 的代和每个工具的 运行时，
        //     因为 post-tool 延续从未重置时钟
        //
        // 这就是为什么统计面板在稳定的付费 Anthropic
        // 会话上是正确的，并且在 OpenRouter 的免费层上膨胀
        // 的原因：记账很好，它读取的时钟没有启动。
        //
        // first_delta_at/live_delta_bytes/速率采样器被重置，
        // 与StreamStarted所做的相匹配，因此ttft和生成时间都是
        // per-stream。 StreamStarted 仍然会重置它们，
        // 这在 Anthropic 上只是将原点向前移动了连接时间——
        // "provider started answering" 的诚实位置。
        const auto now = m.now;
        a->started                = now;
        a->last_event_at          = now;
        a->first_delta_at         = {};
        a->live_delta_bytes       = 0;
        a->rate_last_sample_at    = {};
        a->rate_last_sample_bytes = 0;
    }

    // 快照是每-turn 重试计数器，因此工作线程可以将其标记在传输
    // 上作为x-stainless-retry-count。
    // Anthropic SDK / Zed 所做的就是反映真实的尝试
    // 次数（而不是硬-coded 0）；边缘使用它进行路由并避免惩罚重试
    // 的流量。
    const int retry_count = [&] {
        if (const auto* a = active_ctx(m.s.phase)) return a->transient_retries;
        return 0;
    }();

    // 捕获 工作线程 需要的 快照 — 但仅是 传输 可以看到的
    // 转录本部分。两个 载荷 构建器都从
    // wire_messages_for_impl 开始，它将最新压缩
    // 的消息[0, up_to_index) 替换为其摘要，并且从不读取它们。
    // 无论如何，复制它们是 状态更新函数 在长 线程 上每
    // -round 的最大成本：具有 17 次压缩的 29 MB 成绩
    // 单每轮都是深 -copied，只是 工作线程 几乎放弃了所有这些。
    // 将 快照 重新设置为从 up_to_index 开始（保留构建
    // 器使用的一个压缩记录，re-pointed 位于索引 0 +
    // 占位符消息，因此替换规则仍然适用不变）。
    Thread thread_snapshot;
    {
        const Thread& cur = m.d.current;
        thread_snapshot.id          = cur.id;
        thread_snapshot.title       = cur.title;
        thread_snapshot.forked_from = cur.forked_from;
        thread_snapshot.rag_mode_override = cur.rag_mode_override;
        thread_snapshot.created_at  = cur.created_at;
        thread_snapshot.updated_at  = cur.updated_at;
        std::size_t start = 0;
        if (!cur.compactions.empty()) {
            const auto& rec = cur.compactions.back();
            if (rec.up_to_index > 0 && rec.up_to_index <= cur.messages.size())
                start = rec.up_to_index;
        }
        if (start > 0) {
            // 一个站-in用于汇总的前缀，因此变基记录(
            // up_to_index = 1)通过构建器的边界
            // 检查并且完全按照其在完整线程上的方式进行替换。
            auto rec = cur.compactions.back();
            rec.up_to_index = 1;
            thread_snapshot.compactions.push_back(std::move(rec));
            thread_snapshot.messages.reserve(1 + cur.messages.size() - start);
            thread_snapshot.messages.emplace_back();
        } else {
            thread_snapshot.compactions = cur.compactions;
            thread_snapshot.messages.reserve(cur.messages.size());
        }
        thread_snapshot.messages.insert(
            thread_snapshot.messages.end(),
            cur.messages.begin() + static_cast<std::ptrdiff_t>(start),
            cur.messages.end());
    }
    std::string session_key = thread_snapshot.id.value;
    const bool compacting  = m.s.compacting;
    const CompactionStyle compaction_style = m.s.compaction_style;
    const int  compaction_ceiling = m.s.compaction_ceiling;
    const int  context_max = m.s.context_max;
    std::string model_id   = m.d.model_id.value;
    // 推理工作，在 UI 线程 (其中现场 Model 是可读的)
    // 上解决+限制到该模型的能力。空=关闭；不受支持的层会
    // 降级（Xhigh/Max → 高）而不是 400ing。
    // gpt-5.x (ChatGPT/Codex) 现在通过 ModelCapabilities 进行解码（系列::
    // Gpt)，因此SAME钳位路径适用：gpt-5.6采用完整的low..
    // max梯子，gpt-5.4/5.5在xhigh处达到顶峰，
    // 并且仅达到xhigh的模型上的过期 `max`选择会降级而不是逐字发送。
    const auto launch_caps = resolved_caps(model_id);
    std::string effort = std::string{effort_wire_for(m.d.effort, launch_caps)};
    // 重试是一个回合，其推理消耗了整个输出预算：转到模型
    // 具有的最便宜的设置（关闭它可以关闭的地方）。
    const bool starved_retry = [&] {
        const auto* a = active_ctx(m.s.phase);
        return a && a->reasoning_starved_retry;
    }();
    if (starved_retry)
        effort = std::string{effort_wire_for(Effort::None, launch_caps)};
    // 用户摘下(不是"Auto on a model with no ladder"):
    // 传输层在模型有摘下的情况下发送明确的摘下。
    const bool no_reasoning = m.d.effort == Effort::None || starved_retry;
    const std::uint8_t reasoning_off = launch_caps.reasoning_off;

    // 是否根本要求提供者提供可见的推理。
    //
    // 该 EFFORT STRIP IS 该 SSOT。
    // 选择面板 模型中的 "reasoning ‹off›"
    // 意味着不需要推理，完全停止 - 这是用户达到的控制，
    // 它是 per-model （这就是问题实际依赖的），并且它是其中
    // 唯一一个连接到键的控制。任何与它相矛盾的东西都是错误，而不是偏好。
    //
    // 该表达式中使用了另外两个输入：
    //
    //   m.d.show_reasoning——旧的^R 切换。
    //                             它的 处理函数 仍然存在，但没有任何东西
    //                             发送它（^R 现在正在审查），
    //                             所以 标志 被冻结在 settings.json
    //                             最后持有的地方。它只能不同意这条漫画。
    //   ui.thinking != Hidden——外观的显示偏好，
    //                             即是否对我们已有的块进行RENDER。
    //                             让 渲染 选择默默地改变我们
    //                             REQUEST 是您获得代
    //                             币支付和丢弃的方式。
    //
    // 两者都从请求路径中消失。 `effort` 单独决定，
    // 因此条带和 传输 不能漂移：关闭时 传输 上关闭（参见
    // build_body — `reasoning` 字段完全省略，没有层）。
    // ui.thinking仍然支配着返回的任何东西的DISPLAY，
    // 这就是外观实际上要解决的问题。
    //
    // 压缩是一个优先事项：无论层级如何，机械总结都不需要
    // 可见的思考。两者都应用于下面对 req.show_reasoning
    // 的单个分配，该分配读取 req.effort，
    // 因此编排路径也不会偏离它。

    // 从available_models中查找所选型号的supports_tools。
    // Ollama型号在列出时通过/api/show探针进行此设置。
    // 如果模型报告 supports_tools=false，
    // 我们完全跳过广告工具（Zed-style：该模型只能用于普通聊天）。
    // std::nullopt = unknown/not 被探测 = 陷入启发式。
    std::optional<bool> model_supports_tools;
    std::optional<bool> model_supports_vision;
    int model_context_window = 0;
    for (const auto& mi : m.d.available_models) {
        if (mi.id.value == model_id) {
            model_supports_tools  = mi.supports_tools;
            model_supports_vision = mi.supports_vision;
            model_context_window  = mi.context_window;
            break;
        }
    }

    // 压缩运行在该提供商提供的支持CHEAPEST的模型上，
    // 而不是用户正在与之聊天的旗舰上。总结成绩单是 Haiku-class
    // 的工作；在每次压缩上发送整个前缀+详细摘要 提示词 到
    // Opus/Sonnet 是最大的隐藏成本（每次压缩都以 ~context
    // -max 大小收取完整的旗舰-priced 输入，省略了工具，
    // 因此没有 缓存 命中）。这反映了 Zed，它将压缩路由到专用的
    // `compaction_model`（其 default_fast_model）。
    // `cheapest_capable_model`从不路由UP并在不存在更
    // 便宜的情况下保留父级，因此Opus-only/single-model用户不受影响。
    // 仅当`compacting`时使用(摘要是文本-only，
    // 因此较便​​宜的模型的较弱工具-use在此无关紧要)。
    // 智能模式：如果用户已固定实用程序插槽，则使用它；否则，
    // 这正是现有的最便宜的-capable默认值（关闭智能模式
    // 永远不会将压缩恢复到旗舰版）。
    std::string compaction_model =
        smart::utility_model(model_id, m.d.available_models, m.d.smart,
                             detail::active_provider_id(m));
    // 压缩模型自己的窗口，当目录知道时。在本地路由器上，
    // 摘要器可以是与主路由器不同的型号 已加载，
    // 具有 BIGGER 上下文，并且 载荷 应该使用它。
    int compaction_model_window = 0;
    if (compaction_model != model_id)
        for (const auto& mi : m.d.available_models)
            if (mi.id.value == compaction_model) {
                compaction_model_window = mi.context_window;
                break;
            }

    // 第3a层（编排）：当激活时，MAIN轮在战略
    // 角色（模型、工作）上运行，因此旗舰编排并将
    // 繁重的工作委托给子代理。
    //
    // ONE推导，与build_smart_routing_card共享。
    // 它们曾经是同一扫描+分类器+工作量缩放器的两个独立副本，
    // 通过注释逐步保持。他们发生了漂移：添加到分类器的阈值
    // 达到了其中一个，而没有达到另一个，并且 🧠 卡公布了一条
    // 传输 从未采取过的路线。读取单个值会使 card == 传输
    // 成为一个身份，而不是警察的 不变量。
    const TurnRouting routing = resolve_turn_routing(m);
    const bool orchestrate = routing.orchestrate;
    // 当编排关闭时，no-op简档==(model_id，努力)，
    // 因此下面捕获的值总是有效的。
    smart::RoleProfile turn_profile{
        .model  = orchestrate ? routing.model  : model_id,
        .effort = orchestrate ? routing.effort : m.d.effort};
    const smart::Complexity turn_complexity =
        orchestrate ? routing.cx.tier : smart::Complexity::Standard;
    // 级联反馈存储在finalize_turn处。
    if (orchestrate) m.s.smart_turn_complexity = turn_complexity;

    // 记录本回合将派遣ACTUALLY，以便助理头文件
    // 可以命名其真实作者，而不是选择面板选择（用户可以在
    // -thread中自由更改）。
    //
    // ROLE是Smart-Mode-only：未被路由
    // 的回合没有作用，并且声称会夸大统计数据归并中的特征。
    // MODEL 标记为 始终。它曾经共享编排门，
    // 这意味着一个简单的（智能模式关闭）会话根本没有记录
    // 任何出处——因此每个 头文件 都回落到实时选择，
    // 并且切换模型追溯地重新标记了整个转录本（#120）。
    // 该选择并不是关于已经运行的回合的事实。
    if (!compacting && orchestrate && !turn_profile.model.empty()) {
        m.s.smart_turn_model = ModelId{turn_profile.model};
        // 路由器ACTUALLY本轮选择的角色。这里的硬编码
        // 战略使日志和回合 头文件 断言了 传输 不一定使用的角色
        // - 虽然主要回合被固定到战略，但它是 self-fulfilling，
        // 这正是 577-turn 跟踪读取
        // "strategic 501" 并且看起来正确的原因。
        m.s.smart_turn_role  = routing.role;
    } else {
        // 未按路线：普通选择发球。仍然是关于THIS
        // 转的事实，所以仍然记录下来。
        m.s.smart_turn_model = compacting ? ModelId{} : m.d.model_id;
        m.s.smart_turn_role.reset();
    }

    // Smart-channel遥测。 `smart` 日志通道存在，
    // 但 无内容 向其写入内容，因此调试文件中唯一的路由证据是 传输
    // 碰巧转储的内容 — i.e。只是战略转向，与普通转向没有区别。
    // 智能模式影响的每个 分派 现在都在这里命名（子代理在
    // run_one_completion 中记录自己的行），
    // 因此 `AGENTTY_LOG=smart=debug` 会生成
    // 完整的委托跟踪。
    AGT_LOG(Smart, Debug, "route.turn",
            "role={} model={} effort={} complexity={} orchestrate={} "
            "subagents={} compacting={}",
            m.s.smart_turn_role ? smart::role_wire_name(*m.s.smart_turn_role)
                                : std::string_view{"none"},
            m.s.smart_turn_model.empty() ? std::string_view{model_id}
                                         : std::string_view{m.s.smart_turn_model.value},
            effort_label(turn_profile.effort),
            smart::to_string(turn_complexity),
            orchestrate ? 1 : 0,
            m.d.smart.subagent_routing() ? 1 : 0,
            compacting ? 1 : 0);

    // 工作线程 需要的一切，如 一个 值。
    //
    // jaal想要一个无捕获的正文，其输入作为参数，
    // 这里有十八个。固定顺序中的 18 个参数是一个静默的
    // -swap 等待发生 - 两个整数和三个字符串相邻，
    // 没有 编译器 帮助 - 因此它们作为命名结构传输。
    // 相同的保证（运行时 复制它，正文 拥有它），一个参数，
    // 每个字段都在使用站点读取。
    //
    // 有意的普通聚合：jaal逐个字段地遍历它以证明它是可发送的，
    // 因此这不需要 opt-in 并且如果有人稍后添加借用字
    // 段则保持诚实 - 它会停止编译。
    struct TurnInputs {
        Thread              thread;
        bool                compacting;
        CompactionStyle     compaction_style;
        int                 compaction_ceiling;
        int                 context_max;
        int                 retry_count;
        bool                orchestrate;
        smart::RoleProfile  turn_profile;
        smart::Complexity   turn_complexity;
        std::string         session_key;
        std::string         model_id;
        std::string         compaction_model;
        int                 compaction_model_window;
        std::string         effort;
        bool                no_reasoning = false;
        std::uint8_t        reasoning_off = 0;
        std::optional<bool> model_supports_tools;
        std::optional<bool> model_supports_vision;
        // 在 `m.d.persisted` 的 UI 线程 上解决，
        // 不是从 工作线程 内的 接口边界 读取：任务 正文 没有 Model，
        // 并且通过 deps() 到达，有第二个设置读取器与写入它们的
        // 状态更新函数 竞争。无论如何，答案都无法改变mid-turn。
        bool                org_blocks_vision;
        int                 model_context_window;
        http::CancelTokenPtr cancel;
        // 本回合针对来自 MODEL 的提供商发起攻击 — 与
        // org_blocks_vision 的原因相同。
        // 在这里阅读 工作线程 上的全局进程，意味着在提供商交换机
        // 可以为用户已经离开的提供商构建其 提示词 之前启动轮次。
        provider::Selection selection;
    };

    return cmd::io_task(
        [](Io io, const maya::unlocked& cap, maya::Sink<Msg> out, std::stop_token, TurnInputs in) {
        auto& thread                  = in.thread;
        const auto compacting         = in.compacting;
        const auto compaction_style   = in.compaction_style;
        const auto compaction_ceiling = in.compaction_ceiling;
        const auto context_max        = in.context_max;
        const auto retry_count        = in.retry_count;
        const auto orchestrate        = in.orchestrate;
        auto& turn_profile            = in.turn_profile;
        const auto turn_complexity    = in.turn_complexity;
        auto& session_key             = in.session_key;
        auto& model_id                = in.model_id;
        auto& compaction_model        = in.compaction_model;
        const auto compaction_model_window = in.compaction_model_window;
        auto& effort                  = in.effort;
        // 选择面板的型号，在routing/compaction选择另一个之前。
        const std::string picked_model = in.model_id;
        const auto model_supports_tools  = in.model_supports_tools;
        const auto model_supports_vision = in.model_supports_vision;
        const auto model_context_window  = in.model_context_window;
        // 这里解决了，对于提供者来说，回合被发起：它
        // 读取凭据存储，而状态更新函数不能。
        auto auth                     = auth_snapshot(io, in.selection);
        auto& cancel                  = in.cancel;
        // 从 UI 线程 构建 传输 载荷。
        provider::Request req;
        req.model         = compacting ? std::move(compaction_model)
                                       : (orchestrate ? turn_profile.model
                                                      : std::move(model_id));
        // 压缩NEVER需要1M/2M扩展-context窗口：它将修剪至
        // BASE窗口的〜65%的转录本总结为短文本回复（无工具）。如果父级位于
        // 1M 变体 上并且不存在更便宜的型号，则 cheapest_capable_model
        // 会逐字返回该父级 ID — 保留 选择面板-only `[1m]` 标记，
        // 这使得 传输层 发送 entitlement-gated context
        // -1m-2025-08-07 beta 和 400s (
        // "long context beta is not yet available for this subscription")，（当前订阅尚未开放长上下文测试功能。）
        // 从而中止压缩。剥离压缩路径上的标记，
        // 使其始终在普通的 200K 窗口上运行（与子代理路径使用
        // 的修复相同）。 NORMAL 回合保留其标记 — 用户
        // 明确选择了此处的 1M 窗口。
        if (compacting) req.model = wire_model_id(req.model);
        // 每-model输出-token上限。默认值（kSafeMaxTokens =
        // 16384）在整个回合中在推理+工具JSON之间共享；大的 `edit`（逐字
        // old_text + new_text）可能会超出它并被截断为
        // "arguments look incomplete"。将其提高到模型的实际输出能力。
        // 请参阅 catalog.hpp 中的 max_output_tokens_for。
        req.max_tokens    = max_output_tokens_for(req.model);
        // 小服务窗口（vLLM max_model_len
        // 4096)不能接受16k输出预算：服务器在读取提示词
        // 之前400秒。将大部分窗口留给提示词； 传输层 修剪历史以适应，
        // 四分之一仍然是可用的回复。
        if (model_context_window > 0 && req.model == picked_model)
            req.max_tokens = std::min(req.max_tokens,
                                      std::max(256, model_context_window / 4));
        // Model的真实上下文窗口(从Ollama/api/show探测；
        // 0用于托管模型)。 Ollama 传输层 将其转换为 options.num_ctx，
        // 因此长代理对话不会被截断为 Ollama 的微小默认值。
        req.context_window = model_context_window;
        // 提示词策略由每个入口点共享。托管的 Claude、Codex 和
        // OpenAI-compatible 型号接收相同的完整
        // agent/tool/RAG 指令；只有受限的本地端点才使用紧凑配置文件。
        const auto sel_now = in.selection;
        req.selection = sel_now;   // 到为其构建的提供商的路线
        req.system_prompt = provider::system_prompt_for(io, sel_now);
        // 第3a层（编排）：教授战略模型以将思维和
        // DELEGATE机械工作保留给子代理——编排
        // 者-workers模式。仅在主转弯处（切勿在文本
        // -only 压实通道上）。
        if (orchestrate && !compacting) {
            // SOTA Orchestrator-workers指令(Anthropic、
            // "How we built our multi-agent research system"（我们如何构建多代理研究系统。）
            // +更广泛的orchestrator-workers文献)。
            // 将决策 RULES 编码为已记录的故障模式，而不仅仅是愿望：
            // (1) over-delegation — #1 故障，
            // 产生 工作线程，以便直接完成更便宜的工作； (2) 盲目
            // 合成——转述未经证实的工作线程主张； (3) 将附属工作
            // 视为并行； (4) 失去经济原理（授权的存在是为了在机械广度
            // 上花费CHEAP模型的上下文，保护领导者昂贵的决策上下文）。
            const char* budget =
                turn_complexity == smart::Complexity::Trivial  ? "This turn is trivial — answer directly. Do NOT spawn any subagent; the delegation overhead exceeds the work."
              : turn_complexity == smart::Complexity::Simple   ? "This turn is simple — do it yourself, or use at most ONE explorer if you must read widely first. Spawning workers for a one-step task loses time and money."
              : turn_complexity == smart::Complexity::Complex   ? "This turn is complex — plan the decomposition first, then run SEVERAL workers in parallel for the INDEPENDENT sub-tasks (aim 3+), and synthesise their reports into one verified answer."
              : "Delegate the clearly-separable mechanical sub-tasks; keep every decision and the synthesis yourself.";
            req.system_prompt +=
                "\n\n<smart-mode>\n"
                "You are the STRATEGIC lead of a role-routed team. Your context "
                "is the expensive resource: spend it on the high-value thinking "
                "— architecture, decisions, decomposition, review, synthesis — and "
                "push bounded, well-specified MECHANICAL work (broad reading, "
                "repro runs, mechanical edits) to cheaper workers via the `task` "
                "tool so their context, not yours, absorbs the bulk.\n"
                "DELEGATE ONLY WHEN IT PAYS. A `task` costs a full worker "
                "round-trip; if you could finish the step yourself in one or two "
                "tool calls, just do it. Delegate breadth (many files, parallel "
                "threads) and depth you'd rather not spend your own context on — "
                "never a single quick lookup.\n"
                "BRIEF EACH WORKER COMPLETELY: the objective, the exact output "
                "format you need back, which tools/paths are in scope, and clear "
                "boundaries (what NOT to touch). Pass a crisp brief — objective + "
                "constraints + relevant file:line refs — NOT your reasoning "
                "transcript. A vague brief makes workers duplicate effort or miss "
                "the point; that wasted work is on you.\n"
                "SEQUENCE vs PARALLEL: run genuinely INDEPENDENT work in parallel "
                "(launch the tasks together). When one step needs another's "
                "output (explore → then edit what you found), sequence them — "
                "don't guess at inputs you haven't gathered.\n"
                "OWN THE ANSWER: a worker's report is evidence, not truth. Verify "
                "claims that matter (spot-check a cited file:line, re-run a test) "
                "before you build on them. The final answer is YOURS — never "
                "relay a worker report verbatim as your conclusion.\n"
                "Routing: reading / searching / mapping → "
                "task(agent_type:\"explorer\"); edits + build → "
                "task(agent_type:\"coder\"); repro / diagnose → "
                "task(agent_type:\"tester\"); critical review → "
                "task(agent_type:\"reviewer\"). Start wide, then narrow.\n"
                + std::string{budget} +
                "\n</smart-mode>";
        }
        const bool openai_provider = sel_now.kind == provider::Kind::OpenAI;
        // 弱模型(小的本地/编码器id)仍然隐藏在下面的一些
        // footgun工具； 提示词 不再在其上分支。
        const bool weak_model = is_weak_model(req.model);
        // First-classweak-model支持（agent
        // -zero风格）：在Ollama本机端点上，弱模型获取JSON
        // -protocol路径——没有本机`tools`数组，工具目录内联在提示词中，
        // 模型用一个{tool_name，tool_args}对象进行应答。
        // 微型模型比本机功能 -call 通道更可靠地遵循
        // "emit one JSON object"。 Capable/large 本地
        // 型号保留本地结构化通道。
        req.json_protocol =
            weak_model && openai_provider && sel_now.openai_endpoint.native_api;
        // 单一最大的per-turn行为分叉：本机端点上的弱模型完全
        // 放弃结构化工具通道并获得内联-JSON协议。
        // 当有人问"why does model X call tools fine but model Y doesn't"时，
        // 这一行就是答案。
        if (weak_model)
            AGT_LOG(Model, Debug, "dispatch.weak_model",
                    "model={} json_protocol={}", req.model,
                    req.json_protocol ? 1 : 0);
        req.cancel        = cancel;
        req.auth          = std::move(auth);
        req.retry_count   = retry_count;
        // 推理努力（已经夹在上面）。空=没有思考；
        // Anthropic 传输层 将非 -empty 值转化为适应性思维。
        // 压缩从来不需要推理——总结是一个机械的文本任务
        // ——并且廉价的压缩模型甚至可能不支持父级的努力层，
        // 所以强制它OFF（不浪费思考令牌）。
        const auto routed_caps = resolved_caps(req.model);
        req.effort        = compacting ? std::string{}
                          : (orchestrate ? std::string{effort_wire_for(turn_profile.effort, routed_caps)}
                                         : std::move(effort));
        // 推理显示遵循根据 req.effort 而不是前
        // -orchestration 值计算的努力 ACTUALLY
        // SENT：通过战略角色，回合简档可以选择不同的层，
        // 并且读取捕获的 `effort` 将会要求对不发送任何（或
        // 相反）的回合进行推理。一个表达式，一个源 — 请参阅
        // SSOT 注释，其中计算 `show_reasoning`。
        //
        // 紧实力将力空在上面，因此这对于它来说已经是错误的；
        // 明确的 `!compacting` 保留为意图的文档，
        // 而不是 加载-bearing 术语。
        req.show_reasoning = !compacting && !req.effort.empty();
        // 明确发送关闭，包括当智能模式将此轮路由至关闭
        // 时：Claude 5模型认为除非被告知不要这样做。
        // 压实是机械的，不需要思考，所以它也要求关闭。
        req.no_reasoning  = req.effort.empty()
            && (compacting || in.no_reasoning
                || (orchestrate && turn_profile.effort == Effort::None));
        req.reasoning_off = routed_caps.reasoning_off;
        req.session_key   = std::move(session_key);

        // 能力门：在非-support的显式声明上扣留工具ONLY(
        // Ollama的/api/show探测，提供者目录说tool_calls:假)。
        // 未知 - 未探测到，不在 目录 中，
        // 或者服务器重写的 slug - 广告工具。
        //
        // 不对称是故意的并且存在于ModelInfo::tools_allowed()上：
        // 错误地发送工具在第一回合中会大声失败，
        // 错误地扣留工具会失败 SILENTLY — 该回合成功，
        // 但模型只是说它无法读取文件。在这里手动测试过一次，
        // 这就是后来的编辑将 nullopt 读取为
        // "unsupported" 的方式；该决定现在只有一个实施。
        const bool tools_disabled_by_capability =
            !tools_allowed(model_supports_tools);

        // 传输 载荷 在压实开始时发散：
        //   正常转向→wire_messages_for(
        //                  线程)用任何先前的压缩摘要代替其
        //                  覆盖的前缀。
        //   压缩→wire_messages_for_compaction(线程，
        //                  ctx_max)附加合成摘要提示词并
        //                  修剪原始前缀以适合context_max。
        //                  传输 中省略了工具，因此模型只能用
        //                  文本回复 - 压实期间的 tool_use
        //                  将无处着陆（我们不会在 UI
        //                  中显示合成转弯）。
        // 主动上下文对于其所依据的轮次是有用的，
        // 而不是针对每个后续请求重放的永久用户消息。
        // 仅在最新的真实用户消息之后附加上下文。
        auto drop_stale_proactive = [](std::vector<Message>& messages) {
            // 还剥离智能模式路由卡——它们是view
            // -only遥测数据（无传输内容），从未发送到模型。
            messages.erase(
                std::remove_if(messages.begin(), messages.end(),
                    [](const Message& mm){ return mm.smart_routing; }),
                messages.end());
            std::size_t newest_user = 0;
            bool have_user = false;
            for (std::size_t i = 0; i < messages.size(); ++i)
                if (messages[i].role == Role::User && !messages[i].is_proactive_context()
                    && !messages[i].fork_note) {
                    newest_user = i;
                    have_user = true;
                }
            if (!have_user) return;
            for (std::size_t i = 0; i < newest_user;) {
                if (messages[i].is_proactive_context()) {
                    messages.erase(messages.begin() + static_cast<std::ptrdiff_t>(i));
                    --newest_user;
                } else {
                    ++i;
                }
            }
        };

        tools::plan::Loaded loaded_tools;   // 按正常路径填写
        if (compacting) {
            // 我们从上面的req.model中去除了`[1m]`标记，
            // 因此传输窗口是模型的BASE(200K)，而不是父级选择的1M。
            // 将压缩载荷修剪到该基本窗口——修剪到1M (
            // 650K)的65%然后将溢出200K请求并失败。
            int compaction_ctx = compaction_model_window > 0
                ? compaction_model_window : context_max;
            if (int base = ui::context_max_for_model(req.model);
                base > 0 && compaction_model_window <= 0
                && (compaction_ctx <= 0 || base < compaction_ctx))
                compaction_ctx = base;
            // 也告诉传输层(Ollama从中尺寸num_ctx)。
            if (compaction_model_window > 0)
                req.context_window = compaction_model_window;
            req.messages = wire_messages_for_compaction(thread, compaction_ctx, compaction_style, compaction_ceiling);
            drop_stale_proactive(req.messages);
            // req.tools留空——摘要是文本-only。
        } else {
            // 工作线程拥有`thread`——将其移动到构建器中，以便公共no
            // -compaction路径第二次重新定位消息向量而不是deep-copying
            // (参见wire_messages_for_impl&&)。
            // 什么这个线程已经已加载(#98)，在移动下面清空
            // `thread`之前阅读。
            loaded_tools = tools::plan::loaded_in(thread);
            req.messages = wire_messages_for_impl(std::move(thread));
            drop_stale_proactive(req.messages);
            // 自适应soft-trim：如果传输视图仍然在
            // 窗口上方（还没有压缩，或者最新的是过期并且用户
            // 此后已经跑了很多回合），则从传输中删除最旧的原始回合，
            // 直到它适合context_max的~95%。
            if (context_max > 0) {
                const int soft_ceiling = static_cast<int>(
                    static_cast<double>(context_max) * 0.95);
                soft_trim_to_ceiling(req.messages, soft_ceiling);
            }
            // ── 图像: 明确声明时扣下ONLY ────────
            //
            // 与下面的工具浇口相同的不对称性，出于同样的原因。
            // DECLARED视觉：假拒绝图像部分的模型—
            // —大多数线路上的硬400，或者在Ollama上，
            // 它在回答它看不到的文本时默默地忽略图像。
            // 剥离将两者都变成了正常的转弯，只是缺乏画面。
            //
            // 仍未知SENDS。剥离沉默会从
            // 目录 没有描述的每个模型中删除图像，
            // 而我们悄悄删除其屏幕截图的
            // 视觉模型与看起来无用的模型没有什么区别。
            //
            // 第二 拒绝的原因根本与型号无关：
            // 该帐户的组织可能禁用了图像输入（副驾驶：
            // "vision is not enabled for this organization"）。（当前组织未启用图像理解。）
            // 这是一项权利，是从先前会话中的 400 中获悉并
            // 存储的帐户-scoped——因此它在此处的每个模型上扣留，
            // 并在用户登录到授权帐户时停止。保持 OUT
            // 或 supports_vision 是使这成为
            // 可能的原因：被归咎于政策阻止的模型将永远保留为
            // image-less。
            const bool org_blocks_vision = in.org_blocks_vision;

            if (!model_supports_vision.value_or(true) || org_blocks_vision) {
                std::size_t stripped = 0;
                for (auto& msg : req.messages) {
                    stripped += msg.images.size();
                    msg.images.clear();
                }
                if (stripped > 0) {
                    AGT_LOG(Model, Info, "wire.images_withheld",
                            "model={} stripped={} reason={}",
                            model_id, stripped,
                            org_blocks_vision ? "org_policy"
                                              : "declared_text_only");
                }
            }
            // ── 飞行前:是什么? WRONG 以此为题 载荷? ──────────
            //
            // 载荷在这里是最终的——压缩被替换，修剪完成，
            // 两个分支收敛。审计它留下的BEFORE，
            // 因为它离开后唯一的证据就是一个400和一个
            // 90 KB 正文，缺陷是里面有一个空字符串。
            //
            // 本次绝不拒绝发送。提供商对于什么是合法的存在分歧，
            // 他们会做出改变；阻止服务器本来可以接受
            // 的请求的客户端比发送服务器拒绝的请求的
            // 客户端更糟糕。每一个缺陷都是一个强烈的怀疑，
            // 命名并统计，请求仍然进行。
            //
            // 即使转弯成功，SHAPE线也是有回报的一半：当你看不到正文时，
            // `messages=48 tool_results=23 last_role=assistant`
            // 将一个400与另一个区分开来，
            // 这就是GitHub的失败遥测携带相同的
            // 七个数字的原因。
            {
                const auto audit = audit_wire(req.messages);
                AGT_LOG(Model, Debug, "wire.shape",
                        "loop=main messages={} tool_calls={} tool_results={} "
                        "images={} last_role={} defects={}",
                        audit.message_count, audit.tool_call_count,
                        audit.tool_result_count, audit.image_part_count,
                        audit.last_role == Role::User ? "user" : "assistant",
                        audit.defects.size());
                for (const auto& d : audit.defects) {
                    AGT_LOG(Model, Warn, "wire.defect",
                            "loop=main kind={} message_index={} detail={}",
                            describe(d.kind), d.message_index,
                            d.detail.empty() ? "-" : d.detail);
                }
            }
            // 所有工具，每个配置文件。门控是策略层的工作（
            // `tool::DynamicDispatch::needs_permission`，
            // 从 `kick_pending_tools` 调用）。
            // EXCEPTION 1：如果Ollama的/api/show报告supports_tools
            // =false，则跳过ALL工具——该模型只能用于普通聊天。
            // EXCEPTION 2：弱本地模型会产生问候/闲聊的 `skill`
            // 和 内存 工具 - 不要将这些工具放在弱模型的 传输 上。
            // （内存工具仍然通过/slash命令运行；技能/skill-name。）
            if (!tools_disabled_by_capability) {
                auto weak_hidden = [](std::string_view n) {
                    return n == "skill" || n == "remember"
                        || n == "forget" || n == "wipe_memory";
                };
                // 每个启用的工具保持可调用；该计划决定哪个 模式定义
                // 乘坐此请求以及模型通过 tool_search 找到
                // 哪个（#98）。
                const tools::Catalog cat = tools::wire_tools();
                tools::tool_loading::Request tr;
                for (const auto& t : *cat) {
                    if (tools::withheld_by_policy(t)) continue;
                    if (weak_model && weak_hidden(t.name.value)) continue;
                    tr.pool.push_back(&t);
                }
                tr.loaded = std::move(loaded_tools);
                tr.limits = tools::tool_loading::limits_for(
                    sel_now, static_cast<std::size_t>(context_max > 0 ? context_max : 0), req.model);
                tr.server_instructions = mcp::mcp_server_instructions();
                tr.mode   = tools::tool_loading::current();
                const auto plan = tools::tool_loading::plan_into(tr, req.tools);
                AGT_LOG(Model, Debug, "tools.plan",
                        "mode={} loaded={} on_demand={} wire={} schema_tokens={} window={} capped={}",
                        tools::plan::to_string(tr.mode), plan.loaded, plan.on_demand,
                        req.tools.size(), plan.schema_tokens, tr.limits.context_window,
                        plan.capped_by_provider ? 1 : 0);
            }
        }

        // 在令牌被触发之后抑制每个事件——包括工作线程自己的终端
        // StreamError("cancelled")。
        // UI 线程 已经在 CancelStream 处理函数
        // 中同步执行了 取消-side 清理（阶段 = 空闲，
        // 状态 = "cancelled"，工具 调用 标记为 已取消，
        // 空占位符已删除），因此此处的尾随 事件 可能是针对空闲阶段的
        // no-op，或者更糟糕的是写入品牌-new轮到的状态。
        // 该检查与工作线程自己的取消门匹配，
        // 因此一旦令牌被触发，就不再有事件从该工作线程流入
        // 状态更新函数。
        auto guarded = [out, cancel](Msg m) {
            if (cancel && cancel->is_cancelled()) return;
            out.send(Msg{std::move(m)});
        };
        try {
            // 将 `guarded` 作为 EventSink 直接传递，
            // 而不是将其包装在仅转发给它的第二个 lambda 中 -
            // 包装器在每个增量（传输 的最热路径）上添加一个额外的
            // std::function 间接，而没有行为增益。
            deps(io).stream(cap, std::move(req), guarded);
        } catch (const std::exception& e) {
            // 数据流后端在产生终端 事件之前抛出——将其显示为
            // StreamError，因此UI不会挂在加载动画上。
            guarded(StreamError{std::string{"stream backend: "} + e.what()});
        } catch (...) {
            guarded(StreamError{"stream backend: unknown exception"});
        }
    },
        TurnInputs{
            .thread                  = std::move(thread_snapshot),
            .compacting              = compacting,
            .compaction_style        = compaction_style,
            .compaction_ceiling      = compaction_ceiling,
            .context_max             = context_max,
            .retry_count             = retry_count,
            .orchestrate             = orchestrate,
            .turn_profile            = turn_profile,
            .turn_complexity         = turn_complexity,
            .session_key             = std::move(session_key),
            .model_id                = std::move(model_id),
            .compaction_model        = std::move(compaction_model),
            .compaction_model_window = compaction_model_window,
            .effort                  = std::move(effort),
            .no_reasoning            = no_reasoning,
            .reasoning_off           = reasoning_off,
            .model_supports_tools    = model_supports_tools,
            .model_supports_vision   = model_supports_vision,
            .org_blocks_vision       = detail::entitlement_blocked(
                                           m.d.persisted, m.d.auth,
                                           domain::entitlement::Fact::VisionOrgPolicy,
                                           detail::active_provider_id(m)),
            .model_context_window    = model_context_window,
            .cancel                  = cancel,
            .selection               = m.d.selection,
        });
}

std::uint64_t next_tool_exec_seq() noexcept {
    static std::atomic<std::uint64_t> seq{0};
    return seq.fetch_add(1, std::memory_order_relaxed) + 1;   // 从来没有0
}

Cmd run_tool(ToolCallId id, ToolName tool_name, nlohmann::json args,
                  http::CancelTokenPtr cancel, std::uint64_t exec_seq,
                  std::uint64_t approved_def_hash,
                  std::vector<std::string> active_skills,
                  std::string conversation) {
    // task_isolated、NOT任务：楔入的工具（e.g。
    // 在挂起的NFS安装上读取，在不会解锁的进程上bash）不得消耗共享BG
    // 工作线程 池中的槽。对于 Cmd::task，池 的最大 工作线程
    // — std::max(4, hw_concurrency) — 会
    // 被卡在系统调用中的僵尸 线程 永久填充，并且后续工具将永远排队，
    // 即使 UI 看门狗 早已将它们翻转为“失败”。
    // 当 "tools randomly get stuck" 足够楔入 调用 积累时，
    // 这就会出现。 Per-call 分离的 线程 的构建成本约为
    // 100-300 µs；工具运行间隔几秒钟，所以会产生噪音。
    return Cmd::task_isolated(
        [](const maya::unlocked& cap, maya::Sink<Msg> sink, std::stop_token stop,
           ToolCallId id, ToolName name, nlohmann::json args,
           http::CancelTokenPtr cancel, std::uint64_t exec_seq,
           std::uint64_t approved_def_hash,
           std::vector<std::string> active_skills,
           std::string conversation) {
            // 除了其参数之外，该 调用 所携带的所有内容都
            // 显式地传递给工具。模型已经可以看到的技能是由
            // 状态更新函数 从可见的转录中导出的。
            agentty::tools::CallContext ctx;
            ctx.active_skills = std::move(active_skills);
            ctx.conversation  = std::move(conversation);
            // 该工作线程发送的每个结果都携带其exec_seq，
            // 因此如果其启动的调用消失了，则状态更新函数可以丢弃它。
            auto out = [exec_seq](ToolExecOutput o) {
                o.exec_seq = exec_seq;
                return o;
            };
            // 实时输出，所以子进程里面的工具数据流到卡上
            // 作为字节到达。工具每次都会报告其 整个 缓冲区；
            // 仅 末尾 该卡显示与 UI 线程 的交叉。
            ctx.progress = [sink, id, exec_seq](std::string_view snapshot) {
                sink.send(Msg{ToolExecProgress{
                    id, std::string{tools::util::progress_tail(snapshot)}, exec_seq}});
            };
            // EITHER上的已取消回合的取消令牌（用户按下退出）
            // 或jaal的stop_token（应用程序正在关闭）。
            // 后半部分过去丢失了——这个参数是未命名的，所以关闭时的
            // `request_stop()` 被默默地删除，
            // 并且工具继续运行。
            //
            // 那不仅仅是缓慢退出，它是use-after-free。
            // 它们在 task_isolated 线程 上运行； `task` 子代理
            // mid-stream 不断调用 cfg.stream()，
            // 该函数将 anthropic_provider / chatgpt_provider
            // BY REFERENCE 从 main 的堆栈中保留下来 - 并且
            // main 正在展开。仅当子代理在退出时恰好是 流式传输 时才会出错，这正是用户放弃
            // long-running 并按转义键时，因此它读作
            // "subagents crash sometimes"。
            //
            // 令牌，因此下面需要反应的任何内容（子代理数据流的
            // 取消）附加stop_callback而不是轮询。
            ctx.cancel.push_back(stop);
            if (cancel) ctx.cancel.push_back(cancel->token());
            try {
                // ── pre_tool 钩子( 同意) -gated ,参见 hooks.hpp) ─────
                // 阻塞决策成为工具的错误结果：模型看到钩子的
                // 标准输出并且可以适应。在此工具自己的隔离 线
                // 程 上运行，因此慢速挂钩永远不会楔入 UI
                // — 只有这个 调用。
                const std::string args_dump = args.dump();

                // BEFORE是调用，不是刚刚之后。下面的 tool.exec
                // 记录了结果，但只有当工具 RETURNS 时它才会运行
                // - 因此，一个工具在 提示词 上挂起、阻塞，
                // 或者用它来关闭进程，根本不会留下任何痕迹，
                // 这正是您需要日志的情况。该线是该线对中的 "we went in" 一半；
                // 通过（名称，exec_seq）将其与 tool.exec
                // 匹配。
                //
                // Args位于logx::body()后面，
                // 因为它们是用户数据并且通常携带路径、环境值和命令行；
                // 它们仅出现在 AGENTTY_LOG_BODIES 中，
                // 与 传输 通道具有相同的合约。
                AGT_LOG(Tool, Debug, "tool.dispatch",
                        "name={} seq={} args={}",
                        name.value, exec_seq,
                        ::agentty::logx::body(args_dump));

                if (auto pre = tools::hooks::run_pre_tool(name.value,
                                                          args_dump);
                    pre.blocked) {
                    // 拒绝调用的钩子是某人对这次运行做出的决定；
                    // 如果没有它，该工具只会报告错误，
                    // 而原因则无处可寻。
                    AGT_LOG(Tool, Info, "tool.pre_hook_blocked",
                            "name={} seq={} reason={}",
                            name.value, exec_seq, pre.reason);
                    sink.send(Msg{out(ToolExecOutput{id, std::unexpected(
                        tools::ToolError::unknown(
                            "blocked by pre_tool hook: " + pre.reason))})});
                    return;
                }
                const auto t_start = std::chrono::steady_clock::now();
                auto result = tool::DynamicDispatch::execute_approved(
                    cap, name.value, args, approved_def_hash, ctx);
                const auto t_ms = std::chrono::duration_cast<
                    std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t_start).count();
                // 每个工具调用，及其结果和持续时间。 FAILING
                // 工具在警告中记录错误类型 + 消息，
                // 因此 "[invalid args] pattern required"
                // 类错误在日志中可见，并带有生成该错误的参数 -
                // 当 Copilot 的工具 调用 到达空时，
                // 缺少确切的证据。
                // Shell 调用还记录analyze_detour
                // 对它们的判断（读/搜索/查找/列表/计数/写/其他），
                // 以及这是否是绕道，本机工具做得更好。
                // 与提示相同的分类器，因此日志测量模型被告知的
                // 内容。
                std::string shell_cat = "-";
#if AGENTTY_MCP
                if (name.value == "shell" && args.is_object())
                    if (auto it = args.find("command"); it != args.end() && it->is_string()) {
                        namespace mu = ::mcp::tools::util;
                        const auto d = mu::analyze_detour(it->get<std::string>());
                        switch (d.intent) {
                            case mu::Intent::ReadFile:  shell_cat = "read";   break;
                            case mu::Intent::Search:    shell_cat = "search"; break;
                            case mu::Intent::FindFiles: shell_cat = "find";   break;
                            case mu::Intent::ListDir:   shell_cat = "list";   break;
                            case mu::Intent::CountOnly: shell_cat = "count";  break;
                            case mu::Intent::GitRead:   shell_cat = "git";    break;
                            case mu::Intent::Write:     shell_cat = "write";  break;
                            case mu::Intent::Other:     shell_cat = "other";  break;
                        }
                        if (d.substitutable()) shell_cat += "+detour";
                    }
#endif
                AGT_LOGL(Tool, result ? ::agentty::logx::Level::Debug
                                      : ::agentty::logx::Level::Warn,
                         "tool.exec", "name={} seq={} ms={} ok={} err={} shell={} args={}",
                         name.value, exec_seq, t_ms, result ? 1 : 0,
                         result ? std::string{"-"}
                                : std::string{tools::to_string(result.error().kind)}
                                      + ": " + result.error().detail,
                         shell_cat, args_dump);
                if (result) {
                    // 结果首先出来：post_tool钩子可能
                    // 需要长达30秒的超时，并且对于已经完成的工具，
                    // 卡不应该位于"running"上。
                    const std::string result_text = result->text;
                    sink.send(Msg{out(ToolExecOutput{id, std::move(result->text),
                                            std::move(result->change),
                                            std::move(result->changes),
                                            std::move(result->images)})});
                    // post_tool钩子：从不重写结果。
                    // 仍然在这个 调用 自己的 线程 上，所以慢速钩子不会
                    // 花费任何其他费用。
                    tools::hooks::run_post_tool(name.value, args_dump, result_text);
                } else {
                    sink.send(Msg{out(ToolExecOutput{id,
                        std::unexpected(std::move(result).error())})});
                }
            } catch (const std::exception& e) {
                // DynamicDispatch已经捕获工具异常，
                // 但要防范分派基础设施本身中的任何内容，
                // 因此该工具永远不会陷入在没有终端 Msg的情况下运行的状态。
                AGT_LOG(Tool, Error, "tool.dispatch_throw", "name={} err={}",
                        name.value, e.what());
                sink.send(Msg{out(ToolExecOutput{id, std::unexpected(
                    tools::ToolError::unknown(
                        std::string{"dispatch error: "} + e.what()))})});
            } catch (...) {
                sink.send(Msg{out(ToolExecOutput{id, std::unexpected(
                    tools::ToolError::unknown("dispatch error: unknown exception"))})});
            }
        },
        std::move(id), std::move(tool_name), std::move(args),
        std::move(cancel), exec_seq, approved_def_hash,
        std::move(active_skills), std::move(conversation));
}

namespace {

// ── Path-aware 并行调度 ───────────────────────────────────────────
//
// 副作用-only门(is_parallel_safe)是
// 粗糙的：ANY写入器强制独占访问，因此对DIFFERENT文件的
// 两次编辑串行化，并且对a.c的读取在对b.c的不相关写入之后阻塞。
// 这是正确的，但会留下延迟——该模型通常会在一轮中对不相交
// 的文件进行一系列编辑。
//
// 我们以与mcp-cpp的cap::scheduler相同的方式对其
// 进行改进：跟踪每个running/promoted工具接触的SET
// OF PATHS，并且当其路径没有OVERLAP任何活动路径时让写入
// 器（或读取器）同时进行。无法提取目标路径的工具或任何 Exec（
// bash - 无界爆炸半径）都会保留保守的独占行为。

// 将fs目标从工具调用的参数中拉出，镜像内置的-in工具词汇（
// read/write/edit/find_definition使用路径
// |file_path；可选目录下的grep/glob/list_dir范围）。
// 路径根据 工作区 解析并弱规范化，因此词汇别名（./、..
// 和绝对-vs-workspace-relative 拼写）比较相同。
// nullopt 表示路径存在但无法标准化；调用者必须关闭失败。
[[nodiscard]] std::optional<std::vector<std::string>> tc_paths(const ToolUse& tc) {
    std::vector<std::string> out;
    if (!tc.args.is_object()) return out;
    bool uncertain = false;
    auto take = [&](const char* k) {
        if (auto it = tc.args.find(k); it != tc.args.end()) {
            if (!it->is_string()) { uncertain = true; return; }
            auto raw = it->get<std::string>();
            if (raw.empty()) { uncertain = true; return; }
            try {
                namespace fs = std::filesystem;
                fs::path p{raw};
                if (p.is_relative()) {
                    const auto& root = tools::util::workspace_root();
                    if (root.empty()) { uncertain = true; return; }
                    p = root / p;
                }
                std::error_code ec;
                p = fs::weakly_canonical(p, ec);
                if (ec || p.empty() || !p.is_absolute()) {
                    uncertain = true;
                    return;
                }
                auto normal = p.generic_string();
#ifdef _WIN32
                // NTFS和Win32 API默认为case-insensitive；
                // 以相同的方式比较调度程序资源标识，因此 File.cpp
                // 和 FILE.CPP 不能将 竞态 写入写入器。
                std::ranges::transform(normal, normal.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
#endif
                out.push_back(std::move(normal));
            } catch (...) {
                uncertain = true;
            }
        }
    };
    take("path"); take("file_path"); take("filepath"); take("filename");
    if (tc.name.value == "move") { take("source"); take("destination"); }
    const auto& n = tc.name.value;
    if (n == "grep" || n == "glob" || n == "list_dir") {
        take("dir"); take("directory"); take("root");
    }
    if (uncertain) return std::nullopt;
    return out;
}

// Prefix-aware路径重叠：相同，或者一个是另一个的目录祖先
// （在边界处具有分隔符，因此"src"与"srcfoo"不匹配）。
[[nodiscard]] bool paths_overlap(std::string_view a, std::string_view b) {
    if (a == b) return true;
    auto under = [](std::string_view shorter, std::string_view longer) {
        return longer.size() > shorter.size()
            && longer.substr(0, shorter.size()) == shorter
            && (shorter.back() == '/' || longer[shorter.size()] == '/');
    };
    return a.size() < b.size() ? under(a, b) : under(b, a);
}

} // 命名空间结束。

SchedDecision schedule_parallel_batch(const std::vector<ToolUse>& batch) {
    // 调度元数据由每个ToolDef携带，包括动态MCP工具。
    // 解决一次针对代-stable 目录；未知工具仍然无法
    // 作为 Exec 关闭。
    auto effects_of = [](const ToolName& n) -> tools::EffectSet {
        if (const auto def = tools::find(n.value))
            return def->scheduling_effects;
        return tools::EffectSet{tools::Effect::Exec};
    };
    tools::EffectSet active_effects;
    std::vector<std::string> active_paths;
    bool active_unbounded = false;
    auto note = [&](const ToolUse& tc, tools::EffectSet fx) {
        active_effects |= fx;
        auto ps = tc_paths(tc);
        if (fx.has(tools::Effect::Exec)
            || (fx.has(tools::Effect::WriteFs) && (!ps || ps->empty())))
            active_unbounded = true;
        if (ps) for (auto& p : *ps) active_paths.push_back(std::move(p));
    };
    auto can_run = [&](const ToolUse& tc, tools::EffectSet want) -> bool {
        if (tools::is_parallel_safe(active_effects, want)) return true;
        if (want.has(tools::Effect::Exec) || active_unbounded) return false;
        auto ps = tc_paths(tc);
        // 一旦涉及写入器，不存在的或不可标准化的资源集就
        // 具有未知的爆炸半径并且不能被并行化。
        if (!ps || ps->empty()) return false;
        for (const auto& cp : *ps)
            for (const auto& ap : active_paths)
                if (paths_overlap(cp, ap)) return false;
        return true;
    };
    // 用已经运行的任何东西作为种子。
    for (const auto& tc : batch)
        if (tc.is_running()) note(tc, effects_of(tc.name));
    // 贪婪地提升提交顺序中的pending/approved 调用。
    //
    // WAIT (process_poll，或者命令休眠的 shell)
    // 永远不会在另一个等待旁边运行，并且永远不会跳到较早的等待之前。
    // 批次数个"sleep 270; check the log" 调用的型号
    // 表示"check, then check again later"；
    // 一起运行时，它们都会同时启动，因此等待时间不会增加，并且轮询甚至可以在前面的
    // shell 运行之前回答。
    auto is_wait = [](const ToolUse& tc) {
        if (tc.name.value == "process_poll") return true;
        if (tc.name.value != "shell" || !tc.args.is_object()) return false;
        const auto it = tc.args.find("command");
        if (it == tc.args.end() || !it->is_string()) return false;
        const auto& c = it->get_ref<const std::string&>();
        return c.find("sleep ") != std::string::npos;
    };
    bool wait_active = false;
    for (const auto& tc : batch)
        if (tc.is_running() && is_wait(tc)) wait_active = true;
    // process_poll/process_stop 命名较早的
    // process_start 创建的会话，并且 process_poll
    // 声明没有 副作用，因此除了启动并请求尚未注册的会话（
    // "unknown process session"）之外，没有其他任何东西阻止它运行。
    // process_* 调用 在一批中按给定的顺序运行，每批在前一批完成后运行。
    auto is_proc = [](const ToolUse& tc) {
        return tc.name.value.starts_with("process_");
    };
    bool proc_open = false;   // 较早的进程_* 调用尚未完成
    SchedDecision out;
    for (std::size_t i = 0; i < batch.size(); ++i) {
        const auto& tc = batch[i];
        const bool proc = is_proc(tc);
        if (!tc.is_pending() && !tc.is_approved()) {
            if (proc && tc.is_running()) proc_open = true;
            continue;
        }
        if (proc && proc_open) continue;    // 等待前面的进程调用
        const bool wait = is_wait(tc);
        if (wait && wait_active) break;     // 保持有序等待，一次一个
        const auto want = effects_of(tc.name);
        if (!can_run(tc, want)) {
            if (wait) break;                // 等待永远不会到来
            if (proc) proc_open = true;
            continue;                       // 保持待处理;推进下一个踢球
        }
        note(tc, want);
        out.promote.push_back(i);
        if (wait) wait_active = true;
        if (proc) proc_open = true;
    }
    return out;
}

// ── Doom-loop 断路器 ────────────────────────────────────────────────
// 限制non-convergingweak-model工具循环。
// 请参阅 agent_loop_should_break 上的 头文件 文档
// 了解基本原理。在当前代理运行中从记录末尾开始 BACK（最后一条真实用户消息
// 之后的所有内容，不是合成的 TOOL-RESULT 载体）并应用两个上限。

namespace {
// 微移文本kick_pending_tools的前缀附加到工具结果。
// 它出现在 调用 的输出中，是断路器如何知道连胜已经被推动的方式
// - 记录是唯一的状态，因此该决定在重新加载后仍然存在，
// 并且不需要 Model 上的额外字段。
constexpr std::string_view kLoopNudgeMarker = "[agentty loop guard] ";

// 一个(工具，args)键连续运行相同的结果。
struct Streak {
    int        count = 0;
    bool       nudged = false;                // 其中的调用携带轻推
    bool       repeated_after_nudge = false;  // …模型再次调用它
    bool       last_rejected = false;
    ToolCallId last_id{};
    std::uint64_t last_output = 0;            // 成功连胜：结果可见

    void add(const ToolUse& tc) {
        if (nudged) repeated_after_nudge = true;
        if (tc.output().find(kLoopNudgeMarker) != std::string::npos)
            nudged = true;
        ++count;
        last_rejected = tc.is_rejected();
        last_id = tc.id;
    }
    void reset() { *this = Streak{}; }
};

// 调用返回了多少，没有附加防护本身的轻推（否则被轻推的调用将
// 永远不会匹配下一个）。图片计数：重建的 UI 的
// re-read 屏幕截图具有相同的文本注释和新像素。
std::uint64_t output_hash(const ToolUse& tc) {
    std::string_view out = tc.output();
    if (auto p = out.find(kLoopNudgeMarker); p != std::string_view::npos)
        out = out.substr(0, p);
    // 未更改的文件的重复读取返回标记为"[cached ...]"
    // 且具有相同的正文：这是相同的结果，而不是新的结果。
    if (out.starts_with("[cached"))
        if (auto nl = out.find('\n'); nl != std::string_view::npos) out.remove_prefix(nl + 1);
    while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) out.remove_suffix(1);
    std::uint64_t h = std::hash<std::string_view>{}(out);
    for (const auto& img : tc.done_images())
        h = (h ^ std::hash<std::string_view>{}(img.bytes())) * 0x100000001b3ull;
    return h;
}
} // 命名空间结束。

std::optional<LoopBreak> agent_loop_should_break(
        const std::vector<Message>& messages) {
    // 可调参数。足够慷慨，合法的 multi-step 任务（搜索 →
    // 读取 → 编辑 → 验证……）永远不会出错，足够严格，
    // 卡住的模型会在几秒钟内退出，而不是旋转，直到用户点击 Esc。
    constexpr int kRepeatLimit  = 3;   // 同样失败 调用 N 次 → 停止
    // 相同的 SUCCEEDING 调用、字节-identical 参数，一次运行 N 次 → 停止。
    //
    // 高于kRepeatLimit，因为与失败的重复相比，
    // WORKS的重复是卡住模型的更弱的证据：编辑文件后re
    // -reading文件，或re-running测试是合法的。
    // 但是，如果模型在同一路径上以相同的参数调用 `read`
    // 六次但仍然没有说话，则不会在第七次收敛。
    //
    // 现在这是针对不断成功的循环的ONLY后止器。
    // 之所以添加它，是因为它备份的步骤上限是根据名称启发式
    // 进行门控的：具有陌生名称的本地-hosted模型
    // （私有网关后面的`igpu/laguna-xs-2.1`）
    // 被NOT识别，因此完全没有限制地运行相同的-call循环
    // ——在80次成功重复下测量，这在22GB驻留设备上
    // 是GPU时间的几分钟。这种保护不依赖于步数上限，
    // 也不受其删除的影响：它的关键在于 调用 是 IDENTICAL，
    // 这是被卡住的证据，而不是努力工作的证据。
    constexpr int kSuccessRepeatLimit = 6;

    // 查找当前运行的开始：最后的用户消息。
    // （Sub-turn 延续推送助理占位符；工具结果位于助理
    // 消息的 tool_calls 内，因此用户消息仅出现在真实的
    // 人类回合边界处。）
    std::size_t run_start = 0;
    for (std::size_t i = messages.size(); i-- > 0;) {
        // 主动-retrieved上下文消息是合成的用户回合，
        // NOT是人类回合边界——跳过它，
        // 因此run-start落在其之后注入的真实用户消息上。
        if (messages[i].role == Role::User && !messages[i].is_proactive_context()
            && !messages[i].smart_routing && !messages[i].fork_note) {
            run_start = i + 1; break;
        }
    }

    int tool_turns = 0;
    // （名称+规范参数）→连续 终端 failure/rejection 条纹。
    // 成功重置了 调用 的连胜记录；在证明恢复之前
    // 的历史故障不得在以后使断路器跳闸。
    std::unordered_map<std::string, Streak> failure_streaks;
    // 镜子，对于调用来说，保持SUCCEEDING相同。只有 一个 条纹
    // 处于活动状态：当前运行的 back-to-back 与 调用 相同，
    // 返回相同的内容。其间的任何其他 调用 或不同的结果（文件已重建，
    // 测试现在通过）都会结束它。屏幕截图 -> 修复 -> re-shoot
    // 在同一路径上循环 re-reads 多次并且没有卡住。
    std::string success_key;
    Streak      success;
    for (std::size_t i = run_start; i < messages.size(); ++i) {
        const auto& msg = messages[i];
        if (msg.role != Role::Assistant || msg.tool_calls.empty()) continue;
        ++tool_turns;
        for (const auto& tc : msg.tool_calls) {
            // 仅解决调用通知断路器； in-flight 是
            // 我们要（重新）踢出的批次。
            if (!tc.is_terminal()) continue;
            // 缓存的 args_dump() 避免了每个 kick_pending_tools 上每个
            // 先前已解决的 调用 参数的 re-serializing（这会遍历每个工具 sub-turn
            // 的整个运行 — 原始 args.dump() 使其在运行过程中的时间复杂度为 O(N2)）。
            std::string key = tc.name.value + '\0'
                + (tc.args.is_null() ? std::string{} : tc.args_dump());
            const bool bad = tc.is_failed() || tc.is_rejected();
            if (bad) {
                failure_streaks[key].add(tc);
                success_key.clear();
                success.reset();
                continue;
            }
            failure_streaks[key].reset();
            const std::uint64_t out = output_hash(tc);
            if (key != success_key || out != success.last_output) {
                success_key = std::move(key);
                success.reset();
                success.last_output = out;
            }
            success.add(tc);
        }
    }

    // 达到极限的条纹会轻推一次，然后停止（#90）。参见 LoopBreak。
    auto judge = [](const std::string& key, const Streak& s,
                    std::string reason) -> std::optional<LoopBreak> {
        (void)key;
        // 已经被推动并且该模型自以下以来没有重复 调用：
        // 轻推正在做它的工作（它继续前进，或者正在回答）。
        if (s.nudged && !s.repeated_after_nudge) return std::nullopt;
        const bool stop = s.nudged || s.last_rejected;
        if (stop)
            reason = "Stopped: " + reason;
        else
            reason = std::string{kLoopNudgeMarker} + reason
                   + " If you repeat this exact call, the run will stop.";
        return LoopBreak{std::move(reason), stop, s.last_id};
    };
    auto tool_of = [](const std::string& key) {
        auto nul = key.find('\0');
        return nul == std::string::npos ? key : key.substr(0, nul);
    };

    for (const auto& [key, s] : failure_streaks) {
        if (s.count >= kRepeatLimit) {
            if (auto b = judge(key, s,
                "The `" + tool_of(key) + "` tool failed or was rejected "
                + std::to_string(s.count) + " consecutive times with the same "
                "arguments. Re-trying the identical call won't help — "
                "change the arguments (check the path/target exists, or pick a "
                "different tool), or answer the user directly with what you "
                "already know.")) return b;
        }
    }
    // 对调用进行相同的检查，保留SUCCEEDING。
    // 应用于 每个 模型，不仅仅是 id 启发式 调用
    // 弱：私有网关后面的不熟悉的本地 id 无法被构造识别，
    // 而这正是无界循环成本最高的部署（22GB 驻留 re
    // -running 相同的读取几分钟）。
    //
    // 有效的重复比失败的重复证据更弱，因此阈值更高
    // ——编辑文件后重新-reading是正常的。
    // 六个相同，成功，字节-for-byte 调用无文字答案则
    // 不行。
    // 连续六个 字节-identical 成功 调用，
    // 每个返回与最后一个所做的完全相同，中间没有文本答案，被卡住。
    if (success.count >= kSuccessRepeatLimit) {
        if (auto b = judge(success_key, success,
            "The `" + tool_of(success_key) + "` tool was called "
            + std::to_string(success.count) + " times in a row with identical "
            "arguments and the same result. You already have this output — "
            "answer the user with what you know, or take a different "
            "action.")) return b;
    }
    // ── 无步长 ─────────────────────────────────────────────────────
    //
    // 这里曾经有第三个触发器：25个工具在一次运行中轮流，没有文本答案，
    // 在`enforce_step_cap` 标志后面进行门控，
    // 这对于local/custom端点和weak-looking模型ID来说是正确的。
    //
    // 它消失了，因为STEP COUNT IS 不 EVIDENCE。
    // 另外两个大写字母关键在于重复——同样的 调用、
    // 字节-identical 参数，一遍又一遍——这实际上
    // 区分了卡住的模型和繁忙的模型。原始计数不能：搜索→读取→
    // 编辑→验证在大型 代码库 上运行合法地花费了数十个不同的、
    // 富有成效的步骤，并且以任意数量切断它会截断真正的工作，
    // 而没有任何发现可以指出。门因不一致而使情况变得更糟——
    // 相同的成绩单在 Claude 上很好，但在本地模型上停止了。
    //
    // 这与其他代理的做法相匹配：Claude代码的max_turns
    // 默认是无限制的，助手从不步进-caps。
    //
    // 仍然限制失控的是：上面的重复大写（通用）、
    // 上下文窗口以及用户点击Esc。真正循环通过
    // 真正的 不同 调用 的模型不是计数器应该猜测的
    // 东西 - 这是用户正在观看的东西。
    (void)tool_turns;
    return std::nullopt;
}

Cmd kick_pending_tools(Model& m) {
    if (m.d.current.messages.empty()) return Cmd::none();
    auto& last = m.d.current.messages.back();
    if (last.role != Role::Assistant) return Cmd::none();

    // 如果会话已经空闲，则提前退出。这是late
    // -arrival窗口：工具工作线程 线程可以返回
    // ToolExecOutput AFTER用户有已取消（Esc→
    // phase = Idle）或StreamError下降到Idle之后。
    // 在这些情况下，没有 in-flight 请求附加 sub-turn，
    // 没有 ctx 可以获取，结果无处可去。下面的相变是 TOTAL
    // （reschedule_streaming / 可选的
    // -checked 座位），因此滑过此防护会降级为 no-op
    // 而不是中止 - 但提前返回使意图显而易见并跳过无用的工作。
    //
    // 取消-time处的Pending/Approved的工具已经被CancelStream的拆卸循环标记为Failed/Rejected，
    // 因此无论如何都没有什么
    // 可以推进的了。
    if (m.s.is_idle()) return Cmd::none();

    // 将re-leaked抢救的调用（弱本地模型re
    // -emitting是他们本轮已运行的工具调用）BEFORE任何升级到运行，
    // 因此重复项永远不会执行第二个副作用。
    // 参见 dedup_releaked_salvage_calls。
    dedup_releaked_salvage_calls(m);

    std::vector<Cmd> cmds;
    bool any_pending = false;

    // 副作用-和路径-aware调度器。当助手一回合发射多个工具调用时，
    // 它们都启动待处理； maya 的 BG 工作线程 池 在独立的
    // 线程 上运行任务命令，因此我们在此周期中 分派 的任何工具集都会
    // 同时运行。调度决策（哪个 pending/approved
    // 调用 可以加入 in-flight 浪潮）由 PURE 规划器
    // `schedule_parallel_batch`（刚刚在上面定义，
    // 在 scheduler_path_test 中进行了单元
    // 测试）计算。将实时门保持为该规划器的瘦消费者意味着测试会执行
    // 真正的规则，而不是可能默默漂移的并行重新实现。
    //
    // 延迟（冲突）工具保留为待处理。当阻挡工具的ToolExecOutput落地时，
    // update.cpp re-fires kick_pending_tools和now
    // -unblocked兄弟姐妹前进。
    const auto plan = schedule_parallel_batch(last.tool_calls);
    std::vector<bool> promote(last.tool_calls.size(), false);
    for (auto i : plan.promote) promote[i] = true;

    // 权限预检故意是单独的、非-dispatching阶段。
    // 如果后面的同级需要批准，则在标记任何较早的同级之前返回。
    // 运行：返回 权限 提示词 会丢弃该函数累积的
    // Cmd，因此首先改变将永远搁置该工具。
    if (!m.d.pending_permission) {
        for (std::size_t i = 0; i < last.tool_calls.size(); ++i) {
            const auto& tc = last.tool_calls[i];
            // 只有为此波选择的调用可以是提示词。冲突的 调用
            // 停留在 待处理 直到活动波次完成；尽早提示他们
            // 会在可运行的工作之前重新排序 权限 UX。
            if (!promote[i] || !tc.is_pending()) continue;
            if (m.d.session_grants.contains(tc.name.value)
                || !tool::DynamicDispatch::needs_permission(tc.name.value, m.d.profile))
                continue;
            // 指纹用户即将成为SHOWN的定义，因此批准与该
            // 定义相关联而不仅仅是与名称相关联。在启动时再次
            // 验证（execute_approved）。
            const auto shown = tools::find(tc.name.value);
            m.d.pending_permission = PendingPermission{
                tc.id, tc.name,
                "Tool " + tc.name.value + " needs permission under "
                    + std::string{ui::profile_label(m.d.profile)} + " profile",
                shown ? shown->definition_hash() : 0};
            if (auto ctx = take_active_ctx(std::move(m.s.phase)); ctx)
                m.s.phase = phase::AwaitingPermission{std::move(*ctx)};
            else
                m.s.phase = phase::Idle{};   // 迟到：保持空闲
            return Cmd::none();
        }
    }

    for (std::size_t i = 0; i < last.tool_calls.size(); ++i) {
        auto& tc = last.tool_calls[i];
        // 已批准：用户已授予权限；就像 待处理
        // -but-no-permission-needed
        // 工具一样推进它，减去 权限 检查。让计划者成为日程
        // 安排的唯一事实来源。
        const bool ready = tc.is_pending() || tc.is_approved();
        if (ready) {
            const bool needs_perm = tc.is_approved()
                ? false
                : (!m.d.session_grants.contains(tc.name.value)
                   && tool::DynamicDispatch::needs_permission(tc.name.value, m.d.profile));
            if (needs_perm) {
                // 已显示针对该 调用 (或另一同级）的
                // 权限 请求。保留 待处理，不要 分派。
                any_pending = true;
                continue;
            }
            // Auto-allowed由政策或常设授权。
            // 记录做出决定时允许的内容；对于与启动同一
            // 时刻的这个分支，因此窗口为零，
            // 但记录它会使每个 分派 经过一次检查，
            // 而不是 调用 的两个形状。
            //
            // STANDING 授权 是不同的：它是较早给出的，
            // 可能是在之前的会话中，因此重要的散列是它
            // 所给出的散列——而不是名称现在解析的值。
            // 这就是记录它的全部意义。
            if (tc.approved_def_hash == 0) {
                if (auto g = m.d.grant_def_hash.find(tc.name.value);
                    g != m.d.grant_def_hash.end()) {
                    tc.approved_def_hash = g->second;
                } else if (const auto allowed = tools::find(tc.name.value)) {
                    tc.approved_def_hash = allowed->definition_hash();
                }
            }
            {
                // Effect/path-compatibility门：
                // 规划者决定该工具与in-flight波冲突（或者较早的
                // 兄弟推广了相同的蜱）。留下待处理；
                // kick_pending_tools re-fires
                // 在每个 终端 ToolExecOutput 上，
                // 因此延迟的同级会自动前进，无需显式重新排队。
                if (!promote[i]) {
                    any_pending = true;
                    continue;
                }

                // started_at被标记在StreamToolUseStart处，
                // 因此定时器覆盖整个卡生命周期(参数流式传输
                // +执行)。当我们开始跑步时，请保留它。
                //
                // executing_since在实际的分派
                // 处被标记为HERE，因为这是唯一知道工作何时
                // 真正开始的点——等待权限的工具已经过去了
                // started_at分钟，并且将该缺口计入新杀死
                // 的-approved工具的楔形预算（问题#40)。
                const auto seq = next_tool_exec_seq();
                tc.status = ToolUse::Running{tc.started_at(), {}, {},
                                             m.now,
                                             seq};
                auto cancel = active_ctx(m.s.phase)
                    ? active_ctx(m.s.phase)->cancel
                    : http::CancelTokenPtr{};
                // 只有`skill`工具读取活动集，
                // 因此只有它为导出活动集（可见转录本的扫描）付费。
                std::vector<std::string> active_skills;
                if (tc.name.value == "skill")
                    active_skills = tools::skills::active_in(::agentty::visible_text(m.d.current));
                cmds.push_back(run_tool(tc.id, tc.name, tc.args,
                                        std::move(cancel), seq,
                                        tc.approved_def_hash,
                                        std::move(active_skills),
                                        m.d.current.id.value));

                // Tool墙-clock 看门狗应用户请求被移除。
                // 现在，工具的运行时间与其 工作线程 一样长；
                // 用户通过 Esc 取消。  bash / 诊断仍然是
                // self-timeout 通过 subprocess.cpp。
                // 插入阻塞系统调用（缓慢的 NFS、
                // 死 FUSE 安装）的工具将使卡保持运行状态，
                // 直到 取消 — 明确接受此交易-off。

                // {流式传输、AwaitingPermission、
                // ExecutingTool}→ExecutingTool。
                // 来源是生成 now-running 工具的任何
                // 阶段：流式传输 表示模型刚刚发布的工具，
                // AwaitingPermission 表示用户刚刚
                // 授予的工具，ExecutingTool 表示同一批次
                // 中的同级升级。活跃的 ctx 流经不变。
                // ExecutingTool 不是 -Idle，
                // 因此 active() 自动打开：Tick 订阅保持武装状态，
                // 加载动画 前进，视图的实时运行计时器保持滴答声 — 没有
                // UI 在 long-running bash 命令上看起来冻结。
                if (auto ctx = take_active_ctx(std::move(m.s.phase)); ctx)
                    m.s.phase = phase::ExecutingTool{std::move(*ctx)};
                else
                    m.s.phase = phase::Idle{};   // 总计：永不中止
                any_pending = true;
            }
        } else if (tc.is_running()) {
            any_pending = true;
            // 现在数据流已经完成，SPECULATIVE read
            // -only工具（在StreamToolUseEnd的
            // -stream中期启动）仍在运行。采用它：如果没有这种转变，
            // 阶段在 StreamFinished 之后停留在 流式传输，
            // 并且 ToolExecOutput 防护装置（在 流式传输
            // 时抑制踢球）将永远阻止完成踢球——一个楔子。与上面的促销
            // 分支相同的 ctx 切换。
            if (m.s.is_streaming()) {
                if (auto ctx = take_active_ctx(std::move(m.s.phase)); ctx)
                    m.s.phase = phase::ExecutingTool{std::move(*ctx)};
                else
                    m.s.phase = phase::Idle{};   // 总计：永不中止
            }
        }
    }

    if (!any_pending) {
        const bool has_results = std::ranges::any_of(last.tool_calls, [](const auto& tc){
            return tc.is_terminal();
        });
        if (has_results) {
            // ── Doom-loop 断路器 ────────────────────────────────
            // 在花费另一个模型完成之前，检查该代理运行是否已经停止收敛。
            // 两个大写字母都在 REPETITION 上——
            // 同样的 调用 和 字节-identical 参数
            // ——因为这就是卡住模型和繁忙模型的区别：
            //
            //   ·REPEAT-FAILURE上限——相同的调用连续失败3次。这个
            //     是UNIVERSAL模式(助手的max_reflections=3，
            //     MindStudio的"2–3 attempts then stop")。
            //     应用于 每个 模型，包括 Claude：真正卡在死 调用（错误的路径，
            //     错误的工具）上的有能力的模型也能得到它的帮助。
            //
            //   ·SUCCESS-REPEAT帽-在一次运行中相同的调用 SUCCEEDED 6×。
            //     也通用。这就是在私有网关后面的本地模型上捕获
            //     80-repeat 循环的原因。
            //
            // 有NO步帽。工具轮次计数从来都不是任何证据——在大型
            // 代码库 上进行真正的搜索→读取→编辑→验证运行花费了
            // 数十个不同的、富有成效的步骤——并且将其门控在模型
            // -id 启发式上意味着相同的转录本在本地模型上停止并在
            // Claude 上运行良好。过去在这里计算的整个
            // `weak_step_cap` 选择器也随之而来。
            // 出于同样的原因，Claude 代码和辅助器都没有
            // 配备台阶盖。
            //
            // 首先行程 NUDGES (#90)：解释被
            // 附加到有问题的 调用 的结果中，并且运行继续，
            // 因此模型轮流改变路线，而不是运行进入空闲
            // 状态并看起来冻结，直到用户再次键入。
            // 在轻推（或拒绝条纹）STOPS之后仅重复相同的调用：
            // 当运行的最后一个助手转动并下降到空闲时显示文本。不管怎样，
            // 它都会载入史册，所以 follow-up 向模型展示
            // 了原因。
            if (auto brk = agent_loop_should_break(m.d.current.messages)) {
                bool nudged = false;
                if (!brk->stop) {
                    // 连胜的最新 调用 通常在 `last` 中，
                    // 但可以从末尾搜索整个记录以确保确定。
                    for (auto it = m.d.current.messages.rbegin();
                         it != m.d.current.messages.rend() && !nudged; ++it)
                    for (auto& tc : it->tool_calls) {
                        if (tc.id != brk->target) continue;
                        std::string* out = nullptr;
                        if (auto* d = std::get_if<ToolUse::Done>(&tc.status))
                            out = &d->output;
                        else if (auto* f = std::get_if<ToolUse::Failed>(&tc.status))
                            out = &f->output;
                        if (out) {
                            if (!out->empty()) *out += "\n\n";
                            *out += brk->reason;
                            nudged = true;
                        }
                        break;
                    }
                }
                if (!nudged) {
                    m.s.phase = phase::Idle{};
                    Message note;
                    note.role = Role::Assistant;
                    note.text = std::move(brk->reason);
                    m.d.current.messages.push_back(std::move(note));
                    cmds.push_back(detail::save_current(m));
                    return Cmd::batch(std::move(cmds));
                }
            }

            // 当前缀估计超过context_max的90%时，
            // 以用户-visible
            // "context limit reached — compacting before continuing…"（已达到上下文上限，继续前先进行压缩。）
            // toast的形式出现，将代理从multi-tool突发中拉出来。
            // 这严重破坏了工作流程：当模型重写摘要时，
            // 用户的代理循环将在 -task 中期“停止”。
            //
            // 现在：正常
            // -turn路径上的launch_stream的soft-trim从传输视图中丢弃最旧的原始转弯，
            // 直到它透明地适合context_max的~95%。
            // 代理继续运行。压缩变成严格的-better优化（
            // 摘要>截断），用户可以在闲暇时通过/compact调用，
            // 或者auto-trigger在NEXT用户提交上进行选择。
            // 工作流程不间断。
            //
            // 修剪是传输-only：转录本保持完整，
            // 如果上下文释放（e.g.在手动压缩之后），
            // 则丢弃的转弯会在后续请求中重新出现。

            // ExecutingTool→流式传输(后-tool子-turn)。
            // 活动 ctx 流经：取消 令牌仍然有效（请求仍然打开，
            // sub-turn 数据流 位于同一 SSE 上），
            // 保留 重试 计数器。
            //
            // 我们推送一个新的助手占位符，以便 传输-layer tool_use
            // ↔ tool_result 配对保持正确（传输层 为每个助手
            // 消息合成一个 User-with-tool_results 轮；
            // 将 sub-turns 折叠到同一消息中将在 tool_results
            // 之前交错新的 tool_use 块）。 VIEW 通过
            // build_live_tail（和 freeze_range）
            // 中的共享 `ui::turn_run_end` 帮助器将这些
            // sub-turn 消息折叠回一个视觉 Turn，
            // 匹配 agent_session 的
            // "one agent turn = one Turn" 形状。
            //
            // 这里没有冻结火灾：
            // mid-stream冻结将分裂in-flight助理运行穿过冻结/活动边界，
            // 产生两个视觉转弯，其中用户应该看到一个。
            // 一旦到达 `phase::Idle`，
            // 单个冻结站点就位于 `finalize_turn`。
            // Re-arm 停顿看门狗穿过工具边界。 ExecutingTool
            // 期间没有 SSE 事件 流，因此 last_event_at 与工具
            // 运行前的最后一个增量一样长 - 分钟，对于较长的 `bash`/
            // `gh run watch`。将其带入 流式传输 可以让蜱虫在新的 sub
            // -turn 的 StreamStarted 重置它之前着陆，发射虚假的
            // "stream stalled — no events for Ns"。（流式响应停滞，连续若干秒没有事件。）
            // sub-turn是新鲜的传输相；现在开始计时。
            if (!reschedule_streaming(m.s.phase, [&m](phase::Active& c) {
                    c.last_event_at = m.now;
                    c.retry         = retry::Fresh{};
                }))
                return Cmd::batch(std::move(cmds));   // 迟到
            Message placeholder;
            placeholder.role = Role::Assistant;
            m.d.current.messages.push_back(std::move(placeholder));
            cmds.push_back(launch_stream(m));
        } else {
            // ExecutingTool → 空闲（无继续）。
            // 活动的 ctx 被丢弃 - 请求完成，取消 令牌可以丢弃，
            // 重试 计数器在下一个用户回合时重置。 Idle 将
            // active() 降至 false，停止 Tick
            // 事件订阅。
            m.s.phase = phase::Idle{};
        }
    }
    return Cmd::batch(std::move(cmds));
}

// ── 自行-update 数据 ────────────────────────────────────────────────

Cmd check_for_update() {
    return Cmd::task([](maya::Sink<Msg> out, std::stop_token) {
        // 24h-cached;快速路径是读取一个小文件。
        // 错误会被吞掉——更新 NOTICE 绝不能以失败的形式出现。
        try {
            auto c = update::check_latest(/*力=*/false);
            // `update_available`现在意味着该平台上
            // 的INSTALLABLE(#119)：二进制程序仍在构建的
            // 版本永远不会点亮标签，因此TUI无法通告404秒的更新。
            if (c.error.empty() && c.update_available) {
                out.send(Msg{UpdateCheckDone{true, std::move(c.target),
                                             std::move(c.url)}});
                return;
            }
        } catch (const std::exception& e) {
            util::dbglog("update.check", e.what());
        } catch (...) {
            util::dbglog("update.check", "non-std exception");
        }
        out.send(Msg{UpdateCheckDone{}});
    });
}

Cmd perform_self_update(std::string version) {
    return Cmd::task([](maya::Sink<Msg> out, std::stop_token,
                        std::string version) {
        try {
            // 节流：每个块的HTTP层调用，
            // 这对于~15MB资产来说是数千次。分派每个更新
            // 都会淹没 UI 队列，并使更新本身成为屏幕上
            // 最慢的事情。 帧 是任何人都可以看到的最细粒度，
            // 因此合并到 ~10/s 并始终让最终的 调用
            // 通过（总计达到），以便条形图达到 100%。
            auto last = std::chrono::steady_clock::now()
                      - std::chrono::seconds{1};
            // 摘要骑在检查上，而不是消息：re-read它
            // （通常是缓存命中），因此安装的字节正是为此
            // 版本记录的字节 GitHub。
            std::string sha;
            if (const auto c = update::check_latest(/*力=*/false);
                c.error.empty() && c.target == version)
                sha = c.asset_sha256;
            auto err = update::perform_update(
                version,
                [&](std::size_t got, std::size_t total) {
                    const auto now = std::chrono::steady_clock::now();
                    const bool finished = total > 0 && got >= total;
                    if (!finished && now - last < std::chrono::milliseconds{100})
                        return;
                    last = now;
                    out.send(Msg{msg::MetaMsg{UpdateProgress{got, total}}});
                }, sha);
            if (err.empty())
                out.send(Msg{UpdateApplied{true, version}});
            else
                out.send(Msg{UpdateApplied{false, std::move(err)}});
        } catch (const std::exception& e) {
            out.send(Msg{UpdateApplied{false, e.what()}});
        } catch (...) {
            out.send(Msg{UpdateApplied{false, "unknown error"}});
        }
    }, std::move(version));
}

Cmd fetch_models(const Model& m) {
    // 从MODEL解析UI上的HERE、线程，并将值交给正文。
    // 相反，读取进程全局是交换机可能遇到的错误：状态更新函数
    // 更改 m.d.selection，全局仅在 归并
    // 之后发布，因此这将获取 旧 提供程序的 目录。
    return fetch_models(m.d.selection,
                        detail::active_provider_id(m));
}

Cmd fetch_models(provider::Selection sel,
                 std::string for_provider) {
    // 无捕获正文+按值的参数：jaal的规则(D7/D19)是
    // 任务正文捕获NOTHING，因此它读取的所有内容要么是参数，
    // 要么是它拥有的全局。捕获是 生命周期，您必须对另一个
    // 线程 进行推理；参数是 运行时 为您制作的副本。
    return cmd::io_task(
        [](Io io, const maya::unlocked& cap, maya::Sink<Msg> out, std::stop_token,
           provider::Selection sel, std::string for_provider) {
            const auto auth = auth_snapshot(io, sel);
            try {
                // 在 目录 落地之前确定 "ollama" 行是否
                // 真的讲 Ollama，因此窗口是从头开始读取的，
                // 而不是在第一个回合之后读取（#73）。每个主机缓存。
                if (sel.kind == provider::Kind::OpenAI)
                    (void)provider::openai::endpoint_speaks_native(io, sel.openai_endpoint);
                // ONE型号-list路由器(provider/selection.cpp)，
                // 在与数据流路径相同的轴上调度。
                // 这里没有 is_chatgpt/OpenAI/Anthropic
                // 梯子 - 新的提供者从其行继承其 目录。
                std::string err;
                auto models = provider::list_models_for(io, cap, sel, auth, err);
            // LOCAL 主机上的 空 目录 = 服务器没有应答（关闭、
            // 端口错误、路径错误） — list_models 在任何
            // HTTP 失败时返回 {}，而不是抛出异常。附上原因，
            // 以便 状态更新函数 在连接时烘烤它 IMMEDIATELY，
            // 而不是用户稍后发现的 选择面板 坐在静默的 "no models" 中。
            // 这是探针-on-connect：交换机总是获取，
            // 因此在Enter 的几秒钟内会调用损坏的主机。
            if (!err.empty()) {
                // 选择面板显示提供者自己的名字下的错误；
                // Toast（内置于 状态更新函数）添加主机。
            } else if (models.empty()) {
                if (sel.kind == provider::Kind::OpenAI
                    && !sel.openai_endpoint.use_tls)
                    err = "no response from " + sel.openai_endpoint.host + ":"
                        + std::to_string(sel.openai_endpoint.port)
                        + " \xe2\x80\x94 is the server running?";
                else if (sel.kind == provider::Kind::OpenAI)
                    err = sel.openai_endpoint.host + " listed no models";
            }
            // 型号目录是"which model ran"的另一半。
            // 空列表是 两者、custom-host Copilot
            // bug 和死机本地服务器如何存在的，因此将计数和原因
            // 记录在一起 — 它们在 UI 中是无法区分的。
            AGT_LOGL(Wire, models.empty() ? ::agentty::logx::Level::Warn
                                          : ::agentty::logx::Level::Info,
                     "models.loaded", "provider={} count={} err={}",
                     for_provider, models.size(),
                     err.empty() ? std::string{"-"} : err);
            out.send(Msg{ModelsLoaded{std::move(models), for_provider,
                                      std::move(err)}});
        } catch (const std::exception& e) {
            // 分派和EMPTY ModelsLoaded携带原因(NOT和
            // StreamError)，因此状态更新函数总是清除`models_loading`
            // 并且模型选择面板从"Loading models…"中退出到
            // 用户可以逃脱的"no models"状态。
            // 这里的 StreamError 将被路由到 LIVE TURN 的 重试 状态机：
            // 打开mid-stream（Ctrl+/未在turn_active上门控），
            // 失败的目录获取将弹出正在接收增量和竞态 a
            // RetryStream 工作线程的辅助消息到会话
            // 中——或者锁定健康转向终端。 状态更新函数 将
            // `error` 显示为短暂状态 toast。
            AGT_LOG(Wire, Warn, "models.failed", "provider={} err={}",
                    for_provider, e.what());
            out.send(Msg{ModelsLoaded{std::vector<ModelInfo>{}, for_provider,
                                      std::string{"models fetch: "} + e.what()}});
        } catch (...) {
            out.send(Msg{ModelsLoaded{std::vector<ModelInfo>{}, for_provider,
                                      "models fetch: unknown exception"}});
        }
        },
        std::move(sel), std::move(for_provider));
}

Cmd probe_model_window(const Model& m, std::string model_id) {
    const auto& sel = m.d.selection;
    // 只有本地OpenAI-compatible主机有我们下面可以更改的窗口（路
    // 由器交换型号）。真正的 Ollama 守护进程不是这条路线，
    // 而是在 "ollama" 行下服务的 llama.cpp 是（#73）。
    if (model_id.empty() || sel.kind != provider::Kind::OpenAI
        || provider::openai::endpoint_speaks_native_cached(sel.openai_endpoint)
               .value_or(sel.openai_endpoint.native_api)
        || !provider::openai::detail::is_local_endpoint(sel.openai_endpoint))
        return Cmd::none();
    return cmd::io_task(
        [](Io io, maya::Sink<Msg> out, std::stop_token,
           std::string model_id, provider::Selection sel,
           std::string for_provider) {
            const auto& endpoint = sel.openai_endpoint;
            const auto  auth     = auth_snapshot(io, sel);
            int w = 0;
            try {
                w = provider::openai::probe_loaded_window(io, auth, endpoint,
                                                          model_id);
            } catch (...) {}
            AGT_LOG(Wire, Info, "models.window_probe", "provider={} model={} window={}",
                    for_provider, model_id, w);
            out.send(Msg{ModelWindowProbed{for_provider, model_id, w}});
        },
        // 选择来自Model； 凭据 已在 工作线程 上
        // 解析（状态更新函数 无法读取存储）。
        std::move(model_id), sel, detail::active_provider_id(m));
}

Cmd fetch_models_for(std::string spec) {
    // 获取 任意 提供商的 目录 无需 切换到它 — 融合的 cross
    // -provider 选择面板 在打开时将其中一个扇出每个经过验证的提供商。
    // 从 `spec`（不是 active()）构建一个选择，因此路由器返回该
    // 后端的列表；调度 FusedCatalogLoaded，状态更新函数 将其
    // 合并到 Model::d.provider_catalogs（由
    // provider_id 保护，防止提供商在 -fetch 中期注销）。
    // list_models_for 会回退到空身份验证/无法访问的主机上的捆绑种子，
    // 因此即使在实时获取成功之前，对于托管提供商来说，这也是快速且非 -empty 的。
    return cmd::io_task([](Io io, const maya::unlocked& cap, maya::Sink<Msg> out, std::stop_token,
                           std::string spec) {
        try {
            auto sel = provider::parse_selection(spec);
            // 解析 凭据 为 此 规范 — 而不是 auth_snapshot()，
            // 后者解析为 活动 提供者。融合的 选择面板 将这些分散到每个 OTHER
            // 授权的提供商，因此 快照 将运送 e.g。 Anthropic 的
            // OAuth 承载到 api.mistral.ai → 401。
            const std::string pid =
                sel.kind == provider::Kind::OpenAI
                    ? sel.openai_endpoint.label
                    : std::string{provider::default_provider_id()};
            std::string err;
            auto models = provider::list_models_for(io, cap,
                sel, provider::credentials::resolve(io, pid), err);
            const bool ok = err.empty() || !models.empty();
            out.send(Msg{FusedCatalogLoaded{spec, std::move(models), ok,
                                            std::move(err)}});
        } catch (const std::exception& e) {
            out.send(Msg{FusedCatalogLoaded{spec, std::vector<ModelInfo>{}, false,
                                            e.what()}});
        } catch (...) {
            out.send(Msg{FusedCatalogLoaded{spec, std::vector<ModelInfo>{}, false, {}}});
        }
    }, std::move(spec));
}

Cmd open_browser_async(std::string url) {
    // task_isolated而不是任务：
    // posix_spawn/ShellExecute可以楔入挂起的WindowServer或奇怪的默认-opener。
    // 隔离的线程防止楔子耗尽共享的BG 池。
    return Cmd::task_isolated([](maya::Sink<Msg>, std::stop_token,
                                 std::string url) {
        // 不发送——状态更新函数不关心浏览器是否启动。
        // 如果默认开启器损坏，用户始终可以从 弹出
        // 面板 手动粘贴 auth_url。
        auth::open_browser(url);
    }, std::move(url));
}

Cmd mint_oauth_login() {
    return cmd::io_task([](Io io, maya::Sink<Msg> out, std::stop_token) {
        LoginOAuthMinted r;
        // 如果 CSPRNG 不可用，则 random_urlsafe 抛出。
        // 失败会进入 弹出面板 的失败状态，而不是铸造一个弱秘密。
        try {
            r.verifier      = auth::PkceVerifier{auth::random_urlsafe(io, 128)};
            r.state         = auth::OAuthState{auth::random_urlsafe(io, 32)};
            r.authorize_url = auth::oauth_authorize_url(r.verifier, r.state);
        } catch (const std::exception& e) {
            r = LoginOAuthMinted{};
            r.error = e.what();
        }
        out.send(Msg{msg::LoginMsg{std::move(r)}});
    });
}

Cmd oauth_exchange(auth::OAuthCode    code,
                        auth::PkceVerifier verifier,
                        auth::OAuthState   state) {
    return Cmd::task(
        [](maya::Sink<Msg> out, std::stop_token,
           auth::OAuthCode code, auth::PkceVerifier verifier,
           auth::OAuthState state) {
            try {
                auto r = auth::exchange_code(code, verifier, state);
                out.send(Msg{LoginExchanged{std::move(r)}});
            } catch (const std::exception& e) {
                out.send(Msg{LoginExchanged{std::unexpected(auth::OAuthError{
                    auth::OAuthErrorKind::Network,
                    std::string{"exchange threw: "} + e.what()})}});
            } catch (...) {
                out.send(Msg{LoginExchanged{std::unexpected(auth::OAuthError{
                    auth::OAuthErrorKind::Network,
                    "exchange threw: unknown exception"})}});
            }
        },
        std::move(code), std::move(verifier), std::move(state));
}

Cmd generate_thread_title(const Model& m) {
    // 第一用户消息和第一回复文本，经过修剪：足以命名聊天，
    // 发送便宜。与廉价的 "utility" 型号压实使用相同，
    // 因此它永远不会向旗舰机收费。
    constexpr std::size_t kMaxBytes = 2000;
    std::string user, reply;
    for (const auto& msg : m.d.current.messages) {
        if (msg.typed_by_user() && user.empty() && !msg.text.empty()) user = msg.text;
        else if (msg.role == Role::Assistant && !user.empty() && reply.empty() && !msg.text.empty())
            reply = msg.text;
        if (!user.empty() && !reply.empty()) break;
    }
    if (user.empty()) return Cmd::none();
    user.resize(tools::util::safe_utf8_cut(user, kMaxBytes));
    reply.resize(tools::util::safe_utf8_cut(reply, kMaxBytes));

    struct TitleInputs {
        ThreadId            id;
        std::string         model;
        std::string         user;
        std::string         reply;
        provider::Selection selection;
    };
    const std::string main_model = m.d.model_id.value;
    return cmd::io_task_isolated(
        [](Io io, const maya::unlocked& cap, maya::Sink<Msg> out, std::stop_token, TitleInputs in) {
            provider::Request req;
            req.model         = in.model;
            // 线程自己的会话，因此网关将标题发送到聊天
            // 已经登陆的后端（#118）。
            req.session_key   = in.id.value;
            // 几句话，但是忽略`quick`的思维模型在给出
            // 答案之前仍然需要思考的空间。
            req.max_tokens    = 1024;
            req.quick         = true;
            req.selection     = in.selection;
            req.auth          = auth_snapshot(io, in.selection);
            req.system_prompt =
                "You name chat conversations. Reply with only a title of 3 to 6 "
                "words that says what the user wants. No quotes, no trailing "
                "period, no emoji, no preamble.";
            Message ask;
            ask.role = Role::User;
            ask.text = "User:\n" + in.user +
                       (in.reply.empty() ? std::string{} : "\n\nAssistant:\n" + in.reply) +
                       "\n\nTitle:";
            req.messages.push_back(std::move(ask));

            std::string text;
            bool failed = false;
            bool cut_off = false;
            try {
                deps(io).stream(cap, std::move(req), [&](Msg ev) {
                    // Msg按域对事件进行分组；提供商 事件 是 StreamMsg。
                    const auto* s = std::get_if<msg::StreamMsg>(&ev);
                    if (!s) return;
                    if (const auto* d = std::get_if<StreamTextDelta>(s)) text += d->text;
                    else if (std::holds_alternative<StreamError>(*s)) failed = true;
                    else if (const auto* f = std::get_if<StreamFinished>(s))
                        cut_off = f->stop_reason == StopReason::MaxTokens;
                });
            } catch (...) { failed = true; }
            // 传输层的空
            // -turn占位符和被令牌限制切断的回复不是标题。
            if (cut_off || text == provider::openai::kEmptyTurnText) failed = true;
            AGT_LOG(Model, Debug, "thread.title", "id={} model={} ok={} cut_off={} bytes={}",
                    in.id.value, in.model, failed ? 0 : 1, cut_off ? 1 : 0, text.size());
            out.send(Msg{ThreadTitleGenerated{in.id, failed ? std::string{} : std::move(text)}});
        },
        TitleInputs{
            .id        = m.d.current.id,
            .model     = smart::utility_model(main_model, m.d.available_models, m.d.smart,
                                              detail::active_provider_id(m)),
            .user      = std::move(user),
            .reply     = std::move(reply),
            .selection = m.d.selection,
        });
}

Cmd load_threads_async() {
    // task_isolated 而不是任务：JSON 解析 在深度历史记录
    // 上可能需要几秒钟的时间（每个文件的目录遍历是 `directory_iterator`
    // + `nlohmann::json::parse`）。
    // 隔离它可以使共享的 工作线程 池 自由用于用户同时触发的
    // 数据流/工具任务。
    return cmd::io_task_isolated([](Io io, maya::Sink<Msg> out, std::stop_token) {
        try {
            auto threads = deps(io).load_threads();
            out.send(Msg{ThreadsLoaded{std::move(threads)}});
        } catch (...) {
            // Best-effort：损坏的文件已被
            // load_all_threads 记录并跳过，因此此处的任何抛出
            // 都是用户无法执行的操作。 分派 是一个空列表，因此 UI
            // 不会永远位于 "loading…" 上。
            out.send(Msg{ThreadsLoaded{std::vector<Thread>{}}});
        }
    });
}

Cmd load_library() {
    return cmd::io_task_isolated([](Io io, maya::Sink<Msg> out, std::stop_token) {
        out.send(Msg{LibraryLoaded{read_library(io)}});
    });
}

Cmd render_slash_prompt(std::string typed) {
    return Cmd::task_isolated(
        [](maya::Sink<Msg> out, std::stop_token, std::string line) {
            std::string text;
            bool usage = false;
#if AGENTTY_MCP
            if (auto r = mcp::mcp_expand_prompt(line, &usage)) text = std::move(*r);
#endif
            out.send(Msg{SlashPromptRendered{std::move(line), std::move(text), usage}});
        },
        std::move(typed));
}

Cmd load_plugins_async(bool reconnect) {
    // 将MCP连接快照、INTO和Model。 reconnect=true
    // 为 add/remove/toggle 和首次打开运行完整的重新加
    // 载（重生 + 握手，以桥的 15 秒截止时间为限）；重新连接=假只是
    // 快照已经-live 池。无论哪种方式，它都会分派
    // PluginsUpdated{plugin_model()}，
    // 因此 状态更新函数 将结果存储在 m.ui.plugins 中 - 视图读取 该，
    // 而不是全局 池。这是 Cmd→Msg 规则，使面板成为 Model 的纯功能。
    return Cmd::task_isolated(
        [](maya::Sink<Msg> out, std::stop_token, bool reconnect) {
            if (reconnect) {
                // 强制连接。在 COLD 启动时，还没有任何东西访问 注册表，因此 池 未
                // 构建 - 触摸 registry() 运行 connect_initial_mcp
                // () （生成 + 握手）一次。在 WARM 路径 (
                // add/remove/toggle) 上，池 已经存在，因此我们还运行
                // reload_mcp_plugins() 将 -sync 重新设置为编辑
                // 后的配置。顺序很重要：build-if-cold，然后重新加载。
                (void)tools::wire_tools();
                (void)tools::reload_mcp_plugins();
            }
            auto model = mcp::plugin_model();
            tools::tool_loading::annotate(model);   // 已加载 / 按需 (#98)
            out.send(Msg{PluginsUpdated{std::move(model)}});
        }, reconnect);
}

Cmd settings_add(settings::Category concern, std::string line) {
    return cmd::io_task(
        [](Io io, const maya::unlocked& cap, maya::Sink<Msg> out, std::stop_token, settings::Category c, std::string l) {
            const settings::AddResult r = (c == settings::Category::Plugins)
                ? settings::add_plugin_from_line(io, cap, l)
                : settings::create_starter(io, c, l);
            out.send(Msg{SettingsAddDone{c, r.ok, r.message}});
        }, concern, std::move(line));
}

Cmd read_rag_embed_status(std::uint64_t gen) {
    return cmd::io_task_isolated([](Io io, maya::Sink<Msg> out, std::stop_token, std::uint64_t g) {
        const auto st = tools::rag_embed_status(io);
        RagEmbedStatusRead r;
        r.gen        = g;
        r.ready      = st.state == tools::RagEmbedStatus::State::Ready;
        r.failed     = st.state == tools::RagEmbedStatus::State::Unavailable;
        r.dim        = st.dim;
        r.latency_ms = st.latency_ms;
        r.reason     = st.reason;
        out.send(Msg{std::move(r)});
    }, gen);
}

Cmd apply_rag_settings(store::RagConfig cfg) {
    // 隔离：嵌入器re-probe是可能永远不会返回的网络拨号，
    // 并且不得占用共享工作线程。 stop_token 是内核的，
    // 因此应用程序关闭时会到达它，而我们这边没有注册。
    return Cmd::task_isolated(
        [](maya::Sink<Msg>, std::stop_token st, store::RagConfig cfg) {
            if (st.stop_requested()) return;
            try {
                tools::rag_apply_settings_now(cfg);
            } catch (const std::exception& e) {
                util::dbglog("rag.apply_settings", e.what());
            } catch (...) {}
        },
        std::move(cfg));
}

Cmd edit_plugin(std::filesystem::path path, PluginEdited reply,
                tools::plugin::ServerSpec spec) {
    // 任务，不是task_isolated：一个小文件重写，
    // 有界，所以暖池工作线程是对的。 正文是无捕获的（jaal的任务契约）；
    // 一切都作为一个论点。
    //
    // 路径作为std::string交叉：jaal的可发送检查知道std::string拥有其字节，
    // 并且没有filesystem::path的条目。
    // 工作线程 重建路径，无论哪种方式都是相同的值。
    return cmd::io_task(
        [](Io io, const maya::unlocked& cap, maya::Sink<Msg> out, std::stop_token,
           std::string path_str, PluginEdited r, tools::plugin::ServerSpec spec) {
            const std::filesystem::path path{path_str};
            namespace pl = tools::plugin;
            using K = PluginEdited::Kind;
            auto record = [&r](pl::EditResult res) {
                r.ok             = (res == pl::EditResult::Ok);
                r.already_exists = (res == pl::EditResult::AlreadyExists);
                r.parse_error    = (res == pl::EditResult::ParseError);
            };
            try {
                switch (r.kind) {
                case K::SetServerDisabled:
                    record(pl::set_server_disabled(io, cap, path, r.server, r.flag));
                    break;
                case K::SetToolEnabled:
                    record(pl::set_tool_enabled(io, cap, path, r.server, r.tool, r.flag));
                    // 工具的启用状态被烘焙到缓存的 MCP 目录 中，
                    // 因此 缓存 必须将其丢弃 — 这里，
                    // 在 工作线程 上，就在它所依赖的写入之后，
                    // 而不是作为来自 状态更新函数 的第二次全局戳。
                    if (r.ok) tools::invalidate_mcp_catalog(io);
                    break;
                case K::SetToolPinned:
                    record(pl::set_tool_pinned(io, cap, path, r.server, r.tool, r.flag));
                    // 销在投影处被烘烤到ToolDef (always_expose)中，
                    // 因此目录必须重新-project。
                    if (r.ok) tools::invalidate_mcp_catalog(io);
                    break;
                case K::Approve:
                    r.ok = pl::approve_server(io, cap, path, r.server);
                    break;
                case K::Remove:
                    record(pl::remove_server(io, cap, path, r.server));
                    break;
                case K::Add:
                    record(pl::add_server(io, cap, path, spec, /*力=*/false));
                    break;
                case K::Update:
                    record(pl::update_server(io, cap, path, spec));
                    break;
                }
            } catch (...) {
                // 抛出报告为失败写入的编辑：状态更新函数显示其
                // "could not write"路径，
                // 而不是挂在永远不会到来的答复上。
                r.ok = false;
            }
            out.send(Msg{std::move(r)});
        },
        path.string(), std::move(reply), std::move(spec));
}

Cmd write_fork_transcript(Thread parent, fork_panel::Choice choice) {
    return cmd::io_task(
        [](Io io, maya::Sink<Msg> out, std::stop_token, Thread t,
           fork_panel::Choice choice) {
            ForkTranscriptWritten r{.parent_id = t.id.value, .choice = choice};
            try {
                r.path = persistence::write_thread_transcript_md(io, t).string();
            } catch (...) {
                r.path.clear();
            }
            // 转录本位于~/.agentty/threads下，
            // 在读取工具被沙箱化到的工作区之外。仅打开该文件（
            // mcp-cpp 工具会看到相同的读取根），
            // 以便模型可以读取分叉注释所指向的内容，而不会读取其他内容。
            if (!r.path.empty())
                tools::util::allow_fork_transcripts(io, persistence::threads_dir(), {r.path});
            out.send(Msg{msg::ForkMsg{std::move(r)}});
        },
        std::move(parent), choice);
}

Cmd prewarm_provider(const Model& m) {
    provider::PrewarmTarget t = provider::prewarm_target(m.d.selection);
    if (!t.should_warm()) return Cmd::none();   // 没有副作用来描述
    // 普通任务，不是孤立的：prewarm()仅将POSTS拨到
    // http客户端自己的池并返回，因此这占用了工作线程微秒。
    // 表盘的 生命周期 是客户的，在拆卸时加入了自己的
    // join_prewarm()。
    return cmd::io_task(
        [](Io io, maya::Sink<Msg>, std::stop_token st, provider::PrewarmTarget t) {
            if (st.stop_requested()) return;
            http::default_client(io).prewarm(t.host, t.port, t.override_host,
                                           t.override_port);
        },
        std::move(t));
}

Cmd backfill_summaries_async(std::vector<ThreadId> ids) {
    return cmd::io_task_isolated(
        [](Io io, maya::Sink<Msg> out, std::stop_token stop, std::vector<ThreadId> ids) {
            std::vector<Thread> done;
            try {
                if (!stop.stop_requested() && deps(io).backfill_summaries)
                    done = deps(io).backfill_summaries(ids);
            } catch (...) {}
            out.send(Msg{ThreadSummariesFilled{std::move(done)}});
        },
        std::move(ids));
}

Cmd load_thread_peek_async(ThreadId id) {
    return cmd::io_task_isolated(
        [](Io io, maya::Sink<Msg> out, std::stop_token, ThreadId id) {
            std::optional<std::vector<ui::PeekTurn>> turns;
            try {
                if (auto t = deps(io).load_thread(id)) turns = ui::peek_turns_of(*t);
            } catch (...) {}
            out.send(Msg{ThreadPeekLoaded{std::move(id), std::move(turns)}});
        },
        std::move(id));
}

Cmd load_foreign_sessions_async(foreign::Scan previous) {
    return cmd::io_task_isolated(
        [](Io io, maya::Sink<Msg> out, std::stop_token, foreign::Scan prev) {
            foreign::Scan s;
            try { s = foreign::list_sessions(io, prev); } catch (...) {}
            out.send(Msg{ForeignSessionsLoaded{std::move(s)}});
        },
        std::move(previous));
}

Cmd open_foreign_async(ThreadId id, Thread::Import im) {
    return cmd::io_task_isolated(
        [](Io io, maya::Sink<Msg> out, std::stop_token, ThreadId id, Thread::Import im) {
            std::optional<Thread> keep;
            try { keep = deps(io).load_thread(id); } catch (...) {}
            std::optional<Thread> fresh;
            try { fresh = foreign::read_session(io, im); } catch (...) {}
            if (!fresh) {
                // 文件已消失或无法读取：打开之前导入的内容（
                // 如果有的话），因此用户仍然获得他们的 线程。
                out.send(Msg{ThreadLoaded{keep ? std::move(*keep) : Thread{}}});
                return;
            }
            if (keep && keep->imported && fresh->id == keep->id) {
                // 自导入以来未改变：没有要合并的内容。
                if (keep->imported->size == fresh->imported->size
                    && keep->imported->mtime == fresh->imported->mtime) {
                    out.send(Msg{ThreadLoaded{std::move(*keep)}});
                    return;
                }
                // Re-read之后在其他工具中进行了更多的说明。自导入保留在
                // 末尾以来，在 agentty 中添加了轮次。
                const std::size_t from = std::min<std::size_t>(keep->imported->messages,
                                                               keep->messages.size());
                const std::size_t base = fresh->messages.size();
                for (std::size_t i = from; i < keep->messages.size(); ++i)
                    fresh->messages.push_back(std::move(keep->messages[i]));
                for (const auto& rec : keep->compactions)
                    if (rec.up_to_index > from) {
                        auto r = rec;
                        r.up_to_index = r.up_to_index - from + base;
                        if (r.up_to_index <= fresh->messages.size())
                            fresh->compactions.push_back(std::move(r));
                    }
                if (keep->user_named()) {
                    fresh->title        = keep->title;
                    fresh->renamed_at   = keep->renamed_at;
                    fresh->name_is_auto = keep->name_is_auto;
                }
                fresh->created_at        = keep->created_at;
                fresh->rag_mode_override = keep->rag_mode_override;
                persistence::summarise_for_picker(fresh->messages, *fresh);
                if (keep->updated_at > fresh->updated_at) fresh->updated_at = keep->updated_at;
            }
            deps(io).save_thread(*fresh);
            out.send(Msg{ThreadLoaded{std::move(*fresh)}});
        },
        std::move(id), std::move(im));
}

Cmd load_foreign_peek_async(ThreadId id, Thread::Import im) {
    return cmd::io_task_isolated(
        [](Io io, maya::Sink<Msg> out, std::stop_token, ThreadId id, Thread::Import im) {
            std::optional<std::vector<ui::PeekTurn>> turns;
            try {
                if (auto t = foreign::read_session(io, im)) turns = ui::peek_turns_of(*t);
            } catch (...) {}
            out.send(Msg{ThreadPeekLoaded{std::move(id), std::move(turns)}});
        },
        std::move(id), std::move(im));
}

Cmd load_thread_async(ThreadId id) {
    // task_isolated 而不是任务：单个大 线程（多-MB，
    // 数百条消息）仍然需要 20-50 毫秒的同步 解析，
    // 足够小以保持在 工作线程 池 上 - 但隔离匹配
    // load_threads_async 策略，并使 per-thread
    // 解析 远离 tools/stream 竞争的同一个 池。
    return cmd::io_task_isolated(
        [](Io io, maya::Sink<Msg> out, std::stop_token, ThreadId id) {
            try {
                auto loaded = deps(io).load_thread(id);
                if (loaded) {
                    // 重新打开的分叉仍然指向工作区之外的其父代
                    // 的转录本；分叉时间的 授权 随该会话
                    // 一起死亡（#83）。
                    std::vector<std::string> transcripts;
                    for (const auto& msg : loaded->messages)
                        if (msg.fork_note && !msg.fork_transcript.empty())
                            transcripts.push_back(msg.fork_transcript);
                    if (!transcripts.empty())
                        tools::util::allow_fork_transcripts(
                            io, persistence::threads_dir(), transcripts);
                    out.send(Msg{ThreadLoaded{std::move(*loaded)}});
                } else {
                    // 磁盘读取/解析失败：表面为空的线程，
                    // 因此状态更新函数清除`thread_loading`
                    // 并且UI不会卡在加载动画上。
                    // 状态更新函数检测到空ThreadId==无交换。
                    out.send(Msg{ThreadLoaded{Thread{}}});
                }
            } catch (...) {
                out.send(Msg{ThreadLoaded{Thread{}}});
            }
        }, std::move(id));
}

std::uint64_t next_codex_login_attempt_id() noexcept {
    static std::atomic_uint64_t next{0};
    return next.fetch_add(1, std::memory_order_relaxed) + 1;
}

Cmd probe_host_async(std::string spec, std::uint64_t attempt_id) {
    // 在工作线程上连接-probe自定义主机：拨打其型号列表（配置路径→/
    // v1/models→Ollama/api/tags）并报告DETECTED方言。
    // 以 probe_host 自己的 3s/6s 超时 为界，因此 弹出面板
    // 的 "probing…" 状态无论哪种方式都会快速解析。
    return cmd::io_task([](Io io, maya::Sink<Msg> out, std::stop_token,
                        std::string spec, std::uint64_t attempt_id) {
        HostProbed r;
        r.attempt_id = attempt_id;
        r.spec       = spec;
        try {
            const auto sel = provider::parse_selection(spec);
            const auto auth = provider::credentials::resolve(io, spec);
            const auto probe =
                provider::openai::probe_host(io, auth, sel.openai_endpoint);
            using D = provider::openai::HostProbe::Dialect;
            if (probe.dialect != D::None) {
                r.ok          = true;
                r.models_path = probe.models_path;
                r.native_api  = probe.dialect == D::OllamaNative;
                r.model_count = probe.model_count;
                r.latency_ms  = probe.latency_ms;

                // 这位主持人是否告诉我们一个真实的上下文窗口？
                //
                // list_models运行包括运行时探测在内的整个梯子，
                // 因此在这里询问会在刚刚在毫秒内应答的主机上再花费一个
                // 请求——并且它将add-host toast从
                // "connected"变成"connected, and the window is 8k"，
                // 这就是决定长会话是否有效
                // 的事实。
                //
                // 列出的型号中最大的窗口是正确的摘要：Toast
                // 命名了该主机可以执行的操作，并且 per
                // -model 编号仍然显示在 选择面板 中。
                //
                // ADD TIME IS DIFFERENT来自
                // 例行刷新，因此这里绕过地址启发式：force_probe
                // 询问运行时路由，无论主机看起来是什么样子。
                // 这是用户坐在 弹出面板 前面等待有关
                // 此 主机的答案的时刻，因此额外的几
                // 毫秒是可以承受的，因为它们不是在每次后台
                // 刷新时都可以承受的——另一种选择是向
                // 他们询问一个服务器可以自行回答的问题。
                auto models = provider::openai::list_models(
                    io, auth, sel.openai_endpoint, /*force_probe=*/true);
                for (const auto& mi : models)
                    if (mi.context_window > r.window_tokens)
                        r.window_tokens = mi.context_window;

                // 应答运行时路由IS self-hosted的主机-/props、
                // /api/ps、
                // loaded_instances和meta.n_ctx不是托管的API服务的东西。
                // 因此，当探测在启发式排除的地址上工作时，
                // 请记住主机：以后每次刷新也会探测它，
                // 而无需用户配置任何内容。
                //
                // 这取代了设置切换。
                // 服务器已经告诉我们它是什么；
                // 要求用户确认就等于要求他们重复我们的答案。
                const bool looks_local =
                    provider::openai::detail::is_local_endpoint(sel.openai_endpoint);
                if (r.window_tokens > 0 && !looks_local)
                    r.learned_self_hosted = sel.openai_endpoint.host;
                // 仍然什么都没有，我们通常不会问：主机是可到达的，
                // 但对其窗口AND保持沉默，看起来不像self-hosted。
                // 没有什么需要记住的，也没有 UI 可以自行
                // 解决的问题。
                r.probe_skipped = r.window_tokens <= 0 && !looks_local;
            } else {
                // 每个故障模式一句话，来自探测器自身的分类法(HostProbe::explain)。
                // 三个截然不同的错误曾经作为相同无用的字符串到达​​这里
                // - 特别是 200 + HTML 响应，这是您通过粘贴
                // 仪表板 URL 而不是 API 基础得到的结果，
                // 并且用于报告
                // "HTTP 200 — no model list at any known path"。（HTTP 返回 200，但所有已知路径都没有模型列表。）
                r.error = probe.explain();
                r.needs_key =
                    probe.failure == provider::openai::HostProbe::Failure::NeedsKey;
            }
        } catch (const std::exception& e) {
            r.error = e.what();
        } catch (...) {
            r.error = "probe failed";
        }
        out.send(Msg{std::move(r)});
    }, std::move(spec), attempt_id);
}

Sub device_login_sub(std::string provider, std::string provider_label,
                     std::uint64_t attempt_id) {
    // 原生OAuth设备流，提供商-generic。请求 one-time 代码（
    // 通过 DeviceCodeReady 发送到 弹出面板），然后阻止 -polls
    // 直到用户批准。
    //
    // A STREAM，重点是尝试。
    // jaal保持一次运行存活subscribe()不断返回这个键，
    // 并触发stop_token就在它停止的那一刻——这正是
    // DeviceWaiting状态消失（Esc、成功或更新的尝试）。
    // 因此取消不需要标记，也不需要簿记：不要求订阅IS取消。
    //
    // 每个数据流专用线程，这正是其所需要的：只要用户占用，
    // login()就会阻塞。
    return Sub::stream(
        "login/device/" + provider + "/" + std::to_string(attempt_id),
        [](const maya::unlocked& cap, maya::Sink<Msg> out, std::stop_token stop, std::string provider,
           std::string provider_label, std::uint64_t attempt_id) {
        const std::stop_token cancelled = stop;
        auto emit_code = [&](std::string bare_url, std::string browser_url,
                             std::string user_code) {
            out.send(Msg{DeviceCodeReady{
                .provider = provider,
                .attempt_id = attempt_id,
                .verification_url = std::move(bare_url),
                .browser_url = std::move(browser_url),
                .user_code = std::move(user_code),
            }});
        };
        auto done = [&](std::optional<std::string> error) {
            out.send(Msg{DeviceLoginDone{
                .provider = provider,
                .provider_label = provider_label,
                .attempt_id = attempt_id,
                .error = std::move(error),
            }});
        };
        try {
            // 每个提供商的login()共享相同的形状（超时、device
            // -code接收器、取消-probe)，但是其自己的DeviceCode类型；
            // 将其映射到通用 分派。 login() 在成功时保留令牌，
            // 因此我们仅将 success-or-error 转发到 状态更新函数。
            //
            // 这是合法地在注册表迁移中幸存下来的少数提供者分支之一：
            // 两个臂调用 DIFFERENT函数返回DIFFERENT类型，
            // 并且每个渲染其代码URL不同（GitHub显示裸URL
            // +代码；Kimi想要pre-filled？
            // user_code = 链接所以SSH 用户可以粘贴一件事）。
            // 注册表 标志可以说"this provider does device login"
            // ——它不能消除类型差异。擦除它需要在行上有一个
            // DeviceLoginFn 接口边界，这值得在三个提供商处执行，
            // 而不是两个。
            std::optional<std::string> err;
            if (provider == "copilot") {
                auto r = provider::copilot::login(cap, 900,
                    [&](const provider::copilot::DeviceCode& c) {
                        // GitHub返回一URL(github.com/login/device)；
                        // 用户在那里输入代码。
                        emit_code(c.verification_uri, c.verification_uri, c.user_code);
                    }, cancelled);
                if (!r) err = r.error().render();
            } else if (provider == "kimi") {
                auto r = provider::kimi::login(cap, 900,
                    [&](const provider::kimi::DeviceCode& c) {
                        // 显示+打开PRE-FILLED url(…？
                        // user_code=XXXX)，使得在SSH/headless上，
                        // 用户手动打开的url已经带有该代码。 kimi.ai 的
                        // 批准页面从查询字符串中读取 user_code。
                        const std::string complete =
                            c.verification_uri_complete.empty() ? c.verification_uri
                                                                : c.verification_uri_complete;
                        emit_code(complete, complete, c.user_code);
                    }, cancelled);
                if (!r) err = r.error().render();
            } else {
                err = "unknown device-login provider: " + provider;
            }
            done(std::move(err));
        } catch (const std::exception& e) {
            done(provider + " login threw: " + e.what());
        } catch (...) {
            done(provider + " login threw");
        }
    },
        std::move(provider), std::move(provider_label), attempt_id);
}

Sub codex_login_sub(std::uint64_t attempt_id) {
    // 与 device_login_sub 形状相同：有键的 数据流，所以 Esc 阻止它。
    // 当用户登录时，OAuth 模式会被阻止。
    return cmd::io_stream(
        "login/codex/" + std::to_string(attempt_id),
        [](Io io, const maya::unlocked& cap, maya::Sink<Msg> out, std::stop_token stop,
           std::uint64_t attempt_id) {
        const std::stop_token cancelled = stop;
        try {
            auto r = provider::chatgpt::codex_login(
                io, cap, 900, [attempt_id, out](
                         const provider::chatgpt::CodexDeviceCode& code) {
                    out.send(Msg{CodexDeviceCodeReady{
                        .attempt_id = attempt_id,
                        .verification_url = code.verification_url,
                        .user_code = code.user_code,
                    }});
                }, cancelled);
            out.send(Msg{CodexLoginDone{
                .attempt_id = attempt_id,
                .result = std::move(r),
            }});
        } catch (const std::exception& e) {
            out.send(Msg{CodexLoginDone{
                .attempt_id = attempt_id,
                .result = std::unexpected(auth::OAuthError{
                    auth::OAuthErrorKind::Network,
                    std::string{"ChatGPT login threw: "} + e.what()}),
            }});
        } catch (...) {
            out.send(Msg{CodexLoginDone{
                .attempt_id = attempt_id,
                .result = std::unexpected(auth::OAuthError{
                    auth::OAuthErrorKind::Network,
                    "ChatGPT login threw: unknown exception"}),
            }});
        }
    }, attempt_id);
}

namespace {
// 运行锁定刷新，将其变为Msg。之后，
// re-check该商店仍然承载着我们得到的令牌：帐户切换
// 可以替换工作线程 已保存之后的商店，并且安装旧帐户的承载
// 将跨越-wire它们。该病例报告已取代，状态更新函数
// 将其视为良性。
TokenRefreshed run_refresh(Io io, const maya::unlocked& cap, const std::string& refresh_token) {
    try {
        auto r = auth::refresh_access_token_locked(io, cap, auth::RefreshToken{refresh_token});
        if (r) {
            auto on_disk = auth::load_credentials(io);
            const auto* o = on_disk ? std::get_if<auth::cred::OAuth>(&*on_disk)
                                    : nullptr;
            if (!o || o->access_token != r->access_token)
                return TokenRefreshed{std::unexpected(auth::OAuthError{
                    auth::OAuthErrorKind::Superseded,
                    "store no longer carries the refreshed token"})};
        }
        return TokenRefreshed{std::move(r)};
    } catch (const std::exception& e) {
        return TokenRefreshed{std::unexpected(auth::OAuthError{
            auth::OAuthErrorKind::Network,
            std::string{"refresh threw: "} + e.what()})};
    } catch (...) {
        return TokenRefreshed{std::unexpected(auth::OAuthError{
            auth::OAuthErrorKind::Network,
            "refresh threw: unknown exception"})};
    }
}
}  // 命名空间结束。

Cmd refresh_oauth_if_due() {
    return cmd::io_task([](Io io, const maya::unlocked& cap, maya::Sink<Msg> out, std::stop_token) {
        std::optional<std::string> tok;
        try { tok = auth::oauth_proactive_refresh_token(io); } catch (...) {}
        if (!tok) {
            out.send(Msg{msg::LoginMsg{OAuthRefreshNotDue{}}});
            return;
        }
        out.send(Msg{run_refresh(io, cap, *tok)});
    });
}

Cmd refresh_oauth_for_401() {
    return cmd::io_task([](Io io, const maya::unlocked& cap, maya::Sink<Msg> out, std::stop_token) {
        std::string rt;
        try {
            if (auto loaded = auth::load_credentials(io))
                if (auto* o = std::get_if<auth::cred::OAuth>(&*loaded))
                    rt = o->refresh_token;
        } catch (...) {}
        if (rt.empty()) {
            // API密钥或环境令牌：没有什么可刷新的。
            out.send(Msg{TokenRefreshed{std::unexpected(auth::OAuthError{
                auth::OAuthErrorKind::MissingToken,
                "no refresh token stored; sign in again with /login"})}});
            return;
        }
        out.send(Msg{run_refresh(io, cap, rt)});
    });
}

Cmd refresh_oauth(std::string refresh_token) {
    return cmd::io_task(
        [](Io io, const maya::unlocked& cap, maya::Sink<Msg> out, std::stop_token,
           std::string refresh_token) {
            out.send(Msg{run_refresh(io, cap, refresh_token)});
        }, std::move(refresh_token));
}

} // namespace agentty::app::cmd

namespace agentty {
void end_turn(Phase& p) noexcept {
    if (auto* a = active_ctx(p); a && a->cancel) a->cancel->cancel();
    p = phase::Idle{};
}
} // namespace agentty
