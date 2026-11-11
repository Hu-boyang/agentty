// provider_conformance_test — 一个 合同,针对 每个 方言(英语:Culture).
//
// ── 该文件为何存在 ─────────────────────────────────────────────────
//
// 4个传到用户的bug是同一个bug穿不同衣服,
// 每个都用手,用一种方言,在有人打它之后被发现:
//
//   • 答复减少 `function_call_arguments.done` + 副驾驶工具 调用
//     发送日期 `{}` ("[invalid args] pattern required").
//   • 盲目交谈 `delta.tool_calls[].arguments` ,所以一个
//     联合代理公司生产了`{"a":1}{"a":1}`——同样的症状,
//     不同的方言,发现它只是因为我们去照顾第一个。
//
// 人数 -dialect 测试不可能抓住它,
// 因为每个编码 一个 服务器设置其作者所看到的。
// 固定是一次声明合同, 并立即为每个后端提供它, 所以新提供
// 者继承每个后端 不变量 而不是依赖谁添加它来记住。
//
// 下方的方言只提供a `tool_call(name, args, style)`
// 函数 帧 一个工具 调用 本身 传输 说到的都是方言
// -independent - 这就是重点。

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "agtest.hpp"

#include "agentty/provider/anthropic/transport.hpp"
#include "agentty/provider/chatgpt/responses.hpp"
#include "agentty/provider/ollama/transport.hpp"
#include "agentty/provider/openai/transport.hpp"
#include "agentty/provider/stream_scaffold.hpp"

using namespace agentty;
using json = nlohmann::json;

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

// 服务器可能如何 帧 参数。每个臂是光谱 -legal
// 方言支持它的地方;不能表达一个方言跳过它的地方。
enum class Style { Fragments, Snapshot, Both };

const char* name_of(Style s) {
    switch (s) {
        case Style::Fragments: return "Fragments";
        case Style::Snapshot:  return "Snapshot";
        case Style::Both:      return "Both";
    }
    return "?";
}

// 一个工具的解码结果 调用 ,以方言为单位 -independent 条件。
struct Decoded {
    std::string name;
    std::string args;
    int         starts = 0;
    int         ends   = 0;
};

Decoded decode(const std::vector<Msg>& msgs) {
    Decoded d;
    for (const auto& m : msgs) {
        if (auto* s = leaf<StreamToolUseStart>(m)) { ++d.starts; d.name = s->name.value; }
        if (auto* x = leaf<StreamToolUseDelta>(m)) d.args += x->partial_json;
        if (auto* e = leaf<StreamToolUseEnd>(m))   ++d.ends;
    }
    return d;
}

// ── 方言 方言 ─────────────────────────────────────────────────────────

struct Responses {
    static constexpr const char* id = "openai-responses";
    static bool supports(Style) { return true; }
    static Decoded run(const std::string& name, const std::string& args, Style st) {
        const std::string esc = json(args).dump();
        std::vector<std::string> sse{
            R"({"type":"response.output_item.added","item":{"type":"function_call",)"
            R"("id":"fc_1","call_id":"call_1","name":")" + name + R"("}})"};
        if (st != Style::Snapshot) {
            const auto mid = args.size() / 2;
            for (const auto& part : {args.substr(0, mid), args.substr(mid)})
                sse.push_back(
                    R"({"type":"response.function_call_arguments.delta",)"
                    R"("item_id":"fc_1","delta":)" + json(part).dump() + "}");
        }
        if (st != Style::Fragments)
            sse.push_back(R"({"type":"response.function_call_arguments.done",)"
                          R"("item_id":"fc_1","arguments":)" + esc + "}");
        sse.push_back(R"({"type":"response.output_item.done","item":)"
                      R"({"type":"function_call","id":"fc_1"}})");
        sse.push_back(R"({"type":"response.completed","response":{"usage":{}}})");
        return decode(provider::chatgpt::parse_sse_for_test(sse));
    }
};

struct Chat {
    static constexpr const char* id = "openai-chat";
    // Chat没有单独的 "done" 事件 带参数;
    // 一个将每个块的全值合并的代理 REPEATS。
    // 这就是方言的 快照 形式。
    static bool supports(Style) { return true; }
    static Decoded run(const std::string& name, const std::string& args, Style st) {
        const std::string esc = json(args).dump();
        std::vector<std::string> chunks;
        auto tc = [&](const std::string& head, const std::string& a) {
            chunks.push_back(R"({"choices":[{"delta":{"tool_calls":[{"index":0,)"
                             + head + R"("function":{)" + a + "}}]}}]}");
        };
        if (st == Style::Snapshot) {
            tc(R"("id":"call_1",)", R"("name":")" + name + R"(","arguments":)" + esc);
            tc("", R"("arguments":)" + esc);          // 重复: 无添加
        } else {
            const auto mid = args.size() / 2;
            tc(R"("id":"call_1",)", R"("name":")" + name + R"(","arguments":)"
                                    + json(args.substr(0, mid)).dump());
            tc("", R"("arguments":)" + json(args.substr(mid)).dump());
            if (st == Style::Both) tc("", R"("arguments":)" + esc);
        }
        chunks.push_back(R"({"choices":[{"delta":{},"finish_reason":"tool_calls"}]})");
        std::string bytes;
        for (const auto& c : chunks) bytes += "data: " + c + "\n\n";
        bytes += "data: [DONE]\n\n";
        return decode(provider::openai::parse_sse_for_test(bytes, {name}));
    }
};

struct AnthropicMessages {
    static constexpr const char* id = "anthropic-messages";
    // Anthropic 完全没有 一个 载体 : input_json_delta 碎片。
    // 没有 快照 事件 ,所以,请 快照 武器是无法表达的,
    // 而这是一个值得倾注而不是纸质的事实。
    static bool supports(Style s) { return s == Style::Fragments; }
    static Decoded run(const std::string& name, const std::string& args, Style) {
        const auto mid = args.size() / 2;
        std::vector<std::pair<std::string, std::string>> ev{
            {"content_block_start",
             R"({"index":0,"content_block":{"type":"tool_use","id":"call_1","name":")"
             + name + R"(","input":{}}})"},
        };
        for (const auto& part : {args.substr(0, mid), args.substr(mid)})
            ev.push_back({"content_block_delta",
                          R"({"index":0,"delta":{"type":"input_json_delta",)"
                          R"("partial_json":)" + json(part).dump() + "}}"});
        ev.push_back({"content_block_stop", R"({"index":0})"});
        ev.push_back({"message_delta", R"({"delta":{"stop_reason":"tool_use"}})"});
        ev.push_back({"message_stop", "{}"});
        return decode(provider::anthropic::parse_sse_for_test(ev));
    }
};

struct OllamaNative {
    static constexpr const char* id = "ollama-native";
    // Ollama 释放工具 调用 ATOMICALLY 完全从
    // -parsed 对象 ——没有增量形式可调节。 快照 手臂适用。
    static bool supports(Style s) { return s == Style::Snapshot; }
    static Decoded run(const std::string& name, const std::string& args, Style) {
        const std::string nd =
            R"({"message":{"role":"assistant","tool_calls":[{"function":)"
            R"({"name":")" + name + R"(","arguments":)" + args + "}}]},\"done\":false}\n"
            R"({"message":{"role":"assistant","content":""},"done":true})" "\n";
        return decode(provider::ollama::parse_ndjson_for_test(nd, {name}));
    }
};

// 电话 载荷 方言必须圆形 -trip :巢形物体,
// 一个逃脱的引文,一个斜线——打破天真弦处理的形状.
const std::string kArgs =
    R"({"pattern":"foo\"bar","glob":"src/**/*.cpp","opts":{"word":true}})";

// ── 敌对框架: 相同的 调用 ,发送的方式 BROKEN 服务器发送 ──
//
// 以上合同使用得很好 -formed 传输。
// 每个工具 -call 其它的绳索调查显示它很普遍,
// -python # 3377(在到达顺序上按下碎片键而不是索引),
// opik #8360(流化工具) 调用 ),线条#3950,
// 船员, ⁇ -ai 希亚, llm -gateway
// #136(在续作上空出身份),相同字段,同样几处错误.
//
// 根本原因就是人类的财产 DIALECT , 而不是任何服务器:
// Chat Refinitions 中包含着几个不同的事实,这些事实在字
// 段上没有说明它们是什么. `arguments` 是一个碎片或整个 快照.
// `id` 是身份还是空重述。 `index` 是一个平行
// -call 键或缺席。 Anthropic 名称 类型中的碎片 和
// 答复 将它们分开 事件 ,这正是为什么 没有这个错误类。
//
// 所以解码器不能是 "correct" 与某一类别相反的,
// 它必须正确,因为它与“特定类别”相反。 传输 真实的服务器会发出。
// 这些案例会把实际破坏人的形状钉住,然后它们会与 每个 能够
// 表达它们的方言,所以一个解码器中的固定不能悄悄跳过另一个.
struct Hostile {
    // 继续块重写`id`为空,而不是省略它。
    static Decoded chat_empty_id(const std::string& name, const std::string& args) {
        const auto mid = args.size() / 2;
        auto tc = [&](const std::string& head, const std::string& a) {
            return R"({"choices":[{"delta":{"tool_calls":[{"index":0,)"
                   + head + R"("function":{)" + a + "}}]}}]}";
        };
        std::string bytes;
        bytes += "data: " + tc(R"("id":"call_1",)",
                               R"("name":")" + name + R"(","arguments":")" "\"")
                 + "\n\n";
        bytes += "data: " + tc(R"("id":"",)",
                               R"("arguments":)" + json(args.substr(0, mid)).dump())
                 + "\n\n";
        bytes += "data: " + tc(R"("id":"",)",
                               R"("arguments":)" + json(args.substr(mid)).dump())
                 + "\n\n";
        bytes += R"(data: {"choices":[{"delta":{},"finish_reason":"tool_calls"}]})"
                 "\n\n" "data: [DONE]\n\n";
        return decode(provider::openai::parse_sse_for_test(bytes, {name}));
    }

    // 调试 -by-token 碎片的细微度等于 PREFIX 会
    // 议 日 程 和 议 程 缓冲区 -48号问题 `{"` 当
    // 累积值已经开始时打开嵌入对象 `{"`.
    static Decoded chat_prefix_fragments(const std::string& name,
                                         const std::string& args) {
        std::string bytes;
        bytes += R"(data: {"choices":[{"delta":{"tool_calls":[{"index":0,)"
                 R"("id":"call_1","function":{"name":")" + name +
                 R"(","arguments":""}}]}}]})" "\n\n";
        // 一个 字节 一次:服务器最有对抗性的设定,
        // 以及 llama.cpp 大约是的
        for (char c : args) {
            bytes += "data: " +
                std::string{R"({"choices":[{"delta":{"tool_calls":[{"index":0,)"
                            R"("function":{"arguments":)"}
                + json(std::string(1, c)).dump() + "}}]}}]}" + "\n\n";
        }
        bytes += R"(data: {"choices":[{"delta":{},"finish_reason":"tool_calls"}]})"
                 "\n\n" "data: [DONE]\n\n";
        return decode(provider::openai::parse_sse_for_test(bytes, {name}));
    }
};

TEST_CASE("conformance: hostile framing round-trips the same arguments") {
    // 无论服务器对框架做什么,工具层必须获得
    // 模型所写的确切内容.
    for (auto make : {&Hostile::chat_empty_id, &Hostile::chat_prefix_fragments}) {
        const auto d = make("grep", kArgs);
        // 解析为本地 : doctest 内容 -expands 其宏观论点,所以 `(void)` 内置
        // REQUIRE_NOTHROW 永远无法到达真实 调用 财务报告和已审计财务报表 GCC 仍然警告我们
        // -Wunused-result。见 check_contract (a) 与《京都议定书》第三条第1款有关的其他事项。
        json parsed;
        REQUIRE_NOTHROW(parsed = json::parse(d.args));
        CHECK(parsed == json::parse(kArgs));
        // 一次宣布,一次关闭—— 状态更新函数 双对 tool_use 与
        // tool_result 准确的这些,所以一个滴 End 悬转法轮.
        CHECK(d.starts == 1);
        CHECK(d.ends == 1);
        CHECK(d.name == "grep");
    }
}

template <class D>
void check_contract() {
    for (auto st : {Style::Fragments, Style::Snapshot, Style::Both}) {
        if (!D::supports(st)) continue;
        INFO("dialect = " << std::string{D::id}
             << ", framing = " << std::string{name_of(st)});
        const auto d = D::run("grep", kArgs, st);

        // 1. 模型所编的论据是工具中的论据
        //    层接收,无论服务器如何设置它们。
        //
        //    与PARSEDXXJSON相比,而非字节:一种方言可以合法地
        //    重排-serialise(Ollama圆-trips至json::
        //    dump()),修复错误的参数键,使键序和白空格正常化).
        //    绝不能改变的是VALUE。 双倍的-appended 数据流
        //    (`{...}{...}`) 完全没有实现解析, 因此它仍然捕捉到
        //    它为之写的-emission 重复错误。
        //
        //    解析 一次 变成一个本地人,而不是内部人 REQUIRE_NOTHROW.
        //    doctest 内容 -expands 其论点,
        //    所以 `(void)` 铸造的,不能到达真人, 调用 财务
        //    报告和已审计财务报表 GCC 仍然警告我们
        //    -Wunused-result - 一旦每个方言即时化这个模板,
        //    这是这个文件用来产生的大部分建构噪音. 绑定 结果是真实的,
        //    和 CHECK 下方是输掉的
        json parsed;
        REQUIRE_NOTHROW(parsed = json::parse(d.args));
        CHECK(parsed == json::parse(kArgs));

        // 3. 决 定 调用 一次宣布,一次关闭—— 状态更新函数 双对
        //    tool_use 与 tool_result 完全照着这些
        CHECK(d.starts == 1);
        CHECK(d.ends == 1);
        CHECK(d.name == "grep");
    }
}

} // 命名空间结束。

TEST_CASE("conformance: openai-responses")   { check_contract<Responses>(); }
TEST_CASE("conformance: openai-chat")        { check_contract<Chat>(); }
TEST_CASE("conformance: anthropic-messages") { check_contract<AnthropicMessages>(); }
TEST_CASE("conformance: ollama-native")      { check_contract<OllamaNative>(); }

// 空参数对象是一个LEGITIMATE工具调用(repo_map,
// 列表,测试不采参数),必须作为`{}`生存下来——而不是一个抛弃的三角形。
// 这样一来,使Copilotbug读起来就像一个片面模型:
// 0-argument工具在所有其他工具失败时一直工作.
TEST_CASE("conformance: an empty argument object survives every dialect") {
    const auto empty = [](const std::string& a) {
        return !a.empty() && json::parse(a).is_object() && json::parse(a).empty();
    };
    CHECK(empty(Responses::run("repo_map", "{}", Style::Snapshot).args));
    CHECK(empty(Chat::run("repo_map", "{}", Style::Snapshot).args));
    CHECK(empty(AnthropicMessages::run("repo_map", "{}", Style::Fragments).args));
    CHECK(empty(OllamaNative::run("repo_map", "{}", Style::Snapshot).args));
}

// ── StreamScaffold : 共享的每张 -turn 处理函数 合同 ─────────────────
//
// 每一个 传输层 现建其 StreamHandler 发件人
// provider::StreamScaffold ,所以这些 不变量
// 持有 全部 被建筑所包围 传输层 倒向一只手, -rolled 处理函数
// (或脚手架编辑削弱合同) 失败了这个套房,而不是外地的用户.
TEST_CASE("conformance: scaffold caps the error body at exactly 64 KB") {
    provider::StreamScaffold sc;
    sc.dialect = "test";
    sc.sink = [](Msg) {};
    sc.feed = [](std::string_view) { return true; };
    auto h = sc.handler();
    h.on_headers(500, {});
    CHECK(!sc.ok());
    // 饲料3块32KB——只有前两块适合在盖子下.
    const std::string chunk(32 * 1024, 'x');
    CHECK(h.on_chunk(chunk));   // 错误路径持续排水
    CHECK(h.on_chunk(chunk));
    CHECK(h.on_chunk(chunk));
    CHECK(sc.error_body.size() == provider::kErrorBodyCap);
}

TEST_CASE("conformance: scaffold routes success chunks to feed, error to body") {
    provider::StreamScaffold sc;
    sc.dialect = "test";
    sc.sink = [](Msg) {};
    std::string fed;
    sc.feed = [&](std::string_view c) { fed += c; return true; };

    auto h = sc.handler();
    h.on_headers(200, {});
    CHECK(sc.ok());
    CHECK(h.on_chunk("data: hello\n\n"));
    CHECK(fed == "data: hello\n\n");
    CHECK(sc.error_body.empty());

    // feed的返回值传播(Response Codec的故意stop
    // -after-terminal读作中止).
    sc.feed = [&](std::string_view) { return false; };
    CHECK(!h.on_chunk("more"));
}

TEST_CASE("conformance: scaffold forwards liveness as transport-only events") {
    provider::StreamScaffold sc;
    sc.dialect = "test";
    int heartbeats = 0, buffered = 0;
    sc.sink = [&](Msg m) {
        if (leaf<StreamHeartbeat>(m))    ++heartbeats;
        if (leaf<StreamBufferedWait>(m)) ++buffered;
    };
    sc.feed = [](std::string_view) { return true; };
    auto h = sc.handler();
    // 每个 传输层 必须 传输 两者 回调 — Ollama 运出时无
    // on_buffered_wait 几个月(缓存的发送显示为死气).
    REQUIRE(h.on_activity);
    REQUIRE(h.on_buffered_wait);
    h.on_activity();
    h.on_buffered_wait();
    CHECK(heartbeats == 1);
    CHECK(buffered == 1);
}

TEST_CASE("conformance: shared streaming timeout ladder") {
    // 梯子是与状态更新函数的摊位看门狗和重试的分类器签订的合同;
    // 在这里默默的改变让用户
    // -visible的行为同时改变每个供应商.
    const auto tos = provider::stream_timeouts();
    CHECK(tos.connect == std::chrono::milliseconds(10'000));
    CHECK(tos.total   == std::chrono::minutes(30));
    CHECK(tos.ping    == std::chrono::milliseconds(15'000));
    CHECK(tos.idle    == std::chrono::milliseconds(90'000));
    // 一个合法的把手:本地服务器会得到更长的闲置.
    CHECK(provider::stream_timeouts(std::chrono::milliseconds(600'000)).idle
          == std::chrono::milliseconds(600'000));
}

TEST_CASE("conformance: local servers have no total cap (#108)") {
    // 家里的大模特儿 家里的模特儿 GPU 能够 数据流 30分钟后,
    // 一个答复,主机盖杀死了中场 -answer 服务器正常时
    const auto local = provider::local_stream_timeouts();
    CHECK(local.total == std::chrono::milliseconds(0));
    CHECK(local.idle  == std::chrono::milliseconds(600'000));   // 服务器已死亡, 仍然被捕获
    CHECK(local.connect == provider::stream_timeouts().connect);
    CHECK(provider::local_stream_timeouts(std::chrono::milliseconds(120'000)).idle
          == std::chrono::milliseconds(120'000));
}

// ── 推理 切换 ,针对 每个 方言语 ────────────────
//
// 这个套房存在,但仍错过了一个错误:推理到达了Copilot的gpt-
// *模型上,与bug. 切换 OFF ,因为回复编码器从未读过
// Request::show_reasoning。三 传输层 很荣幸,
// 没有,没有交叉 -checked - 合同只涉及工具 调用.
//
// 即一般形状: 请求字段为 PROMISE 给用户,
// 而四个执行中的三个执行承诺比一个执行一个更差,
// 因为看起来是任意的。所以轴得到同样的处理
// 工具 调用 说一次,每个方言即时
//
// 两种合法的策略,合同都接受:
//
//   SUPPRESS AT SOURCE   Anthropic 请服务器不要发送(
//                        T) `thinking.display` ),所以没有到达过滤器.
//   FILTER AT DECODE     Chat/Ollama/Responses 接受它,
//                        无论方式 并删除文本 在去 状态更新函数.
//
// 两者必须生产的东西是一样的 OBSERVABLE : 没有推理
// 文本到达 状态更新函数 当用户隐藏它时,方言是如何得到它的,
// 这是它自己的业务;它不会得到它。
namespace reasoning {

// 幸存下来的文字 状态更新函数 ,是否 任意 生命
// 信号仍然发射
//
// 生命是全数的 两者 有意载体。方言可以使转弯保持空转
// -text ThinkingDelta (聊天,
// 答复)或与 StreamHeartbeat (ollama);
// 国家 状态更新函数 重排( E) last_event_at
// 使用一种拼写方式将是一种机制而不是保证,而且会使一种
// 行为正确的方言失效,这就是合同如何失去其权威。
struct Seen {
    std::string text;
    int         liveness = 0;
};

Seen collect(const std::vector<Msg>& msgs) {
    Seen s;
    for (const auto& m : msgs) {
        if (auto* t = leaf<StreamThinkingDelta>(m)) { s.text += t->text; ++s.liveness; }
        if (leaf<StreamHeartbeat>(m))               { ++s.liveness; }
    }
    return s;
}

// 每一方言帧 同一转弯:一个推理破裂,然后一个答案.
Seen chat(bool show) {
    const std::string sse =
        "data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"pondering\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"answer\"}}]}\n\n"
        "data: [DONE]\n\n";
    return collect(provider::openai::parse_sse_for_test(
        sse, {}, false, false, show));
}

Seen responses(bool show) {
    const std::vector<std::string> sse{
        R"({"type":"response.reasoning_summary_text.delta","delta":"pondering"})",
        R"({"type":"response.output_text.delta","delta":"answer"})",
        R"({"type":"response.completed","response":{"usage":{}}})"};
    return collect(provider::chatgpt::parse_sse_for_test(sse, show));
}

Seen ollama(bool show) {
    const std::string nd =
        R"({"message":{"role":"assistant","thinking":"pondering"},"done":false})" "\n"
        R"({"message":{"role":"assistant","content":"answer"},"done":true})" "\n";
    return collect(provider::ollama::parse_ndjson_for_test(
        nd, {}, false, false, show));
}

}  // namespace reasoning

TEST_CASE("conformance: hiding reasoning hides it on every dialect") {
    struct Case { const char* id; reasoning::Seen (*run)(bool); bool keeps_text; };
    // 打开( O) -chat KEEPS 隐藏时的文本: 下一个请求会将每
    // 个转折的推理回放到它的字段下( ) GitHub '客户端也这样做,
    // 有些主机400没有它, #117。 不显示它就是视图的工作(
    // ui::reasoning_shown ),不是解码器的.
    for (const Case c : {Case{"openai-chat",      &reasoning::chat,      true},
                         Case{"openai-responses", &reasoning::responses, false},
                         Case{"ollama-native",    &reasoning::ollama,    false}}) {
        INFO("dialect = " << std::string{c.id});

        // 显示:文本达到状态更新函数.
        const auto on = c.run(true);
        CHECK(on.text.find("pondering") != std::string::npos);

        // 隐藏: 无推理 TEXT 生存... 除了这里 传输 需要它,我需要它,你需要它,但你需要它。
        const auto off = c.run(false);
        if (c.keeps_text) CHECK(off.text == on.text);
        else              CHECK(off.text.empty());

        // . 不过... SOME 生命信号仍然在燃烧 吞噬着
        // 事件 完全让一个漫长的沉默推理阶段 看起来像一个死 数据流 ,
        // 并显示 重试 看门狗 行动起来
        CHECK(off.liveness > 0);
    }
}

TEST_CASE("conformance: hiding reasoning never disturbs the answer") {
    // 退路护卫 压制必须碰一个通道
    const std::string sse =
        "data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"think\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"hello \"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"world\"}}]}\n\n"
        "data: [DONE]\n\n";
    std::string text;
    for (const auto& m : provider::openai::parse_sse_for_test(
             sse, {}, false, false, /*show_reasoning=*/false))
        if (auto* t = leaf<StreamTextDelta>(m)) text += t->text;
    CHECK(text == "hello world");
}

// ── 合同的REQUEST半 ─────────────────────────────────
//
// 上面的一切都说出什么方言 DECODES。这个断言它是
// 什么 ENCODES 更安静的虫子们住的地方
//
// 中性`provider::Request`上的字段是对用户的许诺:
// 设定输出上限或推理级,并期望它能够应用.
// 三个执行方所信守的承诺是: WORSE 而不是
// 从未做过的, 写它的人看起来是正确的,
// 任意的人 碰巧在方言上忽略它, 没有任何东西在
// UI 区分了两者。
//
// 这不是假设,推理 切换 3次被押运 传输层 且忽视第四者,
// `max_tokens` 由查特派来的 Anthropic 财务
// 报告和已审计财务报表 Ollama 但被答复者默默地放弃了—
// —通过撰写这一测试而不是通过报告找到的。
//
// 只有回应才暴露出纯洁 正文 今天的建筑商;
// 其他三个组装他们的 正文 内侧 数据流 函数,
// 所以行为断言无法到达它们。与其跳过它们,
// 请检查 SOURCE。粗糙的:它不能
// 证明数值被正确使用,但它确实证明没有人删除
// 了唯一一行。

TEST_CASE("conformance: max_tokens reaches the wire on the responses dialect") {
    provider::Request req;
    req.model = "gpt-5.4";
    Message u; u.role = Role::User; u.text = "hi";
    req.messages.push_back(u);

    // 套:帽出现,在本方言拼音下.
    req.max_tokens = 4096;
    const json capped = provider::chatgpt::build_body_for_test(req);
    REQUIRE(capped.contains("max_output_tokens"));
    CHECK(capped["max_output_tokens"] == 4096);

    // 取消:字段完全省略. 0表示"no opinion",
    // 服务器的默认比我们发明的数字要好.
    req.max_tokens = 0;
    CHECK(!provider::chatgpt::build_body_for_test(req).contains("max_output_tokens"));
}

TEST_CASE("conformance: every dialect reads every promised request field") {
    // 结构的一半。 每行( 直径, 字段) 有一行,
    // 并有证明字段的符号。
    //
    // 如果方言真的不能表达一个字段,
    // 那就是一个合理的答案——但必须写下HERE,
    // 并说明原因,而不是因为缺席而留下。
    struct Row {
        const char* file;
        const char* field;
        const char* evidence;   // 空 = 以无法表示的方式记录
        const char* note;
    };
    static constexpr Row kRows[] = {
        // max_tokens — 输出封顶.
        {"openai/transport.cpp",    "max_tokens", "req.max_tokens",
         "Chat Completions: max_tokens / max_completion_tokens."},
        {"anthropic/transport.cpp", "max_tokens", "req.max_tokens",
         "Anthropic requires max_tokens; also clamps the thinking budget "
         "strictly below it."},
        {"ollama/transport.cpp",    "max_tokens", "req.max_tokens",
         "Ollama spells it options.num_predict."},
        {"responses/codec.cpp",     "max_tokens", "req.max_tokens",
         "Responses spells it max_output_tokens. Was MISSING until this "
         "test was written — the promise was kept by 3 of 4."},

        // 努力——是否要求推理的推理层次,以及第SSOT条(
        // 见docs/COPILOT_RESPONSES.md§6.1)。
        {"openai/transport.cpp",    "effort", "req.effort",
         "reasoning_effort, with capability discovery on rejection."},
        {"ollama/transport.cpp",    "effort", "req.effort",
         "maps onto the native think parameter."},
        {"responses/codec.cpp",     "effort", "req.effort",
         "reasoning.effort; empty tier omits the whole reasoning object."},
        {"anthropic/transport.cpp", "effort", "req.effort",
         "gates extended thinking and sizes budget_tokens."},

        // show_reasoning ——用户是否愿意 SEE 就是这样
        {"openai/transport.cpp",    "show_reasoning", "req.show_reasoning",
         "filters reasoning_content at decode."},
        {"ollama/transport.cpp",    "show_reasoning", "req.show_reasoning",
         "gates swallowed <think> spans."},
        {"responses/codec.cpp",     "show_reasoning", "req.show_reasoning",
         "gates reasoning_summary_text deltas."},
    };

    const std::filesystem::path root{AGENTTY_SRC_ROOT};
    for (const auto& r : kRows) {
        const auto path = root / "src" / "provider" / r.file;
        INFO("dialect = " << r.file << "  field = " << r.field);
        REQUIRE(std::filesystem::exists(path));

        std::ifstream in(path);
        REQUIRE(in);
        const std::string src((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());

        CHECK_MESSAGE(src.find(r.evidence) != std::string::npos,
            r.file << " never reads `" << r.evidence << "`. " << r.note
                   << "  A Request field is a promise to the user; kept by "
                      "some dialects and not others, it looks arbitrary to "
                      "them and correct to us. Either honour it here, or "
                      "change this row to record why this wire cannot "
                      "express it.");
    }
}
