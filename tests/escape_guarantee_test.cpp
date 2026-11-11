// 逃生保证:弹出面板永远无法困住用户.
//
// 每一个 EXCLUSIVE 覆盖面板 吞下无人认领的钥匙——这就是
// 它的原因 弹出面板 如此的,一个 覆盖面板 谁的钥匙 处理函数 无法
// 回答 Escape 使用户无路可逃, 应用程序似乎被冻结。
// Retrieval 面板 准确运出: 它拥有一个键盘 帧 之前,它存在,
// 所以每一个关键包括 Esc 哪儿也没有去。
//
// 人数 -handler 勤勉不能强制执行这一点——它是一个缺失的分支,
// 在任何方面都是 处理函数 ,永远。它的结构上是执行在 分派 被堵住的点
// subscribe.cpp : 如果为 覆盖面板 属于自己的 处理函数
// 放弃逃跑, 即 覆盖面板 反之,是普通的近距离射击
//
// 测试结果为EVERY 覆盖面板 Kind,并声称财产结束,
// 因此,将Esc遗忘的新覆盖面板在此失效,而不是某人的终端失效.

#include "agtest.hpp"

#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/app/subscribe.hpp"
#include "agentty/runtime/panel/nav.hpp"
#include "agentty/runtime/panel/smart_form.hpp"
#include "agentty/runtime/panel/web_search_form.hpp"

#include <string>
#include <vector>

namespace pn = agentty::ui::panel;
using namespace agentty;

namespace {

maya::KeyEvent esc() {
    maya::KeyEvent ev;
    ev.key = maya::SpecialKey::Escape;
    return ev;
}

// 每一个善良,和一个 Model 与那个 覆盖面板 打开,
// 就当桌子这样 -Wswitch 内容 close_msg 并且这项试验的
// 覆盖范围明显是相同的清单。
struct Case {
    pn::Kind    kind;
    const char* name;
    void      (*open)(Model&);
};

const std::vector<Case>& cases() {
    static const std::vector<Case> v = {
        {pn::Kind::Palette, "command palette",
         [](Model& m) { m.ui.panel.descend(pn::Palette{{}}); }},
        {pn::Kind::Mention, "mention palette",
         [](Model& m) { m.ui.panel.descend(pn::Mention{{}}); }},
        {pn::Kind::Symbol, "symbol palette",
         [](Model& m) { m.ui.panel.descend(pn::Symbol{{}}); }},
        {pn::Kind::CodeBlocks, "code block picker",
         [](Model& m) { m.ui.panel.descend(pn::CodeBlocks{{}}); }},
        {pn::Kind::ToolOutput, "tool output viewer",
         [](Model& m) { m.ui.panel.descend(pn::ToolOutput{{}}); }},
        {pn::Kind::Checkpoints, "checkpoint picker",
         [](Model& m) { m.ui.panel.descend(pn::Checkpoints{{}}); }},
        {pn::Kind::Rag, "retrieval pane",
         [](Model& m) { m.ui.panel.descend(pn::Rag{{}}); }},
        {pn::Kind::SettingsList, "settings list",
         [](Model& m) { m.ui.panel.descend(pn::SettingsList{{}}); }},
        {pn::Kind::Fork, "fork picker",
         [](Model& m) { m.ui.panel.descend(pn::Fork{{}}); }},
        {pn::Kind::Models, "model picker",
         [](Model& m) { m.ui.panel.descend(pn::Models{{}}); }},
        {pn::Kind::Providers, "provider picker",
         [](Model& m) { m.ui.panel.descend(pn::Providers{{}}); }},
        {pn::Kind::ThreadList, "thread list",
         [](Model& m) { m.ui.panel.descend(pn::ThreadList{{}}); }},
        {pn::Kind::SmartMode, "smart mode",
         [](Model& m) {
             smart_form::Inputs in;
             m.ui.panel.descend(pn::SmartMode{{}, smart_form::build_form(in)});
         }},
        {pn::Kind::WebSearch, "web search",
         [](Model& m) {
             m.ui.panel.descend(pn::WebSearch{{}, web_search_form::build_form(m.d.persisted.web_search, m.env.settings)});
         }},
        {pn::Kind::Stats, "stats viewer",
         [](Model& m) { m.ui.panel.descend(pn::Stats{}); }},
        {pn::Kind::DiffReview, "diff review",
         [](Model& m) { m.ui.panel.descend(pn::DiffReview{{}}); }},
    };
    return v;
}

} // 命名空间结束。

namespace {

store::Settings g_settings;

// 电话 状态更新函数 到达 Store 接口边界
// 关闭( 长期设置、 装入) 线程 ),所以道具必须存在。
// 持久化.
void install_stub_deps() {
    app::install_deps(app::Deps{
        .stream         = [](const maya::unlocked&, auto, auto) {},
        .save_thread    = [](const auto&) {},
        .delete_thread  = [](const auto&) {},
        .load_threads   = [] { return std::vector<Thread>{}; },
        .load_thread    = [](const auto&) -> std::optional<Thread> { return std::nullopt; },
        .load_settings  = [] { return g_settings; },
        .save_settings  = [](const store::Settings& x) { g_settings = x; },
        .new_thread_id  = [] { return ThreadId{}; },
        .title_from     = [](std::string_view t) { return std::string{t}; },
        .auth           = auth::AuthHeader{auth::ApiKeyHeader{std::string{}}},
    });
}

} // 命名空间结束。

TEST_CASE("escape guarantee: close_msg names a real close for every overlay") {
    // 后援必须关闭每个 覆盖面板 — a NoOp
    // 这儿是一个无声的陷阱。 覆盖面板 ,
    // 所以两者合法地地图 NoOp.
    for (const auto& c : cases()) {
        const auto msg = pn::close_msg(c.kind);
        CHECK(!std::holds_alternative<msg::MetaMsg>(msg)
                  || !std::holds_alternative<NoOp>(std::get<msg::MetaMsg>(msg)),
              (std::string{"close_msg is not a NoOp for "} + c.name).c_str());
    }
}

TEST_CASE("escape guarantee: Esc closes every overlay") {
    install_stub_deps();
    // End 打开 覆盖面板 ,喂饱 状态更新函数 其信息,
    // 并要求 覆盖面板。这是用户体验到的地产。
    // "Esc always gets me out" - 而不是代名词
    for (const auto& c : cases()) {
        Model m;
        c.open(m);
        CHECK(pn::kind_of(m.ui.panel) == c.kind,
              (std::string{"opened "} + c.name).c_str());

        auto [after, cmd] = app::update(std::move(m), pn::close_msg(c.kind));
        CHECK(pn::kind_of(after.ui.panel) != c.kind,
              (std::string{"Esc closes "} + c.name).c_str());
    }
}

TEST_CASE("escape guarantee: a handler that answers nothing still cannot trap") {
    install_stub_deps();
    // 冻结的确切形状:a 覆盖面板 打开,它的 处理函数 将每个键都降低。
    // 分派器 仍然必须产生一个关闭 Escape。
    //
    // 模拟于同一地点 接口边界 subscribe.cpp 使用,
    // 因为真实的 事件订阅 需要活着 终端 : 未声明的逃逸决心
    // close_msg (种类),以上案例证明实际上已结案.
    for (const auto& c : cases()) {
        const std::optional<Msg> declined = std::nullopt;   // 处理函数 说不
        const Msg fallback = declined ? *declined : pn::close_msg(c.kind);

        Model m;
        c.open(m);
        auto [after, cmd] = app::update(std::move(m), fallback);
        CHECK(pn::kind_of(after.ui.panel) != c.kind,
              (std::string{"an inert handler cannot trap the user in "} + c.name).c_str());
    }
}
