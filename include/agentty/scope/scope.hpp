#pragma once
// agentty::scope——配置-resolution代数。
//
// 一个地方回答
// 配置位于何处，由谁放置，以及是否允许执行其中内容。
// 每个配置问题（内存、技能、代理、命令、MCP 插件、钩子）用于将 -roll
// 自己的答案以不同的、巧妙的-incompatible 形状处理：
//
//   ·内存范围{用户，项目}→一条路径；项目以可写性为门控。
//   ·技能一6-root天梯，第-name-wins影子，项目▷用户。
//   ·代理同一梯子，第二次手工搭建。
//   ·命令第三次手工构建的同一梯子。
//   · MCP resolve_config()：环境 ▷ 项目 ▷ 用户，一个 获奖文件，以及
//              粗略 env-var "trust" 门（AGENTTY_MCP_ALLOW_PROJECT)。
//   ·挂钩内容-hash批准存储（ONE正确的信任模型）。
//
// 这些是假装完整的五个部分函数——`config_path(bool project)`、
// `resolve_config()`在未命中时返回空路径、`Scope`枚举忘记值来自哪里。
// 这个 头文件 用 源码树 的其余部分已经说过的形状替换它们：返回 `std::expected`
// 的总函数（参见 maya/core/expected.hpp —
// "Rust has Result<T,E>; we have algebraic types composed monadically"），
// 和类型 超过 `枚举 类：
// uint8_t`，以及作为值携带的出处，因此非法状态——
// 没有来源的配置，从轨迹推断的信任——是无法通过构造来表示的。
//
// PURITY CONTRACT (这是 TEA 代码库)。分辨率是显式 -passed
// `Env` 的纯函数。这里没有涉及 std::getenv 或 cwd：
// 边缘将它们解析为 `Env`，然后每个解析器都是确定性的 归并，
// 状态更新函数 可以在使用制造的 Env 的测试中进行练习。
//
// 请参阅 docs/design/plugin-model.md 了解
// 激发此行为的消费者，以及底部的迁移注释以了解采用它的订单功能。

#include <cstdint>
#include <algorithm>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <expected>

#include "agentty/util/io.hpp"
#include "agentty/util/lock_levels.hpp"

namespace agentty::scope {

namespace fs = std::filesystem;

// ── 锁链: WHOSE 配置, 作为优先的纹章 ───────────────────
// 声明顺序IS决议顺序——最权威优先。
// 优先级曾经存在于此，因此没有特征re-hardcodes "env then project then user"。
// `rank()`将订单转化为可比的实力。
//
//   显式的env-pointed文件($AGENTTY_*_CONFIG)
//              -用户将command-line/environment箭头瞄准它；最权威。
//   本地项目-private和UNCOMMITTED（e.g..agentty/*.local.*，
//              已被 Git 忽略，只属于本机当前仓库，不与他人共享。
//              现在保留，以便代数完成；仅当存在真正的第二个
//              使用者时才连接到解析器（请参阅迁移说明）。
//   项目已提交，与团队共享（<workspace>/.agentty/…）。
//   该用户的全局用户，跨越每个工作区 (~/.agentty/…)。
enum class Locus : std::uint8_t { Explicit, Local, Project, User };

// 强度=优先顺序中的位置。排名较低者获胜。
[[nodiscard]] constexpr int rank(Locus l) noexcept {
    return static_cast<int>(l);
}

[[nodiscard]] constexpr std::string_view to_string(Locus l) noexcept {
    switch (l) {
        case Locus::Explicit: return "explicit";
        case Locus::Local:    return "local";
        case Locus::Project:  return "project";
        case Locus::User:     return "user";
    }
    return "user";  // total:不可达，但边界不偏
}

[[nodiscard]] constexpr std::optional<Locus> parse_locus(std::string_view s) noexcept {
    if (s == "explicit") return Locus::Explicit;
    if (s == "local")    return Locus::Local;
    if (s == "project")  return Locus::Project;
    if (s == "user")     return Locus::User;
    return std::nullopt;  // ONE部分边界——在字符串边缘
}

// ── 横线:目录CONVENTION,正对位置 ────────────────
// 来源的部落。 `.agentty` 是本机； `.agents`/`.claude`
// 是为了 drop-in 兼容性而读取的互操作约定 agentty。声明顺序是
// inside-locus 影子决胜局（本机影子互操作）。这是 Locus
// 的 SEPARATE 轴，因此 6-root 梯子是一次生成的产品（
// Locus × Dialect），而不是四个手-written 阵列。
enum class Dialect : std::uint8_t { Agentty, Agents, Claude };

[[nodiscard]] constexpr std::string_view dir_name(Dialect d) noexcept {
    switch (d) {
        case Dialect::Agentty: return ".agentty";
        case Dialect::Agents:  return ".agents";
        case Dialect::Claude:  return ".claude";
    }
    return ".agentty";
}

// ── 对话框 BREADTH : a / 单 -feature 选择,而不是全球 ───────────────────
//
// 特征读取哪些方言是属性OF 该 FEATURE，因为它回答了只有该特征
// 可以的问题：相同的字节在两个工具中都工作吗？一个技能是markdown跟
// frontmatter和Claude读取同一个文件，
// 所以读取`.claude/skills/`是免费兼容的。 `mcp.json`
// 根本不是 `.agents`/`.claude` 约定 - 这些工具不会将该
// 文件放在那里，它的 模式定义 是我们的。
//
// 这曾经是 plan() 内部的 一个 数组，由每个调用者共享，因此梯子每个位置
// 总是具有三种方言，并且 MCP 将多余的内容过滤回 下游 (
// `if (src.dialect != Dialect::Agentty) continue;`)。
// 一个决策有两个决策站点：plan()报告了六个来源，
// 其中三个曾被阅读过，因此建立在plan()上的任何东西——
// `config where`命令、文档、测试——描述了不存在的行为。
// 添加方言 LOOKED 就像它扩大了 MCP 一样，但默默地什么也没做，
// 因为 下游 过滤器仍然丢弃了它。
//
// 将选择移到布局上会将其折叠到一个站点。它还将默认值翻转到
// 安全端：一个功能现在获得 `.agentty` 仅 并且必须
// 选择 INTO 互操作，而在此之前它获得了所有三个并且必须
// 记住缩小范围。现在忘记会失败——它会少读取一个目录，
// 而不是意外读取克隆的 仓库 的 `.claude/`。

// 仅限本机。默认值：没有其他人编写的格式。
inline constexpr Dialect kNativeOnly[] = {Dialect::Agentty};

// 本机加上互操作约定，按影子顺序（本机获胜）。
// 对于PORTABLE内容——技能、代理、命令——其中相同文件在
// 另一个工具中有效并且读取它们的文件是drop-in兼容性。
inline constexpr Dialect kPortable[] = {
    Dialect::Agentty, Dialect::Agents, Dialect::Claude};

// ── 资料来源:一个已解决的、标记的起源——归并号运载器。 ───────────
// 知道其自身来源的值。由解析器构造 仅（通过 plan()），因此您无法在
// 没有源的情况下构造配置："wrote the edit to the wrong file"
// bug 类在结构上变得不可能，因为要写回的文件是
// `Source::base`，而不是 re-derived
// config_path(bool)。
struct Source {
    Locus    locus{Locus::User};
    Dialect  dialect{Dialect::Agentty};
    fs::path base;              // 该映射到的具体<…>/.agentty目录
    bool     writable = false;  // 内存的项目-writability门，一般

    [[nodiscard]] bool operator==(const Source&) const = default;
};

// T标记有WHERE，它来自。每个已解决的项目都带有其来源，因此 选择面板
// 可以标记 "[project]" / "[user·claude]"，
// 并且编辑器可以定位确切的文件 - 每个功能都无需定制来源管道。
template <class T>
struct Tagged {
    T      value;
    Source source;
};

// ── 信封:已解决的边缘,按价值(纯度合同)传递 ──────────
// 读取 仅 位置环境状态（HOME、cwd/workspace、
// $AGENTTY_*_CONFIG)是在进程边缘处构建 Env 的地方。
// 解析器通过 const& 获取它，因此它们保持纯净，并且测试构造一个虚构的
// Env 来确定性地驱动任何解析。
struct Env {
    fs::path                home;         // ~（空⇒User/…/跳过互操作）
    // 本机User-locus基础：util::user_root()(
    // ~/.agentty，或当被覆盖时$AGENTTY_HOME)。
    // 与 `home` 分开携带，因此互操作方言（~/.agents、
    // ~/.claude）保持锚定在 ~，而本机目录遵循覆盖。空 ⇒ 与 home/
    // ".agentty" 相同，不可用。
    fs::path                user_native_base;
    // 活动 PROJECT目录(util::project_root()：夹在访问边界
    // 内的发射cwd)，NOT是可加宽的workspace_root()。
    // 这是 Project/Local 配置锚点，因此 `--workspace /` 下的
    // `read` 仍然解析 仓库-relative 并且 内存 不会落在 /.agentty 处。
    // 空或"/"⇒没有可用的项目轨迹（与内存的防护相匹配）。
    fs::path                project_root;
    std::optional<fs::path> explicit_config;  // $AGENTTY_<FEATURE>_CONFIG 文件

    // 当项目存储实际上在这里可写时（project_root是真实的、可写的目录——
    // 不是"/"或read-only挂载），则为真。概括 dir_path_writable
    // 检查 内存 在 OFFERING 项目范围之前执行的操作。
    bool project_writable = false;
};

// ── 版式: 如何存储特性, 描述 BY 此特性 ───────────────
// Scope 对它的调用者是谁一无所知——没有 枚举 的功能，
// 没有文件名，没有在这里烘焙的环境变量（这会反转依赖关系并将这个原
// 语变成所有 下游 的 注册表）。调用者将范围分配给 `Layout`：
// 每个根下所需的叶子，以及它是单个文件（override
// -resolved）还是集合目录（union-resolved）。
// 这就是整个合同——范围规定了根源和 归并；
// 该功能拥有其身份。
struct Layout {
    // 每个<base>/<dialect-dir>下的叶：单个-file关注点（
    // "memory.jsonl"、"mcp.json")的文件名，
    // 或集合的子目录（"skills"、"agents")。范围只会将其连接到基础上。
    std::string_view leaf;

    // 可选的 Explicit-locus 覆盖：如果该环境变量命名现有文件，
    // 则它成为最高的-precedence源。空⇒没有逃生舱口。
    std::string_view explicit_env = {};

    // WHICH方言此特征以影子顺序读取。默认为仅限本机 (
    // kNativeOnly) — 请参阅上面的注释 dir_name()：
    // 只有当 相同 BYTES 在两种工具中都工作时，方言才在此处获得一席之地。
    // 可移植内容（技能、代理、命令）通过 kPortable；
    // 我们拥有的所有格式都不会影响这个。
    //
    // plan()准确地发出这些，而不发出其他任何东西，
    // 因此调用者可以打印的梯子就是它读取的梯子。之后没有消费者过滤。
    std::span<const Dialect> dialects = kNativeOnly;
};

// ── 信托:第一 -class ,将输入 CONTENT - 从Locus那里推断不出 ──────
// MCPoison课程(CVE-2025-54136)表述为
// 一种类型：光标将信任固定到服务器的NAME，因此在批准的名称下
// 交换命令保持批准-静默持久化 RCE。这里的信任是（源、
// 内容-hash、批准）的函数，批准由内容哈希作为密钥并
// 持久保存任何提交的文件OUTSIDE，因此克隆的仓库无法批准其
// 自己的可执行配置。
struct Trusted {};                              // 用户放置或批准
struct Pending { std::string content_sha; };    // 需要经过深思熟虑的批准
struct Blocked { std::string reason; };          // 拒绝（e.g.坏工作区）
using Trust = std::variant<Trusted, Pending, Blocked>;

// 内容-hash批准存储（钩子'hooks_approved.json，广义）。
// 存在于USER根下，因此仓库永远不能为自己提供担保。
struct Approvals {
    [[nodiscard]] bool approved(std::string_view content_sha) const noexcept;
    void               approve(std::string_view content_sha);

    std::vector<std::string> shas;  // 排序；小——一些条目
};

// 持久化用于批准存储，在USER根(~/.agentty/<leaf>)下。
// 克隆的仓库不能写入这里，因此它不能预先写入-approve自己的内容。
// 当文件为missing/malformed时，load()返回
// 空存储（关闭失败：无批准→项目配置保持待处理），并且不采用锁。
// save()合并到文件的cross-process部分内；如果出现 IO 错误，
// 则返回 false。
[[nodiscard]] Approvals load_approvals(std::string_view leaf) noexcept;
[[nodiscard]] bool      save_approvals(Io, const lock_levels::below_file_lane&,
                                       std::string_view leaf, const Approvals&) noexcept;

// 配置blob的稳定内容散列——身份信任所绑定。
// 改变字节 (在批准的名称下交换命令）并且散列改变，
// 因此批准不再匹配：MCPoison re-gate。
[[nodiscard]] std::string content_hash(std::string_view bytes) noexcept;

// 总计：(来源、内容、批准)→信任。 Explicit/User
// 是隐式可信的（人类放置它们）。 Project/Local 携带
// 可执行配置启动 待处理，并且仅当 此 内容哈希被批准时才变为可信。
[[nodiscard]] Trust trust_of(const Source& src,
                             std::string_view content_sha,
                             const Approvals& approvals) noexcept;

// ── 域差错——以下每std::expected中E字 ─────────────────────
// agentty给予每个子系统其自己的错误总和(OAuthError，HttpError，
// ToolError，io::fsm::DomainError)；范围跟随而不是借用
// maya::Error，因此范围-specific 上的调用者模式-matches 失败类型。
enum class ErrorKind : std::uint8_t {
    NoSource,       // 计划中没有源产生值（resolve_first)
    ParseFailed,    // 源存在，但其字节没有解析
    NotWritable,    // 编辑以存储不可写的源为目标
    Io,             // 文件系统 read/write 失败
};

struct Error {
    ErrorKind   kind;
    std::string message;
};

template <class T>
using Result = std::expected<T, Error>;

[[nodiscard]] inline std::unexpected<Error> fail(ErrorKind k, std::string msg) {
    return std::unexpected{Error{k, std::move(msg)}};
}

// ── plan(): 给定一个 Env 的布局命令源列表 ────────────
// 单个发电机。按优先顺序发出源（Locus-major、Dialect
// -minor）：显式？ ▷ 项目×{代理,代理,Claude}
// ▷ 用户×{代理,代理,Claude}. Local 仅针对选择加入的
// 布局发出（目前没有 - 请参阅注释）。空根（没有 HOME，不可写的项目）
// 根本不会被发射，所以 下游 归并 永远不会特殊 -case 它们。
[[nodiscard]] std::vector<Source> plan(const Layout& layout, const Env& env);

// ── 归并:一个来源列表上的两个单体 ────────────────────────────
// 优雅的核心。两个解析器均按优先顺序遍历 plan(feature,
// env)；它们的不同之处仅在于如何COMBINE每个来源产生什么。
//
// `Load`读取ONE源：源->结果<std::optional<T>>。 nullopt 表示
// "this source simply isn't present"（不是错误）；错误表示 "present but broken"
// (e.g.ParseFailed) 和短-circuits。

// OVERRIDE 幺半群 — 第一个存在的源赢得整个值。 single
// -file 的答案涉及：内存、钩子和 MCP-as-one-file。
// 如果每个源都不存在，则返回 NoSource。
template <class T, class Load>
[[nodiscard]] Result<Tagged<T>>
resolve_first(const Layout& layout, const Env& env, Load&& load) {
    for (const Source& src : plan(layout, env)) {
        Result<std::optional<T>> got = load(src);
        if (!got) return std::unexpected{std::move(got).error()};  // 存在但已损坏
        if (got->has_value())
            return Tagged<T>{std::move(**got), src};               // 先击者获胜
    }
    return fail(ErrorKind::NoSource, "no source provided a value");
}

// UNION monoid —合并每个存在的源，
// 第一个-key-wins影子，每个幸存的项目标记有它来自的源。
// 收集的答案涉及：技能、代理、命令（以及 MCP，
// 一旦合并项目+用户服务器而不是选择一个文件）。 `key` 将
// 一个项目投射到其影子键（技能名称、服务器名称）；早期的来源在
// 关键冲突上影响了后来的来源——这正是项目 ▷ 用户、原生 ▷ 互操作，
// 免费的。一个损坏的源short-circuits（大声失败，
// 不要默默地放弃一半配置）。
template <class T, class Key, class Load>
[[nodiscard]] Result<std::vector<Tagged<T>>>
resolve_union(const Layout& layout, const Env& env, Key&& key, Load&& load) {
    std::vector<Tagged<T>>   out;
    std::vector<std::string> seen;   // 影子钥匙已被获胜者认领
    for (const Source& src : plan(layout, env)) {
        Result<std::vector<T>> items = load(src);   // 加载:源->结果<vector<T>>
        if (!items) return std::unexpected{std::move(items).error()};
        for (T& item : *items) {
            std::string k{key(item)};
            if (std::find(seen.begin(), seen.end(), k) != seen.end())
                continue;                            // 受到早期来源的影响
            seen.push_back(std::move(k));
            out.push_back(Tagged<T>{std::move(item), src});
        }
    }
    return out;
}

// 构建一次进程-edge环境（读取HOME/project_root/项目可写性）。
// 这里的ONE不纯函数；保持很小并且远离解析器，
// 因此上面的所有内容都可以按值进行测试。如果设置并命名现有文件，
// 布局的 explicit_env 也会被捕获。
[[nodiscard]] Env current_env(const Layout& layout);

}  // namespace agentty::scope

// ── 移徙说明 ─────────────────────────────────────────────────────────
// 采用行为-preserving，最小-first，每次改变一个特征：
//   1.内存-FIRST消费者（完成）。 path_for(Scope) 现在构建一个
//                 Env (拥有其更丰富的getpwuid_r家庭分辨率）
//                 并将plan(Layout{"memory.jsonl"}，
//                 env)过滤到所请求的轨迹×本机方言。行为相同；
//                 项目-clamp + 可写性保护现在位于范围内。这证明了plan()；
//                 当 内存 读取时，resolve_first 着陆（与
//                 写道)跨范围合并。
//   2. skills/agents/commands——工会。四手-written根数组
//                 折叠成plan()+resolve_union；
//                 影子语义由first-key-wins 规则保留。
//   3. MCP — 最后，以及唯一一个表示 GAINED 的行为（完成）。
//                 resolve_union合并项目+用户服务器，编辑到item.source.base的路由
//                 （修复了错误的-file bug），
//                 并且AGENTTY_MCP_ALLOW_PROJECT环境门现在只是
//                 trust_of()+Approvals前面的back-compat pre
//                 -check（修复了MCPoison-class暴露）。
//
//                 bridge.cpp自己的resolve_config
//                 ()——返回一个获胜文件的SECOND梯子——被删除。
//                 它在最糟糕的方向上与 plan() 存在分歧：
//                 mcp.json 项目默默地停止了每台 USER
//                 服务器的运行，而每个表面仍然列出它们。它穿过四个函数的
//                 `bool project_local` 也消失了；
//                 这是当 Source::locus 已经携带它时手工
//                 重建的出处。
//
// Locus::Local现在作为VALUE运送（代数完成
// +未来-proof），但在具体的第二个消费者出现之前不
// 连接到解析器——避免只有一个特征读取的格值。
