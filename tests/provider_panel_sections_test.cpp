// provider_panel_sections_test - 电话 ^P 提供者 选择面板's
// SECTIONED 浏览观点,对真实的表达 存储单元.
//
// `^P` 就是你所在的表面 ? ? ! ? 吗? MANAGE 后端,
// 所以浏览线索 与提供者,你能够实际使用 和崩溃 其余的 注册表 变成
// 一个 "N more providers · type to search" 路标,
// 那个 头文件 和标志牌是 SPELLED 风景中,
// 所以这个 渲染 通过真实视图+ maya 并把它们从画布上读回来:
// 一个来源 -text 说不出提示是可以达到的 或隐藏一行并不
// 等于无法找到

#include "agtest.hpp"

#include "agentty/runtime/model.hpp"
#include "agentty/runtime/provider_rows.hpp"
#include "agentty/runtime/fused_models.hpp"
#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/app/subscribe.hpp"
#include "agentty/runtime/msg.hpp"
#include "agentty/runtime/view/panels.hpp"

#include <maya/render/canvas.hpp>
#include <maya/render/renderer.hpp>
#include <maya/style/theme.hpp>

#include <string>
#include <vector>

using namespace agentty;
namespace pn = agentty::ui::panel;

namespace {

// 无 -ASCII 归并 改为 '?' ——节标题和提示为:
// ASCII 和徽章 铬周围不是我们所主张的。
std::string render_panel(const Model& m, int width = 110, int height = 220) {
    auto root = ui::providers_panel(m);
    maya::StylePool pool;
    maya::Canvas canvas(width, height, &pool);
    canvas.clear();
    maya::render_tree(root, canvas, pool, maya::theme::native, true);

    std::string out;
    const int max_row = canvas.max_content_row();
    for (int y = 0; y <= max_row; ++y) {
        std::string line;
        for (int x = 0; x < width; ++x) {
            char32_t ch = canvas.get(x, y).character;
            if (ch == 0) ch = U' ';
            line.push_back(ch < 128 ? static_cast<char>(ch) : '?');
        }
        while (!line.empty() && line.back() == ' ') line.pop_back();
        out += line;
        out += '\n';
    }
    return out;
}

bool has(const std::string& hay, std::string_view needle) {
    return hay.find(needle) != std::string::npos;
}

Model picker_model(std::string query = {}) {
    Model m;
    m.ui.panel.descend(pn::Providers{});
    if (auto* p = m.ui.panel.get<pn::Providers>()) p->query = std::move(query);
    return m;
}

}  // 命名空间结束。

TEST_CASE("provider picker: browse hides unconfigured behind one signpost") {
    // 没有配置到新。 -install 销售商 注册表
    // 不得 被丢弃为一堵墙 相同的琥珀排。
    Model m = picker_model();
    const std::string screen = render_panel(m);
    INFO(screen);

    // 路标在座,并命名为VERB,而不仅仅是一个计数.
    CHECK(has(screen, "more providers"));
    CHECK(has(screen, "Show"));

    // 你没有建立供应商 不在浏览列表中。
    CHECK_FALSE(has(screen, "Cerebras"));
    CHECK_FALSE(has(screen, "Fireworks"));

    // "Custom host…"仍然可以达到:它总是适用的一行.
    CHECK(has(screen, "ADD"));
}

TEST_CASE("provider picker: ^E expands the withheld providers in place") {
    // 路标一定是 ACTIONABLE ,不仅仅是信息。
    // 选择面板 打开并揭示它站立的行。
    Model m = picker_model();
    auto* p = m.ui.panel.get<pn::Providers>();
    REQUIRE(p != nullptr);
    p->show_all = true;

    const std::string screen = render_panel(m, 110, 400);
    INFO(screen);

    CHECK(has(screen, "Cerebras"));
    CHECK(has(screen, "DeepSeek"));
    CHECK(has(screen, "NOT CONFIGURED"));
    // 不再扣下任何物品 所以路标就退休了
    CHECK_FALSE(has(screen, "more providers"));
    // 足者为后道.
    CHECK(has(screen, "fewer"));
}

TEST_CASE("provider picker: Enter on the signpost expands, doesn't close") {
    // 路标上有 NewCustomHost 载荷 与 "Custom host..." 哨兵,
    // 所以,我叫你们... Enter 一定要被拦截 之前 编号
    // close/switch 路径或打开主机 提示词 而不是扩张。
    namespace P = agentty::provider;
    Model m;
    m.ui.panel.descend(pn::Providers{});

    // 一定要照镜子 状态更新函数 自己的建筑-- 它通过活动
    // 提供方 ID,它可以重新 -order 排,
    // 所以一个索引不通过它就指向不同的排。
    const auto active = m.d.selection.catalog_id();
    const auto rows = ui::build_sectioned_provider_rows(
        {}, "", m.d.persisted, m.d.auth, active, false);
    int sign_idx = -1;
    for (int i = 0; i < static_cast<int>(rows.rows.size()); ++i)
        if (rows.rows[static_cast<std::size_t>(i)].more_signpost) {
            sign_idx = i;
            break;
        }
    REQUIRE(sign_idx >= 0);
    REQUIRE(rows.n_hidden > 0);

    m.ui.panel.get<pn::Providers>()->index = sign_idx;
    auto [m2, cmd] = app::update(std::move(m), Msg{ProvidersSelect{}});

    auto* p2 = m2.ui.panel.get<pn::Providers>();
    REQUIRE(p2 != nullptr);              // 仍然开放 -- -- 不是目的地
    CHECK(p2->show_all);
    // 光标停在第一显示的行上,所以报酬就在它下面.
    const auto after = ui::build_sectioned_provider_rows(
        {}, "", m2.d.persisted, m2.d.auth, active, true);
    REQUIRE(p2->index < static_cast<int>(after.rows.size()));
    CHECK(after.rows[static_cast<std::size_t>(p2->index)].section
              == ui::ProviderSection::More);
}

// ── 密钥路径 ─────────────────────────────────────────────────────────────
//
// 上面的行是由纯函数构建的; 这些按 真实 键通过 app::on_key
// 如此的,这样 绑定 永远无法到达 状态更新函数 被困在
// 这里 而不是被手牵扯进来 终端.

TEST_CASE("provider picker: ^E toggles through the real key path") {
    // A 终端 发送 ^E 以至于无所不能, 字节 0x05 无 ctrl 标志 ;
    // 发送信件时使用 mods.ctrl. 两者 一定要工作 - 工作时必须工作
    // - 使用 char_view 归并 0x01.0x1A到字母和合成 ctrl,
    // 如果其中任一编码错过 绑定 钥匙看起来是片状的,而不是破碎的。
    auto press = [](Model&& mm, maya::KeyEvent ev) {
        if (auto msg = app::on_key(mm, ev))
            mm = app::update(std::move(mm), std::move(*msg)).first;
        return std::move(mm);
    };
    auto fresh = [] {
        Model mm;
        mm.ui.panel.descend(pn::Providers{});
        return mm;
    };

    {   // 原 0x05, 无修改 标志
        maya::KeyEvent ev{maya::CharKey{static_cast<char32_t>(0x05)}, {}, {}};
        Model m2 = press(fresh(), ev);
        auto* p = m2.ui.panel.get<pn::Providers>();
        REQUIRE(p != nullptr);
        CHECK(p->show_all);
    }
    {   // 以 ctrl 设置的 'e'
        maya::KeyEvent ev{maya::CharKey{U'e'}, {}, {}};
        ev.mods.ctrl = true;
        Model m2 = press(fresh(), ev);
        auto* p = m2.ui.panel.get<pn::Providers>();
        REQUIRE(p != nullptr);
        CHECK(p->show_all);
    }
    {   // 和它,这 TOGGLES - 两根压车倒塌
        maya::KeyEvent ev{maya::CharKey{static_cast<char32_t>(0x05)}, {}, {}};
        Model m2 = press(press(fresh(), ev), ev);
        auto* p = m2.ui.panel.get<pn::Providers>();
        REQUIRE(p != nullptr);
        CHECK_FALSE(p->show_all);
    }
}

TEST_CASE("provider picker: no binding on a multiplexer prefix chord") {
    // ^A tmux 的默认前缀和 ^B 屏幕: 在多轮
    // 驱动器下 首次 媒体被吃掉,永远无法到达我们,
    // 所以 绑定 似乎需要两下 nav.hpp 警告这一点;
    // 只有在有东西检查时,规则才有效。
    auto fresh = [] {
        Model mm;
        mm.ui.panel.descend(pn::Providers{});
        return mm;
    };
    for (char32_t c : {U'a', U'b'}) {
        Model m = fresh();
        maya::KeyEvent ev{maya::CharKey{c}, {}, {}};
        ev.mods.ctrl = true;
        const auto msg = app::on_key(m, ev);
        // 要么什么都没有, 或至少 不 扩展 切换.
        if (msg) {
            const auto* pm = std::get_if<msg::ProvidersMsg>(&*msg);
            if (pm)
                CHECK(!std::holds_alternative<ProvidersToggleAll>(*pm));
        }
    }
}

TEST_CASE("provider picker: typing stays typing, ^A is not swallowed") {
    // ^A 不得在过滤查询中降落, 并空空 'a' 绝对不能 切换.
    Model m;
    m.ui.panel.descend(pn::Providers{});

    maya::KeyEvent bare{maya::CharKey{U'a'}, {}, {}};
    if (auto msg = app::on_key(m, bare))
        m = app::update(std::move(m), std::move(*msg)).first;
    auto* p = m.ui.panel.get<pn::Providers>();
    REQUIRE(p != nullptr);
    CHECK(p->query == "a");
    CHECK_FALSE(p->show_all);
}

TEST_CASE("provider picker: the row list is never a stale snapshot") {
    // 该 读取为 flashy 键的bug。 providers_update
    // 用于构建行列表 一次 所以任何手臂在变异后 都会在进入时
    // PREVIOUS 状态: 之后 ^E 扩大清单,
    // `n` 仍然有断裂的长度 ProvidersMove 将光标夹住,
    // 然后 Enter 或 或 时 间 ^D 单人一行 -threaded
    // 完全决定了... 但看起来就像... 竞态 因为它取决于钥匙
    // SEQUENCE 任何钥匙上都没有
    auto press = [](Model&& mm, Msg msg) {
        return app::update(std::move(mm), std::move(msg)).first;
    };

    Model m;
    m.ui.panel.descend(pn::Providers{});

    const int collapsed = static_cast<int>(
        ui::build_sectioned_provider_rows(
            {}, "", m.d.persisted, m.d.auth,
            m.d.selection.catalog_id(), false).rows.size());
    const int expanded = static_cast<int>(
        ui::build_sectioned_provider_rows(
            {}, "", m.d.persisted, m.d.auth,
            m.d.selection.catalog_id(), true).rows.size());
    REQUIRE(expanded > collapsed);       // 一定有办法 逐步显示

    m = press(std::move(m), Msg{ProvidersToggleAll{}});
    REQUIRE(m.ui.panel.get<pn::Providers>()->show_all);

    // 走到尽头,带着... 过期 快照 停止在
    // COLLAPSED 长度和最后一行无法到达。
    for (int i = 0; i < expanded * 2; ++i)
        m = press(std::move(m), Msg{ProvidersJump{ProvidersJump::Where::End}});

    auto* p = m.ui.panel.get<pn::Providers>();
    REQUIRE(p != nullptr);
    CHECK(p->index == expanded - 1);
    CHECK(p->index >= collapsed);        // 真正越过旧天花板

    // 并且从最后一步下来 而不是坚持。
    m = press(std::move(m), Msg{ProvidersMove{+1}});
    CHECK(m.ui.panel.get<pn::Providers>()->index == 0);
}

TEST_CASE("provider picker: collapsing never strands the cursor") {
    // 镜案: ^E 将光标拉回短
    // 列表中 而不是让它指向终点
    // Enter 默默无所作为
    auto press = [](Model&& mm, Msg msg) {
        return app::update(std::move(mm), std::move(msg)).first;
    };

    Model m;
    m.ui.panel.descend(pn::Providers{});
    m = press(std::move(m), Msg{ProvidersToggleAll{}});          // 扩展
    m = press(std::move(m), Msg{ProvidersJump{ProvidersJump::Where::End}});
    m = press(std::move(m), Msg{ProvidersToggleAll{}});          // 崩溃

    auto* p = m.ui.panel.get<pn::Providers>();
    REQUIRE(p != nullptr);
    CHECK_FALSE(p->show_all);
    const int n = static_cast<int>(
        ui::build_sectioned_provider_rows(
            {}, "", m.d.persisted, m.d.auth,
            m.d.selection.catalog_id(), false).rows.size());
    CHECK(p->index >= 0);
    CHECK(p->index < n);
}

TEST_CASE("provider picker: each section header appears at most once") {
    // 必须在 SECTION 秩序。 秩序。 已保存 东道主和 ACP 特工被
    // 标记了 YOURS 但曾经被附在 之后 无法 -configured 乐队,
    // 它创造了 YOURS 分隔符 渲染 第二次中场 -list.
    auto count_of = [](const std::string& s, const std::string& needle) {
        int n = 0;
        for (std::size_t at = 0; (at = s.find(needle, at)) != std::string::npos; ++at)
            ++n;
        return n;
    };

    // 折叠,扩充,搜索——三个都组装了列表.
    {
        Model m = picker_model();
        m.d.persisted.provider_keys["my-box:8080"] = "k";
        const std::string s = render_panel(m, 110, 400);
        INFO(s);
        CHECK(count_of(s, "YOURS") == 1);
        // 电话 已保存 自定义主机在 YOURS ,高于任何以后的乐队。
        const auto host = s.find("my-box");
        if (host != std::string::npos) {
            const auto add = s.find("ADD");
            if (add != std::string::npos) CHECK(host < add);
        }
    }
    {
        Model m = picker_model();
        m.d.persisted.provider_keys["my-box:8080"] = "k";
        m.ui.panel.get<pn::Providers>()->show_all = true;
        const std::string s = render_panel(m, 110, 600);
        INFO(s);
        CHECK(count_of(s, "YOURS") == 1);
        CHECK(count_of(s, "NOT CONFIGURED") == 1);
        // 每一个 YOURS 排在非行前 -configured 分数。
        const auto host = s.find("my-box");
        const auto notc = s.find("NOT CONFIGURED");
        REQUIRE(host != std::string::npos);
        REQUIRE(notc != std::string::npos);
        CHECK(host < notc);
    }
}

TEST_CASE("provider picker: the active provider leads YOURS even unauthed") {
    // 你正在运行它,所以 标记属于 YOURS - 但是警徽必须
    // 说出失踪的真相 凭据 ,它不能这样做 渲染 暗淡的 暗淡 暗淡
    // "Enter to set up" 提供。
    Model m = picker_model();
    const auto rows = ui::build_sectioned_provider_rows(
        {}, "", m.d.persisted, m.d.auth, "anthropic", false);
    REQUIRE(!rows.rows.empty());
    const auto& first = rows.rows.front();
    const auto* p = first.row.preset();
    REQUIRE(p != nullptr);
    CHECK(std::string{p->id} == "anthropic");
    CHECK(first.section == ui::ProviderSection::Yours);
    CHECK_FALSE(first.offer);          // 永远不要"Enter to set up"
}

TEST_CASE("provider picker: a configured provider is promoted into YOURS") {
    Model m = picker_model();
    m.d.persisted.provider_keys["groq"] = "gsk_test";
    const std::string screen = render_panel(m);
    INFO(screen);

    CHECK(has(screen, "YOURS"));
    CHECK(has(screen, "Groq"));
    // 仍隐蔽:配置一个供应商不得泄露其邻居.
    CHECK_FALSE(has(screen, "Cerebras"));
}

TEST_CASE("provider picker: search reaches a provider browse hides") {
    // 没有 -dead-end 隐藏是一个 BROWSE
    // 输入名称必须找到, 否则我们无法接触到这个特性。
    Model m = picker_model("cerebras");
    const std::string screen = render_panel(m);
    INFO(screen);

    CHECK(has(screen, "Cerebras"));
    // 来,来,来 渲染 作为一种提议,只要有行动,而不是关键的警告。
    CHECK(has(screen, "Enter to set up"));
    // 标点牌只属于浏览视图.
    CHECK_FALSE(has(screen, "more providers"));
}

TEST_CASE("provider picker: configured rows lead the unconfigured in search") {
    Model m = picker_model("e");            // 匹配许多供应商
    m.d.persisted.provider_keys["cerebras"] = "k";
    const std::string screen = render_panel(m);
    INFO(screen);

    const auto configured = screen.find("CONFIGURED");
    const auto notconf    = screen.find("NOT CONFIGURED");
    REQUIRE(configured != std::string::npos);
    if (notconf != std::string::npos)
        CHECK(configured < notconf);

    // 脑管是配置的,所以它排序在乐队分机上方.
    const auto cer = screen.find("Cerebras");
    REQUIRE(cer != std::string::npos);
    if (notconf != std::string::npos) CHECK(cer < notconf);
}

// ── 多个 -account 表面画 ──────────────────────────────────────────────
//
// 应享权利为账户 -scoped ,因此一个供应商的两个账户可以服务
// 不同 模式列表。该身份在 选择面板 区域 头文件 (和状态徽章),
// 从不每行:账户不是什么模型的一部分 IS。
// 见 docs/IDENTITY_CAPABILITY_ENTITLEMENT.md.

TEST_CASE("model picker: footer hints describe the HIGHLIGHTED row") {
    // 提示就是保证钥匙会做什么 当前. ^F 是一个 切换 ,
    // 所以在一排已经主演 它的unstars --
    // "favorite" 上面描述的是状态而不是动作。
    // 还有一个标志 -in 提供根本没有任何模式可以显示,所以暗示必须去掉,
    // 而不是点出一个默默无闻的动作。
    auto render = [](const Model& mm) {
        auto root = ui::models_panel(mm);
        maya::StylePool pool;
        maya::Canvas canvas(120, 200, &pool);
        canvas.clear();
        maya::render_tree(root, canvas, pool, maya::theme::native, true);
        std::string s;
        for (int y = 0; y <= canvas.max_content_row(); ++y) {
            for (int x = 0; x < 120; ++x) {
                char32_t ch = canvas.get(x, y).character;
                s.push_back(ch && ch < 128 ? static_cast<char>(ch) : ' ');
            }
            s.push_back('\n');
        }
        return s;
    };

    {   // 平话行:^F加了一个最爱.
        Model m;
        m.ui.panel.descend(pn::Models{});
        FusedRow r;
        r.provider_id = "anthropic";
        r.label       = "Anthropic";
        r.model.id    = ModelId{"claude-opus-4-5"};
        r.authed      = true;
        m.d.fused_rows.push_back(r);
        const std::string s = render(m);
        INFO(s);
        CHECK(has(s, "favorite"));
        CHECK_FALSE(has(s, "unfavorite"));
    }
    {   // 已经是最喜欢的了, 同样的钥匙 REMOVES。
        Model m;
        m.ui.panel.descend(pn::Models{});
        FusedRow r;
        r.provider_id    = "anthropic";
        r.label          = "Anthropic";
        r.model.id       = ModelId{"claude-opus-4-5"};
        r.model.favorite = true;
        r.authed         = true;
        m.d.fused_rows.push_back(r);
        const std::string s = render(m);
        INFO(s);
        CHECK(has(s, "unfavorite"));
    }
}

TEST_CASE("model picker: a scoped list never claims to search all providers") {
    // 标题说 "Anthropic only" 搜索框邀请您过滤
    // "across providers" - 在一个屏幕上有两个相互矛盾的说法。
    // 而^/是后退的出路,所以它的提示必须这么说,或者一个缩小
    // 的列表看起来像陷阱.
    Model m;
    m.ui.panel.descend(pn::Models{});
    auto* mp = m.ui.panel.get<pn::Models>();
    REQUIRE(mp != nullptr);

    FusedRow r;
    r.provider_id = "anthropic";
    r.label       = "Anthropic";
    r.model.id    = ModelId{"claude-opus-4-5"};
    r.authed      = true;
    m.d.fused_rows.push_back(r);

    auto render = [&] {
        auto root = ui::models_panel(m);
        maya::StylePool pool;
        maya::Canvas canvas(120, 200, &pool);
        canvas.clear();
        maya::render_tree(root, canvas, pool, maya::theme::native, true);
        std::string s;
        for (int y = 0; y <= canvas.max_content_row(); ++y) {
            for (int x = 0; x < 120; ++x) {
                char32_t ch = canvas.get(x, y).character;
                s.push_back(ch && ch < 128 ? static_cast<char>(ch) : ' ');
            }
            s.push_back('\n');
        }
        return s;
    };

    {   // 失察:跨-provider承诺是真实的,要坚持下去.
        const std::string s = render();
        INFO(s);
        CHECK(has(s, "across providers"));
    }
    {   // 范围:它不得声称通过一个提供者,以及
        // 脚步必须命名返回的路。
        mp->provider_scope = "anthropic";
        const std::string s = render();
        INFO(s);
        CHECK_FALSE(has(s, "across providers"));
        CHECK(has(s, "this provider"));
        CHECK(has(s, "all providers"));      // ^/ 提示, 逃生舱
    }
}

TEST_CASE("model picker: the scoped header names the active account") {
    Model m;
    m.ui.panel.descend(pn::Models{});
    auto* mp = m.ui.panel.get<pn::Models>();
    REQUIRE(mp != nullptr);
    mp->provider_scope = "anthropic";

    m.d.auth.loaded = true;
    m.d.auth.providers["anthropic"] = auth::ProviderAuth{
        .stored = true, .accounts = {"work", "home"}, .active_account = "work"};

    FusedRow r;
    r.provider_id = "anthropic";
    r.label       = "Anthropic";
    r.model.id    = ModelId{"claude-opus-4-5"};
    r.model.display_name = "Claude Opus 4.5";
    r.authed      = true;
    m.d.fused_rows.push_back(r);

    auto root = ui::models_panel(m);
    maya::StylePool pool;
    maya::Canvas canvas(120, 200, &pool);
    canvas.clear();
    maya::render_tree(root, canvas, pool, maya::theme::native, true);
    std::string screen;
    for (int y = 0; y <= canvas.max_content_row(); ++y) {
        for (int x = 0; x < 120; ++x) {
            char32_t ch = canvas.get(x, y).character;
            screen.push_back(ch && ch < 128 ? static_cast<char>(ch) : ' ');
        }
        screen.push_back('\n');
    }
    INFO(screen);

    // 电话 头文件 具有下列身份: 目录 这是。
    CHECK(has(screen, "WORK"));
    // 模型行本身保持干净——账户被命名一次,
    // 在头文件上,不重复它会窃取宽度的列表.
    const auto hdr = screen.find("ANTHROPIC MODELS");
    REQUIRE(hdr != std::string::npos);
    const auto hdr_eol = screen.find('\n', hdr);
    // 只有一个 WORK 屏幕上,它就在屏幕上 头文件 线条。
    CHECK(screen.find("WORK") < hdr_eol);
    CHECK(screen.find("WORK", hdr_eol) == std::string::npos,
          "the account appears once, on the header — not per model row");
}

TEST_CASE("model picker: a single-account provider adds no chrome") {
    Model m;
    m.ui.panel.descend(pn::Models{});
    auto* mp = m.ui.panel.get<pn::Models>();
    REQUIRE(mp != nullptr);
    mp->provider_scope = "anthropic";

    m.d.auth.loaded = true;
    m.d.auth.providers["anthropic"] = auth::ProviderAuth{
        .stored = true, .accounts = {"work"}, .active_account = "work"};

    FusedRow r;
    r.provider_id = "anthropic";
    r.label       = "Anthropic";
    r.model.id    = ModelId{"claude-opus-4-5"};
    r.model.display_name = "Claude Opus 4.5";
    r.authed      = true;
    m.d.fused_rows.push_back(r);

    auto root = ui::models_panel(m);
    maya::StylePool pool;
    maya::Canvas canvas(120, 200, &pool);
    canvas.clear();
    maya::render_tree(root, canvas, pool, maya::theme::native, true);
    std::string screen;
    for (int y = 0; y <= canvas.max_content_row(); ++y) {
        for (int x = 0; x < 120; ++x) {
            char32_t ch = canvas.get(x, y).character;
            screen.push_back(ch && ch < 128 ? static_cast<char>(ch) : ' ');
        }
        screen.push_back('\n');
    }
    INFO(screen);

    // 一个说法是绝对没有选择的——命名它就是噪音。
    CHECK_FALSE(has(screen, "WORK"));
}

