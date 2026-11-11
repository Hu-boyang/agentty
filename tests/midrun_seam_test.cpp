// midrun_seam_test — 回滚缓冲区-duplication 冻结纪律的
// 回归( agent_session-style: 一个 冻结每转弯,在结算时)。
//
// 真正的症状使用者被击中: maya 内置 diff 处理任何
// 滚动过 可视区域 顶端为 不可变 ——已承诺 终端 本地人 :
// 回滚缓冲区 无法重写 所以 渲染 合同是:一列在画布位置Y发射,
// 一列在位置Y必须 绝不 稍后的变动 帧 (直到一个完全分歧
// 者) 重绘 如果转弯的任何一步——一种工具解决,
// 则 数据流 完成, 解决 -time 冻结——将上方的行
// 移到一行(一个空格) appears/disappears,
// a 头文件 线条 切换 ),每个已经 -committed
// 班底行为 -emitted 旧副本被卡在了一个新的
// 位置上 回滚缓冲区。这就是复制 -turn 鬼魂 鬼魂
// 你该知道鬼魂是谁
//
// 测试模拟生产cadence( sub) -turns 土地在陆地上,
// 整个运行在定居时冻结),并声称已执行的行 PREFIX
// 每一步骤都稳定: 帧 N+1 的行 [0. K] 必须 字
// 节-match 帧 N 的行 [0. K] , K 是屏幕
// 上已经显示的较短的常见前缀。 所承诺前缀中的任何差异
// 都是重复错误。

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include <maya/render/canvas.hpp>
#include <maya/core/anim_clock.hpp>   // maya::testing::freeze_anim_clock
#include <maya/render/renderer.hpp>
#include <maya/style/theme.hpp>
#include <maya/widget/app_layout.hpp>

#include "agtest.hpp"

#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/model.hpp"
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
using std::chrono::steady_clock;
using std::chrono::milliseconds;

static int g_checks   = 0;


static std::string code_block(int n) {
    std::string out;
    for (int i = 0; i < n; ++i)
        out += "    auto x = compute(i) + offset; // plausible line\n";
    return out;
}

static ToolUse settled_edit(const std::string& tag) {
    ToolUse t;
    t.id   = ToolCallId{"edit_" + tag};
    t.name = ToolName{"edit"};
    nlohmann::json edits = nlohmann::json::array();
    edits.push_back({{"old_text", code_block(4)},
                     {"new_text", code_block(4)}});
    t.args = {{"path", "src/" + tag + ".cpp"}, {"edits", edits}};
    auto now = steady_clock::now();
    t.status = ToolUse::Done{now - milliseconds{5}, now, "edited-" + tag};
    return t;
}

// 编辑工具的 diff 载荷 向 tc.output () 中输出, 以 iff 包裹
// 终端状态渲染器解析的 ```diff 围栏，包含 n 个差异块，每块 rows 行。
// 每一行都更改了,够了 正文 横跨 a 可视区域 顶部。
static std::string edit_diff_output(const std::string& tag, int rows) {
    std::string d = "```diff\n";
    d += "--- a/src/" + tag + ".cpp\n";
    d += "+++ b/src/" + tag + ".cpp\n";
    d += "@@ -1," + std::to_string(rows) + " +1," + std::to_string(rows) + " @@\n";
    for (int i = 0; i < rows; ++i) {
        d += "-    auto x = compute(" + std::to_string(i) + "); // was\n";
        d += "+    auto x = compute(" + std::to_string(i) + ") + offset; // now\n";
    }
    d += "```\n";
    return d;
}

// 渲染 编号 CONVERSATION 仅限区域(冻+活) 末尾 每行一行, , ("一行"),
// ("二行") ASCII-folded 空白的痕迹被剥光了
static std::vector<std::string> render_rows(const Model& m,
                                            int width = 100,
                                            int height = 40000) {
    auto root = maya::AppLayout{{
        .thread        = agentty::ui::thread_config(m),
        .changes_strip = agentty::ui::changes_strip_config(m),
        .composer      = agentty::ui::composer_config(m),
        .status_bar    = agentty::ui::status_bar_config(m),
        .overlay       = std::nullopt,
    }}.build();

    maya::StylePool pool;
    maya::Canvas canvas(width, height, &pool);
    canvas.clear();
    maya::render_tree(root, canvas, pool, maya::theme::native, true);

    std::vector<std::string> rows;
    const int max_row = canvas.max_content_row();
    for (int y = 0; y <= max_row; ++y) {
        std::string line;
        for (int x = 0; x < width; ++x) {
            char32_t ch = canvas.get(x, y).character;
            if (ch == 0) ch = U' ';
            line.push_back(ch < 128 ? static_cast<char>(ch) : '?');
        }
        while (!line.empty() && line.back() == ' ') line.pop_back();
        rows.push_back(std::move(line));
    }
    return rows;
}

// 比较两个 帧 下级 maya 内含 -overflow 合同。
// 只有滚动的行 上方 编号 可视区域 顶部是致力于本土 回滚缓冲区 财务
// 报告和已审计财务报表 不可变 打开; 打开 -screen 行( 底部)
// `term_h` )可自由改写 帧-to-frame.
//
// 索引一行 i 英寸 帧 `prev` 如果它已经溢出:我 <
// prev.size() - term_h。对于重复错误,
// 我们只关心这些,如果一个已承诺的行的内容在同一个索引上存在差异的话。
// `cur` ,则 渲染器 将重新 -emit 已经卡在一排
// 下面 回滚缓冲区 返回第一个差异 COMMITTED 行指数,
// 或 -1 如果每个承诺行是稳定的。
static int first_committed_divergence(const std::vector<std::string>& prev,
                                      const std::vector<std::string>& cur,
                                      int term_h) {
    // 行数 `prev` 已经溢出, 可视区域 最后一个 帧.
    const int committed_end =
        static_cast<int>(prev.size()) - term_h;
    const int common =
        std::min<int>(static_cast<int>(cur.size()), committed_end);
    for (int i = 0; i < common; ++i)
        if (prev[static_cast<std::size_t>(i)] != cur[static_cast<std::size_t>(i)])
            return i;
    return -1;
}

// 构建用户 + N 已定的编辑子 -turns 一次一个降落,
// 都留下 LIVE (生产纪律——无中. -run ),
// 然后解决 -freeze 指定已承诺的前缀在连续的
// 间隔中永不改变 快照, INCLUDING 跨越最后的
// 和解 -time 别动
TEST_CASE("incremental freeze prefix stable") {
    constexpr int N = 30;
    // 一个现实 可视区域: maya 保持底部( T)
    // `term_h` 屏幕上排行;上面所有东西都溢到原生的
    // 回滚缓冲区 并且是, 已经 不可变。使用典型的 40。
    // -row 终端。使用 N=30 编辑子 -turns (每行
    // 约6行)画布长得远远超过40行,所以很早的子 -turns
    // 真正承诺 回滚缓冲区 ——正是前缀移位成为可见重复的条件.
    constexpr int kTermH = 40;

    Model m;
    m.d.current.id = agentty::ThreadId{"seam"};
    Message u; u.role = Role::User; u.text = "please do many edits";
    m.d.current.messages.push_back(std::move(u));
    agentty::app::detail::clear_frozen(m);
    agentty::app::detail::freeze_through(m, 1);   // 用户被冻结

    std::vector<std::string> prev;
    int worst_divergence = -1;
    int diverging_step   = -1;

    auto note_divergence = [&](int e, const std::vector<std::string>& p,
                               const std::vector<std::string>& c) {
        int d = first_committed_divergence(p, c, kTermH);
        if (d >= 0 && (worst_divergence < 0 || d < worst_divergence)) {
            worst_divergence = d;
            diverging_step   = e;
            // 抛开FIRST差异的邻里,
            // 这样回归就能够确切地显示哪个行被移动了。
            std::fprintf(stderr,
                "  --- step %d, committed-row divergence at row %d ---\n",
                e, d);
            for (int y = std::max(0, d);
                 y < std::min<int>(d + 3,
                     (int)std::max(p.size(), c.size()));
                 ++y) {
                const char* pv = (y < (int)p.size()) ? p[y].c_str() : "<none>";
                const char* cv = (y < (int)c.size()) ? c[y].c_str() : "<none>";
                std::fprintf(stderr, "    row %2d PREV |%s|\n", y, pv);
                std::fprintf(stderr, "    row %2d CUR  |%s|\n", y, cv);
            }
        }
    };

    for (int e = 0; e < N; ++e) {
        // 已定居的编辑子-turn土地。
        Message a; a.role = Role::Assistant;
        a.tool_calls.push_back(settled_edit("s" + std::to_string(e)));
        m.d.current.messages.push_back(std::move(a));

        // A 流式传输 后续占位符(确切的说,该帖子是什么) -tool
        // 继续推动真实 状态更新函数). NO 中点 -run
        // 冻结——整个运行一直持续到和解.
        Message ph; ph.role = Role::Assistant; ph.streaming_text = "working";
        m.d.current.messages.push_back(std::move(ph));
        m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};

        auto cur = render_rows(m);
        if (!prev.empty()) note_divergence(e, prev, cur);
        prev = std::move(cur);

        // 在下一个真实子之前丢弃占位符 -turn (现场
        // 直播) 末尾 被重建每个周期;下一个编辑消息替换它。
        m.d.current.messages.pop_back();
    }

    // 和解: 运行结束; 单一和解 -time 冻结火灾。
    m.s.phase = agentty::phase::Idle{};
    agentty::app::detail::freeze_through(m, m.d.current.messages.size());
    auto frozen = render_rows(m);
    if (!prev.empty()) note_divergence(N, prev, frozen);

    if (worst_divergence >= 0) {
        std::fprintf(stderr,
            "  committed prefix changed at row %d on step %d "
            "(a scrollback row was rewritten -> duplication)\n",
            worst_divergence, diverging_step);
    }
    CHECK(worst_divergence < 0,
          "committed prefix mutated across the live run / settle freeze");
}

// 该 单人 -tool 过渡以上递增测试 永远不要练习:
// 一个 通过 Run( 仅限参数, 无输出) 编辑流 - > Done(
// diff 栅栏英寸 tc.output () - > 中点 -run 冻结。
// 渲染器 SWITCHES 形状,贯穿此过渡( 3) 流式传输
// EditDiff-from-args ,以斜线表示, - > 终端
// GitDiff-from-fence 满。 接口边界 合约上说: 任何已经
// 溢出 可视区域 顶部 流式传输 必须 渲染 字节-identical 解冻后,
// 或已执行 回滚缓冲区 复制件是孤儿,下面的卡片重复。
//
// 为了实现转换,我们把流式传输编辑工作与已经
// -frozen编辑工作的区块进行三文治(因此,它的早期各行都
// 推到kTermH 可视区域顶端)。然后我们把快照:
//   帧 A: 编辑运行( args 预
//   览) 帧 B: 编辑完成( E) 终端
//   GitDiff 正文) 帧 C:转弯解决——单一解决 -time
// 冻结火,并断言承诺的前缀是稳定的 A->B和B->C。
TEST_CASE("single edit stream to freeze") {
    constexpr int kTermH = 40;

    Model m;
    m.d.current.id = agentty::ThreadId{"single"};
    Message u; u.role = Role::User; u.text = "edit one file";
    m.d.current.messages.push_back(std::move(u));
    agentty::app::detail::clear_frozen(m);
    agentty::app::detail::freeze_through(m, 1);

    // 铅 -in : 运行已解析的编辑。在制作中,此部分的前部分 线程
    // 全部冻结( 全部运行) 终端 — freeze_range's
    // run_is_freezable 门过,所以有很多行坐上 可视
    // 区域 编辑前的顶部 -under-test 甚至出现。
    for (int e = 0; e < 20; ++e) {
        Message a; a.role = Role::Assistant;
        a.tool_calls.push_back(settled_edit("lead" + std::to_string(e)));
        m.d.current.messages.push_back(std::move(a));
    }
    agentty::app::detail::freeze_through(m, m.d.current.messages.size());
    m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};

    const std::string tag = "target";
    constexpr int kRows = 30;

    // ── frame A: the edit is RUNNING. Args carry the hunk; no output yet.
    {
        Message a; a.role = Role::Assistant;
        ToolUse t;
        t.id   = ToolCallId{"edit_" + tag};
        t.name = ToolName{"edit"};
        nlohmann::json edits = nlohmann::json::array();
        // 差异块 文本映射 diff 正文 宽度会排成一行
        std::string ot, nt;
        for (int i = 0; i < kRows; ++i) {
            ot += "    auto x = compute(" + std::to_string(i) + "); // was\n";
            nt += "    auto x = compute(" + std::to_string(i) + ") + offset; // now\n";
        }
        edits.push_back({{"old_text", ot}, {"new_text", nt}});
        t.args = {{"path", "src/" + tag + ".cpp"}, {"edits", edits}};
        t.status = ToolUse::Running{steady_clock::now(), ""};
        a.tool_calls.push_back(std::move(t));
        // 背后是积极的占位符 freeze_settled 继续有效。
        Message ph2; ph2.role = Role::Assistant; ph2.streaming_text = "...";
        m.d.current.messages.push_back(std::move(a));
        m.d.current.messages.push_back(std::move(ph2));
    }
    auto frame_a = render_rows(m);

    // ── frame B: the edit SETTLES. Same args; status -> Done with the
    //    终端渲染器解析的 ```diff 围栏，此处会切换渲染器。
    {
        // 定位现场编辑并翻转到完成。
        agentty::app::detail::with_live_tool(
            m, ToolCallId{"edit_" + tag}, [&](ToolUse& t) {
                auto now = steady_clock::now();
                t.status = ToolUse::Done{now - milliseconds{5}, now,
                                         edit_diff_output(tag, kRows)};
            });
    }
    auto frame_b = render_rows(m);

    // 帧 C：轮次收尾，唯一的收尾冻结操作触发。
    //    (finalize_turn → pending_settle_freeze → freeze_through).
    {
        auto& back = m.d.current.messages.back();
        back.text = std::move(back.streaming_text);
        back.streaming_text.clear();
    }
    m.s.phase = agentty::phase::Idle{};
    agentty::app::detail::freeze_through(m, m.d.current.messages.size());
    auto frame_c = render_rows(m);

    int d_ab = first_committed_divergence(frame_a, frame_b, kTermH);
    int d_bc = first_committed_divergence(frame_b, frame_c, kTermH);

    if (d_ab >= 0) {
        std::fprintf(stderr,
            "  single-edit Running->Done committed-row divergence at %d\n", d_ab);
        for (int y = d_ab; y < std::min<int>(d_ab + 3,
                 (int)std::max(frame_a.size(), frame_b.size())); ++y) {
            std::fprintf(stderr, "    row %2d A |%s|\n", y,
                         y < (int)frame_a.size() ? frame_a[y].c_str() : "<none>");
            std::fprintf(stderr, "    row %2d B |%s|\n", y,
                         y < (int)frame_b.size() ? frame_b[y].c_str() : "<none>");
        }
    }
    if (d_bc >= 0) {
        std::fprintf(stderr,
            "  single-edit Done->freeze committed-row divergence at %d\n", d_bc);
        for (int y = d_bc; y < std::min<int>(d_bc + 3,
                 (int)std::max(frame_b.size(), frame_c.size())); ++y) {
            std::fprintf(stderr, "    row %2d B |%s|\n", y,
                         y < (int)frame_b.size() ? frame_b[y].c_str() : "<none>");
            std::fprintf(stderr, "    row %2d C |%s|\n", y,
                         y < (int)frame_c.size() ? frame_c[y].c_str() : "<none>");
        }
    }

    CHECK(d_ab < 0, "single edit Running->Done rewrote a committed scrollback row");
    CHECK(d_bc < 0, "single edit Done->freeze rewrote a committed scrollback row");
}

// 该 准确截图: a READ 批次( 批次) INSPECT
// 然后是一个 EDIT 批次( 批次) MUTATE ) 中,
// 一个 转过来,读数算满12 -line 正文 运行时仍然在运行。
// 使用冷冻铅 -in 将读数的顶端推到 可视区域 ,读数 正文 下行
// 增长不得 重写任何已承诺的行 MUTATE 批量/继续其下方。
//
// 帧 甲:读 RUNNING (契约,无) 正文 + 编辑其下方的批次 帧 B: 读
// DONE (满 额 正文 )——卡片生长~12行. 帧 中 计: -run
// 冻结火 已执行的前缀必须是 字节-stable
// A->B和B->C. 互联网档案馆的存檔,存档日期2013-09-02.
TEST_CASE("read then edit batch freeze") {
    constexpr int kTermH = 40;

    Model m;
    m.d.current.id = agentty::ThreadId{"readedit"};
    Message u; u.role = Role::User; u.text = "inspect then edit";
    m.d.current.messages.push_back(std::move(u));
    agentty::app::detail::clear_frozen(m);
    agentty::app::detail::freeze_through(m, 1);

    // 铅 -in : 已解冻的编辑器整件( ) 终端
    // )所以读卡的顶部远高于 可视区域 上方才有结果
    for (int e = 0; e < 20; ++e) {
        Message a; a.role = Role::Assistant;
        a.tool_calls.push_back(settled_edit("rlead" + std::to_string(e)));
        m.d.current.messages.push_back(std::move(a));
    }
    agentty::app::detail::freeze_through(m, m.d.current.messages.size());
    m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};

    // INSPECT Sub-turn: a 读作RUNNING(尚未译为正文)。
    {
        Message a; a.role = Role::Assistant;
        ToolUse t;
        t.id   = ToolCallId{"read_target"};
        t.name = ToolName{"read"};
        t.args = {{"path", "src/target.cpp"}};
        t.status = ToolUse::Running{steady_clock::now(), ""};
        a.tool_calls.push_back(std::move(t));
        m.d.current.messages.push_back(std::move(a));
    }
    // 电话 MUTATE 子目录 -turn 已流到读数
    // 以下( 模型按顺序同时释放; 读数结果只是还没有降落)。
    {
        Message a; a.role = Role::Assistant;
        a.tool_calls.push_back(settled_edit("mutate"));
        m.d.current.messages.push_back(std::move(a));
    }
    // 两者背后都活跃着占位符.
    Message ph2; ph2.role = Role::Assistant; ph2.streaming_text = "...";
    m.d.current.messages.push_back(std::move(ph2));
    auto frame_a = render_rows(m);

    // 帧 B: 读 SETTLES —— 满 12 -line 正文 正在出现,
    // 正在成长 INSPECT 牌和移动 MUTATE 下方有批量下来
    agentty::app::detail::with_live_tool(
        m, ToolCallId{"read_target"}, [&](ToolUse& t) {
            auto now = steady_clock::now();
            std::string body;
            for (int i = 0; i < 12; ++i)
                body += "line " + std::to_string(i) + ": target file content\n";
            t.status = ToolUse::Done{now - milliseconds{5}, now, std::move(body)};
        });
    auto frame_b = render_rows(m);

    // 帧 C:转弯——单人解决. -time 冻结火灾。
    {
        auto& back = m.d.current.messages.back();
        back.text = std::move(back.streaming_text);
        back.streaming_text.clear();
    }
    m.s.phase = agentty::phase::Idle{};
    agentty::app::detail::freeze_through(m, m.d.current.messages.size());
    auto frame_c = render_rows(m);

    int d_ab = first_committed_divergence(frame_a, frame_b, kTermH);
    int d_bc = first_committed_divergence(frame_b, frame_c, kTermH);

    auto dump = [&](const char* what, int d,
                    const std::vector<std::string>& p,
                    const std::vector<std::string>& c) {
        if (d < 0) return;
        std::fprintf(stderr, "  read+edit %s committed-row divergence at %d\n",
                     what, d);
        for (int y = d; y < std::min<int>(d + 4,
                 (int)std::max(p.size(), c.size())); ++y) {
            std::fprintf(stderr, "    row %2d P |%s|\n", y,
                         y < (int)p.size() ? p[y].c_str() : "<none>");
            std::fprintf(stderr, "    row %2d C |%s|\n", y,
                         y < (int)c.size() ? c[y].c_str() : "<none>");
        }
    };
    if (d_ab >= 0 || d_bc >= 0)
        std::fprintf(stderr, "  read+edit frames: A=%zu B=%zu C=%zu (kTermH=%d)\n",
                     frame_a.size(), frame_b.size(), frame_c.size(), kTermH);
    dump("Running->Done", d_ab, frame_a, frame_b);
    dump("Done->freeze", d_bc, frame_b, frame_c);

    CHECK(d_ab < 0,
          "read settle grew its body and rewrote a committed row of the "
          "batch below it (the duplicated INSPECT card)");
    CHECK(d_bc < 0, "read+edit Done->freeze rewrote a committed scrollback row");
}

// 该 写入 -tool 转换用户实际点击的:单个 `write`
// 通过运行流( 压缩) 末尾-window 预览, show_all
// =虚假) - > 完成( 完整 正文, show_all =
// real) - > 中间 -run 冻结。 流式传输 预览是小的紧凑卡;
// 结算卡是完整文件( Q) 可视区域-overflowing。
// 用冷冻铅 -in 将牌的顶端排在上方 可视区域 ,
// 任何一个 height/shape 整个结算区的变化 流式传输 复制
// 内容 回滚缓冲区 和下面的定居卡画——用户报告重复的动作框。
TEST_CASE("single write stream to freeze") {
    constexpr int kTermH = 40;

    Model m;
    m.d.current.id = agentty::ThreadId{"write"};
    Message u; u.role = Role::User; u.text = "write a big file";
    m.d.current.messages.push_back(std::move(u));
    agentty::app::detail::clear_frozen(m);
    agentty::app::detail::freeze_through(m, 1);

    // 铅 -in : 已解冻的编辑器整件( ) 终端 因此,
    // 写卡的顶端排位被推到远高于 可视区域 在写完之前,
    // 请在上面
    for (int e = 0; e < 20; ++e) {
        Message a; a.role = Role::Assistant;
        a.tool_calls.push_back(settled_edit("wlead" + std::to_string(e)));
        m.d.current.messages.push_back(std::move(a));
    }
    agentty::app::detail::freeze_through(m, m.d.current.messages.size());
    m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};

    // 文件内容全文( ~ 170行, ) 可视区域
    // -overflowing )——暴露重复的写作的准确形状.
    std::string content;
    for (int i = 0; i < 170; ++i)
        content += "line " + std::to_string(i) + ": some plausible file content\n";

    // 帧 A：write 处于 RUNNING，参数带完整内容，并以流式
    //    以符号表示 -by-token ; 由 终端 时间都到齐了 正文 配置
    //    切片a 末尾 窗口,带有 show_all =false - > 紧凑卡.
    {
        Message a; a.role = Role::Assistant;
        ToolUse t;
        t.id   = ToolCallId{"write_big"};
        t.name = ToolName{"write"};
        t.args = {{"file_path", "/tmp/big.txt"}, {"content", content}};
        t.status = ToolUse::Running{steady_clock::now(), ""};
        a.tool_calls.push_back(std::move(t));
        Message ph2; ph2.role = Role::Assistant; ph2.streaming_text = "...";
        m.d.current.messages.push_back(std::move(a));
        m.d.current.messages.push_back(std::move(ph2));
    }
    auto frame_a = render_rows(m);

    // 帧 B：write 稳定后，show_all 变为 true，显示完整正文。
    {
        agentty::app::detail::with_live_tool(
            m, ToolCallId{"write_big"}, [&](ToolUse& t) {
                auto now = steady_clock::now();
                t.status = ToolUse::Done{now - milliseconds{5}, now,
                                         "Created /tmp/big.txt (170+ 0-)"};
            });
    }
    auto frame_b = render_rows(m);

    // 帧 C：轮次收尾，唯一的收尾冻结操作触发。
    {
        auto& back = m.d.current.messages.back();
        back.text = std::move(back.streaming_text);
        back.streaming_text.clear();
    }
    m.s.phase = agentty::phase::Idle{};
    agentty::app::detail::freeze_through(m, m.d.current.messages.size());
    auto frame_c = render_rows(m);

    // 写下来的 必须 冻结在定居边界上——整个毕业生
    // 变成 m.ui.frozen 立刻来
    {
        const std::size_t live =
            m.d.current.messages.size() - m.ui.frozen_through;
        CHECK(live == 0,
              "settled turn did not freeze whole at the settle boundary");
    }

    // 两者 FROZEN 快照 (frame_c 和 和 数据 LIVE 结算
    // 卡( frame_b 携带 整个 文件 - 字节-identical。
    // 一份已解决的书信 渲染 编号 完整 正文 (show_all 现场直播中 末尾 也
    // 一样 不 只能用冰冻的 快照 :喂养相同 字节 使冻结的状态保持纯净 缓存 击
    // 打( 百分比) -event hash_id 输入 agent_timeline.
    // Cpp公司把高牌做成彩绘 -once 布莱特,所以满 正文
    // 免费的每张 帧 第一次画完后——不需要窗外翻转。
    // 正文 这正是保持承诺的前缀 字节-stable 交接方式(通过
    // first_committed_divergence (见下文)。
    // 保持完整状态 正文 输入 两者 : 最后一个,深 -middle ,并排第一行。
    {
        auto joined = [](const std::vector<std::string>& rows) {
            std::string s;
            for (const auto& r : rows) { s += r; s.push_back('\n'); }
            return s;
        };
        const std::string b = joined(frame_b);
        const std::string c = joined(frame_c);
        CHECK(c.find("line 169: some plausible") != std::string::npos,
              "frozen write snapshot missing the LAST (tail) line");
        CHECK(b.find("line 169: some plausible") != std::string::npos,
              "settled-live write card missing the LAST (tail) line");
        CHECK(c.find("line 85: some plausible") != std::string::npos,
              "frozen write snapshot elided a deep-middle line — settled "
              "write must show the FULL body, not a capped preview");
        CHECK(b.find("line 85: some plausible") != std::string::npos,
              "settled-live write card elided a deep-middle line — settled "
              "write must show the FULL body, not a capped preview");
        CHECK(c.find("line 0: some plausible") != std::string::npos,
              "frozen write snapshot missing the FIRST (head) line");
        CHECK(b.find("line 0: some plausible") != std::string::npos,
              "settled-live write card missing the FIRST (head) line");
    }

    int d_ab = first_committed_divergence(frame_a, frame_b, kTermH);
    int d_bc = first_committed_divergence(frame_b, frame_c, kTermH);

    auto dump = [&](const char* what, int d,
                    const std::vector<std::string>& p,
                    const std::vector<std::string>& c) {
        if (d < 0) return;
        std::fprintf(stderr, "  write %s committed-row divergence at %d\n", what, d);
        for (int y = d; y < std::min<int>(d + 3,
                 (int)std::max(p.size(), c.size())); ++y) {
            std::fprintf(stderr, "    row %2d P |%s|\n", y,
                         y < (int)p.size() ? p[y].c_str() : "<none>");
            std::fprintf(stderr, "    row %2d C |%s|\n", y,
                         y < (int)c.size() ? c[y].c_str() : "<none>");
        }
    };
    if (d_ab >= 0 || d_bc >= 0)
        std::fprintf(stderr, "  write frames: A=%zu B=%zu C=%zu rows (kTermH=%d)\n",
                     frame_a.size(), frame_b.size(), frame_c.size(), kTermH);
    dump("Running->Done", d_ab, frame_a, frame_b);
    dump("Done->freeze", d_bc, frame_b, frame_c);

    CHECK(d_ab < 0, "write Running->Done rewrote a committed scrollback row");
    CHECK(d_bc < 0, "write Done->freeze rewrote a committed scrollback row");
}

// 重新组装全员助理 正文 用户应该从模型的邮件中看到:
// 每个助理邮件都解决了 `text` 加活动 末尾's
// `streaming_text` ,顺序。
// 分裂必须是纯分化 字节 ——不亏不盈,不重订,不重复.
static std::string assembled_body(const Model& m) {
    std::string out;
    for (const auto& msg : m.d.current.messages) {
        if (msg.role != Role::Assistant) continue;
        out += msg.text;
        out += msg.streaming_text;
    }
    return out;
}

// 长 PURE-TEXT 回答 流式传输 内。 agent_session
// 纪律 整个 增长 正文 保持活性(无中位数) -stream 雕刻 -
// maya's StreamingMarkdown 双 -tier 渲染器 保留
// 元素 -tree 高单音调在 字节 数,所以承诺行绝不移动。此测试为 正文
// 区块(每块实数) -tick 245. 律师说,“在法院中,法院有权对被告进行起诉”。
//   (1) 所犯行为 回滚缓冲区 前缀从不变异( 不存在重复),
//   (2) 组装 正文 等于原样 字节 分步走,(3) 解决 -time
//   冻结( 冻结) 仅 冻结)是行 -stable 也一样。
TEST_CASE("streaming text prefix freeze") {
    constexpr int kTermH = 40;

    Model m;
    m.d.current.id = agentty::ThreadId{"stext"};
    Message u; u.role = Role::User; u.text = "explain in detail";
    m.d.current.messages.push_back(std::move(u));
    agentty::app::detail::clear_frozen(m);
    agentty::app::detail::freeze_through(m, 1);

    // 按下活动 流式传输 助理消息。
    Message a; a.role = Role::Assistant;
    m.d.current.messages.push_back(std::move(a));
    m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};

    // 满 正文 模型将产生——偶尔出现
    // 标题和栅栏的代码块的传言。
    // -chunk 满 -body 内容
    // -render 保持测试的速度。
    std::string full;
    for (int i = 0; i < 400; ++i) {
        if (i % 11 == 0) full += "## Heading " + std::to_string(i) + "\n\n";
        if (i % 23 == 0) {
            full += "```cpp\n";
            full += code_block(3);
            full += "```\n\n";
        }
        full += "Sentence number " + std::to_string(i)
             +  " of a long streaming prose answer.\n\n";
    }

    std::vector<std::string> prev;
    int worst_divergence = -1, diverging_step = -1;
    bool content_ok = true;

    constexpr std::size_t kChunk = 1024;
    std::size_t fed = 0, step = 0;
    while (fed < full.size()) {
        const std::size_t n = std::min(kChunk, full.size() - fed);
        m.d.current.messages.back().streaming_text.append(full, fed, n);
        fed += n;
        ++step;

        // (2)内容完整性:编组 == 字节至今已提供.
        if (assembled_body(m) != full.substr(0, fed)) content_ok = false;

        // (1) 已实施 -prefix 稳定。
        auto cur = render_rows(m);
        if (!prev.empty()) {
            int d = first_committed_divergence(prev, cur, kTermH);
            if (d >= 0 && (worst_divergence < 0 || d < worst_divergence)) {
                worst_divergence = d;
                diverging_step   = static_cast<int>(step);
            }
        }
        prev = std::move(cur);
    }

    // 最终结算( E) finalize_turn
    // :  ⁇ 末尾 承诺执行文本和整个冻结
    // ——单一冻结网站。
    {
        auto& last = m.d.current.messages.back();
        last.text += last.streaming_text;
        last.streaming_text.clear();
    }
    m.s.phase = agentty::phase::Idle{};
    agentty::app::detail::freeze_through(m, m.d.current.messages.size());
    auto frozen = render_rows(m);
    if (!prev.empty()) {
        int d = first_committed_divergence(prev, frozen, kTermH);
        if (d >= 0 && (worst_divergence < 0 || d < worst_divergence)) {
            worst_divergence = d;
            diverging_step   = static_cast<int>(step) + 1;
        }
    }

    CHECK(content_ok,
          "assembled body diverged from fed bytes");
    if (worst_divergence >= 0) {
        std::fprintf(stderr,
            "  committed prefix changed at row %d on step %d\n",
            worst_divergence, diverging_step);
    }
    CHECK(worst_divergence < 0,
          "committed prefix mutated while streaming / at settle freeze");
    CHECK(assembled_body(m) == full,
          "final assembled body != original after settle");
}

// (删除) strip_synthetic_fences + run_giant_fence
// + test_giant_fence_prefix_freeze 栅栏 close/reopen
// 回退 备注 freeze_streaming_text_prefix - 机器不见了
// 巨大的单一围栏现在只是停留在生活之中,直到解决(即: agent_session
// 行为),其行为 -prefix 稳定由以下因素所涵盖:
// test_streaming_text_prefix_freeze 上方(包括围栏)。

// 该 已报告的错误形状: 已解析 write/edit 纸牌
// TALLER 超过 可视区域 坐在现场 末尾 直到转弯而和解,
// 然后, -time 冻结将整个运行移动到冻结的前缀。 末尾 财务
// 报告和已审计财务报表 freeze_range 键
// 相同 assistant_run_hash_id ,所以交接必须是
// 纯洁的 缓存 点击 —— 任何横跨冰冻的行移将现场复制件拖入
// 回滚缓冲区 和牌重复。
TEST_CASE("tall card live to frozen seam") {
    constexpr int kTermH = 40;

    Model m;
    m.d.current.id = agentty::ThreadId{"tall"};
    Message u; u.role = Role::User; u.text = "write a big file";
    m.d.current.messages.push_back(std::move(u));
    agentty::app::detail::clear_frozen(m);
    agentty::app::detail::freeze_through(m, 1);   // 用户被冻结

    // 已解决的编辑 正文 谓 FAR 高点比 可视区域.
    Message a; a.role = Role::Assistant;
    {
        ToolUse t;
        t.id   = ToolCallId{"edit_tall"};
        t.name = ToolName{"edit"};
        t.args = {{"path", "src/tall.cpp"}};
        auto now = steady_clock::now();
        t.status = ToolUse::Done{now - milliseconds{5}, now,
                                 edit_diff_output("tall", 120)};
        a.tool_calls.push_back(std::move(t));
    }
    m.d.current.messages.push_back(std::move(a));

    // 员额 -tool 土地占有者(仍然) 流式传输).
    // 整个运行——包括高卡——留下 LIVE (agent_session 纪律;
    // 无中位 -run ) , 以"冻结"为主.
    Message ph; ph.role = Role::Assistant; ph.streaming_text = "working";
    m.d.current.messages.push_back(std::move(ph));
    m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};

    // 帧 LIVE:在直播的末尾中结算的卡片渲染.
    const std::size_t frozen_through_before = m.ui.frozen_through;
    auto live = render_rows(m);
    CHECK(frozen_through_before == 1,
          "run unexpectedly frozen while still active");

    // 转弯定了,单人定了 -time 冻结火灾。
    {
        auto& back = m.d.current.messages.back();
        back.text = std::move(back.streaming_text);
        back.streaming_text.clear();
    }
    m.s.phase = agentty::phase::Idle{};
    agentty::app::detail::freeze_through(m, m.d.current.messages.size());
    auto frozen = render_rows(m);

    // 冻结 必须 已经跑得快了
    CHECK(m.ui.frozen_through == m.d.current.messages.size(),
          "settle freeze did not take the whole run");

    // 强制前缀( 溢出数列 可视区域 顶端)必须是 字节
    // -identical 穿过现场的 / 冻结的交接。
    int d = first_committed_divergence(live, frozen, kTermH);
    if (d >= 0) {
        std::fprintf(stderr,
            "  --- live->frozen committed divergence at row %d ---\n", d);
        for (int y = d; y < std::min<int>(d + 3,
                 (int)std::max(live.size(), frozen.size())); ++y) {
            const char* lv = (y < (int)live.size())   ? live[y].c_str()   : "<none>";
            const char* fv = (y < (int)frozen.size()) ? frozen[y].c_str() : "<none>";
            std::fprintf(stderr, "    row %2d LIVE   |%s|\n", y, lv);
            std::fprintf(stderr, "    row %2d FROZEN |%s|\n", y, fv);
        }
    }
    CHECK(d < 0,
          "tall settled card shifted the committed prefix on freeze "
          "(the overflow->scrollback duplication)");

    // 和阴茎 正文 一定要出现 恰好 一次贯穿整个 渲染 (没有第二个副本
    // 搁浅) 已解决的编辑显示其 完整 正文 ,所以任何 正文 线条正一次
    // 出现——从最后一个数起算一条线条 差异块 ; > 1 副本表示搁
    // 浅 回滚缓冲区 重复,请重复。 NB : 编辑卡盖在 kMaxDiffLines
    // ( 200) 上渲染的 diff —— 选择该窗口内远处的探针线(
    // compute( 90) ) {} 正文 第186行),所以断言跟踪
    // 到: 接口边界 而不是顶部的擦除
    int copies = 0;
    for (const auto& row : frozen)
        if (row.find("compute(90) + offset") != std::string::npos) ++copies;
    CHECK(copies == 1,
          "edit diff body rendered more than once after freeze (duplicate)");
}

// 压缩 -boundary 接口边界 : 以下列日期为准: 传输-ONLY 紧凑,
// 现场直播 末尾 员额变动情况 -compaction 转弯必须已经携带
// `≡ Conversation compacted` 分拆这些 freeze_range。
// 如果有线 末尾 省了,就解决了 -freeze INSERTS 分隔符,
// 作为正义顶端的+1行移 -streamed 转弯——每行承诺 -emits
// 及其 回滚缓冲区 副本(帖子) -compaction 复制 -turn 剪
// 辑( / 剪辑) -panel 移动截图中的bug。 精确地模拟生产状态:
// 预 -compaction 前缀冻结 之前 任何记录(因此其冻结的行没有分割),
// a CompactionRecord 然后降落在 up_to_index
// == frozen_through (合作确实如此) 不 和高高的
// 帖子 -compaction 转弯 数据流 LIVE 然后再安顿下来。
// 见INLINE_SCROLLBACK.md pin #3(divider对称).
TEST_CASE("compaction boundary live to frozen seam") {
    constexpr int kTermH = 40;

    Model m;
    m.d.current.id = agentty::ThreadId{"compact"};
    agentty::app::detail::clear_frozen(m);
    auto& msgs = m.d.current.messages;

    // 压缩前历史：一段简短的用户与助手交互，
    //    在任何压缩记录存在之前就冻结了,因此其冻结的前缀带有
    //    NO分解器(确切的说,生产是如何达到这个状态的).
    {
        Message u; u.role = Role::User; u.text = "old question";
        msgs.push_back(std::move(u));
        Message a; a.role = Role::Assistant; a.text = "old answer";
        msgs.push_back(std::move(a));
    }
    agentty::app::detail::freeze_through(m, msgs.size());
    const std::size_t boundary = msgs.size();
    CHECK(m.ui.frozen_through == boundary,
          "pre-compaction prefix did not freeze");

    // 仅影响传输的压缩到达，记录覆盖 [0, boundary)。
    //    信件为 不 变异; 视图绘制索引 ====
    //    up_to_index.
    {
        agentty::Thread::CompactionRecord rec;
        rec.up_to_index = boundary;
        rec.summary     = "summary of the old conversation";
        m.d.current.compactions.push_back(std::move(rec));
    }

    // 压缩后轮次：用户再次提问，助手
    //    发布一个已解析的编辑器,其 diff 正文 谓 FAR 高点比 可视区域
    //    整个运行会一直持续 LIVE (单位:百日咳) -freeze 还没被开除呢
    {
        Message u; u.role = Role::User;
        u.text = "new question after compaction";
        msgs.push_back(std::move(u));
        Message a; a.role = Role::Assistant;
        ToolUse t;
        t.id   = ToolCallId{"edit_post"};
        t.name = ToolName{"edit"};
        t.args = {{"path", "src/post.cpp"}};
        auto now = steady_clock::now();
        t.status = ToolUse::Done{now - milliseconds{5}, now,
                                 edit_diff_output("post", 120)};
        a.tool_calls.push_back(std::move(t));
        msgs.push_back(std::move(a));
    }
    Message ph; ph.role = Role::Assistant; ph.streaming_text = "done";
    msgs.push_back(std::move(ph));
    m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};

    // 帧 LIVE: 分机+ -compaction后转 渲染在直播的末尾.
    CHECK(m.ui.frozen_through == boundary,
          "run unexpectedly frozen while still active");
    auto live = render_rows(m);

    // 分头人 必须 已经在里面了 LIVE 末尾 没有它,
    // 分化者将只受分化。 按需加载 冻结时间。
    int live_dividers = 0;
    for (const auto& row : live)
        if (row.find("Conversation compacted") != std::string::npos)
            ++live_dividers;
    CHECK(live_dividers == 1,
          "live tail missing the compaction divider before freeze "
          "(it would materialise at freeze time — the +1 row shift)");

    // 轮次收尾，唯一的收尾冻结操作触发。
    {
        auto& back = msgs.back();
        back.text = std::move(back.streaming_text);
        back.streaming_text.clear();
    }
    m.s.phase = agentty::phase::Idle{};
    agentty::app::detail::freeze_through(m, msgs.size());
    auto frozen = render_rows(m);

    CHECK(m.ui.frozen_through == msgs.size(),
          "settle freeze did not take the whole post-compaction run");

    // 强制前缀( 溢出数列 可视区域 顶端)必须是
    // 字节-identical 将自由的分解器移到现场
    // 末尾 ,冷冻器将它作为+1的转动插入到这里。
    int d = first_committed_divergence(live, frozen, kTermH);
    if (d >= 0) {
        std::fprintf(stderr,
            "  --- compaction live->frozen committed divergence row %d ---\n", d);
        for (int y = d; y < std::min<int>(d + 3,
                 (int)std::max(live.size(), frozen.size())); ++y) {
            const char* lv = (y < (int)live.size())   ? live[y].c_str()   : "<none>";
            const char* fv = (y < (int)frozen.size()) ? frozen[y].c_str() : "<none>";
            std::fprintf(stderr, "    row %2d LIVE   |%s|\n", y, lv);
            std::fprintf(stderr, "    row %2d FROZEN |%s|\n", y, fv);
        }
    }
    CHECK(d < 0,
          "compaction divider shifted the committed prefix on freeze "
          "(the post-compaction duplicate-turn bug)");

    // 分头人必须出现 恰好 一次在冰冻中 帧.
    int divider_copies = 0;
    for (const auto& row : frozen)
        if (row.find("Conversation compacted") != std::string::npos)
            ++divider_copies;
    CHECK(divider_copies == 1,
          "compaction divider rendered != once after freeze");

    // 而高大的编辑正文出现一次(没有搁浅的复制品).
    int body_copies = 0;
    for (const auto& row : frozen)
        if (row.find("compute(90) + offset") != std::string::npos)
            ++body_copies;
    CHECK(body_copies == 1,
          "post-compaction edit body rendered more than once (duplicate)");
}

// 生产 -faithful 紧凑流 : submit_message (
// modal.cpp )推下一条用户消息, freeze_through
// ( ) 是这样 的。 THEN 推动助理占位符——所以
// `≡ Conversation compacted` 将分隔符印入
// FROZEN 在提交时, 在活字之前 末尾 已存在。 末尾 因此,
// 只有永远坚持 -flight 助手运行; 分隔符从不在里面。 此选项将守护
// 真实 用户 -reported 路径: 提交必须是干净的 APPEND
// 必须在承诺前缀下方,助理人员必须冻结,不得转移承诺的行。 (从单位中分
// 离开) -level 上面的对称试验,它迫使边界进入活体 末尾 锻炼
// build_live_tail '除号器+数字码直接. )
TEST_CASE("compaction submit freezes divider") {
    constexpr int kTermH = 40;

    Model m;
    m.d.current.id = agentty::ThreadId{"compact2"};
    agentty::app::detail::clear_frozen(m);
    auto& msgs = m.d.current.messages;

    // 预览 -compaction 历史,在记录存在之前就冻结了
    {
        Message u; u.role = Role::User; u.text = "old question";
        msgs.push_back(std::move(u));
        Message a; a.role = Role::Assistant; a.text = "old answer";
        msgs.push_back(std::move(a));
    }
    agentty::app::detail::freeze_through(m, msgs.size());
    const std::size_t boundary = msgs.size();

    // 传输-only 边界上的紧凑记录
    {
        agentty::Thread::CompactionRecord rec;
        rec.up_to_index = boundary;
        rec.summary     = "summary of the old conversation";
        m.d.current.compactions.push_back(std::move(rec));
    }

    // 在下次提交之前, 分隔符是 PENDING ,未显示:它坐落在索引 =
    // == frozen_through 完全,所以没有建筑师 发布它。
    auto pre_submit = render_rows(m);
    int pre_dividers = 0;
    for (const auto& row : pre_submit)
        if (row.find("Conversation compacted") != std::string::npos)
            ++pre_dividers;
    CHECK(pre_dividers == 0,
          "divider shown before the next submit (should be pending)");

    // submit_message 的准确顺序：加入用户消息，调用 freeze_through 并记录
    //    将除法器 + 用户输入 FROZEN ),然后按下助理占位符.
    {
        Message u; u.role = Role::User;
        u.text = "new question after compaction";
        msgs.push_back(std::move(u));
    }
    agentty::app::detail::freeze_through(m, msgs.size());
    CHECK(m.ui.frozen_through == boundary + 1,
          "submit did not freeze through the new user message");
    // 分头人现在在 FROZEN 前缀 - 上方 frozen_through ,所以,
    // 我说 build_live_tail '边界检查( E) up_to_index ⁇ и)ぃ秨簀
    CHECK(m.ui.frozen_through > boundary,
          "frozen_through must pass the compaction boundary at submit");

    Message ph; ph.role = Role::Assistant;   // 空占位符, 如提交文件推
    msgs.push_back(std::move(ph));
    m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};

    // 仅提交 APPENDED 预留值以下(潜水器+用户+占位符)
    // -submit 已执行的前缀——上面的任何内容都不得移动。
    auto after_submit = render_rows(m);
    {
        int d = first_committed_divergence(pre_submit, after_submit, kTermH);
        CHECK(d < 0,
              "post-compaction submit shifted a committed scrollback row");
    }
    // 分裂者现在完全可见一次——它生活在冰冻的前缀,
    // 从不活的末尾.
    int submit_dividers = 0;
    for (const auto& row : after_submit)
        if (row.find("Conversation compacted") != std::string::npos)
            ++submit_dividers;
    CHECK(submit_dividers == 1,
          "compaction divider not stamped exactly once at submit");

    // 助手产生稳定的 edit，其差异正文超出
    //    可视区域。无 流式传输 文本: 推迟解决 -freeze
    //    (meta.cpp (仅于 逐步显示 已经排水,所以冻结瞬间
    //    比较一个完全 -settled 正文 与冰冻的一面——模拟
    //    现场 逐步显示 这儿,只是 竞态 动画时钟( 该路径由
    //    reveal_scrollback_test).
    {
        auto& a = msgs.back();
        ToolUse t;
        t.id   = ToolCallId{"edit_post2"};
        t.name = ToolName{"edit"};
        t.args = {{"path", "src/post2.cpp"}};
        auto now = steady_clock::now();
        t.status = ToolUse::Done{now - milliseconds{5}, now,
                                 edit_diff_output("post2", 120)};
        a.tool_calls.push_back(std::move(t));
    }
    auto live = render_rows(m);

    // 转弯定了,单人定了 -time 冻结火灾。
    m.s.phase = agentty::phase::Idle{};
    agentty::app::detail::freeze_through(m, msgs.size());
    auto frozen = render_rows(m);

    CHECK(m.ui.frozen_through == msgs.size(),
          "settle freeze did not take the whole post-compaction run");

    int d = first_committed_divergence(live, frozen, kTermH);
    if (d >= 0) {
        std::fprintf(stderr,
            "  --- submit-compaction live->frozen divergence row %d ---\n", d);
        for (int y = d; y < std::min<int>(d + 3,
                 (int)std::max(live.size(), frozen.size())); ++y) {
            const char* lv = (y < (int)live.size())   ? live[y].c_str()   : "<none>";
            const char* fv = (y < (int)frozen.size()) ? frozen[y].c_str() : "<none>";
            std::fprintf(stderr, "    row %2d LIVE   |%s|\n", y, lv);
            std::fprintf(stderr, "    row %2d FROZEN |%s|\n", y, fv);
        }
    }
    CHECK(d < 0,
          "post-compaction assistant settle shifted the committed prefix");

    int divider_copies = 0;
    for (const auto& row : frozen)
        if (row.find("Conversation compacted") != std::string::npos)
            ++divider_copies;
    CHECK(divider_copies == 1,
          "compaction divider rendered != once after the settle freeze");

    int body_copies = 0;
    for (const auto& row : frozen)
        if (row.find("compute(90) + offset") != std::string::npos)
            ++body_copies;
    CHECK(body_copies == 1,
          "post-compaction edit body rendered more than once (duplicate)");
}

// 写满的转弯 正文 : 已结算的写工具+一个短文本续作,
// 精确的(工具,文本)对成对,每转一转就使代理发音.
// `content` 使写卡溢出 可视区域.
static void push_write_turn(Model& m, const std::string& tag, int rows) {
    Message a; a.role = Role::Assistant;
    ToolUse t;
    t.id   = ToolCallId{"write_" + tag};
    t.name = ToolName{"write"};
    std::string content;
    for (int i = 0; i < rows; ++i)
        content += tag + " line " + std::to_string(i)
                +  ": plausible file content here\n";
    t.args = {{"file_path", "/tmp/" + tag + ".txt"}, {"content", content}};
    auto now = steady_clock::now();
    t.status = ToolUse::Done{now - milliseconds{5}, now,
                             "Created /tmp/" + tag + ".txt"};
    a.tool_calls.push_back(std::move(t));
    m.d.current.messages.push_back(std::move(a));
}

// 该 多个 -turn 复制: 用户请求写入 三个。
// 每个转弯是一对(写工具、文本摘要)。
// 第三次写入之后，第二次写入的内容重复了。
// 3转弯1和2转弯完全冻结(每个转弯3转弯的边界处均冻结)。
// 数据流 并溢出, 可视区域 ,已经 -committed
// 转弯2行不得 如果冷冻前缀的行序在3号弯时会移动 末尾 长,
// 转2的牌再 -emits 及其 回滚缓冲区 复制
// 件副本。
//
// 我们每个回合都开着生产节奏:
//   提交( freeze_through 前转弯 + 用户) -
//   > 写 Done - > 占位符文本 数据流 (全程运行)
//   LIVE ) - > 文本结算 - > 结算 -time
//   freeze_through 然后,下一个提交。在3号弯,
//   快照 每一步,并断言转弯2的 承诺行是 字节-stable 整个转弯3个序列
TEST_CASE("multi turn write pairs seam") {
    constexpr int kTermH = 40;
    constexpr int kRows  = 60;   // 每一笔超过40-row 可视区域

    Model m;
    m.d.current.id = agentty::ThreadId{"multi"};
    agentty::app::detail::clear_frozen(m);

    auto& msgs = m.d.current.messages;

    // 给定转弯的 正文 线, 以计数副本。
    auto body_marker = [](const std::string& tag) {
        return tag + " line 30:";
    };

    // 驱动器将1号和2号转向完成(完全冷冻).
    for (int turn = 1; turn <= 2; ++turn) {
        const std::string tag = "t" + std::to_string(turn);
        // ── submit
        Message u; u.role = Role::User;
        u.text = "write a file (" + tag + ")";
        msgs.push_back(std::move(u));
        m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};
        agentty::app::detail::freeze_through(m, msgs.size());

        // ── write settles; the continuation placeholder streams. The
        //    整个运行停留 LIVE (无中位数) -run ) , 以"冻结"为主.
        push_write_turn(m, tag, kRows);
        Message ph; ph.role = Role::Assistant; ph.streaming_text = "done writing";
        msgs.push_back(std::move(ph));

        // 续接文本稳定，轮次进入空闲并完成收尾，
        //    单笔结算 -time 冻死整个运行。
        auto& back = msgs.back();
        back.text = std::move(back.streaming_text);
        back.streaming_text.clear();
        m.s.phase = agentty::phase::Idle{};
        agentty::app::detail::freeze_through(m, msgs.size());
    }

    // 两转弯现在必须完全冻结(没有活的).
    CHECK(m.ui.frozen_through == msgs.size(),
          "turns 1 & 2 not fully frozen before turn 3");

    // 快照 两人解决了 -turn 记录片——这是承诺的
    // 基线第3转线,绝不能打扰。
    auto baseline = render_rows(m);

    // 轮次 3：提交后，第三次写入以流式到达并超出可视区。
    {
        Message u; u.role = Role::User; u.text = "write a file (t3)";
        msgs.push_back(std::move(u));
        m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};
        agentty::app::detail::freeze_through(m, msgs.size());
    }
    auto after_submit = render_rows(m);

    // Write 运行( 完整内容存在, 牌窗口) + 占位符。
    push_write_turn(m, "t3", kRows);
    // 放弃正义 -pushed 写入运行到模拟 流
    // 式传输 还没到就到
    agentty::app::detail::with_live_tool(
        m, ToolCallId{"write_t3"}, [&](ToolUse& t) {
            t.status = ToolUse::Running{steady_clock::now(), ""};
        });
    Message ph3; ph3.role = Role::Assistant; ph3.streaming_text = "...";
    msgs.push_back(std::move(ph3));
    auto frame_running = render_rows(m);

    // Write 整场比赛都持续到转弯结束
    agentty::app::detail::with_live_tool(
        m, ToolCallId{"write_t3"}, [&](ToolUse& t) {
            auto now = steady_clock::now();
            t.status = ToolUse::Done{now - milliseconds{5}, now,
                                     "Created /tmp/t3.txt"};
        });
    // 关键 帧 : 写入 + 占位符存在, 冻结有 不 运行(
    // 运行仍在运行)。 运行中 末尾 键此运行时使用相同的
    // assistant_run_hash_id 冻结会用到的
    auto frame_settled = render_rows(m);

    // 转弯定了,单人定了 -time 冻结火灾。
    {
        auto& back = msgs.back();
        back.text = std::move(back.streaming_text);
        back.streaming_text.clear();
    }
    m.s.phase = agentty::phase::Idle{};
    agentty::app::detail::freeze_through(m, msgs.size());
    auto frame_frozen = render_rows(m);

    // 执着的前缀, 可视区域 顶端)必须是 字节
    // -stable 横跨 每个 转3步。转2号卡片的
    // 住址是这个承诺区,任何转折都会重复它。
    struct Step { const char* name; const std::vector<std::string>* rows; };
    const Step steps[] = {
        {"baseline",     &baseline},
        {"after_submit", &after_submit},
        {"running",      &frame_running},
        {"settled",      &frame_settled},
        {"frozen",       &frame_frozen},
    };
    for (std::size_t s = 1; s < std::size(steps); ++s) {
        int d = first_committed_divergence(*steps[s - 1].rows,
                                           *steps[s].rows, kTermH);
        if (d >= 0) {
            std::fprintf(stderr,
                "  multi-turn committed divergence %s->%s at row %d\n",
                steps[s - 1].name, steps[s].name, d);
            const auto& p = *steps[s - 1].rows;
            const auto& c = *steps[s].rows;
            for (int y = d; y < std::min<int>(d + 4,
                     (int)std::max(p.size(), c.size())); ++y) {
                std::fprintf(stderr, "    row %2d P |%s|\n", y,
                             y < (int)p.size() ? p[y].c_str() : "<none>");
                std::fprintf(stderr, "    row %2d C |%s|\n", y,
                             y < (int)c.size() ? c[y].c_str() : "<none>");
            }
        }
        CHECK(d < 0,
              "turn-3 write shifted a committed scrollback row "
              "(turn 2 duplicates)");
    }

    // 转二号 正文 一定要出现 恰好 决赛中一次 帧.
    int t2_copies = 0;
    for (const auto& row : frame_frozen)
        if (row.find(body_marker("t2")) != std::string::npos) ++t2_copies;
    CHECK(t2_copies == 1,
          "turn 2's write body rendered more than once after the third "
          "write (the reported duplicate)");
}

// NOTE: test_incremental_body_freeze_seam /
// _seam_chunked (迫使中层 -stream 雕刻在通过
// set_incremental_freeze_override 开车
// maybe_incremental_freeze ) , 即为 DELETED 生产纪律——在定居时保持现场、
// 冻结——由以下几个方面涵盖: test_incremental_freeze_prefix_stable
// (以上)和 single/write/read 接口边界 测试结果如下。

// 稳定前缀提升的正确性：性能拆分不应改变可观察结果。
//    turn_config_for_assistant_run 吊起领先的
//    quiescent 子 -turns 输入 -flight 运行到
//    一个 可缓存赤色转折以保持每 -frame 以 O(1) 的深度构建。
//    拆分必须 绝不 更改用户所看到的: 每一个已结算的工具卡必须仍然
//    渲染 恰好 一次, 行输出必须相同 帧-to-frame 随前缀的
//    增大(一张挂牌悄悄消失,或 过期 缓存 擦掉一个旧的前缀,
//    是 "sometimes not working" 窃听这个守卫
TEST_CASE("prefix hoist all cards present") {
    Model m;
    m.d.current.id = agentty::ThreadId{"hoist"};
    Message u; u.role = Role::User; u.text = "do many edits";
    m.d.current.messages.push_back(std::move(u));
    agentty::app::detail::clear_frozen(m);
    agentty::app::detail::freeze_through(m, 1);   // 用户被冻结

    constexpr int N = 24;   // 深到可以让升降机介入
    std::vector<std::string> prev;

    for (int e = 0; e < N; ++e) {
        // 已定居的编辑子-turn土地(所有前作都成为可升起的
        // quiescent前缀).
        Message a; a.role = Role::Assistant;
        a.tool_calls.push_back(settled_edit("h" + std::to_string(e)));
        m.d.current.messages.push_back(std::move(a));

        // 流式传输 继任占位符 = 活边(从未竖起).
        Message ph; ph.role = Role::Assistant; ph.streaming_text = "working";
        m.d.current.messages.push_back(std::move(ph));
        m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};

        auto cur = render_rows(m);

        // (1) 每个 迄今已结算的编辑卡必须存在 恰好 一次,我记得,
        //     我说过,我... settled_edit 邮票 tc.output
        //     ()="edited-<tag>" ; 牌号 渲染 工具
        //     名称+路径 src/<tag> .cpp,所以独特的路径是
        //     我们的 -card 标记。挂牌消失(延迟中毒/ 过期 前缀
        //     缓存 ) 降低标记;双倍 -emit (纯空/ 重复插
        //     槽) 将数数撞在 1 以上。
        for (int k = 0; k <= e; ++k) {
            const std::string marker = "src/h" + std::to_string(k) + ".cpp";
            int copies = 0;
            for (const auto& row : cur)
                if (row.find(marker) != std::string::npos) ++copies;
            CHECK(copies == 1,
                  ("settled card " + marker
                   + " not rendered exactly once in a "
                   + std::to_string(e + 1) + "-deep in-flight run "
                   + "(got " + std::to_string(copies)
                   + ") — prefix hoist dropped or duplicated a card").c_str());
        }

        // (2) 承诺的前缀必须是: 字节-stable 整个增长(a)
        //     过期 前缀 缓存 撕裂旧形状会重写一个已经 -overflowed
        //     ) (中文(简体) ).
        if (!prev.empty()) {
            int d = first_committed_divergence(prev, cur, /*term_h=*/40);
            CHECK(d < 0,
                  "prefix hoist rewrote a committed row as the run grew");
        }
        prev = std::move(cur);

        m.d.current.messages.pop_back();   // 丢弃占位符
    }

    // (3) 整行的Snock-freeze:冻结的
    //     输出必须包含每张卡片一次(分解-平面交接干净)。
    m.s.phase = agentty::phase::Idle{};
    agentty::app::detail::freeze_through(m, m.d.current.messages.size());
    auto frozen = render_rows(m);
    for (int k = 0; k < N; ++k) {
        const std::string marker = "src/h" + std::to_string(k) + ".cpp";
        int copies = 0;
        for (const auto& row : frozen)
            if (row.find(marker) != std::string::npos) ++copies;
        CHECK(copies == 1,
              ("card " + marker + " not exactly once after settle-freeze")
                  .c_str());
    }
}

// 用户报告的提升缺陷：一个 RUNNING 工具位于
//    深跑时的直播边缘,然后是用户的短信 AGAIN
//    这里有两个不同的错误, 两者都不是 -settled
//    上面的升降测试可以击球(只有曾经 终端 纸牌 :
//
//    (A) 静态 -Running 工具卡必须 渲染 以奔驰的方式奔驰,
//        -prefix 举起手来 不 吞下边角子 -turn 工具为非
//        -terminal ——其国家 spinner/progress
//        一定要不断更新 所以它必须留在急切的 -built 末尾.
//
//    (B) 何时a 新 用户转弯(及其助手的答复)在运行后附加,
//        即 -settled 上方的工具卡必须仍然 渲染 一次
//        就是一次 一次就是第二次了 和 承诺的前缀不能重写。
//        报告症状(
//        回复到达时，先前工具卡片的输出变得错乱。
//        )是执着的 -row 重写这里—— a 过期 竖起 -prefix 缓存
//        将一个旧形状 铺在已经溢成原生的行上 回滚缓冲区.
static ToolUse running_edit(const std::string& tag) {
    ToolUse t;
    t.id   = ToolCallId{"edit_" + tag};
    t.name = ToolName{"edit"};
    nlohmann::json edits = nlohmann::json::array();
    edits.push_back({{"old_text", code_block(4)},
                     {"new_text", code_block(4)}});
    t.args = {{"path", "src/" + tag + ".cpp"}, {"edits", edits}};
    t.status = ToolUse::Running{steady_clock::now(), ""};
    return t;
}

TEST_CASE("running tool then followup user turn") {
    constexpr int kTermH = 40;
    Model m;
    m.d.current.id = agentty::ThreadId{"run+followup"};
    Message u; u.role = Role::User; u.text = "do a deep run";
    m.d.current.messages.push_back(std::move(u));
    agentty::app::detail::clear_frozen(m);
    agentty::app::detail::freeze_through(m, 1);

    constexpr int N = 24;   // 深到可以让升降机介入
    // 构建一个深层运行 SETTLED 编辑子 -turns.
    for (int e = 0; e < N; ++e) {
        Message a; a.role = Role::Assistant;
        a.tool_calls.push_back(settled_edit("r" + std::to_string(e)));
        m.d.current.messages.push_back(std::move(a));
    }
    // 活的边缘:一个子 -turn 工具是 仍然 RUNNING.
    {
        Message a; a.role = Role::Assistant;
        a.tool_calls.push_back(running_edit("live"));
        m.d.current.messages.push_back(std::move(a));
    }
    m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};

    auto with_running = render_rows(m);

    // (A) 运行中的卡MUST出现(其路径标记显示).
    {
        const std::string marker = "src/live.cpp";
        int copies = 0;
        for (const auto& row : with_running)
            if (row.find(marker) != std::string::npos) ++copies;
        CHECK(copies >= 1,
              "running tool card missing while its tool is running "
              "(hoist swallowed the live edge)");
    }
    // 每张卡片都贴过一次
    for (int k = 0; k < N; ++k) {
        const std::string marker = "src/r" + std::to_string(k) + ".cpp";
        int copies = 0;
        for (const auto& row : with_running)
            if (row.find(marker) != std::string::npos) ++copies;
        CHECK(copies == 1,
              ("settled card " + marker + " not exactly once with a "
               "running edge").c_str());
    }

    // 工具完成;运行会结算和冻结(turn-end).
    agentty::app::detail::with_live_tool(
        m, ToolCallId{"edit_live"}, [&](ToolUse& t) {
            auto now = steady_clock::now();
            t.status = ToolUse::Done{now - milliseconds{5}, now,
                                     "edited-live"};
        });
    m.s.phase = agentty::phase::Idle{};
    agentty::app::detail::freeze_through(m, m.d.current.messages.size());
    auto after_settle = render_rows(m);

    // 强制前缀稳定在安居地之间。
    {
        int d = first_committed_divergence(with_running, after_settle, kTermH);
        CHECK(d < 0,
              "committed prefix rewritten when the running tool settled");
    }

    // (B)用户TEXTSXXAGAIN:新用户转+新助理回复.
    Message u2; u2.role = Role::User; u2.text = "now explain what you did";
    m.d.current.messages.push_back(std::move(u2));
    Message a2; a2.role = Role::Assistant; a2.streaming_text = "Sure — I ";
    m.d.current.messages.push_back(std::move(a2));
    m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};
    auto with_reply = render_rows(m);

    // 上一个运行的每个工具卡必须 仍然 渲染 一次就是这样了
    for (int k = 0; k < N; ++k) {
        const std::string marker = "src/r" + std::to_string(k) + ".cpp";
        int copies = 0;
        for (const auto& row : with_reply)
            if (row.find(marker) != std::string::npos) ++copies;
        CHECK(copies == 1,
              ("prior-run card " + marker + " broke after a follow-up "
               "user turn (got " + std::to_string(copies) + ")").c_str());
    }
    {
        const std::string marker = "src/live.cpp";
        int copies = 0;
        for (const auto& row : with_reply)
            if (row.find(marker) != std::string::npos) ++copies;
        CHECK(copies == 1,
              ("the once-running card broke after a follow-up user turn "
               "(got " + std::to_string(copies) + ")").c_str());
    }
    // 和承诺的前缀(整个冻结运行)必须是 字节-stable
    // 整个新用户转弯到达。
    {
        int d = first_committed_divergence(after_settle, with_reply, kTermH);
        CHECK(d < 0,
              "committed prefix rewritten when a follow-up user turn "
              "arrived after a deep tool run");
    }
}

