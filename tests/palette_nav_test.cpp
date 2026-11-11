// 通过一个分组的选择面板进行导航.
//
// 节次 头文件 已经 NON-SELECTABLE :它们没有动作,
// 所以一个光标降落在其中就是死键压。 状态更新函数 索引 头
// 文件-FREE 匹配列表和显示位置的视图映射, 这意味着在
// 状态更新函数 一定要知道,我必须知道 头文件 已存在,
// 任何箭头键都无法停止。
//
// 将结束的钉子钉上, 因为失败是沉默的: 光标在 头
// 文件 仍然 渲染 (这行看起来只是没有选择),
// 所以唯一的症状是箭形的按键似乎毫无作用.

#include "agtest.hpp"

#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/view/panels.hpp"
#include "agentty/runtime/panel/palette.hpp"

#include <maya/print.hpp>

#include <string>
#include <vector>

namespace pn = agentty::ui::panel;
using namespace agentty;

namespace {

// 已作出 帧 减型号
std::string frame(const Model& m) {
    return maya::render_to_string(ui::palette_panel(m), 82);
}

// 哪个线条带有光标栏,或 -1.
// 栏是"you are here"的ONE标记,
// 所以要求输出是诚实的检查——检查模型状态只会证明模型自己同意.
int cursor_line(const std::string& out) {
    int line = 0;
    std::size_t start = 0;
    while (start <= out.size()) {
        const std::size_t nl = out.find('\n', start);
        const std::string s = out.substr(start, nl - start);
        // U+258E,位于行标车道(过去为面板边框+垫板).
        if (s.find("\xe2\x96\x8e") != std::string::npos) return line;
        if (nl == std::string::npos) break;
        start = nl + 1;
        ++line;
    }
    return -1;
}

// 这是一条线还是一条线 头文件? 头文件 以规则方式划出的
// 唯一一行,以运行到右边缘。
bool is_header_line(const std::string& out, int want) {
    int line = 0;
    std::size_t start = 0;
    while (start <= out.size()) {
        const std::size_t nl = out.find('\n', start);
        const std::string s = out.substr(start, nl - start);
        if (line == want)
            return s.find("\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80") != std::string::npos;
        if (nl == std::string::npos) break;
        start = nl + 1;
        ++line;
    }
    return false;
}

store::Settings g_settings;

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

TEST_CASE("palette nav: the cursor never lands on a section header") {
    install_stub_deps();

    Model m;
    m.ui.panel.descend(pn::Palette{{}});
    m.d.pending_changes.push_back(FileChange{});

    // 全部列。 每站必须是真实的,可运行的行。
    // 组图显示 头文件 和一箭停止在一个人身上,
    // 看起来像一个没有做任何事情的钥匙。
    const int rows = static_cast<int>(filtered_commands("").size());
    REQUIRE(rows > 0);

    for (int step = 0; step < rows + 2; ++step) {
        const auto out = frame(m);
        const int cl = cursor_line(out);
        CHECK(cl >= 0);
        CHECK_FALSE(is_header_line(out, cl));

        auto [next, cmd] = app::update(std::move(m), Msg{PaletteMove{+1}});
        m = std::move(next);
    }
}

TEST_CASE("palette nav: moving up also skips headers") {
    install_stub_deps();

    Model m;
    m.ui.panel.descend(pn::Palette{{}});
    m.d.pending_changes.push_back(FileChange{});

    const int rows = static_cast<int>(filtered_commands("").size());
    REQUIRE(rows > 0);

    for (int step = 0; step < rows + 2; ++step) {
        auto [next, cmd] = app::update(std::move(m), Msg{PaletteMove{-1}});
        m = std::move(next);

        const auto out = frame(m);
        const int cl = cursor_line(out);
        CHECK(cl >= 0);
        CHECK_FALSE(is_header_line(out, cl));
    }
}

TEST_CASE("palette nav: the cursor index addresses the header-free list") {
    // 电话 状态更新函数 一定要留下来 头文件-UNAWARE 如果它把
    // 显示行索引化,增加一个部分会默默地改变每个选择——症状是:
    // "Enter ran the wrong command" 而不是坠机
    install_stub_deps();

    Model m;
    m.ui.panel.descend(pn::Palette{{}});
    auto* o = m.ui.panel.get<pn::Palette>();
    REQUIRE(o != nullptr);

    const auto cmds = filtered_commands("");
    REQUIRE(!cmds.empty());

    // 索引0是FIRST COMMAND,而不是第一个显示行(
    // 在分组视图中是头文件).
    CHECK(o->index == 0);
    const auto out = frame(m);
    const int cl = cursor_line(out);
    CHECK(cl >= 0);
    CHECK_FALSE(is_header_line(out, cl));
    // ...光标下的一行是第一个命令。
    CHECK(out.find(std::string{cmds.front()->label}) != std::string::npos);
}
