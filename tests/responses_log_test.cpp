// responses_log_test — 反应方言必须从日志中调试.
//
// ── 原因 ──────────────────────────────────────────────────────────────────
//
// 一个副驾驶转回,每个参数-taking工具都失败(
// "[invalid args] path required"),而0-argument工具成功.
// 这种不对称意味着争论被丢失,而不是模型软弱.
//
// 日志无法在什么地方解决。 Chart 传输层 放弃其请求( Q)
// `openai.request` + `openai.request.body`
// 及其解码决定,但答复编码程序—— 传输层 副驾驶用于gpt-*模型——只记录故障。
// -looking 数据流 故我对你们所争论的是空洞的,
// "we never sent the tools",（我们从未发送工具。）
// "the server sent no arguments" ,和 "we failed to route them"
// 无法区分,而不在用户的机器上装一个调试器。
//
// ── 该 RULE ─────────────────────────────────────────────────────────────
//
// 每个工具 调用 在整个生命中,
//
//     responses.request 我们要求的(包括工具计数) responses.tool_open
//     item_id ↔ call_id 名称
//     responses.tool_args_routed 承运人交付的 字节 ,
//     有多少个 responses.tool_closed 或 调用 最终与
//
// 有了这四个,就空了 -arguments 报告是一个
// 查询而非猜测。测试的标点是它们的存在,并带有可将它们合并
// 的识别符,因为日志行是微不足道的可删除的,
// 套件中其他任何内容都不会注意到。

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "agtest.hpp"

#include "agentty/provider/chatgpt/responses.hpp"
#include "agentty/util/logx.hpp"

using namespace agentty;
namespace cc = agentty::provider::chatgpt;

namespace {

// 驾驶真正的解码器,然后读回飞行记录器捕获的是什么.
//
// 戒指总是记录Warn+, 以及所有当 build/filter 询问( 调试建立
// 默认值以跟踪 - 见 logx::init). dump_flight_recorder_to
// 是支持的解开槽的方法, 也是崩溃的原因 处理函数 财务报告和已审计
// 财务报表 `agentty diagnostics` 两者都使用,
// 所以通过测试意味着测试一个真正的错误报告所行走的路径.
// logx::detail::init (从第一个木头上的
// 魔法静态) 调用 因此,环境必须在此之前确定,
// 而不是在试验中确定。 正文 ,在其它测试已经登录之后,
// 可能很可能会运行。 -scope 用户让我们在前面 main() ,
// 因此在任何 TEST_CASE.
//
// 传输 =debug正是用户调试此操作的特性,因此测试会练习
// 一个真实的bug报告所行走的相同的过滤路径.
const bool g_log_env = [] {
    ::setenv("AGENTTY_LOG", "wire=debug", /*覆盖=*/1);
    return true;
}();

std::string run_and_capture(const std::vector<std::string>& sse) {
    REQUIRE(g_log_env);

    const auto path = std::filesystem::temp_directory_path()
                    / "agentty_responses_log_test.txt";
    std::filesystem::remove(path);

    // 飞行环是过程 -wide 并且累积的 —— 它没有重新设置,
    // 由设计( 崩溃需要一切 )。 因此, 盖上一个独特的标记,
    // 运行转弯, 并且只保留标记后的土地。 没有这个, 第二次测试会读
    // 取第一个测试的行和 "no arguments were routed"
    // 无法证明。
    static int seq = 0;
    ++seq;
    // 垃圾场的线路是 "<meta> <channel> <site>: <message>" ,
    // 所以网站和消息的连结是 ": " - 配对,不是空地
    const std::string mark = "responses_log_test.mark: seq="
                           + std::to_string(seq);
    AGT_LOG(Wire, Debug, "responses_log_test.mark", "seq={}", seq);

    (void)cc::parse_sse_for_test(sse);
    REQUIRE(logx::dump_flight_recorder_to(path.string().c_str()));

    std::string dump;
    {
        // 范围:Windows无法删除一个仍然打开的文件.
        std::ifstream in(path);
        dump.assign(std::istreambuf_iterator<char>(in),
                    std::istreambuf_iterator<char>());
    }
    std::error_code rec;
    std::filesystem::remove(path, rec);

    const auto at = dump.rfind(mark);
    REQUIRE(at != std::string::npos);
    return dump.substr(at);
}

// 副驾驶 -shaped 转弯: 副驾驶确实如此 不 数据流
// 参数片段,它会汇合并发送一个 `.done` 快照。
// 这就是原始虫藏在体内的形状,所以它就是值得被钉住的形状。
const std::vector<std::string> kHealthyTurn = {
    R"({"type":"response.output_item.added","item":{"type":"function_call","id":"fc_1","call_id":"call_9","name":"read"}})",
    R"({"type":"response.function_call_arguments.done","item_id":"fc_1","arguments":"{\"path\":\"a.txt\"}"})",
    R"({"type":"response.output_item.done","item":{"type":"function_call","id":"fc_1"}})",
    R"({"type":"response.completed","response":{"usage":{}}})",
};

// 用户实际撞到的故障:调用打开并关闭,
// 但没有任何论据到达任何载体上.
const std::vector<std::string> kEmptyArgsTurn = {
    R"({"type":"response.output_item.added","item":{"type":"function_call","id":"fc_1","call_id":"call_9","name":"read"}})",
    R"({"type":"response.output_item.done","item":{"type":"function_call","id":"fc_1"}})",
    R"({"type":"response.completed","response":{"usage":{}}})",
};

bool has(const std::string& hay, std::string_view needle) {
    return hay.find(needle) != std::string::npos;
}

}  // 命名空间结束。

TEST_CASE("responses: a tool call's whole life is in the log") {
    const auto dump = run_and_capture(kHealthyTurn);

    // 开场:身份配对. item_id 后一论点 帧 地址;
    // call_id 是什么,这是 状态更新函数 当它们有分歧时,
    // 它们会用副驾驶机, 与此不匹配的正是 参数着陆和准备降落
    // 之间的区别 tool_args_unroutable.
    CHECK(has(dump, "responses.tool_open"));
    CHECK(has(dump, "item_id=fc_1"));
    CHECK(has(dump, "call_id=call_9"));
    CHECK(has(dump, "name=read"));

    // 运行:由哪家承运人交付字节,以及它们是否是新货.
    // 副驾驶上,这是单行 调用 总计=1(a) 快照 ,
    // 而不是一个碎片), 带有整个争论字符串。
    CHECK(has(dump, "responses.tool_args_routed"));
    CHECK(has(dump, "total=1"));

    // 结 论: 判决, 连同论点 调用 最终还是有的
    // -zero 这儿是一个健康的转弯。
    //
    // 匹配 完整 符号 `args=0 ` ,而不是子弦:`反应。
    // 完成 ` now also carries `salvaged_args=0 unroutable_args
    // =0`, and a bare ` 参数=0 ` matches inside those. The tool_closed line ends with `
    // args=N',所以锚在跟随站点自身场的空间上.
    CHECK(has(dump, "responses.tool_closed"));
    CHECK(!has(dump, " args=0\n"));
    CHECK(!has(dump, " args=0 "));
}

TEST_CASE("responses: the empty-arguments failure is visible in the log") {
    const auto dump = run_and_capture(kEmptyArgsTurn);

    // 电话 调用 已经宣布...
    CHECK(has(dump, "responses.tool_open"));

    // ...没有载体经过过 字节…
    CHECK(!has(dump, "responses.tool_args_routed"));

    // 以及最后没有结果的近距离记录 这三个事实都表明
    // "the server sent no arguments" ,（服务器没有发送参数。）
    // 它是一个不同的错误,来自 "we dropped them" 并
    // 指向不同的修正。没有这条线,两者就与错误报告无法区分。
    //
    // 被困在后方的新线上,这样不能不小心吻合 `salvaged_args=0`
    // / `unroutable_args=0` 已经完工的线路上
    CHECK(has(dump, "responses.tool_closed"));
    CHECK(has(dump, " args=0\n"));
}
