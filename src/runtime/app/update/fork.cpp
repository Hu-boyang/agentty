// fork_update-状态更新函数用于叉子选择面板。
//
// 将 ESCAPES 分叉为完整上下文窗口。它将当前的 线程 分支为品牌
// -new 品牌（新 id，forked_from = 父级），该品牌以 FRESH 开头，
// 并带有近 -zero 上下文 — 不是父级的副本或摘要。父级的完整转录本
// 将导出到 ~/.agentty/threads 下的 Markdown
// 文件中，并且分支会使用单个合成 `fork_note` 消息 (
// Role::User) 进行播种，该消息：
//   ·告诉MODEL该抄本所在的位置，以读取ON DEMAND
//     仅当实际需要较早的上下文时才使用`read`工具；
//   ·渲染作为可见的"\u2443 Forked" 事件卡，因此新鲜的线程是
//     不是空白屏幕。
// 这是整个分叉机制：没有摘要、没有逐字复制、
// 没有 up-front 令牌成本——只是代理可以懒惰地遵循的指针。
//
// 选择面板的唯一选择是RAG在叉中如何主动表现，存储为叉的per
// -thread rag_mode_override：
//
//   每转 RAG → 超驰 = 打开 首先
//   -turn RAG → 超驰 = FirstTurnOnly
//   RAG 关闭 → 超驰 = 关闭
//
// 原始的 线程 是 已保存 并且完全保持不变——叉子在结构上不是
// -destructive。

#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/app/cmd_factory.hpp"

#include <chrono>
#include <utility>

#include <maya/core/overload.hpp>

#include "agentty/runtime/panel/fork.hpp"
#include "agentty/runtime/panel/common.hpp"
#include "agentty/runtime/panel/palette.hpp"
#include "agentty/store/store.hpp"
#include "agentty/io/persistence.hpp"
#include "agentty/tool/skills.hpp"
#include "agentty/provider/selection.hpp"   // prewarm_active_provider

namespace pn = agentty::ui::panel;

namespace agentty::app::detail {

namespace fp   = agentty::fork_panel;
namespace pick = agentty::ui::pick;
using maya::overload;

namespace {

// 将 选择面板 选择映射到分叉的 per-thread RAG 覆盖。
store::RagMode rag_mode_of(fp::Choice c) {
    switch (c) {
        case fp::Choice::RagPerTurn:   return store::RagMode::On;
        case fp::Choice::FirstTurnRag: return store::RagMode::FirstTurnOnly;
        case fp::Choice::RagOff:       return store::RagMode::Off;
    }
    return store::RagMode::Off;
}

const char* label_of(fp::Choice c) {
    switch (c) {
        case fp::Choice::RagPerTurn:   return "RAG per turn";
        case fp::Choice::FirstTurnRag: return "first-turn RAG";
        case fp::Choice::RagOff:       return "RAG off";
    }
    return "";
}

} // 命名空间结束。

Cmd fork_update(Model& m, msg::ForkMsg fm) {
    return std::visit(overload{
        [&](OpenFork) -> Cmd {
            if (m.d.current.messages.empty())
                return set_status_toast(m, "nothing to fork yet");
            if (!m.s.is_idle() || m.s.compacting || m.s.thread_loading)
                return set_status_toast(m, "cannot fork while the agent is working");
            // descend() 已经替换了打开的任何内容（调色板，
            // 如果 ^F 来自那里）并将其隐藏为该面板的父面板，
            // 因此 Esc 展开回到它。这里曾经有一个 close
            // <Palette>()，是打开时留下的一个赋值：现在
            // 它是一个 no-op （插槽容纳 Fork），如果它不再是一个，
            // 它将删除我们只是 已保存 的父项。
            m.ui.panel.descend(pn::Fork{{fp::Choice::RagPerTurn}});
            return Cmd::none();
        },
        [&](CloseFork) -> Cmd {
            ascend(m);   // Esc：返回打开此的调色板，或关闭
            return Cmd::none();
        },
        [&](ForkMove& e) -> Cmd {
            if (auto* o = m.ui.panel.get<pn::Fork>())
                o->choice = fp::next_choice(o->choice, e.delta);
            return Cmd::none();
        },
        [&](ForkThread&) -> Cmd {
            const auto* picked = m.ui.panel.get<pn::Fork>();
            // 无 int->枚举 转换：游标已经 IS 选择，
            // 因此重新排序或调整大小的行列表无法静默地重新映射它。
            const fp::Choice choice = picked ? picked->choice
                                             : fp::Choice::RagOff;
            m.ui.panel.close<pn::Fork>();
            if (m.d.current.messages.empty())
                return set_status_toast(m, "nothing to fork yet");
            if (!m.s.is_idle() || m.s.compacting || m.s.thread_loading)
                return set_status_toast(m, "cannot fork while the agent is working");

            // 1.保持父级不变，并且2.写入其转录本
            //    为了干净、可读的文件，可以叉出READ ON DEMAND。
            //    这就是分叉的全部要点：新的 线程 启动 空，
            //    并且模型仅在需要时才从磁盘中提取较早的上下文。
            //    写入在 工作线程 上运行；当ForkTranscriptWritten回来时，
            //    叉子就建好了。
            return Cmd::batch(save_current_whole(m),
                              cmd::write_fork_transcript(m.d.current, choice));
        },
        [&](ForkTranscriptWritten& e) -> Cmd {
            // 用户可能在写入抄本时已经继续前进（切换线程、
            // 开始转弯、分叉两次）。仅在仍然安全
            // 的情况下分叉所请求的 线程。
            if (m.d.current.id.value != e.parent_id) return Cmd::none();
            if (m.d.current.messages.empty()) return Cmd::none();
            if (!m.s.is_idle() || m.s.compacting || m.s.thread_loading)
                return Cmd::none();
            const fp::Choice choice = e.choice;
            const std::string& parent_id = e.parent_id;
            const std::string& transcript = e.path;

            // 3.构建分叉：FRESH、空、线程，其出处+
            //    每-thread RAG覆盖。没有任何消息被继承。
            Thread fork;
            fork.id = persistence::new_id();
            fork.forked_from = parent_id;
            fork.workspace = m.d.current.workspace.empty() ? m.env.cwd
                                                           : m.d.current.workspace;
            fork.rag_mode_override = rag_mode_of(choice);
            fork.created_at = fork.updated_at = m.wall_now();
            fork.title = m.d.current.title.rfind("Fork: ", 0) == 0
                             ? m.d.current.title
                             : ("Fork: " + (m.d.current.title.empty()
                                                ? std::string{"conversation"}
                                                : m.d.current.title));
            // 分叉以其父代命名； auto-titling 会将
            // 其替换为由前叉音符组成的名称。离开了。
            fork.auto_title_ok = false;

            // 4.播种分叉注释：一条合成的 Role::User 消息，
            //    (a)告诉MODEL先前的上下文存在以及如何到达它，
            //    以及(b)渲染作为可见的"\u2443 Forked"卡，
            //    因此新的线程不是空白屏幕。这是故意的 USER（不是系统）
            //    消息：ChatGPT/Responses 传输层 DROPS
            //    mid-thread 系统消息（归并 它们进入 top
            //    -level `instructions`），因此系统注释可能会
            //    默默消失，并且模型永远不会知道转录本的存在。用户消息在每个
            //    提供者路径中都存在。 `fork_note`/
            //    `fork_transcript` 标志 赋予其安静的卡身份，
            //    并防止每次 "newest real user turn" 扫描
            //    （路由/RAG/历史记录/loop-break）将其误认为是用户的
            //    提示词 — 请参阅 conversation.hpp。
            {
                Message note;
                note.role = Role::User;
                note.fork_note = true;
                if (!transcript.empty()) {
                    note.fork_transcript = transcript;
                    note.text =
                        "This conversation is a fork of an earlier one. Its "
                        "full transcript is saved at:\n  " + transcript +
                        "\nRead it with the `read` tool (or grep it) ONLY if "
                        "you need earlier context — don't read it "
                        "pre-emptively. The fork starts fresh precisely to "
                        "reclaim the context window; pull just the slice you "
                        "need.";
                } else {
                    // 成绩单写入失败（罕见：disk/permissions）。
                    // 仍然给卡片播种，这样 线程 就不是空白，
                    // 并且模型知道它是叉子，只是没有可读的指针。
                    note.text =
                        "This conversation is a fork of an earlier one, "
                        "started fresh to reclaim the context window. The "
                        "prior transcript could not be exported to disk, so "
                        "earlier context isn't retrievable — ask the user if "
                        "you need it.";
                }
                fork.messages.push_back(std::move(note));
            }

            // 5.切换到叉子——一个新鲜的空线程，所以渲染是
            //    相当便宜（没什么可re-emit）。 rehydrate_frozen 在
            //    一个空的 线程 上是一个 no-op； reset_inline 描绘了
            //    干净的作曲家。
            m.ui.view_cache.clear();
            m.d.current = std::move(fork);
            m.d.saved_fps.clear();
            m.s.reset_usage_for_thread(last_reported_prompt(m.d.current));
            Cmd save_fork = save_current(m);
            m.ui.panel.close<pn::ThreadList>();
            rehydrate_frozen(m);
            m.ui.needs_warmup_render = !m.ui.frozen.empty();

            auto toast = set_status_toast(
                m, std::string{"forked \xc2\xb7 fresh context · "} +
                       label_of(choice) +
                       " · prior transcript readable on demand",
                std::chrono::seconds{5});
            // 分叉的第一轮新鲜地冲击网络，并且空闲的TTL通常
            // 已经驱逐了launch-time套接字——如此温暖，
            // 作为副作用与保存并排。
            return Cmd::batch(std::move(save_fork),
                              cmd::prewarm_provider(m),
                              std::move(toast), cmd::reset_inline());
        },
    }, fm);
}

} // namespace agentty::app::detail
