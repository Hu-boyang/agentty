#pragma once
// agentty::app::Program — maya 运行时 绑定。
//
// 转发至per-domain 状态更新函数/查看/订阅。  init
// 函数通过Store 接口边界读取设置+最近的线程。

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <thread>
#include <variant>

#include <maya/core/executor.hpp>
#include <maya/maya.hpp>

#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/app/subscribe.hpp"
#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/mem.hpp"
#include "agentty/util/startup_trace.hpp"
#include "agentty/runtime/app/update/internal.hpp"  // detail::live_tail_reveal_settled
#include "agentty/runtime/view/composer.hpp"        // composer_uses_hardware_caret
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/msg.hpp"
#include "agentty/runtime/panel/visual_parts.hpp"   // 结构散列游走
#include "agentty/runtime/visual.hpp"
#include "agentty/runtime/view/view.hpp"

namespace agentty::app {

namespace pn = agentty::ui::panel;   // 专属覆盖面板插槽的替代方案

[[nodiscard]] std::pair<Model, Cmd> init();

// 记录花费时间长于帧的任何循环步骤（更新臂、查看、订阅）。快速时免费：一个时钟读
// 取并进行比较。以 `ui loop.slow: <what> <ms>` 的
// 形式登陆 agentty.log，因此用户的 "it lags when I open X"
// 成为命名手臂的一行而不是猜测。
void note_slow(const char* what, std::chrono::steady_clock::time_point t0,
               int alt = -1) noexcept;

// 这是域 Msg 变体 的哪个替代（当不是 变体 时为-1），
// 因此 loop.slow 可以说 `ComposerMsg#7` 而不仅仅是家族。
template <class D>
[[nodiscard]] int alt_index(const D& d) noexcept {
    if constexpr (requires { d.index(); }) return static_cast<int>(d.index());
    else return -1;
}

struct AgenttyApp {
    using Model = ::agentty::Model;
    using Msg   = ::agentty::Msg;
    // jaal从程序中读取副作用和来源ROWS (D2):
    // P::Cmd表示该程序可能返回什么，P::Sub它可能监听什么，
    // 并且主机必须服务于每个条目，否则程序不会对其进行编译。
    using Cmd   = ::agentty::Cmd;
    using Sub   = ::agentty::Sub;

    // 时钟update()被传送。声明它会使 jaal 通过步骤时间作为每个更新的
    // 第三个参数（jaal core/program.hpp、
    // "time, as an argument"），因此 状态更新函数 永远不会读取
    // std::chrono 本身，并且记录的运行会重播到同一模型。下面的 接口边界 将其
    // 复制到 Model::now 中，这就是它到达帮助程序的方式。
    using Clock = maya::platform::steady_clock;

    // jaal的init是`Cmd init(Model&)`——它填充模型IN PLACE并且仅
    // 返回第一个Cmd (core/program.hpp，`has_init`)。
    //
    // 这曾经是`static std::pair<Model, Cmd> init()`，
    // 旧的运行时的形状。这不是任何人报告的错误：`has_init` 是
    // `requires` 测试，因此签名 jaal 不能将 调用 简单地读取为
    // 此程序没有 init。
    // 而内核值 -initialises 则改为 Model。 init() 加载
    // 的所有内容（设置、线程 列表、活动提供程序）均已构建，然后掉落在地板上，
    // 表现为 agentty 在重新启动时不记得任何内容，并在每次启动时显示第一个
    // -run 卡。
    //
    // free函数仍然返回一对(init.cpp一起构建Model及其Cmd，
    // 并以这种方式测试调用)，因此这适应。
    static Cmd init(Model& m) {
        auto [built, cmd] = ::agentty::app::init();
        m = std::move(built);
        return std::move(cmd);
    }

    // jaal 调用 P::update(Model&，T)对于当其在Msg
    // 源码树上行走时落在其上的任何东西，因此24个域状态更新函数在这里
    // 是静态成员。每个都转发到其自己的 TU 中的同名免费函数，
    // 这使得 one-leaf 编辑的重建时间约为 1.2 秒。
    //
    // 域被声明为组（runtime/cmd.hpp中的handled_as_group)，
    // 并且这些是声明CLAIMS存在的处理函数——jaal
    // 仅在找到一个组时才尊重一个组，因此如果没有这些，
    // 它会正确地保持下降并改为要求每片叶子。
#define AGENTTY_FWD_UPDATE(DomainMsg)                                    \
    static Cmd update(Model& m, msg::DomainMsg d, Clock::time_point now) { \
        m.now = now;   /* 归并的时间：见Model::now */             \
        const auto t0 = std::chrono::steady_clock::now();                \
        const auto alt = ::agentty::app::alt_index(d);                   \
        auto c = ::agentty::app::update(m, std::move(d));                \
        auto out = ::agentty::app::publish_derived(m, std::move(c));      \
        ::agentty::app::note_slow(#DomainMsg, t0, alt);                  \
        return out;                                                       \
    }

    AGENTTY_FWD_UPDATE(ComposerMsg)
    AGENTTY_FWD_UPDATE(StreamMsg)
    AGENTTY_FWD_UPDATE(ToolMsg)
    AGENTTY_FWD_UPDATE(ToolOutputMsg)
    AGENTTY_FWD_UPDATE(ProvidersMsg)
    AGENTTY_FWD_UPDATE(ModelsMsg)
    AGENTTY_FWD_UPDATE(ThreadListMsg)
    AGENTTY_FWD_UPDATE(PaletteMsg)
    AGENTTY_FWD_UPDATE(MentionMsg)
    AGENTTY_FWD_UPDATE(SymbolMsg)
    AGENTTY_FWD_UPDATE(CodeBlockMsg)
    AGENTTY_FWD_UPDATE(CheckpointMsg)
    AGENTTY_FWD_UPDATE(RagMsg)
    AGENTTY_FWD_UPDATE(StatsMsg)
    AGENTTY_FWD_UPDATE(SettingsListMsg)
    AGENTTY_FWD_UPDATE(ForkMsg)
    AGENTTY_FWD_UPDATE(TodoMsg)
    AGENTTY_FWD_UPDATE(LoginMsg)
    AGENTTY_FWD_UPDATE(DiffReviewMsg)
    AGENTTY_FWD_UPDATE(SmartModeMsg)
    AGENTTY_FWD_UPDATE(WebSearchMsg)
    AGENTTY_FWD_UPDATE(PasteSettingsMsg)
    AGENTTY_FWD_UPDATE(PluginEditMsg)
    AGENTTY_FWD_UPDATE(AppearanceMsg)
    AGENTTY_FWD_UPDATE(SandboxMsg)
    AGENTTY_FWD_UPDATE(MetaMsg)

#undef AGENTTY_FWD_UPDATE

    static maya::Element view(const Model& m) {
        const auto t0 = std::chrono::steady_clock::now();
        auto e = ::agentty::ui::view(m);
        ::agentty::app::note_slow("view", t0);
        if (static bool first = true; first) { first = false; ::agentty::startup::mark("first view built"); }
        return e;
    }

    static auto subscribe(const Model& m) -> Sub {
        const auto t0 = std::chrono::steady_clock::now();
        auto s = ::agentty::app::subscribe(m);
        ::agentty::app::note_slow("subscribe", t0);
        return s;
    }

    // 终端 输入（jaal D43）。 jaal 调用 这些型号与 事件 到达时的
    // 型号相同。 事件 列出了 agentty 所取的内容； jaal 检查每个
    // 都有一个 on()，而 maya 检查 终端 是否可以发送。
    // 否 MouseEvent：agentty 永远不会打开鼠标。
    using Event = std::variant<maya::KeyEvent, maya::PasteEvent,
                               maya::FocusEvent, maya::ResizeEvent>;
    static std::optional<Msg> on(const Model& m, const maya::KeyEvent& k) {
        return ::agentty::app::on_key(m, k);
    }
    static Msg on(const Model& m, const maya::PasteEvent& p) {
        return ::agentty::app::on_paste(m, p);
    }
    static Msg on(const Model&, const maya::FocusEvent& f) {
        return ::agentty::app::on_focus(f);
    }
    static Msg on(const Model&, const maya::ResizeEvent& r) {
        return ::agentty::app::on_resize(r);
    }

    // 可选程序挂钩（jaal core/program.hpp — HasSubsKey)。
    // jaal re-runs subscribe() 仅当该值改变时；
    // 如果没有它，事件订阅 源码树 将在 每个 消息后重建并重新-diffed。
    // 在 app/subscribe.hpp 中，它必须遵守的值和规则与
    // subscribe() 本身一起存在 — 它们只有在一起改变时才保持正确。
    static auto subs_key(const Model& m) noexcept {
        return ::agentty::app::subs_key(m);
    }

    // 可选程序挂钩（参见maya/device.hpp-detail::HasVisualHash)。
    // 运行时 调用就在view()之前；当哈希与之前的
    // 渲染没有变化时，view() + render()被
    // 完全跳过。捕获影响用户可见内容的轴；故意省略像
    // Session::last_tick 这样改变每个
    // Tick 而不改变像素的东西。
    //
    // Time-driven动画(光标闪烁、流式传输、
    // 光标脉冲、加载动画)以粗略间隔存储，
    // 因此每个可见步骤推进散列。存储桶大小是我们纯粹
    // 用于动画的 渲染 频率的上限。
    //
    // ENFORCED CONTRACT——这是NOT一个只能依靠照顾的地方。
    // tests/visual_hash_coverage_test.cpp保存两个声明表：
    // 每个视图-affecting模型轴必须推进该散列，
    // 并且每个非-visual轴（last_tick，令牌计数器）必须NOT。
    // 如果为新字段添加`mix()`，则将匹配行添加到该测试的
    // kVisualAxes；如果添加视图忽略的 临时 状态，
    // 请将其添加到 kInvariantAxes。忘记混合视图
    // -axis是生产中的SILENT死区——测试将其转换为
    // 响亮的CI故障。
    static std::uint64_t visual_hash(const Model& m) {
        std::uint64_t k = 1469598103934665603ULL;
        auto mix = [&](std::uint64_t v) {
            k = (k ^ v) * 1099511628211ULL;
        };
        auto mix_str = [&](std::string_view s) {
            mix(s.size());
            // 不要对长字符串的每个字节进行散列——50个KB
            // 工具输出将主导每个-frame预算。
            // 采样一些稳定的偏移量；与其他地方计算的 size +
            // render_keys 结合起来足以检测变化。
            if (!s.empty()) {
                mix(static_cast<std::uint8_t>(s.front()));
                mix(static_cast<std::uint8_t>(s.back()));
                if (s.size() >= 16)
                    mix(static_cast<std::uint8_t>(s[s.size() / 2]));
            }
        };

        // ── Domain.
        //
        // 冻结的前缀——消息[0..ui.frozen_through)
        // ——是不可变考古层（参见m.ui.frozen)。
        // 它对视觉的贡献被结构对（frozen.size（），
        // frozen_turn）完全捕获，该结构仅在冻结瞬间前进。
        // 冻结范围的 Per-message render_keys
        // 无法更改 - with_live_tool 门
        // 拒绝改变它们 - 因此每个 帧 迭代它们都会烧掉
        // CPU 以获得零信号。仅对实时 末尾 进行哈希处理。
        mix(m.d.current.messages.size());
        mix(static_cast<std::uint64_t>(m.ui.frozen.size()));
        mix(static_cast<std::uint64_t>(m.ui.frozen_turn));
        for (std::size_t i = m.ui.frozen_through;
             i < m.d.current.messages.size(); ++i) {
            mix(m.d.current.messages[i].compute_render_key());
        }
        mix(static_cast<std::uint64_t>(m.d.profile));
        // 推理工作在模型徽章(状态栏)中呈现，
        // 因此层改变(选择面板中的←/→)必须移动散列，
        // 否则门将跳过重绘。也影响了选择面板的推理线。
        mix(static_cast<std::uint64_t>(m.d.effort));
        mix_str(m.d.model_id.value);
        mix(m.d.pending_permission ? 1ULL : 0ULL);

        // ── Appearance.
        //
        // EVERY外观偏好，因为它们中的每一个都改变像
        // 素并且当散列不移动时该门完全跳过view()。
        // 省略它们是选择主题似乎没有任何作用的
        // 原因：状态更新函数更新了模型并保留了选择，
        // 解析器选择了正确的主题——然后帧从未被重建，
        // 因此屏幕保留旧调色板，
        // 直到一些不相关的事件(击键、勾号)碰巧移动了散列。
        //
        // "Changes apply immediately"是该
        // 小组在其自己的页脚中做出的承诺；这是保留它的线。
        //
        // FULLY散列，未采样。 mix_str() 查看长度 +
        // 第一个 + 最后一个 + 中间 字节，这对于 50 KB
        // 工具输出是正确的，但对于方案名称来说是错误的：615 个
        // 内置的 -in 名称中的 76 个与该样本下的另一个名称发生冲突，
        // 并且由于样本键位于 ENDS 上，因此冲突发生在按字母
        // 顺序排列的邻居之间 - 这正是通过浏览器访问的箭头。
        //
        //   酸石灰 -> 冒险黑金属（马
        //   杜克） -> 黑金属（混乱）
        //   Nachtschicht ->
        //   星云漂流玫瑰松黎明 -> 玫瑰松月亮
        //
        // 将箭头指向其中一个，模型发生改变，主题被发布，并且大门仍然
        // 显示"nothing visual moved"——所以没有重绘。
        // 按 Enter（移动 `picking`），它就会出现。
        // 那是"doesn't register until you hit it again"报告。
        //
        // 方案名称为 ~15 字节，并且每个 帧 运行一次，
        // 因此通过采样对 保存 没有任何影响。
        for (unsigned char c : m.d.ui().theme) mix(c);
        mix(m.d.ui().theme.size());
        mix(static_cast<std::uint64_t>(m.d.ui().tier));
        mix(static_cast<std::uint64_t>(m.d.ui().polarity));
        mix(static_cast<std::uint64_t>(m.d.ui().density));
        mix(static_cast<std::uint64_t>(m.d.ui().motion));
        mix(static_cast<std::uint64_t>(m.d.ui().tool_output));
        mix(static_cast<std::uint64_t>(m.d.ui().thinking));
        mix(static_cast<std::uint64_t>(m.d.ui().timestamps));
        mix(static_cast<std::uint64_t>(m.d.ui().prose_width));
        mix(m.d.ui().syntax ? 1ULL : 0ULL);
        mix(m.d.ui().compact_turns ? 1ULL : 0ULL);
        mix(m.d.ui().thread_preview ? 1ULL : 0ULL);

        // ── Session / phase.
        mix(static_cast<std::uint64_t>(m.s.phase.index()));
        mix_str(m.s.status);
        // 将汇总数据流压缩成off-transcript 缓冲区;
        // 活动磁带讲述了其 字节 现场直播 (conversation.cpp
        // → Config::stream)。大小是正确的术语：
        // 每个增量都会增加它，磁带的可见窗口是 字节 的纯函数。
        // 如果没有这个，磁带就会冻结在不相关的 重绘 之间——步行工作关闭的
        // 同一 "changed but not hashed" 类。
        mix(m.s.compaction_buffer.size());
        // 状态到期：桶在 100 毫秒，
        // 因此计量器的滚动计数器不会强制每微秒出现 渲染。
        if (m.s.status_until.time_since_epoch().count() != 0) {
            const auto until_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    m.s.status_until.time_since_epoch()).count();
            mix(static_cast<std::uint64_t>(until_ms / 100));
        }
        // 加载动画 帧在10(其周期长度)处分桶​​。 Turn-level
        // AgentTimeline 缓存 使用相同的存储桶大小，
        // 因此此处的哈希前进对应于新的视觉效果。
        //
        // 条件MUST匹配update/meta.cpp的Tick臂中的前进门：
        // 前进但未散列的帧无形地动画（无重绘)，
        // 并且散列但未前进的渲染刻录为未改变的字形。
        // 这两个案例都是实时的：流式传输 将 和
        // 变成等待提供商 目录 的融合 选择面板。
        if (m.s.active() || m.s.models_loading
            || m.loading_spinner_visible()) {
            mix(static_cast<std::uint64_t>(m.s.spinner.frame_index() % 10));
        }

        // ── Composer / UI.
        // 活动提供商的 model-fetch 错误显示在模型
        // 选择面板、提供商行和帐户列表中。
        mix_str(m.s.models_error);
        mix_str(m.ui.composer.text);
        mix(static_cast<std::uint64_t>(m.ui.composer.cursor));
        // `/`菜单：其选择、Esc及其列出的名称。
        mix(static_cast<std::uint64_t>(m.ui.composer.slash.index));
        mix(m.ui.composer.slash.dismissed ? 1ULL : 0ULL);
        mix(m.ui.library.commands.size());
        mix(m.ui.library.skills.size());
        mix(m.ui.plugins.prompts.size());
        mix(m.ui.composer.attachments.size());
        mix(m.ui.composer.queued.size());
        mix(m.ui.composer.expanded ? 1ULL : 0ULL);

        // ── Modal / picker state.
        //
        // CRITICAL:每个弹出面板的open/closed状态AND其in-modal
        // cursor/query必须馈送散列。
        // 这些 选择面板 是选择 -driven — 按 ↑/↓ 只会
        // 改变 变体 内的索引（或查询字符串）；模型中的任何其他内容
        // 都不会移动。如果该索引未进行散列，则 ModelsMove
        // 会生成门认为视觉上相同的模型，skip_render 会触发，
        // 并且光标不会 重绘，直到某个 OTHER 散列轴（光标
        // -blink 奇偶校验，状态文本）在大约 265 毫秒后发生翻转。
        // 症状："press 4-5 times, registers once."
        //
        // variant::index()捕获Closed-vs-Open
        // （以及登录的sub-states）； OpenAt
        // 索引/调色板查询+索引捕获打开的 选择面板 内的光标移动。
        // 独占覆盖面板槽：其变体索引捕获WHICH 覆盖面板是开放的(
        // None-vs-any和选择面板-vs-picker)；
        // 下面的每个替代块添加 cursor/query 运动 内部 和
        // 打开 覆盖面板。
        mix(static_cast<std::uint64_t>(m.ui.panel.raw().index()));
        visual::mix_any(mix, m.ui.panel.raw());

        // Panel-adjacent状态OUTSIDE是面板视图读取的槽值
        // ——与相同的机器一起行走（它们的类型携带
        // visual_parts，其中成员派生为缓存或时钟）：
        //   ·当模型面板被熔断时，目录+行缓存-ONLY被熔断
        //     打开（它们是其异步数据；目录在关闭后仍保持为温暖的缓存，
        //     并且对于关闭的面板来说，每个空闲滴答走动8×
        //     60个模型纯粹是浪费——测量到整个槽的~5μs与
        //     ~540ns）。
        if (m.ui.panel.is<pn::Models>()) {
            visual::mix_any(mix, m.d.provider_catalogs);
            visual::mix_any(mix, m.d.fused_rows);
        }
        //   ·^J列表及其查看，其读取线程列表和
        //     从插槽外部看缓存。 加载 登陆会更改查看显示的内容，
        //     因此它必须移动哈希值。
        if (m.ui.panel.is<pn::ThreadList>()) {
            // 对列表的每次改变都会增加 threads_rev，所以这是 O(1)，
            // 而不是在 帧 中走约 700 个标题。
            mix(m.d.threads.size());
            mix(m.d.threads_rev);
            mix(m.s.threads_loading ? 1ULL : 0ULL);
            mix(m.ui.peek_cache.size());
            for (const auto& t : m.ui.peek_cache) {
                mix_str(t.id.value);
                mix(t.turns.size());
            }
            mix_str(m.ui.peek_loading);
        }
        //   ·待办事项弹出面板:open/closed AND项目——代理
        //     在 弹出面板 打开时重写项目 mid-stream，
        //     即 hash-invisible（确切的被遗忘的-facet 类，
        //     通过此迁移找到）。
        visual::mix_any(mix, m.ui.todo);
        //   ·插件快照设置列表项目
        mix(m.ui.plugins_loading ? 1ULL : 0ULL);
        visual::mix_any(mix, m.ui.plugins);
        //   ·正文-scroll ScrollStates:浏览他们的零件清单（x/y
        //     可见，渲染水暖豁免）。通往读取每个内容的面板，
        //     如上面的 目录。
        if (m.ui.panel.is<pn::ToolOutput>()) {
            visual::mix_any(mix, m.ui.tool_viewer_scroll);
            mix(m.ui.tool_viewer_tail ? 1ULL : 0ULL);   // 松散Model布尔值
        }
        if (m.ui.panel.is<pn::CodeBlockResult>())
            visual::mix_any(mix, m.ui.code_blocks_scroll);
        // NOTE:统计查看器特意在此列表中设置NOT。它的滚动偏移
        // 量为 ON pn::Stats，因此上面的槽移动已经覆盖了它，
        // 并且 parts_cover_all 证明了该类型的覆盖范围。
        //
        // 它曾经在这里——或者更确切地说，它曾经是这里的
        // MISSING，这与整个块存在要解决的问题相同：
        // 状态视图读取，仅通过某人必须记住写入的线可通过门到达。
        // 上面的两个条目仍然是这个形状。将卷轴移动到
        // 拥有它的面板中是通用的修复方法；
        // 参见 panel/stats.hpp 的参数。

        // 登录：自己的变体槽外；相同的行走，相同的保证（
        // 秘密 缓冲区 摘要长度-only 通过零件清单）。
        visual::mix_any(mix, m.ui.login);

        // Time-driven动画桶。每个桶翻转都会通过哈希
        // 前进强制执行 渲染。存储桶大小是 FLOOR，
        // 我们纯粹为了动画而 re-render 的频率；
        // 实际的wake-up节奏是由小部件帧-request调度程序
        // （maya request_animation_frame
        // （） - 单个动画时钟）驱动的，所以当没有动画时，
        // 这里的紧桶不会花费任何费用。
        //
        // CRITICAL：对于屏幕上实际显示的任何动画，
        // 散列桶必须是 PHASE-LOCKED，
        // 否则两者会相互冲突并跳过 帧 — 症状是 光标 有时会
        // 平滑闪烁，有时会冻结 -until- 按键。
        // 渲染 门（当哈希值不变时为 skip_render）
        // 意味着：如果哈希值没有在确切的 帧 上推进动画 切换，
        // 则小部件的 build() 永远不会运行，
        // 因此它的 request_animation_frame()
        // 永远不会 re-armed，帧 请求永远不会 re-issued，
        // 并且循环将休眠完全空闲的 超时，直到下一次按键。因此，
        // 桶必须在每个可见的动画过渡时步进一次，不能更快，
        // 也不能更慢。
        //
        // 三种制度：
        //   (a)精细动画直播（加载动画/流式传输
        //       光标/欢迎鲍勃/queued-chip脉冲）：以SAME节奏步进，
        //       勾选事件订阅唤醒循环。在 DEC-2026 终端 上，
        //       这是 33 ms (~30 FPS)，synchronized
        //       -output 包装器使每个 帧 原子交换 — 平滑，
        //       无撕裂。在 终端 无需 模式 2026（Apple 终端、
        //       普通 xterm、没有同步直通的 tmux）上，
        //       每个 multi-row 重绘 都会逐渐绘制，因此 帧（加
        //       载动画 / 迷你图 / 状态栏）底部的镀铬在每个 渲染 上明显撕裂。
        //       在那里，Tick 已经下降到 100 ms (
        //       subscribe.cpp)，但是这个桶必须 MATCH：
        //       33 ms 的桶相对于 100 ms 的桶每唤醒前进约 3 倍，
        //       因此，skip-render 门无论如何都会在每个桶上触发
        //       撕裂的 重绘，而 100 ms 油门不会买任何东西。
        //       加锁 到刻度周期的桶意味着每个刻度恰好有一个 渲染 — ~
        //       10 个 FPS 的撕裂 帧 而不是 ~10 个冗余的，并且
        //       phase-locked因此渲染不会加倍。
        //       通过 终端 上的刻度，加载动画 的上限已达到
        //       10 FPS，因此感知的平滑度没有变化；
        //       我们只停止额外的撕裂重绘。同步 终端 时，
        //       周期仍为 33 ms — UX 与之前相同。
        //   (b)空闲，创作者光标闪烁：锁到闪烁HALF-period(
        //       265ms=530ms/2)，因此每个散列步骤恰好是一个
        //       光标切换。这使 帧-request 循环 self
        //       -sustaining （每个 渲染 re
        //       -issues 作曲家的 帧 请求）和闪烁完全规则，
        //       在 ~4 renders/sec.
        //   (c)根本没有任何动画（没有光标-e.g。
        //       弹出面板拥有焦点）：没有时间桶，所以固定的屏幕使
        //       ZERO空闲渲染，直到事件到达。
        // 读取ANIMATION时钟，而不是直接读取steady_clock。
        //
        // 该应用程序中的每次-driven阶段都会读取maya::
        // anim_now_ms()——它被记录为他们可以使用的唯一时钟
        // ——因为它携带测试接口边界:advance_anim_clock_ms
        // ()以有意移动时间，并且FREEZE将其固定，
        // 因此同步渲染序列是模型的纯函数。
        //
        // 此处读取steady_clock绕过了两者。然后下面的存储桶对真实
        // 的墙时间进行采样，因此未更改的模型上的两个 visual_hash
        // () 调用 可能会跨越存储桶边界并且不一致 - 每当调度程序在其两
        // 个 调用 之间放置存储桶边缘时，
        // 断言 "an idle picker does NOT animate" 的 不
        // 变量 测试就会失败。这就是为什么这些案例只在 `ctest -j12` 下消失：
        // 加载拉开了调用之间的差距，它并没有改变逻辑。
        // 与 逐步显示-timing 属于同一类错误，
        // 已在 turn.cpp 中修复；这是 渲染 路径中最后一次直接读取的
        // 时钟。
        const auto now_ms = maya::anim_now_ms();

        // WELCOME屏幕步调ITSELF，因此我们不为其添加时间术语。
        //
        // welcome_screen.hpp 是 RAF-driven：它在 60
        // FPS 处运行其级联，然后在 调用 request_animation_frame_after
        // (110) 处运行，以在 ~9 FPS 处步进鲍勃。 maya 的运行
        // 循环已经保证了那些 帧 渲染 ——它的 RAF 覆盖精确地绕过了这个哈希，
        // 因此小部件永远不会被主机的存储桶覆盖间隙所困。
        //
        // 因此，这里的桶不仅是多余的，而且是有害的：对于一个视觉
        // 效果来说，它是一个 第二 时钟。这用于在 流式传输 刻度（
        // 通过 ssh 80 毫秒）针对小部件的 110 毫秒
        // — 每 30 个请求的 帧 42 个哈希值进行存储，
        // 并通过动画步骤额外的 渲染 登陆部分 -way。
        // 这就是空闲的欢迎屏幕闪烁而不是摆动的原因。
        //
        // maya::animation_pending()报告小
        // 部件是否已经请求下一个帧。当它出现时，循环将以 ITS
        // 节奏安排，并且无论我们返回什么，都会以 渲染 进行 -
        // 所以我们不妨碍。询问 maya 击败了硬-coding
        // 的间隔：小部件可以改变其节奏，并且这保持正确。
        const bool widget_paced = maya::animation_pending();

        const bool fine_anim_live =
            m.s.active()                              // 加载动画 / 流式传输 光标
            || !m.ui.composer.queued.empty();         // 排队-chip脉冲
        // 作曲者光标:问，不回复-derive。
        //
        // 这曾经是`caret_blinking = !m.s.active()`，
        // 带有声称
        // "maya's own gate is simply !active, so match it exactly"的评论。
        // 这是错误的，这是贯穿整个文件的错误的最后一个实例：主机
        // 重述了属于小部件的条件。
        //
        // maya的真实门是`!active && !hardware_caret`
        // ——与HARDWARE 光标一起，终端拥有闪烁，
        // 因此maya不绘制任何内容，不安排任何内容，并且没有可供
        // 哈希跟踪的可见步骤。无论如何，混合 265 毫秒
        // 奇偶校验意味着哈希值在屏幕上翻转了 ~4x/sec，
        // 而屏幕上没有任何移动。每次运行循环都会尽职尽责地重新绘制，
        // 在 光标 下重新绘制作曲家，终端 按照自己的时间表
        // 闪烁——两个时钟敲击，光标 明显闪烁。
        //
        // 作曲家配置已经携带了已解析的答案（这是我们所提供的 maya），
        // 因此请阅读它而不是猜测：仅当 maya 实际上是
        // PAINT 时才闪烁，闪烁的 光标。
        constexpr std::int64_t kBlinkHalfMs = 265;
        const bool caret_blinking =
            !m.s.active() && !ui::composer_uses_hardware_caret(m);
        // 美好的-animation桶期。
        // PHASE-LOCKED通过构造来进行 Tick 订阅：
        // 它是SAME价值streaming_tick_period
        // () 手subscribe.cpp为`Sub::every(period, Tick{})`间隔，
        // 因此每个循环唤醒时哈希值前进一次（33 毫秒同步/100
        // 毫秒非-sync / ≥ 80 ms SSH）。
        // 共享单一功能，消除老手-maintained这里重复的启发式
        // - 它默默地省略了SSH地板并可以击败真正的蜱虫。
        const std::int64_t kFineAnimMs = streaming_tick_period(m.env).count();

        // 流式传输-text 渲染桶。虽然辅助消息处于活动状态 流式传输，
        // 但实时边缘运行 maya 的 reveal_fx（打
        // 乱→解析 + 渐变 + 光标），这是一个 ~60 FPS 动画，
        // 需要在每次 RAF 唤醒（16 毫秒）时出现 渲染。
        // 主机还对-feeds重新整理了每个帧的完整到达源，以便新到达的
        // 字节及时出现。如果我们在滴答周期（非-sync 终端上的100
        // 毫秒）进行存储，渲染门将SKIP，中间的RAF唤醒——
        // view()不会运行，live-edge FX将冻结，
        // 并且一大块新到达的文本将在下一个100毫秒翻转时立即弹出（
        // "stuck then burst"症状）。每唤醒 16 毫秒，
        // 以 RAF 间隔 渲染 步进存储桶。 加载动画-only
        // 外壳（工具运行，无 streaming_text）使 -period
        // 桶保持平静，因此非 -sync 终端 不会在 60 FPS
        // 时撕裂镀铬。
        // 逐步显示-frame需求：询问共享谓词，不重复-derive。
        // 该块用于重述整个条件内联（
        // 活动+末尾角色+streaming_text/pending_stream+推理-channel术语），
        // 并且推理术语是来自该重述漂移的BORN：它是在
        // "Thinking gets stuck"发货后添加到这里的，
        // 而subscribe.cpp的副本仍然缺少它。一种定义（subscribe.hpp
        // 中的 reveal_needs_frames）现在同时适用于两者。
        const bool revealing_text = reveal_needs_frames(m);
        // 逐步显示 渲染 节奏。
        //
        // 在 SYNC-output 终端 (DEC 模式 2026:
        // kitty/ghostty/wezterm/foot/VTE 0.62+/Windows
        // 终端/Konsole 22.04+)上，每个 multi-row 帧 原子交换，因此 16 ms/60
        // FPS 提供平滑的撕裂效果 -free 打字机 - 保留它。
        //
        // 在 NON-SYNC 终端（Apple 终端、普通 xterm、
        // tmux w/o 同步直通，以及 - 报告的重现 - 默认 Termux
        // / Android 终端）上，每个 multi-row 重绘 逐渐绘制，
        // 因此无论速率如何，实时边缘 ALREADY 在每个 渲染 上撕裂：
        // 60 FPS 在那里购买 ZERO 额外的平滑度，
        // 它只会淹没一个缓慢的模拟器，其可以合成大约 6 倍的
        // ANSI diff 字节。在受限设备（缓慢的 ARM + 重量
        // 级 终端 应用程序）上，洪水正是 "streaming gets stuck"
        // 症状 - 显示落后于模型几秒，因为 paint/wire
        // 管道无法排出 帧 的 60 FPS。 Tick 事件订阅
        // 已在这些 终端 上仅 100 毫秒唤醒，并且 RAF
        // 覆盖强制 渲染 在中间 16 毫秒唤醒，没有明显的增益。
        //
        // 因此，将 逐步显示 桶与该绘制周期相匹配：哈希值每个刻度前进
        // 一次：每次唤醒正好一个 渲染，新到达的 字节
        // 仍然显示在下一个刻度上，并且冗余撕裂的 重绘 停止。
        // 逐步显示 SPEED 没有变化（节奏是 bytes/second，
        // 而不是 bytes/frame），因此散文以相同的 -clock
        // 速率填充，只是 帧 较少。共享周期使这个阶段
        // -locked通过构造保持在Tick中——这与kFineAnimMs
        // 所依赖的保证相同。
        //
        // 这是TERMINAL的句点，而不是streaming_tick_period()，
        // 并且通过ssh显示差异。该函数同时回答两个问题：
        // 终端能否原子合成一整帧？
        // （33 与 100 毫秒）和 "is there a slow wire to spare"
        // （远程时 >=80 毫秒下限）。第一个是关于绘画的，属于这里；
        // 第二个是关于 BANDWIDTH 的——举起 逐步显示 桶，
        // 因为它会使打字机变得笨重，其原因与肉眼所见无关。在 100 毫秒，
        // 120 cps 下限，即一次出现 12 个字符，每秒 10
        // 倍：报告的 "slow and bursty" 推理。
        // 旧评论所依赖的 字节-delivery 节奏（
        // Tick 周期本来就是字节交付节奏。
        // 在本地是正确的，而在 ssh 上是错误的，
        // 其中 字节 不断到达，而 链接 的滴答声被阻止。
        //
        // Tick 事件订阅仍然为传输节流；
        // 只有 逐步显示 存储桶是解耦的，因此远程会话
        // 渲染 打字机可以顺利进行，而无需更改其唤醒频率。
        const std::int64_t kRevealBucketMs =
            maya::ansi::env_supports_synchronized_output()
                ? 16    // 同步：60 FPS，原子交换
                : 100;  // non-sync:渐进式油漆，1 渲染/100 ms

        // POST-STREAM SETTLE桶。在 StreamFinished 之后，
        // 阶段进入空闲状态，streaming_text 流入 `text`，因此 两者
        // m.s.active() 和 `revealing_text` 变为 false。
        // finalize_turn 解决每个 末尾 消息 IMMEDIATELY
        // （settle_message_md → finish()，agent_session
        // MessageStop 规则 — NO ~200 ms 滑行）并设置
        // pending_settle_freeze，以便 meta.cpp 的下一个
        // Tick 执行冻结一次 live_tail_reveal_settled()。
        // deferred-freeze Tick - 以及 maya 随后的
        // live-tail→冻结收缩协调 - 在 帧 内推进 仅，实际上是 渲染，
        // 并且运行循环的 visual_hash 门会跳过任何哈希未移动的 帧 (
        // maya/device.hpp)。如果没有快速时间术语，哈希值将落入 光标
        // -blink PARITY 存储桶（一次翻转/265 毫秒）：冻结 Tick 和
        // post-freeze 协调 帧 被关闭，崩溃永远不会完成，并且重复的回合被困在
        // 回滚缓冲区。 pending_settle_freeze 标记了一个
        // Tick，直到冻结触发（finalize_turn 设置它；
        // meta.cpp 的 Tick 清除它），因此在其上键入快速桶以继续滴答
        // 16 毫秒时钟，直到冻结切换落地。
        // 逐步显示-drain时间段必须持续经过BOTH、前-freeze解决
        // (pending_settle_freeze)、和、
        // 后-freeze解决冷却时间(settle_cooldown_ticks)。
        // 冷却时间 帧 是 maya 协调 live-tail→
        // 冻结崩溃的地方（其检测→提交→降级→重绘 收缩链）。
        // request_animation_ frame() 单独
        // CANNOT 驱动它们：运行循环的 visual_hash 门
        // 会跳过任何其散列未移动的 帧 (maya/device.hpp)，
        // 因此如果不推进这里的散列，那些 帧 就会被关闭，并且崩溃永远
        // 不会完成协调 - 在 回滚缓冲区 中留下搁浅的重复回合。
        // 在冷却倒计时期间推进哈希使得每个武装 Tick 实际上是 渲染。
        //
        // THIRD术语-已ALREADY耗尽其传输 字节 (
        // `!detail::live_tail_reveal_settled(m)`)的in
        // -flight 逐步显示。这将关闭 "freeze then everything-at-once"
        // （症状 2）漏洞。当 传输 完成一个文本块时，它的 字节 落在 streaming_text 上，
        // 并在 StreamTextBlockClosed / 工具 round-trip
        // 处提交到 `text`，而小部件的 逐步显示 光标是 仍然 滑向边缘 (
        // is_finalizing / reveal_in_progress)。在该窗口中，
        // 两者 m.s.active() 和 `revealing_text`（
        // 关闭 streaming_text/pending_stream 为非 -empty）
        // 可能为 false，并且 pending_settle_freeze 可能尚未设置。
        // 事件订阅 使 Tick 远离 相同 谓词（subscribe.cpp 中的
        // `!live_tail_reveal_settled`），并且 cached_markdown_for
        // 保持 re-arming RAF — 但如果散列落入 265 ms 光标-blink
        // 奇偶校验桶，则运行循环的 visual_hash 门 SKIPS 其中每一个
        // 武装 帧、view() 永远不会运行，打字机冻结，
        // 并且剩余的 末尾 在下一次 光标 翻转/按键时卡入。
        // 将快速桶锁定在使 帧 源保持武装的精确谓词上，
        // 可以保证每个 帧 实际上是 渲染，因此 逐步显示 会
        // 持续滑到 SSH/任何 链接 的边缘，其中 传输
        // 在 -reveal 中期变得安静。
        // 将快速桶锁定在保持 帧 源武装的精确谓词上保证了每
        // 个 帧 实际上 渲染 — reveal_draining
        // (subscribe.hpp) 是该谓词，
        // 相同 是通过 animation_demand 勾选
        // 事件订阅 武装的，因此两个门不能漂移：事件订阅
        // 保持活动的 帧 源是帧 这个哈希允许通过。
        const bool draining_reveal = reveal_draining(m);

        if (revealing_text || draining_reveal) {
            mix(static_cast<std::uint64_t>(now_ms / kRevealBucketMs));
        } else if (widget_paced) {
            // 小部件拥有下一个帧。无论这个哈希如何，
            // maya 都会在该小部件的时间表上 渲染 它，
            // 因此贡献 NO 时间项 - 这里的第二个时钟是产生节拍的。
        } else if (fine_anim_live) {
            mix(static_cast<std::uint64_t>(now_ms / kFineAnimMs));
        } else if (caret_blinking) {
            // Phase-locked:馈送闪烁PARITY，
            // 而不是时间桶，因此散列在maya用于翻转光标 存储单元
            // 的完全相同的边界上前进。通过构建击败-free。
            mix(static_cast<std::uint64_t>((now_ms / kBlinkHalfMs) & 1));
        }
        // 否则：没有任何动画——根本不贡献时间项。

        return k;
    }

    // 可选程序挂钩（参见maya/device.hpp-detail::HasNeedsWarmup)。
    // 当下一个view()结果包含其存储单元尚未被捕获到maya的组
    // 分缓存中的新鲜再水化的冷冻回滚缓冲区时，返回真。 运行时 在 传输
    // -bound 渲染 之前触发 one-shot off-wire
    // warmup_render()，这会将用户-visible的第一个
    // 帧 从 O(内容) 转换为 O(blit) — 通常需要 50–
    // 660 ms 到工具-heavy 线程 恢复上的 <1 ms。
    //
    // 标志被设置在状态更新函数中(e.g、ThreadLoaded、
    // 处理函数在picker.cpp中)。 Maya 的循环 edge
    // -detects（rising-edge 触发预热，falling-edge
    // 重置闩锁），因此可以安全地在后续 状态更新函数 步骤中保留 标志 设置；
    // 我们只是不会重新-warm，直到它变为假然后再次变为真。
    static bool needs_warmup(const Model& m) {
        return m.ui.needs_warmup_render;
    }

    // 回滚缓冲区重播(maya/render/scrollback_replay.hpp)。
    // 宽度变化会擦除 终端 的 回滚缓冲区； maya 调用
    // 位于 帧 上，并将历史记录打印回来，重新包装在实时画布上方。
    // 使用 live 前缀使用的相同冻结代码构建，因此看起来相同。
    static std::vector<maya::ReplayBlock> scrollback_history(const Model& m) {
        const auto t0 = std::chrono::steady_clock::now();
        auto v = ::agentty::app::detail::build_history_blocks(m);
        ::agentty::app::note_slow("scrollback_history", t0);
        return v;
    }
    // maya优选：最新-first块，因此长线程的
    // 上限重放仅构建其显示的部分。
    static maya::ScrollbackReplay::Chunk scrollback_history_chunk(const Model& m,
                                                                std::size_t cursor) {
        return ::agentty::app::detail::history_chunk(m, cursor);
    }
    // 重播限制：默认无（AGENTTY_REPLAY_ROWS设置一；0=关闭）。
    static std::pair<int, std::size_t> scrollback_limits(const Model& m) {
        return ::agentty::app::detail::scrollback_replay_limits(m);
    }
    // 长线程的重播在工作线程 池上搅动了〜100 MB的划痕。
    // mimalloc 保留每个 线程 的已释放页面，因此在每个
    // 工作线程 以及此处进行收集，或者其中大部分保持映射状态。
    static void scrollback_after() {
        maya::exec::parallel_for(std::max(1u, std::thread::hardware_concurrency()) * 4,
                                 [](std::size_t) { ::agentty::release_to_kernel(); });
        ::agentty::release_to_kernel();
    }
    // 实时前缀位于 AppLayout 的 padding(1) 和
    // Conversation 的 padding(0, 1) 内部：每侧两列。
    // 重播的历史记录具有相同的水平填充，因此其行落在它们所拥有的确切列上。
    static maya::Element scrollback_wrap(maya::Element e) {
        using namespace maya::dsl;
        return (v(std::move(e)) | padding(0, 2)).build();
    }
};

static_assert(maya::Program<AgenttyApp>);

} // namespace agentty::app
