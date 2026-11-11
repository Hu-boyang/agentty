// 数据流-side 更新的助手 状态更新函数 : 现场直播 -preview 抢救期间
// input_json_delta 部分 -JSON 关闭+调校警卫,
// 以及 finalize_turn 状态 -machine 移交 流式传输 · 闲 / 闲 权限
// / ExecutingTool。退出 update.cpp 所以,我们 状态更新函数 管弦
// 乐手总是容易看懂的

#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/app/update/param_tag_repair.hpp"
#include "agentty/runtime/app/update/stream_args.hpp"
#include "agentty/util/logx.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ranges>
#include <span>
#include <unordered_set>
#include <utility>

#include <maya/core/overload.hpp>
#include <maya/terminal/ansi.hpp>

#include "agentty/auth/auth.hpp"
#include "agentty/domain/catalog.hpp"
#include "agentty/domain/smart_mode.hpp"   // utility_model: 哪个模型压缩
#include "agentty/domain/smart_tuning.hpp"
#include "agentty/provider/error_class.hpp"
#include "agentty/provider/selection.hpp"
#include "agentty/runtime/app/cmd_factory.hpp"
#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/panel/code_blocks.hpp"
#include "agentty/runtime/mem.hpp"
#include "agentty/runtime/view/cache.hpp"
#include "agentty/runtime/view/helpers.hpp"
#include "agentty/tool/spec.hpp"
#include "agentty/tool/skills.hpp"   // active_in:导出"already active"集
#include "agentty/tool/tool.hpp"   // DynamicDispatch::needs_permission (投机) 分派)
#include <maya/widget/markdown.hpp>
#include "agentty/tool/util/partial_json.hpp"

namespace agentty::app::detail {

// 聊天一旦有真正的第一次交换, 便命名聊天,
// 如聊天应用程序所做的: 第一个用户消息有文本回复。
// 当用户命名时, 当它已经标题或尝试时, 当请求被取消时,
// 当关闭时, 以及当没有资格的 线程 (从所有权存在前开始,
// 或一个叉子, 以父名称命名)。
Cmd maybe_auto_title(Model& m) {
    const auto& t = m.d.current;
    // 合格:a 线程 或一个被选用的
    // 名字自动取消的人。
    const bool eligible = t.auto_title_ok || (t.name_is_auto && t.renamed_at > 0);
    if (!m.d.persisted.auto_title || !eligible || t.auto_titled || t.user_named())
        return Cmd::none();
    if (!m.s.titling.value.empty()) return Cmd::none();
    const bool replied = std::ranges::any_of(t.messages, [](const Message& msg) {
        return msg.role == Role::Assistant && !msg.text.empty();
    });
    if (!replied) return Cmd::none();
    m.s.titling = t.id;
    return cmd::generate_thread_title(m);
}

using json = nlohmann::json;

namespace {

// 透明顶级 重试 每个用户在放弃并显示调值之前都会转弯,
// 成为模型的真正错误。 有两次尝试会间歇地从边缘中滑出来。
// -timeouts 将会有更多事情发生 上游.
constexpr int kMaxTruncationRetries = 2;

// 预览 -settle 助理信息( ) StreamingMarkdown 编号 EXACT 状态
// cached_markdown_for 已经很迅速地解决了 -path 期望,所以冻结的构建(
// freeze_range → cached_markdown_for 返回 a 字节-identical 元素
// 源码树 财务报告和已审计财务报表 maya 布莱特而不是重 -emitting 整个转弯。
//
// 电话 重绘-after-stream-end 症状: 旧预科 -settle
// 做了个生的 set_content + finish() (a) 突然翻转现场,
// 而 逐步显示 光标为中 -glide - 倒塌预告 -finish 覆盖面板 源
// 码树 (前缀) ComponentElement + 末尾 输入
// vstack.gap (1) 改为固定员额 -finish 源码树, a ±N-row
// 高度级——和(b) DIDN'T 归并 长码块或印章
// last_settled_size/revealed_size ,所以接下来
// cached_markdown_for 调用 错过了快 -path ,关于 -folded ,
// 并生成了 不同 身高比上个活着 帧。无论三角洲 maya 短一点的 帧 对抗高者,活的,
// 再 -emit 从顶部转过来。
//
// 做全程的整理工作... set_content ———————— finish() →
// auto_fold_long_blocks (同样的) threshold/kinds —
// —盖章 缓存 尺寸——指: 活体 帧 已折叠块( Q) turn.cpp 单位 -frame
// 归并 ),这个 锁 编号 相同 折叠完成 源码树 ,和 cached_markdown_for
// 快速击中已定居 -path 返回同样的 源码树 无高阶,无高阶 -emit 镜像
// agent_session '单一的一致' finish() +推出键
// -to-frozen 时间 MessageStop.
//
// 欢呼 OUT 名称空间( 已宣布为 internal.hpp 这么说吧 meta.cpp
// 延期结算 -freeze 能够 调用 一次,我和他一起来 逐步显示 已经
// 耗尽了。
}  // 命名空间( 关闭下面的 十字-TU 助手的肛门)

// running_over_ssh (a) 与 reveal_end_glide_enabled ()作为getenv
// 住在这里 调用 函数后端 -local 都已经是发射事实了,读一下 init() 输入 Model::env (
// runtime/app/env.cpp): m.env.remote 财务报告和已审计财务报表
// m.env.reveal_end_glide().

void settle_message_md(Model& m, const Message& msg) {
    settle_message_md_cached(m, msg);
}

// 同样的解决 通过一个孔 Model : 它只写视图 缓存 ,
// 这是 可变 (view() 使用时使用 回滚缓冲区 重播,
// 它读了模型, 并且一定不需要 Model&.
void settle_message_md_cached(const Model& m, const Message& msg) {
    auto& cache = m.ui.view_cache.message_md(m.d.current.id, msg.id);
    if (!cache.streaming)
        cache.streaming = std::make_shared<maya::StreamingMarkdown>();
    cache.streaming->set_content(msg.text);
    cache.streaming->finish();
    // Auto-fold DISABLED(matches cached_markdown_for):
    // 长码块 渲染全长,从未坍塌为"▸ N lines of code hidden"的支点.
    // 印章这样 cached_markdown_for 已经很迅速地解决了
    // -path 启动并返回缓存 build() 冻结证和每张 帧 后结.
    cache.last_settled_size = msg.text.size();
    cache.revealed_size     = msg.text.size();
}

// 如果在直播中每个助理消息都是真的 末尾 已经完全消耗了它的
// 逐步显示 动画。见 internal.hpp 完全基于理由。
// READ 的,每个消息的 StreamingMarkdown 部件状态;
// 它永远不会变异。 部件尚不存在( 从未被渲染) 或文本为空的
// 邮件会被作为已解决( 动画无任何)。 闸门是冻结移交的保证。
// 字节-identical 标题 -screen 帧 :
// 我们只冻结 之后 部件本身翻转了 live_ ,
// 所以最后一个 LIVE 帧 maya 已缓存 IS 解决 源码树.
bool live_tail_reveal_settled(const Model& m) {
    for (std::size_t i = m.ui.frozen_through;
         i < m.d.current.messages.size(); ++i) {
        const auto& mm = m.d.current.messages[i];
        if (mm.role != Role::Assistant) continue;
        // 还没承诺 传输 字节 - 还没到,让它骑吧
        if (!mm.streaming_text.empty() || !mm.pending_stream.empty())
            return false;
        // 兄弟姐妹们 REASONING 插槽( N) mm.id + "#r" :
        // 推理通过自身揭示 StreamingMarkdown 即使答案是错的
        // 正文 仍为空(纯净者), -thinking 阶段)。如果 逐步显示
        // 中间 -glide 现场直播 帧 谓 不 定案 —— 保持 Tick 武装,
        // 以便推理打字机继续推进( 以至它 FREEZES 键盘。检查过 首次
        // 无条件,在 mm.text.empty ( ) 跳过下面。
        if (const auto* rc = m.ui.view_cache.peek(
                m.d.current.id, MessageId{mm.id.value + "#r"});
            rc && rc->streaming && rc->streaming->is_animating())
            return false;
        if (mm.text.empty()) continue;   // 无第正文至第逐步显示号
        // 无 -migrating 读 -only 探测器:毫米可能是
        // PINNED 活的边缘,还有 message_md 将它迁移
        // 到被逐出者那里, LRU 从内部 状态更新函数.
        // peek() 读着它住的哪个家 而不扰动分界线
        const auto* cache = m.ui.view_cache.peek(m.d.current.id, mm.id);
        if (!cache || !cache->streaming) continue;  // 从未完成
        // 部件仍然在运行... , , 即 逐步显示 滑到边缘, 最终斜坡运行,
        // 或背景 解析 待处理 - 任何这些都意味着现场直播 帧 输入
        // maya's prev_cells 谓 不 却有固定的形状。
        // 此上游设定 必须 镜像 build_live_tail's
        // `reveal_settled` 确切地说,决定现场直播者是否
        // 末尾 STAMPS 可缓存 assistant_run_hash_id ,
        // 这个决定是否冻结火灾。如果它们不同意冻结,可以将一个键印在现场。
        // 末尾 从未涂过( N) 缓存 MISS) → freeze_range
        // 重建运行在 FrozenBuildScope (show_all
        // 可能不同高度的 – 可能 接口边界 转换和线条重复。
        // 逐步显示-gliding ∪ 解析 生活在这里 一个 现在...
        // maya's StreamingMarkdown::is_animating
        // ()——故此门,景色. reveal_settled 缓存-key 门,
        // 和 is_idle 结算 -skip 永远不能分开了
        if (cache->streaming->is_animating())
            return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────
// 电话 流式传输-args 预览解码器( E) update_stream_preview )和待办事宜
// 同步( sync_todo_state_from_args ) 移动到 stream_preview.cpp
// 共享叶片辅助器件和使用以下的救生路径( sniff_any, try_parse_partial,
// get_string_any, missing_required_field + 异名常数)
// 生活于 update/stream_args.hpp.

bool guard_truncated_tool_args(ToolUse& tc,
                               std::chrono::steady_clock::time_point now) {
    auto missing = missing_required_field(tc.name.value, tc.args);
    if (missing.empty()) return false;
    // 缺少一个必要的字段——在失败之前,试图恢复一个混合的
    // 字段 XML/JSON 工具输入,将字段埋入一个
    // 流出字符串值。 -failing 路径是安全保障 :
    // 井 井 水 井 水井 井 油井 井 -formed edit/write
    // 其内容合法地包含着文字 `<parameter name="…">` 标记(
    // 3) e.g 编辑此文件) 存在所需的字段, 所以我们从不到达这里,
    // 也从不重写它。
    if (repair_param_tag_leak(tc.name.value, tc.args)) {
        tc.mark_args_dirty();
        missing = missing_required_field(tc.name.value, tc.args);
        if (missing.empty()) return false;
    }
    // `now` 是那个 归并 时间,被 状态更新函数 (见A/C.5/61/L.48,第16段) Model::now).
    // 说明是什么 OBSERVED 也猜不到为什么
    //
    // 这曾经是用来断言
    // "the stream was truncated before the full tool input arrived"。（完整工具输入尚未到达，数据流就已经截断。）
    // 这是可能的原因之一,而且经常是错误的原因:第48期是解码器
    // 错误在某个 数据流 完整且有效,并 finish_reason
    // =tool_calls ,而信息发送给记者寻找一个从未
    // 存在的短篇反应。诊断作为事实而显示的代价大于没有诊断。
    //
    // 所以: 命名这个领域, 显示什么 DID 原始的词是
    // 证据——Zed's ToolUseJsonParseError
    // 携带 `raw_input` 也是因为这个原因
    std::string got = tc.args_dump();
    constexpr std::size_t kMaxShown = 400;
    const bool clipped = got.size() > kMaxShown;
    if (clipped) {
        got.resize(kMaxShown);
        got += "…";
    }
    tc.status = ToolUse::Failed{
        tc.started_at(),
        now,
        std::string{"Tool call is missing the required field `"}
            + std::string{missing} + "`.\n\nArguments received"
            + (clipped ? " (first " + std::to_string(kMaxShown) + " bytes)"
                       : std::string{})
            + ":\n" + got
            + "\n\nRe-send the call with `" + std::string{missing}
            + "` populated.",
    };
    return true;
}

json salvage_args(const ToolUse& tc) {
    // 拒绝抢救 传输 中间停止 -string. close_partial_json
    // 将合成一个结束 `"` 并生成一个可解析对象,其最后一个字符串值在
    // 任意时被切换 字节 边界; 发送 `write`/`edit`
    // /`bash` 上面的文字会暗中损坏目标文件或运行半个 -typed 命令。
    // 呼叫者 解析-failed 路径( 选项) `StreamToolUseEnd`
    // )已经具备了正确的形状——我们只需要防止它接受合成的
    // -quote 救援。见发现 3 in
    // docs/corruption-analysis.md.
    //
    // 路径 -only 工具(读, list_dir ,
    // glob)也会受到影响,但是它们的值很小,很少在中点被截断。
    // -string 在实践中; 统一适用该规则,
    // 使政策在一处可以审查,而以少数不实拒绝为代价,使政策恶化 数据流.
    if (agentty::tools::util::ended_inside_string(tc.args_streaming)) {
        return json::object();
    }
    if (auto parsed = try_parse_partial(tc.args_streaming)) {
        if (!parsed->empty()) return *parsed;
    }
    json out = json::object();
    auto pick = [&](std::string_view canon,
                    std::span<const std::string_view> keys = {}) {
        auto ks = keys.empty() ? std::span{&canon, 1} : keys;
        if (auto v = sniff_any(tc.args_streaming, ks, /*部分=(%)*/true))
            out[std::string{canon}] = *v;
    };
    const auto& n = tc.name.value;
    if      (n == "read" || n == "list_dir") { pick("path", kPathAliases); }
    else if (n == "write") { pick("path", kPathAliases); pick("content", kContentAliases); }
    else if (n == "edit")  { pick("path", kPathAliases);
                             pick("old_string", kOldStrAliases);
                             pick("new_string", kNewStrAliases); }
    else if (n == "shell") { pick("command", kCommandAliases); }
    else if (n == "grep")  { pick("pattern"); pick("path", kPathAliases); }
    else if (n == "glob")  { pick("pattern"); }
    else if (n == "find_definition") { pick("symbol"); }
    else if (n == "web_fetch")       { pick("url"); }
    else if (n == "web_search")      { pick("query"); }
    else if (n == "diagnostics")     { pick("command", kCommandAliases); }
    else if (n == "git_commit")      { pick("message"); }
    pick("display_description", std::span{&kDisplayDescription, 1});
    return out;
}

Cmd finalize_turn(Model& m, StopReason stop_reason) {
    // 数据流已经过关了——放下取消的手柄,这样一个过期XXEsc
    // 在发射时无法绊倒下一转数据流.
    // 下方(或kick_pending_tools)阶段过渡驱动是否将active()翻转.
    if (auto* a = active_ctx(m.s.phase)) a->cancel.reset();

    // 压缩完成 压缩完成 数据流 将其摘要写成:
    // `m.s.compaction_buffer`
    // (不 记录为: 不可变 跨紧凑 事件 现在:
    //   1. 将第缓冲区号摘要案文删除。
    //   2. 脱衣舞 <summary>…</summary> 标记 / 修剪白空间。
    //   3. 推出一个Thread::CompactionRecord描述
    //      [0, compaction_target_index 未来 传输
    //      载荷 由E. 建构 `cmd_factory::wire_messages_for`
    //      替换摘要,以替换原前缀。
    //
    // 用户 -visible 记录稿保持原样 CompactContext 被炒了,
    // 没有交换,没有 "reset to a shorter conversation" ,
    // 没有 thread_view_start 胡说八道,唯一的 UI 信号是
    // 正在翻转到 "context compacted" 和将视图分隔为信件的
    // 分隔符 [ up_to_index -1)和消息[ up_to_index].
    if (m.s.compacting) {
        std::string summary = std::move(m.s.compaction_buffer);
        m.s.compaction_buffer.clear();

        constexpr std::string_view kOpen = "<summary>";
        constexpr std::string_view kClose = "</summary>";
        if (auto a_pos = summary.find(kOpen); a_pos != std::string::npos) {
            auto body = a_pos + kOpen.size();
            auto b_pos = summary.find(kClose, body);
            if (b_pos != std::string::npos) {
                summary = summary.substr(body, b_pos - body);
            }
        }
        auto is_space = [](char c) { return c==' '||c=='\t'||c=='\n'||c=='\r'; };
        while (!summary.empty() && is_space(summary.front())) summary.erase(summary.begin());
        while (!summary.empty() && is_space(summary.back()))  summary.pop_back();
        if (summary.empty()) summary = "[compaction produced no text]";

        // 将目标索引粘贴到当前记录的大小, 用于大小写
        // queued/submitted 紧凑时(
        // 排队) -on-compact 路径排水口 之后 这
        // 条分支运行,但未来的代码路径 原则上可以登陆消息更早。
        // 电话 不变量 我们关心的是: up_to_index 永远不超过
        // messages.size (所以)这样 传输-substitution 帮手保持健康
        // -defined.
        const std::size_t up_to = std::min(m.s.compaction_target_index,
                                           m.d.current.messages.size());
        Thread::CompactionRecord rec;
        rec.up_to_index = up_to;
        rec.summary     = std::move(summary);
        rec.created_at  = m.wall_now();
        // 这到底给我们买了什么? 以WIRE 载荷而不是记录
        // 本来衡量——记录本是不可变,
        // 所以其中的before/after是相同的,每一次回收都读作零。
        //
        // `before` 与刚才存在的紧凑体是有效的大小;
        // `after` 一种几乎完全不恢复的紧凑
        // 是危险的:它看起来像是成功,留下一个摘要,
        // 下一个转弯紧凑——即: 线程 liverlock
        // 自己总结, 而用户却看它什么都不做。
        // 这只可见于一个 NUMBER.
        rec.tokens_before = cmd::estimate_wire_tokens(m.d.current);
        m.d.current.compactions.push_back(std::move(rec));
        m.d.current.compactions.back().tokens_after =
            cmd::estimate_wire_tokens(m.d.current);
        {
            const auto& r = m.d.current.compactions.back();
            AGT_LOG(Model, Info, "compaction.done",
                    "up_to={} summary_bytes={} tokens_before={} "
                    "tokens_after={} reclaimed={}",
                    r.up_to_index, r.summary.size(),
                    r.tokens_before, r.tokens_after,
                    r.reclaimed().value_or(-1));
        }
        m.d.current.updated_at = m.wall_now();

        // 不执行 冷冻的再水合物 -frozen 前缀按紧凑
        // 度排列不变(记录不是可变 - 只有... . . ..
        // 传输 载荷 将前缀换为摘要。 从抓取中重建它,
        // 将其中的每个元素身份都垃圾化 m.ui.frozen
        // 鱼雷发射矢量 maya 指针 -keyed 组件
        // 缓存 内在的 ComponentElements
        // (减记) 正文 工具卡),不携带自己的 hash_id。
        // 用户一方的明显症状是 -frame 鬼魂/ 满重
        // -layout 全体会议 回滚缓冲区 紧凑完成
        // 的那一刻
        //
        // 压缩分解器不需要插入到这里:
        // `needs_compaction_divider(i)`
        // 输入 freeze_range 咨询 m.d.current.compactions
        // 下一张 freeze_through 调用 (下一个用户转动
        // ) freeze_through 火势 submit_message
        // 冻结 -prior-prefix 路径)将击中
        // `i == up_to_index` 并自然地在用户下一个
        // 消息之前按下分隔符。已涂过的冻结前缀将保留 字节-identical
        // 整个过渡。

        // 快速( R) -refill 如果这个紧凑的降落在
        // `kRapidRefillTurns` 助手转动前一个,
        // 它数到断路器;否则,螺旋重排。
        // `kRapidRefillCount` 翻转禁用内容 标志 所以,
        // 自动 -trigger 输入 modal.cpp 停止开火。
        //
        // 绝对有效 -set 触发( kWorkingSet Cap
        // ~ 260k) 和 绑定的简块(~ 150k),
        // 健康会话紧凑到 WELL 只有在一个工具输出本身大于帖子时才
        // 会出现真正的打击 -compaction 预算 - 断开器
        // 存在的情况。 禁用是 TEMPORARY : 重新敲定
        // -enables ~10 静转后(见
        // turns_since_last_compact 内容 -arm 下方),
        // 所以一个 -off 巨大的输出不 Wedge 自动 -compaction
        // 其余时间。
        constexpr int kRapidRefillTurns = 2;
        constexpr int kRapidRefillCount = 4;
        if (m.s.turns_since_last_compact <= kRapidRefillTurns) {
            ++m.s.recent_compacts;
        } else {
            m.s.recent_compacts = 1;
        }
        m.s.turns_since_last_compact = 0;
        if (m.s.recent_compacts >= kRapidRefillCount) {
            m.s.autocompact_disabled = true;
        }

        m.s.compaction_target_index = 0;
        m.s.compacting    = false;
        m.s.compaction_ceiling = 0;
        m.s.phase         = phase::Idle{};
        // 上次报告 提示词 预览 -compaction
        // 将压缩器取回的。估计只涵盖对话,
        // 而不是系统。 提示词 和工具,
        // 因此减去差值后,保留固定部分;
        // 下一个 StreamUsage 换掉它。
        {
            const auto& last = m.d.current.compactions.back();
            const auto reclaimed = last.reclaimed();
            if (reclaimed && *reclaimed > 0) {
                const int floor = static_cast<int>(last.tokens_after * m.s.est_calibration);
                m.s.tokens_in = std::max(floor, m.s.tokens_in
                    - static_cast<int>(*reclaimed * m.s.est_calibration));
            }
        }
        // 平滑 UX :当用户消息排在紧凑后,它即将在下面
        // 开火——用户会立即看到他们的请求被发出,
        // 所以一个 "context compacted"
        // 祝酒词是他们回答的噪音 流式传输 完全跳过它。
        // 只露出快车 -refill 禁用警告( 这是可
        // 操作的信息: 下次告诉用户操作/ 压缩) ,
        // 并且只显示 "context compacted"
        // 用户可能还在看 加载动画.
        // 即使如此,保持简洁(1.5s),
        // 而不是前4s所居的,它们与用户真正想要阅读的
        // 下一个事物争夺注意力.
        const bool queued_about_to_fire = !m.ui.composer.queued.empty();
        auto now_ts = m.now;
        if (m.s.autocompact_disabled) {
            m.s.status        = "auto-compact disabled (rapid refill); use /compact manually";
            m.s.status_until  = now_ts + std::chrono::seconds{6};
        } else if (queued_about_to_fire) {
            // 静态路径——队列中的消息成为下一个可见的 事件 输入
            // UI 不需要土司,我需要你帮忙
            m.s.status.clear();
            m.s.status_until = {};
        } else {
            m.s.status        = "context compacted";
            m.s.status_until  = now_ts + std::chrono::milliseconds{1500};
        }
        // 压缩的笔录是值得坚持的。 以数值为单位,
        // 并分批分到这区块的3个回文中, 所以,
        // 保存 不论我们离开的方式。
        Cmd save = save_current(m);
        // 手拿回来 -freed 竞技场回归 OS。现在我们不放弃工具
        // 输出——唯一自由的 字节 紧凑的 数据流's input/output
        // 缓冲区 - 但仍然便宜,并保持历史
        // "compact → RSS dips" 用户在 glibc 上发出有效的信号。
        // 没有 -op 打开 musl/macOS/Windows.
        release_to_kernel();

        // 排出用户在压缩时排队的任何信件。 形状与 posit 相同
        // -StreamFinished 排水量在功能末端,
        // 但量身定制: 我们已经在闲置,没有 待处理 工具可以踢,可以踢,也可以踢,
        // 可以击球
        if (queued_about_to_fire) {
            auto& head = m.ui.composer.queued.front();
            m.ui.composer.text        = std::move(head.text);
            m.ui.composer.attachments = std::move(head.attachments);
            m.ui.composer.cursor      = static_cast<int>(m.ui.composer.text.size());
            m.ui.composer.queued.erase(m.ui.composer.queued.begin());
            auto sub_cmd = submit_message(m);
            return Cmd::batch(std::move(save), std::move(sub_cmd));
        }
        if (m.s.status.empty()) return save;
        auto stamp = m.s.status_until;
        auto ttl_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            m.s.status_until - now_ts);
        return Cmd::batch(std::move(save),
                          Cmd::after(ttl_ms + std::chrono::milliseconds{50},
                                     Msg{ClearStatus{stamp}}));
    }
    bool any_truncated = false;
    const bool max_tokens_hit = (stop_reason == StopReason::MaxTokens);
    if (!m.d.current.messages.empty()) {
        auto& last = m.d.current.messages.back();
        // 封住转弯的遥测,同时 phase::Active
        // 仍然存在。这是 最后 切换到 Idle 时,
        // 将 Ctx 和每个 重试 复用到的用户
        // StreamUsage ;
        // 时间和 传输层 健康在这里是可以理解的。
        if (last.role == Role::Assistant) {
            if (const auto* a = active_ctx(m.s.phase)) {
                // `= Telemetry{}` 而不是说 `.emplace()`
                // 两者都做同样的事情,但crange拒绝 安置:它评价
                // is_constructible_v<Message::Telemetry>
                // 以 FALSE 用于此嵌入式集合,
                // 而 is_default_constructible_v 谓 TRUE ,
                // 和 optional::emplace 被限制在前者。 gcc 说两者都是真实的,
                // 因此, 肌肉释放腿(clang 22) 失败在一个文件上,
                // 遥测本身 编译 两者 编译器.
                if (!last.telemetry) last.telemetry = Message::Telemetry{};
                auto& t = *last.telemetry;
                const auto now_st = m.now;
                auto ms_between = [](auto from, auto to) -> std::uint32_t {
                    if (from.time_since_epoch().count() == 0) return 0;
                    if (to <= from) return 0;
                    const auto d = std::chrono::duration_cast<
                        std::chrono::milliseconds>(to - from).count();
                    // 倒不如包:跳的时钟是腐败的读物,
                    // 看起来似是而非的腐败的读
                    // 品比明显封顶的读物还要糟糕.
                    return d > 0xFFFFFFFFLL ? 0xFFFFFFFFu
                                            : static_cast<std::uint32_t>(d);
                };
                // 拆分到第 字节 内容。 在此之前,
                // 提供者正在思考或排队; 之后我们正在接收。
                t.ttft_ms   = ms_between(a->started, a->first_delta_at);
                t.stream_ms = a->first_delta_at.time_since_epoch().count()
                                ? ms_between(a->first_delta_at, now_st)
                                : 0;
                t.transient_retries =
                    static_cast<std::uint16_t>(a->transient_retries);
                t.mid_stream_failures =
                    static_cast<std::uint16_t>(a->mid_stream_failures);
                t.no_progress_failures =
                    static_cast<std::uint16_t>(a->no_progress_failures);
                t.wire_bytes = a->live_delta_bytes > 0xFFFFFFFFull
                                 ? 0xFFFFFFFFu
                                 : static_cast<std::uint32_t>(a->live_delta_bytes);
            }
        }
        // 将任何文字排入平滑处 缓冲区 之前,
        // message_stop 不应留下 -flight 字节 无形.
        if (last.role == Role::Assistant && !last.pending_stream.empty()) {
            last.streaming_text += last.pending_stream;
            last.pending_stream.clear();
        }
        if (last.role == Role::Assistant && !last.streaming_text.empty()) {
            if (last.text.empty()) last.text = std::move(last.streaming_text);
            else                   last.text += std::move(last.streaming_text);
            std::string{}.swap(last.streaming_text);
        }

        // ── 转动产生 无内容 一定要说 ───────────────────
        //
        // 报为 "it just goes silent" 现载于本文件附件。
        // 已保存 线程 :连续三个转弯 text_len=0,
        // tool_calls =0,一个有效的思维块,出=2.4个符号,
        // 和 NO ttft/wire_bytes 完全没有内容 字节 永远的到来。
        // HTTP 那时是200,而 SSE 不错 -formed ,
        // 因此没有任何地方报告错误。用户看到一个空泡,无法从绞刑中看出拒绝。
        //
        // 电话 OpenAI 传输层 已经有过
        // ensure_nonempty_turn (a) Quen打捞
        // 工作以来的准确情况; Anthropic 路径从未增长到等效。
        // 此处 而不是在那个 传输层 这是每个供应商的 唯一一个
        // 地方 数据流 趋同,所以 传输 安静的状态 永远不能再对其中的
        // 某些人保持沉默了
        //
        // IT GOES IN `error`, 不 IN `text`。
        // 写入 agentty 自己的话语进入 `text` 把它们放在模特
        // 的嘴里: 渲染 以模特的嗓音 代言人的名字背后的助理 PERSIST 那样,
        // 以后转身, 把它们重新播放到模型上,就像它所说的。
        // Message::error 这一点是存在的(见宣言:
        // 与正文分开保存，让助手已生成的部分输出和失败原因分别渲染。
        // 渲染 永远无法
        // 回到 传输.
        //
        // 中间 -turn 和解是: 解决 不 空转 :
        // StopReason::ToolUse 表示模型是中点
        // -flight 文本可以合法地为空白(工具) 调用 仅见a
        // TERMINAL 已存在 `error` 也被单独抛在一边 -
        // - -- 一个真正的 数据流 失败已经说了比这更好的话了
        if (last.role == Role::Assistant
            && last.text.empty()
            && last.tool_calls.empty()
            && !last.error
            && stop_reason == StopReason::MaxTokens) {
            // 全体 max_tokens 寻找推理,但什么也没说。
            // 每个供应商都一样: 放下转弯,
            // 再用模型所花费的最小努力再发送一次。
            if (auto* a = active_ctx(m.s.phase); a && !a->reasoning_starved_retry) {
                a->reasoning_starved_retry = true;
                AGT_LOG(Wire, Warn, "stream.reasoning_starved",
                        "model={} retrying at lowest effort", m.d.model_id.value);
                m.ui.view_cache.drop(m.d.current.id, last.id);
                m.ui.view_cache.drop(m.d.current.id, MessageId{last.id.value + "#r"});
                m.d.current.messages.pop_back();
                m.d.current.messages.push_back(Message{.role = Role::Assistant});
                if (!reschedule_streaming(m.s.phase, [](phase::Active&) {}))
                    return Cmd::none();
                m.s.status = "reasoning used the whole budget \xc2\xb7 retrying with less\xe2\x80\xa6";
                return cmd::launch_stream(m);
            }
        }
        if (last.role == Role::Assistant
            && last.text.empty()
            && last.tool_calls.empty()
            && !last.error
            && stop_reason != StopReason::ToolUse) {
            // 命名原因当 传输 给了一个。 `refusal` 是个真实的
            // stop_reason (docs.claude.com/en/api/handling-stop-reasons ,
            // 该 parse_stop_reason 默然折成无
            // 量无边 这也是为什么这个转折看起来 完全一样
            switch (stop_reason) {
                case StopReason::Refusal:
                    last.error = "the model declined to answer "
                                 "\xc2\xb7 rephrasing usually works";
                    break;
                case StopReason::MaxTokens:
                    last.error = "hit the output token limit before "
                                 "any text arrived";
                    break;
                case StopReason::ContextExceeded:
                    last.error = "the context window filled up before "
                                 "the model could reply \xc2\xb7 try /compact";
                    break;
                // EndTurn / StopSequence /
                // 未具体说明: 传输 说出一个正常的结局,但又不给我们
                // 任何东西。说出准确的,再也没有更多的了。
                // guard_truncated_tool_args 见上文。
                default:
                    last.error = "the model ended the turn without "
                                 "producing any output";
                    break;
            }
            AGT_LOG(Wire, Warn, "stream.empty_turn",
                    "terminal stop with no content stop_reason={}",
                    to_string(stop_reason));
        }

        // Pre-settle StreamingMarkdown,
        // 所以消息的渲染高度被锁定在BEFORE中,下个view()运行.
        // 惰性 finish()在视图中将助理消息高度在当值时以1+行移动.
        // 代码块的结束 ```，或末尾的块边界。
        // 承诺 末尾 输入前缀块- maya diff看到
        // 收缩 并释放出收缩路径, 但排成一行已经溢到原生
        // 回滚缓冲区 保留预 -shrink ,
        // 将边框碎片留在 回滚缓冲区↔可视区域 接口边界.
        // 设置在此匹配 agent_session's
        // `m.md.finish() + push to frozen` 学科:每个更新步骤一个连贯的过渡。
        //
        // 解决 当前 无条件. agent_session's MessageStop
        // 完全这样( m.md.finish() 上一部密码装了200毫秒
        // request_finalize() GLIDE 当转弯时,
        // finish() 改为 meta.cpp 滑翔机一旦被冲掉就被滴答了
        // 动画的交接需要密集~16毫秒 帧 来保持现场 -tail
        // 高度稳定,而 逐步显示 光标滚动到边缘; at FPS =
        // 0 超过 SSH/tmux 编号 帧 距离80米,高度飘移在中间
        // -glide 最终冻结的 diff 移动 源码树 反对:
        // 过期 prev_cells - 重复转弯 回滚缓冲区 (所报之虫).
        // finish() 决定 逐步显示 覆盖面板 立即,
        // 所以活的高度等于... ... settled/frozen
        // 高度在 相同 帧 冻液的传递是干净的 唯一损失的是 ~200
        // ms 的 邮箱 -stream 滑翔机——正是交易
        // agent_session 制造。 制造。
        if (last.role == Role::Assistant && !last.text.empty()) {
            if (m.env.reveal_end_glide()) {
                // #5: 已定界 GLIDE 而不是贴面,在 message_stop 编号 逐步显示
                // 光标仍然保持稳定 -state 积压(____________________
                // wire_cps × drain_secs ——以0.40重调,一行或多行).
                // settle_message_md's finish() 将
                // 逐步显示 都写在 一个 帧。反之,武装部件的适应性最终坡道:
                // 光标冲刺到边缘~ ramp_ms 继续使用时,部件会自动翻转
                // 末尾 视止,和 meta.cpp 延期结算 -freeze (
                // pending_settle_freeze ,
                // 门打开 live_tail_reveal_settled
                // ——由解放军武装. is_idle 路径或重置 -checked
                // 运行现在的滴答( E) -shape-neutral
                // settle_message_md + 之后冻结。
                // 高度由于坡道密集而保持锁定 -frame 此处动画( 任意)
                // 终端 同步输出, SSH 包括第2款(a)项。
                auto& cache =
                    m.ui.view_cache.message_md(m.d.current.id, last.id);
                if (!cache.streaming)
                    cache.streaming =
                        std::make_shared<maya::StreamingMarkdown>();
                cache.streaming->set_content(last.text);
                cache.streaming->request_finalize(/*ramp_ms=*/200);
            } else {
                settle_message_md(m, last);
            }
        }
        // 任何冲动 tool_calls 其 人 名 StreamToolUseEnd 从来没有被开除过...
        // Anthropic 通常发送 content_block_stop 每个工具块,但代号 /
        // message_stop 剪切( E) -offs 还可以跳过它。
        for (auto& tc : last.tool_calls) {
            if (!tc.args_streaming.empty() && tc.is_pending()) {
                try {
                    tc.args = json::parse(tc.args_streaming);
                    // `cmd` → `command` 等,在所需时间之前 -field 警卫:
                    // 警卫和每个下游消费者都按字面意思读出犬科钥匙
                    // (发现:Laguna S 2.1 shell)。
                    (void)detail::canonify_tool_args(tc.name.value, tc.args);
                    tc.mark_args_dirty();
                } catch (const std::exception& ex) {
                    auto salvaged = salvage_args(tc);
                    if (!salvaged.empty()) {
                        tc.args = std::move(salvaged);
                        tc.mark_args_dirty();
                    } else {
                        // 区分 "never closed" (解析 失败
                        // 和 无中位数 -string 切除)
                        // "truncated mid-string" (
                        // salvage_args 被拒绝,
                        // 因为 close_partial_json 半个
                        // 字的结尾部分 -written 值。
                        // 中间值 -string 案件为: 重试-eligible
                        // 标记为: 标志 工具 待处理 ,然后让 重试 按钮下方的
                        // 助理占位符并重置 -launch。仅在打开 重试
                        // -budget 工具是否显示为失败( 处理于 重试 块的
                        // 掉下来 -through).
                        const bool mid_string =
                            agentty::tools::util::ended_inside_string(
                                tc.args_streaming);
                        if (mid_string) {
                            tc.stream_mid_string_truncated = true;
                            // 休假状况 待处理 ; 缩写 重试 块下方将看到
                            // any_truncated 和关于 -launch.
                        } else {
                            auto now = m.now;
                            tc.status = ToolUse::Failed{
                            tc.started_at(), now,
                            std::string{"tool args never closed: "}
                                + ex.what()};
                        }
                    }
                }
            }
            std::string{}.swap(tc.args_streaming);
            if (tc.is_pending()) {
                if (guard_truncated_tool_args(tc, m.now)) any_truncated = true;
                // 中间 -string 截断点也参与透明
                // -retry 循环—— 传输 死在字符串值内,
                // 没有救生是安全的,所以唯一可以使用的工具的路径 调
                // 用 重新为 -launching 编号 数据流.
                // Tool 留下来 待处理 这里;
                // 重试 块打开占位符。打开 重试-budget
                // 已耗尽以下块。 -string 留言。
                if (tc.stream_mid_string_truncated) any_truncated = true;
            }
        }
    }

    // ── max_tokens 断线处理 ──
    //
    // 何时 stop_reason == max_tokens ,
    // 生成时会停止。如果工具块此时被流到,
    // 则其 input_json 被切换到 – 它有两个失败模式 :
    //
    //   (a) 截断是"syntactically obvious"
    //       (解析器无法关闭JSON,或缺少所需的模式定义字段)。
    //       guard_truncated_tool_args 以上各项
    //       `any_truncated` , 且工具的通用不完整失败 -args 留言。
    //
    //   (b) 截流在战术上有效的地点着陆——常见于 `write`
    //       /`edit` 其 人 名 `content`
    //       字段恰好关闭。 -body。参数 解析,
    //       模式定义 验证合格, 守卫返回不实。
    //       工具会 分派 写半个 -truncated 文件,
    //       表面显示为沉默的腐败或令人困惑的工具 -runner
    //       错误误导模型,使其认为其论证形状是错误的。
    //
    // 两者都需要同样的处理: 以一个将真正原因命名的信息来失败
    // 工具( / / 或 / i / + / )。
    // max_tokens 并用可操作工作(较小)来标注模型 载荷 /
    // `edit` 结束 `write` / 多个 调用 别 重试
    // ——预算下次不变,模式会同样烧掉. 重试 下方的守卫已经
    // 通过 `!max_tokens_hit`.
    //
    // 我们强迫 -fail 仍然如此 ? ? 每一个?
    // -pending 工具,而不仅仅是被抓住的守卫,来覆盖案件(b)。
    // 终端 状态( 由较早的子程序提交) -turn ) 则
    // 留守.
    if (max_tokens_hit
        && !m.d.current.messages.empty()
        && m.d.current.messages.back().role == Role::Assistant) {
        constexpr std::string_view kMaxTokensExplanation =
            "Output token cap (max_tokens) was reached before the tool "
            "input finished streaming, so the call was cut off. Even if "
            "the args parsed, the body is likely truncated. Retry with a "
            "smaller payload: prefer `edit` over `write` for long files, "
            "or split the change across multiple calls.";
        const auto now = m.now;
        for (auto& tc : m.d.current.messages.back().tool_calls) {
            if (tc.is_pending()) {
                tc.status = ToolUse::Failed{
                    tc.started_at(), now,
                    std::string{kMaxTokensExplanation}};
            } else if (auto* f = std::get_if<ToolUse::Failed>(&tc.status);
                       f && f->output.starts_with("Tool call arguments look incomplete")) {
                // guard_truncated_tool_args 已
                // 使用通用信件失败。升级到 max_tokens-aware 版本,
                // 所以模型得到 一个单一的连贯的故事。
                f->output.assign(kMaxTokensExplanation);
            }
        }
        (void)any_truncated;  // 以上(a)和(b)项所述
    }

    // 透明 重试 打开 上游 截断—— libcurl's TCP 保持不动,
    // 无法防止边缘 LB 关闭闲置连接。当此事件发生在中点时 -tool-input
    // 我们默默地 -launch 上下文,在 kMaxTruncation
    // Retries 上盖上。
    if (auto* a = active_ctx(m.s.phase);
        a && any_truncated
        && !max_tokens_hit
        && a->truncation_retries < kMaxTruncationRetries
        && !m.d.current.messages.empty()
        && m.d.current.messages.back().role == Role::Assistant) {
        auto& last = m.d.current.messages.back();
        const bool has_committed_work =
            !last.text.empty() ||
            std::ranges::any_of(last.tool_calls, [](const auto& tc) {
                return tc.is_done() || tc.is_running();
            });
        if (!has_committed_work) {
            ++a->truncation_retries;
            // 释放两者 缓存 插槽(主要+) "#r" 说到
            // 这个道理 重试 新的占位符不会继承 过期 逐步
            // 显示 部件。
            m.ui.view_cache.drop(m.d.current.id, last.id);
            m.ui.view_cache.drop(m.d.current.id,
                                 MessageId{last.id.value + "#r"});
            m.d.current.messages.pop_back();
            Message placeholder;
            placeholder.role = Role::Assistant;
            m.d.current.messages.push_back(std::move(placeholder));
            // 流式传输 – 流式传输(truncation 重试).
            // 重用相同的ctx——仅-incremented
            // truncation_retries的计数器持续使用,因此kMax
            // Truncation Retries caps在转弯内跨重试工作.
            if (!reschedule_streaming(m.s.phase, [](phase::Active&) {}))
                return Cmd::none();   // 迟到 事件 从空闲:无 -op
            m.s.status = "retrying (upstream cut off)…";
            return cmd::launch_stream(m);
        }
    }

    // 重试 预算已用尽(或 max_tokens 或承诺的工作
    // 封锁了 重试 ),工具仍然标注在中间 -string 断了,
    // 我们留下了 待处理 刚才是这样 重试 循环可以看见它;
    // 现在显示最后的失败消息, 这样模型可以得到一个连贯的 终端
    // 故事而不是静态 -Pending 工具,
    // 该工具 kick_pending_tools 将 分派 以空格为格.
    if (!m.d.current.messages.empty()
        && m.d.current.messages.back().role == Role::Assistant) {
        const auto now_ts = m.now;
        for (auto& tc : m.d.current.messages.back().tool_calls) {
            if (tc.stream_mid_string_truncated && tc.is_pending()) {
                tc.status = ToolUse::Failed{
                    tc.started_at(), now_ts,
                    "tool args truncated mid-string \u2014 the wire cut "
                    "off inside a string value (likely `content` / "
                    "`command` / `new_text`), so the body is incomplete "
                    "and the call was refused. Re-emit the tool with the "
                    "full payload — prefer `edit` over `write` for long "
                    "files, or split the change across multiple calls."};
            }
            tc.stream_mid_string_truncated = false;
        }
    }

    // 将快速-refill的转盘倒在每一个固定的助手转盘上
    // (无论这轮是否发生契约).
    // 契约 -finalize 上方分支将其重置为 0;
    // 非 -compact 转动后,会增加,所以静静的拉伸 最终会恢复
    // -arms 自动 -compaction ,允许 `recent_compacts`
    // 重置下一个触发器。 INT_MAX/2 避免长时间的届会过多。
    if (m.s.turns_since_last_compact < 1000000) ++m.s.turns_since_last_compact;
    if (m.s.autocompact_disabled
        && m.s.turns_since_last_compact > 10) {
        // 长长的静脉线——关于 -enable 自动
        // -compact。用户要么停止触发巨大
        // 的工具输出,要么对话自然地从电击触发器上飘走。
        m.s.autocompact_disabled = false;
        m.s.recent_compacts      = 0;
    }

    // 智能模式 CASCADE 反馈。复杂的热力将这个转弯的下层 放在前面;
    // 现在从管弦乐手的下层修正它 ACTUALLY 已经过了,数数 `task`
    // 代表团在2000年9月26日至30日于日内瓦 -settled 助手转弯 :
    // 重代表团在转弯 热力在 -rated (简单)是
    // 证明它确实很复杂——提出偏见;一个微不足道的零
    // 代表团回答一个问题。 -rated (复杂)转弯,
    // 先偏斜 DECAYS 每一个转弯都走0步,
    // 一个异常点就永远不动, 然后信号就适用了。只有协调时才有意义。
    //
    // GATE : 运行级联 一次 每个用户转动,在 FINAL 和解——不是每个
    // 工具回合 -trip. finalize_turn 每场火 StreamFinished
    // 工具 -using 转弯产生若干( E) StopReason::ToolUse
    // ) 之前的实 EndTurn 运行级联将衰减偏差 N 时间, 数后悔 N
    // 时间和双倍 -count 代表团的证据,转弯时间还在中
    // -flight 如果模型需要工具 OR 任何 pending/ 已核准
    // 工具 调用 已排队 kick_pending_tools 以宣传.
    const bool tools_pending =
        stop_reason == StopReason::ToolUse
        || [&]{
               for (auto it = m.d.current.messages.rbegin();
                    it != m.d.current.messages.rend(); ++it) {
                   if (it->role != Role::Assistant) break;
                   for (const auto& tc : it->tool_calls)
                       if (tc.is_pending() || tc.is_approved()) return true;
               }
               return false;
           }();
    // 转弯了( 不再) 传输 圆形 -trips : 放下枪
    // -flight 路由印章,所以状态徽章 回到 选择面板 选择时闲置。
    // 印章已经停留在每个助手Message上,
    // 所以记录稿保留了它的出处——这只澄清了LIVE指示器.
    if (!tools_pending) {
        m.s.smart_turn_model = ModelId{};
        m.s.smart_turn_role.reset();
    }
    if (!tools_pending && m.d.smart.orchestration() && !m.d.current.messages.empty()) {
        int delegations = 0;
        bool tool_failure = false;
        for (auto it = m.d.current.messages.rbegin();
             it != m.d.current.messages.rend(); ++it) {
            if (it->role != Role::Assistant) break;   // 转弯只跑
            for (const auto& tc : it->tool_calls) {
                if (tc.name == "task") ++delegations;
                // 构建/ 测试/ 诊断失败 调用 转弯就是个地盘 -truth
                // "the cheap route may have been wrong" 信号
                if (std::holds_alternative<ToolUse::Failed>(tc.status)
                    && (tc.name == "shell" || tc.name == "diagnostics"
                        || tc.name == "test" || tc.name == "edit"))
                    tool_failure = true;
            }
            if (!it->text.empty() || !it->tool_calls.empty()) break; // 最新真实的 msg
        }
        const auto cx = m.s.smart_turn_complexity;
        // 衰变先向中立方向前进
        if (m.s.smart_effort_bias > 0) --m.s.smart_effort_bias;
        else if (m.s.smart_effort_bias < 0) ++m.s.smart_effort_bias;
        // 下级 -rated : 廉价 -classified 转动它,产生真正的平行工作。
        int regret = 0;
        if (delegations >= 2
            && (cx == smart::Complexity::Simple || cx == smart::Complexity::Standard))
            regret = +1;
        // 完毕 -rated : 复杂 -classified 却又不给任何授权,
        // 实际上只是直接答复。
        else if (delegations == 0 && cx == smart::Complexity::Complex)
            regret = -1;
        // 失败 build/test 转弯是真实的证据,转弯比路过的要难;
        // 不管代表团人数多少,偏见都上升(事实真相胜过代表团)
        // -count 代理服务器。这是 SESSION 级联——
        // 在 -memory ,每转弯,夹住,退出。
        // -workspace 变体 中,该信号已删除;见 RoleConfig.
        if (tool_failure && regret <= 0) regret = +1;
        m.s.smart_effort_bias += regret;
        // 对称夹住会话级联偏差——一种持续的环境(第1段) "smart.bias_clamp" ,
        // 或 ,或 AGENTTY_SMART_BIAS_CLAMP ),
        // 已解决 RoleConfig 开始时。 -correction
        // 能够从基线中漂移努力。
        const int kBiasCap = m.d.smart.bias_clamp;
        if (m.s.smart_effort_bias >  kBiasCap) m.s.smart_effort_bias =  kBiasCap;
        if (m.s.smart_effort_bias < -kBiasCap) m.s.smart_effort_bias = -kBiasCap;
    }

    // ── 批次( T) -width KPI ─────────────────────────────────────────
    // Tool 调用 每轮型号 -trip 谓 该 墙 -clock 代理会话
    // 中的杠杆( 每回合) -trip 成本秒( 秒) TTFT + 数据流。
    // 记录此回合工具批次的宽度, 如此。 提示词-shaping 工作
    // 有可衡量的目标: 提高avg宽度=回合减少 -trips 每一个任务。
    if (!m.d.current.messages.empty()
        && m.d.current.messages.back().role == Role::Assistant
        && !m.d.current.messages.back().tool_calls.empty()) {
        AGT_LOG(Perf, Debug, "turn.tool_batch", "width={}",
                m.d.current.messages.back().tool_calls.size());
    }
    // 转弯完成, 故持续。 折叠成 `kp` 而不是运行在
    // 这里: 下方的每一次返回都携带 kp, 因此它到达了主机
    // 所有五条路径, 而不会通过它们线程连接第二条本地路径。
    auto kp = Cmd::batch(save_current(m),
                         cmd::kick_pending_tools(m),
                         maybe_auto_title(m));
    // 由下面的闲置-settle块设定,
    // 当答复中包含可流的壳块;分批进入任何回路火。
    Cmd block_toast = Cmd::none();

    if (m.s.is_idle() && !m.ui.composer.queued.empty()) {
        auto& head = m.ui.composer.queued.front();
        m.ui.composer.text        = std::move(head.text);
        m.ui.composer.attachments = std::move(head.attachments);
        m.ui.composer.cursor      = static_cast<int>(m.ui.composer.text.size());
        m.ui.composer.queued.erase(m.ui.composer.queued.begin());
        auto sub_cmd = submit_message(m);
        return Cmd::batch(std::move(kp), std::move(sub_cmd));
    }

    // LOOP 模式( 模式) ^B :转弯完成,用户武装一个消息重复—
    // —re -send 编号 ARMED 快照 而不是现在的作曲家(他们
    // 可能拥有) 带类型的 如下 -up 同时;见 loop_text).
    //
    // 已订购 之后 排队排水: 明确排队的信息是新指令,
    // 并排位高于常态循环, 排队后会再次在转弯时开火。
    // 作曲者会恢复到武装文本,
    // 所以记录和输入都同意发送的内容,
    // 因此解除了 提示词 而不是一个空盒子。
    if (m.s.is_idle() && m.ui.composer.looping()) {
        // 退避 门。 无效的重复已装入 loop_wait_until_ms ;
        // 直到它通过,循环是武装的,但 SLEEPING ,而不是发送。
        // 一个滴答让我们回到这里(在等待的时候,请填写一个武器) 待处理 ),
        // 所以,现在只要拒绝开火,就不会失去任何东西。
        if (!m.ui.composer.loop_ready(
                maya::anim::default_clock().now_ms())) {
            return kp;
        }
        // 这种迭代是从健康状态开始的——前一次
        // 转折正常结束,所以忘记任何较早的失败。
        m.ui.composer.loop_note_success();
        ++m.ui.composer.loop_iterations;
        m.ui.composer.text        = m.ui.composer.loop_text;
        m.ui.composer.attachments = m.ui.composer.loop_attachments;
        m.ui.composer.cursor      = static_cast<int>(m.ui.composer.text.size());
        auto sub_cmd = submit_message(m);
        // 将作曲者排出水面——恢复武装 载荷 所以盒子会保持
        // DISPLAYING 重复的重复。 作曲家读作
        // -only 循环时, 用户不会不小心从草稿中分辨出来,
        // 所以这是状态读出。 loop_text.
        m.ui.composer.text        = m.ui.composer.loop_text;
        m.ui.composer.attachments = m.ui.composer.loop_attachments;
        m.ui.composer.cursor      = static_cast<int>(m.ui.composer.text.size());
        return Cmd::batch(std::move(kp), std::move(sub_cmd));
    }

    // 冷静点 agent_session 推着助手转身
    // m.frozen 时间 MessageStop 我们同样这样
    // 做——一旦 数据流 真正闲置( 无) 待处理 工具,没有队列信息,
    // 将所有已解密的信息输入 m.ui.frozen 并让活人 末尾
    // 缩进为空。 从这里开始, 下一页 view() 读取冷冻的
    // 转折通过 list_ref (零) 数据 -copy 和
    // 现场直播 末尾 在下次提交之前, 任何绘制都不要按新占位符。
    if (m.s.is_idle()) {
        // 解析现场的每个助理消息 末尾 当前 — finish() 决定 逐步
        // 显示 覆盖面板 财务报告和已审计财务报表 锁 完全一样
        // agent_session 时间 MessageStop (
        // m.md.finish() 然后我们定好了 pending_settle_freeze
        // 这么说吧 meta.cpp 滴答执行下一轮的实际冻结 帧 ——到哪一步
        // view() 已刷新已解决( 后) -finish) 源码树 输入
        // maya's prev_cells 现场直播 -tail 冻结的交接是 字节
        // -identical 缓存 HIT (零还原) -emit ,最便宜的过渡 慢
        // SSH 传输).
        //
        // 变了 旧密码装了200毫秒 request_finalize
        // () GLIDE 在此保留部件为 活页,
        // 推迟 finish() 滑翔机被耗尽后,该动画需要密集
        // 帧 使活体高度稳定; FPS =0 超过 SSH 细小
        // 的虱子让高度飘移在中间 -glide ,所以冻结 抖动
        // 源码树 反对: 过期 prev_cells 并困住
        // 一个重复的转弯 回滚缓冲区。完成后立即删除动画窗口。
        for (std::size_t i = m.ui.frozen_through;
             i < m.d.current.messages.size(); ++i) {
            auto& mm = m.d.current.messages[i];
            if (mm.role != Role::Assistant || mm.text.empty()) continue;
            // #5:在互动中 终端 ,让静静 -gliding 逐步显示
            // 完成有界斜坡,而不是强迫 -finishing (一种面条).
            // finalize_turn 已经武装 request_finalize
            // 自动调试 末尾 消息; 跳过任何其部件仍在动画中的消息——
            // pending_settle_freeze 门在下面等待
            // live_tail_reveal_settled 冰冻之前,
            // 以及 meta.cpp 滴答运行 settle_message_md 一旦部件自行安顿。
            if (m.env.reveal_end_glide()) {
                const auto* c = m.ui.view_cache.peek(m.d.current.id, mm.id);
                if (c && c->streaming && c->streaming->is_animating())
                    continue;
            }
            settle_message_md(m, mm);
        }
        // 下一张滴答时冻结( Q) meta.cpp ),
        // 门打开 live_tail_reveal_settled ()——
        // 我们已经说完了,所以下个时刻立即发生冻结火灾。 帧.
        m.ui.pending_settle_freeze = true;

        // 可运行 -code 移动。刚才的回复已经解决,
        // 用户正在读它——如果它携带壳块,则表面显示 Ctrl +G
        // 负担 当前 ,而命令则在屏幕上。只计 RUNNABLE 块
        // 数( E) is_shell_language :
        // 答复就是这样 python/json 片段不应该广告 运行的密钥,
        // 只会敬酒 "isn't shell" 如果答复没有,
        // 请安静,那么,这个微软必须保持足够少的,以保持某种意义。
        if (!m.d.current.messages.empty()
            && m.d.current.messages.back().role == Role::Assistant
            && !m.d.current.messages.back().text.empty()) {
            const auto blocks = code_blocks::extract_code_blocks(
                m.d.current.messages.back().text);
            int runnable = 0;
            for (const auto& b : blocks)
                if (code_blocks::is_shell_language(b.language)) ++runnable;
            if (runnable > 0) {
                block_toast = set_status_toast(m,
                    "\xe2\x96\xb6 " + std::to_string(runnable)
                        + (runnable == 1 ? " runnable code block"
                                         : " runnable code blocks")
                        + " \xe2\x80\x94 Ctrl+G to run",
                    std::chrono::seconds{6});
            }
        }
        // 重温微软,转弯的编辑是 ALREADY LIVE。
        // 在修改条后,复议窗口会默默关闭下一个提交(
        // 默示接受) OFF 默认情况下, 任何信息都不要
        // 告诉用户编辑等待审查 – 表面 ^R 与代码
        // 相同的读取时间 -block 动起来,
        // 代码 -block 土司胜于两者(运行代码是比较罕见的,
        // 更刻意的行为); 更改条/调色板仍然覆盖了审查路径。
        if (block_toast.is_none()
            && !m.d.pending_changes.empty() && !m.d.show_changes_strip) {
            int files = static_cast<int>(m.d.pending_changes.size());
            block_toast = set_status_toast(m,
                "\xe2\x9c\x8e edited " + std::to_string(files)
                    + (files == 1 ? " file" : " files")
                    + " \xe2\x80\x94 Ctrl+R to review",
                std::chrono::seconds{6});
        }
    }

    // 员额 -turn 闲置自动 -compaction.
    //
    // 转弯结束,没有 待处理 工具,没有队列信息。
    // 这是自然的暂停时刻——用户正在读取模型的输出,
    // 而不是输入。如果 传输 估计数超过阈值(约
    // 80%) context_max 带有输出
    // -reserve ,在背景中踢出压缩。
    // 当用户输入下一封信时, 传输 视图已被摘要替换,
    // 下一次请求将按大小的一小部分出局。
    //
    // 阈值镜像旧提交 -time 公式( 公式)
    // context_max - kOutputReserve -
    // kCompactSlack),因为其意图相同:
    // 下一轮是否有触及上下文上限的风险？
    // 区别在于 WHEN 我们检查一下,我们检查一下 -turn
    // 闲置边界,不提交时——所以紧凑圆在用户阅读时发生,而不是封存.
    //
    // 普通-turn路径上的Soft-trim已经通过静静地放下最古老的原始转折来处理
    // 下一次请求将超出上限。
    // 所以这个触发器纯粹是一个质量优化:摘要 > 切变.
    // 如果压缩失败(rat-limited,网络掉落),
    // 用户仍然通过软-trim通过下一轮转折.
    // 门槛是 StreamState::compaction_threshold
    // () = min(95%的窗口,窗口 - 20k)。
    // DEEP 目的:尽量保持最大直播上下文和紧凑。
    // 提示词-cache HIT 跨转弯(1个) -hour Anthropic
    // 锚/ OpenAI prompt_cache_key ),
    // 所以一个大的活前缀成本很少,而每个 COMPACTION 昂贵
    // ( 摘要请求 + 缓存 重置在下一回合。 射击很少( 95%,
    // 而不是提前) 将全部符号燃烧最小化。 总结的片段被分开。
    // wire_messages_for_compaction 所以简约请求
    // 本身 即使在1M窗口上也保持便宜.
    if (m.s.is_idle()
        && !m.s.compacting
        && !m.s.autocompact_disabled
        && m.d.persisted.auto_compact
        && m.s.context_max > 0
        && m.ui.composer.queued.empty()
        && !m.d.current.messages.empty()) {
        const int threshold = m.s.compaction_threshold();
        // 校准原始的( ) 字节-estimate 以从前次
        // 转折中吸取的活性校正因子为单位, 实际的符号计数,
        // 所以主动触发器跟踪实际的标定器而不是固定的
        // bytes/3.5 猜测工具上的过量射击 -heavy
        // 线程 (它以前用来用~60k+头室来发射紧固剂作为备用).
        const int est = static_cast<int>(
            cmd::estimate_wire_tokens(m.d.current) * m.s.est_calibration);
        if (threshold > 0 && std::max(m.s.tokens_in, est) > threshold) {
            // 分派 CompactContext 作为同步
            // Msg 故经同经. 状态更新函数 臂/紧合用途;
            // 臂是紧凑踢球的单一真理来源( target_index 抓获,
            // 缓冲区 明确,阶段过渡)。
            // 用户看到横幅翻转到 "compacting context…"
            // while they're reading; their next submit (when they
            // ) 或完成压缩后的土地(排队以达到最大~1
            // -2转弯) -times) OR Esc
            // -cancels 紧凑和火立刻。
            // Cmd::send ,而不是任务: 此发送 一个 信息,没有工作。
            // jaal 添加( E) `send` 确切地说, D25 ——
            // 任务会燃烧. 池 线程 改为 调用 out.send(Msg ,
            // (第6段),以及 `after(0ms)` 穿越计时器堆积 直至下一步
            // 才到达 成本是1 帧.
            auto compact_cmd = Cmd::send(Msg{CompactContext{}});
            return Cmd::batch(
                std::move(kp), std::move(block_toast), std::move(compact_cmd),
                cmd::load_library());
        }
    }

    // 转弯可能添加或编辑了技能、命令或钩子文件;
    // re -read 离开 归并 所以下次检查时会看到
    return Cmd::batch(
        std::move(kp), std::move(block_toast), cmd::load_library());
}

// ============================================================================
// stream_update — 状态更新函数 费用 `msg::StreamMsg`
// ============================================================================
// 每一个 事件 处理函数 凸起 `last_event_at` 这么一来,就被罚单了 -based 拖放 看门狗 看得出来
// "stream is alive but quiet" 发件人 "stream is stalled."

Cmd stream_update(Model& m, msg::StreamMsg sm) {
    using maya::overload;

    // 每个提供者地址工具 数据流 以稳定方式更新 ToolCallId。
    // 这是峡谷组装器的边界: 传输 适配器可发送附件三角洲或替换 快照 ,
    // 但既不订购,也不 "latest call" 提供方
    // -dependent 给
    //
    // 复制 discription 跨子 -turns (部分) OpenAI
    // -compatible 网关 薄荷 / 确定性 id(工具,指数)——字面
    // "bash:0" 转个圈就转 不 这里需要特殊处理: 此扫描范围为 messages.back
    // (b) 将“联合国”改为“联合国”。 当前 子目录 -turn 助理消息,
    // 所以一个更早的子 -turn 同样的 -id'd 调用 无法遮挡活的
    // (结果路线——一条不同的路, with_live_tool - 扫描整个现场
    // 末尾 并处理碰撞,选择非 -terminal 匹配。 ) (中文(简体)
    // ).
    auto find_streaming_tool = [&](const ToolCallId& id) -> ToolUse* {
        if (id.empty()
            || m.d.current.messages.empty()
            || m.d.current.messages.back().role != Role::Assistant)
            return nullptr;
        auto& calls = m.d.current.messages.back().tool_calls;
        auto it = std::ranges::find(calls, id, &ToolUse::id);
        return it != calls.end() ? &*it : nullptr;
    };

    return std::visit(overload{
        [&](StreamStarted) -> Cmd {
            auto now = m.now;
            // 上一张邮票 -request 闲置的时钟 缓存-lapse 预览 -compaction
            // 触发( 键盘) ( StreamState::should_compact_on_idle).
            // 不 装上门 装上 装上 -warms 时间( 不久)
            // -to-be-small )前缀也,所以它合法地重置
            // 闲置计时器.
            m.s.last_wire_at = now;
            // 阶段 变体 保证不 -null ctx 当活动时;
            // StreamStarted 仅于 submit_message
            // / 重试 已经把我们搬进 流式传输.
            if (auto* a = active_ctx(m.s.phase)) {
                a->started        = now;
                a->last_event_at  = now;
                a->retry          = retry::Fresh{};   // 新鲜 数据流 —— 调和 -arm 看门狗
                // 重置现场 -rate 积分器所以每个子
                // -turn (员额) -tool 测量自己的发电速度,
                // 而不是污染 当前-rate 显示上一个转弯 字节
                // 火花环( 3) rate_history 继续
                // 生活 StreamState 它横跨着潜水艇
                // -turns / 工具漏洞, 用户可以看到整个
                // 会话中持续生成率的痕迹, 而不是每个工具之后
                // 的新鲜空栏 调用.
                a->live_delta_bytes       = 0;
                a->first_delta_at         = {};
                a->rate_last_sample_at    = {};
                a->rate_last_sample_bytes = 0;
            }
            // 新鲜 数据流 活的 —— 擦除任何剩余土司( 重试
            // 倒计时 "error: …", "cancelled"
            // )从之前尝试,所以状态行不显示 过期 给您
            // 留言 上面有一个健康的连接。
            m.s.status.clear();
            m.s.status_until = {};
            // 转弯 TRUE 作者在助理消息上,现在 传输
            // 现场直播 launch_stream 刚才解决了榜样问题;
            // 记录在这里(而不是在现场阅读选择)。
            // 渲染 时间)是指 头文件 即使在用户切换模型的中间,
            // 也要保持诚实 -thread.
            if (!m.s.smart_turn_model.empty()
                && !m.d.current.messages.empty()
                && m.d.current.messages.back().role == Role::Assistant) {
                auto& last = m.d.current.messages.back();
                last.served_model = m.s.smart_turn_model;
                last.served_role  = m.s.smart_turn_role;
                m.ui.view_cache.drop(m.d.current.id, last.id);
            }
            return Cmd::none();
        },
        [&](StreamTextDelta& e) -> Cmd {
            auto now = m.now;
            if (auto* a = active_ctx(m.s.phase)) {
                a->last_event_at = now;
                if (!e.text.empty()) {
                    if (a->first_delta_at.time_since_epoch().count() == 0) {
                        a->first_delta_at = now;
                        // TTFT 样本——请求发射到第一内容 字节 中,
                        // 每个模型。饲料耐久 -aware 与实时
                        // 数据(哪个模型是)的路由 FAST 现在,
                        // 不只是便宜。
                        if (a->started.time_since_epoch().count() != 0) {
                            const auto ttft = std::chrono::duration_cast<
                                std::chrono::milliseconds>(
                                    now - a->started).count();
                            AGT_LOG(Perf, Debug, "turn.ttft",
                                    "model={} ms={}",
                                    m.d.model_id.value,
                                    static_cast<long long>(ttft));
                        }
                        // 第一次真实 字节 此处 数据流
                        // 尝试 —— 连接明显有效。
                        // 重置 重试 预算如此晚 -stream
                        // 失败获得新的阶梯, 而不是继承
                        // 任何尝试之前( 现在) 的尝试
                        // -succeeded 连接
                        // -time 重试 失去这个,一个转弯,
                        // 能够幸存两个瞬间连接失败,
                        // 然后击中一个中点 -stream 90
                        // 秒只能进 重试 标记前的一次
                        // 终端 - 尽管 传输-level
                        // 90岁时的健康状况是完美的 流式传输.
                        a->transient_retries = 0;
                        // 真正的模型输出结束为无 -progress
                        // 梯级——端点明显有效。 (心跳和衰变
                        // 窗口故意这样做) 不 重设此段. )
                        a->no_progress_failures = 0;
                    }
                    a->live_delta_bytes += e.text.size();
                }
            }
            // 紧凑路由: 紧凑期间 数据流 记录没有合成助理占位符
            // 数据流 进入——摘要的寿命 -transcript
            // 输入 `m.s.compaction_buffer`
            // 并且只有表面 以一个 CompactionRecord
            // 时间 finalize_turn。路线 字节 改为
            // messages.back (),每笔相同 -stream
            // 字节 盖上,这样跑掉的总结就不行了 OOM.
            if (m.s.compacting) {
                if (m.s.compaction_buffer.size() < kMaxStreamingBytes) {
                    const auto room = kMaxStreamingBytes - m.s.compaction_buffer.size();
                    if (e.text.size() <= room) m.s.compaction_buffer += e.text;
                    else m.s.compaction_buffer.append(e.text, 0, room);
                }
                return Cmd::none();
            }
            if (!m.d.current.messages.empty()
                && m.d.current.messages.back().role == Role::Assistant) {
                // 附加到平滑处 缓冲区 而不是直接到 streaming_text
                // - 滴答( 滴答) 处理函数 以可控的速度滴出,
                // 这样服务器的突袭就不会出现视觉跳跃.
                // 船长在 COMBINED 可见 + 缓冲大小,
                // 平滑不能推动通过 per -message 预算。
                auto& msg = m.d.current.messages.back();
                // 封印思考阶段:第一个答案字节结束.
                // 累积和 CLOSE ,所以复述后(互不相干
                // 的思想)的推理开启了一个新阶段,并增加了相同
                // 的总数.
                if (msg.reasoning_started_ms != 0) {
                    const std::int64_t now_ms =
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            now.time_since_epoch()).count();
                    msg.reasoning_ms += std::max<std::int64_t>(
                        0, now_ms - msg.reasoning_started_ms);
                    msg.reasoning_started_ms = 0;
                }
                const std::size_t in_flight =
                    msg.streaming_text.size() + msg.pending_stream.size();
                if (in_flight < kMaxStreamingBytes) {
                    const auto room = kMaxStreamingBytes - in_flight;
                    // 头 -of-stream 快路: 虽然信件还没有
                    // 可见的内容, 附加第一个 字节 DIRECTLY
                    // 改为 streaming_text 所以他们画在
                    // 这个三角洲上 渲染 (FPS =0 = 每个 Msg 渲染。
                    // 没有这个,第一个词坐在那里 pending_stream
                    // 直至下一次滴答失火——最多一个完整的滴答时间
                    // 段(同步时33毫秒) 终端 一百米,在别处,
                    // 更多 SSH ——其内容如下:
                    // "the first word sticks"。
                    // 一旦有可见的文本可以平滑反弹,则随后 字节 穿过心脏
                    constexpr std::size_t kHeadReveal = 512;
                    if (msg.streaming_text.empty()
                        && msg.pending_stream.empty()) {
                        const auto head = std::min({e.text.size(), room, kHeadReveal});
                        msg.streaming_text.append(e.text, 0, head);
                        if (head < e.text.size() && head < room) {
                            const auto rest = std::min(e.text.size() - head,
                                                       room - head);
                            msg.pending_stream.append(e.text, head, rest);
                        }
                    } else if (e.text.size() <= room) {
                        msg.pending_stream += e.text;
                    } else {
                        msg.pending_stream.append(e.text, 0, room);
                    }
                }
                // 头后,所有 字节 流经 Tick 缓冲器
                // meta.cpp 所以服务器暴动 逐步显示
                // 而不是跳进去
            }
            return Cmd::none();
        },
        [&](StreamTextBlockClosed) -> Cmd {
            // 电话 传输 关了助手的门 TEXT 内容块。 这是最早的权威
            // "model finished typing prose" 信号和 始终
            // 前面为 tool_use content_block_start ,所以它降落之前,
            // 任何工具卡 被推到 tool_calls. 标志 输入 -flight 消息;
            // 视图( 图像 cached_markdown_for )读作
            // text_block_closed 改为 request_finalize
            // 编号 逐步显示 光标,在真正安静的空隙中 滑到现场,所以硬度是绝对的 -snap
            // 在工具卡上是没有 -op - 电话 逐步显示 爆炸( 爆炸时)
            // 首字符停住，其余内容直到工具调用时才突然出现。
            // 永远不会发生。
            if (auto* a = active_ctx(m.s.phase))
                a->last_event_at = m.now;
            // 没有占位符 标志 紧凑时( 摘要关闭) -transcript );
            // 无害无 -op 那边
            if (!m.s.compacting
                && !m.d.current.messages.empty()
                && m.d.current.messages.back().role == Role::Assistant) {
                m.d.current.messages.back().text_block_closed = true;
            }
            return Cmd::none();
        },
        [&](StreamToolUseStart& e) -> Cmd {
            auto now = m.now;
            if (auto* a = active_ctx(m.s.phase)) {
                a->last_event_at = now;
                // 结构化工具 调用 是真实的模型输出——结束于无 -progress
                // 重试 梯形( 见 no_progress_failures).
                a->no_progress_failures = 0;
            }
            if (!m.d.current.messages.empty()
                && m.d.current.messages.back().role == Role::Assistant) {
                auto& amsg = m.d.current.messages.back();
                // 一个工具 调用 结束思维阶段( 合理 )
                // —— 在工具本身的定时器开始前封印。
                // -and-close 作为答案路径:在
                // 工具打开新阶段后再次解释原因的转弯.
                if (amsg.reasoning_started_ms != 0) {
                    const std::int64_t now_ms =
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            now.time_since_epoch()).count();
                    amsg.reasoning_ms += std::max<std::int64_t>(
                        0, now_ms - amsg.reasoning_started_ms);
                    amsg.reasoning_started_ms = 0;
                }
                ToolUse tc;
                tc.id   = e.id;
                tc.name = e.name;
                tc.args = json::object();
                // 邮票开始,所以卡片显示一个实时计时器在坐标 -streaming
                // 相位也——让用户告诉 "model hasn't started emitting"
                // 发件人 "execution is slow" 看着她
                tc.status = ToolUse::Pending{now};
                m.d.current.messages.back().tool_calls.push_back(std::move(tc));
                // 参数 -stream 看门狗 应用户要求删除 –
                // 没有自动 待处理 − 失败 超时 工具
                // tool_use_start 数据流 进但永远得不到
                // 它 End 事件 将留下 待处理 直到用户通过 Esc.
                // 数据流-level 错误仍然通过 StreamError
                // 处理函数 ,可以清除内 -flight 工具。
            }
            resync_live_tool_viewer(m);
            return Cmd::none();
        },
        [&](StreamToolUseDelta& e) -> Cmd {
            auto now = m.now;
            if (auto* a = active_ctx(m.s.phase)) {
                a->last_event_at = now;
                if (!e.partial_json.empty()) {
                    if (a->first_delta_at.time_since_epoch().count() == 0) {
                        a->first_delta_at = now;
                        // 见StreamTextDelta——同样的理由。
                        a->transient_retries = 0;
                    }
                    a->live_delta_bytes += e.partial_json.size();
                }
            }
            if (auto* tcp = find_streaming_tool(e.id)) {
                auto& tc = *tcp;
                // 捆绑的附着物——超越了上限,我们进一步下降 字节
                // 拯救路径在 StreamToolUseEnd 跑
                // 在任何 雪橇闻得干净。
                if (tc.args_streaming.size() < kMaxStreamingBytes) {
                    const auto room = kMaxStreamingBytes - tc.args_streaming.size();
                    if (e.partial_json.size() <= room) tc.args_streaming += e.partial_json;
                    else tc.args_streaming.append(e.partial_json, 0, room);
                }
                // 启动现场预览。 头文件 立即涂料,
                // 然后再涂 -parses 空间为~120毫秒。
                // StreamToolUseEnd 总是满载 解析 ,
                // 所以最终状态是准确的。
                using clock = std::chrono::steady_clock;
                constexpr auto kPreviewInterval = std::chrono::milliseconds{120};
                auto now2 = clock::now();
                if (tc.last_preview_at.time_since_epoch().count() == 0
                    || now2 - tc.last_preview_at >= kPreviewInterval) {
                    update_stream_preview(tc);
                    tc.last_preview_at = now2;
                    // 现场计划同步:在上文预览期间,
                    // 一个`todo` 调用将部分待办事宜反射为
                    // tc.args;将这段内容推入持久化计划状态,
                    // 这样,弹出面板/全球指标跟踪模型即刻写出的-progress项目,
                    // 而不仅仅是执行输出.
                    if (tc.name == "todo")
                        sync_todo_state_from_args(m, tc.args);
                }
            }
            resync_live_tool_viewer(m);
            return Cmd::none();
        },
        [&](StreamToolUseSnapshot& e) -> Cmd {
            auto now = m.now;
            if (auto* a = active_ctx(m.s.phase)) {
                a->last_event_at = now;
                if (!e.json.empty()) {
                    if (a->first_delta_at.time_since_epoch().count() == 0) {
                        a->first_delta_at = now;
                        a->transient_retries = 0;
                    }
                    a->live_delta_bytes += e.json.size();
                }
            }
            if (auto* tc = find_streaming_tool(e.id)) {
                tc->args_streaming.assign(e.json, 0,
                    std::min(e.json.size(), kMaxStreamingBytes));
                // 快照 替换无效每个附件 -only
                // 预览光标。从新的当前值重建,
                // 包括重写和缩水(不仅仅是前缀增长)。
                tc->stream_sniff_offset = 0;
                tc->stream_sniff_size = 0;
                tc->stream_decoded_value.clear();
                tc->stream_decode_through = 0;
                tc->stream_decode_closed = false;
                tc->stream_parse_through = 0;
                update_stream_preview(*tc);
                tc->last_preview_at = now;
                if (tc->name == "todo") sync_todo_state_from_args(m, tc->args);
            }
            resync_live_tool_viewer(m);
            return Cmd::none();
        },
        [&](StreamToolUseEnd& e) -> Cmd {
            if (auto* a = active_ctx(m.s.phase))
                a->last_event_at = m.now;
            if (auto* tcp = find_streaming_tool(e.id)) {
                auto& tc = *tcp;
                if (!e.extra_content.is_null()) tc.extra_content = std::move(e.extra_content);
                // 空的 args_streaming 用于无参数工具的合法;
                // 参数被种子到 QQ。 StreamToolUseStart.
                if (!tc.args_streaming.empty()) {
                    try {
                        tc.args = json::parse(tc.args_streaming);
                        // 警卫面前的异形加固——看 canonify_tool_args
                        // (拉古纳S 2.1) `cmd` 寻找).
                        (void)detail::canonify_tool_args(tc.name.value, tc.args);
                        tc.mark_args_dirty();
                        std::string{}.swap(tc.args_streaming);
                    } catch (const std::exception& ex) {
                        // 解析 失败——一般为 SSE 中点断线 -content.
                        // 拯救我们所能拥有的星座,使工具仍然有机会运行,
                        // 而不是整个转弯。 当第传输号停止第-string号
                        // (第3号结论)时,salvage_args号拒绝抢救,
                        // 因为第close_partial_json号
                        // 将合成半-written第正文号的句尾句——在
                        // 这种情况下,我们显示一个明显的转折第
                        // -mid-string号失败,
                        // 而不是第"invalid tool arguments"号。
                        const bool mid_string =
                            agentty::tools::util::ended_inside_string(
                                tc.args_streaming);
                        auto salvaged = salvage_args(tc);
                        if (!salvaged.empty()) {
                            tc.args = std::move(salvaged);
                            tc.mark_args_dirty();
                            std::string{}.swap(tc.args_streaming);
                        } else if (mid_string) {
                            // 别急着失败 记下截断和离开工具 待处理
                            // 这么说吧 finalize_turn's
                            // 重试 循环可以弹出占位符,并默默地重置
                            // -launch 在同一 ctx 上—
                            // — 一个新工具 -use 数据流 是
                            // 唯一能拯救中点的东西 -string 断断.
                            // Args_streaming
                            // 保存到 重试 路径会弹出它。
                            tc.stream_mid_string_truncated = true;
                            std::string{}.swap(tc.args_streaming);
                        } else {
                            auto now = m.now;
                            tc.status = ToolUse::Failed{
                                tc.started_at(), now,
                                std::string{"invalid tool arguments: "} + ex.what()
                                    + "\nraw: " + tc.args_streaming};
                            std::string{}.swap(tc.args_streaming);
                        }
                    }
                }
                // 所需 -field 检查被推迟到 finalize_turn
                // 轮到你们了 -level 重试 逻辑拥有单一决定点.
            }
            resync_live_tool_viewer(m);
            // ── 分光值改为-only 分派(中-stream) ─────────────
            // 电话 传输 刚刚 CLOSED 此工具 调用 ⁇   ⁇  τ ⁇
            // ⁇ 流式传输 轮到其他时间( 更多工具) 调用 说流言
            // message_stop。正常流量等待 StreamFinished
            // → finalize_turn → kick_pending_tools ,
            // 所以每个工具的处决序列化 之后 完整 数据流 纯洁的 READ
            // -ONLY 工具( 选项) read/grep/glob/list_dir
            // ) 订单是悲观的:它不能影响模型仍在说的任何话,
            // 所以开始吧 当前 重叠的《公约》 I/O 其余部分 数据流 —
            // —多层 -tool 把这个藏起来 里面有1-3 s 工具时间
            // 数据流 时间,每一转。
            //
            // 安全门(所有门都必须坚守——关闭到正常路径上):
            //   • 注册表-known 工具 scheduling_effects ⊆ {ReadFs,
            //     净额 -free}: ReadFs-only。
            //     随笔 WriteFs/Exec/Net 等待规划者(
            //     命令 +) 权限 + 沙箱 语义学未变。净值不包括:中
            //     值 -stream 网络获取可以观察到转弯即将改变的状态。
            //   • 没有 权限 在当前配置图下需要且无
            //     待处理 权限 弹出面板(从未在-empt之前作出用户决定).
            //   • 分析整理了此文件 事件 (is_pending + args 对象).
            //   • 阶段 流式传输 (不紧凑——紧凑) 数据流
            //     携带不可用工具;而不是ExecutingTool—
            //     —这条路径已经让规划员运行).
            // 开始的工具转换 待处理 奔驰过河 相同 地位机制
            // kick_pending_tools 使用,所以,
            // ToolExecOutput / 取消 撕裂/ 楔形 看门狗 都看到
            // 一个正常的运行工具。 schedule_parallel_batch
            // 种子活动 副作用 从运行工具,所以后来相同 -turn
            // 作者正确地推迟到读完为止。
            if (m.s.is_streaming() && !m.s.compacting
                && !m.d.pending_permission) {
                if (auto* tcp2 = find_streaming_tool(e.id);
                    tcp2 && tcp2->is_pending() && !tcp2->args.is_null()) {
                    auto& tc2 = *tcp2;
                    const auto def = tools::find(tc2.name.value);
                    const bool read_only =
                        def
                        && !def->scheduling_effects.has(tools::Effect::WriteFs)
                        && !def->scheduling_effects.has(tools::Effect::Exec)
                        && !def->scheduling_effects.has(tools::Effect::Net);
                    const bool needs_perm =
                        !m.d.session_grants.contains(tc2.name.value)
                        && tool::DynamicDispatch::needs_permission(
                               tc2.name.value, m.d.profile);
                    // 被打捞的方格可能不见了 模式定义-required 字
                    // 段: 字段 传输 切到字符串外, 所以 try_parse_partial
                    // 快乐地关闭了天体 `path` 从来没有到过。
                    // finalize_turn 拥有这个案子(维修,
                    // 然后) 重试 并且它只看到一个工具 仍然
                    // 待处理 - 所以,在这里发送 跳过整个机制
                    // 完全读 -only 工具 路径控件。
                    // 线程 死亡时的
                    // "[invalid args] path required" ,
                    // 它读到模型 自己的错误,烧掉转弯。
                    //
                    // 相同的上游guard_truncated_tool_args检查,
                    // 一次命名,由两者共同读取,因为两处拼写
                    // 出来的一个门是漂移的门.
                    const bool args_complete =
                        missing_required_field(tc2.name.value, tc2.args).empty();
                    if (read_only && !needs_perm && args_complete) {
                        const auto now2 = m.now;
                        // Auto-allowed,所以决定和发射
                        // 是相同的即时——但是记录下定义,无论如何,
                        // 每分派验证一个相同的方式.
                        tc2.approved_def_hash = def->definition_hash();
                        // 这条路没有权限门,所以卡片诞生和
                        // 处决是巧合的 — — 但无论如何,
                        // 记录两种情况,而不是留下活的时钟倒回去。
                        // 只在有时设置的字段是下一个读者
                        // 必须说明理由的字段。
                        const auto seq = cmd::next_tool_exec_seq();
                        tc2.status = ToolUse::Running{now2, {}, {}, now2, seq};
                        auto cancel = active_ctx(m.s.phase)
                            ? active_ctx(m.s.phase)->cancel
                            : http::CancelTokenPtr{};
                        std::vector<std::string> active_skills;
                        if (tc2.name.value == "skill")
                            active_skills = tools::skills::active_in(
                                ::agentty::visible_text(m.d.current));
                        return cmd::run_tool(tc2.id, tc2.name, tc2.args,
                                              std::move(cancel), seq,
                                              /*approved_def_hash=*/0,
                                              std::move(active_skills),
                                              m.d.current.id.value);
                    }
                }
            }
            return Cmd::none();
        },
        [&](StreamObservedToolResult& e) -> Cmd {
            if (auto* a = active_ctx(m.s.phase))
                a->last_event_at = m.now;
            if (auto* tc = find_streaming_tool(e.id)) {
                const auto now = m.now;
                std::string output = std::move(e.output);
                if (output.empty())
                    output = e.failed ? "External agent tool failed."
                                      : "Executed by external agent.";
                // 来,来,来 EXTERNAL 探员负责这个,所以...
                // agentty 从来没有发出过它,也没有执行窗口。
                // 将卡片的诞生复制成一体, 而不是记录下来。
                // 我们真的不知道遥远的一侧花了多长时间,
                // 假扮成一个测量的猜测比一个诚实的空白更糟糕。
                if (e.failed) tc->settle_failed(now, std::move(output));
                else          tc->settle_done(now, std::move(output));
            }
            resync_live_tool_viewer(m);
            return Cmd::none();
        },
        [&](StreamThinkingDelta& e) -> Cmd {
            // 适配-thinking块三角洲. 两种形状到达:
            // 一个`thinking_delta`携带推理文本(通常在显示下
            // 为空: ocent), 一个`signature_delta`
            // 携带不透明 per-block 签名接近块尾. 将两者累积到
            // -flight 助理消息上, 这样块可以被逐字记录下转盘重播
            // —— Anthropic 400s a tool_use 转盘,
            // 其思维障碍被丢弃.
            if (auto* a = active_ctx(m.s.phase)) {
                // 生命:思维三角洲证明了 传输 静默的推理
                // 经过后还活着 last_event_at
                // 并重置 重试 预算就像心跳路 连接
                // 思考 结构序列 得到一个新的阶梯。
                a->last_event_at = m.now;
                a->transient_retries = 0;
            }
            // 被捕获只用于重播——从未渲染,
            // 在压缩时没有辅助占位符可以附加.
            if (m.s.compacting) return Cmd::none();
            if (!m.d.current.messages.empty()
                && m.d.current.messages.back().role == Role::Assistant) {
                auto& msg = m.d.current.messages.back();
                // 人数 -BLOCK 抓取:一个响应可以携带几个签名的
                // 思维块(interleved diction)——每
                // 个签名覆盖自己的块的准确文本,所以它们绝不能被合并.
                // 一个边界封印了当前块; text/signature
                // 三角洲附件 OPEN (最后)块。 签名三角洲
                // APPEND (圆) -safe 而不是覆盖。
                auto open_block = [&]() -> Message::ThinkingBlock& {
                    if (msg.thinking_blocks.empty())
                        msg.thinking_blocks.emplace_back();
                    return msg.thinking_blocks.back();
                };
                if (e.block_boundary && !msg.thinking_blocks.empty()
                    && (!msg.thinking_blocks.back().text.empty()
                        || !msg.thinking_blocks.back().signature.empty()
                        || !msg.thinking_blocks.back().redacted_data.empty())) {
                    msg.thinking_blocks.emplace_back();
                    // 显示分隔符 blocks/paragraphs -
                    // 没有它连续的总结 运行在中点 -sentence.
                    if (!msg.thinking.empty()) msg.thinking += "\n\n";
                }
                if (!e.redacted_data.empty()) {
                    // 已修改块: 不透明 载荷 ,逐字重播,
                    // 从未提供(无) msg.thinking 附录).
                    open_block().redacted_data = std::move(e.redacted_data);
                }
                if (!e.text.empty()) {
                    open_block().text += e.text;
                    msg.thinking      += e.text;
                    if (!e.wire_field.empty()) msg.reasoning_wire_field = e.wire_field;
                    // 打开此阶段的第一个思维文本( 稳时钟;
                    // 由答案或工具封存) 的思维阶段 调用。。
                    // 内容 -opens 封印之后,
                    // 使互换的思维变得像思维一样明显。
                    if (msg.reasoning_started_ms == 0)
                        msg.reasoning_started_ms =
                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                m.now.time_since_epoch())
                                .count();
                }
                if (!e.signature.empty()) {
                    auto& blk = open_block();
                    blk.signature += e.signature;
                    // 遗留镜( E) 持久化 返回时 -compat +旧门).
                    msg.thinking_signature = blk.signature;
                }
            }
            return Cmd::none();
        },
        [&](StreamReasoning& e) -> Cmd {
            // Codex/Responses 推理项目完成: 隐藏其不透明
            // encrypted_content 打开 -flight 助理消息,
            // 以便下一个请求在输入中重放 [] (保留链条) -of-thought
            // 横跨工具回合的存储:假。 传输-only ,从未显示。
            if (auto* a = active_ctx(m.s.phase))
                a->last_event_at = m.now;
            if (m.s.compacting) return Cmd::none();
            if (e.encrypted.empty()) return Cmd::none();
            if (!m.d.current.messages.empty()
                && m.d.current.messages.back().role == Role::Assistant) {
                auto& msg = m.d.current.messages.back();
                // 转动可以发出多个推理项目;保持所有项目排列有序,
                // 新线-joined,所以每个blob都会重播.
                //
                // 记录 对应的 站点刻制。一个信息中的块总是来自一个转弯,
                // 所以一个标签覆盖了这个集; build_input
                // 仅在为同一站点建构时重放。 没有此选项,
                // 将服务商切换到中点。 -thread 发送
                // 密码文本的新后端无法解密,整个请求400s(
                // openclaw#72602).
                msg.reasoning_site = e.site;
                // OpenRouter 发送一个 JSON 数组;
                // 组合两个将损坏它。 最新的是完整的一个。
                if (msg.reasoning_encrypted.empty() || e.site == "openrouter")
                    msg.reasoning_encrypted = std::move(e.encrypted);
                else {
                    msg.reasoning_encrypted.push_back('\n');
                    msg.reasoning_encrypted += e.encrypted;
                }
            }
            return Cmd::none();
        },
        [&](StreamUsage& e) -> Cmd {
            if (auto* a = active_ctx(m.s.phase))
                a->last_event_at = m.now;
            // 压缩时会写作。 数据流's `input_tokens`
            // 反映 SUMMARISATION 请求( 完整的前缀 +
            // 动词总和) 提示词 及其影响 `output_tokens`
            // 反映模型的概要长度——既不属于用户 -facing
            // 上下文测量或 "tokens generated" 计数器。
            // 没有这个值值,计数器会短暂跳到 传输-payload 中
            // 号大小 -compaction 重排为 0 英寸
            // finalize_turn ,在状态栏上制作可见的闪光器。
            // PRIOR 转动的值仍在打开 m.s.tokens_in/out
            // 紧身衣开始之前就已经装好了 放在原地就更诚实了
            // 模型目前实际携带了哪些内容？
            // 读到下一个真正的回合
            // 结束。
            if (m.s.compacting) return Cmd::none();
            // `input_tokens` 发件人 Anthropic
            // 是那个 完整 此项请求的前缀, 不 三角洲,三转三转 -counted
            // 由第五轮替换,不要添加。 缓存 字段不包括在 `input_tokens`
            // 数字 API 但仍占用上下文窗口,
            // 所以真实 "tokens in context" 是总和。
            if (e.input_tokens || e.cache_read_input_tokens
                || e.cache_creation_input_tokens) {
                m.s.tokens_in = e.input_tokens
                                   + e.cache_read_input_tokens
                                   + e.cache_creation_input_tokens;
                // 提示词-cache 遥测。 LOW 内转弯
                // 时的命中比表示缓存前缀无效——1号隐藏
                // TTFT 费用(冷却) -price 10-
                // 20x 慢于 缓存 读。跟踪每个转弯如此前缀
                // -stability 错误是可捕捉的 :
                // AGENTTY_LOG ⁇   ⁇  § ⁇   ⁇
                m.s.cache_read_tokens     = e.cache_read_input_tokens;
                m.s.cache_creation_tokens = e.cache_creation_input_tokens;
                m.s.cache_hit_ratio =
                    static_cast<double>(e.cache_read_input_tokens)
                    / static_cast<double>(m.s.tokens_in);
                AGT_LOG(Perf, Debug, "turn.cache",
                        "in={} read={} create={} hit={:.3f}",
                        e.input_tokens, e.cache_read_input_tokens,
                        e.cache_creation_input_tokens,
                        m.s.cache_hit_ratio);
                // 重新调整当地 字节-estimator 与地面
                // 事实相反 传输 前缀 刚定价的模型是相同的
                // 记录 estimate_wire_tokens
                // () 将得分,因此(真/估计)是更正因数。
                // EMA-smooth 将它(重度新样本0.35),
                // 所以一个奇转不能摇动, 并夹在[0.5,1.5],
                // 所以腐败的读数永远不能盲目主动的收缩触发器。
                const int est_now = cmd::estimate_wire_tokens(m.d.current);
                if (est_now > 1000 && m.s.tokens_in > 1000) {
                    const double sample =
                        static_cast<double>(m.s.tokens_in) / est_now;
                    double next = 0.65 * m.s.est_calibration + 0.35 * sample;
                    if (next < 0.5) next = 0.5;
                    if (next > 1.5) next = 1.5;
                    m.s.est_calibration = next;
                }
            }
            if (e.output_tokens) m.s.tokens_out = e.output_tokens;
            // 理由说明 -token 分解( 已内置) output_tokens ;
            // 只保留为状态 -bar 显示。随时更新 传输 报告,
            // 所以最后一个真实转折值仍然可见。
            if (e.reasoning_output_tokens)
                m.s.reasoning_tokens = e.reasoning_output_tokens;
            // 将用法映射到 TURN 届会场是
            // 一个实况测量器,每次转弯时都会更换,
            // 下一次开始时就会消失。
            // -turn 历史和习惯 帧 这些
            // 数字是存在的唯一地方。
            //
            // 写在这里,而不是在 finalize_turn
            // 因为转弯可以发出几种用法 帧 (重试 ),
            // 取最新数据就是会话测量仪已经做的,
            // 所以两者不能对同一转弯产生分歧。
            if (!m.d.current.messages.empty()) {
                auto& last = m.d.current.messages.back();
                if (last.role == Role::Assistant) {
                    // `= Telemetry{}` 而不是说 `.emplace()`
                    // : 青铜器 is_constructible_v
                    // <Message::Telemetry> 以至这个被巢穴所覆盖的集合,
                    // is_default_constructible_v 此处的文字是真实的,
                    // 而安置在前者上是受限制的。请参见上文其他遥测地点的更完整的注释。
                    if (!last.telemetry) last.telemetry = Message::Telemetry{};
                    auto& t = *last.telemetry;
                    // 报告第提示词号的帧号报告全部:输入和第缓存号
                    // 两个部分合并,因此,一个没有第缓存号命中的
                    // 后第帧号无法留下一个较早的第缓存号的计数.
                    if (e.input_tokens || e.cache_read_input_tokens
                        || e.cache_creation_input_tokens) {
                        t.input_tokens = static_cast<std::uint32_t>(e.input_tokens);
                        t.cache_read =
                            static_cast<std::uint32_t>(e.cache_read_input_tokens);
                        t.cache_creation =
                            static_cast<std::uint32_t>(e.cache_creation_input_tokens);
                    }
                    if (e.output_tokens)
                        t.output_tokens = static_cast<std::uint32_t>(e.output_tokens);
                    if (e.reasoning_output_tokens)
                        t.reasoning_tokens =
                            static_cast<std::uint32_t>(e.reasoning_output_tokens);
                }
            }
            return Cmd::none();
        },
        [&](StreamHeartbeat e) -> Cmd {
            // 没有 载荷 或 或 时 间 UI 副作用。
            // 两个心跳课都刷新了时钟。只有真实的 SSE/model
            // 心跳重排( T) 重试; HTTP 控制交通证明路径是开放的,
            // 而不是 上游 语义上的进展。
            if (auto* a = active_ctx(m.s.phase)) {
                a->last_event_at = m.now;
                if (e.transport_only) {
                    a->transport_activity = true;
                } else {
                    a->transient_retries = 0;
                }
            }
            return Cmd::none();
        },
        [&](StreamBufferedWait) -> Cmd {
            if (auto* a = active_ctx(m.s.phase)) {
                a->last_event_at = m.now;
                a->transport_activity = true;
            }
            m.s.status = "network gateway is buffering output \xE2\x80\x94 waiting\xE2\x80\xA6";
            m.s.status_until = m.now
                             + std::chrono::seconds{20};
            return Cmd::none();
        },
        [&](StreamFinished e) -> Cmd {
            // 刚刚运行的型号: 紧凑转弯的紧凑型号( 3)
            // finalize_turn 清除 标志 ,所以先读一下).
            std::string ran_model = m.d.model_id.value;
            if (m.s.compacting) {
                std::string cm = smart::utility_model(
                    ran_model, m.d.available_models, m.d.smart,
                    active_provider_id(m));
                if (!cm.empty()) ran_model = std::move(cm);
            }
            auto cmd = finalize_turn(m, e.stop_reason);
            // 本地路由器 加载 测试刚刚运行的模型,
            // 而它仍然是本地的。
            // 便宜: 不 -op Cmd 本地
            // 的几个主机 就会得到其他的。
            cmd = Cmd::batch(std::move(cmd),
                                        cmd::probe_model_window(m, std::move(ran_model)));
            // 电话 传输层 可能已经学会了一个领域,
            // 归并 记录中显示它能活下来 而不是
            // 每次发射都要花费400美元
            {
                bool grew = false;
                for (const auto& [host, bits] : rejected_fields_snapshot()) {
                    auto& have = m.d.persisted.rejected_fields[host];
                    if ((have | bits) != have) { have |= bits; grew = true; }
                }
                if (grew) cmd = Cmd::batch(std::move(cmd), save_record(m));
            }
            // 没有 force_redraw 武装。上一个版本武装
            // needs_force_redraw 在此启用下一个用户
            // 输入 maya 例 -( B) 软 重绘 - 冲洗任何
            // prev_cells/wire 左侧的脱氧反应 流式传输。
            // 该解析器唯一的真正来源是缩写路径的\r\n\x1b[
            // 2K 循环滚动在 可视区域 下方,现在为单正x1b[J]。
            // third_party/maya/src/render/serialize.cpp;
            // prev_cells 与 传输 独自一人
            //
            // 调 -arming 每个第一个按键压上的大小写( B) 都
            // 非常有害: 当缩写员离开光标时中点 -viewport (
            // cursor at 1853) ( 3, 4, 6,
            // 6) ( 6, 8, 6 ) ( 7, 6 content_rows
            // - 1 < term_h - 1), case(B)将光
            // 标拉回 可视区域 底部, 移动画布 -to-buffer
            // 由同一三角洲绘制的地图。 -emitted 排队降落一号
            // 缓冲区 原版下行数 流式传输 原排就被困在了这里 回滚缓冲区
            // 以复制行的正中 可视区域 顶部。随着臂部的消失,
            // 按键压取出正常的diff路径,作曲者留在心理医生留下的地方(没有)
            // "pull down," ,没有重复。
            return cmd;
        },
        [&](StreamError& e) -> Cmd {
            // 该 接口边界 失败会变成用户所看到的
            // 与驱动下一步工作的分类进行登录( Q) 重试 表面对空投,
            // 所以 "why did it give up?" 或 或
            // 时 间 "why did it retry forever?"
            // 报告可从日志而不是从记录截图中回答。
            AGT_LOG(Ui, Warn, "stream.error",
                    "class={} http={} from_stall={} compacting={} msg={}",
                    static_cast<int>(
                        provider::classify_stream_error(e.message, e.http_status)),
                    e.http_status, e.from_stall ? 1 : 0,
                    m.s.compacting ? 1 : 0, e.message);

            // 丁达普:当摊位 看门狗 被炒了,它绊倒了 取消 符号,
            // 导致 工作线程 线程 解开风,释放自己的 StreamError
            // ("cancelled" 不久后,我们又开始讨论这个
            // 第一个错误已经排入 重试 ;
            // 忽略任何后来到达的 重试 跑,
            // 否则我们 竞态 双 工作线程 线程 将参加同一
            // 届会。
            if (m.s.in_scheduled()) return Cmd::none();

            // 压缩失败 : -flight (速率限制,
            // 网络下降等). 压缩是 传输-only - 从来没有
            // 被推入 `messages` 所以我们不必倒带任何
            // 记录 重试 时间: 退避 (临时/费率) -limit
            // 或表面显示一个可以操作的明显土司,然后返回Idle。
            // 无论哪种方式,用户都使用 -visible 记录
            // 稿与预告稿相同 -compaction 因为
            // 它从未变异。
            //
            // 工作流程保护: 重试 透明地按瞬间和速率计算
            // -limit 失败, 片段网络不会迫使用户守护/
            // compact。 汇总请求是服务器的无国籍请求。
            // POV ——每笔 重试 重建同样的 传输
            // 载荷 从 不可变 所以没有什么可以腐败的
            // 终端 失败( 自动、 内容过滤器、
            // 重试 预算已经用完了) 我们掉到Idle,
            // 清除 缓冲区 ,
            // 并依赖 launch_stream 软 -trim
            // 以保持后续 NORMAL 转换工作,即使压缩不能完成。
            // 用户可以重新 -invoke 条件改善后再做
            if (m.s.compacting) {
                if (auto* a = active_ctx(m.s.phase)) a->cancel.reset();

                auto klass = provider::classify_stream_error(e.message, e.http_status);
                if (e.from_stall
                    && klass == provider::ErrorClass::Cancelled) {
                    klass = provider::ErrorClass::Transient;
                }

                const phase::Active* cctx = active_ctx(m.s.phase);
                int prior = cctx ? cctx->transient_retries : 0;
                // 与正常路径相同的预算-decay:
                // 在上次失败开始很久后失败的压缩.
                {
                    auto cnow = m.now;
                    if (cctx
                        && cctx->last_failure_at.time_since_epoch().count() != 0
                        && cnow - cctx->last_failure_at >= provider::kRetryDecayWindow) {
                        prior = 0;
                    }
                }
                const bool can_retry_compact =
                    cctx
                    && (klass == provider::ErrorClass::Transient
                        || klass == provider::ErrorClass::RateLimit)
                    && !e.non_replayable
                    && prior < provider::kMaxRetries
                    // 相同无 -progress 锁定为正常
                    // 路径: 衰变窗口重排 `prior`
                    // 期间 退避 等待,所以没有这个 紧凑
                    // 一个断端点 重试 永远(死环的紧凑面).
                    && cctx->no_progress_failures
                           < provider::kMaxNoProgressFailures
                    // 配额(重置时数)不会在重试上清空.
                    && !(e.retry_after
                         && e.retry_after->count() > provider::kRetryAfterTerminalSeconds);

                if (can_retry_compact) {
                    std::chrono::milliseconds delay;
                    if (e.retry_after.has_value()) {
                        auto s = e.retry_after->count();
                        if (s < 1) s = 1;
                        if (s > 600) s = 600;   // 荣誉服务器退避; 绑定的垃圾
                        delay = std::chrono::seconds(s);
                    } else {
                        delay = provider::backoff_with_jitter(klass, prior);
                    }
                    auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                        delay + std::chrono::milliseconds{999}).count();
                    // 保留用户 -facing 措辞平静:
                    // "context compacting"
                    // 已经挂在旗子上了 我们只是随笔 重试 提示
                    // 让用户知道操作还活着,而不是默默地进行楔形.
                    m.s.status = "compacting — retrying in " + std::to_string(secs) + "s";
                    m.s.status_until = m.now
                                     + delay + std::chrono::milliseconds{1500};
                    // 只重置流线 字节 - 继续 `compacting` 财务报告和已审计
                    // 财务报表 `compaction_target_index` 这么
                    // 说吧 launch_stream 重建相同的压缩请求形状
                    // RetryStream.
                    m.s.compaction_buffer.clear();
                    if (!reschedule_streaming(m.s.phase, [&](phase::Active& c) {
                            c.transient_retries = prior + 1;
                            c.last_failure_at   = m.now;
                            ++c.no_progress_failures;   // 上面的锁链
                            c.retry             = retry::Scheduled{};
                        }))
                        return Cmd::none();
                    return Cmd::after(delay, Msg{RetryStream{}});
                }

                // ── 适应性收缩 -retry 打开 "prompt is too long" ──────────
                // 叉子或/ 手抄本比窗子还大
                // "prompt is too long: X tokens > Y maximum"（提示过长：请求 token 数超过最大值。）
                // 与其放弃, 解析 编号 LIMIT 将压实的天花板
                // 置于它下面, 重试 —— 修剪后只保留最近合用的板块。
                // 重复一次,每次减半,最后一次缩小到很小的
                // -few-turns 重写,所以它成功在 任意
                // 窗口( 甚至是 ~ 10k )。 被几次尝试所包围,
                // 从而真正无法缩合 载荷 仍然结束。
                // llama.cpp 不同的话:
                // "request (9911 tokens) exceeds the available context size (8192 tokens)"。（请求为 9911 token，超过可用的 8192 token 上下文。）
                // 同样的情况(路由器上常见,其参数为 -declared
                // 大小可以超过服务器真正分配的大小),相同的修补.
                const bool llama_overflow =
                    e.message.find("exceeds the available context size")
                        != std::string::npos;
                if (m.s.compacting && e.http_status == 400
                    && (e.message.find("too long") != std::string::npos
                        || llama_overflow)) {
                    // 解析 "... > N maximum" (如果只有X存在,
                    // 则返回X).
                    long limit = 0;
                    if (auto pos = e.message.find(" maximum");
                        pos != std::string::npos) {
                        std::size_t b = pos;
                        while (b > 0 && std::isdigit(
                                   static_cast<unsigned char>(e.message[b-1]))) --b;
                        try { limit = std::stol(e.message.substr(b, pos - b)); }
                        catch (...) { limit = 0; }
                    }
                    if (constexpr std::string_view kSize = "context size (";
                        limit == 0 && llama_overflow) {
                        if (auto pos = e.message.find(kSize); pos != std::string::npos) {
                            try { limit = std::stol(e.message.substr(pos + kSize.size())); }
                            catch (...) { limit = 0; }
                        }
                    }
                    // 下一个上限: 60% 的上限,
                    // 或者如果我们已经有上限, 将上限减半。
                    // 地板让我们不会永远在无法缩缩合的头上旋转。
                    int prev = m.s.compaction_ceiling;
                    int next = prev > 0 ? prev / 2
                             : (limit > 0 ? static_cast<int>(limit * 6 / 10)
                                          : 30000);
                    if (next < 4000) next = 0;   // 放弃下面的路径( T)
                    const phase::Active* sctx = active_ctx(m.s.phase);
                    const int shrink_tries = sctx ? sctx->transient_retries : 0;
                    // 门 重试 显示实际存在的 ctx: sctx 无效 -checked 以上,
                    // 因为阶段可能 不 此处活动( E) e.g 后发生一个延迟错误。
                    // 取消 被丢到Idle)——取. value() 在那条
                    // 路上 bad_optional_access →
                    // std::terminate 内侧 状态更新函数.
                    if (next > 0 && shrink_tries < 6 && sctx) {
                        m.s.compaction_ceiling = next;
                        m.s.compaction_buffer.clear();
                        if (!reschedule_streaming(m.s.phase,
                                [&](phase::Active& c) {
                                    c.transient_retries = shrink_tries + 1;
                                    c.retry = retry::Scheduled{};
                                }))
                            return Cmd::none();
                        m.s.status = "forking \xc2\xb7 trimming to fit…";
                        m.s.status_until = {};
                        return Cmd::after(std::chrono::milliseconds{100},
                                                Msg{RetryStream{}});
                    }
                }

                // 终端 压缩失败。 放下所有与压缩有关的和表面
                // 的 一个 烤面包,轻点 -trim 正常
                // 状态 -turn 路径使代理工作减少 传输 大小;
                // 用户可以/稍后再压缩.
                m.s.compacting              = false;
                m.s.compaction_target_index = 0;
                m.s.compaction_ceiling      = 0;
                m.s.compaction_buffer.clear();
                m.s.phase = phase::Idle{};
                m.s.status = (klass == provider::ErrorClass::Cancelled)
                    ? "compaction cancelled"
                    : ("compaction failed: " + e.message
                       + " — retry with /compact");
                auto now = m.now;
                auto ttl = std::chrono::seconds{
                    klass == provider::ErrorClass::Cancelled ? 3 : 8};
                m.s.status_until = now + ttl;
                auto stamp = m.s.status_until;
                auto status_cmd = Cmd::after(
                    std::chrono::duration_cast<std::chrono::milliseconds>(ttl)
                        + std::chrono::milliseconds{50},
                    Msg{ClearStatus{stamp}});
                // 排水排队消息, 即使压缩失败。
                // launch_stream 软
                // -trim 让他们适应 传输。
                // 最好给用户答复,而不是把打字卡在死板后面。
                if (!m.ui.composer.queued.empty()) {
                    auto& head = m.ui.composer.queued.front();
                    m.ui.composer.text        = std::move(head.text);
                    m.ui.composer.attachments = std::move(head.attachments);
                    m.ui.composer.cursor      = static_cast<int>(m.ui.composer.text.size());
                    m.ui.composer.queued.erase(m.ui.composer.queued.begin());
                    auto sub_cmd = submit_message(m);
                    return Cmd::batch(
                        std::move(status_cmd), std::move(sub_cmd));
                }
                return status_cmd;
            }

            // 工作线程 线程正在解开; 将符号放下, 下一转( 或
            // 预定的重试) 薄荷将新鲜的薄荷。 到达 ctx(
            // 而不是将整个 ctx ) , 因为我们可能需要
            // 重试 计数器进行 can_retry 检查。
            if (auto* a = active_ctx(m.s.phase)) a->cancel.reset();

            // 排水缓冲器 缓冲区 + streaming_text 承诺的
            // 正文 如此的,这样 重试 或 或 时 间 终端 路径不会丢失
            // 已收到 字节 预览 -settle StreamingMarkdown
            // 信息在下个消息之前被锁定 view() — 惰性
            // finish() 视图中,助手高度可以以 1+ 移动
            // 结束 ``` 把尾部提交到前缀时产生的行。
            // 块,会干扰 回滚缓冲区↔可视区域 接口边界.
            Message* last = nullptr;
            if (!m.d.current.messages.empty()
                && m.d.current.messages.back().role == Role::Assistant) {
                last = &m.d.current.messages.back();
                if (!last->pending_stream.empty()) {
                    last->streaming_text += last->pending_stream;
                    last->pending_stream.clear();
                }
                if (!last->streaming_text.empty()) {
                    if (last->text.empty()) last->text = std::move(last->streaming_text);
                    else                    last->text += std::move(last->streaming_text);
                    std::string{}.swap(last->streaming_text);
                }
                if (!last->text.empty()) {
                    auto& cache = m.ui.view_cache.message_md(
                        m.d.current.id, last->id);
                    if (!cache.streaming)
                        cache.streaming = std::make_shared<maya::StreamingMarkdown>();
                    cache.streaming->set_content(last->text);
                    cache.streaming->finish();
                }
            }

            // 将重试诉终端案分类并作出裁定。
            auto klass = provider::classify_stream_error(e.message, e.http_status);
            // A "cancelled" 错误只在 WE 已
            // 取消 编号 数据流 穿过马棚 看门狗 (非用户).
            // 两种形状具有这种意图:
            //   • 合成摊位 StreamError 本身( e.from_stall ),或
            //   • 国家 工作线程 线程 从“从”中解开 取消 编号 看门狗
            //     绊倒 此 转弯——被仍然坐在里面的ctx认出来
            //     StallFired (合成错误没有安排 重试 还
            //     没搬去 Scheduled/Fresh。
            //     使用 标志 合成错误意味着后转回 -entering
            //     StallFired 不能追溯到不相关的用户重分类
            //     -cancel 由不同的转弯。
            if ((e.from_stall || m.s.in_stall_fired())
                && klass == provider::ErrorClass::Cancelled) {
                klass = provider::ErrorClass::Transient;
            }

            // "Committed work" 标注 重试 仅 Done/Running
            // 工具 调用 + 最终文本 正文 块a 重试. A 待处理 工具
            // ( 选项) StreamToolUseStart 发射,
            // 舰长可能已经中途 -streaming 当摊子被撞到的时候) 不
            // 已承诺的 -running 给模特一个机会重新 -emit 同样的定义,
            // 切换 -retry 路径使用(见上文)。
            bool has_committed = false;
            if (last) {
                has_committed = !last->text.empty() ||
                    std::ranges::any_of(last->tool_calls, [](const auto& tc) {
                        return tc.is_done() || tc.is_running();
                    });
            }
            const phase::Active* err_ctx = active_ctx(m.s.phase);
            int prior_transient = err_ctx ? err_ctx->transient_retries : 0;
            // 预算衰减: 如果先前的失败时间比
            // kRetryDecay Window 早,
            // 则 传输 中间是健康的(没有失败,
            // 没有摊位 整个窗口), 所以这个失败开始一个
            // 新的阶梯 而不是继承一个无关的棕色 -out
            // 长期保存多功能 -hour 段 次从零星的零星的积
            // 聚变为永久的 终端 锁锁.
            {
                auto now = m.now;
                if (err_ctx
                    && err_ctx->last_failure_at.time_since_epoch().count() != 0
                    && now - err_ctx->last_failure_at >= provider::kRetryDecayWindow) {
                    prior_transient = 0;
                }
            }

            // ── 重播推理被拒绝: 放弃它, 重试 under ────────────
            // A Response 后端 400s 与
            // invalid_encrypted_content 重放
            // 加密推理不再可读( 旋转键、 账户开关、
            // 过期的 blob )。 推理是优化; 转弯不是。
            // 清除 线程 中存储的每个 blob, 并重发一次。
            if (err_ctx && !has_committed && prior_transient < provider::kMaxRetries
                && e.message.find("invalid_encrypted_content") != std::string::npos) {
                std::size_t dropped = 0;
                for (auto& msg : m.d.current.messages)
                    if (!msg.reasoning_encrypted.empty()) {
                        msg.reasoning_encrypted.clear();
                        msg.reasoning_site.clear();
                        ++dropped;
                    }
                if (dropped > 0) {
                    AGT_LOG(Model, Warn, "reasoning.dropped",
                            "invalid_encrypted_content: cleared {} message(s)", dropped);
                    if (!reschedule_streaming(m.s.phase, [&](phase::Active& c) {
                            c.transient_retries = prior_transient + 1;
                            c.last_failure_at   = m.now;
                            c.retry             = retry::Scheduled{};
                        }))
                        return Cmd::none();
                    auto toast = set_status_toast(
                        m, "saved reasoning no longer readable \xe2\x80\x94 resent without it",
                        std::chrono::seconds{6});
                    return Cmd::batch(save_current_whole(m), std::move(toast),
                                      Cmd::after(std::chrono::milliseconds{50},
                                                 Msg{RetryStream{}}));
                }
            }

            // ── 请求不符合: 学习真实窗口, 重新发送修剪 ────
            // 模型的窗口比我们所相信的要小(一个服务于短
            // -contextXX变体,一个过期目录).
            // 当错误表示实际限度时,把它当作窗口;
            // 在下次发送时的软-trim就符合请求。没有这个,
            // 转弯刚刚用400转死亡.
            if (err_ctx && !m.s.compacting && !has_committed
                && prior_transient < provider::kMaxRetries
                && provider::is_context_overflow(e.message)) {
                const long limit = provider::context_limit_from_overflow(e.message);
                if (limit >= 4096 && limit < m.s.context_max) {
                    AGT_LOG(Model, Warn, "context.learned",
                            "overflow: window {} -> {}", m.s.context_max, limit);
                    m.s.context_max = static_cast<int>(limit);
                    if (!reschedule_streaming(m.s.phase, [&](phase::Active& c) {
                            c.transient_retries = prior_transient + 1;
                            c.last_failure_at   = m.now;
                            c.retry             = retry::Scheduled{};
                        }))
                        return Cmd::none();
                    auto toast = set_status_toast(
                        m, "model window is " + std::to_string(limit / 1000)
                               + "k \xe2\x80\x94 trimmed older turns and resent",
                        std::chrono::seconds{6});
                    return Cmd::batch(std::move(toast),
                                      Cmd::after(std::chrono::milliseconds{50},
                                                 Msg{RetryStream{}}));
                }
            }

            // ── 可选 正文 拒绝: 学习它, 重试 不含 ────────
            // 严格一点 OpenAI-compat 宿主400s a
            // 字段它不知道(# 93: Groq /) Gemini
            // 打开 prompt_cache_key。字段是优化;转弯不是。
            // 记录 "this host rejects X" (永久:
            // 不复转或会发)和 -run 编号 相同 请求—— 正文 构建
            // 者现在省略 X. 以瞬间预算为约束, 主机只能教授每个字段
            // 一次( 它从下一个字段消失) 正文 ),所以这不能循环.
            if (err_ctx && !has_committed
                && m.d.selection.kind != provider::Kind::Anthropic
                && prior_transient < provider::kMaxRetries) {
                const auto field = provider::parse_rejected_field(e.message, e.http_status);
                const auto& ep = m.d.selection.openai_endpoint;
                const std::string host = host_key(ep.host, ep.port);
                if (field != provider::OptionalField::None && !ep.host.empty()
                    && (rejected_fields_for(host) & provider::bit(field)) == 0) {
                    add_rejected_field(host, provider::bit(field));
                    m.d.persisted.rejected_fields[host] |= provider::bit(field);
                    Cmd learned_save = save_record(m);
                    AGT_LOG(Model, Warn, "caps.field_rejected",
                            "host={} field={} http={} msg={}", host,
                            provider::wire_name(field), e.http_status,
                            std::string_view{e.message}.substr(0, 256));
                    if (!reschedule_streaming(m.s.phase, [&](phase::Active& c) {
                            c.transient_retries = prior_transient + 1;
                            c.last_failure_at   = m.now;
                            c.retry             = retry::Scheduled{};
                        }))
                        return Cmd::none();
                    auto toast = set_status_toast(
                        m, std::string{ep.host} + " doesn't take "
                               + std::string{provider::wire_name(field)}
                               + " \xe2\x80\x94 resent without it",
                        std::chrono::seconds{6});
                    return Cmd::batch(std::move(learned_save), std::move(toast),
                                      Cmd::after(std::chrono::milliseconds{50},
                                                 Msg{RetryStream{}}));
                }
            }

            // ── 缺少必要的推理:学习它, 重试 随它(# 117) ──
            // 一种想法 -mode 型号400s 早期的助手缺乏的历史
            // reasoning_content : 转换另一个生产出来的模型,
            // 或它没有理由的模型。 如果没有这个, 会话将死,
            // 直到用户切换模型。 记录什么。 此 模式打开 此 主机
            // 需求(长期存在),以及 -run 编号 相同 转弯;
            // 正文 构建器现在填补了这些转折。 学习只能扩大范围(
            // 无 ) 工具 -call 转动 – 每个转动),
            // 所以每个模型最多学习两次,不能循环.
            if (err_ctx && !has_committed
                && m.d.selection.kind != provider::Kind::Anthropic
                && prior_transient < provider::kMaxRetries) {
                const auto need = provider::parse_reasoning_echo_required(
                    e.message, e.http_status);
                const auto& ep = m.d.selection.openai_endpoint;
                // 服务于这个转弯的模型:Smart Mode可能已经将其
                // 路由到另一个角色的模型,而不是选择面板显示.
                const std::string& served = m.s.smart_turn_model.empty()
                    ? m.d.model_id.value : m.s.smart_turn_model.value;
                const std::string host = host_key(ep.host, ep.port);
                const auto known = reasoning_echo_for(host, served);
                if (need != provider::ReasoningEcho::None && !ep.host.empty()
                    && static_cast<std::uint8_t>(need) > known) {
                    const std::string key = reasoning_echo_key(host, served);
                    set_reasoning_echo(key, static_cast<std::uint8_t>(need));
                    m.d.persisted.reasoning_echo[key] = static_cast<std::uint8_t>(need);
                    Cmd learned_save = save_record(m);
                    AGT_LOG(Model, Warn, "caps.reasoning_echo_required",
                            "key={} scope={} http={} msg={}", key,
                            need == provider::ReasoningEcho::All ? "all" : "tool_calls",
                            e.http_status,
                            std::string_view{e.message}.substr(0, 256));
                    if (!reschedule_streaming(m.s.phase, [&](phase::Active& c) {
                            c.transient_retries = prior_transient + 1;
                            c.last_failure_at   = m.now;
                            c.retry             = retry::Scheduled{};
                        }))
                        return Cmd::none();
                    auto toast = set_status_toast(
                        m, served + " wants reasoning on earlier turns \xe2\x80\x94 "
                               "resent with it",
                        std::chrono::seconds{6});
                    return Cmd::batch(std::move(learned_save), std::move(toast),
                                      Cmd::after(std::chrono::milliseconds{50},
                                                 Msg{RetryStream{}}));
                }
            }

            // (一个自定义主机,400s "off" 字段在其中被愈合 OpenAI
            // 传输层 ,所以,我说 `run`, ACP 和副剂也得到它。 )

            // ── reasoning_effort 拒绝4xx: 学习,夹, 重试 ─────
            // 供应商拒绝了我们发送的功劳价值——通常 NAMES 其已接受的设定
            // 正文 (伊斯兰教: "supported values: [high, none]"
            // )能力 DISCOVERY ,而不是失败:
            // 记录准确的接受集合(永久化,
            // 所以 ladder/clamps ——即使重启之后,
            // 对于无法了解数据库的提供者,也永远正确,
            // 修复 m.d.effort 并重新启动 相同 立即转身
            // launch_stream 内容 -derives 编号 传输
            // 数值通过 resolved_caps ,现在 归并 学习的
            // 集合中——所以 重试 无法在同一轴线上400。 ON 所以
            // 重写漂移永远无法回转我们; 每转弯一次尝试通过
            // transient_retries 和所有其它发现一样 重试.
            if (err_ctx && !has_committed
                && m.d.effort != Effort::None
                && prior_transient < provider::kMaxRetries) {
                if (auto learned = provider::parse_effort_rejection(
                        e.message, e.http_status)) {
                    // Provider-scoped 键( 键) ( "mistral/mistral-small-latest"):
                    // 事实属于 此 主办单位 分派 表格中,
                    // 其他地方的同样赤裸的ID可能接受不同的 枚举.
                    std::string key = scoped_caps_key(m.d.model_id.value);
                    if (key.empty()) key = m.d.model_id.value;
                    set_learned_effort_set(key, *learned);
                    Cmd learned_save = Cmd::none();
                    {   // 坚持不懈——明天的会议开始得正确。
                        m.d.persisted.learned_effort_sets[key] = *learned;
                        learned_save = save_record(m);
                    }
                    const auto caps = resolved_caps(m.d.model_id.value);
                    const Effort before = m.d.effort;
                    m.d.effort = clamp_effort(m.d.effort, caps);
                    // 一个能力 FACT 刚刚从供应商的拒绝
                    // 中得知——最高 -value 异质性 事件。
                    // 警告(默认情况下):持续的事实
                    // 改变未来每个会话的行为,所以共享日志
                    // 必须显示它何时和为何被写入。
                    AGT_LOG(Model, Warn, "caps.effort_learned",
                            "key={} set={:#x} effort {} -> {} http={} msg={}",
                            key, *learned, effort_label(before),
                            effort_label(m.d.effort), e.http_status,
                            std::string_view{e.message}.substr(0, 256));
                    if (!reschedule_streaming(m.s.phase, [&](phase::Active& c) {
                            c.transient_retries = prior_transient + 1;
                            c.last_failure_at   = m.now;
                            c.retry             = retry::Scheduled{};
                        }))
                        return Cmd::none();
                    // 一个安静的, 信息丰富的人 -liner - 转弯还开着
                    const std::string note = m.d.effort == Effort::None
                        ? "this model takes no reasoning effort — sent without it"
                        : "reasoning " + std::string{effort_label(before)}
                            + " → " + std::string{effort_label(m.d.effort)}
                            + " (all this model supports)";
                    auto toast = set_status_toast(m, note,
                                                  std::chrono::seconds{6});
                    return Cmd::batch(
                        std::move(learned_save),
                        std::move(toast),
                        Cmd::after(std::chrono::milliseconds{50},
                                        Msg{RetryStream{}}));
                }
            }

            // ── 长 -context 应享权利400:自 -heal 别死 -end ──
            // 账户的 事件订阅 拒绝上下文-1m β `[1m]`
            // 选择面板 变体 逼我们送来,这是...
            // CAPABILITY DISCOVERY ,
            // 而不是短暂的失败——每一次 重试 标记将相同400,
            // 没有它,同样的请求将在平原200K窗口中成功。
            // 所以:从活动模型中剥离标记,记住权利事实(永久不变
            // —— 选择面板 停止提供 `[1m]` 变体 ,
            // 参见 list_models 来一杯 -line 解释,
            // 并重新启动 相同 立即转弯。没有承诺(400人到达
            // 之前没有内容),所以重启是安全的。在标记上被守护着,
            // 所以是一个服务器。 -side 这条错误的文字
            // 在普通模型上漂移,永远无法循环我们.
            if (err_ctx && !has_committed
                && m.d.model_id.value.find("[1m]") != std::string::npos
                && provider::is_long_context_rejection(e.message, e.http_status)) {
                m.d.model_id = ModelId{wire_model_id(m.d.model_id.value)};
                m.s.context_max = ui::context_max_for_model(m.d.model_id.value);
                // 坚持发现+修正型号id,使下一次发射不会
                // 重新使用这个账户不能使用的-offer.
                Cmd ctx_save = Cmd::none();
                {
                    auto& s = m.d.persisted;
                    // 账户 -scoped : 这个 SUBSCRIPTION。
                    // 由(提供方、账户、模型)键入,
                    // 所以切换账户既不会失去这一事实,也不会将其应用
                    // 于可能完全有资格的账户。
                    (void)entitlement_record_blocked(
                        s, m.d.auth, domain::entitlement::Fact::Context1M,
                        active_provider_id(m), wire_model_id(m.d.model_id.value));
                    s.model_id = m.d.model_id;
                    // 也脱光 `[1m]` 从每笔 -provider
                    // 记得后来的转机 -and-back 给这个提供
                    // 者不恢复 过期 `[1m]` 编号和再 -trigger
                    // 同样的400圈 没有这个 provider_models
                    // [人类]保持1M 变体 并且每次发现
                    // 权利都重复。
                    s.provider_models[active_provider_id(m)] =
                        m.d.model_id.value;
                    ctx_save = save_record(m);
                }
                // 放下来 `[1m]` 现场排队 目录 所以,
                // 我们 选择面板 反映现实,而不等待反弹。
                std::erase_if(m.d.available_models, [](const ModelInfo& mi) {
                    return mi.id.value.find("[1m]") != std::string::npos;
                });
                if (!reschedule_streaming(m.s.phase, [&](phase::Active& c) {
                        c.transient_retries = prior_transient + 1;
                        c.last_failure_at   = m.now;
                        c.retry             = retry::Scheduled{};
                    }))
                    return ctx_save;
                auto toast = set_status_toast(m,
                    "this account lacks the 1M-context beta — fell back to "
                    "the standard 200K window and retried",
                    std::chrono::seconds{8});
                return Cmd::batch(
                    std::move(ctx_save),
                    std::move(toast),
                    Cmd::after(std::chrono::milliseconds{50},
                                    Msg{RetryStream{}}));
            }

            // ── 拒绝视觉:学习 原因 ,则 重试 没有图像 ──
            //
            // 图像上的400元 -bearing 请求有四个原因,
            // 不能互换(见 classify_vision_rejection).
            // 错认区别不是表面的:
            //
            //   • 一项 ORG POLICY 块记录为模型能力
            //     将责备模式,坚持反对模式 每一个账户,
            //     并活过账户切换 修复它
            //   • a MODEL 将能力记录为兽类的事实将剥离
            //     账户上每个模型的图像,
            //     包括完全清晰的图像
            //
            // 所以每种都记录在它所属的地方 然后
            // 轮到它 重试 图像被删除。
            // 重试 关键是:用户提问并附上图片,
            // 关于文本的答案用其中的400个字比死转弯。
            if (const auto vis = provider::classify_vision_rejection(
                        e.message, e.http_status);
                vis != provider::VisionRejection::None
                && err_ctx && !has_committed) {
                // 转弯是否真的携带图像?如果不是,
                // 信息与看起来只是视觉排斥的事物匹配,
                // 而剥离任何东西,那么重试将是一个无限循环,
                // 一个无关的400个循环。
                // 图像会随用户消息和工具结果(读在png上);
                // 两者都必须计算和剥离。
                std::size_t carried = 0;
                for (const auto& msg : m.d.current.messages) {
                    carried += msg.images.size();
                    for (const auto& tc : msg.tool_calls)
                        carried += tc.done_images().size();
                }

                if (carried > 0) {
                    AGT_LOG(Model, Warn, "vision.rejected",
                            "kind={} images={} model={} msg={}",
                            provider::tag(vis), carried,
                            m.d.model_id.value, e.message);

                    auto& s = m.d.persisted;
                    bool remembered = false;
                    Cmd vision_save = Cmd::none();
                    switch (vis) {
                        case provider::VisionRejection::OrgPolicy:
                            // ACCOUNT-wide 空的,请检查date=中的日期值 (帮助) model_id。
                            // 模型可以看见;此登录可能不会。
                            remembered = detail::entitlement_record_blocked(
                                s, m.d.auth, domain::entitlement::Fact::VisionOrgPolicy,
                                active_provider_id(m));
                            break;
                        case provider::VisionRejection::ModelCapability:
                            // 财产 MODEL。录制于 目录
                            // 排好船闸 cmd_factory
                            // 下一轮不显示图像。
                            // -trip.
                            for (auto& mi : m.d.available_models)
                                if (mi.id == m.d.model_id)
                                    mi.supports_vision = false;
                            break;
                        case provider::VisionRejection::MediaType:
                        case provider::VisionRejection::TooManyImages:
                            // 人数 -REQUEST 单人
                            // -model : 不同格式或更少的
                            // 图像会在这个模型上成功。
                            // 记住或者能力会是错误的,所以我们只剥离
                            // 和 重试 轮到这个了
                            break;
                        case provider::VisionRejection::None:
                            break;   // 无法到达,在上方守护
                    }
                    if (remembered) vision_save = save_record(m);

                    // 脱衣舞 重试 记录片保存图像以显示,
                    // 传输 载荷 失去他们,重建从 m.d.current
                    // 自动调试 重试.
                    for (auto& msg : m.d.current.messages) {
                        msg.images.clear();
                        for (auto& tc : msg.tool_calls)
                            if (auto* d = std::get_if<ToolUse::Done>(&tc.status);
                                d && !d->images.empty()) {
                                d->images.clear();
                                d->output += "\n[image removed: the provider "
                                             "could not process it]";
                            }
                    }

                    if (!reschedule_streaming(m.s.phase, [&](phase::Active& c) {
                            c.transient_retries = prior_transient + 1;
                            c.last_failure_at   = m.now;
                            c.retry             = retry::Scheduled{};
                        }))
                        return vision_save;

                    auto toast = set_status_toast(m,
                        vis == provider::VisionRejection::OrgPolicy
                            ? "your organisation has image input disabled — "
                              "retried without the image"
                        : vis == provider::VisionRejection::TooManyImages
                            ? "too many images for this model — retried "
                              "without them"
                        : vis == provider::VisionRejection::MediaType
                            ? "this image format isn't supported — retried "
                              "without it"
                            : "this model can't read images — retried "
                              "without them",
                        std::chrono::seconds{8});
                    return Cmd::batch(
                        std::move(vision_save),
                        std::move(toast),
                        Cmd::after(std::chrono::milliseconds{50},
                                        Msg{RetryStream{}}));
                }
            }

            // ── 认证( A) 401/403 : 试试看 -shot 欧奥特刷新 ─────────
            // 无记名符号过期( 或被旋转) 中点 -session.
            // Deps 仍然拿着 过期 头文件; 加载 从磁盘中获取证书
            // refresh_token ,将同样的背景刷新
            // `init()` 用于启动时,并停放 数据流 ctx 输入
            // retry::Scheduled。该词 TokenRefreshed
            // 处理函数 发送 RetryStream 成功(关于成功)
            // -launches 与新来 -installed 或空投
            // 到 终端 显示已存在的 "run 'agentty login'"
            // 提示如前.
            //
            // 热量在 err_ctx (不能这样) 重试 离爱德尔!
            // has_committed (该模型已经产生了可见
            // 输出——刷新和 -retry ),
            // 并且 oauth_refresh_in_flight 尚未
            // 设置( 避免堆叠刷新 )。 每转弯一次刷新: 计数为
            // transient_retries 因此,
            // 在成功刷新表面后不断返回401的服务器为 终端 而不是永远循环。
            if (klass == provider::ErrorClass::Auth
                && err_ctx
                && !has_committed
                && !m.s.oauth_refresh_in_flight
                && prior_transient < provider::kMaxRetries
                // Anthropic 仅限: credentials.json
                // 持有 ANTHROPIC 欧奥特对,401从a OpenAI
                // -family 提供者(坏的密斯特拉尔密钥,已撤销的 Groq密
                // 钥,...)不得 触发 Anthropic 刷新——刷新成功,
                // update_auth () 安装 Anthropic 无记名为缓存 头文件 ,
                // 并显示 重试 船驶向错误的宿主 无法突破的401环
                && m.d.selection.kind == provider::Kind::Anthropic) {
                // 公园和刷新。 工作线程 读取存储的刷新符;
                // 无( 一种) API 键) 它失败了停靠 数据流 结束
                // TokenRefreshed 带标志 -in 提示,请说
                {
                    // 取消 令牌已经重新设置在这个处理函数的顶端;
                    // 我们只是重建了带有凸起的计数器
                    // 的ctx和排行榜重试的哨兵.
                    if (!reschedule_streaming(m.s.phase, [&](phase::Active& c) {
                            c.transient_retries = prior_transient + 1;
                            c.last_failure_at   = m.now;
                            c.retry             = retry::Scheduled{};
                        }))
                        return Cmd::none();
                    if (last) {
                        // 弹出前放下——未承诺(无文本)占位
                        // 符可能持有 缓存 输入( 绑定或舞台);
                        // 释放它, 使流行不成为孤儿( 见
                        // : CancelStream 理由的流行。
                        // "#r" 推理槽也被放弃了——a
                        // 过期 逐步显示 在那里会重放 旧
                        // 尝试的推理 重试 新鲜的街区
                        m.ui.view_cache.drop(m.d.current.id, last->id);
                        m.ui.view_cache.drop(m.d.current.id,
                                             MessageId{last->id.value + "#r"});
                        m.d.current.messages.pop_back();
                    }
                    Message placeholder;
                    placeholder.role = Role::Assistant;
                    m.d.current.messages.push_back(std::move(placeholder));
                    m.s.oauth_refresh_in_flight = true;
                    m.s.status = "auth expired \xE2\x80\x94 refreshing token\xE2\x80\xA6";
                    m.s.status_until = {};
                    auto refresh_cmd = cmd::refresh_oauth_for_401();
                    // 没有 force_redraw 需要:
                    // StreamingMarkdown 预览 -settle
                    // 已经在上面 锁 消息的高度; 下一个 渲染 选择普通的
                    // diff 路径。 (内置) force_redraw
                    // 安全的地方——只是软的箱子——(B) 重绘 ,
                    // 而不是破坏性的——也没有必要。 )
                    return refresh_cmd;
                }
            }

            // 中间 -stream / 数字 -class 重试 顶部。
            // 失败是 "mid-stream" 如果该 传输 死前的转弯
            // 证明自己还活着 看门狗 已射击了(火烧得离不开),
            // OR 已看到内容三角形( 3)。 first_delta_at 准备,
            // 这些都得到一个 SHORT 重试 预算(见
            // provider::max_retries_for 因为a
            // 传输 联系我们后不断中断 是一个真正的断层,而不是重联的
            // blip——再 -hammering 6x 只会给横幅投影。
            // 一个干净的连接失败( 没有三角洲, 没有摊位) 保留了全部预算,
            // 因为一个新的连接几乎总是成功。
            const bool mid_stream =
                m.s.in_stall_fired()
                || (err_ctx
                    && (err_ctx->transport_activity
                        || err_ctx->first_delta_at.time_since_epoch().count() != 0));
            const int retry_cap = provider::max_retries_for(klass, mid_stream);

            // 中场 -stream 失败, 闸门打开 `mid_stream_failures`
            // 而不是说 `transient_retries`。
            // 后者被任何心跳或第一个内容三角洲重置为0,所以 传输 能够
            // 提供一个 byte/ping 每次下降都会重新确定预算 重试
            // 永远 `mid_stream_failures` 谓 单调递增
            // 的 每转弯( 绝不由 传输-health 因此,
            // 短的中点 -stream 盖子实际带子 终端 接上一个扇动的连接。
            const int mid_prior = err_ctx ? err_ctx->mid_stream_failures : 0;
            const bool budget_left =
                mid_stream ? (mid_prior      < retry_cap)
                           : (prior_transient < retry_cap);

            bool can_retry = (klass == provider::ErrorClass::Transient
                           || klass == provider::ErrorClass::RateLimit)
                          && budget_left
                          && !e.non_replayable
                          && !has_committed
                          && err_ctx;   // 无法从 Idle (无 ctx) 中获取 重试

            // NO-PROGRESS 批次: 变质窗口重排
            // prior_transient 90秒后, 时间轴:
            // "healthy" 传输 ,但一个 退避 也等待该窗口的勾选——
            // 所以一个连接精细且失败的结束点( 本地服务器错误)
            // path/model )可以循环永远。 no_progress_failures
            // 谓 单调递增的 每转弯,重置 仅 以真实的模型输出;
            // 超过硬盖失败为 终端 不论阶级.
            bool no_progress_latched = false;
            if (can_retry && err_ctx
                && err_ctx->no_progress_failures
                       >= provider::kMaxNoProgressFailures) {
                can_retry = false;
                no_progress_latched = true;
            }

            if (can_retry) {
                int attempt = prior_transient;
                std::chrono::milliseconds delay;
                if (e.retry_after.has_value()) {
                    auto s = e.retry_after->count();
                    if (s < 1)   s = 1;
                    // 以小时计量的 Retry-After 代表配额限制，
                    // 不 A BURST LIMIT, 和 RETRYING IT IS 始终 WRONG.
                    //
                    // 429 包含两个不同的事物,一个破解极限 数秒后就清除了,
                    // 一个 重试 计划配额在重新确定之前是不清楚的。 -Auto
                    // 免费的等级答案 `retry-after: 29410`
                    // (8小时至00:00) UTC ),
                    // 类型= insufficient_quota ,
                    // 和 Anthropic/OpenAI 开票盖也要这样
                    //
                    // 将这段话说成是600s,
                    // 使得"you are out until midnight"
                    // 变成了三个毫无意义的10-minute等待,
                    // 每一个等待都保证失败,然后放弃已经传达的FIRST回复。
                    // 用户坐在退避面前一无所获。
                    //
                    // 所以: 尊重服务器提示的上限,
                    // 并且将它以外的任何东西当作终端。 服务器说,
                    // 它何时会再次服务; 如果它比我们愿意等待的要远,
                    // 等待不是答案 — — 告诉
                    // 用户是。
                    if (s > provider::kRetryAfterTerminalSeconds) {
                        can_retry = false;
                    } else {
                        if (s > 600) s = 600;
                        delay = std::chrono::seconds(s);
                    }
                } else {
                    delay = provider::backoff_with_jitter(klass, attempt);
                }
                auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                    delay + std::chrono::milliseconds{999}).count();
                // 向用户显示的尝试计数器反映了实际为这
                // 一失败提供的任何预算。
                const int shown_attempt = mid_stream ? mid_prior : prior_transient;
                m.s.status = std::string{provider::to_string(klass)}
                           + " — retrying in " + std::to_string(secs) + "s"
                           + " (attempt " + std::to_string(shown_attempt + 1)
                           + "/" + std::to_string(retry_cap) + ")…";
                m.s.status_until = m.now
                                 + delay + std::chrono::milliseconds{1500};
                if (!reschedule_streaming(m.s.phase, [&](phase::Active& c) {
                        c.transient_retries = attempt + 1;
                        if (mid_stream) c.mid_stream_failures = mid_prior + 1;
                        c.last_failure_at   = m.now;
                        // 单调递增的 无 -progress 计数——仅由真实的模型输出重置(
                        // 参见 StreamTextDelta / ToolUseStart).
                        ++c.no_progress_failures;
                        c.retry             = retry::Scheduled{};
                    }))
                    return Cmd::none();
                if (last) {
                    // 弹出前跳出 — 见 认证 -retry 弹出在
                    // 上(主和主) "#r" 推理插槽)。
                    m.ui.view_cache.drop(m.d.current.id, last->id);
                    m.ui.view_cache.drop(m.d.current.id,
                                         MessageId{last->id.value + "#r"});
                    m.d.current.messages.pop_back();
                }
                Message placeholder;
                placeholder.role = Role::Assistant;
                m.d.current.messages.push_back(std::move(placeholder));
                auto retry_cmd = Cmd::after(delay, Msg{RetryStream{}});
                // 每一个 重试 与延迟和剩余预算:两个问题 被卡住转弯的上升是
                // "is it still trying?" 财务报告和已
                // 审计财务报表 "why did it stop?" ,
                // 两者都通过看到尝试序列来回答.
                AGT_LOG(Ui, Info, "stream.retry",
                        "attempt={} delay_ms={} mid_stream={}",
                        shown_attempt + 1,
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            delay).count(),
                        mid_stream ? 1 : 0);
                // 没有 force_redraw 需要:预 -settle
                // 上方已定稿 StreamingMarkdown 高度在
                // 前方锁定 重试 数据流 输入新三角洲; 普通 diff
                // 路径处理其余的。 (内置) force_redraw ”
                // “软”案件(B)的安全性,但在这里没有必要。 ”
                return retry_cmd;
            }

            // 终端路径——丢弃源 ctx 并丢弃到 Idle.
            m.s.phase = phase::Idle{};
            // LOOP : 此转弯失败。 循环不是 死亡( 用户要求它
            // 继续运行) , 并且确实 不 内容 -fire 立即
            // ——立即恢复 -send 没有 退避 429是怎样加深
            // 自己的利率限制的。 BACKS OFF : 看见
            // loop_backoff_for 尊重供应商的 重试-After
            // 发送一个,否则升级一个每 -class 延迟。 取消 是一个
            // 彻底阻止它的阶级。 Esc 意思是,毫不含糊地停止。
            if (m.ui.composer.looping()) {
                if (klass == provider::ErrorClass::Cancelled) {
                    m.ui.composer.disarm_loop();
                } else {
                    // 速率-limit和认证失败获得缓慢的进度:
                    // 都意味着 "you are asking too hard / wrongly" ,
                    // 以及 重试 要么就快让事情更糟
                    const int rank =
                        (klass == provider::ErrorClass::RateLimit
                         || klass == provider::ErrorClass::Auth) ? 1 : 0;
                    m.ui.composer.loop_backoff(
                        rank,
                        e.retry_after ? static_cast<int>(e.retry_after->count())
                                      : 0,
                        maya::anim::default_clock().now_ms());
                }
            }
            if (klass == provider::ErrorClass::Cancelled) {
                m.s.status = "cancelled";
            } else if (no_progress_latched) {
                // 梯子烧了整个,没有 -progress
                // 预算: 在一个内容之前的每次尝试都失败 字节。
                // 重复原始服务器错误7次是不能行动的——说出发生了什么,
                // 在哪里修复。
                m.s.status = "giving up after "
                    + std::to_string(provider::kMaxNoProgressFailures)
                    + " failed attempts with no model output \xe2\x80\x94 "
                      "check the server/model (^P \xe2\x86\x92 Custom host, "
                      "^/ \xe2\x86\x92 models). Last error: " + e.message;
            } else {
                m.s.status = "error: " + e.message;
            }
            if (last) {
                if (klass != provider::ErrorClass::Cancelled)
                    last->error = e.message;
                std::string fail_msg = "stream ended before tool args "
                                       "closed: " + e.message;
                auto contains_ci = [](std::string_view hay,
                                      std::string_view needle) noexcept {
                    if (needle.size() > hay.size()) return false;
                    for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i) {
                        bool ok = true;
                        for (std::size_t j = 0; j < needle.size(); ++j) {
                            char a = hay[i + j], b = needle[j];
                            if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + 32);
                            if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + 32);
                            if (a != b) { ok = false; break; }
                        }
                        if (ok) return true;
                    }
                    return false;
                };
                if (contains_ci(e.message, "filtering policy")
                    || contains_ci(e.message, "content filter")
                    || contains_ci(e.message, "blocked by content")) {
                    fail_msg =
                        "Anthropic's safety classifier blocked the tool "
                        "input mid-stream (\"" + e.message + "\"). This "
                        "is the upstream policy on the OAuth path "
                        "tripping on generated content; it is "
                        "probabilistic. Try once more if the content is "
                        "innocuous (lorem ipsum, JSON, code) — most "
                        "false positives clear on retry. If it blocks "
                        "again, write a short stub file via `write` and "
                        "build the rest with successive `edit` calls. "
                        "(Direct API-key callers usually bypass this "
                        "filter entirely; a long chain of safety "
                        "blocks on OAuth is a hint to switch auth.)";
                }
                for (auto& tc : last->tool_calls) {
                    if (tc.is_pending()) {
                        auto now = m.now;
                        tc.status = ToolUse::Failed{
                            tc.started_at(), now, fail_msg};
                    }
                    std::string{}.swap(tc.args_streaming);
                }
            }
            {
                auto now = m.now;
                auto ttl = std::chrono::seconds{
                    klass == provider::ErrorClass::Cancelled ? 3 : 6};
                m.s.status_until = now + ttl;
                auto stamp = m.s.status_until;
                auto status_cmd = Cmd::after(
                    std::chrono::duration_cast<std::chrono::milliseconds>(ttl)
                        + std::chrono::milliseconds{50},
                    Msg{ClearStatus{stamp}});
                return status_cmd;
            }
        },
        [&](RetryStream) -> Cmd {
            // 计划 重试 已发射。如果用户 已取消 等待期间( Esc
            // → CancelStream 将相位移到 Idle 中,
            // 无需做任何动作。 否则会发生过渡。 重试 回到
            // 新来 -flight 这么新鲜的 -launched
            // 数据流 自己的错误流经正常的分类器路径.
            if (auto* a = active_ctx(m.s.phase)) a->retry = retry::Fresh{};
            if (m.s.is_idle()) return Cmd::none();
            return cmd::launch_stream(m);
        },
        [&](CancelStream) -> Cmd {
            // Esc ——全面同步拆解.
            if (auto* a = active_ctx(m.s.phase); a && a->cancel) a->cancel->cancel();

            // 压缩 -cancel 记录里没有东西,
            // 所以我们把记录丢掉 -transcript 缓冲区 并
            // 清除英寸 -flight 标志。用户看到其完整的预览
            // -compaction 记录稿保持完整
            //
            // 工作流程保护: 一条排在 $1 后面的信件 已取消 否则,
            // 紧凑会停息,直到用户手动重新使用 -submits 东西,
            // 把它排在这里 取消 感觉像
            // "keep going without compacting"
            // 而不是说 "freeze my whole session."
            // launch_stream 软 -trim 将适合
            // 传输 载荷 虽然我们没有做总结
            const bool was_compacting = m.s.compacting;
            if (m.s.compacting) {
                m.s.compacting              = false;
                m.s.compaction_target_index = 0;
                m.s.compaction_buffer.clear();
            }

            // 辅助人员部分工作及最后敲定 -flight 工具 调用.
            auto now = m.now;
            if (!m.d.current.messages.empty()
                && m.d.current.messages.back().role == Role::Assistant) {
                auto& last = m.d.current.messages.back();
                if (!last.pending_stream.empty()) {
                    last.streaming_text += last.pending_stream;
                    last.pending_stream.clear();
                }
                if (!last.streaming_text.empty()) {
                    if (last.text.empty()) last.text = std::move(last.streaming_text);
                    else                   last.text += std::move(last.streaming_text);
                    std::string{}.swap(last.streaming_text);
                }
                // 预览 -settle StreamingMarkdown 改为 锁 信件的
                // 高度在下一个消息之前 view() 运行。见 finalize_turn 财务
                // 报告和已审计财务报表 StreamError 完全没有理由
                if (!last.text.empty()) {
                    auto& cache = m.ui.view_cache.message_md(
                        m.d.current.id, last.id);
                    if (!cache.streaming)
                        cache.streaming = std::make_shared<maya::StreamingMarkdown>();
                    cache.streaming->set_content(last.text);
                    cache.streaming->finish();
                }
                for (auto& tc : last.tool_calls) {
                    // settle_failed ,不是赤裸的失败。
                    // RUNNING 工具也一样 -- Esc 中点陆地
                    // -execution ——一个赤裸的失败 —— ——
                    // 丢弃了他们的窗口, 所以一个工具,在40秒前真的
                    // 运行了 取消 与洗涤错误相反的失败, 相同的根 :
                    // 窗户必须横跨整个覆盖,
                    // 并且只有在记录它时才携带。
                    if (!tc.is_terminal())
                        tc.settle_failed(now, "cancelled");
                    std::string{}.swap(tc.args_streaming);
                }
                if (last.text.empty() && last.tool_calls.empty()) {
                    // 信件中除了 a 之外没有其它信息( 可能缓存)
                    // 逐步显示 其简介的部件 流式传输 生命,
                    // 放弃它 缓存 条目 之前 流行, 被删除的消息无法在
                    // 任意一个家庭里孤零零的条目( 主 +) "#r").
                    m.ui.view_cache.drop(m.d.current.id, last.id);
                    m.ui.view_cache.drop(m.d.current.id,
                                         MessageId{last.id.value + "#r"});
                    m.d.current.messages.pop_back();
                }
            }

            m.d.pending_permission.reset();
            m.s.phase = phase::Idle{};
            m.s.status = "cancelled";
            {
                auto ttl = std::chrono::seconds{3};
                m.s.status_until = now + ttl;
                auto stamp = m.s.status_until;
                auto status_cmd = Cmd::after(
                    std::chrono::duration_cast<std::chrono::milliseconds>(ttl)
                        + std::chrono::milliseconds{50},
                    Msg{ClearStatus{stamp}});
                // 如果用户 已取消 a
                // COMPACTION 并排队工作,
                // 立即启动命令。 取消正常的转弯
                // 会让队列独自存在, 因为用户刚刚
                // 杀死自己的 -flight 请求
                // —— 他们可能想要输入不同的下一条,
                // 而不是排队自动排水。
                // 压缩是用户最初没有要求的侧操作;
                // 杀死它不应该惩罚他们的实际
                // 请求。
                if (was_compacting && !m.ui.composer.queued.empty()) {
                    auto& head = m.ui.composer.queued.front();
                    m.ui.composer.text        = std::move(head.text);
                    m.ui.composer.attachments = std::move(head.attachments);
                    m.ui.composer.cursor      = static_cast<int>(m.ui.composer.text.size());
                    m.ui.composer.queued.erase(m.ui.composer.queued.begin());
                    auto sub_cmd = submit_message(m);
                    return Cmd::batch(
                        std::move(status_cmd), std::move(sub_cmd));
                }
                return status_cmd;
            }
        },
        [&](ProactiveContextReady pcr) -> Cmd {
            // 推迟的预告 -turn 检索( submit_message
            // 挡住了这个转弯 数据流 发射到地面上 此 转弯——
            // 在后继助理占位符之前,它读作刚才提出的问题的检索上下文
            // ——并启动 数据流 现在,一样 -turn 搁浅,
            // 不 过期-context 或错 -question 成本。
            // 成本。
            //
            // 丢弃一个 过期 到达: 转弯它当时 已取消 ,
            // 也许一个新的开始(它不能得到它的块或秒
            // launch_stream 只有等待转弯的ID匹配。
            auto* wait = active_ctx(m.s.phase);
            if (!wait || pcr.id == 0 || wait->awaiting_retrieval != pcr.id)
                return Cmd::none();
            wait->awaiting_retrieval = 0;

            // 后面的讯息是空助手占位符,由 submit_message。
            // 在用户消息之前插入该消息 传输 命令会保留 User
            // (问题) → User (context) →
            // Assistance( reply)。如果形状不是我们所
            // 期望的(防御),则在发射前返回附加。
            if (!pcr.block.empty()) {
                Message ctx_msg;
                ctx_msg.role = Role::User;
                // 一项任务:信息 IS a 检索
                // -context 而不是一时的希望 而不是
                // 一日的希望 标志 数字独立设置两行
                ctx_msg.proactive = Message::ProactiveContext{
                    .confidence = pcr.confidence, .expanded = false};
                ctx_msg.text = std::move(pcr.block);
                auto& msgs = m.d.current.messages;
                if (!msgs.empty() && msgs.back().role == Role::Assistant
                        && msgs.back().text.empty()
                        && msgs.back().tool_calls.empty()) {
                    msgs.insert(msgs.end() - 1, std::move(ctx_msg));
                } else {
                    msgs.push_back(std::move(ctx_msg));
                }
                // submit_message 冻结通过用户转弯
                // (它推后的位置符是 LIVE 助理人员 末尾
                // 将冷冻前缀延伸至正义 -inserted
                // 上下文信息也一样——但不是 穿过后继的占位器,
                // 它必须活着,所以它的 数据流 逐步显示 封住,
                // 不要动 size() -1,
                // 当 末尾 是占位符,否则通过全部计数。
                const auto& mm = m.d.current.messages;
                const bool tail_is_placeholder =
                    !mm.empty() && mm.back().role == Role::Assistant
                    && mm.back().text.empty() && mm.back().tool_calls.empty();
                freeze_through(m, tail_is_placeholder ? mm.size() - 1
                                                      : mm.size());
            }
            // 清除 "retrieving context…" 状态和发射请求 该模型
            // 一直在等待。 launch_stream 需要已经设定的活跃阶段——
            // submit_message 留下我们 流式传输 {ctx},没有被移动,
            // 所以 active_ctx 住在这里。
            m.s.status.clear();
            m.s.status_until = {};
            return cmd::launch_stream(m);
        },
    }, sm);
}

} // namespace agentty::app::detail
