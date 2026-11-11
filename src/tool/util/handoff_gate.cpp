// 信托 -handoff 门,见 头文件 为什么这个坐在这里?
// 调用 而不是在内部 沙箱.

#include "agentty/tool/util/handoff_gate.hpp"

#include <maya/runtime.hpp>

#include <algorithm>
#include <ranges>
#include <unordered_set>
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include "agentty/tool/util/sandbox.hpp"
#include "agentty/util/logx.hpp"

namespace agentty::tools::util::handoff {

namespace fs = std::filesystem;

namespace {

// 与经纪人屏蔽的种子相同存储形状和推理:见 sandbox_broker.cpp.
// A 锁-free `maya::snapshot` : 将整个数值
// 替换为小值 `update()` 并被读者复制,所以没有级别和能力;
// 功能 -local 静态,因为第一手交接可以从工具中到达 工作线程
// 启动时的文件 -scope 全球将 竞态 自己的建筑师。
maya::snapshot<std::vector<sandbox_cfg::TrustHandoff>>& feed() {
    static maya::snapshot<std::vector<sandbox_cfg::TrustHandoff>> f;
    return f;
}

// 是否为最近 快照 击出它的帽子。 快照 ,喜欢 feed() ,所以,
// 请 面板 读它没有 锁.
maya::snapshot<bool>& last_truncated() {
    static maya::snapshot<bool> t{false};
    return t;
}

// 与经纪人的饲料相同的原因: 向可信路径的循环写字会
// 毫无限制地增长, 也就是拒绝大门给自己的主机提供服务。
// 比经纪人的64岁要小, 因为交割是更罕见、
// 更响亮的事件 — 如果有32个不同的,那么计算就已经停止了,
// 也就是不久前的信息。
constexpr std::size_t kMaxHandoffs = 32;

[[nodiscard]] std::uint64_t now_ms() {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

void remember(const sandbox_cfg::TrustHandoff& h) {
    (void)feed().update([](const std::vector<sandbox_cfg::TrustHandoff>& old,
                           const sandbox_cfg::TrustHandoff& incoming) {
        std::vector<sandbox_cfg::TrustHandoff> all = old;
        // 折叠重复相同的路径。 不同于经纪人的种子此检查
        // 整个 列表, 而不仅仅是后面: 交接被其路径识别,
        // 而同一文件 一次两次重写 是一个关于一个文件的事实,
        // 不是一个值得保存的序列。
        for (auto& existing : all) {
            if (existing.write.path == incoming.write.path) {
                existing.write.at_ms = incoming.write.at_ms;
                existing.write.command = incoming.write.command;
                return all;
            }
        }
        all.push_back(incoming);
        // 将最老的一时全时丢掉,保留最新的东西 -- --
        // 冲出视野的提前交割需要32个不同的攻击者,
        // 每个攻击者本身都有记录,(默认)被拒绝。
        if (all.size() > kMaxHandoffs) all.erase(all.begin());
        return all;
    }, h);
}

}  // 命名空间结束。

Verdict check_with(std::string_view path, std::string_view tool,
                   sandbox_cfg::HandoffPolicy policy) {
    Verdict v;

    sandbox_cfg::TrustKind kind{};
    bool trusted = sandbox_cfg::is_host_trusted(path, &kind);

    // 如果 SPELLING 无罪,试试看 RESOLVED 选择了它,然后相信它。
    //
    // is_host_trusted() 是一个纯的形状行走,
    // 所以它只看到它交接的。 因此, 共线目录会完全隐藏一个可信任的形状 :
    //
    //     进去 -s .git/hooks 虚伪
    //     的写作 innocuous/post-merge < - 安装一个真正的 git 钩
    //
    // 测量: "innocuous/post-merge" 读作
    // PLAIN ,通过 ".git/hooks/post-merge"
    // 书写在钩子目录中, 无论是哪种方式, 所以只有拼写匹配为一个门,
    // 模型可以通过创建一个连锁- 允许它这样做, 因为里面有
    // 一个连锁 工作区 工作很普通,我需要你帮忙
    //
    // 解析是第二个检查,而不是第一个替换:
    // weakly_canonical 触摸文件系统,
    // 常见的大小写是一条普通路径,必须保持几条字符串比较。只有看起来是
    // 无辜的路径才能为解析支付报酬, `trusted` 短数
    // -circuits 已经匹配的
    if (!trusted) {
        std::error_code ec;
        const fs::path resolved = fs::weakly_canonical(fs::path{path}, ec);
        if (!ec && !resolved.empty() && resolved != fs::path{path}) {
            sandbox_cfg::TrustKind rk{};
            if (sandbox_cfg::is_host_trusted(resolved.string(), &rk)) {
                trusted = true;
                kind = rk;
                AGT_LOG(Tool, Warn, "sandbox.handoff",
                        "path resolves into a trusted location: {} -> {}",
                        logx::body(std::string{path}),
                        logx::body(resolved.string()));
            }
        }
    }

    if (!trusted) {
        // 普通案件,而且必须便宜:这贯穿于代理商制作的每一篇书信上。
        // `is_host_trusted` 是一个在路径形状上行走的表格,
        // 没有 I/O ,所以普通路径需要几条字符串来比较。
        return v;
    }

    v.is_handoff = true;
    v.kind = kind;

    // 记录 之前 政策分支。
    // `Allow` 至今仍是发生在 工作区
    // 并且一个只列出拒绝的种子 会在用户关闭
    // 大门时 安静下来 也就是他们最需要看到
    // 它的时候
    sandbox_cfg::TrustHandoff h;
    h.write.path = std::string{path};
    h.write.command = std::string{tool};
    h.write.at_ms = now_ms();
    h.kind = kind;
    remember(h);

    const auto why = sandbox_cfg::explain(kind);

    // 记录每个结果,允许包含,同样原因 经纪人日志允许:
    // 线索是使边界可以教化的原因。 路径后面是 logx::body
    // ()因为它是 工作区 内容。
    AGT_LOG(Tool, Info, "sandbox.handoff", "{} {} ({}) policy={}",
            policy == sandbox_cfg::HandoffPolicy::Refuse ? "REFUSED" : "allowed",
            logx::body(std::string{path}), why,
            sandbox_cfg::to_string(policy));

    switch (policy) {
        case sandbox_cfg::HandoffPolicy::Allow:
            return v;

        case sandbox_cfg::HandoffPolicy::Warn:
            // 收益,但说是这样 原因就在于此,所以打电话者
            // 可以不带 -deriving 解释。
            v.reason = std::string{"wrote "} + std::string{why};
            return v;

        case sandbox_cfg::HandoffPolicy::Refuse:
            break;
    }

    v.allowed = false;
    // 给 MODEL 和用户一样:只写着的拒绝
    // "no" 重新考了,然后... 重试 安全门上的
    // 循环比结果都差。将机制命名,替代则将循环一转
    // 结束。
    v.reason =
        "refused: " + std::string{why} + ". The sandbox confines this process, "
        "but a file the host later executes escapes it by being run outside. "
        "Write the content somewhere inert and ask the user to wire it up, or "
        "change Trust handoff in the Sandbox pane.";
    return v;
}

Verdict check(std::string_view path, std::string_view tool) {
    // `--sandbox off` 关掉这个也关了。
    //
    // 门不是 沙箱 WALL (它住在工具上) 调用 ,§14),
    // 但它是同一控制的一部分,并且是一个已禁用 沙箱隔离 拒绝
    // 他们写作。 "off" 一定要说"离开",或者"离开"
    // 标志 是一个谎言 在另一个方向上—— 同样的诚实规则
    // 这个子系统的其他部分是建立在。
    //
    // 检查通过requested_mode()而不是
    // is_active():一个没有可用后端的主机仍然得到大门,
    // 因为这个主机是FAILED到沙箱,而不是一个要求不要的用户。
    // 这些答案不同,把它们拼凑起来,会悄悄地把大门落在机器上,
    // 并用其他墙壁最少的。
    if (sandbox::requested_mode() == sandbox::Mode::Off) return {};

    // 读取SEALED配置,所以政策不能在运行的转弯下
    // 改变——这里其他墙壁都有同样的保证.
    return check_with(path, tool, sandbox::config_snapshot()->handoff);
}

std::vector<sandbox_cfg::TrustHandoff> handoff_feed() {
    return feed().get();
}

void clear_handoff_feed() {
    feed().store({});
}

// ── 检测 shell 路径 ──────────────────────────────────

namespace {

// 主任 快照 将不降入。
//
// 故意的 不 隐藏的行走列表 kNeverWalk 列表,
// 区别在于:该列表跳过.git,因为它没有秘密,而.git是最重要的
// 事物之一 WATCH 此处( E) .git/config
// 携带一个 凭据 助手, .git/hooks 运行在下一个任务上。
// 这里跳过的部分只是无法 合理包含我们所关心的可信路径的批量 -- -
// - 构建输出和依赖性 源码树. node_modules 虽已跳
// 过 `node_modules/.bin` 是一个值得信赖的形状:
// 走它比探测值值还贵, 并且通过. package.json ,
// 哪个 IS 看着
constexpr std::string_view kSkipDirs[] = {
    "node_modules", "target", "build", "dist", "out", "__pycache__",
    ".mypy_cache", ".pytest_cache", ".cache", ".next", ".gradle", ".tox",
    "_deps", "CMakeFiles",
};

// 构建 源码树 名称众多( 构建) -rel ,构建 -tsan ,
// 制作( 制作) -build-debug、测试、模糊。 prefix/suffix
// 接通通用拼写和 a CMakeCache.txt 其余的,这些记录中,
// 有7500条 agentty 仓库 ,每张壳 调用.
// 一旦挂载了 CMakeCache.txt 的目录显示, 也跳过它。
[[nodiscard]] bool skip_name(std::string_view name) {
    if (std::ranges::find(kSkipDirs, name) != std::ranges::end(kSkipDirs)) return true;
    return name.starts_with("build") || name.starts_with("cmake-build")
        || name.ends_with("build");
}

// 在.git目录里,只有这些树种值得观看.
//
// 测量于 agentty 仓库 :没有这个,.git在1824年捐了1605元.
// "trusted" 路径——几乎所有的都是对象存储,打包的参考和日志.
// 电话 ShapeRule .git是一个 COMPONENT 相配,
// 所以每个小块 .git/objects/ 读作信任, 且没有这样的
// 内容: git 执行 hooks 和 荣誉配置, 它不运行自己的对象数据库。
// 监视 1605 惰性 文件以捕捉到 3 , 便是检测器如何变成噪音,
// 而噪音是安全特性如何被忽视。
//
// 实际所有的东西都是主机工具 RUNS . Git住在其中的一间里
constexpr std::string_view kGitWatch[] = {"hooks", "info"};

// 已经 `p` 在一个小的 dir, 如果是这样,它是一个部分 我们关心?
// `is_dir` 表示 p 本身是否是一个目录: a FILE 直接在 git dir
// ( 配置, ) FETCH_HEAD 被监视,但那里的目录(对象,日志)是噪音,
// 除非它 hooks/ 或 或 时 间 info/.
//
// A 子模块 居于 QT 坐标: .git/modules/<name>[/
// modules/<name> 并且布局相同,所以过滤器也应用在那里.
[[nodiscard]] bool git_noise(const fs::path& p, bool is_dir = false) {
    auto it = std::find(p.begin(), p.end(), fs::path{".git"});
    if (it == p.end()) return false;
    auto next = std::next(it);
    // 步入( 脚踏车) modules/<name> 双对最内侧的基特迪尔。
    while (next != p.end() && *next == "modules" && std::next(next) != p.end()) {
        it = std::next(next);
        next = std::next(it);
    }
    if (next == p.end()) return false;                    // 基底图本身
    if (*next == "modules") return false;                // 导致 子模块 基特 dirs
    if (std::next(next) == p.end() && !is_dir) return false;   // 里面的文件( A)
    const std::string seg = next->string();
    return std::ranges::find(kGitWatch, seg) == std::ranges::end(kGitWatch);
}

// agentty 数据在 . agentty (线程 ,日志,。。 没有任何东西可以被
// 主机运行, 还有。 ~/.agentty/threads 单凭一个人就能保存上千个文件.
constexpr std::string_view kAgenttyData[] = {"threads", "logs", "cache", "state", "sessions"};

[[nodiscard]] bool agentty_noise(const fs::path& dir) {
    if (dir.parent_path().filename() != ".agentty") return false;
    const std::string name = dir.filename().string();
    return std::ranges::find(kAgenttyData, name) != std::ranges::end(kAgenttyData);
}

// 深度盖。一个可信任的路径很重要,因为 HOST 工具查找和运行,
// 而做此操作的工具(编辑器、git、prenv、
// 构建驱动程序)则看起来接近根部。 源码树 从弹壳
// 转动 调用 进入一个完整的行走。
constexpr int kMaxDepth = 8;

// 输入扫描后,我们才能停下来。 快照 运行在每一个外壳
// 上 调用 如此无限制的行走 将是一个巨大的 仓库
// 每一扇门都要付钱 击中门会降低探测率 调用 而不是
// 拖延转弯-- 记录下来, 因为少静静的扫描是那种
// 使墙静静地停止工作的东西。
constexpr std::size_t kMaxEntries = 20000;

// 需要从一个目录中走出来的东西。 它中的所有文件都只取决于条目
// 名称及其路径, 所以它仍然有效, 而目录的 mtime 却在
// 其中( 添加、 删除或重命名一个条目会撞到它 )。
// 信任的文件仍然在每 快照 上被重新命名。
struct Listing {
    struct Sub {
        std::string name;
        bool link  = false;   // 共线目录: 从未降
        bool noise = false;   // git_noise:从未走过
        bool root  = false;   // 可以在其中显示新信任的文件
    };
    struct File { std::string path; sandbox_cfg::TrustKind kind{}; };
    fs::file_time_type mtime{};
    std::size_t entries = 0;
    bool has_cmake_cache = false;
    std::vector<Sub> dirs;
    std::vector<File> trusted;   // 信任的-shaped 文件, git 噪声已丢弃
};
using ListingPtr = std::optional<maya::shared<Listing>>;

using ListingCache = std::unordered_map<std::string, maya::shared<Listing>>;
// A 锁-free 快照 而不是一张有守护的地图:
// snapshot_trusted ()从产卵路径( 沙箱 姿态),无 锁 能力。
// 单程 加载 地图 一次 ,收集它不得不(重新)在私人批次中建立的清单,
// 并单份公布批次 update() 写在结尾,
// 所以副本 -on-write 成本一旦每个行走发现变化,就支付,
// 而不是每个目录。
maya::snapshot<ListingCache>& listing_cache() {
    static maya::snapshot<ListingCache> c;
    return c;
}
struct ListingWalk {
    maya::shared<ListingCache> seen = listing_cache().load();
    ListingCache               fresh;
    ~ListingWalk();
};
constexpr std::size_t kMaxCachedDirs = 200000;
// 近来更改了此目录, 在同一 mtime 勾选内可能再次更改,
// 因此其列表没有缓存( git 的 "racy" 规则)。
constexpr auto kRacyWindow = std::chrono::seconds{2};

[[nodiscard]] bool is_trusted_dir(const std::string& p) {
    sandbox_cfg::TrustKind k{};
    return sandbox_cfg::is_host_trusted(p + "/probe", &k);
}

// 清单 `dir` ,从 缓存 时间没有动过,
// 一个点球就打 锁 被拖动文件系统时按住。
ListingPtr list_dir(ListingWalk& walk, const fs::path& dir) {
    std::error_code ec;
    const auto mtime = fs::last_write_time(dir, ec);
    if (ec) return std::nullopt;
    std::string key = dir.string();
    ListingPtr hit;
    if (auto it = walk.seen->find(key); it != walk.seen->end()) hit = it->second;
    if (hit && (*hit)->mtime == mtime) return hit;

    Listing built;
    Listing* l = &built;
    l->mtime = mtime;
    fs::directory_iterator it{dir, fs::directory_options::skip_permission_denied, ec};
    if (ec) return std::nullopt;
    while (it != fs::directory_iterator{}) {
        const fs::directory_entry& e = *it;
        ++l->entries;
        std::error_code tec;
        if (e.is_directory(tec)) {
            const fs::path& p = e.path();
            l->dirs.push_back({p.filename().string(), e.is_symlink(tec),
                               git_noise(p, true) || agentty_noise(p),
                               is_trusted_dir(p.string())});
        } else {
            const fs::path& p = e.path();
            if (p.filename() == "CMakeCache.txt") l->has_cmake_cache = true;
            std::string ps = p.string();
            sandbox_cfg::TrustKind kind{};
            if (sandbox_cfg::is_host_trusted(ps, &kind) && !git_noise(p))
                l->trusted.push_back({std::move(ps), kind});
        }
        it.increment(ec);
        if (ec) {
            AGT_LOG(General, Warn, "handoff", "listing {} aborted: {}",
                    key, ec.message());
            return maya::shared<Listing>::make(std::move(built));   // 部分: 从未缓存
        }
    }
    auto frozen = maya::shared<Listing>::make(std::move(built));
    if (fs::file_time_type::clock::now() - mtime >= kRacyWindow)
        walk.fresh.insert_or_assign(std::move(key), frozen);
    return frozen;
}

// 公布此行所建的。 一个销毁器, 这样每一条返回路径 snapshot_trusted
// () 都会保留它所学到的东西。
ListingWalk::~ListingWalk() {
    if (fresh.empty()) return;
    (void)listing_cache().update(
        [](const ListingCache& old, const ListingCache& add) {
            ListingCache m;
            if (old.size() + add.size() <= kMaxCachedDirs) m = old;
            for (const auto& [k, v] : add) {
                if (m.size() >= kMaxCachedDirs) m.clear();
                m.insert_or_assign(k, v);
            }
            return m;
        }, std::move(fresh));
}

}  // 命名空间结束。

TrustedSnapshot snapshot_trusted(std::string_view root) {
    TrustedSnapshot snap;
    if (root.empty()) return snap;
    ListingWalk walk;

    // 允许手段 "do not gate" ,所以没有报告,也没有理由支付统计。
    // -out 以 check() ,包括: --sandbox 关机
    if (sandbox::requested_mode() == sandbox::Mode::Off) return snap;
    const auto cfg = sandbox::config_snapshot();
    if (cfg->handoff == sandbox_cfg::HandoffPolicy::Allow) return snap;

    std::error_code ec;
    fs::path base{root};
    if (!fs::is_directory(base, ec)) return snap;

    std::size_t seen = 0;
    bool truncated = false;
    std::unordered_set<std::string> already;

    // 记得我们走过的目录,所以 review() 能够重新
    // -scan 并注意一个不存在于 快照 时间
    //
    // ⊿硂 碞钡 MODIFICATIONS:
    // `echo x > .git/hooks/post-commit`
    // 创建品牌 -new 钩子是隐形的,这是更危险的那一半
    // 新鲜的钩子是经典的逃脱,空的 仓库 修补前已验证。
    snap.roots.push_back(base.string());

    // 记录一个信任的文件。 共享的是下面的优先级和一般行走,
    // 因此, 两人不能对条目的控件有分歧。
    auto record = [&snap, &already](const std::string& p, sandbox_cfg::TrustKind kind) {
        if (!already.insert(p).second) return;
        TrustedSnapshot::Entry entry;
        entry.path    = p;
        entry.kind    = kind;
        entry.existed = true;
        std::error_code sec;
        entry.size    = fs::file_size(p, sec);
        if (sec) entry.size = 0;
        std::error_code tec;
        const auto t = fs::last_write_time(p, tec);
        entry.mtime_ns = tec ? 0
            : std::chrono::duration_cast<std::chrono::nanoseconds>(
                  t.time_since_epoch()).count();
        snap.entries.push_back(std::move(entry));
    };

    // ── 优先级: 信任的DIRECTORIES, 在盖子可以咬之前 ──
    //
    // 下方的普通步行在 kMax Entries 上盖, 和 a 源码
    // 树 比那还大,它停止了早期。 recursive_directory_iterator
    // 已经没有命令了,所以 对应的 预算范围内的可信路径是任意的,
    // 在运行时可能有所不同 仓库 -- `.git/hooks/post-commit` ,
    // 整个大门的逃生地点,没有比一个卖家Makefile40k
    // 条目更难。墙的覆盖范围由步行命令决定,
    // 不是墙(#78)。
    //
    // 所以,我们... ShapeMatch::Component 规矩被行走
    // 首次 和未封装。这些是主机后期信任的全部目录 -- -- .git、.
    // vscode、.claude。 agentty . . ursor,.
    // idea,.direnv - 和它们都很小: 每人有几十个文件,
    // 对数万个单列波's 源码树 跑到那里,他们走得快,几乎一无所有,而且会变得很高
    // -risk 设置无条件。
    //
    // 断线仍然存在, 仍然可以丢掉一个深 Basename
    // 匹配( 一个销售商 Makefile)。
    // 这样做是正确的: 它属于有限度的工作, 并且将包含支柱系列中每个
    // 开发内容的目录在预算用完之前覆盖。
    //
    // 此处命名, 而非规则表的衍生, 因为表格是匹配的 SHAPES ,而不是地点,
    // 只有这些有 "small, entirely trusted, worth guaranteeing" 属性。
    // is_host_trusted () 是否路径数由
    // 单一权威决定 -- -- 名单只决定行走 ORDER.
    static constexpr std::string_view kPriorityDirs[] = {
        ".git", ".vscode", ".claude", ".agentty", ".cursor", ".idea",
        ".direnv", ".codex", ".gemini",
    };
    // 唯一存在于其中的路径的相同想法 $HOME ,
    // 需要两个组件来命名: 登录时运行或发现于 PATH 很小,
    // 最危险的是 失去在帽子上,当 工作区 ~。。 编号为~
    static constexpr std::string_view kPriorityHomeDirs[] = {
        ".config/fish", ".config/environment.d", ".config/autostart",
        ".config/systemd", ".config/hypr", ".config/sway", ".config/i3",
        ".config/plasma-workspace", ".local/bin",
    };
    const auto is_priority = [](std::string_view name) {
        return std::ranges::find(kPriorityDirs, name) != std::ranges::end(kPriorityDirs);
    };
    // 优先通行证被完全删除,下面的普通人跳过,
    // 或者记录他们的根部两次,然后再次走.git。
    std::unordered_set<std::string> walked;
    {
        // 查找优先目录, 并加载 SHALLOW 扫描(它们生活
        // 在或靠近 工作区 根在每一个真实布局中,然后全部行走。
        // 深度3深到可以在一个子-package中找到.vscode.
        std::vector<fs::path> targets;
        for (auto rel : kPriorityHomeDirs) {
            std::error_code hec;
            fs::path p = base / rel;
            if (fs::is_directory(p, hec) && !fs::is_symlink(fs::symlink_status(p, hec)))
                targets.push_back(std::move(p));
        }
        std::vector<std::pair<fs::path, int>> stack{{base, 0}};
        std::size_t pseen = 0;
        while (!stack.empty() && pseen <= kMaxEntries) {
            auto [dir, depth] = std::move(stack.back());
            stack.pop_back();
            const auto lp = list_dir(walk, dir);
            if (!lp) continue;
            const Listing* l = &**lp;
            if (depth > 0 && l->has_cmake_cache) continue;
            pseen += l->entries;
            for (const auto& s : l->dirs) {
                fs::path p = dir / s.name;
                if (is_priority(s.name)) { targets.push_back(std::move(p)); continue; }
                if (depth < 3 && !s.link && !skip_name(s.name))
                    stack.push_back({std::move(p), depth + 1});
            }
        }
        // 被解开,被打 .git/objects 朋友是它保持便宜。
        for (const auto& t : targets) {
            std::vector<fs::path> st{t};
            while (!st.empty()) {
                const fs::path d = std::move(st.back());
                st.pop_back();
                const auto lp = list_dir(walk, d);
                if (!lp) continue;
                const Listing* l = &**lp;
                for (const auto& f : l->trusted) record(f.path, f.kind);
                for (const auto& s : l->dirs) {
                    if (s.noise) continue;
                    fs::path p = d / s.name;
                    if (s.root) snap.roots.push_back(p.string());
                    if (!s.link) st.push_back(std::move(p));
                }
            }
        }
        if (!targets.empty())
            AGT_LOG(General, Debug, "handoff",
                    "priority pass covered {} trusted dir(s), {} entry(s)",
                    targets.size(), snap.entries.size());
        for (const auto& t : targets) {
            walked.insert(t.string());
            snap.roots.push_back(t.string());   // 直接在内部新建文件
        }
    }
    const std::size_t priority_count = snap.entries.size();

    // 普通行走,盖上。访问整个目录,所以每个目录
    // 检查盖上。
    std::vector<std::pair<fs::path, int>> stack{{base, 0}};
    while (!stack.empty()) {
        auto [dir, depth] = std::move(stack.back());
        stack.pop_back();
        const auto lp = list_dir(walk, dir);
        if (!lp) continue;
        const Listing* l = &**lp;
        if (depth > 0 && l->has_cmake_cache) continue;
        seen += l->entries;
        if (seen > kMaxEntries) { truncated = true; break; }
        for (const auto& f : l->trusted) record(f.path, f.kind);
        for (const auto& s : l->dirs) {
            if (skip_name(s.name) || s.noise) continue;
            fs::path p = dir / s.name;
            std::string ps = p.string();
            if (walked.contains(ps)) continue;
            // 一个值得信赖的人 -shaped 目录( D) .git/hooks ,vscode)是其中的一款
            // 用于用于计算. 新 信任的文件会出现, 所以 review() 内容 -lists 就是这样
            if (s.root) snap.roots.push_back(ps);
            if (depth < kMaxDepth && !s.link) stack.push_back({std::move(p), depth + 1});
        }
    }

    last_truncated().store(truncated);
    if (truncated)
        AGT_LOG(General, Warn, "handoff",
                "trusted snapshot truncated at {} entries under {} -- a write "
                "below the cut would not be reported (the {} high-risk "
                "trusted dir entries were covered first)",
                kMaxEntries, std::string{root}, priority_count);

    AGT_LOG(General, Debug, "handoff", "snapshot {} trusted path(s) under {}",
            snap.entries.size(), std::string{root});
    return snap;
}

bool last_snapshot_truncated() noexcept { return *last_truncated().load(); }

std::size_t review_trusted(const TrustedSnapshot& before, std::string_view tool) {
    if (before.empty()) return 0;

    std::size_t recorded = 0;
    for (const auto& e : before.entries) {
        std::error_code ec;
        const bool exists = fs::is_regular_file(e.path, ec);

        const char* what = nullptr;
        if (!exists) {
            what = "deleted";
        } else {
            std::error_code sec, tec;
            const auto sz = fs::file_size(e.path, sec);
            const auto t = fs::last_write_time(e.path, tec);
            const auto ns = tec ? std::int64_t{0}
                                : std::chrono::duration_cast<std::chrono::nanoseconds>(
                                      t.time_since_epoch())
                                      .count();
            // 大小 OR mtime: 相同时间 -length
            // 重写是只检查大小的个案, 翻转一个字符在钩脚本中
            // 就是那样。
            if ((!sec && sz != e.size) || ns != e.mtime_ns) what = "modified";
        }
        if (!what) continue;

        sandbox_cfg::TrustHandoff h;
        h.kind = e.kind;
        h.write.path = e.path;
        // 命令,而不是工具名称 : "shell" 告诉用户什么,
        // 和 WriteRecord 存在以回答 "who asked"。
        // 这就是不透明字符串真正有用的地方 -- -- 而不是决定,这就是为什么
        // 解析 被否决了,但是... EXPLAINING 事实之后。
        h.write.command = std::string{tool};
        h.write.at_ms = now_ms();
        h.write.created = !e.existed;
        remember(h);
        ++recorded;

        AGT_LOG(General, Warn, "handoff",
                "{} a host-trusted path outside the gate: {} ({})",
                what, e.path, sandbox_cfg::explain(e.kind));
    }

    // 第二传:我们快照时不存在的可信文件.
    //
    // `entries` 只能报告它已经看到的道路,
    // 所以没有这个门 抓住一个覆盖的钩子,错过一个品牌 -new
    // 一种是更容易和危险的动作, 和用同样的方式 快照 即:
    // 只有它实际走过的目录,不是 -recursively ,因此,
    // 这需要每个信任的读取器 -shaped 目录而非另一个 源码树 走着走着吧
    std::vector<std::string> known;
    known.reserve(before.entries.size());
    for (const auto& e : before.entries) known.push_back(e.path);
    std::ranges::sort(known);

    for (const auto& dir : before.roots) {
        std::error_code dec;
        fs::directory_iterator dit{
            dir, fs::directory_options::skip_permission_denied, dec};
        if (dec) continue;
        for (; dit != fs::directory_iterator{}; ) {
            std::error_code fec;
            if (dit->is_regular_file(fec) && !fec) {
                const std::string p = dit->path().string();
                if (!std::ranges::binary_search(known, p)) {
                    sandbox_cfg::TrustKind kind{};
                    if (sandbox_cfg::is_host_trusted(p, &kind)) {
                        sandbox_cfg::TrustHandoff h;
                        h.kind = kind;
                        h.write.path = p;
                        h.write.command = std::string{tool};
                        h.write.at_ms = now_ms();
                        h.write.created = true;
                        remember(h);
                        ++recorded;

                        AGT_LOG(General, Warn, "handoff",
                                "created a host-trusted path outside the gate: {} ({})",
                                p, sandbox_cfg::explain(kind));
                    }
                }
            }

            std::error_code iec;
            dit.increment(iec);
            if (iec) {
                AGT_LOG(General, Warn, "handoff",
                        "trusted review walk aborted under {}: {}",
                        dir, iec.message());
                break;
            }
        }
    }
    return recorded;
}

}  // namespace agentty::tools::util::handoff
