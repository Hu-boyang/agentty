// composer_edit_test — 状态更新函数-level 作曲家
// 的文字报道 -wise 删除密钥( E) Ctrl+W / Alt +D),
// 与现有杀并加 -line + 单词 -motion 家庭 驱动器
// detail::composer_update 直接( 无) maya ,
// 没有 I/O 并声称: 缓冲区 + 光标在读线的 unix 上降落 -word-rubout
// 杀死 -word 包括将这些内容纳入 标签-aware 边界,在一条中去除整个
// 附件符号。

#include <string>

#include "agtest.hpp"

#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/composer_attachment.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/msg.hpp"

using agentty::Model;
using agentty::Cmd;
using agentty::app::detail::composer_update;
namespace msg = agentty::msg;
// 作曲家的留言 结构在 agentty * 命名空间;
// msg::ComposerMsg 变体 化名,把车开进去
using agentty::ComposerDeleteWordBack;
using agentty::ComposerDeleteWordForward;
using agentty::ComposerUndo;
using agentty::ComposerCharInput;
using agentty::ComposerCursorWordLeft;
using agentty::ComposerCursorWordRight;
using agentty::ComposerKillToEndOfLine;

namespace {

void check_edit(const char* name, bool cond, const std::string& got = {}) {
    CHECK_MESSAGE(cond, name << " — got: [" << got << "]");
}

// 运行一个 ComposerMsg 通过 状态更新函数 ,返回生成的模型。
Model step(Model m, msg::ComposerMsg cm) {
    return ::agentty::app::detail::step(::agentty::app::detail::composer_update, std::move(m), std::move(cm)).first;
}

Model with_text(std::string text, int cursor) {
    Model m;
    m.ui.composer.text   = std::move(text);
    m.ui.composer.cursor = cursor;
    return m;
}
} // 命名空间结束。

TEST_CASE("composer_edit") {
    // ── Ctrl +W: 向后删除单词 ──────────────────────────────────
    {
        // 光标在尾 ~ 下下最后一字("baz")+其主空格。
        auto m = step(with_text("foo bar baz", 11), ComposerDeleteWordBack{});
        check_edit("ctrl-w drops trailing word", m.ui.composer.text == "foo bar ",
              m.ui.composer.text);
        check_edit("ctrl-w cursor follows", m.ui.composer.cursor == 8,
              std::to_string(m.ui.composer.cursor));
    }
    {
        // 光标中间 -buffer (在之后) "bar " ) ——删除 "bar " 返回到 "foo ".
        auto m = step(with_text("foo bar baz", 8), ComposerDeleteWordBack{});
        check_edit("ctrl-w mid-buffer", m.ui.composer.text == "foo baz",
              m.ui.composer.text);
        check_edit("ctrl-w mid cursor", m.ui.composer.cursor == 4,
              std::to_string(m.ui.composer.cursor));
    }
    {
        // 光标在 0 ~ no-op, 不崩溃。
        auto m = step(with_text("foo", 0), ComposerDeleteWordBack{});
        check_edit("ctrl-w at start no-op", m.ui.composer.text == "foo" &&
              m.ui.composer.cursor == 0, m.ui.composer.text);
    }
    {
        // 空 缓冲区 □ 没有 -op.
        auto m = step(with_text("", 0), ComposerDeleteWordBack{});
        check_edit("ctrl-w empty no-op", m.ui.composer.text.empty() &&
              m.ui.composer.cursor == 0, m.ui.composer.text);
    }

    // ── Alt +D:删除前置单词 ────────────────────────────────────
    {
        // 光标在 0 ~ 下第一字("foo")+后面的空格。
        auto m = step(with_text("foo bar baz", 0), ComposerDeleteWordForward{});
        check_edit("alt-d drops leading word", m.ui.composer.text == "bar baz",
              m.ui.composer.text);
        check_edit("alt-d cursor stays", m.ui.composer.cursor == 0,
              std::to_string(m.ui.composer.cursor));
    }
    {
        // 光标,位于第-op端。
        auto m = step(with_text("foo bar", 7), ComposerDeleteWordForward{});
        check_edit("alt-d at end no-op", m.ui.composer.text == "foo bar" &&
              m.ui.composer.cursor == 7, m.ui.composer.text);
    }

    // 识别附件标签：在标签右边缘按 Ctrl+W，
    //    中风删除整个占位符符。 @ @ info
    {
        Model m;
        // 构建 "see <CHIP>" 地点 CHIP 是一个真正的占位符,用于 at [0]。
        agentty::Attachment att;
        att.kind       = agentty::Attachment::Kind::Paste;
        att.line_count = 3;
        att.byte_count = 42;
        att.body.set_bytes("x\ny\nz");
        m.ui.composer.attachments.push_back(std::move(att));
        std::string ph = agentty::attachment::make_placeholder(0);
        m.ui.composer.text   = "see " + ph;
        m.ui.composer.cursor = static_cast<int>(m.ui.composer.text.size());

        m = step(std::move(m), ComposerDeleteWordBack{});
        // 电话 标签 信物不见了; "see " (或 减 减 ) "see" 仍然在, 标签 已删除。
        const bool chip_gone =
            m.ui.composer.text.find(static_cast<char>(0x01)) == std::string::npos;
        check_edit("ctrl-w removes whole chip token", chip_gone, m.ui.composer.text);
    }

    // ── 撤销恢复 ctrl-w. ─────────────────────────────────────────
    {
        auto m = step(with_text("foo bar baz", 11), ComposerDeleteWordBack{});
        m = step(std::move(m), ComposerUndo{});
        check_edit("undo restores ctrl-w", m.ui.composer.text == "foo bar baz",
              m.ui.composer.text);
    }

    // ── Undo coalescing: a run of typed chars is ONE undo unit; the
    //    开始一个新单元, 以逐字反转字,
    //    而不是字符。 @ @ info
    {
        Model m;  // 开始为空
        for (char c : std::string("hello"))
            m = step(std::move(m), ComposerCharInput{static_cast<char32_t>(c)});
        check_edit("typed run present", m.ui.composer.text == "hello",
              m.ui.composer.text);
        // 一个撤销应该擦去WHOLE连在一起的单词,回到空的.
        m = step(std::move(m), ComposerUndo{});
        check_edit("one undo rewinds the whole typed word",
              m.ui.composer.text.empty(), m.ui.composer.text);
    }
    {
        // "foo bar" : 空格断开运行, 因此取消皮条 "bar" ,
        // 离开 "foo " (空间) 快照).
        Model m;
        for (char c : std::string("foo bar"))
            m = step(std::move(m), ComposerCharInput{static_cast<char32_t>(c)});
        m = step(std::move(m), ComposerUndo{});
        check_edit("undo peels one word across a space boundary",
              m.ui.composer.text == "foo ", m.ui.composer.text);
    }

    // ── 单词运动消耗一个RUN的点数作为单位. ─────────
    {
        // 光标后 "))))" ~ 字词 -left 跳过整个运行。
        auto m = step(with_text("a))))", 5), ComposerCursorWordLeft{});
        check_edit("word-left eats punctuation run", m.ui.composer.cursor == 1,
              std::to_string(m.ui.composer.cursor));
    }
    {
        // 光标在前面 "((((" ~ 字词 -right 跳过整个跑步。
        auto m = step(with_text("((((a", 0), ComposerCursorWordRight{});
        check_edit("word-right eats punctuation run", m.ui.composer.cursor == 4,
              std::to_string(m.ui.composer.cursor));
    }

    // 队列预览重置：预览排队槽位时进行编辑，必须放弃
    //    浏览后提交不会删除错误的队列条目。 QQ
    {
        Model m;
        m.ui.composer.queued.push_back({"queued msg", {}});
        m.ui.composer.browsing = agentty::ComposerState::QueuePeek{0};   // 假装我们 Alt+↑'d
        m.ui.composer.text = "queued msg";
        m.ui.composer.cursor = 10;
        m = step(std::move(m), ComposerCharInput{U'!'});
        check_edit("typing while peeking returns the composer to Live",
              m.ui.composer.is_live(),
              m.ui.composer.queue_peek_index()
                  ? "still peeking " + std::to_string(*m.ui.composer.queue_peek_index())
                  : "history");
    }

    // 删除到行尾，使用 Alt+K；Ctrl+K 被占用后重新提供可达入口。
    //    全球 -palette 冲突。
    {
        auto m = step(with_text("foo bar baz", 4), ComposerKillToEndOfLine{});
        check_edit("kill-to-end deletes from cursor to EOL",
              m.ui.composer.text == "foo ", m.ui.composer.text);
    }
    {
        // 多个 -line : 杀 -to-end 停止在新线,保持它。
        auto m = step(with_text("foo\nbar", 1), ComposerKillToEndOfLine{});
        check_edit("kill-to-end stops at newline", m.ui.composer.text == "f\nbar",
              m.ui.composer.text);
    }
    {
        // 时 EOL 已经——没有 -op.
        auto m = step(with_text("foo", 3), ComposerKillToEndOfLine{});
        check_edit("kill-to-end at EOL is a no-op", m.ui.composer.text == "foo",
              m.ui.composer.text);
    }

    // ── 循环模式(^B) ───────────────────────────────────────────────
    // ARMING 立即提交(这就是 ^B:
    // 发送此消息，并持续重复发送。
    // 需要将 deps() 接口边界 - 超出这个纯粹的范畴 -editing 测试。
    // IS 被困在这里的每一个过渡 状态更新函数 拥有不触摸网络:空
    // -payload 解除武装必须彻底清除 快照.
    {
        // 来,来,来 空 作曲家不能武装循环——无循环 载荷 绝不开火,
        // 所以拒绝国家而不是进入。 (不在此路径上提交,
        // 所以不) deps() (需要这样做。 )
        auto m = step(with_text("", 0), agentty::ComposerToggleLoop{});
        check_edit("^B on an empty composer does not arm",
                   !m.ui.composer.loop_armed, "armed with no payload");
        check_edit("empty arm leaves looping() false",
                   !m.ui.composer.looping(), "looping with no payload");
    }
    {
        // 解除武装 EVERYTHING. A 过期 快照 留下的
        // 会是后来 -arm 默默地重复错误 提示词 ,
        // 并设置一个 过期 计数会 渲染 ~N为一环不跑.
        Model m;
        m.ui.composer.text            = "live text";
        m.ui.composer.loop_armed      = true;
        m.ui.composer.loop_text       = "run the tests";
        m.ui.composer.loop_iterations = 3;
        check_edit("precondition: looping", m.ui.composer.looping(), "not looping");

        auto off = step(std::move(m), agentty::ComposerToggleLoop{});
        check_edit("^B disarms", !off.ui.composer.loop_armed, "still armed");
        check_edit("disarm clears the snapshot",
                   off.ui.composer.loop_text.empty(), off.ui.composer.loop_text);
        check_edit("disarm resets the counter",
                   off.ui.composer.loop_iterations == 0,
                   std::to_string(off.ui.composer.loop_iterations));
        check_edit("disarm leaves looping() false",
                   !off.ui.composer.looping(), "still looping");
        check_edit("disarm does not touch the live composer text",
                   off.ui.composer.text == "live text", off.ui.composer.text);
    }
    {
        // 循环的作曲者是 READ-ONLY : 框显示 提示词 重新出现
        // -sent ,所以键盘键不能让显示偏离 loop_text (用户会读一件事,
        // 而 agentty )),""("""","(")),
        // ("")"("(","))).
        Model m;
        m.ui.composer.text            = "run the tests";
        m.ui.composer.cursor          = 13;
        m.ui.composer.loop_armed      = true;
        m.ui.composer.loop_text       = "run the tests";

        auto typed = step(std::move(m), ComposerCharInput{U'X'});
        check_edit("typing is ignored while looping",
                   typed.ui.composer.text == "run the tests",
                   typed.ui.composer.text);

        auto bsp = step(std::move(typed), agentty::ComposerBackspace{});
        check_edit("backspace is ignored while looping",
                   bsp.ui.composer.text == "run the tests", bsp.ui.composer.text);

        // . 不过... ^B 仍然有效,否则模式会是一个陷阱。
        auto out = step(std::move(bsp), agentty::ComposerToggleLoop{});
        check_edit("^B still disarms from inside the locked state",
                   !out.ui.composer.loop_armed, "still armed");
        check_edit("the text survives the unlock",
                   out.ui.composer.text == "run the tests", out.ui.composer.text);

        // 一旦解锁,编辑工作又开始.
        auto edit_ok = step(std::move(out), ComposerCharInput{U'!'});
        check_edit("editing resumes after disarm",
                   edit_ok.ui.composer.text != "run the tests",
                   edit_ok.ui.composer.text);
    }

    // ── 循环 退避 ────────────────────────────────────────────
    // 保持循环的属性 无法变成 DoS 用户自身费率
    // 限制: 无效的迭代绝不能重现 -send 立刻开始
    {
        agentty::ComposerState c;
        c.loop_armed = true;
        c.loop_text  = "go";
        const std::int64_t t0 = 1'000'000;

        check_edit("a healthy loop is ready immediately",
                   c.loop_ready(t0), "not ready");

        // 瞬变故障 → 短 退避 ,以及 不 准备就绪
        c.loop_backoff(/*军衔=*/0, /*retry_after=*/0, t0);
        check_edit("a failed iteration is not ready immediately",
                   !c.loop_ready(t0), "re-fired with no backoff");
        check_edit("transient backoff is seconds, not minutes",
                   c.loop_wait_secs(t0) > 0 && c.loop_wait_secs(t0) <= 10,
                   std::to_string(c.loop_wait_secs(t0)));
        check_edit("ready again once the deadline passes",
                   c.loop_ready(t0 + 60'000), "still waiting");

        // 连续的失败升级。
        const int first = c.loop_wait_secs(t0);
        c.loop_backoff(0, 0, t0);
        check_edit("consecutive failures back off further",
                   c.loop_wait_secs(t0) > first,
                   std::to_string(c.loop_wait_secs(t0)));

        // 成功重排了线条, 下一关又开始便宜了。
        c.loop_note_success();
        check_edit("success clears the wait", c.loop_ready(t0), "still waiting");
        c.loop_backoff(0, 0, t0);
        check_edit("success reset the escalation",
                   c.loop_wait_secs(t0) == first,
                   std::to_string(c.loop_wait_secs(t0)));
    }
    {
        // 利率限制必须退后 HARDER 而不是瞬间发光的光辉。
        // 重试 a 快速429是加深它的原因。
        agentty::ComposerState a, b;
        a.loop_armed = b.loop_armed = true;
        a.loop_text  = b.loop_text  = "go";
        const std::int64_t t0 = 1'000'000;
        a.loop_backoff(/*军衔=*/0, 0, t0);   // 瞬间
        b.loop_backoff(/*军衔=*/1, 0, t0);   // 利率限制/ 认证
        check_edit("rate-limit backoff exceeds transient backoff",
                   b.loop_wait_secs(t0) > a.loop_wait_secs(t0),
                   std::to_string(b.loop_wait_secs(t0)) + " vs "
                       + std::to_string(a.loop_wait_secs(t0)));
    }
    {
        // 服务器的自有 重试-After 胜负是完全的,它说,
        // 什么时候回来,任何猜测,我们不是粗鲁的,就是不必要的迟钝的。
        agentty::ComposerState c;
        c.loop_armed = true;
        c.loop_text  = "go";
        const std::int64_t t0 = 1'000'000;
        c.loop_backoff(/*军衔=*/1, /*retry_after=*/42, t0);
        check_edit("Retry-After is honoured verbatim",
                   c.loop_wait_secs(t0) == 42,
                   std::to_string(c.loop_wait_secs(t0)));
    }
}

// 缺少必要论据的`/prompt`作为惯例说明而不是第
// 提示词号,它必须作为现状的祝词,留下草案——提交它时
// 将花在只说"you forgot an argument"
// 号的讯息上。
TEST_CASE("composer slash: a missing prompt argument is a toast, not a send") {
    using agentty::SlashPromptRendered;
    {
        Model m = with_text("/cbm:explore_codebase", 21);
        m.ui.composer.slash_pending = true;
        Cmd c = composer_update(m, SlashPromptRendered{
            .typed = "/cbm:explore_codebase",
            .text  = "/cbm:explore_codebase needs query \xe2\x80\x94 try "
                     "/cbm:explore_codebase <query> [depth]",
            .usage = true});
        CHECK_FALSE(c.is_none());                       // 烤面包的清晨器
        CHECK_FALSE(m.ui.composer.slash_pending);       // 未卡入待处理
        CHECK(m.ui.composer.text == "/cbm:explore_codebase");   // 草稿保存
        CHECK(m.s.status.find("needs query") != std::string::npos);
    }
    {
        // 同样的讯息 `usage` 清除 IS a 已交付
        // 提示词 :它取代作曲家并提交.
        Model m = with_text("/cbm:explore_codebase auth", 26);
        m.ui.composer.slash_pending = true;
        (void)composer_update(m, SlashPromptRendered{
            .typed = "/cbm:explore_codebase auth",
            .text  = "user: explored",
            .usage = false});
        CHECK(m.s.status.find("needs query") == std::string::npos);
    }
}

// 可能是已丢弃的图像路径的粘贴会转到 工作线程(
// 检查读取文件); 其它任何粘贴。 回答会处理。
TEST_CASE("composer paste: image paths are checked off the fold") {
    using agentty::ComposerPaste;
    using agentty::ImagePathSniffed;
    {
        Model m;
        (void)composer_update(m, ComposerPaste{"hello world"});
        REQUIRE(m.ui.composer.attachments.size() == 1);   // 贴在原地
        CHECK(m.ui.composer.attachments[0].kind
              == agentty::Attachment::Kind::Paste);
    }
    {
        Model m;
        Cmd c = composer_update(m, ComposerPaste{"/tmp/shot.png"});
        CHECK_FALSE(c.is_none());          // 嗅探任务
        CHECK(m.ui.composer.attachments.empty());   // 还没有贴上
        // 不是图像:文本作为普通的粘贴片进入.
        (void)composer_update(m, ImagePathSniffed{.text = "/tmp/shot.png"});
        REQUIRE(m.ui.composer.attachments.size() == 1);
        CHECK(m.ui.composer.attachments[0].kind
              == agentty::Attachment::Kind::Paste);
    }
    {
        Model m;
        (void)composer_update(m, ImagePathSniffed{
            .text = "/tmp/a.png", .path = "/tmp/a.png",
            .media_type = "image/png", .body = "\x89PNG...."});
        REQUIRE(m.ui.composer.attachments.size() == 1);
        CHECK(m.ui.composer.attachments[0].kind
              == agentty::Attachment::Kind::Image);
        CHECK(m.ui.composer.attachments[0].media_type == "image/png");
    }
}

// 或 Ctrl +V 和 a 组合键 `richpaste:1:` 贴在盒子里。
TEST_CASE("composer paste: clipboard items and armored text") {
    using agentty::ClipboardItem;
    using agentty::ClipboardRead;
    using agentty::ComposerPaste;
    using K = agentty::Attachment::Kind;
    {
        // 剪贴板上的图像和一些文字:图像标签和粘贴.
        Model m;
        ClipboardRead r;
        r.items.push_back({ClipboardItem::Kind::Image, "image/png", {}, "\x89PNGxxxx"});
        r.items.push_back({ClipboardItem::Kind::Text, "text/plain", {}, "hi there"});
        (void)composer_update(m, std::move(r));
        REQUIRE(m.ui.composer.attachments.size() == 2);
        CHECK(m.ui.composer.attachments[0].kind == K::Image);
        CHECK(m.ui.composer.attachments[0].path == "<clipboard>");
        CHECK(m.ui.composer.attachments[1].kind == K::Paste);
    }
    {
        // A 已保存 文件的路径是 带类型的 进去,不是碎裂。
        Model m;
        ClipboardItem it{ClipboardItem::Kind::Text, "text/plain", {}, "/x/a.pdf"};
        it.saved_path = true;
        ClipboardRead r;
        r.items.push_back(std::move(it));
        (void)composer_update(m, std::move(r));
        CHECK(m.ui.composer.attachments.empty());
        CHECK(m.ui.composer.text == "/x/a.pdf ");
    }
    {
        // 一个装甲的PNG贴着作为文本的图像回来.
        Model m;
        const std::string png = "\x89PNG\r\n\x1a\n0123";
        (void)composer_update(m, ComposerPaste{"richpaste:1:image/png::12::iVBORw0KGgowMTIz"});
        REQUIRE(m.ui.composer.attachments.size() == 1);
        CHECK(m.ui.composer.attachments[0].kind == K::Image);
        CHECK(m.ui.composer.attachments[0].body.bytes() == png);
    }
    {
        // 块断块粘贴为纯文本。
        Model m;
        (void)composer_update(m, ComposerPaste{"richpaste:1:image/png::99::AAAA"});
        REQUIRE(m.ui.composer.attachments.size() == 1);
        CHECK(m.ui.composer.attachments[0].kind == K::Paste);
    }
}
