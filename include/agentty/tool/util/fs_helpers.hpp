#pragma once
// 共享文件系统助手。 Tool 实现需要标准化路径、二进制程序 检测和可预测的
// 遍历时应该跳过哪些目录。
// 列表 - 集中在此处，以便规则在 grep / glob / list_dir
// / find_definition 中保持一致。
//
// ── 对应的 `util` IS 此? ──────────────────────────────────────────
// 有两个，尽管名称冲突，但分裂是故意的：
//
//   agentty::tools::util(
//       include/agentty/tool/util/，
//       src/tool/util/)TOOLBOUNDARY。
//       这里所有的东西都存在，因为 MODEL 要求了一些东西，
//       我们必须小心它：工作区 钳位、路径规范化、子进程生成、沙箱、信任切换门、
//       参数读取、partial-JSON 挽救。统一属性是 UNTRUSTED
//       INPUT — 这些是模型请求和机器之间的墙壁和 解析器。
//
//   agentty::util(include/agentty/util/，
//       src/util/)APP-WIDEPLUMBING，没有工具调用的概念：记录
//       (logx)、home/root分辨率、base64、拆卸排序、self-updater、
//       models.dev缓存。可从任何地方调用，包括从 tools::util
//       本身（fs_helpers 使用 util::home_dir），
//       这就是依赖项仅以一种方式运行的原因：tools::util 可以使用 util，
//       而不是相反。
//
// 添加某物时的经验法则：仅因为模型位于其另一端才有意义吗？
// 那么它就属于这里。否则就是agentty::
// util。

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "agentty/tool/registry.hpp"   // ToolError + 工厂
#include "agentty/util/io.hpp"

namespace agentty::tools::util {

namespace fs = std::filesystem;

// 前向声明 — WorkspacePath 位于 ToolError-using 工厂下方。
class WorkspacePath;

// 将整个文件读取为 二进制程序 blob。打开失败时返回
// ""（需要区分缺失与空的调用者应首先进行统计）。
//
// 两个重载：`WorkspacePath`形式是model
// -facing工具应该使用的工作区-checked条目；
// `fs::path` 形式是 运行时 拥有的路径的未经检查的
// 逃生舱口（凭据、线程 持久化、内存 存储在 ~/.agentty/ 下）。
// 通过 带类型的 参数进行路由使边界在 调用 站点上保持
// 可见 - 审阅者可以一目了然地看到所选工具的形式。
[[nodiscard]] std::string read_file(const fs::path& p);
[[nodiscard]] std::string read_file(const WorkspacePath& p);

// Write内容原子地-ish(截断+写入+刷新)。
// 成功时返回空字符串，否则返回 human-readable 错误。
// 保持工具 lambda 简洁，同时仍然迫使调用者暴露故障。
//
// 关于 WorkspacePath 与 fs::path 过载划分，请参阅上面的 `read_file`。
[[nodiscard]] std::string write_file(const fs::path& p, std::string_view content);
[[nodiscard]] std::string write_file(const WorkspacePath& p, std::string_view content);

// 规范化user-supplied路径。
// 接受 Windows 上的正斜杠（模型经常生成它们），
// 去除周围的空格和引号，并在还不是绝对路径时返回相对于 cwd
// 的绝对路径 — 因此错误消息会命名一个明确的位置。
[[nodiscard]] fs::path normalize_path(std::string_view s);

// 针对已经-normalised文件系统路径的强类型定义。构造一个的唯一方法是通过
// `NormalizedPath{"..."}` 从原始字符串，其中 调用
// `normalize_path` — 因此 "did I already normalize this?"
// 由类型回答。按值传递（便宜：保存单个 fs::path）。
struct NormalizedPath {
    fs::path value;

    explicit NormalizedPath(std::string_view raw) : value(normalize_path(raw)) {}

    [[nodiscard]] const fs::path& path() const noexcept { return value; }
    [[nodiscard]] std::string string() const { return value.string(); }
    [[nodiscard]] bool empty() const noexcept { return value.empty(); }
};

// ── 工作区 边界 ──────────────────────────────────────────────────
// 每个文件系统-touching工具拒绝该根之外的路径。
// 启动时设置一次（来自cwd的main.cpp，或来自--workspace
// CLI 标志）；从工具实现中自由查询。
// 调用 `set_workspace_root` 之前的默认值是进程的 cwd 首先是
// 调用 — 对于测试和独立帮助程序使用是安全的。
//
// 边界是最简单的沙箱层：它不会阻止模型运行走到任何地方的shell命令，
// 但它确实阻止"model casually `read`s ~/.ssh/id_rsa or `write`s to /etc/hosts"
// 的快速路径。与 bash gating + 未来的 OS
// -native 沙箱 (沙箱-exec / bwrap /
// firejail) 配对，用于防御-in-depth 故事。
void set_workspace_root(Io, fs::path root);

[[nodiscard]] const fs::path& workspace_root();

// 活动 PROJECT目录：进程cwd(目录agentty被启动)，
// 被夹在访问边界内。与 workspace_root() 不同，
// 可扩展的 ACCESS BOUNDARY (`--workspace /`)。
// 相对工具路径和仓库-scoped默认从HERE解析，使得`--workspace /`下
// 的`read src/foo.cpp`落在启动的项目中，而不是在`/src/foo.cpp`处。
// 默认情况下 project_root() == workspace_root()。
// 镜像mcp-cpp的util::project_root()；两者通过共享的
// set_workspace_root() 管道保持同步。
[[nodiscard]] fs::path project_root();

// 如果 `target` 位于规范化两侧之后的 工作区 根的 -or-under 处，
// 则为真。符号链接转义被阻止：工作区 内指向 /etc 的 链接
// 将解析为 /etc 并导致前缀检查失败。
// 使用 weakly_canonical，因此仍会根据其现有父组件正确
// 检查 not-yet-existing 路径（e.g.写入目标）。
[[nodiscard]] bool is_within_workspace(const fs::path& target);

// 一次性构造出 工作区-checked 的 NormalizedPath。
// 工具调用:
//     auto p = util::make_workspace_path(*raw, "read");
//     if (!p) return std::unexpected(p.error());
// `tool_name`仅出现在错误消息中并且纯粹是装饰性的。
[[nodiscard]] std::expected<struct NormalizedPath, ToolError>
make_workspace_path(std::string_view raw, std::string_view tool_name);

// ── WorkspacePath ───────────────────────────────────────────
// 携带工作区遏制的*type-level*证明的NormalizedPath。
// 获取它的唯一公开方法是通过
// `WorkspacePath::checked`（或下面的门控构
// 建器工厂），它委托给 is_within_workspace()。
// 一旦函数接受 `const WorkspacePath&`，
// 审阅者就知道遏制检查已经发生 - 忘记这是 编译 错误，
// 而不是丢失 运行时 门。
//
// 如今的工具已经在任何IO之前将每个fs路径路由通过make_workspace_path；
// 这种类型使该规则成为类型系统的属性，而不是 code-review 约定。
// 新的 fs API 应接受 `WorkspacePath`；只有 运行时 自己的非
// -workspace 路径（在 threads/memory/credentials
// 的 ~/.agentty/ 下）通过 read_file/write_file.
// 的 `fs::path` 重载绕过它
class WorkspacePath {
    NormalizedPath inner_;
    // 私人；只有厂友才能铸造一枚。没有公共 ctor
    // = 无法跳过收容检查。
    explicit WorkspacePath(NormalizedPath n) noexcept : inner_(std::move(n)) {}

    friend std::expected<WorkspacePath, ToolError>
        make_workspace_path_checked(std::string_view raw,
                                    std::string_view tool_name);
    friend std::expected<WorkspacePath, ToolError>
        promote_to_workspace_path(NormalizedPath p,
                                  std::string_view tool_name);
    friend std::expected<WorkspacePath, ToolError>
        make_readable_path_checked(std::string_view raw,
                                   std::string_view tool_name);

public:
    [[nodiscard]] const fs::path&   path()   const noexcept { return inner_.path(); }
    [[nodiscard]] std::string       string() const          { return inner_.string(); }
    [[nodiscard]] bool              empty()  const noexcept { return inner_.empty(); }
    [[nodiscard]] const NormalizedPath& normalized() const noexcept { return inner_; }
};

// 工作区-checked工厂。与 make_workspace_path 相同的合约，
// 但产生 WorkspacePath 而不是 NormalizedPath
// ——在新代码中使用它，这样门的成功就会伴随着价值。
[[nodiscard]] std::expected<WorkspacePath, ToolError>
make_workspace_path_checked(std::string_view raw, std::string_view tool_name);

// ── 读取 -only 允许清单 根根根根头 ───────────────────────────────────────────
// 技能目录(agentskills.io三级资源)可能存在OUTSIDE、
// 工作区(~/.agentty/skills/、~/.agents/skills/)。
// 该规范的 client-implementation 指南：用于读取的 允许清单 技能目录，
// 以便模型可以获取捆绑的 scripts/references 而不会被边界拒绝。
// 允许清单 的结构是 READ-ONLY — 只有
// `make_readable_path_checked` 会查阅它； write/edit
// 门（make_workspace_path_checked）永远不会这样做，
// 因此 列入允许清单 根不能成为写转义。
//
// 由技能扫描器在发现时注册。 幂等；
// 受技能上限限制（每个范围≤ 64×2 根）。
void allow_read_root(Io, const fs::path& root);

// 当 `target` 位于注册的 read-allowlist 根下时为真（
// post-canonicalisation，符号链接-escape 像 工作区 一样检查）。
[[nodiscard]] bool is_read_allowlisted(const fs::path& target);

// 到目前为止注册的读取根（规范的）。
[[nodiscard]] std::vector<fs::path> read_roots_now();

// 分叉的线程指向工作区之外的线程目录下的其父代的转录本。
// 使这些文件准确可读（而不是目录），以便在稍后的会话中重新打开时，
// fork 仍然可以工作。仅直接存在于 `threads_dir`
// 中且以 .transcript.md 结尾的文件。
void allow_fork_transcripts(Io, const fs::path& threads_dir,
                            const std::vector<std::string>& paths);

// Read-gate工厂：当路径位于read-allowlist根下的工作区
// OR内时通过。在 read-side 工具 (`read`) 中使用 仅。
[[nodiscard]] std::expected<WorkspacePath, ToolError>
make_readable_path_checked(std::string_view raw, std::string_view tool_name);

// 提升已经-normalised的路径通过收容门。
// 当调用者自己编写 NormalizedPath (e.g.XX) 时很有用。
// 解析针对workspace_root())的附件路径并且
// 现在想要带类型的证明。
[[nodiscard]] std::expected<WorkspacePath, ToolError>
promote_to_workspace_path(NormalizedPath p, std::string_view tool_name);

// 对于目录名来说，我们希望默认情况下跳过递归遍历（
// grep/glob/list_dir）。将跳过列表保留在一处，
// 以便工具保持同步（e.g。立即将 `_deps` 添加到每个工具）。
[[nodiscard]] bool should_skip_dir(std::string_view name) noexcept;

// 启发式：扫描前512个字节以查找NUL。足以避免将
// PNG/可执行文件/模型权重放入 提示词 中。
[[nodiscard]] bool is_binary_file(const fs::path& p);

// ── 人数 -file 状态 缓存 ────────────────────────────────────────────────
// 跨工具共享，因此edit/write可以检测
// 模型上次查看后，文件已经发生变化。
// 以规范路径为关键；值是工具看到的 mtime 加上 content-fingerprint 哈希值，
// 因此我们捕获不会影响 mtime 的 sub-second 编辑（某些文件系统具有 1s mtime 分辨率）。
//
// 记录在每次成功读取后由 `read` 写入，并在成功突变后由
// `write`/`edit` 写入（因此来自同一会话的下一个
// 调用 看到新状态并且不会因自身变化而 false-alarm）。
// 查找永远不会在 IO 上阻塞 - 缓存 纯粹是 in
// -memory 提示表面。
//
// 缓存是进程-wide并且持续存在于代理的生命周期。
// 持续编辑相同文件的 long-running 会话只需为每个
// 文件每次更改支付一次统计成本——每个后续的过时性检查都是
// hash-table 命中。
struct FileSnapshot {
    fs::file_time_type mtime{};
    std::uintmax_t     size = 0;
    std::uint64_t      content_hash = 0;   // FNV-1a 字节工具锯
};

// 记录利用给定的内容散列在`mtime`/`size`观察到`path`。
// `path` 在内部进行了规范化。当呼叫者手边没有 字节 时，
// 将 `0` 传递给 `content_hash` (e.g.
// stat-only路径）；然后，陈旧性检查降级为仅 mtime+size。
void record_file_seen(const fs::path& path,
                      fs::file_time_type mtime,
                      std::uintmax_t size,
                      std::uint64_t content_hash) noexcept;

// 查找工具最后看到的 快照 的 `path`。当没有工具在本次
// 会话中触及该文件时，返回 nullopt。 `path` 已标准化。
[[nodiscard]] std::optional<FileSnapshot> last_seen_file(const fs::path& path) noexcept;

// 计算 字节 范围内的 FNV-1a 64-bit。
// 可内联；已读取文件的工具使用它在 快照 中记录其哈希值。
// FNV-1a被选择而不是xxHash，因为它是分支-free、
// zero-alloc，并且没有依赖性——在一个会话的文件价值
// 中的64位的冲突风险可以忽略不计。
[[nodiscard]] inline std::uint64_t content_fnv1a(std::string_view bytes) noexcept {
    constexpr std::uint64_t kOffset = 0xcbf29ce484222325ULL;
    constexpr std::uint64_t kPrime  = 0x00000100000001b3ULL;
    std::uint64_t h = kOffset;
    for (unsigned char c : bytes) {
        h ^= static_cast<std::uint64_t>(c);
        h *= kPrime;
    }
    return h;
}

// 陈旧性分类。通过对照缓存的 快照 检查文件的
// 当前（mtime、大小）和可选的内容哈希来计算。
enum class StaleVerdict : std::uint8_t {
    Unknown,      // 没有先前的快照——呼叫者决定做什么
    Fresh,        // 快照匹配当前on-disk状态
    Stale,        // 自记录 快照 以来文件已更改
};

// 检查`path`相对于其最后的快照是否看起来过期。
// Stat-based (廉价)：比较mtime+大小。当不存在 快照
// 或 stat 失败时，返回 Unknown。为了获得更强的保证，
// 调用者还可以对文件的当前 字节 进行哈希处理，并与 快照 的
// `content_hash` 进行比较。
[[nodiscard]] StaleVerdict staleness_of(const fs::path& path) noexcept;

} // namespace agentty::tools::util
