// mcp_result_type_test - 电话 `resultType` 字段为
// REQUIRED 2026-07-28协议, 这是我们广告的版本。
//
// 两种情况都不同寻常地明确:
//
//   "Servers implementing this protocol version MUST include this field."（实现此协议版本的服务器必须包含该字段。）
//
//   为保持向后兼容，当客户端收到旧协议版本服务器的结果，
//    而该版本没有 resultType 字段时，
//    必须将缺失字段视为 complete。
//
// 所以这是两份合同,不是一份,总是 EMIT 并容忍它,
// ABSENCE 解码时,一个只满足了字段的解码器。
// `Maybe<std::string>` 将 编译 键,省略 传输 ,
// 并悄悄打破每2026-07-28客户端.
//
// 值得专门测试,因为缺少一个需要的字段是本地看不见的: 我们自己的
// 客户端默认它,所以 agentty-talking-to-agentty
// 完美地工作,同时符合第三 -party 客户端拒绝响应。

#include <mcp/methods.hpp>

#include <string>

#include "agtest.hpp"

using namespace mcp;

namespace {
// 每个结果都打入了谱号`required: [..., "resultType", ...]`.
template <class T>
void check_emits_result_type(const char* what) {
    T v{};
    const Json j = codec<T>().encode(v);
    INFO(what);
    CHECK(j.contains("resultType"));
    if (j.contains("resultType")) CHECK(j["resultType"] == "complete");
}

template <class T>
void check_absent_decodes_complete(const char* what, Json minimal) {
    INFO(what);
    // 2026-07-28前服务器完全省略了字段.
    CHECK(!minimal.contains("resultType"));
    const T v = codec<T>().decode(minimal);
    CHECK(v.resultType == "complete");
}
} // 命名空间结束。

TEST_CASE("mcp: every result emits resultType") {
    // 默认 -constructed 结果必须已经携带—— 调
    // 用 网站不必记住,这是随着新网站的添加而保持真实的
    // 唯一方法。
    check_emits_result_type<CallToolResult>("CallToolResult");
    check_emits_result_type<ListToolsResult>("ListToolsResult");
    check_emits_result_type<ListPromptsResult>("ListPromptsResult");
    check_emits_result_type<GetPromptResult>("GetPromptResult");
    check_emits_result_type<ListResourcesResult>("ListResourcesResult");
    check_emits_result_type<ReadResourceResult>("ReadResourceResult");
    check_emits_result_type<ListResourceTemplatesResult>("ListResourceTemplatesResult");
    check_emits_result_type<CompleteResult>("CompleteResult");
}

TEST_CASE("mcp: an absent resultType decodes as complete") {
    // 光谱的落后 -compat 规则, paltic。
    // 一个较老的服务器发送的字段和客户端 必须 读为 "complete"
    // 而不是空的 —— 这里的空字符串会让一个有效的响应看起来像
    // 一个未知的结果类型.
    check_absent_decodes_complete<CallToolResult>(
        "CallToolResult", Json{{"content", Json::array()}});
    check_absent_decodes_complete<ListToolsResult>(
        "ListToolsResult", Json{{"tools", Json::array()}});
    check_absent_decodes_complete<CompleteResult>(
        "CompleteResult",
        Json{{"completion", Json{{"values", Json::array()}}}});
}

TEST_CASE("mcp: a server-sent resultType survives the round trip") {
    // "input_required" 是一个字符串名称的其他值 —
    // — 服务器需要用户更多的信息才能完成。 "complete"
    // 能让客户停止等待 治疗一半 -answer 成为最终的。
    Json j{{"content", Json::array()}, {"resultType", "input_required"}};
    const auto v = codec<CallToolResult>().decode(j);
    CHECK(v.resultType == "input_required");
    // 而它能存活到re-encoding,所以一个解码和前进
    // 的代理不会默默地将其降级.
    CHECK(codec<CallToolResult>().encode(v)["resultType"] == "input_required");
}
