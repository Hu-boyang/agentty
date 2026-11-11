#pragma once
// agentty::provider——提供者REGISTRY：一个静态的、
// 有序的表，它是每个后端agentty可以与之对话的单一事实来源。
//
// 为什么是 注册表 (而不是 if/else 链)：提供商选择正朝着
// "all providers, first-class" 方向发展。每个需要了解后端的地方
// - 提供者 选择面板 UI、`parse_selection`、main.cpp、
// `Endpoint::from_spec` 中的 per-provider auth-env 分辨率、
// 型号徽章 - 用于硬编码 2-way Anthropic/OpenAI 分支
// （或 `if (label == "groq")` 链）。添加提供商意味着编辑 N 调用
// 站点并希望您能找到所有这些站点。
//
// 现在：将 一个 行添加到下面的 `kProviders` 中，选择面板 显示它，
// 身份验证解析器知道它的环境变量，徽章标记它。该表是 `constexpr`，
// 它可以是 (ids/labels/flags)，端点保留为
// `openai::Endpoint::from_spec`，因此 传输-shape (path/port/tls)
// 位于使用它的 传输层 旁边。
//
// 注册表故意让NOT拥有具体的Provider类型——该类型保留在类型-erased
// `Deps::stream` 接口边界之后（参见runtime/app/deps.hpp)。
// 注册表行仅描述*哪个*后端以及*如何认证*；构建正确的
// 传输层 是 main.cpp 的工作，于 `Kind` 分派。

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "agentty/util/identity.hpp"

namespace agentty::provider {

// ── 供方的两根正交轴 ────────────────────────────────
//
// `Kind`过去常常将它们合并为一个枚举，这就是为什么"is this ChatGPT?"必须在六个站点将
// -derived重新修改为`kind == OpenAI && label == "chatgpt"`的原因。
// 它们在这里分开：
//
//   传输-on-the-wire DIALECT(serializer/parser一轮使用)。
//              这是 传输层 层分支的 仅 事物。
//   生命周期-具体的Provider是否被构建一次并在整个
//              过程中重用（它保持连接/OAuth-token状态），
//              或者是从活动端点的每个调用重建的廉价值。
//
// 提供方身份，例如 chatgpt 与 groq。
// 绝不是枚举——它是行的`id`。任何用于打开身份的内容现在都会从行中读取功能字段
// （请参阅ProviderDescriptor），因此添加提供程序就是添加一行。

// 其中传输方言一转就是serialized/parsed。多个提供商共享一种
// 方言（每个托管的 OpenAI-compat 克隆都说 OpenAIChat）；
// 仅当提供程序需要 新 方言时，它才是特殊的。
enum class Wire : std::uint8_t {
    AnthropicMessages,  // Anthropic/v1/messages SSE。
    OpenAIResponses,    // OpenAI/响应SSE(Codex/ChatGPT+api.openai.com)。
    OpenAIChat,         // OpenAI /chat/completions 使用 SSE，Ollama /api/chat 使用 NDJSON。
    Acp,                // 外部ACP代理子进程(无HTTP方言)。
};

// 具体的Provider是long-lived（拥有刷新的代币/温暖的池，
// 因此它在main()中构造一次并重复使用）还是根据调用构造？
enum class Lifetime : std::uint8_t {
    PerCall,    // 便宜的值传输层，从活动端点重建。
    LongLived,  // 保持cross-turn状态； main() 拥有一个实例。
};

// `Kind`被保留为COARSE，是少数仅关心
// "Anthropic vs OpenAI-family vs ACP subprocess"的调用站点的`Wire`的派生视图。
// 它不再存储在一行中——它是 传输 的纯函数——因此它永远不会
// 偏离方言。新代码应该优先选择 `Wire` + 能力字段；
// `Kind` 留下来只是为了保持迁移的机械性。
enum class Kind : std::uint8_t { Anthropic, OpenAI, ExternalAcp };

[[nodiscard]] constexpr Kind kind_of(Wire w) noexcept {
    switch (w) {
        case Wire::AnthropicMessages: return Kind::Anthropic;
        case Wire::Acp:               return Kind::ExternalAcp;
        case Wire::OpenAIResponses:
        case Wire::OpenAIChat:        return Kind::OpenAI;
    }
    return Kind::OpenAI;
}

// 提供者如何进行认证——驱动UI提示以及认证解析器
// 咨询哪些环境变量。
enum class AuthStyle : std::uint8_t {
    OAuthOrKey,  // Anthropic:OAuth(Pro/Max)或x-api-key。
    ApiKey,      // 托管OpenAI-family:来自env/--key的承载密钥。
    None,        // 本地服务器(Ollama/llama.cpp)：不需要认证。
};

// 哪个long-lived提供商时隙服务该行。 LongLived-lifetime
// 提供者持有 cross-turn 状态（刷新的 OAuth 令牌/温暖的
// 传输层），因此它在 main() 中构造 一次 并重用； 分派 通过该值对路
// 由器进行索引。 `None` = 由 per-call 传输层（通用
// OpenAI-compat/Ollama 构建器）或 ACP 臂提供服务。
//
// 这个住ON 该 ROW——不在一个if-chain里面分派——所以路由是
// 一个数据查找，下面的static_asserts可以和PROVE表连贯：
// 添加没有槽的 OAuth-native 提供者或者没有 long
// -lived 生命周期 的槽是 编译 错误，而不是现场错误。
enum class RouteSlot : std::uint8_t { None, Anthropic, ChatGpt, Copilot, Kimi };
inline constexpr std::size_t kRouteSlots = 5;

// 槽的Log/diagnostic标签——dispatch.turn中的`route=`值。
[[nodiscard]] constexpr std::string_view slot_tag(RouteSlot s) noexcept {
    switch (s) {
        case RouteSlot::Anthropic: return "anthropic-messages";
        case RouteSlot::ChatGpt:   return "chatgpt-responses";
        case RouteSlot::Copilot:   return "copilot";
        case RouteSlot::Kimi:      return "kimi";
        case RouteSlot::None:      return "";
    }
    return "";
}

// 一个后端agentty知道如何到达：它的身份、它如何认证、它讲哪种传输方言、
// 以及它的生命周期。 POD + string_view 所以整个表是
// 一个常量-initialised `constexpr` 数组，堆为零。
//
// 这是THE的事实来源。用于打开提供程序的每个子系统（分派、型号-list、
// 预热、登录门、选择面板）都会在此处读取字段。 "Add a provider"
// == "append a row"。
struct ProviderDescriptor {
    std::string_view id;        // 规范规范令牌("anthropic"，"groq")。
    std::string_view label;     // 显示名称为选择面板/徽章。
    std::string_view blurb;     // one-line描述（选择面板尾随文本）。
    Wire             wire;       // on-the-wire方言(传输层在此分支)。
    Lifetime         lifetime;   // long-lived (拥有状态) vs 根据 调用 重建。
    AuthStyle        auth;
    // TRUE 仅适用于在此计算机上运行的后端（Ollama，自定义本地主机主机）。
    // 不 "needs no API key" — 即 AuthStyle::None，
    // 将两者混为一谈就是 ChatGPT 曾经声称是本地的原因。
    // 通过 凭据 层读取可跳过密钥解析，通过 选择面板 层读取可跳过密钥
    // 提示词。
    bool             is_local;

    // （按顺序）咨询环境变量以找到该提供商的 API 密钥。最后一个条目
    // 通常是通用的 OPENAI_API_KEY 回退。 Anthropic
    // （信用来自 `agentty login`）和本地后端为空。
    // 存储为string_views的固定3-slot数组；未使用的插槽是 ""。
    std::array<std::string_view, 3> auth_env;

    // 密钥PREFIXES足够独特，足以仅从粘贴的密钥中识别该提供商，
    // 首先是最具体的。未使用的插槽是 ""。
    //
    // 为什么这位于行：`agentty login`和第一个-run 弹出
    // 面板都接受"an API key"，然后无处询问它属于哪个提供者，
    // 因此每个粘贴的密钥都被写入Anthropic存储（问题#68）。
    // 提示词 表示 "or any provider"，
    // 代码意味着 "Anthropic"——Groq 的密钥 已保存 被接受，
    // 然后在第一回合失败，出现身份验证错误，指定了错误的供应商。
    //
    // 检测 HAS 是 注册表 事实，而不是身份验证层中
    // 某处的开关：新的提供者已经在这里声明了其主机、
    // 其环境变量及其 传输 方言，而位于其他任何地方的前缀表又是
    // 一个需要忘记的地方。这与上面的 auth_env 的推理
    // 相同。
    //
    // ONLY明确的前缀属于这里。 `sk-` 由 Anthropic、
    // OpenAI 和 DeepSeek 共享，
    // 因此它不识别任何内容并且故意不存在——默默地选择错误的
    // 提供者的猜测比询问更糟糕，因为用户随后调试他们认为
    // 正确的 凭据。空匹配意味着 "ask"，这是诚实的结果。
    std::array<std::string_view, 2> key_prefixes;

    // 当NOT可从运行时端点导出时，FIXED主机在第一次转动之前打开
    // 到TLS的热连接。两个后端需要这个：Anthropic（传输层
    // 硬编码 api.anthropic.com；没有拨打端点）和
    // ChatGPT（与 chatgpt.com/backend-api/codex 通信，
    // 而其端点带有哨兵端口 0）。每隔一行为空：
    // 托管OpenAI-compat后端预热其自己的Endpoint主机，
    // 而本地/ACP后端没有什么值得预热的。
    // 这使得 prewarm_active_provider 成为数据查找而不是 hand-written 开关。
    std::string_view prewarm_host;

    // OAuth-native:后端通过"sign in with <provider>"(其
    // 自己的OAuth流程、-process中的令牌auto-refreshed)而不是持有者
    // API密钥、AND进行身份验证，它使用DEDICATED long-lived 传输层而不是
    // 通用的OpenAI-compat。今天只有 ChatGPT/Codex 设置了这个（
    // Anthropic OAuth 使用 AnthropicMessages，所以它永远不需要路
    // 由 标志）。 "This endpoint is special" 是该行的 DATUM，
    // 在 routing/prewarm/login 站点上读取 - 第二个此类提供程序仅设置 标志。
    bool oauth_native = false;

    // ── 传输 结束点( 结束点) HTTP 事实) ───────────────────────────────
    // 实际拨打该提供商的位置。这些曾经住在 14-arm if-chain
    // 里面 Endpoint::from_spec、RE-STATING
    // host/path 事实已经是这一排的 half-present 了。
    // 两个事实来源完全符合您的预期：`openai` 行声称
    // Wire::OpenAIResponses，
    // 而 传输层 拨打了 /v1/chat/completions，这默默地使推理
    // -text UI 承诺了 传输 从未兑现的东西。
    //
    // 将它们保留在 此处（距 `wire` 多一列）使得此类漂移
    // 无法表示：一行显示“响应”但指向 /chat/completions
    // 现在是单行上的明显矛盾，并且下面的
    // static_assert 在 编译 时间拒绝它。
    //
    // 空`host`意味着"not dialled over the generic OpenAI-compat transport":
    // Anthropic(自己的传输层，硬编码主机)、
    // ChatGPT(专用OAuth 传输层)和ACP(子进程，
    // 无HTTP)将这些留空并由`wire`/`oauth_native`来路由。
    std::string_view host;          // "api.groq.com"; "" = 不是 HTTP-dialled。
    std::string_view path;          // 完成路径，e.g。 "/v1/chat/completions"。
    std::string_view models_path;   // 型号-list路径，e.g。 "/v1/models"。
    std::uint16_t    port = 443;
    bool             use_tls = true;

    // ── SECOND方言(可选) ────────────────────────────────
    // 主机并不总是ONE方言。上面的`wire`/`path`
    // 是该行的DEFAULT；这是 相同 主机上的响应路径（
    // 如果有的话）。空 = 该主机仅讲其默认方言。
    //
    // 为什么这个专栏必须存在：OpenAI自己的文档现在说从
    // GPT-5.4开始，NOT支持使用reasoning_effort
    // 而不是`none`进行工具调用 - 并且GPT-6
    // -class模型完全放弃了聊天功能-calling。
    // 因此，对于推理代理来说，方言不是风格偏好，而是工作轮次和
    // 400 之间的区别。因此，主机需要通告 两者 路径并
    // 让 per-model 谓词进行选择。
    //
    // Copilot 在它成为列之前证明了形状：它的行显示 OpenAIChat，
    // 而 `prefers_responses_dialect()` 悄悄地将
    // -routed gpt-5* 和 mai-code-* 重新设置为 /responses
    // — 该行再次躺着，只是在函数中而不是在字段中。在本专栏中，谎言有了真实
    // 存在的地方，`endpoints_consistent()` 会检查它。
    std::string_view responses_path;   // ""=此处没有响应端点。

    // ── app 属性( 可选) ───────────────────────────────────
    // 一些聚合器要求客户端NAME ITSELF，
    // 并为此向用户返还一些东西。 OpenRouter 是当今
    // 存在的情况：携带 `HTTP-Referer`（+
    // `X-Title`）的请求被记入应用程序页面，显示在公共排名中，
    // 并且（用户实际感受到的部分）满足仅服务于公认的代理工具的模型。
    // 问题＃74。
    //
    // COLUMN，不是 传输层 中的分支，因为上面的端点块
    // 存在：否则身份将是位于 `if (label == "openrouter")`
    // 旁边的字符串文字，其与从这些行漂移的 14
    // -arm if-chain 具有相同的形状。
    // 这里的事实是，它所属的主机旁边有一条线路，
    // 每个拨打该行的 传输层 都会免费接听该行，
    // 而不需要归属的行则只需将其留空即可。
    //
    // 这是故意将NOT作为一般"static headers"逃生舱口：
    // Endpoint::extra_headers已经在per
    // -request协议垃圾（Copilot的编辑器块、Kimi的设备ID）中
    // 扮演该角色。此列有意变窄 - 它回答 "who is calling"，
    // 因此它包含 URL 和显示名称，仅此而已，并且值来自 一个 位置
    // agentty 声明其自己的身份 (identity.hpp)。
    bool             attribution = false;

    // 该行的API接受OpenAI的`prompt_cache_key` 正文字段。
    //
    // OpenAI-COMPATIBLE不是OpenAI：兼容层严格验证正文并且
    // 400未知字段，杀死转弯(#93)--Groq:
    //   "property 'prompt_cache_key' is unsupported"
    //   Gemini:
    // "Unknown name \"prompt_cache_key\": Cannot find field."（未知字段 prompt_cache_key，无法找到对应字段。）
    // 所以这是每行opt-IN，仅在API记录字段(OpenAI)或传递它的地方设置
    // （OpenRouter，路由到 OpenAI 并在其他地方忽略它）。
    // 忽略它会导致 缓存 优化；将其发送到严格的主机会花费整个回合。如有疑问，请省略。
    bool             accepts_prompt_cache_key = false;

    // Ollama的NATIVE/api/chat协议(NDJSON，
    // 结构化tool_calls)代替其OpenAI-compat垫片，这使得弱局部模型
    // 泄漏工具调用作为`content`中的原始JSON。
    bool             native_api = false;

    // ── 授权能力(什么是 LOGIN 流动需要知道) ───────
    // 这四个曾经是 ~26provider-name 字符串比较分散在
    // login.cpp/modal.cpp/pickers.cpp
    // — 正是 注册表 存在要删除的 `if anthropic {} else if copilot {}` 链。
    // 每个都是分支真正测试的 CAPABILITY，
    // 因此新的 OAuth 提供程序成为行编辑而不是 grep。

    // 运行device/browser OAuth流程（无密钥提示词)。
    // 副驾驶和 Kimi 共用通用发射器； ChatGPT 有自己的，因为 Codex
    // 流在 运行时 处协商设备-vs-browser（请参阅oauth_native）。
    bool device_login = false;

    // 该行是否使用定制的ChatGPT/Codex登录流程？
    //
    // PREDICATE而不是原始标志测试，因为明显的测试是错误的并且在两个调用站点处是错误的。
    // `oauth_native` 读作 "the Codex one" 但实际上意味着
    // 通过登录认证，并使用自身长期保持的传输连接。
    // ——对于 Copilot 和 Kimi 来说也是如此，这正是 credentials.cpp
    // 的用法。因此，`if (p->oauth_native)` 将每个设备
    // -flow 提供商发送到ChatGPT 浏览器流程中：选择
    // "GitHub Copilot" 打开OpenAI 标志-in 面板。
    //
    // 两个标志不是独立的——device_login是更具体的声明
    // 并获胜——因此该关系属于这里一次，而不是被每个调用者re
    // -derived（和mis-ordered）。
    [[nodiscard]] constexpr bool codex_login() const noexcept {
        return oauth_native && !device_login;
    }

    // 登录弹出面板提供了CHOICE方法(OAuth 事件订阅 vs API密钥)。
    // 今天只有 Anthropic 这样做：其他一切要么是 key-only
    // 要么是 OAuth-only，并且显示 one-item 菜单是噪音。
    bool method_menu = false;

    // 令牌存在于提供者的OWN存储中（传输层每轮读取它们）而不是
    // 存在于可解析的AuthHeader中。在帐户切换时，
    // 这些需要缓存的 头文件 CLEARED，而不是替换 -
    // resolve() 按设计为它们返回空。
    bool token_in_transport = false;

    // Anthropic-only真正不统一的附加功能：long
    // -idle OAuth令牌在帐户切换时获得主动后台刷新，
    // 并且学习到的"1M context is blocked" 标志是re
    // -armed，因为切换的-to帐户可能拥有不同的权利。
    bool oauth_proactive_refresh = false;

    // Model在切换TO时选择该提供商，没有召回选择，并且还没有实时目录
    // （交换机在UI 线程上运行，因此无法获取）。
    // 空 = "let the ModelsLoaded refetch auto-select the first available"，
    // 这适用于本地后端以及 line-up 为 server
    // -driven 的任何提供商。
    //
    // 静态默认值属于该行； 派生 则不然 — ChatGPT 会读取其
    // 缓存的 目录，而 Copilot 的 line-up 因层而异，
    // 因此这些内容会保留在代码中，并在 调用 站点上写下原因。
    std::string_view default_model;

    // 服务该行的long-lived路由槽(参见上面的RouteSlot)。
    // RouteSlot::None =由wire/native_api在分派时间选择的每-call
    // 传输层。 kProviders 下的 static_asserts 已证明与
    // lifetime/oauth_native 一致。
    RouteSlot route = RouteSlot::None;

    // 当通过通用 OpenAI-compat 传输层 (i.e。
    // 它携带上面的真实端点数据)到达该行时为真。
    [[nodiscard]] constexpr bool http_dialled() const noexcept {
        return !host.empty();
    }

    // 粗类视图，源自`wire`，因此它永远不会漂移。为仅需要 Anthropic
    // -vs-OpenAI-family-vs-ACP 的 调用 站点保留。
    [[nodiscard]] constexpr Kind kind() const noexcept { return kind_of(wire); }
};

// 旧名称。 `ProviderPreset` 是在增长
// wire/lifetime 并成为行为事实来源之前的行类型；保留为别名，
// 以便命名它的约十几个 调用 站点在迁移期间和迁移后继续编译。
using ProviderPreset = ProviderDescriptor;

// ── 桌子上写着 ────────────────────────────────────────────────────────────
// 顺序=选择面板中的显示顺序。首先是 Anthropic（默认），
// 然后按受欢迎度托管 OpenAI-family，最后是本地后端。
//
// 要添加提供者：在此处附加ONE行。下面的端点列是 host/path
// 存在的唯一位置 - Endpoint::from_spec 是对
// 此表的查找，因此没有第二个臂可以保持同步。
inline constexpr std::array<ProviderDescriptor, 15> kProviders{{
    {.id = "anthropic", .label = "Anthropic",
     .blurb = "Claude — OAuth (Pro/Max) or API key",
     // 自己的传输层(不是OpenAI-compat之一)：没有端点列。
     // 在账户切换上获得主动令牌刷新+1M-entitlement
     // re-arm的一行。
     .wire = Wire::AnthropicMessages, .lifetime = Lifetime::LongLived,
     .auth = AuthStyle::OAuthOrKey,
     // sk-ant-涵盖API密钥(sk-ant-api03-)和OAuth
     // 令牌(sk-ant-oat01-)。明确，与 OpenAI 和
     // DeepSeek 也使用的裸 sk- 不同——这就是为什么这些行都没有前缀。
     .key_prefixes = {"sk-ant-", ""},
     .prewarm_host = "api.anthropic.com",
     .method_menu = true, .oauth_proactive_refresh = true,
     .default_model = "claude-opus-4-5",
     .route = RouteSlot::Anthropic},

    {.id = "openai", .label = "OpenAI", .blurb = "GPT / Codex — api.openai.com",
     // DEFAULT是聊天完成；下面的`responses_path`
     // 带有第二种方言。该行曾经在拨打/v1/chat/completions时声称Wire::
     // OpenAIResponses——标签是装饰性的（上面没有发送
     // 任何内容），因此谎言没有被注意到，并使得-text
     // UI超过-promise。 传输和路径必须一致；
     // 第二 路径现在是它自己的一列，而不是第一列中的谎言。
     .wire = Wire::OpenAIChat, .lifetime = Lifetime::PerCall,
     .auth = AuthStyle::ApiKey,
     .auth_env = {"OPENAI_API_KEY", "CODEX_API_KEY", ""},
     .host = "api.openai.com", .path = "/v1/chat/completions",
     .models_path = "/v1/models",
     // 推理模型通过dialect_for()路由至此处。非化妆品：
     // GPT-5.4+通过真实的推理努力拒绝聊天工具调用。
     .responses_path = "/v1/responses",
     // 该领域的主页：记录在聊天完成和响应中。
     .accepts_prompt_cache_key = true,
     .default_model = "gpt-4o"},

    {.id = "chatgpt", .label = "ChatGPT",
     .blurb = "Sign in with ChatGPT — Codex models, no API key",
     // 真实的响应——但是通过其 自身 OAuth 传输层 (/
     // backend-api/codex/responses)，而不是兼容的，所以没有端点列。
     // is_local 保持 FALSE：chatgpt.com 是远程主机，
     // "needs no API key" 是 AuthStyle::None 已经说过的内容。
     // 无default_model:缓存的目录选秀。
     .wire = Wire::OpenAIResponses, .lifetime = Lifetime::LongLived,
     .auth = AuthStyle::None,
     .prewarm_host = "chatgpt.com", .oauth_native = true,
     .token_in_transport = true,
     .route = RouteSlot::ChatGpt},

    {.id = "copilot", .label = "GitHub Copilot",
     .blurb = "Sign in with GitHub — Copilot models, no API key",
     // gpt-4o在每个副驾驶层上运行；切换后不久，
     // 异步获取会将其替换为帐户的真实行-up（包括自动）。
     .wire = Wire::OpenAIChat, .lifetime = Lifetime::LongLived,
     .auth = AuthStyle::None,
     .prewarm_host = "api.githubcopilot.com", .oauth_native = true,
     .host = "api.githubcopilot.com", .path = "/chat/completions",
     .models_path = "/models",
     // 副驾驶是 始终 混合型 — gpt-5* /mai-code-* 只对 / 响应起作用。
     // 该事实过去仅存在于prefers_responses_dialect()内；
     // 现在它暗示的端点位于可以检查的行上。
     // (现场主机从endpoints.api开始被覆写于-session；这是路径，
     // 在Individual/Business/Enterprise.期间是稳定的)
     .responses_path = "/responses",
     .device_login = true, .token_in_transport = true,
     .default_model = "gpt-4o",
     .route = RouteSlot::Copilot},

    {.id = "kimi", .label = "Kimi",
     .blurb = "Sign in with Kimi — Kimi K2 models, no API key",
     // Kimi代码推断API：基础
     // https://api.kimi.com/coding/v1（其自己的提供者构建此；列必须与其一致）。
     .wire = Wire::OpenAIChat, .lifetime = Lifetime::LongLived,
     .auth = AuthStyle::None,
     .prewarm_host = "api.kimi.com", .oauth_native = true,
     .host = "api.kimi.com", .path = "/coding/v1/chat/completions",
     .models_path = "/coding/v1/models",
     .device_login = true, .token_in_transport = true,
     .route = RouteSlot::Kimi},

    {.id = "groq", .label = "Groq",
     .blurb = "Llama/Mixtral on Groq LPUs — very fast",
     .wire = Wire::OpenAIChat, .lifetime = Lifetime::PerCall,
     .auth = AuthStyle::ApiKey,
     .auth_env = {"GROQ_API_KEY", "OPENAI_API_KEY", ""},
     .key_prefixes = {"gsk_", ""},
     .host = "api.groq.com", .path = "/openai/v1/chat/completions",
     .models_path = "/openai/v1/models"},

    {.id = "openrouter", .label = "OpenRouter",
     .blurb = "Any model via openrouter.ai",
     .wire = Wire::OpenAIChat, .lifetime = Lifetime::PerCall,
     .auth = AuthStyle::ApiKey,
     .auth_env = {"OPENROUTER_API_KEY", "OPENAI_API_KEY", ""},
     .key_prefixes = {"sk-or-v1-", "sk-or-"},
     .host = "openrouter.ai", .path = "/api/v1/chat/completions",
     .models_path = "/api/v1/models",
     // OpenRouter运送了OpenAI-compatible响应端点；
     // 上游背后的推理模型需要与first-party相同的路由。
     .responses_path = "/api/v1/responses",
     // 命名我们自己：OpenRouter 将归因于应用程序页面的
     // 请求归功于某些模型仅提供公认的工具。问题＃74。
     .attribution = true,
     // 传递到 OpenAI 上游（在此处固定 缓存 节点）并为
     // 其他节点丢弃：OpenRouter 标准化 正文。
     .accepts_prompt_cache_key = true},

    {.id = "together", .label = "Together",
     .blurb = "Open models on together.ai",
     .wire = Wire::OpenAIChat, .lifetime = Lifetime::PerCall,
     .auth = AuthStyle::ApiKey,
     .auth_env = {"TOGETHER_API_KEY", "OPENAI_API_KEY", ""},
     .host = "api.together.xyz", .path = "/v1/chat/completions",
     .models_path = "/v1/models"},

    {.id = "cerebras", .label = "Cerebras",
     .blurb = "Wafer-scale inference — very fast",
     .wire = Wire::OpenAIChat, .lifetime = Lifetime::PerCall,
     .auth = AuthStyle::ApiKey,
     .auth_env = {"CEREBRAS_API_KEY", "OPENAI_API_KEY", ""},
     .host = "api.cerebras.ai", .path = "/v1/chat/completions",
     .models_path = "/v1/models"},

    {.id = "deepseek", .label = "DeepSeek",
     .blurb = "DeepSeek V4 — api.deepseek.com",
     // DeepSeek提供补全WITHOUT/v1前缀。
     .wire = Wire::OpenAIChat, .lifetime = Lifetime::PerCall,
     .auth = AuthStyle::ApiKey,
     .auth_env = {"DEEPSEEK_API_KEY", "OPENAI_API_KEY", ""},
     .host = "api.deepseek.com", .path = "/chat/completions",
     .models_path = "/models"},

    {.id = "xai", .label = "xAI (Grok)", .blurb = "Grok models — api.x.ai",
     .wire = Wire::OpenAIChat, .lifetime = Lifetime::PerCall,
     .auth = AuthStyle::ApiKey,
     .auth_env = {"XAI_API_KEY", "OPENAI_API_KEY", ""},
     .key_prefixes = {"xai-", ""},
     .host = "api.x.ai", .path = "/v1/chat/completions",
     .models_path = "/v1/models"},

    {.id = "mistral", .label = "Mistral",
     .blurb = "Mistral / Codestral / Magistral — api.mistral.ai",
     .wire = Wire::OpenAIChat, .lifetime = Lifetime::PerCall,
     .auth = AuthStyle::ApiKey,
     .auth_env = {"MISTRAL_API_KEY", "OPENAI_API_KEY", ""},
     .host = "api.mistral.ai", .path = "/v1/chat/completions",
     .models_path = "/v1/models"},

    {.id = "gemini", .label = "Google Gemini",
     .blurb = "Gemini models via the OpenAI-compat API",
     .wire = Wire::OpenAIChat, .lifetime = Lifetime::PerCall,
     .auth = AuthStyle::ApiKey,
     .auth_env = {"GEMINI_API_KEY", "GOOGLE_API_KEY", "OPENAI_API_KEY"},
     // AIzaSy是旧的开发者API密钥前缀； AQ。是较新的 AI-Studio-issued
     // 密钥格式（请参阅 Google AI Studio 论坛报告 -
     // ai.google.dev/t/my-account-only-generates-aq-keys/170801
     // 和兄弟 线程）。新的 AI Studio 帐户现在发行 AQ。
     // 独占密钥，因此将它们保留为未路由，将它们中的每一个都通过粘贴发送
     // 到 Anthropic 存储。两个前缀都带有 Gemini 密钥。
     .key_prefixes = {"AIzaSy", "AQ."},
     .host = "generativelanguage.googleapis.com",
     .path = "/v1beta/openai/chat/completions",
     .models_path = "/v1beta/openai/models"},

    {.id = "fireworks", .label = "Fireworks",
     .blurb = "Open models on fireworks.ai",
     .wire = Wire::OpenAIChat, .lifetime = Lifetime::PerCall,
     .auth = AuthStyle::ApiKey,
     .auth_env = {"FIREWORKS_API_KEY", "OPENAI_API_KEY", ""},
     .host = "api.fireworks.ai", .path = "/inference/v1/chat/completions",
     .models_path = "/inference/v1/models"},

    {.id = "ollama", .label = "Ollama",
     .blurb = "Local models at localhost:11434",
     // 环回上的普通HTTP，以及NATIVE/api/chat协议。
     .wire = Wire::OpenAIChat, .lifetime = Lifetime::PerCall,
     .auth = AuthStyle::None, .is_local = true,
     .host = "localhost", .path = "/api/chat", .models_path = "/api/tags",
     .port = 11434, .use_tls = false, .native_api = true},

    // NOTE:无"llama.cpp"预设行。 llama.cpp / vLLM / LM Studio 服务
    // 器只是一个通用的 OpenAI-compatible 主机 - 使用 "Custom host…"（
    // 选择面板 提供，保存，并且可以删除）。 openai/transport.cpp 中的 from_spec(
    // "llama.cpp") 别名被保留，因此 CLI 上的 `--provider llama.cpp`
    // 仍解析为 localhost:8080。
}};

// 所有预设，针对选择面板/迭代。
[[nodiscard]] inline std::span<const ProviderPreset> providers() noexcept {
    return {kProviders.data(), kProviders.size()};
}

// 通过预设的规范ID查找预设。对于未知 id（e.g。
// 原始 "host:port" 自定义端点，没有预设行）返回 nullptr。
[[nodiscard]] inline const ProviderPreset* preset_for(std::string_view id) noexcept {
    for (const auto& p : kProviders)
        if (p.id == id) return &p;
    return nullptr;
}

// 仅通过其前缀判断该API密钥属于哪个提供商？
// 当没有行声明它时为空——这是常见情况并且NOT失败。
//
// 为什么空是一个真正的答案：一个裸露的 `sk-` 密钥可能是 OpenAI、
// DeepSeek 或自定义端点，而默默地选择一个就是问题 #
// 68 最初发生的原因。每个粘贴的密钥都被写入 Anthropic 存储，
// 因为 提示词 表示 "or any provider"，
// 而代码无法询问。然后，用户调试了他们认为正确的 凭据，
// 针对命名他们未选择的供应商的错误。
//
// 所以合同是：有信心的答案，或者没有。没有得到任何结果的调用者必须 ASK 而不是假设。
// 最长的比赛获胜，因此 `sk-or-v1-` 击败 `sk-or-`，
// 并且未来的 `sk-ant-oat02-` 不能被 `sk-ant-` 遮蔽。
[[nodiscard]] inline std::string_view provider_for_api_key(
        std::string_view key) noexcept {
    std::string_view best;
    std::size_t best_len = 0;
    for (const auto& p : kProviders) {
        for (std::string_view pre : p.key_prefixes) {
            if (pre.empty() || key.size() < pre.size()) continue;
            if (key.substr(0, pre.size()) != pre) continue;
            if (pre.size() > best_len) { best_len = pre.size(); best = p.id; }
        }
    }
    return best;
}

// ── Compile-time invariants ─────────────────────────────────────────────
// 这些是使单一事实来源实际上HOLD的原因。 `openai`
// 行先前声称 Wire::OpenAIResponses，
// 而 传输层 则拨打 /v1/chat/completions；
// 枚举 上没有发送任何内容，因此矛盾在审查中幸存下来，
// 并默默地教导 UI 承诺 传输 永远不会发送的推理文本。现在，
// 方言和路径位于同一行，不一致的是 BUILD ERROR，
// 而不是几个月后有人注意到的错误。
namespace detail {

[[nodiscard]] constexpr bool ends_with(std::string_view s,
                                      std::string_view suf) noexcept {
    return s.size() >= suf.size() && s.substr(s.size() - suf.size()) == suf;
}

[[nodiscard]] constexpr bool endpoints_consistent() noexcept {
    for (const auto& p : kProviders) {
        // 归属名称agentty到HOST，因此它仅意味着我们实际上
        // 通过HTTP拨打的行上的某些内容。检查了下面的 之前 和
        // http_dialled() 保释，因为会出现此错误的行（
        // chatgpt / acp 哨兵）正是保释的行。
        if (p.attribution && !p.http_dialled()) return false;
        if (!p.http_dialled()) {
            // 未通过兼容传输层拨号⇒不得携带半个端点
            // （没有主机的杂散路径是行腐烂的原因）。
            if (!p.path.empty() || !p.models_path.empty()) return false;
            continue;
        }
        if (p.path.empty() || p.models_path.empty()) return false;
        if (p.port == 0) return false;
        // Anthropic/ACP永远不会超过OpenAI-compat 传输层。
        if (p.wire == Wire::AnthropicMessages || p.wire == Wire::Acp)
            return false;
        if (p.native_api) continue;   // Ollama的/api/chat是它自己的形状。
        if (p.wire == Wire::OpenAIChat
            && !ends_with(p.path, "/chat/completions")) return false;
        if (p.wire == Wire::OpenAIResponses
            && !ends_with(p.path, "/responses")) return false;
        // 第二方言得到与第一方言一样的SAME处理：如果
        // 一行通告响应路径，则它实际上必须以/responses结束，
        // 并且它不能重复主路径（在一个URL上声明
        // 两种方言的行是该列存在要消除的漂移）。
        if (!p.responses_path.empty()) {
            if (!ends_with(p.responses_path, "/responses")) return false;
            if (p.responses_path == p.path) return false;
            // 仅对我们实际拨打HTTP的主机有意义。
            if (p.host.empty()) return false;
        }
        // DEFAULT已经是响应的行不得还
        // 携带辅助响应路径——一个问题将有两个答案。
        if (p.wire == Wire::OpenAIResponses && !p.responses_path.empty())
            return false;
    }
    return true;
}

// Id是主键：重复使得preset_for()不明确。
[[nodiscard]] constexpr bool ids_unique() noexcept {
    for (std::size_t i = 0; i < kProviders.size(); ++i)
        for (std::size_t j = i + 1; j < kProviders.size(); ++j)
            if (kProviders[i].id == kProviders[j].id) return false;
    return true;
}

// 没有两个提供商可以声明相同的密钥前缀，并且没有前缀可以是
// 另一个提供商的前缀OF的前缀。
//
// 下半场是最重要的。 provider_for_api_key() 需要最长的匹配，
// 因此 `sk-or-v1-` 在 OpenRouter 内击败 `sk-or-`
// 是好的并且是有意的。但是，如果 FUTURE 行声明 `sk-`，
// 而 Anthropic 声明 `sk-ant-`，
// 则每个 Anthropic 密钥仍将正确解析，并且每个无法识别的 `sk-`
// 密钥将默默地解析为该新提供程序 - 这又是问题 #68，使用不同的供应商名称。
// 可以吞没另一行键的前缀不能是 编译。
//
// 在构建时检查而不是测试，因为故障在 运行时 处是静默
// 的：用户获得针对错误的提供者存储的 凭据 以及命名
// 他们从未选择的某人的身份验证错误。
[[nodiscard]] constexpr bool key_prefixes_unambiguous() noexcept {
    for (std::size_t i = 0; i < kProviders.size(); ++i) {
        for (std::string_view a : kProviders[i].key_prefixes) {
            if (a.empty()) continue;
            for (std::size_t j = 0; j < kProviders.size(); ++j) {
                if (i == j) continue;
                for (std::string_view b : kProviders[j].key_prefixes) {
                    if (b.empty()) continue;
                    // 任一作为另一个的前缀是不明确的ACROSS行；
                    // 平等是这种情况的退化情况。
                    const auto n = a.size() < b.size() ? a.size() : b.size();
                    if (a.substr(0, n) == b.substr(0, n)) return false;
                }
            }
        }
    }
    return true;
}
static_assert(key_prefixes_unambiguous(),
              "two providers claim overlapping API-key prefixes: a pasted key "
              "would resolve to the wrong vendor's store (issue #68)");

// 认证能力必须内部一致。这些字段替换了 login.cpp
// 中的提供程序-name 比较链，因此自相矛盾的
// 行正是迁移所要消除的错误 - 在构建时捕获它，
// 而不是在用户登录 提示词 时捕获它。
[[nodiscard]] constexpr bool auth_caps_consistent() noexcept {
    for (const auto& p : kProviders) {
        // 方法菜单仅在存在 IS 多个方法时才有意义。
        if (p.method_menu && p.auth != AuthStyle::OAuthOrKey) return false;
        // 设备登录是OAuth流程：还通告密钥环境
        // 变量的行将呈现到同一帐户的两个冲突路径。
        if (p.device_login && p.auth != AuthStyle::None) return false;
        // 本地后端根本不进行认证。
        if (p.is_local
            && (p.device_login || p.method_menu || p.token_in_transport))
            return false;
        // 一种运输-held令牌意味着 OAuth 后端，而不是密钥后端
        // —APIkey 是可解析的，因此永远不需要清除缓存的标头。
        // NOT与 平等oauth_native：该标志具体表示
        // "OpenAI-FAMILY OAuth transport"且为假Anthropic，
        // 其 OAuth 拥有自己的AnthropicMessages运输。
        // （较早的草案声称两者是相等的；这个 static_assert
        // 抓住了它，这就是对规则进行编码的点。）
        if (p.token_in_transport && p.auth == AuthStyle::ApiKey) return false;
        if (p.oauth_native && !p.token_in_transport) return false;
        // 在没有OAuth的情况下，active-refresh额外是没有意义的。
        if (p.oauth_proactive_refresh && p.auth == AuthStyle::ApiKey)
            return false;
    }
    return true;
}

} // namespace detail

namespace detail {
// 路由必须与行的其余部分保持一致——这是使 RouteSlot
// -on-the-row 在 分派 时间安全盲目信任的证明：
//   ·路由行必须是LongLived(槽保持cross-turn状态)；
//   ·必须路由LongLived行（否则永远无法到达）；
//   ·必须路由oauth_native OpenAI-family行（其专用
//     传输层 IS其插槽——未路由，它将默默降级为通用per-call路径，
//     确切的custom-host错误类别）；
//   ·没有两行可以共享槽(槽是一个提供者实例)。
[[nodiscard]] constexpr bool routing_consistent() noexcept {
    for (const auto& p : kProviders) {
        if (p.route != RouteSlot::None && p.lifetime != Lifetime::LongLived)
            return false;
        if (p.lifetime == Lifetime::LongLived && p.route == RouteSlot::None)
            return false;
        if (p.oauth_native && p.route == RouteSlot::None) return false;
    }
    for (std::size_t i = 0; i < kProviders.size(); ++i)
        for (std::size_t j = i + 1; j < kProviders.size(); ++j)
            if (kProviders[i].route != RouteSlot::None
                && kProviders[i].route == kProviders[j].route) return false;
    return true;
}
} // namespace detail

static_assert(detail::routing_consistent(),
              "provider row: route slot contradicts lifetime/oauth_native, "
              "or two rows share a long-lived slot");

static_assert(detail::endpoints_consistent(),
              "provider row: Wire disagrees with the path it dials, or an "
              "HTTP-dialled row is missing endpoint data");
static_assert(detail::ids_unique(), "duplicate provider id in kProviders");
static_assert(detail::auth_caps_consistent(),
              "provider row: auth capability flags contradict the row's "
              "AuthStyle (see ProviderDescriptor's auth capability block)");

// 这个传输 DIALECT能否将TEXT的推理返回给我们，对于THIS模型？
//
// 模型推理和让你SEE的模型是不同的东西。
// OpenAI的聊天完成方言不传输GPT-5系列的推理文本：生成令牌并计费，
// 但响应不携带reasoning_content/摘要——这是响应
// API独有的。 Anthropic 消息 数据流 思维块本身，
// 并且许多 OpenAI-COMPAT 服务器（DeepSeek-R1、
// Mistral、vLLM、OpenRouter）确实在聊天
// 方言上填充 reasoning_content，
// 这就是为什么 传输层 解析 无条件地填充。
//
// `model`很重要，因为提供者不一定是ONE方言。
// GitHub Copilot在响应API上通过gpt-5*和mai
// -code-*数据流（其中推理摘要DO返回——测量）来服务相同的账户，
// 而claude-*和gpt-4.x是chat-only（其中它们不返回）。
// 传递空模型询问更粗略的问题"could ANY model on this provider show reasoning"，
// 这是提供者-level行想要的。
[[nodiscard]] bool wire_streams_reasoning_text(
        std::string_view provider_id, std::string_view model = {}) noexcept;

// 提供者行的属性头文件，或者当该行不要求它们
// 时为空。
//
// ONE构建者，因此聊天路径和响应路径不能对agentty
// 是谁存在分歧。两种方言都拨打同一个主机，并且都记入同一个
// 应用程序页面，因此这两个 头文件 的第二个拼写是一个错误，
// 等待其中一个被编辑的那一天。
//
// 头文件名称，来自openrouter.ai/docs/app-attribution：
//   HTTP-Referer REQUIRED。应用程序URL，以及排名的主键。
//                 没有它，应用程序页面就不存在并且其他都不重要。
//   X-Title 显示名称。它本身不执行任何操作 - 它仅标记
//                 HTTP-Referer 已创建的应用程序页面。
//
// 关于名称：文档现在 调用 标题 头文件 `X-OpenRouter-Title`
// 并保留 `X-Title` 作为受支持的别名。我们故意发送
// `X-Title` — 这是绝大多数实际流量使用的内容，它仍然被记录
// 为受支持的，并且已发送的线束已被咬伤，发送 仅 为新名称 (
// QwenLM/qwen-code#12072)。发送两者将是
// Belt-and-braces 选项，但是如果供应商不同意自己的观点，
// 则两个 头文件 声明一个字段最终会得到两个应用程序页面。
//
// 小写名称：头文件名称是每个RFC 9110的大小写-insensitive，
// 并且agentty的其余部分发送小写，因此这些名称匹配它们在
// 传输上的邻居而不是供应商文档中使用的拼写。
//
// x-openrouter-categories将该应用程序归档在市场中的cli-agent下。
[[nodiscard]] inline std::vector<std::pair<std::string, std::string>>
attribution_headers(const ProviderDescriptor* row) {
    std::vector<std::pair<std::string, std::string>> out;
    if (!row || !row->attribution) return out;
    out.reserve(3);
    out.emplace_back("http-referer", std::string{util::identity::url});
    out.emplace_back("x-title",      std::string{util::identity::name});
    out.emplace_back("x-openrouter-categories", std::string{util::identity::category});
    return out;
}

// 默认提供者的id——表的第一行。
[[nodiscard]] inline std::string_view default_provider_id() noexcept {
    return kProviders.front().id;
}

} // namespace agentty::provider
