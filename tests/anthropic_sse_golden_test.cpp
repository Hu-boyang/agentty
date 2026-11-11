// anthropic_sse_golden_test — byte/shape-identity 守护着 Anthropic
// SSE 事件 解析器 (dispatch_event / feed_sse 途经 StreamCtx).
//
// 电话 SSE 解析器 正在被拆分出 transport.cpp 进入自己的模块。
// 分拆不能改变 agentty 给定的女士 SSE 数据流 生产。
// 这个测试脚本很现实 Anthropic 数据流 — message_start (
// +usage),带有deltas的文本内容块, a tool_use 块为
// input_json 三角洲, a message_delta 携带 stop_reason
// + 最后使用,以及 message_stop - 然后把一个峡谷钉上 渲染 排放的
// Msg。任何漂移在 解析 行为( 已删除) 事件 ,重新排队,改变场) 旅行它。
//
// 驱动器 parse_sse_for_test ,运行 相同 feed_sse → dispatch_event
// 路径 : on_chunk 使用时。

#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "agtest.hpp"

#include "agentty/provider/anthropic/transport.hpp"
#include "agentty/runtime/msg.hpp"

namespace {


using namespace agentty;
using agentty::msg::StreamMsg;

// 条形棋子 -line 渲染 编号 a 数据流 Msg. Msg 是一个 变体-of-variants:
// 编号 数据流 事件 被包裹为 Msg{StreamMsg{StreamXxx ~...
// ~ 所以我们剥了外皮 ~ Msg 成员名单 StreamMsg ,然后访问混凝土 事件 .
// 决定因素——抓捕歧视者+ 传输-relevant 字段,无时间 -dependent.
std::string render_stream(const StreamMsg& sm) {
    return std::visit([](const auto& e) -> std::string {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, StreamStarted>)
            return "Started";
        else if constexpr (std::is_same_v<T, StreamTextDelta>)
            return "TextDelta(" + e.text + ")";
        else if constexpr (std::is_same_v<T, StreamTextBlockClosed>)
            return "TextBlockClosed";
        else if constexpr (std::is_same_v<T, StreamToolUseStart>)
            return "ToolUseStart(" + e.id.value + "," + e.name.value + ")";
        else if constexpr (std::is_same_v<T, StreamToolUseDelta>)
            return "ToolUseDelta(" + e.id.value + "," + e.partial_json + ")";
        else if constexpr (std::is_same_v<T, StreamToolUseEnd>)
            return "ToolUseEnd(" + e.id.value + ")";
        else if constexpr (std::is_same_v<T, StreamThinkingDelta>)
            return "ThinkingDelta(" + e.text + "|" + e.signature + ")";
        else if constexpr (std::is_same_v<T, StreamUsage>)
            return "Usage(in=" + std::to_string(e.input_tokens)
                 + ",out=" + std::to_string(e.output_tokens)
                 + ",cc=" + std::to_string(e.cache_creation_input_tokens)
                 + ",cr=" + std::to_string(e.cache_read_input_tokens) + ")";
        else if constexpr (std::is_same_v<T, StreamFinished>)
            return "Finished(" + std::to_string(static_cast<int>(e.stop_reason)) + ")";
        else if constexpr (std::is_same_v<T, StreamError>)
            // 包含 http_status : 中间
            // -stream 错误 带类型的 状态是可重试
            // 超载和死转弯与 渲染器 使这个场变得无法
            // 测试——这正是场在被添加时如何失去说服力。
            return "Error(" + e.message + ",status="
                 + std::to_string(e.http_status) + ")";
        else if constexpr (std::is_same_v<T, StreamHeartbeat>)
            return "Heartbeat";
        else if constexpr (std::is_same_v<T, StreamBufferedWait>)
            return "BufferedWait";
        else
            return "OTHER";
    }, sm);
}

std::string render(const Msg& m) {
    if (const auto* sm = std::get_if<StreamMsg>(&m)) return render_stream(*sm);
    return "NON_STREAM";
}

std::string render_all(const std::vector<Msg>& msgs) {
    std::string s;
    for (const auto& m : msgs) { s += render(m); s += "\n"; }
    return s;
}

std::uint64_t fnv1a(const std::string& s) {
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    return h;
}

} // 命名空间结束。

TEST_CASE("anthropic sse golden") {
    namespace ap = agentty::provider::anthropic;

    // 一个现实 数据流 : 使用量 -bearing 开始,文本块, a tool_use 块,
    // 一个振动心跳,然后是三角形( stop_reason +使用和停止。
    const std::vector<std::pair<std::string, std::string>> events = {
        {"message_start",
         R"({"type":"message_start","message":{"usage":{"input_tokens":100,"output_tokens":1,"cache_creation_input_tokens":40,"cache_read_input_tokens":60}}})"},
        {"content_block_start",
         R"({"type":"content_block_start","index":0,"content_block":{"type":"text"}})"},
        {"content_block_delta",
         R"({"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"Hello "}})"},
        {"content_block_delta",
         R"({"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"world"}})"},
        {"content_block_stop",
         R"({"type":"content_block_stop","index":0})"},
        {"content_block_start",
         R"({"type":"content_block_start","index":1,"content_block":{"type":"tool_use","id":"toolu_01","name":"read"}})"},
        {"content_block_delta",
         R"({"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":"{\"path\":"}})"},
        {"content_block_delta",
         R"({"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":"\"a.cpp\"}"}})"},
        {"content_block_stop",
         R"({"type":"content_block_stop","index":1})"},
        {"ping", R"({"type":"ping"})"},
        {"message_delta",
         R"({"type":"message_delta","delta":{"stop_reason":"tool_use"},"usage":{"output_tokens":12}})"},
        {"message_stop", R"({"type":"message_stop"})"},
    };

    const std::vector<Msg> msgs = ap::parse_sse_for_test(events);
    const std::string rendered = render_all(msgs);

    // 特写-level 断言记录了预期的顺序.
    check(rendered.find("Started\n") != std::string::npos,
          "message_start emits Started");
    check(rendered.find("Usage(in=100,out=1,cc=40,cr=60)") != std::string::npos,
          "message_start usage is surfaced");
    check(rendered.find("TextDelta(Hello )") != std::string::npos
       && rendered.find("TextDelta(world)") != std::string::npos,
          "text deltas surface in order");
    check(rendered.find("TextBlockClosed\n") != std::string::npos,
          "text content_block_stop emits TextBlockClosed");
    check(rendered.find("ToolUseStart(toolu_01,read)") != std::string::npos,
          "tool_use content_block_start emits ToolUseStart");
    check(rendered.find("ToolUseDelta(toolu_01,{\"path\":)") != std::string::npos,
          "tool_use input_json deltas surface with the block's id");
    check(rendered.find("ToolUseEnd(toolu_01)") != std::string::npos,
          "tool_use content_block_stop emits ToolUseEnd");
    check(rendered.find("Heartbeat\n") != std::string::npos,
          "ping emits a (non-transport) heartbeat");
    check(rendered.find("Finished(") != std::string::npos,
          "message_stop emits Finished with the captured stop_reason");

    // 字节-identity 整个过程。
    const std::uint64_t kGoldenHash = 0x2cc3af5474193951ull;
    const std::uint64_t got = fnv1a(rendered);
    if (kGoldenHash == 0) {
        std::fprintf(stderr, "SSE GOLDEN hash = 0x%016llxull\n---\n%s---\n",
                     static_cast<unsigned long long>(got), rendered.c_str());
    } else {
        check(got == kGoldenHash, "SSE Msg sequence is byte-identical to golden");
        if (got != kGoldenHash)
            std::fprintf(stderr, "  expected 0x%016llx got 0x%016llx\n---\n%s---\n",
                         static_cast<unsigned long long>(kGoldenHash),
                         static_cast<unsigned long long>(got), rendered.c_str());
    }
}

TEST_CASE("anthropic error events carry their typed status") {
    namespace ap = agentty::provider::anthropic;

    // 中间 -stream `event: error` 到了
    // 200码内 正文 ——状态线早已不通. Anthropic
    // 键入失败的键入 `error.type` ,
    // 解码器必须作为等价符通过 HTTP 状态如此 重试 梯级
    // 通过 编译-time-proven 表格中没有嗅到人类的信息。
    //
    // 这里的散文是故意不熟悉的:没有底弦的嗅觉器识别。
    // 带类型的 此分类状态 终端 转弯会
    // 因服务器明确说的故障而死 重试.
    const std::vector<std::pair<std::string, std::string>> overloaded = {
        {"error",
         R"({"type":"error","error":{"type":"overloaded_error",)"
         R"("message":"unusually high demand right now"}})"},
    };

    std::string rendered = render_all(ap::parse_sse_for_test(overloaded));
    check(rendered.find("status=503") != std::string::npos,
          "overloaded_error surfaces as 503");
    check(rendered.find("unusually high demand right now") != std::string::npos,
          "the human message is preserved alongside the type");

    // 和一个利率限制, 另一个可重试。
    const std::vector<std::pair<std::string, std::string>> limited = {
        {"error",
         R"({"type":"error","error":{"type":"rate_limit_error",)"
         R"("message":"slow down"}})"},
    };
    rendered = render_all(ap::parse_sse_for_test(limited));
    check(rendered.find("status=429") != std::string::npos,
          "rate_limit_error surfaces as 429");

    // 未知类型表示NOXXXX OPINION:0 将
    // 状态更新函数留在其现有的字符串路径上,而不是发明分类。
    // 在自信的方向上让这个错误是未来的错误类型如何开始被
    // 错误的-retried。
    const std::vector<std::pair<std::string, std::string>> unknown = {
        {"error",
         R"({"type":"error","error":{"type":"some_future_error",)"
         R"("message":"who knows"}})"},
    };
    rendered = render_all(ap::parse_sse_for_test(unknown));
    check(rendered.find("status=0") != std::string::npos,
          "an unrecognised error type yields no opinion");
}

TEST_CASE("anthropic parallel tool blocks stay separate") {
    namespace ap = agentty::provider::anthropic;

    // 两位 tool_use 块块一开,他们的 input_json 三角洲
    // INTERLEAVED. Anthropic 写一个 `index` 时值
    // content_block 帧 ,这个指数是这个街区的 整个生命的特征: 开始,
    // 每个三角洲,停止。
    //
    // 用来保存的解码器 一个 `current_tool_id` 并引导每个
    // 三角洲到上次打开的区块—— `latest_tool_item` 形状
    // 从 docs/TOOL_CALL_ATTRIBUTION.md ,
    // 它已经作为错误在 Openai 中运出 -python 鱼线 鱼丝 鱼尾
    // 和两杯在这 数据流 它将块0的参数附加到块1的参数 调用 ,
    // 生产 JSON 仍然这样 解析 和一个工具运行在另一个 调用 路经.
    //
    // Claude今天一次发出一个阻断,所以旧代码通过
    // 运气而不是建筑是安全的,这消除了运气。
    const std::vector<std::pair<std::string, std::string>> events = {
        {"message_start",
         R"({"type":"message_start","message":{"usage":{"input_tokens":1}}})"},
        {"content_block_start",
         R"({"type":"content_block_start","index":0,"content_block":{"type":"tool_use","id":"toolu_A","name":"read"}})"},
        {"content_block_start",
         R"({"type":"content_block_start","index":1,"content_block":{"type":"tool_use","id":"toolu_B","name":"grep"}})"},
        // 互相干扰,故意不公开
        {"content_block_delta",
         R"({"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":"{\"pattern\":"}})"},
        {"content_block_delta",
         R"({"type":"content_block_delta","index":0,"delta":{"type":"input_json_delta","partial_json":"{\"path\":"}})"},
        {"content_block_delta",
         R"({"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":"\"b\"}"}})"},
        {"content_block_delta",
         R"({"type":"content_block_delta","index":0,"delta":{"type":"input_json_delta","partial_json":"\"a.txt\"}"}})"},
        {"content_block_stop", R"({"type":"content_block_stop","index":0})"},
        {"content_block_stop", R"({"type":"content_block_stop","index":1})"},
        {"message_stop", R"({"type":"message_stop"})"},
    };

    const std::string rendered = render_all(ap::parse_sse_for_test(events));

    // 每一个 调用 接收其 自身 字节。在修复之前,两者都累积到
    // toolu_B ——最后打开的块——和 toolu_A 什么都没有。
    check(rendered.find(R"(ToolUseDelta(toolu_A,{"path":))") != std::string::npos,
          "block 0's first delta lands on block 0's call");
    check(rendered.find(R"(ToolUseDelta(toolu_A,"a.txt"}))") != std::string::npos,
          "block 0's second delta lands on block 0's call");
    check(rendered.find(R"(ToolUseDelta(toolu_B,{"pattern":))") != std::string::npos,
          "block 1's first delta lands on block 1's call");
    check(rendered.find(R"(ToolUseDelta(toolu_B,"b"}))") != std::string::npos,
          "block 1's second delta lands on block 1's call");

    // 关键是: NO 交叉,1号街区 载荷 绝对
    // 不能出现在0号区名下, 接口边界 存在是为了预防,
    // 它也是无形的 下游 因为结果 解析.
    check(rendered.find(R"(ToolUseDelta(toolu_A,{"pattern":))") == std::string::npos,
          "block 1's bytes never land on block 0's call");
    check(rendered.find(R"(ToolUseDelta(toolu_B,{"path":))") == std::string::npos,
          "block 0's bytes never land on block 1's call");

    // 每个都关闭一次。停止名称是它自己的索引,
    // 所以它不能结束错误的调用, 也不能永远打开正确的一个。
    check(rendered.find("ToolUseEnd(toolu_A)") != std::string::npos,
          "block 0 ends");
    check(rendered.find("ToolUseEnd(toolu_B)") != std::string::npos,
          "block 1 ends");
}
