#pragma once
// agentty::Model 汇集整个应用的状态。
//
// 此头文件引入各领域类型，并补充不属于单一领域的
// 界面子状态，包括输入框、选择面板、命令面板和弹出面板。
// 状态更新与视图代码直接包含对应领域头文件，
// 只有运行时整合层需要完整的组合模型。

#include <optional>
#include <limits>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <maya/core/scroll_state.hpp>
#include <maya/element/element.hpp>
#include <maya/render/scrollback_ledger.hpp>
#include <maya/render/scrollback_replay.hpp>

#include "agentty/domain/catalog.hpp"
#include "agentty/domain/smart_mode.hpp"
#include "agentty/provider/selection.hpp"   // Domain::selection 选择状态。
#include "agentty/domain/conversation.hpp"
#include "agentty/foreign/scan.hpp"         // Domain::foreign 外部会话状态。
#include "agentty/diff/diff.hpp"
#include "agentty/domain/id.hpp"
#include "agentty/domain/profile.hpp"
#include "agentty/domain/ui_prefs.hpp"
#include "agentty/domain/ui_theme.hpp"   // ui_prefs::Detected 检测到的界面偏好。
#include "agentty/runtime/settings_registry.hpp"   // registry::EnvSnapshot 环境快照。
#include "agentty/domain/session.hpp"
#include "agentty/domain/todo.hpp"
#include "agentty/runtime/panel/slot.hpp"   // ui::panel::State 独占面板槽位。
#include "agentty/mcp/plugin_model.hpp"    // mcp::PluginModel 由 Model 拥有。
#include "agentty/runtime/composer_attachment.hpp"
#include "agentty/runtime/login.hpp"
#include "agentty/runtime/panel/common.hpp"
#include "agentty/runtime/panel/thread_peek.hpp"   // PeekTurn 保存 ^J 的预览缓存。
#include "agentty/runtime/view/cache.hpp"
#include "agentty/store/store.hpp"          // store::Settings 为持久化记录。
#include "agentty/domain/auth_view.hpp"     // auth::AuthView 认证视图。
#include "agentty/runtime/library.hpp"      // Library 描述磁盘上已安装的内容。
#include "agentty/rag/embed_backend.hpp"    // Env::embed_defaults 嵌入模型默认配置。
#include "agentty/io/clipboard.hpp"         // ClipboardWalk 剪贴板遍历状态。

namespace agentty {

// ============================================================================
// 界面子状态各自负责一项功能，并在拥有它们的 Model 附近声明。
// ============================================================================

struct ComposerState {
    /// 一条已排队、尚未发送的消息，与实时输入框
    /// 一样保存 `text` 和 `attachments`。
    /// Agent 忙碌时排队的粘贴内容或
    /// @file 标签，在取回和重发时仍保留为标签。
    /// 字段名与 ComposerState 一致，
    /// 使排队和取回成为结构化交换，
    /// 避免把附件提前压平成内联文本。
    struct QueuedMessage {
        std::string             text;
        std::vector<Attachment> attachments;
    };

    std::string text;
    int  cursor   = 0;
    bool expanded = false;
    std::vector<QueuedMessage> queued;
    /// 长文本粘贴和 @file 选择保存为独立附件正文。
    /// 输入框文本为每个附件保存一个占位符（\x01ATT:N
    /// \x01），使光标计算和换行继续使用普通字符串逻辑。
    /// 提交时 attachment::expand
    /// 将各占位符展开成附件正文，让模型收到完整内容。
    std::vector<Attachment> attachments;

    /// 撤销与重做。每次修改前的 Snapshot
    /// 保存完整输入框内容，包括文本、光标和附件。
    /// 最多保留 64 项，超出后按先进先出移除旧快照。
    /// 新编辑会清空重做栈，符合从历史中间分叉后
    /// 丢弃原有后续记录的编辑器语义。
    struct Snapshot {
        std::string             text;
        int                     cursor = 0;
        std::vector<Attachment> attachments;
    };
    std::vector<Snapshot> undo_stack;
    std::vector<Snapshot> redo_stack;
    /// 连续的普通字符输入合并为一个撤销单元时为 true，
    /// 详见 push_undo。粘贴、删除、
    /// 光标移动、插入标签、提交以及撤销或重做等非普通
    /// 输入操作将其设为 false，使下一段输入创建新快照。
    /// 这样 Ctrl+Z 按输入片段撤销。
    bool undo_coalescing = false;

    /// 输入框当前显示的内容恰好
    /// 属于以下三种状态之一，
    /// 使用变体类型表达互斥关系。
    ///
    ///   Live 是用户正在编辑的草稿，也是通常的状态。
    ///   History{i} 表示用 ↑ / ↓ 浏览历史用户消息，
    ///               i 索引按时间倒序排列的用户消息。
    ///   QueuePeek{i} 表示原地编辑第 i 个排队条目，使用 Alt+↑ / ↓ 切换。
    ///
    /// 过去这里使用 int history_idx = -1 和 int
    /// queue_peek_idx = -1，并用注释描述两者必须互斥。
    /// 用户只能浏览历史消息或
    /// 编辑排队消息。
    /// 两个独立整数仍允许同时
    /// 非负的非法状态，
    /// 各调用点还必须记住 -1 表示未选中。
    /// 变体类型将互斥关系写入类型，
    /// 使非法组合无法构造。
    ///
    /// 首次按 ↑ 或 Alt+↑ 时，将实时文本和附件
    /// 快照保存到 draft_save，使往返浏览不会破坏草稿。
    /// 输入字符、Backspace、Paste、
    /// Kill 或插入标签等修改操作会返回 Live 并
    /// 丢弃快照，此时取回的内容成为正在编辑的新草稿。
    struct Live {};
    struct History   { int index = 0; };   // 指向历史用户消息。
    struct QueuePeek { int index = 0; };   // 指向 `queued` 中的条目。
    using Browsing = std::variant<Live, History, QueuePeek>;
    Browsing                   browsing{Live{}};

    std::optional<std::string> draft_save;
    /// 与 draft_save 同时
    /// 保存的草稿附件数组。
    /// Alt+↑ 浏览队列，
    /// 再用 Alt+↓ 越过末尾返回时，
    /// 文本和标签一起恢复。原草稿无附件或
    /// 当前快照用于历史浏览时为空，
    /// 因为历史消息的附件已经在提交时展开。
    /// 与 draft_save 一起清除。
    std::vector<Attachment>    draft_save_attachments;

    /// `/` 菜单是否显示，每帧根据文本和光标由 slash::menu_query 推导。
    /// 这里只保存选中位置和 Esc 关闭状态。
    /// dismissed 保存按 Esc 时所在的标记文本，
    /// 该标记不变时菜单保持隐藏。
    struct SlashMenu {
        int                        index = 0;
        std::optional<std::string> dismissed;
        std::string                query;   // 记录 index 所对应的查询。
    };
    SlashMenu slash;
    /// 正在通过 cmd::render_slash_prompt 渲染 MCP
    /// `/prompt`，期间再次按 Enter 不会重复启动。
    bool slash_pending = false;

    /// ── 循环模式（^B）。 ───────────────────────────────────────────
    /// 启用后，每当 Agent 完成一轮，
    /// 就自动重发启用时的消息，直到用户再次
    /// 按 ^B 或 Esc 关闭。
    /// 适合持续重构、重复运行失败测试或轮询直到
    /// 任务收敛，无需反复输入或按
    /// Enter。
    ///
    /// loop_text /
    /// loop_attachments
    /// 保存启用时的消息快照，因此循环运行
    /// 期间用户仍可编辑后续消息。
    /// 重复发送的内容始终是启用循环时的内容。
    ///
    /// loop_iterations 统计自动发送的完成次数，
    /// 第一次手动发送记为 0，用于在界面显示 ⟳ ×N。
    /// 显示进度可以帮助用户区分持续运行和程序卡住。
    ///
    /// LOOP 在成功后重复执行，并在失败时自适应退避。
    /// 正常结束一轮后立即重发，
    /// 因为这一轮本身已经消耗了实际时间。
    /// 失败时循环继续，
    /// 但通过 loop_wait_until_ms
    /// 等待一段时间。立即重试 429 会
    /// 加重限流，甚至导致封禁。
    ///
    /// 优先采用提供方返回的延迟信号。
    /// 存在 Retry-After 响应头时严格
    /// 遵循服务器指定的重试时间。否则根据错误类别
    /// 和 loop_failures 逐步增加等待时间，
    /// 下次成功时清零。短暂故障只等待数秒，
    /// 持续故障则退避到数分钟，避免忙循环。
    ///
    /// 只有 Cancelled 会立即停止循环，Esc 明确表示停止。
    bool                    loop_armed = false;
    std::string             loop_text;
    std::vector<Attachment> loop_attachments;
    int                     loop_iterations = 0;
    /// 连续失败次数，用于计算退避时间，
    /// 任何一次成功都会清零。
    int                     loop_failures = 0;
    /// 允许再次发送的最早实际时间，单位毫秒。0 表示现在就绪。
    std::int64_t            loop_wait_until_ms = 0;

    [[nodiscard]] bool looping() const noexcept {
        return loop_armed && !loop_text.empty();
    }

    /// 停止循环并清除待重复的消息，由取消操作和 ^B
    /// 开关调用，可安全重复执行。
    void disarm_loop() noexcept {
        loop_armed = false;
        loop_text.clear();
        loop_attachments.clear();
        loop_iterations = 0;
        loop_failures = 0;
        loop_wait_until_ms = 0;
    }

    /// 轮次正常结束后清除连续失败计数，
    /// 使下一次短暂故障从最短退避时间开始。
    void loop_note_success() noexcept {
        loop_failures = 0;
        loop_wait_until_ms = 0;
    }

    /// 循环现在是否可以发送。等待退避期间返回 false。
    [[nodiscard]] bool loop_ready(std::int64_t now_ms) const noexcept {
        return looping() && now_ms >= loop_wait_until_ms;
    }

    /// 供界面倒计时使用的剩余秒数，就绪时为 0。
    [[nodiscard]] int loop_wait_secs(std::int64_t now_ms) const noexcept {
        if (loop_wait_until_ms <= now_ms) return 0;
        return static_cast<int>((loop_wait_until_ms - now_ms + 999) / 1000);
    }

    /// 记录一次失败，并设置下次重试的等待时间。
    ///
    /// 提供方传入 retry_after
    /// 时优先采用它指定的重试时间。
    /// 否则按类别增加退避，
    /// 限流或认证错误的起始等待时间比瞬时故障更长。
    /// 设置上限后，持续故障最终稳定为慢速轮询，
    /// 等待时间不会无限增长。
    void loop_backoff(int error_class_rank, int retry_after_secs,
                      std::int64_t now_ms) noexcept {
        ++loop_failures;
        int secs;
        if (retry_after_secs > 0) {
            secs = retry_after_secs;
        } else {
            // 错误等级为 0 时表示瞬时或其他错误，为 1 时表示需要更慢重试的限流或认证错误。
            const int base = error_class_rank >= 1 ? 30 : 5;
            const int cap  = error_class_rank >= 1 ? 600 : 120;
            // 每次连续失败后加倍，分别为 5、10、20、40… 秒和 30、60、120… 秒。
            secs = base;
            for (int i = 1; i < loop_failures && secs < cap; ++i) secs *= 2;
            if (secs > cap) secs = cap;
        }
        loop_wait_until_ms = now_ms + static_cast<std::int64_t>(secs) * 1000;
    }

    // ── 历史与队列浏览查询。 ─────────────────────────────────────────
    // 这些辅助读取函数只在对应变体处于活动
    // 状态时返回索引。调用方无需自行拆解变体，
    // 也不能把当前模式与条目索引分开读取，
    // 从而避免 -1 哨兵值造成的状态分离。
    [[nodiscard]] bool is_live() const noexcept {
        return std::holds_alternative<Live>(browsing);
    }
    [[nodiscard]] std::optional<int> history_index() const noexcept {
        if (auto* h = std::get_if<History>(&browsing)) return h->index;
        return std::nullopt;
    }
    [[nodiscard]] std::optional<int> queue_peek_index() const noexcept {
        if (auto* q = std::get_if<QueuePeek>(&browsing)) return q->index;
        return std::nullopt;
    }

    /// 用户最后一次操作输入框的实际时间，
    /// 使用 maya 动画时钟的毫秒值。
    /// 任意按键、编辑或光标移动都会更新。
    /// 15 秒无操作后，绘制的块状光标停止闪烁，
    /// 使输入框变为静态，减少 GPU 终端的空闲合成。
    /// 首次操作前为 0，期间正常闪烁。
    std::int64_t last_edit_ms = 0;
};

// 待办面板独立保存条目列表，
// 打开或关闭状态由 open 中的变体类型表示。
struct TodoState {
    ui::pick::Modal       open;     // 状态为 Closed 或 OpenModal。
    std::vector<TodoItem> items;
};

// ============================================================================
// Model 汇集应用状态，并分为以下三部分。
//   d 为 Domain，保存会话内容等需要持久化或发送给提供方的数据。
//   s 为 Session，保存当前请求的状态机和取消句柄。
//   ui 保存选择面板、弹出面板和视图虚拟化等临时界面状态。
//
// 这种划分使函数的操作范围清晰可见。只修改 m.ui.
// * 的函数不会误改会话，读取 m.d.
// * 的状态更新逻辑也无需传递选择面板的状态。
// ============================================================================

struct Model {
    struct Domain {
        // 完整保存的持久化记录。
        //
        // 过去 init() 将记录逐字段拆到下方成员，
        // 导致修改某项偏好的状态更新函数无法直接生成
        // Settings。它必须通过 Deps 读取旧
        // 记录再修改，update/appearance.cpp
        // 曾因此产生读取后修改再写回的非纯操作。
        //
        // 持有完整记录后，保存可由当前状态直接推导。
        // 修改对应字段，将 persisted
        // 交给保存副作用即可。字段正在逐项迁移，
        // 每次独立提交便于定位回归问题，下方成员是尚未迁移
        // 的部分。
        //
        // provider_keys 不包含在这里。
        // 它们封存在独立密钥存储中，不会写入 settings.json，
        // 由拥有密钥存储的宿主在保存时合并。
        store::Settings     persisted;

        Thread              current;
        // save_current 上次为 current 描述
        // 保存操作时的指纹，用于下次只发送差异。空值表示发送完整会话，
        // 适用于新会话、切换会话或上次保存被拒绝的情况。
        std::vector<std::uint64_t> saved_fps;
        std::vector<Thread> threads;
        // Codex / Claude Code 会话，用作选择器中的分组标题。
        // 任一列表变化时，将其中尚未打开的 foreign_only 条目
        // 合并进 threads。单独保存，避免原生会话重新加载时丢失这些条目。
        foreign::Scan foreign;
        // threads 或其中任一行变化时递增，包括加载、
        // 重命名和删除，让重绘判定无需遍历整个列表。
        std::uint64_t threads_rev = 0;
        Profile             profile = Profile::Write;

        // agentty 的外观配置，实际存储于 persisted.ui；
        // 这里沿用现有 34 处调用使用的名称。它属于 Domain，
        // 因为这是用户选择并持久化的状态，生命周期长于
        // Model::ui 中的临时面板和光标。
        //
        // 刻意使用访问器而非引用成员；
        // 引用会使 Domain 无法复制赋值，而每次状态更新都会
        // 复制或移动 Model。
        [[nodiscard]] ::agentty::ui_prefs::Prefs&       ui()       noexcept { return persisted.ui; }
        [[nodiscard]] const ::agentty::ui_prefs::Prefs& ui() const noexcept { return persisted.ui; }

        std::vector<ModelInfo> available_models;
        ModelId                model_id{std::string{"claude-opus-4-5"}};
        // 当前会话使用的提供方，例如 Anthropic、
        // OpenAI 系列端点或 ACP 代理。这是权威状态。
        //
        // 此前只保存于 provider::select /
        // provider::active() 进程全局对象，
        // 由切换状态更新函数写入。全局副本仍供事件循环之外的流式工作线程、
        // ACP 服务器和 main 读取，但现在由模型派生：
        // 修改该字段的状态更新完成后，分派边界通过 update.cpp 的
        // publish_derived 发布副本。状态更新函数及其调用的
        // Cmd 构造函数直接读取 Model。
        //
        // init() 根据 main() 启动时选择的提供方初始化。
        provider::Selection    selection;

        // 设置记录之外的凭据状态，包括哪些存储持有令牌、
        // 哪些环境变量已设置以及账号注册表。宿主在启动时和每次凭据
        // 副作用后通过 LoadAuthView →
        // AuthViewLoaded 加载，状态更新和视图无需读磁盘。
        auth::AuthView         auth;

        // 跨提供方的统一模型选择器，见 docs/design/unified-model-picker.md。
        // provider_catalogs 是统一选择器打开时延迟构建的跨
        // 提供方合并目录，每个已认证提供方一项。它与 available_models 并列，
        // 后者仍是传输路径和上下文计算使用的当前提供方目录。
        // recent_models 保存有上限的最近使用 (provider,
        // model) 对，最新在前，用于 RECENT 分组和
        // ^Tab 快速切换，并持久化到
        // Settings.recent_models。
        std::vector<ProviderCatalog> provider_catalogs;
        std::vector<ModelRef>        recent_models;
        // 统一选择器行缓存。仅在打开、过滤、
        // 目录加载或收藏等输入变化时由状态更新函数重建，
        // 使视图和光标计算每帧读取现成向量，
        // 无需每次按键重新枚举提供方、
        // 读取 settings.json 并给模型评分。选择器关闭时为空。
        std::vector<FusedRow>        fused_rows;
        // 未认证提供方的登录入口在统一选择器打开时计算一次，
        // 避免每次按键重建行时读取磁盘认证状态。
        // 它与 provider_catalogs 一起构成选择器的数据源。
        std::vector<SigninOffer>     fused_offers;
        // 推理力度等级，可在模型选择器中通过 ←/→ 即时切换。
        // None 表示默认不启用思考；其他等级让 Claude 提供
        // 方发送自适应思考和 output_config.effort。
        // 持久化到 Settings，构建请求时再根据具体模型决定是否启用。
        Effort                 effort = Effort::None;

        // Smart Mode：按角色路由执行，见 docs/design/smart-mode.md。
        // enabled 默认关闭，需要用户主动开启。
        // 关闭时不改变行为，内部辅助调用仍默认使用最便宜模型。
        // 配置持久化到 Settings。
        smart::RoleConfig      smart;

        std::vector<FileChange>          pending_changes;
        // 审查面板关闭时，X/^X 拒绝全部的两次按键保护状态。
        // 第一次按键进入待确认并显示提示，第二次执行，
        // 其他任意差异审查消息都会解除确认。面板内使用面板
        // 状态 confirm_reject_all。
        bool                             reject_all_armed = false;
        // 编辑后是否显示持续可见的“N changes”改动审查条。
        // 启动时从 Settings 加载，可通过命令面板即时切换。默认关闭；
        // 改动仍加入 pending_changes，可通过 Ctrl
        // +R 打开面板，只是不持续显示横幅。
        bool                             show_changes_strip = false;
        // 是否在会话中显示模型的推理或思考内容，并向 Anthropic 请求
        // 可见思考，使用 interleaved-thinking beta。
        // 启动时从 Settings 加载，可通过 Ctrl+K →
        // Reasoning 切换，默认关闭，适用于各提供方。
        bool                             show_reasoning = false;
        std::optional<PendingPermission> pending_permission;
        // 连续发生的 shell 绕行次数，由 analyze_detour().substitutable() 判断。
        // 用于 update/tool.cpp 的偏离提醒，其他任何调用都会重置。
        int                              shell_detour_streak = 0;

        // 会话级“始终允许”授权，以工具名为键，例如 shell、
        // write。由 PermissionApproveAlways 设置，
        // kick_pending_tools 在显示提示前查询。
        // 切换权限配置时清空，使收紧配置后重新要求确认。
        // 启动时从持久化的 always_allow_tools 初始化。
        std::set<std::string>            session_grants;

        // 每项授权对应的工具定义指纹，以工具名为键。
        //
        // 仅按名称授权会使权限自动适用于以后解析到
        // 同名的任何实现，而 MCP 工具名称由服务器控制。
        // 因此授权携带被批准定义的指纹，
        // 调用也按该哈希分派；若工具已被重新定义，
        // execute_approved 会拒绝执行。
        //
        // 缺失或为 0 表示尚未校验。旧版本保存的
        // 授权没有哈希，直接拒绝会使升级后所有旧
        // 授权失效。
        std::map<std::string, std::uint64_t> grant_def_hash;

        // 是否仍有任一提供方的模型目录正在获取？
        //
        // 这是判断统一模型选择器列表是否仍在填充的唯一谓词。
        // 它不同于 Session::models_loading，
        // 后者只跟踪切换提供方或启动时的当前
        // 提供方目录请求。统一选择器会向所有
        // 已认证提供方并发取目录，并在这里分别跟踪。
        // 此前混用判定，导致加载动画只在切换
        // 提供方时转动，真正打开选择器时却冻结。
        [[nodiscard]] bool any_catalog_loading() const noexcept {
            for (const auto& c : provider_catalogs)
                if (c.state == ProviderCatalog::State::Loading) return true;
            return false;
        }
    };

    struct UI {
        ComposerState       composer;
        // ── 独占面板槽位。 ─────────────────────────────
        // 所有模态覆盖界面，包括选择器、命令面板、
        // 只读视图和差异审查，都放在同一个变体中。
        // 打开新界面即赋值，从结构上关闭旧界面，
        // 不可能同时打开两个，替代了旧 16 个字段
        // 所需的约 17 处互相关闭逻辑。
        // 各分支及刻意独立的登录、权限、待办界面见
        // overlay_state.hpp；
        // panel::top() 合并四类状态并决定优先级。
        ui::panel::State  panel;
        // 转义序列剪贴板查询的记录：发送查询时递增 seq，
        // 成功粘贴时令 done = seq。
        // ClipboardQueryTimeout{seq}
        // 只有在序号仍匹配且 done 尚未追上时才诊断，
        // 确保回复取消超时，快速重试也不会触发旧提示。
        std::uint64_t       clipboard_query_seq  = 0;
        std::uint64_t       clipboard_query_done = 0;
        // 查询发出时采样 maya::clipboard_rx_bytes()。
        // 超时路径通过比较该值，区分终端完全未
        // 答复和大型图像回复仍在传输。
        std::uint64_t       clipboard_rx_mark    = 0;
        // 查询发出时采样 maya::clipboard_read_refused()。
        // 计数变化表示终端返回 EPERM/ENOSYS，
        // 而非保持沉默；两种故障需要不同修复方式，
        // 用户无法仅靠表面症状区分。
        std::uint64_t       clipboard_refused_mark = 0;
        // 当前终端查询是否由图像粘贴意图触发，即无法本地读
        // 取剪贴板时按 Ctrl+V / Alt+V，
        // 而非普通文本粘贴。随 seq 递增一起设置。
        //
        // 原因是 SSH+tmux 场景会同时请求
        // OSC 5522 和 OSC 52：前者
        // 只被 kitty 支持且携带图像字节，
        // 后者广泛支持但只返回文本。非 kitty
        // 终端会忽略前者并回复后者，使想粘贴截图的用户只
        // 得到文本；回复又取消了无响应超时。若不保存该标志，
        // 就无法解释这个隐蔽失败。
        bool                clipboard_wanted_image = false;
        // wanted_image 为真但收到文本时，
        // ComposerPaste 暂存该文本，用于处理 tmux
        // 中 OSC 52 文本回复早于 OSC 5522 图像回复的竞态。
        // 超时路径将其作为回退，避免明明已有文本却报告无响应。
        std::string         clipboard_text_fallback;
        // 最近一次查询回复的大小与哈希，避免 tmux 下
        // OSC 5522 和 OSC 52 对同一查询重复
        // 回复时粘贴两次。
        std::size_t         clipboard_last_reply_size = 0;
        std::size_t         clipboard_last_reply_hash = 0;
        // 剪贴板回复的 ESC 字节若途中被剥离，
        // 可能作为普通文本到达；这里收集完整内容，防止将其直接粘贴为正文。
        // 见 ComposerPaste 分支。
        std::string         clipboard_leaked_reply;
        // 等待终端回复的 Ctrl+V 读取状态，
        // 保留 richpaste 的读取路径遍历进度。
        // 终端沉默时可继续尝试下一条路径，收到任意回复后清空。
        ClipboardWalk       clipboard_walk;
        // 终端查询何时被视为无响应。超时分支在此
        // 之前持续检查，因为拒绝只通过计数器体现。
        std::chrono::steady_clock::time_point clipboard_query_deadline{};
        // 每当将图像编码文本转换为粘贴标签，或标签继续增长时递增。
        // ArmoredPasteStall 仅在其后再无数据到达时
        // 触发。
        std::uint64_t       armored_paste_gen = 0;
        // Smart Mode 的槽位分配状态曾以 slot、
        // advanced、from 三个字段保存在这里，因为跳转会销毁原面板。
        // 现在状态随分配模式的 Models 面板保存于
        // pn::Models::assign_slot，
        // from 快照中保留完整 SmartMode 面板；
        // 放弃选择器即结构性放弃模式，ascend() 恢复面板，无需另存。

        // 通过 ←/→ 改变但尚未落盘的推理力度。每次按键保存会
        // 在界面线程同步执行加载、fsync 和 rename，
        // 因此 CycleEffort 仅设置该标志，
        // 由 CloseModels/Select 统一写入一次。
        bool                effort_dirty = false;
        // 最近一次已提示“不在提供方列表中”的模型标识，
        // 避免每次刷新目录都重复弹出提示。
        std::string         unlisted_model_noted;
        // 轮次运行中选择的新模型，待轮次结束后应用，
        // publish_derived 每次状态更新都会检查。
        // 这样续接与重试仍使用轮次开始时的模型和提供方。
        // 新选择覆盖旧选择，重新选择正在使用的模型则清空。
        std::optional<ModelRef> pending_switch;
        // Plugins/MCP 连接快照由 Model 拥有，
        // 渲染时不读取全局连接池。view 和
        // visual_hash 必须是 Model 的纯函数；
        // 位于模型之外的界面状态无法触发重绘，
        // 也无法由状态更新循环驱动，这曾造成连接中卡住和
        // 面板延迟。连接本体仍由 mcp::ConnectionPool
        // 管理套接字和子进程，但面向界面的值快照
        // 通过 PluginsUpdated 消息镜像到此，
        // 采用与 ThreadsLoaded 相同
        // 的 Cmd→Msg 流程。视图只渲染此快照，
        // 不调用 plugin_model()。
        mcp::PluginModel    plugins;
        bool                plugins_loading = false;  // 连接或重新加载 Cmd 正在执行。
        // 工具查看器实时行（第 0 行）的跟随尾部开关。
        // 为真时正文自动滚动到最新输出，类似 tail -f；
        // 向上滚动会关闭，按 End 或回到底部
        // 重新开启。仅查看实时条目时使用。
        bool                tool_viewer_tail = true;
        TodoState           todo;
        ui::login::State    login;            // Closed | Picking | OAuthCode | OAuthExchanging | ApiKeyInput | Failed
        // 终端窗口焦点状态（?1004）。初始为 true，
        // 因为启动 agentty 的终端拥有焦点。它控制硬件光标：
        // 失去焦点时停放并隐藏，避免非活动面板中出现闪烁光标。
        bool                terminal_focused = true;
        // 宿主最近一次通过 TerminalResized 报告的终端尺寸。
        // 首次报告前使用 80x40，与旧版直接查询 tty 时
        // 针对管道或分离 stdout 的回退值一致。
        int                 term_cols = 80;
        int                 term_rows = 40;

        // ── 已固定的回滚前缀，使用 maya ScrollbackLedger。 ───
        //
        // 只追加的账本，保存已经构建完成、
        // 代表会话稳定部分的 Element 块。
        // 视图通过 Config::ledger 向
        // maya 的 Conversation 组件传入借用指针，
        // 再以 ledger_ref 渲染，实现跨帧零复制；
        // maya 每帧绘制时也会把各块真实布局高度写回
        // 账本。
        //
        // 高度记账方向的反转：见证链与裁剪记账。
        // 账本替代了宿主维护的四套并行记录：frozen
        // / frozen_rows / frozen_is_separator，
        // 以及 frozen_row_total、
        // frozen_cols。旧方案根据重建宽度自行
        // 测量行数，与 maya 实际绘制发生偏差，
        // 曾造成手机 SSH 重复内容、调整尺寸后过量
        // 提交和单行估算漂移。现在统一由 maya 测量：
        // 裁剪提交量只能由 ledger.harvest
        // () 根据绘制记录高度生成，
        // 形式为 ScrollbackDebt 令牌。
        // 宿主无法伪造或调整，因而类型结构直接排除了测量漂移。
        //
        // 每次追加一个账本块，粒度如下：
        //   • 每个新发言方轮次前的间隔：空行、分隔线、空行。
        //   • 已稳定消息或消息组构建好的 Turn Element。
        //   • CompactionRecord 边界对应的压缩分隔条。
        //
        // 生产方是 frozen.cpp 中的 freeze_range/freeze_through，
        // 由收尾路径调用。切换会话、NewThread 或 OpenThread
        // 时清空并重置 frozen_through；加载保存的会话后
        // 由 rehydrate_frozen() 重建。
        maya::ScrollbackLedger frozen;

        // m.d.current.messages 的排他上界。
        // 索引小于 frozen_through 的消息都已构建到
        // frozen 中，无需实时渲染。
        // 后缀 [frozen_through .. end) 是每帧
        // 重建的实时尾部，逐 Turn 的 shared_ptr Element
        // 缓存降低了尾部已稳定消息的开销。
        std::size_t         frozen_through = 0;

        // 下一条新冻结的 Assistant 轮次编号。实时尾部使用
        // frozen_turn + assistant_messages_in_tail
        // 生成显示编号。
        int                 frozen_turn = 0;

        // 延迟收尾冻结。流结束后，
        // StreamingMarkdown 的
        // finish() 改变构建结构和前缀代数，
        // 使组件哈希不同于此前的实时帧。
        // 如果同一次 Tick 中立即 freeze_through，
        // 下一次绘制会拿完成后的哈希与旧实时
        // 帧的 prev_cells 比较，
        // 导致组件缓存未命中并从顶部重画整轮。
        // 为此 finalize_turn 只完成
        // 收尾并设置标志，暂不冻结；
        // 先由一次 view() 绘制已完成但仍在实时
        // 尾部的树，更新 prev_cells。
        // 下一次空闲 Tick 再冻结字节和哈希均相同的树，
        // 命中缓存并清除标志。
        bool                pending_settle_freeze = false;

        // 冻结后的稳定窗口。
        // 延迟冻结会在一次状态更新中把
        // 实时尾部并入冻结前缀。
        // 若尾部已超出可视区，
        // maya 需要多帧完成协调：发现
        // 溢出前缀缩短、提交视口外行、
        // 降为 Stale，
        // 再在下一帧轻量重绘。fps=0
        // 时若 pending_settle_freeze
        // 清除后立即停止 Tick，
        // 只会得到一帧，无法走完整个链条，
        // 重复轮次就会留到下一次按键。
        // agent_session 持续 30 fps，
        // MessageStop 后仍继续渲染，
        // 所以不会出现此问题。这里通过冻结后继续保留数
        // 帧 Tick 模拟该行为：冻结时设为
        // kSettleCooldown，每次 Tick 递减，
        // 大于 0 时保持订阅。
        int                 settle_cooldown_ticks = 0;

        // 会话恢复后的首个 Tick 再执行一次
        // trim_frozen_if_oversized。
        // 恢复时的预算基于估算高度，
        // 首次绘制则通过 record_paint
        // 写入各块真实高度。工具密集的会话中，
        // 面板装饰可能使实际总高度达到估算的数倍。
        // 延迟裁剪根据真实绘制数据闭合反馈，沿用收尾冻结的 drop_front
        // + harvest + commit_scrollback 路径。
        // ThreadLoaded 设置此标志，
        // 并通过 settle_cooldown_ticks 启用 Tick；由 Tick 分支消费。
        bool                pending_rehydrate_trim = false;

        // 给 maya 运行循环的一次性预热提示：
        // 下次 view() 包含该会话尚未绘制的大量
        // 冻结历史，应在向终端输出前预热组件缓存。
        // 会话切换后由 rehydrate_frozen
        // () 填充 m.ui.frozen 时设置，
        // 产生预热后模型的状态更新步骤会清除它，
        // 确保每次切换只预热一次。
        //
        // maya 的 run<P> 通过可选 Program 钩子
        // static bool needs_warmup(const Model
        // &) 检测，见 app.hpp 的 detail::HasNeedsWarmup。
        // 为真时，warmup_render 在不输出终端数据的情况下填充
        // 按哈希索引的组件缓存，使可见渲染直接复用单元格；工具密集会话恢复时，
        // 可把 80～660 ms 的冷绘制降低到不足 1 ms。
        bool                needs_warmup_render = false;

        // 后台 git status 刷新是否正在执行？它用于 @
        // 选择器的工作集排序。
        //
        // 该任务的唯一在途标志特意保存在 Model 上。
        // 仅为 false 时，状态更新函数才生成刷新 Cmd，
        // GitSignalsRefreshed 清除它。
        // 这样“同一时刻最多一个任务”能由测试直接构造并断言，
        // 无需依赖藏在工作区层、状态更新函数看不到的静态
        // 原子变量。
        //
        // 此前 @ 处理函数每次按键都会启动无保护的分离线程，
        // 长按 @ 会产生 N 条线程和 2N 个 git
        // 子进程，各自超时 8 秒，争相写入同一张表，
        // 结果取决于任意完成顺序。
        //
        // 不纳入 visual_hash，因为它不会改变任何像素。
        bool                git_refresh_inflight = false;

        // 自定义 --provider 规格及其 -m 记忆值。
        // 主机第一次成功返回模型列表时才固定保存；init 根据 main 的
        // 启动参数设置一次，由 ModelsLoaded 接收。
        std::optional<std::pair<std::string, std::string>> unproven_spec;

        // 磁盘上已安装的内容，包括技能、批准记录、
        // 斜杠命令和钩子文件。启动时及每轮结束后通过
        // LibraryLoaded 副作用加载，因为轮次可能修改这些内容。
        // 状态更新函数和面板读取此快照，不直接读磁盘。
        Library library;

        // ^J 预览已加载的会话缓存，最新在前，最多 kPeekCache 个：
        // 只保存卡片展示的行并按轮次拆分，加载完成时构建一次。
        // 完整会话可能有数十 MB，这里仅需 KB 级空间。
        struct PeekThread {
            ThreadId id;
            std::vector<ui::PeekTurn> turns;
        };
        static constexpr std::size_t kPeekCache = 16;
        std::vector<PeekThread> peek_cache;
        // 正在加载的会话标识，避免长按方向键时为经过的每一
        // 行都排入一次加载。
        std::string peek_loading;

        // 工作区是否为 Git 仓库？启动探测 RepoProbed
        // 返回前为 nullopt。检查点读取此状态，
        // 避免在状态更新线程上直接启动 git。
        std::optional<bool> git_repo;
        // 启动探测 TerminalScrollbackProbed 得到的终端
        // 回滚缓冲区长度，由 Scrollback=Auto 使用。
        struct TerminalScrollback {
            int         rows      = 0;      // 0 表示未知或无限制。
            bool        unlimited = false;
            bool        exact     = false;  // 来自终端实际配置，不使用默认猜测。
            std::string source;
        };
        std::optional<TerminalScrollback> terminal_scrollback;

        // 跨帧组件状态缓存，目前仅用于：
        //   • StreamingMarkdown：为每条 Message 保留组件实例，
        //     使块边界缓存能在实时渲染帧之间
        //     持续存在。
        //   • Agent 时间线面板冻结：所有工具调用进入终态后，
        //     保存一次 AgentTimeline Element 快照，
        //     直到该消息被推入
        //     m.ui.frozen。
        // 不再缓存 Turn 级 Element。稳定轮次作为
        // Element 值保存在 m.ui.frozen，
        // 实时尾部每帧重建，范围受活动轮次限制。
        mutable ui::ViewCache view_cache;

        // 回滚重放缓存按会话和分块起始消息索引保存
        // 已构建历史块，并将块内容和轮次编号纳入键。
        // 再次调整尺寸或切回近期会话时复用 Element，
        // 仅重新布局；消息变化会改变键并触发重建。
        // 最多保留 kHistoryThreads
        // 个会话，淘汰最旧项。
        struct HistoryChunk {
            std::uint64_t                  key = 0;
            std::vector<maya::ReplayBlock> blocks;
            std::size_t                    next = 0;
        };
        struct ThreadHistory {
            ThreadId id;
            std::unordered_map<std::size_t, HistoryChunk> chunks;
        };
        static constexpr std::size_t kHistoryThreads = 1;
        mutable std::vector<ThreadHistory> history;   // 最近使用的在前。
        // 当前会话各消息组的起始索引和轮次编号，
        // 让重放块通过 O(log n) 查找范围，
        // 而无需每次从消息 0 遍历。会话或其长度变化时
        // 重建。
        struct RunIndex {
            ThreadId                 thread;
            std::size_t              msg_count = 0;
            std::vector<std::size_t> starts;
            std::vector<int>         turn_at;
        };
        mutable RunIndex history_runs;
        // 轮次运行期间宽度发生变化。
        // 此前重放已重新折行历史内容，但实时轮次已
        // 滚出屏幕的行仍按旧宽度输出；轮次稳定并冻结后，
        // 再重放一次修正它们。
        bool replay_after_settle = false;

        // ── 模态选择器的滚动状态。 ─────────────────────────────
        //
        // 各模态选择器的滚动状态。maya::Picker
        // 通过 Picker::Config 中的借用
        // 指针读取和修改；存储由宿主拥有，遵守 maya
        // 负责外观与行为、agentty 负责模型状态的
        // 适配规则。
        //
        // view 接收 const Model&，
        // 但 maya 在绘制时会回写 max_y、
        // bar_v_bounds 和 viewport_bounds，
        // 因此需要 mutable。与 view_cache 相同，
        // 这类渲染缓存和边界信息不改变模型可观察行为，符合逻辑常量语义。
        //
        // 默认在打开、关闭、重新打开之间保留。
        // 语义发生变化时，例如过滤查询改变匹配集合，
        // 状态更新函数可以主动将 .y 重置为 0。
        //
        // auto_dispatch = false：
        // 这些选择器由选中行驱动，
        // 光标由 ModelsMove /
        // ThreadListMove /
        // PaletteMove 等状态更新维护，
        // Picker 每次构建时自动滚动以保持选中行可见。
        // 若同时启用默认的 auto_dispatch，
        // ↑/↓/PageUp 还会直接改变
        // ScrollState::handle 的
        // scroll.y，与选择跟随互相抵消，
        // 表现为连按四五次才有反应。滚动位置应由选中索引唯一决定，
        // 不再额外分派原始按键。
        //
        // 注意：maya 的 ScrollState 因修复
        // 释放后使用问题而增加了析构函数，用于从活动状态注册表注销，
        // 所以已不再是聚合类型，不能使用 {.
        // auto_dispatch = false} 指定初始化。
        // routed_scroll() 通过普通构造清除该标志。
        static maya::ScrollState routed_scroll() noexcept {
            maya::ScrollState s;
            s.auto_dispatch = false;
            return s;
        }
        mutable maya::ScrollState fused_picker_scroll     = routed_scroll();
        mutable maya::ScrollState provider_picker_scroll  = routed_scroll();
        mutable maya::ScrollState thread_list_scroll      = routed_scroll();
        mutable maya::ScrollState command_palette_scroll  = routed_scroll();
        mutable maya::ScrollState mention_palette_scroll  = routed_scroll();
        mutable maya::ScrollState symbol_palette_scroll   = routed_scroll();
        mutable maya::ScrollState code_blocks_scroll      = routed_scroll();
        mutable maya::ScrollState checkpoints_scroll      = routed_scroll();
        mutable maya::ScrollState rag_settings_scroll     = routed_scroll();
        // 统计视图的偏移不保存在这里，
        // 而位于 pn::Stats 中。
        // 面板槽位遍历可使重绘判定自动看到它，
        // 也让生命周期与面板绑定。此旧式状态集合只能由
        // program.hpp 手写 mix() 访问，
        // 新面板应优先采用前一种结构。
        mutable maya::ScrollState smart_mode_scroll       = routed_scroll();
        mutable maya::ScrollState plugin_edit_scroll      = routed_scroll();
        mutable maya::ScrollState appearance_scroll       = routed_scroll();
        mutable maya::ScrollState sandbox_scroll          = routed_scroll();
        mutable maya::ScrollState fork_scroll              = routed_scroll();
        mutable maya::ScrollState todo_scroll             = routed_scroll();
        mutable maya::ScrollState tool_viewer_scroll      = routed_scroll();
        mutable maya::ScrollState account_list_scroll     = routed_scroll();
        // Ctrl+K 设置列表，包括 Plugins/Commands/Agents/Hooks。
        // Plugins 可能有 125 个工具、超过 135 行，
        // 因此需要与其他选择器一样支持滚动。
        mutable maya::ScrollState settings_list_scroll    = routed_scroll();
    };

    Domain      d;
    StreamState s;
    UI          ui;

    // 当前正在处理的消息时间，不是时钟。
    //
    // jaal 以参数向 update() 传入本次状态
    // 更新的时间，AgenttyApp 声明 Clock，
    // 见 jaal core/program.hpp。
    // 这个契约保证录制的运行能够重放得到相同模型。
    // 分派边界每次首先把该参数写入此字段，供下方约 300
    // 处辅助调用使用，避免为每个函数增加 now 参数；
    // 因此一次更新中始终使用同一个时间。
    //
    // 规则及其实际约束方式：
    //   * 仅允许分派边界写入，包括 program.hpp 的 AGENTTY_FWD_UPDATE，
    //     以及测试入口 update(Model, Msg, now)。其他位置不得赋值。
    //   * 仅状态更新函数读取。view() 和 subscribe() 看到的 const Model 中，
    //     now 属于上一次更新，已不再代表当前时间；
    //     elm_purity 禁止 update/ 之外读取。
    //   * 状态更新函数也不能自行读取时钟，由静态检查的 clock 类别约束。
    //
    // 使用与 jaal 相同的 steady_clock，避免系统日历时钟回拨。
    std::chrono::steady_clock::time_point now{};

    // 本次状态更新对应的日历时间，仅供必须使用真实日期的场景：
    // 会话 created_at/updated_at 会持久
    // 化并用于排序；OAuth 过期时间需要与提供方时钟比较。
    //
    // 单调时钟与日历无直接关系，所以另设输入而不直接转换 now。
    // 两种时间在同一分派边界按同一步骤推导：内核启动时只读取
    // 一次日历时钟，由 init 设置 Model::wall_epoch
    // / steady_epoch；每次更新把单调
    // now 映射到该时间线上。这样一次更新对应两种时钟
    // 中的同一瞬间，录制重放也得到相同日期。
    // 过去各调用点直接读 system_clock，
    // 会使 updated_at 与同时保存的消息时间不同。
    [[nodiscard]] std::chrono::system_clock::time_point wall_now() const noexcept {
        return wall_at(now);
    }
    // 把任意单调时刻映射到同一日历时间线。视图传入当前帧的
    // maya::anim_now()，无需自行读取系统
    // 时钟即可得到日期。init() 尚未设置纪元时，
    // 回退到真实日历时钟。
    [[nodiscard]] std::chrono::system_clock::time_point wall_at(
            std::chrono::steady_clock::time_point t) const noexcept {
        if (steady_epoch == std::chrono::steady_clock::time_point{})
            return std::chrono::system_clock::now();
        return wall_epoch + std::chrono::duration_cast<
                   std::chrono::system_clock::duration>(t - steady_epoch);
    }
    // wall_now() 使用的两种时钟基准，仅由 init() 设置一次。
    std::chrono::system_clock::time_point wall_epoch{};
    std::chrono::steady_clock::time_point steady_epoch{};

    // 最近一次发布到运行时子代理注册表的 Model 路由视图，
    // 见 tool/subagent.cpp。
    //
    // 子代理循环在工作线程上运行，从注册表而非
    // Model 读取配置，因此注册表必须跟踪四
    // 个模型字段。过去状态更新函数内部通过
    // tools::subagent::set_*() 发布，
    // 四个文件共有 11 处调用；每处修改都得记住同步，
    // 才能保持正确。
    //
    // 现在状态更新函数不自行发布。
    // 分派边界 update.cpp 的
    // publish_subagent_if_changed 每次
    // 更新后派生视图，与此快照比较，仅在变化时返回 cmd::publish_subagent。
    // 同步成为统一流程，避免调用点漏写。
    struct SubagentView {
        std::string             model;
        std::string             provider;
        smart::RoleConfig       smart;
        std::vector<ModelInfo>  candidates;
        [[nodiscard]] bool operator==(const SubagentView&) const = default;
    };
    SubagentView published_subagent;

    // 最近一次发布到 provider::select
    // 进程全局对象的提供方。与 published_subagent 相同，
    // 分派边界比较 Model 中的选择与此快照，
    // 仅变化时返回发布副作用。比较依据保存在 Model 中，
    // 可重放，无需读回全局状态。
    provider::Selection published_selection;

    // 最近一次发布到工具层的 web_search 策略，
    // 规则同 published_selection。初始为 nullopt，
    // 确保首轮状态更新必定发布；此前工具层自行延迟读取 settings.json，
    // 可能与 init() 加载的值不一致。
    std::optional<web_search_cfg::Config> published_web_search;
    // 同样记录 richpaste 使用的粘贴设置，消息为 PublishPasteConfig。
    std::optional<paste_cfg::Config> published_paste;
    // 同样记录 MCP 工具加载设置，消息为 PublishToolLoading（#98）。
    std::optional<std::string> published_tool_loading;

    // 进程启动环境，仅由 init() 读取一次。
    //
    // Elm 状态更新不读取 getenv，
    // 否则行为会依赖 Model 之外的隐藏状态，
    // 使重放和环境变量测试失真。进程运行期间
    // 这些启动事实不会变化，因此捕获后可直接作为
    // 状态更新可见的完整事实。此前 update/
    // 中分散着六处 getenv 和两个函数
    // 局部静态对象，现由下方谓词统一定义。
    struct Env {
        // 远程会话，包括 SSH、mosh、Eternal Terminal 或
        // 强制指定。帧间隔至少 80 ms，因此流式和逐字显示需要降低刷新频率。
        bool remote               = false;
        // 特指普通 SSH，会排除 mosh/ET。输入框的剪贴板路径不同，
        // 因为 OSC 52 通过 SSH PTY 传递。
        bool ssh                  = false;
        // AGENTTY_NO_REVEAL_GLIDE：强制在轮次结束时立即完成显示。
        bool no_reveal_glide      = false;
        // 终端支持同步输出，使用 DECSET 2026。
        bool synchronized_output  = false;
        // AGENTTY_FROZEN_COLLAPSE：为屏幕外正文保留占位，
        // 不重新渲染。
        bool frozen_collapse      = false;
        // AGENTTY_NO_UPDATE_CHECK / AGENTTY_NO_AUTO_UPDATE 配置。
        bool no_update_check      = false;
        bool no_auto_update       = false;
        // 解析后的 AGENTTY_MAX_CONTEXT_TOKENS，0 表示未设置；
        // 它是回退到默认上下文长度前的最后一级覆盖。
        int  max_context_tokens   = 0;
        // AGENTTY_REVEAL / _TYPEWRITER / _DECORATE，未设置表示启用。
        bool reveal               = true;
        bool reveal_typewriter    = true;
        bool reveal_decorate      = true;
        // AGENTTY_PAINTED_CARET：始终自行绘制输入框光标。
        bool painted_caret        = false;
        // 是否运行在协作编辑器宿主中，例如 AGENTTY_HOST=emacs，
        // 或设置 INSIDE_EMACS 并使用 vterm；用于输出文件事件 OSC。
        bool host_integration     = false;
        // 回滚重放限制，见 maya/render/scrollback_replay.hpp。
        // Scrollback 设置决定行数，这里保存相关启动事实。
        // replay_rows_override 来自 AGENTTY_REPLAY_ROWS，
        // -1 为未设置、0 为关闭重放；terminal_history_rows 来自终端声明，
        // 例如 tmux history-limit，0 表示未知。
        int         replay_rows_override = -1;
        int         terminal_history_rows = 0;
        // 启动探测完成前，Scrollback=Auto 仅根据
        // 环境采用终端文档中的默认值；0 表示未知。
        int         auto_scrollback_rows = 0;
        std::size_t replay_max_bytes = std::numeric_limits<std::size_t>::max();
        // 终端通过 TERM、COLORTERM、COLORFGBG、
        // NO_COLOR 报告的颜色等级和背景明暗，用于解析 auto 外观。
        ui_prefs::Detected terminal;
        // 设置项的环境覆盖值，以及 AGENTTY_SMART_MODE 强制值。
        settings::registry::EnvSnapshot settings;
        // 当前程序是否能替换自身？启动时仅检查
        // 一次：解析自身路径，并在其目录创建、
        // 删除文件以探测写权限。此前每次考虑自动
        // 更新时都在状态更新中同步调用，
        // 隐藏于 util/ 辅助函数的文件写入绕过了按
        // 名称检查。运行期间这两项结果不会变化。
        bool        self_update_ok     = false;
        std::string self_update_reason;   // self_update_ok 为 false 时的原因。
        // 由 AGENTTY_EMBED_* / AGENTTY_OLLAMA_HOST
        // 描述的嵌入模型配置，覆盖默认值。RAG 面板以此为初始状态。
        rag::embed::EmbedConfig embed_defaults;
        // agentty 的每用户根目录，默认为 ~/.agentty，也可由 $AGENTTY_HOME 指定；
        // 只解析一次。状态更新函数使用的用户 mcp.json 等路径均从这里派生。
        std::string user_root;
        // 设置 AGENTTY_RAG_PROACTIVE 时，为本次运行强制
        // 开启或关闭主动注入，覆盖保存的 RAG 设置。
        std::optional<bool> rag_proactive_override;

        // 路径显示的相对基准，包括启动工作目录和主目录，
        // 例如 src/x.cpp、~/notes。一次捕获后，
        // 工具卡片各帧都使用相同格式，无需查询操作系统。
        std::string cwd;
        std::string home;

        // 在 message_stop 时把逐字显示平滑推进到末尾，而不是直接贴出剩余文本。
        // 需要同步输出，否则 200 ms 的过渡只有约两帧，容易造成高度漂移。
        // Motion=Off 或 AGENTTY_NO_REVEAL_GLIDE
        // =1 会关闭此行为。
        [[nodiscard]] bool reveal_end_glide() const noexcept {
            return !no_reveal_glide && synchronized_output;
        }
    } env;

    // 当前屏幕上是否有加载动画？
    //
    // 选择器目录加载动画的统一启用条件。它必须属于
    // Model 而不只是 Domain，
    // 因为只有绘制动画的界面可见时，33 ms 的
    // Tick 才有价值。过去只看目录加载状态，
    // 选择器关闭后事件循环仍按帧率唤醒，浪费工作并增加输入延迟：
    // 若空闲循环已经进入定时轮询，下次按键会晚一帧响应。
    // 实测导航时方向键 p90 为 12.7 ms、最大 55 ms，
    // 而 p50 只有 0.1 ms。
    //
    // 各调用点与 Session::active() 配合，三处条件必须一致：
    // subscribe.cpp 决定是否发送 Tick，update/meta.cpp
    // 推进动画，app/program.hpp 决定帧是否改变
    // visual_hash。
    [[nodiscard]] bool loading_spinner_visible() const noexcept {
        return ui.panel.is<ui::panel::Models>()
            && d.any_catalog_loading();
    }
};

} // namespace agentty
