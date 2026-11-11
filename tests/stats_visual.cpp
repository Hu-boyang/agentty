// stats_visual 将每个数据标签都丢到 终端 ,颜色。
//
// 电话 单元测试 列上加亮号 POSITIONS 和数字。
// 也不能告诉你面板是否令人愉快。这是用来构建现实的工具。 线程 ,
// 驱动真实视图函数,并释放真实 ANSI 这么说吧
// `./build/dev/stats_visual` 显示一个
// 用户所看到的——栏、带、盲文图和所有图。
//
// 不是测试条目( Q) NO_TEST :它的产出是给人类的,是用来证实
// "this looks nice" 说无.
//
//   ./build/stats_visual              every tab, 76 cols
//   ./build/stats_visual 100          every tab, 100 cols
//   ./build/stats_visual 76 Cache     one tab
//   ./build/stats_visual 76 Tools 8   one tab, scrolled 8 rows
//
// 比面板高一个标签 可视区域 SCROLLS 真正的应用,
// 哪个 -shot 垃圾堆不能做, 所以这个工具设置
// 高 可视区域 并同时打印整个标签。这就是关键所在:
// 您正在检查版式,而要滚动的图是您无法审查的图。
//
// 电话 线程 它的构造是故意的混乱——一个失败的
// 工具 调用 转弯,转弯 缓存-warm 段,
// 不平衡的符号计数—— 因为只看干净数据的数据块是生产中
// 看起来不好的数据块。

#include "agentty/domain/stats/tabs.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/panel/stats.hpp"
#include "agentty/runtime/view/panels.hpp"

#include <maya/render/canvas.hpp>
#include <maya/render/renderer.hpp>
#include <maya/style/theme.hpp>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

using namespace agentty;
namespace pn = agentty::ui::panel;

namespace {

Message user_turn(const char* text) {
    Message m;
    m.role = Role::User;
    m.text = text;
    return m;
}

// 有个助手转身 随身携带
Message turn(const char* model, smart::ModelRole role,
             std::uint32_t in, std::uint32_t out,
             std::uint32_t cache_r = 0, std::uint32_t cache_w = 0,
             std::uint32_t ttft = 420, std::uint32_t stream_ms = 3800,
             std::uint32_t reasoning = 0, std::uint16_t retries = 0) {
    Message m;
    m.role = Role::Assistant;
    m.served_model = ModelId{model};
    m.served_role  = role;
    Message::Telemetry t;
    t.input_tokens     = in;
    t.output_tokens    = out;
    t.reasoning_tokens = reasoning;
    t.cache_read       = cache_r;
    t.cache_creation   = cache_w;
    t.ttft_ms          = ttft;
    t.stream_ms        = stream_ms;
    t.wire_bytes       = out * 4;
    t.transient_retries = retries;
    m.telemetry = t;
    if (reasoning) {
        m.reasoning_ms = 2400;
        m.thinking_blocks.push_back({"…", "sig", ""});
    }
    return m;
}

void add_tool(Message& m, const char* name, int ms, bool ok = true) {
    ToolUse t;
    t.id   = ToolCallId{std::string{name} + std::to_string(ms)};
    t.name = ToolName{name};
    const auto start = std::chrono::steady_clock::now();
    if (ok) {
        ToolUse::Done d;
        d.started_at  = start;
        d.finished_at = start + std::chrono::milliseconds{ms};
        t.status = d;
    } else {
        ToolUse::Failed f;
        f.started_at  = start;
        f.finished_at = start + std::chrono::milliseconds{ms};
        t.status = f;
    }
    m.tool_calls.push_back(std::move(t));
}

Model realistic_thread() {
    Model m;
    m.d.current.id = ThreadId{"visual"};
    using R = smart::ModelRole;

    auto push = [&](const char* prompt, Message a) {
        m.d.current.messages.push_back(user_turn(prompt));
        m.d.current.messages.push_back(std::move(a));
    };

    // 冷的第一转:没有缓存,模型认为很硬.
    {
        auto a = turn("claude-opus-4-5", R::Strategic, 8200, 1400, 0, 8200,
                      1200, 9400, 900);
        add_tool(a, "read", 42);
        add_tool(a, "grep", 88);
        push("plan the refactor", std::move(a));
    }
    // 暖转:前缀现在被缓存,因此输入下降和TTFT减半.
    {
        auto a = turn("claude-sonnet-4-5", R::Implementation, 900, 2100,
                      9600, 0, 260, 6100);
        add_tool(a, "read", 31);
        add_tool(a, "edit", 120);
        add_tool(a, "edit", 96);
        push("apply it to the first file", std::move(a));
    }
    {
        auto a = turn("claude-sonnet-4-5", R::Implementation, 1100, 1800,
                      11800, 0, 240, 5200);
        add_tool(a, "edit", 140);
        add_tool(a, "bash", 2400);          // 慢的,对P95
        push("and the second", std::move(a));
    }
    // 廉价的公用车轮.
    {
        auto a = turn("claude-haiku-4-5", R::Utility, 400, 320, 12400, 0,
                      140, 900);
        add_tool(a, "grep", 24);
        push("what changed?", std::move(a));
    }
    // 转弯有麻烦: 二 重试 和一个失败的工具 调用.
    {
        auto a = turn("claude-sonnet-4-5", R::Implementation, 1300, 2600,
                      13100, 0, 3100, 11200, 0, 2);
        a.telemetry->mid_stream_failures = 1;
        add_tool(a, "bash", 5200, /*ok=*/false);
        add_tool(a, "read", 38);
        push("run the tests", std::move(a));
    }
    // 一代大人物,为地块的高峰.
    {
        auto a = turn("claude-opus-4-5", R::Strategic, 2200, 5400, 14200, 0,
                      680, 21400, 1800);
        push("write the design doc", std::move(a));
    }
    {
        auto a = turn("claude-haiku-4-5", R::Utility, 500, 260, 15900, 0,
                      120, 700);
        push("commit it", std::move(a));
    }

    // 主动性 -retrieval 最后一个问题
    auto& q = m.d.current.messages.emplace_back(user_turn("why is it slow?"));
    Message::ProactiveContext pc;
    pc.confidence = 0.82;
    q.proactive = pc;
    m.d.current.messages.push_back(
        turn("claude-sonnet-4-5", R::Implementation, 1000, 1600, 16400, 0,
             300, 4400));

    // 长 末尾 工具的编辑 调用 ,形状像真正的编码会话:
    // 主导 shell/edit 双对,几张读数,
    // 以及使用过一两次的工具。这就是锻炼环的 "other" 桶
    // 及其直截了当的调色板——一个整洁的4 -tool 线程 才不会呢
    {
        auto a = turn("claude-sonnet-4-5", R::Implementation, 1200, 1900,
                      15100, 0, 280, 5600);
        for (int i = 0; i < 22; ++i) add_tool(a, "shell", 40 + i * 7);
        for (int i = 0; i < 9;  ++i) add_tool(a, "edit",  90 + i * 11);
        for (int i = 0; i < 4;  ++i) add_tool(a, "read",  25 + i * 5);
        add_tool(a, "write", 130);
        add_tool(a, "git_commit", 260);
        add_tool(a, "list_dir", 18);
        add_tool(a, "shell", 4400, /*ok=*/false);
        push("land the whole change", std::move(a));
    }

    return m;
}

// 渲染 一个面板,用真实的打印 SGR.
void dump(const Model& m, int w, int scroll) {
    maya::StylePool pool;
    maya::Canvas canvas(w, 60, &pool);
    // TWICE ,加上 -applied 中间。 maya's
    // ScrollState 夹着y对 max_y ,
    // 和 max_y 只写回 之后 排版传球——在寒冷的状态下,
    // 第一个 渲染 夹着 y 到 0 , 偏移会默默丢失。 真正的应用
    // 程序永远不会注意到( ) 帧 2的测量数据来自 帧 1),
    // 但有一个 -shot 垃圾必须运行 通道,恢复它要求的,然后油漆。
    maya::render_tree(ui::stats_panel(m), canvas, pool, maya::theme::native,
                      /*auto_height=*/true);
    // ONE_PASS =1 站在这里: 诊断显示除了此工具
    // 的复制品之外, 还有一个真正的布局错误 渲染.
    if (std::getenv("ONE_PASS")) return;
    if (auto* o = m.ui.panel.get<pn::Stats>()) {
        o->scroll.y = scroll;
        o->scroll.clamp();
    }
    maya::render_tree(ui::stats_panel(m), canvas, pool, maya::theme::native,
                      /*auto_height=*/true);
    if (std::getenv("STATS_VISUAL_DEBUG")) {
        const auto el = ui::stats_panel(m);
        std::fprintf(stderr, "[scroll y=%d max_y=%d  measured(1<<14)=%d measured(w)=%d]\n",
                     m.ui.panel.get<pn::Stats>()->scroll.y,
                     m.ui.panel.get<pn::Stats>()->scroll.max_y,
                     maya::measure_element(el, 1 << 14).height.value,
                     maya::measure_element(el, w).height.value);
    }

    int last = 0;
    for (int y = 0; y < 60; ++y)
        for (int x = 0; x < w; ++x)
            if (canvas.get(x, y).character != 0
                && canvas.get(x, y).character != U' ') { last = y; break; }

    for (int y = 0; y <= last; ++y) {
        std::string line;
        std::uint16_t cur = 0xFFFF;
        for (int x = 0; x < w; ++x) {
            const auto cell = canvas.get(x, y);
            if (cell.style_id != cur) {
                line += maya::Style::reset_sgr();
                line += pool.get(cell.style_id).to_sgr();
                cur = cell.style_id;
            }
            const char32_t ch = cell.character ? cell.character : U' ';
            if (ch < 0x80) {
                line += static_cast<char>(ch);
            } else if (ch < 0x800) {
                line += static_cast<char>(0xC0 | (ch >> 6));
                line += static_cast<char>(0x80 | (ch & 0x3F));
            } else if (ch < 0x10000) {
                line += static_cast<char>(0xE0 | (ch >> 12));
                line += static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
                line += static_cast<char>(0x80 | (ch & 0x3F));
            } else {
                line += static_cast<char>(0xF0 | (ch >> 18));
                line += static_cast<char>(0x80 | ((ch >> 12) & 0x3F));
                line += static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
                line += static_cast<char>(0x80 | (ch & 0x3F));
            }
        }
        line += maya::Style::reset_sgr();
        std::printf("%s\n", line.c_str());
    }
}

}  // 命名空间结束。

int main(int argc, char** argv) {
    const int w = argc > 1 ? std::atoi(argv[1]) : 76;
    const std::string_view only = argc > 2 ? argv[2] : "";
    const int scroll = argc > 3 ? std::atoi(argv[3]) : 0;

    // 高点 可视区域 故无卷轴出垃圾场。
    // panel_viewport_h () 读作: LINES 事实就是如此
    //
    // COLUMNS 也是因为这个原因,
    // 而它就是 加载-bearing 而不是清理:小组
    // 决定 COLUMN COUNT 从 终端 宽度,
    // 而不是从宽度提供其组件——在弹性取出边框、
    // 垫条和滚动条之前,要约到达,所以不能相信它可以
    // 作出布局决定。 COLUMNS 80,
    // 每个80块以上的垃圾 都变成了单数 -column 工具
    // 是说谎的,不是面板。
#ifdef _WIN32
    _putenv_s("LINES", "120");
    _putenv_s("COLUMNS", std::to_string(w).c_str());
#else
    setenv("LINES", "120", 1);
    setenv("COLUMNS", std::to_string(w).c_str(), 1);
#endif

    Model m = realistic_thread();
    m.ui.panel.descend(pn::Stats{});

    // 归并一旦知道如此可用性,然后走可见的标签——同
    // 一套条图,所以这个工具不能显示面板不会显示的
    // 标签.
    auto* o = m.ui.panel.get<pn::Stats>();
    const auto& f = o->projection.refresh(m.d.current);

    for (auto t : stats::visible_tabs(f)) {
        if (!only.empty() && stats::tab_title(t) != only) continue;
        o->tab = t;
        o->scroll.y = scroll;
        std::printf("\n\x1b[1m── %s ──\x1b[0m  %s\n\n",
                    std::string{stats::tab_title(t)}.c_str(),
                    std::string{stats::tab_subtitle(t)}.c_str());
        dump(m, w, scroll);
    }
    std::printf("\n");
    return 0;
}
