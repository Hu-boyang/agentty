// scrollback_oracle - 开车 真实 maya Runtime::render (app.cpp
// 内含路径)通过 a PTY 与 真实 agentty 视图 + freeze/trim + 工具 -card
// 寿命周期,并强制执行 APPEND-ONLY SCROLLBACK ORACLE:
//
//   时下 帧 N,模拟者已承诺 回滚缓冲区 必须是前缀
//   -extension 备注 回滚缓冲区 时间 帧 N-1.
//   任何已修改、重新排序或重复 -committed 行是
//   一种腐败——不需要列举情景。
//
// 结转的额外支票 scrollback_repro.cpp:
//   - 唯一传言标记("uniq-T-P")最多出现在
//     累计笔录( 回滚缓冲区 + 可视区域),
//   - 作曲家铬最多出现一次
//
// 假设范围超出原方案:
//   - 工具 -card 转弯 : ToolUse 待处理 - > 运行( 增长) progress_text,
//     活过) - > 完成 归并 中间 -stream ,与流言相交——确切的形状
//     "duplicated prose above tool cards" 报告。
//   - 一个助理消息里面有多个工具回合
//   - 交替传言 -only 工具 -heavy 转过身去
//     触发冷冻前方 -trim (commit_scrollback ) 反复出现.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <clocale>
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

#include <maya/screen.hpp>
#include <maya/core/anim_clock.hpp>
#include <maya/terminal/tmux.hpp>   // reset_cache_for_test + FAKE 探测器 接口边界

#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/view/view.hpp"

using agentty::Model;
using agentty::Message;
using agentty::Role;
using agentty::ToolUse;

namespace {  // 归并: TU-local (捆绑成 agentty_standalone_tests)

static FILE* err = nullptr;
static int g_failures = 0;
// 地面 -truth 次数 maya's 回滚缓冲区 门必须
// RECOVER (无) -Synced 整个运行。
// 这个神谕驱动一个真正的 运行时 a 项下 PTY 与 字节
// -faithful 附加 -only 转动脚本——正确的管道从不
// 经过大门,所以 不变量 即: 保留 ZERO。任意勾选表示a
// 帧 已执行 -row 突变类型 守卫不可能抓获(秒)
// -accountant ),这比单纯的视觉哑弹更难失败:这意味
// 着阴影本身与模拟者不相符合.
static unsigned long g_recoveries = 0;
// 子集数 g_recoveries 本来不是 其中一个 -time 空 -state
// 滴水滴水滴滴滴滴 i.e。真实的第二 -accountant 事件 一定是零点
static unsigned long g_bad_recoveries = 0;
// 线条所在 逐步显示 已到达 不可变 回滚缓冲区 (真实,
// 用户) -visible 腐败;报告 -only 直到 maya 土地。
static unsigned long g_scramble_garbage = 0;

// ── Minimal ANSI 终端 本地仿真器 回滚缓冲区 ──────────────
//
// 宽度模型 : 存储单元 已解码 UTF-8 CODEPOINTS ,
// 每个代码点都占据 wcwidth (3) 列——一个独立的宽度参考(
// libc的表格, 不 maya 说到这里 maya-side 宽度
// 错误显示为漂移在此, 而不是自在 -consistently 隐形。
// -ASCII 代码点 渲染 以 '?' 记录中(标记检查为 ASCII
// 但他们的柱脚印是忠实的。
struct TermEmu {
    int cols, rows;
    int cx = 0, cy = 0;
    std::vector<std::string> screen;
    std::vector<std::string> scrollback;
    bool decawm = true;

    TermEmu(int c, int r) : cols(c), rows(r) {
        screen.assign((std::size_t)rows, std::string((std::size_t)cols, ' '));
    }
    std::string& line(int y) { return screen[(std::size_t)y]; }

    // 设置一个显示宽度的解码码符( 1 或 2)。
    void put_cp(char32_t cp, int w) {
        if (w <= 0) return;                    // 组合 / 0-width: 跳过
        if (cx + w > cols) {
            if (decawm) { cx = 0; cursor_newline(); }
            else        { cx = cols - w; if (cx < 0) cx = 0; }
        }
        if (cy < 0) cy = 0;
        if (cy >= rows) cy = rows - 1;
        std::string& l = line(cy);
        if ((int)l.size() < cols) l.resize((std::size_t)cols, ' ');
        const char vis = (cp < 128) ? (char)cp : '?';
        l[(std::size_t)cx] = vis;
        for (int k = 1; k < w && cx + k < cols; ++k)
            l[(std::size_t)(cx + k)] = (cp < 128) ? ' ' : '?';
        cx += w;
    }
    void cursor_newline() {
        if (cy < rows - 1) { ++cy; return; }
        std::string top = screen.front();
        while (!top.empty() && top.back() == ' ') top.pop_back();
        scrollback.push_back(std::move(top));
        screen.erase(screen.begin());
        screen.push_back(std::string((std::size_t)cols, ' '));
    }
    void carriage_return() { cx = 0; }
    void cursor_up(int n)   { cy -= n; if (cy < 0) cy = 0; }
    void cursor_down(int n) { for (int i = 0; i < n; ++i) cursor_newline(); }
    void cursor_fwd(int n)  { cx += n; if (cx > cols) cx = cols; }
    void erase_to_eol() {
        std::string& l = line(cy);
        if ((int)l.size() < cols) l.resize((std::size_t)cols, ' ');
        for (int x = cx; x < cols; ++x) l[(std::size_t)x] = ' ';
    }
    void erase_line_from_start() {
        std::string& l = line(cy);
        if ((int)l.size() < cols) l.resize((std::size_t)cols, ' ');
        for (int x = 0; x <= cx && x < cols; ++x) l[(std::size_t)x] = ' ';
    }
    void erase_line_all() {
        line(cy).assign((std::size_t)cols, ' ');
    }
    void erase_to_eos() {
        erase_to_eol();
        for (int y = cy + 1; y < rows; ++y)
            line(y).assign((std::size_t)cols, ' ');
    }
    bool saw_3j = false;   // 帧-scoped: CSI 3J 观察
    bool saw_2j = false;   // 帧-scoped: CSI 2J 观察
    std::string carry;   // 上一个种子的 ESC/UTF-8 序列的后继不完整

    void feed(const std::string& chunk) {
        // 读取的pty可以分割一个逃生序列或一个 UTF -8个代码点,
        // 横跨两个读数(无限pty) 缓冲区 + maya's
        // write_or_buffer 稍后冲掉剩下的 帧。 将藏品放在前
        // 并重新 -stash 任何不完整的 末尾 而不是把它印成文字。
        std::string b = std::move(carry);
        carry.clear();
        b += chunk;
        std::size_t i = 0, n = b.size();
        while (i < n) {
            unsigned char ch = (unsigned char)b[i];
            if (ch == '\r') { carriage_return(); ++i; continue; }
            if (ch == '\n') { cursor_newline(); ++i; continue; }
            if (ch == 0x1b) {
                std::size_t adv = parse_esc(b, i);
                if (adv == kIncomplete) { carry = b.substr(i); return; }
                i = adv; continue;
            }
            if (ch < 0x20) { ++i; continue; }
            // 解码为UTF-8编码点.
            char32_t cp = 0; int len = 1;
            if      (ch < 0x80)      { cp = ch; len = 1; }
            else if ((ch >> 5) == 0x6)  len = 2;
            else if ((ch >> 4) == 0xe)  len = 3;
            else if ((ch >> 3) == 0x1e) len = 4;
            else { ++i; continue; }               // 偏差续作字节
            if (len > 1) {
                if (i + (std::size_t)len > n) { carry = b.substr(i); return; }
                cp = (char32_t)(ch & (0xff >> (len + 1)));
                for (int k = 1; k < len; ++k)
                    cp = (cp << 6) | (char32_t)((unsigned char)b[i+(std::size_t)k] & 0x3f);
            }
            i += (std::size_t)len;
            int w = (cp < 128) ? 1 : wcwidth((wchar_t)cp);
            if (w < 0) w = 1;                     // 未知: 假设 1 和 多数模拟器类似
            put_cp(cp, w);
        }
    }
    static constexpr std::size_t kIncomplete = (std::size_t)-1;
    std::size_t parse_esc(const std::string& b, std::size_t i) {
        const std::size_t n = b.size();
        if (i + 1 >= n) return kIncomplete;
        if (b[i + 1] == ']') {           // OSC - 跳过到 BEL 或 或 时 间 ST
            std::size_t j = i + 2;
            while (j < n && b[j] != '\x07'
                   && !(b[j] == '\x1b' && j + 1 < n && b[j + 1] == '\\')) ++j;
            if (j < n && b[j] == '\x07') return j + 1;
            if (j + 1 < n) return j + 2;
            return kIncomplete;          // OSC 仍然打开在 缓冲区 结束
        }
        if (b[i + 1] != '[') return i + 2;
        std::size_t j = i + 2;
        bool priv = false;
        if (j < n && b[j] == '?') { priv = true; ++j; }
        int params[4] = {0, 0, 0, 0};
        bool have[4] = {false, false, false, false};
        int pi = 0;
        while (j < n && ((b[j] >= '0' && b[j] <= '9') || b[j] == ';')) {
            if (b[j] == ';') { if (pi < 3) ++pi; ++j; continue; }
            params[pi] = params[pi] * 10 + (b[j] - '0');
            have[pi] = true; ++j;
        }
        if (j >= n) return kIncomplete;   // CSI 仍然打开在 缓冲区 结束
        char fin = b[j];
        const int p = have[0] ? params[0] : 0;
        switch (fin) {
            case 'A': cursor_up(p ? p : 1); break;
            case 'B': cursor_down(p ? p : 1); break;
            case 'C': cursor_fwd(p ? p : 1); break;
            case 'D': cx -= (p ? p : 1); if (cx < 0) cx = 0; break;
            case 'G': cx = (p ? p - 1 : 0); if (cx < 0) cx = 0; break;
            case 'H': case 'f': {
                // CUP 行; 缩写(1) -based ; 缺少参数默认为 1。
                int r = have[0] && params[0] ? params[0] : 1;
                int c = have[1] && params[1] ? params[1] : 1;
                cy = r - 1; if (cy < 0) cy = 0; if (cy >= rows) cy = rows - 1;
                cx = c - 1; if (cx < 0) cx = 0; if (cx > cols) cx = cols;
                break;
            }
            case 'K':
                if (p == 1)      erase_line_from_start();
                else if (p == 2) erase_line_all();
                else             erase_to_eol();
                break;
            case 'J':
                if (p == 2 || p == 3) {
                    if (p == 3) { scrollback.clear(); saw_3j = true; }
                    saw_2j = true;
                    for (auto& l : screen) l.assign((std::size_t)cols, ' ');
                } else {
                    erase_to_eos();
                }
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

static std::string read_all(int fd) {
    std::string s;
    char buf[16384];
    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n > 0) { s.append(buf, (std::size_t)n); continue; }
        break;
    }
    return s;
}

// 推进 ANIMATION 时钟( 小时) maya::anim_now_ms
// - 每次唯一的来源 -driven 部件为: 逐步显示 光标,
// 摇摆窗口, 最终斜坡, anim::Clock /动) 无
// 需 睡觉,部件计算得没错 ages/phases/deadlines
// 拍摄时, 拍摄时会用零墙价来进行相同的动画动态,
// 运行时 输入 sleep_for.
static void tick(int ms = 20) {
    maya::testing::advance_anim_clock_ms(ms);
}

// ── 甲骨文状态( 单形状) ────────────────────────────────────────────
struct Oracle {
    std::vector<std::string> committed;   // 回滚缓冲区 截止到最后 帧
    std::vector<std::string> committed_at; // 每行输入的 帧 标签
    int frames_checked = 0;
};

static void dump_transcript(const TermEmu& emu) {
    auto all = emu.transcript();
    while (!all.empty() && all.back().empty()) all.pop_back();
    std::fprintf(err, "  --- transcript (%zu sb + %d view) ---\n",
                 emu.scrollback.size(), emu.rows);
    for (int y = 0; y < (int)all.size(); ++y)
        std::fprintf(err, "  %4d|%s%s\n", y,
            y < (int)emu.scrollback.size() ? "" : "V ",
            all[(std::size_t)y].c_str());
}

// 附录-only 甲骨文。 失败时返回真值。
static bool check_oracle(Oracle& orc, const TermEmu& emu,
                         const std::string& tag) {
    const auto& sb = emu.scrollback;
    ++orc.frames_checked;
    bool failed = false;

    if (sb.size() < orc.committed.size()) {
        ++g_failures; failed = true;
        std::fprintf(err,
            "  FAIL[%s][oracle]: scrollback SHRANK %zu -> %zu "
            "(committed rows vanished)\n",
            tag.c_str(), orc.committed.size(), sb.size());
    } else {
        for (std::size_t i = 0; i < orc.committed.size(); ++i) {
            if (sb[i] != orc.committed[i]) {
                ++g_failures; failed = true;
                std::fprintf(err,
                    "  FAIL[%s][oracle]: committed row %zu REWRITTEN\n"
                    "    was: '%s'  (committed at frame %s)\n"
                    "    now: '%s'\n",
                    tag.c_str(), i, orc.committed[i].c_str(),
                    i < orc.committed_at.size() ? orc.committed_at[i].c_str() : "?",
                    sb[i].c_str());
                break;
            }
        }
    }
    for (std::size_t i = orc.committed.size(); i < sb.size(); ++i)
        orc.committed_at.push_back(tag);
    orc.committed_at.resize(sb.size(), tag);
    orc.committed = sb;
    return failed;
}

// 标记 -uniqueness + 作曲家( R) -chrome 核对累积记录
static bool check_transcript(TermEmu& emu, const std::string& tag,
                             bool dump_on_fail) {
    auto all = emu.transcript();
    while (!all.empty() && all.back().empty()) all.pop_back();

    bool failed = false;
    std::vector<std::pair<std::string,int>> seen;
    for (int y = 0; y < (int)all.size(); ++y) {
        const std::string& ln = all[(std::size_t)y];
        auto pos = ln.find("uniq-");
        if (pos == std::string::npos) continue;
        std::size_t e = pos;
        while (e < ln.size() && ln[e] != ' ' && ln[e] != '.') ++e;
        std::string tok = ln.substr(pos, e - pos);
        // 重复=腐败 仅 当 LATER 复制本坐落于 不可变
        // 本地语言 回滚缓冲区 (y < 回滚缓冲区 排行数。
        // 可视区域 逐行重新刷漆 帧 ,并带有 逐步显示 键盘
        // 间距 全部 文本( 光标上输入门), 中间 -type
        // 直列合法显示标记 PREFIX 给几个人 帧
        // — "Paragraph uniq-1-1" 输入时
        // "uniq-1-10" ——哪个前缀与一个真正的早期
        // 标记相撞。这是动画,而不是一条线。真正的线条(截图
        // 类)总是与 两者 已承诺副本: 过期 副本已经写在sb,
        // settle/freeze/trim 同一弯的
        // 右下方有现场副本,因此,这一检查仍然对每一个真正的
        // 腐败都产生着火,因为转弯的闲置。 帧 最迟
        const bool later_copy_committed = y < (int)emu.scrollback.size();
        if (later_copy_committed) {
            for (auto& [s, py] : seen) {
                if (s == tok) {
                    ++g_failures;
                    failed = true;
                    std::fprintf(err,
                        "  FAIL[%s]: marker %s duplicated at transcript rows %d and %d "
                        "(scrollback=%zu)\n",
                        tag.c_str(), tok.c_str(), py, y, emu.scrollback.size());
                    break;
                }
            }
        }
        if (failed) break;
        seen.emplace_back(std::move(tok), y);
    }

    int composer_hits = 0;
    for (const auto& ln : all)
        if (ln.find("type a message") != std::string::npos
            || ln.find("type to queue") != std::string::npos)
            ++composer_hits;
    if (composer_hits > 1) {
        ++g_failures;
        failed = true;
        std::fprintf(err, "  FAIL[%s]: composer chrome appears %d times in transcript\n",
                     tag.c_str(), composer_hits);
    }

    // ── SCRAMBLE GARBAGE 已承付 回滚缓冲区 ────────────────────────
    // 电话 逐步显示 ⁇   ⁇ 副作用 随机绘画 ASCII/box
    // 刚开始的格言 -typed 密码点,在 LIVE 可视区域 (
    // 他们解决了 帧 如果一行人承诺 不可变 本地语言 回滚缓冲区
    // 其拼接器尚未解决, 垃圾是永久的 —— 用户 -visible
    // "corrupted scrollback" 错误。
    // 重复 -marker 上面检查不能看见它(行没有重复,它是被擦伤的),
    // 门本身 -heal 历史上被解职为 "benign style_id" 差,
    // 所以这个班 默默地运走。
    //
    // 结构上检测:这种牵引数据流是FIXED的传言句,
    // 所以任何包含其稳定前缀但NOT的稳定后缀的行都已
    // 被损坏为-word(e.g)中位.
    //   "...exactly like real assistan8??/%8" 改为
    //   行为与真实助手正文完全一致。
    // 仅 回滚缓冲区 检查行数—— a 可视区域 直列显示中点
    // -scramble 末尾 时 逐步显示 仍然在动画中。
    {
        // 绳子的固定句子: "...exactly like real assistant prose".
        // 承诺的一行可以合法 END 中点 -sentence
        // (字包),所以单是前缀匹配就不是腐败。
        // 腐败就是当行继续过前缀时,有东西会 ISN
        // 真正的延续... i.e 垃圾堆在字里乱排
        static constexpr std::string_view kStem = "exactly like real assis";
        for (std::size_t y = 0; y < emu.scrollback.size(); ++y) {
            const std::string& ln = emu.scrollback[y];
            const auto p = ln.find(kStem);
            if (p == std::string::npos) continue;
            // 沿着这行的干线的文字。
            const std::string tail = ln.substr(p + kStem.size());
            // 合法性的可能性: 包在某个地方的行 inside/after
            // "tant prose" -所以我们... 末尾 一定是个 PREFIX 备注
            // "tant prose" (可能还有已经修剪过的后遗症空间).
            static constexpr std::string_view kRest = "tant prose";
            const bool legit = kRest.compare(0, tail.size(), tail) == 0
                            || tail.rfind(kRest, 0) == 0;   // 末尾 开始于其余部分
            if (!legit) {
                // HARD-FAIL。这是 真实 用户 -visible 点火时有错误 :
                // 编号 逐步显示 被关进监狱里 不可变 回滚缓冲区.
                // 根本原因就是建筑设计。 reveal_fx's 覆盖面板
                // 无法看到可视区或滚动位置。
                // (reveal_fx.cpp :1221),所以连现场的都一样.
                // BOTTOM 划行(唯一一个它相信是安全的)变成一个卷轴 -off 下
                // 行时 帧 ,冻结其未解决的拼音格言 回滚缓冲区.
                //
                // FIXED 输入 maya 途经 TextRevealParams::scramble_glyph_safe:
                // 摇摆着闪烁 STYLING 但总是发出 真实 格莱弗,
                // 所以每个 字节 能够到达 回滚缓冲区 已经是最终的。
                // 这个探测器已报告 -only ~ 难
                // -fail 固定之日,如果它再次燃烧,
                // -safe 保证退步。
                ++g_scramble_garbage;
                std::fprintf(err,
                    "  FAIL[%s]: SCRAMBLE GARBAGE committed to scrollback row %zu:\n"
                    "           '%s'\n"
                    "           (expected '...assistant prose' or a clean wrap; a\n"
                    "            reveal scramble frame reached immutable scrollback\n"
                    "            — scramble_glyph_safe regressed, see\n"
                    "            reveal_fx.cpp render_live_panel_ / text_reveal.hpp)\n",
                    tag.c_str(), y, ln.c_str());
                ++g_failures; failed = true;
                break;
            }
        }
    }

    if (failed && dump_on_fail) dump_transcript(emu);
    return failed;
}

// ── 转动脚本 ────────────────────────────────────────────────────────
//
// 两个转弯形状,交替:
//   品行转折——数据流高标下(原复写形).
//   工具转动—— 数据流 短的传言铅 -in ,然后运行 kRounds
//                 工具卡 待处理 - > 运行( 进度增长) - > 完成
//                 ( ) 归并 ),每个字后面都有更多的散文。 状态更新函数
//                 其变异( 4) with_live_tool-style
//                 : 变异直播 末尾 到位,没有其他东西。

struct Ctx {
    // Screen::impl() — maya::detail::Runtime
    // 成为 detail::Device。在主机拆分中(见下文驱动程序旁的注释)。
    // DEVICE 直接用于内行会计和 commit_inline_prefix.
    maya::detail::Device* rt;
    TermEmu*   emu;
    Oracle*    orc;
    int        master;
    Model*     m;
    bool       done = false;

    bool frame(const std::string& tag) {
        emu->saw_3j = emu->saw_2j = false;
        const std::size_t sb_before = emu->scrollback.size();
        const int rows_before = rt->inline_content_rows();
        static const bool trace = std::getenv("ORACLE_TRACE") != nullptr;
        // 快照 被害者 回滚缓冲区 末尾 之前 这个 渲染 如果
        // 这个 帧 驱动器 maya 无 -Synced 我们
        // 可以打印出 准确的行 曾经和即将重写。
        std::vector<std::string> sb_snapshot = emu->scrollback;
        const unsigned long recov_before = rt->scrollback_recovery_count();
        maya::Element root = agentty::ui::view(*m);
        (void)rt->render(root);
        // 残余排水量 -- -- REQUIRED 打开 macOS ,其中,
        // PTY 缓冲区 ~1 时 KB 和任何 帧 长得比那大 字节
        // 生产循环排出在~8毫秒( has_pending_writes
        // 民意调查 render() 内容 -entry 如果神谕真的这样,
        // 那么,我们就会重新做人 不 照镜子 maya DEFERS 下一个
        // 模式 -bearing 帧 完全( 绝对数 ) render()
        // 早点 -outs 当残留物是 待处理 :欢迎 – 对话 撕裂
        // 滑出豁免 "t0-submit" 标记到稍后的 帧 (假回收)
        // FAIL 和半身撕裂的模范, -frame (假话)
        // "composer chrome twice" FAIL).
        // Linux's 64 KB PTY 缓冲区 永远不分开 帧 ,
        // 这就是为什么这 仅显示 macOS. maya 内部的一致性是 传输
        // -write-side 并且不受绳子的读数影响
        for (int k = 0; k < 256 && rt->has_pending_writes(); ++k) {
            emu->feed(read_all(master));
            (void)rt->render(root);
        }
        const unsigned long recov_after = rt->scrollback_recovery_count();
        const int rows_after = rt->inline_content_rows();
        if (recov_after != recov_before) {
            const unsigned long delta = recov_after - recov_before;
            g_recoveries += delta;
            // EXEMPTION ——第一位呈递者泪下满满.
            // -screen 空 -state SPLASH (
            // "a calm middleware" ,
            // 则 新-HERE 高高的作曲家,在一栏 终端 比它溅出
            // 的水还短 BLANK 引导行进入 回滚缓冲区 ;
            // 当喷出在 t0 时消失 -submit 那些被逼的行
            // 与对话的画面没有匹配,所以 maya 正确处理软案件
            // -B 恢复(承诺空白行+ 重绘。行是空白的,
            // 附加 -only 先知会保持绿色,而这个重塑恰好发生 一次
            // 生产中每期的拆卸,而不是第二次。 -accountant
            // 阶级——所以不认为是腐败。 OTHER 帧 一定要保持
            // 同步( delta == 0) , 这就不是变量 我们坚持
            const bool splash_teardown = (tag == "t0-submit");
            if (!splash_teardown) {
                // 非 国 家 -splash 恢复是指 maya
                // 门检测到阴影 -vs-viewport 意见不一,
                // SELF-HEALED 绝大多数是 逐步显示 鬼魂
                // 鬼魂 -band 隐藏位( Q) SGR 暗影隐藏着
                // 隐藏 style_id 画布上是底座 所以门被打碎了
                // -cell 缩写 标志 良性风格 -index
                // 差别和柔软 重绘 这不是 用户 -visible
                // 腐败——附录 -only [奇迹]和重复 -line
                // [图解]下面的检查是 地面真相,他们保持绿色。
                // 这么说吧 COUNT 恢复(遥测),但不 FAIL 上面有真经,
                // oracle/transcript 腐败没有经过检验。
                g_bad_recoveries += delta;
                std::fprintf(err,
                    "  [warn] %s: maya scrollback gate RECOVERED %lu time(s) "
                    "on this frame (rows %d -> %d) — benign self-heal (conceal-"
                    "band style_id), no oracle/transcript corruption\n",
                    tag.c_str(), delta, rows_before, rows_after);
            } else {
                std::fprintf(err,
                    "  [info] %s: %lu expected splash-teardown recovery (blank "
                    "overflow rows collapse; not a corruption)\n",
                    tag.c_str(), delta);
            }
        }
        if (trace)
            std::fprintf(err, "  [trace %s] rows %d -> %d sb %zu\n",
                         tag.c_str(), rows_before, rows_after,
                         emu->scrollback.size());
        if (const char* want = std::getenv("ORACLE_DUMP_AT");
            want && tag == want) {
            std::fprintf(err, "  [dump %s] viewport:\n", tag.c_str());
            for (std::size_t i = 0; i < emu->screen.size(); ++i)
                std::fprintf(err, "    vp%3zu|%s\n", i,
                             emu->screen[i].c_str());
            const std::size_t lo = emu->scrollback.size() > 12
                                 ? emu->scrollback.size() - 12 : 0;
            for (std::size_t i = lo; i < emu->scrollback.size(); ++i)
                std::fprintf(err, "    sb%4zu|%s\n", i,
                             emu->scrollback[i].c_str());
        }
        if (rows_before != 0 && rows_after == 0) {
            std::fprintf(err, "  [dbg] frame %s: maya entered non-Synced (rows %d -> 0)\n",
                         tag.c_str(), rows_before);
            const std::size_t lo = sb_snapshot.size() > 40 ? sb_snapshot.size() - 40 : 0;
            std::fprintf(err, "  [dbg] committed rows [%zu..%zu) BEFORE this render:\n",
                         lo, sb_snapshot.size());
            for (std::size_t i = lo; i < sb_snapshot.size(); ++i)
                std::fprintf(err, "    sb%4zu|%s\n", i, sb_snapshot[i].c_str());
        }
        emu->feed(read_all(master));
        if (emu->saw_3j || emu->saw_2j)
            std::fprintf(err, "  [frame %s] %s%s sb %zu -> %zu\n",
                         tag.c_str(),
                         emu->saw_2j ? "2J " : "", emu->saw_3j ? "3J" : "",
                         sb_before, emu->scrollback.size());
        bool f = check_oracle(*orc, *emu, tag);
        if (f) dump_transcript(*emu);
        f |= check_transcript(*emu, tag, /*dump_on_fail=*/true);
        return f;
    }
};

// 数据流 `paras` 将段落标记为下移到现场助理消息中。
// `wrapping=true` 每一段足够长,
// 以80个栏目包装3-4行的文字。 WRAPPED 第一次
// 重复的段落 -word 中 额 -reveal 快照 ,
// 所以包装+ 逐步显示 交互是一个需要的轴。
// `burst>1` 渲染只每一次burd-th突变——模型
// 在加载(visual_hash门/慢终端)下跳过帧的真应用,
// 其中在ONE的几块三角洲土地使帧.
static bool stream_paras(Ctx& cx, Message& msg, const std::string& st,
                         int first, int paras, const char* tag_prefix,
                         bool wrapping = false, int burst = 1) {
    int since_render = 0;
    for (int d = first; d < first + paras; ++d) {
        msg.streaming_text +=
            "\n\nParagraph uniq-" + st + "-" + std::to_string(d)
            + " covers stage " + std::to_string(d)
            + " of the pipeline in turn " + st + ".";
        if (wrapping)
            msg.streaming_text +=
                " It elaborates at considerable length about the provider"
                " objects and whichever translation unit defines the missing"
                " symbol so the paragraph wraps across several terminal rows"
                " exactly like real assistant prose does in the field.";
        if (++since_render >= burst) {
            since_render = 0;
            tick(); if (cx.frame(std::string(tag_prefix) + "-d" + std::to_string(d))) return true;
            tick(); if (cx.frame(std::string(tag_prefix) + "-a" + std::to_string(d))) return true;
        }
    }
    if (since_render) {
        tick(); if (cx.frame(std::string(tag_prefix) + "-flush")) return true;
    }
    return false;
}

// 通过制作解析当前助理消息 逐步显示 门,然后将 + 修整精确地冻结为
// meta.cpp 选中值 / execute_cmd 做点什么?
static bool settle_freeze_trim(Ctx& cx, const std::string& st) {
    Model& m = *cx.m;
    // 镜像生产( 4 000) meta.cpp 计时) 恰好:
    //   1. finalize_turn 移动 streaming_text 案文和武器 逐步显示
    //      最终坡道( pending_settle_freeze ),
    //      但确实如此 不 finish() 部件——打字机不断滑向直播边缘.
    //   2. 其后的票证 渲染 排水量 逐步显示 ; 冻结火灾 仅
    //      一次 live_tail_reveal_settled ()是真实的(Widget flipped
    //      live_off on its own),所以最后一个是 LIVE 帧 maya 已缓存 IS
    //      定着的形状和 settle_message_md()'s finish() 是一个 字节
    //      -idempotent 无 -op.
    // 将settle_message_md(旧的吊带虫)调到前面,
    // 迫使第finish()XXXXBEFORE号立即排出第逐步显示号——将一排承诺的
    // 排成排成排,并将第maya号开到第HardReset号,在结算第帧号。
    for (std::size_t i = m.ui.frozen_through;
         i < m.d.current.messages.size(); ++i) {
        auto& b = m.d.current.messages[i];
        if (b.role != Role::Assistant) continue;
        if (!b.streaming_text.empty()) {
            b.text += b.streaming_text;
            b.streaming_text.clear();
        }
    }
    m.s.phase = agentty::phase::Idle{};
    int guard = 0;
    do {
        tick();
        if (cx.frame("t" + st + "-settle" + std::to_string(guard))) return true;
    } while (!agentty::app::detail::live_tail_reveal_settled(m)
             && ++guard < 200);

    // 逐步显示 排水量 -- -- 当前 确定每个信件的标记( Q) 字节-idempotent
    // 和Tick一样冷冻 live_tail_reveal_settled
    // 分行会的。
    for (std::size_t i = m.ui.frozen_through;
         i < m.d.current.messages.size(); ++i) {
        auto& b = m.d.current.messages[i];
        if (b.role != Role::Assistant || b.text.empty()) continue;
        agentty::app::detail::settle_message_md(m, b);
    }
    agentty::app::detail::freeze_through(m, m.d.current.messages.size());
    std::fprintf(err, "  [dbg] t%s pre-trim: maya_rows=%d frozen_row_total=%zu frozen_entries=%zu\n",
                 st.c_str(), cx.rt->inline_content_rows(),
                 (std::size_t)m.ui.frozen.row_total(), m.ui.frozen.size());
    auto trim = agentty::app::detail::trim_frozen_if_oversized(m);
    if (const auto* c = std::get_if<maya::CommitScrollback>(&trim.inner)) {
        std::fprintf(err, "  [info] turn %s: trim commit_scrollback(%d) -> frozen_row_total=%zu\n",
                     st.c_str(), c->debt.rows(), (std::size_t)m.ui.frozen.row_total());
        cx.rt->commit_inline_prefix(c->debt.rows());
        // NOTE : 前缀执行后 maya '阴影在转移;神谕在承诺'
        // 快照 仍然有效( 承诺发出0) 字节).
    }
    if (cx.frame("t" + st + "-freeze")) return true;
    for (int f = 0; f < 6; ++f) {
        tick();
        if (cx.frame("t" + st + "-idle" + std::to_string(f))) return true;
    }
    return false;
}

// 一种转动(原转动形状)的转动(prose-only).
static bool prose_turn(Ctx& cx, int t, int H) {
    Model& m = *cx.m;
    const std::string st = std::to_string(t);

    { // 提交
        Message u; u.role = Role::User;
        u.text = "turn " + st + ": please explain everything again";
        m.d.current.messages.push_back(std::move(u));
        agentty::app::detail::freeze_through(m, m.d.current.messages.size());
        m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};
        if (cx.frame("t" + st + "-submit")) return true;
    }

    Message a; a.role = Role::Assistant;
    a.streaming_text = "Turn " + st + " opening: uniq-" + st + "-open.";
    m.d.current.messages.push_back(std::move(a));
    for (int f = 0; f < 3; ++f) { tick(); if (cx.frame("t" + st + "-o" + std::to_string(f))) return true; }

    if (stream_paras(cx, m.d.current.messages.back(), st, 0, 2 * H / 3, ("t" + st).c_str()))
        return true;

    return settle_freeze_trim(cx, st);
}

// 工具-card转弯:转弯: 转弯:通过 -in,
// 然后转弯:待处理 - > 运行(进度增加) - > 完成,在回合之间转弯。
// 故意敌对:
//   - 够了 WRAPPING 活人第一张牌前的传言 末尾
//     已经溢出 可视区域 当卡片开始增长时,
//   - 进展不断增长 MANY 线条(长条工作形状,30) KB
//     外地报告中的产出,
//   - 爆破了 - 爆裂了 - Burning! 帧 ——突变超过已变数 帧.
static bool tool_turn(Ctx& cx, int t, int H) {
    Model& m = *cx.m;
    const std::string st = std::to_string(t);

    { // 提交 — 镜像 submit_message: 冻结前转弯, 然后修剪
        Message u; u.role = Role::User;
        u.text = "turn " + st + ": run the checks and explain uniq-" + st + "-ask.";
        m.d.current.messages.push_back(std::move(u));
        agentty::app::detail::freeze_through(m, m.d.current.messages.size());
        auto trim = agentty::app::detail::trim_frozen_if_oversized(m);
        if (const auto* c = std::get_if<maya::CommitScrollback>(&trim.inner)) {
            std::fprintf(err, "  [info] turn %s: submit trim commit_scrollback(%d)\n",
                         st.c_str(), c->debt.rows());
            cx.rt->commit_inline_prefix(c->debt.rows());
        }
        m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};
        if (cx.frame("t" + st + "-submit")) return true;
    }

    Message a; a.role = Role::Assistant;
    a.streaming_text = "Turn " + st + " tool opening: uniq-" + st + "-open.";
    m.d.current.messages.push_back(std::move(a));
    for (int f = 0; f < 2; ++f) { tick(); if (cx.frame("t" + st + "-o" + std::to_string(f))) return true; }

    // 包装传言铅 -in : 足以溢出 可视区域 之前
    // 首张牌已经存在,所以以后的牌会生长,而后卡会生长
    // 末尾 高过 终端.
    const int lead = H / 4 + 3;
    if (stream_paras(cx, m.d.current.messages.back(), st, 0, lead,
                     ("t" + st + "-pre").c_str(), /*围网=*/true, /*爆破=( 以“ 破碎” 表示)*/2))
        return true;

    const int kRounds = 3;
    int para_next = lead;
    for (int r = 0; r < kRounds; ++r) {
        auto& msg = m.d.current.messages.back();
        const std::string rt_tag = "t" + st + "-r" + std::to_string(r);

        // 第二轮是 PARALLEL-TOOL 圆 : 两个 工具
        // 打开 一个 消息( 模式平行发送) tool_use
        // 块——一种真实和常见的 传输 形状)。 Tool A是快的;
        // 工具B的运行进度超过2×× 可视区域 , 承诺面板
        // TITLE 和A's(双关语) 事件-header
        // 排入本地 回滚缓冲区 时 两者 仍然活着的铬。
        // 相同 帧 B的进步——a GROW 帧 已实施 -row
        // 变异( 标题计数器) 0/2→1/2, A's 加载动画
        // 门的长臂 HardResets 错配: 破坏性擦拭类。
        if (r == 2) {
            for (int tno = 0; tno < 2; ++tno) {
                ToolUse tc;
                tc.id   = agentty::ToolCallId{"tool-" + st + "-" + std::to_string(r)
                                              + "-" + std::to_string(tno)};
                tc.name = agentty::ToolName{"Bash"};
                tc.args = nlohmann::json{
                    {"command", "make par-" + std::to_string(tno)},
                    {"display_description",
                     "uniq-" + st + "c" + std::to_string(r) + (tno ? "b" : "a") + ". go"}
                };
                tc.status = ToolUse::Pending{maya::anim_now()};
                msg.tool_calls.push_back(std::move(tc));
            }
            m.s.phase = agentty::phase::ExecutingTool{agentty::phase::Active{}};
            tick(); if (cx.frame(rt_tag + "-pending2")) return true;

            auto& ta = msg.tool_calls[msg.tool_calls.size() - 2];
            auto& tb = msg.tool_calls.back();
            ta.status = ToolUse::Running{maya::anim_now(), ""};
            tb.status = ToolUse::Running{maya::anim_now(), ""};
            tick(); if (cx.frame(rt_tag + "-run2both")) return true;

            // B 成长于 2x 可视区域 ——承诺标题+A's 头文件 划线
            const int kProg = 2 * H + 10;
            for (int p = 0; p < kProg; ++p) {
                auto& run = std::get<ToolUse::Running>(tb.status);
                run.progress_text +=
                    "[par " + std::to_string(p) + "/" + std::to_string(kProg)
                    + "] linking shard " + std::to_string(p) + "...\n";
                if (p % 3 == 2) {
                    tick(); if (cx.frame(rt_tag + "-bgrow" + std::to_string(p + 1))) return true;
                }
            }

            // 翻转完成 和 B会成长得更深 一个 帧 净额 GROW ,
            // 带有承诺的标题计数器+A的格子两个突变.
            {
                auto sa = std::get<ToolUse::Running>(ta.status).started_at;
                ta.status = ToolUse::Done{
                    sa, maya::anim_now(), "ok: A done"};
                auto& run = std::get<ToolUse::Running>(tb.status);
                for (int k = 0; k < 4; ++k)
                    run.progress_text += "[par tail " + std::to_string(k) + "]\n";
                tick(); if (cx.frame(rt_tag + "-aflip")) return true;
            }

            // B 已写;然后继续传言(新消息,下方面板).
            {
                auto sb = std::get<ToolUse::Running>(tb.status).started_at;
                tb.status = ToolUse::Done{
                    sb, maya::anim_now(), "ok: B done"};
                tick(); if (cx.frame(rt_tag + "-bdone")) return true;
            }
            {
                Message cont; cont.role = Role::Assistant;
                m.d.current.messages.push_back(std::move(cont));
            }
            m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};
            if (stream_paras(cx, m.d.current.messages.back(), st, para_next, 3,
                             (rt_tag + "-post").c_str(), /*围网=*/true, /*爆破=( 以“ 破碎” 表示)*/2))
                return true;
            para_next += 3;
            continue;
        }

        // Tool 显示 : 待处理 参数 流式传输 进来吧
        {
            ToolUse tc;
            tc.id   = agentty::ToolCallId{"tool-" + st + "-" + std::to_string(r)};
            tc.name = agentty::ToolName{"Bash"};
            tc.args = nlohmann::json{
                {"command", "make check-round-" + std::to_string(r)},
                // 保留独有的符号 SHORT 财务报告和已审计
                // 财务报表 EARLY :窄形(46个科)夹长卡片描述,
                // 带有椭圆形,可以将两个不同的标记切换到相同的前
                // 缀和假 -positive 独到性检查。
                {"display_description", "uniq-" + st + "c" + std::to_string(r) + ". go"}
            };
            tc.status = ToolUse::Pending{maya::anim_now()};
            m.s.phase = agentty::phase::ExecutingTool{agentty::phase::Active{}};
            msg.tool_calls.push_back(std::move(tc));
            tick(); if (cx.frame(rt_tag + "-pending")) return true;
        }

        // 运行中 : progress_text 长成一个 LOT
        // 连发的 长打卡工作 其卡不断增长 末尾 被冲出。
        // 第一轮是 CHROME-COMMIT 回合: 进展超过 2x。
        // 可视区域 所以,面板的, TITLE + 事件-header
        // 将行实际滚动为本地 回滚缓冲区 工具仍在运行时( W) 加载动画
        // 格丽芙,现场标题。任何稍后 字节 改为这些行(对数勾选、
        // 运行中、编曲中、 title_end 交换)是一种承诺 -row
        // 重写——用户的 "stacked ACTIONS fragments"
        // 截图类.
        {
            auto& tc = msg.tool_calls.back();
            tc.status = ToolUse::Running{maya::anim_now(), ""};
            tick(); if (cx.frame(rt_tag + "-run0")) return true;
            const int kProgress = (r == 1) ? (2 * H + 10) : (H / 2 + 8);
            for (int p = 0; p < kProgress; ++p) {
                auto& run = std::get<ToolUse::Running>(tc.status);
                run.progress_text +=
                    "[" + std::to_string(p) + "/" + std::to_string(kProgress)
                    + "] compiling translation unit " + std::to_string(p)
                    + " of round " + std::to_string(r) + "...\n";
                if (p % 3 == 2) {   // 爆破:每完成3个增长步骤 帧
                    tick(); if (cx.frame(rt_tag + "-run" + std::to_string(p + 1))) return true;
                }
            }
        }

        // 完成: 纸牌 归并 至其高处, SHRINK).
        // 第1轮:翻转完成 和 将续作的第一回传言 字节 之前
        // 下一个 渲染 - 一个 帧 带着两个被害者 -chrome
        // 变异( 变异) ( 加载动画 标题计数器、脚印)和a
        // GROW (下面是新品行)生产中,这是爆破 帧:
        // ToolExecOutput + 继续 数据流 第一次
        // 三角洲在两边 渲染。增长+运动是 HardReset
        // 臂 臂 手 臂 枪 maya 大门——破坏性恢复.
        {
            auto& tc = msg.tool_calls.back();
            auto started = std::get<ToolUse::Running>(tc.status).started_at;
            tc.status = ToolUse::Done{
                started, maya::anim_now(),
                "ok: 42 tests passed (round " + std::to_string(r) + ")"};
            if (r != 1) {
                tick(); if (cx.frame(rt_tag + "-done")) return true;
                tick(); if (cx.frame(rt_tag + "-fold")) return true;
            }
            // r == 1: NO 渲染 这儿,
            // Done翻转也一样 帧 继续传言如下。
        }

        // 更多( E) WRAPPING 后传语卡。 PRODUCTION
        // 这是 新 子目录 -turn: kick_pending_tools
        // 执行工具后推动新的助理占位符,并继续执行 数据流 文本
        // 三角洲登陆 该 留言。所以,帖子 -tool 传言 渲染
        // 中文本无 自身 文本位 下方 面板(在
        // turn_config_for_assistant_run
        // 文本(i) – panel(i) - text(i+1) 顺序,
        // 从未被缩合到前置 -tool 消息的( 给您)
        // streaming_text 将它添加到同一信件(旧的绳子错误)上,
        // 将标记下块增大 上方 和推板已经 -committed 标题
        // 行下行——一个关 -viewport 重写从未发生于 传输.
        {
            Message cont; cont.role = Role::Assistant;
            m.d.current.messages.push_back(std::move(cont));
        }
        m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};
        if (stream_paras(cx, m.d.current.messages.back(), st, para_next, 3,
                         (rt_tag + "-post").c_str(), /*围网=*/true, /*爆破=( 以“ 破碎” 表示)*/2))
            return true;
        para_next += 3;
    }

    return settle_freeze_trim(cx, st);
}

// A WRITE/EDIT 转弯:变异 -tool 与巴什弹不同的危险:a
// 流式传输 write/edit 纸牌 渲染 a SMALL 末尾-windowed
// 预览( E) show_all 骗人, code_tail ) 当它的
// ⁇ 在生长时,然后在 终端 翻转到 完整 正文 (show_all ——高大,
// 高大,无所适从. GROW 在一个 帧。编辑时另加切换 RENDER
// PATHS 结算时 : 流式传输 EditDiff-from- 栅栏
// 围栏 -parsed GitDiff 从工具输出中。两个切换必须纯底
// -appends w.r.t . . 承诺的行。
static bool write_edit_turn(Ctx& cx, int t, int H) {
    Model& m = *cx.m;
    const std::string st = std::to_string(t);

    { // 提交 — 镜像 submit_message: 冻结前转弯, 然后修剪
        Message u; u.role = Role::User;
        u.text = "turn " + st + ": write the file and fix uniq-" + st + "-ask.";
        m.d.current.messages.push_back(std::move(u));
        agentty::app::detail::freeze_through(m, m.d.current.messages.size());
        auto trim = agentty::app::detail::trim_frozen_if_oversized(m);
        if (const auto* c = std::get_if<maya::CommitScrollback>(&trim.inner)) {
            std::fprintf(err, "  [info] turn %s: submit trim commit_scrollback(%d)\n",
                         st.c_str(), c->debt.rows());
            cx.rt->commit_inline_prefix(c->debt.rows());
        }
        m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};
        if (cx.frame("t" + st + "-submit")) return true;
    }

    Message a; a.role = Role::Assistant;
    a.streaming_text = "Turn " + st + " write opening: uniq-" + st + "-open.";
    m.d.current.messages.push_back(std::move(a));
    for (int f = 0; f < 2; ++f) { tick(); if (cx.frame("t" + st + "-o" + std::to_string(f))) return true; }

    // 包装传言铅 -in 所以,我们 末尾 已经溢出 可视
    // 区域 在卡片存在之前——在卡片上方的行行被承诺时,
    // 每一次后来的卡片突变都会发生.
    const int lead = H / 4 + 3;
    if (stream_paras(cx, m.d.current.messages.back(), st, 0, lead,
                     ("t" + st + "-pre").c_str(), /*围网=*/true, /*爆破=( 以“ 破碎” 表示)*/2))
        return true;

    // WRITE：参数流式到达，内容增长到可视区约三倍，卡片
    //    待处理 带 末尾-window 预览; 然后运行; 然后完成 ~
    //    扩展卡片 末尾-window → 完整 正文 在一个 帧.
    {
        auto& msg = m.d.current.messages.back();
        ToolUse tc;
        tc.id   = agentty::ToolCallId{"write-" + st};
        tc.name = agentty::ToolName{"write"};
        tc.args = nlohmann::json{{"file_path", "src/gen_" + st + ".cpp"},
                                 {"content", ""}};
        tc.args_streaming = "{\"file_path\":\"src/gen_" + st + ".cpp\",\"content\":\"";
        tc.status = ToolUse::Pending{maya::anim_now()};
        msg.tool_calls.push_back(std::move(tc));
        tick(); if (cx.frame("t" + st + "-w-pending")) return true;

        // 数据流 内容: 状态更新函数 填充参数 [ "content" 部分
        // -JSON 解码器为 input_json 三角洲土地;
        // args_streaming 锁步生长( 它反馈 compute_render_key
        // 这么说吧 缓存 没办法 过期-blit 3×H线,5线的连线, 渲染 每2个
        const int kLines = 3 * H;
        std::string content;
        int since = 0;
        for (int ln = 0; ln < kLines; ++ln) {
            std::string line = "    auto v" + std::to_string(ln)
                + " = compute_" + st + "(" + std::to_string(ln) + ");\n";
            content += line;
            auto& wt = msg.tool_calls.back();
            wt.args_streaming += line;
            if (ln % 5 == 4) {
                wt.args["content"] = content;   // 解码器快照 cadence
                if (++since >= 2) {
                    since = 0;
                    tick(); if (cx.frame("t" + st + "-w-grow" + std::to_string(ln + 1))) return true;
                }
            }
        }
        {
            auto& wt = msg.tool_calls.back();
            wt.args["content"] = content;
            tick(); if (cx.frame("t" + st + "-w-args-done")) return true;

            // 执行: 运行( 简讯), 然后完成。 Done翻转切换 正文
            // 末尾-window → show_all 完整 内容(~3×H
            // 行)——大号 -frame GROW 此转弯可以检查。
            m.s.phase = agentty::phase::ExecutingTool{agentty::phase::Active{}};
            wt.status = ToolUse::Running{maya::anim_now(), ""};
            tick(); if (cx.frame("t" + st + "-w-run")) return true;
            auto started = std::get<ToolUse::Running>(wt.status).started_at;
            wt.status = ToolUse::Done{
                started, maya::anim_now(),
                "Wrote " + std::to_string(content.size()) + " bytes to src/gen_"
                    + st + ".cpp"};
            tick(); if (cx.frame("t" + st + "-w-done")) return true;
            tick(); if (cx.frame("t" + st + "-w-fold")) return true;
        }
    }

    // 继续传教 新 信件( 生成子 -turn 形状)。
    {
        Message cont; cont.role = Role::Assistant;
        m.d.current.messages.push_back(std::move(cont));
    }
    m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};
    if (stream_paras(cx, m.d.current.messages.back(), st, lead, 3,
                     ("t" + st + "-w-post").c_str(), /*围网=*/true, /*爆破=( 以“ 破碎” 表示)*/2))
        return true;

    // EDIT：差异块依次进入 args.edits，单块预览省略，
    //    完成后输出带 ```diff 围栏，随后
    //    已结算卡 -renders 穿过围栏 -parse GitDiff 路径在
    //    完整 (show_all), a 渲染-path 切换 + 高度在一中生长 帧.
    {
        auto& msg = m.d.current.messages.back();
        ToolUse tc;
        tc.id   = agentty::ToolCallId{"edit-" + st};
        tc.name = agentty::ToolName{"edit"};
        tc.args = nlohmann::json{{"path", "src/gen_" + st + ".cpp"},
                                 {"edits", nlohmann::json::array()}};
        tc.args_streaming = "{\"path\":\"src/gen_" + st + ".cpp\",\"edits\":[";
        tc.status = ToolUse::Pending{maya::anim_now()};
        msg.tool_calls.push_back(std::move(tc));
        m.s.phase = agentty::phase::ExecutingTool{agentty::phase::Active{}};
        tick(); if (cx.frame("t" + st + "-e-pending")) return true;

        // 5 差异块 ,每侧每条~6行,每条完成1条 帧.
        const int kHunks = 5;
        std::string diff_body;
        for (int hk = 0; hk < kHunks; ++hk) {
            std::string ot, nt;
            for (int l = 0; l < 6; ++l) {
                ot += "old_" + st + "_h" + std::to_string(hk) + "_l"
                    + std::to_string(l) + "();\n";
                nt += "new_" + st + "_h" + std::to_string(hk) + "_l"
                    + std::to_string(l) + "();\n";
            }
            auto& et = msg.tool_calls.back();
            et.args["edits"].push_back({{"old_text", ot}, {"new_text", nt}});
            et.args_streaming += ot + nt;   // 渲染 密钥的大小代理
            diff_body += "@@ -" + std::to_string(hk * 10 + 1) + ",6 +"
                       + std::to_string(hk * 10 + 1) + ",6 @@\n";
            for (int l = 0; l < 6; ++l)
                diff_body += "-old_" + st + "_h" + std::to_string(hk) + "_l"
                           + std::to_string(l) + "();\n";
            for (int l = 0; l < 6; ++l)
                diff_body += "+new_" + st + "_h" + std::to_string(hk) + "_l"
                           + std::to_string(l) + "();\n";
            tick(); if (cx.frame("t" + st + "-e-hunk" + std::to_string(hk))) return true;
        }

        // Done：输出携带 ```diff 围栏，进入稳定 GitDiff 路径。
        // 满 正文 打开翻转和续作的第一首 字节 输入 一个 帧 (
        // 生产破裂: ToolExecOutput + 第一次在两个之间
        // 继续三角洲 渲染 )——增长+. 正文-path 开关时的相同
        // 帧 是那个 HardReset 手臂,如果有任何东西在上方 接口
        // 边界 已经改变。
        {
            auto& et = msg.tool_calls.back();
            et.status = ToolUse::Running{maya::anim_now(), ""};
            tick(); if (cx.frame("t" + st + "-e-run")) return true;
            auto started = std::get<ToolUse::Running>(et.status).started_at;
            et.status = ToolUse::Done{
                started, maya::anim_now(),
                "Edited src/gen_" + st + ".cpp (" + std::to_string(kHunks)
                    + " edits):\n```diff\n" + diff_body + "\n```"};
            // NO 渲染 这儿,Done翻转也一样 帧
            // 继续传言如下。
        }
    }
    {
        Message cont; cont.role = Role::Assistant;
        m.d.current.messages.push_back(std::move(cont));
    }
    m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};
    if (stream_paras(cx, m.d.current.messages.back(), st, lead + 3, 3,
                     ("t" + st + "-e-post").c_str(), /*围网=*/true, /*爆破=( 以“ 破碎” 表示)*/2))
        return true;

    return settle_freeze_trim(cx, st);
}

// A DEEP 自动驾驶运行: 一个用户提交, 然后 MANY 子目录 -turns
// 陆地一号 -at-a-time 留下来 LIVE (无中位数) -run ) ,
// 直到整个运行结束。这是一个假设,即已解决 -prefix HOIST 优化——用户点击
// 的
// 运行中的工具卡片未显示，随后回复和先前卡片渲染错乱。
// 转向的两种危险,没有其他的转机可以做到:
//
//   • 升降机投入 仅 深度: 已结算子 -turns 累积
//     turn_config_for_assistant_run 崩溃
//     前的曲线 一个 隐藏的赤色 转动键于 a SUBRANGE 散开
//     ADVANCES 作为更多子 -turns。每次提前调整哪个子
//     -turns 位于缓存前缀内相对于 提前加载 末尾 内部
//     -live- 末尾 结构变化 maya 内置 diff 必须是纯附件吸收,
//     或重写承诺的牌行( 以 "prior card broke"
//     腐败问题)。
//
//   • 现场边缘保持 RUNNING 工具,其进展超过
//     可视区域 ,所以,定居的潜艇 -turns
//     上面是真正致力于本地人 回滚缓冲区 高架边界在
//     它们下面不断移动
static bool deep_run_turn(Ctx& cx, int t, int H) {
    Model& m = *cx.m;
    const std::string st = std::to_string(t);

    { // 提交
        Message u; u.role = Role::User;
        u.text = "turn " + st + ": do a long series of edits (uniq-" + st + "-ask).";
        m.d.current.messages.push_back(std::move(u));
        agentty::app::detail::freeze_through(m, m.d.current.messages.size());
        auto trim = agentty::app::detail::trim_frozen_if_oversized(m);
        if (const auto* c = std::get_if<maya::CommitScrollback>(&trim.inner))
            cx.rt->commit_inline_prefix(c->debt.rows());
        m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};
        if (cx.frame("t" + st + "-submit")) return true;
    }

    // 打开散文子 -turn (模型的, "I'll do these edits" ),该词的词句为:
    // 生产一个子 -turn's streaming_text 承诺 `text`
    // 财务报告和已审计财务报表 逐步显示 排水沟 之前 下一个子 -turn
    // 其下方的附加( 最终在子之间 -turns。镜像: 数据流 把它解决了
    // `text` 因此它成为一个稳定的承诺前缀——只有 TRUE 生命
    // 边缘( 最后一个消息) 永远不要离开 inline/animating.
    {
        Message a; a.role = Role::Assistant;
        a.streaming_text = "Turn " + st + " deep opening: uniq-" + st + "-open.";
        m.d.current.messages.push_back(std::move(a));
        for (int f = 0; f < 2; ++f) {
            tick(); if (cx.frame("t" + st + "-o" + std::to_string(f))) return true;
        }
        // 解决它: streaming_text ——文本,
        // 排水 逐步显示 (少数) 帧 ),与下一个子之前的定稿完全相同。 -turn.
        auto& op = m.d.current.messages.back();
        op.text = std::move(op.streaming_text);
        op.streaming_text.clear();
        for (int f = 0; f < 40
                 && !agentty::app::detail::live_tail_reveal_settled(m); ++f) {
            tick(); if (cx.frame("t" + st + "-o-settle" + std::to_string(f))) return true;
        }
    }

    // MANY 已解析的编辑子 -turns ,每个在它的 自身
    // 助理信息(制作子) -turn 深到可以与升起和溢出 可视区域
    // 已经很多次了 首次 半个 渲染 每个子 -turn (最多溢
    // 额) -commit 边框); 边框 第二 半个陆地潜水艇
    // -turns 输入 BURSTS 每架3辆 帧 —— 下层
    // -load 多个小组承诺的形状 回滚缓冲区 在一个diff。
    const int kEdits = 3 * H + 10;   // 过了高腿膝盖 + 可视区域
    for (int e = 0; e < kEdits; ++e) {
        const std::string et = st + "e" + std::to_string(e);
        Message a; a.role = Role::Assistant;
        ToolUse tc;
        tc.id   = agentty::ToolCallId{"dedit-" + et};
        tc.name = agentty::ToolName{"edit"};
        tc.args = nlohmann::json{
            {"path", "src/deep_" + et + ".cpp"},
            {"edits", nlohmann::json::array({{{"old_text", "a();\n"},
                                              {"new_text", "b();\n"}}})}};
        auto now = maya::anim_now();
        tc.status = ToolUse::Done{now - std::chrono::milliseconds{5}, now,
                                  "edited uniq-" + et + "-done"};
        a.tool_calls.push_back(std::move(tc));
        m.d.current.messages.push_back(std::move(a));
        // 上半部为: 渲染 每个子 -turn 下半场:3号爆炸。
        const int burst = (e < kEdits / 2) ? 1 : 3;
        if (e % burst == burst - 1) {
            tick(); if (cx.frame("t" + st + "-e" + std::to_string(e))) return true;
        }
    }
    tick(); if (cx.frame("t" + st + "-e-flush")) return true;

    // 电话 LIVE 边: 还有一个子 -turn 工具是: 工具是 :
    // 工具 仍然 RUNNING 不断进步 正文 溢出时 可视区域。
    // 已解决的子程序 -turns 承诺 回滚缓冲区 ; 运行中的卡必须 渲染
    // 和 每隔一段时间 帧 '承诺的前缀必须留下' 字节-stable.
    {
        Message a; a.role = Role::Assistant;
        ToolUse tc;
        tc.id   = agentty::ToolCallId{"drun-" + st};
        tc.name = agentty::ToolName{"Bash"};
        tc.args = nlohmann::json{
            {"command", "make deep-" + st},
            {"display_description", "uniq-" + st + "-run. go"}};
        tc.status = ToolUse::Running{maya::anim_now(), ""};
        a.tool_calls.push_back(std::move(tc));
        m.d.current.messages.push_back(std::move(a));
        m.s.phase = agentty::phase::ExecutingTool{agentty::phase::Active{}};
        tick(); if (cx.frame("t" + st + "-run0")) return true;

        const int kProgress = H + 12;
        auto& tcr = m.d.current.messages.back().tool_calls.back();
        for (int p = 0; p < kProgress; ++p) {
            auto& run = std::get<ToolUse::Running>(tcr.status);
            run.progress_text +=
                "[" + std::to_string(p) + "/" + std::to_string(kProgress)
                + "] deep step " + std::to_string(p) + " of turn " + st + "...\n";
            if (p % 3 == 2) {
                tick(); if (cx.frame("t" + st + "-run" + std::to_string(p + 1))) return true;
            }
        }

        // Tool 结束。
        auto started = std::get<ToolUse::Running>(tcr.status).started_at;
        tcr.status = ToolUse::Done{
            started, maya::anim_now(),
            "ok: deep run uniq-" + st + "-run-done"};
    }

    // 继续传教NEW消息(运行后回复).
    {
        Message cont; cont.role = Role::Assistant;
        m.d.current.messages.push_back(std::move(cont));
    }
    m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};
    if (stream_paras(cx, m.d.current.messages.back(), st, 0, 3,
                     ("t" + st + "-post").c_str(), /*围网=*/true, /*爆破=( 以“ 破碎” 表示)*/2))
        return true;

    return settle_freeze_trim(cx, st);
}

static int run_shape(int W, int H) {
    int master = -1, slave = -1;
    if (openpty(&master, &slave, nullptr, nullptr, nullptr) != 0) {
        std::fprintf(err, "openpty failed\n"); return 2;
    }
    struct winsize ws{};
    ws.ws_col = (unsigned short)W; ws.ws_row = (unsigned short)H;
    ioctl(slave, TIOCSWINSZ, &ws);
    dup2(slave, STDIN_FILENO);
    dup2(slave, STDOUT_FILENO);
    close(slave);
    fcntl(master, F_SETFL, fcntl(master, F_GETFL, 0) | O_NONBLOCK);

    std::fprintf(err, "=== shape %dx%d ===\n", W, H);

    // Host 拆分 : maya::detail::Runtime + RunConfig
    // 成为 maya::Screen + maya::Options。
    // 神谕驱动 DEVICE 直接( 内行会计, 回滚缓冲区 收复台,
    // commit_inline_prefix 因此它保持屏幕为 生命周期 并到达通过 impl() 给那些
    maya::Options cfg;
    cfg.mode = maya::Mode::Inline;
    auto scr_r = maya::Screen::open(cfg);
    if (!scr_r) { std::fprintf(err, "Screen::open failed\n"); return 2; }
    auto scr = std::move(*scr_r);
    auto& rt = scr.impl();

    TermEmu emu(W, H);
    emu.feed(read_all(master));

    Oracle orc;

    Model m;
    m.d.current.id = agentty::ThreadId{"oracle"};
    m.d.available_models.push_back({});
    m.d.available_models.back().id = agentty::ModelId{"claude-opus-4-1"};
    agentty::app::detail::clear_frozen(m);

    Ctx cx{&rt, &emu, &orc, master, &m};

    if (cx.frame("welcome")) goto done;

    // 旋转传言/击打 -tool / 写入+编辑器旋转; 足以触发两次修饰。
    // 写入+edit 旋转覆盖变异 -tool 纸牌 末尾
    // -window→show_all 和解扩展和 EditDiff
    // →GitDiff 渲染-path 开关,两行都坐到面板上方。
    {
        const int kTurns = 6;
        for (int t = 0; t < kTurns; ++t) {
            bool failed = (t % 3 == 0) ? prose_turn(cx, t, H)
                        : (t % 3 == 1) ? tool_turn(cx, t, H)
                                       : write_edit_turn(cx, t, H);
            if (failed) goto done;
        }
    }

    // A DEEP 自动驾驶运行于 a NON-EMPTY
    // 冻结前缀: 已结算前缀的升起在此进行( 许多
    // 子) -turns 运行边,然后结算和冻结。
    // 这是用户报告中断的情景;它运行 之后 旋转后将真正冷冻的
    // 前缀 + 修剪 回滚缓冲区 已经存在它下面。
    if (deep_run_turn(cx, 6, H)) goto done;
    // 跟着我走 -up 之后转身—— "text again after the run" 键,
    // 以显示前一次运行的牌已损坏。
    if (prose_turn(cx, 7, H)) goto done;

    std::fprintf(err, "  [info] frames oracle-checked: %d\n", orc.frames_checked);
    std::fprintf(err, "  [info] scrollback gate recoveries this shape: %lu\n",
                 rt.scrollback_recovery_count());

done:
    close(master);
    return 0;
}

}  // 名称空间( 归并)

int main() {
    // 调试 -built 利玛雅 ABORTS 任何一个 回滚缓冲区-gate 发射
    // (响亮的绊线, maya ——包括《公约》 KNOWN-BENIGN 分类
    // : 此甲骨文故意容忍作为遥测( t0 溅落的撕裂, 隐藏) -band
    // style_id 自己 -heals ; 见 recov_after 上方块)。
    // 告诉 maya 改为软 -recover 而不是,确切的说,
    // production/Release 执行; 先知的自附件 -only
    // [甲骨文]+ 重复 -line [标 检查仍然是真正腐败的真相。
    // 与scrollback_prefix_harness.cpp相同的先例.
    setenv("MAYA_NO_GATE_ABORT", "1", 1);
    // 中立 tmux. 自 a2b858f8 作曲家的硬件 -caret 门
    // 问 maya::tmux::active () 而不是读 $TMUX
    // 内置线条——和 active() 咨询一个 -shot 探测器 真实
    // tmux 服务器。 所以当运行此套件时 FROM 内部 tmux(
    // 非常普通的 dev 设置),作曲家会下注 -tmux 渲染 路径:
    // 它画了第二段, 定居的作曲乐队在一个 逐步显示 以模范者发誓,
    // "composer chrome twice". CI 和一无所有
    // -terminal 德夫·鲁斯从未见过,因为 $TMUX 甲骨文必须
    // 锻炼 一个 渲染 路径, 不管主机, 所以将 tmux 绑定 -free
    // 一条: 清空 $TMUX 和武力 maya 由 env( 从未产入
    // tmux) 进行回答的探测器, 设置了空特性。 然后重置缓存的探测器以
    // 获取覆盖 副作用 第一次选举之前 帧.
    unsetenv("TMUX");
    setenv("MAYA_TMUX_FAKE", "1", 1);
    unsetenv("MAYA_TMUX_FEATURES");
    unsetenv("MAYA_TMUX_PASSTHROUGH");
    maya::tmux::reset_cache_for_test();
    // 冻结动画时钟: tick() 然后按20毫秒的步数移动,
    // 实时不漏入。 CI 运行器在间距中添加了自己的
    // 毫秒 帧 编辑 逐步显示 走上了不同的路径( 1)
    // macOS 运行中报告了一个从未复制过的重复标记。
    maya::testing::freeze_anim_clock(1'000'000);
    setlocale(LC_ALL, "C.UTF-8");   // wcwidth 需要一个 UTF-8 个地块
    err = fdopen(dup(STDERR_FILENO), "w");
    const int shapes[][2] = {{80, 30}, {60, 18}, {100, 50}, {46, 76}};
    // ORACLE_SHAPE="60x18" 运行单个形状(调试迭代).
    const char* only = std::getenv("ORACLE_SHAPE");
    for (auto& s : shapes) {
        if (only) {
            const std::string want = std::to_string(s[0]) + "x" + std::to_string(s[1]);
            if (want != only) continue;
        }
        if (run_shape(s[0], s[1]) == 2) return 2;
    }
    if (g_failures == 0)
        std::fprintf(err, "PASS: append-only oracle + markers + chrome intact "
                          "(%lu benign gate self-heal/s, all shapes)%s\n",
                     g_bad_recoveries,
                     g_scramble_garbage
                         ? "  \xe2\x9a\xa0 SCRAMBLE GARBAGE IN SCROLLBACK"
                           " (scramble_glyph_safe regressed \xe2\x80\x94 see above)"
                         : "");
    else
        std::fprintf(err, "FAILED: %d corruption(s) detected (%lu benign gate "
                          "self-heal/s, %lu total incl. splash teardown)\n",
                     g_failures, g_bad_recoveries, g_recoveries);
    if (g_scramble_garbage)
        std::fprintf(err, "  [known-bug] scramble glyphs committed to immutable "
                          "scrollback on %lu row(s) \xe2\x80\x94 reveal_fx overlay is "
                          "viewport-blind (reveal_fx.cpp:1221)\n", g_scramble_garbage);
    return g_failures ? 1 : 0;
}
