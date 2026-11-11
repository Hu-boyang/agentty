#pragma once
// visual_parts为面板域——每种类型的结构帧-hash行走(
// visual.hpp)不能auto-decompose，各自具有完整性证明。
//
// 零件清单是类型的VISIBILITY DECISION，每个base/member一行：
// 步行它，投影它（长度、计数），或者用理由visual::exempt它。
// static_assert(parts_cover_all<T>)
// 在没有决定类型的情况下添加成员时触发，而不是稍后错过的重绘。
//
// 仅由program.hpp（哈希站点）收录；其他地方都是零成本。

#include "agentty/domain/catalog.hpp"
#include "agentty/mcp/plugin_model.hpp"
#include "agentty/runtime/login.hpp"
#include "agentty/runtime/panel/slot.hpp"
#include "agentty/runtime/visual.hpp"

#include <maya/core/scroll_state.hpp>

// ── 统计查看器: 来源 缓存 是那个 加载-bearing 豁免 ──────
namespace agentty::stats_panel {

// VISUAL输入是选择哪个选项卡以及正文
// 滚动到的位置。投影是豁免的，这是 帧 哈希
// 值的有趣情况，因此值得精确说明：
//
// `projection`是抄本的DERIVED 缓存，
// 在渲染期间懒惰地刷新。散列它在两个方向上都是错误的。
// 正确性错误：哈希值会在 缓存 REFRESHES 时发生变化，
// 而不是在显示发生变化时发生变化 - 并且由于刷新发生在 渲染 期间，
// 因此哈希值会追逐自己的 末尾。成本错误：按 帧
// 进行每个计数正是增量 归并 需要避免的每 -frame
// 工作。
//
// 没有任何损失，因为投影是抄本的纯函数，
// 并且抄本已经通过对话自身的部分进行了散列。
// 消息着陆会通过该路线碰撞 帧 哈希值，然后面板将
// 归并 转换为新的 末尾 — 因此显示仍然会更新，
// 而 缓存 不会成为哈希值的输入。
//
// `scroll`在这里是因为面板OWNS吧。
// 当它存在于 Model::UI 时，进入大门的唯一路线是
// program.hpp 中的一条手 -written mix()
// 线 - 并且该面板的线从未被写入，因此每个 StatsScroll
// 都会产生一个名为相同的门模型，而 帧 被跳过。仅当不相关的散列
// 轴翻转时，偏移量才会到达屏幕：光标 闪烁或下一次击键。
// 拥有它使得遗漏无法代表 - parts_cover_all 计算成员。
inline auto visual_parts(const Open& p) {
    return std::make_tuple(static_cast<std::uint8_t>(p.tab),
                           visual::ref(p.scroll),
                           visual::exempt);  // 投影：源自消息
}
static_assert(visual::parts_cover_all<Open>);

} // namespace agentty::stats_panel

// ── 技能查看器:扫描是快照,不是散列输入 ──────────
namespace agentty::skills_panel {

// VISUAL输入是选择哪一行以及滚动正文的位置。
// `rows` 因与统计预测不同的原因
// 而被豁免，并且差异值得说明：
//
// 统计数据的投影被刷新DURING 渲染，因此对其进行散列将是
// 追逐其自己的末尾的散列。技能的行在打开时被填充为 一次，
// 并且在面板存在期间再也不会被触及 - 因此它们不能在 帧
// 之间更改，并且对它们进行散列将在每个 帧 向 re
// -walk 每个技能的发现付费，但什么也学不到。
// 重新扫描意味着新的面板值，该槽位自己的标识已经覆盖了该面板值。
inline auto visual_parts(const Open& p) {
    return std::make_tuple(p.index,
                           visual::ref(p.scroll),
                           visual::exempt);  // 行：快照，固定为打开
}
static_assert(visual::parts_cover_all<Open>);

} // namespace agentty::skills_panel

// ── 窗体: 密钥是 加载-bearing 豁免 ─────────────────
namespace agentty::form::field {

// LENGTH 仅。 凭据 的 字节 绝不能到达任何散列 - 散列是
// 一个信息通道，并且此投影是类型 -level 保证步行保留。
// （Same-length 覆盖对门来说是不可见的；通过单次编辑
// 无法访问，这也会将光标移动到下面。）占位符不是 秘密（绘制为
// -is、"✓ key saved"），并且是整个走的：从
// "not set" 翻转到 "key saved" 的行必须是 重绘。
inline auto visual_parts(const Secret& s) {
    return std::make_tuple(s.value.size(), s.cursor, visual::ref(s.placeholder));
}
static_assert(visual::parts_cover_all<Secret>);

} // namespace agentty::form::field

namespace agentty::rag::embed {

// EmbedConfig携带着PLAINTEXT api_key
// ——步行者绝不能看到的第二个秘密。其他所有内容都是逐字可见
// 的（表格行渲染），所以这个列表是：走过的每个字段，键作为长度。
inline auto visual_parts(const EmbedConfig& c) {
    return std::make_tuple(c.backend, visual::ref(c.model),
                           visual::ref(c.host), c.port, c.tls,
                           visual::ref(c.path),
                           visual::ref(c.model_path),
                           visual::ref(c.tokenizer_path),
                           c.api_key.size(),          // LENGTH，从来没有 字节
                           visual::ref(c.dim));
}
static_assert(visual::parts_cover_all<EmbedConfig>);

} // namespace agentty::rag::embed

// ── 域名: MIFd-model 目录 + 专门小组 -adjacent 快照 ───────
namespace agentty {

// Id<Tag>具有构造函数（不是聚合）；一名可见成员。
template <class Tag>
inline auto visual_parts(const Id<Tag>& id) {
    return std::make_tuple(visual::ref(id.value));
}

// ProviderCatalog:派生 缓存 (search_keys、
// row_keys、display_labels、reason_flags —从
// `models` 重建并且仅当它改变时)是豁免的：每个 帧 行走 450 个小写干草堆
// 买不到 `models` 尚未发出信号的任何东西。 loaded_at_ms 是新鲜度时钟，
// 而不是像素。
inline auto visual_parts(const ProviderCatalog& c) {
    return std::make_tuple(visual::ref(c.provider_id), visual::ref(c.label),
                           c.state, visual::ref(c.models),
                           visual::ref(c.error),
                           visual::ref(c.account_label),
                           visual::exempt,   // loaded_at_ms:TTL时钟
                           visual::exempt,   // search_keys:导出缓存
                           visual::exempt,   // row_keys:导出缓存
                           visual::exempt,   // display_labels:导出缓存
                           visual::exempt,   // reason_flags:导出缓存
                           visual::exempt);  // reason_epoch：缓存版本印记
}
static_assert(visual::parts_cover_all<ProviderCatalog>);

} // namespace agentty

namespace agentty::mcp {

// ServerState:一切渲染(设置列表投影整
// 行——连接状态、错误、起源徽章、工具子树)。
// 如果每个成员都是公共+聚合的，则Auto-decomposition将起作用；
// 拼写出来是因为 Origin 位于字符串旁边，
// 并且列表兼作可见性记录。
inline auto visual_parts(const ServerState& s) {
    return std::make_tuple(visual::ref(s.name), visual::ref(s.command),
                           visual::ref(s.url), s.connected, s.disabled,
                           visual::ref(s.error), s.origin,
                           visual::ref(s.config_dir), s.untrusted,
                           s.passthrough,
                           visual::ref(s.tools));
}
static_assert(visual::parts_cover_all<ServerState>);

} // namespace agentty::mcp

// ── maya 将面板键入 ───────────────────────────────
namespace maya {

// ScrollState:只有OFFSETS是型号-visible(
// 状态更新函数-driven.y决定正文-scroll视图窗口在哪一行)。
// 其他一切都是 渲染 管道 - 写回最大值和 Paint-bar
// 纠正每个 帧 的 RENDERER 填充（散列它们会将最后 帧 的
// 油漆读入此 帧 的门：反馈循环，而不是状态）、步长、拖动簿记、
// paint-generation 计数器。定义agentty
// -side：maya没有理由知道我们的帧门存在。
//
// NON-AGGREGATE(取消注册析构函数)，因此brace
// -probing无法计算成员并且证明无法对照它们检查该列表。这正是为什么下面的
// opt-in 是显式的：arity-0 类型失败 CLOSED，所以这个列表是
// REVIEWED CLAIM 而不是静默传递。如果 ScrollState 增长了
// 另一个 状态更新函数-driven 字段，请将其添加到此处 - 没有其他任何
// 信息可以告诉您。
inline auto visual_parts(const ScrollState& s) {
    return std::make_tuple(s.x, s.y);
}

} // namespace maya

namespace agentty::visual {
// 所审查的权利要求位于拥有该概念的命名空间中。
template <> inline constexpr bool trusted_parts<maya::ScrollState> = true;
} // namespace agentty::visual

namespace maya {
static_assert(agentty::visual::parts_cover_all<ScrollState>);

} // namespace maya

namespace agentty::form {

// 表单：每个成员都是可见的。
inline auto visual_parts(const Form& f) {
    return std::make_tuple(visual::ref(f.title), visual::ref(f.subtitle),
                           visual::ref(f.error),
                           visual::ref(f.fields), f.cursor,
                           visual::ref(f.focus), f.dirty,
                           visual::ref(f.note), f.note_replaces_grammar,
                           f.edit_dirty);
}
static_assert(visual::parts_cover_all<Form>);

} // namespace agentty::form

// ── 专题小组位置替代:基地+成员 明确名单 ────────────
namespace agentty::ui::panel {

// 父快照是在其子打开时呈现的NOT——行走它也
// 将使堆叠面板中的每个击键重新-walk其整个祖先。
// 豁免，以此行为理由。
inline auto visual_parts(const WithFrom&) {
    return std::make_tuple(visual::exempt);
}
static_assert(visual::parts_cover_all<WithFrom>);

inline auto visual_parts(const Models& p) {
    return std::make_tuple(visual::ref(static_cast<const pick::OpenAt&>(p)),
                           visual::ref(static_cast<const WithFrom&>(p)),
                           visual::ref(p.assign_slot));
}
static_assert(visual::parts_cover_all<Models>);

inline auto visual_parts(const Providers& p) {
    return std::make_tuple(visual::ref(static_cast<const pick::OpenAt&>(p)),
                           visual::ref(static_cast<const WithFrom&>(p)),
                           p.show_all);
}
static_assert(visual::parts_cover_all<Providers>);

inline auto visual_parts(const ThreadList& p) {
    return std::make_tuple(visual::ref(static_cast<const pick::OpenAt&>(p)),
                           visual::ref(static_cast<const WithFrom&>(p)),
                           visual::ref(p.renaming),
                           visual::ref(p.rename_input),
                           p.rename_cursor,
                           p.all_workspaces,
                           p.only_foreign,
                           visual::ref(p.peek_id),
                           p.peek_turn);
}
static_assert(visual::parts_cover_all<ThreadList>);

inline auto visual_parts(const SmartMode& p) {
    return std::make_tuple(visual::ref(static_cast<const WithFrom&>(p)),
                           visual::ref(p.form), p.advanced);
}
static_assert(visual::parts_cover_all<SmartMode>);

inline auto visual_parts(const WebSearch& p) {
    return std::make_tuple(visual::ref(static_cast<const WithFrom&>(p)),
                           visual::ref(p.form), visual::ref(p.scroll),
                           visual::exempt,   // key_origins:投影到表格行中
                           visual::exempt,   // keys_read:   same
                           visual::exempt,   // keys_gen：记账，从未绘制
                           visual::exempt);  // rows_stale：记账，从未绘制
}
static_assert(visual::parts_cover_all<WebSearch>);

inline auto visual_parts(const PasteSettings& p) {
    return std::make_tuple(visual::ref(static_cast<const WithFrom&>(p)),
                           visual::ref(p.form), visual::ref(p.scroll));
}
static_assert(visual::parts_cover_all<PasteSettings>);

inline auto visual_parts(const PluginEdit& p) {
    return std::make_tuple(visual::ref(static_cast<const WithFrom&>(p)),
                           visual::ref(p.form), visual::ref(p.server),
                           p.project, visual::ref(p.built_kind));
}
static_assert(visual::parts_cover_all<PluginEdit>);

// 两个部分，因为槽具有两个面：父快照和面板。 面板 自己的字段
// 被下面它自己的 visual_parts 覆盖，
// 这是 `restore` 被豁免的地方 - 它是 Esc 撤消存储，
// 从未绘制，因此对其进行散列会唤醒 帧 以获得不可见的东西。
inline auto visual_parts(const Appearance& p) {
    return std::make_tuple(visual::ref(static_cast<const WithFrom&>(p)),
                           visual::ref(p.pane));
}
static_assert(visual::parts_cover_all<Appearance>);

inline auto visual_parts(const AppearancePane& p) {
    return std::make_tuple(visual::ref(p.form), p.picking,
                           visual::ref(p.picker));
}
static_assert(visual::parts_cover_all<AppearancePane>);

// 与外观相同的两个方面：母体快照和面板。
inline auto visual_parts(const Sandbox& p) {
    return std::make_tuple(visual::ref(static_cast<const WithFrom&>(p)),
                           visual::ref(p.pane));
}
static_assert(visual::parts_cover_all<Sandbox>);

// 列表编辑器槽：父快照和面板，相同的两个方面。
inline auto visual_parts(const SandboxList& p) {
    return std::make_tuple(visual::ref(static_cast<const WithFrom&>(p)),
                           visual::ref(p.pane));
}
static_assert(visual::parts_cover_all<SandboxList>);

// `form`是携带用户可以移动的轴的唯一构件。
// 其余部分是从它派生的或针对会话固定的，因此它们都不会与行
// 发生偏差：
//   每次编辑后从表格重新计算预览（参见状态更新函数)
//   后端/claybin_available主机事实，
//   确定打开saved_pending_restart锁存器；它只会变成 false
//                                ->true，并且它驱动的页脚位于表单自己
//                                的 帧 内
inline auto visual_parts(const SandboxPane& p) {
    // `facts`是NOT豁免的，即加载-bearing。
    // 副标题和重启判决均源自该行（第 17 节、第 19 节），
    // 因此对其进行更改会更改屏幕上的内容 - 免除它将使 面板 保留
    // 渲染 一条 过期 "what is running" 行，这正是 面板
    // 存在的要防止的错误类别。
    return std::make_tuple(visual::ref(p.form), visual::exempt,
                           visual::ref(p.restart_note),
                           // `working` 保存 path/port 列表，
                           // 其中选择行 渲染 作为计数——因此即使
                           // 没有行保存它，它也是可见状态。
                           visual::exempt,
                           visual::exempt,
                           visual::ref(p.facts),
                           visual::exempt, visual::exempt);
}
static_assert(visual::parts_cover_all<SandboxPane>);

// AppearancePane::ThemePicker在类型(panel/appearance.hpp)
// 旁边声明了它自己的visual_parts——它拥有一个FilteredPicker，
// 它有一个user-provided构造函数，因此brace
// -arity探针读取0并且完整性证明需要opt-in存在于它所描述的事物旁边。

inline auto visual_parts(const Palette& p) {
    return std::make_tuple(
        visual::ref(static_cast<const agentty::palette::Open&>(p)),
        visual::ref(static_cast<const WithFrom&>(p)));
}
static_assert(visual::parts_cover_all<Palette>);

inline auto visual_parts(const Mention& p) {
    return std::make_tuple(
        visual::ref(static_cast<const agentty::mention::Open&>(p)),
        visual::ref(static_cast<const WithFrom&>(p)));
}
static_assert(visual::parts_cover_all<Mention>);

inline auto visual_parts(const Symbol& p) {
    return std::make_tuple(
        visual::ref(static_cast<const agentty::symbol::Open&>(p)),
        visual::ref(static_cast<const WithFrom&>(p)));
}
static_assert(visual::parts_cover_all<Symbol>);

inline auto visual_parts(const CodeBlocks& p) {
    return std::make_tuple(
        visual::ref(static_cast<const agentty::code_blocks::Open&>(p)),
        visual::ref(static_cast<const WithFrom&>(p)));
}
static_assert(visual::parts_cover_all<CodeBlocks>);

inline auto visual_parts(const CodeBlockResult& p) {
    return std::make_tuple(
        visual::ref(static_cast<const agentty::code_blocks::Result&>(p)),
        visual::ref(static_cast<const WithFrom&>(p)));
}
static_assert(visual::parts_cover_all<CodeBlockResult>);

inline auto visual_parts(const ToolOutput& p) {
    return std::make_tuple(
        visual::ref(static_cast<const agentty::tool_output::Open&>(p)),
        visual::ref(static_cast<const WithFrom&>(p)));
}
static_assert(visual::parts_cover_all<ToolOutput>);

inline auto visual_parts(const Checkpoints& p) {
    return std::make_tuple(
        visual::ref(static_cast<const agentty::checkpoints::Open&>(p)),
        visual::ref(static_cast<const WithFrom&>(p)));
}
static_assert(visual::parts_cover_all<Checkpoints>);

inline auto visual_parts(const Rag& p) {
    return std::make_tuple(
        visual::ref(static_cast<const agentty::rag_settings::Open&>(p)),
        visual::ref(static_cast<const WithFrom&>(p)));
}
static_assert(visual::parts_cover_all<Rag>);

inline auto visual_parts(const SettingsList& p) {
    return std::make_tuple(
        visual::ref(static_cast<const agentty::settings::ListOpen&>(p)),
        visual::ref(static_cast<const WithFrom&>(p)));
}
static_assert(visual::parts_cover_all<SettingsList>);

inline auto visual_parts(const Fork& p) {
    return std::make_tuple(
        visual::ref(static_cast<const agentty::fork_panel::Open&>(p)),
        visual::ref(static_cast<const WithFrom&>(p)));
}
static_assert(visual::parts_cover_all<Fork>);

inline auto visual_parts(const DiffReview& p) {
    return std::make_tuple(
        visual::ref(static_cast<const pick::OpenAtCell&>(p)),
        visual::ref(static_cast<const WithFrom&>(p)));
}
static_assert(visual::parts_cover_all<DiffReview>);

// 统计查看器的唯一VISUAL输入是选择哪个选项卡。
// 缓存的投影是豁免的——这是 帧 哈希值的有趣情况，
// 因此值得精确说明：
//
// `smart`和`stamp`是转录本的DERIVED 缓存，
// 在渲染期间懒惰地刷新。对它们进行哈希处理在两个方向上都是错误的。
// 正确性错误：散列将在缓存刷新时而不是在显示改变时改变，
// 并且由于刷新发生在DURING 渲染，
// 所以这是追逐其自己的末尾的散列。成本错误：
// 每 帧 走一次整个计数正是 缓存 存在要避免的每 -frame
// 工作。
//
// 没有丢失任何东西，因为缓存是抄本的纯函数，
// 并且抄本已经由对话自身的部分散列。
// 消息着陆通过该路线碰撞 帧 哈希值，
// 面板注意到其标记是 过期，并重新计算 - 因此显示仍然更新，
// 而无需 缓存 作为哈希值的输入。
inline auto visual_parts(const Stats& p) {
    return std::make_tuple(
        visual::ref(static_cast<const agentty::stats_panel::Open&>(p)),
        visual::ref(static_cast<const WithFrom&>(p)));
}
static_assert(visual::parts_cover_all<Stats>);

inline auto visual_parts(const Skills& p) {
    return std::make_tuple(
        visual::ref(static_cast<const agentty::skills_panel::Open&>(p)),
        visual::ref(static_cast<const WithFrom&>(p)));
}
static_assert(visual::parts_cover_all<Skills>);

} // namespace agentty::ui::panel

namespace agentty::tool_output {

// 条目嵌入完整的 ToolUse SNAPSHOT (time_points，
// arg 数据流 — 域管道路径不得分解)。它上面的字符串是渲染的行；
// 快照 的 正文 渲染 也是如此，但一旦捕获（即快照点），
// 除了活动行之外，它就不是可变，其不断增长的输出被镜像到每个工具
// 事件 上的 `output` — 因此 output.size
// () + is_live 覆盖其运动。
inline auto visual_parts(const Entry& e) {
    return std::make_tuple(visual::ref(e.name), visual::ref(e.title),
                           visual::ref(e.detail), visual::ref(e.trailing),
                           e.output.size(), e.failed, e.is_live,
                           visual::exempt);   // 调用:不可变 快照
}
static_assert(visual::parts_cover_all<Entry>);

} // namespace agentty::tool_output

// ── 登录: 其自己的 变体 位于槽外 ───────────────────────
namespace agentty::ui::login {

// API密钥IN FLIGHT是秘密：仅长度+光标，与field::
// Secret相同的规则。 （OAuth 回调 代码是用户刚刚从自己的
// 浏览器复制的一个简短的 -lived 和 -time 令牌
// - 为了统一仍然消化了长度 -only：门需要编辑检测，而不是 字节。）
inline auto visual_parts(const OAuthCode& s) {
    return std::make_tuple(visual::exempt,          // 验证者：未渲染
                           visual::exempt,          // 状态随机数：未渲染
                           visual::ref(s.authorize_url),
                           s.code_input.size(), s.cursor);
}
static_assert(visual::parts_cover_all<OAuthCode>);

inline auto visual_parts(const ApiKeyInput& s) {
    return std::make_tuple(visual::ref(s.origin),
                           visual::ref(s.provider),
                           visual::ref(s.provider_label),
                           s.key_input.size(),      // SECRET:仅长度
                           s.cursor,
                           s.key_required);
}
static_assert(visual::parts_cover_all<ApiKeyInput>);

inline auto visual_parts(const ChatGptWaiting& s) {
    return std::make_tuple(s.attempt_id,
                           s.device_auth,
                           visual::ref(s.authorize_url),
                           visual::ref(s.user_code));
}
static_assert(visual::parts_cover_all<ChatGptWaiting>);

inline auto visual_parts(const DeviceWaiting& s) {
    return std::make_tuple(visual::ref(s.provider),
                           visual::ref(s.provider_label),
                           s.attempt_id,
                           visual::ref(s.authorize_url),
                           visual::ref(s.browser_url),
                           visual::ref(s.user_code));
}
static_assert(visual::parts_cover_all<DeviceWaiting>);

} // namespace agentty::ui::login
