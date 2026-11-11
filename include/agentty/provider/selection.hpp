#pragma once
// agentty::provider--wide选择运行时与哪个后端进行对话。
// 高频路径 (Deps::stream) 已经是 -erased 类型，
// 因此 状态更新函数 / 视图永远不会命名具体的提供程序。两个 调用 站点仍然
// 需要知道活动后端 out-of-band：
//
//   ·cmd::fetch_models()—路由到右侧的list_models()实现。
//   ·main.cpp/ACP/子代理——构造正确的混凝土Provider。
//
// 该头文件集中了该选择。 `select()`在启动时从main()开始被调用一次；
// 只要需要 out-of-band 路由，就会读取 `active()`。

#include <cstdint>
#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "agentty/auth/auth.hpp"
#include "agentty/domain/auth_view.hpp"   // auth::AuthView 认证视图。
#include "agentty/domain/catalog.hpp"   // ModelInfo
#include "agentty/provider/openai/endpoint.hpp"
#include "agentty/provider/registry.hpp"
#include "agentty/store/store.hpp"        // store::Settings

#include <maya/runtime.hpp>
#include "agentty/util/io.hpp"

namespace agentty::provider {

// `Kind`存在于registry.hpp(后端元数据的
// 单一真实来源)；选择重用它。

// 已解析的active-provider配置。对于 OpenAI-family 后端，
// 端点携带基本 URL / 端口 / TLS / 标签；对于 Anthropic，
// 它未使用（人择 传输层 硬编码 api.anthropic.com）。
struct Selection {
    Kind             kind = Kind::Anthropic;
    openai::Endpoint openai_endpoint;   // 仅当kind==OpenAI时才有意义
    // 外部ACP代理的规范ID("claude-agent-acp"、"codex-acp"或config
    // -defined id)。仅当 kind == ExternalAcp 时才有意义； 运行时 将
    // 其交给 stream_external_acp() 到 spawn/drive 正确的子进程。
    std::string      acp_agent_id;

    // 该选择名称的提供商行由parse_selection解析为ONCE。
    //
    // 身份和端点是ORTHOGONAL，并且折叠它们是破坏custom-host流的原因。
    // 过去，通过将 `openai_endpoint.label` 与
    // 文字进行比较，每次查询时的身份都是 re-derived，
    // 但将提供程序指向该标签的自定义基 URL OVERWRITES，
    // 因此身份被破坏，并且 Copilot-backed 自定义主机会默默
    // 降级为通用 OpenAI 路径（空模型列表，无错误）。
    //
    // 携带该行使得身份成为值，而不是派生。指向constexpr
    // kProviders表（静态存储，永不悬挂）；
    // null 仅适用于真正未知的自定义主机，该主机正确地没有
    // 注册表 身份。
    const ProviderPreset* row = nullptr;

    // 该选择的提供者ID("copilot"、"kimi"、…)，
    // 或对于无法识别的自定义主机的""。 该 身份访问器 — 比新 调用 站点
    // 上的任何 is_X() 谓词更喜欢它。
    [[nodiscard]] std::string_view provider_id() const noexcept {
        return row ? row->id : std::string_view{};
    }

    // 乘坐具有其自己的OAuth存储(ChatGPT/Codex、Copilot、
    // Kimi)的专用long-lived 传输层，而不是通用key-authed
    // OpenAI路径。通过设置一行 标志，一个新的此类提供程序就成为现实。
    [[nodiscard]] bool is_oauth_native() const noexcept {
        return row && row->oauth_native;
    }

    // 注册表 id标识的身份——行的不可变主键。 不 显示标签，
    // 自定义-host 流将覆盖该标签。
    [[nodiscard]] bool is_copilot() const noexcept { return provider_id() == "copilot"; }
    [[nodiscard]] bool is_kimi()    const noexcept { return provider_id() == "kimi"; }
    [[nodiscard]] bool is_chatgpt() const noexcept { return provider_id() == "chatgpt"; }

    // 结构平等。 `row` 与 POINTER 进行比较，
    // 这是准确的：它始终指向静态 kProviders 表，
    // 因此同一注册表行的两个选择保留相同的地址。仅当 Model
    // 的选择实际发生更改时，调度接缝才将其发布到 process
    // -global 副本。
    [[nodiscard]] bool operator==(const Selection&) const = default;

    // 该选择归档在 目录 id 下 — 每个最近、fused-catalog
    // 和功能行使用的键：OpenAI-family 端点标签、ACP 代理 id，
    // 否则默认 (Anthropic) id。 一个 定义：状态更新函数
    // 和提供者 选择面板 都询问它，因此他们不能对哪一行是
    // "active" 存在分歧。
    [[nodiscard]] std::string catalog_id() const;
};

// 解析将提供商规范放入选择中。接受：
//   "" → Anthropic（默认） "anthropic"
//   → Anthropic "openai"
//   / "groq" / "openrouter" / "together" / "cerebras" /
//               "ollama" → OpenAI-compatible 以及针对自定义底座的
//   匹配端点预设 "host[:port]" → OpenAI-compatible URL
[[nodiscard]] Selection parse_selection(std::string_view spec);

// CANONICALISE是用户-typed提供者规范，然后才成为IDENTITY。
// 原始字符串被用作(1)传输规范、
// (2)provider_keys/provider_models的设置键、
// (3)选择面板行标识、以及(4)显示标签——因此同一端点的两种拼写(
// "http://h:8080"与"http://h:8080/")否则会分割键、
// 模型和行。这是整个 custom-host PR 系列背后的根本原因（#
// 8/#10/#11/#17/#19 每个修补了一个 下游 站点）：规范化
// 必须在 一次 发生，在每个入口点（CLI --provider、
// TUI custom-host 弹出面板），然后才能保留规范或比较。
// 规则：修剪空白；删除尾随 '/'（但绝不删除方案的 "//"）；
// 预设和 ACP id 不受影响地通过。 "#name" 帐户片段是 PRESERVED —
// 它是设计身份的一部分。
[[nodiscard]] inline std::string canonical_spec(std::string_view spec) {
    std::size_t b = 0, e = spec.size();
    while (b < e && (spec[b] == ' ' || spec[b] == '\t')) ++b;
    while (e > b && (spec[e - 1] == ' ' || spec[e - 1] == '\t'
                     || spec[e - 1] == '\r' || spec[e - 1] == '\n')) --e;
    std::string s{spec.substr(b, e - b)};
    while (s.size() > 1 && s.back() == '/' && s[s.size() - 2] != '/')
        s.pop_back();

    // CASE-FOLD 该 HOST（和方案），绝不是路径或#name。
    // DNS是case-insensitive，所以"YOLO-AUTO.COM"
    // 和"yolo-auto.com"是相同的服务器，并且MUST在这里折叠
    // 成一个身份——否则它们变成两个已保存主机、两个provider_keys
    // 条目和一个端点的两个选择面板行，并且用户将密钥粘贴到一个拼写中，
    // 然后想知道为什么另一个说"no key"。路径保持不变，因为路径 CAN
    // 为 case-sensitive（不保证 `/V1` 在任意网关上等于
    // `/v1`），并且 "#name" 帐户标签是 user-chosen 文本。
    const auto scheme_end = s.find("//");
    std::size_t host_begin = (scheme_end == std::string::npos) ? 0 : scheme_end + 2;
    std::size_t host_end   = s.find('/', host_begin);
    if (host_end == std::string::npos) host_end = s.find('#', host_begin);
    if (host_end == std::string::npos) host_end = s.size();
    for (std::size_t i = 0; i < host_end; ++i)
        if (s[i] >= 'A' && s[i] <= 'Z') s[i] = static_cast<char>(s[i] - 'A' + 'a');
    return s;
}

// ── 坚持主义 -on-success 费用 CLI 自定义主机 ───────────────────────
// 自定义--provider规范是原始INTENT，直到主机回答某些问题为止；
// 将其保留在 解析 时间会让未来的每次裸机发布都因拼写错误而中毒
// settings.json。 main.cpp 在此处注册规范（以及
// -m 召回文件），而不是写入设置。 init()将其带入Model一次；
// 当 NON-EMPTY 目录 到达该提供商时，ModelsLoaded
// 状态更新函数 会持续存在 — 证明 -gated 粘性。
// 已知的预设永远不会注册（它们一如既往地保留在 解析）。
void set_unproven_spec(Io, std::string spec, std::string model_recall);
// 已注册(spec，model_recall)，清除它。由 init() 调用过一次。
[[nodiscard]] std::optional<std::pair<std::string, std::string>> take_unproven_spec_at_launch(Io);

// Session-wide 自定义身份验证 头文件 NAME (--auth-header) 用于
// OpenAI-family 后端，其网关不接受 `Authorization: Bearer` (e.g.
// `X-API-Key`)。存储过程-globally，
// 因此每个parse_selection（从选择面板启动AND实时提供商
// 交换机）将其标记到生成的端点上。空（默认）保留旗手 头文件。
void set_custom_auth_header(Io, std::string name);
[[nodiscard]] std::string custom_auth_header();

// 安装活动选择(process-global)。在启动时由提供商 -picker
// 状态更新函数 调用，用于实时交换机 (UI 线程)。
void select(Io, Selection s);

// 读取有效选择。在 select() 运行之前默认为 Anthropic。
// 返回在选择互斥量下取得的BY-VALUE 快照：数据流 工作线程
// 线程读取此(launch_stream的任务/run_stream_sync)，
// 而UI 线程可能是来自提供者选择面板的`select()`中期。
// 交回引用让 工作线程 观察到嵌入端点字符串的撕裂的
// move-assign（数据 竞态 → 可能崩溃）；
// 快照 使每次读取都成为一致的副本。
[[nodiscard]] Selection active();

// 活动后端的人类显示名称 — "Anthropic"、
// "Groq"、"Ollama"、"OpenAI" 或没有预设行的
// 自定义主机的原始端点标签。由状态栏提供商徽章使用。
[[nodiscard]] std::string provider_display_name(const Selection& s);

// 主持人在第一轮预热前，完全源自一场选秀。
// `host` 空 ⇒ 没有什么可加热的（本地后端/ACP 子进程/端口 0 哨兵）。
// `override_host`/`override_port` 带有 AGENTTY_API_HOST
// 表盘（仅限 Anthropic），因此温暖连接针对真正的 上游。这是（选择、注册表、
// 端点）的 PURE 函数 — 无全局变量，无 I/O — 因此预热路由表为 unit
// -testable； prewarm_active_provider() 就是
// "resolve target, open socket"。
struct PrewarmTarget {
    std::string   host;             // "" ⇒ 跳过预热
    std::uint16_t port = 443;
    std::string   override_host;    // AGENTTY_API_HOST主机(Anthropic)，否则""
    std::uint16_t override_port = 0;

    [[nodiscard]] bool should_warm() const noexcept { return !host.empty(); }
};

// 解析选择的预热目标。 注册表-driven：预设行的`prewarm_host`（
// Anthropic→api.anthropic.com，ChatGPT→chatgpt.com）获胜；
// 否则使用 OpenAI-compat 端点自己的主机，对于本地/no-TLS/端口 0
// 端点则跳过。显式地传递了一个选择，以便测试可以驱动它； prewarm_active_provider
// () 调用 位于 active() 上。
[[nodiscard]] PrewarmTarget prewarm_target(const Selection& sel);

// 在分离的后台线程上打开到ACTIVE提供者的主机的TCP+TLS连接，
// 将其停放在http客户端的池中，以便第一个真实请求跳过冷握手。
// 跨本机后端统一 - Anthropic 和 ChatGPT/Codex（以及
// 任何托管的 OpenAI-family 端点）获得相同的领先优势。
// 本地 (Ollama / llama.cpp) 和 ACP 子进程是 no-ops。
// 每个（主机、端口）幂等；从任何 线程 向 调用 发射 -and-forget 都是安全的。
// prewarm_target(active())上的薄包装器——路由存在于那里。
void prewarm_active_provider(Io);

// 解析提供商规范 注册表-driven 的 AuthHeader。
//   ·Anthropic →源自 `anthropic_creds` (OAuth/x-api-key 来自
//                   `agentty login`)，由调用者传入。
//   · OpenAI-family→承载密钥，按优先顺序：`cli_key` (--key)
//                   > `saved_key`（从 Settings.provider_keys 粘贴到
//                   -app）> 预设的 auth_env 中的第一个非 -empty 环境变量。
//   ·本地(Ollama)→空ApiKeyHeader(本地服务器不需要认证)。
// 由 main.cpp 在启动时由 和 由提供者 -picker
// 状态更新函数 用于实时交换机，因此两者永远不会在后端如何验证方面存在分歧。
[[nodiscard]] auth::AuthHeader resolve_auth_for(
    std::string_view spec,
    const auth::AuthHeader& anthropic_creds,
    std::string_view cli_key = {},
    std::string_view saved_key = {});

// 获取型号目录进行选择——ONE型号-list路由器。
// 替换fetch_models()hand-wrote的
// `is_chatgpt → OpenAI → Anthropic`梯子：它在
// 与数据流路径相同的轴上分派（传输方言+oauth_native 标志），
// 因此选择面板和传输层永远不会对哪个后端选择名称有分歧。 ExternalAcp
// 返回空（ACP 代理选择自己的模型并且不公开 目录）。 `auth` 是活动的
// 凭据（由 Anthropic / OpenAI-compat 目录 端点使用；
// ChatGPT 读取自己的 in-process OAuth 凭据并忽略它）。
// 它通过网络获取（并刷新 OAuth 令牌）：`unlocked`。
[[nodiscard]] std::vector<ModelInfo> list_models_for(Io io, const maya::unlocked& cap,
    const Selection& sel, const auth::AuthHeader& auth);
// 相同，并将`error`设置为实时获取失败的原因（工作时为空）。
// 失败时结果仍然可能是非-empty：托管提供商回
// 退到其捆绑种子。
[[nodiscard]] std::vector<ModelInfo> list_models_for(Io io, const maya::unlocked& cap,
    const Selection& sel, const auth::AuthHeader& auth, std::string& error);

// ── Provider-picker 搜索过滤器( 由 状态更新函数 + 视图) ────────
// 返回 `providers()` 中的索引，其中 id/label/blurb 与
// `query` 匹配（空查询 = 全部，按 注册表 顺序）。 Fuzzy-scored 并排名，
// 因此 "kimi" 或 "ds" 浮在右行上。由 两者 picker.cpp（索引 ↔
// select/move 上的预设分辨率）和 pickers.cpp（第 渲染 行）使用，
// 因此过滤后的列表永远不会在它们之间出现不一致。在 selection.cpp 中定义。
[[nodiscard]] std::vector<int> filter_provider_indices(std::string_view query);

// 排名相同，但是 AUTH-AWARE：在空查询中，
// 仅返回您实际配置的提供商，因此浏览视图是 "your backends"
// 而不是供应商 注册表 的平面转储。键入会像以前一样
// 搜索整个 注册表，因此没有任何内容变得无法访问
// - 未配置的行会在您命名时变成 Sign-in。
//
// `n_configured`报告配置了多少个返回的索引，因此调用者可以
// 对列表进行分段，而无需对每行进行re-deriving认证。
struct FilteredProviders {
    std::vector<int> indices;
    int              n_configured = 0;   // 已授权的领先`indices`
    int              n_hidden     = 0;   // 保留 注册表 行（空查询）
};
[[nodiscard]] FilteredProviders filter_provider_indices(
    std::string_view query, const store::Settings& settings,
    const auth::AuthView& view);

// 用户的已保存自定义OpenAI-compatible主机，已排序。
// 这些 Settings.provider_keys 条目不会解析到内置的 -in 预设行
// - 原始 "host:port"、Ollama/llama.cpp 端点、私有网关。
// 提供者-picker VIEW和其状态更新函数都构建这个列表，
// 并且融合的选择面板的目录刷新消耗它；将其保留在这里可以
// 保证它们 渲染 并按 相同 顺序索引 相同 行（此处的漂移
// 会将选择映射到错误的主机）。
//
// ADOPTION 是测试在 RESOLVED id 而非原始规格上进行的原因。
// parse_selection() 归并 是一个将已知后端命名到该后端行的规范：
// "api.githubcopilot.com" 解析为 provider_id
// "copilot"，这就是使 Copilot endpoint/auth 应用而不是
// 通用 /v1/models 路径的原因（参见 provider_identity_test）。
// 但是 `preset_for(spec)` 询问 不同 问题 -
// "is this string a preset id?" - 并为该主机回答 nullptr，
// 因此它过去常常作为 "custom" 主机返回，并且是预设。然后，每个消费者将其列出
// 两次：提供商 选择面板 显示了两行 "GitHub Copilot"，一旦融合的
// 选择面板 获得了自定义 -host 目录，它就会生成重复的 目录 和重复的模型。
//
// Ask重要的问题：该规范RESOLVE是否为预设行？
// 如果是这样，它已经被预设枚举覆盖，并且不能在此处
// 再次列出。
[[nodiscard]] inline std::vector<std::string> saved_custom_hosts(
    const std::map<std::string, std::string>& provider_keys) {
    std::vector<std::string> hosts;
    for (const auto& [spec, key] : provider_keys) {
        if (preset_for(spec)) continue;                     // 字面上是预设id
        if (preset_for(parse_selection(spec).provider_id()))
            continue;                                       // 采用到预设行上
        hosts.push_back(spec);
    }
    std::sort(hosts.begin(), hosts.end());
    return hosts;
}

} // namespace agentty::provider

// 选择是纯数据加上`row`，其仅指向constexpr
// kProviders表（静态，不可变）。可以安全地跨 线程 进行复制，
// 并且从 const 可以访问的任何内容都无法更改。
MAYA_SENDABLE(agentty::provider::Selection);
MAYA_FROZEN(agentty::provider::Selection);
