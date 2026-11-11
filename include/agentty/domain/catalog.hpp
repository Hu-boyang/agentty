#pragma once
// agentty 目录 —描述用户可以选择的LLM。

#include "agentty/util/digits.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <atomic>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include <maya/runtime.hpp>

#include "agentty/domain/id.hpp"
#include "agentty/domain/capkey.hpp"   // 规范能力-key纪律
#include "agentty/domain/reasoning_off.hpp"

namespace agentty {

// 是否应该向具有该声明的能力的模型发送工具定义？
//
// 自由形式，用于保存可选的而不是整个ModelInfo的
// 调用站点（传输构建器通过id查找能力）。相同的规则，
// 一种实现：仅显式保留 `false`。
[[nodiscard]] inline bool tools_allowed(std::optional<bool> supports) noexcept {
    return !supports.has_value() || *supports;
}

struct ModelInfo {
    ModelId     id;
    std::string display_name;
    std::string provider;
    // 模型的上下文窗口在TOKENS，如提供者所报告的。
    //
    // 0=UNKNOWN，即区别为加载-bearing。过去默认为 200000，
    // 这使得 "the gateway told us 200k" 和
    // "nobody said anything" 具有相同的值 - 因此
    // 服务于 1M 窗口的 LiteLLM/vLLM/OpenRouter
    // 模型被默默地限制为 200k，无法知道默认值已被替换。调用者现在可以显式
    // 解析未知窗口（请参阅 ui::context_max_for_model），
    // 并且可以更喜欢用户覆盖或探测值而不是猜测。
    int  context_window = 0;
    bool favorite       = false;
    // 该模型接受传输上的工具定义吗？
    //
    // TRI-STATE，第三状态为加载-bearing：
    //   true-提供商声明工具支持。
    //   false —提供商声明NO工具支持。工具被扣留；
    //             该模型只能用于普通聊天。
    //   nullopt — UNKNOWN(未探测、未列出、
    //             由服务器重写的段、提供者报告没有能力块)。广告中的工具
    //             ARE。
    //
    // 未知必须意味着SEND，并且这种不对称是故意的。
    // 将工具错误地发送到无法使用它们的模型会在第一轮产生很大的局部错误。
    // 错误的 WITHHOLDING 它们根本不会产生任何错误：转弯成功，
    // 模型根本看不到工具并说它无法读取文件 - 用户报告为
    // "tools are broken with this provider"（此提供方的工具调用无法正常工作。）
    // 并且没有测试注意到，因为没有任何失败。
    //
    // 通过下面的tools_allowed()来读取它，
    // 而不是手动测试可选的；这里的旧评论说
    // "false (or unset)" 保留，这与代码所做的相反，
    // 并且认为该评论会默默地禁用每个未探测模型的工具的调用者。
    std::optional<bool> supports_tools;

    // 该模型是否应该发送工具定义？ 一个 将 tri-state
    // 折叠为一个决定，因此调用者无法向后获取未知案例。
    // 请参阅 supports_tools 了解未知发送的原因。
    [[nodiscard]] bool tools_allowed() const noexcept {
        return agentty::tools_allowed(supports_tools);
    }

    // 该模型接受图像部件吗？相同的 tri-state，
    // 相同的推理，默认 OPPOSITE——不对称就是重点。
    //
    //   nullopt 没人说→SEND。不保持沉默会从
    //            目录 没有描述的每个模型中删除图像，
    //            而默默地从未看到屏幕截图的视觉模型与看到
    //            屏幕截图但没有帮助的模型没有什么区别。
    //            正是失败的supports_tools文件。
    //   真宣愿景→发送。
    //   假DECLARED文字-only→保留。这是我们实际知道的一种情况，
    //            在大多数线路上发送的是硬 400 (
    //            "image input is not supported for this model")，（该模型不支持图像输入。）
    //            或者在 Ollama 上发送的是模型悄悄忽略的图像。
    //
    // 今天，每条路径都未设置——尚未填充任何内容，
    // 因此行为未改变。它存在，所以在两个 调用 站点发展自己之前，
    // 决定已经有 一个 家，这就是 切换 最终被四个中的
    // 三个 传输层 所尊重的原因。
    std::optional<bool> supports_vision;

    [[nodiscard]] bool vision_allowed() const noexcept {
        // 镜像tools_allowed()：仅显式`false`保留。
        return supports_vision.value_or(true);
    }

    // 值相等（用于检测活动提供者的融合目录何时已经偏离
    // available_models并且需要re-seed）。
    [[nodiscard]] bool operator==(const ModelInfo&) const = default;
};

// (提供商，模型）对——cross-provider交换机目标的身份和MRU/recents单元。
// 小，值-typed，并且具有可比性，因此它可以键入融合的
// 选择面板的选择并对最近列表进行重复数据删除。
struct ModelRef {
    std::string provider_id;   // "anthropic"、"openai"、"kimi"、主机规格……
    std::string model_id;      // ModelId::value 上 传输

    [[nodiscard]] bool operator==(const ModelRef& o) const noexcept {
        return provider_id == o.provider_id && model_id == o.model_id;
    }
    [[nodiscard]] bool empty() const noexcept {
        return provider_id.empty() && model_id.empty();
    }
};

// 加星号的模型是（提供商，模型）对：Groq和OpenRouter
// 后面的相同id是两个模型。存储"provider\tmodel"
// （最近的编码）。一个由较旧版本编写的裸 ID 到处都是。
[[nodiscard]] inline std::string favorite_key(std::string_view provider_id,
                                              std::string_view model_id) {
    std::string k{provider_id};
    k += '\t';
    k += model_id;
    return k;
}
[[nodiscard]] inline bool is_favorite(const std::vector<ModelId>& favs,
                                      std::string_view provider_id,
                                      std::string_view model_id) {
    const std::string scoped = favorite_key(provider_id, model_id);
    for (const auto& f : favs)
        if (f.value == scoped || f.value == model_id) return true;
    return false;
}

// 一个经过验证的提供商的模型目录，保存在融合的选择面板的合并视图中。
// `state` 驱动 per-provider 组 头文件（正在加载
// …/就绪/失败）； `models` 是 list_models_for
// () 结果（由于捆绑的种子，即使没有身份验证，它也是非 -empty，
// 因此新添加的提供程序仍会立即显示行）。
struct ProviderCatalog {
    std::string provider_id;
    std::string label;                    // 提供商显示名称
    enum class State : std::uint8_t { Idle, Loading, Ready, Failed };
    State state = State::Idle;
    std::vector<ModelInfo> models;
    // 为什么最后一次实时获取失败（"HTTP 401: Invalid API key")，
    // 如选择面板中所示。工作的时候是空的。
    std::string error;
    std::string account_label;            // 该提供商的活跃帐户
    // Steady-clock 毫秒，当该 目录 的 LIVE 获取
    // 最后完成时（0=从未实时获取；仍然显示捆绑种子）。
    // 驱动 on-open 新鲜度检查：重新获取早于 TTL（或失败）
    // 的 目录，以便融合列表保持最新状态，而不是在第一个 加载 之后冻结。
    std::int64_t loaded_at_ms = 0;
    // 导出每-model模糊干草堆，已经小写，`models`中的每个条目一个（
    // 相同顺序）。当 `models` 更改时重建一次（rebuild_fused_rows
    // 中的尺寸保护），因此 per-KEYSTROKE 过滤器永远不会为每
    // 个模型 re-allocates 或 re-lowercases 干草堆。
    // 空⇒尚未构建（构建回落到内联组成干草堆）。
    std::vector<std::string> search_keys;
    // 根据-model FOLDED ROW IDENTITY (capkey::norm_row_id)导出，
    // 与`models`的顺序相同。融合构建的别名 -dedup 用于每个（候选 ×
    // 已见）对上的 调用 norm_row_id — O(n²)，每个探测有两个字符
    // 串 分配，在 450-model 聚合器列表上每次击键约 200k 分配。
    // 每次 目录 改变折叠每个 id 一次 将重复数据删除变成 hash-set 探针。
    // 与 search_keys（相同尺寸的防护罩）一起重建。
    std::vector<std::string> row_keys;
    // 导出的规范显示标签（ui::model_display_label)，
    // 与`models`-label_fn标准化和分配相同的顺序，并且浏览模式（
    // 空查询）按需加载是每次重建时EVERY模型的标签。
    // 使用与 search_keys 相同的 size-guard 失效进行缓存。
    std::vector<std::string> display_labels;
    // 派生 per-model "has an effort ladder" 标志 （与
    // `models` 的顺序相同），记忆 effort_capable(resolved_caps(id,
    // 提供商)) — 每个 调用 在 shared_mutex 后面进行
    // 3 个 注册表 地图查找，对于在 300-model 上构建的 per
    // -keystroke 行来说太热了目录。每当 `models` 发生变化时，
    // 与 search_keys 一起重建；当功能注册表通过下面的 caps_epoch
    // 标记更改（models.dev 刷新/学习拒绝/^E 覆盖）时，
    // 也会失效。空⇒未构建（行构建回落到实时解析）。
    std::vector<bool> reason_flags;
    // reason_flags构建时caps_epoch()的值；
    // 不匹配意味着 注册表 的能力在我们下面发生了改变 → 重建 标志。
    std::uint64_t reason_epoch = 0;

    // 丢弃每个派生的缓存。 调用 这个在 每个 站点取代了
    // `models` — 有三个，他们每个人都曾经把 -clear
    // 和他们碰巧记得的 缓存 交到一起，这就是后来添加的 缓存 在同
    // 一个 -SIZE 模型交换后默默地保持服务 过期 行的方式（
    // rebuild_fused_rows 中的大小防护仅捕获长度变化）。
    // 一种方法，因此添加缓存立即更新每个失效站点。
    void invalidate_derived() noexcept {
        search_keys.clear();
        row_keys.clear();
        display_labels.clear();
        reason_flags.clear();
        reason_epoch = 0;
    }
};

// 用户NOT登录的提供商——呈现为融合列表底部的单个"sign in to <label>"要约。
// 存储在 Model 上，因此 per-keystroke 重建永远
// 不会从磁盘重新进行 -derives 身份验证。
struct SigninOffer {
    std::string provider_id;
    std::string label;
};

// FUSED cross-provider 选择面板中的单个渲染
// 行：用户可以自动切换到的具体（提供者、模型），
// OR——当`model.id`为空并且`authed`为假时——路由
// 到登录流程的"sign in to <provider>"提议。
// 纯值类型：状态更新函数 构建这些向量及其视图 渲染；选择由 (provider_id,
// model.id) 标识键控，因此异步 目录 合并永远不会
// 移动光标的逻辑目标。
struct FusedRow {
    std::string provider_id;
    std::string label;                    // 提供商显示名称
    // 规范的，提供者-uniform MODEL标记视图渲染并突出显示
    // -SAME字符串match_positions索引。
    // 通过 FusedInputs::label_fn（ui::model_display_
    // 标签）在 build_fused_rows 中计算，因此融合后的 选择面板 与 per
    // -provider 的读数相同。 Sign-in 报价行为空（无型号）。当没有注入
    // label_fn 时，回落到原始 display_name/id（直接注入 调用
    // build_fused_rows 的 单元测试）。
    std::string model_label;
    ModelInfo   model;                    // 空 ID ⇒ "sign in" 优惠
    bool        authed = true;
    bool        active = false;           // ==当前提供商+型号
    bool        recent = false;           // 属于RECENT部分
    bool        favorite_section = false; // 属于FAVORITES部分
    bool        reasons = false;          // 模型可以推理（预先计算在
                                          // 构建使得视图永远不会解码能力层，
    // 出于同样的原因与 `reasons` 一起预先计算：
    // ModelCapabilities::tier_for标记id并运行多次子字符串扫描，
    // 并且视图触及每个可见行EVERY FRAME。 0..3 映射到 Tier::Weak..
    // 旗舰。
    std::uint8_t tier = 0;
                                          // 仅当提供者明确报告
    // 该模型不能调用工具（Ollama的/api/show探针）时，
    // 每帧每行的上限为FALSE。这样的模型根本无法驱动
    // 代理——它只能聊天——所以 选择面板 必须说 之前 是选择，
    // 而不是让用户想知道为什么没有发生任何事情。
    // 未知（常见情况：托管提供商不会宣传这一点）仍然是正确的。
    //
    // 这里特意设置了NOT：账号。模型由（提供商，
    // 模型）标识 - 帐户是提供商当前正在使用的 凭据，
    // 与模型选择正交并且可以在固定选择下切换。将其放在模型行
    // 上意味着选择一个模型会选择一个帐户（它不会：
    // switch_to_model_ref 解析提供者的
    // 活动 凭据），并且会在其每一行上重复一个提供者-level 事实。
    // 帐户位于 provider/account 选择面板
    // 和状态栏上，可以在其中进行操作。
    bool tool_capable = true;
    // Fuzzy-match 字节 偏移到当前查询的模型 NAME (
    // display_name，其他 id) 中 — 视图突出显示的字符
    // (fzf-style)，因此大的过滤列表显示每行匹配的 原因。
    // 当没有查询/仅匹配提供者名称时为空。在构建中每次击键计算一次。
    std::vector<int> match_positions;

    [[nodiscard]] bool is_signin_offer() const noexcept {
        return !authed && model.id.value.empty();
    }
    [[nodiscard]] ModelRef ref() const {
        return ModelRef{provider_id, model.id.value};
    }
};

// Canonical 传输 id：剥离 agentty 的内部扩展-context
// 标记（`[1m]` / `[2m]`），因此原始提供商 id 位于 传输 上。
// 后缀是PICKER-ONLY标记（它选择1M/2M窗口+上下文beta）；
// 上游 API 从来没有听说过它，`claude-sonnet-5[1m]` 上有 404。
// 这反映了 Claude 代码的 `Yu()`（它剥离了 /\[(1|2)m\]/gi）。
// 每个 传输层 必须 在将 req.model 放入请求 正文 之前都会通过此路由。
[[nodiscard]] inline std::string wire_model_id(std::string_view id) {
    std::string out;
    out.reserve(id.size());
    for (std::size_t i = 0; i < id.size();) {
        if (id[i] == '[' && i + 3 < id.size() && id[i + 2] == 'm'
            && id[i + 3] == ']'
            && (id[i + 1] == '1' || id[i + 1] == '2')) {
            i += 4;   // 跳过 "[1m]" / "[2m]"
            continue;
        }
        out.push_back(id[i]);
        ++i;
    }
    return out;
}

// ============================================================================
// ModelCapabilities —带类型的 关于从其 ID 导出的模型的知识。
// ============================================================================
//
// 传输-level决策（发送哪个测试版头文件、绘制哪种颜色、
// 上下文-window上限是什么）都取决于用户选择的模型。
// 提供程序不公开功能探测 — `/v1/models` 返回 id 并显示元数据，
// 而不是 "this model accepts the fine-grained-streaming beta"
// — 因此我们从模型 id 字符串推断功能。集中于此，
// 因此每个要求 "is this Sonnet 4?" 读取
// 相同解码值的站点，并添加对新一代 ("claude-haiku-5-…")
// 的支持仅涉及 `decode()`，而不是跨 运行时
// 的每个 if-substring 检查。
//
// 解码策略：对'-'进行分词而不是子串匹配。
// Anthropic id跟随`claude-{family}-{generation}-{revision}[-{date}]`，
// 因此位置分词器随着目录的增长而保持稳健。当 `-5-` 型号发货时，
// 旧的 `model.find("opus-4")` /
// `model.find("haiku-4")` 方案就默默地停止识别这一代；
// 使用标记，我们读取 `family` 之后的整数，并且 >=
// 4 检查无需源编辑即可继续工作。
//
// 限制：这仍然是推论，不是来自上游的合约。
// 如果 Anthropic 重组 id 模式定义（
// 删除 `claude-` 前缀，在家族和世代之间插入标签等），
// 则解码器需要相应的更新 - 但在单个、结构明确的站
// 点而不是分散的子字符串检查。
struct ModelCapabilities {
    // 《神鬼寓言》/《神话》是2026年的旗舰泳道（《神鬼寓言5》=通用-access，
    // 《神话5》=受限；相同底层模型）。它们位于层次结构中的 上方 Opus 中，
    // 并共享 Opus-class 规格（1M ctx、128k 输出、工作量）。
    enum class Family : std::uint8_t { Unknown, Haiku, Sonnet, Opus, Fable, Mythos, Gpt };

    Family family = Family::Unknown;
    // 代提取为整数。 0 = 未知/4 之前。
    // 当 下游 关心特定代时，使用数值；下面的方便 标志
    // 涵盖了常见的 "are we on Claude 4+ wire?"
    // 情况。
    int  generation = 0;
    // Pre-decoded "Claude 4-or-later" —传输用来
    // 决定是否发送上下文-management beta 头文件的阈值。
    bool generation_4_or_later = false;
    // Minor/revision令牌：紧接在生成之后的整数(e.g.
    // `opus-4-8`→第4代，修订版8)。 0 = 未知。
    // 让 传输 区分 4.5 和 4.8，这是下面的 -capability 门所需要的。
    int  revision = 0;
    // agentty-internal：用户选择加入 1M-context-window测试版。
    // 标签是`[1m]`在选择时附加到模型 ID 上；
    // 上游id没有这样的后缀。
    bool extended_context_1m   = false;

    // 启发式：该模型在结构化工具-calling处是UNRELIABLE并且倾向于超过-call/将工具JSON泄漏到散文中
    // （弱本地模型）。
    // 驱动slim Decision-first系统提示词、
    // doom-loop防护装置以及工具-suppressed 重试。强托管模型（
    // 任何已知的 Claude 系列）和工具-trained 本地系列不是 弱。
    // 推理完全存在于from_id中（不存在网络探测）。
    bool weak_tool_use = false;

    // 从id的大小标签解析的BILLIONS中的参数计数(qwen3
    // -coder:480b -> 480，qwen3:8b -> 8)，
    // 当id不携带任何内容时为0(每个托管Claude/GPT id，
    // 以及命名像`glm-4.6`这样的版本的本地id)。由from_id设置；
    // 这是定制主机可靠地向我们提供的唯一功能信号，也是让 tier() 对未知系列
    // 进行评级而不是将所有内容都称为中级的原因。
    int params_b = 0;

    // 这是通过top-level `reasoning_effort` 枚举 (low
    // |medium|high)暴露可配置推理的OpenAI-Chat-wire模型——
    // 它是Claude/GPT `Family`梯子中的NOT，但仍然支持努力控制。
    // 涵盖 Mistral (小 4/中 3.5 — 可调整推理)、
    // DeepSeek-Reasoner/R1、xAI Grok 推理和 Gemini `*-thinking`。
    // 解码于from_id；由supports_effort()咨询。 （米斯特拉
    // 尔的裁判官本身就有REJECTS reasoning_effort，
    // 所以它包括NOT。）将ORTHOGONAL保留到`family`，
    // 所以tier/context/output天花板（关闭家庭+一代）完全不受
    // 影响——只有努力门读到这个标志。这些型号只带low/medium/high
    // （不带`max`/`xhigh`，都是Claude/GPT的扩展），
    // 所以下面的梯门特意做了NOT为reasoning_compat打开。
    bool reasoning_compat = false;

    // 模型的努力枚举是BINARY：它仅接受low/medium（以及每个Claude/GPT扩展）
    // 上的{无，高}和400。这是 Mistral 的平台-wide 合约 - 探测
    // api.mistral.ai 显示那里的每个推理模型（mistral-medium 3.5+、
    // mistral-small 4、magistral）拒绝 low|medium|minimal
    // |xhigh|max 和 "supported values: [high, none]"。
    // 从 id 的 "stral" 房屋命名（mistral/magistral/devstral/
    // ministral/leanstral …）解码，因此 选择面板 梯子折叠到关闭→高，
    // 并且 clamp/wire 将任何 过期 low/medium 选秀权提升到高而不是 400ing。
    // 与reasoning_compat正交：这仅形状WHICH级别存在，
    // reasoning_compat(+catalog/overrides)决定IF努力存在。
    bool effort_high_only = false;

    // ── 精准努力 -value SET(models.dev / learn -caps 形状) ─────
    // 当effort_set_known、`effort_set`是该模型的API
    // 接受的ON级别的EXACT集合时，每个努力一位（参见effort_bit)。
    // 0 + 已知 = 模型完全拒绝 reasoning_effort。
    // 这是所有其他努力领域近似的地面 -truth 表示：家庭门、
    // reasoning_compat 和 effort_high_only 都只是在
    // 不知道确切集合时使用的 DERIVATIONS。由 resolved_caps
    // () 从（按优先顺序）学习的 -from-rejection 注册表（提供者
    // TOLD 我们通过 400 它接受的内容）和 models.dev 快照 填充。
    // 从未由 constexpr from_id 设置 — 静态推断仅猜测。
    std::uint8_t effort_set       = 0;
    bool         effort_set_known = false;
    // 该型号的"off"是如何表示的(catalog_reasoning_off_for，
    // 然后是resolved_caps中的家族回退)。不是由 from_id 设置的。
    std::uint8_t reasoning_off    = 0;   // ReasoningOff，原始存储：下面声明
    // Anthropic thinking.type值是模型接受的，
    // 如/v1/models声明它们(capabilities.thinking.types)。 0 = 未声明，
    // 因此由下面的帮助程序中的 id 规则决定。由 resolved_caps 设置。
    static constexpr std::uint8_t kThinkDeclared     = 1u << 0;
    static constexpr std::uint8_t kThinkEnabled      = 1u << 1;
    static constexpr std::uint8_t kThinkAdaptive     = 1u << 2;
    static constexpr std::uint8_t kThinkDisabled     = 1u << 3;
    static constexpr std::uint8_t kThinkBetweenTools = 1u << 4;
    std::uint8_t thinking_types   = 0;
    [[nodiscard]] constexpr bool thinking_declared() const noexcept {
        return (thinking_types & kThinkDeclared) != 0;
    }
    [[nodiscard]] constexpr bool accepts_thinking(std::uint8_t t) const noexcept {
        return (thinking_types & t) != 0;
    }

    [[nodiscard]] constexpr bool is_haiku()  const noexcept { return family == Family::Haiku; }
    [[nodiscard]] constexpr bool is_sonnet() const noexcept { return family == Family::Sonnet; }
    [[nodiscard]] constexpr bool is_opus()   const noexcept { return family == Family::Opus; }
    [[nodiscard]] constexpr bool is_fable()  const noexcept { return family == Family::Fable; }
    [[nodiscard]] constexpr bool is_mythos() const noexcept { return family == Family::Mythos; }
    // OpenAI gpt-5.x (ChatGPT/Codex 响应行：gpt-5.6-sol，gpt-5.4，…)。
    [[nodiscard]] constexpr bool is_gpt()    const noexcept { return family == Family::Gpt; }
    // Fable/Mythos共享Opus-class的能力；
    // 将它们分组到下面的大门，这样一个检查就覆盖了整个旗舰通道。
    [[nodiscard]] constexpr bool is_flagship() const noexcept {
        return family == Family::Opus || family == Family::Fable
            || family == Family::Mythos;
    }
    [[nodiscard]] constexpr bool is_known_family() const noexcept {
        return family != Family::Unknown;
    }
    // 当该模型需要weak-model防护装置(细长提示词、doom-loop帽、
    // 工具-suppressed 重试)时确实如此。参见 infer_weak_tool_use。
    [[nodiscard]] constexpr bool is_weak_tool_user() const noexcept {
        return weak_tool_use;
    }

    // ── 努力(output_config.effort)能力门 ───────────────────
    // Opus 4.5+和Sonnet 4.6+上的努力是GA；它在 Sonnet 4.5、
    // 俳句和任何 4 之前的型号上播放 400 秒。 `max` 登陆 Opus 4.6+
    // / Sonnet 4.6； `xhigh` 随 Opus 4.7 一起提供（仅限
    // Opus）。盖茨读取解码后的系列+生成+修订，因此新的ID仅更新from_id()。
    [[nodiscard]] constexpr bool supports_effort() const noexcept {
        // 旗舰车道(Fable/Mythos 5+)带有努力
        // 控制GA(中是最佳点，最大是上限)。
        if (family == Family::Fable || family == Family::Mythos)
            return generation >= 5;
        if (family == Family::Opus)
            return generation > 4 || (generation == 4 && revision >= 5);
        if (family == Family::Sonnet)
            return generation > 4 || (generation == 4 && revision >= 6);
        // OpenAI gpt-5.x (ChatGPT/Codex):整条线是
        // 由推理力度驱动，例如 reasoning.effort 从 low 开始，
        // ultra；medium是账户默认)。响应后端期望每次都付出努力，因此公开它。
        if (family == Family::Gpt)
            return generation >= 5;
        // OpenAI-Chat-wire推理模型(Mistral Small/Medium、
        // DeepSeek-Reasoner、Grok推理、Gemini思维):top-level
        // `reasoning_ effort` low|medium|high。不在家庭阶梯中，
        // 纯粹由解码的 标志（或用户覆盖）控制 - 请参阅上面的 reasoning_compat。
        if (reasoning_compat)
            return true;
        return false;
    }
    [[nodiscard]] constexpr bool supports_effort_max() const noexcept {
        if (!supports_effort()) return false;
        // 兼容推理模型仅公开3-level 枚举 (low|medium|high)；
        // `max`/`xhigh` 是 Claude/GPT-lane 扩展。此处设置上限，
        // 以便 过期 最大选择在 effort_wire_for 中降级为 `high` 而不是被发送。
        if (reasoning_compat) return false;
        if (family == Family::Fable || family == Family::Mythos)
            return true;  // 旗舰车道占据各个级别，包括。最大
        if (family == Family::Opus)
            return generation > 4 || (generation == 4 && revision >= 6);
        // GPT-5.6旗舰线(sol/terra/luna)接受`max` ;早期的 gpt-5.
        // x (5.5/5.4) 在 xhigh 达到顶峰。
        if (family == Family::Gpt)
            return generation > 5 || (generation == 5 && revision >= 6);
        return true;  // 任何努力-capable十四行诗（4.6+）也需要`max`
    }
    [[nodiscard]] constexpr bool supports_effort_xhigh() const noexcept {
        if (!supports_effort()) return false;
        if (reasoning_compat) return false;  // 仅 3-level 枚举 — 见上文
        if (family == Family::Fable || family == Family::Mythos)
            return true;  // 旗舰巷暴露全梯
        // 每个当前的gpt-5.x模型都支持xhigh。
        if (family == Family::Gpt) return true;
        return family == Family::Opus
            && (generation > 4 || (generation == 4 && revision >= 7));
    }

    // Anthropic思维-mode选择(界面在4.6边界发生变化)。 TRUE => 发送
    // 思考：{type:"adaptive"} + output_config.effort; FALSE
    // => 发送遗留思维：{type:"enabled", budget_tokens:N}。
    //
    //   • Opus/Sonnet 4.5 及更早版本：仅 "enabled" — "adaptive" 400s
    //     ("adaptive thinking is not supported on this model")。（该模型不支持自适应思考。）
    //   ·Opus 4.6:均被接受；我们更喜欢自适应（前向路径）。
    //   • Opus 4.7 / 4.8、Sonnet 4.6+ 和旗舰 5+ 通道：仅
    //     "adaptive" ("enabled"/budget_tokens 400s)。
    // 未知的 ids 默认为自适应（每个当前旗舰都是 4.6+；过期-guess 400
    // self-heals — 但请参阅 transport.cpp 了解确切的 传输）。
    [[nodiscard]] constexpr bool uses_adaptive_thinking() const noexcept {
        if (thinking_declared())
            return accepts_thinking(kThinkAdaptive) || !accepts_thinking(kThinkEnabled);
        if (family == Family::Fable || family == Family::Mythos)
            return generation >= 5;
        if (family == Family::Opus || family == Family::Sonnet)
            return generation > 4 || (generation == 4 && revision >= 6);
        // Non-Claude系列根本不使用该接口；呼叫者仅在
        // Anthropic 传输 上查询此信息。
        return generation >= 5;
    }

    // 怎么说"off"。当 /v1/models 声明模型的思维类型时，我们遵循它们；
    // 否则，下面的 id 规则与 Anthropic 的 per-model 表匹配
    // （thinking-troubleshooting 文档，实时检查）。
    //
    // Claude 5+模型在请求将`thinking`排除在外时进行思考，
    // 因此必须说"off"：思考：{type:"disabled"}。 4.
    // x 将缺失字段视为关闭。无论如何都会拒绝 "disabled" 的
    // 模型将由 传输层 重试并了解事实。
    [[nodiscard]] constexpr bool needs_explicit_thinking_off() const noexcept {
        if (thinking_declared()) return accepts_thinking(kThinkDisabled);
        if (thinking_always_on() || supports_between_tools_thinking())
            return false;
        if (family == Family::Opus || family == Family::Sonnet
            || family == Family::Haiku)
            return generation >= 5;
        return false;
    }

    // 默认情况下在 "disabled" 上思考 400 的模型（并且
    // 没有 between_tools）。最接近关闭的是最低的努力。
    [[nodiscard]] constexpr bool thinking_always_on() const noexcept {
        if (thinking_declared())
            return accepts_thinking(kThinkAdaptive | kThinkEnabled)
                && !accepts_thinking(kThinkDisabled | kThinkBetweenTools);
        if (family == Family::Fable || family == Family::Mythos)
            return true;   // 每Fable/Mythos，包括神话预览
        if (family == Family::Opus)
            return generation > 5 || (generation == 5 && revision > 0);
        return false;
    }

    // Sonnet 5.5也不能禁用思考，但有最低设置，思考：{type:
    // "between_tools"}：没有up-front思考，
    // 仍然在工具调用之间思考。仅在不接受 "disabled" 时使用。
    [[nodiscard]] constexpr bool supports_between_tools_thinking() const noexcept {
        if (thinking_declared())
            return accepts_thinking(kThinkBetweenTools)
                && !accepts_thinking(kThinkDisabled);
        return family == Family::Sonnet && generation == 5 && revision == 5;
    }

    // 这些将每个思维块绑定到产生它的确切前缀，因此在系统提示词或工具改变400秒后
    // 重放一个。发送 block_binding:{prefix_mismatch_behavior:
    // "drop_block"} （测试版思维
    // -binding-controls）会丢弃 过期 块。与 Zed 相同的列表。
    [[nodiscard]] constexpr bool binds_thinking_to_prefix() const noexcept {
        if (generation != 5) return false;
        if (family == Family::Opus || family == Family::Sonnet
            || family == Family::Haiku)
            return revision == 5;
        if (family == Family::Fable) return revision == 1;
        return false;
    }

    // ── 内容 -window 探测(1M) -window (问题) ────────────────
    // 对照Claude代码二进制程序的型号目录进行验证(每个条目携带
    // `context: {window:200000, supports_1m_beta, supports_1m_suffix}`)。
    // Claude代码从帐户层执行NOT auto-detect 1M：基本窗口是
    // ALWAYS 200k，并且1M是用户选择的显式选择面板 VARIANT——以
    // `[1m]`结尾的id（其`Yu()`条带/\[(1|2)m\]/gi，因此未来`[2m]`
    // 也是预期的）。仅当模型的 目录 条目具有 `supports_1m_suffix:true`
    // 和 并且帐户持有 `context_1m_entitlement` 时，才会提供 变体。
    // agentty 准确地反映了这一点：`[1m]` 后缀设置了
    // extended_context_1m（下面的 from_id 中），它既发送 context
    // -1m beta 又将 context_window() 扩大到 1M。
    //
    // supports_1m_suffix()：该型号可以提供`[1m]` 变体吗？
    // 根据 Claude 代码的 目录，Sonnet-4 系列、Opus-4 系列和 Haiku
    // 4.5 都带有 supports_1m_suffix:true；较旧的（第 3 代）型号没有。
    // 旗舰next-gen车道（Fable/Mythos 5+）也被视为1M-capable。
    [[nodiscard]] constexpr bool supports_1m_suffix() const noexcept {
        if (family == Family::Sonnet) return generation >= 4;
        if (family == Family::Opus)   return generation >= 4;
        if (family == Family::Haiku)  return generation >= 4;
        if (family == Family::Fable || family == Family::Mythos)
            return generation >= 5;
        return false;
    }

    // 模型的上下文窗口以TOKENS表示——ctx-%和auto
    // -compaction分母。
    //
    // 该梯级代表 Claude 家族的 仅，
    // 其中 200k 是每一代的公开基数，`[1m]` 是记录的拓宽。
    // 那是知识，而不是猜测。
    //
    // 它曾经对ANY已知家族应答200k，在Family::
    // Gpt中扫过——而GPT窗口不是200k。 gpt-5.4是400k，
    // gpt-4.1是1M，两者都无法从id推断出来。因此，
    // 目录 没有 已加载 的 Copilot 会话在 400k 模型上
    // 却得到了 200k 分母：仪表读数是实际使用情况的两倍，而 auto
    // -compaction 的发射量大约是应有的 线程 的一半，
    // 默默地丢弃了模型仍然可以看到的上下文。
    //
    // 对于我们不知道其窗户的家庭来说，返回0是诚实的答案。
    // 0 表示 UNKNOWN，解析器的后续梯级（
    // models.dev、环境逃逸舱口、
    // 保守的默认值）正是为此构建的。一个自信的错误数字胜过所有的人；
    // 诚实的零让梯子发挥作用。
    //
    // 注意无论哪种方式都使其安全的不对称性：over-estimate无法打开传输，
    // under
    // -estimate仅比其必须的更早压缩。
    [[nodiscard]] constexpr int context_window() const noexcept {
        if (extended_context_1m) return 1'000'000;   // 显式[1m]变体
        switch (family) {
            case Family::Haiku:
            case Family::Sonnet:
            case Family::Opus:
            case Family::Fable:
            case Family::Mythos:
                return 200'000;      // 发表Claude底座，每一代
            case Family::Gpt:
            case Family::Unknown:
                break;               // 不能从 id 推断出——这么说
        }
        return 0;   // 未知：呼叫者退回到目录或探针
    }

    // ── 能力等级:提供者 -RELATIVE 兵力 序号 ────────────
    //
    // 没有提供商运送 "power" 号码； /v1/models 返回
    // ids，而不是强度指标。因此，我们从已从 id 解码的字段中得出一个
    // 粗略层 - 供应商的 自身 命名是排序（Haiku < Sonnet <
    // Opus <= Fable/Mythos；gpt-5-mini < gpt
    // -5 < gpt-5.6；按参数计数的本地模型）。对于比较某个提供商的
    // 型号 WITHIN 有意义（型号路由器选择活跃提供商提供的最便宜的有能力
    // 的型号）； 不 绝对交叉-vendor 分数。
    //
    //   0 工具弱/小/不可靠 — 永远不要在这里进行工作 1 小
    //   但仍具备能力的低成本路径，例如 Haiku、
    //   *-mini/nano、7–13B 2 中/主力 — Sonnet、普通 gpt-5.
    //   x、14–69B 3 旗舰 — Opus/Fable/Mythos、gpt-5.6+、70B+
    enum class Tier : std::uint8_t { Weak = 0, Cheap = 1, Mid = 2, Flagship = 3 };

    [[nodiscard]] constexpr Tier tier() const noexcept {
        if (weak_tool_use) return Tier::Weak;
        // 旗舰通道：Opus/Fable/Mythos，或者采用`max`的gpt-5.x。
        if (is_flagship()) return Tier::Flagship;
        if (is_gpt() && supports_effort_max()) return Tier::Flagship;
        // 便宜巷：俳句是正典小托管Claude。
        if (is_haiku()) return Tier::Cheap;
        // 中路：Sonnet 和普通的 gpt-5.x 主力线。
        if (is_sonnet() || is_gpt()) return Tier::Mid;
        // 未知系列（本地Ollama/OpenAI-compat/任何自定义主机）。
        //
        // 使用id携带的params-in-B标签。
        // 这是智能模式工作与自定义主机上的 no-op 智能
        // 模式之间的区别：每个 未知型号用于返回 Tier::Mid，
        // 因此 480B 和 4B 是同一层。后果，
        // 均在现场报道：
        //
        //   ·实用程序严格要求cheapest_capable_model某事
        //     下方 父级。一切都在中端，没有任何东西低于中端，
        //     所以它回到了父级——旗舰运行了每一个
        //     琐碎的转弯。
        //   ·实施需要最强的MID模型。与
        //     所有中间部分都是整个目录上的free-for-all，
        //     因此它可以选择型号BIGGER而不是父项（测量：父项qwen3
        //     -coder:480b-> deepseek-v3.1:671b）。
        //   ·来自战略的De-escalation因此永远不会发生，
        //     无论环境旋钮如何调整。
        //
        // 阈值遵循本地生态系统实际运输的大小，并且故意是粗略的——这是成本
        // -routing提示，而不是基准。 70B+是旗舰通道（llama-70b、
        // qwen3-coder:480b、kimi-k2:1t、deepseek
        // 671b）； 14-69B 是中间主力车道（qwen3:32b，
        // mistral-small:24b）；任何在弱检查中幸存下来的较
        // 小的东西都是便宜的车道（qwen3：8b，4b）。
        const int pb = params_b;
        if (pb >= 70) return Tier::Flagship;
        if (pb >= 14) return Tier::Mid;
        if (pb >  0)  return Tier::Cheap;
        // 根本没有尺寸标签（`glm-4.6`、`minimax-m2`、裸自定义名称）。
        // 假设mid-capable：它是安全的中间——
        // 它从不默默地将未知的产品提升为旗舰产品，
        // 并且从不将有能力的模型降级到仅运行琐碎转弯的廉价车道。
        return Tier::Mid;
    }

    // 相同层，但也尊重明确标记提供者的-cheaper 变体 (gpt-5
    // -mini、gpt-4o-mini、*-nano)的`mini`
    // /`nano`/`small`/`lite` id提示。
    // 它们与它们的全兄弟姐妹解码为同一个家族，因此 id 字符串是唯一的区分信号；
    // 持有该 ID 的路由器将其传递到此处，将其降级到廉价通道。
    [[nodiscard]] static constexpr Tier tier_for(std::string_view id) noexcept {
        const Tier base = from_id(id).tier();
        if (base == Tier::Weak) return base;
        auto has = [&](std::string_view n) {
            if (n.size() > id.size()) return false;
            for (std::size_t i = 0; i + n.size() <= id.size(); ++i) {
                bool eq = true;
                for (std::size_t j = 0; j < n.size(); ++j) {
                    char a = id[i + j], b = n[j];
                    if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + 32);
                    if (a != b) { eq = false; break; }
                }
                if (eq) return true;
            }
            return false;
        };
        const bool cheap_hint =
            has("mini") || has("nano") || has("small") || has("lite")
            || has("flash") || has("turbo");
        // OpenAI o-series推理模型（o1/o3/o4)：变体的基础是昂贵的、
        // 缓慢的推理模型——NOT是廉价的主力——所以决不能将其选作"cheaper Mid"。
        // 只有 -mini/-nano o-series 变体 才是真正便宜的。
        // 检测到 `o<digit>` 代币，因此 `o1`、`o3-pro`
        // 排名为旗舰，而 `o1-mini`、`o3-mini` 排名为廉价。
        auto is_o_series = [&]() {
            for (std::size_t i = 0; i + 1 < id.size(); ++i) {
                const bool at_start = (i == 0) || id[i - 1] == '-' || id[i - 1] == '/';
                if (at_start && (id[i] == 'o' || id[i] == 'O')
                    && id[i + 1] >= '1' && id[i + 1] <= '9')
                    return true;
            }
            return false;
        };
        if (is_o_series()) return cheap_hint ? Tier::Cheap : Tier::Flagship;
        if (cheap_hint) return Tier::Cheap;
        return base;
    }

    // 解码id字符串。 高频路径 上的纯/noexcept/无分支。
    // 没有 分配——分词器使用 string_view 分割。
    [[nodiscard]] static constexpr ModelCapabilities from_id(std::string_view id) noexcept {
        ModelCapabilities caps{};

        // 去除`[1m]`扩展-context后缀。当用户选择 1M
        // -window 变体 时，agentty 会附加此内容；
        // 上游 id 不携带它。
        if (auto pos = id.find("[1m]"); pos != std::string_view::npos) {
            caps.extended_context_1m = true;
            id = id.substr(0, pos);
        }

        // 在 '-' 上代币化。家庭生活在任何等于 "haiku" / "sonnet"
        // / "opus" 的代币上； Generation 是紧随其后的
        // 整数-parseable 标记。
        std::string_view prev{};
        std::size_t start = 0;
        // 对于紧随生成令牌之后的令牌来说也是如此——即修订版
        // （`opus-4-8`→修订版8）。通过任何其他标记重置，
        // 以便以后的杂散整数（日期、尺寸标签）不会被误读。
        bool expect_revision = false;
        for (std::size_t i = 0; i <= id.size(); ++i) {
            const bool boundary = (i == id.size() || id[i] == '-');
            if (!boundary) continue;
            if (i > start) {
                std::string_view tok = id.substr(start, i - start);
                const bool was_expecting_revision = expect_revision;
                expect_revision = false;
                if (tok == "haiku")       caps.family = Family::Haiku;
                else if (tok == "sonnet") caps.family = Family::Sonnet;
                else if (tok == "opus")   caps.family = Family::Opus;
                else if (tok == "fable")  caps.family = Family::Fable;
                else if (tok == "mythos") caps.family = Family::Mythos;
                // NOTE：`gpt`在这里匹配的是NOT。 gpt-4o / gpt-4.1
                // / gpt-3.5 必须保留 Family::Unknown（通用
                // OpenAI-compat：16k 上限，不费力）。一旦版本令牌确认生成 >= 5，
                // 只有 gpt-5.x 响应行会获取 Family::Gpt，如下设置。
                else if (was_expecting_revision) {
                    // 修订令牌——与一代 解析 相同的
                    // 1-/2-digit 合理性检查，因此日期不能漏掉。
                    int r = 0;
                    bool ok = !tok.empty() && tok.size() <= 2;
                    for (char c : tok) {
                        if (c < '0' || c > '9') { ok = false; break; }
                        r = ::agentty::util::push_digit(r, c);
                    }
                    if (ok) caps.revision = r;
                }
                else if (prev == "gpt") {
                    // OpenAI 模式定义将版本打包到`gpt`之后的ONE点标记中(
                    // `gpt-5.6-sol`→"5.6"；`gpt-5`→"5")。
                    // 解析整数部分作为生成，小数部分作为修订，
                    // 因此努力门可以区分gpt-5.6(接受`max`)
                    // 和gpt-5.4(xhigh top)。
                    // 只有gpt-5+响应行变成Family::Gpt；
                    // gpt-4o / gpt-4.1 / gpt-3.5 保持未知（通用兼容性）。
                    int g = 0, r = 0;
                    bool ok = !tok.empty(), frac = false, any_frac = false;
                    int gd = 0, rd = 0;   // 每个部分的位数
                    for (char c : tok) {
                        if (c == '.') {
                            if (frac || gd == 0) { ok = false; break; }
                            frac = true;
                            continue;
                        }
                        if (c < '0' || c > '9') { ok = false; break; }
                        // 下面仅接受1-2位数字部分；停止添加过去的内容，
                        // 这样长时间运行就不会溢出 int (UB)。
                        if (frac) { if (rd < 3) r = r * 10 + (c - '0'); ++rd; any_frac = true; }
                        else      { if (gd < 3) g = g * 10 + (c - '0'); ++gd; }
                    }
                    // 与Claude分支完全相同的界限：版本部分是1-2位数字。
                    // 如果没有 digital-count 上限，
                    // 像 `gpt-20250514` 这样的代币将 解析 作为第
                    // 20250514 代，并且仅在上限偶然情况下才通过 `g <= 99`。
                    if (ok && gd >= 1 && gd <= 2 && rd <= 2 && g >= 5 && g <= 99) {
                        caps.family = Family::Gpt;
                        caps.generation = g;
                        caps.generation_4_or_later = true;
                        if (any_frac) caps.revision = r;
                    }
                }
                else if (prev == "haiku" || prev == "sonnet" || prev == "opus"
                         || prev == "fable" || prev == "mythos") {
                    // 版本令牌 — 解析 作为 int (无 分配)。
                    // 两个 拼写到达此分支并且都已生效：
                    //
                    //   DASHED `claude-opus-4-7` 生成于本令牌中，
                    //                               修订于NEXT 之一。
                    //   DOTTED `claude-opus-4.7` gen 和
                    //                               修订版被打包到这一令牌中。
                    //
                    // 点线形式是 GitHub Copilot 的 /models
                    // 返回的内容（`claude-opus-4.7`、
                    // `claude-sonnet-5.5`、`claude-fable-5.1`），
                    // 并且通过 `size() <= 2` 长度检查，它曾经是 REJECTED
                    // — '4'、'.'、'7' 是三个字符 — 因此每个点线
                    // Copilot ID 都解码为第 0 代/修订版0，同时保留其家人。
                    // 这是可用的最糟糕的故障模式：`is_opus()`保持真实，
                    // 所以看起来没有什么未知的，但是每个读取NUMBER的门
                    // 默默地回答"old model"——从Opus 4.7+
                    // 中扣除的努力`max`/`xhigh`，
                    // 跳过1M-context梯级，并且标签呈现为裸"Opus"，
                    // 因此`claude-opus-4.7`和`claude-opus-4.8`
                    // 折叠到ONE 选择面板 行（#68 中重复的
                    // -looking 行与从另一侧看到的错误相同）。
                    //
                    // 解析在这里都是形状，与`gpt`分支已经具有的
                    // 形状完全相同。引发长度检查的日期守卫被保留并明确
                    // 表示：LEGACY 模式定义 (
                    // `claude-3-5-sonnet-20241022`)
                    // 在家族后面放置了一个 8-digit 日期，
                    // 并将其读取为 20241022 代将错误地通过
                    // 每个 `>= 4` 门。一代为1-2位，改版为1-2位；
                    // 任何更长的都不是一个版本。
                    int g = 0, r = 0;
                    bool ok = !tok.empty(), frac = false, any_frac = false;
                    int gd = 0, rd = 0;   // 每个部分的位数
                    for (char c : tok) {
                        if (c == '.') {
                            // 仅一个点，并且它必须分隔两个数字序列——
                            // `4.`、`.7`和`4.7.1`不是版本。
                            if (frac || gd == 0) { ok = false; break; }
                            frac = true;
                            continue;
                        }
                        if (c < '0' || c > '9') { ok = false; break; }
                        if (frac) { if (rd < 3) r = r * 10 + (c - '0'); ++rd; any_frac = true; }
                        else      { if (gd < 3) g = g * 10 + (c - '0'); ++gd; }
                    }
                    // 两个部分的合理性窗口相同——这就是将 `20241022`
                    // (8 位数字)排除在外的原因，无论是否带有点。
                    if (ok && gd >= 1 && gd <= 2 && rd <= 2 && (!frac || any_frac)) {
                        caps.generation = g;
                        caps.generation_4_or_later = (g >= 4);
                        if (any_frac) {
                            // 虚线：修订随一代而来，因此NEXT令牌
                            // 是变体字(`-latest`，
                            // `-thinking`)，从未修订。
                            caps.revision = r;
                        } else {
                            expect_revision = true;  // 虚线：下一步是修订
                        }
                    }
                }
                prev = tok;
            }
            start = i + 1;
        }
        // 解析是大小标签，BEFORE是弱推理——两者都读取它，
        // 并且tier()稍后从存储的字段中读取它。
        caps.params_b          = params_b_from_id(id);
        caps.weak_tool_use     = infer_weak_tool_use(id, caps);
        caps.reasoning_compat  = infer_reasoning_compat(id, caps);
        caps.effort_high_only  = infer_effort_high_only(id);
        return caps;
    }

private:
    // 仅根据模型的id来确定模型在tool-calling处是否弱。
    //
    // 强（NOT弱）：
    //   -任何已知的Claude家族(托管的、优秀的工具使用)。
    //   -具有native/trained工具-calling的当地家庭：qwen3、llama3.1、
    //     llama3.3、mistral/mixtral/ministral、command-r、hermes、
    //     firefunction、functionary、devstral、codestral、gpt-oss、花岗岩、
    //     glm-4、deepseek(v3/v4/r1/reasoner/chat)、grok、gemini、magistral。
    //   -任何模型≥14B参数（足够大以遵循工具模式定义）。
    //
    // 弱（用守卫治疗）：
    //   -小型局部模型（<=〜8B参数：7b、3b、1.5b等）。
    //   - 老/coder-only小户泄露工具JSON(qwen2.5，
    //     codellama、deepseek-coder、phi、gemma<=9b、starcoder、stable-code)。
    //   - 未知的 ids 默认为 不 弱（假设有能力）除非 的 id
    //     带有小-parameter标签。
    //
    // 参数大小（以十亿为单位），从 id 中的 `<N>[.<M>]b` 标记解析（
    // qwen2.5-coder:7b、llama3.1:70b、qwen3-coder:
    // 480b、phi3:3.8b）。使用整数部分（下限）— 1.7b 计为 1，6.7b
    // 计为 6。当 id 不包含尺寸标签时返回 0（每个托管 Claude/GPT id
    // 以及诸如 `glm-4.6` 等命名版本而不是尺寸的本地 id）。
    //
    // 从 infer_weak_tool_use 中吊出，因此 TIER 也可以使用它。
    // 它曾经是该函数中的本地函数，这就是为什么 size 只产生 二进制
    // 程序 weak/not-weak 答案，而从不产生层梯度 - 这个错误使每
    // 个 Ollama 模型都落在 Tier::Mid 中。
    [[nodiscard]] static constexpr int params_b_from_id(
            std::string_view id) noexcept {
        int params_b = 0;
        for (std::size_t i = 0; i < id.size(); ++i) {
            const char c = id[i];
            // 'b' = 十亿，'t' = TRILLIONS。
            // 前沿的 open-weight 模型将自己标记为 T (kimi-k2:1t)，
            // 如果没有这个，它们会解析为 0 == "no size tag"
            // 并跌至中间默认值 - 生态系统中最大的模型落在中间车道。
            const bool is_b = (c == 'b' || c == 'B');
            const bool is_t = (c == 't' || c == 'T');
            if ((!is_b && !is_t) || i == 0) continue;
            // 'b' 之后的字符不能是字母（因此跳过 "bf16" 等）
            if (i + 1 < id.size()) {
                char n = id[i + 1];
                if ((n >= 'a' && n <= 'z') || (n >= 'A' && n <= 'Z')) continue;
            }
            // 回顾一下数字标记：数字，最多有一个 '.'。
            std::size_t d = i;
            bool seen_dot = false, seen_digit = false;
            while (d > 0) {
                char p = id[d - 1];
                if (p >= '0' && p <= '9') { seen_digit = true; --d; }
                else if (p == '.' && !seen_dot) { seen_dot = true; --d; }
                else break;
            }
            if (!seen_digit) continue;            // 'b' 之前没有数字
            // 数字之前的 char 必须是分隔符，而不是字母
            if (d > 0) {
                char p = id[d - 1];
                bool sep = !((p >= 'a' && p <= 'z') || (p >= 'A' && p <= 'Z'));
                if (!sep) continue;
            }
            // 仅 解析 和 INTEGER 部分（第一个 '.' 之前的所有内容）。
            // 饱和：id 是不可信文本，长数字运行会溢出
            // int (UB)。真正的模型没有一百万个参数。
            constexpr int kCap = 1'000'000;
            int v = 0;
            for (std::size_t k = d; k < i; ++k) {
                if (id[k] == '.') break;
                v = v >= kCap ? kCap : v * 10 + (id[k] - '0');
            }
            if (is_t) v = v >= kCap / 1000 ? kCap : v * 1000;   // 1t == 1000b
            if (v > kCap) v = kCap;
            if (v > params_b) params_b = v;       // 取最大的匹配
        }
        return params_b;
    }

    [[nodiscard]] static constexpr bool infer_weak_tool_use(
            std::string_view id, const ModelCapabilities& caps) noexcept {
        // 已知的Claude家族→永远强大。
        if (caps.is_known_family()) return false;

        auto contains = [](std::string_view hay, std::string_view needle) {
            if (needle.size() > hay.size()) return false;
            for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i) {
                bool eq = true;
                for (std::size_t j = 0; j < needle.size(); ++j) {
                    char a = hay[i + j], b = needle[j];
                    if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + 32);
                    if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + 32);
                    if (a != b) { eq = false; break; }
                }
                if (eq) return true;
            }
            return false;
        };

        // 参数大小以十亿为单位（0 = id 不带有大小标签）。
        // 在运行之前已被 from_id 解析。
        const int params_b = caps.params_b;

        // Tool-trained / 说明-strong 当地家庭 → 即使
        // 尺寸较小也很坚固。
        const bool strong_family =
            contains(id, "qwen3")        || contains(id, "llama3.1")   ||
            contains(id, "llama-3.1")    || contains(id, "llama3.3")   ||
            contains(id, "llama-3.3")    || contains(id, "mistral")    ||
            contains(id, "mixtral")      || contains(id, "ministral")  ||
            contains(id, "command-r")    || contains(id, "hermes")     ||
            contains(id, "firefunction") || contains(id, "functionary")||
            contains(id, "devstral")     || contains(id, "codestral")  ||
            contains(id, "gpt-oss")      || contains(id, "granite")    ||
            contains(id, "glm-4")        || contains(id, "deepseek-v3")||
            contains(id, "deepseek-v4")  || contains(id, "deepseek-r1")||
            contains(id, "deepseek-reasoner") || contains(id, "deepseek-chat") ||
            contains(id, "grok")         || contains(id, "gemini")      ||
            contains(id, "magistral");
        // >= ~7B 的强大家庭是可靠的；只有 标志 如果它明显很小（
        // <= 3B），那么它就会很弱，即使是好的家庭也会陷入困境。
        if (strong_family) return params_b != 0 && params_b <= 3;

        // 已知弱者/编码器-only/小户泄露工具JSON。
        const bool weak_family =
            contains(id, "qwen2.5")      || contains(id, "qwen2")      ||
            contains(id, "codellama")    || contains(id, "code-llama") ||
            contains(id, "deepseek-coder")|| contains(id, "starcoder") ||
            contains(id, "stable-code")  || contains(id, "phi")        ||
            contains(id, "gemma")        || contains(id, "tinyllama")  ||
            contains(id, "smollm")       || contains(id, "codegemma")  ||
            contains(id, "sqlcoder");
        if (weak_family) return true;

        // 大型型号（无弱-family信号）可靠地跟随工具模式定义。
        if (params_b >= 14) return false;

        // 没有家庭信号：依赖大小。 <= 8B → 弱；
        // 否则假设模型（或托管端点，未知 ID）有能力。
        if (params_b != 0 && params_b <= 8) return true;
        return false;
    }

    // 确定 OpenAI-Chat-wire 模型是否通过 top-level
    // `reasoning_effort` 枚举 (low|medium|high) 公开可配置推理，
    // 因此 标签 和 `reasoning_effort` 载荷 的工作应该为此点亮。
    //
    // 这是故意id-string推断（无网络探测），镜像infer_weak_tool_use。
    // Claude/GPT 由 `family` 梯子处理，并且必须由 不
    // 设置此 标志（他们的努力是家庭 -gated）。我们识别接受 top
    // -level `reasoning_effort` 的托管推理行：
    //   • Mistral — ADJUSTABLE-reasoning 型号采用 reasoning_effort：
    //       - "mistral-medium"（中3.5，i.e。mistral-medium-latest /
    //         米斯特拉尔-medium-3-5 …)
    //       - "mistral-small"（小4，i.e。mistral-small-latest /
    //         milsral-small-3-
    //     * …) Mistral 的 "magistral-*" 型号将 NATIVELY 和
    //     REJECT 的参数解释为 HTTP 422 — 因此它们在下面明确为 EXCLUDED。
    //     （根据 Mistral 的推理文档进行验证，第 20 期。）
    //   • DeepSeek "deepseek-reasoner" / "deepseek-r1"（r1 也进行蒸馏）。
    //   • xAI Grok 推理行："grok-4*"、"grok-3-mini" (grok-3-mini
    //     原因； grok-code-fast 是一个非 -reasoning 编码器→排除）。
    //   • 通过OpenAI 兼容垫片Google Gemini "*-thinking"。
    //   • OpenAI o-series 通过聊天传输（"o1"、"o3"、"o4-mini"）进行代理。
    //
    // 用户覆盖（AGENTTY_FORCE_EFFORT，在 运行时 处读
    // 取）由 effort_capable() 在工作瓶颈处单独应用
    // - 此处有意参考 不，因此 from_id() 保持纯 constexpr
    // 解码（高频路径 上没有 getenv，可在常量表达式中使用）。
    [[nodiscard]] static constexpr bool infer_reasoning_compat(
            std::string_view id, const ModelCapabilities& caps) noexcept {
        // Claude/GPT是家庭-gated；切勿将它们穿过此 标志。
        if (caps.is_known_family() || caps.family == Family::Gpt) return false;

        auto contains = [](std::string_view hay, std::string_view needle) {
            if (needle.size() > hay.size()) return false;
            for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i) {
                bool eq = true;
                for (std::size_t j = 0; j < needle.size(); ++j) {
                    char a = hay[i + j], b = needle[j];
                    if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + 32);
                    if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + 32);
                    if (a != b) { eq = false; break; }
                }
                if (eq) return true;
            }
            return false;
        };

        // 排除 首次 — 与推理系列共享前缀的非 -reasoning
        // 编码器行和 native-reasoning 模型。
        // 下面这些肯定不输给什么，所以早点回来吧。
        //   • grok-code-fast：非-reasoning 编码器（共享"grok"）。
        //   • 裁判官-*：原因 NATIVELY 和 REJECTS reasoning_effort (422)，
        //     即使它共享 "mistral"… 不 — "magistral"
        //     是一个不同的标记，但要明确保护，以便将来的子字符串匹配
        //     无法抓住它。
        if (contains(id, "grok-code")) return false;
        if (contains(id, "magistral")) return false;

        return contains(id, "mistral-small")     // 米斯特拉尔小4（可调）
            || contains(id, "mistral-medium")    // Mistral Medium 3.5（可调）
            || contains(id, "deepseek-reasoner")
            || contains(id, "deepseek-r1")
            || contains(id, "grok-4")
            || contains(id, "grok-3-mini")
            || contains(id, "gpt-oss")           // OpenAI 打开-weight 推理器
            || contains(id, "-thinking")
            || contains(id, "o1")
            || contains(id, "o3")
            || contains(id, "o4-mini");
    }

    // Mistral-platform型号:reasoning_effort 枚举是BINARY。
    // 探测 api.mistral.ai（2026 年 11 月）：采用该参数的每个模型都接受 仅 {
    // none, high} 和 low/medium/minimal/xhigh/max
    // 上的 400 秒以及 "supported values: [high, none]"。
    // 由 "-stral" 家族命名（mistral / magistral / ministral
    // / devstral / codestral / voxtral / Leanstral）
    // 认可 - 该家族中的非 -reasoning 系无论如何都有努力 OFF，所以这里的
    // over-match 是无害的（标志 仅在努力时塑造阶梯）。纯 ID 解码：在任何 目录 获取
    // 到达之前，从 帧 1 开始正确。
    [[nodiscard]] static constexpr bool infer_effort_high_only(
            std::string_view id) noexcept {
        auto contains = [](std::string_view hay, std::string_view needle) {
            if (needle.size() > hay.size()) return false;
            for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i) {
                bool eq = true;
                for (std::size_t j = 0; j < needle.size(); ++j) {
                    char a = hay[i + j], b = needle[j];
                    if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + 32);
                    if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + 32);
                    if (a != b) { eq = false; break; }
                }
                if (eq) return true;
            }
            return false;
        };
        return contains(id, "stral");
    }
};

// ── Provider 能力键的范围 ───────────────────────────────
// SAME裸模型ID可以存在于具有DIFFERENT合同的多个提供
// 商上（Groq上的gpt-oss-120b vs Cerebras
// vs本地Ollama）。因此，能力事实记录在范围键
// "provider/model" 下，并查找范围-first、裸-second。
// 提供商层将活动提供商 ID 推送到每个交换机 (provider::select) 上，
// 因此 resolved_caps（仅接收模型 ID）可以根据
// 提供商状态查阅范围事实，而无需域层。
namespace caps_scope_detail {
inline maya::snapshot<std::string>& val() { static maya::snapshot<std::string> s; return s; }
} // namespace caps_scope_detail

inline void set_caps_provider_scope(std::string provider_id) {
    caps_scope_detail::val().store(std::move(provider_id));
}
[[nodiscard]] inline std::string caps_provider_scope() {
    return caps_scope_detail::val().get();
}
// 当设置了范围时，"provider/model"； "" 当没有时（调用者则使用裸）。
// one-arg形式使用ACTIVE提供者范围；在解析属于
// 不同 提供程序的行时显式传递 `scope`（立即融合每个
// 经过验证的提供程序的模型的 选择面板 渲染）。
// 模型组件被规范化（capkey::norm_model)，
// 因此 "3.5" 与 "3-5" 与案例 变体 是一个关键——注册
// 机构的作者进行相同的规范化，使得拼写-based在结构上不可能丢失。
[[nodiscard]] inline std::string scoped_caps_key(std::string_view model_id,
                                                 std::string_view scope) {
    if (scope.empty()) return {};
    return capkey::scoped(scope, model_id);
}
[[nodiscard]] inline std::string scoped_caps_key(std::string_view model_id) {
    return scoped_caps_key(model_id, caps_provider_scope());
}

// ── 现场直播 -catalog 推理 注册表 ──────────────────────────────────────────
// 一些提供商DECLARE per-model在他们的/v1/models 载荷中
// 提供推理支持(Mistral:capabilities.reasoning true
// |false)。实时 标志 击败了 id 推断 - 它跟踪提供者的实际 分派 表，
// 该表在我们的静态启发下向 两者 方向漂移（magistral-* 现在 ACCEPTS
// reasoning_effort，尽管 from_id 排除了它；
// 日期为 mistral-medium-2505/2508 REJECT，尽管子字符串匹配）。
// list_models()在这里记录了标志； resolved_caps() 归并
// 它在 UNDER 中是用户的显式覆盖，但 OVER 是 from_id 的猜测。
// 与覆盖 注册表 相同的 锁 规则（原子空探针 → 共享 锁），因为它也位于 渲染
// 路径上。
// ── 能力时代 ──────────────────────────────────────────────────────
// 单调递增的计数器被EVERY能力-registry写入（目录推理声明、
// declared/learned努力集、用户覆盖）碰撞。
// 记忆 resolved_caps-derived 值（融合的 选择面板 的
// per-model reason_flags）的调用者在构建时标记纪元并在其移动
// 时重建 — 因此 models.dev 刷新登陆 mid-session 会
// 使依赖于它的 缓存 完全无效，并且不会进行任何轮询。
namespace caps_epoch_detail {
inline std::atomic<std::uint64_t>& counter() {
    static std::atomic<std::uint64_t> c{1};
    return c;
}
} // namespace caps_epoch_detail
[[nodiscard]] inline std::uint64_t caps_epoch() noexcept {
    // 获取与发布碰撞的配对：看到新纪元的读者也看到
    // 在其之前发生的每个 注册表 交换。
    return caps_epoch_detail::counter().load(std::memory_order_acquire);
}
inline void bump_caps_epoch() noexcept {
    caps_epoch_detail::counter().fetch_add(1, std::memory_order_release);
}


// 将注册表键标准化到位：模型组件（在最后一个'/'之后，
// 或裸露时的整个字符串）经过capkey::norm_model，
// 因此无论源拼写如何，作者和读者都同意字节-for-byte。
[[nodiscard]] inline std::string norm_caps_key(std::string_view key) {
    if (auto slash = key.rfind('/'); slash != std::string_view::npos) {
        std::string out{key.substr(0, slash + 1)};
        out += capkey::norm_model(key.substr(slash + 1));
        return out;
    }
    return capkey::norm_model(key);
}

// 下面每个注册表共享的形状：被不同来源毒害的事实和密钥的地图，
// 保存在jaal::snapshot中。
//
// 这些在每-frame 渲染路径上并且从每流式传输轮(
// resolved_caps)读取并且很少写入，但在BULK中：
// models.dev 加载是~80k事实。这是 快照 的形状：读取器
// 采用一个原子 加载 而不是 锁，编写器构建一个新映射并将其交换。
// 因为每次写入都会复制整个映射，所以批量源必须使用 CatalogBatch
// （每个 加载 一个副本），而不是 per-fact setter，否则 加载 会变成二次方。
// 没有锁就没有等级，没有能力，所以视图、状态更新函数和锁 正文都可以自由读取大写，
// 并且它们都不能是死锁的一半。
template <class V>
struct Facts {
    std::map<std::string, V> map;
    std::set<std::string>    poisoned;
};
template <class V>
struct Registry {
    maya::snapshot<Facts<V>> facts;

    // 一批写入，应用于ONE副本并发布一次。 快照 写入会复制整个映射，
    // 因此一次写入 N 个事实的时间复杂度为 O(N^2)； models.dev
    // 加载 约为 80k 写入。构建一个批次，然后 commit() 它。
    struct Batch {
        std::vector<std::pair<std::string, V>> sets;
        std::vector<std::pair<std::string, V>> merges;   // 同意-or-poison
        [[nodiscard]] bool empty() const noexcept { return sets.empty() && merges.empty(); }
    };
    void commit(Batch b) {
        if (b.empty()) return;
        (void)facts.update([](const Facts<V>& f, const Batch& batch) {
            Facts<V> out = f;
            for (const auto& [k, v] : batch.sets) out.map[k] = v;
            for (const auto& [k, v] : batch.merges) merge_into(out, k, v);
            return out;
        }, std::move(b));
    }

    // 记录确切密钥的事实。
    void set(std::string key, V v) {
        (void)facts.update([](const Facts<V>& f, const std::string& k, const V& val) {
            Facts<V> out = f;
            out.map[k] = val;
            return out;
        }, std::move(key), v);
    }
    // 同意-or-poison合并跨源共享的密钥。
    void merge(std::string key, V v) {
        (void)facts.update([](const Facts<V>& f, const std::string& k, const V& val) {
            if (f.poisoned.count(k)) return f;
            Facts<V> out = f;
            merge_into(out, k, val);
            return out;
        }, std::move(key), v);
    }
    // 放下一把钥匙。返回是否剩下任何东西。
    bool erase(std::string key) {
        return !facts.update([](const Facts<V>& f, const std::string& k) {
            Facts<V> out = f;
            out.map.erase(k);
            return out;
        }, std::move(key))->map.empty();
    }
    // 范围-first，裸-tail 回退。
    [[nodiscard]] std::optional<V> find(std::string_view scoped, std::string_view bare) const {
        const auto f = facts.load();
        if (f->map.empty()) return std::nullopt;
        if (!scoped.empty())
            if (auto it = f->map.find(std::string(scoped)); it != f->map.end()) return it->second;
        if (auto it = f->map.find(std::string(bare)); it != f->map.end()) return it->second;
        return std::nullopt;
    }
    // 替换一切（设置水合物）。
    void replace(std::map<std::string, V> all) {
        facts.store(Facts<V>{std::move(all), {}});
    }
    [[nodiscard]] std::map<std::string, V> snapshot() const { return facts.load()->map; }
    [[nodiscard]] bool any() const { return !facts.load()->map.empty(); }

    // 同意-or-poison规则，在一处为merge()和commit()。
    static void merge_into(Facts<V>& f, const std::string& k, const V& val) {
        if (f.poisoned.count(k)) return;
        if (auto it = f.map.find(k); it != f.map.end()) {
            if (it->second != val) { f.map.erase(it); f.poisoned.insert(k); }
            return;
        }
        f.map[k] = val;
    }
};

namespace catalog_reasoning_detail {
inline Registry<bool>& reg() { static Registry<bool> r; return r; }
// 其记录的跨源推理事实CONFLICTED(一个源说原因，
// 另一个源说没有)的密钥被中毒。 Cross-provider 聚合器
// 不同意 相同 裸 id（e.g。一个将 `mistral-large-2402`
// 列为推理，另一个则不列出）——否则平面裸命名空间将是 Last
// -writer-wins，默默地在指令模型上点亮推理 标签。
// 中毒密钥被视为"no info"，因此解决方案落入范围事实或
// id-inference，而不是信任硬币-flip。
} // namespace catalog_reasoning_detail

inline void set_catalog_reasoning(std::string model_id, bool reasons) {
    bump_caps_epoch();
    catalog_reasoning_detail::reg().set(norm_caps_key(model_id), reasons);
}

// 合并可能与DIFFERENT源（modelsdev中的裸-tail路径）
// 共享密钥的事实。第一作者记录；后来的作家认为 AGREES 是 no-op；
// 后来的一位作者说 DISAGREES 毒害了密钥，因此它读作
// "no info"。作用域键永远不会在提供者之间发生冲突，
// 因此它们使用上面的普通设置器；只有模糊的裸末尾经过这里。
inline void merge_catalog_reasoning(const std::string& raw_id, bool reasons) {
    catalog_reasoning_detail::reg().merge(norm_caps_key(raw_id), reasons);
}
// Tri-state:1(目录表示原因)、0(目录表示不)、-1(无信息)。
// Scoped-first ("provider/model")，裸回退 —参见scoped_caps_key。
[[nodiscard]] inline int catalog_reasoning_for(std::string_view model_id,
                                               std::string_view scope = {}) {
    if (!catalog_reasoning_detail::reg().any()) return -1;
    auto scoped = scope.empty() ? scoped_caps_key(model_id)
                                : scoped_caps_key(model_id, scope);
    const auto v = catalog_reasoning_detail::reg().find(
        std::move(scoped), std::string{capkey::norm_tail(model_id)});
    return v ? (*v ? 1 : 0) : -1;
}

// DECLARED努力-value集：元数据源(models.dev 快照，
// 或发布其努力枚举的提供者)命名模型接受的确切ON级别。
// 位于 下方 的学习的 -from-rejection 注册表（来自
// 提供商自己的 400 节拍 Third-party 元数据
// 的基本事实）和低于用户覆盖，但 上方 from_id 派生。
// 与上述推理声明相同的键控和 锁 规则。
namespace catalog_effort_detail {
inline Registry<std::uint8_t>& reg() { static Registry<std::uint8_t> r; return r; }
} // namespace catalog_effort_detail

inline void set_catalog_effort_set(std::string model_id, std::uint8_t set) {
    bump_caps_epoch();
    catalog_effort_detail::reg().set(norm_caps_key(model_id), set);
}

// 模型的推理如何切换OFF(reasoning_off.hpp中的枚举)。
namespace catalog_reasoning_off_detail {
inline Registry<std::uint8_t>& reg() { static Registry<std::uint8_t> r; return r; }
} // namespace catalog_reasoning_off_detail

// Anthropic自己的/v1/models表示模型接受什么：thinking.type值
// （ModelCapabilities::kThink*位）和努力水平（effort_bit掩码，
// 0=未声明）。 First-party，所以它击败了 id 规则和
// models.dev。也写在请求因思维类型被拒绝时，因此每个会话最多会做出
// 一次错误的猜测。带钥匙 "anthropic/<model>"。
struct AnthropicDecl {
    std::uint8_t thinking = 0;
    std::uint8_t effort   = 0;
    bool operator==(const AnthropicDecl&) const = default;
};
namespace anthropic_decl_detail {
inline Registry<AnthropicDecl>& reg() { static Registry<AnthropicDecl> r; return r; }
} // namespace anthropic_decl_detail

inline void set_anthropic_decls(std::vector<std::pair<std::string, AnthropicDecl>> all) {
    Registry<AnthropicDecl>::Batch b;
    for (auto& [id, d] : all)
        b.sets.emplace_back(scoped_caps_key(id, "anthropic"), d);
    anthropic_decl_detail::reg().commit(std::move(b));
    bump_caps_epoch();
}
[[nodiscard]] inline std::optional<AnthropicDecl> anthropic_decl_for(
        std::string_view model_id, std::string_view scope = {}) {
    if (!anthropic_decl_detail::reg().any()) return std::nullopt;
    const std::string s = scope.empty() ? caps_provider_scope() : std::string{scope};
    if (s != "anthropic") return std::nullopt;
    return anthropic_decl_detail::reg().find(scoped_caps_key(model_id, s), {});
}
// API拒绝了该型号的thinking.type `rejected`；
// `instead` 是它命名的类型（0 = 无）。从模型当前的思维事实开始，
// 其余部分保持原样。
inline void learn_anthropic_thinking(std::string_view model_id,
                                     std::uint8_t current,
                                     std::uint8_t rejected,
                                     std::uint8_t instead) {
    const std::string key = scoped_caps_key(model_id, "anthropic");
    AnthropicDecl d = anthropic_decl_detail::reg().find(key, {}).value_or(AnthropicDecl{});
    std::uint8_t t = (d.thinking ? d.thinking : current) | 1u;   // kThink声明
    t = static_cast<std::uint8_t>((t & ~rejected) | instead);
    if (t == d.thinking) return;
    d.thinking = t;
    anthropic_decl_detail::reg().set(key, d);
    bump_caps_epoch();
}

inline void set_catalog_reasoning_off(std::string model_id, ReasoningOff v) {
    bump_caps_epoch();
    catalog_reasoning_off_detail::reg().set(norm_caps_key(model_id),
                                            static_cast<std::uint8_t>(v));
}

// Bare-tail合并用于努力-set 注册表 —与merge_catalog_reasoning
// 相同的cross-provider冲突语义：同意→保留，不同意→毒害
// "no declaration"，因此解析回落到范围事实/id-inference。
inline void merge_catalog_effort_set(const std::string& raw_id,
                                     std::uint8_t set) {
    catalog_effort_detail::reg().merge(norm_caps_key(raw_id), set);
}
// ── models.dev 上下文 -window 声明 ──────────────────────────────
//
// 为每个非-Claude模型提供真实窗口的梯级。
//
// ModelCapabilities::from_id()对于已知族报告200k，
// 对于其他一切报告0——Claude-shaped答案，因为这是它解码的族。
// 相对于实时 快照 进行测量，剩下 2698 个模型，其实际窗口为 1M 或更多
// 读数作为 200k 默认值：gemini-2.5-pro (1M)、
// gpt-4.1 (1M)、qwen3-coder-plus (1M)、
// llama-4-scout (10M)。一个模型的窗口被低估了 5 倍，
// 压缩速度比需要的时间快五倍，并且用户被告知一个没有人宣布的数字。
//
// 与上面的推理和努力注册表相同的形状，以及相同的冲突规则，
// 因为危险是相同的：SAME裸id由不同主机以不同大小提供（qwen3
// -coder-plus在iflowcn上是256k，在alibaba
// -coding-plan-cn上是1M）。因此，范围记录是权威的，
// 共享的裸密钥是 merge-or-poison——两个不同的来源将密钥读取
// 为 "no declaration"，而不是让一个主机的数字渗透到另一个
// 主机的数字上。
//
// 这是故意的 第二-TO-LAST 梯级（参见 resolve_context_window)：
// 来自端点的实时广告排名高于它，因为只有网关知道 IT
// 如何为模型提供服务，并且只有网关可以在溢出时返回 400。
// 静态元数据是在没有任何广告的情况下可用的最佳猜测 -
// 对于 custom-host 情况来说，这是大多数情况。
namespace catalog_context_detail {
inline Registry<int>& reg() { static Registry<int> r; return r; }
} // namespace catalog_context_detail

inline void set_catalog_context_window(std::string model_id, int tokens) {
    if (tokens <= 0) return;
    bump_caps_epoch();
    catalog_context_detail::reg().set(norm_caps_key(model_id), tokens);
}

// Bare-tail合并-与兄弟姐妹相同的agree-or-poison语义。
//
// NOTE密钥：norm_TAIL，而不是norm_caps_key。
// 裸命名空间由 末尾 精确键入，因此供应商 -prefixed id
// 及其裸形式 COLLIDE 可能会互相毒害；在这里写入前缀键
// 意味着 "elsewhere/model" 和 "model" 永远
// 不会相遇，因此永远无法检测到不一致，并且读取器（查找 末尾）将
// 错过每个前缀写入。这种不对称性是兄弟 注册表 中的一个真正的错误；
// 不要在这里复制它。
inline void merge_catalog_context_window(const std::string& raw_id, int tokens) {
    if (tokens <= 0) return;
    std::string key{capkey::norm_tail(raw_id)};
    if (key.empty()) return;
    catalog_context_detail::reg().merge(std::move(key), tokens);
}

// 模型的实际输出上限，如提供者或models.dev所声明的那样。
// 与上下文窗口相同的作用域：作用域记录获胜，裸末尾是merge
// -or-poison。
namespace catalog_max_output_detail {
inline Registry<int>& reg() { static Registry<int> r; return r; }
} // namespace catalog_max_output_detail

inline void set_catalog_max_output(std::string model_id, int tokens) {
    if (tokens <= 0) return;
    bump_caps_epoch();
    catalog_max_output_detail::reg().set(norm_caps_key(model_id), tokens);
}
inline void merge_catalog_max_output(const std::string& raw_id, int tokens) {
    if (tokens <= 0) return;
    std::string key{capkey::norm_tail(raw_id)};
    if (key.empty()) return;
    catalog_max_output_detail::reg().merge(std::move(key), tokens);
}

// ── 终点 -keyed 声明(海关) -host / 本地主机( R) 接口边界) ──────────
// 上面的提供者-scoped映射由agentty的PROVIDER id键控—
// —对于自定义主机来说，其是用户带类型的 ("localhost:11434"，
// "https://api.my-gw.com/v1")的端点规范，
// 是models.dev一无所知的字符串。它的作用域条目永远无法到达这样的会话，
// 并且当两个提供者在共享模型 ID 上存在分歧时，bare-tail 合并就会中毒。
// 但是自定义主机的身份 IS 及其 URL，并且 models.dev 也
// 声明了一个 — 因此声明是 ALSO，由端点 ORIGIN（规范主机 + 端口，
// scheme/path 删除）索引，并在会话自己的 目录 加载 时按模型
// 声明。
//
// 首先通过 origin+PORT 进行键控，将 host-only 作为第二
// 层：端口是用户定义，而不是身份 — LM Studio 侦听您设置的任何内容，
// 但 models.dev 恰好具有 一个 lmstudio 条目 — 而在
// "127.0.0.1" 上，同一主机携带多个开发条目（atomic-chat:
// 1337、lmstudio:1234、 lynkr:8081) 不得 合并。
// 它们之间的第二个限定符是 MODEL ID 本身：声明适用于每个模型，
// 因此会话行仅采用实际声明它的条目； 不同 端口上的条目比会话永远不适用（请参阅
// src/util/modelsdev.cpp 中的
// claim_context_windows_for_endpoint）。
namespace catalog_endpoint_detail {
struct Entry {
    int tokens = 0;
    int port   = 0;   // 0 当声明 API URL 使用方案默认值时
};
// 主机→（模型→条目）
using Hosts = std::map<std::string, std::map<std::string, Entry>>;
inline maya::snapshot<Hosts>& hosts() { static maya::snapshot<Hosts> h; return h; }
} // namespace catalog_endpoint_detail

// 解析 ORIGIN 出自 API URL/端点规范：规范主机（小写；
// 每个本地主机拼写 — "localhost"、"127.0.0.1"、
// "0.0.0.0"、"::1" — 归并 到 "127.0.0.1"）加上显式端口，
// 当 URL 使用方案默认值时为 0。 "" 当没有主机可以恢复时。
[[nodiscard]] inline std::string
context_endpoint_origin(std::string_view url, std::uint16_t& port_out) {
    port_out = 0;
    auto s = url;
    if (auto p = s.find("://"); p != std::string_view::npos) {
        const auto scheme = s.substr(0, p);
        std::string sch;
        for (char c : scheme) sch.push_back(
            static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        if (sch == "http")  port_out = 80;
        if (sch == "https") port_out = 443;
        s.remove_prefix(p + 3);
    }
    if (auto p = s.find('/'); p != std::string_view::npos)
        s = s.substr(0, p);
    if (auto p = s.find('@'); p != std::string_view::npos)
        s = s.substr(p + 1);                       // 剥离用户信息
    std::string hostport{s};
    while (!hostport.empty() && (hostport.back() == ':')) hostport.pop_back();
    std::string host = hostport;
    if (auto colon = hostport.rfind(':'); colon != std::string::npos) {
        const std::string ps = hostport.substr(colon + 1);
        if (!ps.empty()
            && std::all_of(ps.begin(), ps.end(),
                           [](char c){ return c >= '0' && c <= '9'; })) {
            const long v = std::strtol(ps.c_str(), nullptr, 10);
            port_out     = static_cast<std::uint16_t>(v);
            host         = hostport.substr(0, colon);
        }
    }
    // 规范化：小写，归并每个环回拼写到"127.0.0.1"上，
    // 因此指向"localhost:1234"的会话满足models.dev条目，
    // 其API表示"127.0.0.1:1234"。
    std::string canon;
    canon.reserve(host.size());
    for (char c : host)
        canon.push_back(
            static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (canon == "localhost" || canon == "0.0.0.0" || canon == "[::1]"
        || canon == "::1")
        canon = "127.0.0.1";
    if (canon.empty()) return {};
    return canon;
}

// 在其端点的原点下索引一个模型的声明窗口。每个声明都落在（规范主机，
// 来自 API 的 ORIGINAL 端口，URL — 当 URL
// 使用方案默认值时为 0）；另一个端口上的会话是否可以声称
// 它是读取器的 调用 (endpoint_context_window_for)，
// 它知道会话的端口和正在解析的模型 ID。
// Same-origin重写覆盖（刷新的快照是更新的事实），
// 因此这永远不会中毒——与裸-tail合并不同，
// 错误的-port条目只是无法匹配而不是删除信息。
inline void merge_endpoint_context_window(std::string_view api_url,
                                          const std::string& raw_id,
                                          int tokens) {
    if (tokens <= 0) return;
    std::uint16_t port = 0;
    const std::string host = context_endpoint_origin(api_url, port);
    if (host.empty()) return;
    const std::string model_id = norm_caps_key(raw_id);
    if (model_id.empty()) return;
    bump_caps_epoch();
    (void)catalog_endpoint_detail::hosts().update(
        [](const catalog_endpoint_detail::Hosts& hs, const std::string& h, const std::string& m,
           const catalog_endpoint_detail::Entry& e) {
            auto out = hs;
            out[h][m] = e;
            return out;
        },
        host, model_id, catalog_endpoint_detail::Entry{tokens, port});
}

// ── 散装 加载 ──────────────────────────────────────────────────────────────
// 每个 目录 写入都会发布其整个地图的新副本，因此每个事实调用上面的设置器
// 一次是二次的：models.dev 加载 (~9k 模型，
// ~80k 事实)花费了几分钟的 CPU re-copyingever-larger 地图。
// 批量源用 相同 调用 填充 CatalogBatch 并提交
// 一次：每个 注册表 都被复制并发布一次。键在这里的标准化与单个
// setter 的操作完全相同。
struct CatalogBatch {
    Registry<bool>::Batch         reasoning;
    Registry<std::uint8_t>::Batch effort;
    Registry<std::uint8_t>::Batch reasoning_off;
    Registry<int>::Batch          context;
    Registry<int>::Batch          max_output;
    std::vector<std::tuple<std::string, std::string, catalog_endpoint_detail::Entry>> endpoints;

    void set_reasoning(std::string id, bool r) {
        reasoning.sets.emplace_back(norm_caps_key(id), r);
    }
    void merge_reasoning(const std::string& raw_id, bool r) {
        reasoning.merges.emplace_back(norm_caps_key(raw_id), r);
    }
    void set_effort_set(std::string id, std::uint8_t s) {
        effort.sets.emplace_back(norm_caps_key(id), s);
    }
    void merge_effort_set(const std::string& raw_id, std::uint8_t s) {
        effort.merges.emplace_back(norm_caps_key(raw_id), s);
    }
    void set_reasoning_off(std::string id, ReasoningOff v) {
        if (v != ReasoningOff::Unknown)
            reasoning_off.sets.emplace_back(norm_caps_key(id), static_cast<std::uint8_t>(v));
    }
    void merge_reasoning_off(const std::string& raw_id, ReasoningOff v) {
        if (v != ReasoningOff::Unknown)
            reasoning_off.merges.emplace_back(norm_caps_key(raw_id),
                                              static_cast<std::uint8_t>(v));
    }
    void set_context_window(std::string id, int tokens) {
        if (tokens > 0) context.sets.emplace_back(norm_caps_key(id), tokens);
    }
    void merge_context_window(const std::string& raw_id, int tokens) {
        if (tokens <= 0) return;
        std::string key{capkey::norm_tail(raw_id)};
        if (!key.empty()) context.merges.emplace_back(std::move(key), tokens);
    }
    void set_max_output(std::string id, int tokens) {
        if (tokens > 0) max_output.sets.emplace_back(norm_caps_key(id), tokens);
    }
    void merge_max_output(const std::string& raw_id, int tokens) {
        if (tokens <= 0) return;
        std::string key{capkey::norm_tail(raw_id)};
        if (!key.empty()) max_output.merges.emplace_back(std::move(key), tokens);
    }
    void merge_endpoint_context_window(std::string_view api_url,
                                       const std::string& raw_id, int tokens) {
        if (tokens <= 0) return;
        std::uint16_t port = 0;
        std::string host = context_endpoint_origin(api_url, port);
        if (host.empty()) return;
        std::string mid = norm_caps_key(raw_id);
        if (mid.empty()) return;
        endpoints.emplace_back(std::move(host), std::move(mid),
                               catalog_endpoint_detail::Entry{tokens, port});
    }

    // 发布所有内容：一份副本+每注册表一份原子交换。
    //
    // 注册表是单独的快照，因此读者可以在其他注册表之前
    // 观察批次的注册表中的一些。首先发布读者查找 之后
    // 决定（窗口、工作集、端点窗口）的事实，最后发布推理
    // （resolved_caps 检查以决定的内容），
    // 因此将新模型视为推理的读者也会看到其其余事实。
    // caps_epoch 最后移动：只有在一切就位后，
    // 键入其上的 缓存 才会重建。
    void commit() {
        const bool any = !reasoning.empty() || !effort.empty()
                      || !reasoning_off.empty()
                      || !context.empty() || !max_output.empty()
                      || !endpoints.empty();
        if (!any) return;
        catalog_context_detail::reg().commit(std::move(context));
        catalog_max_output_detail::reg().commit(std::move(max_output));
        catalog_effort_detail::reg().commit(std::move(effort));
        catalog_reasoning_off_detail::reg().commit(std::move(reasoning_off));
        if (!endpoints.empty())
            (void)catalog_endpoint_detail::hosts().update(
                [](const catalog_endpoint_detail::Hosts& hs, const auto& eps) {
                    auto out = hs;
                    for (const auto& [h, mid, e] : eps) out[h][mid] = e;
                    return out;
                }, std::move(endpoints));
        catalog_reasoning_detail::reg().commit(std::move(reasoning));
        bump_caps_epoch();
    }
};

// 0=无声明；否则，窗口将有一个 models.dev 条目为该
// 模型声明服务于该 ENDPOINT。等级，最强者优先：
//   1.确切来源（主机+会话自己的端口）-固定到的条目
//      您所指的听众；
//   2、host-tier(表项的API URL没有携带显式端口)；
//   3. HOST-LEVEL采用：该端口是USER定义的（LM Studio
//      聆听您设置的任何内容；只有一个 lmstudio 条目），
//      因此 不同 端口上的 Same-host 条目仍然适用 -
//      但仅当第二个限定符表示它是明确的时：模型 id 必须实际
//      上在那里声明，并且如果多个 Same-host 条目在不同窗口
//      声明相同的模型，则拒绝采用 host-level（0），
//      而不是让atomic-chat 的数字渗入 lmstudio。
// `host`是CANONICAL起源主机(context_endpoint_origin形式)。
[[nodiscard]] inline int endpoint_context_window_for(
    std::string_view model_id, std::string_view host, std::uint16_t port) {
    const auto all = catalog_endpoint_detail::hosts().load();
    if (all->empty()) return 0;
    const std::string mid = norm_caps_key(model_id);
    if (mid.empty()) return 0;
    const auto hit = all->find(std::string(host));
    if (hit == all->end() || hit->second.empty()) return 0;
    const auto& entries = hit->second;
    int host_tier = 0, adopted = 0; bool ambiguous = false;
    for (const auto& [mid_key, e] : entries) {
        if (mid_key != mid) continue;
        if (e.port == port) return e.tokens;              // (1)准确产地
        if (e.port == 0)   { host_tier = e.tokens; continue; }  // (2)
        if (port == 0)     { host_tier = e.tokens; continue; }  // 会话
            // 也没有端口——任何 same-host 声明
            // 都是最好的证据（无端口会话无法进一步消除歧义；
            // 上面的 model-id 限定符仍然适用）。
        if (adopted == 0)  adopted = e.tokens;            // (3)主机-level
        else if (adopted != e.tokens) ambiguous = true;   // 两个条目，
            // 同一型号、同一主机上的不同窗口→无索赔。
    }
    if (host_tier > 0) return host_tier;
    return ambiguous ? 0 : adopted;
}

// 0=无声明。范围-first，裸回退。
[[nodiscard]] inline int catalog_context_window_for(std::string_view model_id,
                                                    std::string_view scope = {}) {
    if (!catalog_context_detail::reg().any()) return 0;
    auto scoped = scope.empty() ? scoped_caps_key(model_id)
                                : scoped_caps_key(model_id, scope);
    return catalog_context_detail::reg()
        .find(scoped, capkey::norm_tail(model_id))
        .value_or(0);
}

// 0=无声明。范围-first，裸回退。
[[nodiscard]] inline int catalog_max_output_for(std::string_view model_id,
                                                std::string_view scope = {}) {
    if (!catalog_max_output_detail::reg().any()) return 0;
    auto scoped = scope.empty() ? scoped_caps_key(model_id)
                                : scoped_caps_key(model_id, scope);
    return catalog_max_output_detail::reg()
        .find(scoped, capkey::norm_tail(model_id))
        .value_or(0);
}

// -1=无声明；否则声明的 ON 级别的位掩码。
// Scoped-first ("provider/model")，裸回退 —参见scoped_caps_key。
[[nodiscard]] inline int catalog_effort_set_for(std::string_view model_id,
                                                std::string_view scope = {}) {
    if (!catalog_effort_detail::reg().any()) return -1;
    auto scoped = scope.empty() ? scoped_caps_key(model_id)
                                : scoped_caps_key(model_id, scope);
    const auto v = catalog_effort_detail::reg().find(scoped, capkey::norm_tail(model_id));
    return v ? static_cast<int>(*v) : -1;
}

// 该模型如何关闭推理，范围-first，如工作集。
[[nodiscard]] inline ReasoningOff catalog_reasoning_off_for(std::string_view model_id,
                                                            std::string_view scope = {}) {
    if (!catalog_reasoning_off_detail::reg().any()) return ReasoningOff::Unknown;
    auto scoped = scope.empty() ? scoped_caps_key(model_id)
                                : scoped_caps_key(model_id, scope);
    const auto v = catalog_reasoning_off_detail::reg().find(scoped,
                                                            capkey::norm_tail(model_id));
    return v ? static_cast<ReasoningOff>(*v) : ReasoningOff::Unknown;
}

// 所声明的事实，否则模型族已知会做什么。
// 只有行为不依赖主人的家庭才会来这里。
[[nodiscard]] inline ReasoningOff reasoning_off_for(std::string_view model_id,
                                                    std::string_view scope = {}) {
    if (auto d = catalog_reasoning_off_for(model_id, scope); d != ReasoningOff::Unknown)
        return d;
    std::string lower{model_id};
    for (char& c : lower) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    auto has = [&](std::string_view n) { return lower.find(n) != std::string::npos; };
    // gpt-oss和o-series总是推理；地板低。
    if (has("gpt-oss") || has("deepseek-r1") || has("deepseek-reasoner")
        || has("magistral") || has("qwq"))
        return ReasoningOff::Impossible;
    // 以 on/off 开关为模板的混合思想家。
    if (has("qwen3") && !has("thinking-2507") && !has("instruct-2507"))
        return ReasoningOff::Toggle;
    if (has("glm-4.5") || has("glm-4.6") || has("glm-4.7") || has("glm-5")
        || has("deepseek-v3.1") || has("deepseek-v3.2") || has("deepseek-v4")
        || has("nemotron") || has("granite-3") || has("granite-4")
        || has("kimi-k2-thinking") || has("seed-oss"))
        return ReasoningOff::Toggle;
    return ReasoningOff::Unknown;
}

// ── 人数 -model 推理 -effort 覆盖 注册表 ─────────────────────────
// "configure it myself" 接口边界 (问题#20)。 constexpr
// from_id()仅解码INFERS是否是兼容型号的原因；用户可以覆盖每个模型 - 保留在
// Settings.reasoning_effort_overrides 中并在启动时推送到此处
// （以及在每个 in-app 切换 上），镜像 provider::set_custom_auth
// _header。解析优先级，由 resolved_caps() 应用：
//     per-model覆盖>AGENTTY_FORCE_EFFORT环境>from_id推断。
// Claude/GPT留在家庭-gated-仅覆盖opens/closes的COMPAT车道
// （低|中|高）；它从不制造 max/xhigh 梯子。
namespace reasoning_override_detail {
// 覆盖映射对于绝大多数用户来说是空的(没有人设置^E覆盖)，但是在PER
// -FRAME UI 渲染路径上调用reasoning_override_for；
// 快照 读取是一个原子 加载。
inline Registry<bool>& reg() { static Registry<bool> r; return r; }
inline void erase(std::string key) { (void)reg().erase(std::move(key)); }
} // namespace reasoning_override_detail

// Set/clear单个模型的覆盖（真=强制作用力开启，假=关闭）。
inline void set_reasoning_override(std::string model_id, bool on) {
    bump_caps_epoch();
    reasoning_override_detail::reg().set(norm_caps_key(model_id), on);
}
inline void clear_reasoning_override(const std::string& model_id) {
    bump_caps_epoch();
    reasoning_override_detail::erase(norm_caps_key(model_id));
}
inline void clear_reasoning_overrides() {
    bump_caps_epoch();
    reasoning_override_detail::reg().replace({});
}
inline void set_reasoning_overrides(std::map<std::string, bool> all) {
    bump_caps_epoch();
    // 标准化每个键：这些映射从持久设置中吸收，
    // 这可能早于 capkey 规则或带有与今天的
    // 查找不同的拼写。原始安装会默默地孤立这些事实。
    std::map<std::string, bool> normed;
    for (auto& [k, v] : all) normed[norm_caps_key(k)] = v;
    reasoning_override_detail::reg().replace(std::move(normed));
}
// Tri-state查找：1(强制开启)、0(强制关闭)、-1(不覆盖该id)。
// 首先确定范围（"groq/<model>":^E为每个提供者设置它，
// 因为两个网关后面的相同ID可能不同），然后是来自旧版本的裸密钥。
[[nodiscard]] inline int reasoning_override_for(std::string_view model_id,
                                                std::string_view scope = {}) {
    if (!reasoning_override_detail::reg().any()) return -1;
    const auto scoped = scope.empty() ? scoped_caps_key(model_id)
                                      : scoped_caps_key(model_id, scope);
    const auto v = reasoning_override_detail::reg().find(
        scoped, std::string{capkey::norm_tail(model_id)});
    return v ? (*v ? 1 : 0) : -1;
}

// AGENTTY_FORCE_EFFORT，在main()设定一次。
// 位于上面的 per-model 覆盖旁边，在相同的 注册表 resolved_caps
// 读取中；该域不读取环境。 1 强制开启，0 强制关闭，-1 未设置。
namespace effort_force_detail {
inline std::atomic<int>& cell() noexcept { static std::atomic<int> v{-1}; return v; }
}
inline void set_effort_force(int v) noexcept {
    effort_force_detail::cell().store(v, std::memory_order_relaxed);
    bump_caps_epoch();
}
// 解析 AGENTTY_FORCE_EFFORT的值："1"/true/yes/on→1，"0"→0，否则-1。
[[nodiscard]] inline int parse_effort_force(const char* e) noexcept {
    if (!e || !*e) return -1;
    if (e[0] == '0' && e[1] == '\0') return 0;
    const char c = (e[0] >= 'A' && e[0] <= 'Z') ? static_cast<char>(e[0] + 32) : e[0];
    if (e[0] == '1' || c == 't' || c == 'y' || c == 'o') return 1;
    return -1;
}
[[nodiscard]] inline int effort_force_override() noexcept {
    return effort_force_detail::cell().load(std::memory_order_relaxed);
}

// ── 已拒收的 -field 注册表(每 HOST) ──────────────────────────────
// 主机告诉我们它不接受的可选 request-body 字段（
// provider::OptionalField 位），从它自己的 400
// (#93) 获悉。由 HOST（"api.groq.com"、
// "llm.corp:8443"）键入，而不是型号：正文-schema
// 验证是网关的，其背后的每个型号都是相同的。 传输层 在添加可选字段之前会参考此信息，
// 因此在 一个 拒绝后，主机再也不会看到该字段 - 保留在“设置”中，
// 因此即使在下次启动时也不会出现。与其他注册表相同的 锁 规则。
namespace rejected_fields_detail {
inline Registry<std::uint16_t>& reg() { static Registry<std::uint16_t> r; return r; }
} // namespace rejected_fields_detail

[[nodiscard]] inline std::string host_key(std::string_view host, std::uint16_t port) {
    std::string k{host};
    for (char& c : k) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    if (port != 0 && port != 443) k += ":" + std::to_string(port);
    return k;
}
inline void add_rejected_field(const std::string& host, std::uint16_t bit) {
    // OR 在一次 快照 更新中，因此两个并发学习不能下降一点。
    (void)rejected_fields_detail::reg().facts.update(
        [](const Facts<std::uint16_t>& f, const std::string& k, std::uint16_t b) {
            Facts<std::uint16_t> out = f;
            out.map[k] = static_cast<std::uint16_t>(out.map[k] | b);
            return out;
        }, host, bit);
}
inline void set_rejected_fields(std::map<std::string, std::uint16_t> all) {
    rejected_fields_detail::reg().replace(std::move(all));
}
[[nodiscard]] inline std::uint16_t rejected_fields_for(const std::string& host) {
    return rejected_fields_detail::reg().find("", host).value_or(0);
}
[[nodiscard]] inline std::map<std::string, std::uint16_t> rejected_fields_snapshot() {
    return rejected_fields_detail::reg().snapshot();
}

// ── 总结经验 -echo 注册表(每HOST + MODEL) ────────────────────────
// 哪个早期助手打开模型需要`reasoning_content`，甚至在没有自己
// 推理的情况下打开发送（provider::ReasoningEcho；#117）。
// 学习了自己的400个模型并坚持了下来(Settings.reasoning_echo)。
//
// 由主机AND型号加密，与rejected_fields不同：这是
// MODEL的合同，而不是网关的模式定义。一个网关（Vercel、
// Pollinations）并排提供严格和宽松的模型，
// 而严格的-schema主机（例如Groq 400）则未知领域，因此在主机上填充每个模型
// 的历史记录将用一个400交换另一个。
namespace reasoning_echo_detail {
inline Registry<std::uint8_t>& reg() { static Registry<std::uint8_t> r; return r; }
} // namespace reasoning_echo_detail

[[nodiscard]] inline std::string reasoning_echo_key(std::string_view host_key_,
                                                    std::string_view model_id) {
    std::string k{host_key_};
    k += '|';
    k += norm_caps_key(model_id);
    return k;
}
// 已知模型在其第一个 400 之前需要它，因此模型切换后的第一轮已经完成。
// 其中每一项都是可以学习的；这次只有保存个往返。 Beam（反射）检查 每个
// 辅助转弯并以多个名称进行运输：Vercel AI 网关上的 reflection/beam
// (-free)、Pollinations 上的 vercel-beam
// 及其 pre-launch 隐形名称 cat-jam (OpenCode
// Zen、9router#4804)。
[[nodiscard]] inline std::uint8_t seeded_reasoning_echo(std::string_view model_id) {
    const std::string tail = capkey::norm_tail(model_id);
    const auto has = [&](std::string_view s) {
        return tail.find(s) != std::string::npos;
    };
    // "reflection/beam-free"→供应商"reflection"、末尾 "beam-free"。
    const auto slash = model_id.rfind('/');
    const bool reflection = slash != std::string_view::npos
        && capkey::norm_tail(model_id.substr(0, slash)) == "reflection";
    if (has("cat-jam") || has("vercel-beam")
        || (reflection && tail.starts_with("beam")))
        return 2;   // provider::ReasoningEcho::All
    return 0;
}
inline void set_reasoning_echo(const std::string& key, std::uint8_t scope) {
    reasoning_echo_detail::reg().set(key, scope);
}
inline void set_reasoning_echoes(std::map<std::string, std::uint8_t> all) {
    reasoning_echo_detail::reg().replace(std::move(all));
}
// 学习击败种子，因此事实证明需要比其种子所说的更多（或
// 更少）的模型由其自身的400校正。
[[nodiscard]] inline std::uint8_t reasoning_echo_for(std::string_view host_key_,
                                                     std::string_view model_id) {
    if (auto v = reasoning_echo_detail::reg().find(
            "", reasoning_echo_key(host_key_, model_id)))
        return *v;
    return seeded_reasoning_echo(model_id);
}

// ── 已吸取的教训 -set 注册表 (专题) DETECTION ,而不是点数) ──────
// 当提供商拒绝 reasoning_effort 值时，它通常会在错误 正文 中命名合约 —
// Mistral: "supported values: [high, none]"；通用 OpenAI
// 兼容端点可能报告未启用 reasoning_effort。
// 数据流 状态更新函数 解析(provider::parse_effort_rejection)
// 在这里记录了EXACT接受的集合，将其持久化(
// Settings.learned_effort_sets)，
// 并且默默地使用钳位值重试。从那时起，在梯子上，夹具和 传输 对于该模型
// 来说都是正确的 - 重新启动，没有释放，对于没有数据库听说过的提供商。
// 与其他注册管理机构（渲染-path 读者）相同的 锁 规则。
namespace learned_effort_detail {
// 型号 id → effort_set 位掩码（0 = 完全拒绝该参数）。
inline Registry<std::uint8_t>& reg() { static Registry<std::uint8_t> r; return r; }
} // namespace learned_effort_detail

inline void set_learned_effort_set(std::string model_id, std::uint8_t set) {
    bump_caps_epoch();
    learned_effort_detail::reg().set(norm_caps_key(model_id), set);
}
inline void set_learned_effort_sets(std::map<std::string, std::uint8_t> all) {
    bump_caps_epoch();
    // 标准化每个键——与上述set_reasoning_overrides
    // 相同的持续-spelling危险。
    std::map<std::string, std::uint8_t> normed;
    for (auto& [k, v] : all) normed[norm_caps_key(k)] = v;
    learned_effort_detail::reg().replace(std::move(normed));
}
[[nodiscard]] inline std::map<std::string, std::uint8_t>
learned_effort_sets_snapshot() {
    return learned_effort_detail::reg().snapshot();
}
// -1=对于该id没有学到任何东西；否则为位掩码（可能为 0 = 参数关闭）。
// Scoped-first查找：在Groq处于活动状态时了解到的事实是键入
// "groq/gpt-oss-120b"并且NOT是否泄漏到Cerebras上的
// 相同ID；裸露的条目（遗留的，或明确未限定范围的）仍然在任何地方匹配。
[[nodiscard]] inline int learned_effort_set_for(std::string_view model_id,
                                                std::string_view scope = {}) {
    if (!learned_effort_detail::reg().any()) return -1;
    auto scoped = scope.empty() ? scoped_caps_key(model_id)
                                : scoped_caps_key(model_id, scope);
    const auto v = learned_effort_detail::reg().find(
        std::move(scoped), std::string{capkey::norm_tail(model_id)});
    return v ? static_cast<int>(*v) : -1;
}

// 解码运行时推理-effort覆盖中的模型ID 和 归并（每个模型，
// 然后AGENTTY_FORCE_EFFORT环境）。这是纯 constexpr
// from_id() 的 id-aware 兄弟：调用 站点使用 此，因此用户的覆盖
// 统一达到 supports_effort()/effort_wire_for()
// /选择面板 — 每个消费者都已经通过返回的上限进行了漏斗。
//
// `scope`：要解决其合同的提供商。空 = 活动 提供程序（
// 适用于 传输 路径）。当 渲染 CROSS
// -provider 出现时，传递行自己的 provider_id（
// 融合的 选择面板 立即显示每个经过身份验证的提供程序的行 - 解析活动
// Mistral 范围下的 Groq 行将读取错误的主机事实）。
[[nodiscard]] inline ModelCapabilities resolved_caps(std::string_view model_id,
                                                     std::string_view scope = {}) {
    ModelCapabilities caps = ModelCapabilities::from_id(model_id);
    // LEARNED集适用于每个通道——甚至Claude/GPT——因为
    // 它是直接来自提供商自己的拒绝的基本事实。 （实际上 Claude/GPT
    // 永远不会登陆这里；守卫存在于聚合器主机中，这些主机通过不同的
    // 合约通过 OpenAI 传输 提供 claude-* id。）
    if (const int learned = learned_effort_set_for(model_id, scope);
        learned >= 0) {
        caps.effort_set       = static_cast<std::uint8_t>(learned);
        caps.effort_set_known = true;
    }
    // Claude/GPT是家庭-gated；覆盖仅触及兼容通道，因此我们永远不会
    // 制造他们的 max/xhigh 梯子或干扰 tier/context 逻辑。
    if (caps.is_known_family() || caps.family == ModelCapabilities::Family::Gpt) {
        // Anthropic自己对此型号的声明，当我们有它的时候。
        if (caps.family != ModelCapabilities::Family::Gpt)
            if (const auto d = anthropic_decl_for(model_id, scope)) {
                caps.thinking_types = d->thinking;
                if (d->effort != 0 && !caps.effort_set_known) {
                    caps.effort_set       = d->effort;
                    caps.effort_set_known = true;
                }
            }
        // 对这些的思考是强制性的：选择面板中没有"off"，
        // 并且存储的关闭读取为最低的努力。
        if (caps.thinking_always_on())
            caps.reasoning_off = static_cast<std::uint8_t>(ReasoningOff::Impossible);
        return caps;
    }
    // 声明的（models.dev/发布提供商）努力枚举——仅
    // 兼容通道：家庭阶梯已经是精确的，
    // Claude/GPT的第三-party元数据表达了不同的传输语义（自适应思维）。
    if (!caps.effort_set_known)
        if (const int declared = catalog_effort_set_for(model_id, scope);
            declared >= 0) {
            caps.effort_set       = static_cast<std::uint8_t>(declared);
            caps.effort_set_known = true;
        }
    // 优先级：显式per-model覆盖>全局环境>提供者的OWN 目录声明
    // （capabilities.reasoning，
    // 由list_models记录）>from_id推断。 目录 层是吸收提供
    // 商漂移的层 - e.g。 Mistral 翻转裁判以接受
    // reasoning_effort，或拒绝它的过时的 Mistra-medium
    // 修订版 - 无需更改代码。
    const int per_model = reasoning_override_for(model_id, scope);
    if (per_model >= 0) {
        caps.reasoning_compat = (per_model == 1);
        // 显式的用户重写甚至击败了学习的集合：force-on重新打开导出的梯子，
        // force-off杀死它。
        if (caps.effort_set_known) {
            if (per_model == 0) { caps.effort_set = 0; }
            else if (caps.effort_set == 0) { caps.effort_set_known = false; }
        }
    } else if (const int env = effort_force_override(); env >= 0) {
        caps.reasoning_compat = (env == 1);
        if (env == 0 && caps.effort_set_known) caps.effort_set = 0;
    } else if (const int live = catalog_reasoning_for(model_id, scope);
               live >= 0) {
        caps.reasoning_compat = (live == 1);
    }
    caps.reasoning_off = static_cast<std::uint8_t>(reasoning_off_for(model_id, scope));
    return caps;
}

// 方便：直接从模型id字符串推断weak-tool-use。
// 由仅保存id而不保存大写字母的provider/runtime路径使用。
[[nodiscard]] inline bool is_weak_model(std::string_view model_id) noexcept {
    return ModelCapabilities::from_id(model_id).is_weak_tool_user();
}

// 该模型BEGIN是否在以NO打开think-tag的推理中输出？
// vLLM 调用这个原因-by-default;这样做的家庭是
// Magistral（米斯特拉尔的推理线——发出想法，
// 然后是[/THINK]，然后是答案）和DeepSeek-R1 /
// deepseek-reasoner（仅有时在前面加上<think>）。
// 对于这些，首次 关闭标记之前的引导内容是隐式推理，必须路由到思维块。
// 任何其他模型都必须将 绝不 的引导文本视为推理 - 普通答案
// 散文中的杂散 "</think>" 保持逐字。
//
// MODEL事实，而不是传输层事实：它描述了如何训练模型来发出令牌，
// 在为其提供服务的每个主机上都是相同的。住在这里（与 is_weak_model
// / infer_reasoning_compat），
// 因此每个 传输层 都会读取一个权限 - openai-compat
// 传输层 用于嗅探此内联，这正是第二个 传输层 的漂移方式。
[[nodiscard]] constexpr bool reasons_by_default(std::string_view model_id) noexcept {
    // constexpr-friendly case-insensitive子字符串扫描
    // (镜像from_id的推理块使用的contains()帮助器)。
    constexpr auto has = [](std::string_view id, std::string_view n) {
        if (n.size() > id.size()) return false;
        for (std::size_t i = 0; i + n.size() <= id.size(); ++i) {
            bool eq = true;
            for (std::size_t j = 0; j < n.size(); ++j) {
                char a = id[i + j], b = n[j];
                if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
                if (a != b) { eq = false; break; }
            }
            if (eq) return true;
        }
        return false;
    };
    return has(model_id, "magistral")
        || has(model_id, "deepseek-r1")
        || has(model_id, "deepseek-reasoner");
}
static_assert(reasons_by_default("Magistral-Small-2506"));
static_assert(reasons_by_default("deepseek-r1:70b"));
static_assert(!reasons_by_default("mistral-small-latest"));
static_assert(!reasons_by_default("qwen3:32b"));   // 显式-tag家族


// 这个ID是否是chat/completions模型，代理实际上可以
// DRIVE（数据流文本+调用工具），而不是原始提供商
// `/v1/models`转储也列出的嵌入/图像/音频/审核/实时端点？
//
// 子代理路由器选择最便宜的CAPABLE候选；如果没有这个门，它会很乐意选择
// `text-embedding-3-small` 或 `dall-e-3` 作为
// "cheapest Mid"，并将整个子代理路由到无法聊天的模型 - 彻底破坏任务。
// OpenAI 的 /v1/models 是最严重的罪犯（它返回密钥可以触及的所有资产）；
// Anthropic/ChatGPT/Ollama 列表是 chat-only，所以这是一个
// 无操作。根据已知的非聊天模型标识设置拒绝清单。
// 片段 - 拒绝-list（不是allow-list），因此永远不会错误地排除新的聊天模型。
[[nodiscard]] inline bool is_dispatchable_model(std::string_view id) noexcept {
    auto has = [&](std::string_view n) {
        if (n.size() > id.size()) return false;
        for (std::size_t i = 0; i + n.size() <= id.size(); ++i) {
            bool eq = true;
            for (std::size_t j = 0; j < n.size(); ++j) {
                char a = id[i + j], b = n[j];
                if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + 32);
                if (a != b) { eq = false; break; }
            }
            if (eq) return true;
        }
        return false;
    };
    // 出现在原始/v1/models列表中的非-chat资产系列。
    if (has("embedding") || has("embed")     ||   // 文本-embedding-3-*，*-embed
        has("dall-e")    || has("dalle")     ||   // 图像生成
        has("whisper")                       ||   // 讲话-to-text
        has("tts")                           ||   // 文字-to-speech
        has("audio")     || has("realtime")  ||   // 音频/实时套接字
        has("moderation")                    ||   // 全方位-moderation-*
        has("image")                         ||   // *-image-*代
        has("transcribe")                    ||   // GPT-4o-transcribe
        has("search")                        ||   // *-search-preview检索
        has("rerank")    || has("reranker")  ||   // 重新排序器(cohere/voyage)
        has("guard"))                             // llama-guard安全过滤器
        return false;
    return true;
}

// ── Model路由器:为授权角色选择最廉价的能动模型 ──────
//
// Read-only子代理角色(explorer/reviewer)
// 做繁重的工作——读取、grep、映射、总结——小型模型可以以
// 一小部分成本处理和旗舰模型一样的工作。将它们路由到更便宜的型号是
// 最大的子代理经济杠杆，但它必须 绝不 默默地降低没有更便宜的
// 提供商的质量，并且必须留在 相同 提供商内（交叉-vendor
// auth/caching 不转移）。
//
// 策略(所有信号已经-decoded，无网络)：
//   ·仅考虑活跃提供商实际提供的模型（`candidates`，
//     i.e。 m.d.available_models — 已经提供者-scoped）。
//   ·按ModelCapabilities::tier_for(id)排名：拒绝弱（第0层，不可靠
//     在工具处)以及探针标记为no-tools的任何东西。
//   ·选择仍然≥`min_tier`的LOWEST层（能力底线
//     对于角色）——这是该提供商上最便宜的有能力的模型。
//   ·从不路由UP:如果最便宜的有能力的候选者不比最便宜的候选者便宜
//     父级，保留父模型（返回不变）。因此，仅使用 Opus 或仅
//     使用一种模型的用户不会看到任何变化，也没有回归。
//
// `parent_model`是主回合使用的； `candidates`
// 是提供商的型号列表。返回运行角色的 id。
[[nodiscard]] inline std::string cheapest_capable_model(
        std::string_view parent_model,
        const std::vector<ModelInfo>& candidates,
        ModelCapabilities::Tier min_tier = ModelCapabilities::Tier::Cheap) {
    using Tier = ModelCapabilities::Tier;
    const Tier parent_tier = ModelCapabilities::tier_for(parent_model);

    std::string best;
    Tier best_tier = Tier::Flagship;
    int  best_ctx  = -1;
    for (const auto& mi : candidates) {
        const std::string_view id = mi.id.value;
        // 报告没有工具支持的探测器使该模型不合格——子代理
        // 的全部工作就是工具的使用。
        if (!mi.tools_allowed()) continue;
        // 切勿路由到原始 /v1/models 转储也列出的非 -chat 资产 (
        // embedding/image/audio/moderation) — 它无法运行代理轮流。
        if (!is_dispatchable_model(id)) continue;
        const Tier t = ModelCapabilities::tier_for(id);
        if (t == Tier::Weak) continue;              // 工具永远不可靠
        if (t < min_tier) continue;                 // 角色所在楼层下方
        if (t >= parent_tier) continue;             // 不比母品便宜
        // 偏好最便宜的等级；在一个层内更喜欢 LARGER 上下文
        // 窗口（真正的聊天模型而不是 4k 存根/预览 变体）。
        const int ctx = mi.context_window;
        const bool better = best.empty() || t < best_tier
                          || (t == best_tier && ctx > best_ctx);
        if (better) {
            best = mi.id.value;
            best_tier = t;
            best_ctx  = ctx;
        }
    }
    // 无论我们路由到哪个id，DROP都是`[1m]`/`[2m]`扩展的
    // -context 选择面板标记。该标记是一个parent-chat-only信号：
    // 它加宽了窗口AND，使传输层发送`context-1m` beta 头文件。
    // 子代理永远不需要它 - read-only 角色在age-faded 工具
    // 结果的紧张输出预算下运行，甚至从未接近 200K 窗口，更不用说 1M 了。
    // 更糟糕的是，在无权获得整个子代理请求 (
    // "long context beta is not yet available for this subscription")（当前订阅尚未开放长上下文测试功能。）
    // 的 long-context beta 400 的 事件订阅 上
    // 发送 `context-1m`，这正是此条要防止的失败。
    // 当没有什么东西严格来说更便宜时，我们仍然会落入此处，因此选择 `[1m]`
    // 变体 的家长将简单的、始终 -accepted 的 ID 交给其子代理商。
    return wire_model_id(best.empty() ? parent_model : std::string_view{best});
}

// ── Model 选择面板 命令:先是力量,从不固定家庭桶 ──────
//
// id的FAMILY NAME绝不能决定其在选择面板中的位置——早期设计以该
// 固定顺序将"Opus, Sonnet, Haiku, Fable, Mythos"
// 分入桶中，这将Fable/Mythos沉到了最底部，即使它们是NEWEST旗舰
// -tier车道（与Opus相同的强度等级，
// 只是碰巧排序最后alphabetically/positionally的代号不同）。
// 相反，按实际能力排序：tier() 降序（旗舰型号 — Opus 和 Fable/Mythos
// — 始终领先），然后是同一层中最新的 generation.revision，
// 然后是一个小系列的 tie-break，因此相同的 -generation 对
// 端 仍然按可预测的方式分组，而不是仅在 id 字符串上交错。
[[nodiscard]] inline bool model_picker_less(const ModelInfo& a,
                                            const ModelInfo& b) noexcept {
    const auto ca = ModelCapabilities::from_id(a.id.value);
    const auto cb = ModelCapabilities::from_id(b.id.value);
    const auto ta = ModelCapabilities::tier_for(a.id.value);
    const auto tb = ModelCapabilities::tier_for(b.id.value);
    if (ta != tb) return ta > tb;                       // 较高等级第一
    // 同一层：对于未知家庭，id的尺寸标签是我们拥有的唯一强度信号，
    // 并且它必须击败下面按字母顺序排列的tie-break。
    // 如果没有这个 `qwen3:4b` 在 `qwen3:8b` ("4" < "8")
    // 之前排序，那么 "strongest below the parent" 选择
    // 了 SMALLER 模型 — 并且实现最终可能比实用程序更弱。
    if (ca.params_b != cb.params_b && (ca.params_b || cb.params_b))
        return ca.params_b > cb.params_b;               // 先做大
    if (ca.generation != cb.generation)
        return ca.generation > cb.generation;           // 最新一代第一
    if (ca.revision != cb.revision)
        return ca.revision > cb.revision;                // 最新改版第一
    auto family_name_rank = [](ModelCapabilities::Family f) -> int {
        switch (f) {
            case ModelCapabilities::Family::Opus:   return 0;
            case ModelCapabilities::Family::Fable:  return 1;
            case ModelCapabilities::Family::Mythos: return 2;
            case ModelCapabilities::Family::Sonnet: return 3;
            case ModelCapabilities::Family::Haiku:  return 4;
            default:                                return 5;
        }
    };
    const int fa = family_name_rank(ca.family);
    const int fb = family_name_rank(cb.family);
    if (fa != fb) return fa < fb;
    return a.id.value < b.id.value;   // 最终稳定平局-break
}

// ============================================================================
// 努力——用户-selectable reasoning/spend层(output_config.effort)。
// ============================================================================
// `None`不发送任何内容——保留默认的no-thinking、replay
// -safe 传输。任何其他级别都会使 Claude 提供者发送自适应思维 + 匹配
// 的 `output_config.effort`。可以从型号 选择面板 中选择实时。
// 有序LOWEST→HIGHEST推理意图。 `Minimal` 是 OpenAI
// 的 gpt-5 底层（reasoning_effort="minimal" — 最快，
// 最少推理）；它位于 下方 梯子的低处。 NB、枚举 ORDINAL 顺序是梯形顺序，
// 但持久位掩码 (effort_bit) 是它的 INDEPENDENT —
// Minimal 占用一个新的高位，因此现有的 learned_effort_sets
// 掩码（将 low..max 编码为位 0..4）保持不变。
//
// `Auto`是NOT横档。由于没有选择，而且它位于 Max 之后，
// 因此每个现有的 序号 - 以及 smart_mode 级联钳中的一个 序号
// 比较 - 都没有受到影响。
//
// 原因 IT EXISTS。 `None` 正在做两项工作："the user asked for no reasoning"
// 和 "the user never said"。这种合并打破了智能模式的整个前提。
// effort_for_complexity将用户的努力视为梯子的
// MIDPOINT——标准保持它，复杂步骤+1，简单步骤-1——但没有梯级0，
// 地板。从那里开始，分类为复合体的转弯只能到达 `minimal`，
// 一个梯级，而来自 `medium` 基地的同一转弯则到达 `high`。
// 因此，在默认情况下，分类器的判决几乎是惰性的：它可以随心所
// 欲地得分 提示词 复杂，而表盘无处可去。
//
// 这与 ROLE 轴上已经修复过一次的错误相同（参见 resolve_turn_routing
// — 该角色被硬编码为战略角色，
// 因此 "the classifier's verdict only ever moved the EFFORT dial"）。
// 镜像在努力轴上幸存下来。
//
// 将 Auto 作为其自己的状态，对于任何激活的模型（下面的
// auto_base_effort），"never configured" 都锚
// 定在梯子的 MIDPOINT 上，因此所有四个复杂性层都映射到真正不同的
// 操作点——而显式的 None 仍然意味着它始终意味着什么，并且仍然发送 ""。
enum class Effort : std::uint8_t { None, Minimal, Low, Medium, High, Xhigh, Max, Auto };

// output_config.effort的传输值。无 → ""（省略该字段）。
//
// 自动必须NEVER到达这里：它在任何wire/step/clamp
// 调用之前被resolve_auto_effort()解析为真实梯级。
// 返回 "" 是安全的 回退（与关闭相同），而不是发明提供者
// 会拒绝的级别，而是调试构建断言 - 传输 上的 Auto
// 意味着错过了解析站点。
[[nodiscard]] constexpr std::string_view effort_wire(Effort e) noexcept {
    switch (e) {
        case Effort::None:    return "";
        case Effort::Minimal: return "minimal";
        case Effort::Low:     return "low";
        case Effort::Medium:  return "medium";
        case Effort::High:    return "high";
        case Effort::Xhigh:   return "xhigh";
        case Effort::Max:     return "max";
        case Effort::Auto:    return "";   // 未解决；见上面的注释
    }
    return "";
}

// 选择面板 UI的短标签(无渲染为"off"，自动为"auto")。
[[nodiscard]] constexpr std::string_view effort_label(Effort e) noexcept {
    if (e == Effort::None) return std::string_view{"off"};
    if (e == Effort::Auto) return std::string_view{"auto"};
    return effort_wire(e);
}

// 解析将持续的传输值返回到Effort。
//
// 这两个哨兵故意不同，
// 这就是整个迁移故事：
//
//   "auto"→自动（由任何接触过该设置的人编写）""→自动（
//   绝不 CONFIGURED——自该设置存在以来，该字段一直为空
//                     -by-default，因此空值无法区分
//                     "old default"和"chose off"。
//                     将其视为自动可以为现有用户提供无需迁移通行证的修复；
//                     任何真正想要保持沉默的人都会选择`off`，
//                     该选项现在仍然存在如 "none" 和 round
//   -trips。） "none" → 无（EXPLICIT 关闭 -
//   仅由有意选择写入）未知 → 自动（此构建不知道的级别；
//                     自动是安全锚点，per-model 夹具处理其余部分）
[[nodiscard]] constexpr Effort effort_from_wire(std::string_view s) noexcept {
    if (s == "minimal") return Effort::Minimal;
    if (s == "low")    return Effort::Low;
    if (s == "medium") return Effort::Medium;
    if (s == "high")   return Effort::High;
    if (s == "xhigh")  return Effort::Xhigh;
    if (s == "max")    return Effort::Max;
    if (s == "none" || s == "off") return Effort::None;
    return Effort::Auto;
}

// 持久形式。两个哨兵的 effort_from_wire 的逆：
// 显式关闭round-trips为"none"(因此它与未触及的""
// 有区别)，并且自动为"auto"。
[[nodiscard]] constexpr std::string_view effort_to_wire_setting(Effort e) noexcept {
    if (e == Effort::None) return "none";
    if (e == Effort::Auto) return "auto";
    return effort_wire(e);
}

// AGENTTY_FORCE_EFFORT覆盖是较早定义的(在resolved_caps之前)，
// 其中归并将其转化为per-model分辨率。参见 effort_force_override()。

// ModelCapabilities::effort_set中ON努力水平的位。
// None 没有位 - "off"（省略该字段）始终可用并且永远不会被学习掉。
[[nodiscard]] constexpr std::uint8_t effort_bit(Effort e) noexcept {
    switch (e) {
        case Effort::Low:     return 1u << 0;
        case Effort::Medium:  return 1u << 1;
        case Effort::High:    return 1u << 2;
        case Effort::Xhigh:   return 1u << 3;
        case Effort::Max:     return 1u << 4;
        case Effort::Auto:    return 0;   // 不是横档：没有位，没学过
        // Minimal采用历史低..最大范围的新位ABOVE，
        // 因此在该层存在之前写入的持久掩码仍然相同地
        // 解码（无迁移）。梯形图位置由 枚举 序号 +
        // 下面的显式梯形图设置，而不是由该位值设置。
        case Effort::Minimal: return 1u << 5;
        case Effort::None:    return 0;
    }
    return 0;
}

// 根据我们所掌握的最佳信息，模型的 ON-level 设置为位掩码：
// 已知时的精确集合(learned/models.dev)，
// 否则来自family/compat门的DERIVED。这是 该 单一来源，下面的每个
// ladder/clamp/wire 帮助程序都会读取 — 异构性是数据，而不是代码路径。
[[nodiscard]] constexpr std::uint8_t effort_set_of(
        const ModelCapabilities& caps) noexcept {
    if (caps.effort_set_known) return caps.effort_set;
    if (!caps.supports_effort()) return 0;
    if (caps.effort_high_only) return effort_bit(Effort::High);
    std::uint8_t s = static_cast<std::uint8_t>(
        effort_bit(Effort::Low) | effort_bit(Effort::Medium)
        | effort_bit(Effort::High));
    // `minimal`是OpenAI的gpt-5+底层(reasoning_effort="minimal")。
    // 它不是 和 Claude/compat 级别，因此将其从第 5 代
    // 开始纳入 GPT 系列。当知道确切的组数（learned/models.dev）时，
    // 获胜；这只会播种 派生 默认值，因此 选择面板 会提供它。
    if (caps.family == ModelCapabilities::Family::Gpt && caps.generation >= 5)
        s |= effort_bit(Effort::Minimal);
    if (caps.supports_effort_xhigh()) s |= effort_bit(Effort::Xhigh);
    if (caps.supports_effort_max())   s |= effort_bit(Effort::Max);
    return s;
}

// 当模型应该暴露努力控制时为真。 `caps` 预计是 RESOLVED
// 上限（来自 resolved_caps(id)），它已经折叠在 per
// -model + env 覆盖中；这是 supports_effort 的一个简洁、
// 可读的别名，由 选择面板 视图和下面的免费函数使用。
[[nodiscard]] inline bool effort_capable(const ModelCapabilities& caps) noexcept {
    return effort_set_of(caps) != 0;
}

// THIS模型上Effort::Auto的锚点：其ON梯子的中点。
//
// 智能模式将用户的努力读取为MIDPOINT并按复杂
// 度从其步进±1。为此，锚点需要在两个方向上都有净空，
// 而这正是 `None`（第 0 级，地板）所没有的。
//
// 固定级别将是错误的修复——"medium"对于唯一的ON梯级是`high`
// 的模型来说是毫无意义的，并且对于`max`的模型来说是一种侮辱。
// 所以这是一个 POSITION，而不是一个级别：走模型实际接受的梯级并
// 选择中间的梯级。作为数据的异构性，同样的规则 effort_step
// 通过走 effort_set_of() 而不是硬编码级别来遵循。
//
//   gpt-5 {最小，低，中，高}→索引1（
//   共4）→低克劳德{低，中，高，最大}→索引1（
//   共4）→中米斯特拉尔{高}→索引0（共1）→
//   高non-reasoning{}→无（无锚定）
//
// 仅ON横档——`off`在该梯子中是故意的NOT。
// 包含它会将中点向下拖向地板，并重新引入需要
// 消除的不对称性；想要下车的用户就选择下车。
//
// Lower-middle而不是upper-middle((n-1)/2，
// 不是n/2)：从这里开始复杂的步骤+1，因此常见的重型箱子降落在-to-upper中期，
// 而自动本身在标准转弯上不会成为昂贵的选择。
[[nodiscard]] constexpr Effort auto_base_effort(
        const ModelCapabilities& caps) noexcept {
    const std::uint8_t set = effort_set_of(caps);
    if (set == 0) return Effort::None;          // 没有梯子：没有什么可以锚定的
    constexpr Effort ladder[] = {Effort::Minimal, Effort::Low, Effort::Medium,
                                 Effort::High, Effort::Xhigh, Effort::Max};
    Effort on[6]{};
    int n = 0;
    for (Effort lv : ladder) if (set & effort_bit(lv)) on[n++] = lv;
    if (n == 0) return Effort::None;
    return on[(n - 1) / 2];
}

// 将Auto解析为真实梯级；所有其他值都保持不变。
//
// 调用这在EDGE处——用户存储的努力第一次遇到具体模型的地方——因此
// 下面的步进器、夹具或传输助手不必知道Auto的存在。这使得 Auto
// 成为纯粹的 UI/persistence 概念，
// 并让 effort_step/effort_for_complexity 中的算术保持原样。
[[nodiscard]] constexpr Effort resolve_auto_effort(
        Effort e, const ModelCapabilities& caps) noexcept {
    return e == Effort::Auto ? auto_base_effort(caps) : e;
}

// 与所请求的级别相比最近支持的ON级别：更喜欢与请求最接近的级别AT OR 下方
// （不要认为比所要求的更难），否则是其之上的最低级别。
// "map intent to nearest wire value" 原语 — 设置了 二
// 进制程序 {high} 后，每个请求都映射到 high；与{low,high}，中映射低。
[[nodiscard]] constexpr Effort nearest_effort(
        Effort e, std::uint8_t set) noexcept {
    // Auto不是梯级并且必须经受住钳位：它意味着"no choice yet"，
    // 并且钳位不是选择。在这里折叠它会在用户每次切换模型
    // 时默默地破坏状态（switch_to_model_ref
    // 在每一跳上夹紧），这是该功能死亡的微妙方式
    // - 在第一次运行时正确锚定，此后永远降级为梯级。
    if (e == Effort::Auto) return Effort::Auto;
    if (set == 0 || e == Effort::None) return Effort::None;
    constexpr Effort ladder[] = {Effort::Minimal, Effort::Low, Effort::Medium,
                                 Effort::High, Effort::Xhigh, Effort::Max};
    constexpr int kN = 6;
    int want = 0;
    for (int i = 0; i < kN; ++i) if (ladder[i] == e) { want = i; break; }
    for (int i = want; i >= 0; --i)          // 在-or-below第一
        if (set & effort_bit(ladder[i])) return ladder[i];
    for (int i = want + 1; i < kN; ++i)      // 其他最低以上
        if (set & effort_bit(ladder[i])) return ladder[i];
    return Effort::None;   // 当设置!= 0时无法到达
}

// 无法停止推理的模型(gpt-oss，o3，
// R1)："off"将是传输上的谎言，因此选择面板隐藏它并且存储的
// 关闭读取作为模型采用的最低层。
[[nodiscard]] constexpr bool always_reasons(const ModelCapabilities& caps) noexcept {
    return caps.reasoning_off == static_cast<std::uint8_t>(ReasoningOff::Impossible)
        && effort_set_of(caps) != 0;
}
[[nodiscard]] constexpr Effort lowest_effort(std::uint8_t set) noexcept {
    for (Effort e : {Effort::Minimal, Effort::Low, Effort::Medium, Effort::High,
                     Effort::Xhigh, Effort::Max})
        if (set & effort_bit(e)) return e;
    return Effort::None;
}

// 将努力钳制到模型实际支持的内容并返回其传输值。 "" 当模型根本
// 无法付出努力时（或 e == None）。提供者 调用 是一个
// 过期 选择（选择了 e.g.Xhigh，然后交换到没有 xhigh
// 的模型；或者 二进制程序-enum Mistral 模型上的中等），
// 默默地降级到最近的支持级别，而不是 400ing。
[[nodiscard]] inline std::string_view effort_wire_for(
        Effort e, const ModelCapabilities& caps) noexcept {
    // 该 传输边缘。 Auto 是 UI/persistence 状态，
    // 而不是 传输 值，因此它在这里解析为其 per-model 锚点 - 这就是
    // 为什么 下游（步进器、Anthropic 传输层 中的预算表）不需要知道
    // Auto 存在。
    if (e == Effort::None && always_reasons(caps))
        return effort_wire(lowest_effort(effort_set_of(caps)));
    return effort_wire(nearest_effort(resolve_auto_effort(e, caps),
                                      effort_set_of(caps)));
}

// effort_wire_for的带类型的兄弟：将存储的工作降级为`caps`
// 实际支持的并返回工作本身(不是传输字符串)。当 SWITCHING
// MODEL/PROVIDER 修复 Model::effort 时使用，
// 以便 选择面板 标签 和请求路径一致 — 没有 xhigh 的模型上携带的 过期 Xhigh
// 在此变为 High，而在非 -reasoning 模型上的工作则崩溃为 None。
[[nodiscard]] inline Effort clamp_effort(
        Effort e, const ModelCapabilities& caps) noexcept {
    if (e == Effort::None && always_reasons(caps))
        return lowest_effort(effort_set_of(caps));
    return nearest_effort(e, effort_set_of(caps));
}

// 用户可以针对给定模型循环的有序努力：自动+关闭+模型的API所
// 接受的ON级别，按阶梯顺序。 选择面板 渲染 此，因此用户永远
// 无法达到 400 的级别 - 二进制程序-enum 型号显示自动
// ·关闭·高，完整的-ladder 旗舰显示自动·
// 关闭·低·中·高·x高·最大。
//
// 自动引导，因为它是默认和推荐的位置：它的第
// 一个←是`off`，因此彻底关闭推理仍然是一次击键，
// 并且→遍历真实级别没有改变。
//
// 在没有梯子的模型上，没有什么可以选择，因此列表保持 {off}
// - 提供 "auto"，其中自动解析为关闭将是一个谎言。
[[nodiscard]] inline std::vector<Effort> available_efforts(
        const ModelCapabilities& caps) {
    const std::uint8_t set = effort_set_of(caps);
    if (set == 0) return std::vector<Effort>{Effort::None};
    std::vector<Effort> out{Effort::Auto};
    if (!always_reasons(caps)) out.push_back(Effort::None);
    for (Effort e : {Effort::Minimal, Effort::Low, Effort::Medium, Effort::High,
                     Effort::Xhigh, Effort::Max})
        if (set & effort_bit(e)) out.push_back(e);
    return out;
}

// 步骤`cur`通过`delta`(包装)在模型的可用努力内。
// 当模型根本不支持努力时，返回 None。
[[nodiscard]] inline Effort cycle_effort(
        Effort cur, int delta, const ModelCapabilities& caps) {
    if (!effort_capable(caps)) return Effort::None;
    const auto list = available_efforts(caps);
    const int n = static_cast<int>(list.size());
    int idx = 0;
    for (int i = 0; i < n; ++i)
        if (list[static_cast<std::size_t>(i)] == cur) { idx = i; break; }
    idx = ((idx + delta) % n + n) % n;
    return list[static_cast<std::size_t>(idx)];
}

// 每个-model正常轮次的最大OUTPUT-token预算。
//
// WHY存在：全局默认值（provider::kSafeMaxTokens=
// 16384)在整个轮次中共享——推理散文AND、工具-call、JSON来自相同的预算。
// `edit` 是最坏的情况：模型重现了现有的 `old_text` VERBATIM
// （用于模糊匹配）加上 `new_text`，因此单个编辑可能是同一跨度内 `write`
// 标记的约 2 倍。当推理+编辑 JSON 一起超出 16384 时，提供程序会在
// `input_json` 中停止 数据流，并使用 stop_reason=
// max_tokens，并且参数到达时会被截断 — 显示为
// "Tool call arguments look incomplete"。（工具调用参数似乎不完整。）
// 子代理已经避开了这个（task.cpp Hard-codes 32000）；主要转弯从未发生过。
//
// ROBUSTNESS：我们设置了上限 HIGH，匹配运输代理所做的事情，
// 而不是保守地​​猜测——too-low 上限会默默地截断编辑，
// 这是我们正在修复的失败。参考文献（根据真实来源验证）：
//   • Claude 代码默认为 32000，用于 Sonnet/Opus，可通过
//                  CLAUDE_CODE_MAX_OUTPUT_TOKENS; Opus 4.6 → 64k–128k。
//   · Aider Claude 3.7 Sonnet → 64000 (输出 128k beta 头文件)；
//                  3.5Sonnet/Haiku→8192； DeepSeek-V3 → 128000。
// 现代 Claude 4.x Sonnet/Opus 正式支持 64k 输出令牌，
// 因此这是我们系列的默认设置。较旧的 3.5 Haiku/Sonnet 上限为 8k；
// 我们检测旧版 id 并进行限制，因此我们的请求绝不会超出模型允许的范围（Anthropic
// 400s a max_tokens 高于模型上限）。
//
// OVERRIDE:AGENTTY_MAX_OUTPUT_TOKENS(镜像Claude
// 代码的环境旋钮)。每个模型都以正整数获胜。 main() 读取一次，调用 读取一次
// set_max_output_override(); the domain does not read the environment.
namespace max_output_detail {
inline std::atomic<int>& cell() noexcept { static std::atomic<int> v{0}; return v; }
}
inline void set_max_output_override(int v) noexcept {
    max_output_detail::cell().store(v > 0 ? v : 0, std::memory_order_relaxed);
}
// 仅限数字；其他任何内容都视为未设置。
[[nodiscard]] inline int parse_max_output(const char* e) noexcept {
    if (!e) return 0;
    int v = 0;
    for (const char* p = e; *p >= '0' && *p <= '9'; ++p) v = ::agentty::util::push_digit(v, *p);
    return v;
}
[[nodiscard]] inline int max_output_tokens_for(std::string_view model_id) {
    if (const int v = max_output_detail::cell().load(std::memory_order_relaxed); v > 0)
        return v;
    // 提供商自己的数字（/v1/models，models.dev)胜过表格。
    // 有上限，因此一回合默认不能计费384k上限。
    if (const int declared = catalog_max_output_for(wire_model_id(model_id));
        declared > 0)
        return std::min(declared, 128000);

    const auto caps = ModelCapabilities::from_id(model_id);
    if (caps.is_known_family()) {
        // Claude家族。一代人推动了天花板：
        //   Fable/Mythos 5+ -> 64000（旗舰通道，128k-capable；
        //                       64k是安全可提高的默认值，匹配4.x Opus/Sonnet.
        //                       AGENTTY_MAX_OUTPUT_TOKENS 达到完整的
        //   128k。) 4.x Sonnet/Opus -> 64000 (官方支持的输出
        //   上限) Haiku (任何一代) -> 8192 (Haiku 的实际输出
        //   上限为 8k) <= 3.x           -> 8192 (旧
        //   型号在此之上 400) 未知 gen -> 16384 (宽敞但普遍接受)
        if (caps.is_haiku()) return 8192;
        // OpenAI gpt-5.x(ChatGPT/Codex响应):
        // 支持large-output(272k上下文)； 64k 是一个安全、
        // 通用的 -accepted 上限，与旗舰 Claude 通道相匹配。
        // 明确的意图是明确的，未来的 gpt 一代不会默默地失败。
        if (caps.is_gpt()) return 64000;
        // 旗舰车道（Fable/Mythos)和任何Claude 4-or-later Sonnet/Opus.
        if (caps.is_fable() || caps.is_mythos()) return 64000;
        if (caps.generation >= 4) return 64000;
        // 旧版模式定义 (`claude-3-5-sonnet-...`，`claude-3-opus-...`)
        // 将BEFORE代放入家族中，因此caps.generation在这里为0。
        // 检测`claude-3-`前缀并将整个3.
        // x Sonnet/Opus行视为8k-capped——在这些模型上请求更多400。
        if (model_id.find("claude-3") != std::string_view::npos) return 8192;
        if (caps.generation == 3) return 8192;
        return 16384;
    }

    // Non-Claude(本地Ollama/OpenAI-compat/未知)。
    // 映射到 num_predict / max_tokens。
    // 保持保守的共享默认值 - 已经是典型编辑的 4 倍，并且弱本地模型不会发出巨大的
    // 正文。
    return 16384;
}

} // namespace agentty
