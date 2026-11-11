#include "agentty/tool/util/fs_helpers.hpp"
#include "agentty/tool/util/disk_files.hpp"
#include "agentty/util/home_dir.hpp"

#include <maya/runtime.hpp>
#include <mcp/tools/util/fs_helpers.hpp>   // project_root( ): 两层都有一个规则

#include <algorithm>
#include <fstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace agentty::tools::util {


fs::path normalize_path(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))  s.remove_suffix(1);
    if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"')
                          || (s.front() == '\'' && s.back() == '\''))) {
        s.remove_prefix(1);
        s.remove_suffix(1);
    }
    // 斜拉伸。模型会写出一个贝壳的路径( )
    // `~/projects/foo` )没有扩张,
    // 它们就圆 -trip 输入 `<cwd>/~/projects/foo` ,
    // 该工具不存在,工具要么返回 NotFound 或者,
    // 在慢慢处理奇异前缀的文件系统中,坐着 stat() 足够打到 看门狗.
    // 壳体公约:赤裸 `~` 或 或 时 间 `~/...` 扩展至 $HOME.
    // (`~user/...` 百分比 -user 扩建在这里不支持;
    // 我们需要 getpwnam_r 模型从未见过它的工作。)
    std::string expanded;
    if (!s.empty() && s.front() == '~' && (s.size() == 1 || s[1] == '/')) {
        // Home 根通过统一解析器( Q) $HOME 打开 POSIX/MSYS2,
        // $USERPROFILE 本地语言 Windows ——赤裸的genenv( )
        // () "HOME" 左侧 `~/…` 未扩展于本地 Windows (HOME ,
        // 将参数转换为 `<project_root>/~/…` 工具失败 NotFound.
        const fs::path home = agentty::util::home_dir();
        if (!home.empty()) {
            expanded = home.string();
            expanded.append(s.data() + 1, s.size() - 1);
            s = expanded;
        }
    }
    fs::path p{s};
    if (!p.is_absolute()) {
        // 相对于 活动 PROJECT (在出入边界内被夹住),
        // 不 工具 线程 穿过了 fs::absolute (a) 与 不
        // 原始边界。 fs::absolute (锁定在任何cwd的
        // 工作线程 线程 碰巧有,这可能会失败 工作区-containment
        // 检查该模型的合理预期将进入的路径 -bounds ;
        // 原始边界将转动 `src/foo.cpp` 输入
        // `/src/foo.cpp` 下级 `--workspace /`.
        // project_root()是右锚,
        // 默认等于如此常见的发射的边界不变.
        p = project_root() / p;
    }
    return p.lexically_normal();
}

namespace {

// 函数 -local 静态,所以默认值最初是 cwd 调用 ,
// 无论静态 -initialisation 命令,不测试 调用
// set_workspace_root () 仍然有合理的违约情况。
fs::path& mutable_workspace_root() {
    static fs::path root = [] {
        std::error_code ec;
        auto cwd = fs::current_path(ec);
        return ec ? fs::path{"/"} : cwd;
    }();
    return root;
}

} // 命名空间结束。

void set_workspace_root(Io, fs::path root) {
    std::error_code ec;
    // 打开 Windows, weakly_canonical 无法打开 junctions/reparse 点。
    // 如果条形文字化失败, 请使用路径作为 -is
    // 这比拒绝有效更优雅 工作区 根部, 根部 ,
    // 根 , 顶部。
    auto canon = fs::weakly_canonical(root, ec);
    auto resolved = ec ? std::move(root) : std::move(canon);
    // 保证绝对性;在Windows上,一些不良的犬科化
    // 可能会在错误情况下返回相对路径.
    if (!resolved.is_absolute()) {
        std::error_code aec;
        auto abs = fs::absolute(resolved, aec);
        if (!aec) resolved = std::move(abs);
    }
    // 电话 mcp-cpp 工具读取此根: 工具状态
    // agentty 手握着边界 调用.
    mutable_workspace_root() = std::move(resolved);
}

const fs::path& workspace_root() {
    return mutable_workspace_root();
}

fs::path project_root() {
    // mcp-cpp 统治我们的边界,所以...
    // agentty-native 和 mcp 数据 -served 工具
    // 解决与同一目录的相对路径。
    static DiskFiles disk;   // 无国籍:一个就够了
    return ::mcp::tools::util::project_root(
        ::mcp::tools::util::bounds_from(disk, workspace_root(), {}));
}

bool is_within_workspace(const fs::path& target) {
    if (target.empty()) return false;
    std::error_code ec;
    // weakly_canonical 解决存在的问题,
    // 剩下的就当作 -is ——在文件尚不存在但父目录存在的情况下,
    // 适合写入目标,它解决了共鸣链接,从而在其中 -workspace
    // 链接 指向/etc 失败前缀检查。
    // 打开 Windows ,此选项可能会失败 junctions/reparse
    // 积分 如果失败 重试 父证书( 普通写) -target 大小写: 文件不存在,
    // 但目录确实存在。 如果失败, 请尝试父目录 -relative
    // 将这一方法作为最后手段。
    auto canon_target = fs::weakly_canonical(target, ec);
    if (ec) {
        // weakly_canonical 失败( E) e.g。母组件是一个连接的连锁,
        // 或者 EACCES 或走链,或 Windows ),"重分点".
        // 不执行 回到平原上 回到平地上 absolute()
        // ——跳过共鸣解析度,让一个精心设计的 链接 逃离边界
        // 重试 父目录( 常见的写) -target 例:文件尚未存在,
        // 但目录确实存在,而且其分类仍然能够解决共解链接,
        // 然后是 -attach 名称。
        std::error_code pec;
        auto parent = target.parent_path();
        if (!parent.empty()) {
            auto canon_parent = fs::weakly_canonical(parent, pec);
            if (!pec && !canon_parent.empty()) {
                canon_target = canon_parent / target.filename();
                ec.clear();
            }
        }
        // 如果连父母都这样 重试 失败, 倒车 lexically_normal
        // 绝对路径(没有符号链接分辨率,但至少一致)。
        if (ec) {
            std::error_code aec;
            auto abs = fs::absolute(target, aec);
            if (!aec) {
                canon_target = abs.lexically_normal();
                ec.clear();
            } else {
                return false;   // 连绝对的都得不到;否认
            }
        }
    }
    auto canon_root = workspace_root();   // 已装入套装
    // 构成部分 -wise 前缀检查。普通字符串 startsWith 将允许/
    // home/user/project-other 根为/ 时通过 home/user/project.
    auto rt = canon_root.begin();
    auto tt = canon_target.begin();
    for (; rt != canon_root.end() && tt != canon_target.end(); ++rt, ++tt) {
        if (*rt != *tt) return false;
    }
    return rt == canon_root.end();
}

// ── 读取 -only 允许清单 根根根根头 ───────────────────────────────────────────
namespace {

// ⁇   ⁇  READ 门也接受( ⁇
// ~/.agentty/skills 等下的技能目录)  ⁇  读取
// 来源于工具 线程 , 技能扫描仪的注册. A 快照: 一小根,
// 每门检查时读取,很少附图——没有 ⁇  锁,所以 ⁇  门不需要能力.
using Roots = std::vector<std::string>;
[[nodiscard]] maya::snapshot<Roots>& read_roots() {
    static maya::snapshot<Roots> r;
    return r;
}

// 构成部分 -wise "target under root" - 和
// 那辆一样的形状 工作区 前缀检查上方; 已计及, 使两个门共享共线
// -safe 犬科化规则。
[[nodiscard]] bool under_root(const fs::path& canon_target, const fs::path& root) {
    auto rt = root.begin();
    auto tt = canon_target.begin();
    for (; rt != root.end() && tt != canon_target.end(); ++rt, ++tt)
        if (*rt != *tt) return false;
    return rt == root.end();
}

} // 命名空间结束。

void allow_read_root(Io, const fs::path& root) {
    std::error_code ec;
    auto canon = fs::weakly_canonical(root, ec);
    // 打开 Windows, weakly_canonical 可能发生交叉点故障。
    // 如果失败,请倒入 lexically_normal (绝对( root
    // ) )。 这提供了稳定 缓存 键,即使我们无法解决共鸣。
    if (ec) {
        std::error_code aec;
        auto abs = fs::absolute(root, aec);
        if (aec) return;   // 连绝对的都得不到 保释
        canon = abs.lexically_normal();
    }
    if (canon.empty()) return;
    read_roots().update(
        [](const Roots& roots, const std::string& c) {
            if (std::find(roots.begin(), roots.end(), c) != roots.end()) return roots;
            Roots next = roots;
            next.push_back(c);
            return next;
        },
        canon.string());
}

std::vector<fs::path> read_roots_now() {
    const auto roots = read_roots().load();
    std::vector<fs::path> out;
    out.reserve(roots->size());
    for (const auto& r : *roots) out.emplace_back(r);
    return out;
}

void allow_fork_transcripts(Io io, const fs::path& threads_dir,
                            const std::vector<std::string>& paths) {
    std::error_code ec;
    const auto dir = fs::weakly_canonical(threads_dir, ec);
    if (ec || dir.empty()) return;
    constexpr std::string_view kSuffix = ".transcript.md";
    for (const auto& raw : paths) {
        if (raw.empty()) continue;
        std::error_code fec;
        const auto file = fs::weakly_canonical(fs::path{raw}, fec);
        if (fec || !fs::is_regular_file(file, fec)) continue;
        // 下方的 线程 dir 中一个级别, 并且只有记录。
        // 编辑后可以指向其它任何地方的注释不会打开任何内容。
        if (file.parent_path() != dir) continue;
        const auto name = file.filename().string();
        if (name.size() <= kSuffix.size()
            || name.compare(name.size() - kSuffix.size(), kSuffix.size(), kSuffix) != 0)
            continue;
        allow_read_root(io, file);
    }
}

bool is_read_allowlisted(const fs::path& target) {
    if (target.empty()) return false;
    std::error_code ec;
    auto canon = fs::weakly_canonical(target, ec);
    if (ec) {
        // weakly_canonical 失败( 坏路口等)。
        // 退到 lexically_normal(absolute()。读取需要文件在比较时存在,
        // 所以没有父目录 重试 喜欢 is_within_workspace.
        std::error_code aec;
        auto abs = fs::absolute(target, aec);
        if (aec) return false;   // 不能说教;否认
        canon = abs.lexically_normal();
    }
    const auto roots = read_roots().load();
    for (const auto& r : *roots)
        if (under_root(canon, fs::path{r})) return true;
    return false;
}

std::expected<NormalizedPath, ToolError>
make_workspace_path(std::string_view raw, std::string_view tool_name) {
    NormalizedPath p{raw};
    if (!is_within_workspace(p.path())) {
        // 有用且可操作的信息: 指定违法路径、
        // 活动根和两条出路( 以更宽的目录重开或通过
        // --workspace )。 该模型可以读取,
        // 也可以请用户扩大范围或选择不同的路径。
        return std::unexpected(ToolError::out_of_workspace(
            "tool '" + std::string{tool_name} + "' refused: '"
            + p.string() + "' is outside the workspace root '"
            + workspace_root().string() + "'. "
            "Restart agentty in a parent directory or pass "
            "--workspace <dir> to widen the scope."));
    }
    return p;
}

// ── WorkspacePath 工厂 ───────────────────────────────────────
std::expected<WorkspacePath, ToolError>
make_workspace_path_checked(std::string_view raw, std::string_view tool_name) {
    auto np = make_workspace_path(raw, tool_name);
    if (!np) return std::unexpected(std::move(np.error()));
    return WorkspacePath{std::move(*np)};
}

std::expected<WorkspacePath, ToolError>
promote_to_workspace_path(NormalizedPath p, std::string_view tool_name) {
    if (!is_within_workspace(p.path())) {
        return std::unexpected(ToolError::out_of_workspace(
            "tool '" + std::string{tool_name} + "' refused: '"
            + p.string() + "' is outside the workspace root '"
            + workspace_root().string() + "'."));
    }
    return WorkspacePath{std::move(p)};
}

std::expected<WorkspacePath, ToolError>
make_readable_path_checked(std::string_view raw, std::string_view tool_name) {
    NormalizedPath p{raw};
    if (is_within_workspace(p.path()) || is_read_allowlisted(p.path()))
        return WorkspacePath{std::move(p)};
    return std::unexpected(ToolError::out_of_workspace(
        "tool '" + std::string{tool_name} + "' refused: '"
        + p.string() + "' is outside the workspace root '"
        + workspace_root().string() + "' and not under any skill "
        "directory. Restart agentty in a parent directory or pass "
        "--workspace <dir> to widen the scope."));
}

// ── 已检查 read/write 代表 ────────────────────────────────
std::string read_file(const WorkspacePath& p) {
    return read_file(p.path());
}

std::string write_file(const WorkspacePath& p, std::string_view content) {
    return write_file(p.path(), content);
}

bool should_skip_dir(std::string_view name) noexcept {
    static const std::vector<std::string_view> skip = {
        ".git", "node_modules", "build", "target", "__pycache__",
        ".cache", "vendor", "dist", "out", ".next", ".venv",
        "cmake-build-debug", "cmake-build-release", ".idea", ".vscode",
        "_deps", "third_party", "thirdparty", "3rdparty", "external",
        ".mypy_cache", ".pytest_cache", ".tox", ".gradle", "Pods",
        "bazel-bin", "bazel-out", "DerivedData", ".terraform",
    };
    for (auto s : skip) if (name == s) return true;
    // 前缀家庭: 退出 -of-source CMake 构建 源码树 (build/ ,
    // 构建 - *, 制作 -build- *)充满了生成的+获取的文件(_deps),
    // 否则会淹没@ -file 选择面板 / 符号 索引。同步到 mcp-cpp's
    // should_skip_dir.
    if (name.starts_with("build-") || name.starts_with("cmake-build"))
        return true;
    return false;
}

bool is_binary_file(const fs::path& p) {
    std::ifstream ifs(p, std::ios::binary);
    // 无法打开( C) 权限 拒绝, 短暂 锁, 竞态-deleted):
    // 不 a 二进制程序 裁决。 `true` 来了 grep/glob
    // 默默跳过文件并制作 read/edit 报告假冒
    // "cannot read binary file" 返回时 `false` 所以,
    // 打电话者真正的读数运行,并显示实际的打开错误而不是掩盖它。
    if (!ifs) return false;
    char buf[512];
    ifs.read(buf, sizeof(buf));
    auto n = ifs.gcount();
    for (int i = 0; i < n; ++i)
        if (buf[i] == '\0') return true;
    return false;
}

// ── 快照 缓存 档案 ────────────────────────────────────────────────────
namespace {

// 电话 快照 每一个工具最后看到, 通过犬类路径。 FileSnapshot
// 持有 file_time_type ,哪个 jaal 里面看不到,所以我们...
// 缓存 保留勾选符并重建 快照 离开的路上。
struct SeenFile {
    // 钟表是自己的勾子类型,不是 int64_t : libstdc++ 使用一个 64 -bit 代表,
    // libc++( 打开clang ) macOS ) 使用量 __int128 - 缩小范围
    // int64_t 在一个支架上 -init 是一个很难的错误。
    fs::file_time_type::rep mtime = 0;
    std::uintmax_t size = 0;
    std::uint64_t  content_hash = 0;
};
// A 快照 : 代理触摸到的每份文件一个条目( 最多数百个),
// 每文件写一次 read/write 工具 调用 由坚固的门阅读,
// 收到 -on-write 这么便宜,留着这些 noexcept
// 帮助者 锁-free ,因此,没有能力到达工具壳 调用 他们。
using SeenMap = std::unordered_map<std::string, SeenFile>;
[[nodiscard]] maya::snapshot<SeenMap>& file_cache() {
    static maya::snapshot<SeenMap> c;
    return c;
}

// 定义不需要文件存在( 写入目标在我们记录的那一刻可能还不存在)。
// 返回逻辑上的绝对形式, 当 weakly_canonical 出错
// ——我们每个文件仍然有稳定的密钥 调用 ,只是没有韵律 -aware
// 打开 Windows, weakly_canonical 无法打开
// junctions/reparse 点;
// 贬低 lexically_normal(absolute() )比空归或坠毁更安全.
[[nodiscard]] std::string canon_key(const fs::path& p) noexcept {
    std::error_code ec;
    auto canon = fs::weakly_canonical(p, ec);
    if (!ec) return canon.string();
    // 回退1:如果p已经绝对,则使用其逻辑上的正常形式
    if (p.is_absolute()) return p.lexically_normal().string();
    // 回退2: 使它绝对,然后逻辑上正常
    auto abs = fs::absolute(p, ec);
    if (!ec) return abs.lexically_normal().string();
    // 回退 3: 返回为 -is (不应该发生,但防御)
    return p.string();
}

} // 命名空间结束。

void record_file_seen(const fs::path& path,
                      fs::file_time_type mtime,
                      std::uintmax_t size,
                      std::uint64_t content_hash) noexcept {
    auto key = canon_key(path);
    if (key.empty()) return;
    try {
        file_cache().update(
            [](const SeenMap& m, const std::string& k, const SeenFile& v) {
                SeenMap next = m; next[k] = v; return next;
            },
            std::move(key), SeenFile{mtime.time_since_epoch().count(), size, content_hash});
    } catch (...) { /* 最好,你给我看看 -effort 簿记; noexcept 合同 */ }
}

std::optional<FileSnapshot> last_seen_file(const fs::path& path) noexcept {
    auto key = canon_key(path);
    if (key.empty()) return std::nullopt;
    const auto m = file_cache().load();
    auto it = m->find(key);
    if (it == m->end()) return std::nullopt;
    const SeenFile& seen = it->second;
    return FileSnapshot{fs::file_time_type{fs::file_time_type::duration{seen.mtime}},
                        seen.size, seen.content_hash};
}

StaleVerdict staleness_of(const fs::path& path) noexcept {
    auto snap = last_seen_file(path);
    if (!snap) return StaleVerdict::Unknown;
    std::error_code ec;
    // 打开 Windows, last_write_time/file_size
    // 无法打开 junctions/reparse 点。如果我们不能按下当前文件,
    // 则无法比较;返回未知(按可能处理) 过期 安全地说。
    auto cur_mtime = fs::last_write_time(path, ec);
    if (ec) return StaleVerdict::Unknown;
    auto cur_size  = fs::file_size(path, ec);
    if (ec) cur_size = 0;   // 默认为 0 出错; 仍与 snap 比较
    // mtime 颗粒性是文件系统 -dependent (HFS + 1 s,ext4 ns, +1 s, +4 ns , + 1 , → + + 1 x ,
    // = + = = o = 3 ; + o + → = 4 ; = → o ; → → 3 = ° = 5 ; o → ° + ; ° o ° ;
    // / = Q = ; Q + ° → ; ; − + 大于 = 大于 + 3 +。 FAT 严格比较:任何差异——前进 OR 后向 —
    // 计为 过期。大小不匹配也是一种 过期 即使在mtime没有改变时(有些工具触摸) -without-updating-mtime).
    if (snap->mtime == cur_mtime && snap->size == cur_size)
        return StaleVerdict::Fresh;
    return StaleVerdict::Stale;
}

} // namespace agentty::tools::util
