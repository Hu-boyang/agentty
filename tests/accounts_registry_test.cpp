// accounts_registry_test —— 名称为 -provider 账户商店。
//
// 锻炼了注册表层(list/upsert/get/remove/set_active/active_label)
// 在沙盒式的XDG_CONFIG_HOME中,因此它永远不触及真正的凭据.
// 提供商-specific snapshot/activate适配器需要活的凭据文件,
// 并且间接被状态更新函数覆盖;在这里我们锁是交换器
// UI所依赖的纯注册表语义学.

#include "agtest_cap.hpp"
#include "agentty/auth/accounts.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>
#if !defined(_WIN32)
#include <sys/wait.h>
#include <unistd.h>   // 叉子,快来
#endif

#include "agtest.hpp"

namespace fs = std::filesystem;
namespace acc = agentty::auth::accounts;

TEST_CASE("accounts registry list/upsert/get/remove/set_active") {
    // 沙箱: a PID-unique 配置目录,所以套房 保持隐形 -j.
    auto root = fs::temp_directory_path() /
                ("agentty_accounts_" + std::to_string(::getpid()));
    fs::remove_all(root);
    fs::create_directories(root);
    ::setenv("AGENTTY_HOME", root.c_str(), 1);
    ::setenv("HOME", root.c_str(), 1);
    // 键盘是选择 -in (AGENTTY_USE_KEYSTORE ),留下未设的,
    // 保持纯洁的(纯净的) file/crypt 路径,这就是我们在这里要锻炼。
    ::unsetenv("AGENTTY_USE_KEYSTORE");

    // 空的 注册表.
    CHECK(acc::list(::agentty::IoAccess::grant()).empty(), "fresh registry lists nothing");
    CHECK(acc::active_label(::agentty::IoAccess::grant(), "anthropic").empty(), "no active label initially");
    CHECK(!acc::get(::agentty::IoAccess::grant(), "anthropic", "work").has_value(), "get on empty is nullopt");

    // 升二级 Anthropic 账户+1 ChatGPT.
    CHECK(acc::upsert(::agentty::IoAccess::grant(), agtest::cap(), "anthropic", "work",     R"({"method":"oauth"})"), "upsert work");
    CHECK(acc::upsert(::agentty::IoAccess::grant(), agtest::cap(), "anthropic", "personal", R"({"method":"api_key"})"), "upsert personal");
    CHECK(acc::upsert(::agentty::IoAccess::grant(), agtest::cap(), "chatgpt",   "team",     R"({"account_id":"acc_123"})"), "upsert chatgpt");

    // 每个供应商的最新上升趋势是积极的。
    CHECK(acc::active_label(::agentty::IoAccess::grant(), "anthropic") == "personal", "newest upsert is active (anthropic)");
    CHECK(acc::active_label(::agentty::IoAccess::grant(), "chatgpt")   == "team",     "chatgpt active label");

    // list_for 由提供者绘制的镜框。
    CHECK(acc::list_for(::agentty::IoAccess::grant(), "anthropic").size() == 2, "two anthropic accounts");
    CHECK(acc::list_for(::agentty::IoAccess::grant(), "chatgpt").size()   == 1, "one chatgpt account");
    CHECK(acc::list(::agentty::IoAccess::grant()).size() == 3, "three accounts total");

    // 得到返回存储的密件。
    auto w = acc::get(::agentty::IoAccess::grant(), "anthropic", "work");
    CHECK(w.has_value() && w->secret == R"({"method":"oauth"})", "get returns work secret");

    // 持久化 : 新的读数(新进程将重新 -read 档案)看到他们.
    // 我们用re-reading到list()模拟,后者是re-parses accounts.json。
    CHECK(acc::path().find("accounts.json") != std::string::npos, "registry path is accounts.json");
    CHECK(fs::exists(acc::path()), "accounts.json written to disk");

    // 明确切换活动。
    CHECK(acc::set_active(::agentty::IoAccess::grant(), agtest::cap(), "anthropic", "work"), "set_active work");
    CHECK(acc::active_label(::agentty::IoAccess::grant(), "anthropic") == "work", "active is now work");
    CHECK(!acc::set_active(::agentty::IoAccess::grant(), agtest::cap(), "anthropic", "ghost"), "set_active on missing fails");

    // 上调已有的更新 secret + tumps 活动,不重复。
    CHECK(acc::upsert(::agentty::IoAccess::grant(), agtest::cap(), "anthropic", "work", R"({"method":"oauth","v":2})"), "re-upsert work");
    CHECK(acc::list_for(::agentty::IoAccess::grant(), "anthropic").size() == 2, "no duplicate row after re-upsert");
    auto w2 = acc::get(::agentty::IoAccess::grant(), "anthropic", "work");
    CHECK(w2 && w2->secret.find("\"v\":2") != std::string::npos, "secret updated in place");

    // 取消活动账户 \ 最新剩余部分 被推广。
    CHECK(acc::remove(::agentty::IoAccess::grant(), agtest::cap(), "anthropic", "work"), "remove active work");
    CHECK(acc::get(::agentty::IoAccess::grant(), "anthropic", "work") == std::nullopt, "work is gone");
    CHECK(acc::active_label(::agentty::IoAccess::grant(), "anthropic") == "personal", "personal promoted to active");
    CHECK(acc::list_for(::agentty::IoAccess::grant(), "anthropic").size() == 1, "one anthropic account left");

    // 删除最后一个 ~ 正在使用的标签清除。
    CHECK(acc::remove(::agentty::IoAccess::grant(), agtest::cap(), "anthropic", "personal"), "remove personal");
    CHECK(acc::list_for(::agentty::IoAccess::grant(), "anthropic").empty(), "no anthropic accounts left");
    CHECK(acc::active_label(::agentty::IoAccess::grant(), "anthropic").empty(), "active label cleared when empty");

    // ChatGPT 人类的树槽没有被污染
    CHECK(acc::list_for(::agentty::IoAccess::grant(), "chatgpt").size() == 1, "chatgpt account still present");

    // 拒绝空的 provider/label.
    CHECK(!acc::upsert(::agentty::IoAccess::grant(), agtest::cap(), "", "x", "s"), "upsert rejects empty provider");
    CHECK(!acc::upsert(::agentty::IoAccess::grant(), agtest::cap(), "anthropic", "", "s"), "upsert rejects empty label");

    fs::remove_all(root);
}

// 几个 agentty 窗口同时登录账户。在 注册表 被打过
// 十字架 -process 节,每个字改为 accounts.json ,
// 并重新命名其副本。 一个 共享 ".tmp" 名字,
// 所以两个甚至可以写同样的临时: 失败者的账户悄悄地消失了.
#if !defined(_WIN32)
TEST_CASE("accounts registry: concurrent processes never lose an account") {
    auto root = fs::temp_directory_path() /
                ("agentty_accounts_x_" + std::to_string(::getpid()));
    fs::remove_all(root);
    fs::create_directories(root);
    ::setenv("AGENTTY_HOME", root.c_str(), 1);
    ::setenv("HOME", root.c_str(), 1);
    ::unsetenv("AGENTTY_USE_KEYSTORE");

    constexpr int kWriters = 6;
    constexpr int kEach    = 8;
    std::vector<::pid_t> kids;
    for (int w = 0; w < kWriters; ++w) {
        const ::pid_t pid = ::fork();
        if (pid == 0) {
            bool ok = true;
            for (int i = 0; i < kEach; ++i)
                ok = acc::upsert(::agentty::IoAccess::grant(), agtest::cap(), "anthropic",
                                 "w" + std::to_string(w) + "-" + std::to_string(i),
                                 R"({"method":"api_key"})") && ok;
            ::_exit(ok ? 0 : 1);
        }
        if (pid > 0) kids.push_back(pid);
    }
    int failed = 0;
    for (::pid_t p : kids) {
        int st = 0;
        ::waitpid(p, &st, 0);
        if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) ++failed;
    }
    CHECK(failed == 0, "every writer's upserts reported success");
    const auto n = acc::list_for(::agentty::IoAccess::grant(), "anthropic").size();
    const std::string survived = "all " + std::to_string(kWriters * kEach)
                               + " accounts survived (got " + std::to_string(n) + ")";
    CHECK(n == kWriters * kEach, survived);
    std::size_t temps = 0;
    for (const auto& e : fs::recursive_directory_iterator(root))
        if (e.path().filename().string().find(".tmp") != std::string::npos) ++temps;
    CHECK(temps == 0, "no temp files left behind");

    fs::remove_all(root);
}
#endif
