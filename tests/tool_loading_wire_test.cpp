// tool_loading_wire_test — #98结束通过真实编码器结束.
//
// 计划者决定;这些检查每个提供者实际得到的字节:
//   Anthropic 每一个工具在 传输 打开 -demand 单曲 `defer_loading` ,
//              则 缓存 针在非 -deferred 工具,以及一个 tool_search
//              变成结果 tool_reference 块。
//   OpenAI 打开 -demand 工具缺失,不超过128个,
//              推迟 标志 永远不能漏进 正文.

#include "agtest.hpp"

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "agentty/provider/anthropic/transport.hpp"
#include "agentty/provider/msg_shared.hpp"
#include "agentty/tool/tool_loading.hpp"
#include "agentty/tool/tool_plan.hpp"

using namespace agentty;
namespace tl = agentty::tools::tool_loading;
namespace tp = agentty::tools::plan;
using agentty::tools::ToolDef;
using agentty::tools::ToolOrigin;

namespace {

std::vector<ToolDef> catalog(int mcp_tools) {
    std::vector<ToolDef> c;
    for (const char* n : {"read", "edit", "shell"}) {
        ToolDef t;
        t.name.value = n;
        t.origin = ToolOrigin::Native;
        t.input_schema = {{"type", "object"}};
        c.push_back(t);
    }
    for (int i = 0; i < mcp_tools; ++i) {
        ToolDef t;
        t.name.value = "mcp__srv__tool_" + std::to_string(i);
        t.description = "tool " + std::to_string(i);
        t.origin = ToolOrigin::Mcp;
        t.origin_id = "srv";
        t.input_schema = {{"type", "object"}};
        c.push_back(t);
    }
    return c;
}

std::vector<provider::ToolSpec> plan_for(const std::vector<ToolDef>& cat, provider::Kind kind,
                                         tp::Loading mode, tp::Loaded loaded = {}) {
    provider::Selection sel;
    sel.kind = kind;
    tl::Request r;
    for (const auto& t : cat) r.pool.push_back(&t);
    r.loaded = std::move(loaded);
    r.limits = tl::limits_for(sel, 200'000);
    r.mode = mode;
    std::vector<provider::ToolSpec> out;
    (void)tl::plan_into(r, out);
    return out;
}

} // 命名空间结束。

TEST_CASE("tool loading wire: Anthropic defers, OpenAI omits (#98)") {
    const auto cat = catalog(200);
    const auto a = plan_for(cat, provider::Kind::Anthropic, tp::Loading::OnDemand);
    const auto o = plan_for(cat, provider::Kind::OpenAI, tp::Loading::OnDemand);

    std::size_t a_deferred = 0;
    for (const auto& s : a) a_deferred += s.defer_loading ? 1 : 0;
    CHECK(a.size() == cat.size() + 1);           // 每个工具 + tool_search
    CHECK(a_deferred == 200);
    CHECK(a.back().name == "tool_search");
    CHECK(!a.back().defer_loading);

    for (const auto& s : o) CHECK(!s.defer_loading);
    CHECK(o.size() == 3 + 1);                    // 土生土长者+ tool_search
    CHECK(o.back().name == "tool_search");
    // 搜索工具名称可以找到。
    CHECK(o.back().description.find("srv (200 tools)") != std::string::npos);
}

TEST_CASE("tool loading wire: OpenAI never gets more than 128 tools") {
    const auto cat = catalog(500);
    const auto o = plan_for(cat, provider::Kind::OpenAI, tp::Loading::All);
    CHECK(o.size() <= 128);
    CHECK(o.back().name == "tool_search");       // 其余的可以到达
    // 聊天编码器会把任何延迟的东西都放下,作为后台.
    std::vector<provider::ToolSpec> mixed = o;
    mixed.push_back({"leak", "x", {{"type", "object"}}, false, /*defer_loading=*/true});
    CHECK(provider::wire::openai_chat_tools(mixed).size() == o.size());
}

TEST_CASE("tool loading wire: a tool_search hit on Anthropic is a tool_reference") {
    Thread t;
    Message u;
    u.role = Role::User;
    u.text = "decompile main";
    t.messages.push_back(u);
    Message a;
    a.role = Role::Assistant;
    ToolUse call;
    call.id.value = "toolu_1";
    call.name.value = "tool_search";
    call.args = {{"query", "decompile"}};
    call.status = ToolUse::Done{{}, {}, std::string{"1 tool loaded for this conversation.\n"}
        + std::string{tp::kLoadedMarker} + "mcp__bn__decompile_function - Decompile\n"
        + "  schema: {\"type\":\"object\"}\n"};
    a.tool_calls.push_back(call);
    t.messages.push_back(a);

    const auto body = nlohmann::json::parse(provider::anthropic::messages_json_string(t));
    // 消息[2]是携带tool_result的用户转弯.
    const auto& content = body.at(2).at("content");
    REQUIRE(content.is_array());
    const auto& result = content.at(0);
    CHECK(result.at("type") == "tool_result");
    bool ref = false, other = false;
    for (const auto& b : result.at("content")) {
        if (b.at("type") == "tool_reference" && b.at("tool_name") == "mcp__bn__decompile_function")
            ref = true;
        else
            other = true;
    }
    CHECK(ref);
    // 现场直播 API 400s a tool_result 将参考文献与其他块混合。
    CHECK(!other);
    // 还有 线程 现在算作 已加载.
    CHECK(tp::loaded_in(t).contains("mcp__bn__decompile_function"));
}

TEST_CASE("tool loading wire: a loaded tool stays on the OpenAI wire next turn") {
    const auto cat = catalog(200);
    const auto before = plan_for(cat, provider::Kind::OpenAI, tp::Loading::OnDemand);
    const auto after = plan_for(cat, provider::Kind::OpenAI, tp::Loading::OnDemand,
                                {"mcp__srv__tool_150"});
    bool found = false;
    for (const auto& s : after) found |= s.name == "mcp__srv__tool_150";
    CHECK(found);
    CHECK(after.size() == before.size() + 1);
}
