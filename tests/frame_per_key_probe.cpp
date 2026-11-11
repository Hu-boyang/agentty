// 帧-per-key 忠诚: 您所按的每支箭都产生 帧 说
// SHOWS 箭头的结果?
//
// 报告症状:
// 连续向下移动时，一次按键突然不动，下一次却移动两行。
// 就是这样 不 丢弃的密钥 — 丢弃密钥以后永远不能移动两行。状态是正确的;
// 中间键 FRAME 从来没有被画过。
//
// 机制( maya app.hpp : 一个快餐 终端 发送数
// 个箭头 一个 read()。循环减少每个 事件 ,
// 但只设置 `needs_render = true` 和油漆 一次。
// 所以用户经过的行是计算出来的,并立即覆盖,然后才能到达 终端.
//
// 这个模型是循环的批量 并计算了用户实际看到的
// DISTINCT 行,vs按键。
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/msg.hpp"
#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/panel/appearance.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace agentty;
namespace pn = agentty::ui::panel;

int main(int argc, char** argv) {
    // 箭射到一个 read() 1 = 每根钥匙1 帧 (慢打字);
    // > 1 = 密钥 -repeat 运行中 帧 费率。
    const int batch  = argc > 1 ? std::atoi(argv[1]) : 3;
    const int total  = argc > 2 ? std::atoi(argv[2]) : 12;

    app::install_deps(app::Deps{
        .stream        = [](const maya::unlocked&, provider::Request, provider::EventSink) {},
        .save_thread   = [](const Thread&) {},
        .delete_thread = [](const ThreadId&) {},
        .load_threads  = [] { return std::vector<Thread>{}; },
        .load_thread   = [](const ThreadId&) { return std::optional<Thread>{}; },
        .load_settings = [] { return store::Settings{}; },
        .save_settings = [](const store::Settings&) {},
        .new_thread_id = [] { return ThreadId{"t-frames"}; },
        .title_from    = [](std::string_view) { return std::string{"t"}; },
    });

    Model m;
    for (int i = 0; i < 40; ++i) {
        Message msg;
        msg.role = (i % 2) ? Role::Assistant : Role::User;
        msg.text = "message " + std::to_string(i);
        m.d.current.messages.push_back(std::move(msg));
    }
    app::detail::rehydrate_frozen(m);
    m = app::update(std::move(m), Msg{OpenAppearance{}}).first;
    m = app::update(std::move(m), Msg{AppearancePickTheme{}}).first;

    const auto row = [&] {
        const auto* o = m.ui.panel.get<pn::Appearance>();
        return o ? o->pane.picker.picker.index() : -1;
    };

    std::printf("keys arriving %d-per-read, %d keys total\n\n", batch, total);
    std::printf("  read#   keys         row AFTER paint   rows the user SAW\n");

    // 固定后的运行循环，见 maya app.hpp：
    // 读数只消耗到并包括第一个读数 NAVIGATION 键;
    // 其余的被推后并交付下一个迭代, 之后是此 帧。所以,
    // 一整批箭头变成每支箭头。 帧 ,而粘贴(非) -navigation
    // 一次还是排水
    const bool nav_breaks_batch = std::getenv("OLD_BATCHING") == nullptr;

    std::vector<int> seen;          // 每PAINT有一个条目,而不是每键
    int pressed = 0;
    std::vector<int> deferred;      // 上次读取时所持箭头
    for (int r = 0; pressed < total || !deferred.empty(); ++r) {
        int n = 0;
        if (!deferred.empty()) {
            n = static_cast<int>(deferred.size());
            deferred.clear();
        } else {
            n = std::min(batch, total - pressed);
            pressed += n;
        }
        // 将事件从此读取,在第一个导航键后停止.
        int applied = 0;
        for (int k = 0; k < n; ++k) {
            m = app::update(std::move(m), Msg{AppearanceThemeMove{+1}}).first;
            ++applied;
            if (nav_breaks_batch && applied < n) {
                deferred.assign(static_cast<std::size_t>(n - applied), 1);
                break;
            }
        }
        // ...然后画画 一次 以所消耗的给养。
        seen.push_back(row());
        std::printf("  %-6d  %-11d  %-16d  %d\n", r + 1, applied, row(), row());
    }

    std::printf("\n  keys pressed         : %d\n", total);
    std::printf("  frames painted       : %zu\n", seen.size());
    std::printf("  rows the user SAW    : %zu of %d\n", seen.size(), total);

    if (seen.size() < static_cast<std::size_t>(total)) {
        std::printf("\n  → %zu row(s) were computed but NEVER SHOWN.\n",
                    total - seen.size());
        std::printf("    Felt as: a key that \"does nothing\", then the next "
                    "one \"moves twice\".\n");
        // 指定跳过行的名称, 这正是用户所看跳过的。
        std::printf("    Visible sequence: ");
        for (std::size_t i = 0; i < seen.size(); ++i)
            std::printf("%d%s", seen[i], i + 1 < seen.size() ? " → " : "\n");
        std::printf("    Expected        : ");
        for (int i = 1; i <= total; ++i)
            std::printf("%d%s", i, i < total ? " → " : "\n");
    } else {
        std::printf("\n  → every keypress got its own frame.\n");
    }
    return 0;
}
