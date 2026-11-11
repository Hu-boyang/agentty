// settings_list_scroll_test ——设置列表为 真实 滚动 选择面板.
//
// 插件( X) 面板 (Ctrl +K → 插件,或/mcp 服务
// 器)从活服务器构建 快照 ; 单一服务器广告125工具生成135+行。
// 列表曾经是 一个 选择面板 以滚动 = 键盘构建 nullptr 由 .
// . 由 : maya 用于描绘 正文 以静态块的形式,
// 所以在脚下划行,通过 弹出面板 边界,关闭屏幕。
//
// 由 Pins 3 属性 渲染 编号 真实 视图通过 maya (canvas平坦
// ——与 panel_sections_render_test):
//   1. 边界: 画布在弹出面板的底边框上结束; 无行
//      画在它下面。
//   2. 剪辑:大名单 渲染 仅 ~ viewport_h 排行数.
//   3. 可滚动:驱动选择的深层移动窗口——早期
//      排出画布, 晚排变得可见。
//
// 移植自 PR #34(被切割)横跨面板的重构:ov:_pn::, settings_list_view
// →settings_list_panel , RO – 项目词汇表。

#include "agtest.hpp"

#include "agentty/runtime/model.hpp"
#include "agentty/runtime/panel/settings/list.hpp"
#include "agentty/runtime/view/panels.hpp"

#include <maya/render/canvas.hpp>
#include <maya/render/renderer.hpp>
#include <maya/style/theme.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace pn = agentty::ui::panel;

using namespace agentty;

namespace {

// 插件 快照 与 `n` 启用连接服务器上的工具。
Model big_plugins_model(int n) {
    Model m;
    mcp::PluginModel snap;
    mcp::ServerState s;
    s.name      = "big";
    s.connected = true;
    for (int i = 0; i < n; ++i)
        s.tools.push_back({"tool_" + std::to_string(i),
                           "test tool " + std::to_string(i), true, false});
    snap.servers.push_back(std::move(s));
    m.ui.plugins = std::move(snap);
    return m;
}

// 渲染 仅 编号 选择面板 元素,将画布平整为文本(方法与
// panel_sections_render_test:
// render_tree + 字符 归并 ; 无 -ASCII 归并 改
// 为 '?'。画布是故意的 SHORT (48行):虫子溢出,
// 弹出面板 过 终端 底部,所以短画布可以视同漆色的行超越边界。
std::string render_picker(const Model& m, int width = 100, int height = 48) {
    auto root = ui::settings_list_panel(m);
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
        out.push_back('\n');
    }
    while (!out.empty() && out.back() == '\n') out.pop_back();
    return out;
}

std::vector<std::string> split_lines(const std::string& s) {
    std::vector<std::string> lines;
    std::size_t at = 0;
    while (at <= s.size()) {
        auto nl = s.find('\n', at);
        if (nl == std::string::npos) { lines.push_back(s.substr(at)); break; }
        lines.push_back(s.substr(at, nl - at));
        at = nl + 1;
    }
    return lines;
}

bool has(const std::string& s, std::string_view needle) {
    return s.find(needle) != std::string::npos;
}

int count_tool_rows(const std::string& screen, int n) {
    int seen = 0;
    for (int i = 0; i < n; ++i)
        if (has(screen, "tool_" + std::to_string(i))) ++seen;
    return seen;
}

} // 命名空间结束。

TEST_CASE("settings list: a 120-tool server is one row until opened") {
    Model m = big_plugins_model(120);
    m.ui.panel.descend(pn::SettingsList{{settings::Category::Plugins, 0}});
    const std::string screen = render_picker(m);
    INFO(screen);
    CHECK(has(screen, "big"));
    CHECK(has(screen, "120 tools"));
    CHECK_MESSAGE(!has(screen, "tool_0"), "collapsed: no tool rows");
    CHECK(has(screen, "show tools"));   // 脚印上说要怎么打开
}

TEST_CASE("settings list: bounded, clipped, and scrollable") {
    Model m = big_plugins_model(120);
    {
        pn::SettingsList o{{settings::Category::Plugins, 2}};
        o.expanded = {"big"};   // 工具崩溃,直到服务器打开
        m.ui.panel.descend(std::move(o));
    }

    const std::string screen = render_picker(m);
    INFO(screen);

    // ── 1: 定界——无漆 弹出面板 下边框 ──
    {
        const auto lines = split_lines(screen);
        int last_nonblank = -1;
        for (int y = static_cast<int>(lines.size()) - 1; y >= 0; --y)
            if (!lines[static_cast<std::size_t>(y)].empty()) {
                last_nonblank = y;
                break;
            }
        REQUIRE(last_nonblank >= 0);
        const std::string& bottom =
            lines[static_cast<std::size_t>(last_nonblank)];
        CHECK_MESSAGE(bottom.find("tool_") == std::string::npos,
                      "no list row is painted at/below the bottom border");
        int footer_row = -1;
        for (int y = 0; y < last_nonblank; ++y)
            if (lines[static_cast<std::size_t>(y)].find("esc close")
                    != std::string::npos
                || lines[static_cast<std::size_t>(y)].find("esc back")
                    != std::string::npos)
                footer_row = y;
        CHECK_MESSAGE(footer_row >= 0, "footer key hints are inside the modal");
        CHECK_MESSAGE(footer_row < last_nonblank,
                      "footer renders above the bottom border (no underpaint)");
    }

    // ── 2: 剪辑——只有 可视区域 窗口 渲染 ──
    const int seen = count_tool_rows(screen, 120);
    CHECK_MESSAGE(seen > 0, "some tool rows render");
    CHECK_MESSAGE(seen < 40, "list is clipped to the viewport, not painted whole");

    // ── 3:滚动——深层选择移动窗口 ──
    // 0行=服务器行, 1..n= tool_i (摘要是: 面板 头文件
    // ——光标 117 坐上 tool_116.
    const bool early_before = has(screen, "tool_0\xe2\x80\xa6") || has(screen, " tool_0");
    {
        pn::SettingsList o{{settings::Category::Plugins, 117}};
        o.expanded = {"big"};
        m.ui.panel.descend(std::move(o));
    }
    const std::string deep = render_picker(m);
    INFO(deep);
    CHECK_MESSAGE(has(deep, "tool_116"), "deep row is reachable and rendered");
    if (early_before)
        CHECK_MESSAGE(deep.find(" tool_0 ") == std::string::npos,
                      "early row scrolled out of the viewport window");
}
