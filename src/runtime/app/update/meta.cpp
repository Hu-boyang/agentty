// meta_update — 状态更新函数 费用 `msg::MetaMsg`
// 届会 -level 事件 不属于任何单个域: Tick clock +
// 数据流-stall 看门狗 + 符号 -rate 取样机 剖面循环
// 对话压缩启动 scroll/toggle 状态 -toast 清净戒戒.

#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/app/update.hpp"
#include "agentty/domain/smart_mode.hpp"   // kSmartModeRows 移动

#include <algorithm>
#include <chrono>
#include <utility>

#include <maya/core/overload.hpp>
#include <maya/core/motion.hpp>   // anim::keep_animating — 帧 请求

#include "agentty/runtime/app/cmd_factory.hpp"
#include "agentty/util/update.hpp"   // self_update_possible
#include "agentty/runtime/app/deps.hpp"
#include "agentty/provider/selection.hpp"   // provider::active (上钩) 看门狗)
#include "agentty/provider/ollama/transport.hpp"  // effective_num_ctx
#include "agentty/provider/openai/transport.hpp"  // endpoint_speaks_native_cached
#include "agentty/runtime/composer_attachment.hpp"
#include "agentty/runtime/mem.hpp"
#include "agentty/runtime/panel/settings/items.hpp"      // ascend(): 重新验证已恢复的列表
#include "agentty/runtime/settings_registry.hpp"   // 调制行写入-back
#include "agentty/runtime/view/helpers.hpp"   // ui::profile_label
#include "agentty/runtime/view/palette.hpp"   // ui::palette_context
#include "agentty/store/store.hpp"
#include "agentty/tool/subagent.hpp"   // set_smart:任务路由器本身的复制件
#include "agentty/tool/tool_plan.hpp"  // CycleToolLoading(#98) 页面存档备份,存于互联网档案馆,2009年7月1日.
#include "agentty/workspace/checkpoint.hpp"
#include "agentty/runtime/panel/smart_form.hpp"
#include "agentty/runtime/app/settings_cache.hpp"
#include "agentty/runtime/view/helpers.hpp"

namespace pn = agentty::ui::panel;

namespace agentty::app::detail {

using maya::overload;

// 解决三个角色档次 DISPLAY 建造 面板.
//
// 分辨率为提供者 -scoped 喜欢 传输 路径,
// 所以在另一个提供者下制作的针头显示自动 -fill 最终点
// 无法成为的模型 数据流。这个检查需要目录, 这就是为什么它
// 住在这里而不是在 smart_form.cpp - 档案是纯的
//
// 与 picker.cpp ,该程序重新打开 面板 指定槽
// 后:一个建构者表示重新打开 面板 与原样没有区别。
form::Form build_smart_form(const Model& m, bool advanced) {
    const auto& sm = m.d.smart;
    const std::string parent = m.d.model_id.value;

    auto slot = [&](smart::ModelRole role) {
        auto rp = smart::resolve_role(role, parent, m.d.effort,
                                      m.d.available_models, sm,
                                      active_provider_id(m));
        std::string label = ui::pretty_model_label(rp.model);
        smart_form::SlotView v;
        v.label  = label.empty() ? rp.model : label;
        v.pinned = sm.enabled && sm.slot(role).set;
        return v;
    };

    smart_form::Inputs in;
    in.enabled = sm.enabled;
    // 内线是 LOCK ,而不是迟到的拒绝:行 渲染 读 -only
    // 并命名变量,而不是 寻找现场和敬酒 Enter.
    if (m.env.settings.smart_enabled)
        in.enabled_lock = "env: AGENTTY_SMART_MODE";
    in.strategic      = slot(smart::ModelRole::Strategic);
    in.implementation = slot(smart::ModelRole::Implementation);
    in.utility        = slot(smart::ModelRole::Utility);

    // 高级行从设置的持久配置直接预测
    // 注册表 ——值,范围,标签,帮助,
    // 内存 锁 和来源都来自排。
    in.advanced = advanced;
    in.smart = m.d.smart;
    in.env = m.env.settings;
    return smart_form::build_form(in);
}

int resolved_context_max(const Model& m, std::string_view provider_id) {
    // 如果目录携带一个,供应商为THIS模型广告的窗口。
    // 0意味着没有报告任何情况——一个探测器从未运行过,
    // 或者网关的/v1/models行没有说出大小。
    int advertised = 0;
    for (const auto& mi : m.d.available_models) {
        if (mi.id == m.d.model_id) { advertised = mi.context_window; break; }
    }
    const int resolved =
        ui::resolve_context_window(provider_id, m.d.model_id.value,
                                   advertised, m.d.persisted,
                                   m.env.max_context_tokens);

    // Ollama 其分配的重量为: KV 缓存 来匹配,
    // 所以 agentty 夹子 num_ctx 最高额,实际上 加载 电脑上,
    // 而不是OOMing服务器中间 -request.
    //
    // 电话 GAUGE 不得不同意这个夹子。 传输 发送 num_ctx
    // =32768 当这个返回模型的未解密的131072,
    // 所以上下文栏读取了四分之一的真实使用和自动 -compaction
    // 等了四遍了 等了四个月 线程 模特儿悄悄地切开自己的车 提示词
    // 很久以前,我一直想着 agentty 认为要紧凑...
    // "local models forget everything"
    // 内在的无形,因为每个层都单独正确
    //
    // 一种功能既能回答两个问题。
    //
    // 不过，只有当主机真正讲 Ollama 的协议时。 llama.cpp
    // 可以在 11434 的 "ollama" 行（#73）下服务；
    // 然后请求转到 /v1，不会发送 num_ctx，并且窗口是服务器
    // 启动时使用的窗口。将其限制为 32k 将 131k 服务器显示为 32k。
    // 尚未询问的主机计为 Ollama，与 分派 匹配。
    if (provider_id == "ollama"
        && provider::openai::endpoint_speaks_native_cached(
               m.d.selection.openai_endpoint).value_or(true))
        return provider::ollama::effective_num_ctx(resolved);
    return resolved;
}

Cmd meta_update(Model& m, msg::MetaMsg mm) {
    return std::visit(overload{
        [&](CompactContext) -> Cmd {
            // 如果转弯已经在飞行中或者压实已经在运行，
            // 则拒绝——下一个 CompactContext
            // 干净地降落在空闲状态。拒绝空的 线程（无需压缩）。
            // 在这两种情况下的Toast：不执行任何操作的手
            // 动压缩读取为损坏的键绑定。
            if (!m.s.is_idle() || m.s.compacting)
                return set_status_toast(m,
                            "cannot compact while the agent is working");
            if (m.d.current.messages.empty())
                return set_status_toast(m, "nothing to compact yet");

            // 压缩是传输-only：我们从不改变转录本。
            // 总结提示词是由`cmd_factory::wire_messages_for_compaction`
            // 动态构建和修剪的（其咨询`m.s.context_max`
            // 并丢弃最旧的回合，直到请求适合窗口的~65%）。
            // 流式回复落在 `m.s.compaction_buffer`，
            // 而不是 `messages`。
            // 用户不断地看到他们曾经经历过的每一个回
            // 合——状态横幅是正在发生压缩的唯一 UI
            // 信号。
            //
            // `compaction_target_index`是结果
            // CompactionRecord将覆盖的边界：
            // 后续请求载荷中，用摘要替换 [0, this) 范围。
            // 在启动时捕获，因此用户在压缩期间提交的轮次（
            // 通过queue-on-compact路径排队）
            // 不会被追溯地折叠到它们不属于的摘要中。
            m.s.compaction_target_index = m.d.current.messages.size();
            m.s.compaction_buffer.clear();

            auto now = m.now;
            phase::Active ctx;
            ctx.started       = now;
            ctx.last_event_at = now;
            m.s.phase     = phase::Streaming{std::move(ctx)};
            m.s.compacting = true;
            m.s.status      = "compacting context\xe2\x80\xa6";   // …
            m.s.status_until = {};   // 粘性直至压实完成
            return cmd::launch_stream(m);
        },

        [&](ToggleChangesStrip) -> Cmd {
            m.d.show_changes_strip = !m.d.show_changes_strip;
            // 坚持下去，以便它在重新启动后仍然存在。
            m.d.persisted.show_changes_strip = m.d.show_changes_strip;
            auto save = save_record(m);
            auto cmd = set_status_toast(m, m.d.show_changes_strip
                ? "changes strip: shown" : "changes strip: hidden (Ctrl+R still reviews)");
            return Cmd::batch(std::move(save), std::move(cmd));
        },

        [&](ToggleAutoTitle) -> Cmd {
            m.d.persisted.auto_title = !m.d.persisted.auto_title;
            auto save = save_record(m);
            auto cmd = set_status_toast(m, m.d.persisted.auto_title
                ? "chat names: automatic after the first reply"
                : "chat names: first line of the first message");
            return Cmd::batch(std::move(save), std::move(cmd));
        },

        [&](CycleToolLoading) -> Cmd {
            namespace tp = tools::plan;
            const auto next = [](tp::Loading l) {
                switch (l) {
                    case tp::Loading::Auto:     return tp::Loading::All;
                    case tp::Loading::All:      return tp::Loading::OnDemand;
                    case tp::Loading::OnDemand: return tp::Loading::Auto;
                }
                return tp::Loading::Auto;
            }(tp::loading_from(m.d.persisted.tool_loading));
            m.d.persisted.tool_loading = std::string{tp::to_string(next)};
            auto save = save_record(m);
            // publish_derived看到记录移动并告诉工具层。
            auto toast = set_status_toast(m,
                next == tp::Loading::All      ? "MCP tools: load all (up to the provider's limit)"
              : next == tp::Loading::OnDemand ? "MCP tools: load on demand, the model searches"
                                              : "MCP tools: auto, load what fits");
            // 插件面板计数取决于模式。
            return Cmd::batch(std::move(save), std::move(toast),
                              agentty::app::cmd::load_plugins_async(/*重新连接=*/false));
        },

        [&](ToggleAutoCompact) -> Cmd {
            m.d.persisted.auto_compact = !m.d.persisted.auto_compact;
            auto save = save_record(m);
            auto cmd = set_status_toast(m, m.d.persisted.auto_compact
                ? "auto-compact: on"
                : "auto-compact: off \xc2\xb7 /compact when you want it");
            return Cmd::batch(std::move(save), std::move(cmd));
        },

        [&](CycleProfile) -> Cmd {
            m.d.profile = m.d.profile == Profile::Write   ? Profile::Ask
                      : m.d.profile == Profile::Ask     ? Profile::Minimal
                                                       : Profile::Write;
            // 配置文件切换re-establishes信任基线；
            // 删除会话 "always allow" 授权，因此收紧到
            // Minimal 实际上是 re-arms 用户期望的 提示词。
            // 也清除 PERSISTED 授权 - 否则它们会在下次启动时复活
            // （从设置中初始化 re-seeds session_grants），
            // 并且 re-arm 会默默地 un-does 本身。
            // 这也是ONLY用户-facing撤销过期总是-allow的方法。
            const bool had_grants = !m.d.session_grants.empty();
            m.d.session_grants.clear();
            m.d.grant_def_hash.clear();   // 哈希值属于那些授权
            m.d.persisted.always_allow_tools.clear();
            // 一个保存，而不是两个：persist_settings
            // 写入了整个记录，因此清除上面的授权就足够了。
            // 首先单独保存它们（就像这样做的）只会为下面的 保存
            // 创建一个窗口来写入仍然包含它们的记录。
            auto profile_save = persist_settings(m);
            // 确认切换——否则配置文件改变是不可见的（
            // 作曲家标签更新，但是键盘-driven循环需要命名新模式
            // +它的作用的反馈节拍）。
            const char* gist =
                  m.d.profile == Profile::Write   ? "auto-approves every tool"
                : m.d.profile == Profile::Ask     ? "asks before writes & commands"
                                                  : "asks before every tool";
            auto toast = set_status_toast(
                m, "profile: " + std::string{ui::profile_label(m.d.profile)}
                     + " \xc2\xb7 " + gist
                     + (had_grants ? " \xc2\xb7 always-allow grants reset" : ""),
                std::chrono::seconds{4});
            return toast;
        },
        [&](RestoreCheckpoint& e) -> Cmd {
            // 倒带=破坏性双重恢复：工作树文件AND和记录都返回
            // 到提交检查点用户轮次之前的瞬间（Zed的
            // "restore checkpoint"）。
            // 仅从空闲状态 - 运行回合持有写入文件的工具 工作线程；
            // 将它们与 checkout-index 进行竞赛会将
            // 快照 字节 与新的工具输出交织在一起。
            if (!m.s.is_idle() || m.s.compacting || m.s.thread_loading)
                return set_status_toast(m, "cannot rewind while the agent is working");
            // 检查点消息必须仍然存在（压缩永远不会删除消息，
            // 因此这仅在 过期 id 上失败）。
            const auto& msgs = m.d.current.messages;
            const bool found = std::any_of(msgs.begin(), msgs.end(),
                [&](const Message& msg) {
                    return msg.checkpoint_id && *msg.checkpoint_id == e.id;
                });
            if (!found)
                return set_status_toast(m, "checkpoint not found in this thread");
            // thread_loading兼作
            // "transcript is about to be rewritten"防护：
            // submit_message排队而不是触发，因此没有消息可以到达在
            // CheckpointRestored时间计算的索引和截断之间。
            m.s.thread_loading = true;
            m.s.status         = "rewinding to checkpoint\xe2\x80\xa6";
            m.s.status_until   = {};
            // 隔离工作线程：大快照上的git checkout-index可以阻塞几秒；
            // 永远不会在共享的 BG 池 上。
            auto cmd = Cmd::task_isolated(
                [](maya::Sink<Msg> out, std::stop_token, CheckpointId id) {
                    CheckpointRestored done;
                    done.id = id;
                    std::string err;
                    done.ok = workspace::restore_checkpoint(id.value, &err);
                    if (!done.ok) done.error = err.empty() ? "restore failed" : err;
                    out.send(Msg{std::move(done)});
                },
                e.id);
            return cmd;
        },
        [&](CheckpointRestored& e) -> Cmd {
            m.s.thread_loading = false;
            if (!e.ok) {
                return set_status_toast(m, "rewind failed: " + e.error,
                                         std::chrono::seconds{6});
            }
            // 文件已经是字节-identical到快照;
            // 现在将抄本切回到检查点用户转弯之前，
            // 并将其文本交还给作曲家进行编辑+重新提交。
            auto& msgs = m.d.current.messages;
            auto it = std::find_if(msgs.begin(), msgs.end(),
                [&](const Message& msg) {
                    return msg.checkpoint_id && *msg.checkpoint_id == e.id;
                });
            if (it == msgs.end()) {
                // 恢复了文件，但消息消失了（不应该发生——分派在
                // 空闲+thread_loading上被门控）。
                return set_status_toast(m, "files rewound (turn already gone)");
            }
            // Composer 补充：旧的 提示词 —标签 的普通
            // -text 渲染 占位符成为它们的可见标签。
            // 原始占位符无法安全地复活（图像 正文 在提交时被耗尽到
            // Message.images；悬空的 标签 请求 400 秒）。
            std::string refill;
            refill.reserve(it->text.size());
            for (std::size_t i = 0; i < it->text.size(); ) {
                if (static_cast<unsigned char>(it->text[i]) == attachment::kSentinel) {
                    auto len = attachment::placeholder_len_at(it->text, i);
                    if (len > 0) {
                        auto idx = attachment::placeholder_index(it->text, i);
                        if (idx < it->attachments.size()) {
                            refill.push_back('[');
                            refill.append(attachment::chip_label(it->attachments[idx]));
                            refill.push_back(']');
                        }
                        i += len;
                        continue;
                    }
                }
                refill.push_back(it->text[i++]);
            }
            const auto cut = static_cast<std::size_t>(it - msgs.begin());
            msgs.erase(msgs.begin() + static_cast<std::ptrdiff_t>(cut), msgs.end());
            // 覆盖 now-deleted 消息的压缩记录是没有
            // 意义的（up_to_index 点超过了末尾）——丢弃它们。
            std::erase_if(m.d.current.compactions,
                          [&](const Thread::CompactionRecord& r) {
                              return r.up_to_index > cut;
                          });
            m.d.current.updated_at = m.wall_now();
            Cmd save_restored = save_current(m);

            reset_composer_draft(m.ui.composer);
            m.ui.composer.text   = std::move(refill);
            m.ui.composer.cursor = static_cast<int>(m.ui.composer.text.size());
            if (m.ui.composer.text.find('\n') != std::string::npos)
                m.ui.composer.expanded = true;

            // 批发内容交换到 SHORTER 转录本 — 与
            // ThreadLoaded/NewThread
            // 恢复相同：重建冻结前缀并重置内联区域（截断的转弯
            // 行位于本机 回滚缓冲区 中，
            // 只有 reset_inline 可以清除它们；用户明确要求倒带）。
            //
            // 也删除整个 渲染 缓存：被截断的 末尾 的
            // 消息消失了（它们的条目永远不会再次冻结——唯一
            // 的 per-entry 删除），并且幸存的前
            // 缀即将被重建到冻结的分类帐中。两者都会在下一个
            // 渲染 上延迟重新填充。
            m.ui.view_cache.clear();
            rehydrate_frozen(m);
            m.ui.needs_warmup_render = !m.ui.frozen.empty();
            release_to_kernel();
            auto toast = set_status_toast(
                m, "rewound \xc2\xb7 files restored, prompt back in composer",
                std::chrono::seconds{5});
            return Cmd::batch(std::move(save_restored), cmd::reset_inline(),
                              std::move(toast));
        },
        [&](TerminalFocus& e) -> Cmd {
            m.ui.terminal_focused = e.focused;
            return Cmd::none();
        },
        [&](TerminalResized& e) -> Cmd {
            // 宽度改变mid-turn:maya现在
            // 重播历史，但是已经滚动的运行回合的行保持
            // 旧的宽度。稳定后再次重播（元勾选）。
            if (e.cols > 0 && m.ui.term_cols > 0 && e.cols != m.ui.term_cols
                && !m.s.is_idle())
                m.ui.replay_after_settle = true;
            // 来自分离tty的瞬态0保持最后的好值。
            if (e.cols > 0) m.ui.term_cols = e.cols;
            if (e.rows > 0) m.ui.term_rows = e.rows;
            return Cmd::none();
        },
        [&](ToggleRetrievedExpanded& e) -> Cmd {
            // 在其紧凑片段形式和完整-passage扩展之间翻转寻址的检索-context卡。
            // Frozen-prefix 门，与每个 id-addressed
            // 突变相同的 freeze-prefix 门：进入 m.ui.frozen
            // 的卡将其扩展状态烘焙到快照元素中（hash_id 在冻结时标记，
            // 从未重新计算），因此在那里突变
            // proactive_expanded 会更改模型状态，而不会到达画布。
            // 只有切换一张LIVE（不是-yet-frozen）卡；
            // 在 回滚缓冲区 上，它没有 -ops，
            // 匹配 "scrollback is archaeology"。
            // 冻结卡的完整 -passage 查看器是 Ctrl+O 覆盖面板，
            // 它从不接触画布。
            auto& msgs = m.d.current.messages;
            for (std::size_t i = m.ui.frozen_through; i < msgs.size(); ++i) {
                if (msgs[i].id == e.id && msgs[i].proactive) {
                    msgs[i].proactive->expanded = !msgs[i].proactive->expanded;
                    break;
                }
            }
            return Cmd::none();
        },
        [&](Tick) -> Cmd {
            auto now = m.now;
            // 自上一个刻度以来的 Wall-clock 间隙 — 保留用于下面的挂起
            // -recovery 变基 (last_event_at += tick_gap)。
            // NOTE:这里不再导出动画dt。现在，每个 Tick
            // -era 动画（加载动画、tok/s 平滑）都是
            // maya 动画框架内的 Clock-driven：加载动画 从
            // 共享动画时钟读取其 帧，而 disp_rate 运动
            // self-ticks 在 vi​​ew-loop 帧 时钟上。
            // 老手-fed dt——以及两个时钟不一致的微妙错误类（
            // view() 先消耗了帧间隔，导致加载动画缓慢。
            // ——也随之消失了。
            if (m.s.last_tick.time_since_epoch().count() == 0) m.s.last_tick = now;
            const auto tick_gap = now - m.s.last_tick;
            m.s.last_tick = now;

            // ── 定期检查释放情况 ────────────────────────
            // 该检查用于在启动时运行ONCE。因此，
            // 保持开放一天的会话（这里的正常情况）因此在运行
            // 时从未了解过版本削减，并且没有 in-app 方式
            // 询问："Update agentty" 调色板条目
            // 是在已经成功的检查上门控的，因此没有网络的启动
            // 让您除了退出和运行 `agentty update`
            // 之外根本没有任何路径。
            //
            // 每小时一次，如 Zed。检查本身是磁盘上的 24h-cached，
            // 因此其中绝大多数都是在 工作线程、线程 和 分派 上
            // 读取的一个小文件，什么也没有。
            constexpr auto kUpdatePollInterval = std::chrono::hours{1};
            if (!m.s.update_in_flight
                && m.s.update_pending_restart.empty()
                && m.s.update_latest.empty()
                && !m.env.no_update_check) {
                if (m.s.last_update_poll.time_since_epoch().count() == 0)
                    m.s.last_update_poll = now;   // 启动检查刚刚运行
                else if (now - m.s.last_update_poll >= kUpdatePollInterval) {
                    m.s.last_update_poll = now;
                    return cmd::check_for_update();
                }
            }

            // ── LOOP 恢复 ───────────────────────────────────────
            // 失败的迭代使循环停在退避最后期限之后。
            // finalize_turn是正常的re-send站点，
            // 但它仅在转弯ENDS时运行——睡眠循环没有转弯结束，
            // 因此Tick（在等待待处理时由animation_demand
            // 保持武装）将其带回来。在截止日期过后并且
            // 会话真正空闲时触发。
            if (m.ui.composer.loop_wait_until_ms > 0 && m.s.is_idle()) {
                const auto now_ms = maya::anim::default_clock().now_ms();
                if (m.ui.composer.loop_ready(now_ms)) {
                    m.ui.composer.loop_note_success();   // 明确期限
                    ++m.ui.composer.loop_iterations;
                    m.ui.composer.text        = m.ui.composer.loop_text;
                    m.ui.composer.attachments = m.ui.composer.loop_attachments;
                    m.ui.composer.cursor =
                        static_cast<int>(m.ui.composer.text.size());
                    auto sub_cmd = detail::submit_message(m);
                    // 将提示词保留在屏幕上（循环时该框被读取-only）—
                    // —与finalize_turn的恢复相同。
                    auto& sc = m.ui.composer;
                    sc.text        = sc.loop_text;
                    sc.attachments = sc.loop_attachments;
                    sc.cursor      = static_cast<int>(sc.text.size());
                    return sub_cmd;
                }
            }

            // 滑动 BIG tok/s 读数。以状态栏显示的相同原始
            // 瞬时速率重新定位平滑运动。 VIEW 读取 disp_rate.get()，
            // 该 -ticks 在共享 帧 时钟上，
            // 并在滑行时保持 帧 处于武装状态 — 这里没有 dt。
            // 当没有任何物体在飞行时，将其拉至0处静止。
            {
                double raw_target = 0.0;
                if (auto* ra = active_ctx(m.s.phase);
                    ra && m.s.is_streaming()
                    && ra->first_delta_at.time_since_epoch().count() != 0) {
                    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  now - ra->first_delta_at).count();
                    if (ms >= 250) {
                        double sec = std::max(0.001, static_cast<double>(ms) / 1000.0);
                        raw_target = (static_cast<double>(ra->live_delta_bytes) / 4.0) / sec;
                    } else {
                        raw_target = m.s.disp_rate.target();  // 保温期间保持-up
                    }
                }
                if (raw_target != m.s.disp_rate.target())
                    m.s.disp_rate.spring_to(raw_target);
            }

            // -freeze后结算窗口倒计时。在解决-freeze之后，
            // 保持蜱事件订阅武装（subscribe.cpp门在其上）几个帧，
            // 以便maya的渲染器在时钟停止之前完全
            // 协调live-tail→冻结崩溃。此处递减，
            // 以便每个渲染的 帧 都会燃烧一个。
            if (m.ui.settle_cooldown_ticks > 0) --m.ui.settle_cooldown_ticks;

            // ── 推迟员额-rehydrate ──────────────────────
            // 由 ThreadLoaded 武装。补水预算步行在
            // ESTIMATED 高度进行；一旦第一个油漆印记了每
            // 个密封块的REAL铺设-out高度（record_paint），
            // 重新-run修剪，以便冻结的窗户受地面实况限制，
            // 而不是估计漂移（工具-heavy 线程在
            // -estimate几个-fold下）。触发一次；
            // 等待每个块都有记录的高度，以便 drop_front 中的
            // 掉落证明门实际上可以释放行。
            Cmd rehydrate_trim = Cmd::none();
            if (m.ui.pending_rehydrate_trim) {
                bool all_recorded = !m.ui.frozen.empty();
                for (std::size_t k = 0; k < m.ui.frozen.size(); ++k)
                    if (!m.ui.frozen.recorded_at(k)) { all_recorded = false; break; }
                if (all_recorded) {
                    m.ui.pending_rehydrate_trim = false;
                    rehydrate_trim = trim_frozen_if_oversized(m);
                } else if (m.ui.frozen.empty()) {
                    m.ui.pending_rehydrate_trim = false;   // 没有什么可修剪的
                }
                // 否则：尚未涂漆——Tick 事件订阅 在 pending_rehydrate_trim
                // (subscribe.cpp) 上保持武装状态，直到涂漆。
            }

            // ── 递延结算 -freeze (员额) -stream 重绘 固定) ──────
            // finalize_turn 解决了 just-finished
            // 辅助消息（finish() 在其 StreamingMarkdown 上），
            // 但将冻结推迟到 AVOID post-settle 重绘：
            // finish() 更改了 md 小部件的构建形状 + 前缀生成，
            // 因此在同一个刻度中冻结将使 post-finish 冻结的 源码树
            // 与最后的 pre-finish 存在差异帧还在maya的
            // prev_cells（缓存错过=重新-emit从上面全翻）。现在，
            // 一个 view() 已经通过实时 末尾 绘制了已结算的（post
            // -finish）消息，因此 prev_cells 持有 post
            // -finish 哈希值。冻结 字节-and-hash-identical
            // 源码树 此处 是 缓存 命中（无 re-emit）。在同一步骤中进行修剪，
            // 与旧的内联路径完全相同。
            // ── 递延结算 -freeze (员额) -stream 重绘 固定) ──────
            // finalize_turn武装了逐步显示，
            // 最终确定了
            // -finished辅助消息上的斜坡并且设置pending_settle_freeze
            // WITHOUT呼叫finish()。我们保持冻结，
            // 直到 逐步显示 耗尽 FULLY（live_tail_reveal_settled）：
            // 打字机光标到达活动边缘，最终完成斜坡完成，小部件翻转 live_
            // off ON ITS 自身，并且活动 末尾 已将固定（后
            // -finish-shape）源码树 绘制到 maya 的
            // prev_cells 中至少一次。只有 THEN 我们才运行
            // settle_message_md（一个 now
            // -shape-neutral finish()，它只是标记了 缓存
            // fast-path）并冻结。因为 快照 等于 on-screen 帧 字
            // 节-for-byte，所以 live-tail→冻结切换是
            // maya 组件 -cache HIT — 零 re-emit，
            // 零 回滚缓冲区 重复。这是 agent_session 保证（实时高度 ==
            // 冻结瞬间的固定高度），逐步显示 动画完好无损。
            //
            // 当 逐步显示 仍在动画时，我们保持 标志 设置并
            // 设置动画 帧，以便打字机保持滑行，并且门在下一个 帧
            // 上重新出现在 -checked 上 — 它看起来
            // 永远不会卡住，并且冻结也不会提前触发。
            Cmd settle_freeze_trim = Cmd::none();
            if (m.ui.pending_settle_freeze && m.s.is_idle()) {
                if (live_tail_reveal_settled(m)) {
                    m.ui.pending_settle_freeze = false;
                    for (std::size_t i = m.ui.frozen_through;
                         i < m.d.current.messages.size(); ++i) {
                        auto& mm = m.d.current.messages[i];
                        if (mm.role != Role::Assistant || mm.text.empty())
                            continue;
                        settle_message_md(m, mm);
                    }
                    freeze_through(m, m.d.current.messages.size());
                    // 丢弃超过预算的最旧的冻结条目。 live-tail
                    // 折叠和 front-trim 是 两个 不同的收缩，
                    // 具有 OPPOSITE 传输 要求，
                    // 因此它们不能集中在一个 none() 下：
                    //
                    //  ·崩溃(活末尾 归并进入冻结
                    //    前缀）是 BOTTOM-shrink，具有未更改的溢出前缀。
                    //    maya 的 scrollback_prefix_
                    //    匹配分支下降到append-only per-row
                    //    diff — 损坏-free、NO 主机提交。
                    //
                    //  • 修剪从 TOP 的条目中删除条目。
                    //    冻结前缀 — top-DELETION maya
                    //    无法在 渲染 时间协调（移位的 -up 画布未能
                    //    通过前缀 memcmp，并且 maya 在
                    //    PHYSICAL 旧行上提交 新 前缀，搁浅重复项）。
                    //    它 必须 主机-commit 正是删除的行。
                    //    当（且仅当）实际修剪时，
                    //    trim_frozen_if_oversized 返回
                    //    commit_scrollback(N)； none() 当它没有时。
                    //
                    // 逐字转发装饰件的Cmd：常见折叠
                    // -only勾上的none()，也装饰前部的罕见勾上的
                    // commit_scrollback(N)。
                    settle_freeze_trim = trim_frozen_if_oversized(m);
                    // 在本轮期间调整大小：其滚动的-off行以旧宽度打印。
                    // 现在它已被冻结，重播会重新包装
                    // 所有内容（在重置下修剪的提交没有实际意义，
                    // 重置会从头开始重新绘制）。
                    if (m.ui.replay_after_settle) {
                        m.ui.replay_after_settle = false;
                        settle_freeze_trim = cmd::reset_inline();
                    }

                    // 使时钟保持活动状态几个帧，以便maya的缩减协调
                    // （其自身的diff-path崩溃）在FPS=0时完成，
                    // 镜像agent_session的always
                    // -on时钟。
                    m.ui.settle_cooldown_ticks = 6;
                    ::maya::anim::keep_animating();
                } else {
                    // 逐步显示仍在耗尽——保持帧武装，以便打字机动画
                    // 并且我们在下一个滴答声中重新-test门。
                    // 切勿冻结静止 -animating 转弯。
                    ::maya::anim::keep_animating();
                }
            } else if (m.ui.pending_settle_freeze) {
                // 已准备好冻结，但 FSM 尚未空闲（尾随的 sub-turn 拆解、
                // 重试 ctx 仍停在该阶段中，或者 off-thread
                // 降价 解析 在阶段达到空闲之前完成了一根头发）。
                // 上面的冻结 正文 是在 is_idle() 上门控的，
                // 所以它确实 不 运行了这个刻度 - 并且它也做了 不 re
                // -arm a 帧。在 FPS=0 事件-driven 屏幕上，
                // 周期性的 Tick 可以被合并，所以如果没有其他安排 帧，
                // 逐步显示 会默默地关闭 -thread 和
                // pending_settle_freeze 链集：just
                // -finished 降价位于实时 末尾 中，直到下一次击键，
                // 然后与 过期 prev_cells 进行比较（缓存 错过 →
                // 重复/迟到 -committed 转）。只要 标志 是 待处理，
                // Self-perpetuate 就是 帧，因此一旦 is_idle
                // () 翻转为真，门始终会在真正的 帧 上重新-tested。
                ::maya::anim::keep_animating();
            }

            // ── pending_stream → streaming_text ──────────────────────
            // 每个刻度将缓冲的 传输 字节 移动到 streaming_text 中。
            // 无主机-side节奏：agentty使NOT动画化数据流。
            // 该视图将文本+streaming_text+pending_stream
            // 完整地馈送到StreamingMarkdown小部件，
            // 并且maya的reveal_fx拥有live-edge动画。
            // 这是一个简单的附加；旧的速率/积压/突发-flush / 传输
            // -quiet更平滑的是第二个节奏时钟，它与小部件的节拍并产生
            // "stuck then burst"口吃 - 已删除。
            if (!m.d.current.messages.empty()
                && m.d.current.messages.back().role == Role::Assistant)
            {
                auto& msg = m.d.current.messages.back();
                if (!msg.pending_stream.empty()) {
                    msg.streaming_text.append(msg.pending_stream);
                    msg.pending_stream.clear();
                }
            }

            // 勾选上没有 mid-stream 冻结或修剪。单个冻结位点是
            // finalize_turn（通过 pending_settle_freeze、
            // agent_session MessageStop 类似物）。
            // 流式传输 期间的雕刻是 "redraws from top + scrollback corruption"
            // 的记录来源：每个 -stream 中期冻结都会标记一个冻结转弯，
            // 其 hash_id maya 的组件 缓存 在之前的 live
            // -tail 帧 上没有看到，因此 缓存 错过并重新-emitted
            // 这些行 - 有时过度承诺 回滚缓冲区。声称的 per
            // -frame 成本（
            // 五千行文字回复每一帧都重新布局。
            // 实际上是由 maya 的组件 缓存 在 STABLE live-tail
            // 哈希上处理的； mid-stream 雕刻是使 缓存 无效的雕刻。
            // agent_session从不雕刻mid-stream并且在长时间运行中显示出零损坏/零减速。
            //
            Cmd midrun_trim = Cmd::none();

            // ── Stream-stall watchdog ──────────────────────────────────
            // 120秒的完全沉默极有可能是楔
            // 入的传输层而不是合法的模型行为。
            // Clock-skew守卫：如果渲染传递花费了>2秒，
            // 我们将last_event_at向前变基，因此一个缓慢的帧不能
            // 合成虚假的数据流-stalled错误。
            constexpr auto kTickRebaseThreshold = std::chrono::seconds(2);
            if (auto* a = active_ctx(m.s.phase);
                a && tick_gap >= kTickRebaseThreshold
                && a->last_event_at.time_since_epoch().count() != 0) {
                a->last_event_at += tick_gap;
            }
            constexpr auto kStallSecs = std::chrono::seconds(120);
            // LOCAL提供者默默地处理提示词——没有SSE 字节，
            // 没有保活——并且消费者硬件上的20-30B模型
            // 可以在其第一个令牌之前在长提示词上磨练几分钟。
            // 120秒将在-processing中切断它并且重试将开始相同的研磨
            // （局部死循环的另一面）。
            // 匹配传输层本地闲置补贴； Esc 仍然取消。
            const bool local_provider = [&] {
                const auto& sel = m.d.selection;
                return sel.kind == provider::Kind::OpenAI
                    && !sel.openai_endpoint.use_tls;
            }();
            const auto stall_after =
                local_provider ? std::chrono::seconds(600) : kStallSecs;
            if (auto* a = active_ctx(m.s.phase);
                a && m.s.is_streaming()
                && std::holds_alternative<retry::Fresh>(a->retry)
                && a->last_event_at.time_since_epoch().count() != 0
                && now - a->last_event_at >= stall_after) {
                a->retry = retry::StallFired{};
                if (a->cancel) a->cancel->cancel();
                auto since = std::chrono::duration_cast<std::chrono::seconds>(
                                 now - a->last_event_at).count();
                std::string msg = "stream stalled — no events for "
                                + std::to_string(since) + "s";
                StreamError err{std::move(msg)};
                err.from_stall = true;   // 消息上携带停止意图
                return Cmd::after(
                    std::chrono::milliseconds(0),
                    Msg{std::move(err)});
            }

            // ── ExecutingTool 楔形 看门狗 ───────────────────────────
            // 上面的 数据流-stall 看门狗 永远不会覆盖 ExecutingTool
            // (is_streaming() 在那里为 false)，
            // 并且一旦工具运行，就不会出现 SSE 事件 流，
            // 因此在这个阶段搁浅的转弯无法恢复——它只是静静地死去 (加载动画 停止，
            // 没有错误，只有新的提交才能恢复应用程序)。两个不同的楔子：
            //
            //   (a)NO工具实际上是返回消息上的is_running()，
            //       但相位是ExecutingTool。飞行中的任何物体都
            //       不会 分派 a 终端 ToolExecOutput，
            //       因此 FSM 会永久卡住。这是一个真正的逻辑楔形 — 在从
            //       last_event_at（相位时钟，
            //       在 ExecutingTool 期间未受影响）测量的短暂宽限后恢复。
            //
            //   (b)工具IS正在运行，但其工作线程挂在阻塞系统调用中（
            //       死NFS/FUSE挂载，冻结网络）。仅从时间上看，
            //       这与合法的慢速工具无法区分，因此网络故意为 LONG
            //       (kToolWedge)，并根据工具自己的
            //       started_at 进行测量，而不是 过期
            //       last_event_at。这是最后一个-resort安全阀，
            //       NOT是根据用户要求拆除的per-tool 超时；
            //       正常构建/`gh run watch` 在其下完成
            //       得很好。
            //
            // 两种情况下的恢复：使有问题的运行工具失败（或者，对于（a)，
            // 仅重新-fire调度器）和调用 kick_pending_tools，
            // 其要么继续轮流（post-tool sub
            // -turn），要么干净地下降到空闲。
            //
            // kToolWedge 仅保留 上方 bash 自己的最大 self-timeout
            // （300 秒，mcp-cpp shell.cpp 中的 `timeout`
            // arg 上限），因此合法长的 user-set bash 命令会在最后的
            // -resort 网络发生故障之前按照自己的方式完成。 SPINNING 弱-model
            // 循环的真正修复是kick_pending_tools 中的doom
            // -loop 断路器，而不是这个hung-syscall 网络。
            constexpr auto kNoRunningGrace = std::chrono::seconds(30);
            constexpr auto kToolWedge      = std::chrono::seconds(330);
            if (m.s.is_executing_tool()
                && !m.d.current.messages.empty()
                && m.d.current.messages.back().role == Role::Assistant) {
                auto& back = m.d.current.messages.back();
                bool any_running = false;
                bool wedged      = false;
                for (auto& tc : back.tool_calls) {
                    if (!tc.is_running()) continue;
                    any_running = true;
                    auto started = tc.started_at();
                    // 活跃度参考：EXECUTION
                    // START的LATER和最新进展快照_ToolUse::Running拥有该规则为liveness_origin()，
                    // 因此显示时间和活跃
                    // 时间不能再次偏离。
                    //
                    // 执行开始，而不是卡诞生：等待用户批准
                    // 的工具没有运行，并且根据工具的预算对
                    // 人类的 think-time 进行计费
                    // 意味着在长时间暂停后批准命令立即失败，
                    // 在它执行单个指令之前（问题#40）。
                    //
                    // 健康的 long-runner (通过工具 调用 工作的子代理，
                    // 流式传输 构建）不断碰撞
                    // last_progress_at，因此楔形帽会随着每个
                    // 生命迹象而重置，并且永远不会在平坦的 330 秒处将其切断。
                    // 真正挂在阻塞系统调用上的工具不会
                    // 发出任何进展，因此源保持不变并且网络与
                    // 以前完全相同。
                    auto liveness = started;
                    if (const auto* r = std::get_if<ToolUse::Running>(&tc.status))
                        liveness = r->liveness_origin();
                    if (started.time_since_epoch().count() != 0
                        && now - liveness >= kToolWedge) {
                        // 报告时间SPENT EXECUTING。
                        // 这里的报告卡年龄是旧消息如何声明工具
                        // "ran 400s" 的，当时它大部分时间
                        // 都在等待人类。
                        auto exec_from = liveness;
                        if (const auto* r = std::get_if<ToolUse::Running>(&tc.status);
                            r && r->executing_since.time_since_epoch().count() != 0)
                            exec_from = r->executing_since;
                        auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                                        now - exec_from).count();
                        auto quiet = std::chrono::duration_cast<std::chrono::seconds>(
                                        now - liveness).count();
                        // MESSAGE可以使用exec_from，
                        // 其回退到邮票-less运行的活性——显示器需要一些数字。
                        // STORED 窗口不能：这是一个会计事实，
                        // 而 settle_failed 只记录真实的 分派 印记
                        // （参见 ToolUse::settle_failed）。
                        tc.settle_failed(now,
                            "tool ran " + std::to_string(secs) + "s ("
                            + std::to_string(quiet) + "s with no progress) "
                            "\xe2\x80\x94 worker likely hung on a blocking "
                            "syscall; failing it so the turn can recover. "
                            "The worker thread may continue in the "
                            "background; its result is discarded if it "
                            "ever returns.");
                        wedged = true;
                    }
                }
                if (!any_running) {
                    auto* a = active_ctx(m.s.phase);
                    if (a && a->last_event_at.time_since_epoch().count() != 0
                        && now - a->last_event_at >= kNoRunningGrace) {
                        wedged = true;   // FSM 飞行中空无一物滞留
                    }
                }
                if (wedged) {
                    auto kick = cmd::kick_pending_tools(m);
                    return kick;
                }
            }

            // 当 数据流 正在主动产生 字节 时，每隔约 500
            // ms 将 tok/s 采样到迷你图环中。
            if (auto* a = active_ctx(m.s.phase);
                a && m.s.is_streaming()
                && a->first_delta_at.time_since_epoch().count() != 0) {
                constexpr auto kSampleInterval = std::chrono::milliseconds{500};
                if (a->rate_last_sample_at.time_since_epoch().count() == 0) {
                    a->rate_last_sample_at    = now;
                    a->rate_last_sample_bytes = a->live_delta_bytes;
                } else if (now - a->rate_last_sample_at >= kSampleInterval) {
                    auto window_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                         now - a->rate_last_sample_at).count();
                    auto bytes_delta = (a->live_delta_bytes >= a->rate_last_sample_bytes)
                                       ? (a->live_delta_bytes - a->rate_last_sample_bytes)
                                       : 0;
                    float rate = window_ms > 0
                               ? (static_cast<float>(bytes_delta) / 4.0f)
                                 * (1000.0f / static_cast<float>(window_ms))
                               : 0.0f;
                    m.s.rate_history[m.s.rate_history_pos] = rate;
                    m.s.rate_history_pos =
                        (m.s.rate_history_pos + 1) % StreamState::kRateSamples;
                    if (m.s.rate_history_pos == 0) m.s.rate_history_full = true;
                    a->rate_last_sample_at    = now;
                    a->rate_last_sample_bytes = a->live_delta_bytes;
                }
            }
            {
                std::vector<Cmd> trims;
                if (!rehydrate_trim.is_none())
                    trims.push_back(std::move(rehydrate_trim));
                if (!midrun_trim.is_none())
                    trims.push_back(std::move(midrun_trim));
                if (!settle_freeze_trim.is_none())
                    trims.push_back(std::move(settle_freeze_trim));
                if (trims.size() == 1)
                    return std::move(trims.front());
                if (!trims.empty())
                    return Cmd::batch(std::move(trims));
            }

            // ── 闲置 缓存-lapse 预览 -compaction ───────────────────────────
            // 仅当 提示词缓存 保留前缀时，Deep-ride (95
            // %)才便宜。如果我们闲置的时间足够长，以至于 缓存 即将失效，
            // 和 前缀很大，那么用户的 下一个 消息将以全输入速率
            // 重新 -price 整个事情。 Pre-empt：在前
            // 缀仍然是温暖的 缓存 的情况下发射压缩 当前，
            // 因此它们回到了一个小的温暖上下文。这就是ONLY主动
            // 早期-compaction路径；它被限制在真正大的+
            // near-TTL-idle状态，所以普通的暂停永远不会绊倒它。
            if (m.s.is_idle()
                && !m.s.compacting
                && !m.s.autocompact_disabled
                && m.d.persisted.auto_compact
                && !m.d.current.messages.empty()) {
                const int est = static_cast<int>(
                    cmd::estimate_wire_tokens(m.d.current) * m.s.est_calibration);
                if (m.s.should_compact_on_idle(now, est)) {
                    // 清除last_wire_at，这样我们在分派压缩请求时就
                    // 不会在每个Tick 都重新-fire；当摘要请求实际发出时，
                    // StreamStarted re-stamps 它。
                    m.s.last_wire_at = {};
                    auto compact_cmd = Cmd::task(
                        [](maya::Sink<Msg> out, std::stop_token) {
                            out.send(Msg{CompactContext{}});
                        });
                    return compact_cmd;
                }
            }
            return Cmd::none();
        },
        [&](Quit) -> Cmd {
            // 使活动回合的取消令牌FIRST跳闸。如果 数据流 （或工具）正在飞行 -
            // 恰好当 spinner/typewriter 正在动画时 - 它的 工作线程
            // 被阻止在 HTTP poll/SSL_read. 中，没有其他信号通知它退出，
            // 所以如果没有这个循环退出，但 ~BackgroundQueue
            // 的加入等待 工作线程 注意到 取消 缓慢的方式（下一个服务
            // 器 字节 或空闲） 超时) — 4-10 秒
            // "^C doesn't quit during an animation" 挂起。
            // Esc/CancelStream已经这样做了；也必须戒掉。
            // 该令牌是原子的，工作线程 的轮询循环每约 200 毫秒检查一次，
            // 因此连接会立即返回。
            if (auto* a = active_ctx(m.s.phase); a && a->cancel) a->cancel->cancel();

            Cmd save = m.d.current.messages.empty()
                         ? Cmd::none() : save_current_whole(m);
            // 在退出之前，始终将用户在活动提供者下的CURRENTLY的模型归档，
            // 因此重新启动（或稍后切换回该后端）精确地恢复该模型，
            // 而不是内置的-in默认值。如果没有这个，用户从未打开
            // 型号 选择面板 或切换提供商的会话根本不会记录任何 per
            // -provider 召回 -
            // "it forgets the model I was using" 投诉。
            // persist_settings 是 加载-modify-save，
            // 因此它保留了所有其他后端的调用 + 键。
            auto quit_save = persist_settings(m);
            // ORDER，是加载-bearing的两倍。
            //
            // 1.设置保存现在是EFFECT，因此到达商店
            //    当主机运行Cmd时——在该状态更新函数返回之后。
            //    此处清空 write-behind 队列将清空
            //    之前（保存 已排队）。所以排水管移动到主机端：
            //    main() 调用 settings_cache::flush()在拆卸期间，
            //    在循环运行完该Cmd之后。
            // 2.在Cmd内，保存必须从BEFORE退出——
            //    解释器在看到退出 (kernel.hpp
            //    `if (exit_) return`) 后立即停止调度，
            //    因此在静默删除后批处理的任何内容。
            return Cmd::batch(std::move(quit_save), std::move(save),
                              Cmd::quit());
        },
        [&](NoOp) -> Cmd { return Cmd::none(); },
        [&](RedrawScreen) -> Cmd {
            // Ctrl-L→可视区域-only软重绘。
            //
            // 丢弃 maya 的 渲染器 存储单元 缓存 (force_redraw
            // 将活动 InlineFrameState 上的 prev_rows 归
            // 零)并且在下一个组合触发 serialize.cpp 的情况-(B)路径：
            // cursor_up + re-serialize 最后
            // `term_h` 行 + \x1b[J. Net 副作用：
            // 活动 可视区域 中的每个 存储单元 都会从画布上重写，
            // 擦除半 -written 帧 中的任何幽灵字形或留下的杂散子进程输出。
            // 便宜的;保留 回滚缓冲区；保留相对于 可视区域 的光标
            // 位置（无 "composer jumps to the bottom"
            // 震动）。
            //
            // ── SCOPE ─────────────────────────────────────────────────
            //
            // 该热键修复了 可视区域 损坏 仅。
            // 已经溢出 终端 可视区域 的 回滚缓冲区 行
            // 归 终端 模拟器所有，并且不能由内联
            // -mode 应用程序重新绘制，
            // 除非覆盖 帧 之上的任何非 -agentty 内容。
            // 硬合约存在于
            // third_party/maya/src/render/serialize.cpp 的 case-
            // (B) 发出中 — 请参阅那里的 SCOPE CONTRACT 评论以了解完整的理由。
            //
            // 如果用户的回滚缓冲区被损坏（杂散子进程写入了
            // agentty，tmux 面板交换变得混乱，
            // 字节在调整大小期间被丢弃），
            // Ctrl-L将无济于事。在这种情况下正确的恢复是：
            //   ·终端仿真器自己的重绘热键——大多数
            //     终端将它们的Ctrl-L绑定到模拟器的本地存储单元网格的完整重绘，
            //     其中IS能够到达
            //     回滚缓冲区行。
            //   ·调整终端窗口的大小——maya对待WIDTH
            //     改变为连贯性-collapse，将下一个渲染路由到
            //     发出\x1b[2J\x1b[3J。
            //     x1b[H]的HardReset（每行的换行点移位，
            //     prev_cells是字节-invalid，
            //     没有其他恢复是一致的）。该序列也擦除了主机
            //     回滚缓冲区——具有破坏性，但在宽度变化时结构上
            //     是不可避免的。 HEIGHT-only 调整
            //     大小保持柔软（情况-(B)，无 回滚缓冲区 擦除）。
            //     无论哪种方式，它都是调整事件
            //     大小的被动结果，不限于击键。
            return cmd::force_redraw();
        },
        [&](ClearStatus& e) -> Cmd {
            // No-op，如果用户（或另一个处理函数)
            // 自安排该清洁程序以来写了更新的状态——标记
            // 将不匹配，因此当前横幅保留。
            if (m.s.status_until == e.stamp) {
                m.s.status.clear();
                m.s.status_until = {};
            }
            return Cmd::none();
        },
        [&](UpdateCheckDone& e) -> Cmd {
            // 后台发布检查落地。
            if (e.update_available && !e.latest.empty()) {
                m.s.update_latest = std::move(e.latest);
                m.s.update_url    = std::move(e.url);

                // START 该 DOWNLOAD — 不要等待被询问。
                //
                // 这曾经在标签处停止：用户必须注意到它，
                // 打开调色板并运行"Update agentty"。
                // 大多数人从未这样做过，因此人们运行旧的 二
                // 进制程序 数周并报告了已经修复的错误。
                // Zed 的模型更好，它是用户已经期望的模型 -
                // 更新是 TO 发生的事情，当你被告知时，
                // 剩下的唯一一步就是重新启动。
                //
                // 在 m.env.self_update_ok 上门控，在启动时由
                // self_update_possible() 回答一次，
                // 它拒绝替换 二进制程序 不是我们要做的情况：package
                // -manager 安装在 /usr、nix、Windows 下。
                // 标签 与之前一模一样 - 一个运行 pacman/apt/brew 的
                // 信号 - 没有任何东西被触及。
                //
                // AGENTTY_NO_AUTO_UPDATE=1保持旧
                // 行为（仅通知，根据请求安装）。在不询问的情况下更换
                // 某人的 二进制程序 需要一个关闭开关，该开关也不会
                // 关闭通知 - AGENTTY_NO_UPDATE_CHECK
                // 是更大的锤子，可以完全禁用检查。
                if (!m.s.update_in_flight
                    && m.s.update_pending_restart.empty()
                    && !m.env.no_auto_update
                    && m.env.self_update_ok) {
                    m.s.update_in_flight = true;
                    std::string v = m.s.update_latest;
                    // 安静：无论用户实际在做什么，下载都是附带的。
                    // 状态行报告进度（参见 UpdateProgress），
                    // 标签 报告结果；
                    // 两者都不会抢走焦点。
                    return cmd::perform_self_update(std::move(v));
                }
            }
            return Cmd::none();
        },
        [&](UpdateProgress& e) -> Cmd {
            // 下载完成后的 过期 进度线比没有更
            // 糟糕——一旦我们不再更新，就忽略到达的
            // 任何内容。
            if (!m.s.update_in_flight) return Cmd::none();
            const auto mib = [](std::size_t b) {
                char buf[32];
                std::snprintf(buf, sizeof buf, "%.1f", double(b) / (1024.0 * 1024.0));
                return std::string{buf};
            };
            if (e.total > 0) {
                const int pct = int((e.got * 100) / e.total);
                m.s.status = "\xe2\xac\x86 downloading agentty "
                           + mib(e.got) + " / " + mib(e.total) + " MiB ("
                           + std::to_string(pct) + "%)";
            } else {
                // 无内容-Length：显示我们拥有什么而不
                // ​​是我们必须发明的百分比。
                m.s.status = "\xe2\xac\x86 downloading agentty "
                           + mib(e.got) + " MiB";
            }
            m.s.status_until = {};
            return Cmd::none();
        },
        [&](UpdateApplied& e) -> Cmd {
            m.s.update_in_flight = false;
            if (e.ok) {
                // 在磁盘上更新；正在运行的进程是旧映像。
                // 标签从"⬆ available"切换到
                // "↺ restart"——持久化标记，
                // 因为下面的状态行滚动离开并且重新启动是仍然未完成的一步。
                m.s.update_latest.clear();
                m.s.update_pending_restart = e.detail;
                m.s.status = "✓ updated to v" + e.detail +
                             " — restart agentty to use it";
                m.s.status_until = {};       // 粘性直至被覆盖
                return Cmd::none();
            }
            auto toast = set_status_toast(
                m, "update failed: " + e.detail +
                   " — try `agentty update` from a shell",
                std::chrono::seconds{8});
            return toast;
        },

        [&](GitSignalsRefreshed) -> Cmd {
            // 背景`git status`完成。清除闩锁是整个手臂的
            // 事情——刷新的地图本身位于 工作区 缓存 中，
            // `@` 选择面板 直接读取，因此没有任何内容可以复制到
            // Model 中。
            //
            // 这是使刷新为单一-flight的一半：当
            // 设置了标志时，作曲家的`@` 处理函数
            // 不再发出刷新Cmd，因此按住键合并而不是
            // 每次击键产生线程。
            m.ui.git_refresh_inflight = false;
            return Cmd::none();
        },
    }, mm);
}

void ascend(Model& m) {
    if (!m.ui.panel.ascend()) {   // 无父 → 开过 线程
        m.ui.panel.close_all();
        return;
    }
    // 重新验证：恢复的快照与下降一样古老，并且模型可能已经移动。
    // 仅声明索引 LIVE 列表需要工作 — 自由文本
    // （查询）不能转到 过期。
    if (auto* p = m.ui.panel.get<pn::Palette>()) {
        // 过滤后的列表为model-dependent(门控行appear/vanish)；
        // 将光标固定在今天的列表而不是昨天的列表中。
        const int sz = static_cast<int>(
            filtered_commands(p->query, ui::palette_context(m)).size());
        p->index = sz <= 0 ? 0 : std::clamp(p->index, 0, sz - 1);
    } else if (auto* s = m.ui.panel.get<pn::SettingsList>()) {
        // 行源自实时模型状态（当子面板打开时插件可能
        // 已连接或被删除）；将恢复的光标夹到今天的列表中。
        // 搜索索引已重建，因此 面板 中刚刚
        // 更改的值会在此处显示其新值。
        int rows = 0;
        if (s->concern == settings::Category::General)
            s->index_rows = settings_search_index(m);
        if (settings::is_search(*s))
            rows = static_cast<int>(settings::search(s->index_rows, s->query).size());
        else
            rows = static_cast<int>(settings::items_for(m, s->concern).size());
        s->index = rows <= 0 ? 0 : std::clamp(s->index, 0, rows - 1);
    } else if (auto* sm = m.ui.panel.get<pn::SmartMode>()) {
        // 形式快照可以早于在子项中进行的配置改变（fused
        // -picker槽分配写入智能配置）。
        // 从实时模型重建 - 每次（重新）打开使用相同的构建
        // 器 - 仅保留视图状态：光标和高级 切换。
        const int cur = sm->form.cursor;
        auto f = build_smart_form(m, sm->advanced);
        f.cursor = std::clamp(cur, 0,
                              static_cast<int>(f.fields.size()) - 1);
        sm->form = std::move(f);
    }
}

[[nodiscard]] Cmd apply_smart(Model& m, smart::RoleConfig cfg) {
    // Env 覆盖了 面板 或配置文件可以说的任何内容，
    // 并且他们赢得了 此处，因此下面的每个持有者都看到相同的解析
    // 值——而不是每个人都 re-reading 环境并且可能不同意。
    settings::registry::apply_env(cfg, m.env.settings);

    // 1. UI 线程的副本——什么是分类器和工作量缩放器
    //    下一回合继续阅读。
    m.d.smart = std::move(cfg);

    // 2.坚持。进入 RECORD，不是新鲜的 加载-modify-save：旧的
    //    这里的评论声称加载-modify-save避免了重击，
    //    但它却是CAUSED重击。它在留下 `m.d.persisted`
    //    过期 的同时写入了商店，因此下一个整个 -record 保存
    //    将旧的智能配置直接放回去。
    m.d.persisted.smart = m.d.smart;
    auto smart_save = save_record(m);

    // 3.子代理路由器的副本。 `task` 在 工作线程 上运行，没有 Model，所以
    //    它需要自己的快照——但是不再有状态更新函数推动它。 分派 接口边界（
    //    update.cpp、publish_derived）看到 m.d.smart
    //    发生变化，并在 归并 之后发布它。
    return smart_save;
}

} // namespace agentty::app::detail
