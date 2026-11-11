// config/inventory.cpp — 解析并打印清单。
//
// 这里的所有内容归并、SAME发挥真正的解析器归并的作用：
// scope::plan为读梯形图，dirs::resolve_dry为写路径。
// 没有任何 re-derives 位置，这使得输出比第二意见更
// 值得信赖。参见 inventory.hpp 的参数。

#include "agentty/config/inventory.hpp"

#include "agentty/util/home_dir.hpp"
#include "agentty/util/logx.hpp"
#include "agentty/util/storage_lock.hpp"

#if !defined(_WIN32)
#include <unistd.h>
#endif
#if AGENTTY_MCP
#include "agentty/mcp/import.hpp"   // 外部配置，以其修复命名
#endif

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <variant>

namespace agentty::config {

namespace fs = std::filesystem;

namespace {

// 库存。每行指向AT中的常量头文件；
// 它从不复制一个定义，因此每个关注点只有一个定义。
constexpr std::array<Entry, 12> kEntries{{
    {"mcp",      "MCP servers",          &kMcpLayout,      nullptr},
    {"skills",   "skills",               &kSkillsLayout,   nullptr},
    {"agents",   "subagent definitions", &kAgentsLayout,   nullptr},
    {"commands", "slash commands",       &kCommandsLayout, nullptr},
    {"memory",   "learned memory",       &kMemoryLayout,   nullptr},
    {"threads",  "conversation history", nullptr,          &kThreadsSpec},
    {"credentials", "sign-ins and keys",  nullptr,          &kCredentialsSpec},
    {"state",    "approvals",            nullptr,          &kStateSpec},
    {"cache",    "refetchable caches",   nullptr,          &kCacheSpec},
    {"logs",     "diagnostics",          nullptr,          &kLogsSpec},
    {"rag",      "retrieval indexes",    nullptr,          &kRagSpec},
    {"feedback", "retrieval learning",   nullptr,          &kFeedbackSpec},
}};

// 全方言乘积，用于计算特征SKIPS。整套唯一拼写的地方；
// 跳过的列表与布局自己的 `dialects` 有
// 一定的差异，因此它永远不会与 plan() 不一致。
constexpr scope::Dialect kAllDialects[] = {
    scope::Dialect::Agentty, scope::Dialect::Agents, scope::Dialect::Claude};

[[nodiscard]] bool reads_dialect(const scope::Layout& l, scope::Dialect d) noexcept {
    // 空跨度意味着"defaulted"，并且plan()将其视为native
    // -only——镜像该内容而不是报告不读取任何内容的特征。
    const auto ds = l.dialects.empty()
        ? std::span<const scope::Dialect>{scope::kNativeOnly}
        : l.dialects;
    for (scope::Dialect have : ds)
        if (have == d) return true;
    return false;
}

// 源的具体目标：叶连接到基部上，
// 除了变量已经命名为确切文件的显式轨迹处。
[[nodiscard]] fs::path target_of(const scope::Source& src,
                                 const scope::Layout& layout,
                                 const scope::Env& env) {
    if (src.locus == scope::Locus::Explicit && env.explicit_config)
        return *env.explicit_config;
    return src.base / fs::path{layout.leaf};
}

[[nodiscard]] bool env_set(std::string_view name) noexcept {
    if (name.empty()) return false;
    const char* v = std::getenv(std::string{name}.c_str());
    return v && v[0];
}

// 批准叶MCP担保。使用相同的常量 bridge.cpp；
// 此处命名，以便报告读取与生成门读取的相同的存储。
constexpr std::string_view kMcpApprovalsLeaf = kMcpApprovals;

// trust_of关于该来源的一句话。
//
// 调用和SAME作用于生成门调用，具有相同的内容散
// 列和相同的批准存储，因此这里打印的单词是连接
// 循环所作用的单词。任何不足都是第二意见，
// 这就是整个文件的存在要避免的。
[[nodiscard]] std::string trust_word(const scope::Source& src,
                                     const fs::path& file) {
    // 打开之前的is_regular_file：DIRECTORY打开良好，
    // 然后从第一次读取中抛出，而istreambuf_iterator
    // 吸食无法捕获。报告信任绝不能成为中止这一过程的
    // 原因。
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) return {};
    std::ifstream in(file, std::ios::binary);
    if (!in) return {};
    const std::string bytes((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    const scope::Trust t = scope::trust_of(
        src, scope::content_hash(bytes),
        scope::load_approvals(kMcpApprovalsLeaf));
    if (std::holds_alternative<scope::Trusted>(t)) return "trusted";
    if (const auto* b = std::get_if<scope::Blocked>(&t))
        return "blocked: " + b->reason;
    return "needs approval";
}

// 字节在`dir`下，递归。符号链接不是：将 链接 插入
// 别人的 源码树 将使 agentty 报告他们的磁盘是我们的，
// 并且循环将挂起命令。
//
// 当无法检查某些源码树时，设置`out_partial`。
// 然后计数是 UNDERSTATES，这样说比数字更重要：
// 不可读文件的2MB旁边的无声0B是整个
// 文件存在以防止的同一类置信谎言。
[[nodiscard]] std::uintmax_t dir_bytes(const fs::path& dir, bool& out_partial) {
    out_partial = false;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        // 规范可以命名 FILE 而不是目录。
        const auto sz = fs::file_size(dir, ec);
        return ec ? 0 : sz;
    }
    std::uintmax_t total = 0;
    fs::recursive_directory_iterator it{
        dir, fs::directory_options::skip_permission_denied, ec};
    if (ec) { out_partial = true; return 0; }
    // 以 error_code 增量前进 MANUALLY。 range
    // -for 调用 抛出运算符++，并下降到 chmod-000 子目录，
    // 在那里抛出 - skip_permission_denied 控制初始打开，
    // 而不是每个步骤。这丢弃了整个运行总计并报告 0 B 为 1.9
    // MB 源码树。
    const fs::recursive_directory_iterator end{};
    while (it != end) {
        std::error_code sec;
        if (it->is_regular_file(sec) && !sec) {
            const auto sz = it->file_size(sec);
            if (!sec) total += sz;
            else      out_partial = true;
        } else if (sec) {
            out_partial = true;
        } else if (it->is_directory(sec) && !sec) {
            // skip_permission_denied使不可读的目录消失
            // SILENTLY——没有error_code，没有什么值得注意的，
            // 并且它里面的所有内容根本就不会出现。持有 2 MB 的 chmod-
            // 000 子目录报告为 0 B，没有任何迹象表明遗漏了任何内容。
            //
            // 所以直接探测它。打开它是了解递
            // 归将假装它为空的唯一方法。
            std::error_code oec;
            fs::directory_iterator probe{it->path(), oec};
            if (oec) out_partial = true;
        }
        std::error_code iec;
        it.increment(iec);
        if (iec) {
            // 这里不能下降。保留我们计算的内容，
            // 注意答案很短，然后停止——迭代器不
            // 提供恢复过去不可读的分支的功能。
            out_partial = true;
            break;
        }
    }
    return total;
}

// 三个重要的-ish数字，这是任何人都需要回答"is this the big one"的。
// 精确的 字节 将是噪音。
[[nodiscard]] std::string human_bytes(std::uintmax_t n) {
    const char* unit[] = {"B", "KB", "MB", "GB", "TB"};
    double v = static_cast<double>(n);
    int u = 0;
    while (v >= 1024.0 && u < 4) { v /= 1024.0; ++u; }
    char buf[32];
    std::snprintf(buf, sizeof buf, u == 0 ? "%.0f %s" : "%.1f %s", v, unit[u]);
    return buf;
}

}  // 命名空间结束。

std::span<const Entry> inventory() noexcept { return kEntries; }

const Entry* find(std::string_view name) noexcept {
    for (const Entry& e : kEntries)
        if (e.name == name) return &e;
    return nullptr;
}

Report describe(const Entry& e) {
    Report r;
    r.name = e.name;
    r.what = e.what;

    if (e.read) {
        r.explicit_env = e.read->explicit_env;
        const scope::Env env = scope::current_env(*e.read);
        // plan() IS 梯子。这里没有过滤——这就是方言
        // 变化带来的 不变量：计划发出的就是被读取的。
        for (const scope::Source& src : scope::plan(*e.read, env)) {
            ReadRow row;
            row.locus   = src.locus;
            row.dialect = src.dialect;
            const fs::path p = target_of(src, *e.read, env);
            row.path = p.string();
            std::error_code ec;
            row.exists = fs::exists(p, ec);
            // 只有可执行内容需要担保，并且只有存在的
            // 文件具有要绑定批准的字节。
            if (row.exists && executable(e.name))
                row.trust = trust_word(src, p);
            r.reads.push_back(std::move(row));
        }
        for (scope::Dialect d : kAllDialects) {
            if (reads_dialect(*e.read, d)) continue;
            r.skipped.push_back({d, std::string{scope::dir_name(d)}});
        }
#if AGENTTY_MCP
        // 本机上的外部配置为 EXIST。仅在存在时列出：命名无人
        // 拥有的路径是噪音，而命名他们拥有的路径则是
        // "why is agentty ignoring my servers" 的答案。
        if (executable(e.name)) {
            const fs::path home = util::home_dir_or_empty();
            std::error_code fec;
            const fs::path proj = fs::current_path(fec);
            for (auto t : mcp::import_::all_tools()) {
                const auto loc = mcp::import_::locations_of(t, home, proj);
                for (const fs::path& p : {loc.user, loc.project}) {
                    if (p.empty() || !fs::is_regular_file(p, fec)) continue;
                    r.foreign.push_back(
                        {std::string{mcp::import_::name_of(t)}, p.string(),
                         "agentty mcp import --from "
                             + std::string{mcp::import_::name_of(t)}});
                }
            }
        }
#endif
    }

    if (e.write) {
        WriteRow w;
        w.env            = e.write->env;
        w.anchor         = anchor_env(e.write->root);
        w.env_moves_root = moves_whole_root(*e.write);
        w.rebuildable   = e.write->life.rebuildable;
        w.keep_last     = e.write->life.keep_last;
        w.retention     = e.write->retention;
        // resolve_dry:报告位置而不创建它或发出警告。
        // 该命令是观察者；询问日志去哪里一定不能
        // 创建日志目录。
        if (auto got = dirs::resolve_dry(*e.write)) {
            w.path   = got->path.string();
            w.origin = got->origin;
            std::error_code sec;
            if (fs::exists(got->path, sec)) {
                // 共享已解析目录的规格（rag 和反馈都位于
                // <project>/.agentty 上）不得
                // 各自声明整个 源码树，或者列总和是实际磁盘的两倍。
                // 路径的FIRST行拥有测量；后来的人会这么说，
                // 而不是重复不属于他们的数字。
                bool owned_by_earlier = false;
                for (const Entry& other : kEntries) {
                    if (&other == &e) break;   // 仅第BEFORE行这一行
                    if (!other.write) continue;
                    if (auto o = dirs::resolve_dry(*other.write);
                        o && o->path == got->path) {
                        owned_by_earlier = true;
                        break;
                    }
                }
                w.shared_dir = owned_by_earlier;
                if (!owned_by_earlier) {
                    bool partial = false;
                    w.bytes    = dir_bytes(got->path, partial);
                    w.partial  = partial;
                    w.measured = true;
                }
            }
        } else {
            w.error = got.error().detail.empty() ? "unresolved"
                                                 : got.error().detail;
        }
        r.write = std::move(w);
    }

    AGT_LOG(Persist, Debug, "config", "describe {}: {} read, {} skipped, write={}",
            e.name, r.reads.size(), r.skipped.size(), r.write.has_value());
    return r;
}

namespace {

[[nodiscard]] const char* origin_note(dirs::Origin o) noexcept {
    switch (o) {
        case dirs::Origin::Default:          return "";
        case dirs::Origin::Override:         return "  (overridden)";
        case dirs::Origin::OverrideFellBack: return "  (override unusable — fell back)";
    }
    return "";
}

void print_one(const Report& r) {
    std::printf("%s — %s\n", std::string{r.name}.c_str(),
                std::string{r.what}.c_str());

    if (!r.reads.empty()) {
        // 有闸门时，将闸门命名为头文件。
        // 连续看到 "needs approval" 的读者应该知道 原因 这个问题
        // 就是所问的问题。
        std::printf("\nread, in precedence order%s\n",
                    executable(r.name) ? "  (executable — project configs are"
                                         " vouched by content hash)" : "");
        int n = 0;
        for (const ReadRow& row : r.reads) {
            std::printf("  %d  %-8s %-8s %s%s%s%s\n", ++n,
                        std::string{scope::to_string(row.locus)}.c_str(),
                        std::string{scope::dir_name(row.dialect)}.c_str(),
                        row.path.c_str(),
                        row.exists ? "" : "   (missing)",
                        row.trust.empty() ? "" : "   ",
                        row.trust.c_str());
        }
        if (!r.explicit_env.empty())
            std::printf("     $%s overrides all of the above\n",
                        std::string{r.explicit_env}.c_str());
    }

    // 有价值的一半。 "Not read, and why" 是指有人在提交
    // 问题之前发现自己的 .claude/mcp.json 被故意忽略。
    if (!r.skipped.empty() || !r.foreign.empty()) {
        std::printf("\nnot read\n");
        for (const SkippedDialect& s : r.skipped)
            std::printf("  %-8s not a portable format for %s\n",
                        s.dir.c_str(), std::string{r.name}.c_str());
        // 外部配置获取采用它的命令。实时读取它会将用户的
        // 服务器放置在两个具有优先规则的位置；
        // import 将它们合二为一，这正是他们所要求的。
        for (const ForeignSource& f : r.foreign)
            std::printf("  %-8s %s\n           → %s\n",
                        f.tool.c_str(), f.path.c_str(), f.adopt_with.c_str());
    }

    if (r.write) {
        std::printf("\nwrite\n");
        if (!r.write->error.empty()) {
            std::printf("  unresolved: %s\n", r.write->error.c_str());
        } else {
            std::printf("  %s%s\n", r.write->path.c_str(),
                        origin_note(r.write->origin));
            if (r.write->measured)
                std::printf("  %s on disk%s\n",
                            human_bytes(r.write->bytes).c_str(),
                            r.write->partial
                                ? "  (at least — part of it is unreadable)" : "");
            else if (r.write->shared_dir)
                std::printf("  shares this directory with another concern"
                            " (counted there)\n");
            // 准确说出每个变量的变化。首先是锚点（当根
            // 有锚点时），因为那是模型；然后这个规范自己的变量，
            // 标记为更窄，因此两者永远不会被视为
            // 相等。
            if (!r.write->anchor.empty())
                std::printf("  $%s moves the whole root\n",
                            std::string{r.write->anchor}.c_str());
            if (!r.write->env.empty() && !r.write->env_moves_root)
                std::printf("  $%s moves just this%s\n",
                            std::string{r.write->env}.c_str(),
                            r.write->anchor.empty() ? "" : " (narrower)");
            if (r.write->rebuildable && r.write->keep_last > 0)
                std::printf("  rebuildable, keeps the newest %u\n",
                            r.write->keep_last);
            else if (r.write->rebuildable)
                std::printf("  rebuildable — safe to delete\n");
            else if (!r.write->retention.empty())
                std::printf("  %s\n", std::string{r.write->retention}.c_str());
            else
                std::printf("  kept until you delete it\n");
        }
    }
    std::printf("\n");
}

// 一个屏幕上的存储模型：两个根，
// 以及其下的每个目录的变量。存储仅按环境配置。
void print_env() {
    std::printf("storage model\n\n");
    std::printf("  ~/.agentty           follows you         $AGENTTY_HOME%s\n",
                env_set("AGENTTY_HOME") ? "         (set)" : "");
    std::printf("  <project>/.agentty   follows the code    $AGENTTY_PROJECT_DIR%s\n",
                env_set("AGENTTY_PROJECT_DIR") ? "  (set)" : "");
    std::printf("\neach directory can also move on its own:\n\n");
    for (const Entry& e : kEntries) {
        if (!e.write || e.write->env.empty()) continue;
        const std::string env{e.write->env};
        const char* v = std::getenv(env.c_str());
        std::printf("  $%-26s %-12s %s\n", env.c_str(), std::string{e.name}.c_str(),
                    v && *v ? v : "");
    }
    const char* kd = std::getenv("AGENTTY_THREADS_KEEP_DAYS");
    std::printf("\n  $%-26s %-12s %s\n", "AGENTTY_THREADS_KEEP_DAYS", "retention",
                kd && *kd ? kd : "(unset: threads are kept)");
    std::printf("\nrelative paths resolve against the root, not the current dir.\n");
    std::printf("$AGENTTY_MCP_CONFIG is not storage, it names one file to read.%s\n",
                env_set("AGENTTY_MCP_CONFIG") ? "   (set)" : "");
    std::printf("full map: https://agentty.org/docs/storage\n");
}

// 旧版本留下的文件现在无法读取。
struct Stale { fs::path path; const char* why; };

[[nodiscard]] std::vector<Stale> find_stale() {
    std::vector<Stale> out;
    std::error_code ec;
    auto add = [&](const fs::path& p, const char* why) {
        if (!p.empty() && fs::exists(p, ec)) out.push_back({p, why});
    };
    const fs::path home = util::home_dir_or_empty();
    if (auto u = dirs::resolve_dry(dirs::Spec{.root = dirs::Root::User})) {
        const fs::path& r = u->path;
        add(r / "settings.json~", "old settings backup");
        add(r / ".settings.lock", "old settings lock");
        for (fs::directory_iterator it{r, ec}, end; !ec && it != end; it.increment(ec)) {
            const auto n = it->path().filename().string();
            if (n.starts_with("rag_") && n.find(".ragdb") != std::string::npos)
                add(it->path(), "index from a run in your home dir");
        }
    }
    if (auto t = dirs::resolve_dry(kThreadsSpec)) {
        for (fs::directory_iterator it{t->path, ec}, end; !ec && it != end; it.increment(ec))
            if (it->path().filename().string().ends_with(".thread.ragdb"))
                add(it->path(), "per-thread index, no longer used");
    }
    if (auto p = dirs::resolve_dry(dirs::Spec{.root = dirs::Root::Project})) {
        add(p->path / "routing_memory.tsv", "old routing memory");
        add(p->path / "routing_memory.tsv.lock", "old routing memory lock");
    }
    if (auto c = dirs::resolve_dry(kCredentialsSpec))
        add(c->path / "copilot_model_support.json", "cache that now lives in cache/");
    if (!home.empty()) {
        add(home / ".cache" / "agentty", "old cache location");
        // 迁移移动它所知道的；剩下的任何东西可能仍然很重要。
        if (const auto c = home / ".config" / "agentty"; fs::is_empty(c, ec) && !ec)
            add(c, "old config location, empty");
    }
    return out;
}

// ── 移动 ────────────────────────────────────────────────────────────────────

struct Tally { std::uintmax_t files = 0, bytes = 0; bool ok = true; };

// p下的文件和字节，不遵循符号链接。
[[nodiscard]] Tally tally(const fs::path& p) {
    Tally t;
    std::error_code ec;
    if (!fs::exists(p, ec)) return t;
    fs::recursive_directory_iterator it{p, fs::directory_options::skip_permission_denied, ec}, end;
    if (ec) { t.ok = false; return t; }
    for (; it != end; it.increment(ec)) {
        if (ec) { t.ok = false; break; }
        std::error_code fec;
        if (it->is_symlink(fec)) { ++t.files; continue; }
        if (it->is_regular_file(fec)) { ++t.files; t.bytes += it->file_size(fec); }
    }
    return t;
}

[[nodiscard]] fs::path absolute_clean(const fs::path& p) {
    std::error_code ec;
    auto a = fs::weakly_canonical(fs::absolute(p, ec), ec);
    return ec ? fs::absolute(p) : a;
}

[[nodiscard]] bool inside(const fs::path& child, const fs::path& parent) {
    auto c = child.begin();
    for (auto p = parent.begin(); p != parent.end(); ++p, ++c)
        if (c == child.end() || *c != *p) return false;
    return true;
}

// `agentty config move <dir> <path>`:复制、
// 验证，然后删除。 agentty 从不编写自己的配置；它打印导出行供用户添加。
int cmd_move(std::span<const std::string> argv) {
    auto usage = [] {
        std::fprintf(stderr, "usage: agentty config move <dir> <path>\n       dirs:");
        for (const Entry& e : kEntries)
            if (e.write && !e.write->env.empty()) std::fprintf(stderr, " %s", std::string{e.name}.c_str());
        std::fprintf(stderr, "\n       a path of `default` moves it back.\n");
        return 2;
    };
    if (argv.size() != 2) return usage();
    const Entry* entry = find(argv[0]);
    if (!entry || !entry->write || entry->write->env.empty()) return usage();
    const std::string env{entry->write->env};

    const auto from_r = dirs::resolve_dry(*entry->write);
    if (!from_r) { std::fprintf(stderr, "agentty: can't resolve %s\n", argv[0].c_str()); return 1; }
    const fs::path from = absolute_clean(from_r->path);
    const bool to_default = argv[1] == "default";
    fs::path to;
    if (to_default) {
        auto d = dirs::resolve_dry(dirs::Spec{.root = entry->write->root, .leaf = entry->write->leaf});
        if (!d) { std::fprintf(stderr, "agentty: no default for %s\n", argv[0].c_str()); return 1; }
        to = absolute_clean(d->path);
    } else {
        std::string raw = argv[1];
        if (raw == "~" || raw.starts_with("~/"))
            raw = (util::home_dir() / raw.substr(raw.size() > 1 ? 2 : 1)).string();
        to = absolute_clean(raw);
    }
    // 一旦数据存在就告诉外壳什么。
    auto print_export = [&] {
        if (to_default)
            std::printf("\nnow remove $%s from your shell profile:\n  unset %s\n",
                        env.c_str(), env.c_str());
        else
            std::printf("\nnow add this to your shell profile:\n  export %s='%s'\n",
                        env.c_str(), to.string().c_str());
    };

    std::printf("move %s\n  from %s\n  to   %s\n\n", argv[0].c_str(),
                from.string().c_str(), to.string().c_str());
    if (to == from) { std::printf("already there.\n"); return 0; }
    if (inside(to, from) || inside(from, to)) {
        std::fprintf(stderr, "agentty: one path is inside the other; pick a separate folder.\n");
        return 1;
    }
    std::error_code ec;
    if (fs::exists(to, ec) && !(fs::is_directory(to, ec) && fs::is_empty(to, ec))) {
        std::fprintf(stderr, "agentty: %s already exists and isn't empty.\n", to.string().c_str());
        return 1;
    }
    // 当我们复制时，没有人可以写作。
    const auto lock = util::try_store_exclusive();
    if (!lock) {
        std::fprintf(stderr, "agentty: another agentty is running. quit it and try again.\n");
        return 1;
    }

    if (!fs::exists(from, ec)) {
        std::printf("  nothing there yet\n");
        print_export();
        return 0;
    }
    const Tally before = tally(from);
    if (!before.ok) { std::fprintf(stderr, "agentty: can't read all of %s\n", from.string().c_str()); return 1; }
    std::printf("  %ju files, %s\n", before.files, human_bytes(before.bytes).c_str());
    if (fs::exists(to, ec)) fs::remove(to, ec);   // 上面检查的空目录
    fs::create_directories(to.parent_path(), ec);
    // 相同的文件系统：重命名是即时且原子的。否则复制。
    ec.clear();
    fs::rename(from, to, ec);
    const bool renamed = !ec;
    if (!renamed) {
        ec.clear();
        fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
        const Tally after = tally(to);
        if (ec || !after.ok || after.files != before.files || after.bytes != before.bytes) {
            std::fprintf(stderr, "agentty: copy didn't verify (%s). nothing changed; removed the partial copy.\n",
                         ec ? ec.message().c_str() : "size mismatch");
            std::error_code rec;
            fs::remove_all(to, rec);
            return 1;
        }
        std::printf("  copied and verified\n");
        fs::remove_all(from, ec);
        if (ec) std::printf("  couldn't remove the old copy (%s); it's safe to delete %s\n",
                            ec.message().c_str(), from.string().c_str());
        else    std::printf("  removed the old copy\n");
    } else {
        std::printf("  renamed in place (same disk)\n");
    }
#ifndef _WIN32
    if (entry->write->owner_only) fs::permissions(to, fs::perms::owner_all, ec);
#endif
    print_export();
    return 0;
}

// ── 医生 ───────────────────────────────────────────────────────────────────

// 检查存储是否健康：每个目录都可用，秘密是私有的，没有静默忽略的覆盖。
// 如果有任何问题需要修复，请退出 1。
int cmd_doctor() {
    int problems = 0, notes = 0;
    auto bad  = [&](const std::string& s) { ++problems; std::printf("  ✗ %s\n", s.c_str()); };
    auto note = [&](const std::string& s) { ++notes;    std::printf("  ! %s\n", s.c_str()); };
    auto good = [](const std::string& s)  { std::printf("  ✓ %s\n", s.c_str()); };

    std::printf("storage doctor\n\n");
    for (const Entry& e : kEntries) {
        if (!e.write) continue;
        const std::string name{e.name};
        const auto r = dirs::resolve_dry(*e.write);
        if (!r) { bad(name + ": " + r.error().detail); continue; }
        if (r->origin == dirs::Origin::OverrideFellBack) {
            bad(name + ": override is unusable, using " + r->path.string());
            continue;
        }
        std::error_code ec;
        const fs::path& p = r->path;
        if (!fs::exists(p, ec)) { good(name + ": " + p.string() + " (made on first use)"); continue; }
        if (!fs::is_directory(p, ec)) { bad(name + ": " + p.string() + " is not a folder"); continue; }
#ifndef _WIN32
        if (::access(p.c_str(), W_OK) != 0) { bad(name + ": " + p.string() + " is not writable"); continue; }
        if (e.write->owner_only) {
            const auto perms = fs::status(p, ec).permissions();
            if ((perms & (fs::perms::group_all | fs::perms::others_all)) != fs::perms::none) {
                bad(name + ": " + p.string() + " is readable by others; fix: chmod 700 '" + p.string() + "'");
                continue;
            }
        }
#endif
        good(name + ": " + p.string() + (r->origin == dirs::Origin::Override ? "  (moved)" : ""));
    }

#ifndef _WIN32
    // 无论文件夹位于何处，每个秘密都必须是 0600。
    if (auto c = dirs::resolve_dry(kCredentialsSpec)) {
        std::error_code ec;
        for (fs::directory_iterator it{c->path, ec}, end; !ec && it != end; it.increment(ec)) {
            std::error_code fec;
            if (!it->is_regular_file(fec) || it->path().extension() == ".lock") continue;
            if (it->path().filename() == "copilot_model_support.json") continue;   // 过期，见干净
            const auto perms = it->status(fec).permissions();
            if ((perms & (fs::perms::group_all | fs::perms::others_all)) != fs::perms::none)
                bad("credentials: " + it->path().filename().string() +
                    " is readable by others; fix: chmod 600 '" + it->path().string() + "'");
        }
    }
#endif

    // 保留设置但无法使用值得大声说出来。
    if (const char* kd = std::getenv("AGENTTY_THREADS_KEEP_DAYS"); kd && *kd) {
        char* end = nullptr;
        const long n = std::strtol(kd, &end, 10);
        if (end == kd || *end != '\0' || n <= 0)
            bad(std::string{"$AGENTTY_THREADS_KEEP_DAYS='"} + kd + "' is not a positive whole number, so threads are kept");
        else
            good("threads expire after " + std::to_string(n) + " days idle");
    }

    if (const auto stale = find_stale(); !stale.empty())
        note(std::to_string(stale.size()) + " file(s) left by older versions; see `agentty config clean`");

    std::printf("\n%s\n", problems ? "needs attention." : notes ? "healthy, with notes." : "healthy.");
    return problems ? 1 : 0;
}

int print_clean(bool apply) {
    const auto stale = find_stale();
    if (stale.empty()) { std::printf("nothing stale.\n"); return 0; }
    std::uintmax_t total = 0;
    for (const auto& s : stale) {
        bool partial = false;
        const auto b = dir_bytes(s.path, partial);
        total += b;
        std::printf("  %-9s %s   (%s)\n", human_bytes(b).c_str(), s.path.string().c_str(), s.why);
        if (apply) { std::error_code ec; fs::remove_all(s.path, ec); }
    }
    std::printf("\n%s %s\n", apply ? "removed" : "would remove", human_bytes(total).c_str());
    if (!apply) std::printf("run `agentty config clean --yes` to delete them.\n");
    return 0;
}

void print_table() {
    std::printf("where things live\n\n");
    // 手动填充列而不是用printf的%-Ns填充，因为占位符是长
    // 破折号：三个BYTES，一个COLUMN。 %-6s 填充到 字节 计数，
    // 并将 存储单元 短接两个，这正是具有非 -ASCII
    // 存储单元 的表变得混乱的方式。
    auto pad = [](std::string s, std::size_t cols) {
        std::size_t width = 0;
        for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++width;
        if (width < cols) s.append(cols - width, ' ');
        return s;
    };
    std::printf("  %s%s%s%s%s\n",
                pad("concern", 13).c_str(), pad("what", 23).c_str(),
                pad("reads", 7).c_str(), pad("on disk", 10).c_str(), "writes");
    std::uintmax_t total = 0;
    for (const Entry& e : kEntries) {
        const Report r = describe(e);
        std::string reads = "—";
        if (e.read) reads = std::to_string(r.reads.size());
        // 尺寸列是所需的#58："move my non-config data"
        // 是无法回答的，直到您可以看到哪些类别是大的。
        std::string size = "—";
        if (r.write && r.write->measured) {
            size = human_bytes(r.write->bytes);
            if (r.write->partial) size += "+";   // 计数不足；查看详细视图
            total += r.write->bytes;
        } else if (r.write && r.write->shared_dir) {
            size = "(shared)";
        }
        const std::string where =
            r.write ? (r.write->error.empty() ? r.write->path : "unresolved")
                    : "—";
        std::printf("  %s%s%s%s%s\n",
                    pad(std::string{e.name}, 13).c_str(),
                    pad(std::string{e.what}, 23).c_str(),
                    pad(reads, 7).c_str(), pad(size, 10).c_str(),
                    where.c_str());
    }
    std::printf("  %s%s%s%s\n", pad("", 13).c_str(), pad("total", 23).c_str(),
                pad("", 7).c_str(), human_bytes(total).c_str());
    std::printf("\n`agentty config <concern>`  details for one row\n"
                "`agentty config env`        every override and which are set\n"
                "`agentty config move <dir> <path>`  move a folder, safely\n"
                "`agentty config doctor`     check permissions and overrides\n"
                "`agentty config clean`      files old versions left behind\n");
}

}  // 命名空间结束。

int cmd_config(std::span<const std::string> argv) {
    if (argv.empty()) {
        print_table();
        return 0;
    }
    const std::string& want = argv.front();
    if (want == "env") {
        print_env();
        return 0;
    }
    if (want == "clean") {
        const bool yes = argv.size() > 1 && (argv[1] == "--yes" || argv[1] == "-y");
        return print_clean(yes);
    }
    if (want == "move")   return cmd_move(argv.subspan(1));
    if (want == "doctor") return cmd_doctor();
    if (const Entry* e = find(want)) {
        print_one(describe(*e));
        return 0;
    }
    std::fprintf(stderr, "unknown concern: %s\n\nknown: env clean move doctor", want.c_str());
    for (const Entry& e : kEntries)
        std::fprintf(stderr, " %s", std::string{e.name}.c_str());
    std::fprintf(stderr, "\n");
    return 1;
}

}  // namespace agentty::config
