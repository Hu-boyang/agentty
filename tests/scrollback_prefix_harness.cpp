// scrollback_prefix_harness — STANDALONE 打开 -demand 附录
// -only-prefix 预言 agentty↔maya 回滚缓冲区 接口边界.
//
// 原因 此 EXISTS (以及为什么是) 不 测试条目 :
//   完整的先知( 圣灵) tests/scrollback_oracle_test.cpp 驱动器
//   maya 真实的 Runtime::render 以电子邮件方式发送 PTY 整个 agentty 视图
//   + freeze/trim 生命周期——彻底但很重,它生活在后面 AGENTTY_BUILD_TESTS
//   + cest. 此工程的工作流程跳过 cest。 所以 不变量 这一点实际上很重要——
//   提交到原生回滚缓冲区的行，之后绝不能被重写、重排或重复。
//   没有便宜的、可以伸手的支票。
//
//   这条绳子可以填补空白,它能驱动 maya's PUBLIC 内置 -frame
//   API (时间) 渲染 承诺 承诺 表示承诺 表示决心 渲染 直接对极小
//   于 -process 终端 本地模拟器 回滚缓冲区 ,跨过一个超大电池
//   -startup / 已执行 -prefix / 重写 -emit 假设,
//   并声明附件 -only-prefix 属性。它与 一个 g++ 行(
//   无) CMake ,没有 maya 测试目标),运行在一秒以内:
//
//     g++ -std=c++23 -I third_party/maya/include tests/scrollback_prefix_harness.cpp
//         build/third_party/maya/libmaya.a -o /tmp/sbh && /tmp/sbh
//
//   (任何活动) libmaya.a / 对象集提供 InlineFrameState
//   + 画板 + 作家作品; 绳子只触及这三家公共 头文件.)
//
//   最低额 TermEmu + 管道刻录器从
//   test_fresh_oversized_scrollback.cpp - 保持自我 - 保持自已 - 保持
//   自身自已 -contained 这儿,所以绳子有 ZERO 对测试的依赖 源码树's check.hpp 或造胶.

#include "maya/render/inline_frame.hpp"
#include "maya/render/canvas.hpp"
#include "maya/terminal/writer.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

using namespace maya;
using namespace maya::inline_frame;

// ─────────────────────────────────────────────────────────────────────────
// Non-blocking 管线作家 (移植).
// ─────────────────────────────────────────────────────────────────────────
static std::pair<Writer, int> make_pipe_writer() {
    int fds[2];
    if (pipe(fds) != 0) { std::perror("pipe"); std::abort(); }
    fcntl(fds[1], F_SETFL, fcntl(fds[1], F_GETFL, 0) | O_NONBLOCK);
    fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL, 0) | O_NONBLOCK);
    Writer w{static_cast<platform::NativeHandle>(fds[1])};
    return {std::move(w), fds[0]};
}

static std::string read_fd(int rfd) {
    std::string s;
    char buf[8192];
    for (;;) {
        ssize_t n = read(rfd, buf, sizeof(buf));
        if (n > 0) { s.append(buf, static_cast<std::size_t>(n)); continue; }
        break;
    }
    return s;
}

// ─────────────────────────────────────────────────────────────────────────
// TermEmu 最低 ANSI 终端 与本地关系 回滚缓冲区 (发端).
// ─────────────────────────────────────────────────────────────────────────
struct TermEmu {
    int cols, rows;
    int cx = 0, cy = 0;
    std::vector<std::string> screen;
    std::vector<std::string> scrollback;
    bool decawm = true;

    TermEmu(int c, int r) : cols(c), rows(r) {
        screen.assign(static_cast<std::size_t>(rows),
                      std::string(static_cast<std::size_t>(cols), ' '));
    }
    std::string& line(int y) { return screen[static_cast<std::size_t>(y)]; }

    void put(char ch) {
        if (cx >= cols) { if (decawm) { cx = 0; cursor_newline(); } else cx = cols - 1; }
        if (cy < 0) cy = 0;
        if (cy >= rows) cy = rows - 1;
        std::string& l = line(cy);
        if (static_cast<int>(l.size()) < cols) l.resize(static_cast<std::size_t>(cols), ' ');
        l[static_cast<std::size_t>(cx)] = ch;
        ++cx;
    }
    void cursor_newline() {
        if (cy < rows - 1) { ++cy; return; }
        std::string top = screen.front();
        while (!top.empty() && top.back() == ' ') top.pop_back();
        scrollback.push_back(std::move(top));
        screen.erase(screen.begin());
        screen.push_back(std::string(static_cast<std::size_t>(cols), ' '));
    }
    void carriage_return() { cx = 0; }
    void cursor_up(int n)   { cy -= n; if (cy < 0) cy = 0; }
    void cursor_down(int n) { for (int i = 0; i < n; ++i) cursor_newline(); }
    void cursor_fwd(int n)  { cx += n; if (cx > cols) cx = cols; }
    void erase_to_eol() {
        std::string& l = line(cy);
        if (static_cast<int>(l.size()) < cols) l.resize(static_cast<std::size_t>(cols), ' ');
        for (int x = cx; x < cols; ++x) l[static_cast<std::size_t>(x)] = ' ';
    }
    void erase_to_eos() {
        erase_to_eol();
        for (int y = cy + 1; y < rows; ++y) line(y).assign(static_cast<std::size_t>(cols), ' ');
    }
    void feed(const std::string& b) {
        std::size_t i = 0, n = b.size();
        while (i < n) {
            unsigned char ch = static_cast<unsigned char>(b[i]);
            if (ch == '\r') { carriage_return(); ++i; continue; }
            if (ch == '\n') { cursor_newline(); ++i; continue; }
            if (ch == 0x1b) { i = parse_esc(b, i); continue; }
            if (ch >= 0x20) { put(static_cast<char>(ch < 128 ? ch : '?')); ++i; continue; }
            ++i;
        }
    }
    std::size_t parse_esc(const std::string& b, std::size_t i) {
        const std::size_t n = b.size();
        if (i + 1 >= n) return n;
        if (b[i + 1] != '[') return i + 2;
        std::size_t j = i + 2;
        bool priv = false;
        if (j < n && b[j] == '?') { priv = true; ++j; }
        int param = 0; bool have_param = false;
        while (j < n && b[j] >= '0' && b[j] <= '9') { param = param * 10 + (b[j]-'0'); have_param = true; ++j; }
        if (j >= n) return n;
        char final = b[j];
        const int p = have_param ? param : 0;
        switch (final) {
            case 'A': cursor_up(p ? p : 1); break;
            case 'B': cursor_down(p ? p : 1); break;
            case 'C': cursor_fwd(p ? p : 1); break;
            case 'D': cx -= (p ? p : 1); if (cx < 0) cx = 0; break;
            case 'G': cx = (p ? p - 1 : 0); if (cx < 0) cx = 0; break;
            case 'H': cy = 0; cx = 0; break;
            case 'K': erase_to_eol(); break;
            case 'J':
                if (p == 2 || p == 3) {
                    if (p == 3) scrollback.clear();
                    for (auto& l : screen) l.assign(static_cast<std::size_t>(cols), ' ');
                } else erase_to_eos();
                break;
            case 'h': if (priv && p == 7) decawm = true; break;
            case 'l': if (priv && p == 7) decawm = false; break;
            default: break;
        }
        return j + 1;
    }
    std::vector<std::string> transcript() const {
        std::vector<std::string> t = scrollback;
        for (const auto& l : screen) {
            std::string s = l;
            while (!s.empty() && s.back() == ' ') s.pop_back();
            t.push_back(std::move(s));
        }
        return t;
    }
};

// ─────────────────────────────────────────────────────────────────────────
// 利用状态+断言.
// ─────────────────────────────────────────────────────────────────────────
static int g_failures = 0;
static int g_checks   = 0;

static void check(bool ok, const std::string& msg) {
    ++g_checks;
    if (!ok) { ++g_failures; std::fprintf(stderr, "  FAIL: %s\n", msg.c_str()); }
}

static Canvas marked_canvas(int width, int rows, StylePool& pool, const char* prefix) {
    Canvas c(width, rows + 4, &pool);
    auto sid = pool.intern(Style{});
    for (int y = 0; y < rows; ++y) {
        std::string label = std::string(prefix) + "-row-" + std::to_string(y);
        c.write_text(0, y, label, sid);
    }
    return c;
}

// 电话 INVARIANT 检查: 整个记录中两次没有标记行( Q)
// 回滚缓冲区 ++ 可视区域。重复表示承诺行已重新 -emitted
// ——确切的腐败类. 返回第一dup对(或{-1,-1}).
struct DupScan { int a = -1, b = -1; std::string line; int count = 0; };
static DupScan scan_dups(const TermEmu& emu, const char* marker) {
    auto all = emu.transcript();
    DupScan r;
    std::vector<std::pair<std::string,int>> seen;
    for (int y = 0; y < static_cast<int>(all.size()); ++y) {
        const std::string& ln = all[static_cast<std::size_t>(y)];
        if (ln.find(marker) == std::string::npos) continue;
        bool dup = false;
        for (auto& [s, py] : seen)
            if (s == ln) { ++r.count; dup = true; if (r.a < 0) { r.a = py; r.b = y; r.line = ln; } }
        if (!dup) seen.emplace_back(ln, y);
    }
    return r;
}

static void dump(const TermEmu& emu, const char* why) {
    if (!std::getenv("DUMP_TRANSCRIPT")) return;
    auto all = emu.transcript();
    std::fprintf(stderr, "  --- transcript [%s] (%zu sb + %zu view) ---\n",
                 why, emu.scrollback.size(), emu.screen.size());
    for (int y = 0; y < static_cast<int>(all.size()); ++y)
        std::fprintf(stderr, "  %3d|%s%s\n", y,
            y < static_cast<int>(emu.scrollback.size()) ? "" : "V ",
            all[static_cast<std::size_t>(y)].c_str());
}

// 电话 maya 公共证人 可视区域 高度在无 -tty 测试上下文。
using maya::term_rows_for_test;

// ─────────────────────────────────────────────────────────────────────────
// 帧 驱动程序。空 = 弗赖什种子 渲染 和同步的 渲染 有过 不同
// 签名(同步 消耗 a verify() ShadowWitness )),
// 所以他们需要单独的帮助者——正是两者塑造了". 运行时 使用时。
// ─────────────────────────────────────────────────────────────────────────

// 空 / 种子 / 空 的 / 籽 / 尾 的 种子 或 种子。 渲染。释放的种子 字
// 节 进入模拟器,以返回同步臂(我们已到达它)。
static std::optional<InlineFrame<Synced>>
seed_render(TermEmu& emu, int rfd, const Canvas& c, int term_h,
            StylePool& pool, Writer& writer) {
    auto o = InlineFrame<Empty>{}.seed().render(
        c, content_rows(c), term_rows_for_test(term_h), pool, writer,
        /*同步=( ===*/false);
    emu.feed(read_fd(rfd));
    return std::visit([](auto&& a) -> std::optional<InlineFrame<Synced>> {
        using T = std::decay_t<decltype(a)>;
        if constexpr (std::is_same_v<T, InlineFrame<Synced>>) return std::move(a);
        else return std::nullopt;
    }, std::move(o));
}

// (A同步 ) ——核实 —— 渲染 司机会在这里多住 -frame 假设;
// 当前的单 -render / 重写 -emit 完全多档 -frame
// 流式传输 路径覆盖在 PTY 先知会代替他——见 SCOPE 说明
// scn_oversized_startup.)

// ─────────────────────────────────────────────────────────────────────────
// SCENARIO 1 — OVERSIZED STARTUP 每行都放一次,就放一次
//   首个( 弗雷什 ) 渲染 高过画布的 可视区域 必须在顶端(
//   内容 - term_h )行进 回滚缓冲区 财务报告和已审计
//   财务报表 末尾 输入 可视区域 ,每行现成 恰好 读取顺序。
//   这是附件 -only 基数; 上限大小写( A) 或双倍 -emitting
//   将会显示为缺少的行或重复。
//
//   NOTE ON SCOPE : 这个拉带驱动器 maya's RAW
//   InlineFrame API ,这与自己的 prev_cells 阴影却有 不
//   拥有主机 -side 前面 -trim (从下一个画布上删除已承诺的前缀)
//   agentty 视图在 流式传输 帧 . 忠实地模拟渐进式多 -frame
//   GROWTH 因此,需要复制这一条线 -trim ,这正是什么 满 PTY
//   甲骨文 (中文(简体) ).
//   tests/scrollback_oracle_test.cpp 驾驶真实的
//   Runtime::render + agentty 视图中已使用。这种独立的控制带会
//   刻意保持到单项 -render / 重写 -emit 不变量 来,来,来 CAN
//   模型的精确性—— 加载-bearing 腐败类别(已执行的行) -emitted —
//   —而不是那些在船舶上造成严重损失的船舶情况。 "failures" 系系系系错构,
//   不是 maya 错误,诚实的报导比广义的要好 -but-wrong 报道。
// ─────────────────────────────────────────────────────────────────────────
static void scn_oversized_startup(int W, int TERM_H) {
    const int CONTENT = TERM_H + 16;
    StylePool pool;
    auto [writer, rfd] = make_pipe_writer();
    TermEmu emu(W, TERM_H);

    Canvas c = marked_canvas(W, CONTENT, pool, "BOOT");
    auto s = seed_render(emu, rfd, c, TERM_H, pool, writer);
    check(s.has_value(), "startup: oversized first render reaches Synced");
    dump(emu, "startup");

    // 每一个 BOOT-row-k 一次,按上升顺序,全部 CONTENT 排行数.
    auto all = emu.transcript();
    int expected = 0; bool ordered = true;
    for (const auto& ln : all) {
        if (ln.find("BOOT-row-") == std::string::npos) continue;
        if (ln.find("BOOT-row-" + std::to_string(expected)) == std::string::npos) {
            ordered = false; break;
        }
        ++expected;
    }
    check(ordered, "startup: rows appear once each in ascending order");
    check(expected == CONTENT, "startup: all " + std::to_string(CONTENT)
          + " rows present (saw " + std::to_string(expected) + ")");
    auto d = scan_dups(emu, "BOOT-row-");
    check(d.a < 0, "startup: no row duplicated (rows "
                   + std::to_string(d.a) + "/" + std::to_string(d.b) + ")");
    char drain[4096]; while (read(rfd, drain, sizeof(drain)) > 0) {}
    close(rfd);
}

// ─────────────────────────────────────────────────────────────────────────
// SCENARIO 2 - 已执行 -prefix IMMUTABILITY 现场 API 级别。
//   过大之后 渲染 + 承诺(流出), scrollback_prefix_matches
//   一定是,我必须 TRUE 以行者发誓, 字节-identical
//   以至物质溢出, 运行时 安全门,还有...
//   ScrollbackMarker 必须夹住: 您无法执行的行数
//   超过 prev_rows。这些是准确的上游 Runtime::render
//   '门依赖; 将它们钉在这里会发现阴影移/ 前缀中的回归
//   -compare 数学不需要一个满分 帧 循环。
// ─────────────────────────────────────────────────────────────────────────
static void scn_committed_prefix_immutable(int W, int TERM_H) {
    const int CONTENT = TERM_H + 20;
    StylePool pool;
    auto [writer, rfd] = make_pipe_writer();
    TermEmu emu(W, TERM_H);

    Canvas c = marked_canvas(W, CONTENT, pool, "PIN");
    auto s = seed_render(emu, rfd, c, TERM_H, pool, writer);
    check(s.has_value(), "pin: oversized render reaches Synced");
    if (!s) { close(rfd); return; }

    const int overflow = s->rows() - TERM_H;
    check(overflow > 0, "pin: frame overflows the viewport");

    // 承诺的前缀是 字节-identical 转到画布顶端 `overflow`
    // 行——门必须同意.
    check(s->scrollback_prefix_matches(c, overflow),
          "pin: committed prefix matches canvas top (gate would allow diff)");

    // 完毕 -commit 夹: 要求多于 prev_rows 生成一个标记,以 prev_rows
    // (从来没有出过) -of-range ) , 以"换班"为主.
    auto over = s->scrollback_marker(s->rows() + 100);
    check(over.rows() <= s->rows(),
          "pin: over-commit marker clamps to prev_rows ("
          + std::to_string(over.rows()) + " <= " + std::to_string(s->rows()) + ")");

    // A DIVERGENT 画布必须顶端 FAIL 前缀匹配——大门不能绿
    // -light diff 过一个转动的前缀。
    Canvas other = marked_canvas(W, CONTENT, pool, "XXX");  // 不同的标签
    check(!s->scrollback_prefix_matches(other, overflow),
          "pin: divergent prefix correctly FAILS the gate");

    // 抓取标记 之前 移动 -commit 消耗量 `s`.
    auto committed_marker = s->scrollback_marker(overflow);

    // 交纳是好的 -formed : 实施后 `overflow`,
    // prev_rows 多少人来过,就只留下零
    const int before = s->rows();
    auto committed = std::move(*s).commit(committed_marker);
    check(committed.rows() == before - overflow,
          "pin: commit drops exactly `overflow` rows ("
          + std::to_string(committed.rows()) + " == "
          + std::to_string(before - overflow) + ")");
    char drain[4096]; while (read(rfd, drain, sizeof(drain)) > 0) {}
    close(rfd);
}

// ─────────────────────────────────────────────────────────────────────────
// SCENARIO 3 — RE-EMIT 编号 相同 已用过的帆布,
//   渲染 已经 -committed 通过一个超大小画布 FRESH
//   空( 空前缀) 必须 线条重复 —— 这是危险 运行时 通过路径
//   回收避免 Stale/HardReset ,从不裸露的箱子 -A.
//   我们坚持危险 IS 现(验证绳子能探测到) SAFE
//   路径(承诺+) 过期 重绘 已经 不 线条。
// ─────────────────────────────────────────────────────────────────────────
static void scn_reemit_hazard_and_safe(int W, int TERM_H) {
    const int TALL = TERM_H + 16;

    // (a) HAZARD : 空重写 -emit 线条( harness 一定要) DETECT 来,我们来,我们去,我们去
    {
        StylePool pool;
        auto [writer, rfd] = make_pipe_writer();
        TermEmu emu(W, TERM_H);
        Canvas c = marked_canvas(W, TALL, pool, "REEM");
        auto s1 = seed_render(emu, rfd, c, TERM_H, pool, writer);
        check(s1.has_value(), "reemit-hazard: first render Synced");
        // 空空重置 -emit 会 议 日 程 和 议 程 相同 画布(不安全的形状).
        auto s2 = seed_render(emu, rfd, c, TERM_H, pool, writer);
        check(s2.has_value(), "reemit-hazard: re-emit Synced");
        auto d = scan_dups(emu, "REEM-row-");
        check(d.a >= 0, "reemit-hazard: bare re-emit STRANDS a dup (harness detects corruption)");
        char drain[4096]; while (read(rfd, drain, sizeof(drain)) > 0) {}
        close(rfd);
    }

    // (b) SAFE: HardReset-shaped 内容 -emit (擦除前缀)确实 不 线条。
    {
        StylePool pool;
        auto [writer, rfd] = make_pipe_writer();
        TermEmu emu(W, TERM_H);
        Canvas c = marked_canvas(W, TALL, pool, "SAFE");
        auto s1 = seed_render(emu, rfd, c, TERM_H, pool, writer);
        check(s1.has_value(), "reemit-safe: first render Synced");
        // Model HardReset::render: 预置\x1b[2J\x1 b][3J
        // \x11b][H擦擦,然后是赤裸的新鲜的re-emit. x1b[3J] 清除回滚缓冲区,
        // 所以re-scroll推的只有刚开始的-blanked行——没有dup.
        emu.feed(std::string("\x1b[2J\x1b[3J\x1b[H"));
        auto s2 = seed_render(emu, rfd, c, TERM_H, pool, writer);
        check(s2.has_value(), "reemit-safe: wipe re-emit Synced");
        auto d = scan_dups(emu, "SAFE-row-");
        check(d.a < 0, "reemit-safe: HardReset wipe prevents the strand (rows "
                       + std::to_string(d.a) + "/" + std::to_string(d.b) + ")");
        char drain[4096]; while (read(rfd, drain, sizeof(drain)) > 0) {}
        close(rfd);
    }
}

// ────────────────────────────────────────────────────────────────────────────────────
// SCENARIO 4 — ScrollbackMarker GENERATION 绑定 (类型) -theoretic SoT).
//   标记是连接到 IDENTITY 使用标记的标记
//   相同 生成时执行; a STALE 标记(在干预
//   承诺之前发布) REJECTED 作为无 -op
//   - 它永远不会 MISAPPLY a 替换的行数。
//   这是押金 -watermark 第二届 -accountant 按类型分类的课关闭。
// ────────────────────────────────────────────────────────────────────────────────────
static void scn_marker_generation(int W, int TERM_H) {
    const int TALL = TERM_H + 16;
    StylePool pool;
    auto [writer, rfd] = make_pipe_writer();
    TermEmu emu(W, TERM_H);
    Canvas c = marked_canvas(W, TALL, pool, "GEN");
    auto s = seed_render(emu, rfd, c, TERM_H, pool, writer);
    check(s.has_value(), "gen: oversized render reaches Synced");
    if (!s) { close(rfd); return; }

    const int overflow = s->rows() - TERM_H;
    check(overflow > 0, "gen: frame overflows the viewport");

    // 已经存在状态的状态 带来了非 -zero 生成。
    const std::uint64_t gen_before = s->state_generation();
    check(gen_before != 0, "gen: rendered state has non-zero generation ('"
          + std::to_string(gen_before) + "')");

    // 薄荷标记 当前 ,在 gen_before。这是 过期
    // 一次我们尝试在一次介入后再使用
    auto stale = s->scrollback_marker(overflow);
    check(stale.generation() == gen_before,
          "gen: marker captures the issuing state's generation");

    // (a) LEGITIMATE同为-tick承诺:从`s`中
    //     铸造的FRESH标记立即被消耗——同一代人必须成功和进步。
    const int before = s->rows();
    auto fresh = s->scrollback_marker(overflow);
    auto committed = std::move(*s).commit(fresh);
    check(committed.rows() == before - overflow,
          "gen: same-generation commit succeeds (drops exactly overflow: "
          + std::to_string(committed.rows()) + " == "
          + std::to_string(before - overflow) + ")");

    // 承诺推进了一代人——国家身份改变.
    const std::uint64_t gen_after = committed.state_generation();
    check(gen_after != gen_before,
          "gen: commit advances the generation ("
          + std::to_string(gen_before) + " -> "
          + std::to_string(gen_after) + ")");

    // (b) STALE 标记拒绝: `stale` 标记( 仍然在 gen_before
    //     适用于 POST-commit 状态必须是 NO-OP - 整行不变。
    //     没有一代守卫,这个会掉下来的 `overflow` 更多从
    //     阴影中推出的行数( 保证金) -watermark 错误).
    const int rows_pre_stale = committed.rows();
    auto after_stale = std::move(committed).commit(stale);
    check(after_stale.rows() == rows_pre_stale,
          "gen: STALE marker is REJECTED as a no-op (rows unchanged: "
          + std::to_string(after_stale.rows()) + " == "
          + std::to_string(rows_pre_stale) + ")");

    char drain[4096]; while (read(rfd, drain, sizeof(drain)) > 0) {}
    close(rfd);
}

// ────────────────────────────────────────────────────────────────────────────────────
// SCENARIO 5 — ScrollbackProof:渲染门升入型号系统.
//   check_scrollback 是那个 SOLE 证明的制作人
//   InlineFrame<Synced>::渲染 现在 REQUIRES . 三项成果:
//     - 溢出+匹配前缀 → WITNESSED 证明( 绑定状态)
//     - 溢出+调换前缀 + std::nullopt (电话 必须 追回)
//     - 适合你,我爱他 可视区域          → VACUOUS 证明( 3) nullptr , 溢出数 0)
//   不经过判定门直接渲染溢出帧。
//   现在无法合并; 运行时 履行该义务。
// ────────────────────────────────────────────────────────────────────────────────────
static void scn_scrollback_proof(int W, int TERM_H) {
    const int TALL = TERM_H + 16;
    StylePool pool;
    auto [writer, rfd] = make_pipe_writer();
    TermEmu emu(W, TERM_H);
    Canvas c = marked_canvas(W, TALL, pool, "PRF");
    auto s = seed_render(emu, rfd, c, TERM_H, pool, writer);
    check(s.has_value(), "proof: oversized render reaches Synced");
    if (!s) { close(rfd); return; }

    // (a) 过度流+匹配前缀 → WITNESSED 证明。
    auto ok = s->check_scrollback(c, TERM_H);
    check(ok.has_value(),
          "proof: overflow + matching prefix issues a proof");
    if (ok) {
        const int overflow = s->rows() - TERM_H;
        check(ok->valid(), "proof: issued proof is valid (not moved-from)");
        check(ok->overflow_rows() == overflow,
              "proof: witnessed proof records the overflow count ("
              + std::to_string(ok->overflow_rows()) + " == "
              + std::to_string(overflow) + ")");
        check(ok->bound_to() != nullptr,
              "proof: witnessed proof is bound to a state");
    }

    // (b) 流出+ SHIFTED 前缀 __ std::nullopt (必须恢复).
    Canvas shifted = marked_canvas(W, TALL, pool, "ZZZ");  // 不同的标签
    auto bad = s->check_scrollback(shifted, TERM_H);
    check(!bad.has_value(),
          "proof: shifted prefix yields NO proof (render is blocked)");

    // (c) 帧 FITS 可视区域(term_h  ⁇   ⁇ ) − VACUOUS 证明。
    auto vac = s->check_scrollback(c, s->rows() + 5);
    check(vac.has_value(), "proof: non-overflow frame issues a vacuous proof");
    if (vac) {
        check(vac->overflow_rows() == 0,
              "proof: vacuous proof has zero overflow");
        check(vac->bound_to() == nullptr,
              "proof: vacuous proof is unbound (no committed prefix)");
    }

    char drain[4096]; while (read(rfd, drain, sizeof(drain)) > 0) {}
    close(rfd);
}

int main() {
    std::fprintf(stderr, "scrollback_prefix_harness — append-only-prefix oracle\n");

    // 生成方案有意输入 committed() a STALE
    // 标记以证明它被拒绝。在调试中 -built 路径中止的
    // libmaya( 响亮的绊线); 告诉它不要 -op 而不是让
    // 绳子能够坚持拒绝而不是崩溃。 释放libmaya忽略了这一点
    // (已经沉默了不) -op).
    setenv("MAYA_NO_GATE_ABORT", "1", 1);

    // 传播 终端 形状( E) narrow/wide,
    // short/tall ),因此溢出/承诺/前缀 -compare 算术
    // 是跨政权进行的,而不是一个幸运的大小。
    struct Shape { int w, h; };
    const Shape shapes[] = {
        { 80, 24 },
        { 40, 10 },
        { 120, 50 },
        { 60, 8  },
    };
    for (auto s : shapes) {
        scn_oversized_startup(s.w, s.h);
        scn_committed_prefix_immutable(s.w, s.h);
        scn_reemit_hazard_and_safe(s.w, s.h);
        scn_marker_generation(s.w, s.h);
        scn_scrollback_proof(s.w, s.h);
    }

    std::fprintf(stderr, "\n%d checks, %d failures\n", g_checks, g_failures);
    if (g_failures == 0) std::fprintf(stderr, "PASS\n");
    else                 std::fprintf(stderr, "FAIL\n");
    return g_failures == 0 ? 0 : 1;
}
