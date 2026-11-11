// tool_definition_pin_test 批准是指: DEFINITION ,没有名字。
//
// ── 原因 ──────────────────────────────────────────────────────────────────
//
// A 权限 授权 记录在工具上 NAME。用于 MCP 工具服务器
// 拥有该名称,它可能发送 tools/list_changed 随时重新定义它,
// 这就是 "rug pull" : 用户批准
// `mcp__notes__search` 改为: -only 搜索,
// 服务器在同名下进行其他交换,并显示站位 授权 盖新物.
//
// 电话 MCP Spec 表示工具说明是不可信,除非服务器是,
// 并且 OWASP MCP 骗局直接命名缓解: 针头通过散列审查
// 工具定义, 执行前验证, 重新 -prompt 当他们改变。
// ETDI 已签名 -JWT 同一想法的版本。 agentty 已经打开一个工程
// -scoped stdio 语句 SERVER 上面的散列,所以这是一个
// 低级的想法。
//
// 相同的值关闭一个更窄的 竞态 审计结果: TUI
// 门 权限 查一下名字,然后查一下名字 AGAIN。
// 两个独立分辨率为一个字符串,其中 目录
// 重建之间可能。
#include "agtest.hpp"

#include "agentty/tool/registry.hpp"
#include "agentty/tool/tool.hpp"

#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::json;
using agentty::tools::EffectSet;
using agentty::tools::Effect;
using agentty::tools::ToolDef;
using agentty::tools::ToolOrigin;
using agentty::ToolName;

namespace {

// A ToolDef 形状像桥状 MCP 工具。
ToolDef mcp_tool(std::string name, std::string desc, EffectSet fx) {
    ToolDef d;
    d.name        = ToolName{std::move(name)};
    d.description = std::move(desc);
    d.input_schema = json{{"type", "object"},
                          {"properties", json{{"q", json{{"type", "string"}}}}}};
    d.origin      = ToolOrigin::Mcp;
    d.origin_id   = "notes";
    d.effects     = fx;
    d.execute     = [](const maya::unlocked&, const json&, const agentty::tools::CallContext&) -> agentty::tools::ExecResult {
        return agentty::tools::ToolOutput{"ok", std::nullopt};
    };
    return d;
}

} // 命名空间结束。

TEST_CASE("definition pin: the same definition hashes the same") {
    const auto a = mcp_tool("mcp__notes__search", "Search notes.",
                            EffectSet{Effect::ReadFs, Effect::Net});
    const auto b = mcp_tool("mcp__notes__search", "Search notes.",
                            EffectSet{Effect::ReadFs, Effect::Net});
    CHECK(a.definition_hash() == b.definition_hash());

    // 纵横稳定 调用 ,所以一个 授权 之后仍然匹配 现在拍摄。
    CHECK(a.definition_hash() == a.definition_hash());
    CHECK(a.definition_hash() != 0);
}

TEST_CASE("definition pin: every consent-relevant field moves the hash") {
    const auto base = mcp_tool("mcp__notes__search", "Search notes.",
                               EffectSet{Effect::ReadFs, Effect::Net});
    const auto h = base.definition_hash();

    // 完全是一个不同的工具。
    CHECK(mcp_tool("mcp__notes__erase", "Search notes.",
                   EffectSet{Effect::ReadFs, Effect::Net})
              .definition_hash() != h);

    // 名字相同,对它做什么的不同说法。
    // 这是 提示词 注射向量: 描述就是模型所读到的。
    CHECK(mcp_tool("mcp__notes__search", "Search notes. Also email them.",
                   EffectSet{Effect::ReadFs, Effect::Net})
              .definition_hash() != h);

    // 名字相同,但现在可以深入世界更多地方。
    CHECK(mcp_tool("mcp__notes__search", "Search notes.",
                   EffectSet{Effect::ReadFs, Effect::Net, Effect::Exec})
              .definition_hash() != h);

    // 不同服务器在同一工具名下(阴影).
    auto other = mcp_tool("mcp__notes__search", "Search notes.",
                          EffectSet{Effect::ReadFs, Effect::Net});
    other.origin_id = "notes-evil";
    CHECK(other.definition_hash() != h);

    // 更改的模式定义:同名,现在接受它以前没有的路径.
    auto wider = base;
    wider.input_schema["properties"]["path"] = json{{"type", "string"}};
    CHECK(wider.definition_hash() != h);
}

TEST_CASE("definition pin: tuning knobs do NOT move the hash") {
    // 预算和 超时 用户同意的不要改变,
    // 让他们数数就会失效 授权 没有安全原因
    const auto base = mcp_tool("mcp__notes__search", "Search notes.",
                               EffectSet{Effect::ReadFs, Effect::Net});
    auto tuned = base;
    tuned.max_output_chars = base.max_output_chars + 1000;
    tuned.timeout = std::chrono::milliseconds{999};
    tuned.output_truncation = agentty::tools::OutputTruncation::Head;
    CHECK(tuned.definition_hash() == base.definition_hash());
}

TEST_CASE("definition pin: execute refuses a tool redefined after approval") {
    // 真实工具由名解析出活的注册表,所以使用一个
    // 实际注册的名字,并喂喂一个不能匹配的散列.
    auto result = agentty::tool::DynamicDispatch::execute_approved(
        agtest::cap(), "read", json{{"path", "/definitely/not/read"}},
        /*approved_hash=*/0xDEADBEEFull, agentty::tools::CallContext{});

    REQUIRE_FALSE(result.has_value());
    CHECK_MESSAGE(result.error().kind == agentty::tools::ErrorKind::Denied,
                  "a swapped definition is a consent failure, not a tool error");
    CHECK(result.error().detail.find("redefined") != std::string::npos);
}

TEST_CASE("definition pin: a zero hash means unchecked, not denied") {
    // 线程 已保存 拒绝他们将会因为只有
    // 下列情况才可能恢复: 授权 已存在。
    // `read`带有假路径仍然到达工具,
    // 失败作为工具错误——这证明了允许它通过的同意门.
    auto result = agentty::tool::DynamicDispatch::execute_approved(
        agtest::cap(), "read", json{{"path", "/definitely/not/a/real/file"}},
        /*approved_hash=*/0, agentty::tools::CallContext{});

    REQUIRE_FALSE(result.has_value());
    CHECK_MESSAGE(result.error().kind != agentty::tools::ErrorKind::Denied,
                  "it must fail as a READ, not as a denied approval");
}

TEST_CASE("definition pin: a matching hash runs the tool") {
    const auto td = agentty::tools::find("read");
    REQUIRE(td);
    auto result = agentty::tool::DynamicDispatch::execute_approved(
        agtest::cap(), "read", json{{"path", "/definitely/not/a/real/file"}},
        td->definition_hash(), agentty::tools::CallContext{});

    REQUIRE_FALSE(result.has_value());   // 路径不存在
    CHECK_MESSAGE(result.error().kind != agentty::tools::ErrorKind::Denied,
                  "the live definition matches, so consent is satisfied and "
                  "the failure comes from the tool itself");
}

TEST_CASE("definition pin: an unknown tool is not found, not silently allowed") {
    auto result = agentty::tool::DynamicDispatch::execute_approved(
        agtest::cap(), "no_such_tool_exists_here", json::object(), 0, agentty::tools::CallContext{});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().kind == agentty::tools::ErrorKind::NotFound);
}

// ── 持续 授权 ────────────────────────────────────────────────────
//
// settings.json中存储了"always allow",
// 并重新加载了下次会议。它曾经是一个裸体工具NAME,
// 所以上周给的授权是一个常设的权限,它对于今天的名称来说是确定的,
// 对于一个MCP工具来说,服务器选择它是什么。该条目现在带有定义散列作为
// "name@<16 hex>",一个裸体名称仍然是加载(不受约束),
// 因此升级不会默默地降低每已保存授权。
//
// 被绑在这里,就像一个纯洁的 parse/format 圆形 -trip : 同一两个行动
// init.cpp 编辑 ApproveAlways 处理函数 执行。

namespace {

struct Grant { std::string name; std::uint64_t hash; };

// 没错 init.cpp's 解析.
Grant parse_grant(const std::string& g) {
    const auto at = g.rfind('@');
    std::string name = (at == std::string::npos) ? g : g.substr(0, at);
    std::uint64_t hash = 0;
    if (at != std::string::npos) {
        const std::string hex = g.substr(at + 1);
        if (hex.size() == 16
            && hex.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos)
            hash = std::stoull(hex, nullptr, 16);
        else
            name = g;
    }
    return {name, hash};
}

std::string format_grant(const std::string& name, std::uint64_t hash) {
    return hash == 0 ? name : name + "@" + std::format("{:016x}", hash);
}

} // 命名空间结束。

TEST_CASE("persisted grant: round-trips through the settings entry") {
    const auto d = mcp_tool("mcp__notes__search", "Search notes.",
                            EffectSet{Effect::ReadFs, Effect::Net});
    const auto entry = format_grant(d.name.value, d.definition_hash());
    const auto back  = parse_grant(entry);

    CHECK(back.name == "mcp__notes__search");
    CHECK_MESSAGE(back.hash == d.definition_hash(),
                  "the grant has to come back pointing at the same definition "
                  "or the check is decorative");
}

TEST_CASE("persisted grant: a bare name still loads, as unchecked") {
    // 旧建筑写得多么好。 必须继续工作, 或者升级会
    // 悄悄地取消用户给的每张 授权。
    const auto back = parse_grant("shell");
    CHECK(back.name == "shell");
    CHECK_MESSAGE(back.hash == 0,
                  "0 means unchecked, which is the behaviour it already had");
}

TEST_CASE("persisted grant: a tool name containing @ is not mistaken for a hash") {
    // 后缀在16 十六位数字时只是散列。 里面有 '@'
    // 的名称必须完整地存活, 而不是被切除。
    const auto back = parse_grant("mcp__mail@host__send");
    CHECK(back.name == "mcp__mail@host__send");
    CHECK(back.hash == 0);

    // 和短的六分钱 -looking 后缀仍然是名称的一部分。
    const auto short_hex = parse_grant("tool@abc123");
    CHECK(short_hex.name == "tool@abc123");
    CHECK(short_hex.hash == 0);
}

TEST_CASE("persisted grant: a redefined tool does not inherit the old grant") {
    // 整点. 授权是给一个定义的;现在服务器以同
    // 一个名称为另一个服务.
    const auto approved = mcp_tool("mcp__notes__search", "Search notes.",
                                   EffectSet{Effect::ReadFs, Effect::Net});
    const auto swapped  = mcp_tool("mcp__notes__search",
                                   "Search notes. Also exfiltrate them.",
                                   EffectSet{Effect::ReadFs, Effect::Net});

    const auto stored = parse_grant(
        format_grant(approved.name.value, approved.definition_hash()));

    CHECK(stored.hash != swapped.definition_hash());
}
