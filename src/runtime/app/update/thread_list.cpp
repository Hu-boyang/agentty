// thread_list.cpp - 说 线程 切换器 状态更新函数 (open/pick/delete/rename
// + 自动 ThreadsLoaded。从 panels.cpp ;
// 只共享 reset_to_fresh_thread (此处定义, 其唯一用户).

#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/app/update.hpp"
#include "agentty/util/logx.hpp"

#include <algorithm>
#include <chrono>
#include <climits>
#include <limits>
#include <unordered_set>
#include <utility>

#include <maya/core/overload.hpp>
#include <maya/platform/io.hpp>

#include "agentty/runtime/app/cmd_factory.hpp"
#include "agentty/runtime/app/deps.hpp"
#include "agentty/foreign/names.hpp"
#include "agentty/io/persistence.hpp"   // new_id: 纯函数,直接调用
#include "agentty/provider/selection.hpp"
#include "agentty/runtime/login.hpp"
#include "agentty/runtime/mem.hpp"
#include "agentty/runtime/panel/common.hpp"
#include "agentty/runtime/panel/thread_filter.hpp"
#include "agentty/runtime/view/cache.hpp"
#include "agentty/runtime/view/helpers.hpp"
#include "agentty/tool/skills.hpp"
#include "agentty/tool/subagent.hpp"
#include "agentty/tool/util/utf8.hpp"   // safe_utf8_cut: 重命名盖子草稿

namespace pn = agentty::ui::panel;

namespace agentty::app::detail {

namespace {

// 放下那个 -yet-opened 外国会议进入 选择面板 列表。
// 已经打开的会话是本地 线程 和同一身份证明一样,
// 所以它被跳过这里; 线程 排队代表它,保持秩序
void merge_foreign(Model& m) {
    std::erase_if(m.d.threads, [](const Thread& t) { return t.foreign_only; });
    if (m.d.foreign.sessions.empty() || !m.d.ui().foreign_sessions) return;
    std::unordered_set<std::string> have;
    have.reserve(m.d.threads.size());
    for (const auto& t : m.d.threads) have.insert(t.id.value);
    bool added = false;
    for (const auto& s : m.d.foreign.sessions)
        if (!have.contains(s.id.value)) { m.d.threads.push_back(s); added = true; }
    if (added)
        std::ranges::stable_sort(m.d.threads, std::greater<>{}, &Thread::updated_at);
}

// 导入的 线程 背后的会话: 它的文件看起来有变化吗 ?
// 与上次扫描相比,不是磁盘,所以状态更新函数保持纯度.
[[nodiscard]] bool session_moved_on(const Model& m, const Thread& t) {
    if (!t.imported) return false;
    for (const auto& s : m.d.foreign.sessions)
        if (s.id == t.id && s.imported)
            return s.imported->size != t.imported->size || s.imported->mtime != t.imported->mtime;
    return false;
}

}  // 命名空间结束。

}  // namespace agentty::app::detail

namespace agentty::app::detail {

namespace pick = agentty::ui::pick;
using maya::overload;

namespace {

// 或 选择面板 列表中:它的查询, 在其文件夹范围中。
[[nodiscard]] std::vector<int> visible(const Model& m, const pn::ThreadList& p) {
    return ui::visible_threads(m.d.threads, p.query, m.env.cwd, p.all_workspaces, p.only_foreign);
}

// 电话 线程 下划线 选择面板 光标, 过滤器后。 - 1 当无时。
[[nodiscard]] int picked_thread(const Model& m, const pn::ThreadList& p) {
    const auto vis = visible(m, p);
    if (vis.empty()) return -1;
    return vis[static_cast<std::size_t>(
        std::clamp(p.index, 0, static_cast<int>(vis.size()) - 1))];
}

[[nodiscard]] int visible_count(const Model& m, const pn::ThreadList& p) {
    return static_cast<int>(visible(m, p).size());
}

// 在哪里,我们在哪里 线程 `id` 坐在这 选择面板 '当前列表, 或 - 1。
[[nodiscard]] int visible_index_of(const Model& m, const pn::ThreadList& p,
                                   const ThreadId& id) {
    const auto vis = visible(m, p);
    for (int i = 0; i < static_cast<int>(vis.size()); ++i)
        if (m.d.threads[static_cast<std::size_t>(vis[static_cast<std::size_t>(i)])].id == id)
            return i;
    return -1;
}

// 旧 线程 没有 选择面板 最新消息:
// 预览和信件计数的要填入的。
constexpr std::size_t kSummaryBatch = 12;

[[nodiscard]] Cmd next_summary_batch(Model& m) {
    if (m.s.summaries_filling) return Cmd::none();
    std::vector<ThreadId> ids;
    for (const auto& t : m.d.threads) {
        if (t.message_count == 0 && t.preview.empty() && !t.summary_tried && !t.foreign_only)
            ids.push_back(t.id);
        if (ids.size() == kSummaryBatch) break;
    }
    if (ids.empty()) return Cmd::none();
    // 标在作品前,所以无法概括的线程(空,
    // 不可读)不再被选中,回填总是结束.
    for (auto& t : m.d.threads)
        if (std::ranges::find(ids, t.id) != ids.end()) t.summary_tried = true;
    m.s.summaries_filling = true;
    return cmd::backfill_summaries_async(std::move(ids));
}

// 偷看会停在一排上 装上它之前,所以在列表中保持不动
// 加载 每隔一段时间 线程 已经过去了。
constexpr auto kPeekSettle = std::chrono::milliseconds{90};

// 将鼠标指向光标下的行。 一个新的行将其卷轴重排到最近的转弯,
// 除非它被缓存或已经装入, (重) 启动一个按键结算定
// 时器: 持有 QQ 继续替换它, 所以只有您停在
// 的行是 已加载。
[[nodiscard]] Cmd sync_peek(Model& m) {
    auto* p = m.ui.panel.get<pn::ThreadList>();
    if (!p) return Cmd::none();
    const int at = picked_thread(m, *p);
    if (at < 0) { p->peek_id.clear(); return Cmd::none(); }
    const ThreadId id = m.d.threads[static_cast<std::size_t>(at)].id;
    if (p->peek_id == id.value) return Cmd::none();
    p->peek_id   = id.value;
    p->peek_turn = 0;
    const bool cached = std::ranges::any_of(
        m.ui.peek_cache, [&](const auto& t) { return t.id == id; });
    // 关闭在外观中:追踪行(所以打开显示),
    // 但加载什么都没有.
    if (!m.d.ui().thread_preview || cached || m.ui.peek_loading == id.value)
        return Cmd::cancel("thread-peek");
    return Cmd::after("thread-peek", kPeekSettle, Msg{ThreadPeekSettled{id}});
}

// 贴贴或 带类型的 标题的一块, 平整为一行:
// 换行符和制表符成为空格, 其他控制 字节。
// 草稿在提交时再次被清理;这只保持屏幕上正常状态。
[[nodiscard]] std::string one_line(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        const auto u = static_cast<unsigned char>(c);
        if (c == '\n' || c == '\r' || c == '\t') out.push_back(' ');
        else if (u >= 0x20 && u != 0x7f)         out.push_back(c);
    }
    return out;
}

}  // 命名空间结束。
// ── Fresh-thread 重置( 重置) ────────────────────────────────────────────────────
// 把模型换到一个品牌 -new 空 线程 并返回 终端
// 重置擦拭离开的 线程 关闭了 -screen.
//
// 这是 SHARED 有两个切入点的后方核心: `NewThread` (^N ,
// 在 选择面板 和 国 务 院 `ThreadListDelete` 当删除的行是活动 线程.
// 故意的,在打电话者之后 save/delete 决定-
// NewThread 将离开的恒定 线程 首先,
// 删除刚刚销毁了它——所以呼叫者拥有这个政策,而这个是重置。
// 让两个呼叫者在一个代码路径上保持其漂移状态就是阻止他们
// 漂流(他们曾经有,删除副本没有相位重置+内核释放+内线擦拭,
// 这正是制造中位的机械) -stream 交换安全。
//
// 返回 reset_inline Cmd 呼叫者可以用自己的命令进行
// 批量(删除也踢出一个) 线程-list 刷新+敬酒仪式).
[[nodiscard]] Cmd reset_to_fresh_thread(Model& m) {
    // 整个放下 渲染 缓存 : 每一(tid,
    // msg)项属于 线程 我们要走了,他们的消息 永远不会再冻结
    // (冻结是唯一的 -entry 掉下来,它只在 当前 线程).
    // 嵌入键 thread_id 所以没有碰撞——这纯粹是让旧的重新
    // 恢复 线程's staged/pinned 输入时不会停留
    m.ui.view_cache.clear();
    // A 线程 加载 还在飞行中是用来代替的笔录,
    // 忘了它,结果不会覆盖新鲜的 线程.
    if (!m.s.loading_thread.value.empty()) {
        m.s.loading_thread = {};
        m.s.thread_loading = false;
    }
    m.d.current = Thread{};
    m.d.saved_fps.clear();
    m.d.current.id = persistence::new_id();
    m.d.current.auto_title_ok = true;
    m.d.current.workspace = m.env.cwd;
    m.d.current.created_at = m.d.current.updated_at =
        m.wall_now();
    m.s.reset_usage_for_thread(0);
    clear_frozen(m);
    // 关闭一次 弹出面板 设计了 旧 线程 : 数据 选择面板
    // 我们从,加上调色板/代码 -block 选择面板 其内容属于离境者,
    // 线程 最后一次回复
    m.ui.panel.close<pn::ThreadList>();
    m.ui.panel.close<pn::Palette>();
    m.ui.panel.close<pn::CodeBlocks>(); m.ui.panel.close<pn::CodeBlockResult>();
    // 擦掉整份作曲草稿——贴上 -but-unsent 图像
    // ( 或任何) 标签 / 队列中的消息)属于 线程 我们要走了,
    // 它曾经是空的 -bytes 图像附件到新窗口 线程
    // 第一次提交,400"请求。
    reset_composer_draft(m.ui.composer);
    // 新鲜空 线程 没有任何活转弯- 任何 流式传输 相位并
    // 把内核递回到中间 -stream 换不掉的 传输 运行
    // 对齐 a 线程 已经不复存在。
    end_turn(m.s.phase);
    // 聪明 -Mode 百分比 -THREAD 线路状态不能泄露到新状态中 线程:
    // 复杂势头( classify_score_with_context
    // 继承从 PREVIOUS ,会话的叠加偏差,以及最后一个转折的
    // 签名(结果反馈会将新的属性归为 线程 第一次答复 旧 线程 路径。
    // 学习的每 -workspace 以往( RoutingMemory
    // )通过设计生存——它们是十字形的. -thread.
    m.s.smart_turn_complexity  = smart::Complexity::Standard;
    m.s.smart_effort_bias      = 0;
    release_to_kernel();
    // 调 -warm 活动提供者的 TLS 套接字 发射
    // -time 温室前的年长时间通常超过 池 用户读回信、
    // 写作、点击 ^N ,90s闲置 TTL 已经把暖气切断了
    // 没有这个 首次 每一个新的转折 线程 内容 -pays 完整
    // DNS+TCP+TLS 第一次握手(~150-300毫秒) SSE 字节。
    // 返回时, 副作用 ,所以拨号与用户输入的第一个 提示词.
    //
    // 人数 maya 合同 reset_inline 是那个 一个
    // 允许的线条 `\x1b[3J` : 明确、用户 -initiated
    // 内容交换,它擦拭 已保存-lines (包括 -agentty ),
    // 可以接受,原因正是用户要求切换 线程 执行。 不 扩展为每 -turn
    // 路径。
    return Cmd::batch(cmd::prewarm_provider(m), cmd::reset_inline());
}

// 重命名( E) 线程 `id` ,或与 `clear` 将选中的
// 名称放下, 回到自动名称: 其首条消息的第一行, 自动 -titled
// 下个回复后再次重写。两个印章 renamed_at ,所以要么击败
// 过期 复制另一个 agentty 可能仍然会写入。更新列表行,
// 如果是打开的 线程, m.d.current (这是
// -saved 并且会写回旧标题).
[[nodiscard]] Cmd apply_rename(Model& m, const ThreadId& id, std::string title, bool clear) {
    const auto it = std::ranges::find(m.d.threads, id, &Thread::id);
    Thread* open = m.d.current.id == id ? &m.d.current : nullptr;
    if (clear) {
        // 第一个 -line 名称,从第一个消息中您 带类型的.
        const Thread& src = open ? *open : (it != m.d.threads.end() ? *it : m.d.current);
        std::string first;
        for (const auto& msg : src.messages)
            if (msg.role == Role::User && !msg.fork_note && !msg.is_compact_summary
                && !msg.proactive.has_value() && !msg.text.empty()) {
                first = msg.text;
                break;
            }
        // 列表行不保存信件: 保留其当前标题,
        // 直到 线程 打开并下个回复标题。
        title = first.empty() ? (it != m.d.threads.end() ? it->title : src.title)
                              : persistence::title_from_first_message(first);
    }
    const std::int64_t at = std::chrono::duration_cast<std::chrono::seconds>(
        m.wall_now().time_since_epoch()).count();
    auto apply = [&](Thread& t) {
        t.title        = title;
        t.renamed_at   = at;
        t.name_is_auto = clear;
        // 清除后的名字再次获得汽车标题( 如果有授权)。
        if (clear) { t.auto_titled = false; t.auto_title_ok = true; }
    };
    if (it != m.d.threads.end()) apply(*it);
    ++m.d.threads_rev;
    if (open) apply(*open);
    auto toast = set_status_toast(m, clear ? "name cleared \xc2\xb7 back to automatic"
                                           : "renamed to \"" + title + "\"");
    return Cmd::batch(Cmd(RenameThread{id, std::move(title), at, clear}), std::move(toast));
}

Cmd thread_list_update(Model& m, msg::ThreadListMsg tm) {
    return std::visit(overload{
        [&](OpenThreadList) -> Cmd {
            // 如果没有加载在飞行中,则在背景中刷新——
            // 行走+解析太慢(秒,有数百个多-MB线程文件),
            // 无法在此同步运行. 选择面板在缓存
            // 列表中立即打开; ThreadsLoaded登陆后,
            // 新条目逐渐消失.
            Cmd cmd = Cmd::none();
            if (!m.s.threads_loading) {
                m.s.threads_loading = true;
                cmd = cmd::load_threads_async();
            }
            // 其他特工的会话 未经更改的文件不是Re-read
            if (!m.s.foreign_loading && m.d.ui().foreign_sessions) {
                m.s.foreign_loading = true;
                cmd = Cmd::batch(std::move(cmd), cmd::load_foreign_sessions_async(m.d.foreign));
            }
            // 打开 AT 当前 线程 ,而不是行 0 — 用户的精神主锚是
            // "where am I" ,从那里循环(XX 较新/XX 年长)
            // 来反射 Alt е е -cycle 秩序。 秩序。
            pn::ThreadList fresh{{0}};
            fresh.index = std::max(0, visible_index_of(m, fresh, m.d.current.id));
            m.ui.panel.descend(std::move(fresh));
            return Cmd::batch(std::move(cmd), sync_peek(m));
        },
        [&](CloseThreadList) -> Cmd {
            // Esc 带有过滤器先清除,所以一个打字符永远不会关闭列表.
            if (auto* p = m.ui.panel.get<pn::ThreadList>(); p && !p->query.empty()) {
                const int was = picked_thread(m, *p);
                p->query.clear();
                // 保持同一线程,现在处于未过滤位置.
                p->index = was < 0 ? 0 : std::max(0, visible_index_of(
                    m, *p, m.d.threads[static_cast<std::size_t>(was)].id));
                return Cmd::none();
            }
            ascend(m);   // Esc:返回到打开此的任何内容，或关闭
            return Cmd::none();
        },
        [&](ThreadListFilterInput& e) -> Cmd {
            auto* p = m.ui.panel.get<pn::ThreadList>();
            if (!p) return Cmd::none();
            p->confirm_remove.clear();
            p->query += ui::utf8_encode(e.ch);
            p->index = 0;                    // 最佳( 最近) 匹配第一
            m.ui.thread_list_scroll.y = 0;
            return sync_peek(m);
        },
        [&](ThreadListFilterBackspace) -> Cmd {
            auto* p = m.ui.panel.get<pn::ThreadList>();
            if (!p || p->query.empty()) return Cmd::none();
            p->confirm_remove.clear();
            p->query.erase(static_cast<std::size_t>(
                ui::utf8_prev(p->query, static_cast<int>(p->query.size()))));
            p->index = 0;
            m.ui.thread_list_scroll.y = 0;
            return sync_peek(m);
        },
        [&](ThreadListMove& e) -> Cmd {
            auto* p = m.ui.panel.get<pn::ThreadList>();
            if (!p) return Cmd::none();
            p->confirm_remove.clear();   // 移动裁军a 待处理 ^D
            p->index = ui::step(p->index, e.delta, visible_count(m, *p));
            return sync_peek(m);
        },
        [&](ThreadListJump& e) -> Cmd {
            auto* p = m.ui.panel.get<pn::ThreadList>();
            if (!p) return Cmd::none();
            const int sz = visible_count(m, *p);
            if (sz == 0) return Cmd::none();
            p->confirm_remove.clear();   // 跳跃式解除武装 待处理 ^D
            using W = ThreadListJump::Where;
            constexpr int kPage = 14;  // 与 pickers.cpp 中的 kViewportH 匹配
            switch (e.where) {
                case W::Home:     p->index = 0; break;
                case W::End:      p->index = sz - 1; break;
                case W::PageUp:   p->index = std::max(0, p->index - kPage); break;
                case W::PageDown: p->index = std::min(sz - 1, p->index + kPage); break;
            }
            return sync_peek(m);
        },
        [&](ThreadListScopeToggle& e) -> Cmd {
            auto* p = m.ui.panel.get<pn::ThreadList>();
            if (!p) return Cmd::none();
            // 瞄准镜,在 Tab 命令。一个不列出(否) 线程
            // 在此文件夹中,没有其它 -tool 正在跳过会议)。
            enum Scope { All, Folder, Foreign };
            const bool has_folder  = ui::workspace_thread_count(m.d.threads, m.env.cwd) > 0;
            const bool has_foreign = ui::foreign_thread_count(m.d.threads) > 0;
            std::vector<Scope> ring{All};
            if (has_folder)  ring.push_back(Folder);
            if (has_foreign) ring.push_back(Foreign);
            if (ring.size() == 1)
                return set_status_toast(m, m.s.foreign_loading
                        ? "still looking for Codex / Claude Code sessions\xe2\x80\xa6"
                        : "only one list here: no other folders or tools have threads",
                    std::chrono::seconds{3});
            const Scope now = p->only_foreign ? Foreign
                            : !ui::effective_all(m.d.threads, m.env.cwd, p->all_workspaces) ? Folder
                            : All;
            const auto at_now = std::ranges::find(ring, now);
            const int i = at_now == ring.end() ? 0 : static_cast<int>(at_now - ring.begin());
            const int n = static_cast<int>(ring.size());
            const Scope next = ring[static_cast<std::size_t>(((i + e.delta) % n + n) % n)];

            // 如果光标在新范围,则保持光标在相同的线程上.
            const int was = picked_thread(m, *p);
            p->all_workspaces = next != Folder;
            p->only_foreign   = next == Foreign;
            p->confirm_remove.clear();
            const int at = was < 0 ? -1 : visible_index_of(
                m, *p, m.d.threads[static_cast<std::size_t>(was)].id);
            p->index = std::max(0, at);
            m.ui.thread_list_scroll.y = 0;
            return sync_peek(m);
        },
        [&](ThreadPeekScroll& e) -> Cmd {
            auto* p = m.ui.panel.get<pn::ThreadList>();
            if (!p || p->peek_id.empty() || !m.d.ui().thread_preview) return Cmd::none();
            // peek_turn 算回最近的转弯( 0 = 最新转弯)。
            // 将这个转弯的转弯切入到这里 线程 是真的,所以远端不是
            // "sticky"。在 线程 已经 已加载 计数未知 :
            // 保留压轴(一旦它降落就被打开),而不是将其丢弃,
            // 因为它读作死箭键。
            const auto it = std::ranges::find(m.ui.peek_cache, p->peek_id,
                                              [](const auto& t) { return t.id.value; });
            const int last = it == m.ui.peek_cache.end()
                ? std::numeric_limits<int>::max() / 2
                : std::max(0, static_cast<int>(it->turns.size()) - 1);
            const int cur = std::clamp(p->peek_turn, 0, last);
            p->peek_turn = e.to_end ? (e.delta < 0 ? last : 0)
                                    : std::clamp(cur - e.delta, 0, last);
            return Cmd::none();
        },
        [&](ThreadPeekSettled& e) -> Cmd {
            // 只有在光标还在那一行,而且它还没有装入。
            // 另一行的 加载 正在飞行中,它不会被挡住 :
            // 那个是 过期 现在, 和着陆在 缓存 无害的。
            const auto* p = m.ui.panel.get<pn::ThreadList>();
            if (!p || p->peek_id != e.id.value) return Cmd::none();
            if (m.ui.peek_loading == e.id.value) return Cmd::none();
            m.ui.peek_loading = e.id.value;
            // 未打开的会话没有 agentty 文件: 读取源代码。
            for (const auto& t : m.d.threads)
                if (t.id == e.id && t.foreign_only && t.imported)
                    return cmd::load_foreign_peek_async(std::move(e.id), *t.imported);
            return cmd::load_thread_peek_async(std::move(e.id));
        },
        [&](ThreadPeekLoaded& e) -> Cmd {
            if (m.ui.peek_loading == e.id.value) m.ui.peek_loading.clear();
            if (!e.turns) return Cmd::none();
            std::erase_if(m.ui.peek_cache, [&](const auto& t) { return t.id == e.id; });
            m.ui.peek_cache.insert(m.ui.peek_cache.begin(),
                                   Model::UI::PeekThread{e.id, std::move(*e.turns)});
            if (m.ui.peek_cache.size() > Model::UI::kPeekCache)
                m.ui.peek_cache.resize(Model::UI::kPeekCache);
            // 时按压 已加载 被保留在未密闭处,现在适合他们。
            if (auto* p = m.ui.panel.get<pn::ThreadList>(); p && p->peek_id == e.id.value)
                p->peek_turn = std::clamp(
                    p->peek_turn, 0,
                    std::max(0, static_cast<int>(m.ui.peek_cache.front().turns.size()) - 1));
            return Cmd::none();
        },
        // ── Model 互换: 互换前承付溢出 ──────────────
        //
        // ThreadListSelect 财务报告和已审计财务报表 NewThread 替换 m.d.current 批发.
        // 交换前,我们为分派 Cmd::commit_scrollback_overflow
        // ()——不 force_redraw(见下文历史)。
        //
        // 为什么做 -overflow 需要:
        //   maya 内置 diff 处理行 [0], prev_rows - term_h (a)
        //   承诺 回滚缓冲区 ("updatable_start" 输入 serialize.cpp).
        //   讽ρ 线程 溢额( prev_rows >
        //   term_h 这些行被 diff 扫描和每 -row
        //   发售后,换了新的 线程 画布排在Y位置上的内容
        //   完全不同 但diff仍然认为
        //   "scrollback, untouchable"
        //   绝不发泄。结果:明显; 接口边界 中点 -viewport
        //   中标 传输 仍然持旧 -thread 字节 反
        //   新规 -thread 画布,表现为相邻行上
        //   两个不相关的文字片段.
        //
        //   commit_scrollback_overflow() 调
        //   用 输入 maya's commit_inline_overflow
        //   预收的 prev_cells 以最大值( 0) 表示,
        //   prev_rows - term_h 排队,跑完后 prev_rows
        //   ≤ term_h, updatable_start 滴到0,
        //   diff扫描了全部常见范围——每个可见的行都正确发射到新位置 线程.
        //
        //   卷出线条 prev_cells 已经 字节
        //   编号 终端 已经承诺 其本土 回滚缓冲区
        //   反正(它们都是通过底部排放的) -edge \r。
        //   n的期间 流式传输 承诺承认这一事实——零 传输
        //   副作用.
        //
        // 为什么 不 force_redraw:
        //   cmd::force_redraw 降级 ~
        //   同步 ~ 过期 , 将下一个路径 渲染 由
        //   编译案(B)进行。 -to-fit 分支(
        //   scroll_n > 0) 发音\n 可视区域
        //   新建时的下方 帧 高于旧光标的乘以 可视区域 顶
        //   部——每个屏幕上滚动一行(旧的) 线程 末尾
        //   + 上面的主机 shell 历史) 上到
        //   终端-owned 回滚缓冲区 历史:8becb88
        //   号的犯案正是如此,于0b24148年恢复。
        [&](ThreadListSelect) -> Cmd {
            auto* p = m.ui.panel.get<pn::ThreadList>();
            Cmd cmd = Cmd::none();
            if (p) p->confirm_remove.clear();   // 选择一个待处理XXXX ^D
            if (p && !m.d.threads.empty() && !m.s.thread_loading) {
                const int at = picked_thread(m, *p);
                if (at < 0) return Cmd::none();   // 过滤器匹配无
                const Thread& meta = m.d.threads[static_cast<std::size_t>(at)];
                // 一样 -thread 内容 -select - 关闭
                // 选择面板 是唯一有用的动作。没有 ASync 加载
                // : 将只是重新划分相同 字节 闪耀。
                if (meta.id == m.d.current.id) {
                    m.ui.panel.close<pn::ThreadList>();
                    return Cmd::none();
                }
                m.s.thread_loading = true;
                m.s.loading_thread = meta.id;
                // 暖一下套接字,然后第一个转弯 线程 用户正在
                // 切换 INTO 已经没有了 -pay 握手(
                // 手握手握手掌) 池 闲着 TTL 通常在作曲室里拆迁。
                // 返回,而不是表演。
                Cmd load;
                if (meta.imported && (meta.foreign_only || session_moved_on(m, meta))) {
                    // A Codex / Claude 代码会话: 在第一次打开时导入,
                    // 或重新导入 -read 之后人们又说了很多
                    load = cmd::open_foreign_async(meta.id, *meta.imported);
                    if (meta.foreign_only)
                        set_status_toast(m, "opening " + foreign::display_name(meta.imported->source)
                                                + " session\xe2\x80\xa6", std::chrono::seconds{2});
                } else {
                    load = cmd::load_thread_async(meta.id);
                }
                cmd = Cmd::batch(cmd::prewarm_provider(m), std::move(load));
            }
            m.ui.panel.close<pn::ThreadList>();
            return cmd;
        },
        [&](ThreadListDelete) -> Cmd {
            // ^D 输入 线程 选择面板 : 2个 -press 删除时使用 confirm_remove ,
            // 镜像 SettingsListRemove / AccountRemove。
            // 第一排按下标记它 待处理 (在视图中显示警徽); 相同 返回行数
            // DeleteThread 副作用 任意
            // move/jump/select/new/close 解除 待处理 州立大学.
            auto* p = m.ui.panel.get<pn::ThreadList>();
            if (!p || m.d.threads.empty()) return Cmd::none();
            // 光标是 FILTERED 列表; 用于计算数据。
            // picked_thread 地图回放并夹住它,所以一个列表
            // 缩小的Aync刷新不能索引到结尾.
            const int idx = picked_thread(m, *p);
            if (idx < 0) return Cmd::none();
            const Thread& target = m.d.threads[static_cast<std::size_t>(idx)];
            // 使用线程 id作为确认密钥——在标题编辑上稳定.
            const std::string key = target.id.value;
            if (p->confirm_remove != key) {
                p->confirm_remove = key;
                return Cmd::none();
            }
            // 第二媒体——承诺。 快照 需要的一切,都给我 OUT 矢量元素的
            // 之前 erase() : 擦除无效 `target` 读到
            // target.title / target.id 之后是用法,
            // 使用时, -after-free。在参考文献直播时复制到这里。
            const ThreadId  target_id = target.id;
            const bool      was_current = (target_id == m.d.current.id);
            const std::string label =
                target.title.empty() ? "(untitled)" : target.title;

            p->confirm_remove.clear();
            // 删除是 值 ,在下面分批分解为两支手臂的返回——
            // 带到这里来,在那里 target_id 仍然在手头。
            Cmd del = Cmd(DeleteThread{target_id});
            m.d.threads.erase(m.d.threads.begin() + idx);
            ++m.d.threads_rev;
            // 将光标粘合,使其在移除后仍然有效。
            const int sz = visible_count(m, *p);
            if (sz == 0) {
                p->index = 0;
            } else if (p->index >= sz) {
                p->index = sz - 1;
            }
            std::erase_if(m.ui.peek_cache, [&](const auto& t) { return t.id == target_id; });
            std::string msg = "deleted \"" + label + "\"";
            if (was_current) msg += " \xe2\x80\x94 started a new thread";
            auto toast = set_status_toast(m, std::move(msg));
            // 删除 活动 线程 叶 m.d.current 指着一个
            // 线程 其文件已不存在 —— 转换为新空 线程 通过
            // 相同 核心项目 NewThread。这个单一代码路径是
            // 保证相重设 + 内核释放(因此是中位)。 -stream
            // 删除不能离开 传输 与死人对决 线程 ),
            // 该 modal/skill/cache 撕裂,
            // 还有 reset_inline 擦拭删除的 线程 关闭了 -screen.
            if (was_current) {
                auto reset = reset_to_fresh_thread(m);
                return Cmd::batch(std::move(del), cmd::load_threads_async(),
                                  std::move(reset), std::move(toast));
            }
            return Cmd::batch(std::move(del), std::move(toast), sync_peek(m));
        },
        [&](ThreadListRenameStart) -> Cmd {
            auto* p = m.ui.panel.get<pn::ThreadList>();
            if (!p) return Cmd::none();
            const int idx = picked_thread(m, *p);
            if (idx < 0) return Cmd::none();
            const Thread& target = m.d.threads[static_cast<std::size_t>(idx)];
            p->confirm_remove.clear();
            p->renaming     = target.id.value;
            p->rename_input = target.title;   // 前-filled:编辑,不要重排
            p->rename_cursor = static_cast<int>(p->rename_input.size());
            return Cmd::none();
        },
        [&](ThreadListRenameChar& e) -> Cmd {
            auto* p = m.ui.panel.get<pn::ThreadList>();
            if (!p || p->renaming.empty()) return Cmd::none();
            const std::string add = one_line(ui::utf8_encode(e.ch));
            // 拒绝过盖子而不是剪接提交,
            // 所以你看到的是存储的。
            if (p->rename_input.size() + add.size() > persistence::kTitleMaxBytes)
                return Cmd::none();
            p->rename_input.insert(static_cast<std::size_t>(p->rename_cursor), add);
            p->rename_cursor += static_cast<int>(add.size());
            return Cmd::none();
        },
        [&](ThreadListRenameBackspace) -> Cmd {
            auto* p = m.ui.panel.get<pn::ThreadList>();
            if (!p || p->renaming.empty() || p->rename_cursor <= 0) return Cmd::none();
            const int at = ui::utf8_prev(p->rename_input, p->rename_cursor);
            p->rename_input.erase(static_cast<std::size_t>(at),
                                  static_cast<std::size_t>(p->rename_cursor - at));
            p->rename_cursor = at;
            return Cmd::none();
        },
        [&](ThreadListRenameDelete) -> Cmd {
            auto* p = m.ui.panel.get<pn::ThreadList>();
            if (!p || p->renaming.empty()) return Cmd::none();
            const int end = ui::utf8_next(p->rename_input, p->rename_cursor);
            p->rename_input.erase(static_cast<std::size_t>(p->rename_cursor),
                                  static_cast<std::size_t>(end - p->rename_cursor));
            return Cmd::none();
        },
        [&](ThreadListRenameCursor& e) -> Cmd {
            auto* p = m.ui.panel.get<pn::ThreadList>();
            if (!p || p->renaming.empty()) return Cmd::none();
            using To = ThreadListRenameCursor::To;
            switch (e.to) {
                case To::Left:  p->rename_cursor = ui::utf8_prev(p->rename_input, p->rename_cursor); break;
                case To::Right: p->rename_cursor = ui::utf8_next(p->rename_input, p->rename_cursor); break;
                case To::Home:  p->rename_cursor = 0; break;
                case To::End:   p->rename_cursor = static_cast<int>(p->rename_input.size()); break;
            }
            return Cmd::none();
        },
        [&](ThreadListRenamePaste& e) -> Cmd {
            auto* p = m.ui.panel.get<pn::ThreadList>();
            if (!p || p->renaming.empty()) return Cmd::none();
            // 只有适合的才能进入,所以一个长的糊糊的中间 -title
            // 剪切粘贴, 切换后从文本中切换 光标.
            std::string add = one_line(e.text);
            const std::size_t room = persistence::kTitleMaxBytes
                                   - std::min(persistence::kTitleMaxBytes, p->rename_input.size());
            add.resize(tools::util::safe_utf8_cut(add, room));
            p->rename_input.insert(static_cast<std::size_t>(p->rename_cursor), add);
            p->rename_cursor += static_cast<int>(add.size());
            return Cmd::none();
        },
        [&](ThreadListRenameCancel) -> Cmd {
            auto* p = m.ui.panel.get<pn::ThreadList>();
            if (!p) return Cmd::none();
            p->renaming.clear();
            p->rename_input.clear();
            p->rename_cursor = 0;
            return Cmd::none();
        },
        [&](RenameCurrentThread& e) -> Cmd {
            // A 线程 里面什么都没有,不是 已保存 磁盘
            // 上没有东西可以重命名
            if (m.d.current.messages.empty())
                return set_status_toast(m, "send a message first, then /rename");
            std::string title = persistence::clean_title(e.title);
            if (title.empty())
                return set_status_toast(m, "/rename needs a title");
            return apply_rename(m, m.d.current.id, std::move(title), /*清 净 =*/false);
        },
        [&](ThreadListRenameSubmit) -> Cmd {
            auto* p = m.ui.panel.get<pn::ThreadList>();
            if (!p || p->renaming.empty()) return Cmd::none();
            const ThreadId id{p->renaming};
            std::string title = persistence::clean_title(p->rename_input);
            p->renaming.clear();
            p->rename_input.clear();
            p->rename_cursor = 0;
            // 由 id 找到: 中途降落的刷新 -edit 可能
            // 重新排序了列表,或者删除了 线程 (删除别处):那么
            // 没有东西可以重命名.
            const auto it = std::ranges::find(m.d.threads, id, &Thread::id);
            if (it == m.d.threads.end()) return Cmd::none();
            // 空草稿能清空名字,回到自动草稿。 线程 你从来没有
            // 命名 这是一个不 -op ,就像一个不变的草案:
            // 未经修改的草稿上的Enter不得花费一张磁盘写作的费用.
            const bool clear = title.empty();
            if (clear ? !it->user_named() : title == it->title) return Cmd::none();
            Cmd out = apply_rename(m, id, std::move(title), clear);
            // 重新命名的行可能不再匹配过滤器.
            if (auto* pp = m.ui.panel.get<pn::ThreadList>()) {
                const int sz = visible_count(m, *pp);
                pp->index = sz > 0 ? std::clamp(pp->index, 0, sz - 1) : 0;
            }
            return out;
        },
        [&](ThreadCycle& e) -> Cmd {
            // Alt QQ/________ 跳到邻近地区 线程 没有 选择面板.
            // 紧急命令(与 ^J :索引0 = 最新;+1 = 旧, -1 = 新,
            // 两端包装. Gated on a locate session
            // – s互换 m.d.current 活动下 数据流
            // 将拖入 -flight 克特克斯的笔记.
            if (m.s.active()) {
                auto cmd = set_status_toast(m,
                    "wait for the reply to finish before switching threads");
                return cmd;
            }
            if (m.s.thread_loading) return Cmd::none();
            const int sz = static_cast<int>(m.d.threads.size());
            if (sz == 0) {
                // 历史不是 已加载 来点新鲜的,然后...
                // 下一个 新闻工作,说吧。
                Cmd cmd = Cmd::none();
                if (!m.s.threads_loading) {
                    m.s.threads_loading = true;
                    cmd = cmd::load_threads_async();
                }
                auto toast = set_status_toast(m, "no other threads yet");
                return Cmd::batch(std::move(cmd), std::move(toast));
            }
            // 定位当前 线程。在重新定义的列表中。
            // 新的未保存 线程 里面没有,请 "newest"
            // 成为了锚 所以第一个按着最近的 已保存 线程.
            int cur = -1;
            for (int i = 0; i < sz; ++i)
                if (m.d.threads[static_cast<std::size_t>(i)].id == m.d.current.id) {
                    cur = i;
                    break;
                }
            int target;
            if (cur < 0) {
                target = (e.delta >= 0) ? 0 : sz - 1;
            } else {
                if (sz == 1) {
                    auto toast = set_status_toast(m, "only one thread");
                    return toast;
                }
                target = ((cur + e.delta) % sz + sz) % sz;
            }
            const Thread& meta = m.d.threads[static_cast<std::size_t>(target)];
            if (meta.id == m.d.current.id) return Cmd::none();
            // 保留左方的线程——同样的礼节NewThread延伸.
            // finalize_turn 保存每转弯,但标题编辑或
            // un-persisted 末尾不应该丢失到一个快速循环.
            Cmd save = m.d.current.messages.empty()
                         ? Cmd::none() : save_current_whole(m);
            m.s.thread_loading = true;
            m.s.loading_thread = meta.id;
            // "thread k/N · title" ——
            // 位置读取重复 Alt – 压力感觉像是翻过甲板,
            // 而不是在电传中盲转。 ThreadLoaded
            // 不许碰 m.s.status.
            auto toast = set_status_toast(m,
                "thread " + std::to_string(target + 1) + "/"
                    + std::to_string(sz) + " \xc2\xb7 "
                    + (meta.title.empty() ? "(untitled)" : meta.title));
            // 预温会暖和 套接字的切换 -into 线程 初
            // 转法轮.
            return Cmd::batch(std::move(save),
                              cmd::prewarm_provider(m),
                              cmd::load_thread_async(meta.id),
                              std::move(toast));
        },
        [&](NewThread) -> Cmd {
            // 坚持向外的 线程 之前,我们放弃它(删除的
            // 活跃 -row 路径则相反——它刚刚移除了 线程 ,
            // 所以它不得 保存 ),在下方共享重置,
            // 在作出这一政策决定后拥有所有东西.
            Cmd save = m.d.current.messages.empty()
                         ? Cmd::none() : save_current_whole(m);
            auto reset = reset_to_fresh_thread(m);
            return Cmd::batch(std::move(save), std::move(reset));
        },
        [&](ThreadTitleGenerated& e) -> Cmd {
            if (m.s.titling == e.id) m.s.titling = {};
            // 模特儿的回答 也是一样的清理 带类型的 名称。
            // 引用和后继的时段是通常的聊天 -model 习惯。
            std::string title = persistence::clean_title(e.title);
            while (!title.empty() && (title.front() == '"' || title.front() == '\''))
                title.erase(0, 1);
            while (!title.empty() && (title.back() == '"' || title.back() == '\''
                                      || title.back() == '.'))
                title.pop_back();
            // 失败: 保留第一个-line 名称, 并且不要再问了。
            // 否则无法回答的模型会花费每个转弯的请求。
            if (title.empty()) {
                if (m.d.current.id != e.id) return Cmd::none();
                // 试一次,永远不要,标题不变
                m.d.current.auto_titled = true;
                return save_current(m);
            }

            // 只有开着 线程。如果您在标题被制作时切换,
            // 则 线程 保持它的第一线 -line 名称,
            // 与失败的请求相同。
            if (m.d.current.id != e.id || m.d.current.user_named()) return Cmd::none();
            if (const auto it = std::ranges::find(m.d.threads, e.id, &Thread::id);
                it != m.d.threads.end()) {
                if (it->user_named()) return Cmd::none();   // 永远不要替换选中的名称
                it->title       = title;
                it->auto_titled = true;
                ++m.d.threads_rev;
            }
            m.d.current.title       = title;
            m.d.current.auto_titled = true;
            // 说:名字在您下面更改了, /重命名更改了它.
            auto toast = set_status_toast(m, "named this chat \"" + title + "\" \xc2\xb7 /rename to change");
            return Cmd::batch(save_current(m), std::move(toast));
        },
        [&](SaveRefused& e) -> Cmd {
            // 只有开放的线程有下降基础。现在发送整个,
            // 所以拒绝的三角洲携带的东西直到下一个转弯才丢失。
            if (e.id != m.d.current.id) return Cmd::none();
            m.d.saved_fps.clear();
            return save_current(m);
        },
        [&](ThreadsLoaded& e) -> Cmd {
            // 在重新命名之前可以从磁盘上读取。
            // 保留任何行中较新的重命名。
            for (auto& t : e.threads)
                if (const auto it = std::ranges::find(m.d.threads, t.id, &Thread::id);
                    it != m.d.threads.end() && it->renamed_at > t.renamed_at) {
                    t.title        = it->title;
                    t.renamed_at   = it->renamed_at;
                    t.name_is_auto = it->name_is_auto;
                }
            // 留着 选择面板 光标在 线程 重装
            // 可以在上面加排或重排行
            ThreadId was;
            if (const auto* p = m.ui.panel.get<pn::ThreadList>())
                if (const int at = picked_thread(m, *p); at >= 0)
                    was = m.d.threads[static_cast<std::size_t>(at)].id;
            // 保留回填进度: 重装不能重装 -queue a
            // 线程 已经尝试了(一个空的或不可读的)。
            for (auto& t : e.threads)
                if (t.message_count == 0 && t.preview.empty())
                    if (const auto it = std::ranges::find(m.d.threads, t.id, &Thread::id);
                        it != m.d.threads.end())
                        t.summary_tried = it->summary_tried;
            m.d.threads = std::move(e.threads);
            merge_foreign(m);
            ++m.d.threads_rev;
            m.s.threads_loading = false;
            // 旧的线程还没有预览: 填入,一次批次,
            // 现在列表已经显示。
            Cmd fill = next_summary_batch(m);
            // RE-clamp也:新名单可能较短.
            if (auto* p = m.ui.panel.get<pn::ThreadList>()) {
                const int sz = visible_count(m, *p);
                const int at = was.value.empty() ? -1 : visible_index_of(m, *p, was);
                p->index = at >= 0 ? at : (sz > 0 ? std::clamp(p->index, 0, sz - 1) : 0);
                return Cmd::batch(std::move(fill), sync_peek(m));
            }
            return fill;
        },
        [&](ForeignSessionsLoaded& e) -> Cmd {
            m.s.foreign_loading = false;
            ThreadId was;
            if (const auto* p = m.ui.panel.get<pn::ThreadList>())
                if (const int at = picked_thread(m, *p); at >= 0)
                    was = m.d.threads[static_cast<std::size_t>(at)].id;
            m.d.foreign = std::move(e.scan);
            merge_foreign(m);
            ++m.d.threads_rev;
            if (auto* p = m.ui.panel.get<pn::ThreadList>()) {
                const int sz = visible_count(m, *p);
                const int at = was.value.empty() ? -1 : visible_index_of(m, *p, was);
                p->index = at >= 0 ? at : (sz > 0 ? std::clamp(p->index, 0, sz - 1) : 0);
                return sync_peek(m);
            }
            return Cmd::none();
        },
        [&](ThreadSummariesFilled& e) -> Cmd {
            m.s.summaries_filling = false;
            for (auto& f : e.threads)
                if (const auto it = std::ranges::find(m.d.threads, f.id, &Thread::id);
                    it != m.d.threads.end()) {
                    it->preview       = std::move(f.preview);
                    it->message_count = f.message_count;
                    if (it->workspace.empty()) it->workspace = std::move(f.workspace);
                }
            if (!e.threads.empty()) ++m.d.threads_rev;
            return next_summary_batch(m);
        },
        [&](ThreadLoaded& e) -> Cmd {
            // 合成单曲的结果 -thread 加载 被踢下来的
            // ThreadListSelect 空的,将打开 线程 (默认)
            // ThreadId )指读取的磁盘,或 解析 失败;
            // 只需清除 加载动画 离开当前 线程 已经就位了
            // 20世纪80年代的待处理 加载表示它被取代(^N):放弃它.
            if (m.s.loading_thread.value.empty()) return Cmd::none();
            const bool wanted = e.thread.id == m.s.loading_thread;
            m.s.loading_thread = {};
            m.s.thread_loading = false;
            if (!wanted || e.thread.id.value.empty()) return Cmd::none();
            // 聪明 -Mode 百分比 -thread 路由状态属于离
            // 队 线程 也——与重置时相同 reset_to_fresh_thread
            // (运动、级联偏差、结果) -feedback 签名。
            // 没有签名, 已加载 线程 首转继承 旧 线程
            // 层次动力及其第一个后续 -up 训练旧的签名。
            m.s.smart_turn_complexity  = smart::Complexity::Standard;
            m.s.smart_effort_bias      = 0;
            // 同步部分的时序探测器 加载 (水合物+ )
            // release_to_kernel 仍然生活在 生活于 UI 线程.
            // AGENTTY_LOG 脱衣舞娘会露面的
            //
            // A STOPWATCH :它读了两次真实的钟表 时间工作在这个
            // 内部 归并 ,该 归并 '是单瞬间( 3) m.now )不能办.
            // 日志 -only ; 从未到达 Model ,所以重播不会受到影响。
            auto stamp = [](const char* tag, auto t0) {
                auto dt = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - t0).count();
                AGT_LOG(Perf, Debug, "thread.load", "stage={} ms={:.2f}",
                        tag, dt);
            };
            m.d.current = std::move(e.thread);
            m.d.saved_fps.clear();
            // 开幕会议是 已保存 agentty 线程 从这里开始:
            // 选择面板 排不再是外国占位符,扫描的复制件会跟随其大小,
            // 所以下一个打开时不会重新打开 -import.
            if (m.d.current.imported) {
                Thread h = persistence::thread_header(m.d.current);
                if (auto it = std::ranges::find(m.d.threads, m.d.current.id, &Thread::id);
                    it != m.d.threads.end())
                    *it = std::move(h);
                else
                    m.d.threads.insert(m.d.threads.begin(), std::move(h));
                for (auto& s : m.d.foreign.sessions)
                    if (s.id == m.d.current.id) s.imported = m.d.current.imported;
                ++m.d.threads_rev;
            }
            // 重命名被作者存储, 可能仍然忙于早期的 保存 ,
            // 所以一个 加载 之后,您就可以从磁盘
            // 上读取旧标题。保留最近重新命名的标题。
            if (const auto it = std::ranges::find(m.d.threads, m.d.current.id, &Thread::id);
                it != m.d.threads.end() && !it->title.empty()
                && it->renamed_at > m.d.current.renamed_at) {
                m.d.current.title        = it->title;
                m.d.current.renamed_at   = it->renamed_at;
                m.d.current.name_is_auto = it->name_is_auto;
            }
            m.s.reset_usage_for_thread(last_reported_prompt(m.d.current));
            // 已加载XX线程号没有直播转弯,所以任何运行
            // 状态仍然处于我们刚刚离开的线程号位置。
            // 放下它不是内部管理,而是崩溃修复。
            //
            // 切换中 线程 中点 -stream (无) 加载 即是同声,所以 传输
            // 仍然可以运行)左 `phase` 无 -Idle 财务报告和已审计
            // 财务报表 `pending_permission` 设定,两者都指向已经
            // 不存在的文字记录。 新 线程 寻找旧的留言 线程's ToolUse 编号,
            // 和英寸 -flight 读取 active_ctx (为消息消失
            // 的跑步) NewThread 一直通过 reset_to_fresh_thread
            // (见A/C.5/61/L.48,第16段)
            // "a fresh empty thread has no live turn");
            // ThreadLoaded 情况是一样的,只是错过了
            //
            // 顺序事项: 将内核交回 之前 记录稿被补水了 晚
            // 三角洲无法降落在半个 -swapped 模型 模型
            // 模式
            end_turn(m.s.phase);
            m.d.pending_permission.reset();
            // 模具设计 旧 线程 '内容(调色板和代码)
            // -block 选择面板 将指数保存在离线时 线
            // 程 上次的回复,所以他们会输入错误的笔录。
            m.ui.panel.close<pn::Palette>();
            m.ui.panel.close<pn::CodeBlocks>();
            m.ui.panel.close<pn::CodeBlockResult>();
            // 整个放下 渲染 缓存 - 理由与 NewThread:
            // 属于 线程 被左转,不会再次冻僵 已加载 线
            // 程 重建其冻结的前缀通过 rehydrate_frozen
            // 下层和重新分布 缓存 懒惰的。
            m.ui.view_cache.clear();
            // 删去作曲草稿——与 NewThread :
            // 贴上 -but-unsent 图像 / 标签
            // / 队列中的消息属于 线程 左侧,
            // 剩余图像附件为空 字节 (排入前一个信件),
            // 它序列化一个空图像块,并有400s的下一个提交.
            reset_composer_draft(m.ui.composer);
            auto t1 = std::chrono::steady_clock::now();
            rehydrate_frozen(m);
            stamp("rehydrate_frozen", t1);
            // 冻 回滚缓冲区 刚从寒冷中建成的, 第一次 render()
            // 否则会支付满额布局+油漆, 覆盖每个冰冻的转弯。
            // 翻转暖气 标志 这么说吧 maya 运行循环预览 -warms
            // 构成部分 缓存 之前, 传输-bound 渲染 -
            // 看见了 Program::needs_warmup 钩子
            m.ui.needs_warmup_render = !m.ui.frozen.empty();
            // 举起手来 -shot 员额 -paint 修剪: 使用的水合水
            // 预算 ESTIMATED 高度;第一种油漆将真实的油漆记录在分类账中,
            // 而Tick手臂则重新记录 -trims 和那些对齐,
            // 票 事件订阅 关卡上 标志 (subscribe.cpp 直到它燃烧。
            m.ui.pending_rehydrate_trim = !m.ui.frozen.empty();
            auto t2 = std::chrono::steady_clock::now();
            release_to_kernel();
            stamp("release_to_kernel", t2);
            AGT_LOG(Perf, Debug, "thread.load_done",
                    "msgs={} frozen={} frozen_rows={} "
                    "frozen_through={} term_h={}",
                    m.d.current.messages.size(),
                    m.ui.frozen.size(),
                    m.ui.frozen.row_total(),
                    m.ui.frozen_through,
                    m.ui.term_rows);
            // 批发模式转换为已加载 线程.
            // 与上文NewThread相同的理由:前线程的
            // 溢出行承诺为原生的回滚缓冲区,
            // 只有reset_inline(其喷出
            // `\x1b[2J\x1b[3J\x1b[H`)可以抹去它们,
            // 没有它,前线程的末尾转弯可以看到在补水的线程的第一转弯之上.
            //
            // 人数 maya/device.hpp reset_inline
            // () 证明文件:这是对以下物品的经批准追回: 线程 切换/
            // 新建 线程。该词 `\x1b[3J` 成本( 擦除
            // 用户的预 -agentty 外壳 回滚缓冲区 )可以接受,
            // 因为用户明确要求内容交换( 选择面板 选择).
            return cmd::reset_inline();
        },
    }, tm);
}

} // namespace agentty::app::detail
