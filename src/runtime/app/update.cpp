// agentty::app::update-per-domain 状态更新函数入口点。
//
// jaal针对24个域(下面的AGENTTY_DOMAIN_UPDATE)中的每一个遍历Msg、
// 源码树和调用更新(Model&，DomainMsg)；每个都转发到 update/
// <domain>.cpp 中的 状态更新函数，因此叶编辑会重建一个 TU。
// update(Model，Msg)再往下是整个-Msg入口测试使用。

#include "agentty/runtime/app/update.hpp"

#include <utility>

#include <maya/core/overload.hpp>

#include "agentty/runtime/app/update/internal.hpp"

namespace agentty::app {

using maya::overload;

// (已删除)`is_user_input`先前对下面的
// `needs_force_redraw`消费者进行了门控。 标志 消失了 -
// 请参阅 model.hpp 的评论了解其基本原理（maya-side 渲染
// 器 修复使得 post-stream 重绘 变得不必要，并且在每次第一次击
// 键时触发它都会主动导致本应防止的 回滚缓冲区-duplication 症状）。

// pair-returning整个-Msg入口点，用于测试。
//
// jaal 不执行 调用 操作 — 它通过 AgenttyApp 遍历 Msg 源码树
// 和 调用 per-domain 重载，这从 jaal 的参数中标记 m.now。
// 这之所以保留，是因为〜40个测试将状态更新函数驱动为`update(model, msg)`，
// 并且将结果读取为值使得它们清晰可见。
//
// `now` 是 归并 的时间，与 jaal 所传递的时间完全相同。
// 关心时间的测试就通过了；一个没有得到 kTestEpoch、
// FIXED 瞬间的东西——从来没有挂钟。这就是要点：旧条目让
// 每个 状态更新函数 本身读取 std::chrono，
// 因此测试的结果可能取决于机器运行它所需的时间。现在，无论测试是否命名时间，
// 测试都是可重复的，并且提前时间是测试有意做的事情。
std::pair<Model, Cmd> update(Model m, Msg msg) {
    return update(std::move(m), std::move(msg), kTestEpoch);
}

std::pair<Model, Cmd> update(Model m, Msg msg,
                             std::chrono::steady_clock::time_point now) {
    m.now = now;   // 归并的时间：见Model::now
    // 直接构建的 Model 测试从未运行过 init()，因此它没有日历锚点，
    // wall_now() 将报告 1970。第一次将其锚定到固定日期
    // 归并 — 确定性的，因此检查 updated_at 的测试每次
    // 运行都会得到相同的答案，以及真实的日期，因此对其进行排序或格式化
    // 的代码的行为与应用程序中的行为相同。
    if (m.steady_epoch == std::chrono::steady_clock::time_point{}) {
        m.steady_epoch = kTestEpoch;
        m.wall_epoch   = std::chrono::system_clock::time_point{
            std::chrono::seconds{1'767'225'600}};   // 2026-01-01T00:00:00Z
    }
    // One-shot预热标志：由ThreadLoaded设置，
    // 由maya的下一个render()上的运行循环消耗。清除每个后续的
    // 状态更新函数 步骤，以便稍后的 线程 加载 看到干净的假→真边缘
    // （maya 的循环仅在上升沿触发 warmup_render）。
    // picker.cpp中的ThreadLoaded 处理函数将在该clear
    // -by-next-step路径运行之前将其自身的交换设置回真。
    const bool is_thread_load = std::visit([](const auto& x) {
        using T = std::decay_t<decltype(x)>;
        if constexpr (std::is_same_v<T, msg::ThreadListMsg>) {
            return std::holds_alternative<::agentty::ThreadLoaded>(x);
        }
        return false;
    }, msg);
    if (!is_thread_load) m.ui.needs_warmup_render = false;

    // 现在每个域状态更新函数都是`Cmd(Model&, DomainMsg)`，
    // 因此每个臂都是直接的调用并且模型不需要往返。
    auto cmd = std::visit(overload{
        [&](msg::ComposerMsg cm)     { return detail::composer_update     (m, std::move(cm)); },
        [&](msg::StreamMsg sm)       { return detail::stream_update       (m, std::move(sm)); },
        [&](msg::ToolMsg tm)         { return detail::tool_update         (m, std::move(tm)); },
        [&](msg::ToolOutputMsg tm)   { return detail::tool_output_update  (m, std::move(tm)); },
        [&](msg::ProvidersMsg pm)    { return detail::providers_update    (m, std::move(pm)); },
        [&](msg::ModelsMsg pm)       { return detail::models_update       (m, std::move(pm)); },
        [&](msg::ThreadListMsg tm)   { return detail::thread_list_update  (m, std::move(tm)); },
        [&](msg::PaletteMsg pm)      { return detail::palette_update      (m, std::move(pm)); },
        [&](msg::MentionMsg mm)      { return detail::mention_update      (m, std::move(mm)); },
        [&](msg::SymbolMsg sm)       { return detail::symbol_update       (m, std::move(sm)); },
        [&](msg::CodeBlockMsg cm)    { return detail::codeblock_update    (m, std::move(cm)); },
        [&](msg::CheckpointMsg cm)   { return detail::checkpoint_update   (m, std::move(cm)); },
        [&](msg::RagMsg rm)          { return detail::rag_settings_update (m, std::move(rm)); },
        [&](msg::StatsMsg sm)        { return detail::stats_update        (m, std::move(sm)); },
        [&](msg::SettingsListMsg sm) { return detail::settings_list_update(m, std::move(sm)); },
        [&](msg::ForkMsg fm)         { return detail::fork_update         (m, std::move(fm)); },
        [&](msg::TodoMsg tm)         { return detail::todo_update         (m, std::move(tm)); },
        [&](msg::LoginMsg lm)        { return detail::login_update        (m, std::move(lm)); },
        [&](msg::DiffReviewMsg dm)   { return detail::diff_review_update  (m, std::move(dm)); },
        [&](msg::SmartModeMsg sm)    { return detail::smart_mode_update   (m, std::move(sm)); },
        [&](msg::WebSearchMsg wm)    { return detail::web_search_update   (m, std::move(wm)); },
        [&](msg::PasteSettingsMsg pm) { return detail::paste_settings_update(m, std::move(pm)); },
        [&](msg::PluginEditMsg pm)   { return detail::plugin_edit_update  (m, std::move(pm)); },
        [&](msg::AppearanceMsg am)   { return detail::appearance_update   (m, std::move(am)); },
        [&](msg::SandboxMsg sm)      { return detail::sandbox_update      (m, std::move(sm)); },
        [&](msg::MetaMsg mm)         { return detail::meta_update         (m, std::move(mm)); },
    }, msg);

    // 实时 接口边界 运行相同的 post-fold 步骤 (program.hpp)，
    // 因此测试会看到应用程序将发出的发布 副作用。
    cmd = publish_derived(m, std::move(cmd));
    return {std::move(m), std::move(cmd)};
}

// ── Model-derived 说明世界读作 ────────────────────────────────────────────────
//
// 子代理路由器在工作线程 线程上运行并读取注册表，
// 因此注册表必须跟踪四个Model字段。这是 一个 保持
// 步调一致的地方：导出 Model 暗示的视图，
// 仅当它与上次发布的内容不同时才发布。
//
// 纯Elm意义上的：它读取Model，记录它在那里发布的内容（因此比较是
// 针对Model状态，而不是隐藏静态），并且RETURNS是副作用。
// 主机执行它（host.hpp句柄（PublishSubagent））。
//
// 决定"changed"的是平等，而不是散列：冲突将默默地保持
// 过期路由器运行，并且平等在这个方向上不会错——与jaal的
// subs_key (D38)的推理相同。
Cmd publish_derived(Model& m, Cmd c) {
    // 回合运行时选取的模型：将其应用到结束回合的归并上。
    // 首先，下面发布的选择和子代理视图
    // 已经描述了新模型。
    if (Cmd sw = detail::apply_pending_switch(m); !sw.is_none())
        c = Cmd::batch(std::move(c), std::move(sw));

    // 主动提供者。 Model 拥有它；循环外的代码（数据流
    // 工作线程、ACP、主）读取进程全局中的已发布副本。
    // 与下面的子代理视图形状相同：与上次发布的内容进行比较，
    // 当它移动时，主机执行 RETURN 和 副作用。
    // 这里没有写入——该函数仅描述。
    //
    // 订购是安全的：jaal在更新返回后立即在该线程上解释归并的Cmd，
    // 因此发布在任何稍后的归并运行之前落地；以及来自 此 归并 的 Cmd，
    // 需要提供者（launch_stream、
    // fetch_models）已按值捕获 m.d.selection。
    Cmd sel_effect = Cmd::none();
    if (!(m.published_selection == m.d.selection)) {
        m.published_selection = m.d.selection;
        sel_effect = Cmd(PublishSelection{m.d.selection});
    }

    // 将 IN PLACE 与 Model 逐字段进行比较。
    // 这在每次 归并 之后运行，包括击键，并且候选列表
    // 可以是数百个模型：首先构建视图将在每次击键时复制该向量，
    // 只是为了发现没有任何变化。副本仅发生在实际移动
    // 了这些字段之一的罕见 归并 上。
    auto& pub = m.published_subagent;
    const std::string provider = detail::active_provider_id(m);
    Cmd sub_effect = Cmd::none();
    if (!(pub.model == m.d.model_id.value && pub.provider == provider
          && pub.smart == m.d.smart && pub.candidates == m.d.available_models)) {
        pub.model      = m.d.model_id.value;
        pub.provider   = provider;
        pub.smart      = m.d.smart;
        pub.candidates = m.d.available_models;
        sub_effect = Cmd(PublishSubagent{
            .model = pub.model, .provider = pub.provider,
            .smart = pub.smart, .candidates = pub.candidates,
        });
    }

    // web_search政策。 面板 提交写入 m.d.persisted.web_search；
    // 这是告诉工具层的一个地方，因此每条更改策略的路径（面板、
    // 未来的 CLI、设置重新加载）都会以相同的方式到达它，
    // 而 状态更新函数 永远不必记住。
    Cmd web_search_effect = Cmd::none();
    if (!m.published_web_search || !(*m.published_web_search == m.d.persisted.web_search)) {
        m.published_web_search = m.d.persisted.web_search;
        web_search_effect = Cmd(PublishWebSearchPolicy{m.d.persisted.web_search});
    }

    // 粘贴设置richpaste以同样的方式读取(设置→粘贴)。
    Cmd paste_effect = Cmd::none();
    if (!m.published_paste || !(*m.published_paste == m.d.persisted.paste)) {
        m.published_paste = m.d.persisted.paste;
        paste_effect = Cmd(PublishPasteConfig{m.d.persisted.paste});
    }

    // MCP装刀(#98)，同理：转工作线程读取槽位。
    Cmd loading_effect = Cmd::none();
    if (!m.published_tool_loading || *m.published_tool_loading != m.d.persisted.tool_loading) {
        m.published_tool_loading = m.d.persisted.tool_loading;
        loading_effect = Cmd(PublishToolLoading{m.d.persisted.tool_loading});
    }

    // 没有任何移动——常见情况，每次击键——不花费任何批次。
    if (sel_effect.is_none() && sub_effect.is_none() && web_search_effect.is_none()
        && paste_effect.is_none() && loading_effect.is_none())
        return c;
    // 该选择是在子代理视图之前发布的，因此读取两者的
    // 工作线程看到它们描述相同的提供者。
    return Cmd::batch(std::move(c), std::move(sel_effect), std::move(sub_effect),
                      std::move(web_search_effect), std::move(paste_effect),
                      std::move(loading_effect));
}

namespace {

// One-shot预热标志：由ThreadLoaded设置，由maya的主机在下
// 一个render()消耗。在每个 OTHER 状态更新函数 步骤上清除它，
// 以便后面的 线程 加载 仍然产生干净的假->真边缘（主机仅在上升沿触发
// warmup_render）。运行后，picker.cpp 中的
// ThreadLoaded 处理函数 将其自身的交换设置回 true。
//
// 驻留在此处而不是内联在每个重载中，因为它必须在 每个 域中发生，
// 每步恰好一次，在 状态更新函数 看到模型之前。
template <class Domain>
void clear_warmup_unless_thread_load(Model& m, const Domain& d) {
    bool is_thread_load = false;
    if constexpr (std::is_same_v<Domain, msg::ThreadListMsg>)
        is_thread_load = std::holds_alternative<::agentty::ThreadLoaded>(d);
    if (!is_thread_load) m.ui.needs_warmup_render = false;
}

} // 命名空间结束。

// ── jaal的切入点 ───────────────────────────────────────────────────
// 每个域一次过载。 jaal 沿着 Msg 源码树 和 调用 行走，
// 与它所着陆的位置相匹配；我们不再写外访了。
//
// 现在每个都是直接调用:状态更新函数都是`Cmd(Model&, DomainMsg)`，
// 所以没有模型可以来回交。
//
// 作为宏完成，因为写出 24 个相同的 正文 有 24 次机会
// 拼写其中一个，并且这里的拼写错误将整个域路由到错误的
// 状态更新函数——这是 编译器 无法看到的错误，
// 因为每个 状态更新函数 具有相同的形状。此后宏未立即定义。
//
// `clear_warmup_unless_thread_load`不是
// 偶然的：旧的分派器在EVERY步骤之前运行它，并且放弃它会使预热标志在第
// 一个线程 加载之后停留在上面，所以maya在每个帧上重新-warmed 渲染 缓存。
// 它在这里发生的原因与在那里发生的原因相同 - 请参阅它的评论。
#define AGENTTY_DOMAIN_UPDATE(DomainMsg, reducer)                       \
    Cmd update(Model& m, msg::DomainMsg d) {                            \
        clear_warmup_unless_thread_load(m, d);                          \
        return detail::reducer(m, std::move(d));                        \
    }

AGENTTY_DOMAIN_UPDATE(ComposerMsg,     composer_update)
AGENTTY_DOMAIN_UPDATE(StreamMsg,       stream_update)
AGENTTY_DOMAIN_UPDATE(ToolMsg,         tool_update)
AGENTTY_DOMAIN_UPDATE(ToolOutputMsg,   tool_output_update)
AGENTTY_DOMAIN_UPDATE(ProvidersMsg,    providers_update)
AGENTTY_DOMAIN_UPDATE(ModelsMsg,       models_update)
AGENTTY_DOMAIN_UPDATE(ThreadListMsg,   thread_list_update)
AGENTTY_DOMAIN_UPDATE(PaletteMsg,      palette_update)
AGENTTY_DOMAIN_UPDATE(MentionMsg,      mention_update)
AGENTTY_DOMAIN_UPDATE(SymbolMsg,       symbol_update)
AGENTTY_DOMAIN_UPDATE(CodeBlockMsg,    codeblock_update)
AGENTTY_DOMAIN_UPDATE(CheckpointMsg,   checkpoint_update)
AGENTTY_DOMAIN_UPDATE(RagMsg,          rag_settings_update)
AGENTTY_DOMAIN_UPDATE(StatsMsg,        stats_update)
AGENTTY_DOMAIN_UPDATE(SettingsListMsg, settings_list_update)
AGENTTY_DOMAIN_UPDATE(ForkMsg,         fork_update)
AGENTTY_DOMAIN_UPDATE(TodoMsg,         todo_update)
AGENTTY_DOMAIN_UPDATE(LoginMsg,        login_update)
AGENTTY_DOMAIN_UPDATE(DiffReviewMsg,   diff_review_update)
AGENTTY_DOMAIN_UPDATE(SmartModeMsg,    smart_mode_update)
AGENTTY_DOMAIN_UPDATE(WebSearchMsg,    web_search_update)
AGENTTY_DOMAIN_UPDATE(PasteSettingsMsg, paste_settings_update)
AGENTTY_DOMAIN_UPDATE(PluginEditMsg,   plugin_edit_update)
AGENTTY_DOMAIN_UPDATE(AppearanceMsg,   appearance_update)
AGENTTY_DOMAIN_UPDATE(SandboxMsg,      sandbox_update)
AGENTTY_DOMAIN_UPDATE(MetaMsg,         meta_update)

#undef AGENTTY_DOMAIN_UPDATE

} // namespace agentty::app
