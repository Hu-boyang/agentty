// conversation.cpp ——对话的视图适配器 可视区域.
//
// agent_session-style 快速路径: 手 maya 借给
// m.ui.frozen (附件) 缩写 -only 已建 -Element
// 向量在每一个固定的转弯上生长)加小活体 -tail 冻结的元素(在
// -flight 助手转动 + 任何队列 -message 预览).
//
// 人均数 -frame 因此,费用为O(
// visible_live_tail )无论会期有多长。 绝不 重建:
// 被构建为内部元素值 m.ui.frozen 他们当时是安定的。
// src/runtime/app/update/frozen.cpp
// 并在那里呆到 线程 切换/ NewThread / 紧凑
// 触发重建。

#include "agentty/runtime/view/thread/conversation.hpp"
#include <maya/core/anim_clock.hpp>   // anim_now : 数据 帧 时钟

#include "agentty/util/logx.hpp"

#include <filesystem>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string_view>
#include <utility>

#include <maya/dsl.hpp>
#include <maya/render/cache_id.hpp>
#include <maya/widget/activity_indicator.hpp>
#include <maya/widget/conversation.hpp>
#include <maya/widget/permission.hpp>
#include <maya/widget/turn.hpp>

#include "agentty/runtime/view/palette.hpp"
#include "agentty/runtime/view/helpers.hpp"   // ui::reasoning_shown 判定函数。
#include "agentty/runtime/view/thread/activity_indicator.hpp"
#include "agentty/runtime/view/thread/seam.hpp"
#include "agentty/runtime/view/thread/turn/permission.hpp"
#include "agentty/runtime/view/thread/turn/turn.hpp"

namespace agentty::ui {

namespace {

// gap_row / compaction_divider_row 来从
// SHARED 接口边界 头文件 (seam.hpp )——相同的定义
// frozen.cpp's freeze_range 封印,所以活的和冻的行序
// 是 字节-identical (a) 建筑(一个定义地点,而不是一个反射的公约)。

// 哨兵队 -check : 唯一内容为 tool_calls (无旁白)
// -message 分类; 运行 -merge 以前使用的逻辑现在
// 生活在共同的 `ui::turn_run_end` /
// `ui::turn_config_for_assistant_run` 帮手们
[[maybe_unused]] bool is_tool_only_assistant(const Message& mm) {
    return mm.role == Role::Assistant
        && mm.text.empty()
        && mm.streaming_text.empty()
        && !mm.tool_calls.empty();
}

// 搭建现场 -tail 要件:每发言者一个回合 -run
// : 用户信件是自己的转折; 连续的助理信件运行( 一个逻辑
// 代理转折, 可能分拆到 N 子) -turns 按员额
// 开列的 -tool 继续) 倒塌 一个 转过身来 正文
// 互换每个子 -turn ' 文本和工具批次按源顺序排列。
// 这是 agent_session 形状——相同的合并逻辑 冻结的构建者使用(
// ) `freeze_range` 调用 同样的 `turn_run_end`
// / `turn_config_for_assistant_run` 帮助者,
// 所以活的和冻的行序列是 字节-identical 用于相同的输入。
void build_live_tail(const Model& m, int& running_turn,
                     std::vector<maya::Element>& out) {
    const std::size_t total = m.d.current.messages.size();
    const std::size_t start = std::min(m.ui.frozen_through, total);
    if (start >= total) return;

    out.reserve(out.size() + (total - start) * 2);

    // 镜像 freeze_range 分隔策略通过 SHARED
    // ui::compaction_boundary_at (
    // seam.hpp : 运行在紧凑边界上得到
    // `≡ Conversation compacted` 同一的上游,相同的元素,
    // 和两个建筑者的定义地点。
    auto compaction_boundary = [&](std::size_t idx) {
        return compaction_boundary_at(m.d.current.compactions, idx, total);
    };
    const std::size_t seam_at = import_seam_index(m.d.current);
    const std::string seam_from = m.d.current.imported
        ? foreign::display_name(m.d.current.imported->source) : std::string{};

    bool first_in_tail = true;
    std::size_t i = start;
    while (i < total) {
        std::size_t run_end = turn_run_end(m.d.current.messages, i);

        // 无中点 -stream 前缀 -split 且无中位数 -run 继续。
        // 整个直播运行 渲染 以 一个 转( 匹配) agent_session ,
        // 其中,整个在 -flight 助理人员 正文 住在这里
        // m.assistant_body 并且只承诺冻结在 MessageStop).
        // 雕刻机需要一个 "continuation" 剩余(
        // freeze_settled_subturns /
        // freeze_streaming_text_prefix () 已删除—— finalize_turn
        // 唯一的冰冻场地,所以现场 末尾 总是从一个整体开始 -turn 边界。

        // 由全员的中转机从侧面 -turn 空档 两者 边则让
        // 部分断裂得到上面的呼吸空间(与下面的空隙相对应),
        // 而不是拥抱上面的内容。命令 — — 领先的空隙、
        // 分隔器、后继的空隙 — — 由相同的空隙释放出来。
        // freeze_range ,所以活的和冻的行序
        // 留下 字节-identical 整个冰冻 接口边界.
        const bool first_overall = m.ui.frozen.empty() && first_in_tail && i == 0;
        if (compaction_boundary(i)) {
            if (!first_overall) out.push_back(gap_row());   // 空间 上方
            out.push_back(compaction_divider_row());
        }
        if (i == seam_at) {
            if (!first_overall) out.push_back(gap_row());
            out.push_back(import_seam_row(seam_from));
        }

        if (!first_overall) {
            out.push_back(gap_row());
        }
        first_in_tail = false;

        const Message& head = m.d.current.messages[i];
        int turn_num = running_turn;

        if (head.role == Role::Assistant) {
            // 轮次内活动指示器，采用 agent_session 模式。
            //    显示呼吸 "thinking…" 当代理代理活动时行到助手
            //    转弯时行到 正文 槽尚未 - i.e. cfg.body 空的,
            //    在 turn_config_for_assistant_run。
            //    形状与 agent_session
            //    : 思维部件出现在助手转弯中 正文
            //    直至第一次 text/tool/etc.
            //    槽地,然后内容替换它。
            auto cfg = turn_config_for_assistant_run(
                i, run_end, turn_num, m);
            // 保留一个指标 -height 用于该 整个 状态。
            // 当 末尾 是一个空的占位符,我们画的呼吸
            // "thinking…" 部件; 一旦真正内容到达,
            // 我们交换到相同的内容 -height 隐形太空人,
            // 所以现场 -tail 数行不变,没有这个,
            // 先 -byte / 首个 -tool 将槽从
            // 2行翻到0, 线程 跟踪的太空人无法吸收
            // 心理医生 当记录已经填满 可视区域 ,
            // 然后编曲器会按指示器的高度跳跃。
            const Message& tail = m.d.current.messages[run_end - 1];
            // "Empty placeholder" 什么都没有 可见 仍然,
            // 故意这样做 不 需要的软件 pending_stream 改
            // 为空:第一个 content_block_delta 土地
            // 字节 输入 pending_stream (stream.cpp ),
            // 它们只在以后才变得可见 滴答,当 meta.cpp 滴
            // 进水里 streaming_text。如果我们放下占位符的瞬间
            // pending_stream ,该指示行将消失 帧 之前
            // cached_markdown_for 拥有任何
            // streaming_text 要绘制( has_text 打开门
            // text/streaming_text – 现场闪烁1~0~1行
            // 接口边界 推着 composer/status 倒数第二
            // 数据流。将指示器保存到 streaming_text 其实有
            // 字节 使指标=内容互换为单一高度 -stable 步骤。
            const bool tail_is_empty_placeholder =
                tail.role == Role::Assistant
                && tail.text.empty()
                && tail.streaming_text.empty()
                && tail.tool_calls.empty();
            // 只有 最后 运行在 末尾 输入 -flight
            // 在一个高点必须保留在指标的 QQ 内容翻转处。
            // 末尾 已经解决(模型已移到新子) -turn
            // ——给他们一个空手器,两排废品,更重要的是,
            // 让他们的空间变得像 正文 形状与什么
            // 不同 freeze_range 将建造,
            // 从而防止 hash_id 缓存 下方从
            // 交战和强迫全员 重绘 数量(可能很大)
            // 正文 每隔一段时间 帧 整个运行期间。
            const bool is_last_run     = (run_end >= total);
            const bool reserve_slot   = m.s.active() && is_last_run;
            // ...其他的都已经说完了 "still working"
            // 现场推理区有自己的 加载动画 ,
            // 它自己的仪表和文本到达它; 下方的六角形磁带是第二部动画,
            // 同样的事实, 它花费它占用的行数加上在
            // 真实内容登陆时的交割。
            const bool show_indicator = reserve_slot && tail_is_empty_placeholder
                                        && !reasoning_shown(tail);
            if (show_indicator) {
                using namespace maya::dsl;
                maya::ActivityIndicator::Config ind;
                ind.edge_color    = cfg.rail_color;
                ind.spinner_glyph = std::string{m.s.spinner.current_frame()};
                ind.label         = "thinking";
                ind.words         = activity_indicator_words();

                if (const auto* a = active_ctx(m.s.phase)) {
                    ind.stream_bytes = a->live_delta_bytes;
                    if (a->first_delta_at.time_since_epoch().count() != 0) {
                        auto now = maya::anim_now();
                        auto ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                         now - a->first_delta_at).count();
                        if (ts_ms >= 250) {
                            double sec = static_cast<double>(ts_ms) / 1000.0;
                            double tok = static_cast<double>(a->live_delta_bytes) / 4.0;
                            ind.stream_rate = static_cast<float>(tok / sec);
                        }
                    }
                }

                constexpr std::size_t kEntropyTail = 512;
                const std::string& a_text = head.streaming_text;
                const std::string& a_pend = head.pending_stream;
                const std::string& src = !a_pend.empty() ? a_pend : a_text;
                if (!src.empty()) {
                    std::size_t n = std::min(kEntropyTail, src.size());
                    ind.entropy_window = std::string_view{
                        src.data() + src.size() - n, n};
                }

                cfg.body.emplace_back(
                    maya::ActivityIndicator{std::move(ind)}.build());
            }
            // NOTE : 一旦存在真实内容,
            // 则无跟踪空间器。该指示符占用 正文 仅
            // 时 末尾 是空占位符; 第一组 text/tool 空间,
            // 内容本身 持有 正文 高度。 保留 2
            // -row 整个活动阶段(旧行为)的空间
            // 器意味着固定的内容带有死后跟踪空间
            // VANISHED 转弯 -end ——可见的
            // 2 -row 显示器 -content 翻
            // 转它本来是要平滑的,不是 -event
            // 实践上:第一个内容槽是 \\ 1 行,
            // 所以高度步骤很小,并且发生一次,
            // 中 -stream 而不是作为每个转弯边界的摇摆。

            // 缓存 解决 -but-not-yet-frozen 跑,
            // 在现场直播 末尾 与每个工具 终端 且未活动 数据流 谓 字
            // 节-stable : 其类型 正文 (其中可包括一个多边
            // -thousand-line write/edit 牌子)不会改变,
            // 直到 freeze_through 移动到 m.ui.frozen。
            // 没有 hash_id 现场直播 末尾 没有 缓存 输入,
            // 所以 maya REBUILDS 财务报告和已审计财务报表
            // REPAINTS 整个 正文 每隔一段时间 帧 — a
            // 3000-line 写入 末尾 测量 ~80ms/frame
            // (o1_probe livetail_ms 栏中。
            // 相同 键 freeze_range 将使用下列手段:(a)
            // 正文 油漆一次,然后在等待冷冻时擦亮,以及(b) 缓存 输入在
            // 冻结的交接(同键)中幸存,所以冻结瞬间是无缝的。只有在运行完全时才有。
            // 终端 和 无 indicator/spinner 添加了槽(
            // 每个槽的变异) 帧 并且必须错过。
            const bool run_terminal = [&] {
                for (std::size_t j = i; j < run_end; ++j)
                    for (const auto& tc : m.d.current.messages[j].tool_calls)
                        if (!tc.is_terminal()) return false;
                return true;
            }();
            // CRITICAL : 不执行 将可缓存的密钥同时印出 逐步
            // 显示 覆盖面板 仍然在动画中。 assistant_run_hash_id
            // 被键入到消息 id + compute_render_key
            // () (正文) content/size )——这是
            // INVARIANT 横跨 逐步显示 正在移动的 – 干净的过渡,
            // 因为其中的文本 字节 不变,只有每个 -frame 覆盖面板
            // 存储单元 如果在后面的边缘 显示闪烁的甘油时 盖上它
            // maya 画那些摇摆 存储单元 输入 hash_id-keyed
            // 组件 缓存 ; 当 逐步显示 然后决定清除文本 密钥是 UNCHANGED ,
            // 所以,请 缓存 HITS 永远 重绘 · 将垃圾堆在安置
            // 地上 末尾 永远(冰冻) -glyph 截图 :
            // "just let me\u2423Z@o%" )并且因为:
            // freeze_range 重复使用相同的密钥, 垃圾会被永久冻结。
            // 离开运行 UNKEYED (重建+ 重绘 每隔一段时间 帧 ,
            // 像进去 -flight 运行),直到每条消息 逐步显示
            // 部件已经完全耗尽了,然后是干净的 存储单元 的窗口是 ~200
            // ms 斜坡 +~ 376 ms 的 缓存, 冻结的交接会
            // 打入一个干净的入口。 pending_settle_freeze
            // RAF 时间,所以这最多需要 几笔额外的重建。
            const bool reveal_settled = [&] {
                for (std::size_t j = i; j < run_end && j < m.d.current.messages.size(); ++j) {
                    const auto& mj = m.d.current.messages[j];
                    if (mj.role != Role::Assistant) continue;
                    // 兄弟姐妹们 REASONING 插槽( N) mj.id
                    // + "#r" : 推理 数据流 透过自己的 逐步显示
                    // 即使答案是错的 正文 仍然是空( 纯 -thinking
                    // 检查它 首次 无条件——当 mj.text 空的
                    // 正是冻死的那个 "Thinking" 键盘。
                    if (const auto* rc = m.ui.view_cache.peek(
                            m.d.current.id, MessageId{mj.id.value + "#r"});
                        rc && rc->streaming && rc->streaming->is_animating())
                        return false;
                    if (mj.text.empty()) continue;
                    // 无 -migrating 读 -only 探测器:Mj可能是
                    // PINNED 活的边缘,还有 message_md 将它
                    // 从固定的集合中移出。 peek() 读书从任何家。
                    const auto* mc = m.ui.view_cache.peek(
                        m.d.current.id, mj.id);
                    if (!mc || !mc->streaming) continue;
                    // 权威上游(见 is_animating () 内 maya)
                    // — 必须 保持准确的镜子
                    // live_tail_reveal_settled 门,现在也是 调
                    // 用 就是这样
                    if (mc->streaming->is_animating())
                        return false;
                }
                return true;
            }();
            if (run_terminal && !reserve_slot && reveal_settled) {
                cfg.hash_id = assistant_run_hash_id(m, i, run_end);
            }
            // NOTE : 输入 -flight (流式传输 )
            // 运行是故意的 不 缓存。 转弯带动画染色体——
            // 工具 加载动画 格莱弗和活的 `elapsed`
            // 计数器——每处更改 帧 独立 正文 字节 整个
            // -Turn hash_id 键入工具状态 + 正文
            // -size 桶会擦伤 a 过期 桶之间的卡片,
            // 冷却 加载动画 和已读的(已读) "liveness gone").
            // 人均数 -frame 重建是便宜的,因为 流式传输
            // write/edit 正文 切成一个 末尾 窗口中
            // tool_body_preview_config (O(窗口),
            // 不是O(文件))),所以我们支付~0.04ms来重建并保持动画的活力.
            out.push_back(maya::Turn{std::move(cfg)}.build());
            ++running_turn;
            i = run_end;
        } else {
            // 用户(或其他非用户) -Assistant ) 头部:单 -message 转身
            // freeze_range 数字,用户随该数字转 BARE
            // frozen_turn (目前为止助理人数已稳定) --
            // 不 frozen_turn +1——因此用户行携带前方的助手转弯的号码.
            // running_turn 种子为 frozen_turn +1
            // 并跟踪 下一个 助手号码,所以这里的匹配用户号码是 running_turn -1.
            // 使用 running_turn (旧代码)让用户行在
            // 什么前方 freeze_range 邮票——a 字节 冻结时
            // 的分歧 接口边界 瞬间 a user/divider 坐在现场
            // 末尾 (e.g。该员额 -compaction 边界,
            // 它会拉着正义 -frozen 转身 回滚缓冲区。
            // 见 INLINE_SCROLLBACK.md 标点 # 3 (
            // live/frozen 建筑商必须同意 字节-for-byte).
            auto cfg = turn_config(head, i, turn_num - 1, m,
                                   /*继续=*/false);
            out.push_back(maya::Turn{std::move(cfg)}.build());
            // 用户转弯不碰 running_turn ——运行计数在
            // 助理转弯上方(比对). frozen.cpp 政策。
            i = run_end;
        }
    }
}

// 构建队列 -message 预览行: 在 末尾 中,
// 可以让用户看到队列中的内容。 Claude 代码的
// 外观在 80106500 平面——视觉上与真正的用户转弯
// 完全相同.
//
// 电话 "queued not sent" 提示过去是缺席的
// -of-assistant 加上作曲家的 `❚ N queued` 标签 :
// 缺少元素和空数。 WHEN 外传的,所以一行 看上去完全像一个发送的消息
// 读作 "it sent" 或 或 时 间 "nothing happened"
// 而不是说 "this fires when the turn ends"。
// 第一行的元现在声明了承诺;其余的则保留了编号,因为说它一次是通知,
// 又说它三次是抱怨。
void build_queued_previews(const Model& m, int& running_turn,
                           std::vector<maya::Element>& out) {
    if (m.ui.composer.queued.empty()) return;
    out.reserve(out.size() + m.ui.composer.queued.size() * 2);
    const auto now = m.wall_at(maya::anim_now());
    const std::size_t base_idx = m.d.current.messages.size();
    const std::size_t n = m.ui.composer.queued.size();
    for (std::size_t qi = 0; qi < n; ++qi) {
        Message synthetic;
        synthetic.role        = Role::User;
        synthetic.text        = m.ui.composer.queued[qi].text;
        synthetic.attachments = m.ui.composer.queued[qi].attachments;
        synthetic.timestamp   = now;
        std::string meta = "queued #" + std::to_string(qi + 1)
                         + " / "     + std::to_string(n);
        // 只有在第一个,而且只有在一个转弯实际运行时 --
        // -- 闲置,没有"this turn"结束,
        // 而且索赔将是一个谎言(排队排在下一个提交时)。
        if (qi == 0 && m.s.active())
            meta += " \xc2\xb7 sends when this turn ends";
        if (m.ui.composer.queue_peek_index() == static_cast<int>(qi))
            meta = "\xe2\x9c\x8e editing \xe2\x80\x94 " + meta;   // ✎
        out.push_back(gap_row());
        auto cfg = turn_config(synthetic, base_idx + qi, running_turn, m,
                               /*继续=*/false,
                               /*meta_override=*/meta);
        out.push_back(maya::Turn{std::move(cfg)}.build());
        ++running_turn;
    }
}

// 找到现场 ToolUse a pending_permission 正在
// 瞄准目标,走着解冻 末尾 (唯一一个工具可以 仍然在预 -terminal).
const ToolUse* find_pending_tool(const Model& m) {
    if (!m.d.pending_permission) return nullptr;
    const auto& pp_id = m.d.pending_permission->id;
    const auto& msgs  = m.d.current.messages;
    for (std::size_t i = m.ui.frozen_through; i < msgs.size(); ++i) {
        for (const auto& tc : msgs[i].tool_calls) {
            if (tc.id == pp_id) return &tc;
        }
    }
    return nullptr;
}

// 构建权限卡元素. 浮点数作为自己的live_tail行位于活动
// 助手Turn(agent_session形状)下方,
// 而不是作为Turn 正文插槽注入——当权限
// appears/disappears.返回无声时,当待处理 权限
// 没有匹配的活的ToolUse(在运行-end 竞态期间的corner case)时,
// 调用者跳过推力时,保持面高稳定.
std::optional<maya::Element> build_permission_row(const Model& m) {
    const ToolUse* tc = find_pending_tool(m);
    if (!tc) return std::nullopt;
    return maya::Permission{inline_permission_config(
        *m.d.pending_permission, *tc)}.build();
}

} // 命名空间结束。

maya::Conversation::Config conversation_config(const Model& m) {
    maya::Conversation::Config cfg;

    // ── 借入的密封前缀( 0) -copy 油漆 -measured). ─────
    // maya 渲染 穿过这里 ledger_ref : 0 个 -copy
    // 和旧的一样 list_ref 路径, PLUS maya 漆质的传球
    // 记录 每一个街区都铺设好了 -out 高回每个分类账 帧 ——三联计算
    // 承诺的高度是用(Witness Chain,Trim Account)
    // 来计价的. Maya's hash_id-keyed 存储单元
    // 缓存 已经做了 -painted 块击中以后的每一个 帧.
    cfg.ledger = &m.ui.frozen;

    // ── 现场直播 末尾. ─────────────────────────────────
    // 唯一重建的东西每 帧 被一个内围 -flight
    // 代理转弯( 一个用户+ 可能几个助手续写) 加上任何
    // 队列 -message 预览。
    int running_turn = m.ui.frozen_turn + 1;
    build_live_tail(m, running_turn, cfg.live_tail);
    build_queued_previews(m, running_turn, cfg.live_tail);

    // 待处理 权限在活动助手Turn(agent_session
    // 图案)下方作为自己的live_tail行浮动,
    // 在提示词 appears/disappears时保持助手面板高度稳定,
    // 并给予卡片与agent_session时相同
    // 的外边处理.
    if (m.d.pending_permission) {
        if (auto e = build_permission_row(m))
            cfg.live_tail.push_back(std::move(*e));
    }

    // 形状探测器: 每 conversation_config 引用的语句
    // frozen/live_tail 大小加粗糙的活字 -tail 内容草图。
    // AGENTTY_LOG =perf=追踪表面——追踪,因为它是每个视图构造的一行.
    if (::agentty::logx::enabled(::agentty::logx::Channel::Perf,
                                 ::agentty::logx::Level::Trace)) {
        std::size_t live_msgs = (m.d.current.messages.size()
            > m.ui.frozen_through)
            ? (m.d.current.messages.size() - m.ui.frozen_through)
            : 0;
        std::size_t live_text_bytes = 0;
        std::size_t live_tool_count = 0;
        for (std::size_t i = m.ui.frozen_through;
             i < m.d.current.messages.size(); ++i) {
            const auto& msg = m.d.current.messages[i];
            live_text_bytes += msg.text.size() + msg.streaming_text.size();
            live_tool_count += msg.tool_calls.size();
        }
        AGT_LOG_HOT(Perf, Trace, "view.shape",
                "frozen={} live_tail={} live_msgs={} "
                "live_text={} live_tools={} frozen_through={} msgs={}",
                m.ui.frozen.size(), cfg.live_tail.size(), live_msgs,
                live_text_bytes, live_tool_count, m.ui.frozen_through,
                m.d.current.messages.size());
    }

    // 没有单独的in_flight指标——空的
    // -placeholder助手Turn在流式传输期间携带自己的"thinking…"
    // 正文插槽(见build_live_tail),与m.thinking_active
    // 在助手Turne内产生正文插槽的agent_session相匹配,
    // 而不是在它的下面产生免费的-floating指标.
    cfg.in_flight = std::nullopt;
    return cfg;
}

} // namespace agentty::ui
