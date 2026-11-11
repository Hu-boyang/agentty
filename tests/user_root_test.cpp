// user_root_test.cpp ——每笔单笔 -user 根 (正数) ~/.agentty
// + 单数 -time 遗产 ~/.config/agentty 移徙。 util/user_root.hpp
// 和原因的 -not- . . .. . 解析这个测试的理由 .
//
// 驱动器 user_root (a) 用于对付伪造的 $HOME/$XDG_CONFIG_HOME 沙箱:
//   1. 刚安装 − 根创建了0700,没有触碰遗留的目录;
//   2. 遗留物安装 − credentials/caches/logs 迁移到原地
//      (复制件+核实件+删除件),空出时已过期的遗留目录;
//   3. 专有能力 − 秒 调用 什么都不会迁移,也什么都不会乱动;
//   4. 新的-file-wins − 新布局下的现有文件永远不会存在
//      由 a 覆盖 过期 遗赠副本;
//   5. $AGENTTY_HOME = 超过根批发。
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>   // 发作
#endif

#include "agentty/util/user_root.hpp"

namespace fs = std::filesystem;

namespace {

fs::path g_sandbox;

void write_file(const fs::path& p, const std::string& body) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << body;
}

std::string read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

int fail(const char* what) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    return 1;
}

}  // 命名空间结束。

int main() {
    // 沙箱:独特的tmp家,所以测试永远不能触及真实~.
    g_sandbox = fs::temp_directory_path() /
                ("agentty_user_root_test_" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(g_sandbox, ec);
    const fs::path home = g_sandbox / "home";
    fs::create_directories(home);
    ::setenv("HOME", home.c_str(), 1);
    ::unsetenv("AGENTTY_HOME");
    ::unsetenv("XDG_CONFIG_HOME");

    // 2+3+4：旧版安装，在首次调用 user_root() 之前构建，
    // 因为迁移是一次 -per-process). ─────────────────────
    const fs::path legacy = home / ".config" / "agentty";
    write_file(legacy / "credentials.json", "SECRET");
    write_file(legacy / "accounts.json", "ACCOUNTS");
    write_file(legacy / "modelsdev.json", "MODELS");
    write_file(legacy / "update_check.json", "UPDATE");
    write_file(legacy / "stderr.log", "STDERR");
    write_file(legacy / "kimi_device_id", "DEVID");
    // 遗产根 -level 日志( 预览) -consolidation ~/.agentty/agentty.log).
    write_file(home / ".agentty" / "agentty.log", "OLDLOG");
    // 4:新布局已经有一个 FRESH credentials.json -
    // 电话 过期 常规复制不得 击之.
    write_file(home / ".agentty" / "credentials" / "credentials.json", "FRESH");

    const fs::path root = agentty::util::user_root();
    if (root != home / ".agentty") return fail("root != ~/.agentty");

#ifndef _WIN32
    struct stat st{};
    if (::stat(root.c_str(), &st) != 0) return fail("root missing");
    if ((st.st_mode & 0777) != 0700) return fail("root not 0700");
#endif

    // 隐藏的秘密 ~ credentials/ …
    if (read_file(root / "credentials" / "accounts.json") != "ACCOUNTS")
        return fail("accounts.json not migrated to credentials/");
    if (read_file(root / "credentials" / "kimi_device_id") != "DEVID")
        return fail("kimi_device_id not migrated");
    // ...4: 不乱打新文件...
    if (read_file(root / "credentials" / "credentials.json") != "FRESH")
        return fail("stale legacy credentials clobbered a fresh file");
    // …缓存 → cache/ 诊断 + logs/ (两德.
    if (read_file(root / "cache" / "modelsdev.json") != "MODELS")
        return fail("modelsdev.json not migrated to cache/");
    if (read_file(root / "cache" / "update_check.json") != "UPDATE")
        return fail("update_check.json not migrated to cache/");
    if (read_file(root / "logs" / "stderr.log") != "STDERR")
        return fail("stderr.log not migrated to logs/");
    if (read_file(root / "logs" / "agentty.log") != "OLDLOG")
        return fail("agentty.log not moved into logs/");
    if (fs::exists(root / "agentty.log")) return fail("root-level agentty.log left behind");

    // 遗产必须被清除 agentty 拥有和退休的一切。
    if (fs::exists(legacy / "credentials.json")) return fail("legacy secret left behind");
    if (fs::exists(legacy)) return fail("emptied legacy dir not retired");

    // 子目录访问器在根内解析, 凭据 0700.
    if (agentty::util::user_credentials_dir() != root / "credentials")
        return fail("user_credentials_dir mismatch");
    if (agentty::util::user_cache_dir() != root / "cache")
        return fail("user_cache_dir mismatch");
    if (agentty::util::user_logs_dir() != root / "logs")
        return fail("user_logs_dir mismatch");
#ifndef _WIN32
    if (::stat((root / "credentials").c_str(), &st) != 0
        || (st.st_mode & 0777) != 0700)
        return fail("credentials/ not 0700");
#endif

    // 3: 专有能力——第二 调用 (同一种过程;
    // 一次) -flag 绝对不能打扰任何东西。
    if (agentty::util::user_root() != root) return fail("second call diverged");
    if (read_file(root / "credentials" / "credentials.json") != "FRESH")
        return fail("second call clobbered credentials");

    // 5:$AGENTTY_HOME 凌驾于根批发.
    const fs::path override_root = g_sandbox / "custom_root";
    ::setenv("AGENTTY_HOME", override_root.c_str(), 1);
    if (agentty::util::user_root() != override_root)
        return fail("AGENTTY_HOME override ignored");
    if (agentty::util::user_cache_dir() != override_root / "cache")
        return fail("cache dir ignores AGENTTY_HOME");

    // ── 6个: -category 超音速( 问题# 58) ─────────────────────────
    // 问题是 "config on the system disk, bulk on a second drive" 所以,
    // 要针是 SPLIT :三块散式凹槽移动,
    // 两块小的不移动.
    const fs::path drive2 = g_sandbox / "drive2";
    ::setenv("AGENTTY_CACHE_DIR", (drive2 / "c").c_str(), 1);
    ::setenv("AGENTTY_LOGS_DIR", (drive2 / "l").c_str(), 1);
    ::setenv("AGENTTY_THREADS_DIR", (drive2 / "t").c_str(), 1);
    if (agentty::util::user_cache_dir() != drive2 / "c")
        return fail("AGENTTY_CACHE_DIR ignored");
    if (agentty::util::user_logs_dir() != drive2 / "l")
        return fail("AGENTTY_LOGS_DIR ignored");
    if (agentty::util::user_threads_dir() != drive2 / "t")
        return fail("AGENTTY_THREADS_DIR ignored");
    if (!fs::is_directory(drive2 / "t"))
        return fail("override dir not created");
    // 拆分的全部点: 保密和设置保留。
    // 将 凭据 拖到可移动驱动器上的覆
    // 盘将和要求的相反。
    if (agentty::util::user_credentials_dir() != override_root / "credentials")
        return fail("credentials followed a bulk override");
    if (agentty::util::user_root() != override_root)
        return fail("root moved under a subdir override");
#ifndef _WIN32
    // 线程 对话的历史, 绝对不能让世界毁灭 -readable
    // 只是因为用户预先 -made 0755目录。
    if (::stat((drive2 / "t").c_str(), &st) != 0 || (st.st_mode & 0777) != 0700)
        return fail("overridden threads dir not 0700");
#endif

    // 7: a RELATIVE 覆盖解决根,而不是 CWD.
    // 否则,一个设置将意味着对每个启动的agentty
    // 目录有一个不同的目录。
    ::setenv("AGENTTY_LOGS_DIR", "logs2", 1);
    if (agentty::util::user_logs_dir() != override_root / "logs2")
        return fail("relative override not anchored at the root");

    // 8:a 数据 UNUSABLE 超常返回默认状态,
    // 而不是让呼叫者完全没有目录。 (它也警告,
    // 这就是保持 回退 由沉默的降级。 )
    const fs::path blocker = g_sandbox / "blocker";
    write_file(blocker, "not a directory");
    ::setenv("AGENTTY_CACHE_DIR", blocker.c_str(), 1);
    if (agentty::util::user_cache_dir() != override_root / "cache")
        return fail("unusable override did not fall back");

    // 9:空运算法 UNSET ,没有 "use the empty path"。
    // 导出 -but- 空的var是常见的外壳事故,不能转移任何东西。
    ::setenv("AGENTTY_LOGS_DIR", "", 1);
    if (agentty::util::user_logs_dir() != override_root / "logs")
        return fail("empty override not treated as unset");

    // 10:凭据和状态移动只使用自己的变量.
    ::setenv("AGENTTY_CREDENTIALS_DIR", (drive2 / "secrets").c_str(), 1);
    ::setenv("AGENTTY_STATE_DIR", (drive2 / "st").c_str(), 1);
    if (agentty::util::user_credentials_dir() != drive2 / "secrets")
        return fail("AGENTTY_CREDENTIALS_DIR ignored");
    if (agentty::util::user_state_dir() != drive2 / "st")
        return fail("AGENTTY_STATE_DIR ignored");
#ifndef _WIN32
    if (::stat((drive2 / "secrets").c_str(), &st) != 0 || (st.st_mode & 0777) != 0700)
        return fail("overridden credentials dir not 0700");
#endif
    ::unsetenv("AGENTTY_CREDENTIALS_DIR");
    ::unsetenv("AGENTTY_STATE_DIR");
    if (agentty::util::user_state_dir() != override_root / "state")
        return fail("state default not under the root");

    ::unsetenv("AGENTTY_CACHE_DIR");
    ::unsetenv("AGENTTY_LOGS_DIR");
    ::unsetenv("AGENTTY_THREADS_DIR");
    ::unsetenv("AGENTTY_HOME");

    fs::remove_all(g_sandbox, ec);
    std::printf("PASS: single-root layout, legacy migration, idempotence, "
                "fresh-wins, AGENTTY_HOME override, per-category overrides\n");
    return 0;
}
