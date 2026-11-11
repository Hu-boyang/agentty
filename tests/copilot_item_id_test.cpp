// copilot_item_id_test ——副驾驶代理重写. item_id ;位置不设.
//
// ── 用户日志中暴露的缺陷。 ─────────────────────────────────────────
//
// 每一个争论 -taking 工具失败 "[invalid args] path required"
// 零时 -argument 工具( 工具) list_dir, repo_map (已奏效。)
// 45种工具已公布,16种 调用 打开,并:
//
//     responses.tool_args_unroutable: item_id=+SqMJNRKVsofdaNo…  字节=
//     91 responses.tool_closed:          call_id=call_4D9 ... 参数=0
//
// 48个无法循规蹈矩 帧 16个 调用 —— 3人(正数) 调用 ,
// 每个辩论的载体各有一个(增加一个,即得分的),
// output_item.done ),所有内容相同 字节 数。所以服务器 DID
// 发来三遍的辩词 我们没有附上任何辩词 调用 关闭时以 args=0.
//
// 无法运行的item_ids是~440-char base64 blobs,
// 与服务器所宣布的没有匹配。 这是 Copilot 的加密推理内容 :
// 其代理重写 `item_id` 参数 帧 重写不圆 -trip . 其他地方的文献记录为开元
// 72602号( "Encrypted content item_id did not match the target item id" 和。
// 和 简 况 github/copilot-sdk
// #615.
//
// ── 原因 IT WAS SILENT ────────────────────────────────────────────────────
//
// addressed_item (a) 采取任何不采取 -empty
// item_id 重写 ID 是非 -empty ,所以它赢了,
// 并且因为它赢了, sole() 回退 本来可以引导一个单一 -call
// 转右 永远不跑。 检查缺失。 载荷 并投放工具,并随 `{}`.
//
// ── 该 RULE ─────────────────────────────────────────────────────────────
//
//     偏爱 output_index。一个 item_id 决意无非是噪音,
//     不是权威。
//
// output_index是响应输出[]阵列中的
// 项槽——一个代理身份不能重写而不重新编号它所描述
// 的阵列.

#include <string>
#include <vector>

#include "agtest.hpp"

#include "agentty/provider/chatgpt/responses.hpp"

using namespace agentty;
namespace cc = agentty::provider::chatgpt;

namespace {

template <class Leaf>
const Leaf* leaf(const Msg& m) {
    const Leaf* found = nullptr;
    std::visit([&](const auto& domain) {
        std::visit([&](const auto& l) {
            if constexpr (std::is_same_v<std::decay_t<decltype(l)>, Leaf>)
                found = &l;
        }, domain);
    }, m);
    return found;
}

// 解码器积蓄的每样东西 调用 编号.
std::string args_for(const std::vector<Msg>& msgs, std::string_view call_id) {
    std::string out;
    for (const auto& m : msgs)
        if (const auto* d = leaf<StreamToolUseDelta>(m))
            if (d->id.value == call_id) out += d->partial_json;
    return out;
}

// 双层副驾驶代用品 item_id - 形状 -accurate (长,
// base64 ),关键是它与未宣布的项目匹配。
const char* kEncryptedBlob =
    "gAAAAABn5kQx7VqLmN0pRz8sW3bYcJ4fHtKoP1dXeR6uM9aQwZ2iT5vN8hL0gB7yC3rE"
    "6mK9pJ4sX1nW8zQ2vY5tA0cF7bH3dG6kM1oP9rS4uE8wI2xT5yV0aN6lJ3qR7fD1gC4b"
    "H8mK2pX5sW9nZ1vY6tB0eG7cJ4dF3kL9oQ2rT5uI8wA1xM6yN0pS4vE7bH3gD6kR9fC2"
    "jL8mW5nX1sZ4tY0aB7eK3dG6oJ9pQ2rF5uH8iT1wM4xN7yV0cS3bE6gL9kD2fR5jP8mA";

}  // 命名空间结束。

TEST_CASE("copilot: arguments route by output_index when item_id is rewritten") {
    // 一个 调用 ,在《任择议定书》中提出的论点 `.done` 载体重写 item_id
    // ——原木上的确切形状. output_index 完全没有问题
    const std::vector<std::string> sse = {
        R"({"type":"response.output_item.added","output_index":0,"item":{"type":"function_call","id":"fc_real","call_id":"call_9","name":"read"}})",
        std::string{R"({"type":"response.function_call_arguments.done","output_index":0,"item_id":")"}
            + kEncryptedBlob + R"(","arguments":"{\"path\":\"a.txt\"}"})",
        R"({"type":"response.output_item.done","output_index":0,"item":{"type":"function_call","id":"fc_real"}})",
        R"({"type":"response.completed","response":{"usage":{}}})",
    };

    const auto msgs = cc::parse_sse_for_test(sse);

    // 辩论开始之前,这里是空的 `read` 失败的
    // "path required".
    CHECK(args_for(msgs, "call_9") == R"({"path":"a.txt"})");
}

TEST_CASE("copilot: parallel calls stay separate under rewritten item_ids") {
    // 使位置键入非 -obvious : 2个 调用 正在飞行中,
    // 两者都重写 ID。 获取错误的附件。 调用 其对
    // 他人的论据——有效 JSON 错误的工具,沉默。
    const std::string blob_a = std::string{kEncryptedBlob} + "AA";
    const std::string blob_b = std::string{kEncryptedBlob} + "BB";

    const std::vector<std::string> sse = {
        R"({"type":"response.output_item.added","output_index":0,"item":{"type":"function_call","id":"fc_a","call_id":"call_a","name":"read"}})",
        R"({"type":"response.output_item.added","output_index":1,"item":{"type":"function_call","id":"fc_b","call_id":"call_b","name":"grep"}})",
        // 争吵的到来不合秩序,每个都只按位置处理.
        R"({"type":"response.function_call_arguments.done","output_index":1,"item_id":")" + blob_b + R"(","arguments":"{\"pattern\":\"b\"}"})",
        R"({"type":"response.function_call_arguments.done","output_index":0,"item_id":")" + blob_a + R"(","arguments":"{\"path\":\"a.txt\"}"})",
        R"({"type":"response.output_item.done","output_index":0,"item":{"type":"function_call","id":"fc_a"}})",
        R"({"type":"response.output_item.done","output_index":1,"item":{"type":"function_call","id":"fc_b"}})",
        R"({"type":"response.completed","response":{"usage":{}}})",
    };

    const auto msgs = cc::parse_sse_for_test(sse);

    CHECK(args_for(msgs, "call_a") == R"({"path":"a.txt"})");
    CHECK(args_for(msgs, "call_b") == R"({"pattern":"b"})");
}

TEST_CASE("copilot: a rewritten item_id alone still routes a lone call") {
    // 任何地点都没有output_index,一个调用打开.
    // 旧代码将重写id当作权威,错过了,
    // 并放弃了载荷——sole()回退从没有机会,
    // 一个无所不能的ID不能压制它.
    const std::vector<std::string> sse = {
        R"({"type":"response.output_item.added","item":{"type":"function_call","id":"fc_real","call_id":"call_9","name":"read"}})",
        std::string{R"({"type":"response.function_call_arguments.done","item_id":")"}
            + kEncryptedBlob + R"(","arguments":"{\"path\":\"a.txt\"}"})",
        R"({"type":"response.output_item.done","item":{"type":"function_call","id":"fc_real"}})",
        R"({"type":"response.completed","response":{"usage":{}}})",
    };

    const auto msgs = cc::parse_sse_for_test(sse);

    CHECK(args_for(msgs, "call_9") == R"({"path":"a.txt"})");
}

TEST_CASE("copilot: a good item_id still wins over position") {
    // 修补工作不能倒退得那么快 -behaved 服务器。
    // 这里 item_id 正确且 output_index DISAGREES
    // 与之( 重新编号为 帧 ); id 名称为 真实打开 调用 ,所以必须使用。
    const std::vector<std::string> sse = {
        R"({"type":"response.output_item.added","output_index":0,"item":{"type":"function_call","id":"fc_a","call_id":"call_a","name":"read"}})",
        R"({"type":"response.output_item.added","output_index":1,"item":{"type":"function_call","id":"fc_b","call_id":"call_b","name":"grep"}})",
        // 位置表示1号档,但id明确命名fc_a.
        R"({"type":"response.function_call_arguments.done","item_id":"fc_a","arguments":"{\"path\":\"a.txt\"}"})",
        R"({"type":"response.output_item.done","output_index":0,"item":{"type":"function_call","id":"fc_a"}})",
        R"({"type":"response.output_item.done","output_index":1,"item":{"type":"function_call","id":"fc_b"}})",
        R"({"type":"response.completed","response":{"usage":{}}})",
    };

    const auto msgs = cc::parse_sse_for_test(sse);

    CHECK(args_for(msgs, "call_a") == R"({"path":"a.txt"})");
    CHECK(args_for(msgs, "call_b").empty());
}

TEST_CASE("copilot: an unknown position with several calls open is not guessed") {
    // 语言的规范 : 当服务器命名时, 我们没有,
    // 并且不止一个 调用 打开, 没有诚实的答案。
    // 丢弃碎片是正确的; 选择一个附件 调用's
    // 字节 仍然在 解析.
    //
    // 这也是打捞(以下)的界限。 只有在打捞不错误的情况下,
    // 才能恢复——完全是一个候选人。 两个候选人
    // 是硬币翻转,以及生产有效的硬币翻翻。 JSON
    // 比一个明显断裂的转弯还糟糕
    const std::vector<std::string> sse = {
        R"({"type":"response.output_item.added","output_index":0,"item":{"type":"function_call","id":"fc_a","call_id":"call_a","name":"read"}})",
        R"({"type":"response.output_item.added","output_index":1,"item":{"type":"function_call","id":"fc_b","call_id":"call_b","name":"grep"}})",
        // 7号机组从未公布过,ID是重写blob.
        std::string{R"({"type":"response.function_call_arguments.done","output_index":7,"item_id":")"}
            + kEncryptedBlob + R"(","arguments":"{\"path\":\"x\"}"})",
        R"({"type":"response.completed","response":{"usage":{}}})",
    };

    const auto msgs = cc::parse_sse_for_test(sse);

    CHECK(args_for(msgs, "call_a").empty());
    CHECK(args_for(msgs, "call_b").empty());
}

TEST_CASE("copilot: unroutable args are salvaged when exactly one call is open") {
    // GitHub 客户船 `copilot_salvaged_tool_input_ids` 遥测中:
    // 它重建无法路由的工具输入,并报告它必须多久建一次。
    // 除非它用自己的服务器在生产过程中开火,否则它不会建起来 —
    // — 因此,它会为一个代理服务器的丢失提出论据。 传输 是一个
    // EXPECTED 州,不是特别的州。
    //
    // 需要这个的载体是 `output_item.done` 其论点在项目中
    // 得到了处理。 自身 `id` 字段,而不是通过 addressed_item (,
    // 所以 sole()-fallback 拯救了我们 `.delta`
    // /`.done` 承运人不适用。如果代理重写了这里的ID,
    // 那么它会做什么 item_id 参数 帧 - 电话 载荷 拥有者与
    // 调用 发送日期 `{}`.
    //
    // 一个 调用 打开意味着有一件事 字节
    // 无法找到错误的归属 并收回
    const std::vector<std::string> sse = {
        R"({"type":"response.output_item.added","item":{"type":"function_call","id":"fc_real","call_id":"call_9","name":"read"}})",
        // 已完成的项目在REWRITTEN id中重申了这些论点。
        std::string{R"({"type":"response.output_item.done","item":{"type":"function_call","id":")"}
            + kEncryptedBlob + R"(","arguments":"{\"path\":\"a.txt\"}"}})",
        R"({"type":"response.completed","response":{"usage":{}}})",
    };

    const auto msgs = cc::parse_sse_for_test(sse);

    // 而不救活这个 `read` 发送日期 `{}` —
    // "[invalid args] path required" ——而辩论则在日志中进行.
    CHECK(args_for(msgs, "call_9") == R"({"path":"a.txt"})");
}

TEST_CASE("copilot: salvage refuses rather than guessing between two calls") {
    // 同样的无法循路的载荷,但现在有两座调用是开放的. Salvage
    // must 不 火:任意选择产生JSON,即解析和发送,
    // 所以错误的猜测是沉默的腐败——`read`运行时带有`grep`的论点,
    // 或者相反.
    //
    // 拒绝成本一个断掉的转弯,在日志中是这样说的。 猜测成本是一个
    // 工具运行在输入错误上, 而没有人更明智。 不对称是整个原因。
    // OpenCalls::sole () 返回无选择
    // "many" 而不是把第一个元素还回去。
    const std::vector<std::string> sse = {
        R"({"type":"response.output_item.added","item":{"type":"function_call","id":"fc_a","call_id":"call_a","name":"read"}})",
        R"({"type":"response.output_item.added","item":{"type":"function_call","id":"fc_b","call_id":"call_b","name":"grep"}})",
        std::string{R"({"type":"response.output_item.done","item":{"type":"function_call","id":"unknown_)"}
            + kEncryptedBlob + R"(","arguments":"{\"path\":\"a.txt\"}"}})",
        R"({"type":"response.completed","response":{"usage":{}}})",
    };

    const auto msgs = cc::parse_sse_for_test(sse);

    // 也没有 调用 受孤儿的优待; 字节.
    CHECK(args_for(msgs, "call_a").empty());
    CHECK(args_for(msgs, "call_b").empty());
}
