// theme_idle_async_parse_test ——现场主题
// 切换必须重新颜色 a SETTLED 不只是屏幕上那些重新油漆的部位
//
// 该 REPORT. 切换主题直播"sometimes doesn't work well",
// 但当其他东西已经动画化(欢迎屏幕,加载动画, 数据流)时,
// 它总是起作用. 条件是: 重绘正确是什么,
// 从每个帧的现场主题位置上, 哪些是REBUILDS,
// 哪些停留的过期是已经承诺的.
//
// 该 MECHANISM。一个承诺的减值块被作为完整存储 RENDERED
// 元素 :
//
//     prefix_blocks.push_back
//         ( std::make_shared<const Element>(md_block_to_element (块))
//
// 财务报告和已审计财务报表 md_block_to_element 解析
// ~58 `colors::` 混凝土颜色的插槽 AT COMMIT
// TIME. render_committed_block_ 然后重放
// 被冻结的元素, 将调色板调色板显示为已解密的信息 渲染 里面有它被
// 执行的文字, 下游 能够改变它,因为链条中没有任何主题名词:
//
//   • set_content/set_content_async 无 -op 不变 字节 ,并设置主题
//     切换不更改信件的 字节。所以没有 -parse ,
//     和 build_dirty_ 保持清醒,别走开
//   • 已定居的快速通道进入 build() 返回时 cached_build_ 直截了当的。
//   • 递增 "prefix grew" / "windowed prefix" 路径再使用
//     每一个不变块的现有儿童元素——经过纯粹的主题改变后,
//     所有这些元素。
//   • render_committed_block_'s hash_id 也没有主题词,所以
//     渲染器's 存储单元 缓存 就会打碎 过期 存储单元 随便你
//
// 结果是不晚 重绘。这是 PERMANENT :没有修复
// 这个测试显示, 3, 4和5 都建得稳定, 都是错误的。
// 记录片将旧方案保存到某事迫使 -parse.
//
// 该 FIX (maya, streaming/build.cpp).
// CommittedPrefix 现在保留每个区块的 PARSED 其元素
// 旁的形状,以及 build() 比较 theme::live_epoch ()
// 在时间的旁边,块块被制成。当调色板移动时,承诺的块是 RE-RENDERED
// 从解析表单( 不重写) -parsed ) 前面的字节被撞到,
// 所以每个 缓存 以上各级通知。
//
// 内容 此 TEST 执行。提交已解决 正文 在一个计划下,
// 发布第二个计划; 渲染 一次。结果必须是 字节-identical
// 以至在第二个方案下承诺的新部件。 帧 没有键盘键,
// 没有 -parse.
//
// 原因 该 EXISTING THEME TESTS MISS IT.
// theme_memo_stale_probe 财务报告和已审计财务报表
// theme_alternating_key_test 两种种子内容 之后 发布主题, 因此它们的块被
// 新调色板下执行, 并且不会出现 Staleness。 错误需要在 $ 下执行的内容。 旧 主题。

#include <cstdio>
#include <string>

#include <maya/maya.hpp>
#include <maya/style/schemes.hpp>
#include <maya/style/theme.hpp>
#include <maya/widget/markdown.hpp>

#include "agentty/domain/ui_theme.hpp"

using namespace agentty;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

// 有个安顿的助理 正文 舒适的结束 async.cpp's
// 16 KB 阈值,带有足够的标记下结构,调色板实际出现在发射
// 的 SGR (头,栅栏,内含码, 链接).
std::string big_body() {
    std::string s;
    s.reserve(48 * 1024);
    s += "# A settled answer\n\n";
    for (int i = 0; i < 260; ++i) {
        s += "## Section ";
        s += std::to_string(i);
        s += "\n\nSome prose with `inline code` and a [link](https://example.com) "
             "that the theme colours differently.\n\n";
        s += "```cpp\nint value_";
        s += std::to_string(i);
        s += " = 42;  // a comment\n```\n\n";
        s += "- a bullet\n- another bullet\n\n";
    }
    return s;
}

// 一个解决了 StreamingMarkdown ,喂得正好的方式, 和我们一样 cached_markdown_for
// 输入已解密的消息 : set_content_async + finish().
std::string render_settled(maya::StreamingMarkdown& md,
                           std::string_view body) {
    md.set_content_async(body);
    md.finish();
    return maya::render_to_string_ansi(md.build(), 100);
}

// 电话 FIX ,形状相同 状态更新函数 适用:已解决 正文 输入 SYNCHRONOUSLY ,
// 所以没有自动窗口和 帧 等待。这个镜子 settle_message_md
// (stream.cpp ),这是什么 restyle_sealed_turns
// 现在 调用 现场直播 末尾.
std::string render_settled_sync(maya::StreamingMarkdown& md,
                                std::string_view body) {
    md.set_content(body);
    md.finish();
    return maya::render_to_string_ansi(md.build(), 100);
}

// 构建到 源码树 停止改变,并返回稳定 渲染.
//
// 解决的部件合法地需要一个 SETTLE-FRAME 重建于
// finish() 还没到最后的状态
// 执行一次 build() 之后的渲染。
// 即使是健康病例,也不是固定点。 -swap 相对于固定点构建会比较两种
// 不同的事物,并报告一个不存在的bug。 -to-fixpoint
// 是一种诚实的对比。
std::string render_to_fixpoint(maya::StreamingMarkdown& md) {
    std::string prev = maya::render_to_string_ansi(md.build(), 100);
    for (int i = 0; i < 8; ++i) {
        std::string next = maya::render_to_string_ansi(md.build(), 100);
        if (next == prev) return next;
        prev = std::move(next);
    }
    return prev;
}

} // 命名空间结束。

int main() {
    // 这些比较画 字节 跨调色板; a NO_COLOR 电话铃声的内脏
    // CI ,代理贝壳,一个控制台,没有 VT )将使每个方案都相同。
    // MAYA_COLOR 谓 maya 已经记录的定型 -test 覆盖( 见
    // detect_color_level).
    ::unsetenv("NO_COLOR");
    ::setenv("MAYA_COLOR", "truecolor", 1);
    std::printf("=== theme_idle_async_parse_test ===\n\n");

    const maya::Theme* dracula = ui_prefs::find_scheme("Dracula");
    const maya::Theme* gruv    = ui_prefs::find_scheme("Gruvbox Light");
    if (!dracula || !gruv) {
        std::printf("could not find both schemes — abort\n");
        return 2;
    }

    const std::string body = big_body();
    std::printf("body = %zu bytes (async threshold is 16384)\n\n", body.size());
    check(body.size() >= 16 * 1024,
          "the body is large enough to take the async parse path");

    // 真实基准：使用相同解析结果，得到各主题应有的外观。
    // 允许和解。每个主题新建一个部件, 新建两次,
    // 任何 待处理 同步结果在我们之前通过 快照.
    // 事实必须来自 SYNC 路径,必须在 相同 构建索引,
    // 我们比较。部件的第一个构建及其固定点在逻辑上是不同的。
    // SGR 字节 (后来将已承诺的块建为缓存的块,
    // 以释放较少的越狱),因此比较一个帖子
    // -swap 首先用固定点来构建 将两个不同的
    // 事物进行比较
    std::printf("ground truth (fresh widget, SYNC parse, FIRST build)\n");
    std::string want_dracula, want_gruv;
    {
        ui_prefs::publish_theme(*dracula);
        maya::StreamingMarkdown md;
        want_dracula = render_settled_sync(md, body);
    }
    {
        ui_prefs::publish_theme(*gruv);
        maya::StreamingMarkdown md;
        want_gruv = render_settled_sync(md, body);
    }
    check(want_dracula != want_gruv,
          "the two schemes DO paint this body differently");
    std::printf("    dracula bytes=%zu  gruvbox bytes=%zu\n\n",
                want_dracula.size(), want_gruv.size());

    // 真实操作：同一个组件已在 Dracula 下稳定，然后
    // 主题被交换, 和 app 渲染 一次。 这正是闲置的主题开关得到的。
    // —— restyle_sealed_turns 发布主题,
    // 下一个 帧 re-feeds 部件, 也没有第二个 帧 来,
    // 因为屏幕上没有任何东西是动画的。
    //
    // 重头戏 -feed 使用该 SYNC 路径,即固定线:
    // restyle_sealed_turns 现在 调用 settle_message_md
    // 过现场 末尾 回来之前,所以 -feed 没有未排定的同步窗口 帧 躲起来
    std::printf("idle switch (one widget, one render after the swap)\n");

    // A FRESH 德古拉公司下制造的部件,在第一建筑时取样,
    // 因此可以直接与 want_dracula.
    ui_prefs::publish_theme(*dracula);
    maya::StreamingMarkdown md;
    const std::string under_dracula = render_settled_sync(md, body);
    check(under_dracula == want_dracula,
          "the settled body matches the Dracula reference");

    // 该 SWAP。该词 字节 做吗 不 更改——主题切换从不触及已
    // 解决的消息文本——所以 set_content 无 -ops ,
    // 没有再 -parsed ,每个来源 -keyed 脏 标志 保持清醒,
    // 我们得去 仅 移动的是调色板,这正是闲置的姿态
    //
    // 调 -feeding 相同的数据 字节 应用是做什么的 帧 之后
    // invalidate_colours(); it is a no-op inside the widget, which is the
    // 整个观点——重彩必须来自主题时代,
    // 而不是来自饲料.
    ui_prefs::publish_theme(*gruv);
    const std::string after_one_frame = render_settled_sync(md, body);

    check(after_one_frame != under_dracula,
          "ONE render after the swap already changed the screen");
    check(after_one_frame == want_gruv,
          "and it shows the NEW palette, not the outgoing one");

    // 诊断：内容虽已变化，却不同于真实基准，可能有两类
    // 不同的原因,需要分开:
    //
    //   (a) UNSCHEDULED - 自动自动 解析 还没有被收养 所以
    //       这个 帧 是一个 PARTIAL/previous 源码树 秒后进行
    //       build() (无) 帧 动画可以免费提供),然后匹配.
    //
    //   (b) 良性带状差异——再利用部件与新鲜部件有
    //       合理差异(第2段) 归并 状态, 前缀 快照 ),
    //       尽管调色板是正确的.
    //
    // 驱动额外的build(),看看它是否在地面上合拢的真相.
    if (after_one_frame != want_gruv) {
        std::printf("\n  diagnosing the mismatch\n");
        auto first_diff = [](const std::string& a, const std::string& b) {
            std::size_t i = 0;
            while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
            return i;
        };
        const std::size_t d = first_diff(after_one_frame, want_gruv);
        std::printf("    build#1 len=%zu  want len=%zu  first diff at %zu\n",
                    after_one_frame.size(), want_gruv.size(), d);
        auto show = [&](const char* tag, const std::string& s) {
            std::string frag = s.substr(d > 20 ? d - 20 : 0,
                                        60);
            std::string esc;
            for (char c : frag) {
                if (c == '\x1b') esc += "\\e";
                else if (c == '\n') esc += "\\n";
                else esc += c;
            }
            std::printf("    %-8s ...%s...\n", tag, esc.c_str());
        };
        show("build#1", after_one_frame);
        show("want", want_gruv);

        std::string prev = after_one_frame;
        for (int i = 2; i <= 5; ++i) {
            const std::string again =
                maya::render_to_string_ansi(md.build(), 100);
            std::printf("    build #%d: %s ground truth, %s previous\n", i,
                        again == want_gruv   ? "==" : "!=",
                        again == prev        ? "==" : "!=");
            if (again == want_gruv) {
                std::printf("\n  => converged on build #%d — the recolour is\n"
                            "     LATE rather than missing.\n", i);
                break;
            }
            prev = again;
        }
        if (prev != want_gruv)
            std::printf("\n  => never converges. The committed blocks are\n"
                        "     frozen in the OUTGOING palette: they were\n"
                        "     rendered at commit time and nothing re-renders\n"
                        "     them, so no number of frames can fix it.\n");
    }

    // 回归保护：在异步输入路径执行相同操作，
    // 一个解密的消息实际上收录 agentty (turn.cpp 已
    // 定居的路线 正文 结束 set_content_async 同样
    // 的期望——色调不得取决于哪一种 解析 路径 正文 被带走了
    std::printf("\nregression guard (async feed path)\n");
    {
        ui_prefs::publish_theme(*dracula);
        maya::StreamingMarkdown md2;
        render_settled(md2, body);
        const std::string a = render_to_fixpoint(md2);
        ui_prefs::publish_theme(*gruv);
        const std::string b = render_to_fixpoint(md2);
        check(b != a, "the async-fed body recolours too");
    }

    std::printf("\n");
    if (failures == 0) {
        std::printf("PASS — a live theme switch recolours a settled body\n");
        return 0;
    }
    std::printf("FAILED: %d check(s) — settled body kept the old palette\n",
                failures);
    return 1;
}
