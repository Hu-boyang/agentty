// stats_render_probe — 数据面板是否真的画出来?
//
// 单元测试涵盖投影(数字)和条形(几何).
// PANEL也不是渲染,所以一个将两者错联在一起的宿主——一个
// 标签索引一个字的关闭,一个空的正文,一个字条从不出现——通过两者,
// 仍然显示一个破的面板。这驱动了真实的视图功能,
// 并打印出一个用户会看到的东西。

#include "agtest.hpp"

#include "agentty/domain/stats/facts.hpp"
#include "agentty/domain/stats/tabs.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/panel/stats.hpp"
#include "agentty/runtime/view/panels.hpp"

#include <maya/render/canvas.hpp>
#include <maya/render/renderer.hpp>
#include <maya/style/theme.hpp>

#include <cstdio>
#include <string>
#include <vector>

using namespace agentty;
namespace pn = agentty::ui::panel;

namespace {

Message served(const char* model, smart::ModelRole role) {
    Message m;
    m.role = Role::Assistant;
    m.served_model = ModelId{model};
    m.served_role  = role;
    return m;
}

std::vector<std::string> render_rows(const Model& m, int w, int h) {
    maya::StylePool pool;
    maya::Canvas canvas(w, h, &pool);
    maya::render_tree(ui::stats_panel(m), canvas, pool, maya::theme::native,
                      /*auto_height=*/true);
    std::vector<std::string> rows;
    for (int y = 0; y < h; ++y) {
        std::string s;
        for (int x = 0; x < w; ++x) {
            const auto c = canvas.get(x, y);
            if (c.character >= 0x20 && c.character < 0x7F)
                s += static_cast<char>(c.character);
            else if (c.character == 0 || c.character == U' ') s += ' ';
            else s += '?';
        }
        while (!s.empty() && s.back() == ' ') s.pop_back();
        rows.push_back(std::move(s));
    }
    while (!rows.empty() && rows.back().empty()) rows.pop_back();
    return rows;
}

bool any_row_has(const std::vector<std::string>& rows, std::string_view n) {
    for (const auto& r : rows)
        if (r.find(n) != std::string::npos) return true;
    return false;
}

}  // 命名空间结束。

TEST_CASE("stats panel: renders a real thread") {
    Model m;
    m.d.current.messages = {
        served("claude-opus-4-5",   smart::ModelRole::Strategic),
        served("claude-opus-4-5",   smart::ModelRole::Strategic),
        served("claude-sonnet-4-5", smart::ModelRole::Implementation),
        served("claude-haiku-4-5",  smart::ModelRole::Utility),
        served("claude-haiku-4-5",  smart::ModelRole::Utility),
    };
    m.ui.panel.descend(pn::Stats{});

    const auto rows = render_rows(m, 76, 40);
    std::printf("\n--- stats panel: Session (5 turns) ---\n");
    for (const auto& r : rows) std::printf("|%s\n", r.c_str());

    CHECK(!rows.empty());                       // 画了些东西
    CHECK(any_row_has(rows, "Stats"));          // 编号 帧 标题
    // 带带带带 AVAILABLE 选项卡——会话和模型总是很聪明,
    // 因为这些转弯经过了路由,以及 不 工具或 缓存.
    CHECK(any_row_has(rows, "Session"));
    CHECK(any_row_has(rows, "Models"));
    CHECK(any_row_has(rows, "Smart"));
    CHECK(!any_row_has(rows, "Tools"));
    CHECK(any_row_has(rows, "5 turns"));
}

TEST_CASE("stats panel: the Smart tab states its denominator") {
    Model m;
    m.d.current.messages = {
        served("claude-opus-4-5",   smart::ModelRole::Strategic),
        served("claude-opus-4-5",   smart::ModelRole::Strategic),
        served("claude-sonnet-4-5", smart::ModelRole::Implementation),
        served("claude-haiku-4-5",  smart::ModelRole::Utility),
        served("claude-haiku-4-5",  smart::ModelRole::Utility),
    };
    auto open = pn::Stats{};
    open.tab = stats::Tab::Smart;
    m.ui.panel.descend(std::move(open));

    const auto rows = render_rows(m, 76, 40);
    std::printf("\n--- stats panel: Smart ---\n");
    for (const auto& r : rows) std::printf("|%s\n", r.c_str());

    CHECK(any_row_has(rows, "By role") || any_row_has(rows, "BY ROLE"));
    CHECK(any_row_has(rows, "Strategic"));
    CHECK(any_row_has(rows, "Utility"));
    // 5个路由转弯中3个在战略下方运行.
    CHECK(any_row_has(rows, "60%"));
}

TEST_CASE("stats panel: the empty state is not a wall of zeroes") {
    Model m;
    m.ui.panel.descend(pn::Stats{});
    const auto rows = render_rows(m, 76, 24);
    std::printf("\n--- stats panel (empty) ---\n");
    for (const auto& r : rows) std::printf("|%s\n", r.c_str());

    CHECK(!rows.empty());
    // 没有任何可言的标签是完全没有的,
    // 而不是现在和空的——因此没有读数,也没有解释零。
    CHECK(!any_row_has(rows, "By role"));
    CHECK(!any_row_has(rows, "Smart"));
    CHECK(!any_row_has(rows, "Cache"));
}

TEST_CASE("stats panel: survives a narrow terminal") {
    // 面板是一种观点:它必须降解,绝不中止。
    Model m;
    m.d.current.messages = {served("m", smart::ModelRole::Utility)};
    m.ui.panel.descend(pn::Stats{});
    for (int w : {20, 30, 40, 60, 100}) {
        const auto rows = render_rows(m, w, 30);
        CHECK_MESSAGE(!rows.empty(), "renders at width " << w);
    }
}
