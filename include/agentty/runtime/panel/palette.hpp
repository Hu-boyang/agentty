#pragma once
// 命令调色板 — 枚举、label/description 表和打开的 弹出面板
// 的 UI 状态，保持在单个 头文件 中，因此添加新命令是一个 -file 更改
// （扩展 枚举，向 `kCommands` 追加一行，然后 传输 在 update.cpp
// 的 PaletteSelect 中选择处理函数）。

#include <algorithm>
#include <array>
#include <cctype>
#include <climits>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "agentty/runtime/fuzzy.hpp"

namespace agentty {

enum class Command : std::uint8_t {
    NewThread,
    ReviewChanges,
    AcceptAll,
    RejectAll,
    CycleProfile,
    OpenModels,
    SwapModel,
    OpenProviders,
    OpenThreads,
    OpenPlan,
    RunCodeBlock,
    InspectToolOutputs,
    CompactContext,
    RewindCheckpoint,
    ForkThread,
    OpenGeneralSettings,
    OpenStats,
    OpenSkills,
    OpenLogin,
    UpdateAgentty,
    Quit,
};

// 命令所属的调色板部分。呈现为 color-coded 徽章（
// 光标下的 Hue-stable），因此平面列表读取为分组系列，
// 并折叠到模糊过滤器中，以便 `changes` 呈现整个集群。
// 这里的顺序定义调色板中的部分顺序。
enum class Category : std::uint8_t {
    Thread,     // 新款/前叉/紧凑型/倒带
    Changes,    // 审核/接受/拒绝
    Navigate,   // 线程/计划/工具输出/代码块
    Config,     // 配置文件/模型/提供商/智能/抹布/插件/挂钩/命令/代理
    Account,    // 登录/退出
    General,    // 退出/更新
};

[[nodiscard]] constexpr std::string_view category_label(Category c) noexcept {
    switch (c) {
        case Category::Thread:   return "Thread";
        case Category::Changes:  return "Changes";
        case Category::Navigate: return "Go";
        case Category::Config:   return "Config";
        case Category::Account:  return "Account";
        case Category::General:  return "";
    }
    return "";
}

struct CommandDef {
    Command     id;
    const char* label;
    const char* description;
    const char* shortcut;   // 直接全局按键绑定，或者""，如果调色板-only
    Category    category = Category::General;
    bool        danger   = false;  // 破坏性的：丢弃工作/改变工作树
};

inline constexpr std::array kCommands = std::array{
    // ── 线程 ──────────────────────────────────────────────────────────
    CommandDef{Command::NewThread,     "New thread",         "Start a fresh conversation", "Ctrl+N", Category::Thread},
    CommandDef{Command::ForkThread,    "Fork thread",        "Branch into a fresh thread with near-zero context — the parent transcript stays readable on demand", "", Category::Thread},
    CommandDef{Command::CompactContext,"Compact context",    "Replace history with a structured summary", "", Category::Thread},
    CommandDef{Command::RewindCheckpoint,"Rewind to checkpoint","Restore files + conversation to any earlier turn", "", Category::Thread, /*危险=*/true},
    // ── 变动 ─────────────────────────────────────────────────────────
    CommandDef{Command::ReviewChanges, "Review changes",     "Open the diff review pane", "Ctrl+R", Category::Changes},
    CommandDef{Command::AcceptAll,     "Accept all changes", "Apply every pending hunk", "", Category::Changes},
    CommandDef{Command::RejectAll,     "Reject all changes", "Discard every pending hunk", "", Category::Changes, /*危险=*/true},
    // ── 前进( 导航) ───────────────────────────────────────────────────
    CommandDef{Command::OpenThreads,   "Open threads",       "Browse saved conversations", "Ctrl+J", Category::Navigate},
    CommandDef{Command::OpenPlan,      "Open plan",          "View task progress", "Ctrl+T", Category::Navigate},
    CommandDef{Command::InspectToolOutputs, "Inspect tool outputs", "Read tool outputs — the running tool is the live top row", "Ctrl+O", Category::Navigate},
    CommandDef{Command::RunCodeBlock,  "Run code block",     "Run a fenced block from the last reply", "Ctrl+G", Category::Navigate},
    // ── 配置 ──────────────────────────────────────────────────────────
    CommandDef{Command::CycleProfile,  "Cycle profile",      "Write → Ask → Minimal", "Shift+Tab", Category::Config},
    CommandDef{Command::OpenModels,    "Switch model",       "Switch model across every signed-in provider", "Ctrl+/", Category::Config},
    CommandDef{Command::SwapModel,     "Swap to previous model", "Jump back to the model you used before (cross-provider)", "Ctrl+Tab", Category::Config},
    CommandDef{Command::OpenProviders, "Switch provider",    "Choose the LLM backend (Anthropic, OpenAI, …)", "Ctrl+P", Category::Config},
    CommandDef{Command::OpenStats, "Stats", "Tokens, cache, tools, latency, routing — what this thread actually did", "Ctrl+L", Category::Config},
    CommandDef{Command::OpenSkills, "Skills", "What is installed, what it declared, and what agentty noticed in it", "", Category::Config},
    CommandDef{Command::OpenGeneralSettings, "Settings",     "Profile, Smart Mode, retrieval (RAG), changes strip, Appearance (theme, colors, density, motion), sandbox, web search, MCP servers / plugins, slash commands, subagents, hooks", "", Category::Config},
    // (也没有智能模式行。^S打开它，^S关闭它——
    // 面板本身拥有的和弦——所以调色板行是到达已经击
    // 键一次的东西的第三种方式，并且它是配置：你设置
    // 一次路由策略，你不会在-turn中达到它。
    //
    // (也没有MCP服务器/斜线命令/子代理/挂钩/检索/更改条带条目，
    // 出于相同的原因并遵循相同的规则：每个都是
    // CONFIGURATION，每个已经有一个设置主页，
    // 并且第二个门仅使调色板成为人们学习它的地方。
    // 该调色板用于您在 mid-turn 中达到的动词 — 开关型号、
    // 分叉、紧凑、评论。设置是针对您设置过一次的名词。
    // 设置的描述对它们的单词进行了命名，以便搜索仍然可以找到它们。
    //
    // （没有外观条目。它更深了一个Enter——设置→
    // 外观——因为调色板是当你知道自己想要什么时扫描的列表，
    // 而主题不是你在-turn中达到的东西。
    // 两扇门到一扇面板也使调色板成为人们学习它的地方，
    // 这是错误的地方：设置是它的其他邻居居住的地方。
    //
    // 但是一扇你不能FIND的门不是门。
    // 过滤器跨越标签+描述，因此人们实际输入的单词
    // ——主题、颜色、外观——在上面的“设置”描述中命名。
    // 这就是这里描述的全部工作：成为位于下一层的
    // 行的搜索索引。）
    // ── 账户 ─────────────────────────────────────────────────────────
    // （没有 "Sign out" 行。它注销了恰好是 活动
    // 的提供者，仅在事后祝酒中命名 — 这是一种破坏性操作，
    // 其目标在按下之前无法看到。^P 显示每个提供者及其
    // 身份验证状态，并在两个 -press 确认后注销您
    // 指向的提供者，并深入了解 OAuth 提供者的 per
    // -account 管理。执行其中盲子集的行不是一条捷径，
    // 这是一扇更糟糕的门。）
    CommandDef{Command::OpenLogin,     "Sign in / add account", "Sign in — or add another OAuth / API-key account", "", Category::Account},
    // ── 常规 ─────────────────────────────────────────────────────────
    CommandDef{Command::UpdateAgentty, "Update agentty",     "Download + install the new release (shown when one is available)", "", Category::General},
    CommandDef{Command::Quit,          "Quit",               "Exit agentty", "Ctrl+C", Category::General},
};

// `/name`从作曲家运行相同的动作，或者当没有作曲家时运行""。
// 保留在 kCommands 旁边，因此可以在此处看到重命名的行。
[[nodiscard]] constexpr const char* slash_name_for(Command c) noexcept {
    switch (c) {
        case Command::NewThread:        return "/new";
        case Command::CompactContext:   return "/compact";
        case Command::OpenModels:       return "/model";
        case Command::ForkThread:       return "/fork";
        case Command::RewindCheckpoint: return "/rewind";
        case Command::ReviewChanges:    return "/review";
        case Command::OpenThreads:      return "/threads";
        case Command::OpenGeneralSettings: return "/settings";
        case Command::OpenLogin:        return "/login";
        case Command::Quit:             return "/quit";
        default:                        return "";
    }
}

// 实时上下文决定出现哪些条件-visible行。死行（Accept-all，
// 没有差异，Run-code-block，没有围栏回复）会训练用户不信任调色板，
// 因此我们将其隐藏 -​​ 与 "Update agentty" 已
// 应用的规则相同。默认设置使每一行都可见，因此不关心的调用者（测试，
// palette_index_of）的行为与以前一样。
struct PaletteContext {
    bool update_available   = true;
    bool has_pending_changes = true;   // 盖茨审查/接受-all/拒绝-all
    bool has_code_block     = true;    // 门运行代码块

    // 下面的 记忆缓存 在此关键，因此平等是类型契约的一部分，
    // 而不是 缓存 re-derives 逐个字段的东西（
    // 并且在添加第四个门的那天出错）。
    [[nodiscard]] bool operator==(const PaletteContext&) const noexcept = default;
};

// 真实当且仅当在给定实时上下文的情况下`cmd`应该是VISIBLE。
// 将其保留在一个谓词中（而不是分散的 `if`）意味着视图和 分派器 在可见性上
// 免费达成一致，因为两者都经过 filtered_commands()。
[[nodiscard]] inline bool command_visible(const CommandDef& cmd,
                                          const PaletteContext& ctx) noexcept {
    switch (cmd.id) {
        case Command::UpdateAgentty: return ctx.update_available;
        // "Review changes" 是 ENTRY POINT — 保持它始终可发现，
        // 以便您可以找到它（当队列为空时，它会提示 "no pending changes to review"）。
        // 只有 BULK 操作被门控：accepting/rejecting
        // "all" 是真正的 no-op，没有 待处理，而死的破坏性
        // 行会侵蚀信任。
        case Command::AcceptAll:
        case Command::RejectAll:     return ctx.has_pending_changes;
        case Command::RunCodeBlock:  return ctx.has_code_block;
        default:                     return true;
    }
}

// ── 模糊匹配器 ────────────────────────────────────────────────────
// 命令 LABELS 使用共享子序列匹配器（fuzzy.hpp)进行评分，
// 因此调色板按照其他 选择面板 的方式进行排名："re"→审查/拒绝/倒回（标签前缀），
// 而不是 "New th-re-ad"（埋藏在描述中的 "re"）。
// Description/shortcut/category 的点击率会被保留，
// 但排名低于每个标签的点击率。

// 对字段（描述/快捷方式/类别）的Case-insensitive子字符串测试
// ——仅用于KEEP标签不匹配的行，排名低于每个标签匹配。
// Discovery-by-intent（"api" → 切换
// 提供商），不让描述噪音超过真正的标签热门。
[[nodiscard]] inline bool field_contains(const char* field, std::string_view needle) {
    if (!field) return false;
    std::string hay;
    for (const char* p = field; *p; ++p) hay.push_back(
        static_cast<char>(std::tolower((unsigned char)*p)));
    return hay.find(needle) != std::string::npos;
}

// 带有标签-highlight位置的得分、排名比赛结果。
struct CommandMatch {
    const CommandDef* cmd;
    int               score;
    std::vector<int>  positions;   // 要突出显示的标签偏移量（如果没有则为空）
};

// THE匹配器。返回排名为 best-first 的可见命令，每个命令都有其
// label-highlight 偏移量。视图的单一事实来源 + 分派器。
//
// MEMOISED，并通过引用返回——参见下面的match_commands()，
// 了解为什么这个是未缓存的核心，而调用者使用的是缓存的包装器。
[[nodiscard]] inline std::vector<CommandMatch>
match_commands_uncached(std::string_view query, PaletteContext ctx) {
    std::string needle;
    needle.reserve(query.size());
    for (char c : query) needle.push_back(
        static_cast<char>(std::tolower((unsigned char)c)));

    std::vector<CommandMatch> out;
    out.reserve(kCommands.size());
    for (const auto& cmd : kCommands) {
        if (!command_visible(cmd, ctx)) continue;
        if (needle.empty()) { out.push_back({&cmd, 0, {}}); continue; }

        fuzzy::Match lm = fuzzy::score(cmd.label, needle);
        if (lm.matched()) {
            out.push_back({&cmd, lm.score + 1000, std::move(lm.positions)});
        } else if (field_contains(cmd.description, needle)
                || field_contains(cmd.shortcut, needle)
                || (!category_label(cmd.category).empty()
                    && field_contains(std::string{category_label(cmd.category)}.c_str(), needle))) {
            // Description/shortcut/category命中：保留，
            // 但排名低于每个标签匹配（无+1000)并且按目录位置（下面的索引）。
            out.push_back({&cmd, 0, {}});
        }
    }
    if (!needle.empty())
        std::stable_sort(out.begin(), out.end(),
            [](const CommandMatch& a, const CommandMatch& b) {
                return a.score > b.score;   // 稳定的 目录 订单保持平局
            });
    return out;
}

// 缓存的前门。每个来电者都会经历这个过程。
//
// 调色板是从未获得过滤器记忆缓存的选择面板调色板。
// 它的两个兄弟姐妹（`@`和`#`）每手-rolled一个；
// 调色板被留下re-running，每次击键整个模糊扫描
// TWICE——一次在状态更新函数中夹紧光标，
// 一次在视图中绘制——加上第三次在Enter上解决选择。
// 兄弟姐妹已经解决了同样的错误，只是没有第三次复制，这是存在于 调
// 用 站点而不是类型中的模式的标准命运。
//
// 关键是(查询，ctx)，因为可见性是真实输入：当
// 差异到达或检测到更新时，命令可以出现或消失，
// 而查询不变。单独键入查询将固定 过期 行集 — 确切的
// 这类缺陷已由其他缓存通过把完整输入纳入键值来避免。
//
// one-entry 缓存在这里是精确的，而不是近似的：在帧内，
// 每个呼叫者询问相同的问题，并且在帧内，
// 查询每次击键最多改变一次。
[[nodiscard]] inline const std::vector<CommandMatch>&
match_commands(std::string_view query, PaletteContext ctx) {
    static std::string               cached_query;
    static PaletteContext            cached_ctx{};
    static bool                      cached_valid = false;
    static std::vector<CommandMatch> cached;

    if (cached_valid && cached_query == query && cached_ctx == ctx)
        return cached;

    cached       = match_commands_uncached(query, ctx);
    cached_query = std::string{query};
    cached_ctx   = ctx;
    cached_valid = true;
    return cached;
}

// 指针-only查看match_commands，排名。为 分派器 + 现有 调用
// sites/tests 保留，仅需要 "which command is at row N"。
[[nodiscard]] inline std::vector<const CommandDef*>
filtered_commands(std::string_view query, PaletteContext ctx) {
    const auto& matches = match_commands(query, ctx);
    std::vector<const CommandDef*> out;
    out.reserve(matches.size());
    for (const auto& m : matches) out.push_back(m.cmd);
    return out;
}

// Back-compat重载：原始(查询，update_available)形状。
// 保持现有 调用 站点 + 测试正常运行；新的 调用 站点通过 PaletteContext。
[[nodiscard]] inline std::vector<const CommandDef*>
filtered_commands(std::string_view query, bool update_available = true) {
    return filtered_commands(query, PaletteContext{.update_available = update_available});
}

// Sum-type状态，与`runtime/panel/common.hpp`
// 中的其他选择面板、变体形状相同。查询 缓冲区 + 在 Open
// 替代方案中选择的索引 live 仅 — 当调色板关闭时
// 它们不能存在（过去是一个 bool + 两个字段，
// 其中 bool 按惯例门控其有效性；现在类型系统强制执行）。
namespace palette {
struct Closed {};
struct Open {
    std::string query;
    int         index = 0;
};
} // namespace palette

using PaletteState = std::variant<palette::Closed, palette::Open>;

// `ctx`的调色板列表AS RENDERED中`cmd`的索引。
// 用于重新-open，调色板集中在用户最后选择的行e.g上。
// Esc-ing之后又出了一个设置选择面板。
//
// 上下文很重要：行是 GATED (Review/Accept-all
// 需要 待处理 更改，Run-code-block 需要隔离回复，
// Update 需要可用更新)，因此呈现的列表比 kCommands 短，
// 并且隐藏行移动后的每个索引。恢复针对未过滤表计算的游标会落在邻居上
// - 其读作 "Esc moved my selection"，
// 与返回您离开的位置相反。
//
// 如果不可见则返回0（防御性的；用户刚刚使用的行通过
// 构造可见）。
[[nodiscard]] inline int palette_index_of(Command cmd,
                                          PaletteContext ctx = {}) noexcept {
    int i = 0;
    for (const auto& c : kCommands) {
        if (!command_visible(c, ctx)) continue;
        if (c.id == cmd) return i;
        ++i;
    }
    return 0;
}

[[nodiscard]] inline bool is_open(const PaletteState& s) noexcept {
    return std::holds_alternative<palette::Open>(s);
}
[[nodiscard]] inline       palette::Open* opened(PaletteState& s)       noexcept { return std::get_if<palette::Open>(&s); }
[[nodiscard]] inline const palette::Open* opened(const PaletteState& s) noexcept { return std::get_if<palette::Open>(&s); }

} // namespace agentty
