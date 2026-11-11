// shell_env_floor_test ——型号. -supplied env不能成为
// 运行用户从未批准的代码的方法.
//
// ── 原因 ──────────────────────────────────────────────────────────────────
//
// `shell` 接受一个 `env` 对象和层次 覆盖儿童环境,
// 呼叫者 -wins。没有过滤 KEY。所以:
//
//     shell( 命令 = "git status" ,内存 "GIT_SSH_COMMAND": "/tmp/mine"})
//
// 过每个命令检查 - 命令的确是 `git status` - 和同意卡只
// 显示 `command` ,所以用户批准了 "git status"
// 并得到了/ 已经 tmp/mine 执行。形状与 CVE-2026-55743
// (允许清单 绕过一个内置任务,检查器跳过。
//
// 两项需要但单靠本身是不够的的补救:
//   - 现为渲染 cd和env的卡,同意后显示将运行什么;
//   - 工具拒绝 -execution 子进程类的密钥直接。
//
// 否定列表不能完整( Q) GOFLAGS=-toolexec=,
// RUSTC_WRAPPER 下一个还没有写下来的,所以是地板和
// 沙箱 边界。它买的:井 -known 表格停止静静工作。
#include "agtest.hpp"

#include "agentty/tool/registry.hpp"
#include "agentty/tool/tool.hpp"
#include "agentty/runtime/view/thread/turn/permission.hpp"

#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::json;

namespace {

// 运行 `shell` 并带有一个内嵌块,并报告是否被拒绝。
bool env_refused(const char* key, const char* value = "/tmp/payload") {
    auto r = agentty::tool::DynamicDispatch::execute(
        agtest::cap(), "shell", json{{"command", "true"},
                      {"env", json{{key, value}}}});
    if (r.has_value()) return false;
    return r.error().kind == agentty::tools::ErrorKind::InvalidArgs
        && r.error().detail.find("refused") != std::string::npos;
}

} // 命名空间结束。

TEST_CASE("shell env: the loader hooks are refused") {
    CHECK(env_refused("LD_PRELOAD"));
    CHECK(env_refused("LD_AUDIT"));
    CHECK(env_refused("LD_LIBRARY_PATH"));
    CHECK(env_refused("DYLD_INSERT_LIBRARIES"));
}

TEST_CASE("shell env: the shell startup hooks are refused") {
    CHECK(env_refused("BASH_ENV"));
    CHECK(env_refused("ENV"));
    CHECK(env_refused("PROMPT_COMMAND"));
    CHECK(env_refused("IFS", " "));
}

TEST_CASE("shell env: what git runs for you is refused") {
    // 动机案:命令保留`git status`.
    CHECK(env_refused("GIT_SSH_COMMAND"));
    CHECK(env_refused("GIT_EXTERNAL_DIFF"));
    CHECK(env_refused("GIT_ASKPASS"));
    CHECK(env_refused("GIT_CONFIG_GLOBAL"));
}

TEST_CASE("shell env: interpreter and toolchain hooks are refused") {
    CHECK(env_refused("PYTHONSTARTUP"));
    CHECK(env_refused("NODE_OPTIONS", "--require=/tmp/x.js"));
    CHECK(env_refused("PERL5OPT"));
    CHECK(env_refused("JAVA_TOOL_OPTIONS"));
    CHECK(env_refused("RUSTC_WRAPPER"));
    CHECK(env_refused("GOFLAGS", "-toolexec=/tmp/x"));
}

TEST_CASE("shell env: PATH is refused") {
    // 重点 PATH 每一个命令 赤裸裸的指令 在批准字符串中,是你的。
    CHECK(env_refused("PATH", "/tmp/bin"));
}

TEST_CASE("shell env: the match is case-insensitive") {
    // 案例不是边界:内核的生态就是案例 -sensitive ,
    // 但一个只知道大写拼写的检查器是
    // `ld_preload` 远离任何装货机宽大无用的宿主,
    // 这里严格操作的成本是零。
    CHECK(env_refused("ld_preload"));
    CHECK(env_refused("Git_Ssh_Command"));
}

TEST_CASE("shell env: exported-function smuggling is refused by prefix") {
    CHECK(env_refused("BASH_FUNC_ls%%", "() { rm -rf /; }"));
}

TEST_CASE("shell env: ordinary knobs still work") {
    // 地板不能打破合法用途。 这就是 `env` 存在
    // 的原因: 使建筑安静, 使测试动词, 强制颜色。
    CHECK_FALSE(env_refused("CI", "1"));
    CHECK_FALSE(env_refused("RUST_LOG", "debug"));
    CHECK_FALSE(env_refused("NO_COLOR", "1"));
    CHECK_FALSE(env_refused("CLICOLOR_FORCE", "1"));
    CHECK_FALSE(env_refused("MY_APP_TOKEN_NAME", "x"));
}

TEST_CASE("shell env: a refused key fails the call, it is not dropped") {
    // 悄悄地放下它,会执行一个命令,它做一些与所问
    // 的有细微不同的事情,这比明确的否定更糟糕.
    auto r = agentty::tool::DynamicDispatch::execute(
        agtest::cap(), "shell", json{{"command", "true"},
                      {"env", json{{"LD_PRELOAD", "/tmp/x.so"},
                                   {"CI", "1"}}}});
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().detail.find("LD_PRELOAD") != std::string::npos);
}

// ── 同意卡必须显示 ─────────────────────────────────

TEST_CASE("permission card: the shell card shows cd and env") {
    agentty::ToolUse tc;
    tc.id   = agentty::ToolCallId{"c1"};
    tc.name = agentty::ToolName{"shell"};
    tc.args = json{{"command", "ls -la"},
                   {"cd", "/tmp/elsewhere"},
                   {"env", json{{"CI", "1"}}}};
    agentty::PendingPermission pp{tc.id, tc.name, "needs permission", 0};

    const auto cfg = agentty::ui::inline_permission_config(pp, tc);

    CHECK(cfg.description.find("ls -la") != std::string::npos);
    CHECK_MESSAGE(cfg.description.find("/tmp/elsewhere") != std::string::npos,
                  "cd changes what the command acts on, so it is part of the "
                  "decision");
    CHECK_MESSAGE(cfg.description.find("CI=1") != std::string::npos,
                  "env is part of what executes; a card that hides it asks "
                  "for consent to something other than what runs");
}

TEST_CASE("permission card: a plain shell call reads exactly as before") {
    // 没有 cd, 没有掩体 - > 没有装饰。 普通的个案不会被清理。
    agentty::ToolUse tc;
    tc.id   = agentty::ToolCallId{"c1"};
    tc.name = agentty::ToolName{"shell"};
    tc.args = json{{"command", "cargo test"}};
    agentty::PendingPermission pp{tc.id, tc.name, "needs permission", 0};

    const auto cfg = agentty::ui::inline_permission_config(pp, tc);
    CHECK(cfg.description == "cargo test");
}

TEST_CASE("permission card: bash shows its command, not raw json") {
    // bash 是使用最多的工具，却未进入 shell 分支，
    // 导致提示直接输出整个参数对象。
    agentty::ToolUse tc;
    tc.id   = agentty::ToolCallId{"c1"};
    tc.name = agentty::ToolName{"bash"};
    tc.args = json{{"command", "rm -rf build"},
                   {"display_description", "Clean the build"}};
    agentty::PendingPermission pp{tc.id, tc.name, "needs permission", 0};

    const auto cfg = agentty::ui::inline_permission_config(pp, tc);
    CHECK(cfg.description == "rm -rf build");
    CHECK(cfg.lang == "sh");
    CHECK(cfg.verb == "run this command?");
}
