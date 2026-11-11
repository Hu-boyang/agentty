#pragma once
#include <cstdint>
#include <span>
#include <string_view>
#include <maya/widget/agent_timeline.hpp>
#include "agentty/runtime/model.hpp"

namespace agentty::ui {

// 建立助手轮的"Actions"面板配置。聚合状态（total/done/elapsed/category
// 计数），选择每个 -category 颜色，计算 title/footer，
// 将 tool_calls 放入 事件。
//
// 通过借用的跨度获取tool_calls，
// 因此turn_config/freeze_range/build_live_tail中的工具-batch合并可以从多个不相交消息的tool_calls合成面板，
// 而无需每帧一个消息深
// -copying。
[[nodiscard]] maya::AgentTimeline::Config agent_timeline_config(
    std::span<const ToolUse> tool_calls,
    int spinner_frame,
    maya::Color rail_color);

// 将面板构建为ready-to-emit元素。对于 全部-TERMINAL 批次
// （每个工具 Done/Failed/Rejected），批次 字节 不是可变，
// 因此 full-built 元素被存储在该批次的 content-address
// 下（每个工具的 id + 状态 + 输出大小 + 渲染 键，加上 grep
// -hits 签名 + 轨道颜色）。命中时，它返回缓存元素的副本 - 跳过整个
// agent_timeline_config 重建（grep 扫描、详细字符串、
// 页脚、per-event CacheIdBuilder）和 maya 布局行走。
//
// 这是对长-in-flight-turn滞后的修复：
// edit/write-heavy自动驾驶仪转弯在实时末尾中累积了MANY已经-settled工具面板；
// 该运行没有 hash_id，而 末尾 仍然是 数据流，因此主机之前每
// 帧 重建 每个 固定面板 — 每 帧 O(Σ 批量大小)。像以前一样，
// 每个 帧 都会重新构建一个非 -terminal 批次（仍然是
// running/pending 的工具，或动画 加载动画）。
//
// 发射的字节是新构建的字节-identical，因此冻结切换(freeze_range
// 标记相同的assistant_run_hash_id)保持纯粹的maya 缓
// 存命中——没有回滚缓冲区-corruption表面。
[[nodiscard]] maya::Element agent_timeline_element(
    std::span<const ToolUse> tool_calls,
    int spinner_frame,
    maya::Color rail_color);

// 记忆变体。结果与 agent_timeline_element 相同，
// 但通过专用的 per-message 面板备忘录在 sub-turn 的稳定
// MessageId + render_key（单个 uint64 比较）上键入 首次。
// 在固定的 sub-turn 上，这会跳过 O(tools) content-key
// 字符串构建，即使是 g_panel_cache 命中也会付出代价，
// 因此在 -flight 的长运行中，其固定面板是 re-emitted，每帧都保持平坦的转弯深度。
// `msg_id`为Message::id.值； `render_key`
// 是 Message::compute_render_key()。在 记忆缓存 未命中时落入内容
// -addressed 路径 (g_panel_cache)。
[[nodiscard]] maya::Element agent_timeline_element_memoized(
    std::string_view msg_id,
    std::uint64_t render_key,
    std::span<const ToolUse> tool_calls,
    int spinner_frame,
    maya::Color rail_color);

} // namespace agentty::ui
