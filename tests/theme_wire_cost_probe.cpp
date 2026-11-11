// 主题有什么变化? COST 编号 终端?
//
// 光标移动 重绘 主题更改 重绘 每隔一段时间 存储单元 屏幕上——每一
// 颜色都变了,所以 maya's 存储单元 diff 找不到不变的跨度,
// 并释放整个 可视区域 慢一点 链接 ,或 a ,或 终端 慢到这个
// 解析 SGR ,这是区别于 "instant" 财务报告和已审计
// 财务报表 "mushy" 即便在 CPU 成本是几毫秒。
//
// 每个按键压印出字节用于主题移动相对于一个平版光标移动,
// 所以两者直接可比较.
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/msg.hpp"
#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/panel/appearance.hpp"
#include "agentty/runtime/view/view.hpp"

#include <maya/core/render_context.hpp>
#include <maya/print.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace agentty;
namespace pn = agentty::ui::panel;
using clk = std::chrono::steady_clock;

namespace {

Model seeded(int msgs) {
    Model m;
    for (int i = 0; i < msgs; ++i) {
        Message msg;
        msg.role = (i % 2) ? Role::Assistant : Role::User;
        msg.text = "## Heading " + std::to_string(i) +
            "\n\nProse with `code` and *emphasis*, long enough to wrap across "
            "a couple of lines in a normal terminal.\n\n```cpp\nint f" +
            std::to_string(i) + "() { return 42; }\n```\n";
        m.d.current.messages.push_back(std::move(msg));
    }
    app::detail::rehydrate_frozen(m);
    return m;
}

// 渲染 并返回所释放的字符串—— 字节 a 终端 必须 解析.
std::string frame(const Model& m, int w = 100, int h = 40) {
    maya::RenderContext ctx{w, h, maya::render_generation(), true};
    maya::RenderContextGuard g(ctx);
    auto el = ui::view(m);
    return maya::render_to_string(el, w);
}

// 有多少个 字节 CHANGED 介于两个之间 帧 ——一个流言蜚语的作家寄来的.
std::size_t changed_bytes(const std::string& a, const std::string& b) {
    std::size_t p = 0;
    const std::size_t lim = std::min(a.size(), b.size());
    while (p < lim && a[p] == b[p]) ++p;
    std::size_t q = 0;
    while (q < lim - p && a[a.size()-1-q] == b[b.size()-1-q]) ++q;
    return b.size() - p - q;
}

}  // 命名空间结束。

int main(int argc, char** argv) {
    const int msgs = argc > 1 ? std::atoi(argv[1]) : 400;

    app::install_deps(app::Deps{
        .stream        = [](const maya::unlocked&, provider::Request, provider::EventSink) {},
        .save_thread   = [](const Thread&) {},
        .delete_thread = [](const ThreadId&) {},
        .load_threads  = [] { return std::vector<Thread>{}; },
        .load_thread   = [](const ThreadId&) { return std::optional<Thread>{}; },
        .load_settings = [] { return store::Settings{}; },
        .save_settings = [](const store::Settings&) {},
        .new_thread_id = [] { return ThreadId{"t-wire"}; },
        .title_from    = [](std::string_view) { return std::string{"t"}; },
    });

    std::printf("transcript=%d msgs, viewport 100x40\n\n", msgs);
    std::printf("%-22s %12s %12s %10s\n",
                "action", "frame bytes", "changed", "ms");

    // ── 主题移动: 屏幕上的每一颜色更改 ──────────────────
    //
    // 电话 AVERAGE 并不是投诉 "Not snappy ALWAYS"
    // 是一个 末尾 问题:如果20个键中有一个成本是10x的,
    // 那么,平均值会留在预算范围内,而手仍然感到很不适。
    // 所以收集每个样本并打印分配。
    {
        Model m = seeded(msgs);
        m = app::update(std::move(m), Msg{OpenAppearance{}}).first;
        m = app::update(std::move(m), Msg{AppearancePickTheme{}}).first;
        std::string prev = frame(m);
        std::vector<double> samples;
        std::size_t worst_changed = 0, total_bytes = 0;
        for (int i = 0; i < 200; ++i) {
            auto t0 = clk::now();
            m = app::update(std::move(m), Msg{AppearanceThemeMove{+1}}).first;
            std::string cur = frame(m);
            samples.push_back(
                std::chrono::duration<double, std::milli>(clk::now() - t0).count());
            worst_changed = std::max(worst_changed, changed_bytes(prev, cur));
            total_bytes = cur.size();
            prev = std::move(cur);
        }
        std::sort(samples.begin(), samples.end());
        const auto pct = [&](double p) {
            return samples[static_cast<std::size_t>(p * (samples.size() - 1))];
        };
        std::printf("%-22s %12zu %12zu %10.1f\n",
                    "theme browser  \u2193", total_bytes, worst_changed, pct(1.0));
        std::printf("    p50=%.1f  p90=%.1f  p99=%.1f  MAX=%.1f ms"
                    "   (spread %.0fx)\n",
                    pct(0.50), pct(0.90), pct(0.99), pct(1.0),
                    pct(0.50) > 0 ? pct(1.0) / pct(0.50) : 0.0);
    }

    // ── 设置列表移动: 只有光标行更改 ─────────────────
    {
        Model m = seeded(msgs);
        m = app::update(std::move(m),
                        Msg{OpenSettingsList{settings::Category::General}}).first;
        std::string prev = frame(m);
        double worst_ms = 0;
        std::size_t worst_changed = 0, total_bytes = 0;
        for (int i = 0; i < 12; ++i) {
            auto t0 = clk::now();
            m = app::update(std::move(m), Msg{SettingsListMove{+1}}).first;
            std::string cur = frame(m);
            worst_ms = std::max(worst_ms,
                std::chrono::duration<double, std::milli>(clk::now() - t0).count());
            worst_changed = std::max(worst_changed, changed_bytes(prev, cur));
            total_bytes = cur.size();
            prev = std::move(cur);
        }
        std::printf("%-22s %12zu %12zu %10.1f\n",
                    "settings list  \u2193", total_bytes, worst_changed, worst_ms);
    }

    std::printf("\nA cursor move repaints a row; a theme move repaints every\n"
                "cell that carries colour. If `changed` is close to `frame\n"
                "bytes` for the theme row, the whole viewport is on the wire\n"
                "for each keypress \u2014 which is what a slow terminal feels.\n");
    return 0;
}
