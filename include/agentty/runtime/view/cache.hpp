#pragma once
// 查看-side 渲染 缓存。
//
// 将 可变 UI 状态保留在纯域类型之外（消息、ToolUse）。
// 域描述对话是什么；这个 缓存 描述了*我们
// 已经为其绘制的内容*，因此我们可以跳过每个 帧
// 重建相同的元素。
//
//   ·消息降价——最终确定的辅助消息，其`text`为
//     不可变。由（thread_id、msg_idx）键入。 流式传输 消息携带单独
//     的 `StreamingMarkdown` 和 缓存 块边界，因此每个增量
//     成本为 O(new_chars)，而不是 O(总计)。
//
//   ·转动配置+内置元件-FULL maya::Turn::Config+
//     由 Turn::build() 返回的用于已确定回合的
//     元素（在消息列表中具有后继者，因此通过构造完全解析）。
//     缓存代理时间线（每个工具卡配置），权限行，markdown 正文。
//     如果没有这个，每个 帧 都会遍历每条可见消息并从头开始重建 N
//     工具卡 × M 圈；约 10 圈后 帧 时间增长得足够多，
//     甚至直接模式也感觉迟缓，并且在 SSH-tunnelled 气隙上，
//     更大的 帧 为每 -byte 传输成本付出了代价。
//     重用缓存的元素使得 per-frame 的成本为 O(
//     active_turn) 而不是 O(total_turns × 工具)。
//
// ── 灵灵 -pure 状态更新函数 / 自我 -managing 缓存 ──
//
// 历史：
//   v1:进程-global thread_local映射由(ThreadId，msg_idx)键控，
//       具有诸如`message_md_cache(tid, idx)`和
//       `evict_thread(tid)`等自由函数。 状态更新函数 在 线程
//       开关/NewThread/压缩上调用 `ui::evict_thread(...)` 以
//       使 过期 条目无效 - type-pure（Model 不变）但spirit
//       -impure（对 Model 隐藏的突变 process-global 状态）。
//
//   v2:ViewCache作为可变成员移入Model::UI。
//       状态更新函数 突变现在在返回的 Model — 类型 -pure
//       和 表面 -pure 中可见。但 状态更新函数 仍然必须了解 渲染
//       -side 问题：它调用 `m.ui.view_cache.evict_thread()`
//       来防止 过期 缓存 条目在压缩后与同一（thread_id，
//       msg_idx）键上的新条目发生冲突。
//
//   v3 (此文件)：缓存由(thread_id，
//       message.id)键控——在构造时生成并保存到磁盘的稳定
//       的per-Message标识符。压缩会创建具有新 ID
//       的新消息，因此旧条目自然会成为孤儿；不需要明确驱逐。
//       状态更新函数 不再接触 缓存。 View-side 记忆化缓
//       存 在 Model::UI::view_cache 上停留到
//       `mutable`，这是标准的逻辑 -const
//       模式：填充 记忆化缓存 槽不会改变可观察到的 Model
//       行为（每个 缓存 命中都会返回 缓存 未命中所构建的内容）。
//
// 运行时通过构造在一个线程上序列化更新+查看，
// 因此不需要内部加锁。

#include <array>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include <maya/widget/turn.hpp>

#include "agentty/domain/id.hpp"

namespace maya {
    struct Element;
    class  StreamingMarkdown;
}

namespace agentty::ui {

struct MessageMdCache {
    std::shared_ptr<maya::Element>            finalized;
    std::shared_ptr<maya::StreamingMarkdown>  streaming;
    // 稳定后最后馈送到`streaming`的源的大小。
    // 一旦确定的msg.text与其匹配，则跳过per-frame
    // set_content()/finish()往返行程——set_content
    // 的字节-相等的快速路径已经是no-op，但仍然为每个可见的已确定回
    // 合在每个帧支付O(text)memcmp。 SIZE_MAX 是
    // "not yet settled" 哨兵。
    //
    // 无内容散列：在StreamFinished移动
    // streaming_text→文本之后，msg.text永远不会就地重写。
    // 编辑取代了消息批发（新的 MessageId，新的 缓存 插槽），
    // 因此仅尺寸就足够了 不变量。
    std::size_t                               last_settled_size =
        static_cast<std::size_t>(-1);

    // 配置该小部件的逐步显示-fx策略的运动级别。
    //
    // Per-WIDGET，因为问题"must I re-apply the policy"
    // 是per-widget。它曾经是cached_markdown_for
    // ()本地的compare-and-reset锁存器，
    // 每个帧运行SEVERAL次——每个流式传输小部件运行一次（推理"#r"通道，
    // 答案通道，以及在multi-message回合中每个子-turn运行一次）。
    // 第一个调用者消耗了更改，并且 帧 读取 "unchanged" 的每个后来
    // 的小部件，因此切换运动 mid-stream 应用于推理块并留下
    // 动画答案。
    //
    // 进程-global "changed this frame"
    // 标志修复了该问题并引入了更严重的问题：它在不发布
    // 每个帧的任何内容上都是粘性的，因此它在测试之间以及
    // 面板帧和线程 帧之间发生泄漏。存储应用值 此处 根本
    // 不需要 帧 语义——当小部件所拥有的内容与实际情况不同时，
    // 它就会重新-applies。
    //
    // -1 = 尚未应用任何内容（无条件配置新的小部件）。
    int                                       applied_motion = -1;

    // 一旦推理->应答接口边界滑翔机已针对该槽位被装备，
    // 则设置。触发器是状态 TRANSITION（当答案的第
    // 一个 字节 落地时推理就解决了），而不是 字节 到达，
    // 因此它不能生活在尺寸 -changed 门下——推理的源
    // 在那一刻停止增长，这就是 接口边界 以前不受限制的原因。
    // 锁定，因此最后期限被标记一次； re-arming 每
    // 个 帧 都会不断地向后推，光标永远不会落地。
    bool                                      seam_glide_armed = false;

    // ── 逐步显示 记账 ──
    //
    // 不存在主机-side打字机光标：cached_markdown_for
    // 每隔帧将到达的字节的FULL组馈送到流式传输小部件，并让maya的
    // reveal_fx对活动边缘进行动画处理。因此，`revealed_size`
    // 始终保持 == source.size()；它的存在只是为了让
    // 已确定的 fast-path / freeze 快照 可以在将
    // 其视为最终消息之前确认整个消息已被馈送。
    std::size_t                               revealed_size = 0;

    // `source`增长的最后帧的动画-clock时间戳（毫秒，
    // maya::anim_now_ms)（新的字节从传输到达）。
    // 驱动 "stream in motion" 宽限窗口，
    // 使 16 毫秒动画 帧 处于武装状态，同时 字节 正在流动，
    // 因此 reveal_fx 动画流畅。一旦 传输 安静的时间超过窗口（模型失速），
    // RAF 就会失效，循环会回到更平静的 Tick 节奏；下一个 Delta
    // 的 eventfd 唤醒会恢复它。在源收缩/滚动时重置为零。
    //
    // 在 ANIMATION 时钟（不是原始 steady_clock）上，
    // 因此它与 逐步显示 小部件同步前进，它门控并在
    // maya::testing::advance_anim_clock_ms 下
    // 保持确定性 — 否则，同步的 字节-identical 渲染 序列可能采用不同的
    // finalize/defer 路径 run-to-run 纯粹来自 wall-clock
    // 抖动（片状midrun_seam_test 犯了-prefix 分歧）。 0 = 未设置。
    std::int64_t                              last_grow_tick_ms = 0;
    std::size_t                               last_grow_size = 0;

    // 对 multi-sub-turn 渲染 刮擦 缓冲区。
    // 当单个助手消息在先前的 sub-turn（现在在 `msg.text`）
    // 中生成文本并且现在在 follow-up sub-turn（在
    // `msg.streaming_text`）中生成 流式传输 文本时，
    // 我们需要为小部件提供两者的 CONCATENATION，
    // 以便实时 末尾 不断明显增长。将已连接的 缓冲区 固定在此处，
    // 可以为 string_view 送入
    // set_content_async 提供稳定的支持。结算时清算。
    std::string                               combined_source;

    // 最后折叠成combined_source的三个部件的尺寸。
    // 当msg.text/streaming_text/pending_stream的NONE增长时，
    // 由cached_markdown_for将per-frame concat
    // + set_content_async round-trip用于short-circuit
    // (reveal_fx动画期间的主要情况：小部件渲染 60 FPS但字节到达10-30/ s，
    // 因此 >90% 的 帧 不会产生源更改）。
    //
    // 没有这个保护，活动末尾重建N
    // -byte字符串AND每隔帧对长sub-turn-2消息执行N-byte
    // memcmp（文本保存先前sub-turn的固定正文，streaming_text增长）。
    // in-flight运行中每个帧的O(turn-length)
    // 使得感知到的渲染速度随着转弯大小而降低。
    std::size_t last_text_size           = static_cast<std::size_t>(-1);
    std::size_t last_streaming_size      = static_cast<std::size_t>(-1);
    std::size_t last_pending_size        = static_cast<std::size_t>(-1);

    // 在先前的帧上看到的可视区域高度(来自RenderContext的
    // available_height())。 帧 之间的 DROP 是 终端
    // 收缩：终端 自动将顶部 可视区域 行推送到 不可变 本机 回滚缓冲区 中。
    // 如果实时 逐步显示 边缘（ghost-blanked + 加扰的 per
    // -codepoint）位于这些行中，则会将 过期 冻结在 回滚缓冲区 中，
    // 并且该行会丢失。 cached_markdown_for 将 逐步显示 扣到此类
    // 帧 (snap_reveal_to_edge) 的边缘，因此不存在可绞合的幻影行。
    // 0是"first frame / unknown"哨兵（不推断收缩）。
    int last_render_height = 0;

    // ── Tool-panel 推迟 ──
    //
    // 当 cached_markdown_for 持有该消息的工具面板 OFF-SCREEN 时，
    // 这是正确的，因为 逐步显示 光标仍然滑向实时边缘。在 传输 上，
    // content_block_stop(文本) 和 content_block_start
    // (tool_use) 是连续的 SSE 事件 — 通常是相同的 TCP 段 —
    // 因此 pre-emptive end-of-text 排空在卡存在之前让 ZERO
    // 帧 运行，并且强制 回滚缓冲区-safety 硬-snap 粘贴整个wire_cps
    // ×drain_secs 积压在一台 帧 中（
    // "md sticks then bursts at the tool" 症状；
    // tool_boundary_burst_probe）。虽然设置了这个 标志，
    // 但视图会跳过 append_assistant_tool_panel，因此散文下方
    // 没有任何 渲染 可以将 mid-reveal 行推入 不可变 回滚缓冲区 — 滑动
    // 是安全的，卡的第一个油漆处的卡扣是 no-op。
    // 由 cached_markdown_for 重新计算 每个 帧；消耗了相同的 帧 （相同的 cfg 构建）
    // append_assistant_body_slots/turn_config_for_assistant_run。
    // 从未构建 markdown (tool-only sub-turns) 的消息
    // 从未设置它 — 它们没有 逐步显示 的散文。
    bool defer_tool_panel = false;
    // 首先帧延迟启用——驱动cached_markdown_for中的
    // kMaxCardDeferMs硬上限，因此卡永远不会无限期地隐藏。
    // 动画-clock ms (maya::anim_now_ms)用于测试时钟下的确定性，
    // 与last_grow_tick_ms相同的原理。零 = "not deferring"。
    std::int64_t card_defer_since_ms = 0;
    // Two-phase推迟退出。当滑行完成时，退出 帧 运行 finish()
    // + fx-off — 其中 MUTATES 在 可视区域 底部行（打乱
    // 末尾 un-ghosts，尾随段落重新包装到提交的块路径中） —
    // 而面板 STAYS 隐藏，因此 帧 是突变 -only（diff
    // -repaintable 中地方）。面板在 下一个 帧 上显示，
    // 这是一个纯粹的底部 -append 生长，其穿过 可视区域 顶部的行都是最终的。
    // 将两者折叠成一个 帧 是生长+突变 - maya 的 HardReset
    // 臂（oracle-proven：one-frame 出口在
    // 60x18 处添加了 4 个门恢复）。
    bool defer_exit_finished = false;

    // ── 备忘录助理 -turn 已过期( 头文件 元) 维基月球在线解说-维基月度在线解说-1,1,2,2,3,2,4,2,5,2,6,5,5,4,5,7,5,6,6,7,7,6,8,7,8,6,9,7,9,8,8,9,6,1,7,2,7,1,8,5,8,2,8,1,5,1,1,6,2,1,3,7,3,1,4,7,4,1,9,2,9,1,10,1,0,1, ──
    //
    // assistant_elapsed()将-scans
    // m.d.current.messages反转到先前的用户消息以计算回合的wall-clock持续时间。
    // 该扫描是O(自上次用户转弯以来的sub-turns)=O(
    // 转弯长度)，在转弯飞行时为运行头运行EVERY 帧——真正的
    // 每-frame成本随着转弯深度而增长(整个in
    // -flight转弯停留在实时末尾中直到稳定；它永远不会在
    // -turn中期冻结——参见frozen.cpp)。该值仅取决于
    // 两个 不可变 时间戳（此头 + 先前的用户消息），因此一旦计算出来，
    // 它就不会改变。 缓存 它在这里键入标题消息的 ID，
    // 并跳过对每个后续 帧 的扫描。 `elapsed_valid` 将
    // "cached nullopt"（无先前用户转/出
    // -of-range dt）与 "not yet computed"
    // 区分开来。
    bool                 elapsed_valid = false;
    std::optional<float> elapsed_cached{};
};

// Per-(线程，msg)渲染缓存，由LIFECYCLE分区。
//
// 缓存 具有两层含义，它们具有真正不同的 不变量，并且将它们合并在单个
// LRU 下是产生全类 "typewriter stalls on a deep run"
// 错误的原因。所以他们是分裂的：
//
//   ┌─────────────┬──────────────────────────┬───────────────────────────┐
//   │             │  LIVE (pinned)           │  SETTLED (staged)         │
//   ├─────────────┼──────────────────────────┼───────────────────────────┤
//   │ 保存内容      │ 动画状态                 │ 纯渲染缓存               │
//   │               │ StreamingMarkdown        │ 已构建的 Element 与      │
//   │               │ 逐字显示光标、延迟状态   │ 已显示完成但仍在实时     │
//   │               │ 机的记账信息             │ 尾部的子轮次 Markdown    │
//   │             │                          │  tail (not yet frozen)    │
//   │ 淘汰影响      │ 正确性缺陷               │ 可以低成本重建，但实际   │
//   │               │ 滑动期间销毁逐字显示     │ 不会淘汰：条目在冻结时   │
//   │               │ 组件，导致打字机动画     │ 移除，而冻结也是最后一   │
//   │               │ 停住                     │ 次读取该条目的时刻       │
//   │ 数量          │ 受当前活动轮次约束       │ 受当前活动轮次约束       │
//   │               │ 约一个流式边界及其       │ 已显示完成、等待冻结     │
//   │               │ 实时子轮次               │ Tick 的子轮次            │
//   │ 策略          │ 绝不淘汰                 │ 冻结时移除               │
//   └─────────────┴──────────────────────────┴───────────────────────────┘
//
// 调用者声明访问想要哪个帽子：`message_md_live()`固定该条目；
// `message_md()` / `turn_config()` 将其作为固定的
// 记忆缓存 上演。消息是 "live"，而其 逐步显示 小部件是
// 对 live/finalizing/revealing/parsing 进行逻辑或判断，
// 其工具-panel 延迟机器正在运行 — i.e。驱逐它会损坏 -screen 输出。
// 视图已经计算了该谓词（参见 turn.cpp
// `subturn_stably_keyable`，其否定 IS 活跃度）。
//
// ── 为什么是 NO 顶端 ──
//
// 旧设计将固定边限制在 LRU 下，
// 与 RAM 进行交易的正确性：太小并且活动边被驱逐
// （失速）；太大而稳定的散文 源码树 囤积了千兆字节。
// 和 的困境和上限都消失了，因为已解决的条目有
// PROVABLE 死亡瞬间：帧 其消息冻结。
//
// 每个定居的-accessor 调用站点(cached_markdown_for、
// assistant_elapsed、settle_message_md、
// error/cancel抢救)读取LIVE TAIL——范围[frozen_through、
// size())中的消息。当 finalize_turn 完成一次运行
// 和下一个 Tick 调用 freeze_through() 时，这些消息会移动
// 上方 frozen_through：它们的行被密封到 m.ui.frozen（
// maya 账本，从其自己的组件 缓存 中位块传输）中，并且 NO 代码路径会
// 再次读取它们的 ViewCache 条目 - 不是在下一个 帧，也不是在scroll
// -back上（scroll-back是终端的原生回滚缓冲区，从来不是ViewCache重建）。
// 该条目在轮到冻结的那一刻就死了。
//
// 因此，freeze_range()DROPS是其密封的每条消息
// 的条目（参见其settle-to-drop循环）。因此，
// 已确定的地图仅保存当前的条目，而不是 -yet-frozen
// 末尾 — 以 一个 回合为界，与固定集完全相同。
// 没有什么可以限制的：地图在每次冻结时都是 self-emptying，
// 并且一万回合的会话与一回合的会话携带相同数量的已确定条目。
//
// 结算是一次迁移：小部件完成耗尽的一个帧，
// 调用者停止传递实时标志，并且条目从固定映射移动到已
// 结算映射（在运行message_md_live()之后，
// message_md()调用执行该迁移，保留载荷)；
// 几个帧之后冻结掉落它。
//
class ViewCache {
public:
    ViewCache() = default;

    // Move-only：复制语义对于渲染 缓存来说是无意义的，
    // 并且会使整个元素图深入-copy。移动很便宜（
    // unordered_map和列表没有除了移动）。
    ViewCache(const ViewCache&)            = delete;
    ViewCache& operator=(const ViewCache&) = delete;
    ViewCache(ViewCache&&) noexcept            = default;
    ViewCache& operator=(ViewCache&&) noexcept = default;

    // ── 已解决的接入器(分阶段,冻结时投放) ──
    // 对于动画状态被完全耗尽的条目：对于仍然处于实时末尾中的子-turn，
    // 纯渲染 记忆缓存。 freeze_range() 在其消息
    // 冻结（最后一次读取）的 帧 处将其丢弃，因此已确定的地图
    // 保持受活动回合限制 — 无上限，无 LRU。
    [[nodiscard]] MessageMdCache&  message_md (const ThreadId& tid,
                                               const MessageId& mid);

    // ── 现场接驳者(被绑住——从未被逐出) ──
    // 对于保存加载-bearing动画状态的条目（实时逐步
    // 显示小部件或活动延迟机器）。固定住 OUTSIDE
    // 固定地图，因此没有运行深度可以驱逐 流式传输 边缘
    // mid-glide。在固定键 MIGRATES 上调用实
    // 时访问器将其转移到固定映射（并且固定访问器将其迁移回来），
    // 因此消息的槽遵循其生命周期，其 载荷 完好无损。
    [[nodiscard]] MessageMdCache&  message_md_live (const ThreadId& tid,
                                                    const MessageId& mid);

    // 探测/断言的内省：该密钥当前是否被
    // 固定（i.e.结构上免于驱逐）？
    [[nodiscard]] bool is_pinned(const ThreadId& tid,
                                 const MessageId& mid) const noexcept;
    [[nodiscard]] std::size_t pinned_count()  const noexcept { return pinned_.size(); }
    [[nodiscard]] std::size_t settled_count() const noexcept { return entries_.size(); }

    // 从 两者 家庭（固定和固定）中删除消息条目，立即收回其 载荷。
    // 如果不存在则为 -op。这是一个已确定条目的死亡时刻：
    // freeze_range() 调用在它密封的每条消息上，
    // 因为一旦消息被冻结，它的行就存在于m.ui.frozen（
    // maya账本）中，并且没有代码路径再次读取它的ViewCache
    // 条目——所以保留它纯粹是泄漏。从固定侧掉落太关闭了镜子危险（
    // 一个停止动画但从未将 -entered 渲染 接口边界 向下
    // -migrated、e.g.error/cancel mid
    // -reveal 的小部件）：通过构造，冻结消息位于
    // NEITHER 地图中。这就是为什么固定地图 self
    // -emptying 和上限变得不必要的原因。
    void drop(const ThreadId& tid, const MessageId& mid);

    // const 查看操作，不迁移条目，也不更新访问记录。
    // 从消息所在的位置（固定或固定）返回消息的 md 槽，如果不存在则
    // 返回 nullptr。与 message_md() /
    // message_md_live() 不同，不 会创建、
    // 迁移或重新排序任何内容 - 它是纯粹的读取。将其用于 read-only
    // 状态探测：此组件是否仍在播放动画？
    // 不得扰乱分区或 LRU 顺序：通过变异访问器路由此类探针
    // 会将实时条目向下迁移到 LRU（un-pinning 边缘）
    // 或虚假地提高 LRU 新近度。该指针在下一次变异 缓存 调
    // 用 之前一直有效。
    [[nodiscard]] const MessageMdCache* peek(const ThreadId& tid,
                                             const MessageId& mid) const noexcept;

    // 掉落 EVERYTHING — 两张地图，都是 线程。
    // 调用批量对话交换 (NewThread / ThreadLoaded
    // / CheckpointRestored)，
    // 其中 m.d.current 被替换，每个缓存的 (tid,
    // msg) 条目都属于不再出现在屏幕上的对话。如果没有这个，
    // 之前的 线程 的已确定条目将永远保留：冻结只会在 当前 线程 的
    // 消息冻结时删除条目，而交换的 -away 线程 的消息永远不会再次冻结。
    // 由于密钥嵌入了 thread_id，因此不存在正确性错误（无冲突）
    // ——只有无界-over-session-of-many-threads 泄漏，
    // 该泄漏将被关闭。新的 线程 在其第一个 渲染 上从头开始
    // 重新填充。
    void clear() noexcept { entries_.clear(); pinned_.clear(); }

    // 仅删除 SETTLED 备忘录，保持固定条目不变。
    //
    // 用于在不干扰数据流的情况下使渲染的外观无效。
    // 已确定的条目是纯粹的 记忆缓存 —`finalized`
    // 是一个内置元素，其颜色已经解析 — 因此在主题更改后，
    // 它完全是错误的，并且删除它需要一次重建。
    // PINNED 条目则相反：它保存无法从消息中重建的
    // 加载-bearing 动画状态（实时 逐步显示 小部件，
    // 活动的延迟机器），并且在 -stream 中删除它会
    // 重新启动当前正在写入的回合的 逐步显示。无论如何，
    // 这些条目每隔 帧 就会从它们的小部件中重建，因此它们自己
    // 遵循新的主题而不会被破坏。
    void clear_settled() noexcept { entries_.clear(); }

    // （这里故意不再有invalidate_colours()。）
    //
    // 它过去常常在主题改变时删除每个已确定条目的内置元素，
    // 因为该元素将传出调色板RESOLVED放入其中，
    // 并且没有其他东西会使它无效——主题切换不会改变字节，
    // 因此没有内容-keyed 缓存通知。
    //
    // 颜色现在已晚-bound (docs/LATE_BINDING.md)：
    // 构建的元素携带符号槽并在PAINT处解析，因此它根本不是新
    // 主题下的过期并且没有什么可以无效的。如果您发现自己想要恢复此功能，
    // 那么真正的问题是哪个值被过早解析 -
    // maya/style/binding.hpp 中的
    // MAYA_ASSERT_LATE_BOUND 就是被捕获的地方。

private:
    // 一台载荷型，两户。一个密钥同时存在于其中的 AT MOST 一个 中；
    // migrate_() 在它们之间移动 Entry，
    // 保留 MessageMdCache 载荷（逐步显示 小部件，
    // 块 缓存，延迟状态），因此结算永远不会丢失动画状态。两者都受到 活动
    // 回合的限制：由实时 sub-turns 固定，
    // 由排空的 -but-not-yet-frozen sub-turns
    // 结算——从结算中冻结掉落，因此在一个会话中两者都不会增长。无 LRU，
    // 无上限：死亡瞬间（冻结）是明确的，不摊销。
    //   ·entries_-结算记忆缓存，冻结时掉落。
    //   · pinned_-活动状态，从未被逐出，耗尽→向下迁移。
    struct Entry {
        MessageMdCache md;
        bool           pinned = false;
    };
    std::unordered_map<std::string, Entry> entries_;   // 落户
    std::unordered_map<std::string, Entry> pinned_;    // 居住（永不驱逐）

    // 访问SETTLED侧的`key`：在entries_中查找-or-insert。
    // 如果密钥已固定，请先将其向下迁移（保留 载荷）。
    Entry& touch_settled_(const std::string& key);
    // 访问LIVE侧的`key`:find-or-insert in
    // pinned_。如果密钥已解决，则将其向上迁移（保留载荷）。从不驱逐。
    Entry& touch_pinned_(const std::string& key);
    // 将现有钥匙从一个家庭移至另一个家庭，
    // 携带其载荷。返回对新家中条目的引用。
    Entry& migrate_to_pinned_(const std::string& key);
    Entry& migrate_to_settled_(const std::string& key);

    static std::string make_key_(const ThreadId& tid, const MessageId& mid);
};

} // namespace agentty::ui
