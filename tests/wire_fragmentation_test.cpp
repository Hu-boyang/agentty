// wire_fragmentation_test ——工具 -argument CONFORMANCE MATRIX.
//
// ── 为什么这个测试存在 ─────────────────────────────────────────────────
//
// A 流式传输 协议可能提供工具 调用 辩论作为递增的片段,
// 作为权威 快照 ,或两者兼有。每个手臂都是光谱 -legal ,
// 而你得到的是一个财产 SERVER ,而不是协议: Codex
// 数据流 碎片; GitHub 副驾驶代理机组联合起来,
// 只发送 `.done` 快照.
//
// 预告 -existing 编码测试( E) codex_responses_test
// 编码 一个 服务器的选择——三角洲——仿佛协议一样。它通过时,“副驾驶”完全无法使用:
// 每个工具 调用 发送日期 `{}` ,所以,我说 `grep` 失败
// "pattern required" 财务报告和已审计财务报表 `read` 失败
// "path required" 零时 -argument 工具( 工具) repo_map ,
// 列表,测试)似乎有用。不对称使得它看起来像一个片状模型而不是一个掉下来的模型。 SSE 事件.
//
// 因此,该文件没有测试记录。它列举了法律
// 破碎空间,并声称必须横跨所有地区的ONE财产。
// of it:
//
//     然而服务器选择了 帧 参数,
//     解码参数是相同的。
//
// 仅处理一些臂的解码器在此通过构建失败。
// 添加一个新的框架 提供者将使用 = 添加一个计数器。

#include <map>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "agtest.hpp"

#include "agentty/provider/chatgpt/responses.hpp"
#include "agentty/provider/wire/streamed.hpp"

using namespace agentty;
namespace cc = agentty::provider::chatgpt;
using json = nlohmann::json;

template <class Leaf>
static const Leaf* leaf(const Msg& m) {
    const Leaf* found = nullptr;
    std::visit([&](const auto& domain) {
        std::visit([&](const auto& l) {
            if constexpr (std::is_same_v<std::decay_t<decltype(l)>, Leaf>)
                found = &l;
        }, domain);
    }, m);
    return found;
}

// ── 法律框架空间 ──────────────────────────────────────────────
//
// 每个样式都是符合要求的服务器可能采用的一种方式 帧 一个工具 调用 ' 参数。
// 无更多 "correct" 而不是另一个
enum class Framing {
    DeltasOnly,      // Codex。碎片,不 终端 快照.
    DoneOnly,        // GitHub 副驾驶 手持: 快照 是那个 仅
                     // 载体 这是被悄悄抛下的手臂
    DeltasAndDone,   // 两者均不应重复第快照号。
    InlineOnAdded,   // 整块起动 -front 打开 output_item.added.
    ItemDoneOnly,    // 只有已完成的项目重复了这些内容。

    // 电话 CUSTOM 工具 -call 协议。与上面两个形状相同,但项目类型不同(
    // `custom_tool_call` ),不同 事件 名称( 4)
    // `response.custom_tool_call_input.*` 和 和 数据 载荷 下级 `input`
    // 而不是说 `arguments`.
    //
    // 使用自定义工具调用的模型在此传递其参数ONLY。
    // 我们掉了这些 事件 完全直到 GitHub 发现自己的客户
    // 端为 反序列化 ——同声无息. `{}` 分派 说
    // DoneOnly 由副驾驶制造, 由同一原因: 载体无人列出。
    // 把它们放进矩阵中就是阻止第三次重复的。
    CustomDeltasOnly,
    CustomDoneOnly,
};

static std::string name_of(Framing f) {
    switch (f) {
        case Framing::DeltasOnly:       return "DeltasOnly";
        case Framing::DoneOnly:         return "DoneOnly (Copilot)";
        case Framing::DeltasAndDone:    return "DeltasAndDone";
        case Framing::InlineOnAdded:    return "InlineOnAdded";
        case Framing::ItemDoneOnly:     return "ItemDoneOnly";
        case Framing::CustomDeltasOnly: return "CustomDeltasOnly";
        case Framing::CustomDoneOnly:   return "CustomDoneOnly";
    }
    return "?";
}

// 这是在自定义-tool协议而不是函数之一上设定的吗?
static bool is_custom(Framing f) {
    return f == Framing::CustomDeltasOnly || f == Framing::CustomDoneOnly;
}

// 构建一个光谱 -legal 答复 SSE 脚本发送 `args` 下级 `style`.
static std::vector<std::string> synth(Framing style, const std::string& args) {
    const std::string esc = json(args).dump();  // args 以 JSON 字符串字元表示
    std::vector<std::string> sse;

    // 习惯 -tool 协议中所有三种拼写方式不同。
    // 将拼写保留在一个位置,这样下面的脚本读作: 一个
    // 形状有两组词组,这就是它是什么。
    const char* item_type = is_custom(style) ? "custom_tool_call" : "function_call";
    const char* arg_key   = is_custom(style) ? "input" : "arguments";

    std::string added =
        std::string{R"({"type":"response.output_item.added","item":{"type":")"}
        + item_type + R"(","id":"fc_1","call_id":"call_9","name":"grep")";
    if (style == Framing::InlineOnAdded)
        added += std::string{",\""} + arg_key + "\":" + esc;
    added += "}}";
    sse.push_back(added);

    const auto delta = [&](const std::string& chunk) {
        const char* ev = is_custom(style)
                       ? "response.custom_tool_call_input.delta"
                       : "response.function_call_arguments.delta";
        sse.push_back(std::string{R"({"type":")"} + ev
                      + R"(","item_id":"fc_1","delta":)"
                      + json(chunk).dump() + "}");
    };

    switch (style) {
        case Framing::DeltasOnly:
        case Framing::DeltasAndDone:
        case Framing::CustomDeltasOnly: {
            const auto mid = args.size() / 2;
            delta(args.substr(0, mid));
            delta(args.substr(mid));
            break;
        }
        case Framing::DoneOnly:
        case Framing::InlineOnAdded:
        case Framing::ItemDoneOnly:
        case Framing::CustomDoneOnly:
            break;  // 完全没有碎片
    }

    // 电话 终端 快照。每个发送的服务器发送 `.done`.
    if (style == Framing::DoneOnly || style == Framing::DeltasAndDone) {
        sse.push_back(
            R"({"type":"response.function_call_arguments.done","item_id":"fc_1",)"
            R"("arguments":)" + esc + "}");
    }
    if (style == Framing::CustomDoneOnly) {
        sse.push_back(
            R"({"type":"response.custom_tool_call_input.done","item_id":"fc_1",)"
            R"("input":)" + esc + "}");
    }

    std::string done =
        std::string{R"({"type":"response.output_item.done","item":{"type":")"}
        + item_type + R"(","id":"fc_1")";
    if (style == Framing::ItemDoneOnly)
        done += std::string{",\""} + arg_key + "\":" + esc;
    done += "}}";
    sse.push_back(done);
    sse.push_back(R"({"type":"response.completed","response":{"usage":{}}})");
    return sse;
}

// 重新组装什么 状态更新函数 将看到: 缩合的三角洲。
static std::string decode_args(const std::vector<std::string>& sse) {
    std::string out;
    for (const auto& m : cc::parse_sse_for_test(sse))
        if (auto* d = leaf<StreamToolUseDelta>(m)) out += d->partial_json;
    return out;
}

TEST_CASE("tool arguments survive every legal SSE framing") {
    // 现实的载荷:巢形物体,逃出引号,斜线.
    const std::string args =
        R"({"pattern":"foo\"bar","glob":"src/**/*.cpp","opts":{"word":true}})";

    for (auto style : {Framing::DeltasOnly, Framing::DoneOnly,
                       Framing::DeltasAndDone, Framing::InlineOnAdded,
                       Framing::ItemDoneOnly,
                       Framing::CustomDeltasOnly, Framing::CustomDoneOnly}) {
        INFO("framing = " << name_of(style));
        const auto decoded = decode_args(synth(style, args));

        // 属性:框架对解码结果是隐形的.
        CHECK(decoded == args);

        // 一定是这样 PARSEABLE - 半个 -merged 或双倍 -appended
        // 数据流 两者均不能等同,因此,这一方法也捕获了重复的排放。
        CHECK(json::parse(decoded)["pattern"] == "foo\"bar");
        CHECK(json::parse(decoded)["opts"]["word"] == true);
    }
}

TEST_CASE("a tool call emits exactly one end regardless of framing") {
    const std::string args = R"({"pattern":"x"})";
    for (auto style : {Framing::DeltasOnly, Framing::DoneOnly,
                       Framing::DeltasAndDone, Framing::InlineOnAdded,
                       Framing::CustomDeltasOnly, Framing::CustomDoneOnly}) {
        INFO("framing = " << name_of(style));
        std::map<std::string, int> ends;
        for (const auto& m : cc::parse_sse_for_test(synth(style, args)))
            if (auto* e = leaf<StreamToolUseEnd>(m)) ++ends[e->id.value];
        CHECK(ends["call_9"] == 1);
    }
}

// ── unseen() 的代数 ───────────────────────────────────────────────────

TEST_CASE("unseen: a snapshot after fragments adds nothing") {
    std::string have;
    CHECK(provider::wire::unseen(have, "{\"a\":", false) == "{\"a\":");
    CHECK(provider::wire::unseen(have, "1}", false) == "1}");
    CHECK(provider::wire::unseen(have, "{\"a\":1}", true).empty());
    CHECK(have == "{\"a\":1}");
}

TEST_CASE("unseen: a snapshot with no fragments yields all of it, once") {
    std::string have;
    CHECK(provider::wire::unseen(have, "{\"a\":1}", true) == "{\"a\":1}");
    CHECK(provider::wire::unseen(have, "{\"a\":1}", true).empty());
}

TEST_CASE("unseen: a snapshot extending fragments yields only the tail") {
    std::string have;
    CHECK(provider::wire::unseen(have, "{\"a\":", false) == "{\"a\":");
    CHECK(provider::wire::unseen(have, "{\"a\":1}", true) == "1}");
    CHECK(have == "{\"a\":1}");
}

TEST_CASE("unseen: a shorter snapshot never retracts emitted bytes") {
    std::string have;
    CHECK(provider::wire::unseen(have, "{\"a\":1}", false) == "{\"a\":1}");
    CHECK(provider::wire::unseen(have, "{\"a\":", true).empty());
    CHECK(have == "{\"a\":1}");
}

// ── 聊天-completions方言 ─────────────────────────────────────────
//
// 同样的双重性存在于/ chat/completions:
// `delta.tool_calls[].function .arguments`
// 通常携带 FRAGMENTS ,但连接的代理可以重复 COMPLETE 参数连续块。
// 盲动会变成 `{"a":1}{"a":1}` ——无法解析,失败的状态是:
// "invalid args" 和反应错误一样
#include "agentty/provider/openai/transport.hpp"

static std::string decode_chat_args(const std::vector<std::string>& chunks) {
    std::string bytes;
    for (const auto& c : chunks) bytes += "data: " + c + "\n\n";
    bytes += "data: [DONE]\n\n";
    std::string out;
    for (const auto& m : provider::openai::parse_sse_for_test(bytes, {"grep"}))
        if (auto* d = leaf<StreamToolUseDelta>(m)) out += d->partial_json;
    return out;
}

TEST_CASE("chat: coalesced tool arguments are not concatenated") {
    const std::string args = R"({"pattern":"x"})";
    const std::string esc  = json(args).dump();
    // 重复每个块中全部参数的代理。
    std::vector<std::string> sse = {
        R"({"choices":[{"delta":{"tool_calls":[{"index":0,"id":"call_1",)"
        R"("function":{"name":"grep","arguments":)" + esc + "}}]}}]}",
        R"({"choices":[{"delta":{"tool_calls":[{"index":0,)"
        R"("function":{"arguments":)" + esc + "}}]}}]}",
        R"({"choices":[{"delta":{},"finish_reason":"tool_calls"}]})",
    };
    CHECK(decode_chat_args(sse) == args);
}

TEST_CASE("chat: genuine fragments still assemble") {
    std::vector<std::string> sse = {
        R"({"choices":[{"delta":{"tool_calls":[{"index":0,"id":"call_1",)"
        R"("function":{"name":"grep","arguments":"{\"pattern\":"}}]}}]})",
        R"({"choices":[{"delta":{"tool_calls":[{"index":0,)"
        R"("function":{"arguments":"\"x\"}"}}]}}]})",
        R"({"choices":[{"delta":{},"finish_reason":"tool_calls"}]})",
    };
    CHECK(decode_chat_args(sse) == R"({"pattern":"x"})");
}
