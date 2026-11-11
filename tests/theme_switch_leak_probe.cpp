// 主题 -switch 探测器,驱动 真实 外观 状态更新函数
// 浏览器(通过计划)在现实记录和每页报告上的做法(XQ/XQ)
// -switch 时间加过程 RSS 每个N开关
//
// "Gets laggy over time"是无约束增长的标志,而不是慢码:
// 如果开关# 500 的成本是 1 的, 就不会有漏水,
// 成本只是每 -switch 工作。 如果它爬升, 东西正在积累。
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/msg.hpp"
#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/panel/appearance.hpp"
#include "agentty/domain/ui_prefs.hpp"

#include "agentty/runtime/app/update/internal.hpp"   // rehydrate_frozen

#include <maya/style/schemes.hpp>
#include <maya/core/render_context.hpp>
#include <maya/print.hpp>

#include "agentty/runtime/view/view.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace agentty;

static long rss_kb() {
    std::FILE* f = std::fopen("/proc/self/status", "r");
    if (!f) return -1;
    char line[256];
    long kb = -1;
    while (std::fgets(line, sizeof line, f))
        if (std::sscanf(line, "VmRSS: %ld kB", &kb) == 1) break;
    std::fclose(f);
    return kb;
}

int main(int argc, char** argv) {
    const int switches = argc > 1 ? std::atoi(argv[1]) : 600;
    const int msgs     = argc > 2 ? std::atoi(argv[2]) : 60;

    app::install_deps(app::Deps{
        .stream        = [](const maya::unlocked&, provider::Request, provider::EventSink) {},
        .save_thread   = [](const Thread&) {},
        .delete_thread = [](const ThreadId&) {},
        .load_threads  = [] { return std::vector<Thread>{}; },
        .load_thread   = [](const ThreadId&) { return std::optional<Thread>{}; },
        .load_settings = [] { return store::Settings{}; },
        .save_settings = [](const store::Settings&) {},
        .new_thread_id = [] { return ThreadId{"t-theme"}; },
        .title_from    = [](std::string_view) { return std::string{"t"}; },
    });

    // 一份足够流传的文字记录 证明Re-colouring是真正的工作
    Model m;
    for (int i = 0; i < msgs; ++i) {
        Message msg;
        msg.role = (i % 2) ? Role::Assistant : Role::User;
        msg.text = "## Heading " + std::to_string(i) +
            "\n\nSome prose with `code` and *emphasis*, long enough to wrap "
            "across a couple of lines in a normal terminal width.\n\n"
            "```cpp\nint f" + std::to_string(i) + "() { return 42; }\n```\n";
        m.d.current.messages.push_back(std::move(msg));
    }

    // 每个方案名称, 所以我们循环 像浏览器做的。
    std::vector<std::string> names;
    for (const auto& s : maya::theme::schemes) names.emplace_back(s.name);
    std::printf("themes=%zu  transcript=%d msgs  switches=%d  frozen_through=%zu\n\n",
                names.size(), msgs, switches, m.ui.frozen_through);

    // 打开 面板 + 浏览器, 和用户一样, 所以探测器走
    // 相同 状态更新函数 路径作为主题列表中的 QQ/QQ 而不是
    // 可能跳过昂贵部分的快捷键。
    //
    // frozen_through 应用软件的建立方式:
    // rehydrate_frozen () ,列预算项下。
    //
    // 手 -seeding 指数比没用的还要差...
    // rehydrate_frozen() 调用 clear_frozen
    // ()首先,所以一个种子值被擦去 在第一个开关上,探测器默默地测量一个记录片,
    // 那就是 ENTIRELY 现场直播 末尾 状态在真实会话中不存在,
    // 并且是按 -switch 成本在对话长度(236ms at 1000
    // messages)中看起来线性,当真实数字平整在~6ms时,
    // 因为只有~2屏幕是永远存在的.
    app::detail::rehydrate_frozen(m);

    m = app::update(std::move(m), Msg{OpenAppearance{}}).first;
    m = app::update(std::move(m), Msg{AppearancePickTheme{}}).first;

    const long rss0 = rss_kb();
    std::printf("%-8s %-12s %-12s %-12s %-10s\n",
                "switch", "us/switch", "us/render", "RSS kB", "\u0394RSS kB");

    using clk = std::chrono::steady_clock;
    const int bucket = switches / 12 > 0 ? switches / 12 : 1;
    double bucket_us = 0, bucket_render_us = 0;
    std::printf("(us/switch = reducer only; us/render = view()+render, what the "
                "user actually waits on)\n");
    for (int i = 0; i < switches; ++i) {
        auto t0 = clk::now();
        // 下移浏览器列表 = 1 直播预览 = one re-seal.
        m = app::update(std::move(m), Msg{AppearanceThemeMove{+1}}).first;
        bucket_us += std::chrono::duration<double, std::micro>(clk::now() - t0).count();

        // ...然后是,我从小就... 帧。该词 状态更新函数
        // 只标出什么 过期 ; 缩写 COST 主题变化的重构它掉下的元素,
        // 发生在这里。 状态更新函数 将 调用 转 -parse
        // 整个记录稿 "free".
        auto t1 = clk::now();
        {
            maya::RenderContext ctx{100, 40, maya::render_generation(), true};
            maya::RenderContextGuard g(ctx);
            auto el = ui::view(m);
            (void)maya::render_to_string(el, 100);
        }
        bucket_render_us += std::chrono::duration<double, std::micro>(clk::now() - t1).count();

        if ((i + 1) % bucket == 0) {
            const long r = rss_kb();
            std::printf("%-8d %-12.0f %-12.0f %-12ld %+-10ld\n",
                        i + 1, bucket_us / bucket, bucket_render_us / bucket,
                        r, r - rss0);
            bucket_us = 0;
            bucket_render_us = 0;
        }
    }
    return 0;
}
