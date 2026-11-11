// tests/context_window_robustness_test.cpp - 窗户,一个网关骗。
//
// 上下文窗口驱动显示器 和 自动 -compaction 所以错误的号码不是化妆
// 品错误: NEGATIVE 窗口完全禁用压缩(每个) `used < window`
// 测试为假),每个回合都有一个1个紧凑的窗口。 "agentty is broken"
// 而不是说
// "this gateway reported nonsense".
//
// 此字段没有字符串( Q) : OpenAI's /v1/models
// 完全没有窗口——见 conformance.hpp —— —— ,
// 所以每个价值都来自没有合同的第三方。提取必须生存下来。
//
// 2020 MEASURED FAILURES 上次缩小执行范围,它直接涉及: `v.get<int>()`
// / `static_cast<int>(uint64)` /`std::stoi` :
//
//   1844744073709551615 - > -1 臭虫代理机的int64
//   最大值 4000000000 - > -294967296 实数,
//   仅此而已 INT_MAX 1e18 - > -2147483648
//   浮点缩小, UB "1e6" - > 1 stoi 停止在
//   'e' "128000abc" - > 128 000 部分 解析 废字串
//
// GitHub 副驾驶 运行时 保留 max_context_window_tokens /
// max_prompt_tokens / max_output_tokens 以同样的
// 理由作为单独的验证限制:一个没有标签的,没有验证的号码是您以后不能调试的东西。

#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include "agentty/provider/openai/transport.hpp"

using json = nlohmann::json;
namespace det = agentty::provider::openai;

TEST_CASE("window: hostile numeric shapes yield 0, never garbage") {
    // 每一个都产生了一个错误的-but-plausible英寸以前。
    CHECK(det::advertised_context_window(
              json::parse(R"({"context_length":18446744073709551615})")) == 0);
    CHECK(det::advertised_context_window(
              json::parse(R"({"context_length":4000000000})")) == 0);
    CHECK(det::advertised_context_window(
              json::parse(R"({"context_length":1e18})")) == 0);
    CHECK(det::advertised_context_window(
              json::parse(R"({"context_length":-1})")) == 0);
    CHECK(det::advertised_context_window(
              json::parse(R"({"context_length":0})")) == 0);

    // 0表示"nothing advertised",即调用者变为默认.
    // 就是这样 仅 安全故障:默认显然是一种违约,
    // 而-294967296则无声无息地灾难性。
}

TEST_CASE("window: a partial string parse is a misread, not a lenient read") {
    // 一些代理字符串将数字元数据串成,所以字符串必须读取——但只有
    // 在WHOLE字符串是数字时才读取.
    CHECK(det::advertised_context_window(
              json::parse(R"({"context_length":"131072"})")) == 131072);

    // "1e6"曾经成为1——一个象征的窗口,每转一转就紧凑.
    CHECK(det::advertised_context_window(
              json::parse(R"({"context_length":"1e6"})")) == 0);
    CHECK(det::advertised_context_window(
              json::parse(R"({"context_length":"128000abc"})")) == 0);
    CHECK(det::advertised_context_window(
              json::parse(R"({"context_length":"not a number"})")) == 0);
    CHECK(det::advertised_context_window(
              json::parse(R"({"context_length":""})")) == 0);
}

TEST_CASE("window: implausible magnitudes are rejected at both ends") {
    // 1k以下为OUTPUT盖或文物,而非代理上下文窗口.
    // 将一个读作窗口是失败模式 梯子的注释 调用 输出 :
    // 给出看似合理的错误数值，而非明显失败。
    CHECK(det::advertised_context_window(
              json::parse(R"({"context_length":512})")) == 0);
    CHECK(det::advertised_context_window(
              json::parse(R"({"context_length":1})")) == 0);

    // 乐队的边缘本身被接受.
    CHECK(det::advertised_context_window(
              json::parse(R"({"context_length":1024})")) == 1024);

    // 100M以上不是2026年部署(最大运至~10M).
    CHECK(det::advertised_context_window(
              json::parse(R"({"context_length":999000000})")) == 0);
}

TEST_CASE("window: the real shapes still work") {
    // CAPTURED LIVE 发件人 https://yolo-auto.com/v1/models,
    // 2026-09-23. vLLM 携带两把钥匙;经核实为125 040枚 -token 提示词.
    CHECK(det::advertised_context_window(json::parse(
              R"({"id":"qwen3.8-flash","context_length":131072,
                  "max_model_len":131072})")) == 131072);

    // OpenRouter 巢筑着 DEPLOYMENT 窗口,
    // 它能战胜任何平滑键,因为它是特定路线会真正接受的.
    CHECK(det::advertised_context_window(json::parse(
              R"({"top_provider":{"context_length":200000},
                  "context_length":128000})")) == 200000);

    // LiteLLM的model_info块。
    CHECK(det::advertised_context_window(json::parse(
              R"({"model_info":{"max_input_tokens":1000000}})")) == 1000000);

    // llama.cpp 单位 -model 元数据 : n_ctx 是那个 SERVED
    // 窗口( 何为 -c 被设定为, n_ctx_train 火车 -time 最大。
    // 已经获得胜利。 —— 也就是这个请求的所在。 这是第49期: 没有它,
    // 8k 的本地模型就声称有 200k , 而压缩则从未发射过。
    CHECK(det::advertised_context_window(json::parse(
              R"({"meta":{"n_ctx":8192,"n_ctx_train":32768}})")) == 8192);

    // GGUF 拱门 -prefixed 键,就是模型本身的元数据拼写它的方式.
    CHECK(det::advertised_context_window(json::parse(
              R"({"qwen2.context_length":32768})")) == 32768);

    // 被勒了,就像几个代理的发音。
    CHECK(det::advertised_context_window(json::parse(
              R"({"max_model_len":"65536"})")) == 65536);
}

TEST_CASE("window: OUTPUT caps are never read as the window") {
    // max_tokens / max_output_tokens /
    // max_completion_tokens 是一个较小的大小顺序。
    // 读取一个,如窗口使代理 "forget" 经常在巨大的窗户上 看上去
    // 很合理 所以没人怀疑这个号码
    CHECK(det::advertised_context_window(
              json::parse(R"({"max_tokens":4096})")) == 0);
    CHECK(det::advertised_context_window(
              json::parse(R"({"max_output_tokens":8192})")) == 0);
    CHECK(det::advertised_context_window(
              json::parse(R"({"max_completion_tokens":16384})")) == 0);

    // . 不过... model_info.max_tokens IS 窗口打开 LiteLLM
    // 这就是为什么梯子读它在那里,只有在那里。
    CHECK(det::advertised_context_window(json::parse(
              R"({"model_info":{"max_tokens":128000}})")) == 128000);
}

TEST_CASE("window: a row that says nothing returns 0") {
    // OpenAI 属于自己的 / v1/models。
    // 缺席必须是代表性的,这样,呼叫者可以故意使用默认的,而不是发明数字。
    CHECK(det::advertised_context_window(json::parse(
              R"({"id":"gpt-4o","object":"model","owned_by":"openai"})")) == 0);
    CHECK(det::advertised_context_window(json::parse(R"({})")) == 0);
    CHECK(det::advertised_context_window(json::parse(R"([])")) == 0);
    CHECK(det::advertised_context_window(json::parse(R"(null)")) == 0);
}
