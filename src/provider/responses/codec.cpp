// agentty::provider::responses：OpenAI Responses API 方言的编解码器。
//
// 该文件是来自 src/provider/chatgpt/responses.cpp
// 的 EXTRACTED（通过重命名查看 git 历史记录）：对话编码器、
// 工具编码器和 SSE 状态机结果完全是主机 -neutral，
// 而每个后端只有 HTTP 信封不同。现在，每个讲这种方言的主机（
// ChatGPT/Codex 和 GitHub 副驾驶）都共享它，
// 并通过 `responses::Site` 描述符提供 per-host 差异。
// 契约和各主机实际差异的测量表见
// include/agentty/provider/responses/responses.hpp。
#include "agentty/provider/responses/responses.hpp"

#include <maya/runtime.hpp>
#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include <format>

#include "agentty/provider/stream_epilogue.hpp"
#include "agentty/provider/stream_scaffold.hpp"
#include "agentty/provider/error_class.hpp"
#include "agentty/provider/usage.hpp"
#include "agentty/provider/msg_shared.hpp"
#include "agentty/provider/wire.hpp"
#include "agentty/provider/debug.hpp"
#include "agentty/provider/wire/streamed.hpp"
#include "agentty/provider/wire_supersede.hpp"
#include "agentty/runtime/composer_attachment.hpp"
#include "agentty/util/base64.hpp"
#include "agentty/util/dbglog.hpp"

namespace agentty::provider::responses {
namespace {
using json = nlohmann::json;

// UTF-8 擦洗 — 粘贴的斑点/工具输出可以携带无效的内容UTF-8那个nlohmann::
// dump() 会抛出。用 U+ 替换格式错误的字节FFFD.
std::string scrub_utf8(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    const auto* p   = reinterpret_cast<const unsigned char*>(in.data());
    const auto* end = p + in.size();
    while (p < end) {
        unsigned char c = *p;
        if (c < 0x80) { out.push_back(static_cast<char>(c)); ++p; continue; }
        int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : -1;
        if (extra < 0 || p + extra >= end) { out.append("\xEF\xBF\xBD"); ++p; continue; }
        bool ok = true;
        for (int k = 1; k <= extra; ++k)
            if ((p[k] & 0xC0) != 0x80) { ok = false; break; }
        if (!ok) { out.append("\xEF\xBF\xBD"); ++p; continue; }
        out.append(reinterpret_cast<const char*>(p), extra + 1);
        p += extra + 1;
    }
    return out;
}

} // namespace：上方不依赖具体主机的辅助函数仅在本编译单元可见。

// ── agentty 答复 `input[]` 数组 ───────────────────────
//
// Rebuilds the whole turn history each 调用 (the Codex backend runs
// store:false，因此状态由客户端维护。每个助手 tool_call 都转换为
// function_call 条目，紧接对应的 function_call_output。
json build_input(const provider::Request& req, std::string_view site) {
    json input = json::array();
    // Count 终端-carrying tool results so each can be assigned a
    // recency rank (0 = newest) for the shared age-tiered 传输 budget.
    // 如果没有这个， 500 KiB grep 或 big-file `read` 会在随后的每个回合中重播 VERBATIM，
    // 在压缩之前很久就使 提示词 膨胀 - 通过 wire::cap_tool_result_aged 应用所有
    // 其他 传输层 的修复相同。 Errors never fade (the model may need
    // 需要完整错误才能恢复，而已处于预算内的结果
    // 保持原样发送。
    int total_tool_results = 0;
    for (const auto& m : req.messages)
        for (const auto& tc : m.tool_calls)
            if (tc.is_terminal()) ++total_tool_results;
    int seen_tool_results = 0;

    // 本次请求中图像单边尺寸的上限。默认较宽松，
    // 请求中的图像数量达到提供方阈值后再收紧。
    // 按整个消息列表计算，因为限制针对请求中的块总数，
    // 而非单个轮次。
    const unsigned max_side =
        util::wire_max_side(wire::wire_image_count(req.messages));
    const auto superseded = wire::superseded_read_ids(req.messages);
    for (const auto& m : req.messages) {
        if (m.role == Role::System) continue;   // 折叠成`instructions`

        const std::string text = m.attachments.empty()
            ? m.text
            : attachment::expand(m.text, m.attachments);

        if (m.role == Role::User) {
            json content = json::array();
            if (!text.empty())
                content.push_back({
                    {"type", "input_text"}, {"text", scrub_utf8(text)},
                });
            for (const auto* imgp : wire::wire_message_images(m, max_side)) {
                const auto& img = *imgp;
                content.push_back({
                    {"type", "input_image"},
                    {"image_url", "data:" + std::string{wire::wire_media_type(img)}
                                    + ";base64," + img.base64()},
                });
            }
            if (content.empty()) continue;
            input.push_back({
                {"type", "message"}, {"role", "user"},
                {"content", std::move(content)},
            });
            continue;
        }

        // 助手轮次：将其正文（如果存在）输出为 output_text 消息，
        // then each tool 调用 as function_call + function_call_output.
        //
        // FIRST重放任何捕获的推理项目。
        // 响应要求推理项位于其生成的消息/function_call 项之前（项
        // -pairing 不变量）。在 store:false 下，我们仅向服务器
        // `id` 发送 `encrypted_content` — 不（回显 rs_...
        // id 使后端在非 -persisted 响应上进行 404 查找）。
        if (!m.reasoning_encrypted.empty()
            && !site.empty() && m.reasoning_site == site) {
            std::size_t start = 0;
            while (start <= m.reasoning_encrypted.size()) {
                std::size_t nl = m.reasoning_encrypted.find('\n', start);
                std::string blob = m.reasoning_encrypted.substr(
                    start, nl == std::string::npos ? std::string::npos : nl - start);
                if (!blob.empty())
                    input.push_back({
                        {"type", "reasoning"},
                        {"summary", json::array()},
                        {"encrypted_content", std::move(blob)},
                    });
                if (nl == std::string::npos) break;
                start = nl + 1;
            }
        }

        if (!text.empty()) {
            input.push_back({
                {"type", "message"}, {"role", "assistant"},
                {"content", json::array({
                    json{{"type", "output_text"}, {"text", scrub_utf8(text)}}})},
            });
        }
        for (const auto& tc : m.tool_calls) {
            std::string args = tc.args.is_null() ? "{}" : tc.args.dump();
            input.push_back({
                {"type", "function_call"},
                {"call_id", tc.id.value},
                {"name", tc.name.value},
                {"arguments", scrub_utf8(args)},
            });
            // 每个 function_call 必须 后面跟着它的
            // function_call_output 或下一个请求 400 秒（
            // "No tool output found for function call"），（没有找到函数调用对应的工具输出。）
            // 并且因为历史记录每回合重播，所以 线程 保持楔形。从未完成的
            // 调用（已取消、数据流 死亡，在构建请求时仍在运行）会获得合成错误输出，
            // 与 Anthropic 和聊天 传输层 遵循相同的规则。
            // in-memory ToolUse 未受影响。
            if (!tc.is_terminal()) {
                input.push_back({
                    {"type", "function_call_output"},
                    {"call_id", tc.id.value},
                    {"output", "(tool call did not complete \u2014 previous turn ended before this tool produced a result)"},
                });
                continue;
            }
            {
                // Age-tiered 传输 预算（与其他 传输层 共享）：
                // 最新结果保持了全部预算，过期 成功逐渐
                // 变得紧张+末尾，因此大转储停止每回合重播。
                const int recency_rank =
                    total_tool_results - 1 - seen_tool_results;
                ++seen_tool_results;
                const bool is_error = tc.is_failed() || tc.is_rejected();
                std::string out = (!is_error && superseded.count(tc.id.value))
                    ? std::string{wire::kSupersededReadPointer}
                    : wire::cap_tool_result_aged(
                          tc.output(), recency_rank, is_error);
                // 愿景tool_result：当工具显示图像（在图像文件上
                // 读取）时，响应API接受`output`作为内容部分的
                // ARRAY——文本后跟input_image部分——
                // 而不是纯字符串。与其他方言相同的 SSOT 选择器；
                // 这里只有JSON形状是OpenAI-specific。
                auto imgs = wire::wire_tool_result_images(tc, max_side);
                if (imgs.empty()) {
                    input.push_back({
                        {"type", "function_call_output"},
                        {"call_id", tc.id.value},
                        {"output", scrub_utf8(out)},
                    });
                } else {
                    json parts = json::array();
                    if (!out.empty())
                        parts.push_back({{"type", "output_text"},
                                         {"text", scrub_utf8(out)}});
                    for (const auto* imgp : imgs) {
                        const auto& img = *imgp;
                        parts.push_back({
                            {"type", "input_image"},
                            {"image_url",
                             "data:" + std::string{wire::wire_media_type(img)}
                                 + ";base64," + img.base64()},
                        });
                    }
                    input.push_back({
                        {"type", "function_call_output"},
                        {"call_id", tc.id.value},
                        {"output", std::move(parts)},
                    });
                }
            }
        }
    }
    return input;
}

json build_tools(const provider::Request& req) {
    json tools = json::array();
    for (const auto& t : req.tools) {
        if (t.defer_loading) continue;   // Anthropic-only概念；从来没有在这里
        // 响应API功能工具是FLAT(顶层的name/description/parameters)，
        // 与聊天完成的嵌套{function:{...}}不同。
        // null-schema 保护与 Chat/Ollama 编码器 (SSOT) 共享。
        tools.push_back({
            {"type", "function"},
            {"name", t.name},
            {"description", t.description},
            {"parameters", wire::tool_schema_or_empty(t.input_schema)},
        });
    }
    return tools;
}

// NEUTRAL响应请求正文。这里的一切都适用于方言本身。只有 一个 后端的任何
// 真实情况都属于该主机的 Site::decorate_body（ChatGPT 的
// 存储：false + 加密的-reasoning include[]，Copilot
// 的 reasoning.summary 模式，...）。
//
// `model`在这里故意被默认为NOT：主机可以重写slug（副驾驶
// 的自动会话选择服务器-blessed型号），因此呼叫者的Target::
// model是权威的，并且stream()在修饰后对其进行标记。
json build_body(const provider::Request& req, std::string_view site) {
    json body{
        {"model", req.model},
        {"instructions", scrub_utf8(req.system_prompt)},
        {"input", build_input(req, site)},
        {"tool_choice", "auto"},
        {"parallel_tool_calls", true},
        {"stream", true},
    };
    if (auto tools = build_tools(req); !tools.empty()) body["tools"] = tools;
    // 推理阶梯。 `summary: auto` 使推理模型返回 human
    // -readable 摘要文本 (response.reasoning_summary_text.
    // *)，而不是默默地燃烧思考标记 - 在 ChatGPT 和
    // Copilot 上测量。
    //
    // 对于NO努力层，该字段被完全省略。在这种情况下，
    // 它曾经发送 `reasoning: {summary: auto}`，
    // 而不是 "no opinion" — 这是对推理摘要的明确请求。
    // 选择面板 的条带读取 `reasoning ‹off›`，
    // 而每个请求都要求它，因此用户看到他们已关闭并支付代
    // 币的思维块。努力带是唯一选择的地方，因此 `off`
    // 必须意味着 传输 上的关闭。
    if (!req.effort.empty())
        body["reasoning"] = json{{"effort", req.effort}, {"summary", "auto"}};
    else if (req.no_reasoning
             && req.reasoning_off
                    == static_cast<std::uint8_t>(::agentty::ReasoningOff::EffortNone))
        // 默认gpt-5.1+原因；将该字段排除在外是 "medium"。
        body["reasoning"] = json{{"effort", "none"}};
    // 输出上限。聊天发送 `max_tokens`，Anthropic 发送 `max_tokens`，
    // Ollama 发送 `num_predict`——这种方言拼写为
    // `max_output_tokens`，并且用于根本不发送任何内容。
    //
    // 中立请求上的字段是对用户的承诺。被四个实现中的
    // 三个实现所保留，比没有实现更糟糕：对于编写它的人来说，
    // 它看起来是正确的，而对于忽略它的人来说，
    // 它看起来是任意的。这正是 切换 的推理在几个
    // 月内被破坏的原因（参见上面的注释），因此即使没有人报告它，
    // 同样的阅读也适用于这里。
    //
    // 0 表示 "no opinion" 并省略该字段 — 服务器的
    // 默认值是比我们发明的数字更好的答案。
    if (req.max_tokens > 0) body["max_output_tokens"] = req.max_tokens;
    return body;
}

// ── SSE 分派 状态 ─────────────────────────────────────────────────────
// 一件打开的function_call项目。 `args` 从响应方言可能使用的所有四个
// 载体中累积 - output_item.added 上的 `arguments`、.
// delta 片段、.done 快照 以及 output_item.done 上的项目 快照。
// unseen()使冗余的没有-ops，因此我们路由每一个承运商并且没有
// 一个是我们忘记的。 （副驾驶发送 仅 .done；我们过去常常将它和 分派
// 每个工具 调用 与 `{}` 一起丢弃。）
struct ToolSlot {
    std::string call_id;   // id a tool_result 必须回显
    std::string args;
    // 该项目在响应的output[]数组中的位置。
    //
    // 这是副驾驶的ONLY身份完好的委托书。它将参数 帧 上的 `item_id`
    // 重写为 ~440-char base64 blob（其加密的
    // 推理内容），与它曾经宣布的任何项目都不匹配，因此每个运营商看起来都
    // 无法寻址，并且所有 45 个工具都通过 `{}` 调度。
    // 与 openclaw#72602 和 copilot-sdk 报告
    // 相同的根本原因：代理的 id 重写不围绕 -trip。
    //
    // -1=打开该调用的帧不携带output_index。
    int output_index = -1;
};

// ── 这套 调用 无法任意选择 ──
//
// 普通集合暴露begin()，无序容器上的`*s.begin()`是装扮成
// 选择的ARBITRARY元素。这正是该编解码器带来的错误：名为
// `latest_tool_item` 的 回退 是从
// `*open_tool_items.begin()` 刷新的，
// 因此 "latest" 意味着哈希发生的任何一项，并且未寻址的参数片段被附加到 coin
// -flip 调用 上。
//
// 纪律测试扫描该字面形状，但扫描仪只能捕获它所知道的拼写——`.back()`、
// `.front()`、"most recent"时间戳以及同一
// 错误的十几个其他拼写会绕过它。所以真正的修复在这里，在类型中：这暴露
// 了 `sole()`，
// 它回答 "is there exactly one, and if so which"，
// 以及 无内容 回答 "give me any one of them"。
// 未来的解码器无法重新引入该错误，因为没有成员可以重新引入该错误。
//
// drain() 的存在是因为在回合结束时关闭每个打开的
// 调用 确实需要所有它们，并且当您拿取批次时顺序并不重要。
class OpenCalls {
public:
    void open(std::string item_id) { items_.insert(std::move(item_id)); }
    bool close(const std::string& item_id) { return items_.erase(item_id) > 0; }

    [[nodiscard]] std::size_t size()  const noexcept { return items_.size(); }
    [[nodiscard]] bool        empty() const noexcept { return items_.empty(); }

    // 一个打开调用，或者当有零个或多个时nullopt。
    //
    // 这是产生单个id的ONLY读取，并且它是全部的：
    // "many"情况没有答案，因此调用者不会意外地收到猜测。
    // 与旧的 `*begin()` 相比，它总是返回一些东西，
    // 并且仅靠运气才正确。
    [[nodiscard]] std::optional<std::string> sole() const {
        if (items_.size() != 1) return std::nullopt;
        return *items_.begin();   // 归属-ok：size()==1，无顺序错误
    }

    // 每开调用，就结束-of-turn扫。全部采用并不是
    // 一种选择，因此没有什么可出错的。
    [[nodiscard]] std::vector<std::string> drain() {
        std::vector<std::string> out(items_.begin(), items_.end());
        return out;
    }

private:
    std::unordered_set<std::string> items_;
};

struct StreamCtx {
    EventSink sink;
    wire::SseFramer sse;
    // item.id (fc_…) → 打开 调用 的插槽。由 item_id 键控，
    // 因为这是 事件 所携带的参数；该插槽保存着它们必须在下面转发的
    // call_id。
    std::unordered_map<std::string, ToolSlot> tools;
    OpenCalls open_tool_items;
    bool text_block_open = false;
    // 镜子req.show_reasoning。其他三个 传输层（聊天、
    // ollam、anthropic）已经开始此操作；响应编解码器没有，
    // 因此转向推理 OFF 的用户仍然可以看到使用此方言的每个模型的
    // 摘要（Copilot 的 gpt-*、ChatGPT/Codex）。
    // 切换 看起来很糟糕，因为它使用的是四种方言中的一种。
    //
    // HIDDEN仍然发出EMPTY ThinkingDelta
    // 而不是什么也不发出，与聊天传输层使用的形状相同：事件是回
    // 合的活跃信号（它重置last_event_at和
    // 状态更新函数中的重试计数器），因此完全吞下它将使长时间无声的
    // 推理阶段看起来像停滞的数据流。空文本不存储任何内容。
    bool show_reasoning = true;
    // 该 数据流 正在与哪个主机（responses::Site::id)通话。
    // 标记在每个捕获的推理块上，以便重播可以
    // 检查密文是否返回到可以解密它的后端。
    std::string site;
    bool saw_function_call = false;
    bool saw_refusal = false;
    bool terminated = false;
    // ── 整个反应中计算出的健康参数-routing ────────
    //
    // 速率，而不是事件。 GitHub 的客户端
    // 报告了遥测中抢救的工具输入以及配额，
    // 这表明代理 传输 上的参数丢失是要测量的正常状态，
    // 而不是记录一次并被遗忘的错误。
    //
    // 对 `response.completed` 进行总结，
    // 因此每轮一行显示该 传输 是否健康——而不是需要有人在
    // 20 MB 日志中计算警告，而这正是副驾驶诊断实际采取的。
    int salvaged_args  = 0;   // 已恢复，因为恰好有一个 调用 打开
    int unroutable_args = 0;  // 掉落：没有任何东西可以安全地拥有它们
    StopReason stop = StopReason::EndTurn;
    // 诊断：本轮到达了多少个推理-bearing 事件。
    // 让 "reasoning is not showing" 报告仅从日志中
    // 拆分为两个 真实 原因：0 → 服务器从未发送摘要文本（model/account
    // 层不发出它），>0 → 它到达并且丢失是 client-side (
    // reducer/render)。最后浮现出-of-turn 调试线。
    int thinking_deltas = 0;
};

// 参数事件属于哪个开放调用？
//
// 每当服务器发送一个时，`item_id`就是答案，
// 这几乎总是如此。当它不存在时，只有当 一个 调用 正好打开时，
// 我们才可能回退——那么就没有什么问题了。
//
// 在有几个开放的情况下，任何选择都是抛硬币，失败的一方将
// 一个 调用 的论点附加到另一个。结果仍然是 PARSES，
// 因此损坏是静默的并到达工具调用：`edit` 使用
// 属于 `shell` 的路径运行。返回空会丢弃片段，
// 然后回合会因缺少必填字段而明显失败——这比使用另一个
// 调用 的 字节 调度工具要好。
//
// 这曾经回退到 `latest_tool_item`，这听起来是有序的，但实际上并非如此：它是在
// unordered_set 上从 `*open_tool_items.begin()` 刷新的，
// 因此 "latest" 是哈希碰巧产生的任何一项。与聊天解码器的 wire::ToolCallTracker
// 相同的规则，返回 Ambigitude 而不是 pick；
// 参见 include/agentty/provider/wire/tool_calls.hpp.
[[nodiscard]] std::string addressed_item(const StreamCtx& ctx, const json& j) {
    // (1) output_index — 响应的输出[]数组中的项目的槽。
    //
    // 在 item_id 之前尝试了 首次，因为它是代理在不对
    // 其所描述的数组进行重新编号的情况下无法重写的唯一标识。
    // Copilot 的代理将参数 帧 上的 `item_id` 替换为 ~440-char
    // base64 blob（其加密的推理内容），该 blob 与它所宣布的任何内容都不
    // 匹配 — 因此 item_id 是非 -empty，看起来很权威，无人解析，
    // 并且每个工具都以 `{}` 调度。位置匹配完全回避了重写。 openclaw#72602
    // ("Encrypted content item_id did not match the target item id")（加密内容的 item_id 与目标条目标识不一致。）
    // 和 github/copilot-sdk#615
    // 中记录了相同的故障。
    if (const auto oi = j.find("output_index");
        oi != j.end() && oi->is_number_integer()) {
        const int idx = oi->get<int>();
        for (const auto& [item, slot] : ctx.tools)
            if (slot.output_index == idx) return item;
        // 我们没有调用的位置是NOT，这是导致重写
        // item_id的原因：服务器确切地告诉我们它
        // 意味着哪个槽，但我们没有它。
        // 仅通过下面明确的路径继续查找。
    }

    // (2)item_id-在每个井-behaved响应服务器上正确，
    // 并且当帧不携带output_index时仍然是唯一的句柄。
    if (const auto it = j.find("item_id");
        it != j.end() && it->is_string()
        && !it->get_ref<const std::string&>().empty()) {
        const auto& id = it->get_ref<const std::string&>();
        // 仅当它命名为我们实际打开的调用时才信任它。
        // 一个没有解析的 id 不是权威，而是噪音——将其视为权威
        // 是抑制下面的 sole() 回退 并将可路由的 single
        // -call 转变成 `{}` 的原因。
        if (ctx.tools.find(id) != ctx.tools.end()) return id;
    }

    // (3)恰好一个候选意味着不存在错误的顺序，
    // 并且 OpenCalls::sole() 是唯一可以回答的
    // 读取——它返回零个或多个的 nullopt，
    // 而不是交回任意项目。
    if (auto only = ctx.open_tool_items.sole()) return *only;
    return {};
}

// 将参数载荷从ANY载波路由到时隙中并且仅转发新已知的内容。
// `total` = 服务器的完整视图，而不是片段。
void feed_tool_args(StreamCtx& ctx, const std::string& item_id,
                    std::string_view payload, bool total) {
    auto it = ctx.tools.find(item_id);
    if (it == ctx.tools.end()) {
        // 我们无法路由的参数载荷是一个工具调用，
        // 它将因"[invalid args]"而失败——型号DID
        // 发送参数，我们只是无法附加它们（物品从未打开/id不匹配/我们不知道的载体事件形状）。
        // 正是 `.done` bug 的失败形态；
        // 决不能保持沉默。
        //
        // ── SALVAGE ────────────────────────────────────────────────
        //
        // GitHub自己的客户端在其遥测中传送
        // `copilot_salvaged_tool_input_ids`
        // ——它重建未能路由的工具输入并报告频率。除非它在生产中针对
        // 您自己的服务器触发，否则您不会构建它。教训是，代理 传输
        // 上的参数丢失是 EXPECTED 状态，而不是错误。
        //
        // 所以恢复，但仅在恢复不会出错的情况下进行：正好一个调用打开
        // 意味着没有顺序出错并且没有第二个候选者损坏。
        // `sole()` 回答了这个问题，仅此而已 - 它返回
        // nullopt 为 0 OR 许多，因此这不能衰减为
        // "pick the most recent"，这是整个 接口边界 存在要
        // 防止的错误。
        //
        // 随着几个调用的开放，我们仍然下降。重写的 id
        // 加上两个候选者就像抛硬币一样，失败的一方会产生有效
        // 的 JSON 并被分配给错误的工具——这比明显损坏
        // 的回合还要糟糕。
        if (auto only = ctx.open_tool_items.sole();
            only && ctx.tools.count(*only)) {
            AGT_LOG(Wire, Warn, "responses.tool_args_salvaged",
                    "item_id={} recovered_to={} bytes={}",
                    item_id, *only, payload.size());
            ++ctx.salvaged_args;
            it = ctx.tools.find(*only);
        } else {
            AGT_LOG(Wire, Warn, "responses.tool_args_unroutable",
                    "item_id={} known_items={} open={} bytes={}",
                    item_id, ctx.tools.size(),
                    ctx.open_tool_items.size(), payload.size());
            ++ctx.unroutable_args;
            return;
        }
    }
    const auto fresh = wire::unseen(it->second.args, payload, total);
    // 哪个CARRIER交付了字节，以及它们是否是新的。
    // 这是分隔工具 调用 最终与 `{}` 的两种方式的线：
    //
    //   根本没有routed=行→服务器没有向任何运营商发送
    //                             任何参数（或者它们去了上面的
    //                             tool_args_不可路由，
    //   单独记录）routed=和fresh=0→字节到达但
    //                             我们已经有了它们，i.e。
    //   冗余载体 — 健康的routed=新鲜>0 → 参数真正积累
    //
    // Copilot 发送 仅 和 `.done` 快照，因此在健康的
    // Copilot 轮流中，这是 调用 的单条routed=线路，
    // total=1，fresh=<all of it>。如果该行丢失，
    // 则错误是解码器的 上游。
    AGT_LOG(Wire, Debug, "responses.tool_args_routed",
            "item_id={} call_id={} total={} bytes={} fresh={} have={}",
            item_id, it->second.call_id, total ? 1 : 0, payload.size(),
            fresh.size(), it->second.args.size());
    if (!fresh.empty())
        ctx.sink(StreamToolUseDelta{ToolCallId{it->second.call_id},
                                    std::string{fresh}});
}

void close_tool(StreamCtx& ctx, const std::string& item_id) {
    if (item_id.empty() || !ctx.open_tool_items.close(item_id)) return;
    if (const auto it = ctx.tools.find(item_id); it != ctx.tools.end()) {
        // 对于该调用的判决，目前已交给状态更新函数。
        // 这里的 `args=0` 是用户看到的
        // "[invalid args] path required" 的
        // 确切症状 - 将其放在日志中并附加 call_id 会将其从猜测
        // 转变为查找，上面的routed=行表示 字节 是否到达。
        AGT_LOG(Wire, Debug, "responses.tool_closed",
                "item_id={} call_id={} args={}",
                item_id, it->second.call_id, it->second.args.size());
        ctx.sink(StreamToolUseEnd{ToolCallId{it->second.call_id}});
    }
}

void close_all_tools(StreamCtx& ctx) {
    std::vector<std::string> ids = ctx.open_tool_items.drain();
    for (const auto& id : ids) close_tool(ctx, id);
}

void emit_usage(StreamCtx& ctx, const json& usage) {
    // 共享提取器——参见usage::from_responses（
    // Responses/Codex使用形状的单一事实来源）。
    if (auto su = usage::from_responses(usage)) ctx.sink(*su);
}

void dispatch(StreamCtx& ctx, std::string_view data) {
    if (data.empty() || data == "[DONE]") return;
    // 在任何解释之前的基本事实。聊天 传输层 已在 Trace 处转储其请求 正文；
    // 这是响应的一半，这就是 "the server never sent the arguments"
    // 与 "we failed to route them"
    // 的解决方案，无需另一轮猜测。
    AGT_LOG(Wire, Trace, "responses.frame", "raw={}", data);
    json j;
    try { j = json::parse(data); } catch (const std::exception& e) {
        // 帧我们甚至不能解析是DROPPED 传输 事件——默默失去的最
        // 昂贵的东西（掉落的output_text/function_call
        // 帧读作"model went mute"或"invalid args"，
        // 从来不是它本来的样子）。警告：发布时默认开启，
        // 并降落在crash-ring飞行记录器中。
        AGT_LOG(Wire, Warn, "responses.frame_unparseable",
                "err={} bytes={} head={}", e.what(), data.size(),
                data.substr(0, 256));
        return;
    }

    const auto type = j.value("type", std::string{});

    if (type == "response.output_text.delta") {
        if (!ctx.text_block_open) ctx.text_block_open = true;
        ctx.sink(StreamTextDelta{j.value("delta", std::string{})});
        return;
    }
    // 该模型下降了。它的拒绝文本是用户得到的唯一解释，
    // 因此像任何回复一样显示它，并以拒绝结束回合。
    if (type == "response.refusal.delta") {
        ctx.saw_refusal = true;
        if (!ctx.text_block_open) ctx.text_block_open = true;
        ctx.sink(StreamTextDelta{j.value("delta", std::string{})});
        return;
    }
    if (type == "response.refusal.done") {
        ctx.saw_refusal = true;
        return;
    }
    if (type == "response.reasoning_summary_text.delta"
        || type == "response.reasoning_text.delta") {
        ++ctx.thinking_deltas;
        ctx.sink(StreamThinkingDelta{
            ctx.show_reasoning ? j.value("delta", std::string{}) : std::string{},
            {}});
        return;
    }
    // 摘要PART边界：响应API将推理摘要分成多个部分（每个段落
    // 一个）并且针对每个响应发出多个推理ITEMS（每个工具调用之前一个）。
    // 否则，它们的文本增量将在没有分隔符的情况下连接 (
    // 第一段思考末尾与第二段思考开头直接粘连。
    // 发出块边界，以便 状态更新函数 插入段落分隔符 -
    // 与 事件 Anthropic 相同用于新的思维块。
    if (type == "response.reasoning_summary_part.added") {
        if (ctx.show_reasoning)
            ctx.sink(StreamThinkingDelta{{}, {}, /*block_boundary=*/true});
        return;
    }
    if (type == "response.output_item.added") {
        const auto& item = j.value("item", json::object());
        const auto itype  = item.value("type", std::string{});
        if (itype == "reasoning") {
            // 新推理项——其摘要文本的段落边界（见上文）。
            // 如果该项目不产生可见文本，则无害。
            // 当推理被隐藏时被抑制：周围没有文本的
            // 边界将打开一个空的思维块。
            if (ctx.show_reasoning)
                ctx.sink(StreamThinkingDelta{{}, {}, /*block_boundary=*/true});
        }
        if (itype == "function_call" || itype == "custom_tool_call") {
            // 新工具调用打开。首先关闭任何先前的文本块，以便 逐步显示 光
            // 标在卡片之前捕捉（与 Anthropic 接口边界 匹配）。
            //
            // `custom_tool_call`在定制
            // -tool协议下是相同的：命名的调用，
            // 其参数数据流在custom_tool_call_input
            // 载体上而不是function_call_arguments载体上。
            // 它打开了一个相同的槽，因为 下游 的所有内容——归因、
            // 论点积累、ToolUse 和 状态更新函数 构建
            // ——都是同一个问题。此处仅识别 `function_call`
            // 将使载体 (5) 连接到从未存在的插槽，
            // 这是直接丢弃 事件 的更令人困惑的版本。
            if (ctx.text_block_open) {
                ctx.text_block_open = false;
                ctx.sink(StreamTextBlockClosed{});
            }
            const std::string item_id = item.value("id", std::string{});
            const std::string call_id = item.value("call_id", item_id);
            const std::string name    = item.value("name", std::string{});
            // 该项目在响应的output[]数组中占据的槽。
            // 此处捕获是因为这是 item_id 值得信赖的
            // 帧（服务器正在宣布该项目，而不是引用它），
            // 因此它是位置和身份仍然可以绑定在一起的地方。
            // 后来的参数 帧 仅携带副驾驶的位置 - 请
            // 参阅 addressed_item。
            const int out_idx = (j.contains("output_index")
                                 && j["output_index"].is_number_integer())
                              ? j["output_index"].get<int>() : -1;
            ctx.tools[item_id] = ToolSlot{call_id, {}, out_idx};
            ctx.open_tool_items.open(item_id);
            ctx.saw_function_call = true;
            // 身份配对，每调用记录一次。 item_id是后面参数
            // 帧的地址； call_id 是 状态更新函数 和工具
            // 分派器 使用的。当它们不同时（在 Copilot 上），
            // 这里的不匹配是参数着陆和参数去
            // tool_args_unroutable 之间的差异。
            AGT_LOG(Wire, Debug, "responses.tool_open",
                    "item_id={} call_id={} name={} output_index={}",
                    item_id, call_id, name.empty() ? "<empty>" : name,
                    out_idx);
            ctx.sink(StreamToolUseStart{ToolCallId{call_id}, ToolName{name}});
            // 运营商(1)：一些后端在`added`上将整个args字符串
            // up-front交付。 A 快照，因此将其作为总计进行路由。
            // `input`是同一字段的自定义-tool拼写。
            auto a0 = item.value("arguments", std::string{});
            if (a0.empty()) a0 = item.value("input", std::string{});
            if (!a0.empty())
                feed_tool_args(ctx, item_id, a0, /*总计=*/true);
        }
        return;
    }
    // 载体（2):增量片段。 Codex 发送的内容。
    if (type == "response.function_call_arguments.delta") {
        feed_tool_args(ctx, addressed_item(ctx, j),
                       j.value("delta", std::string{}), /*总计=*/false);
        return;
    }
    // 运营商(3)：终止争论数据流的权威快照。 GitHub
    // Copilot 的代理合并并发送 仅，因此忽略它的编解码器
    // 会使用 `{}` 调度每个工具 调用。对于也传输片段的服务器来说，
    // 这是一个 no-op，因为 observe_total
    // () 仅返回看不见的后缀（通常什么也没有）。
    if (type == "response.function_call_arguments.done") {
        feed_tool_args(ctx, addressed_item(ctx, j),
                       j.value("arguments", std::string{}), /*总计=*/true);
        return;
    }
    // 载体(5):通过CUSTOM工具-call输入数据流。与载体（2）和（3）
    // 相同的两个形状 - `.delta` 碎片，然后是 `.done` 快照 -
    // 但使用不同的 事件 名称，并且 载荷 位于 `input` 而不是
    // `delta`/`arguments`。
    //
    // 使用自定义工具调用的模型在此传递其参数ONLY。
    // 我们过去常常掉进 unhandled_event 并丢弃它们，
    // 这与运营商 (3) 在 Copilot 上造成的相同无声 `{}`
    // 分派 — 出于同样的原因：无人枚举的运营商。 GitHub 自己的客户端
    // 反序列化了这个（`response.custom_tool_call_input.delta`
    // 位于其 事件 表中的其他客户端旁边），这就是提示检查
    // 的原因。
    //
    // 与所有其他运营商一样通过相同的 feed_tool_args
    // 接口边界 进行路由，因此归属、快照-vs-fragment
    // 协调和记录通过构造而不是通过谨慎而相同。
    if (type == "response.custom_tool_call_input.delta") {
        feed_tool_args(ctx, addressed_item(ctx, j),
                       j.value("delta", std::string{}), /*总计=*/false);
        return;
    }
    if (type == "response.custom_tool_call_input.done") {
        feed_tool_args(ctx, addressed_item(ctx, j),
                       j.value("input", std::string{}), /*总计=*/true);
        return;
    }
    if (type == "response.output_item.done") {
        const auto& item = j.value("item", json::object());
        const auto itype = item.value("type", std::string{});
        if (itype == "function_call" || itype == "custom_tool_call") {
            const std::string item_id = item.value("id", std::string{});
            // 载体(4)：完成的项目可以重述完整的论点。
            // 在发送 调用 之前了解它们的最后机会——当较早
            // 的承运商已经提供它们时，为 no-op。
            //
            // 自定义工具调用在`input`下携带相同的快照，
            // 而不是`arguments`；尝试两者而不是假设
            // 哪个协议产生了这个项目。这里贴错标签会花费一
            // 整个 调用 的参数，而查找的成本就是一次地图查找。
            auto a = item.value("arguments", std::string{});
            if (a.empty()) a = item.value("input", std::string{});
            if (!a.empty())
                feed_tool_args(ctx, item_id, a, /*总计=*/true);
            close_tool(ctx, item_id);
        }
        else if (itype == "reasoning") {
            // 推理项完成。捕获其不透明的 encrypted_content，
            // 以便 状态更新函数 可以将其存储在助手消息中并在下一轮重播（在
            // store:false 下跨工具轮次的 chain-of-thought
            // 连续性）。可见摘要已通过 reasoning_summary_text.delta
            // → StreamThinkingDelta 传输。
            if (auto enc = item.value("encrypted_content", std::string{});
                !enc.empty())
                ctx.sink(StreamReasoning{std::move(enc), ctx.site});
            if (ctx.text_block_open) {
                ctx.text_block_open = false;
                ctx.sink(StreamTextBlockClosed{});
            }
        }
        else if (ctx.text_block_open) {
            ctx.text_block_open = false;
            ctx.sink(StreamTextBlockClosed{});
        }
        return;
    }
    if (type == "response.completed") {
        close_all_tools(ctx);
        const auto& resp = j.value("response", json::object());
        emit_usage(ctx, resp.value("usage", json::object()));
        // 传输 上没有 finish_reason — 输出中的
        // function_call 意味着模型在继续之前需要工具结果。
        ctx.stop = ctx.saw_refusal       ? StopReason::Refusal
                 : ctx.saw_function_call ? StopReason::ToolUse
                                         : StopReason::EndTurn;
        AGT_LOG(Wire, Debug, "responses.completed",
                "stop={} tool_calls={} thinking_deltas={} "
                "salvaged_args={} unroutable_args={}",
                ctx.saw_function_call ? "tool_use" : "end_turn",
                ctx.tools.size(), ctx.thinking_deltas,
                ctx.salvaged_args, ctx.unroutable_args);
        ctx.sink(StreamFinished{ctx.stop});
        ctx.terminated = true;
        return;
    }
    if (type == "response.incomplete") {
        close_all_tools(ctx);
        const auto& resp = j.value("response", json::object());
        emit_usage(ctx, resp.value("usage", json::object()));
        const auto reason = resp.value("incomplete_details", json::object())
                                .value("reason", std::string{});
        // 警告：不完整的响应被截断，转给用户WILL通知； 传输 原因（
        // "max_output_tokens"、"content_filter"...）是
        // 对 "why did it stop mid-sentence?" 报告的答案。
        AGT_LOG(Wire, Warn, "responses.incomplete", "reason={}", reason);
        ctx.stop = reason == "max_output_tokens" ? StopReason::MaxTokens
                 : reason == "content_filter"    ? StopReason::Refusal
                                                 : StopReason::EndTurn;
        ctx.sink(StreamFinished{ctx.stop});
        ctx.terminated = true;
        return;
    }
    if (type == "response.failed" || type == "error") {
        close_all_tools(ctx);
        std::string msg;
        // 在消息旁边显示错误 TYPE/CODE。 运行时 的分类（string_view）
        // 嗅探此文本来决定可重试性（e.g."rate_limit"，"429"，
        // "overloaded"，"server_error"） - 删除类型会将
        // 瞬态过载错误分类为 终端 并跳过 Anthropic/OpenAI
        // 获得的 auto-retry。镜像 传输 的 in-band 错误形状：
        // `{type|code}: {message}`。
        auto compose = [](const json& err) {
            std::string m = err.value("message", std::string{});
            std::string tag = err.value("type", err.value("code", std::string{}));
            if (!tag.empty()) return m.empty() ? tag : tag + ": " + m;
            return m;
        };
        if (type == "error") {
            // top-level `error` 事件：其自身的`type`字段是SSE
            // 事件鉴别器("error")，因此有意义的分类器标记是`code`(e.g.
            // "rate_limit_exceeded"，"server_error")。
            // 有些变体将细节嵌套在`error`下；处理两者。
            const json& err = j.contains("error") && j["error"].is_object()
                                  ? j["error"] : j;
            std::string m   = err.value("message", j.value("message", std::string{}));
            std::string tag = err.value("code", err.value("type", std::string{}));
            if (tag == "error") tag.clear();   // 从来没有事件鉴别器
            msg = tag.empty() ? m : (m.empty() ? tag : tag + ": " + m);
            if (msg.empty()) msg = "stream error";
        } else {
            const auto& err = j.value("response", json::object())
                                  .value("error", json::object());
            msg = compose(err);
            if (msg.empty()) msg = "Codex request failed";
        }
        const json& err_obj = j.contains("error") && j["error"].is_object()
                                  ? j["error"] : j;
        std::string code = err_obj.value("code", err_obj.value("type", std::string{}));
        if (code == "error") code.clear();   // 事件鉴别符，不是代码
        if (code.empty())
            code = j.value("response", json::object())
                       .value("error", json::object())
                       .value("code", std::string{});
        // 服务器已经带类型的出现此故障。将其映射到等效的 HTTP 状态，
        // 以便 状态更新函数 通过经过验证的分类（
        // HttpError）表进行分类，而不是嗅探散文
        // - 当 SSE 错误 正文 到达时，状态行早已消失（
        // 它是 200），因此如果没有这个，重试 梯子就会从人类
        // 句子中猜测。 0 = 无法识别，保留字符串路径。
        const int status = provider::wire_error_type_status(code);
        // 完整的 传输 错误 事件，不仅仅是组成的一个 -liner：
        // 事件 可能携带字段 compose() 没有出现（参数、请求 id），
        // 这些字段决定提供者-side 票证与客户端错误。
        AGT_LOG(Wire, Warn, "responses.error_event",
                "code={} status={} msg={} raw={}",
                code.empty() ? "<none>" : code, status, msg,
                data.substr(0, 2048));
        ctx.sink(StreamError{msg, std::nullopt, status});
        ctx.terminated = true;
        return;
    }
    // response.created/in_progress/content_part.* /*_summary_part.*等。
    // 是结构性的——与 渲染 无关。提高活跃度，让 看门狗
    // 知道 传输 在长时间的推理过程中是健康的。
    //
    // 任何我们不认识的东西也会出现在这里，而沉默正是让 Copilot
    // bug 如此昂贵的原因：`.done` 携带了工具参数，我们忽略了它，
    // 唯一的症状是 "[invalid args] pattern required"
    // ——它读起来是一个坏模型，而不是一个被丢弃的 事件。
    // 掉下去的仍然是右边的BEHAVIOUR（未知的事件通常是结构性的）；
    // 它只是不能是看不见的。每个 事件 类型都有一个调试行，
    // 已进行重复数据删除，因此一种新的方言怪癖会自行命名，而不是归咎于模型。
    if (!type.empty() && !type.starts_with("response.created")
        && !type.starts_with("response.in_progress")
        && !type.starts_with("response.content_part")
        && !type.starts_with("response.output_text.done")
        && !type.contains("_summary_part")) {
        // 锁-free重复数据删除：赛车第一次观测可能会记录两次，
        // 这对于诊断线来说是无害的。
        static maya::snapshot<std::set<std::string>> seen;
        if (!seen.load()->contains(std::string{type})) {
            seen.update([](const std::set<std::string>& s, const std::string& t) {
                auto next = s; next.insert(t); return next;
            }, std::string{type});
            // 在WIRE频道上，而不是dbglog。
            // 未知的 事件 是有关此方言的错误报告中最有用的
            // 一行 - 在 `AGENTTY_LOG=wire=debug` 下，
            // 调试 传输层 的过滤器实际运行，
            // dbglog 行根本不出现。在您要观看的
            // 一个频道上不可见的掉线载波是掉线两次。
            AGT_LOG(Wire, Warn, "responses.unhandled_event", "type={}", type);
            util::dbglog("responses.unhandled_event", type);
        }
    }
    ctx.sink(StreamHeartbeat{});
}

// ── 共享的传输层 ──────────────────────────────────────────────────
//
// 此行以上的所有内容都是主机-neutral。 HOST 的所有
// 不同之处都通过 `site` 到达：POST 的位置、凭据 的内容、
// 额外的 正文 字段以及如何表述 HTTP 错误。
// 循环本身 - SSE 框架、错误-body 封顶、重试-after 捕获、
// 看门狗 超时 和共享尾声 - 对于每个响应后端都是相同的，
// 这就是该模块存在的全部原因。
namespace {
provider::StreamResult stream_attempt(const Site& site, const Target& tgt,
                                      const provider::Request& req,
                                      const provider::EventSink& sink,
                                      bool may_retry_401, bool& rejected);
}

provider::StreamResult stream(const maya::unlocked& cap, const Site& site,
                              provider::Request req,
                              provider::EventSink sink) {
    sink(StreamStarted{});
    // Host解析凭据+目的地(可能阻止令牌刷新，
    // 并且可能重写req.model)。真实的散文是主持人的：这
    // 一层永远不会发明它无法确定的补救文本。
    auto target = site.authorize(cap, req);
    if (!target) {
        sink(StreamError{target.error()});
        return provider::StreamResult::failed(std::string{site.id} + ": " + target.error());
    }
    // 每回合一次强制刷新：401意味着令牌被拒绝，即使它看起来
    // 有效（提前撤销、时钟偏差）。 codex-rs 在放弃之前
    // 也做了同样的事情。
    for (bool reauthorized = false;; reauthorized = true) {
        bool rejected = false;
        auto r = stream_attempt(site, *target, req, sink,
                                /*may_retry_401=*/!reauthorized && site.reauthorize != nullptr,
                                rejected);
        if (!rejected) return r;
        AGT_LOG(Wire, Warn, "responses.unauthorized", "site={} refreshing and retrying", site.id);
        auto again = site.reauthorize(cap, req, *target);
        if (!again) {
            sink(StreamError{again.error(), std::nullopt});
            return provider::StreamResult::failed(std::string{site.id} + ": " + again.error());
        }
        target = std::move(again);
    }
}

provider::StreamResult stream(const Site& site,
                              std::expected<Target, std::string> target,
                              provider::Request req,
                              provider::EventSink sink) {
    sink(StreamStarted{});
    if (!target) {
        sink(StreamError{target.error()});
        return provider::StreamResult::failed(std::string{site.id} + ": " + target.error());
    }
    bool rejected = false;
    return stream_attempt(site, *target, req, sink, /*may_retry_401=*/false, rejected);
}

namespace {
// 一个POST+数据流。对于 `may_retry_401`，任何 事件
// 之前的 401 返回，设置了 `rejected` 并且没有发出任何内容，
// 因此调用者可以刷新并重新发送；否则，回合将通过共享尾声结束。
provider::StreamResult stream_attempt(const Site& site, const Target& tgt,
                                      const provider::Request& req,
                                      const provider::EventSink& sink,
                                      bool may_retry_401, bool& rejected) {
    const Target* target = &tgt;

    http::Request hr;
    hr.method  = http::HttpMethod::Post;
    hr.host    = target->host;
    hr.port    = target->port;
    hr.path    = target->path;
    hr.plaintext = target->plaintext;
    hr.headers = target->headers;
    // SSE反-buffering三人组每agentty中的流式传输 传输层发送。
    http::append_sse_no_buffer(hr.headers);

    try {
        json body = build_body(req, site.id);
        // Host-specific场层位于中性正文之上。
        if (site.decorate_body) site.decorate_body(body, req);
        // 主机的型号选择具有权威性（参见build_body)。
        if (!target->model.empty()) body["model"] = target->model;
        hr.body = body.dump();
    } catch (const std::exception& e) {
        sink(StreamError{std::string{"could not encode request: "} + e.what()});
        return provider::StreamResult::failed("could not encode request");
    }

    // 请求元数据+完整的正文，与聊天传输层的`openai.request`
    // /`openai.request.body`对完全匹配。如果没有这些，
    // 响应方言就是您无法从日志中调试的 传输层：带有空工具参数返回的
    // Copilot 转弯会为您提供解码后的 事件，但绝不是我们 ASKED
    // 的内容，因此 "did we send the tools?" 和
    // "did the server drop them?" 看起来相同。
    // 正文 被截断为 不 — 被拒绝的请求恰好与最后一个 字节
    // 相关。
    //
    // 工具计数在调试行上而不是仅在跟踪正文中，
    // 因为"how many tools did this turn advertise"
    // 是模型停止调用它们时的第一个问题，并且不应该花费完整的
    // -body转储来回答。
    std::size_t tool_count = 0;
    try {
        const auto parsed = json::parse(hr.body);
        if (const auto t = parsed.find("tools");
            t != parsed.end() && t->is_array())
            tool_count = t->size();
    } catch (...) { /* 仅诊断 — 永远不会失败地记录它 */ }

    AGT_LOG(Wire, Debug, "responses.request",
            "POST https://{}:{}{} site={} model={} tools={} reasoning={} bytes={}",
            target->host, static_cast<unsigned>(target->port), target->path,
            site.id, target->model.empty() ? req.model : target->model,
            tool_count, req.show_reasoning ? 1 : 0, hr.body.size());
    AGT_LOG(Wire, Trace, "responses.request.body", "bytes={} raw={}", hr.body.size(), ::agentty::logx::body(hr.body));

    StreamCtx ctx;
    ctx.sink = sink;
    ctx.show_reasoning = req.show_reasoning;
    ctx.site = std::string{site.id};
    // 共享支架：status/Retry-After捕获、心跳、缓冲-wait、
    // 传输转储、上限错误正文。一旦 dispatch() 触发 终端 事件（
    // `response.completed` 后故意中止 - 尾声中理解为
    // AlreadyTerminated 的延迟胜利），我们的提要就会停止读取。
    provider::StreamScaffold sc;
    sc.dialect = "openai-responses";
    sc.sink    = sink;
    sc.feed    = [&](std::string_view chunk) {
        ctx.sse.feed(chunk.data(), chunk.size(),
            [&](std::string_view, std::string_view payload, char*) {
                dispatch(ctx, payload);
            });
        return !ctx.terminated;
    };

    http::Timeouts tos = target->plaintext ? provider::local_stream_timeouts()
                                           : provider::stream_timeouts();

    auto result = http::default_client(::agentty::IoAccess::grant()).stream(hr, sc.handler(), tos, req.cancel);

    // 已拒绝 凭据，尚未传输任何内容：让呼叫者刷新。
    if (may_retry_401 && sc.http_status == 401 && !ctx.terminated) {
        rejected = true;
        return provider::StreamResult::failed("401");
    }

    // 统一end-of-turn对(调试摘要+警告原始错误正文)，
    // 并附加方言-specific计数器。
    sc.log_result(bool(result),
                  result ? std::string_view{} : result.error().render(),
                  std::format("terminated={} thinking_deltas={}",
                              ctx.terminated ? 1 : 0, ctx.thinking_deltas));

    // End通过SHARED尾声，
    // 因此每个响应主机完成与Anthropic/OpenAI/Ollama.相同的关键情况是AlreadyTerminated:
    // 当`response.completed` 帧在dispatch()内发射
    // StreamFinished时，on_chunk返回假以停止读取(故意的延迟胜利)，
    // HTTP层将其报告为中止/ "cancelled" 转账。
    // finish_stream 将其视为 EXPECTED（不发出任何信号），
    // 避免了过去在干净转弯后显示的虚假 StreamError{"cancelled"}。
    return provider::finish_stream({
        .terminated  = ctx.terminated,
        .sink        = sink,
        .result_ok   = bool(result),
        .http_status = sc.http_status,
        .non_replayable = !result && result.error().non_replayable,
        .cancel      = req.cancel,
        .stop        = ctx.stop,
        .http_error_message = [&]() -> std::string {
            return site.explain_http_error(sc.http_status, sc.error_body);
        },
        .retry_after = sc.retry_after(),
        .transport_error_message = [&]() -> std::string {
            return result.error().render();
        },
        // 响应总是以response.completed/不完整/失败结束。
        .expects_terminal_event = true,
    });
}
} // 命名空间结束。

// ── 编码访问器(由主机和测试共享) ───────────────────────────
std::vector<Msg> parse_sse_for_test(const std::vector<std::string>& sse_data_lines,
                                    bool show_reasoning) {
    std::vector<Msg> out;
    StreamCtx ctx;
    ctx.show_reasoning = show_reasoning;
    ctx.sink = [&](Msg m) { out.push_back(std::move(m)); };
    for (const auto& line : sse_data_lines) dispatch(ctx, line);
    return out;
}

} // namespace agentty::provider::responses
