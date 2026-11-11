// speculative_dispatch_args_test - 读了 -only
// 工具只有在其参数实际到达时才提前发出。
//
// ── 原因 ──────────────────────────────────────────────────────────────────
//
// StreamToolUseEnd 发送已读 -only, 权限-free 工具,
// 而不是等待, 立即 finalize_turn: a `read` 打开门,
// 打开门 "args parsed clean" 以 `!args.is_null()` ,
// 该 SALVAGED 参数集也满足了。
//
// salvage_args 切到一个字符串值内。切到一个字串值外的切到一个值后,
// 就被切到一个数位值后。 `"symbol": "dispatch_stream",`
// — 解析 变成一个恰巧缺失的整洁对象 `path`。通常路径处理如下:
// guard_truncated_tool_args 通知失踪
// 模式定义-required 田间,修理它能,和 finalize_turn
// 重试 编号 数据流 透明,但是 guard_truncated_tool_args
// 只看到一个工具 仍然 待处理 ,所以在这里发送跳过整个机制
// —— 完全读作 -only 工具此路径存在以加速。
//
// 矿产于近代的40 线程:3323 `read` 调用中的44个死亡为
// "[invalid args] path required",其中33个死亡时的标注
// 为`{}`. 至于读作自己的错误的模型,它重新读作-emits,所以它也是相同的调用;
// 真正的原因是传输切割的中-arguments.
//
// 固定在此: 工具未完成的参数 待处理 费用 finalize_turn
// 以拥有,完成的参数仍然可以快速获取路径。
#include <chrono>
#include <string>
#include <utility>

#include "agtest.hpp"

#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/msg.hpp"

namespace A = agentty;
namespace D = agentty::app::detail;

namespace {

A::Model apply(A::Model m, A::msg::StreamMsg event) {
    auto [next, cmd] = D::step(::agentty::app::detail::stream_update,
                               std::move(m), std::move(event));
    (void)cmd;
    return std::move(next);
}

// 中场模特儿 -stream ,在读数需要否的配置文件中
// 权限 —— 推测路径的配置是为之而建的.
A::Model streaming_model() {
    A::Model m;
    m.d.profile = A::Profile::Write;
    A::Message assistant;
    assistant.role = A::Role::Assistant;
    m.d.current.messages.push_back(std::move(assistant));
    A::phase::Active active;
    active.last_event_at = std::chrono::steady_clock::now();
    m.s.phase = A::phase::Streaming{std::move(active)};
    return m;
}

// 数据流 单曲 `read` 调用 谁的论点 字节 完全一样 `json`.
A::Model stream_read_call(std::string json) {
    auto m = streaming_model();
    const A::ToolCallId id{"tc_spec_1"};
    m = apply(std::move(m), A::StreamToolUseStart{id, A::ToolName{"read"}});
    m = apply(std::move(m), A::StreamToolUseDelta{id, std::move(json)});
    m = apply(std::move(m), A::StreamToolUseEnd{id});
    return m;
}

const A::ToolUse* only_call(const A::Model& m) {
    for (const auto& msg : m.d.current.messages)
        for (const auto& tc : msg.tool_calls)
            if (tc.id.value == "tc_spec_1") return &tc;
    return nullptr;
}

} // 命名空间结束。

TEST_CASE("speculative dispatch: complete args still take the fast path") {
    auto m = stream_read_call(R"({"path":"src/main.cpp","limit":40})");
    const auto* tc = only_call(m);
    REQUIRE(tc != nullptr);
    CHECK_MESSAGE(tc->is_running(),
                  "a clean read must still start while the model keeps typing");
    CHECK(tc->args.value("path", std::string{}) == "src/main.cpp");
}

TEST_CASE("speculative dispatch: args cut outside a string stay Pending") {
    // 确切的形状从 线程: display_description
    // 财务报告和已审计财务报表 符号 来了 `path` 切口在绳子
    // 外 所以打捞物会很快乐地关闭物体
    auto m = stream_read_call(
        R"({"display_description":"Read dispatch_stream","symbol":"dispatch_stream",)");
    const auto* tc = only_call(m);
    REQUIRE(tc != nullptr);
    CHECK_MESSAGE(!tc->is_running(),
                  "a read with no path can only fail -- dispatching it burns "
                  "the turn and blames the model");
    CHECK_MESSAGE(tc->is_pending(),
                  "left Pending so finalize_turn can repair, retry, then fail "
                  "with an honest message");
}

TEST_CASE("speculative dispatch: empty args stay Pending") {
    // 在44次地雷故障中,33次地雷故障有`{}`吨,但根本没有得到任何结果。
    auto m = stream_read_call("");
    const auto* tc = only_call(m);
    REQUIRE(tc != nullptr);
    CHECK_FALSE(tc->is_running());
}

TEST_CASE("speculative dispatch: a tool with no required args is unaffected") {
    // git_status 什么都不拿,所以你应该... `{}` 已经完全完成,
    // 而且不能不走快速的路。
    auto m = streaming_model();
    const A::ToolCallId id{"tc_spec_1"};
    m = apply(std::move(m), A::StreamToolUseStart{id, A::ToolName{"git_status"}});
    m = apply(std::move(m), A::StreamToolUseDelta{id, "{}"});
    m = apply(std::move(m), A::StreamToolUseEnd{id});

    const auto* tc = only_call(m);
    REQUIRE(tc != nullptr);
    CHECK_MESSAGE(tc->is_running(),
                  "an argumentless read-only tool keeps the fast path");
}
