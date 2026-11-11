#pragma once
// agentty::app::cmd--effecting方命令运行时问题的工厂。
//
// 这些用agentty-specific胶水包裹maya的Cmd：
// 开始流式传输转弯，执行工具，在转弯结束后推进待处理工具执行。

#include <maya/maya.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "agentty/runtime/model.hpp"
#include "agentty/runtime/cmd.hpp"
#include "agentty/runtime/msg.hpp"
#include "agentty/io/http.hpp"   // http::CancelTokenPtr (run_tool)
#include "agentty/provider/selection.hpp"  // provider::Selection (fetch_models)
#include "agentty/tool/plugin.hpp"         // plugin::ServerSpec (edit_plugin)

namespace agentty::app::cmd {

// ── 人均数 -turn 路径 DECISION ────────────────────────────────
//
// 哪个模型服务于这个回合、付出什么努力以及为什么。 一个 值，
// 由 resolve_turn_routing() 计算一次 — 🧠 卡 渲染 它并 launch_stream
// 调度它。
//
// 两者用于从相同的Model导出它INDEPENDENTLY，
// 各自运行相同的扫描、相同的分类器和相同的工作量缩放器。
// 对两者的评论都是 "same call as launch_stream so card == wire"，
// 保持这一点真实意味着永远在两个站点上以相同的方式传递
// 每个参数。它并没有保持正确：添加到分类器的阈值到达一个站点，
// 而不是另一个站点，因此该卡通告了请求从未采用的路线，
// 唯一注意到的是专门为寻找它而编写的测试。
//
// 对于一个值，无需保持步调。卡 == 传输 不再是被
// 测试的 不变量 而是成为一个身份。
struct TurnRouting {
    // 空模型⇒编排关闭并且简单选择服务轮；
    // 该卡根本不显示。
    bool                    orchestrate = false;
    std::string             model;      // 已解决的战略模型（""=选择）
    Effort                  base = Effort::None;     // BEFORE复杂度缩放
    Effort                  effort = Effort::None;   // AFTER复杂度缩放
    smart::ComplexityScore  cx{};       // 缩放它的分类
    smart::ComplexityScore  cx_text{};  // text-only 分数，用于电梯来源
    bool                    subagents = false;

    // 本轮解决的角色。智能模式过去将主要转向无条件地固定为战略，
    // 因此旗舰服务于每个转向，包括分类器已经评分为琐碎的转向
    // - 实施和实用程序只能通过 `task` 子代理访问，
    // 而许多会话从未产生该子代理。该角色现在遵循复杂性层（由
    // RoleConfig::main_turn_floor
    // 限制），上面的 `model`/`effort`
    // 是该角色的配置文件。所以记录的卡、日志行和转头文件
    // 都与实际使用的传输的角色名称相同。
    smart::ModelRole        role = smart::ModelRole::Strategic;

    // 作出关于提示词的决定。持有这样的卡可以 NAME
    // 为何某一层被提升（载荷/继续/更正），
    // 而无需重新-walking成绩单并冒着扫描与上面的偏差的风险。
    std::string             prompt;
};

// THE路由决策为即将启动的轮次。纯：读取成绩单、
// 目录 和 m.d.smart，但不写入任何内容。
//
// 即使在 DEFERRED 主动路径上，在提交时对 调用 也是安全的，
// 在启动时读取也是安全的，其中检索在其间拼接上下文消息：最新的-user扫描跳过主动-context、
// 🧠-card和fork-note消息，这是可以落在该窗口中的所有内容。
// smart_routing_card_test 将其固定。
[[nodiscard]] TurnRouting resolve_turn_routing(const Model& m);

// 改变`m`以在m.stream.cancel中安装新的取消令牌，
// 然后在工作线程上分派流式传输任务。 Esc (CancelStream) 翻转
// 令牌以中止 in-flight 数据流。
[[nodiscard]] Cmd launch_stream(Model& m);

// 为即将启动的回合构建智能模式ROUTING CARD，
// 或者当编排关闭时构建智能模式std::nullopt（无显示）。
// 调用者在助理占位符之前插入一条合成的 传输-inert 消息 (
// smart_routing=true)，以便用户可以看到，
// 作为第一个 -class 线程 事件，轮流被路由到哪个模型 + 工作、
// 缩放它的分类复杂性以及哪些层处于活动状态。纯的;铸造新鲜的 MessageId。
// 渲染 `routing` — launch_stream 调度的
// 相同 值。
[[nodiscard]] std::optional<Message> build_smart_routing_card(const Model& m);

// 模型在下一个请求上实际看到的内容：应用任何Thread::
// CompactionRecord替换（最新记录的摘要替换
// 传输上的消息[0..up_to_index)）。
// 镜像 launch_stream 的正常 -turn 分支所提供的内容。
// 当调用者需要推断 传输 载荷 大小或形状时，使用此方法 —
// auto-compaction 触发器特别需要估计
// COMPACTED 前缀，而不是原始转录本，否则他们会在每次
// 压缩后立即重新执行 -fire。
[[nodiscard]] std::vector<Message> wire_messages_for(const Thread& t);

// 要求模型总结/compact的`t`的请求：传输视图(
// 修剪以适合`context_max`)加上默认总结提示词。
[[nodiscard]] std::vector<Message> compaction_request_messages(const Thread& t,
                                                              int context_max);
// 模型回复的摘要文本（去除<summary>标签）。
[[nodiscard]] std::string summary_from_reply(std::string_view reply);

// 传输-only 修剪以适合 `ceiling` 估计的令牌。
// 保留头部、最新的用户消息和最新的消息；丢弃其余的oldest-first，
// 只有在万不得已的情况下才丢弃头部。结果始终以用户打开。
void soft_trim_to_ceiling(std::vector<Message>& v, int ceiling);

// 字节-based 前缀标记估计是针对 传输 视图计算的（i.e.
// 应用了压缩替换）。与 `estimate_prefix_tokens(Thread)`
// 的近似值相同，但 auto-compaction 逻辑
// 和上下文-gauge 的分母是正确的。
[[nodiscard]] int estimate_wire_tokens(const Thread& t);

// 一个工具分派的新的、process-unique标签。将其标记在
// ToolUse::Running 上并将相同的值传递给 run_tool。
[[nodiscard]] std::uint64_t next_tool_exec_seq() noexcept;

[[nodiscard]] Cmd run_tool(ToolCallId id,
                                      ToolName tool_name,
                                      nlohmann::json args,
                                      http::CancelTokenPtr cancel = {},
                                      std::uint64_t exec_seq = 0,
                                      // 指纹的定义已获得同意；
                                      // re-checked 紧接着
                                      // 执行之前。 0 跳过检查。
                                      std::uint64_t approved_def_hash = 0,
                                      // 模型已经可以看到正文的技能，
                                      // 来自skills::
                                      // active_in(visible_text(线程))。
                                      // `skill`工具读取它以进行重复数据删除。
                                      std::vector<std::string> active_skills = {},
                                      // 调用服务于线程，用于CallContext::
                                      // conversation。
                                      std::string conversation = {});

// 检查最新的助理轮次，并在工具结果输入后启动 待处理 工具 调用、
// 请求 权限，或者启动 follow-up 数据流。
// 变异`m`(设置阶段，可以推送占位辅助消息）。
[[nodiscard]] Cmd kick_pending_tools(Model& m);

// 解决后台助理消息中的每个PENDING *抢救的*工具调用，
// 即字节-duplicates a 调用已经在同一代理回合中较早地终端，
// 将其标记为Failed-without-side-effects而不是让它第二次运行。
// 回收的 调用（当弱本地模型将工具 调用 泄漏到 `content` 通道时，
// 由 OpenAI-compat 传输层 生成的合成 `call_salvaged_`
// id）是唯一经过重复数据删除的 - 结构化 调用 是模型的故意意图。
// 返回已删除重复数据的数量。在任何升级到跑步之前由
// kick_pending_tools 调用；暴露进行测试。
std::size_t dedup_releaked_salvage_calls(Model& m);

// ── Path-aware 并行工具调度( 纯度; 暴露测试) ────────────
// 给定一批工具调用 (模型一回合的发射）和已经RUNNING的集合，
// 决定当前-pending 调用可以在这个时间点被提升来运行
// CONCURRENTLY。该决策通过 PATH 分析细化了粗略
// 的 副作用-only 规则 (is_parallel_safe)：
// 两个不相交文件的写入器（或者读取 a.c 并写入 b.c）
// 并行运行而不是串行运行。冲突（重叠路径、任何 Exec 或无法
// 提取路径的编写器）将保留 待处理，并在阻止者解决后继续进行下一个踢。
// 提交顺序被保留：调用 永远不会跳到较早的冲突 调用 之前。
// `running` 和 `pending` 索引到同一逻辑批次；
// 返回 `pending` 索引的子集，现在可以安全开始。
struct SchedDecision {
    std::vector<std::size_t> promote;   // 待处理 指数开始此报价
};
[[nodiscard]] SchedDecision schedule_parallel_batch(
    const std::vector<ToolUse>& batch);

// ── Doom-loop 断路器(纯度;供测试使用暴露) ──────────────────────
// 弱本地模型（qwen2.5-coder、codellama等）通常会陷入
// 非-converging工具循环：它们为目标选择错误的工具（e.g.
// `read`在URL上或不存在的文件上），得到错误结果，并无限期地重新
// -issue一个near-identical 调用。如果没有本机完成信号，
// 主代理循环将旋转，直到用户点击 Esc——"tool usage is fucked"
// 报告背后的症状。镜像迭代/repeated-failure 覆盖了每个严肃的
// local-agent 框架船舶（Qwen-Agent、aider、cline）。
//
// 给定CURRENT代理轮次的完整消息历史（自最后一个真实用户消息
// 以来的运行），当循环应该强制-stopped或std::nullopt
// 继续进行时，返回非-empty微移字符串。两个触发器，
// 都以 REPETITION 为键——同样的 调用 带有 字节-identical
// 参数——因为这就是卡住模型和繁忙模型的区别：
//   (1)REPEAT:相同的(工具，args)调用出现>=kRepeatLimit
//       次并且其结果是失败——模型卡在re-trying和死调用上。
//       ALWAYS被强制执行(每个生产工具都这样做：aider的
//       max_reflections、MindStudio的"2–3 attempts then stop")。
//       真正在死亡的 调用 上循环的强大模型也能从中受益。
//   (2)SUCCESS-REPEAT：相同的（工具，参数）调用
//       SUCCEEDS在一次运行中≥6次——模型re
//       -reading具有相同参数的相同路径六次并且仍然不说话将不会在第七次收敛。
//       也始终执行。
//
// 故意存在NO步帽。工具转动的原始计数并不能证明任何事情：在大型
// 代码库 上进行合法的搜索→读取→编辑→验证运行需要花费数十个不同的、
// 高效的步骤。 Claude 代码的 max_turns
// 默认情况下是无限制的，出于同样的原因，助手永远不会执行 -caps。
// 旧的版本还通过模型 -id 启发式进行门控，
// 因此相同的转录本在本地模型上停止并在 Claude 上运行良好。
// 两个优势（#90）。 首次 时间，连续达到极限时，
// 仅断路器 NUDGES：解释会附加到有问题的 调用
// 的工具结果中，并且运行会继续，因此模型有机会更改参数、
// 切换工具或答案 — 而不是运行处于空闲状态并看起来冻结，
// 直到用户再次键入。仅当模型重复相同的 调用 之后 时，
// 断路器 STOP 才会运行；然后，文本将在最后
// 一个助理轮流中浮出水面（用户会明白原因）。
// 拒绝总是停止：用户拒绝了调用，轻推也无济于事。
struct LoopBreak {
    std::string reason;     // user/model-facing解释
    bool        stop = true;   // false =轻推：注释`target`，继续
    ToolCallId  target{};      // 仅微移：结果携带它的 调用
};
[[nodiscard]] std::optional<LoopBreak> agent_loop_should_break(
    const std::vector<Message>& messages);

// 获取型号目录。
//
// Three-argument形式采用了工作线程需要INSTEAD来实现的：正文用于
// 从工作线程 线程进行调用 provider::active()、
// auth_snapshot()和active_provider_id()，
// 与UI 线程的提供商切换竞争——这就是为什么auth_snapshot() 根本需要 互斥量。
// jaal对于任务正文的规则是它需要的一切都是参数（
// docs/concurrency.md§4.6)，并且UI 线程上的状态更新函数
// 简单地知道这些值。
//
// `for_provider`是状态更新函数在DELIVERY
// 时间比较的陈旧标记，因此在交换机被丢弃之后登陆的目录
// 而不是针对错误的提供商安装。
//
// 用选择本身声明：provider::active()已经
// 返回在选择互斥量下获取的by-value 快照，
// 因此将一个交给工作线程正是它的用途。
// 凭据在工作线程上解析为`sel`。
[[nodiscard]] Cmd fetch_models(provider::Selection sel,
                                          std::string        for_provider);

// 常见情况：获取MODEL的活动提供者。解析来自 `m`
// 的调用 (UI) 线程 的参数并转发。 调用 这个来自
// 状态更新函数；上面的重载适用于已经拥有它们的调用者。
[[nodiscard]] Cmd fetch_models(const Model& m);

// 获取SPECIFIC提供商的目录而不切换到它(融合选择面板 fan-out)。
// 调度 FusedCatalogLoaded{规格，型号，确定}。
[[nodiscard]] Cmd fetch_models_for(std::string spec);

// Re-measure当主动提供者是本地OpenAI-compatible端点(llama.cpp路由器、
// LM Studio、Ollama)时`model_id`在活动提供者上的实时上下文窗口。
// 调度 ModelWindowProbed。适用于托管提供商的 no-op Cmd。
[[nodiscard]] Cmd probe_model_window(const Model& m, std::string model_id);

// ── 自行-update 数据 ─────────────────────────────────────────────
// 后台释放检查(24h-cached，从不阻塞帧)：调度UpdateCheckDone。
// check_for_update() 每次发射时都可以安全发射。
[[nodiscard]] Cmd check_for_update();
// 下载+原子安装给定版本；发送 UpdateApplied。
[[nodiscard]] Cmd perform_self_update(std::string version);

// ── 内 -app 登录 弹出面板 ──────────────────────────────────────────────────
// Fire-and-forget:向平台浏览器开启器发起攻击。包裹在 Cmd::task 中，
// 因此楔形的 xdg-open / 打开 / ShellExecute 永远无法阻止
// 状态更新函数 滴答声。
[[nodiscard]] Cmd open_browser_async(std::string url);

// 创建PKCE验证者+状态并授权URL用于Anthropic OAuth登录；
// 回复 LoginOAuthMinted。
[[nodiscard]] Cmd mint_oauth_login();

// 从 UI 线程 运行 OAuth 代码-exchange HTTP POST。
// 完成后分派 LoginExchanged{result}，无论 success/failure
// 为何 — 状态更新函数 与 `expected<OAuthToken, OAuthError>` 匹配，
// 以决定是否安装信用或转换为失败。
[[nodiscard]] Cmd oauth_exchange(auth::OAuthCode    code,
                                            auth::PkceVerifier verifier,
                                            auth::OAuthState   state);

// 运行 UI 线程 上的 OAuth 刷新 HTTP POST。
// 当 `auth::take_pending_refresh()` 返回
// 隐藏的令牌时从 `AgenttyApp::init()` 调度（
// i.e.on-disk 信用已过期，但具有刷新令牌）。运行时，TUI 已经绘制出来，
// 因此用户会在底行看到粘性的 "refreshing OAuth token…"
// toast，而不是旧的 pre-TUI stderr 行，
// 并且启动不再在网络往返中被门控。
[[nodiscard]] Cmd refresh_oauth(std::string refresh_token);

// 读取 Anthropic OAuth 令牌，如果在约 5 分钟内失效则刷新它。
// 刷新时回复TokenRefreshed，否则OAuthRefreshNotDue。
// 呼叫者设置m.s.oauth_refresh_in_flight；两个回复都清楚了。
[[nodiscard]] Cmd refresh_oauth_if_due();

// 一辆401停放着数据流。读取存储的 Anthropic 刷新令牌并用它刷新。
// 无论哪种方式回复 TokenRefreshed：成功恢复 数据流，
// "no refresh token stored" 失败并显示 Sign-in 提示。
[[nodiscard]] Cmd refresh_oauth_for_401();

// 为ChatGPT登录尝试分配进程-unique身份。 Async
// progress/completion 必须携带它，以便放弃的尝试无法改变较
// 新的登录 弹出面板。
[[nodiscard]] std::uint64_t next_codex_login_attempt_id() noexcept;

// 连接-probe自定义主机关闭UI 线程：拨打其型号列表（配置路径→/
// v1/models→Ollama/api/tags），检测方言，
// 分派 HostProbed。共享 next_codex_login_attempt_id()，
// 因此 过期 探测结果（用户 Esc 已提交/重新提交）会被 状态更新函数 丢弃。
// 工作线程 自行解析主机的 已保存 密钥。
[[nodiscard]] Cmd probe_host_async(std::string spec, std::uint64_t attempt_id);

// 登录工作线程是SUBSCRIPTIONS，不是Cmds。
//
// 当用户登录时，两者都会阻止 -poll 提供者长达 900 秒，
// 并且两者都必须在用户按下 Esc 时停止。这正是键控源的用途：
// 当 弹出面板 启动时 subscribe() 返回 1，
// 当键停止返回时 jaal 触发 正文 的 stop_token
// — 因此关闭 弹出面板 IS 取消 工作线程，
// 而不是必须记住的第二件事。 （这没有被记住：他们在 Esc 之后
// 轮询的命令中，每次放弃的尝试都会泄露 线程。）
//
// jaal还丢弃来自停止生成的消息，因此迟到的结果永远不会落在继续
// 前进的模型中。无论如何，attempt_id 都会留下来——这就是
// 状态更新函数 的比赛，而且不需要任何费用。
[[nodiscard]] Sub codex_login_sub(std::uint64_t attempt_id);

// Provider-generic设备流程（GitHub副驾驶，Kimi，……）。
// `provider` 是 注册表 id，`provider_label` 是成功 toast 的显示名称。
// 调度 DeviceCodeReady，然后调度 DeviceLoginDone。
[[nodiscard]] Sub device_login_sub(
    std::string provider, std::string provider_label, std::uint64_t attempt_id);

// 每 线程 JSON 走 ~/.agentty/threads/ 和 解析 离开 UI 线程。
// 完成后调度 `ThreadsLoaded{vec}`。在 real
// -world 中使用数百个 multi-MB 文件时，目录 walk
// + 解析 可能需要几秒钟的时间，因此它作为后台任务运行，而不是阻止启动；
// `init()` 立即返回，并带有空的 线程 列表。
[[nodiscard]] Cmd load_threads_async();

// Ask是线程的短标题的廉价模型，来自其第一条用户消息和回复，
// 聊天应用程序命名聊天的方式。关闭 UI 线程；
// 调度 ThreadTitleGenerated（任何失败时标题为空）。
[[nodiscard]] Cmd generate_thread_title(const Model& m);

// 完成后，Connect/snapshot、MCP 服务器将关闭 UI 线程 和 分派
// `PluginsUpdated{model}` — 状态更新函数 将其存储在 m.ui.plugins 中，
// 这是插件面板 渲染 的单一来源。重新连接=true重生+re-handshakes（
// add/remove/toggle/first打开）； reconnect=false
// 只是 快照 现场 池。镜子 load_threads_async。
[[nodiscard]] Cmd load_plugins_async(bool reconnect);

// 读取UI 线程上已安装的技能、批准、命令和挂钩状态；答案 LibraryLoaded，
// 状态更新函数 存储在 m.ui.library 中。
[[nodiscard]] Cmd load_library();

// 渲染一条MCP `/prompt args`线断了UI 线程（它询问服务器）；
// 回答SlashPromptRendered，提交文本。失败时为空文本。
// 离开线路的必需参数永远不会到达服务器：回复是标记为
// `usage`的one-line使用说明，状态更新函数敬酒
// 而不是提交。
[[nodiscard]] Cmd render_slash_prompt(std::string typed);

// 设置面板的add-mode创建：插入用户mcp.json的插件行，
// 或者启动器command/agent文件。将磁盘从 归并 中写入并用
// SettingsAddDone 应答。
[[nodiscard]] Cmd settings_add(settings::Category concern, std::string line);

// 解析单个线程的JSON关闭UI 线程。从 线程 选择面板 的
// Enter 处理函数 发送，因此同步 ~30ms
// -per-thread 解析 不会落在按键和下一次绘制之间。
// 成功时调度 `ThreadLoaded{thread}`；
// 失败时（文件消失，解析错误）会调度一个带有空线程的`ThreadLoaded`，
// 这样状态更新函数就可以优雅地no-op，
// 而不会让`thread_loading` 标志卡住。
[[nodiscard]] Cmd load_thread_async(ThreadId id);

// 读取线程以获取^J窥视，关闭UI 线程，并发送ThreadPeekLoaded。
// 只写到：与 load_thread_async 不同，它 授权 什么都没有，
// 因为看着 线程 并不能打开它。
[[nodiscard]] Cmd load_thread_peek_async(ThreadId id);

// 列出本机上的 Codex/Claude 代码会话为 选择面板，关闭 UI 线程，
// 并发送 ForeignSessionsLoaded。 `previous`
// 是最后一次扫描；此后未更改的文件不会再次被读取。
[[nodiscard]] Cmd load_foreign_sessions_async(foreign::Scan previous);

// 打开另一个代理的会话：将其整体读取，将其保存作为agentty 线
// 程并发送ThreadLoaded。之前导入时（存在具有此 id 的
// agentty 线程），自 agentty 中添加的回合在
// 刷新会话后将保留。
[[nodiscard]] Cmd open_foreign_async(ThreadId id, Thread::Import im);

// 查看轮流查看尚未打开的会话。
[[nodiscard]] Cmd load_foreign_peek_async(ThreadId id, Thread::Import im);

// 为UI 线程上的一批旧线程填写选择面板汇总并发送
// ThreadSummariesFilled。当这一批着陆时，状态更新函数
// 会开始下一批，因此一次只能运行一个。
[[nodiscard]] Cmd backfill_summaries_async(std::vector<ThreadId> ids);

// Re-sync将RAG猎犬带到`cfg`关闭UI 线程。 副作用，
// 所以 状态更新函数 RETURNS 这个而不是调用检索器本身：
// update() 仍然是 (Model, Msg) 的纯函数，
// 并且该工作在所有其他后台作业运行的地方运行 - 在 jaal 的 池、
// 已取消 上并等待内核自己的关闭。不发送任何内容；检索器是它自己的状态。
[[nodiscard]] Cmd apply_rag_settings(store::RagConfig cfg);

// Ask活体检索器关于其嵌入器的了解，对于在探针代`gen`处打开
// 的RAG 面板。检索器与工具 线程 共享，因此在 工作线程 上读取它。
// 答案 RagEmbedStatusRead。
[[nodiscard]] Cmd read_rag_embed_status(std::uint64_t gen);

// 在第一个请求之前打开到活动提供者的主机的TLS连接，
// 因此用户的第一轮跳过握手。
//
// EFFECT，因此状态更新函数返回该值而不是拨打自身。
// 目标从刚刚设置的 状态更新函数 同步解析为 此处 —
// prewarm_target() 是纯的 — 并且只有表盘在
// 工作线程 上运行。这种划分很重要：解析 工作线程 会在稍后的某个
// 时刻读取活动的提供者，在快速的第二次切换可能已经改变了它之后。
// 不发送任何内容。
[[nodiscard]] Cmd prewarm_provider(const Model& m);

// Write一改为mcp.json，关闭UI 线程，并报告回来。
//
// 写入是读取+JSON重写+原子重命名，所以状态更新函数一定不能这样做。
// 它会返回 this； 工作线程 执行匹配的
// tools::plugin::*() 调用 并分派 PluginEdited
// 和结果，状态更新函数 的 处理函数 for PluginEdited
// 执行 调用 内联的操作（重新加载插件模型，吐司，关闭 面板，
// 显示字段错误）。 `reply` 是回显的请求，并填写了结果，
// 因此即使 面板 继续前进，处理函数 也会准确地执行所请求的内容。
//
// `spec`仅在添加/更新时被咨询。
[[nodiscard]] Cmd edit_plugin(std::filesystem::path path, PluginEdited reply,
                              tools::plugin::ServerSpec spec = {});

// Write `parent`的成绩单进行fork，关闭UI 线程，
// 并用读取工具打开其目录。回复 ForkTranscriptWritten。
[[nodiscard]] Cmd write_fork_transcript(Thread parent, fork_panel::Choice choice);

// 弄清楚为什么剪贴板读取没有得到答复（mosh、tmux 配置、ssh）并回复
// ClipboardDiagnosed。遍历 /proc 并询问 tmux，因此是 工作线程。
// 在cmd_clipboard_diag.cpp年。
[[nodiscard]] Cmd diagnose_clipboard(bool in_ssh);

// 通过richpaste在工作线程上行走的路线读取剪贴板并回复ClipboardRead。
// 于cmd_clipboard_diag.cpp年。
[[nodiscard]] Cmd read_clipboard();
// 终端保持安静（或拒绝）：在工作线程上将行走移至其下一条路线，
// 并回复ClipboardRead。
[[nodiscard]] Cmd resume_clipboard(ClipboardWalk walk, std::string why, bool refused);

// 保存粘贴了不能放在作曲器中的文件为-is(参见settle_clipboard_items)，
// 然后用ClipboardRead回复。
[[nodiscard]] Cmd settle_clipboard(std::vector<ClipboardItem> items);

// 检查粘贴的路径是否为图像文件命名并读取，回复ImagePathSniffed。
// 于cmd_image_paste.cpp年。
[[nodiscard]] Cmd sniff_image_path(std::string text);

} // namespace agentty::app::cmd
