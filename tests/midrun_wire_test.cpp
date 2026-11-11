// midrun_wire_test — 传输-LEVEL 回滚缓冲区-duplication 回归
//
// midrun_seam_test 渲染 每个模式状态 render_tree
// 比较行数组。 CONTENT/HEIGHT 歧义,但 不 实际
// 数 maya 内置 -compose 传输 排放量: render_tree
// 没有 prev_cells 阴影, 不溢出承诺,
// 也不使用光标。 截图错误( 已解析的写卡复制到 回滚缓冲区 )是单数。
// 传输 文物——冻结 帧 内容 -emits 已经溢出行
// 数 可视区域 上面,把旧副本搁在屏幕上
//
// 这个测试驱动器 maya's 真实 内置成像( 相同) compose_inline_
// 帧 编号 运行时 调用 结束 agentty '是真实元素' 源码树:
//
//   帧 答:现场直播 末尾 保存已解析的文字( 完整) 正文 ),溢出(" ⁇ ").
//            渲染 同步。一些行承诺本地 回滚缓冲区.
//   帧 B: 书写被冻结( freeze_through). 渲染 新的 源码树 反 对
//            相同 InlineFrameState.
//
// 不变量 : 数据 字节 帧 B 发射不得 包含一个 -paint
// 排行图 帧 已经推到上面了 可视区域 顶部,我们模拟 传输 作为行网格,
// 发射器变异; 任何写入行索引 < ( frameA_rows
// - term_h )是执着的 -scrollback 重写
// = 重复错误。

#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

#include "wire_pipe.hpp"

#include <nlohmann/json.hpp>

#include <maya/render/canvas.hpp>
#include <maya/render/renderer.hpp>
#include <maya/render/inline_frame.hpp>
#include <maya/render/serialize.hpp>
#include <maya/style/theme.hpp>
#include <maya/terminal/writer.hpp>
#include <maya/widget/app_layout.hpp>

#include "agtest.hpp"

#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/msg.hpp"
#include "agentty/runtime/panel/common.hpp"
#include "agentty/runtime/view/changes_strip.hpp"
#include "agentty/runtime/view/composer.hpp"
#include "agentty/runtime/view/panels.hpp"
#include "agentty/runtime/view/status_bar/status_bar.hpp"
#include "agentty/runtime/view/thread/conversation.hpp"
#include "agentty/runtime/view/thread/thread.hpp"
#include "agentty/runtime/view/view.hpp"

namespace pn = agentty::ui::panel;

using agentty::Model;
using agentty::Message;
using agentty::Role;
using agentty::ToolCallId;
using agentty::ToolName;
using agentty::ToolUse;
using std::chrono::steady_clock;
using std::chrono::milliseconds;

using namespace maya;
using namespace maya::inline_frame;

static int g_checks   = 0;


// ── Non-blocking pipe writer; the read end is drained per frame so we
//    准确记录每块所排放的 字节。
static std::pair<Writer, int> make_pipe_writer() { return wire_pipe::make_writer(); }
static std::string drain(int rfd) { return wire_pipe::drain(rfd); }

// 为当前模型状态构建对讲器 Element 源码树。
static maya::Element build_root(const Model& m) {
    return maya::AppLayout{{
        .thread        = agentty::ui::thread_config(m),
        .changes_strip = agentty::ui::changes_strip_config(m),
        .composer      = agentty::ui::composer_config(m),
        .status_bar    = agentty::ui::status_bar_config(m),
        .overlay       = std::nullopt,
    }}.build();
}

// 渲染 `root` 以显示其内容; 返回其内容。
static Canvas paint(const maya::Element& root, int width, StylePool& pool) {
    // 种子高; render_tree 与 auto_height 长它。
    Canvas c(width, 4000, &pool);
    c.clear();
    std::vector<layout::LayoutNode> nodes;
    maya::render_tree(root, c, pool, maya::theme::native, nodes, /*auto_height=*/true);
    return c;
}

static ToolUse settled_write(const std::string& tag, int n_lines) {
    ToolUse t;
    t.id   = ToolCallId{"write_" + tag};
    t.name = ToolName{"write"};
    std::string content;
    for (int i = 0; i < n_lines; ++i)
        content += "line " + std::to_string(i) + ": plausible file body text\n";
    t.args = {{"file_path", "/tmp/" + tag + ".md"}, {"content", content}};
    auto now = steady_clock::now();
    t.status = ToolUse::Done{now - milliseconds{5}, now,
                             "Created /tmp/" + tag + ".md"};
    return t;
}

// 解决 `read` 高高的纸牌 正文 (每条源线一行,滑至 head/tail
// 由 ToolBodyPreview ) 区别 id + per -line
// 包含如此的两张牌 相同 字节 大小还剩 渲染 不同 正文 —— 人均数
// -event hash_id 碰撞表面。
static ToolUse settled_read(const std::string& tag, int n_lines,
                            const std::string& path) {
    ToolUse t;
    t.id   = ToolCallId{"read_" + tag};
    t.name = ToolName{"read"};
    t.args = {{"path", path}};
    std::string out;
    for (int i = 0; i < n_lines; ++i)
        out += std::to_string(i) + ": " + tag + " source line text here\n";
    auto now = steady_clock::now();
    t.status = ToolUse::Done{now - milliseconds{5}, now, std::move(out)};
    return t;
}

// 该 截图情景 传输: a 唯一 输入 -flight 助理运行,
// 长得高一个 终端 每个子工具卡 -turn (模型读取了数个文件在一行).
// 无内容 中间 -run (agent_session —
// —整个多功能 -card 运行是 LIVE 末尾 和关于
// -lays-out 每隔一段时间 帧 将先前的牌从下图中滚动 可视
// 区域 顶部。
//
// 腐败截图上就是这个形状 许多人已经... 终端 read/bash
// 叠加在一条活跑中的卡片, 与流浪 存储单元 从一张卡上 正文 血流
// 到另一张卡片的脚上。 -event hash_id 每一块块
// 终端 已缓存卡 存储单元 如果卡片上方长出 将缓存卡片转换为新Y
// maya 单行 diff + will_scroll_off
// 防守必须保持 prev_cells 与锁在一起 传输 或承诺行获得
// 一个 过期 布莱特。
//
// 不变量每帧:阴影保持有效(verify()成功),
// 帧保持同步(无恢复重绘),没有一行已经溢出
// 可视区域顶部的PRIORXX帧被改写.
TEST_CASE("growing live run no committed rewrite") {
    constexpr int kWidth = 100;
    constexpr int kTermH = 30;

    Model m;
    m.d.current.id = agentty::ThreadId{"wiregrow"};
    Message u; u.role = Role::User; u.text = "investigate the renderer";
    m.d.current.messages.push_back(std::move(u));
    // 整个前转弯冻结在提交时( SessionStart 模拟);
    // 直播 末尾 谓 仅 输入 -flight 助理从下面跑
    agentty::app::detail::clear_frozen(m);
    agentty::app::detail::freeze_through(m, 1);
    m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};

    // 进 -flight 助理运行: 一个消息( sub) -turn
    // )每张工具卡,一次附上一张。 -turn 下个任务到来之前就
    // 解决它的工具问题——现实的帖子 -tool 占位符笼。
    StylePool pool;
    auto [writer, rfd] = make_pipe_writer();

    // 跟踪 传输 作为行网格: 字节 ,在它从 滚动时,
    // 可视区域 顶端不是可变 本地语 回滚缓冲区.
    // 我们 快照 每一个被承诺的前缀 并且要求它永远不变。
    std::vector<std::string> committed_wire;   // 已经删除的行 -screen

    auto rows_of = [&](const Canvas& c) {
        std::vector<std::string> rows;
        const int mr = c.max_content_row();
        for (int y = 0; y <= mr; ++y) {
            std::string line;
            for (int x = 0; x < kWidth; ++x) {
                char32_t ch = c.get(x, y).character;
                line.push_back(ch && ch < 128 ? static_cast<char>(ch) : ' ');
            }
            while (!line.empty() && line.back() == ' ') line.pop_back();
            rows.push_back(std::move(line));
        }
        return rows;
    };

    // 种子初创帧,然后将同步状态带过附件.
    std::optional<InlineFrame<Synced>> synced;
    const char* files[] = {
        "third_party/maya/src/render/serialize.cpp", "third_party/maya/src/render/renderer.cpp",
        "third_party/maya/include/maya/app/app.hpp", "src/runtime/app/update/frozen.cpp",
        "src/runtime/app/update/stream.cpp", "third_party/maya/examples/agent_session.cpp",
    };
    constexpr int kCards = 6;
    bool ok_all = true;

    for (int card = 0; card < kCards && ok_all; ++card) {
        // 添加一个新的子 -turn 高个子 终端 读卡。 正文 备注
        // VARYING 高度, 每个附件会按不同的多位移动下方的行
        // -row 三角洲( 演习) will_scroll_off 范围).
        Message a; a.role = Role::Assistant;
        a.tool_calls.push_back(settled_read(
            "c" + std::to_string(card), 30 + card * 17, files[card]));
        m.d.current.messages.push_back(std::move(a));

        Canvas c = paint(build_root(m), kWidth, pool);
        auto cur_rows = rows_of(c);
        const int total_rows = static_cast<int>(cur_rows.size());
        const int new_committed = total_rows > kTermH ? total_rows - kTermH : 0;

        if (!synced) {
            auto outcome = InlineFrame<Empty>{}.seed().render(
                c, content_rows(c), term_rows_for_test(kTermH), pool, writer,
                false);
            (void)drain(rfd);
            synced = std::visit(
                [](auto&& arm) -> std::optional<InlineFrame<Synced>> {
                    using T = std::decay_t<decltype(arm)>;
                    if constexpr (std::is_same_v<T, InlineFrame<Synced>>)
                        return std::move(arm);
                    else return std::nullopt;
                }, std::move(outcome));
            CHECK(synced.has_value(), "grow: first frame did not reach Synced");
            if (!synced) { ok_all = false; break; }
        } else {
            auto wit = synced->verify();
            CHECK(wit.has_value(),
                  "grow: shadow verify failed before append render "
                  "(prev_cells already desynced from the wire)");
            if (!wit) { ok_all = false; break; }
            auto proof = synced->check_scrollback(c, kTermH);
            CHECK(proof.has_value(),
                  "grow: scrollback gate rejected the append frame "
                  "(committed prefix shifted)");
            if (!proof) { ok_all = false; break; }
            auto outcome = std::move(*synced).render(
                c, content_rows(c), term_rows_for_test(kTermH), pool, writer,
                std::move(*wit), std::move(*proof), false);
            (void)drain(rfd);
            synced = std::visit(
                [](auto&& arm) -> std::optional<InlineFrame<Synced>> {
                    using T = std::decay_t<decltype(arm)>;
                    if constexpr (std::is_same_v<T, InlineFrame<Synced>>)
                        return std::move(arm);
                    else return std::nullopt;
                }, std::move(outcome));
            CHECK(synced.has_value(),
                  "grow: append frame demoted out of Synced — maya hit the "
                  "overflow recovery repaint (the bleed/ghost symptom)");
            if (!synced) { ok_all = false; break; }
        }

        // 每一行的罪状 PRIOR 帧 一定要坚持住 相同 字节 现在,
        // 改变就是承诺 -scrollback 重写——
        // 流浪汉 -cell 截图上的血迹
        const int check_n = std::min<int>(
            static_cast<int>(committed_wire.size()), new_committed);
        for (int y = 0; y < check_n; ++y) {
            if (committed_wire[static_cast<std::size_t>(y)]
                != cur_rows[static_cast<std::size_t>(y)]) {
                std::fprintf(stderr,
                    "  GROW committed-row rewrite at y=%d (card=%d):\n"
                    "    was |%s|\n    now |%s|\n",
                    y, card,
                    committed_wire[static_cast<std::size_t>(y)].c_str(),
                    cur_rows[static_cast<std::size_t>(y)].c_str());
                CHECK(false,
                      "a row that already overflowed the viewport top was "
                      "rewritten on a later frame — committed-scrollback "
                      "corruption (the screenshot bleed)");
                ok_all = false;
                break;
            }
        }
        // 将承诺的快照扩展至新的(更深的)边界.
        committed_wire.assign(cur_rows.begin(),
                              cur_rows.begin() + new_committed);
    }

    std::fprintf(stderr, "  [grow] cards=%d committed_rows=%zu\n",
                 kCards, committed_wire.size());
    close(rfd);
}

// 输出参考的 grep卡 `path` ,在给定的行号,以 agentty 标记下方形状(
// Grep ) collect_grep_hits 解析
// `## Matches in <path>` 头文件 + `### L<n>` 锚).
static ToolUse settled_grep(const std::string& tag, const std::string& path,
                            const std::vector<int>& lines) {
    ToolUse t;
    t.id   = ToolCallId{"grep_" + tag};
    t.name = ToolName{"grep"};
    t.args = {{"pattern", "needle_" + tag}};
    std::string out = "## Matches in " + path + "\n\n";
    for (int ln : lines)
        out += "### L" + std::to_string(ln) + "-" + std::to_string(ln)
             + "\n```\n" + std::to_string(ln) + ": match here\n```\n\n";
    auto now = steady_clock::now();
    t.status = ToolUse::Done{now - milliseconds{5}, now, std::move(out)};
    return t;
}

// REGRESSION : grep – 读于同一路径。读卡会结算,
// 并且是 rendered/cached 首次 (没有亮度),然后, -path
// 格拉普在逃难后降落: collect_grep_hits 现在索引路径,
// 所以读 正文 获得领先地位 `▸ matches: …` 简列 - 一个
// 高点的排队——而 tc.output().size() 谓 UNCHANGED 数字。
// -event hash_id 因此,必须 归并 highlight_lines ,
// 或 ,或 maya 缓存( 缩短) 中 存储单元 进入现在 -taller
// 保留空档,每行下移一行,流血 过期 存储单元 我们开着它, 真实 组件
// 缓存 (paint() 共享 线程-local 缓存 横跨 帧 如此的,
// 一个 过期 血会浮出水面的
TEST_CASE("grep read highlight no stale blit") {
    constexpr int kWidth = 100;
    constexpr int kTermH = 30;
    const std::string path = "third_party/maya/src/render/serialize.cpp";

    Model m;
    m.d.current.id = agentty::ThreadId{"wirehl"};
    Message u; u.role = Role::User; u.text = "find and read the diff path";
    m.d.current.messages.push_back(std::move(u));
    // 铅 -in 冷冻的转动将现场阅读卡的顶部推到上面 可视
    // 区域 故其行必归本族; 回滚缓冲区 和一个 -row a/
    // 下调 过期 黑点会腐蚀 a COMMITTED 划线
    for (int e = 0; e < 8; ++e) {
        Message lu; lu.role = Role::User;
        lu.text = "context turn " + std::to_string(e);
        m.d.current.messages.push_back(std::move(lu));
        Message la; la.role = Role::Assistant;
        la.tool_calls.push_back(settled_write("lead" + std::to_string(e), 6));
        m.d.current.messages.push_back(std::move(la));
    }
    agentty::app::detail::clear_frozen(m);
    agentty::app::detail::freeze_through(m, m.d.current.messages.size());
    m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};

    StylePool pool;
    auto [writer, rfd] = make_pipe_writer();

    auto rows_of = [&](const Canvas& c) {
        std::vector<std::string> rows;
        const int mr = c.max_content_row();
        for (int y = 0; y <= mr; ++y) {
            std::string line;
            for (int x = 0; x < kWidth; ++x) {
                char32_t ch = c.get(x, y).character;
                line.push_back(ch && ch < 128 ? static_cast<char>(ch) : ' ');
            }
            while (!line.empty() && line.back() == ' ') line.pop_back();
            rows.push_back(std::move(line));
        }
        return rows;
    };

    // 子类 -turn 1: 读卡结算 首次 ,单独保存,
    // 并被缓存(每 -event hash_id 键入于无 -highlight 高度。
    // 将突出它降落在 相同 子目录 -turn 后传一个节拍(
    // 模型在一个工具批次中发出 + grep; 读取) tool_use
    // 到达和定居 在grep's做之前。
    {
        Message a; a.role = Role::Assistant;
        a.tool_calls.push_back(settled_read("r", 60, path));
        m.d.current.messages.push_back(std::move(a));
    }
    Canvas ca = paint(build_root(m), kWidth, pool);
    auto rows_a = rows_of(ca);
    auto oa = InlineFrame<Empty>{}.seed().render(
        ca, content_rows(ca), term_rows_for_test(kTermH), pool, writer, false);
    (void)drain(rfd);
    InlineFrame<Synced> sa = std::visit(
        [](auto&& arm) -> InlineFrame<Synced> {
            using T = std::decay_t<decltype(arm)>;
            if constexpr (std::is_same_v<T, InlineFrame<Synced>>)
                return std::move(arm);
            else { std::fprintf(stderr, "  hl frame A not Synced\n");
                   std::abort(); }
        }, std::move(oa));
    const int rows_a_n   = sa.rows();
    const int committed_a = rows_a_n > kTermH ? rows_a_n - kTermH : 0;

    // 树皮在地上 相同 消息的工具批次。 collect_grep_hits (每笔)
    // -panel ,通过消息的 tool_calls )现在索引路径,
    // 因此上面的读取继承 highlight_lines − 其 类 正文 由 长 着 着
    // `▸ matches:` 简列。
    m.d.current.messages.back().tool_calls.push_back(
        settled_grep("g", path, {42, 61, 88}));
    Canvas cb = paint(build_root(m), kWidth, pool);
    auto rows_b = rows_of(cb);
    auto wit = sa.verify();
    CHECK(wit.has_value(),
          "hl: shadow verify failed after frame A (already desynced)");
    if (wit) {
        auto proof = sa.check_scrollback(cb, kTermH);
        CHECK(proof.has_value(),
              "hl: scrollback gate rejected the grep-append frame");
        auto ob = std::move(sa).render(
            cb, content_rows(cb), term_rows_for_test(kTermH), pool, writer,
            std::move(*wit), std::move(*proof), false);
        (void)drain(rfd);
        bool synced_b = std::visit([](auto&& arm) {
            using T = std::decay_t<decltype(arm)>;
            return std::is_same_v<T, InlineFrame<Synced>>;
        }, std::move(ob));
        CHECK(synced_b,
              "hl: grep-append frame demoted out of Synced — stale-blit "
              "height mismatch tripped the recovery repaint");
    }

    // 执着的前缀( 溢出在 帧 A) 须 不 已重写为 帧 B.
    // 如果 -event 键被忽略 highlight_lines ,
    // 读数 正文's 存储单元 将短短一行划为更高的槽,
    // 转移承诺的行。
    const int check_n = std::min<int>(committed_a,
        std::min<int>(static_cast<int>(rows_a.size()),
                      static_cast<int>(rows_b.size())));
    int first_div = -1;
    for (int y = 0; y < check_n; ++y) {
        if (rows_a[static_cast<std::size_t>(y)]
            != rows_b[static_cast<std::size_t>(y)]) { first_div = y; break; }
    }
    if (first_div >= 0) {
        std::fprintf(stderr,
            "  HL committed-row divergence at %d (committed=%d):\n"
            "    A |%s|\n    B |%s|\n",
            first_div, committed_a,
            rows_a[static_cast<std::size_t>(first_div)].c_str(),
            rows_b[static_cast<std::size_t>(first_div)].c_str());
    }
    CHECK(first_div < 0,
          "a committed scrollback row changed when a same-path Grep added "
          "the read's highlight summary row — stale-blit height mismatch "
          "(per-event hash_id must fold highlight_lines)");

    // DIRECT 过期-blit 检查: re -PAINT 帧 B's
    // 源码树 进入一个新鲜的画布,分享 相同 线程-local 组件 缓存
    // 编号 帧-A 彩绘有素,如果读数是每张 -event
    // hash_id ⊿Τ 归并 highlight_lines 画画画画
    // 缓存 FAST PATH 并打碎了这个 帧-A 存储单元 (无)
    // `▸ matches:` 排,短一行)进入现在的更高预留槽——画布
    // 然后显示读数 正文 最后一条沟渠,它在哪里 footer/`done`
    // 行应该是( 截图的偏差线) -number + `DO2`/`DONE`
    // 我们断言画的画布包含摘要行,即: 正文 必须 现已有;a 过期 布
    // 莱特省略了它。
    Canvas cc = paint(build_root(m), kWidth, pool);   // 缓存-hit 油漆
    auto rows_c = rows_of(cc);
    bool has_matches_row = false;
    for (const auto& r : rows_c)
        if (r.find("matches:") != std::string::npos) { has_matches_row = true; break; }
    CHECK(has_matches_row,
          "the read body's `▸ matches:` summary row is MISSING from the "
          "cache-hit repaint — maya blitted the stale (pre-highlight) cells "
          "because the per-event hash_id ignored highlight_lines (the "
          "screenshot stale-blit / gutter-bleed corruption)");
    // 连续两次 帧-B 油漆(cb 新建,cc) 缓存-hit 必须是 字节
    // -identical: a 过期 blit 使 cc 缩短 / 移动。
    int cdiv = -1;
    const int cmin = std::min<int>(static_cast<int>(rows_b.size()),
                                   static_cast<int>(rows_c.size()));
    for (int y = 0; y < cmin; ++y)
        if (rows_b[static_cast<std::size_t>(y)]
            != rows_c[static_cast<std::size_t>(y)]) { cdiv = y; break; }
    if (cdiv >= 0)
        std::fprintf(stderr,
            "  HL cache-hit repaint diverged at %d:\n    fresh |%s|\n"
            "    cached|%s|\n", cdiv,
            rows_b[static_cast<std::size_t>(cdiv)].c_str(),
            rows_c[static_cast<std::size_t>(cdiv)].c_str());
    CHECK(cdiv < 0 && rows_b.size() == rows_c.size(),
          "cache-hit repaint of the same tree diverged from the fresh "
          "paint — a stale-height blit corrupted the canvas");

    std::fprintf(stderr, "  [hl] committed=%d rows_a=%d rows_b=%zu rows_c=%zu\n",
                 committed_a, rows_a_n, rows_b.size(), rows_c.size());
    close(rfd);
}

// 该 传输-level 测试。用户转动 + 已解析的文字,
// 超过 可视区域 ,在(idle)转弯时被冻结。 maya
// 真正的内线 构成整个冻结 并断言没有承诺 回滚缓冲区
// 行重写.
TEST_CASE("write freeze no rewrite") {
    constexpr int kWidth = 100;
    constexpr int kTermH = 30;

    Model m;
    m.d.current.id = agentty::ThreadId{"wire"};
    Message u; u.role = Role::User; u.text = "write a big file";
    m.d.current.messages.push_back(std::move(u));
    agentty::app::detail::clear_frozen(m);
    agentty::app::detail::freeze_through(m, 1);

    // 已解决的写作为活动转弯唯一的子 -turn. 数据流 闲置( E) finalize_turn
    // 路径 : m.s 不 活动,所以它会一直存在到 freeze_through。
    // 这个镜像 "model wrote a file then stopped."
    Message a; a.role = Role::Assistant;
    a.tool_calls.push_back(settled_write("big", 120));
    m.d.current.messages.push_back(std::move(a));
    // 默认状态闲置( 无) 流式传输 {行动}}。

    StylePool pool;
    auto [writer, rfd] = make_pipe_writer();

    // 帧 A：渲染实时尾部中已稳定的 write，初始化 Empty→Fresh→Synced。
    Canvas ca = paint(build_root(m), kWidth, pool);
    auto outcome_a = InlineFrame<Empty>{}.seed().render(
        ca, content_rows(ca), term_rows_for_test(kTermH), pool, writer,
        /*同步=( ===*/false);
    (void)drain(rfd);

    InlineFrame<Synced> sa = std::visit(
        [](auto&& arm) -> InlineFrame<Synced> {
            using T = std::decay_t<decltype(arm)>;
            if constexpr (std::is_same_v<T, InlineFrame<Synced>>)
                return std::move(arm);
            else { std::fprintf(stderr, "  frame A did not reach Synced\n");
                   std::abort(); }
        }, std::move(outcome_a));

    const int rows_a = sa.rows();
    const int committed_a = rows_a > kTermH ? rows_a - kTermH : 0;
    CHECK(committed_a > 0,
          "test setup: live write must overflow the viewport "
          "(else there's no committed scrollback to corrupt)");

    // 冻结：write 通过空闲路径并入冻结前缀。
    agentty::app::detail::freeze_through(m, m.d.current.messages.size());

    // 帧 B：使用相同状态渲染冻结树，并验证
    //    阴影先(模仿 运行时 手动,然后 渲染.
    Canvas cb = paint(build_root(m), kWidth, pool);
    auto wit = sa.verify();
    CHECK(wit.has_value(),
          "shadow verify failed after frame A (state already poisoned)");

    std::string bytes_b;
    if (wit) {
        auto proof = sa.check_scrollback(cb, kTermH);
        CHECK(proof.has_value(),
              "freeze: scrollback gate rejected the frozen frame");
        auto outcome_b = std::move(sa).render(
            cb, content_rows(cb), term_rows_for_test(kTermH), pool, writer,
            std::move(*wit), std::move(*proof), /*同步=( ===*/false);
        bytes_b = drain(rfd);
        // 一定要保持同步,这里降级意味着 maya 本身检测到溢出
        // -while-poisoned 正在恢复中(可见闪烁)。
        bool synced_b = std::visit([](auto&& arm) {
            using T = std::decay_t<decltype(arm)>;
            return std::is_same_v<T, InlineFrame<Synced>>;
        }, std::move(outcome_b));
        CHECK(synced_b,
              "freeze frame demoted out of Synced — maya hit a recovery "
              "path (overflow-while-poisoned), the duplicate-write symptom");
    }

    // 帧 B 的内容高于 可视区域 顶部必须匹配 帧 冰冻
    // 的 渲染 写作必须 字节-identical 给活的
    // 我们是 -derive 既以行字符串的形式绘制,又比较已承诺的前缀 [0],
    // committed_a).
    auto rows_of = [&](const Canvas& c) {
        std::vector<std::string> rows;
        const int mr = c.max_content_row();
        for (int y = 0; y <= mr; ++y) {
            std::string line;
            for (int x = 0; x < kWidth; ++x) {
                char32_t ch = c.get(x, y).character;
                line.push_back(ch && ch < 128 ? static_cast<char>(ch) : ' ');
            }
            while (!line.empty() && line.back() == ' ') line.pop_back();
            rows.push_back(std::move(line));
        }
        return rows;
    };
    auto ra = rows_of(ca);
    auto rb = rows_of(cb);
    int first_div = -1;
    for (int y = 0; y < committed_a && y < (int)rb.size(); ++y) {
        if (y >= (int)ra.size() || ra[y] != rb[y]) { first_div = y; break; }
    }
    if (first_div >= 0) {
        std::fprintf(stderr,
            "  committed-row divergence at %d (rows_a=%d committed=%d):\n"
            "    A |%s|\n    B |%s|\n",
            first_div, rows_a, committed_a,
            first_div < (int)ra.size() ? ra[first_div].c_str() : "<none>",
            first_div < (int)rb.size() ? rb[first_div].c_str() : "<none>");
    }
    CHECK(first_div < 0,
          "frozen render diverges from live render in the committed "
          "scrollback prefix — the stranded-duplicate root cause");

    close(rfd);
}

// SETTLE冻结并延续:写完后,一个占位符文本(sub
// -turn)流出,然后转弯结束,
// 单个结算-time冻结一次取WHOLE运行(agent_session
// 的MessageStop模拟). 同一传输 不变量:冻结不得
// 重写承诺的回滚缓冲区行.
TEST_CASE("write midrun active freeze no rewrite") {
    constexpr int kWidth = 100;
    constexpr int kTermH = 30;

    Model m;
    m.d.current.id = agentty::ThreadId{"wiremid"};
    Message u; u.role = Role::User; u.text = "write a big file then continue";
    m.d.current.messages.push_back(std::move(u));
    agentty::app::detail::clear_frozen(m);
    agentty::app::detail::freeze_through(m, 1);

    // 已解决写入子 -turn + a 键 活动 流式传输 后面的占位符
    // (继续) 状态更新函数 推后脚 -tool). 数据流 正在激活。
    Message a; a.role = Role::Assistant;
    a.tool_calls.push_back(settled_write("bigmid", 120));
    m.d.current.messages.push_back(std::move(a));
    Message ph; ph.role = Role::Assistant; ph.streaming_text = "continuing";
    m.d.current.messages.push_back(std::move(ph));
    m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};

    StylePool pool;
    auto [writer, rfd] = make_pipe_writer();

    // 帧 答:现场直播 末尾 = [固定写 [活式占位符]. 流过.
    Canvas ca = paint(build_root(m), kWidth, pool);
    auto outcome_a = InlineFrame<Empty>{}.seed().render(
        ca, content_rows(ca), term_rows_for_test(kTermH), pool, writer, false);
    (void)drain(rfd);
    InlineFrame<Synced> sa = std::visit(
        [](auto&& arm) -> InlineFrame<Synced> {
            using T = std::decay_t<decltype(arm)>;
            if constexpr (std::is_same_v<T, InlineFrame<Synced>>)
                return std::move(arm);
            else { std::fprintf(stderr, "  midrun frame A not Synced\n");
                   std::abort(); }
        }, std::move(outcome_a));

    const int rows_a = sa.rows();
    const int committed_a = rows_a > kTermH ? rows_a - kTermH : 0;
    CHECK(committed_a > 0,
          "midrun setup: live write must overflow the viewport");

    // 中间 -run 无冻结( N) agent_session。
    // 回合和解:占位符文本输入、相位闲置、
    // 单位和解 -time 冻死整个运行。
    {
        auto& back = m.d.current.messages.back();
        back.text = std::move(back.streaming_text);
        back.streaming_text.clear();
    }
    m.s.phase = agentty::phase::Idle{};
    agentty::app::detail::freeze_through(m, m.d.current.messages.size());

    // 帧 B:冷冻前缀+活占位符,对同状态.
    Canvas cb = paint(build_root(m), kWidth, pool);
    auto wit = sa.verify();
    CHECK(wit.has_value(), "midrun shadow verify failed after frame A");
    if (wit) {
        auto proof = sa.check_scrollback(cb, kTermH);
        CHECK(proof.has_value(),
              "midrun: scrollback gate rejected the frozen frame");
        auto outcome_b = std::move(sa).render(
            cb, content_rows(cb), term_rows_for_test(kTermH), pool, writer,
            std::move(*wit), std::move(*proof), false);
        (void)drain(rfd);
        bool synced_b = std::visit([](auto&& arm) {
            using T = std::decay_t<decltype(arm)>;
            return std::is_same_v<T, InlineFrame<Synced>>;
        }, std::move(outcome_b));
        CHECK(synced_b,
              "midrun freeze frame demoted out of Synced — recovery path "
              "hit (the duplicate-write symptom)");
    }

    auto rows_of = [&](const Canvas& c) {
        std::vector<std::string> rows;
        const int mr = c.max_content_row();
        for (int y = 0; y <= mr; ++y) {
            std::string line;
            for (int x = 0; x < kWidth; ++x) {
                char32_t ch = c.get(x, y).character;
                line.push_back(ch && ch < 128 ? static_cast<char>(ch) : ' ');
            }
            while (!line.empty() && line.back() == ' ') line.pop_back();
            rows.push_back(std::move(line));
        }
        return rows;
    };
    auto ra = rows_of(ca);
    auto rb = rows_of(cb);
    int first_div = -1;
    for (int y = 0; y < committed_a && y < (int)rb.size(); ++y) {
        if (y >= (int)ra.size() || ra[y] != rb[y]) { first_div = y; break; }
    }
    if (first_div >= 0) {
        std::fprintf(stderr,
            "  MIDRUN committed-row divergence at %d (rows_a=%d committed=%d):\n"
            "    A |%s|\n    B |%s|\n",
            first_div, rows_a, committed_a,
            first_div < (int)ra.size() ? ra[first_div].c_str() : "<none>",
            first_div < (int)rb.size() ? rb[first_div].c_str() : "<none>");
    }
    CHECK(first_div < 0,
          "MIDRUN frozen render diverges from live render in the committed "
          "scrollback prefix — stranded duplicate while stream active");

    close(rfd);
}

// 完整 寿命周期 传输: 流式传输 (已关闭) - >
// 结算( 完全) 正文 ) - > 冻结。
// 三个构件对一个正在演变的状态。 流式传输 牌是紧凑的,
// 在结算时会完全扩展。 正文 冻结它,
// 所有承诺 回滚缓冲区 排队必须活过两个 定居扩张 和。
// 这是最接近真实截图序列的模型。
TEST_CASE("write streaming settle freeze") {
    constexpr int kWidth = 100;
    constexpr int kTermH = 30;

    Model m;
    m.d.current.id = agentty::ThreadId{"wirelife"};
    Message u; u.role = Role::User; u.text = "write a big file";
    m.d.current.messages.push_back(std::move(u));
    agentty::app::detail::clear_frozen(m);
    agentty::app::detail::freeze_through(m, 1);

    // 铅 -in 冷冻的转动,所以写卡的顶部 被推到上面
    // 可视区域 顶端 之前 确是安定的,确是有行的。
    // 完整的用户+写入交换器冻结在它们结算的边界上——这是现在
    // 内容达到m.ui.frozen的唯一方式.
    for (int e = 0; e < 6; ++e) {
        Message lu; lu.role = Role::User;
        lu.text = "write lead file " + std::to_string(e);
        m.d.current.messages.push_back(std::move(lu));
        Message la; la.role = Role::Assistant;
        la.tool_calls.push_back(settled_write("lead" + std::to_string(e), 4));
        m.d.current.messages.push_back(std::move(la));
    }
    agentty::app::detail::freeze_through(m, m.d.current.messages.size());
    m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};

    std::string content;
    for (int i = 0; i < 120; ++i)
        content += "line " + std::to_string(i) + ": plausible file body text\n";

    StylePool pool;
    auto [writer, rfd] = make_pipe_writer();

    // 帧 A：write 仍在流式运行，卡片采用窗口化紧凑显示。
    {
        Message a; a.role = Role::Assistant;
        ToolUse t;
        t.id = ToolCallId{"wlife"}; t.name = ToolName{"write"};
        t.args = {{"file_path", "/tmp/life.md"}, {"content", content}};
        t.status = ToolUse::Running{steady_clock::now(), ""};
        a.tool_calls.push_back(std::move(t));
        Message ph; ph.role = Role::Assistant; ph.streaming_text = "...";
        m.d.current.messages.push_back(std::move(a));
        m.d.current.messages.push_back(std::move(ph));
    }
    Canvas ca = paint(build_root(m), kWidth, pool);
    auto oa = InlineFrame<Empty>{}.seed().render(
        ca, content_rows(ca), term_rows_for_test(kTermH), pool, writer, false);
    (void)drain(rfd);
    InlineFrame<Synced> s = std::visit(
        [](auto&& arm) -> InlineFrame<Synced> {
            using T = std::decay_t<decltype(arm)>;
            if constexpr (std::is_same_v<T, InlineFrame<Synced>>) return std::move(arm);
            else { std::fprintf(stderr, "  life frame A not Synced\n"); std::abort(); }
        }, std::move(oa));

    auto rows_of = [&](const Canvas& c) {
        std::vector<std::string> rows;
        const int mr = c.max_content_row();
        for (int y = 0; y <= mr; ++y) {
            std::string line;
            for (int x = 0; x < kWidth; ++x) {
                char32_t ch = c.get(x, y).character;
                line.push_back(ch && ch < 128 ? static_cast<char>(ch) : ' ');
            }
            while (!line.empty() && line.back() == ' ') line.pop_back();
            rows.push_back(std::move(line));
        }
        return rows;
    };
    auto check_prefix = [&](const char* tag, const std::vector<std::string>& prev,
                            const Canvas& cur_canvas, int prev_rows) {
        const int committed = prev_rows > kTermH ? prev_rows - kTermH : 0;
        auto cur = rows_of(cur_canvas);
        int d = -1;
        for (int y = 0; y < committed && y < (int)cur.size(); ++y)
            if (y >= (int)prev.size() || prev[y] != cur[y]) { d = y; break; }
        if (d >= 0)
            std::fprintf(stderr,
                "  %s committed divergence at %d (committed=%d)\n"
                "    PREV |%s|\n    CUR  |%s|\n", tag, d, committed,
                d < (int)prev.size() ? prev[d].c_str() : "<none>",
                d < (int)cur.size()  ? cur[d].c_str()  : "<none>");
        CHECK(d < 0, tag);
    };

    // 帧 B：write 完成进入 Done，实时尾部中的正文展开。
    auto prev_a = rows_of(ca);
    const int rows_a = s.rows();
    agentty::app::detail::with_live_tool(
        m, ToolCallId{"wlife"}, [&](ToolUse& t) {
            auto now = steady_clock::now();
            t.status = ToolUse::Done{now - milliseconds{5}, now, "Created /tmp/life.md"};
        });
    Canvas cb = paint(build_root(m), kWidth, pool);
    {
        auto wit = s.verify();
        CHECK(wit.has_value(), "life shadow verify failed after A");
        if (wit) {
            auto proof = s.check_scrollback(cb, kTermH);
            CHECK(proof.has_value(),
                  "life: scrollback gate rejected the settle frame");
            auto ob = std::move(s).render(
                cb, content_rows(cb), term_rows_for_test(kTermH), pool, writer,
                std::move(*wit), std::move(*proof), false);
            (void)drain(rfd);
            s = std::visit([](auto&& arm) -> InlineFrame<Synced> {
                using T = std::decay_t<decltype(arm)>;
                if constexpr (std::is_same_v<T, InlineFrame<Synced>>) return std::move(arm);
                else { std::fprintf(stderr,
                    "  life Running->Done demoted out of Synced\n"); std::abort(); }
            }, std::move(ob));
        }
    }
    check_prefix("life Running->Done rewrote a committed row", prev_a, cb, rows_a);

    // 帧 C：轮次收尾，执行唯一的收尾冻结。
    auto prev_b = rows_of(cb);
    const int rows_b = s.rows();
    {
        auto& back = m.d.current.messages.back();
        back.text = std::move(back.streaming_text);
        back.streaming_text.clear();
    }
    m.s.phase = agentty::phase::Idle{};
    agentty::app::detail::freeze_through(m, m.d.current.messages.size());
    Canvas cc = paint(build_root(m), kWidth, pool);
    {
        auto wit = s.verify();
        CHECK(wit.has_value(), "life shadow verify failed after B");
        if (wit) {
            auto proof = s.check_scrollback(cc, kTermH);
            CHECK(proof.has_value(),
                  "life: scrollback gate rejected the freeze frame");
            auto oc = std::move(s).render(
                cc, content_rows(cc), term_rows_for_test(kTermH), pool, writer,
                std::move(*wit), std::move(*proof), false);
            (void)drain(rfd);
            bool synced = std::visit([](auto&& arm) {
                using T = std::decay_t<decltype(arm)>;
                return std::is_same_v<T, InlineFrame<Synced>>;
            }, std::move(oc));
            CHECK(synced, "life Done->freeze demoted out of Synced");
        }
    }
    check_prefix("life Done->freeze rewrote a committed row", prev_b, cc, rows_b);

    close(rfd);
}

// 该 真实 StreamFinished 路径 : 帧 A 渲染 现场
// 直播 末尾 时 数据流 谓 活动 (上次运行保留一项活动)
// -indicator 插槽和运行不是 缓存是因为 reserve_slot 然则.
// 数据流 闲着 和 freeze_through 火在同一处 状态更新函数 步骤。
// 帧 B 渲染 冻结前缀+空活字 末尾。如果活动者活着 帧 曾经是
// TALLER 而不是冰冻 帧 (保留槽/活铬掉落),
// 帧 SHRINKS 溢出时 - > maya 收缩 -guard
// 承付溢额+ demote_to_stale - > 案例-(
// B) 重绘 它可以拖动重复。这是精确的截图情景。
TEST_CASE("write idle finalize freeze") {
    constexpr int kWidth = 100;
    constexpr int kTermH = 30;

    Model m;
    m.d.current.id = agentty::ThreadId{"wireidle"};
    Message u; u.role = Role::User; u.text = "write a big file";
    m.d.current.messages.push_back(std::move(u));
    agentty::app::detail::clear_frozen(m);
    agentty::app::detail::freeze_through(m, 1);

    // 已解决的写作作为活动转弯唯一的子-turn,数据流 活动(工具返回完成后,
    // StreamFinished翻转相向Idle).
    // is_last_run + 活动 QQ reserve_slot路径.
    Message a; a.role = Role::Assistant;
    a.tool_calls.push_back(settled_write("idle", 120));
    m.d.current.messages.push_back(std::move(a));
    m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};

    StylePool pool;
    auto [writer, rfd] = make_pipe_writer();

    // 帧 A: 活动 现场直播 渲染 写着写着写著的
    Canvas ca = paint(build_root(m), kWidth, pool);
    auto oa = InlineFrame<Empty>{}.seed().render(
        ca, content_rows(ca), term_rows_for_test(kTermH), pool, writer, false);
    (void)drain(rfd);
    InlineFrame<Synced> s = std::visit(
        [](auto&& arm) -> InlineFrame<Synced> {
            using T = std::decay_t<decltype(arm)>;
            if constexpr (std::is_same_v<T, InlineFrame<Synced>>) return std::move(arm);
            else { std::fprintf(stderr, "  idle frame A not Synced\n"); std::abort(); }
        }, std::move(oa));
    const int rows_a = s.rows();
    const int committed_a = rows_a > kTermH ? rows_a - kTermH : 0;
    CHECK(committed_a > 0, "idle setup: live write must overflow viewport");

    auto rows_of = [&](const Canvas& c) {
        std::vector<std::string> rows;
        const int mr = c.max_content_row();
        for (int y = 0; y <= mr; ++y) {
            std::string line;
            for (int x = 0; x < kWidth; ++x) {
                char32_t ch = c.get(x, y).character;
                line.push_back(ch && ch < 128 ? static_cast<char>(ch) : ' ');
            }
            while (!line.empty() && line.back() == ' ') line.pop_back();
            rows.push_back(std::move(line));
        }
        return rows;
    };
    auto prev_a = rows_of(ca);

    // StreamFinished: 相 - > 离子,然后冻结整个运行。 同一步骤。
    m.s.phase = agentty::phase::Idle{};
    agentty::app::detail::freeze_through(m, m.d.current.messages.size());

    // 帧 B: 冻结前缀+空直播 末尾.
    Canvas cb = paint(build_root(m), kWidth, pool);
    const int rows_b = (int)rows_of(cb).size();
    std::fprintf(stderr, "  [idle] rows_a=%d rows_b=%d committed_a=%d\n",
                 rows_a, rows_b, committed_a);
    CHECK(rows_b >= rows_a,
          "FREEZE SHRANK the frame (rows_b < rows_a) while overflowed — "
          "maya's shrink-guard fires commit+demote_to_stale -> case-(B) "
          "repaint that strands a duplicate. The live tail carried chrome "
          "(activity slot / gap) the frozen build dropped.");

    auto wit = s.verify();
    CHECK(wit.has_value(), "idle shadow verify failed after A");
    if (wit) {
        auto proof = s.check_scrollback(cb, kTermH);
        CHECK(proof.has_value(),
              "idle: scrollback gate rejected the freeze frame");
        auto ob = std::move(s).render(
            cb, content_rows(cb), term_rows_for_test(kTermH), pool, writer,
            std::move(*wit), std::move(*proof), false);
        (void)drain(rfd);
        bool synced = std::visit([](auto&& arm) {
            using T = std::decay_t<decltype(arm)>;
            return std::is_same_v<T, InlineFrame<Synced>>;
        }, std::move(ob));
        CHECK(synced,
              "idle freeze demoted out of Synced — shrink/poison recovery "
              "path hit (the duplicate-write symptom)");
    }

    auto rb = rows_of(cb);
    int d = -1;
    for (int y = 0; y < committed_a && y < (int)rb.size(); ++y)
        if (y >= (int)prev_a.size() || prev_a[y] != rb[y]) { d = y; break; }
    if (d >= 0)
        std::fprintf(stderr,
            "  IDLE committed divergence at %d\n    A |%s|\n    B |%s|\n",
            d, d < (int)prev_a.size() ? prev_a[d].c_str() : "<none>",
            d < (int)rb.size() ? rb[d].c_str() : "<none>");
    CHECK(d < 0, "IDLE finalize freeze rewrote a committed scrollback row");

    close(rfd);
}

// 该 转弯 -finish 文本路径。 帧 A: 一个长的助手 TEXT 回复
// 仍然 流式传输 - 阶段 活动,信息 字节 住在这里
// streaming_text 编辑 StreamingMarkdown 渲染
// 未经承诺的后方街区 render_tail (直播模式). 帧 溢出时 可视区域.
// 帧 B: StreamFinished — streaming_text 移动到文本中,
// 标记为 finish() 'd( 时间: 末尾 被困在峡谷里 跑道被冻结
// render_tail 所执行的行数 -block 行数可以不同;如果 帧 B是,
// 我们 SHORTER 以倾注的言语发誓, maya 收缩 -guard
// 火灾(流量)+ demote_to_stale - > 案例-(B) 重
// 绘 线条重复。 "it doubles when a turn finishes"
// — a TEXT 回答,不是工具卡。
TEST_CASE("text turn finish shrink") {
    constexpr int kWidth = 80;
    constexpr int kTermH = 24;

    Model m;
    m.d.current.id = agentty::ThreadId{"wiretext"};
    Message u; u.role = Role::User; u.text = "explain at length";
    m.d.current.messages.push_back(std::move(u));
    agentty::app::detail::clear_frozen(m);
    agentty::app::detail::freeze_through(m, 1);

    // 长篇散文答复以一个小路的街区结束,该街区的活人(L.
    // render_tail ) v 已解决(承诺 -block )布局可能有所不同:
    // 紧接着一个段落,并附有 NO 终止空白行——经典
    // 尾部尚未在边界处提交。
    // 形状 逐步显示 动画片在最后的片段 帧.
    std::string body;
    for (int i = 0; i < 60; ++i)
        body += "This is paragraph line " + std::to_string(i)
              + " of a long streamed assistant reply that wraps.\n\n";
    body += "Final points before the turn ends:\n";
    for (int i = 0; i < 6; ++i)
        body += "- bullet item number " + std::to_string(i) + "\n";
    body += "- last bullet with no trailing newline boundary";

    StylePool pool;
    auto [writer, rfd] = make_pipe_writer();

    // 帧 A: 流式传输 — 字节 输入 streaming_text , 阶段活动。
    {
        Message a; a.role = Role::Assistant;
        a.streaming_text = body;
        m.d.current.messages.push_back(std::move(a));
        m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};
    }
    // 转弯的视线 cached_markdown_for 启用 maya
    // reveal_fx (一个墙上打字机) 在活的部件,所以 首次 仅
    // 显示 字节 光标走过 很小一部分 `body`。这个测试是关于
    // freeze/shrink SEAM ,而不是 逐步显示 动画,
    // 它需要 帧 至 时 渲染 整个流线 正文 故它会溢出 可视区域 预
    // 览 -seed 编号 缓存 与 reveal_fx OFF 所以,
    // 我们 流式传输 帧 测量固定高度。
    {
        auto& cache = m.ui.view_cache.message_md(
            m.d.current.id, m.d.current.messages.back().id);
        cache.streaming = std::make_shared<maya::StreamingMarkdown>();
        cache.streaming->set_reveal_fx(false);
    }
    Canvas ca = paint(build_root(m), kWidth, pool);
    auto oa = InlineFrame<Empty>{}.seed().render(
        ca, content_rows(ca), term_rows_for_test(kTermH), pool, writer, false);
    (void)drain(rfd);
    InlineFrame<Synced> s = std::visit(
        [](auto&& arm) -> InlineFrame<Synced> {
            using T = std::decay_t<decltype(arm)>;
            if constexpr (std::is_same_v<T, InlineFrame<Synced>>) return std::move(arm);
            else { std::fprintf(stderr, "  text frame A not Synced\n"); std::abort(); }
        }, std::move(oa));
    const int rows_a = s.rows();
    const int committed_a = rows_a > kTermH ? rows_a - kTermH : 0;
    CHECK(committed_a > 0, "text setup: streaming reply must overflow viewport");

    auto rows_of = [&](const Canvas& c) {
        std::vector<std::string> rows;
        const int mr = c.max_content_row();
        for (int y = 0; y <= mr; ++y) {
            std::string line;
            for (int x = 0; x < kWidth; ++x) {
                char32_t ch = c.get(x, y).character;
                line.push_back(ch && ch < 128 ? static_cast<char>(ch) : ' ');
            }
            while (!line.empty() && line.back() == ' ') line.pop_back();
            rows.push_back(std::move(line));
        }
        return rows;
    };
    auto prev_a = rows_of(ca);

    // StreamFinished 闲行路径: 执行 streaming_text - > 文本,
    // 平定标记( 完成), 冻结运行。 Mirrors finalize_turn 闲臂.
    {
        auto& last = m.d.current.messages.back();
        last.text = std::move(last.streaming_text);
        std::string{}.swap(last.streaming_text);
        auto& cache = m.ui.view_cache.message_md(m.d.current.id, last.id);
        if (!cache.streaming)
            cache.streaming = std::make_shared<maya::StreamingMarkdown>();
        cache.streaming->set_content(last.text);
        cache.streaming->finish();
    }
    m.s.phase = agentty::phase::Idle{};
    agentty::app::detail::freeze_through(m, m.d.current.messages.size());

    // 帧 B: 冻结前缀+空直播 末尾.
    Canvas cb = paint(build_root(m), kWidth, pool);
    const int rows_b = (int)rows_of(cb).size();
    std::fprintf(stderr, "  [text] rows_a=%d rows_b=%d committed_a=%d\n",
                 rows_a, rows_b, committed_a);
    CHECK(rows_b >= rows_a,
          "TEXT TURN-FINISH SHRANK the frame (rows_b < rows_a) while "
          "overflowed — the live render_tail layout was taller than the "
          "settled committed-block layout; shrink-guard fires and strands "
          "a duplicate. This is the 'reply doubles when the turn finishes' "
          "bug.");

    auto wit = s.verify();
    CHECK(wit.has_value(), "text shadow verify failed after A");
    if (wit) {
        auto proof = s.check_scrollback(cb, kTermH);
        CHECK(proof.has_value(),
              "text: scrollback gate rejected the freeze frame");
        auto ob = std::move(s).render(
            cb, content_rows(cb), term_rows_for_test(kTermH), pool, writer,
            std::move(*wit), std::move(*proof), false);
        (void)drain(rfd);
        bool synced = std::visit([](auto&& arm) {
            using T = std::decay_t<decltype(arm)>;
            return std::is_same_v<T, InlineFrame<Synced>>;
        }, std::move(ob));
        CHECK(synced,
              "text turn-finish demoted out of Synced — shrink recovery "
              "path hit (the duplicate-reply symptom)");
    }

    auto rb = rows_of(cb);
    int d = -1;
    for (int y = 0; y < committed_a && y < (int)rb.size(); ++y)
        if (y >= (int)prev_a.size() || prev_a[y] != rb[y]) { d = y; break; }
    if (d >= 0)
        std::fprintf(stderr,
            "  TEXT committed divergence at %d\n    A |%s|\n    B |%s|\n",
            d, d < (int)prev_a.size() ? prev_a[d].c_str() : "<none>",
            d < (int)rb.size() ? rb[d].c_str() : "<none>");
    CHECK(d < 0, "TEXT turn-finish rewrote a committed scrollback row");

    close(rfd);
}

// REGRESSION 给心理医生 -while-overflowed 恢复门。
// 当前一个 帧 OVERFLOWS 编号 可视区域 财务报告和已审计财务报表 此 帧 短,
// 可以有两个形状:
//
//   (1) TURN-FINISH FREEZE 心理医生在 BOTTOM ;
//       已存在本地的行 回滚缓冲区 (溢出前缀)是 字节-IDENTICAL。
//       diff 路径处理它附加 -only . 追回(承诺+案件-
//       (B)) 不得 此处开火——(B)例 -paints 从内容顶端,
//       重叠已承诺的前缀, 并拖动重复。
//       这是 "turn doubles in scrollback when it finishes"
//       错误——它会伸缩 EVEN 当 帧 从溢出处到适值( )
//       new_rows <= term_h ),即前门,
//       new_rows <= term_h ( ) 错误地归为个案 -B.
//
//   (2) SCROLLBACK-CONTENT SHIFT ——前缀不同;回收 IS
//       需要帮助。
//
// 该大门通过InlineFrameState::scrollback_prefix_matches进行歧视。
// 驱动一个真实的带载同步状态穿过收缩并断言:(a)
// 纯底部收缩的前缀-match 上游返回真实,
// (b) 经核实的diff路径保持同步,并重写没有承诺的行.
TEST_CASE("overflowed shrink stays synced") {
    constexpr int kWidth = 80;
    constexpr int kTermH = 24;

    Model m;
    m.d.current.id = agentty::ThreadId{"wireshrink"};
    Message u; u.role = Role::User; u.text = "go";
    m.d.current.messages.push_back(std::move(u));
    agentty::app::detail::clear_frozen(m);
    agentty::app::detail::freeze_through(m, 1);

    StylePool pool;
    auto [writer, rfd] = make_pipe_writer();

    auto rows_of = [&](const Canvas& c) {
        std::vector<std::string> rows;
        const int mr = c.max_content_row();
        for (int y = 0; y <= mr; ++y) {
            std::string line;
            for (int x = 0; x < kWidth; ++x) {
                char32_t ch = c.get(x, y).character;
                line.push_back(ch && ch < 128 ? static_cast<char>(ch) : ' ');
            }
            while (!line.empty() && line.back() == ' ') line.pop_back();
            rows.push_back(std::move(line));
        }
        return rows;
    };

    // 帧 A:一个高个子的定居者 -text 答复时,答复的幅度很大。
    // 电话 正文 是那个 FROZEN 内容; 额外携带 TRAILING
    // 该段 帧 B掉落 关键是 掉落的排队都到了 BOTTOM (内)
    // rows_b, rows_a )),故作前缀. rows_a -
    // term_h )是(这是) 字节-identical A和B之间——真正
    // 的转弯 -finish 下边 -shrink (实值) 光标 / 逐步
    // 显示 差距崩溃, 不 a 内容 -removal 改变 回滚缓冲区.
    const int kBodyParas = 80;
    auto body_text = [](int paras) {
        std::string body;
        for (int i = 0; i < paras; ++i)
            body += "reply line " + std::to_string(i) + " with enough text\n\n";
        return body;
    };
    // 最后一段在A(长)和B(短)的单行上覆盖了几行。
    // 上面的每行都是一样的,所以承诺的前缀保持
    // 不变 字节-identical 则下部收缩.
    auto tall_tail = [](int width_lines) {
        std::string s = "TAIL";
        for (int i = 0; i < width_lines; ++i) s += " wwwwwwww wwwwwwww wwwwwwww";
        return s;
    };
    {
        Message a; a.role = Role::Assistant;
        a.text = body_text(kBodyParas) + tall_tail(8);
        auto& cache = m.ui.view_cache.message_md(m.d.current.id, a.id);
        cache.streaming = std::make_shared<maya::StreamingMarkdown>();
        cache.streaming->set_content(a.text);
        cache.streaming->finish();
        m.d.current.messages.push_back(std::move(a));
    }
    Canvas ca = paint(build_root(m), kWidth, pool);
    auto oa = InlineFrame<Empty>{}.seed().render(
        ca, content_rows(ca), term_rows_for_test(kTermH), pool, writer, false);
    (void)drain(rfd);
    InlineFrame<Synced> s = std::visit(
        [](auto&& arm) -> InlineFrame<Synced> {
            using T = std::decay_t<decltype(arm)>;
            if constexpr (std::is_same_v<T, InlineFrame<Synced>>) return std::move(arm);
            else { std::fprintf(stderr, "  shrink frame A not Synced\n"); std::abort(); }
        }, std::move(oa));
    const int rows_a = s.rows();
    auto prev_a = rows_of(ca);
    CHECK(rows_a > kTermH + 4,
          "shrink setup: frame A must overflow the viewport by several rows");

    // 帧 B: 仅删除后面的填充段落。所有 80 正文
    // 段落保留,因此上行(承诺)为 字节-identical ;
    // 只有最下面的几行缩小,停留在溢出。
    {
        auto& a = m.d.current.messages.back();
        a.text = body_text(kBodyParas) + tall_tail(0);
        auto& cache = m.ui.view_cache.message_md(m.d.current.id, a.id);
        cache.streaming->set_content(a.text);
        cache.streaming->finish();
    }
    Canvas cb = paint(build_root(m), kWidth, pool);
    const int rows_b = (int)rows_of(cb).size();
    std::fprintf(stderr, "  [shrink] rows_a=%d rows_b=%d term_h=%d\n",
                 rows_a, rows_b, kTermH);
    CHECK(rows_b < rows_a && rows_b > kTermH,
          "shrink setup: frame B must be shorter than A yet still overflow");

    // 门的歧视性者,行使对 真实 API。对于纯底部缩小,
    // 溢出前缀是 字节-identical ——必须恢复.
    // 不 火力和diff路径运行。
    const int prev_rows_v = s.rows();
    const int overflow    = prev_rows_v - kTermH;
    const bool prefix_unchanged = s.scrollback_prefix_matches(cb, overflow);
    if (!prefix_unchanged) {
        auto rba = rows_of(ca);
        auto rbb = rows_of(cb);
        for (int y = 0; y < overflow; ++y) {
            const std::string& la = y < (int)rba.size() ? rba[y] : std::string();
            const std::string& lb = y < (int)rbb.size() ? rbb[y] : std::string();
            if (la != lb) {
                std::fprintf(stderr,
                    "  [shrink] prefix diverges at row %d/%d\n    A |%s|\n    B |%s|\n",
                    y, overflow, la.c_str(), lb.c_str());
                break;
            }
        }
    }
    CHECK(prefix_unchanged,
          "pure bottom shrink: scrollback_prefix_matches must report the "
          "overflow prefix unchanged, so the recovery does NOT fire and the "
          "diff path keeps it Synced (no stranded duplicate).");

    // 运行实际的闸机决定(模拟Runtime::render的同步臂).
    std::string bytes_guard;
    if (!prefix_unchanged) {
        std::fprintf(stderr, "  [shrink] unexpected recovery path\n");
        std::abort();
    } else {
        auto wit = s.verify();
        CHECK(wit.has_value(), "shrink shadow verify failed after A");
        if (wit) {
            auto proof = s.check_scrollback(cb, kTermH);
            CHECK(proof.has_value(),
                  "shrink: scrollback gate rejected the pure-bottom-shrink "
                  "frame (prefix should be unchanged)");
            auto ob = std::move(s).render(
                cb, content_rows(cb), term_rows_for_test(kTermH), pool, writer,
                std::move(*wit), std::move(*proof), false);
            bytes_guard = drain(rfd);
            bool synced = std::visit([](auto&& arm) {
                using T = std::decay_t<decltype(arm)>;
                return std::is_same_v<T, InlineFrame<Synced>>;
            }, std::move(ob));
            CHECK(synced, "still-overflowed shrink diff did not stay Synced");
        }
    }

    // 传输-volume 见证。纯洁的 diff 路径 BOTTOM 缩放
    // 仅释放已更改的底行 + 滚动 -off 保护 +\ x1b[J] —
    // — 少数 KB 最多是, 不 完整 -viewport-plus-committed-prefix
    // 重绘 生成的buggy 承诺+ case-(B) 恢复。
    // 下面的结构检查( stays Syncted, 没有承诺行
    // 重写)是主要断言; 这个约束只抓回了 case- (B)。
    std::fprintf(stderr, "  [shrink] prefix_unchanged=%d emitted_bytes=%zu\n",
                 (int)prefix_unchanged, bytes_guard.size());
    CHECK(bytes_guard.size() < 4096,
          "shrink emitted a full-viewport repaint — the recovery over-fired "
          "into case-(B), re-emitting committed scrollback rows (the stranded "
          "duplicate). Expected the small incremental diff.");

    auto rb = rows_of(cb);
    const int committed_b = rows_b > kTermH ? rows_b - kTermH : 0;
    int d = -1;
    for (int y = 0; y < committed_b && y < (int)rb.size(); ++y)
        if (y >= (int)prev_a.size() || prev_a[y] != rb[y]) { d = y; break; }
    if (d >= 0)
        std::fprintf(stderr,
            "  SHRINK committed divergence at %d\n    A |%s|\n    B |%s|\n",
            d, d < (int)prev_a.size() ? prev_a[d].c_str() : "<none>",
            d < (int)rb.size() ? rb[d].c_str() : "<none>");
    CHECK(d < 0, "still-overflowed shrink rewrote a committed scrollback row");

    close(rfd);
}

// MID-RUN TRIM 现场 传输。一个长期活动运行会累积一个高的冷冻前缀,
// 以 OUTPUT-ELIDED 工具(bash / 读 / grep),其 maya
// 制成器会崩溃一大批输出到几行. trim_frozen_above_ 可视区域
// 放下枪 -screen 正面和外射 commit_scrollback(N).
// 腐败的守护者: estimate_msg_rows OVER
// -counts 银牌, 银牌 -loop 将一个条目仍放入屏幕,
// 并显示边界土地位于真实的关闭线以下 -screen 排行数
// - 帧 B 然后重写一个承诺 回滚缓冲区 排行( 击打)
// -card 驾驶真正的作曲 跨过修剪 并断言没有承诺的行被改写
// 和 承诺数没有被夹住( 无下面) -commit).
static ToolUse settled_bash(const std::string& tag, int n_lines) {
    ToolUse t;
    t.id = ToolCallId{"bash_" + tag}; t.name = ToolName{"shell"};
    t.args = {{"command", "grep -rn x src # " + tag}};
    std::string out;
    for (int i = 0; i < n_lines; ++i)
        out += "src/file" + std::to_string(i) + ".cpp:" + std::to_string(i)
             + ": a matching line of plausible source text here\n";
    auto now = steady_clock::now();
    t.status = ToolUse::Done{now - milliseconds{5}, now, std::move(out)};
    return t;
}

TEST_CASE("midrun trim output heavy no rewrite") {
    constexpr int kWidth = 100;
    constexpr int kTermH = 30;

    Model m;
    m.d.current.id = agentty::ThreadId{"wiretrim"};
    // 多个 COMPLETE 转弯(用户+吵闹的敲击),每圈在固定边界上被冻结,
    // 然后是活动 流式传输 末尾 每张卡片都有一张 HUGE 输出到 ~4 行。
    // -fix 线段估计结束 -counted ~120行,
    // 所以一个小绊倒的保存 -loop 滴水点
    // -an-on-screen-entry 路径。
    for (int e = 0; e < 16; ++e) {
        Message u; u.role = Role::User;
        u.text = "noisy work " + std::to_string(e);
        m.d.current.messages.push_back(std::move(u));
        Message a; a.role = Role::Assistant;
        a.tool_calls.push_back(
            settled_bash("t" + std::to_string(e), 120));
        m.d.current.messages.push_back(std::move(a));
    }
    agentty::app::detail::clear_frozen(m);
    agentty::app::detail::freeze_through(m, m.d.current.messages.size());
    Message ph; ph.role = Role::Assistant; ph.streaming_text = "continuing";
    m.d.current.messages.push_back(std::move(ph));
    m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};

    StylePool pool;
    auto [writer, rfd] = make_pipe_writer();

    // 帧 A: 渲染 高高的冰冻前缀+活的占位符。
    Canvas ca = paint(build_root(m), kWidth, pool);
    auto oa = InlineFrame<Empty>{}.seed().render(
        ca, content_rows(ca), term_rows_for_test(kTermH), pool, writer, false);
    (void)drain(rfd);
    InlineFrame<Synced> sa = std::visit(
        [](auto&& arm) -> InlineFrame<Synced> {
            using T = std::decay_t<decltype(arm)>;
            if constexpr (std::is_same_v<T, InlineFrame<Synced>>)
                return std::move(arm);
            else { std::fprintf(stderr, "  trim frame A not Synced\n");
                   std::abort(); }
        }, std::move(oa));

    const int rows_a = sa.rows();
    const int committed_a = rows_a > kTermH ? rows_a - kTermH : 0;
    CHECK(committed_a > 0,
          "trim setup: prefix must overflow the viewport");

    // 裁剪：生产中的唯一裁剪操作，在轮次边界触发。
    //    从MODEL中丢弃 -budget 前置,并返回从TOP
    //    中删除的 N 行的 commit_scrollback(N)。
    //    顶部 -deletion 不可见 maya's
    //    渲染-time 缩放调节( 已移转) -up 画布失败前
    //    缀 memcmp, 所以 maya 将实施 新 前缀过
    //    PHYSICAL 旧行和线条重复)——主机必须将掉的行
    //    对着静态 -valid 旧 帧 之前 下一个 渲染.
    auto cmd = agentty::app::detail::trim_frozen_if_oversized(m);
    const auto* commit = std::get_if<maya::CommitScrollback>(&cmd.inner);
    CHECK(commit != nullptr,
          "trim must return commit_scrollback(N) for the top-deletion; "
          "none() leaves maya unable to reconcile and strands a duplicate.");
    // 将承诺与制作的Cmd解释器完全一样:
    // 提前发送( 旧) -content )同步( 同步) (% 1) 帧's
    // prev_rows/prev_cells 以 N 键为前 渲染 短数 源码树.
    // commit_inline_prefix 内部夹子=分钟(N, prev_rows
    // - term_h 由于被撤掉的条目过多,N从未超过这个界限。
    if (commit && commit->debt.rows() > 0) {
        const int safe = std::min(commit->debt.rows(), std::max(0, sa.rows() - kTermH));
        if (safe > 0)
            sa = std::move(sa).commit(sa.scrollback_marker(safe));
    }

    // 渲染 帧 B对修剪 源码树 结束 maya's 真实
    // 同步 渲染, 之后 执行主机。这恰恰是制作 传输:
    // 冰冻 源码树 顶部收缩,主机 承诺了掉下行,
    // 下一个 渲染 一定要降落 保存 源码树 已就绪 无需
    // 重写承诺 回滚缓冲区 划线
    Canvas cb = paint(build_root(m), kWidth, pool);
    // AUTHORITATIVE 校验:修剪完之后, 真实 使保存的
    // 高度冻结 源码树 (+ 现场直播) 末尾 ) 仍然必须是 QQ
    // term_h 修补的正确性在于保持... 可视区域 屏幕上显示,
    // 如果 estimate_msg_rows OVER-counts
    // 输出 -elided 纸牌, 纸牌 -loop 早点停下来,
    // 然后 真实 保持行数低于 term_h —— 投放的条目仍在屏幕上,
    // 并且它作为鬼乐队的忠实复制线。 即使当 maya 夹子
    // 碰巧吸收了 字节 三角洲(英语:Delta).
    const int kept_real_rows = cb.max_content_row() + 1;
    std::fprintf(stderr, "  [trim] kept_real_rows=%d term_h=%d\n",
                 kept_real_rows, kTermH);
    CHECK(kept_real_rows >= kTermH,
          "trim left FEWER than a viewport of REAL rows on screen — the "
          "estimate over-counted and dropped an on-screen entry (ghost band)");

    auto wit = sa.verify();
    CHECK(wit.has_value(), "trim: shadow verify failed before render");
    std::string bytes_b;
    if (wit) {
        // 顶部 -drop 将内容向上移动,所以承诺的前缀可能不再匹配——
        // check_scrollback 然后返回无光, 生产路径会
        // 执行溢出 + 柔性 -repaints 3 包括第1款(c)项。
        // 有效的证据( 停留同步) 和回收都正确且不正确 -destructive
        // 反战 -corruption 证据是 kept_real_rows
        // >= term_h 上面请检查
        auto proof = sa.check_scrollback(cb, kTermH);
        maya::inline_frame::RenderOutcome ob = [&] {
            if (proof) {
                return std::move(sa).render(
                    cb, content_rows(cb), term_rows_for_test(kTermH), pool,
                    writer, std::move(*wit), std::move(*proof), false);
            }
            const int overflow = std::max(0, sa.rows() - kTermH);
            auto committed = std::move(sa).commit(sa.scrollback_marker(overflow));
            return maya::inline_frame::RenderOutcome{
                std::move(committed).demote_to_stale()};
        }();
        bytes_b = drain(rfd);
        bool synced_b = std::visit([](auto&& arm) {
            using T = std::decay_t<decltype(arm)>;
            return std::is_same_v<T, InlineFrame<Synced>>;
        }, std::move(ob));
        // maya 合法地拥有 自身 承诺 -overflow + 软
        // -repaint 此处恢复( 顶部) -drop 移动内容上移,
        // 所以前缀不再匹配)——即 CORRECT 校正且非对齐
        // -destructive (没有\x1b[3J擦拭) 我们
        // 禁止的就是虫的症状: 被承诺 回滚缓冲区 正在重写边界下的行。
        // kept_real_rows >= term_h 上面的证明
        // 已经显示完整 可视区域 保留在屏幕上;是否 maya 保持
        // 同步或降级到其软体 -repaint 恢复,没有承诺的行线。
        (void)synced_b;
        (void)rows_a;
        (void)committed_a;
    }

    close(rfd);
}

TEST_CASE("midrun trim full body writes no rewrite") {
    // 该 截图情景, 结束于 传输 : 重复( E) 完整-body 写到
    // 活动运行时,每个运行时都会溢出 可视区域 ,带有中点 -run 整形射击,
    // 全装 正文 表示( 表示) frozen_rows [是现在]
    // maya's 真实 测量高度( 4 ) push_frozen
    // 测量已建元素,而不是估计。 -loop 并且用计数,从这个数中产生,
    // 故能计算。 -real-rows >= term_h 证据和否
    // -clamp (safe_n == commit_n (a) 建筑证据,
    // 尽管每件 正文 渲染 满座的
    constexpr int kWidth = 100;
    constexpr int kTermH = 30;

    Model m;
    m.d.current.id = agentty::ThreadId{"wirefull"};
    // 单独转弯( 用户+ 满) -body ),所以冷冻前缀是
    // MANY 输入修剪可以实际降下——真实截图形状(重复)
    // "write a file" ),没有合并的巨型 -run.
    // 标注在前缀上是几个视图 超过保留边距 现实的每
    // -Tick cadence(一些条目已删除),
    // 而非病理单一巨型 -trim.
    for (int e = 0; e < 6; ++e) {
        Message u; u.role = Role::User;
        u.text = "write file " + std::to_string(e);
        m.d.current.messages.push_back(std::move(u));
        Message a; a.role = Role::Assistant;
        a.tool_calls.push_back(settled_write("f" + std::to_string(e), 40));
        m.d.current.messages.push_back(std::move(a));
    }
    Message ph; ph.role = Role::Assistant; ph.streaming_text = "continuing";
    m.d.current.messages.push_back(std::move(ph));
    m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};

    agentty::app::detail::clear_frozen(m);
    agentty::app::detail::freeze_through(m, m.d.current.messages.size() - 1);

    StylePool pool;
    auto [writer, rfd] = make_pipe_writer();

    Canvas ca = paint(build_root(m), kWidth, pool);
    auto oa = InlineFrame<Empty>{}.seed().render(
        ca, content_rows(ca), term_rows_for_test(kTermH), pool, writer, false);
    (void)drain(rfd);
    InlineFrame<Synced> sa = std::visit(
        [](auto&& arm) -> InlineFrame<Synced> {
            using T = std::decay_t<decltype(arm)>;
            if constexpr (std::is_same_v<T, InlineFrame<Synced>>)
                return std::move(arm);
            else { std::fprintf(stderr, "  full-body trim frame A not Synced\n");
                   std::abort(); }
        }, std::move(oa));

    const int rows_a = sa.rows();
    const int committed_a = rows_a > kTermH ? rows_a - kTermH : 0;
    CHECK(committed_a > 0,
          "full-body trim setup: prefix must overflow the viewport");

    auto cmd = agentty::app::detail::trim_frozen_if_oversized(m);
    const auto* commit = std::get_if<maya::CommitScrollback>(&cmd.inner);
    CHECK(commit != nullptr,
          "full-body trim must return commit_scrollback(N) for the "
          "top-deletion; none() leaves maya unable to reconcile and "
          "strands a duplicate of the trimmed boundary one screen up.");
    // 将承诺与制作的Cmd解释器完全一样:
    // 提前发送( 旧) -content )同步( 同步) (% 1) 帧's prev_rows/prev_cells
    // 以 N 键为前 渲染 短数 源码树.
    if (commit && commit->debt.rows() > 0) {
        const int safe = std::min(commit->debt.rows(), std::max(0, sa.rows() - kTermH));
        if (safe > 0)
            sa = std::move(sa).commit(sa.scrollback_marker(safe));
    }

    // 渲染 帧 B对修剪 源码树 结束 maya's 真实
    // 同步 渲染, 之后 主机执行( 与顶端一起锁定
    // 推进阴影) -drop ⁇ .
    Canvas cb = paint(build_root(m), kWidth, pool);
    const int kept_real_rows = cb.max_content_row() + 1;
    std::fprintf(stderr, "  [full-body trim] kept_real_rows=%d term_h=%d\n",
                 kept_real_rows, kTermH);
    CHECK(kept_real_rows >= kTermH,
          "full-body trim left fewer than a viewport of REAL rows — a "
          "measured entry disagreed with the render (ghost band)");

    auto wit = sa.verify();
    CHECK(wit.has_value(), "full-body trim: shadow verify failed");
    std::string bytes_b;
    if (wit) {
        // 和产出一样 -heavy 变体 : 顶部 -drop 可能
        // 将承诺的前缀移到 check_scrollback 返回无选择;
        // 然后生产路径会显示溢出+软 -repaints。
        // 结果都正确,没有 -destructive.
        auto proof = sa.check_scrollback(cb, kTermH);
        maya::inline_frame::RenderOutcome ob = [&] {
            if (proof) {
                return std::move(sa).render(
                    cb, content_rows(cb), term_rows_for_test(kTermH), pool,
                    writer, std::move(*wit), std::move(*proof), false);
            }
            const int overflow = std::max(0, sa.rows() - kTermH);
            auto committed = std::move(sa).commit(sa.scrollback_marker(overflow));
            return maya::inline_frame::RenderOutcome{
                std::move(committed).demote_to_stale()};
        }();
        bytes_b = drain(rfd);
        bool synced_b = std::visit([](auto&& arm) {
            using T = std::decay_t<decltype(arm)>;
            return std::is_same_v<T, InlineFrame<Synced>>;
        }, std::move(ob));
        // 如产出-heavyXX变体:maya可能保持同步(附录-only diff)
        // 或自行承诺-overflow +软-repaint 恢复顶端-drop
        // 转动——两者都是正确和非-destructive. 反-corruption
        // 证明是kept_real_rows → term_h(一个
        // 完整的可视区域 留在屏幕上).
        (void)synced_b;
        (void)rows_a;
        (void)committed_a;
    }

    close(rfd);
}

// REGRESSION: opening/closing 模型 选择面板 反复地,
// 必须超越欢迎的屏幕 不 底线的线程复制件 帧 本地语 回滚缓冲区。
// 该词 选择面板 覆盖面板 数行 TALLER 而不是欢迎的基数;
// 终端 以欢迎方式+ 覆盖面板 超过 term_h ,生长时滚动顶端行(字签),
// 以进入 回滚缓冲区 通过底部 -edge \r\n,和收缩 -back 无法
// 收回它们——每个 open/close 循环拖曳另一个副本( Q)
// 每打开一个选择器，应用标识就变长。
//
// 我们开着 真实 内置构图( 无) force_redraw
// ——这让案件流传更为糟糕. -B 滚动),跨越若干开放周期
// -在某一周期 SHORT 长度高度,且不主张超过 可视区域
// 上面的更早一点 帧 被改写过, 和 编号 帧 保持
// 同步( 恢复降级会自己滚动)。
static maya::Element view_root(const Model& m) {
    return agentty::ui::view(m);
}

TEST_CASE("model picker open close no scrollback growth") {
    using namespace agentty;
    constexpr int kWidth = 100;
    // SHORT 可视区域 很高兴见到你 覆盖面板 溢出——生产触发器.
    constexpr int kTermH = 20;

    // 音符转动 : ModelsSelect 持续设置通过 deps().
    app::install_deps(app::Deps{
        .stream        = [](const maya::unlocked&, provider::Request, provider::EventSink) {},
        .save_thread   = [](const agentty::Thread&) {},
        .delete_thread = [](const agentty::ThreadId&) {},
        .load_threads  = [] { return std::vector<agentty::Thread>{}; },
        .load_thread   = [](const ThreadId&) { return std::optional<agentty::Thread>{}; },
        .load_settings = [] { return store::Settings{}; },
        .save_settings = [](const store::Settings&) {},
        .new_thread_id = [] { return ThreadId{"stub"}; },
        .title_from    = [](std::string_view) { return std::string{}; },
        .auth          = {},
    });

    Model m;
    m.d.current.id = ThreadId{"pickercycle"};
    m.d.available_models.push_back({});
    m.d.available_models.back().id = ModelId{"claude-opus-4-1"};
    m.d.available_models.push_back({});
    m.d.available_models.back().id = ModelId{"claude-sonnet-4-5"};

    StylePool pool;
    auto [writer, rfd] = make_pipe_writer();

    auto rows_of = [&](const Canvas& c) {
        std::vector<std::string> rows;
        const int mr = c.max_content_row();
        for (int y = 0; y <= mr; ++y) {
            std::string line;
            for (int x = 0; x < kWidth; ++x) {
                char32_t ch = c.get(x, y).character;
                line.push_back(ch && ch < 128 ? static_cast<char>(ch) : ' ');
            }
            while (!line.empty() && line.back() == ' ') line.pop_back();
            rows.push_back(std::move(line));
        }
        return rows;
    };

    std::optional<InlineFrame<Synced>> synced;
    std::vector<std::string> committed_wire;   // 已经删除的行 -screen

    auto render_state = [&](const Model& mm) {
        Canvas c = paint(view_root(mm), kWidth, pool);
        auto cur = rows_of(c);
        const int total = static_cast<int>(cur.size());
        const int new_committed = total > kTermH ? total - kTermH : 0;

        if (!synced) {
            auto o = InlineFrame<Empty>{}.seed().render(
                c, content_rows(c), term_rows_for_test(kTermH), pool, writer,
                false);
            (void)drain(rfd);
            synced = std::visit(
                [](auto&& a) -> std::optional<InlineFrame<Synced>> {
                    using T = std::decay_t<decltype(a)>;
                    if constexpr (std::is_same_v<T, InlineFrame<Synced>>)
                        return std::move(a);
                    else return std::nullopt;
                }, std::move(o));
            CHECK(synced.has_value(), "picker-cycle: first frame Synced");
        } else {
            auto wit = synced->verify();
            CHECK(wit.has_value(),
                  "picker-cycle: shadow verify before render (desync)");
            if (!wit) { synced.reset(); return; }
            auto proof = synced->check_scrollback(c, kTermH);
            maya::inline_frame::RenderOutcome o = [&] {
                if (proof) {
                    return std::move(*synced).render(
                        c, content_rows(c), term_rows_for_test(kTermH), pool,
                        writer, std::move(*wit), std::move(*proof), false);
                }
                // Open/close 高度过渡 将承诺的前缀移到前
                // 缀 帧 阴影: 生产会让它失去 -viewport 细
                // 行和柔软 -repaints 途经 过期 (关于
                // -enters 同步, 没有 回滚缓冲区 擦干净 Model
                // 确切地说,—— 真实 反 -corruption 不变量 是
                // 那个 字节-stability 检查下面( 没有)
                // -committed 传输 排是重写,不管哪个臂部使这个 帧.
                const int prev_rows = synced->rows();
                const int overflow = prev_rows > kTermH
                    ? prev_rows - kTermH : 0;
                auto committed = std::move(*synced).commit(
                    synced->scrollback_marker(overflow));
                return std::move(committed).demote_to_stale().render(
                    c, content_rows(c), term_rows_for_test(kTermH), pool,
                    writer, false);
            }();
            (void)drain(rfd);
            synced = std::visit(
                [](auto&& a) -> std::optional<InlineFrame<Synced>> {
                    using T = std::decay_t<decltype(a)>;
                    if constexpr (std::is_same_v<T, InlineFrame<Synced>>)
                        return std::move(a);
                    else return std::nullopt;
                }, std::move(o));
            CHECK(synced.has_value(),
                  "picker-cycle: render re-entered Synced after recovery");
            if (!synced) return;
        }

        // 强制前缀必须是 字节-stable 跨度( 跨度) 帧.
        const int check_n = std::min<int>(
            static_cast<int>(committed_wire.size()), new_committed);
        for (int y = 0; y < check_n; ++y) {
            if (committed_wire[static_cast<std::size_t>(y)]
                != cur[static_cast<std::size_t>(y)]) {
                std::fprintf(stderr,
                    "  PICKER committed-row rewrite at y=%d:\n"
                    "    was |%s|\n    now |%s|\n", y,
                    committed_wire[static_cast<std::size_t>(y)].c_str(),
                    cur[static_cast<std::size_t>(y)].c_str());
                CHECK(false,
                      "a base-frame row that overflowed the viewport top "
                      "was rewritten on a later open/close — stranded "
                      "scrollback copy (the growing-wordmark bug)");
                break;
            }
        }
        committed_wire.assign(cur.begin(), cur.begin() + new_committed);
    };

    // 欢迎(基地).
    render_state(m);
    // 该 固定 不变量 (view.cpp overlay_layer
    // : 打开 a 选择面板 必须 绝不 种出 帧。
    // 该词 覆盖面板 下层 -inset 画的画范围是两行的
    // at/above 底部的涂色范围; 不设置 覆盖面板 底
    // 箱的bg 结构上的2 -unpainted 下行( +2 打开,
    // -2 关闭), 将字标推入原生 回滚缓冲区 每轮一次。
    {
        Canvas closed_c = paint(view_root(m), kWidth, pool);
        const int closed_rows = closed_c.max_content_row();
        m.ui.panel.descend(pn::Models{{0, ""}});
        Canvas open_c = paint(view_root(m), kWidth, pool);
        m.ui.panel.close<pn::Models>();
        const int open_rows = open_c.max_content_row();
        if (open_rows > closed_rows) {
            std::fprintf(stderr,
                "  PICKER frame grew on open: closed=%d open=%d\n",
                closed_rows + 1, open_rows + 1);
        }
        CHECK(open_rows <= closed_rows,
              "opening the model picker grew the frame past the closed "
              "welcome height \u2014 rows would cross the viewport boundary "
              "into native scrollback (the growing-wordmark bug)");
    }
    // 几个开放周期——用户报告的累积周期。
    // 渲染 多个动画 帧 每一个州,所以欢迎字标记的每
    // 个州 -frame 鲍勃和 选择面板 动画预告.
    for (int cycle = 0; cycle < 4; ++cycle) {
        m.ui.panel.descend(pn::Models{{0, ""}});   // 打开
        for (int f = 0; f < 5; ++f) {
            render_state(m);
            std::this_thread::sleep_for(std::chrono::milliseconds{8});
        }
        auto [m2, cmd] = ::agentty::app::detail::step(::agentty::app::detail::models_update, std::move(m), msg::ModelsMsg{ModelsSelect{}});  // 关闭
        m = std::move(m2);
        (void)cmd;
        for (int f = 0; f < 5; ++f) {
            render_state(m);
            std::this_thread::sleep_for(std::chrono::milliseconds{8});
        }
    }

    std::fprintf(stderr, "  [picker] cycles=4 committed_rows=%zu\n",
                 committed_wire.size());
    close(rfd);
}

