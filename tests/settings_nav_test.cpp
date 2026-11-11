// settings_nav_test — Esc 谓 BACK 一个 LEVEL ,没有 "exit".
//
// 设置 面板 有三个途径: 直接和弦( / / 或 / i / ) ^S),
// a Ctrl +K 调色板行,以及设置列表( Ctrl +K =
// 设置 = 检索 / 智能模式。 Esc 土地应该取决于你们曾经使用
// 过的哪一种土地, 面板 无法从常数中回答, 处理函数 用于尝试:
// CloseRag 总是打开调色板, CloseSmartMode
// 总是掉到 线程。每个入口对准一个入口,其余则不对,
// 设置不对 -list 手执 -offs 直接的陷阱门(在打开目标
// 前就关闭了名单),所以 Esc 完全失去了你的位置。 )
//
// 每一个 面板 现在 CARRIES a 快照 打开的面板 pn::From ),
// 所以,(a) ,(a, ) "return to the wrong place"
// 停止代表——返回是 完整 状态、查询和光标,而不是重建。
// 这些测试会在每个入口处进行,并按下 Esc ,通过真实 app::update
// 状态更新函数.

#include "agtest.hpp"

#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/panel/form_keys.hpp"
#include "agentty/runtime/panel/settings/categories.hpp"
#include "agentty/runtime/panel/settings/items.hpp"   // items_for,行动
#include "agentty/runtime/panel/rag.hpp"
#include "agentty/runtime/settings_registry.hpp"
#include "agentty/runtime/view/palette.hpp"   // ui::palette_context

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
        .new_thread_id  = [] { return ThreadId{"t-nav"}; },
        .title_from     = [](std::string_view t) { return std::string{t}; },
        .auth           = {},
    });
}

// Esc ,用户按下:通过真实的键_intent_ 状态更新函数 路径。
//
// 电话 RAG 面板 推迟关闭时间 a Cmd (表层)
// `applied.close` 成为 RagEmbedClose 下个
// 勾选时发来的消息),同步测试从未运行过——所以下面是 -up 这是你们的财产,
// 这是你们的财产 面板 合成管道,不是导航测试的
Model press_escape(Model m) {
    const auto esc = form::keys::Action{form::keys::Intent::Close};
    if (m.ui.panel.is<pn::SmartMode>())
        return app::update(std::move(m), Msg{SmartModeKey{esc}}).first;
    if (m.ui.panel.is<pn::Rag>()) {
        m = app::update(std::move(m), Msg{RagEmbedKey{esc}}).first;
        if (m.ui.panel.is<pn::Rag>())
            m = app::update(std::move(m), Msg{RagEmbedClose{}}).first;
        return m;
    }
    if (m.ui.panel.is<pn::SettingsList>())
        return app::update(std::move(m), Msg{CloseSettingsList{}}).first;
    if (m.ui.panel.is<pn::Stats>())
        return app::update(std::move(m), Msg{CloseStats{}}).first;
    return m;
}

// 打开调色板然后运行 `row` 用户的操作方式: 输入其标签后,
// 即可留下单对子 Enter.
//
// 故意的NOT "compute the index" – 光标
// 索引了FILTERED列表,过滤依赖于从活的Model(待
// 处理 更改,代码块,所有闸门行的可用更新)中衍生出来的
// PaletteContext。一个建立自身上下文的测试索引了一个
// 不同于状态更新函数的列表,并默默地选择了错误的命令,这正是这个帮助者
// 在第一次尝试时所做的.
Model run_from_palette(Model m, std::string_view query, Command expect) {
    m = app::update(std::move(m), Msg{OpenPalette{}}).first;
    for (char c : query)
        m = app::update(std::move(m),
                        Msg{PaletteInput{static_cast<char32_t>(c)}}).first;
    (void)expect;   // 以可读性在 调用 站点命名
    return app::update(std::move(m), Msg{PaletteSelect{}}).first;
}

} // 命名空间结束。

TEST_CASE("settings nav: ^S then Esc closes to the thread") {
    // 直接的和弦不是导航——你没有从任何地方降下,
    // 所以没有什么可以回和 Esc 表示 "done".
    install_stub_deps();
    Model m;
    m = app::update(std::move(m), Msg{OpenSmartMode{}}).first;
    REQUIRE(m.ui.panel.is<pn::SmartMode>());

    m = press_escape(std::move(m));
    CHECK(m.ui.panel.is<pn::None>());
}

TEST_CASE("settings nav: palette → pane → Esc returns to the palette") {
    // ...与那个 完整 调色板 描述用户左侧的: 他们的查询 带类型
    // 的 和匹配行上的光标。 (旧源代码) -reconstruction
    // 重设置查询到 "" 并重计算了光标; 快照 刚刚恢复。 )
    //
    // 驱动通过行 调色板 仍然。智能模式和检索器曾经
    // 在这里测试;它们现在是设置行( ^S 和设置 ~ 常规),
    // 所以调色板 -origin 合同使用保留的命令
    // 执行: Stats 和设置列表本身。
    // 正在测试的是 snapshot/restore ,
    // 而不是哪个 面板 恰好在它的最远。
    install_stub_deps();
    {
        Model m = run_from_palette(Model{}, "stats", Command::OpenStats);
        REQUIRE(m.ui.panel.is<pn::Stats>());

        m = press_escape(std::move(m));
        REQUIRE(m.ui.panel.is<pn::Palette>());
        const auto* p = m.ui.panel.get<pn::Palette>();
        CHECK(p->query == "stats");
        // 光标必须仍然降落在运行的命令上——通过 相同 过滤后
        // 列出视图 渲染 ,反对 LIVE 上下文( 重新验证为
        // ascend() ,如果在 面板 )),"("当时是
        // 开放的").
        const auto matches = filtered_commands(p->query, ui::palette_context(m));
        REQUIRE(!matches.empty());
        REQUIRE(p->index >= 0);
        REQUIRE(p->index < static_cast<int>(matches.size()));
        CHECK(matches[static_cast<std::size_t>(p->index)]->id
              == Command::OpenStats);
    }
    {
        Model m = run_from_palette(Model{}, "settings",
                                   Command::OpenGeneralSettings);
        REQUIRE(m.ui.panel.is<pn::SettingsList>());

        m = press_escape(std::move(m));
        REQUIRE(m.ui.panel.is<pn::Palette>());
        const auto* p = m.ui.panel.get<pn::Palette>();
        CHECK(p->query == "settings");
        const auto matches = filtered_commands(p->query, ui::palette_context(m));
        REQUIRE(!matches.empty());
        REQUIRE(p->index >= 0);
        REQUIRE(p->index < static_cast<int>(matches.size()));
        CHECK(matches[static_cast<std::size_t>(p->index)]->id
              == Command::OpenGeneralSettings);
    }
}

TEST_CASE("settings nav: settings list → pane → Esc returns to the list") {
    // 捕捉门 激活 "Retrieval" 或 或 时 间
    // "Smart Mode" 从设置列表中关闭列表并打开目标,
    // 且无 内存 这么简单 Esc 降两级到 线程 ——用户读成
    // 堆栈的行为是不会预料到的.
    install_stub_deps();

    const auto descend = [](settings::Action want) {
        Model m;
        m = app::update(std::move(m),
                        Msg{OpenSettingsList{settings::Category::General}}).first;
        REQUIRE(m.ui.panel.is<pn::SettingsList>());

        // 走到行上激活它,而不是到达状态更新函数
        // ——这是用户所走的道路.
        auto* o = m.ui.panel.get<pn::SettingsList>();
        const auto rows = settings::items_for(m, o->concern);
        int idx = -1;
        for (int i = 0; i < static_cast<int>(rows.size()); ++i)
            if (rows[static_cast<std::size_t>(i)].action == want)
                idx = i;
        REQUIRE(idx >= 0);
        // 排行不是第一,或者说 "remembered the row" 财务报告和已审计财务报表
        // "reset to the top" 也一样
        REQUIRE(idx > 0);
        m.ui.panel.get<pn::SettingsList>()->index = idx;
        m = app::update(std::move(m), Msg{SettingsListActivate{}}).first;
        return std::pair{std::move(m), idx};
    };

    {
        auto [m, idx] = descend(settings::Action::OpenSmart);
        REQUIRE(m.ui.panel.is<pn::SmartMode>());
        m = press_escape(std::move(m));
        REQUIRE(m.ui.panel.is<pn::SettingsList>());
        CHECK(m.ui.panel.get<pn::SettingsList>()->concern
              == settings::Category::General);
        CHECK(m.ui.panel.get<pn::SettingsList>()->index == idx);
    }
    {
        auto [m, idx] = descend(settings::Action::OpenRag);
        REQUIRE(m.ui.panel.is<pn::Rag>());
        m = press_escape(std::move(m));
        REQUIRE(m.ui.panel.is<pn::SettingsList>());
        CHECK(m.ui.panel.get<pn::SettingsList>()->concern
              == settings::Category::General);
        CHECK(m.ui.panel.get<pn::SettingsList>()->index == idx);
    }
}

namespace {

// 将光标放在 `id`; 需要QUIREs 该行存在。
void focus_on(form::Form& f, std::string_view id) {
    REQUIRE_MESSAGE(form::focus_row(f, id), "no row " << std::string{id});
}

form::keys::Action intent(form::keys::Intent i) { return form::keys::Action{i}; }
form::keys::Action digit(char32_t c) {
    form::keys::Action a{form::keys::Intent::Insert};
    a.ch = c;
    return a;
}

} // 命名空间结束。

TEST_CASE("settings fields: a RAG pipeline toggle saves on its own") {
    // 切换 从未设置 left_field ,并且仅 left_field
    // 曾经是 保存 ,所以每个管道开关都恢复了
    install_stub_deps();
    g_settings = {};
    Model m;
    m.d.persisted.rag.dedup = true;
    m = app::update(std::move(m), Msg{OpenRag{}}).first;
    auto* rag = m.ui.panel.get<pn::Rag>();
    REQUIRE(rag != nullptr);
    auto* f = &rag->embed.form;
    focus_on(*f, "rag.dedup");
    auto next = app::update(std::move(m),
                    Msg{RagEmbedKey{intent(form::keys::Intent::AdjustUp)}}).first;
    CHECK(next.d.persisted.rag.dedup == false);
    CHECK(next.d.persisted.rag.configured);
}

TEST_CASE("settings fields: typing a Smart Mode number keeps the edit, commits on leave") {
    // 每个数字都用来重建窗体,它以一个键结束编辑.
    install_stub_deps();
    Model m;
    m = app::update(std::move(m), Msg{OpenSmartMode{}}).first;
    m = app::update(std::move(m), Msg{SmartModeAdvanced{}}).first;
    auto* o = m.ui.panel.get<pn::SmartMode>();
    REQUIRE(o != nullptr);
    focus_on(o->form, "smart.deep_margin");
    const auto* d = settings::registry::find("smart.deep_margin");
    REQUIRE(d != nullptr);
    const auto want = static_cast<int>(d->max);
    REQUIRE(want != m.d.smart.deep_margin);

    // Enter, Backspace 数字 Enter : 编辑器内数个键盘键。
    m = app::update(std::move(m), Msg{SmartModeKey{intent(form::keys::Intent::Activate)}}).first;
    m = app::update(std::move(m), Msg{SmartModeKey{intent(form::keys::Intent::Backspace)}}).first;
    m = app::update(std::move(m),
                    Msg{SmartModeKey{digit(static_cast<char32_t>('0' + want))}}).first;
    o = m.ui.panel.get<pn::SmartMode>();
    REQUIRE(o != nullptr);
    CHECK(o->form.editing());
    // Enter 编辑时翻译为 LeaveField.
    m = app::update(std::move(m), Msg{SmartModeKey{intent(form::keys::Intent::LeaveField)}}).first;
    CHECK(m.d.smart.deep_margin == want);
}

TEST_CASE("settings nav: every list is reachable from Settings") {
    // 插件、命令、潜水器和钩子在调色板行走时失去了唯一的通道。
    // 每个都必须在中心设置一个门,
    // 清单上的Esc必须降落在那个门上。
    install_stub_deps();
    for (auto want : {settings::Category::Plugins, settings::Category::Commands,
                      settings::Category::Agents, settings::Category::Hooks}) {
        Model m;
        m = app::update(std::move(m),
                        Msg{OpenSettingsList{settings::Category::General}}).first;
        const auto rows = settings::items_for(m, settings::Category::General);
        int idx = -1;
        for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
            const auto& r = rows[static_cast<std::size_t>(i)];
            if (r.action == settings::Action::OpenList && r.target == want) idx = i;
        }
        REQUIRE(idx >= 0);
        m.ui.panel.get<pn::SettingsList>()->index = idx;
        m = app::update(std::move(m), Msg{SettingsListActivate{}}).first;
        REQUIRE(m.ui.panel.is<pn::SettingsList>());
        CHECK(m.ui.panel.get<pn::SettingsList>()->concern == want);

        m = press_escape(std::move(m));
        REQUIRE(m.ui.panel.is<pn::SettingsList>());
        CHECK(m.ui.panel.get<pn::SettingsList>()->concern
              == settings::Category::General);
        CHECK(m.ui.panel.get<pn::SettingsList>()->index == idx);
    }
}

TEST_CASE("settings nav: General settings opens from the palette") {
    // 普通类( 外观 / 智能模式 / 检索) 切换 ) 已完全建成——行,
    // 标, 状态更新函数 武器——但 NO 命令打开它:
    // 已死亡 UI 直到 "Settings" 调色板行已落地。 硬币可达性 + 秒
    // Esc 链条。
    install_stub_deps();
    Model m = run_from_palette(Model{}, "settings",
                               Command::OpenGeneralSettings);
    REQUIRE(m.ui.panel.is<pn::SettingsList>());
    CHECK(m.ui.panel.get<pn::SettingsList>()->concern
          == settings::Category::General);
    m = press_escape(std::move(m));
    CHECK(m.ui.panel.is<pn::Palette>());   // 快照 恢复,如同各地一样
}

TEST_CASE("settings nav: the origin survives a slot round trip") {
    // 将一个模型槽交给模型 选择面板 : 数据 整个
    // SmartMode 面板 (形式、高级、自己的母链) -mode
    // 选择面板's `from` 快照 ,和 Esc 恢复——与
    // 面板 还在知道在哪里打开 FROM.
    //
    // 通过设置列表到达,这就是智能模式现在居住的
    // 地方( ^S 测试的源头是另一扇门。
    // 面板 记录在路上, 无论是什么来源恰好。
    install_stub_deps();
    Model m;
    m = app::update(std::move(m),
                    Msg{OpenSettingsList{settings::Category::General}}).first;
    REQUIRE(m.ui.panel.is<pn::SettingsList>());
    {
        auto* o = m.ui.panel.get<pn::SettingsList>();
        const auto rows = settings::items_for(m, o->concern);
        int idx = -1;
        for (int i = 0; i < static_cast<int>(rows.size()); ++i)
            if (rows[static_cast<std::size_t>(i)].action
                    == settings::Action::OpenSmart)
                idx = i;
        REQUIRE(idx >= 0);
        o->index = idx;
    }
    m = app::update(std::move(m), Msg{SettingsListActivate{}}).first;
    REQUIRE(m.ui.panel.is<pn::SmartMode>());

    // 进入 选择面板 编号 真实 方式:手 -off descend()
    // 指定 -mode 载有 面板 中文本无 快照.
    {
        pn::Models picker{{0, ""}, {}, smart::ModelRole::Strategic};
        m.ui.panel.descend(std::move(picker));
    }
    m = app::update(std::move(m), Msg{OpenModels{}}).first;
    REQUIRE(m.ui.panel.is<pn::Models>());
    REQUIRE(m.ui.panel.get<pn::Models>()->assign_slot
            == smart::ModelRole::Strategic);
    m = app::update(std::move(m), Msg{CloseModels{}}).first;

    // 回到原点 面板 ——和 Esc 肯定还知道它从哪里来的
    REQUIRE(m.ui.panel.is<pn::SmartMode>());
    m = press_escape(std::move(m));
    CHECK(m.ui.panel.is<pn::SettingsList>());
}
