// dispatch_route_test ——该类的回报. -erased 提供者 -routing 接口边界.
//
// dispatch_stream (路由、选择、Req、沉槽)是提供者
// -agnostic : 两个长 -lived 本地供应商被删除 StreamFns。
// 让我们注射 FAKE 选择所到达的路线和确切的断言——没有网络,
// 没有混凝土 传输层。这个 锁 路由表,所以未来的编辑不能默默发送
// Anthropic 转身 OpenAI 路径(反之亦然)。
//
// 我们只能廉价地观察被抹去的两条路线(人类、聊天); OpenAI-compat
// / Ollama / ACP 实际建造武器 传输层 内部,所以测试显示,
// 被清除的 -route 手臂加紧身衣 is_chatgpt (a) 在内部
// 选择其中的上游 Kind::OpenAI.

#include <memory>
#include <string>

#include "agtest.hpp"

#include "agentty/provider/dispatch.hpp"
#include "agentty/provider/prompt_policy.hpp"
#include "agentty/provider/selection.hpp"

using namespace agentty;

// 路由器,两个长 -lived 手臂只记录哪个开火,每个手臂返回
// 一个 DISTINCT StreamResult 测试可以证明 分派
// 通过去除的将结果值传播回向 接口边界 (不只是那支手臂开火)
struct Probe {
    std::string hit;
    provider::ProviderRouter router() {
        provider::ProviderRouter r;
        r.set(provider::LongLived::Anthropic,
              [this](const maya::unlocked&, provider::Request, provider::EventSink) {
                  hit = "anthropic";
                  provider::StreamResult sr;
                  sr.end  = provider::StreamEnd::CleanClose;
                  sr.stop = StopReason::EndTurn;
                  return sr;
              })
         .set(provider::LongLived::ChatGpt,
              [this](const maya::unlocked&, provider::Request, provider::EventSink) {
                  hit = "chatgpt";
                  provider::StreamResult sr;
                  sr.end   = provider::StreamEnd::TransportError;
                  sr.error = "chatgpt-boom";
                  return sr;
              });
        return r;
    }
};

static std::string route_for(const provider::Selection& sel) {
    Probe p;
    provider::dispatch_stream(agtest::cap(), p.router(), sel, provider::Request{},
                              provider::EventSink{});
    return p.hit;
}

// 一样,但还给我 StreamResult 分派 返回(结果值)。
static provider::StreamResult result_for(const provider::Selection& sel) {
    Probe p;
    return provider::dispatch_stream(agtest::cap(), p.router(), sel, provider::Request{},
                                     provider::EventSink{});
}

TEST_CASE("anthropic selection hits anthropic route") {
    // 通关 parse_selection : 路由读取 注册表 排行的 .
    // route 字段,和一排 -less Kind::Anthropic 生产上无法达到。
    CHECK(route_for(provider::parse_selection("anthropic")) == "anthropic");
}

// 转弯 工作线程 构建请求( 模式 id、 认证、 ) 提示词
// )用于一个提供者,并与该提供者一起盖章。 挂图必须遵循印章,
// 而不是现场选择: 开关, 而转弯运行用来发送该提供者的
// 模型和新开机的钥匙。 -picked 一个
TEST_CASE("a request routes to the provider it was built for") {
    provider::select(::agentty::IoAccess::grant(), provider::parse_selection("anthropic"));
    Probe p;
    provider::Request built_for_chatgpt;
    built_for_chatgpt.selection = provider::parse_selection("chatgpt");
    (void)provider::dispatch_stream(agtest::cap(), p.router(), std::move(built_for_chatgpt),
                                    provider::EventSink{});
    CHECK(p.hit == "chatgpt");   // 不是活的人类

    Probe q;
    (void)provider::dispatch_stream(agtest::cap(), q.router(), provider::Request{},
                                    provider::EventSink{});
    CHECK(q.hit == "anthropic");   // 未加印章:现场提供者
}

TEST_CASE("chatgpt selection hits chatgpt route") {
    // parse_selection 决定 注册表 行; 身份旅行 ON 选择,
    // 而不是重新 -derived 调自 a 可变 标签。
    const auto sel = provider::parse_selection("chatgpt");
    CHECK(sel.is_chatgpt());
    CHECK(route_for(sel) == "chatgpt");
}

TEST_CASE("is_chatgpt predicate") {
    provider::Selection a;
    a.kind = provider::Kind::Anthropic;
    CHECK(!a.is_chatgpt());
    CHECK(!a.is_oauth_native());

    const auto o = provider::parse_selection("openai");
    CHECK(!o.is_chatgpt());       // OpenAI-Kind 但不是聊天的行, 而不是聊天的行
    CHECK(!o.is_oauth_native());  // 打开行是 ApiKey ,没有 oauth_native

    const auto c = provider::parse_selection("chatgpt");
    CHECK(c.is_chatgpt());
    // is_oauth_native ( ) 系 , ( ) DATA-DRIVEN
    // : 读作: ProviderPreset::oauth_native 从 注册表 列,
    // 而不是一个标签比较。聊天的列是唯一设置 标志 ,所以第二个 OAuth
    // -native 提供商会把 自己的 标志 上面的上面会亮亮 上面没有代码更改
    CHECK(c.is_oauth_native());
}

// 重新设计的核心: LONG-LIVED 设置选中的路径位置,
// 纯从 注册表 数据( oauth_native 标志 + Anthropic 方言,
// 从不带标签梯子。 锁 来推导未来编辑无法引导 ChatGPT
// 转至 Anthropic 传输层 或反之为。
TEST_CASE("long lived slot derivation") {
    // 插槽 IS 编号 注册表 排的.route 字段——解决排的制作方式(
    // parse_selection ),永远不要手 -build.
    CHECK(provider::long_lived_slot(provider::parse_selection("anthropic"))
          == provider::LongLived::Anthropic);
    CHECK(provider::long_lived_slot(provider::parse_selection("chatgpt"))
          == provider::LongLived::ChatGpt);

    // 主持人 OpenAI-compat 后端有 NO 长 -lived
    // 插槽——每段构建 调用 从终点,所以 分派 掉进地上 传输层 路径。
    CHECK(provider::long_lived_slot(provider::parse_selection("groq"))
          == provider::LongLived::None);

    // 来,来,来 ACP 子进程既不长 -lived 插槽 - 它需要 ACP 手,手,手 手,手臂,手,手臂 手,臂,手,臂 手,胳膊,手,胳膊
    provider::Selection acp;
    acp.kind = provider::Kind::ExternalAcp;
    acp.acp_agent_id = "claude-agent-acp";
    CHECK(provider::long_lived_slot(acp) == provider::LongLived::None);
}

// 预热ROUTING表,锁定为纯函数(没有打开套接字).
// prewarm_target (塞尔)是 注册表-driven 证明我对
// 这个想法很热心 -host 每一个后端形状的衍生。
TEST_CASE("prewarm target table") {
    // Anthropic – 传输层的固定主机从注册表排位.
    {
        provider::Selection s;
        s.kind = provider::Kind::Anthropic;
        auto t = provider::prewarm_target(s);
        CHECK(t.should_warm());
        CHECK(t.host == "api.anthropic.com");
        CHECK(t.port == 443);
    }
    // ChatGPT → chatgpt.com (它的终点载着端口-0哨,所以 注册表
    // prewarm_host )),胜负.
    {
        provider::Selection s;
        s.kind = provider::Kind::OpenAI;
        s.openai_endpoint.label = "chatgpt";
        auto t = provider::prewarm_target(s);
        CHECK(t.should_warm());
        CHECK(t.host == "chatgpt.com");
        CHECK(t.port == 443);
    }
    // 已主机 OpenAI-compat (groq) ——温暖自己的端点主机.
    {
        provider::Selection s;
        s.kind = provider::Kind::OpenAI;
        s.openai_endpoint.label = "groq";
        s.openai_endpoint.host  = "api.groq.com";
        s.openai_endpoint.port  = 443;
        s.openai_endpoint.use_tls = true;
        auto t = provider::prewarm_target(s);
        CHECK(t.should_warm());
        CHECK(t.host == "api.groq.com");
    }
    // 当地 Ollama - 没有什么可以暖和的。
    {
        provider::Selection s;
        s.kind = provider::Kind::OpenAI;
        s.openai_endpoint.label = "ollama";
        s.openai_endpoint.host  = "localhost";
        s.openai_endpoint.port  = 11434;
        s.openai_endpoint.use_tls = false;
        auto t = provider::prewarm_target(s);
        CHECK(!t.should_warm());
    }
    // ACP 子进程 − no HTTP 层。
    {
        provider::Selection s;
        s.kind = provider::Kind::ExternalAcp;
        s.acp_agent_id = "claude-agent-acp";
        auto t = provider::prewarm_target(s);
        CHECK(!t.should_warm());
    }
    // -0号口,一个非-registry端点上的哨号 ~跳过(不要拨0).
    {
        provider::Selection s;
        s.kind = provider::Kind::OpenAI;
        s.openai_endpoint.label = "custom";
        s.openai_endpoint.host  = "example.com";
        s.openai_endpoint.port  = 0;
        s.openai_endpoint.use_tls = true;
        auto t = provider::prewarm_target(s);
        CHECK(!t.should_warm());
    }
}

// 接口边界将传输层的StreamResult传播回呼叫者——结果是
// 一个VALUE,在-erased型边界上幸存下来,而不只是在水槽
// 上的一个副作用.
TEST_CASE("dispatch propagates stream result") {
    // parse_selection ,不是一只手,没有一只手 -built 选择:
    // 路由读取 注册表 行数( sel.row - > 路线),和一行 -less
    // Kind::Anthropic 价值是指国家生产不能生产。
    const auto anth = provider::parse_selection("anthropic");
    auto ra = result_for(anth);
    CHECK(ra.end == provider::StreamEnd::CleanClose);
    CHECK(ra.ok());
    CHECK(!ra.cancelled());

    const auto cg = provider::parse_selection("chatgpt");
    auto rc = result_for(cg);
    CHECK(rc.end == provider::StreamEnd::TransportError);
    CHECK(!rc.ok());
    CHECK(rc.error.has_value() && *rc.error == "chatgpt-boom");
}

// 电话 PRECEDENCE 终止层的心脏, 被锁在隔离中。
// 未来编辑会重新排序这些检查( Q) e.g 移动 HTTP 检查上方
// 取消 检查)会默默地转动用户的 Esc 以五百人迷惑的态度,
// "HTTP 500" 错误而不是干净 取消。固定顺序是:
//   已经 -terminated > 用户 -cancel > http (中文(简体) ). -error > 传输层-error >清净.
TEST_CASE("classify stream end precedence") {
    auto tok   = std::make_shared<agentty::http::CancelToken>();
    auto fresh = std::make_shared<agentty::http::CancelToken>();

    // A 正文 已经发射的 终端 赢过 EVERYTHING 其它的,
    // 甚至一个套 取消 标志和500美元,如此清晨 -abort 绝不是错误。
    tok->cancel();
    CHECK(provider::classify_stream_end(/*终止=*/true, false, 500, tok)
          == provider::StreamEnd::AlreadyTerminated);

    // 一组 取消 符号比 a HTTP 地位和a 传输层 失败: 用户 Esc
    // 中500是一个 取消 ,而不是错误的表面。
    CHECK(provider::classify_stream_end(false, false, 500, tok)
          == provider::StreamEnd::UserCancelled);

    // 没有 取消 - 一个 HTTP QQ 400 状态比 a 传输层 错误。
    CHECK(provider::classify_stream_end(false, true, 503, fresh)
          == provider::StreamEnd::HttpError);

    // 没有 取消 ,没有 HTTP 错误,但客户端返回了! 传输层 错误。
    CHECK(provider::classify_stream_end(false, false, 0, fresh)
          == provider::StreamEnd::TransportError);

    // 名义上的一切 ~ 干净的接近( 以最后的-seen 停步理由完成).
    CHECK(provider::classify_stream_end(false, true, 0, fresh)
          == provider::StreamEnd::CleanClose);
    // 2xx 状态不是一个错误 ~ 仍然是干净的关闭。 无效 取消 符号作为
    // "not cancelled" (没有坠毁) nullptr).
    CHECK(provider::classify_stream_end(false, true, 200, nullptr)
          == provider::StreamEnd::CleanClose);
}

TEST_CASE("hosted models share agent prompt policy") {
    const auto anthropic = provider::parse_selection("anthropic");
    const auto chatgpt   = provider::parse_selection("chatgpt");
    const auto local     = provider::parse_selection("llama.cpp");

    const std::string full = provider::system_prompt_for(::agentty::IoAccess::grant(), anthropic);
    CHECK(!full.empty());
    CHECK(provider::system_prompt_for(::agentty::IoAccess::grant(), chatgpt) == full);
    CHECK(provider::system_prompt_for(::agentty::IoAccess::grant(), local) != full);

    // #91: 运行于ANOTHER 的一台 llama.cpp/vLLM
    // 仍然是本地模式, 必须获得本地的 提示词 (及其操作规则) ,
    // 而不是主机。 它曾经需要主机 \ \ localhost。
    auto lan = local;
    lan.openai_endpoint.host = "192.168.1.40";
    CHECK(provider::system_prompt_for(::agentty::IoAccess::grant(), lan)
          == provider::system_prompt_for(::agentty::IoAccess::grant(), local));

    // 91号又说: `agentty run` 和副剂选择他们的 提示词
    // 结束 uses_local_prompt ()它必须同意主转弯,
    // 或者当地模式得到主机。 提示词 没有 "don't refuse" 规则。
    CHECK(provider::uses_local_prompt(local));
    CHECK(provider::uses_local_prompt(lan));
    CHECK(!provider::uses_local_prompt(anthropic));
    CHECK(!provider::uses_local_prompt(chatgpt));
    CHECK(!provider::uses_local_prompt(provider::parse_selection("groq")));
    const auto ollama = provider::parse_selection("ollama");
    CHECK(provider::uses_local_prompt(ollama));
    CHECK(provider::local_system_prompt_for(ollama)
          == provider::system_prompt_for(::agentty::IoAccess::grant(), ollama));
    CHECK(provider::local_system_prompt_for(local)
          == provider::system_prompt_for(::agentty::IoAccess::grant(), local));
}


// 认证能力存在于 注册表 行, 不在提供者链中 -name 相较,在这之前,
// login.cpp / modal.cpp / pickers.cpp 与
// 26 字符串比较 "anthropic" / "copilot" /
// "chatgpt" / "kimi" 决定要运行的 OAuth 流,
// 是否显示方法菜单, 与缓存的 aut 如何操作 头文件 ,以及默认的哪个模式。
// 这正是 `if openai {} else if anthropic {}`
// 形状 注册表 已存在以删除: 添加一个提供者, 意味着其 对端.
//
// 这些断言将 CAPABILITIES 所以未来的一行不能悄悄地 与读
// 它们代码的代码相矛盾 不变量 自己是,他们就是 static_asserts
// 输入 registry.hpp (auth_caps_consistent
// )——这涵盖了那些看不见的语义图.
TEST_CASE("provider registry carries the auth capabilities login reads") {
    using namespace agentty::provider;

    auto row = [](std::string_view id) -> const ProviderDescriptor& {
        const auto* p = preset_for(id);
        REQUIRE(p != nullptr);
        return *p;
    };

    // ── Anthropic : 唯一的方法菜单( OAuth) 事件订阅 数字 API 键) ──
    {
        const auto& p = row("anthropic");
        CHECK(p.method_menu);
        CHECK(p.oauth_proactive_refresh);
        CHECK(!p.device_login);
        CHECK(!p.token_in_transport);   // 证明决心到一个真正的 头文件
        CHECK(p.default_model == "claude-opus-4-5");
    }

    // ── ChatGPT : 发言 Codex 流动, 由它自己持有的标志 传输层 ──
    // 也曾声称 is_local 真实的站立 -in 费用 "needs no API key" ,
    // 它迫使一个 `is_local && !oauth_native`
    // 在一个网站工作 想要文字意义。
    {
        const auto& p = row("chatgpt");
        CHECK(p.oauth_native);
        CHECK(p.token_in_transport);
        CHECK(!p.device_login);         // 拥有 oauth_native 的发射路径
        CHECK(!p.method_menu);
        CHECK(!p.is_local);             // chatgpt.com 不是本地主机
    }

    // ── 副驾驶 / Kimi: 共享通用设备发射器 ──────────
    for (std::string_view id : {"copilot", "kimi"}) {
        const auto& p = row(id);
        CHECK(p.device_login);
        CHECK(p.token_in_transport);
        CHECK(!p.method_menu);
        CHECK(!p.is_local);
    }

    // ── 本地后端从未认证 ───────────────────────────
    {
        const auto& p = row("ollama");
        CHECK(p.is_local);
        CHECK(!p.device_login);
        CHECK(!p.method_menu);
        CHECK(!p.token_in_transport);
    }

    // ── 密钥主机提供者: 根本没有 OAuth 机械 ───────────
    for (std::string_view id : {"groq", "mistral", "openrouter"}) {
        const auto& p = row(id);
        CHECK(p.auth == AuthStyle::ApiKey);
        CHECK(!p.device_login);
        CHECK(!p.method_menu);
        CHECK(!p.token_in_transport);
        CHECK(!p.oauth_proactive_refresh);
    }

    // ── is_local表示XX00120,专指: LOCALHOST,专指. ───────────────────────
    // 回归这个守护者:一个远程提供者 标记自己在本地的意思 "keyless".
    // AuthStyle::None 已经说过了
    for (const auto& p : providers())
        if (p.is_local)
            CHECK(p.host.empty() || p.host == "localhost");
}
