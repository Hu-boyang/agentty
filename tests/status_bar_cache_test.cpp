// 状态栏的活动行被缓存在一个内容密钥上.
//
// 这条线是一个测量的降解梯级:它构建每个子 -widget 真正
// 有风格的碎片 MEASURES 最多8个候选形状,来挑选最富有的
// 适合选手。这是排队的正确方法,也是每一次重做一个可怜的东西。
// 帧 ——剖析直播. 流式传输 届会已
// StatusBar::activity_row 53%的失业人口 全部 渲染 时间,
// 因为梯子跑~30次一秒,无论它中的任何事物是否改变。
//
// 所以,东道主声明了该行的身份和 maya 缓存 下方
// 的铁板板 危险 缓存 像这样不是慢,它是 STALENESS
// : 缺少输入的密钥会描绘一个状态栏,它悄悄地停止了
// 对它所要报告的东西的跟踪。这些案例都将双倍地标上,
// 即内容移动时的关键动作,而内容不移动时则不移动。

#include "agtest.hpp"

#include "agentty/runtime/model.hpp"
#include "agentty/runtime/view/status_bar/status_bar.hpp"

#include <maya/print.hpp>

#include <string>

namespace {

using agentty::Model;

[[nodiscard]] std::uint64_t key_of(const Model& m) {
    return agentty::ui::status_bar_config(m).content_key;
}

// 有个有似是而非的模拟器 转弯,所以梯子读的字段都是没有的 -default .
// 按价值计算返回 -- -- Model 正在移动 -only ,
// 因此每个案例都会建立自己的,而不是复制一个共享的基数.
[[nodiscard]] Model streaming_model() {
    Model m;
    m.s.phase = agentty::phase::Streaming{};
    return m;
}

}  // 命名空间结束。

TEST_CASE("status bar: the row carries a content key") {
    // 选择 -in 缓存: 0 表示 "rebuild every frame".
    // agentty 选择加入,所以这里的零键会默默恢复53%的成本,没有其他症状.
    const Model m = streaming_model();
    CHECK(key_of(m) != 0);
}

TEST_CASE("status bar: the key is stable for unchanged content") {
    // 整个点。 由同一模型构建的两个配置必须一致,
    // 否则 缓存 永远不会命中, 而密钥是纯高的。
    const Model m = streaming_model();
    CHECK(key_of(m) == key_of(m));

    const Model same = streaming_model();
    CHECK(key_of(same) == key_of(m));
}

TEST_CASE("status bar: the key moves when the row's content moves") {
    // 每个都是梯级读取的输入和渲染.
    // 如果键忽略一个,则行会继续绘制以前的值——
    // 这个测试的失败是无法实现的.
    const std::uint64_t k0 = key_of(streaming_model());

    {   // 相位驱动动词、格言和重音颜色
        Model m = streaming_model();
        m.s.phase = agentty::phase::Idle{};
        CHECK(key_of(m) != k0);
    }
    {   // 状态横幅 TAKES OVER 完全的插槽
        Model m = streaming_model();
        m.s.status = "saved";   // 无截止时间的 NO-empty = 活动
        CHECK(key_of(m) != k0);
    }
}

TEST_CASE("status bar: a cached row never paints stale content") {
    // End 结束于 渲染器 而不是单通过关键:
    // 不同内容必须产生不同的像素,即使有缓冲,
    // 同样内容也必须可以复制。
    auto render_with = [](const char* verb, std::uint64_t key) {
        maya::StatusBar::Config c;
        c.phase.verb = verb;
        c.phase.glyph = "*";
        c.phase.elapsed_secs = 1.0f;
        c.context.used = 100;
        c.context.max = 1000;
        c.context.cells = 10;
        c.content_key = key;
        return maya::render_to_string(maya::StatusBar{c}.build(), 120);
    };

    const std::string a = render_with("Thinking", 111);
    const std::string b = render_with("Running",  222);
    const std::string c = render_with("Thinking", 111);

    CHECK(a != b);                                   // 否 过期 数据
    CHECK(a == c);                                   // 和决定性
    CHECK(a.find("Thinking") != std::string::npos);
    CHECK(b.find("Running")  != std::string::npos);
}
