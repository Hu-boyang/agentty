#pragma once
// agentty::Cmd——副作用行，声明一次。
//
// jaal放置程序的副作用 IN ITS TYPE (D2)：
// `Cmd<Msg, a, b, c>`表示该程序可能返回哪个副作用，
// 并且主机必须处理其中的每一个，否则程序不会针对它编译。
// 这就是使 ACP 主机易于处理的属性 - 编译器 列出了它仍然欠下的内容，
// 而不是我们在 运行时 上查找。
//
// 因此，该行位于此处，在一个地方，其他所有内容均表示 `Cmd`。
//
// 三组：
//
//   jaal核心退出、发送、之后、任务、现在、随机——始终可用，
//                 无需命名它们（D6：核心行始终在）。
//   maya、终端、set_title、commit_scrollback、
//                 write_clipboard、暂停、……——只有 TERMINAL
//                 才能做的事情。非 -terminal 主机 (ACP) 不会为这些服务提供服务，
//                 这正是列出它们的原因：编译器 会这么说。
//   agentty自己的save_settings、save_thread……——只有THIS应用程序
//                 才能做的事情。在拆除 Deps 接口边界 时添加（docs/design/jaal-rewrite.md
//                 中的步骤 7）。

#include <maya/runtime.hpp>
#include <maya/host/effects.hpp>
#include <maya/host/sources.hpp>   // maya::KeyEvent/PasteEvent/...

#include <string>
#include <string_view>

#include "agentty/domain/conversation.hpp"   // ImageContent
#include "agentty/domain/id.hpp"
#include "agentty/util/sendable.hpp"       // Id<Tag>、fs::path、EffectSet
#include "agentty/util/io.hpp"
#include "agentty/io/http.hpp"               // http::CancelTokenPtr
#include "agentty/provider/selection.hpp"    // provider::Selection
#include "agentty/runtime/msg.hpp"
#include "agentty/runtime/store_fx.hpp"   // save_thread、write_file、……

// ── agentty的值类型,如jaal所见 ──────────────────────────────
// Id<Tag>、fs::path和EffectSet由util/sendable.hpp选择加入，
// 任何受保护的值都可以包括这些。下面的需要更重的 头文件，
// 因此它们与携带它们的 Msg 源码树 一起生活。

// LazyBytes和ImageContent被选择加入到ImageContent
// (domain/conversation.hpp)旁边，因此I/O层也可以将它们排队。


// 在类型(io/http.hpp)旁边选择http::CancelTokenPtr。

// 在其定义(selection.hpp)旁边选择provider::Selection。

// ── 消息( I) 源码树 ,作为 jaal 路经它 ───────────────────────────────
// Msg是24个DOMAIN 变体的变体，不是一个扁平的叶子列表。
// agentty将
// 它们分组，因为平坦的Msg将尺寸(Msg)固定到最重的叶子上，并且在接触叶子后
// 花费约19秒来重建。 jaal 的 D36 就是这种形状，在此应用程序上测量：一个
// 域 TU 在 1.19 秒内重建，循环 TU 花了 6.9 秒 -> 1.77 秒。
//
// jaal自身沿源码树下降，因此默认情况下它想要每个LEAF有一个update()，
// 并且如此说，每个丢失病例及其所采取的路径（Msg -> AppearanceMsg
// -> AppearanceThemeCommit）有一个诊断。我们不希望这样：
// 每个域具有ONE 状态更新函数，其获取整个域变体并在其自己的TU中访问它本身。
// 这就是 handled_as_group 所声明的内容。
//
// 它选择BY NAME而不是被推断，并且D36解释了原因：catch
// 所有 template <class M> update(Model&, M) 重载。
// 像叶子一样愉快地匹配组类型，因此推断它会让catch
// -all默默地声明每个域并关闭下面的穷举性检查。
// 命名每个叶子可以检查我们 HAVEN'T 分组的每片叶子。
#define AGENTTY_MSG_GROUP(T) \
    template <> inline constexpr bool maya::handled_as_group<::agentty::msg::T> = true;

AGENTTY_MSG_GROUP(ComposerMsg)
AGENTTY_MSG_GROUP(StreamMsg)
AGENTTY_MSG_GROUP(ToolMsg)
AGENTTY_MSG_GROUP(ToolOutputMsg)
AGENTTY_MSG_GROUP(ProvidersMsg)
AGENTTY_MSG_GROUP(ModelsMsg)
AGENTTY_MSG_GROUP(ThreadListMsg)
AGENTTY_MSG_GROUP(PaletteMsg)
AGENTTY_MSG_GROUP(MentionMsg)
AGENTTY_MSG_GROUP(SymbolMsg)
AGENTTY_MSG_GROUP(CodeBlockMsg)
AGENTTY_MSG_GROUP(CheckpointMsg)
AGENTTY_MSG_GROUP(RagMsg)
AGENTTY_MSG_GROUP(StatsMsg)
AGENTTY_MSG_GROUP(SettingsListMsg)
AGENTTY_MSG_GROUP(ForkMsg)
AGENTTY_MSG_GROUP(TodoMsg)
AGENTTY_MSG_GROUP(LoginMsg)
AGENTTY_MSG_GROUP(DiffReviewMsg)
AGENTTY_MSG_GROUP(SmartModeMsg)
AGENTTY_MSG_GROUP(WebSearchMsg)
AGENTTY_MSG_GROUP(PasteSettingsMsg)
AGENTTY_MSG_GROUP(PluginEditMsg)
AGENTTY_MSG_GROUP(AppearanceMsg)
AGENTTY_MSG_GROUP(SandboxMsg)
AGENTTY_MSG_GROUP(MetaMsg)

#undef AGENTTY_MSG_GROUP

// 每个top-level域必须是声明的组。漏掉的仍然是 编译
// （jaal 下降，每片叶子都转换回域 处理函数），
// 这就是 SandboxMsg 在未被注意到的情况下未注册的原因。
namespace agentty::msg_groups {
template <class V> struct all_domains_grouped;
template <class... Ds> struct all_domains_grouped<std::variant<Ds...>> {
    static constexpr bool value = (maya::handled_as_group<Ds> && ...);
};
static_assert(all_domains_grouped<::agentty::MsgBase>::value,
              "a Msg domain is missing its AGENTTY_MSG_GROUP line above");
} // namespace agentty::msg_groups

namespace agentty {

/// 每副作用，agentty 状态更新函数可能返回。
///
/// 添加副作用时添加到该行；无法运行的主机停止编译，
/// `require_host_for` 命名 副作用 和程序。
using Cmd = maya::Cmd<Msg,
    // ── maya's terminal effects ──────────────────────────────────
    maya::commit_scrollback,   // 将内联行交给终端的回滚缓冲区
    maya::write_clipboard,     // OSC 52 写入
    maya::query_clipboard,     // OSC 52 读；回复以粘贴形式到达
    maya::emit_host_sequence,  // 形成的控制序列（编辑器挂钩）
    maya::reset_inline,        // 删除内联帧，在其下方重新开始
    maya::force_redraw,        // 重绘从头开始
    maya::set_mouse,           // 鼠标报告on/off
    maya::suspend,             // 将 tty 交给孩子（编辑器、寻呼机）
    // ── agentty的自有:持久化 ───────────────────────────────
    // 这些是调用到Deps 接口边界，这使得每个状态更新函数和已保存都是不纯的。
    // 作为 副作用，它们是 状态更新函数 返回且主机运行的值 — 请参阅
    // runtime/store_fx.hpp.
    save_thread,
    delete_thread,
    rename_thread,
    write_file,
    save_settings,
    // ── agentty 自己的:出版 Model-derived 状态为 工作线程 线程 ──
    // 由 分派 接口边界 返回，从未由 状态更新函数 返回 — 参见 Model::
    // published_subagent解释为什么同步不是状态更新函数执行的步骤。
    publish_subagent,
    publish_selection,
    publish_web_search_policy,
    publish_paste_config,
    publish_tool_loading,
    // ── agentty的自有:凭据 ───────────────────────────────
    install_auth,
    save_credentials,
    save_codex_credentials,
    load_auth_view,
    account_op
>;

/// agentty的事件订阅:仅jaal的核心源(每，数据流)。
/// 终端输入不再是事件订阅(jaal D43)；它通过 AgenttyApp::on（型号，
/// 事件）到达，请参阅 app/program.hpp.
using Sub = maya::Sub<Msg>;

// ── 字母拼写 终端 副作用 ──────────────────────────────────────
// jaal通过将其PAYLOAD交给Cmd:`Cmd(SetTitle{s})`来构建副作用。
// 在一个 调用 站点上读起来很好，在 40 个站点上读起来很差，旧的 运行时 将这些
// 拼写为命名工厂 (`Cmd<Msg>::write_clipboard(s)`)，
// 所以保留这些名称。它们是免费的——每一个都是一个构造函数 调用。
//
// 它们位于 agentty::app::cmd 中，与应用程序自己的 副作用 工厂 (
// runtime/app/cmd_factory.hpp) 相同的命名空间：状态更新函数
// 表示 `cmd::run_tool(...)` 和 `cmd::write_clipboard(...)`，
// 而不关心 副作用 来自 接口边界 的哪一侧。这就是一行的重点——程序
// 列出了它需要的内容，主机和应用程序自己的 副作用 坐在一起。
namespace app::cmd {

[[nodiscard]] inline Cmd write_clipboard(std::string text) {
    return Cmd(maya::WriteClipboard{std::move(text)});
}
[[nodiscard]] inline Cmd query_clipboard(maya::ClipboardDialect d = maya::ClipboardDialect::automatic) {
    return Cmd(maya::QueryClipboard{d});
}
[[nodiscard]] inline Cmd emit_osc(int code, std::string_view payload) {
    return Cmd(maya::osc(code, payload));
}
[[nodiscard]] inline Cmd emit_host_sequence(std::string seq) {
    return Cmd(maya::EmitHostSequence{std::move(seq)});
}
[[nodiscard]] inline Cmd reset_inline() {
    return Cmd(maya::ResetInline{});
}
[[nodiscard]] inline Cmd force_redraw() {
    return Cmd(maya::ForceRedraw{});
}
[[nodiscard]] inline Cmd set_mouse(bool on) {
    return Cmd(maya::SetMouse{on});
}

/// 承担收获的回滚缓冲区债务；空的时候无事可做。
/// 带类型的 ScrollbackDebt 是重点——只有 maya 的账本
/// 可以铸造一个，因此 状态更新函数 不能提交偏离 传输 的行计数。
[[nodiscard]] inline Cmd commit_scrollback(maya::ScrollbackDebt debt) {
    return maya::commit_from<Cmd>(debt);
}

// ── 副作用 就是这样 IO ──────────────────────────────────────────────────────────
// 正文获得I/O:正文(I/O，Sink<Msg>，stop_token，
// args...)的任务，或者，当它需要锁，正文(I/O，const
// maya::unlocked&，Sink<Msg>，stop_token，
// args...)时。与 Cmd::task 相同的规则（无捕获，可发送参数）。
// 这就是 状态更新函数 请求 IO 的方式：它返回任务，工作线程 获取 I/O ——并且，
// 因为 工作线程 线程 开始不持有 锁，所以 `unlocked` 功能使其能够按排名顺序获取 锁。
// 状态更新函数 本身也永远不会得到。
namespace io_detail {
template <class Body, class... Args>
concept IoCapBody =
    std::is_convertible_v<Body, void (*)(Io, const maya::unlocked&, maya::Sink<Msg>,
                                         std::stop_token, Args...)>;
template <class Body, class... Args>
concept IoPlainBody =
    std::is_convertible_v<Body, void (*)(Io, maya::Sink<Msg>, std::stop_token, Args...)>;

// 任务正文 jaal运行：无捕获的lambda（因此它转换为函数指针，
// 其是jaal的任务契约），首先获取新的工作线程 线程的
// `unlocked`（core/fx.hpp）。它添加了 I/O，
// 并将 锁 的能力转发到 正文。
template <class Body, class... Args>
inline constexpr auto run_io = [](const maya::unlocked& cap, maya::Sink<Msg> out,
                                  std::stop_token st, Args... args) {
    if constexpr (IoCapBody<Body, Args...>)
        Body{}(IoAccess::grant(), cap, std::move(out), std::move(st), std::move(args)...);
    else
        Body{}(IoAccess::grant(), std::move(out), std::move(st), std::move(args)...);
};
template <class Body, class... Args>
concept IoBody = IoCapBody<Body, Args...> || IoPlainBody<Body, Args...>;
}  // namespace io_detail

template <class Body, class... Args>
    requires io_detail::IoBody<Body, Args...>
[[nodiscard]] Cmd io_task(Body, Args... args) {
    return Cmd::task(io_detail::run_io<Body, Args...>, std::move(args)...);
}

/// io_task在其自己的线程上，用于可能永远不会返回的工作。
template <class Body, class... Args>
    requires io_detail::IoBody<Body, Args...>
[[nodiscard]] Cmd io_task_isolated(Body, Args... args) {
    return Cmd::task_isolated(io_detail::run_io<Body, Args...>, std::move(args)...);
}

/// Sub::stream，其正文获得I/O:正文(I/O，Sink<Msg>，stop_token，args...)。
template <class Body, class... Args>
    requires io_detail::IoBody<Body, Args...>
[[nodiscard]] Sub io_stream(std::string key, Body, Args... args) {
    return Sub::stream(std::move(key), io_detail::run_io<Body, Args...>, std::move(args)...);
}

}  // namespace app::cmd

}  // namespace agentty
