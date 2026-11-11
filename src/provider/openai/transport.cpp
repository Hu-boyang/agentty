// agentty::provider::openai —OpenAI-compatible 聊天完成 传输层。
//
// 镜像Anthropic 传输层的结构(SSE 解析器 → StreamCtx
// → 分派 → 相同 agentty消息)，但讲的是 OpenAI 传输 格式：
//
//   POST {base}/v1/chat/completions   {model, messages, tools, stream:true}
//   SSE: data: {"choices":[{"delta":{...},"finish_reason":...}], "usage":...}
//   data: [DONE]   终止数据流。
//
// 与Anthropic相比最难的部分是工具-call 流式传输形状。 OpenAI 数据流
// `delta.tool_calls: [{index, id, function:{name, arguments}}]` 其中：
//   ·给定`index`的第一个增量携带id+function.name，
//   ·同一指数的后续增量带有`function.arguments`
//     片段（部分JSON字符串）并省略id/name，
//   • 没有显式的 "tool call done" 事件 — 当
//     NEW索引出现，或者当数据流完成/finish_reason到达时。
//
// 我们将其转化为agentty的块模型：第一眼看到索引时StreamToolUseStart，
// 每个参数片段StreamToolUseDelta，
// 以及当索引被取代或数据流结束时StreamToolUseEnd。

#include "agentty/provider/openai/transport.hpp"
#include "agentty/domain/reasoning_off.hpp"
#include "agentty/version.hpp"

#include <maya/runtime.hpp>
// 观察表：这个spec-less 传输以各种方式拼写场存在于那里，而不是这里。
// 请参阅 lens.hpp 了解为什么使用镜头而不是 acp::Codec。
#include "agentty/provider/openai/dialect.hpp"

#include <map>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <array>
#include <chrono>
#include <format>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>
#include <simdjson.h>

#include "agentty/provider/debug.hpp"   // 传输转储→logx `wire`通道
#include "agentty/provider/dialect.hpp"  // 聊天-vs-Responses路由(SSOT)
#include "agentty/provider/openai/responses_site.hpp"
#include "agentty/provider/registry.hpp" // 端点列：from_spec的SSOT
#include "agentty/provider/error_class.hpp" // OptionalField：习得的拒绝（#93）
#include "agentty/provider/stream_epilogue.hpp"
#include "agentty/provider/stream_scaffold.hpp"
#include "agentty/provider/usage.hpp"
#include "agentty/provider/msg_shared.hpp"
#include "agentty/provider/wire.hpp"
#include "agentty/provider/wire/streamed.hpp"
#include "agentty/provider/wire/tool_calls.hpp"
#include "agentty/provider/wire_supersede.hpp"
#include "agentty/runtime/composer_attachment.hpp"
#include "agentty/tool/util/fs_helpers.hpp"   // util::workspace_root/project_root — AGENTS.md锚定+步行开始
#include "agentty/util/base64.hpp"
#include "agentty/util/dbglog.hpp"

namespace agentty::provider::openai {

using json = nlohmann::json;

namespace {

// 从 OpenRouter 捕获的标签 reasoning_details，因此它们仅被发送回
// OpenRouter (Message::reasoning_site)。
constexpr std::string_view kOpenRouterReasoningSite = "openrouter";

// ── UTF -8 擦洗(与 Anthropic 传输层) ───────────────────
// 工具输出或粘贴的斑点可以携带无效的UTF-8； nlohmann 的
// dump() 扔在上面。将每个格式错误的 字节 替换为 U+FFFD，
// 这样请求就会生成，而不是回合因 JSON type_error 而终止。
// 共享严格实现（拒绝超长编码+代理）——参见
// wire::scrub_utf8。
using wire::scrub_utf8;

// ── 人数 -tool-call 流式传输 累积器 ─────────────────────────────────────
// 每个 OpenAI tool_calls[].index 一个槽。我们需要记住我们在开放
// 增量上看到的 id+name，以便 StreamToolUseEnd/抢救路径有它们。
struct ToolCallSlot {
    std::string id;
    std::string name;
    // 到目前为止，该 调用 的所有内容均已解码。
    // `delta.tool_calls[].function .arguments`
    // USUALLY 携带片段，但合并代理可能会在每个块中重复 COMPLETE 参数
    // - 盲连接会将其变成 `{"a":1}{"a":1}`，这会像 "invalid args"
    // 一样失败，就像响应 `.done` 错误一样。保留运行值可以让
    // unseen() 识别重复并且不发出任何信号。
    std::string args;
    bool started = false;   // StreamToolUseStart 已发出
    bool ended   = false;   // StreamToolUseEnd 已发出
};

struct StreamCtx {
    EventSink sink;
    // Model id，仅诊断——标记 Model-channel 日志行，
    // 因此 salvage/drop 事件 命名产生它的型号。
    std::string model_id;

    // 镜子req.show_reasoning。现在只有米斯特拉尔思维-PARTS路径读取它；
    // 普通 reasoning_content/reasoning 始终会被捕获，
    // 因为下一回合会将其发送回来（请参阅 build_messages）。
    // 默认为 true，因此测试工具保持捕获语义。
    bool show_reasoning = true;

    // 字节-level 数据流框架存在于共享传输帮助器中（
    // 参见include/agentty/provider/wire.hpp)。每个请求只驱动一个：
    // `sse`为/v1/chat/completions兼容路径，`ndjson`
    // 为Ollama的原生/api/chat.，两者都有自己的缓冲区+读游标+压缩；
    // 传输层 仅供应 per-event/per-line 分派。
    wire::SseFramer  sse;
    wire::LineFramer ndjson;

    // 将simdjson点播解析器重新用于内容-delta快速路径（参见
    // dispatch_data_fast)。有状态 — 缓存 划过 iterate() 调用，
    // 因此 高频路径 避免了每个 SSE 帧 的 malloc。
    simdjson::ondemand::parser simd_parser;

    // OpenRouter的`reasoning_details`
    // (Gemini思想签名、加密推理、总结)由`index`跨增量合并。
    // 发送回下一个请求，以便模型在工具 调用 上保持其思想链。
    json reasoning_details = json::array();
    // Per-call `extra_content` (Gemini思想签名)，由调用 id。
    std::map<std::string, json, std::less<>> tool_extra;
    // 对于在 调用 的 ID 之前到达的签名，
    // 块被归因于最后的 调用。
    json pending_extra;

    // Tool-call 流式传输状态。归因（哪个块属于哪个 调用）位于
    // wire::ToolCallTracker 中 - 它联合键控（id，
    // 索引），因为单独的方法在提供者之间都不可靠，并且它是在没有任何 SSE
    // 管道的情况下进行单元测试的。 调用可能会交错，因此每个都有自己的生命周期；
    // 没有提供商-global "active" 调用。
    wire::ToolCallTracker tools;
    bool any_structured_tool = false; // 真正的 tool_calls[] 三角洲到达
    // 不可归属的块毒害了工具状态：我们无法知道 字节 属于
    // 哪个 调用，因此每个正在运行的 调用 可能会丢失参数。
    // 设置一次，报告一次，它就会阻止我们发布从我们
    // 不再信任的状态构建的工具 事件。
    bool tool_attribution_failed = false;

    // ── 递增泄漏的-tool-call 救助(当地型号) ─────────────
    // 许多Ollama/llama.cpp模型(qwen2.5-coder、
    // hermes、mistral等)将工具调用发出为`content`中的裸JSON，
    // 而不是结构化`tool_calls[]`通道。我们解析 INCREMENTALLY：
    // 一旦识别出完整的JSON对象，立即将其作为真正的工具调用发出（因此
    // 该卡在流式传输期间出现，而不是在[DONE]之后出现）。
    //
    // IMPORTANT:补救ONLY适用于响应的START。
    // 一旦我们看到模型正在输出散文（不是工具 调用），我们就永久禁用抢救。
    // 这可以防止 `int main() {` 或
    // `#include <iostream>` 等代码出现误报。
    //
    // 持有缓冲区累积COULD仍然是工具JSON的内容。
    // 我们跟踪支撑深度以了解JSON对象何时完成。
    std::string text_hold;          // 可疑的缓冲文本
    bool        holding   = false;  // 当前缓冲潜在工具JSON
    bool        salvage_eligible = true;  // 仍可以是工具调用(响应开始)
    bool        any_text_flushed = false;
    int         salvage_seq = 0;    // 唯一化合成的废品调用 ids
    std::vector<std::string> known_tools;  // 我们可以挽救的工具名称
    // 内存工具通常破坏性太大而无法从泄露的JSON推断。
    // 请求设置仅启用最新用户明确请求的操作，
    // 因此weak/local模型可以尊重"remember this"
    // 而不使得未经请求的内存突变成为可能。
    bool allow_remember_salvage = false;
    bool allow_forget_salvage = false;
    bool allow_wipe_salvage = false;

    // Mistral / Magistral（以及一些 vLLM 推理 解析器）不会发送
    // 单独的 reasoning_content 字段 - 它们将模型的思考包装在 [
    // THINK]…[/THINK] (Mistral) 或 <think>…</think>
    // (DeepSeek-R1 / Qwen / QwQ / 大多数本地模型) 标签 内部
    // 中`content`。我们剥离那些内联并将包含的文本 re-route 作为推理（
    // StreamThinkingDelta），因此它在推理块中 渲染 而不是泄漏到答案中（因此
    // 领先的 `[`/`<` 永远不会触发工具-call 救援）。镜像 vLLM 的
    // BaseThinkingReasoningParser 合同：
    //   ·in_think:当前在推理范围内（跨增量）。
    //   ·think_carry:跨越两个内容增量的标签("[TH"|"INK]")。
    //   · think_closed：看到了 首次 关闭标签 — 之后的所有内容都是
    //     内容，甚至是后来的杂散开放标签(没有re-entry)。
    //   ·think_seen_open:看到OPEN标签。许多模型（Magistral）开始
    //     在使用 NO 开放标签进行推理时——由下面的 reason_by_default 处理。
    //   ·think_probing:仍判断数据流是否以标签/打开
    //     开始推理（缓冲区是第一个字节，直到确定）。
    bool        in_think = false;
    bool        think_closed = false;
    bool        think_seen_open = false;
    bool        think_emitted_content = false;   // 任何非-think的散文
    bool        think_probing = true;            // 仍决定open-vs-default
    // 是否已知THIS模型以NO开放标签开始推理(Magistral，
    // 以及仅有时在前面加上<think>的DeepSeek-R1)。
    // 当为 false 时，我们将 绝不 视为隐含推理 - 普通答案
    // 散文中的杂散 "</think>"/"[/THINK]" 会逐字保留。
    // 从模型 ID 设置。无论如何，显式的打开+关闭标签始终受到尊重。
    bool        reason_by_default = false;
    std::string think_carry;   // 跨增量的部分open/close标签
    std::string think_probe;   // 前导字节被缓冲，直到reason-by-default决定

    // 用于抢救的增量JSON 解析状态。
    int  brace_depth = 0;           // 当前 JSON 对象中的嵌套 {} 深度
    int  bracket_depth = 0;         // 嵌套[]深度（对于调用的数组）
    bool in_string = false;         // JSON 字符串文字内
    bool escape_next = false;       // 下一个字符在字符串中转义
    std::size_t json_start = std::string::npos;  // 当前JSON开始的偏移量
    bool saw_wrapper = false;  // 当前暂存期间是否见过工具包裹标记，例如 ``` 或 <tool_call>？

    StopReason stop_reason = StopReason::Unspecified;
    bool terminated = false;
};

// `s`是否可能是泄露工具-call的START？这仅在响应开始时调
// 用（salvage_eligible=true）。一旦检测到散文，
// 就会永久禁用此响应的抢救。共享分类器 — 请参阅
// wire::could_be_tool_json（单一事实来源；在此处和 ollama
// 传输层 中重复）。
using wire::could_be_tool_json;

[[nodiscard]] StopReason parse_openai_finish(std::string_view fr) noexcept {
    // OpenAI finish_reason:"stop" | "length" | "tool_calls"
    // | "content_filter" | "function_call" (旧版)。映射到 agentty 的 枚举。
    if (fr == "stop")           return StopReason::EndTurn;
    if (fr == "length")         return StopReason::MaxTokens;
    if (fr == "tool_calls")     return StopReason::ToolUse;
    if (fr == "function_call")  return StopReason::ToolUse;
    // 提供商安全过滤器切断了回复。如果没有这个，它就会被读作一个干净的、
    // 可能是空的、没有给出任何理由的回合结束。
    if (fr == "content_filter") return StopReason::Refusal;
    return StopReason::Unspecified;
}

// 将每个 still-open 工具 调用 恰好关闭一次。 OpenAI Chat
// 可以通过 tool_calls[].index 交错参数增量，并且只给出一个
// Turn-level 完成，因此关闭 调用 只是因为发出的另一个索引会截断同级。
json extra_for(StreamCtx& ctx, std::string_view id) {
    auto it = ctx.tool_extra.find(id);
    if (it == ctx.tool_extra.end()) return nullptr;
    json out = std::move(it->second);
    ctx.tool_extra.erase(it);
    return out;
}

void close_open_tools(StreamCtx& ctx) {
    // 在此响应中早些时候归因失败：我们知道一些论点 字节 没有被放置，
    // 但不知道哪个 调用 丢失了它们。现在宣布这些 调用
    // 将为模型提供一组看起来完整但实际上并不完整的工具调用。
    // StreamError已经出去了；保持安静。
    if (ctx.tool_attribution_failed) return;
    for (auto& slot : ctx.tools.all()) {
        // 其名称从未解析为已知工具的调用从未被
        // 公布（开始门等待可解析的名称，
        // 因此碎片名称可以完成累积）。现在根据积累的内容宣布它，
        // 因此它达到了“未知工具”的模型：
        // <full name>”而不是默默地消失在-turn中。
        if (!slot.started && !slot.name.empty()) {
            if (slot.id.empty()) slot.id = "call_late";
            ctx.sink(StreamToolUseStart{ToolCallId{slot.id},
                                        ToolName{slot.name}});
            if (!slot.args.empty())
                ctx.sink(StreamToolUseDelta{ToolCallId{slot.id}, slot.args});
            slot.started = true;
        }
        if (!slot.started || slot.ended || slot.id.empty()) continue;
        ctx.sink(StreamToolUseEnd{ToolCallId{slot.id}, extra_for(ctx, slot.id)});
        slot.ended = true;
    }
}

// 剥离围绕泄漏工具调用放置的弱本地模型的包装，
// 因此剩下的是（理想情况下）裸露的JSON对象：周围的空白、单个
// tool_call>…</tool_call> 标签对，或 ```json … ``` 代码围栏。顺序
// 是标签-then-fence-then-trim，宽大地应用（
// 允许缺少闭合tag/fence——传输可能已被切断）。返回内部切片。
[[nodiscard]] std::string_view strip_tool_call_wrappers(std::string_view sv) noexcept {
    auto ltrim = [](std::string_view& s) {
        while (!s.empty() && (s.front()==' '||s.front()=='\t'
                              ||s.front()=='\n'||s.front()=='\r')) s.remove_prefix(1);
    };
    auto rtrim = [](std::string_view& s) {
        while (!s.empty() && (s.back()==' '||s.back()=='\t'
                              ||s.back()=='\n'||s.back()=='\r')) s.remove_suffix(1);
    };
    ltrim(sv); rtrim(sv);
    // <tool_call> … </tool_call>
    if (sv.starts_with("<tool_call>")) {
        sv.remove_prefix(std::string_view{"<tool_call>"}.size());
        if (auto p = sv.rfind("</tool_call>"); p != std::string_view::npos)
            sv = sv.substr(0, p);
        ltrim(sv); rtrim(sv);
    }
    // ```json … ```  (or a bare ``` fence)
    if (sv.starts_with("```")) {
        sv.remove_prefix(3);
        if (sv.starts_with("json")) sv.remove_prefix(4);
        ltrim(sv);
        if (auto p = sv.rfind("```"); p != std::string_view::npos)
            sv = sv.substr(0, p);
        rtrim(sv);
    }
    return sv;
}

// 正确，当且仅当所持有的缓冲区像裸露的工具-call JSON对象一样打开，但是
// INCOMPLETE — it begins with `{` (after optional whitespace / a ```json
// 栅栏/<tool_call>标签）但不将解析作为完整的JSON值。
// 这是模型泄漏到 `content` 的工具 调用 的签名，传输 切断了
// 正文中途被上游截断。
// 仅仅命名未广告工具的 COMPLETE 对象不是 不完整的 - 它仍然以文本形式显示，
// 以便用户了解模型的含义。
[[nodiscard]] bool hold_is_truncated_tool_json(std::string_view sv) noexcept {
    std::string_view raw = sv;
    sv = strip_tool_call_wrappers(sv);
    // 暂存内容只有工具调用包裹标记，例如单独的 ``` / ```json。
    // 栅栏或剥离后没有 JSON 正文 的 <tool_call> 标签 —
    // 是一个泄漏的包装纸，其 正文 从未到达（或栅栏 -only 开启器，如
    // 例如 qwen 只输出 "```json" 就停止。这绝不应作为正常
    // prose; surfacing it dumps a stray "json" / "```" into the reply (the
    // 错误，其中 "hi" 用文字文本 "json" 回答）。放下它。
    if (sv.empty() && raw != sv) return true;
    // 同样，脱衣机左侧有栅栏-word-only残余物（e.g。型号
    // 只输出 "json" 或 "```json"，没有任何可解析内容的情况。
    if (sv == "json" || sv == "```" || sv == "`") return true;
    // 空对象 "{}" — qwen2.5-coder:14b 在传递
    // 工具时输出此内容，但它不想输出任何 调用。这是垃圾，不是散文。
    if (sv == "{}") return true;
    if (sv.empty() || sv.front() != '{') return false;
    try { auto _ = json::parse(sv); (void)_; return false; }   // 完成 — 保留为文本
    catch (...) { return true; }                    // 截断 — 丢弃
}

// COMPLETE JSON形状为EXACTLY的物体，像工具调用 ({"name"|"function":
// "<str>"，"arguments"|"parameters"：{...}})，
// 其在known_tools中命名工具NOT。较弱的本地模型会因输入错误的工具
// 名称（e.g."read_file" 而不是 "read"）而不断泄漏这些信息；
// 暴露原始 JSON 会将垃圾转储到回复中，并在下一回合启动 re-leak。
// 用户想要阅读的绝不是散文。我们在ARE的时候才咨询过广告工具。
[[nodiscard]] bool hold_is_unknown_tool_call(
        std::string_view sv, const std::vector<std::string>& known) noexcept {
    if (known.empty()) return false;            // 不处于代理模式
    sv = strip_tool_call_wrappers(sv);
    if (sv.empty() || sv.front() != '{') return false;
    json j;
    try { j = json::parse(sv); } catch (...) { return false; }
    if (!j.is_object()) return false;
    std::string name;
    if (j.contains("name") && j["name"].is_string())
        name = j["name"].get<std::string>();
    else if (j.contains("function") && j["function"].is_string())
        name = j["function"].get<std::string>();
    else if (j.contains("tool") && j["tool"].is_string())
        name = j["tool"].get<std::string>();
    else
        return false;                           // 没有工具-name键——真正的散文
    // ALSO必须携带args/params字段才能算作工具-call形状。
    if (!j.contains("arguments") && !j.contains("parameters")) return false;
    for (const auto& t : known) if (t == name) return false;  // 刊登广告
    return true;                                 // 工具-shaped，名称未知
}

// 将text_hold作为普通散文发出。当我们确定某个前缀不是工具
// JSON（e.g.，在 JSON 对象之前引导散文）时使用。
void flush_text_slice(StreamCtx& ctx, std::size_t len) {
    if (len == 0) return;
    std::string_view slice{ctx.text_hold.data(), len};
    ctx.sink(StreamTextDelta{std::string{slice}});
    ctx.any_text_flushed = true;
    ctx.text_hold.erase(0, len);
    // 由于我们删除了前缀，因此重置 JSON 解析 状态。
    ctx.json_start = std::string::npos;
    ctx.brace_depth = 0;
    ctx.bracket_depth = 0;
    ctx.in_string = false;
    ctx.escape_next = false;
}

// 将任何保持的文本作为普通散文发出并停止保持。当持有的 缓冲
// 区 不再是泄漏工具 -call JSON 时，或者在抢救不
// 适用时完成时调用。
//
// IMPORTANT:如果保持像工具-call对象一样打开但是是INCOMPLETE
// (传输在`content`中切断，所以它不能解析)，我们对其DROP而不是
// 将其刷新为可见散文。将半-written
// `{"name":"remember","arguments":{...`转储到助手
// 正文中都显示JSON垃圾AND轮-trips回到弱本地模型（qwen/llama.cpp），
// 然后在下一轮重新-leaks相同的调用 - 卡住的"upstream cut off"
// re-invocation。保留并显示 COMPLETE 对象（甚至是
// 命名未公布的工具的对象），以便用户看到模型的意图。

// 将内容增量分割在内联推理标签上并将内部路由到推理块。处理 两者 分隔符系列 — Mistral
// [THINK]…[/THINK] 和 DeepSeek-R1/Qwen <think>…
// </think> — 以及三个 real-world 形状 vLLM 的
// BaseThinkingReasoningParser 句柄：
//   1.显式打开+关闭："[THINK]…[/THINK]answer"
//   2.NO开放标签（原因-by-default，Magistral上常见）：数据流开始
//      在推理中，FIRST关闭标签结束它——"…thoughts…[/THINK]answer"
//   3.完全没有标签：纯内容。
// 第一个关闭后标签EVERYTHING是内容（后来的流浪打开从来没有re
// -enters推理）。跨增量分割的标签在 think_carry 中携带。
// 内部发出为StreamThinkingDelta(隐藏推理时的心跳，
// 匹配reasoning_content路径)；
// 返回的字符串是调用者输入到工具-call 回收中的内容。
std::string extract_think_spans(StreamCtx& ctx, std::string_view in) {
    // 一旦推理结束，一切都是简单内容——快速路径。
    if (ctx.think_closed) {
        std::string s{in};
        if (!s.empty()) ctx.think_emitted_content = true;
        return s;
    }

    static constexpr std::string_view kOpens[]  = {"[THINK]", "<think>"};
    static constexpr std::string_view kCloses[] = {"[/THINK]", "</think>"};

    std::string prose;
    std::string buf = ctx.think_carry + std::string{in};   // 前置携带部分
    ctx.think_carry.clear();

    auto emit_reason = [&](std::string_view r) {
        if (r.empty()) return;
        if (ctx.show_reasoning) ctx.sink(StreamThinkingDelta{std::string{r}, ""});
        else                    ctx.sink(StreamThinkingDelta{"", ""});   // 心跳
    };
    auto find_any = [&](const std::string_view (&set)[2], std::size_t from,
                        int& which) -> std::size_t {
        std::size_t best = std::string::npos; which = -1;
        for (int t = 0; t < 2; ++t) {
            std::size_t p = buf.find(set[t], from);
            if (p < best) { best = p; which = t; }
        }
        return best;
    };
    auto tail_is_partial = [&](const std::string_view (&set)[2],
                               std::size_t from) -> std::size_t {
        // buf[from:]的最长后缀，其是标签的正确前缀。
        std::size_t keep = buf.size();
        for (int t = 0; t < 2; ++t) {
            const auto& tag = set[t];
            for (std::size_t back = 1;
                 back < tag.size() && back <= buf.size() - from; ++back) {
                if (std::string_view{buf}.substr(buf.size() - back)
                        == tag.substr(0, back))
                    keep = std::min(keep, buf.size() - back);
            }
        }
        return keep;
    };

    // 核心分裂是内部lambda，因此EVERY出口流过下面的
    // think_emitted_content更新（探测器必须仅在开始时激发）。
    auto run = [&]() {
        std::size_t i = 0;
        while (i < buf.size()) {
            if (ctx.in_think) {
                int w = -1;
                const std::size_t pos = find_any(kCloses, i, w);   // CLOSE标签
                if (pos == std::string::npos) {
                    const std::size_t keep = tail_is_partial(kCloses, i);
                    emit_reason(std::string_view{buf.data() + i, keep - i});
                    ctx.think_carry.assign(buf, keep, std::string::npos);
                    return;
                }
                emit_reason(std::string_view{buf.data() + i, pos - i});
                ctx.in_think = false;
                ctx.think_closed = true;          // 锁存器：无re-entry
                i = pos + kCloses[w].size();
                prose += std::string_view{buf.data() + i, buf.size() - i};
                return;
            }

            // REASON-BY-DEFAULT探头。一开始我们还不知道
            // 这个模型是否以标签打开，以没有标签的推理开始（Magistral），
            // 或者根本没有推理。 缓冲区 领先于 think_probe
            // 中的 字节，直至做出以下决定：
            //   ·任何OPEN之前的CLOSE→缓冲区是隐式推理；
            //   • OPEN → 缓冲区（最多）是内容；
            //   · 缓冲区超过上限→假定纯内容（齐平）。
            // 有界上限防止非-reasoning模型停顿。
            if (ctx.think_probing) {
                // Reason-by-default已知的模型NOT从不将前导文本视为隐式推理
                // ——直接跳至显式-tag模式(无缓冲，
                // 无杂散-close误报)。
                if (!ctx.reason_by_default) { ctx.think_probing = false; continue; }
                int ow = -1, cw = -1;
                const std::size_t open  = find_any(kOpens, i, ow);
                const std::size_t close = find_any(kCloses, i, cw);
                if (close != std::string::npos
                    && (open == std::string::npos || close < open)) {
                    // 隐式推理：探测缓冲区+文字可达关闭。
                    ctx.think_probing = false;
                    ctx.think_closed = true;
                    emit_reason(ctx.think_probe);
                    emit_reason(std::string_view{buf.data() + i, close - i});
                    ctx.think_probe.clear();
                    i = close + kCloses[cw].size();
                    prose += std::string_view{buf.data() + i, buf.size() - i};
                    return;
                }
                if (open != std::string::npos) {
                    // 显式打开：在内容之前探测缓冲区+文本。
                    ctx.think_probing = false;
                    prose += ctx.think_probe; ctx.think_probe.clear();
                    prose += std::string_view{buf.data() + i, open - i};
                    ctx.in_think = true;
                    ctx.think_seen_open = true;
                    i = open + kOpens[ow].size();
                    continue;
                }
                // 还没有标签。 缓冲区，减去可能的部分标签 末尾。
                constexpr std::size_t kProbeCap = 256;
                const std::size_t keep = std::min(tail_is_partial(kOpens, i),
                                                  tail_is_partial(kCloses, i));
                ctx.think_probe.append(buf, i, keep - i);
                ctx.think_carry.assign(buf, keep, std::string::npos);
                if (ctx.think_probe.size() > kProbeCap) {
                    // 没有标签的长领先运行 → 该模型没有包装推理；
                    // 提交 缓冲区 作为内容并停止探测。
                    ctx.think_probing = false;
                    prose += ctx.think_probe; ctx.think_probe.clear();
                }
                return;
            }

            // 过去的探针：仅显式-tag模式（打开re
            // -enters推理直到第一次关闭锁存器think_closed）。
            int w = -1;
            const std::size_t open = find_any(kOpens, i, w);
            if (open == std::string::npos) {
                const std::size_t keep = tail_is_partial(kOpens, i);
                prose += std::string_view{buf.data() + i, keep - i};
                ctx.think_carry.assign(buf, keep, std::string::npos);
                return;
            }
            prose += std::string_view{buf.data() + i, open - i};
            ctx.in_think = true;
            ctx.think_seen_open = true;
            i = open + kOpens[w].size();
        }
    };
    run();
    if (!prose.empty()) ctx.think_emitted_content = true;
    return prose;
}

void flush_text_hold(StreamCtx& ctx) {
    ctx.holding = false;
    if (ctx.text_hold.empty()) return;
    if (hold_is_truncated_tool_json(ctx.text_hold)) {
        // Weak-model异质性事件：该模型试图通过将JSON泄漏到
        // 内容中来将调用作为工具，并被切断。 字节 被丢弃（正确），
        // 但 ATTEMPT 必须可见 - 这就是
        // "the model ignores tools" 在 传输 上的实际外观。
        AGT_LOG(Model, Warn, "salvage.dropped_truncated",
                "model={} bytes={} head={}", ctx.model_id, ctx.text_hold.size(),
                std::string_view{ctx.text_hold}.substr(0, 200));
        ctx.text_hold.clear();   // 截断的泄露工具调用——掉落
        return;
    }
    if (hold_is_unknown_tool_call(ctx.text_hold, ctx.known_tools)) {
        AGT_LOG(Model, Warn, "salvage.dropped_unknown_tool",
                "model={} bytes={} head={}", ctx.model_id, ctx.text_hold.size(),
                std::string_view{ctx.text_hold}.substr(0, 200));
        ctx.text_hold.clear();   // 完整泄漏命名未公布的工具
        return;
    }
    ctx.sink(StreamTextDelta{ctx.text_hold});
    ctx.any_text_flushed = true;
    ctx.text_hold.clear();
}

// 防止SILENTLY EMPTY转弯。弱本地模型有时会将截断的工具
// -call JSON 泄漏到`content` 中，仅此而已；
// flush_text_hold 然后将其丢弃（正确 - 参见上文），
// 但助理消息最终显示零文本和零工具 调用，因此用户会看到一个空气泡。
// 如果整个 数据流 没有生成任何文本，没有回收的工具，也没有结构化工具 调用，
// 请发出一条短线，这样转弯就不会是空白。 调用 这个 之后 salvage/flush
// 在每个 终端 收盘时，之前 StreamFinished。
void ensure_nonempty_turn(StreamCtx& ctx) {
    if (ctx.any_text_flushed) return;
    if (ctx.any_structured_tool) return;
    if (ctx.stop_reason == StopReason::ToolUse) return;  // 打捞出一艘调用
    ctx.sink(StreamTextDelta{std::string{kEmptyTurnText}});
    ctx.any_text_flushed = true;
}

// 尝试从JSON对象发出单个工具调用。如果发出则返回 true。
// JSON必须具有{"name":"...","arguments":{...}}或
// 使用"function"作为名称键(某些型号使用此变体)。
[[nodiscard]] bool emit_salvaged_tool(StreamCtx& ctx, std::string_view sv) {
    json j;
    try { j = json::parse(sv); } catch (...) { return false; }
    if (!j.is_object()) return false;

    // 提取工具名称-模型使用不同的键：
    //   -"name":"tool_name"(标准)
    //   -"function":"tool_name"(某些型号)
    //   -"tool"："tool_name"（罕见）
    std::string name;
    if (j.contains("name") && j["name"].is_string()) {
        name = j["name"].get<std::string>();
    } else if (j.contains("function") && j["function"].is_string()) {
        name = j["function"].get<std::string>();
    } else if (j.contains("tool") && j["tool"].is_string()) {
        name = j["tool"].get<std::string>();
    } else {
        return false;  // 没有可识别的名称键
    }
    // 仅对我们实际宣传的工具进行抢救——绝不发明调用。
    bool known = false;
    for (const auto& t : ctx.known_tools) if (t == name) { known = true; break; }
    if (!known) return false;

    // 敏感meta-tools仅在最新的用户消息明确请求该确切类别的操作时才被挽救。
    // 这保留了旧的 greeting/small-talk 安全门，
    // 同时修复了只能发出 JSON-in-content 工具
    // 的弱模型：它们的显式 "remember this" 调用
    // 过去每次都会被吞掉。
    if (name == "remember" && !ctx.allow_remember_salvage) return true;
    if (name == "forget" && !ctx.allow_forget_salvage) return true;
    if (name == "wipe_memory" && !ctx.allow_wipe_salvage) return true;
    if (name == "skill") return true;

    // 参数可以是对象（qwen)或JSON字符串（一些模板）。
    std::string args = "{}";
    if (j.contains("arguments")) {
        const auto& a = j["arguments"];
        if (a.is_string())      args = a.get<std::string>();
        else if (a.is_object()) args = a.dump();
        else if (a.is_array())  args = a.dump();  // 部分机型采用数组
    } else if (j.contains("parameters")) {
        const auto& p = j["parameters"];
        if (p.is_object())      args = p.dump();
        else if (p.is_string()) args = p.get<std::string>();
    }

    std::string call_id = "call_salvaged_" + std::to_string(ctx.salvage_seq++);
    // 该模型绕过了结构化工具通道，我们从泄露的内容JSON中恢复了它。
    // 可以，但有 MODEL CAPABILITY 的气味：
    // 对给定（模型、主机）的频繁抢救意味着其本机工具语法被
    // 破坏或禁用（"grammar set to none")
    // ——值得报告上游的确切异质性事实。
    AGT_LOG(Model, Info, "salvage.tool_call",
            "model={} tool={} args_bytes={} id={}",
            ctx.model_id, name, args.size(), call_id);
    ctx.sink(StreamToolUseStart{ToolCallId{call_id}, ToolName{name}});
    ctx.sink(StreamToolUseDelta{ToolCallId{call_id}, args});
    ctx.sink(StreamToolUseEnd{ToolCallId{call_id}});
    ctx.stop_reason = StopReason::ToolUse;
    return true;
}

// 处理工具调用的阵列（一些模型发出[{...}，{...}]）。
// 如果发射了至少一个工具，则返回真。
[[nodiscard]] bool emit_salvaged_tool_array(StreamCtx& ctx, std::string_view sv) {
    json arr;
    try { arr = json::parse(sv); } catch (...) { return false; }
    if (!arr.is_array()) return false;
    bool any = false;
    for (const auto& item : arr) {
        if (item.is_object() && emit_salvaged_tool(ctx, item.dump()))
            any = true;
    }
    return any;
}

// 扫描text_hold以获取可能是工具调用的完整JSON objects/arrays。
// 当它们被发现时立即发射它们。如果有任何发出，则返回 true。
// 这是核心增量抢救逻辑——工具卡出现在DURING 流式传输，
// 而不是在[DONE]之后。
[[nodiscard]] bool try_incremental_salvage(StreamCtx& ctx) {
    if (ctx.any_structured_tool) return false;  // 真tool_calls[]获胜
    if (ctx.text_hold.empty()) return false;

    bool any_emitted = false;

    // 剥离弱本地模型包裹泄漏工具调用的包裹前缀(
    // 开头tags/fences)和后缀(结尾)。当我们看到
    // 实际 包装器时，仅 会被剥离——不要剥离普通的空白或内容。
    // 这在增量抢救开始时并且在提取JSON对象
    // 之后被调用（以消耗尾随包装器）。
    auto skip_wrapper_prefix = [&]() {
        std::string_view sv{ctx.text_hold};
        std::size_t orig = sv.size();
        
        // 助手：从 sv 中修剪前导空白。
        auto ltrim = [&sv]() {
            while (!sv.empty() && (sv.front()==' '||sv.front()=='\t'
                                   ||sv.front()=='\n'||sv.front()=='\r')) {
                sv.remove_prefix(1);
            }
        };
        
        // 继续剥离包装纸，直到我们找不到更多的包装纸为止。
        // This handles cases like: <tool_call>```json\n{...}
        bool found_any = false;
        bool found_this_pass = true;
        while (found_this_pass) {
            found_this_pass = false;
            ltrim();
            
            // <tool_call> 开头标签
            if (sv.starts_with("<tool_call>")) {
                sv.remove_prefix(11);
                found_this_pass = true;
                found_any = true;
                continue;
            }
            // </tool_call>结束标签
            if (sv.starts_with("</tool_call>")) {
                sv.remove_prefix(12);
                found_this_pass = true;
                found_any = true;
                continue;
            }
            // ```json fence
            if (sv.starts_with("```json")) {
                sv.remove_prefix(7);
                found_this_pass = true;
                found_any = true;
                continue;
            }
            // 单独的 ``` 围栏，可带或不带 json 后缀。JSON 正文
            // 跟在空格后面。无条件地剥离它——工具 调用 包装
            // 器是 绝不 后面是重要的非 -JSON 内容。
            if (sv.starts_with("```")) {
                sv.remove_prefix(3);
                // 如果存在的话，还要剥离 "json"（型号可能会单独发送）。
                ltrim();
                if (sv.starts_with("json")) sv.remove_prefix(4);
                found_this_pass = true;
                found_any = true;
                continue;
            }
            // 单独的 json 前缀，可能来自流式 ```json 围栏；此前增量中
            // ``` 已被剥离。这里移除残留前缀。
            if (sv.starts_with("json")) {
                sv.remove_prefix(4);
                found_this_pass = true;
                found_any = true;
                continue;
            }
        }
        
        if (!found_any) {
            // 没有找到包装器 此 调用 — 但如果我们在 PREVIOUS 调用 (
            // saw_wrapper) 上看到一个包装器，仍然会去除前导空白（间隙
            // between ```json and {).
            if (!ctx.saw_wrapper) return;
            // 在先前的 -seen 包装器之后删除空白。
        } else {
            ctx.saw_wrapper = true;
        }
        
        // 去掉包装器后的最后一个空格。
        ltrim();
        
        std::size_t removed = orig - sv.size();
        if (removed > 0) {
            ctx.text_hold.erase(0, removed);
            // 由于我们删除了内容，因此重置 全部 解析 状态。
            ctx.json_start = std::string::npos;
            ctx.brace_depth = 0;
            ctx.bracket_depth = 0;
            ctx.in_string = false;
            ctx.escape_next = false;
        }
    };

    skip_wrapper_prefix();
    if (ctx.text_hold.empty()) return false;

    // 逐个字符扫描找到完整的JSON objects/arrays.
    // 我们跟踪 brace/bracket 深度以了解 {} 或 [] 何时完成。
    for (std::size_t i = 0; i < ctx.text_hold.size(); ++i) {
        char c = ctx.text_hold[i];

        if (ctx.escape_next) {
            ctx.escape_next = false;
            continue;
        }

        if (ctx.in_string) {
            if (c == '\\') ctx.escape_next = true;
            else if (c == '"') ctx.in_string = false;
            continue;
        }

        // 不在字符串中。
        if (c == '"') {
            ctx.in_string = true;
            continue;
        }

        if (c == '{') {
            if (ctx.brace_depth == 0 && ctx.bracket_depth == 0) {
                // 开始一个新的 JSON 对象。
                if (ctx.json_start == std::string::npos) {
                    // 将任何散文 之前 冲入 JSON。
                    if (i > 0) {
                        flush_text_slice(ctx, i);
                        i = 0;  // 从新的开始继续。
                    }
                    ctx.json_start = 0;
                }
            }
            ctx.brace_depth++;
        } else if (c == '}') {
            if (ctx.brace_depth > 0) ctx.brace_depth--;
            // 完整的对象？
            if (ctx.brace_depth == 0 && ctx.bracket_depth == 0
                && ctx.json_start != std::string::npos) {
                std::size_t end = i + 1;
                std::string_view candidate{ctx.text_hold.data() + ctx.json_start,
                                           end - ctx.json_start};
                if (emit_salvaged_tool(ctx, candidate)) {
                    any_emitted = true;
                    // 将发射的 JSON 从支架中取出。
                    ctx.text_hold.erase(0, end);
                    ctx.json_start = std::string::npos;
                    ctx.brace_depth = 0;
                    ctx.bracket_depth = 0;
                    // 如果存在末尾 </tool_call> 或 ```，则跳过。
                    skip_wrapper_prefix();
                    // 从剩余保留的开头重新开始扫描。
                    i = static_cast<std::size_t>(-1);  // ++后为0
                    continue;
                } else {
                    // 完整的物体，不是可回收的 调用。如果它是 tool
                    // -SHAPED 但命名了一个未公布的工具（weak
                    // -model 错误），请将其删除而不是转储原始 JSON。
                    // 否则它就是散文——与文本一样齐平。
                    if (hold_is_unknown_tool_call(candidate, ctx.known_tools)) {
                        ctx.text_hold.erase(0, end);
                        ctx.json_start = std::string::npos;
                        ctx.brace_depth = 0;
                        ctx.bracket_depth = 0;
                        skip_wrapper_prefix();
                    } else {
                        flush_text_slice(ctx, end);
                    }
                    i = static_cast<std::size_t>(-1);
                    continue;
                }
            }
        } else if (c == '[') {
            if (ctx.brace_depth == 0 && ctx.bracket_depth == 0) {
                // 启动 JSON 数组（某些型号发出 [{...}, {...}]）。
                if (ctx.json_start == std::string::npos) {
                    if (i > 0) {
                        flush_text_slice(ctx, i);
                        i = 0;
                    }
                    ctx.json_start = 0;
                }
            }
            ctx.bracket_depth++;
        } else if (c == ']') {
            if (ctx.bracket_depth > 0) ctx.bracket_depth--;
            // 阵法齐全？
            if (ctx.bracket_depth == 0 && ctx.brace_depth == 0
                && ctx.json_start != std::string::npos) {
                std::size_t end = i + 1;
                std::string_view candidate{ctx.text_hold.data() + ctx.json_start,
                                           end - ctx.json_start};
                if (emit_salvaged_tool_array(ctx, candidate)) {
                    any_emitted = true;
                    ctx.text_hold.erase(0, end);
                    ctx.json_start = std::string::npos;
                    ctx.brace_depth = 0;
                    ctx.bracket_depth = 0;
                    skip_wrapper_prefix();
                    i = static_cast<std::size_t>(-1);
                    continue;
                } else {
                    // 无效工具 调用 — 以文本形式刷新。
                    flush_text_slice(ctx, end);
                    i = static_cast<std::size_t>(-1);
                    continue;
                }
            }
        }
    }

    return any_emitted;
}

// 最后：尝试将剩余的保留文本作为工具 调用 进行抢救。
// 这处理了 JSON 在 -stream 中期不完整但在 [DONE]
// 之前完成的情况。如果被抢救则返回 true。
[[nodiscard]] bool try_salvage_tool_call(StreamCtx& ctx) {
    if (ctx.any_structured_tool) return false;
    if (ctx.text_hold.empty())   return false;

    // 增量抢救的最后尝试（可能现在完成）。
    if (try_incremental_salvage(ctx)) return true;

    // 传统路径：尝试将整个保持作为一个对象。
    std::string_view sv = strip_tool_call_wrappers(ctx.text_hold);
    if (sv.empty()) return false;

    // 可以是单个对象或数组。
    if (sv.front() == '{') {
        if (emit_salvaged_tool(ctx, sv)) {
            ctx.text_hold.clear();
            ctx.holding = false;
            return true;
        }
    } else if (sv.front() == '[') {
        if (emit_salvaged_tool_array(ctx, sv)) {
            ctx.text_hold.clear();
            ctx.holding = false;
            return true;
        }
    }
    return false;
}

// 处理一个 choice[0].delta 对象。
void merge_reasoning_details(json& into, const json& chunks);
void handle_delta(StreamCtx& ctx, const json& delta) {
    if (auto rd = delta.find("reasoning_details"); rd != delta.end() && rd->is_array())
        merge_reasoning_details(ctx.reasoning_details, *rd);
    // 推理/链-of-thought文本。 Reasoning-capable 在聊天 传输 数据
    // 流 上模拟了他们在 PARALLEL 到 `content` 领域的思维，并且生态系统
    // 对其名称没有达成一致 - DeepSeek/vLLM 说
    // `reasoning_content`，OpenAI 的响应 API 和一些 OpenRouter
    // 传递说 `reasoning`。
    //
    // 对应的 SPELLINGS EXIST IS 不 DECIDED 此处。
    // dialect::reasoning_delta() 是唯一的事实来源，
    // 它还对排序规则进行编码（更喜欢特定的键，但当它是 空 时就会失败—
    // —一些代理在填充的推理旁边发送一个空的 reasoning_content，
    // 并在仅存在的第一个键处停止，默默地丢弃所有推理）。
    // 添加提供商的拼写是一个 -line 更改，由针对捕获的灯具的
    // openai_dialect_test.cpp 覆盖。
    //
    // 发射为 StreamThinkingDelta — 相同 事件 Anthropic 传输层
    // 发射为 thinking_delta — 因此 状态更新函数 / UI 渲染 完全相同。
    // 在任何可见内容之前的长时间推理暂停期间也是一个活跃
    // 信号。
    // 捕获无论 UI 是否显示它：下一回合将其发送回（
    // GitHub 的客户端出于相同原因保留它）。
    // 隐藏的推理仍然没有呈现；视图决定这一点，而不是 传输。
    if (auto r = dialect::reasoning_delta()(delta)) {
        StreamThinkingDelta td{*r, {}};
        td.wire_field = std::string{dialect::reasoning_field(delta)};
        ctx.sink(std::move(td));
    }

    // 结构化内容-parts数组（米斯特拉尔推理模型）：`content`作为带类型的部分的LIST到达，
    // 而不是字符串 - {"content":[{"type":"thinking",
    //   "thinking":[{"type":
    //                "text","text":"…"}]}]}
    // for chain-of-thought, and {"type":"text","text":"…"} parts for prose.
    // （在 Mistra-small-latest 上实时探测，
    // reasoning_effort=高；最终答案仍然以简单内容 STRING
    // 的形式到达，如下处理。）将思维部分路由到
    // reasoning_content 路径发出的同一个 StreamThinkingDelta，
    // 并将文本部分路由到正常的散文处理中——没有这个分支，
    // 整个数组形式就默默地 DROPPED：推理不可见，期间没有活跃心跳。
    if (delta.contains("content") && delta["content"].is_array()) {
        for (const auto& part : delta["content"]) {
            if (!part.is_object()) continue;
            const std::string type = part.value("type", "");
            if (type == "thinking") {
                // 嵌套：思考：[{type:"text", text:"…"}, …]
                std::string chunk;
                if (auto th = part.find("thinking");
                    th != part.end() && th->is_array()) {
                    for (const auto& seg : *th)
                        if (seg.is_object() && seg.value("type", "") == "text")
                            chunk += seg.value("text", "");
                } else if (auto tx = part.find("text");
                           tx != part.end() && tx->is_string()) {
                    chunk = tx->get<std::string>();   // 平面 变体
                }
                // 一个有思想的 PART 到来了——这本身就是一个
                // 活跃信号，即使它积累的文本是空的。米斯特拉尔
                // 数据流 接近 -empty 在长推理过程中的思维片段；
                // 推理隐藏（show_reasoning =
                // false）并且还没有可见的散文，
                // 放弃这些默默地让摊位看门狗挨饿。展示时说出推理；
                // 否则 始终 为该部分发出心跳，无论是否为空文本。
                if (ctx.show_reasoning) {
                    if (!chunk.empty())
                        ctx.sink(StreamThinkingDelta{chunk, {}});
                    else
                        ctx.sink(StreamHeartbeat{});
                } else {
                    ctx.sink(StreamHeartbeat{});
                }
            } else if (type == "text") {
                const std::string t = part.value("text", "");
                if (!t.empty()) {
                    // 来自零件阵列的散文永远不会泄露工具 JSON（结构化
                    // 发射器使用真实的 tool_calls 通道），
                    // 因此直接发射 — 镜像 salvage-disabled 路径。
                    ctx.sink(StreamTextDelta{t});
                    ctx.any_text_flushed = true;
                    ctx.salvage_eligible = false;
                }
            }
        }
    }

    // 纯辅助文本。
    if (delta.contains("content") && delta["content"].is_string()) {
        const auto& raw = delta["content"].get_ref<const std::string&>();
        // Mistral/Magistral 将他们的推理内联为 `content`
        // 内的 [THINK]…[/THINK]。将这些跨度剥离为推理 首次；
        // `s`只是非-think的散文。 （如果模型从不发出 THINK 标签，
        // 这是一个廉价的身份通行证 - 没有找到标签，散文 == 原始。）在这里这样做
        // 还可以阻止 `[THINK]` 的领先 `[` 触发工具 -call 回收。
        const std::string s = extract_think_spans(ctx, raw);
        if (!s.empty()) {
            // SIMPLE LOGIC:
            // 1.如果我们已经持有，则追加并检查它是否仍然有效
            // 2.如果仍然可以挽救（响应开始），检查这是否
            //    STARTS是工具调用图案。如果没有，则发出并禁用抢救。
            // 3.一旦发出任何散文，就永久禁用抢救。
            
            if (ctx.holding) {
                // 积累潜在工具调用。
                ctx.text_hold += s;
                // 尝试提取完整的JSON工具调用。
                (void)try_incremental_salvage(ctx);
                // 如果仍然保持，检查其是否仍然可以是工具调用。
                if (ctx.holding && !could_be_tool_json(ctx.text_hold)) {
                    // 不是工具调用——像散文一样齐平。
                    flush_text_hold(ctx);
                    ctx.salvage_eligible = false;  // 不再尝试打捞
                }
            } else if (ctx.salvage_eligible && !ctx.any_structured_tool) {
                // 工具调用之间的响应开始。检查这是否可能是
                // 泄露工具 调用 的开始。
                if (could_be_tool_json(s)) {
                    // 开始保持。
                    ctx.holding = true;
                    ctx.text_hold = s;
                    ctx.json_start = std::string::npos;
                    ctx.brace_depth = 0;
                    ctx.bracket_depth = 0;
                    ctx.in_string = false;
                    ctx.escape_next = false;
                    ctx.saw_wrapper = false;
                    (void)try_incremental_salvage(ctx);
                    // 如果它是完整的工具调用，则try_incremental_salvage
                    // 已经发射它并清除保持。
                } else {
                    // 不是工具调用模式——作为散文发出。
                    ctx.sink(StreamTextDelta{s});
                    ctx.any_text_flushed = true;
                    ctx.salvage_eligible = false;  // 散文被检测到，不再打捞
                }
            } else {
                // 回收被禁用——仅直接发出文本。
                ctx.sink(StreamTextDelta{s});
                ctx.any_text_flushed = true;
            }
        }
    }

    // Tool-call片段（结构化）。
    if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
        if (!delta["tool_calls"].empty()) {
            ctx.any_structured_tool = true;
            ctx.holding = false;
            ctx.text_hold.clear();
            ctx.salvage_eligible = false;  // 真工具调用，无需打捞
        }
        int array_pos = -1;
        const std::size_t array_len = delta["tool_calls"].size();
        for (const auto& tc : delta["tool_calls"]) {
            ++array_pos;
            // `index`是聊天完成如何区分PARALLEL 调用：
            // 不同调用的片段交错在一个数据流中，
            // 并且索引是分隔它们的唯一东西。省略它的提供
            // 者过去常常将每个并行 调用 折叠到插槽 0 上，
            // 因此第二个 调用 的参数被附加到第一个，
            // 并且都丢失了。回到 载荷 自己的阵列
            // 位置使它们分开，这就是该场存在时的含义。
            const bool has_wire_index =
                tc.contains("index") && tc["index"].is_number_integer();
            const int index = has_wire_index ? tc["index"].get<int>() : array_pos;
            if (index < 0) continue;
            // `index`是服务器-controlled。恶意或有缺陷
            // 的端点可能会发送荒谬的端点，因此在它到达跟踪器之前对其
            // 进行限制 - 否则我们为其分配 per-call 状态。
            //
            // 当字段是ABSENT时，我们回退到载荷自己的
            // 阵列位置。聊天完成在一个增量中交错并行 调用
            // 的片段，而省略索引的提供者将位置作为唯一分隔
            // 它们的东西：开始增量列出了 调用，而延续增量以
            // 相同 顺序列出了它们的片段，没有 id 和索引。
            // 如果没有 回退，两个片段看起来都像
            // "no identity at all"，
            // 其中有两个飞行中的 调用 被正确地报告为不明确
            // ——正确，但它会丢弃参数，而 -ordered
            // 服务器确实给了我们足够的位置。
            //
            // 该推理仅在位置为 EVIDENCE 时成立，
            // 而不是猜测：它要求三角洲重述飞行中的每个 调用，
            // 因此插槽 N 实际上是第 N 个 调用。
            // 携带 FEWER 元素而不是开放 调用 的增量
            // 没有这样的对应关系——元素 0 可以是它们中的任何
            // 一个——并且使用那里的位置默默地将一个 调用 的
            // 参数附加到另一个。在这种情况下，根本不发送任何索引，
            // 并让跟踪器将其报告为不可归因。
            constexpr std::size_t kMaxToolSlots = 1024;
            const bool position_is_evidence =
                has_wire_index || ctx.tools.size() <= 1
                || array_len >= ctx.tools.size();
            std::optional<std::size_t> slot_index;
            if (position_is_evidence
                && static_cast<std::size_t>(index) < kMaxToolSlots)
                slot_index = static_cast<std::size_t>(index);

            // 该块属于WHO吗？
            //
            // 委托给wire::ToolCallTracker，
            // 其联合键入（id，索引）并且失败，而不是猜测块
            // 何时携带两者。这个规则并不明显，我们错了两次：
            // index-only键控合并了两个重用索引0的并行调用，
            // 并且空的-id重述擦除了调用的身份，因此它从未被关闭。
            // 两者都固定在
            // tests/tool_call_identity_test.cpp 上，与 接口边界 相对，
            // 而不是与 SSE 固定装置相对。
            std::string_view wire_id;
            if (tc.contains("id") && tc["id"].is_string())
                wire_id = tc["id"].get_ref<const std::string&>();

            const auto who = ctx.tools.attribute(
                wire::ChunkIdentity{wire_id, slot_index});
            if (!who) {
                // 不明确：没有id，没有索引，几个调用在飞行中。
                // 任何选择都是抛硬币，失败的一方将这些 字节
                // 附加到另一个 调用 的参数中 - 产生仍然是
                // 解析 的 JSON，因此损坏会悄悄地
                // 到达工具调用。
                //
                // 删除片段可以避免损坏，但保持沉默：剩余的参数仍然是 解析，
                // 因此回合运行一个工具，其参数丢失了 SOME，
                // 但没有任何说明。我们无法归因的块意味着飞行中的每个
                // 调用 现在都是可疑的，因此转弯失败 - zed
                // 做出相同的选择（ToolCallAccumulator::entry
                // 返回 AmbiguousToolCallChunk
                // 并且映射器将其显示为完成错误
                // 而不是最佳猜测）。举报一次：
                // 否则后面的块将各自引发它们自己的错误。
                if (!ctx.tool_attribution_failed) {
                    ctx.tool_attribution_failed = true;
                    AGT_LOG(Wire, Warn, "openai.tool_args_unattributable",
                            "calls_in_flight={}", ctx.tools.size());
                    ctx.sink(StreamError{
                        "the provider sent a tool-call fragment that names "
                        "neither an id nor an index while "
                            + std::to_string(ctx.tools.size())
                            + " calls were in flight — it cannot be placed "
                              "without risking corrupted arguments",
                        std::nullopt});
                }
                continue;
            }

            // 不同 id重用的索引意味着该索引处的前一个调用已经结束。
            // 在这里关闭它：未关闭的 调用 留下没有 tool_result
            // 的 tool_use 并挂起转牌，因此在不关闭的
            // 情况下分裂会用一个错误换取更糟糕的错误。
            if (who->displaced) {
                auto& old = ctx.tools.displaced(*who);
                if (old.started && !old.ended && !old.id.empty()) {
                    ctx.sink(StreamToolUseEnd{ToolCallId{old.id}, extra_for(ctx, old.id)});
                    old.ended = true;
                }
            }

            auto& slot = ctx.tools.at(*who);

            // Gemini的思想签名在调用上作为extra_content(并且，
            // 在一些网关上，function.thought_signature)。保留它
            // for the end event; the next request has to send it back.
            if (auto ec = tc.find("extra_content"); ec != tc.end() && ec->is_object())
                ctx.pending_extra = *ec;
            else if (tc.contains("function") && tc["function"].is_object())
                if (auto ts = tc["function"].find("thought_signature");
                    ts != tc["function"].end() && ts->is_string() && !ts->get_ref<const std::string&>().empty())
                    ctx.pending_extra = json{{"google", {{"thought_signature", *ts}}}};
            if (!ctx.pending_extra.is_null() && !slot.id.empty()) {
                ctx.tool_extra[slot.id] = std::move(ctx.pending_extra);
                ctx.pending_extra = nullptr;
            }

            std::string fn_name;
            std::string fn_args;
            if (tc.contains("function") && tc["function"].is_object()) {
                const auto& fn = tc["function"];
                if (fn.contains("name") && fn["name"].is_string())
                    fn_name = fn["name"].get<std::string>();
                if (fn.contains("arguments") && fn["arguments"].is_string())
                    fn_args = fn["arguments"].get<std::string>();
            }
            // NAME也可以以片段形式到达，而不仅仅是参数。
            //
            // 规范自己的累加器(`accumulated.function.name ??=`)
            // 采用第一个非-null并且从不追加，这对于发送一次全名的服务
            // 器是正确的。但真正的网关将其分开：`"re"`，
            // 然后 `"ad"` 为 `read`（开放代码＃24137）。
            // 仅获取第一个片段会调度一个名为 `re` 的工具，
            // 该工具会失败 "unknown tool" 3 次并终止回合。
            //
            // 所以：追加直到调用为ANNOUNCED。
            // 下面的公告门等待一个解析为真实工具的名称，
            // 这就是使附加安全的原因 - 一个服务器将整个名称预先
            // 发送到第一个片段上的匹配项，并且永远不会累积第二个片段，
            // 而一个将其拆分的服务器会一直累积直到它完成。
            //
            // 延续上的空名称是 IGNORED，从未分配：
            // 多个网关（Azure、本地代理）在参数块上re
            // -send `"name": ""`，并让它通过擦除已
            // 建立的名称（A3S-Lab/Code#139）。
            if (!fn_name.empty()) {
                if (slot.started) {
                    // 已在已解析的名称下宣布——稍后的
                    // 片段无法重命名 调用。
                } else if (slot.name.empty()) {
                    slot.name = fn_name;
                } else if (slot.name != fn_name) {
                    slot.name += fn_name;
                }
            }

            // 一旦命名了我们可以实际运行的东西就宣布。
            //
            // 等待KNOWN工具是让上面的积累终止的原因：完整的
            // 名称立即解析，部分名称则不会，我们继续收集。
            // 一个永远不会解析的名字会在回合结束时由
            // close_open_tools 宣布，因此 调用 仍然会
            // 达到模型 "unknown tool: <full name>"
            // 而不是消失。
            const bool name_is_known = [&] {
                if (ctx.known_tools.empty()) return true;   // 没有任何声明
                for (const auto& t : ctx.known_tools)
                    if (t == slot.name) return true;
                return false;
            }();
            if (!slot.started && !slot.name.empty() && name_is_known) {
                if (slot.id.empty())
                    slot.id = "call_" + std::to_string(index);
                ctx.sink(StreamToolUseStart{ToolCallId{slot.id},
                                            ToolName{slot.name}});
                slot.started = true;
            }

            if (!fn_args.empty() && slot.started) {
                // 快照是当服务器重述COMPLETE参数时（
                // 比我们所拥有的更长，或者在重传时等于它）。
                // 片段永远不会长于累积的缓冲区。
                // 这区分：
                //  (1) 快照 较长：fn_args > slot.args 和 slot.args
                //  是前缀 (2) 幂等 重复：fn_args ==
                //  slot.args 恰好 (3) 片段：fn_args.size() == 1
                //      (e.g.，来自 token-by-token 的 "{") 恰好与
                //      我们累积的开始匹配缓冲区 — 这是合法的新数据，而不是重传。
                const bool snapshot = (fn_args.size() > slot.args.size()
                                    && fn_args.compare(0, slot.args.size(), slot.args) == 0)
                                   || fn_args == slot.args;
                const auto fresh = wire::unseen(slot.args, fn_args, snapshot);
                if (!fresh.empty())
                    ctx.sink(StreamToolUseDelta{ToolCallId{slot.id},
                                                std::string{fresh}});
            }
        }
    }
}

// 主要OpenAI SSE 帧的快速路径：一旦不再进行抢救，
// 仅携带`delta.content`字符串的单一选择。这是 流式传输 回合体积的大部分。
// simdjson ondemand 使 字节 就位（在 SseFramer
// 的带衬垫的 缓冲区 之上），并发出一个 StreamTextDelta，
// 而没有 按需加载 和 nlohmann DOM。
//
// Tri-state，由于SAME原因作为Anthropic的快速路径：
// simdjson就地转义字符串，改变缓冲区，因此一旦我们读取了内容字符串，
// 调用者必须将NOT重新-parse。
//
//   无法解析——与提取的任何字符串的精确简单形状BEFORE不匹配；
//                 缓冲区 原始，调用者退回到 nlohmann。
//   已处理——发出内容增量；完毕。
//
// 需要完整路径的 ANYTHING 上的保释（→ 不可解析）：救助仍然符合资格/当前持有、
// `error`/`usage`/`finish_reason` 字段、
// `tool_calls` 增量、missing/empty/non-string 内容、
// 多个选择-relevant 字段。正确性第一——错误的 Handled
// 会丢弃工具 调用 或用法 帧。守卫是廉价的关键探测器（没有逃脱）。
static_assert(wire::kSseSimdPadding == simdjson::SIMDJSON_PADDING,
              "wire::kSseSimdPadding must equal simdjson::SIMDJSON_PADDING");
enum class FastData { Unparseable, Handled };
FastData dispatch_data_fast(StreamCtx& ctx, std::string_view data, char* padded) {
    if (!padded) return FastData::Unparseable;
    // 只有当散文流动并且没有打开leaked-tool-call保留时才是安全的。
    // 在任何其他救助状态下，handle_delta的货舱机械必须运行。
    if (ctx.holding || ctx.salvage_eligible) return FastData::Unparseable;
    // 用法帧 (stream_options.include_usage)
    // 或mid-body错误帧都需要完整路径；廉价的子字符串探针在我们甚至
    // 解析 之前就拒绝它们（这种情况很少见，因此很少支付探针成本）。
    if (data.find("\"usage\"") != std::string_view::npos
        || data.find("\"error\"") != std::string_view::npos
        || data.find("\"finish_reason\"") != std::string_view::npos
        || data.find("\"tool_calls\"") != std::string_view::npos
        // 推理文本（reasoning_content/推理）骑乘
        // PARALLEL到内容并且可以在对象中位于其之前； forward
        // -only 快速路径无法安全地查看它未到达的字段，
        // 因此将任何推理 -bearing 帧 交给 handle_delta（随机
        // -access）。探针通过通用的“推理前缀”来匹配两个字段名称。
        || data.find("\"reasoning") != std::string_view::npos
        // Mistral (Small/Medium 3.5+，reasoning_effort=
        // 高)数据流模型的链-of-thought作为STRUCTURED内容数组：
        //   delta.content = [{"type":"thinking",
        // "thinking":[{"type":"text",…}]}]
        // 并且稍后才将内容翻转为简单答案 STRING。快速路径将内容作为字符
        // 串字段读取，因此一个thinking-array 帧将被默默地丢弃（
        // 没有推理渲染，没有活跃心跳→一个长的post-tool推理过程可能会导致看门狗停顿）。
        // 一旦散文开始流动（禁用抢救），这个 帧 就会在稍后的回合/交错思维中达到快速路径，
        // 因此探测思考-part标记并将任何结构化的-content
        // 帧交给handle_delta。
        || data.find("\"thinking\"") != std::string_view::npos)
        return FastData::Unparseable;

    const std::size_t cap = data.size() + simdjson::SIMDJSON_PADDING;
    simdjson::ondemand::document doc;
    if (ctx.simd_parser.iterate(padded, data.size(), cap).get(doc))
        return FastData::Unparseable;

    simdjson::ondemand::array choices;
    if (doc.find_field_unordered("choices").get_array().get(choices))
        return FastData::Unparseable;
    auto it = choices.begin();
    if (it == choices.end()) return FastData::Unparseable;
    simdjson::ondemand::object choice;
    if ((*it).get_object().get(choice)) return FastData::Unparseable;

    simdjson::ondemand::object delta;
    if (choice.find_field_unordered("delta").get_object().get(delta))
        return FastData::Unparseable;

    // 提取内容字符串可能会转义到缓冲区——这是提交点；
    // 永远不会返回无法解析的过去。
    std::string_view content;
    if (delta.find_field_unordered("content").get_string().get(content))
        return FastData::Unparseable;
    if (!content.empty()) {
        ctx.sink(StreamTextDelta{std::string{content}});
        ctx.any_text_flushed = true;
    }
    return FastData::Handled;
}

// 处理一个 choice[0].delta 对象。
void handle_delta(StreamCtx& ctx, const json& delta);

// OpenRouter 数据流 `reasoning_details`碎片：具有相同
// `index`的条目是一个细节，并且它们的text/summary/data碎片连接在一起。
// 与 Zed 的 ReasoningDetailsAccumulator 相同的合并。
void merge_reasoning_details(json& into, const json& chunks) {
    for (const auto& c : chunks) {
        if (!c.is_object()) continue;
        json* target = nullptr;
        if (auto i = c.find("index"); i != c.end() && i->is_number_unsigned())
            for (auto& d : into)
                if (auto di = d.find("index"); di != d.end() && *di == *i) { target = &d; break; }
        if (!target) { into.push_back(c); continue; }
        for (auto it = c.begin(); it != c.end(); ++it) {
            const auto& k = it.key();
            if ((k == "text" || k == "summary" || k == "data") && it->is_string()
                && target->contains(k) && (*target)[k].is_string())
                (*target)[k] = (*target)[k].get<std::string>() + it->get<std::string>();
            else if (!it->is_null())
                (*target)[k] = *it;
        }
    }
}

// 将回合的 reasoning_details 交给 状态更新函数，并进行标记，
// 以便仅重播到 OpenRouter。
void flush_reasoning_details(StreamCtx& ctx) {
    if (ctx.reasoning_details.empty()) return;
    ctx.sink(StreamReasoning{ctx.reasoning_details.dump(), std::string{kOpenRouterReasoningSite}});
    ctx.reasoning_details = json::array();
}

// 在`msg`的最后一个文本部分上放置一个Anthropic 缓存点，
// 将纯字符串内容变成一个文本部分，以便可以携带它。
void cache_last_text(json& msg, json cache) {
    auto c = msg.find("content");
    if (c == msg.end()) return;
    if (c->is_string()) {
        if (c->get_ref<const std::string&>().empty()) return;
        *c = json::array({{{"type", "text"}, {"text", *c}, {"cache_control", std::move(cache)}}});
        return;
    }
    if (!c->is_array()) return;
    for (auto it = c->rbegin(); it != c->rend(); ++it)
        if (it->is_object() && it->value("type", "") == "text") {
            (*it)["cache_control"] = std::move(cache);
            return;
        }
}

// Claude via OpenRouter:系统对提示词缓存一小时，
// 最新的user/tool消息默认5分钟。 Anthropic 的四个中的两个。
void mark_claude_cache_points(json& messages) {
    if (!messages.is_array() || messages.empty()) return;
    if (messages[0].value("role", "") == "system")
        cache_last_text(messages[0], {{"type", "ephemeral"}, {"ttl", "1h"}});
    for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
        const auto role = it->value("role", "");
        if (role == "system") break;
        if (role == "user" || role == "tool") {
            cache_last_text(*it, {{"type", "ephemeral"}});
            break;
        }
    }
}

// 解析+分派 一个 SSE `data:` 载荷。
void dispatch_data(StreamCtx& ctx, std::string_view data) {
    if (data.empty()) return;
    if (data == "[DONE]") {
        close_open_tools(ctx);
        if (!ctx.terminated) {
            flush_reasoning_details(ctx);
            // 在 终端 事件 之前抢救泄露的工具 调用 (或
            // 将文本刷新为散文)。
            if (!try_salvage_tool_call(ctx)) flush_text_hold(ctx);
            ensure_nonempty_turn(ctx);
            ctx.sink(StreamFinished{ctx.stop_reason});
            ctx.terminated = true;
        }
        return;
    }

    json j;
    try { j = json::parse(data); } catch (...) { return; }

    // Top-level错误对象(某些服务器数据流错误帧 mid-body)。
    if (j.contains("error")) {
        // SSOT与下面的HTTP-status路径相同：dialect::error_message
        // ()知道该传输使用的每个信封，因此两个站点都不会重新-derives它。
        std::string msg = dialect::error_message()(j).value_or("unknown error");
        ctx.sink(StreamError{msg, std::nullopt});
        ctx.terminated = true;
        return;
    }
    // 裸错误形状({"code":500，"message":"…"，
    // "type":"…"})-llama.cpp在某些版本上发出该WITHOUT
    // "error"包装器。没有选择的 帧、数字错误代码 ≥400 和消息是失败，
    // 而不是增量；默默地丢弃它是 local-model 死循环的一部分。
    // 守卫：真实OpenAI-dialect 帧始终携带"object"(e.g.
    // "chat.completion.chunk") —它的存在意味着内容，
    // 而不是错误，但是其他键看起来是错误-shaped。
    if (!j.contains("choices") && !j.contains("object")
        && j.contains("message")
        && j["message"].is_string() && j.contains("code")
        && j["code"].is_number_integer()
        && j["code"].get<int>() >= 400) {
        StreamError err{j["message"].get<std::string>(), std::nullopt};
        const int code = j["code"].get<int>();
        if (code <= 599) err.http_status = code;   // 带类型的分类
        ctx.sink(std::move(err));
        ctx.terminated = true;
        return;
    }

    // 使用可以到达最后的帧(当设置stream_options.include_usage
    // 时)OR被附加到最后的选择帧。共享提取器 - 请参阅
    // usage::from_openai（OpenAI-compat 形状的单一事实来源）。
    if (j.contains("usage")) {
        if (auto su = usage::from_openai(j["usage"])) ctx.sink(*su);
    }

    if (!j.contains("choices") || !j["choices"].is_array()
        || j["choices"].empty()) {
        return;
    }
    const auto& choice = j["choices"][0];

    if (choice.contains("delta") && choice["delta"].is_object())
        handle_delta(ctx, choice["delta"]);

    // finish_reason终止选择。将其隐藏为 StreamFinished 并关闭
    // 所有打开的工具 调用。我们确实 不 在这里发出 StreamFinished -
    // `[DONE]` 哨兵（或 emit_terminal 在 数据流 关闭）发出，
    // 所以 finish_reason 之后的使用 帧 仍然有效。
    //
    // 如果我们已经抢救了泄露的工具调用 (stop_reason==ToolUse)，
    // 不要让服务器的"stop"覆盖它——弱本地模型报告
    // finish_reason =停止，即使它们在内容中将工具调用发出为JSON。
    if (choice.contains("finish_reason") && choice["finish_reason"].is_string()) {
        auto server_reason = parse_openai_finish(
            choice["finish_reason"].get<std::string_view>());
        if (ctx.stop_reason != StopReason::ToolUse)
            ctx.stop_reason = server_reason;
        close_open_tools(ctx);
    }
}

// SSE换行器。 OpenAI 数据流 `data: {json}\n\n`
// 帧（没有`event:`行），所以解析器比Anthropic简单：累积
// `data:`行，分派在空白-line终止符上。框架本身是共享的wire::
// SseFramer； OpenAI 只是忽略（始终-empty）事件 名称。
void feed_sse(StreamCtx& ctx, const char* data, size_t len) {
    ctx.sse.feed(data, len, [&](std::string_view event, std::string_view payload,
                                char* padded) {
        // llama.cpp (和一些代理）将 -stream 中的故障作为 NAMED SSE
        // 事件 — `event: error` + JSON 正文 — 在 HTTP 200 下传送。
        // OpenAI 本身从未命名 事件，因此旧的 解析器 完全忽略了该名称：错误
        // 帧 落到了 dispatch_data，其 j.contains(
        // "error") 检查仅捕获 {"error":…} TOP-LEVEL
        // 形状 — llama.cpp 的named-event 正文 是裸露的错误
        // 对象（{"code"：500，"message"：…，"type"：…}），
        // 所以它默默地DROPPED：然后数据流关闭干净，
        // 轮到制造"(empty response)"，重试机器永远re-fired -
        // local-model死循环。将其表面化为真正的 StreamError。
        if (event == "error" && !ctx.terminated) {
            std::string msg{payload.empty() ? std::string_view{"stream error"}
                                            : payload};
            int code = 0;   // 数字错误代码 → 带类型的 分类
            try {
                auto j = json::parse(payload);
                if (j.is_object()) {
                    // 信封形状存在于 dialect::error_message
                    // () — SSOT 中，用于说明此规范 -less 传输 如何拼写错误。
                    // 存在三个（{"error":{"message"}}、
                    // 被某些网关扁平化的 {"error":"…"} 以及 llama.cpp
                    // 的裸露的 {"message","code"}），
                    // 并且一个无法识别的向用户显示为
                    // 模型无缘无故停止了。
                    // 这是最糟糕的诊断。 解析 这里的它们也将是与第一个列表不同的第二个部分列表。
                    if (auto m = dialect::error_message()(j)) msg = std::move(*m);
                    // llama.cpp在`code`中标记would-be
                    // HTTP状态(500模板失败、400错误请求、503忙)。
                    // 将其带入 StreamError.http_status 会
                    // 通过 TYPED 分类器路由 状态更新函数 — 如果没有它，
                    // 字符串嗅探可能会将确定性 per-request 故障 (
                    // "failed to read connection") 误归档为瞬态和
                    // re-loop。
                    if (j.contains("code") && j["code"].is_number_integer())
                        code = j["code"].get<int>();
                    else if (j.contains("error") && j["error"].is_object()
                             && j["error"].contains("code")
                             && j["error"]["code"].is_number_integer())
                        code = j["error"]["code"].get<int>();
                }
            } catch (...) { /* 保留原始载荷作为消息 */ }
            StreamError err{std::move(msg), std::nullopt};
            if (code >= 400 && code <= 599) err.http_status = code;
            ctx.sink(std::move(err));
            ctx.terminated = true;
            return;
        }
        // [DONE]和空的帧直接走完整路径（终端处理住在那里）。
        // 否则尝试 content-delta 快速路径；
        // 对于需要 salvage/usage/tool 处理的任何内容，它都会返回 Unparseable
        // - 使 `payload` 保持原始状态。
        if (!payload.empty() && payload != "[DONE]"
            && dispatch_data_fast(ctx, payload, padded) == FastData::Handled)
            return;
        dispatch_data(ctx, payload);
    });
}

// 如果辅助消息携带任何tool_calls(其结果必须遵循OpenAI
// 格式的`role:"tool"`消息)，则为真。
[[nodiscard]] bool is_assistant_with_results(const Message& m) noexcept {
    return wire::is_assistant_with_results(m);
}

// ── Ollama 原产地/ api/chat 协议 ────────────────────────────────────────
// OpenAI-compat垫片(/v1/chat/completions)使弱
// 本地模型泄漏工具调用成为`content`中的原始JSON。本机端点应用模型的聊天模板，
// 返回结构化的 `message.tool_calls`，并通过简单的问候语干净地聊天。
// 请求 正文 形状不同（消息带有 tool_calls 内联，工具结果为角色："tool"），
// 响应为 NDJSON：每行一个 JSON 对象，
// 每个 `{"message":{...},"done":bool}`。

// 构建本机`messages`阵列。类似于 OpenAI 形状，但 Ollama
// 希望工具参数为 JSON OBJECT（不是序列化字符串），工具结果为角色：
// "tool" 和 `tool_name`。
[[nodiscard]] json build_native_messages(const std::vector<Message>& msgs) {
    json arr = json::array();
    // 对终端-carrying工具结果进行计数，以便可以为每个结果分配共享age
    // -tiered 传输预算的新近度等级（0=最新）。
    // 请参阅 wire::cap_tool_result_aged — 旧转储逐渐褪色为
    // 紧头+末尾，因此从 30 调用 之前读取的 60 KiB 会停止每轮完整重播。
    int total_tool_results = 0;
    for (const auto& m : msgs)
        if (is_assistant_with_results(m))
            total_tool_results += static_cast<int>(m.tool_calls.size());
    int tool_results_emitted = 0;
    const auto superseded = wire::superseded_read_ids(msgs);
    for (const auto& m : msgs) {
        const bool has_text  = !m.text.empty();
        const bool has_tools = is_assistant_with_results(m);
        bool has_images = false;
        if (m.role == Role::User)
            for (const auto& img : m.images)
                if (!img.bytes().empty()) { has_images = true; break; }

        if (has_text || has_images || has_tools) {
            json msg;
            msg["role"] = (m.role == Role::User) ? "user" : "assistant";
            std::string wire_text = m.attachments.empty()
                ? m.text : attachment::expand(m.text, m.attachments);
            msg["content"] = scrub_utf8(wire_text);
            if (has_images) {
                // Ollama本机：图像是base64字符串的数组。
                json imgs = json::array();
                for (const auto& img : m.images)
                    if (!img.bytes().empty())
                        imgs.push_back(img.base64());
                if (!imgs.empty()) msg["images"] = std::move(imgs);
            }
            if (has_tools) {
                json calls = json::array();
                for (const auto& tc : m.tool_calls) {
                    calls.push_back({
                        {"function", {
                            {"name", tc.name.value},
                            {"arguments", tc.args.is_null() ? json::object()
                                                            : tc.args},
                        }},
                    });
                }
                msg["tool_calls"] = std::move(calls);
            }
            arr.push_back(std::move(msg));
        }
        if (has_tools) {
            for (const auto& tc : m.tool_calls) {
                const int recency_rank = total_tool_results - 1 - tool_results_emitted;
                ++tool_results_emitted;
                std::string out = tc.output();
                bool is_error = !tc.is_terminal() || tc.is_failed() || tc.is_rejected();
                if (out.empty()) {
                    if (tc.is_rejected())       out = "(rejected by user)";
                    else if (!tc.is_terminal()) out = "(no output)";
                } else if (!is_error && superseded.count(tc.id.value)) {
                    out = std::string{wire::kSupersededReadPointer};
                } else {
                    out = wire::cap_tool_result_aged(out, recency_rank, is_error);
                }
                arr.push_back({
                    {"role", "tool"},
                    {"tool_name", tc.name.value},
                    {"content", scrub_utf8(out)},
                });
            }
        }
    }
    return arr;
}

// 处理一个本机消息增量（NDJSON 帧的`message`对象）。
void handle_native_message(StreamCtx& ctx, const json& message) {
    // 结构化工具调用获胜——在本机端点上不需要补救。
    if (message.contains("tool_calls") && message["tool_calls"].is_array()
        && !message["tool_calls"].empty()) {
        ctx.any_structured_tool = true;
        ctx.holding = false;
        ctx.text_hold.clear();
        int idx = 0;
        for (const auto& tc : message["tool_calls"]) {
            std::string name, args = "{}";
            if (tc.contains("function") && tc["function"].is_object()) {
                const auto& fn = tc["function"];
                if (fn.contains("name") && fn["name"].is_string())
                    name = fn["name"].get<std::string>();
                if (fn.contains("arguments")) {
                    const auto& a = fn["arguments"];
                    if (a.is_string())      args = a.get<std::string>();
                    else if (!a.is_null())  args = a.dump();
                }
            }
            if (name.empty()) continue;
            std::string id = "call_native_"
                + std::to_string(ctx.salvage_seq++) + "_"
                + std::to_string(idx++);
            ctx.sink(StreamToolUseStart{ToolCallId{id}, ToolName{name}});
            ctx.sink(StreamToolUseDelta{ToolCallId{id}, args});
            ctx.sink(StreamToolUseEnd{ToolCallId{id}});
            ctx.stop_reason = StopReason::ToolUse;
        }
    }
    // 助理文本。 Ollama 的原生 /api/chat 是 SUPPOSED，
    // 用于将工具 调用 路由到上面的结构化 tool_calls[] 通道中 - 但
    // qwen2.5-coder 和朋友发出一个裸露的 {"name":..,
    // "arguments":..}，与聊天模板的 <tool_call> 包装
    // 器不匹配，因此 Ollama 将其留在 `content` 中。如果没有打捞，
    // 这些型号就无法使用 调用 任意 工具。因此，我们通过与 OpenAI-compat
    // 路径相同的 hold/salvage 机器运行内容：执行命名 ADVERTISED
    // 工具的完整 JSON 对象；问候语泄漏/未知-tool / 内存工具被删除（从未
    // 显示为原始JSON，从未循环）。 handle_delta 是唯一的事实来源。
    if (message.contains("content") && message["content"].is_string()) {
        const auto& s = message["content"].get_ref<const std::string&>();
        // Feed ONLY的内容字段——结构化的tool_calls在
        // 上面进行了处理；传递整个消息将使他们加倍-process。
        if (!s.empty()) handle_delta(ctx, json{{"content", s}});
    }
}

// 解析 来自 /api/chat. 的 1 条 NDJSON 线
void dispatch_native_line(StreamCtx& ctx, std::string_view line) {
    if (line.empty()) return;
    json j;
    try { j = json::parse(line); } catch (...) { return; }
    if (j.contains("error")) {
        std::string msg = j["error"].is_string()
            ? j["error"].get<std::string>() : j["error"].dump();
        ctx.sink(StreamError{msg, std::nullopt});
        ctx.terminated = true;
        return;
    }
    if (j.contains("message") && j["message"].is_object())
        handle_native_message(ctx, j["message"]);
    if (j.value("done", false)) {
        // 最后的帧携带用途+done_reason。共享提取器 — 请参阅
        // usage::from_ollama。
        if (auto su = usage::from_ollama(j)) ctx.sink(*su);
        auto reason = j.value("done_reason", std::string{"stop"});
        if (ctx.stop_reason != StopReason::ToolUse)
            ctx.stop_reason = (reason == "length") ? StopReason::MaxTokens
                                                   : StopReason::EndTurn;
    }
}

// 用于/api/chat的NDJSON换行器(换行-delimited JSON，而不是SSE)。
void feed_ndjson(StreamCtx& ctx, const char* data, size_t len) {
    ctx.ndjson.feed(data, len, [&](std::string_view line) {
        dispatch_native_line(ctx, line);
    });
}

} // 命名空间结束。

// 该端点真的使用Ollama的本机/api/*协议吗？
// 请参阅 头文件。无法访问被视为本机，因此繁忙的守护进程不会降级。
namespace {
using NativeCache = std::map<std::string, bool, std::less<>>;
maya::snapshot<NativeCache>& native_cache() {
    static maya::snapshot<NativeCache> c;
    return c;
}
std::string native_key(const Endpoint& ep) {
    return ep.host + ":" + std::to_string(ep.port);
}
} // 命名空间结束。

std::optional<bool> endpoint_speaks_native_cached(const Endpoint& ep) {
    if (!ep.native_api) return false;
    auto snap = native_cache().load();
    if (const auto it = snap->find(native_key(ep)); it != snap->end())
        return it->second;
    return std::nullopt;
}

void set_speaks_native_for_test(const Endpoint& ep, bool native) {
    native_cache().update([](const NativeCache& m, const std::string& k, const bool& v) {
        NativeCache next = m;
        next.insert_or_assign(k, v);
        return next;
    }, native_key(ep), native);
}

bool endpoint_speaks_native(Io io, const Endpoint& ep) {
    if (!ep.native_api) return false;
    if (const auto known = endpoint_speaks_native_cached(ep)) return *known;
    auto& cache = native_cache();
    const std::string key = native_key(ep);

    http::Request probe;
    probe.method    = http::HttpMethod::Get;
    probe.host      = ep.host;
    probe.port      = ep.port;
    probe.path      = "/api/tags";
    probe.plaintext = !ep.use_tls;
    http::Timeouts tos;
    tos.connect = std::chrono::milliseconds(2'000);
    tos.total   = std::chrono::milliseconds(3'000);

    auto resp = http::default_client(io).send(probe, tos);
    const bool native = !resp || resp->status == 200;
    if (!native)
        AGT_LOG(Wire, Info, "openai",
                "{}:{} has no /api/tags — treating it as OpenAI-compatible",
                ep.host, ep.port);

    cache.update([](const NativeCache& m, const std::string& k, const bool& v) {
        NativeCache next = m;
        next.insert_or_assign(k, v);
        return next;
    }, key, native);
    return native;
}

// ── 端点预设 ─────────────────────────────────────────────────────────
Endpoint Endpoint::from_spec(std::string_view spec) {
    auto eq = [](std::string_view a, const char* b) {
        return a == std::string_view{b};
    };
    if (eq(spec, "codex")) {
        // "codex" 是 LEGACY ALIAS：它曾经是它自己的 "Codex API"
        // 选择面板 行，但那是 字节-identical 到 `openai`（相同的主机，
        // 相同的 /v1/chat/completions，相同的承载身份验证 -
        // 只有密钥环境变量不同）。该行被删除，CODEX_API_KEY 折叠到
        // `openai` 预设的 auth_env 中；规范仍然在这里解析，
        // 因此 `--provider codex` 和持久配置继续工作。真正不同的 Codex
        // 路径是 `chatgpt`（通过 ChatGPT OAuth 响应 API）。
        // 通过注册表解决，因此它不能偏离`openai`。
        spec = "openai";
    }
    // 空规格 = 默认 OpenAI-family 主机，像所有其他预设
    // 一样通过 注册表 解析（没有其 host/path 的第二个副本）。
    if (spec.empty()) spec = "openai";
    if (eq(spec, "chatgpt") || eq(spec, "codex-cli")) {
        // 本机ChatGPT路径从不拨打该端点：
        // main.cpp将该标签分派到chatgpt::ChatGptProvider。
        // 保留本地-shaped哨兵，以便通用OpenAI
        // -family选择不变量保持真实。标签被规范化为"chatgpt"；
        // "codex-cli" 仍然被接受为旧规范（持久配置，
        // `--provider codex-cli`），但标准化为相同的端点。
        return Endpoint{"localhost", 0, "/", "/", false, "chatgpt"};
    }
    // NOTE `copilot`从下面的注册表行解析。本机 Copilot
    // 路径 (copilot::CopilotProvider) 在请求时从令牌交换
    // 的 `endpoints.api` 覆盖主机（Individual/Business/
    // Enterprise 均不同）；该行是哨兵+标签。

    // ── 预设行:一个 向供应商注册表查询 ──────────────
    // 这曾经是14-arm if-chain，re-stated
    // host/path事实上注册表行已经有一半-carried。
    // 两者发生了漂移（`openai` 行声称
    // Wire::OpenAIResponses，而此函数拨打 /v1/chat/completions），
    // 因此端点列现在位于该行上，这是它们的纯粹投影。添加提供者只是触及
    // 桌面——这里没有任何手臂可以忘记。
    if (const auto* row = provider::preset_for(spec);
        row && row->http_dialled()) {
        Endpoint ep{std::string{row->host}, row->port, std::string{row->path},
                    std::string{row->models_path}, row->use_tls,
                    std::string{row->id}};
        ep.native_api = row->native_api;
        ep.prompt_cache_key = row->accepts_prompt_cache_key;
        // 当行要求时说出我们自己的名字（今天的 OpenRouter）。
        // 响应路径使用相同的构建器，因此两种方言都会记入一个应用程序页面。
        // 乘坐 extra_headers，
        // build_request_headers 已附加在聊天和模型 调用 上。
        ep.extra_headers = provider::attribution_headers(row);
        return ep;
    }

    if (eq(spec, "llama.cpp")) {
        // 不是预设行（llama.cpp/vLLM/LM Studio 服务器只是通用
        // OpenAI-compatible 主机 — 请参阅 注册表 NOTE），
        // 但保留为方便别名：/v1 上的 OpenAI REST 方言（不 Ollama 的本
        // 机 /api/chat），端口 8080，无身份验证，普通HTTP（本地主机）。
        return Endpoint{"localhost", 8080, "/v1/chat/completions",
                         "/v1/models", false, "llama.cpp"};
    }
    // 完整的URL形式："https://host[:port][/path]"或"http://host[:port][/path]"。
    // 该路径是BASE PREFIX-agentty向其附加/chat/completions和/models，
    // 因此"https://gw.example.com/api"→路径"/api/chat/completions"。
    // 在scheme://authority之后没有路径→空前缀→"/chat/completions"。
    // 这允许用户到达不在/v1上提供服务的服务器（
    // e.g.代理到/api/chat/completions的网关）。
    // 裸 "host[:port]" 规格（无方案）落入下面的旧版 处理函数 并保留 /v1 默认值。
    if (spec.starts_with("https://") || spec.starts_with("http://")) {
        Endpoint ep;
        ep.label = std::string{spec};
        std::string_view s{spec};
        // 可选的"#name" FRAGMENT:命名该条目，以便SAME端点上的
        // 几个帐户保持不同("https://ollama.com/v1#work"
        // 和"…/v1#personal"是两个设置键→两个API键、两个已保存型号、
        // 两个选择面板行)。该片段是一个纯粹的本地标签 - 在这里被剥离，
        // 因此它永远不会到达 传输；保存在 `label` 中，
        // 以便 rows/badges 显示它。
        if (auto hash = s.rfind('#'); hash != std::string_view::npos)
            s = s.substr(0, hash);
        const bool tls = spec.starts_with("https://");
        ep.use_tls = tls;
        s.remove_prefix(tls ? 8 : 7);   // "https://"=8，"http://"=7
        ep.port = tls ? 443 : 80;

        // 从第一个'/'处的路径中分离权限。
        auto slash = s.find('/');
        std::string_view authority = (slash == std::string_view::npos)
            ? s : s.substr(0, slash);
        // 剥离userinfo:"user:pass@host"拨打`host`，
        // 而不是整个字符串。保留下来，它变成了 SNI 名称和 Host 头文件，因此 TLS
        // 因名称不匹配而失败，读取为 "certificate problem"
        // 而不是 "your URL has credentials in it"。
        if (auto at = authority.rfind('@'); at != std::string_view::npos)
            authority = authority.substr(at + 1);

        std::string prefix;   // 基本路径前缀，可以为空
        if (slash != std::string_view::npos) {
            prefix = std::string{s.substr(slash)};
            // 删除？query和#fragment——都不属于基本路径。
            // 没有这个，"/v1?key=x"产生路径
            // "/v1?key=x/chat/completions"，每个服务器404s。
            if (auto q = prefix.find_first_of("?#"); q != std::string::npos)
                prefix.resize(q);
            // 剥离尾随'/'，因此"/api/"→"/api"，以及"/"→""。
            while (prefix.size() > 1 && prefix.back() == '/')
                prefix.pop_back();
            if (prefix == "/") prefix.clear();
            // 折叠重复斜杠：手-edited或串联的URL产生"//v1"，
            // 并且"//v1/chat/completions"是到大
            // 多数路由器的不同路径。
            for (std::size_t i = 1; i + 1 <= prefix.size();) {
                if (prefix[i] == '/' && prefix[i - 1] == '/') prefix.erase(i, 1);
                else ++i;
            }
            // 该 一个 USERS ACTUALLY HIT。文档说路径是
            // PREFIX，我们附加 /chat/completions — 但
            // 提供商提供给您以及您因此粘贴的内容是完整端点 URL。
            // 添加到此后会得到
            // "/v1/chat/completions/chat/completions"：
            // 每个请求都会返回 404，而 URL 显然是正确的。
            //
            // 因此接受两种拼写。已经在端点的完成路径
            // IS 中结束的前缀；取出它之前的部分。
            for (std::string_view tail : {"/chat/completions", "/completions"}) {
                if (prefix.size() > tail.size()
                    && std::string_view{prefix}.ends_with(tail)) {
                    prefix.resize(prefix.size() - tail.size());
                    break;
                }
            }
        }
        // BARE URL → /v1 DEFAULT。此端点使用 OpenAI 方言，
        // 并且该方言到处都位于 /v1 下 — api.openai.com/v1、
        // llama.cpp、vLLM、LM Studio、Ollama 的兼容
        // shim 都服务于 /v1/chat/completions； OpenAI SDK
        // 约定是 base_url CONTAINS /v1。
        // 从 "http://host:8080/" 派生出裸露的 "/chat/completions"
        // 会产生一条几乎没有服务器响应的路径 - 用户在每个请求上都会收到 404（或更糟糕的是，
        // 旧的 解析器 丢弃的流式错误）并将其报告为 "the model locks"。
        // EXPLICIT路径("/api"，"/openai/v1")总是
        // 逐字保留，因此不寻常的网关保持可表达。
        if (prefix.empty()) prefix = "/v1";

        // 将主机与端口分离。 IPv6 文字用方括号括起来 — "[::1]"
        // 或 "[::1]:8080" — 因此端口冒号是 之后 "]"，
        // 而不是地址内的冒号。对于普通主机，端口位于最后一个 ':' 之后。
        if (!authority.empty() && authority.front() == '[') {
            auto close = authority.find(']');
            if (close != std::string_view::npos) {
                ep.host = std::string{authority.substr(1, close - 1)};  // 剥离[ ]
                auto rest = authority.substr(close + 1);                // "" or ":8080"
                if (rest.size() > 1 && rest.front() == ':') {
                    try {
                        int port_int = std::stoi(std::string{rest.substr(1)});
                        ep.port = (port_int > 0 && port_int <= 65535)
                            ? static_cast<std::uint16_t>(port_int)
                            : (tls ? 443 : 80);
                    } catch (...) { ep.port = tls ? 443 : 80; }
                }
            } else {
                ep.host = std::string{authority};  // 畸形；拨号为-is
            }
        } else if (auto colon = authority.rfind(':');
                   colon != std::string_view::npos) {
            ep.host = std::string{authority.substr(0, colon)};
            try {
                int port_int = std::stoi(std::string{authority.substr(colon + 1)});
                ep.port = (port_int > 0 && port_int <= 65535)
                    ? static_cast<std::uint16_t>(port_int)
                    : (tls ? 443 : 80);
            }
            catch (...) { ep.port = tls ? 443 : 80; }
        } else {
            ep.host = std::string{authority};
        }

        // 空主机(e.g."https:///path"或
        // "https://:443/")→回退到默认端点而不是拨打空主机。
        if (ep.host.empty()) return Endpoint{};

        // 主机名是 case-insensitive (RFC 4343)，
        // 但我们在几个地方将它们作为字符串进行比较 — SNI、
        // Host 头文件 以及在端点上键入的 per-provider 凭据 查找。
        // "API.Foo.com" 和 "api.foo.com" 是同一台服务器，
        // 大写的用户不应获得第二个 key-less 身份。
        for (char& c : ep.host)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        ep.path       = prefix + "/chat/completions";
        ep.models_path = prefix + "/models";
        return ep;
    }
    // 将其他任何内容视为原始 "host[:port]" — 默认为 443 上的
    // https，如果给定非 443 端口则为纯 http（本地服务器约定）。
    // 标签携带原始规格，因此模型徽章/提供商读数显示实际端点
    // （"my-box.lan:8080")而不是通用
    // 占位符。
    Endpoint ep;
    ep.label = std::string{spec};
    std::string s{spec};
    // "#name"片段：本地multi-account标签，从未拨打过（参见上面的
    // URL分支）。剥离 之前 端口拆分为 "host:8080#work" 解析 端口，
    // 而不是 "8080#work"。
    if (auto hash = s.rfind('#'); hash != std::string::npos)
        s.resize(hash);
    // 与URL臂相同的标准化：裸规格可以携带用户信息（
    // "user@my-box.lan:8080")并且可以大写。
    // 两者都达到相同的拨号代码，因此两者都需要相同的清理——让一只手臂
    // 接受另一只手臂拒绝的形状就是它自己的错误报告。
    if (auto at = s.rfind('@'); at != std::string::npos)
        s.erase(0, at + 1);
    if (auto colon = s.rfind(':'); colon != std::string::npos) {
        ep.host = s.substr(0, colon);
        try {
            int port_int = std::stoi(s.substr(colon + 1));
            ep.port = (port_int > 0 && port_int <= 65535)
                ? static_cast<std::uint16_t>(port_int)
                : 443;   // 超出范围→回退到https默认值
        }
        catch (...) { ep.port = 443; }
        ep.use_tls = (ep.port == 443);
    } else {
        ep.host = std::move(s);
        ep.port = 443;
        ep.use_tls = true;
    }
    for (char& c : ep.host)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ep;
}

// ── 信件阵列( OpenAI 形状) ───────────────────────────────────────────
//
// 与Anthropic的区别：
//   ·系统提示词是头部的`role:"system"`消息，而不是顶部-level
//     `system`字段——在run_stream_sync中处理，不在此处。
//   ·辅助工具调用在辅助消息上直播
//     `tool_calls:[{id, type:"function", function:{name, arguments}}]`。
//   ·Tool结果是SEPARATE `role:"tool"`消息，具有
//     `tool_call_id` —每个 调用 一个，
//     在请求它们的助理消息之后立即发出。
//   ·图像：OpenAI使用`内容：[{type:"image_url"，image_url:{url:
//     "data:<mime>;base64,<...>"}}]`。
json build_messages(const Thread& t) { return build_messages(t, 0); }

json build_messages(const Thread& t, std::uint8_t reasoning_echo) {
    const auto echo = static_cast<provider::ReasoningEcho>(reasoning_echo);
    json arr = json::array();
    // 共享age-tiered 传输预算的新近度排名（0=最新工具结果）。与 Anthropic
    // 相同的政策 — 请参阅 wire::cap_tool_result_aged。
    int total_tool_results = 0;
    for (const auto& m : t.messages)
        if (is_assistant_with_results(m))
            total_tool_results += static_cast<int>(m.tool_calls.size());
    int tool_results_emitted = 0;
    // 较早的读取其文件的稍后回合re-read/edited被折叠为one-line指针而不是它们的完整正文
    // ——参见wire::superseded_read_ids。
    const auto superseded = wire::superseded_read_ids(t);
    const unsigned max_side = util::wire_max_side(wire::wire_image_count(t.messages));
    for (const auto& m : t.messages) {
        const bool has_text   = !m.text.empty();
        // 跳过空的-bytes图像（泄漏到错误的线程中的已耗尽
        // 的草稿附件）——没有载荷的"data:...;base64,"
        // 使得服务器拒绝请求。
        bool has_images = false;
        if (m.role == Role::User)
            for (const auto& img : m.images)
                if (!img.bytes().empty()) { has_images = true; break; }
        const bool has_tools  = is_assistant_with_results(m);

        if (has_text || has_images || has_tools) {
            json msg;
            msg["role"] = (m.role == Role::User) ? "user" : "assistant";

            // 将 标签 占位符展开到 传输 的 正文 中。
            std::string wire_text = m.attachments.empty()
                ? m.text
                : attachment::expand(m.text, m.attachments);
            wire_text = scrub_utf8(wire_text);

            if (has_images) {
                // 多模式内容阵列（文本+image_url部分）。
                json content = json::array();
                if (!wire_text.empty())
                    content.push_back({{"type", "text"}, {"text", wire_text}});
                for (const auto& img : m.images) {
                    if (img.bytes().empty()) continue;
                    std::string url = "data:" + img.media_type + ";base64,"
                                    + img.base64();
                    content.push_back({{"type", "image_url"},
                                       {"image_url", {{"url", url}}}});
                }
                msg["content"] = std::move(content);
            } else {
                // 纯字符串内容。即使辅助消息是纯粹的 tool_calls，
                // OpenAI 也需要 `content` 存在 —
                // 在这种情况下使用空字符串（也接受 null，
                // 但 "" 在兼容服务器上更安全）。
                msg["content"] = wire_text;
            }

            if (has_tools) {
                json calls = json::array();
                for (const auto& tc : m.tool_calls) {
                    json fn;
                    fn["name"] = tc.name.value;
                    // OpenAI想要参数作为STRING (序列化JSON)。
                    fn["arguments"] = tc.args.is_null()
                        ? std::string{"{}"}
                        : tc.args.dump();
                    json call = {
                        {"id", tc.id.value},
                        {"type", "function"},
                        {"function", std::move(fn)},
                    };
                    // Gemini要求其思想签名返回到携带它的调用上，
                    // 或者下一个请求400秒。
                    if (tc.extra_content.is_object() && !tc.extra_content.empty())
                        call["extra_content"] = tc.extra_content;
                    calls.push_back(std::move(call));
                }
                msg["tool_calls"] = std::move(calls);
            }
            // 将回合的推理发送回其到达的字段下，如 GitHub 的客户端所做的那样
            // （runtime.node，chat-completions
            // 编码器：per-message reasoning_wire_field）。
            // 没有记录字段的消息（另一种方言，或没有推理）不会得到任何结果。
            if (!m.reasoning_wire_field.empty() && !m.thinking.empty())
                msg[m.reasoning_wire_field] = scrub_utf8(m.thinking);
            // OpenRouter的reasoning_details从这一轮开始，
            // 回到他们来时的样子(Gemini认为签名必须如此，否则下一个调用
            // 就会失去模型的思想链)。仅铸造了 OpenRouter。
            if (m.role == Role::Assistant && m.reasoning_site == kOpenRouterReasoningSite
                && !m.reasoning_encrypted.empty()) {
                json rd = json::parse(m.reasoning_encrypted, nullptr, false);
                if (rd.is_array() && !rd.empty()) msg["reasoning_details"] = std::move(rd);
            }
            // 一种需要在较早的回合中进行推理的模型，每回合 400 秒，
            // 而没有推理（#117）：产生的另一个模型，或者它没有推理的模型。
            // 在该模型要求的字段下发送该回合确实具有的推理（另
            // 一个模型的想法，或内嵌在 <think> 标签中的文本）；
            // 否则留一空格。空字符串是不够的：网关检查非
            // -empty 值（9router#4804，
            // 在第 5 圈和第 41 圈进行测试）。
            const bool echo_turn = m.role == Role::Assistant
                && (echo == provider::ReasoningEcho::All
                    || (echo == provider::ReasoningEcho::ToolCalls && has_tools));
            if (echo_turn) {
                auto it = msg.find("reasoning_content");
                const bool present = it != msg.end() && it->is_string()
                                     && !it->get_ref<const std::string&>().empty();
                if (!present) {
                    std::string r = m.reasoning_wire_field.empty()
                                        ? scrub_utf8(m.thinking) : std::string{};
                    // 发送其推理为 `reasoning` 的
                    // 轮次已在 传输 上；重复而不是发明一个。
                    if (r.empty())
                        if (auto o = msg.find("reasoning");
                            o != msg.end() && o->is_string())
                            r = o->get<std::string>();
                    msg["reasoning_content"] = r.empty() ? std::string{" "}
                                                         : std::move(r);
                }
            }
            arr.push_back(std::move(msg));
        }

        // Tool结果作为单独的角色："tool"消息。
        if (has_tools) {
            // OpenAI聊天仅允许角色中的文本："tool"消息，
            // 因此工具返回的图像（在屏幕截图上读取）遵循本轮的
            // 每个工具结果作为ONE用户消息。工具消息在助手
            // 调用 之后保持连续，这是 API 所需要的。
            json tool_images = json::array();
            for (const auto& tc : m.tool_calls) {
                const int recency_rank = total_tool_results - 1 - tool_results_emitted;
                ++tool_results_emitted;
                // 发送我们拥有的任何输出； non-terminal 调用（在
                // OpenAI 路径上罕见）仍然需要配对工具消息或在未应答的
                // tool_call_id 上的下一个请求 400 秒。
                std::string out = tc.output();
                bool is_error = !tc.is_terminal() || tc.is_failed() || tc.is_rejected();
                if (out.empty()) {
                    if (tc.is_rejected())      out = "(rejected by user)";
                    else if (!tc.is_terminal()) out = "(no output)";
                } else if (!is_error && superseded.count(tc.id.value)) {
                    out = std::string{wire::kSupersededReadPointer};
                } else {
                    out = wire::cap_tool_result_aged(out, recency_rank, is_error);
                }
                arr.push_back({
                    {"role", "tool"},
                    {"tool_call_id", tc.id.value},
                    {"content", scrub_utf8(out)},
                });
                if (is_error || superseded.count(tc.id.value)) continue;
                for (const auto* imgp : wire::wire_tool_result_images(tc, max_side)) {
                    if (tool_images.empty())
                        tool_images.push_back({{"type", "text"},
                            {"text", "Images returned by the tool calls above:"}});
                    tool_images.push_back({{"type", "image_url"},
                        {"image_url", {{"url", "data:" + std::string{wire::wire_media_type(*imgp)}
                                                + ";base64," + imgp->base64()}}}});
                }
            }
            if (!tool_images.empty())
                arr.push_back({{"role", "user"}, {"content", std::move(tool_images)}});
        }
    }
    return arr;
}

// ── 头文件 建造器 ───────────────────────────────────────────────────────────
// "user"，当最新消息是用户带类型的时，否则"agent"
// (工具结果轮、子代理、压缩）。
std::string initiator_for(const std::vector<Message>& msgs) {
    for (auto it = msgs.rbegin(); it != msgs.rend(); ++it) {
        if (it->role == Role::Assistant) return "agent";
        if (it->typed_by_user()) return "user";
    }
    return "agent";
}

http::Headers build_request_headers(const AuthHeader& auth,
                                    const Endpoint& endpoint) {
    http::Headers h;
    h.push_back({"accept", "application/json"});
    h.push_back({"content-type", "application/json"});
    h.push_back({"user-agent", ::agentty::user_agent()});
    // Ask代表UNCOMPRESSED 正文。 agentty 的 HTTP 客户端不会
    // 解码内容 -encodings（没有 gzip/deflate/br 膨胀器），
    // 因此网关对其 JSON 响应进行 gzip 压缩 — z.ai 的 GLM 编码计划在
    // /models 上执行此操作，即使没有发送 Accept-Encoding — 向我们提供 字节，
    // 我们不能 解析：模型列表返回为空，并且选择面板 没有显示任何内容（问题#30）。
    // 流式传输路径已经经由append_sse_no_buffer强制`identity`；
    // 发出 此处 使得每个 OpenAI-family 请求（型号列表、
    // /api/show、/v1 探针、数据流）都一致安全 - 没有请求可以忘记它。
    // 数据流 上的 append_sse_no_buffer 无害重复（
    // 相同名称+值；服务器遵循 first/identical 指令）。
    h.push_back({"accept-encoding", "identity"});
    // 双臂携带着一个明显的秘密； OpenAI 系列将其作为不记名令牌（或自
    // 定义原始 头文件）发送。请参阅 auth::bearer_token —
    // OpenAI-family 令牌提取的单一事实来源。
    std::string key = auth::bearer_token(auth);
    // 哪个凭据出去：长度+仅最后4个，从来不是密钥。
    // 调试，因此当追逐 401 时它就在那里，否则就安静。
    AGT_LOG(Wire, Debug, "openai.auth", "host={} key_len={}{}", endpoint.host,
            key.size(), key.size() >= 4 ? " tail=" + key.substr(key.size() - 4)
                                        : std::string{});
    if (key.empty()) return h;
    if (!endpoint.auth_header_name.empty()) {
        // 自定义 头文件 名称 (--auth-header)：对于使用 e.g 进行身份验证的网关，
        // 密钥为原始密钥，无 "Bearer " 前缀。 `X-API-Key`。
        // 小写-头文件名称是大小写-insensitive并且每隔一个发送的头文件
        // agentty都是小写。
        std::string name = endpoint.auth_header_name;
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        h.push_back({std::move(name), std::move(key)});
    } else {
        // 默认值：`Authorization: Bearer <token>`用于整个
        // OpenAI系列——OpenAI/Groq/OpenRouter全部使用承载密钥。
        // （ApiKeyHeader 的原始 `sk-...` 值以同样的方式输出；
        // 这里没有 `x-api-key`。）
        h.push_back({"authorization", "Bearer " + key});
    }
    // 端点-specific静态头文件(e.g.副驾驶的编辑器
    // -identification块)。最后附加，这样它们就不会被上面的默认值破坏。
    for (const auto& kv : endpoint.extra_headers)
        h.push_back({kv.first, kv.second});
    return h;
}

void append_session_header(http::Headers& h, std::string_view key) {
    if (!key.empty())
        h.push_back({"x-session-id", std::string{key}});
}

std::string local_transport_hint(const Endpoint& ep, int http_status,
                                 http::HttpErrorKind kind) {
    const std::string where = ep.host + ":" + std::to_string(ep.port);
    // 从未回答：它确实可能没有运行。
    if (http_status == 0)
        return "  (is the local server running on " + where +
               "? start it, or fix the host with Ctrl-P \xe2\x86\x92 Custom host)";
    // 它回答了，所以它正在运行。不要让人们查看健康的
    // 服务器（#108）。
    if (kind == http::HttpErrorKind::Timeout)
        return "  (" + where + " answered, then sent nothing for too long; "
               "it may be stuck or out of memory. check its log)";
    return {};
}

// 提供者的错误正文中的人类句子，或有上限的原始摘录。
//
// OpenAI-compat家族在野外看到的形状：
//   {"error": {"message": "..."}}
//   OpenAI、Groq、大多数 {"error":
//   "..."} 一些网关 {"message":
//   "..."} 一些 [{"error": {"message": "..."}}]
// Google Gemini 的事件可能由数组包裹。
// 的数组包装器过去常常陷入原始转储，因此用户看到的屏幕是 JSON（
// `fieldViolations`、`@type`，...），而不是指出问题的一句
// 话 (#93)。无论形状如何，结果都是一行：换行符折叠，因此记录显示一个句子。
std::string provider_error_message(std::string_view body) {
    auto one_line = [](std::string s) {
        for (char& c : s) if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        std::string out;
        out.reserve(s.size());
        for (char c : s)
            if (!(c == ' ' && !out.empty() && out.back() == ' ')) out += c;
        while (!out.empty() && out.back() == ' ') out.pop_back();
        return out;
    };
    if (body.empty()) return {};
    try {
        json j = json::parse(body);
        if (j.is_array() && !j.empty()) j = j.front();
        if (j.is_object()) {
            if (auto e = j.find("error"); e != j.end()) {
                if (e->is_object()) {
                    if (auto m = e->find("message"); m != e->end() && m->is_string())
                        return one_line(m->get<std::string>());
                } else if (e->is_string()) {
                    return one_line(e->get<std::string>());
                }
            }
            if (auto m = j.find("message"); m != j.end() && m->is_string())
                return one_line(m->get<std::string>());
        }
    } catch (...) {}
    constexpr std::size_t kRawCap = 300;
    return one_line(std::string{body.substr(0, kRawCap)});
}

// 从最新的真实用户消息仅推断明确的内存-tool意图。
// 这是用于 JSON-in-content 打捞的安全门，不是一般的 NLP 保单：
// 假阴性仅仅让调用被吞掉；误报可能会导致 内存 发生突变，
// 因此请缩小词汇范围并采取行动-specific。
void set_memory_salvage_intent(StreamCtx& ctx, const Request& req) {
    std::string text;
    for (auto it = req.messages.rbegin(); it != req.messages.rend(); ++it) {
        if (it->role == Role::User && !it->is_proactive_context()) {
            text = it->text;
            break;
        }
    }
    std::ranges::transform(text, text.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    auto has = [&](std::string_view s) { return text.find(s) != std::string::npos; };

    ctx.allow_remember_salvage = has("remember") || has("don't forget")
        || has("do not forget") || has("keep in mind") || has("from now on");
    ctx.allow_forget_salvage = has("forget") || has("remove the memory")
        || has("remove memory") || has("drop the memory");
    ctx.allow_wipe_salvage = has("wipe memory") || has("wipe your memory")
        || has("forget everything") || has("clean slate");
}

// ── 流式传输 切入点 ────────────────────────────────────────────────────
provider::StreamResult run_stream_sync(Request req, EventSink sink, http::CancelTokenPtr cancel) {
    // ── 对话框 ─────────────────────────────────────────────────────
    // 这是每个 OpenAI-family 回合经过的 一个 阻塞点（
    // 副驾驶和 Kimi 代表也在这里），因此 chat-vs-Responses
    // 选择属于此处，而不是在每个呼叫者中重复。
    //
    // 这不是优化。根据 OpenAI 的推理指南，聊天完成会拒绝在除
    // `none` 之外的任何 reasoning_effort 处调用 GPT-
    // 5.4+ 的工具，并且 GPT-6-class 模型完全放弃聊天功能调用 —
    // agentty 始终发送工具，因此对于这些模型，此分叉是工作轮次和 400 之间的区别。
    // dialect_for() 拥有决定权（请参阅provider/dialect.hpp)
    // 和来自实时 404 的 self-corrects。
    //
    // 故意AFTER什么都不做，BEFORE进行身份
    // 验证检查：响应路径使用自己的主机
    // -phrased错误执行自己的凭据解析，因此首先进行聊天
    // 检查会报告消息中的错误端点。
    if (dialect_for(req.endpoint.label, req.model) == Dialect::Responses) {
        openai::ResponsesEndpoint rep;
        if (responses_endpoint_for(req.endpoint.label, rep)) {
            // responses_endpoint_for()已经从该行
            // 解决了这两个问题。无论如何，从 LIVE 端点重新
            // -assert：本机 Copilot 路径在请求时从其令牌
            // 交换重写主机和 头文件，因此 运行时 端点是静态行所知道
            // 的内容的超集。对于普通行 (OpenRouter)，
            // 两者是相同的，这是 no-op。
            rep.provider_id   = req.endpoint.label;
            if (!req.endpoint.extra_headers.empty())
                rep.extra_headers = req.endpoint.extra_headers;
            // openai::Request和provider::Request是不同的类型
            // (前者带有像weak_model/native_api这样的chat-only字段)，
            // 因此明确地投影而不是假设布局重叠。
            provider::Request rreq;
            rreq.model          = std::move(req.model);
            rreq.system_prompt  = std::move(req.system_prompt);
            rreq.messages       = std::move(req.messages);
            rreq.tools          = std::move(req.tools);
            rreq.max_tokens     = req.max_tokens;
            rreq.context_window = req.context_window;
            rreq.auth           = std::move(req.auth);
            rreq.retry_count    = req.retry_count;
            rreq.cancel         = std::move(cancel);
            rreq.effort         = req.effort;
            rreq.show_reasoning = req.show_reasoning;
            rreq.no_reasoning   = req.no_reasoning;
            rreq.reasoning_off  = req.reasoning_off;
            rreq.session_key    = req.session_key;
            rreq.affinity_key   = req.affinity_key;
            return stream_responses(rep, std::move(rreq), std::move(sink));
        }
        // Row不通告/响应（自定义主机，或者用户覆盖端点）。
        // 聊天失败：降级的回合胜过任何回合。
    }

    // 只有已知的托管提供商才确定需要密钥。自定义主机可能不会（RunPod
    // 或隧道 llama.cpp 是 TLS 并且无密钥），
    // 因此发送它并让服务器应答；然后 401 会告诉你该怎么做。
    const bool custom_host = provider::preset_for(req.endpoint.label) == nullptr;
    if (req.endpoint.use_tls && is_empty(req.auth) && !custom_host) {
        sink(StreamError{"not authenticated — set the provider's API key "
                         "(e.g. OPENAI_API_KEY) or run 'agentty login'"});
        return provider::StreamResult::failed("not authenticated");
    }

    StreamCtx ctx;
    ctx.sink = std::move(sink);
    ctx.model_id = req.model;
    ctx.show_reasoning = req.show_reasoning;
    // BEGIN在推理中没有开放标签的型号：Magistral(
    // Mistral的推理线)和DeepSeek-R1家族。对于这些，
    // 在第一个结束标记之前引导内容是隐式推理。其他模型仅获得 EXPLICIT
    // [THINK]/<think> 跨度路由 - 从不引导文本 -
    // 因此普通散文中的杂散关闭标签会逐字保留。
    // 家庭名单是MODEL事实并且存在于目录 (
    // reasons_by_default)中——每个传输层都有一个权威。
    ctx.reason_by_default = agentty::reasons_by_default(req.model);
    set_memory_salvage_intent(ctx, req);
    // 我们这一轮宣传的工具——抢救路径仅在命名其中一个时将
    // 泄漏的 -JSON "tool call" 转换为真实的。
    ctx.known_tools.reserve(req.tools.size());
    for (const auto& t : req.tools) ctx.known_tools.push_back(t.name);

    // ── 构建请求正文 ──────────────────────────────────────────
    // 一个纯函数，直接在openai_request_body_test.cpp中断言。
    // 将其排除在该高频路径之外使得一致性层
    // 成为合同而不是注释。
    const bool native = req.endpoint.native_api;
    json body = build_request_body(req);

    std::string body_str;
    try {
        body_str = body.dump();
    } catch (const nlohmann::json::exception& e) {
        ctx.sink(StreamError{std::string{"request build failed (invalid UTF-8): "}
                             + e.what()});
        ctx.sink(StreamFinished{StopReason::Unspecified});
        return provider::StreamResult::failed("request build failed: invalid UTF-8");
    }

    // ── HTTP 请求 ────────────────────────────────────────────────────────
    http::Request hreq;
    hreq.method  = http::HttpMethod::Post;
    hreq.host    = req.endpoint.host;
    hreq.port    = req.endpoint.port;
    hreq.path    = req.endpoint.path;
    hreq.plaintext = !req.endpoint.use_tls;
    if (const auto& ov = http::agentty_api_host_override(); ov.active()) {
        hreq.dial_host = ov.host;
        hreq.dial_port = ov.port;
    }
    hreq.headers = build_request_headers(req.auth, req.endpoint);
    append_session_header(hreq.headers,
                          req.affinity_key.empty() ? req.session_key : req.affinity_key);
    // 副驾驶每 USER-initiated 回合就收取一次额外费用。
    // Tool-result 轮和子代理 调用 是代理的，并且这么说，
    // 否则每个工具轮都会花费额外的费用。与 Zed 和 VS 代码相同的规则。
    if (req.endpoint.label == "copilot")
        hreq.headers.push_back({"x-initiator", initiator_for(req.messages)});
    http::append_sse_no_buffer(hreq.headers);
    hreq.body    = std::move(body_str);

    // 请求元数据+完整的正文落在一个结构化日志的`wire`
    // 通道上（参见provider/debug.hpp)。
    // 正文 被 不 截断：服务器拒绝的请求正是最后一个 字节
    // 重要的情况，而剪裁的转储会让您重新猜测。
    AGT_LOG(Wire, Debug, "openai.request",
            "POST {}://{}:{}{} model={} native={} bytes={}",
            req.endpoint.use_tls ? "https" : "http", req.endpoint.host,
            static_cast<unsigned>(req.endpoint.port), req.endpoint.path,
            req.model, native ? 1 : 0, hreq.body.size());
    AGT_LOG(Wire, Trace, "openai.request.body", "bytes={} raw={}", hreq.body.size(), ::agentty::logx::body(hreq.body));

    provider::StreamScaffold sc;
    sc.dialect = native ? "ollama-native" : "openai-chat";
    sc.sink    = ctx.sink;
    sc.feed    = [&](std::string_view chunk) {
        if (native) feed_ndjson(ctx, chunk.data(), chunk.size());
        else        feed_sse(ctx, chunk.data(), chunk.size());
        return true;
    };
    http::StreamHandler handler = sc.handler();

    // 标准梯子，具有ONE合法的per-transport旋钮：
    // LOCAL(明文)服务器在提示词处理期间不发送任何内容——消费者
    // 硬件上的llama.cpp可以在第一个令牌之前研磨几分钟，
    // 并且-processing中期的90秒空闲切割迫使重试从头开始永远重新-processes相同的提示词
    // (本地死循环)。明文端点 10 分钟，
    // 并且没有总上限 (#108)； Esc仍然取消。
    http::Timeouts tos = req.endpoint.use_tls
        ? provider::stream_timeouts()
        : provider::local_stream_timeouts();

    // 保留取消令牌的副本：移动到数据流 调用中，
    // 但是finish_stream需要它来区分用户取消和post
    // -loop处的传输层错误。
    http::CancelTokenPtr cancel_for_end = cancel;
    auto result = http::default_client(::agentty::IoAccess::grant()).stream(hreq, std::move(handler),
                                                tos, cancel);

    // 该主机不采用的场外(较旧的服务器400s
    // reasoning_effort:"none"，严格的模板字段)：
    // 为主机学习它并在没有它的情况下重新发送一次。这里而不是在
    // 状态更新函数 中，所以 `agentty run`、ACP 和子代理以
    // 相同的方式治愈； TUI 将学习到的位与主机拒绝的其余字段一起保留。
    // 4xx 正文 去往 error_body，而不是接收器，因此没有显示任何内容。
    if (sc.http_status == 400 || sc.http_status == 422) {
        using provider::OptionalField;
        const std::string host = host_key(req.endpoint.host, req.endpoint.port);
        const std::string_view said = sc.error_body;
        auto names = [&](std::string_view f) {
            return said.find(f) != std::string_view::npos;
        };
        std::uint16_t learn = 0;
        if (body.contains("reasoning_effort") && body["reasoning_effort"] == "none"
            && names("reasoning_effort"))
            learn = provider::bit(OptionalField::EffortNone);
        else if (body.contains("chat_template_kwargs") && names("chat_template_kwargs"))
            learn = provider::bit(OptionalField::ChatTemplateKwargs);
        if (learn != 0 && (rejected_fields_for(host) & learn) == 0
            && !(cancel && cancel->is_cancelled())) {
            add_rejected_field(host, learn);
            AGT_LOG(Model, Warn, "caps.field_rejected",
                    "host={} field={} http={} (resending)", host,
                    learn == provider::bit(OptionalField::EffortNone)
                        ? "reasoning_effort:none" : "chat_template_kwargs",
                    sc.http_status);
            return run_stream_sync(std::move(req), std::move(ctx.sink), std::move(cancel));
        }
    }

    // 一端-of-turn行+失败时的原始错误正文——每个"why did my provider fail?"
    // 报告都需要这一对。镜像 Anthropic 传输层 的
    // anthropic.response/anthropic.error.body
    // 接口边界，因此无论哪个后端处于活动状态，共享日志都会读取相同的内容。
    // 通过脚手架统一 end-of-turn 对（调试摘要 + 警告
    // 原始错误 正文），主机 + 终止附加为方言详细信息。
    sc.log_result(bool(result),
                  result ? std::string_view{} : result.error().render(),
                  std::format("host={} terminated={}",
                              req.endpoint.host, ctx.terminated ? 1 : 0));

    // 整个帖子-loop到SHARED尾声：一个终端 事件，正确的优先级，
    // 与每个其他提供者相同。 on_any_end 关闭两个路径上
    // 打开的工具块； before_finish（仅成功）
    // 抢救泄漏的-JSON工具调用/刷新保留的文本/保证非-empty转向。
    return provider::finish_stream({
        .terminated  = ctx.terminated,
        .sink        = ctx.sink,
        .result_ok   = bool(result),
        .http_status = sc.http_status,
        .non_replayable = !result && result.error().non_replayable,
        .cancel      = cancel_for_end,
        .stop        = ctx.stop_reason,
        .http_error_message = [&]() -> std::string {
            std::string msg = "HTTP " + std::to_string(sc.http_status);
            if (auto said = provider_error_message(sc.error_body); !said.empty())
                msg += ": " + said;
            if (sc.http_status == 401 || sc.http_status == 403)
                msg += provider::preset_for(req.endpoint.label)
                    ? "  (check the provider API key)"
                    : "  (this host wants an API key: add it in the provider "
                      "picker, Ctrl-P, or pass -k)";
            // 本地 OpenAI-compatible 服务器上的 404 是 两个 事件之一：
            // 该服务器上不存在聊天路径（custom-host 规范缺少 /v1 前缀 —
            // llama.cpp 仅服务 /v1/…），或者型号 ID 不是 loaded/known.
            // 两者名称均为 server-neutrally — 旧提示表示
            // 'ollama pull' 至 llama.cpp/vLLM 用户。
            if (sc.http_status == 404 && !req.endpoint.use_tls)
                msg += "  (404 from " + req.endpoint.host + ": either the "
                       "path '" + req.endpoint.path + "' doesn't exist on "
                       "this server \xe2\x80\x94 most need the spec to end in /v1 \xe2\x80\x94 "
                       "or the model '" + req.model + "' isn't loaded. "
                       "Check with Ctrl-P \xe2\x86\x92 Custom host, or pick a listed "
                       "model with ^/)";
            return msg;
        },
        .retry_after = sc.retry_after(),
        .transport_error_message = [&]() -> std::string {
            std::string msg = std::string{"http: "} + result.error().render();
            if (!req.endpoint.use_tls)
                msg += local_transport_hint(req.endpoint, sc.http_status,
                                            result.error().kind);
            return msg;
        },
        .before_finish = [&ctx]() {
            flush_reasoning_details(ctx);
            if (!try_salvage_tool_call(ctx)) flush_text_hold(ctx);
            ensure_nonempty_turn(ctx);
        },
        .on_any_end = [&ctx]() {
            close_open_tools(ctx);
        },
    });
}

namespace {

// 仅限 CLAUDE.md 层。 Anthropic 提示词 还注入了 agent
// -authored learn-memory (load_recent_*) 和技能 目录，
// 但这些可以运行数千个令牌，并且明显混淆简单 提示词 上的小型本地模型（一旦学习到的
// 事实存在，14b 用 "I didn't understand" 回答 "hi"）。
// 本地模型得到简洁的 user-authored CLAUDE.md 指导，仅此而已。
// user/project/local 包装器是共享的 wire::claude_md_blocks
// （也由 Ollama 传输层 使用）。 AGENTS.md（AAIF 标准，
// 项目-scoped）通过 wire::agents_md_block 前置，
// 因此标准化公共项目指南显示为 之前 个人 CLAUDE.md 层
// - 与 Anthropic 提示词 使用的排序相同。
[[nodiscard]] std::string local_memory_blocks() {
    return wire::agents_md_block(
               "Project guidance following the open AGENTS.md standard "
               "(agents.md, stewarded by the Agentic AI Foundation under "
               "the Linux Foundation). Treat as authoritative public "
               "project conventions.",
               tools::util::workspace_root(),
               tools::util::project_root(),
               wire::resolve_global_agents_md())
         + wire::claude_md_blocks(
               "Project-specific guidance the user has authored. Treat these as "
               "persistent context for THIS workspace and user.");
}

} // 命名空间结束。

std::string_view local_model_prompt_addendum() {
    return
        "\n\nMost messages are answered in plain words — greetings, small "
        "talk, and questions you already know do NOT use a tool. Only call a "
        "tool when the user asks you to touch files, run a command, or look "
        "something up; then make exactly ONE call and wait for its result. "
        "Never call remember/forget/wipe_memory on your own.";
}

std::string_view local_model_action_rules() {
    return
        "DOING WORK\n"
        "- When the user asks you to create, implement, build, change, fix, "
        "rename, or delete something, DO it with tools. Write files with "
        "`write`, change them with `edit`, run commands with the shell tool.\n"
        "- NEVER paste the code (or a command) into your reply instead of "
        "calling the tool. Code in a reply does nothing \u2014 the user wants "
        "the files on disk.\n"
        "- Big tasks are normal here. You are NOT limited to one reply: you "
        "work in many steps, one tool call at a time, and the user sees each "
        "file land. So never refuse or apologise that a task is too large "
        "for a single response. Start with the first file, then the next, "
        "until it is done.\n"
        "- If you are unsure what exists, `read` or list the directory "
        "first. A missing directory is not a reason to stop: `write` "
        "creates parent directories.\n"
        "- Never say a file was created, changed, or deleted unless a tool "
        "you called returned that result.\n"
        "- A `read` result tagged [cached] still contains the file's "
        "content: use it, do not read the same file again.\n\n";
}

std::string local_model_system_prompt() {
    std::string cwd;
    try { cwd = std::filesystem::current_path().string(); }
    catch (const std::exception& e) { util::dbglog("openai.local_prompt.cwd", e.what()); }
    catch (...) { util::dbglog("openai.local_prompt.cwd", "non-std exception"); }

#if defined(_WIN32)
    const char* os_name = "Windows";
    const char* shell   = "cmd.exe";
#elif defined(__APPLE__)
    const char* os_name = "macOS";
    const char* shell   = "sh";
#else
    const char* os_name = "Linux";
    const char* shell   = "sh";
#endif

    // 针对 OpenAI-compatible/本地模型（Ollama、llama.cpp、vLLM)进行了调整。
    // 比 Claude 提示词 更简单、更坚定：本地模型
    // 比长散文更好地遵循简短的命令规则，并且它们读取最近的
    // 历史不太可靠，因此回忆提醒是明确的。
    std::string out;
    out += "You are agentty, a terminal coding assistant. You are helpful, "
           "direct, and act on requests instead of asking which option to "
           "pick. Keep replies concise.\n\n";

    out += "CONVERSATION MEMORY\n"
           "- The full conversation so far is provided in the messages. "
           "ALWAYS use earlier messages to answer follow-up questions "
           "(names, files, decisions the user already gave you).\n"
           "- If the user told you a fact earlier (e.g. their name), recall "
           "it from the conversation; do not say you don't have it.\n\n";

    out += "TOOLS\n"
           "- Tools let you read/edit files and run commands. Call a tool "
           "ONLY when the task needs it (touch files, run a command, search "
           "the codebase). For greetings, chit-chat, or questions you can "
           "answer from the conversation, reply in plain text \u2014 do NOT call "
           "a tool.\n"
           "- To edit an existing file use `edit` (targeted change). Use "
           "`write` only to create a new file.\n"
           "- Make ONE tool call at a time and wait for its result before "
           "the next. Never invent a tool result.\n"
           "- Never call remember/forget/wipe_memory unless the user asks "
           "you to remember or forget something.\n\n";

    out += local_model_action_rules();

    out += "OUTPUT\n"
           "- Output is rendered as GitHub-flavoured markdown in a terminal. "
           "Use fenced code blocks for code. Keep tables small. LaTeX math "
           "renders too: `$…$` inline, `$$…$$` or a ```math fence for "
           "display (fractions, roots, sums, matrices).\n\n";

    out += "ENVIRONMENT\n";
    out += "- os: "; out += os_name; out += "\n";
    out += "- shell: "; out += shell; out += "\n";
    if (!cwd.empty()) { out += "- cwd: "; out += cwd; out += "\n"; }

    // 仅限 User/project CLAUDE.md 层。对于本地模型，
    // 省略了技能 目录 + 已学习的-memory（小模型上的令牌膨胀 + 混乱）；
    // 技能仍然通过 /skill-name 显式激活。
    out += local_memory_blocks();
    return out;
}

// ── Ollama /api/show 探测器 ───────────────────────────────────────────────────
// 从Ollama获取模型的能力。失败时返回 std::nullopt。
// Zed-style:仅在功能[]中报告"tools"的模型获得工具。
namespace {
struct OllamaProbe {
    std::optional<bool> supports_tools;
    // Ollama的/api/show能力[]还宣传"thinking"用于推理
    // -capable模型(deepseek-r1、qwen3、qwq、gpt-oss、
    // magistral…)——LIVE、per-model、权威声明。 nullopt
    // = 探测失败/没有该字段的旧 ollama。
    std::optional<bool> supports_thinking;
    int context_window = 0;  // 真实模型窗口来自model_info.*.context_length
};

OllamaProbe probe_ollama_model(Io io, const AuthHeader& auth,
                               const Endpoint& ep,
                               const std::string& model_name) {
    OllamaProbe out;
    http::Request hreq;
    hreq.method     = http::HttpMethod::Post;
    hreq.host       = ep.host;
    hreq.port       = ep.port;
    hreq.path       = "/api/show";
    hreq.plaintext  = !ep.use_tls;
    hreq.headers    = build_request_headers(auth, ep);
    hreq.body       = json{{"model", model_name}}.dump();
    hreq.max_body_bytes = 512 * 1024;  // /api/show 可以很大（模型文件）

    http::Timeouts tos;
    tos.connect = std::chrono::milliseconds(2'000);
    tos.total   = std::chrono::milliseconds(5'000);

    auto resp = http::default_client(io).send(hreq, tos);
    if (!resp || resp->status != 200) return out;

    try {
        auto j = json::parse(resp->body);
        // Tool能力(Zed-style):仅广告"tools"的型号
        // 获得本机功能-call频道。
        if (j.contains("capabilities") && j["capabilities"].is_array()) {
            bool has_tools = false;
            bool has_thinking = false;
            for (const auto& cap : j["capabilities"])
                if (cap.is_string()) {
                    const auto& s = cap.get_ref<const std::string&>();
                    if (s == "tools")    has_tools = true;
                    if (s == "thinking") has_thinking = true;
                }
            out.supports_tools    = has_tools;
            out.supports_thinking = has_thinking;
        }
        // 真实上下文窗口。 /api/show 返回 model_info 作为 arch-prefixed 键
        // 的平面映射；该窗口位于 "<arch>.context_length" (e.g.
        // "qwen2.context_length": 32768, "llama.context_length": 8192) 下。
        // 架构前缀因模型而异，因此扫描架构中以".context_length"
        // 而不是hard-coding结尾的任何键。这反映了 Ollama 自己的
        // CLI 在 cmd/cmd.go 的 showInfo 中所做的事情。
        //
        // 接受FLOAT以及整数。 Ollama 本身发送一个普通整数 - 针对真正的
        // 0.34.2 守护程序进行验证，该守护程序回答 "qwen3.context_length":
        // 40960 - 但 ModelInfo 是 带类型的
        // map[string]any，因此协议中没有任何内容保证这一点，
        // 并且 LiteLLM 已经将此类字段作为浮点数 (16385.0) 发出。
        // 在这里严格并不会带来任何好处，并且会默默地关闭一个窗口。
        if (j.contains("model_info") && j["model_info"].is_object()) {
            for (auto it = j["model_info"].begin(); it != j["model_info"].end(); ++it) {
                const std::string& key = it.key();
                if (key.size() >= 15
                    && key.compare(key.size() - 15, 15, ".context_length") == 0
                    && it.value().is_number()) {
                    const double d = it.value().get<double>();
                    if (d > 0 && d < 2e9) {
                        out.context_window = static_cast<int>(d);
                        break;
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        util::dbglog("openai.probe_ollama_model.parse", e.what());
    } catch (...) {
        util::dbglog("openai.probe_ollama_model.parse", "non-std exception");
    }
    return out;
}
} // 命名空间结束。

// 确切的JSON agentty戴在传输上。 Pure：请求输入，JSON 输出。
// 请参见 头文件 了解为什么这不是内嵌在 数据流 路径中，
// 并参见 conformance.hpp 了解下面每层决策背后的证据。
json build_request_body(const Request& req) {
    const bool native = req.endpoint.native_api;
    json body;
    body["model"]  = req.model;
    body["stream"] = true;

    if (native) {
        // Ollama本机/api/chat:系统提示词作为角色："system"消息，
        // 结构化tool_calls、NDJSON响应。
        // num_predict=最大输出令牌(Ollama默认为低~128)。
        body["options"] = {{"num_predict", req.max_tokens}};
        json messages = json::array();
        if (!req.system_prompt.empty())
            messages.push_back({{"role", "system"},
                                {"content", scrub_utf8(req.system_prompt)}});
        for (auto& m : build_native_messages(req.messages))
            messages.push_back(std::move(m));
        body["messages"] = std::move(messages);
        if (!req.tools.empty()) {
            body["tools"] = wire::openai_chat_tools(req.tools);
            body["tool_choice"] = "auto";
        }
        return body;
    }

    // max_tokens是OpenAI聊天端点上的`max_tokens`：
    // 整个兼容系列都采用它。 OpenAI自己推理模型400它并想要
    // max_completion_tokens；该主机学习一次重命名。
    const std::uint16_t rejected =
        rejected_fields_for(host_key(req.endpoint.host, req.endpoint.port));
    auto allowed = [&](provider::OptionalField f) {
        return (rejected & provider::bit(f)) == 0;
    };
    if (allowed(provider::OptionalField::MaxTokens))
        body["max_tokens"] = req.max_tokens;
    else
        body["max_completion_tokens"] = req.max_tokens;
    // Ask用于最终SSE 事件上的使用帧，因此上下文量表甚至
    // 可以根据流式传输请求进行更新。
    if (allowed(provider::OptionalField::StreamOptions))
        body["stream_options"] = {{"include_usage", true}};

    // 消息：先系统提示词，然后通话。
    const bool is_openrouter = req.endpoint.label == "openrouter"
                            || req.endpoint.host == "openrouter.ai";
    json messages = json::array();
    if (!req.system_prompt.empty()) {
        messages.push_back({{"role", "system"},
                            {"content", scrub_utf8(req.system_prompt)}});
    }
    {
        const std::uint8_t echo = reasoning_echo_for(
            host_key(req.endpoint.host, req.endpoint.port), req.model);
        json conv = build_messages(Thread{ThreadId{""}, "", req.messages, {}, {}}, echo);
        for (auto& m : conv) messages.push_back(std::move(m));
    }
    body["messages"] = std::move(messages);

    if (!req.tools.empty()) {
        body["tools"] = wire::openai_chat_tools(req.tools);
        body["tool_choice"] = "auto";
    }

    // 提示词-cache 路由。 OpenAI auto-caches 前缀
    // >=1024 个令牌；发送稳定的 prompt_cache_key 将
    // 对话的相同系统+工具+历史前缀固定到一个 缓存 节点。 PROBED TIER，
    // 来自 注册表 行：这曾经单独在 TLS ("hosted") 上门控，
    // 而 TLS 不是证据——Groq 和 Gemini 被托管，经过严格验证，
    // 并且 400 个未知字段，所以每次对它们的轮次都会死掉 (#93)。
    // 本地服务器也拒绝它，而且它们的 KV 缓存 无论如何都是前缀-automatic。
    if (req.endpoint.prompt_cache_key && !req.session_key.empty()
        && allowed(provider::OptionalField::PromptCacheKey))
        body["prompt_cache_key"] = req.session_key;

    // 推理努力。 PROBED TIER：目录 已经排除了不能（或不能）接受
    // 它的型号 - e.g。 Mistral MAGISTRAL 本身有推理，
    // REJECTS reasoning_effort (422)，所以 目录
    // 排除了它，并且这里的努力是空的。所以这不需要 re-check 的能力。
    // NOTE：对于排除的模型，推理 TEXT 仍然是 数据流 — 响应
    // -side 推理 处理函数 是无条件的（请参阅
    // handle_delta）。
    //
    // 不 在 use_tls、i.e 上门控。不在 "is this a hosted server" 上。
    //
    // 曾经是，原因为"local servers reject the field"。
    // 对于为其编写的服务器来说这是正确的，但一般来说是错误的：
    //
    //   ·Ollama，拒绝它的一方，使用NATIVE/api/chat
    //     上面分支并在该行之前返回。
    //     守卫命名的服务器永远无法访问。
    //   ·llama.cpp记录--reasoning-effort并接受请求
    //     场。守卫也是如此：它默默地放弃了用户的 per-turn 选择，
    //     将服务器的 command-line 默认保留为唯一有效的通道。
    //     那就是错误报告中的
    //     强制服务器端处理似乎部分有效。
    //
    // 现在保护这一点的是应该的事情：当 resolved_caps
    // () 表示模型需要努力阶梯时，`req.effort` 仅是非
    // -empty，并且对于本地服务器，现在的答案来自服务器自己的
    // 声明（参见 dialect.hpp 中的
    // declared_reasoning）而不是文件名启发式。 传输层 加密
    // 从来都不是模型功能的证据。
    if (!req.effort.empty()) {
        // OpenRouter使用其自己的`reasoning`对象规范化其
        // 背后的每个模型的推理； `reasoning_effort` 仅适用
        // 于恰好涉及 OpenAI 领域的型号。
        if (is_openrouter && allowed(provider::OptionalField::Reasoning))
            body["reasoning"] = {{"effort", req.effort}};
        else
            body["reasoning_effort"] = req.effort;
    }

    if (is_openrouter) {
        // 粘性路由：一个会话停留在一个上游上，
        // 因此其提示词缓存不断命中(与会话密钥的推导相同)。
        if (!req.session_key.empty() && allowed(provider::OptionalField::SessionId))
            body["session_id"] = req.session_key;
        // Claude到OpenRouter仅缓存在请求标记处。
        // 标记系统提示词(1h；它很少改变)和最新消息，
        // 因此每一轮都重复使用它之前的所有内容。泽德也这样做。
        if (req.model.starts_with("anthropic/") && allowed(provider::OptionalField::CacheControl))
            mark_claude_cache_points(body["messages"]);
    }
    // reasoning_details 仅返回到 OpenRouter，而不会返回到
    // Claude，后者拒绝将其作为输入字段。
    if (!is_openrouter || req.model.starts_with("anthropic/")
        || !allowed(provider::OptionalField::ReasoningDetails))
        for (auto& m : body["messages"]) m.erase("reasoning_details");
    if (!allowed(provider::OptionalField::ExtraContent))
        for (auto& m : body["messages"])
            if (auto tc = m.find("tool_calls"); tc != m.end() && tc->is_array())
                for (auto& c : *tc) c.erase("extra_content");

    // 推理OFF。对于默认情况下思考的模型来说，省略工作量字段
    // 并不是关闭的，因此会发送关闭：
    //   OpenRouter reasoning.enabled =
    //   false（它映射每个模型）自定义主机reasoning_effort：
    //                 "none"（OpenAI值；
    //                 vLLM和llama.cpp都读取它）加上
    //                 chat_template_kwargs.enable_thinking
    //                 = false，用于静默忽略
    //   "none"（无法捕获静默忽略，400可以）托管预设reasoning_effort：
    //                 "none"的旧服务器仅在模型声明的地方；
    // 严格 模式定义 上的未知字段 400 主机在 400 秒内任一字段学习一次（
    // parse_rejected_field / stream.cpp 中的努力-none 臂），
    // 每个主机持续存在，并且重新发送轮流。无法停止推理的模型什么也得不到：选择
    // 面板 永远不会提供它们。
    using agentty::ReasoningOff;
    const auto off_how = static_cast<ReasoningOff>(req.reasoning_off);
    const bool custom_host = provider::preset_for(req.endpoint.label) == nullptr;
    const bool want_off = req.effort.empty() && (req.no_reasoning || req.quick);
    if (want_off && off_how != ReasoningOff::Impossible && off_how != ReasoningOff::Omit) {
        if (is_openrouter) {
            if (allowed(provider::OptionalField::Reasoning)
                && (off_how == ReasoningOff::EffortNone || off_how == ReasoningOff::Toggle))
                body["reasoning"] = {{"enabled", false}};
        } else if (custom_host) {
            if (allowed(provider::OptionalField::EffortNone))
                body["reasoning_effort"] = "none";
            if (allowed(provider::OptionalField::ChatTemplateKwargs))
                body["chat_template_kwargs"] = {{"enable_thinking", false},
                                                {"thinking", false}};
        } else if (off_how == ReasoningOff::EffortNone) {
            body["reasoning_effort"] = "none";
        }
    }

    return body;
}

// ── Model 列名 ────────────────────────────────────────────────────────────
// 每次失败一个可起诉的判决。分类和措辞同时存在，
// 因此不会发生变化。
std::string HostProbe::explain() const {
    switch (failure) {
        case Failure::None:
            return {};
        case Failure::Unreachable:
            return "nothing listening \xe2\x80\x94 is the server running on "
                   "that host:port?";
        case Failure::NeedsKey:
            // 不 地址中有拼写错误。这么说，否则用户开始编辑
            // 已经正确的 URL。
            return "HTTP " + std::to_string(http_status)
                 + " \xe2\x80\x94 the endpoint is right, it needs an API key";
        case Failure::NotAnApi:
            // 最常见的一个custom-host错误：
            // 粘贴仪表板/文档URL而不是API基础。
            return "that's a web page, not an API \xe2\x80\x94 use the API "
                   "base URL (usually ends in /v1)";
        case Failure::NoModelList:
            return "reachable, but no model list at any known path "
                   "\xe2\x80\x94 check the path prefix (often /v1)";
        case Failure::HttpError:
            return "HTTP " + std::to_string(http_status)
                 + " \xe2\x80\x94 no model list at any known path";
    }
    return "probe failed";
}

HostProbe probe_host(Io io, const AuthHeader& auth, const Endpoint& endpoint) {
    HostProbe out;
    http::Timeouts tos;
    tos.connect = std::chrono::milliseconds(3'000);
    tos.total   = std::chrono::milliseconds(6'000);

    // 迄今为止所见过的最严重的故障，因此LAST尝试不能
    // 消除最具信息性的诊断。顺序很重要：配置路径上的
    // 401 比 回退 上的 404 有用得多，
    // 因为这意味着地址始终是正确的。
    auto note = [&](HostProbe::Failure f) {
        const auto rank = [](HostProbe::Failure x) {
            switch (x) {
                case HostProbe::Failure::NeedsKey:    return 5;  // 最有用的
                case HostProbe::Failure::NotAnApi:    return 4;
                case HostProbe::Failure::NoModelList: return 3;
                case HostProbe::Failure::HttpError:   return 2;
                case HostProbe::Failure::Unreachable: return 1;
                case HostProbe::Failure::None:        return 0;
            }
            return 0;
        };
        if (rank(f) > rank(out.failure)) out.failure = f;
    };

    auto attempt = [&](const std::string& path) -> bool {
        http::Request r;
        r.method    = http::HttpMethod::Get;
        r.host      = endpoint.host;
        r.port      = endpoint.port;
        r.path      = path;
        r.plaintext = !endpoint.use_tls;
        if (const auto& ov = http::agentty_api_host_override(); ov.active()) {
            r.dial_host = ov.host;
            r.dial_port = ov.port;
        }
        r.headers        = build_request_headers(auth, endpoint);
        r.max_body_bytes = 2ull * 1024 * 1024;
        const auto t0   = std::chrono::steady_clock::now();
        auto resp       = http::default_client(io).send(r, tos);
        const auto ms   = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        if (!resp) { note(HostProbe::Failure::Unreachable); return false; }
        out.http_status = resp->status;
        if (resp->status == 401 || resp->status == 403) {
            // 地址是 CORRECT——它应答了，并且它想要 凭据。
            // 视为成功之外最具信息性的结果。
            note(HostProbe::Failure::NeedsKey);
            return false;
        }
        if (resp->status != 200) { note(HostProbe::Failure::HttpError); return false; }

        // 200 + HTML 是仪表板-URL 错误。检测到 之前 尝试 解析，
        // 因此诊断是 "that's a web page"，而不是用户无法处理的 JSON 错误。
        // 已验证实时：https://yolo-auto.com /models
        // 返回 200 text/html 以及完整的 SPA。
        const std::string_view body{resp->body};
        const auto first = body.find_first_not_of(" \t\r\n");
        if (first != std::string_view::npos
            && (body.compare(first, 1, "<") == 0)) {
            note(HostProbe::Failure::NotAnApi);
            return false;
        }

        // 对任一形状的模型进行计数。 {"data":[…]} = OpenAI 方言； {"models":
        // […]} = Ollama /api/tags.
        try {
            auto j = json::parse(resp->body);
            if (j.contains("data") && j["data"].is_array()) {
                out.dialect     = HostProbe::Dialect::OpenAiCompat;
                out.model_count = static_cast<int>(j["data"].size());
            } else if (j.contains("models") && j["models"].is_array()) {
                out.dialect     = HostProbe::Dialect::OllamaNative;
                out.model_count = static_cast<int>(j["models"].size());
            } else {
                note(HostProbe::Failure::NoModelList);
                return false;                    // 200但不是型号列表
            }
        } catch (...) { note(HostProbe::Failure::NoModelList); return false; }
        out.models_path = path;
        out.latency_ms  = static_cast<long>(ms);
        out.failure     = HostProbe::Failure::None;
        return true;
    };

    // 1.配置的路径（遵守显式前缀）。 2. /v1 默认值。
    // 3.Ollama的本机/api/tags——11434上的裸守护程序或代理。
    if (attempt(endpoint.models_path)) return out;
    if (endpoint.models_path != "/v1/models" && attempt("/v1/models"))
        return out;
    if (attempt("/api/tags")) return out;
    return out;   // 方言无；失败是最好的诊断
}

namespace detail {

// 上下文窗口为一个/v1/models行的网关ADVERTISES，
// 或者当它什么也没说时为0。
//
// 这里没有标准字段——OpenAI自己的/v1/models仅返回
// {id, object, 创建的,owned_by因此，
// 每个想要发布窗口的网关都发明了自己的放置窗口的位置。
// 阅读全部内容与 1M 的区别-token模型开箱即用，
// 并且被默默地固定为默认值：
//
//   LiteLLM model_info.max_input_tokens（也.
//               max_tokens作为回退；配置的`model_info:`
//               块直接传递到/v1/models和/model/info）
//   OpenRouter context_length和top_provider.context_length
//               ——后者是实际服务请求的端点的窗口，因此当两者不一致时
//               vLLM获胜max_model_len（在 vllm#
//   4643 中添加到 /v1/models）llama.cpp /
//   n_ctx，以及 meta.n_ctx_train ollama
//   -style 探测其他报告
//
// 当一行携带多个时，顺序很重要：对于模型系列，优先选择描述
// 此 部署（top_provider、
// max_model_len)而不是 目录 数字的字段，因为网关通常以
// 小于其理论最大值的方式服务于模型。
//
// 返回 0 而不是默认值，因此调用者可以区分 "the gateway told us"
// 和 "nobody knows"——它们应该得到不同的处理，
// 并且将它们合并是将每个自定义 -host 模型固定到内置 -in
// 默认值的原因。
[[nodiscard]] inline int advertised_context_window(const nlohmann::json& m) {
    // 上下文窗口是TOKEN COUNT，并且下面的每个形状
    // 都必须经受住位于、溢出或串化它的网关。
    //
    // 旧的提取直接缩小到`int`，即溢出时的UB，
    // 并产生看似合理的-looking垃圾，而不是可见的故障：
    //
    //   18446744073709551615 -> -1 (uint64 max，
    //   从有缺陷的代理中看到) 4000000000 -> -294967296 (实数，
    //   刚好 > INT_MAX) 1e18 -> -2147483648
    //   (浮动缩小，UB) "1e6" -> 1（stoi 停在 'e'）
    //
    // 达到计量表的任何一个都比什么都不报告更糟糕：NEGATIVE窗口
    // 完全禁用压缩（每个`used < window`测试都是假的），
    // 并且每个回合1的窗口都压缩。两者都读作 "agentty is broken"
    // 而不是 "this gateway lies"。
    //
    // 所以：先加宽，验证，THEN缩小。拒绝合理范围之外的
    // 任何内容 - 低于 1024 不是可用的代理窗口（几乎可以
    // 肯定是输出上限或 解析 工件），高于 100M 不是
    // 2026 年的真正部署（最大的已交付窗口约为 10M）。
    constexpr std::int64_t kMinPlausibleWindow = 1024;
    constexpr std::int64_t kMaxPlausibleWindow = 100'000'000;
    auto as_int = [](const nlohmann::json& v) -> int {
        std::int64_t n = 0;
        if (v.is_number_integer())  n = v.get<std::int64_t>();
        else if (v.is_number_unsigned()) {
            const auto u = v.get<std::uint64_t>();
            if (u > static_cast<std::uint64_t>(kMaxPlausibleWindow)) return 0;
            n = static_cast<std::int64_t>(u);
        } else if (v.is_number_float()) {
            const double d = v.get<double>();
            // NaN/inf与两个边界比较为假，因此这会在没有单独的
            // isfinite()的情况下拒绝它们——并且永远不会缩小
            // 不适合的值。
            if (!(d >= static_cast<double>(kMinPlausibleWindow)
               && d <= static_cast<double>(kMaxPlausibleWindow))) return 0;
            n = static_cast<std::int64_t>(d);
        } else if (v.is_string()) {
            // 一些代理对数字元数据进行字符串化。要求 整个 字符
            // 串为数字："1e6" 不能变成 1，
            // "128000abc" 不能变成 128000——部分
            // 解析 是误读，而不是宽大读取。
            const auto& s = v.get_ref<const std::string&>();
            try {
                std::size_t consumed = 0;
                n = std::stoll(s, &consumed);
                if (consumed != s.size()) return 0;
            } catch (...) { return 0; }
        } else {
            return 0;
        }
        if (n < kMinPlausibleWindow || n > kMaxPlausibleWindow) return 0;
        return static_cast<int>(n);
    };
    auto pick = [&](const nlohmann::json& obj, const char* key) -> int {
        if (!obj.is_object()) return 0;
        const auto it = obj.find(key);
        return it == obj.end() ? 0 : as_int(*it);
    };

    // 首先部署-specific。
    if (const auto tp = m.find("top_provider");
        tp != m.end() && tp->is_object())
        if (const int w = pick(*tp, "context_length"); w > 0) return w;
    if (const int w = pick(m, "max_model_len"); w > 0) return w;

    // LiteLLM的model_info块。
    if (const auto mi = m.find("model_info");
        mi != m.end() && mi->is_object()) {
        if (const int w = pick(*mi, "max_input_tokens"); w > 0) return w;
        if (const int w = pick(*mi, "max_tokens");       w > 0) return w;
        if (const int w = pick(*mi, "context_window");   w > 0) return w;
    }

    // llama-server的per-model `meta`块。
    //
    // 对照llama.cpp tools/server/server-context.cpp (
    // get_res_model_info)进行验证：每个/v1/models行携带
    //
    //     "meta"：{"n_ctx"：slot_n_ctx，"n_ctx_train"：……，……}
    //
    // n_ctx是SERVED窗口——服务器实际启动的窗口（-c)，
    // 跨时隙划分——并且n_ctx_train是模型的train
    // -time最大值。所服务的数字是限制此请求的数字，
    // 因此它获胜； n_ctx_train 是省略它的构建的
    // 回退。
    //
    // 下面的平梯已经列出了 n_ctx/n_ctx_train，
    // 但 llama.cpp 从未将它们放在一行的顶层 - 仅在 `meta`
    // 内 - 因此每个 llama-server 模型都达到了 200k
    // 默认值。这就是问题 #49：8k 或 32k 本地模型的规格
    // 声称为 200k，因此压缩从未触发，服务器反而截断了 提示词。
    if (const auto meta = m.find("meta");
        meta != m.end() && meta->is_object()) {
        if (const int w = pick(*meta, "n_ctx");       w > 0) return w;
        if (const int w = pick(*meta, "n_ctx_train"); w > 0) return w;
    }

    // GGUF arch-prefixed 键："<arch>.context_length"。
    //
    // 这就是窗口在模型自己的元数据中的拼写方式——"qwen2.context_length"、
    // "llama.context_length"、
    // "gemma3.context_length"——
    // 并且这正是Ollama自己的CLI在cmd/cmd.go的showInfo()中读取的内容。
    // 前缀因架构而异，因此请扫描后缀，而不是查找 Hard-coding，该列表将与每个新
    // 型号系列一起使用 过期。
    //
    // 搜索任何嵌套对象，因为键出现在一些服务器上的顶层并且出现
    // 在Ollama上的"model_info"下。
    {
        const auto scan = [](const nlohmann::json& obj) -> int {
            if (!obj.is_object()) return 0;
            constexpr std::string_view kSuffix = ".context_length";
            for (auto it = obj.begin(); it != obj.end(); ++it) {
                const std::string& key = it.key();
                if (key.size() <= kSuffix.size()) continue;
                if (key.compare(key.size() - kSuffix.size(),
                                kSuffix.size(), kSuffix) != 0) continue;
                if (!it.value().is_number()) continue;
                const double d = it.value().get<double>();
                // 与上面的as_int相同的合理带：低于1k的
                // "window"是输出上限或工件，而不是可用的。
                if (d >= static_cast<double>(kMinPlausibleWindow)
                 && d <= static_cast<double>(kMaxPlausibleWindow))
                    return static_cast<int>(d);
            }
            return 0;
        };
        if (const auto mi = m.find("model_info");
            mi != m.end() && mi->is_object())
            if (const int w = scan(*mi); w > 0) return w;
        if (const int w = scan(m); w > 0) return w;
    }

    // 扁平拼写，按行最有可能携带它们的顺序。
    //
    // 每个托管的 OpenAI-compatible API 都为同
    // 一个号码选择了自己的名称，并且我们不读取的拼写是一个不小的失误：
    // 模型默默地下降到 200k 默认值，仪表误读，
    // 并且 auto-compaction 在错误的点触发。
    // 所以这个列表是生态系统，而不是猜测——我们可以实际命名每个供应商的一个条目。
    //
    //   context_length Together、Fireworks、DeepSeek、
    //                      Perplexity、
    //   openrouter 的平行
    //   context_window Groq max_context_length
    //                      Mistral (api.mistral.ai)
    //   — 它自己的拼写，不匹配生态系统中的任何其他内容 max_input_tokens
    //                      Qwen/DashScope 以及其他几个单独报告输入一半的内容；
    //                      这是 提示词 边界的数字，这是上下文量规测量 n_ctx
    //                      / n_ctx_train
    //   llama.cpp 的数字（服务窗口，然后训练 -time）
    //
    // NOTE故意缺席：`max_tokens`、`max_output_tokens`、
    // `max_completion_tokens`。
    // 这些是 OUTPUT 上限，小一个数量级，当窗口读取一个
    // 时会产生一个看似合理的错误数字，而不是明显的失败 - 然后
    // 代理不断在具有巨大窗口的模型上执行 "forgets"。
    for (const char* key : {"context_length", "context_window",
                            "max_context_length",
                            "max_input_tokens", "n_ctx", "n_ctx_train"})
        if (const int w = pick(m, key); w > 0) return w;

    return 0;
}

// ── 窗口探测器: 询问网关的 目录 号没有说什么 ──────────
//
// 上面的/v1/models梯子从CARRIES的每个形状恢复窗口(openrouter
// 的top_provider、vLLM的max_model_len、LiteLLM的
// max_input_tokens、llama.cpp的n_ctx、嵌套的model_info、
// 字符串化的数字)。它不能做的是为一个网关发明一个，其行是裸露的
// `{"id":..,"object":"model"}`——而且是其中的大多数：在增长
// max_input_tokens之前的库存LiteLLM、Ollama的/v1 shim、LM Studio、
// vanilla OpenAI-compat服务器。
//
// 这些模型然后一直下降到kDefaultContextWindow (200k)，
// 这就是为什么服务于1M
// -context模型的LiteLLM后面的用户看到200k并合理地得出200k是上限的结论。
//
// 静态数据不能解决这个问题。 self-hosted 网关的 URL
// 对任何第三方来说都是不可知的。-party 目录：models.dev
// 携带 7824 个模型，其中 18 个模型位于环回源后面，
// 根本没有 `litellm` 条目。唯一知道私有部署窗口的是部署。
//
// 那么ASK吧。两个端点，都很便宜，都是可选的：
//
//   /v1/model/info LiteLLM的管理路由。
//                    返回从其自己的成本地图解析的 per-model
//                    max_input_tokens - 权威的，以及代理将执行的
//                    确切数字。
//   /props llama.cpp的服务器自省：default_generation_
//                    settings.n_ctx是服务器STARTED所使用的窗口，
//                    其击败了模型的train-time n_ctx_train。
//
// 每个端点每个目录 加载探测ONCE，而不是每个模型，
// 并且仅当至少一行返回窗口-less时。已经在 /v1/models
// 中应答的网关无需支付任何费用；一个不支付任何请求的人。
struct WindowProbe {
    // 模型ID->窗口。当端点不提供任何路由时为空。
    std::map<std::string, int> per_model;
    // 适用于整个服务器的单个窗口（llama.cpp服务于一个模型）。
    // 当 per_model 没有 id 条目时使用。
    int server_wide = 0;
    // per_model是服务器分配的MEASUREMENT，
    // 还是对方型号支持的DECLARATION？
    //
    // 该区别决定这是否可以缩小已经通告的行的窗口。测量结果可能是：
    // llama.cpp 的 meta.n_ctx 和 LM Studio
    // 的 loaded_instances[].
    // config.context_length 是正在运行的进程实际分配的大小，
    // 因此请求确实不能超过它们。
    //
    // 声明可以NOT。 LiteLLM的/v1/model/info是代理被交给的配置文件；
    // 它可以是 过期，保守的，或者是管理员从未编辑过的默认值，
    // 并且让它覆盖更大的广告窗口会默默地消耗用户
    // 真正拥有的上下文。对于任何未来的 目录-shaped
    // 路线也是如此。
    //
    // 犯这个错误是一个安静、昂贵的错误：too
    // -small意味着我们压缩了一个从来不需要压缩的对话，
    // 并且用户看到历史无缘无故地消失了。
    bool measured = false;

    // ── 能力,不是窗口 ───────────────────────────────────────────
    // 一起骑行。 /props 是 仅 的地方 llama.cpp 说
    // 了有关功能的任何内容 - 它的 /v1/models 行是裸 id
    // + 对象，没有可供 dialect::declared_reasoning
    // () 读取的功能对象 - 并且我们已经向窗口发出此请求。
    //
    // 像 per_model 这样的键控使得 multi-model 路由器对每
    // 个型号进行应答；一台 -model 服务器写入它所服务的一个 id。
    // 空 = 服务器从未提及 chat_template_caps（不是 llama.cpp，
    // 或者比该字段更旧的版本），它必须与 "said false" 不同。
    std::map<std::string, bool> reasoning_effort;
    // 工具-call支撑件(chat_template_caps.supports_tool_calls)具有相同的形状。
    std::map<std::string, bool> tool_calls;
    // Server-wide 回退，对于单个-model形状，
    // 其中/props回答一个已加载模型，并且我们还没有可以键入的ID。
    std::optional<bool> reasoning_effort_wide;
    std::optional<bool> tool_calls_wide;
};

// 用户已明确选择进行 运行时 探测的主机。
//
// 由 运行时 从 Settings::probe_hosts
// 安装 — 推送 IN 而不是读出，因为该层不依赖于
// 设置存储（整个 传输层 由端点和身份验证 头文件 构成，
// 这使得它可以在没有文件系统的情况下进行测试）。
maya::snapshot<std::set<std::string>>& probe_opt_in() {
    static maya::snapshot<std::set<std::string>> s;
    return s;
}

// 该端点是我们应该无条件探测的服务器吗？
//
// 探测最多四个额外轮-trips，并且它尝试的每条路线（/api/v1/models、
// /props、/api/ps)是本地-server-specific，
// 因此针对托管的API，它每次刷新购买保证的404s。在 self-hosted
// 服务器上，它们很便宜，并且携带唯一准确的答案 - RUNTIME
// 窗口 - 目录 无法知道。
//
// "Local"因此意味着"a server you run"，
// 而不是"a server on this machine"。
// 此版本的第一个版本仅检查环回，当有人将 llama-server
// 放在带有 GPU 的盒子上并通过 LAN 与它交谈时，这是错误的
// - 几小时内第 49 号报道。私人地址是诚实的概括：RFC1918
// 和朋友在互联网上不可路由，因此后面没有托管的 API。
//
// Host-based，而不是TLS-based：`https://llama.lan:8443`
// 仍然是你的，普通的-HTTP远程网关仍然是别人的。
[[nodiscard]] bool is_local_endpoint(const Endpoint& ep) {
    const std::string& h = ep.host;
    if (h.empty()) return false;

    // 显式 opt-in，对于启发式无法看到的任何内容：公共地址上的服务器、
    // 在具有其自己的 DNS 的 VPN 后面、或通过隧道到达。在应用程序中
    // 设置（add-host toast 在重要时提供它）并保留在
    // Settings::probe_hosts 中； AGENTTY_PROBE_HOSTS
    // 添加到它一次运行而不持续。
    //
    // 存在这种情况是因为另一种选择是告诉某人他们的
    // 设置不受支持——探测是了解 运行时 窗口的唯一方法，
    // 因此拒绝运行它会使窗口永久错误。
    if (probe_opt_in().load()->count(h) > 0)
        return true;
    if (const char* v = std::getenv("AGENTTY_PROBE_HOSTS")) {
        std::string_view list{v};
        while (!list.empty()) {
            const auto comma = list.find(',');
            auto item = list.substr(0, comma);
            // 修剪空格
            while (!item.empty() && item.front() == ' ') item.remove_prefix(1);
            while (!item.empty() && item.back()  == ' ') item.remove_suffix(1);
            if (!item.empty() && item == h) return true;
            if (comma == std::string_view::npos) break;
            list.remove_prefix(comma + 1);
        }
    }

    if (h == "localhost" || h == "::1" || h == "[::1]") return true;
    // 宿主机的docker-desktop/podman桥别名。
    if (h == "host.docker.internal") return true;
    // .local名称根据定义是mDNS_链接-local(RFC 6762)。
    if (h.size() > 6 && h.compare(h.size() - 6, 6, ".local") == 0) return true;

    // 私有/非-routable IPv4。解析而不是前缀-matched，
    // 因此"17.2.3.4"（Apple）不会被误认为172.16/12，
    // 并且仅以数字开头的主机名根本不会被误认为是地址。
    unsigned a = 0, b = 0, c = 0, d = 0;
    char tail = '\0';
    if (std::sscanf(h.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) == 4
        && a < 256 && b < 256 && c < 256 && d < 256) {
        if (a == 127) return true;                     // 环回
        if (a == 10)  return true;                     // 10/8
        if (a == 192 && b == 168) return true;         // 192.168/16
        if (a == 172 && b >= 16 && b <= 31) return true;  // 172.16/12
        if (a == 169 && b == 254) return true;         // 链接-local
        if (a == 100 && b >= 64 && b <= 127) return true;  // CGNAT——尾鳞
        return false;   // 真实可路由地址
    }

    // IPv6唯一-local(fc00::/7)和链接-local(fe80::/10)。
    if (h.size() > 2) {
        const std::string_view v6{h.front() == '[' ? h.c_str() + 1 : h.c_str()};
        if (v6.size() >= 2) {
            const char c0 = static_cast<char>(std::tolower(v6[0]));
            const char c1 = static_cast<char>(std::tolower(v6[1]));
            if (c0 == 'f' && (c1 == 'c' || c1 == 'd')) return true;
            if (c0 == 'f' && c1 == 'e' && v6.size() >= 3) {
                const char c2 = static_cast<char>(std::tolower(v6[2]));
                if (c2 >= '8' && c2 <= '9') return true;
                if (c2 == 'a' || c2 == 'b') return true;
            }
        }
    }

    // 裸露的单一-label主机名（"gpubox"、"nas")
    // 没有点并且在没有搜索域的情况下无法在公共互联网上解析——
    // 它是您网络上的机器。
    if (h.find('.') == std::string::npos && h.find(':') == std::string::npos)
        return true;

    return false;
}

// Percent-encode查询-string值。
//
// Model id 通常包含 '/' 和 ':' ("qwen/qwen3-coder-30b"、
// "llama3.2:latest")，并且 llama-server 的路由器与确切名称匹配。
// 发送这些原始数据要么会截断第一个保留字符处的值，
// 要么完全丢失模型。
[[nodiscard]] inline std::string url_encode(std::string_view in) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(in.size() * 3);
    for (unsigned char c : in) {
        const bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                       || (c >= '0' && c <= '9')
                       || c == '-' || c == '_' || c == '.' || c == '~';
        if (safe) out.push_back(static_cast<char>(c));
        else { out.push_back('%'); out.push_back(hex[c >> 4]); out.push_back(hex[c & 15]); }
    }
    return out;
}

[[nodiscard]] inline WindowProbe probe_endpoint_windows(const AuthHeader& auth,
                                                       const Endpoint& ep,
                                                       const std::vector<std::string>& ids = {}) {
    WindowProbe out;
    http::Timeouts tos;
    tos.connect = std::chrono::milliseconds(1'500);
    tos.total   = std::chrono::milliseconds(4'000);

    auto get = [&](std::string_view path) -> std::optional<std::string> {
        http::Request hreq;
        hreq.method    = http::HttpMethod::Get;
        hreq.host      = ep.host;
        hreq.port      = ep.port;
        hreq.path      = std::string{path};
        hreq.plaintext = !ep.use_tls;
        hreq.headers   = build_request_headers(auth, ep);
        hreq.max_body_bytes = 2ull * 1024 * 1024;
        if (const auto& ov = http::agentty_api_host_override(); ov.active()) {
            hreq.dial_host = ov.host;
            hreq.dial_port = ov.port;
        }
        auto resp = http::default_client(::agentty::IoAccess::grant()).send(hreq, tos);
        if (!resp || resp->status != 200) return std::nullopt;
        return resp->body;
    };

    // 1.LiteLLM/v1/model/info—{"data":[{"model_name":..,
    //    "model_info":{"max_input_tokens":1048576}}，...]}
    if (auto body = get("/v1/model/info")) {
        try {
            auto j = json::parse(*body);
            for (const auto& row : j.value("data", json::array())) {
                if (!row.is_object()) continue;
                // id位于此处的model_name下（它是PUBLIC别名，
                // 这是/v1/models列出的以及我们分派上的）。
                std::string id = row.value("model_name", std::string{});
                if (id.empty()) id = row.value("id", std::string{});
                if (id.empty()) continue;
                // 窗防护装置的能力BEFORE。这些都是一行中的独立事实，
                // 并且 LiteLLM 管理员在未触及令牌限制的
                // 情况下声明 `supports_reasoning: true`
                // 是常见情况 - 在丢失的窗口上执行 `continue`
                // 会丢弃代理曾经做出的唯一推理声明。 （这条路线也是
                // 仅 放置库存 LiteLLM 说明了有关功能的
                // 任何内容：其 /v1/models 行通常是裸
                // id + 对象，与 llama.cpp 完全一样，
                // 这就是该服务器需要 /props 的原因。）
                if (const auto reasons = dialect::declared_reasoning()(row))
                    out.reasoning_effort[id] = *reasons;
                // 重复使用与 目录 路径相同的容差阶梯：窗口可以平放在行上或
                // 嵌套在 model_info 下，并且 LiteLLM 将其
                // 作为 FLOAT (16385.0) 发出。
                int w = advertised_context_window(row);
                if (w <= 0) continue;
                out.per_model[id] = w;
            }
        } catch (const std::exception& e) {
            util::dbglog("openai.window_probe.model_info", e.what());
        } catch (...) {}
    }
    // NOT测量：/v1/model/info是代理自己的配置，
    // 是与行的声明类似的声明，因此它填补了漏洞但从不缩小更大的漏洞。
    //
    // 也返回 CAPABILITY 命中，而不仅仅是窗口命中。
    // 声明推理但没有限制的代理仍然回答了这个探测，并且进入下面的
    // LM Studio / llama.cpp 分支将向
    // LiteLLM 端点询问它不提供服务的路由。
    if (!out.per_model.empty() || !out.reasoning_effort.empty()) return out;

    // 2. LM Studio /api/v1/models — NATIVE API，而不是 /v1 垫片。
    //
    // LM Studio在一个端口上提供两个API。 OpenAI-compatible /v1
    // 报告 `max_context_length`：型号 ARCHITECTURE 支持什么。
    // 对于每个正在运行的实例，本机还报告：
    //
    //     "loaded_instances":[{"config":{"context_length":16384}}]
    //
    // 这是实例实际上已加载所在的窗口。这些是不同的数字，
    // 只有第二个数字限制了请求——在 16k 时能够处理
    // 128k 已加载 的模型仍然会在 16k 时拒绝。
    //
    // 这是问题 #49 的后半部分：仪表显示了
    // 架构最大值，因此在服务器截断 提示词 时压缩
    // 处于空闲状态。
    //
    // NOTE不对称性，这很重要：UNLOADED模型根本没有进入这里。
    // LM Studio 不会公开已卸载模型的 已保存 加载 配置，
    // 因此它的真实窗口在 加载 之前是不可知的 - 并且替换
    // max_context_length 会根据从未分配的
    // 窗口调整 提示词 的大小，这是我们正在修复的错误。如果不保留它，
    // 现有的梯子就会回落到 max_context_length
    // 作为声明的上限，这是诚实的：它是模型 加载 之前
    // 可用的最佳界限。
    if (auto body = get("/api/v1/models")) {
        try {
            auto j = json::parse(*body);
            for (const auto& row : j.value("models", json::array())) {
                if (!row.is_object()) continue;
                const std::string id = row.value("key", std::string{});
                if (id.empty()) continue;

                // 声明能力，记录BEFORE，下面的已加载
                // -instance检查返回提前。
                //
                // 这一行是 LM Studio 唯一说明模型可以做什么的
                // 地方：
                //
                //     "capabilities":{"vision":真，
                //                      "trained_for_tool_use":真，
                //                      "reasoning":{"default":"on"，...}}
                //
                // /v1 shim 的行不携带这样的对象，因此，如果没有这个对象，
                // 每个 LM Studio 模型都会通过 resolved_caps
                // () 落入 id-inference 梯级 — 一种针对
                // 无法读取 `publisher/model` 密钥的托管
                // 命名（`:7b` 标签、已知系列前缀）进行调整的启发式方法。
                // 推理支持最终由文件名决定。
                //
                // 故意ABOVE早`loaded_instances`-continue:
                // 能力是 MODEL 的属性，
                // 并且在卸载时是可知的，这与 运行时 窗口不同，
                // 后者是实例的属性，但不是。在此记录意味着
                // 选择面板 在第一个 加载 之前而不是
                // 之后显示正确的可供性。
                if (const auto ci = row.find("capabilities");
                    ci != row.end() && ci->is_object()) {
                    // Endpoint-scoped，像所有其他目录写
                    // 入一样：另一台主机上的相同密钥可能具有不同的构建。
                    const std::string scoped = ep.label + "/" + id;
                    if (const auto reasons = dialect::declared_reasoning()(*ci))
                        set_catalog_reasoning(scoped, *reasons);
                    // 特意将NOT写在这里，与下面的Ollama分支不同。
                    // reasoning_compat 已经精确地
                    // 导出了 low|medium|high，
                    // 这是 OpenAI 传输 所承载的阶梯 - 因此一组将
                    // 重述默认值已经说过的内容。 Ollama 需要显式写入，
                    // 因为 `think` 是它自己的字段，有自己的合约，
                    // 而不是 OpenAI 枚举。
                    //
                    // WOULD 证明此处写入合理的一件事是不同意的
                    // allowed_options 列表（LM
                    // Studio 已在某些 gpt-oss 版本
                    // 上提供了 `xhigh`）。阅读它需要一个真正
                    // 的载荷来表明分歧；在那之前，重申默认值只是
                    // 保持同步的第二个地方。
                }

                const auto li = row.find("loaded_instances");
                if (li == row.end() || !li->is_array()) continue;
                // 一个模型的多个实例可以是不同尺寸的已加载。
                // 最小的是保证每个请求都能满足的唯一一个，
                // 因此它是安全范围。
                int best = 0;
                for (const auto& inst : *li) {
                    if (!inst.is_object()) continue;
                    const auto cfg = inst.find("config");
                    if (cfg == inst.end() || !cfg->is_object()) continue;
                    const int w = advertised_context_window(*cfg);
                    if (w > 0 && (best == 0 || w < best)) best = w;
                }
                if (best > 0) out.per_model[id] = best;
            }
        } catch (const std::exception& e) {
            util::dbglog("openai.window_probe.lmstudio", e.what());
        } catch (...) {}
    }
    if (!out.per_model.empty()) {
        // MEASURED:这是已加载实例分配的大小。
        out.measured = true;
        return out;
    }

    // 3. llama.cpp /props——RUNTIME窗口(命令行上的-c)，
    //    这是实际适用的数字； n_ctx_train
    //    是建筑天花板，通常要大得多。
    //
    //    两种服务器形状：
    //
    //    唯一-MODEL（经典的`llama-server -m model.gguf`）：
    //    裸露的/props回答其所服务的一个模型，因此结果是server
    //    -wide。
    //
    //    路由模式，例如 `llama-server --models-dir ...`，
    //    multi-model）：裸露的 /props 返回 DUMMY 和 n_ctx：0 — 字面上是一个占位符，
    //    因此网络 UI 不会中断（在 tools/server/server-models.cpp、
    //    get_router_props 中验证）。真正的答案需要？model=<name>。
    //
    //    所以这里的0不是"no answer"，而是"ask again per model"。
    if (auto body = get("/props")) {
        try {
            auto j = json::parse(*body);
            // 能力随车窗行程而行。在任何早期返回之前记录
            // 窗口逻辑 之前，因为这两个答案是独立的：
            // 路由器的裸 /props 有一个虚拟
            // n_ctx 0，但仍然带有真实的模板上限，
            // 而我们已经知道其窗口的服务器仍然需要这个。
            const auto wide_effort = dialect::template_reasoning_effort()(j);
            const auto wide_tools  = dialect::template_tool_calls()(j);
            if (wide_effort) out.reasoning_effort_wide = *wide_effort;
            if (wide_tools)  out.tool_calls_wide       = *wide_tools;
            if (auto dg = j.find("default_generation_settings");
                dg != j.end() && dg->is_object()) {
                if (const int w = advertised_context_window(*dg); w > 0)
                    out.server_wide = w;
            }
            if (out.server_wide <= 0)
                out.server_wide = advertised_context_window(j);
            // MEASURED:default_generation_settings.n_ctx是meta.slot_n_ctx，
            // 本进程实际分配的窗口(
            // 在llama.cpp中验证tools/server/server-context.cpp)。
            if (out.server_wide > 0) out.measured = true;

            // 路由器模式：询问每个模型给我们的那些。
            //
            // `autoload=false`是NOT可选的。
            // 路由器的 models_autoload 默认为 TRUE，
            // 因此一个简单的 /props?model=X LOADS 该模型
            // - 遍历列表会将服务器上的每个模型拉入 内存 只是为了读取数字。
            // 使用 autoload=false 时，卸载的模型会回答
            // "model is not loaded"，我们只需跳过它，无论如何，
            // 这都是正确的结果：非驻留模型没有分配的窗口可报告。
            if (j.value("role", std::string{}) == "router" && !ids.empty()) {
                for (const auto& id : ids) {
                    auto pb = get("/props?model=" + url_encode(id)
                                  + "&autoload=false");
                    if (!pb) continue;
                    try {
                        auto pj = json::parse(*pb);
                        // Per-model caps:在路由器上，
                        // 每个型号都有其OWN聊天模板，
                        // 因此上面的server-wide答案（虚拟）没有描述其中任何一个。
                        if (auto e = dialect::template_reasoning_effort()(pj))
                            out.reasoning_effort[id] = *e;
                        if (auto t = dialect::template_tool_calls()(pj))
                            out.tool_calls[id] = *t;
                        int w = 0;
                        if (auto dg = pj.find("default_generation_settings");
                            dg != pj.end() && dg->is_object())
                            w = advertised_context_window(*dg);
                        if (w <= 0) w = advertised_context_window(pj);
                        if (w > 0) out.per_model[id] = w;
                    } catch (...) {}
                }
                if (!out.per_model.empty()) out.measured = true;
            }
        } catch (const std::exception& e) {
            util::dbglog("openai.window_probe.props", e.what());
        } catch (...) {}
    }
    if (out.measured) return out;

    // 4. Ollama/api/ps-已加载 RIGHT NOW是什么，以及尺寸是多少。
    //
    // /api/tags (路线 list_models 用于 Ollama)根本不
    // 携带任何窗口——针对 llama api/types.go ListModelResponse
    // 进行验证，其是{名称、型号、modified_at、大小、摘要、
    // 细节、功能}。因此，Ollama 行到达此处为 0，运行时 回落到
    // effective_num_ctx() 的 8k 下限/32k 上限。
    //
    // 回退是猜测。 /api/ps 报告，根据 LOADED 模型，
    // ProcessModelResponse.context_length —
    // 守护进程分配的实际 num_ctx，这与 llama.cpp 的 n_ctx
    // 属于同一类事实，并且严格优于启发式。尽管有人要求更多，
    // 但运行 `ollama serve` 和 OLLAMA_CONTEXT_LENGTH=
    // 65536 的人目前仍被我们的上限限制在 32k。
    //
    // 仅出现 已加载 模型，这是正确的：卸载的模型没有
    // 分配的窗口可报告，而发明一个是#49 bug。
    if (auto body = get("/api/ps")) {
        try {
            auto j = json::parse(*body);
            for (const auto& row : j.value("models", json::array())) {
                if (!row.is_object()) continue;
                const int w = row.value("context_length", 0);
                if (w <= 0) continue;
                // Ollama同时回答"llama3.2"和"llama3.2:latest"；
                // 索引以行携带的名称为准，因此无论用户 带类型的 如何查找，
                // list_models 中的查找都会命中。
                for (const char* key : {"model", "name"}) {
                    const auto id = row.value(key, std::string{});
                    if (!id.empty()) out.per_model[id] = w;
                }
            }
            if (!out.per_model.empty()) out.measured = true;
        } catch (const std::exception& e) {
            util::dbglog("openai.window_probe.ollama_ps", e.what());
        } catch (...) {}
    }
    return out;
}

}  // namespace detail

void install_probe_hosts(std::set<std::string> hosts) {
    detail::probe_opt_in().store(std::move(hosts));
}

// 公共货运代理 — 请参阅 头文件 了解为什么 0 是合同。
int advertised_context_window(const nlohmann::json& model_row) {
    return detail::advertised_context_window(model_row);
}

int router_declared_window(const nlohmann::json& row) {
    // llama-server路由器行(tools/server/server-models.cpp、
    // get_router_models)携带argv，路由器将启动具有以下子项的argv:
    // {"status":{"value":"unloaded","args":[
    // "llama-server","--ctx-size","32768",...]}}。
    // 只有 LOADED 行也会获得 `meta`，因此对于其他所有内容，这是唯一写入大小的位置。
    //
    // 槽位窗口，镜像server-context.cpp：具有统一的KV 缓存，
    // 每个槽位可以使用整个池；如果没有它，池 将被分割到 --parallel
    // 插槽中。 --parallel 默认为自动（4 个插槽，统一），
    // --kv-unified-per-slot 限制每个插槽。
    const auto st = row.find("status");
    if (st == row.end() || !st->is_object()) return 0;
    const auto args = st->find("args");
    if (args == st->end() || !args->is_array()) return 0;

    long long ctx = -1, parallel = -1, per_slot = 0;
    int unified = -1;   // -1 未设置，0 关闭，1 开启
    auto num = [](const std::string& s) -> long long {
        try {
            std::size_t used = 0;
            const long long v = std::stoll(s, &used);
            return used == s.size() ? v : -1;
        } catch (...) { return -1; }
    };
    const auto& a = *args;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!a[i].is_string()) continue;
        const auto& k = a[i].get_ref<const std::string&>();
        const bool has_v = i + 1 < a.size() && a[i + 1].is_string();
        const std::string v = has_v ? a[i + 1].get<std::string>() : "";
        if (k == "--ctx-size" || k == "-c")            { ctx = num(v); ++i; }
        else if (k == "--parallel" || k == "-np")      { parallel = num(v); ++i; }
        else if (k == "--kv-unified-per-slot")         { per_slot = num(v); ++i; }
        else if (k == "--kv-unified" || k == "-kvu")   unified = 1;
        else if (k == "--no-kv-unified" || k == "-no-kvu") unified = 0;
    }
    if (parallel < 0) {            // 自动：4个插槽，统一KV
        parallel = 4;
        if (unified < 0) unified = 1;
    }
    // 无 -c (或 -c 0) 意味着 "whatever the model was trained with"，
    // 除非 --kv-unified-per-slot 确定 池 本身的尺寸。
    if (ctx <= 0 && per_slot > 0) ctx = parallel * per_slot;
    if (ctx <= 0) return 0;
    long long slot = unified == 1 ? ctx : ctx / std::max(1LL, parallel);
    if (per_slot > 0 && per_slot < slot) slot = per_slot;
    return detail::advertised_context_window(nlohmann::json{{"n_ctx", slot}});
}

int probe_loaded_window(Io /*io*/, const AuthHeader& auth, const Endpoint& endpoint,
                        const std::string& model_id) {
    if (model_id.empty() || !detail::is_local_endpoint(endpoint)) return 0;
    const auto probe = detail::probe_endpoint_windows(auth, endpoint, {model_id});
    // 声明（LiteLLM的配置）不是这所要求的。
    if (!probe.measured) return 0;
    if (const auto it = probe.per_model.find(model_id); it != probe.per_model.end())
        return it->second;
    // Per-model 路由（路由器、LM Studio、Ollama）已回答，
    // 但不适用于此 ID：它不是 已加载。 server_wide 仅在单个
    // -model 服务器上有意义，它描述了它所服务的一个模型。
    return probe.per_model.empty() ? probe.server_wide : 0;
}

std::vector<ModelInfo> list_models(Io io, const AuthHeader& auth, const Endpoint& endpoint,
                                   bool force_probe) {
    std::string ignored;
    return list_models(io, auth, endpoint, force_probe, ignored);
}

std::vector<ModelInfo> list_models(Io io, const AuthHeader& auth, const Endpoint& endpoint,
                                   bool force_probe, std::string& error) {
    std::vector<ModelInfo> result;
    error.clear();
    // 没有密钥的已知托管提供商只会返回 401。定制主机可能根本
    // 不需要密钥，所以询问它。
    if (endpoint.use_tls && is_empty(auth)
        && provider::preset_for(endpoint.label) != nullptr)
        return result;

    http::Request hreq;
    hreq.method = http::HttpMethod::Get;
    hreq.host   = endpoint.host;
    hreq.port   = endpoint.port;
    hreq.path   = endpoint.models_path;
    hreq.plaintext = !endpoint.use_tls;
    if (const auto& ov = http::agentty_api_host_override(); ov.active()) {
        hreq.dial_host = ov.host;
        hreq.dial_port = ov.port;
    }
    hreq.headers        = build_request_headers(auth, endpoint);
    hreq.max_body_bytes = 2ull * 1024 * 1024;

    http::Timeouts tos;
    tos.connect = std::chrono::milliseconds(5'000);
    tos.total   = std::chrono::milliseconds(10'000);

    auto resp = http::default_client(io).send(hreq, tos);
    // PATH-PREFIX PROBE（自定义主机）：像 http://host:8080/
    // 这样的规范派生 models_path "/models"，
    // 但大多数本地 OpenAI-compatible 服务器（llama.cpp、
    // vLLM、LM Studio）仅在 "/v1" 下服务。这里的 404（
    // 服务器显然可以访问）几乎总是缺少前缀，它是 "custom host dead loop"
    // 的根源：选择面板 没有显示任何模型，无论如何用户都会提示，并且每次都会
    // 出现 404。 重试 一次，前面加上 /v1；成功后，采用 此 过程
    // 的更正路径（Endpoint::from_spec 输出根据选择重新构建，
    // 因此更正也必须在请求时进行 - 请参阅 run_stream_sync
    // 的匹配 回退 注释）。
    //
    // 重试也覆盖Ollama。 11434 是 Ollama 的端口，
    // 但 llama.cpp 可以被告知使用它，然后 /api/tags 404s 而 /
    // v1/models 回答很好 - 用户可以卷曲针对服务器的空模型 选择面板 (#73)。
    // 回到这里意味着为该端口上的 OpenAI-compatible
    // 服务器选择 "Ollama" 列出型号而不是看起来损坏。
    bool fell_back_to_v1 = false;
    if (resp && resp->status == 404
        && !endpoint.models_path.starts_with("/v1/")) {
        http::Request retry = hreq;
        retry.path = "/v1/models";
        auto second = http::default_client(io).send(retry, tos);
        if (second && second->status == 200) {
            resp = std::move(second);
            // 无论预设如何，正文现在是OpenAI-shaped，因此下面的解析
            // 必须遵循RESPONSE而不是注册表行。将 {"data":
            // [...]} 列表读取为 Ollama 的 {"models":
            // [...]} 会产生一个空的 选择面板，看起来与 404 完全相同。
            fell_back_to_v1 = true;
        }
    }
    if (!resp) {
        error = "can't reach " + endpoint.host + ": " + resp.error().render();
        return result;
    }
    if (resp->status != 200) {
        std::string msg = "HTTP " + std::to_string(resp->status);
        if (auto said = provider_error_message(resp->body); !said.empty())
            msg += ": " + said;
        if (resp->status == 401 || resp->status == 403)
            msg += is_empty(auth) ? " \xe2\x80\x94 this host needs an API key"
                                  : " \xe2\x80\x94 the API key was rejected";
        error = std::move(msg);
        return result;
    }
    // 整个列表的能力事实分为一批并发布一次：per
    // -model设置者每次复制完整的注册表。
    CatalogBatch caps;
    try {
        auto j = json::parse(resp->body);
        if (endpoint.native_api && !fell_back_to_v1) {
            // Ollama /api/tags:{"models":[{"name":
            // "qwen2.5-coder:7b",...}]}首先收集型号名称，
            // 然后探测每个型号的/api/show以确定工具支持(Zed-style能力检查)。
            std::vector<std::string> names;
            for (const auto& m : j.value("models", json::array())) {
                auto id = m.value("name", "");
                if (!id.empty()) names.push_back(id);
            }
            // 通过/api/show.探测每个模型的能力这为每个模型添加了round-trip，
            // 但是在后台获取中同时运行。如果没有它，
            // 我们就无法知道模型是否真的支持结构化
            // 工具 调用（Ollama 的功能[]。
            // "tools")与仅将工具JSON泄漏到内容中。
            for (const auto& id : names) {
                auto probe = probe_ollama_model(io, auth, endpoint, id);
                ModelInfo info{
                    .id             = ModelId{id},
                    .display_name   = id,
                    .provider       = endpoint.label,
                    .supports_tools = probe.supports_tools,
                };
                if (probe.context_window > 0)
                    info.context_window = probe.context_window;
                // Ollama DECLARES per-model
                // 在/api/show功能[] ("thinking")中的推理—
                // —将其范围记录到该端点，以便resolved_caps的live
                // -catalog层覆盖id启发法不知道的本地模型(qwq、
                // qwen3、新社区模型)。 Ollama 的 `think`
                // 接受等级为 low|medium|high，因此请准确声明该阶梯；
                // 编码端 归并 minimal/xhigh/max 向内匹配。
                if (probe.supports_thinking.value_or(false)) {
                    caps.set_reasoning(endpoint.label + "/" + id, true);
                    caps.set_effort_set(
                        endpoint.label + "/" + id,
                        static_cast<std::uint8_t>(
                            effort_bit(Effort::Low) | effort_bit(Effort::Medium)
                            | effort_bit(Effort::High)));
                } else if (probe.supports_thinking.has_value()) {
                    caps.set_reasoning(endpoint.label + "/" + id, false);
                }
                result.push_back(std::move(info));
            }
        } else {
            for (const auto& m : j.value("data", json::array())) {
                auto id = m.value("id", "");
                if (id.empty()) continue;
                // 原始/v1/models转储列出了密钥可以接触的每个资产
                // ——嵌入、image/audio/moderation端点、
                // 重新排序——代理无法驱动这些资产。丢弃它们，
                // 这样它们就不会污染子代理路由器选择的 选择面板 OR。
                if (!is_dispatchable_model(id)) continue;
                // 目录中的DECLARE per
                // -model能力的提供者得到该事实记录——resolved_caps
                // () 归并它在OVER id推断中，因此工作量阶梯跟踪
                // 服务器的实时分派表而不是我们的静态猜测（其漂移：
                // 过时的mistral-medium修订版拒绝reasoning_effort，
                // 而magistral现在接受它）。
                //
                // 通读方言表，而不是手写。野外存在两种形状——Mistral
                // 的普通 bool 和 LM Studio 的对象——这个
                // 调用 站点用于测试 is_boolean() 并跳过
                // 其他任何内容。这会悄悄地将 每个 LM Studio
                // 模型放到 id-inference 梯级上，
                // 这是对托管命名约定的启发式方法，
                // 无法对 `publisher/model` 密钥进行分类。
                // 然后推理支持由文件名决定。 dialect::declared_reasoning
                // () 在一处声明两种拼写；第三台服务器在那里添加一条线，
                // 而不是在这里添加一个分支。
                if (const auto ci = m.find("capabilities");
                    ci != m.end() && ci->is_object()) {
                    // Provider-scoped 密钥：另一台主机上的相同裸
                    // ID（Groq 与 Cerebras 上的 gpt
                    // -oss）可能具有不同的合同，因此切勿让一台主机的声明泄露。
                    const std::string scoped = endpoint.label + "/" + id;
                    if (const auto reasons = dialect::declared_reasoning()(*ci))
                        caps.set_reasoning(scoped, *reasons);
                }
                result.push_back(ModelInfo{
                    .id           = ModelId{id},
                    .display_name = m.value("display_name", id),
                    .provider     = endpoint.label,
                    .context_window = [&] {
                        const int w = detail::advertised_context_window(m);
                        return w > 0 ? w : router_declared_window(m);
                    }(),
                });
            }
            // 目录 未提及的任何行都有 一个 机会由网关本身应答（
            // LiteLLM /v1/model/info，
            // llama.cpp /props）。推迟到这里 - 在
            // 解析整个列表之后 - 因此探测器每个 加载 而不是每
            // 个模型触发一次，并且当每行已经携带一个窗口时根本不会触发。
            //
            // 这是修复 "my 1M-context models show 200k" 的 接口边界：
            // 私有网关的窗口只能从网关获知，而不能从由URL
            // 键控的第三-party 目录获知。
            // 每当 任意 行未知时，以及 — 对于 LOCAL
            // 服务器 — — 每当行已经声明某些内容时，进行探测，
            // 因为它们声明的可能不是 运行时 窗口。
            //
            // 对于两个最大的本地服务器来说，仅在 `any_unknown`
            // 上门控探测器是错误的，因为两者都声明了一些内容：
            //
            //   * LM Studio 的 /v1 行带有 `max_context_length` — 什么
            //     ARCHITECTURE支持。能够支持 128k 但 16k
            //     的 已加载 的型号仍然拒绝 16k。该行看起来为 "known"，
            //     因此探测器从未运行，并且仪表永远显示 128k。
            //   * llama-server 行带有 meta.n_ctx_train 以及
            //     服务于meta.n_ctx。
            //
            // DECLARED 上限和 MEASURED 运行时 窗口是不同的事实，
            // 并且测量的事实是请求必须容纳在内部的唯一事实——因此当每一行
            // 是 "known" 时，我们仍然探测，并且每 -model
            // 结果 OVERRIDES 是声明的窗口（如果它更小）。
            // （这也是为什么 #49 报告
            // "values don't update on refresh"：刷新 re
            // -read 声明的上限和探测结果无处着陆。）
            //
            // Smaller-only，故意的。如果探测器大于
            // 所宣布的上限，则意味着我们误读了某些内容；
            // 在此基础上提高窗口可能会冒构建服务器拒绝的 提示词
            // 的风险，而降低窗口只会花费空间。向较小的数字
            // 失败。
            //
            // LOCAL-ONLY，也是故意的。该探测器最多有三个额外的
            // HTTP round-trips，并且其端点都不存在于托管的
            // API 上 - 每次刷新时针对 api.openai.com
            // 运行它都会购买三个有保证的 404。托管行保留旧规则：
            // 探针只能填补一个洞。
            const bool any_unknown =
                std::any_of(result.begin(), result.end(),
                            [](const ModelInfo& mi) { return mi.context_window <= 0; });
            const bool local = detail::is_local_endpoint(endpoint);
            if (any_unknown || local || force_probe) {
                // ids让探针根据端点需要的模型进行应答（
                // llama-server路由器模式的/props？model=）。
                std::vector<std::string> ids;
                ids.reserve(result.size());
                for (const auto& mi : result) ids.push_back(mi.id.value);
                const auto probe =
                    detail::probe_endpoint_windows(auth, endpoint, ids);
                // 能力第一，并且在其OWN条件下：下面的窗口块
                // 在已找到窗口的探针上门控，但是llama.cpp
                // 构建可以在报告n_ctx 0（路由器虚拟）的
                // 同时回答chat_template_caps，
                // 并且已经声明其窗口的行仍然需要记录其上限。
                for (auto& mi : result) {
                    const auto pick = [&](const std::map<std::string, bool>& per,
                                          const std::optional<bool>& wide)
                        -> std::optional<bool> {
                        if (const auto it = per.find(mi.id.value); it != per.end())
                            return it->second;
                        // server_wide仅代表单个-model服务器。
                        // 在路由器上，它是虚拟的，其不描述真实
                        // 模型——与上面的窗口相同的不对称性。
                        return per.empty() ? wide : std::nullopt;
                    };
                    // Endpoint-scoped 键，与其他 目录 一样写入：
                    // 另一主机上的相同模型id可能是具有
                    // 不同模板的不同数量。
                    const std::string scoped = endpoint.label + "/" + mi.id.value;
                    if (const auto e = pick(probe.reasoning_effort,
                                            probe.reasoning_effort_wide)) {
                        // 这是对 "no reasoning on llama.cpp" 的修复。
                        // 如果没有它，这些行将通过 resolved_caps
                        // () 进行 id 推断，
                        // 其读取 GGUF 路径并进行猜测——因此
                        // 工作量阶梯根据文件的命名而出现或消失。
                        caps.set_reasoning(scoped, *e);
                    }
                    if (const auto t = pick(probe.tool_calls, probe.tool_calls_wide)) {
                        // 仅在显式错误时出现WITHHOLD。未知仍发送工具（
                        // 见ModelInfo::tools_allowed）：
                        // 默默地从不获取工具的模型看起来
                        // 与只是不擅长工具的模型相同。
                        if (!*t) mi.supports_tools = false;
                    }
                }
                if (!probe.per_model.empty() || probe.server_wide > 0) {
                    for (auto& mi : result) {
                        const auto it = probe.per_model.find(mi.id.value);
                        if (it != probe.per_model.end()) {
                            // 总是填坑； SHRINK 仅适用于实际测量（参见
                            // WindowProbe::measured）。
                            // 让声明缩小更大的广告窗口
                            // 会悄悄地消耗用户实际拥有的
                            // 上下文——而这种症状，
                            // 即对话在没有明显原因的情况下提前压缩，
                            // 是没有人会在这里追溯到的。
                            if (mi.context_window <= 0
                                || (probe.measured && it->second < mi.context_window))
                                mi.context_window = it->second;
                        } else if (probe.server_wide > 0
                                   && (mi.context_window <= 0
                                       || (probe.measured && local
                                           && probe.server_wide < mi.context_window))) {
                            // server_wide是单个
                            // -model本地服务器（llama.cpp
                            // /props、LM Studio 已加载实例）
                            // 的测量窗口。它可能会缩小 过期 声明，
                            // 但仅限于本地端点，其中它是对正在运行的服务器的测量，
                            // 而不是网关-wide 目录 提示。
                            mi.context_window = probe.server_wide;
                        }
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        util::dbglog("openai.list_models.parse", e.what());
    } catch (...) {
        util::dbglog("openai.list_models.parse", "non-std exception");
    }
    caps.commit();   // 无论收集到什么，发布一次

    return result;
}

// ── 测试带 ─────────────────────────────────────────────────────────────
std::vector<Msg> parse_sse_for_test(std::string_view sse_bytes,
                                   std::vector<std::string> known_tools,
                                   bool allow_memory_salvage,
                                   bool reason_by_default,
                                   bool show_reasoning) {
    std::vector<Msg> out;
    StreamCtx ctx;
    ctx.show_reasoning = show_reasoning;
    ctx.known_tools = std::move(known_tools);
    ctx.allow_remember_salvage = allow_memory_salvage;
    ctx.allow_forget_salvage = allow_memory_salvage;
    ctx.allow_wipe_salvage = allow_memory_salvage;
    ctx.reason_by_default = reason_by_default;
    ctx.sink = [&out](Msg m) { out.push_back(std::move(m)); };
    feed_sse(ctx, sse_bytes.data(), sse_bytes.size());
    // 镜像run_stream_sync的终端保证：如果[DONE]从未到达，
    // 则合成关闭以便测试看到StreamFinished。
    if (!ctx.terminated) {
        close_open_tools(ctx);
        flush_reasoning_details(ctx);
        if (!try_salvage_tool_call(ctx)) flush_text_hold(ctx);
        ensure_nonempty_turn(ctx);
        ctx.sink(StreamFinished{ctx.stop_reason});
    }
    return out;
}

// NDJSON (本机/api/chat)测试工具。驱动器 feed_ndjson
// 然后镜像 run_stream_sync 的 终端 salvage/flush，
// 因此测试观察到实时本机路径生成的相同 Msg 序列。
std::vector<Msg> parse_ndjson_for_test(std::string_view ndjson_bytes,
                                       std::vector<std::string> known_tools,
                                       bool allow_memory_salvage) {
    std::vector<Msg> out;
    StreamCtx ctx;
    ctx.known_tools = std::move(known_tools);
    ctx.allow_remember_salvage = allow_memory_salvage;
    ctx.allow_forget_salvage = allow_memory_salvage;
    ctx.allow_wipe_salvage = allow_memory_salvage;
    ctx.sink = [&out](Msg m) { out.push_back(std::move(m)); };
    feed_ndjson(ctx, ndjson_bytes.data(), ndjson_bytes.size());
    if (!ctx.terminated) {
        close_open_tools(ctx);
        if (!try_salvage_tool_call(ctx)) flush_text_hold(ctx);
        ensure_nonempty_turn(ctx);
        ctx.sink(StreamFinished{ctx.stop_reason});
    }
    return out;
}

} // namespace agentty::provider::openai
