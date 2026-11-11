// Tool-execution-result 帮助者 : apply_tool_output
// 翻译 a ToolExecOutput 改为 Done/Failed 匹配时的状态 ToolUse;
// mark_tool_rejected 是对称的 -liner 费用 权限 否认。
// 都走着走着 m.d.current.messages 因为a ToolCallId
// 只有在转弯之内 才具有本地的独特性 我们不把它们索引

#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/app/update.hpp"
#include "agentty/io/clipboard.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

#include <maya/core/overload.hpp>
#include <maya/core/motion.hpp>   // anim::keep_animating — 帧 请求
#include <nlohmann/json.hpp>

#include "agentty/runtime/app/cmd_factory.hpp"
#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/view/thread/turn/agent_timeline/tool_args.hpp"
#include "agentty/runtime/view/host_escape.hpp"
#include "agentty/runtime/view/thread/turn/agent_timeline/tool_helpers.hpp"
#include "agentty/store/store.hpp"
#include "agentty/tool/spec.hpp"
#include "agentty/tool/util/utf8.hpp"
#include "agentty/util/logx.hpp"

#if AGENTTY_MCP
#include <mcp/tools/util/shell_detour.hpp>
#endif

namespace pn = agentty::ui::panel;

namespace agentty::app::detail {

// 连续这么多的外壳绕道之后,尖端在顶端得到一个简单的提醒.
// 3:一个绕道是噪音,两个可以是1-off,三个是习惯.
inline constexpr int kDetourReminderAt = 3;

using json = nlohmann::json;

namespace {

// 用于主机(编辑)集成的私人OSC号码逃逸——参见ui::
// host::file_event_osc. 5379被主流终端所不称;
// "agentty;"载荷标签让一个主机和我们的匹配,忽略了其他OSC.
constexpr int kHostOsc = 5379;

// 构建主机跟随 -along OSC 载荷 用于完成的 FILE 工具,
// 或非的无选择 -file 工具/不活动集成。只有指定具体路径的工具才符合条件(
// read/edit/write/move/list_dir ); 路径来自同一
// 流式传输-aware 阅读时间线 头文件 使用,读取的1 -based 冲抵
// 改为以下 -along 线条。
[[nodiscard]] std::optional<std::string> file_event_osc_for(const ToolUse& tc) {
    const std::string& n = tc.name.value;
    const bool is_file_tool =
        n == "read" || n == "edit" || n == "write" || n == "move"
        || n == "list_dir";
    if (!is_file_tool) return std::nullopt;
    std::string path = ui::tool_path_arg(tc);
    if (n == "move" && path.empty())
        path = ui::safe_arg(tc.args, "destination");
    if (path.empty()) return std::nullopt;
    std::optional<int> line;
    if (n == "read") {
        if (int off = ui::safe_int_arg(tc.args, "offset", 0); off > 0) line = off;
    }
    return ui::host::file_event_osc(n, path, line);
}

// 人数 -tool-output 天花板在谈话中被携带。
// Tool 已锁定捕获的跑步者( 读 = 1) MiB ,grep=8个
// MiB ,击打=30 KB 以至他们俩在其中,确是忠贞的, 调用.
// 长久 -lived 进程 内存 ,一个统一的谈话 -side 盖是右旋钮:
// 近处有50 grep 结果的会话 -cap 400,000 + 时数
// MB 总计* 终端 * 工具输出我们从未修剪。 Head + 末尾
// 保持模型( Y) -relevant 上下文( 文件的顶部,
// 上次错误等) 并允许多 -MiB 抓取紧凑到~256 KiB.
constexpr std::size_t kStoredOutputCap = 256u * 1024u; // 256KiB

std::string clamp_output(std::string s) {
    if (s.size() <= kStoredOutputCap) return s;
    constexpr std::size_t half = kStoredOutputCap / 2;
    // MUST的两个切点均位于UTF-8代码-point边界上:
    // 夹线是传输作为tool_result的切点,
    // nlohmann::json::dump()将type_error.
    // 316抛到一个拆分的多-byte序列上——这将会杀死整个请求中的序列化.
    // 头:最大边界 \\\ 一半。 末尾 : 开始走动
    // FORWARD 任何继续 字节 故保留后缀起于铅 字节.
    const std::size_t head_end = tools::util::safe_utf8_cut(s, half);
    std::size_t tail_start = s.size() - half;
    while (tail_start < s.size()
           && (static_cast<unsigned char>(s[tail_start]) & 0xC0) == 0x80)
        ++tail_start;
    std::string out;
    out.reserve(kStoredOutputCap + 64);
    out.append(s, 0, head_end);
    out.append("\n\n… (");
    out.append(std::to_string((s.size() - head_end - (s.size() - tail_start)) / 1024));
    out.append(" KiB elided to keep memory bounded) …\n\n");
    out.append(s, tail_start, s.size() - tail_start);
    return out;
}

// 一旦一个工具到达 终端 状态, 单 -call 流式传输 抓图 缓冲区
// (原始内容存档于2018-09-21). Raw Data couplator,
// 惰性 args.dump 缓存 —— 死亡体重—— 死亡 传输 关闭,
// 解析参数, 任何关于 -render 来源 `args` 直接释放它们,
// 这样长会话不会给每个完成的工具钉一个副本 调用.
void release_streaming_buffers(ToolUse& tc) {
    std::string{}.swap(tc.args_streaming);
    tc.args_dump_cache.clear();
    tc.args_dump_cache.shrink_to_fit();
    tc.args_dump_valid = false;
}

// 留着 渲染 时间在为一个有界限的窗口计时, 所以 maya 得到的跟踪
// -up 帧 生活状况 -tail shrink/overflow 一张卡
// 的高度变化中点后需要调节 -turn。这是与延迟结算相同的杠杆
// -freeze 使用量( 百分比) subscribe.cpp 打开勾选
// 符 settle_cooldown_ticks > 0; 武装这里的每
// 一张牌 -height 变异繁殖 agent_session 总是
// -on 时间准确 接口边界 高度变化的地方,增长 -only 这么长
// 的时间 -flight 窗口中,绝对不会短短的。
void arm_reconcile_cooldown(Model& m) {
    constexpr int kReconcileTicks = 6;
    if (m.ui.settle_cooldown_ticks < kReconcileTicks)
        m.ui.settle_cooldown_ticks = kReconcileTicks;
    ::maya::anim::keep_animating();
}


} // 命名空间结束。

// 构建工具 -output-viewer 条目列表: 每一个已结算的工具 调用
// 在当前 线程 与非 -empty 存储输出, NEWEST 首次 (用户刚刚
// 看的卷轴过去的一个是条目0) 由 kMax Entries /
// kSnapshot Budget 所包围,所以马拉松会话不能气球 覆盖面板 打开。
// 抓图(复制输出) 字节 使 : 使 覆盖面板 不受
// 其下记录的更改——每个存储的输出都已被夹住到256
// 个 KiB 上游 ,所以复制品便宜且有界限.
[[nodiscard]] std::vector<tool_output::Entry> collect_viewer_entries(const Model& m) {
    std::vector<tool_output::Entry> out;
    std::size_t budget = tool_output::kSnapshotBudget;

    // 单行 0 是 LIVE 工具,如果当前有活动:这包括
    // 参数 流式传输 / 权限 (待处理 以及当地执行(运行)。
    // Ctrl +O 说 "nothing to inspect yet"
    // 整个模型/工具 -input 阶段——确切时间
    // edit/write 预览最有用。
    for (auto mit = m.d.current.messages.rbegin();
         mit != m.d.current.messages.rend(); ++mit) {
        bool found = false;
        for (auto tit = mit->tool_calls.rbegin();
             tit != mit->tool_calls.rend(); ++tit) {
            const auto* run = std::get_if<ToolUse::Running>(&tit->status);
            if (!run && !tit->is_pending() && !tit->is_approved()) continue;
            tool_output::Entry e;
            e.is_live  = true;
            e.name     = tit->name.value;
            e.title    = ui::tool_display_name(tit->name.value);
            e.detail   = ui::tool_timeline_detail(*tit);
            e.output   = run ? run->progress_text : std::string{};
            e.call     = *tit;
            e.failed   = false;
            char buf[32];
            const char* phase = run ? "running"
                : (tit->is_approved() ? "approved" : "streaming input");
            std::snprintf(buf, sizeof buf, "%s \xc2\xb7 %.1fs",
                          phase, ui::tool_elapsed(*tit));
            e.trailing = buf;
            if (!e.output.empty())
                e.trailing += (e.output.size() >= 1024)
                    ? " \xc2\xb7 " + std::to_string(e.output.size() / 1024) + " KB"
                    : " \xc2\xb7 " + std::to_string(e.output.size()) + " B";
            budget -= std::min(budget, e.output.size());
            out.push_back(std::move(e));
            found = true;
            break;
        }
        if (found) break;
    }
    for (auto mit = m.d.current.messages.rbegin();
         mit != m.d.current.messages.rend(); ++mit) {
        // 获取 -context 牌也在这里浮出水面,所以 完整
        // 模型基于的段落可在 Ctrl+O 覆盖面板 ——包括卡片卷入
        // ( 不可变 ) 冷冻前缀,其中 -place Ctrl
        // +U 扩展无法到达。 透過觀眾的平原渲染 -text (行
        // 线 -numbered) 正文 路径: 默认构造
        // ToolUse `call` 没有任何结构化的种类,
        // 所以 正文 舞台倒塌到原始状态 -text 自动分枝。
        if (mit->is_proactive_context() && !mit->text.empty()) {
            // 脱下 <retrieved-context> … </retrieved-context>
            // 栅栏和铅 -in 序言 正文 仅仅是那些段落。
            std::string body = mit->text;
            if (auto open = body.find('>'); open != std::string::npos
                && body.compare(0, 18, "<retrieved-context") == 0) {
                std::size_t start = body.find('\n', open);
                start = (start == std::string::npos) ? open + 1 : start + 1;
                std::size_t end = body.rfind("</retrieved-context>");
                if (end == std::string::npos || end < start) end = body.size();
                body = body.substr(start, end - start);
            }
            while (!body.empty() && (body.back() == '\n' || body.back() == ' '
                                  || body.back() == '\t' || body.back() == '\r'))
                body.pop_back();
            if (!body.empty() && out.size() < tool_output::kMaxEntries
                && body.size() <= budget) {
                budget -= body.size();
                tool_output::Entry e;
                e.name   = "retrieved_context";
                e.title  = "Retrieved context";
                e.output = std::move(body);
                e.failed = false;
                // 详细:通过数(每个源块以"["开头).
                std::size_t passages = 0;
                for (std::size_t p = 0; (p = e.output.find("[", p)) != std::string::npos; ++p)
                    if (p == 0 || e.output[p - 1] == '\n') ++passages;
                e.detail = passages == 0 ? std::string{}
                    : std::to_string(passages)
                        + (passages == 1 ? " passage" : " passages");
                e.trailing = (e.output.size() >= 1024)
                    ? std::to_string(e.output.size() / 1024) + " KB"
                    : std::to_string(e.output.size()) + " B";
                out.push_back(std::move(e));
            }
            continue;
        }
        for (auto tit = mit->tool_calls.rbegin();
             tit != mit->tool_calls.rend(); ++tit) {
            const auto& tc = *tit;
            if (!tc.is_terminal()) continue;
            const auto& body = tc.output();
            if (body.empty()) continue;
            if (out.size() >= tool_output::kMaxEntries) return out;
            if (body.size() > budget) continue;   // 跳过, 保留小一点的
            budget -= body.size();

            tool_output::Entry e;
            e.failed = tc.is_failed();
            e.output = body;
            // 原始名称驱动分类颜色徽章;显示名称+细节
            // 再利用时间线的帮助,所以列表读取的
            // 完全像记录卡是re-inspected.
            e.name   = tc.name.value;
            e.title  = ui::tool_display_name(tc.name.value);
            e.detail = ui::tool_timeline_detail(tc);
            e.call   = tc;   // 快照 给富人 正文 渲染 (diff/gutter)
            // 拖拉:持续时间 ^大小;失败者首先如此。
            e.trailing = e.failed ? "failed" : "";
            if (float secs = ui::tool_elapsed(tc); secs >= 0.05f) {
                char buf[32];
                std::snprintf(buf, sizeof buf, "%s%.1fs",
                              e.trailing.empty() ? "" : " \xc2\xb7 ", secs);
                e.trailing += buf;
            }
            if (!e.trailing.empty()) e.trailing += " \xc2\xb7 ";
            e.trailing += (body.size() >= 1024)
                ? std::to_string(body.size() / 1024) + " KB"
                : std::to_string(body.size()) + " B";
            out.push_back(std::move(e));
        }
    }
    return out;
}

// 留着 Ctrl+O 快照 二者同步 stream.cpp (工具数) -input
// deltas/snapshots )这个 TU (执行) progress/results).
void resync_live_tool_viewer(Model& m) {
    auto* o = m.ui.panel.get<pn::ToolOutput>();
    if (!o) return;
    // Anchor:记住用户的哪个条目是按身份,而不是指数——
    // 它们下面的活行换行指数prepending/removing.
    const bool was_live_selected =
        o->index >= 0 && o->index < static_cast<int>(o->entries.size())
        && o->entries[static_cast<std::size_t>(o->index)].is_live;
    auto fresh = collect_viewer_entries(m);
    const bool has_live = !fresh.empty() && fresh.front().is_live;
    if (fresh.empty()) { return; }   // 保留最后的快照 而不是空白
    // 如果用户正在读取直播行,请将其固定在0行(它
    // 要么仍然住在那里,要么刚刚安顿到最新的条目).
    if (was_live_selected) {
        o->index = 0;
    } else {
        // 无 -live 选择: 活行的存在将所有东西都移为一。
        // 运行时按三角洲调整索引 -row 数到一,
        // 同一完成的条目仍留在光标下。
        const bool had_live = !o->entries.empty() && o->entries.front().is_live;
        o->index += (has_live ? 1 : 0) - (had_live ? 1 : 0);
    }
    o->entries = std::move(fresh);
    o->index = std::clamp(o->index, 0,
                          std::max(0, static_cast<int>(o->entries.size()) - 1));
}

void apply_tool_output(Model& m, const ToolCallId& id,
                       std::expected<std::string, tools::ToolError>&& result,
                       std::optional<FileChange>&& change,
                       std::vector<FileChange>&& changes,
                       std::vector<ImageContent>&& images,
                       std::uint64_t exec_seq) {
    const bool landed = with_exec_tool(m, id, exec_seq, [&](ToolUse& tc) {
        // 幂等 : 已经装入的工具 终端 状态( 已执行 / 失败
        // / 已拒绝 ) 保持状态。 realistic ways a late
        // ToolExecOutput 能够在此降落 :
        //   (a) 墙壁 -clock 看门狗 部队
        //       -failed 工具在60秒时;
        //       工作线程 线程 最终没有受伤
        //       seconds/minutes 后因.
        //       最初的失败原因( "hung" )对用户比晚期产出
        //       更有用——而且可重新进行覆盖。 -arm 已经
        //       超越这个工具的转弯。
        //   (b) 同一id上的重复的分派(不应发生,
        //       但可以低廉地防御)。
        // 不管哪种方式,降低晚期结果都保持了历史的
        // 稳定.
        //
        // 冻前缀 : with_live_tool 已跳过带有索引的
        // 信件 < frozen_through ,
        // 所以一个 ToolExecOutput 说 竞态 冻结( 回合已定,
        // 用户再次提交, 工具) 工作线程 终于回来了) 无声无息地不
        // -ops 没有门,变异会降落在 传言中,它使元素进入
        // m.ui.frozen 谓 不可变 ——可永久可见.
        // -Running 加载动画 输入 回滚缓冲区.
        if (tc.is_terminal()) return;
        auto now = m.now;
        if (result) {
            // 漂流提醒。 贝壳会得到贝壳: 在贝壳检查后, 下一个贝壳是
            // 81%的时间( 22% 的贝壳是本地贝壳)。
            // 每枚贝壳 -call 一旦习惯开始,小费很容易被打消,
            // 所以当绕道链时,在决定点上直截了当地说( 事件-driven
            // 参考:第1段。 OpenDev 2603.05344 §
            // 2.3.4) 与 计数 substitutable() ,
            // 提示所用的同一门。任何其他 调用 重现历史
            bool detour = false;
#if AGENTTY_MCP
            if (tc.name.value == "shell" && tc.args.is_object())
                if (auto it = tc.args.find("command"); it != tc.args.end() && it->is_string())
                    detour = ::mcp::tools::util::analyze_detour(it->get<std::string>()).substitutable();
#endif
            if (detour) {
                if (++m.d.shell_detour_streak >= kDetourReminderAt)
                    *result = "[reminder] shell detour #"
                        + std::to_string(m.d.shell_detour_streak)
                        + " in a row. the tip below names the exact native "
                          "call; make that call next. native calls run in "
                          "parallel and each gets its own card.\n"
                        + std::move(*result);
            } else {
                m.d.shell_detour_streak = 0;
            }
            tc.settle_done(now, clamp_output(std::move(*result)),
                           std::move(images));
        } else {
            // 渲染 带类型的 错误为 "[kind] detail"
            // 因此该分类在工具中可见 -card
            // 历史没有失去人类 -readable 细节。
            // 模型只需要回弦;当视图在分类上分枝时,
            // 类型的结构为未来保留。
            tc.settle_failed(now, clamp_output(result.error().render()));
        }
        release_streaming_buffers(tc);
    });
    // 过期XX 工作线程(其调用为已取消)也
    // 不得排队审查:用户已经离开该调用。
    if (exec_seq != 0 && !landed) return;
    // 排队结构变化(编辑/写 / apply_patch / 替换)
    // for diff-review. Multi-file tools (replace) fill `changes`; single-file
    // 工具填充 `change`. DEDUP 每个文件: 连续编辑到 相同
    // 路径崩溃为显示净原版的检讨条目,所以 面板 读作
    // 说明 login.cpp 发生了哪些修改。
    // 而不是堆叠重叠的 diff。只有成功时排队——一个失败的工具没有写任何内容。
    if (result) {
        auto queue_one = [&](FileChange&& ch) {
            auto& q = m.d.pending_changes;
            auto it = std::ranges::find(q, ch.path, &FileChange::path);
            if (it != q.end()) {
                std::string original = it->original_contents;
                *it = diff::compute(ch.path, original, ch.new_contents);
            } else {
                q.push_back(std::move(ch));
            }
        };
        if (!changes.empty())
            for (auto& ch : changes) queue_one(std::move(ch));
        else if (change)
            queue_one(std::move(*change));
    }
    // 运行 加载动画 全部进入 output/error
    // 正文 ——卡片此时已使高度变化(特别是失败卡片因
    // 错误行而增大),如果高度变化在钟点即将失效时会改变溢出前
    // 缀(最后转向Idle的工具,或组合在一起) FPS=0 帧),
    // maya 组成了转动 帧 一次 永远得不到跟随 -up
    // 帧 编号 shrink/overflow 校正需要——旧
    // 卡的顶端线条 回滚缓冲区 (无)
    // 卡片在上一屏就被截断。
    // 腐败问题)。 agent_session 永远看不到,
    // 因为钟声响起 UNCONDITIONALLY 镜像: 解除调节 冷却,
    // 使时钟持续运行到几个 帧 任何牌之后的牌 -height 更改,
    // 保证 maya 调和 接口边界 和参考文献
    // 一样 -on 勾选。
    arm_reconcile_cooldown(m);
}

void mark_tool_rejected(Model& m, const ToolCallId& id,
                        std::string_view reason) {
    with_live_tool(m, id, [&](ToolUse& tc) {
        auto now = m.now;
        if (reason.empty()) {
            tc.status = ToolUse::Rejected{now};
        } else {
            // 一个理由意味着policy/failure拒绝. settle_failed如果
            // 工具已经发出,则保留一个真正的执行窗口,如果它没有发出,则记录NONE——对于
            // "User rejected this tool call"来说,这个记录总是:
            // 工具坐在了那边 权限 提示词 复制到
            // executing_since (这里用来烤纸牌出生
            // 当作假 分派 印花和账单 提示词 等待为工具时间。
            tc.settle_failed(now, clamp_output(std::string{reason}));
        }
        release_streaming_buffers(tc);
    });
    // 与 apply_tool_output : 被拒的工具卡
    // 正文 变化高度( E) 加载动画 拒绝的理由,所以保持时钟
    // 滴答几声 帧 允许 maya 调和 接口边界.
    arm_reconcile_cooldown(m);
}

// ============================================================================
// tool_update — 状态更新函数 费用 `msg::ToolMsg`
// ============================================================================
// Tool-execution 本地跑者的结果 +
// 权限-prompt 用户的解析。 权限 住在这里是因为
// 权限 提示词 总是一个"关于"一个" 待处理 工具 调用 和
// 分辨率反馈到工具状态机器中——没有干净的分割。

Cmd tool_update(Model& m, msg::ToolMsg tm) {
    using maya::overload;

    return std::visit(overload{
        // ── 活性工具进展(流式传输子进程输出) ────────────
        // 由子进程运行器生成, 并且有迄今为止的全部累积输出 ~ 80 ms。
        // 我们刚刚设定了它。 不 Cmd 返回——并依赖现有的
        // Tick 事件订阅 (在 ExecutingTool
        // 改为: -render 中。如果工具已经定稿(延迟),
        // 则忽略 快照 赛车 终端 ToolExecOutput).
        [&](ToolExecProgress& e) -> Cmd {
            // 冻前缀不是可变 ——进展较晚. 快照 转身,
            // 已经解决 m.ui.frozen 默默
            // 无声 -ops 给
            with_exec_tool(m, e.id, e.exec_seq, [&](ToolUse& tc) {
                if (auto* r = std::get_if<ToolUse::Running>(&tc.status)) {
                    // 带子 -and-braces 终端 线条
                    // -discipline 输入 -tree 已
                    // 清理捕获边界的子进程导师, 但是 快照 也可以
                    // 从不骑马的路程到达(外部) MCP 服务器 流式传输 进程,
                    // 子代理反馈回声工具输出。 ESC/CR/BS 字
                    // 节 到达卡片 正文 油漆控制器 字节 以 存储
                    // 单元 并使他们承诺: 回滚缓冲区 ——流浪汉.
                    // -glyph 腐败类。廉价:O(n)扫描,
                    // 以及常见(已经) -clean ) 案件只复制本。
                    // 先是脱衣服 快照 提前到达 -cut 从 内 部
                    // -tree 跑步者( 4) progress_tail
                    // 自动调试 工作线程 );切线将其它任何源线绑在同一窗口。
                    // progress_text 复制到
                    // 卡片中 帧 现场时间线重建。
                    r->progress_text = tools::util::strip_terminal_controls(
                        tools::util::progress_tail(e.snapshot));
                    // 重置活性时钟:这个工具显然正在取得进展,
                    // 因此楔网的盖子从这里重启,
                    // 而不是从(可能为许多-minutes-ago)
                    // 发射时间.
                    r->last_progress_at = m.now;
                }
            });
            // 如果用户正在观看CtrlO查看器,则保持其Live行
            // (row 0)跟踪这个新鲜输出的位置.
            resync_live_tool_viewer(m);
            return Cmd::none();
        },

        // ── 人数 -tool 墙 -clock 看门狗 ──────────────────────────────────
        [&](ToolTimeoutCheck& e) -> Cmd {
            bool flipped = false;
            with_live_tool(m, e.id, [&](ToolUse& tc) {
                if (tc.is_terminal()) return;
                auto now = m.now;
                const auto* sp = tools::spec::lookup(tc.name.value);
                auto secs = sp ? sp->max_seconds : std::chrono::seconds{0};
                std::string reason;
                if (tc.is_pending() || tc.is_approved()) {
                    reason = "tool stayed " + std::string{tc.status_name()}
                        + " for " + std::to_string(secs.count())
                        + " s \xe2\x80\x94 args probably never finished streaming "
                        "(transient API error mid-tool_use, or the "
                        "stream silently exited without a terminal event).";
                } else {
                    reason = "tool execution exceeded "
                        + std::to_string(secs.count())
                        + " s wall-clock \xe2\x80\x94 likely hung on a blocking "
                        "syscall (slow/dead filesystem mount, network "
                        "freeze, or worker deadlock). The tool's worker "
                        "thread may continue in the background; its "
                        "result will be discarded if it ever returns.";
                }
                tc.settle_failed(now, std::move(reason));
                flipped = true;
            });
            if (!flipped) return Cmd::none();
            auto cmd = cmd::kick_pending_tools(m);
            return cmd;
        },

        // ── Tool 执行结果 ───────────────────────────────────────
        [&](ToolExecOutput& e) -> Cmd {
            // A 工作线程 其 人 名 调用 曾经是 已取消 或已解决;
            // 其id现在可能属于不同的 调用 把它全扔掉 全部放下
            if (!tool_exec_is_current(m, e.id, e.exec_seq)) {
                AGT_LOG(Tool, Info, "tool.stale_result",
                        "id={} seq={} dropped", e.id.value, e.exec_seq);
                return Cmd::none();
            }
            // 任务 副作用 自动调试 UI 计划状态——
            // 只有在 调用 最终确切的状态会降落在这里 即使
            // 活着的人 流式传输 同步( 时间轴 )
            // stream.cpp ) 与一个部分阵列相竞。
            if (e.result) {
                // 最新数据 -first 并在比赛中止步: 调用
                // 换成现在,所以这是O(1) 实际上,
                // 而不是走遍每一个工具 调用 输入 线程 每一结果。
                bool found = false;
                for (auto mit = m.d.current.messages.rbegin();
                     !found && mit != m.d.current.messages.rend(); ++mit)
                    for (const auto& tc : mit->tool_calls)
                        if (tc.id == e.id) {
                            if (tc.name == "todo") sync_todo_state_from_args(m, tc.args);
                            found = true;
                            break;
                        }
            }
            apply_tool_output(m, e.id, std::move(e.result), std::move(e.change),
                              std::move(e.changes), std::move(e.images),
                              e.exec_seq);
            // 如果CtrlO查看器打开,请刷新它,这样Live行
            // 就在这个工具完成的瞬间就结算成一个完成的条目.
            resync_live_tool_viewer(m);

            // 合作 -host 随其后 -along : 当运行在编辑器上时
            // PTY (Emacs/vterm 看着我们 OSC ,
            // 告诉它是什么文件 代理刚刚触摸,以便打开 / 逐步显示 / 亲身感受它。
            // 帧-safe (maya cmd::emit_osc 关闭 -of-band ),
            // 且完全拒绝 -op 正常状态 终端 (m.env.host_integration 事实并非如此。
            Cmd host_cmd = Cmd::none();
            if (m.env.host_integration) {
                for (const auto& msg_ : m.d.current.messages) {
                    bool done_scan = false;
                    for (const auto& tc : msg_.tool_calls) {
                        if (tc.id != e.id || !tc.is_done()) continue;
                        if (auto osc = file_event_osc_for(tc))
                            host_cmd = cmd::emit_osc(kHostOsc, *osc);
                        done_scan = true; break;
                    }
                    if (done_scan) break;
                }
            }

            // 无中点 -run 冷冻或修剪这里。 finalize_turn
            // (无) agent_session MessageStop 模拟)
            // ——整个代理转动被包裹成一个转动元素并推向 m.ui.frozen
            // 解剖学上说,刻在中间 -run (上一个)
            // freeze_settled_subturns +
            // trim_frozen_above_ 可视区域 调用 被记录的
            // 源头 "redraws from top + scrollback corruption"
            // 在每个工具上 继续 接口边界 :冻结导致一个进入,
            // hash_id maya 组件 缓存 现场没有看到 末尾
            // 上一个 帧 ,所以,请 缓存 错过和重新 -emitted
            // 这些行,有时已经过去 -committed 回滚缓冲区.
            // agent_session 从来没有刻在中间
            // -stream ,并显示零腐败/零长期减速(由
            // long_session 我们也是这样
            // A SPECULATIVE 读 -only 工具( 启动中点) -stream
            // 时间 StreamToolUseEnd )可以在完成时完成 传输 还在 流
            // 式传输 轮到这个了 做 不 运行 kick_pending_tools 然后:
            // 它可以将活动 ctx 退出 phase::Streaming
            // (或更糟,见0) 待处理 工作并启动续作 数据流
            // 反对: 传输 仍能交付。你应当把结果降落在地面上。
            // StreamFinished → finalize_turn
            // 运行正常踢键并找到此工具 终端.
            if (m.s.is_streaming()) {
                if (host_cmd.is_none()) return Cmd::none();
                return host_cmd;
            }
            auto kick = cmd::kick_pending_tools(m);
            if (host_cmd.is_none())
                return kick;
            return Cmd::batch(
                std::move(kick), std::move(host_cmd));
        },

        // ── Permission ──────────────────────────────────────────────
        [&](PermissionApprove) -> Cmd {
            if (!m.d.pending_permission) return Cmd::none();
            auto id = m.d.pending_permission->id;
            // 权限 只能用活的枪来射击工具 末尾 冰冻的
            // 转弯 根據定义 每一次 待处理 权限.
            // with_live_tool 冻僵了 -prefix
            // 门是结构上的保证 不变量.
            with_live_tool(m, id, [&, shown = m.d.pending_permission->def_hash](ToolUse& tc) {
                // 将批准情况标为类型状态:待处理  ⁇ 批准.
                // kick_pending_tools 核准后
                // 作为 "permission already granted"
                // 和经过同一地点的路线 副作用-parallel 门作为
                // 非 -permissioned 工具——如果兄弟
                // 阅读仍在运行,则新批准 Write/Bash 等待它
                // 而不是赛车。
                tc.status = ToolUse::Approved{tc.started_at()};
                // 用户实际看到的。 目录 重建在这与发射之间,
                // execute_approved 拒绝,
                // 而不是在此名称下运行不同的工具。
                tc.approved_def_hash = shown;
            });
            m.d.pending_permission.reset();
            return cmd::kick_pending_tools(m);
        },
        [&](PermissionReject) -> Cmd {
            if (!m.d.pending_permission) return Cmd::none();
            auto id = m.d.pending_permission->id;
            mark_tool_rejected(m, id, "User rejected this tool call.");
            m.d.pending_permission.reset();
            auto cmd = cmd::kick_pending_tools(m);
            return cmd;
        },
        [&](PermissionApproveAlways) -> Cmd {
            if (!m.d.pending_permission) return Cmd::none();
            auto id   = m.d.pending_permission->id;
            auto name = m.d.pending_permission->tool_name;
            // 记录会话 -scoped 授权 用于此工具 NAME 所以每个
            // 未来 调用 自动切换到此会话 -approves (顾问于
            // kick_pending_tools 这同样也传播给兄弟会
            // 待处理 在当前批次中同名工具: re -kick 下调
            // -evaluates 每一个 待处理 工具与现在相对 -populated
            // 授权 设定,所以排队的兄弟 `bash` 不会再了 -prompt
            // 镜像 Zed 的每张 -session 允许 -list
            // 与活的兄弟姐妹传播。
            m.d.session_grants.insert(name.value);
            // 还有 内容 被批准,而不仅仅是名称。 授权 以一个
            // 赤裸的名字覆盖着这个名字以后决定的, 对于一个
            // MCP 工具是服务器的选择——所以 授权 携带 指纹
            // 用户所看到的定义,以及 execute_approved
            // 如果它改变,则拒绝。
            const auto granted_hash = m.d.pending_permission->def_hash;
            if (granted_hash != 0) m.d.grant_def_hash[name.value] = granted_hash;
            // 坚持 授权 (泽德语) always_allow 规则: 重新装入 -proof.
            // 记录上,所以下一部的-recordXX 保存 背着它,
            // 而不是写一个比它早的拷贝.
            //
            // 存储为 "name@<hex>" 定义也能够幸存。空名(旧建筑的条目,
            // 或手) -edited settings.json 仍然(
            // ) 加载 和手段 "any definition".
            Cmd grant_save = Cmd::none();
            {
                auto& allow = m.d.persisted.always_allow_tools;
                std::string entry = name.value;
                if (granted_hash != 0)
                    entry += "@" + std::format("{:016x}", granted_hash);
                const auto same_tool = [&](const std::string& e) {
                    const auto at = e.rfind('@');
                    return (at == std::string::npos ? e : e.substr(0, at))
                           == name.value;
                };
                auto it = std::find_if(allow.begin(), allow.end(), same_tool);
                if (it == allow.end()) {
                    allow.push_back(std::move(entry));
                    grant_save = save_record(m);
                } else if (*it != entry) {
                    *it = std::move(entry);   // 内容 -granted 定义
                    grant_save = save_record(m);
                }
            }
            m.s.status = name.value + ": always allowed (persists \xc2\xb7 "
                         "Shift+Tab profile cycle resets)";
            m.s.status_until = m.now
                             + std::chrono::seconds{4};
            with_live_tool(m, id, [&, shown = m.d.pending_permission->def_hash](ToolUse& tc) {
                tc.status = ToolUse::Approved{tc.started_at()};
                tc.approved_def_hash = shown;   // 见PermissionApprove
            });
            m.d.pending_permission.reset();
            return Cmd::batch(std::move(grant_save),
                              cmd::kick_pending_tools(m));
        },
    }, tm);
}

} // namespace agentty::app::detail
