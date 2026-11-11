// stream_clock_test.cpp —— 人均数 -stream 时间是从Ttft测量的。
//
// 帆船报告 PR 65号:统计数据精确,一个模型在付费提供者上,
// "balloon out of proportion" 打开
// OpenRouter 免费的等级,其中自动选择型号 重试 总是这样
// 他怀疑 PR 刚刚是一秒钟,老虫子 PR 恰巧能显眼
//
// finalize_turn 封条 ttft = first_delta_at - a - > 已启动。
// `started` 仅由 StreamStarted 事件 ,只有两个 传输层 家庭表示:
// Anthropic (message_start 和“回复编码”。 OpenAI-chat
// 传输层 -- OpenRouter 格罗克,米斯特拉尔, llama.cpp,
// LM 工作室 - 和 Ollama 绝不要允许任何人妄谈, `started` 持有所打印的值
// 整个 转弯,所以Ttft吸收:
//   * 每一个 重试's 退避 睡觉
//   * 每一个先前的子 -turn 一代人和每个工具 运行时 因为
//     后-tool 续作也从未重设时钟
//
// 固定的邮票 钟在 launch_stream ,每点 数据流 这些测试能推动
// 真正的 状态更新函数 与 NO StreamStarted , 一种方式,
// 一种 OpenAI-chat 传输层 行为,并坚持 ttft措施一 数据流.
#include <chrono>
#include <string>

#include "agentty/runtime/app/cmd_factory.hpp"
#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/msg.hpp"
#include "agentty/store/store.hpp"
#include "agtest.hpp"

using namespace agentty;
namespace detail = agentty::app::detail;
namespace cmd = agentty::app::cmd;
using Clock = std::chrono::steady_clock;

namespace {

void install_stub_deps() {
    app::install_deps(app::Deps{
        .stream        = [](const maya::unlocked&, provider::Request, provider::EventSink) {},
        .save_thread   = [](const Thread&) {},
        .delete_thread = [](const ThreadId&) {},
        .load_threads  = [] { return std::vector<Thread>{}; },
        .load_thread   = [](const ThreadId&) { return std::optional<Thread>{}; },
        .load_settings = [] { return store::Settings{}; },
        .save_settings = [](const store::Settings&) {},
        .new_thread_id = [] { return ThreadId{"t-clock"}; },
    });
}

// 每一个 归并 这里运行在明确的时间, 所以ttft出来准确。
// T0 即当 数据流 正在试验发射中。
constexpr Clock::time_point T0 = app::kTestEpoch + std::chrono::hours{1};
using std::chrono::milliseconds;

// 中场模特儿 -turn : 用户消息,一个助理占位符, 流式传输 相位,
// 其时钟已启动。 `ago` 之前的 T0 - 在同一转弯期的很长
// 一段时间里站在那里(一个先前的子) -turn 工具、工具、
// 工具 重试 退避 下一个 数据流 不得 继承。
Model streaming_since(Clock::duration ago) {
    Model m;
    m.now = T0;
    m.d.current.id = ThreadId{"t-clock"};
    Message u;
    u.role = Role::User;
    u.text = "go";
    m.d.current.messages.push_back(std::move(u));
    Message a;
    a.role = Role::Assistant;
    m.d.current.messages.push_back(std::move(a));

    phase::Active ctx;
    ctx.started       = T0 - ago;
    ctx.last_event_at = ctx.started;
    m.s.phase = phase::Streaming{std::move(ctx)};
    return m;
}

Model step(Model m, msg::StreamMsg sm, Clock::time_point at) {
    return app::update(std::move(m), Msg{std::move(sm)}, at).first;
}

// 封住转弯的路 真正的 数据流 显示: 一个三角洲。 `at` ,然后结束。
const Message::Telemetry& seal(Model& m, Clock::time_point at) {
    m = step(std::move(m), StreamTextDelta{"hello"}, at);
    m = step(std::move(m), StreamFinished{StopReason::EndTurn}, at);
    REQUIRE(m.d.current.messages.back().telemetry.has_value());
    return *m.d.current.messages.back().telemetry;
}

}  // 命名空间结束。

TEST_CASE("stream clock: a relaunch restarts ttft, with no StreamStarted") {
    install_stub_deps();

    // 转弯已经跑90秒了 数据流 发射 -- -- 重试 ,上一个子 -turn ,
    // 慢工具。一个 OpenAI-chat 传输层 然后发送三角洲 无需
    // a StreamStarted 事件.
    Model m = streaming_since(std::chrono::seconds{90});
    (void)cmd::launch_stream(m);   // 重新启动: 重试 员额 -tool ,在 T0

    const auto& t = seal(m, T0 + milliseconds{20});
    // 就是这样 数据流 等等,90年代的转弯已经用完了
    CHECK_MESSAGE(t.ttft_ms == 20,
                  "ttft must measure THIS stream, not the whole turn so far: "
                  "an OpenAI-chat transport never emits StreamStarted, so "
                  "launch_stream is the only thing that can restart the clock");
}

TEST_CASE("stream clock: without a relaunch the stale origin is what leaks") {
    install_stub_deps();

    // 负控,同样的模型,但直接把三角洲送到90号 -s-old 钟,
    // 每一次都这样 OpenAI-chat 子目录 -turn
    // 如果停止报告~90秒, 则该时钟正在新的地点重新启动,
    // 上面的测试需要重新进行 -reading.
    Model m = streaming_since(std::chrono::seconds{90});
    const auto& t = seal(m, T0);
    CHECK(t.ttft_ms == 90'000);
}

TEST_CASE("stream clock: StreamStarted still moves the origin forward") {
    install_stub_deps();

    // Anthropic 发射 StreamStarted 打开 message_start。两种重置的组合:
    // launch_stream 将请求印上 StreamStarted 内容
    // -stamps 提供方回答时。 ttft 将连接时间排除在外, 一如既往。
    Model m = streaming_since(std::chrono::seconds{90});
    (void)cmd::launch_stream(m);
    m = step(std::move(m), StreamStarted{}, T0 + milliseconds{30});

    const auto& t = seal(m, T0 + milliseconds{35});
    CHECK(t.ttft_ms == 5);   // 30毫秒前的 30毫米前的 3 毫秒前 StreamStarted 不包括
}
