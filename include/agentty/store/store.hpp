#pragma once
// agentty::store-"somewhere threads and settings live"的抽象。
// 概念为纯域；具体适配器（FsStore、-memory
// 测试存储、假设的云同步）位于该 头文件 之外。

#include <concepts>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "agentty/domain/conversation.hpp"
#include "agentty/domain/catalog.hpp"
#include "agentty/domain/profile.hpp"
#include "agentty/domain/sandbox_config.hpp"
#include "agentty/domain/web_search_config.hpp"
#include "agentty/domain/paste_config.hpp"
#include "agentty/domain/ui_prefs.hpp"
#include "agentty/domain/smart_mode.hpp"

namespace agentty::store {

// 保留RAG配置。 USER-FACING表面是单模（RAG 选择
// 面板循环它）；每个其他字段都是适配器仍然遵循的内部默认值（高级用户可
// 通过 env 进行调整），但 UI 不再公开 - 使 选择面板
// 保持一个决定。 `configured=false` 表示用户从未
// 接触过 选择面板，因此适配器保留其 env-derived 默认值；
// 一旦 选择面板 提交，它就会翻转为真，并且 `mode` 成为
// proactive/pre-turn 行为的权威。
// RagMode是DOMAIN词汇，在domain/rag_mode.hpp中定义
// ——线程携带per-thread覆盖，而这个头文件包括conversation.hpp，
// 所以在这里定义它使得该字段无法在没有循环的情况下命名自己的类型。
// 因此每个现有的 `store::RagMode` 的别名仍然是 编译。
using RagMode = agentty::RagMode;
using agentty::to_string;

[[nodiscard]] constexpr std::string_view describe(RagMode m) noexcept {
    switch (m) {
        case RagMode::On:            return "inject retrieved context before every turn";
        case RagMode::FirstTurnOnly: return "ground the first turn, then stay quiet";
        case RagMode::Off:           return "no pre-turn injection (tools still work)";
    }
    return "";
}

struct RagConfig {
    bool     configured = false;   // 用户是否曾经进行过已保存 RAG设置？
    RagMode  mode       = RagMode::On;

    // ── 内部默认( 不) 选择面板-exposed ; 内 -tunable) ──
    // 知识来源。
    bool  skills        = true;
    bool  memory        = true;
    bool  mcp_resources = false;

    // 管道阶段。
    bool  contextual = true;
    bool  dedup      = true;
    bool  mmr        = true;
    bool  stitch     = true;
    bool  autocut    = true;

    // 上述阶段的数值调整。它们仅存在于 rag::Config 中，
    // 并且只能通过环境变量访问 - 因此它们无法被发现并重置每个
    // shell。它们现在被持久化，因为设置 注册表 将它们公开为行；
    // 请参阅
    // runtime/settings_registry.hpp 了解拥有其范围的表。
    float mmr_lambda          = 0.65f;
    float dedup_threshold     = 0.92f;
    float autocut_sensitivity = 2.0f;

    // 功率模式(延迟/round-trip成本)。
    bool  prf        = false;   // 伪-relevance-feedback扩展
    bool  corrective = false;   // CRAG 分级
    bool  graph      = false;   // GraphRAG扩展
    bool  expand     = false;   // 多-query/RAG-Fusion
    bool  hyde       = false;   // HyDE

    // 融合。
    std::string fusion = "convex";   // "convex" | "rrf"
    bool  adaptive_fusion = true;
    // RRF-mode权重(在凸融合下被忽略)。
    float dense_weight = 1.0f;
    float bm25_weight  = 1.0f;

    // 主动/pre-turn注入调整。在应用时，`proactive` 是 `mode`
    // 的 派生（On/FirstTurnOnly ⇒ true，Off ⇒ false）；
    // 条形图和 字节 上限保持内部默认值。
    bool   proactive          = true;
    double proactive_min_conf = 0.35;
    int    proactive_bytes    = 6144;

    // 基础设施。
    bool  persist = true;   // .agentty/下的.ragdb 缓存
    bool  learn   = false;  // 隐式文件-open反馈循环
    bool  trace   = false;  // 归并 per-stage 跟踪进入模式标签

    // ── 嵌入( 内嵌 ) RAG 选择面板 ——嵌入式) ──
    // WHICH嵌入器检索用途。空的 `embed_backend` 意味着
    // "never configured" — env-derived 默认值，
    // 因此升级用户完全保留今天的行为。
    //
    // API KEY IS DELIBERATELY ABSENT：它存在于由端点键入
    // 的 OS 密钥库中，而不是存在于 settings.json 中。纯文本配置文件中的
    // 凭据 是等待 screen-share 或 bug-report 附件的泄漏。
    std::string   embed_backend;             // "auto" | "ollama" | "openai" | …
    std::string   embed_model;
    std::string   embed_host;
    std::uint16_t embed_port = 0;            // 0=后端默认
    bool          embed_tls  = false;
    std::string   embed_path;
    std::string   embed_model_path;
    std::string   embed_tokenizer_path;
    // 来自最后一次成功探测的MEASURED尺寸。持久化，以便重新启动可以重用热索引；
    // 绝不是 user-typed（参见 embed_backend.hpp）。
    std::uint32_t embed_dim = 0;
};

// 持久的用户设置——模型+简档+收藏夹。  采用 Store 概念
// 是因为它就是 Store reads/writes，
// 而不是因为设置本身就是第一个 -class 域。
struct Settings {
    ModelId              model_id;
    Profile              profile = Profile::Write;

    // 外观。一个 USER 问题，而不是一个项目问题——终端
    // 灯是你眼睛的财产，而不是你碰巧所处的 仓库 的财产—
    // —所以它存在于用户设置中，并在结账之间跟随你。
    ui_prefs::Prefs      ui;
    std::vector<ModelId> favorite_models;
    // 活动LLM后端。空 / "anthropic" = 默认 Claude 路径 (OAuth/Pro/Max)。
    // 任何其他值 ("openai" | "groq" | "openrouter" | "together"
    // | "cerebras" | "ollama" | "host[:port]") 通过
    // OpenAI-compatible 传输层 路由。由`--provider`设置；
    // 在 main.cpp 启动时咨询。
    std::string          provider;
    // 通过 in-app 登录 弹出面板 输入的 Per-provider API 密钥，
    // 由提供商的规范 ID ("openai"，"groq"，…) 键入 — 自
    // 定义主机由其端点规范键入。这里的 已保存 密钥优先于 env-var 链，
    // 因此粘贴一次密钥的用户不必在每个 shell 中重新-export。
    // Anthropic 不是 存储在这里 - 它的信用记录存在于
    // credentials.json 中。
    //
    // 该地图仅是IN-MEMORY的真实来源。静态时，它是密封的（
    // auth::keys→provider-keys.json，机器-bound
    // AES-256-GCM +可选的OS密钥库）并且永远不会序列化到
    // settings.json，这是备份或粘贴的错误报告很乐意携带的纯文本文件。
    std::map<std::string, std::string> provider_keys;
    // 每个提供商选择的最后一个模型，由规范的提供商 ID (
    // "anthropic"、"openai"、"ollama"、
    // …)键入。让提供者切换恢复用户上次在该后端使用的模型，
    // 而不是携带新提供者上不存在的模型 ID。以上全球`model_id`
    // 保持活跃型号；这张地图只是 per-provider 的回忆。
    std::map<std::string, std::string> provider_models;

    // 用户覆盖模型的 CONTEXT WINDOW，以令牌形式，
    // 键入 "<provider_id>\t<model_id>" —
    // MRU 使用相同的复合密钥，因为裸模型 ID 在提供者之间不是唯一
    // 的（两个网关后面的相同名称可以由不同的窗口提供服务）。
    //
    // 为什么这必须存在。 agentty 从三个地方学习窗口：型号 id（
    // Claude/GPT 系列）、Ollama /api/show 探测器
    // 或网关在 /v1/models. 中通告的字段。一个普通的 OpenAI
    // -compatible 代理可能会发布其中的 NONE — OpenAI
    // 自己的 /v1/models 模式定义 只是 {id, object,
    // created, owned_by}，因此网关没有义务说什么——然后窗口会
    // 回落到可能有 5 倍错误的默认值。在LiteLLM后面运行1M
    // -token型号的用户无法纠正它；出于同样的原因，甚至 Claude 代码也
    // 需要显式覆盖（LiteLLM 文档为非 -Anthropic 型号设置一）。
    //
    // 覆盖始终为 WINS。这是可用的最具体的
    // 声明 - 配置网关的人知道它服务什么 -
    // 启发式可以悄悄否决的设置不是设置。
    std::map<std::string, int> context_overrides;
    // 主机被证明是 self-hosted，因此稍后刷新也会探测它们。
    //
    // 运行时上下文窗口只能由ASKING服务器(llama.cpp
    // meta.n_ctx、LM Studio的已加载实例、
    // Ollama/api/ps)获知，并且这些路由被禁止从任何看起来像其他人的
    // API的东西中获取——否则针对
    // api.openai.com的每次刷新都会支付保证的404秒。
    //
    // 单独从地址检测私有地址。这组用于其余的：公共主机名
    // 上的服务器，位于具有自己的 DNS 的 VPN 后面，
    // 或通过隧道。条目是 LEARNED，未配置 -
    // add-host 探针询问每个新主机，无论地址如何，
    // 并且回答 运行时 路由的主机已经证明了它是什么（
    // 没有托管 API 服务 /props）。
    // 为此故意没有 切换：服务器已经告诉我们，因此询问用户将
    // 要求他们重复我们的答案。
    //
    // 精确的主机匹配（无端口、无方案）。 AGENTTY_PROBE_HOSTS
    // 添加到此运行一次而不持续。
    std::set<std::string> probe_hosts;
    // 最近-active (提供者，模型）对的MRU，MOST-RECENT-FIRST并且有界。
    // 驱动融合模型 选择面板 的 RECENT 部分和 ^Tab Quick-swap
    // （跳转到之前的提供商+模型）。磁盘上的每个条目都是
    // "<provider_id>\t<model_id>"。
    // 与 provider_models 不同（这是根据 -provider 召回的 MAP）；
    // 这是一个有序的跨-provider历史记录，用户实际上是切换之间。
    std::vector<std::string> recent_models;
    // 推理努力层，以PERSISTED形式(effort_to_wire_setting)：
    // 传输值如"high"，或两个标记之一——"none"用于显式关闭，
    // "auto"用于居中默认值。空意味着 UNSET（从未配置），
    // 读回为自动，不 为关闭：两者无法区分，但都保留为 ""，
    // 这就是将表盘固定在梯子底部的原因。启动时通过
    // effort_from_wire 重新加载到
    // Model::effort。
    std::string          effort;
    // Per-model推理-effort能力覆盖(问题#20)。由型号 id 键入；
    // true = 对于 目录 无法识别为推理机的兼容型号强制使用力度旋钮 ON，
    // false = 对于 `reasoning_effort` 上
    // 400 秒的型号强制使用 OFF。缺少 id = 使用 目录 推断。
    // 在启动时和每次在-app 切换 (型号选择面板中的^E)时被推入
    // 目录的覆盖注册表。 Claude/GPT 留在家人-gated。
    std::map<std::string, bool> reasoning_effort_overrides;
    // LEARNED能力事实：模型id→其API接受的reasoning_effort
    // ON-levels的精确集合，作为effort_bit位掩码（0=完全
    // 拒绝该参数）。当提供者 4xxes 一个工作量值并在错误 正文 中命名其
    // 合约时编写（stream.cpp 中的拒绝→学习→夹紧→重试 臂）；
    // 在初始化时将其合并到 目录 的 learn-effort 注册表 中，
    // 以便以后的每个会话的 ladder/clamps/wire 都能正确启动。
    std::map<std::string, std::uint8_t> learned_effort_sets;
    // LEARNED:主机("api.groq.com"，"llm.corp:8443")
    // →它用400(provider::OptionalField位)拒绝的可选请求字段。
    // 由 stream.cpp (#93) 中的拒绝→学习→重试 手臂编写，
    // 在初始化时进行水合，因此 传输层 永远不会再次发送主机字段。
    std::map<std::string, std::uint16_t> rejected_fields;
    // LEARNED："host|model"→哪个早期助手转动该模型需要
    // reasoning_content（provider::ReasoningEcho：
    // 1个工具-call转动，每转动2个）。由 stream.cpp (
    // #117) 中的拒绝→学习→重试 臂编写，在初始化时水合。由模型和主机
    // 键入：一个网关同时服务于严格模型和宽松模型。
    std::map<std::string, std::uint8_t> reasoning_echo;
    // Tool将被授予的用户命名为"always allow" (PermissionApproveAlways)。
    // 坚持下来，以便 授权 在重新启动后继续存在 — Zed 的 always_allow 规则。
    // 在初始化时将已加载写入Model::session_grants；
    // 注意 CycleProfile 仍然清除会话的 in-memory 设置（收紧
    // 配置文件 re-arms 提示词），但 授权 在下次启动时重新加载。
    std::vector<std::string> always_allow_tools;
    // DISCOVERED权利事实：该账户的事件订阅拒绝了context-1m-2025-
    // 08-07测试版(HTTP 400
    // "long context beta is not yet available for this subscription")。（当前订阅尚未开放长上下文测试功能。）
    // 我们只能通过尝试来了解这一点——OAuth 令牌没有说明。设置后：目录 停止提供
    // `[1m]` 选择面板 变体，并且 传输 从不发送测试版 头文件。
    //
    // LEGACY- 读-only，迁移源。取代于`entitlements`
    // 下面：这个布尔值是帐户-BLIND，因此必须在每次帐户切换时手动清除
    // 此限制由即将移除的账号学习得到。
    // 这意味着 ping
    // -ponging有权和无权帐户之间的重新-discovered永远一样的400。
    // 装载一次并折叠进入钥匙存储；再也没有写过。
    bool                 context_1m_blocked = false;
    // ACCOUNT-SCOPED权利事实，
    // 键入"<fact>\x1f<provider>\x1f<account>[\x1f<folded-model>]"
    // —参见include/agentty/domain/entitlement.hpp了解分层原理
    // （身份和能力是账户-blind；权利不是）。
    // 仅存储负面事实：不存在⇒不知道被阻止。
    std::map<std::string, bool> entitlements;
    // 用户-configured RAG行为(RAG设置选择面板)。
    // 默认配置为 false ⇒ 适配器保留其 env-derived 配置。
    RagConfig rag;

    // web_search 策略（设置→网页搜索）。每个字段都是 注册表 行，
    // 因此出厂时默认为 ARE "never touched" — 无
    // `configured` 标志； 保存 仅写入偏离默认值的行。
    web_search_cfg::Config web_search;

    // MCP刀具装载(#98)："auto" | "all" | "on_demand"。
    // 参见 tools::plan。 Empty = auto，保存仅在移动时才写入。
    std::string tool_loading;

    // Ctrl+V和`agentty paste`如何读取剪贴板（设置→粘贴）。
    // 注册表行如web_search:出厂默认值ARE
    // "never touched"和保存仅写入移出其默认值的行。
    paste_cfg::Config paste;

    // 沙箱策略(shell命令可能达到的目标)。出于同样的原因组成整体
    // RoleConfig 是：设置上的一组扁平字段使
    // 网络已关闭，但端口允许清单仍非空。
    // 可表示，并强制在每个边界处进行手动 -written 映射 - 这是一个忘记字
    // 段的地方。 `configured` 在经过有意编辑之前一直保持错误状态，
    // 因此升级永远不会更改任何人的 沙箱。
    sandbox_cfg::Config sandbox;

    // 智能模式(角色-based执行路由，docs/design/smart-mode.md)。
    // 默认关闭。
    //
    // DOMAIN类型，完整存储——`profile`与domain::
    // Profile进行相同的移动。这曾经是十一个平面字段（每个槽三个加上三
    // 个整数），这是一个 RoleConfig 拆开：它使
    // 固定了模型，却忘记记录对应提供方。
    // 可表示，并强制在每个边界的两个形状之间进行手动 -written
    // 映射（加载、保存、启动、设置 面板）。这些映射中的每一个都是
    // 一个可以忘记字段的地方，有几个是这样。
    //
    // store->domain是这个代码库已经允许的方向（
    // 规则见domain/rag_mode.hpp）；域永远不会以这种方式回头。
    smart::RoleConfig    smart;

    // 在代理编辑文件之后显示持久化 "N changes"审阅条（作曲家上方
    // 的横幅）。默认为 OFF — 编辑会悄悄应用；打开它（Ctrl+K →
    // "Changes strip"）以获得always-on评论横幅。
    // Ctrl+R / "Review changes" 以任何方式打开评论 面板。
    bool                 show_changes_strip = false;
    // 显示模型的 reasoning/thinking. 开启时：成绩单 渲染
    // 是一个推理块（实时思考 数据流，然后是一个 -line
    // "Thought for N tokens" 总结），
    // 和 Anthropic 请求 可见 思考（交错-thinking 测试版），
    // 因此其增量不会被编辑。默认关闭 — 保持 传输 的精简，并让成绩单专注于答案。
    // 每个提供商均统一（Anthropic / Codex / OpenAI-compat 推理机）。
    // 切换:Ctrl+K→"Reasoning"。
    bool                 show_reasoning = false;
    // 在第一次回复后，向廉价实用新型询问简短的聊天标题，
    // 就像聊天应用程序那样。 Off 保留第一个-line 名称。
    bool                 auto_title = true;
    // 当上下文接近窗口时（转弯后，或在 提示词缓存 到期
    // 之前空闲时），自行压缩上下文。 Off 将其保留为 /
    // compact；请求仍然会被修剪以适应，因此不会溢出。
    bool                 auto_compact = true;
};

template <class S>
concept Store = requires(S& s, const Thread& t, const ThreadId& id,
                         const Settings& settings) {
    // load_threads 返回 线程 *仅元数据*（id、标题、时间戳）。
    // 每个返回的线程上的消息向量是空的——在选择时通过
    // load_thread延迟地获取完整的正文。
    // 这使启动 RAM 与 线程 计数成比例，而不是总转录本 字节。
    { s.load_threads() }     -> std::same_as<std::vector<Thread>>;
    { s.load_thread(id) }    -> std::same_as<std::optional<Thread>>;
    { s.backfill_summaries(std::vector<ThreadId>{}) } -> std::same_as<std::vector<Thread>>;
    { s.save_thread(t) }     -> std::same_as<void>;
    { s.delete_thread(id) }  -> std::same_as<void>;
    { s.load_settings() }    -> std::same_as<Settings>;
    { s.save_settings(settings) } -> std::same_as<void>;
    { s.new_id() }           -> std::convertible_to<ThreadId>;
    { s.title_from(std::string_view{}) } -> std::convertible_to<std::string>;
};

} // namespace agentty::store
