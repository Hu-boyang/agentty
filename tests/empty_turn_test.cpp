// empty_turn_test - 转弯产生 无内容 一定要这么说
//
// 该 BUG 此 PINS 从一个真实的 已保存 线程 (~/.agentty/threads):
//
//   建议7出=726 字节 =1150 ttft=3442 = 健康
//   转弯 = 9 出局 = 4 字节 ⁇   ⁇  ?
//   "stress test this sandbox" 建议11出* 字节 重复了,
//   同样的建议13出=2 字节 用户 QQ ttft QQ 带类型的 "?"
//
// 连续三个转弯 text_len=0, tool_calls=0,
// a VALID 思维障碍,签名,2-4计费输出符号,没有 ttft_ms
// 或 或 时 间 wire_bytes 完全——意思是没有内容 字节 曾经到过这里
// HTTP 那时是200,而 SSE 框架不错 -formed ,所以没有出现任何错误。
// 用户三次看到一个空泡,并报告为 "it just goes silent".
//
// 有两个独立的缺陷:
//
//   1. parse_stop_reason ( )无 `refusal` 手,手,手 手,手臂,手,手臂 手,臂,手,臂 手,胳膊,手,胳膊 Anthropic 文档
//      (docs.claude.com/en/api/handling-stop-reasons
//      安全下降的报告——200年中。 -stream ,通过 message_delta。
//      它通过未指明, 其内容如下: "wire said nothing"
//      如此的下降与干净的无法区分 end_turn.
//
//   2. 没有使助理空转。 OpenAI 传输层 已经
//      曾经 ensure_nonempty_turn () 自从Quen打捞工作以来;
//      Anthropic 路径从没有增长到相当。
//
// 警卫住在finalize_turn(update/stream.cpp)
// 而不是传输层,因为这是每个提供者的数据流聚集的地方
// ——所以一个安静的传输不能只沉默其中的一部分.

#include <string>

#include "agtest.hpp"

#include <maya/print.hpp>            // render_to_string
#include <maya/widget/turn.hpp>

#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/msg.hpp"

using namespace agentty;

namespace {

// 转过身来 终端 事件 并把助理消息递回来。
//
// 穿过 app::update 而不是戳着 Model ,
// 所以这个覆盖线条: 如果警卫被移动到某个地方 状态更新函数 无法到达,
// 此程序停止编译或停止通过。
Message settle_with(StopReason stop, const std::string& streamed = {},
                    bool with_tool_call = false, bool retry_spent = false) {
    Model m;
    m.d.current.id = ThreadId{"empty-turn"};

    Message u;
    u.role = Role::User;
    u.text = "stress test this sandbox";
    m.d.current.messages.push_back(std::move(u));

    Message a;
    a.role = Role::Assistant;
    a.streaming_text = streamed;
    if (with_tool_call) {
        ToolUse tc;
        tc.id   = ToolCallId{"call_1"};
        tc.name = ToolName{"bash"};
        a.tool_calls.push_back(std::move(tc));
    }
    m.d.current.messages.push_back(std::move(a));
    phase::Active act{};
    act.reasoning_starved_retry = retry_spent;
    m.s.phase = phase::Streaming{std::move(act)};

    auto [out, _] = app::update(std::move(m),
                                Msg{msg::StreamMsg{StreamFinished{stop}}});
    return out.d.current.messages.back();
}

bool has(std::string_view hay, std::string_view needle) {
    return hay.find(needle) != std::string_view::npos;
}

}  // 命名空间结束。

TEST_CASE("empty turn: a refusal says it was refused") {
    // 被举报的bug的确切形状:终端停止,没有文字,没有工具.
    const auto msg = settle_with(StopReason::Refusal);

    // 电话 一个 硬保证——永远不再是空泡.
    agtest::check(msg.error.has_value(),
                  "a refused turn renders something");
    // ...它命名了事业,因为 "declined" 财务报告和已审计财务报表
    // "crashed" 调用 用于用户完全不同的回复。
    agtest::check(has(*msg.error, "declined"),
                  "the refusal is described as a decline");

    // 该 CHANNEL IS 该 POINT.
    // agentty 自己的言语永远不能落到 `text` :
    // 这就是模型的声音。在那里他们会 渲染 作为模型名字的助理
    // 坚持这样下去 并按它所说的那样回放到模型的下一个转弯
    agtest::check(msg.text.empty(),
                  "the notice is NOT put in the model's mouth");
}

TEST_CASE("empty turn: every terminal stop is covered") {
    // 警卫不能特别 -case 只是我们碰巧撞到的原因
    // A 传输 安静地继续 任意 终端 停止是同一个错误。
    // MaxTokens 重试 第一次(下一次测试);此处 重试 已经用完了。
    for (const auto stop : {StopReason::Refusal,
                            StopReason::MaxTokens,
                            StopReason::ContextExceeded,
                            StopReason::EndTurn,
                            StopReason::StopSequence,
                            StopReason::Unspecified}) {
        const auto msg = settle_with(stop, {}, false, /*retry_spent=*/true);
        INFO("stop_reason: " << std::string{to_string(stop)});
        agtest::check(msg.error.has_value(),
                      "no terminal stop leaves a blank bubble");
        agtest::check(msg.text.empty(),
                      "no terminal stop forges assistant prose");
    }
}

TEST_CASE("empty turn: reasoning that ate the budget is retried once") {
    // 任何提供者:max_tokens,没有说出的意思是推理就全部用上了.
    // 转弯是re-sent, 以最低努力而不是错误。
    const auto msg = settle_with(StopReason::MaxTokens);
    agtest::check(!msg.error.has_value(), "first starve retries, no error yet");
    agtest::check(msg.text.empty(), "retry starts from a fresh placeholder");
}

TEST_CASE("empty turn: a tool call is NOT an empty turn") {
    // 虚假 -positive 使用一个叫作没有序言的工具的
    // 模型是正常的,而且非常常见 -- -- 盖章
    // "produced no output" 写在记录里会谎话
    // 写在一大部分健康转弯上
    const auto msg = settle_with(StopReason::ToolUse, /*流线=*/{},
                                 /*with_tool_call=*/true);
    agtest::check(!msg.error.has_value(),
                  "a silent tool call gets no error banner");
    agtest::check(msg.text.empty(),
                  "a silent tool call keeps its empty text");
}

TEST_CASE("empty turn: real text is never overwritten") {
    // 另一个假的 -positive : 警卫跑 之后
    // streaming_text 归并 输入文字,所以它必须观察折叠的结果,
    // 而不是预 -fold 如果它先跑,它就会在正常转弯时 盖上通知的印记
    const auto msg = settle_with(StopReason::EndTurn, "a real answer");
    agtest::check(msg.text == "a real answer",
                  "a turn with content is left exactly alone");
    agtest::check(!msg.error.has_value(),
                  "a turn with content gets no error banner");
}

TEST_CASE("empty turn: refusal survives the wire round-trip") {
    // 缺陷 (1)自行:在修复之前,这个解析到未指定,
    // 所以一个下降到状态更新函数时,穿着与干净的完成相同的面孔.
    agtest::check(parse_stop_reason("refusal") == StopReason::Refusal,
                  "refusal parses to its own value");
    agtest::check(parse_stop_reason("model_context_window_exceeded")
                      == StopReason::ContextExceeded,
                  "context-window overflow parses to its own value");
    // 圆 -trip ,所以该值 a 线程 持续可以读回。
    agtest::check(parse_stop_reason(to_string(StopReason::Refusal))
                      == StopReason::Refusal,
                  "refusal round-trips through to_string");
    // 未知的未来值必须保持未指明,而不是崩溃或化名到
    // 一个真实的原因上.
    agtest::check(parse_stop_reason("some_future_reason")
                      == StopReason::Unspecified,
                  "an unknown stop reason is Unspecified");
}

// ── 实际到达屏幕的是什么 ────────────────────────────────
//
// 上面的一切证明 通知的地盘在 `error` FIELD。
// 除了模型说话之外,没有一种方法可以证明用户可以分辨出来,
// 而模型说话是使用这个字段的全部要点。所以,画出真正的部件,
// 看看这个 字节 a 终端 将会收到。
TEST_CASE("empty turn render: the notice is marked, not spoken") {
    const auto msg = settle_with(StopReason::Refusal);
    REQUIRE(msg.error.has_value());

    maya::Turn::Config cfg;
    cfg.glyph = "\xe2\x9c\xa6";          // ✦
    cfg.label = "Opus 5";
    cfg.meta  = "12:34";
    cfg.error = *msg.error;
    const auto out = maya::render_to_string(maya::Turn{cfg}.build(), 100);

    // ⁇ 琌┮Τ ⁇ "agentty is telling you something" 发件人
    // "the model said this". Model 传言从不携带.
    agtest::check(out.find("\xe2\x9a\xa0") != std::string::npos,
                  "the notice paints behind a warning glyph");
    agtest::check(out.find("declined") != std::string::npos,
                  "the reason reaches the screen");
}
