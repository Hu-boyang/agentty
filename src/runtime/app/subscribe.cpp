#include "agentty/runtime/app/subscribe.hpp"

#include <chrono>
#include <cstdlib>
#include <optional>
#include <variant>

#include <maya/terminal/ansi.hpp>

#include "agentty/runtime/login.hpp"
#include "agentty/domain/ui_live.hpp"   // 思考显示偏好，用于控制推理内容的逐字显示。
#include "agentty/runtime/view/helpers.hpp"  // ui::reasoning_shown 判定函数。
#include "agentty/runtime/panel/common.hpp"
#include "agentty/runtime/panel/top.hpp"
#include "agentty/runtime/panel/nav.hpp"
#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/app/cmd_factory.hpp"   // device_login_sub / codex_login_sub 登录订阅。
#include "agentty/runtime/slash.hpp"            // 输入框的 `/` 命令菜单。

namespace pn = agentty::ui::panel;

namespace agentty::app {

using maya::KeyEvent;
using maya::CharKey;
using maya::SpecialKey;
namespace pick = agentty::ui::pick;
namespace nav  = agentty::ui::nav;

namespace {

// 通过 SSH 会话运行 agentty 时返回 true。
// 只检测一次 SSH 守护进程传入远程 Shell 的
// 环境变量。SSH 场景的渲染瓶颈在传输链路，
// 每个流式帧会产生数 KB 的 ANSI 差异数据，
// 30 fps 会挤满高延迟或低带宽链路，造成轮次中途卡
// 顿以及结束后的集中补绘。因此远程场景会降低流式 Tick
// 的频率，具体见下方周期配置。
// SSH 检测已移到 app::detail（stream.cpp），
// 供轮次结束时的逐字显示策略复用，声明见 internal.hpp。当前编译单元通过
// detail:: 声明继续使用它。

} // 命名空间结束。

// 统一定义流式 Tick 的周期，完整说明见
// subscribe.hpp 中的声明。此函数只读取 init()
// 捕获到 Model::env 的启动环境，因此 subscribe()
// 与 visual_hash() 根据同一字段得到一致结果，
// 无需直接读取进程环境。
std::chrono::milliseconds streaming_tick_period(const Model::Env& env) noexcept {
    // SSH 已不再降低 Tick 频率，否则轮次结束时的平滑显示只剩两三帧，
    // 看起来像一次粘贴。需要减少重绘时使用 Motion=Off。
    return env.synchronized_output
        ? std::chrono::milliseconds(33)
        : std::chrono::milliseconds(100);
}

// 最后一条消息仍包含尚未合并到稳定正文的传输数据时返回 true，
// 具体为 streaming_text / pending_stream。
// Tick 订阅控制逐字动画时钟和 pending_stream 向
// streaming_text 的逐步转移。
// 若仅依据 m.s.active() 启用时钟，数据流刚开始时会出现一个 Tick 的空档。
// 最初一两条 StreamTextDelta 消息会
// 触发独立渲染（fps=0），但动画循环和逐步转移
// 要等刚启用的 Tick 定时器实际触发后才开始，
// 因而前一两个词后会短暂停顿。
// agent_session 始终订阅 Tick，所以没有这个问题。
// 空闲时持续触发 Tick 会浪费资源，
// 因此只把 Tick 的启用范围扩展到
// 仍有实时数据的帧。第一条增量到达时，
// 时钟就已运行，逐字动画从首字节开始连续显示，
// 同时保留零空闲开销。
bool tail_has_live_bytes(const Model& m) noexcept {
    if (m.d.current.messages.empty()) return false;
    const auto& back = m.d.current.messages.back();
    return !back.streaming_text.empty() || !back.pending_stream.empty();
}

bool reveal_needs_frames(const Model& m) noexcept {
    if (!m.s.active() || m.d.current.messages.empty()) return false;
    const auto& back = m.d.current.messages.back();
    if (back.role != Role::Assistant) return false;
    return !back.streaming_text.empty()
        || !back.pending_stream.empty()
        // 纯思考阶段还没有答案数据，但推理通道
        // msg.thinking 已在独立逐字显示。
        // 遗漏此条件会导致快速时间桶未启用、
        // 已请求的 RAF 帧被过滤，
        // 思考文字于是停住，直到下一次按键。
        //
        // 视图渲染和活动指示器都使用 ui::reasoning_shown。
        // 控制推理内容是否显示的条件，
        // 也必须用于判断是否播放动画，否则显示状态与动画状态
        // 就会不一致。
        || ::agentty::ui::reasoning_shown(back);
}

bool reveal_draining(const Model& m) noexcept {
    return m.ui.pending_settle_freeze
        || m.ui.settle_cooldown_ticks > 0
        // 逐字显示尚未完成收尾时返回 true，涉及 is_live /
        // reveal_in_progress / is_finalizing
        // / is_parsing。传输结束后，基于实际时间的打字
        // 动画往往还会持续数秒。此时停止时钟会让动画中途冻结，
        // CPU 仅约 1%，直到下一次按键才继续。
        || !detail::live_tail_reveal_settled(m);
}

// 主界面是否启用改动条的批量快捷键（^A 接受全部、
// ^X 拒绝全部）。on_key 使用当前模型读取此条件。
//
// 需要同时满足三个条件。
//   * show_changes_strip 表示改动条标题中实际显示了快捷键提示。
//     其他位置不会提示这些快捷键。默认隐藏改动条时，
//     若仍启用 ^X，用户可能毫无预期地撤销所有待确认修改。
//     因此提示是否可见与快捷键是否可用由同一个开关控制。
//   * pending_changes 必须非空，否则没有可接受或拒绝的修改。
//   * !m.s.active() 表示当前轮次已停止运行。轮次中途修改集合仍在增长，
//     这时做批量决定会作用于 Agent 尚未写完的
//     集合。
// 输入框状态不在条件中。快捷键需要按住 Ctrl，
// 不会与普通输入冲突，详见 on_diff_review 的按键策略。
bool strip_chords_armed(const Model& m) noexcept {
    return m.d.show_changes_strip
        && !m.d.pending_changes.empty()
        && !m.s.active();
}

bool animation_demand(const Model& m) noexcept {
    return m.s.active()
        // `models_loading` 让统一模型
        // 选择面板在等待较慢的后端（例如 Ollama、
        // 自定义主机）时持续播放加载动画。
        // 加载符号停止会让用户误以为程序卡住。
        || m.s.models_loading
        || m.loading_spinner_visible()
        || tail_has_live_bytes(m)
        || m.ui.pending_rehydrate_trim
        // LOOP 在等待退避时间时仍需运行动画时钟。
        // 重发由收尾路径触发，倒计时标签也需更新。
        // 否则一次循环失败后，应用会完全空闲，
        // 直到下一次按键才恢复循环。
        || m.ui.composer.loop_wait_until_ms > 0
        || reveal_draining(m);
}

namespace {

// ── 各模态界面的按键处理函数；返回 std::nullopt 表示继续传递。 ──────────

std::optional<Msg> on_permission(const KeyEvent& ev) {
    if (auto* ck = std::get_if<CharKey>(&ev.key)) {
        char32_t c = ck->codepoint;
        // 提示框打开时，^C 仍必须能够退出。此处理函数的
        // 返回值会被直接返回，不会继续交给 on_global；
        // 若返回 nullopt 吞掉 ^C，用户就只能先
        // 回答提示才能退出。旧式终端发送原始字节 0x03。
        if (c == 0x03 || (ev.mods.ctrl && (c == U'c' || c == U'C')))
            return Quit{};
        switch (c) {
            case 'y': case 'Y': return PermissionApprove{};
            case 'n': case 'N': return PermissionReject{};
            case 'a': case 'A': return PermissionApproveAlways{};
        }
    }
    if (std::holds_alternative<SpecialKey>(ev.key)
        && std::get<SpecialKey>(ev.key) == SpecialKey::Escape)
        return PermissionReject{};
    return std::nullopt;
}

std::optional<Msg> on_command_palette(const KeyEvent& ev) {
    // ^K 命令面板使用共享按键规则和输入过滤。不启用跳转或 vim 导航，
    // 因为可打印字符用于查询。除切换面板的快捷键外，其他 Ctrl
    // 组合键直接吞掉，不能变成查询文本。
    nav::NavSpec s;
    s.close     = [] { return Msg{ClosePalette{}}; };
    s.select    = [] { return Msg{PaletteSelect{}}; };
    s.move      = [](int d) { return Msg{PaletteMove{d}}; };
    s.filter_bs = [] { return Msg{PaletteBackspace{}}; };
    s.filter_ch = [](char32_t c) { return Msg{PaletteInput{c}}; };
    s.toggle_close_chord = U'k';
    return nav::translate(s, ev);
}

std::optional<Msg> on_mention_palette(const KeyEvent& ev) {
    // @ 提及列表依附于输入框：每个 CharKey，包括带 Ctrl 的字符，
    // 都交给面板自身的输入逻辑，因此 extra 优先认领字符。
    nav::NavSpec s;
    s.close     = [] { return Msg{CloseMention{}}; };
    s.select    = [] { return Msg{MentionSelect{}}; };
    s.move      = [](int d) { return Msg{MentionMove{d}}; };
    s.filter_bs = [] { return Msg{MentionBackspace{}}; };
    s.extra     = [](const KeyEvent& e) -> std::optional<Msg> {
        if (auto* ck = std::get_if<CharKey>(&e.key))
            return Msg{MentionInput{ck->codepoint}};
        return std::nullopt;
    };
    return nav::translate(s, ev);
}

std::optional<Msg> on_symbol_palette(const KeyEvent& ev) {
    nav::NavSpec s;
    s.close     = [] { return Msg{CloseSymbol{}}; };
    s.select    = [] { return Msg{SymbolSelect{}}; };
    s.move      = [](int d) { return Msg{SymbolMove{d}}; };
    s.filter_bs = [] { return Msg{SymbolBackspace{}}; };
    s.extra     = [](const KeyEvent& e) -> std::optional<Msg> {
        if (auto* ck = std::get_if<CharKey>(&e.key))
            return Msg{SymbolInput{ck->codepoint}};
        return std::nullopt;
    };
    return nav::translate(s, ev);
}

// Ctrl+G 代码块选择器。Enter 运行当前行；
// 直接输入数字可运行对应行，编号从 1 开始，与 ①②③ 标签一致。
// 例如 Ctrl+G 后按 2 即可运行第二块。
// e 将内容放入输入框供编辑，y 复制纯净文本。
std::optional<Msg> on_code_block_picker(const KeyEvent& ev) {
    nav::NavSpec s;
    s.close  = [] { return Msg{CloseCodeBlocks{}}; };
    s.select = [] { return Msg{CodeBlocksSelect{}}; };
    s.move   = [](int d) { return Msg{CodeBlocksMove{d}}; };
    s.toggle_close_chord = U'g';
    s.extra  = [](const KeyEvent& e) -> std::optional<Msg> {
        const auto v = nav::char_view(e);
        if (!v) return std::nullopt;
        // 数字保持直接按键语义：它们用于选择并运行第 N 个代码块，
        // 相当于 Enter 的数字形式。
        if (!v->ctrl && v->c >= U'1' && v->c <= U'9')
            return Msg{CodeBlocksSelect{static_cast<int>(v->c - U'1')}};
        // 操作使用组合键，纯列表模式也支持下方的字母按键。
        if (v->ctrl) switch (v->c) {
            case U'e': return Msg{CodeBlocksEdit{}};
            case U'y': return Msg{CodeBlocksCopy{}};
            default: break;
        }
        return std::nullopt;
    };
    // 纯列表中，字母与组合键均可使用；即使终端复用器截获
    // ^E/^Y，用户仍能执行操作。
    s.letter_verbs = {
        {U'e', [] { return Msg{CodeBlocksEdit{}}; }},
        {U'y', [] { return Msg{CodeBlocksCopy{}}; }},
    };
    return nav::translate(s, ev);
}

// 运行结果卡片：^A 附加到输入框，^Y 复制，
// Esc/Enter 关闭。Enter 默认
// 丢弃卡片而不附加，避免意外改变输入框内容；
// 附加必须显式使用快捷键。
std::optional<Msg> on_code_block_result(const KeyEvent& ev) {
    nav::NavSpec s;
    s.close     = [] { return Msg{CodeBlockResultDiscard{}}; };
    s.select    = [] { return Msg{CodeBlockResultDiscard{}}; };   // 安全的默认行为。
    s.move      = [](int d) { return Msg{CodeBlocksMove{d}}; };
    s.page_step = 10;
    s.extra     = [](const KeyEvent& e) -> std::optional<Msg> {
        const auto v = nav::char_view(e);
        if (!v || !v->ctrl) return std::nullopt;
        switch (v->c) {
            case U'a': return Msg{CodeBlockResultAttach{}};
            case U'y': return Msg{CodeBlockResultCopy{}};
            default:   return std::nullopt;
        }
    };
    s.letter_verbs = {
        {U'a', [] { return Msg{CodeBlockResultAttach{}}; }},
        {U'y', [] { return Msg{CodeBlockResultCopy{}}; }},
    };
    return nav::translate(s, ev);
}

// 统计视图：带选项卡的只读面板。
//
// 没有 select 或 move，也没有选择光标，
// 因为面板中没有可执行操作的条目。给只读视图接入选择逻辑，
// 会让高亮暗示 Enter 能执行操作，实际却没有作用。
//
// Tab / Shift-Tab 切换选项卡；←/→ 也可切换，
// 因为选项卡条是横向排列。滚动通过共享规则的 page_step 处理，
// 使长统计列表与其他面板一样可以翻页。
std::optional<Msg> on_stats_viewer(const KeyEvent& ev) {
    nav::NavSpec s;
    s.close     = [] { return Msg{CloseStats{}}; };
    // 含图表的选项卡可能高于面板可视区，
    // 例如 Tokens 约有 22 行，
    // 而正文仅有 14 行。面板因此需要滚动并
    // 显示滚动条，否则底部图表无法访问。
    // 只读面板没有可选条目，所以 ↑/↓ 滚动正文。
    s.move      = [](int d) { return Msg{StatsScroll{d}}; };
    s.page_step = 10;
    s.extra = [](const KeyEvent& e) -> std::optional<Msg> {
        const auto* sk = std::get_if<SpecialKey>(&e.key);
        if (!sk) return std::nullopt;
        switch (*sk) {
            case SpecialKey::Tab:     return Msg{StatsTab{+1}};
            case SpecialKey::BackTab: return Msg{StatsTab{-1}};
            case SpecialKey::Left:    return Msg{StatsTab{-1}};
            case SpecialKey::Right:   return Msg{StatsTab{+1}};
            default:                  return std::nullopt;
        }
    };
    return nav::translate(s, ev);
}

// 技能视图：只读列表。
//
// 它与统计视图不同，具有选择光标，因为每行都有详情，
// 移动选择会改变页脚说明。但它没有 select，
// Enter 按设计不执行操作。
//
// 这里也不提供批准按键，这一限制是设计的重要部分。
// Anthropic 的测量中，Claude Code 权限
// 提示的批准率为 93%。若用户正在浏览的列表只需按一个键
// 就能批准，极易形成习惯性确认。因此，
// 批准放在 agentty skill approve NAME 中，
// 由确认界面同时展示检查结果和技能正文。面板说明内容与原因，
// 批准则需要用户主动前往专门入口。
std::optional<Msg> on_skills_viewer(const KeyEvent& ev) {
    nav::NavSpec s;
    s.close     = [] { return Msg{CloseSkills{}}; };
    s.move      = [](int d) { return Msg{SkillsMove{d}}; };
    s.page_step = 10;
    return nav::translate(s, ev);
}

// 回退检查点选择器：共享按键规则、vim 导航和只读列表。
std::optional<Msg> on_checkpoint_picker(const KeyEvent& ev) {
    nav::NavSpec s;
    s.close     = [] { return Msg{CloseCheckpoints{}}; };
    s.select    = [] { return Msg{CheckpointsSelect{}}; };
    s.move      = [](int d) { return Msg{CheckpointsMove{d}}; };
    s.page_step = 10;
    return nav::translate(s, ev);
}

// 按键路由只需知道表单面板当前由哪种模式
// 占用键盘。
struct FormFocus {
    bool open     = false;
    bool editing  = false;
    bool choosing = false;
};

[[nodiscard]] inline FormFocus focus_of(const form::Form& f) noexcept {
    return FormFocus{true, f.editing(), f.choosing()};
}

// 把按键转交共享表单层。表单未认领时返回 nullopt，
// 让全局处理仍能执行，例如任何位置都能通过 ^C 退出。
template <class Wrap>
[[nodiscard]] std::optional<Msg> on_form(const FormFocus& f, const KeyEvent& ev,
                                        Wrap&& wrap) {
    if (!f.open) return std::nullopt;
    if (auto a = form::keys::translate(f.editing, f.choosing, ev))
        return wrap(*a);
    return std::nullopt;
}

// 检索覆盖面板的按键路由。全部键位映射由共享表单层负责，
// 包括键盘所属模式、各类行的 Enter 行为和 Esc
// 返回逻辑，因此这里只做转发。
//
// ^E 是例外：它改变哪些行存在，
// 属于面板状态。这里使用组合键，
// 因为可打印字符现在会直接编辑文本行。
// 浏览模式下 ^E 可用；
// 编辑模式下表单层优先将它解释为移动到行尾。
std::optional<Msg> on_rag_settings(const FormFocus& f, const KeyEvent& ev) {
    if (f.open && !f.editing)
        if (const auto v = nav::char_view(ev); v && v->ctrl && v->c == U'e')
            return Msg{RagAdvanced{}};
    return on_form(f, ev, [](form::keys::Action a) { return Msg{RagEmbedKey{a}}; });
}

// 设置选择器，包括 Plugins/Commands/Agents/Hooks，有两种模式：
//   列表模式使用共享规则和 vim 导航，a 添加、d 删除、Space 执行。
//   添加模式是单行文本提示；所有可打印字符都作为
//               字面文本输入，因此关闭导航快捷键。
std::optional<Msg> on_settings_list(const KeyEvent& ev, bool input_active,
                                   bool hub, bool has_query) {
    if (input_active) {
        nav::NavSpec s;
        s.close     = [] { return Msg{SettingsListCancelInput{}}; };
        s.select    = [] { return Msg{SettingsListSubmitInput{}}; };
        s.filter_bs = [] { return Msg{SettingsListBackspace{}}; };
        s.filter_ch = [](char32_t c) { return Msg{SettingsListChar{c}}; };
        return nav::translate(s, ev);
    }
    if (hub) {
        // 设置中心允许输入以搜索全部设置。Esc 先清空查询，
        // 再关闭面板，避免一次输入错误就退出。
        nav::NavSpec s;
        s.close     = has_query ? std::function<Msg()>{[] { return Msg{SettingsSearchClear{}}; }}
                                : std::function<Msg()>{[] { return Msg{CloseSettingsList{}}; }};
        s.select    = [] { return Msg{SettingsListActivate{}}; };
        s.move      = [](int d) { return Msg{SettingsListMove{d}}; };
        s.filter_bs = [] { return Msg{SettingsSearchBackspace{}}; };
        s.filter_ch = [](char32_t c) { return Msg{SettingsSearchInput{c}}; };
        return nav::translate(s, ev);
    }
    nav::NavSpec s;
    s.close   = [] { return Msg{CloseSettingsList{}}; };
    s.select  = [] { return Msg{SettingsListActivate{}}; };
    s.move    = [](int d) { return Msg{SettingsListMove{d}}; };
    s.extra   = [](const KeyEvent& e) -> std::optional<Msg> {
        // → 打开插件服务器的工具列表；← 从服务器或任一工具
        // 行关闭该列表。其他行忽略这两个按键。
        if (const auto* sk = std::get_if<SpecialKey>(&e.key)) {
            if (*sk == SpecialKey::Right) return Msg{SettingsListExpand{true}};
            if (*sk == SpecialKey::Left)  return Msg{SettingsListExpand{false}};
        }
        const auto v = nav::char_view(e);
        if (!v || !v->ctrl) return std::nullopt;
        switch (v->c) {
            // 组合键与下方的单字母操作对应。
            // ^A 在 Plugins 中打开先选类型的添加表单，
            // 在其他列表打开单行提示；^N 保留旧式单行添加；
            // ^E 编辑；^D 通过两次按键删除。
            case U'a': return Msg{SettingsListEditOpen{/*add=*/true}};
            case U'n': return Msg{SettingsListAddStart{}};
            case U'e': return Msg{SettingsListEditOpen{/*add=*/false}};
            case U'd': return Msg{SettingsListRemove{}};
            default:   return std::nullopt;
        }
    };
    // 此列表没有文本输入，单字母操作不会歧义。上方的组合键
    // 可能被终端复用器截获，例如 ^A 是 tmux 前缀，
    // 首次按键不会到达应用，因此同时支持两种方式。
    s.letter_verbs = {
        {U'a', [] { return Msg{SettingsListEditOpen{/*add=*/true}}; }},
        {U'n', [] { return Msg{SettingsListAddStart{}}; }},
        {U'e', [] { return Msg{SettingsListEditOpen{/*add=*/false}}; }},
        {U'd', [] { return Msg{SettingsListRemove{}}; }},
        {U'p', [] { return Msg{SettingsListPin{}}; }},   // 始终加载此工具（#98）。
    };
    return nav::translate(s, ev);
}

// 分支选择器是纯列表，每行对应分支使用的一种独立 RAG 模式。
std::optional<Msg> on_fork_picker(const KeyEvent& ev) {
    nav::NavSpec s;
    s.close   = [] { return Msg{CloseFork{}}; };
    s.select  = [] { return Msg{ForkThread{}}; };
    s.move    = [](int d) { return Msg{ForkMove{d}}; };
    return nav::translate(s, ev);
}

// 模型选择器使用完整导航规则、输入过滤和推理力度切换。
// ^P 跳转到提供方，^F 管理收藏，^R 切换推理显示，
// ^L 强制刷新全部目录。模型选择只保留这一个界面。
std::optional<Msg> on_fused_picker(const KeyEvent& ev) {
    nav::NavSpec s;
    s.close     = [] { return Msg{CloseModels{}}; };
    s.select    = [] { return Msg{ModelsSelect{}}; };
    s.move      = [](int d) { return Msg{ModelsMove{d}}; };
    s.jump      = [](nav::Jump j) {
        using W = ModelsJump::Where;
        return Msg{ModelsJump{j == nav::Jump::Home ? W::Home
                                 : j == nav::Jump::End  ? W::End
                                 : j == nav::Jump::PageUp ? W::PageUp
                                                          : W::PageDown}};
    };
    s.filter_bs = [] { return Msg{ModelsFilterBackspace{}}; };
    s.filter_ch = [](char32_t c) { return Msg{ModelsFilterInput{c}}; };
    s.extra     = [](const KeyEvent& e) -> std::optional<Msg> {
        if (const auto* sk = std::get_if<SpecialKey>(&e.key)) {
            // ←/→ 循环切换当前高亮模型的推理力度等级。
            if (*sk == SpecialKey::Left)  return Msg{ModelsCycleEffort{-1}};
            if (*sk == SpecialKey::Right) return Msg{ModelsCycleEffort{+1}};
            return std::nullopt;
        }
        const auto v = nav::char_view(e);
        if (!v) return std::nullopt;
        // ^/ 也可能以不带 Ctrl 标志的原始字节 0x1F 到达；
        // 它将列表限定为高亮行的提供方，再按一次清除限制。
        // Esc 关闭。合并后的选择器是唯一模型界面，
        // 因此 ^/ 无需再用于关闭另一个面板。
        if (v->ctrl && v->c == U'/')
            return Msg{ModelsScopeProvider{}};
        if (v->ctrl) {
            switch (v->c) {
                case U'p': return Msg{OpenProviders{}};   // 跳转到关联面板。
                case U'f': return Msg{ModelsToggleFavorite{}};
                // 这里不绑定 ^W 来切换上下文窗口覆盖值，
                // 原因与下方 ^R 相同：它是
                // 针对模型设置一次的配置项，
                // 不是选模型时经常使用的导航操作，
                // 不适合放在列表行的快捷键上。
                //
                // 通常也无需手动设置窗口。系统会先依次参考网关 /
                // v1/models 声明、/v1/model/info、
                // /props、/api/show 探测结果和
                // models.dev 声明，再考虑手动覆盖。
                // 这些来源覆盖约 95% 的内置提供方：openrouter
                // 368/368、google 39/39、
                // anthropic 14/14、openai 43/48。
                // 剩余情况通常是私有网关提供了所有目录都不
                // 认识的模型标识。此时可通过文档中的
                // AGENTTY_MAX_CONTEXT_TOKENS 配置，
                // 无需让所有用户多记一个快捷键。
                // 这里也不绑定 ^R 来切换推理内容显示。
                // 是否显示推理属于 Appearance 的
                // Thinking 设置；在模型选择器里增加
                // 第二个隐藏快捷键，曾导致两处设置发生分歧。
                // 与可见设置矛盾的隐藏控制会让行为
                // 更难理解。
                case U'l': return Msg{ModelsRefresh{}};
                default:   break;
            }
            // 继续返回 nullopt。translate()
            // 不会把带 Ctrl 的字符输入过滤框，
            // 分派链也会直接返回此处理函数的结果，因此该组合键会被吞掉。
        }
        return std::nullopt;
    };
    return nav::translate(s, ev);
}

// 提供方选择器使用共享规则和输入过滤。^/ 跳到合并后的模型选择器；
// ^D 或向前 Delete 通过两次按键删除已保存主机，
// 或退出使用密钥的预设。使用 Ctrl 可避免与以 d 开头的名称，
// 如 deepseek，冲突。
std::optional<Msg> on_provider_picker(const KeyEvent& ev) {
    nav::NavSpec s;
    s.close     = [] { return Msg{CloseProviders{}}; };
    s.select    = [] { return Msg{ProvidersSelect{}}; };
    s.move      = [](int d) { return Msg{ProvidersMove{d}}; };
    s.jump      = [](nav::Jump j) {
        using W = ProvidersJump::Where;
        return Msg{ProvidersJump{j == nav::Jump::Home ? W::Home
                                    : j == nav::Jump::End  ? W::End
                                    : j == nav::Jump::PageUp ? W::PageUp
                                                             : W::PageDown}};
    };
    s.filter_bs = [] { return Msg{ProvidersFilterBackspace{}}; };
    s.filter_ch = [](char32_t c) { return Msg{ProvidersFilterInput{c}}; };
    s.toggle_close_chord = U'p';
    s.extra     = [](const KeyEvent& e) -> std::optional<Msg> {
        if (const auto* sk = std::get_if<SpecialKey>(&e.key)) {
            // 向前 Delete 执行删除；Mac 笔记本的 delete
            // 实际发送 Backspace，因此提供下方 ^D 作为替代。
            if (*sk == SpecialKey::Delete) return Msg{ProvidersDelete{}};
            return std::nullopt;
        }
        const auto v = nav::char_view(e);
        if (!v) return std::nullopt;
        if (v->ctrl && v->c == U'/')
            return Msg{OpenModels{}};                       // 跳转到关联面板。
        if (v->ctrl && (v->c == U'd' || v->c == U'D'))
            return Msg{ProvidersDelete{}};
        // ^E（展开）显示或隐藏尚未配置的提供方。
        //
        // 不能用 ^A：它是 tmux 的默认前缀，^B 则是
        // screen 前缀，首次按键无法到达应用，
        // 看起来就像需要按两次。nav.hpp 已明确提示这一问题。
        if (v->ctrl && (v->c == U'e' || v->c == U'E'))
            return Msg{ProvidersToggleAll{}};
        return std::nullopt;
    };
    return nav::translate(s, ev);
}

// 会话列表使用共享规则，并可输入标题进行过滤。操作采用组合
// 键：^N 新建、^D 两次删除、^R 重命名，
// 字母则用于过滤。重命名时，Enter 保存、Esc 放弃，
// Left/Right/Home/End 移动光标，
// Delete 向前删除；忽略组合键，避免 ^D 删除正在重
// 命名的行。
std::optional<Msg> on_thread_list(const KeyEvent& ev, bool renaming) {
    if (renaming) {
        nav::NavSpec s;
        s.close     = [] { return Msg{ThreadListRenameCancel{}}; };
        s.select    = [] { return Msg{ThreadListRenameSubmit{}}; };
        s.filter_bs = [] { return Msg{ThreadListRenameBackspace{}}; };
        s.filter_ch = [](char32_t c) { return Msg{ThreadListRenameChar{c}}; };
        s.extra     = [](const KeyEvent& e) -> std::optional<Msg> {
            const auto* sk = std::get_if<SpecialKey>(&e.key);
            if (!sk) return std::nullopt;
            using To = ThreadListRenameCursor::To;
            switch (*sk) {
                case SpecialKey::Left:   return Msg{ThreadListRenameCursor{To::Left}};
                case SpecialKey::Right:  return Msg{ThreadListRenameCursor{To::Right}};
                case SpecialKey::Home:   return Msg{ThreadListRenameCursor{To::Home}};
                case SpecialKey::End:    return Msg{ThreadListRenameCursor{To::End}};
                case SpecialKey::Delete: return Msg{ThreadListRenameDelete{}};
                default:                 return std::nullopt;
            }
        };
        return nav::translate(s, ev);
    }
    nav::NavSpec s;
    s.close   = [] { return Msg{CloseThreadList{}}; };
    s.select  = [] { return Msg{ThreadListSelect{}}; };
    s.move    = [](int d) { return Msg{ThreadListMove{d}}; };
    s.filter_bs = [] { return Msg{ThreadListFilterBackspace{}}; };
    s.filter_ch = [](char32_t c) { return Msg{ThreadListFilterInput{c}}; };
    s.jump    = [](nav::Jump j) {
        using W = ThreadListJump::Where;
        return Msg{ThreadListJump{j == nav::Jump::Home ? W::Home
                                : j == nav::Jump::End  ? W::End
                                : j == nav::Jump::PageUp ? W::PageUp
                                                         : W::PageDown}};
    };
    // 再次按 ^J 关闭。旧式终端的原始 0x0A 会先被 maya 映射
    // 为 SpecialKey::Enter，因此这里只能收到带修饰标志的形式。
    s.toggle_close_chord = U'j';
    s.extra   = [](const KeyEvent& e) -> std::optional<Msg> {
        if (const auto* sk = std::get_if<SpecialKey>(&e.key)) {
            switch (*sk) {
                // Tab 按“全部 → 当前目录 → Codex 与 Claude → 全部”循环；Shift+Tab 反向切换。
                case SpecialKey::Tab:     return Msg{ThreadListScopeToggle{e.mods.shift ? -1 : +1}};
                case SpecialKey::BackTab: return Msg{ThreadListScopeToggle{-1}};
                // Shift+↑↓ 跳到首尾，方便没有 Home/End 的键盘。
                case SpecialKey::Up:
                    if (e.mods.shift) return Msg{ThreadListJump{ThreadListJump::Where::Home}};
                    return std::nullopt;
                case SpecialKey::Down:
                    if (e.mods.shift) return Msg{ThreadListJump{ThreadListJump::Where::End}};
                    return std::nullopt;
                case SpecialKey::Left:  return Msg{ThreadPeekScroll{-1, e.mods.shift}};
                case SpecialKey::Right: return Msg{ThreadPeekScroll{+1, e.mods.shift}};
                default:                return std::nullopt;
            }
        }
        const auto v = nav::char_view(e);
        if (!v || !v->ctrl) return std::nullopt;   // 仅使用组合键。
        switch (v->c) {
            case U'n': return Msg{NewThread{}};
            case U'd': return Msg{ThreadListDelete{}};
            case U'r': return Msg{ThreadListRenameStart{}};
            default:   return std::nullopt;
        }
    };
    return nav::translate(s, ev);
}

// Smart Mode 配置使用共享规则和 vim 导航；Space 切换当前项，
// x 将选中槽位重置为自动。
// Smart Mode 与 Retrieval 使用相同的
// 三行结构，保证两个面板行为一致。
std::optional<Msg> on_smart_mode(const FormFocus& f, const KeyEvent& ev) {
    // 再次按 ^S 关闭由它打开的面板。这是面板特有的快捷键，
    // 因为表单层不处理打开面板的组合键。
    if (const auto v = nav::char_view(ev); v && v->ctrl && v->c == U's')
        return Msg{CloseSmartMode{}};
    // ^E 显示或隐藏高级路由策略行，与 Rag
    // 面板的高级选项使用同一快捷键。
    // 单字母不再执行面板操作：在文本行输入文字，
    // 在其他行无操作。
    if (!f.editing)
        if (const auto v = nav::char_view(ev); v && v->ctrl && v->c == U'e')
            return Msg{SmartModeAdvanced{}};
    return on_form(f, ev, [](form::keys::Action a) { return Msg{SmartModeKey{a}}; });
}

// Web Search 是纯表单面板，无专用组合键。
// 最外层 Esc 返回打开它的设置列表。
std::optional<Msg> on_web_search(const FormFocus& f, const KeyEvent& ev) {
    return on_form(f, ev, [](form::keys::Action a) { return Msg{WebSearchKey{a}}; });
}

// 粘贴设置与 Web Search 一样，是纯表单面板。
std::optional<Msg> on_paste_settings(const FormFocus& f, const KeyEvent& ev) {
    return on_form(f, ev, [](form::keys::Action a) { return Msg{PasteSettingsKey{a}}; });
}

// 插件编辑器是纯表单面板，无专用组合键；
// Esc 通过导航关闭逻辑恢复底下的设置列表。
std::optional<Msg> on_plugin_edit(const FormFocus& f, const KeyEvent& ev) {
    return on_form(f, ev, [](form::keys::Action a) { return Msg{PluginEditKey{a}}; });
}

// Appearance 是表单面板，并有一个模态子界面：主题浏览器。
//
// 主题浏览器是可过滤列表，使用共享导航规则，
// 不再手写按键分支。NavSpec 让它自动支持
// PageUp/PageDn 和 Home/End。
// 打开时它独占键盘；由于有 filter_ch，
// 可打印字符用于查询，不会成为表单操作。
std::optional<Msg> on_appearance(const FormFocus& f, bool picking,
                                 const KeyEvent& ev) {
    if (picking) {
        nav::NavSpec s;
        s.close     = [] { return Msg{AppearanceThemeCancel{}}; };
        s.select    = [] { return Msg{AppearanceThemeCommit{}}; };
        s.move      = [](int d) { return Msg{AppearanceThemeMove{d}}; };
        s.filter_ch = [](char32_t c) {
            std::string t;
            maya::detail::encode_utf8(t, c);
            return Msg{AppearanceThemeQuery{std::move(t)}};
        };
        // 空文本就是 Backspace 信号。所有查询变化
        // 使用同一种消息，使状态更新函数只需在一处重新过滤和预览。
        s.filter_bs = [] { return Msg{AppearanceThemeQuery{""}}; };
        s.page_step = 10;
        // 模态界面会吞掉导航规则未认领的按键，
        // 避免意外落到列表下方的面板。
        if (auto msg = nav::translate(s, ev)) return msg;
        return Msg{NoOp{}};
    }
    return on_form(f, ev, [](form::keys::Action a) { return Msg{AppearanceKey{a}}; });
}

// 沙箱是普通表单面板，没有专用组合键。
//
// 此前设置 ^S 是为了防止输入到一半的端口
// 列表成为策略，但这混淆了保存配置和应用实时边界。
// 只有后者不应在会话中途发生；
// 策略在启动时已固定，面板也不会调用
// set_config()。保存本身只是文件写入，
// 额外要求快捷键反而容易丢失编辑。
//
// 因此修改会像其他面板一样
// 即时保存，
// 页脚仍明确提示重启后生效。
std::optional<Msg> on_sandbox(const FormFocus& f, const KeyEvent& ev) {
    return on_form(f, ev, [](form::keys::Action a) { return Msg{SandboxKey{a}}; });
}

std::optional<Msg> on_diff_review(const KeyEvent& ev) {
    if (std::holds_alternative<SpecialKey>(ev.key)) {
        auto sk = std::get<SpecialKey>(ev.key);
        switch (sk) {
            case SpecialKey::Escape: return CloseDiffReview{};
            case SpecialKey::Up:     return DiffReviewMove{-1};
            case SpecialKey::Down:   return DiffReviewMove{+1};
            case SpecialKey::Left:   return DiffReviewPrevFile{};
            case SpecialKey::Right:  return DiffReviewNextFile{};
            // Enter 接受当前差异块，作为主要操作，
            // 使只有 Return 和字母键的终端也能完成审查。
            case SpecialKey::Enter:  return AcceptHunk{};
            case SpecialKey::Tab:      return DiffReviewNextFile{};
            case SpecialKey::BackTab:  return DiffReviewPrevFile{};
            // 在当前聚焦差异块的限高正文中翻页。
            // 大型差异块可能超过 24 行窗口，否则第 25 行
            // 之后无法查看，用户只能盲目决定。
            case SpecialKey::PageDown: return DiffReviewScroll{+12};
            case SpecialKey::PageUp:   return DiffReviewScroll{-12};
            default: break;
        }
    }
    if (auto* ck = std::get_if<CharKey>(&ev.key)) {
        char32_t c = ck->codepoint;
        // 旧式终端用 0x01..0x1A 表示 Ctrl+字母，
        // 不带 Ctrl 修饰标志。统一还原为字母
        // 并视为按住 Ctrl，让批量快捷键不
        // 依赖键盘协议支持。
        const bool raw_ctrl = (c >= 0x01 && c <= 0x1A);
        if (raw_ctrl) c = U'a' + (c - 1);
        const bool ctrl = ev.mods.ctrl || raw_ctrl;

        // 批量操作可能具有破坏性或难以撤销，因此必须按住
        // Ctrl：Ctrl+A 接受全部，Ctrl+X 拒绝全部。
        // 普通 a/x 因而不会意外触发全部拒绝。
        if (ctrl) {
            switch (c) {
                case U'a': case U'A': return AcceptAllChanges{};
                case U'x': case U'X': return RejectAllChanges{};
                // 逐块决策也采用统一的组合键策略，
                // Enter 仍是主要的接受操作。
                case U'y': case U'Y': return AcceptHunk{};
                case U'n': case U'N': return RejectHunk{};
                // 再次按 ^R 关闭面板，提交语义与 Esc 相同。
                // 已做决定会保留，尚未决定的差异块继续等待处理。
                case U'r': case U'R': return CloseDiffReview{};
                // 在当前差异块正文中滚动半页。
                case U'd': case U'D': return DiffReviewScroll{+12};
                case U'u': case U'U': return DiffReviewScroll{-12};
                default: break;
            }
            return std::nullopt;   // 避免未处理的 Ctrl 组合键继续落入单个差异块的按键逻辑。
        }

        switch (c) {
            // 单个差异块的决策操作。上方处理组合键，
            // 这里也接受普通字母，因为此面板没有文本输入。
            // 即使终端复用器占用了 ^A / ^B 前缀，
            // 用户仍能接受差异块。
            case U'y': case U'Y': return AcceptHunk{};
            case U'n': case U'N': return RejectHunk{};
            default: break;
        }
    }
    return std::nullopt;
}

// 待办面板属于非独占覆盖面板，只处理 Esc 和自身的 ^T 开关。
// 其余按键经过分派器的 todo 分支继续交给 on_global。
std::optional<Msg> on_todo_modal(const KeyEvent& ev) {
    nav::NavSpec s;
    s.close = [] { return Msg{CloseTodoModal{}}; };
    s.toggle_close_chord = U't';
    return nav::translate(s, ev);
}

// Ctrl+O 工具输出查看器的列表和正文共用一个处理函数。列表中 ↑↓ / j
// / k 移动，Enter 打开正文，y 复制，Esc / q 关闭。正文中 ↑↓ /
// j / k / PgUp / PgDn / Home / End 滚动，y 复制，
// Esc 返回列表。状态更新函数依据 Open::viewing 分支，
// 同一组 Move / Copy 消息用于两个阶段。h / l 和 ← / → 在条目间切换。
std::optional<Msg> on_tool_viewer(const Model& m, const KeyEvent& ev) {
    using Op = ToolOutputFind::Op;
    const auto* o = m.ui.panel.get<ui::panel::ToolOutput>();
    // `/` 命令提示打开时拥有键盘输入。
    if (o && o->typing) {
        if (const auto* sk = std::get_if<SpecialKey>(&ev.key)) {
            switch (*sk) {
                case SpecialKey::Escape:    return Msg{ToolOutputFind{Op::Cancel}};
                case SpecialKey::Enter:     return Msg{ToolOutputFind{Op::Commit}};
                case SpecialKey::Backspace: return Msg{ToolOutputFind{Op::Erase}};
                case SpecialKey::Down:      return Msg{ToolOutputFind{Op::Next}};
                case SpecialKey::Up:        return Msg{ToolOutputFind{Op::Prev}};
                default:                    return std::nullopt;
            }
        }
        if (const auto* ck = std::get_if<CharKey>(&ev.key);
            ck && !ev.mods.ctrl && !ev.mods.alt && ck->codepoint >= 0x20)
            return Msg{ToolOutputFind{Op::Type, ck->codepoint}};
        return std::nullopt;
    }
    nav::NavSpec s;
    s.close     = [] { return Msg{CloseToolOutput{}}; };
    s.select    = [] { return Msg{ToolOutputSelect{}}; };
    s.move      = [](int d) { return Msg{ToolOutputMove{d}}; };
    s.page_step = 10;
    s.toggle_close_chord = U'o';
    s.extra     = [](const KeyEvent& e) -> std::optional<Msg> {
        if (const auto* sk = std::get_if<SpecialKey>(&e.key)) {
            if (*sk == SpecialKey::Left)     return Msg{ToolOutputStep{-1}};
            if (*sk == SpecialKey::Right)    return Msg{ToolOutputStep{+1}};
            if (*sk == SpecialKey::PageUp)   return Msg{ToolOutputMove{0, -1}};
            if (*sk == SpecialKey::PageDown) return Msg{ToolOutputMove{0, +1}};
            return std::nullopt;
        }
        // n 与 N 的含义不同，因此要在 letter_verbs 统一大小写之前匹配。
        if (const auto* ck = std::get_if<CharKey>(&e.key); ck && !e.mods.ctrl && !e.mods.alt) {
            if (ck->codepoint == U'/') return Msg{ToolOutputFind{Op::Open}};
            if (ck->codepoint == U'n') return Msg{ToolOutputFind{Op::Next}};
            if (ck->codepoint == U'N') return Msg{ToolOutputFind{Op::Prev}};
            if (ck->codepoint == U' ') return Msg{ToolOutputMove{0, +1}};
        }
        const auto v = nav::char_view(e);
        if (!v || !v->ctrl) return std::nullopt;   // 这里只处理组合键。
        switch (v->c) {
            case U'y': return Msg{ToolOutputCopy{}};
            case U'f': return Msg{ToolOutputFind{Op::Open}};
            default:   return std::nullopt;
        }
    };
    s.letter_verbs = { {U'y', [] { return Msg{ToolOutputCopy{}}; }} };
    return nav::translate(s, ev);
}

// 登录面板根据当前子状态分派按键。
// Picking 只接受 1 / 2 和用于关闭的 Esc。
// OAuthCode、ApiKeyInput 接受文本、光标移动和 Enter。
// OAuthExchanging 只接受 Esc，可取消并回到 Picking。
// Failed 接受任意按键返回 Picking。
std::optional<Msg> on_login(const ui::login::State& state, const KeyEvent& ev) {
    using namespace agentty::ui::login;

    // Esc 每次只返回一层，例如密钥提示 →
    // 主机输入 → 提供方选择 → 关闭。
    // 状态更新函数读取各子状态的 `back` 来源，
    // 无父状态时直接关闭。
    if (std::holds_alternative<SpecialKey>(ev.key)
        && std::get<SpecialKey>(ev.key) == SpecialKey::Escape)
        return LoginBack{};

    if (std::holds_alternative<Picking>(state)
        || std::holds_alternative<Failed>(state)) {
        if (auto* ck = std::get_if<CharKey>(&ev.key))
            return LoginPickMethod{ck->codepoint};
        return std::nullopt;
    }

    if (std::holds_alternative<OAuthExchanging>(state)
        || std::holds_alternative<ChatGptWaiting>(state)
        || std::holds_alternative<HostProbing>(state)) {
        // 正在等待异步结果，包括 HTTP 交换、
        // 本地回调或主机探测。只接受上方已经处理的 Esc。
        return NoOp{};
    }

    // ── 账号切换器是列表，不是文本输入框。 ────────────────
    // ↑ / ↓ 移动高亮项，Enter 切换到或添加选中账户。
    // 连续按两次 Delete / Backspace 或 ^D 确认删除。
    // 普通字母不生效，遵循统一按键策略。
    if (std::holds_alternative<AccountList>(state)) {
        if (std::holds_alternative<SpecialKey>(ev.key)) {
            switch (std::get<SpecialKey>(ev.key)) {
                case SpecialKey::Up:        return AccountMove{-1};
                case SpecialKey::Down:      return AccountMove{+1};
                case SpecialKey::Enter:     return AccountSelect{};
                case SpecialKey::Backspace: return AccountRemove{};
                case SpecialKey::Delete:    return AccountRemove{};
                default: return std::nullopt;
            }
        }
        if (const auto v = nav::char_view(ev); v && v->c == U'd')
            return AccountRemove{};   // 纯列表支持组合键或普通字母。
        return std::nullopt;
    }

    // ── 只在 OAuthCode 状态下启用的界面快捷键。 ─────────────────────────────
    // 普通 c / o 会与验证码输入冲突，
    // 因此只有按下修饰键或验证码字段为空时才处理。
    // 刚进入此界面时通常字段为空，
    // 用户可以先复制 URL，再粘贴验证码。
    if (auto* oc = std::get_if<OAuthCode>(&state)) {
        if (auto* ck = std::get_if<CharKey>(&ev.key)) {
            char32_t c = ck->codepoint;
            // 统一 Ctrl+字母的表示方式。根据是否启用 KKP /
            // modifyOtherKeys，终端可能发送原始控制字节 0x01..
            // 0x1A，也可能发送小写字母加 Ctrl 修饰标志。
            if (ev.mods.ctrl && c >= 0x01 && c <= 0x1A)
                c = U'a' + (c - 1);
            const bool empty_code = oc->code_input.empty();
            // 全局层保留 Ctrl+C 用于退出，
            // 因此使用 Ctrl+Y 复制 URL，
            // 避免冲突。验证码为空时普通 c / o 也有效，
            // 常见操作只需进入界面、按 c、
            // 到浏览器粘贴，无需修饰键。
            const bool is_y = (c == U'y' || c == U'Y');
            const bool is_c = (c == U'c' || c == U'C');
            const bool is_o = (c == U'o' || c == U'O');
            if (ev.mods.ctrl && is_y) return LoginCopyAuthUrl{};
            if (ev.mods.ctrl && is_o) return LoginOpenBrowserAgain{};
            if (empty_code && is_c)   return LoginCopyAuthUrl{};
            if (empty_code && is_o)   return LoginOpenBrowserAgain{};
        }
    }

    // Copilot 设备登录面板没有自由文本字段，
    // 验证码由用户在浏览器输入。因此 c 直接复制验证码，
    // o 重新打开浏览器，无需修饰键或 empty_code 条件。
    if (std::holds_alternative<DeviceWaiting>(state)) {
        // 设备登录面板（Copilot / Kimi 等）中的验证码
        // 在浏览器输入，没有文本字段，因此可直接使用普通字母。
        // c 复制验证码，u 复制 URL，o 重新打开浏览器。
        if (auto* ck = std::get_if<CharKey>(&ev.key)) {
            char32_t c = ck->codepoint;
            if (ev.mods.ctrl && c >= 0x01 && c <= 0x1A) c = U'a' + (c - 1);
            if (c == U'c' || c == U'C') return LoginCopyCode{};
            if (c == U'u' || c == U'U') return LoginCopyAuthUrl{};
            if (c == U'o' || c == U'O') return LoginOpenBrowserAgain{};
        }
    }

    // OAuthCode 和 ApiKeyInput 都接受自由文本输入。
    if (std::holds_alternative<SpecialKey>(ev.key)) {
        switch (std::get<SpecialKey>(ev.key)) {
            case SpecialKey::Enter:     return LoginSubmit{};
            case SpecialKey::Backspace: return LoginBackspace{};
            case SpecialKey::Left:      return LoginCursorLeft{};
            case SpecialKey::Right:     return LoginCursorRight{};
            default: return std::nullopt;
        }
    }
    if (auto* ck = std::get_if<CharKey>(&ev.key))
        if (ck->codepoint >= 0x20) return LoginCharInput{ck->codepoint};
    return std::nullopt;
}

std::optional<Msg> on_global(const KeyEvent& ev) {
    // 旧式终端（iSH、普通 xterm、
    // 未启用 KKP / modifyOtherKeys
    // 的 tmux）把 Ctrl+J 作为 LF 字节
    // 0x0A 发送。maya 将回车的 0x0D 与
    // 换行的 0x0A 都解析成 SpecialKey::Enter，
    // 导致 Ctrl+J 与回车看起来相同。
    // raw_sequence 保留原始字节，
    // 回车发送 \r，Ctrl+J 发送 \n，
    // 因此可以据此恢复 OpenThreadList 绑定。
    // 将回车映射为 LF 的终端无法区分两者，
    // 因而无法使用 Ctrl+J。
    if (std::holds_alternative<SpecialKey>(ev.key)
        && std::get<SpecialKey>(ev.key) == SpecialKey::Enter
        && !ev.mods.shift && !ev.mods.alt && !ev.mods.ctrl
        && ev.raw_sequence == "\n")
        return OpenThreadList{};
    // Alt+← / → 按 ^J 列表的排列顺序快速切换会话，
    // 无需打开选择面板。列表最新在前，← 切到更新的会话，→ 切到更旧的会话。
    // 在 on_global 中先于 on_composer 检查，
    // 避免与输入框的普通方向键或 Ctrl+方向键光标操作冲突。
    // 没有真实 Alt 键的终端（例如 iPhone
    // 终端）用 Esc 前缀模拟 Alt。
    // 因此主界面的独立 Esc 必须无副作用，
    // 否则先按 Esc 再按方向键会中途退出 agentty。
    // Ctrl+C 是唯一的应用退出键。
    if (ev.mods.alt && !ev.mods.ctrl
        && std::holds_alternative<SpecialKey>(ev.key)) {
        switch (std::get<SpecialKey>(ev.key)) {
            case SpecialKey::Left:  return ThreadCycle{-1};
            case SpecialKey::Right: return ThreadCycle{+1};
            default: break;
        }
    }
    if (auto* ck = std::get_if<CharKey>(&ev.key)) {
        char32_t c = ck->codepoint;
        // 旧式终端（iSH、普通 xterm、未启用 KKP
        // / modifyOtherKeys 的 tmux）直接
        // 发送 Ctrl+字母对应的 0x01..0x1A 控制字节，
        // 不设置 Ctrl 修饰标志。例如 Ctrl+K 到达时是
        // 0x0B。这里统一转换成隐含 Ctrl 的小写字母，
        // 使快捷键不依赖键盘协议。Ctrl+J / 0x0A 已在
        // 上方特殊处理，因为 maya 会先把它转换成 Enter。
        const bool raw_ctrl = (c >= 0x01 && c <= 0x1A);
        if (raw_ctrl) c = U'a' + (c - 1);
        // 旧式终端的 Ctrl+/ 以 0x1F 字节发送，与 Ctrl+_ 相同，
        // 没有 Ctrl 标志。启用扩展按键的 tmux 会将其编码为 ctrl+_。
        // 这里将两者都视为 ^/。
        const bool ctrl_slash = (c == 0x1F) || (ev.mods.ctrl && c == U'_');
        if (ctrl_slash) c = U'/';
        // 排除 0x09（Ctrl+I / Tab）和 0x0D（Ctrl+M / Enter）。
        // 旧式终端始终把它们当作 Tab / Enter，
        // 拦截它们会破坏补全和提交。
        const bool ctrl = ev.mods.ctrl || ctrl_slash
                       || (raw_ctrl && c != U'i' && c != U'm');
        if (ctrl) {
            switch (c) {
                case U'c': case U'C': return Quit{};
                case U'/':           return OpenModels{};
                case U'j': case U'J': return OpenThreadList{};
                case U'k': case U'K': return OpenPalette{};
                case U'p': case U'P': return OpenProviders{};
                case U'l': case U'L': return OpenStats{};
                // ^L 已用于统计面板，
                // 因此将重绘操作移到空闲的 ^Y。
                // 手动重绘用于修复损坏的屏幕，
                // 属于不常用的恢复操作。
                case U'y': case U'Y': return RedrawScreen{};
                case U'r': case U'R': return OpenDiffReview{};
                case U'n': case U'N': return NewThread{};
                case U't': case U'T': return OpenTodoModal{};
                case U'e': case U'E': return ComposerToggleExpand{};
                // ^B 开关控制 LOOP。发送当前消息，
                // 并在每轮结束后自动重发，直到关闭。
                // 这里使用全局处理，使流式响应期间、
                // 输入框未接收按键时也能停止循环。
                case U'b': case U'B': return ComposerToggleLoop{};
                case U'g': case U'G': return OpenCodeBlocks{};
                case U'o': case U'O': return OpenToolOutput{};
                case U's': case U'S': return OpenSmartMode{};
                default: break;
            }
        }
    }
    if (ev.mods.ctrl && std::holds_alternative<SpecialKey>(ev.key)) {
        auto sk = std::get<SpecialKey>(ev.key);
        // ^Tab 在最近使用记录中快速切回上一个 (provider, model)。
        if (sk == SpecialKey::Tab) return SwitchToPreviousModel{};
    }
    if (ev.mods.shift && std::holds_alternative<SpecialKey>(ev.key)) {
        auto sk = std::get<SpecialKey>(ev.key);
        if (sk == SpecialKey::Tab || sk == SpecialKey::BackTab)
            return CycleProfile{};
    }
    return std::nullopt;
}

// 主界面按键映射所依赖的输入框状态。
struct ComposerKeyState {
    bool text_empty;
    bool has_queued;
    bool in_history;     // 正在浏览历史用户消息，此时 ↓ 有意义。
    bool has_history;    // 存在历史用户消息，此时 ↑ 有意义。
    bool in_queue_peek;  // 正在原地编辑排队条目，使用 Alt+↑ / ↓ 切换。
    bool slash_menu = false;    // `/` 命令菜单正在显示。
    bool slash_exact = false;   // 输入的 /name 与某个已知命令名称完全匹配。
};

std::optional<Msg> on_composer(ComposerKeyState s, const KeyEvent& ev);

// `/` 菜单自己的按键。其余按键返回 nullopt，
// 继续按常规方式编辑输入框，从而在输入时持续过滤菜单。
std::optional<Msg> on_slash_menu(ComposerKeyState s, const KeyEvent& ev) {
    if (!s.slash_menu || ev.mods.alt || ev.mods.shift) return std::nullopt;
    if (const auto* sk = std::get_if<SpecialKey>(&ev.key)) {
        switch (*sk) {
            case SpecialKey::Up:     return Msg{ComposerSlashMove{-1}};
            case SpecialKey::Down:   return Msg{ComposerSlashMove{+1}};
            case SpecialKey::Tab:    return Msg{ComposerSlashComplete{}};
            case SpecialKey::Escape: return Msg{ComposerSlashDismiss{}};
            // 完整命令名继续走常规 Enter 执行。
            // 名称只输入一部分时先补全，避免 Enter 执行推测出来的命令。
            case SpecialKey::Enter:
                if (!s.slash_exact) return Msg{ComposerSlashComplete{}};
                return std::nullopt;
            default: return std::nullopt;
        }
    }
    if (const auto* ck = std::get_if<CharKey>(&ev.key)) {
        const char32_t c = ck->codepoint;
        if (c == 0x10 || (ev.mods.ctrl && (c == U'p' || c == U'P'))) return Msg{ComposerSlashMove{-1}};
        if (c == 0x0E || (ev.mods.ctrl && (c == U'n' || c == U'N'))) return Msg{ComposerSlashMove{+1}};
    }
    return std::nullopt;
}

std::optional<Msg> on_composer(ComposerKeyState s, const KeyEvent& ev) {
    if (auto msg = on_slash_menu(s, ev)) return msg;
    if (std::holds_alternative<SpecialKey>(ev.key)) {
        auto sk = std::get<SpecialKey>(ev.key);
        switch (sk) {
            case SpecialKey::Enter:
                // Shift+Enter 插入换行，
                // 同时提供 Alt+Enter 作为不支持 KKP /
                // modifyOtherKeys 的终端的通用回退。
                // maya 在进入时启用两种协议，但终端可能忽略它们，
                // 导致 Shift+Enter 退化为普通 Enter。
                // 这时 Alt+Enter 仍可插入换行，
                // 因为旧式终端普遍以 `\x1b\r` 发送它。
                // 普通 Enter 仍然提交消息。
                return (ev.mods.shift || ev.mods.alt)
                       ? Msg{ComposerNewline{}}
                       : Msg{ComposerEnter{}};
            case SpecialKey::Backspace:
                // 输入框为空且未预览任何排队消息时，
                // Alt+Backspace 撤销最近一次入队，移除队列末尾的消息。
                // 用于代理仍在忙碌时，
                // 用户刚将消息排队又立即反悔的情况。
                if (ev.mods.alt && s.text_empty && s.has_queued && !s.in_queue_peek)
                    return Msg{ComposerQueuePopLast{}};
                return ComposerBackspace{};
            case SpecialKey::Left:
                // Ctrl+Left 按词向左移动，与 readline
                // 和常见编辑器一致。普通 Left 按字符移动，并识别附件标签。
                if (ev.mods.ctrl) return ComposerCursorWordLeft{};
                return ComposerCursorLeft{};
            case SpecialKey::Right:
                if (ev.mods.ctrl) return ComposerCursorWordRight{};
                return ComposerCursorRight{};
            case SpecialKey::Home:      return ComposerCursorHome{};
            case SpecialKey::End:       return ComposerCursorEnd{};
            case SpecialKey::Up:
                // Alt+↑ 进入逐条队列编辑，
                // 优先于其他 ↑ 绑定，
                // 因此编辑输入框时也能
                // 直接修正已排队消息，
                // 无需先清空草稿。
                if (ev.mods.alt && s.has_queued)
                    return Msg{ComposerQueuePeekPrev{}};
                // ↑ 的优先级依次为：
                //   1. 队列非空且输入框为空时，取回排队消息。
                //      对应 Claude Code 的“按上箭头
                //      编辑排队消息”提示，二进制偏移为 84602515 /
                //      76303220).
                //   2. 已在浏览历史时，继续向更早的
                //      历史消息移动。
                //   3. 输入框为空且存在至少一条先前的
                //      用户消息时，开始浏览历史。
                // 其他情况，例如编辑多行文本，
                // 继续交给后续处理，让 ↑ 仍可用于移动光标。
                if (s.text_empty && s.has_queued)
                    return Msg{ComposerRecallQueued{}};
                if (s.in_history) return ComposerHistoryPrev{};
                if (s.text_empty && s.has_history) return ComposerHistoryPrev{};
                return std::nullopt;
            case SpecialKey::Down:
                // Alt+↓ 从队列逐条预览
                // 返回实时草稿。只有正在预览时才处理，
                // 否则继续传递。
                if (ev.mods.alt && s.in_queue_peek)
                    return Msg{ComposerQueuePeekNext{}};
                // ↓ 只在浏览历史时生效，
                // 朝实时草稿方向返回；
                // 其他情况继续传递。
                if (s.in_history) return ComposerHistoryNext{};
                return std::nullopt;
            case SpecialKey::Escape:
                // 主界面上 Esc 不执行操作，
                // 退出仅使用 Ctrl+C。原因是 Esc
                // 经常用于关闭临时界面，立即退出会丢失会话；
                // 此外 iPhone 终端如 iSH、
                // Termius、a-Shell 没有 Alt 键，
                // 会用 Esc 后接方向键模拟 Alt+←/→，
                // 前半个 Esc 不能触发退出。
                // Esc 仍可取消流式轮次，
                // 由上方分派处理；也可关闭各模态界面，
                // 由各自处理函数负责。
                return std::nullopt;
            default: return std::nullopt;
        }
    }
    if (auto* ck = std::get_if<CharKey>(&ev.key)) {
        // 带 Ctrl 的字母按编辑器控制键处理，先于可打印文本分支判断，
        // 使 CharKey{0x1A} 和 CharKey{
        // 'z'}+ctrl 两种 Ctrl+Z 形式均能识别。
        if (ev.mods.ctrl && !ev.mods.alt) {
            char32_t c = ck->codepoint;
            // 部分终端用 0x01..0x1A 发送 ASCII 控制键，
            // KKP / modifyOtherKeys 等协议则保留小写字母并设置
            // mods.ctrl=true；这里统一处理。
            if (c >= 0x01 && c <= 0x1A) c = U'a' + (c - 1);
            switch (c) {
                case U'u': return ComposerKillToBeginningOfLine{};
                case U'w':
                    // Ctrl+W 向后删除一个词，
                    // 对应 readline 的 unix-word-rubout，
                    // 是常见 shell 和编辑器通用的删词键。
                    return ComposerDeleteWordBack{};
                case U'z':
                    // Ctrl+Shift+Z 是另一种重做绑定，
                    // 符合 macOS 习惯；普通 Ctrl+Z 执行
                    // 撤销。
                    return ev.mods.shift ? Msg{ComposerRedo{}}
                                         : Msg{ComposerUndo{}};
                case U'y': return ComposerRedo{};
                case U'v':
                    // Ctrl+V 从剪贴板粘贴图像。
                    // Linux/macOS 的终端通常
                    // 发送原始字节 0x16；
                    // Windows Terminal
                    // 默认将其绑定为终端自己的粘贴操作，
                    // 会先吞掉按键，因此下方也支持 Alt+V，
                    // 并在 update/composer.cpp
                    // 中检测空的括号粘贴事件。
                    return ComposerImagePasteFromClipboard{};
                default: break;
            }
        }
        // Alt+V 是另一种图像粘贴触发方式，
        // Windows Terminal 等终端都会把它
        // 传给应用。它与 Ctrl+V 产生相同 Msg，
        // 状态更新函数不区分来源。
        if (ev.mods.alt && !ev.mods.ctrl) {
            char32_t c = ck->codepoint;
            // Alt+V 以 ESC v 到达，映射为 CharKey{'v'}
            // + mods.alt。部分终端同时按 Shift 时会发大写字母，
            // 因此 V 和 v 都应触发。
            if (c == U'v' || c == U'V')
                return ComposerImagePasteFromClipboard{};
            // Alt+D 向前删除一个词，对应 readline 的 kill-word。
            // 与 Ctrl+W 对称，以 ESC d 到达并映射为 CharKey{'d'}+alt。
            if (c == U'd' || c == U'D')
                return ComposerDeleteWordForward{};
            // Alt+K 删除至行尾。readline
            // 原本使用 Ctrl+K，但应用已用它
            // 打开命令面板，on_global 会
            // 优先认领，因此改用 Alt 组合键。
            // 它与删除至行首的 Ctrl+U 对应，
            // 类似 Alt+D 与 Ctrl+W 的前后删词
            // 关系。
            if (c == U'k' || c == U'K')
                return ComposerKillToEndOfLine{};
        }
        if (ck->codepoint >= 0x20) return ComposerCharInput{ck->codepoint};
    }
    return std::nullopt;
}

} // namespace

std::optional<Msg> on_key(const Model& m, const KeyEvent& ev) {
    // 键盘所属覆盖界面由 panel::top() 决定；
    // 视图也用同一函数选择渲染界面，
    // 使按键和画面始终对应。
    const auto active_panel = ui::panel::top(m);
    const bool settings_list_adding = [&] {
        const auto* so = m.ui.panel.get<pn::SettingsList>();
        return so && so->input_active;
    }();
    const bool streaming  = m.s.active()
                         && !m.s.is_awaiting_permission();
    // Ctrl+←/→ 切换会话的前提：代理轮次必须完全空闲。
    // 这与 streaming 判定不同，后者特意排除等待权限的阶段，
    // 以便 Esc 保持取消语义。只要轮次未结束，
    // 无论是否显示权限提示，都不允许切换会话。
    const bool turn_active = m.s.active();
    bool has_history = false;
    for (const auto& msg : m.d.current.messages)
        if (msg.role == Role::User && !msg.text.empty()) { has_history = true; break; }
    // 最新的、尚未冻结的实时检索上下文卡片，
    // 是 Ctrl+U 展开的目标。如果最近的主动检索
    // 卡片已并入冻结前缀，或不存在卡片，则为空。
    // 冻结卡片的 Element 已固定，无法通过该状态更新展开；
    // 完整内容改用 Ctrl+O 覆盖面板查看。
    std::optional<MessageId> live_retrieved_id;
    for (std::size_t i = m.d.current.messages.size(); i-- > m.ui.frozen_through; ) {
        if (m.d.current.messages[i].is_proactive_context()) {
            live_retrieved_id = m.d.current.messages[i].id;
            break;
        }
    }
    const auto slash_cat  = slash::catalog_of(m);
    const auto slash_rows = slash::menu_rows(m, slash_cat);
    const bool slash_exact = !slash_rows.empty() && [&] {
        const auto q = slash::menu_query(m.ui.composer.text, m.ui.composer.cursor);
        return q && slash_rows.front().entry->name == q->query;
    }();
    const ComposerKeyState composer_state{
        m.ui.composer.text.empty(),
        !m.ui.composer.queued.empty(),
        m.ui.composer.history_index().has_value(),
        has_history,
        m.ui.composer.queue_peek_index().has_value(),
        !slash_rows.empty(),
        slash_exact,
    };

    // 改动条的批量快捷键 ^A / ^X。
    const bool chords_armed = strip_chords_armed(m);

    // 各表单面板当前所处的模式。
    FormFocus rag_form, smart_form_snap, plugin_form_snap, appearance_form_snap;
    FormFocus sandbox_form_snap, web_search_form_snap, paste_form_snap;
    bool appearance_picking = false;
    if (const auto* o = m.ui.panel.get<ui::panel::Rag>())
        rag_form = focus_of(o->embed.form);
    if (const auto* o = m.ui.panel.get<ui::panel::SmartMode>())
        smart_form_snap = focus_of(o->form);
    if (const auto* o = m.ui.panel.get<ui::panel::WebSearch>())
        web_search_form_snap = focus_of(o->form);
    if (const auto* o = m.ui.panel.get<ui::panel::PasteSettings>())
        paste_form_snap = focus_of(o->form);
    if (const auto* o = m.ui.panel.get<ui::panel::PluginEdit>())
        plugin_form_snap = focus_of(o->form);
    if (const auto* o = m.ui.panel.get<ui::panel::Appearance>()) {
        appearance_form_snap = focus_of(o->pane.form);
        appearance_picking   = o->pane.picking;
    }
    if (const auto* o = m.ui.panel.get<ui::panel::Sandbox>())
        sandbox_form_snap = focus_of(o->pane.form);
    FormFocus sandbox_list_snap;
    if (const auto* o = m.ui.panel.get<ui::panel::SandboxList>())
        sandbox_list_snap = focus_of(o->pane.form);

            // ^C 在覆盖界面路由之前处理，
            // 确保任何位置都能退出。每个模态选择器的
            // 处理结果都会直接返回，不继续向下传递；
            // 若缺少此处，选择器、会话列表或命令面板会吞掉 ^C，
            // 让用户必须先按 Esc 才能退出。
            // on_permission 原先已有局部处理，提升到这里后统一
            // 覆盖全部界面。旧式终端发送原始字节 0x03。
            if (auto* ck = std::get_if<CharKey>(&ev.key)) {
                const char32_t c = ck->codepoint;
                if (c == 0x03 || (ev.mods.ctrl && (c == U'c' || c == U'C')))
                    return Quit{};
            }
            // 登录模态界面独占键盘。认证是前置步骤，
            // 用户完成登录或按 Esc 退出前无法操作
            // 其他界面；退出后 agentty 仍未认证。
            // 将事件交给当前覆盖界面的处理函数。
            // Kind 的 switch 由 -Wswitch
            // 检查穷尽性，新增界面而漏写分支会产生编译警告。
            // Kind::Todo 是唯一允许未认领按键
            // 继续进入全局处理的环境式覆盖界面。
            using OK = ui::panel::Kind;

            // ── 保证用户能够退出。 ──────────────────────────────────
            // 独占覆盖面板会吞掉未处理按键。
            // 如果处理函数遗漏 Esc，
            // 用户就无法退出，应用会看起来像卡住。
            // 检索面板曾出现过这个问题。表单还未创建时就
            // 已经获得键盘所有权，导致包括 Esc 在内
            // 的所有按键都没有作用。
            //
            // 为避免每个处理函数分别维护同样的保障，
            // 在此统一执行。dispatch 先
            // 运行面板自己的处理函数。如果它拒绝处理
            // Escape，就调用面板的通用关闭操作。
            // 这样即使面板有缺陷、尚未完整构造或
            // 刚刚新增，也不会把用户困在里面。
            //
            // 非独占覆盖面板（Todo）会把未处理按键交给
            // 全局逻辑，因而不适用独占面板的吞键规则。
            const auto dispatch = [&]() -> std::optional<Msg> {
                switch (active_panel) {
                    case OK::Login:          return on_login(m.ui.login, ev);
                    case OK::Permission:     return on_permission(ev);
                    case OK::Palette: return on_command_palette(ev);
                    case OK::Mention:        return on_mention_palette(ev);
                    case OK::Symbol:         return on_symbol_palette(ev);
                    case OK::CodeBlocks:     return on_code_block_picker(ev);
                    case OK::CodeBlockResult: return on_code_block_result(ev);
                    case OK::ToolOutput:     return on_tool_viewer(m, ev);
                    case OK::Checkpoints:    return on_checkpoint_picker(ev);
                    case OK::Rag:    return on_rag_settings(rag_form, ev);
                    case OK::Stats:          return on_stats_viewer(ev);
                    case OK::Skills:         return on_skills_viewer(ev);
                    case OK::SettingsList: {
                        const auto* so = m.ui.panel.get<pn::SettingsList>();
                        const bool hub = so && so->concern == settings::Category::General;
                        return on_settings_list(ev, settings_list_adding, hub,
                                                so && !so->query.empty());
                    }
                    case OK::Fork:           return on_fork_picker(ev);
                    case OK::Models:    return on_fused_picker(ev);
                    case OK::Providers: return on_provider_picker(ev);
                    case OK::ThreadList: {
                        const auto* tl = m.ui.panel.get<pn::ThreadList>();
                        return on_thread_list(ev, tl && !tl->renaming.empty());
                    }
                    case OK::SmartMode:      return on_smart_mode(smart_form_snap, ev);
                    case OK::WebSearch:      return on_web_search(web_search_form_snap, ev);
                    case OK::PasteSettings:  return on_paste_settings(paste_form_snap, ev);
                    case OK::PluginEdit:     return on_plugin_edit(plugin_form_snap, ev);
                    case OK::Appearance:
                        return on_appearance(appearance_form_snap,
                                             appearance_picking, ev);
                    case OK::Sandbox:        return on_sandbox(sandbox_form_snap, ev);
                    // 路径列表编辑器是普通表单面板，
                    // 没有专用组合键。Esc 已按表单
                    // 规则提交修改，无需再单独设置保存按键。
                    case OK::SandboxList:
                        return on_form(sandbox_list_snap, ev,
                                       [](form::keys::Action a) {
                                           return Msg{SandboxListKey{a}};
                                       });
                    case OK::DiffReview:     return on_diff_review(ev);
                    case OK::Todo:           return on_todo_modal(ev);
                    case OK::None:           break;
                }
                return std::nullopt;
            };

            if (auto r = dispatch()) return r;

            // Todo 是非独占面板，未处理按键继续
            // 进入下方全局处理，不会被吞掉，
            // 因此不应用独占面板的保障逻辑。
            if (active_panel != OK::None && active_panel != OK::Todo) {
                if (std::holds_alternative<SpecialKey>(ev.key)
                    && std::get<SpecialKey>(ev.key) == SpecialKey::Escape)
                    return ui::panel::close_msg(active_panel);
                // 独占覆盖面板吞掉未处理的非 Esc 按键，
                // 以维持模态输入行为。
                return Msg{NoOp{}};
            }
            // 斜杠菜单属于输入框。显示时，Esc 隐藏、
            // ^N/^P 移动等按键先于流式取消和全局快捷键处理；
            // 再按一次 Esc 仍可取消轮次。
            if (active_panel == OK::None)
                if (auto msg = on_slash_menu(composer_state, ev)) return msg;
            // 实时流期间，Esc 取消请求而不退出。
            // 上方模态界面各自吞掉 Esc，
            // 因此这里只在普通输入框界面触发。
            if (streaming
                && std::holds_alternative<SpecialKey>(ev.key)
                && std::get<SpecialKey>(ev.key) == SpecialKey::Escape)
                return CancelStream{};
            // Ctrl+←/→ 快速切换会话，顺序与 Alt+←/
            // → 相同：← 更新、→ 更旧。仅在输入框为空时启用：
            // 输入框有文字时，Ctrl+方向键
            // 保留 readline 的按词移动语义，
            // 由 on_composer 处理。
            // 代理轮次运行期间也不会切换，
            // 以免在实时流下替换当前会话。
            if (!turn_active && composer_state.text_empty
                && ev.mods.ctrl && !ev.mods.alt
                && std::holds_alternative<SpecialKey>(ev.key)) {
                switch (std::get<SpecialKey>(ev.key)) {
                    case SpecialKey::Left:  return ThreadCycle{-1};
                    case SpecialKey::Right: return ThreadCycle{+1};
                    default: break;
                }
            }
            // 输入框为空时，Ctrl+U 展开或收起最新
            // 的检索上下文卡片，在完整段落和单行摘要间切换。
            // 限制为空输入框，避免抢占输入时
            // Ctrl+U 删除至行首的语义。
            // 没有实时卡片则继续传递。这里需要预先计算的目标标识，
            // 因此不放在 on_global 中。
            if (composer_state.text_empty && live_retrieved_id
                && ev.mods.ctrl && !ev.mods.alt) {
                if (auto* ck = std::get_if<CharKey>(&ev.key)) {
                    char32_t c = ck->codepoint;
                    if (c >= 0x01 && c <= 0x1A) c = U'a' + (c - 1);
                    if (c == U'u')
                        return ToggleRetrievedExpanded{*live_retrieved_id};
                }
            }
            if (auto msg = on_global(ev)) return msg;

            // 改动条批量快捷键：^A 接受全部，文件字节已由
            // 工具写入，因此不执行破坏性修改；^X 拒绝全部
            // 并回滚磁盘文件，所以状态更新函数要求两次按键确认。
            //
            // 必须按住 Ctrl，
            // 与 on_diff_review 内的
            // 规则一致。批量决定不能放在单字母上，
            // 面板内外相同行为也应使用相同快捷键；
            // 普通 x 尤其容易在焦点假设变化时误触。
            //
            // 只在完全没有覆盖界面的主界面启用，包括 Todo 也不能打开，
            // 确保模态界面拥有自己的按键规则。其余启用条件见
            // strip_chords_armed()。
            if (chords_armed && active_panel == OK::None) {
                if (auto* ck = std::get_if<CharKey>(&ev.key); ck && !ev.mods.alt) {
                    char32_t c = ck->codepoint;
                    // 旧式终端不报告修饰键，只发送 Ctrl
                    // +字母对应的原始控制字节。这里与全局和
                    // 面板内路由一样统一格式，
                    // 保证不支持键盘协议时也能使用快捷键。
                    const bool raw_ctrl = (c >= 0x01 && c <= 0x1A);
                    if (raw_ctrl) c = U'a' + (c - 1);
                    if (ev.mods.ctrl || raw_ctrl) {
                        if (c == U'a' || c == U'A')
                            return Msg{AcceptAllChanges{}};
                        if (c == U'x' || c == U'X')
                            return Msg{RejectAllChanges{}};
                    }
                }
            }

            return on_composer(composer_state, ev);
}

Msg on_paste(const Model& m, const maya::PasteEvent& pe) {
    const auto active_panel = ui::panel::top(m);
    const auto* so     = m.ui.panel.get<pn::SettingsList>();
    const auto* tl     = m.ui.panel.get<pn::ThreadList>();
    const bool filter_open = active_panel == ui::panel::Kind::Palette
                          || active_panel == ui::panel::Kind::Models
                          || active_panel == ui::panel::Kind::Providers
                          || active_panel == ui::panel::Kind::Mention
                          || active_panel == ui::panel::Kind::Symbol
                          || active_panel == ui::panel::Kind::ThreadList
                          || (so && so->concern == settings::Category::General);
    std::string s = pe.content;
    // 粘贴内容交给当前文本输入的所有者。可能是登录面板、
    // 设置列表新增提示、表单面板的当前行、过滤条件输入框，
    // 或主输入框。每个表单面板会像处理普通输入那样为当前行启动编辑。
    // 所有表单面板都必须覆盖。过去遗漏面板时，内容会被粘贴到
    // 其后方隐藏的主输入框。
    if (active_panel == ui::panel::Kind::Login) return LoginPaste{std::move(s)};
    if (so && so->input_active)                 return SettingsListPaste{std::move(s)};
    if (active_panel == ui::panel::Kind::ThreadList && tl && !tl->renaming.empty())
        return ThreadListRenamePaste{std::move(s)};
    if (filter_open) return PanelFilterPaste{std::move(s)};
    if (m.ui.panel.get<ui::panel::Rag>())         return RagEmbedPaste{std::move(s)};
    if (m.ui.panel.get<ui::panel::SmartMode>())   return SmartModePaste{std::move(s)};
    if (m.ui.panel.get<ui::panel::WebSearch>())   return WebSearchPaste{std::move(s)};
    if (m.ui.panel.get<ui::panel::PasteSettings>()) return PasteSettingsPaste{std::move(s)};
    if (m.ui.panel.get<ui::panel::PluginEdit>())  return PluginEditPaste{std::move(s)};
    if (m.ui.panel.get<ui::panel::Appearance>())  return AppearancePaste{std::move(s)};
    if (m.ui.panel.get<ui::panel::SandboxList>()) return SandboxListPaste{std::move(s)};
    if (m.ui.panel.get<ui::panel::Sandbox>())     return SandboxPaste{std::move(s)};
    if (filter_open) return PanelFilterPaste{std::move(s)};
    // 未使用括号标记的粘贴分段到达时，后续片段扩展首段创建的附件标签。
    if (pe.continues) return ComposerPaste{std::move(s), /*path_checked=*/true, /*literal=*/false,
                                           /*continues_paste=*/true};
    return ComposerPaste{std::move(s)};
}

// 终端窗口焦点（?1004）控制真实光标。失去焦点时，
// 输入框停止输出光标锚点，硬件光标随之隐藏。
Msg on_focus(const maya::FocusEvent& fe) noexcept { return TerminalFocus{fe.focused}; }

// 宿主在连接时以及每次 SIGWINCH 时报告终端尺寸。状态更新函数
// 从 Model 读取行列信息，无需直接查询 tty。
Msg on_resize(const maya::ResizeEvent& re) noexcept {
    return TerminalResized{re.width.value, re.height.value};
}

Sub subscribe(const Model& m) {
    // Tick 仅在存在动画时唤醒循环。fps=0 时其余操作
    // 完全由事件驱动。周期选择，以及 SSH 和不支持同步输出的终端为何不同，
    // 见 streaming_tick_period()。
    // animation_demand() 统一定义本定时器、Tick 分支（
    // update/meta.cpp）以及 visual_hash（app/program.hpp）
    // 使用的动画需求条件。各项原因见上方定义。
    //
    // Tick 周期依据宿主终端是否支持 DEC
    // 2026 同步输出决定。能原子缓冲整帧时，
    // 33 ms（约 30 fps）可保持加载动画平滑；
    // Apple Terminal、普通 xterm
    // 和未透传同步序列的 tmux 等逐字节绘制
    // 终端改用 100 ms（10 fps），
    // 把闪烁频率降为约三分之一。启动时只探测一次，
    // 见 maya::ansi::env_supports_synchronized_output
    // ()。
    //
    // SSH 场景的瓶颈在传输链路。
    // 每帧产生数 KB 的 ANSI 差异，
    // 30 fps 时约为 290 KB/s，
    // 可能耗尽高延迟或低带宽连接，
    // 造成轮次中卡顿和结束后的追赶式重绘。
    // 因此远程 Tick 至少为 80
    // ms（约 12 fps），
    // 降低持续字节率；SSH 往返延迟已
    // 主导流畅度，少量降帧通常不明显。
    // 取本地周期与 SSH 下限中较慢者，
    // 因此已经使用 100 ms 的非同步
    // 终端不会反而加速。逐字显示按字节每秒推进，
    // 速度不变，只是帧数更少、
    // 每帧增量更大。
    // 收尾冻结由 meta.cpp 的 Tick 执行，
    // 对应 agent_session 的 MessageStop：m.s.is_idle
    // () 且 pending_settle_freeze 为真时触发。
    // 如果只根据 active() 或实时字节启用 Tick，
    // 最后字节并入稳定正文时计时就会停止，但组件的 live_ 尚未关闭，
    // pending_settle_freeze 仍等待处理。
    // 延迟冻结因此卡到下一次按键，
    // 再与过期 prev_cells 比较，
    // 引发缓存未命中和整轮重绘，导致回滚缓冲区中的重复内容。
    // 应持续发送 Tick，直到冻结执行并清除标志，
    // 使实时尾部到冻结前缀的交接发生在新帧上，
    // 等效于 agent_session 持续 30 fps 的行为。
    //
    // 还必须包含逐字显示尚在滑动的情况，否则会出现
    // CPU 占用很低但显示卡住的问题。maya 的
    // reveal_fx 按实际经过时间推进：即使
    // 传输结束，m.s.active() 和
    // tail_has_live_bytes 都为
    // false，光标仍可能以约 90 字符每秒
    // 显示最后一段，长回复尚需数秒。若此时停止 Tick，
    // 显示会冻结到下一次按键再瞬间跳到末尾。!
    // live_tail_reveal_settled
    // (m) 恰好覆盖 is_live、
    // reveal_in_progress、
    // is_finalizing、is_parsing 等未完成状态，
    // 应据此持续唤醒，直到显示追上实时边界；
    // build_live_tail 和延迟冻结也使用同一判定。

    // ── 登录工作任务。 ────────────────────────────────────────────
    //
    // 设备登录和 ChatGPT 登录会阻塞轮询提供方，最长 900 秒。
    // 原先使用不受状态约束的一次性 Cmd 时，Esc 虽然关闭面板，
    // 工作任务仍会耗尽整个等待预算，导致每次尝试都遗留一个线程。
    //
    // 改成带来源键的订阅后，登录状态存在时才
    // 请求数据流。Esc、成功或新尝试使旧
    // 状态及其键消失，jaal 随即触发
    // 任务的 stop_token，
    // 将关闭面板与取消任务统一起来。
    // jaal 还会丢弃已停止代次的消息，
    // 避免迟到结果写入新模型。
    //
    // 在下方 Tick 条件之前计算，并包含在两个返回分支中。
    // 登录可以与活动动画并存，若动画分支漏掉登录订阅，
    // 就会在登录中途停止工作任务。
    Sub login_sub = Sub::none();
    if (const auto* dw = std::get_if<ui::login::DeviceWaiting>(&m.ui.login))
        login_sub = cmd::device_login_sub(dw->provider, dw->provider_label,
                                          dw->attempt_id);
    else if (const auto* cw = std::get_if<ui::login::ChatGptWaiting>(&m.ui.login))
        login_sub = cmd::codex_login_sub(cw->attempt_id);

    // Tick 驱动所有基于时间的动画。三个控制点必须对哪些动画
    // 正在运行保持一致，因此共享 animation_demand
    // （subscribe.hpp）。本订阅启用定时器，
    // update/meta.cpp 的 Tick 分支推进动画状态，
    // app/program.hpp 的 visual_hash
    // 决定唤醒是否进入 view()。各条件的说明见上方定义。
    if (animation_demand(m))
        return Sub::batch(Sub::every(streaming_tick_period(m.env), Tick{}),
                          std::move(login_sub));
    return login_sub;
}

// 只包含 subscribe() 读取的全部状态，详见 SubsKey。
SubsKey subs_key(const Model& m) noexcept {
    SubsKey k;
    k.animation_demand = animation_demand(m);
    k.tick_period      = streaming_tick_period(m.env);

    // 直接从 m.ui.login 读取登录工作任务的来源键，
    // 不要求登录面板当前拥有键盘。即使其他覆盖面板位于上方，
    // 等待中的登录仍继续轮询。
    if (const auto* dw = std::get_if<ui::login::DeviceWaiting>(&m.ui.login))
        k.login_worker = {dw->attempt_id, dw->provider};
    else if (const auto* cw = std::get_if<ui::login::ChatGptWaiting>(&m.ui.login))
        k.login_worker = {cw->attempt_id, std::string{"codex"}};

    return k;
}

} // namespace agentty::app
