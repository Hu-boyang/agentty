// openrouter_test - 电话 OpenRouter 扩展编码代理需要。
//
//   1. reasoning_details 数据流 以片块形式,按指数合并,然后回来
//      接下来的请求,所以推理模型 保持他们的思维链 跨
//      工具 调用 (Gemini 认为签字需要。
//   2. 绝不对Claude(拒绝球场),也绝不对另一个主机。
//   3. Claude 途经 OpenRouter 获得 缓存 点: 系统 提示词 1h,最新转弯。
//   4. 合理努力的用途 OpenRouter's `reasoning` 对象。
//   5. session_id 保持一个谈话 上游.

#include "agtest.hpp"

#include <nlohmann/json.hpp>

#include "agentty/provider/openai/transport.hpp"
#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/msg.hpp"

#include <string>
#include <variant>

using json = nlohmann::json;
using namespace agentty;
namespace oa = agentty::provider::openai;

namespace {

oa::Request openrouter(std::string model) {
    oa::Request r;
    r.model = std::move(model);
    r.system_prompt = "you are a coding agent";
    r.max_tokens = 4096;
    r.endpoint = oa::Endpoint::from_spec("openrouter");
    r.session_key = "thread-42";
    return r;
}

Message user(std::string t) { Message m; m.role = Role::User; m.text = std::move(t); return m; }

Message assistant_with_details(std::string details) {
    Message m;
    m.role = Role::Assistant;
    m.text = "checking";
    m.reasoning_encrypted = std::move(details);
    m.reasoning_site = "openrouter";
    return m;
}

const StreamReasoning* reasoning_in(const std::vector<Msg>& v) {
    for (const auto& m : v)
        if (const auto* s = std::get_if<msg::StreamMsg>(&m))
            if (const auto* r = std::get_if<StreamReasoning>(s)) return r;
    return nullptr;
}

const StreamToolUseEnd* tool_end_in(const std::vector<Msg>& v) {
    for (const auto& m : v)
        if (const auto* s = std::get_if<msg::StreamMsg>(&m))
            if (const auto* e = std::get_if<StreamToolUseEnd>(s)) return e;
    return nullptr;
}

}  // 命名空间结束。

TEST_CASE("openrouter: reasoning_details merge by index across deltas") {
    const std::string sse =
        R"(data: {"choices":[{"delta":{"reasoning_details":[{"type":"reasoning.text","index":0,"text":"first "}]}}]})" "\n\n"
        R"(data: {"choices":[{"delta":{"reasoning_details":[{"type":"reasoning.text","index":0,"text":"second","signature":"sig"}]}}]})" "\n\n"
        R"(data: {"choices":[{"delta":{"reasoning_details":[{"type":"reasoning.encrypted","index":1,"data":"AAA"}]}}]})" "\n\n"
        R"(data: {"choices":[{"delta":{"content":"done"},"finish_reason":"stop"}]})" "\n\n"
        "data: [DONE]\n\n";
    const auto out = oa::parse_sse_for_test(sse);
    const StreamReasoning* r = reasoning_in(out);
    REQUIRE(r != nullptr);
    CHECK(r->site == "openrouter");
    const json d = json::parse(r->encrypted);
    REQUIRE(d.size() == 2);
    CHECK(d[0]["text"] == "first second", "fragments of one detail join up");
    CHECK(d[0]["signature"] == "sig");
    CHECK(d[1]["data"] == "AAA");
}

TEST_CASE("openrouter: reasoning_details go back on the next request") {
    auto r = openrouter("google/gemini-3-pro");
    r.messages = {user("fix it"),
                  assistant_with_details(R"([{"type":"reasoning.text","index":0,"text":"t","signature":"s"}])"),
                  user("and now?")};
    const json b = oa::build_request_body(r);
    bool found = false;
    for (const auto& m : b["messages"])
        if (m.value("role", "") == "assistant" && m.contains("reasoning_details")) {
            found = true;
            CHECK(m["reasoning_details"][0]["signature"] == "s");
        }
    CHECK(found, "the thought signature is replayed");
}

TEST_CASE("openrouter: never replayed to Claude or to another host") {
    auto claude = openrouter("anthropic/claude-sonnet-5");
    claude.messages = {user("a"), assistant_with_details(R"([{"index":0,"text":"t"}])"), user("b")};
    for (const auto& m : oa::build_request_body(claude)["messages"])
        CHECK(!m.contains("reasoning_details"), "Claude rejects it as input");

    auto other = openrouter("x");
    other.endpoint = oa::Endpoint{"api.example.com", 443, "/v1/chat/completions", "/v1/models", true, "example"};
    other.messages = claude.messages;
    for (const auto& m : oa::build_request_body(other)["messages"])
        CHECK(!m.contains("reasoning_details"), "only OpenRouter minted it");
}

TEST_CASE("openrouter: Claude gets cache points, other models don't") {
    auto r = openrouter("anthropic/claude-sonnet-5");
    r.messages = {user("first"), Message{}, user("latest question")};
    r.messages[1].role = Role::Assistant;
    r.messages[1].text = "an answer";
    const json b = oa::build_request_body(r);
    const json& sys = b["messages"][0];
    REQUIRE(sys["content"].is_array());
    CHECK(sys["content"].back()["cache_control"]["ttl"] == "1h", "system prompt cached for an hour");
    const json& last = b["messages"].back();
    REQUIRE(last["content"].is_array());
    CHECK(last["content"].back()["cache_control"]["type"] == "ephemeral", "newest turn cached");

    auto gpt = openrouter("openai/gpt-6");
    gpt.messages = r.messages;
    CHECK(oa::build_request_body(gpt)["messages"][0]["content"].is_string(),
          "only Claude needs explicit cache points");
}

TEST_CASE("openrouter: effort as `reasoning`, plus a sticky session") {
    auto r = openrouter("deepseek/deepseek-v4");
    r.effort = "high";
    r.messages = {user("hi")};
    const json b = oa::build_request_body(r);
    CHECK(b["reasoning"]["effort"] == "high");
    CHECK(!b.contains("reasoning_effort"));
    CHECK(b["session_id"] == "thread-42");
}

TEST_CASE("copilot: only a typed message is a user turn") {
    Message tool_round; tool_round.role = Role::Assistant;
    CHECK(oa::initiator_for({user("fix it")}) == "user");
    CHECK(oa::initiator_for({user("fix it"), tool_round}) == "agent",
          "a tool-result round is the agent's, not a new premium request");
    Message note; note.role = Role::User; note.text = "summary"; note.is_compact_summary = true;
    CHECK(oa::initiator_for({note}) == "agent");
}

TEST_CASE("gemini: a tool call's thought signature comes off the stream") {
    const std::string sse =
        R"(data: {"choices":[{"delta":{"tool_calls":[{"index":0,"id":"call_1","type":"function",)"
        R"("extra_content":{"google":{"thought_signature":"SIG"}},)"
        R"("function":{"name":"read","arguments":"{\"path\":\"a\"}"}}]}}]})" "\n\n"
        R"(data: {"choices":[{"delta":{},"finish_reason":"tool_calls"}]})" "\n\n"
        "data: [DONE]\n\n";
    const auto out = oa::parse_sse_for_test(sse, {"read"});
    const StreamToolUseEnd* e = tool_end_in(out);
    REQUIRE(e != nullptr);
    CHECK(e->extra_content["google"]["thought_signature"] == "SIG");
}

TEST_CASE("gemini: the signature goes back on the same call next turn") {
    oa::Request r;
    r.model = "gemini-3-pro";
    r.max_tokens = 4096;
    r.endpoint = oa::Endpoint::from_spec("gemini");
    Message a;
    a.role = Role::Assistant;
    ToolUse tc;
    tc.id = ToolCallId{"call_1"};
    tc.name = ToolName{"read"};
    tc.args = json{{"path", "a"}};
    tc.extra_content = json{{"google", {{"thought_signature", "SIG"}}}};
    tc.status = ToolUse::Done{{}, {}, "file contents"};
    a.tool_calls.push_back(tc);
    r.messages = {user("read a"), a, user("thanks")};
    const json b = oa::build_request_body(r);
    bool found = false;
    for (const auto& m : b["messages"])
        if (m.contains("tool_calls"))
            for (const auto& c : m["tool_calls"])
                if (c["id"] == "call_1") {
                    found = true;
                    CHECK(c["extra_content"]["google"]["thought_signature"] == "SIG");
                }
    CHECK(found);
}

// 整个路,没有任何东西,只是嘲笑网络:真实的 SSE
// 解析器's 事件 经历真实的 数据流 状态更新函数 ,
// 然后从 线程 模型寄的就是回味
TEST_CASE("end to end: what the stream carried is in the next request") {
    const std::string sse =
        R"(data: {"choices":[{"delta":{"reasoning_details":[{"type":"reasoning.text","index":0,"text":"need to "}]}}]})" "\n\n"
        R"(data: {"choices":[{"delta":{"reasoning_details":[{"type":"reasoning.text","index":0,"text":"read it","signature":"RSIG"}]}}]})" "\n\n"
        R"(data: {"choices":[{"delta":{"tool_calls":[{"index":0,"id":"call_1","type":"function",)"
        R"("extra_content":{"google":{"thought_signature":"TSIG"}},)"
        R"("function":{"name":"read","arguments":"{\"path\":\"notes.txt\"}"}}]}}]})" "\n\n"
        R"(data: {"choices":[{"delta":{},"finish_reason":"tool_calls"}]})" "\n\n"
        "data: [DONE]\n\n";

    Model m;
    m.d.current.messages = {user("what's on line 2?")};
    Message live; live.role = Role::Assistant;
    m.d.current.messages.push_back(live);
    m.s.phase = phase::Streaming{phase::Active{}};

    for (auto& ev : oa::parse_sse_for_test(sse, {"read"}))
        if (auto* s = std::get_if<msg::StreamMsg>(&ev)) {
            // StreamFinished 将交给工具 分派 ;
            // 停止到已解决的消息, 下一个请求就是从那里建立起来的.
            if (std::holds_alternative<StreamFinished>(*s)) break;
            (void)app::detail::stream_update(m, std::move(*s));
        }

    const Message& turn = m.d.current.messages.back();
    REQUIRE(turn.tool_calls.size() == 1);
    CHECK(turn.tool_calls[0].extra_content["google"]["thought_signature"] == "TSIG");
    CHECK(turn.reasoning_site == "openrouter");

    // 该工具运行; 现在遵循-up请求.
    Message done = turn;
    done.tool_calls[0].status = ToolUse::Done{{}, {}, "alpha\nbeta\n"};
    auto r = openrouter("google/gemini-3-pro");
    r.messages = {user("what's on line 2?"), done};
    const json b = oa::build_request_body(r);
    const json* asst = nullptr;
    for (const auto& msg : b["messages"])
        if (msg.value("role", "") == "assistant") asst = &msg;
    REQUIRE(asst != nullptr);
    CHECK((*asst)["reasoning_details"][0]["text"] == "need to read it");
    CHECK((*asst)["reasoning_details"][0]["signature"] == "RSIG");
    CHECK((*asst)["tool_calls"][0]["extra_content"]["google"]["thought_signature"] == "TSIG");
}
