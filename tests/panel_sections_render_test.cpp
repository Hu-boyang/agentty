// panel_sections_render_test ——引信型号. 选择面板's SECTION
// HEADERS ,声称已实际交付 存储单元.
//
// 浏览为 2 -section 版式( 简况) "recent" /
// "all providers" ),没有 -provider 分拆。
// 该 头文件 谓 SPELLED 风景中,所以这个 渲染 编号 选择
// 面板 通过真实视图+ maya 读到 头文件 反向画布,而不是给源头加固
// ——源头 -text 断言不能告诉你 头文件 可到达或其标题的行在屏幕上。

#include "agtest.hpp"

#include "agentty/runtime/model.hpp"
#include "agentty/runtime/fused_models.hpp"
#include "agentty/runtime/view/panels.hpp"

#include <maya/render/canvas.hpp>
#include <maya/render/renderer.hpp>
#include <maya/style/theme.hpp>

#include <string>
#include <vector>

using namespace agentty;
namespace pn = agentty::ui::panel;

namespace {

ModelInfo mk(std::string id, std::string name, std::string provider) {
    ModelInfo mi;
    mi.id = ModelId{std::move(id)};
    mi.display_name = std::move(name);
    mi.provider = std::move(provider);
    mi.context_window = 200000;
    return mi;
}

ProviderCatalog cat(std::string id, std::string label,
                    std::vector<ModelInfo> models) {
    ProviderCatalog c;
    c.provider_id = std::move(id);
    c.label = std::move(label);
    c.state = ProviderCatalog::State::Ready;
    c.models = std::move(models);
    return c;
}

// 渲染 仅 编号 选择面板 元素(不是整个应用程序布局),并将画布平整为文本。
// -ASCII 归并 改为 '?' ——各节标题为: ASCII ,
// 并显示 badge/ 环绕着它们 的铬不是我们所主张的
std::string render_panel(const Model& m, int width = 100, int height = 200) {
    auto root = ui::models_panel(m);
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
    return out;
}

bool has(const std::string& hay, std::string_view needle) {
    return hay.find(needle) != std::string::npos;
}

// 构建一个 Model 与 选择面板 打开 `cats` ,作为 状态更新函数 将:
// fused_rows 是那个 状态更新函数-maintained 缓存 视图
// 为: 从未重建 帧 ,所以一个 渲染 测试必须同样地充斥它。
Model picker_model(const std::vector<ProviderCatalog>& cats,
                   ModelRef active = {}) {
    Model m;
    ui::FusedInputs in;
    in.catalogs = &cats;
    in.active   = active;
    m.d.fused_rows = ui::build_fused_rows(in);
    m.ui.panel.descend(pn::Models{});
    return m;
}

}  // 命名空间结束。

TEST_CASE("picker: browse lists every provider under one 'all providers' header") {
    // 拥有活动提供者集,其非 -recent 行数不是 被冲出一个
    // "from this provider" 块块——每个非 -recent 模式,
    // 不论提供者,都生活在一个单一的 "all providers" 头文件.
    const std::vector<ProviderCatalog> cats{
        cat("anthropic", "Anthropic",
            {mk("claude-opus-4-5",  "Claude Opus 4.5",  "anthropic"),
             mk("claude-haiku-4-5", "Claude Haiku 4.5", "anthropic")}),
        cat("openai", "OpenAI", {mk("gpt-5", "GPT-5", "openai")}),
    };
    const Model m = picker_model(cats, ModelRef{"anthropic", "claude-opus-4-5"});
    const std::string screen = render_panel(m);

    INFO(screen);
    // 平面一节——无 -provider 分头。
    CHECK(has(screen, "ALL PROVIDERS"));
    CHECK_FALSE(has(screen, "from this provider"));
    CHECK_FALSE(has(screen, "from all other providers"));
    // 开始行 两者 提供者 渲染 下方是这个
    CHECK(has(screen, "Claude Haiku 4.5"));   // 活跃提供者, 非-recent
    CHECK(has(screen, "GPT-5"));              // 另一供应商
}

TEST_CASE("picker: with NO active provider, the header is still 'all providers'") {
    // 新鲜的 -install 形状: 无活动。 头文件 情况
    // 没有改变——从未出现下降的反差。
    const std::vector<ProviderCatalog> cats{
        cat("anthropic", "Anthropic",
            {mk("claude-opus-4-5", "Claude Opus 4.5", "anthropic")}),
        cat("openai", "OpenAI", {mk("gpt-5", "GPT-5", "openai")}),
    };
    const Model m = picker_model(cats);           // 活动左 空
    const std::string screen = render_panel(m);

    INFO(screen);
    CHECK_FALSE(has(screen, "from this provider"));
    CHECK_FALSE(has(screen, "from all other providers"));
    CHECK(has(screen, "ALL PROVIDERS"));
    // 模型仍列于其下.
    CHECK(has(screen, "Claude Opus 4.5"));
    CHECK(has(screen, "GPT-5"));
}

TEST_CASE("picker: the active model leads, and its row is on screen") {
    // 针头是建材 不变量 (fused_models_test
    // 证明命令; 这针是 选择面板 RENDERS
    // 而不是正确命令它 然后画出其他东西
    const std::vector<ProviderCatalog> cats{
        cat("anthropic", "Anthropic",
            {mk("claude-opus-4-5",  "Claude Opus 4.5",  "anthropic"),
             mk("claude-haiku-4-5", "Claude Haiku 4.5", "anthropic")}),
    };
    const std::vector<ModelRef> recents{
        ModelRef{"anthropic", "claude-haiku-4-5"},
        ModelRef{"anthropic", "claude-opus-4-5"},
    };
    Model m;
    ui::FusedInputs in;
    in.catalogs = &cats;
    in.recents  = &recents;
    in.active   = ModelRef{"anthropic", "claude-opus-4-5"};
    m.d.fused_rows = ui::build_fused_rows(in);
    m.ui.panel.descend(pn::Models{});

    const std::string screen = render_panel(m);
    INFO(screen);
    CHECK(has(screen, "RECENT"));   // ┌─ RECENT divider (^K-style, uppercased)
    // 字母顺序前的海库类型; 活动 Opus 仍然必须是第一个 RECENT 排队,
    // 因为 选择面板 预览 -selects 第0行 开始
    const auto recent_hdr = screen.find("RECENT");
    const auto opus       = screen.find("Claude Opus 4.5");
    const auto haiku      = screen.find("Claude Haiku 4.5");
    REQUIRE(recent_hdr != std::string::npos);
    REQUIRE(opus  != std::string::npos);
    REQUIRE(haiku != std::string::npos);
    CHECK(opus < haiku);
}
