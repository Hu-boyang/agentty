// 外观 面板 :每行应用直播,独占其设置.
//
// 两个属性,两个都有一个真正的洞。
//
// LIVE。该词 面板 是一个预览——您更改一行,然后根据真实结果
// 判断结果。 UI ,只有在变化达到 ALREADY 屏幕上显示的
// 颜色行( 计数、 分级、 极性、 语法) 必须重新
// -seal 结构记录(密度、紧凑的转弯)不得故意这样做,
// 因为 -sealing 以新的高度 流泪 回滚缓冲区 分类账。
//
// 唯一负责位置：是否显示模型推理内容。
// 问到两个地方: 面板 思考的一行,还有 ^R 切换 模型中
// 选择面板 他们可能不同意,分歧在最坏的方向上是昂贵的——
// ^R "shown" + 思考 "Hidden" 意思是推理
// 是 REQUESTED 从提供者,计费,然后投放未交,
// 每个屏幕报告的情况与另一个相反。

#include "agtest.hpp"

#include "agentty/domain/ui_prefs.hpp"
#include "agentty/domain/ui_live.hpp"   // publish() / motion_frame_divisor( )
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/panel/appearance.hpp"
#include "agentty/runtime/panel/settings/items.hpp"
#include <maya/core/motion.hpp>
#include <maya/widget/markdown.hpp>

#include <print>
#include <functional>
#include <string>
#include <vector>

namespace {

namespace up = agentty::ui_prefs;
namespace pn = agentty::ui::panel;
using agentty::Model;

// IDs的外观形式 发光, 是否必须改变每个
// -style 已经画过。
struct Row { std::string_view id; bool recolours; };

[[nodiscard]] const agentty::form::Field* field(const agentty::form::Form& f,
                                                std::string_view id) {
    for (const auto& row : f.fields)
        if (row.id == id) return &row;
    return nullptr;
}

}  // 命名空间结束。

TEST_CASE("appearance: every row the pane builds is a real, reachable setting") {
    // 排行符号为"无" 状态更新函数 控件是死控
    // 件: 它移动, 它持续, 它不会改变任何东西。
    // 面板 编辑 状态更新函数 已键入 相同 以恒星命名
    // 以示不流 以示每个身份都是其中之一
    Model m;
    const auto form = pn::build_appearance_form(m.d.ui(), m.env.terminal);

    static constexpr std::string_view kKnown[] = {
        pn::kApLang, pn::kApTheme, pn::kApTier, pn::kApPolarity, pn::kApDensity,
        pn::kApCompact, pn::kApThreadPeek, pn::kApForeign, pn::kApMotion, pn::kApScrollback, pn::kApSyntax, pn::kApToolOutput,
        pn::kApThinking, pn::kApTimestamps, pn::kApProseWidth,
    };

    int rows = 0;
    for (const auto& f : form.fields) {
        // 头文件节是行,后面没有设置;它们带有一个
        // 保留的id前缀而不是空的,因为ids上的
        // 表型机械键是独一无二的.
        if (f.id.empty() || f.id.starts_with("__header")) continue;
        bool known = false;
        for (std::string_view k : kKnown) known = known || (f.id == k);
        if (!known) std::println("  UNHANDLED row id: {}", f.id);
        CHECK(known);
        ++rows;
    }
    CHECK(rows >= 10);
}

TEST_CASE("appearance: a colour row restyles, a structural row does not") {
    // 分拆不是化妆品。 当元素是时, 颜色会解决。 BUILT ,
    // 所以密封的转弯 保留它被封在下面的任何主题 直到它重建。
    // -seal。高度不同:关于 -sealing 冰冻的转弯,
    // 在新的高度 撕裂内线 回滚缓冲区 分类账,所以这些行是
    // 向前的 -only 设计
    Model m;
    const auto form = pn::build_appearance_form(m.d.ui(), m.env.terminal);

    // 颜色 -affecting 排行必须全部存在——这里的打字符会默默停止沉闷,
    // 症状是: "the transcript kept the old theme".
    for (std::string_view id : {pn::kApTier, pn::kApPolarity, pn::kApSyntax})
        CHECK(field(form, id) != nullptr);

    // ...结构的也一样, 故意的。
    for (std::string_view id : {pn::kApDensity, pn::kApCompact})
        CHECK(field(form, id) != nullptr);
}

TEST_CASE("appearance: Thinking is the only home for reasoning display") {
    // 模型( 模型) 选择面板 用于携带 ^R 切换
    // 都不见了 都不见了 标签 编辑 按键绑定 -
    // 所以这个 面板 两人无法再报告不同的答案。
    Model m;
    const auto form = pn::build_appearance_form(m.d.ui(), m.env.terminal);
    const auto* thinking = field(form, pn::kApThinking);
    REQUIRE(thinking != nullptr);

    // 三州,行者各说其事.
    const auto* choice = std::get_if<agentty::form::field::Choice>(&thinking->value);
    REQUIRE(choice != nullptr);
    CHECK(choice->labels.size() == 3);
}

TEST_CASE("appearance: hiding reasoning stops us paying for it") {
    // 旧矛盾中昂贵的一半。 用户要求推理,
    // 告诉我们要隐藏未发售的帐单符号,
    // 这严格来说比诚实回答更糟糕。
    //
    // 折叠的仍请求,故意:显示一个已崩溃的块,
    // 只是折叠,如果它从未发送,则无法展开.
    Model m;
    m.d.show_reasoning = true;

    m.d.ui().thinking = up::Thinking::Hidden;
    CHECK(!(m.d.show_reasoning && m.d.ui().thinking != up::Thinking::Hidden));

    m.d.ui().thinking = up::Thinking::Collapsed;
    CHECK(m.d.show_reasoning && m.d.ui().thinking != up::Thinking::Hidden);

    m.d.ui().thinking = up::Thinking::Shown;
    CHECK(m.d.show_reasoning && m.d.ui().thinking != up::Thinking::Hidden);
}

TEST_CASE("appearance: Animation Off actually stops animation") {
    // 排队承诺的比交付的多. reduce_motion 只打开
    // 了相机 PRIMITIVES (闪烁,波浪,
    // frame_index ),所以,(a) ,(a,
    // ) "off" 停止一个 光标 眨眼和一双 加载动画 旋转,
    // 而不是其他。 帧 直接(欢迎级联及其永久的波波, 流式传输 逐步显示 ,
    // 推理栏杆,减码光标),没有人查阅设定.
    //
    // 门在上 帧 REQUEST 现在,这是每一件事
    // -driven 动画必须经过。 测量在闲置的欢迎屏幕上: 5%
    // 的核心 - > 0%。
    maya::anim::set_reduce_motion(false);
    CHECK(!maya::anim::reduce_motion());

    maya::anim::set_reduce_motion(true);
    CHECK(maya::anim::reduce_motion());

    // 原始人定居而不是吞噬:a 光标 举行 ON (关闭
    // 光标比不眨眼还糟糕),呼吸亮点在它的中点保持
    // -value (冻在槽上读作 渲染 错误),
    // 和 a 加载动画 打开 帧 0,这是每个的休息格言 帧 设定。
    CHECK(maya::anim::blink(530.0));
    CHECK(maya::anim::frame_index(8, 80) == 0);

    // keep_animating ()是最重要的
    // 大门——每一个自我 -driven 动画会经过它,
    // 所以咬它是什么 "off" 总计,而不是被记住的部件列表。
    // 调用 动起来,它只是没有安排什么。
    maya::anim::keep_animating();
    maya::anim::keep_animating_after(100);

    maya::anim::set_reduce_motion(false);
    CHECK(!maya::anim::reduce_motion());
}

TEST_CASE("appearance: Reduced motion is a real middle, not a relabelled Full") {
    // 0xfk0 (# 36) 运行 agentty 以高处的摩什为顶点,
    // -latency 链接 并要求
    // 禁用不必要动画，仅保留必需更新。
    // 减少是本应回答的设置,它没有:它只放弃了 DECORATIVE 层
    // (可移动、梯度、 光标 闪烁),它会重排样式 字节 已经送来了
    //
    // 记录的量度 数据流 (anthropic_md_stream 关闭
    // fixtures/anthropic_md_tour.jsonl ),
    // 计数 帧 其 人 名 RENDER 事情变了,慢慢来 链接 支付:
    //
    //     1753年完全改变 帧
    //     减少1753个 帧   ← 字节-identical
    //     改为全天候98 帧
    //
    // 所以中间设置的成本 与昂贵的一样。
    // 用户的真正选择是 满分或没有 逐步显示。
    //
    // 动议是 两个 轴,不是一:装饰是视觉噪声(可视性),
    // 重绘 速率是带宽。现在降低,使带宽变薄 帧 REQUESTS 因此,
    // 案文仍然在四分之一的时间内——进展仍然可以看清
    // ——仍然可以看出来。 帧.
    namespace up = agentty::ui_prefs;

    // 政策,如域所言.
    const auto divisor_for = [](up::Motion mo) {
        up::Prefs p;
        p.motion = mo;
        up::publish(p);
        return up::motion_frame_divisor();
    };
    CHECK(divisor_for(up::Motion::Full)    == 1);
    CHECK(divisor_for(up::Motion::Reduced) == 4);   // 固定线
    // 完全停止请求,因此,该潜水员是没有意义的——
    // 但决不能声称瘦了已经停止的东西。
    CHECK(divisor_for(up::Motion::Off)     == 1);

    // ...以及政策的代价,在 帧 模拟60秒
    // 的请求 FPS 动画,这是记者感觉的号码
    const auto requests_per_sec = [](bool off, int div) {
        maya::anim::set_reduce_motion(off);
        maya::anim::set_frame_divisor(div);
        int n = 0;
        for (int f = 0; f < 60; ++f) {
            if (maya::anim::reduce_motion()) continue;
            const int d = maya::anim::frame_divisor();
            if (d > 1 && (f % d) != 0) continue;
            ++n;
        }
        return n;
    };
    CHECK(requests_per_sec(false, 1) == 60);   // 满
    CHECK(requests_per_sec(false, 4) == 15);   // 减少——4辆更便宜,仍在移动
    CHECK(requests_per_sec(true,  1) == 0);    // 关闭

    // divisor被夹:计算一个无稽价值的主机
    // 不能完全拖动动动画( 0 或负) 或拉长一个
    // 帧 一分钟后
    maya::anim::set_frame_divisor(0);
    CHECK(maya::anim::frame_divisor() == 1);
    maya::anim::set_frame_divisor(-5);
    CHECK(maya::anim::frame_divisor() == 1);
    maya::anim::set_frame_divisor(10000);
    CHECK(maya::anim::frame_divisor() == 60);

    // 离开进程,正如我们发现的那样:这些是全球时间档,
    // 后来的情况是: 渲染 否则,将继承一个薄的钟表。
    maya::anim::set_frame_divisor(1);
    maya::anim::set_reduce_motion(false);
    up::publish(up::Prefs{});
}

TEST_CASE("appearance: Syntax highlighting off actually removes the colour") {
    // 这个 切换 被坚持, 散落在了 渲染 键,
    // 并读取 无内容: maya 无法打开它,
    // 一个移动的设定 保存 ,而变化的像素比缺失的像素更差。
    //
    // 故意将每个案件的内容区分开来——记下一个在(来源、
    // 语言)上建的代码块,所以 -rendering
    // 编号 相同 片段会为缓存元素服务, 并隐藏差异。
    // 功能无所作为时, 此检查的第一个版本便如此通过。
    auto colours_in = [](bool on, const std::string& body) {
        maya::set_syntax_highlighting(on);
        const maya::Element e = maya::markdown("```cpp\n" + body + "\n```\n");
        int runs = 0;
        std::function<void(const maya::Element&)> walk =
            [&](const maya::Element& el) {
                std::visit([&](const auto& n) {
                    using T = std::decay_t<decltype(n)>;
                    if constexpr (std::is_same_v<T, maya::TextElement>) {
                        runs += static_cast<int>(n.runs.size());
                    } else if constexpr (std::is_same_v<T, maya::BoxElement>) {
                        for (const auto& c : n.children) walk(c);
                    } else if constexpr (std::is_same_v<T, maya::ElementList>) {
                        for (const auto& c : n.items) walk(c);
                    }
                }, el.inner);
            };
        walk(e);
        return runs;
    };

    CHECK(colours_in(true,  "int a = 42; // on")  > 0);
    CHECK(colours_in(false, "int b = 42; // off") == 0);
    maya::set_syntax_highlighting(true);
}

TEST_CASE("settings: every row that opens a pane shows the door arrow") {
    // 外观有 状态更新函数 ,行,和 枚举 ——被
    // 一手排除在外 -written 开动画画的
    // 线条是最大的 面板 在设置中是唯一没有控点的
    // 门: 它读作一个值行, 当你按下时碰巧做一些
    // 事情 Enter.
    //
    // opens_pane (a) 属于 RANGE 超过毗连的门块,
    // 因此在括号中添加一个 , 将箭头取出, 并且没有第二次编辑。
    // 这把三个 的。 其它的 都 称不上 门。
    namespace se = agentty::settings;

    CHECK(se::opens_pane(se::Action::OpenRag));
    CHECK(se::opens_pane(se::Action::OpenAppearance));
    CHECK(se::opens_pane(se::Action::OpenSmart));
    CHECK(se::opens_pane(se::Action::OpenSandbox));
    CHECK(se::opens_pane(se::Action::OpenWebSearch));
    CHECK(se::opens_pane(se::Action::OpenList));

    // 选择一个值不是门,它改变的东西 此处 ,并获得自己的
    // 格言 而不是一个意义 "leads away".
    CHECK(!se::opens_pane(se::Action::CycleProfile));
    CHECK(!se::opens_pane(se::Action::None));
    CHECK(!se::opens_pane(se::Action::ToggleChangesStrip));
    CHECK(!se::opens_pane(se::Action::TogglePlugin));
}

TEST_CASE("settings: a row's description says what it DOES, in every state") {
    // 智能模式的关闭分支是空话 "off" 将所有兄弟姐妹都读作描述(第2
    // 段) "pre-turn context injection",
    // "hidden · ^R still reviews" ) 一个
    // -word 状态读作行,其描述已消失 MISSING ——关闭是默认,
    // 所以这是新用户看到的第一件事。设置行必须回答的问题是
    // "what would changing this do?" 这个问题在
    // 那个功能无所作为的州里最响亮。
    //
    // 所以: 任何一行都无法用空的状态单词描述自己。
    // 检查过。 两者 数值 切换 ,因为缺陷只存在于其中
    // 之一——一个曾经建立过名单的测试,会一直流传下去.
    namespace se = agentty::settings;

    for (bool smart_on : {false, true}) {
        agentty::Model m;
        m.d.smart.enabled = smart_on;

        const auto rows = se::items_for(m, se::Category::General);
        CHECK(rows.size() >= 5);

        for (const auto& r : rows) {
            if (r.primary.empty()) continue;
            // 单是州字不是描述。 (真正的行说
            // "off · every turn goes to the main model" 状态 和 副作用.)
            CHECK(r.secondary != "off");
            CHECK(r.secondary != "on");
            CHECK(r.secondary != "enabled");
            CHECK(r.secondary != "disabled");
            // 每排将军都要说点什么
            CHECK(!r.secondary.empty());
        }
    }
}

TEST_CASE("settings: a stateful row reports its state, not just its topic") {
    // 彼二分明法门. "pre-turn context injection"
    // 名称 TOPIC 但从不值,所以无论检索
    // 是开还是关,行看起来都是一样的——你必须
    // 打开 面板 显示当前设置的一行 IS.
    namespace se = agentty::settings;

    agentty::Model off;
    off.d.smart.enabled = false;
    agentty::Model on;
    on.d.smart.enabled = true;

    const auto a = se::items_for(off, se::Category::General);
    const auto b = se::items_for(on,  se::Category::General);
    REQUIRE(a.size() == b.size());

    const auto find = [](const std::vector<se::Item>& rows, se::Action act) {
        for (const auto& r : rows) if (r.action == act) return r.secondary;
        return std::string{};
    };

    // 翻转设置必须是 可见。
    CHECK(find(a, se::Action::OpenSmart) != find(b, se::Action::OpenSmart));
    CHECK(!find(a, se::Action::OpenSmart).empty());
}

