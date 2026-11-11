// sandbox_provenance.cpp-WHICH路径塑造了主机后来的信任。
//
// 匹配移至claybin (clay::shape_matches，
// policy/path_shape.hpp)，因为它是具有尖锐边缘的通用机制：整个
// -component而不是子串，或者规则在".gitignore"上触发并被忽略；
// 和形状而不是确切的名称，或者 `git --git-dir=.foo` 走过它，
// 这是 Cursor 在 3.0.0 中发布并修复的错误。
//
// 这里留下的是agentty的意见的唯一部分：TABLE。
// 每一个条目都是记录在案的逃亡地点，或者明显的兄弟姐妹。
// claybin 对 ".vscode/tasks.json" 是否重要
// 没有看法——这是关于开发者工具如何行为的声明，而不是关于内核的声明。

#include "agentty/domain/sandbox_provenance.hpp"
#include "agentty/domain/sandbox_config.hpp"   // 配置，kAlwaysMasked*，mask_paths

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <span>

#include <maya/runtime.hpp>

// 匹配器位于 claybin 中，这是必需的 子模块 ——所以这是一个普通的包含，
// 而不是受保护的包含。
//
// 这用于携带用于没有claybin的构建的相同形状逻辑
// 的第二本地副本，理由是信任-handoff TABLE
// 是agentty的意见并且应该编译无论如何。
// 这个推理是合理的，但结论是错误的：这意味着六十行
// 重复匹配，两种分类可能不同的配置，以及承认重复的注释
// 是 "deliberate"。由于需要 claybin，
// 因此只有一个匹配器，这正是边界审核首先想要的。
#include "claybin/policy/path_shape.hpp"

namespace agentty::sandbox_cfg {

namespace {

using clay::ShapeMatch;
using clay::ShapeRule;
using clay::shape_matches;

// tag == TrustKind，通过claybin携带的不透明整数进行投射。
constexpr std::uint32_t tag(TrustKind k) { return static_cast<std::uint32_t>(k); }

// 有序：第一场比赛获胜，因此更具体的原因首先出现，
// 其中路径可能符合两个规则。
constexpr std::array kRules{
    // ── 生命周期钩:CVE-2026-48124(Cursor,CVSS 8.5)  ⁇ . ──────────────
    ShapeRule{"hooks.json", tag(TrustKind::HookConfig), ShapeMatch::Basename},
    ShapeRule{".claude", tag(TrustKind::HookConfig), ShapeMatch::Component},
    ShapeRule{".agentty", tag(TrustKind::HookConfig), ShapeMatch::Component},
    ShapeRule{".cursor", tag(TrustKind::HookConfig), ShapeMatch::Component},

    // ── 编辑器自动化:反重力的".vscode time bomb" ────────────
    ShapeRule{".vscode", tag(TrustKind::EditorTask), ShapeMatch::Component},
    ShapeRule{"tasks.json", tag(TrustKind::EditorTask), ShapeMatch::Basename},
    ShapeRule{"launch.json", tag(TrustKind::EditorTask), ShapeMatch::Basename},
    ShapeRule{".idea", tag(TrustKind::EditorTask), ShapeMatch::Component},

    // ── ⁇ :fsmonitor + 钩子 + 配置 (Cursor 3.0.0) ──────────────────
    //
    // ".git" 作为 COMPONENT，因此重命名的 git dir 不会溜过去，
    // 并且内部按基本名称，因为该目录可以被称为任何东西，而 `config`
    // 和 `HEAD` 则不能。
    ShapeRule{".git", tag(TrustKind::GitConfig), ShapeMatch::Component},
    ShapeRule{"config", tag(TrustKind::GitConfig), ShapeMatch::Basename},
    ShapeRule{"HEAD", tag(TrustKind::GitConfig), ShapeMatch::Basename},
    ShapeRule{"hooks", tag(TrustKind::GitConfig), ShapeMatch::Component},

    // ── 口译和入境点: GHSA-p9g2-cr55-cw9c ──────────────
    //
    // Python扩展在发现期间执行venv解释器。
    // 工作区 下的任何 bin/ 都具有相同的属性。
    ShapeRule{"node_modules/.bin", tag(TrustKind::Interpreter), ShapeMatch::Component},
    ShapeRule{"pyvenv.cfg", tag(TrustKind::Interpreter), ShapeMatch::Basename},
    ShapeRule{".venv", tag(TrustKind::Interpreter), ShapeMatch::Component},
    ShapeRule{"venv", tag(TrustKind::Interpreter), ShapeMatch::Component},

    // ── shell init: 运行在用户的下一个 shell 上 ──────────────────
    ShapeRule{".envrc", tag(TrustKind::ShellInit), ShapeMatch::Basename},
    ShapeRule{".bashrc", tag(TrustKind::ShellInit), ShapeMatch::Basename},
    ShapeRule{".zshrc", tag(TrustKind::ShellInit), ShapeMatch::Basename},
    ShapeRule{".profile", tag(TrustKind::ShellInit), ShapeMatch::Basename},
    ShapeRule{".bash_profile", tag(TrustKind::ShellInit), ShapeMatch::Basename},
    ShapeRule{".bash_login", tag(TrustKind::ShellInit), ShapeMatch::Basename},
    ShapeRule{".bash_logout", tag(TrustKind::ShellInit), ShapeMatch::Basename},
    ShapeRule{".zprofile", tag(TrustKind::ShellInit), ShapeMatch::Basename},
    ShapeRule{".zshenv", tag(TrustKind::ShellInit), ShapeMatch::Basename},
    ShapeRule{".zlogin", tag(TrustKind::ShellInit), ShapeMatch::Basename},
    ShapeRule{".xprofile", tag(TrustKind::ShellInit), ShapeMatch::Basename},
    ShapeRule{".xinitrc", tag(TrustKind::ShellInit), ShapeMatch::Basename},
    ShapeRule{".xsession", tag(TrustKind::ShellInit), ShapeMatch::Basename},
    ShapeRule{".pam_environment", tag(TrustKind::ShellInit), ShapeMatch::Basename},
    ShapeRule{"activate", tag(TrustKind::ShellInit), ShapeMatch::Basename},
    ShapeRule{".direnv", tag(TrustKind::ShellInit), ShapeMatch::Component},
    // fish，以及per-userenvironment.d，在每次登录时读取
    ShapeRule{".config/fish", tag(TrustKind::ShellInit), ShapeMatch::Component},
    ShapeRule{".config/environment.d", tag(TrustKind::ShellInit), ShapeMatch::Component},

    // ── 会话开始: 在下一个登录时运行, 位于任意的 沙箱 之外 ───────
    ShapeRule{".config/autostart", tag(TrustKind::SessionStart), ShapeMatch::Component},
    ShapeRule{".config/systemd", tag(TrustKind::SessionStart), ShapeMatch::Component},
    ShapeRule{".config/hypr", tag(TrustKind::SessionStart), ShapeMatch::Component},
    ShapeRule{".config/sway", tag(TrustKind::SessionStart), ShapeMatch::Component},
    ShapeRule{".config/i3", tag(TrustKind::SessionStart), ShapeMatch::Component},
    ShapeRule{".config/plasma-workspace", tag(TrustKind::SessionStart), ShapeMatch::Component},

    // ── PATH: 这里的文件会显示每个后期 shell 的 真实命令 ──
    ShapeRule{".local/bin", tag(TrustKind::Interpreter), ShapeMatch::Component},

    // ── 其他代理: 配置命令他们运行的未保存的名 ───
    ShapeRule{".codex", tag(TrustKind::HookConfig), ShapeMatch::Component},
    ShapeRule{".gemini", tag(TrustKind::HookConfig), ShapeMatch::Component},

    // ── 构建脚本: 在下一个构建上运行, 即将完成 ─────────────
    ShapeRule{"Makefile", tag(TrustKind::BuildScript), ShapeMatch::Basename},
    ShapeRule{"package.json", tag(TrustKind::BuildScript), ShapeMatch::Basename},
    ShapeRule{"build.rs", tag(TrustKind::BuildScript), ShapeMatch::Basename},
    ShapeRule{"CMakeLists.txt", tag(TrustKind::BuildScript), ShapeMatch::Basename},
    ShapeRule{"conftest.py", tag(TrustKind::BuildScript), ShapeMatch::Basename},

    // ── 守护进程套接字 : GHSA-v4xv-rqh3-w9mc ─────────────────────────────
    //
    // "One Docker socket to rule them all"转义Codex，
    // 光标AND Gemini CLI。特权本地守护进程是第二个执行环境。
    ShapeRule{"docker.sock", tag(TrustKind::DaemonSocket), ShapeMatch::Basename},
    ShapeRule{"containerd.sock", tag(TrustKind::DaemonSocket), ShapeMatch::Basename},
    ShapeRule{"podman.sock", tag(TrustKind::DaemonSocket), ShapeMatch::Basename},
};

}  // 命名空间结束。

bool is_host_trusted(std::string_view path, TrustKind* out_kind) {
    std::uint32_t t = 0;
#if defined(_WIN32)
    // claybin的形状规则在'/
    // '上分裂。来自 std::filesystem ("C:\\repo\\.git\\hooks\\post-commit") 的 Windows 路径根本没有 '
    // /'，因此 NO 可信文件曾经匹配，并且切换门在 Windows 上没有报告任何内容。匹配通用形式； '
    // \\' 永远不是合法的文件名 字节，
    // 因此这不能创建错误匹配。
    std::string generic{path};
    std::ranges::replace(generic, '\\', '/');
    path = generic;
#endif
    if (!shape_matches(path, std::span<const ShapeRule>{kRules}, &t)) return false;
    if (out_kind) *out_kind = static_cast<TrustKind>(t);
    return true;
}

namespace {

// 目录永远不值得为凭据文件而走动。
//
// 需要两个属性和 两者 来证明跳过一个属性是合理的：它确实
// 是巨大的（因此每次生成时都会花费实时时间），并且 凭据
// 文件并不合法地存在于那里。第二个是安全论点——如果这个
// 列表中的某个名字曾经成为秘密 DO 的某个地方，
// 那么跳过它就不再是一个性能决策，而是成为一个漏洞。
//
// .git 是一个有趣的情况：它不保存 `.env`，但 执行 保存
// `.git/config`，它可以携带 凭据 助手。这是由上面的 trust
// -handoff 规则（TrustKind::GitConfig）覆盖的，而不是通过屏蔽来覆盖，
// 因此在这里跳过它不会丢失任何东西。
constexpr std::string_view kNeverWalk[] = {
    ".git", "node_modules", "target", "build", "dist", ".venv", "venv",
    "__pycache__", ".mypy_cache", ".pytest_cache", ".cache", ".next",
    ".gradle", ".tox", "vendor", "Pods", ".terraform",
};

[[nodiscard]] bool never_walk(std::string_view name) {
    for (auto d : kNeverWalk) if (name == d) return true;
    return false;
}

// 该基本名称是否命名为 凭据 文件？
//
// 两种规则，由显式标记区分：
//
//   SUFFIX `*.pem`、`*.tfvars` —匹配扩展名中的任何文件 ENDING。
//           这就是步行必须存在的原因：如果不查看磁盘
//           上实际存在的内容，则后缀无法转变为路径。
//   NAME 其他所有内容都与整个基本名称完全匹配。
//
// 标记是明确的而不是从形状推断的。
// 推断 "starts with a dot and has no second dot"
// 看起来很整洁，但却是错误的：它默默地将 `.env`、`.npmrc`
// 和 `.netrc` 提升为后缀，因此 `prod.env` 和
// `scoped.npmrc` 也开始被屏蔽。将 SECURITY
// 规则的覆盖范围扩大为添加不相关条目的 副作用 正是此列表绝不能发生的意外
// - 不知从何而来的掩码与丢失的掩码一样令人困惑，并且没有人审查该列表会看到
// it.
[[nodiscard]] bool is_masked_name(std::string_view name) {
    for (const char* n : kAlwaysMaskedNames) {
        std::string_view rule{n};
        if (rule.starts_with("*")) {
            rule.remove_prefix(1);                 // "*.pem" -> ".pem"
            // `>`不是`>=`，因此该规则从不匹配IS裸后缀的
            // 文件：名为".pem"的文件不是密钥。
            if (name.size() > rule.size() && name.ends_with(rule)) return true;
            continue;
        }
        if (name == rule) return true;
    }
    return false;
}

// 从 `dir` 走到 `depth_left` 层，附加基本名称为 凭据
// 名称的每个文件。
//
// 递归且安全：深度以第一个 调用 之前的 mask_scan_depth 为界，
// 并且从不遵循目录符号链接，因此没有输入可以将
// 其驱动得比界限更深。
void sweep(const std::filesystem::path& dir, int depth_left,
           std::vector<std::string>& out) {
    std::error_code ec;
    // skip_permission_denied：工作区
    // 可以包含我们无法读取的目录，并且掩蔽扫描不得使其中
    // 一个目录的整个生成失败。目录符号链接后面不是——跟随
    // 工作区 中的一个将掩盖用户从未询问过的主机路径，
    // 而指向内部的一个将遍历 源码树 两次。
    std::filesystem::directory_iterator it{
        dir, std::filesystem::directory_options::skip_permission_denied, ec};
    if (ec) return;

    for (const auto& entry : it) {
        std::error_code sec;
        const auto name = entry.path().filename().string();
        // symlink_status，不是状态：符号链接NAMED .
        // env被掩码为链接而不是跟随。跟随它会掩盖它指向
        // 的任何地方，并使 链接 本身可读。
        const auto st = entry.symlink_status(sec);
        if (sec) continue;

        if (std::filesystem::is_directory(st)) {
            if (depth_left > 0 && !never_walk(name))
                sweep(entry.path(), depth_left - 1, out);
            continue;
        }
        if (is_masked_name(name)) out.push_back(entry.path().string());
    }
}

// 上面的`sweep`遍历的DIRECTORY SHAPE的
// 指纹：它将下降到的每个目录的mtime，折叠在一起。
//
// 为什么这是扫描的缓存的右键，以及为什么它是精确的而不是近似的：创建、
// 删除或重命名条目会更新目录CONTAINING的运行时间。
// 这是唯一可以更改扫描返回的文件的突变 - 编辑文件的
// CONTENTS 无法将其添加到列表中或将其从列表中删除，
// 并且该操作的父级 mtime 不会移动。
// 所以 "same stamp" 确实意味着 "same answer"。
//
// 它必须镜像扫描的遍历EXACTLY——相同的深度算术、
// 相同的never_walk跳过、相同的no-follow规则。
// 访问的目录少于扫描的邮票将对其跳过的目录内的更改视而不见，
// 这是一个 过期 掩码，这是一个洞。因此，
// 这两个功能故意采用相同的形状；改变其中一个，就必须改变另一个。
//
// 故意便宜：每个DIRECTORY一个status()，除了迭代器已经产生的内容之外，
// 没有对其条目的readdir，并且没有每个-file symlink_status。
std::uint64_t dir_stamp(const std::filesystem::path& dir, int depth_left) {
    std::error_code ec;
    std::uint64_t h = 1469598103934665603ull;          // FNV-1a偏移基础
    const auto mix = [&h](std::uint64_t v) {
        h ^= v;
        h *= 1099511628211ull;
    };

    const auto mt = std::filesystem::last_write_time(dir, ec);
    if (!ec)
        mix(static_cast<std::uint64_t>(mt.time_since_epoch().count()));

    std::filesystem::directory_iterator it{
        dir, std::filesystem::directory_options::skip_permission_denied, ec};
    if (ec) return h;

    for (const auto& entry : it) {
        if (depth_left <= 0) break;
        std::error_code sec;
        const auto st = entry.symlink_status(sec);
        if (sec || !std::filesystem::is_directory(st)) continue;
        const auto name = entry.path().filename().string();
        if (never_walk(name)) continue;
        mix(dir_stamp(entry.path(), depth_left - 1));
    }
    return h;
}

}  // 命名空间结束。

std::vector<std::string> mask_paths(const Config& cfg, std::string_view workspace,
                                   std::string_view home) {
    std::vector<std::string> out;

    // 1. $HOME 凭据。根据设计不可配置：用户控制
    //    可以关闭以使它们的构建工作处于关闭状态的控件。
    if (!home.empty()) {
        const std::string h{home};
        for (const char* m : kAlwaysMasked) out.push_back(h + m);
    }

    // 2. 工作区里面的凭据名字，通过行走找到。的
    //    工作区由两个后端绑定READ-WRITE，因此范围不能
    //    保存这里的任何东西，并且掩模是唯一的墙。
    if (!workspace.empty()) {
        // CACHED，因为这是在EVERY命令的延迟路径中。
        //
        // 头文件已经警告
        // 会让每条 shell 命令的延迟路径都包含一次完整目录扫描。
        // 这里无界行走并限制深度作为响应。边界是不够的：在 agentty
        // 源码树 上以发送深度 = 3 进行测量，扫描花费 3.7 毫秒，
        // 并为每个生成再次运行 - re-deriving 每次都会得到
        // 字节-identical 答案。针对大约 10 毫秒的沙盒
        // `true`，这是大部分内容，它随着 仓库 而不是随着工作而增长。
        //
        // 该 KEY IS 该 TREE'S 自身 SHAPE，不是时钟。
        //
        // time-to-live是明显的第一次尝试，这里是WRONG：
        // 这意味着出现在TTL内的凭据可由下一个命令读取。
        // sandbox_live_check 立即捕获了它——它植入了
        // `services/api/key.pem` 并在不到一秒的时间
        // 内对其运行了命令，而 过期 列表让 正文 通过了。一个最终只是
        // -correct的面具并不是一堵墙，一个2-second的洞对于特工经常做的
        // `write file` → `run tool`对来说
        // 已经足够长了。
        //
        // 因此，关键是扫描下降到的每个 DIRECTORY 的 mtime。
        // 创建、删除或重命名文件会影响其父文件的修改时间——这是
        // POSIX 在此保证的一件事，并且正是 事件 可以更改此列表。
        // Stat-ing 目录的成本只是遍历其条目的一小部分（每个
        // 目录一个系统调用，没有 readdir，没有 per-file
        // symlink_status），因此常见情况在保持
        // 精确的同时保持便宜。
        // maya::snapshot而不是锁：命令从工具工作线程 线程产生，
        // 因此这确实是共享的，但它是由(ws、深度、标记)键入的
        // read-mostly值。读者拿一个原子加载；未命中则扫除
        // OUTSIDE 任何 锁 并公布结果。两次并发的失误都会
        // 导致扫描失败，最后一个存储获胜——这两个结果对于它们的
        // 密钥来说都是正确的，因此这会花费一段时间，而不是正确性。
        struct Cache {
            std::string              ws;
            int                      depth = -1;
            std::uint64_t            stamp = 0;
            std::vector<std::string> paths;
        };
        // Function-local静态：第一命令可以在启动期间从
        // 工作线程登陆，并且文件-scope全局将竞态其自己的构造函数。
        static maya::snapshot<Cache> cache;

        const int depth = static_cast<int>(cfg.mask_scan_depth);
        // OUTSIDE、锁:stat-ing、
        // 源码树是缓慢的部分，并且将锁跨过它会序列化一次文件
        // 系统行走之后的每个并发生成——将延迟修复变成延迟错误。
        const std::uint64_t stamp =
            dir_stamp(std::filesystem::path{workspace}, depth);

        // 每个命令复制〜16个短字符串一次并不妨碍
        // 这种存在的避免。
        std::vector<std::string> hits;
        {
            auto cur = cache.load();
            if (cur->ws == workspace && cur->depth == depth && cur->stamp == stamp) {
                hits = cur->paths;
            } else {
                Cache next;
                sweep(std::filesystem::path{workspace}, depth, next.paths);
                next.ws    = std::string{workspace};
                next.depth = depth;
                next.stamp = stamp;
                hits = next.paths;
                cache.store(std::move(next));
            }
        }
        out.insert(out.end(), std::make_move_iterator(hits.begin()),
                   std::make_move_iterator(hits.end()));
    }

    // 3.最后是用户自己的拒绝，因此显式拒绝无法撤消
    //    由我们之一。
    for (const auto& d : cfg.deny_paths)
        if (auto r = resolve_user_path(d, workspace, home); !r.empty())
            out.push_back(std::move(r));

    // 重复数据删除：路径可以从扫描和从deny_paths到达，
    // 并且发射相同的安装两次最多是计划中的噪声。排序也是使 argv
    // 在运行中保持稳定的原因——否则测试断言顺序在 Directory
    // -iteration 顺序上会不稳定。
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::string resolve_user_path(std::string_view path, std::string_view workspace,
                              std::string_view home) {
    if (path.empty()) return {};
    std::filesystem::path p;
    if (path == "~" || path.starts_with("~/")) {
        if (home.empty()) return {};
        p = std::filesystem::path{std::string{home}} / std::string{path.substr(path == "~" ? 1 : 2)};
    } else {
        p = std::filesystem::path{std::string{path}};
        if (p.is_relative()) {
            if (workspace.empty()) return {};
            p = std::filesystem::path{std::string{workspace}} / p;
        }
    }
    std::string s = p.lexically_normal().string();
    while (s.size() > 1 && (s.back() == '/' || s.back() == '\\')) s.pop_back();
    return s;
}

Config apply_posture(const Config& base, Posture p) {
    Config c = base;
    c.configured = true;

    // 故意触及NOT：后端，read_paths，write_paths，deny_paths。
    // 姿势是关于墙壁有多紧的陈述；引擎和用户的
    // 项目-specific路径列表与姿势无关。
    // 参见头文件。

    switch (p) {
        case Posture::Permissive:
            // 关闭，不是"loose"。没有过滤器，没有盖子，
            // 也没有网络——只有两个后端总是构建的安装墙。
            // 它的存在是为了让工具链中断的用户有一个不是
            // `--sandbox off` 的地方可以去：掩码和
            // 挂载命名空间仍然保留，这绝对比没有更重要。
            c.fs_scope = FsScope::Toolchain;
            c.net_mode = NetMode::Full;
            c.syscall_mode = SyscallMode::Off;
            c.wx_protect = false;
            c.memory_mb = 0;
            // Permissive 仍保留防止 fork 炸弹的进程数上限，
            // 并且它是唯一一个资源墙。
            //
            // memory/cpu/nofile/time在这里全部变成0，
            // 因为现场评论给出的原因是：正确的上限是机器和构建的属性，
            // 因此错误的猜测将工作的`cargo build`变成了OOM杀戮，
            // 看起来像是agentty的错误。放弃它们使得 Permissive
            // 成为 "my toolchain breaks" 的真正答案。
            //
            // max_procs不同，该领域自己的评论说明了原因：
            // 与内存不同，进程数存在普遍安全的上限。正常构建不需要同时
            // 运行 4096 个进程；即使在 64 核机器上执行 make -j，
            // 峰值通常也只有数百个。异常情形通常是试图创建数百万进程的
            // fork 炸弹，例如 `:(){ :|:& };:`。
            //
            // 它与其余部分一起归零，这为每个许可用户提供了来自任何批准命令的
            // 无限分叉炸弹 - 为 tests/sandbox_audit.cpp
            // 的确切发现而编写（它将 resource.pids 视为
            // `none`，这就是为什么 4096 成为默认值），通过预设重新引入。
            // claybin 将 0 视为无限制，并且没有设置 rlimit，
            // 也根本没有设置 pids.max，因此这不是一个
            // 较软的上限；没有上限。
            //
            // 许可意味着"the walls are off"，
            // 而不是"the machine is forfeit"。分叉炸弹不是
            // 工具链兼容性问题，因此没有什么可以破解的。
            c.max_procs = 4096;
            c.cpu_percent = 0;
            c.max_open_files = 0;
            c.wall_clock_secs = 0;
            c.cpu_secs = 0;
            c.scope_ipc = false;
            c.close_inherited_fds = true;   // 从来没有兼容性问题
            c.fake_hostname = false;
            c.mask_scan_depth = 3;          // 秘密被掩盖 REGARDLESS
            // 切换保持在呼叫者的值上而不是被放松。
            // 许可是关于过程周围的围墙；切换门是关于代理为
            // HOST 运行编写的内容，这些是不同的问题（第
            // 14 节）。宽松的沙箱是一个选择；
            // 让代理将你的 git hooks 作为该选择的 副作用
            // 来编写，这不是任何人都能做出的。
            break;

        case Posture::Balanced:
            // 通过构造，正是结构默认值：这就是
            // `configured = false` 已经意味着的，
            // 因此选择平衡的用户显式地获得与从未打开 面板 的用户
            // 相同的边界。将它们写出来而不是默认的
            // -constructing可以使四种姿势并排可读。
            c.fs_scope = FsScope::Toolchain;
            c.net_mode = NetMode::Full;
            c.syscall_mode = SyscallMode::Compiler;
            c.wx_protect = true;
            c.memory_mb = 0;        // 机器的事，不是我们的事
            c.max_procs = 4096;     // 叉-bomb帽；审计发现没有
            c.cpu_percent = 0;
            c.max_open_files = 0;
            c.wall_clock_secs = 0;
            c.cpu_secs = 0;
            c.scope_ipc = true;
            c.close_inherited_fds = true;
            c.fake_hostname = false;
            c.mask_scan_depth = 3;
            c.handoff = HandoffPolicy::Refuse;
            break;

        case Posture::Hardened:
            // claybin可以建造的每堵墙，带有上限，真实的建造得以幸存。
            //
            // 数字是有趣的部分。它们被选择为可生存而不是令人印象
            // 深刻：8 GB 和 400%（四核），因为 链接器
            // 峰值很高，OOM 杀死 mid-build 读取为
            // agentty 错误，并且根本没有 wall-clock 上限，
            // 因为长构建不是攻击，并且在其中触发的 超时
            // 会教用户关闭 沙箱。
            c.fs_scope = FsScope::Minimal;
            c.net_mode = NetMode::Ports;   // 默认为443/80/22/53
            c.syscall_mode = SyscallMode::Strict;
            c.wx_protect = true;
            c.memory_mb = 8192;
            c.max_procs = 2048;
            c.cpu_percent = 400;
            c.max_open_files = 4096;
            c.wall_clock_secs = 0;
            c.cpu_secs = 0;
            c.scope_ipc = true;
            c.close_inherited_fds = true;
            c.fake_hostname = true;        // 将主机名保留在日志之外
            c.mask_scan_depth = 5;         // 更深层次的扫掠；这是一个成本行
            c.handoff = HandoffPolicy::Refuse;
            break;

        case Posture::Airgapped:
            // 这种态度使得§11的承认不真实。
            //
            // 在这个子系统的其他地方，网络是开放的，
            // 并且文档清楚地说明读取访问加网络是读取加渗漏。
            // 这是一个不再真实的设置，这就是为什么
            // 它值得预设，即使它破坏了大多数工作
            // 流程：读取和编辑代码不需要任何成本，
            // 而且它是这里唯一一个泄漏的 凭据 无法
            // 离开机器的配置。
            c.fs_scope = FsScope::Minimal;
            c.net_mode = NetMode::None;
            c.syscall_mode = SyscallMode::Strict;
            c.wx_protect = true;
            c.memory_mb = 8192;
            c.max_procs = 2048;
            c.cpu_percent = 400;
            c.max_open_files = 4096;
            c.wall_clock_secs = 0;
            c.cpu_secs = 0;
            c.scope_ipc = true;
            c.close_inherited_fds = true;
            c.fake_hostname = true;
            c.mask_scan_depth = 5;
            c.handoff = HandoffPolicy::Refuse;
            break;

        case Posture::Custom:
            // 不可选择：是detect_posture REPORTS，
            // 不是要申请的东西。原封不动地返回配置是唯一
            // 合理的答案——没有什么可写的。
            break;
    }
    return c;
}

Posture detect_posture(const Config& cfg) {
    // 通过COMPARISON对照每种姿势将产生的结果，
    // 而不是通过一次检查一个区域。
    //
    // 这就是为什么标签不会错的原因。apply_posture
    // 是姿势含义的唯一定义，并且检测询问它而不是重新定义
    // -encoding同样的知识在第二位可能会漂移。
    // 向 Config 添加一个字段，这样就可以继续工作；
    // 添加字段和手-write比较，以及您忘记窗格开始声明的那一天
    // "Hardened"对于不是的配置。
    //
    // `configured`是从两侧的比较中归一化的。
    // 它是关于用户是否曾经触摸过 面板 的簿记，而不是边界的一部分 -
    // 并且 apply_posture 总是设置它，因此如果没有这个，
    // 则 never-opened 配置（configured =
    // false）将报告自定义，即使它的墙完全平衡。 面板 会在
    // "Custom" 上为每个新用户打开，这既是错误的，也是最没用的。
    Config probe = cfg;
    probe.configured = true;

    // 顺序仅对于显示而言重要：首先检查平衡，
    // 因为这是压倒性的常见答案，因此常见情况需要进行比较。
    for (auto p : {Posture::Balanced, Posture::Hardened, Posture::Airgapped,
                   Posture::Permissive}) {
        if (probe == apply_posture(probe, p)) return p;
    }
    return Posture::Custom;
}

}  // namespace agentty::sandbox_cfg
