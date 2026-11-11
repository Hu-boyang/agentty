// tool_call_identity_test — WHO 这块是属于你的吗?
//
// 这里每个案例都是某个提供者从项目中的错误报告或其他工具中实际发送的形状。
// 接口边界 新的报告变成四行而不是 SSE 固定图:无 HTTP ,
// 没有 JSON ,没有 事件 管道 - 只是,
// 我刚开始 "the chunk said this, which call is it?".
//
// 标注的规则:身份是(id,索引) JOINTLY 当
// 一个块没有带, FAILS 而不是猜。错误的猜错会
// 附加一个 调用's 字节 给另一个,和产量 JSON
// 仍然这样 解析 ,因此腐败是沉默的,并达到一种工具的引用。
// 明显的失败就是明显的破裂。 传输 让你避免在
// 其中做出选择。

#include <string>

#include "agtest.hpp"

#include "agentty/provider/wire/tool_calls.hpp"

using namespace agentty::provider::wire;

namespace {

// 简写:一个块,既包含一半,仅包含id,仅包含索引,或者没有。
ChunkIdentity both(std::string_view id, std::size_t ix) { return {id, ix}; }
ChunkIdentity id_only(std::string_view id)              { return {id, std::nullopt}; }
ChunkIdentity index_only(std::size_t ix)                { return {"", ix}; }
ChunkIdentity nothing()                                 { return {"", std::nullopt}; }

}  // 命名空间结束。

TEST_CASE("identity: the ordinary case — open by index, continue by index") {
    ToolCallTracker t;

    auto a = t.attribute(both("call_a", 0));
    REQUIRE(a.has_value());
    CHECK(a->is_new);
    CHECK(!a->displaced.has_value());

    // 续块只携带指数, 相同 调用。
    auto b = t.attribute(index_only(0));
    REQUIRE(b.has_value());
    CHECK(!b->is_new);
    CHECK(b->call == a->call);
    CHECK(t.size() == 1);
}

TEST_CASE("identity: parallel calls stay apart") {
    ToolCallTracker t;
    auto a = t.attribute(both("call_a", 0));
    auto b = t.attribute(both("call_b", 1));
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(a->call != b->call);
    CHECK(t.size() == 2);

    // 中间延伸的右侧土地 调用.
    auto a2 = t.attribute(index_only(0));
    auto b2 = t.attribute(index_only(1));
    REQUIRE(a2.has_value());
    REQUIRE(b2.has_value());
    CHECK(a2->call == a->call);
    CHECK(b2->call == b->call);
}

TEST_CASE("identity: a reused index with a new id is a NEW call") {
    // 该 BUG WE SHIPPED。服务器完成 调用 以
    // 索引 0 键入,然后在那里开始另一个,仅用 ID 来区分它们。
    // 单凭索引键入,它们合并并损坏了两组参数。
    //
    // Zed的ToolCallAccumulator 声明了这个测试规则,用于:
    // 单独依赖任一值都不可靠，因为部分提供方会省略索引、
    // 复用索引，或在并行调用之间重复使用标识。
    ToolCallTracker t;
    auto a = t.attribute(both("call_a", 0));
    REQUIRE(a.has_value());
    t.at(*a).started = true;

    auto b = t.attribute(both("call_b", 0));
    REQUIRE(b.has_value());
    CHECK(b->is_new);
    CHECK(b->call != a->call);
    // 和流离失所者, 调用 被举报,因为打电话者 必须 关上它:
    // 未关闭 调用 离开a tool_use 没有 tool_result
    // 并挂转弯。不关闭交易则分拆一个错误来换一个更糟糕的错误。
    REQUIRE(b->displaced.has_value());
    CHECK(*b->displaced == a->call);

    // 匿名续作现在属于 调用 持有该指数,
    // 即 新 一个
    auto c = t.attribute(index_only(0));
    REQUIRE(c.has_value());
    CHECK(c->call == b->call);
}

TEST_CASE("identity: a known id continues its call even if the index moves") {
    // ID 是更强的控件。 服务器会重新编号 : -stream
    // 不得叉开一个 调用 有个稳定的身份证明
    ToolCallTracker t;
    auto a = t.attribute(both("call_a", 0));
    REQUIRE(a.has_value());

    auto moved = t.attribute(both("call_a", 3));
    REQUIRE(moved.has_value());
    CHECK(!moved->is_new);
    CHECK(moved->call == a->call);
    CHECK(t.size() == 1);

    // 新的指数现在也通向它。
    auto by_new = t.attribute(index_only(3));
    REQUIRE(by_new.has_value());
    CHECK(by_new->call == a->call);
}

TEST_CASE("identity: id-only providers (no usable index)") {
    // MiniMax ,通过zed #42584: 调用 由 id 识别,
    // 而索引字段完全不存在。 调用 仍然必须分开。
    ToolCallTracker t;
    auto a = t.attribute(id_only("call_a"));
    auto b = t.attribute(id_only("call_b"));
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(a->call != b->call);

    // 重复的编号是 相同 调用 持续,不是第三。
    auto a2 = t.attribute(id_only("call_a"));
    REQUIRE(a2.has_value());
    CHECK(!a2->is_new);
    CHECK(a2->call == a->call);
    CHECK(t.size() == 2);
}

TEST_CASE("identity: an empty id is absent, not a value") {
    // 几个代理 RESTATE 编号为 "" 继续而不是省略(
    // 将) -gateway #136),治疗. "" 以身份打开
    // 秒 调用 其编号是空的——和结束 -of-turn 扫描跳
    // 过空 -id 调用 ,所以它永远不会关闭 和转弯挂。
    ToolCallTracker t;
    auto a = t.attribute(both("call_a", 0));
    REQUIRE(a.has_value());

    auto b = t.attribute(both("", 0));
    REQUIRE(b.has_value());
    CHECK(!b->is_new);
    CHECK(b->call == a->call);
    CHECK(t.size() == 1);
}

TEST_CASE("identity: no id and no index is legal for a single call") {
    // 有些服务器发送的开口块没有字段。 飞行中只有
    // 调用 , 并不存在任何模糊性。
    ToolCallTracker t;
    auto a = t.attribute(nothing());
    REQUIRE(a.has_value());
    CHECK(a->is_new);

    auto b = t.attribute(nothing());
    REQUIRE(b.has_value());
    CHECK(!b->is_new);
    CHECK(b->call == a->call);
    CHECK(t.size() == 1);
}

TEST_CASE("identity: no id and no index with several calls FAILS") {
    // 电话 加载-bearing 案件数量。 调用 在一个
    // 没有证据的飞船中:任何选择都是硬币翻转,而输家则默默地
    // 破坏一种工具的引用。 JSON 仍然这样 解析.
    //
    // 所以这是一个错误的值,不是猜测。打电话者可以说出一些真实的话。
    // "the server sent an argument fragment we cannot attribute"（服务器发送了无法归属到具体调用的参数片段。）
    // - 而不是与另一个工具一起发送工具 调用's 字节.
    ToolCallTracker t;
    (void)t.attribute(both("call_a", 0));
    (void)t.attribute(both("call_b", 1));

    auto amb = t.attribute(nothing());
    REQUIRE(!amb.has_value());
    CHECK(amb.error() == AttributionError::Ambiguous);
}

TEST_CASE("identity: reset clears every handle") {
    ToolCallTracker t;
    (void)t.attribute(both("call_a", 0));
    (void)t.attribute(both("call_b", 1));
    CHECK(t.size() == 2);

    t.reset();
    CHECK(t.empty());

    // 索引地图随它而去——索引 0 必须打开新 调用 ,
    // 而不是决心 过期 上一转的插槽。
    auto a = t.attribute(index_only(0));
    REQUIRE(a.has_value());
    CHECK(a->is_new);
    CHECK(t.size() == 1);
}
