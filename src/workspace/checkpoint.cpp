// SPDX-License-Identifier: Apache-2.0
//
// checkpoint.cpp - 盖特 -based 工作区 检查
// 站(见 头文件 设计镜像 Zed 的代理检查站:无父母承诺的物体被钉在
// 下面 refs/agentty/checkpoints/<id> ,
// 针对 THROWAWAY 索引文件,所以用户的真实索引, HEAD ,
// 隐藏和反转记录从未被扰动。承诺对普通的 ⁇ 瓷(没有分枝点)是看不见的,
// 并且会生存到被我们自己的保存所磨灭为止。 -count
// 政策或一个 `git gc` 可在删除参考后删除。

#include "agentty/workspace/checkpoint.hpp"
#include "agentty/util/digits.hpp"

#include "agentty/tool/util/fs_helpers.hpp"
#include "agentty/tool/util/subprocess.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <atomic>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>   // GetCurrentProcessId
#else
#  include <unistd.h>    // 发作
#endif

namespace agentty::workspace {

namespace fs = std::filesystem;
namespace util = agentty::tools::util;

namespace {

// 最多保留这么多的检查站裁判;最古老的超过上限
// 的是在创建时间被放弃. 64个用户历史转折是充足
// 的——恢复目标几乎总是最后的几圈转折,
// 每个裁判为后来改变的每个文件都打出整圈的针.
constexpr int kKeepCheckpoints = 64;

constexpr std::string_view kRefPrefix = "refs/agentty/checkpoints/";

struct RepoInfo {
    bool        in_repo = false;
    std::string root;      // 工作树顶端-level(绝对)
    std::string git_dir;   // 绝对数.git dir( 工作树-aware)
};

// 沿新线线的引线 调用 单叶 -line 输出。
std::string chomp(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

// 发现附件 仓库 一次 每个进程。 仓库-ness 和布局不改变中点
// -session ; 缓存每个检查点避免两个子进程产物。
// (用户运行) `git init` 中点 -session 重新启动
// 时捡起它——可接受。 )
//
// 我们从这里探险 PROCESS CWD ,
// 没有 util::workspace_root () 有关 工作区
// root 是文件系统 * 访问 * 边界, 并例行拓宽( Q) `-w /`
// 全部用于 -disk ——但, `-w /` 千万不要让检查层认为工程
// 是 `/` (这从来就不是) 仓库 )cwd是用户启动的目录。
// agentty 调自 -- -- i.e . 其项目——和
// agentty 永远不要离它而去 所以 `git -C <cwd> rev-parse`
// 走到真正的包围 仓库 不管我们有多宽 沙箱 门是。
// 如果cwd不知何故无法读取,则会倒回到工作区根.
const RepoInfo& repo() {
    static const RepoInfo info = [] {
        RepoInfo r;
        // 活动 PROJECT(在进入边界内夹住),而不是原始边界:
        // `-w /`不得使检查站层认为工程是`/`.
        // project_root()集中了这个cwd-vs-boundary分辨率
        // (与git工具+normalize_path共享).
        std::string ws = util::project_root().string();
        if (ws.empty()) ws = util::workspace_root().string();
        // 一个 产卵,不是两个。 `git rev-parse` 按
        // 选项顺序打印每个请求的值, 因此单引用即可同时生成工作树顶
        // -level (第1行)和绝对的git dir(第2行)。
        // 过程创建是昂贵的 Windows ,这个探测器坐落在第一个转弯的关键
        // 路径上( submit_message -> in_git_repo
        // ) -- -- 将产卵量减半,使成本减半。
        auto res = util::run_argv_s(
            {"git", "-C", ws, "rev-parse", "--show-toplevel",
             "--absolute-git-dir"}, 8192, std::chrono::seconds{10});
        if (!res.started || res.exit_code != 0) return r;
        std::string out = res.output;
        const auto nl = out.find('\n');
        if (nl == std::string::npos) return r;   // 需要两行
        r.root    = chomp(out.substr(0, nl));
        r.git_dir = chomp(out.substr(nl + 1));
        r.in_repo = !r.root.empty() && !r.git_dir.empty();
        return r;
    }();
    return info;
}

// 运行时带 GIT_INDEX_FILE 指着我们的刮伤指数。 POSIX
// 获取核心 `env` 前缀( 总是在 Linux/macOS);
// Windows 途经 CMD 的路线 `set … &&` 外壳形式。 输出封顶慷慨,
// 这里有管道输出。 refs/paths ,而非文件 正文.
util::SubprocessResult run_git_scratch(const std::string& index_file,
                                       std::vector<std::string> git_argv,
                                       std::size_t max_bytes = 512 * 1024) {
#ifdef _WIN32
    std::string cmd = "set GIT_INDEX_FILE=" + index_file + "&& ";
    for (const auto& a : git_argv) {
        cmd += '"';
        for (char c : a) { if (c == '"') cmd += '\\'; cmd += c; }
        cmd += "\" ";
    }
    return util::run_command_s(cmd, max_bytes, std::chrono::seconds{120});
#else
    std::vector<std::string> argv;
    argv.reserve(git_argv.size() + 2);
    argv.emplace_back("env");
    argv.emplace_back("GIT_INDEX_FILE=" + index_file);
    for (auto& a : git_argv) argv.push_back(std::move(a));
    return util::run_argv_s(argv, max_bytes, std::chrono::seconds{120});
#endif
}

util::SubprocessResult run_git(std::vector<std::string> tail,
                               std::size_t max_bytes = 512 * 1024) {
    std::vector<std::string> argv = {"git", "-C", repo().root};
    for (auto& a : tail) argv.push_back(std::move(a));
    return util::run_argv_s(argv, max_bytes, std::chrono::seconds{120});
}

bool ok(const util::SubprocessResult& r) {
    return r.started && !r.timed_out && r.exit_code == 0;
}

// 切除进程中被杀死的索引文件 -operation。一个基特 调用
// 这里可能运行120,所以只有文件闲置 远过去,这是 过期 ;
// 运行(此进程或其它进程) agentty ) 触摸其档案.
// 每十六新洗一汗,常足为足.
void sweep_stale_scratch_indexes() {
    std::error_code ec;
    const auto cutoff = fs::file_time_type::clock::now() - std::chrono::minutes{10};
    for (const auto& e : fs::directory_iterator(fs::path(repo().git_dir), ec)) {
        const auto name = e.path().filename().string();
        if (!name.starts_with("agentty-checkpoint-index")) continue;
        std::error_code tec;
        const auto t = fs::last_write_time(e.path(), tec);
        if (!tec && t < cutoff) fs::remove(e.path(), tec);
    }
}

// 切除 git dir 内的索引路径, UNIQUE 百分比 调
// 用 种子从a COPY 实际指数的数值 `add -A` 仅重
// -hashes 数据信息有变化的文件——没有种子,
// 每个检查点都会重新 -hash 将每个操作的区分文件替换为旧的守护 锁:
// 编号 正文 跨越了 git 子进程, 以及 exec.cpp's
// Windows 标准 -feeder 加入薄荷块 thread_root
// 内 run_child 被扣押的 锁 中止( 3) jaal ),
// 从而在第一次提交(A/C.5/52/L.48)中使进程失去活力。 仓库。
// 每个操作有一个索引文件, 锁 没有什么可以保护的, 和创造者 -vs-restore
// 工作树 竞态 保留在应用程序级别( 仅从 Idle 运行到
// thread_loading 设置; 拒绝时提交。
std::string prepare_scratch_index() {
    static std::atomic<unsigned> seq{0};
    const unsigned n = seq.fetch_add(1, std::memory_order_relaxed);
    if (n % 16 == 0) sweep_stale_scratch_indexes();
    // 名字里也写着"皮德": 2 agentty 在一个 仓库 不可共享文件。
#ifdef _WIN32
    const unsigned long pid = ::GetCurrentProcessId();
#else
    const unsigned long pid = static_cast<unsigned long>(::getpid());
#endif
    const fs::path idx = fs::path(repo().git_dir)
        / ("agentty-checkpoint-index." + std::to_string(pid)
           + "." + std::to_string(n));
    std::error_code ec;
    fs::remove(idx, ec);
    fs::copy_file(fs::path(repo().git_dir) / "index", idx,
                  fs::copy_options::overwrite_existing, ec);
    // 复制失败( 新鲜的仓库 尚未有索引) 是好的 ——
    // git 从头创建抓痕指数, 速度更慢。
    return idx.string();
}

void drop_scratch_index(const std::string& idx) {
    std::error_code ec;
    fs::remove(fs::path(idx), ec);
}

std::string ref_for(const std::string& id) {
    return std::string{kRefPrefix} + id;
}

// Delete 检查点以外的最古老的检查点。 Creativedate somes
// older -first ; 保存前的一切 -window 末尾 继续
//
// 两个创造可以同时磨磨( 不) 锁。每个删除列表中
// 包含的强制值, 只有当此后没有人移动它时,
// 才会放弃; 已删除的强制值则失败。 调用.
void prune_old_checkpoints() {
    auto r = run_git({"for-each-ref", "--sort=creatordate",
                      "--format=%(refname) %(objectname)", std::string{kRefPrefix}});
    if (!ok(r)) return;
    std::vector<std::pair<std::string, std::string>> refs;   // 参考书目,sha
    std::istringstream in(r.output);
    for (std::string line; std::getline(in, line); ) {
        line = chomp(std::move(line));
        const auto sp = line.rfind(' ');
        if (sp != std::string::npos) refs.emplace_back(line.substr(0, sp), line.substr(sp + 1));
    }
    if (static_cast<int>(refs.size()) <= kKeepCheckpoints) return;
    const std::size_t drop = refs.size() - static_cast<std::size_t>(kKeepCheckpoints);
    for (std::size_t i = 0; i < drop; ++i)
        (void)run_git({"update-ref", "-d", refs[i].first, refs[i].second});
}

// 仓库-relative 检查点中记录的每个文件的路径 源码树.
//
// NEWLINE-separated, 不 -z : 子进程运行器擦除
// 终端 控制 字节 全部捕获产出( strip_terminal_controls ),
// 以及 NUL 是一个 C0 控制器 – 每个\0 分隔符被默默地丢弃,
// 解析器 没有分离器, 和两个列表都回来了 空 恢复 -half
// (删除检查站后创建的文件)从未运行,简要基线错误.
// -c core.quotePath =假发生 UTF-8 字节
// (无C) -style 引用非 -ASCII 名称); 包含一个
// NEWLINE 格式上仍然模糊不清,因此这些条目被防守性跳过(
// 引用时甚至有 quotePath =虚假,引用条目的开头是 '"'
// ——过滤这些而不是错误 -delete).
bool snapshot_paths(const std::string& commit,
                    std::unordered_set<std::string>& out) {
    auto r = run_git({"-c", "core.quotePath=false",
                      "ls-tree", "-r", "--name-only", commit},
                     16 * 1024 * 1024);
    if (!ok(r)) return false;
    std::istringstream in(r.output);
    for (std::string line; std::getline(in, line); ) {
        line = chomp(std::move(line));
        if (line.empty() || line.front() == '"') continue;
        out.emplace(std::move(line));
    }
    return true;
}

// 仓库-relative 工作树上的一切道路:
// 跟踪( -c 加上未跟踪 -but-not-ignored (-o --exclude-standard).
// 一样 newline/quotePath 理由说明 snapshot_paths.
bool current_paths(std::vector<std::string>& out) {
    auto r = run_git({"-c", "core.quotePath=false",
                      "ls-files", "-c", "-o", "--exclude-standard"},
                     16 * 1024 * 1024);
    if (!ok(r)) return false;
    std::istringstream in(r.output);
    for (std::string line; std::getline(in, line); ) {
        line = chomp(std::move(line));
        if (line.empty() || line.front() == '"') continue;
        out.emplace_back(std::move(line));
    }
    return true;
}

} // 命名空间结束。

bool in_git_repo(Io) { return repo().in_repo; }

// 一旦repo()至少被迫一次(由启动前的OR由任何较早的
// 阻断呼叫者强制),则设定为真。提交路径读取此文,
// 以决定in_git_repo()是立即回答(缓存温暖),还是必须在UI线程
// (缓存寒冷)上产出`git`。
// 见in_git_repo_if_ready()。
namespace {
std::atomic<bool> g_repo_ready{false};
}

std::optional<bool> in_git_repo_if_ready(Io) {
    if (!g_repo_ready.load(std::memory_order_acquire)) return std::nullopt;
    return repo().in_repo;   // 已经建的 缓存 : 这是一个纯字段 读取
}

void prewarm_repo_info(std::stop_token stop) {
    // SYNCHRONOUS 触摸( 触摸) repo() 强制其职能 -local
    // 静态到备忘列表, 运行两个 `git rev-parse` 这里的探测
    // 器 而不是第一次提交。
    //
    // 运行为 Cmd::task_isolated 关闭时 jaal 停止( L) `stop` 等待着
    // for us to return; the probe fills a static, so skip it once stopped
    // 或快速出口竞态静态破坏.
    if (stop.stop_requested()) return;
    (void)repo();
    g_repo_ready.store(true, std::memory_order_release);
}

namespace {
bool create_checkpoint_locked(const std::string& id);

// 源码树 以刮痕指数构建的当前工作树的 id(种子来自真实的)
// `add -A` 仅重 -hashes 已更改文件),失败时无效。
std::optional<std::string> worktree_tree() {
    const std::string idx = prepare_scratch_index();
    // --ignore-errors : 一个无法读取的文件( 纸质, 中段已消失) -scan 降解为
    // "not in the tree" 而不是失败。
    if (!ok(run_git_scratch(idx, {"git", "-C", repo().root, "add", "-A",
                                  "--ignore-errors"}))) {
        drop_scratch_index(idx);
        return std::nullopt;
    }
    auto tree = run_git_scratch(idx, {"git", "-C", repo().root, "write-tree"});
    drop_scratch_index(idx);
    if (!ok(tree)) return std::nullopt;
    return chomp(tree.output);
}
} // 命名空间结束。

CheckpointDiff checkpoint_summary(const std::string& id) {
    CheckpointDiff out;
    if (!repo().in_repo || id.empty()) return out;

    // 解决检查站的犯案问题;如果被查封,则不动声色地保释。
    auto rev = run_git({"rev-parse", "--verify", "--quiet",
                        ref_for(id) + "^{commit}"});
    if (!ok(rev)) return out;
    const std::string commit = chomp(rev.output);

    // 建造丢弃 源码树 会 议 日 程 和 议 程 当前 工作树完全
    // 一样 create_checkpoint 做(从真种中切出索引)
    // `add -A` 仅重 -hashes 已更改文件,然后是数字
    // -diff 检查站 源码树 对抗它,做它 源码树-vs-tree
    // (无) 源码树-vs-worktree 保持其历史
    // untracked/new-file 与实际回落的倒数相同。
    const auto tree = worktree_tree();
    if (!tree) return out;
    const std::string& cur_tree = *tree;

    // --numstat : 每更改文件一条行, "<added>\t<deleted>\t<path>".
    // 二进制程序 文件报表 "-\t-\t<path>" ——算作一个已更改的文件,
    // 没有线条三角洲. -M 允许重命名检测, 因此移动的文件是更改,
    // 而不是添加+删除对。
    auto ns = run_git({"diff", "--numstat", "-M", commit, cur_tree},
                      16 * 1024 * 1024);
    if (!ok(ns)) return out;

    std::istringstream in(ns.output);
    for (std::string line; std::getline(in, line); ) {
        line = chomp(std::move(line));
        if (line.empty()) continue;
        ++out.files_changed;
        // 解析 双主标签 -separated 整数列; "-" (二
        // 进制程序 ) 提供零行三角洲,但仍是一个已更改的文件.
        auto t1 = line.find('\t');
        if (t1 == std::string::npos) continue;
        auto t2 = line.find('\t', t1 + 1);
        if (t2 == std::string::npos) continue;
        auto to_int = [](std::string_view s) -> int {
            if (s == "-" || s.empty()) return 0;
            int v = 0;
            for (char c : s) { if (c < '0' || c > '9') return 0; v = ::agentty::util::push_digit(v, c); }
            return v;
        };
        out.insertions += to_int(std::string_view{line}.substr(0, t1));
        out.deletions  += to_int(std::string_view{line}.substr(t1 + 1, t2 - t1 - 1));
    }
    out.valid = true;
    return out;
}

bool create_checkpoint(const std::string& id) {
    if (!repo().in_repo || id.empty()) return false;
    return create_checkpoint_locked(id);
}

namespace {
bool create_checkpoint_locked(const std::string& id) {
    // EVERYTHING阶段(跟踪更改+新文件,包含删除内容).
    const auto tree = worktree_tree();
    if (!tree) return false;

    // 无父母责任: 永远不恢复历史, 任何父母都
    // 无法放弃裁判, 使得整个 快照 是可以收集的。
    // 身份证被强制内在 所以检查站在工作 仓库 用户未配置的
    // user.name/email 还没呢
    auto commit = run_git({"-c", "user.name=agentty",
                           "-c", "user.email=checkpoint@agentty",
                           "commit-tree", *tree,
                           "-m", "agentty checkpoint " + id});
    if (!ok(commit)) return false;

    if (!ok(run_git({"update-ref", ref_for(id), chomp(commit.output)})))
        return false;

    prune_old_checkpoints();
    return true;
}
} // 命名空间结束。

bool checkpoint_exists(const std::string& id) {
    if (!repo().in_repo || id.empty()) return false;
    return ok(run_git({"rev-parse", "--verify", "--quiet",
                       ref_for(id) + "^{commit}"}));
}

bool restore_checkpoint(const std::string& id,
                        std::string* error) {
    auto fail = [&](std::string why) {
        if (error) *error = std::move(why);
        return false;
    };
    if (!repo().in_repo) return fail("not inside a git repository");
    if (id.empty())      return fail("empty checkpoint id");

    auto rev = run_git({"rev-parse", "--verify", "--quiet",
                        ref_for(id) + "^{commit}"});
    if (!ok(rev)) return fail("checkpoint no longer exists (pruned?)");
    const std::string commit = chomp(rev.output);

    // 快照 + 当前文件集 首次 如果名单中的任何一项都
    // 失败了 我们先保释再碰上一个 字节 工作树。
    std::unordered_set<std::string> snap;
    if (!snapshot_paths(commit, snap)) return fail("failed to list checkpoint contents");
    std::vector<std::string> now;
    if (!current_paths(now)) return fail("failed to list current files");

    // 重写检查点上的所有快照文件 源码树 通过刮痕索引:
    // 已读 -tree 充电, 退票 -index -a
    // -f 部队 -writes。真实索引和 HEAD 保持无
    // 动于衷 -- `git status` 之后显示恢复
    // 状态为普通工作 -tree 更改,这正是用户的期望。
    const std::string idx = prepare_scratch_index();
    const bool wrote =
        ok(run_git_scratch(idx, {"git", "-C", repo().root, "read-tree", commit}))
        && ok(run_git_scratch(idx, {"git", "-C", repo().root,
                                    "checkout-index", "-a", "-f"}));
    drop_scratch_index(idx);
    if (!wrote) return fail("failed to write checkpoint files");

    // Delete 已存在但未在 快照 - 电话
    // "agent created a file after this point" 倒带的一半。
    // 忽略的文件( 构建输出, ) 缓存 ) 从未出现在
    // `now` 最好还是删除 -effort ;失败的解链接
    // 会降解为剩余文件,而不是失败的恢复.
    const fs::path root{repo().root};
    for (const auto& rel : now) {
        if (snap.contains(rel)) continue;
        std::error_code ec;
        fs::remove(root / rel, ec);
    }
    return true;
}

} // namespace agentty::workspace
