#include "agentty/runtime/view/composer.hpp"
#include "agentty/runtime/view/slash_menu.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>

#include "agentty/runtime/composer_attachment.hpp"
#include "agentty/runtime/panel/top.hpp"
#include "agentty/runtime/view/helpers.hpp"
#include "agentty/runtime/view/palette.hpp"
#include "agentty/runtime/view/status_bar/model_badge.hpp"
#include "agentty/provider/selection.hpp"
#include <maya/core/anim_clock.hpp>
#include <maya/terminal/tmux.hpp>
#include <maya/core/motion.hpp>   // anim::keep_animating_after (打字机) (打字) -window 失眠) (电话:

namespace agentty::ui {

namespace {

// 地图 agentty 运行时 状态 ~ 部件状态 枚举。
// 纯数据翻译;部件拥有所有视觉决定(边框颜色, 提示词 粗体,
// 占位符文字,高度针).
maya::Composer::State composer_state(const Model& m) {
    if (m.s.is_awaiting_permission()) return maya::Composer::State::AwaitingPermission;
    if (m.s.is_executing_tool())      return maya::Composer::State::ExecutingTool;
    if (m.s.is_streaming())           return maya::Composer::State::Streaming;
    return maya::Composer::State::Idle;
}

// 将每个占位符替换为人 -readable 标签 标题( 4)
// "[Pasted text · 412 lines · 14 KB]" /
// "[@src/auth/login.cpp]"。翻译 agentty-space
// 光标到视图 -space 并列: 任意 agentty 光标位置是 – by
// 标签-aware 导航于 update/composer.cpp - 总是在...
// 标签 边界,永远不要在符号内,所以地图很好 -defined.
struct DisplayText {
    std::string text;
    int         cursor = 0;
};
DisplayText render_chips(std::string_view src, int agentty_cursor,
                         const std::vector<Attachment>& attachments) {
    DisplayText out;
    out.text.reserve(src.size());
    int i = 0;
    int n = static_cast<int>(src.size());
    int cur = std::clamp(agentty_cursor, 0, n);
    while (i < n) {
        if (i == cur) out.cursor = static_cast<int>(out.text.size());
        if (static_cast<unsigned char>(src[i]) == 0x01) {
            auto len = attachment::placeholder_len_at(
                src, static_cast<std::size_t>(i));
            if (len > 0) {
                auto idx = attachment::placeholder_index(
                    src, static_cast<std::size_t>(i));
                std::string chip = "[?]";
                if (idx < attachments.size()) {
                    chip = "[" + attachment::chip_label(attachments[idx]) + "]";
                }
                out.text.append(chip);
                i += static_cast<int>(len);
                continue;
            }
        }
        out.text.push_back(src[i++]);
    }
    if (cur >= n) out.cursor = static_cast<int>(out.text.size());
    return out;
}

// 视觉剪辑线, 荒谬的宽。 maya 编曲词 -wraps
// 时间 终端 宽度,所以是800 -byte 单人
// -line 粘贴( E) URL, base64 blob,
// hash)产生~10个包裹的行并主导作曲者的垂直空间.
// BODY 任意线条 `kVisibleLineWidth`
// 字节 与 `head … tail (N chars)`
// 纯粹的视觉,基础 缓冲区 光标的主机线是 绝不
// 剪辑后用户可以在粘贴内容内编辑。
//
// 纪律: 运行于 * after * render_chips 所以,
// 我们 字节 已在此计数 标签 字幕,不是原始的占位符,
// 我们工作 字节 ,不显示列——足够接近 ASCII-heavy
// 贴面(URLs / hashes) 我们并不欠完美的石墨
// 计算纯化妆品的剪辑。
constexpr int kVisibleLineWidth = 160;
constexpr int kHeadKeep         = 80;
constexpr int kTailKeep         = 40;

DisplayText clip_long_lines(DisplayText in) {
    DisplayText out;
    out.text.reserve(in.text.size());
    int n   = static_cast<int>(in.text.size());
    int cur = in.cursor;
    int i   = 0;
    while (i < n) {
        int j = i;
        while (j < n && in.text[j] != '\n') ++j;
        int line_len = j - i;
        bool cursor_on_line = cur >= i && cur <= j;
        if (line_len > kVisibleLineWidth && !cursor_on_line) {
            out.text.append(in.text, i, kHeadKeep);
            out.text.append(" \xe2\x80\xa6 ");                  // " … "
            out.text.append(in.text, j - kTailKeep, kTailKeep);
            // 总是-on后缀,这样用户就知道视觉剪辑下还有更多的(
            // "(412 chars)"/"(1.2 KB)").
            char suf[32];
            if (line_len < 1024)
                std::snprintf(suf, sizeof(suf), " (%d chars)", line_len);
            else
                std::snprintf(suf, sizeof(suf), " (%.1f KB)",
                              static_cast<double>(line_len) / 1024.0);
            out.text.append(suf);
        } else {
            // 光标位于此线OR上,线短到渲染逐字记录。
            // 如果光标位于一个剪辑的-eligible线上,
            // 我们就会显示它为-is,所以编辑工作继续
            // 工作——作曲家的-wrap字仍然会块化它,
            // 但至少用户所戳的字节字是可见的。
            if (cursor_on_line && cur > i) {
                // 将光标切换到 OUT 缓冲区.
                // ext没有为此线获得额外的字节,
                // 所以三角洲只是(cur-i)加到当前出局大小.
                int new_cursor = static_cast<int>(out.text.size()) + (cur - i);
                out.cursor = new_cursor;
            }
            out.text.append(in.text, i, line_len);
        }
        if (j < n) {
            out.text.push_back('\n');
            // 恰好在一条剪线的后端新线上设置了光标——将它放置在
            // 帖子上 -clip 代表性的定义是:目的
            // -of-clipped-line;
            // cursor_on_line 上面抓住它,用于未剪切的路径。
            // 对于剪切的路由, 我们在这里处理它: 如果光标坐落在 j
            // ( '\n' 线被剪断, 把它扔到我们刚刚排放的新线上。
            if (cur == j && line_len > kVisibleLineWidth)
                out.cursor = static_cast<int>(out.text.size()) - 1;
        }
        i = j + 1;
        if (j == n) break;  // 没有新线索,我们完了
    }
    // 光标在-of-buffer末端.
    if (cur >= n) out.cursor = static_cast<int>(out.text.size());
    return out;
}

} // 命名空间结束。

// 定义 "does the terminal own the caret this frame"。
// 参见 composer.hpp 为何要分享而不是暗示:
// 配置构建器和 visual_hash 他们必须同意,他们生活在不同的档案里,
// 纯粹的超越模型,廉价的... visual_hash 调用 每一个 帧.
bool composer_uses_hardware_caret(const Model& m) noexcept {
    // AGENTTY_PAINTED_CARET =1 选择退出批发(断开)
    // -DECTCEM 终端 ,或更喜欢涂鸦块的用户。
    const bool painted_caret_env = m.env.painted_caret;
    const auto state = composer_state(m);
    const bool agent_active =
        state == maya::Composer::State::Streaming ||
        state == maya::Composer::State::ExecutingTool;
    // 部件时 -paced 动画在屏幕上 帧 重绘 ~30时 FPS 硬件 光标
    // 单位 -frame 25l – 移动 – 25h 舞蹈使光标 -animating
    // 终端 (WezTerm, Windows 终端 重播他们的淡出 每个 帧:
    // a 光标 flicer(确认为 - AGENTTY_PAINTED_CARET
    // =1 移除它。 涂鸦块。 光标 是一个平原 存储单元 免得你这样
    // 动画两部在-screen上驱动:
    //   • 欢迎 -screen 字母 bob( 当 线程 空的),和
    //   • 现场直播 数据流 / 工具运行( Q) agent_active).
    // 使用涂料 光标 两者都一样 流式传输 已选择已绘制
    // EXCEPT 近期内 -typing 窗口; 将例外情形删除,
    // 从而 数据流 键盘中键后不闪烁。没有动画的闲置会保留本地硬件
    // 光标 + 终端-side 眨眼 闪烁
    const bool welcome_bob = m.d.current.messages.empty();
    return !painted_caret_env
        && !welcome_bob
        && !agent_active
        && ui::panel::top(m) == ui::panel::Kind::None
        && m.ui.terminal_focused
        // 内部 tmux, 只要求硬件 光标 Tmux 何时
        // 会实际 FORWARD 光标 -style
        // 越狱(其次) `cstyle` 没有它,
        // 我们 DECSCUSR 被吞没,而 光标 静静地回到
        // 外形的形状 终端 画过... 回退 老实说,
        // 其实是外人 has_feature ()对于一切都是虚假的,
        // 因此检查范围只局限于tmux案件.
        && (!maya::tmux::active()
            || maya::tmux::has_feature(maya::tmux::Feature::CursorStyle));
}

maya::Composer::Config composer_config(const Model& m) {
    maya::Composer::Config cfg;
    auto disp = clip_long_lines(render_chips(m.ui.composer.text,
                                             m.ui.composer.cursor,
                                             m.ui.composer.attachments));
    cfg.text            = std::move(disp.text);
    cfg.cursor          = disp.cursor;
    cfg.state           = composer_state(m);
    cfg.active_color    = phase_color(m.s.phase);
    cfg.text_color      = fg;
    cfg.accent_color    = accent;
    cfg.warn_color      = warn;
    cfg.highlight_color = highlight;
    cfg.queued          = m.ui.composer.queued.size();
    // LOOP(^B):武装的~~标签+有锡的边界. `looping()`
    // (不是生的标志),
    // 所以武装的-but-empty载荷无法画出永远不会开火的环线.
    cfg.loop            = m.ui.composer.looping();
    cfg.loop_iterations = m.ui.composer.loop_iterations;
    cfg.loop_wait_secs  = m.ui.composer.loop_wait_secs(
        maya::anim::default_clock().now_ms());
    cfg.loop_color      = role_brand_alt;   // 与青色队列标签不同
    // 循环框是读出提示词为re-sent,而不是可编辑的
    // 草稿(状态更新函数将每个变异消息都丢掉),因此渲染改为读
    // -only:暗文本,停放光标.
    cfg.read_only       = cfg.loop;
    cfg.profile         = {.label = std::string{profile_label(m.d.profile)},
                           .color = profile_color(m.d.profile)};
    cfg.expanded        = m.ui.composer.expanded;

    // 脚-row 左槽: 型号 ^ 提供者身份,
    // 从状态栏移到这里。 有两个原因它在这个位置上读得更好:
    //
    //   • 这是你们即将发出的信息的财产 SEND ,所以它会
    //     属于输入,
    //     而不是在流式传输时带有相/吞吐量/上下文的turn-status条。
    //   —— 其接管的槽是自己花在" ⁇ 送 ⁇ ... ⁇ "上.
    //     QQ新线——用户一次学习两把键,
    //     每把键上都无需重覆. 帧 其余时间。
    //
    // 设置非 -empty `status` 取代部件中的提示;
    // show_key_hints =虚假使意图明确,
    // 所以未来的空徽章(未知型号)留下了空位,
    // 而不是默默地复活提示.
    cfg.status          = model_badge_config(m);
    cfg.show_key_hints  = false;
    // 当`/`菜单打开时,它的键会占据插槽,
    // 这样你就可以看到如何完成,运行和不猜测的解职.
    if (slash_menu_open(m)) {
        auto k = [](const char* s) { return maya::dsl::text(s, maya::Style{}.with_fg(fg).with_bold()); };
        auto l = [](const char* s) { return maya::dsl::text(s, maya::Style{}.with_fg(muted).with_dim()); };
        cfg.status = maya::dsl::h(k("\xe2\x86\xb9"), l(" complete  "),     // ↹
                                  k("\xe2\x86\xb5"), l(" run  "),          // ↵
                                  k("\xe2\x86\x91\xe2\x86\x93"), l(" pick  "), // ↑↓
                                  k("esc"), l(" hide")).build();
    }
    // TYPING MID-TURN 说甚么. Enter 将真正做。
    //
    // Enter 转弯时 QUEUES ;转弯时消息会发出.
    // 部件在空的中教它 -composer 占位符,
    // 它会消失在您输入的瞬间 -- 确切地说,当您决定是否
    // 按时 Enter.
    //
    // 附加在模范徽章上而不是替换:徽章是你们将要发送的
    // WITH ,并用它来交换 说 WHEN 却会
    // 用隐藏另一个问题来回答一个问题
    //
    // 固定在两个字前。脚印上已经带有徽章、文字和符号计数器以及
    // 简介 标签 ; 满句( "sends when this turn ends"
    // ) 将七个竞合项目从一个行中切出, 以环境为目的。
    // 排队消息下的记录行带有长的表格, 也就是有人问
    // 我的消息怎么了？
    // 已经在寻找了。
    else if (m.s.active() && !m.ui.composer.text.empty()) {
        cfg.status = maya::dsl::h(
            model_badge_config(m),
            maya::dsl::text("  \xe2\x86\xb5 queues",
                            maya::Style{}.with_fg(muted).with_dim())).build();
    }

    // ── 地面上安检台 真实 载荷 ──────────────────────
    //
    // cfg.text 是那个 标签-rendered
    // 显示字符串: 长粘贴或 @ 文件崩溃为短标题,
    // 所以当附件存在时, 大量计算字/
    // 符/ 线。 扩展附件 正文 回到里面(
    // 同样的通过 传输层 运行于提交时间)并
    // 计数 该 显示显示的显示器是显示在模型上。
    // 当没有附件时,则完全跳过扩展。 载荷 ,
    // 和 -1 使部件从数中获取。
    if (!m.ui.composer.attachments.empty() && !m.ui.composer.text.empty()) {
        std::string full = attachment::expand(m.ui.composer.text,
                                               m.ui.composer.attachments);
        int words = 0;
        bool in_word = false;
        int lines = 1;
        for (char c : full) {
            const bool ws = (c == ' ' || c == '\t' || c == '\n' || c == '\r');
            if (!ws && !in_word) { ++words; in_word = true; }
            else if (ws)         { in_word = false; }
            if (c == '\n') ++lines;
        }
        cfg.word_estimate  = words;
        cfg.token_estimate = static_cast<int>((full.size() + 3) / 4);
        cfg.line_estimate  = lines;
    }

    // 单笔开始 -row 编曲:框内拥抱一行输入,
    // 直到用户自己将文本包到第二行。2层(前默认)
    // 保留永久空白行。 流式传输 反 -flicker
    // 单曲 -row 癬ㄓ候眎 τ
    // cache_id 下方已经保持了整个盒子的稳定
    // 流式传输 帧 所以不需要额外钉住的排来压制波波
    cfg.min_body_rows   = 1;

    // 闪烁 -stop : 将部件递给最后一个 -interaction 时间戳,
    // 这样它就不再在用户闲置后眨眼15s的涂鸦光标(模仿猫咪的)
    // cursor_stop_blinking_after。
    // 保持闲置 agentty 驾驶的 终端 永远的混凝土 GPU 终端 与一个侵略者
    // repaint_delay.
    cfg.last_edit_ms    = m.ui.composer.last_edit_ms;

    // ── 硬件光标 ─────────────────────────────────────────
    // 使用该 终端's 真实 光标为作曲家 光标 :
    // 本地闪烁, 零闲醒 -ups, IME 弹珠锚在真人
    // 身上 存储单元 ,屏幕阅读器跟踪真实位置。
    // 仅在 全部 每一个门都杀了一个真正的鬼 -cursor 报告:
    //   • 没有 覆盖面板 (palette/picker 拥有密钥,现在拥有
    //     光标 也通过 query_caret 搜索中 头文件);
    //   • 终端 窗口为 FOCUSED (?1004)——在无盲目酒吧
    //     闲置 面板;
    //   • 闲置的代理人, OR 用户 带类型的 短短几秒钟内
    //     代理时 数据流 ,滚动成原生 回滚缓冲区 显示光标下;
    //     如果用户滚动到读取( tmux 复制) -mode /
    //     回滚缓冲区 将可见屏幕冻结) -aimed 光标 渲染
    //     结束 THREAD 内容——
    //     "ghost cursor on the rail line" 报表中。
    //     阅读用户不需要 光标 ;打字者在第一个键盘上重获(
    //     ComposerCharInput 刷新点 last_edit_ms ),
    //     以及 流式传输 勾选为 -evaluates 此窗口时 帧.
    // AGENTTY_PAINTED_CARET =1 选择退出批发(断开)
    // -DECTCEM 终端 或更喜欢该块的用户)。
    // 读取 -only (roop模式) 将 光标 : 没有任何输入,
    // 以及硬件 光标 就会在一瞬间 缓冲区 忽略了钥匙
    cfg.hardware_caret = composer_uses_hardware_caret(m) && !cfg.read_only;
    // 理由为(!) agent_active || typing_recently (c)
    // 大门—— 相同 规则有和没有 tmux :
    //   • 闲置(代理人不闲置) 流式传输 : 屏幕是 STATIC ,所以Tmux的(Tmux),(Tmx的),(和"Tmux")
    //     复制 -mode 光标跟踪一个稳定的位置——没有幽灵。
    //     这是您最常在的休息状态, 以及硬件的位置。 光标 's
    //     win( 您的配置 block/beam 当地人
    //     眨眼 IME 锚定)最要紧. 光标 ON.
    //   • 流式传输 和 你曾经 带类型的 4s之内:您正在排队
    //     并看着作曲家... 光标 ON (按右边键,
    //     所以是tmux卷轴) -up 鬼魂在空地上降落 而不是在地上
    //     降落 线程 铁轨; 参见 maya's 光标 ).  ⁇ .
    //   • 流式传输 和 闲置 -handed : 滚动到 回滚缓冲区 ; 如果
    //     显示 光标 会跟踪内容。
    //     光标 OFF——涂装的回退覆盖.
    // tmux下,复制-mode光标是tmux的OWN,
    // 它画在面板-光标位置,而不管DECTCEM,并且从面板侧
    // (经验上是被复制的)不可移动. 右-margin公园+这个
    // 门意味着它只出现在空白的垫板上,从不出现在-thread中
    // ——所以tmux不再需要比光亮的终端更严格的规则.
    // 输入窗口 LAPSES 没有任何模式改变,没有什么会重新 -run
    // view() 来翻转 光标 返回( 涂抹模式 / 公园) ,
    // 直至下一个键键键或 数据流 选中此项。 请求在失效边界上一个醒点,
    // 从而及时翻转。 闲置 -with-no-window 帧
    // 请求无( 0) -wake 闲置保存).
    // 重置在这里,而不是线程从
    // composer_uses_hardware_caret
    // : 此块需要 WINDOW ,而不是 光标 与一个呼叫者相比,
    // 复制两个便宜的比较比扩大上游的返回类型要好。
    {
        constexpr std::int64_t kTypingWindowMs = 4000;
        const bool agent_active =
            cfg.state == maya::Composer::State::Streaming ||
            cfg.state == maya::Composer::State::ExecutingTool;
        const bool typing_recently =
            m.ui.composer.last_edit_ms > 0 &&
            (maya::anim::default_clock().now_ms() - m.ui.composer.last_edit_ms)
                < kTypingWindowMs;
        if (cfg.hardware_caret && typing_recently && agent_active) {
            const auto until_lapse = kTypingWindowMs
                - (maya::anim::default_clock().now_ms() - m.ui.composer.last_edit_ms);
            if (until_lapse > 0)
                maya::anim::keep_animating_after(until_lapse + 30);
        }
    }

    // ── 十字记 -frame 缓存 键( 键) ( 流式传输 反 -flicker) ───────────────
    //
    // 期间 流式传输 重置主机( E) -runs view() ;
    // 没有稳定的身份, 作曲家的完整框( 边框 + 分隔符
    // + 宽度) -adaptive 提示组件) re
    // -lays-out 每一个 帧 和提示行的 1 键
    // -cell 漂浮读作闪烁。 归并 输入 恰好 改变所提供
    // 的投入 存储单元 故键在众多之间恒定 流式传输 帧 ,
    // 其中作曲者视觉上完全相同,并移动其中任何一种动作的
    // 瞬间(用户类型,相位翻转,队列深度勾选,配置换乘).
    // maya::Composer::build ()只在活动
    // 时(光标稳定,不眨眼)查阅,因此,排除眨眼阶段是安全的。
    cfg.cache_id = maya::CacheIdBuilder{}
        .add(std::string_view{"agentty-composer"})
        .add(std::string_view{cfg.text})
        .add(static_cast<std::uint64_t>(cfg.cursor))
        .add(static_cast<std::uint64_t>(cfg.state))
        .add(cfg.active_color)
        .add(static_cast<std::uint64_t>(cfg.queued))
        // 循环模式更改边框色调 和 添加a 标签 标签带有
        // 重复计数的标签——两者都必须移动散列或自动
        // -send 将 重绘 没什么
        .add(static_cast<std::uint64_t>(cfg.loop ? 1 : 0))
        .add(static_cast<std::uint64_t>(
            static_cast<std::uint32_t>(cfg.loop_iterations)))
        // 倒计时每秒都会计时一次,必须 重绘 与它。 与它一样。
        .add(static_cast<std::uint64_t>(
            static_cast<std::uint32_t>(cfg.loop_wait_secs)))
        .add(std::string_view{cfg.profile.label})
        .add(cfg.profile.color)
        .add(static_cast<std::uint64_t>(cfg.expanded ? 1 : 0))
        // 脚印的模型 \ 提供者 标签 是一个不透明的元素
        // 缓存 ,所以散开 FACTS 背后——否则,
        // 将转换模型或提供者中间 -session 制作一个作曲家,
        // 门的分数是视觉完全相同的, 标签 继续命名旧模型,
        // 直到其他的散列轴发生移动。
        .add(std::string_view{m.d.model_id.value})
        .add(std::string_view{m.s.smart_turn_model.value})
        .add(m.d.selection.provider_id())
        .add(static_cast<std::uint64_t>(cfg.hardware_caret ? 1 : 0))
        .add(static_cast<std::uint64_t>(cfg.min_body_rows))
        .add(static_cast<std::uint64_t>(
            static_cast<std::uint32_t>(cfg.token_estimate)))
        .add(static_cast<std::uint64_t>(
            static_cast<std::uint32_t>(cfg.word_estimate)))
        .add(static_cast<std::uint64_t>(
            static_cast<std::uint32_t>(cfg.line_estimate)))
        .build();
    return cfg;
}

} // namespace agentty::ui
