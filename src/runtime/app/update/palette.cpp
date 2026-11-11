// palette_update+todo_update-状态更新函数用于命令调色板
// 和待办事项弹出面板。两者都是简单的list-modals；调色板是 分派器
// 用于操作命令（NewThread、ReviewChanges 等），因此它 re
// -enters top-level update() 将 Command::* 扇入
// 匹配域 Msg。

#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/app/cmd_factory.hpp"
#include "agentty/runtime/view/palette.hpp"   // ui::palette_context

#include <algorithm>
#include <utility>

#include <maya/core/overload.hpp>

#include "agentty/runtime/panel/common.hpp"
#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/panel/code_blocks.hpp"
#include "agentty/runtime/view/helpers.hpp"

namespace pn = agentty::ui::panel;

namespace agentty::app::detail {

namespace pick = agentty::ui::pick;
using maya::overload;

// 从当前的Model构建调色板的实时可见性上下文，因此有条件地-dead行（
// 没有差异的Accept-all，没有屏蔽回复的Run-code-block，
// 没有释放的更新）永远不会渲染。一个地方，由这个 状态更新函数 + 视图中的每
// 个 filtered_commands() 调用 进行咨询。
// 定义在 meta.cpp，并在 internal.hpp 声明，供视图共享。

// ── 命令分派驱动程序 ───────────────────────────────────────────────
// 每个调色板命令在一处声明ITS OWN行为：下面的注册表是{命令，处理函数}的平面表。添加命令是这里
// 的一行（加上 command_palette.hpp 中的元数据行）——没有增长的开关，
// 没有 four-file 编辑。 The vast majority just re-enter the
// 状态更新函数 with a Msg, so `emit<T>()` builds that 处理函数
// generically; the few commands with bespoke 副作用 (
// 学习重置和自更新等操作各自使用具名 lambda。
//
// A 处理函数 is Model-in → Step-out; the driver has already closed the palette
// before calling it, so 处理函数 only describe the action.
using CommandHandler = std::function<Cmd(Model&)>;

namespace {
// emit<T>: the 处理函数 for a command that simply dispatches Msg{T
// {}} back through the top-level 状态更新函数.覆盖约 20 行。
//
// Still goes through the whole-Msg 状态更新函数 rather than a per-domain one:
// 命令跨越每个域，并且手动将每个域路由到其自己的状态更新函数正是该注册表存在要避免的分派表。
// 它返回的二元组在这里解包，
// so the 处理函数 themselves are already in jaal's
// shape.
template <class T>
[[nodiscard]] CommandHandler emit() {
    return [](Model& m) {
        auto [next, cmd] = agentty::app::update(std::move(m), Msg{T{}});
        m = std::move(next);
        return std::move(cmd);
    };
}
// emit_val<T>(v): same, for a Msg that carries a 载荷 (settings category).
template <class T, class V>
[[nodiscard]] CommandHandler emit_val(V v) {
    return [v](Model& m) {
        auto [next, cmd] = agentty::app::update(std::move(m), Msg{T{v}});
        m = std::move(next);
        return std::move(cmd);
    };
}
// Esc链上的NOTE：调色板快照下方的选择臂和`adopt()`
// 将其放置到命令打开的WHATEVER上——通常在分派之后。
// 旧的 emit_settings<> 包装器（为每个设置命令提供
// -stamped 一个源）已经消失：命令不需要 per-caller 知识，
// 并且每个调色板 -opened 覆盖面板 现在都展开回调色板。
} // 命名空间结束。

// 注册表：命令→它的作用。排序是为了可读性，而不是查找（驱动
// 程序会线性查找超过 25 个条目 - 非常便宜，
// 并且保持表声明性）。 枚举 中每个命令一个条目；缺少的条目是
// no-op（由驱动程序处理），因此构建不能默默地错误-wire。
[[nodiscard]] const std::vector<std::pair<Command, CommandHandler>>& command_registry() {
    static const std::vector<std::pair<Command, CommandHandler>> reg = [] {
        std::vector<std::pair<Command, CommandHandler>> r;
        r.reserve(25);
        auto add = [&](Command c, CommandHandler h) { r.emplace_back(c, std::move(h)); };

        // ── 线程 ──
        add(Command::NewThread,        emit<NewThread>());
        add(Command::ForkThread,       emit<OpenFork>());
        add(Command::CompactContext,   emit<CompactContext>());
        add(Command::RewindCheckpoint, emit<OpenCheckpoints>());
        // ── 变动 ──
        add(Command::ReviewChanges,    emit<OpenDiffReview>());
        add(Command::AcceptAll,        emit<AcceptAllChanges>());
        // 从调色板中拒绝-all已经是显式的multi-step动作
        // (打开调色板→选择行→Enter)，因此它跳过和弦的two
        // -press防护并在此拨片上执行。意图通过消息传播；
        // 这里没有 状态更新函数 状态的 pre-staging。
        add(Command::RejectAll,        emit_val<RejectAllChanges>(/*确认=*/true));
        // ── 去吧,我们走 ──
        add(Command::OpenThreads,      emit<OpenThreadList>());
        add(Command::OpenPlan,         emit<OpenTodoModal>());
        add(Command::InspectToolOutputs, emit<OpenToolOutput>());
        add(Command::RunCodeBlock,     emit<OpenCodeBlocks>());
        // ── 配置 ──
        add(Command::CycleProfile,     emit<CycleProfile>());
        add(Command::OpenModels,       emit<OpenModels>());
        add(Command::SwapModel,        emit<SwitchToPreviousModel>());
        add(Command::OpenProviders,    emit<OpenProviders>());
        add(Command::OpenStats,        emit<OpenStats>());
        add(Command::OpenSkills,       emit<OpenSkills>());
        add(Command::OpenGeneralSettings,
            emit_val<OpenSettingsList>(settings::Category::General));
        // ── 账户 ──
        add(Command::OpenLogin,        emit<OpenLogin>());
        // ── 常规 ──
        add(Command::UpdateAgentty, [](Model& m) -> Cmd {
            if (m.s.update_latest.empty() || m.s.update_in_flight)
                return Cmd::none();
            m.s.update_in_flight = true;
            std::string v = m.s.update_latest;
            m.s.status = "\xe2\xac\x86 downloading agentty v" + v + "\xe2\x80\xa6";
            m.s.status_until = {};
            return cmd::perform_self_update(std::move(v));
        });
        add(Command::Quit,             emit<Quit>());
        return r;
    }();
    return reg;
}

// 运行光标所在的命令。调色板已被调用者关闭；
// 未知命令（没有 注册表 条目）是安全的 no-op。
[[nodiscard]] Cmd dispatch_command(Command sel, Model& m) {
    for (const auto& [id, handler] : command_registry())
        if (id == sel) return handler(m);
    return Cmd::none();
}

Cmd palette_update(Model& m, msg::PaletteMsg pm) {
    return std::visit(overload{
        [&](OpenPalette) -> Cmd {
            m.ui.panel.descend(pn::Palette{});
            return Cmd::none();
        },
        [&](ClosePalette) -> Cmd {
            // Esc展开一个级别——在另一个面板上打开的调
            // 色板（罕见但可以通过和弦）恢复它；超过 线程，
            // 关闭。
            ascend(m);
            return Cmd::none();
        },
        [&](PanelFilterPaste& e) -> Cmd {
            // 每个过滤器面板的ONE臂：通过打开面板的OWN 带类型
            // 的-input消息重播粘贴，一次一个字符——因此粘贴
            // 具有精确的打字语义（ASCII门、光标重置、
            // 模型面板的re-rank)，没有重复的逻辑，也没有交叉-TU耦合。
            // Control 字符被放置在此处，因此 multi
            // -line 剪贴板无法将换行符偷偷放入过滤器中。
            // 受剪贴板尺寸限制；每一步都是便宜的带类型的路径。
            Cmd out = Cmd::none();
            for (char c : e.text) {
                const auto u = static_cast<unsigned char>(c);
                if (u < 0x20 || u >= 0x7f) continue;
                const auto ch = static_cast<char32_t>(u);
                Msg per_char =
                    m.ui.panel.is<pn::Palette>()   ? Msg{PaletteInput{ch}}
                  : m.ui.panel.is<pn::Models>()    ? Msg{ModelsFilterInput{ch}}
                  : m.ui.panel.is<pn::Providers>() ? Msg{ProvidersFilterInput{ch}}
                  : m.ui.panel.is<pn::Mention>()   ? Msg{MentionInput{ch}}
                  : m.ui.panel.is<pn::Symbol>()    ? Msg{SymbolInput{ch}}
                  : m.ui.panel.is<pn::ThreadList>() ? Msg{ThreadListFilterInput{ch}}
                  : m.ui.panel.is<pn::SettingsList>() ? Msg{SettingsSearchInput{ch}}
                                                   : Msg{NoOp{}};
                auto [next, cmd] = agentty::app::update(std::move(m),
                                                        std::move(per_char));
                m = std::move(next);
                // 最后的写入者获胜，如前所述：带类型的路径的
                // Cmd是状态提示，并且只有最后的击键是当前的。
                out = std::move(cmd);
            }
            return out;
        },
        [&](PaletteInput& e) -> Cmd {
            auto* o = m.ui.panel.get<pn::Palette>();
            if (o && static_cast<uint32_t>(e.ch) < 0x80) {
                o->query.push_back(static_cast<char>(e.ch));
                // 将光标重置到（新过滤的）列表的顶部，
                // 以便先前的索引不指向 now-hidden 行。
                o->index = 0;
            }
            return Cmd::none();
        },
        [&](PaletteBackspace) -> Cmd {
            auto* o = m.ui.panel.get<pn::Palette>();
            if (o && !o->query.empty()) {
                o->query.pop_back();
                o->index = 0;
            }
            return Cmd::none();
        },
        [&](PaletteMove& e) -> Cmd {
            auto* o = m.ui.panel.get<pn::Palette>();
            if (!o) return Cmd::none();
            // 钳制*可见*行计数，而不是kCommands.size()。
            // 如果没有上限，
            // 用于离开-screen和Enter的光标将静静地落到no-match路径。
            int sz = static_cast<int>(filtered_commands(
                o->query, ui::palette_context(m)).size());
            if (sz <= 0) { o->index = 0; return Cmd::none(); }
            o->index = ui::step(o->index, e.delta, sz);
            return Cmd::none();
        },
        [&](PaletteSelect) -> Cmd {
            auto* o = m.ui.panel.get<pn::Palette>();
            if (!o) return Cmd::none();
            // 解析光标→带类型的命令通过SAME过滤列表
            // 呈现视图。之前的设计针对未过滤的 枚举 打开
            // 了原始 o->index，每当任何查询处于
            // 活动状态时都会默默地触发错误的命令。
            auto matches = filtered_commands(o->query, ui::palette_context(m));
            // 复制出BEFORE结束：`o`指向变体，
            // 并且close()破坏该替代（之后旧代码通过
            // 悬空指针读取o->index）。
            const int idx = o->index;
            if (matches.empty()
                || idx < 0
                || idx >= static_cast<int>(matches.size())) {
                m.ui.panel.close<pn::Palette>();
                return Cmd::none();
            }
            const Command sel = matches[static_cast<std::size_t>(idx)]->id;
            // 快照调色板——查询、光标、其自己的父链——然后分派并让
            // 打开的命令WHATEVER采用它作为其Esc目标。
            // 通用：注册表 不需要 per-command 原始管道，
            // 并且不打开任何内容的命令会留下 None 插槽，
            // 其中 adopt() 是 no-op。
            auto parent = pn::From::of(pn::Snapshot{m.ui.panel.raw()});
            m.ui.panel.close<pn::Palette>();
            // 行为存在于命令注册表 (dispatch_command)中，
            // 而不是内联开关——一个声明表，没有漂移，
            // 并且添加命令永远不会触及该臂。
            auto cmd = dispatch_command(sel, m);
            m.ui.panel.adopt(std::move(parent));
            return cmd;
        },
    }, pm);
}

Cmd todo_update(Model& m, msg::TodoMsg tm) {
    return std::visit(overload{
        [&](OpenTodoModal) -> Cmd {
            m.ui.todo.open = pick::OpenModal{};
            return Cmd::none();
        },
        [&](CloseTodoModal) -> Cmd {
            m.ui.todo.open = pick::Closed{};
            return Cmd::none();
        },
        // （没有 UpdateTodos 臂：特工的待办事项通过
        // stream_preview 的直接同步写入陆地 — 请参阅那里的 sync_todos。
        // 无人发送的消息。）
    }, tm);
}

} // namespace agentty::app::detail
