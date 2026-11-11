// acp_agents_test ——通用,配置 -driven 外部 ACP 注册表.

#include "agentty/provider/acp_agents.hpp"
#include "agentty/provider/selection.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#if !defined(_WIN32)
#include <unistd.h>
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
namespace {
// 文件只需要一个 -process 独有的名字; pid是 posix 获得一个的方法,
// GetCurrentProcessId 同一个地产,没有额外的道具
[[nodiscard]] int local_pid() noexcept {
    return static_cast<int>(::GetCurrentProcessId());
}
}  // 命名空间结束。
#endif

#include "agtest.hpp"

namespace fs = std::filesystem;
namespace P = agentty::provider;

TEST_CASE("no builtin acp agents when unconfigured") {
    ::unsetenv("AGENTTY_ACP_AGENTS");

    CHECK(!P::is_acp_agent_id("claude-agent-acp"));
    CHECK(!P::is_acp_agent_id("codex-acp"));
    CHECK(!P::resolve_acp_agent("claude-agent-acp").has_value());
    CHECK(!P::resolve_acp_agent("codex-acp").has_value());
    CHECK(P::enumerate_acp_agents().empty());

    // 未配置 ACP-looking 名称不能静默地生成子进程或被误读为
    // OpenAI 主办单位名称; 过期 已保存 选择会安全地退后。
    CHECK(P::parse_selection("claude-agent-acp").kind == P::Kind::Anthropic);
    CHECK(P::parse_selection("codex-acp").kind == P::Kind::Anthropic);
    CHECK(P::parse_selection("anthropic").kind == P::Kind::Anthropic);
    CHECK(P::parse_selection("").kind == P::Kind::Anthropic);
    CHECK(P::parse_selection("openai").kind == P::Kind::OpenAI);
}

TEST_CASE("configured acp agents resolve") {
    fs::path tmp = fs::temp_directory_path() /
        ("acp-agents-" + std::to_string(
#if defined(_WIN32)
             local_pid()
#else
             ::getpid()
#endif
         ) + ".json");
    {
        std::ofstream f(tmp);
        f << R"({
          "acpAgents": {
            "my-agent": {
              "command": "my-acp",
              "args": ["serve", "--stdio"],
              "env": { "AGENT_LOG": "1" },
              "cwd": "/work"
            },
            "codex-acp": { "command": "codex-acp", "args": ["acp"] }
          }
        })";
    }
    ::setenv("AGENTTY_ACP_AGENTS", tmp.string().c_str(), 1);

    CHECK(P::is_acp_agent_id("my-agent"));
    auto mine = P::resolve_acp_agent("my-agent");
    CHECK(mine.has_value());
    if (mine) {
        CHECK(mine->command == "my-acp");
        CHECK(mine->args.size() == 2);
        CHECK(mine->cwd == "/work");
        CHECK(mine->env.size() == 1);
        auto argv = mine->argv();
        CHECK(argv.size() == 3);
        CHECK(argv[0] == "my-acp");
        CHECK(argv[1] == "serve");
    }

    CHECK(P::is_acp_agent_id("codex-acp"));
    auto selection = P::parse_selection("codex-acp");
    CHECK(selection.kind == P::Kind::ExternalAcp);
    CHECK(selection.acp_agent_id == "codex-acp");
    CHECK(P::provider_display_name(selection) == "codex-acp");

    auto agents = P::enumerate_acp_agents();
    CHECK(agents.size() == 2);
    bool has_mine = false, has_codex = false;
    for (const auto& a : agents) {
        if (a.id == "my-agent") has_mine = true;
        if (a.id == "codex-acp") has_codex = true;
    }
    CHECK(has_mine);
    CHECK(has_codex);

    ::unsetenv("AGENTTY_ACP_AGENTS");
    std::error_code ec;
    fs::remove(tmp, ec);
}
