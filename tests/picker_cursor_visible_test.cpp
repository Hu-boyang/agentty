// picker_cursor_visible_test - 电话 不变量 每个列表 覆盖面板 欠用户的 :
//
//     该 HIGHLIGHTED ROW IS ON SCREEN.
//
// 试探一下,这个会吸引到主题 -browser 错误,
// 它写在反面 PRIMITIVE 而不是反对一个面板,
// 所以它为每一个 选择面板 使用时 FilteredPicker 而不是只打破
// 了那个
//
// 它所夹的bug:主题浏览器所保存的bug `index` 财务报告和
// 已审计财务报表 `scroll` 成为两个松散的田地。 状态更新函数
// 高级索引; 没有任何东西可以推进滚动; 视图通过一个 THIRD
// 属于该附件的滚动对象 面板 后面,光标就从615底部走过去 -row
// 列表中,用户被留下来引导他们无法看到的选择——下方似乎已死亡,
// Enter 执行一个远在最后一行可见线以下的图案。
//
// FilteredPicker::visible () 关闭
// 该词 DERIVING 将光标中的窗口而不是存储。没有抵消可以
// 忘记推进,所以下面的地产今天并不仅仅是真实的,而是建筑的真。

#include <cstdio>
#include <string>
#include <vector>

#include "agentty/runtime/panel/filtered_picker.hpp"
#include "agentty/util/snapshot.hpp"

using namespace agentty;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

// A 选择面板 合成行的N 故意比任何一列大得多
// 可视区域 因为那是虫子所生活的政权
ui::FilteredPicker<std::string> make_picker(int n) {
    std::vector<std::string> rows;
    rows.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) rows.push_back("row-" + std::to_string(i));

    auto snap = util::make_snapshot(std::move(rows));
    ui::SnapshotSource<std::string> src{
        .ready = [] { return true; },
        .fetch = [snap] { return snap; },
    };
    ui::FilterFn<std::string> filter =
        [](const std::vector<std::string>& entries, std::string_view q) {
            std::vector<std::size_t> out;
            for (std::size_t i = 0; i < entries.size(); ++i)
                if (q.empty() || entries[i].find(q) != std::string_view::npos)
                    out.push_back(i);
            return out;
        };
    return ui::FilteredPicker<std::string>{std::move(src), std::move(filter)};
}

// 该 不变量 ,作为上游:无论光标是什么,我们将会看到的
// 窗口 渲染 包含它,而该偏移处的条目是选定的条目。
bool cursor_is_visible(const ui::FilteredPicker<std::string>& p, int vh) {
    const auto w   = p.visible(vh);
    const auto ent = p.visible_entries(vh);
    if (w.total == 0) return w.cursor == -1 && ent.empty();

    if (w.cursor < 0) return false;                        // 必须命名行
    if (static_cast<std::size_t>(w.cursor) >= ent.size()) return false;
    if (ent.size() != w.count) return false;

    // 窗口必须指向的行 BE 选择。这是抓取的部份
    // -by-one 窗口:切片可以包含光标的索引,
    // 而偏移算术仍然会给错误的行命名。
    const std::string* sel = p.selected();
    return sel != nullptr && *ent[static_cast<std::size_t>(w.cursor)] == *sel;
}

} // 命名空间结束。

int main() {
    std::printf("=== picker_cursor_visible_test ===\n\n");

    // ── 1. 逐一走完整个名单 ────────────────────
    // 错误报告给出的准确手势: 抓住并监视。
    // 每个位置都必须被渲染, 而不仅仅是第一个屏幕。
    {
        auto p = make_picker(615);          // 实际计数方案
        const int vh = 8;
        int offscreen = 0;
        for (int i = 0; i < 615; ++i) {
            if (!cursor_is_visible(p, vh)) ++offscreen;
            p.move_wrapping(1);
        }
        std::printf("615 rows, viewport 8 — every cursor position\n");
        std::printf("    positions where the highlight was off screen: %d / 615\n",
                    offscreen);
        check(offscreen == 0, "the highlight is never off screen");
        std::printf("\n");
    }

    // ── 2. 包装 ─────────────────────────────────────────────────────
    // 环绕是存储的偏移最明显错误的地方:
    // 光标跳到最远的一端,而只保留增量的
    // 卷轴则指向列表的另一端。
    {
        auto p = make_picker(615);
        const int vh = 8;
        p.move_wrapping(-1);                 // 从第0行起 ~ 最后一行
        const auto w = p.visible(vh);
        std::printf("wrap past the top\n");
        std::printf("    cursor landed in window [%zu, %zu) of %zu\n",
                    w.first, w.first + w.count, w.total);
        check(cursor_is_visible(p, vh), "wrapping keeps the highlight visible");
        check(w.scrolled_above(), "the window followed the cursor to the end");
        std::printf("\n");
    }

    // ── 3. 过滤 ────────────────────────────────────────────────────
    // 缩小会改变N行的含义。 光标会留在
    // 一个已不存在的列表中。
    {
        auto p = make_picker(615);
        const int vh = 8;
        p.move_wrapping(400);                // 深入名单
        p.type("row-61");                    // 倒塌成一团
        std::printf("filtering after scrolling deep\n");
        std::printf("    filtered to %zu rows, cursor=%d\n",
                    p.filtered().size(), p.index());
        check(cursor_is_visible(p, vh), "a narrowed list re-homes the highlight");

        p.clear_query();                     // 重新拓宽
        check(cursor_is_visible(p, vh), "widening keeps it visible too");
        std::printf("\n");
    }

    // ── 4. 可视区域 大小,包括退化的大小 ────────────────────
    // 面板的 可视区域 从“E”中计算 终端 ;
    // 一个小窗口和一个大于列表的窗口通过重新大小可以达到.
    {
        std::printf("assorted viewport heights\n");
        int bad = 0;
        for (int vh : {1, 2, 3, 5, 8, 13, 40, 1000}) {
            auto p = make_picker(615);
            for (int step = 0; step < 50; ++step) {
                p.move_wrapping(13);         // 步长队 -ish 带 vh
                if (!cursor_is_visible(p, vh)) { ++bad; break; }
            }
        }
        check(bad == 0, "holds at every viewport height");
        std::printf("\n");
    }

    // ── 5. 窗户从不掉下 ────────────────────────────
    // 滑过最后一行的窗口 渲染 列表下方的空白条, 上面有更多行—
    // — ưμ㼯A "half empty picker" 症状
    {
        auto p = make_picker(100);
        const int vh = 8;
        int overrun = 0;
        for (int i = 0; i < 100; ++i) {
            const auto w = p.visible(vh);
            if (w.first + w.count > w.total) ++overrun;
            p.move_wrapping(1);
        }
        check(overrun == 0, "the window never extends past the last row");
        std::printf("\n");
    }

    // ── 6. 简表 ──────────────────────────────────────────────────
    // 与可视区域相比,行数更少,绝对不能滚动,
    // 空列表不能报告光标,而不能报告行数0.
    {
        auto p = make_picker(3);
        const auto w = p.visible(8);
        check(w.first == 0 && w.count == 3, "a short list fills from the top");
        check(!w.scrolled_above() && !w.scrolled_below(),
              "a short list reports no scroll affordances");
        check(cursor_is_visible(p, 8), "short lists satisfy the invariant too");

        auto e = make_picker(0);
        const auto ew = e.visible(8);
        check(ew.cursor == -1 && ew.empty(),
              "an empty list names no row (not row 0)");
        std::printf("\n");
    }

    if (failures == 0) {
        std::printf("PASS — the highlighted row is always on screen\n");
        return 0;
    }
    std::printf("FAILED: %d check(s)\n", failures);
    return 1;
}
