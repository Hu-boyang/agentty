#pragma once
// agentty::config — 每个 agentty 读取配置或写入 字节 的位置，
// 声明一次。
//
// 模型：两个根，各一个锚。
//
//     ~/.agentty遵循HUMAN $AGENTTY_HOME <project>/.
//     agentty遵循CODE $AGENTTY_PROJECT_DIR
//
// 其他所有东西都是其中一个下面的叶子，因此锚移动它。
//
// 下面的每个常数是THE对其特征的定义。该功能读取它并
// `agentty config`打印它，因此打印的答案不会偏离真实的答案。
// config_inventory_test如果任何其他文件声明了
// 自己的布局或规范，则构建失败。

#include "agentty/dirs/dirs.hpp"
#include "agentty/scope/scope.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace agentty::config {

// ── 读取:每个功能的外观和位置 ─────────────────────────
//
// Layout::dialects是每个-feature的选择：kPortable，
// 其中相同的字节在另一个工具中有效，kNativeOnly（默认）对于我们拥有的格式。

// mcp.json不是.agents/.claude约定，模式定义是我们的约定。
inline constexpr scope::Layout kMcpLayout{
    .leaf = "mcp.json", .explicit_env = "AGENTTY_MCP_CONFIG"};

// 具有 frontmatter 的 Markdown — 在其他工具中有效，
// 因此阅读它们的工具是免费的兼容性。
inline constexpr scope::Layout kSkillsLayout{
    .leaf = "skills", .dialects = scope::kPortable};
inline constexpr scope::Layout kAgentsLayout{
    .leaf = "agents", .dialects = scope::kPortable};
inline constexpr scope::Layout kCommandsLayout{
    .leaf = "commands", .dialects = scope::kPortable};

inline constexpr scope::Layout kMemoryLayout{.leaf = "memory.jsonl"};

// 对该内容进行操作是否会产生一个过程？只有 MCP 可以，
// 这就是克隆 仓库 的配置 待处理 的原因，直到有人担保
// 其确切的 字节。函数而不是布局字段：作用域拥有读取代数，
// 并且不得了解调用者对 字节 执行的操作。
[[nodiscard]] constexpr bool executable(std::string_view name) noexcept {
    return name == "mcp";
}

// ── Write : 地点 字节 土地,以及多久的 ────────────────────────────
//
// 每个目录都有自己的变量AGENTTY_<NAME>_DIR。存储仅由环境决定。
// docs/website/storage.md 列出了它们。

inline constexpr dirs::Spec kThreadsSpec{
    .root = dirs::Root::User, .leaf = "threads",
    .env = "AGENTTY_THREADS_DIR", .owner_only = true,
    .retention = "kept until deleted; AGENTTY_THREADS_KEEP_DAYS expires old ones"};
inline constexpr dirs::Spec kCacheSpec{
    .root = dirs::Root::User, .leaf = "cache", .env = "AGENTTY_CACHE_DIR",
    .retention = "refetchable, safe to delete"};
inline constexpr dirs::Spec kLogsSpec{
    .root = dirs::Root::User, .leaf = "logs", .env = "AGENTTY_LOGS_DIR",
    .retention = "agentty.log rotates at 32 MB, one old copy kept"};
inline constexpr dirs::Spec kCredentialsSpec{
    .root = dirs::Root::User, .leaf = "credentials",
    .env = "AGENTTY_CREDENTIALS_DIR", .owner_only = true,
    .retention = "kept until you sign out"};
inline constexpr dirs::Spec kStateSpec{
    .root = dirs::Root::User, .leaf = "state", .env = "AGENTTY_STATE_DIR",
    .retention = "approval hashes, kept until revoked"};

// 推导：一扫即可回收它。 keep_last=1 会
// 保留先前嵌入器的索引，因此切换后端不会强制完全重建。
inline constexpr dirs::Spec kRagSpec{
    .root = dirs::Root::Project, .leaf = "cache", .env = "AGENTTY_RAG_DIR",
    .life = {.rebuildable = true, .keep_last = 1,
             .min_age = std::chrono::seconds{3600}}};

// 由于使用而积累，没有任何东西可以再生它，因此从未被清扫过。
// 与索引分开的叶子，因为保留不同。
inline constexpr dirs::Spec kFeedbackSpec{
    .root = dirs::Root::Project, .leaf = "state",
    .env = "AGENTTY_PROJECT_STATE_DIR",
    .life = {}, .retention = "search learning tallies, delete to forget"};

// 两个根都在 dirs::root_for 中解决了它们的锚点，
// 因此没有规范声明一个——这使得新类别无法将自己宣传为锚点。
[[nodiscard]] constexpr bool moves_whole_root(const dirs::Spec&) noexcept {
    return false;
}
[[nodiscard]] constexpr std::string_view anchor_env(dirs::Root r) noexcept {
    return r == dirs::Root::User ? "AGENTTY_HOME" : "AGENTTY_PROJECT_DIR";
}

// ── 核准用品 ──────────────────────────────────────────────────────
//
// 内容散列人类所担保的。在用户状态目录中，因此克隆的
// 仓库 永远无法证明自己。
inline constexpr std::string_view kSkillsApprovals = "skills_approved.json";
inline constexpr std::string_view kHooksApprovals  = "hooks_approved.json";
inline constexpr std::string_view kMcpApprovals    = "mcp_approvals.json";

// ── 其中一个问题,两个都一半 ─────────────────────────────────────────────
//
// 指针，而不是值：副本将是第二定义。
struct Entry {
    std::string_view     name;    // 您输入的内容：`agentty config mcp`
    std::string_view     what;    // 一行，用于表格
    const scope::Layout* read  = nullptr;   // null ⇒ 不读取为配置
    const dirs::Spec*    write = nullptr;   // null ⇒ 不写任何内容
};

[[nodiscard]] std::span<const Entry> inventory() noexcept;
[[nodiscard]] const Entry* find(std::string_view name) noexcept;

// ── 决心 ────────────────────────────────────────────────────
//
// 由相同功能折叠而成的解析器归并-scope::plan用于梯形图，
// dirs::resolve_dry用于写入路径。这里没有任何内容 re-derives
// 位置。

struct ReadRow {
    scope::Locus   locus{};
    scope::Dialect dialect{};
    std::string    path;
    bool           exists = false;
    // 仅针对当前文件的可执行关注点进行设置。
    // 由相同的 trust_of 计算生成门 调用。
    std::string    trust;
};

// 该特征不读取方言。派生为与布局自己的 `dialects`
// 的设置差异，因此它不能与 IS 读取的内容
// 不一致。
struct SkippedDialect {
    scope::Dialect dialect{};
    std::string    dir;          // ".claude"，".agents"
};

// 在本机上找到的另一个工具的配置，故意不读取：
// 这些在文件名和嵌套方面有所不同，因此它们不是方言，
// 并且读取我们不拥有的可执行配置将让其他人的
// 编辑改变我们产生的内容。用采用它的命令打印。
struct ForeignSource {
    std::string tool;       // "junie"，"claude"，……
    std::string path;
    std::string adopt_with;
};

struct WriteRow {
    std::string      path;
    dirs::Origin     origin = dirs::Origin::Default;
    std::string_view env;        // 该规范自身的变量，如果有的话
    std::string_view anchor;     // 移动其整个根的变量
    bool             env_moves_root = false;
    bool             rebuildable = false;
    unsigned         keep_last = 0;
    std::string_view retention;  // Spec::retention，用于打印输出
    std::string      error;      // 当规范无法解析时为non-empty

    // 测量。仅针对此规范拥有的叶子进行计数，因此共享目录的两
    // 个规范不会使总数增加一倍。
    std::uintmax_t   bytes = 0;
    bool             measured = false;   // 当目录不存在时为假
    bool             shared_dir = false; // 按兄弟规范计数
    bool             partial = false;    // 其中一些是无法读取的
};

struct Report {
    std::string_view            name;
    std::string_view            what;
    std::vector<ReadRow>        reads;
    std::vector<SkippedDialect> skipped;
    std::vector<ForeignSource>  foreign;
    std::string_view            explicit_env;
    std::optional<WriteRow>     write;
};

[[nodiscard]] Report describe(const Entry&);

//   agentty config 每关注一行，
//   每 agentty config mcp 一个梯子，包括 不 读取
//   agentty config env 的型号
int cmd_config(std::span<const std::string> argv);

}  // namespace agentty::config
