#pragma once
// agentty::dirs — WHERE agentty 的 字节 住在那里，而 HOW LONG 他们住在那里。
//
// 将-side写入对端的agentty::scope。
//
// 范围配置LOOKUP总计：一个代数（轨迹×方言），每次命中的出处，
// 没有调用者re-deriving优先级。
// 它回答 "where do I READ this from?"。
//
// 没有任何答案回答 "where do I WRITE this to?"，
// 并且配置在范围之前的方式增长了一半：在每个 调用 站点一次做出一个决定。
// 测量结果：
//
//   -内存将其项目路径固定到project_root()
//   - rag 在 FIVE 单独站点使用原始 fs::current_path()
//   -技能故意传递"."并携带NOTE解释其原因
//     不同意内存
//
// 解释为什么一个消费者不同意另一个消费者的评论是
// 缺失基元的形状。
//
// 并且根本没有LIFECYCLE。 user_root 的 resolve_subdir
// () 回答 `where`；没有函数回答 `for how long`。
// 这就是为什么来自被取代的命名方案的 47 MB .ragdb 可以永远保留在
// 磁盘上 (#62) — 泄漏不是 rag 代码中的疏忽，而是没有任何人询问。
//
// ── 合同,为什么它形状 像范围 ──────────────────────
//
// 调用者移交规范：哪个根、哪个叶、哪个覆盖var、
// 它的寿命有多长。 dirs 知道 无内容 的调用者是谁
// ——没有 枚举 的功能，这里没有文件名。这将反转依赖
// 关系并将这个原语变成所有 下游 的 注册表，
// 这正是使 scope::Layout 工作的属性。
//
//     agentty::scope:布局→读取根，
//     按优先级agentty::dirs:规范→一个写入根，具有生命周期
//
// 全部，而非部分：每个解析产生其出处的路径AND，或带类型的错误。每个调用者都不会出现空路径，
// 即 -interprets — 该模式就是 scope-model.md 调用
// 把偏函数伪装成全函数。
// 这就是 MCP "toggled the wrong file" 错误发生的方式。

#include <chrono>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace agentty::dirs {

namespace fs = std::filesystem;

// ── 根: 谁 源码树 ,因此,它接下来 ──────────────────────
// 两个值，而不是五个。 util/user_root.hpp 中的长注释
// 争论了为什么 agentty 有 一个 用户 root 而不是 XDG 的四个；
// 同样的推理也适用于这个 枚举。仅当它回答有关 内容 该 DATA FOLLOWS
// 的真正不同的问题时，根才存在于此处。
enum class Root {
    // ~/.agentty-遵循HUMAN。设置、
    // 凭据、线程、学习 内存、日志：无论您站在哪个结帐处，
    // 这些内容都应该相同。
    User,

    // <anchor>/.agentty —遵循 CODE。检索索引、反馈：
    // 远离产生它们的源码树而无意义的事物并且
    // 不能在不相关的结账之间共享。
    Project,
};

// ── 生命周期:故意宣布WITH为路径 ────────────────────────
// 该字段是#62的结构修复。将保留放在规范中意味着
// ADDING A STORE FORCES 该
// DECISION — 如果在声明站点上不说明是否有任何内容回收它，
// 则无法引入新目录。默认值 "forever" 是某人做出的选择，
// 而不是没人问的问题。
struct Lifecycle {
    // 是否可以将其删除而不丢失用户无法恢复的任何内容？
    //
    // 这是将我们与blobs::gc分开的轴线，并且它比看起来更重要。
    // blobs::gc 是 CAUTIOUS，
    // 因为错误会破坏对话历史记录，无论重新计算多少都无法恢复。
    // .ragdb 是可重建的，因此错误会导致 re
    // -index 的损失。相同的mark-and-sweep机制，
    // 相反的风险态势——共享的GC会犯这个错误，除非区别是一个领域
    // 而不是一个惯例。
    bool rebuildable = false;

    // 保留N个最新匹配的变体，扫除旧的。 0 ⇒ 保留全部。
    //
    // 存在是因为 rag 命名方案中存在真正的紧张：文件名
    // 中的 embedder-identity 标签是 CORRECT
    // （它阻止我们从不兼容的空间提供向量），但它使文件名集不受限制。
    // keep_last=1 意味着 A/B-ing 两个
    // 嵌入器不会在每次切换回来时强制完全重建，同时仍然限制增长。
    unsigned keep_last = 0;

    // 任何事物合格之前的宽限期。镜像 blobs::gc 的
    // 原因是：另一个 agentty 进程可能是 mid
    // -save，并且并发进程可能合法地运行不同的配置。
    std::chrono::seconds min_age{0};

    [[nodiscard]] constexpr bool sweeps() const noexcept {
        return rebuildable && keep_last > 0;
    }
};

// ── Spec: 特性宣布它自己的存储内容 ──────────────────
struct Spec {
    Root root = Root::User;

    // 已解析的根下的叶。子目录名称（"threads"、"rag"）；
    // dirs 只会加入它。
    //
    // EMPTY意味着"the root itself"，对于拥有自己的文件
    // 名并且只想要目录的调用者（rag附加rag_docs.
    // <embedder-tag>.ragdb）。分辨率为special-cases，
    // 因此结果没有尾随分隔符 - `root / ""` 命名相同的目录，
    // 但将 UNEQUAL 与 `root` 进行比较，这会默默地破坏任何
    // "did this path move?" 比较。
    std::string_view leaf;

    // 覆盖变量，AGENTTY_<NAME>_DIR。每个目录都有一个。
    std::string_view env = {};

    // 在解析的目录上强制0700。默认值是从根继承的，
    // 这对于默认路径来说足够了，对于可能指向用户已经
    // 创建的 0755 目录的覆盖来说，不 也足够了。
    bool owner_only = false;

    Lifecycle life{};

    // 一行关于事物在这里生存了多久，对于 `agentty config`。
    // 仅当没有任何东西扫过目录时才使用，从那时起生命周期就不能说了。
    std::string_view retention = {};
};

// ── 来源:来源,原因相同 ──────────
// 设置了变量但没有得到它的用户应该知道，并且存储面板
// 不能从裸路径诚实地报告"where are my bytes"。
// 将这一点保留在结果上使得 回退 案例
// 变得可调试而不是神秘。
enum class Origin {
    Default,            // <root>/<leaf>
    Override,           // $env 命名它，并且它起作用了
    OverrideFellBack,   // $env命名为它，它无法使用，我们警告+默认
};

struct Resolved {
    fs::path path;
    Origin   origin = Origin::Default;

    // 在加入 `leaf` 之前，根 `path` 位于其下方。进行此操作，
    // 以便调用者可以解析兄弟姐妹，而无需 re-deriving 根。
    fs::path root;

    [[nodiscard]] bool overridden() const noexcept {
        return origin != Origin::Default;
    }
};

// ── 错误 : 带类型的 ,因为 "empty path" 不是一个错误报告 ────────────
struct Error {
    enum class Kind {
        NoRoot,          // 用户 root 无法解析（无 HOME）或无项目锚点
        NotWritable,     // 锚点存在，但我们不能在其下书写
        Unusable,        // 均覆盖AND默认失败
    };
    Kind        kind = Kind::NoRoot;
    std::string detail;
};

[[nodiscard]] std::string_view to_string(Error::Kind) noexcept;

// ── 决议 ───────────────────────────────────────────────────────────

// 解析(并创建)`spec`的目录。
//
// 覆盖语义是从 user_root 的 resolve_subdir
// () 逐字继承的，因为它们已经是正确的：
//
//   -设置时$env获胜并且NON-EMPTY获胜。导出的-but-blank变量是
//     常见的壳事故与搬迁无关。
//   -RELATIVE覆盖针对根解析，而不是进程CWD。
//     `AGENTTY_LOGS_DIR=logs2`必须表示ONE目录，
//     而不是对于从其启动agentty的每个目录而言不同的目录。
//   -失败的覆盖WARNS ONCE并回退。默默地铺纸
//     它用默认的方式告诉用户错误的事情两次：设置没有采取，
//     并且什么也没说。
[[nodiscard]] std::expected<Resolved, Error> resolve(const Spec& spec);

// 解决而不创建任何东西，并且没有警告。
// 对于探测器和想要报告不打算写入的位置的 UI。
[[nodiscard]] std::expected<Resolved, Error> resolve_dry(const Spec& spec);

// ── 项目存储锚 ───────────────────────────────────────────
//
// 不 project_root()，差值为加载-bearing。
//
// project_root()被记录为
// 进程当前目录，但必须限制在访问边界内。
// 它不会走起来。这就是 RIGHT 的工作 - 相对工具路径必须跟踪用户站立的位置，
// 因此 `read src/foo.cpp` 落在启动的项目中，而不是 /src/foo.cpp.
//
// 对于存储来说这是错误的，因为它会移动。从子目录启动，你会得到一个 第二 索引：
// `cd src/ && agentty` 构建了自己的 82 MB <cwd>/.
// agentty，因为 current_path() 移动了并且没有任何东西锚定它。
//
// 因此，存储提出不同的问题并得到其自己的答案：保存项目标记的最近的封闭目录，
// 当没有项目标记时回退到project_root()。然后，
// 两者都受到范围的 usable_project_root()
// 保护 - "/" 是 agentty 的不受限制的 -access 哨兵，
// 并且永远不是分散状态的地方。
//
// 故意留下project_root()。将它们统一起来会重新引入
// 一个或另一个错误。
[[nodiscard]] fs::path project_anchor();

// 搜索的标记，nearest-first。顺序并不重要：
// 步行会在包含 任意 的第一个目录处停止。
[[nodiscard]] std::span<const std::string_view> project_markers() noexcept;

// ── 扫荡:生命周期,强制 ───────────────────────────────────────
//
// 回收名称带有身份标签的文件的被取代的VARIANTS。
// 激励案例是 rag 的 `rag_code.<embedder>.ragdb`：
// 标签是正确的（它阻止我们从不兼容的空间提供向量），
// 但它使文件名集不受限制，并且没有收集失败者 - 来自已
// 退休命名方案的 47 MB 孤儿仍然在磁盘上（#62）。
//
// 刻意以blobs::gc为原型，包括dry_run和min_age，
// 刻意与ONE轴不同：Lifecycle::rebuildable。
// blobs::gc 是谨慎的，因为删除 blob 会破坏会话历史记录，
// 而无法通过重新计算来恢复。一个 .ragdb 花费一个 re-index。
// 这种不对称性就是为什么 `sweeps()` 需要可重建的 和 keep_last —
// —无论 keep_last 说什么，不可重建的商店都会被 绝不 扫荡。
struct SweepStats {
    std::size_t examined = 0;
    std::size_t kept     = 0;
    std::size_t deleted  = 0;
    std::size_t too_new  = 0;   // 被min_age阻止
    std::size_t failed   = 0;   // >0 ⇒ 某些内容无法读取；见下文
    std::uintmax_t bytes_freed = 0;
};

// 在规范解析到的目录中要扫描的内容。
//
// `stem`+`suffix`将变体标签括起来：主干="rag_code."，
// 后缀=".ragdb"匹配`rag_code.<anything>.ragdb`。
// `keep` 命名了 变体，无论其年龄或数量如何，都必须存活下来 - 活的。
//
// Sidecars 以其原理被横扫：删除 `x.ragdb` 并
// 留下 `x.ragdb.meta.json` 是你如何获得描述
// 不存在的文件的元的方式，然后每个加载器都必须防御该元。
struct SweepRequest {
    Spec             spec;
    std::string_view stem;
    std::string_view suffix;
    std::span<const std::string>  keep;      // 实时文件名，从未删除
    std::span<const std::string_view> sidecar_suffixes;  // e.g。 ".meta.json"

    // 还删除UNTAGGED `<stem-without-dot><suffix>`
    // (e.g.没有嵌入器标签的旧版`rag_code.ragdb`)。默认关闭，
    // 因为 "a name I do not recognise" 通常是保留文件
    // 而不是删除它的原因。
    bool include_untagged = false;
};

// 扫一扫。除非 spec.life.sweeps()，否则 no-op 返回 {}。
//
// SAFETY：如果无法检查任何目录条目，则不删除任何内容，
// 并且`failed`不是-zero。与 blobs::gc 相同的规则 - 对
// 当前内容的错误肯定会消耗磁盘，而不是数据。
[[nodiscard]] SweepStats sweep(const SweepRequest& req, bool dry_run = false);

}  // namespace agentty::dirs
