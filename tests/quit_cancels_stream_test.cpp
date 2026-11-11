// quit_cancels_stream_test — ^C / 退出必须绊倒 -flight
// 转弯 取消 表示这样 流式传输 (或工具) 工作线程 立即解开路障,没有它 事件 循环退出但
// ~ BackgroundQueue 加入等待着 HTTP 工作线程 通知 取消 慢路(
// 下一个服务器) 字节 闲置/ 闲置 超时 —— —— .
// "^C doesn't quit during an animation" 绞刑 Esc/CancelStream
// 已经这样做了; 退出的这个针头也这样做了。

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "agtest.hpp"

#include "agentty/io/http.hpp"
#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/msg.hpp"

namespace A = agentty;
namespace D = agentty::app::detail;

namespace {
void install_stub_deps() {
    A::app::install_deps(A::app::Deps{
        .stream        = [](const maya::unlocked&, A::provider::Request, A::provider::EventSink) {},
        .save_thread   = [](const A::Thread&) {},
        .delete_thread = [](const A::ThreadId&) {},
        .load_threads  = [] { return std::vector<A::Thread>{}; },
        .load_thread   = [](const A::ThreadId&) { return std::optional<A::Thread>{}; },
        .load_settings = [] { return A::store::Settings{}; },
        .save_settings = [](const A::store::Settings&) {},
        .new_thread_id = [] { return A::ThreadId{"t"}; },
        .title_from    = [](std::string_view) { return std::string{}; },
        .auth          = {},
    });
}
} // 命名空间结束。

TEST_CASE("Quit cancels the in-flight stream token") {
    install_stub_deps();

    // 中场模特儿 -stream: 流式传输 带有活性 Ctx
    // 的相位 取消 符号—— 用于 加载动画 正在动画。
    A::Model m;
    A::Message assistant;
    assistant.role = A::Role::Assistant;
    m.d.current.messages.push_back(std::move(assistant));

    A::phase::Active active;
    auto token = std::make_shared<A::http::CancelToken>();
    active.cancel = token;
    m.s.phase = A::phase::Streaming{std::move(active)};

    check(!token->is_cancelled(), "precondition: token starts un-cancelled");

    // 分派 退出通过元a 状态更新函数.
    auto [next, cmd] = D::step(::agentty::app::detail::meta_update, std::move(m), A::Quit{});
    (void)next; (void)cmd;

    check(token->is_cancelled(),
          "Quit trips the active turn's cancel token so teardown doesn't block");
}

TEST_CASE("Quit with no active stream is safe") {
    install_stub_deps();
    // 闲置模式:无 active_ctx。退出时不能崩溃于空 取消 路径。
    A::Model m;
    auto [next, cmd] = D::step(::agentty::app::detail::meta_update, std::move(m), A::Quit{});
    (void)next; (void)cmd;
    check(true, "Quit is a no-op on the cancel token when nothing is in flight");
}
