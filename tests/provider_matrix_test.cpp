// provider_matrix_test ——每笔 SELECTABLE 提供商,按排排列。
//
// ── 档案是用来做什么的,是故意的 ───────────────
//
// provider_conformance_test 状态 一个 合同
// 和即时付款 DIALECT (其中4个),这是正确的形状, 传输 解码,
// 它是一个工具 -call 框架错误属于。
//
// 但使用者不会选方言,他们选 "groq" 或 或 时 间 "gemini" ,
// 我们15行中的13行共用单曲 OpenAIChat 解码器,
// 这13个之间的区别是 ROW :它拨号的地方,哪个env vars
// 携带它的钥匙,它是否需要一个钥匙,它是否有第二个方言,
// 那长 -lived 传输层 时间档,这些事实让一个用户的供应商崩溃,
// 它们都是 -row 单人 -dialect.
//
// 注册表 COHERENCE 已经证明在 编译 时间( routing_consistent, endpoints_consistent,
// ids_unique, auth_caps_consistent 输入 registry.hpp).
// 此文件不重述这些内容—— 测试 -asserts a
// static_assert 上面写着表里无法证明的东西:
//
//   1. 每个行都可以被一个用户实际键入的字符串所达到,
//   2. 每行终点都能够通过圆通转弯 进入一个拨号终点,
//   3. 认证故事与行的行法一致,
//   4. 一行的方言 符合它拨打的路
//   5. 个人 -provider 手有怪声 -coded 坚持住
//
// 循环是超乎 kProviders 的, 因此在添加时会
// 测试一个新行。 这正是值得拥有的属性: 守护者所面对的失败
// 模式是某个人附加一行, 并且没有任何东西能注意到它是无法到达的。

#include <algorithm>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "agtest.hpp"

#include "agentty/provider/openai/transport.hpp"
#include "agentty/provider/registry.hpp"
#include "agentty/provider/selection.hpp"

using namespace agentty;
using namespace agentty::provider;
using json = nlohmann::json;

namespace {

// Msg 是一个 变体 域子的词 -variants 叶子要更深一层
// get_leaf<T> 挖出来, 返回 nullptr 如果该 Msg 叶子不是么.
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

// 表格中的每一行,按数值,一个环自然读取.
[[nodiscard]] std::vector<const ProviderDescriptor*> all_rows() {
    std::vector<const ProviderDescriptor*> out;
    for (const auto& p : kProviders) out.push_back(&p);
    return out;
}

// 这排能说话吗? HTTP 超过一般 OpenAI-compat 传输层?
// Anthropic (自 发) 传输层), ChatGPT (专责官员) 传输层 和。
// 和 简 况 ACP (子进程)请假 `host` 空且路由为 wire/oauth_native.
[[nodiscard]] bool http_dialled(const ProviderDescriptor& p) {
    return !p.host.empty();
}

}  // 命名空间结束。

// ── 1. 可达到性 ──────────────────────────────────────────────────────
//
// 存在一行 SELECTED。用户输入一个光谱符号 - "groq",
// "gemini", "ollama" ——和
// parse_selection 无法用自己的身份识别的一排是 仍然
// 出现在 选择面板 ,这比不存在的还要糟糕。

TEST_CASE("matrix: every provider is reachable by its own id") {
    for (const auto* p : all_rows()) {
        CAPTURE(p->id);
        const auto sel = parse_selection(std::string{p->id});
        CHECK(sel.provider_id() == p->id);
        const auto* row = preset_for(sel.provider_id());
        REQUIRE(row != nullptr);
        CHECK(row->id == p->id);
    }
}

TEST_CASE("matrix: every provider has a non-empty label and blurb") {
    // 电话 选择面板 渲染。空标签是用户无法
    // 识别的空白行;空模糊是他们无法选择的行。
    for (const auto* p : all_rows()) {
        CAPTURE(p->id);
        CHECK(!p->id.empty());
        CHECK(!p->label.empty());
        CHECK(!p->blurb.empty());
    }
}

// ── 2. 终点回合 -trip ───────────────────────────────────────────────
//
// 排队 host/path/port/tls. 选择提供者必须
// 产生一个完全带有这些内容的终点。 注册表 注释 调用 名称:
// `openai` 列 曾声称 传输层 拨号/
// v1/chat/completions ,并显示 UI 答应的推理
// 传输 从来没有送过

TEST_CASE("matrix: an http-dialled provider round-trips its endpoint") {
    for (const auto* p : all_rows()) {
        if (!http_dialled(*p)) continue;
        CAPTURE(p->id);

        const auto sel = parse_selection(std::string{p->id});
        const auto ep  = openai::Endpoint::from_spec(std::string{p->id});

        CHECK(ep.host == p->host);
        CHECK(ep.port == p->port);
        CHECK(ep.use_tls == p->use_tls);
        // 完成路径是 POST 实际的转折。 路径为空的行
        // 会拨 "/" 和 404 每一个请求。
        CHECK(!p->path.empty());
        CHECK(!p->models_path.empty());
    }
}

TEST_CASE("matrix: a provider's declared dialect matches the path it dials") {
    // 传输 路径是两个 事实相同的声明。 endpoints_consistent
    // ( 证明它是为了 TABLE 时间 编译 时间; 此检查在后面的相同
    // 保存 from_spec ()已经建立了我们实际拨打的终点.
    for (const auto* p : all_rows()) {
        if (!http_dialled(*p)) continue;
        // Ollama 说吧 自身 议定书》的执行情况 api/chat — NDJSON
        // 结构化 tool_calls ,而不是 OpenAI 嘘,它的行载着
        // Wire::OpenAIChat 因为这就是 TRANSPORT 选择,
        // 而 native_api 说 载荷 形状不同。 编译-time 证据
        // 可以免除它 完全出于这个原因, 这一点也必须。
        if (p->native_api) continue;
        CAPTURE(p->id);
        switch (p->wire) {
            case Wire::OpenAIChat:
                CHECK(p->path.find("/chat/completions") != std::string_view::npos);
                break;
            case Wire::OpenAIResponses:
                CHECK(p->path.find("/responses") != std::string_view::npos);
                break;
            case Wire::AnthropicMessages:
            case Wire::Acp:
                // 未拨号于通用 传输层; `host`
                // 到达这里意味着一行会成长一个主机,
                // 而没有人决定它意味着什么。
                FAIL("http-dialled row with a non-HTTP wire: " << p->id);
                break;
        }
    }
}

TEST_CASE("matrix: a native-protocol provider is local and un-keyed") {
    // native_api 意思是说 载荷 谓 不 编号 OpenAI 形状,
    // 所以关于 正文 今天是 关机了 今天是 Ollama
    // 单打独斗,如果第二排设置,这些是使其安全到特别的属性
    // -case : 它运行于此机器上,不需要钥匙,
    // 所以错误的猜测花费了当地400美元而不是泄露 凭据.
    for (const auto* p : all_rows()) {
        if (!p->native_api) continue;
        CAPTURE(p->id);
        CHECK(p->is_local);
        CHECK(p->auth == AuthStyle::None);
        // 及其道路不得 看起来像 OpenAI
        // 上面的豁免就是隐藏一行 可以使用通用形状的
        CHECK(p->path.find("/chat/completions") == std::string_view::npos);
    }
}

TEST_CASE("matrix: a second dialect, when present, is on the same host") {
    // 答复栏是 相同 主机的另一条路径,
    // 而不是不同的后端。 指向其它方言的行会默默
    // 发送 凭据 给A组的B组
    for (const auto* p : all_rows()) {
        if (p->responses_path.empty()) continue;
        CAPTURE(p->id);
        CHECK(http_dialled(*p));
        CHECK(p->responses_path.find("/responses") != std::string_view::npos);
        // 和默认路径不同,或者列是否定的 -op 这说明
        // dialect_for ()看似它有选择,当它没有选择。
        CHECK(p->responses_path != p->path);
    }
}

// ── 3. 认证 ──────────────────────────────────────────────────────────────

TEST_CASE("matrix: a key-authenticated provider names its env vars") {
    // 远程行 AuthStyle::ApiKey 且没有 env
    // var 只能通过 UI ——这是任何脚本的无声回归.
    // agentty ,而通常的方式 新排船一半 -wired.
    for (const auto* p : all_rows()) {
        if (p->auth != AuthStyle::ApiKey) continue;
        if (p->is_local) continue;
        CAPTURE(p->id);
        const bool any_env = std::any_of(
            p->auth_env.begin(), p->auth_env.end(),
            [](std::string_view e) { return !e.empty(); });
        CHECK(any_env);
    }
}

TEST_CASE("matrix: only a genuinely local backend is marked local") {
    // is_local 表示 "runs on this machine",
    // 不 "needs no key" 两者的混杂是为什么 ChatGPT 曾经自称是
    // 当地人—— 凭据 层跳过本地行的密钥解析度,所以一个错误的标签的远程行
    // 拨号没有自动执行符和401s。
    for (const auto* p : all_rows()) {
        CAPTURE(p->id);
        if (p->is_local) {
            // 本地后端在回转上被接通,或者没有超过HTTP.
            if (http_dialled(*p)) {
                const bool loopback = p->host == "localhost"
                                   || p->host == "127.0.0.1"
                                   || p->host == "::1";
                CHECK(loopback);
                // 当地 HTTP 默认为普通; TLS 返回
                // 时需要用户没有的证书。
                CHECK(!p->use_tls);
            }
            // 本地后端不能要求钥匙
            CHECK(p->auth == AuthStyle::None);
        }
    }
}

TEST_CASE("matrix: an oauth-native provider is long-lived and un-keyed") {
    // oauth_native 意思是这行骑着... DEDICATED
    // 传输层 才有新意,只有... 传输层 长 -lived ,
    // 它一定不会 也寻找一个 API 键。
    for (const auto* p : all_rows()) {
        if (!p->oauth_native) continue;
        CAPTURE(p->id);
        CHECK(p->lifetime == Lifetime::LongLived);
        CHECK(p->auth != AuthStyle::ApiKey);
    }
}

// ── 4. 跑道插槽 ─────────────────────────────────────────────────────

TEST_CASE("matrix: long-lived providers own distinct routing slots") {
    // 长 -lived 拥有的行 -process 传输层 状态(会话,一个符号)。
    // 两行共享一个插槽将共享该状态,这就是一个提供者的
    // 凭据最终如何应另一个提供者的请求而结束.
    std::set<int> seen;
    for (const auto* p : all_rows()) {
        if (p->lifetime != Lifetime::LongLived) continue;
        CAPTURE(p->id);
        CHECK(p->route != RouteSlot::None);
        const auto [_, inserted] = seen.insert(static_cast<int>(p->route));
        CHECK(inserted);
    }
}

TEST_CASE("matrix: per-call providers claim no routing slot") {
    // 反面:每方 -call 列从每个转弯的终点重建,
    // 没有状态可拥有。 要求一个槽将保留一个
    // 传输层 永远不存在
    for (const auto* p : all_rows()) {
        if (p->lifetime != Lifetime::PerCall) continue;
        CAPTURE(p->id);
        CHECK(p->route == RouteSlot::None);
    }
}

// ── 5. 共享解码器,每个提供者 ──────────────────────────────────
//
// 13行共享 OpenAIChat 解码器,所以解码器是
// 13个断开的提供者。兼容套件会将方言标注在其中;
// 这个标点是每个 CHAT 实际圆形 -trips 工具 调用 ——
// 一行宣讲 OpenAIChat 但不知何故,它不能解码 是一个
// 矛盾 值得在这里抓住而不是在用户的会话。

namespace {

// 一个工具 调用 设计了井的路 -behaved 聊天服务器发送.
[[nodiscard]] std::string chat_tool_call(std::string_view name,
                                         std::string_view args) {
    const auto mid = args.size() / 2;
    std::string sse;
    sse += R"(data: {"choices":[{"delta":{"tool_calls":[{"index":0,)"
           R"("id":"call_1","type":"function","function":{"name":")"
           + std::string{name} + R"(","arguments":""}}]}}]})" "\n\n";
    for (auto part : {args.substr(0, mid), args.substr(mid)})
        sse += "data: " + std::string{
            R"({"choices":[{"delta":{"tool_calls":[{"index":0,)"
            R"("function":{"arguments":)"}
            + json(std::string{part}).dump() + "}}]}}]}" + "\n\n";
    sse += R"(data: {"choices":[{"delta":{},"finish_reason":"tool_calls"}]})"
           "\n\n" "data: [DONE]\n\n";
    return sse;
}

}  // 命名空间结束。

TEST_CASE("matrix: every chat-dialect provider decodes a tool call") {
    const std::string args =
        R"({"pattern":"foo\"bar","opts":{"word":true},"n":3})";

    for (const auto* p : all_rows()) {
        if (p->wire != Wire::OpenAIChat) continue;
        // Ollama的/api/chat是NDJSON,而不是SSE——一个完全
        // 由符合套装的OllamaNative臂覆盖的不同解码器(
        // parse_ndjson_for_test).
        if (p->native_api) continue;
        CAPTURE(p->id);

        const auto msgs = openai::parse_sse_for_test(
            chat_tool_call("grep", args), {"grep"});

        std::string got;
        int starts = 0, ends = 0;
        std::string id, name;
        for (const auto& m : msgs) {
            if (const auto* s = get_leaf<StreamToolUseStart>(m)) {
                ++starts; id = s->id.value; name = s->name.value;
            }
            if (const auto* d = get_leaf<StreamToolUseDelta>(m))
                got += d->partial_json;
            if (get_leaf<StreamToolUseEnd>(m)) ++ends;
        }

        // 一次宣布,一次结束, 辩论完整和可解析。
        CHECK(starts == 1);
        CHECK(ends == 1);
        CHECK(name == "grep");
        CHECK(!id.empty());
        // 解析为本地 : doctest 内容 -expands 其宏观论点,所以 `(void)`
        // 内置 REQUIRE_NOTHROW 永远无法到达真实 调用 财务报告和已审计财务报表
        // GCC 仍然警告我们 -Wunused-result.
        json parsed;
        REQUIRE_NOTHROW(parsed = json::parse(got));
        CHECK(parsed == json::parse(args));
    }
}

// ── 6. 手势 -coded 百分比 -provider 奇数 ────────────────────────────────────
//
// 将两个差异编码为共享解码器,由 NAME。它们是唯一一个特定的提供者行为特殊的地方。
// -cased ,所以他们是唯一的地方 在那里,
// "it works for DeepSeek but not Mistral" 能够隐藏。

TEST_CASE("matrix: deepseek-style reasoning_content surfaces as thinking") {
    // DeepSeek 和大多数相似的代理 数据流 与内容平行的领域的推理。
    // StreamThinkingDelta - 一样 事件 编号
    // Anthropic 传输层 发射——所以 UI 渲染 完全一样
    const std::string sse =
        "data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"step \"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"one\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"answer\"}}]}\n\n"
        "data: [DONE]\n\n";

    const auto msgs = openai::parse_sse_for_test(sse, {});
    std::string think, text;
    for (const auto& m : msgs) {
        if (const auto* t = get_leaf<StreamThinkingDelta>(m)) think += t->text;
        if (const auto* c = get_leaf<StreamTextDelta>(m))     text  += c->text;
    }
    CHECK(think == "step one");
    CHECK(text == "answer");
}

TEST_CASE("matrix: openrouter-style empty reasoning_content is not fatal") {
    // Τ OpenRouter 传球发送 空 reasoning_content
    // 和人口众多的 `reasoning`。如果打破第一个键的显示,
    // 则会放弃所有推理——解码器必须不断寻找,直到找到内容。
    const std::string sse =
        "data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"\","
            "\"reasoning\":\"real thinking\"}}]}\n\n"
        "data: [DONE]\n\n";

    const auto msgs = openai::parse_sse_for_test(sse, {});
    std::string think;
    for (const auto& m : msgs)
        if (const auto* t = get_leaf<StreamThinkingDelta>(m)) think += t->text;
    CHECK(think == "real thinking");
}

TEST_CASE("matrix: mistral-style inline [THINK] tags are re-routed") {
    // 迷宫/法官的包装思维 THINK]…[/THINK] 内部 内容。
    // 独自一人 就会漏进答案 和领先者 '[' 运行工具
    // -call 抢救热力。
    const std::string sse =
        "data: {\"choices\":[{\"delta\":{\"content\":\"[THINK]pondering[/THINK]\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"the answer\"}}]}\n\n"
        "data: [DONE]\n\n";

    const auto msgs = openai::parse_sse_for_test(sse, {});
    std::string think, text;
    for (const auto& m : msgs) {
        if (const auto* t = get_leaf<StreamThinkingDelta>(m)) think += t->text;
        if (const auto* c = get_leaf<StreamTextDelta>(m))     text  += c->text;
    }
    CHECK(think.find("pondering") != std::string::npos);
    CHECK(text.find("pondering") == std::string::npos);
    CHECK(text.find("the answer") != std::string::npos);
}

TEST_CASE("matrix: deepseek-style <think> tags split across deltas") {
    // 标记本身可以通过服务器的块("<thi" | "nk>")来分割.
    // 一个只匹配整块标记的解码器将标记泄露到答案中.
    const std::string sse =
        "data: {\"choices\":[{\"delta\":{\"content\":\"<thi\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"nk>hidden</think>shown\"}}]}\n\n"
        "data: [DONE]\n\n";

    const auto msgs = openai::parse_sse_for_test(sse, {});
    std::string think, text;
    for (const auto& m : msgs) {
        if (const auto* t = get_leaf<StreamThinkingDelta>(m)) think += t->text;
        if (const auto* c = get_leaf<StreamTextDelta>(m))     text  += c->text;
    }
    CHECK(think.find("hidden") != std::string::npos);
    CHECK(text.find("hidden") == std::string::npos);
    CHECK(text.find("<think") == std::string::npos);
    CHECK(text.find("shown") != std::string::npos);
}
