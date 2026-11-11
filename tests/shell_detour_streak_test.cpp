// 漂移提醒: 当 shell 绕道链时, 提示会从第三排的顶部
// 得到一个简单的提醒。 任何非-detour 调用 重排此串。
// 绕行 -ness 来源 analyze_detour().substitutable() ,
// 提示所用的同一门,所以一个构件或一个书写永远不会算数。

#include <chrono>
#include <string>

#include "agtest.hpp"

#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/model.hpp"

using agentty::Message;
using agentty::Model;
using agentty::Role;
using agentty::ToolCallId;
using agentty::ToolName;
using agentty::ToolUse;
using agentty::app::detail::apply_tool_output;

namespace {

// 按下一个运行 shell(或其他) 调用,并输入它的输出。
std::string run(Model& m, int n, const char* tool, const std::string& cmd,
                std::string out = "tip: something\n\nok") {
    Message msg;
    msg.role = Role::Assistant;
    ToolUse tc;
    tc.id   = ToolCallId{"c" + std::to_string(n)};
    tc.name = ToolName{tool};
    tc.args = {{"command", cmd}, {"path", "x"}};
    const auto now = std::chrono::steady_clock::now();
    tc.status = ToolUse::Running{now, {}, {}, now, 0};
    msg.tool_calls.push_back(std::move(tc));
    m.d.current.messages.push_back(std::move(msg));
    apply_tool_output(m, ToolCallId{"c" + std::to_string(n)}, std::move(out),
                      std::nullopt, {}, {}, 0);
    return m.d.current.messages.back().tool_calls.back().output();
}

}  // 命名空间结束。

TEST_CASE("shell detour streak adds a reminder from the third in a row") {
    Model m;
    check(run(m, 1, "shell", "cat a.cpp").find("[reminder]") == std::string::npos, "1st: no reminder");
    check(run(m, 2, "shell", "grep -rn x src").find("[reminder]") == std::string::npos, "2nd: no reminder");
    const auto third = run(m, 3, "shell", "sed -n 1,5p a.cpp");
    check(third.starts_with("[reminder] shell detour #3"), "3rd: reminder leads: " + third.substr(0, 60));
    check(third.find("tip: something") != std::string::npos, "3rd: tip kept under it");
    check(m.d.shell_detour_streak == 3, "streak counted");

    // 本地人 本地人 调用 重新发售它。
    run(m, 4, "read", "", "file body");
    check(m.d.shell_detour_streak == 0, "native call resets");
    check(run(m, 5, "shell", "cat b").find("[reminder]") == std::string::npos, "after reset: quiet again");

    // 真正的壳也重新装配它, 从来不算作绕道.
    run(m, 6, "shell", "cat c");
    run(m, 7, "shell", "cmake --build build -j12", "ok");
    check(m.d.shell_detour_streak == 0, "build resets");
    run(m, 8, "shell", "sed -i 's/a/b/' f", "ok");
    check(m.d.shell_detour_streak == 0, "a write never counts");
}
