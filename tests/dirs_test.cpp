// dirs_test.cpp — agentty::dirs ,写 -side 存储原始。
//
// 值得打针的属性,以及每个守卫的虫子:
//
//   1. 默认分辨率+来源(Origin::Default)
//   2. 绝对胜负(# 58的整点)
//   3. a RELATIVE 覆盖解析 ROOT ,而不是阴茎 -
//      "one directory, not one per launch dir" 规则
//   4. 一种情况 空 覆盖计数为未设置( 导出) -but-blank 是一个常见
//      炮弹事故,必须无动于衷)
//   5. UNUSABLE 的操作会发出警告,并会退后,报告
//      Origin::OverrideFellBack 而不是默默地把它贴在上面
//   6. 非 国 家 -overridable Spec完全忽略环境( . 凭据
//      不可因为 shell 配置文件中的行而移动)
//   7. owner_only 迫使 0700,即使超过 07555 dir的点
//   8. project_anchor() WALKS UP 标记的标记—— `cd src/ && agentty`
//      重复-index bug (# 61. 这是project_root ()
//      无法做到的, 因为它是 cwd 夹住的, 也是 dirs 拥有自己的锚的原因。
//   9. 无标记的源码树 回到 cwd,所以行为不变
//      在那原始人存在之前
//  10. Root::Project 挂断 <anchor>/.agentty
//  11. resolve_dry() 没有任何结果
//  12. Lifecycle::sweeps ()是62号的结构性大门
//  13. 扫射: keep_last + 现场 -variant 保护+侧车+遗产
//      未标记的文件, 和有界限的爆炸半径
//  14. a NON-rebuildable 商店从不扫荡,不管什么 keep_last 说
//  15. min_age 退后,别走 变体 并行进程可能仍在写作中
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "agentty/dirs/dirs.hpp"
#include "agentty/tool/util/fs_helpers.hpp"

namespace fs = std::filesystem;
using namespace agentty::dirs;

namespace {

fs::path g_sandbox;
int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++g_failures;
}

void touch(const fs::path& p) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << "x";
}

#ifndef _WIN32
unsigned mode_of(const fs::path& p) {
    struct stat st{};
    if (::stat(p.c_str(), &st) != 0) return 0;
    return st.st_mode & 07777;
}
#endif

}  // 命名空间结束。

int main() {
    g_sandbox = fs::temp_directory_path() /
                ("agentty_dirs_test_" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(g_sandbox, ec);

    const fs::path home = g_sandbox / "home";
    fs::create_directories(home);
    ::setenv("HOME", home.c_str(), 1);
    ::unsetenv("AGENTTY_HOME");
    ::unsetenv("XDG_CONFIG_HOME");
    ::unsetenv("AGENTTY_TEST_DIR");

    const fs::path root = home / ".agentty";

    // ── 1. 默认解决 ────────────────────────────────────────────
    {
        const Spec s{.root = Root::User, .leaf = "widgets", .env = "AGENTTY_TEST_DIR"};
        auto r = resolve(s);
        check(r.has_value(), "default: resolved");
        if (r) {
            check(r->path == root / "widgets", "default: path under root");
            check(r->origin == Origin::Default, "default: origin");
            check(!r->overridden(), "default: not overridden");
            check(fs::is_directory(r->path), "default: created");
            check(r->root == root, "default: root reported");
        }
    }

    // ── 2. 绝对胜负 ────────────────────────────────────────
    {
        const fs::path elsewhere = g_sandbox / "elsewhere";
        ::setenv("AGENTTY_TEST_DIR", elsewhere.c_str(), 1);
        const Spec s{.root = Root::User, .leaf = "widgets", .env = "AGENTTY_TEST_DIR"};
        auto r = resolve(s);
        check(r.has_value(), "abs override: resolved");
        if (r) {
            check(r->path == elsewhere, "abs override: path honoured");
            check(r->origin == Origin::Override, "abs override: origin");
            check(fs::is_directory(elsewhere), "abs override: created");
        }
    }

    // ── 3. 相对的推翻解决了ROOT而不是cwd ────
    // 规则,使一个压倒性 一个 目录而不是每个
    // 目录的不同目录 agentty 发射开始
    {
        ::setenv("AGENTTY_TEST_DIR", "widgets2", 1);
        const fs::path cwd_before = fs::current_path();
        fs::create_directories(g_sandbox / "unrelated");
        fs::current_path(g_sandbox / "unrelated");

        const Spec s{.root = Root::User, .leaf = "widgets", .env = "AGENTTY_TEST_DIR"};
        auto r = resolve(s);
        check(r.has_value(), "rel override: resolved");
        if (r) {
            check(r->path == root / "widgets2", "rel override: under root, not cwd");
            check(r->path.parent_path() != g_sandbox / "unrelated",
                  "rel override: did NOT resolve against cwd");
        }
        fs::current_path(cwd_before);
    }

    // ── 4. 空表示未设置 ─────────────────────────────────────────────
    {
        ::setenv("AGENTTY_TEST_DIR", "", 1);
        const Spec s{.root = Root::User, .leaf = "widgets", .env = "AGENTTY_TEST_DIR"};
        auto r = resolve(s);
        check(r.has_value(), "empty override: resolved");
        if (r) {
            check(r->path == root / "widgets", "empty override: treated as unset");
            check(r->origin == Origin::Default, "empty override: origin is Default");
        }
    }

    // ── 5. 无法使用超载警告+后退,SAYS后退 ─────────────
    // 一条在路上的文件是最有可能的错误。 警告文本会被
    // 眼睛检查; 测试的针头是, 我们倒下 和 报告它,
    // 而不是默默地为默认服务, 好像什么都没发生一样。
    {
        const fs::path blocker = g_sandbox / "a-file-not-a-dir";
        touch(blocker);
        ::setenv("AGENTTY_TEST_DIR", blocker.c_str(), 1);
        const Spec s{.root = Root::User, .leaf = "widgets", .env = "AGENTTY_TEST_DIR"};
        auto r = resolve(s);
        check(r.has_value(), "bad override: still resolves");
        if (r) {
            check(r->path == root / "widgets", "bad override: fell back to default");
            check(r->origin == Origin::OverrideFellBack,
                  "bad override: origin records the fallback");
            check(r->overridden(), "bad override: overridden() is true");
        }
        ::unsetenv("AGENTTY_TEST_DIR");
    }

    // ── 6. 一个非-overridable光谱忽略了环境 ────────────────
    {
        ::setenv("AGENTTY_TEST_DIR", (g_sandbox / "ignored").c_str(), 1);
        const Spec s{.root = Root::User, .leaf = "secrets"};  // 无 .env
        auto r = resolve(s);
        check(r.has_value(), "no-env: resolved");
        if (r) {
            check(r->path == root / "secrets", "no-env: env ignored");
            check(r->origin == Origin::Default, "no-env: origin is Default");
        }
        ::unsetenv("AGENTTY_TEST_DIR");
    }

    // ── 7. owner_only 7: 00 的超载力,我们没有创建 ───────
#ifndef _WIN32
    {
        const fs::path loose = g_sandbox / "loose";
        fs::create_directories(loose);
        ::chmod(loose.c_str(), 0755);
        check(mode_of(loose) == 0755, "owner_only: precondition is 0755");

        ::setenv("AGENTTY_TEST_DIR", loose.c_str(), 1);
        const Spec s{.root = Root::User, .leaf = "private",
                     .env = "AGENTTY_TEST_DIR", .owner_only = true};
        auto r = resolve(s);
        check(r.has_value(), "owner_only: resolved");
        if (r) check(mode_of(r->path) == 0700, "owner_only: tightened to 0700");
        ::unsetenv("AGENTTY_TEST_DIR");
    }
#endif

    // ── 8. 锚 WALKS UP 标记 ───────────────────────────────
    // #61重复-indexbug, 以及这个不是project_root
    // ( ) 的全部原因: 从子目录发射, 锚不得 移动。
    {
        const fs::path proj = g_sandbox / "proj";
        const fs::path deep = proj / "src" / "runtime";
        fs::create_directories(deep);
        fs::create_directories(proj / ".git");

        const fs::path cwd_before = fs::current_path();

        fs::current_path(proj);
        agentty::tools::util::set_workspace_root(::agentty::IoAccess::grant(), proj);
        const fs::path from_top = project_anchor();

        fs::current_path(deep);
        agentty::tools::util::set_workspace_root(::agentty::IoAccess::grant(), deep);
        const fs::path from_deep = project_anchor();

        check(from_top == fs::weakly_canonical(proj), "anchor: found from top");
        check(from_deep == fs::weakly_canonical(proj),
              "anchor: WALKED UP from src/runtime to the .git root");
        check(from_top == from_deep,
              "anchor: stable regardless of launch dir (the #61 fix)");

        // 以及从它走来的存储路径.
        const Spec s{.root = Root::Project, .leaf = "rag"};
        auto r = resolve(s);
        check(r.has_value(), "project: resolved");
        if (r) {
            check(r->path == fs::weakly_canonical(proj) / ".agentty" / "rag",
                  "project: <anchor>/.agentty/<leaf>");
            check(fs::is_directory(r->path), "project: created");
        }

        fs::current_path(cwd_before);
    }

    // ── 9. 无标记的 源码树 回到 cwd ───────────────────────
    // 行为必须从原始存在之前就改变,所以没有.
    // git的源码树仍然存储而不是拒绝.
    {
        const fs::path bare = g_sandbox / "bare";
        fs::create_directories(bare);
        const fs::path cwd_before = fs::current_path();

        // 前提是 "no marker anywhere above"。
        // 该词 沙箱 住在临时的迪尔, 所以一个标记在其中之一 ITS 祖先(一个
        // 带有 TEMP =C:\ 开发\ Temp 旁边的 C:\ 发展\。
        // agentty ) 使行走(行走) ) -up 合法地停下来;
        // 这是环境,不是虫子, 所以这个案子不能建起来,被跳过。
        fs::path marked_ancestor;
        for (fs::path p = fs::weakly_canonical(bare); ; p = p.parent_path()) {
            for (std::string_view m : project_markers()) {
                std::error_code mec;
                if (fs::exists(p / fs::path{m}, mec)) { marked_ancestor = p; break; }
            }
            if (!marked_ancestor.empty() || p == p.parent_path()) break;
        }

        fs::current_path(bare);
        agentty::tools::util::set_workspace_root(::agentty::IoAccess::grant(), bare);
        const fs::path a = project_anchor();
        if (marked_ancestor.empty())
            check(a == fs::weakly_canonical(bare), "markerless: falls back to cwd");
        else
            std::fprintf(stderr, "skip: markerless case -- %s carries a project "
                                 "marker above the temp dir\n",
                         marked_ancestor.string().c_str());

        fs::current_path(cwd_before);
    }

    // ── 10. resolve_dry 没有任何结果 ──────────────────────────────────
    {
        const Spec s{.root = Root::User, .leaf = "never-made"};
        auto r = resolve_dry(s);
        check(r.has_value(), "dry: resolved");
        if (r) {
            check(r->path == root / "never-made", "dry: path computed");
            check(!fs::exists(r->path), "dry: nothing created");
        }
    }

    // ── 11. 生命周期是62号的大门 ────────────────────────────────
    // 纯,所以通过数值而不是对磁盘进行检查.
    {
        check(!Lifecycle{}.sweeps(), "lifecycle: default never sweeps");
        check(!Lifecycle{.rebuildable = true}.sweeps(),
              "lifecycle: rebuildable alone does not sweep (keep_last=0 ⇒ keep all)");
        check(!Lifecycle{.keep_last = 1}.sweeps(),
              "lifecycle: keep_last WITHOUT rebuildable never sweeps — the "
              "blobs::gc asymmetry, unrecoverable data is never auto-deleted");
        check((Lifecycle{.rebuildable = true, .keep_last = 1}.sweeps()),
              "lifecycle: rebuildable + keep_last sweeps");
    }

    // ── 12. RAG 光谱形状:空叶+覆盖 ────────────────────
    // 抹布的斑点使用叶= "" (他们想要. agentty 根本身,
    // 然后添加自己的文件名)和共享 $AGENTTY_RAG_DIR
    // 被固定在这里 因为空叶很容易与一条迷路连接
    // 因为这是61号固定船的确切形状
    {
        const fs::path proj = g_sandbox / "ragproj";
        const fs::path deep = proj / "src" / "rag";
        fs::create_directories(deep);
        fs::create_directories(proj / ".git");
        const fs::path cwd_before = fs::current_path();

        const Spec rag{.root = Root::Project, .leaf = "", .env = "AGENTTY_RAG_DIR"};

        fs::current_path(proj);
        agentty::tools::util::set_workspace_root(::agentty::IoAccess::grant(), proj);
        auto top = resolve(rag);

        fs::current_path(deep);
        agentty::tools::util::set_workspace_root(::agentty::IoAccess::grant(), deep);
        auto sub = resolve(rag);

        check(top.has_value() && sub.has_value(), "rag spec: resolved from both");
        if (top && sub) {
            check(top->path == fs::weakly_canonical(proj) / ".agentty",
                  "rag spec: empty leaf yields the .agentty root itself");
            check(top->path == sub->path,
                  "rag spec: ONE index dir from root and from src/rag (#61)");
        }

        // 并且它被移动, 61号的功能一半。
        const fs::path moved = g_sandbox / "ragmoved";
        ::setenv("AGENTTY_RAG_DIR", moved.c_str(), 1);
        auto over = resolve(rag);
        check(over.has_value(), "rag spec: override resolved");
        if (over) {
            check(over->path.parent_path() == moved, "rag spec: $AGENTTY_RAG_DIR honoured");
            check(over->path.filename().string().starts_with("ragproj-"),
                  "rag spec: each project gets its own subfolder under the override");
            check(over->origin == Origin::Override, "rag spec: origin is Override");
        }
        // 在同一控制下的第二个项目不得共享。
        {
            const fs::path other = g_sandbox / "otherproj";
            fs::create_directories(other / ".git");
            fs::current_path(other);
            agentty::tools::util::set_workspace_root(::agentty::IoAccess::grant(), other);
            auto o = resolve(rag);
            check(o && over && o->path != over->path,
                  "rag spec: two projects never share one override folder");
        }
        ::unsetenv("AGENTTY_RAG_DIR");

        fs::current_path(cwd_before);
    }

    // ── 13. 扫荡:62号生命周期,强制实施 ───────────────────────────
    {
        const fs::path proj = g_sandbox / "sweepproj";
        fs::create_directories(proj / ".git");
        const fs::path cwd_before = fs::current_path();
        fs::current_path(proj);
        agentty::tools::util::set_workspace_root(::agentty::IoAccess::grant(), proj);

        const Spec swept{
            .root = Root::Project, .leaf = "",
            .life = {.rebuildable = true, .keep_last = 1},
        };
        auto dir = resolve(swept);
        check(dir.has_value(), "sweep: dir resolved");
        const fs::path d = dir->path;

        // 4变体+活1+侧车+遗存未标记文件.
        auto mk = [&](const std::string& n, int age_days) {
            touch(d / n);
            touch(d / (n + ".meta.json"));
            auto t = fs::file_time_type::clock::now()
                   - std::chrono::hours{24 * age_days};
            std::error_code tec;
            fs::last_write_time(d / n, t, tec);
        };
        mk("rag_code.live.ragdb", 0);
        mk("rag_code.aaaa.ragdb", 1);   // 最新数据 过期 —— 由 . keep_last=1
        mk("rag_code.bbbb.ragdb", 5);
        mk("rag_code.cccc.ragdb", 9);
        touch(d / "rag_code.ragdb");               // 遗留, 未标记
        touch(d / "rag_docs.other.ragdb");         // 不同茎,没有被污染
        touch(d / "unrelated.txt");

        const std::array<std::string, 1> keep{"rag_code.live.ragdb"};
        static constexpr std::array<std::string_view, 1> side{".meta.json"};
        const SweepRequest req{
            .spec = swept, .stem = "rag_code.", .suffix = ".ragdb",
            .keep = keep, .sidecar_suffixes = side, .include_untagged = true,
        };

        // DRY RUN 首先,报告,什么也碰不到, 能够让这个
        // 安全发展起来的地产, 与真正的商店相对。
        auto dry = sweep(req, /*dry_run=*/true);
        check(dry.deleted == 3, "sweep dry: would delete 3 (bbbb, cccc, legacy)");
        check(fs::exists(d / "rag_code.cccc.ragdb"), "sweep dry: deleted nothing");
        check(fs::exists(d / "rag_code.ragdb"), "sweep dry: legacy still there");

        auto st = sweep(req);
        check(st.deleted == 3, "sweep: deleted 3");
        check(st.failed == 0, "sweep: no failures");

        // 活的和最新的一个 过期 变体 活下来了
        check(fs::exists(d / "rag_code.live.ragdb"), "sweep: live kept");
        check(fs::exists(d / "rag_code.aaaa.ragdb"),
              "sweep: newest stale kept (keep_last=1 keeps the A/B warm)");
        check(!fs::exists(d / "rag_code.bbbb.ragdb"), "sweep: older deleted");
        check(!fs::exists(d / "rag_code.cccc.ragdb"), "sweep: oldest deleted");

        // 侧车随主.
        check(!fs::exists(d / "rag_code.cccc.ragdb.meta.json"),
              "sweep: sidecar deleted with its principal");
        check(fs::exists(d / "rag_code.live.ragdb.meta.json"),
              "sweep: live sidecar kept");

        // 遗存未标记文件:#62实际47 MB.
        check(!fs::exists(d / "rag_code.ragdb"), "sweep: legacy untagged deleted");

        // 爆破半径:不同干线和无关文件未受影响.
        check(fs::exists(d / "rag_docs.other.ragdb"), "sweep: other stem untouched");
        check(fs::exists(d / "unrelated.txt"), "sweep: unrelated file untouched");

        fs::current_path(cwd_before);
    }

    // ── 14. 非 国 -rebuildable 存储为 绝不 扫荡 ───────────────────────
    // 电话 blobs::gc 不对称,强制。 keep_last 单凭自己就无所事事:
    // 反馈数据是从实际使用中积累出来的,没有东西再生,
    // 所以任何扫描都不可能收集到。
    {
        const fs::path proj = g_sandbox / "nosweep";
        fs::create_directories(proj / ".git");
        const fs::path cwd_before = fs::current_path();
        fs::current_path(proj);
        agentty::tools::util::set_workspace_root(::agentty::IoAccess::grant(), proj);

        const Spec precious{
            .root = Root::Project, .leaf = "",
            .life = {.rebuildable = false, .keep_last = 1},   // 不 可重建
        };
        auto dir = resolve(precious);
        check(dir.has_value(), "no-sweep: resolved");
        touch(dir->path / "rag_code.aaaa.ragdb");
        touch(dir->path / "rag_code.bbbb.ragdb");

        const SweepRequest req{
            .spec = precious, .stem = "rag_code.", .suffix = ".ragdb",
        };
        auto st = sweep(req);
        check(st.deleted == 0, "no-sweep: nothing deleted");
        check(st.examined == 0, "no-sweep: did not even look");
        check(fs::exists(dir->path / "rag_code.bbbb.ragdb"),
              "no-sweep: non-rebuildable data survives keep_last");

        fs::current_path(cwd_before);
    }

    // ── 15. min_age 挡住新鲜的 变体 ───────────────────────────
    // 另外一个过程可能是中期 -save ;
    // 同时运行的嵌入器可能是合法的。
    {
        const fs::path proj = g_sandbox / "graceproj";
        fs::create_directories(proj / ".git");
        const fs::path cwd_before = fs::current_path();
        fs::current_path(proj);
        agentty::tools::util::set_workspace_root(::agentty::IoAccess::grant(), proj);

        // keep_last 必须是 > 0 或 sweeps()
        // 测试是虚假的,而且会空虚地通过 -- -- 它会测量大门,
        // 而不是优雅的窗户。 变体 与 keep_last =1 :没有
        // min_age 年长者将被删除,所以持有 两者 证明窗口有用。
        const Spec graced{
            .root = Root::Project, .leaf = "",
            .life = {.rebuildable = true, .keep_last = 1,
                     .min_age = std::chrono::seconds{3600}},
        };
        auto dir = resolve(graced);
        check(dir.has_value(), "min_age: resolved");
        touch(dir->path / "rag_code.fresh1.ragdb");   // mtime = now
        touch(dir->path / "rag_code.fresh2.ragdb");   // mtime = now

        const SweepRequest req{
            .spec = graced, .stem = "rag_code.", .suffix = ".ragdb",
        };
        auto st = sweep(req);
        check(st.examined == 2, "min_age: both variants examined");
        check(st.too_new == 2, "min_age: both held back by the grace window");
        check(st.deleted == 0, "min_age: nothing deleted");
        check(fs::exists(dir->path / "rag_code.fresh1.ragdb")
                  && fs::exists(dir->path / "rag_code.fresh2.ragdb"),
              "min_age: fresh variants survive even beyond keep_last");

        fs::current_path(cwd_before);
    }

    // ── resolve_dry 决心同意 ─────────────────────────────────
    //
    // 观察者必须预测作者. `agentty config`
    // 解析干燥, 以便询问日志去向不 MAKE 一个日志
    // 目录——但是一个跳过可行性检查的干燥解析度报告着
    // 一个编目器会拒绝的路径,这比创建目录更糟糕: 用户
    // 被告知一个永远不存在的地方.
    //
    // 抓住它的案子是: $AGENTTY_HOME 命名为 a FILE.
    // `<file>/threads` 仅仅是 ABSENT ,其内容为:
    // "fine, we'll create it" 除非你走到最近的祖先 找到一个文件在路上。
    {
        const fs::path base = g_sandbox / "dry";
        fs::create_directories(base);
        const fs::path afile = base / "a-file";
        { std::ofstream f(afile); f << "x"; }

        const Spec s{.root = Root::User, .leaf = "threads",
                     .env = "AGENTTY_TEST_DRY"};

        // 超过点 a FILE - > 作者警告并退后,
        // 所以干答案必须是: 回退 标记为此。
        ::setenv("AGENTTY_TEST_DRY", afile.string().c_str(), 1);
        auto dry = resolve_dry(s);
        auto wet = resolve(s);
        check(dry.has_value() && wet.has_value(), "dry: both resolve");
        if (dry && wet) {
            check(dry->path == wet->path,
                  "dry: the path matches what the writer chose");
            check(dry->origin == wet->origin,
                  "dry: the origin matches too (fell-back, not override)");
            check(dry->origin == Origin::OverrideFellBack,
                  "dry: a file-valued override is reported as fallen back");
        }
        ::unsetenv("AGENTTY_TEST_DRY");

        // 和干燥的意志 仍然产生 无内容 - 这就是它
        // 存在的全部原因。
        const fs::path probe = base / "never-made";
        const Spec q{.root = Root::User, .leaf = "", .env = "AGENTTY_TEST_DRY2"};
        ::setenv("AGENTTY_TEST_DRY2", probe.string().c_str(), 1);
        (void)resolve_dry(q);
        check(!fs::exists(probe), "dry: resolves without creating");
        ::unsetenv("AGENTTY_TEST_DRY2");
    }

    // ── 工程根忽略了自身 ───────────────────────────────
    //
    // <project>/agentty持有衍生状态 -- -- 数十个
    // MB检索指数 -- -- 旁边是一个用户的来源。
    // agentty自己的.gitignore保护agentty的仓库和没有人:
    // 在任何其他的检查中,在热指数分期后的第一个`git add -A`,
    // 运输规则INSIDE的目录使它成为结构而不是民俗。
    {
        const fs::path proj = g_sandbox / "ignoreproj";
        fs::create_directories(proj / ".git");
        const fs::path cwd_before = fs::current_path();
        fs::current_path(proj);
        agentty::tools::util::set_workspace_root(::agentty::IoAccess::grant(), proj);

        const Spec s{.root = Root::Project, .leaf = ""};
        auto d = resolve(s);
        check(d.has_value(), "ignore: project spec resolves");
        if (d) {
            const fs::path ig = d->root / ".gitignore";
            check(fs::exists(ig), "ignore: .gitignore written on creation");
            std::ifstream in(ig);
            std::string body((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
            check(body.find("\n*\n") != std::string::npos
                      || body.rfind("*\n") != std::string::npos,
                  "ignore: the rule ignores everything in here");

            // 绝不 被打中了,一个想做一部分的用户 agentty (
            // 单位:千美元) skills/ dir是真实的用途)编辑此文件,
            // 并且每次发布时重制它的工具都会成为论证的工具.
            {
                std::ofstream out(ig, std::ios::trunc);
                out << "# mine\n*.ragdb\n";
            }
            (void)resolve(s);
            std::ifstream again(ig);
            std::string after((std::istreambuf_iterator<char>(again)),
                               std::istreambuf_iterator<char>());
            check(after == "# mine\n*.ragdb\n",
                  "ignore: a user-edited file is left alone");
        }

        // 电话 USER root 无法获得此文件...
        // ~/.agentty 内不在内 仓库 规则是噪音
        const Spec u{.root = Root::User, .leaf = "threads"};
        if (auto ud = resolve(u))
            check(!fs::exists(ud->root / ".gitignore"),
                  "ignore: user root is left alone");

        fs::current_path(cwd_before);
    }

    // ── 一个超能力定位, 很多项目 ────────────────────────────
    //
    // #61的担心:在同一个目录上点出两次检票,
    // 然后降落在同一文件名上。没有腐败——索引元记录其实体
    // 根并拒绝一个外国的。但每个切换都花费一个完整的重置成本
    // -index ,比磁盘还糟糕 已保存.
    {
        const fs::path p1 = g_sandbox / "shareda";
        const fs::path p2 = g_sandbox / "sharedb";
        const fs::path shared = g_sandbox / "onedir";
        fs::create_directories(p1 / ".git");
        fs::create_directories(p2 / ".git");
        const fs::path cwd_before = fs::current_path();

        const Spec s{.root = Root::Project, .leaf = "cache"};
        ::setenv("AGENTTY_PROJECT_DIR", shared.string().c_str(), 1);

        fs::current_path(p1);
        agentty::tools::util::set_workspace_root(::agentty::IoAccess::grant(), p1);
        auto a = resolve(s);

        fs::current_path(p2);
        agentty::tools::util::set_workspace_root(::agentty::IoAccess::grant(), p2);
        auto b = resolve(s);

        ::unsetenv("AGENTTY_PROJECT_DIR");
        check(a.has_value() && b.has_value(), "shared: both resolve");
        if (a && b) {
            check(a->path != b->path,
                  "two projects under one override get separate directories");
            check(a->path.string().find("shareda-") != std::string::npos,
                  "the basename stays readable");
        }

        // 还有 DEFAULT 保持不散 -- 每个项目已经有自己的项目。
        // agentty ,所以没有什么可以分解的。
        fs::current_path(p1);
        agentty::tools::util::set_workspace_root(::agentty::IoAccess::grant(), p1);
        if (auto d = resolve(s)) {
            // 双侧加冕:在 macOS 临时目录生活在/var,
            // 与/var的连结 private/var 和解脱者 坎尼卡利兹。
            std::error_code ec1, ec2;
            const auto want = fs::weakly_canonical(p1 / ".agentty" / "cache", ec1);
            const auto got  = fs::weakly_canonical(d->path, ec2);
            check(got == want, "the default path is plain");
        }

        fs::current_path(cwd_before);
    }

    // ── 标记为非-empty,包括普通案件 ────────────────
    {
        auto m = project_markers();
        check(!m.empty(), "markers: non-empty");
        bool has_git = false;
        for (auto s : m) if (s == ".git") has_git = true;
        check(has_git, "markers: .git present");
    }

    fs::remove_all(g_sandbox, ec);

    if (g_failures == 0) {
        std::fprintf(stderr, "all dirs checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d dirs check(s) failed\n", g_failures);
    return 1;
}
