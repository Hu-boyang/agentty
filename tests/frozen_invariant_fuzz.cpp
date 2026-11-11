// frozen_invariant_fuzz ——对被冻结的财产进行随机测试. 回滚缓冲区 子
// 系统( 3) src/runtime/app/update/frozen.cpp).
//
// 中场比赛的测试针 具体,已知 -bad 假设情况。
// 相同 不变量 从另一个方向: 它驱动 ARBITRARY
// 对话增长的序列 + 冻结 -subsystem
// 业务和索赔的固定结构 不变量 持有日期 每个
// 操作——不只是手 -picked 州级。
//
// 捕捉的虫类正是真正的 回滚缓冲区 腐败一直存在:
// freeze/trim 路径,使平行的冻结阵列脱离锁步,
// 拖动一个主分隔符, 让 frozen_through 运行
// 未执行 messages.size(),
// under/over- 承诺 回滚缓冲区 横行,
// 或冻结 可变 流式传输 背面信息。每个都是一个 -line 不变量
// 随机走动,它会打印种子 + 操作跟踪,所以它具有决定性的再生产性。
//
// 不变量 检查每一次操作后( 见 check_invariants):
//   I1 锁步 : frozen.size()==frozen_rows.size()==
//   frozen_is_separator.size() I2  frozen_row_total ⁇   ⁇
//   frozen_rows ),并且永远不要 < 0 (
//   size_t 下流) I3  frozen_through <= messages.size
//   () I4 冻结的前缀永远不会 OPENS 分隔符(gap)条目 I5 期间 活动 数据流,
//       frozen_through 永远无法到达 可变 背面信息 ( messages.size ()
//   —— 重复 -card 错误 I6 修剪( C) Cmd 要么是无,要么是 CommitScrollback
//       算得上保守派的吗? UNDER-estimate 投放(承诺 QQ 投放,从未投出)
//       OVER-commit 永远; 永远 CommitScrollbackOverflow).
//       完毕 -commit 制作下一个 帧 增长 + maya 滚动可见的
//       末尾 变成本地 回滚缓冲区 = 重复带。 -commit 已调和
//       maya's 渲染-time 调值回收(
//       scrollback_prefix_matches + 承付溢额+ 案件
//       -B 因此它是安全的—— 与真实的 maya 字节 并被反射为
//       midrun_wire_test ,哪个 渲染 修剪经过 maya 并接受和解。
//       这是 agent_session 纪律(它蓄意从事
//       n*ROWS_PER_DROP_LOWER 下级 -estimate).
//   I7  frozen_rows 条目都是 QQ 0
//
// 重写: 失败的打印 `SEED=<n>` ——关于 -run 针脚走,
// 因为 PRNG 和 op 选择是种子的纯函数。

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <maya/render/canvas.hpp>
#include <maya/render/renderer.hpp>
#include <maya/style/theme.hpp>
#include <maya/widget/app_layout.hpp>

#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/msg.hpp"
#include "agentty/runtime/view/changes_strip.hpp"
#include "agentty/runtime/view/composer.hpp"
#include "agentty/runtime/view/status_bar/status_bar.hpp"
#include "agentty/runtime/view/thread/conversation.hpp"
#include "agentty/runtime/view/thread/thread.hpp"

using agentty::Model;
using agentty::Message;
using agentty::Role;
using agentty::ToolCallId;
using agentty::ToolName;
using agentty::ToolUse;
using std::chrono::milliseconds;
using std::chrono::steady_clock;

namespace {  // 归并: TU-local (捆绑成 agentty_standalone_tests)
static int g_failures = 0;
static int g_checks   = 0;

// ── 确定PRNG( splitmix64) ──────────────────────────────────────
struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed) {}
    std::uint64_t next() {
        s += 0x9e3779b97f4a7c15ULL;
        std::uint64_t z = s;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }
    // [0, n)
    int below(int n) { return n <= 0 ? 0 : static_cast<int>(next() % static_cast<std::uint64_t>(n)); }
    bool chance(int pct) { return below(100) < pct; }
};

// ── 构建器 ──────────────────────────────────────────────────────────────
static std::string code_block(int n) {
    std::string out;
    for (int i = 0; i < n; ++i)
        out += "    auto x = compute(i) + offset; // plausible line\n";
    return out;
}

static ToolUse settled_edit(const std::string& tag, int lines) {
    ToolUse t;
    t.id   = ToolCallId{"edit_" + tag};
    t.name = ToolName{"edit"};
    nlohmann::json edits = nlohmann::json::array();
    edits.push_back({{"old_text", code_block(lines)},
                     {"new_text", code_block(lines)}});
    t.args = {{"path", "src/" + tag + ".cpp"}, {"edits", edits}};
    auto now = steady_clock::now();
    t.status = ToolUse::Done{now - milliseconds{5}, now, "edited-" + tag};
    return t;
}

static ToolUse settled_write(const std::string& tag, int lines) {
    ToolUse t;
    t.id   = ToolCallId{"write_" + tag};
    t.name = ToolName{"write"};
    std::string content;
    for (int i = 0; i < lines; ++i)
        content += "line " + std::to_string(i) + ": file body content\n";
    t.args = {{"file_path", "/tmp/" + tag + ".txt"}, {"content", content}};
    auto now = steady_clock::now();
    t.status = ToolUse::Done{now - milliseconds{5}, now, "Created"};
    return t;
}

static ToolUse settled_bash(const std::string& tag, int out_lines) {
    ToolUse t;
    t.id   = ToolCallId{"bash_" + tag};
    t.name = ToolName{"shell"};
    t.args = {{"command", "grep -rn pattern src # " + tag}};
    std::string output;
    for (int i = 0; i < out_lines; ++i)
        output += "src/f" + std::to_string(i) + ".cpp:" + std::to_string(i)
                + ": matching line\n";
    auto now = steady_clock::now();
    t.status = ToolUse::Done{now - milliseconds{5}, now, std::move(output)};
    return t;
}

// ── 操作日志, 打印在失败上, 用于确定重制程序 ──────────────
static std::vector<std::string> g_oplog;
static std::uint64_t g_seed = 0;

static void fail(const char* inv, const char* detail) {
    ++g_failures;
    std::fprintf(stderr, "  FAIL [%s] %s  (SEED=%llu)\n", inv, detail,
                 static_cast<unsigned long long>(g_seed));
    std::fprintf(stderr, "    op trace (%zu ops):\n", g_oplog.size());
    for (const auto& o : g_oplog) std::fprintf(stderr, "      %s\n", o.c_str());
}

#define INV(cond, name, detail)                          \
    do { ++g_checks; if (!(cond)) fail(name, detail); } while (0)

// ── 电话 不变量 电池 ────────────────────────────────────────────────
static void check_invariants(const Model& m, bool stream_active,
                             const agentty::Cmd* trim_cmd,
                             std::size_t expected_dropped_rows) {
    const auto& f = m.ui;

    // I1 锁步已经到了 STRUCTURAL : 分类账拥有
    // 一个内元矢量, 并附带其元素—— 没有主机 -side
    // 并行向量左移。检查访问者是否在 -range 换成是的
    for (std::size_t k = 0; k < f.frozen.size(); ++k)
        INV(f.frozen.block_rows(k) >= 1 || !f.frozen.recorded_at(k), "I1",
            "ledger block has zero recorded rows");

    // I7 + I2 行会计 : row_total 等于总和 block_rows.
    std::size_t sum = 0;
    for (std::size_t k = 0; k < f.frozen.size(); ++k)
        sum += f.frozen.block_rows(k);
    INV(sum == f.frozen.row_total(), "I2",
        "row_total() != sum(block_rows)");

    // I3 边框
    INV(f.frozen_through <= m.d.current.messages.size(), "I3",
        "frozen_through outran messages.size()");

    // I4 前缀从不打开分隔符( gap) 行
    if (!f.frozen.empty())
        INV(!f.frozen.separator_at(0), "I4",
            "frozen prefix opens on a separator/gap entry");

    // I5 永远不要冻结 可变 流式传输 返回时
    if (stream_active && !m.d.current.messages.empty())
        INV(f.frozen_through < m.d.current.messages.size(), "I5",
            "froze the mutable streaming back message");

    // I6 修剪 Cmd 合同。 -trim (删除 OLDEST 冻结条目)
    // 是顶端 -DELETION. maya's 渲染-time 转动调节( 微调)
    // scrollback_prefix_matches + 承付溢额+ 案件
    // -B 软 重绘) 执行 调和它,但只有在主机没有安全 OVER-commit:
    // 承诺 MORE 行数 大于 maya 物理卷轴的下一位 帧's
    // content_rows 超过 prev_rows ,所以,我说 maya
    // 读起来像成长 并滚动静物 -visible 末尾 变成本地 回滚缓冲区
    // = 重复带。提交 FEWER 合同是: none() 当一切没有掉落的时候,
    // OR a CommitScrollback 算得上保守派的吗?
    // UNDER-estimate (已删除)。 maya 字节 (超过)
    // -commit 恰好是C型的哑巴 -actual 行;
    // 下边 -commit 由清净的和镜子映射 midrun_wire_test.
    // CommitScrollbackOverflow 永远无效( 它 前进 ) prev_rows
    // 过什么 maya 以为溢出, 结束了 -commits).
    if (trim_cmd) {
        const bool is_none =
            std::holds_alternative<agentty::Cmd::None>(trim_cmd->inner);
        const auto* commit =
            std::get_if<maya::CommitScrollback>(&trim_cmd->inner);
        INV(is_none || commit != nullptr, "I6",
            "trim returned an unexpected Cmd type — must be none() or a "
            "conservative commit_scrollback(N) (never CommitScrollbackOverflow).");
        if (commit) {
            // 永远 OVER-commit : 执行的行数比裁剪的线条多
            // 重复可见的线条 末尾 下调 -commit 安全( 3)
            // maya 调和它,所以约束是\\\\ n不是\\\。
            INV(static_cast<std::size_t>(std::max(0, commit->debt.rows()))
                    <= expected_dropped_rows,
                "I6",
                "trim's commit_scrollback row count > rows actually "
                "dropped (OVER-commit strands a duplicate band).");
            // 并且必须承诺 SOMETHING 当它掉下行( 正下方的绑定),
            // 所以是一个真正的顶端 -deletion 永远不能完全不承诺
            // -via-overflow-misread.
            INV(commit->debt.rows() >= 1, "I6",
                "trim dropped rows but committed 0 — expected a positive "
                "conservative count.");
        } else {
            // none() QQ 什么都没有掉。
            INV(expected_dropped_rows == 0, "I6",
                "trim dropped rows but returned none() — the deletion is "
                "uncommitted, maya will re-emit the shifted prefix over "
                "committed scrollback.");
        }
    }
}

// 渲染 整个谈话到一个 BOUNDED 画布。
// 这只是检查一下,没有操作产生模型 maya 无法列出
// (此处抛出=硬回归)——结构 不变量 检查时要单独检查,
// 不需要完整 -height 画画,画画高度很高,
// 写个记录,写得很多高 bash/write 牌不使
// 每烟 渲染 O(千行经).
static void render_smoke(const Model& m, int width) {
    auto root = maya::AppLayout{{
        .thread        = agentty::ui::thread_config(m),
        .changes_strip = agentty::ui::changes_strip_config(m),
        .composer      = agentty::ui::composer_config(m),
        .status_bar    = agentty::ui::status_bar_config(m),
        .overlay       = std::nullopt,
    }}.build();
    maya::StylePool pool;
    maya::Canvas canvas(width, 600, &pool);
    canvas.clear();
    maya::render_tree(root, canvas, pool, maya::theme::native, true);
}

// ── 随便走走 ───────────────────────────────────────────────────────
static void run_walk(std::uint64_t seed, int width) {
    g_seed = seed;
    g_oplog.clear();
    Rng rng(seed);

    Model m;
    m.d.current.id = agentty::ThreadId{"fuzz"};
    agentty::app::detail::clear_frozen(m);
    bool stream_active = false;

    const int steps = 20 + rng.below(40);
    for (int step = 0; step < steps; ++step) {
        // 实际应用中的流式传输 末尾是ALWAYS messages.back():
        // 事情不会被推到它上面 直到它解决。 Model 忠实
        // 的 -- -- 如果 数据流 正在激活,在进行新的推动之前,
        // 唯一允许的谈话突变是解决它。 (一个通过活的推动的
        // 绳子) 末尾 制造一种 I5 "violation"
        // 生产中不可能发生这种情况。 )
        if (stream_active && rng.chance(60)) {
            if (!m.d.current.messages.empty()) {
                auto& back = m.d.current.messages.back();
                if (!back.streaming_text.empty()) {
                    back.text = back.streaming_text;
                    back.streaming_text.clear();
                }
            }
            stream_active = false;
            m.s.phase = agentty::phase::Idle{};
            g_oplog.push_back("settle stream -> idle");
        }

        // ── 调换对话 ──
        // 跳过增长时的 a 数据流 活动( 活动) 末尾 拥有后座).
        const int grow = stream_active ? -1 : rng.below(6);
        if (grow < 0) {
            g_oplog.push_back("(stream active — no push this step)");
        } else if (grow == 0) {
            Message u; u.role = Role::User;
            u.text = "request " + std::to_string(step);
            m.d.current.messages.push_back(std::move(u));
            g_oplog.push_back("push user");
        } else if (grow == 1) {
            Message a; a.role = Role::Assistant;
            a.text = "reply " + std::to_string(step);
            m.d.current.messages.push_back(std::move(a));
            g_oplog.push_back("push assistant-text");
        } else if (grow == 2) {
            Message a; a.role = Role::Assistant;
            a.tool_calls.push_back(
                settled_edit("e" + std::to_string(step), 2 + rng.below(8)));
            m.d.current.messages.push_back(std::move(a));
            g_oplog.push_back("push assistant-edit");
        } else if (grow == 3) {
            Message a; a.role = Role::Assistant;
            a.tool_calls.push_back(
                settled_write("w" + std::to_string(step), 20 + rng.below(120)));
            m.d.current.messages.push_back(std::move(a));
            g_oplog.push_back("push assistant-write");
        } else if (grow == 4) {
            Message a; a.role = Role::Assistant;
            a.tool_calls.push_back(
                settled_bash("b" + std::to_string(step), 50 + rng.below(300)));
            m.d.current.messages.push_back(std::move(a));
            g_oplog.push_back("push assistant-bash");
        } else {
            // 流式传输 末尾(活跃)
            Message t; t.role = Role::Assistant;
            t.streaming_text = "continuing to work ...";
            m.d.current.messages.push_back(std::move(t));
            stream_active = true;
            m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};
            g_oplog.push_back("push streaming-tail (active)");
        }

        // ── 选择一个冻结的-subsystem操作 ──
        agentty::Cmd trim_cmd = agentty::Cmd::none();
        const agentty::Cmd* trim_ptr = nullptr;
        std::size_t dropped_rows = 0;

        const int op = rng.below(7);
        switch (op) {
        case 0: {
            // freeze_through 最多为随机 live_start (声响).
            std::size_t n = m.d.current.messages.size();
            std::size_t live_start = n == 0 ? 0 : rng.below(static_cast<int>(n) + 1);
            // 永远不要冻到 可变 活动时返回 数据流.
            if (stream_active && live_start > n - 1) live_start = n - 1;
            agentty::app::detail::freeze_through(m, live_start);
            g_oplog.push_back("freeze_through(" + std::to_string(live_start) + ")");
            break;
        }
        case 1:
        case 2:
            // (退休行动) -stream 木雕 -- -- freeze_settled_subturns
            // 财务报告和已审计财务报表 freeze_streaming_text_prefix
            // ——删除;生产有完全一个冻结地点(Settle)。
            // freeze_through。保持病例数,使种子在各种版本之间保持可比性。
            g_oplog.push_back("(carve op retired — no-op)");
            break;
        case 3:
        case 4: {
            // 模拟已实现 帧 剪切前:分类账的可复制
            // 性门只滴出有油漆记录的高度的块,
            // 而这个绳子从不运行 maya 真正的油漆传票,
            // 每个街区的策略高度作为记录的高度—
            // —准确的分类账 -tagged 油漆
            // 和铺设的 -out 生产高度。
            for (std::size_t k = 0; k < m.ui.frozen.size(); ++k)
                m.ui.frozen.record_paint(
                    k, static_cast<int>(m.ui.frozen.block_rows(k)));
            std::size_t before = m.ui.frozen.row_total();
            trim_cmd = agentty::app::detail::trim_frozen_if_oversized(m);
            dropped_rows = before - m.ui.frozen.row_total();
            trim_ptr = &trim_cmd;
            g_oplog.push_back("trim_frozen_if_oversized (dropped "
                              + std::to_string(dropped_rows) + " rows)");
            break;
        }
        case 5:
            // 水合物是闲置的 -only 生产业务 线程 切换/ 加载 )——
            // 永远不要中途开火. -stream。在活动时跳过它,这样我们就不会
            // 制造出不可能的 "froze the live back" 州立大学.
            if (stream_active) {
                g_oplog.push_back("(rehydrate skipped — stream active)");
                break;
            }
            agentty::app::detail::rehydrate_frozen(m);
            // 整个记录稿的再水化原因;
            // 活性的标志必须反映之后的真实阶段.
            stream_active = std::holds_alternative<agentty::phase::Streaming>(m.s.phase);
            g_oplog.push_back("rehydrate_frozen");
            break;
        default:
            g_oplog.push_back("(no frozen op this step)");
            break;
        }

        check_invariants(m, stream_active, trim_ptr, dropped_rows);

        // 烟雾: maya 一定能不扔掉就把模型扔出去
        // 抽样(不是每个步骤)—— 不变量 电池的上方是真实覆盖;
        // 渲染 只是一个 crash/throw 手
        // 提琴和高高的笔录彩绘是绳索的主要成本.
        if (rng.chance(20)) {
            try {
                render_smoke(m, width);
            } catch (const std::exception& e) {
                fail("RENDER", e.what());
            } catch (...) {
                fail("RENDER", "non-std exception during render");
            }
        }

        if (g_failures) return;  // 停止第一次失败, 以找到一个干净的追踪
    }
}

}  // 名称空间( 归并)

int main() {
    std::printf("frozen_invariant_fuzz\n");

    const int widths[] = {60, 80, 100, 140};
    const int walks_per_width = 120;

    for (int wi = 0; wi < 4 && !g_failures; ++wi) {
        for (int k = 0; k < walks_per_width && !g_failures; ++k) {
            std::uint64_t seed = 0xA11CE5ULL
                               + static_cast<std::uint64_t>(wi) * 1'000'003ULL
                               + static_cast<std::uint64_t>(k) * 2'654'435'761ULL;
            run_walk(seed, widths[wi]);
        }
    }

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    if (g_failures) { std::printf("FAILED\n"); return 1; }
    std::printf("PASSED\n");
    return 0;
}
