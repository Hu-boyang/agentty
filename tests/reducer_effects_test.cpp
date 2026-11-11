// 状态更新函数 描述他们 副作用 ;这是证明它的测试形状.
//
// jaal设计博士的第二个成功标准:
//
//     状态更新测试的流程是：构造 Model，发送 Msg，断言新 Model 的状态，
//      并检查 expect_effect<save_settings>(1)。
//
// 之前,我说 持久化 成为 副作用 ,这是不可能的: 保存
// 发生了 内部 编号 状态更新函数 ,通过 Deps 接口边界
// 所以唯一能检查的方法就是安装一个假店 然后检查它 状态更新函数
// 自己的回报价值 没有提到它写了什么。
//
// 现在 Cmd IS。这些测试不会安装道具,也不会触摸磁盘。
#include "agtest.hpp"
#include "agtest_fx.hpp"

#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/model.hpp"

using namespace agentty;

TEST_CASE("effects: toggling a pref returns exactly one settings save") {
    Model m;

    auto [m2, cmd] = app::update(std::move(m), Msg{ToggleChangesStrip{}});

    // 模型变了...
    CHECK(m2.d.show_changes_strip == true);
    // ...还有手臂 SAID 改为 保存 一次就是一次 一次是一次是一次
    CHECK(agtest::fx::count<SaveSettings>(cmd) == 1);

    // 它写的东西会带去改变的——记录,而不是商店的
    // Re-read。
    const auto* e = agtest::fx::find<SaveSettings>(cmd);
    REQUIRE(e != nullptr);
    CHECK(e->settings.show_changes_strip == true);
}

TEST_CASE("effects: a reducer that changes nothing saves nothing") {
    Model m;
    // 将光标移动到一个 选择面板 纯洁 UI 州; 不得写。
    auto [m2, cmd] = app::update(std::move(m), Msg{ThreadListMove{+1}});
    CHECK(agtest::fx::count<SaveSettings>(cmd) == 0);
    CHECK(agtest::fx::count<SaveThread>(cmd) == 0);
}

TEST_CASE("effects: save_record describes the record it was handed") {
    Model m;
    m.d.persisted.ui.theme = "probe-theme";
    m.d.persisted.show_reasoning = true;

    const Cmd cmd = app::detail::save_record(m);

    const auto* e = agtest::fx::find<SaveSettings>(cmd);
    REQUIRE(e != nullptr);
    CHECK(e->settings.ui.theme == "probe-theme");
    CHECK(e->settings.show_reasoning == true);
}

// 退出臂是ORDER最重要的一个地方:jaal的翻译在第一次
// 退出时停止发送(`if (exit_) return`),
// 所以在它被默不作声地放下后,一个保存批量地被发送——这是保存
// 保持了最后一转和主动模式.
TEST_CASE("effects: Quit saves BEFORE it quits") {
    Model m;
    m.d.current.messages.push_back(Message{.role = Role::User, .text = "hi"});

    auto [m2, cmd] = app::update(std::move(m), Msg{Quit{}});

    bool seen_save = false;
    bool save_came_first = false;
    agtest::fx::for_each(cmd, [&](const auto& e) {
        using U = std::remove_cvref_t<decltype(e)>;
        if constexpr (std::same_as<U, SaveSettings> || std::same_as<U, SaveThread>)
            seen_save = true;
        else if constexpr (std::same_as<U, maya::payload_t<maya::fx::quit, Msg>>)
            save_came_first = seen_save;
    });
    CHECK_MESSAGE(seen_save, "Quit must persist the session");
    CHECK_MESSAGE(save_came_first,
                  "the save must be batched BEFORE quit or the interpreter "
                  "drops it");
}
