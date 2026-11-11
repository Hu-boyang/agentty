#pragma once
#include <cstddef>
#include <span>
#include <string_view>
#include <maya/widget/conversation.hpp>
#include <maya/widget/turn.hpp>
#include "agentty/runtime/model.hpp"

namespace agentty::ui {

// 为一条消息构建回合配置——头文件+带类型的正文槽(
// PlainText/MarkdownText/AgentTimeline/缓存的流式传输-markdown元素)。
// 当此消息是同一-speaker运行中的第2个以上辅助信息时，
// 设置`continuation = true`；然后，
// Turn 小部件会抑制其 头文件（字形 + 标签 + 元），
// 并且“对话”小部件会跳过 inter-turn 分隔线，因此来自一个代理
// 操作的连续辅助消息在视觉上会作为一个块流动。
//
// `tool_calls_override`让工具-batch合并在freeze_range/build_live_tail中，
// 从多个不相交消息的tool_calls合成单个面板，
// 而无需深-copying `msg`。当非-empty时，
// AgentTimeline扫描使用此跨度而不是`msg.tool_calls`；
// 当像以前一样使用空 `msg.tool_calls` 时。
//
// 权限卡是作为Turn 正文槽发出的NOT——主机将其作为
// 同级浮动在实时末尾 (agent_session形状)下方。
[[nodiscard]] maya::Turn::Config turn_config(const Message& msg,
                                             std::size_t msg_idx,
                                             int turn_num,
                                             const Model& m,
                                             bool continuation = false,
                                             std::string_view meta_override = {},
                                             std::span<const ToolUse> tool_calls_override = {});

// 构建ONE Turn::Config，覆盖一系列连续的助理消息。
// `run_first`为头消息(Role::Assistant)的索引； `run_end` 是
// 排他的 — [run_first, run_end) 中的每条消息都必须是 Assistant。
// 正文按照源顺序交错每个消息的文本（降价）和工具批次，
// 因此`text -> tool -> text -> tool`序列渲染
// 作为具有N个正文槽的单个视觉轮，匹配agent_session的形状。
//
// 头文件 (glyph、label、meta)取自头部消息；
// 元携带头部的时间戳+（可选）自上次用户消息以来经过的时间。
[[nodiscard]] maya::Turn::Config turn_config_for_assistant_run(
    std::size_t run_first, std::size_t run_end,
    int turn_num, const Model& m);

// 决定当前说话者-run结束的位置。对于助理头来说，
// 它会在连续的助理消息中向前移动；对于用户/其他角色，
// 它返回 `from + 1`。由 live-tail 和 freeze 构
// 建器使用，因此它们对消息向量进行相同的切片。
[[nodiscard]] std::size_t turn_run_end(const std::vector<Message>& messages,
                                       std::size_t from);

// 为助手运行[run_start、run_end)构建maya
// hash_id(组件-cache键)。 唯一 SOURCE OF TRUTH
// 为密钥形状，由必须同意 字节-for-byte 的两个站点使用，
// 因此冻结切换是 缓存 HIT（零行移位）：conversation.cpp
// 的已解决 -run 实时 末尾 和 freeze_range（单个
// 空闲-freeze 站点）。两者之间的 key-shape 不匹配会
// 产生 缓存 缺失和重复幽灵；在这里集中构建意味着未来的编辑
// 不能默默地取消同步一个调用者。
[[nodiscard]] maya::CacheId assistant_run_hash_id(
    const Model& m, std::size_t run_start, std::size_t run_end);


} // namespace agentty::ui
