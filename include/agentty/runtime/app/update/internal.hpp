#pragma once
// update/* 编译单元的共享内部结构。不是公共 agentty::app 接口的
// 一部分 - 外部调用者通过 update.hpp 中的 agentty::app::update()。
// 位于 include/ 而不是私有 src/ 头文件 下，
// 因此三个 update/*.cpp 文件和 update.cpp 都可以看到相同的声明。

#include <concepts>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include <maya/maya.hpp>
#include <nlohmann/json_fwd.hpp>  // 这里仅声明JSON；完整类型位于 .cpp 中

#include "agentty/runtime/cmd.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/domain/entitlement.hpp"
#include "agentty/runtime/msg.hpp"
#include "agentty/runtime/panel/step.hpp"   // ui::step:列表游标规则

namespace agentty::app {

// 状态更新函数步骤：下一个模型，以及应该发生什么。
//
// 这仍然是OLD形状(按值返回模型)。 jaal 是
// `Cmd update(Model&, Leaf)` — 就地变异，
// 仅返回 副作用 — 这会删除 172 个 `return {std::move(m), ...}` 位点，
// 并带来真正的危险：`return {std::move(m), f(m)}`
// 具有未指定的评估顺序，并且今天的 源码树 中至少有一个。
//
// 转换签名是它自己的步骤(docs/design/jaal-rewrite.md中的4)，
// 因此机械23-file改变永远不会与行为改变混合在一起。
// 目前仅Cmd类型移动到jaal类型。
using Step = std::pair<Model, Cmd>;
inline Step done(Model m) { return {std::move(m), Cmd::none()}; }

namespace detail {

// 每-message直播缓冲区的硬上限。行为不当的服务器（或敌对代理）发出无限制的
// `text_delta`/`input_json_delta`，否则会增加
// `streaming_text` / `args_streaming`，
// 直到进程 OOM。 8 MiB 远远高于任何现实的单一 -message
// 正文 — 达到此上限意味着确实损坏了 上游，而不是真正的工作负载。
inline constexpr std::size_t kMaxStreamingBytes = 8 * 1024 * 1024;

// 视图虚拟化阈值——当转录本超过kViewWindow消息时，
// 将最旧的kSliceChunk切片成终端 回滚缓冲区，
// 使得per-frame Yoga布局通道保持有界。
//
// Per-Element缓存(715679f)消除了用于固定转弯的per
// -frame Turn::build()重建——但是maya仍然在
// 每个帧通过Yoga行走完整的可见元素源码树，并且布局成本与节点数成线性比例。
// Tool-heavy 会话（在单个用户消息下堆叠的
// Read/Grep/Bash 卡）轻松超过了 per-message 的平均值：
// 具有5轮以上工具轮次的一条辅助消息本身有100-200个节点。
// 当kViewWindow = 40时，实时画布达到5000+节点，
// 渲染延迟达到可见窗口底部的〜Tick间隔，并且作曲家的重绘开始
// 落后于击键（
// 先闪烁，随后卡住并遮住输入框。
// ——下一个帧落后于终端的实际光标位置）。
//
// 20/8将实时源码树限制为大约2-3回合的工具卡（大约1000-1500
// 个节点），其适合在中等硬件上的一个Tick内。 trade-off 是
// 用户在实时画布中看到较少的 回滚缓冲区 转弯 "above the fold"
// - 但承诺的转弯仍保留在 终端 的本机 回滚缓冲区 中，无论如何，
// 这就是 Page-Up / mouse-wheel 着陆的地方。
//
// 历史：
//   60/20 → 40/15 (在长时间训练中呈现-canvas-rows-bound尖峰)
//   40/15 → 20/8 (工具-heavy轮上的瑜伽布局成本)
inline constexpr int kViewWindow = 20;
inline constexpr int kSliceChunk = 8;

// ── update_stream.cpp ────────────────────────────────────────────────────
void update_stream_preview(ToolUse& tc);
bool guard_truncated_tool_args(ToolUse& tc,
                               std::chrono::steady_clock::time_point now);
nlohmann::json salvage_args(const ToolUse& tc);
Cmd finalize_turn(Model& m, StopReason stop_reason = StopReason::Unspecified);
// 完成一轮后：如果线程需要聊天标题，则询问聊天标题。
Cmd maybe_auto_title(Model& m);
// 保存 m.d.current：相对于为其描述的最后保存的增量，
// 因此长线程仅花费其新消息。使用这个，切勿使用 SaveThread{}。
[[nodiscard]] Cmd save_current(Model& m);
// 一样的，整张寄过来。对于离开线程的保存（切换、新建、
// 退出、分叉）：从线程重新发送拒绝的增量Model，
// 此时它不再拥有该线程。
[[nodiscard]] Cmd save_current_whole(Model& m);

// 从 `todo` 工具 调用 的 args["todos"] 数组同步 持久化
// 计划状态 (m.ui.todo.items)。在 tool-exec 输出的
// arg 流式传输 和 期间实时调用，因此 弹出面板 + 任何全局指示器
// 都会在模型写入 in-progress 项时立即跟踪它。 No-op 当
// args 不携带 todos 数组时（部分早期 数据流）。
void sync_todo_state_from_args(Model& m, const nlohmann::json& args);

// ── 保存设置 ───────────────────────────────────────────────────────
//
// THE规则：`m.d.persisted`是记录。节省就是移交。
//
// agentty曾经对保存设置了两种不同的方式，并且他们进行了战斗：
//
//   A. 加载-modify-save —
//      `auto s = deps().load_settings(); s.x = ...; deps().save_settings(s);` —
//      写了商店但留下了 `m.d.persisted` 过期。
//   B.persist_settings(m)，其种子FROM `m.d.persisted`。
//
// 因此，每个 A-style 写入都是指向下一个 B 的实弹：
// 切换将更改条带(A)，然后更改主题或切换模型或退出(B)，
// 并且B直接在A的更改上写入其过期记录。
// 这就是 "settings don't persist" 错误——写入到达，
// 然后一个不相关的 保存 默默地恢复它。
//
// 解决方法是删除A形状而不是要求~20 调用站点记住也补丁记录。
// 编辑 `m.d.persisted` 上的字段，然后编辑 调
// 用 上的字段：
//
//     m.d.persisted.show_changes_strip=m.d.show_changes_strip;
//     save_record(m);
//
// 因此，正好有一个快照在起作用，并且没有任何东西需要保持同步。
// 仍然在接口边界处写入-behind，因此状态更新函数永远不会使磁盘上的帧停顿。
//
// (这是jaal希望状态更新函数具有的形状。一旦`update`返回`Cmd`
// 而不是一对，
// 正文就变成`return Cmd::fx<save_settings>({m.d.persisted})`，
// 接口边界完全消失——
// 参见docs/design/jaal-rewrite.md.现在通过一个函数路由每个写入器是稍后进行机械更改的原因，
// 而不是20个单独的更改。)
[[nodiscard]] Cmd save_record(Model& m);

// `provider_keys`也是由主机上的凭据层写入的(AccountOp:add_key，
// 账户activate/remove，clear_active)。每个这样的 副作用
// 都与 LoadAuthView 进行批处理，其回复 (AuthViewLoaded)
// 将保管库的密钥复制回 m.d.persisted，因此记录永远不会变成 过期，
// 并且整个 -record 保存 无法删除密钥。

// ── update/submit.cpp 帮助者 ─────────────────────────────────────────────
Cmd            submit_message(Model& m);
// 将仍然作为 SEPARATE 域成员（model_id、努力、简档、智能、
// 活动提供者）存在的设置字段同步到 `m.d.persisted`，
// 然后将 保存 记录。当 THOSE 之一发生变化时使用此功能；当您直接编辑
// `m.d.persisted` 时，请使用 save_record()。
[[nodiscard]] Cmd persist_settings(Model& m);

// 清除ALL瞬时作曲家草稿状态(文本、光标、附件、undo/redo、
// 历史记录、排队消息、queue-peek/draft 快照)。
// 用于批量线程交换 (NewThread /
// ThreadLoaded)：草稿（包括粘贴的
// -but-unsent 图像附件）属于用户所在的线程，
// 而不是他们切换到的线程。将粘贴的图像（其字节已排入先前的消息中，
// 因此剩余的附件正文为 空）泄漏到下一个线程的第一个提交中，
// 该提交序列化了一个空图像块并对请求进行了 400 处理。
void           reset_composer_draft(ComposerState& c);

// 当前-active提供者的规范ID("anthropic"代表Claude路径，
// 否则OpenAI端点标签——"openai"/"ollama"/…)。
// 用于在 Settings::provider_models 中键入每个 -provider 模型的召回。
// 活动提供者的目录 id-每个最近/fused-catalog/能力行的键被归档在：
// Anthropic的默认id，或者OpenAI
// -family端点标签，或者ACP代理id下。
//
// MODEL的选择的函数。它曾经读取进程全局（provider::active()），
// 因此 状态更新函数 询问 "which provider" 得到了全局
// 所说的任何内容 - 其中，mid-switch，可能是留下的提供者。
std::string    active_provider_id(const Model& m);

// ── 应享权利( ACCOUNT-scoped 事实) ──────────────────────────────────
//
// ONE位置（提供者、账户）被解析用于权利查找，因此没有调用站点重新-derives它，
// 并且没有人可以意外地单独由提供者键入——这正是遗留账户-blind
// `context_1m_blocked` bool所做的。
// 请参阅 include/agentty/domain/entitlement.hpp
// 了解该层存在的原因。
//
// `is_blocked` 读取 `provider` 的 CURRENTLY
// 活动 帐户的密钥存储（对于活动帐户，传递 active_provider_id(m)）。
// `record_blocked` 学会拒绝并坚持下去；当事实是新的时
// 它返回 true，因此调用者可以跳过冗余设置写入。
[[nodiscard]] bool entitlement_blocked(const store::Settings& s,
                                       const auth::AuthView& accounts,
                                       domain::entitlement::Fact f,
                                       std::string_view provider,
                                       std::string_view model_id = {});
bool entitlement_record_blocked(store::Settings& s,
                                const auth::AuthView& accounts,
                                domain::entitlement::Fact f,
                                std::string_view provider,
                                std::string_view model_id = {});

// 选择切换 TO 提供商 `spec` 时激活的型号。
// 首选该提供程序最后使用的模型 (Settings::provider_models)，
// 否则提供程序类型的默认内置 -in 是正常的。
// 当不存在召回并且提供程序没有硬编码默认值时返回空（模型列表重新
// 获取将自动-select第一个可用模型）。
// 获取设置RECORD而不是读取存储：Model已经保存它，
// 并且重新-reads和接口边界的状态更新函数是两个副本如何分开的。
// 参见上文 save_record。
std::string    model_for_provider(const store::Settings& s,
                                  std::string_view spec);

// 提交实时提供者切换——ONE放置完整序列，因此三个入口点(提供者选择面板、
// custom-host 弹出面板、API-key 弹出面板)永远不会漂移。
// 鉴于已经-resolved目的地`spec`和已经-resolved
// `new_auth`，这：
//   1.将OUTGOING模型归档在其规范提供者ID下（召回），
//   2.安装+保留新选择（提供者+努力+密钥，通过
//      persist_settings，因此努力永远不会落在一跳上），
//   3.为新后端激活有效模型（召回→built-in
//      默认 → auto-select 为空），更新 context_max + 子代理，
//   4. RE-CLAMPS Model::effort 将新型号的功能改为过期
//      Xhigh/high层无法骑乘到不支持它的模型上，
//   5、交换Deps认证，清除过期型号列表，返回
//      Cmd批次(状态吐司+模型重新获取)供调用者返回。
// `label`是确认祝酒词的人名。
// `desired_model`（可选）：当非-empty时，
// 在新提供商上激活的确切传输模型ID，优先于per-provider召回
// ——这是融合的交叉-provider 选择面板传递的，因此Enter
// 是ATOMIC提供商+模型开关。空（每个现有调用者）保留调用路径。
[[nodiscard]] Cmd
commit_provider_switch(Model& m, std::string_view spec,
                       std::string_view label,
                       std::string_view desired_model = {},
                       bool open_panel = true);

// AUTHENTICATING IS 不 SELECTING。
//
// 设置提供者——粘贴API密钥、添加自定义主机、OAuth标志-in——用于像
// 选择面板 Enter那样以commit_provider_switch结尾。
// 因此，在 Claude 上的 mid-task 时注册备份 Groq 密钥会
// 移动活动后端 和（因为交换机必须为新提供商选择一个模型），默默地从您的
// 控制下更改该模型。添加提供程序的常见原因是 "have it available"，
// 而不是 "use it right now"。
//
// 这是 凭据-path 结局：密钥是 已保存 并且重新
// 加载了身份验证视图，活动选择和模型完全保持原样，
// 并且 Toast 说明如何切换（如果这是您想要的）。
//
// `label`在祝酒词中命名提供者。返回 toast Cmd；调用者用自己的
// save/AddKey 副作用 对其进行批处理。
[[nodiscard]] Cmd
provider_ready_without_switch(Model& m, std::string_view label,
                              std::string_view detail = {});

// 当 无内容 尚未配置时为真 — 在设置或环境中对于任何提供
// 者都没有 凭据。在该路径上有一个 Sign-in SHOULD
// 开关：没有可保留的活动后端，并且在第一个 -run 用户
// 登录后未进行配置会比由此避免的意外情况更糟糕。
[[nodiscard]] bool no_provider_configured(const Model& m);

// ── 冻 -scrollback 前缀帮助( E) frozen.cpp) ────────────────────────
//
// freeze_through_prior_turn:行走m.d.current.messages
// [frozen_through..end)并将构建的转向元件(具有前导间隙)推入
// m.ui.frozen，直至(但包括NOT)`live_start`处的消息。
// 应用 -batch-merge 逻辑 conversation_config 在查看
// 时执行的相同工具，因此冻结的视觉效果与实时视觉效果 字节-for-byte 相匹配。
//
// 典型的调用:在submit_message处，冻结通过刚刚
// -finished代理轮次（在即将推送新用户的时刻live_start =
// messages.size()）。
void freeze_through(Model& m, std::size_t live_start);

// NOTE：以前在这里声明的增量（mid-stream）正文-freeze API（
// incremental_freeze_enabled、set_incremental_freeze_覆盖、
// maybe_incremental_freeze、incremental_freeze_target）
// 已经是DELETED，以及之前的freeze_settled_subturns /
// freeze_streaming_ text_prefix / trim_frozen_above_viewport。
// agent_session - 零 回滚缓冲区 损坏的参考实现 - 每回合恰好
// 冻结一次 (MessageStop)；唯一的生产模拟是 finalize_turn
// → pending_settle_freeze → freeze_through。
// Mid-stream 雕刻附加到 m.ui.frozen WHILE，
// 实时后缀在同一更新中缩小——两个形状变化 maya 的状态内联差异无法
// 消除歧义——并标记冻结的转弯，其散列 maya 的 缓存 从未见过，
// 迫使 缓存-miss re-emits 过度提交回滚缓冲区（
// 重复的-line/flush-left-header/box-border/composer-bleed损坏）。
// 不要重新引入它们。

// rehydrate_frozen:根据当前线程的消息+压缩
// 记录从头开始重建m.ui.frozen。用于 线程
// 交换机 / 线程 加载 — 消息向量被批量替换的任何地方。
// 复位frozen_through和frozen_turn。
void rehydrate_frozen(Model& m);

// clear_frozen:丢弃整个冻结向量并重置计数器。
// 对于新鲜-start提交之前的NewThread。
void clear_frozen(Model& m);

// 回滚缓冲区重播(maya/render/scrollback_replay.hpp)：
// 历史ABOVE是实时冻结前缀，最早的在前，与freeze_range
// 完全相同地构建它。 终端 调用 仅在擦除 回滚缓冲区（宽度变化，
// reset_inline）的 帧 上，因此它永远不会按照 帧 运行。
// 读取模型并可能解决never-shown消息的降价问题；不触及任何活动状态。
[[nodiscard]] std::vector<maya::ReplayBlock> build_history_blocks(const Model& m);
// 相同的历史最新-first，每调用 (SIZE_MAX=在实时前缀处)
// 以`cursor`结束的运行块。有上限的重播只会构建它所保留的内容。
// 连接时与 build_history_blocks 的输出相同。
[[nodiscard]] maya::ScrollbackReplay::Chunk history_chunk(const Model& m, std::size_t cursor);
// 重播的（最大行数，最大字节）：AGENTTY_REPLAY_ROWS（如果设置），
// 否则回滚缓冲区设置，永远不会超过终端保留的内容（tmux报告它）。
[[nodiscard]] std::pair<int, std::size_t> scrollback_replay_limits(const Model& m);

// restyle_sealed_turns:重新-publish主题，
// 删除内置颜色，并重建冻结前缀，以便EVERYTHING ON SCREEN
// 承载新方案。在主题浏览器中的每个箭头键上运行，这就是其成本 加载
// -bearing 的原因 - 冻结账本绑定到 ~3 个视口 (
// frozen_row_budget)，因此它是 O（可见行），
// 而不是 O（记录）。外部链接，以便 theme_preview_cost_probe
// 可以将其计时作为回归测试。
void restyle_sealed_turns(Model& m);

// 两行预算，以及为什么它们是两个数字。
//
// frozen_row_budget LIVE画布：m.ui.frozen中为re
//                    -rendering保留了什么。每次调整大小和
//                    Ctrl-L 都会从上到下移动，并且用户会观看该绘制，
//                    因此它仅限于〜3个视口。
// rehydrate_row_budget RESUME 绘制了什么，
//                    ~10 个视口。超出实时预算的部分并没有浪费：post
//                    -paint 修剪将其提交给 终端 的本机 回滚缓冲区
//                    （drop_front 累积 ScrollbackDebt，
//                    harvest() 铸造提交令牌），
//                    这使得恢复的 线程 的历史记录完全可以滚动。
//
// 这些只是一个数字。在 线程 开关上，用户只有一轮上下文，
// 上面没有任何内容：没有任何内容溢出，因此本机 回滚缓冲区 中没有任何内容，
// 并且 reset_inline() 的 \x1b[3J 刚刚擦除了
// 所有内容。导出后，rehydrate_scrollback_test
// 可以将关系固定在固定的行数。
// Model重载读取主机上次报告的终端大小(
// Model::ui.term_rows)； int 明确地接受它，用于测试。
[[nodiscard]] std::size_t frozen_row_budget(int term_rows);
[[nodiscard]] std::size_t frozen_row_budget(const Model& m);
[[nodiscard]] std::size_t rehydrate_row_budget(int term_rows);
[[nodiscard]] std::size_t rehydrate_row_budget(const Model& m);

// 解决一个助理消息的StreamingMarkdown小部件：馈送最终的字节、
// finish()(刷新末尾→前缀，翻转live_关闭)，
// 应用cached_markdown_for使用的相同的auto-fold预设，
// 并标记缓存尺寸，以便每个-frame解决的fast-path接合。在 stream.cpp 中定义。
void settle_message_md(Model& m, const Message& msg);
// 同样，通过const Model(它只触及可变视图缓存)。
void settle_message_md_cached(const Model& m, const Message& msg);

// Remote-session检测和end-of-turn 逐步显示-glide策略是LAUNCH
// FACTS，由init()捕获一次到Model::env(
// runtime/app/env.cpp)：读取m.env.remote和m.env.reveal_end_glide()。
// 这里它们是自由函数，getenv 调用位于function
// -local静态后面，这使得每个请求进程函数而不是Model
// 函数的状态更新函数。

// live_tail_reveal_settled：真，当且仅当 每个 实时
// 末尾 [frozen_through..end) 中的助手消息已完全耗尽其 逐步
// 显示 动画 — 小部件翻转 live_ off，打字机光标到达实时边缘，
// 最终完成斜坡完成，并且没有异步 解析 正在运行。此时，现场 末尾 将 SETTLED
// 源码树 绘制到 maya 的 prev_cells 中，因此现在进行的冻结是
// 字节-and-hash-identical 到屏幕上的内容（缓存 HIT，
// 零 re-emit）。谓词集是build_live_tail的
// `reveal_settled`的EXACT镜像（is_live ||
// reveal_in_progress || is_finalizing ||
// is_parsing）：该门决定是否活末尾 STAMPS可缓存assistant_run_hash_id，
// 这个一个人决定是否冻结火灾——他们必须同意，或者冻结印上一把钥匙，
// 现场末尾从未画过，freeze_range在不同的高度重建(show_all)。
// 习惯于GATE延迟的结算-freeze：我们从不最终确定+冻结逐步显示仍在
// 动画的回合，这是post-stream duplicate/ghost的结构性根本原因
// （冻结与prev_cells中的still-animating
// live 帧不同的post-finish形状）。当 末尾 没有
// Assistant md 要耗尽（没有任何等待）时返回 true。
bool live_tail_reveal_settled(const Model& m);

// ensure_frozen_width是GONE(账本油漆-recording re
// -stamps每个密封块在每个帧的活动宽度处的高度——调整大小自行修复；
// 参见maya/render/scrollback_ledger.hpp)。

// trim_frozen_if_oversized:当冻结超过软上限时，
// 丢弃最旧的N个块以保持maya的prev_cells工作集有界。
// 返回由账本从 maya 自己的 Paint-recorded 高度铸造的
// cmd::commit_scrollback(ScrollbackDebt)。如果低于上限，则为 No-op。
Cmd trim_frozen_if_oversized(Model& m);

// 设置 `ttl` 之后的 auto-clears 的短暂状态 toast。
// 返回安排 ClearStatus 哨兵的 Cmd（stamp-matched，
// 以便较新的状态覆盖而不被擦除）。用于 "no-op" 反馈，
// 例如 "no pending changes" /
// "nothing to copy" - 任何替代方案都是静默故障，让用户怀疑他们
// 的击键是否已注册。
Cmd set_status_toast(Model& m, std::string text,
                                std::chrono::seconds ttl = std::chrono::seconds{3});

// ── update/stream.cpp 帮助者 ────────────────────────────────────────────
// （在上面的模块范围中声明 - `update_stream_preview`、`salvage_args`、
// `finalize_turn`。下面的 stream_update 状态更新函数 使用它们。）

// 该Model所指向的模型的解析上下文窗口。
//
// ONE地方组装三个输入resolve_context_window层（用户覆盖、
// 广告窗口、id推断）。四个 调用 站点用于打开 -code
// 先读取 context_max_for_model，再遍历 available_models，并用探测结果覆盖。
// — 根本没有覆盖步骤，
// 并且需要四次相同的修复。
[[nodiscard]] int resolved_context_max(const Model& m,
                                       std::string_view provider_id);

// 从当前实时工具状态重建打开的Ctrl+O 快照。
// 由参数-stream和执行状态更新函数调用；查看器关闭时为 no-op。
void resync_live_tool_viewer(Model& m);
// 快照每个finished/streaming工具调用进入查看器条目——由面板的开
// 臂(update/tool_output.cpp)和上面的重新同步使用。
// 在其读取的执行状态旁边的tool.cpp中定义。
[[nodiscard]] std::vector<tool_output::Entry> collect_viewer_entries(const Model& m);

// ── update/tool.cpp 帮助者 ──────────────────────────────────────────────
void apply_tool_output(Model& m, const ToolCallId& id,
                       std::expected<std::string, tools::ToolError>&& result,
                       std::optional<FileChange>&& change = std::nullopt,
                       std::vector<FileChange>&& changes = {},
                       std::vector<ImageContent>&& images = {},
                       std::uint64_t exec_seq = 0);
void mark_tool_rejected(Model& m, const ToolCallId& id,
                        std::string_view reason);

// ── 冻结-prefix 不可改变性门 ──────────────────────────────────────
//
// `m.ui.frozen`是full-built元素快照的append-only向量；
// 它们的 `hash_id` 在冻结时被标记并且永远不会重新计算，
// 因此底层 ToolUse 的任何 -freeze 后突变都是不可见的，
// 直到 线程 切换/再水化。因此，通过 ToolCallId（
// ToolExecOutput / apply_tool_output、
// ToolExecProgress、ToolTimeoutCheck、
// PermissionReject / mark_tool_rejected）定位工具的突变
// 位点必须拒绝接触其封闭消息索引 < frozen_through 的任何工具。
//
// `with_live_tool`是通过id改变工具的唯一方法。它搜索 仅 和
// 实时 末尾 [frozen_through .. end) 并在突变运行时
// 返回 true。 过期 id（轮次已被冻结的工具）返回 false — 调用者将
// 其视为 no-op，与 apply_tool_output 的现有
// "idempotent on terminal" 行为相匹配。
//
// 重复的 ID：ToolCallId 应该是唯一的，但提供商打破了这一点
// （参见 update/stream.cpp 中的 `uniquify`
// 守卫），并且实时 末尾 通常在一回合内保存 SEVERAL 助理消息 —
// kick_pending_tools 为每个 sub-turn 附加
// 一个新的占位符。当两个 调用 共享一个 id 时，匹配第一个找到的工具会将
// 第二个工具的结果路由到第一个工具已经的 -terminal 卡上，
// 其中 apply_tool_output 将其作为后期重复项丢弃：该工具
// 确实运行，其输出消失，并且该卡保持运行状态，直到 330 s 楔形
// 网络使其失败。因此，更喜欢第一个 NON-terminal 匹配，
// 并且只有在没有实时 调用 携带该 id 时才回退到 终端 匹配。
// 每个呼叫者都希望这样：无论如何，四个 exec/permission 站点都会
// 在 终端 上保释，因此第一个 NON-terminal 匹配始终是更好的猜测。
// 回调被调用为`f(ToolUse&)`。 `ToolMutator` 引脚形状错误，
// -signature lambda 是 调用 站点上的一个干净概念错误，
// 而不是循环内的模板-depth 错误。
template <class F>
concept ToolMutator = std::invocable<F&, ToolUse&>;

template <ToolMutator F>
bool with_live_tool(Model& m, const ToolCallId& id, F&& f) {
    ToolUse* settled = nullptr;   // 第一场 终端 比赛 — 仅 回退
    for (std::size_t i = m.ui.frozen_through;
         i < m.d.current.messages.size(); ++i) {
        for (auto& tc : m.d.current.messages[i].tool_calls) {
            if (tc.id != id) continue;
            if (!tc.is_terminal()) {
                std::forward<F>(f)(tc);
                return true;
            }
            if (!settled) settled = &tc;
        }
    }
    if (settled) {
        std::forward<F>(f)(*settled);
        return true;
    }
    return false;
}

// 将 工作线程 的消息路由到发送它的确切执行。 `seq`
// 是 工作线程 启动时使用的 Running::exec_seq。
// 执行不再运行的标记消息来自 工作线程，其 调用 为 已
// 取消 或已解决；该ID可能属于NEW 调用（ID在各个回合
// 中不是唯一的），因此它被整个丢弃。掉落时返回 false。
// seq 0=未标记：回退到id路由(with_live_tool)。
template <ToolMutator F>
bool with_exec_tool(Model& m, const ToolCallId& id, std::uint64_t seq,
                    F&& f) {
    if (seq == 0) return with_live_tool(m, id, std::forward<F>(f));
    for (std::size_t i = m.ui.frozen_through;
         i < m.d.current.messages.size(); ++i)
        for (auto& tc : m.d.current.messages[i].tool_calls)
            if (tc.id == id)
                if (auto* r = std::get_if<ToolUse::Running>(&tc.status);
                    r && r->exec_seq == seq) {
                    std::forward<F>(f)(tc);
                    return true;
                }
    return false;
}

[[nodiscard]] inline bool tool_exec_is_current(Model& m,
                                               const ToolCallId& id,
                                               std::uint64_t seq) {
    return with_exec_tool(m, id, seq, [](ToolUse&) {});
}

// ── 人数 -domain 状态更新函数 ──────────────────────────────────────────────────
// 每片 `Msg` 一个。 update.cpp的top-level
// std::visit调度一个Msg到下面匹配的状态更新函数；
// 每个 状态更新函数 对其域 变体 都有自己的访问权限，并在自己的 TU 中实例化。
// 向一个域添加叶子只会重新编译该域的 TU 加上 msg.hpp 的
// 下游 包含器，而不是其他九个 状态更新函数。
//
// 两个 SHAPES，转换过程中：
//
//   Cmd f(Model&m, DomainMsg) ← jaal的，什么都变成了 步骤f
//   (Model m, DomainMsg) ← 老一对-returning一个
//
// update.cpp分派到任一者（它检测形状），因此域可以自行
// 移动而无需286-site 标志日。配对形式使 `deps()`
// 保持活力：返回其模型的 状态更新函数 无法将其 副作用 也描述为值，
// 因此它会到达 接口边界。转换域使其 副作用 可表达 —
// 请参阅 docs/design/jaal-rewrite.md.
//
// 到目前为止已转换：全部 23 个。
//
// TEST SHIM。测试直接到达 `update(Model, Msg)` 和 调
// 用 域 状态更新函数，通常将 `.first` 链接到下一个 调用。将大约 35
// 个此类站点重写为 in-place 形式将导致无法测试任何内容，
// 因此 `step()` 将转换后的 状态更新函数 调整回一对：
//
//     auto s = step(detail::tool_update, std::move(m), msg);
//     // s.first == 新型号 //
//     s.second == Cmd
//
// 它与update.cpp使用的适配器相同，但指向不同的方向。 src/ 调用
// 里什么都没有。
template <class R, class... A>
[[nodiscard]] inline std::pair<Model, Cmd> step(R reducer, Model m, A&&... a) {
    Cmd cmd = reducer(m, std::forward<A>(a)...);
    return {std::move(m), std::move(cmd)};
}

Cmd  composer_update      (Model& m, msg::ComposerMsg       cm);
Cmd  stream_update        (Model& m, msg::StreamMsg         sm);
Cmd  tool_update          (Model& m, msg::ToolMsg           tm);
Cmd  tool_output_update   (Model& m, msg::ToolOutputMsg     tm);
Cmd  providers_update(Model& m, msg::ProvidersMsg pm);
Cmd  models_update  (Model& m, msg::ModelsMsg     pm);
// 选择 mid-turn 的模型，一旦阶段空闲(publish_derived)就应用。
Cmd  apply_pending_switch(Model& m);
// 共享的fused-row构建器（SSOT用于状态更新函数+视图）：将
// 经过认证的提供者枚举到目录（+un-authed标志-in提供），
// 应用当前fused_picker查询，并返回ordered/sectioned
// FusedRow列表。视图渲染它并导出可视光标； 状态更新函数从中选择。
[[nodiscard]] std::vector<FusedRow> fused_rows_for_model(const Model& m);
// Re-enumerate打开模型选择面板的提供者和行(
// 当关闭时没有-op)。对于更改登录提供商的回复。
void refresh_open_model_picker(Model& m);
Cmd  thread_list_update   (Model& m, msg::ThreadListMsg     tm);
Cmd  palette_update       (Model& m, msg::PaletteMsg pm);
Cmd  mention_update       (Model& m, msg::MentionMsg mm);
Cmd  symbol_update        (Model& m, msg::SymbolMsg  sm);
Cmd  codeblock_update     (Model& m, msg::CodeBlockMsg      cm);
Cmd  checkpoint_update    (Model& m, msg::CheckpointMsg     cm);
Cmd  rag_settings_update  (Model& m, msg::RagMsg    rm);
Cmd  stats_update         (Model& m, msg::StatsMsg  sm);
Cmd  settings_list_update (Model& m, msg::SettingsListMsg   sm);
Cmd  fork_update          (Model& m, msg::ForkMsg           fm);
Cmd  todo_update          (Model& m, msg::TodoMsg           tm);
Cmd  login_update         (Model& m, msg::LoginMsg          lm);
Cmd  diff_review_update   (Model& m, msg::DiffReviewMsg     dm);
Cmd  smart_mode_update    (Model& m, msg::SmartModeMsg      sm);
Cmd  web_search_update    (Model& m, msg::WebSearchMsg      wm);
Cmd  paste_settings_update(Model& m, msg::PasteSettingsMsg  pm);
Cmd  plugin_edit_update   (Model& m, msg::PluginEditMsg     pm);
// 编辑面板的PluginEdited回复的一半（其写入是cmd::
// edit_plugin 副作用)。 settings_list 拥有叶子并在此处路由 EditPane 回复。
// 在 plugin_edit.cpp 中定义。
Cmd  plugin_edit_result   (Model& m, PluginEdited e);
Cmd  appearance_update    (Model& m, msg::AppearanceMsg     am);
Cmd  sandbox_update       (Model& m, msg::SandboxMsg        sm);
Cmd  meta_update          (Model& m, msg::MetaMsg           mm);

// ── Esc: 后一级 ───────────────────────────────────────────────
//
// 恢复打开该面板的面板（其 完整 状态 — 查询、
// 光标、嵌套父级），然后针对实时模型 REVALIDATE：
// 当子级打开时模型可能已经改变，因此恢复的光标可以
// 指向 now-shorter 过滤列表的末尾。
// 当没有父级时直接关闭（通过 线程 打开）。
//
// 一个 功能，因为每个 -pane 副本之前都是这样漂移的：每次
// 关闭 处理函数 都硬编码一个目的地，该目的地对于一个入口点是正确的，
// 而对于其他入口点是错误的。导航是 STACK，而不是活板门 -
// Esc 会沿着你进来的路走回去。
void ascend(Model& m);

// 从实时模型+目录(meta.cpp)构建智能模式面板。
// 与picker.cpp共享，其在槽分配之后重新打开面板
// ——一个构建器意味着重新打开的面板不能与原始的不同。
[[nodiscard]] form::Form build_smart_form(const Model& m, bool advanced = false);

// 每个设置面板的完整形式，完全按照面板的构建方式构建，
// 用于设置搜索索引。包括高级行。
[[nodiscard]] form::Form rag_form_for_search(const Model& m);
[[nodiscard]] form::Form sandbox_form_for_search(const Model& m);
[[nodiscard]] form::Form web_search_form_for_search(const Model& m);
[[nodiscard]] form::Form paste_form_for_search(const Model& m);
[[nodiscard]] form::Form appearance_form_for_search(const Model& m);
// 以上所有内容，作为设置中心的搜索索引。
[[nodiscard]] std::vector<settings::ListOpen::Hit> settings_search_index(const Model& m);

// ── 该 智能模式配置切入点 ───────────────────────────────
//
// 在读取智能模式的任何地方安装`cfg`。有 三个 持有者，
// 这是唯一知道所有持有者的地方：
//
//   1. store::Settings — 持久化，因此重启后仍有效
//   2. Model::Domain—UI 线程；分类器每轮读取它
//   3. tools::subagent-工作线程-thread副本，因为`task`运行时没有
//                          访问Model处
//
// fan-out 使得该功能变得困难。它曾经在每个 调用 站点上都是
// open-coded，并且该系列中的每个错误都是执行以下三个操作
// 中的一两个的站点：已保存 但从未应用（仅在重新启动后才使用 副作用），
// 已应用但从未推送到子代理（工作线程 根据 过期 策略路由）。
// 调用者不能忘记其未执行的步骤。
//
// 坚持为副作用:"change the config"和
// "write it down"是相同的意图，并且将它们分开是另一对出错的步骤。
// 返回 保存，以便调用者可以对其进行批处理——写入现在是 副作用，
// 而不是发生在 状态更新函数 内部的事情。
[[nodiscard]] Cmd apply_smart(Model& m, smart::RoleConfig cfg);

// ── 估计行数(frozen.cpp) ──────────────────────────────────────────
//
// 预测的消息的RENDERED高度，在终端行中，在`cols`处。
// 馈送rehydrate_frozen的keep-loop，
// 它遍历最新的-first，对这些进行求和，直到它覆盖了行预算。
// 估计值绝不能超过实际渲染高度：over-count 使循环相信它
// 已经提前覆盖了预算并停止，使 LESS 历史记录保持在预期之外，
// 并且对于 渲染器 省略输出的工具，间隙可以是任意大（500-line
// 差异绘制为 7 行）。 -counting以下是安全的。
//
// 单独导出，以便测试可以确定每个工具的估计-vs-render合同；
// 除了生产中的 frozen.cpp 调用 之外什么都没有。
std::size_t estimate_msg_rows(const Message& mm, int cols);

} // namespace detail
} // namespace agentty::app
