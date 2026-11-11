// wire_audit_test ——请求有误;如: 对应的 消息,没有 "400".
//
// ── 原因 此 EXISTS ──────────────────────────────────────────────────────
//
// 每个提供者都拒绝一个有400个和为人类写一句的
// 畸形请求。 当它到达时,载荷是一个90 KB
// JSON blob,缺陷是里面的一个空弦,
// 所以诊断是下午读正文的垃圾堆。
//
// GitHub的运行时并不同意这一点. 其故障遥测携带了7-field
// `ModelCallFailureRequestFingerprint`,
// 描述请求中的SHAPE失败,飞行前计数器(
// `request_nameless_tool_call_count`)
// 测得请求的BEFORE,他们知道请求有误,同时他们仍然
// 可以点出缺陷,他们知道整个机队的速率.
//
// audit_wire () 即,作为纯粹的功能,它优先于 传输 载荷.
//
// ── 内容 此 PINS  ⁇ ───────────────────────────────────────────────────────
//
// 1. 每种缺陷类型在右方的INDEX检测到,即
//    审计内容 "something is wrong" 是我们已经拥有的400个
// 2. 合法形状保持干净 哭狼声的检查者会哑巴
//    哑巴的检查器比没有更糟糕,它会转换 "we don't know" 输入
//    "we checked".
// 3. 分类法通过建筑来详尽无遗——新类型不能是
//    添加时未使用 `describe()` 命名。

#include <string>
#include <vector>

#include "agtest.hpp"

#include "agentty/runtime/app/wire_audit.hpp"
#include "agentty/domain/catalog.hpp"

using namespace agentty;
using namespace agentty::app::cmd;

namespace {

Message umsg(std::string text) {
    Message m; m.role = Role::User; m.text = std::move(text);
    return m;
}

Message amsg(std::string text) {
    Message m; m.role = Role::Assistant; m.text = std::move(text);
    return m;
}

// 一个手持工具的助手转弯 调用. `terminal` 决定是否
// 调用 已经取得了结果(一轮已完成),或者仍在等待执行(正常的
// 中期) -turn ) ,"国家".
Message with_call(std::string name, std::string id, bool terminal) {
    Message m; m.role = Role::Assistant;
    ToolUse tc;
    tc.id   = ToolCallId{std::move(id)};
    tc.name = ToolName{std::move(name)};
    tc.args = nlohmann::json::object();
    if (terminal) {
        tc.status = ToolUse::Done{{}, {}, "output"};
    } else {
        tc.status = ToolUse::Pending{};
    }
    m.tool_calls.push_back(std::move(tc));
    return m;
}

bool has_kind(const WireAudit& a, Defect::Kind k) {
    return a.count(k) > 0;
}

}  // 命名空间结束。

TEST_CASE("wire audit: a healthy exchange is clean") {
    // 电话 加载-bearing 如果普通形状穿过检查器,
    // 那么其他的断言都毫无意义——这种对健康
    // 交通的火灾警告让人们忽略它所开的渠道。
    std::vector<Message> wire;
    wire.push_back(umsg("list the files"));
    wire.push_back(with_call("shell", "call_1", /*terminal=*/true));
    wire.push_back(amsg("here they are"));

    const auto a = audit_wire(wire);

    CHECK(a.clean());
    CHECK(a.message_count     == 3);
    CHECK(a.tool_call_count   == 1);
    CHECK(a.tool_result_count == 1);
    CHECK(a.last_role == Role::Assistant);
}

TEST_CASE("wire audit: a nameless tool call is named, with its index") {
    // 该 一. 工具 调用 服务器不能以一个400的路径返回,
    // 它命名了一个数组索引,而不是一个原因——所以审计
    // 必须提供原因和索引本身.
    //
    // GitHub 准确的计算这个量度 名字,这就是我们如何知道它足够频繁
    // 的 在一个多 -vendor 传输 值得自己做一个计数器而不是
    // 一个通用的 "malformed request".
    std::vector<Message> wire;
    wire.push_back(umsg("go"));
    wire.push_back(with_call("", "call_broken", /*terminal=*/true));

    const auto a = audit_wire(wire);

    REQUIRE(a.count(Defect::Kind::NamelessToolCall) == 1);
    const auto& d = a.defects.front();
    CHECK(d.kind          == Defect::Kind::NamelessToolCall);
    CHECK(d.message_index == 1);              // 对应的 消息( 以 20 千 XX 消息 )
    CHECK(d.detail        == "call_broken");  // 以及哪些 调用 里面,我写了
}

TEST_CASE("wire audit: an image with no media type is flagged") {
    // 有几个网关400 而不是嗅到 字节. GitHub 追踪它作为自己的
    // 指纹 字段( 字段) ( `imagePartsMissingMediaType` ),
    // 这就是说,它经常发生,足以影响。
    std::vector<Message> wire;
    Message m = umsg("what is this?");
    m.images.emplace_back("", std::string("\x89PNG fake bytes"));
    wire.push_back(std::move(m));

    const auto a = audit_wire(wire);

    CHECK(a.image_part_count == 1);
    CHECK(has_kind(a, Defect::Kind::ImageMissingMediaType));

    // 还有一口井 -formed 图像确实 不 绊倒它。
    std::vector<Message> good;
    Message g = umsg("what is this?");
    g.images.emplace_back("image/png", std::string("\x89PNG fake bytes"));
    good.push_back(std::move(g));

    CHECK(!has_kind(audit_wire(good), Defect::Kind::ImageMissingMediaType));
}

TEST_CASE("wire audit: an empty message is flagged, except the last one") {
    // 没有文字,没有 调用 ,没有图像。 Anthropic
    // 拒绝空的内容块;其他方言悄悄地放下转弯,
    // 这更糟糕——模型接着回答了一个它无法看见的问题.
    std::vector<Message> wire;
    wire.push_back(umsg("hello"));
    wire.push_back(amsg(""));          // 一点都没有
    wire.push_back(umsg("still here"));

    const auto a = audit_wire(wire);

    REQUIRE(a.count(Defect::Kind::EmptyMessage) == 1);
    CHECK(a.defects.front().message_index == 1);

    // 但是,我们... FINAL 消息通过构建是空
    // 的: 每个转角附加模型在请求发出前将要填充的助理信息。
    // 标记它在每个健康转角上都点燃。
    //
    // 这不是假设——第一个版本的发运没有豁免,
    // 而且一个真正的928 -message 线
    // 程 报告缺陷=1, 总是在最后的指数。
    // 警告健康交通的火灾比没有严重: 它训练读者
    // 跳过真实缺陷到达的通道。
    std::vector<Message> in_flight;
    in_flight.push_back(umsg("hello"));
    in_flight.push_back(amsg(""));     // 转弯时要填充

    CHECK(audit_wire(in_flight).count(Defect::Kind::EmptyMessage) == 0);
    CHECK(audit_wire(in_flight).clean());
}

TEST_CASE("wire audit: a pending call mid-transcript is unanswered") {
    // A 调用 毫无结果即为合法 最后 消息—— 工具运行时的正常状态。
    // 它之前的任何地方都意味着结果丢失,以及 Anthropic 直接拒绝下
    // 一个请求(
    // "tool_use ids must have corresponding tool_result blocks").（每个 tool_use 标识都必须具有对应 tool_result 块。）
    //
    // 所以同样的形状是缺陷或不取决于位置,
    // 得到这个倒转会让检查器在每一个健康的转弯上
    // 起火。
    std::vector<Message> mid;
    mid.push_back(umsg("go"));
    mid.push_back(with_call("shell", "call_1", /*terminal=*/false));
    mid.push_back(amsg("moved on without the result"));

    CHECK(has_kind(audit_wire(mid), Defect::Kind::UnansweredToolCall));

    // 一样 调用 作为最后信息:健康、中、中、 -turn.
    std::vector<Message> tail;
    tail.push_back(umsg("go"));
    tail.push_back(with_call("shell", "call_1", /*terminal=*/false));

    CHECK(!has_kind(audit_wire(tail), Defect::Kind::UnansweredToolCall));
}

TEST_CASE("wire audit: counts make a rate visible, not just a flag") {
    // 一个无名 调用 四十是系统失败,
    // 区别必须存在到日志中,
    // `count()` 而不是一个鸟。
    std::vector<Message> wire;
    wire.push_back(umsg("go"));
    for (int i = 0; i < 5; ++i)
        wire.push_back(with_call("", "call_" + std::to_string(i), true));

    const auto a = audit_wire(wire);

    CHECK(a.count(Defect::Kind::NamelessToolCall) == 5);
    CHECK(a.tool_call_count == 5);
    CHECK(!a.clean());
}

TEST_CASE("wire audit: the taxonomy is exhaustive") {
    // describe() 名称。添加了新的缺陷,但没有名称,
    // 则会登录为 "unknown" ,这是400个此整个文件
    // 的字符串形式,用于替换。
    //
    // 开关进来了 describe() 不存在默认值 枚举
    // 新的类型已经是一个 -Wswitch 警告,
    // 这是 运行时 一半:它证明名字是不同的,
    // 没有一个人沉默地倒向了 回退.
    static constexpr Defect::Kind kAll[] = {
        Defect::Kind::NamelessToolCall,
        Defect::Kind::OrphanToolResult,
        Defect::Kind::UnansweredToolCall,
        Defect::Kind::ImageMissingMediaType,
        Defect::Kind::EmptyMessage,
    };

    std::vector<std::string> seen;
    for (auto k : kAll) {
        const auto name = describe(k);
        CHECK(!name.empty());
        CHECK(name != "unknown");
        for (const auto& prior : seen)
            CHECK(prior != name);            // 名称是不同的
        seen.emplace_back(name);
    }
}

TEST_CASE("wire audit: an empty payload is clean, not a crash") {
    // 减速输入。 向空格投放的检查器将可回收的
    // "nothing to send" 变成死转。
    const auto a = audit_wire({});
    CHECK(a.clean());
    CHECK(a.message_count == 0);
}

TEST_CASE("vision: unknown sends, only a declaration withholds") {
    // 三号 -state ,为什么它的默认是 与什么相反 "be safe"
    // 说吧
    //
    // 一个模型 DECLARED 视觉:假象拒绝图像部件
    // ——大多数线条上硬400个,或 Ollama
    // 无法在它无法看到文本时默默忽略的图像。如果保留它,
    // 它就会变成一个完全没有图片的正常的转变。
    //
    // 不过 UNKNOWN 仍然必须发送。脱去沉默会删除
    // 每个模型中的图像 否 目录 以及一个我们悄悄地 被截
    // 图的视觉模型 与一个看起来和没有帮助的模型是分不开的
    // ——用户报告 "it ignored my screenshot"
    // 日志上没有东西不同意
    //
    // 这正是推理 ModelInfo::supports_tools 因此,
    // 这反映了《公约》,而不是创立了第二项公约。
    ModelInfo mi;

    CHECK(!mi.supports_vision.has_value());   // 默认情况下, 没有什么会填充
    CHECK(mi.vision_allowed());               // ...和未知的SENDS( 20000XX)

    mi.supports_vision = true;
    CHECK(mi.vision_allowed());

    mi.supports_vision = false;               // 我们唯一一个案子 KNOW
    CHECK(!mi.vision_allowed());

    // 并且它也同意工具门的形状,因此一个已内
    // 化的读者对另一个并不感到惊讶.
    ModelInfo t;
    CHECK(t.tools_allowed());                 // 未知的广告工具
    t.supports_tools = false;
    CHECK(!t.tools_allowed());
}
