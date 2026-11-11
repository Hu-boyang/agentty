// key_routing_test : 键盘按键的路由 当前 模型 模型 模式
//
// 多数测试 分派 Msg 数值直入 update() ,
// 它跳过决定输入路径的一半 对应的 处理函数 键到达。这些驱动它:
// 路径一个键通过 AgenttyApp::on (怎样?
// ) jaal 调用 ,与活模式一起,应用结果,路径下键.
//
// routing曾经是一个关闭在subscribe()所建的快照上的路由器,
// 一个改变哪个表面拥有键盘的电文留下了下一个键盘路由器与旧的(
// 按键直到再按一次才生效。
// jaal D43移除了路由器 ,因此这不能再去过期;
// 这些测试使它保持了这个方式.
#include "agtest.hpp"

#include <optional>
#include <vector>

#include "agentty/runtime/model.hpp"
#include "agentty/runtime/msg.hpp"
#include "agentty/runtime/app/program.hpp"
#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/app/subscribe.hpp"
#include "agentty/runtime/panel/appearance.hpp"
#include "agentty/runtime/app/deps.hpp"

using namespace agentty;
namespace pn = agentty::ui::panel;

namespace {

// 内核的一键: 程序为KeyEvent的on(),
// 以现在的模式调用。
std::optional<Msg> route(const Model& m, const maya::KeyEvent& ev) {
    return app::AgenttyApp::on(m, ev);
}

// 按键:与当前模型对接,然后应用其生产的任何
// 产品。
[[nodiscard]] bool press(Model& m, const maya::KeyEvent& ev) {
    auto msg = route(m, ev);
    if (!msg) return false;
    m = app::update(std::move(m), std::move(*msg)).first;
    return true;
}

// 通过 a 的逃逸路线 处理函数 达到 app::deps
// () (它坚持恢复的主题),所以必须安装支线,
// 即使这个测试只关心路由.
void install_stub_deps() {
    app::install_deps(app::Deps{
        .stream        = [](const maya::unlocked&, provider::Request, provider::EventSink) {},
        .save_thread   = [](const Thread&) {},
        .delete_thread = [](const ThreadId&) {},
        .load_threads  = [] { return std::vector<Thread>{}; },
        .load_thread   = [](const ThreadId&) { return std::optional<Thread>{}; },
        .load_settings = [] { return store::Settings{}; },
        .save_settings = [](const store::Settings&) {},
        .new_thread_id = [] { return ThreadId{}; },
        .title_from    = [](std::string_view t) { return std::string{t}; },
        .write_file    = [](const std::string&, const std::string&) {},
        .auth          = {},
    });
}

maya::KeyEvent special(maya::SpecialKey k) {
    maya::KeyEvent ev;
    ev.key = k;
    return ev;
}

} // 命名空间结束。

TEST_CASE("routing: the first arrow after Enter reaches the theme browser") {
    Model m;
    m = app::update(std::move(m), Msg{OpenAppearance{}}).first;

    // 步行到主题行中,与用户的方式一样,所以表单的焦点
    // 是真实而非假设的.
    const auto* pane = m.ui.panel.get<pn::Appearance>();
    REQUIRE(pane != nullptr);

    // 打开浏览器 Enter。这是面板 -CHANGING
    // 键: 之后的外观 面板 拥有不同模式的键盘。
    m = app::update(std::move(m), Msg{AppearancePickTheme{}}).first;
    const auto* opened = m.ui.panel.get<pn::Appearance>();
    REQUIRE(opened != nullptr);
    CHECK(opened->pane.picking, "Enter did not open the browser");

    const int idx_before    = opened->pane.picker.picker.index();
    const int cursor_before = opened->pane.form.cursor;

    // 该 ASSERTION : 1 QQ,通过从模型中构建的子
    // 程序进行路由 当前。它必须移动浏览器的亮度。
    // 如果 subscribe() 和预赛的路线 -Enter
    // 快照 它将将窗体光标移动到下面,用户将看到没有发生任何情况。
    CHECK(press(m, special(maya::SpecialKey::Down)),
          "the arrow produced no message at all");

    const auto* after = m.ui.panel.get<pn::Appearance>();
    REQUIRE(after != nullptr);
    CHECK(after->pane.picker.picker.index() != idx_before,
          "the FIRST arrow after Enter did not move the browser — this is "
          "the 'input doesn't register until you hit it again' bug");
    CHECK(after->pane.form.cursor == cursor_before,
          "the arrow leaked into the form behind the browser");
}

TEST_CASE("routing: Escape leaves the browser and the next key hits the form") {
    install_stub_deps();
    Model m;
    m = app::update(std::move(m), Msg{OpenAppearance{}}).first;
    m = app::update(std::move(m), Msg{AppearancePickTheme{}}).first;
    REQUIRE(m.ui.panel.get<pn::Appearance>() != nullptr);
    CHECK(m.ui.panel.get<pn::Appearance>()->pane.picking);

    // Esc 关闭浏览器—— 反向的相同过渡。 下一个密钥必须
    // 到达 FORM ,而不是一个不再上线的浏览器。
    CHECK(press(m, special(maya::SpecialKey::Escape)),
          "Escape produced no message");
    const auto* closed = m.ui.panel.get<pn::Appearance>();
    REQUIRE(closed != nullptr);
    CHECK(!closed->pane.picking, "Escape did not close the browser");

    const int idx_before    = closed->pane.picker.picker.index();
    const int cursor_before = closed->pane.form.cursor;

    CHECK(press(m, special(maya::SpecialKey::Down)),
          "the arrow after Escape produced no message");

    const auto* after = m.ui.panel.get<pn::Appearance>();
    REQUIRE(after != nullptr);
    CHECK(after->pane.form.cursor != cursor_before,
          "the first arrow after closing the browser did not move the form");
    CHECK(after->pane.picker.picker.index() == idx_before,
          "the arrow still reached the closed browser");
}

// ^A/^X 曾经用他们的武器门运送 被路由器俘虏
// 并失踪 SubsKey 他们死了,因为...
// 帧 编辑后 过期-live 排队完毕后, 正在读取现场模式。
TEST_CASE("routing: the changes-strip chords follow the live model") {
    install_stub_deps();
    Model m;
    m.d.show_changes_strip = true;
    const auto ctrl_x = [] {
        maya::KeyEvent k{};
        k.key = maya::CharKey{U'x'};
        k.mods.ctrl = true;
        return k;
    }();
    const auto is_reject = [](const std::optional<Msg>& msg) {
        const auto* dr = msg ? std::get_if<msg::DiffReviewMsg>(&*msg) : nullptr;
        return dr && std::holds_alternative<RejectAllChanges>(*dr);
    };

    CHECK_FALSE(is_reject(route(m, ctrl_x)));   // 无队列:未装入武器

    FileChange fc;
    fc.path = "src/main.cpp";
    m.d.pending_changes.push_back(fc);
    CHECK_MESSAGE(is_reject(route(m, ctrl_x)),
                  "queued changes arm ^X on the very next key");

    m.d.show_changes_strip = false;
    CHECK_MESSAGE(!is_reject(route(m, ctrl_x)),
                  "hiding the strip disarms ^X on the very next key");
}

// ^/ 是那个 字节 0x1F,与QQ相同. plain(英语:Plain). 终端 发送原始
// 字节 ; 小猫发送 ctrl+ '/' ; 带有扩展键的 tmux 转动 字节 转换成 ctrl
// + '_' 一旦应用程序要求 modifyOtherKeys。所有三个必须打开 选择面板.
TEST_CASE("routing: every way ^/ arrives opens the model picker") {
    install_stub_deps();
    auto key = [](char32_t c, bool ctrl) {
        maya::KeyEvent ev;
        ev.key = maya::CharKey{c};
        ev.mods.ctrl = ctrl;
        return ev;
    };
    for (const auto& ev : {key(0x1F, false), key(U'/', true), key(U'_', true)}) {
        Model m;
        REQUIRE(press(m, ev));
        CHECK(m.ui.panel.get<pn::Models>() != nullptr);

        // 在露天的天空里,我看见了 选择面板 处理相同的密钥(对提供方的镜),
        // 而不是 带类型的 进入过滤器。
        CHECK(route(m, ev).has_value());
        CHECK(press(m, ev));
        CHECK(m.ui.panel.get<pn::Models>() != nullptr);
    }
}
