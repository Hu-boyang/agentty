// test_ghost_caret_runtime.cpp — REPRO 现场 完整 RUNTIME 职等
// "ghost caret on line 2" 报告(检测+诊断功劳: davidwed,
// agentty PR #24):
//
//   将编曲行填充到包装边缘 → 第1 字节( 组合包装
//   到第2 个视觉行) → 后空格( 组合缩写回1 个视觉列)。
//   用户看到所绘的 光标 仍然在(已废除)第二排,
//   直到下一排 重绘.
//
// 驱动 真实 agentty view() + update() + 真实 maya
// Screen::present 以电子邮件方式发送 PTY 变成一个紧凑的
// VT 模拟器,然后检查 SCREEN 排: 收缩后, 光标 块必须回到上面的线上,
// 以及以前的 -caret-holding 排行必须空.
//
// 构建 : 链接 反 对 agentty 测试 库 (见A/C.5/61/L.48,第16段) CMake agentty_test()).
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <clocale>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif
#include <sys/ioctl.h>
#include <unistd.h>

#include <maya/screen.hpp>
#include <maya/core/anim_clock.hpp>
#include <maya/terminal/tmux.hpp>   // reset_cache_for_test 探测器是一 -shot

#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/msg.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/view/view.hpp"
#include "agentty/runtime/view/composer.hpp"

using agentty::Model;
using agentty::Msg;

// ── 压缩 VT 模拟器(仅显示屏幕+光标); 回滚缓冲区 不需要) (中文(简体) ). ──
struct VtEmu {
    int w, h;
    int cx = 0, cy = 0;
    std::vector<std::string> screen;
    bool decawm = true;
    bool cursor_hidden = false;   // DECTCEM (?25l/?25h)

    VtEmu(int c, int r)
        : w(c), h(r), screen(static_cast<std::size_t>(r),
                             std::string(static_cast<std::size_t>(c), ' ')) {}

    void newline() {
        ++cy;
        if (cy >= h) {
            screen.erase(screen.begin());
            screen.push_back(std::string(static_cast<std::size_t>(w), ' '));
            cy = h - 1;
        }
    }
    void put_cp(char32_t cp, int wd) {
        if (wd <= 0) return;
        if (cx + wd > w) {
            if (decawm) { cx = 0; newline(); }
            else cx = w - wd;
        }
        if (cy >= h) newline();
        char vis = (cp < 128) ? static_cast<char>(cp) : '?';
        // 我们只需要几何+ ASCII; 渲染 块格为 '#'.
        if (cp == 0x2588) vis = '#';
        else if (cp >= 0x2500 && cp <= 0x25FF) vis = '?';
        screen[static_cast<std::size_t>(cy)]
              [static_cast<std::size_t>(cx)] = vis;
        for (int k = 1; k < wd; ++k)
            screen[static_cast<std::size_t>(cy)]
                  [static_cast<std::size_t>(cx + k)] = '?';
        cx += wd;
    }

    void feed(const std::string& s) {
        std::size_t i = 0, n = s.size();
        std::string carry;
        while (i < n || !carry.empty()) {
            char32_t cp = 0; int len = 0;
            if (!carry.empty()) {
                carry += (i < n) ? std::string(1, s[i]) : std::string();
                if (i >= n && carry.size() < 4) break;   // 不完整的末尾
                // 从载运中解码
                unsigned char c0 = static_cast<unsigned char>(carry[0]);
                len = (c0 < 0x80) ? 1 : (c0 >> 5) == 0x6 ? 2 :
                      (c0 >> 4) == 0xE ? 3 : (c0 >> 3) == 0x1E ? 4 : 1;
                if (carry.size() < static_cast<std::size_t>(len)) {
                    if (i < n) { carry += s[i]; ++i; continue; }
                    break;
                }
                cp = (c0 < 0x80) ? c0 : (c0 & (0xff >> (len + 1)));
                for (int k = 1; k < len; ++k)
                    cp = (cp << 6) | (static_cast<unsigned char>(carry[static_cast<std::size_t>(k)]) & 0x3F);
                carry.clear();
                if (i < n) ++i;
            } else {
                unsigned char c0 = static_cast<unsigned char>(s[i]);
                if (c0 == 0x1B) {
                    // CSI 解析 国 务 院
                    if (i + 1 >= n) break;
                    if (s[i + 1] == '[') {
                        std::size_t j = i + 2;
                        std::string params;
                        while (j < n && ((s[j] >= '0' && s[j] <= '9')
                                      || s[j] == ';' || s[j] == '?')) {
                            params += s[j]; ++j;
                        }
                        // 中级 字节 (0x20-0x2F): e.g。该 SP 输入
                        // DECSCUSR `CSI 5 SP q` . . ..
                        // . 来 . 字节 土地在 `fin` ,而不是印刷为格言。
                        while (j < n && s[j] >= 0x20 && s[j] <= 0x2F) ++j;
                        if (j >= n) { carry.clear(); break; }  // 部分 CSI
                        char fin = s[j];
                        i = j + 1;
                        auto num = [&](int def) {
                            if (params.empty()) return def;
                            std::string f;
                            for (char p : params) { if (p == ';') break; if (p != '?') f += p; }
                            return f.empty() ? def : std::atoi(f.c_str());
                        };
                        auto num_at = [&](int idx, int def) {
                            int cur = 0; std::string f;
                            for (char p : params) {
                                if (p == ';') { if (cur == idx) break; ++cur; f.clear(); }
                                else if (p != '?') f += p;
                            }
                            return f.empty() ? def : std::atoi(f.c_str());
                        };
                        switch (fin) {
                            case 'A': cy -= num(1); if (cy < 0) cy = 0; break;
                            case 'B': cy += num(1); if (cy > h - 1) cy = h - 1; break;
                            case 'C': cx += num(1); if (cx > w - 1) cx = w - 1; break;
                            case 'D': cx -= num(1); if (cx < 0) cx = 0; break;
                            case 'H': case 'f': {
                                int r = num_at(0, 1), c2 = num_at(1, 1);
                                cy = r - 1; cx = c2 - 1;
                                if (cy < 0) cy = 0; if (cy > h - 1) cy = h - 1;
                                if (cx < 0) cx = 0; if (cx > w - 1) cx = w - 1;
                                break;
                            }
                            case 'K': {
                                int m = num(0);
                                auto& l = screen[static_cast<std::size_t>(cy)];
                                if (m == 0)      for (int x = cx; x < w; ++x) l[static_cast<std::size_t>(x)] = ' ';
                                else if (m == 1) for (int x = 0; x <= cx; ++x) l[static_cast<std::size_t>(x)] = ' ';
                                else             l.assign(static_cast<std::size_t>(w), ' ');
                                break;
                            }
                            case 'J': {
                                int m = num(0);
                                if (m == 0) {
                                    auto& l = screen[static_cast<std::size_t>(cy)];
                                    for (int x = cx; x < w; ++x) l[static_cast<std::size_t>(x)] = ' ';
                                    for (int y = cy + 1; y < h; ++y)
                                        screen[static_cast<std::size_t>(y)]
                                              .assign(static_cast<std::size_t>(w), ' ');
                                } else if (m == 1) {
                                    for (int y = 0; y < cy; ++y)
                                        screen[static_cast<std::size_t>(y)]
                                              .assign(static_cast<std::size_t>(w), ' ');
                                    auto& l = screen[static_cast<std::size_t>(cy)];
                                    for (int x = 0; x <= cx; ++x) l[static_cast<std::size_t>(x)] = ' ';
                                } else {
                                    for (auto& l : screen)
                                        l.assign(static_cast<std::size_t>(w), ' ');
                                }
                                break;
                            }
                            case 'h':
                                if (params.find("?7") != std::string::npos) decawm = true;
                                if (params.find("?25") != std::string::npos) cursor_hidden = false;
                                break;
                            case 'l':
                                if (params.find("?7") != std::string::npos) decawm = false;
                                if (params.find("?25") != std::string::npos) cursor_hidden = true;
                                break;
                            case 'm': case 'r': case 's': case 'u': case 't': default: break;
                        }
                        continue;
                    }
                    i += 2;   // 其他越狱:跳过2 字节
                    continue;
                }
                if (c0 == '\r') { cx = 0; ++i; continue; }
                if (c0 == '\n') { newline(); ++i; continue; }
                if (c0 == '\x08') { if (cx > 0) --cx; ++i; continue; }
                if (c0 == '\x07') { ++i; continue; }
                // UTF -8号从 数据流 (仅完成序列).
                len = (c0 < 0x80) ? 1 : (c0 >> 5) == 0x6 ? 2 :
                      (c0 >> 4) == 0xE ? 3 : (c0 >> 3) == 0x1E ? 4 : 1;
                if (i + static_cast<std::size_t>(len) > n) {
                    carry = s.substr(i);
                    break;
                }
                cp = (c0 < 0x80) ? c0 : (c0 & (0xff >> (len + 1)));
                for (int k = 1; k < len; ++k)
                    cp = (cp << 6) | (static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]) & 0x3F);
                i += static_cast<std::size_t>(len);
            }
            int wd = (cp < 128) ? 1 : wcwidth(static_cast<wchar_t>(cp));
            if (wd < 0) wd = 1;
            put_cp(cp, wd);
        }
    }

    std::string row(int y) const {
        std::string r = screen[static_cast<std::size_t>(y)];
        while (!r.empty() && r.back() == ' ') r.pop_back();
        return r;
    }
};

// 排水,直到小便安静 macOS 送出小便 送出大便 送入小便 字
// 节 同步,所以第一个 EAGAIN 并不是写完的。
static std::string read_all(int fd) {
    std::string s;
    char buf[8192];
    for (;;) {
        ssize_t k = ::read(fd, buf, sizeof(buf));
        if (k > 0) { s.append(buf, static_cast<std::size_t>(k)); continue; }
        if (k < 0 && errno == EINTR) continue;
        pollfd pfd{fd, POLLIN, 0};
        if (::poll(&pfd, 1, 60) <= 0) break;
    }
    return s;
}

static void tick(int ms = 20) {
    maya::testing::advance_anim_clock_ms(ms);
}

static void dump(const VtEmu& emu, const char* why) {
    std::fprintf(stderr, "--- screen after %s ---\n", why);
    for (int y = 0; y < emu.h; ++y) {
        std::string r = emu.row(y);
        std::fprintf(stderr, "  %2d|%s|\n", y, r.c_str());
    }
}

int main() {
    // 使证人 -Chain 闸门绊线 FATAL : 任何已承诺 -prefix
    // 腐败问题 回滚缓冲区-safety 属性错误 -parked
    // 光标会违反) 大声中止而不是软 -recovering.
    setenv("MAYA_GATE_ABORT", "1", 1);
    setenv("MAYA_NO_SYNC", "1", 1);   // 无同步-output 包装器 — 未同步传输
    // 硬币硬币 -caret 路径: 在 tmux 下, 作曲者返回到所
    // 绘 光标 (tmux 副本) -mode 鬼魂是活光标 所以硬件
    // -caret 以下断言需要 tmux。 -free 内线。
    // 调音器 回退 被涂上涂着的图画覆盖 -mode 运行( R)
    // AGENTTY_PAINTED_CARET =1),与作曲家的绘画路径
    // 相同.
    unsetenv("TMUX");
    setenv("TERM", "xterm-256color", 1);
    setlocale(LC_ALL, "C.UTF-8");

    // tmux 存在通过 FAKE 探测器 接口边界 ,
    // 从不设定 $TMUX 有两个原因 都比这个测试更简单
    //
    //   1. maya ⁇ 娥贝贝琌 一个-SHOT 设置
    //      第一次第composer_config() 调用 之后的 $TMUX
    //      变化无所改变,因为探测器已经回答 "not in tmux" 并冻结了。
    //      下面每个 tmux 的断言都是默默运行 tmux-FREE.
    //   2. 不含 MAYA_TMUX_FAKE 探测器的弹壳 向开发者
    //      真实 tmux 服务器,所以结果取决于谁
    //      的机器和谁的配置 已加载 ——在一个
    //      名词广告ctyle,红色在没有服务器的盒子上,
    //      一个能力测试不能问开发者的环境来回答.
    //
    // 特性列表反映了现代 tmux 报告的内容; ctyle 是其中之一 光标 门实际上咨询(
    // NAME OF TRANSLATORS) DECSCUSR )),"("编程").
    auto set_tmux = [](bool on) {
        if (on) {
            setenv("TMUX", "/tmp/tmux-0/default,1,0", 1);
            setenv("MAYA_TMUX_FAKE", "1", 1);
            setenv("MAYA_TMUX_FEATURES",
                   "ccolour,clipboard,cstyle,extkeys,focus,RGB,sync", 1);
        } else {
            unsetenv("TMUX");
            unsetenv("MAYA_TMUX_FAKE");
            unsetenv("MAYA_TMUX_FEATURES");
        }
        maya::tmux::reset_cache_for_test();   // 新的掩体下为 re-probe
    };
    set_tmux(false);

    // ── 门单位-check ─────────────────────────────────────
    // 作曲家只显示硬件 光标 安全的时候,
    // 每个门下都修一个真正的鬼 -cursor 报告;
    // 直接坚持决定(比驱动整个应用程序更精确)。
    // 跳过到 AGENTTY_PAINTED_CARET 批发选择
    // -out (这迫使每个门都关)——画的 -mode 运行覆盖了路径。
    if (std::getenv("AGENTTY_PAINTED_CARET") == nullptr) {
        Model g;
        g.d.current.messages.push_back({});
        g.d.current.messages.back().role = agentty::Role::User;
        g.ui.composer.text = "hi";
        g.ui.composer.cursor = 2;
        g.ui.terminal_focused = true;
        g.s.phase = agentty::phase::Idle{};

        // 基本情况(tmux) -free ,焦点,闲点:硬件 光标 ON.
        set_tmux(false);
        if (!agentty::ui::composer_config(g).hardware_caret) {
            std::fprintf(stderr, "FAIL(gate): hardware caret off in the "
                         "safe base case (tmux-free, focused, idle)\n");
            return 1;
        }
        // tmux + 闲置 : 光标 ON 复制本时 -mode 鬼是一头 流式
        // 传输 - 临Τ -scroll 问题; 闲置屏幕是静态的,
        // 所以 tmux 的副本 -mode 光标跟踪一个稳定的位置, 并且从不在移动内容上降落。
        // 此前采用强制隐藏。
        // 在 tmux 下,离开常见的闲置状态 光标-LESS —
        // tmux 没有硬件光标。
        // 右侧 -margin 公园处理 流式传输-scroll 案件;闲置不再支付。
        set_tmux(true);
        if (!agentty::ui::composer_config(g).hardware_caret) {
            std::fprintf(stderr, "FAIL(gate): hardware caret OFF under tmux "
                         "while idle — idle is static, no copy-mode ghost\n");
            return 1;
        }
        set_tmux(false);
        // 无重点 终端 : 关闭( 不活动时不眨眼栏) 面板).
        g.ui.terminal_focused = false;
        if (agentty::ui::composer_config(g).hardware_caret) {
            std::fprintf(stderr, "FAIL(gate): hardware caret ON while the "
                         "terminal window is unfocused\n");
            return 1;
        }
        g.ui.terminal_focused = true;
        // tmux + 流式传输 没有最近的编辑: 仍然为OFF(
        // rows 滚动; 向上滚动的阅读用户不能将 光标 re-aimed
        // 移动到 线程 内容上—— 鬼魂右-margin 公园减轻了,
        // 而在屏幕移动时没有完全删除)。 请检查tmux 下方的门是否被保存,
        // 具体地说, 不只是 tmux-free。
        set_tmux(true);
        g.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};
        g.ui.composer.last_edit_ms = maya::anim::default_clock().now_ms();
        maya::testing::advance_anim_clock_ms(10'000);   // 10s 过后
        if (agentty::ui::composer_config(g).hardware_caret) {
            std::fprintf(stderr, "FAIL(gate): hardware caret ON while "
                         "streaming with no recent edit (under tmux)\n");
            return 1;
        }
        set_tmux(false);
        // ..还有一个 RECENT 编辑期间 流式传输 已经 不 带回来:
        // composer_uses_hardware_caret ()
        // 减少硬件 光标 整个期间的 -screen 动画( 直播) 数据流
        // 重绘 ~30时 FPS ,并显示 光标 单位 -frame 25l
        // – 移动 – 25h 舞蹈使光标 -animating 终端
        // 重播他们的淡出 帧 可见闪光灯 涂鸦块 光标 中键入队列中的信件
        // -stream 保持闪烁 -free . tmux 调制解析器 -free
        // 这个人如此外在 `env TMUX=…` 无法掩盖
        // (行为由f7ced849设定——最近-typing例外被故意删除.
        // )
        g.ui.composer.last_edit_ms = maya::anim::default_clock().now_ms();
        maya::testing::advance_anim_clock_ms(500);      // 0.5 秒后开始
        if (agentty::ui::composer_config(g).hardware_caret) {
            std::fprintf(stderr, "FAIL(gate): hardware caret ON while "
                         "streaming (painted caret expected, even when "
                         "actively typing)\n");
            return 1;
        }
        std::fprintf(stderr, "PASS(gate): caret on when idle (incl. tmux); "
                     "painted (off) while streaming, even when typing; "
                     "off when unfocused.\n");
    }

    // 小点 终端 作曲者将文字包裹在一个可以到达的边缘
    // 和 整个 帧 (线程 + 作曲家+状态栏)
    // OVERFLOWS 编号 可视区域 ——作曲家骑着底边,
    // 所以包裹 帧 滚动到本地 回滚缓冲区 和缩水者,
    // 帧 必须删除过去承诺的行。这就是错误报告
    // 来自的几何学(长话、被钉在底部的作曲)。
    const int W = 46, H = 10;
    const int H2 = 16;   // 调整大小 -phase 高度( 纸) 变体)

    int master = -1, slave = -1;
    if (openpty(&master, &slave, nullptr, nullptr, nullptr) != 0) {
        std::fprintf(stderr, "openpty failed\n"); return 2;
    }
    struct winsize ws{};
    ws.ws_col = static_cast<unsigned short>(W);
    ws.ws_row = static_cast<unsigned short>(H);
    ioctl(slave, TIOCSWINSZ, &ws);
    dup2(slave, STDIN_FILENO);
    dup2(slave, STDOUT_FILENO);
    ::close(slave);
    fcntl(master, F_SETFL, fcntl(master, F_GETFL, 0) | O_NONBLOCK);

    // 替换主机分割 maya::detail::Runtime +
    // RunConfig 与 maya::Screen + maya::Options
    // 屏幕拥有设备,并清理其销毁器; `impl()` 最终决定的逃生舱门
    // -bytes 下方的断言,需要明确 cleanup() 调用.
    maya::Options cfg;
    cfg.mode = maya::Mode::Inline;
    auto scr_r = maya::Screen::open(cfg);
    if (!scr_r) { std::fprintf(stderr, "Screen::open failed\n"); return 2; }
    maya::Screen& scr = *scr_r;
    maya::detail::Device& rt = scr.impl();

    Model m;
    // 主持人发送大小为 Msg 随函附上和逐项 SIGWINCH;
    // view() 读它从 Model ,所以测试必须同样。
    auto resized = [&](int cols, int rows) {
        auto [m2, cmd] = agentty::app::update(std::move(m),
                                              Msg{agentty::TerminalResized{cols, rows}});
        m = std::move(m2);
        (void)cmd;
    };
    resized(W, H);
    // 欢迎收看屏幕 —— 平庸的作曲家 -only 帧.
    m.d.current.messages.push_back({});
    m.d.current.messages.back().role = agentty::Role::User;

    VtEmu emu(W, H2);   // LATER调整规模阶段的大小(H216)

    auto frame = [&](const char* tag) {
        tick(20);
        maya::Element root = agentty::ui::view(m);
        // 排水师傅 WHILE 渲染. A macOS ty( 双胞胎)
        // 缓冲区 仅几个 KB ; 满 -frame 写大于该摊位(或
        // 被非拖放) -blocking 写作,直到有人读,
        // 如此排水 后发现撕裂 帧. Linux 4K+线条学科 缓冲区 藏起来
        std::string got;
        std::atomic<bool> done{false};
        std::thread drain([&] {
            char buf[8192];
            for (;;) {
                ssize_t k = ::read(master, buf, sizeof(buf));
                if (k > 0) { got.append(buf, static_cast<std::size_t>(k)); continue; }
                if (done.load()) {
                    // render() 已经回来了,所以,每个 字节
                    // 会 议 日 程 和 议 程 帧 已经写好了,
                    // 等待的只是小提琴手 末尾 60米的沉默,
                    // 而不是25米:共享 CI VM 能够拖那么久,
                    // 一个短的窗口会喂喂模具的撕裂 帧 - 这个测试的bug存在。
                    pollfd pfd{master, POLLIN, 0};
                    if (::poll(&pfd, 1, 60) <= 0) break;   // 静态: 帧 完成
                } else {
                    pollfd pfd{master, POLLIN, 0};
                    (void)::poll(&pfd, 1, 5);
                }
            }
        });
        (void)rt.render(root);
        tick(20);
        done = true;
        drain.join();
        emu.feed(got);
        (void)tag;
    };
    auto type = [&](char32_t ch) {
        auto [m2, cmd] = agentty::app::update(std::move(m),
                                              Msg{agentty::ComposerCharInput{ch}});
        m = std::move(m2);
        (void)cmd;
    };
    auto backspace = [&]() {
        auto [m2, cmd] = agentty::app::update(std::move(m),
                                              Msg{agentty::ComposerBackspace{}});
        m = std::move(m2);
        (void)cmd;
    };

    // ── 基线:空作曲 ──────────────────────────────────────
    frame("initial");
    dump(emu, "initial");

    // 填充行数 一个 CHAR PER FRAME 直到作曲家
    // 用视觉包装为止( 以 光标 滴到第一行下方的一行。
    // 这映射了用户的键盘键盘: 每个字符都是自己的 帧 ,
    // 所以包装 -inducing 字符是 仅 其三角洲 帧.
    int first_text_row = -1, caret_row = -1, caret_col = -1;
    auto scan = [&]() {
        first_text_row = caret_row = caret_col = -1;
        // 电话 光标 谓 EITHER 可见的硬件光标( 硬件 -) 光标 模式
        // — 自 maya CursorHint 工作) ) 翻译: OR 绘画 '#'
        // 块( 涂抹模式 /) AGENTTY_PAINTED_CARET).
        // 两种地图都到 "exactly one caret on screen" ;
        // 以下断言是模式 -agnostic.
        if (!emu.cursor_hidden) {
            caret_row = emu.cy;
            caret_col = emu.cx;
        }
        for (int y = 0; y < H; ++y) {
            std::size_t p = emu.screen[static_cast<std::size_t>(y)].find('#');
            if (p != std::string::npos
                && caret_row < 0) { caret_row = y; caret_col = static_cast<int>(p); }
        }
        for (int y = 0; y < H; ++y) {
            std::string r = emu.row(y);
            if (r.find("BBBB") != std::string::npos) { first_text_row = y; break; }
        }
    };
    // 种子一个词,所以first_text_row是可以找到的.
    for (const char* p = "BBBB"; *p; ++p) type(static_cast<char32_t>(*p));
    frame("seed");
    for (int guard = 0; guard < 80; ++guard) {
        type(U'B');
        frame("typed-1");
        scan();
        if (first_text_row >= 0 && caret_row > first_text_row) break;   // 包起来
    }
    dump(emu, "after fill (wrapped)");
    if (caret_row < 0) {
        std::fprintf(stderr, "FAIL: no caret visible after fill\n");
        return 1;
    }
    std::fprintf(stderr, "after fill: caret at row %d col %d "
                 "(first text row %d)\n", caret_row, caret_col, first_text_row);

    // 包裹状态:光标行 > 第一文本行.
    if (first_text_row < 0 || caret_row <= first_text_row) {
        std::fprintf(stderr, "SKIP: composer did not wrap (caret row %d, "
                             "first text row %d)\n", caret_row, first_text_row);
        return 77;
    }

    // ── 后方空间 ─────────────────────────────────────────────────
    backspace();
    frame("backspace");

    int br = -1, bc = -1;
    auto scan_caret = [&](int& r, int& c) {
        r = c = -1;
        if (!emu.cursor_hidden) { r = emu.cy; c = emu.cx; return; }
        for (int y = 0; y < H; ++y) {
            std::size_t p = emu.screen[static_cast<std::size_t>(y)].find('#');
            if (p != std::string::npos) { r = y; c = static_cast<int>(p); return; }
        }
    };
    scan_caret(br, bc);
    if (br < 0) {
        std::fprintf(stderr, "FAIL: caret vanished after backspace\n");
        return 1;
    }
    std::fprintf(stderr, "after backspace: caret at row %d col %d\n", br, bc);

    if (br > first_text_row) {
        dump(emu, "BUG state");
        std::fprintf(stderr,
            "\nBUG REPRODUCED: caret still on the wrapped row %d after the "
            "shrink (composer is single-row again).\n", br);
        return 42;
    }

    // 并核实:行 光标 曾经占据的必须是 gone/blank.
    if (br == caret_row) {
        dump(emu, "BUG state");
        std::fprintf(stderr,
            "\nBUG (variant): caret did not move rows on shrink "
            "(row %d before and after).\n", caret_row);
        return 43;
    }

    // 振荡：输入和退格交替进行，期间插入光标闪烁帧。
    // 眨眼时 切换 编号 光标 存储单元 每530 ms;
    // 驱动多个循环, 如此 过期 diff槽有 每一个机会浮出水面。
    for (int cycle = 0; cycle < 6; ++cycle) {
        type(U'B');
        frame("osc-type");
        backspace();
        tick(540);           // 跨越闪烁的切换边界
        frame("osc-backspace+blink");
        scan_caret(br, bc);
        if (br < 0) {
            std::fprintf(stderr, "FAIL: caret vanished in cycle %d\n", cycle);
            return 1;
        }
        if (br > first_text_row) {
            dump(emu, "BUG state (osc)");
            std::fprintf(stderr,
                "\nBUG REPRODUCED (cycle %d): caret on wrapped row %d after "
                "shrink.\n", cycle, br);
            return 42;
        }
        // 过期-caret DUPLICATE 检查, 模式 -agnostic
        // 硬件 -caret 模式: 模式 终端 自己的光标 IS 编号 光
        // 标 ——必须可见,必须存在. ZERO 绘制 '#' 块( 任意 '#'
        // 是一个 过期 绘制 存储单元。油漆模式: 完全一个 '#'.
        int n = 0;
        for (int y = 0; y < H; ++y)
            for (char ch : emu.screen[static_cast<std::size_t>(y)])
                if (ch == '#') ++n;
        const int want = emu.cursor_hidden ? 1 : 0;
        if (n != want) {
            dump(emu, "BUG state (osc, dup)");
            std::fprintf(stderr,
                "\nBUG (variant, cycle %d): %d painted caret block(s) on "
                "screen (expected %d; hardware cursor %s).\n",
                cycle, n, want, emu.cursor_hidden ? "hidden" : "visible");
            return 43;
        }
    }

    std::fprintf(stderr, "PASS: caret tracks wrap/shrink through %d "
                 "oscillation cycles.\n", 6);

    // 流式变体：流式过程中，输入框执行相同的换行与收缩过程。
    // 输入 流式传输 状态——即: 仅 说明在哪里 build() 穿过十字路口的
    // 路线 -frame ComponentCache (按键) cache_id ,
    // 哪个 归并 文本+ cursor+状态,所以缓存的 blit 绝不能服务
    // 过期 包几何学).
    m.s.phase = agentty::phase::Streaming{agentty::phase::Active{}};

    // 键入到再次包裹。
    scan();
    for (int guard = 0; guard < 80; ++guard) {
        type(U'B');
        frame("stream-type");
        scan();
        if (first_text_row >= 0 && caret_row > first_text_row) break;
    }
    if (!(first_text_row >= 0 && caret_row > first_text_row)) {
        std::fprintf(stderr, "SKIP: streaming composer did not wrap\n");
        return 78;
    }
    std::fprintf(stderr, "streaming wrapped: caret row %d (first text row %d)\n",
                 caret_row, first_text_row);
    const int s_first_text_row = first_text_row;
    const int s_caret_row = caret_row;

    backspace();
    frame("stream-backspace");
    scan_caret(br, bc);
    if (br < 0) {
        std::fprintf(stderr, "FAIL: caret vanished after streaming backspace\n");
        return 1;
    }
    std::fprintf(stderr, "after streaming backspace: caret at row %d col %d\n",
                 br, bc);
    if (br > s_first_text_row) {
        dump(emu, "BUG state (streaming shrink)");
        std::fprintf(stderr,
            "\nBUG REPRODUCED (streaming): caret on wrapped row %d after "
            "shrink.\n", br);
        return 44;
    }
    if (br == s_caret_row) {
        dump(emu, "BUG state (streaming, no move)");
        std::fprintf(stderr,
            "\nBUG (variant, streaming): caret did not move on shrink.\n");
        return 45;
    }

    // 还要两个周期 流式传输 (缓存 热的现在。
    for (int cycle = 0; cycle < 2; ++cycle) {
        type(U'B');
        frame("stream-osc-type");
        backspace();
        frame("stream-osc-backspace");
        scan_caret(br, bc);
        if (br < 0 || br > s_first_text_row) {
            dump(emu, "BUG state (streaming osc)");
            std::fprintf(stderr,
                "\nBUG REPRODUCED (streaming cycle %d): caret row %d.\n",
                cycle, br);
            return 44;
        }
    }

    std::fprintf(stderr, "PASS: streaming-state (component-cache) path "
                 "tracks wrap/shrink too.\n");

    // 填充变体：实际用户报告中，终端能够容纳内容时，
    // 作曲家会 不 坐在溢出的带子里,所以 maya 反作曲家的反作曲 -bounce
    // 持有( 时间) inline_min_content 垫, kMaxHoldPad
    // =2 自由连接 1 -row 缩小后空格。 垫在 kHoldDecayFrames
    // =6 之后衰变。 NON-RISING 帧 -但只有现在 帧 其实 渲染。
    // 在最后一个后空格后, 仅 渲染 驱动程序是闲置的 光标 闪烁( ~ 4)
    // renders/s ——一旦眨眼停止(15秒闲), 无内容 渲染 : 搁浅的垫
    // 行 = "composer still two lines tall / cursor on the ghost second line"
    // 报告。
    //
    // 重排: 高一点的新小会话 终端 ,然后将 ~12
    // 驱动 帧 (足够被束缚到衰败) 并断言垫行是 GONE
    // — i.e 状态栏的顶端边框坐落着 DIRECTLY
    // 位于作曲家底边框之下.
    {
        struct winsize ws2{};
        ws2.ws_col = (unsigned short)W;
        ws2.ws_row = (unsigned short)H2;
        ioctl(master, TIOCSWINSZ, &ws2);
        resized(W, H2);
        // maya 将下个调整大小 帧 通过其宽度- 后退 /
        // SIGWINCH 路径。
        tick(50);
        frame("resize-tall");

        m.s.phase = agentty::phase::Idle{};
        m.ui.composer.text.clear();
        m.ui.composer.cursor = 0;
        m.ui.composer.browsing = agentty::ComposerState::Live{};
        m.ui.composer.draft_save.reset();
        frame("cleared");

        int prev_bottom = -1;
        auto composer_bottom = [&]() {
            // 作曲家的底边框行: 最后 将框底按住 -left
            // 转角 '╰' (已移交) '?'。
            // 查找最后一行中包含 '?' ,跳过状态栏(
            // 其行中也包含 '?' ); 作曲家的底边框
            // 是位于状态栏顶边框上方的行。我们识别状态栏的
            // 边框 TOP 边框行为第二行 -to-last 边排,
            // 并取其上方者.
            int bordered[3] = {-1, -1, -1};
            int nb = 0;
            for (int y = H2 - 1; y >= 0 && nb < 3; --y) {
                std::size_t p = emu.screen[static_cast<std::size_t>(y)].find('?');
                if (p != std::string::npos) bordered[nb++] = y;
            }
            // 边框 [0] = 状态上,边框 [1] =组合器底部,
            // 边框[2] =聚合器顶部.
            return bordered[1];
        };
        prev_bottom = composer_bottom();
        if (prev_bottom < 0) {
            std::fprintf(stderr, "FAIL: composer bottom border not found after clear\n");
            return 1;
        }

        // 再来一次
        scan();
        for (int guard = 0; guard < 80; ++guard) {
            type(U'B');
            frame("pad-type");
            scan();
            if (first_text_row >= 0 && caret_row > first_text_row) break;
        }
        if (!(first_text_row >= 0 && caret_row > first_text_row)) {
            std::fprintf(stderr, "SKIP: pad-variant composer did not wrap\n");
            return 77;   // ctest SKIP_RETURN_CODE
        }
        const int wrapped_bottom = composer_bottom();
        if (wrapped_bottom <= prev_bottom) {
            std::fprintf(stderr, "SKIP: composer did not grow on wrap "
                         "(bottom %d -> %d)\n", prev_bottom, wrapped_bottom);
            return 77;   // ctest SKIP_RETURN_CODE
        }
        std::fprintf(stderr, "pad-variant: wrapped bottom row %d (was %d)\n",
                     wrapped_bottom, prev_bottom);

        backspace();
        frame("pad-backspace");
        const int after_bs_bottom = composer_bottom();
        std::fprintf(stderr, "pad-variant: bottom after backspace %d\n",
                     after_bs_bottom);

        // 推开 12个 帧。 垫必须衰减到 0 ,
        // 编曲底部必须返回 prev_bottom
        // 如果它保持高一行,则幽灵垫行就搁浅了
        // ——这正是用户所看到的.
        for (int f = 0; f < 12; ++f) {
            tick(265);           // 闪烁一半 -period : 真实 渲染 驱动程序
            frame("pad-decay");
        }
        const int settled_bottom = composer_bottom();
        std::fprintf(stderr, "pad-variant: settled bottom %d (pre-wrap %d)\n",
                     settled_bottom, prev_bottom);
        if (settled_bottom != prev_bottom) {
            dump(emu, "BUG state (pad ghost row)");
            std::fprintf(stderr,
                "\nBUG REPRODUCED (pad): composer bottom settled at row %d "
                "but pre-wrap height was %d — a ghost pad row survived the "
                "shrink.\n", settled_bottom, prev_bottom);
            return 46;
        }
        std::fprintf(stderr, "PASS: anti-bounce pad decays after the "
                     "shrink (no ghost row).\n");
    }

    // 欢迎界面变体：复现用户所说的“仅首次出现”的情况。
    // 开始,一旦第一个提示词消失,它不会发生".
    //
    // 新的课上, 线程 谓 空 财务报告和已审计财务报表 Thread::Config
    // 通往该地的路线 WelcomeScreen (thread.cpp:
    // messages.empty() → is_empty + 欢迎 cfg。 树下与对话有三种不同,
    // 与对话有相互作用。 wrap/shrink 舞蹈: 音乐
    //   - 欢迎你 CLAMPED 改为 term_rows-11 (welcome_screen.cpp
    //     kidleChromeRows)——作曲家确实如此. 不 骑着 可视区域 底部; 下部; 底部, 下部, 底部 ; 底端, 底端; 底底, 底底; 底边, 底边; 底面, 底面; 底侧, 底侧; 底层, 底层; 底座, 底座; 底板, 底板; 底门, 底门; 底角, 底角; 底口, 底口;
    //     底线, 底线; 底框, 底框; 底
    //   • 欢迎词签Bobs(每秒1) -letter ),保留了
    //     visual_hash 以自己的气质推进;
    //   • 输入第一个字符交换欢迎字-对话字(繁体字)
    //     "starters card" first_run 路径仅存在于 线程 空的) (中文(
    //     简体) ).
    // 我之前的电话 变体 将用户信件推到 SUPPRESS
    // ——欢迎——所以他们没有一个能行使这副树。
    // 新鲜空 线程 来,你应当填满, 一个 后空格,
    // 空格 -drive 闪烁并断言作曲家返回到它的单曲
    // -row 身高 高点
    for (const int Hw : {H2, 10}) {
        // 小点 -height 传:欢迎( 已宣布为4行) + 作曲家
        // + 状态栏 OVERFLOW 编号 可视区域 ——作曲家骑在底边,
        // 包装卷轴则会行。这是第一个
        // -start-on-a-small-terminal 几何学.
        {
            struct winsize wsw{};
            wsw.ws_col = (unsigned short)W;
            wsw.ws_row = (unsigned short)Hw;
            ioctl(master, TIOCSWINSZ, &wsw);
            resized(W, Hw);
            tick(50);
            frame("welcome-resize");
        }
        m.d.current.messages.clear();          // 返回欢迎屏幕
        m.d.threads.clear();                   // first_run 启动路径
        m.s.threads_loading = false;
        m.s.phase = agentty::phase::Idle{};
        m.ui.composer.text.clear();
        m.ui.composer.cursor = 0;
        m.ui.composer.browsing = agentty::ComposerState::Live{};
        m.ui.composer.draft_save.reset();
        frame("welcome-fresh");
        dump(emu, "welcome (first start)");

        auto composer_bottom2 = [&]() {
            int bordered[3] = {-1, -1, -1};
            int nb = 0;
            for (int y = H2 - 1; y >= 0 && nb < 3; --y) {
                std::size_t p = emu.screen[static_cast<std::size_t>(y)].find('?');
                if (p != std::string::npos) bordered[nb++] = y;
            }
            return bordered[1];
        };
        // 滚动 -invariant 作曲高度:作曲者之间的行
        // BOTTOM 边框和边框 STATUS BAR 上边框。
        // 欢迎的单词标记 bob 可以移动绝对行( 它包着)
        // -tagline 休息线滚动 in/out ),
        // 因此绝对主播误读为鬼行;只有作曲家和状态栏之间出现真正的一行,
        // 这个差距才能扩大.
        auto composer_gap = [&]() {
            int status_top = -1, comp_bottom = -1;
            for (int y = H2 - 1; y >= 0; --y) {
                std::size_t p = emu.screen[static_cast<std::size_t>(y)].find('?');
                if (p != std::string::npos) {
                    if (status_top < 0)      status_top = y;
                    else if (comp_bottom < 0) { comp_bottom = y; break; }
                }
            }
            return (status_top >= 0 && comp_bottom >= 0)
                   ? status_top - comp_bottom : -1;
        };
        const int welcome_bottom = composer_bottom2();
        const int welcome_gap    = composer_gap();
        if (welcome_bottom < 0) {
            std::fprintf(stderr, "FAIL: welcome composer bottom not found\n");
            return 1;
        }
        std::fprintf(stderr, "welcome: composer bottom row %d\n", welcome_bottom);

        // 填满直到包裹。 NOTE : 数据 首次 字符交换欢迎树
        // ( C) thread.cpp is_empty ~ 谈话),
        // 所以真实的 wrap/shrink 舞蹈会与对话的视角相悖,
        // 帧 说 MATTERS 用户报告是欢迎的 JUST 已
        // 互换: 新鲜会话, 首页 提示词 正在执行中 带类型的 ,
        // 瞬间几何,互换交接(思维) -indicator-style
        // 接口边界 来,来,来 AppLayout 选项
        // 选项注释名。按相同的方式驱动:键到包。
        scan();
        for (int guard = 0; guard < 80; ++guard) {
            type(U'B');
            frame("welcome-type");
            scan();
            if (first_text_row >= 0 && caret_row > first_text_row) break;
        }
        if (!(first_text_row >= 0 && caret_row > first_text_row)) {
            std::fprintf(stderr, "SKIP: welcome-frame composer did not wrap "
                         "(caret row %d, text row %d)\n",
                         caret_row, first_text_row);
            return 81;
        }
        const int w_bottom = composer_bottom2();
        const int w_gap    = composer_gap();
        std::fprintf(stderr, "welcome: wrapped, bottom %d (was %d), gap %d "
                     "(was %d), caret row %d, text row %d\n",
                     w_bottom, welcome_bottom, w_gap, welcome_gap,
                     caret_row, first_text_row);
        dump(emu, "welcome wrapped");

        // ONE后空 ~ 又单行.
        backspace();
        frame("welcome-backspace");
        dump(emu, "welcome right after backspace");

        // 闲置 -drive 闪烁( 闪烁) 仅 渲染 驱动程序),
        // 用于远超 kHoldDecayFrames=6的 渲染.
        for (int f = 0; f < 12; ++f) {
            tick(265);
            frame("welcome-decay");
        }
        dump(emu, "welcome after backspace + idle decay");

        const int settled2 = composer_bottom2();
        const int settled_gap = composer_gap();
        std::fprintf(stderr, "welcome: settled bottom %d (pre-wrap %d), "
                     "gap %d (pre-wrap %d)\n",
                     settled2, welcome_bottom, settled_gap, welcome_gap);
        if (settled_gap != welcome_gap) {
            std::fprintf(stderr,
                "\nBUG REPRODUCED (welcome): composer→status-bar gap settled "
                "at %d rows but pre-wrap was %d — ghost row survives the "
                "first-start shrink.\n", settled_gap, welcome_gap);
            return 47;
        }

        // 光标必须回到单行文字上.
        scan_caret(br, bc);
        scan();
        if (br < 0 || (first_text_row >= 0 && br > first_text_row)) {
            std::fprintf(stderr,
                "\nBUG REPRODUCED (welcome caret): caret row %d, text row "
                "%d — the painted caret is still on the wrapped row.\n",
                br, first_text_row);
            return 48;
        }
        std::fprintf(stderr, "PASS: welcome-screen (first start, H=%d) "
                     "wrap/shrink is clean.\n", Hw);
    }

    // ── 退出路径: "agentty eats composer lines on close" 重排 ───
    // 硬件 -caret 模式。 帧 以物理光标结尾 AT 该
    // CARET CELL - 在作曲盒里, 上面的行 帧 下边。
    // Runtime::cleanup ( )必须发出定稿 字节 (
    // CUD 回到休息的行). 之前 编号 终端 dtor 的
    // 撕裂 \r\ n; 否则, 弹壳 提示词 中点陆地 -box
    // 输出时会吃掉剩下的作曲行 回滚缓冲区.
    {
        // 保证一个正常的焦点闲置作曲家, 并显示 光标。
        m.s.phase = agentty::phase::Idle{};
        frame("pre-exit");
        scan_caret(br, bc);
        const bool caret_was_shown = !emu.cursor_hidden;
        const int  caret_row_pre   = br;

        // 找到那个 帧 '下行:状态栏的下边框是最后一个非
        // -blank 排在屏幕上。
        int frame_bottom = -1;
        for (int y = emu.h - 1; y >= 0; --y) {
            if (emu.row(y).find_first_not_of(' ') != std::string::npos) {
                frame_bottom = y;
                break;
            }
        }

        // 拆掉那个 运行时 真正的戒烟方式
        (void)rt.cleanup();
        emu.feed(read_all(master));

        // 光标现在必须坐着 AT 或 或 时 间 下方 编号
        // 帧 最后一行——从不在作曲盒里——所以贝壳
        // 提示词 接下来的开始 在记录稿下面的一条新线。
        if (caret_was_shown && caret_row_pre >= 0 && frame_bottom >= 0
            && emu.cy < frame_bottom) {
            dump(emu, "exit state");
            std::fprintf(stderr,
                "\nBUG REPRODUCED (exit): cleanup left the cursor at row %d "
                "but the frame bottom is row %d — the shell prompt will "
                "overwrite composer rows (the 'eats lines on close' "
                "report).\n", emu.cy, frame_bottom);
            return 49;
        }
        // 模拟壳体 提示词 编号 终端 下一个字;
        // 它必须降落在新行上,而不是在盒子内。
        const int prompt_row = emu.cy;
        emu.feed("$ \r\n");
        if (frame_bottom >= 0 && prompt_row < frame_bottom) {
            std::fprintf(stderr,
                "\nBUG (exit variant): shell prompt rendered at row %d, "
                "inside the frame (bottom %d).\n", prompt_row, frame_bottom);
            return 49;
        }
        std::fprintf(stderr, "PASS: exit path returns the cursor below the "
                     "frame (row %d >= bottom %d) before the shell prompt.\n",
                     prompt_row, frame_bottom);
    }

    return 0;
}
