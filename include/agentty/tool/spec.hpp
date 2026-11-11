#pragma once
// agentty::tools::spec-编译-time工具目录。
//
// `src/tool/tools/*.cpp`中的每个工具工厂在运行时处填充
// `ToolDef`。 *每个*工具的形状 - 它的名称、它的功能、
// 它的 流式传输 行为 - 也固定在 编译 时间，并作为 `ToolSpec` 的
// `constexpr std::array` 存在于此。
//
// 其作为单独的编译-time表存在的三个原因：
//
// 1.静态交叉-checks。底部的 `static_assert` 块
//    证明了任何 运行时 测试都无法保证的关于 目录 的属性 —
//    每个会写文件系统的工具都确实声明 WriteFs。
//    只读工具不会意外获得 Exec 能力。
//    bash 工具的名称必须是 bash，不能写成 Bash。
//    这些是在它们起源的构建中捕获的，而不是在运行时捕获的。
//
// 2.目录元数据的单一事实来源。工厂位于
//    `src/tool/tools/*`参考
//    `tools::spec::lookup("bash")`
//    作为其名称/描述/副作用/提前加载-streaming 标志，
//    所以不可能出现拼写错误——只有一个地方可以写入字符串`"bash"`，
//    并且查找要么返回规格，要么工厂失败到编译。
//
// 3. 传输-format生成器(e.g.JSON工具列表发送至Anthropic)
//    可以迭代 `constexpr` 表，而无需支付 运行
//    时 初始化成本。今天，传输 路径仍然使用 运行时
//    `registry()` 向量；规格表让我们可以逐步迁移它。

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string_view>

#include "agentty/tool/effects.hpp"
#include "agentty/domain/id.hpp"

namespace agentty::tools::spec {

enum class Kind : std::uint8_t;  // 转发-declared;在 ToolSpec 之后定义

struct ToolSpec {
    std::string_view name;            // 传输 标识符 — 必须是唯一的
    Kind             kind;            // 关闭-set鉴别器；匹配`name`
    EffectSet        effects;         // 能力集；政策驱动
    bool             eager_input_streaming;   // FGTS 选择-in 标志 (Anthropic)
    // Wall-clock 看门狗截止时间。当此类工具转换为运行状态时，
    // reducer 会调度 `Cmd::after(max_seconds, ToolTimeoutCheck{id})`；
    // 如果计时器触发时处理程序仍在运行，则处理程序强制执行-fails。
    // `0s` 表示 "no overlay timeout"
    // — 用于通过子进程运行程序（bash、
    // 诊断）拥有自己的超时的工具，因此我们不会加倍 -gate。
    //
    // 按工具最长的-but-still-reasonable 运行时进行
    // 挑选：缓慢的NFS读取可能需要20秒；跨 Linux 内核 源码树 的
    // 递归 grep 可能需要一分钟。
    // 看门狗是"the worker thread is wedged"的安全网；
    // 合法的 Slow-but-progressing 工作负载应该能够轻松地适应所选的上限。
    //
    // 带类型的 为 `std::chrono::seconds`，因此调用者不会
    // 在没有显式 `duration_cast` 的情况下意外地将其提供给
    // 毫秒 -expecting 调度程序 — 该单元位于该类型中。
    std::chrono::seconds max_seconds;

    // 在 分派器 边界（DynamicDispatch::execute、
    // tool.hpp)处强制实施的 Per-tool 输出字符预算。
    // 0 表示 "no cap" — 该工具已按设计生成有界输出（
    // todo、git_commit）。其他任何内容都是硬性上限：
    // 超过此长度的文本将根据 `trunc_strategy` 进行截断，
    // 并使用 one-line "[... N chars elided ...]" 标记，
    // 以便模型知道响应已被剪辑并可以请求具体信息。
    // 受 Claude Code 的 per-tool 输出上限
    // 的启发 - agentty 的各个工具已经分块输出（Read
    // 使用 offset/limit，Bash 使用 末尾-only，
    // Grep caps 匹配），但这是涵盖内部限制在病理
    // 输入上失败的工具的安全网（来自 `bash` 的 10
    // MB single-line 日志文件，
    // 读取文件有 2000 行长行，Grep 处于失控模式）。
    // self-limit 的工具上限非常慷慨，因此它几乎不会因
    // 健康的输入而出错。
    //
    // 按字符数而不是标记来选择，因为 agentty 不在本地运行标记生成器。
    // ~4 chars/token 根据经验，30k-char
    // 上限 ≈ 7.5k 代币。
    int max_output_chars;

    // 当输出超过max_output_chars
    // 时使用的策略。每个策略都会保留工具的典型
    // 调用者最关心的输出部分：
    //
    //   Head-保留前N个字符，删除末尾。适合读取/编辑/Write，
    //              其中答案是
    //              按顺序返回所请求的块。
    //   末尾——保留最后N个字符，丢掉头。
    //              适合 Bash/诊断，
    //              其中最新的日志行/错误消息比设置输出更重要。
    //
    //   HeadTail —保留第一个~70%+最后一个~30%，删除中间。
    //              适合Grep/Web/Git，
    //              其中开始（most-relevant匹配，
    //              页面头文件，第一次提交）AND和结束（摘要/
    //              "N more matches"提示，页脚，
    //              最近提交）携带信号。
    enum class TruncStrategy : std::uint8_t { Head, Tail, HeadTail };
    TruncStrategy trunc_strategy;
};

// 完整工具目录，显示顺序与运行时一致。
// 注册表 (`src/tool/registry.cpp:build_registry`)。
// 顺序很重要：模型对较早的条目有回忆偏差，因此 `edit` 在 `write` 之前，
// 以推动 full-file 重写。
//
// 描述文本故意是目录中的NOT——它是传输-only元数据（每个工具的工厂
// 组成其自己的帮助文本，有时平台-conditional，如Windows
// 上的bash）。 Cross-validating 描述什么
// 也买不了； cross-validating 副作用 + 名称很重要。
// Short-hand，因此目录读作`20s`而不是
// `std::chrono::seconds{20}`。作用域为 目录 数组，
// 因此 UDL 不会泄漏到包含该数组的 头文件。
namespace detail {
using sec = std::chrono::seconds;
}

// ── 关闭 -set 用于该数据库中每个工具的身份 目录 ───────────────────
// 带类型的鉴别器使得调用站点停止hand-rolling `name == "bash"`
// 字符串比较。添加一个工具：添加一个 Kind 臂，在 目录 中添加一行，并在 编译
// 时间添加匹配的 `kind` 和 `kind_of("new_tool") == Kind::NewTool`
// — 下面的 `static_assert` 墙证明每个 Kind
// 都有一个 目录 条目，并且每个 目录 条目都有一个 Kind。
//
// 传输身份保留在`name`字符串字段（Anthropic的
// 词汇是传输上的真实来源）；实物是内部封闭设置的reducer/views
// 分派。
enum class Kind : std::uint8_t {
    Read,
    Edit,
    Write,
    Move,
    Remove,
    Bash,
    ProcessStart,
    ProcessPoll,
    ProcessStop,
    Grep,
    Glob,
    ListDir,
    Todo,
    WebFetch,
    WebSearch,
    FindDefinition,
    Diagnostics,
    Test,
    GitStatus,
    GitDiff,
    GitLog,
    GitShow,
    GitBlame,
    GitCommit,
    GitBranch,
    GitStash,
    GitRebase,
    GitCherryPick,
    Remember,
    Forget,
    Wipe,
    Task,
    Skill,
    SearchDocs,
    SearchCode,
    RepoMap,
};

inline constexpr std::array kCatalog = {
    //         名称种类 副作用 提前加载 超时 字符策略
    ToolSpec{"read",            Kind::Read,           {Effect::ReadFs},                     false,   detail::sec{20},   80000,  ToolSpec::TruncStrategy::Head},
    ToolSpec{"edit",            Kind::Edit,           {Effect::ReadFs, Effect::WriteFs},    true,    detail::sec{20},   40000,  ToolSpec::TruncStrategy::Head},
    ToolSpec{"write",           Kind::Write,          {Effect::WriteFs},                    true,    detail::sec{20},   40000,  ToolSpec::TruncStrategy::Head},
    ToolSpec{"move",            Kind::Move,           {Effect::ReadFs, Effect::WriteFs},    false,   detail::sec{20},   4000,   ToolSpec::TruncStrategy::Head},
    ToolSpec{"remove",          Kind::Remove,         {Effect::ReadFs, Effect::WriteFs},    false,   detail::sec{20},   4000,   ToolSpec::TruncStrategy::Head},
    ToolSpec{"shell",           Kind::Bash,           {Effect::Exec},                       true,    detail::sec{0},    30000,  ToolSpec::TruncStrategy::Tail},
    ToolSpec{"process_start",   Kind::ProcessStart,   {Effect::Exec},                       false,   detail::sec{30},   4000,   ToolSpec::TruncStrategy::Tail},
    ToolSpec{"process_poll",    Kind::ProcessPoll,    {},                                   false,   detail::sec{5},    30000,  ToolSpec::TruncStrategy::HeadTail},
    ToolSpec{"process_stop",    Kind::ProcessStop,    {Effect::Exec},                       false,   detail::sec{10},   30000,  ToolSpec::TruncStrategy::Tail},   // 子进程-managed; 末尾-only 匹配 log-stream 使用情况
    ToolSpec{"grep",            Kind::Grep,           {Effect::ReadFs},                     false,   detail::sec{45},   30000,  ToolSpec::TruncStrategy::HeadTail},
    ToolSpec{"glob",            Kind::Glob,           {Effect::ReadFs},                     false,   detail::sec{30},   25000,  ToolSpec::TruncStrategy::Head},
    ToolSpec{"list_dir",        Kind::ListDir,        {Effect::ReadFs},                     false,   detail::sec{20},   25000,  ToolSpec::TruncStrategy::Head},
    ToolSpec{"todo",            Kind::Todo,           {} /* 纯净 */,                        true,    detail::sec{5},    0,      ToolSpec::TruncStrategy::Head},   // 在-memory;从来不大
    ToolSpec{"web_fetch",       Kind::WebFetch,       {Effect::Net},                        false,   detail::sec{30},   30000,  ToolSpec::TruncStrategy::Head},
    ToolSpec{"web_search",      Kind::WebSearch,      {Effect::Net},                        false,   detail::sec{20},   25000,  ToolSpec::TruncStrategy::HeadTail},
    ToolSpec{"find_definition", Kind::FindDefinition, {Effect::ReadFs},                     false,   detail::sec{30},   25000,  ToolSpec::TruncStrategy::HeadTail},
    ToolSpec{"diagnostics",     Kind::Diagnostics,    {Effect::Exec},                       false,   detail::sec{0},    30000,  ToolSpec::TruncStrategy::Tail},
    ToolSpec{"test",            Kind::Test,           {Effect::Exec},                       false,   detail::sec{0},    40000,  ToolSpec::TruncStrategy::Tail},   // 子进程-managed; 末尾-only
    ToolSpec{"git_status",      Kind::GitStatus,      {Effect::ReadFs},                     false,   detail::sec{20},   30000,  ToolSpec::TruncStrategy::HeadTail},
    ToolSpec{"git_diff",        Kind::GitDiff,        {Effect::ReadFs},                     false,   detail::sec{20},   60000,  ToolSpec::TruncStrategy::HeadTail},  // 差异可能很大；更大的预算
    ToolSpec{"git_log",         Kind::GitLog,         {Effect::ReadFs},                     false,   detail::sec{20},   30000,  ToolSpec::TruncStrategy::HeadTail},
    ToolSpec{"git_show",        Kind::GitShow,        {Effect::ReadFs},                     false,   detail::sec{20},   60000,  ToolSpec::TruncStrategy::HeadTail},
    ToolSpec{"git_blame",       Kind::GitBlame,       {Effect::ReadFs},                     false,   detail::sec{20},   40000,  ToolSpec::TruncStrategy::HeadTail},
    ToolSpec{"git_commit",      Kind::GitCommit,      {Effect::WriteFs},                    true,    detail::sec{30},   0,      ToolSpec::TruncStrategy::Head},   // pre-commit钩子可能会很慢；产量保持较小
    ToolSpec{"git_branch",      Kind::GitBranch,      {Effect::WriteFs},                    false,   detail::sec{20},   20000,  ToolSpec::TruncStrategy::Head},   // list/create/switch/delete;产量保持较小
    ToolSpec{"git_stash",       Kind::GitStash,       {Effect::WriteFs},                    false,   detail::sec{30},   50000,  ToolSpec::TruncStrategy::Head},   // list/push/pop/apply/drop/show
    ToolSpec{"git_rebase",      Kind::GitRebase,      {Effect::WriteFs},                    false,   detail::sec{60},   50000,  ToolSpec::TruncStrategy::Head},   // onto/continue/abort/skip;可以运行钩子
    ToolSpec{"git_cherry_pick", Kind::GitCherryPick,  {Effect::WriteFs},                    false,   detail::sec{60},   50000,  ToolSpec::TruncStrategy::Head},   // pick/continue/abort/skip
    // 内存工具-~/.agentty/memory.jsonl(用户)或<workspace>
    // /.agentty/memory.jsonl(项目)中的append/remove记录。
    // 已加载 每次都会在 <learned-memory> 下回到系统 提示词 中。
    // 微小的 IO 足迹，以 src/tool/memory_store.cpp 中的文件帽为界；
    // 输出是一条确认线，永远不会很大。
    ToolSpec{"remember",        Kind::Remember,       {Effect::WriteFs},                    false,   detail::sec{5},    2000,   ToolSpec::TruncStrategy::Head},
    ToolSpec{"forget",          Kind::Forget,         {Effect::WriteFs},                    false,   detail::sec{5},    2000,   ToolSpec::TruncStrategy::Head},
    ToolSpec{"wipe_memory",     Kind::Wipe,           {Effect::WriteFs},                    false,   detail::sec{5},    2000,   ToolSpec::TruncStrategy::Head},
    // 子代理 分派。生成一个独立的代理循环（自己的上下文、
    // 自己的工具预算、深度-capped），该循环运行直至完成
    // 并返回ONE压缩结果。携带 Effect::Exec：子代理
    // 可以运行 bash/write/etc.，因此它至少与
    // bash 一样强大，并且必须具有相同的门控。与子流程工具一样，
    // 在循环内拥有自己的 wall-clock 预算（有界回合数），
    // 因此 覆盖面板 看门狗 为 0。 HeadTail：子代理
    // 的最终摘要在两端携带信号。
    ToolSpec{"task",            Kind::Task,           {Effect::Exec},                       false,   detail::sec{0},    40000,  ToolSpec::TruncStrategy::HeadTail},
    // 技能加载器——从磁盘读取-demand SKILL.md 正文（
    // agentskills.io根：.agentty/.agents/.claude×project/user）。
    // ReadFs; 正文 的技能模块上限为 64 KiB，
    // 因此 64000 个字符的预算空间很大。头部：技能文档显示为top
    // -down。
    ToolSpec{"skill",           Kind::Skill,          {Effect::ReadFs},                     false,   detail::sec{10},   64000,  ToolSpec::TruncStrategy::Head},
    // search_docs-代理document/knowledge RAG。
    // 读取语料库文件（ReadFs）并查询本地Ollama嵌入端点（Net）
    // 以进行混合BM25+密集检索。 HeadTail 保持顶部命中 和
    // 为尾部较低的 -ranked 块； 60 年代涵盖了第一个
    // -call 索引构建+适度语料库的批量嵌入。
    ToolSpec{"search_docs",     Kind::SearchDocs,     {Effect::ReadFs, Effect::Net},        false,   detail::sec{60},   30000,  ToolSpec::TruncStrategy::HeadTail},
    // search_code — SEMANTIC代码检索(grep的混合补码)：
    // BM25+源块上的可选密集嵌入，用于与代码不共享令牌的概念查询。
    // 读取 源码树 (ReadFs) 并查询本地 Ollama 嵌入
    // 端点 (Net)。首先 调用 遍历 + 索引 源码树，
    // 因此 60 年代的预算镜像 search_docs。
    ToolSpec{"search_code",     Kind::SearchCode,     {Effect::ReadFs, Effect::Net},        false,   detail::sec{60},   30000,  ToolSpec::TruncStrategy::HeadTail},
    // repo_map-代码库的aider-style令牌-budgeted
    // PageRank骨架(def/ref图，签名线)。工具本身的输出
    // 已经是预算-packed（通过其自己的预算参数≤60 KB），
    // 因此分派器上限是一个安全网。 Head：地图排名为 best
    // -first，因此从 末尾 截断只会丢失 low-rank 文件。
    ToolSpec{"repo_map",        Kind::RepoMap,        {Effect::ReadFs},                     false,   detail::sec{30},   60000,  ToolSpec::TruncStrategy::Head},
};

// 传输-string → 种类。 `std::nullopt`
// 用于不在 目录 中的名称，因此调用者（状态更新函数 守卫、
// 运行时 分派）可以明确地对未知工具做出反应，而不是默默地通过字符串比较链。
[[nodiscard]] constexpr std::optional<Kind> kind_of(std::string_view name) noexcept {
    for (const auto& s : kCatalog) if (s.name == name) return s.kind;
    return std::nullopt;
}

// 种类→传输-string。总计：每种类型都有一个位于下方 static_assert
// 墙的入口，因此 回退 在 -formed 井中无法到达。
[[nodiscard]] constexpr std::string_view name_of(Kind k) noexcept {
    for (const auto& s : kCatalog) if (s.kind == k) return s.name;
    return {};
}

// 编译-time查找。返回指向规范的指针，如果名称不存在则返回 nullptr。
// 由 运行时 工厂用于从表中填充 `ToolDef::name`
// / `description` / `effects`。
[[nodiscard]] constexpr const ToolSpec* lookup(std::string_view name) noexcept {
    for (const auto& s : kCatalog) if (s.name == name) return &s;
    return nullptr;
}

// ── 日程安排-effects视图 ──────────────────────────────────────
// `effects`回答PERMISSION问题("how much trust does this tool need?")；
// 这回答了 CONCURRENCY 问题 ("what does it actually contend on?")。
// 除了 `task` 之外，它们对于每个工具都是一致的：
//
//   ·任务携带Effect::Exec代表权限——子代理可以运行bash，
//     所以它必须像bash一样gate。
//   ·但是对于SCHEDULING，Exec意味着“排他性：无限的爆炸半径，
//     没有其他东西可以运行”——它序列化每个任务并阻止正在运行
//     的子代理后面的所有同级工具。这违背了该工具的全部目的：
//     任务精确存在，因此模型可以扇出多个孤立的调查
//     CONCURRENTLY（出于同样的原因，Claude
//     代码以并行波方式运行其任务工具）。子代理自己的工具调用
//     在其循环内单独为副作用-gated + path
//     -scheduled，因此父将其调度为coarse
//     -Exec是内部分派已经管理的double-counting争用。
//
// 因此，任务调度为{ReadFs，Net}——可与读取、
// 其他任务以及不相交的-path写入器组合——而其权限门保持Exec。
// 任何具有相同分割的未来工具都会在这里占据一行。
[[nodiscard]] constexpr EffectSet sched_effects(const ToolSpec& s) noexcept {
    if (s.name == "task") return {Effect::ReadFs, Effect::Net};
    return s.effects;
}

// 修复了-string非-type模板参数，以便工具工厂可以编写
// `spec::require<"bash">()`并在编译时间捕获
// 拼写错误。实例化站点在常量表达式中计算 `lookup`，
// 并在结果上计算 `static_assert`。
template <std::size_t N>
struct FixedName {
    char data[N];
    consteval FixedName(const char (&s)[N]) {
        for (std::size_t i = 0; i < N; ++i) data[i] = s[i];
    }
    [[nodiscard]] constexpr std::string_view view() const noexcept {
        return {data, N - 1};   // 条带尾随 NUL
    }
};

// `spec::require<"bash">()`返回"bash"的目录条目。
// 如果不存在具有该名称的条目，则出现 编译 错误
// — 无法静默创建名称不在 目录 中的工具。
template <FixedName Name>
[[nodiscard]] consteval const ToolSpec& require() {
    constexpr std::string_view name_v = Name.view();
    constexpr const ToolSpec* s = lookup(name_v);
    static_assert(s != nullptr,
                  "tool name not in agentty::tools::spec::kCatalog — add an "
                  "entry there before calling spec::require<...>");
    return *s;
}

// ── 编译-time 正确性证明 目录 ───────────────────────
// 这些是安全网：读者可能假设的关于工具能力的每个
// 属性都在构建时得到验证。任何编辑上述 目录
// 的人如果破坏了 不变量，都会立即收到信号。
namespace proofs {

// 消毒剂上的NOTE构建：GCC的ASan/UBSan仪器模块-level
// `constexpr kCatalog`全局，并且该仪器泄漏到下面的校样的
// `consteval`评估中("not a constant expression")。
// 证明是有效的——它们编译并通过每个NORMAL构建，这是真正的门。
// 在 AGENTTY_SANITIZER_BUILD 下，我们跳过 re-evaluating 它们，
// 以便消毒剂可以完成其实际工作（RUNTIME 内存-safety 检查）。有关完整原理，
// 请参阅 CMake AGENTTY_SANITIZE_ALL 块。
#ifndef AGENTTY_SANITIZER_BUILD

// 目录中的每个名称都是唯一的。
consteval bool all_names_unique() {
    for (std::size_t i = 0; i < kCatalog.size(); ++i)
        for (std::size_t j = i + 1; j < kCatalog.size(); ++j)
            if (kCatalog[i].name == kCatalog[j].name) return false;
    return true;
}
static_assert(all_names_unique(), "tool catalog has duplicate names");

// 每个种类的手臂在目录中只出现一次。与名称唯一性检查配对：它们一起证明
// `kind_of(name)` 是单射的，
// 而 `name_of(kind)` 是全射的。
consteval bool kinds_bijective() {
    constexpr Kind kAll[] = {
        Kind::Read, Kind::Edit, Kind::Write, Kind::Move, Kind::Remove,
        Kind::Bash, Kind::ProcessStart, Kind::ProcessPoll, Kind::ProcessStop,
        Kind::Grep, Kind::Glob, Kind::ListDir, Kind::Todo,
        Kind::WebFetch, Kind::WebSearch, Kind::FindDefinition,
        Kind::Diagnostics, Kind::Test, Kind::GitStatus, Kind::GitDiff,
        Kind::GitLog, Kind::GitShow, Kind::GitBlame, Kind::GitCommit,
        Kind::GitBranch, Kind::GitStash, Kind::GitRebase, Kind::GitCherryPick,
        Kind::Remember, Kind::Forget,
        Kind::Wipe,
        Kind::Task,
        Kind::Skill,
        Kind::SearchDocs,
        Kind::SearchCode,
        Kind::RepoMap,
    };
    if (std::size(kAll) != kCatalog.size()) return false;
    for (auto k : kAll) {
        int hits = 0;
        for (const auto& s : kCatalog) if (s.kind == k) ++hits;
        if (hits != 1) return false;
    }
    // 对于每一行，相反的情况成立（名称↔种类round-trip）。
    for (const auto& s : kCatalog) {
        auto k = kind_of(s.name);
        if (!k || *k != s.kind) return false;
        if (name_of(s.kind) != s.name) return false;
    }
    return true;
}
static_assert(kinds_bijective(),
              "spec::Kind and kCatalog must be in bijection — every Kind arm "
              "needs exactly one row whose `kind` matches and whose `name` "
              "round-trips through kind_of/name_of");

// 每个工具都有一个非-empty名称（传输需要它）。
consteval bool all_names_present() {
    for (const auto& s : kCatalog) if (s.name.empty()) return false;
    return true;
}
static_assert(all_names_present(), "every tool needs a non-empty name");

// 查找适用于每个 目录 条目。
static_assert(lookup("shell")    != nullptr);
static_assert(lookup("git_commit") != nullptr);
static_assert(lookup("nonexistent") == nullptr);

// 能力不变量——我们希望永远不违反的规则。

// `bash`和`diagnostics`是仅有的Exec工具；
// 没有其他方法可以执行任意代码。
consteval bool only_known_exec_tools() {
    for (const auto& s : kCatalog) {
        if (!s.effects.has(Effect::Exec)) continue;
        if (s.name != "shell" && s.name != "diagnostics" && s.name != "test"
            && s.name != "process_start" && s.name != "process_stop"
            && s.name != "task")
            return false;
    }
    return true;
}
static_assert(only_known_exec_tools(),
              "Only `bash`, `diagnostics`, and `task` may carry Effect::Exec — "
              "adding another Exec tool requires a separate review and updating "
              "this static_assert");

// 改变文件系统的工具必须不是 也是 Exec——这些
// 工具严格来说更危险，并且如果它们两者都需要，
// 则属于 bash 系列。 （保持政策表干净。）
consteval bool no_writefs_and_exec_combo() {
    for (const auto& s : kCatalog)
        if (s.effects.has(Effect::WriteFs) && s.effects.has(Effect::Exec))
            return false;
    return true;
}
static_assert(no_writefs_and_exec_combo(),
              "no tool may carry both WriteFs and Exec — promote to bash");

// 纯工具必须有空的副作用。
static_assert(lookup("todo")->effects.empty());

// Read-side工具必须NOT有WriteFs/Net/Exec。
consteval bool readonly_invariants() {
    constexpr std::string_view kReadOnly[] = {
        "read","grep","glob","list_dir","find_definition","repo_map",
        "git_status","git_diff","git_log","git_show","git_blame",
    };
    for (auto n : kReadOnly) {
        auto* s = lookup(n);
        if (!s) return false;
        if (s->effects.has(Effect::WriteFs)) return false;
        if (s->effects.has(Effect::Exec))    return false;
        if (s->effects.has(Effect::Net))     return false;
    }
    return true;
}
static_assert(readonly_invariants(),
              "a tool listed as read-only carries a write/exec/net capability");

// 网络工具必须是网络工具。
consteval bool only_web_is_net() {
    for (const auto& s : kCatalog) {
        if (!s.effects.has(Effect::Net)) continue;
        if (s.name != "web_fetch" && s.name != "web_search" &&
            s.name != "search_docs" && s.name != "search_code") return false;
    }
    return true;
}
static_assert(only_web_is_net(),
              "Only web_fetch/web_search/search_docs/search_code may carry Effect::Net");

// ── 人数 -tool 超时 证明 ─────────────────────────────────────────────
// 固定 wall-clock-watchdog 值，以便粗心的编辑（
// 在错误的工具上设置 0，或在快速工具上设置 6000）破坏构建，
// 而不是默默地改变 运行时 行为。

// Subprocess-managed工具(bash、
// 诊断)在`subprocess.cpp`中有自己的超时； 覆盖面板 看门狗 必须为 0 以避免
// 出现 double-gating。
consteval bool subprocess_tools_have_no_overlay_timeout() {
    auto* b = lookup("shell");
    auto* d = lookup("diagnostics");
    return b && d && b->max_seconds == std::chrono::seconds{0}
                  && d->max_seconds == std::chrono::seconds{0};
}
static_assert(subprocess_tools_have_no_overlay_timeout(),
              "bash/diagnostics must have max_seconds=0 — they own their timeout");

// 每个其他工具MUST都具有有限的、合理的覆盖面板 超时。
// 零意味着 "never time out"，而这就是 看门狗
// 的存在要防止的错误。上限为 5 分钟，这样任何工具都无法让
// 代理停留的时间超过用户的耐心。
consteval bool other_tools_have_bounded_timeout() {
    using std::chrono::seconds;
    for (const auto& s : kCatalog) {
        // bash/diagnostics拥有其子进程超时; `task` 通过
        // 有界的 sub-agent 回合计数拥有其预算（没有对 看门狗 的单个
        // 系统调用）。三者都故意设置了 max_seconds=0。
        if (s.name == "shell" || s.name == "diagnostics" || s.name == "test"
            || s.name == "task")
            continue;
        if (s.max_seconds < seconds{1} || s.max_seconds > seconds{300}) return false;
    }
    return true;
}
static_assert(other_tools_have_bounded_timeout(),
              "non-subprocess tools (except `task`) need a max_seconds in [1, 300]");

// `web_fetch`不能等待比底层http总
// 超时更长的时间，否则看门狗会在客户端仍然愉快地
// 阻塞在慢速服务器上时触发。
static_assert(lookup("web_fetch")->max_seconds <= std::chrono::seconds{30},
              "web_fetch overlay timeout must be ≤ http total (30s)");

// ── 截断策略与工具的 副作用 形状相关 ─────────
// 每个TruncStrategy保留输出的不同片段，并且该选择应该遵循工具DOES
// 的内容——而不是根据喜好来设置每个-row。将相关性固定为 不变量 可在构建时捕获
// 新增 Exec 工具默认采用 Head，导致 bash 输出保留开头而非末尾。
// 类错误，
// 而不是在用户报告中
// 捕获。
//
//   Effect::Exec→末尾(日志数据流：最新行
//   获胜)Effect::Net→头或HeadTail(第正文页
//   OR搜索结果)仅Effect::WriteFs→头(按顺序
//   回显其写入的块)纯→头(在-memory状态没有"tail"语
//   义)ReadFs→头或HeadTail（取决于工具；见下文）
//
// ReadFs有两种合法的形状：线性文件读取器(read、list_dir、glob)更喜欢Head；
// search-like 工具（grep、find_definition、git_*）更
// 喜欢 HeadTail 来保留 "N more matches"/摘要 末尾。
consteval bool truncation_matches_effect_shape() {
    for (const auto& s : kCatalog) {
        // 0表示没有上限——策略不适用，跳过。
        if (s.max_output_chars == 0) continue;

        if (s.effects.has(Effect::Exec)) {
            // `task` 是 Exec-class，但其输出是结构化子代理摘要，
            // 而不是日志 数据流 — HeadTail 保留
            // 开头帧和最终结果。 bash/diagnostics
            // 保留 末尾（最新日志行获胜）。
            if (s.name == "task") {
                if (s.trunc_strategy == ToolSpec::TruncStrategy::Tail) return false;
                continue;
            }
            if (s.trunc_strategy != ToolSpec::TruncStrategy::Tail) return false;
            continue;
        }
        if (s.effects.has(Effect::Net)) {
            // 网络工具按用例划分：fetch-a-page想要Head（
            // lede是前导），search-results想要HeadTail
            // （"N more results"页脚承载信号）。
            // 末尾 没有意义 — 网络响应不会记录 数据流。
            if (s.trunc_strategy == ToolSpec::TruncStrategy::Tail) return false;
            continue;
        }
        if (s.effects.has(Effect::WriteFs) && !s.effects.has(Effect::ReadFs)) {
            if (s.trunc_strategy != ToolSpec::TruncStrategy::Head) return false;
            continue;
        }
        // 剩余：纯或read-only-ish工具——Head和
        // HeadTail都是可防御的；只是排除末尾（这里没有工具是日志
        // 数据流）。
        if (s.trunc_strategy == ToolSpec::TruncStrategy::Tail) return false;
    }
    return true;
}
static_assert(truncation_matches_effect_shape(),
              "a tool's trunc_strategy doesn't match its effect shape — see "
              "truncation_matches_effect_shape for the rules");

// 每个具有 `max_output_chars > 0` 的工具都必须具有
// 合理的最小值，以便省略标记（一行，约 30 个字符）仍然为内容留有空间。
consteval bool output_caps_are_sane() {
    for (const auto& s : kCatalog) {
        if (s.max_output_chars == 0) continue;
        if (s.max_output_chars < 1000)       return false;  // 地板：为标记留出空间
        if (s.max_output_chars > 200'000)    return false;  // 上限：~50k 代币
    }
    return true;
}
static_assert(output_caps_are_sane(),
              "a tool's max_output_chars is outside the sane [1000, 200000] band");

// FGTS (eager_input_streaming)仅当工具产生其
// 参数mid-stream-i.e时才有意义。对于 JSON arg
// blob 足够大的工具，流式传输 早期的大括号很重要。我们在这里不强制
// 执行严格的白名单（该策略可能会随着 Anthropic 向前滚动模型而改变），
// 但我们 DO 要求标记为 提前加载 的工具至少有一个 副作用 —
// 一个纯粹的 no-op 工具获得 提前加载 流式传输 几乎肯定是
// 来自较重邻居的 copy-paste 错误。
//
// 例外：`todo`是纯粹的并且有意提前加载，因为它的arg
// blob（项目列表）是模型发出的唯一内容，并且early
// -open行为让UI 渲染成为in-progress列表，
// 而模型仍在输入它。
consteval bool eager_streaming_is_justified() {
    for (const auto& s : kCatalog) {
        if (!s.eager_input_streaming) continue;
        if (s.name == "todo") continue;          // 记录的异常
        if (s.effects.empty()) return false;     // 纯+提前加载可疑
    }
    return true;
}
static_assert(eager_streaming_is_justified(),
              "a pure (effect-less) tool other than `todo` opted into "
              "eager_input_streaming — review the rationale");

// ── 排程-effects 差异证明 ─────────────────────────────────
// `sched_effects`回答了CONCURRENCY问题并且
// 对于一个工具来说允许与`effects`(PERMISSION问题)不同：
// `task`。分歧是 加载-bearing——它让子代理同时扇出，
// 而不是在粗略的 Exec 锁 后面串行——但到目前为止还没有
// 任何东西固定它。重命名 `task`、对其 目录 副作用
// 进行更改或对 `sched_effects` 进行杂散
// 编辑可能会默默地崩溃分歧（每个子代理都重新-serialising）
// 或将其传播到另一个工具（让 Exec 工具意外运行）。
// 这些证明使得任何此类漂移在引入时都成为构建错误。

// 对于每个工具EXCEPT `task`，调度副作用==权限 副作用。
// 没有工具可以悄悄地获取与策略门控的
// 能力不同的调度身份。
consteval bool sched_effects_match_except_task() {
    for (const auto& s : kCatalog) {
        if (s.name == "task") continue;   // 一批准分歧
        if (sched_effects(s) != s.effects) return false;
    }
    return true;
}
static_assert(sched_effects_match_except_task(),
              "a tool other than `task` has sched_effects != effects — "
              "scheduling identity must match the permission capability set "
              "for every tool but the sanctioned `task` divergence");

// `task`的分歧是EXACT，并非偶然：它作为Exec进行门
// 控（子代理可以运行bash，因此权限必须严格），但SCHEDULES
// 作为{ReadFs，Net}（可与读取、其他任务和不相交的
// -path写入器组合）。将两半固定住，这样两半都不会独立腐烂。
consteval bool task_divergence_is_exact() {
    auto* t = lookup("task");
    if (!t) return false;
    // 权限方：严格Exec，丝毫不弱。
    if (!t->effects.has(Effect::Exec)) return false;
    // 调度侧：精确地{ReadFs，Net}-执行DROPPED，
    // 因此子代理不会在粗略的独占锁后面序列化。
    auto sched = sched_effects(*t);
    if (sched.has(Effect::Exec))     return false;
    if (!sched.has(Effect::ReadFs))  return false;
    if (!sched.has(Effect::Net))     return false;
    if (sched.has(Effect::WriteFs))  return false;
    return true;
}
static_assert(task_divergence_is_exact(),
              "`task` must gate as Exec but schedule as exactly {ReadFs, Net} — "
              "the divergence that lets subagents run concurrently");

#endif  // AGENTTY_SANITIZER_BUILD

} // namespace proofs

} // namespace agentty::tools::spec
