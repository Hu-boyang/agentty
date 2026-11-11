// wire_shared_test ——共享,单人 -source-of-truth 传输层 帮手们
//
// 提供商之后 -unification 通过了,两个关切 每 传输层 用来手 -roll
// (而且, DRIFTED (n) 一次在 传输 / 数据流-epilogue
// 头文件:
//
//   • provider::parse_retry_after(头文件 —— —— . 重试-After 退避 提示
//     解析器. Ollama 复制完全丢失( 它忽略了服务器) 退避 )),
//     现在全部四人. 传输层 调用 3⁄4 ̄ ̧漯B
//   • wire::scrub_utf8 / wire::is_valid_utf8 - 严格 UTF -8个验证,两个
//     传输层 (OpenAI, Ollama 携带a WEAKER 复制
//     了接受过长编码和代位编码的文本; 犬科的一号拒绝两者。
//   • wire::could_be_tool_json ——漏水者,(" ⁇ "). -tool-call 前缀嗅觉,即
//     OpenAI 财务报告和已审计财务报表 Ollama 传输层 每个打开 -coded (相同的逻辑,
//     漂移的白空间);现在一个分类器.
//   • usage::from_openai / from_responses / from_ollama ——三号标志 -usage
//     形状。 Ollama-native 复制对 -pasted 三个 时间。
//
// 这个 锁 未来编辑不能默默地重现的行为 -diverge.

#include <string>
#include <string_view>

#include "agtest.hpp"

#include <nlohmann/json.hpp>

#include "agentty/io/http.hpp"
#include "agentty/provider/error_class.hpp"
#include "agentty/provider/stream_epilogue.hpp"
#include "agentty/provider/usage.hpp"
#include "agentty/provider/wire.hpp"

using namespace agentty;
using agentty::provider::parse_retry_after;
namespace wire = agentty::provider::wire;

static http::Headers hdrs(std::initializer_list<std::pair<const char*, const char*>> kv) {
    http::Headers h;
    for (auto& [n, v] : kv) h.push_back({n, v});
    return h;
}

TEST_CASE("retry after") {
    // 整数-second整数 → 解析.
    {
        auto r = parse_retry_after(hdrs({{"retry-after", "42"}}));
        CHECK(r.has_value() && r->count() == 42);
    }
    // 大小写 -insensitive 头文件 名称(针对代理人进行辩护)。
    {
        auto r = parse_retry_after(hdrs({{"Retry-After", "7"}}));
        CHECK(r.has_value() && r->count() == 7);
    }
    // 缺席 头文件 − 无选择( = = 运行时 )),"回到自己的日程.
    {
        auto r = parse_retry_after(hdrs({{"content-type", "application/json"}}));
        CHECK(!r.has_value());
    }
    // 零被否决( a 0 -second 退避 ”。“““没有意义;退后”。
    {
        auto r = parse_retry_after(hdrs({{"retry-after", "0"}}));
        CHECK(!r.has_value());
    }
    // HTTP-date 形式是故意的 不 解析 = 无文本。
    {
        auto r = parse_retry_after(hdrs({{"retry-after", "Wed, 21 Oct 2015 07:28:00 GMT"}}));
        CHECK(!r.has_value());
    }
    // 整数 + 拒绝后拖曳垃圾( 必须全部消耗)。
    {
        auto r = parse_retry_after(hdrs({{"retry-after", "30s"}}));
        CHECK(!r.has_value());
    }
    // 第一次匹配的头文件获胜;扫描停止.
    {
        auto r = parse_retry_after(hdrs({{"retry-after", "5"}, {"retry-after", "99"}}));
        CHECK(r.has_value() && r->count() == 5);
    }
    // 空的头文件 设置 ~ 无空, 不崩溃。
    {
        auto r = parse_retry_after({});
        CHECK(!r.has_value());
    }
}

TEST_CASE("retry delay in the body: gemini sends no header (#102)") {
    using agentty::provider::parse_retry_after_body;
    using agentty::provider::parse_go_duration;
    using std::chrono::seconds;

    // 真实的 正文 从102( google.rpc.RetryInfo + 消息行).
    const std::string gemini = R"([{"error":{"code":429,"message":"You exceeded your current quota, please check your plan and billing details. * Quota exceeded for metric: generativelanguage.googleapis.com/generate_content_free_tier_requests, limit: 0, model: gemini-4-argon Please retry in 1h40m43.390047655s.","status":"RESOURCE_EXHAUSTED","details":[{"@type":"type.googleapis.com/google.rpc.QuotaFailure"},{"@type":"type.googleapis.com/google.rpc.RetryInfo","retryDelay":"6043s"}]}}])";
    CHECK(parse_retry_after_body(gemini) == seconds{6043});
    // 超过配额线 : 终端 ,而不是三个 10 -minute 等会
    CHECK(parse_retry_after_body(gemini)->count()
          > agentty::provider::kRetryAfterTerminalSeconds);

    // 消息 -only 窗体( 无) RetryInfo 详细情况)。
    CHECK(parse_retry_after_body("Please retry in 1h40m43.390047655s.") == seconds{6044});
    // 爆破的极限会停留很短的等待。
    CHECK(parse_retry_after_body(R"({"retryDelay": "43s"})") == seconds{43});
    CHECK(parse_retry_after_body("retry in 2.5s") == seconds{3});

    CHECK(parse_go_duration("500ms") == seconds{1});
    CHECK(parse_go_duration("2m") == seconds{120});
    CHECK(!parse_go_duration("soon"));
    CHECK(!parse_retry_after_body("rate limited"));
    CHECK(!parse_retry_after_body(R"({"retryDelay": ""})"));
}

TEST_CASE("scrub utf8 strict") {
    // 有效 ASCII + 多个 -byte 未经更改通过( 0) -copy 快速路径).
    CHECK(wire::is_valid_utf8("hello"));
    CHECK(wire::is_valid_utf8("caf\xC3\xA9"));           // 咖啡馆
    CHECK(wire::is_valid_utf8("\xE2\x9C\x93"));          // ✓ (U+2713)
    CHECK(wire::is_valid_utf8("\xF0\x9F\x9A\x80"));      // 🚀 (U+1F680)
    CHECK(wire::scrub_utf8("hello") == "hello");
    CHECK(wire::scrub_utf8("caf\xC3\xA9") == "caf\xC3\xA9");

    const std::string repl = "\xEF\xBF\xBD";   // U+FFFD

    // 仅一个续写字节无效 + 替换。
    CHECK(!wire::is_valid_utf8("\x80"));
    CHECK(wire::scrub_utf8("a\x80""b") == "a" + repl + "b");

    // 切换的多 -byte 结尾的序列 -of-string + 替换。
    // 字节 无法完成( 3 中只有 1) 字节 现今),故被替换,
    // 我们推进一个. 字节 ;孤儿的继位; 字节 改为“其他”
    CHECK(!wire::is_valid_utf8("\xE2\x9C"));
    CHECK(wire::scrub_utf8("\xE2\x9C") == repl + repl);

    // STRICT : a 数据 OVERLONG 编码 '/' (0xC0
    // 0xAF)必须拒绝——每名弱者 -byte-shape 检查
    // OpenAI/Ollama 使用过的副本 ACCEPTED 这是统一时的分歧
    CHECK(!wire::is_valid_utf8("\xC0\xAF"));
    CHECK(wire::scrub_utf8("\xC0\xAF") == repl + repl);

    // STRICT : 代号点( U+) D800 =0xED
    // 0xA0 0x80 必须拒绝——同样被老弱抄袭接受.
    CHECK(!wire::is_valid_utf8("\xED\xA0\x80"));
    CHECK(wire::scrub_utf8("\xED\xA0\x80") == repl + repl + repl);

    // STRICT : 超长 3 -byte NUL (0xE0 0x80 0x80) 拒绝.
    CHECK(!wire::is_valid_utf8("\xE0\x80\x80"));

    // 位于Unicode上限(0xF4 0x90...)上方 → 无效.
    CHECK(!wire::is_valid_utf8("\xF4\x90\x80\x80"));

    // 混合有效+无效:有效部件存活,仅替换坏字节.
    CHECK(wire::scrub_utf8("ok\xC3\xA9""\x80""done") == "ok\xC3\xA9" + repl + "done");
}

TEST_CASE("could be tool json") {
    // 模糊 / 仍然 -resolving 前缀 保留 OPEN (返回
    // 真实) 所以 传输层 一直持有而不是冲洗 半-formed 调用 作为传教词.
    CHECK(wire::could_be_tool_json(""));               // 还没
    CHECK(wire::could_be_tool_json("   "));            // 仅空白
    CHECK(wire::could_be_tool_json("{"));              // 赤裸的开口
    CHECK(wire::could_be_tool_json("{ "));             // 括号+ws
    CHECK(wire::could_be_tool_json("{\"name\""));       // 键入
    CHECK(wire::could_be_tool_json("{}"));             // 空对象
    CHECK(wire::could_be_tool_json("  {\"a\":1}"));      // 引线 ws + 对象
    CHECK(wire::could_be_tool_json("[{"));             // 对象数组
    CHECK(wire::could_be_tool_json("["));              // 单项
    CHECK(wire::could_be_tool_json("<tool_call>"));    // 完整标签
    CHECK(wire::could_be_tool_json("<tool"));          // 不完整的标记前缀
    CHECK(wire::could_be_tool_json("```"));            // 裸栏
    CHECK(wire::could_be_tool_json("```json"));        // JSON 栅栏
    CHECK(wire::could_be_tool_json("```\n{"));          // 栏杆然后对象

    // 从事的传教活动 CLOSES 无法成为工具的时刻
    // 调用 再来一次
    CHECK(!wire::could_be_tool_json("Sure, I'll do that"));
    CHECK(!wire::could_be_tool_json("{not_a_key"));    // `{` 则非 -key 字符
    CHECK(!wire::could_be_tool_json("```cpp"));        // 已执行的代码围栏
    CHECK(!wire::could_be_tool_json("<div>"));         // 否 <tool_call> 数据
    CHECK(!wire::could_be_tool_json("[1,2,3]"));       // JSON 缩略图数组
    CHECK(!wire::could_be_tool_json("hello {world}")); // 含有牙套的传言
}

TEST_CASE("usage extractors") {
    using nlohmann::json;
    namespace usage = agentty::provider::usage;

    // OpenAI /v1/chat/completions 形状。 cached_tokens
    // 谓 PART 备注 prompt_tokens ,因此它会移出输入:输入+ cache_read 仍然为
    // 100 -token 提示词 ,而不是164 (#96).
    {
        auto u = json::parse(R"({"prompt_tokens":100,"completion_tokens":40,
            "prompt_tokens_details":{"cached_tokens":64}})");
        auto su = usage::from_openai(u);
        CHECK(su.has_value());
        CHECK(su->input_tokens == 36);
        CHECK(su->output_tokens == 40);
        CHECK(su->cache_read_input_tokens == 64);
        CHECK(su->input_tokens + su->cache_read_input_tokens == 100);
    }
    // 一个报告比提示词更缓存的服务器被夹住,从不负.
    {
        auto su = usage::from_openai(json::parse(R"({"prompt_tokens":10,
            "prompt_tokens_details":{"cached_tokens":50}})"));
        CHECK(su.has_value());
        CHECK(su->input_tokens == 0);
        CHECK(su->cache_read_input_tokens == 10);
    }
    // completion_tokens_details下(o-series / gpt-5 / compat
    // exculation)的reasoning_tokens,表面分开;已经存在于completion_tokens内.
    {
        auto u = json::parse(R"({"prompt_tokens":10,"completion_tokens":200,
            "completion_tokens_details":{"reasoning_tokens":150}})");
        auto su = usage::from_openai(u);
        CHECK(su.has_value());
        CHECK(su->output_tokens == 200);
        CHECK(su->reasoning_output_tokens == 150);
    }
    // 缺少详细信息 ^ 0(non-reasoning型号).
    {
        auto su = usage::from_openai(json::parse(R"({"prompt_tokens":5,"completion_tokens":9})"));
        CHECK(su.has_value() && su->reasoning_output_tokens == 0);
    }
    // OpenAI : 无细节对象 ^ cache_read 保持0号状态。
    {
        auto su = usage::from_openai(json::parse(R"({"prompt_tokens":5,"completion_tokens":1})"));
        CHECK(su.has_value() && su->cache_read_input_tokens == 0);
    }
    // 全部( A) -zero / 空 / 无 缩略语( 匹配 传输层' "only sink if non-empty"
    // 警卫。
    CHECK(!usage::from_openai(json::object()).has_value());
    CHECK(!usage::from_openai(json::parse(R"({"prompt_tokens":0,"completion_tokens":0})")).has_value());
    // - 诺-object − 无头,没有扔。
    CHECK(!usage::from_openai(json("not-an-object")).has_value());

    // 答复/ Codex 形状 : input_tokens 是那个 整个 提示词
    // 这里也存在; 缓存和写有的符号是其中的一部分。
    {
        auto u = json::parse(R"({"input_tokens":200,"output_tokens":50,
            "input_tokens_details":{"cached_tokens":128}})");
        auto su = usage::from_responses(u);
        CHECK(su.has_value());
        CHECK(su->input_tokens == 72);
        CHECK(su->output_tokens == 50);
        CHECK(su->cache_read_input_tokens == 128);
    }
    {
        auto u = json::parse(R"({"input_tokens":200,
            "input_tokens_details":{"cached_tokens":120,"cache_write_tokens":30}})");
        auto su = usage::from_responses(u);
        CHECK(su.has_value());
        CHECK(su->input_tokens == 50);
        CHECK(su->cache_read_input_tokens == 120);
        CHECK(su->cache_creation_input_tokens == 30);
    }
    // reasoning_tokens 下级 output_tokens_details (Responses/Codex).
    {
        auto u = json::parse(R"({"input_tokens":20,"output_tokens":300,
            "output_tokens_details":{"reasoning_tokens":220}})");
        auto su = usage::from_responses(u);
        CHECK(su.has_value());
        CHECK(su->output_tokens == 300);
        CHECK(su->reasoning_output_tokens == 220);
    }
    CHECK(!usage::from_responses(json::object()).has_value());

    // Ollama 本地语言 DONE 帧 顶端 -level prompt_eval_count / eval_count.
    {
        auto frame = json::parse(R"({"done":true,"prompt_eval_count":300,"eval_count":80})");
        auto su = usage::from_ollama(frame);
        CHECK(su.has_value());
        CHECK(su->input_tokens == 300);
        CHECK(su->output_tokens == 80);
        CHECK(su->cache_read_input_tokens == 0);  // 本地没有缓存概念
    }
    CHECK(!usage::from_ollama(json::parse(R"({"done":true})")).has_value());
}


