// codex_responses_test - 核查 ChatGPT (Codex (c) 反应
// -API 传输层 '纯翻译层, 无网络 :
//
//   1. build_body_for_test ——要求——答复 -API JSON (平面函数)
//                             工具、输入[ 项、 function_call +
//                             function_call_output 双人配对, reasoning/effort).
//   2. parse_sse_for_test ——按稿答复 SSE `data:` JSON – 已经 agentty
//                             Msg 顺序 状态更新函数 消耗(文本三角洲、推理、
//                             流化工具) -call 组装,
//                             response.completed → StopReason + 使用量).
//
// 运行: 构建`codex_responses_test`目标, 执行。 退出 0 = 通过。

#include <map>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "agtest.hpp"

#include "agentty/provider/chatgpt/responses.hpp"
#include "agentty/provider/error_class.hpp"

using namespace agentty;
namespace cc = agentty::provider::chatgpt;
using json = nlohmann::json;


template <class Leaf>
static const Leaf* leaf(const Msg& m) {
    const Leaf* found = nullptr;
    std::visit([&](const auto& domain) {
        std::visit([&](const auto& l) {
            if constexpr (std::is_same_v<std::decay_t<decltype(l)>, Leaf>)
                found = &l;
        }, domain);
    }, m);
    return found;
}

// ── 1. 要求正文 ─────────────────────────────────────────────────────────
// ── 1b. 每一 function_call 成对的,甚至一个从未完成的 ──────────
// 答复 API 400s "No tool output found for function call"
// 当一个 function_call 没有 function_call_output
// 历史每转一次 都重现 所以 已取消 调用 用来楔形 线程 永远
TEST_CASE("unfinished call is paired with a synthetic output") {
    provider::Request req;
    req.model = "gpt-5-codex";
    Message u; u.role = Role::User; u.text = "go";
    req.messages.push_back(u);
    Message a; a.role = Role::Assistant;
    for (int st = 0; st < 3; ++st) {
        ToolUse tc;
        tc.id = ToolCallId{"call_" + std::to_string(st)};
        tc.name = ToolName{"read"}; tc.args = json{{"path", "a"}};
        if (st == 0) tc.status = ToolUse::Done{.output = "ok"};
        else if (st == 1) tc.status = ToolUse::Pending{};
        else tc.status = ToolUse::Running{};
        a.tool_calls.push_back(tc);
    }
    req.messages.push_back(a);
    const json body = cc::build_body_for_test(req);
    std::map<std::string, int> calls, outputs;
    for (const auto& it : body["input"]) {
        if (it.value("type", "") == "function_call") ++calls[it["call_id"].get<std::string>()];
        if (it.value("type", "") == "function_call_output") ++outputs[it["call_id"].get<std::string>()];
    }
    CHECK(calls.size() == 3);
    for (const auto& [id, n] : calls) {
        INFO(id);
        CHECK(outputs[id] == 1);          // 每件产出 调用
    }
}

TEST_CASE("build body") {
    provider::Request req;
    req.model         = "gpt-5-codex";
    req.system_prompt = "You are a coding agent.";
    req.effort        = "high";

    Message u; u.role = Role::User; u.text = "list the files";
    req.messages.push_back(u);

    // 一个叫工具的助手转弯,结果完成.
    Message a; a.role = Role::Assistant; a.text = "";
    ToolUse tc;
    tc.id   = ToolCallId{"call_abc"};
    tc.name = ToolName{"shell"};
    tc.args = json{{"command", "ls"}};
    tc.status = ToolUse::Done{.output = "file1\nfile2"};
    a.tool_calls.push_back(tc);
    req.messages.push_back(a);

    provider::ToolSpec ts;
    ts.name         = "shell";
    ts.description  = "run a shell command";
    ts.input_schema = json{{"type", "object"},
                           {"properties", {{"command", {{"type", "string"}}}}}};
    req.tools.push_back(ts);

    json body = cc::build_body_for_test(req);

    CHECK(body["model"] == "gpt-5-codex");
    CHECK(body["instructions"] == "You are a coding agent.");
    CHECK(body["stream"] == true);
    CHECK(body["store"] == false);
    CHECK(body["tool_choice"] == "auto");
    CHECK(body["reasoning"]["effort"] == "high");

    // 工具为 FLAT (name/description/parameters )),在最高层.
    CHECK(body["tools"].is_array() && body["tools"].size() == 1);
    CHECK(body["tools"][0]["type"] == "function");
    CHECK(body["tools"][0]["name"] == "shell");
    CHECK(body["tools"][0].contains("parameters"));
    CHECK(!body["tools"][0].contains("function"));  // 不 筑巢 筑巢

    // 输入[ :用户消息,然后 function_call ,则 function_call_output.
    const auto& in = body["input"];
    CHECK(in.is_array() && in.size() == 3);
    CHECK(in[0]["type"] == "message");
    CHECK(in[0]["role"] == "user");
    CHECK(in[0]["content"][0]["type"] == "input_text");
    CHECK(in[0]["content"][0]["text"] == "list the files");

    CHECK(in[1]["type"] == "function_call");
    CHECK(in[1]["call_id"] == "call_abc");
    CHECK(in[1]["name"] == "shell");
    CHECK(json::parse(in[1]["arguments"].get<std::string>())["command"] == "ls");

    CHECK(in[2]["type"] == "function_call_output");
    CHECK(in[2]["call_id"] == "call_abc");
    CHECK(in[2]["output"] == "file1\nfile2");
}

TEST_CASE("build body with images") {
    provider::Request req;
    req.model = "gpt-5.4";

    Message with_text;
    with_text.role = Role::User;
    with_text.text = "what is this?";
    with_text.images.push_back(ImageContent{"image/png", std::string{"\x89PNG", 4}});
    req.messages.push_back(std::move(with_text));

    // 绝不能仅仅因为文本是空的而丢弃图像-only消息.
    Message image_only;
    image_only.role = Role::User;
    image_only.images.push_back(ImageContent{"image/jpeg", std::string{"\xff\xd8\xff", 3}});
    req.messages.push_back(std::move(image_only));

    const json body = cc::build_body_for_test(req);
    const auto& in = body["input"];
    CHECK(in.size() == 2);
    CHECK(in[0]["content"].size() == 2);
    CHECK(in[0]["content"][0]["type"] == "input_text");
    CHECK(in[0]["content"][0]["text"] == "what is this?");
    CHECK(in[0]["content"][1]["type"] == "input_image");
    CHECK(in[0]["content"][1]["image_url"] == "data:image/png;base64,iVBORw==");
    CHECK(in[1]["content"].size() == 1);
    CHECK(in[1]["content"][0]["type"] == "input_image");
    CHECK(in[1]["content"][0]["image_url"] == "data:image/jpeg;base64,/9j/");
}

// ── 2. SSE → Msg 绘图 ─────────────────────────────────────────────────────
TEST_CASE("sse text and usage") {
    std::vector<std::string> sse = {
        R"({"type":"response.created","response":{}})",
        R"({"type":"response.output_item.added","item":{"type":"message","role":"assistant"}})",
        R"({"type":"response.output_text.delta","delta":"Hello "})",
        R"({"type":"response.output_text.delta","delta":"world"})",
        R"({"type":"response.output_item.done","item":{"type":"message"}})",
        R"({"type":"response.completed","response":{"usage":{"input_tokens":12,"output_tokens":5,"input_tokens_details":{"cached_tokens":8}}}})",
    };
    auto msgs = cc::parse_sse_for_test(sse);

    std::string text;
    bool finished = false, saw_usage = false;
    StopReason stop = StopReason::Unspecified;
    for (const auto& m : msgs) {
        if (auto* d = leaf<StreamTextDelta>(m)) text += d->text;
        if (auto* f = leaf<StreamFinished>(m)) { finished = true; stop = f->stop_reason; }
        if (auto* u = leaf<StreamUsage>(m)) {
            saw_usage = true;
            CHECK(u->input_tokens == 4);      // 12-token 提示词, 其中8个缓存
            CHECK(u->output_tokens == 5);
            CHECK(u->cache_read_input_tokens == 8);
        }
    }
    CHECK(text == "Hello world");
    CHECK(finished);
    CHECK(stop == StopReason::EndTurn);
    CHECK(saw_usage);
}

TEST_CASE("sse tool call") {
    std::vector<std::string> sse = {
        R"({"type":"response.output_item.added","item":{"type":"function_call","id":"fc_1","call_id":"call_9","name":"read"}})",
        R"({"type":"response.function_call_arguments.delta","item_id":"fc_1","delta":"{\"path\":"})",
        R"({"type":"response.function_call_arguments.delta","item_id":"fc_1","delta":"\"a.txt\"}"})",
        R"({"type":"response.output_item.done","item":{"type":"function_call","id":"fc_1"}})",
        R"({"type":"response.completed","response":{"usage":{}}})",
    };
    auto msgs = cc::parse_sse_for_test(sse);

    std::string name, id, args;
    bool start = false, end = false, finished = false;
    StopReason stop = StopReason::Unspecified;
    for (const auto& m : msgs) {
        if (auto* s = leaf<StreamToolUseStart>(m)) { start = true; name = s->name.value; id = s->id.value; }
        if (auto* d = leaf<StreamToolUseDelta>(m)) {
            CHECK(d->id.value == "call_9");
            args += d->partial_json;
        }
        if (auto* e = leaf<StreamToolUseEnd>(m)) {
            CHECK(e->id.value == "call_9");
            end = true;
        }
        if (auto* f = leaf<StreamFinished>(m)) { finished = true; stop = f->stop_reason; }
    }
    CHECK(start);
    CHECK(name == "read");
    CHECK(id == "call_9");                 // 关联 id, 不是 fc_ 项 id
    CHECK(json::parse(args)["path"] == "a.txt");
    CHECK(end);
    CHECK(finished);
    CHECK(stop == StopReason::ToolUse);    // a function_call ⇒ ToolUse 停止( E)
}

TEST_CASE("sse parallel tool calls are id addressed") {
    std::vector<std::string> sse = {
        R"({"type":"response.output_item.added","item":{"type":"function_call","id":"fc_1","call_id":"call_1","name":"edit"}})",
        R"({"type":"response.function_call_arguments.delta","item_id":"fc_1","delta":"{\"path\":\"a.cpp\","})",
        R"({"type":"response.output_item.added","item":{"type":"function_call","id":"fc_2","call_id":"call_2","name":"shell"}})",
        R"({"type":"response.function_call_arguments.delta","item_id":"fc_2","delta":"{\"command\":\"true\"}"})",
        R"({"type":"response.function_call_arguments.delta","item_id":"fc_1","delta":"\"old_text\":\"x\",\"new_text\":\"y\"}"})",
        R"({"type":"response.output_item.done","item":{"type":"function_call","id":"fc_2"}})",
        R"({"type":"response.output_item.done","item":{"type":"function_call","id":"fc_1"}})",
        R"({"type":"response.completed","response":{"usage":{}}})",
    };

    std::map<std::string, std::string> args;
    std::map<std::string, int> ends;
    for (const auto& m : cc::parse_sse_for_test(sse)) {
        if (auto* d = leaf<StreamToolUseDelta>(m))
            args[d->id.value] += d->partial_json;
        if (auto* e = leaf<StreamToolUseEnd>(m))
            ++ends[e->id.value];
    }

    CHECK(json::parse(args["call_1"])["new_text"] == "y");
    CHECK(json::parse(args["call_2"])["command"] == "true");
    CHECK(ends["call_1"] == 1);
    CHECK(ends["call_2"] == 1);
}

TEST_CASE("sse an unaddressed delta with several calls open is not guessed") {
    // 答语方言通常会解决每个论点 事件 与 item_id 当它是 ABSENT
    // 解码器掉回 `latest_tool_item` ,它听起来是命令的,
    // 而不是命令的。它是从 `*open_tool_items.begin()`
    // 打开一个 unordered_set, i.e . .
    // 不论散列碰巧产生哪一项。
    //
    // 带两个 调用 打开那块硬币翻转, 失去的侧面附着一个
    // 调用 对方的论据。结果仍然是 解析 ,所以腐败是沉默的,
    // 并达到工具引用—— 同样的失败 聊天解码器
    // ToolCallTracker 拒绝通过返回Amibridge
    // 而不是采摘来实施犯罪。
    std::vector<std::string> sse = {
        R"({"type":"response.output_item.added","item":{"type":"function_call","id":"fc_1","call_id":"call_1","name":"edit"}})",
        R"({"type":"response.output_item.added","item":{"type":"function_call","id":"fc_2","call_id":"call_2","name":"shell"}})",
        // 没有 item_id ,两个 调用 开放——不可归属。
        R"({"type":"response.function_call_arguments.delta","delta":"{\"path\":\"x\"}"})",
        R"({"type":"response.output_item.done","item":{"type":"function_call","id":"fc_1"}})",
        R"({"type":"response.output_item.done","item":{"type":"function_call","id":"fc_2"}})",
        R"({"type":"response.completed","response":{"usage":{}}})",
    };

    std::map<std::string, std::string> args;
    for (const auto& m : cc::parse_sse_for_test(sse))
        if (auto* d = leaf<StreamToolUseDelta>(m))
            args[d->id.value] += d->partial_json;

    // 也没有 调用 可能接收到孤儿的碎片。丢弃它将是一个
    // 诚实的结果:在缺失的球场上转弯明显失败,而不是运行
    // `edit` 带有属于 `shell`.
    CHECK(args["call_1"].empty());
    CHECK(args["call_2"].empty());
}

TEST_CASE("sse an unaddressed delta with ONE call open still lands") {
    // 反省,为什么不能只是 "always require item_id":
    // 和一个 调用 飞行中不存在任何模糊之处,而老服务器会
    // 合法地忽略这个领域。拒绝这样做会破坏它们,从而无法安全
    // 地得到任何好处。
    std::vector<std::string> sse = {
        R"({"type":"response.output_item.added","item":{"type":"function_call","id":"fc_1","call_id":"call_1","name":"edit"}})",
        R"({"type":"response.function_call_arguments.delta","delta":"{\"path\":\"x\"}"})",
        R"({"type":"response.output_item.done","item":{"type":"function_call","id":"fc_1"}})",
        R"({"type":"response.completed","response":{"usage":{}}})",
    };

    std::map<std::string, std::string> args;
    for (const auto& m : cc::parse_sse_for_test(sse))
        if (auto* d = leaf<StreamToolUseDelta>(m))
            args[d->id.value] += d->partial_json;

    CHECK(json::parse(args["call_1"])["path"] == "x");
}

TEST_CASE("sse reasoning") {
    std::vector<std::string> sse = {
        R"({"type":"response.reasoning_summary_text.delta","delta":"thinking…"})",
        R"({"type":"response.completed","response":{"usage":{}}})",
    };
    auto msgs = cc::parse_sse_for_test(sse);
    std::string think;
    for (const auto& m : msgs)
        if (auto* t = leaf<StreamThinkingDelta>(m)) think += t->text;
    CHECK(think == "thinking…");
}

// 已完成推理输出项目产量 StreamReasoning 含不透明 encrypted_content
// —— 摇摆的 状态更新函数 用于交叉的藏品 -round 重放,我们来玩
TEST_CASE("sse reasoning encrypted capture") {
    std::vector<std::string> sse = {
        R"({"type":"response.reasoning_summary_text.delta","delta":"planning"})",
        R"({"type":"response.output_item.done","item":{"type":"reasoning","id":"rs_1","encrypted_content":"ENC_BLOB_123"}})",
        R"({"type":"response.completed","response":{"usage":{}}})",
    };
    auto msgs = cc::parse_sse_for_test(sse);
    std::string enc;
    for (const auto& m : msgs)
        if (auto* r = leaf<StreamReasoning>(m)) enc = r->encrypted;
    CHECK(enc == "ENC_BLOB_123");
}

// build_body 必须 OPT IN 以加密推理,然后后端返回。
TEST_CASE("build body requests encrypted reasoning") {
    provider::Request req;
    req.model = "gpt-5-codex";
    Message u; u.role = Role::User; u.text = "hi";
    req.messages.push_back(u);
    json body = cc::build_body_for_test(req);
    CHECK(body.contains("include"));
    CHECK(body["include"].is_array());
    bool has = false;
    for (const auto& x : body["include"])
        if (x == "reasoning.encrypted_content") has = true;
    CHECK(has);
}

// 助理转身 reasoning_encrypted 重播放一个 `reasoning` 项目 AHEAD 排 定 的 会 议
// function_call ——与 encrypted_content 财务报告和已审计财务报表 NO 服务器 ID。
TEST_CASE("build input replays reasoning") {
    provider::Request req;
    req.model = "gpt-5-codex";

    Message u; u.role = Role::User; u.text = "do it";
    req.messages.push_back(u);

    Message a; a.role = Role::Assistant; a.text = "";
    a.reasoning_encrypted = "BLOB_A\nBLOB_B";   // 将两个项目分成一个
    a.reasoning_site      = "chatgpt";          // 由THIS后端铸造
    ToolUse tc;
    tc.id = ToolCallId{"call_1"}; tc.name = ToolName{"shell"};
    tc.args = json{{"command", "ls"}};
    tc.status = ToolUse::Done{.output = "ok"};
    a.tool_calls.push_back(tc);
    req.messages.push_back(a);

    json body = cc::build_body_for_test(req);
    const auto& in = body["input"];
    // 用户, 推理( E) BLOB_A ),推理( BLOB_B), function_call ,产出
    CHECK(in.size() == 5);
    CHECK(in[0]["type"] == "message");
    CHECK(in[1]["type"] == "reasoning");
    CHECK(in[1]["encrypted_content"] == "BLOB_A");
    CHECK(!in[1].contains("id"));                 // 存储: 假冒下没有服务器 ID
    CHECK(in[2]["type"] == "reasoning");
    CHECK(in[2]["encrypted_content"] == "BLOB_B");
    CHECK(in[3]["type"] == "function_call");      // 逻辑在调用之前
    CHECK(in[4]["type"] == "function_call_output");
}

TEST_CASE("build input never replays another site's reasoning") {
    // 加密推理不透明 和 账户 -scoped : 只有它能解密的后端。
    // 发送一个副驾驶blob 到 ChatGPT 服务器忽略的字段不是。 整个 请求
    // 加密内容的 item_id 与目标条目标识不一致。
    // 开元72602,开元2号.
    // github/copilot-sdk#615).
    //
    // 重播在薄荷的场地上 上面刻着门的三
    // 种方法 套房里没有其他东西会注意到
    // 错的 -replayed 油井生产出完美的井 -formed 请求。
    const auto input_of = [](const Message& a) {
        provider::Request req;
        req.model = "gpt-5-codex";
        Message u; u.role = Role::User; u.text = "go";
        req.messages.push_back(u);
        req.messages.push_back(a);
        return cc::build_body_for_test(req)["input"];
    };
    const auto has_reasoning = [](const json& in) {
        for (const auto& item : in)
            if (item.value("type", "") == "reasoning") return true;
        return false;
    };

    Message a; a.role = Role::Assistant; a.text = "done";
    a.reasoning_encrypted = "BLOB";

    // 1. 由THIS网站进行消化——重播,这是整个特色.
    a.reasoning_site = "chatgpt";
    CHECK(has_reasoning(input_of(a)));

    // 2. 由ANOTHER网址拆卸,这是用户切换
    //    提供方为 ^P 中点 -thread ,这是支持的事情。
    a.reasoning_site = "copilot";
    CHECK(!has_reasoning(input_of(a)));

    // 3. UNTAGGED 也掉下来了 线程 在标签存在之前写入
    //    重新装入空址,以及 "we don't know who minted this"
    //    关闭失败。 -of-thought 旧的连续性 线
    //    程 成本没有明显值; 转弯成本为400。
    a.reasoning_site.clear();
    CHECK(!has_reasoning(input_of(a)));
}

TEST_CASE("sse error") {
    std::vector<std::string> sse = {
        R"({"type":"response.failed","response":{"error":{"message":"boom"}}})",
    };
    auto msgs = cc::parse_sse_for_test(sse);
    bool saw = false;
    for (const auto& m : msgs)
        if (auto* e = leaf<StreamError>(m)) { saw = true; CHECK(e->message == "boom"); }
    CHECK(saw);
}

// 进一进 -band 带有错误 `type`/`code` 必须在信件文本中显示,
// 所以 运行时's classify() 可以将其引导到自动 -retry
// (内部) Codex 与平价 Anthropic/OpenAI。没有类型,
// "Rate limited" 单是会保密的 终端 转弯会失败 而不是退缩
TEST_CASE("sse error type is retryable") {
    {
        std::vector<std::string> sse = {
            R"({"type":"response.failed","response":{"error":)"
            R"({"type":"rate_limit_exceeded","message":"Rate limited"}}})",
        };
        auto msgs = cc::parse_sse_for_test(sse);
        const StreamError* err = nullptr;
        for (const auto& m : msgs)
            if (auto* e = leaf<StreamError>(m)) err = e;
        CHECK(err != nullptr);
        if (err) {
            CHECK(err->message.find("rate_limit_exceeded") != std::string::npos);
            CHECK(provider::classify(err->message) == provider::ErrorClass::RateLimit);
        }
    }
    {
        // Top-level `error` 事件 具有瞬态的server_error型.
        std::vector<std::string> sse = {
            R"({"type":"error","code":"server_error","message":"upstream 503"})",
        };
        auto msgs = cc::parse_sse_for_test(sse);
        const StreamError* err = nullptr;
        for (const auto& m : msgs)
            if (auto* e = leaf<StreamError>(m)) err = e;
        CHECK(err != nullptr);
        if (err) {
            CHECK(err->message.find("server_error") != std::string::npos);
            CHECK(provider::classify(err->message) == provider::ErrorClass::Transient);
        }
    }
}

TEST_CASE("http error details") {
    CHECK(cc::format_http_error_for_test(
        400, R"({"error":{"type":"invalid_request_error","message":"input is too long"}})")
        == "invalid_request_error: input is too long");
    CHECK(cc::format_http_error_for_test(
        400, "event: error\ndata: {\"error\":\"context window exceeded\"}\n\n")
        == "context window exceeded");
    CHECK(cc::format_http_error_for_test(400, "plain edge rejection\n")
        == "Codex backend returned HTTP 400: plain edge rejection");
}

TEST_CASE("session id is stable per conversation") {
    // 与我谈话时的同声 相同 session_id 每转
    // ( 新的随机 ID ) , 每转都会击败后端 提示
    // 词缓存 衍生必须确定每个密钥,并且各密钥之间有所不同。
    const std::string a1 = cc::session_id_for_test("thread-A");
    const std::string a2 = cc::session_id_for_test("thread-A");
    const std::string b1 = cc::session_id_for_test("thread-B");
    CHECK(!a1.empty());
    CHECK(a1 == a2);          // 在一个交替的谈话中稳定
    CHECK(a1 != b1);          // 不同的对话不会碰撞
    CHECK(a1.size() == 36);   // uuid-v4 形状( 后端接受)
    CHECK(a1[14] == '4');     // 版本的硝化
    // 无持久身份 – 非-empty 随机标识( 校正于缓存)。
    CHECK(!cc::session_id_for_test("").empty());
}

TEST_CASE("stale tool result is budget capped") {
    // 一个巨大的工具输出 旧 调用 不得 逐个转弯(在飞机上重播)
    // 传输 最新的结果是保留了全部预算; 过期 成功者会消失到
    // 头部紧凑+ 末尾 通过共享
    // wire::cap_tool_result_aged 路径。
    provider::Request req;
    req.model = "gpt-5-codex";

    const std::string big(200000, 'x');   // 200 KiB 倾销

    // 最古老的调用:大结果,许多回转.
    Message a1; a1.role = Role::Assistant;
    ToolUse old_tc;
    old_tc.id = ToolCallId{"call_old"};
    old_tc.name = ToolName{"grep"};
    old_tc.args = json::object();
    old_tc.status = ToolUse::Done{.output = big};
    a1.tool_calls.push_back(old_tc);
    req.messages.push_back(a1);

    // 运行的更新工具 调用 所以,旧的 年龄过满 -result 窗口中选择一个窗口。
    for (int i = 0; i < 12; ++i) {
        Message a; a.role = Role::Assistant;
        ToolUse tc;
        tc.id = ToolCallId{"call_" + std::to_string(i)};
        tc.name = ToolName{"shell"};
        tc.args = json::object();
        tc.status = ToolUse::Done{.output = "ok"};
        a.tool_calls.push_back(tc);
        req.messages.push_back(a);
    }

    const json body = cc::build_body_for_test(req);
    const auto& in = body["input"];
    // 找最老的,找最老的那个 function_call_output (call_old 并声称它被封顶。
    std::string old_output;
    for (const auto& item : in) {
        if (item["type"] == "function_call_output"
            && item["call_id"] == "call_old") {
            old_output = item["output"].get<std::string>();
            break;
        }
    }
    CHECK(!old_output.empty());
    CHECK(old_output.size() < big.size());   // 淡化,不是逐字记录
    CHECK(old_output.size() < 8192);         // 紧头+ 末尾 预算
}


// ── 推理 切换 一定要努力解决 每个 方言语 ─────────────────────
//
// 报告针对副驾驶:当选择了gpt-* 模型时,
// 在模型面板上关闭推理仍然显示摘要,而 Claude
// 模型尊重它,分拆不是卖家,而是卖家 DIALECT.
// 副驾驶路线 gpt- * 超过 resignation 和
// claude- * 结束 Anthropic ,
// 以及其中四个 传输层 解码器是无条件的 解码程序。 切换 4条
// 道路之一没有执行,这更糟糕,因为它看起来是任意的。
TEST_CASE("sse reasoning is suppressed when the user hides it") {
    std::vector<std::string> sse = {
        R"({"type":"response.output_item.added","item":{"type":"reasoning","id":"r1"}})",
        R"({"type":"response.reasoning_summary_part.added","item_id":"r1"})",
        R"({"type":"response.reasoning_summary_text.delta","delta":"pondering…"})",
        R"({"type":"response.output_text.delta","delta":"the answer"})",
        R"({"type":"response.completed","response":{"usage":{}}})",
    };

    // 可见:摘要文本达到状态更新函数.
    std::string shown;
    for (const auto& m : cc::parse_sse_for_test(sse, /*show_reasoning=*/true))
        if (auto* t = leaf<StreamThinkingDelta>(m)) shown += t->text;
    CHECK(shown == "pondering…");

    // 隐藏: 没有 TEXT 的推理存储...
    std::string hidden;
    int heartbeats = 0, boundaries = 0;
    for (const auto& m : cc::parse_sse_for_test(sse, /*show_reasoning=*/false))
        if (auto* t = leaf<StreamThinkingDelta>(m)) {
            hidden += t->text;
            ++heartbeats;
            if (t->block_boundary) ++boundaries;
        }
    CHECK(hidden.empty());

    // ..但是,我们... 事件 仍然开火,因为轮到 LIVENESS 信号 :
    // 编号 状态更新函数 重排( E) last_event_at 编辑
    // 重试 完全吞下去,一个漫长的沉默推理阶段 看起来像一个停滞的
    // 数据流 并接受重审。
    CHECK(heartbeats > 0);
    // 但是,没有段落界限——没有文字的边界
    // 将打开记录稿中空洞的思维屏障。
    CHECK(boundaries == 0);
}

TEST_CASE("sse hidden reasoning does not disturb the answer") {
    // 回归护卫:压制推理不得触及数据流上的
    // 其他任何内容.
    std::vector<std::string> sse = {
        R"({"type":"response.reasoning_summary_text.delta","delta":"think"})",
        R"({"type":"response.output_text.delta","delta":"hello "})",
        R"({"type":"response.output_text.delta","delta":"world"})",
        R"({"type":"response.completed","response":{"usage":{"input_tokens":3,"output_tokens":2}}})",
    };
    std::string text;
    for (const auto& m : cc::parse_sse_for_test(sse, /*show_reasoning=*/false))
        if (auto* t = leaf<StreamTextDelta>(m)) text += t->text;
    CHECK(text == "hello world");
}
