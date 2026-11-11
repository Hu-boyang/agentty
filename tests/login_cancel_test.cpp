// 取消登录必须实际停止工作线程.
//
// 历史,因为这个bug的形状很容易被重新引入:
//
//   原为 工作线程 是个 Cmd::task 持有 shared_ptr
//   <atomic_bool> ,和 Esc 绊倒 标志。该词 jaal 迁移
//   (f643ec58) 正文 任务 stop_token 并停止阅读 标志
//   ——以推理为理由: jaal 移动符号
//   关闭程序或启动该工作的订阅消失时，都必须取消。
//   下半部分只是对一个 SOURCE。一个 -shot Cmd::task
//   没有 事件订阅 内核关闭时的信号火力 和别的地方 状态更新函数
//   继续设置 标志 没有人读,和 Esc 离开 工作线程 块
//   -polling 提供900个预算的其余部分。
//
// 修补就是形状 jaal 想要: 需要一个 工作线程 已键入 Sub::stream ,
// 和 HOLDING 该 WAITING STATE 运行它。 "close the modal"
// 财务报告和已审计财务报表 "cancel the worker" 停止做两个可以分开的东西,
// 它们都是一样的行为, 这些测试正好就是这个原因。
#include "agtest.hpp"

#include "agentty/runtime/app/subscribe.hpp"
#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/login.hpp"
#include "agentty/runtime/model.hpp"

using namespace agentty;

namespace {

// 几叶 事件订阅 这个模型需要。
[[nodiscard]] int leaves(const Sub& s) {
    int n = 0;
    s.for_each([&](const auto&) { ++n; });
    return n;
}

} // 命名空间结束。

TEST_CASE("login: the worker runs only while the modal is waiting") {
    Model m;
    const int idle = leaves(app::subscribe(m));

    // 进入等待状态时,要求再有一个来源:民意调查环路.
    m.ui.login = ui::login::DeviceWaiting{
        .provider = "copilot", .provider_label = "GitHub Copilot",
        .attempt_id = 1,
    };
    CHECK_MESSAGE(leaves(app::subscribe(m)) == idle + 1,
                  "a waiting device login must subscribe its worker");

    // Esc 放弃它,这就是火 正文's stop_token.
    auto [m2, _] = app::update(std::move(m), Msg{CloseLogin{}});
    CHECK(std::holds_alternative<ui::login::Closed>(m2.ui.login));
    CHECK_MESSAGE(leaves(app::subscribe(m2)) == idle,
                  "closing the modal must drop the worker's subscription — "
                  "otherwise it keeps polling the provider for 900 s");
}

TEST_CASE("login: ChatGPT waiting subscribes its own worker") {
    Model m;
    const int idle = leaves(app::subscribe(m));

    m.ui.login = ui::login::ChatGptWaiting{.attempt_id = 1};
    CHECK(leaves(app::subscribe(m)) == idle + 1);

    auto [m2, _] = app::update(std::move(m), Msg{CloseLogin{}});
    CHECK(leaves(app::subscribe(m2)) == idle);
}

// 关键在于何者为源其身份。新的尝试必须产生 不同 键,
// 或 jaal 就能保持旧的 工作线程 运行中,
// 新的尝试永远不会开始。
TEST_CASE("login: each attempt is a distinct subscription") {
    Model m;
    m.ui.login = ui::login::DeviceWaiting{
        .provider = "copilot", .provider_label = "GitHub Copilot",
        .attempt_id = 1,
    };
    const auto first = app::subs_key(m);

    m.ui.login = ui::login::DeviceWaiting{
        .provider = "copilot", .provider_label = "GitHub Copilot",
        .attempt_id = 2,
    };
    CHECK_MESSAGE(!(first == app::subs_key(m)),
                  "a newer attempt must replace the running worker");
}

// 登录可以在转动动画时打开. subscribe() 返回路径的
// 早期添加 Tick 计时器, 而登录源必须幸存它 —— 将它悄悄地
// 丢在那里。 取消 标志 -in 中点 -flight.
TEST_CASE("login: the worker survives an animating turn") {
    Model m;
    m.ui.login = ui::login::DeviceWaiting{
        .provider = "copilot", .provider_label = "GitHub Copilot",
        .attempt_id = 1,
    };
    const int quiet = leaves(app::subscribe(m));

    m.s.phase = phase::Streaming{phase::Active{}};
    REQUIRE(app::animation_demand(m));          // 早期的-return路径
    CHECK_MESSAGE(leaves(app::subscribe(m)) == quiet + 1,   // +1 = 勾选
                  "the login worker must still be subscribed while streaming");
}

// 守门人,迟到的 工作线程 不再损坏新登录。
// 值得不考虑如何锁定 取消 已经交付。
TEST_CASE("login: a stale attempt's result is ignored") {
    Model m;
    m.ui.login = ui::login::DeviceWaiting{
        .provider = "copilot", .provider_label = "GitHub Copilot",
        .attempt_id = 2,                        // 编号 当前 尝试
    };

    auto [m2, _] = app::update(std::move(m),
                               Msg{DeviceLoginDone{
                                   .provider = "copilot",
                                   .provider_label = "GitHub Copilot",
                                   .attempt_id = 1,       // STALE
                                   .error = std::nullopt,
                               }});

    auto* w = std::get_if<ui::login::DeviceWaiting>(&m2.ui.login);
    REQUIRE(w != nullptr);
    CHECK(w->attempt_id == 2);
}
