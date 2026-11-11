#pragma once
// agentty::ui::FilteredPicker<Entry>-每个
// "snapshot + query + cursor" 覆盖面板的共享状态。
//
// ── 设计缺陷就结束了 ──────────────────────────────────────────
//
// 三个覆盖面板是同一个东西：`@`(文件)、`#`(符号)、
// `Ctrl+K`(命令)。每个独立的hand-wrote具有相同的三个机构，
// 并且hand-copies向三个不同的方向漂移：
//
//   1. 该 FILTER MEMO。 filter_*() 的复杂度为 O(N × query)，并且被调用为两个或
//      每次击键三次（状态更新函数夹紧光标，状态更新函数解析选择，视图绘制行）。
//      mention.hpp 和 symbol.hpp 各自生长出相同的
//      `cached_matches / cached_query / cached_valid`
//      三重奏以及相同的配件。调色板从来没有一个，所以每个键都 re-filters 两次。
//      同样的bug，解决了两次，错过了一次。
//
//   2. 该 SNAPSHOT REFILL。 选择面板 可能会打开 COLD（背景索引不
//      尚未发布）具有空的快照，并且必须在索引落地时重新填充。
//      该笔芯被准确写入 一个 消息臂 — "printable typed"
//      臂：
//
//          if (o->files.empty() && files_ready()) o->files = list_workspace_files();
//
//      因此，冷打开`@`并按下Backspace或↓而不是打字的用户永远
//      不会重新填充，并且面板永远停留在"indexing…"上。更糟糕的是：
//      一旦查询是非-empty，`empty()`防护装置就
//      永远不会再次发射，因此冷-opened 选择面板在其余生中
//      保留其在击键一上获取的任何部分列表。
//
//      缺陷在于，笔芯被附接到EVENT (〜6个臂之一），
//      而它实际上是READING和快照的属性。每条手臂都已穿过存取器；
//      将其放在那里使得 "refill on access"
//      通过构造总计，而不是在六个分支之一中为真。
//
//   3. 该 CURSOR CLAMP。每只手臂 re-derived `clamp(index, 0, size-1)` by
//      手对着计数它必须记住在过滤之后重新计算。
//
// FilteredPicker拥有全部三个。 覆盖面板声明了
// 它的条目类型和它的过滤函数；它继承了正确的 记忆缓存、
// 总重新填充以及不能指向过滤列表之外的光标。

#include <algorithm>
#include <cstddef>
#include <functional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "agentty/runtime/panel/step.hpp"
#include "agentty/runtime/visual.hpp"
#include "agentty/util/snapshot.hpp"

namespace agentty::ui {

// 选择面板如何获得(和re-obtains)其候选集。
//
// `ready`和`fetch`是分开的，因为选择面板必须区分
// "published but genuinely empty workspace"和
// "still indexing"——两个渲染不同的提示，并且单个可为空的快照不能说出哪一个。
template <class Entry>
struct SnapshotSource {
    std::function<bool()>                              ready;
    std::function<util::Snapshot<std::vector<Entry>>()> fetch;
};

// 过滤器：条目+查询→条目索引，最佳匹配优先。
template <class Entry>
using FilterFn =
    std::function<std::vector<std::size_t>(const std::vector<Entry>&, std::string_view)>;

template <class Entry>
class FilteredPicker {
public:
    FilteredPicker() = default;

    FilteredPicker(SnapshotSource<Entry> source, FilterFn<Entry> filter)
        : source_(std::move(source)), filter_(std::move(filter)) {}

    // ── 查询变异 ───────────────────────────────────────────────────
    // 这些中的每一个都使记忆缓存无效并将光标重置在ONE位置，
    // 因此没有臂可以改变查询并忘记两者之一。

    void type(char32_t ch) {
        // 今天只有 ASCII 打印件到达这里（关键路由器过滤器），
        // 但无论如何编码都是 UTF-8-safe。
        if (ch < 0x20) return;
        if (ch < 0x80) query_.push_back(static_cast<char>(ch));
        else           append_utf8(ch);
        on_query_changed();
    }

    void type(std::string_view text) {
        if (text.empty()) return;
        query_.append(text);
        on_query_changed();
    }

    // 当查询已经为空时返回假——`@`和Ctrl+K 选择面板用来表示
    // "backspace on an empty query closes me"的信号。
    // 返回它(而不是每个调用者在调用之前re-testing
    // `query().empty()`)保持测试和突变原子。
    bool backspace() {
        if (query_.empty()) return false;
        // 弹出整个UTF-8代码点，而不是字节：从multi
        // -byte序列中弹出一个字节会留下过滤器然后扫描的无效字符串。
        //
        while (!query_.empty()
               && (static_cast<unsigned char>(query_.back()) & 0xC0) == 0x80)
            query_.pop_back();
        if (!query_.empty()) query_.pop_back();
        on_query_changed();
        return true;
    }

    void clear_query() {
        if (query_.empty()) return;
        query_.clear();
        on_query_changed();
    }

    [[nodiscard]] const std::string& query() const noexcept { return query_; }

    // ── 光标 ───────────────────────────────────────────────────────────
    // 针对此处计算的 FILTERED 计数进行钳制，
    // 因此 覆盖面板 不能再针对 过期 或未过滤的大小移动光标。

    void move(int delta) const {
        index_ = step(index_, delta, static_cast<int>(filtered().size()));
    }

    // 对每个步长进行换行，用于主题浏览器的分页。
    void move_wrapping(int delta) const {
        const auto n = static_cast<int>(filtered().size());
        if (n <= 0) { index_ = 0; return; }
        index_ = ((index_ + delta) % n + n) % n;
    }

    void jump_to(int i) const {
        const auto n = static_cast<int>(filtered().size());
        index_ = n <= 0 ? 0 : std::clamp(i, 0, n - 1);
    }

    [[nodiscard]] int index() const noexcept {
        const auto n = static_cast<int>(filtered().size());
        // 夹住READ以及写入：当冷-opened 选择面板重新填充时，
        // 快照可以在存储的光标下方生长。
        if (n <= 0) return 0;
        return std::clamp(index_, 0, n - 1);
    }

    // 当前突出显示的条目，或者当列表为空时为nullptr。
    // 返回指针(不是引用+单独的empty()检查)
    // 使得不可能跳过空情况。
    [[nodiscard]] const Entry* selected() const {
        const auto& idx = filtered();
        if (idx.empty()) return nullptr;
        const auto i = static_cast<std::size_t>(index());
        if (i >= idx.size()) return nullptr;
        return &entries()[idx[i]];
    }

    // ── Viewport ─────────────────────────────────────────────────────
    //
    // 光标及包含它的窗口必须表达为同一个值。
    //
    // 存在这种情况是因为它们曾经是两个，并且它们之间的间隙
    // 是主题浏览器中断的地方：它移动了`index`，
    // 而单独的`scroll`位于其旁边未读，因此突出显示从
    // 615-row列表的底部走开，并且用户只能转向他们
    // 看不到的选择。按下按钮似乎没有任何作用；然后，
    // Enter 从最后一个可见行的下方提交了一个方案。
    //
    // 窗口是DERIVED，从未被存储。
    // 没有偏移字段会忘记前进，没有第二个写入器到 竞态，
    // 也没有办法到 渲染 一个排除光标的切片——放置
    // 窗口的算术与保证它包含 index() 的算术相同。
    // 呼叫者不能保留该错误，因为没有状态可以保留。
    struct Window {
        std::size_t first  = 0;  // 按过滤顺序的第一个可见位置
        std::size_t count  = 0;  // 有多少是可见的
        std::size_t total  = 0;  // filtered().size()，用于 "n of m" + 滚动条
        int         cursor = -1; // 切片中光标的偏移 WITHIN，如果为空则为 -1

        [[nodiscard]] bool empty()   const noexcept { return count == 0; }
        [[nodiscard]] bool scrolled_above() const noexcept { return first > 0; }
        [[nodiscard]] bool scrolled_below() const noexcept {
            return first + count < total;
        }
    };

    // 切片到渲染的高度为可视区域 `rows`，
    // 以文本编辑器滚动的方式以光标为中心：光标静止在中间，
    // 直到到达列表的末尾，在此处窗口停止并且光标
    // 本身行进了最后一半-screen。夹紧，
    // 使最后一页填满，而不是在末尾处留下空白行。
    [[nodiscard]] Window visible(int rows) const {
        Window w;
        w.total = filtered().size();
        if (w.total == 0 || rows <= 0) return w;

        const auto vh  = static_cast<std::size_t>(rows);
        const auto cur = static_cast<std::size_t>(index());

        w.count = std::min(vh, w.total);
        if (w.total > vh) {
            // 保持光标居中，然后夹住两端。写为有符号算术，
            // 因为 cur - vh/2 在顶部附近下溢。
            const auto half  = static_cast<long long>(vh) / 2;
            long long  first = static_cast<long long>(cur) - half;
            const long long max_first =
                static_cast<long long>(w.total) - static_cast<long long>(vh);
            first = std::clamp(first, 0LL, max_first);
            w.first = static_cast<std::size_t>(first);
        }
        w.cursor = static_cast<int>(cur - w.first);
        return w;
    }

    // 该窗口中的条目按过滤顺序排列 — 多么好的视图 渲染。
    // 返回投影的条目指针（不是索引）可以防止调用者重新
    // -deriving映射并在第二站点上得到错误的结果。
    [[nodiscard]] std::vector<const Entry*> visible_entries(int rows) const {
        const Window w = visible(rows);
        const auto& idx = filtered();
        const auto& src = entries();
        std::vector<const Entry*> out;
        out.reserve(w.count);
        for (std::size_t i = w.first; i < w.first + w.count && i < idx.size(); ++i)
            out.push_back(&src[idx[i]]);
        return out;
    }

    // ── 阅读候选集 ────────────────────────────────────────

    // THE存取器。每次读取都会经过这里，这正是重新填充
    // 在这里的原因：在发布后台索引之前打开的 选择面板
    // 在下一次读取时会自行加满，无论导致读取的原因是什么
    // — 击键、箭头、调整大小、重绘。旧代码将其附加到一个消息臂，
    // 因此错过了五个。
    [[nodiscard]] const std::vector<Entry>& entries() const {
        if (!snapshot_.has_value() && source_.ready && source_.fetch && source_.ready()) {
            snapshot_ = source_.fetch();
            memo_valid_ = false;   // 较大的快照意味着新的匹配索引
        }
        return snapshot_.get();
    }

    // 记忆的过滤结果：索引到entries()，最好的第一个。
    // 仅当查询或 快照 实际更改时才重新计算，
    // 因此两次或三次读取单个击键执行的成本为一次，而不是三次。
    [[nodiscard]] const std::vector<std::size_t>& filtered() const {
        const auto& src = entries();   // 可以重新填充，可能会使记忆缓存失效
        if (!memo_valid_ || memo_query_ != query_) {
            memo_ = filter_ ? filter_(src, query_) : identity_indices(src.size());
            memo_query_ = query_;
            memo_valid_ = true;
        }
        return memo_;
    }

    // 一旦支持索引已发布，则为真。与 `filtered().empty()`
    // 不同：选择面板 的页脚显示前者为 "indexing…"，
    // 后者为 "workspace empty"，将它们混为一谈就是 Cold
    // -open 路径变得不可见的原因。
    [[nodiscard]] bool source_ready() const {
        return snapshot_.has_value() || (source_.ready && source_.ready());
    }

    // 对于显式的 "the index changed" 信号（工作区 开关、
    // -tool-edit 后刷新），在下一次读取时强制重新填充。
    void invalidate() const {
        snapshot_   = {};
        memo_valid_ = false;
    }

private:
    void on_query_changed() const {
        memo_valid_ = false;
        index_      = 0;
    }

    void append_utf8(char32_t cp) {
        if (cp < 0x800) {
            query_.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            query_.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            query_.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            query_.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            query_.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            query_.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            query_.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            query_.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            query_.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    static std::vector<std::size_t> identity_indices(std::size_t n) {
        std::vector<std::size_t> v(n);
        for (std::size_t i = 0; i < n; ++i) v[i] = i;
        return v;
    }

    SnapshotSource<Entry> source_;
    FilterFn<Entry>       filter_;
    std::string           query_;

    // `mutable`出于同样的原因旧手-rolled备忘录是：
    // 视图采用`const Model&`并且必须能够读取（并因此填充）缓存。
    // 逻辑常量 — 这里的每个 可变 成员都是 缓存 或已经由
    // query_ + snapshot_ 确定的状态钳位。
    mutable util::Snapshot<std::vector<Entry>> snapshot_;
    mutable std::vector<std::size_t>           memo_;
    mutable std::string                        memo_query_;
    mutable bool                               memo_valid_ = false;
    mutable int                                index_      = 0;

    // ── 电话 帧-hash 合同 ──────────────────────────────────────────
    // visual.hpp 的步行大括号-decomposes 类型对
    // 它们进行散列，但这里不能这样做（私有成员、
    // user-provided 构造函数）。步行失败 CLOSED 并需要
    // 一个明确的零件列表，而不是让该失败打开，因此故意在此处命名两个可见轴：
    //
    //   query_屏幕上的文本。
    //   index_突出显示的行。
    //
    // 其他一切都是这两个的PROJECTION加上背景快照:
    // memo_/memo_query_/memo_valid_是过滤器(快照，
    // 查询)的纯缓存，并且对缓存进行散列将使门取决于重绘
    // 是否碰巧填充它。 `snapshot_` 因一个更
    // 微妙的原因而被豁免——它是由后台任务重新发布的，
    // 因此对其进行哈希处理会让更新循环之外的 线程 移动 帧 门。
    // 选择面板 的冷-open 填充是通过在读取时重新计算
    // `index()`/`filtered()` 来显示的，
    // 并且在索引变暖时已经运行的刻度线将其带到屏幕上。
    friend auto visual_parts(const FilteredPicker& p) {
        return std::make_tuple(std::cref(p.query_), p.index_);
    }
};

} // namespace agentty::ui

namespace agentty::visual {
// 所审查的权利要求位于拥有该概念的命名空间中。
//
// FilteredPicker是非-aggregate(私有成员+user
// -provided构造函数)，因此brace-arity探测报告0并且完整性证明无法计算其成员。
// 这在设计上失败了 CLOSED，这就是这个 opt-in 存在的原因：它是一个
// 经过深思熟虑的、可 grep 的声明，由人类检查了零件清单。检查：在 选择面板
// 的八个成员中，正好有两个是可见状态（query_、index_），六个是 缓存
// 或管道（source_、filter_、snapshot_、memo_、
// memo_query_、memo_valid_）。请参阅 filtered_picker.hpp
// 中的朋友 visual_parts 了解为什么每个都被排除。
template <class Entry>
inline constexpr bool trusted_parts<ui::FilteredPicker<Entry>> = true;
} // namespace agentty::visual

namespace agentty::ui {
static_assert(visual::parts_cover_all<FilteredPicker<std::string>>);
} // namespace agentty::ui
