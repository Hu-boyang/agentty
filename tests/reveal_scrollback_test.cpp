// reveal_scrollback_test - 复制 流式传输 回滚缓冲区 DUPLICATION
// 与 reveal_fx ON (生产观音道).
//
// 电话 scrollback_wire_fuzz 利用特意使用武力
// reveal_fx OFF 这么说吧 帧 高度是模型状态的一个纯函数。
// 但是在生产中报告的bug( 同一流派线在 20 年中两次出现)。
// 回滚缓冲区 现场直播时 数据流 ) 仅显示为 reveal_fx
// ON - 现场直播 -edge 覆盖面板 (可移动/梯度/) 光标
// ) 改变帆布的后行 每个 帧 独立,不管是否新建 字节 到了,
// 如果这 -frame 变异曾经触碰过一行 ALREADY 溢出过 可
// 视区域 上,致力于本土 回滚缓冲区, maya 或重写已承诺的行
// ( W4 或违反《公约》的行为)或 shrink/prefix
// 调和 -commits a 转录的复制件 – 可见的复制件。
//
// 这个测试驱动 真实 ui::view (reveal_fx ON ,
// 完全与 turn.cpp 建起来)通过 maya's 真实 内在的编曲,
// 流式传输 长的传言 正文 将小部分溢出, 可视区域 继续 渲染 帧 (
// 推进墙壁) -clock 所以,我们 逐步显示 覆盖面板 动画),并称:
//
//   R1 无 传输 排,曾经承诺过 可视区域 上面,
//       以后再重写一次 帧 (被困难者) -duplicate 检查).
//   R2 每隔一段时间 渲染 保持同步( 无) Stale/HardReset 满 重绘).
//   R3 编号 prev_cells 阴影在每一次检查前 渲染.
//   R4 无 UNIQUE 单张制成的画布中两次出现传言线(元素)
//       -tree 双倍 -render 检查).
//   R5  该 SCREENSHOT SYMPTOM。一个真实的 ANSI
//       终端 模拟器( E) TermEmu )消耗实际 BYTES maya
//       写给fd, 保持本地 回滚缓冲区 + 可视区域 . 任何独特的传言
//       线不得在累积记录中两次出现( 回滚缓冲区 ++屏幕)——完全如用户的眼所见.
//       R1-R4 重建行从 maya's CANVAS ,因此无法看到
//       内容重 -committed 改为本地 回滚缓冲区 由来自
//       -the-top 重绘; R5 ,它能捕捉到生产重复:在固定的-
//       冻结的交接时, reveal_fx 部件的已结算元素 源码树 与
//       上一个不同 LIVE (逐步显示-animated) 源码树 ,
//       所以,我说 maya 内容 -emits 整个转弯,降落a 第二
//       现场直播下方的清洁复制件( Q) SGR-garbled ) 复制
//       本已经输入 回滚缓冲区.
//
// 失败意味着 逐步显示 覆盖面板 (或 减 减 height/shape
// 意味着) 扰乱了承诺 回滚缓冲区 前缀或强制 a 来源 -the-top
// 内容 -emit ——生产重复错误.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#if defined(__APPLE__)
#include <util.h>       // 打开 macOS
#else
#include <pty.h>        // 闪光灯上的开口(链接 -lutil)
#endif
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <maya/core/anim_clock.hpp>
#include <maya/render/canvas.hpp>
#include <maya/render/inline_frame.hpp>
#include <maya/render/renderer.hpp>
#include <maya/render/serialize.hpp>
#include <maya/style/theme.hpp>
#include <maya/terminal/writer.hpp>

#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/view/view.hpp"
#include "agentty/tool/registry.hpp"

using agentty::Model;
using agentty::Message;
using agentty::Role;
using agentty::ToolUse;
using namespace maya;
using namespace maya::inline_frame;

namespace {  // 归并: TU-local (捆绑成 agentty_standalone_tests)
static int g_failures = 0;
static int g_checks   = 0;

#define CHK(cond, detail)                                              \
    do { ++g_checks; if (!(cond)) {                                    \
        ++g_failures;                                                  \
        std::fprintf(stderr, "  FAIL: %s\n", std::string(detail).c_str()); \
    } } while (0)

// 画画 真实 视图通过 render_tree 以大小为上下文。
static Canvas paint_view(const Model& m, int width, int term_h,
                         StylePool& pool) {
    maya::RenderContext ctx{width, term_h, maya::render_generation(),
                            /*auto_height=*/true};
    maya::RenderContextGuard guard(ctx);
    Canvas c(width, 8000, &pool);
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

static std::pair<Writer, int> make_pipe_writer() {
    int fds[2];
    if (pipe(fds) != 0) { std::perror("pipe"); std::abort(); }
    fcntl(fds[1], F_SETFL, fcntl(fds[1], F_GETFL, 0) | O_NONBLOCK);
    fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL, 0) | O_NONBLOCK);
    return {Writer{static_cast<platform::NativeHandle>(fds[1])}, fds[0]};
}

// 安装 a PTY 双对作为进程 STDOUT ,大小为(cols x rains)。
// CRITICAL: agentty's frozen.cpp 大小的修饰
// commit_scrollback (N)关 term_dims (),
// 查询ioctl() TIOCGWINSZ 继续 STDOUT_FILENO .
// 在ioctl失败并倒回80x40的平坦管试验中——与绳索真实性(width
// x)有分歧的几何. term_h) 渲染 ,所以修剪结束 -commits
// 并线条重复(a) TEST 保护人类的文物 PRODUCTION 冻结
// +trim 路径实际上是正确的。 备份 stdout 与真实的 PTY
// 大小到带子几何 term_dims () 返回 相同 大小 渲染器 上面写着,
// 所以修饰的证明是几何 -consistent ——正是生产.
// 不变量 返回主页(读侧);写入 maya 发送到 STDOUT 回来
static int install_pty_stdout(int cols, int rows) {
    int master = -1, slave = -1;
    if (openpty(&master, &slave, nullptr, nullptr, nullptr) != 0) {
        std::perror("openpty");
        return -1;
    }
    struct winsize ws{};
    ws.ws_col = static_cast<unsigned short>(cols);
    ws.ws_row = static_cast<unsigned short>(rows);
    ioctl(slave, TIOCSWINSZ, &ws);
    // 重定向 STDOUT 奴隶如此 term_dims () iOCTL 打开
    // STDOUT_FILENO 看见( cols x rains)。 奴隶也是 fd。 maya 写过
    dup2(slave, STDOUT_FILENO);
    close(slave);
    fcntl(master, F_SETFL, fcntl(master, F_GETFL, 0) | O_NONBLOCK);
    return master;
}

static void drain_fd(int rfd) {
    char buf[8192];
    while (read(rfd, buf, sizeof(buf)) > 0) {}
}

// 读取 全部 目前情况 -available 字节 从管子变成弦(弦) 帧
// maya 刚刚写了 -blocking fd; 我们循环到 EAGAIN.
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

// ───────────────────────────────────────────────────────────────────────
// 一个真实的 ANSI 终端 本地模拟器 回滚缓冲区.
//
// 画布 -reconstruction 牵引( 4) 机 Harness::frame
// )只能看到什么 maya's CANVAS 描述单曲 帧 - 它看不到
// 实际的 BYTES maya 写给fd, 也不要写什么 终端's
// NATIVE SCROLLBACK 累积为从顶部滚动的行。生产重复错误(
// 让我检查图表源码。
// 显示 TWICE 累计数 终端 状态: 行承诺 回滚缓冲区 ,
// 然后一个后 帧's cursor-up/EL/scroll 跳舞再现
// -paints 里面的同样内容 可视区域 - 所以现在屏幕显示它英寸 两者 地点
//
// 此模拟器会消耗 maya ' 准确的逃逸词汇表( coursor) up/down/ 前进,
// EL \x1b[K, ED \x1b [J,\r,\n 与底部 -edge 滚动到 回滚缓冲区,
// DECAWM ?7l, SGR 同步吗? 2026h/l, hide/show 光标,
// 指向 HardReset 擦拭\x1b[2J\x1 b[3J\x11b[H]并维护:
//   - `rows`线的可视区域,每条宽`cols`
//   - 本地的回滚缓冲区矢量(从顶部滚动的行数)
// 测试可以重复检查 回滚缓冲区 + 可视区域 TOGETHER ,
// 正是用户的眼所见。
struct TermEmu {
    int cols, rows;
    int cx = 0, cy = 0;                  // 光标( 光标) ( 可视区域-relative)
    std::vector<std::string> screen;    // 行, 每一列字符
    std::vector<std::string> scrollback;// 已执行行(不可变历史)
    bool decawm = true;                 // 默认自动-wrap

    TermEmu(int c, int r) : cols(c), rows(r) {
        screen.assign(static_cast<std::size_t>(rows),
                      std::string(static_cast<std::size_t>(cols), ' '));
    }

    std::string& line(int y) { return screen[static_cast<std::size_t>(y)]; }

    void put(char ch) {
        if (cx >= cols) {
            if (decawm) { cx = 0; cursor_newline(); }
            else        { cx = cols - 1; }   // 右边缘夹住
        }
        if (cy < 0) cy = 0;
        if (cy >= rows) cy = rows - 1;
        std::string& l = line(cy);
        if (static_cast<int>(l.size()) < cols)
            l.resize(static_cast<std::size_t>(cols), ' ');
        l[static_cast<std::size_t>(cx)] = ch;
        ++cx;
    }

    // \n 下行滚动 可视区域 上; 离开顶部的行是致力于
    // 本地 回滚缓冲区 (确切地说,是一个真实的, 终端).
    void cursor_newline() {
        if (cy < rows - 1) { ++cy; return; }
        // 卷起一个
        std::string top = screen.front();
        while (!top.empty() && top.back() == ' ') top.pop_back();
        scrollback.push_back(std::move(top));
        screen.erase(screen.begin());
        screen.push_back(std::string(static_cast<std::size_t>(cols), ' '));
        // cy 留在第1行 继续
    }

    void carriage_return() { cx = 0; }
    void cursor_up(int n)   { cy -= n; if (cy < 0) cy = 0; }
    void cursor_down(int n) { for (int i = 0; i < n; ++i) cursor_newline(); }
    void cursor_fwd(int n)  { cx += n; if (cx > cols) cx = cols; }

    void erase_to_eol() {  // \x1b[K
        std::string& l = line(cy);
        if (static_cast<int>(l.size()) < cols)
            l.resize(static_cast<std::size_t>(cols), ' ');
        for (int x = cx; x < cols; ++x) l[static_cast<std::size_t>(x)] = ' ';
    }

    void erase_to_eos() {  // \x1b [J] - 擦除光标 - > EOL 那么,在这排,然后
        erase_to_eol();    // 下方每行都一样
        for (int y = cy + 1; y < rows; ++y)
            line(y).assign(static_cast<std::size_t>(cols), ' ');
    }

    void full_wipe() {  // \x1b [2J\x1 b][3J\x11b][H] — 清屏+回滚缓冲区
        for (auto& l : screen) l.assign(static_cast<std::size_t>(cols), ' ');
        scrollback.clear();
        cx = cy = 0;
    }

    // 提供一块块 maya's 传输 字节.
    void feed(const std::string& b) {
        std::size_t i = 0, n = b.size();
        while (i < n) {
            unsigned char ch = static_cast<unsigned char>(b[i]);
            if (ch == '\r') { carriage_return(); ++i; continue; }
            if (ch == '\n') { carriage_return_optional_then_lf(); ++i; continue; }
            if (ch == 0x1b) { i = parse_esc(b, i); continue; }
            // 可打印( 处理任意 QX 0x20) 字节 作为格; 多字节 UTF
            // -8 lead/continuation 字节 每一个占1个 存储
            // 单元 粗体模型中的插槽——对 ASCII-only 测试流言).
            if (ch >= 0x20) { put(static_cast<char>(ch < 128 ? ch : '?')); ++i; continue; }
            ++i; // 忽略其他 C0
        }
    }

    // plain\n in maya 输出将线程推进( 需要时,
    // 其前方为内容 \r)。 Maya 会在行间发出\r\n;
    // \r 已经重置 cx。 滚动循环时的赤色\n 必须在底部滚动。
    void carriage_return_optional_then_lf() { cursor_newline(); }

    // 解析 单曲 CSI/escape 顺序从 b[i] (b[i]  ⁇ )开始 ESC).
    // 返回刚刚经过序列的索引。
    std::size_t parse_esc(const std::string& b, std::size_t i) {
        const std::size_t n = b.size();
        if (i + 1 >= n) return n;
        if (b[i + 1] != '[') return i + 2;   // un-CSI esc: 跳过 2
        std::size_t j = i + 2;
        bool priv = false;
        if (j < n && b[j] == '?') { priv = true; ++j; }
        int param = 0; bool have_param = false;
        while (j < n && b[j] >= '0' && b[j] <= '9') {
            param = param * 10 + (b[j] - '0'); have_param = true; ++j;
        }
        if (j >= n) return n;
        char final = b[j];
        const int p = have_param ? param : 0;
        switch (final) {
            case 'A': cursor_up(p ? p : 1); break;
            case 'B': cursor_down(p ? p : 1); break;
            case 'C': cursor_fwd(p ? p : 1); break;
            case 'D': cx -= (p ? p : 1); if (cx < 0) cx = 0; break;
            case 'G': cx = (p ? p - 1 : 0); if (cx < 0) cx = 0; break;
            case 'H': cy = 0; cx = 0; break;       // CUP( 此处只使用家)
            case 'K': erase_to_eol(); break;       // EL 0
            case 'J':
                if (p == 2 || p == 3) {            // ED 2/3 — 清屏
                    if (p == 3) scrollback.clear();
                    for (auto& l : screen)
                        l.assign(static_cast<std::size_t>(cols), ' ');
                } else {
                    erase_to_eos();                // ED 0
                }
                break;
            case 'h': if (priv && p == 7) decawm = true; break;  // DECAWM 打开
            case 'l': if (priv && p == 7) decawm = false; break; // DECAWM 关
            case 'm': break;                        // SGR — 忽略样式
            default: break;                         // ?25h/l, ?2026h/l 等 类.
        }
        return j + 1;
    }

    // 完全可见 -to-the-user 记录: 回滚缓冲区 然后显示屏幕。
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

// 传输 牵引: 载 maya 同步 帧 横跨 帧 ,模拟所犯(
// 不可变) 回滚缓冲区 列, 检查 R1/R2/R3 每隔一段时间 帧.
struct Harness {
    int width, term_h;
    StylePool pool;
    Writer writer;
    int rfd;
    std::optional<InlineFrame<Synced>> synced;
    std::vector<std::string> wire;   // 承诺行数( 不可变 永远)
    std::size_t commits = 0;
    std::size_t last_wrote = 0;      // 字节 maya 上一篇写在 帧 上
    bool dead = false;
    TermEmu emu;                     // 真实 终端: 回滚缓冲区 + 可视区域

    Harness(int w, int th)
        : width(w), term_h(th), emu(w, th) {
        auto pw = make_pipe_writer();
        writer = std::move(pw.first);
        rfd = pw.second;
    }
    ~Harness() { close(rfd); }

    // R5 : 运行重复检查 完整 累计数 终端 记录
    // (内部) 回滚缓冲区 + 当前 可视区域 )——
    // 用户所看到的正是。 -prose 线条出现在两个
    // 不同的绝对行的活屏幕+ 回滚缓冲区 是截图错误 :
    // 同样的 正文 一次画两处,我们只画两处 标志 线条,
    // 在 $ 上同时显示 -screen VIEWPORT
    // 区域(所报用户的可见复制件)——一条合法深入的线
    // 回滚缓冲区 和 内容 -shown 生活就是腐败
    void check_emu(const std::string& tag) {
        auto scr = emu.scrollback;
        // 可视区域 (拖拉)行,空的 -trimmed:
        std::vector<std::string> view;
        for (const auto& l : emu.screen) {
            std::string s = l;
            while (!s.empty() && s.back() == ' ') s.pop_back();
            view.push_back(std::move(s));
        }
        auto is_prose = [](const std::string& ln) {
            return ln.find("tracing call path-")     != std::string::npos
                || ln.find("serializes runs batch-") != std::string::npos
                || ln.find("diffs canvas-")          != std::string::npos
                || ln.find("builds tree-")           != std::string::npos;
        };
        // 完整记录= 回滚缓冲区 ++ 可视区域。
        // 索引空间是绝对的( 回滚缓冲区 一种独特的传教线必须
        // 最多出现在整个世界的一次。 整个 记录
        std::vector<std::string> all = scr;
        for (auto& v : view) all.push_back(v);
        std::vector<std::pair<std::string,int>> seen;
        for (int y = 0; y < static_cast<int>(all.size()); ++y) {
            const std::string& ln = all[static_cast<std::size_t>(y)];
            if (!is_prose(ln)) continue;
            for (auto& [s, py] : seen) {
                if (s == ln) {
                    CHK(false,
                        "R5: line duplicated in terminal transcript @" + tag
                        + " abs-rows " + std::to_string(py) + " and "
                        + std::to_string(y)
                        + " (scrollback=" + std::to_string(scr.size())
                        + ")\n      |" + ln + "|");
                    std::fprintf(stderr,
                        "    --- terminal transcript (%zu sb + %zu view) ---\n",
                        scr.size(), view.size());
                    for (int yy = 0; yy < static_cast<int>(all.size()); ++yy)
                        std::fprintf(stderr, "    %3d|%s%s\n", yy,
                            yy < static_cast<int>(scr.size()) ? "" : "V ",
                            all[static_cast<std::size_t>(yy)].c_str());
                    dead = true;
                    return;
                }
            }
            seen.emplace_back(ln, y);
        }

        // ── R6: 该 COMPOSER-DUPLICATION SYMPTOM ────────────────────
        // 转弯后,报告的错误是 第二 编曲输入框的
        // 复制件搁浅了一个屏幕( 原生) 回滚缓冲区 ),
        // 而现场作曲家骑着 可视区域 下边。
        // 作曲家闲置的占位符行 渲染 "❯ type a message…" ;
        // 非 类型 -ASCII 提示词 标签 降解为粗体中的空格
        // TermEmu 但是,我们... ASCII 子字符串
        // "type a message" 稳定,必须出现 AT MOST
        // 一次 整个记录( 回滚缓冲区 ++ 可视区域。
        // 两次事件=用户看到的被困重复。
        auto is_composer = [](const std::string& ln) {
            return ln.find("type a message")      != std::string::npos
                || ln.find("streaming \xe2\x80\x94 type to queue")
                       != std::string::npos;
        };
        int composer_hits = 0;
        int first_composer = -1;
        for (int y = 0; y < static_cast<int>(all.size()); ++y) {
            if (!is_composer(all[static_cast<std::size_t>(y)])) continue;
            ++composer_hits;
            if (first_composer < 0) first_composer = y;
            else {
                CHK(false,
                    "R6: composer duplicated in terminal transcript @" + tag
                    + " abs-rows " + std::to_string(first_composer) + " and "
                    + std::to_string(y)
                    + " (scrollback=" + std::to_string(scr.size()) + ")");
                std::fprintf(stderr,
                    "    --- terminal transcript (%zu sb + %zu view) ---\n",
                    scr.size(), view.size());
                for (int yy = 0; yy < static_cast<int>(all.size()); ++yy)
                    std::fprintf(stderr, "    %3d|%s%s\n", yy,
                        yy < static_cast<int>(scr.size()) ? "" : "V ",
                        all[static_cast<std::size_t>(yy)].c_str());
                dead = true;
                return;
            }
        }
    }

    // 应用修饰 commit_scrollback(N) Cmd 改为 maya
    // 同步状态 之前 下一个 渲染 - 到底发生了什么? 运行时 已经。
    // meta.cpp / modal.cpp 向上移动修饰 Cmd 司机把
    // 它变成 synced.commit(scrollback_marker (单位(N,
    // prev_rows-term_h )),反对: PRIOR 帧 仍然在
    // -valid 同步状态( N) commit_inline_overflow
    // 没有这个,绳子就与生产和冻结不同 帧 内容 -emits 整个转弯——但
    // 这种分歧是: ALSO 真正的bug表面, 所以我们保持两个代码路径的运行。
    void apply_trim(agentty::Cmd& cmd) {
        if (dead || !synced) return;
        const auto* c = std::get_if<maya::CommitScrollback>(&cmd.inner);
        if (!c) return;
        const int prev_rows = synced->rows();
        const int safe = std::min(c->debt.rows(), std::max(0, prev_rows - term_h));
        if (safe <= 0) return;
        synced = std::move(*synced).commit(synced->scrollback_marker(safe));
        commits += static_cast<std::size_t>(safe);
    }

    void frame(const Model& m, const std::string& tag) {
        if (dead) return;
        Canvas c = paint_view(m, width, term_h, pool);
        auto cur = rows_of(c, width);
        const int R = static_cast<int>(cur.size());
        if (R <= 0) return;

        // R4: WITHIN-FRAME 同一异体异体异形线不得
        // 出现在两行的异体异种中 唯一 直接显示的截图症状:
        // 让我检查图表源码。
        // 一次可见两次表示元素 源码树 本身将文本带入两次(
        // 冻结前缀) 和 现场直播 末尾 ,或两个活的子
        // -turns )——一种观点 -layer 窃听 传输
        // 不变量 (R1-R3 看不到,因为每个 帧 内部一致。
        {
            std::vector<std::pair<std::string,int>> seen;
            for (int y = 0; y < R; ++y) {
                const std::string& ln = cur[static_cast<std::size_t>(y)];
                // 只检查测试 UNIQUE 传言 正文 线条。
                // 每个 paragraph/step 假设情景 数据流 带有
                // 一个独特的整数标记( Q) "tracing call path-N",
                // "diffs canvas-N" ),所以一个 TRUE 内部
                // -frame 重复其中之一表示相同信息 正文 两次制成( 冻结
                // 前缀) 和 现场直播 末尾 ,或子 -turn 由两个地方制成)
                // ——精确截图bug. Tool-card 铬( 含铬)
                // "Read 8 lines", "ACTIONS" 每个小组合法重复,
                // 并被故意排除在外。
                const bool is_test_prose =
                    ln.find("tracing call path-") != std::string::npos
                 || ln.find("serializes runs batch-") != std::string::npos
                 || ln.find("diffs canvas-") != std::string::npos
                 || ln.find("builds tree-") != std::string::npos;
                if (!is_test_prose) continue;
                bool is_dup = false;
                for (auto& [s, py] : seen) {
                    if (s == ln) {
                        CHK(false,
                            "R4: line duplicated within one frame @" + tag
                            + " rows " + std::to_string(py) + " and "
                            + std::to_string(y) + "\n      |" + ln + "|");
                        std::fprintf(stderr, "    --- full frame (%d rows) ---\n", R);
                        for (int yy = 0; yy < R; ++yy)
                            std::fprintf(stderr, "    %3d|%s\n", yy,
                                         cur[static_cast<std::size_t>(yy)].c_str());
                        is_dup = true;
                        dead = true;
                        break;
                    }
                }
                if (is_dup) return;
                seen.emplace_back(ln, y);
            }
        }

        if (!synced) {
            auto o = InlineFrame<Empty>{}.seed().render(
                c, content_rows(c), term_rows_for_test(term_h), pool,
                writer, false);
            emu.feed(read_fd(rfd));
            synced = std::visit(
                [](auto&& a) -> std::optional<InlineFrame<Synced>> {
                    using T = std::decay_t<decltype(a)>;
                    if constexpr (std::is_same_v<T, InlineFrame<Synced>>)
                        return std::move(a);
                    else return std::nullopt;
                }, std::move(o));
            CHK(synced.has_value(), "R2: first frame not Synced @" + tag);
            if (!synced) { dead = true; return; }
        } else {
            // 镜像 maya's app.cpp 唯一 回滚缓冲区-invariant 门 :
            // 如果该 帧 谓 OVERFLOWED 承诺的溢出前
            // 缀不再与 传输 ,每笔 -row diff 不安全
            // ( 它会重写 ) 不可变 本地语言 回滚缓冲区).
            // 回收 之前 diff —— 完全一个memcmp,
            // 用于缩水/ 生长/ 任何未来原因 :
            //   • 增长(R > prev_rows (B) 将重新 -overflow ,所以,我说
            //     HardReset(\x1b[2J\x1 b][3J\x11b[H擦擦+新鲜油漆]);
            //   • 不增长:承诺 -viewport 行+软 -repaint.
            const int prev_rows = synced->rows();
            if (prev_rows > term_h
                && !synced->scrollback_prefix_matches(c, prev_rows - term_h)) {
                const int overflow = prev_rows - term_h;
                if (R > prev_rows) {
                    // 成长+前缀移位 → HardReset.
                    auto hr = std::move(*synced).demote_to_hard_reset();
                    auto o = std::move(hr).render(
                        c, content_rows(c), term_rows_for_test(term_h), pool,
                        writer, false);
                    { std::string b = read_fd(rfd); last_wrote = b.size(); emu.feed(b); }
                    // HardReset 擦净本地 回滚缓冲区 (。
                    // x1b[3J]和 重绘 从顶部: 传输 阴影
                    // 已经过去 过期 ,所以重置它 匹配新鲜的油漆。
                    wire.clear();
                    commits = 0;
                    synced = std::visit(
                        [](auto&& a) -> std::optional<InlineFrame<Synced>> {
                            using T = std::decay_t<decltype(a)>;
                            if constexpr (std::is_same_v<T, InlineFrame<Synced>>)
                                return std::move(a);
                            else return std::nullopt;
                        }, std::move(o));
                    CHK(synced.has_value(),
                        "R3: hard-reset recovery did not return Synced @" + tag);
                    if (!synced) { dead = true; return; }
                    if (std::getenv("REVEAL_DBG"))
                        std::fprintf(stderr, "[dbg] %-14s HARD-RESET grow\n",
                                     tag.c_str());
                    check_emu(tag);
                    return;
                }
                // 不生长+前缀移位 + 承诺 + 软-repaint.
                synced = std::move(*synced).commit(
                    synced->scrollback_marker(overflow));
                commits += static_cast<std::size_t>(overflow);
            }
            auto wit = synced->verify();
            if (!wit) {
                // 真实-RUNTIME RECOVERY (
                // app.cpp :一个被毒害的影子确实如此 不
                // 关闭会话——它降级到 过期 下一个 渲染 写作
                // 时(B) 帧 已经溢出 可视区域,
                // app.cpp 先把关了 -viewport 排队,
                // 然后降级。 Model 这儿,所以,
                // 绳子锻炼 相同 制作的路程是阴暗的平坦—— 使作曲家
                // 陷入困境的道路。 R3 ,将案件隐藏在(B)
                // -serialize 完全没有。 )
                const int prev_rows = synced->rows();
                if (prev_rows > term_h) {
                    const int overflow = prev_rows - term_h;
                    synced = std::move(*synced).commit(
                        synced->scrollback_marker(overflow));
                    commits += static_cast<std::size_t>(overflow);
                }
                auto stale = std::move(*synced).demote_to_stale();
                auto o = std::move(stale).render(
                    c, content_rows(c), term_rows_for_test(term_h), pool,
                    writer, false);
                { std::string b = read_fd(rfd); last_wrote = b.size(); emu.feed(b); }
                synced = std::visit(
                    [](auto&& a) -> std::optional<InlineFrame<Synced>> {
                        using T = std::decay_t<decltype(a)>;
                        if constexpr (std::is_same_v<T, InlineFrame<Synced>>)
                            return std::move(a);
                        else return std::nullopt;
                    }, std::move(o));
                CHK(synced.has_value(),
                    "R3: stale recovery did not return Synced @" + tag);
                if (!synced) { dead = true; return; }
            } else {
                // 回滚缓冲区 门,类型 -enforced: Synced::render
                // REQUIRES 证明已承诺的 -viewport 前
                // 缀为 字节-stable 我们跑 maya 上面的
                // 恢复( 承诺在前缀上溢出) -changed 收缩),
                // 所以 帧 符合或承诺的前缀稳定,
                // check_scrollback 薄荷能证明。如果它仍然返回无
                // 源( 转换前置) -commit ⊿Τ ⁇ 描 maya
                // 准确: 执行关闭 -viewport 细行和柔软
                // -repaint 途经 过期.
                auto proof = synced->check_scrollback(c, term_h);
                maya::inline_frame::RenderOutcome o = [&] {
                    if (proof) {
                        return std::move(*synced).render(
                            c, content_rows(c), term_rows_for_test(term_h),
                            pool, writer, std::move(*wit), std::move(*proof),
                            false);
                    }
                    const int prev_rows = synced->rows();
                    const int overflow = prev_rows > term_h
                        ? prev_rows - term_h : 0;
                    auto committed = std::move(*synced).commit(
                        synced->scrollback_marker(overflow));
                    commits += static_cast<std::size_t>(overflow);
                    return maya::inline_frame::RenderOutcome{
                        std::move(committed).demote_to_stale()};
                }();
                { std::string b = read_fd(rfd); last_wrote = b.size(); emu.feed(b); }
                synced = std::visit(
                    [](auto&& a) -> std::optional<InlineFrame<Synced>> {
                        using T = std::decay_t<decltype(a)>;
                        if constexpr (std::is_same_v<T, InlineFrame<Synced>>)
                            return std::move(a);
                        else return std::nullopt;
                    }, std::move(o));
                CHK(synced.has_value(), "R2: render demoted out of Synced @" + tag);
                if (!synced) { dead = true; return; }
            }
        }

        // R1: 传输-model 字节 检查。 传输 索引输入 +
        // y 字行经过 可视区域 顶部( R - term_h
        // )从现在开始,任何已经 -captured 索引必须匹配。
        //
        // CAVEAT : 数据 reveal_fx 覆盖面板 瞬间
        // INFLATES 画布高度( 幽灵) -extend + 摇摆
        // 末尾 活在边缘上,没有那些排 曾经身体滚动 离开真实 终端。
        // 所以画布 -derived `R - term_h` 边界可以
        // 立即标注 IN-VIEWPORT 以行为 "committed"
        // 然后这行就合法地变换了下一个 帧 也就是说, 不 腐败——
        // 没有任何东西卷土重来。 不可变 - 排位是... - 排子是...
        // - I'm... EMULATOR 本地人 : 回滚缓冲区 (从真实的 字节。
        // 只治疗一个 传输 承诺的指数 -immutable 一旦模拟器
        // 实际将其滚动(wi < emu.scrollback.size()).
        // R5 下面是 -screen-duplication 地面真相;
        // R1 更紧 传输-immutability 警卫,这里有真正的
        // 卷轴 -off 排成这样 逐步显示 高度焦躁不能错 -positive.
        const int over = R > term_h ? R - term_h : 0;
        const std::size_t real_sb = emu.scrollback.size();
        for (int y = 0; y < over; ++y) {
            const std::size_t wi = commits + static_cast<std::size_t>(y);
            if (wi < wire.size()) {
                // 只有确定REAL 终端对回滚缓冲区的承诺行
                // 不可更改;在-viewport行(即使过
                // 了膨胀的画布边界)中,仍然可能改变。
                if (wi < real_sb && wire[wi] != cur[static_cast<std::size_t>(y)]) {
                    CHK(false,
                        "R1: committed wire row " + std::to_string(wi)
                        + " REWRITTEN @" + tag
                        + "\n      was |" + wire[wi]
                        + "|\n      now |" + cur[static_cast<std::size_t>(y)] + "|");
                    dead = true;
                    return;
                }
                // 保持阴影跟踪最新消息 -viewport
                // 包含这样一个后期的真卷轴 -off 比较右侧
                // 字节.
                if (wi >= real_sb) wire[wi] = cur[static_cast<std::size_t>(y)];
            } else if (wi == wire.size()) {
                wire.push_back(cur[static_cast<std::size_t>(y)]);
            } else {
                wire.resize(wi, std::string{});
                wire.push_back(cur[static_cast<std::size_t>(y)]);
            }
        }

        // R5 :截图症状——在上下进行重复检测.
        // 真实 终端 记录(内部) 回滚缓冲区 + 可视
        // 区域 ) 实际 字节 maya 写,不是画布重建。
        if (std::getenv("REVEAL_DBG")) {
            std::fprintf(stderr,
                "[dbg] %-14s R=%d prev=%d commits=%zu sb=%zu wrote=%zu\n",
                tag.c_str(), R,
                synced ? synced->rows() : -1, commits,
                emu.scrollback.size(), last_wrote);
        }
        check_emu(tag);
    }
};

// 推进 ANIMATION 时钟一拍,所以 逐步显示 覆盖面板 '时间源
// 移动于 帧 动画连在一线的边缘都会变异 帧 不存在新情况 字节 抵达(
// 生产量) RAF 使用量(cadence).
// maya::testing::advance_anim_clock_ms 而不是
// 真正的睡眠: 每次 -driven 部件读取 maya::anim_now_ms ,
// 所以同样的20毫秒 ANIMATION 时间以零墙价通过——用于花费大于其
// ~280 s的95%的套房 运行时 睡着了。
static void tick() {
    maya::testing::advance_anim_clock_ms(20);
}

// 开动生产 流式传输 粗鲁的 reveal_fx ON (真实的)
// ui::view 构建 StreamingMarkdown 与
// set_reveal_fx ┮и碞倒硂模 渲染 ——视图安装部件).
static void run_scenario(int width, int term_h) {
    setenv("LINES", std::to_string(term_h).c_str(), 1);
    setenv("COLUMNS", std::to_string(width).c_str(), 1);
    const int pty_master = install_pty_stdout(width, term_h);

    Model m;
    // 大小达到 状态更新函数 并透过 Model ,作为 TerminalResized
    // 将会送来
    m.ui.term_cols = width;
    m.ui.term_rows = term_h;
    m.d.current.id = agentty::ThreadId{"reveal"};
    m.d.available_models.push_back({});
    m.d.available_models.back().id = agentty::ModelId{"claude-opus-4-1"};
    agentty::app::detail::clear_frozen(m);

    Harness h(width, term_h);
    h.frame(m, "welcome");

    // (1) 固定用户转弯,冻结——使数据流承诺
    //    上面的前缀 之后的溢出会降落在某个地方
    {
        Message u; u.role = Role::User;
        u.text = "Please walk through the whole rendering pipeline in detail.";
        m.d.current.messages.push_back(std::move(u));
        agentty::app::detail::freeze_through(m, m.d.current.messages.size());
        h.frame(m, "submit");
    }

    // 2) 开始流式传输 的助手答复.
    Message a; a.role = Role::Assistant;
    a.streaming_text = "Let me check the diagram source:";
    m.d.current.messages.push_back(std::move(a));
    m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};

    // 渲染 多次打开 无需 新建( N) 字节 这是关键的
    // 窗口: 逐步显示 覆盖面板 动画在自己的时钟上显示活的边缘,
    // 而 字节 内容被冻结。如果 覆盖面板 或它的隐含高度
    // 扰动已经 -committed 排队 R1 火势
    for (int f = 0; f < 6; ++f) { tick(); h.frame(m, "open" + std::to_string(f)); }

    // 3) 数据流 长 正文 使小的, 可视区域 所以,我们
    //    开通线( 3) "Let me check the diagram source:"
    //    ) 滚动过顶部,并承诺 回滚缓冲区 保留 渲染 与
    //    逐步显示 动画在每一三角洲之间
    auto& back = m.d.current.messages.back();
    for (int d = 0; d < 60; ++d) {
        back.streaming_text +=
            "\n\nParagraph " + std::to_string(d)
            + " (#" + std::to_string(d * 7 + 3) + "): the renderer stage "
            + std::to_string(d) + " builds tree-" + std::to_string(d)
            + ", lays out region-" + std::to_string(d * 2)
            + ", diffs canvas-" + std::to_string(d * 3)
            + " and serializes runs batch-" + std::to_string(d * 5) + ".";
        tick();
        h.frame(m, "delta" + std::to_string(d));
        // 额外拒绝 -byte 动画 帧 三角洲之间——纯度
        // 逐步显示 运动超过已执行的前缀(生产) RAF 选择。
        tick();
        h.frame(m, "anim" + std::to_string(d));
        if (h.dead) return;
    }

    // (4) 解决:移动 streaming_text 文本, 解决 md 缓存 闲着,闲着,
    //    那样 WAIT 财务报告和审定财务报表 逐步显示 冷冻前完全排水
    //    ——确切地说就是生产门(第1款) meta.cpp 滴答冻结一次
    //    live_tail_reveal_settled (m) 真实的。
    //    逐步显示 排水沟捕获 a 源码树 与末世相悖者,
    //    逐步显示-animated) 帧 → maya 内容 -emits 整个转弯。
    {
        auto& b = m.d.current.messages.back();
        b.text += b.streaming_text;
        b.streaming_text.clear();
        agentty::app::detail::settle_message_md(m, b);
        m.s.phase = agentty::phase::Idle{};
        // 递延 -settle 油漆:勾选+ 渲染 直到 逐步
        // 显示 已经排尽了(捆绑起来,吊不起来,不能绕过测试)
        int guard = 0;
        do {
            tick();
            h.frame(m, "settle" + std::to_string(guard));
        } while (!agentty::app::detail::live_tail_reveal_settled(m)
                 && ++guard < 200 && !h.dead);
        agentty::app::detail::freeze_through(m, m.d.current.messages.size());
        // 几何学上的 Trim 运行 -consistent 现在已经很暗了( Name PTY
        // -backed 生产: commit_scrollback (N) 它的返回是大小从 相同
        // term_dims (a) 与《京都议定书》第三条第1款有关的其他事项 渲染器 被关起来
        auto trim = agentty::app::detail::trim_frozen_if_oversized(m);
        h.apply_trim(trim);
        h.frame(m, "freeze");
    }
    if (pty_master >= 0) close(pty_master);
}

// 理由(思考)设想——其原因(思考) HEADER 结算时的变动( 百分比)
// "✦ Thinking ⠻ · N tok" → "✦ Reasoned · N tokens"
// 以及其国家 加载动画 动画每 帧。漫长的推理会超越 可视区域 所以,
// 我们 头文件 + 早期思维行滚动 COMMIT 改为本地 回滚缓冲区
// while live; at settle the block must not rewrite any committed row.
static void run_reasoning_scenario(int width, int term_h) {
    setenv("LINES", std::to_string(term_h).c_str(), 1);
    setenv("COLUMNS", std::to_string(width).c_str(), 1);
    const int pty_master = install_pty_stdout(width, term_h);

    Model m;
    // 大小达到 状态更新函数 并透过 Model ,作为 TerminalResized
    // 将会送来
    m.ui.term_cols = width;
    m.ui.term_rows = term_h;
    m.d.current.id = agentty::ThreadId{"reason"};
    m.d.available_models.push_back({});
    m.d.available_models.back().id = agentty::ModelId{"claude-opus-4-1"};
    m.d.show_reasoning = true;
    agentty::app::detail::clear_frozen(m);

    Harness h(width, term_h);
    h.frame(m, "rwelcome");

    {
        Message u; u.role = Role::User;
        u.text = "Think hard, then answer: uniq-R-Q.";
        m.d.current.messages.push_back(std::move(u));
        agentty::app::detail::freeze_through(m, m.d.current.messages.size());
        h.frame(m, "rsubmit");
    }

    // 助理进入 PURE-REASONING 阶段( 尚未有答案文本)。
    Message a; a.role = Role::Assistant;
    m.d.current.messages.push_back(std::move(a));
    m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};
    for (int f = 0; f < 6; ++f) { tick(); h.frame(m, "ropen" + std::to_string(f)); }

    // 数据流 长长的推理 正文 溢出时 可视区域 所以,
    // 我们 头文件 + 早期思想从顶部滚动并致力于 回滚缓冲区.
    auto& back = m.d.current.messages.back();

    // CLIPPING ORACLE (离我们远点) R1-R6 重复
    // 预告 : R 检查证明没有行是永远的 REWRITTEN 但他们
    // 对一排绝对不会发射的物体视而不见——原子弹。
    // "reasoning hides rows" 症状(固定) -height
    // 设置设置。 直接守护它: 60 个思维标记中的每一个 必须 字面上显示于
    // 终端 记录(内部) 回滚缓冲区 ++ 打开 -screen 在一些 帧.
    // 高帽会 渲染 只能是跟踪窗口 所以中间的标记永远无法到达 终端
    // 财务报告和已审计财务报表 `seen[d]` 就会一直假的
    std::vector<bool> seen(60, false);
    auto scan_terminal = [&]() {
        const auto tr = h.emu.transcript();
        for (int d = 0; d < 60; ++d) {
            if (seen[d]) continue;
            const std::string marker =
                "Thought " + std::to_string(d)
                + " (#" + std::to_string(d * 7 + 3) + ")";
            for (const auto& row : tr)
                if (row.find(marker) != std::string::npos) { seen[d] = true; break; }
        }
    };

    for (int d = 0; d < 60; ++d) {
        back.thinking +=
            "\n\nThought " + std::to_string(d)
            + " (#" + std::to_string(d * 7 + 3) + "): weigh option "
            + std::to_string(d) + " against constraint-" + std::to_string(d * 2)
            + " and revise plan-" + std::to_string(d * 3) + ".";
        tick(); h.frame(m, "rdelta" + std::to_string(d));
        tick(); h.frame(m, "ranim" + std::to_string(d));
        scan_terminal();
        if (h.dead) { if (pty_master >= 0) close(pty_master); return; }
    }

    // 平息:答案到达(flips except live_settled),
    // 然后闲置+等待逐步显示排水+冻结——生产门.
    {
        auto& b = m.d.current.messages.back();
        b.text = "Answer: uniq-R-A.";
        agentty::app::detail::settle_message_md(m, b);
        m.s.phase = agentty::phase::Idle{};
        int guard = 0;
        do {
            tick();
            h.frame(m, "rsettle" + std::to_string(guard));
            scan_terminal();
        } while (!agentty::app::detail::live_tail_reveal_settled(m)
                 && ++guard < 200 && !h.dead);
        agentty::app::detail::freeze_through(m, m.d.current.messages.size());
        auto trim = agentty::app::detail::trim_frozen_if_oversized(m);
        h.apply_trim(trim);
        h.frame(m, "rfreeze");
        scan_terminal();

        // 可能没有高帽隐藏思想行.
        int hidden = 0;
        for (int d = 0; d < 60; ++d) if (!seen[d]) ++hidden;
        CHK(hidden == 0,
            "reasoning CLIPPED: " + std::to_string(hidden)
            + " of 60 thought rows never reached the terminal (height cap hid"
            " them) [" + std::to_string(width) + "x"
            + std::to_string(term_h) + "]");
    }
    if (pty_master >= 0) close(pty_master);
}

// 多个 -sub-turn 情景——截图形状:文本 —— ACTIONS 面板
// – 继续文本 – ACTIONS 面板... 全部在内 一个 活( un)
// -frozen ) 助手运行。当工具子运行时 -turn 上一条短信的
// streaming_text 已解决为 `.text` (制作:
// settle_message_md )但信息( ) STAYS 现场直播
// 末尾 因为跑步不是 终端 (续作: 待处理 新鲜的 StreamingMarkdown
// 部件是用于继续的。 Reveal_fx 谓 ON 每一个部件。整个运行都从
// 可视区域 这么早的潜艇 -turns 溢出+承诺。如果有解决
// -but-still-live 上一个子 -turn 内容 -lays-out
// 或该定义 逐步显示 覆盖面板 使一排人心烦意乱, R1 火——可见的重复。
static ToolUse done_tool(const std::string& tag) {
    ToolUse t;
    auto now = maya::anim_now();
    t.id = agentty::ToolCallId{"read_" + tag};
    t.name = agentty::ToolName{"read"};
    std::string out;
    for (int i = 0; i < 8; ++i)
        out += std::to_string(i) + ": " + tag + " source line\n";
    t.status = ToolUse::Done{now - std::chrono::milliseconds{5}, now,
                             std::move(out)};
    return t;
}

// 工具还剩 RUNNING : 其牌号 渲染 编号 spinner/awaiting
// 正文 (~1行)——(一行)," ⁇ "," ⁇ , ⁇ ". SHORT。
// 当它后来的过渡 终端 来,来,来 SWAPS 全文 output/error 正文,
// GROWING 这张卡,那个中场 -turn 身高不断增长,活得更久 末尾 已经溢出 可视区域 ,
// 是准确的, 接口边界 电影的画面 腐败的存在。
static ToolUse running_tool(const std::string& tag) {
    ToolUse t;
    auto now = maya::anim_now();
    t.id   = agentty::ToolCallId{"bash_" + tag};
    t.name = agentty::ToolName{"shell"};
    t.args = nlohmann::json{{"command", "git commit -m " + tag}};
    t.status = ToolUse::Running{now - std::chrono::milliseconds{5}};
    return t;
}

// 将运行的工具转换为 FAILED 带多件 -line 错误
// 正文 直接通过真实的驱动 apply_tool_output
// 状态更新函数 生产高度 -grow + 冷却 -arm 路径
// 通过手进行) -built 状态在这里。

// 解决 WRITE 带有工具卡 LONG 正文 一次 终端 ,视图 渲染 来,
// 来,来 show_all =真(满) 正文 ) 中, 两者 现场
// 直播 末尾 和冻伤 快照 — a TALL 彩色卡片。 这是用户的形状。
// 回滚缓冲区 (先前为 `write random.txt` diff),
// 而a(一个) 新 转身就落到它上面 并困住一个重复的作曲家。
// `lines` 控制该 正文 高度让卡片溢出 可视区域.
static ToolUse settled_write_tool(const std::string& tag, int lines) {
    ToolUse t;
    auto now = maya::anim_now();
    t.id   = agentty::ToolCallId{"write_" + tag};
    t.name = agentty::ToolName{"write"};
    std::string content;
    for (int i = 0; i < lines; ++i)
        content += "line " + std::to_string(i) + ": " + tag
                 + " written body content that wraps a bit\n";
    t.args = nlohmann::json{{"path", "random_" + tag + ".txt"},
                            {"content", content}};
    std::string out = "wrote " + std::to_string(lines) + " lines to random_"
                    + tag + ".txt";
    t.status = ToolUse::Done{now - std::chrono::milliseconds{5}, now,
                             std::move(out)};
    return t;
}

// PRIOR-WRITE-CARD 情景——被报道的虫虫的确切形状.
// 包含一个 TALL 已结结的写卡被冻结到前缀( 它生活在
// 本地 回滚缓冲区 ,就像用户的早些时候
// `write random.txt`). THEN 转第二圈
// 数据流 一份答复,将溢出,而成交。 帧 画布包含( 完整)
// -body 将卡片写在冷冻前缀中; maya 内容
// -serializes 作曲家乐队再次被推入 回滚缓冲区 —
// R6 开火,运行中 TALL 终端 (所报几何.
static void run_prior_writecard_scenario(int width, int term_h) {
    setenv("LINES", std::to_string(term_h).c_str(), 1);
    setenv("COLUMNS", std::to_string(width).c_str(), 1);
    const int pty_master = install_pty_stdout(width, term_h);

    Model m;
    // 大小达到 状态更新函数 并透过 Model ,作为 TerminalResized
    // 将会送来
    m.ui.term_cols = width;
    m.ui.term_rows = term_h;
    m.d.current.id = agentty::ThreadId{"revealpw"};
    m.d.available_models.push_back({});
    m.d.available_models.back().id = agentty::ModelId{"claude-opus-4-1"};
    agentty::app::detail::clear_frozen(m);

    Harness h(width, term_h);

    // 转一: 用户询问, 助手写一个高高的文件。
    // 完全构建模型 首次 ,则 渲染 初始数据 帧
    // 已经到达了高度——这反映了复会(水合物)而不是
    // 欢迎——内容跳跃,所以第一次是 帧 生长
    // 不是和我们测试的固定生长相融合的
    {
        Message u; u.role = Role::User;
        u.text = "Create random.txt with some nonsense.";
        m.d.current.messages.push_back(std::move(u));

        Message a; a.role = Role::Assistant;
        a.text = "Done \xe2\x80\x94 created random.txt with a bit of nonsense.";
        a.tool_calls.push_back(settled_write_tool("r1", term_h + 20));
        m.d.current.messages.push_back(std::move(a));

        agentty::app::detail::settle_message_md(
            m, m.d.current.messages.back());
        agentty::app::detail::rehydrate_frozen(m);
        h.frame(m, "pw-turn1-rehydrated");
        auto trim = agentty::app::detail::trim_frozen_if_oversized(m);
        h.apply_trim(trim);
        h.frame(m, "pw-turn1-trim");
    }
    if (h.dead) { if (pty_master >= 0) close(pty_master); return; }

    // 转二:用户再次询问; 助手 数据流 的回复会溢出,
    // 然后解决。 这是困住作曲者的和解。
    {
        Message u; u.role = Role::User;
        u.text = "now edit it";
        m.d.current.messages.push_back(std::move(u));
        agentty::app::detail::freeze_through(m, m.d.current.messages.size());
        h.frame(m, "pw-turn2-submit");
    }

    Message a2; a2.role = Role::Assistant;
    a2.streaming_text = "Sure, here are three trivia facts, unrelated:";
    m.d.current.messages.push_back(std::move(a2));
    m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};
    for (int f = 0; f < 4; ++f) { tick(); h.frame(m, "pw-open" + std::to_string(f)); }

    auto& back = m.d.current.messages.back();
    for (int d = 0; d < 40 && !h.dead; ++d) {
        back.streaming_text +=
            "\n\nParagraph " + std::to_string(d)
            + ": builds tree-" + std::to_string(d)
            + ", diffs canvas-" + std::to_string(d)
            + ", serializes runs batch-" + std::to_string(d) + ".";
        tick(); h.frame(m, "pw-d" + std::to_string(d));
        tick(); h.frame(m, "pw-anim" + std::to_string(d));
    }
    if (h.dead) { if (pty_master >= 0) close(pty_master); return; }

    // 解决2号转弯穿过生产门
    {
        auto& b = m.d.current.messages.back();
        b.text += b.streaming_text;
        b.streaming_text.clear();
        agentty::app::detail::settle_message_md(m, b);
        m.s.phase = agentty::phase::Idle{};
        int guard = 0;
        do {
            tick();
            h.frame(m, "pw-settle" + std::to_string(guard));
        } while (!agentty::app::detail::live_tail_reveal_settled(m)
                 && ++guard < 200 && !h.dead);
        agentty::app::detail::freeze_through(m, m.d.current.messages.size());
        auto trim = agentty::app::detail::trim_frozen_if_oversized(m);
        h.apply_trim(trim);
        h.frame(m, "pw-freeze");
        // 冰冻后几款闲置动画帧(settle_cooldown
        // 窗口)——作曲家继续重新刷漆;
        // 任何搁浅的复制品现在都可以看到R6.
        for (int f = 0; f < 4 && !h.dead; ++f) { tick(); h.frame(m, "pw-idle" + std::to_string(f)); }
    }
    if (pty_master >= 0) close(pty_master);
}

static void run_multiturn_scenario(int width, int term_h) {
    setenv("LINES", std::to_string(term_h).c_str(), 1);
    setenv("COLUMNS", std::to_string(width).c_str(), 1);
    const int pty_master = install_pty_stdout(width, term_h);

    Model m;
    // 大小达到 状态更新函数 并透过 Model ,作为 TerminalResized
    // 将会送来
    m.ui.term_cols = width;
    m.ui.term_rows = term_h;
    m.d.current.id = agentty::ThreadId{"revealmt"};
    m.d.available_models.push_back({});
    m.d.available_models.back().id = agentty::ModelId{"claude-opus-4-1"};
    agentty::app::detail::clear_frozen(m);

    Harness h(width, term_h);
    h.frame(m, "mt-welcome");

    // 冻结用户转动,所以直播运行上方有承诺的前缀.
    {
        Message u; u.role = Role::User;
        u.text = "Investigate the rendering pipeline end to end.";
        m.d.current.messages.push_back(std::move(u));
        agentty::app::detail::freeze_through(m, m.d.current.messages.size());
        h.frame(m, "mt-submit");
    }

    m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};

    // 几个文本 + 工具 + 继续 子 -turns 内 一个 活跑 跑起来
    for (int turn = 0; turn < 6 && !h.dead; ++turn) {
        const std::string st = std::to_string(turn);

        // 新助理子 -turn 消息( I) : 数据流 传言入其中.
        Message a; a.role = Role::Assistant;
        a.streaming_text = "Let me check diagram source " + st + ":";
        m.d.current.messages.push_back(std::move(a));
        for (int f = 0; f < 4; ++f) { tick(); h.frame(m, "mt" + st + "-open" + std::to_string(f)); }

        // 长一点散文(跨第-turns分流积累).
        auto& back = m.d.current.messages.back();
        for (int d = 0; d < 4; ++d) {
            back.streaming_text +=
                "\n\nStep " + st + "." + std::to_string(d)
                + " (read-" + std::to_string(turn * 10 + d)
                + "): tracing call path-" + std::to_string(turn * 4 + d)
                + " through layout-" + std::to_string(turn)
                + " and serialize-" + std::to_string(d) + ".";
            tick(); h.frame(m, "mt" + st + "-d" + std::to_string(d));
        }

        // Tool 子目录 -turn 土地:生产将上一个文本
        // 放入 `.text` (它留在现场) 末尾 )并按下工具消息。
        {
            auto& b = m.d.current.messages.back();
            b.text += b.streaming_text;
            b.streaming_text.clear();
            agentty::app::detail::settle_message_md(m, b);
        }
        Message tmsg; tmsg.role = Role::Assistant;
        tmsg.tool_calls.push_back(done_tool("mt" + st));
        m.d.current.messages.push_back(std::move(tmsg));
        for (int f = 0; f < 3; ++f) { tick(); h.frame(m, "mt" + st + "-tool" + std::to_string(f)); }
    }

    // 最终结算+冻结.
    {
        auto& b = m.d.current.messages.back();
        if (!b.streaming_text.empty()) {
            b.text += b.streaming_text;
            b.streaming_text.clear();
            agentty::app::detail::settle_message_md(m, b);
        }
        m.s.phase = agentty::phase::Idle{};
        for (int f = 0; f < 12; ++f) { tick(); h.frame(m, "mt-settle" + std::to_string(f)); }
        agentty::app::detail::freeze_through(m, m.d.current.messages.size());
        auto trim = agentty::app::detail::trim_frozen_if_oversized(m);
        h.apply_trim(trim);
        h.frame(m, "mt-freeze");
    }
    if (pty_master >= 0) close(pty_master);
}

// 提交 -mid-reveal 情景-镜像 modal.cpp '提交路径,
// 强制 -freezes 前一个助手转弯( 转弯) ( settle_message_md
// + 冻结_ 通) 用户点击的瞬间 Enter, EVEN IF 编号 逐步
// 显示 还在中 -glide (pending_settle_freeze
// 如果冻起来 快照 抓获一个活 -overlay 源码树 与接下来的事物不同
// 帧 颜料,前转弯 正文 能够降落在 两者 冷冻前缀和重置 -render
// 重复 我们 数据流 a 正文 溢出,然后归顺者, 无需 任何 逐步显示
// -drain 等一等,还有 渲染. R1/R4 找到任何重复的线索。
static void run_submit_mid_reveal_scenario(int width, int term_h) {
    setenv("LINES", std::to_string(term_h).c_str(), 1);
    setenv("COLUMNS", std::to_string(width).c_str(), 1);
    const int pty_master = install_pty_stdout(width, term_h);

    Model m;
    // 大小达到 状态更新函数 并透过 Model ,作为 TerminalResized
    // 将会送来
    m.ui.term_cols = width;
    m.ui.term_rows = term_h;
    m.d.current.id = agentty::ThreadId{"revealsub"};
    m.d.available_models.push_back({});
    m.d.available_models.back().id = agentty::ModelId{"claude-opus-4-1"};
    agentty::app::detail::clear_frozen(m);

    Harness h(width, term_h);
    h.frame(m, "sub-welcome");

    for (int turn = 0; turn < 4 && !h.dead; ++turn) {
        const std::string st = std::to_string(turn);

        // 用户提交( 首转: 只需推; 后转: 这是中转)
        // -reveal 提交部队 -freezes )),前助理转弯.
        Message u; u.role = Role::User;
        u.text = "Question " + st + ": explain a subsystem.";
        m.d.current.messages.push_back(std::move(u));
        // 镜像 modal.cpp 提交: 解决前助理转而
        // 冻结通过, 无需 等待着 逐步显示 排水沟
        for (std::size_t i = m.ui.frozen_through;
             i + 1 < m.d.current.messages.size(); ++i) {
            auto& mm = m.d.current.messages[i];
            if (mm.role != Role::Assistant || mm.text.empty()) continue;
            agentty::app::detail::settle_message_md(m, mm);
        }
        agentty::app::detail::freeze_through(m, m.d.current.messages.size());
        m.ui.pending_settle_freeze = false;
        h.frame(m, "sub" + st + "-submit");

        // 数据流 助理回复, 溢出 可视区域. 渲染 与 逐步显示
        // 三角洲之间的动画。 do 不 排水量 逐步显示 结尾处的
        // —— 下一个 循环的提交火中 -glide.
        Message a; a.role = Role::Assistant;
        a.streaming_text = "Diagram lookup " + st + " begins now:";
        m.d.current.messages.push_back(std::move(a));
        m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};
        auto& back = m.d.current.messages.back();
        for (int d = 0; d < 30; ++d) {
            back.streaming_text +=
                "\n\nPara " + st + "-" + std::to_string(d)
                + ": builds tree-" + std::to_string(turn * 100 + d)
                + ", lays region-" + std::to_string(turn * 100 + d)
                + ", diffs canvas-" + std::to_string(turn * 100 + d)
                + ", serializes runs batch-" + std::to_string(turn * 100 + d)
                + ".";
            tick(); h.frame(m, "sub" + st + "-d" + std::to_string(d));
            if (h.dead) return;
        }
        // 将文本调和为 . text( 所以下一个提交方的和解路径
        // 是 否) -op 形状 -wise )但离开 逐步显示
        // 可能还活着,做 不 冻结——下一份重复提交就是冻结。
        back.text += back.streaming_text;
        back.streaming_text.clear();
        // 故意的 故意的 不 呼叫 settle_message_md 这儿... 逐步显示 保持活,
        // 准确的说 pending_settle_freeze 状态 modal.cpp 卫兵.
        m.s.phase = agentty::phase::Idle{};
        m.ui.pending_settle_freeze = true;
        h.frame(m, "sub" + st + "-idle");
    }
    if (pty_master >= 0) close(pty_master);
}

// 代码 -block 情景——截图形状:
// 让我检查图表源码。
// 之后是A级 LONG 外溢的栅栏形码块(人鱼/图源) 可视区域
// 自动 -fold 谓 DISABLED ,所以块 渲染 输入
// 完整 每一个阶段——生活、定居和冻结高度都同意(无中段) -stream
// 归并 这仍然会强调 reveal/freeze 回滚缓冲区
// 超越已溢入原生内容的路径 回滚缓冲区 与 reveal_fx ON ;
// 断言证实没有冻结交割时的重复。
static void run_codeblock_fold_scenario(int width, int term_h) {
    setenv("LINES", std::to_string(term_h).c_str(), 1);
    setenv("COLUMNS", std::to_string(width).c_str(), 1);
    const int pty_master = install_pty_stdout(width, term_h);

    Model m;
    // 大小达到 状态更新函数 并透过 Model ,作为 TerminalResized
    // 将会送来
    m.ui.term_cols = width;
    m.ui.term_rows = term_h;
    m.d.current.id = agentty::ThreadId{"revealcb"};
    m.d.available_models.push_back({});
    m.d.available_models.back().id = agentty::ModelId{"claude-opus-4-1"};
    agentty::app::detail::clear_frozen(m);

    Harness h(width, term_h);
    h.frame(m, "cb-welcome");

    {
        Message u; u.role = Role::User;
        u.text = "Show me the rendering pipeline diagram.";
        m.d.current.messages.push_back(std::move(u));
        agentty::app::detail::freeze_through(m, m.d.current.messages.size());
        h.frame(m, "cb-submit");
    }

    Message a; a.role = Role::Assistant;
    a.streaming_text = "Let me check the diagram source:";
    m.d.current.messages.push_back(std::move(a));
    m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};
    for (int f = 0; f < 4; ++f) { tick(); h.frame(m, "cb-open" + std::to_string(f)); }

    auto& back = m.d.current.messages.back();
    // 打开一个栅栏的代码块,然后 数据流 > 40行,所以会溢出
    // 和 横跨 归并 门槛,然后 CLOSE 围栏(承诺) 现场
    // 归并 改为~1行).
    back.streaming_text += "\n\n```mermaid";
    tick(); h.frame(m, "cb-fence-open");
    for (int d = 0; d < 70 && !h.dead; ++d) {
        back.streaming_text +=
            "\n  node" + std::to_string(d) + " --> builds tree-"
            + std::to_string(d) + "; diffs canvas-" + std::to_string(d)
            + "; serializes runs batch-" + std::to_string(d) + ";";
        tick(); h.frame(m, "cb-line" + std::to_string(d));
        tick(); h.frame(m, "cb-anim" + std::to_string(d));
    }
    if (h.dead) { if (pty_master >= 0) close(pty_master); return; }
    // 关闭围栏 封锁区 归并 活着
    back.streaming_text += "\n```\n\nThat is the full pipeline graph.";
    for (int f = 0; f < 6 && !h.dead; ++f) {
        tick(); h.frame(m, "cb-fold" + std::to_string(f));
    }
    if (h.dead) { if (pty_master >= 0) close(pty_master); return; }

    // 将+冻结通过生产门。
    {
        auto& b = m.d.current.messages.back();
        b.text += b.streaming_text;
        b.streaming_text.clear();
        agentty::app::detail::settle_message_md(m, b);
        m.s.phase = agentty::phase::Idle{};
        int guard = 0;
        do {
            tick();
            h.frame(m, "cb-settle" + std::to_string(guard));
        } while (!agentty::app::detail::live_tail_reveal_settled(m)
                 && ++guard < 200 && !h.dead);
        agentty::app::detail::freeze_through(m, m.d.current.messages.size());
        auto trim = agentty::app::detail::trim_frozen_if_oversized(m);
        h.apply_trim(trim);
        h.frame(m, "cb-freeze");
    }
    if (pty_master >= 0) close(pty_master);
}

// TOOL-CARD-GROW-AT-SETTLE 情景——精确的
// 截图形状。现场助理运行时会溢出 可视区域 和击打工具时(
// `git commit` ) 仍然在 RUNNING (短数 加载动画。
// 然后工具会 FAILS ,交换 加载动画 用于多 -line
// 错误 正文 : 牌号 GROWS 中点 -turn ,将每一行移到上面,
// 包括已经溢出的前缀,现在承诺要用土语来形容 回滚缓冲区。
// 如果 maya 变换的构件 帧 一次没有了 -up 调节 帧 (钟在
// 工具上) 继续 接口边界 ),旧牌的顶端排成一行 回滚缓冲区 =
// = 数据 "card cut off one screen up"
// 损坏用户的盘旋。 apply_tool_output 现在把
// 调节器拿起来 时间就一直不停地转动 agent_session
// 总是 -on 时钟; 绳子 渲染 那些冷却 帧 财务报告和已审计
// 财务报表 R1/R5/R6 校验没有搁浅的重复。
static void run_toolgrow_scenario(int width, int term_h) {
    setenv("LINES", std::to_string(term_h).c_str(), 1);
    setenv("COLUMNS", std::to_string(width).c_str(), 1);
    const int pty_master = install_pty_stdout(width, term_h);

    Model m;
    // 大小达到 状态更新函数 并透过 Model ,作为 TerminalResized
    // 将会送来
    m.ui.term_cols = width;
    m.ui.term_rows = term_h;
    m.d.current.id = agentty::ThreadId{"revealtg"};
    m.d.available_models.push_back({});
    m.d.available_models.back().id = agentty::ModelId{"claude-opus-4-1"};
    agentty::app::detail::clear_frozen(m);

    Harness h(width, term_h);
    h.frame(m, "tg-welcome");

    // 冻用户转:在直播运行上方承诺前缀.
    {
        Message u; u.role = Role::User;
        u.text = "push all and run the tests";
        m.d.current.messages.push_back(std::move(u));
        agentty::app::detail::freeze_through(m, m.d.current.messages.size());
        h.frame(m, "tg-submit");
    }

    m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};

    // 助理 数据流 传言,流传无穷 可视区域 ,
    // 然后发布一个仍然 RUNNING (短卡).
    Message a; a.role = Role::Assistant;
    a.streaming_text = "I'll push both repos and run the suite:";
    m.d.current.messages.push_back(std::move(a));
    for (int f = 0; f < 4; ++f) { tick(); h.frame(m, "tg-open" + std::to_string(f)); }

    {
        auto& back = m.d.current.messages.back();
        for (int d = 0; d < 30 && !h.dead; ++d) {
            back.streaming_text +=
                "\n\nStep " + std::to_string(d)
                + ": builds tree-" + std::to_string(d)
                + ", diffs canvas-" + std::to_string(d)
                + ", serializes runs batch-" + std::to_string(d) + ".";
            tick(); h.frame(m, "tg-d" + std::to_string(d));
            tick(); h.frame(m, "tg-anim" + std::to_string(d));
        }
    }
    if (h.dead) { if (pty_master >= 0) close(pty_master); return; }

    // 将散文调和为.text(它保持活性——工具续作) 待处理 )并推一个
    // RUNNING shash 工具子 -turn (短数 加载动画 ),卡片,(卡片).
    {
        auto& b = m.d.current.messages.back();
        b.text += b.streaming_text;
        b.streaming_text.clear();
        agentty::app::detail::settle_message_md(m, b);
    }
    Message tmsg; tmsg.role = Role::Assistant;
    tmsg.tool_calls.push_back(running_tool("tg"));
    m.d.current.messages.push_back(std::move(tmsg));
    m.s.phase = agentty::phase::ExecutingTool{agentty::phase::Active{}};
    for (int f = 0; f < 5; ++f) { tick(); h.frame(m, "tg-running" + std::to_string(f)); }
    if (h.dead) { if (pty_master >= 0) close(pty_master); return; }

    // 该 SEAM : 工具 FAILS. apply_tool_output
    // 换掉 加载动画 用于多 -line 错误 正文 -
    // 这张卡 GROWS 生产武器,调节冷却在这里;
    // 模型通过 渲染 冷却 帧 (钟不停地滴答) 所以
    // maya 调和转换后的溢出前缀,而不是编组一次并铺设它。
    {
        const auto id = m.d.current.messages.back().tool_calls.front().id;
        agentty::app::detail::apply_tool_output(
            m, id,
            std::unexpected(agentty::tools::ToolError::subprocess(
                "[subprocess failed] git_commit (add) failed (exit 128): "
                "fatal: pathspec did not match any files")));

        // apply_tool_output武装
        // settle_cooldown_ticks.
        // 渲染正是冷却的帧(meta.cpp每滴1)——maya调节
        // 所生长卡的窗口,没有固定时钟在ONEXX帧和转动前缀后失效.
        int guard = 0;
        while (m.ui.settle_cooldown_ticks > 0 && guard++ < 40 && !h.dead) {
            --m.ui.settle_cooldown_ticks;   // 镜像 meta.cpp 试题
            tick();
            h.frame(m, "tg-reconcile" + std::to_string(guard));
        }
        // 转弯就到Idle; 闲着的几个 帧 证实已成长的
        // 卡片和作曲家是稳定的,单人 -copy.
        m.s.phase = agentty::phase::Idle{};
        agentty::app::detail::freeze_through(m, m.d.current.messages.size());
        auto trim = agentty::app::detail::trim_frozen_if_oversized(m);
        h.apply_trim(trim);
        h.frame(m, "tg-freeze");
        for (int f = 0; f < 4 && !h.dead; ++f) {
            tick(); h.frame(m, "tg-idle" + std::to_string(f));
        }
    }
    if (pty_master >= 0) close(pty_master);
}

// TRIM-STORM 假设情况—— FRONT-DELETION 修补的路径。
// 每一个其他情况都超过 可视区域 但保留被冻结的前缀 UNDER
// frozen_row_budget (),所以, trim_frozen_if_oversized
// 永不从条目中丢弃 FRONT - 它回来了 none() 编辑 渲染-time
// 门处理底部 -shrink 倒塌,这留下了修剪 commit_scrollback
// (N) 会计完全不 -exercised 结束 -to-end.
//
// 跑过来 MANY 完全转弯,每个高到 冰冻的前缀吹过
// frozen_row_budget (a) (最大(48,
// term_h*3 一旦完成,修饰就会从前面掉下最古老的条目——
// a TOP - 删除 maya 无法调和。 渲染 时间——
// 和问题 commit_scrollback (N) 如果N 低于
// -counts 实际掉落的行数(旧的8192) -width ),
// 已删除条目的物理行线条, 以本地语言 回滚缓冲区: R6
// 查看作曲者/ 前一个转动 复制了一个屏幕。 如果 N 精确( 固定) ,
// 边界一次并 R1/R5/R6 每修剪一次都要保持干净
static void run_trimstorm_scenario(int width, int term_h) {
    setenv("LINES", std::to_string(term_h).c_str(), 1);
    setenv("COLUMNS", std::to_string(width).c_str(), 1);
    const int pty_master = install_pty_stdout(width, term_h);

    Model m;
    // 大小达到 状态更新函数 并透过 Model ,作为 TerminalResized
    // 将会送来
    m.ui.term_cols = width;
    m.ui.term_rows = term_h;
    m.d.current.id = agentty::ThreadId{"revealts"};
    m.d.available_models.push_back({});
    m.d.available_models.back().id = agentty::ModelId{"claude-opus-4-1"};
    agentty::app::detail::clear_frozen(m);

    Harness h(width, term_h);
    h.frame(m, "ts-welcome");

    // 足够让累积的冷冻前缀超过 frozen_row_budget
    // () 多次重复,强迫重复的前线 -trims。
    // 每个回合:用户提交(冻结),助理 数据流 a 正文
    // 溢出时 可视区域 ,和解 + 冻结 + 修饰。
    const int kTurns = 14;
    for (int turn = 0; turn < kTurns && !h.dead; ++turn) {
        const std::string st = std::to_string(turn);

        {
            Message u; u.role = Role::User;
            u.text = "Turn " + st + ": walk the whole pipeline in detail please.";
            m.d.current.messages.push_back(std::move(u));
            agentty::app::detail::freeze_through(m, m.d.current.messages.size());
            h.frame(m, "ts" + st + "-submit");
        }

        Message a; a.role = Role::Assistant;
        a.streaming_text = "Beginning turn " + st + " investigation:";
        m.d.current.messages.push_back(std::move(a));
        m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};
        for (int f = 0; f < 3; ++f) { tick(); h.frame(m, "ts" + st + "-open" + std::to_string(f)); }

        auto& back = m.d.current.messages.back();
        // ~1.2 风景点 正文 每转弯,就连两个转弯都
        // 超过预算, 以后的转弯都会触发前锋 -trim.
        const int body_lines = term_h + term_h / 4 + 6;
        for (int d = 0; d < body_lines && !h.dead; ++d) {
            back.streaming_text +=
                "\n\nStep " + st + "." + std::to_string(d)
                + ": builds tree-" + std::to_string(turn * 1000 + d)
                + ", diffs canvas-" + std::to_string(turn * 1000 + d)
                + ", serializes runs batch-" + std::to_string(turn * 1000 + d) + ".";
            tick(); h.frame(m, "ts" + st + "-d" + std::to_string(d));
        }
        if (h.dead) break;

        // 解决+冻结+修剪通过生产门.
        auto& b = m.d.current.messages.back();
        b.text += b.streaming_text;
        b.streaming_text.clear();
        agentty::app::detail::settle_message_md(m, b);
        m.s.phase = agentty::phase::Idle{};
        int guard = 0;
        do {
            tick();
            h.frame(m, "ts" + st + "-settle" + std::to_string(guard));
        } while (!agentty::app::detail::live_tail_reveal_settled(m)
                 && ++guard < 200 && !h.dead);
        agentty::app::detail::freeze_through(m, m.d.current.messages.size());
        // 该 PATH UNDER TEST : 此修剪一旦前缀超过预算,
        // 就会减少前缀并返回 commit_scrollback(N).
        auto trim = agentty::app::detail::trim_frozen_if_oversized(m);
        h.apply_trim(trim);
        h.frame(m, "ts" + st + "-freeze");
        // 几处闲置 帧 因此,任何搁浅的复制都变得可见于 R6
        // 作曲家时 重绘 员额 -trim.
        for (int f = 0; f < 3 && !h.dead; ++f) { tick(); h.frame(m, "ts" + st + "-idle" + std::to_string(f)); }
    }
    if (pty_master >= 0) close(pty_master);
}

}  // 名称空间( 归并)

int main() {
    // 时间只经过 tick() ,所以一个 帧 序列在a上是一样的 已
    // 加载 CI 运行在闲置的盒子上。
    maya::testing::freeze_anim_clock(1'000'000);
    // 每一个假想dup2是 PTY 结束 STDOUT 给
    // term_dims (a) 实际几何; 保存 原样,所以摘要可见于后面。
    const int saved_stdout = dup(STDOUT_FILENO);
    std::fprintf(stderr, "reveal_scrollback_test\n");
    const struct { int w, th; } shapes[] = {
        {80, 24}, {60, 16}, {100, 20}, {72, 12},
    };
    // TALL 终端 ( > 24行) 练习 分钟( rows,
    // 24) 夹插 view.cpp : 版式为 BUILT 24岁
    // 以下 -row 顶点但是 RENDERED 写作的绝对数排在建筑中
    // 源码树 可能不同意它降落在什么地方 传输。被报道的bug是一个编曲
    // 被困在屏幕上, 在一个转弯后, 完全就解决了这样的问题。
    // 终端 — R6 抓住它,我们找到它了。
    const struct { int w, th; } tall_shapes[] = {
        {80, 40}, {100, 50}, {120, 32}, {90, 60},
    };
    for (auto s : shapes) {
        run_scenario(s.w, s.th);
        if (g_failures) break;
    }
    for (auto s : shapes) {
        if (g_failures) break;
        run_multiturn_scenario(s.w, s.th);
    }
    for (auto s : shapes) {
        if (g_failures) break;
        run_submit_mid_reveal_scenario(s.w, s.th);
    }
    for (auto s : shapes) {
        if (g_failures) break;
        run_codeblock_fold_scenario(s.w, s.th);
    }
    // 调 -run 每场比赛 TALL 终端 ——未经过考验的制度,
    // 其中: view.cpp 分钟( rows, 24) 构建 -vs-render 高夹咬。
    for (auto s : tall_shapes) {
        if (g_failures) break;
        run_scenario(s.w, s.th);
    }
    for (auto s : tall_shapes) {
        if (g_failures) break;
        run_multiturn_scenario(s.w, s.th);
    }
    for (auto s : tall_shapes) {
        if (g_failures) break;
        run_codeblock_fold_scenario(s.w, s.th);
    }
    // 报告的错误: a PRIOR 转动时,
    // 在冷冻前缀中高高的已结算写卡,然后在它上面再转一次。
    // R6 触发这里的平原 -prose 以上
    // 情况保持清白。
    for (auto s : tall_shapes) {
        if (g_failures) break;
        run_prior_writecard_scenario(s.w, s.th);
    }
    for (auto s : shapes) {
        if (g_failures) break;
        run_prior_writecard_scenario(s.w, s.th);
    }
    // 截图错误: 击打工具 FAILS 中点 -turn 活的,
    // 是活的, 末尾 已经溢出 可视区域 ——卡片因错误而增长.
    // 正文 ,移动已承诺的前缀。运行在正常和 TALL 形状。
    for (auto s : shapes) {
        if (g_failures) break;
        run_toolgrow_scenario(s.w, s.th);
    }
    for (auto s : tall_shapes) {
        if (g_failures) break;
        run_toolgrow_scenario(s.w, s.th);
    }
    // 电话 FRONT-DELETION 修剪路径:许多高转弯超过冻结的预算,
    // 并强迫真正的前线 -trims 其 人 名 commit_scrollback
    // (N)必须是 EXACT 下调 -commit 线条重复( R) R6 ),
    // 修补使N精确。 TALL 形状。
    for (auto s : shapes) {
        if (g_failures) break;
        run_trimstorm_scenario(s.w, s.th);
    }
    for (auto s : tall_shapes) {
        if (g_failures) break;
        run_trimstorm_scenario(s.w, s.th);
    }
    // 长 REASONING 溢出:  ⁇ Thinking/Reasoned
    // 头文件 + 加载动画 滚动 + 执行时, 活, 然后 头文件
    // 平时的翻转——不能重写任何承诺的行。小形状迫使溢出;
    // 高的形状锻炼不 -overflow 路径。
    for (auto s : shapes) {
        if (g_failures) break;
        run_reasoning_scenario(s.w, s.th);
    }
    for (auto s : tall_shapes) {
        if (g_failures) break;
        run_reasoning_scenario(s.w, s.th);
    }
    if (saved_stdout >= 0) { dup2(saved_stdout, STDOUT_FILENO); close(saved_stdout); }
    std::fprintf(stderr, "%d checks, %d failures\n", g_checks, g_failures);
    if (g_failures) { std::fprintf(stderr, "FAILED\n"); return 1; }
    std::fprintf(stderr, "PASSED\n");
    return 0;
}
