#pragma once
// agentty::smart::tuning —用于智能模式的高级数字旋钮。
//
// 智能模式FEATURE 切换（其层运行）存在于Ctrl+S 覆盖
// 面板中并持续到settings.json。 此 文件是下面的层：
// 高级用户可能合法地想要重新调整其工作流程的少数 NUMERIC 策略常量，
// 以与 AGENTTY_RAG_* 旋钮相同的方式公开为环境变量
// （由 settings::registry::read_env
// 在启动时读取一次，限制在安全范围内，取消设置 ⇒ 持久值）。
// 只有 解析器 住在这里：该域不读取环境。
//
// 仅暴露正品POLICY。如果更改（签名哈希空间 +
// FNV 种子、存储压缩阈值、各个分类器特征权重），
// 会破坏存储数据或破坏 不变量 的实现内部结构故意不是
// — 层 THRESHOLDS 是分类的正确控制面，
// 而不是每个 -feature 权重 15 个繁琐的控制面。
//
//   AGENTTY_SMART_DEEP_MARGIN(整数，
//       默认3，范围1..8)分类器-score余量，在该余量处转弯在其
//       带中是"deep"并且获得额外的连续努力步骤。
//       降低⇒连续缩放更多的是提前加载到add/drop的额外步骤；
//       更高 ⇒ 它保持接近离散层行为。
//
//   AGENTTY_SMART_BIAS_CLAMP (整数，
//       默认2，范围1..4)会话级联努力偏差上的对称钳制（±N步）。
//       限制本次训练的 self-correction 的努力偏离基线的程度。
//
//   AGENTTY_SMART_COMPLEX_THRESHOLD（整数，
//       默认 3，范围 1..8）Feature-score at/above，
//       回合分类为复杂。降低⇒更多回合升级为复杂（更多推理，更多成本）；更高⇒更少。
//       Simple/Standard边界跟踪它（标准是正下方的带）。
//
//   AGENTTY_SMART_MODE（0 或 1，
//       未设置 ⇒ settings.json） 智能模式主开关的
//       SESSION 覆盖。 1 强制打开，0 强制关闭，
//       仅适用于 此 进程 — 持久设置既不会被读取为事实来源，也不会被覆盖
//       （当覆盖处于活动状态时，持久会跳过该字段，并且 ^S 覆盖面板
//       显示引脚）。
//       对于脚本化运行（CI、基准测试、平分）很有用，
//       在这种情况下您需要确定性路由而不触及用户的配置。

#include <algorithm>
#include <optional>
#include <string>

namespace agentty::smart::tuning {

namespace detail {

// 解析 旋钮的值，被夹紧。 nullopt 表示未设置或格式错误：
// 对 shell 配置文件中的拼写错误最不令人惊讶的响应是让配置
// 的值保持不变。
inline std::optional<int> parse_int(const char* v, int lo, int hi) noexcept {
    if (v && v[0]) {
        try { return std::clamp(std::stoi(v), lo, hi); } catch (...) {}
    }
    return std::nullopt;
}

} // namespace detail

// ── 已运出的价值, 命名 ───────────────────────────────────
// 这些是默认值和强制范围的 一个 真实来源。
// 它们是每个访问器内的文字，这很好，而 env 是唯一的
// 入口点 - 但设置 注册表 现在带有与具有自己的边界
// 的持久行相同的三个旋钮，并且必须一致的两个 hand
// -copied 数字正是 注册表 存在要防止的漂移。
// settings_registry.hpp static_asserts其行反对这些。
inline constexpr int kDeepMarginDefault = 3, kDeepMarginMin = 1, kDeepMarginMax = 8;
inline constexpr int kBiasClampDefault  = 2, kBiasClampMin  = 1, kBiasClampMax  = 4;
inline constexpr int kComplexDefault    = 3, kComplexMin    = 1, kComplexMax    = 8;

// 智能模式主交换机的会话覆盖。 nullopt = 不覆盖（settings.json
// 管辖）； true/false = 已固定此进程。除空之外的任何值都视为
// 打开 - 因此 =1、=true、=yes 都有效 - 并且 "0"
// /"false"/"off"/"no" 为关闭。 一个 环境名称：
// 第二个拼写是第二个事实来源，而 "which one wins" 是用户不
// 应该问的问题。
[[nodiscard]] inline std::optional<bool> parse_enabled(const char* v) noexcept {
    if (!v || !v[0]) return std::nullopt;
    const std::string s{v};
    return !(s == "0" || s == "false" || s == "off" || s == "no");
}

// ── 人数 -layer 逃生舱门 ────────────────────────────────────
// 智能模式的三种行为曾经是user-facing 切换。它们现在被折叠
// 到主交换机中——没有人在编排关闭的情况下合理地运行智能模式——但每个都
// 保留 NEGATIVE 环境覆盖，以便开发人员可以平分路由错误
// 这属于编排逻辑，还是子代理路由？
// 而无需编辑设置或重建。
//
// 故意env-only并故意否定：默认开启，
// 覆盖是逃生舱口，并且UI中的逃生舱口只是具有
// 额外步骤的切换。
//
//   AGENTTY_SMART_NO_INTERNAL=1 compaction/titles 停留在主
//   模型上 AGENTTY_SMART_NO_ORCHESTRATE=1 主转停留在所选模型上
//   AGENTTY_SMART_NO_SUBAGENTS=1 工作线程 使用层 auto-router
//
// 这些值达到RoleConfig::no_*到settings::registry::read_env。
[[nodiscard]] inline bool parse_disabled(const char* v) noexcept {
    if (!v || !v[0]) return false;
    const std::string s{v};
    return !(s == "0" || s == "false" || s == "off" || s == "no");
}

// 命令行是否命名了模型？
//
// `--model X`是合同，而不是提示：每个角色都在其上运行。
// 在启动时设置一次，由组装 RoleConfig 的任何人读取。
// 它是进程状态而不是环境变量，因为它不是用户旋钮——它
// 反映了 argv 事实，而第二个拼写将是第二个事实来源。
//
// TUI 也这样读：`agentty --model X`
// 交互地具有相同的间隙，其中压缩路由远离用户命名的模型（#70）。
inline bool& model_pinned() noexcept {
    static bool pinned = false;
    return pinned;
}

} // namespace agentty::smart::tuning
