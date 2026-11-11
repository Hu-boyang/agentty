// agentty::dirs——write-side存储原语。
// 请参阅 dirs.hpp 了解合同以及其形状像示波器的原因。
//
// 该文件是不纯的边缘：它读取环境、统计磁盘并创建目录。
// Spec/Lifecycle 类型保持纯净且可测试。

#include "agentty/dirs/dirs.hpp"

#include <maya/runtime.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "agentty/tool/util/fs_helpers.hpp"
#include "agentty/util/logx.hpp"
#include "agentty/io/shared_file.hpp"
#include "agentty/util/user_root.hpp"

namespace agentty::dirs {

namespace {

// 标识项目源码树的标记。首先是 `.git`，
// 因为这是极其常见的情况，并且步行在第一次点击时停止，
// 因此顺序是一种廉价的延迟胜利，而不是关于重要性的声明。
//
// 故意包括`.agentty`：已经具有agentty状态
// IS锚点的源码树，这使得锚点在步骤2中的整个迁移过程
// 中保持稳定（不断发现现有的<cwd>/.agentty），
// 而不是搁浅扫描随后将收集的文件。
constexpr std::array<std::string_view, 4> kMarkers{
    ".git", ".agentty", ".hg", ".svn",
};

// "/"(和"C:\")是agentty的不受限制的-access-boundary哨兵，
// 绝不是分散状态的地方。与 scope::usable_project_root
// () 完全匹配；两者必须一致，否则作用域可读的路径在这里将变得不可写。
[[nodiscard]] bool usable_project_root(const fs::path& p) noexcept {
    if (p.empty()) return false;
    return p != p.root_path();
}

[[nodiscard]] bool dir_is_writable(const fs::path& p) noexcept {
    std::error_code ec;
    if (!fs::is_directory(p, ec)) return false;
#if defined(_WIN32)
    return true;  // 没有便宜的W_OK;写入本身表面失败
#else
    return ::access(p.c_str(), W_OK) == 0;
#endif
}

// 每个变量最多警告一次，process-wide。
//
// 每 调用 重复一次比沉默更糟糕：这些解析器是
// 从 渲染 和检索路径调用的，因此损坏的覆盖将
// 打印在每个 帧 上并埋葬重要的一行。
//
// maya::snapshot，而不是锁：这些解析器从渲染
// 路径以及从其他没有手头能力的锁'调用者内部运行，
// 并且该集合只会不断增长。 锁-free read-mostly
// 值完全符合这一点。两个 线程 在同一时刻第一次触及相同的
// 损坏超控可能会同时打印 - 一条重复的警告线，永远不会丢失。
void warn_once(std::string_view env, const fs::path& bad,
               const fs::path& fallback, const std::error_code& ec) {
    using Seen = std::set<std::string, std::less<>>;
    static maya::snapshot<Seen> warned;
    if (warned.load()->contains(env)) return;
    // 无捕获和拥有-value参数：update()可以重试。
    (void)warned.update([](const Seen& old, const std::string& k) {
        Seen next = old;
        next.insert(k);
        return next;
    }, std::string{env});

    // 当路径已经作为非 -directory 存在时，
    // create_directories()留下 `ec` CLEAR—
    // —它没有失败，它没有任何关系——所以报告 ec 在这里打印
    // "Undefined error: 0" 作为单个最可能的错误（将覆盖指向文件）。
    // 直接命名该案例并保留其他所有内容的 errno 文本。
    // 继承自user_root的resolve_subdir，历经千辛万苦才学会。
    std::error_code exists_ec;
    const bool in_the_way = fs::exists(bad, exists_ec);
    // 将消息保存在命名字符串中：ec.message()返回BY 值，
    // 因此在三元数内部的临时变量上调用.c_str()会在full
    // -expression结束时留下悬空指针。
    const std::string ec_text = ec ? ec.message() : std::string{"cannot create"};
    const std::string why = in_the_way ? std::string{"exists but is not a directory"}
                                       : ec_text;

    std::fprintf(stderr,
                 "agentty: warning: $%.*s='%s' is not usable (%s) — falling back to %s\n",
                 static_cast<int>(env.size()), env.data(),
                 bad.string().c_str(), why.c_str(), fallback.string().c_str());

    AGT_LOG(Persist, Warn, "dirs",
            "override ${} unusable ({}): {} -> falling back to {}",
            env, why, bad.string(), fallback.string());
}

// 8个十六进制字符标识路径，用于将两个项目分开在一个
// 覆盖目录下。
//
// FNV-1a而不是std::hash：这落在PERSISTED
// 目录名中，并且std::hash仅需要在一个程序的一次运行内稳定。
// libstdc++/libc++差异将默默地孤立每个索引。
[[nodiscard]] std::string path_tag(const fs::path& p) {
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char c : p.string()) {
        h ^= c;
        h *= 1099511628211ull;
    }
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx",
                  static_cast<unsigned long long>(h));
    return std::string{buf, 8};
}

// 解决Spec挂起的根源，而不触及`leaf`。
[[nodiscard]] std::expected<fs::path, Error> root_for(Root r) {
    switch (r) {
        case Root::User: {
            fs::path p = ::agentty::util::user_root();
            if (p.empty())
                return std::unexpected(Error{Error::Kind::NoRoot,
                                             "no user root (HOME unresolvable)"});
            return p;
        }
        case Root::Project: {
            fs::path a = project_anchor();
            if (a.empty())
                return std::unexpected(Error{Error::Kind::NoRoot,
                                             "no usable project anchor"});

            // $AGENTTY_PROJECT_DIR移动此根批发，
            // $AGENTTY_HOME移动用户方式一。
            if (const char* v = std::getenv("AGENTTY_PROJECT_DIR"); v && *v) {
                fs::path given{v};
                // 相对解析针对锚点，而不是针对cwd：每个检出
                // 一个目录，而不是每个启动目录一个目录。
                if (given.is_relative()) given = a / given;
                // 一次覆盖，许多项目——因此给每个项目提供其
                // 自己的子目录。如果没有这个，两个签出会落在相同
                // 的文件名上，索引元的根保护会将其变成每个开关上
                // 的完整 re-index (#61)。
                // 基本名称保持可读，散列消除歧义。
                return given / (a.filename().string() + "-" + path_tag(a));
            }

            if (!dir_is_writable(a))
                return std::unexpected(Error{Error::Kind::NotWritable,
                                             "project anchor is not writable: "
                                                 + a.string()});
            return a / ".agentty";
        }
    }
    return std::unexpected(Error{Error::Kind::NoRoot, "unknown root"});
}

// 共享核心。 `create` 区分 resolve() 和 resolve_dry()。
[[nodiscard]] std::expected<Resolved, Error> resolve_impl(const Spec& spec,
                                                          bool create) {
    auto root = root_for(spec.root);
    if (!root) return std::unexpected(root.error());

    // EMPTY叶意味着"the root itself"——拥有其自己的文件名
    // （rag附加rag_docs.<tag>.ragdb）并且想要该目录的调用者。
    //
    // 不得经过operator/：加入空路径会附加尾随分隔符，
    // 因此`root / ""`将UNEQUAL与`root`
    // 进行比较，即使它命名相同的目录。在比较两个解析路径之前，
    // 这种不平等是不可见的，这正是 "did this move?"
    // 检查的作用。
    const fs::path dflt = spec.leaf.empty() ? *root
                                            : *root / fs::path{spec.leaf};

    // 设定时$env获胜，NON-EMPTY获胜。 Export-but-blank
    // 变量是常见的 shell 事故，必须不重定位任何内容。
    fs::path chosen = dflt;
    bool overridden = false;
    if (!spec.env.empty()) {
        // getenv需要NUL-terminated字符串；规范有观点。
        const std::string env_name{spec.env};
        if (const char* v = std::getenv(env_name.c_str()); v && *v) {
            fs::path given{v};
            // RELATIVE覆盖针对ROOT进行解析，
            // 进程CWD:AGENTTY_LOGS_DIR=logs2
            // 必须意味着一个目录，而不是每个启动目录的不同目录。
            chosen = given.is_absolute() ? std::move(given) : *root / given;
            // PROJECT数据的绝对覆盖由每个项目共享，
            // 因此每个项目都有其自己的子文件夹，
            // 与$AGENTTY_PROJECT_DIR的方式相同。否则，
            // 两个 仓库 会争夺一个索引文件，并在每台交换机上重建它。
            if (spec.root == Root::Project && fs::path{v}.is_absolute()) {
                const fs::path a = project_anchor();
                if (!a.empty()) chosen /= a.filename().string() + "-" + path_tag(a);
            }
            overridden = true;
        }
    }

    Resolved out;
    out.root = *root;
    out.path = chosen;
    out.origin = overridden ? Origin::Override : Origin::Default;

    if (!create) {
        // PREDICT创建路径会做什么，不做的话。
        //
        // 这种提前返回过去常常跳过下面的每一个可行性检查，因此即使当
        // $AGENTTY_HOME 命名为 FILE——一条永远不会存在的路径时，
        // 干燥的解决方案也会愉快地报告 `$AGENTTY_HOME/threads`。
        // 作者警告"will not persist this session"并后退；
        // 观察者称这条破损的道路是事实。
        //
        // 这个差距是`agentty config`存在
        // 值得信赖的全部原因：如果描述程序的工具不同意程序，
        // 那么它就毫无价值。因此，以便宜的方式镜像决策——
        // 使用存在探测而不是 mkdir。
        // 当路径已经是目录时，路径是可创建的，当最近的EXISTING祖先是目录时，
        // OR是可创建的。仅检查 `chosen` 本身是不够的：
        // `$AGENTTY_HOME=/tmp/afile` 会使
        // `/tmp/afile/threads` 不存在，读作 "fine, we'll make it"，
        // 而 mkdir 会在其路径中的文件上失败。
        auto creatable = [](fs::path p) {
            std::error_code ec;
            if (fs::is_directory(p, ec)) return true;
            if (fs::exists(p, ec))      return false;   // 存在，不是目录
            for (fs::path up = p.parent_path(); !up.empty();
                 up = up.parent_path()) {
                if (fs::is_directory(up, ec)) return true;
                if (fs::exists(up, ec))       return false;  // FILE挡住了
                if (up == up.parent_path())   break;
            }
            return false;
        };
        if (!creatable(chosen)) {
            if (!overridden)
                return std::unexpected(Error{Error::Kind::Unusable,
                                             "cannot create " + chosen.string()});
            // 作者采用相同的回退，以相同的方式报告，
            // 因此呼叫者打印的起源与其真正获得的起源相匹配。
            out.path   = dflt;
            out.origin = Origin::OverrideFellBack;
            if (!creatable(dflt))
                return std::unexpected(
                    Error{Error::Kind::Unusable,
                          "neither override nor default usable: " + dflt.string()});
        }
        return out;
    }

    std::error_code ec;
    fs::create_directories(chosen, ec);

    if (!fs::is_directory(chosen, ec)) {
        if (!overridden) {
            // DEFAULT无法使用。没有什么可回退的，
            // 所以这是一个硬错误而不是警告——返回一条我们知道不
            // 起作用的路径是调用者最终从其他地方完全报告
            // "Undefined error: 0" 的原因。
            return std::unexpected(Error{Error::Kind::Unusable,
                                         "cannot create " + chosen.string()});
        }
        warn_once(spec.env, chosen, dflt, ec);
        chosen = dflt;
        std::error_code dec;
        fs::create_directories(chosen, dec);
        if (!fs::is_directory(chosen, dec))
            return std::unexpected(Error{Error::Kind::Unusable,
                                         "neither override nor default usable: "
                                             + dflt.string()});
        out.path = chosen;
        out.origin = Origin::OverrideFellBack;
    }

#ifndef _WIN32
    // 默认从根继承0700；覆盖可以指向任何地方，
    // 包括用户已经创建的 0755 目录。
    // 这是必须显式设置该位而不是假定的位置。
    if (spec.owner_only) ::chmod(chosen.c_str(), S_IRWXU);
#endif

    // 当我们创建它时，将 PROJECT 根设为 self-ignoring 一次。
    //
    // <project>/.agentty保存派生状态——在这个仓库中的
    // 33MB检索索引——紧邻用户的源码树。 agentty 自己的 .
    // gitignore 列出了它，但这并没有保护任何其他人：在任何
    // OTHER 签出中，在热索引暂存数十兆字节的 二进制程序 之后，
    // 第一个 `git add -A` 是作者在审查时发现的。
    //
    // 将规则INSIDE传送到目录使其成为结构化的而不是
    // 每个用户必须知道要写的东西。该文件会忽略它自己的目录，
    // 因此它也是 git 可以看到的唯一内容。
    //
    // Best-effort 并且从未被覆盖：故意提交 .
    // agentty 的一部分（共享 skills/ 目录是真正的用途）
    // 的用户编辑此文件，并且每次启动时重写它的工具将是一个争论的工具。
    if (spec.root == Root::Project && !out.root.empty()) {
        // 仅在不存在的情况下创建（独占创建，因此两个一起启动的
        // agentty 不能一半 -write 或破坏用户的）。
        const fs::path ignore = out.root / ".gitignore";
        std::error_code gec;
        if (!fs::exists(ignore, gec))
            (void)persistence::detail::create_in_section(ignore.string(),
                "# agentty's project state. Derived data (retrieval indexes, caches)\n"
                "# lives here and should not be committed.\n"
                "# Delete a line to start tracking something; agentty won't rewrite this.\n"
                "*\n", 0644);
    }

    AGT_LOG(Persist, Debug, "dirs", "resolved {} -> {} ({})",
            spec.leaf, out.path.string(),
            out.origin == Origin::Default         ? "default"
            : out.origin == Origin::Override      ? "override"
                                                  : "override-fell-back");
    return out;
}

}  // 命名空间结束。

std::string_view to_string(Error::Kind k) noexcept {
    switch (k) {
        case Error::Kind::NoRoot:      return "no-root";
        case Error::Kind::NotWritable: return "not-writable";
        case Error::Kind::Unusable:    return "unusable";
    }
    return "unknown";
}

std::span<const std::string_view> project_markers() noexcept {
    return {kMarkers.data(), kMarkers.size()};
}

fs::path project_anchor() {
    // 从 project_root() 开始——cwd，
    // 已经被夹在访问边界内——所以行走永远不能在 沙箱 之外开始。
    fs::path start = ::agentty::tools::util::project_root();
    if (start.empty()) return {};

    std::error_code ec;
    fs::path cur = fs::weakly_canonical(start, ec);
    if (ec) cur = start;

    // 走到最近的标记处。在根路径处停止，因此根本没有标记
    // 的 源码树 会落入夹紧的 cwd，
    // 而不是逃逸到 "/"。
    for (fs::path p = cur; usable_project_root(p); p = p.parent_path()) {
        for (std::string_view m : kMarkers) {
            std::error_code mec;
            if (fs::exists(p / fs::path{m}, mec)) {
                AGT_LOG(Persist, Debug, "dirs", "anchor {} (marker {})",
                        p.string(), m);
                return p;
            }
        }
        if (p == p.parent_path()) break;  // 防守：无无限循环
    }

    // 我们上方任何地方都没有标记。回到固定的 cwd，
    // 这是每个消费者在存在之前所做的事情 - 因此无标记的
    // 源码树 的行为与今天完全相同，而不是拒绝存储。
    if (!usable_project_root(cur)) return {};
    AGT_LOG(Persist, Debug, "dirs", "anchor {} (no marker, cwd fallback)",
            cur.string());
    return cur;
}

std::expected<Resolved, Error> resolve(const Spec& spec) {
    return resolve_impl(spec, /*创建=*/true);
}

std::expected<Resolved, Error> resolve_dry(const Spec& spec) {
    return resolve_impl(spec, /*创建=*/false);
}

SweepStats sweep(const SweepRequest& req, bool dry_run) {
    SweepStats st;

    // 大门。无论 keep_last 说什么，不可重建的商店永远不会被
    // 清扫——请参阅 Lifecycle::rebuildable 了解
    // 为什么不对称是一个领域而不是惯例。
    if (!req.spec.life.sweeps()) return st;
    if (req.stem.empty() || req.suffix.empty()) return st;

    // resolve_dry:扫掠不得CREATE它正在清理的目录。
    auto d = resolve_dry(req.spec);
    if (!d) return st;

    std::error_code ec;
    if (!fs::is_directory(d->path, ec)) return st;

    const auto now = fs::file_time_type::clock::now();

    struct Candidate {
        fs::path path;
        fs::file_time_type mtime;
        std::uintmax_t size = 0;
    };
    std::vector<Candidate> cands;
    // 遗留的未加标签的文件被跟踪SEPARATELY，
    // 不与加标签的变体一起排名。将它们放在同一个列表中，
    // 让 keep_last 保护一个 - 由于旧文件通常是目录
    // 中的最新文件，因此它赢得了位置，而 live-adjacent
    // 变体 则被删除。它们无法被任何当前的代码路径读取，
    // 因此没有什么可排名的：它们总是会发生。
    std::vector<Candidate> legacy;

    // 未加标签的旧名称："rag_code."+".ragdb"->"rag_code.ragdb"。
    std::string untagged;
    if (req.include_untagged) {
        std::string s{req.stem};
        if (!s.empty() && s.back() == '.') s.pop_back();
        untagged = s + std::string{req.suffix};
    }

    fs::directory_iterator it{d->path, fs::directory_options::skip_permission_denied, ec};
    if (ec) {
        // 甚至无法打开该目录。报告并删除任何内容。
        st.failed = 1;
        return st;
    }

    for (const auto& entry : it) {
        std::error_code eec;
        if (!entry.is_regular_file(eec) || eec) {
            if (eec) ++st.failed;
            continue;
        }
        const std::string name = entry.path().filename().string();

        bool is_variant = name.size() > req.stem.size() + req.suffix.size()
                       && name.starts_with(req.stem) && name.ends_with(req.suffix);
        const bool is_legacy = !untagged.empty() && name == untagged;
        if (!is_variant && !is_legacy) continue;

        ++st.examined;

        // 活变体无条件生存。
        bool live = false;
        for (const auto& k : req.keep)
            if (name == k) { live = true; break; }
        if (live) { ++st.kept; continue; }

        std::error_code tec;
        auto mt = fs::last_write_time(entry.path(), tec);
        std::error_code sec;
        auto sz = fs::file_size(entry.path(), sec);
        if (tec || sec) { ++st.failed; continue; }

        // 宽限窗。另一个进程可能是 mid-save，
        // 并且并发进程可能合法地运行不同的配置。
        // 遗留的未标记文件是豁免的：当前代码路径甚至不能命名它，
        // 因此不会有任何东西要写入它。
        if (!is_legacy && req.spec.life.min_age.count() > 0
            && now - mt < req.spec.life.min_age) {
            ++st.too_new;
            ++st.kept;
            continue;
        }

        cands.push_back({entry.path(), mt, sz});
        if (is_legacy) {
            legacy.push_back(cands.back());
            cands.pop_back();
        }
    }

    // 任何不可读的内容意味着我们对目录的看法是不完整的，因此我们不能说
    // 哪个变体是最新的。 Delete 什么也没有。与 blobs::gc
    // 相同的规则：对当前内容的错误会导致磁盘成本，而不是数据成本。
    if (st.failed > 0) {
        st.kept += cands.size() + legacy.size();
        AGT_LOG(Persist, Warn, "dirs",
                "sweep of {} aborted: {} unreadable entr{}",
                d->path.string(), st.failed, st.failed == 1 ? "y" : "ies");
        return st;
    }

    // 最新的在前，然后保留N个最新的。 keep_last 计数
    // 保留在活动索引之外的 VARIANTS，这就是使两个嵌入器之间
    // 的 A/B 空闲的原因：切换回来并且之前的索引仍然是热的。
    std::sort(cands.begin(), cands.end(),
              [](const Candidate& a, const Candidate& b) { return a.mtime > b.mtime; });

    const std::size_t keep_n = std::min<std::size_t>(req.spec.life.keep_last,
                                                     cands.size());
    st.kept += keep_n;

    // 注定=每个旧文件的标记变体超出keep_last、PLUS。
    std::vector<Candidate> doomed;
    doomed.reserve(cands.size() - keep_n + legacy.size());
    for (std::size_t i = keep_n; i < cands.size(); ++i) doomed.push_back(cands[i]);
    for (const auto& l : legacy) doomed.push_back(l);

    for (const auto& c : doomed) {
        // Sidecars 遵循原则：描述已消失的文件的 .
        // meta.json 是每个加载器必须防御的陷阱。
        std::uintmax_t freed = c.size;
        std::vector<fs::path> victims{c.path};
        for (std::string_view sfx : req.sidecar_suffixes) {
            fs::path side{c.path.string() + std::string{sfx}};
            std::error_code xec;
            if (fs::is_regular_file(side, xec)) {
                std::error_code zec;
                freed += fs::file_size(side, zec);
                victims.push_back(std::move(side));
            }
        }

        if (dry_run) {
            ++st.deleted;
            st.bytes_freed += freed;
            AGT_LOG(Persist, Debug, "dirs", "sweep (dry) would remove {}",
                    c.path.filename().string());
            continue;
        }

        bool all_gone = true;
        for (const auto& v : victims) {
            std::error_code rec;
            fs::remove(v, rec);
            if (rec) all_gone = false;
        }
        if (all_gone) {
            ++st.deleted;
            st.bytes_freed += freed;
            AGT_LOG(Persist, Info, "dirs", "sweep removed {} ({} bytes)",
                    c.path.filename().string(), freed);
        } else {
            ++st.failed;
        }
    }

    if (st.deleted > 0)
        AGT_LOG(Persist, Info, "dirs",
                "sweep {}: examined {} kept {} deleted {} ({} bytes)",
                dry_run ? "dry-run" : "done",
                st.examined, st.kept, st.deleted, st.bytes_freed);
    return st;
}

}  // namespace agentty::dirs
