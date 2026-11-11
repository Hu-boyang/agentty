// checkpoint_test 结束 -to-end 覆盖范围
// workspace::checkpoint 反对: 真实 抓图 仓库 (模块外壳到 git;
// 仅此而已 ) 创建孤立的 仓库 在系统下临时目录, 时间长度为( ) repo() 缓存
// 其根在第一时间 调用 因此,这个测试必须按自己的程序进行—— MODE ),
// 并横行整个生命周期:创建 → 变异(编辑 + 删除 + 添加 + 重命名) -ish ●
// 概述 □ 恢复 □ 专有 □ 缺失 -id 失败 ^ 忽略 -file 生存 生存 维基
// 百科中的相关条目: 维基语录链接:名人名言 - 谚语 - 谚文 - 谚語 - 谚言
//
// 记录 -side 流动( RestoreCheckpoint 状态更新函数 )
// 此处没有涵盖——需要完整. Model ; 此文件拥有 git/worktree 一半,
// 这就是破坏行动所在的地方。

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <unistd.h>   // 发作
#else
#include <process.h>
#define getpid _getpid
#endif

#include "agentty/workspace/checkpoint.hpp"
#include "agentty/tool/util/subprocess.hpp"
#include "agtest_cap.hpp"

namespace fs = std::filesystem;
using namespace agentty;

static int g_fails = 0;
static void check(bool ok, const std::string& msg) {
    if (!ok) { ++g_fails; std::printf("  FAIL: %s\n", msg.c_str()); }
}

static std::string slurp(const fs::path& p) {
    std::ifstream f(p);
    return std::string((std::istreambuf_iterator<char>(f)), {});
}
static void put(const fs::path& p, const std::string& s) {
    if (p.has_parent_path()) fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::trunc);
    f << s;
}
static void sh(const std::string& cmd) {
    (void)tools::util::run_command_s(cmd, 65536, std::chrono::seconds{30});
}

int main() {
    // ── 孤立的划痕 仓库 ; 移动 cwd INTO 第一次之前的 第一次的时候 repo() ─────
    fs::path root = fs::temp_directory_path()
                  / ("agentty-cp-test-" + std::to_string(::getpid()));
    fs::create_directories(root);
    fs::current_path(root);
    sh("git init -q .");
    // repo() 必须在不配置用户身份的情况下工作( 承诺) -tree 内线
    // -c 身份); 离开 user.name/email 故意的不设.

    check(workspace::in_git_repo(::agentty::IoAccess::grant()), "scratch repo detected");

    // ── t0 内容: 嵌入式 dir, 名称中的空格, 然后检查点 ────────
    put("a.txt", "original-a\n");
    put("sub/b.txt", "original-b\n");
    put("spaced name.txt", "original-spaced\n");
    check(workspace::create_checkpoint("cp1"), "create cp1");
    check(workspace::checkpoint_exists("cp1"), "cp1 exists");
    check(!workspace::checkpoint_exists("zzz"), "unknown id does not exist");

    // ── 变异:编辑、删除、添加 ───────────────────────────────────────
    put("a.txt", "MUTATED-a\n");
    fs::remove("sub/b.txt");
    put("new.txt", "agent-created\n");
    put("spaced name.txt", "MUTATED-spaced\n");

    auto d = workspace::checkpoint_summary("cp1");
    check(d.valid, "summary computes");
    check(d.files_changed == 4,
          "summary counts 4 changed files (got "
          + std::to_string(d.files_changed) + ")");

    // ── 恢复: 工作树返回到 t0 准确 ─────────────────────────
    std::string err;
    check(workspace::restore_checkpoint("cp1", &err),
          "restore cp1 (" + err + ")");
    check(slurp("a.txt") == "original-a\n", "edited file restored");
    check(slurp("sub/b.txt") == "original-b\n", "deleted file restored");
    check(slurp("spaced name.txt") == "original-spaced\n",
          "filename with spaces restored");
    check(!fs::exists("new.txt"), "file created after checkpoint removed");
    if (fs::exists("new.txt")) {
        auto ls = tools::util::run_command_s(
            "git ls-files -z -c -o --exclude-standard | tr '\\0' '\\n'",
            65536, std::chrono::seconds{30});
        std::printf("  [diag] ls-files:\n%s\n", ls.output.c_str());
        auto lt = tools::util::run_command_s(
            "git ls-tree -r --name-only refs/agentty/checkpoints/cp1",
            65536, std::chrono::seconds{30});
        std::printf("  [diag] snapshot tree:\n%s\n", lt.output.c_str());
        std::printf("  [diag] cwd=%s\n", fs::current_path().string().c_str());
        auto rr = tools::util::run_command_s(
            "git rev-parse --show-toplevel", 65536, std::chrono::seconds{30});
        std::printf("  [diag] toplevel=%s\n", rr.output.c_str());
    }

    // 后-restore 摘要必须干净( 0 已更改文件)。
    auto d2 = workspace::checkpoint_summary("cp1");
    check(d2.valid && d2.files_changed == 0,
          "post-restore summary is clean (got "
          + std::to_string(d2.files_changed) + ")");

    // ── 专有能力 ─────────────────────────────────────────────────────
    check(workspace::restore_checkpoint("cp1", &err), "second restore ok");

    // ── 失败路径表面错误, 触摸无 ─────────────────────
    put("canary.txt", "untouched\n");
    check(!workspace::restore_checkpoint("nope", &err),
          "missing checkpoint fails");
    check(!err.empty(), "failure carries a reason");
    check(slurp("canary.txt") == "untouched\n",
          "failed restore leaves worktree alone");

    // ── 忽略的文件在创建和恢复时都幸存 ───────────────────
    put(".gitignore", "ignored.log\n");
    put("ignored.log", "v1\n");
    check(workspace::create_checkpoint("cp2"), "create cp2");
    put("ignored.log", "v2\n");
    check(workspace::restore_checkpoint("cp2", &err), "restore cp2");
    check(slurp("ignored.log") == "v2\n",
          "ignored file untouched by restore");

    // ── 实际指数和 HEAD 保持无动于衷 ───────────────
    auto st = tools::util::run_command_s("git status --porcelain", 65536,
                                         std::chrono::seconds{30});
    check(st.started && st.exit_code == 0, "git status runs");
    // 每个文件应显示为 untracked/modified 相
    // 对于 空 真实指数——恢复一定没有预示什么.
    auto staged = tools::util::run_command_s(
        "git diff --cached --name-only", 65536, std::chrono::seconds{30});
    check(staged.started && staged.output.empty(),
          "restore staged nothing into the real index");

    // ── 抓取索引文件: 已无任何遗存, 过期 扫过 ─────────
    auto scratch_files = [] {
        int n = 0;
        for (const auto& e : fs::directory_iterator(".git"))
            if (e.path().filename().string().starts_with("agentty-checkpoint-index")) ++n;
        return n;
    };
    check(scratch_files() == 0, "no scratch index left after create/summary/restore");
    // 一个被杀死的过程留下: 足够老了 过期.
    put(".git/agentty-checkpoint-index.99999.0", "x");
    fs::last_write_time(".git/agentty-checkpoint-index.99999.0",
                        fs::file_time_type::clock::now() - std::chrono::hours{1});
    // 和新鲜的,好像另一个 agentty 中点时 -operation : 保留。
    put(".git/agentty-checkpoint-index.88888.0", "x");
    for (int i = 0; i < 17; ++i)   // 足够创建来打扫
        (void)workspace::create_checkpoint("sweep" + std::to_string(i));
    check(!fs::exists(".git/agentty-checkpoint-index.99999.0"), "stale scratch index swept");
    check(fs::exists(".git/agentty-checkpoint-index.88888.0"), "fresh one left alone");
    fs::remove(".git/agentty-checkpoint-index.88888.0");

    // ── 并行创建: 所有成功, 无漏漏 ─────────────────────
    {
        std::atomic<int> made{0};
        std::vector<std::thread> ts;
        for (int i = 0; i < 8; ++i)
            ts.emplace_back([&, i] {
                if (workspace::create_checkpoint("par" + std::to_string(i))) ++made;
            });
        for (auto& t : ts) t.join();
        check(made == 8, "8 parallel creates all succeed");
        check(scratch_files() == 0, "and leave no scratch index");
        auto refs = tools::util::run_command_s(
            "git for-each-ref --count=1000 refs/agentty/checkpoints/", 1 << 20,
            std::chrono::seconds{30});
        int n = 0;
        for (char c : refs.output) n += c == '\n';
        check(n >= 8 && n <= 64, "refs stay within the keep cap");
    }

    // ── 清理 ─────────────────────────────────────────────────────────
    fs::current_path(fs::temp_directory_path());
    std::error_code ec;
    fs::remove_all(root, ec);

    if (g_fails) {
        std::printf("CHECKPOINT TESTS FAILED (%d)\n", g_fails);
        return 1;
    }
    std::printf("ALL CHECKPOINT TESTS PASSED\n");
    return 0;
}
