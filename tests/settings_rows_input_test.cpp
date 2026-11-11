// settings_rows_input_test ——每个设置上的每一个可编辑行 面板
// 通过真实的密钥路径获取输入( E) on_key - > 更新),而不仅仅是 状态更新函数.
//
// 每行: -place 类型( 切换, 选择, 滑块, 数字) 必须在
// Q 或 Q 上更改; 文本类型必须在 带类型的 字母上输入编辑,
// 将编辑保留在多个键上, 并保留在 Enter 上。 忽略所有字段
// 的行是死字段。

#include "agtest.hpp"

#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/app/cmd_factory.hpp"   // generate_thread_title
#include "agentty/runtime/app/program.hpp"   // AgenttyApp::visual_hash (无) 渲染 门)
#include "agentty/runtime/app/subscribe.hpp"
#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/panel/form.hpp"
#include "agentty/runtime/panel/rag.hpp"
#include "agentty/runtime/panel/settings/categories.hpp"
#include "agentty/runtime/panel/settings/items.hpp"
#include "agentty/runtime/panel/step.hpp"
#include "agentty/runtime/panel/thread_filter.hpp"
#include "agentty/runtime/panel/top.hpp"
#include "agentty/io/persistence.hpp"   // k 标签Max 字节
#include "agtest_fx.hpp"

#include <maya/terminal/input.hpp>

#include <functional>
#include <string>
#include <vector>

namespace pn = agentty::ui::panel;
using namespace agentty;

namespace {

store::Settings g_settings;

void install_stub_deps() {
    app::install_deps(app::Deps{
        .stream         = [](const maya::unlocked&, auto, auto) {},
        .save_thread    = [](const Thread&) {},
        .delete_thread  = [](const auto&) {},
        .load_threads   = [] { return std::vector<Thread>{}; },
        .load_thread    = [](const ThreadId&) { return std::optional<Thread>{}; },
        .load_settings  = [] { return g_settings; },
        .save_settings  = [](const store::Settings& s) { g_settings = s; },
        .new_thread_id  = [] { return ThreadId{"t-rows"}; },
        .title_from     = [](std::string_view t) { return std::string{t}; },
        .auth           = {},
    });
}

maya::KeyEvent sk(maya::SpecialKey k) { return maya::KeyEvent{k, {}, {}}; }
maya::KeyEvent ch(char32_t c) { return maya::KeyEvent{maya::CharKey{c}, {}, {}}; }

Model press(Model m, const maya::KeyEvent& ev) {
    if (auto msg = app::on_key(m, ev))
        m = app::update(std::move(m), std::move(*msg)).first;
    return m;
}

using FormOf = std::function<form::Form*(Model&)>;

std::string show(const form::FieldValue& v) {
    return std::visit([](const auto& x) -> std::string {
        using T = std::decay_t<decltype(x)>;
        if constexpr (std::is_same_v<T, form::field::Toggle>) return x.on ? "on" : "off";
        else if constexpr (std::is_same_v<T, form::field::Choice>) return std::to_string(x.index);
        else if constexpr (std::is_same_v<T, form::field::Number>) return std::to_string(form::number_value(x));
        else if constexpr (std::is_same_v<T, form::field::Slider>) return std::to_string(x.value);
        else if constexpr (requires { x.value; }) return std::string{x.value};
        else return "";
    }, v);
}

// 依次访问每行,每行都锻炼.
void exercise(const std::string pane, Msg open, const FormOf& form_of,
              const std::vector<std::string>& skip = {}, Model start = {}) {
    install_stub_deps();
    Model m = app::update(std::move(start), std::move(open)).first;
    form::Form* f = form_of(m);
    REQUIRE_MESSAGE(f != nullptr, pane);
    const auto kind = pn::top(m);
    int checked = 0;

    for (int at = 0; at < 200; ++at) {
        f = form_of(m);
        REQUIRE_MESSAGE(f != nullptr, pane << ": pane closed by a key");
        if (at >= static_cast<int>(f->fields.size())) break;
        if (f->fields[static_cast<std::size_t>(at)].is_header()) continue;
        f->cursor = at;
        const auto* row = f->focused();
        if (!row) break;
        const std::string id = row->id;

        const bool skipped = std::find(skip.begin(), skip.end(), id) != skip.end();
        if (row->is_pick() && !skipped) {
            // 手拿开 Enter : 键盘必须另加一个东西,
            // Esc 一定要回来 面板.
            m = press(std::move(m), sk(maya::SpecialKey::Enter));
            const bool moved = pn::top(m) != kind || [&] {
                auto* g = form_of(m);
                return g && g->choosing();
            }();
            bool sub_open = false;
            if (auto* a = m.ui.panel.get<pn::Appearance>()) sub_open = a->pane.picking;
            CHECK_MESSAGE((moved || sub_open), pane << " / " << id << ": Enter on a pick did nothing");
            for (int i = 0; i < 3 && pn::top(m) != kind; ++i)
                m = press(std::move(m), sk(maya::SpecialKey::Escape));
            if (auto* a = m.ui.panel.get<pn::Appearance>(); a && a->pane.picking)
                m = press(std::move(m), sk(maya::SpecialKey::Escape));
            REQUIRE_MESSAGE(pn::top(m) == kind, pane << " / " << id << ": Esc did not come back");
            ++checked;
            continue;
        }
        if (!row->editable())
            MESSAGE(pane << " / " << id << ": not editable ("
                    << (row->locked ? "locked: " + row->locked_reason
                        : row->is_pick() ? std::string{"pick"}
                        : row->is_action() ? std::string{"action"} : std::string{"?"}) << ")");
        if (row->editable() && !skipped) {
            const std::string before = show(row->value);
            if (row->is_text_like()) {
                // 类型 -to-edit 再来两把钥匙 Enter 离开编辑。
                m = press(std::move(m), ch(U'7'));
                m = press(std::move(m), ch(U'x'));
                m = press(std::move(m), sk(maya::SpecialKey::Backspace));
                f = form_of(m);
                REQUIRE(f != nullptr);
                CHECK_MESSAGE(f->editing(), pane << " / " << id << ": typing did not start an edit");
                m = press(std::move(m), sk(maya::SpecialKey::Enter));
                f = form_of(m);
                REQUIRE(f != nullptr);
                CHECK_MESSAGE(!f->editing(), pane << " / " << id << ": Enter did not end the edit");
                ++checked;
            } else {
                m = press(std::move(m), sk(maya::SpecialKey::Right));
                f = form_of(m);
                REQUIRE(f != nullptr);
                std::string after = f->find(id) ? show(f->find(id)->value) : before;
                if (after == before) {   // 已经最大值 : 尝试其他方式
                    m = press(std::move(m), sk(maya::SpecialKey::Left));
                    f = form_of(m);
                    REQUIRE(f != nullptr);
                    after = f->find(id) ? show(f->find(id)->value) : before;
                }
                CHECK_MESSAGE(after != before, pane << " / " << id << ": arrows did nothing");
                ++checked;
            }
        }
        // 还在这上面 面板 光标还留在我们留下的地方吗?
        REQUIRE_MESSAGE(pn::top(m) == kind, pane << " / " << id << ": a key left the pane");
    }
    CHECK_MESSAGE(checked > 0, pane << ": no editable rows exercised");
    MESSAGE(pane << ": " << checked << " rows exercised");
}

}  // 命名空间结束。

TEST_CASE("settings rows: every plugin add-form row takes input") {
    // 添加表单从插件列表中以`a`打开.
    install_stub_deps();
    Model m = app::update(Model{}, Msg{OpenSettingsList{settings::Category::Plugins}}).first;
    m = press(std::move(m), ch(U'a'));
    REQUIRE(m.ui.panel.get<pn::PluginEdit>() != nullptr);
    exercise("Plugin add", Msg{SettingsListMove{0}},
             [](Model& mm) -> form::Form* {
                 auto* o = mm.ui.panel.get<pn::PluginEdit>();
                 return o ? &o->form : nullptr;
             }, {}, std::move(m));
}

TEST_CASE("settings rows: list keys move, open and come back") {
    install_stub_deps();
    for (auto c : {settings::Category::General, settings::Category::Plugins,
                   settings::Category::Commands, settings::Category::Agents,
                   settings::Category::Hooks}) {
        Model m = app::update(Model{}, Msg{OpenSettingsList{c}}).first;
        const auto* o = m.ui.panel.get<pn::SettingsList>();
        REQUIRE(o != nullptr);
        const int n = static_cast<int>(settings::items_for(m, c).size());
        const int start = o->index;
        m = press(std::move(m), sk(maya::SpecialKey::Down));
        o = m.ui.panel.get<pn::SettingsList>();
        REQUIRE(o != nullptr);
        if (n > 1) CHECK_MESSAGE(o->index != start, settings::label(c) << ": Down did nothing");
        m = press(std::move(m), sk(maya::SpecialKey::Escape));
        CHECK_MESSAGE(m.ui.panel.get<pn::SettingsList>() == nullptr,
                      settings::label(c) << ": Esc did not close");
    }
}

TEST_CASE("settings rows: a paste lands in the open pane, never the composer") {
    // 外观和 沙箱 被丢失在粘贴路由器,和 RAG ,
    // 智能模式和插件仅用糊口 -edit 剩下的人 都
    // 掉进隐藏在后面的作曲家的脑海里 面板.
    install_stub_deps();
    const auto composer_after = [](Msg open, Model start = {}) {
        Model m = app::update(std::move(start), std::move(open)).first;
        const std::string before = m.ui.composer.text;
        auto msg = app::on_paste(m, maya::PasteEvent{"pasted"});
        m = app::update(std::move(m), std::move(msg)).first;
        return m.ui.composer.text == before;
    };
    Model sb;
    sb.d.persisted.sandbox.configured = true;
    CHECK_MESSAGE(composer_after(Msg{OpenAppearance{}}), "Appearance");
    CHECK_MESSAGE(composer_after(Msg{OpenSandbox{}}, std::move(sb)), "Sandbox");
    CHECK_MESSAGE(composer_after(Msg{OpenSmartMode{}}), "Smart Mode");
    CHECK_MESSAGE(composer_after(Msg{OpenRag{}}), "RAG");
    CHECK_MESSAGE(composer_after(Msg{OpenWebSearch{}}), "Web Search");
}

TEST_CASE("lists: a single step wraps, a page step stops at the end") {
    CHECK(ui::step(4, +1, 5) == 0);    // ~ 在最后一行 ~ 第一条 ~
    CHECK(ui::step(0, -1, 5) == 4);    // ⁇ 材行  ⁇ 程
    CHECK(ui::step(2, +10, 5) == 4);   // PgDn 停止在底部
    CHECK(ui::step(2, -10, 5) == 0);
    CHECK(ui::step(0, +1, 0) == 0);    // 空列表
    // 跳过头文件仍然包着.
    const auto hdr = [](int i) { return i == 0 || i == 3; };
    CHECK(ui::step_over(4, +1, 5, hdr) == 1);
    CHECK(ui::step_over(1, -1, 5, hdr) == 4);
}

TEST_CASE("settings search: finds a setting in any pane and opens it on that row") {
    install_stub_deps();
    Model m = app::update(Model{}, Msg{OpenSettingsList{settings::Category::General}}).first;
    auto* hub = m.ui.panel.get<pn::SettingsList>();
    REQUIRE(hub != nullptr);
    REQUIRE_FALSE(hub->index_rows.empty());

    // 打字进入搜索,而不是排行捷径.
    for (char32_t c : std::u32string{U"prose"}) m = press(std::move(m), ch(c));
    hub = m.ui.panel.get<pn::SettingsList>();
    REQUIRE(hub != nullptr);
    CHECK(hub->query == "prose");
    const auto hits = settings::search(hub->index_rows, hub->query);
    REQUIRE_FALSE(hits.empty());
    const auto& top = hub->index_rows[static_cast<std::size_t>(hits.front())];
    CHECK(top.pane == settings::ListOpen::Pane::Appearance);

    // Enter 打开 面板 与匹配行上的光标。
    m = press(std::move(m), sk(maya::SpecialKey::Enter));
    auto* ap = m.ui.panel.get<pn::Appearance>();
    REQUIRE(ap != nullptr);
    REQUIRE(ap->pane.form.focused() != nullptr);
    CHECK(ap->pane.form.focused()->id == top.row);

    // Esc 回到搜索, 查询完整; Esc 重新清除;
    // 第三个 Esc 关上
    m = press(std::move(m), sk(maya::SpecialKey::Escape));
    hub = m.ui.panel.get<pn::SettingsList>();
    REQUIRE(hub != nullptr);
    CHECK(hub->query == "prose");
    m = press(std::move(m), sk(maya::SpecialKey::Escape));
    hub = m.ui.panel.get<pn::SettingsList>();
    REQUIRE(hub != nullptr);
    CHECK(hub->query.empty());
    m = press(std::move(m), sk(maya::SpecialKey::Escape));
    CHECK(m.ui.panel.get<pn::SettingsList>() == nullptr);
}

TEST_CASE("settings search: an advanced row opens with advanced rows shown") {
    install_stub_deps();
    Model m = app::update(Model{}, Msg{OpenSmartMode{"smart.deep_margin"}}).first;
    auto* o = m.ui.panel.get<pn::SmartMode>();
    REQUIRE(o != nullptr);
    CHECK(o->advanced);
    REQUIRE(o->form.focused() != nullptr);
    CHECK(o->form.focused()->id == "smart.deep_margin");
}

TEST_CASE("settings search: every indexed row exists in the pane it names") {
    // 指数是建立在 面板 ' 自己的表格, 所以每个击球必须打开真正的一行。
    // catch a 面板 其开放的道路造就了不同的形态.
    install_stub_deps();
    Model base = app::update(Model{}, Msg{OpenSettingsList{settings::Category::General}}).first;
    const auto rows = base.ui.panel.get<pn::SettingsList>()->index_rows;
    for (const auto& h : rows) {
        Model m = app::update(Model{}, Msg{OpenSettingsList{settings::Category::General}}).first;
        using P = settings::ListOpen::Pane;
        Msg open = h.pane == P::Rag        ? Msg{OpenRag{h.row}}
                 : h.pane == P::Smart      ? Msg{OpenSmartMode{h.row}}
                 : h.pane == P::Appearance ? Msg{OpenAppearance{h.row}}
                 : h.pane == P::Sandbox    ? Msg{OpenSandbox{h.row}}
                 : h.pane == P::Paste      ? Msg{OpenPasteSettings{h.row}}
                                           : Msg{OpenWebSearch{h.row}};
        m = app::update(std::move(m), std::move(open)).first;
        const form::Form* f = nullptr;
        if (auto* x = m.ui.panel.get<pn::Rag>())        f = &x->embed.form;
        if (auto* x = m.ui.panel.get<pn::SmartMode>())  f = &x->form;
        if (auto* x = m.ui.panel.get<pn::Appearance>()) f = &x->pane.form;
        if (auto* x = m.ui.panel.get<pn::Sandbox>())    f = &x->pane.form;
        if (auto* x = m.ui.panel.get<pn::WebSearch>())  f = &x->form;
        if (auto* x = m.ui.panel.get<pn::PasteSettings>()) f = &x->form;
        REQUIRE(f != nullptr);
        REQUIRE(f->focused() != nullptr);
        CHECK_MESSAGE(f->focused()->id == h.row,
                      settings::pane_label(h.pane) << " / " << h.row);
    }
}

TEST_CASE("threads: typing filters by title, Enter opens the filtered row") {
    install_stub_deps();
    Model m;
    for (const char* t : {"fix login bug", "settings search", "refactor login"}) {
        Thread th;
        th.id = ThreadId{t};
        th.title = t;
        m.d.threads.push_back(th);
    }
    m = app::update(std::move(m), Msg{OpenThreadList{}}).first;
    REQUIRE(m.ui.panel.get<pn::ThreadList>() != nullptr);
    for (char32_t c : std::u32string{U"login"}) m = press(std::move(m), ch(c));
    const auto* p = m.ui.panel.get<pn::ThreadList>();
    REQUIRE(p != nullptr);
    CHECK(p->query == "login");
    const auto vis = ui::visible_threads(m.d.threads, p->query);
    REQUIRE(vis.size() == 2);
    // 然后, \\\\ \ \\ 包装回第一对比赛。
    m = press(std::move(m), sk(maya::SpecialKey::Down));
    m = press(std::move(m), sk(maya::SpecialKey::Down));
    CHECK(m.ui.panel.get<pn::ThreadList>()->index == 0);
    // Esc清除过滤器,但保持开放.
    m = press(std::move(m), sk(maya::SpecialKey::Escape));
    p = m.ui.panel.get<pn::ThreadList>();
    REQUIRE(p != nullptr);
    CHECK(p->query.empty());
}

TEST_CASE("settings search: blank, backspace and paste behave") {
    install_stub_deps();
    Model m = app::update(Model{}, Msg{OpenSettingsList{settings::Category::General}}).first;
    const int hub_rows = static_cast<int>(settings::items_for(m, settings::Category::General).size());
    // 领先空间被忽略,所以枢机行会留下.
    m = press(std::move(m), ch(U' '));
    CHECK(m.ui.panel.get<pn::SettingsList>()->query.empty());
    // 将土地贴在搜索中,而不是作曲家身上.
    { auto msg = app::on_paste(m, maya::PasteEvent{"port\n"}); m = app::update(std::move(m), std::move(msg)).first; }
    auto* hub = m.ui.panel.get<pn::SettingsList>();
    REQUIRE(hub != nullptr);
    CHECK(hub->query == "port");
    CHECK(m.ui.composer.text.empty());
    CHECK_FALSE(settings::search(hub->index_rows, hub->query).empty());
    // Backspace 空返回到行中的中枢,您可以操作.
    for (int i = 0; i < 4; ++i) m = press(std::move(m), sk(maya::SpecialKey::Backspace));
    hub = m.ui.panel.get<pn::SettingsList>();
    REQUIRE(hub != nullptr);
    CHECK(hub->query.empty());
    CHECK(hub->index >= 0);
    CHECK(hub->index < hub_rows);
    // 无对:Enter无所作为,枢纽保持开放.
    for (char32_t c : std::u32string{U"zzqqxx"}) m = press(std::move(m), ch(c));
    m = press(std::move(m), sk(maya::SpecialKey::Enter));
    CHECK(m.ui.panel.get<pn::SettingsList>() != nullptr);
}

TEST_CASE("threads: paste goes into the filter") {
    install_stub_deps();
    Model m;
    Thread t;
    t.id = ThreadId{"a"};
    t.title = "alpha";
    m.d.threads.push_back(t);
    m = app::update(std::move(m), Msg{OpenThreadList{}}).first;
    { auto msg = app::on_paste(m, maya::PasteEvent{"alp"}); m = app::update(std::move(m), std::move(msg)).first; }
    REQUIRE(m.ui.panel.get<pn::ThreadList>() != nullptr);
    CHECK(m.ui.panel.get<pn::ThreadList>()->query == "alp");
    CHECK(m.ui.composer.text.empty());
}

namespace {
maya::KeyEvent ctrl(char32_t c) {
    maya::KeyEvent ev{maya::CharKey{c}, {}, {}};
    ev.mods.ctrl = true;
    return ev;
}

// 键怎么返回主机运行, 穿过真正的键路径。
agtest::fx::Store press_fx(Model& m, const maya::KeyEvent& ev) {
    agtest::fx::Store s;
    if (auto msg = app::on_key(m, ev)) {
        auto [next, cmd] = app::update(std::move(m), std::move(*msg));
        m = std::move(next);
        agtest::fx::run(cmd, s);
    }
    return s;
}

Model two_threads_open() {
    Model m;
    for (const char* id : {"p1", "p2"}) {
        Thread th;
        th.id = ThreadId{id};
        th.title = "[Pasted text \xc2\xb7 3 lines \xc2\xb7 99 B]";
        m.d.threads.push_back(th);
    }
    m.d.current.id = ThreadId{"other"};
    return app::update(std::move(m), Msg{OpenThreadList{}}).first;
}
} // 命名空间结束。

TEST_CASE("threads: ^R renames the focused row, pre-filled") {
    install_stub_deps();
    Model m = two_threads_open();
    m = press(std::move(m), sk(maya::SpecialKey::Down));   // 焦点p2
    m = press(std::move(m), ctrl(U'r'));
    auto* p = m.ui.panel.get<pn::ThreadList>();
    REQUIRE(p != nullptr);
    CHECK(p->renaming == "p2");
    CHECK(p->rename_input == m.d.threads[1].title);

    // 信件编辑草稿,而不是过滤器; ^D 中点没有做任何工作 -edit.
    for (int i = 0; i < 64; ++i) m = press(std::move(m), sk(maya::SpecialKey::Backspace));
    for (char32_t c : std::u32string{U"auth fix"}) m = press(std::move(m), ch(c));
    CHECK(press_fx(m, ctrl(U'd')).deleted_threads.empty());
    p = m.ui.panel.get<pn::ThreadList>();
    REQUIRE(p != nullptr);
    CHECK(p->query.empty());
    CHECK(p->confirm_remove.empty());
    CHECK(p->rename_input == "auth fix");

    const auto fx = press_fx(m, sk(maya::SpecialKey::Enter));
    REQUIRE(fx.renamed_threads.size() == 1);
    CHECK(fx.renamed_threads[0].id.value == "p2");
    CHECK(fx.renamed_threads[0].title == "auth fix");
    CHECK(m.d.threads[1].title == "auth fix");
    CHECK(m.d.threads[0].title != "auth fix");
    p = m.ui.panel.get<pn::ThreadList>();
    REQUIRE(p != nullptr);             // Enter 已保存;没有打开线程
    CHECK(p->renaming.empty());
}

TEST_CASE("threads: a reload read before the rename landed keeps it") {
    install_stub_deps();
    Model m = two_threads_open();
    m = press(std::move(m), ctrl(U'r'));
    for (int i = 0; i < 64; ++i) m = press(std::move(m), sk(maya::SpecialKey::Backspace));
    for (char32_t c : std::u32string{U"mine"}) m = press(std::move(m), ch(c));
    const auto fx = press_fx(m, sk(maya::SpecialKey::Enter));
    REQUIRE(fx.renamed_threads.size() == 1);
    const ThreadId id = fx.renamed_threads[0].id;
    const auto at = fx.renamed_threads[0].renamed_at;
    CHECK(at > 0);
    auto row = [&](const Model& mm) -> const Thread& {
        return *std::ranges::find(mm.d.threads, id, &Thread::id);
    };
    CHECK(row(m).renamed_at == at);

    // 列表从旧标题的磁盘中返回:作者尚未存储重命名,
    // 内存中较新的重命名获胜.
    std::vector<Thread> from_disk = m.d.threads;
    for (auto& t : from_disk)
        if (t.id == id) { t.title = "[Pasted text \xc2\xb7 3 lines \xc2\xb7 99 B]"; t.renamed_at = 0; }
    m = app::update(std::move(m), Msg{ThreadsLoaded{std::move(from_disk)}}).first;
    CHECK(row(m).title == "mine");

    // 从较新的磁盘(另一个为agentty)重命名确实可以替换它.
    from_disk = m.d.threads;
    for (auto& t : from_disk)
        if (t.id == id) { t.title = "theirs"; t.renamed_at = at + 10; }
    m = app::update(std::move(m), Msg{ThreadsLoaded{std::move(from_disk)}}).first;
    CHECK(row(m).title == "theirs");
}

TEST_CASE("threads: a generated title names the open thread, cleaned") {
    install_stub_deps();
    Model m;
    m.d.current.id    = ThreadId{"t-auto"};
    m.d.current.title = "[Pasted text \xc2\xb7 3 lines \xc2\xb7 99 B]";
    m.d.threads.push_back(m.d.current);
    m.s.titling = m.d.current.id;

    // 引文 后期和换行时间被剥离.
    agtest::fx::Store fx;
    auto [next, cmd] = app::update(std::move(m),
        Msg{ThreadTitleGenerated{ThreadId{"t-auto"}, "\"Fix the login bug.\"\n"}});
    m = std::move(next);
    agtest::fx::run(cmd, fx);
    CHECK(m.d.current.title == "Fix the login bug");
    CHECK(m.d.current.auto_titled);
    CHECK(m.d.threads[0].title == "Fix the login bug");
    CHECK(m.s.titling.value.empty());
    REQUIRE(fx.saved_threads.size() == 1);
    CHECK(fx.saved_threads[0].title == "Fix the login bug");

    // 一个失败的请求(空标题)保留它拥有的名称,
    // 而不再被问到:一个无法回答的模型会花费一个请求转弯.
    m.d.current.title = "kept"; m.d.current.auto_titled = false;
    m = app::update(std::move(m), Msg{ThreadTitleGenerated{ThreadId{"t-auto"}, ""}}).first;
    CHECK(m.d.current.title == "kept");
    CHECK(m.d.current.auto_titled);
    CHECK(app::detail::maybe_auto_title(m).is_none());
}

TEST_CASE("threads: a generated title never replaces a chosen name") {
    install_stub_deps();
    Model m;
    m.d.current.id         = ThreadId{"t-mine"};
    m.d.current.title      = "my name";
    m.d.current.renamed_at = 1'800'000'000;
    m.d.threads.push_back(m.d.current);
    m = app::update(std::move(m),
        Msg{ThreadTitleGenerated{ThreadId{"t-mine"}, "Model title"}}).first;
    CHECK(m.d.current.title == "my name");
    CHECK(m.d.threads[0].title == "my name");

    // 一个人也不行 线程 你转身离开。
    m.d.current.id = ThreadId{"elsewhere"};
    m.d.threads[0].renamed_at = 0;
    m = app::update(std::move(m),
        Msg{ThreadTitleGenerated{ThreadId{"t-mine"}, "Model title"}}).first;
    CHECK(m.d.threads[0].title == "my name");
}

TEST_CASE("threads: a title is asked for once, after the first reply") {
    install_stub_deps();
    Model m;
    m.d.current.id = ThreadId{"t-ask"};
    m.d.current.auto_title_ok = true;   // a 线程 开始于授权
    Message u; u.role = Role::User; u.text = "how do I fix the login bug";
    m.d.current.messages.push_back(u);
    CHECK(app::detail::maybe_auto_title(m).is_none());   // 尚未回复

    Message a; a.role = Role::Assistant; a.text = "Check the session cookie.";
    m.d.current.messages.push_back(a);
    CHECK_FALSE(app::detail::maybe_auto_title(m).is_none());
    CHECK(m.s.titling == m.d.current.id);
    CHECK(app::detail::maybe_auto_title(m).is_none());   // 已询问

    m.s.titling = {};
    m.d.current.auto_titled = true;
    CHECK(app::detail::maybe_auto_title(m).is_none());   // 已标题

    m.d.current.auto_titled = false;
    m.d.current.renamed_at  = 1;
    CHECK(app::detail::maybe_auto_title(m).is_none());   // 用户命名它

    m.d.current.renamed_at = 0;
    m.d.persisted.auto_title = false;
    CHECK(app::detail::maybe_auto_title(m).is_none());   // 关机
}

TEST_CASE("threads: old threads and forks are never auto-titled") {
    install_stub_deps();
    Model m;
    m.d.current.id = ThreadId{"t-old"};
    Message u; u.role = Role::User; u.text = "an old chat";
    Message a; a.role = Role::Assistant; a.text = "a reply";
    m.d.current.messages = {u, a};
    // 已保存 存在所有权之前:否 标志 继续它的名字。
    CHECK(app::detail::maybe_auto_title(m).is_none());

    // 叉:它的首个用户消息是叉音符,
    // 以它的母词命名,它从标志关起.
    Model f;
    f.d.current.id = ThreadId{"t-fork"};
    f.d.current.title = "Fork: an old chat";
    Message note; note.role = Role::User; note.fork_note = true;
    note.text = "This conversation is a fork of an earlier one";
    f.d.current.messages = {note, a};
    CHECK(app::detail::maybe_auto_title(f).is_none());
}

TEST_CASE("threads: a new thread is eligible, a fresh one from ^N too") {
    install_stub_deps();
    Model m;
    m.d.current.messages.push_back(Message{});
    m = app::update(std::move(m), Msg{NewThread{}}).first;
    CHECK(m.d.current.auto_title_ok);
}

TEST_CASE("threads: clearing a rename goes back to the automatic name") {
    install_stub_deps();
    Model m;
    m.d.current.id = ThreadId{"t-c"};
    m.d.current.title = "my name";
    m.d.current.renamed_at = 1'700'000'000;   // 2023年的重命名
    Message u; u.role = Role::User; u.text = "fix the login bug\nand more";
    Message a; a.role = Role::Assistant; a.text = "ok";
    m.d.current.messages = {u, a};
    m.d.threads.push_back(m.d.current);
    m = app::update(std::move(m), Msg{OpenThreadList{}}).first;

    // ^R ,把草稿清空, Enter.
    m = press(std::move(m), ctrl(U'r'));
    for (int i = 0; i < 64; ++i) m = press(std::move(m), sk(maya::SpecialKey::Backspace));
    const auto fx = press_fx(m, sk(maya::SpecialKey::Enter));
    REQUIRE(fx.renamed_threads.size() == 1);
    CHECK(fx.renamed_threads[0].name_is_auto);
    CHECK(fx.renamed_threads[0].renamed_at > 1'700'000'000);   // 击败旧的重命名
    CHECK(m.d.current.title == "fix the login bug and more");   // 首个-line名称
    CHECK_FALSE(m.d.current.user_named());
    // 并在下次回复后获得汽车标题.
    CHECK_FALSE(app::detail::maybe_auto_title(m).is_none());
}

TEST_CASE("threads: a clear on a thread you never named writes nothing") {
    install_stub_deps();
    Model m = two_threads_open();
    m = press(std::move(m), ctrl(U'r'));
    for (int i = 0; i < 64; ++i) m = press(std::move(m), sk(maya::SpecialKey::Backspace));
    CHECK(press_fx(m, sk(maya::SpecialKey::Enter)).renamed_threads.empty());
}

TEST_CASE("threads: the title prompt skips the fork note and injected context") {
    install_stub_deps();
    Model m;
    m.d.model_id.value = "test-model";
    Message note; note.role = Role::User; note.fork_note = true; note.text = "fork note";
    Message mine; mine.role = Role::User; mine.text = "the real question";
    Message a; a.role = Role::Assistant; a.text = "an answer";
    m.d.current.messages = {note, mine, a};
    // 不空: 它找到一个 带类型的 信件可标题。
    CHECK_FALSE(app::cmd::generate_thread_title(m).is_none());
    Model only_note;
    only_note.d.current.messages = {note, a};
    CHECK(app::cmd::generate_thread_title(only_note).is_none());
}

TEST_CASE("threads: an auto title says so in a toast and in the list") {
    install_stub_deps();
    Model m;
    m.d.current.id = ThreadId{"t-n"};
    m.d.threads.push_back(m.d.current);
    m = app::update(std::move(m),
        Msg{ThreadTitleGenerated{ThreadId{"t-n"}, "Login bug"}}).first;
    CHECK(m.s.status.find("Login bug") != std::string::npos);
    CHECK(m.s.status.find("/rename") != std::string::npos);
}

TEST_CASE("settings: chat names toggles auto titling") {
    install_stub_deps();
    Model m;
    const bool before = m.d.persisted.auto_title;
    m = app::update(std::move(m), Msg{ToggleAutoTitle{}}).first;
    CHECK(m.d.persisted.auto_title != before);
    const auto rows = settings::items_for(m, settings::Category::General);
    CHECK(std::ranges::any_of(rows, [](const auto& r) { return r.primary == "Chat names"; }));
}

TEST_CASE("threads: rename Esc, empty and unchanged write nothing") {
    install_stub_deps();
    Model m = two_threads_open();
    const std::string before = m.d.threads[0].title;

    // Esc 取消编辑并保留 选择面板 打开。
    m = press(std::move(m), ctrl(U'r'));
    m = press(std::move(m), ch(U'x'));
    m = press(std::move(m), sk(maya::SpecialKey::Escape));
    REQUIRE(m.ui.panel.get<pn::ThreadList>() != nullptr);
    CHECK(m.ui.panel.get<pn::ThreadList>()->renaming.empty());
    CHECK(m.d.threads[0].title == before);

    // Enter 草案没有被修改,就是没有 -op.
    m = press(std::move(m), ctrl(U'r'));
    CHECK(press_fx(m, sk(maya::SpecialKey::Enter)).renamed_threads.empty());

    // 空白的草稿在A 线程 你从来没有命名是没有 -op (澄清一个名字,
    // 那里没有什么可澄清的)
    m = press(std::move(m), ctrl(U'r'));
    for (int i = 0; i < 64; ++i) m = press(std::move(m), sk(maya::SpecialKey::Backspace));
    m = press(std::move(m), ch(U' '));
    CHECK(press_fx(m, sk(maya::SpecialKey::Enter)).renamed_threads.empty());
    CHECK(m.d.threads[0].title == before);
}

TEST_CASE("threads: rename paste is one line, capped, and renames the open thread") {
    install_stub_deps();
    Model m = two_threads_open();
    m.d.current.id = ThreadId{"p1"};
    m.d.current.title = m.d.threads[0].title;
    m = press(std::move(m), ctrl(U'r'));
    for (int i = 0; i < 64; ++i) m = press(std::move(m), sk(maya::SpecialKey::Backspace));
    { auto msg = app::on_paste(m, maya::PasteEvent{"one\ntwo"}); m = app::update(std::move(m), std::move(msg)).first; }
    auto* p = m.ui.panel.get<pn::ThreadList>();
    REQUIRE(p != nullptr);
    CHECK(p->rename_input == "one two");
    CHECK(p->query.empty());
    CHECK(m.ui.composer.text.empty());

    { auto msg = app::on_paste(m, maya::PasteEvent{std::string(200, 'z')}); m = app::update(std::move(m), std::move(msg)).first; }
    p = m.ui.panel.get<pn::ThreadList>();
    REQUIRE(p != nullptr);
    CHECK(p->rename_input.size() == persistence::kTitleMaxBytes);
    m = press(std::move(m), ch(U'q'));   // 拒绝在封顶上输入
    CHECK(m.ui.panel.get<pn::ThreadList>()->rename_input.size() == persistence::kTitleMaxBytes);

    const auto fx = press_fx(m, sk(maya::SpecialKey::Enter));
    REQUIRE(fx.renamed_threads.size() == 1);
    // 开阔 线程 重新为 -saved 发件人 m.d.current 转弯,
    // 所以它必须背着 新标题,或者下一个 保存 写回旧的
    CHECK(m.d.current.title == fx.renamed_threads[0].title);
}

TEST_CASE("threads: rename edits at the caret, and arrows, Home/End and Delete work") {
    install_stub_deps();
    Model m;
    Thread th;
    th.id = ThreadId{"c1"};
    th.title = "caf\xc3\xa9 bug";          // a 2-byte 中间代码点 -title
    m.d.threads.push_back(th);
    m.d.current.id = ThreadId{"other"};
    m = app::update(std::move(m), Msg{OpenThreadList{}}).first;
    m = press(std::move(m), ctrl(U'r'));
    auto draft = [&] { return m.ui.panel.get<pn::ThreadList>()->rename_input; };
    auto caret = [&] { return m.ui.panel.get<pn::ThreadList>()->rename_cursor; };
    CHECK(caret() == static_cast<int>(th.title.size()));   // 结尾打开

    // Home ,然后修正第一个字母: Delete 向前,输入新的。
    m = press(std::move(m), sk(maya::SpecialKey::Home));
    CHECK(caret() == 0);
    m = press(std::move(m), sk(maya::SpecialKey::Delete));
    m = press(std::move(m), ch(U'C'));
    CHECK(draft() == "Caf\xc3\xa9 bug");
    CHECK(caret() == 1);

    // 正确的步骤超过整个代码点, 从来没有进入它的中间。
    for (int i = 0; i < 3; ++i) m = press(std::move(m), sk(maya::SpecialKey::Right));
    CHECK(caret() == 5);                     // 过去 "af" 和2号 -byte e
    m = press(std::move(m), sk(maya::SpecialKey::Left));
    CHECK(caret() == 3);
    m = press(std::move(m), sk(maya::SpecialKey::Delete));   // 删除 e-acute 整段
    CHECK(draft() == "Caf bug");
    m = press(std::move(m), sk(maya::SpecialKey::Backspace)); // 删除'f'
    CHECK(draft() == "Ca bug");
    CHECK(caret() == 2);

    // Delete 最终 Backspace 起始时:无任何要删除。
    m = press(std::move(m), sk(maya::SpecialKey::End));
    m = press(std::move(m), sk(maya::SpecialKey::Delete));
    m = press(std::move(m), sk(maya::SpecialKey::Home));
    m = press(std::move(m), sk(maya::SpecialKey::Backspace));
    CHECK(draft() == "Ca bug");

    // 贴贴在地上 光标.
    { auto msg = app::on_paste(m, maya::PasteEvent{"[x] "}); m = app::update(std::move(m), std::move(msg)).first; }
    CHECK(draft() == "[x] Ca bug");
    CHECK(caret() == 4);
    // Alt +Left (帮助) +Left (帮助 线程 快点 -cycle 仅移动 光标 给
    maya::KeyEvent alt_left{maya::SpecialKey::Left, {}, {}};
    alt_left.mods.alt = true;
    m = press(std::move(m), alt_left);
    CHECK(caret() == 3);
    CHECK(m.ui.panel.get<pn::ThreadList>()->renaming == "c1");
    CHECK(m.d.current.id.value == "other");

    const auto fx = press_fx(m, sk(maya::SpecialKey::Enter));
    REQUIRE(fx.renamed_threads.size() == 1);
    CHECK(fx.renamed_threads[0].title == "[x] Ca bug");
}

TEST_CASE("threads: a capped paste mid-title keeps the text after the caret") {
    install_stub_deps();
    Model m = two_threads_open();
    m = press(std::move(m), ctrl(U'r'));
    m = press(std::move(m), sk(maya::SpecialKey::Home));
    const std::string before = m.ui.panel.get<pn::ThreadList>()->rename_input;
    { auto msg = app::on_paste(m, maya::PasteEvent{std::string(200, 'z')}); m = app::update(std::move(m), std::move(msg)).first; }
    const auto* p = m.ui.panel.get<pn::ThreadList>();
    REQUIRE(p != nullptr);
    CHECK(p->rename_input.size() == persistence::kTitleMaxBytes);
    CHECK(p->rename_input.ends_with(before));
    CHECK(p->rename_cursor == static_cast<int>(persistence::kTitleMaxBytes - before.size()));
}

TEST_CASE("threads: opening a renamed thread before the rename lands keeps it") {
    install_stub_deps();
    Model m = two_threads_open();
    m = press(std::move(m), ctrl(U'r'));
    for (int i = 0; i < 64; ++i) m = press(std::move(m), sk(maya::SpecialKey::Backspace));
    for (char32_t c : std::u32string{U"kept"}) m = press(std::move(m), ch(c));
    m = press(std::move(m), sk(maya::SpecialKey::Enter));
    REQUIRE(m.d.threads[0].title == "kept");

    // 磁盘读取的字形为作者的种族,仍然有旧标题。 A 加载 只会
    // 在土地是 待处理 一项( ThreadListSelect 设置它,
    // 所以站起来 同样的方式。
    Thread loaded;
    loaded.id = ThreadId{"p1"};
    loaded.title = "[Pasted text \xc2\xb7 3 lines \xc2\xb7 99 B]";
    m.s.loading_thread = loaded.id;
    m.s.thread_loading = true;
    m = app::update(std::move(m), Msg{ThreadLoaded{std::move(loaded)}}).first;
    CHECK(m.d.current.id.value == "p1");
    CHECK(m.d.current.title == "kept");
}

TEST_CASE("settings rows: a cleared Embeddings port takes any number (#97)") {
    // 报告: RAG > 嵌入 > 端口, Backspace
    // 中输入数字。只有从 1 开始的数字才输入。通过真实数字 面板 ,
    // 真实的键,和 渲染 门: 每一个键盘键必须也 重绘.
    install_stub_deps();
    Model start;
    start.d.persisted.rag.embed_backend = "ollama";   // 有端口行
    Model m = app::update(std::move(start),
                          Msg{OpenRag{std::string{rag_settings::kFieldPort}}}).first;
    auto* rag = m.ui.panel.get<pn::Rag>();
    REQUIRE(rag != nullptr);
    auto* row = rag->embed.form.focused();
    REQUIRE(row != nullptr);
    REQUIRE(row->id == rag_settings::kFieldPort);

    auto h = app::AgenttyApp::visual_hash(m);
    const auto step = [&](const maya::KeyEvent& ev, const char* what) {
        m = press(std::move(m), ev);
        const auto h2 = app::AgenttyApp::visual_hash(m);
        CHECK_MESSAGE(h2 != h, what << " did not repaint");
        h = h2;
    };
    step(sk(maya::SpecialKey::Enter), "Enter");
    for (int i = 0; i < 5; ++i) step(sk(maya::SpecialKey::Backspace), "Backspace");
    for (char32_t c : std::u32string{U"8080"}) step(ch(c), "digit");
    m = press(std::move(m), sk(maya::SpecialKey::Enter));
    rag = m.ui.panel.get<pn::Rag>();
    REQUIRE(rag != nullptr);
    const auto* port = rag->embed.form.find(rag_settings::kFieldPort);
    REQUIRE(port != nullptr);
    CHECK(form::number_value(std::get<form::field::Number>(port->value)) == 8080);
}

TEST_CASE("settings rows: every Appearance row takes input") {
    // 主题行将手移到浏览器(Enter),而不是箭头.
    exercise("Appearance", Msg{OpenAppearance{}},
             [](Model& m) -> form::Form* {
                 auto* o = m.ui.panel.get<pn::Appearance>();
                 return o ? &o->pane.form : nullptr;
             });
}

TEST_CASE("settings rows: every Sandbox row takes input") {
    // 在 brap 下, 大多数行都是故意锁定的。 选择 claybin ,
    // 选择用户的操作方式, 然后它解锁的每行必须输入。
    Model m;
    m.d.persisted.sandbox.backend = sandbox_cfg::LinuxBackend::Claybin;
    m.d.persisted.sandbox.configured = true;
    exercise("Sandbox", Msg{OpenSandbox{}},
             [](Model& m) -> form::Form* {
                 auto* o = m.ui.panel.get<pn::Sandbox>();
                 return o ? &o->pane.form : nullptr;
             }, {}, std::move(m));
}

TEST_CASE("settings rows: every Smart Mode row takes input") {
    exercise("Smart Mode", Msg{OpenSmartMode{}},
             [](Model& m) -> form::Form* {
                 auto* o = m.ui.panel.get<pn::SmartMode>();
                 return o ? &o->form : nullptr;
             });
}

TEST_CASE("settings rows: every RAG row takes input") {
    exercise("RAG", Msg{OpenRag{}},
             [](Model& m) -> form::Form* {
                 auto* o = m.ui.panel.get<pn::Rag>();
                 return o ? &o->embed.form : nullptr;
             });
}

TEST_CASE("settings rows: every Web Search row takes input") {
    exercise("Web Search", Msg{OpenWebSearch{}},
             [](Model& m) -> form::Form* {
                 auto* o = m.ui.panel.get<pn::WebSearch>();
                 return o ? &o->form : nullptr;
             });
}
