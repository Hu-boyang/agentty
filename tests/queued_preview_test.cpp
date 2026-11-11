// queued_preview_test ——一个排队的消息说 WHEN 将送去。
//
// 中转 -task 队列是用来:在代理工作时键入,然后在转弯结束的时刻发出消息。
// 提示曾经是 `❚ N queued` 数和记录一行 看上去就像一个发送的消息,
// 如此紧迫 Enter 中点 -turn 读为 "it sent" 或 或
// 时 间 "nothing happened" 而不是说
// 轮次结束时触发。
//
// 透过真实的视图渲染,因为索赔涉及的是到达屏幕的内容;
// 一个源
// -text断言无法告诉你该线实际上可见.

#include "agtest.hpp"

#include "agentty/runtime/model.hpp"
#include "agentty/runtime/view/thread/conversation.hpp"
#include "agentty/runtime/view/composer.hpp"

#include <maya/render/canvas.hpp>
#include <maya/render/renderer.hpp>
#include <maya/style/theme.hpp>

#include <string>

using namespace agentty;

namespace {

// 无 -ASCII 归并 改为空格: 测试中的行是 ASCII
// 环绕的铬不是人们所主张的
std::string render(const Model& m, int width = 100) {
    auto root = maya::Conversation{ui::conversation_config(m)}.build();
    maya::StylePool pool;
    maya::Canvas canvas(width, 60, &pool);
    canvas.clear();
    maya::render_tree(root, canvas, pool, maya::theme::native, true);
    std::string s;
    for (int y = 0; y <= canvas.max_content_row(); ++y) {
        for (int x = 0; x < width; ++x) {
            char32_t ch = canvas.get(x, y).character;
            s.push_back(ch && ch < 128 ? static_cast<char>(ch) : ' ');
        }
        s.push_back('\n');
    }
    return s;
}

Model with_queued(std::string text) {
    Model m;
    ComposerState::QueuedMessage q;
    q.text = std::move(text);
    m.ui.composer.queued.push_back(std::move(q));
    return m;
}

}  // 命名空间结束。

TEST_CASE("queued preview: mid-turn, it says when it sends") {
    Model m = with_queued("actually, use the other approach");
    m.s.phase = phase::Streaming{};
    REQUIRE(m.s.active());

    const std::string s = render(m);
    INFO(s);
    CHECK(s.find("queued #1") != std::string::npos);
    CHECK(s.find("sends when this turn ends") != std::string::npos);
}

TEST_CASE("queued preview: idle, it does not promise a turn that isn't running") {
    // 什么都没有 流式传输 ,所以没有 "this turn"
    // 以结束。 排队排在下一个提交时会排出排队。 否则的话会
    // 是一个谎言, 有时错误的提示比没有提示更糟糕。
    Model m = with_queued("later");
    REQUIRE(!m.s.active());

    const std::string s = render(m);
    INFO(s);
    CHECK(s.find("queued #1") != std::string::npos);
    CHECK(s.find("sends when this turn ends") == std::string::npos);
}

TEST_CASE("composer footer: typing mid-turn says Enter will queue") {
    // 教书的占位符 "type to queue" 已经走了,
    // 那一刻 IS 文本 - 确切的用户决定是否按 Enter.
    // 脚印从那时起就携带它,在模范徽章旁边
    // 而不是代替它.
    auto footer = [](const Model& m, int width = 100) {
        auto root = maya::Composer{ui::composer_config(m)}.build();
        maya::StylePool pool;
        maya::Canvas canvas(width, 30, &pool);
        canvas.clear();
        maya::render_tree(root, canvas, pool, maya::theme::native, true);
        std::string s;
        for (int y = 0; y <= canvas.max_content_row(); ++y) {
            for (int x = 0; x < width; ++x) {
                char32_t ch = canvas.get(x, y).character;
                s.push_back(ch && ch < 128 ? static_cast<char>(ch) : ' ');
            }
            s.push_back('\n');
        }
        return s;
    };

    {   // 中-turn 有文本:简单说,长的形态存在于
        // 记录排;脚印是环境的,已经挤满了。
        Model m;
        m.ui.composer.text = "wait, do it the other way";
        m.ui.composer.cursor = static_cast<int>(m.ui.composer.text.size());
        m.s.phase = phase::Streaming{};
        const std::string s = footer(m);
        INFO(s);
        CHECK(s.find("queues") != std::string::npos);
    }
    {   // 与文本闲置: Enter 立即发送, 所以提示不能
        //  显示 - 它将描述一个没有发生的队列。
        Model m;
        m.ui.composer.text = "a normal message";
        m.ui.composer.cursor = static_cast<int>(m.ui.composer.text.size());
        REQUIRE(!m.s.active());
        const std::string s = footer(m);
        INFO(s);
        CHECK(s.find("queues") == std::string::npos);
    }
}

TEST_CASE("queued preview: the promise is made once, not per row") {
    // 三个排队的项目声明一次,每行重复一次都是在抱怨,
    // 而各行相邻,所以一行都覆盖了它们.
    Model m = with_queued("first");
    for (const char* t : {"second", "third"}) {
        ComposerState::QueuedMessage q;
        q.text = t;
        m.ui.composer.queued.push_back(std::move(q));
    }
    m.s.phase = phase::Streaming{};

    const std::string s = render(m);
    INFO(s);
    CHECK(s.find("queued #1") != std::string::npos);
    CHECK(s.find("queued #3") != std::string::npos);
    const auto first = s.find("sends when this turn ends");
    REQUIRE(first != std::string::npos);
    CHECK(s.find("sends when this turn ends", first + 1) == std::string::npos);
}
