// midrun_freeze_test ——单曲正确性. -freeze 纪律(
// agent_session-style : 整个活动运行会保留 LIVE ;
// 将它冻结为 m.ui.frozen 一次,在结算时( finalize_turn 冷冻的
// 文字记录和现场直播 渲染 一定是,我必须 VISUALLY SEAMLESS - 转
// 一圈读作: 一个 转弯 :
//
//   · 确切地说 一个 扬声器 头文件 整个转弯( 冷冻前缀)
//     载着它吧!其余的,是续发的。 头文件 被
//     压制),
//   • 每个 完全完成了第-turn 号的正文 渲染号(没有隐藏) /
//     以白昼为首;
//   • 转弯号码不重复或中度提前 -run,
//   • frozen_through 预付款, 确切的说是一个子 -turn 继续活着。
//
// 结构验证 : 渲染 整个对话( 冻结列表 + 直播) 末尾
// )到一个画像 并读到 存储单元 以文字形式返回。

#include <algorithm>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "agtest.hpp"

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
using std::chrono::steady_clock;
using std::chrono::milliseconds;

// 渲染 整个对话(冻结+直播) 末尾 )和倾倒所绘的行作为 ASCII
// -folded 文本中选择一个
static std::string render_dump(const Model& m, int width = 100,
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

    std::string out;
    const int max_row = canvas.max_content_row();
    for (int y = 0; y <= max_row; ++y) {
        std::string line;
        for (int x = 0; x < width; ++x) {
            char32_t ch = canvas.get(x, y).character;
            if (ch == 0) ch = U' ';
            line.push_back(ch < 128 ? static_cast<char>(ch) : '?');
        }
        while (!line.empty() && line.back() == ' ') line.pop_back();
        out += line;
        out.push_back('\n');
    }
    return out;
}

static int count_occurrences(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return 0;
    int n = 0;
    for (std::size_t p = hay.find(needle); p != std::string::npos;
         p = hay.find(needle, p + needle.size()))
        ++n;
    return n;
}

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

// 构建活动自动 -pilot 运行:用户,然后是 N 助理子 -turns 每一个
// 都有一个固定的编辑,加一个 流式传输 末尾 中。仅冻结用户(提交) -time
// freeze_through (b) 与《京都议定书》有关的 整个 活动运行保持
// 直播—— agent_session 纪律,跑步会冻结一次,在和解时
static Model active_run(int n) {
    Model m;
    m.d.current.id = agentty::ThreadId{"midrun"};
    Message u; u.role = Role::User; u.text = "please do many edits";
    m.d.current.messages.push_back(std::move(u));
    for (int e = 0; e < n; ++e) {
        Message a; a.role = Role::Assistant;
        a.tool_calls.push_back(settled_edit("s" + std::to_string(e)));
        m.d.current.messages.push_back(std::move(a));
    }
    Message tail; tail.role = Role::Assistant;
    tail.streaming_text = "continuing to work";
    m.d.current.messages.push_back(std::move(tail));
    m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};

    agentty::app::detail::clear_frozen(m);
    agentty::app::detail::freeze_through(m, 1);          // 用户
    return m;
}

// 1. 在用户以外的活动运行中,NOTHING被冻结(整个运行中)
//    运行是直播, agent_session-style ),在解决运行冻结整段.
TEST_CASE("live bounded") {
    Model m = active_run(40);
    CHECK(m.ui.frozen_through == 1,
          "mid-run freeze fired during an active run — the single freeze "
          "site is settle (finalize_turn)");
    // 解决 : 数据流 完成。 finalize_turn 排水沟 streaming_text
    // 将文本写入冻结前的文本; 镜像 : run_is_freezable
    // 拒绝任何使用 Un 的运行 -drained 流式传输 字节.
    {
        auto& back = m.d.current.messages.back();
        back.text = std::move(back.streaming_text);
        back.streaming_text.clear();
    }
    m.s.phase = agentty::phase::Idle{};
    agentty::app::detail::freeze_through(m, m.d.current.messages.size());
    CHECK(m.ui.frozen_through == m.d.current.messages.size(),
          "settle freeze didn't freeze the whole run");
}

// 2. 每一完成的第-turn 渲染分段全部(无任何隐藏)。
TEST_CASE("all content present") {
    constexpr int N = 40;
    Model m = active_run(N);
    auto txt = render_dump(m);
    int present = 0;
    for (int e = 0; e < N; ++e)
        if (txt.find("src/s" + std::to_string(e) + ".cpp") != std::string::npos)
            ++present;
    CHECK(present == N, "not every settled sub-turn rendered in full");
    CHECK(txt.find("earlier action") == std::string::npos,
          "an elision marker leaked into the render");
}

// 3. 只有一个发言者 头文件 转弯(无重复)
//    冻结的 Q 继续 接口边界。助理标签是模型徽章;
//    我们计算元 "turn N" 标记,只显示 HEADER
//    排行载体——一种延续抑制它.
TEST_CASE("single turn header") {
    Model m = active_run(20);
    auto txt = render_dump(m);
    // 转弯的元件 "turn N"。对于一个逻辑助手转弯,
    // 它必须完全出现在一个被冻结的前缀+活字段 末尾.
    const int turn_markers = count_occurrences(txt, "turn 1");
    CHECK(turn_markers == 1,
          "turn number appears != once \u2014 continuation seam duplicated header");
}

// 4. 比较a SETTLED + 冻 渲染 与同一人对决
//    消息已完全直播:冻结更改WHERE行渲染(冻对活),
//    永不更改WHICH行.
TEST_CASE("split preserves content") {
    constexpr int N = 15;
    Model split = active_run(N);
    // 解析+冻结整条(生产路径).
    split.s.phase = agentty::phase::Idle{};
    agentty::app::detail::freeze_through(split,
        split.d.current.messages.size());

    Model whole;
    whole.d.current.id = agentty::ThreadId{"whole"};
    Message u; u.role = Role::User; u.text = "please do many edits";
    whole.d.current.messages.push_back(std::move(u));
    for (int e = 0; e < N; ++e) {
        Message a; a.role = Role::Assistant;
        a.tool_calls.push_back(settled_edit("s" + std::to_string(e)));
        whole.d.current.messages.push_back(std::move(a));
    }
    Message tail; tail.role = Role::Assistant;
    tail.streaming_text = "continuing to work";
    whole.d.current.messages.push_back(std::move(tail));
    whole.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};
    agentty::app::detail::clear_frozen(whole);
    agentty::app::detail::freeze_through(whole, 1);   // 仅冻结用户

    auto a = render_dump(split);
    auto b = render_dump(whole);
    // 两者中每个编辑标记都存在.
    int both = 0;
    for (int e = 0; e < N; ++e) {
        std::string needle = "src/s" + std::to_string(e) + ".cpp";
        if (a.find(needle) != std::string::npos
            && b.find(needle) != std::string::npos) ++both;
    }
    CHECK(both == N, "split and whole renders disagree on content presence");
}

// 5. 冻结门从来不冻结一个非 -terminal 运行: 带有运行
//    工具中的 末尾, freeze_through 运行开始时停止;
//    一旦运行全部结束 终端 整个都冻僵了 run_is_freezable
//    - 唯一的中场 -run 单人安全 -freeze 纪律需要。 )
TEST_CASE("trailing tool batch freezes") {
    Model m;
    m.d.current.id = agentty::ThreadId{"trailing"};
    Message u; u.role = Role::User; u.text = "write a file";
    m.d.current.messages.push_back(std::move(u));
    Message a; a.role = Role::Assistant;
    ToolUse running = settled_edit("only");
    running.status = ToolUse::Running{steady_clock::now(), ""};
    a.tool_calls.push_back(std::move(running));
    m.d.current.messages.push_back(std::move(a));
    m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};

    agentty::app::detail::clear_frozen(m);
    // 甚至一个(不相干)整体 -thread 冻结尝试必须停止在非 -terminal
    // 运行( R)... run_is_freezable 门是门。
    agentty::app::detail::freeze_through(m, m.d.current.messages.size());
    CHECK(m.ui.frozen_through == 1,
          "froze a run with a Running tool — spinner pinned in scrollback");

    // 工具会结算; 转弯结束; 冻结将全部收回。 Name
    agentty::app::detail::with_live_tool(
        m, ToolCallId{"edit_only"}, [](ToolUse& t) {
            auto now = steady_clock::now();
            t.status = ToolUse::Done{now - milliseconds{5}, now, "edited"};
        });
    m.s.phase = agentty::phase::Idle{};
    agentty::app::detail::freeze_through(m, m.d.current.messages.size());
    CHECK(m.ui.frozen_through == m.d.current.messages.size(),
          "settled run did not freeze at the settle boundary");
    auto txt = render_dump(m);
    CHECK(txt.find("src/only.cpp") != std::string::npos,
          "frozen tool batch body vanished from the render");
}

// 第一次还回去 NON-BLANK 已制成行,或 "" 如果每行都是空白的。
static std::string first_nonblank_row(const Model& m) {
    auto txt = render_dump(m);
    std::size_t p = 0;
    while (p < txt.size()) {
        std::size_t eol = txt.find('\n', p);
        std::string line = txt.substr(p, eol == std::string::npos ? std::string::npos : eol - p);
        bool blank = line.find_first_not_of(" ") == std::string::npos;
        if (!blank) return line;
        if (eol == std::string::npos) break;
        p = eol + 1;
    }
    return {};
}

// 有多少首首首行在第一内容行前是空白的.
static int leading_blank_rows(const Model& m) {
    auto txt = render_dump(m);
    int n = 0;
    std::size_t p = 0;
    while (p < txt.size()) {
        std::size_t eol = txt.find('\n', p);
        std::string line = txt.substr(p, eol == std::string::npos ? std::string::npos : eol - p);
        if (line.find_first_not_of(" ") != std::string::npos) break;
        ++n;
        if (eol == std::string::npos) break;
        p = eol + 1;
    }
    return n;
}

// 6. 已保存-thread 重新装入( 水合物)。 长 IDLE 线程 最终
//    运行为 GIANT 部队 rehydrate_frozen 子
//    目录 -turn 切。重建的冻伤前缀不得 空白或空空格打开
//    头文件-less 继续——画布顶部必须是真正的转弯 头文件.
//    (所报错误 : 已保存 线程 渲染 空洞/转弯
//    头文件 ) 被切断.
TEST_CASE("rehydrate top is header") {
    Model m;
    m.d.current.id = agentty::ThreadId{"saved"};
    // 几个完整的用户+助手交换,然后一个 GIANT 最终自动 -pilot 运行
    // ( 众多大型) write/edit 子目录 -turns () 闲置。
    for (int t = 0; t < 4; ++t) {
        Message u; u.role = Role::User;
        u.text = "request " + std::to_string(t);
        m.d.current.messages.push_back(std::move(u));
        Message a; a.role = Role::Assistant;
        a.text = "reply " + std::to_string(t);
        m.d.current.messages.push_back(std::move(a));
    }
    // 巨型终极运行:用户转弯后60个解析 -edit 子目录 -turns
    // + 关闭文本子 -turn ~60脂肪卡 ~ 水合水预算,
    // 所以割地 MID-RUN.
    Message gu; gu.role = Role::User; gu.text = "do a huge refactor";
    m.d.current.messages.push_back(std::move(gu));
    for (int e = 0; e < 60; ++e) {
        Message a; a.role = Role::Assistant;
        a.tool_calls.push_back(settled_edit("big" + std::to_string(e)));
        m.d.current.messages.push_back(std::move(a));
    }
    Message done; done.role = Role::Assistant; done.text = "all done";
    m.d.current.messages.push_back(std::move(done));
    m.s.phase = agentty::phase::Idle{};

    agentty::app::detail::rehydrate_frozen(m);

    // 整本记录都闲着,所以应该没有活的.
    CHECK(m.ui.frozen_through == m.d.current.messages.size(),
          "rehydrate left messages unfrozen on an idle thread");
    // 画布不能打开 HOLE。单空行是 Turn 部件的
    // 上边( 每个回合都有它); 真正的错误是搁浅的多边
    // -row 间隙(一个主要分隔符条目)或 头文件 - 少继续,
    // 让1号 -row 内建函数 边距。
    CHECK(leading_blank_rows(m) <= 1,
          "rehydrated saved thread opens with a blank gap at the top");
    // 第一个可见的行必须是转弯 头文件 (型号徽章/元),
    // 不流浪 正文 内容来自 a 头文件-less 继续。
    // 助手徽章是模型名称; 一个用户 头文件 是那个 提示词.
    // 无论哪种方式,第一行都应该携带扬声器,而不是赤裸的编辑
    // diff线条.
    const std::string top = first_nonblank_row(m);
    CHECK(!top.empty(), "rehydrated thread rendered nothing");
    CHECK(top.find("src/big") == std::string::npos,
          "rehydrated thread opens MID-card (header-less continuation) "
          "— the run header was cut off");
}

// 7. 特里姆永远不能拉起一条领先的空白行。
//    trim_frozen_if_oversized 放下前置条目,
//    新的第一个冻结条目必须是转弯, 而不是中间 -turn gap_row 之前的这个
TEST_CASE("trim no leading gap") {
    Model m;
    m.d.current.id = agentty::ThreadId{"trim"};
    // 许多完全闲置的交换,所以冻结推[转 [gap][转]. .
    // 并且排行预算被超过, 迫使前修剪。
    for (int t = 0; t < 80; ++t) {
        Message u; u.role = Role::User;
        u.text = "q" + std::to_string(t);
        m.d.current.messages.push_back(std::move(u));
        Message a; a.role = Role::Assistant;
        a.tool_calls.push_back(settled_edit("t" + std::to_string(t)));
        m.d.current.messages.push_back(std::move(a));
    }
    m.s.phase = agentty::phase::Idle{};
    agentty::app::detail::clear_frozen(m);
    agentty::app::detail::freeze_through(m, m.d.current.messages.size());
    auto cmd = agentty::app::detail::trim_frozen_if_oversized(m);
    (void)cmd;

    CHECK(leading_blank_rows(m) <= 1,
          "trim stranded a blank gap row at the top of the frozen prefix");
    const std::string top = first_nonblank_row(m);
    CHECK(top.find("src/t") == std::string::npos,
          "trim left a header-less card at the top (dropped a run header "
          "but kept its body)");
}

// 8. 结束 -budget 修剪将最古老的冻结条目从 MODEL
//    (顶部) -DELETION 冻结前缀)和 必须 将原生的行完全投入
//    回滚缓冲区 途经 Cmd::commit_scrollback(N).
//    这不是 下层 -shrink: maya's 渲染-time
//    缩进调节无法修正顶端 -deletion。在投放后,新的画布内容会向上移动,
//    因此画布行0与 prev_cells 财务报告和已审计财务报表
//    scrollback_prefix_matches (a) 假返回;
//    maya 唯一的前缀 -mismatch 追回承诺 新 前缀为
//    回滚缓冲区 横行 PHYSICALLY 抓好,别动 旧 内容,将一个复刻的
//    边界线一个屏幕上。返回 none() TRIGGERS 那个虫子,它不会避免它。
//    commit_scrollback(removed_rows 与前一个
//    (旧)相对应 -content) 帧 ,
//    推进 prev_rows/prev_cells 锁在锁上,所以下一个 渲染 坐标和边界就一次。
//    CONTRACT : 修饰使模型变异 和 返回时 commit_scrollback
//    (removed_rows).
TEST_CASE("trim commits exact dropped rows") {
    Model m;
    m.d.current.id = agentty::ThreadId{"trimrows"};
    // 几根高的笔迹转动,每根井都翻过一个 可视区域 ,
    // 所以冰冻的前缀吹过 frozen_row_budget
    // ()和前修剪火——报道的重写(重写大写)的确切形状.
    for (int t = 0; t < 6; ++t) {
        Message u; u.role = Role::User;
        u.text = "write a file (" + std::to_string(t) + ")";
        m.d.current.messages.push_back(std::move(u));
        Message a; a.role = Role::Assistant;
        agentty::ToolUse tw;
        tw.id   = agentty::ToolCallId{"w" + std::to_string(t)};
        tw.name = agentty::ToolName{"write"};
        std::string content;
        for (int i = 0; i < 80; ++i)
            content += "line " + std::to_string(i) + ": file body content\n";
        tw.args = {{"file_path", "/tmp/f" + std::to_string(t) + ".txt"},
                   {"content", content}};
        auto now = std::chrono::steady_clock::now();
        tw.status = agentty::ToolUse::Done{
            now - std::chrono::milliseconds{5}, now, "Created"};
        a.tool_calls.push_back(std::move(tw));
        m.d.current.messages.push_back(std::move(a));
    }
    m.s.phase = agentty::phase::Idle{};
    agentty::app::detail::clear_frozen(m);
    agentty::app::detail::freeze_through(m, m.d.current.messages.size());

    // 模拟一个已实现 帧 : 分类账的可复制性门只会
    // 掉下高度高的块 maya 油漆记录了,
    // 而这种绳索从未运行过真正的油漆通道。每个街区的政策高度
    // 作为记录的高度——就是分类账的准确高度。
    // -tagged 油漆和铺设的 -out 生产高度。
    for (std::size_t k = 0; k < m.ui.frozen.size(); ++k)
        m.ui.frozen.record_paint(
            k, static_cast<int>(m.ui.frozen.block_rows(k)));

    // 在修剪前记录行总数 这样我们就可以
    // 计算 修剪时的行数
    const std::size_t rows_before = m.ui.frozen.row_total();
    const std::size_t entries_before = m.ui.frozen.size();

    auto cmd = agentty::app::detail::trim_frozen_if_oversized(m);

    const std::size_t rows_after   = m.ui.frozen.row_total();
    const std::size_t dropped_rows = rows_before - rows_after;
    CHECK(entries_before > m.ui.frozen.size(),
          "trim did not drop any entries despite an over-budget prefix");
    CHECK(dropped_rows > 0, "trim dropped entries but no rows");

    // 返回的CmdXXMUST是commit_scrollback(N),
    // 其中N是从冻结的前缀TOP中去掉的切线的确切数列.
    // 顶部 -deletion 不可见 maya's 渲染-time 校正;
    // 主机必须将空格对齐 -valid 旧 帧 所以下一个 渲染 较短的
    // 画布坐标。 正在返回 none() (或 减 减 )
    // commit_scrollback_overflow ) 线条为重复的边界。
    const auto* commit = std::get_if<maya::CommitScrollback>(&cmd.inner);
    CHECK(commit != nullptr,
          "trim must return commit_scrollback(N) for the top-deletion; "
          "none() leaves maya unable to reconcile and strands a duplicate "
          "of the trimmed boundary one screen up.");
    if (commit) {
        CHECK(commit->debt.rows() == static_cast<int>(dropped_rows),
              "trim committed the wrong row count — must equal the rows "
              "dropped from the frozen prefix, or the scrollback boundary "
              "misaligns (under-commit strands the new prefix; over-commit "
              "eats a visible row).");
    }
}

// 9. (删除) trim_frozen_above_viewport 中场 -run-safe 修剪为
//    中间 -run 两者都不见了:单一的冻结地点已经解决( finalize_turn ),
//    所以唯一的修饰是 trim_frozen_if_oversized
//    ——包含在试验8中.

// 10. 产出 -elided 工具(bash, read, grep,...) 渲染 a 固定数额。 head/tail
//     预算, 不 全部输出。 frozen_rows 估计必须跟踪
//     ELIDED 渲染 高度,而不是输出行数——一个超
//     -count 中间旅行 -run 保留 -loop 将一个
//     -screen 进入和困住一个罪犯 -scrollback 鬼
//     乐队(狂欢) -card 重複的bug。 将存储的估计数保留在
//     与输出量巨大的shash卡真实渲染高度的近距离。
TEST_CASE("rehydrate bounds a LATER oversized run") {
    // 保存的空间 -loop 走路最新鲜的 -first 并且
    // 经常在跑步中切片 直到它被切掉的时候 首次 考虑过(
    // `units == 0` 因此,进行了第二次或稍后的大规模运行。
    // 整个 ,而预算检查只是在之后才开始——到那时,
    // 这几行已经进入。 线程 最终以2247行冰冻的一排对
    // 180 -row 预算因为2号跑是2100列单车.
    //
    // 最重要的是,这很重要 RESIZE :宽度变化使每个缓存布局无效,
    // 所以整个被冻结的画布重新 -lays-out。测量了这一点。
    // 线程 ,每调整4.44 ms,而每栏0.43 ms为暖度,
    // 拖动则每栏调整一个。
    //
    // 形状:一个SMALL最新运行(所以第一次重复不会绊倒旧的切口),
    // 然后是巨大的一个后面.
    Model m;
    m.d.current.id = agentty::ThreadId{"laterbig"};

    for (int t = 0; t < 4; ++t) {
        Message u; u.role = Role::User;
        u.text = "early " + std::to_string(t);
        m.d.current.messages.push_back(std::move(u));
        Message a; a.role = Role::Assistant;
        a.text = "reply " + std::to_string(t);
        m.d.current.messages.push_back(std::move(a));
    }
    // 巨大的跑步——第二 -newest 一旦小的被附着。
    Message gu; gu.role = Role::User; gu.text = "do a huge refactor";
    m.d.current.messages.push_back(std::move(gu));
    for (int e = 0; e < 60; ++e) {
        Message a; a.role = Role::Assistant;
        a.tool_calls.push_back(settled_edit("big" + std::to_string(e)));
        m.d.current.messages.push_back(std::move(a));
    }
    // 一辆小的NEWEST在它前面跑.
    Message lu; lu.role = Role::User; lu.text = "thanks";
    m.d.current.messages.push_back(std::move(lu));
    Message la; la.role = Role::Assistant; la.text = "you're welcome";
    m.d.current.messages.push_back(std::move(la));
    m.s.phase = agentty::phase::Idle{};

    agentty::app::detail::rehydrate_frozen(m);

    // 测试到底是为了什么: 不应该采取超大小的运行 整个。
    // 它曾经检查过 `rows < 1000` ——1000个是。
    // 远高于合法上限，但远低于曾出现的 2247 行错误值。
    //
    //
    // 水合物现在种子 a WIDER 窗口,
    // 因此画布在帖子前可以合法超过1000行 -paint
    // 修剪运行。行数不再不是变量 ; 检查 不变量 自己来
    //
    // 跑步的两种方式是防止一个不可分割的块, 或者是通过:
    // 保持 -walk CUT 或 freeze_range
    // SPLIT 输入到每个 -sub-turn 块( 这样修剪
    // 可以一次一次掉一个)。 这个测试对于生产出来的两个块都
    // 存在错误。 – 一个巨大的块, 每排都存在。
    const std::size_t rows    = m.ui.frozen.row_total();
    const std::size_t entries = m.ui.frozen.size();

    // 六十号 -tool 运行会是 一个 如果将块全部拆
    // 分或切除,则会留下许多块,所以输入数就是结果。
    CHECK_MESSAGE(entries > 4,
                  "frozen canvas is " << entries << " entries / " << rows
                  << " rows — an oversized run behind the newest one was "
                  "taken whole instead of being cut or split");

    // 任何单一块都无法支配:这就是画布的钉子,
    // 因为一个块是三分块的原子(它会掉下整块或没有).
    std::size_t tallest = 0;
    for (std::size_t k = 0; k < entries; ++k)
        tallest = std::max(tallest, m.ui.frozen.block_rows(k));
    CHECK_MESSAGE(tallest < 1000,
                  "one block is " << tallest << " rows — the trim can only "
                  "drop whole blocks, so this pins the canvas forever");

    // 画布必须仍然可用: 绑定它不能空空它。
    CHECK_MESSAGE(rows > 0, "the bound must keep SOME history, not none");
    CHECK(m.ui.frozen.size() > 0);
}

TEST_CASE("output elided tool row estimate matches render") {
    Model m;
    m.d.current.id = agentty::ThreadId{"bashcap"};
    Message u; u.role = Role::User; u.text = "run a noisy command";
    m.d.current.messages.push_back(std::move(u));
    Message a; a.role = Role::Assistant;
    agentty::ToolUse tb;
    tb.id   = agentty::ToolCallId{"bash_noisy"};
    tb.name = agentty::ToolName{"shell"};
    tb.args = {{"command", "grep -rn pattern src"}};
    std::string output;
    for (int i = 0; i < 200; ++i)
        output += "src/file" + std::to_string(i) + ".cpp:"
                + std::to_string(i) + ": some matching line of text\n";
    auto now = steady_clock::now();
    tb.status = agentty::ToolUse::Done{now - milliseconds{5}, now,
                                       std::move(output)};
    a.tool_calls.push_back(std::move(tb));
    m.d.current.messages.push_back(std::move(a));
    m.s.phase = agentty::phase::Idle{};

    agentty::app::detail::clear_frozen(m);
    agentty::app::detail::freeze_through(m, m.d.current.messages.size());

    // 击打卡 elides 200 输出线到 ~4 末尾 已存储的行
    // frozen_row_total 一定是在同一个球场上 真实 使
    // 高度——预 -fix 估计数 全部200行(~200+行),
    // 狂野地高于~15 -row 渲染.
    auto txt = render_dump(m);
    int real_rows = 0;
    for (char c : txt) if (c == '\n') ++real_rows;

    CHECK(m.ui.frozen.row_total() < static_cast<std::size_t>(real_rows) + 32,
          "bash output row estimate vastly over-counts the elided render "
          "(would trip the mid-run keep-loop into a ghost band)");
    // Sanity:正文仍然实际被制备(没有投放).
    CHECK(txt.find("file199.cpp") != std::string::npos
          || txt.find("file19") != std::string::npos,
          "bash tail rows missing from the render");
}

// 10b. 每个 输出-elided 工具的相同不变量,而不仅仅是击打.
//      上面的案子被砸了好几年 被推想者悄悄地不同意 渲染器 用于其他工具:
//      frozen.cpp 帽子是一只手,你用它来保护我 -written 姓名清单,
//      以及 git_status/log/commit 那时还在查
//      git_diff/show/blame/branch/stash/rebase/cherry_pick
//      很久没见了 `git diff` 曾经是 ESTIMATED 其输出
//      全高(~500行)和 RENDERED 滑动(~7),
//      结束 -count 这正是我们所要保持的 -loop 将仍在屏幕上的条目删除。
//
//      绕过工具,使得覆盖差距结构化,
//      而不是记住每个工具都加一个案例的问题。
TEST_CASE("output elided row estimate matches render for every such tool") {
    const char* kTools[] = {
        "shell", "diagnostics", "read", "find_definition",
        "grep", "glob", "list_dir", "web_search",
        "git_status", "git_diff", "git_log", "git_show", "git_blame",
        "git_commit", "git_branch", "git_stash", "git_rebase",
        "git_cherry_pick",
    };

    for (const char* tool_cstr : kTools) {
        const std::string tool{tool_cstr};
        CAPTURE(tool);
        Model m;
        m.d.current.id = agentty::ThreadId{"elide"};
        Message u; u.role = Role::User; u.text = "do a thing";
        m.d.current.messages.push_back(std::move(u));
        Message a; a.role = Role::Assistant;

        agentty::ToolUse tc;
        tc.id   = agentty::ToolCallId{tool + "_1"};
        tc.name = agentty::ToolName{tool};
        tc.args = nlohmann::json::object();
        std::string output;
        for (int i = 0; i < 400; ++i)
            output += "output line " + std::to_string(i)
                    + " with some trailing text to make it wrap-ish\n";
        auto now = steady_clock::now();
        tc.status = agentty::ToolUse::Done{now - milliseconds{5}, now,
                                           std::move(output)};
        a.tool_calls.push_back(std::move(tc));
        m.d.current.messages.push_back(std::move(a));
        m.s.phase = agentty::phase::Idle{};

        // REAL号机使整个记录的高度达到这个宽度。
        constexpr int kCols = 100;
        agentty::app::detail::clear_frozen(m);
        agentty::app::detail::freeze_through(m, m.d.current.messages.size());
        auto txt = render_dump(m, kCols);
        int real_rows = 0;
        for (char c : txt) if (c == '\n') ++real_rows;

        // 保存的-loop实际消耗的PREDICTION.
        // row_total()是从真实的渲染测量的,所以断言它不能捕捉到
        // 一个估计错误——estimate_msg_rows是测试中的函数.
        const std::size_t est =
            agentty::app::detail::estimate_msg_rows(
                m.d.current.messages[1], kCols);

        // 一个 -sided: UNDER-counting 只
        // 保存更多历史(安全); a OVER-count 保持
        // 原状 -loop 早点停下来,悄悄地放下它相信它所覆盖的历史
        // 渲染器 空隙没有限制——400行输出以~7行绘制.
        CHECK(est <= static_cast<std::size_t>(real_rows),
              tool << ": estimate " << est << " exceeds the real render "
                   << real_rows << " rows — frozen.cpp counts this tool's "
                      "full output while its renderer elides it");
    }
}
