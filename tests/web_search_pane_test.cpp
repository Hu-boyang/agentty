// 网络搜索 面板 : 设置 ~ 网络搜索表驱动持续 web_search 策略,
// 而承诺编辑器作为工具层到达 副作用 而不是一个 状态更新函数
// 调用 进入进程 -global 州立大学.
//
// 失败的这个针是 面板 被活的但断开: 画的行,
// 打开一个 shell 的门, 坚持到 Model 但让工具层永远
// 读取种子。 Smart Mode 的 面板 回答这个用
// apply_smart ,两者都坚持 和 出版; 此项 面板's
// commit() 做同样的事情,下面的测试 都取决于两者。

#include "agtest.hpp"
#include "agtest_fx.hpp"

#include "agentty/io/persistence.hpp"
#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/msg.hpp"
#include "agentty/runtime/panel/nav.hpp"
#include "agentty/runtime/panel/settings/items.hpp"
#include "agentty/runtime/panel/web_search_form.hpp"
#include "agentty/runtime/settings_registry.hpp"

#include "agentty/auth/keystore.hpp"
#include "agentty/tool/web_search_secret.hpp"

#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

using namespace agentty;
namespace pn = agentty::ui::panel;
namespace reg  = agentty::settings::registry;
namespace se   = agentty::settings;
namespace wtf  = agentty::web_search_form;
namespace fx   = agtest::fx;

namespace {

Model opened(Model m = Model{}) {
    auto [mm, _] = app::update(std::move(m), Msg{OpenWebSearch{}});
    return std::move(mm);
}

const pn::WebSearch& pane_of(const Model& m) {
    const auto* o = m.ui.panel.get<pn::WebSearch>();
    REQUIRE(o != nullptr);
    return *o;
}

Msg key(form::keys::Intent i) { return Msg{WebSearchKey{form::keys::Action{i, 0}}}; }

Model send(Model m, Msg msg) {
    auto [mm, _] = app::update(std::move(m), std::move(msg));
    (void)_;
    return std::move(mm);
}

// 开车 面板 改为 `row_id` (下调) -arrow ),然后适用 `key`.
void focus_row(Model& m, std::string_view row_id) {
    auto* o = m.ui.panel.get<pn::WebSearch>();
    for (int guard = 0; guard < 32; ++guard) {
        const auto* row = o->form.focused();
        if (row && row->id == row_id) return;
        auto [mm, _] = app::update(std::move(m), key(form::keys::Intent::MoveNext));
        m = std::move(mm);
        o = m.ui.panel.get<pn::WebSearch>();
        REQUIRE(o != nullptr);
    }
    CHECK(false);   // row_id 不是搜索行
}

// 打开 面板 开关已经安装 `mode` ("auto" | "on" | "off"):
// 数行只能编辑,所以测试从那里开始。
Model opened_in(const char* mode) {
    Model m;
    m.d.persisted.web_search.mode_text = mode;
    return opened(std::move(m));
}

} // 命名空间结束。

TEST_CASE("web search pane: the General list has a door that opens the pane") {
    Model m;
    const auto items = se::items_for(m, se::Category::General);
    const auto* door = static_cast<const se::Item*>(nullptr);
    for (const auto& i : items)
        if (i.action == se::Action::OpenWebSearch) door = &i;
    REQUIRE(door != nullptr);
    CHECK(door->primary == "Web Search");
    CHECK(se::opens_pane(door->action), "a door row wears the \xe2\x86\x92 affordance");
    CHECK(!door->secondary.empty(),
          "the row says what the policy does, so it reads as a state, not a bare name");
    // ...谁先回答,名字叫"... 面板 '菜单使用。
    CHECK(door->secondary.find("Brave Free") != std::string::npos);

    // 以动作打开:设置列表降入面板.
    auto [m2, c2] = app::update(Model{}, Msg{OpenWebSearch{}});
    // 两位 头文件 ,从 注册表 组别( "Search",
    // "Services" (三) 每一 web_search.* 注册表 列;
    // 空位下自由默认的拨号; NO 键行,因为三个字都不需要键。
    // 面板 OPENS 开关上,不是开关上 头文件.
    //
    // 行被命名而不是计算:精确的计数是第一次添加一行时因正确而
    // 失败的测试(见smart_tuning_settings_test
    // 中的注).
    const auto& form = pane_of(m2).form;
    int headers = 0;
    for (const auto& f : form.fields) if (f.is_header()) ++headers;
    CHECK(headers == 2);
    for (const auto& d : reg::kSettings)
        if (d.owner() == reg::Owner::WebSearch)
            CHECK(form.find(d.id) != nullptr, std::string{d.id});
    for (const char* id : {"web_search.dial.brave-free.freshness",
                           "web_search.dial.ddg-free.region",
                           "web_search.dial.ddg-free.safe",
                           "web_search.dial.tavily-free.topic",
                           "web_search.dial.tavily-free.time_range"})
        CHECK(form.find(id) != nullptr, id);
    for (const auto& f : form.fields)
        CHECK(f.id.rfind("web_search.key.", 0) != 0, f.id);
    CHECK(form.fields.front().is_header());
    const auto* first = form.focused();
    REQUIRE(first != nullptr);
    CHECK(first->id == "web_search.mode");
    CHECK(!first->locked);
}

TEST_CASE("web search pane: turning search off persists and publishes") {
    Model m = opened();

    // 默认:自动,10个结果,上限20.
    CHECK(web_search_cfg::mode(m.d.persisted.web_search) == web_search_cfg::Mode::Auto);

    // 打开开关上模式下拉, 步骤到关闭, 确认。
    focus_row(m, "web_search.mode");
    m = send(std::move(m), key(form::keys::Intent::Activate));
    m = send(std::move(m), key(form::keys::Intent::MenuNext));
    m = send(std::move(m), key(form::keys::Intent::MenuNext));
    auto [m2, cmd] = app::update(std::move(m), key(form::keys::Intent::MenuCommit));
    CHECK(web_search_cfg::mode(m2.d.persisted.web_search) == web_search_cfg::Mode::Off,
          "the Model holds the new value");

    // 坚持主义 副作用: a 保存 曾经是 RETURNED ,用于 settings.json 与行相续.
    const auto* saved = fx::find<agentty::SaveSettings>(cmd);
    REQUIRE(saved != nullptr);
    CHECK(saved->settings.web_search.mode_text == "off");

    // 出版: 状态更新函数 仅返回 保存 ; 告诉工具层
    // publish_derived 工作( 时间: update.cpp ),
    // 它会随每条消息运行 归并 应用到这里,就像应用一样
    // 副作用 名称 新 政策。
    const int saves  = fx::count<agentty::SaveSettings>(cmd);
    CHECK(saves == 1, "persist");
    m2.published_web_search = agentty::web_search_cfg::Config{};   // 仿佛在前作中公布
    const Cmd seam = app::publish_derived(m2, Cmd::none());
    const auto* pub = fx::find<agentty::PublishWebSearchPolicy>(seam);
    REQUIRE(pub != nullptr);
    CHECK(web_search_cfg::mode(pub->policy) == web_search_cfg::Mode::Off,
          "the tool layer is told search is off");
    // 别再在一次 归并 改变不了什么。
    CHECK(fx::find<agentty::PublishWebSearchPolicy>(
              app::publish_derived(m2, Cmd::none())) == nullptr);

    // 面板现在反映了禁用状态,光标保留在行上.
    const auto* row = pane_of(m2).form.find("web_search.mode");
    REQUIRE(row != nullptr);
    CHECK(pane_of(m2).form.focused() == row, "cursor stays on the switch");
}

TEST_CASE("web search pane: typed rows commit on leaving the field, not per keystroke") {
    Model m = opened_in("on");
    focus_row(m, "web_search.count");

    // Enter EDIT 模式,键入一个数字,留在字段: 此处的
    // 已承诺编辑将 保存 `1` 前往 `15` 并重建表格 光标.
    auto [m1, c1] = app::update(std::move(m), key(form::keys::Intent::Activate));
    const auto* ed = m1.ui.panel.get<pn::WebSearch>();
    CHECK(ed && ed->form.editing(), "in the field");

    auto [m2, c2] = app::update(std::move(m1),
        Msg{WebSearchKey{form::keys::Action{form::keys::Intent::Insert, U'1'}}});
    CHECK(fx::find<agentty::SaveSettings>(c2) == nullptr,
          "mid-edit: nothing saved yet");
    CHECK(m2.d.persisted.web_search.count == 10, "mid-edit: model untouched");

    // 离开外地( E) Esc/arrow 出——一个 Msg) — 该 承诺是坚定的。
    auto [m3, c3] = app::update(std::move(m2), key(form::keys::Intent::MoveNext));
    CHECK(m3.d.persisted.web_search.count == 1, "left the field: committed");
    CHECK(fx::find<agentty::SaveSettings>(c3) != nullptr);
}

TEST_CASE("web search pane: the exclusion text normalises at the tool, not the field") {
    Model m = opened();
    focus_row(m, "web_search.exclude_sites");

    // 输入一个带有URL的杂乱列表,然后离开.
    auto [m1, _1] = app::update(std::move(m), key(form::keys::Intent::Activate));
    std::string typed = "https://www.Pinterest.com/  foo";
    for (char ch : typed) {
        auto [mm, _] = app::update(std::move(m1),
            Msg{WebSearchKey{form::keys::Action{form::keys::Intent::Insert,
                static_cast<char32_t>(static_cast<unsigned char>(ch))}}});
        m1 = std::move(mm);
    }
    auto [m2, c2] = app::update(std::move(m1), key(form::keys::Intent::MoveNext));

    // 电话 面板 显示什么是 带类型的 ; 正常化是工具的工作( )
    // web_search_cfg ),或 面板 编辑 传输 将会对价值产生分歧。
    CHECK(m2.d.persisted.web_search.exclude_sites == typed);
    CHECK(fx::find<agentty::SaveSettings>(c2) != nullptr);
    const auto sites =
        web_search_cfg::exclude_list(m2.d.persisted.web_search.exclude_sites);
    CHECK(sites == std::vector<std::string>{"pinterest.com"});

    // 离开现场承诺:a 保存 被送回了,请看 载荷 类型( SaveSettings ),
    // 而不是从 副作用 描述器别名( ) save_settings )),
    // 它从未出现在a Cmd.
    CHECK(fx::find<agentty::SaveSettings>(c2) != nullptr);
}

TEST_CASE("web search pane: Esc closes back out, and close_msg names it") {
    Model m = opened();
    // close_msg 用于此 覆盖面板 必须是自己的接近:
    // a NoOp 或者通过是保证的陷阱
    const Msg cm = pn::close_msg(pn::Kind::WebSearch);
    CHECK(std::holds_alternative<msg::WebSearchMsg>(cm),
          "close_msg must name this pane's own close, not pass through it");

    auto [m2, _] = app::update(std::move(m), Msg{CloseWebSearch{}});
    CHECK(m2.ui.panel.get<pn::WebSearch>() == nullptr, "closed");
    (void)_;
}

TEST_CASE("web search pane: auto locks the two count rows, and on unlocks them") {
    Model m = opened();   // 默认为自动
    for (const char* id : {"web_search.count", "web_search.max_count"}) {
        const auto* row = pane_of(m).form.find(id);
        REQUIRE(row != nullptr);
        CHECK(row->locked, "auto: the model sets the count");
    }

    // 切换到On通过滴滴:数行解锁.
    focus_row(m, "web_search.mode");
    m = send(std::move(m), key(form::keys::Intent::Activate));
    m = send(std::move(m), key(form::keys::Intent::MenuNext));
    m = send(std::move(m), key(form::keys::Intent::MenuNext));
    m = send(std::move(m), key(form::keys::Intent::MenuPrev));
    m = send(std::move(m), key(form::keys::Intent::MenuCommit));
    CHECK(web_search_cfg::mode(m.d.persisted.web_search) == web_search_cfg::Mode::On);
    for (const char* id : {"web_search.count", "web_search.max_count"}) {
        const auto* row = pane_of(m).form.find(id);
        REQUIRE(row != nullptr);
        CHECK(!row->locked, "on: the counts are the user's to set");
    }
    // 永远不要返回 无论是汽车还是汽车。
    const auto* ex = pane_of(m).form.find("web_search.exclude_sites");
    REQUIRE(ex != nullptr);
    CHECK(!ex->locked);
}

TEST_CASE("web search pane: auto leaves Never return editable") {
    Model m = opened();   // 自动
    const auto* ex = pane_of(m).form.find("web_search.exclude_sites");
    REQUIRE(ex != nullptr);
    CHECK(!ex->locked, "exclusions apply in auto, so the row is live");
}

TEST_CASE("web search pane: off locks every row but the switch") {
    Model m = opened_in("off");
    for (const char* id : {"web_search.count", "web_search.max_count", "web_search.exclude_sites"}) {
        const auto* row = pane_of(m).form.find(id);
        REQUIRE(row != nullptr);
        CHECK(row->locked, "off: nothing is searched, so nothing here applies");
    }
    const auto* sw = pane_of(m).form.find("web_search.mode");
    REQUIRE(sw != nullptr);
    CHECK(!sw->locked, "the way back out must stay open");
}

TEST_CASE("web search pane: a locked count row ignores keys and saves nothing") {
    Model m = opened();   // 自动: 已锁定的数行
    focus_row(m, "web_search.count");
    auto [m1, c1] = app::update(std::move(m), key(form::keys::Intent::AdjustUp));
    CHECK(m1.d.persisted.web_search.count == 10);
    CHECK(fx::find<agentty::SaveSettings>(c1) == nullptr);
    auto [m2, c2] = app::update(std::move(m1), key(form::keys::Intent::Activate));
    CHECK(!pane_of(m2).form.editing(), "a locked row cannot be entered");
    (void)c2;
}

TEST_CASE("web search pane: switching mode keeps the stored counts") {
    // 所设置的计数必须从一次汽车旅行中幸存下来:apply_form
    // 跳过锁定的行,因此,汽车不能用它显示的东西来覆盖它们.
    Model m;
    m.d.persisted.web_search.mode_text = "on";
    m.d.persisted.web_search.count = 7;
    m = opened(std::move(m));
    focus_row(m, "web_search.mode");
    m = send(std::move(m), key(form::keys::Intent::Activate));
    m = send(std::move(m), key(form::keys::Intent::MenuPrev));    // 自动
    m = send(std::move(m), key(form::keys::Intent::MenuCommit));
    REQUIRE(web_search_cfg::mode(m.d.persisted.web_search) == web_search_cfg::Mode::Auto);
    CHECK(m.d.persisted.web_search.count == 7);
}

// ── 倒退:编辑必须经受住不止一个键盘的冲击 ───────────
//
// 报告: "Most results allowed" 接受一个数字,
// Backspace / Delete 什么都没做 "Never return"。
// 一个原因:关键臂从 已保存 配置于 每个 `changed` ,
// 包括中 -edit 键,它重置行,并在每次按键后删除编辑焦点。
// 上方的测试仅此一次 带类型的 一次,只检查过,什么都没有 已保存 键键,
// 所以它看不到。这些键键键键可以键入数键,然后检查 FORM 不只是,
// 我们 Model.

namespace {

Msg ins(char32_t c) {
    return Msg{WebSearchKey{form::keys::Action{form::keys::Intent::Insert, c}}};
}

std::int64_t number_of(const Model& m, std::string_view id) {
    const auto* f = pane_of(m).form.find(id);
    REQUIRE(f != nullptr);
    const auto* n = std::get_if<form::field::Number>(&f->value);
    REQUIRE(n != nullptr);
    return form::number_value(*n);
}

std::string text_of(const Model& m, std::string_view id) {
    const auto* f = pane_of(m).form.find(id);
    REQUIRE(f != nullptr);
    const auto* t = std::get_if<form::field::Text>(&f->value);
    REQUIRE(t != nullptr);
    return t->value;
}

} // 命名空间结束。

TEST_CASE("web search pane: a number row takes more than one digit") {
    Model m = opened_in("on");
    focus_row(m, "web_search.max_count");
    m = send(std::move(m), key(form::keys::Intent::Activate));   // 编辑
    REQUIRE(pane_of(m).form.editing());

    // 3 键然后在新编辑会话上为 5: 第一个数字替换 已保存
    // 20(窗体层) "fresh" 规则),第二附录.
    m = send(std::move(m), ins(U'3'));
    CHECK(pane_of(m).form.editing(), "still editing after the first digit");
    CHECK(number_of(m, "web_search.max_count") == 3);
    m = send(std::move(m), ins(U'5'));
    CHECK(pane_of(m).form.editing(), "still editing after the second digit");
    CHECK(number_of(m, "web_search.max_count") == 35);
    CHECK(m.d.persisted.web_search.max_count == 20, "nothing saved mid-edit");

    // 数字上的Backspace 将最后的位数放下,并保留在编辑中.
    m = send(std::move(m), key(form::keys::Intent::Backspace));
    CHECK(number_of(m, "web_search.max_count") == 3);
    m = send(std::move(m), ins(U'8'));
    CHECK(number_of(m, "web_search.max_count") == 38);

    m = send(std::move(m), key(form::keys::Intent::LeaveField));
    CHECK(m.d.persisted.web_search.max_count == 38, "committed on leaving");
}

TEST_CASE("web search pane: Backspace and Delete edit the exclusion text") {
    Model m = opened();
    focus_row(m, "web_search.exclude_sites");
    m = send(std::move(m), key(form::keys::Intent::Activate));
    REQUIRE(pane_of(m).form.editing());

    for (char32_t c : std::u32string{U"abc.com"}) m = send(std::move(m), ins(c));
    CHECK(text_of(m, "web_search.exclude_sites") == "abc.com");

    m = send(std::move(m), key(form::keys::Intent::Backspace));
    CHECK(text_of(m, "web_search.exclude_sites") == "abc.co", "Backspace removes one");
    CHECK(pane_of(m).form.editing(), "and the field stays in edit");

    m = send(std::move(m), key(form::keys::Intent::CaretHome));
    m = send(std::move(m), key(form::keys::Intent::DeleteForward));
    CHECK(text_of(m, "web_search.exclude_sites") == "bc.co", "Delete removes under the caret");

    m = send(std::move(m), key(form::keys::Intent::LeaveField));
    CHECK(m.d.persisted.web_search.exclude_sites == "bc.co");
}

TEST_CASE("web search pane: the exclusion row says what separates domains") {
    const auto* d = reg::find("web_search.exclude_sites");
    REQUIRE(d != nullptr);
    const std::string help{d->help};
    CHECK(help.find("space") != std::string::npos);
    CHECK(help.find("comma") != std::string::npos);
}

// ── 服务:通过真正的状态更新函数提供钥匙和插槽 ───────────────────
//
// 电话 注册表 以上行为平面设置。 这些是非: a 带
// 类型的 API 键转到密钥存储器, 不 编号 Model ,
// 而一个槽更改重建其下各行。

namespace {

struct KeyHome {
    std::filesystem::path dir;
    std::string old_home, old_ks;
    bool had_home = false, had_ks = false;
    KeyHome() {
        dir = std::filesystem::temp_directory_path()
            / ("agentty_wt_keys_" + std::to_string(::getpid()));
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir / "home");
        if (const char* h = ::getenv("AGENTTY_HOME")) { had_home = true; old_home = h; }
        if (const char* k = ::getenv("AGENTTY_USE_KEYSTORE")) { had_ks = true; old_ks = k; }
        ::setenv("AGENTTY_HOME", (dir / "home").c_str(), 1);
        ::setenv("AGENTTY_USE_KEYSTORE", "0", 1);
        ::unsetenv("AGENTTY_WEB_SEARCH_KEY_EXA_API");
        // keystore::available ()每个进程都缓存;
        // 如果在运行之前就已装有武器,则a store() 就能到达开发商真正的键盘
        real_keyring = agentty::auth::keystore::available();
    }
    bool real_keyring = false;
    ~KeyHome() {
        if (had_home) ::setenv("AGENTTY_HOME", old_home.c_str(), 1);
        else          ::unsetenv("AGENTTY_HOME");
        if (had_ks) ::setenv("AGENTTY_USE_KEYSTORE", old_ks.c_str(), 1);
        else        ::unsetenv("AGENTTY_USE_KEYSTORE");
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};

// update() ,然后运行它返回的任务(关于此项) 线程 ,
// 就像内核会在一个 工作线程 并反馈他们的答案--直到一无所有。
// 钥匙店是 IO ,所以,请 面板 将其交给任务;
// 这是测试如何看待整个无内核的往返。
Model settle(Model m, Msg msg) {
    std::vector<Msg> queue;
    queue.push_back(std::move(msg));
    for (int round = 0; !queue.empty(); ++round) {
        REQUIRE(round < 32);   // 总是会开始另一个任务
        std::vector<Msg> next;
        for (auto& q : queue) {
            auto [mm, cmd] = app::update(std::move(m), std::move(q));
            m = std::move(mm);
            for (auto& r : fx::run_tasks(std::move(cmd))) next.push_back(std::move(r));
        }
        queue = std::move(next);
    }
    return m;
}

Model with_exa_primary() {
    Model m;
    m.d.persisted.web_search.primary = "exa-api";
    return settle(std::move(m), Msg{OpenWebSearch{}});
}

void type_text(Model& m, std::string_view text) {
    for (char c : text) m = send(std::move(m), ins(static_cast<char32_t>(c)));
}

} // 命名空间结束。

TEST_CASE("web search pane: a typed API key is stored, not kept in the Model") {
    KeyHome home;
    if (home.real_keyring) { MESSAGE("skipped: OS keystore armed for this process"); return; }
    Model m = with_exa_primary();
    REQUIRE(pane_of(m).form.find("web_search.key.exa-api") != nullptr);
    CHECK(pane_of(m).form.find("web_search.key.exa-api")->origin.empty(),
          "no key: the widget's own \"not set\" speaks, nothing is repeated");
    CHECK(std::get<form::field::Secret>(
              pane_of(m).form.find("web_search.key.exa-api")->value).placeholder.empty());
    CHECK(!pane_of(m).form.find("web_search.key.exa-api")->locked);

    focus_row(m, "web_search.key.exa-api");
    m = send(std::move(m), key(form::keys::Intent::Activate));      // 开始编辑
    REQUIRE(pane_of(m).form.editing());
    type_text(m, "exa-SECRET-0123456789");
    // 中间 -edit 字行中,将文本和 Model 什么都没有
    CHECK(wtf::key_row_service(pane_of(m).form.focused()->id) == "exa-api");
    // 离开球场: 状态更新函数 空出行并返回存储密钥的任务。
    // 它没有 IO 自己来
    {
        auto [m2, cmd] = app::update(std::move(m), key(form::keys::Intent::LeaveField));
        CHECK(!agentty::tools::web_search_secret::has("exa-api"),
              "the reducer stored nothing itself");
        CHECK(std::get<form::field::Secret>(
                  pane_of(m2).form.find("web_search.key.exa-api")->value).value.empty(),
              "the typed key left the row at once");
        m = std::move(m2);
        for (auto& r : fx::run_tasks(std::move(cmd))) m = settle(std::move(m), std::move(r));
    }

    CHECK(agentty::tools::web_search_secret::load("exa-api") == "exa-SECRET-0123456789");
    // 排空了,现在说关键在哪里生活, 从来没有是什么。
    const auto* row = pane_of(m).form.find("web_search.key.exa-api");
    REQUIRE(row != nullptr);
    CHECK(std::get<form::field::Secret>(row->value).value.empty());
    CHECK(std::get<form::field::Secret>(row->value).placeholder == "\xe2\x9c\x93 key saved");
    CHECK(row->origin.empty());
    CHECK(row->locked, "a stored key locks its row until it is removed");
    // 电话 Model (这样) settings.json 从来没有见过。
    CHECK(m.d.persisted.web_search.primary == "exa-api");
    CHECK(pane_of(m).form.find("web_search.keyclear.exa-api") != nullptr);
}

TEST_CASE("web search pane: a paste on a focused, empty key row lands without a keystroke first") {
    KeyHome home;
    if (home.real_keyring) { MESSAGE("skipped: OS keystore armed for this process"); return; }
    Model m = with_exa_primary();
    focus_row(m, "web_search.key.exa-api");
    REQUIRE(!pane_of(m).form.editing());
    m = send(std::move(m), Msg{WebSearchPaste{"exa-PASTED-0123456789"}});
    CHECK(pane_of(m).form.editing());
    CHECK(std::get<form::field::Secret>(
              pane_of(m).form.find("web_search.key.exa-api")->value).value
          == "exa-PASTED-0123456789");

    // ... 而 Enter 单独存储它: 所报流量, 结束到结束。
    // 列然后说, 被锁定, 并提供删除密钥。
    m = settle(std::move(m), key(form::keys::Intent::LeaveField));
    CHECK(agentty::tools::web_search_secret::load("exa-api") == "exa-PASTED-0123456789");
    const auto* row = pane_of(m).form.find("web_search.key.exa-api");
    REQUIRE(row != nullptr);
    CHECK(std::get<form::field::Secret>(row->value).value.empty());
    CHECK(std::get<form::field::Secret>(row->value).placeholder == "\xe2\x9c\x93 key saved");
    CHECK(row->locked);
    CHECK(pane_of(m).form.find("web_search.keyclear.exa-api") != nullptr);
}

TEST_CASE("web search pane: a paste on a row that takes no text is dropped") {
    // 面板现在每开时就收到一个贴面,而不仅是-edit中,
    // 所以状态更新函数使得一个贴面不出现在选择行(并且
    // 退出面板后面的作曲).
    Model m = opened();
    focus_row(m, "web_search.primary");
    const auto before = m.d.persisted.web_search;
    m = send(std::move(m), Msg{WebSearchPaste{"exa-api"}});
    CHECK(!pane_of(m).form.editing());
    CHECK(!pane_of(m).form.choosing());
    CHECK(m.d.persisted.web_search == before);
}

TEST_CASE("web search pane: a paste on a locked key row is dropped") {
    KeyHome home;
    if (home.real_keyring) { MESSAGE("skipped: OS keystore armed for this process"); return; }
    REQUIRE(agentty::tools::web_search_secret::store(::agentty::IoAccess::grant(), agtest::cap(), "exa-api", "exa-STORED-0123456789"));
    Model m = with_exa_primary();
    focus_row(m, "web_search.key.exa-api");
    m = send(std::move(m), Msg{WebSearchPaste{"exa-PASTED-0123456789"}});
    CHECK(!pane_of(m).form.editing());
    CHECK(std::get<form::field::Secret>(
              pane_of(m).form.find("web_search.key.exa-api")->value).value.empty());
}

TEST_CASE("web search pane: opening reads key origins on a worker, not in the reducer") {
    KeyHome home;
    if (home.real_keyring) { MESSAGE("skipped: OS keystore armed for this process"); return; }
    REQUIRE(agentty::tools::web_search_secret::store(::agentty::IoAccess::grant(), agtest::cap(), "exa-api", "exa-STORED-0123456789"));
    Model m;
    m.d.persisted.web_search.primary = "exa-api";
    auto [m1, cmd] = app::update(std::move(m), Msg{OpenWebSearch{}});
    // 在读数答复前,面板不主张两者均不主张。
    const auto* row = pane_of(m1).form.find("web_search.key.exa-api");
    REQUIRE(row != nullptr);
    CHECK(!row->locked);
    CHECK(std::get<form::field::Secret>(row->value).placeholder == "checking\xe2\x80\xa6");
    // 读取是任务;其答案填充了来源.
    auto replies = fx::run_tasks(std::move(cmd));
    REQUIRE(replies.size() == 1);
    Model m2 = std::move(m1);
    for (auto& r : replies) m2 = settle(std::move(m2), std::move(r));
    CHECK(std::get<form::field::Secret>(
              pane_of(m2).form.find("web_search.key.exa-api")->value).placeholder
          == "\xe2\x9c\x93 key saved");
    CHECK(pane_of(m2).form.find("web_search.key.exa-api")->locked);
    CHECK(pane_of(m2).form.find("web_search.keyclear.exa-api") != nullptr);
}

TEST_CASE("web search pane: a stale key read does not undo a newer change") {
    KeyHome home;
    if (home.real_keyring) { MESSAGE("skipped: OS keystore armed for this process"); return; }
    Model m;
    m.d.persisted.web_search.primary = "exa-api";
    auto [opened_m, open_cmd] = app::update(std::move(m), Msg{OpenWebSearch{}});
    Model m1 = std::move(opened_m);
    // 打开的读数结束,但答案还没有折叠...
    auto stale = fx::run_tasks(std::move(open_cmd));
    // 当 面板 开始一个商店,它会撞倒它的一代。
    m1.ui.panel.get<pn::WebSearch>()->keys_gen += 1;
    // 所以旧的答案被放弃了,而不是显示在较新的状态上.
    for (auto& r : stale) {
        auto [mm, cmd] = app::update(std::move(m1), std::move(r));
        m1 = std::move(mm);
        CHECK(fx::run_tasks(std::move(cmd)).empty());
    }
    CHECK(!pane_of(m1).keys_read);
}

TEST_CASE("web search pane: key origins that arrive mid-edit are shown when the edit ends") {
    KeyHome home;
    if (home.real_keyring) { MESSAGE("skipped: OS keystore armed for this process"); return; }
    REQUIRE(agentty::tools::web_search_secret::store(::agentty::IoAccess::grant(), agtest::cap(), "exa-api", "exa-STORED-0123456789"));
    Model m;
    m.d.persisted.web_search.primary = "exa-api";
    auto [m1, open_cmd] = app::update(std::move(m), Msg{OpenWebSearch{}});
    Model mm = std::move(m1);
    // 在读取答案前开始编辑文本行...
    focus_row(mm, "web_search.exclude_sites");
    mm = send(std::move(mm), key(form::keys::Intent::Activate));
    REQUIRE(pane_of(mm).form.editing());
    for (auto& r : fx::run_tasks(std::move(open_cmd))) mm = settle(std::move(mm), std::move(r));
    // ...编辑没有被干扰...
    CHECK(pane_of(mm).form.editing());
    // ...没有改变(Esc:没有犯罪),仍然显示起源。
    mm = send(std::move(mm), key(form::keys::Intent::LeaveField));
    CHECK(!pane_of(mm).form.editing());
    CHECK(pane_of(mm).form.find("web_search.key.exa-api")->locked);
}

TEST_CASE("web search pane: a key that does not look like one is refused, not stored") {
    KeyHome home;
    if (home.real_keyring) { MESSAGE("skipped: OS keystore armed for this process"); return; }
    Model m = with_exa_primary();
    focus_row(m, "web_search.key.exa-api");
    m = send(std::move(m), key(form::keys::Intent::Activate));
    type_text(m, "nope");
    m = settle(std::move(m), key(form::keys::Intent::LeaveField));
    CHECK(!agentty::tools::web_search_secret::has("exa-api"));
    CHECK(std::get<form::field::Secret>(
              pane_of(m).form.find("web_search.key.exa-api")->value).value.empty());
    CHECK(!pane_of(m).form.find("web_search.key.exa-api")->locked);
}

TEST_CASE("web search pane: Remove key forgets a stored key") {
    KeyHome home;
    if (home.real_keyring) { MESSAGE("skipped: OS keystore armed for this process"); return; }
    REQUIRE(agentty::tools::web_search_secret::store(::agentty::IoAccess::grant(), agtest::cap(), "exa-api", "exa-STORED-0123456789"));
    Model m = with_exa_primary();
    REQUIRE(pane_of(m).form.find("web_search.keyclear.exa-api") != nullptr);
    focus_row(m, "web_search.keyclear.exa-api");
    m = settle(std::move(m), key(form::keys::Intent::Activate));
    CHECK(!agentty::tools::web_search_secret::has("exa-api"));
    CHECK(pane_of(m).form.find("web_search.keyclear.exa-api") == nullptr);
    CHECK(!pane_of(m).form.find("web_search.key.exa-api")->locked,
          "removing the key unlocks the row for a new one");
    CHECK(std::get<form::field::Secret>(
              pane_of(m).form.find("web_search.key.exa-api")->value).placeholder.empty());
    // 删除行已消失, 光标会降落在它所服务的密钥行上。
    REQUIRE(pane_of(m).form.focused() != nullptr);
    CHECK(pane_of(m).form.focused()->id == "web_search.key.exa-api");
}

TEST_CASE("web search pane: picking a keyed service for a slot brings its key row") {
    KeyHome home;
    if (home.real_keyring) { MESSAGE("skipped: OS keystore armed for this process"); return; }
    Model m = opened();
    CHECK(pane_of(m).form.find("web_search.key.exa-api") == nullptr);
    // 主降级:打开,步向按键服务,承诺.
    focus_row(m, "web_search.primary");
    m = send(std::move(m), key(form::keys::Intent::Activate));
    for (int i = 0; i < 7; ++i) m = send(std::move(m), key(form::keys::Intent::MenuNext));
    m = send(std::move(m), key(form::keys::Intent::MenuCommit));
    CHECK(m.d.persisted.web_search.primary == "exa-api");
    CHECK(pane_of(m).form.find("web_search.key.exa-api") != nullptr);
    CHECK(pane_of(m).form.find("web_search.dial.exa-api.type") != nullptr);
    // 旧服务栏目从面板(其拨号保持记录)走光.
    CHECK(pane_of(m).form.find("web_search.dial.brave-free.freshness") == nullptr);
    // 重建在Prime下添加并移除了行;光标保持ON
    // Prime,而不是在任何行滑入其旧位置上.
    REQUIRE(pane_of(m).form.focused() != nullptr);
    CHECK(pane_of(m).form.focused()->id == "web_search.primary");
}

TEST_CASE("web search pane: choosing a dial value persists it") {
    KeyHome home;
    if (home.real_keyring) { MESSAGE("skipped: OS keystore armed for this process"); return; }
    Model m = opened();
    focus_row(m, "web_search.dial.brave-free.freshness");
    m = send(std::move(m), key(form::keys::Intent::Activate));
    m = send(std::move(m), key(form::keys::Intent::MenuNext));      // 任何时候 - > 过去一天
    m = send(std::move(m), key(form::keys::Intent::MenuNext));      // - > 上个星期
    auto [m2, cmd] = app::update(std::move(m), key(form::keys::Intent::MenuCommit));
    CHECK(m2.d.persisted.web_search.dials.at("brave-free.freshness") == "pw");
    const auto* saved = fx::find<agentty::SaveSettings>(cmd);
    REQUIRE(saved != nullptr);
    CHECK(saved->settings.web_search.dials.at("brave-free.freshness") == "pw");
}
