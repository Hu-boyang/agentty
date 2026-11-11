// hooks_gate_test.cpp ——生命周期 -hooks
// CONSENT GATE。这个针头的安全属性是整个设计:一个尚未明确
// 批准的钩子文件 绝不 执行,以及任何 字节 更改为已核准文件 -gates
// 也覆盖了 pre_tool 块语义和 post_tool 开火
// -and-forget 合同。
//
// 在 沙箱 CWD + HOME 临时运行( 批准在 $HOME 下有效)。
// Hook 命令写入标记文件, 执行( 或缺失) 是可以观察到的。

#include "agtest_cap.hpp"
#include "agentty/tool/hooks.hpp"
#include "agentty/tool/util/sandbox.hpp"
#include "agentty/auth/auth.hpp"

#include "agtest.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <print>
#include <string>

#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;
namespace hooks = agentty::tools::hooks;

namespace {

void write_file(const fs::path& p, const std::string& body) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << body;
}

[[nodiscard]] std::string read_all(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::string s((std::istreambuf_iterator<char>(f)),
                  std::istreambuf_iterator<char>());
    return s;
}

// 一个创建`marker`的钩位指令 cmd.exe(Windows钩位壳)没有`touch`;
// `type nul >`是它的拼写.
[[nodiscard]] std::string touch_cmd(const fs::path& marker) {
#ifdef _WIN32
    return "type nul > \"" + marker.string() + "\"";
#else
    return "touch " + marker.string();
#endif
}

// 核准 当前 字节 直接写入批准地图(交互式)
// `agentty hooks approve` 做同一个商店。
void approve_current(const fs::path& hooks_file, const fs::path& home) {
    std::string raw = read_all(hooks_file);
    std::error_code ec;
    auto abs = fs::weakly_canonical(hooks_file, ec);
    nlohmann::json j = nlohmann::json::object();
    j[(ec ? fs::absolute(hooks_file, ec) : abs).string()] =
        agentty::auth::sha256_hex(raw);
    write_file(home / ".agentty" / "hooks_approved.json", j.dump(2));
}

void unapproved_never_runs(const fs::path& sandbox) {
    std::println("--- unapproved_never_runs ---");
    const fs::path marker = sandbox / "pre_ran.marker";
    write_file(sandbox / ".agentty" / "hooks.json",
               nlohmann::json{
                   {"pre_tool", {{{"match", ""},
                                  {"run", touch_cmd(marker)}}}},
               }.dump());

    check(hooks::pending_approval(::agentty::IoAccess::grant()), "unapproved file reports pending");
    auto d = hooks::run_pre_tool("shell", "{}");
    check(!d.blocked, "unapproved hooks never block");
    check(!fs::exists(marker), "unapproved hook COMMAND NEVER EXECUTED");
    std::println("PASS\n");
}

void approved_runs_and_blocks(const fs::path& sandbox, const fs::path& home) {
    std::println("--- approved_runs_and_blocks ---");
    const fs::path marker = sandbox / "post_ran.marker";
    // pre_tool 警戒:阻止任何有动静的殴打 "rm -rf".
    // post_tool : 降下标记(可观察火力) -and-forget).
    write_file(sandbox / ".agentty" / "hooks.json",
               nlohmann::json{
                   {"pre_tool",
                    {{{"match", "shell"},
                      {"run",
#ifdef _WIN32
                       "findstr /c:\"rm -rf\" \"%AGENTTY_HOOK_PAYLOAD_FILE%\" >nul "
                       "&& (echo dangerous command blocked& exit /b 1) "
                       "|| exit /b 0"
#else
                       "grep -q 'rm -rf' \"$AGENTTY_HOOK_PAYLOAD_FILE\" "
                       "&& { echo 'dangerous command blocked'; exit 1; } "
                       "|| exit 0"
#endif
                      }}}},
                   {"post_tool", {{{"match", "shell"},
                                   {"run", touch_cmd(marker)}}}},
               }.dump());
    approve_current(sandbox / ".agentty" / "hooks.json", home);
    check(!hooks::pending_approval(::agentty::IoAccess::grant()), "approved file no longer pending");

    // 宾语 调用 :允许,在火灾后。
    auto ok = hooks::run_pre_tool("shell", R"({"command":"ls -la"})");
    check(!ok.blocked, "benign bash allowed");
    hooks::run_post_tool("shell", R"({"command":"ls"})", "listing…");
    check(fs::exists(marker), "post_tool hook executed for approved file");

    // 危险调用:用钩子的尖端挡住作为理由.
    auto bad = hooks::run_pre_tool("shell", R"({"command":"rm -rf /tmp/x"})");
    check(bad.blocked, "matching pre_tool guard blocks");
    check(bad.reason.find("dangerous command blocked") != std::string::npos,
          "hook stdout becomes the block reason (got '" + bad.reason + "')");

    // 农用-matching工具:护卫不开火.
    auto other = hooks::run_pre_tool("read", R"({"path":"rm -rf"})");
    check(!other.blocked, "match=bash guard ignores the read tool");
    std::println("PASS\n");
}

void byte_change_regates(const fs::path& sandbox) {
    std::println("--- byte_change_regates ---");
    const fs::path marker = sandbox / "regate.marker";
    // 更改已核准文件( 添加空格 ) - 任意 字节 页:1
    const fs::path hf = sandbox / ".agentty" / "hooks.json";
    write_file(hf, nlohmann::json{
                       {"pre_tool", {{{"match", ""},
                                      {"run", touch_cmd(marker)}}}},
                   }.dump() + " ");
    check(hooks::pending_approval(::agentty::IoAccess::grant()),
          "byte change to an approved file re-gates (pending again)");
    (void)hooks::run_pre_tool("shell", "{}");
    check(!fs::exists(marker), "re-gated hook did not execute");
    std::println("PASS\n");
}

void kill_switch(const fs::path& sandbox, const fs::path& home) {
    std::println("--- kill_switch ---");
    const fs::path marker = sandbox / "killswitch.marker";
    write_file(sandbox / ".agentty" / "hooks.json",
               nlohmann::json{
                   {"pre_tool", {{{"match", ""},
                                  {"run", touch_cmd(marker)}}}},
               }.dump());
    approve_current(sandbox / ".agentty" / "hooks.json", home);
#ifdef _WIN32
    _putenv_s("AGENTTY_NO_HOOKS", "1");
#else
    ::setenv("AGENTTY_NO_HOOKS", "1", 1);
#endif
    check(!hooks::pending_approval(::agentty::IoAccess::grant()), "kill switch silences pending notice");
    (void)hooks::run_pre_tool("shell", "{}");
    check(!fs::exists(marker), "AGENTTY_NO_HOOKS=1 disables approved hooks");
#ifdef _WIN32
    _putenv_s("AGENTTY_NO_HOOKS", "");   // 空值删除
#else
    ::unsetenv("AGENTTY_NO_HOOKS");
#endif
    std::println("PASS\n");
}

// 超大个头 hooks.json 不可读作 "no hooks file".
//
// read_all 返回空的为 "past the cap"
// 完全如它所为 "missing" ,所以,
// 我说 `agentty hooks` 打印 "no hooks file"
// 用户文件坐在那里时 -- a PreToolUse 写给 BLOCK
// 没有什么是默默无闻的,没有说 源码树 这是最重要的一个: 它打开
// 了命令执行,失败是用户相信一个警卫没有武器。
void oversized_file_is_reported(const fs::path& sandbox) {
    std::println("--- oversized_file_is_reported ---");
    const fs::path cfg = sandbox / ".agentty" / "hooks.json";
    // 真实有效的钩子——通过64KiB盖加码.
    nlohmann::json j{
        {"pre_tool", {{{"match", ""}, {"run", "exit 2"}}}},
        {"pad", std::string(70u * 1024u, 'x')},
    };
    write_file(cfg, j.dump());
    check(fs::file_size(cfg) > 64u * 1024u, "fixture is past the cap");

    // 钩子是NOT运行的—— 这部分总是真实的,并且始终是真实的。
    check(!hooks::pending_approval(::agentty::IoAccess::grant()),
          "an unloadable file offers nothing to approve");

    // `agentty hooks` 必须 SAY 文件太大, 而不是声称没有。
    // 退出 1。 CI 通知 通知 已经通知了
    const int rc = hooks::cli(::agentty::IoAccess::grant(), agtest::cap(), "list");
    check(rc == 1, "hooks list exits non-zero for an oversized file");
    std::println("PASS\n");
}

} // 命名空间结束。

TEST_CASE("hooks_gate") {
    const fs::path sandbox =
        fs::temp_directory_path() / ("agentty_hooks_test_" +
                                     std::to_string(::getpid()));
    const fs::path home = sandbox / "home";
    fs::create_directories(home);
    fs::current_path(sandbox);
    // 用户 -scope 配置解析通过 util::user_root() ($AGENTTY_HOME ,
    // 其他则 home_dir()/.agentty ,在其中 home_dir ( ) 系 ,
    // ( ) $HOME 那样 $USERPROFILE )——点 全部 测试室的
    // 档案 home/.agentty 装填器读的。只设置 USERPROFILE 打开
    // Windows 输给继承人 $HOME (MSYS2, Git 贝什弹).
    ::setenv("HOME", home.string().c_str(), 1);
#ifdef _WIN32
    _putenv_s("USERPROFILE", home.string().c_str());
#endif
    ::unsetenv("AGENTTY_HOME");   // 倒车 home_dir()/.agentty
    // 钩子从这里穿过 沙箱 ,其模式和 工作区
    // 正在处理 -wide 并可能由较早的试验例
    // (有: 工作区 测试对象不是墙壁 而是同意门
    (void)agentty::tools::util::sandbox::init(agentty::tools::util::sandbox::Mode::Off);

    std::println("=== hooks_gate_test ===");
    unapproved_never_runs(sandbox);
    approved_runs_and_blocks(sandbox, home);
    byte_change_regates(sandbox);
    kill_switch(sandbox, home);
    oversized_file_is_reported(sandbox);

    std::error_code ec;
    fs::current_path(fs::temp_directory_path(), ec);
    fs::remove_all(sandbox, ec);
}
