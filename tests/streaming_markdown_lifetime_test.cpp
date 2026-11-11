// streaming_markdown_lifetime_test ——解决. 源码树 必须是超过其部件的寿命。
//
// REGRESSION (maya 54ad00d − 固定号: StreamingMarkdown::build
// (a) 将儿童包裹在 component() 抓获的羊肉 `this` 从而可以
// 将布局宽度记录到 `last_paint_width_` 成员 逐步显示'
// s line_bounded 夹口需要真正的包宽度).
//
// 但是羊肉会跑到 LAYOUT 时间,而不是构建时间
// —— 和元素 持有它被明确设计为超过部件:
// settled_element ( ) 手拿定案 源码树
// 以主机形式 shared_ptr<const Element> 这么冷
// 回滚缓冲区 它可以保持它 堆,和 渲染器 缓存 横跨 帧。所以每个隐藏的
// 布局 源码树 写 4 字节 变成自由 内存.
//
// 瓦尔格林德给它取了个确切的名字:
//   大小 4 的写法无效
//     maya::StreamingMarkdown::build (:{lambda(int,
//   int)}}: operator() 地址在3168码的区块内
//     maya::StreamingMarkdown::~StreamingMarkdown()
//
// 腐败远远没有达到目的—— bad_array_new_length 或 或 时 间
// SIGSEGV 在一个不相关的元素内复制若干 帧 之后,这就是我们做的 agentty
// 显示 "crash when loading a thread".
//
// 此测试直接复制了 所有权 模式: 构建 + 结算部件,
// 隐藏其 settled_element( ), DESTROY 部件,
// 然后将隐藏的 源码树 放置出来。 在 ASan/valgrind 下,
// 旧代码行走这里; 没有消毒器, 它仍然在运行路径, 宽度 存储单元
// 必须保持可读性和正确性。

#include <maya/print.hpp>
#include <maya/render/renderer.hpp>
#include <maya/widget/markdown.hpp>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL: %s\n", what); ++failures; }
}

// A 正文 足够我们铺满宽度的宽度 和几个街区的宽度
// 所以定居 源码树 有了真正的孩子可以走路。
std::string sample_body() {
    return "# Heading\n\n"
           "The quick brown fox jumps over the lazy dog, and keeps on "
           "jumping until the line has to wrap at any sensible width.\n\n"
           "- one item that is also fairly long so it wraps too\n"
           "- another item\n\n"
           "Closing paragraph with enough text to occupy several rows.\n";
}

} // 命名空间结束。

int main() {
    // ── 1. 定居的源码树必须在其部件的破坏中生存 ────────
    {
        std::shared_ptr<const maya::Element> stashed;
        {
            maya::StreamingMarkdown md;
            md.set_content(sample_body());
            md.finish();
            stashed = md.settled_element();
            check(stashed != nullptr, "settled_element() returns a tree");
            // 准备 一次 部件还活着时——这是宽度的质数
            // 存储单元 羊肉会写信的
            (void)maya::render_to_string(*stashed, 80);
        }   // md在这里被摧毁;`stashed`故意超过它.

        // 藏起来的 源码树 现为主机( 冻结) 回滚缓冲区 's) 改为 渲染.
        // 刻录它不能触碰已死的部件。
        // 这是在修复前写入 内存 的精确操作。
        const std::string a = maya::render_to_string(*stashed, 80);
        check(!a.empty(), "settled tree renders after the widget is gone");

        // 宽度为DIFFERENT的Re-render:每个都再次
        // 引用布局羊肉达,所以每个都通过(现在的)抓取来写另一个.
        for (int w : {40, 100, 24, 120}) {
            const std::string out = maya::render_to_string(*stashed, w);
            check(!out.empty(), "settled tree renders at every width");
        }
    }

    // ── 2. 数处藏匿 源码树 ,所有活在它们的部件 ────────────
    // 冻 回滚缓冲区 将许多这些组合同时保存; 将组合式相互
    // 连接是现实的形状, 并捕获每枚 -instance 存储
    // 单元 意外地分享了这些
    {
        std::vector<std::shared_ptr<const maya::Element>> stash;
        for (int i = 0; i < 8; ++i) {
            maya::StreamingMarkdown md;
            md.set_content("Turn " + std::to_string(i) + "\n\n" + sample_body());
            md.finish();
            stash.push_back(md.settled_element());
        }
        for (int pass = 0; pass < 3; ++pass)
            for (const auto& el : stash) {
                check(el != nullptr, "stashed element non-null");
                const std::string out = maya::render_to_string(*el, 60 + pass * 20);
                check(!out.empty(), "each stashed tree renders independently");
            }
    }

    // ── 3. A LIVE (未解决) 部件重新 -lays 安全地从许多宽度外移 ───
    // 宽度 存储单元 已存在,用于服务 逐步显示 夹子,
    // 是私人的,所以我们不接触它,而是使用写它的道路——一个不断在
    // 宽度变化时铺设的活的部件,这就是在它被修改时的大小。
    // 流式传输 ——并要求它保持一致性并坠毁 -free .
    // (在Valgrind下,这是一个正在被捕获的捕捉器报告的地方;
    // 值本身由以下内容涵盖: 逐步显示 自己的测试。 )
    {
        maya::StreamingMarkdown md;
        md.set_live(true);
        md.set_content(sample_body());
        for (int w : {80, 50, 120, 32, 96}) {
            const std::string out = maya::render_to_string(md.build(), w);
            check(!out.empty(), "live widget re-lays out at a new width");
        }
        // 把它安顿下来,藏起来,放下部件,然后再放出来: 活的
        // —沉的—孤的序列是真正的冰冻的-scrollback路径.
        md.finish();
        auto settled = md.settled_element();
        check(settled != nullptr, "settled after a live phase");
        const std::string before = maya::render_to_string(*settled, 80);
        {
            maya::StreamingMarkdown doomed;
            doomed.set_content(sample_body());
            doomed.finish();
            auto other = doomed.settled_element();
            (void)maya::render_to_string(*other, 80);
        }
        const std::string after = maya::render_to_string(*settled, 80);
        check(before == after,
              "a stashed tree renders identically after an unrelated "
              "widget is created and destroyed");
    }

    if (failures == 0) std::printf("PASS\n");
    return failures == 0 ? 0 : 1;
}
