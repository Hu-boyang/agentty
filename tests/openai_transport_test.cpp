// openai_transport_test - 核查 OpenAI-compatible
// 传输层 纯翻译层, 没有任何网络 :
//
//   1. build_tools     — provider::ToolSpec → OpenAI 函数 模式定义.
//   2. build_messages  — 线程 → OpenAI 消息阵列( 助手) tool_calls
//                        + 单独的角色:"tool"结果+多式联运图像).
//   3. parse_sse_for_test —已编稿 OpenAI SSE 帧 – 已经 agentty Msg
//                        顺序 状态更新函数 消耗(文本三角洲,流化工具) -call 组装,
//                        finish_reason→StopReason ,使用,
//                        [ DONE] 终端).
//
// 运行: 构建`openai_transport_test`目标, 执行。 退出 0 = 通过。

#include <cstdio>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "agtest.hpp"

#include "agentty/provider/openai/transport.hpp"
#include "agentty/io/http.hpp"
#include "agentty/provider/msg_shared.hpp"
#include "agentty/provider/registry.hpp"
#include "agentty/provider/selection.hpp"
#include <cstdlib>

using namespace agentty;
namespace oai = agentty::provider::openai;

// ── Msg 检查助手 ──────────────────────────────────────────────────
// Msg 是一个 变体 域子的词 -variants 叶子要更深一层
// get_leaf<T> 挖出来, 返回 nullptr 如果该 Msg 叶子不是么.
template <class Leaf>
const Leaf* get_leaf(const Msg& m) {
    const Leaf* found = nullptr;
    std::visit([&](const auto& domain) {
        std::visit([&](const auto& leaf) {
            if constexpr (std::is_same_v<std::decay_t<decltype(leaf)>, Leaf>)
                found = &leaf;
        }, domain);
    }, m);
    return found;
}

template <class Leaf>
int count_leaf(const std::vector<Msg>& msgs) {
    int n = 0;
    for (const auto& m : msgs) if (get_leaf<Leaf>(m)) ++n;
    return n;
}

// 将全部调和 StreamTextDelta 载荷 依序排列.
static std::string joined_text(const std::vector<Msg>& msgs) {
    std::string s;
    for (const auto& m : msgs)
        if (const auto* d = get_leaf<StreamTextDelta>(m)) s += d->text;
    return s;
}

// 将全部调和 StreamToolUseDelta 载荷 依序排列.
static std::string joined_tool_args(const std::vector<Msg>& msgs) {
    std::string s;
    for (const auto& m : msgs)
        if (const auto* d = get_leaf<StreamToolUseDelta>(m)) s += d->partial_json;
    return s;
}

// ── 测试 ───────────────────────────────────────────────────────────────────

TEST_CASE("sse anti-buffering headers are the shared SSOT trio") {
    // 三个指令将网关从buffering/compressing
    // 一条事件 数据流. 固定这样未来的编辑不能默默地掉下去,
    // 也不能为单条传输 改变一条——每条流式传输 传输层 路线通过这个帮助者.
    auto trio = agentty::http::sse_no_buffer_headers();
    CHECK(trio.size() == 3);
    auto has = [&](const char* n, const char* v) {
        for (const auto& h : trio) if (h.name == n) return h.value == v;
        return false;
    };
    CHECK(has("cache-control", "no-cache, no-transform"));
    CHECK(has("pragma", "no-cache"));
    CHECK(has("accept-encoding", "identity"));

    // append_sse_no_buffer 将同一三重奏组合到现有的列表中。
    agentty::http::Headers h;
    h.push_back({"content-type", "application/json"});
    agentty::http::append_sse_no_buffer(h);
    CHECK(h.size() == 4);
}

TEST_CASE("test_build_tools") {
    std::vector<provider::ToolSpec> tools;
    tools.push_back({"read", "Read a file",
                     nlohmann::json{{"type", "object"},
                                    {"properties", {{"path", {{"type", "string"}}}}}},
                     false});
    auto j = agentty::provider::wire::openai_chat_tools(tools);
    CHECK(j.is_array());
    CHECK(j.size() == 1);
    CHECK(j[0]["type"] == "function");
    CHECK(j[0]["function"]["name"] == "read");
    CHECK(j[0]["function"]["description"] == "Read a file");
    CHECK(j[0]["function"]["parameters"]["type"] == "object");

    // 零数 模式定义 被守护到有效的空 -object 模式定义 (SSOT
    // 与您共享的守护 Ollama + 响应编码器)——在赤空上严格后端400.
    std::vector<provider::ToolSpec> noschema;
    noschema.push_back({"ping", "no args", nlohmann::json(nullptr), false});
    auto n = agentty::provider::wire::openai_chat_tools(noschema);
    CHECK(n[0]["function"]["parameters"]["type"] == "object");
    CHECK(n[0]["function"]["parameters"]["properties"].is_object());
}

TEST_CASE("test_build_messages_basic") {
    Thread t;
    Message u;
    u.role = Role::User;
    u.text = "hello";
    t.messages.push_back(u);

    auto arr = oai::build_messages(t);
    CHECK(arr.is_array());
    CHECK(arr.size() == 1);
    CHECK(arr[0]["role"] == "user");
    CHECK(arr[0]["content"] == "hello");
}

TEST_CASE("test_build_messages_tool_roundtrip") {
    // 带有已完成工具的助理信息 调用 + 一个助理留言 tool_calls
    // + 一个单独的角色: "tool" 结果信息。
    Thread t;

    Message a;
    a.role = Role::Assistant;
    a.text = "let me check";
    ToolUse tc;
    tc.id   = ToolCallId{"call_abc"};
    tc.name = ToolName{"read"};
    tc.args = nlohmann::json{{"path", "foo.txt"}};
    tc.status = ToolUse::Done{{}, {}, "file contents here"};
    a.tool_calls.push_back(tc);
    t.messages.push_back(a);

    auto arr = oai::build_messages(t);
    CHECK(arr.size() == 2);

    // [0] 助手与tool_calls.
    CHECK(arr[0]["role"] == "assistant");
    CHECK(arr[0]["content"] == "let me check");
    CHECK(arr[0]["tool_calls"].is_array());
    CHECK(arr[0]["tool_calls"][0]["id"] == "call_abc");
    CHECK(arr[0]["tool_calls"][0]["type"] == "function");
    CHECK(arr[0]["tool_calls"][0]["function"]["name"] == "read");
    // 参数必须是 STRING (按顺序排列) JSON).
    CHECK(arr[0]["tool_calls"][0]["function"]["arguments"].is_string());
    CHECK(arr[0]["tool_calls"][0]["function"]["arguments"]
          == std::string{"{\"path\":\"foo.txt\"}"});

    // [1] 工具结果.
    CHECK(arr[1]["role"] == "tool");
    CHECK(arr[1]["tool_call_id"] == "call_abc");
    CHECK(arr[1]["content"] == "file contents here");
}

// 年龄 -tiered 工具 -result 清除( 分摊)
// wire::cap_tool_result_aged :在一个长的工具上 -burst 线程
// 最新成果保持了全额预算, 过期 都淡化成头部紧凑+ 末尾 所以一个大垃圾场停止了每转弯重播。
static std::string oai_tool_content(const nlohmann::json& arr, const char* id) {
    for (const auto& msg : arr)
        if (msg.value("role", "") == "tool"
            && msg.value("tool_call_id", "") == id)
            return msg.value("content", "");
    return {};
}

TEST_CASE("test_build_messages_age_tiering") {
    std::string big = "HEAD_SENTINEL_AAAA\n";
    big.append(60 * 1024, 'x');
    big += "\nTAIL_SENTINEL_ZZZZ";

    Thread t;
    Message u; u.role = Role::User; u.text = "start"; t.messages.push_back(u);
    const int n = 20;
    for (int i = 0; i < n; ++i) {
        Message a; a.role = Role::Assistant; a.text = "c";
        ToolUse tc;
        tc.id   = ToolCallId{"call_" + std::to_string(i)};
        tc.name = ToolName{"grep"};
        tc.status = ToolUse::Done{{}, {}, big};
        a.tool_calls.push_back(std::move(tc));
        t.messages.push_back(std::move(a));
    }
    auto arr = oai::build_messages(t);

    // 最新( 4) call_19 ,0级)保持满;最老( call_0 ,19级),已淡出.
    std::string newest = oai_tool_content(arr, "call_19");
    std::string oldest = oai_tool_content(arr, "call_0");
    CHECK(newest.size() > 40 * 1024);
    CHECK(oldest.size() < 6 * 1024);
    CHECK(oldest.find("bytes elided") != std::string::npos);
    // 错误的结果仍然保持头+ 末尾 (回顾,不是重播).
    CHECK(oldest.find("HEAD_SENTINEL_AAAA") != std::string::npos);
    CHECK(oldest.find("TAIL_SENTINEL_ZZZZ") != std::string::npos);

    // 一部短篇的旧结果逐字逐句(无所淡出).
    Thread t2;
    Message u2; u2.role = Role::User; u2.text = "start"; t2.messages.push_back(u2);
    for (int i = 0; i < n; ++i) {
        Message a; a.role = Role::Assistant; a.text = "c";
        ToolUse tc;
        tc.id   = ToolCallId{"call_" + std::to_string(i)};
        tc.name = ToolName{"grep"};
        tc.status = ToolUse::Done{{}, {}, "3 matches\n"};
        a.tool_calls.push_back(std::move(tc));
        t2.messages.push_back(std::move(a));
    }
    auto arr2 = oai::build_messages(t2);
    CHECK(oai_tool_content(arr2, "call_0") == "3 matches\n");
}

TEST_CASE("test_sse_text_stream") {
    // 纯文本完成:两条内容deltas,finish_reason stop, [DONE].
    std::string sse =
        "data: {\"choices\":[{\"delta\":{\"content\":\"Hel\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"lo!\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
        "data: {\"choices\":[],\"usage\":{\"prompt_tokens\":12,\"completion_tokens\":3}}\n\n"
        "data: [DONE]\n\n";
    auto msgs = oai::parse_sse_for_test(sse);

    CHECK(joined_text(msgs) == "Hello!");
    CHECK(count_leaf<StreamFinished>(msgs) == 1);

    // 使用量浮出水面.
    bool saw_usage = false;
    for (const auto& m : msgs)
        if (const auto* u = get_leaf<StreamUsage>(m)) {
            CHECK(u->input_tokens == 12);
            CHECK(u->output_tokens == 3);
            saw_usage = true;
        }
    CHECK(saw_usage);

    // finish_reason "stop" → EndTurn.
    for (const auto& m : msgs)
        if (const auto* f = get_leaf<StreamFinished>(m))
            CHECK(f->stop_reason == StopReason::EndTurn);
}

TEST_CASE("test_sse_reasoning_stream") {
    // 理由说明 -capable 聊天模式 传输 数据流 链条 -of-thought 在一个
    // 字段 PARALLEL 改为内容 : DeepSeek 使用 reasoning_content ;
    // 一些代理人使用推理。两者必须表面为 StreamThinkingDelta (无)
    // 相同 事件 编号 Anthropic 传输 和 发 出 绝不 漏入可见文本 正文.
    std::string sse =
        "data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"Let me \"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"think.\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"Answer: 42\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
        "data: [DONE]\n\n";
    auto msgs = oai::parse_sse_for_test(sse);

    // 思考文本,顺序通过StreamThinkingDelta.
    std::string think;
    for (const auto& m : msgs)
        if (const auto* t = get_leaf<StreamThinkingDelta>(m)) think += t->text;
    CHECK(think == "Let me think.");

    // 可见文本是ONLY的内容,而不是推理.
    CHECK(joined_text(msgs) == "Answer: 42");
    CHECK(count_leaf<StreamFinished>(msgs) == 1);
}

TEST_CASE("test_sse_reasoning_alt_field") {
    // 候补 `reasoning` 字段名称( 部分) OpenRouter
    // 通行证)处理相同.
    std::string sse =
        "data: {\"choices\":[{\"delta\":{\"reasoning\":\"hmm\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"ok\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
        "data: [DONE]\n\n";
    auto msgs = oai::parse_sse_for_test(sse);
    std::string think;
    for (const auto& m : msgs)
        if (const auto* t = get_leaf<StreamThinkingDelta>(m)) think += t->text;
    CHECK(think == "hmm");
    CHECK(joined_text(msgs) == "ok");
}

// #117. 每个三角洲都说出它推理的所在领域, 下一个请求会把推理
// 的转折 回到同一个领域,方法 GitHub 客户端有(每
// -message reasoning_wire_field).
TEST_CASE("test_sse_reasoning_records_its_field") {
    auto field_of = [](const std::string& sse) {
        std::string f;
        for (const auto& m : oai::parse_sse_for_test(sse))
            if (const auto* t = get_leaf<StreamThinkingDelta>(m)) f = t->wire_field;
        return f;
    };
    CHECK(field_of("data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"a\"}}]}\n\n"
                   "data: [DONE]\n\n") == "reasoning_content");
    CHECK(field_of("data: {\"choices\":[{\"delta\":{\"reasoning\":\"a\"}}]}\n\n"
                   "data: [DONE]\n\n") == "reasoning");
    // 空的 空的 reasoning_content 再加上一个繁杂的推理:一个有文字的推理.
    CHECK(field_of("data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"\",\"reasoning\":\"a\"}}]}\n\n"
                   "data: [DONE]\n\n") == "reasoning");
}

TEST_CASE("test_build_messages_replays_reasoning_under_its_field") {
    Thread t{ThreadId{"r"}, "", {}, {}, {}};
    Message u; u.role = Role::User; u.text = "q";
    Message a; a.role = Role::Assistant; a.text = "A";
    a.thinking = "thought"; a.reasoning_wire_field = "reasoning_content";
    Message b; b.role = Role::Assistant; b.text = "B";
    b.thinking = "other";   b.reasoning_wire_field = "reasoning";
    Message c; c.role = Role::Assistant; c.text = "C";   // 其它模式: 无
    t.messages = {u, a, u, b, u, c, u};

    auto arr = oai::build_messages(t);
    std::vector<nlohmann::json> asst;
    for (const auto& m : arr) if (m["role"] == "assistant") asst.push_back(m);
    REQUIRE(asst.size() == 3);
    CHECK(asst[0]["reasoning_content"] == "thought");
    CHECK(!asst[0].contains("reasoning"));
    CHECK(asst[1]["reasoning"] == "other");
    CHECK(!asst[1].contains("reasoning_content"));
    CHECK(!asst[2].contains("reasoning_content"));
    CHECK(!asst[2].contains("reasoning"));
    for (const auto& m : arr)
        if (m["role"] == "user") CHECK(!m.contains("reasoning_content"));
}

// #117,案件298b3359未了:需要推理的模型 每个 先前的
// 助手转弯( Beam), 历史上又写了另一个模型。
// 默认值会保持以上状态; 具有学习范围, 每个转弯没有
// reasoning_content 买一个
TEST_CASE("test_build_messages_fills_required_reasoning") {
    Thread t{ThreadId{"r"}, "", {}, {}, {}};
    Message u; u.role = Role::User; u.text = "q";
    Message own; own.role = Role::Assistant; own.text = "A";
    own.thinking = "mine"; own.reasoning_wire_field = "reasoning_content";
    Message lax; lax.role = Role::Assistant; lax.text = "B";   // 另一种模式,没有推理
    Message foreign; foreign.role = Role::Assistant; foreign.text = "C";
    foreign.thinking = "theirs";   // 说得有道理,但是在 传输 无字段( 内置) <think>, Anthropic)
    Message sent_as; sent_as.role = Role::Assistant; sent_as.text = "D";
    sent_as.thinking = "as-reasoning"; sent_as.reasoning_wire_field = "reasoning";
    Message tool; tool.role = Role::Assistant; tool.text = "E";
    ToolUse tc;
    tc.id = ToolCallId{"call_1"}; tc.name = ToolName{"read"};
    tc.args = nlohmann::json{{"path", "f"}};
    tc.status = ToolUse::Done{{}, {}, "body"};
    tool.tool_calls.push_back(tc);
    t.messages = {u, own, u, lax, u, foreign, u, sent_as, u, tool, u};

    auto assistants = [&](std::uint8_t echo) {
        std::vector<nlohmann::json> out;
        for (const auto& m : oai::build_messages(t, echo))
            if (m["role"] == "assistant") out.push_back(m);
        return out;
    };

    // 所有(2):每个助理转弯都带一个非 -empty reasoning_content.
    auto all = assistants(2);
    REQUIRE(all.size() == 5);
    CHECK(all[0]["reasoning_content"] == "mine");          // 自己的,没有被污染的
    CHECK(all[1]["reasoning_content"] == " ");             // 无要发送的: 占位符
    CHECK(all[2]["reasoning_content"] == "theirs");        // 另一种模式的思维,再利用
    CHECK(all[3]["reasoning"] == "as-reasoning");          // 其自己的字段仍然发送
    CHECK(all[3]["reasoning_content"] == "as-reasoning");  // 而这个模型想要的
    CHECK(all[4]["reasoning_content"] == " ");
    for (const auto& a : all) {
        REQUIRE(a["reasoning_content"].is_string());
        CHECK(!a["reasoning_content"].get<std::string>().empty());
    }

    // ToolCalls (1):只有有工具的转弯 调用 已经填满。
    auto tools = assistants(1);
    REQUIRE(tools.size() == 5);
    CHECK(tools[0]["reasoning_content"] == "mine");
    CHECK(!tools[1].contains("reasoning_content"));
    CHECK(!tools[2].contains("reasoning_content"));
    CHECK(!tools[3].contains("reasoning_content"));
    CHECK(tools[4]["reasoning_content"] == " ");

    // 无( 0) 完全就是普通超载: 没有请求更改从未
    // 要求过的模型。
    CHECK(oai::build_messages(t, 0) == oai::build_messages(t));
    for (const auto& m : oai::build_messages(t))
        if (m["role"] == "user") CHECK(!m.contains("reasoning_content"));
}

TEST_CASE("test_sse_structured_thinking_parts") {
    // 迷信推理模型(mistral-small 4 / -medium 3.5 与
    // reasoning_effort) 数据流 链式-of-thought 作为 STRUCTURED 内容-parts
    // ARRAY——被探测的直播:
    //   {"content":[{"type":
    //                "thinking", "thinking":[{
    // "type":"text","text":"…" ...最后的答案是 简单的内容
    // STRING。思维部分必须浮出水面 StreamThinkingDelta (不丢弃,
    // 不漏入流言);文本 -type 数组中的部件必须 渲染 作是说.
    std::string sse =
        "data: {\"choices\":[{\"delta\":{\"role\":\"assistant\",\"content\":\"\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":[{\"type\":\"thinking\","
        "\"thinking\":[{\"type\":\"text\",\"text\":\"Okay, let\"}],\"closed\":true}]}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":[{\"type\":\"thinking\","
        "\"thinking\":[{\"type\":\"text\",\"text\":\"'s see.\"}]}]}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":[{\"type\":\"text\","
        "\"text\":\"Part-array prose. \"}]}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"Four\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
        "data: [DONE]\n\n";
    auto msgs = oai::parse_sse_for_test(sse);

    std::string think;
    for (const auto& m : msgs)
        if (const auto* t = get_leaf<StreamThinkingDelta>(m)) think += t->text;
    CHECK(think == "Okay, let's see.");

    // Prose = 数组文本部分 + 平方字符串答案;思考永不漏出.
    CHECK(joined_text(msgs) == "Part-array prose. Four");
    CHECK(count_leaf<StreamFinished>(msgs) == 1);
}

// Mistral / 法官内在推理作为[ THINK]…[/THINK] 内部
// 内容( 无) reasoning_content 字段。它必须表面为
// StreamThinkingDelta ,标签必须剥除,可见的答案不得 丢失第一个
// 字符( 首字母) `[` 用来绊倒工具 -call 抢救。
TEST_CASE("test_sse_think_tags_in_content") {
    std::string sse =
        "data: {\"choices\":[{\"delta\":{\"content\":\"[THINK]let me think[/THINK]\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"Answer: 42\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
        "data: [DONE]\n\n";
    auto msgs = oai::parse_sse_for_test(sse);
    std::string think;
    for (const auto& m : msgs)
        if (const auto* t = get_leaf<StreamThinkingDelta>(m)) think += t->text;
    CHECK(think == "let me think");          // 推理
    CHECK(joined_text(msgs) == "Answer: 42"); // 标签已剥除, 第一个字符保存
}

// A THINK 标签 SPLIT 跨两个 SSE 三角洲( 3 0 个) "[TH" |
// "INK]let me…" ——部分标记是携带的,而不是作为传教品排放的。
TEST_CASE("test_sse_think_tags_split_across_deltas") {
    std::string sse =
        "data: {\"choices\":[{\"delta\":{\"content\":\"[TH\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"INK]deep[/TH\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"INK]hi\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
        "data: [DONE]\n\n";
    auto msgs = oai::parse_sse_for_test(sse);
    std::string think;
    for (const auto& m : msgs)
        if (const auto* t = get_leaf<StreamThinkingDelta>(m)) think += t->text;
    CHECK(think == "deep");
    CHECK(joined_text(msgs) == "hi");   // 无流浪者 "[TH" / "[/TH" 泄露的线索
}

// REASON-BY-DEFAULT (马) 数据流 以 NO
// 打开标记——所有直到第一个[/ THINK 逻辑,其余的都是满足的。
TEST_CASE("test_sse_reason_by_default_no_open_tag") {
    std::string sse =
        "data: {\"choices\":[{\"delta\":{\"content\":\"working it out\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\" carefully[/THINK]Done.\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
        "data: [DONE]\n\n";
    // reason_by_default = 事实(截至 magistral/deepseek-r1).
    auto msgs = oai::parse_sse_for_test(sse, {}, false, /*reason_by_default=*/true);
    std::string think;
    for (const auto& m : msgs)
        if (const auto* t = get_leaf<StreamThinkingDelta>(m)) think += t->text;
    CHECK(think == "working it out carefully");   // 引文 = 推理
    CHECK(joined_text(msgs) == "Done.");          // 关闭后=内容
}

// DeepSeek-R1 / 问讯. QwQ / 多数本地模型使用 <think>…</think>
// 而不是米斯特拉尔的[ THINK 同样的提取,同样的保证。
TEST_CASE("test_sse_angle_think_tags") {
    std::string sse =
        "data: {\"choices\":[{\"delta\":{\"content\":\"<think>hmm</think>Yes\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
        "data: [DONE]\n\n";
    auto msgs = oai::parse_sse_for_test(sse);
    std::string think;
    for (const auto& m : msgs)
        if (const auto* t = get_leaf<StreamThinkingDelta>(m)) think += t->text;
    CHECK(think == "hmm");
    CHECK(joined_text(msgs) == "Yes");
}

// 字义 "</think>" 必须在真实的答案中出现( 开始时没有推理)
// 不 复古 -hide 答案——原因 -by-default 探测
// 器只在极处开火 数据流 开始
TEST_CASE("test_sse_stray_close_tag_in_prose_is_kept") {
    std::string sse =
        "data: {\"choices\":[{\"delta\":{\"content\":\"The tag \"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"</think> ends reasoning.\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
        "data: [DONE]\n\n";
    auto msgs = oai::parse_sse_for_test(sse);
    // 第一次三角洲行动 "The tag " 探测器解除了 后
    // 一个 </think> 答案中是逐字的,而不是作为推理结尾。
    CHECK(joined_text(msgs).find("The tag") != std::string::npos);
    CHECK(joined_text(msgs).find("ends reasoning") != std::string::npos);
}

TEST_CASE("test_sse_tool_call_nested_object_fragments") {
    // 一期48号. llama.cpp (和任何服务器) 数据流 符号
    // -by-token (b) 碎片 `function.arguments`
    // 细到可以使碎片等于 PREFIX 以我们所积蓄的(财富)-- `{"`
    // 打开 a NESTED 对象,当 缓冲区 已经从 `{"` 顶部 -level
    // 对象。
    //
    // 聊天补全不标签其 载荷 ,所以一个 -chunk
    // 测试无法从重报值的连接代理中分辨该碎片。
    // 古老的热力猜想 "retransmission" 扔了,
    // 它吃了那个 `{` 打开 `edits` 并使得整个参数字符串无效
    // JSON —— 显示在用户的
    // "old_string is missing — stream truncated"
    // 时间: 数据流 既未缺一栏,也未截断。
    auto frag = [](std::string_view a) {
        std::string esc;
        for (char c : a) {
            if (c == '"')       esc += "\\\"";
            else if (c == '\\') esc += "\\\\";
            else                 esc += c;
        }
        return "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
               "\"function\":{\"arguments\":\"" + esc + "\"}}]}}]}\n\n";
    };

    std::string sse =
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
            "\"id\":\"call_7\",\"type\":\"function\","
            "\"function\":{\"name\":\"edit\",\"arguments\":\"\"}}]}}]}\n\n";

    // 记者的形状: 破坏力的碎片是 `{"` 指数8,
    // 即 字节-identical 编号 缓冲区 头两个 字节.
    for (std::string_view f : {"{\"", "path", "\":\"", "/tmp/a.md", "\",\"",
                               "edits", "\":", "[", "{\"", "old_text",
                               "\":\"", "x", "\",\"", "new_text", "\":\"",
                               "y", "\"}", "]", "}"})
        sse += frag(f);

    sse += "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n\n"
           "data: [DONE]\n\n";

    auto msgs = oai::parse_sse_for_test(sse);

    // 每一个 字节 服务器发送,顺序, 没有掉任何。
    CHECK(joined_tool_args(msgs) ==
          std::string{"{\"path\":\"/tmp/a.md\",\"edits\":"
                      "[{\"old_text\":\"x\",\"new_text\":\"y\"}]}"});

    // 并且必须真的 解析 ——用户 -visible 症状是
    // 一种工具 调用 失败, 缺少所需的字段。
    auto parsed = nlohmann::json::parse(joined_tool_args(msgs), nullptr, false);
    CHECK(!parsed.is_discarded());
    CHECK(parsed.contains("edits"));
    CHECK(parsed["edits"].is_array());
    CHECK(parsed["edits"][0].contains("old_text"));
}

TEST_CASE("test_sse_tool_call_coalescing_proxy_snapshots") {
    // 电话 OTHER 载体, 其存在时的热度:
    // 重述 COMPLETE 每一个块的参数。
    // 盲调会让 `{"a":1}{"a":1,"b":2}` ;
    // 分类者必须注意每个 载荷 包含上一个,并只发出后缀。
    //
    // 故意在碎片试验旁边:这两个是每个试验对象的起因。
    // -chunk 猜想不可能, 和任何一种的固定 打破
    // 对方不是固定。
    std::string sse =
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
            "\"id\":\"call_9\",\"type\":\"function\","
            "\"function\":{\"name\":\"grep\",\"arguments\":\"\"}}]}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
            "\"function\":{\"arguments\":\"{\\\"pattern\\\":\"}}]}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
            "\"function\":{\"arguments\":\"{\\\"pattern\\\":\\\"x\\\"}\"}}]}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n\n"
        "data: [DONE]\n\n";

    auto msgs = oai::parse_sse_for_test(sse);
    CHECK(joined_tool_args(msgs) == std::string{"{\"pattern\":\"x\"}"});

    auto parsed = nlohmann::json::parse(joined_tool_args(msgs), nullptr, false);
    CHECK(!parsed.is_discarded());
}

TEST_CASE("test_sse_tool_call_empty_id_on_continuation") {
    // 继续的块块 RESTATES `id` 以 "" 而不是省略它。
    // 几个 OpenAI-compatible 代理执行( llm)
    // -gateway #136是公开报道, 并且盲目分配它失去 调用 身份: 身份:
    // 以后的三角洲都会空空如也 ToolCallId ,
    // 则结束 -of-turn 扫描空的 ID 跳过槽,所以 调用 永远不关闭,
    // 转弯时会用工具挂起 调用 绝不是派遣者。
    std::string sse =
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
            "\"id\":\"call_keep\",\"type\":\"function\","
            "\"function\":{\"name\":\"read\",\"arguments\":\"\"}}]}}]}\n\n"
        // id 重报为空——不得 ⁇ call_keep.
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"\","
            "\"function\":{\"arguments\":\"{\\\"path\\\":\"}}]}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"\","
            "\"function\":{\"arguments\":\"\\\"a.txt\\\"}\"}}]}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n\n"
        "data: [DONE]\n\n";

    auto msgs = oai::parse_sse_for_test(sse);

    // ID在每一个航母上都存活下来.
    for (const auto& m : msgs) {
        if (const auto* s = get_leaf<StreamToolUseStart>(m))
            CHECK(s->id.value == "call_keep");
        if (const auto* d = get_leaf<StreamToolUseDelta>(m))
            CHECK(d->id.value == "call_keep");
        if (const auto* e = get_leaf<StreamToolUseEnd>(m))
            CHECK(e->id.value == "call_keep");
    }

    // 还有 调用 实际 CLOSED ——扫荡跳过空的ID,
    // 所以一个被涂抹的ID在这里作为缺失的出现. End 而不是一个错误的。
    CHECK(count_leaf<StreamToolUseEnd>(msgs) == 1);
    CHECK(joined_tool_args(msgs) == std::string{"{\"path\":\"a.txt\"}"});
}

TEST_CASE("test_sse_parallel_tool_calls_without_index") {
    // 聊天补全间断片 PARALLEL 调用 在一个 数据流 ,
    // 和 `index` 唯一能将它们分开的就是 忽略了用
    // 来降落的田地的提供者 调用 0档, 调制一个 调用
    // '对他人的争论' - 两者都 调用 堕落.
    //
    // 数组位置是自然的 回退 :这是什么? `index`
    // 意味着当存在, 而这就是什么 载荷 已经告诉我们了
    std::string sse =
        // 两位 调用 打开于 一个 三角洲,也没有指数
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":["
            "{\"id\":\"call_a\",\"type\":\"function\","
             "\"function\":{\"name\":\"read\",\"arguments\":\"\"}},"
            "{\"id\":\"call_b\",\"type\":\"function\","
             "\"function\":{\"name\":\"grep\",\"arguments\":\"\"}}"
        "]}}]}\n\n"
        // 两者的参数,仍然没有索引,相同的数组顺序.
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":["
            "{\"function\":{\"arguments\":\"{\\\"path\\\":\\\"a\\\"}\"}},"
            "{\"function\":{\"arguments\":\"{\\\"pattern\\\":\\\"b\\\"}\"}}"
        "]}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n\n"
        "data: [DONE]\n\n";

    auto msgs = oai::parse_sse_for_test(sse);

    // 两个人 调用 没有一个合并的blob。
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 2);
    CHECK(count_leaf<StreamToolUseEnd>(msgs) == 2);

    // 各别 调用 理由不言自明 调用.
    std::string a_args, b_args;
    for (const auto& m : msgs)
        if (const auto* d = get_leaf<StreamToolUseDelta>(m)) {
            if (d->id.value == "call_a") a_args += d->partial_json;
            if (d->id.value == "call_b") b_args += d->partial_json;
        }
    CHECK(a_args == std::string{"{\"path\":\"a\"}"});
    CHECK(b_args == std::string{"{\"pattern\":\"b\"}"});
}

TEST_CASE("test_sse_tool_call_unattributable_fragment_fails_the_turn") {
    // 其中一个案件归属问题无法真正解决:一个碎片 NEITHER
    // 编号 NOR 指数,数个时到达 调用 打开,
    // 数组 -position 回退 不适用(单项) -element
    // 属于打开的数组 调用).
    //
    // 猜测腐败的参数 SILENTLY - 失败者 JSON
    // 仍然 解析 ,所以工具运行与另一个 调用's 字节 附加。
    // 丢弃碎片可以避免腐败, 但以自己的方式沉默: 工具运行时
    // 带有参数 MISSING 模特儿被告知 调用 成功。
    //
    // 既不可以接受,转弯也不行。这也是泽德的行为。
    // ToolCallAccumulator::entry 返回时 AmbiguousToolCallChunk ,
    // 而地图员则将其变成一个完成错误而不是一个最佳的猜测).
    std::string sse =
        // 两位 调用 打开,每个都正确识别。
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":["
            "{\"index\":0,\"id\":\"call_a\",\"type\":\"function\","
             "\"function\":{\"name\":\"read\",\"arguments\":\"\"}}"
        "]}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":["
            "{\"index\":1,\"id\":\"call_b\",\"type\":\"function\","
             "\"function\":{\"name\":\"grep\",\"arguments\":\"\"}}"
        "]}}]}\n\n"
        // 一块没有身份的碎片 调用 无法知晓。
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":["
            "{\"function\":{\"arguments\":\"{\\\"path\\\":\\\"a\\\"}\"}}"
        "]}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n\n"
        "data: [DONE]\n\n";

    auto msgs = oai::parse_sse_for_test(sse);

    // 失败者为REPORTED,没有吞没.
    CHECK(count_leaf<StreamError>(msgs) >= 1);

    // 报了案 一次 ——长答复不能每个无法定位的块
    // 出现一个错误.
    CHECK(count_leaf<StreamError>(msgs) == 1);

    // 毒药的调用是NOT交给状态更新函数的,仿佛完成了:这里的
    // StreamToolUseEnd将使转弯的分派XX`read`没有路径.
    CHECK(count_leaf<StreamToolUseEnd>(msgs) == 0);

    // 错误将情况命名为足够适合采取行动。
    bool explains = false;
    for (const auto& m : msgs)
        if (const auto* e = get_leaf<StreamError>(m))
            explains = e->message.find("tool-call fragment") != std::string::npos;
    CHECK(explains);
}

TEST_CASE("test_sse_tool_call_index_reused_for_a_new_call") {
    // 提供者可以将REUSE指数0作为第二个平行的调用,
    // 仅用一个新的`id`来区分它们. 单是索引上的键法就将
    // 它们合并:第二个调用的论点被附加在第一个,两者都腐败.
    //
    // 从Zed那里学到的 ToolCallAccumulator ,而其中的键(id,索引)是共同的,
    // 正因为如此——
    // 单独依赖任一值都不可靠：部分提供方会省略索引、复用索引，或在并行调用间重复标识。
    //
    std::string sse =
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
            "\"id\":\"call_a\",\"type\":\"function\","
            "\"function\":{\"name\":\"read\","
            "\"arguments\":\"{\\\"path\\\":\\\"a\\\"}\"}}]}}]}\n\n"
        // 同样的指数, 不同 id——一个新的 调用 而不是继续
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
            "\"id\":\"call_b\",\"type\":\"function\","
            "\"function\":{\"name\":\"grep\","
            "\"arguments\":\"{\\\"pattern\\\":\"}}]}}]}\n\n"
        // 匿名续作:属于 LATEST 调用 指数。
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
            "\"function\":{\"arguments\":\"\\\"b\\\"}\"}}]}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n\n"
        "data: [DONE]\n\n";

    auto msgs = oai::parse_sse_for_test(sse);

    // 两个 调用 ,每个都有自己的论据——没有一个合并的blob.
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 2);
    CHECK(count_leaf<StreamToolUseEnd>(msgs) == 2);

    std::string a, b;
    for (const auto& m : msgs)
        if (const auto* d = get_leaf<StreamToolUseDelta>(m)) {
            if (d->id.value == "call_a") a += d->partial_json;
            if (d->id.value == "call_b") b += d->partial_json;
        }
    CHECK(a == std::string{"{\"path\":\"a\"}"});
    CHECK(b == std::string{"{\"pattern\":\"b\"}"});
}

TEST_CASE("test_sse_tool_call_id_only_no_index") {
    // MiniMax (Zed #42584) 识别 调用 由 ID
    // 单独发送,没有可用的索引。 调用 因此,到达时根本没有指数,
    // 唯一能将它们分开的就是ID.
    std::string sse =
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":["
            "{\"id\":\"call_a\",\"type\":\"function\","
             "\"function\":{\"name\":\"read\",\"arguments\":\"{\\\"path\\\":\"}}"
        "]}}]}\n\n"
        // 继续使用SAME id,仍无索引.
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":["
            "{\"id\":\"call_a\","
             "\"function\":{\"arguments\":\"\\\"src\\\"}\"}}"
        "]}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n\n"
        "data: [DONE]\n\n";

    auto msgs = oai::parse_sse_for_test(sse);

    // 一个 调用,不是两个: 重复的id是相同的调用 继续.
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 1);
    CHECK(count_leaf<StreamToolUseEnd>(msgs) == 1);
    CHECK(joined_tool_args(msgs) == std::string{"{\"path\":\"src\"}"});
}

TEST_CASE("test_sse_tool_call_stream") {
    // OpenAI 工具 -call 流式传输 : 打开 帧 携带 id+名称,
    // 之后 帧 携带辩证碎片; finish_reason "tool_calls".
    std::string sse =
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
            "\"id\":\"call_42\",\"type\":\"function\","
            "\"function\":{\"name\":\"read\",\"arguments\":\"\"}}]}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
            "\"function\":{\"arguments\":\"{\\\"path\\\":\"}}]}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
            "\"function\":{\"arguments\":\"\\\"a.txt\\\"}\"}}]}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n\n"
        "data: [DONE]\n\n";
    auto msgs = oai::parse_sse_for_test(sse);

    // 完全有一个工具-use打开+关闭.
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 1);
    CHECK(count_leaf<StreamToolUseEnd>(msgs) == 1);

    // 开始携带 id + 名称。
    for (const auto& m : msgs)
        if (const auto* s = get_leaf<StreamToolUseStart>(m)) {
            CHECK(s->id.value == "call_42");
            CHECK(s->name.value == "read");
        }

    // 集合的争论碎片构成完整的JSON.
    CHECK(joined_tool_args(msgs) == std::string{"{\"path\":\"a.txt\"}"});

    // finish_reason "tool_calls" → ToolUse.
    for (const auto& m : msgs)
        if (const auto* f = get_leaf<StreamFinished>(m))
            CHECK(f->stop_reason == StopReason::ToolUse);

    // 命令:必须在每一个三角洲之前开始, End 一定要跟着他们
    int start_idx = -1, end_idx = -1, last_delta_idx = -1;
    for (int i = 0; i < (int)msgs.size(); ++i) {
        if (get_leaf<StreamToolUseStart>(msgs[i])) start_idx = i;
        if (get_leaf<StreamToolUseDelta>(msgs[i])) last_delta_idx = i;
        if (get_leaf<StreamToolUseEnd>(msgs[i]))   end_idx = i;
    }
    CHECK(start_idx >= 0 && last_delta_idx > start_idx && end_idx > last_delta_idx);
}

TEST_CASE("test_sse_two_tool_calls") {
    // 两个平行工具 调用 (指数0然后指数1) 第二个
    // 指数必须关闭第一个指数。 调用 在第二场开场之前
    std::string sse =
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
            "\"id\":\"c0\",\"function\":{\"name\":\"glob\",\"arguments\":\"{}\"}}]}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":1,"
            "\"id\":\"c1\",\"function\":{\"name\":\"grep\",\"arguments\":\"{}\"}}]}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n\n"
        "data: [DONE]\n\n";
    auto msgs = oai::parse_sse_for_test(sse);

    CHECK(count_leaf<StreamToolUseStart>(msgs) == 2);
    CHECK(count_leaf<StreamToolUseEnd>(msgs) == 2);

    std::vector<std::string> ids;
    for (const auto& m : msgs)
        if (const auto* s = get_leaf<StreamToolUseStart>(m)) ids.push_back(s->id.value);
    CHECK(ids.size() == 2 && ids[0] == "c0" && ids[1] == "c1");
}

TEST_CASE("test_sse_error_frame") {
    std::string sse =
        "data: {\"error\":{\"message\":\"rate limit exceeded\",\"type\":\"rate_limit\"}}\n\n";
    auto msgs = oai::parse_sse_for_test(sse);
    bool saw_err = false;
    for (const auto& m : msgs)
        if (const auto* e = get_leaf<StreamError>(m)) {
            CHECK(e->message == "rate limit exceeded");
            saw_err = true;
        }
    CHECK(saw_err);
}

// ── 漏损 -tool-call 抢救( 弱化 Qwen2.5 等本地模型) -coder:7b) ──
// 这些模型释放出 调用 光着身子 JSON 输入 `content` 与
// finish_reason "stop" 而不是结构化 tool_calls [] 频道.
TEST_CASE("test_sse_salvage_leaked_tool_call") {
    // 确切的形状 Ollama 返回 qwen2.5 时 -coder : 7b: 包含{的一款
    // 内容三角形 "name":..,"arguments" :{. ⁇ 作为字符串.
    std::string sse =
        "data: {\"choices\":[{\"delta\":{\"content\":"
            "\"{\\\"name\\\": \\\"echo\\\", \\\"arguments\\\": "
            "{\\\"text\\\": \\\"hi\\\"}}\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
        "data: [DONE]\n\n";
    auto msgs = oai::parse_sse_for_test(sse, {"echo", "read"});

    // 漏水的 漏水了 漏水 漏水 JSON 必须是 真实 工具 调用 ,而不是表面为文本。
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 1);
    CHECK(count_leaf<StreamToolUseEnd>(msgs) == 1);
    CHECK(joined_text(msgs).empty());     // 文字里没有漏掉的东西 正文
    for (const auto& m : msgs)
        if (const auto* s = get_leaf<StreamToolUseStart>(m))
            CHECK(s->name.value == "echo");
    CHECK(joined_tool_args(msgs) == std::string{"{\"text\":\"hi\"}"});
    // 牺牲 调用 报告 ToolUse 所以,我们 状态更新函数 踢出工具环。
    for (const auto& m : msgs)
        if (const auto* f = get_leaf<StreamFinished>(m))
            CHECK(f->stop_reason == StopReason::ToolUse);
}

// ── <tool_call>-tag-wrapped 漏( 微调) ( qwen/Hermes 聊天 -template 窗体) ──────
// Quen2.5号 -coder 模板指示模型进行包扎 调用 输入
// <tool_call>…</tool_call> 时 Ollama 无法
// 剥离他们到达的标记 `content` ; 抢救必须剥去标签并回收 调用.
TEST_CASE("test_sse_salvage_tool_call_tags") {
    std::string sse =
        "data: {\"choices\":[{\"delta\":{\"content\":"
            "\"<tool_call>\\n{\\\"name\\\": \\\"echo\\\", "
            "\\\"arguments\\\": {\\\"text\\\": \\\"hi\\\"}}\\n"
            "</tool_call>\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
        "data: [DONE]\n\n";
    auto msgs = oai::parse_sse_for_test(sse, {"echo", "read"});
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 1);
    CHECK(joined_text(msgs).empty());   // 标记 + JSON 消耗,不泄漏
    for (const auto& m : msgs)
        if (const auto* s = get_leaf<StreamToolUseStart>(m))
            CHECK(s->name.value == "echo");
    CHECK(joined_tool_args(msgs) == std::string{"{\"text\":\"hi\"}"});
}

// 工具调用泄漏在 <tool_call> 标签内的 ```json 围栏中，同时带两种包裹形式。
// 某些模板制作)也必须打捞干净.
TEST_CASE("test_sse_salvage_fenced_tags") {
    std::string sse =
        "data: {\"choices\":[{\"delta\":{\"content\":"
            "\"<tool_call>```json\\n{\\\"name\\\": \\\"echo\\\", "
            "\\\"arguments\\\": {}}\\n```</tool_call>\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
        "data: [DONE]\n\n";
    auto msgs = oai::parse_sse_for_test(sse, {"echo"});
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 1);
    CHECK(joined_text(msgs).empty());
}

TEST_CASE("test_sse_salvage_unknown_tool_stays_text") {
    // 完整 JSON 对象 SHAPED 像个工具 调用 ({"name","arguments"
    // )但命名一个工具我们做了 不 广告是弱的 -model 错误类型 (E) e.g.
    // "read_file" 费用 "read" 它从未被拯救(我们从未发明过),
    // 调用) 和 从未露出生机 JSON 已经放下了,空的 -turn
    // 回退 然后填满转弯,这样用户就不会看到空白的气泡。
    std::string sse =
        "data: {\"choices\":[{\"delta\":{\"content\":"
            "\"{\\\"name\\\": \\\"nonexistent\\\", \\\"arguments\\\": {}}\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
        "data: [DONE]\n\n";
    auto msgs = oai::parse_sse_for_test(sse, {"echo"});
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 0);
    CHECK(joined_text(msgs).find('{') == std::string::npos);  // 原始的, 将使用 JSON 已丢弃
    CHECK(joined_text(msgs).find("nonexistent") == std::string::npos);
}

// 非 国 家 -tool-shaped JSON 对象( 无名称+参数密钥)
// 指定任何特定名称都是真 prose/data 它必须仍然作为文字浮出水面。
TEST_CASE("test_sse_plain_object_stays_text") {
    std::string sse =
        "data: {\"choices\":[{\"delta\":{\"content\":"
            "\"{\\\"answer\\\": 42, \\\"unit\\\": \\\"none\\\"}\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
        "data: [DONE]\n\n";
    auto msgs = oai::parse_sse_for_test(sse, {"echo"});
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 0);
    CHECK(!joined_text(msgs).empty());          // 而不是工具-shaped ——保存
    CHECK(joined_text(msgs).find("answer") != std::string::npos);
}

TEST_CASE("test_sse_plain_json_prose_not_salvaged") {
    // 普通的传教士,只是 STARTS 文本不是。 held/mangled.
    std::string sse =
        "data: {\"choices\":[{\"delta\":{\"content\":\"Sure, here you go.\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
        "data: [DONE]\n\n";
    auto msgs = oai::parse_sse_for_test(sse, {"echo"});
    CHECK(joined_text(msgs) == "Sure, here you go.");
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 0);
}

TEST_CASE("test_sse_structured_tool_still_works_with_salvage_on") {
    // A 真实 结构化工具 调用 一定不受救援路径的影响。
    std::string sse =
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
            "\"id\":\"call_1\",\"function\":{\"name\":\"read\","
            "\"arguments\":\"{}\"}}]}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n\n"
        "data: [DONE]\n\n";
    auto msgs = oai::parse_sse_for_test(sse, {"read"});
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 1);
    for (const auto& m : msgs)
        if (const auto* s = get_leaf<StreamToolUseStart>(m))
            CHECK(s->name.value == "read");
}

TEST_CASE("test_sse_truncated_leaked_tool_call_dropped") {
    // Quen 泄露了这个 调用 输入 `content` 但是,我们...
    // 传输 中关 -body (无闭合牙套,无) DONE 半个 -written
    // JSON 不得 表面是可见的传言——倾倒它会污染助手的转身和弱型号的再
    // -leaks 同样的 调用 下一个转弯( 粘住) "upstream cut off"
    // 内容 -invocation 原始的。 JSON ;
    // 否则转弯会完全空(没有文本,没有工具) 调用), ensure_nonempty_turn
    // 替换一个固定的哨兵,这样用户就永远看不到一个空白的气泡.
    std::string sse =
        "data: {\"choices\":[{\"delta\":{\"content\":"
            "\"{\\\"name\\\": \\\"remember\\\", \\\"argum\"}}]}\n\n";
    auto msgs = oai::parse_sse_for_test(sse, {"remember"});
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 0);  // 无法回收
    // 截断的JSON本身不得作为散文泄露.
    CHECK(joined_text(msgs).find("remember") == std::string::npos);
    CHECK(joined_text(msgs).find('{') == std::string::npos);
    // 但转弯不 -empty (空的) 维基百科中的相关条目: 维基语录链接:名人名言 - 文学作品 - 谚语 - 谚文 - 谚語 -turn 回退 ) , 被开除.
    CHECK(!joined_text(msgs).empty());
}

TEST_CASE("test_sse_fence_only_leak_dropped") {
    // qwen 回答问候时只返回 ```json 围栏，即泄漏的工具调用
    // 包装)和无 JSON 正文 — 以文字文本 "json"
    // 回答 "hi" 的bug。 光包装器必须作为 不 表面的散文;
    // 空的 -turn 回退 则填充转弯。
    std::string sse =
        "data: {\"choices\":[{\"delta\":{\"content\":\"```json\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
        "data: [DONE]\n\n";
    auto msgs = oai::parse_sse_for_test(sse, {"read"});
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 0);
    CHECK(joined_text(msgs).find("json") == std::string::npos);
    CHECK(joined_text(msgs).find('`') == std::string::npos);
}

TEST_CASE("test_sse_two_leaked_calls_unique_ids") {
    // 在一个数据流中,两个完全泄露的调用必须获得DISTINCT
    // 合成的ID,或者状态更新函数键都在同一卡(重复卡)上.
    std::string sse =
        "data: {\"choices\":[{\"delta\":{\"content\":"
            "\"{\\\"name\\\": \\\"echo\\\", \\\"arguments\\\": "
            "{\\\"text\\\": \\\"a\\\"}}\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
        "data: [DONE]\n\n";
    // 一个 数据流 只有一个扣子,所以验证身份是两个不同的
    // 解析 共享无ctx是微不足道的真;相反,id是以下的-0形式,
    // 因此未来第二次救援在同一个ctx中是不同的。
    auto msgs = oai::parse_sse_for_test(sse, {"echo"});
    for (const auto& m : msgs)
        if (const auto* s = get_leaf<StreamToolUseStart>(m))
            CHECK(s->id.value == "call_salvaged_0");
}

TEST_CASE("endpoints are a projection of the provider registry (SSOT)") {
    // 曾经存在的终点事实 TWICE : 一半在 注册表 排队,14分一半 -arm
    // 如果 -chain 内 from_spec 漂流着—— `openai` 广告
    // 行 Wire::OpenAIResponses 时 传输层 拨号/
    // v1/chat/completions ,这说明 -text UI 承诺输出
    // 传输 从来没有发送。
    //
    // from_spec 现在,这是一张表格的投影,
    // 这个针: HTTP-dialled 横行必须下决心
    // 恰好 未来硬码臂 阴影一行失败了
    for (const auto& row : provider::kProviders) {
        if (!row.http_dialled()) continue;
        CAPTURE(row.id);
        auto ep = oai::Endpoint::from_spec(row.id);
        CHECK(ep.host        == row.host);
        CHECK(ep.path        == row.path);
        CHECK(ep.models_path == row.models_path);
        CHECK(ep.port        == row.port);
        CHECK(ep.use_tls     == row.use_tls);
        CHECK(ep.native_api  == row.native_api);
        // 标签是每个分派站点比较的路由键.
        CHECK(ep.label       == row.id);
    }
}

TEST_CASE("non-HTTP providers keep their sentinel endpoints") {
    // Anthropic, ChatGPT 财务报告和已审计财务报表 ACP
    // 已经 不 到达的 OpenAI-compat 传输层 (自 发) 传输层
    // / OAuth 响应路径/ 子进程。 它们故意不带终点列,
    // 所以 from_spec 绝不能为他们发明一种——标签仍然必须正确路由。
    auto chatgpt = oai::Endpoint::from_spec("chatgpt");
    CHECK(chatgpt.label == "chatgpt");
    // 遗产谱系的词条与同一标签.
    CHECK(oai::Endpoint::from_spec("codex-cli").label == "chatgpt");
}

TEST_CASE("legacy codex alias resolves to the openai row, not a copy") {
    // `codex` 曾经是自己的 选择面板 排队 字节-identical
    // 改为 `openai`。现在它是一个别名, RESOLVES THROUGH
    // 编号 注册表 而不是重置宿主——因此它不能从 `openai`
    // 两者都是硬码字面文字时的
    auto codex  = oai::Endpoint::from_spec("codex");
    auto openai = oai::Endpoint::from_spec("openai");
    CHECK(codex.host == openai.host);
    CHECK(codex.path == openai.path);
    CHECK(codex.models_path == openai.models_path);
    CHECK(codex.label == openai.label);
    // 而空谱表示"the default host"——又同一行.
    auto empty = oai::Endpoint::from_spec("");
    CHECK(empty.host == openai.host);
    CHECK(empty.path == openai.path);
}

TEST_CASE("attribution: openrouter names agentty on the chat dialect") {
    // 问题74. OpenRouter 将请求输入到按键在 URL 我们发送,
    // 一些模型只服务 一种公认的代理带,所以这两个 头文件 是两个字
    // 之间的区别 "works" 财务报告和已审计财务报表 "403"
    // 部分内容: 目录.
    const auto find = [](const auto& hs, std::string_view name)
            -> std::optional<std::string> {
        for (const auto& [k, v] : hs)
            if (k == name) return v;
        return std::nullopt;
    };

    // ── 聊天方言 ──
    auto orouter = oai::Endpoint::from_spec("openrouter");
    auto referer = find(orouter.extra_headers, "http-referer");
    auto title   = find(orouter.extra_headers, "x-title");
    REQUIRE(referer.has_value());
    REQUIRE(title.has_value());
    // HTTP-Referer 是那个 PRIMARY 应用程序页面的密钥,
    // 所以必须是真正的公众 URL ——这里的占位符默默地创建了别人的
    // 应用页面,或者根本没有.
    CHECK(*referer == "https://agentty.org");
    CHECK(*title == "agentty");

    // ── 其他的一行都保持沉默,你都不要动 ──
    // 归属是选择 -in 每行发送 agentty's URL 改为 OpenAI ,
    // Groq或用户自己的 llama.cpp 最多是噪音,还有... 指纹 更糟糕的是,我们没有
    for (const char* id : {"openai", "groq", "together", "cerebras",
                           "deepseek", "mistral", "ollama", "llama.cpp"}) {
        auto ep = oai::Endpoint::from_spec(id);
        INFO("row: ", id);
        CHECK(!find(ep.extra_headers, "http-referer").has_value());
        CHECK(!find(ep.extra_headers, "x-title").has_value());
    }

    // 自定义主机的光谱匹配无行,所以它也得不到任何.
    auto custom = oai::Endpoint::from_spec("https://gw.example.com/api");
    CHECK(!find(custom.extra_headers, "http-referer").has_value());

    // ── 他们实际上到达了 传输 ──
    // 携带它们的结构不是重点;要求是重点。这是同样的建造者。
    // 数据流 和模型 -list 调用 穿过这里,
    // 意味着一个被归结的请求真的被归结了。
    const auto wire = [](const agentty::http::Headers& hs, std::string_view nm)
            -> std::optional<std::string> {
        for (const auto& x : hs)
            if (x.name == nm) return x.value;
        return std::nullopt;
    };
    auto hs = oai::build_request_headers(
        oai::AuthHeader{oai::BearerHeader{"sk-or-v1-test"}}, orouter);
    CHECK(wire(hs, "http-referer") == std::optional<std::string>{"https://agentty.org"});
    CHECK(wire(hs, "x-title")      == std::optional<std::string>{"agentty"});
    CHECK(wire(hs, "x-openrouter-categories") == std::optional<std::string>{"cli-agent"});
    // 认证 头文件 仍然赢得它的名额,额外名额是附加的,而不是在某个
    // 事物的顶端合并 加载-bearing.
    CHECK(wire(hs, "authorization") == std::optional<std::string>{"Bearer sk-or-v1-test"});
}

TEST_CASE("test_endpoint_presets") {
    auto groq = oai::Endpoint::from_spec("groq");
    CHECK(groq.host == "api.groq.com");
    CHECK(groq.use_tls);
    CHECK(groq.path == "/openai/v1/chat/completions");

    auto openrouter = oai::Endpoint::from_spec("openrouter");
    CHECK(openrouter.host == "openrouter.ai");
    CHECK(openrouter.use_tls);
    CHECK(openrouter.path == "/api/v1/chat/completions");
    CHECK(openrouter.models_path == "/api/v1/models");
    CHECK(!openrouter.native_api);

    auto together = oai::Endpoint::from_spec("together");
    CHECK(together.host == "api.together.xyz");
    CHECK(together.use_tls);
    CHECK(together.path == "/v1/chat/completions");

    auto cerebras = oai::Endpoint::from_spec("cerebras");
    CHECK(cerebras.host == "api.cerebras.ai");
    CHECK(cerebras.use_tls);
    CHECK(cerebras.path == "/v1/chat/completions");

    auto deepseek = oai::Endpoint::from_spec("deepseek");
    CHECK(deepseek.host == "api.deepseek.com");
    CHECK(deepseek.use_tls);
    // DeepSeek's OpenAI-compatible 终点位于 ROOT (无/v1前缀).
    CHECK(deepseek.path == "/chat/completions");
    CHECK(deepseek.models_path == "/models");

    auto xai = oai::Endpoint::from_spec("xai");
    CHECK(xai.host == "api.x.ai");
    CHECK(xai.use_tls);
    CHECK(xai.path == "/v1/chat/completions");
    CHECK(xai.models_path == "/v1/models");

    auto mistral = oai::Endpoint::from_spec("mistral");
    CHECK(mistral.host == "api.mistral.ai");
    CHECK(mistral.use_tls);
    CHECK(mistral.path == "/v1/chat/completions");

    auto gemini = oai::Endpoint::from_spec("gemini");
    CHECK(gemini.host == "generativelanguage.googleapis.com");
    CHECK(gemini.use_tls);
    // 谷歌的音乐 OpenAI-compat shim在/ i 下筑巢 / i v1beta/openai.
    CHECK(gemini.path == "/v1beta/openai/chat/completions");
    CHECK(gemini.models_path == "/v1beta/openai/models");

    auto fireworks = oai::Endpoint::from_spec("fireworks");
    CHECK(fireworks.host == "api.fireworks.ai");
    CHECK(fireworks.use_tls);
    CHECK(fireworks.path == "/inference/v1/chat/completions");
    CHECK(fireworks.models_path == "/inference/v1/models");

    auto ollama = oai::Endpoint::from_spec("ollama");
    CHECK(ollama.host == "localhost");
    CHECK(ollama.port == 11434);
    CHECK(!ollama.use_tls);
    CHECK(ollama.native_api);
    CHECK(ollama.path == "/api/chat");

    auto llama = oai::Endpoint::from_spec("llama.cpp");
    CHECK(llama.host == "localhost");
    CHECK(llama.port == 8080);
    CHECK(!llama.use_tls);
    CHECK(llama.path == "/v1/chat/completions");
    CHECK(!llama.native_api);   // OpenAI 方言,没有 Ollama 本地语言

    auto custom = oai::Endpoint::from_spec("my.host:8080");
    CHECK(custom.host == "my.host");
    CHECK(custom.port == 8080);
    CHECK(!custom.use_tls);
    CHECK(custom.label == "my.host:8080");   // 标签带有原始的光谱

    auto def = oai::Endpoint::from_spec("");
    CHECK(def.host == "api.openai.com");
    CHECK(def.use_tls);

    // ── 完整的URL谱(http://或https://prefix) 互联网档案馆的存檔,存档日期2013-09-02. ──────────────
    // A URL spec让用户为不服务于 /v1 的服务器选择自定义路径前缀( e.g.
    // chat.example.org 服务于/ api/chat/completions。
    // 路径在 URL 是一个 BASE PREFIX; agentty
    // 附录 / chat/completions 和/模具的它。
    {
        auto url = oai::Endpoint::from_spec("https://chat.example.org/api");
        CHECK(url.host == "chat.example.org");
        CHECK(url.port == 443);
        CHECK(url.use_tls);
        CHECK(url.path == "/api/chat/completions");
        CHECK(url.models_path == "/api/models");
        CHECK(!url.native_api);
        CHECK(url.label == "https://chat.example.org/api");
    }
    {
        // 退位(问题30):必须保留一个多-segment自定义路径前缀VERBATIM,
        // 并附有/chat/completions+/型号,
        // 不会崩溃为/v1.
        // z.ai的GLM编码计划服务于/api/coding/paas/v4,
        // 其/型号在那里存在——由于预计模型的取回会打错URL,所以选择面板号是空的.
        auto url = oai::Endpoint::from_spec(
            "https://api.z.ai/api/coding/paas/v4");
        CHECK(url.host == "api.z.ai");
        CHECK(url.port == 443);
        CHECK(url.use_tls);
        CHECK(url.path == "/api/coding/paas/v4/chat/completions");
        CHECK(url.models_path == "/api/coding/paas/v4/models");
        CHECK(!url.native_api);
    }
    {
        auto url = oai::Endpoint::from_spec("http://localhost:8080/custom");
        CHECK(url.host == "localhost");
        CHECK(url.port == 8080);
        CHECK(!url.use_tls);
        CHECK(url.path == "/custom/chat/completions");
        CHECK(url.models_path == "/custom/models");
    }
    {
        // 方案后无路径 : DEFAULT (无) OpenAI 方言生活在/v1下,
        // 每个真正的服务器上—— api.openai.com, llama.cpp ,vLLM,,
        // (英语: LM 工作室; 空 "/chat/completions" 全部404块).
        auto url = oai::Endpoint::from_spec("https://my-gateway.com");
        CHECK(url.host == "my-gateway.com");
        CHECK(url.port == 443);
        CHECK(url.use_tls);
        CHECK(url.path == "/v1/chat/completions");
        CHECK(url.models_path == "/v1/models");
    }
    {
        // 前缀的拖拉刀在附件之前被剥除。
        auto url = oai::Endpoint::from_spec("https://host:9000/prefix/");
        CHECK(url.host == "host");
        CHECK(url.port == 9000);
        CHECK(url.use_tls);
        CHECK(url.path == "/prefix/chat/completions");
        CHECK(url.models_path == "/prefix/models");
    }
    {
        // http://带有明确的端口,没有路径 →/v1 默认(见上文).
        auto url = oai::Endpoint::from_spec("http://10.0.0.5:5000");
        CHECK(url.host == "10.0.0.5");
        CHECK(url.port == 5000);
        CHECK(!url.use_tls);
        CHECK(url.path == "/v1/chat/completions");
        CHECK(url.models_path == "/v1/models");
    }
    {
        // "#name" 片段: a LOCAL 多个 -account 标记。
        // 两个单在片段拨号时不同的规格 IDENTICAL 终点( E)
        // host/port/path )但保持不同的标签——不同的设置键——分开
        // API 键和 已保存 模型每个账户在同一服务器上。
        auto a = oai::Endpoint::from_spec("https://ollama.com/v1#work");
        auto b = oai::Endpoint::from_spec("https://ollama.com/v1#personal");
        CHECK(a.host == "ollama.com");
        CHECK(a.port == 443);
        CHECK(a.use_tls);
        CHECK(a.path == "/v1/chat/completions");
        CHECK(a.models_path == "/v1/models");
        CHECK(b.host == a.host);
        CHECK(b.path == a.path);
        CHECK(a.label == "https://ollama.com/v1#work");
        CHECK(b.label == "https://ollama.com/v1#personal");
    }
    {
        // 原始主机上的碎片: port spec——剥离BEFORE 端口拆分.
        auto url = oai::Endpoint::from_spec("my-box.lan:8080#lab");
        CHECK(url.host == "my-box.lan");
        CHECK(url.port == 8080);
        CHECK(!url.use_tls);
        CHECK(url.label == "my-box.lan:8080#lab");
    }
    {
        // 无效的端口(non-numeric) ~ 回到默认方案。
        auto url = oai::Endpoint::from_spec("https://host:abc/path");
        CHECK(url.host == "host");
        CHECK(url.port == 443);
        CHECK(url.use_tls);
        CHECK(url.path == "/path/chat/completions");
    }
    {
        // Out-of-range端口 ~ 回到默认方案。
        auto url = oai::Endpoint::from_spec("http://host:99999/path");
        CHECK(url.host == "host");
        CHECK(url.port == 80);
        CHECK(!url.use_tls);
        CHECK(url.path == "/path/chat/completions");
    }
    {
        // 空主机( E) e.g. "https:///path" ) —— 倒回到默认终点.
        auto url = oai::Endpoint::from_spec("https:///path");
        CHECK(url.host == "api.openai.com");
        CHECK(url.port == 443);
        CHECK(url.use_tls);
    }
    {
        // 后向commat:赤主机:port(无方案)仍然使用/v1默认.
        auto noscheme = oai::Endpoint::from_spec("my.host:8080");
        CHECK(noscheme.host == "my.host");
        CHECK(noscheme.port == 8080);
        CHECK(!noscheme.use_tls);
        CHECK(noscheme.path == "/v1/chat/completions");
        CHECK(noscheme.models_path == "/v1/models");
        CHECK(noscheme.label == "my.host:8080");
    }
    {
        // IPv6 字面上加一个端口:端口结肠是AFTERXXXX
        // ']'的1个,拨号主机的括号被剥离.
        auto url = oai::Endpoint::from_spec("http://[::1]:8080/api");
        CHECK(url.host == "::1");
        CHECK(url.port == 8080);
        CHECK(!url.use_tls);
        CHECK(url.path == "/api/chat/completions");
    }
    {
        // IPv6 字面上带有NO端口:rfind(':')
        // 在地址内会分裂——括号解析将整个地址作为主机保存.
        auto url = oai::Endpoint::from_spec("https://[2001:db8::1]/v1");
        CHECK(url.host == "2001:db8::1");
        CHECK(url.port == 443);
        CHECK(url.use_tls);
        CHECK(url.path == "/v1/chat/completions");
    }

    // 注册表 ↔ from_spec 一致性 : 每个 OpenAI-family 预设的 id
    // 必须解决到可用的终点( 非 -empty 主机+ 单机 chat/models ) 路径。
    // Anthropic 被跳过——它没有穿过 OpenAI from_spec.
    // 接住这个 "added a registry row, forgot the endpoint arm"
    // 这类缺陷：回退会把标识当作原始主机名，并
    // 默然拨错地方.
    for (const auto& p : agentty::provider::providers()) {
        if (p.kind() != agentty::provider::Kind::OpenAI) continue;
        // 跳过行不是 到达的 OpenAI-compat 传输层.
        // ChatGPT 谓 Kind::OpenAI 却骑着自己的奥
        // 奥特 传输层 (/backend-api/codex/responses ),
        // 没有端点柱,因此坚持 host/port/TLS 之前这行人声称
        // is_local 真实的站立 -in 费用 "needs no API key";
        // http_dialled ()是诚实的上游,
        // 注册表 已经暴露了
        if (!p.http_dialled()) continue;
        auto ep = oai::Endpoint::from_spec(p.id);
        CHECK(!ep.host.empty());
        CHECK(!ep.path.empty());
        CHECK(!ep.models_path.empty());
        // 主办( 非) -local ) 预设必须使用 TLS 当地人不能
        if (p.is_local) {
            CHECK(!ep.use_tls);
        } else {
            CHECK(ep.use_tls);
            CHECK(ep.port == 443);
        }
        // 标签必须圆 -trip 编号 相同 预设此
        // 模式徽章和提供者读出名称( 不通过原始 -host
        // 标签分支).
        CHECK(ep.label == p.id);
    }
}

// ── 包装模型种子:主机 API-key 提供者在密钥前显示模型 ──
// 与一个 空 认证 头文件, openai::list_models 短数
// -circuits 空为 TLS 终点( 无网络)。
// list_models_for 则必须回到每 -provider 捆绑种子,
// 以便新选择的主机提供者在 选择面板 键盘设定前。活的抓取将取代以后的键盘。
TEST_CASE("bundled model seed when no key / fetch empty") {
    namespace P = agentty::provider;
    const auth::AuthHeader none{};

    for (const char* id : {"xai", "mistral", "gemini", "fireworks",
                           "deepseek", "groq", "cerebras", "together"}) {
        auto models = P::list_models_for(::agentty::IoAccess::grant(), agtest::cap(), P::parse_selection(id), none);
        CHECK(!models.empty());
        // 种子邮票 供应商标签 选择面板 组好它右。
        if (!models.empty())
            CHECK(models.front().provider == std::string{id});
    }

    // 几块混凝土弹片落地 预想(最新第一).
    {
        auto xai = P::list_models_for(::agentty::IoAccess::grant(), agtest::cap(), P::parse_selection("xai"), none);
        CHECK(!xai.empty());
        if (!xai.empty()) CHECK(xai.front().id.value == "grok-4.6");
    }
    {
        auto gem = P::list_models_for(::agentty::IoAccess::grant(), agtest::cap(), P::parse_selection("gemini"), none);
        CHECK(!gem.empty());
        if (!gem.empty()) CHECK(gem.front().id.value == "gemini-2.5-pro");
    }

    // 提供WITHOUT 种子( openrouter, 自定义主机,
    // 本地端) 合法地保持空置, 没有密钥, 而不是显示猜测的模型。
    {
        auto orr = P::list_models_for(::agentty::IoAccess::grant(), agtest::cap(), P::parse_selection("openrouter"), none);
        CHECK(orr.empty());
    }
}

// ── provider_display_name: URL-form 标签崩溃到主机 [: port] ──
// 习惯 OpenAI-compatible 主机输入为 "https://chat.example.org/api" 已经 Endpoint::label
// ⁇   ⁇ URL (见A/C.5/61/L.48,第16段) Endpoint::from_spec, transport.cpp).
// 电话 badge/toast 改为: "chat.example.org" ,不是长 URL。
// 省略了方案默认端口;非 -default 端口为保存( 3) "host:port").
TEST_CASE("test_provider_display_name_url_label") {
    namespace P = agentty::provider;
    using oai::Endpoint;

    {
        P::Selection s;
        s.kind = P::Kind::OpenAI;
        s.openai_endpoint = Endpoint::from_spec("https://chat.example.org/api");
        // 圣洁: from_spec 已建的 URL-form 终点设计。
        CHECK(s.openai_endpoint.host == "chat.example.org");
        CHECK(s.openai_endpoint.port == 443);
        CHECK(s.openai_endpoint.use_tls);
        CHECK(s.openai_endpoint.path == "/api/chat/completions");
        // 标签保留完整的URL( 预设的查找键); 显示崩溃。
        CHECK(s.openai_endpoint.label == "https://chat.example.org/api");
        CHECK(P::provider_display_name(s) == "chat.example.org");
    }
    {
        // Non-default https端口:保持主机:port.
        P::Selection s;
        s.kind = P::Kind::OpenAI;
        s.openai_endpoint = Endpoint::from_spec("https://host:9000/prefix/");
        CHECK(P::provider_display_name(s) == "host:9000");
    }
    {
        // http://with no. (帮助) 互联网档案馆的存檔,存档日期2013-03-02. -default 端口: 保留主机: 端口( 80) IS http
        // 的默认值, 所以 http://host:80 → "host" ,没有 "host:80").
        P::Selection s;
        s.kind = P::Kind::OpenAI;
        s.openai_endpoint = Endpoint::from_spec("http://10.0.0.5:5000");
        CHECK(P::provider_display_name(s) == "10.0.0.5:5000");
    }
    {
        // http://带有默认端口 80:崩溃到赤裸主机.
        P::Selection s;
        s.kind = P::Kind::OpenAI;
        s.openai_endpoint = Endpoint::from_spec("http://10.0.0.5:80");
        CHECK(P::provider_display_name(s) == "10.0.0.5");
    }
    {
        // 赤主机[:port](无方案)标签不变:通过.
        P::Selection s;
        s.kind = P::Kind::OpenAI;
        s.openai_endpoint = Endpoint::from_spec("my.host:8080");
        CHECK(s.openai_endpoint.label == "my.host:8080");
        CHECK(P::provider_display_name(s) == "my.host:8080");
    }
    {
        // IPv6 字面上崩溃到括号内的权威( 有效形式),
        // 端口保留在 之后 ']' 无括号 -awareness
        // 标签会在地址内拆分。
        P::Selection s;
        s.kind = P::Kind::OpenAI;
        s.openai_endpoint = Endpoint::from_spec("http://[::1]:8080/api");
        CHECK(P::provider_display_name(s) == "[::1]:8080");
    }
    {
        // 套接字IPv6:默认端口省略,括号保留.
        P::Selection s;
        s.kind = P::Kind::OpenAI;
        s.openai_endpoint = Endpoint::from_spec("https://[2001:db8::1]/v1");
        CHECK(P::provider_display_name(s) == "[2001:db8::1]");
    }
}

// ── TUI 自定义 -host 提交: 光谱字符串必须圆 -trip 不变 ──
// login_submit's CustomHostInput 用来剥离的手臂 URL
// 修剪断路的图谋 https://host/api → /v1/chat/completions.
// 修复后,原始光谱会流入 parse_selection → from_spec。
// 这个将合同定在 TUI 必须接受: URL 窗体 a CLI 用户可以通过
// --provider ,加上裸体主机[:port].
TEST_CASE("endpoint: the URL a provider actually hands you") {
    namespace P = agentty::provider;

    // 该 一个 PEOPLE HIT 医生说这条路是一条 PREFIX
    // 并附加 chat/completions. 但供应商的仪表盘 给了你,
    // 因此你粘贴的 是完整的终点 URL . 与所产生的项目相配合
    // "/v1/chat/completions/chat/completions"
    // :每一次请求404次,来自 URL 显示在设置中显然正确 面板.
    {
        auto sel = P::parse_selection("https://api.example.com/v1/chat/completions");
        CHECK(sel.openai_endpoint.host == "api.example.com");
        CHECK(sel.openai_endpoint.path == "/v1/chat/completions");
        CHECK(sel.openai_endpoint.models_path == "/v1/models");
    }
    // 同 URL 是一个前缀,两种拼写都命名同一个终点,
    // 所以两者必须降落在它上——这就是接受完整形式的全部点.
    {
        auto sel = P::parse_selection("https://api.example.com/v1");
        CHECK(sel.openai_endpoint.path == "/v1/chat/completions");
    }
    // 一条网关在 sub-path 下, 贴满整条.
    {
        auto sel = P::parse_selection("https://gw.corp/openai/v1/chat/completions");
        CHECK(sel.openai_endpoint.path == "/openai/v1/chat/completions");
        CHECK(sel.openai_endpoint.models_path == "/openai/v1/models");
    }
}

TEST_CASE("endpoint: query strings and fragments never reach the path") {
    namespace P = agentty::provider;
    // 阿苏雷 -style URL携带? API-version=. 左转前缀,
    // 它变成了 "/v1?api-version=x/chat/completions" ,每个服务器404。
    {
        auto sel = P::parse_selection("https://api.example.com/v1?api-version=2024");
        CHECK(sel.openai_endpoint.host == "api.example.com");
        CHECK(sel.openai_endpoint.path == "/v1/chat/completions");
    }
    // 碎片是我们自己的多块 -account 标记,并且从未拨号。
    {
        auto sel = P::parse_selection("https://api.example.com/v1#work");
        CHECK(sel.openai_endpoint.path == "/v1/chat/completions");
    }
}

TEST_CASE("endpoint: userinfo is stripped from the dialled host") {
    namespace P = agentty::provider;
    // "user:pass@host" 左整形变成了 SNI 名称和 Host 头文件 ,
    // 所以,我说 TLS 名称不匹配失败——作为证书错误显示给用户。
    // "your URL has credentials in it".（URL 中携带了凭据。）
    {
        auto sel = P::parse_selection("https://user:pw@api.example.com/v1");
        CHECK(sel.openai_endpoint.host == "api.example.com");
        CHECK(sel.openai_endpoint.port == 443);
    }
    // 赤主机[:port]臂达到相同的拨号代码,
    // 所以它需要同样的清理——一个臂接受一个形状,另一个拒绝是自己
    // 的bug报告.
    {
        auto sel = P::parse_selection("user@my-box.lan:8080");
        CHECK(sel.openai_endpoint.host == "my-box.lan");
        CHECK(sel.openai_endpoint.port == 8080);
    }
}

TEST_CASE("endpoint: a host is dialled case-insensitively") {
    namespace P = agentty::provider;
    // 主机名为例 -insensitive (RFC 4343
    // 但我们比较一下它们作为弦 SNI ,则 Host 头文件 ,
    // 并显示 凭据 查找在端点上按键。 大写用户不应获得第二个密钥
    // -less 同一服务器的身份。
    {
        auto a = P::parse_selection("https://API.Example.COM/v1");
        auto b = P::parse_selection("https://api.example.com/v1");
        CHECK(a.openai_endpoint.host == "api.example.com");
        CHECK(a.openai_endpoint.host == b.openai_endpoint.host);
    }
    {
        auto sel = P::parse_selection("My-Box.LAN:8080");
        CHECK(sel.openai_endpoint.host == "my-box.lan");
    }
}

TEST_CASE("endpoint: duplicate slashes collapse") {
    namespace P = agentty::provider;
    // 一只手,你来帮忙 -edited 或加固过的 URL 产量 "//v1" ,
    // 和 "//v1/chat/completions" 是一个与大多数路由器不同的路径。
    auto sel = P::parse_selection("https://api.example.com//v1");
    CHECK(sel.openai_endpoint.path == "/v1/chat/completions");
}

TEST_CASE("test_tui_custom_host_specs") {
    namespace P = agentty::provider;

    // 用户报告断裂的确切光谱。
    {
        auto sel = P::parse_selection("https://chat.example.org/api");
        CHECK(sel.kind == P::Kind::OpenAI);
        CHECK(sel.openai_endpoint.host == "chat.example.org");
        CHECK(sel.openai_endpoint.port == 443);
        CHECK(sel.openai_endpoint.use_tls);
        CHECK(sel.openai_endpoint.path == "/api/chat/completions");
        CHECK(sel.openai_endpoint.models_path == "/api/models");
    }
    // http://带有明确的端口和路径前缀.
    {
        auto sel = P::parse_selection("http://localhost:8080/custom");
        CHECK(sel.kind == P::Kind::OpenAI);
        CHECK(sel.openai_endpoint.host == "localhost");
        CHECK(sel.openai_endpoint.port == 8080);
        CHECK(!sel.openai_endpoint.use_tls);
        CHECK(sel.openai_endpoint.path == "/custom/chat/completions");
        CHECK(sel.openai_endpoint.models_path == "/custom/models");
    }
    // https://没有路径 →/v1 默认(OpenAI方言生活在/v1下,
    // 每个真实服务器上;一个赤裸的/chat/completions 404s 处处).
    {
        auto sel = P::parse_selection("https://my-gateway.com");
        CHECK(sel.kind == P::Kind::OpenAI);
        CHECK(sel.openai_endpoint.host == "my-gateway.com");
        CHECK(sel.openai_endpoint.path == "/v1/chat/completions");
        CHECK(sel.openai_endpoint.models_path == "/v1/models");
    }
    // 赤主 [:port] (legacy TUI 行为) → /v1 默认,不变.
    {
        auto sel = P::parse_selection("localhost:8080");
        CHECK(sel.kind == P::Kind::OpenAI);
        CHECK(sel.openai_endpoint.host == "localhost");
        CHECK(sel.openai_endpoint.port == 8080);
        CHECK(!sel.openai_endpoint.use_tls);
        CHECK(sel.openai_endpoint.path == "/v1/chat/completions");
        CHECK(sel.openai_endpoint.models_path == "/v1/models");
    }
}

// ── 请求 头文件 / --auth-header ─────────────────────────────────────────
// build_request_headers 单位是 OpenAI-family
// 认证 头文件 已发射( 4) 数据流 模型列表, Ollama。验证默认的熊鼠臂,
// 自定义 -name 覆盖, 和空 -key 案件记录。
static void find_header(const agentty::http::Headers& hs, std::string_view name,
                        const agentty::http::Header** out) {
    *out = nullptr;
    for (const auto& h : hs)
        if (h.name == name) { *out = &h; return; }
}

TEST_CASE("local error hint: only ask 'is it running' when it never answered (#108)") {
    oai::Endpoint ep;
    ep.host = "localhost";
    ep.port = 11434;
    ep.use_tls = false;
    using K = agentty::http::HttpErrorKind;

    // 拒绝 / 从未回答 : 服务器可能真的关闭。
    const auto down = oai::local_transport_hint(ep, 0, K::Connect);
    CHECK(down.find("is the local server running on localhost:11434") != std::string::npos);

    // 回答200,然后安静下来: WAS 运行中,这是108号
    const auto quiet = oai::local_transport_hint(ep, 200, K::Timeout);
    CHECK(quiet.find("is the local server running") == std::string::npos);
    CHECK(quiet.find("answered") != std::string::npos);

    // 回答,然后放弃连接:没有误导暗示.
    CHECK(oai::local_transport_hint(ep, 200, K::PeerClosed).empty());
}

TEST_CASE("test_build_request_headers") {
    oai::Endpoint def;   // 否 auth_header_name = 持枪者

    // 默认:双亲双臂发射`authorization: Bearer <key>`.
    const agentty::http::Header* h = nullptr;
    auto hs = oai::build_request_headers(oai::AuthHeader{oai::ApiKeyHeader{"k1"}}, def);
    find_header(hs, "authorization", &h);
    CHECK(h && h->value == "Bearer k1");
    // 每一个 OpenAI-family 请求请求未压缩 正文 —
    // agentty's HTTP 客户端没有内容 -encoding 请求时的
    // 充气器 -build 侧, 以及一个关口, 反正Gzips( z.ai ,
    // 第30期由客户端的解码器处理。 -SSE 请求永远不能省略。
    find_header(hs, "accept-encoding", &h);
    CHECK(h && h->value == "identity");

    hs = oai::build_request_headers(oai::AuthHeader{oai::BearerHeader{"k2"}}, def);
    find_header(hs, "authorization", &h);
    CHECK(h && h->value == "Bearer k2");

    // 自定义名称: 键出局 RAW 名称(较次),
    // 无授权 头文件 发送。
    oai::Endpoint custom;
    custom.auth_header_name = "X-API-Key";
    hs = oai::build_request_headers(oai::AuthHeader{oai::ApiKeyHeader{"k3"}}, custom);
    find_header(hs, "x-api-key", &h);
    CHECK(h && h->value == "k3");
    find_header(hs, "authorization", &h);
    CHECK(h == nullptr);

    // 空密钥: 在两个方案( 本地后端) 下没有 头文件 认证。
    hs = oai::build_request_headers(oai::AuthHeader{oai::ApiKeyHeader{""}}, custom);
    find_header(hs, "x-api-key", &h);
    CHECK(h == nullptr);
    find_header(hs, "authorization", &h);
    CHECK(h == nullptr);

    // $parse_selection 将会话覆盖到终点(空
    // 的会话则会让它清晰可见).
    agentty::provider::set_custom_auth_header(::agentty::IoAccess::grant(), "Api-Key");
    auto sel = agentty::provider::parse_selection("my.host:9000");
    CHECK(sel.openai_endpoint.auth_header_name == "Api-Key");
    agentty::provider::set_custom_auth_header(::agentty::IoAccess::grant(), "");
    sel = agentty::provider::parse_selection("my.host:9000");
    CHECK(sel.openai_endpoint.auth_header_name.empty());
}

// ── 人数 -preset 认证决议 ──────────────────────────────────────────────
// resolve_auth_for 每一个提供者开关都经过的单一映射
// ( 启动) 和 编号 选择面板。验证右方认证上每个预设的种类土地:
//   • Anthropic ——登录证明,逐字记录。
//   • 当地( ollama/llama.cpp ——一个 空 键(无 auth),永不设嵌入链。
//   • 主办 OpenAI-family □ 无记名钥匙,优先
//     cli_key > saved_key > env链.
TEST_CASE("test_resolve_auth_per_preset") {
    using namespace agentty::provider;
    // 个性鲜明 Anthropic 证明我们可以证明它圆 -trips 毫无动静。
    auth::AuthHeader anthropic{auth::BearerHeader{"anthropic-oauth-token"}};

    // 清除任何环境键, 使 env-chain 分支具有决定性。
    ::unsetenv("OPENAI_API_KEY");
    ::unsetenv("GROQ_API_KEY");
    ::unsetenv("OPENROUTER_API_KEY");
    ::unsetenv("TOGETHER_API_KEY");
    ::unsetenv("CEREBRAS_API_KEY");

    for (const auto& p : providers()) {
        auto a = resolve_auth_for(p.id, anthropic, /*cli_key=*/{}, /*saved_key=*/{});
        if (p.kind() == Kind::Anthropic) {
            // 恢复登录信用值不变。
            CHECK(!auth::is_empty(a));
            auto* b = std::get_if<auth::BearerHeader>(&a);
            CHECK(b && b->token == "anthropic-oauth-token");
        } else if (p.auth == AuthStyle::None) {
            // 本地后端: 空密钥, 它不得 抓住了那个
            // Anthropic 意外的标志。
            CHECK(auth::is_empty(a));
        } else {
            // 已主机 OpenAI-family 没有 env/saved/cli 键 = 空持单人。
            CHECK(auth::is_empty(a));
        }
    }

    // 已保存 键( 输入 -app 粘贴路径),为主机提供者所尊崇.
    {
        auto a = resolve_auth_for("openrouter", anthropic,
                                  /*cli_key=*/{}, /*saved_key=*/"sk-saved");
        CHECK(!auth::is_empty(a));
        auto* k = std::get_if<auth::ApiKeyHeader>(&a);
        CHECK(k && k->value == "sk-saved");
    }
    // cli_key 赢过 saved_key.
    {
        auto a = resolve_auth_for("groq", anthropic,
                                  /*cli_key=*/"sk-cli", /*saved_key=*/"sk-saved");
        auto* k = std::get_if<auth::ApiKeyHeader>(&a);
        CHECK(k && k->value == "sk-cli");
    }
    // 本地后端忽略了已保存密钥( stayes keyles).
    {
        auto a = resolve_auth_for("ollama", anthropic,
                                  /*cli_key=*/{}, /*saved_key=*/"sk-ignored");
        CHECK(auth::is_empty(a));
    }
}

// ── 递增性打捞试验 ──────────────────────────────────────────────────

// 流线 JSON 符号( 如真实) Ollama —— 正确打捞。
TEST_CASE("test_sse_salvage_streamed_tokens") {
    // 模拟如何 Ollama 发送 JSON 一次一个标志。
    std::string sse =
        "data: {\"choices\":[{\"delta\":{\"content\":\"{\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"\\\"name\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"\\\":\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\" \\\"read\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"\\\",\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\" \\\"arguments\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"\\\":\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\" {}\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"}\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
        "data: [DONE]\n\n";
    auto msgs = oai::parse_sse_for_test(sse, {"read", "write"});
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 1);
    CHECK(count_leaf<StreamToolUseEnd>(msgs) == 1);
    CHECK(joined_text(msgs).empty());
    for (const auto& m : msgs)
        if (const auto* s = get_leaf<StreamToolUseStart>(m))
            CHECK(s->name.value == "read");
    for (const auto& m : msgs)
        if (const auto* f = get_leaf<StreamFinished>(m))
            CHECK(f->stop_reason == StopReason::ToolUse);
}

// Prose 语句 之前 a JSON 对象:一旦检测到传言,则打捞就禁用。
// 这样做可以防止像"int main() {"或类似代码上的假阳性.
// 如果一个模型发出传言,然后是工具 调用 ,则 JSON 以文本显示。
TEST_CASE("test_sse_prose_then_tool_call") {
    std::string sse =
        "data: {\"choices\":[{\"delta\":{\"content\":\"Let me check.\\n\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"{\\\"name\\\": \\\"read\\\", \\\"arguments\\\": {}}\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
        "data: [DONE]\n\n";
    auto msgs = oai::parse_sse_for_test(sse, {"read"});
    // 传言后, 救生功能被禁用, 因此没有释放调用 工具。
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 0);
    // 散文和JSON都被冲成文本.
    auto text = joined_text(msgs);
    CHECK(text.find("Let me check") != std::string::npos);
    CHECK(text.find("read") != std::string::npos);
}

// 工具 调用 的矩阵 : [{},{...}] 应该释放多个工具。
TEST_CASE("test_sse_salvage_array_of_calls") {
    std::string sse =
        "data: {\"choices\":[{\"delta\":{\"content\":"
            "\"[{\\\"name\\\": \\\"read\\\", \\\"arguments\\\": {}}, "
            "{\\\"name\\\": \\\"write\\\", \\\"arguments\\\": {}}]\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
        "data: [DONE]\n\n";
    auto msgs = oai::parse_sse_for_test(sse, {"read", "write"});
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 2);
    CHECK(count_leaf<StreamToolUseEnd>(msgs) == 2);
    CHECK(joined_text(msgs).empty());
    // 验证不同的身份
    std::vector<std::string> ids;
    for (const auto& m : msgs)
        if (const auto* s = get_leaf<StreamToolUseStart>(m))
            ids.push_back(s->id.value);
    CHECK(ids.size() == 2 && ids[0] != ids[1]);
}

// 多个分离 JSON 对象顺序( 2个) 调用 ,而不是数组)。
TEST_CASE("test_sse_salvage_two_sequential_calls") {
    std::string sse =
        "data: {\"choices\":[{\"delta\":{\"content\":"
            "\"{\\\"name\\\": \\\"read\\\", \\\"arguments\\\": {}}\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"\\n\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":"
            "\"{\\\"name\\\": \\\"write\\\", \\\"arguments\\\": {}}\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
        "data: [DONE]\n\n";
    auto msgs = oai::parse_sse_for_test(sse, {"read", "write"});
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 2);
    CHECK(count_leaf<StreamToolUseEnd>(msgs) == 2);
}

// 一些模型使用 "function" 改为 "name" 用于工具名密钥。
TEST_CASE("test_sse_salvage_function_key") {
    std::string sse =
        "data: {\"choices\":[{\"delta\":{\"content\":"
            "\"{\\\"function\\\": \\\"echo\\\", \\\"arguments\\\": "
            "{\\\"text\\\": \\\"Hi there!\\\"}}\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
        "data: [DONE]\n\n";
    auto msgs = oai::parse_sse_for_test(sse, {"echo", "read"});
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 1);
    CHECK(count_leaf<StreamToolUseEnd>(msgs) == 1);
    CHECK(joined_text(msgs).empty());
    for (const auto& m : msgs)
        if (const auto* s = get_leaf<StreamToolUseStart>(m))
            CHECK(s->name.value == "echo");
    CHECK(joined_tool_args(msgs) == std::string{"{\"text\":\"Hi there!\"}"});
    for (const auto& m : msgs)
        if (const auto* f = get_leaf<StreamFinished>(m))
            CHECK(f->stop_reason == StopReason::ToolUse);
}

// 漏出的内存工具(remember/forget/wipe_memory)
// 必须在传输层上是SWALLOWED:没有卡片永远没有诞生(
// 我们从未主动自动-run内存工具,闪存-then-delete卡坏UX).
// JSON被消耗(不是表面作为散文),转弯结束没有工具调用.
TEST_CASE("test_sse_salvage_memory_tool_swallowed") {
    std::string sse =
        "data: {\"choices\":[{\"delta\":{\"content\":"
            "\"{\\\"name\\\": \\\"remember\\\", \\\"arguments\\\": "
            "{\\\"text\\\": \\\"a fact\\\"}}\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
        "data: [DONE]\n\n";
    auto msgs = oai::parse_sse_for_test(sse, {"remember", "read"});
    // 没有工具卡,JSON从不以传言的形式浮出水面.
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 0);
    CHECK(joined_text(msgs).find("remember") == std::string::npos);
    CHECK(joined_text(msgs).find('{') == std::string::npos);
}

// 同样的泄漏 -content 形状是一个合法工具 调用
// 当最新的用户明确要求记住某件事时. Weak/local
// 模型往往缺乏一个工作结构化的工具通道;吞噬这个路径
// 使记忆完全失败了.
TEST_CASE("test_sse_salvage_memory_tool_explicit_request") {
    std::string sse =
        "data: {\"choices\":[{\"delta\":{\"content\":"
            "\"{\\\"name\\\": \\\"remember\\\", \\\"arguments\\\": "
            "{\\\"text\\\": \\\"a fact\\\"}}\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
        "data: [DONE]\n\n";
    auto msgs = oai::parse_sse_for_test(sse, {"remember", "read"},
                                        /*allow_memory_salvage=*/true);
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 1);
    CHECK(count_leaf<StreamToolUseEnd>(msgs) == 1);
    CHECK(joined_tool_args(msgs) == std::string{"{\"text\":\"a fact\"}"});
    for (const auto& m : msgs)
        if (const auto* s = get_leaf<StreamToolUseStart>(m))
            CHECK(s->name.value == "remember");
}

// 漏报 `skill` 调用 (元数据) -tool 弱模型幻觉从 目录 块块,
// 在问候中——{ "name":"skill","arguments":{"name"
// :...)必须是 SWALLOWED 从未被处决 冲浪会孕育出一种 "skill not found" 牌,
// 然后循环。 JSON 被消耗,而不是显示为散文。
TEST_CASE("test_sse_salvage_skill_swallowed") {
    std::string sse =
        "data: {\"choices\":[{\"delta\":{\"content\":"
            "\"{\\\"name\\\": \\\"skill\\\", \\\"arguments\\\": "
            "{\\\"name\\\": \\\"greeting\\\"}}\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
        "data: [DONE]\n\n";
    auto msgs = oai::parse_sse_for_test(sse, {"skill", "read"});
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 0);
    CHECK(joined_text(msgs).find("skill") == std::string::npos);
    CHECK(joined_text(msgs).find('{') == std::string::npos);
}

// ── 原生的。 Ollama /api/chat (NDJSON 路径 ────────────────────────────────────

// 清净的问候:内容 数据流 作为纯文本,没有工具 调用.
TEST_CASE("test_ndjson_plain_greeting") {
    std::string nd =
        "{\"message\":{\"role\":\"assistant\",\"content\":\"Hello! \"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\"How can I help?\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\"\"},"
            "\"done\":true,\"done_reason\":\"stop\","
            "\"prompt_eval_count\":10,\"eval_count\":5}\n";
    auto msgs = oai::parse_ndjson_for_test(nd, {"read", "remember"});
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 0);
    CHECK(joined_text(msgs) == std::string{"Hello! How can I help?"});
    for (const auto& m : msgs)
        if (const auto* f = get_leaf<StreamFinished>(m))
            CHECK(f->stop_reason == StopReason::EndTurn);
}

// 结构化本土 tool_calls (function.arguments 作为对象)成为真正
// 的工具 调用 带 call_native_ 编号.
TEST_CASE("test_ndjson_structured_tool_call") {
    std::string nd =
        "{\"message\":{\"role\":\"assistant\",\"content\":\"\","
            "\"tool_calls\":[{\"function\":{\"name\":\"read\","
            "\"arguments\":{\"path\":\"/etc/hostname\"}}}]}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\"\"},"
            "\"done\":true,\"done_reason\":\"stop\"}\n";
    auto msgs = oai::parse_ndjson_for_test(nd, {"read"});
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 1);
    for (const auto& m : msgs)
        if (const auto* s = get_leaf<StreamToolUseStart>(m)) {
            CHECK(s->name.value == "read");
            CHECK(std::string_view{s->id.value}.starts_with("call_native_"));
        }
    CHECK(joined_tool_args(msgs) == std::string{"{\"path\":\"/etc/hostname\"}"});
    for (const auto& m : msgs)
        if (const auto* f = get_leaf<StreamFinished>(m))
            CHECK(f->stop_reason == StopReason::ToolUse);
}

// 该 回归: qwen2.5 -coder 漏出一个赤裸的{ "name" :.}工具 调用 变成
// 本地 message.content (其输出不匹配) Ollama's <tool_call>
// 模板包裹，因此服务器把它留在 content 中。必须尝试恢复。
// 成为真正的工具 调用 所以这个工具实际上运行着... 不 显示为原始 JSON.
TEST_CASE("test_ndjson_leaked_content_salvaged") {
    std::string nd =
        "{\"message\":{\"role\":\"assistant\",\"content\":"
            "\"{\\\"name\\\": \\\"read\\\", \\\"arguments\\\": "
            "{\\\"path\\\": \\\"/etc/hostname\\\"}}\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\"\"},"
            "\"done\":true,\"done_reason\":\"stop\"}\n";
    auto msgs = oai::parse_ndjson_for_test(nd, {"read"});
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 1);
    CHECK(joined_text(msgs).empty());          // 从未显示为原始 JSON
    for (const auto& m : msgs)
        if (const auto* s = get_leaf<StreamToolUseStart>(m)) {
            CHECK(s->name.value == "read");
            CHECK(std::string_view{s->id.value}.starts_with("call_salvaged_"));
        }
    for (const auto& m : msgs)
        if (const auto* f = get_leaf<StreamFinished>(m))
            CHECK(f->stop_reason == StopReason::ToolUse);
}

// 纯粹的传言,只是提到 JSON 谓 不 抢救完毕
TEST_CASE("test_ndjson_prose_not_salvaged") {
    std::string nd =
        "{\"message\":{\"role\":\"assistant\",\"content\":"
            "\"Sure, here is what I think about your question.\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\"\"},"
            "\"done\":true}\n";
    auto msgs = oai::parse_ndjson_for_test(nd, {"read", "remember"});
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 0);
    CHECK(!joined_text(msgs).empty());
}

// 带语言标签的 Markdown 围栏，例如 ```cpp、```python，必须正常流式输出。
// 马上, 不 将用作可能的工具 -call JSON。这是一个错误 :
// 里面有代码块的模型会冻结 数据流.
TEST_CASE("test_sse_markdown_code_fence_not_held") {
    // 内部包含大括号的 ```cpp 代码块。
    std::string sse =
        "data: {\"choices\":[{\"delta\":{\"content\":\"```cpp\\n\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"int main() {\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"\\n  return 0;\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"\\n}\\n```\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
        "data: [DONE]\n\n";
    auto msgs = oai::parse_sse_for_test(sse, {"read"});
    // 应否 不 将 held/salvaged 作为一种工具 调用.
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 0);
    // 所有文字应立即冲洗成散文.
    auto text = joined_text(msgs);
    CHECK(text.find("```cpp") != std::string::npos);
    CHECK(text.find("int main()") != std::string::npos);
    CHECK(text.find("return 0") != std::string::npos);
}

// 回归：qwen2.5-coder:14b 将工具调用包在 ```json 围栏中。
// SIMPLE TEST : 全部内容为块。
TEST_CASE("test_ndjson_fenced_tool_call_simple") {
    std::string nd =
        "{\"message\":{\"role\":\"assistant\",\"content\":"
        "\"```json\\n{\\n  \\\"name\\\": \\\"read\\\",\\n  \\\"arguments\\\": {\\n    \\\"path\\\": \\\"/tmp/test\\\"\\n  }\\n}\\n```\""
        "},\"done\":true}\n";
    auto msgs = oai::parse_ndjson_for_test(nd, {"read"});
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 1);
}

// 回归：qwen2.5-coder:14b 将工具调用包在 ```json 围栏中，
// 途经 流式传输 符号,每个符号分别到达.
[[maybe_unused]] static void test_ndjson_fenced_tool_call_streaming() {
    // 模拟实际 流式传输 从 qwen2.5 调出 -coder:14b
    std::string nd =
        "{\"message\":{\"role\":\"assistant\",\"content\":\"```\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\"json\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\"\\n\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\"{\\n\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\" \"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\" \\\"\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\"name\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\"\\\":\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\" \\\"\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\"read\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\"\\\",\\n\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\" \"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\" \\\"\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\"arguments\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\"\\\":\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\" {\\n\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\"   \"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\" \\\"\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\"path\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\"\\\":\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\" \\\"\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\"/tmp/test\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\"\\\"\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\"\\n\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\" \"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\" }\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\"\\n\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\"}\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\"\\n\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\"```\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\"\"},\"done\":true}\n";
    auto msgs = oai::parse_ndjson_for_test(nd, {"read"});
    // 除错: 打印所有信件
    std::printf("\n=== test_ndjson_fenced_tool_call_streaming ===\n");
    std::printf("Total msgs: %zu\n", msgs.size());
    for (const auto& m : msgs) {
        if (auto* sm = std::get_if<msg::StreamMsg>(&m)) {
            std::visit([](const auto& inner) {
                using T = std::decay_t<decltype(inner)>;
                if constexpr (std::is_same_v<T, StreamToolUseStart>) {
                    std::printf("  ToolUseStart: %s\n", inner.name.value.c_str());
                } else if constexpr (std::is_same_v<T, StreamTextDelta>) {
                    std::printf("  TextDelta: '%s'\n", inner.text.c_str());
                } else if constexpr (std::is_same_v<T, StreamFinished>) {
                    std::printf("  Finished\n");
                } else if constexpr (std::is_same_v<T, StreamToolUseDelta>) {
                    std::printf("  ToolUseDelta\n");
                } else if constexpr (std::is_same_v<T, StreamToolUseEnd>) {
                    std::printf("  ToolUseEnd\n");
                }
            }, *sm);
        }
    }
    // 应该作为工具来拯救 调用, 不 显示为原始 JSON.
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 1);
    // 不应看到原始的JSON文本。
    auto text = joined_text(msgs);
    CHECK(text.find("read") == std::string::npos);
    CHECK(text.find('{') == std::string::npos);
}

TEST_CASE("test_ndjson_empty_object_response") {
    // qwen2.5-coder:14b 工具通过时输出 QQ
    auto msgs = oai::parse_ndjson_for_test(
        R"({"message":{"role":"assistant","content":"{}"},"done":false}
{"message":{"role":"assistant","content":""},"done":true,"done_reason":"stop"}
)", {"read", "write"});
    
    // 将 XQ 冲为文本, 不显示 "unparseable"
    bool found_braces = false;
    bool found_unparseable = false;
    for (auto& m : msgs) {
        if (auto* td = get_leaf<StreamTextDelta>(m)) {
            if (td->text == "{}") found_braces = true;
            if (td->text.find("unparseable") != std::string::npos) found_unparseable = true;
        }
    }
    CHECK(found_braces);
    CHECK(!found_unparseable);
}

// ── 字节-identity 警卫 ────────────────────────────────────────────────
// 扣紧 EXACT 产出 build_messages () 和准确性 Msg 序列编号
// parse_sse_for_test () 超过特性 -rich 输入,因此 OpenAI 传输
// 层 可重构( 3) e.g 中,将“拆分为模块”),但证明没有 -drift 保证——与纪律相同
// Anthropic wire_golden / anthropic_sse_golden 测试。
static std::uint64_t fnv1a(const std::string& s) {
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    return h;
}

TEST_CASE("test_wire_body_golden") {
    namespace oai = agentty::provider::openai;
    using agentty::Message; using agentty::Role; using agentty::Thread;
    using agentty::ThreadId; using agentty::ToolCallId; using agentty::ToolName;
    using agentty::ToolUse;

    auto done = [](const char* id, const char* name, nlohmann::json args,
                   const std::string& out) {
        ToolUse tc; tc.id = ToolCallId{id}; tc.name = ToolName{name};
        tc.args = std::move(args); tc.status = ToolUse::Done{{}, {}, out};
        return tc;
    };

    std::vector<Message> msgs;
    Message u; u.role = Role::User; u.text = "do the thing"; msgs.push_back(u);
    // 改为foo.cpp(后经编辑取代)
    { Message a; a.role = Role::Assistant; a.text = "reading";
      a.tool_calls.push_back(done("call_r1", "read",
          nlohmann::json{{"path", "src/foo.cpp"}},
          std::string("FIRST_") + std::string(4096, 'a')));
      msgs.push_back(std::move(a)); }
    // 大胶囊( 已经足够老了)
    { Message a; a.role = Role::Assistant; a.text = "grepping";
      a.tool_calls.push_back(done("call_g1", "grep",
          nlohmann::json{{"pattern", "x"}},
          std::string("GHEAD_") + std::string(80 * 1024, 'g')));
      msgs.push_back(std::move(a)); }
    // 编辑 foo.cpp (上层) call_r1)
    { Message a; a.role = Role::Assistant; a.text = "editing";
      a.tool_calls.push_back(done("call_e1", "edit",
          nlohmann::json{{"path", "src/foo.cpp"}}, "ok"));
      msgs.push_back(std::move(a)); }
    // 最新数据:关于 -read foo.cpp (现场副本)
    { Message a; a.role = Role::Assistant; a.text = "re-reading";
      a.tool_calls.push_back(done("call_r2", "read",
          nlohmann::json{{"path", "src/foo.cpp"}},
          std::string("SECOND_") + std::string(4096, 'b')));
      msgs.push_back(std::move(a)); }

    Thread t{ThreadId{"g"}, "", std::move(msgs), {}, {}};
    const std::string wire = oai::build_messages(t).dump();

    CHECK(wire.find("FIRST_") == std::string::npos);      // 已替换的读作已崩溃
    CHECK(wire.find("content was dropped to save context") != std::string::npos);  // 显示的指针文本
    CHECK(wire.find("SECOND_") != std::string::npos);     // 现场阅读全

    constexpr std::uint64_t kGolden = 0x5cbfe9a39e038624ull;
    const std::uint64_t got = fnv1a(wire);
    if (kGolden == 0)
        std::fprintf(stderr, "OPENAI wire golden = 0x%016llxull (len=%zu)\n",
                     static_cast<unsigned long long>(got), wire.size());
    else
        CHECK(got == kGolden);
}

TEST_CASE("test_sse_named_error_event_llamacpp") {
    // llama.cpp 数据流 失败作为 NAMED SSE 事件 下级 HTTP 200:
    //   event: error
    //   data: {"code":500,"message":"...","type":"server_error"}
    // 旧解析器完全忽略事件名称，只识别
    // {"error":...} wrapper — the frame was silently dropped, the stream
    // "closed clean", ensure_nonempty_turn fabricated "(empty response)",（连接正常关闭，ensure_nonempty_turn 却生成了空响应占位。）
    // 重试机制因此循环，形成用户报告的自定义主机死循环。
    std::string sse =
        "event: error\n"
        "data: {\"code\":500,\"message\":\"failed to apply chat template\","
        "\"type\":\"server_error\"}\n\n";
    auto msgs = oai::parse_sse_for_test(sse);

    // 只有一个 StreamError ,带信 和 数字代码为 http_status 所以,
    // 我们 状态更新函数 通过 带类型的 路径 (500 ) – 具有
    // 实际预算的瞬态 – 不是字符串 -sniffed).
    int errors = 0;
    for (const auto& m : msgs)
        if (const auto* e = get_leaf<StreamError>(m)) {
            ++errors;
            CHECK(e->message == "failed to apply chat template");
            CHECK(e->http_status == 500);
        }
    CHECK(errors == 1);
    // 没有捏造 "(empty response)" 文本和无 StreamFinished
    // - 错误是 终端 事件.
    CHECK(joined_text(msgs).empty());
    CHECK(count_leaf<StreamFinished>(msgs) == 0);
}

TEST_CASE("test_sse_bare_error_object_llamacpp") {
    // 同样的失败,但作为一个 UNNAMED 数据 帧 带有赤白错误形状({ "code":..,
    // "message" :..}——没有{. "error" 包装。
    // llama.cpp 版本释放此信息; 它必须表面显示为 StreamError
    // 也不要被误认为是空洞的三角洲
    std::string sse =
        "data: {\"code\":400,\"message\":\"unknown model alias\","
        "\"type\":\"invalid_request_error\"}\n\n"
        "data: [DONE]\n\n";
    auto msgs = oai::parse_sse_for_test(sse);

    int errors = 0;
    for (const auto& m : msgs)
        if (const auto* e = get_leaf<StreamError>(m)) {
            ++errors;
            CHECK(e->message == "unknown model alias");
            CHECK(e->http_status == 400);   // → 带类型的 终端 ,从来没有再试
        }
    CHECK(errors == 1);
    CHECK(joined_text(msgs).empty());
}


TEST_CASE("test_sse_tool_name_split_across_deltas") {
    // 打开代码 # 24137: a OpenAI-compatible 网关
    // 分割 function.name 和它分裂争论的方式一样 "re" 那样
    // "ad" 费用 `read`。光谱本身的累积器( `name ??=`
    // ) 取第一个非 -null 绝不附图,故我们派遣了一个工具, `re` ,
    // 获得 "unknown tool" 三次,末日来临 -loop 断路器杀死了转弯。
    //
    // 这个名字是累积的,直到它解决了我们所宣传的工具;
    // 这个门使累积结束,而不会猜测预期会
    // 有多少碎片。
    std::string sse =
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
            "\"id\":\"c1\",\"type\":\"function\","
            "\"function\":{\"name\":\"re\",\"arguments\":\"\"}}]}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
            "\"function\":{\"name\":\"ad\",\"arguments\":\"\"}}]}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
            "\"function\":{\"arguments\":\"{\\\"path\\\":\\\"a.txt\\\"}\"}}]}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n\n"
        "data: [DONE]\n\n";

    auto msgs = oai::parse_sse_for_test(sse, {"read"});

    CHECK(count_leaf<StreamToolUseStart>(msgs) == 1);
    for (const auto& m : msgs)
        if (const auto* s = get_leaf<StreamToolUseStart>(m))
            CHECK(s->name.value == "read");        // 否 "re" 数据
    CHECK(joined_tool_args(msgs) == std::string{"{\"path\":\"a.txt\"}"});
}

TEST_CASE("test_sse_whole_tool_name_is_not_doubled") {
    // 另一方向,以及打破每个正常服务器的方向:
    // 一条网关 RESTATES 继续块上的全名
    // 不能累积 "readread"。
    // 只在 调用 并且只是针对一个与我们持有的 不同的碎片
    std::string sse =
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
            "\"id\":\"c1\",\"type\":\"function\","
            "\"function\":{\"name\":\"read\",\"arguments\":\"\"}}]}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
            "\"function\":{\"name\":\"read\","
            "\"arguments\":\"{\\\"path\\\":\\\"a.txt\\\"}\"}}]}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n\n"
        "data: [DONE]\n\n";

    auto msgs = oai::parse_sse_for_test(sse, {"read"});
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 1);
    for (const auto& m : msgs)
        if (const auto* s = get_leaf<StreamToolUseStart>(m))
            CHECK(s->name.value == "read");
}

TEST_CASE("test_sse_empty_name_on_continuation_keeps_the_name") {
    // A3S-Lab/Code #139: Azure 和
    // 几个当地代理人 -send `"name": ""` 指定它会
    // 擦除一个既定的名称和 下一个 打开空工具名失败。
    std::string sse =
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
            "\"id\":\"c1\",\"type\":\"function\","
            "\"function\":{\"name\":\"read\",\"arguments\":\"\"}}]}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
            "\"function\":{\"name\":\"\","
            "\"arguments\":\"{\\\"path\\\":\\\"a.txt\\\"}\"}}]}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n\n"
        "data: [DONE]\n\n";

    auto msgs = oai::parse_sse_for_test(sse, {"read"});
    for (const auto& m : msgs)
        if (const auto* s = get_leaf<StreamToolUseStart>(m))
            CHECK(s->name.value == "read");
    CHECK(joined_tool_args(msgs) == std::string{"{\"path\":\"a.txt\"}"});
}

TEST_CASE("test_sse_unresolvable_tool_name_still_reaches_the_model") {
    // 积聚门等待一个能够解决的名字。
    // 绝不 决心决不能消失——它必须作为
    // 失败者达到模式。 调用 而不是用工具结束
    // 的转弯 调用 却没能默默发生
    std::string sse =
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
            "\"id\":\"c1\",\"type\":\"function\","
            "\"function\":{\"name\":\"nosuchtool\",\"arguments\":\"{}\"}}]}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n\n"
        "data: [DONE]\n\n";

    auto msgs = oai::parse_sse_for_test(sse, {"read"});
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 1);
    CHECK(count_leaf<StreamToolUseEnd>(msgs) == 1);
    for (const auto& m : msgs)
        if (const auto* s = get_leaf<StreamToolUseStart>(m))
            CHECK(s->name.value == "nosuchtool");
}

TEST_CASE("test_sse_error_object_mid_stream_terminates") {
    // 供应商可以回答200, 数据流 几瓶三角酒,
    // 然后发射 `{"error": …}` 替换a finish_reason。
    // 该词 传输层 看见干净的200个和关闭的 数据流 ,所以只有解码器 事件
    // 转弯失败了
    //
    // 解码器一直处理这个形状;所缺少的是消费者.
    // subagenter roop的StreamError
    // 臂是空的,因此错误被解码并丢弃,转弯返回其部分
    // 文本作为成功的答案——一个没有显示出任何错误的
    // 截断的回复,这是对失败的请求最糟糕的读取.
    std::string sse =
        "data: {\"choices\":[{\"delta\":{\"content\":\"starting\"}}]}\n\n"
        "data: {\"error\":{\"message\":\"upstream exploded\","
            "\"type\":\"server_error\"}}\n\n"
        "data: [DONE]\n\n";

    auto msgs = oai::parse_sse_for_test(sse);

    // 错误被保留之前的内容——它确实已经到达。
    CHECK(joined_text(msgs).find("starting") != std::string::npos);

    // 错误被释放出来, 携带提供者自己的信息,而不是通用的
    // "stream failed"。
    bool saw = false;
    for (const auto& m : msgs)
        if (const auto* e = get_leaf<StreamError>(m)) {
            saw = true;
            CHECK(e->message.find("upstream exploded") != std::string::npos);
        }
    CHECK(saw);
}

// ── 本地服务器的现场探测器(opt-in) ────────────────────────────
//
// 问题49不是 解析器 单独一个虫子—— 解析器 变动由下列因素涵盖:
// context_window_test。这只涵盖一个结束 -to-end
// 运行可: list_models () 实际 REACHES 探测器,
// 用于局部终点,并且测量到 运行时 窗口覆盖一个已宣布的建筑。
//
// Opt-in因为它需要端口上的服务器.
// tests/fake_local_server.py服务于llama.cpp的server-context.cpp
// (get_res_model_info)和LM工作室的原产地API所核实的准确的载荷:
//
//   python3 tests/fake_local_server.py llama     8080 &
//   AGENTTY_LIVE_LOCAL_PORT=8080 AGENTTY_LIVE_LOCAL_MODE=llama ./agentty_tests
//   python3 tests/fake_local_server.py lmstudio  1234 &
//   AGENTTY_LIVE_LOCAL_PORT=1234 AGENTTY_LIVE_LOCAL_MODE=lmstudio ./agentty_tests
// 新增主机路径无论地址形式如何，都强制进行运行时探测。
// 这使功能无需额外设置开关：即使自行托管的服务器使用
// 看似公网的主机名，也会被自动识别。
//
// 使用回转服务器,但声称 FLAG 而不是地址 - 而不是地址, list_models
// 与 force_probe 一定要击中 运行时 即使是终点的路径
// is_local_endpoint () 本来会拒绝的。
TEST_CASE("live: force_probe asks even when the address says otherwise") {
    const char* port_s = std::getenv("AGENTTY_LIVE_LOCAL_PORT");
    if (!port_s) return;
    namespace oai = agentty::provider::openai;

    oai::Endpoint ep;
    ep.host        = "127.0.0.1";
    ep.port        = static_cast<std::uint16_t>(std::atoi(port_s));
    ep.path        = "/v1/chat/completions";
    ep.models_path = "/v1/models";
    ep.use_tls     = false;

    // 找找地址,找找看,找找找看 PUBLIC 从而,热度会下降,
    // 同时仍然会解决回转的积木。 -label 宿主被当成本地人,
    // 一个点点的不是——所以 "x.example.com" 由连接层
    // 绘制到127.0.0.1的图案就是我们需要的形状。我们不能伪造 DNS 来,
    // 所以,相反地, 两者 调用 DIFFER 他们问甚么?
    // 以武力, 运行时 线路被击中, 即使行已声明窗口( Name
    // any_unknown ⁇ 伪.
    //
    // 树枝在/ tmp/fp_check.py 日志每个路径;
    // 此选项表示 OBSERVABLE 结果是——武力绝不能返回
    // 一个较小的窗口,当行宣布无任何结果时,武力必须返回一个窗口。
    auto with    = oai::list_models(::agentty::IoAccess::grant(), auth::AuthHeader{}, ep, /*力=*/true);
    auto without = oai::list_models(::agentty::IoAccess::grant(), auth::AuthHeader{}, ep, /*力=*/false);
    REQUIRE(!with.empty());
    REQUIRE(with.size() == without.size());
    for (std::size_t i = 0; i < with.size(); ++i)
        CHECK(with[i].context_window >= without[i].context_window);
}

TEST_CASE("live: the Ollama row on a llama.cpp server reports its served window (#73)") {
    // python3 tests/fake_local_server.py llama131k 18434 &
    // AGENTTY_LIVE_OLLAMA_ROW_PORT=18434 ./agentty_tests
    const char* port_s = std::getenv("AGENTTY_LIVE_OLLAMA_ROW_PORT");
    if (!port_s) return;   // 未选择在

    namespace oai = agentty::provider::openai;
    // 真实的 Ollama 注册表 排,移到树枝端口: llama.cpp
    // 开始于 -c 131072号船在港口 Ollama 通常使用。
    auto ep = oai::Endpoint::from_spec("ollama");
    REQUIRE(ep.native_api);
    ep.host = "127.0.0.1";
    ep.port = static_cast<std::uint16_t>(std::atoi(port_s));

    CHECK_FALSE(oai::endpoint_speaks_native(::agentty::IoAccess::grant(), ep));
    CHECK(oai::endpoint_speaks_native_cached(ep) == std::optional<bool>{false});

    auto models = oai::list_models(::agentty::IoAccess::grant(), auth::AuthHeader{}, ep, false);
    REQUIRE(models.size() == 1u);
    CHECK(models[0].context_window == 131072);
}

TEST_CASE("live: a local server's RUNTIME window reaches ModelInfo") {
    const char* port_s = std::getenv("AGENTTY_LIVE_LOCAL_PORT");
    const char* mode   = std::getenv("AGENTTY_LIVE_LOCAL_MODE");
    if (!port_s || !mode) return;   // 未选择在

    namespace oai = agentty::provider::openai;
    oai::Endpoint ep;
    // AGENTTY_LIVE_LOCAL_HOST 使这一点出现在 NON-loopback 地址,
    // 即49号报告形状: lama -server 放在盒子上,拿着 GPU ,
    // 然后从 LAN.
    if (const char* h = std::getenv("AGENTTY_LIVE_LOCAL_HOST")) ep.host = h;
    else                                                        ep.host = "127.0.0.1";
    ep.port        = static_cast<std::uint16_t>(std::atoi(port_s));
    ep.path        = "/v1/chat/completions";
    ep.models_path = "/v1/models";
    ep.use_tls     = false;
    // 非 国 家 -empty 标签这样 目录 写在一块
    // 土地上 SCOPED 键( 键) ( "provider/model" ,
    // 这是产品的工作,而能力在下方的解析度。
    // 空的镜框静静地回到空钥上,将测试一个没有真正终点的路径。
    ep.label       = "local-test";

    // force_probe 镜像什么 ADD-HOST 流程确实:
    // 询问 运行时 路由,不管地址是什么样子,所以自己 -hosted
    // 公开服务器 -looking 检测到名称而不是配置。
    const bool force = std::getenv("AGENTTY_LIVE_FORCE_PROBE") != nullptr;
    auto models = oai::list_models(::agentty::IoAccess::grant(), auth::AuthHeader{}, ep, force);
    REQUIRE(!models.empty());

    const std::string m{mode};
    if (m == "real") {
        // 指着一个 真实 守护进程,不是支点,请注意 不变量 ,
        // 而不是固定的编号(窗口取决于模型和
        // OLLAMA_CONTEXT_LENGTH :无论我们报告什么,
        // 都必须是一个真实的数字,它必须是 LOADED 窗口从 /
        // api/ps 而不是更大的建筑设计 来自于/ api/show.
        //
        // 以0.34.2的量, qwen3:0.6b的量是4096和
        // 40960, 10x的差值, 选错的差值意味着每1个
        // 提示词 超过4k的服务器会默默地切除.
        bool any = false;
        for (const auto& mi : models) {
            if (mi.context_window <= 0) continue;
            any = true;
            INFO("model " << mi.id.value << " window " << mi.context_window);
            // 这个家族的建筑窗口是40960;已加载的面积要小得多。
            // 任何40960的作品都意味着我们读错了字
            // 段。
            CHECK(mi.context_window < 40960);
        }
        CHECK(any);
    } else if (m == "router") {
        // 拉玛 -server ROUTER 模式。空的/ 道具与假
        // 人答案 n_ctx 0,所以窗口只存在于/props?
        // model= <name> 这正是问题所在 以及一个光线探测器错过的
        //
        // 两个型号: qwen3-coder为已加载(32768),gemma3:27b为非.
        // 卸货的必须保留任何梯子 而不是继承
        // 已加载 模特儿的窗户
        REQUIRE(models.size() == 2u);
        const ModelInfo* loaded = nullptr;
        const ModelInfo* unloaded = nullptr;
        for (const auto& mi : models) {
            if (mi.id.value == "qwen3-coder") loaded = &mi;
            if (mi.id.value == "gemma3:27b")  unloaded = &mi;
        }
        REQUIRE(loaded != nullptr);
        REQUIRE(unloaded != nullptr);
        CHECK(loaded->context_window == 32768);
        // 不常住,所以没有测量:大小来自发射的ags 路
        // 由器列表中为此列出.
        CHECK(unloaded->context_window == 131072);

        // 一次有一个模型 现场探测器只回答 已加载 一次,
        // 从不 加载 任何事物本身。
        CHECK(oai::probe_loaded_window(::agentty::IoAccess::grant(), auth::AuthHeader{}, ep, "qwen3-coder") == 32768);
        CHECK(oai::probe_loaded_window(::agentty::IoAccess::grant(), auth::AuthHeader{}, ep, "gemma3:27b") == 0);
        CHECK(oai::probe_loaded_window(::agentty::IoAccess::grant(), auth::AuthHeader{}, ep, "qwen3-coder") == 32768);

        // 请求其他模型将其换入(router自动加载).
        {
            agentty::http::Request r;
            r.method    = agentty::http::HttpMethod::Post;
            r.host      = ep.host;
            r.port      = ep.port;
            r.path      = "/v1/chat/completions";
            r.plaintext = true;
            r.headers   = {{"content-type", "application/json"}};
            r.body      = R"({"model":"gemma3:27b","messages":[]})";
            auto resp = agentty::http::default_client(::agentty::IoAccess::grant()).send(r, {});
            REQUIRE(resp);
            REQUIRE(resp->status == 200);
        }
        CHECK(oai::probe_loaded_window(::agentty::IoAccess::grant(), auth::AuthHeader{}, ep, "gemma3:27b") == 131072);
        CHECK(oai::probe_loaded_window(::agentty::IoAccess::grant(), auth::AuthHeader{}, ep, "qwen3-coder") == 0);

        // ── 能力,不是窗口 ──────────────────────────────────
        // 与上面的窗口一样不对称,原因相同。在路由器上,每个模型都有其 自身 聊天模板,
        // 所以盖子是每个模型和赤裸/道具假人(作用: "router",
        // n_ctx 0,没有 chat_template_caps -
        // 看见了 server-models.cpp get_router_props
        // ) 描述其中无一.
        //
        // LOADED模型答案,因此其模板封顶被记录下来.
        using agentty::catalog_reasoning_for;
        CHECK(catalog_reasoning_for("qwen3-coder", ep.label) == 1);

        // 电话 UNLOADED 一种得到 -1,而不是 0。
        // 我们用自动加载=假的问法,所以路由器拒绝而不是产入
        // 一个过程——这是正确的交易,但它意味着我们什么也学不到。
        // `false` 打开
        // 此前请求得还不够强制。
        // 将会禁用支持它的模型上的推理,而用户无法从真实的答案中分辨出。
        // 留空运行符,让推断到模型 加载 之后,一个明白真理的人,
        CHECK(catalog_reasoning_for("gemma3:27b", ep.label) == -1);
    } else if (m == "litellm") {
        // 该 REGRESSION GUARD. /v1/models 128,000广告;
        // 代理广告的/ v1/model/info 宣布一项 过期 8192.
        // A DECLARATION 决不能缩小更大的广告窗口——
        // 只有 MEASUREMENT 可能( WindowProbe::measured
        // ) 倒着走会让那些不需要紧凑的谈话变得紧凑,
        // 没有人会把症状追溯到探测器身上。
        CHECK(models.front().context_window == 128000);
    } else if (m == "ollama") {
        // /api/tags 搭载了完全没有窗口,所以这行到达了0.
        // /api/ps 报告 LOADED context_length ,
        // 这是对守护进程分配内容的真正衡量。
        CHECK(models.front().context_window == 16384);
    } else if (m == "llama") {
        // meta.n_ctx = 8192(已使用), meta.n_ctx_train = 32768(上限)。
        // 在修复之前,这行没有广告,解决了200k
        // 的默认.
        CHECK(models.front().context_window == 8192);

        // 同一请求的另一半。 llama.cpp's /v1/models
        // 排队完全没有能力 所以在这之前 这里的每个模型都掉下来了
        // resolved_caps () 以推论推论a GGUF 文件名
        // ——如何使用 "no reasoning on llama.cpp"
        // 甚至在一个模型上也发生过 其模板支持它。
        //
        // /props answers,其答案为MEASUREMENT:llama.cpp在加载运行该模型自己的jinja模板,
        // 与探测器输入值相对应,diffs输出值(common/jinja/caps.cpp).
        // Single-model服务器,所以一个服务器
        // -wide的答案覆盖了它所服务的一个模型.
        using agentty::catalog_reasoning_for;
        CHECK(catalog_reasoning_for(models.front().id.value, ep.label) == 1);

        // supports_tool_calls 工具在树根上是真实的,所以工具是允许的。
        // 三号 -state 必须是未知的 -or-true 从来没有一个硬的假。
        CHECK(models.front().tools_allowed());
    } else {
        // /v1 声明 max_context_length 262144;
        // 土著 API 报告实例 已加载 16384. 体积较小的,量度的必须赢
        CHECK(models.front().context_window == 16384);

        // ── 能力,不是窗口 ──────────────────────────────────
        // /v1 shim没有能力对象,
        // 因此这一切都来自原生的/api/v1/models路线——同样的请求
        // 回答了上面的窗口.
        using agentty::catalog_reasoning_for;

        // 被宣布为 OBJECT ({allowed_options
        // 而不是赤裸的胸肌。这个形状是整个。 LM 工作室错误: 已
        // 测试的阅读器 is_boolean () 并跳过其他任何,
        // 所以这里的每个模型都经过 id 推论—— 一个热度大于主机命名,
        // 不能分类一个 `publisher/model` 键。
        // 理由由文件名决定。
        //
        // 这行也是 UNLOADED-adjacent 写作能力
        // 处于重要位置 上方 编号 loaded_instances
        // 早点 -continue ,因为能力属于 MODEL
        // 并且可以知道它卸下时, 不同于窗口,它属于实例。
        // 所以,我们... 选择面板 显示第一个表格之前的合适付价 加载.
        CHECK(catalog_reasoning_for("qwen/qwen3-coder-30b", ep.label) == 1);

        // 有个模型 NO `reasoning` 键获取
        // 无内容 ——不是宣布拒绝,这是蓄意的,
        // 与它看起来的相反。
        //
        // LM 工作室的能力对象是每个模型 由应用程序知道,
        // 和 "the key is missing" 涵盖两个
        // "this model cannot reason" 财务报告和已审计
        // 财务报表 "this build/model entry does not say" .
        // . 并不是同样的要求,而是把缺席视为困难 NO 将 POISON
        // 编号 -inference 下方为: a DeepSeek
        // -R1 列出的蒸馏器,没有键,则将固定为无 -reasoning
        // 沉默不是证据,你应当把声音空空地说出来,然后,你应当说出道理。
        CHECK(catalog_reasoning_for("mistralai/mistral-7b-instruct-v0.3",
                                    ep.label) == -1);

        // trained_for_tool_use 倒数第二行是虚假的,
        // 不得 成为坚硬的束缚者, -state 输入
        // ModelInfo::supports_tools。
        // 这是关于 TRAINING ,而不是服务器是否接受一个工具阵列,
        // 而一个默默地永远得不到工具的模型与一个仅仅对工具
        // 不好的模型是无法区分的.
        for (const auto& mi : models)
            CHECK(mi.tools_allowed());
    }
}
