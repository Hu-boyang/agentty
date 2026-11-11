#pragma once
// agentty::smart —角色-based执行路由(智能模式基础)。
//
// 参见docs/design/smart-mode.md.这个想法：
// 回合（或内部调用）具有ROLE——战略（计划/硬推理）、
// 实施（编写代码/机械编辑）、实用（grep/读取/提交-msg/检索）
// ——并且角色在分派时间解析为（模型、努力）PROFILE，
// 而不是硬-coding，每个模型名称调用 站点。
//
// 该头文件是PURE解析器(步骤1)。它没有 I/O、没有 运行时 依赖项、没有 传输——
// 只有给定父模型、用户的工作量和提供者的模型 目录 的映射 `role -> RoleProfile`。
// 它重用现有的 目录 原语（cheapest_capable_model、层、
// model_picker_less、clamp_effort），
// 因此 "which model is cheaper/stronger" 保留在 一个 位置。
//
// Zero-config设计为：没有用户覆盖，启用智能模式路由
//   战略->父（旗舰）模型，在用户的努力实现->最强的MID
//   -tier模型，一次努力降级实用->最便宜的有能力的模型，
//   努力OFF单个-model/Opus-only/one-tier账户降级到
// 所有工作都使用父代理的模型。
// -否改变，没有回归——就像子代理路由器一样。

#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "agentty/domain/catalog.hpp"
#include "agentty/domain/complexity.hpp"
#include "agentty/domain/smart_tuning.hpp"

namespace agentty::smart {

// 三种执行角色。故意保持最小化（该设计认为 3 是
// 最佳点：越少是错误的经济，越多则难以管理）。
enum class ModelRole : std::uint8_t {
    Strategic,       // 规划、架构、硬推理——最佳模型、硬思考
    Implementation,  // 编写代码、机械 multi-file 编辑 — 有能力的中型模型
    Utility,         // grep/read/commit-msg/retrieval/summaries——最便宜的能力，没想到
};

// Title-case标签为UI。上面的role_label()
// 是LOG/wire的拼写("impl")；这是人类在 覆盖面板 中读到的内容。
// 保留在 枚举 旁边，因此添加角色可以使这两项义务同时可见。
[[nodiscard]] constexpr std::string_view role_display_name(ModelRole r) noexcept {
    switch (r) {
        case ModelRole::Strategic:      return "Strategic";
        case ModelRole::Implementation: return "Implementation";
        case ModelRole::Utility:        return "Utility";
    }
    return "Strategic";
}

[[nodiscard]] constexpr std::string_view role_label(ModelRole r) noexcept {
    switch (r) {
        case ModelRole::Strategic:      return "strategic";
        case ModelRole::Implementation: return "impl";
        case ModelRole::Utility:        return "utility";
    }
    return "strategic";
}

// 角色的PERSISTED拼写及其逆。
//
// 故意分开role_label()，这是一个UI缩写 （
// "impl"）选择适合状态栏。在磁盘上重用它显然会很整洁
// -up并会默默地打破每一个期待的读者"implementation"
// ——这正是促使输入该字段的根本原因（
// docs/STRONG_TYPES_AUDIT.md§发现2)。
//
// 这两者在结构上是相反的：role_wire_name是总
// 开关（因此-Wswitch捕获新的枚举器）并且
// role_from_wire_name精确地接受其发出的内容，加上历史拼写。
[[nodiscard]] constexpr std::string_view role_wire_name(ModelRole r) noexcept {
    switch (r) {
        case ModelRole::Strategic:      return "strategic";
        case ModelRole::Implementation: return "implementation";
        case ModelRole::Utility:        return "utility";
    }
    return "strategic";
}

// nullopt 对于不存在或无法识别的角色——"Smart Mode was off"和
// 此会话由更新版本生成，其中包含当前版本不认识的角色。
// 都被正确地呈现为"no role tag"而不是被猜测。
//
// "impl"被接受，因为role_label()总是
// 产生它并且某些东西可能保留了该拼写。
[[nodiscard]] inline std::optional<ModelRole>
role_from_wire_name(std::string_view s) noexcept {
    if (s == "strategic")      return ModelRole::Strategic;
    if (s == "implementation") return ModelRole::Implementation;
    if (s == "impl")           return ModelRole::Implementation;
    if (s == "utility")        return ModelRole::Utility;
    return std::nullopt;
}

// 角色运行所解析的（模型、成果）。 `model` 是 传输
// id（选择面板 标记已被 目录 助手剥离），可以安全地处理请求。
struct RoleProfile {
    std::string             model;
    Effort effort = Effort::None;
    // 当是呼叫者自己的NOT时，可以服务`model`的提供商。
    //
    // 空是常见情况并且意味着"dispatch where you already are"。
    // Non-empty仅发生在用户固定在另一个端点上的槽上：
    // 模型 ID 是端点-scoped，因此针对活动提供者重播
    // 它是 404 — 正是出于这个原因，解析器用于删除此类 pin。
    // 相反，命名提供者可以让 CAN 到达它的调用者（子代理运行者，
    // 通过 Config::stream_to）兑现该 PIN，
    // 而无法访问的调用者仍会退回。
    std::string             provider;

    [[nodiscard]] bool cross_provider() const noexcept {
        return !provider.empty();
    }
};

// Per-role 用户覆盖，保留在设置中。空的 `model` 表示
// 未设置时，根据目录自动填充。
// Effort::None 作为覆盖，与 "unset" 没有区别；角色的工作量为 auto-derived，
// 除非显式配置了整个槽（请参阅 SlotOverride）。
struct SlotOverride {
    std::string             model;    // "" = 自动
    Effort effort = Effort::None;
    bool                    set   = false;   // 一旦用户固定该插槽，则为 true
    // 制作该引脚的提供商。模型 id 仅对为其提供服务的端点有意义：固定在
    // Anthropic 上的 `claude-opus-4-5` 不是模型
    // Groq 或 Ollama 可以 数据流，并且将其分派到即时 400/404.
    // 选择面板 已经拒绝 SELECT 跨 -provider 行，
    // 但引脚 PERSISTS — 因此在 Anthropic 上创建的
    // 引脚默默地re-applied 在用户切换到另一个提供商后，在 选择面板
    // 的守卫从未看到的代码路径上。
    //
    // 记录提供者可以让 resolve_role 确定 pin 的范围，
    // 而无需对照 目录 验证它，这很重要：当 目录 是 空 或 STALE 时，
    // 仍然必须遵守 pin（参见 subagent_pin_test —
    // Copilot bug）。 Provider身份离线可知；
    // 目录 会员资格不是。
    //
    // 空意味着"unknown provenance" (在该字段存在之前写入
    // 的settings.json)并且在任何地方都受到尊重，保留旧的行为。
    std::string             provider;

    // 结构平等。子代理发布者 (app/subscribe.cpp)
    // 在其推送的配置上键入密钥，并且必须在路由器读取的字段
    // 发生更改时重新发布 - 哈希可能会发生冲突并默默地保持
    // 过期 路由器运行，而平等则不能。
    [[nodiscard]] bool operator==(const SlotOverride&) const = default;
};

struct RoleConfig {
    bool enabled = false;   // 智能模式总开关（默认关闭）

    // 用户在命令行上命名模型，因此每个角色都使用它。
    //
    // 智能模式回答"should agentty optimise cost for me"
    // ——偏好。这回答了 "is --model binding"——一份合同。
    // 它们是相同的交换机，这就是为什么打开智能模式 OFF 并没有停止
    // 路由：它反而落入了 auto-router 层，
    // 并且通过 `--model X` 的线束仍然得到压缩，并且 read
    // -only 子代理记入不同的模型（#70）。文档中没有任何内容表明 --model
    // 是一个提示。
    //
    // 在解析 --model 的任何地方进行设置，
    // 并通过 tuning::model_pinned() (257d566) 镜像到
    // CLI 和 TUI 配置站点——第一个版本仅在无头路径上设置它，
    // 因此 `agentty --model X` 交互地具有它要修复的相同错误。
    bool pinned_model = false;

    // 让角色在提供者OTHER上运行而不是活动的提供者。
    //
    // 默认为OFF，这个默认是有趣的部分。固定在另一个端点上的槽是可分派的—
    // —credentials::resolve()对任何提供者应答，
    // 而dispatch_stream()接受显式选择——所以旧的
    // "auth doesn't transfer"原因(
    // docs/design/smart-mode.md§9)已经过期。
    // 不 过期的是成本：提示词 缓存是 per-provider，因此跳跃端点
    // re-sends 的角色每轮都不会缓存其前缀。对于简短的阅读-only子代理这是一个公平的交易；
    // 对于一个健谈的角色，它的成本可能比更便宜的型号 保存
    // 更高。
    //
    // 因此，这是用户做出的选择，而不是为他们推断的选择——
    // 并且在关闭该选择后，外部 pin 的行为与之前完全相同：
    // 被忽略以支持该提供商自己的 目录，从未作为 404 分派。
    bool cross_provider_roles = false;

    // 开发者逃生舱口(AGENTTY_SMART_NO_INTERNAL/_NO_ORCHESTRATE/_NO_SUBAGENTS)，
    // 由settings::registry::apply_env填充了环境覆盖。
    // 从来没有坚持过，从来没有在UI。
    bool no_internal    = false;
    bool no_orchestrate = false;
    bool no_subagents   = false;

    // ONE决定，三个时隙。这里以前还有七个切换。
    //
    // 其中三个（内部路由、编排、子代理路由）现在是FOLDED IN：它们
    // 就是智能模式*意味着*，并且没有用户合理地关闭它们。
    // 将压缩摘要交给旗舰模型，而非低成本模型。
    // 并不是一个值得争论的偏好——它严格来说更贵，
    // 但没有任何好处。只有当合理的用户合理地选择任
    // 一方式时，切换 才能赢得一席之地；这些没有。
    // Debug/bisect逃生舱口存在于环境变量（上面的no_*字段）中，
    // 而不是在UI中。
    //
    // 其中四个（学习路由、结果反馈、推测预热、
    // 计划召回）是 self-supervised 反馈循环，
    // 其从 per-workspace 持久状态突变路由。
    // 它们从未根据固定策略进行衡量，并且每个都带有正确
    // 性表面（两个状态文件、遗憾分母、衰减时间表）加上用户
    // 想知道他们做什么的认知成本。已删除。四个不可衡量的
    // 反馈循环是产品对冲，而不是意见。
    SlotOverride strategic;
    SlotOverride implementation;
    SlotOverride utility;

    // 当设置为 已加载 时，数字路由策略，RESOLVED（
    // 环境覆盖，否则为持久设置，否则为出厂默认设置）。
    //
    // 这些存在于此处，而不是在使用时从环境中读取，因为它们现在是 user
    // -configurable：埋在分类器中的 getenv() 无法看到
    // 用户在设置 UI 中设置的值，并且将整个 store::Settings
    // 线程到纯域代码中以达到三个整数会更糟。 RoleConfig 已经是
    // 用户的 Smart Mode 配置。
    // 已经在 Model::Domain 中，并且已经在每个需要这些
    // 的站点的范围内 - 所以这就是它们所属的地方。
    //
    // 默认镜像smart::tuning::kDefault*；请参阅 settings_registry.hpp
    // 了解使两者保持同步的 static_assert。
    int deep_margin       = tuning::kDeepMarginDefault;
    int bias_clamp        = tuning::kBiasClampDefault;
    int complex_threshold = tuning::kComplexDefault;

    // ── 主要 -turn 角色底线 ──────────────────────────────────────
    //
    // 主会话轮可以运行的CHEAPEST角色。该回合的分类复杂
    // 性选择一个角色（参见role_for_complexity）；
    // 这限制了镐允许向下移动的距离。
    //
    // 为什么会存在这种情况。智能模式用于将主要转弯路由为战略
    // UNCONDITIONALLY — 仅当模型选择通过
    // `task` 工具生成子代理时才能访问实施和实用程序。
    // 用户的 577-turn 跟踪显示了结果：在 501 轮上
    // 角色=战略，在 76 轮上角色=无，实现和实用程序实际上从不，
    // 以及 ZERO 任务 调用。与此同时，分类器正确地将其中
    // 142 个转弯 (25%) 识别为琐碎或简单。这是正确的，
    // 没有什么可以对其采取任何行动：两个 Ollama 帐户在
    // 一周内被烧毁，每一次琐碎的转弯都以旗舰费率计费，
    // 并且没有环境变量可以改变它，因为该角色在 分派 路径中是一个常量。
    //
    // 这里的策略准确地恢复旧的行为，这就是为什么
    // 它是默认的NOT：旧的行为是错误。
    // 实用程序是默认设置 - 完整的阶梯，
    // 每一层都路由到分类器所属的位置。想要旗舰机看到
    // 所有内容的用户故意设置“策略”并恢复之前的语义。
    //
    // 存储为 传输 NAME 而不是 枚举，因此它利用现有的设置机制（
    // Type::Enum 绑定 std::string 成员），
    // 因此 settings.json 值是 self-describing。
    // 下面的main_turn_floor_role()是读取的带类型的；无法识别的字符
    // 串会回退到实用程序，这会阻止 hand-edited 设置文件默默地禁用路由。
    std::string main_turn_floor = "utility";

    [[nodiscard]] ModelRole main_turn_floor_role() const noexcept {
        if (auto r = role_from_wire_name(main_turn_floor)) return *r;
        return ModelRole::Utility;
    }

    // 主要回合的角色是否完全遵循分类器。关闭使每个主要转弯都具有战略性，
    // 无论上面的楼层如何 - 提供相同的逃生舱口
    // main_turn_floor=战略性，保持独立，
    // 因此设置 UI 可以将 "don't route my main turn"
    // 表示为开关，而不是用户必须推理的楼层。
    bool route_main_turn = true;

    // 给定层现在是否处于活动状态。这三个只是主开关（保留为命名访问器，
    // 因此 调用 站点读取为意图 - "is orchestration on"
    // - 而不是一个裸布尔，因此环境逃生舱口
    // 只有一个可以应用的位置）。
    [[nodiscard]] bool internal_routing() const noexcept;
    [[nodiscard]] bool orchestration()    const noexcept;
    [[nodiscard]] bool subagent_routing() const noexcept;

    [[nodiscard]] const SlotOverride& slot(ModelRole r) const noexcept {
        switch (r) {
            case ModelRole::Strategic:      return strategic;
            case ModelRole::Implementation: return implementation;
            case ModelRole::Utility:        return utility;
        }
        return strategic;
    }

    // 可变过载，因此WRITERS经历与读者相同的角色->字段映射，
    // 而不是re-deriving它。 clear-slot 处理函数
    // 用于通过一系列索引比较来选择字段，其尾随的 else 捕获每个
    // out-of-range 光标并重置实用程序。
    [[nodiscard]] SlotOverride& slot(ModelRole r) noexcept {
        return const_cast<SlotOverride&>(
            static_cast<const RoleConfig&>(*this).slot(r));
    }

    // 结构平等，出于与 SlotOverride 相同的
    // 原因：子代理发布者在该变化时准确地重新发布。
    [[nodiscard]] bool operator==(const RoleConfig&) const = default;
};

// 层门：主开关减去显影剂逃生舱口。
inline bool RoleConfig::internal_routing() const noexcept {
    return enabled && !no_internal;
}
inline bool RoleConfig::orchestration() const noexcept {
    return enabled && !no_orchestrate;
}
inline bool RoleConfig::subagent_routing() const noexcept {
    return enabled && !no_subagents;
}

// ── 复杂性 角色 ──────────────────────────────────────────
//
// 哪个角色应该轮流分类器在这一层评分。 一个 总功能 — 梯形图
// 在此声明一次，而不是在 分派 站点上以 if-chain 形式表示，
// 因此 "what runs a Simple turn"
// 有一个答案，路由器、设置 UI 和测试都会读取。
//
//   简单 → 实用程序 "yes"、"run it"、
//   "commit" 简单 → 实用程序有针对性的查找、
//   一个 -line 修复 标准 → 实现
//   默认工作轮 复杂 → 战略架构、调试、"why"
//
// 标准故意映射到实施而不是战略：它是 CONSERVATIVE
// FALLBACK 层（此处是不可分类的转向），
// 而 "write the code I just described"
// 正是实施角色的用途。在引发此问题的跟踪中，标准版为 183
// 圈（占会话的 32%），因此默认情况下将其发送到旗舰版是大部分支出，
// 至少出于最少的原因。
[[nodiscard]] constexpr ModelRole role_for_complexity(Complexity c) noexcept {
    switch (c) {
        case Complexity::Trivial:  return ModelRole::Utility;
        case Complexity::Simple:   return ModelRole::Utility;
        case Complexity::Standard: return ModelRole::Implementation;
        case Complexity::Complex:  return ModelRole::Strategic;
    }
    return ModelRole::Strategic;   // 不可达；失败昂贵，不便宜
}

// MAIN轮的角色应该作为：分类器的选择，被夹紧，
// 因此它永远不会比用户的楼层便宜，并且当main
// -turn路由关闭时固定到战略。
//
// 钳制是角色的ORDER上的max()，这就是为什么枚举被声明为最便宜的-last:
// 战略(0)<实施(1)<效用(2)，
// 因此"no cheaper than the floor"是基础值上的min(pick，floor)。
static_assert(static_cast<std::uint8_t>(ModelRole::Strategic) <
                  static_cast<std::uint8_t>(ModelRole::Implementation) &&
              static_cast<std::uint8_t>(ModelRole::Implementation) <
                  static_cast<std::uint8_t>(ModelRole::Utility),
              "main_turn_role clamps by comparing the enum's underlying "
              "value, so ModelRole must stay ordered strongest-first. "
              "Reordering it silently inverts the floor.");

[[nodiscard]] constexpr ModelRole main_turn_role(
        Complexity c, ModelRole floor, bool route) noexcept {
    if (!route) return ModelRole::Strategic;
    const auto pick = static_cast<std::uint8_t>(role_for_complexity(c));
    const auto lim  = static_cast<std::uint8_t>(floor);
    return static_cast<ModelRole>(pick < lim ? pick : lim);
}

namespace detail {

// 该提供商上最强的MID-tier候选者（用于实施）：最有能力的模型是NOT
// 旗舰-priced。重用 model_picker_less（层级描述，
// 然后是最新的）仅限于 Mid，并拒绝非 -dispatchable/
// 弱/no-tools 资产。
//
// MID层中的最强模型，或者——当不存在中间模型时——
// 严格地BELOW父层中的最强模型。
//
// 回退是"Implementation never de-escalates"的修复。
// 这过去要求 tier == Mid 完全一致，否则返回空，
// 并且调用者将空读取为 "route to the parent"。
// 因此，带有旗舰父级且其下只有廉价型号的 目录（在定制主机上很常见：
// 一个大型号加上几个小型号，14-69B 频段中没有）完全具有
// NO 可到达的 de-escalation 路径 — 每轮实现都在
// 旗舰上默默运行，这就是一周的工作以最高费率计费的方式。
//
// `parent_model`使得回退安全：我们只
// 考虑严格低于父级的层中的模型，因此这永远不能路由实现UP，
// 即不变量 resolve_role文档。
[[nodiscard]] inline std::string strongest_mid(
        const std::vector<ModelInfo>& candidates,
        std::string_view parent_model = {}) {
    using Tier = ModelCapabilities::Tier;
    const ModelInfo* best = nullptr;
    for (const auto& mi : candidates) {
        const std::string_view id = mi.id.value;
        if (!mi.tools_allowed()) continue;
        if (!is_dispatchable_model(id)) continue;
        if (ModelCapabilities::tier_for(id) != Tier::Mid) continue;
        if (!best || model_picker_less(mi, *best)) best = &mi;
    }
    if (best) return wire_model_id(std::string_view{best->id.value});
    if (parent_model.empty()) return std::string{};

    // 无中间层。选择 任意 级别中低于父级的最强型号；
    // 一次性降级的廉价型号仍然是真正的 de-escalation，
    // 并且毫无疑问比向旗舰机收取机械编辑费用要好。
    // 弱模型仍然被排除在外 - 工作为 multi-file
    // 工具工作的角色无法在泄漏工具 JSON 的模型上运行。
    const Tier parent_tier = ModelCapabilities::tier_for(parent_model);
    for (const auto& mi : candidates) {
        const std::string_view id = mi.id.value;
        if (!mi.tools_allowed()) continue;
        if (!is_dispatchable_model(id)) continue;
        const Tier t = ModelCapabilities::tier_for(id);
        if (t == Tier::Weak) continue;
        if (t >= parent_tier) continue;         // 永不路由实现向上
        if (!best || model_picker_less(mi, *best)) best = &mi;
    }
    return best ? wire_model_id(std::string_view{best->id.value}) : std::string{};
}

// 步骤`n`沿着THE MODEL的OWN努力阶梯放置：关闭，则恰好是该模型
// 接受的ON水平，上升。 该 单步原语 - 头文件 中的每个
// complexity/cascade/step-down 调整都会经过它，
// 因此 "what is one step harder" 每个模型都有一个答案。
//
// 这走的是模型的阶梯而不是固定的全局阶梯，
// 因为固定的阶梯错误两次：
//
//   1.省略了Minimal。 `minimal` 是 gpt-5+ 的 BOTTOM 推理层，所以
//      gpt-5上的用户以最小的努力使`step(minimal, 0)`
//      从梯子上掉下来到索引0==无——智能模式在每个标准转弯上
//      默默地转弯REASONING OFF，并且简单转弯也做同样的事情。
//      整个工作就是扩展推理的功能正在禁用它。
//   2.它步进了模型没有的梯级。在 Claude 型号上
//      (没有`minimal`)从低位下降到Minimal，
//      然后clamp_effort将UP快速恢复到低位——
//      因此实现从未真正从低位父级下降。
//
// 行走effort_set_of(caps)修复了两者：梯子
// IS能力数据，因此一步只能落在模型将接受的水平上，
// 并且结果不需要post-hoc钳制。异构性是数据，而不是代码路径。
[[nodiscard]] inline Effort effort_step(
        Effort e, int n, const ModelCapabilities& caps) noexcept {
    Effort ladder[7];
    int nl = 0;
    ladder[nl++] = Effort::None;            // "off"是一个梯级，除非模型总是认为
    if (always_reasons(caps)) nl = 0;
    const std::uint8_t set = effort_set_of(caps);
    for (Effort lv : {Effort::Minimal, Effort::Low, Effort::Medium,
                      Effort::High, Effort::Xhigh, Effort::Max})
        if (set & effort_bit(lv)) ladder[nl++] = lv;

    // 在步进之前将 stale/unsupported 请求捕捉到 真实
    // 梯级上，因此该步长是从模型实际所在的位置测量的。
    const Effort start = nearest_effort(e, set);
    int idx = 0;
    for (int i = 0; i < nl; ++i) if (ladder[i] == start) { idx = i; break; }
    idx += n;
    if (idx < 0)   idx = 0;
    if (idx >= nl) idx = nl - 1;
    return ladder[idx];
}

// 在模型的阶梯上从`e`迈出一步DOWN的努力。
// Impl 认为，用于从用户的战略工作中得出实施工作，但要少一些。
// 已锁定 — effort_step 只能返回受支持的
// 级别。
[[nodiscard]] inline Effort effort_step_down(
        Effort e, const ModelCapabilities& caps) {
    return effort_step(e, -1, caps);
}

} // namespace detail

// 通过回合的复杂性来缩放基本推理工作，应用级联校正`bias`(在
// 工作-steps中；循环对本次会话启发式是否为under/over-rating的运行估计)，
// 然后钳制到模型支持的内容。
// 研究杠杆"scale effort to query complexity"
// 加上路由调查比one-shot路由更喜欢的级联：复杂的转弯认为比基
// 线更进一步HARDER，平凡的认为NONE，简单的一步下降；
// `bias` 通过循环的反馈微移结果 up/down。
// 模糊性的偏差是向上的——在 -thinking 下，硬转向的成本不仅仅是
// 在简单转向上浪费一点预算。
[[nodiscard]] inline Effort effort_for_complexity(
        Effort base, Complexity c, const ModelCapabilities& caps, int bias = 0) {
    using E = Effort;
    // 一种步进原语，行走THIS模型的梯子(参见effort_step)。
    auto step = [&](E e, int n) { return detail::effort_step(e, n, caps); };
    E scaled;
    switch (c) {
        case Complexity::Trivial:  scaled = E::None;       break;
        case Complexity::Simple:   scaled = step(base, -1); break;
        case Complexity::Standard: scaled = base;          break;
        case Complexity::Complex:  scaled = step(base, +1); break;
        default:                   scaled = base;          break;
    }
    // 无论正偏差如何，琐碎的事情都保持无——ack就是ack。
    if (c != Complexity::Trivial && bias != 0) scaled = step(scaled, bias);
    return clamp_effort(scaled, caps);
}

// CONTINUOUS努力缩放。 effort_for_complexity 仅使用离散层，
// 因此几乎没有进入复杂的转弯和 OVERWHELMINGLY 复杂的转弯都
// 恰好获得+1 - 分类器的 score/margin 在最后一英里被丢弃。
// 该过载将层步骤保持为骨干（在层边界处的相同输出），
// 但根据 DEEP 转弯在其带中的位置添加了
// 部分细化：
//   ·复杂内部深处（大边距）→额外的+1步骤（达到
//     有效+2旧路径需要级联偏置才能命中），
//   ·深层简单（大边距）→额外的-1步（明确的ack-ish
//     one-liner比边界-Simple转弯下降得更远），
//   ·标准和浅带→正是层行为。
// 乐谱中的单调，仍然固定于模型，Trivial 仍然固定于 None。
// `deep`是频带被认为饱和的裕度。
[[nodiscard]] inline Effort effort_for_score(
        Effort base, const ComplexityScore& cx, const ModelCapabilities& caps,
        int bias = 0, int deep = tuning::kDeepMarginDefault) {
    using E = Effort;
    // 与effort_for_complexity相同的单个基
    // 元——层边界处的相同输出是共享步进器的属性，而不是巧合。
    auto step = [&](E e, int n) { return detail::effort_step(e, n, caps); };
    if (cx.tier == Complexity::Trivial) return clamp_effort(E::None, caps);

    // 饱和裕度是PARAMETER，而不是getenv()读取。
    // 现在是用户可配置的，从此处的环境读取的值无法看到用户在
    // UI 中更改的设置。持有RoleConfig通行证
    // `smart.deep_margin`的呼叫者；默认情况下，
    // 所有其他调用者（包括测试）都可以像以前一样进行编译和行为。
    const int kDeep = deep;
    int extra = 0;
    if (cx.tier == Complexity::Complex && cx.margin >= kDeep) extra = +1;
    else if (cx.tier == Complexity::Simple && cx.margin >= kDeep) extra = -1;

    // 层骨干(匹配effort_for_complexity)，然后分数细化，
    // 然后级联偏置，然后钳位。
    int tier_step = (cx.tier == Complexity::Complex) ? +1
                  : (cx.tier == Complexity::Simple)  ? -1 : 0;
    E scaled = step(base, tier_step + extra);
    if (bias != 0) scaled = step(scaled, bias);
    return clamp_effort(scaled, caps);
}

// 将角色解析为它应该运行的（模型、成果）。
//
//   角色 此 调用 的执行角色
//   parent_model 用户为主轮选择的模型（传输 或 选择面板
//   id） parent_effort 用户在主轮
//   候选上的努力设置 活动提供者的模型 目录
//   cfg per-role 覆盖 + 启用的 标志
//
// 当cfg.enabled为假时，每个角色解析为(parent_model，
// parent_effort)——智能模式关闭是纯粹的pass-through。
// 启用后，用户固定的插槽获胜；否则，zero-config auto-fill 适用。
// 出于成本原因，解析器 绝不 将角色 UP 路由到父层；战略是父级（或
// user-pinned 更强的模型）。
[[nodiscard]] inline RoleProfile resolve_role(
        ModelRole role,
        std::string_view parent_model,
        Effort parent_effort_in,
        const std::vector<ModelInfo>& candidates,
        const RoleConfig& cfg,
        std::string_view active_provider = {}) {
    const std::string parent_wire = wire_model_id(parent_model);
    // THE为主转分辨率点。自动意味着 "no choice yet"，
    // 它解析的内容取决于手头的模型 - 因此在任何步进或夹紧之前，
    // 它会根据父项的上限解析为 此处。下面的每一行（以及
    // effort_for_complexity 下游）都在真实的梯级上工作，
    // 并且永远不需要知道 Auto 的存在。
    const Effort parent_effort =
        resolve_auto_effort(parent_effort_in, resolved_caps(parent_wire));
    RoleProfile pass{parent_wire,
                     clamp_effort(parent_effort,
                                  resolved_caps(parent_wire))};
    if (!cfg.enabled) return pass;

    // 0. PINNED 模型优先于其他任何模型，包括显式模型
    //    插槽。用户在命令行中命名它；这是一个合同，而不是一个提示（
    //    #70）。
    //
    //    此检查已添加到此处的 role_model() 和 MISSED，这使得在最重要的
    //    路径中修复了一半的错误：resolve_role 是 MAIN TURN 经历
    //    的内容（cmd_factory 的 resolve_turn_routing）
    //    以及第 3b 层用于子代理角色的内容。 main_turn_role() 可以
    //    作为实现或实用程序获得一个简单的转弯，因此 `--model opus` 加上
    //    智能模式仍然回答了十四行诗或俳句的简单转弯 - 原始报告 (
    //    请求仍发给与选中模型不同的其他模型。
    //    保留了智能模式。
    //
    //    返回 `pass`，它已经以其固定的努力承载父模型，
    //    因此固定运行也保留了用户的努力。
    if (cfg.pinned_model) return pass;

    // 1.此插槽的显式用户覆盖完全获胜——但仅在以下情况下
    //    其所固定的提供者。模型 ID 是端点-scoped，
    //    因此针对 Groq 重放 Anthropic 引脚会调度
    //    一个提供商从未听说过的 ID（每次转弯或每次委托都会立即出现
    //    400/404，直到用户找到智能模式 覆盖面板）。
    //
    //    故意对 不 进行 目录-membership 检查：引脚必须在空或
    //    静止的 -loading 目录 中幸存下来（
    //    subagent_pin_test/副驾驶错误）。 Provider 身份
    //    离线已知；会员资格不是。未知的 -provenance 密码（-upgrade
    //    之前的设置）和未知的活动提供者（未通过密码的调用者）都会受到尊重
    //    - 惊喜比以前严格减少，永远不会更多。
    if (const auto& ov = cfg.slot(role); ov.set && !ov.model.empty()) {
        const bool provider_matches =
            active_provider.empty() || ov.provider.empty()
            || ov.provider == active_provider;
        const std::string wire = wire_model_id(std::string_view{ov.model});
        if (provider_matches)
            return RoleProfile{wire,
                               clamp_effort(ov.effort, resolved_caps(wire)),
                               {}};
        // 外部pin：NAME其所属的提供商并让呼叫者决定。
        // 一个可以 分派 存在的人（子代理运行者，
        // 通过 Config::stream_to）尊重该引脚；
        // 无法检查 cross_provider() 并回退到下面的 auto
        // -fill，这是每个调用者过去无条件获得的。
        //
        // 该努力是销钉自己的，夹在被销钉的模型上——
        // 而不是父母的。努力是要运行的模型的一个属性。
        if (cfg.cross_provider_roles)
            return RoleProfile{wire,
                               clamp_effort(ov.effort, resolved_caps(wire)),
                               ov.provider};
        // Cross-provider路由关闭：行为与之前完全
        // 相同并且排名高于THIS提供商的目录。该引脚未清除 -
        // 切换回来并再次应用。
    }

    // 2. Zero-config auto-fill 来自 目录。
    switch (role) {
        case ModelRole::Strategic:
            // 父模型，至少与用户设置一样努力思考。
            return pass;

        case ModelRole::Implementation: {
            std::string mid = detail::strongest_mid(candidates, parent_wire);
            if (mid.empty()) return pass;   // 父级以下没有任何内容 → 父级
            const auto caps = resolved_caps(mid);
            return RoleProfile{std::move(mid),
                               detail::effort_step_down(parent_effort, caps)};
        }

        case ModelRole::Utility: {
            // 最便宜的模型，没有推理预算——繁重的工作。
            std::string cheap =
                cheapest_capable_model(parent_wire, candidates,
                                       ModelCapabilities::Tier::Cheap);
            return RoleProfile{std::move(cheap), Effort::None};
        }
    }
    return pass;
}

// Subagent-context角色解析。相同的模型路由到
// resolve_role，但返回的推理工作是 HARD
// -CLAMPED 到父代：工作线程 绝不能比生成它的回合思考得更
// 努力（不变量：子代理工作≤父代工作；当父代的工作关闭时，
// 每个 工作线程 的工作也关闭）。这将 不变量 保留在解析器中，
// 而不是依赖于 调用 站点从不连接 req.effort — 映射到战略
// 的 reviewer/coder 角色（在父工作中返回父模型）将在
// 调用者读取 .effort 时继承完整的父工作。
[[nodiscard]] inline RoleProfile resolve_subagent_role(
        ModelRole role,
        std::string_view parent_model,
        Effort parent_effort,
        const std::vector<ModelInfo>& candidates,
        const RoleConfig& cfg,
        std::string_view active_provider = {}) {
    RoleProfile p = resolve_role(role, parent_model, parent_effort,
                                 candidates, cfg, active_provider);
    // 有序努力量表上的min(已解决，父级)。
    if (static_cast<int>(p.effort) > static_cast<int>(parent_effort))
        p.effort = parent_effort;
    return p;
}

// ── 每个路由器都问到的问题 ──────────────────────────────────
//
// 此角色允许使用哪些模型？
// 三个站点用于独立地回答它——压缩、read-only子代理和公用设施轮流
// ——每个站点以自己的方式咨询路由策略。三个决策点做一个决策，
// 这就是 `--model X` 最终在主要转弯和其他地方提供咨询
// 的 绑定 的原因 (#70)。
//
// `floor`是角色实际需要的能力：用于摘要（文本输入、
// 文本输出）的廉价，用于read-only探索的中等（决定40个
// grep命中中的哪一个重要不是机械工作）。
//
// 返回WIRE模型ID。
// 将角色解析为（模型、提供者）目标。
//
// 下面的 role_model() 的长形式，对于可以 分派 到除他们
// 自己之外的提供商的呼叫者而言。其他一切都不断调用 role_model
// () 并单独获取模型。
//
// 这里没有设置努力：其所服务的内部角色（压缩、标题、
// read-only探索）都在努力关闭的情况下运行。
[[nodiscard]] inline RoleProfile role_target(
        std::string_view parent_model,
        const std::vector<ModelInfo>& candidates,
        const RoleConfig& cfg,
        ModelCapabilities::Tier floor,
        const SlotOverride* slot = nullptr,
        std::string_view active_provider = {}) {
    const std::string parent = wire_model_id(parent_model);

    // 固定模型完全获胜。用户在命令行中命名它；
    // 没有成本启发法比这更重要。
    if (cfg.pinned_model) return {parent, Effort::None, {}};

    // AGENTTY_SMART_NO_INTERNAL=1被记录为
    // 压缩与标题生成继续使用主模型。
    // 因此它也必须停止层回退——它用于仅门控下面的槽，这使得降级运行并使
    // 承诺为假(#70)。
    if (slot == &cfg.utility && cfg.no_internal)
        return {parent, Effort::None, {}};

    // 接下来是用户明确设置的智能模式槽。
    if (slot && cfg.internal_routing() && slot->set && !slot->model.empty()) {
        const bool same_provider = active_provider.empty()
                                || slot->provider.empty()
                                || slot->provider == active_provider;
        if (same_provider)
            return {wire_model_id(std::string_view{slot->model}),
                    Effort::None, {}};
        // 固定在 ANOTHER 提供者上：对其进行命名，以便能够到达
        // 该端点的调用者认可该 pin。门控在与 resolve_role
        // 相同的 opt-in 上 - 一个 交换机为每个角色决定这一点，
        // 而不是两个解析器在是否允许跳跃方面存在分歧。
        if (cfg.cross_provider_roles)
            return {wire_model_id(std::string_view{slot->model}),
                    Effort::None, slot->provider};
        // 关：失败并排名高于该提供商自己的目录。
    }

    // 否则，就选择最便宜的型号来清理地板。
    // 从不路由UP，因此单个-model账户看不到任何变化。
    return {cheapest_capable_model(parent, candidates, floor),
            Effort::None, {}};
}

[[nodiscard]] inline std::string role_model(
        std::string_view parent_model,
        const std::vector<ModelInfo>& candidates,
        const RoleConfig& cfg,
        ModelCapabilities::Tier floor,
        const SlotOverride* slot = nullptr,
        std::string_view active_provider = {}) {
    auto t = role_target(parent_model, candidates, cfg, floor, slot,
                         active_provider);
    // cross-provider目标不能由仅具有一个端点的调用者分派，
    // 因此它降级为旧的答案：回退，就好像槽被未
    // 设置一样。该函数一直具有相同的行为。
    if (t.cross_provider())
        return cheapest_capable_model(wire_model_id(parent_model), candidates,
                                      floor);
    return std::move(t.model);
}

// 实用轮次：压缩摘要、线程标题、HyDE扩展。发短信，
// 发短信，所以廉价层就足够了。
[[nodiscard]] inline std::string utility_model(
        std::string_view parent_model,
        const std::vector<ModelInfo>& candidates,
        const RoleConfig& cfg,
        std::string_view active_provider = {}) {
    return role_model(parent_model, candidates, cfg,
                      ModelCapabilities::Tier::Cheap, &cfg.utility,
                      active_provider);
}

// ── 智能模式 覆盖面板 排行,作为 TYPE ────────────────────────────
//
// 一行是主交换机或三个角色插槽之一。这是整个域，
// 因此它被拼写为 枚举，而不是某人必须记住其形状
// 的列表中的 int 索引。
//
// int 不是一个小风格问题。 覆盖面板 从十一排减少到四排； 渲染器紧随其后，
// 其他四个地方则没有。光标以 11 为模换行（因此 ↑/↓ 遍历了
// 从未绘制的七行），两个 slot-assign 在 `8 + slot`
// （旧插槽行）处返回 re-opened，并且clear-slot
// 处理函数 将每个索引 ≥ 3 映射到实用程序 — 因此 out
// -of-range 游标上的 `x` 会默默地重置用户不在的插槽。
// 这些中的每一个都是可表示的，因为类型表示"any integer"，
// 而只有四个值是合法的，并且布局在每个站点处
// 重复而不是被陈述一次。
//
// 对于这个枚举：没有第八行可命名，`next`/`prev`
// 不能离开枚举，并且槽行通过总函数不进行算术地映射到ModelRole。
// bug 类消失了，没有修补。
enum class OverlayRow : std::uint8_t {
    Master,          // on/off开关
    Strategic,       // ─┐
    Implementation,  //  ├─ 按显示顺序排列的三个角色槽位
    Utility,         // ─┘
};

// 显示顺序。 ONE处写下布局；下面的每一次
// 遍历和每一次转换都源自它。
inline constexpr OverlayRow kOverlayRows[] = {
    OverlayRow::Master,
    OverlayRow::Strategic,
    OverlayRow::Implementation,
    OverlayRow::Utility,
};
inline constexpr int kSmartModeRows =
    static_cast<int>(sizeof(kOverlayRows) / sizeof(kOverlayRows[0]));

// 这一行是模型槽之一吗？ （i.e。它有ModelRole吗）
[[nodiscard]] constexpr bool is_slot_row(OverlayRow r) noexcept {
    return r != OverlayRow::Master;
}

// 槽位行配置的角色。返回主开关的 nullopt，因此调用者不能默默
// 地将 "not a slot" 视为插槽 - 这正是旧的尾随
// `else` 从 out-of-range 游标重置实用程序的方式。
[[nodiscard]] constexpr std::optional<ModelRole> role_of(OverlayRow r) noexcept {
    switch (r) {
        case OverlayRow::Master:         return std::nullopt;
        case OverlayRow::Strategic:      return ModelRole::Strategic;
        case OverlayRow::Implementation: return ModelRole::Implementation;
        case OverlayRow::Utility:        return ModelRole::Utility;
    }
    return std::nullopt;
}

// 配置给定角色的行——role_of的逆。总计，
// 因此 "re-open the overlay on the slot I just set" 不需要 `1 + slot` 算术。
[[nodiscard]] constexpr OverlayRow row_of(ModelRole r) noexcept {
    switch (r) {
        case ModelRole::Strategic:      return OverlayRow::Strategic;
        case ModelRole::Implementation: return OverlayRow::Implementation;
        case ModelRole::Utility:        return OverlayRow::Utility;
    }
    return OverlayRow::Strategic;
}

// 光标移动，在枚举上闭合：换行是类型的属性，
// 因此没有调用位点可以通过错误的模数换行。
[[nodiscard]] constexpr OverlayRow next_row(OverlayRow r, int delta) noexcept {
    const int n = kSmartModeRows;
    const int i = ((static_cast<int>(r) + delta) % n + n) % n;
    return kOverlayRows[i];
}

} // namespace agentty::smart
