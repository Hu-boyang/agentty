// ollama_transport_test ——核实献身者. Ollama /api/chat
// 传输层 纯翻译层, 带有 NO 网络 :
//
//   1. build_messages    — 线程 信件 – 本地信件阵列
//                          (助理人员) tool_calls 与 OBJECT 参数+ id,
//                          单独作用 : "tool" 结果 tool_name ,
//                          用户图像为 base64 数组).
//   2. parse_ndjson_for_test —已编稿 NDJSON 帧 – 已经 agentty Msg
//                          顺序 状态更新函数 消费(文本三角洲, STRUCTURED
//                          tool_calls → Start/Delta/End ,使用时,
//                          done_reason→StopReason). NO 内容 -salvage:
//                          内容中的 JSON 物体仍为流派。
//   3. system_prompt——包含提醒+env块.
//
// 运行: 构建`ollama_transport_test`目标, 执行。 退出 0 = 通过。

#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "agtest.hpp"

#include "agentty/provider/ollama/transport.hpp"

using namespace agentty;
namespace oll = agentty::provider::ollama;


template <class Leaf>
const Leaf* get_leaf(const Msg& m) {
    const Leaf* found = nullptr;
    std::visit([&](const auto& domain) {
        std::visit([&](const auto& leaf) {
            if constexpr (std::is_same_v<std::decay_t<decltype(leaf)>, Leaf>)
                found = &leaf;
        }, domain);
    }, m);
    return found;
}

template <class Leaf>
int count_leaf(const std::vector<Msg>& msgs) {
    int n = 0;
    for (const auto& m : msgs) if (get_leaf<Leaf>(m)) ++n;
    return n;
}

static std::string joined_text(const std::vector<Msg>& msgs) {
    std::string s;
    for (const auto& m : msgs)
        if (const auto* d = get_leaf<StreamTextDelta>(m)) s += d->text;
    return s;
}

// 合并推理文本(统一 StreamThinkingDelta
// 事件 每一个供应商都发出。 Ollama 表面原生 + 内含推理。
static std::string joined_thinking(const std::vector<Msg>& msgs) {
    std::string s;
    for (const auto& m : msgs)
        if (const auto* d = get_leaf<StreamThinkingDelta>(m)) s += d->text;
    return s;
}

static std::string joined_tool_args(const std::vector<Msg>& msgs);

// ── 1. build_messages ────────────────────────────────────────────────────────
TEST_CASE("build messages text") {
    std::vector<Message> msgs;
    Message u; u.role = Role::User; u.text = "hello"; msgs.push_back(u);
    Message a; a.role = Role::Assistant; a.text = "hi there"; msgs.push_back(a);

    auto arr = oll::build_messages(msgs);
    CHECK(arr.is_array());
    CHECK(arr.size() == 2);
    CHECK(arr[0]["role"] == "user");
    CHECK(arr[0]["content"] == "hello");
    CHECK(arr[1]["role"] == "assistant");
    CHECK(arr[1]["content"] == "hi there");
}

TEST_CASE("build messages tool calls") {
    // 带一个助理消息 终端 工具 调用 = 助理Msg 携带设备 tool_calls
    // [{id, 函数 :{name, 参数 : OBJECT}}] 和 单独的
    // 角色: "tool" 信件为 tool_name +内容随附.
    std::vector<Message> msgs;
    Message a;
    a.role = Role::Assistant;
    a.text = "";
    ToolUse tc;
    tc.id   = ToolCallId{"call_1"};
    tc.name = ToolName{"read"};
    tc.args = nlohmann::json{{"path", "/etc/hostname"}};
    tc.status = ToolUse::Done{{}, {}, "myhost"};
    a.tool_calls.push_back(std::move(tc));
    msgs.push_back(std::move(a));

    auto arr = oll::build_messages(msgs);
    CHECK(arr.size() == 2);
    // 助理消息
    CHECK(arr[0]["role"] == "assistant");
    CHECK(arr[0].contains("tool_calls"));
    CHECK(arr[0]["tool_calls"].size() == 1);
    CHECK(arr[0]["tool_calls"][0]["id"] == "call_1");
    CHECK(arr[0]["tool_calls"][0]["function"]["name"] == "read");
    // 参数必须是JSON OBJECT,而不是序列化字符串
    CHECK(arr[0]["tool_calls"][0]["function"]["arguments"].is_object());
    CHECK(arr[0]["tool_calls"][0]["function"]["arguments"]["path"]
          == "/etc/hostname");
    // 工具结果消息
    CHECK(arr[1]["role"] == "tool");
    CHECK(arr[1]["tool_name"] == "read");
    CHECK(arr[1]["content"] == "myhost");
}

// 年龄 -tiered 工具 -result 清除( 分摊) wire::cap_tool_result_aged。
// 本地模型有 SMALLEST 上下文窗口,所以 过期 60 KiB 垃圾场的重播
// 每一转都最疼这里,锻炼 两者 本地角色: "tool" 路径和 JSON-protocol
// "TOOL RESULT (name):" 用户 -turn 路径。
TEST_CASE("build messages age tiering") {
    std::string big = "HEAD_SENTINEL_AAAA\n";
    big.append(60 * 1024, 'x');
    big += "\nTAIL_SENTINEL_ZZZZ";

    const int n = 20;
    auto make = [&]() {
        std::vector<Message> msgs;
        Message u; u.role = Role::User; u.text = "start"; msgs.push_back(u);
        for (int i = 0; i < n; ++i) {
            Message a; a.role = Role::Assistant; a.text = "c";
            ToolUse tc;
            tc.id   = ToolCallId{"call_" + std::to_string(i)};
            tc.name = ToolName{"grep"};
            tc.status = ToolUse::Done{{}, {}, big};
            a.tool_calls.push_back(std::move(tc));
            msgs.push_back(std::move(a));
        }
        return msgs;
    };

    // 原生路径:结果为角色: "tool" 信息,按发件顺序(最老的顺序)。
    // 首次 工具消息为最古老的( 变速); ; 最后 最新消息 已经传出去了
    {
        auto arr = oll::build_messages(make(), /*json_protocol=*/false);
        std::string oldest, newest;
        for (const auto& msg : arr)
            if (msg.value("role", "") == "tool") {
                if (oldest.empty()) oldest = msg.value("content", "");
                newest = msg.value("content", "");
            }
        CHECK(newest.size() > 40 * 1024);
        CHECK(oldest.size() < 6 * 1024);
        CHECK(oldest.find("bytes elided") != std::string::npos);
        CHECK(oldest.find("HEAD_SENTINEL_AAAA") != std::string::npos);
        CHECK(oldest.find("TAIL_SENTINEL_ZZZZ") != std::string::npos);
    }

    // JSON-protocol 路径: 结果是角色 : "user" "TOOL RESULT (name):" 转弯
    {
        auto arr = oll::build_messages(make(), /*json_protocol=*/true);
        std::string oldest, newest;
        for (const auto& msg : arr)
            if (msg.value("role", "") == "user") {
                auto c = msg.value("content", "");
                if (c.rfind("TOOL RESULT", 0) != 0) continue;
                if (oldest.empty()) oldest = c;
                newest = c;
            }
        CHECK(newest.size() > 40 * 1024);
        CHECK(oldest.size() < 6 * 1024);
        CHECK(oldest.find("bytes elided") != std::string::npos);
    }
}

TEST_CASE("build messages images") {
    std::vector<Message> msgs;
    Message u; u.role = Role::User; u.text = "what is this?";
    ImageContent img; img.media_type = "image/png"; img.set_bytes("abc");
    u.images.push_back(std::move(img));
    msgs.push_back(std::move(u));

    auto arr = oll::build_messages(msgs);
    CHECK(arr.size() == 1);
    CHECK(arr[0].contains("images"));
    CHECK(arr[0]["images"].is_array());
    CHECK(arr[0]["images"].size() == 1);
    CHECK(arr[0]["images"][0].is_string());
}

// ── 2. parse_ndjson ──────────────────────────────────────────────────────────
TEST_CASE("ndjson plain text") {
    std::string nd =
        "{\"message\":{\"role\":\"assistant\",\"content\":\"Hello \"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\"Ayush\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\"\"},"
        "\"done\":true,\"done_reason\":\"stop\","
        "\"prompt_eval_count\":10,\"eval_count\":3}\n";
    auto msgs = oll::parse_ndjson_for_test(nd);
    CHECK(joined_text(msgs) == "Hello Ayush");
    CHECK(count_leaf<StreamFinished>(msgs) == 1);
    CHECK(count_leaf<StreamUsage>(msgs) == 1);
    // 没有工具调用泄露.
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 0);
}

// 本地推理: Ollama's `message.thinking` 字段( 当我们发送思考时为人所
// 聚集: true) 必须显示为统一 StreamThinkingDelta 事件 - 电话 相同
// 事件 Anthropic/OpenAI-compat/ChatGPT 发出——必须发出—— 不 渗入
// 可见的流言
TEST_CASE("ndjson native thinking field") {
    const std::string nd =
        "{\"message\":{\"role\":\"assistant\",\"thinking\":\"Let me plan. \","
        "\"content\":\"\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"thinking\":\"Step two.\","
        "\"content\":\"\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\"The answer.\"},"
        "\"done\":true,\"done_reason\":\"stop\"}\n";
    auto msgs = oll::parse_ndjson_for_test(nd);
    CHECK(joined_thinking(msgs) == "Let me plan. Step two.");
    CHECK(joined_text(msgs) == "The answer.");
    // 理性从不作为散文出现.
    CHECK(joined_text(msgs).find("plan") == std::string::npos);
}

// 内在推理 回退 : 倾销链的模型 -of-thought 输入 <think>
// …</think> 输入 `content` (深处) -r1 , qwen3 没有
// 原生田) , 必须有它 STRIPPED 从散文 和 被证明是推理
TEST_CASE("ndjson inline think block surfaced as reasoning") {
    const std::string nd =
        "{\"message\":{\"role\":\"assistant\","
        "\"content\":\"<think>weigh options</think>Final answer.\"},"
        "\"done\":true,\"done_reason\":\"stop\"}\n";
    auto msgs = oll::parse_ndjson_for_test(nd);
    CHECK(joined_text(msgs) == "Final answer.");
    // 推理呈表面(统一为事件),没有丢弃.
    CHECK(joined_thinking(msgs).find("weigh options") != std::string::npos);
    // 亦不漏入道场.
    CHECK(joined_text(msgs).find("weigh") == std::string::npos);
    CHECK(joined_text(msgs).find("<think>") == std::string::npos);
}

TEST_CASE("ndjson structured tool call") {
    std::string nd =
        "{\"message\":{\"role\":\"assistant\",\"content\":\"\","
        "\"tool_calls\":[{\"id\":\"call_x\",\"function\":{\"name\":\"read\","
        "\"arguments\":{\"path\":\"/tmp/f\"}}}]},"
        "\"done_reason\":\"stop\",\"done\":true}\n";
    auto msgs = oll::parse_ndjson_for_test(nd);
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 1);
    CHECK(count_leaf<StreamToolUseEnd>(msgs) == 1);
    // 参数对象以 delta 载荷 转发.
    bool saw_args = false;
    for (const auto& m : msgs)
        if (const auto* d = get_leaf<StreamToolUseDelta>(m))
            if (d->partial_json.find("/tmp/f") != std::string::npos) saw_args = true;
    CHECK(saw_args);
    // StopReason 应该 ToolUse.
    bool tool_stop = false;
    for (const auto& m : msgs)
        if (const auto* f = get_leaf<StreamFinished>(m))
            if (f->stop_reason == StopReason::ToolUse) tool_stop = true;
    CHECK(tool_stop);
}

TEST_CASE("ndjson content salvage to tool") {
    // 弱型号会泄露工具 调用 赤裸裸的 JSON 输入 CONTENT
    // 工具被公布后,它被打捞成 STRUCTURED 调用 (不显示为文本).
    std::string nd =
        "{\"message\":{\"role\":\"assistant\",\"content\":"
        "\"{\\\"name\\\":\\\"write\\\",\\\"arguments\\\":"
        "{\\\"file_path\\\":\\\"/tmp/f\\\"}}\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\"\"},"
        "\"done\":true,\"done_reason\":\"stop\"}\n";
    auto msgs = oll::parse_ndjson_for_test(nd, {"write", "read"});
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 1);
    CHECK(count_leaf<StreamToolUseEnd>(msgs) == 1);
    bool saw_args = false;
    for (const auto& m : msgs)
        if (const auto* d = get_leaf<StreamToolUseDelta>(m))
            if (d->partial_json.find("/tmp/f") != std::string::npos) saw_args = true;
    CHECK(saw_args);
    // 牺牲 JSON 不得 也作为可见的散文出现.
    CHECK(joined_text(msgs).find("\"name\"") == std::string::npos);
}

TEST_CASE("ndjson no tools means no salvage") {
    // 与 NO 宣传工具, JSON-looking 内容是纯
    // 文本——没有可挽回的,所以不要吞下合法的散文.
    std::string nd =
        "{\"message\":{\"role\":\"assistant\",\"content\":"
        "\"{\\\"name\\\":\\\"read\\\",\\\"arguments\\\":{}}\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\"\"},"
        "\"done\":true,\"done_reason\":\"stop\"}\n";
    auto msgs = oll::parse_ndjson_for_test(nd);   // 无known_tools
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 0);
    CHECK(joined_text(msgs).find("\"name\"") != std::string::npos);
}

TEST_CASE("ndjson footgun tool swallowed") {
    // 漏报 `remember` 调用 (脚枪工具)是 绝不 自动
    // -run 和 从来没有像垃圾一样出现——它被默默吞没。
    std::string nd =
        "{\"message\":{\"role\":\"assistant\",\"content\":"
        "\"{\\\"name\\\":\\\"remember\\\",\\\"arguments\\\":"
        "{\\\"text\\\":\\\"hi\\\"}}\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\"\"},"
        "\"done\":true,\"done_reason\":\"stop\"}\n";
    auto msgs = oll::parse_ndjson_for_test(nd, {"remember", "write"});
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 0);   // 不运行
    CHECK(joined_text(msgs).find("remember") == std::string::npos);  // 未显示
}

TEST_CASE("ndjson explicit memory tool salvaged") {
    std::string nd =
        "{\"message\":{\"role\":\"assistant\",\"content\":"
        "\"{\\\"name\\\":\\\"remember\\\",\\\"arguments\\\":"
        "{\\\"text\\\":\\\"hi\\\"}}\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\"\"},"
        "\"done\":true,\"done_reason\":\"stop\"}\n";
    auto msgs = oll::parse_ndjson_for_test(
        nd, {"remember", "write"}, /*json_protocol=*/false,
        /*allow_memory_salvage=*/true);
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 1);
    CHECK(count_leaf<StreamToolUseEnd>(msgs) == 1);
    CHECK(joined_tool_args(msgs) == std::string{"{\"text\":\"hi\"}"});
}

TEST_CASE("ndjson rescue tool from prose") {
    // qwen 同时输出叙述文字，并把工具调用藏在 ```sh 围栏块中。
    // 终端 救援扫描必须找到它 并把它救活成一个真实的 调用.
    std::string nd =
        "{\"message\":{\"role\":\"assistant\",\"content\":"
        "\"Sure, I'll rename it.\\n\\n```sh\\n"
        "{\\\"name\\\": \\\"bash\\\", \\\"arguments\\\": "
        "{\\\"command\\\":\\\"mv /a /b\\\"}}\\n```\\nDone.\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\"\"},"
        "\"done\":true,\"done_reason\":\"stop\"}\n";
    auto msgs = oll::parse_ndjson_for_test(nd, {"shell", "write"});
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 1);
    bool saw_args = false;
    for (const auto& m : msgs)
        if (const auto* d = get_leaf<StreamToolUseDelta>(m))
            if (d->partial_json.find("mv /a /b") != std::string::npos) saw_args = true;
    CHECK(saw_args);
}

TEST_CASE("ndjson unknown tool in prose not salvaged") {
    // 栅栏状块命名为 NON-advertised 工具(qwen 幻觉)
    // `git` )不得 被拯救——没有这样的工具 调用.
    std::string nd =
        "{\"message\":{\"role\":\"assistant\",\"content\":"
        "\"```sh\\n{\\\"name\\\": \\\"git\\\", \\\"arguments\\\": "
        "{\\\"command\\\":\\\"mv /a /b\\\"}}\\n```\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\"\"},"
        "\"done\":true,\"done_reason\":\"stop\"}\n";
    auto msgs = oll::parse_ndjson_for_test(nd, {"shell", "write"});
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 0);
}

TEST_CASE("ndjson error frame") {
    std::string nd = "{\"error\":\"model not found\"}\n";
    auto msgs = oll::parse_ndjson_for_test(nd);
    bool saw_err = false;
    for (const auto& m : msgs)
        if (const auto* e = get_leaf<StreamError>(m))
            if (e->message.find("model not found") != std::string::npos)
                saw_err = true;
    CHECK(saw_err);
}

// ── 4. JSON-protocol 模式(非常弱的模型,代理 -zero 样式) ───────────────
// 调和每个 StreamToolUseDelta 载荷 (已打捞的 ⁇ ) JSON).
static std::string joined_tool_args(const std::vector<Msg>& msgs) {
    std::string s;
    for (const auto& m : msgs)
        if (const auto* d = get_leaf<StreamToolUseDelta>(m)) s += d->partial_json;
    return s;
}
static std::string first_tool_name(const std::vector<Msg>& msgs) {
    for (const auto& m : msgs)
        if (const auto* s = get_leaf<StreamToolUseStart>(m)) return s->name.value;
    return {};
}

// 干净的单身 -object 想想看,我从不觉得 tool_name, tool_args ~ 一个工具 ~ 调用.
TEST_CASE("jp clean object") {
    std::string nd =
        "{\"message\":{\"role\":\"assistant\",\"content\":"
        "\"{\\\"thoughts\\\":[\\\"list files\\\"],\\\"tool_name\\\":"
        "\\\"bash\\\",\\\"tool_args\\\":{\\\"command\\\":\\\"ls -la\\\"}}\"},"
        "\"done\":true,\"done_reason\":\"stop\"}\n";
    auto msgs = oll::parse_ndjson_for_test(nd, {"shell", "write"}, /*json_protocol=*/true);
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 1);
    CHECK(first_tool_name(msgs) == "shell");
    CHECK(joined_tool_args(msgs).find("ls -la") != std::string::npos);
    // 推理 JSON 不得 以传教士身份被释放。
    CHECK(joined_text(msgs).find("thoughts") == std::string::npos);
}

// 在物体("Sure! {...}")之前进行铅化描述,仍然进行抢救.
TEST_CASE("jp object with leading prose") {
    std::string nd =
        "{\"message\":{\"role\":\"assistant\",\"content\":"
        "\"Sure, here goes: {\\\"tool_name\\\":\\\"bash\\\","
        "\\\"tool_args\\\":{\\\"command\\\":\\\"pwd\\\"}} done!\"},"
        "\"done\":true,\"done_reason\":\"stop\"}\n";
    auto msgs = oll::parse_ndjson_for_test(nd, {"shell"}, /*json_protocol=*/true);
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 1);
    CHECK(first_tool_name(msgs) == "shell");
    CHECK(joined_tool_args(msgs).find("pwd") != std::string::npos);
}

// `tool`/`args` 化名( 闲置) 模式定义 现予接受。
TEST_CASE("jp tool args aliases") {
    std::string nd =
        "{\"message\":{\"role\":\"assistant\",\"content\":"
        "\"{\\\"tool\\\":\\\"write\\\",\\\"args\\\":"
        "{\\\"file_path\\\":\\\"/tmp/x\\\",\\\"content\\\":\\\"hi\\\"}}\"},"
        "\"done\":true,\"done_reason\":\"stop\"}\n";
    auto msgs = oll::parse_ndjson_for_test(nd, {"write", "shell"}, /*json_protocol=*/true);
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 1);
    CHECK(first_tool_name(msgs) == "write");
    CHECK(joined_tool_args(msgs).find("/tmp/x") != std::string::npos);
}

// tool_name 带 `tool:action` 后缀 __ args.action 注射过
TEST_CASE("jp action suffix") {
    std::string nd =
        "{\"message\":{\"role\":\"assistant\",\"content\":"
        "\"{\\\"tool_name\\\":\\\"bash:run\\\",\\\"tool_args\\\":"
        "{\\\"command\\\":\\\"echo hi\\\"}}\"},"
        "\"done\":true,\"done_reason\":\"stop\"}\n";
    auto msgs = oll::parse_ndjson_for_test(nd, {"shell"}, /*json_protocol=*/true);
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 1);
    CHECK(first_tool_name(msgs) == "shell");
    CHECK(joined_tool_args(msgs).find("\"action\":\"run\"") != std::string::npos);
}

// 普通聊天( 无工具) , 以 JSON-protocol 模式 = 传言,无幻术 调用.
TEST_CASE("jp plain chat no tool") {
    std::string nd =
        "{\"message\":{\"role\":\"assistant\",\"content\":\"Hello! How can I help?\"},"
        "\"done\":true,\"done_reason\":\"stop\"}\n";
    auto msgs = oll::parse_ndjson_for_test(nd, {"shell"}, /*json_protocol=*/true);
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 0);
    CHECK(joined_text(msgs).find("How can I help") != std::string::npos);
}

// A tool_args 本身包含一个对象 `}` 引用字符串中
// 不得截断平衡 -object 提取。
TEST_CASE("jp brace in string value") {
    std::string nd =
        "{\"message\":{\"role\":\"assistant\",\"content\":"
        "\"{\\\"tool_name\\\":\\\"bash\\\",\\\"tool_args\\\":"
        "{\\\"command\\\":\\\"echo '}'\\\"}}\"},"
        "\"done\":true,\"done_reason\":\"stop\"}\n";
    auto msgs = oll::parse_ndjson_for_test(nd, {"shell"}, /*json_protocol=*/true);
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 1);
    CHECK(joined_tool_args(msgs).find("echo '}'") != std::string::npos);
}

// 一个未知 tool_name 输入 JSON-protocol 模式为 不 被打捞成a 调用.
TEST_CASE("jp unknown tool not salvaged") {
    std::string nd =
        "{\"message\":{\"role\":\"assistant\",\"content\":"
        "\"{\\\"tool_name\\\":\\\"frobnicate\\\",\\\"tool_args\\\":{}}\"},"
        "\"done\":true,\"done_reason\":\"stop\"}\n";
    auto msgs = oll::parse_ndjson_for_test(nd, {"shell", "write"}, /*json_protocol=*/true);
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 0);
}

// 语法 `response` 伪 -tool: tool_name=="response" 文本
// 输入 tool_args 被解开为纯文本三角形 – 没有幽灵工具 调用 ,无生 JSON.
TEST_CASE("jp response pseudo tool") {
    std::string nd =
        "{\"message\":{\"role\":\"assistant\",\"content\":"
        "\"{\\\"thoughts\\\":[\\\"hi\\\"],\\\"tool_name\\\":\\\"response\\\","
        "\\\"tool_args\\\":{\\\"text\\\":\\\"Hello there!\\\"}}\"},"
        "\"done\":true,\"done_reason\":\"stop\"}\n";
    auto msgs = oll::parse_ndjson_for_test(nd, {"shell"}, /*json_protocol=*/true);
    CHECK(joined_text(msgs).find("Hello there!") != std::string::npos);
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 0);
    // 原始协议 JSON 不得 漏入传言.
    CHECK(joined_text(msgs).find("tool_name") == std::string::npos);
}

// 语法 -forced 工具 调用: tool_name=="shell" 命令在 tool_args
// 发射一个真正的击打工具 -use start/delta/end 载 ⁇ .
TEST_CASE("jp grammar tool call") {
    std::string nd =
        "{\"message\":{\"role\":\"assistant\",\"content\":"
        "\"{\\\"thoughts\\\":[\\\"list files\\\"],\\\"tool_name\\\":\\\"bash\\\","
        "\\\"tool_args\\\":{\\\"command\\\":\\\"ls\\\"}}\"},"
        "\"done\":true,\"done_reason\":\"stop\"}\n";
    auto msgs = oll::parse_ndjson_for_test(nd, {"shell"}, /*json_protocol=*/true);
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 1);
    CHECK(count_leaf<StreamToolUseEnd>(msgs) == 1);
    CHECK(first_tool_name(msgs) == "shell");
    CHECK(joined_tool_args(msgs).find("ls") != std::string::npos);
}

// 电话 `response` 案文也可在下列案文下载入: `response`
// 键入 tool_args (有些模型选别名而不是 `text`).
TEST_CASE("jp response alias key") {
    std::string nd =
        "{\"message\":{\"role\":\"assistant\",\"content\":"
        "\"{\\\"tool_name\\\":\\\"response\\\","
        "\\\"tool_args\\\":{\\\"response\\\":\\\"hey\\\"}}\"},"
        "\"done\":true,\"done_reason\":\"stop\"}\n";
    auto msgs = oll::parse_ndjson_for_test(nd, {"shell"}, /*json_protocol=*/true);
    CHECK(joined_text(msgs).find("hey") != std::string::npos);
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 0);
}

// 渐进式 `response` 流式传输 : 语法 -forced 到达的聊天回复
// 对象 MANY 内容 帧 (真实的) Ollama 传输 )必须解码并排放
// INCREMENTALLY (每桶1三角洲) 帧 以 字节 来了)所以 maya's
// reveal_fx 动画打字机——必须出现完整回复文本 恰好 一次 (无重复)
// done/freeze 交接。这是 Qwen2.5 的固定 -coder
// "Hi Ayush!Hi Ayush!" 复制 + 冻结 -md 虫子,你这混蛋
TEST_CASE("jp response streamed incrementally") {
    // {"tool_name":"response","tool_args":{"text":
    // "Hello Ayush!" 送来的Char -by-char 喜欢 Ollama 语法 -constrained 数据流.
    auto frame = [](const std::string& c) {
        std::string esc;
        for (char ch : c) {
            if (ch == '"') esc += "\\\"";
            else if (ch == '\\') esc += "\\\\";
            else if (ch == '\n') esc += "\\n";
            else if (ch == '\t') esc += "\\t";
            else if (ch == '\r') esc += "\\r";
            else esc += ch;
        }
        return "{\"message\":{\"role\":\"assistant\",\"content\":\""
             + esc + "\"}}\n";
    };
    std::vector<std::string> chunks = {
        "{\n ", " \"tool", "_name", "\": ", " \"", "response", "\",\n",
        " ", " \"tool", "_args", "\": ", " {\n", "   ", " \"", "text",
        "\": ", " \"", "Hello", " Ay", "ush", "!\"\n", " ", " }\n", "}",
    };
    std::string nd;
    for (const auto& c : chunks) nd += frame(c);
    nd += "{\"message\":{\"role\":\"assistant\",\"content\":\"\"},"
          "\"done\":true,\"done_reason\":\"stop\"}\n";

    auto msgs = oll::parse_ndjson_for_test(nd, {"shell"}, /*json_protocol=*/true);
    // 完全答复,确切一次——没有重复。
    std::string txt = joined_text(msgs);
    CHECK(txt == "Hello Ayush!");
    // 协议脚手架不得 泄露成散文。
    CHECK(txt.find("tool_name") == std::string::npos);
    CHECK(txt.find("tool_args") == std::string::npos);
    CHECK(txt.find('{') == std::string::npos);
    // 无幽灵工具调用.
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 0);
    // 被确认为 MULTIPLE 三角洲( 递增) 逐步显示 ),没有一处安顿的垃圾堆.
    CHECK(count_leaf<StreamTextDelta>(msgs) >= 2);
}

// 流回函(\n,\\)内逃逸的字符,
// 即使越狱跨越帧边界,也必须正确解码.
TEST_CASE("jp response streamed escapes") {
    // tool_args.text == 'line1\nsay "hi"' (新行+ 一条被逃出引号)
    std::string nd =
        "{\"message\":{\"role\":\"assistant\",\"content\":"
        "\"{\\\"tool_name\\\":\\\"response\\\",\\\"tool_args\\\":"
        "{\\\"text\\\":\\\"line1\\\\nsay \\\\\\\"hi\\\\\\\"\\\"}}\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\"\"},"
        "\"done\":true,\"done_reason\":\"stop\"}\n";
    auto msgs = oll::parse_ndjson_for_test(nd, {"shell"}, /*json_protocol=*/true);
    std::string txt = joined_text(msgs);
    CHECK(txt == "line1\nsay \"hi\"");
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 0);
}

// A 真实 工具 调用 流线字符 -by-char 不得 被进步派劫持
// -response 路径——它仍然发射一个工具 -use ,没有文本泄露。
TEST_CASE("jp streamed tool call not hijacked") {
    auto frame = [](const std::string& esc) {
        return "{\"message\":{\"role\":\"assistant\",\"content\":\""
             + esc + "\"}}\n";
    };
    std::string nd;
    nd += frame("{\\\"tool_name\\\":\\\"");
    nd += frame("bash\\\",\\\"tool_args\\\":");
    nd += frame("{\\\"command\\\":\\\"ls\\\"}}");
    nd += "{\"message\":{\"role\":\"assistant\",\"content\":\"\"},"
          "\"done\":true,\"done_reason\":\"stop\"}\n";
    auto msgs = oll::parse_ndjson_for_test(nd, {"shell"}, /*json_protocol=*/true);
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 1);
    CHECK(first_tool_name(msgs) == "shell");
    CHECK(joined_tool_args(msgs).find("ls") != std::string::npos);
    CHECK(joined_text(msgs).find("tool_name") == std::string::npos);
}

// A 真实 被俘的Qwen2.5号 -coder : 最新语法 -constrained
// 数据流 (24 帧 ,卷曲 / 缩曲 api/chat 与反应伪 -tool。
//   答复是{ "tool_name": "response", "tool_args":
// { "text": "Hi Ayush!" ~ 释放的字符 ~ -by-char,
// INCLUDING 模型的后继 " }" 帧 用来在回复中泄露一个偏差的牙套。 字节 关闭
// 传输 那样就证明 修补是和模型相对的 不仅仅是一只手 -written 固定。 固定。
TEST_CASE("jp live capture qwen") {
    static const char* kFrames[] = {
        "{\"message\":{\"role\":\"assistant\",\"content\":\"{\"}}",
        "{\"message\":{\"role\":\"assistant\",\"content\":\" \\\"\"}}",
        "{\"message\":{\"role\":\"assistant\",\"content\":\"tool\"}}",
        "{\"message\":{\"role\":\"assistant\",\"content\":\"_name\"}}",
        "{\"message\":{\"role\":\"assistant\",\"content\":\"\\\":\"}}",
        "{\"message\":{\"role\":\"assistant\",\"content\":\" \\\"\"}}",
        "{\"message\":{\"role\":\"assistant\",\"content\":\"response\"}}",
        "{\"message\":{\"role\":\"assistant\",\"content\":\"\\\",\"}}",
        "{\"message\":{\"role\":\"assistant\",\"content\":\" \\\"\"}}",
        "{\"message\":{\"role\":\"assistant\",\"content\":\"tool\"}}",
        "{\"message\":{\"role\":\"assistant\",\"content\":\"_args\"}}",
        "{\"message\":{\"role\":\"assistant\",\"content\":\"\\\":\"}}",
        "{\"message\":{\"role\":\"assistant\",\"content\":\" {\"}}",
        "{\"message\":{\"role\":\"assistant\",\"content\":\" \\\"\"}}",
        "{\"message\":{\"role\":\"assistant\",\"content\":\"text\"}}",
        "{\"message\":{\"role\":\"assistant\",\"content\":\"\\\":\"}}",
        "{\"message\":{\"role\":\"assistant\",\"content\":\" \\\"\"}}",
        "{\"message\":{\"role\":\"assistant\",\"content\":\"Hi\"}}",
        "{\"message\":{\"role\":\"assistant\",\"content\":\" Ay\"}}",
        "{\"message\":{\"role\":\"assistant\",\"content\":\"ush\"}}",
        "{\"message\":{\"role\":\"assistant\",\"content\":\"!\\\"\"}}",
        "{\"message\":{\"role\":\"assistant\",\"content\":\" }\"}}",
        "{\"message\":{\"role\":\"assistant\",\"content\":\" }\"}}",
        "{\"message\":{\"role\":\"assistant\",\"content\":\"\"},"
        "\"done\":true,\"done_reason\":\"stop\","
        "\"prompt_eval_count\":51,\"eval_count\":24}",
    };
    std::string nd;
    for (const char* f : kFrames) { nd += f; nd += '\n'; }

    auto msgs = oll::parse_ndjson_for_test(nd, {"shell"}, /*json_protocol=*/true);
    std::string txt = joined_text(msgs);
    CHECK(txt == "Hi Ayush!");                       // 准确, 一次( 无 dup)
    CHECK(txt.find('{') == std::string::npos);        // 没有脚手架漏水
    CHECK(txt.find('}') == std::string::npos);        // 没有后端的-brace泄漏
    CHECK(txt.find("tool_name") == std::string::npos);
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 0); // 是个聊天答覆
    CHECK(count_leaf<StreamTextDelta>(msgs) >= 2);     // 流经递增
    CHECK(count_leaf<StreamUsage>(msgs) == 1);
}

// ── 5. build_options (num_ctx / num_predict /取样) ──────────────────────
TEST_CASE("options unknown window default") {
    // 无上下文窗口 ~ 安全代理 -sized 默认 8192,
    // 远高于 Ollama's 2k/4k 地板会静静地中断长话短说
    oll::Request r;
    r.max_tokens = 16384;
    r.context_window = 0;
    auto o = oll::build_options(r);
    CHECK(o["num_ctx"].get<int>() == 8192);
    // num_predict 被夹在窗口的一半上,所以 提示词 总是适合。
    CHECK(o["num_predict"].get<int>() == 4096);
}

TEST_CASE("options probed window") {
    // 一台真正的327668-window型号获得该窗口(被打开到32768的天花板上).
    oll::Request r;
    r.max_tokens = 16384;
    r.context_window = 32768;
    auto o = oll::build_options(r);
    CHECK(o["num_ctx"].get<int>() == 32768);
    CHECK(o["num_predict"].get<int>() == 16384);  // 半个32768,  ⁇ max_tokens
}

TEST_CASE("options huge window clamped") {
    // 128k-window型号被夹在代理机天花板上,这样我们就不会试图分配
    // 一个巨大的KVXX缓存和OOM,作为本地的Ollama服务器.
    oll::Request r;
    r.max_tokens = 16384;
    r.context_window = 131072;
    auto o = oll::build_options(r);
    CHECK(o["num_ctx"].get<int>() == 32768);
}

TEST_CASE("options small window floored") {
    // 一个报告一个小窗口的模型仍然能拿到代理地板.
    oll::Request r;
    r.max_tokens = 16384;
    r.context_window = 2048;
    auto o = oll::build_options(r);
    CHECK(o["num_ctx"].get<int>() == 8192);
}

TEST_CASE("the gauge measures the window ollama will actually serve") {
    // 当地模型公布的窗口是声称 WEIGHTS 而不是机器的问题
    // Ollama 分配一个 KV 缓存 来匹配,
    // 所以 agentty 夹子 num_ctx 最高额将 加载 电脑上,
    // 而不是服务器的操作 -request 夹子没错
    //
    // 虫子是那个 传输 上下文测量仪对未打开的窗口进行评分,
    // 因此在131k的本地模型上,栏中读取了四分之一的真实使用量和
    // 自动 -compaction 等了四遍了 等了四个月 线程
    // 模特儿悄悄地切开自己的车 提示词 很久以前,我一直想着
    // agentty 认为要紧凑...
    // "local models forget everything" 内在
    // 的隐形 因为每一层都是正确的 而且没有两层同意
    //
    // 所以:一个功能,和 传输 谓 派生 上面写着,
    // 他们不能在夹子的每个有趣的地方漂流
    struct Case { int advertised; int served; const char* why; };
    static constexpr Case kCases[] = {
        {0,        8192,  "unknown window falls to the agent default"},
        {2048,     8192,  "below the floor is raised to it"},
        {8192,     8192,  "exactly the floor is unchanged"},
        {16384,   16384,  "between floor and ceiling passes through"},
        {32768,   32768,  "exactly the ceiling is unchanged"},
        {131072,  32768,  "a 128k model is clamped, not served whole"},
        {1000000, 32768,  "a 1M model is clamped just the same"},
    };

    for (const auto& c : kCases) {
        INFO(c.why << " (advertised=" << c.advertised << ")");

        // 计数器会用到的号码。
        CHECK(oll::effective_num_ctx(c.advertised) == c.served);

        // 号码 传输 实际发送,独立建造。
        oll::Request r;
        r.max_tokens     = 16384;
        r.context_window = c.advertised;
        CHECK(oll::build_options(r)["num_ctx"].get<int>() == c.served);
    }
}

TEST_CASE("options json protocol sampling") {
    // Weak/json-protocol 路径获得工具的低温 -call 可靠性。
    oll::Request r;
    r.max_tokens = 16384;
    r.context_window = 8192;
    r.json_protocol = true;
    auto o = oll::build_options(r);
    CHECK(o.contains("temperature"));
    CHECK(o["temperature"].get<double>() < 0.5);
    // Capable(not-json-protocol)型号保留其Modelfile默认.
    oll::Request r2;
    r2.max_tokens = 16384;
    r2.context_window = 8192;
    auto o2 = oll::build_options(r2);
    CHECK(!o2.contains("temperature"));
}

// ── 6. <think> 推理剥离 ───────────────────────────────────────────
TEST_CASE("think block stripped from prose") {
    // 推理模型内含 <think>…</think> 然后回答。
    // 只显示答案;推理从未到达用户。
    std::string nd =
        "{\"message\":{\"role\":\"assistant\",\"content\":"
        "\"<think>let me reason</think>The answer is 42.\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\"\"},"
        "\"done\":true,\"done_reason\":\"stop\"}\n";
    auto msgs = oll::parse_ndjson_for_test(nd);
    CHECK(joined_text(msgs).find("The answer is 42") != std::string::npos);
    CHECK(joined_text(msgs).find("let me reason") == std::string::npos);
    CHECK(joined_text(msgs).find("<think>") == std::string::npos);
}

TEST_CASE("think then tool call salvaged") {
    // 模特儿想,然后就漏了个工具 调用 内容。 <think> 块不得
    // 击败 hold/salvage 路径——工具 调用 仍然开火。
    std::string nd =
        "{\"message\":{\"role\":\"assistant\",\"content\":"
        "\"<think>I should list files</think>"
        "{\\\"name\\\":\\\"bash\\\",\\\"arguments\\\":"
        "{\\\"command\\\":\\\"ls\\\"}}\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\"\"},"
        "\"done\":true,\"done_reason\":\"stop\"}\n";
    auto msgs = oll::parse_ndjson_for_test(nd, {"shell", "write"});
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 1);
    CHECK(first_tool_name(msgs) == "shell");
    CHECK(joined_text(msgs).find("should list files") == std::string::npos);
}

// ── 3. system_prompt ─────────────────────────────────────────────────────────
TEST_CASE("system prompt shape") {
    auto p = oll::system_prompt();
    CHECK(p.find("agentty") != std::string::npos);
    CHECK(p.find("CONVERSATION MEMORY") != std::string::npos);
    CHECK(p.find("ENVIRONMENT") != std::string::npos);
    // 动词 Claude 传言/大知识 -memory 垃圾不得 现身.
    CHECK(p.find("<file-editing>") == std::string::npos);
    CHECK(p.find("<learned-memory") == std::string::npos);
}

// ── 新: JSON-protocol 历史轮 -trip ────────────────────────────────────
// 内 JSON-protocol 模式 build_messages 必须 渲染 工具 调用
// 作为模型自己的{ tool_name,tool_args \ 对象( 辅助作用) 和结果为平面 USER
// "TOOL RESULT (name):" 转弯... 不 本地人 tool_calls []
// / 作用: "tool" 形状一个流言 -only 模型不会读。
TEST_CASE("build messages json protocol roundtrip") {
    std::vector<Message> msgs;
    Message a; a.role = Role::Assistant; a.text = "";
    ToolUse tc;
    tc.id   = ToolCallId{"call_1"};
    tc.name = ToolName{"shell"};
    tc.args = nlohmann::json{{"command", "ls -la"}};
    tc.status = ToolUse::Done{{}, {}, "file1\nfile2"};
    a.tool_calls.push_back(std::move(tc));
    msgs.push_back(std::move(a));

    auto arr = oll::build_messages(msgs, /*json_protocol=*/true);
    // 助理传言 -JSON 对象+用户 TOOL RESULT 转弯(无原生形状).
    CHECK(arr.size() == 2);
    CHECK(arr[0]["role"] == "assistant");
    CHECK(!arr[0].contains("tool_calls"));               // 不 本地语言
    {
        auto obj = nlohmann::json::parse(arr[0]["content"].get<std::string>());
        CHECK(obj["tool_name"] == "shell");
        CHECK(obj["tool_args"]["command"] == "ls -la");
    }
    CHECK(arr[1]["role"] == "user");                     // 不 角色( E) : "tool"
    CHECK(arr[1]["content"].get<std::string>().find("TOOL RESULT (shell)")
          != std::string::npos);
    CHECK(arr[1]["content"].get<std::string>().find("file1") != std::string::npos);
}

// ── 新 参数( U) -key 修理 NATIVE 结构化频道 ─────────────────────
// 本地频道的弱模式 传播遗产 `bash` 名称( 仍为 `shell` 与{}
// 连接 "cmd" :...} 而不是{{} \fn微软雅黑 "command" :...};
// ; : 解析器 既要接受别名,又要重新绘制峡谷的钥匙 `command`.
TEST_CASE("ndjson native arg key repair") {
    const char* nd =
        "{\"message\":{\"role\":\"assistant\",\"content\":\"\","
        "\"tool_calls\":[{\"function\":{\"name\":\"bash\","
        "\"arguments\":{\"cmd\":\"ls\"}}}]}}\n"
        "{\"done\":true,\"done_reason\":\"stop\"}\n";
    auto msgs = oll::parse_ndjson_for_test(nd, {"shell", "bash"}, /*json_protocol=*/false);
    std::string args;
    for (const auto& m : msgs)
        if (const auto* d = get_leaf<StreamToolUseDelta>(m)) args += d->partial_json;
    auto j = nlohmann::json::parse(args);
    CHECK(j.contains("command"));        // 重新映射
    CHECK(j["command"] == "ls");
    CHECK(!j.contains("cmd"));            // 已删除的别名
}

// ── 新 参数( U) -key 修理 SALVAGE 频道 ───────────────────────────────
// 漏水的 { } ( 发音) "tool_name":"read","tool_args":{"file":
// "x.c" QQ 必须着陆为{ "path":"x.c" }所以读工具实际上找到了它的论据。
TEST_CASE("jp salvage arg key repair") {
    const char* nd =
        "{\"message\":{\"role\":\"assistant\","
        "\"content\":\"{\\\"tool_name\\\":\\\"read\\\","
        "\\\"tool_args\\\":{\\\"file\\\":\\\"x.c\\\"}}\"}}\n"
        "{\"done\":true,\"done_reason\":\"stop\"}\n";
    auto msgs = oll::parse_ndjson_for_test(nd, {"read"}, /*json_protocol=*/true);
    std::string args;
    for (const auto& m : msgs)
        if (const auto* d = get_leaf<StreamToolUseDelta>(m)) args += d->partial_json;
    auto j = nlohmann::json::parse(args);
    CHECK(j.contains("path"));           // 文件 – 路径
    CHECK(j["path"] == "x.c");
    CHECK(!j.contains("file"));
}

// ── 永远 -blank-turn 安全网(苹果公司) `if not received_content`) ──────────
// 个QWEN `response` 其对象 `text` 空的,
// 但谁的 `thoughts` 带真言必须露出思想的正文——绝不是空洞的转折。
TEST_CASE("jp response empty text falls back to thoughts") {
    // 单一内容 帧 带了整个 {想法, tool_name 答复: tool_args
    // :{文本 : "" QQ 对象, 然后单独完成 帧 文本为空,
    // 所以 回退 必须是表面 `thoughts` 作是说.
    std::string nd =
        "{\"message\":{\"role\":\"assistant\",\"content\":"
        "\"{\\\"thoughts\\\":[\\\"You are Ayush\\\"],\\\"tool_name\\\":"
        "\\\"response\\\",\\\"tool_args\\\":{\\\"text\\\":\\\"\\\"}}\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\"\"},"
        "\"done\":true,\"done_reason\":\"stop\"}\n";
    auto msgs = oll::parse_ndjson_for_test(nd, {"shell"}, /*json_protocol=*/true);
    std::string txt = joined_text(msgs);
    CHECK(txt.find("You are Ayush") != std::string::npos);
    CHECK(txt.find("tool_name") == std::string::npos);
    CHECK(count_leaf<StreamToolUseStart>(msgs) == 0);
}

// A JSON-protocol 转动产生 无内容 仍然可以使用( 模式只
// 释放白空间) 渲染 可见转弯: Aider 精确
// `(empty response)` 占位符,永远不要一个沉默的空虚。
TEST_CASE("jp truly empty shows placeholder") {
    std::string nd =
        "{\"message\":{\"role\":\"assistant\",\"content\":\"   \"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\"\"},"
        "\"done\":true,\"done_reason\":\"stop\"}\n";
    auto msgs = oll::parse_ndjson_for_test(nd, {"shell"}, /*json_protocol=*/true);
    CHECK(joined_text(msgs).find("(empty response)") != std::string::npos);
    CHECK(count_leaf<StreamFinished>(msgs) == 1);
}

// 纯粹的传言 STARTED 生活看起来像它可能 JSON 但不是工具,
// 但不是一个工具 调用 必须 仍然 到达用户——永远不要静默地空转。
TEST_CASE("prose starting with brace not dropped") {
    std::string nd =
        "{\"message\":{\"role\":\"assistant\",\"content\":"
        "\"Here is the answer: 42.\"}}\n"
        "{\"message\":{\"role\":\"assistant\",\"content\":\"\"},"
        "\"done\":true,\"done_reason\":\"stop\"}\n";
    auto msgs = oll::parse_ndjson_for_test(nd, {"shell"});
    CHECK(joined_text(msgs).find("42") != std::string::npos);
}

// ── 字节-identity 警卫 ─────────────────────────────────────────────────
// 扣紧 EXACT build_messages () 输出于一个特性 -rich
// 信件列表, 以至 Ollama 传输层 可用不重构 -drift 保证(
// 与 Anthropic/OpenAI 金色的卫士).
static std::uint64_t oll_fnv1a(const std::string& s) {
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    return h;
}

TEST_CASE("wire body golden") {
    auto done = [](const char* id, const char* name, nlohmann::json args,
                   const std::string& out) {
        ToolUse tc; tc.id = ToolCallId{id}; tc.name = ToolName{name};
        tc.args = std::move(args); tc.status = ToolUse::Done{{}, {}, out};
        return tc;
    };
    std::vector<Message> msgs;
    Message u; u.role = Role::User; u.text = "do it"; msgs.push_back(u);
    { Message a; a.role = Role::Assistant; a.text = "reading";
      a.tool_calls.push_back(done("call_r1", "read",
          nlohmann::json{{"path", "src/foo.cpp"}},
          std::string("FIRST_") + std::string(4096, 'a')));
      msgs.push_back(std::move(a)); }
    { Message a; a.role = Role::Assistant; a.text = "grepping";
      a.tool_calls.push_back(done("call_g1", "grep",
          nlohmann::json{{"pattern", "x"}},
          std::string("GHEAD_") + std::string(80 * 1024, 'g')));
      msgs.push_back(std::move(a)); }
    { Message a; a.role = Role::Assistant; a.text = "editing";
      a.tool_calls.push_back(done("call_e1", "edit",
          nlohmann::json{{"path", "src/foo.cpp"}}, "ok"));
      msgs.push_back(std::move(a)); }
    { Message a; a.role = Role::Assistant; a.text = "re-reading";
      a.tool_calls.push_back(done("call_r2", "read",
          nlohmann::json{{"path", "src/foo.cpp"}},
          std::string("SECOND_") + std::string(4096, 'b')));
      msgs.push_back(std::move(a)); }

    const std::string wire = oll::build_messages(msgs, /*json_protocol=*/false).dump();
    CHECK(wire.find("FIRST_") == std::string::npos);      // 已替换的读作已崩溃
    CHECK(wire.find("content was dropped to save context") != std::string::npos);
    CHECK(wire.find("SECOND_") != std::string::npos);     // 现场阅读全

    constexpr std::uint64_t kGolden = 0x945cece785ad177full;
    const std::uint64_t got = oll_fnv1a(wire);
    if (kGolden == 0)
        std::fprintf(stderr, "OLLAMA wire golden = 0x%016llxull (len=%zu)\n",
                     static_cast<unsigned long long>(got), wire.size());
    else
        CHECK(got == kGolden);
}

