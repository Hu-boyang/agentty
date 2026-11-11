// md_robustness_test ——减记 渲染
// 器 绝不能崩溃, 决不静静地放下内容, 也绝不操纵一段,
// 不管模型的发射量或表面有多窄。
//
// 两者都是真实的,运来的bug:
//   • 帆布(1,......)造成负面因素 std::size_t 扔下来
//     bad_alloc 从内到内 渲染 ——任何通过减
//     值计算宽度的呼叫器(一个其铬超过其母体的嵌入部件,
//     a),都能够达到硬崩溃。 终端 在调整大小时报告0列
//     竞态).
//   • 减值部件无条件的2-column缩进消耗了该设备
//     整个 宽度为% 2 的内容区域,
//     因此文档完全没有显示。
//     渲染器 已经。
//
// Model 产出按性质是对抗性的(pseudo) -tags
// 喜欢 <shell> ,角括号偏差,半 -streamed 标记,
// 所以这些形状也被扫描在这里: 每个人必须生存在每一个宽度。

#include "agtest.hpp"

#include <maya/print.hpp>
#include <maya/widget/markdown.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[nodiscard]] std::string render_md(const std::string& src, int width,
                                    bool live = false) {
    auto w = std::make_shared<maya::StreamingMarkdown>();
    w->set_live(live);
    w->set_reveal_fx(false);
    w->set_reveal_decorate(false);
    w->append(src);
    if (!live) w->finish();
    return maya::render_to_string(w->build(), width);
}

// 可见( 非) -space ()字符——用于 "did the content survive".
[[nodiscard]] std::size_t visible_chars(std::string_view s) {
    std::size_t n = 0;
    for (char c : s)
        if (c != ' ' && c != '\n' && c != '\r' && c != '\t') ++n;
    return n;
}

// 形状是一个聊天模型, 实际释放出打破这个渲染器的。
const std::vector<std::string>& adversarial_docs() {
    static const std::vector<std::string> docs = {
        "plain prose with no markup at all, just words that wrap",
        "Yes - `^U` is the field clear chord (`Intent::ClearField`), but "
        "you're describing `^K` clearing the whole buffer.",
        // 修道士( 修道) -tags : 否 (中文(简体) ). HTML ,
        // 必须 渲染 作为文本,不得 打开原始 -HTML 将下方的标记吞下
        "<shell>\nUse POSIX tools; `uname -a` gives the kernel.",
        "The prompt has a <shell> stanza and a <thinking> block inline.",
        "<tool_call name=\"x\">\nprose after the pseudo-tag",
        "<system-reminder>\n- a list item\n- another item",
        // 不平衡/ 括号被删除—— 永远不要挂标签, 必须保持文字化。
        "Compare `a < b` and `c > d` in one paragraph.",
        "an unclosed <tag that never ends on this line",
        "a lone < and a lone > and a <> empty pair",
        // 真实的HTML与散文混合.
        "<div>\nreal html block\n</div>\n\nprose after",
        "inline <b>bold</b> and <i>italic</i> tags",
        // 结构必须维持在狭窄的宽度。
        "# Heading\n\n- bullet one\n- bullet two\n\n```sh\ncode line\n```",
        "| a | b |\n|---|---|\n| 1 | 2 |",
        "> a block quote that is long enough to need wrapping somewhere",
    };
    return docs;
}

} // 命名空间结束。

TEST_CASE("markdown: never crashes at any width, including degenerate ones") {
    // 宽度 呼叫者可以真正产生: 真正的 终端 ,
    // 一个挤压的嵌套容器,一个大小调整的容器 竞态 中,
    // 则不进行任何投放或中止。
    for (const auto& doc : adversarial_docs()) {
        for (int w : {200, 80, 40, 20, 10, 5, 3, 2, 1, 0, -1, -80}) {
            const std::string out = render_md(doc, w);
            // 合同的宽度下降 "renders nothing" ,
            // 永远 "throws" 完全达到这一界限就是断言。
            CHECK(out.size() < 1u << 22);   // 并且不会爆炸大小
        }
    }
}

TEST_CASE("markdown: content is never silently dropped on a narrow surface") {
    // 回归:a 2 -column 装饰缩进 吃了整个内容区域,
    // 所以宽度 QQ 2 已实现 空。任何能够至少持有
    // 一个字符的宽度必须显示字符。
    for (const auto& doc : adversarial_docs()) {
        for (int w : {40, 10, 5, 3, 2, 1}) {
            const std::string out = render_md(doc, w);
            check(visible_chars(out) > 0,
                  "width " + std::to_string(w) + " must render SOME content");
        }
    }
}

TEST_CASE("markdown: a pseudo-tag does not swallow the markdown after it") {
    // <shell> 不是,是"我们",不是"我们" HTML。
    // 严格地说 CommonMark 7型,它会打开一个生的 HTML 块,
    // 将所有东西都吃掉,然后带入下一条空白行。 maya 故意的歧义:未知的标记是文本。
    const std::string doc =
        "<shell>\n- first bullet\n- second bullet\n\nclosing prose";
    const std::string out = render_md(doc, 80);
    check(out.find("first bullet")  != std::string::npos,
          "list under a pseudo-tag survives");
    check(out.find("second bullet") != std::string::npos,
          "the whole list survives, not just its head");
    check(out.find("closing prose") != std::string::npos,
          "prose after the pseudo-tag survives");
    check(out.find("<shell>") != std::string::npos,
          "the pseudo-tag itself renders as literal text");
}

// ── 以-body(内含)标记 ──────────────────────────────────────────
// 电话 BLOCK 路径门未激活 -HTML 块打开 html::is_known_tag ,
// 所以从 <shell> 继续传播。内线标记采用不同的路线( cm_inline's
// scan_html_tag ,没有这样的门),有一个标签,
// 只是 LOOKS 喜欢 HTML 曾经被消耗为标记: 一种样式标记不包含文字,
// 所以 "Set <var> to 3" 已交付 "Set  to 3".
// 只有名字与真实相撞 HTML 内容受到影响, 这正是一个模型的设定 点击,
// 当它谈论标记或回声 agentty 属于自己的 提示词 标记( 3)
// <environment>, <output>, <summary>, <dir>).
// 合同:只有在BALANCED时才有标记。
TEST_CASE("markdown: an unbalanced in-body tag is never swallowed") {
    // 与真正的HTML元素相撞的名称——回归集.
    for (const char* name : {"var", "cite", "code", "kbd", "q", "div", "p",
                             "span", "font", "summary", "dir", "output",
                             "data", "object", "b", "i", "em", "mark"}) {
        const std::string doc =
            std::string("Set <") + name + "> to three.";
        const std::string out = render_md(doc, 70);
        INFO("tag <", name, "> rendered: ", out);
        CHECK_MESSAGE(out.find(std::string("<") + name + ">") != std::string::npos,
                      "a lone opening tag must survive as literal text");
        CHECK_MESSAGE(out.find("three") != std::string::npos,
                      "surrounding prose must survive");

        // 一个偏差的CLOSE标签属于没有范围,不能自行删除.
        const std::string cdoc =
            std::string("Set </") + name + "> to three.";
        const std::string cout_ = render_md(cdoc, 70);
        INFO("close </", name, "> rendered: ", cout_);
        CHECK(cout_.find(std::string("</") + name + ">") != std::string::npos);
    }

    // 修道士( 修道) -tags 没有 HTML 双胞胎已经很好; 锁 也让他们进来
    for (const char* name : {"shell", "thinking", "environment", "cwd",
                             "tool_call", "shell-notes", "memory-tools"}) {
        const std::string doc = std::string("Use the <") + name + "> tool.";
        const std::string out = render_md(doc, 70);
        INFO("pseudo <", name, "> rendered: ", out);
        CHECK(out.find(std::string("<") + name + ">") != std::string::npos);
    }
}

TEST_CASE("markdown: balanced in-body HTML still styles rather than printing") {
    // 合同的另一半——定本不能真正内在化 HTML 成字标记汤。
    // 配对的配对被消耗为标记及其标记。
    // CONTENT 活下来了
    struct Case { const char* doc; const char* keep; };
    for (auto c : {
            Case{"Set <b>bold</b> here.", "bold"},
            Case{"Set <em>emph</em> here.", "emph"},
            Case{"Set <var>x</var> here.", "x"},
            Case{"A <span style=\"color:red\">red</span> word.", "red"},
            Case{"A <cite>source</cite> here.", "source"},
         }) {
        const std::string out = render_md(c.doc, 70);
        INFO("doc: ", c.doc, " rendered: ", out);
        CHECK_MESSAGE(out.find(c.keep) != std::string::npos,
                      "content between a matched pair must render");
        CHECK_MESSAGE(out.find("</") == std::string::npos,
                      "a balanced pair must not print its own closing tag");
    }
}

TEST_CASE("markdown: an unclosed inline tag does not bleed across blocks") {
    // 一个不平衡的标记现在保持文字化,所以它也绝不能留下
    // 一个样式范围打开,以给后面段落配色.
    const std::string doc =
        "Para one has an unclosed <b>bold tag.\n\n"
        "Para two is plain.\n\n"
        "## Heading\n";
    const std::string out = render_md(doc, 70);
    CHECK(out.find("Para two is plain.") != std::string::npos);
    CHECK(out.find("<b>") != std::string::npos);
    CHECK(out.find("Heading") != std::string::npos);
}

TEST_CASE("markdown: in-body tags stream to the same result as one-shot") {
    for (const char* doc : {"Use the <shell> tool now.",
                            "Mixed <shell> and <b>bold</b> here.",
                            "Set <var> to 3.",
                            "A <div> mid-sentence."}) {
        auto w = std::make_shared<maya::StreamingMarkdown>();
        w->set_live(true);
        w->set_reveal_fx(false);
        w->set_reveal_decorate(false);
        for (char ch : std::string(doc)) w->append(std::string(1, ch));
        w->finish();
        const std::string streamed = maya::render_to_string(w->build(), 70);
        const std::string oneshot  = render_md(doc, 70);
        INFO("doc: ", doc, "\n one-shot: ", oneshot, "\n streamed: ", streamed);
        CHECK(visible_chars(streamed) == visible_chars(oneshot));
    }
}

TEST_CASE("markdown: streaming a doc byte-by-byte never loses the settled text") {
    // 标记拆分 ACROSS 三角洲的边界是 流式传输 危险:活体 解析器
    // 看见了 "<she" 那样 "ll>" 并且不能锁定与已解决的区块
    // 类型不同的区块类型 解析 比较 字节-by-byte 活的建筑到一个
    // -shot 已定居的建筑。
    for (const auto& doc : adversarial_docs()) {
        auto w = std::make_shared<maya::StreamingMarkdown>();
        w->set_live(true);
        w->set_reveal_fx(false);
        w->set_reveal_decorate(false);
        for (char c : doc) {
            w->append(std::string(1, c));
            (void)maya::render_to_string(w->build(), 60);   // 不可丢弃
        }
        w->finish();
        const std::string streamed = maya::render_to_string(w->build(), 60);
        const std::string oneshot  = render_md(doc, 60);
        check(visible_chars(streamed) == visible_chars(oneshot),
              "byte-streamed and one-shot renders settle identically");
    }
}
