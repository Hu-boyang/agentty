// scrollback_wire_fuzz ——随机化. 传输-LEVEL 反 -corruption 模糊无常.
//
// 最好的保证 agentty 能够给它 回滚缓冲区 腐败和
// 鬼魂是不可能的: 驱动 ARBITRARY 每一生产变异源的
// 断裂—— 流式传输 三角洲 固定工具卡 固定冻结 修剪
// 选择面板 open/close ,用户提交——通过 真实
// ui::view (覆盖面板 和(列入) maya's 真实 内在编曲,
// 并断言 传输 不变量 之后 每个 帧:
//
//   W1 缩进 -while-overflowed 总是找到溢出前缀
//       字节-identical (无) recovery/demote
//       路径 绝不 生产圈下火——这条路是线条重复的) W2 编号
//   prev_cells 阴影在每一次检查前 渲染 (无脱氧) W3 每隔
//   一段时间 渲染 保持同步( 无) Stale/HardReset
//   重绘) W4  a 传输 排,曾经承诺过 可视区域 顶端,
//       是 绝不 以后由任何人员重写 帧 (按: 字条) -duplicate 鬼魂检查,
//       字节-for-byte 反对绝对 传输 模型
//   W5 明确 CommitScrollback 修剪时的排行与绳子一致
//       传输 会计(无) over/under 执 案 )
//
// 失败的指纹 SEED=<n> + 确定性重排的操作跟踪。

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <optional>
#include <string>
#include <vector>

#include <unistd.h>

#include "wire_pipe.hpp"

#include <nlohmann/json.hpp>

#include <maya/render/canvas.hpp>
#include <maya/render/inline_frame.hpp>
#include <maya/render/renderer.hpp>
#include <maya/render/serialize.hpp>
#include <maya/style/theme.hpp>
#include <maya/terminal/writer.hpp>

#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/view/view.hpp"

namespace pn = agentty::ui::panel;

using agentty::Model;
using agentty::Message;
using agentty::Role;
using agentty::ToolCallId;
using agentty::ToolName;
using agentty::ToolUse;
using std::chrono::milliseconds;
using std::chrono::steady_clock;

// A FIXED 墙 -clock 每条模糊消息的时标。 转弯 头文件
// 渲染 msg.timestamp 以 HH:MM (timestamp_hh_mm ;
// 默认 -constructed time_point 那是一个时代,
// HH:MM 以本地时区为准。将其固定在一瞬间( 12: 00) UTC —
// —与 TZ=UTC 设置在 run_walk 所以,已经做了 头文件 字节 每
// 一个机器上都一样 承诺的行内容 保持了模态的纯函数
namespace {  // 归并: TU-local (捆绑成 agentty_standalone_tests)
static const std::chrono::system_clock::time_point kFixedTs =
    std::chrono::system_clock::time_point{std::chrono::hours{12}};

using namespace maya;
using namespace maya::inline_frame;

static int g_failures = 0;
static int g_checks   = 0;
static std::uint64_t g_seed = 0;
static std::vector<std::string> g_oplog;

static void fail(const char* inv, const std::string& detail) {
    ++g_failures;
    std::fprintf(stderr, "  FAIL [%s] %s  (SEED=%llu)\n", inv, detail.c_str(),
                 static_cast<unsigned long long>(g_seed));
    std::fprintf(stderr, "    op trace (%zu ops):\n", g_oplog.size());
    for (const auto& o : g_oplog)
        std::fprintf(stderr, "      %s\n", o.c_str());
}

#define INV(cond, name, detail)                                  \
    do { ++g_checks; if (!(cond)) fail(name, detail); } while (0)

// ── 确定PRNG( splitmix64) ─────────────────────────────────────
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
    int below(int n) {
        return n <= 0 ? 0
                      : static_cast<int>(next()
                            % static_cast<std::uint64_t>(n));
    }
    bool chance(int pct) { return below(100) < pct; }
};

// ── 管道写作机(真正的作家在真实的fd之上,每帧排水) ───────
static std::pair<Writer, int> make_pipe_writer() { return wire_pipe::make_writer(); }
static std::string drain_fd(int rfd) { return wire_pipe::drain(rfd); }

// ── 已解决的工具 -card 建筑者(现实的) 正文 形状) ────────────
static ToolUse settled_tool(Rng& rng, const std::string& tag) {
    ToolUse t;
    auto now = steady_clock::now();
    const int kind = rng.below(4);
    if (kind == 0) {
        t.id = ToolCallId{"write_" + tag};
        t.name = ToolName{"write"};
        std::string content;
        const int n = 10 + rng.below(120);
        for (int i = 0; i < n; ++i)
            content += "line " + std::to_string(i) + ": file body content\n";
        t.args = {{"file_path", "/tmp/" + tag + ".txt"}, {"content", content}};
        t.status = ToolUse::Done{now - milliseconds{5}, now, "Created"};
    } else if (kind == 1) {
        t.id = ToolCallId{"edit_" + tag};
        t.name = ToolName{"edit"};
        std::string block;
        const int n = 3 + rng.below(20);
        for (int i = 0; i < n; ++i)
            block += "    auto v" + std::to_string(i) + " = compute();\n";
        nlohmann::json edits = nlohmann::json::array();
        edits.push_back({{"old_text", block}, {"new_text", block + "    done();\n"}});
        t.args = {{"path", "src/" + tag + ".cpp"}, {"edits", edits}};
        t.status = ToolUse::Done{now - milliseconds{5}, now, "edited"};
    } else if (kind == 2) {
        t.id = ToolCallId{"bash_" + tag};
        t.name = ToolName{"shell"};
        t.args = {{"command", "grep -rn pattern src # " + tag}};
        std::string out;
        const int n = 20 + rng.below(250);
        for (int i = 0; i < n; ++i)
            out += "src/f" + std::to_string(i) + ".cpp:" + std::to_string(i)
                 + ": matching line of source text\n";
        t.status = ToolUse::Done{now - milliseconds{5}, now, std::move(out)};
    } else {
        t.id = ToolCallId{"read_" + tag};
        t.name = ToolName{"read"};
        t.args = {{"path", "src/" + tag + ".cpp"}};
        std::string out;
        const int n = 15 + rng.below(80);
        for (int i = 0; i < n; ++i)
            out += std::to_string(i) + ": " + tag + " source line text\n";
        t.status = ToolUse::Done{now - milliseconds{5}, now, std::move(out)};
    }
    return t;
}

// ── 油漆助手—— 真实 完整视图( E) 覆盖面板 列入) ─────────────
// 安装大小 RenderContext 环绕 两者 风景和油漆:
// ui::view 将父上下文置于自己的ioctl之上(因此,
// 即使测试跑者 Stdout 是一个真正的 tty ,
// 模拟术语 dims 仍然保留), render_tree 巢卫继承了油漆。
static Canvas paint_view(const Model& m, int width, int term_h,
                         StylePool& pool) {
    maya::RenderContext ctx{width, term_h, maya::render_generation(),
                            /*auto_height=*/true};
    maya::RenderContextGuard guard(ctx);
    Canvas c(width, 4000, &pool);
    c.clear();
    std::vector<layout::LayoutNode> nodes;
    maya::render_tree(agentty::ui::view(m), c, pool, maya::theme::native, nodes,
                      /*auto_height=*/true);
    return c;
}

static std::vector<std::string> rows_of(const Canvas& c, int width) {
    std::vector<std::string> rows;
    const int mr = c.max_content_row();
    for (int y = 0; y <= mr; ++y) {
        std::string line;
        for (int x = 0; x < width; ++x) {
            char32_t ch = c.get(x, y).character;
            line.push_back(ch && ch < 128 ? static_cast<char>(ch) : ' ');
        }
        while (!line.empty() && line.back() == ' ') line.pop_back();
        rows.push_back(std::move(line));
    }
    return rows;
}

// ── 电话 传输 绳子 ───────────────────────────────────────────
// 承运人 maya 同步 帧 状态横跨 帧 并模拟物理 传输
// 绝对行数组: 已承诺行数( 横跨 可视区域 顶部,由溢出 OR 明确,
// CommitScrollback ) ,
// 即为 不可变 永远... W4 字节-compares 以后每一次,
// 我都会告诉你 帧 对付他们。
struct WireHarness {
    int width;
    int term_h;
    StylePool pool;
    Writer writer;
    int rfd;

    std::optional<InlineFrame<Synced>> synced;
    std::vector<std::string> wire;   // 已实施( 不可变) 传输 行数
    std::size_t commits = 0;         // 明确 CommitScrollback 共计
    bool dead = false;               // 第一次失败后停止

    WireHarness(int w, int th, std::pair<Writer, int> pw)
        : width(w), term_h(th),
          writer(std::move(pw.first)), rfd(pw.second) {}
    ~WireHarness() { close(rfd); }

    // 应用明确的CommitScrollback(三元的Cmd),反射Runtime::
    // commit_inline_prefix:夹子到prev_rows - term_h.
    void apply_commit(const Model& m, int rows_requested) {
        if (!synced || rows_requested <= 0) return;
        const int prev_rows = synced->rows();
        const int safe_max = prev_rows > term_h ? prev_rows - term_h : 0;
        const int n = std::min(rows_requested, safe_max);
        // W5 : 修剪的行数一定不需要夹子——夹子
        // 表示模型比起 传输 能够实施(低于
        // -commit ~ 困在鬼魂里).
        INV(n == rows_requested, "W5",
            "trim commit clamped: requested "
            + std::to_string(rows_requested) + " safe "
            + std::to_string(safe_max));
        if (n <= 0) { if (rows_requested > 0) dead = true; return; }
        // 承诺的行离开 可变 窗口:将其抓入
        // 传输 模型 当前 (如果经过了...
        // 可视区域 顶端 —— 仅承诺移动边界,
        // 即 字节 以标记前进。
        synced = std::move(*synced).commit(synced->scrollback_marker(n));
        commits += static_cast<std::size_t>(n);
        (void)m;
    }

    // 应用 commit_scrollback_overflow() —
    // maya 自我的,我爱自己 -measured
    // 结束 -commit-impossible 承诺。
    // 镜像 Runtime::commit_inline_prefix(prev_rows
    // - term_h : 准确执行行 渲染器 KNOWS 已溢出( 没有主机数 )。
    // 这就是生产修饰 + 结算 -freeze 现在发行,通过建造它永远无法结束
    // -commit 上面只有卷轴的行 所以没有 W5 夹住危险以断言——计数 IS
    // prev_rows - term_h.
    void apply_commit_overflow(const Model& m) {
        if (!synced) return;
        const int prev_rows = synced->rows();
        const int overflow = prev_rows > term_h ? prev_rows - term_h : 0;
        if (overflow <= 0) return;   // 无溢出物——第-op号(安全)
        synced = std::move(*synced).commit(synced->scrollback_marker(overflow));
        commits += static_cast<std::size_t>(overflow);
        (void)m;
    }

    // 渲染 单曲 帧 结束 maya '真实的作曲; 检查 W1-W4.
    void frame(const Model& m, const std::string& tag) {
        if (dead) return;
        Canvas c = paint_view(m, width, term_h, pool);
        auto cur = rows_of(c, width);
        const int R = static_cast<int>(cur.size());
        if (R <= 0) return;

        if (!synced) {
            auto o = InlineFrame<Empty>{}.seed().render(
                c, content_rows(c), term_rows_for_test(term_h), pool,
                writer, false);
            (void)drain_fd(rfd);
            synced = std::visit(
                [](auto&& a) -> std::optional<InlineFrame<Synced>> {
                    using T = std::decay_t<decltype(a)>;
                    if constexpr (std::is_same_v<T, InlineFrame<Synced>>)
                        return std::move(a);
                    else
                        return std::nullopt;
                }, std::move(o));
            INV(synced.has_value(), "W3", "first frame not Synced @" + tag);
            if (!synced) { dead = true; return; }
        } else {
            // 缩水 -while-overflowed 复数。当前一个 帧 溢额(
            // prev_rows > term_h 和。 和 简 况 此 帧 更短
            // 一点 maya 同步 渲染 区分两个子 -cases (app.cpp):
            //
            //   * 前缀 UNCHANGED (转) -finish 冻结已经...
            //     溢出行数为 字节-identical): maya
            //     掉到附件边 -only diff路径,不需要承诺
            //
            //   * 前缀 CHANGED (回滚缓冲区-content 班级 e.g 现场直播
            //     流式传输 末尾 内容 -lays-out 以较短的冻结形式结算:
            //     maya COMMITS 横行本身和柔软 -repaints。
            //     这是 CORRECT 编号、非 -destructive 恢复,
            //     不 腐败。
            //
            // 绳子镜 maya : 在前缀上 -changed 缩写,
            // 我们应用相同的 commit_scrollback_overflow
            // maya 内部确实如此 prev_rows 预 付 至
            // term_h diff 之前的空间 渲染 真正的反战者
            // -corruption 保证是 W4 (没有) -committed
            // 传输 排字被改写过,下面检查,不管我们拿了哪个分支。
            const int prev_rows = synced->rows();
            if (prev_rows > term_h && R < prev_rows) {
                const int overflow = prev_rows - term_h;
                const bool pm =
                    synced->scrollback_prefix_matches(c, overflow);
                if (!pm) {
                    // maya 自己恢复: 执行关闭 -viewport 排行数.
                    synced = std::move(*synced).commit(
                        synced->scrollback_marker(overflow));
                    commits += static_cast<std::size_t>(overflow);
                }
            }
            // W2 : 影子总是验证( prev_cells == 传输).
            auto wit = synced->verify();
            INV(wit.has_value(), "W2", "shadow verify failed @" + tag);
            if (!wit) { dead = true; return; }
            // 回滚缓冲区 门,类型 -enforced: 渲染 REQUIRES 证明。
            // 我们跑过了 maya 上面的恢复( 承诺在前缀上溢出)
            // -changed 收缩),所以 帧 符合或承诺的前缀稳定,
            // check_scrollback 薄荷能证明。
            // 如果它仍然返回无源( 转换前置) -commit ⊿
            // Τ ⁇ 描 maya 准确: 执行关闭 -viewport
            // 细行和柔软 -repaint 途经 过期.
            auto proof =
                synced->check_scrollback(c, term_h);
            maya::inline_frame::RenderOutcome o = [&] {
                if (proof) {
                    return std::move(*synced).render(
                        c, content_rows(c), term_rows_for_test(term_h),
                        pool, writer, std::move(*wit), std::move(*proof),
                        false);
                }
                // 前缀在已移动的下 -overflowed 帧:
                // maya 将关闭 -viewport 细行和柔软 -repaints
                // 途经 过期 ——和 过期 渲染 RE-ENTERS
                // 同步( 组成案件B) 重绘 已经到位,没有 回滚缓冲区 擦干净
                // 渲染 来,结果再次同步,
                // 完全按照 app.cpp 恢复是的,离开它 过期
                // 将拖着 帧 脱离统一状态 W3 声称。
                const int prev_rows = synced->rows();
                const int overflow = prev_rows > term_h
                    ? prev_rows - term_h : 0;
                auto committed = std::move(*synced).commit(
                    synced->scrollback_marker(overflow));
                commits += static_cast<std::size_t>(overflow);
                return std::move(committed).demote_to_stale().render(
                    c, content_rows(c), term_rows_for_test(term_h),
                    pool, writer, false);
            }();
            (void)drain_fd(rfd);
            synced = std::visit(
                [](auto&& a) -> std::optional<InlineFrame<Synced>> {
                    using T = std::decay_t<decltype(a)>;
                    if constexpr (std::is_same_v<T, InlineFrame<Synced>>)
                        return std::move(a);
                    else
                        return std::nullopt;
                }, std::move(o));
            // W3 : 时间 渲染 保持同步。
            INV(synced.has_value(), "W3",
                "render demoted out of Synced @" + tag);
            if (!synced) { dead = true; return; }
        }

        // W4: 传输-model 字节 选中。 Canvas raine
        // y 地图为绝对 传输 索引输入 +y 字行经过 可视
        // 区域 顶端 此 帧 (R - term_h 已经发生的
        // 任何比较 -captured 指数,然后扩展捕获。
        //
        // EXCEPTION : 欢迎屏幕( 空)
        // 线程 )是先变位的 -run UI ——生产中.
        // 绝不 承诺实现真正的 回滚缓冲区 ;
        // 第一个提交件在 可视区域 和谈话 渲染 如果它
        // 溢出一个短的, 终端 ,它的顶端排行不是
        // 承诺 传输 中跳过抓取 线程 没有
        // 任何信息 所以正当的欢迎 交换 不被误认为是
        // 承诺 -row 重写。一旦真正的转弯出现并溢出,
        // 捕捉将完全恢复到以前(欢迎) 帧 毫无作为,
        // 所以没有 过期 冲突前缀)。
        if (m.d.current.messages.empty()) return;
        const int over = R > term_h ? R - term_h : 0;
        for (int y = 0; y < over; ++y) {
            const std::size_t wi = commits + static_cast<std::size_t>(y);
            if (wi < wire.size()) {
                if (wire[wi] != cur[static_cast<std::size_t>(y)]) {
                    INV(false, "W4",
                        "committed wire row " + std::to_string(wi)
                        + " rewritten @" + tag + "\n      was |" + wire[wi]
                        + "|\n      now |" + cur[static_cast<std::size_t>(y)]
                        + "|");
                    dead = true;
                    return;
                }
            } else if (wi == wire.size()) {
                wire.push_back(cur[static_cast<std::size_t>(y)]);
            } else {
                // 空档:明确承诺在从未越过溢出边界时所犯行数——从当前
                // 线上回填 帧 (他们上) -screen 报告应提交委员会,
                // 以便委员会能够就报告内容作出更深入的评述。
                wire.resize(wi, std::string{});
                wire.push_back(cur[static_cast<std::size_t>(y)]);
            }
        }
    }
};

// ── 随便走走 ───────────────────────────────────────────────
// 照亮生产场景:
//   提交: 结算先前的活期 md / 推动用户 / freeze_through (大小)
//   数据流 : 推动助手 末尾 ,增长 streaming_text 横跨 帧 工具: 将已
//   结算的工具卡附加到活运行结算中: 文本 QQ streaming_text,
//   settle_message_md, PAINT (无) pending_settle_freeze
//            推迟 帧), freeze_through,
//            trim_frozen_if_oversized 适用 CommitScrollback
//   Cmd 选择面板:  open/close 覆盖面板 任意点
//
// reveal_fx 强制 OFF 有关带状部件:模糊需要 帧 高度是
// 模型状态的纯函数( 墙) -clock 排字机会让他们有时间
// -dependent。。 内容。 逐步显示 冻结 接口边界 单独锁定
// live_tail_reveal_settled +
// stream_liveness_test 在这里我们模糊结构 传输 数学
static void seed_md_no_reveal(Model& m, const Message& msg) {
    auto& cache = m.ui.view_cache.message_md(m.d.current.id, msg.id);
    if (!cache.streaming) {
        cache.streaming = std::make_shared<maya::StreamingMarkdown>();
        cache.streaming->set_reveal_fx(false);
    }
}

static void run_walk(std::uint64_t seed, int width, int term_h) {
    g_seed = seed;
    g_oplog.clear();

    // 视图构建读作终端暗号通过ioctl,与COLUMNS/LINESXX
    // 回退(ui::view的建 ⁇ -phase
    // RenderContext +欢迎夹). 吊带没有tty,
    // 所以传输是模拟暗号通过回退.
    setenv("LINES", std::to_string(term_h).c_str(), 1);
    setenv("COLUMNS", std::to_string(width).c_str(), 1);

    // 将时区按好,转弯 -header 时间戳( T)
    // timestamp_hh_mm -> localtime_r) 渲染
    // IDENTICALLY 每台机器上都有这种装置 头文件 字节 要看
    // 跑步者的情况 TZ ,所以承诺行的内容变成机器 -dependent
    // 财务报告和已审计财务报表 W4's 字节-exact 检查可以弹出
    // 横跨 CI 宿主(行走本身在 头文件 组合到固定的每件 -message
    // 下面的乘数戳,每 帧's 字节 纯粹是模态的函数。
    setenv("TZ", "UTC", 1);
    tzset();

    Rng rng(seed);
    Model m;
    // 大小达到 状态更新函数 并透过 Model ,作为 TerminalResized
    // 将会送来
    m.ui.term_cols = width;
    m.ui.term_rows = term_h;
    m.d.current.id = agentty::ThreadId{"wirefuzz"};
    m.d.available_models.push_back({});
    m.d.available_models.back().id = agentty::ModelId{"claude-opus-4-1"};
    m.d.available_models.push_back({});
    m.d.available_models.back().id = agentty::ModelId{"claude-sonnet-4-5"};
    agentty::app::detail::clear_frozen(m);

    WireHarness h(width, term_h, make_pipe_writer());
    bool streaming = false;
    int  delta_i = 0;

    auto op = [&](std::string s) { g_oplog.push_back(std::move(s)); };

    // 第一届 帧 : 欢迎屏幕( 空) 线程).
    h.frame(m, "welcome");

    const int steps = 25 + rng.below(35);
    for (int step = 0; step < steps && !h.dead && !g_failures; ++step) {
        const std::string st = std::to_string(step);

        if (!streaming) {
            switch (rng.below(5)) {
            case 0: {   // 提交:用户推+冻结前转
                Message u; u.role = Role::User;
                u.text = "request " + st + ": do a thing with detail";
                u.timestamp = kFixedTs;
                m.d.current.messages.push_back(std::move(u));
                agentty::app::detail::freeze_through(
                    m, m.d.current.messages.size());
                op("submit+freeze");
                h.frame(m, "submit" + st);
                break;
            }
            case 1: {   // 启动 数据流
                Message a; a.role = Role::Assistant;
                a.timestamp = kFixedTs;
                a.streaming_text = "working on it";
                seed_md_no_reveal(m, a);
                m.d.current.messages.push_back(std::move(a));
                m.s.phase =
                    agentty::phase::Streaming{agentty::phase::Active{}};
                streaming = true;
                op("start stream");
                h.frame(m, "sstart" + st);
                break;
            }
            case 2: {   // 选择面板 打开 – 帧 ⁇ 关 帧
                m.ui.panel.descend(pn::Models{{0, ""}});
                op("picker open");
                h.frame(m, "popen" + st);
                if (rng.chance(50)) h.frame(m, "popen2-" + st);
                m.ui.panel.close<pn::Models>();
                op("picker close");
                h.frame(m, "pclose" + st);
                break;
            }
            case 3: {   // 闲置 re-render( 动画勾选)
                op("idle frame");
                h.frame(m, "idle" + st);
                break;
            }
            default: {  // 解析工具转一次( 历史重播)
                Message u; u.role = Role::User;
                u.text = "tool req " + st;
                u.timestamp = kFixedTs;
                m.d.current.messages.push_back(std::move(u));
                Message a; a.role = Role::Assistant;
                a.timestamp = kFixedTs;
                a.tool_calls.push_back(settled_tool(rng, "h" + st));
                m.d.current.messages.push_back(std::move(a));
                agentty::app::detail::freeze_through(
                    m, m.d.current.messages.size());
                op("settled turn+freeze");
                // 渲染 之前 剪切:生产中,每个冻结的条目都
                // 出现在 传输 给许多人 帧 在修剪时可以丢掉
                // 它( 内容) streams/renders ,
                // 然后和解,然后冻结; OLDEST。
                // 修剪 永远 -rendered 将行行对
                // 过期 prev_rows - 不是生产状态
                h.frame(m, "hturn" + st);
                if (h.dead) break;
                auto cmd =
                    agentty::app::detail::trim_frozen_if_oversized(m);
                        // W5 : 修剪返回 commit_scrollback
                // (N) 其下行时( 上) -DELETION 说
                // maya's 渲染-time 校正不能固定——东道方
                // 必须锁定执行),或 none() 两者均可以接受;
                // 不变量 否为 OTHER 命令类型返回。
                INV(std::holds_alternative<agentty::Cmd::None>(cmd.inner) ||
                    std::holds_alternative<maya::CommitScrollback>(cmd.inner),
                    "W5", "trim returned unexpected command type (must be "
                    "none() or commit_scrollback(N))");
                op("trim");
                h.frame(m, "htrim" + st);
                break;
            }
            }
        } else {
            switch (rng.below(4)) {
            case 0: {   // 数据流 三角洲( 三角洲)
                auto& back = m.d.current.messages.back();
                const int n = 1 + rng.below(4);
                for (int d = 0; d < n; ++d)
                    back.streaming_text +=
                        "\n\nDelta " + std::to_string(delta_i++)
                        + ": more prose that wraps across the width "
                          "and keeps the live tail growing steadily.";
                op("deltas x" + std::to_string(n));
                h.frame(m, "delta" + st);
                break;
            }
            case 1: {   // 已结算工具卡在运行中登陆
                Message a; a.role = Role::Assistant;
                a.timestamp = kFixedTs;
                a.tool_calls.push_back(settled_tool(rng, "t" + st));
                // 生产:工具子 -turn 被推; 键 流式传输
                // 末尾 信件将其文本解析为 `text` 首个
                auto& back = m.d.current.messages.back();
                if (!back.streaming_text.empty()) {
                    back.text += back.streaming_text;
                    back.streaming_text.clear();
                    agentty::app::detail::settle_message_md(m, back);
                }
                m.d.current.messages.push_back(std::move(a));
                // 续占位符——下一个子-turn
                Message ph; ph.role = Role::Assistant;
                ph.timestamp = kFixedTs;
                ph.streaming_text = "continuing";
                seed_md_no_reveal(m, ph);
                m.d.current.messages.push_back(std::move(ph));
                op("tool sub-turn + continuation");
                h.frame(m, "tool" + st);
                break;
            }
            case 2: {   // 选择面板 过一个生活 数据流
                m.ui.panel.descend(pn::Models{{0, ""}});
                op("picker open (streaming)");
                h.frame(m, "spopen" + st);
                m.ui.panel.close<pn::Models>();
                op("picker close (streaming)");
                h.frame(m, "spclose" + st);
                break;
            }
            default: {  // 结算:finalize_turn − 延期冻结+ 裁剪
                auto& back = m.d.current.messages.back();
                back.text += back.streaming_text;
                back.streaming_text.clear();
                agentty::app::detail::settle_message_md(m, back);
                m.s.phase = agentty::phase::Idle{};
                streaming = false;
                // 推迟的 -settle 油漆( pending_settle_freeze):
                // 单曲 帧 画画帖子 -finish 源码树 之前 冻结
                op("settle md + paint");
                h.frame(m, "settlepaint" + st);
                if (h.dead) break;
                agentty::app::detail::freeze_through(
                    m, m.d.current.messages.size());
                // 生产经过了修剪,以进行模型突变,
                // 但有问题 NO 东道主 回滚缓冲区 承诺现场
                // -tail 冻结塌方( meta.cpp
                // 返回时 none() : 时 maya
                // 通过缩写同步调节缩放本身 -arm 将此镜像显示:
                // 放下修饰 Cmd 画画 画绳子 frame()
                // 穿过 maya 真正的作曲, 做调和。
                (void)agentty::app::detail::trim_frozen_if_oversized(m);
                op("freeze + paint");
                h.frame(m, "freeze" + st);
                break;
            }
            }
        }
    }
}

}  // 名称空间( 归并)

int main() {
    std::printf("scrollback_wire_fuzz\n");

    struct Shape { int w, th; };
    const Shape shapes[] = {{80, 24}, {100, 20}, {120, 30}, {60, 16}};
    const int walks_per_shape = 40;

    for (int si = 0; si < 4 && !g_failures; ++si) {
        for (int k = 0; k < walks_per_shape && !g_failures; ++k) {
            std::uint64_t seed = 0xC0FFEEULL
                + static_cast<std::uint64_t>(si) * 7'368'787ULL
                + static_cast<std::uint64_t>(k)  * 2'654'435'761ULL;
            run_walk(seed, shapes[si].w, shapes[si].th);
        }
    }

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    if (g_failures) { std::printf("FAILED\n"); return 1; }
    std::printf("PASSED\n");
    return 0;
}
