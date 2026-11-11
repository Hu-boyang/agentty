// subs_key 一定要包罗万象 subscribe() 读,没有别的。
//
// subscribe() 仅询问 Tick 计时器和登录 工作线程 输入通过
// on_key/on_paste ,读取直播模式( jaal D43 ),
// 所以输入状态不得 键中:如果是的话,每个键中键都会重现 -run
// subscribe() 白费了
#include "agtest.hpp"

#include "agentty/runtime/app/subscribe.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/panel/appearance.hpp"

using namespace agentty;

TEST_CASE("subs_key: stable when nothing subscribe() reads has changed") {
    Model m;
    CHECK(app::subs_key(m) == app::subs_key(m));

    const auto before = app::subs_key(m);
    m.s.status = "a status line";
    CHECK(before == app::subs_key(m));
}

TEST_CASE("subs_key: input state does not churn the key") {
    Model m;
    const auto before = app::subs_key(m);
    m.ui.composer.text = "hi";
    m.ui.panel.descend(ui::panel::Appearance{});
    CHECK_MESSAGE(before == app::subs_key(m),
                  "typing and opening panels must not re-run subscribe()");
}

TEST_CASE("subs_key: the Tick gate is part of the key") {
    Model m;
    const auto idle = app::subs_key(m);
    m.s.phase = phase::Streaming{phase::Active{}};
    REQUIRE(app::animation_demand(m));
    CHECK_MESSAGE(!(idle == app::subs_key(m)),
                  "starting a turn must arm the Tick timer");
}

TEST_CASE("subs_key: the login worker is part of the key") {
    Model m;
    const auto idle = app::subs_key(m);
    m.ui.login = ui::login::DeviceWaiting{
        .provider = "copilot", .provider_label = "GitHub Copilot",
        .attempt_id = 1,
    };
    CHECK_MESSAGE(!(idle == app::subs_key(m)),
                  "a waiting login must start its worker");
}
