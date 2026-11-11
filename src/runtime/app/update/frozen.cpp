// frozen.cpp-附加-only密封的回滚缓冲区前缀(maya ScrollbackLedger)。
//
// 反映agent_session示例的`m.frozen`规则：已确定的转弯在
// 它们确定时被构建到元素值ONCE中，密封到m.ui.frozen (
// maya::ScrollbackLedger)中，并且由maya通过ledger_ref呈现。
// 视图 (conversation_config) 将借用的指针交给 maya
// 到账本，因此无论会话运行多长时间，每个 -frame 的成本都是
// O(visible_live)，而不是 O(visible_total_turns
// × tool_cards_per_turn)。
//
// 该 ACCOUNTING INVERSION。该文件用于维护
// 并行测量管道：通过主机 -reconstructed 宽度（
// term_cols - 4，通过 ensure_frozen_width
// 调整大小进行修复）测量的每 -entry 行计数，
// 在四个并行向量中保持同步，并汇总到每个修剪发送的 maya 的
// 精确 commit_scrollback 计数中。每个历史装饰
// -corruption错误都在该管道和maya实际绘制的内容之间漂移。
// 所有这些都是DELETED：maya的油漆通道将每个密封块的真实
// 铺设-out高度记录到每个帧的分类账中（与传输相同的布局通道，
// 相同的宽度，相同的帧），并且修剪的提交计数由ledger.harvest()
// 铸造为带类型的 ScrollbackDebt令牌，
// 该文件无法制造或调整。 ESTIMATES 行保留在仅 POLICY
// （何时修剪，保留多少）下方的饲料中 - 他们从不涉及会计。
//
// 生产者是`freeze_through(m, live_start)`：
// 遍历消息[frozen_through .. live_start)，
// 应用conversation_config在查看时使用的相同
// 工具-batch合并，并且密封每个视觉单元的一个转向元素（
// 前面有间隙）。压实分隔线插入其边界索引处。
//
// 生命周期不变量:
//   ·frozen.size()对应于消息[0..frozen_through)。
//   ·一旦被推动，密封块就是不可变（元素值是
//     构建后读取-only)。
//   ·对于 i < frozen_through 改变消息 [i] 的任何操作都是
//     禁止；如果有必要进行这样的突变（检查点恢复、追溯编辑），
//     则 调用 rehydrate_frozen() 从头开始
//     ​​重建。

#include "agentty/runtime/app/update/internal.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <maya/core/executor.hpp>
#include <maya/dsl.hpp>
#include <maya/element/text.hpp>   // maya::string_width（显示-column计数）
#include <maya/platform/io.hpp>
#include <maya/render/cache_id.hpp>
#include <maya/render/scrollback_ledger.hpp>
#include <maya/render/scrollback_replay.hpp>
#include <maya/widget/conversation.hpp>
#include <maya/widget/turn.hpp>

#include "agentty/runtime/view/palette.hpp"
#include "agentty/domain/ui_prefs.hpp"
#include "agentty/runtime/view/thread/seam.hpp"
#include "agentty/runtime/view/thread/turn/turn.hpp"
#include "agentty/runtime/view/thread/turn/agent_timeline/tool_body_preview.hpp"

namespace agentty::app::detail {

namespace {

// 实时 终端 几何形状的单一事实来源：Model，主机的大小调整 事件
// 订阅 使其保持最新 (TerminalResized)。两个轴都来自一份报告，
// 因此 归并 内的宽度和高度永远不会不一致。
// 行估计 (estimate_msg_rows) 将 字节 计数除以 `cols`，
// 并且修剪大小将其 keep-margins 关闭 `rows` — 两者都
// 必须反映 渲染器 放置画布的 相同 终端 状态，
// 或 "provably above the viewport" 修剪证明
// 漂移（在 -counted 行下 → 删除 on-screen 条目 →
// re-emit 已提交回滚缓冲区 移动 = 重复幽灵）。 状态更新函数 读取
// Model 而不是 tty 也是使重放看到原始运行看到的大小的原因。
struct TermDims { int cols; int rows; };
TermDims term_dims(const Model& m) {
    return TermDims{m.ui.term_cols > 0 ? m.ui.term_cols : 80,
                    m.ui.term_rows > 0 ? m.ui.term_rows : 40};
}

// 行估计使用的有效换行宽度，从显式的终端-column计数导出。
// 固定在合理的范围内，因此短暂的 -bogus 1-col
// 读取不会将估计值增加到数千行（这将超过 -trim）。
// 渲染器 为转弯导轨/排水沟保留了几根柱子，因此要减去一小部分余量。
// 已经持有 term_dims() 快照 的调用者会传递其 .
// cols，因此一次修剪中的每一行-math 调用 都会
// 反映 一个 终端 状态（证明取决于它）。
int estimate_wrap_cols(int term_cols) {
    // Rail+padding吃掉~4列的实际内容宽度。
    return std::max(16, term_cols - 4);
}
int estimate_wrap_cols(const Model& m) { return estimate_wrap_cols(term_dims(m).cols); }

// measure_cols是GONE。它重建了冻结块的 EXACT
// 内容宽度（term_cols - 4：AppLayout 的
// padding(1) 吃 2，Conversation 的
// padding(0,1) 吃 2），因此 seal-time 度量与
// 真实的 渲染 匹配。该重建是最后一个主机-side测量化石——这里的
// -2-vs-4区别本身就是对narrow-terminal重复幽灵的修复，
// 任何未来的chrome-padding变化都会默默地重新引入它。
// ledger.seal_measured()现在将新块布置在为片段（
// record_paint_width)记录的宽度maya的OWN绘制通道处，
// 因此没有什么可以重构的。上面的 estimate_wrap_cols
// 仅适用于粗略文本 -byte POLICY 估计 (
// estimate_msg_rows)，其中 ±2 列是噪声。

}  // 命名空间结束。

// Live-canvas行预算=终端 可视区域的小倍数，从明确的终端
// -row计数导出（参见estimate_wrap_cols了解one
// -snapshot的基本原理）。
//
// 匿名命名空间中的NOT：在internal.hpp中声明，
// 因此rehydrate_scrollback_test可以将live-vs-seed关系固定在固定的行数处。
// 与下面的 estimate_msg_rows 的推理相同。
// 实时m.ui.frozen矢量IS内联画布；每个完整的重绘（恢复交换，
// 调整大小→HardReset re-emit，Ctrl-L）都会从上到下走动，
// 用户会看到油漆。较旧的行存在于磁盘上（通过 选择面板 调用）。
// 在 width-resize maya 上发出 \x1b[3J，
// 它会擦除​​本机 回滚缓冲区，因此在调整大小时超出此预算的任何
// 内容都会消失 - 调整大小以保持有用的最近窗口可恢复。
std::size_t frozen_row_budget(int term_rows) {
    // 〜3个视口，地板，所以一个小窗口仍然保留有用的上下文。
    // Trade-off：更大的窗口=调整大小时恢复的最近的回合和
    // 更大的one-shot HardReset油漆。每-frame
    // 成本不变（maya hash_id 缓存 命中已结算条目）。 3x 选择，
    // 因此 80x24 终端 在宽度调整大小后恢复约 72 行
    // 最近的 回滚缓冲区，同时仍然限制画布。
    return static_cast<std::size_t>(std::max(48, term_rows * 3));
}
std::size_t frozen_row_budget(const Model& m) { return frozen_row_budget(term_dims(m).rows); }

// RESUME绘制了多少转录本，而不是保留了多少转录本。
//
// 这些只是一个数字，将它们合并起来会让用户失去他们的历史记录。
// 上面的评论是正确的，LIVE画布必须保留~3个视口：
// 每次调整大小时都是re-walked和Ctrl-L，并且用户观看该绘制。但它也声称旧的行是安全的，
// 因为它们是 "live on disk (recall via picker)"，
// 而在 trim_frozen_if_oversized 中，因为它们是
// 实时内容溢出时已提交到终端原生回滚缓冲区。
//
// 两者都不适用于线程 SWITCH。没有任何内容溢出——线程只是从
// 磁盘上读取——并且reset_inline()首先发出\x1b[3J，
// 这会擦除那里的任何本机回滚缓冲区。在 3326-message 线
// 程 上测量：交换机发出 1 个 3J，然后发出 174 个换行符。
// 82-row 窗口之前的所有内容均未渲染 和 不可滚动。
// 向上滚动没有显示任何内容。
//
// 因此，重新水合这个WIDER窗口的种子，并且现有的修剪立即将其带回到
// frozen_row_budget()。这不是浪费工作：修剪的
// drop_front 将每个掉落块的 Paint-recorded
// 高度累积为 ScrollbackDebt，而 harvest()
// 铸造 commit_scrollback 令牌 - 与将实时溢出放入本机
// 回滚缓冲区 的机制相同。这些行实际上从顶部滚动到 终端 的 已保存-lines，
// 这正是它们可滚动的原因。
//
// Net 副作用：实时画布未更改（因此调整大小和 Ctrl-L 的
// 成本与今天的成本相同），并且用户在恢复的 线程 上获得约 10 个真实的、
// 可滚动历史的视口，而不是什么都没有。
//
// 10x而不是更大：种子是保留窗口的一个-shot同步解析（每
// 个助理消息settle_message_md），并且这是用户
// 等待的唯一部分。在 82 行处测量，开关的整个 UI
// -thread 成本为 1.5 毫秒；这会用几毫秒来换取历史记录。
// 进一步提高它是一个实际成本，但回报递减——超过约 10
// 个屏幕就没有人滚动。
std::size_t rehydrate_row_budget(int term_rows) {
    const int rows = term_rows > 0 ? term_rows : 24;
    return static_cast<std::size_t>(std::max(1200, rows * 20));
}
std::size_t rehydrate_row_budget(const Model& m) { return rehydrate_row_budget(term_dims(m).rows); }

namespace {

// Inter-turn间隙+压实分隔器现在来自SHARED 接口边界 头文件
// (agentty/runtime/view/thread/seam.hpp)
// ——该文件AND conversation.cpp的实时末尾的一个定义站点，
// 因此冻结接口边界不能在两个构建器之间漂移字节-wise。
using ui::gap_row;
using ui::compaction_divider_row;
using ui::gap_rows;

// 逃生舱口用于再水化-time关闭-screen 正文下方塌陷。
//
// OFF BY DEFAULT。崩溃仅从 rehydrate_frozen 运行，
// 其 仅 调用者是 ThreadLoaded — 从磁盘加载 线程。在该路径中，
// 从磁盘读取巨大的 正文，并将 绝不 绘制到 此 终端 的本机 回滚缓冲区，
// 因此用 "⋯ N rows collapsed — scroll up in your terminal to view"
// 替换它会隐藏根本不存在的内容：向上滚动显示之前的 线程 /
// 选择面板，而不是折叠的行。这就是 "wtf is this"
// 回归——一条 已加载 消息默默地被存根吞没。 perf win
// （限制一个 just-loaded 超大 正文 的一个
// -off 冷 重绘）不值得丢失一条 已加载 消息，
// 因此默认值现在是完全保真度。
//
// 仅当工作流程在 相同 终端 中重新启动 agentty 时，才选择 IN 且
// AGENTTY_FROZEN_COLLAPSE=1 (或 t/y)，其中 正文 IS
// 仍在本机 回滚缓冲区 中并且存根的指针是真实的。由 init() 读入
// m.env.frozen_collapse 一次。

// OFF-SCREEN 正文的紧凑占位符从in-app re-render窗口中折叠出来
// （参见collapse_oversized_offscreen_entries)。
// 一排昏暗的 ⋯ 行，与 compaction_divider_row 相同的镀铬，
// 因此 seal-time 测量报告 ~1 行。完整的 正文 在 终端 的
// 原生 回滚缓冲区（实时绘制）和磁盘上均未受影响。
maya::Element collapsed_body_stub(std::size_t orig_rows) {
    maya::Turn::Config cfg;
    cfg.glyph      = "\xe2\x8b\xaf";   // ⋯
    cfg.label      = std::to_string(orig_rows)
                   + " rows collapsed \xe2\x80\x94 scroll up in your terminal to view";
    cfg.rail_color = ui::muted;
    return maya::Turn{std::move(cfg)}.build();
}

// Sentinel-check:`mm`是唯一内容为tool_calls(
// 无散文)的助手消息吗？作为分类文件保留；实际的 run
// -merge 策略现在位于 `ui::turn_run_end` 中，
// 从 `freeze_range` 和 `build_live_tail`
// 调用。
[[maybe_unused]] bool is_tool_only_assistant(const Message& mm) {
    return mm.role == Role::Assistant
        && mm.text.empty()
        && mm.streaming_text.empty()
        && !mm.tool_calls.empty();
}

// 单个消息对冻结回合的贡献的廉价显示-column行估计。 不 a 渲染 —
// 仅用于 BOUND 冻结画布高度的粗略代理。设计为 one-sided（
// 确切地说或 UNDER，绝不是 OVER），因此两种装饰的 keep-loops
// 在构造上都是安全的。由 rehydrate_frozen（预算步行）和
// freeze_range（根据 -entry frozen_rows
// 会计）共享。

// 显示单个物理行（无嵌入换行符）占用的列，
// 以及它以宽度 `cols` 产生的换行行计数。
// 使用maya::string_width——渲染器所包裹的SAME
// UCD-backed宽度表——因此估计跟踪真实列，而不是字节。
// 每个字形 3 字节 的 CJK / 表情符号行不再膨胀至其
// 实际行数的约 3 倍；强制装饰不对称安全裕度的 字节
// -based 超过 -count 已经消失，根据装饰证明的要求，
// 估计值是一个 -sided（精确或低于）。
std::size_t line_display_rows(std::string_view line, std::size_t cols) {
    if (cols < 1) cols = 1;
    const int w = maya::string_width(line);
    if (w <= 0) return 1;                 // 空白/zero-width行=1行
    return (static_cast<std::size_t>(w) + cols - 1) / cols;   // 天花板
}

// 给定内容宽度处文本正文的Wrapped-row计数：每个硬
// 换行符开始一个新行，并且比`cols`宽的行显示软
// -wraps列到ceil(width/cols)行。
// 这反映了 渲染器 的实际作用（显示列，而不是 字节），
// 因此估计会密切跟踪真实的画布高度，而不是 OVER-counts 多字节
// 文本 - 两个修剪的 keep-loops 的安全方向。
std::size_t wrapped_rows(std::string_view body, int cols) {
    const std::size_t w = static_cast<std::size_t>(cols < 1 ? 1 : cols);
    std::size_t rows = 0;
    std::size_t line_start = 0;
    while (true) {
        const std::size_t nl = body.find('\n', line_start);
        const std::size_t line_end =
            (nl == std::string_view::npos) ? body.size() : nl;
        rows += line_display_rows(
            body.substr(line_start, line_end - line_start), w);
        if (nl == std::string_view::npos) break;
        line_start = nl + 1;
    }
    return rows == 0 ? 1 : rows;
}

// 由 JSON 参数 源码树 中的每个 STRING 值贡献的 Wrapped
// -row 计数。工具卡 渲染 其大 载荷（写入 `content`，
// 编辑 `edits[].new_text`/`old_text` 等）每行一行，
// 因此真实渲染的高度跟踪这些字符串中的换行符 - 不 JSON 字节 长度。
// 对每个 -string wrapped_rows 进行求和，计算每个 载荷
// 的真实源行（加上换行），它可以在没有 OVER-counting 的情况下
// 密切跟踪渲染高度 - 这是 mid-run 修剪的 keep-loop 的安全
// 侧（请参阅 estimate_msg_rows）。 Non-string
// 标量没有任何贡献（它们 渲染 内联在 头文件 中，而不是 正文 中）。
// 便宜：没有分配，没有dump()。
std::size_t estimate_json_string_rows(const nlohmann::json& j, int cols) {
    switch (j.type()) {
        case nlohmann::json::value_t::string:
            return wrapped_rows(j.get_ref<const std::string&>(), cols);
        case nlohmann::json::value_t::array: {
            std::size_t n = 0;
            for (const auto& e : j) n += estimate_json_string_rows(e, cols);
            return n;
        }
        case nlohmann::json::value_t::object: {
            std::size_t n = 0;
            for (const auto& [k, v] : j.items())
                n += estimate_json_string_rows(v, cols);
            return n;
        }
        default:
            return 0;
    }
}

// 对于其 maya 渲染器 ELIDES 输出到固定 head/tail 预算的工具的最大渲染
// BODY 行，无论 show_all 为何。 tool_body_preview.hpp：
//   bash/diagnostics→bash_output_tail，
//   bash_tail=4 grep/glob/list_dir/web_search/git_*
//   →code_block头+末尾=4+3=7
//   read/find_definition
// →file_read，read_head=5 web_fetch→JSON，
// 头+末尾~7这些渲染 相同 省略了 和 冻结的高度（他们的 渲染 方法 调用 elide()
// 具有固定预算，并且从不兑现 show_all），因此按省略的金额计算完整的 wrapped_rows
// （输出） OVER-counts — 正是使 mid-run keep-loop 放弃
// on-screen 条目并搁浅的原因犯了-scrollback鬼魂。当工具 渲染 其
// 完整的 正文（write/edit 参数路径）时返回 0，以便调用者将其全部计数。
std::size_t tool_output_render_cap(std::string_view name) {
    if (name == "shell" || name == "diagnostics")          return 4;
    if (name == "read" || name == "find_definition")      return 5;
    // 每个 git_*工具省略为code_head+code_tail。
    // 其中三个曾经在这里按名称列出，默默地将其他七个（
    // git_diff/show/blame/branch/stash/rebase/cherry_pick）
    // 计为FULL输出高度，而渲染 7行 - 500-line
    // `git diff`被计为~500并绘制为7。这就是该函数存在的目的是防止
    // over-count，这就是在git-heavy轮上撕毁回滚缓冲区的原因。
    // Prefix-match，因此稍后添加的git工具由构造覆盖，
    // 而不是通过记住编辑此列表来覆盖。
    //
    // git_diff 渲染 Kind::GitDiff 和其余的都落入
    // Kind::CodeBlock，但是两者都通过相同的 code_head/code_tail
    // 预算省略，因此一个上限对于所有这些都是正确的。 （此处较旧的评论声称
    // "git_diff show_all" 渲染 已满 - 但事实并非如此；git_diff_body
    // 从未设置 show_all。）
    if (name.starts_with("git_"))                         return 7;
    if (name == "grep" || name == "glob" || name == "list_dir"
     || name == "web_search" || name == "web_fetch")      return 7;
    return 0;   // 0 ⇒ 无上限（渲染 全输出）
}

// Wrapped-row 计数呈现为降价的 PROSE，考虑了冻结构建的长代码
// 块的 auto-fold。 cached_markdown_for 归并
// 任何长度超过 kFoldLineThreshold (40) 行的隔离代码块
// 到 SETTLED（冻结）渲染 中的 ~1 行。一个普通的 wrapped_rows
// () 会计算这样一个块的每一行——回复中的 100-line 代码块估计
// ~100 行，但 渲染 ~1 折叠，OVER-counting 折叠 ~
// 99 并将 mid-run keep-loop 绊倒 回滚缓冲区 幽灵（与
// bash 输出上限相同的类）。遍历文本：在栅栏之外正常计数换行行；
// 在栅栏 缓冲区 内，对线进行计数，并在关闭时添加 min(real_rows,
// fold_cap)，以便长块折叠到折叠高度。镜像 maya 的 归并 (
// markdown.hpp auto_fold_long_blocks)。
std::size_t prose_rows(std::string_view body, int cols) {
    if (body.empty()) return 0;
    std::size_t rows = 0;
    std::size_t line_start = 0;
    bool in_fence = false;
    std::size_t fence_wrapped = 0;
    std::size_t open_fence_rows = 0;
    const std::size_t w = static_cast<std::size_t>(cols < 1 ? 1 : cols);
    while (true) {
        const std::size_t nl = body.find('\n', line_start);
        const std::size_t line_end =
            (nl == std::string_view::npos) ? body.size() : nl;
        std::string_view line = body.substr(line_start, line_end - line_start);
        // 检测栅栏定界符：第一个非-space运行的行是
        // ``` 或 ~~~，起始围栏后允许附带信息字符串。
        std::string_view trimmed = line;
        while (!trimmed.empty() && trimmed.front() == ' ')
            trimmed.remove_prefix(1);
        const bool is_fence =
            trimmed.rfind("```", 0) == 0 || trimmed.rfind("~~~", 0) == 0;
        const std::size_t line_rows = line_display_rows(line, w);
        if (is_fence) {
            if (!in_fence) {
                in_fence = true;
                fence_wrapped = 0;
                open_fence_rows = line_rows;
            } else {
                in_fence = false;
                // Auto-fold DISABLED:完整的代码块ALWAYS 渲染——
                // 开放栅栏+正文+关闭栅栏——从未折叠成"▸ N lines hidden"存根。
                // 镜子 cached_markdown_for / settle_message_md，
                // 不再是 调用
                // auto_fold_long_blocks。
                rows += open_fence_rows + fence_wrapped + line_rows;
            }
        } else if (in_fence) {
            fence_wrapped += line_rows;
        } else {
            rows += line_rows;
        }
        if (nl == std::string_view::npos) break;
        line_start = nl + 1;
    }
    // 未终止的栅栏(流式传输 快照)-计算其缓冲的
    // 正文未封盖：它不会折叠，直到块关闭+稳定。
    if (in_fence) rows += open_fence_rows + fence_wrapped;
    return rows == 0 ? 1 : rows;
}

// 正文针对EXPLICIT内容宽度的估计。修剪捕获一个 term_dims
// () 快照 并传递其派生列，因此单个 keep-walk 中的每
// 个条目都是根据计算边距的 相同 终端 状态进行测量的 - 中间
// 漂移的宽度 -walk 是使 "provably above the viewport"
// 证明不同步的原因。
//
// 匿名名称空间中的 不：在 internal.hpp 中声明，
// 以便测试可以固定estimate-vs-render 合约（参见那里的评论）。
// 生产调用者都在这个文件中。
}  // 命名空间结束。

std::size_t estimate_msg_rows(const Message& mm, int cols) {
    // Prose 正文：计算真实换行行(换行-aware)，
    // 折叠长代码块以匹配冻结的渲染的auto-fold。
    std::size_t rows = 0;
    if (!mm.text.empty())           rows += prose_rows(mm.text, cols);
    if (!mm.streaming_text.empty()) rows += prose_rows(mm.streaming_text, cols);

    for (const auto& tc : mm.tool_calls) {
        // 工具卡的 RENDERED 正文 是一个 ROW PER SOURCE LINE，
        // 用于写入（line-numbered)/编辑（per-hunk diff)——
        // 这些回显完整的参数 载荷 (show_all)。 OUTPUT-based
        // 工具（bash、read、grep 等）DON'T：它们的 maya 渲染器将
        // 输出省略到固定的 head/tail 预算（参见 tool_output_render_cap），
        // 因此 200-line bash 转储 渲染 ~4 行。
        // 计算 -counts 以上的全部输出 — 上限如下。
        //
        // 该计数必须为 不 OVER-estimate。
        // 它为修剪的 keep-loop 提供数据，它遍历 NEWEST
        // 条目对行进行求和，直到有 "kept the budget"，
        // 然后丢弃其余的。如果保留的条目超过 -counts，则保留总和会
        // 先于实际行到达边距，因此修剪会删除屏幕上实际行为 仍然 的条目
        // — re-emitting 提交的 回滚缓冲区 行 = 重复幽灵。
        // UNDER-count 只会保留更多（更高的画布，
        // 永远不会掉落在 -screen 条目上），因此 under/exact
        // 是安全的一面。
        //
        // 正文仅在每个工具状态的一个源中显示：
        //   ·解决→解析`args`(写入内容/编辑差异块)或
        //                `output()`(read/grep结果)。
        //                参数获胜；我们做NOT还添加args_streaming，
        //                它以raw-JSON形式保存SAME 字节（附加-only，
        //                从未清除）并且会使计数加倍。
        //   ·流式传输→参数尚未解析；活卡 渲染 来自
        //                args_streaming，所以改为计数。
        //
        // ELIDE CAP适用于WHOLE 正文。对于 maya 渲染
        // 器 省略固定 head/tail 预算（bash/read/grep/
        // …）的工具，两者 args-derived 行 和 output()
        // 行 渲染 被封顶，而不仅仅是输出 - 以全高度 OVER
        // -counts 进行计数，并使 mid-run keep-loop
        // 下降on-screen 条目（重复幽灵）。因此，从卡片提取的
        // 任何来源计算原始 正文 行，然后将 SUM 夹到盖子上。
        // 无上限工具（write/edit args、git_diff
        // show_all）的 cap==0 且计数为整数。
        const std::size_t out_cap = tool_output_render_cap(tc.name.value);
        std::size_t tool_rows = 0;
        if (!tc.args.is_null())
            tool_rows += estimate_json_string_rows(tc.args, cols);
        else if (!tc.args_streaming.empty())
            tool_rows += wrapped_rows(tc.args_streaming, cols);
        if (!tc.output().empty())
            tool_rows += wrapped_rows(tc.output(), cols);
        if (out_cap > 0 && tool_rows > out_cap)
            tool_rows = out_cap;
        // 解决时写入/编辑渲染其FULL 正文（无头+末尾上限），
        // 因此估计通过上面的estimate_json_string_rows
        // 对每个源行计数一行——无上限夹。估计显示-column准确，
        // 还有一个-sided UNDER（string_width，
        // ceil-wrap），修剪keep-loops的安全方向。
        rows += tool_rows;
        // 每个工具卡 头文件 / 页脚 / 镀铬行（即使是空的 正文
        // 也有约 4 行 — 标题、分隔符、状态、空白）。固定行数，
        // 宽度-independent。
        rows += 4;
    }
    // Per-message信封(头文件、间隙、分隔线)-一些固定行。
    rows += 3;
    return rows;
}

// 用于尚未保存暗淡快照的one-shot冻结路径(freeze_range、
// rehydrate_frozen)的便利转发器。
// 明确命名而不是重载：匿名
// -namespace重载将为该文件中的每个后续调用者提供SHADOW和外部两个-arg。
namespace {
std::size_t estimate_msg_rows_auto(const Model& m, const Message& mm) {
    return estimate_msg_rows(mm, estimate_wrap_cols(m));
}

// 相对于 EXPLICIT 内容宽度，折叠成 一个 冻结转向条目
// 的运行消息[from..to)的估计行。返回 std::size_t
// - 病态运行（巨大的 线程，微小的 终端 膨胀换行计数）可以对
// INT_MAX 求和，并且截断为负整数会将条目限制在
// push_frozen 中的 1 行，严重低于 -counting 一个
// 巨大的条目，因此它永远不会修剪。
std::size_t estimate_run_rows(const Model& m, std::size_t from,
                              std::size_t to, int cols) {
    std::size_t rows = 0;
    for (std::size_t k = from; k < to && k < m.d.current.messages.size(); ++k)
        rows += estimate_msg_rows(m.d.current.messages[k], cols);
    return rows;
}
std::size_t estimate_run_rows(const Model& m, std::size_t from, std::size_t to) {
    return estimate_run_rows(m, from, to, estimate_wrap_cols(m));
}

// 固定与裁剪原语，对 maya::ScrollbackLedger 的薄封装。
//
// 曾经住在这里的确切的-height ACCOUNTING机器（
// four-vector锁步、ensure_frozen_width
// 的调整大小修复、精确的提交总和）是GONE:maya的油漆通道将每个
// 密封块的lay-out高度记录到每个帧的分类帐中，并且修剪通过
// 这些记录从这些记录中铸造它们的提交计数harvest()。
//
// 现在，密封-time测量也是maya的。 push_frozen 转发到
// ledger.seal_measured()，后者通过 maya 自己的布局引擎
// 在 WIDTH 该 PAINT PASS RECORDED 处为此分类帐的片段
// (record_paint_width) 布置块 — 不是
// 终端列数减去界面装饰占用的左右边距。
// 的主机重建。该重建（measure_cols' -4，以及-2-vs-4 注释
// 堆栈证明它）是该文件中的最后一个测量化石； chrome-padding 的
// 更改曾经悄悄地破坏了它。 seal_measured 具有双重职责：
//
//  1. CACHE WARMING(加载-bearing为冻结接口边界)：布局
//     该过程刷新了块的hash_id的maya的hash-keyed测量条目，
//     因此冻结帧布置在真实高度——oracle-proven
//     字节-stability机制。
//  2. POLICY估算：测量种子block_rows()直至
//     第一账本记载真实价值。我们的粗estimate_run_rows
//     只是前-first-paint 回退。
//
// 会计从不触及这些数字——修剪的提交专门从paint
// -recorded高度(ledger.harvest())铸造，
// 因此这里的测量漂移至多花费一个可避免的软重绘，
// 而绝不会回滚缓冲区损坏。
// `source`是1+构建块的第一条消息的索引(0=无)。
// 重播会读取它以查找实时前缀的开始位置。
void push_frozen(Model& m, maya::Element e, std::size_t rows,
                 bool separator = false, std::uint64_t source = 0) {
    (void)m.ui.frozen.seal_measured(std::move(e), rows, separator, source);
}

// 条带前导分隔器块使得密封前缀总是在真正的转弯
// 时打开（索引0 渲染处的分隔器作为画布顶部的孔）。
// 删除的行产生的债务会在分类账中累积。
void drop_leading_separators(Model& m) {
    (void)m.ui.frozen.drop_leading_separators();
}

// OFF-SCREEN巨型正文的再水合-time崩溃。 该 COLD-REPAINT BOUND。
//
// rehydrate_frozen 在 FRESH 涂料上重建 in-app 画布（仅
// ThreadLoaded — 绝不是 -session 中间），因此这里缩小的条目
// 通过构造是 回滚缓冲区-safe：没有必须保留 字节-accurate 的提交前缀。
// 恢复/调整大小/Ctrl-L上的主要cold-render成本是比整个冻结预算-e.g高的单一工具正文。
// 3000-line 写入。当这样的 正文 不是 尾随条目时，
// re-rendering 每个完整 重绘 上的所有行都不会
// 购买任何东西。将其替换为 one-row 存根；
// 账本会重置区块的记录高度，并在下一次绘制 re-records
// 存根的真实（微小）高度。尾随条目保持满状态。
void collapse_oversized_offscreen_entries(Model& m) {
    if (!m.env.frozen_collapse)          return;
    if (m.ui.frozen.size() < 2)          return;  // 仅当前结果
    const std::size_t budget = frozen_row_budget(m);
    if (m.ui.frozen.row_total() <= budget) return;  // 已经适合

    // 尾随非-separator条目=当前结果；永远不要折叠它。
    std::size_t last_real = m.ui.frozen.size();
    for (std::size_t k = m.ui.frozen.size(); k-- > 0; )
        if (!m.ui.frozen.separator_at(k)) { last_real = k; break; }

    for (std::size_t k = 0; k < m.ui.frozen.size(); ++k) {
        if (k == last_real)                 continue;  // 保持结果完整
        if (m.ui.frozen.separator_at(k))    continue;  // 间隙/分隔物
        const std::size_t cur = m.ui.frozen.block_rows(k);
        if (cur <= budget)                  continue;  // 不是巨人
        m.ui.frozen.replace(k, collapsed_body_stub(cur), /*est_rows=*/1);
    }
}

// Run-level安全门：冻结回合捕获元素快照，
// 其hash_id被标记一次并且从不重新计算。
// not-yet-settled 运行可能会通过两种方式毒害 快照：
//
//  ·待处理/已批准/正在运行的工具——其状态稍后发生变化
//    （当ToolExecOutput落地时）但是
//    m.ui.frozen中渲染的元素永远保持前-mutation状态，
//    在回滚缓冲区中永久可见-Running 加载动画；
//
//  · 字节 仍然在 streaming_text/pending_stream — 正文 是
//    仍在增长，因此 快照 会冻结一个前缀，而实时 末尾
//    re-renders 更长的 正文，将每一行移到
//    下面（重复幽灵）。
//
// 拒绝冻结未完全解决 终端 和 的任何运行。
// 这使得 agent_session 保证保持 BY
// CONSTRUCTION，而不是通过调用者纪律：即使在活动 数据流
// 期间发生错误 -called freeze_through，
// 也会在运行边界处停止（frozen_through 停在 `i`；一旦静止，
// settle-time 冻结就会开始运行）。生产调用者都是空闲的-gated，
// 所以这个门永远不会在那里触发——它的存在是为了让不安全的调用变得不可能，
// 反映了agent_session如何只冻结在它的MessageStop
// 处理函数中。
bool run_is_freezable(const Model& m, std::size_t from, std::size_t run_end) {
    for (std::size_t j = from; j < run_end; ++j) {
        const Message& mm = m.d.current.messages[j];
        if (!mm.streaming_text.empty() || !mm.pending_stream.empty())
            return false;
        for (const auto& tc : mm.tool_calls) {
            if (!tc.is_terminal()) return false;
        }
    }
    return true;
}

// 冻结消息[从..到)，将构建的转向元件(和任何前导间隙/压缩分隔器)
// 推入m.ui.frozen。每个扬声器一个轮次-run：用户消息
// 是它自己的轮次；一系列连续的助手消息会折叠成 一个 轮，
// 其 正文 交错每个 sub-turn 的文本 + 工具批次（参见
// `ui::turn_config_for_assistant_run`）。
// 相同的 `ui::turn_run_end` 帮助器驱动
// `build_live_tail` 中的边界，
// 因此对于相同的输入，冻结行和活动行形状是相同的。
//
// 每次助手运行前进 `m.ui.frozen_turn`
// 一次（一个逻辑代理轮次等于一个显示编号），
// 因此实时 末尾 将计算下一个运行轮次计数保持同步。
// `split_oversized_runs`-将too-tall辅助运行密封为每个子-turn一个块，
// 而不是整个运行的一个块。
//
// OFF为实时路径(freeze_through)。刚刚解决的运行是
// build_live_tail 是 渲染 和 帧 之前的一个，
// 并且两者必须生成 字节-identical 行或冻结 接口边界 在用户下切换。
//
// ON用于再水化。该路径设置 frozen_through = 总计，
// 因此实时 末尾 是 空 — 这些运行中没有 re-renders，
// 也没有 接口边界 可以匹配。这也是需要它的路径：修剪的原子是一个块，
// 因此一个巨大的运行将画布永远固定在其自身的高度。
//
// 建筑物与回滚缓冲区重播（build_history_blocks)共享：
// 两者都走相同的跑步并构建相同的转弯，因此在调整大小之后
// 重播的历史与第一次冻结时的字节相同。 `emit`接收每个块；
// `first_overall` 表示是否已发出。
// 返回步行停止处的索引（`to`，或尚未解决的
// 第一次运行）。
struct BlockSink {
    // (元素、估计行、分隔符、缓存散列或0、第一消息索引)
    std::function<void(maya::Element, std::size_t, bool, std::uint64_t, std::size_t)> emit;
    bool any = false;
    int  turn = 0;   // 跑步助手-turn号（frozen_turn）
    bool drop_view_cache = true;   // 仅实时冻结
};

std::size_t build_blocks(const Model& m, std::size_t from, std::size_t to,
                         bool split_oversized_runs, BlockSink& sink) {
    const std::size_t total = m.d.current.messages.size();
    if (from >= to || to > total) return from;
    // 冻结元素被绘制一次，然后永久位图传输，因此工具 正文 渲染 在
    // 这里具有完整内容 (show_all) — 与实时 末尾 不同，
    // 后者省略了一个窗口以实现 per-frame 的廉价。
    ui::FrozenBuildScope frozen_scope;

    auto needs_compaction_divider = [&](std::size_t i) {
        for (const auto& rec : m.d.current.compactions) {
            if (rec.up_to_index == i && rec.up_to_index > 0
                && rec.up_to_index <= total) return true;
        }
        return false;
    };
    std::size_t cur = from;   // 正在构建的运行（块的源）
    auto push = [&](maya::Element e, std::size_t rows, bool sep = false,
                    std::uint64_t hash = 0) {
        sink.emit(std::move(e), rows, sep, hash, cur);
        sink.any = true;
    };
    // 分隔符无论位于何处看起来都相同（给定间隙设置），
    // 因此每种分隔符都有一个稳定的键：渲染一次，
    // 然后从重播缓存提供。
    auto sep_key = [](std::string_view kind) {
        return maya::CacheIdBuilder{}
            .add(std::string_view{"agentty.sep"})
            .add(kind)
            .add(static_cast<std::uint64_t>(gap_rows()))
            .build().hash();
    };

    std::size_t i = from;
    while (i < to) {
        cur = i;
        // 运行边界——与build_live_tail共享。
        const std::size_t run_end_global =
            ui::turn_run_end(m.d.current.messages, i);
        const std::size_t run_end = std::min(run_end_global, to);

        // 安全门：如果本次运行中任何工具尚未达到终端，
        // 则在此停止；一旦解决，调用者将从 `i` 恢复。
        if (!run_is_freezable(m, i, run_end)) return i;

        const bool first_overall = !sink.any;

        // 压实分隔物，两侧是BOTH侧上的完整的inter
        // -turn间隙——用于上方呼吸空间的前导间隙（因此分节符
        // 不会拥抱其上方的内容）加上下方的尾随间隙。
        // build_live_tail 发出相同的序列，
        // 因此冻结行和活动行在冻结 接口边界 期间保持 字节-identical。
        if (needs_compaction_divider(i)) {
            if (!first_overall)
                push(gap_row(), static_cast<std::size_t>(gap_rows()), /*九月=*/true, sep_key("gap"));
            push(compaction_divider_row(), 1, /*九月=*/true, sep_key("compaction"));
        }
        if (i == ui::import_seam_index(m.d.current)) {
            if (!first_overall)
                push(gap_row(), static_cast<std::size_t>(gap_rows()), /*九月=*/true, sep_key("gap"));
            const std::string from = foreign::display_name(m.d.current.imported->source);
            push(ui::import_seam_row(from), 1, /*九月=*/true, sep_key("import:" + from));
        }

        const Message& head = m.d.current.messages[i];

        // 前导间隙：除了第一个冻结行之外，每轮之前有一个空白
        // 行（避免 top-of-thread 间隙）。
        if (!first_overall) {
            push(gap_row(), static_cast<std::size_t>(gap_rows()), /*九月=*/true, sep_key("gap"));
        }

        if (head.role == Role::Assistant) {
            // 运行通常密封为ONE块，因为这是实时末尾构建的并且两者
            // 必须在冻结接口边界上保持字节-identical
            // （参见build_live_tail)。
            //
            // 但是块也是TRIM的ATOM:drop_front可以脱落
            // 整个块或者不脱落。因此，一次巨大的运行将画布永远固定在其
            // 自身的高度 - 修剪无法将其分开，
            // 并且 collapse_oversized_offscreen_entries
            // 故意跳过尾随条目（这是当前结果）。在恢复的 线程 上，
            // 其最新运行是一个长 auto-pilot 序列，该尾随条目
            // IS 是巨人：根据 180-row 预算测量 一个 条目中的
            // 488 行，稳定的 -state 调整大小为 18 毫秒而不是
            // 4 毫秒。
            //
            // 将其分开。当 CALLER 这么说时是安全的 - 请
            // 参阅有关此功能的 split_oversized_runs 注释。
            // 再水合设置 frozen_through = 总计，
            // 因此其运行没有实时对应项，也没有 接口边界 来保留 字节-identical；
            // 实时稳定路径将其关闭。
            //
            // Sub-turn边界是自然分割：每个
            // 都是运行构建器已经独立布局的文本+工具单元，
            // 因此N个块将相同的行绘制为一个。
            const std::size_t run_rows = estimate_run_rows(m, i, run_end);
            const std::size_t split_at = frozen_row_budget(m);

            if (split_oversized_runs && run_rows > split_at && (run_end - i) > 1) {
                int turn_num = sink.turn + 1;
                for (std::size_t sub = i; sub < run_end; ++sub) {
                    if (sub > i)
                        push(gap_row(), static_cast<std::size_t>(gap_rows()),
                             /*九月=*/true, sep_key("gap"));
                    auto scfg = ui::turn_config_for_assistant_run(
                        sub, sub + 1, turn_num, m);
                    // 以 SUB-RANGE 为键，因此每个块
                    // 缓存 都是独立的，并且稍后的整个 -run 构
                    // 建不会与拆分块发生冲突。
                    scfg.hash_id = ui::assistant_run_hash_id(m, sub, sub + 1);
                    const std::uint64_t h = scfg.hash_id.hash();
                    push(maya::Turn{std::move(scfg)}.build(),
                         estimate_run_rows(m, sub, sub + 1), false, h);
                }
                ++sink.turn;
            } else {
                int turn_num = sink.turn + 1;
                auto cfg = ui::turn_config_for_assistant_run(
                    i, run_end, turn_num, m);

                // 散列键：固定助手运行-通过共享ui::
                // assistant_run_hash_id构建，
                // 因此实时末尾和冻结标记字节-identical键（缓存
                // HIT，冻结接口边界处的零行移位）。一旦冻结，
                // 底层的 字节 就不会改变——该条目的 生命周期 密钥是
                // 稳定的。
                cfg.hash_id = ui::assistant_run_hash_id(m, i, run_end);
                const std::uint64_t h = cfg.hash_id.hash();
                push(maya::Turn{std::move(cfg)}.build(), run_rows, false, h);
                ++sink.turn;
            }
        } else {
            // 用户/compaction-summary single-message转。
            int turn_num = sink.turn;
            auto cfg = ui::turn_config(head, i, turn_num, m,
                                       /*继续=*/false,
                                       /*meta_override=*/{},
                                       /*tool_calls_override=*/{});
            cfg.hash_id = maya::CacheIdBuilder{}
                .add(std::string_view{"agentty.turn"})
                .add(std::string_view{head.id.value})
                .add(head.compute_render_key())
                .build();
            const std::uint64_t h = cfg.hash_id.hash();
            push(maya::Turn{std::move(cfg)}.build(),
                 estimate_run_rows(m, i, run_end), false, h);
        }

        // 通过构造，冻结消息不再从 ViewCache 读取：其轮
        // 次元素被密封到 m.ui.frozen (maya 账本，
        // 从 maya 自己的组件 缓存 中位块传输)，
        // 并且实时 末尾 只迭代 [frozen_through，
        // size())。因此，DROP 的 ViewCache
        // 条目 - 它冻结的 帧 是该条目最后被触及的 帧，
        // 保留它纯粹是泄漏。从两个家中删除也会关闭 pin-leak（
        // 一个停止动画但从未在 渲染 接口边界 — error/cancel
        // mid-reveal 处关闭 -migrated 的小部件，
        // 或者在其排水 帧 之前滚动到前缀的运行）。
        // 这就是为什么固定地图 self-emptying 及其上限变得不必要。
        // 幂等 no-op 适用于消息没有 缓存 条目的常见情况。
        // 重播建立的历史已经被冻结，并且不能触及实时的 缓存。
        for (std::size_t k = i; sink.drop_view_cache && k < run_end; ++k) {
            const auto& mid = m.d.current.messages[k].id;
            m.ui.view_cache.drop(m.d.current.id, mid);
            // 兄弟推理槽(turn.cpp的MdView::Reasoning
            // 键中中部+"#r"下的推理StreamingMarkdown)。
            // 也将其删除，以便推理 逐步显示 小部件不会在冻结后泄漏。
            m.ui.view_cache.drop(m.d.current.id, MessageId{mid.value + "#r"});
        }

        i = run_end;
    }
    return to;
}

void freeze_range(Model& m, std::size_t from, std::size_t to,
                  bool split_oversized_runs = false) {
    BlockSink sink;
    sink.any  = !m.ui.frozen.empty();
    sink.turn = m.ui.frozen_turn;
    sink.emit = [&m](maya::Element e, std::size_t rows, bool sep, std::uint64_t,
                     std::size_t msg) {
        push_frozen(m, std::move(e), rows, sep, static_cast<std::uint64_t>(msg) + 1);
    };
    const std::size_t stopped = build_blocks(m, from, to, split_oversized_runs, sink);
    m.ui.frozen_turn = sink.turn;
    // `to`之前的停止是un-settled运行：frozen_through停在那里，
    // 因此一旦实时路径解决它，下一个freeze_through就恢复。
    if (from < to && to <= m.d.current.messages.size())
        m.ui.frozen_through = stopped;
}

} // 命名空间结束。

void freeze_through(Model& m, std::size_t live_start) {
    if (live_start <= m.ui.frozen_through) return;
    freeze_range(m, m.ui.frozen_through, live_start);
}

// ── 回滚缓冲区 重播源 ──────────────────────────────────────────────────────
// 消息[0，实时前缀开始)作为密封块，以与freeze_range构
// 建它们相同的方式构建，因此调整大小将历史字节-identical重播到它第一次冻结的方式。
// 从不触及实时状态：它自己的回合计数器，没有 view
// -cache 删除实时条目，没有分类帐写入。
std::size_t history_end(const Model& m) {
    // 实时前缀从第一账本块的源消息开始。
    for (std::size_t k = 0; k < m.ui.frozen.size(); ++k)
        if (const auto src = m.ui.frozen.source(k); src > 0)
            return static_cast<std::size_t>(src - 1);
    // 空账本：没有任何东西被冻结，因此没有任何东西被修剪掉。
    return 0;
}

std::vector<maya::ReplayBlock> build_history_blocks(const Model& m) {
    // 重放是模型的VIEW：它读取消息并仅写入视图缓存，
    // 其正是为此的可变（view()做同样的事情）。
    std::vector<maya::ReplayBlock> out;
    const std::size_t end = std::min(history_end(m), m.d.current.messages.size());
    if (end == 0) return out;
    // 此会话从未显示的消息的结算降价（恢复的线程的
    // 跳过的前缀），使用相同的同步结算再水合，
    // 因此折叠高度匹配。记住我们创建了哪些，以便之后我们
    // 可以准确地删除它们。
    std::vector<std::size_t> settled_here;
    for (std::size_t k = 0; k < end; ++k) {
        const Message& mm = m.d.current.messages[k];
        if (mm.role == Role::Assistant && !mm.text.empty()
            && !m.ui.view_cache.peek(m.d.current.id, mm.id)) {
            settle_message_md_cached(m, mm);
            settled_here.push_back(k);
        }
    }
    BlockSink sink;
    sink.drop_view_cache = false;
    // 所呈现的字节还取决于回合标签("turn N")和
    // 相对时间戳("4m ago")，而hash_id没有涵
    // 盖这一点。 归并 轮数和时间戳是相对的分钟，
    // 因此缓存的块永远不会显示带有 过期 标签。
    const bool relative = agentty::ui_prefs::current().timestamps
                          == agentty::ui_prefs::Timestamps::Relative;
    const std::uint64_t minute = relative
        ? static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::minutes>(
              m.wall_at(maya::anim_now()).time_since_epoch()).count())
        : 0;
    sink.emit = [&out, &sink, minute](maya::Element e, std::size_t, bool sep,
                                      std::uint64_t hash, std::size_t) {
        if (hash != 0 && !sep)
            hash = maya::CacheIdBuilder{}
                       .add(hash)
                       .add(static_cast<std::uint64_t>(sink.turn))
                       .add(minute)
                       .build().hash();
        out.push_back(maya::make_replay_block(hash, std::move(e)));
    };
    (void)build_blocks(m, 0, end, /*split_oversized_runs=*/true, sink);
    // 接口边界：一个间隙将最后一个历史回合与实时前缀的第
    // 一个回合分开。 drop_leading_separators
    // 通常会去除实时前缀的前导间隙，因此历史记录会提供它；
    // 如果实时前缀仍然在分隔符上打开，则它已经有一个分隔符。
    // 实时 帧 以 AppLayout 的顶部填充行开始，
    // 它代表间隙的尾随空白，因此历史记录会添加间隙减去该行。
    if (!out.empty() && !m.ui.frozen.empty() && !m.ui.frozen.separator_at(0)) {
        using namespace maya::dsl;
        if (gap_rows() > 1)
            out.push_back(maya::make_replay_block(maya::CacheIdBuilder{}.add(std::string_view{"agentty.sep.seam"}).add(static_cast<std::uint64_t>(gap_rows())).build().hash(), v(blank(), maya::Conversation::divider()).build()));
        else
            out.push_back(maya::make_replay_block(maya::CacheIdBuilder{}.add(std::string_view{"agentty.sep.seam"}).add(static_cast<std::uint64_t>(gap_rows())).build().hash(), maya::Conversation::divider()));
    }
    // 密封的元素拥有自己的降价源码树；为此确定的
    // 缓存 条目是沉重的。放下它们。
    for (std::size_t k : settled_here)
        m.ui.view_cache.drop(m.d.current.id, m.d.current.messages[k].id);
    return out;
}

// 重放可以打印的行：AGENTTY_REPLAY_ROWS（如果设置），
// 否则 回滚缓冲区 设置，并且永远不会超过 tmux 保留的内容。
// Auto 是 终端 自己的 回滚缓冲区 长度：启动探针进入后的答案，
// 在此之前是 env-only 猜测（tmux，或 终端 记录的默认值）。
std::pair<int, std::size_t> scrollback_replay_limits(const Model& m) {
    constexpr int kAll = std::numeric_limits<int>::max();
    if (m.env.replay_rows_override >= 0)
        return {m.env.replay_rows_override, m.env.replay_max_bytes};
    int rows = 0;
    const auto level = m.d.ui().scrollback;
    if (level == ui_prefs::Scrollback::Auto) {
        if (const auto& t = m.ui.terminal_scrollback; t && (t->unlimited || t->rows > 0))
            rows = t->unlimited ? kAll : t->rows;
        else if (m.env.auto_scrollback_rows > 0)
            rows = m.env.auto_scrollback_rows;
        else
            rows = ui_prefs::scrollback_rows(level);
    } else {
        rows = ui_prefs::scrollback_rows(level);
        if (rows == 0) rows = kAll;   // 满
    }
    // 无论如何，超过 tmux 保留的行都会被它删除。
    if (m.env.terminal_history_rows > 0 && rows > m.env.terminal_history_rows)
        rows = m.env.terminal_history_rows;
    return {rows, m.env.replay_max_bytes};
}

// 相同的历史，一次拉最新的-first一个块，因此具有行上限的重放仅
// 构建它所显示的内容。 `cursor` 是前一个块开始的消息索引
// （SIZE_MAX = 从实时前缀开始）。每个块覆盖以
// `cursor` 结尾的整个运行，大约是其中的 kChunkRuns。
maya::ScrollbackReplay::Chunk history_chunk(const Model& m, std::size_t cursor) {
    maya::ScrollbackReplay::Chunk out;
    const auto& msgs = m.d.current.messages;
    const std::size_t live = std::min(history_end(m), msgs.size());
    const std::size_t hi = std::min(cursor, live);
    if (hi == 0) return out;

    // 块位于从 线程 开始计数的 FIXED 边界上（每个
    // kChunkRuns 运行），因此相同的块是相同的键，
    // 但是活动前缀移动了：重新打开 线程 将其窗口放置在其他位置，
    // 并且 live-relative 块将永远不会再次到达 缓存。
    // 仅接触实时前缀的块在 `hi` 处被缩短。
    constexpr int kChunkRuns = 24;
    // 整个线程的运行开始，计算一次并被该重放的每
    // 个块重用（以及下一个，直到线程增长）。
    auto& rs = m.ui.history_runs;
    if (rs.thread != m.d.current.id || rs.msg_count != msgs.size()) {
        rs.thread = m.d.current.id;
        rs.msg_count = msgs.size();
        rs.starts.clear();
        rs.turn_at.clear();
        int turn = 0;
        for (std::size_t k = 0; k < msgs.size();) {
            rs.starts.push_back(k);
            rs.turn_at.push_back(turn);
            if (msgs[k].role == Role::Assistant) ++turn;
            k = ui::turn_run_end(msgs, k);
        }
    }
    // 最后一次运行在 `hi` 之前开始，并且固定边界位于或低于它。
    const auto it = std::lower_bound(rs.starts.begin(), rs.starts.end(), hi);
    if (it == rs.starts.begin()) return out;
    const std::size_t last_run  = static_cast<std::size_t>(it - rs.starts.begin()) - 1;
    const std::size_t first_run = last_run - (last_run % kChunkRuns);
    const std::size_t lo = rs.starts[first_run];
    // `lo`处的转弯号码：助手在其之前运行（补充水的规则）。
    const int turn_at_lo = rs.turn_at[first_run];

    const bool relative = agentty::ui_prefs::current().timestamps
                          == agentty::ui_prefs::Timestamps::Relative;
    const std::uint64_t minute = relative
        ? static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::minutes>(
              m.wall_at(maya::anim_now()).time_since_epoch()).count())
        : 0;

    // 重用来自较早重放的构建元素：密钥涵盖[lo，
    // hi)中每条消息的id+渲染密钥、主题纪元、
    // turn-label输入以及该块是否携带接口边界。
    // 任何会改变渲染的 源码树 的东西都会改变键。按照 线程 保留
    // （最近很少），因此切换回 线程 会重用它所构建的内容。
    auto& hist = m.ui.history;
    auto th = std::find_if(hist.begin(), hist.end(),
                           [&](const auto& h) { return h.id == m.d.current.id; });
    if (th == hist.end()) {
        hist.insert(hist.begin(), Model::UI::ThreadHistory{m.d.current.id, {}});
        if (hist.size() > Model::UI::kHistoryThreads) hist.pop_back();
        th = hist.begin();
    } else if (th != hist.begin()) {
        std::rotate(hist.begin(), th, th + 1);   // 最近的第一个
        th = hist.begin();
    }
    auto& chunks = th->chunks;
    const bool seam = cursor == SIZE_MAX;
    maya::CacheIdBuilder kb;
    kb.add(static_cast<std::uint64_t>(hi)).add(minute)
      .add(static_cast<std::uint64_t>(maya::theme::live_epoch()))
      .add(static_cast<std::uint64_t>(gap_rows()))
      .add(static_cast<std::uint64_t>(seam && !m.ui.frozen.separator_at(0)));
    for (std::size_t k = lo; k < hi; ++k) {
        kb.add(std::string_view{msgs[k].id.value});
        kb.add(msgs[k].compute_render_key());
    }
    const std::uint64_t key = kb.build().hash();
    if (auto it = chunks.find(lo); it != chunks.end() && it->second.key == key) {
        out.blocks = it->second.blocks;   // 元素是共享句柄：便宜
        out.next   = it->second.next;
        return out;
    }

    // `lo`处的转弯数：上面用跑步步行计算的。
    const int turn = turn_at_lo;

    std::vector<std::size_t> settled_here;
    for (std::size_t k = lo; k < hi; ++k) {
        const Message& mm = msgs[k];
        if (mm.role == Role::Assistant && !mm.text.empty()
            && !m.ui.view_cache.peek(m.d.current.id, mm.id))
            settled_here.push_back(k);
    }
    // 解析是重播的冷成本（该会话从未显示的消息）。每条消息都有
    // 自己的 StreamingMarkdown，因此：在此 线程
    // 上创建插槽（缓存 映射不是 线程-safe），
    // 然后并行创建 解析 它们。一个小部件只触及它自己的状态。
    if (!settled_here.empty()) {
        // 解析进入该函数拥有的小部件，
        // 然后将它们交给该线程上的缓存：跨并行部分不保存
        // 对缓存映射的引用。
        std::vector<std::shared_ptr<maya::StreamingMarkdown>> parsed(settled_here.size());
        maya::exec::parallel_for(parsed.size(), [&](std::size_t j) {
            auto w = std::make_shared<maya::StreamingMarkdown>();
            w->set_content(msgs[settled_here[j]].text);
            w->finish();
            parsed[j] = std::move(w);
        });
        for (std::size_t j = 0; j < parsed.size(); ++j) {
            const Message& mm = msgs[settled_here[j]];
            auto& c = m.ui.view_cache.message_md(m.d.current.id, mm.id);
            c.streaming         = std::move(parsed[j]);
            c.last_settled_size = mm.text.size();
            c.revealed_size     = mm.text.size();
        }
    }
    BlockSink sink;
    sink.drop_view_cache = false;
    sink.turn = turn;
    sink.any  = lo > 0;   // 上方有间隙，除非这是第一个转弯
    sink.emit = [&out, &sink, minute](maya::Element e, std::size_t, bool sep,
                                      std::uint64_t hash, std::size_t) {
        // 转弯块混合在转弯标签和时钟中；分隔符保留其共享的 per-kind
        // 密钥（build_blocks' sep_key）。
        if (hash != 0 && !sep)
            hash = maya::CacheIdBuilder{}
                       .add(hash)
                       .add(static_cast<std::uint64_t>(sink.turn))
                       .add(minute)
                       .build().hash();
        out.blocks.push_back(maya::make_replay_block(hash, std::move(e)));
    };
    (void)build_blocks(m, lo, hi, /*split_oversized_runs=*/true, sink);
    // 第一块（最接近实时前缀）以接口边界间隙结束。
    if (cursor == SIZE_MAX && !out.blocks.empty() && !m.ui.frozen.empty()
        && !m.ui.frozen.separator_at(0)) {
        using namespace maya::dsl;
        if (gap_rows() > 1)
            out.blocks.push_back(maya::make_replay_block(maya::CacheIdBuilder{}.add(std::string_view{"agentty.sep.seam"}).add(static_cast<std::uint64_t>(gap_rows())).build().hash(), v(blank(), maya::Conversation::divider()).build()));
        else
            out.blocks.push_back(maya::make_replay_block(maya::CacheIdBuilder{}.add(std::string_view{"agentty.sep.seam"}).add(static_cast<std::uint64_t>(gap_rows())).build().hash(), maya::Conversation::divider()));
    }
    for (std::size_t k : settled_here)
        m.ui.view_cache.drop(m.d.current.id, msgs[k].id);
    out.next = lo;
    // 保留用于下一次重播（调整大小返回、线程切换回来），
    // 因此它仅re-lays输出。仅保留当前 线程 的
    // 块：其他块根据需要再次构建，无论如何这是冷路径。
    chunks[lo] = Model::UI::HistoryChunk{key, out.blocks, out.next};
    return out;
}

// ── 递增(中间) -stream) 正文 冻结 -- REMOVED ────────────────────
// mid-stream雕刻(
// maybe_incremental_freeze/live_body_freeze/incremental_freeze_target和AGENTTY_INCREMENTAL_FREEZE门)
// 是DELETED，而不仅仅是禁用。它将 still-streaming
// 回复的块密封到 m.ui.frozen mid-turn 中：附加到冻结的前
// 缀 WHILE，实时后缀在同一更新中缩小 - 两个形状变化 maya 的
// STATEFUL 内联 回滚缓冲区-prefix-match 差异无法消除歧义。
// 雕刻的 Turn 标记为 cfg.hash_id={} （rebuild
// -allowed，关键 maya 的组件 缓存 从未在之前的实时 帧 上绘制过），
// 因此 缓存 错过并重新-emitted 这些行，有时覆盖已经提交给本机
// 回滚缓冲区 的行：重复的-lines /lush-left-header
// / box-border /composer-bleed腐败。这是 相同
// 的结构故障，与早期的 freeze_settled_subturns
// / freeze_streaming_text_prefix 机械一样，
// 重新引入了两次 "for performance" 并恢复了两次。
//
// 一个声音冻结瞬间是turn-settle (finalize_turn →
// pending_settle_freeze → freeze_through,
// meta.cpp)，在 is_idle() && live_tail_reveal_settled
// () 上门控，并在哈希 字节-identical 上键入实时 末尾 (
// assistant_run_hash_id) — 一个 maya 组件-cache HIT，
// 零重新-emit。 流式传输 通过 maya 的 StreamingMarkdown
// reveal_fx 在屏幕上保持完全增量；仅 freeze/commit 边界被推迟解决。
// 实时 末尾 的 Per-frame 成本受 STABLE live-tail 哈希上 maya
// 的 缓存 限制（在 o1_probe / long_session_bench 中持平），
// 因此 mid-stream 雕刻不会购买任何内容，并且会影响正确性。不要把它带回来。

// ensure_frozen_width是GONE。它的存在是为了在
// 调整大小后修复主机的并行 row-count 标记；
// 账本的绘制记录了每个 帧 上实时宽度的每个块的 re-stamps，
// 因此在一个绘制内调整 self-heals 的大小而无需主机操作。
// （在调整大小和下一次绘制之间的更新周期中的修剪着陆会在旧宽度上产生债务
// - 安全：宽度变化将内联一致性降级为 HardReset，
// 其中 commit_inline_prefix 是 no-op。）

void clear_frozen(Model& m) {
    m.ui.frozen.clear();
    m.ui.frozen_through = 0;
    m.ui.frozen_turn    = 0;
    m.ui.pending_settle_freeze   = false;
    m.ui.pending_rehydrate_trim  = false;
}

void rehydrate_frozen(Model& m) {
    clear_frozen(m);
    const auto& msgs = m.d.current.messages;
    const std::size_t total = msgs.size();
    if (total == 0) return;

    // 有界再水化。实时冻结矢量 IS 内联画布：
    // 每个完整的重绘 maya(恢复交换、RESIZE→发散擦拭+重绘、
    // Ctrl-L)都会走遍整个画布顶部-to-bottom，
    // 以及进行绘画的用户WATCHES。因此，实时画布高度（而不仅仅是
    // 简历）必须保持较小。慷慨的 1500-row 种子仍然意味
    // 着从顶部开始每次调整 重绘 1500 行的大小。
    //
    // 将种子绑定到 可视区域 的 SMALL 倍数（~2 个屏幕）。
    // 所有较旧的内容都会在第一次绘制时（或实时时）发送到终端的
    // NATIVE 回滚，因此scroll-up 仍然显示它
    // - 由终端立即绘制，而不是由我们 re-emitted。
    // 完整的转录本在磁盘上完好无损；通过 选择面板 回忆旧
    // 的上下文。保留画布约 2 个屏幕使得恢复、
    // 调整大小和 重绘 都只绘制约 2 个屏幕。
    //
    // 向后遍历整个扬声器-runs，累积估计行；
    // 在超出预算的运行后停止，以便最新的上下文始终是完整的。
    // 如果最新的运行单独超出了预算，则将 内部 削减为 sub
    // -turn 粒度（保持尾随的 sub-turns 适合），
    // 这样即使是巨大的最终 auto-pilot 运行也能
    // 快速恢复。
    const std::size_t kRehydrateRowBudget = rehydrate_row_budget(m);
    std::size_t row_budget = 0;
    std::size_t start      = total;
    std::size_t cursor     = total;
    while (cursor > 0) {
        std::size_t j = cursor;
        if (msgs[j - 1].role == Role::Assistant) {
            while (j > 0 && msgs[j - 1].role == Role::Assistant) --j;
        } else {
            --j;
        }
        std::size_t run_rows = 0;
        for (std::size_t k = j; k < cursor; ++k)
            run_rows += estimate_msg_rows_auto(m, msgs[k]);

        // 削减 内部 超出预算剩余部分的运行，
        // 保留其最新的 sub-turns。
        //
        // 该状况曾经是`units == 0`守卫、i.e。
        // 只能剪切 首次（最新）运行。这限制了常见情况并留下了
        // 一个漏洞：达到第二个或更晚的大运行被采取了 整个，
        // 然后预算检查才启动。一个真正的 线程 最终冻结了
        // 2247 行，而预算为 180-row - 超出了
        // 12 倍 - 因为运行 #2 本身就有 2100 行。
        //
        // 这对于 RESIZE 来说最为重要。宽度更改会使每个缓存的布局
        // 失效（换行点移动），因此整个冻结的画布 re-lays-out：
        // 在 2247 行上测量每次调整大小为 4.44 毫秒，
        // 而每次调整大小为 0.43 毫秒，一次拖动会为每列发出一次调整大小。
        //
        // `remaining`而不是全部预算，
        // 因此运行已经对其进行计数——否则相同的超调会导致
        // 下降一级。
        const std::size_t remaining = kRehydrateRowBudget > row_budget
                                        ? kRehydrateRowBudget - row_budget
                                        : 0;
        if (run_rows > remaining && (cursor - j) > 1) {
            std::size_t kept = 0;
            std::size_t cut  = cursor;
            for (std::size_t k = cursor; k-- > j; ) {
                kept += estimate_msg_rows_auto(m, msgs[k]);
                cut = k;
                if (kept >= remaining) break;
            }
            // 单个子-turn仍然可以单独超过`remaining`；
            // 这是故意的。切入一条消息下方将显示半
            // -rendered转弯，而下面的折叠
            // 通道则限制了真正超大的单个正文。
            start = cut;
            row_budget += kept;
            break;
        }

        start = j;
        row_budget += run_rows;
        if (row_budget >= kRehydrateRowBudget) break;
        cursor = j;
    }

    // 种子frozen_turn到跳过的前缀中的辅助运行的计数，
    // 因此可见的回合数反映了它们的真实位置(e.g.
    // "turn 87"而不是长时间重新加载时的"turn 1")。
    int skipped_assistant_runs = 0;
    for (std::size_t k = 0; k < start; ) {
        const std::size_t run_end = ui::turn_run_end(msgs, k);
        if (msgs[k].role == Role::Assistant) ++skipped_assistant_runs;
        k = run_end;
    }
    m.ui.frozen_turn = skipped_assistant_runs;

    // 在冻结前解决所保留窗口的markdown SYNCHRONOUSLY。
    // freeze_range通过cached_markdown_for构
    // 建每个回合，对于已解决的消息，将大正文路由到set_content_async
    // ——然后构建密封元素BEFORE，异步解析提交任何块，
    // 因此auto_fold_long_blocks与归并没有任何关系，
    // 并且密封的正文在完全展开的高度（300-line栅栏）绘制约 300 行，
    // 而不是 ~1-row 归并 存根）。上面的预算行走已经假设了
    // FOLDED 高度（prose_rows 反映了 归并），
    // 因此冻结的画布是预算的 60 倍——每个后续的 帧（以及每个
    // Ctrl+L 恢复 重绘，即 O（画布行））都为此付出了代价。
    // settle_message_md与结算时应用的数据流路径相同，
    // 同步set_content+完成+auto_fold；之后，
    // cached_markdown_for 取其固定的 fast-path，
    // 并且密封高度与估计相符。只有保留的窗口支付同步解析；跳过的前缀 [0,
    // start) 根本不会被构建。
    for (std::size_t k = start; k < total; ++k) {
        const Message& mm = msgs[k];
        if (mm.role == Role::Assistant && !mm.text.empty())
            settle_message_md(m, mm);
    }

    freeze_range(m, start, total, /*split_oversized_runs=*/true);

    // mid-run剪切(巨大的最终运行)可以使第一个冻结
    // 条目成为其自己的头文件被跳过的延续，
    // OR运行-boundary间隙可以首先着陆——或者在洞上打开画布。
    // 去掉所有前导分隔符，以便前缀在真正的转弯时打开。
    drop_leading_separators(m);

    // 限制冷-repaint成本：任何高于整个冻结预算的OFF
    // -SCREEN 正文被替换为one-row存根（仅新鲜-paint，
    // 因此回滚缓冲区-safe）。尾随条目（当前结果）保持完整。
    collapse_oversized_offscreen_entries(m);
}

Cmd trim_frozen_if_oversized(Model& m) {
    // 密封前缀上的软帽。在其上方，最旧的块被删除 - maya
    // 的行差异看到较短的存活 源码树，并且已经的
    // -overflowed 行自然地提交给本机 回滚缓冲区。
    //
    // POLICY vs ACCOUNTING。该函数计算的所有内容
    // （行预算、keep-walk、要删除的块数）都是 POLICY，
    // 从分类帐的 Paint-recorded（或 seal
    // -estimate）行计数中读取。 ACCOUNTING（maya
    // 的影子推进的提交计数）由 ledger.harvest() 从
    // maya 自己记录的绘制通道高度独家铸造，
    // 以主机无法制造或调整的 带类型的 ScrollbackDebt 形式返回。
    // 这里的草率政策会导致 CONTEXT 修剪过多或过少；
    // 它永远不会损坏 回滚缓冲区。
    //
    // 为什么是 ROWS，而不是条目：内联画布 auto-resizes
    // 到 `row_total + chrome`，以及 per-frame
    // 渲染 成本与 TOTAL SEALED ROWS 成比例，
    // 而不是条目计数 — 单个 write/edit 正文 是 一个
    // 块中的数百行。较旧的轮次保留在 终端 的本机 回滚缓冲区 中（
    // 当它们溢出时在那里提交），并且完整的消息历史记录在磁盘上完好无损
    // - 只有 in-app re-render 窗口缩小。

    const std::size_t kFrozenMaxRows = frozen_row_budget(m);
    constexpr std::size_t kFrozenMaxEntries = 120;
    // 保留层——参见上面的 row-driven 基本原理：
    // 保留尾随块直到它们覆盖最近工作的~kFrozenMaxRows，
    // 永远不小于2，并且仅当保留的块很小时才扩大到
    // kKeepMinEntries。
    constexpr std::size_t kKeepMinEntries = 8;

    const bool over_rows    = m.ui.frozen.row_total() > kFrozenMaxRows;
    const bool over_entries = m.ui.frozen.size() > kFrozenMaxEntries;
    if (!over_rows && !over_entries) return Cmd::none();

    std::size_t budget_entries = 0;
    std::size_t keep_rows      = 0;
    for (std::size_t k = m.ui.frozen.size(); k-- > 0; ) {
        ++budget_entries;
        keep_rows += m.ui.frozen.block_rows(k);
        if (keep_rows >= kFrozenMaxRows) break;
    }
    std::size_t keep_entries =
        (keep_rows >= kFrozenMaxRows)
            ? budget_entries
            : std::max(budget_entries, kKeepMinEntries);
    if (keep_entries < 2)                  keep_entries = 2;
    if (keep_entries > m.ui.frozen.size()) keep_entries = m.ui.frozen.size();

    // 从前面放下块直到满足两个上限，
    // 以上面计算的保留底板为界。
    std::size_t drop = 0;
    const std::size_t max_drop = m.ui.frozen.size() - keep_entries;
    std::size_t rows_after    = m.ui.frozen.row_total();
    std::size_t entries_after = m.ui.frozen.size();
    while (drop < max_drop
           && (rows_after > kFrozenMaxRows || entries_after > kFrozenMaxEntries)) {
        rows_after -= m.ui.frozen.block_rows(drop);
        --entries_after;
        ++drop;
    }
    // 退化keep-walk：budget-crossing块是OLDEST条目
    // （巨大的mid-run延续——补水切割保持运行，
    // 其真实绘制高度使其估计相形见绌）。然后，步行固定每个条目（
    // budget_entries == 大小 ⇒ max_drop
    // == 0），上面的循环不会丢失任何内容，使画布永远超出预算数倍。
    // 当剩余的尾随块仍然覆盖至少一个可视区域上下文时，
    // 仅删除最旧的巨人是安全的——
    // 被删除的行通过与任何其他修剪相同的可证明的drop_front/harvest路径提交到本机回滚缓冲区。
    //
    if (drop == 0 && over_rows && m.ui.frozen.size() > 2) {
        const std::size_t first_rows = m.ui.frozen.block_rows(0);
        const std::size_t rest       = m.ui.frozen.row_total() - first_rows;
        const auto dims = term_dims(m);
        const std::size_t viewport =
            static_cast<std::size_t>(dims.rows > 0 ? dims.rows : 24);
        if (first_rows > kFrozenMaxRows && rest >= viewport)
            drop = 1;
    }
    if (drop == 0) return Cmd::none();

    // 该 ACCOUNTING。 drop_front 将掉落
    // 量化到安全边界（延伸穿过暴露的分隔符，夹住 Paint
    // -recorded 前缀，永远不会在前面留下分隔符），
    // 并累积每个掉落块的 PAINT-RECORDED 高度作为债务。
    // harvest() 铸造 带类型的 代币 — 正是为掉落
    // 的块物理绘制的 maya 行，通过 maya 自己的布局
    // 通道在最后一个 帧 的实时宽度上测量。不存在漂移的主机
    // 测量。
    (void)m.ui.frozen.drop_front(drop);
    auto debt = m.ui.frozen.harvest();
    if (debt.empty()) return Cmd::none();

    return cmd::commit_scrollback(std::move(debt));
}

} // namespace agentty::app::detail
