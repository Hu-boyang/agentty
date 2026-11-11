// provider_identity_test - 供应商的 IDENTITY
// 必须在结束点覆盖后存活,并且每个 注册表 排必须有一个可以达到的模型 目录.
//
// ── 为什么这个测试存在 ─────────────────────────────────────────────────
//
// `Selection`携带两个正交事实:WHICH提供商是这个,
// WHERE拨号。它们曾经崩溃了 — 身份在每次查询中都是re-derived,
// 将`openai_endpoint.label`与字面比作:
//
//     嘘 is_copilot () 康斯特 { 返回 is_oauth_native () QQ 标签 == "copilot"; }
//
// 习惯 -host 流动 OVERWRITES 标签与用户的底部 URL。所以,
// 将自定义提供者与副驾驶端点相对抗,悄悄地摧毁了它的身份: `is_copilot()`
// 失实了 list_models_for 掉到每个手臂上, 都掉到普通的上方
// v1/models 接吧,副驾驶服务 目录 时间 `/models` - 电话 选择面板
// 空空而无差错,因为 "fell off the end of an if-chain"
// 无法区分 "this account has no models".
//
// 修补解决 注册表 线条 一次 并读取其身份。
// 这些测试将标记属性:随你喜欢的终点,行(因此,
// 目录 机制),保留.

#include <string>

#include "agtest.hpp"

#include "agentty/provider/registry.hpp"
#include "agentty/provider/selection.hpp"

using namespace agentty;
using namespace agentty::provider;

TEST_CASE("provider identity survives an endpoint override") {
    auto sel = parse_selection("copilot");
    CHECK(sel.provider_id() == "copilot");
    CHECK(sel.is_copilot());

    // 用户在自己的基地指向同一个提供者 URL。这是习惯 -host
    // 弹出面板 是否: 主机、端口和 LABEL 全部替换。
    sel.openai_endpoint.host  = "copilot.internal.corp";
    sel.openai_endpoint.label = "copilot.internal.corp";

    // 身份是该财产的财产 ROW ,而不是显示字符串——所以
    // 目录 仍然通往副驾驶自己的取车的路线,而不是贬低通用的.
    // / v1/models GET 却一无所获
    CHECK(sel.provider_id() == "copilot");
    CHECK(sel.is_copilot());
}

TEST_CASE("identity is by registry id, never by display label") {
    // 一个真正未知的自定义主机没有 注册表 绝对不能误认为奥奥特人
    // 之一 -native 后端。 (中文(简体) ).
    Selection unknown;
    unknown.kind = Kind::OpenAI;
    unknown.openai_endpoint.label = "not-a-provider.example";
    CHECK(unknown.provider_id().empty());
    CHECK_FALSE(unknown.is_copilot());
    CHECK_FALSE(unknown.is_kimi());
    CHECK_FALSE(unknown.is_chatgpt());
    CHECK_FALSE(unknown.is_oauth_native());
}

TEST_CASE("parse_selection carries the row for every registry provider") {
    // 使身份得以生存的财产: 注册表 认识 -trips 结束
    // parse_selection 回到自己身边
    for (const auto& p : providers()) {
        INFO("provider = " << std::string{p.id});
        const auto sel = parse_selection(p.id);
        CHECK(sel.provider_id() == p.id);
    }
}

TEST_CASE("the three oauth-native backends are identified by id") {
    for (const char* id : {"copilot", "chatgpt", "kimi"}) {
        INFO("provider = " << id);
        const auto sel = parse_selection(id);
        CHECK(sel.provider_id() == id);
        CHECK(sel.is_oauth_native());
    }
}

TEST_CASE("an Anthropic selection carries its row") {
    // Anthropic 永远不填充 openai_endpoint ,
    // 所以它的标签保持了结构默认—— "openai"。从该标签中衍生身份将解决
    // WRONG 排行; 无法携带.
    const auto sel = parse_selection("anthropic");
    CHECK(sel.kind == Kind::Anthropic);
    CHECK(sel.provider_id() == "anthropic");
    CHECK_FALSE(sel.is_oauth_native());
}

TEST_CASE("a custom host naming a known provider adopts its row") {
    // 迪斯科报告: "custom provider with gh copilot enabled does not provide any model"。
    // 习惯 -host Spec 没有 注册表 排队,所以它曾经得到
    // GENERIC OpenAI 默认 - / v1/models
    // 而不是副驾驶的/型号——和 选择面板 空无一物地出现。
    for (const char* spec : {"api.githubcopilot.com",
                             "api.githubcopilot.com:443",
                             "https://api.githubcopilot.com"}) {
        INFO("spec = " << spec);
        const auto sel = parse_selection(spec);
        CHECK(sel.provider_id() == "copilot");
        CHECK(sel.is_copilot());
        CHECK(sel.is_oauth_native());
        // 终点栏来自行,所以 目录 获取点击次数 副驾驶/
        // 模型而不是通用 / v1/models.
        CHECK(sel.openai_endpoint.models_path == "/models");
    }
}

TEST_CASE("a genuinely custom host keeps the endpoint the user typed") {
    // 收养必须NOT捕获无关的主机:一个真正的自定义端点保留
    // 自己的路径,不会获得虚假的身份.
    const auto sel = parse_selection("llm.internal.corp:8000");
    CHECK(sel.provider_id().empty());
    CHECK_FALSE(sel.is_oauth_native());
    CHECK(sel.openai_endpoint.host == "llm.internal.corp");
    CHECK(sel.openai_endpoint.port == 8000);
    CHECK(sel.openai_endpoint.models_path == "/v1/models");
}

// ── 智能模式/ 模式定点 ───────────────────────────────────────────
#include "agentty/provider/copilot/provider.hpp"

// 报告:
// Smart Mode 的模型固定未生效：指定 Luna xhigh 负责实现，却选到仅支持 Responses 的 GPT 5.3 codex，因端点错误失败。
//
//
// 自动解析器曾经倒置到 `selected_model` (然后,
// 任何聊天) -capable 模型),当钉住的弹片没有在
// available_models 圆柱静默地变成一个 不同 模型——成本错误,
// 能力错误,替代物是反应时400 -only 但聊天路径运行。
TEST_CASE("copilot: a pinned model is never silently substituted") {
    const std::vector<std::string> avail{"gpt-5.3-codex", "gpt-4o"};

    // 针 IS 完全可以享受荣誉。
    CHECK(copilot::pick_auto_model_for_test(avail, "gpt-5.3-codex", "gpt-4o")
          == "gpt-4o");

    // 针头不是 空的,所以呼叫者报告。
    // 不 "gpt-5.3-codex" ,这就是旧的 回退 生产。
    CHECK(copilot::pick_auto_model_for_test(avail, "gpt-5.3-codex",
                                            "gpt-5.6-luna").empty());
}

TEST_CASE("copilot: Auto still delegates the choice") {
    const std::vector<std::string> avail{"mai-code-1.1-flash", "gpt-4o"};
    // 自动表示 "you choose" : 服务器在聊天时自己选择胜负
    // -capable...
    CHECK(copilot::pick_auto_model_for_test(avail, "gpt-4o", "copilot-auto")
          == "gpt-4o");
    // ...当代表用户选择时,我们仍然 PREFER 聊天 -capable
    // 模式高于回复 -only 一条( mai) -code- ),
    // 所以自动转弯会得到最广泛的支持。 -code- *通过混凝土
    // 得到它 * -request 上面的分支。
    CHECK(copilot::pick_auto_model_for_test(avail, "mai-code-1.1-flash",
                                            "copilot-auto") == "gpt-4o");
    CHECK(copilot::pick_auto_model_for_test(avail, "", "copilot-auto") == "gpt-4o");
    // 一点好处都没有
    CHECK(copilot::pick_auto_model_for_test({}, "", "copilot-auto").empty());
}

// ── 路线: 注册表 线条 IS 路线决定 ────────────────────
//
// long_lived_slot 读取 sel.row - > 路线 — 一个字段,
// 无衍生。 Name 注册表's routing_consistent()
// static_assert 已经证明表格的连贯性 编译 时间;
// 这些 夹针 RUNTIME 同一合同的正面:什么 分派 实际看到每个标识,
// 和一行 -less (真正的习惯) 选择率降为每 -call 路径。
#include "agentty/provider/dispatch.hpp"

TEST_CASE("routing: every long-lived provider maps to its unique slot") {
    auto slot_of = [](const char* spec) {
        return provider::long_lived_slot(provider::parse_selection(spec));
    };
    CHECK(slot_of("anthropic") == provider::RouteSlot::Anthropic);
    CHECK(slot_of("chatgpt")   == provider::RouteSlot::ChatGpt);
    CHECK(slot_of("copilot")   == provider::RouteSlot::Copilot);
    CHECK(slot_of("kimi")      == provider::RouteSlot::Kimi);
    // 人数 -call 行没有槽:由通用的比对服务 传输层.
    CHECK(slot_of("openai")    == provider::RouteSlot::None);
    CHECK(slot_of("ollama")    == provider::RouteSlot::None);
    // 真正的自定义主机完全没有行。 -call 建筑工程。
    CHECK(slot_of("http://my-llama-box:8080/v1")
          == provider::RouteSlot::None);
}

TEST_CASE("routing: slot identity survives an endpoint override") {
    // 习惯 -host 错误类 此设计已退休: 指定已知
    // 提供者主机的自定义光谱 ROW 和行来 路线槽,
    // 所以它服务 相同的长 -lived 传输层 (同
    // OAuth 会话),非默默退化为通用 -call 路径。
    auto sel = provider::parse_selection("https://api.githubcopilot.com");
    CHECK(provider::long_lived_slot(sel) == provider::RouteSlot::Copilot);
}

TEST_CASE("routing: slot tags match the dispatch.turn route vocabulary") {
    // slot_tag 输入 `route=` 区域 dispatch.turn
    // 日志行; 排除这些字符串的 docs grep 故障。
    CHECK(provider::slot_tag(provider::RouteSlot::Anthropic) == "anthropic-messages");
    CHECK(provider::slot_tag(provider::RouteSlot::ChatGpt)   == "chatgpt-responses");
    CHECK(provider::slot_tag(provider::RouteSlot::Copilot)   == "copilot");
    CHECK(provider::slot_tag(provider::RouteSlot::Kimi)      == "kimi");
}

#include "agentty/provider/auth_state.hpp"
#include "agentty/store/store.hpp"

TEST_CASE("signed_in: a saved custom host counts, even with no key") {
    store::Settings s;
    auth::AuthView v;
    const std::string tls   = "https://pod-8888.proxy.runpod.net/v1";
    const std::string local = "http://127.0.0.1:8080/v1";
    CHECK_FALSE(provider::signed_in(tls, s, v));     // 从未添加
    s.provider_keys[tls]   = "";                     // 无键远程主机
    s.provider_keys[local] = "";                     // 无密钥本地主机
    CHECK(provider::signed_in(tls, s, v));
    CHECK(provider::signed_in(local, s, v));
    s.provider_keys[tls] = "sk-x";
    CHECK(provider::signed_in(tls, s, v));
}

TEST_CASE("signed_in: hosted presets still need a real key") {
    store::Settings s;
    auth::AuthView v;
    s.provider_keys["groq"] = "";
    CHECK_FALSE(provider::signed_in("groq", s, v));
    s.provider_keys["groq"] = "gsk-x";
    CHECK(provider::signed_in("groq", s, v));
    // 一个空的id是Anthropic,从不自定义主机.
    CHECK_FALSE(provider::signed_in("", s, v));
}
