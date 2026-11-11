// 形式组件层的测试:共享状态模型,一个关键映射,
// 纯投影到maya::Panel::Config.
//
// 值得打针的属性是五手里那些虫子的属性 -rolled
// 选择面板 此选项取代:
//
//   * 键盘完全由一个模式拥有(键盘绝对不能从一个键盘中漏出)
//     文本字段为全局导航),
//   * Esc 从滴滴滴滴下来,是真实的 CANCEL (该值未受影响,直到
//     承诺,
//   * 一个秘密的简单文字从未到达 渲染 层层- 完全没有
//     "is masked" ,但结构上 缺失什么 maya 交给你

#include <doctest/doctest.h>

#include <maya/print.hpp>
#include <maya/widget/panel/caret.hpp>

#include "agentty/runtime/panel/form.hpp"
#include "agentty/runtime/panel/form_keys.hpp"
#include "agentty/runtime/view/form_panel.hpp"

using namespace agentty::form;
namespace keys = agentty::form::keys;

namespace {

maya::KeyEvent key(maya::SpecialKey k) {
    maya::KeyEvent ev;
    ev.key = k;
    return ev;
}

maya::KeyEvent chr(char32_t c, bool ctrl = false) {
    maya::KeyEvent ev;
    ev.key = maya::CharKey{c};
    ev.mods.ctrl = ctrl;
    return ev;
}

Form demo() {
    return Builder{"Demo"}
        .choice("backend", "Backend", {"Auto", "Ollama", "Custom"}, {"auto", "ollama", "openai"}, "auto")
        .text("host", "Host", "127.0.0.1")
        .number("port", "Port", 11434, 1, 65535)
        .slider("weight", "Weight", 0.65, 0.0, 1.0, 0.05)
        .secret("key", "API key", "sk-super-secret")
        .action("test", "Test connection")
        .build();
}

// 驱动一个键通过路由器+ 状态更新函数 ,作为 面板 已经。
keys::Applied press(Form& f, const maya::KeyEvent& ev) {
    auto a = keys::translate(f, ev);
    if (!a) return {};
    return keys::apply(f, *a);
}

} // 命名空间结束。

TEST_CASE("form: builder produces addressable rows") {
    auto f = demo();
    CHECK(f.fields.size() == 6);
    REQUIRE(f.find("host") != nullptr);
    CHECK(f.find("host")->label == "Host");
    CHECK(f.find("nope") == nullptr);
    // 选择由存储的 id 选择,而不是显示标签。
    CHECK(std::get<field::Choice>(f.find("backend")->value).id() == "auto");
}

TEST_CASE("form: exactly one mode owns the keyboard") {
    auto f = demo();
    CHECK(std::holds_alternative<focus::Browsing>(f.focus));

    // 文本行上的Enter进行编辑。
    f.cursor = 1;
    press(f, key(maya::SpecialKey::Enter));
    CHECK(f.editing());
    CHECK_FALSE(f.choosing());

    // 编辑时 'j' 是一个字符—— 不 动作,这是手漏气
    // -rolled 投入不断涌现。
    const int before = f.cursor;
    press(f, chr(U'j'));
    CHECK(f.cursor == before);
    CHECK(std::get<field::Text>(f.find("host")->value).value.find('j') != std::string::npos);

    // Esc 离开球场,但不是形式。
    auto r = press(f, key(maya::SpecialKey::Escape));
    CHECK_FALSE(r.close);
    CHECK_FALSE(f.editing());

    // Esc 关闭。
    r = press(f, key(maya::SpecialKey::Escape));
    CHECK(r.close);
}

TEST_CASE("form: dropdown opens, commits, and cancels non-destructively") {
    auto f = demo();
    f.cursor = 0;

    press(f, key(maya::SpecialKey::Enter));
    CHECK(f.choosing());
    // 打开时不能触及价值。
    CHECK(std::get<field::Choice>(f.find("backend")->value).id() == "auto");

    press(f, key(maya::SpecialKey::Down));
    // 仍未触及——亮点移动,而不是田野.
    CHECK(std::get<field::Choice>(f.find("backend")->value).id() == "auto");

    // Esc 是一个 TRUE 取消.
    press(f, key(maya::SpecialKey::Escape));
    CHECK_FALSE(f.choosing());
    CHECK(std::get<field::Choice>(f.find("backend")->value).id() == "auto");

    // 重新打开,移动,承诺。
    press(f, key(maya::SpecialKey::Enter));
    press(f, key(maya::SpecialKey::Down));
    auto r = press(f, key(maya::SpecialKey::Enter));
    CHECK(r.changed);
    CHECK_FALSE(f.choosing());
    CHECK(std::get<field::Choice>(f.find("backend")->value).id() == "ollama");
    CHECK(f.dirty);
}

TEST_CASE("form: an open dropdown swallows stray keys") {
    auto f = demo();
    f.cursor = 0;
    press(f, key(maya::SpecialKey::Enter));
    REQUIRE(f.choosing());

    // 菜单不能使用的密钥 不 掉到下面的形状
    const int cursor_before = f.cursor;
    press(f, key(maya::SpecialKey::Right));
    CHECK(f.cursor == cursor_before);
    CHECK(f.choosing());
}

TEST_CASE("form: a dropdown never filters — j/k always navigate") {
    // 滴滴会保持 ENUM。它故意没有查询状态,
    // 所以 j/k 无条件导航,并且永远不能被吞噬为过滤文本。
    // 一个设置得足够大,需要搜索的候选人是 `Pick` 划线
    auto f = demo();
    f.cursor = 0;
    press(f, key(maya::SpecialKey::Enter));
    REQUIRE(f.choosing());

    press(f, key(maya::SpecialKey::Down));
    CHECK(f.dropdown()->highlighted == 1);
    press(f, key(maya::SpecialKey::Up));
    CHECK(f.dropdown()->highlighted == 0);

    // 甚至一个长列表导航而不是过滤——选项计数
    // 不会改变交互模式.
    std::vector<std::string> many;
    for (int i = 0; i < 12; ++i) many.push_back("model-" + std::to_string(i));
    auto g = Builder{"Long"}.choice("m", "Model", many).build();
    press(g, key(maya::SpecialKey::Enter));
    REQUIRE(g.choosing());
    press(g, key(maya::SpecialKey::Down));
    CHECK(g.dropdown()->highlighted == 1);
}

TEST_CASE("form: a Pick row hands off instead of opening a dropdown") {
    // 使选择保持诚实的逃生舱: large/dynamic 候选设置
    // 打开一个真实 选择面板 ,表格中永远不会知道他们。
    auto f = Builder{"Hand-off"}
        .pick("model", "Model", "claude-opus-4-5")
        .build();
    f.cursor = 0;

    const auto r = press(f, key(maya::SpecialKey::Enter));
    CHECK(r.hand_off);
    CHECK_FALSE(f.choosing());     // 无内线列表
    CHECK_FALSE(f.editing());      // 而不是编辑
    CHECK_FALSE(r.changed);
}

TEST_CASE("form: dropdown commit resolves through the same list the view shows") {
    // 状态更新函数和视图以相同的功能解决选项,
    // 因此突出显示的行和承诺值不能有差异.
    std::vector<std::string> many;
    for (int i = 0; i < 12; ++i) many.push_back("model-" + std::to_string(i));
    auto f = Builder{"Long"}.choice("m", "Model", many).build();

    press(f, key(maya::SpecialKey::Enter));
    for (int i = 0; i < 3; ++i) press(f, key(maya::SpecialKey::Down));
    const auto& c = std::get<field::Choice>(f.find("m")->value);
    REQUIRE(visible_options(c).labels.size() == 12);
    press(f, key(maya::SpecialKey::Enter));
    CHECK(std::get<field::Choice>(f.find("m")->value).label() == "model-3");
}

TEST_CASE("form: a letter on a number row is a true no-op") {
    // 报告编号为"typing an alpha char misbehaves everywhere".
    //
    // 数字行只取数字,但类型-to-edit进入了第ANYWAY场次:
    // 焦点翻转到编辑,edit_dirty变真,
    // 然后number_insert放下了字符,
    // 该行被留了活,"changed"没有屏幕显示——
    // 因此箭头开始移动一个光标而不是行光标,
    // Esc进行了从未发生的编辑,面板读作吞下了键.
    //
    // 被拒绝的性格必须离开 每个 轴未动,这就是它为什么是没有
    // -op 而不是半个 -entered 届会。
    auto f = demo();
    f.cursor = 2;                               // 端口,一个数字行
    const auto before = number_value(std::get<field::Number>(f.find("port")->value));

    press(f, chr(U'x'));
    CHECK_FALSE(f.editing());                   // 没有开始会话
    CHECK_FALSE(f.dirty);                       // 无标记更改
    CHECK(number_value(std::get<field::Number>(f.find("port")->value)) == before);

    // 数字数字仍然在后面工作,所以警卫没有把排位楔子。
    press(f, chr(U'7'));
    CHECK(f.editing());
    CHECK(number_value(std::get<field::Number>(f.find("port")->value)) == 7);

    // 中间 -session 也:数字之间的一个字母会被忽略,
    // 而不是结束或损坏编辑。
    press(f, chr(U'z'));
    CHECK(number_value(std::get<field::Number>(f.find("port")->value)) == 7);
    press(f, chr(U'9'));
    CHECK(number_value(std::get<field::Number>(f.find("port")->value)) == 79);
}

TEST_CASE("form: the first digit typed REPLACES, it does not append") {
    // 报告为"input doesn't work on all fields sometimes",
    // 这就是机制。
    //
    // 附加使数字行的行为与其他设置字段一样: 屏幕上开始
    // 打字时的值是 SAVED , 所以扩展它绝不是键盘键的表示。
    // 打入 8 键入行中, 显示 4096 给定
    // 40968.
    //
    // 在一个 SMALL 范围比出人意料的还要差,它就是死输入:最大 8
    // 的一行显示 3, 类型 5 - > 3*10+5 = 35 - >
    // 夹到 8. 该行现在最大,所以以后的每个数字也夹到 8 和 无内容。
    // 没有 光标 没有任何错误,没有办法 来解释原因。
    auto f = demo();
    f.cursor = 2;                               // 11434号港口,射程1-65535

    // Type-to-edit:第一个可打印开始会话.
    press(f, chr(U'8'));
    CHECK(number_value(std::get<field::Number>(f.find("port")->value)) == 8);

    // 还有 第二 数字仍然附加 -- 4-0- 9-6 必须输入4096, 所以这是新鲜的
    // -vs-continuing 区分而不是 "replace always".
    press(f, chr(U'0'));
    CHECK(number_value(std::get<field::Number>(f.find("port")->value)) == 80);
    press(f, chr(U'8'));
    CHECK(number_value(std::get<field::Number>(f.find("port")->value)) == 808);
}

TEST_CASE("form: Enter-then-type replaces too") {
    // 另一条路进入编辑会话必须表现得相同,或者字段的响应取决于您是
    // 如何打开的——这是它自己的类型
    // "sometimes input doesn't work".
    auto f = demo();
    f.cursor = 2;
    press(f, key(maya::SpecialKey::Enter));
    press(f, chr(U'7'));
    CHECK(number_value(std::get<field::Number>(f.find("port")->value)) == 7);
}

TEST_CASE("form: a Number row cleared with Backspace takes any number (#97)") {
    // 左舷,射程1 -65535 Backspace 键,然后输入 8080。
    // 该值是每个键的插入键,所以 "empty" 成为1(最小),
    // 并随同该1的每个数字。
    auto f = demo();
    f.cursor = 2;                               // 港口,11434
    press(f, key(maya::SpecialKey::Enter));
    for (int i = 0; i < 6; ++i) press(f, key(maya::SpecialKey::Backspace));
    for (char32_t c : std::u32string{U"8080"}) press(f, chr(c));
    press(f, key(maya::SpecialKey::Enter));
    CHECK(number_value(std::get<field::Number>(f.find("port")->value)) == 8080);

    // 之后相同 ^U ,当数字 带类型的 启动于最小值以下。
    press(f, key(maya::SpecialKey::Enter));
    press(f, chr(U'u', /*ctrl= (帮助)*/true));
    for (char32_t c : std::u32string{U"0443"}) press(f, chr(c));
    press(f, key(maya::SpecialKey::Enter));
    CHECK(number_value(std::get<field::Number>(f.find("port")->value)) == 443);
}

TEST_CASE("form: a number edit clamps once, when it ends") {
    // 最大夹口以上; 空的字段保留其 已保存 值;
    // 和 中点 -edit 显示什么是 带类型的 ,包含空。
    auto f = demo();
    f.cursor = 2;                               // 11434, 1-65535)号港口
    press(f, key(maya::SpecialKey::Enter));
    for (int i = 0; i < 6; ++i) press(f, key(maya::SpecialKey::Backspace));
    const auto& n = std::get<field::Number>(f.find("port")->value);
    CHECK(number_text(n).empty());              // 清除显示为空, 而非 "1"
    press(f, key(maya::SpecialKey::Enter));
    CHECK(n.value == 11434);                    // 无 带类型的: 无变化

    press(f, key(maya::SpecialKey::Enter));
    for (char32_t c : std::u32string{U"999999"}) press(f, chr(c));
    CHECK(number_text(n) == "999999");          // 带类型的 以 -is ,不按键夹
    press(f, key(maya::SpecialKey::Enter));
    CHECK(n.value == 65535);                    // 一次被夹住,一次被夹死
}

TEST_CASE("form: a small-range number row never goes deaf to typing") {
    // 所报虫的准确形状,在显示它的行上:沙箱XX面板的
    // "Secret scan depth"是0-8,船只是3.
    //
    // 输入值必须能够达到范围中的每个值。 在旧附件中,
    // 键盘键入后, 键入后按最大键键排列, 并停留在那里。
    auto f = Builder{"Depth"}.number("depth", "Secret scan depth", 3, 0, 8).build();
    f.cursor = 0;

    press(f, chr(U'5'));
    CHECK(number_value(std::get<field::Number>(f.find("depth")->value)) == 5);

    // 离开后再打一个不同的数字。行必须再次回答,
    // 而不是被卡在它所要的上。
    press(f, key(maya::SpecialKey::Escape));
    press(f, chr(U'2'));
    CHECK(number_value(std::get<field::Number>(f.find("depth")->value)) == 2);

    // 包括 0,该附件永远无法从非生产 -zero 开始
    press(f, key(maya::SpecialKey::Escape));
    press(f, chr(U'0'));
    CHECK(number_value(std::get<field::Number>(f.find("depth")->value)) == 0);
}

TEST_CASE("form: number and slider clamp on every mutation") {
    auto f = demo();
    f.cursor = 2;                               // 端口
    press(f, key(maya::SpecialKey::Enter));
    for (char32_t c : {U'9', U'9', U'9', U'9', U'9', U'9'}) press(f, chr(c));
    CHECK(number_value(std::get<field::Number>(f.find("port")->value)) == 65535);

    press(f, key(maya::SpecialKey::Escape));
    f.cursor = 3;                               // 重量滑动器
    for (int i = 0; i < 20; ++i) press(f, key(maya::SpecialKey::Right));
    CHECK(std::get<field::Slider>(f.find("weight")->value).value == doctest::Approx(1.0));
    for (int i = 0; i < 40; ++i) press(f, key(maya::SpecialKey::Left));
    CHECK(std::get<field::Slider>(f.find("weight")->value).value == doctest::Approx(0.0));
}

TEST_CASE("form: slider steps stay on the grid") {
    // 重复的 ± 步不得累积浮力漂移到 0.6500000000000001。
    auto f = demo();
    f.cursor = 3;
    for (int i = 0; i < 5; ++i) press(f, key(maya::SpecialKey::Right));
    for (int i = 0; i < 5; ++i) press(f, key(maya::SpecialKey::Left));
    CHECK(std::get<field::Slider>(f.find("weight")->value).value == doctest::Approx(0.65));
}

TEST_CASE("form: locked rows refuse edits") {
    auto f = Builder{"Locked"}
        .text("host", "Host", "127.0.0.1")
        .lock("env: AGENTTY_EMBED_ENDPOINT")
        .build();
    f.cursor = 0;
    press(f, key(maya::SpecialKey::Enter));
    CHECK_FALSE(f.editing());                   // 永远不要参与
    press(f, key(maya::SpecialKey::Right));
    CHECK(std::get<field::Text>(f.find("host")->value).value == "127.0.0.1");
    CHECK_FALSE(f.dirty);
}

TEST_CASE("form: an action row reports rather than mutating") {
    auto f = demo();
    f.cursor = 5;                               // "Test connection"
    auto r = press(f, key(maya::SpecialKey::Enter));
    CHECK(r.fired);
    CHECK_FALSE(r.changed);
    CHECK_FALSE(f.editing());
}

TEST_CASE("form: no save chord — Esc commits; ^C stays ambient") {
    auto f = demo();
    // ^S 谓 不 窗体键: 留下一个字段, 并 Esc 叶子
    // 同一行为第二个关键是保持同步的第二个问题.
    CHECK_FALSE(keys::translate(f, chr(U's', /*ctrl= (帮助)*/true)).has_value());
    // ^C 不得 被声称——应用程序从任何地方退出.
    CHECK_FALSE(keys::translate(f, chr(U'c', true)).has_value());
}

// ── 预测 ───────────────────────────────────────────────────────

TEST_CASE("form view: a Secret's plaintext never reaches the widget") {
    // 没有 "is masked" ——结构缺失. maya::panel::Secret
    // 没有任何领域可以支撑它, 所以没有现在或未来 渲染 路径可以泄露。
    auto f = demo();
    const auto cfg = agentty::ui::form_config(f, maya::Color::blue());

    bool saw_secret = false;
    for (const auto& row : cfg.items) {
        if (const auto* s = std::get_if<maya::panel::Secret>(&row.control)) {
            saw_secret = true;
            CHECK(s->filled == 15);             // "sk-super-secret"
        }
        // 任何控制都无法携带简洁的文字。
        if (const auto* t = std::get_if<maya::panel::Text>(&row.control))
            CHECK(t->value.find("sk-") == std::string::npos);
    }
    CHECK(saw_secret);
}

TEST_CASE("form view: menu is present only while choosing") {
    auto f = demo();
    f.cursor = 0;
    CHECK_FALSE(agentty::ui::form_config(f, maya::Color::blue()).menu.has_value());

    press(f, key(maya::SpecialKey::Enter));
    const auto cfg = agentty::ui::form_config(f, maya::Color::blue());
    REQUIRE(cfg.menu.has_value());
    CHECK(cfg.menu_row == 0);
    CHECK(cfg.menu->options.size() == 3);
    CHECK(cfg.menu->options[0] == "Auto");
}

TEST_CASE("form view: the widget renders every control kind") {
    // 烟雾测试 maya 部件不为每个控件投掷而构建,
    // 包括使用打开的菜单(z) -stacked 浮点路径).
    auto f = demo();
    f.cursor = 0;
    press(f, key(maya::SpecialKey::Enter));
    auto cfg = agentty::ui::form_config(f, maya::Color::blue());
    cfg.items[1].origin = "settings.json";
    cfg.items[2].error  = "port must be 1-65535";
    maya::Element el = maya::Panel{std::move(cfg)}.build();
    CHECK(true);   // 建造时不扔
}

TEST_CASE("form view: one setting is one row") {
    // 递归: 每行 `help` 画了第二副暗号 -line ,所以一个 面板 高度
    // 是 2x 的设置数。 RAG 面板 共有 5 组的 22 个设置 头文件
    // 14排50多列 14排 14排 50 3排 14 4排 14列 14 14
    // 3排 -row 可视区域 ,它将设置列表变成一个流言蜚语的墙壁,你必须滚动才能
    // 到达一个单 切换.
    //
    // 帮助属于行,光标是ON:这是它所能描述的唯一一行,
    // 它花费的是一条线而不是N.
    auto f = Builder{"Helpy"}
        .toggle("a", "Alpha", true,  "the first thing")
        .toggle("b", "Beta",  false, "the second thing")
        .toggle("c", "Gamma", true,  "the third thing")
        .build();

    const auto height = [&](const Form& form) {
        auto cfg = agentty::ui::form_config(form, maya::Color::blue());
        cfg.viewport_h = 100;            // 无夹: 测量内容
        const auto out = maya::render_to_string(
            maya::Panel{std::move(cfg)}.build(), 80);
        int n = 0;
        for (char ch : out) if (ch == '\n') ++n;
        return n;
    };

    f.cursor = 0;
    const int h0 = height(f);
    f.cursor = 1;
    const int h1 = height(f);

    // 移动光标时不能改变 面板 的高度: 无论是哪种方式都
    // 存在一条帮助线。 在修复之前, 全部三个都画好了。
    CHECK(h0 == h1);

    // 显示的帮助是光标下的帮助,而不是全部.
    const auto text_at = [&](int cursor) {
        f.cursor = cursor;
        auto cfg = agentty::ui::form_config(f, maya::Color::blue());
        cfg.viewport_h = 100;
        return maya::render_to_string(maya::Panel{std::move(cfg)}.build(), 80);
    };
    const std::string on_first  = text_at(0);
    const std::string on_second = text_at(1);
    CHECK(on_first.find("the first thing")  != std::string::npos);
    CHECK(on_first.find("the second thing") == std::string::npos);
    CHECK(on_second.find("the second thing") != std::string::npos);
    CHECK(on_second.find("the first thing")  == std::string::npos);
}

TEST_CASE("form view: headers cost one row, not two") {
    // 退步: 每个区 头文件 发射空空格线,
    // 上面是每组5行14个废格 -row 可视区域 检索
    // 中 面板 ——在一张你滚动的列表中,一行说无是一行
    // 是隐藏有是的. 头文件 已经是不同的(粗体、淡体、
    // 无值栏),所以空白的买不到任何东西。
    //
    // 头文件 渲染 UPPER-CASE 以一条循规蹈矩,故不能误以为是
    // 一条锁线(这是什么)。 "Endpoint  auto-detected"
    // 看上去就像两个都只是阴暗的时候
    auto f = Builder{"Grouped"}
        .header("Sources")
        .toggle("a", "Alpha", true)
        .header("Pipeline")
        .toggle("b", "Beta", false)
        .build();

    auto cfg = agentty::ui::form_config(f, maya::Color::blue());
    cfg.viewport_h = 100;
    const auto out = maya::render_to_string(
        maya::Panel{std::move(cfg)}.build(), 80);

    // 也没有直接上方的空行 头文件.
    const auto no_blank_before = [&](const std::string& label) {
        const auto at = out.find(label);
        REQUIRE(at != std::string::npos);
        const auto line_start = out.rfind('\n', at);
        REQUIRE(line_start != std::string::npos);
        const auto prev_start = out.rfind('\n', line_start - 1);
        const std::string prev = out.substr(
            prev_start + 1, line_start - prev_start - 1);
        // 上行必须携带内容,而不只是帧+空格.
        return prev.find_first_not_of(" \u2502") != std::string::npos;
    };
    CHECK(no_blank_before("PIPELINE"));
    // 还有 头文件 视像化为 头文件 :盖子加一条到边的规矩.
    CHECK(out.find("SOURCES") != std::string::npos);
    CHECK(out.find("\xe2\x94\x80\xe2\x94\x80") != std::string::npos);
}

TEST_CASE("form view: the current row is tinted, the others are not") {
    // 帮助从无重点的行走, 单一的口音 存储单元
    // 左侧是唯一的焦点信号, 当你的眼睛在极右的值栏上时,
    // 很容易丢失。现在的行在整个过程中都有
    // 一个细微的洗涤。
    //
    // 洗洗的一定是真颜色,不是 ANSI `black` :
    // 黑色 IS 编号 终端 大部分黑暗主题的背景, 所以第一次尝试
    // 这一点是无法被构建所见的。
    //
    // 主题 SLOT 现在( N) panel::Theme::row_bg
    // = Surface),所以将一个已知的主题而不是字面标注在下面.
    // `native` 根本就没有洗刷的,正确无误: 原住民
    // 国家没有表面颜色,因为发明一拳就能打出一个人的洞 终端.
    static const maya::Theme kProbe = [] {
        maya::Theme t = maya::theme::native;
        t.surface = maya::Color::rgb(35, 38, 52);
        return t;
    }();
    maya::theme::set_live(kProbe);

    auto f = Builder{"Tinted"}
        .toggle("a", "Alpha", true)
        .toggle("b", "Beta", false)
        .build();
    f.cursor = 0;

    auto cfg = agentty::ui::form_config(f, maya::Color::blue());
    cfg.viewport_h = 100;
    const auto ansi = maya::render_to_string_ansi(
        maya::Panel{std::move(cfg)}.build(), 80);

    const auto line_with = [&](const std::string& label) {
        const auto at = ansi.find(label);
        REQUIRE(at != std::string::npos);
        const auto from = ansi.rfind('\n', at);
        const auto to   = ansi.find('\n', at);
        return ansi.substr(from + 1, to - from - 1);
    };

    const std::string wash = "48;2;35;38;52";
    CHECK(line_with("Alpha").find(wash) != std::string::npos);
    CHECK(line_with("Beta").find(wash)  == std::string::npos);

    maya::theme::set_live(maya::theme::native);
}

TEST_CASE("form view: the panel is FRAMED like every other picker") {
    // 退转 : Form::build () 原先返回的空地无边、
    // 无垫或无垫 -width。无帧 覆盖面板 涂料透明地覆盖 线程 ,
    // 屏幕上读作 "the key did nothing" /
    // "the picker closed" - 电话 面板 形式是
    // 选择面板-family 覆盖面板 并必须坐在同一 帧.
    auto f = demo();
    const auto out = maya::render_to_string(
        maya::Panel{agentty::ui::form_config(f, maya::Color::blue())}.build(), 80);

    // 圆形的边角,完全就是 选择面板 画他们。
    CHECK(out.find("\xe2\x95\xad") != std::string::npos);   // ╭
    CHECK(out.find("\xe2\x95\xb0") != std::string::npos);   // ╰
    // 衔骑上界.
    CHECK(out.find("Demo") != std::string::npos);
    // 而行则实际上被画.
    CHECK(out.find("Backend") != std::string::npos);
    CHECK(out.find("Host")    != std::string::npos);
}

TEST_CASE("form view: an open dropdown paints its options over the panel") {
    // 浮点数必须实际到达屏幕——一个状态菜单 -only 但从
    // 不画画 也是一样的无形 -overlay 失败 一级下降。
    auto f = demo();
    f.cursor = 0;
    press(f, key(maya::SpecialKey::Enter));
    REQUIRE(f.choosing());

    const auto out = maya::render_to_string(
        maya::Panel{agentty::ui::form_config(f, maya::Color::blue())}.build(), 80);
    // 每个后端选项都在屏幕上,包括不属于当前选择的
    // 选项(i.e. 这是列表,不是关闭控制).
    CHECK(out.find("Ollama") != std::string::npos);
    CHECK(out.find("Custom") != std::string::npos);
}

TEST_CASE("form view: the option list is INLINE, inside the panel") {
    // 名单曾经是z -stacked 浮点,它必须发明一个
    // 不透明的背景,宽度,翻转和夹子——每个都是一个错误(
    // buged) -through ,溢出,主题 -coloured。
    // 它现在在面板的自身流中扩张,所以唯一可以断言的是,
    // 每个选项行都保留 内部 面板边框。
    auto f = demo();
    f.cursor = 0;
    press(f, key(maya::SpecialKey::Enter));
    REQUIRE(f.choosing());

    const auto out = maya::render_to_string(
        maya::Panel{agentty::ui::form_config(f, maya::Color::blue())}.build(), 80);

    std::size_t start = 0;
    int checked = 0;
    while (start <= out.size()) {
        const std::size_t nl = out.find('\n', start);
        const std::string line = out.substr(start, nl - start);
        // 这些字符串仅作为选项行出现(闭合控制显示"Custom endpoint",
        // 而打开时只显示chevron).
        if (line.find("Ollama") != std::string::npos
            || line.find("llama.cpp server") != std::string::npos) {
            // 完全有两个垂直的条:面板的左右边框.
            // 第三个意味着一个盒子 回到里面;
            // 更少意味着线逃过了 帧.
            int bars = 0;
            for (std::size_t i = 0;
                 (i = line.find("\xe2\x94\x82", i)) != std::string::npos; i += 3)
                ++bars;
            CHECK(bars == 2);
            ++checked;
        }
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    CHECK(checked > 0);   // 我们确实检查了一排
}

TEST_CASE("form view: a Secret is never painted in the clear") {
    // 结束 -to-end 类型版本 -level 保证:
    // 渲染 整个部件,确认该平话文本在输出中无一出现。
    auto f = demo();
    const auto out = maya::render_to_string(
        maya::Panel{agentty::ui::form_config(f, maya::Color::blue())}.build(), 80);
    CHECK(out.find("sk-super-secret") == std::string::npos);
    CHECK(out.find("sk-")             == std::string::npos);
    CHECK(out.find("*")               != std::string::npos);   // 换成蒙面
}

TEST_CASE("form: a Secret's placeholder is shown only while it is empty") {
    // 占位符不是秘密的:这是机密可能交给部件的字符串,
    // 它报告的是别处持有的密钥("key saved")。
    auto f = Builder{"Keys"}
        .secret("key", "API key", "").placeholder("held elsewhere")
        .text("host", "Host", "x").placeholder("ignored")    // 无法保存 : no-op
        .build();
    CHECK(std::get<field::Secret>(f.find("key")->value).placeholder == "held elsewhere");
    f.cursor = 1;   // 关闭秘密:它被引出无重点
    auto out = maya::render_to_string(
        maya::Panel{agentty::ui::form_config(f, maya::Color::blue())}.build(), 80);
    CHECK(out.find("held elsewhere") != std::string::npos);
    CHECK(out.find("not set")        == std::string::npos);

    // 一个带类型的值获胜:行显示面具,从不显示占位符.
    std::get<field::Secret>(f.find("key")->value).value = "sk-typed-value";
    out = maya::render_to_string(
        maya::Panel{agentty::ui::form_config(f, maya::Color::blue())}.build(), 80);
    CHECK(out.find("held elsewhere") == std::string::npos);
    CHECK(out.find("sk-typed")       == std::string::npos);
}

TEST_CASE("form: paste_into_focused pastes the way typing types") {
    // 浏览文本-like行:贴面打开编辑和土地.
    {
        auto f = demo();
        f.cursor = 4;   // 密钥
        REQUIRE(!f.editing());
        CHECK(paste_into_focused(f, "sk-NEW"));
        CHECK(f.editing());
        CHECK(f.edit_dirty);   // 离开球场 承诺
        CHECK(std::get<field::Secret>(f.find("key")->value).value
              == "sk-super-secretsk-NEW");   // 现场 光标 ,如打字时
    }
    // 已经编辑: paste_into。
    {
        auto f = demo();
        f.cursor = 1;   // Host
        press(f, key(maya::SpecialKey::Enter));
        REQUIRE(f.editing());
        CHECK(paste_into_focused(f, ".lan"));
        CHECK(std::get<field::Text>(f.find("host")->value).value == "127.0.0.1.lan");
    }
    // 无任何 Lands, 和NO 编辑会话打开, 打开: 锁定的行...
    {
        auto f = demo();
        f.cursor = 1;
        f.fields[1].locked = true;
        CHECK_FALSE(paste_into_focused(f, "x"));
        CHECK_FALSE(f.editing());
        CHECK(std::get<field::Text>(f.find("host")->value).value == "127.0.0.1");
    }
    // ... 一条不是文本( 选择, 动作)...
    for (int row : {0, 5}) {
        auto f = demo();
        f.cursor = row;
        CHECK_FALSE(paste_into_focused(f, "x"));
        CHECK_FALSE(f.editing());
    }
    // ...打开下拉(菜单拥有密钥)...
    {
        auto f = demo();
        f.cursor = 0;
        press(f, key(maya::SpecialKey::Enter));
        REQUIRE(f.choosing());
        CHECK_FALSE(paste_into_focused(f, "x"));
        CHECK(f.choosing());
    }
    // ... 并且粘贴一行, 将不取任何字母: 数字上的信,
    // 任何地方都有一个赤裸的新线。 半-state 型的-to-edit 拒绝信件。
    {
        auto f = demo();
        f.cursor = 2;   // 端口( T)
        CHECK_FALSE(paste_into_focused(f, "abc"));
        CHECK_FALSE(f.editing());
        CHECK(number_value(std::get<field::Number>(f.find("port")->value)) == 11434);
        f.cursor = 1;   // Host
        CHECK_FALSE(paste_into_focused(f, "\n"));
        CHECK_FALSE(f.editing());
    }
    // 数字需要一个贴上的数字整体,正如带类型的数字那样。
    {
        auto f = demo();
        f.cursor = 2;
        CHECK(paste_into_focused(f, "8080"));
        CHECK(number_value(std::get<field::Number>(f.find("port")->value)) == 8080);
    }
}

TEST_CASE("form: an open pane ALWAYS answers Esc") {
    // 退位("input stops getting through"
    // 冻结): 退位的覆盖面板是没有表格的,由推迟的Cmd填充.
    // 至少一个帧拥有了面板,可以不采取行动——每把钥匙,
    // INCLUDING,本来可以逃脱的Esc都被吞噬.
    //
    // 结构修补是形式是在建筑中建造的,不是可选的,
    // 所以 "open but inert" 无法表达。
    // 这将行为的一半固定:从每个焦点模式, Esc 最终必须关闭。
    auto f = demo();

    // 浏览 —— 立即关闭.
    CHECK(press(f, key(maya::SpecialKey::Escape)).close);

    // 编辑 → 第Esc次离开字段,第二关.
    f = demo();
    f.cursor = 1;
    press(f, key(maya::SpecialKey::Enter));
    REQUIRE(f.editing());
    CHECK_FALSE(press(f, key(maya::SpecialKey::Escape)).close);
    CHECK(press(f, key(maya::SpecialKey::Escape)).close);

    // 选择 ^ 第Esc次取消列表,第二结束.
    f = demo();
    f.cursor = 0;
    press(f, key(maya::SpecialKey::Enter));
    REQUIRE(f.choosing());
    CHECK_FALSE(press(f, key(maya::SpecialKey::Escape)).close);
    CHECK(press(f, key(maya::SpecialKey::Escape)).close);
}

TEST_CASE("form: an empty form still answers Esc") {
    // 冷冻实际击中的堕落的个案:一个没有行的表
    // 单不能成为键盘黑洞.
    Form empty;
    auto a = keys::translate(empty, key(maya::SpecialKey::Escape));
    REQUIRE(a.has_value());
    CHECK(keys::apply(empty, *a).close);
}

TEST_CASE("form: the key router depends only on focus, not on the rows") {
    // 电话 标志 超载是合同 : 两个 布尔决定整个密钥
    // 映射( 编辑, 选择: 焦点) -shaped 偷看过行
    // 的路由器 过期 中点 -batch 并放下了钥匙。
    const auto esc = key(maya::SpecialKey::Escape);
    const auto j   = chr(U'j');

    // 浏览: 打印器总是 TypeToEdit - 电话
    // ROUTER 保留光标 -independent (其创始合同:两桶,
    // 无排),和 apply() 当焦点行不是文本时,
    // 将意图降低 -like 偷看行的路由器走 过期 中点 -batch
    // 并默默地吃键盘(然后读一个字母).
    {
        auto a = keys::translate(/*编辑=*/false, /*选择=*/false, j);
        REQUIRE(a.has_value());
        CHECK(a->intent == keys::Intent::TypeToEdit);
        CHECK(a->ch == U'j');
    }
    // 编辑:相同的密钥是文本,从不导航.
    {
        auto a = keys::translate(/*编辑=*/true, /*选择=*/false, j);
        REQUIRE(a.has_value());
        CHECK(a->intent == keys::Intent::Insert);
        CHECK(a->ch == U'j');
    }
    // 选择: 开放的滴水滴水吞下可打印的(a) 枚举 列表没有过滤器;
    // 向窗体下漏出密钥将是一个错误 -target).
    {
        auto a = keys::translate(/*编辑=*/false, /*选择=*/true, j);
        REQUIRE(a.has_value());
        CHECK(a->intent == keys::Intent::None);
    }
    // 而每个模式都回答Esc——在路由器级别上,逃生保证.
    for (auto [e, c] : {std::pair{false, false}, {true, false}, {false, true}}) {
        auto a = keys::translate(e, c, esc);
        REQUIRE(a.has_value());
        CHECK(a->intent != keys::Intent::None);
    }
}

TEST_CASE("form: caret offset lands after multi-byte text") {
    // maya 连结 光标 由 字节 字符索引中,将
    // caret between é and 漢 instead of after 漢.
    auto f = Builder{"UTF"}.text("t", "T", "").build();
    f.cursor = 0;
    press(f, key(maya::SpecialKey::Enter));
    press(f, chr(U'é'));
    press(f, chr(U'漢'));
    const auto& v = std::get<field::Text>(f.find("t")->value).value;
    CHECK(v.size() == 5);
    const auto at = caret_offset(f.find("t")->value, true);
    CHECK(at == 5);
    CHECK(maya::panel::detail::with_caret(v, at) == v + "\xe2\x96\x88");
}

TEST_CASE("form: a Number row being edited has a caret") {
    auto f = Builder{"N"}.number("n", "N", 42, 0, 999).build();
    CHECK(caret_offset(f.find("n")->value, false) == std::string::npos);
    CHECK(caret_offset(f.find("n")->value, true) == 2);
}
