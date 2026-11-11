// 精通数据-level 覆盖本土工具时间表卡.
// 每个生命周期状态都必须 渲染 有用 正文 文本而不是空白卡片。

#include <array>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "agtest.hpp"

#include "agentty/domain/conversation.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/app/update.hpp"
#include "agentty/runtime/panel/tool_output.hpp"
#include "agentty/runtime/view/panels.hpp"
#include "agentty/runtime/view/thread/turn/agent_timeline/tool_body_preview.hpp"
#include "agentty/runtime/view/thread/turn/agent_timeline/tool_helpers.hpp"
#include "agentty/runtime/view/thread/turn/agent_timeline/tool_args.hpp"
#include "maya/widget/tool_body_preview.hpp"
#include <maya/render/canvas.hpp>
#include <maya/render/renderer.hpp>
#include <maya/style/theme.hpp>

namespace A = agentty;
namespace U = agentty::ui;
using Kind = maya::ToolBodyPreview::Kind;



static A::ToolUse make_tool(std::string name, A::ToolUse::Status status,
                            nlohmann::json args = nlohmann::json::object()) {
    static int sequence = 0;
    A::ToolUse tc;
    tc.id = A::ToolCallId{"card-" + std::to_string(++sequence)};
    tc.name = A::ToolName{std::move(name)};
    tc.args = std::move(args);
    tc.status = std::move(status);
    return tc;
}

static bool visible(const maya::ToolBodyPreview::Config& body) {
    switch (body.kind) {
    case Kind::None:     return false;
    case Kind::EditDiff: return !body.hunks.empty();
    case Kind::TodoList: return !body.todos.empty();
    default:             return !body.text.empty();
    }
}

TEST_CASE("tool timeline adapter") {
    static constexpr std::array<const char*, 33> native_tools = {
        "read", "edit", "write", "move", "remove", "shell",
        "process_start", "process_poll", "process_stop",
        "grep", "glob", "list_dir", "repo_map", "todo",
        "web_fetch", "web_search", "find_definition", "search_structural",
        "diagnostics", "test", "git_status", "git_diff", "git_log",
        "git_show", "git_blame", "git_commit", "remember", "forget",
        "wipe_memory", "task", "skill", "search_docs", "search_code"
    };

    for (const char* name : native_tools) {
        auto expect_header_only = [&](A::ToolUse tc, const char* state) {
            const auto body = U::tool_body_preview_config(tc);
            check(!visible(body) && !U::tool_display_name(name).empty(),
                  std::string{name} + " " + state
                      + " stays zero-row with a useful event header");
        };
        auto expect_visible = [&](A::ToolUse tc, const char* state) {
            const auto body = U::tool_body_preview_config(tc);
            check(visible(body), std::string{name} + " " + state + " has a body");
        };

        expect_header_only(make_tool(name, A::ToolUse::Pending{}), "pending");
        expect_header_only(make_tool(name, A::ToolUse::Approved{}), "approved");
        expect_header_only(make_tool(name, A::ToolUse::Running{}), "silent running");

        auto live = make_tool(name, A::ToolUse::Running{});
        std::get<A::ToolUse::Running>(live.status).progress_text = "live progress";
        const auto live_body = U::tool_body_preview_config(live);
        check(visible(live_body) && live_body.text == "live progress"
                  && live_body.is_streaming,
              std::string{name} + " exposes live progress");

        expect_header_only(make_tool(name, A::ToolUse::Done{}), "empty success");
        expect_visible(make_tool(name, A::ToolUse::Done{{}, {}, "result text"}),
                       "success output");

        const auto empty_failure = U::tool_body_preview_config(
            make_tool(name, A::ToolUse::Failed{}));
        check(!visible(empty_failure),
              std::string{name} + " empty failure stays header-only");

        const auto failure = U::tool_body_preview_config(
            make_tool(name, A::ToolUse::Failed{{}, {}, "specific error"}));
        check(visible(failure) && failure.text.find("specific error") != std::string::npos,
              std::string{name} + " preserves failure output");

        expect_header_only(make_tool(name, A::ToolUse::Rejected{}), "rejected");
        check(!U::tool_display_name(name).empty(),
              std::string{name} + " has a display name");
    }

    // 未来土著和 MCP 工具继承相同的 回退 不需要
    // 添加到 分派器 名字的单子
    auto external = make_tool("mcp__example__long_operation", A::ToolUse::Running{},
                              {{"display_description", "Long MCP operation"}});
    auto external_body = U::tool_body_preview_config(external);
    check(!visible(external_body)
              && U::tool_timeline_detail(external) == "Long MCP operation",
          "unknown silent MCP tool uses its stable event header");
    check(U::tool_display_name("tool_search") == "Tool Search"
              && U::tool_timeline_detail(make_tool("tool_search",
                  A::ToolUse::Running{}, {{"query", "browser automation"}}))
                  == "browser automation",
          "tool search has a useful label and query");
    check(U::tool_display_name("mcp__prompts__get") == "MCP Prompt"
              && U::tool_timeline_detail(make_tool("mcp__prompts__get",
                  A::ToolUse::Running{}, {{"name", "summarize"}}))
                  == "summarize",
          "MCP prompt broker names its prompt");
    check(U::tool_display_name("mcp__github__search_issues") == "Search Issues \xc2\xb7 github",
          "an MCP tool is named by its tool, then its server");
    check(U::tool_timeline_detail(make_tool("mcp__github__search_issues",
              A::ToolUse::Running{}, {{"q", "crash on start"}}))
              == "q crash on start",
          "an MCP tool with no description shows its args");

    auto invisible_live = make_tool("mcp__example__silent", A::ToolUse::Running{},
                                    {{"display_description", "Silent MCP wait"}});
    std::get<A::ToolUse::Running>(invisible_live.status).progress_text =
        "\x1b[2K\r\n\t";
    const auto invisible_live_body = U::tool_body_preview_config(invisible_live);
    check(!visible(invisible_live_body)
              && U::tool_timeline_detail(invisible_live) == "Silent MCP wait",
          "ANSI-only progress falls back to the stable event header");

    const auto whitespace_done = U::tool_body_preview_config(make_tool(
        "mcp__example__silent", A::ToolUse::Done{{}, {}, " \n\t"}));
    check(!visible(whitespace_done),
          "whitespace-only success stays header-only");

    const auto ansi_failure = U::tool_body_preview_config(make_tool(
        "mcp__example__silent", A::ToolUse::Failed{{}, {}, "\x1b[31m\x1b[0m"}));
    check(!visible(ansi_failure),
          "ANSI-only failure stays header-only");

    // 结构化的渲染器在具有有意义的内容时仍然会获胜.
    auto read = make_tool("read", A::ToolUse::Done{{}, {}, "one\ntwo"},
                          {{"path", "sample.cpp"}});
    check(U::tool_body_preview_config(read).kind == Kind::FileRead,
          "read keeps structured file body");

    // 真实行号: 页脚从 正文 地雷的地雷和
    // 为 TRUE 第一行(授权于参数回声).
    {
        auto tc = make_tool("read",
            A::ToolUse::Done{{}, {},
                "alpha\nbeta\n\n[showing lines 40-41 of 90; 49 more \xe2\x80\x94 "
                "pass offset=42 for the next chunk]"},
            {{"path", "sample.cpp"}});
        const auto body = U::tool_body_preview_config(tc);
        check(body.kind == Kind::FileRead && body.start_line == 40,
              "read footer anchors gutter at true start line");
        check(body.text.find("[showing lines") == std::string::npos,
              "read footer stripped from numbered body");
    }
    // 符号 全文如下: SUCCESS 头文件 被剥离,
    // 并剥离 `(lines A\u2013B)` 区域锁定沟渠,即使没有抵消的参数。
    {
        auto tc = make_tool("read",
            A::ToolUse::Done{{}, {},
                "SUCCESS: `foo` defined at f.cpp:12 (lines 12\xe2\x80\x93 14).\n\n"
                "int foo() {\n  return 1;\n}\n"},
            {{"path", "f.cpp"}, {"symbol", "foo"}});
        const auto body = U::tool_body_preview_config(tc);
        check(body.kind == Kind::FileRead && body.start_line == 12,
              "symbol read anchors gutter at definition line");
        check(body.text.rfind("int foo", 0) == 0,
              "symbol header stripped from numbered body");
        check(U::tool_timeline_detail(tc).find("foo()") != std::string::npos,
              "read header names the symbol read");
    }

    // 将 grep 的 Markdown 重新组织为 path:line:text。
    // for the structure GrepMatches table,其中的行号来源于
    // `### L<s>-<e>`块标记.
    {
        auto tc = make_tool("grep",
            A::ToolUse::Done{{}, {},
                "Found 2 matches across 1 file.\n\n"
                "## Matches in src/a.cpp\n\n"
                "### fn foo \xe2\x80\xba L41-43\n```\n"
                "int x = 1;\nint y = 2;\nint z = 3;\n```\n\n"
                "Showing all 2 matches."},
            {{"pattern", "int"}});
        const auto body = U::tool_body_preview_config(tc);
        check(body.kind == Kind::GrepMatches,
              "grep renders the structured match table");
        check(body.text.find("src/a.cpp:41:int x = 1;") != std::string::npos
                  && body.text.find("src/a.cpp:43:int z = 3;") != std::string::npos,
              "grep rows carry true derived line numbers");
        check(body.text.find("Showing all") == std::string::npos
                  && body.text.find("Found 2") == std::string::npos,
              "grep headline/pagination chatter dropped from body");
    }
    // 无法解析的grep输出(无栅栏)仍可读取为CodeBlock.
    {
        auto tc = make_tool("grep",
            A::ToolUse::Done{{}, {}, "No matches for 'zzz'. Try: loosen"},
            {{"pattern", "zzz"}});
        check(U::tool_body_preview_config(tc).kind == Kind::CodeBlock,
              "fenceless grep output falls back to code block");
    }

    // 进程 QQ 共享的 shash 终端-output 正文 (末尾 窗口,
    // 结构提取),而不是通用头 -anchored 回退.
    {
        auto tc = make_tool("process_poll",
            A::ToolUse::Done{{}, {}, "server listening on :8080\n"});
        check(U::tool_body_preview_config(tc).kind == Kind::BashOutput,
              "process_poll output renders as terminal output");
    }

    // 替换干燥 -run 预览重塑成彩色 GitDiff 纸牌。
    {
        auto tc = make_tool("replace",
            A::ToolUse::Done{{}, {},
                "DRY RUN \xe2\x80\x94 would replace 1 occurrence of `foo` \xe2\x86\x92 `bar` "
                "across 1 file.\n\n"
                "src/a.cpp:12  (1 hit)\n"
                "  - int foo = 1;\n"
                "  + int bar = 1;\n\n"
                "Re-run with apply:true to write these changes."},
            {{"find", "foo"}, {"replacement", "bar"}});
        const auto body = U::tool_body_preview_config(tc);
        check(body.kind == Kind::GitDiff, "replace preview renders as a diff");
        check(body.text.find("-int foo = 1;") != std::string::npos
                  && body.text.find("+int bar = 1;") != std::string::npos,
              "replace preview keeps the -/+ lines line-leading for GitDiff");
        check(body.text.find("DRY RUN") == std::string::npos,
              "replace summary line dropped from the diff body");
    }
    // rewrite_structural 预览 : -line L<n> :主播成为QQ L <n> @@.
    {
        auto tc = make_tool("rewrite_structural",
            A::ToolUse::Done{{}, {},
                "DRY RUN \xe2\x80\x94 would rewrite 1 occurrence across 1 file.\n"
                "src/b.cpp:\n"
                "  L42: - assertEquals(a, b)\n"
                "  L42: + assertEqual(b, a)\n"},
            {{"pattern", "assertEquals($A, $B)"}});
        const auto body = U::tool_body_preview_config(tc);
        check(body.kind == Kind::GitDiff,
              "rewrite_structural preview renders as a diff");
        check(body.text.find("@@ L42 @@") != std::string::npos,
              "rewrite_structural line number becomes a hunk anchor");
        check(body.text.find("-assertEquals(a, b)") != std::string::npos
                  && body.text.find("+assertEqual(b, a)") != std::string::npos,
              "rewrite_structural -/+ lines are line-leading");
    }
    // A 'no matches' 替换信件无 -/+ 预览 – 保留一个代码块。
    {
        auto tc = make_tool("replace",
            A::ToolUse::Done{{}, {},
                "no structural matches \xe2\x80\x94 nothing to rewrite."},
            {{"find", "zzz"}, {"replacement", "q"}});
        check(U::tool_body_preview_config(tc).kind == Kind::CodeBlock,
              "replace with no preview falls back to code block");
    }

    auto todo = make_tool("todo", A::ToolUse::Running{},
        {{"todos", {{{"content", "Audit cards"}, {"status", "in_progress"}}}}});
    check(U::tool_body_preview_config(todo).kind == Kind::TodoList,
          "todo keeps structured checklist");

    auto failed_todo = make_tool("todo", A::ToolUse::Failed{{}, {}, "todo failed"},
        {{"todos", {{{"content", "Audit cards"}, {"status", "in_progress"}}}}});
    check(U::tool_body_preview_config(failed_todo).kind == Kind::Failure,
          "failed todo shows error instead of stale checklist");

    // 代表性细节绘图证明以前是匿名工具,
    // 解释它们在事件 头文件的运作,而回退 正文则解释
    // 生命周期状态.
    check(U::tool_timeline_detail(make_tool("move", A::ToolUse::Running{},
              {{"source", "a"}, {"destination", "b"}})).find("a") != std::string::npos,
          "move detail includes source");
    check(U::tool_timeline_detail(make_tool("search_structural", A::ToolUse::Running{},
              {{"pattern", "Widget($$$)"}, {"path", "src"}})).find("Widget") != std::string::npos,
          "structural detail includes pattern");
    check(U::tool_timeline_detail(make_tool("search_code", A::ToolUse::Running{},
              {{"query", "retry handling"}})) == "retry handling",
          "semantic search detail includes query");
    check(!U::tool_timeline_detail(make_tool("remove", A::ToolUse::Pending{},
              {{"path", "tmp"}, {"recursive", "not-a-boolean"}})).empty(),
          "malformed remove boolean cannot break rendering");
    check(U::tool_timeline_detail(make_tool("wipe_memory", A::ToolUse::Pending{},
              {{"scope", "project"}, {"confirm", nullptr}})).find("preview")
              != std::string::npos,
          "malformed wipe confirmation cannot break rendering");
    check(!U::tool_timeline_detail(make_tool("search_code", A::ToolUse::Pending{},
              {{"query", nlohmann::json::array({"wrong type"})}})).empty(),
          "malformed string argument cannot break rendering");
    check(!U::tool_timeline_detail(make_tool("git_blame", A::ToolUse::Pending{},
              {{"path", "file.cpp"}, {"start_line", "wrong type"}})).empty(),
          "malformed integer argument cannot break rendering");

    // 失败摘要:一个失败的工具在其中显示出一个简单的原因 头文件 细节,
    // 所以它看起来不像成功。 "[kind] detail"
    // 错误显示 KIND ; 闪电车预告片 "exit code N" ;
    // 赤裸的错误显示其第一行。拒绝的工具保持不变。
    check(U::tool_timeline_detail(make_tool("edit", A::ToolUse::Failed{{}, {},
              "[not found] old_text did not match"},
              {{"path", "x.cpp"}})).find("not found") != std::string::npos,
          "failed edit surfaces the error kind in the header");
    check(U::tool_timeline_detail(make_tool("shell", A::ToolUse::Failed{{}, {},
              "boom\n[exit code 1]"},
              {{"command", "false"}})).find("exit code 1") != std::string::npos,
          "failed bash surfaces the exit code in the header");
    check(U::tool_timeline_detail(make_tool("read", A::ToolUse::Failed{{}, {},
              "permission denied"},
              {{"path", "x"}})).find("permission denied") != std::string::npos,
          "failed read surfaces an unstructured reason");
    {
        // 一个没有输出的失败工具仍然获得'failed'标记,
        // 而不是看起来成功.
        const auto d = U::tool_timeline_detail(
            make_tool("grep", A::ToolUse::Failed{}, {{"pattern", "x"}}));
        check(d.find("failed") != std::string::npos,
              "failed tool with empty output still marked failed");
    }
    {
        // 一个成功的工具永远不会得到失败后缀.
        const auto d = U::tool_timeline_detail(make_tool("read",
            A::ToolUse::Done{{}, {}, "ok"}, {{"path", "x.cpp"}}));
        check(d.find("failed") == std::string::npos
                  && d.find("[") == std::string::npos,
              "successful tool has no failure suffix");
    }
    check(U::tool_timeline_detail(make_tool("read", A::ToolUse::Rejected{},
              {{"path", "x.cpp"}})).find("failed") == std::string::npos,
          "rejected tool is not labelled failed");

    // Line/entry 计数必须反映 FILE/DIR ,而不是装饰输出。
    // 读取:权威计数是 "of N" 工具报告
    // "[showing lines A-B of N]" 脚步——不是后
    // -count 输出行(包括 display_description + 脚本身)。
    {
        // 这么大 -file 改为: 3 个可见行 正文 但脚印上写着812
        std::string out = "describe this\n1: line one\n2: line two\n3: line "
                          "three\n[showing lines 1-3 of 812; pass offset=4]";
        const auto d = U::tool_timeline_detail(make_tool("read",
            A::ToolUse::Done{{}, {}, out}, {{"path", "big.cpp"},
            {"display_description", "describe this"}}));
        check(d.find("1\xe2\x80\x93" "3 of 812") != std::string::npos,
              "read reports the range shown of the file's true length: " + d);
        check(d.find("5 lines") == std::string::npos,
              "read does not count the description + footer rows");
    }
    {
        // 大纲 回退 形状 : "path — N definitions across M lines:".
        std::string out = "map it\nfoo.cpp \xe2\x80\x94 4 definitions across "
                          "296 lines:\n\nL1 a\nL2 b";
        const auto d = U::tool_timeline_detail(make_tool("read",
            A::ToolUse::Done{{}, {}, out}, {{"path", "foo.cpp"}}));
        check(d.find("296 lines") != std::string::npos,
              "read outline reports the 'across N lines' count");
    }
    {
        // 小文件读全(无页脚):倒回内容行.
        std::string out = "a\nb\nc";
        const auto d = U::tool_timeline_detail(make_tool("read",
            A::ToolUse::Done{{}, {}, out}, {{"path", "tiny.txt"}}));
        check(d.find("3 lines") != std::string::npos,
              "small whole-file read falls back to a content line count");
    }
    {
        // list_dir : 说明预置+必须发出调试通知
        // 不 算作条目。这里有三行正文。
        std::string out = "listing\nfile a.txt\ndir  sub/\nfile b.txt\n"
                          "[>1000 entries, truncated]";
        const auto d = U::tool_timeline_detail(make_tool("list_dir",
            A::ToolUse::Done{{}, {}, out}, {{"path", "."},
            {"display_description", "listing"}}));
        check(d.find("3 entries") != std::string::npos,
              "list_dir counts real entries, excluding desc + notice lines");
    }

    // 编辑 -file 可见度: 文件名必须显示 DURING
    // 流式传输 在节奏加速前 待处理 编辑其解析的参数仍然空,
    // 但其原始 数据流 已载 "path" 必须 渲染 道路,
    // 不是空的 "…".
    {
        auto tc = make_tool("edit", A::ToolUse::Pending{});
        tc.args = nlohmann::json::object();   // 解析 还没赶上呢
        tc.args_streaming =
            R"({"display_description":"fix","path":"src/foo.cpp","edits":[)";
        const auto d = U::tool_timeline_detail(tc);
        check(d.find("src/foo.cpp") != std::string::npos,
              "edit shows the streaming path before args reparse");
        check(d != "\xe2\x80\xa6", "edit is not a bare ellipsis while streaming");
    }
    {
        // 中点断线 -path :显示流经至今的事物而不是无.
        auto tc = make_tool("write", A::ToolUse::Pending{});
        tc.args_streaming = R"({"path":"src/partial)";
        check(U::tool_timeline_detail(tc).find("src/partial") != std::string::npos,
              "write shows a truncated streaming path");
    }
    {
        // 解析的参数胜出一次出现(权威),数据流忽略.
        auto tc = make_tool("read", A::ToolUse::Done{{}, {}, "a\nb"},
                            {{"path", "real.cpp"}});
        tc.args_streaming = R"({"path":"stale.cpp"})";
        check(U::tool_timeline_detail(tc).find("real.cpp") != std::string::npos,
              "parsed path is authoritative over the streaming buffer");
    }
    {
        // 移动 : source/destination 从“可看见的” 数据流 也一样。
        auto tc = make_tool("move", A::ToolUse::Pending{});
        tc.args_streaming = R"({"source":"a.txt","destination":"b.txt"})";
        const auto d = U::tool_timeline_detail(tc);
        check(d.find("a.txt") != std::string::npos
                  && d.find("b.txt") != std::string::npos,
              "move shows streaming source and destination");
    }
    // pick_streaming_string 逃脱 + 密钥 -vs-value 处理。
    check(U::pick_streaming_string(R"({"path":"a\/b\nc"})", "path") == "a/b\nc",
          "pick_streaming_string decodes escapes");
    check(U::pick_streaming_string(R"({"x":"path","path":"real"})", "path")
              == "real",
          "pick_streaming_string finds the KEY, not a same-named value");
    check(U::pick_streaming_string(R"({"other":"x"})", "path").empty(),
          "pick_streaming_string returns empty when key absent");

    // 每一个工具 头文件 参数为 流式传输-live ,而不只是文件路径:
    // a command/pattern/query 显示其时刻 字节 到了,
    // 在解析之前 -args 复式(同一机制,通过 safe().)
    {
        auto tc = make_tool("shell", A::ToolUse::Pending{});
        tc.args_streaming = R"({"command":"npm run build)";  // 切换
        check(U::tool_timeline_detail(tc).find("npm run build") != std::string::npos,
              "bash command shows while streaming");
    }
    {
        auto tc = make_tool("grep", A::ToolUse::Pending{});
        tc.args_streaming = R"({"pattern":"TODO","path":"src)";
        check(U::tool_timeline_detail(tc).find("TODO") != std::string::npos,
              "grep pattern shows while streaming");
    }
    {
        auto tc = make_tool("search_code", A::ToolUse::Pending{});
        tc.args_streaming = R"({"query":"retry backoff)";
        check(U::tool_timeline_detail(tc).find("retry backoff") != std::string::npos,
              "search query shows while streaming");
    }

    const nlohmann::json malformed_edit_args = {{"edits", {
        {{"old_text", nlohmann::json::array()}, {"new_text", nullptr}}
    }}};
    for (auto tc : {
            make_tool("edit", A::ToolUse::Pending{}, malformed_edit_args),
            make_tool("edit", A::ToolUse::Running{}, malformed_edit_args),
            make_tool("edit", A::ToolUse::Done{{}, {}, "done"}, malformed_edit_args)}) {
        const auto body = U::tool_body_preview_config(tc);
        check(visible(body) || !U::tool_timeline_detail(tc).empty(),
              "malformed nested edit fields remain renderable");
    }

    const nlohmann::json malformed_todo_args = {{"todos", {
        {{"content", nlohmann::json::array()}, {"status", nullptr}}
    }}};
    auto malformed_todo = make_tool("todo", A::ToolUse::Running{}, malformed_todo_args);
    check((visible(U::tool_body_preview_config(malformed_todo))
               || !U::tool_timeline_detail(malformed_todo).empty()),
          "malformed nested todo fields remain renderable");

    auto huge_read = make_tool("read", A::ToolUse::Done{{}, {}, "line"},
        {{"path", "file.cpp"},
         {"offset", std::numeric_limits<std::uint64_t>::max()}});
    check(U::tool_body_preview_config(huge_read).start_line == 1,
          "out-of-range read offset safely uses default line");

    // 练习真正的 覆盖面板 适配器,不仅指通用 选择面板 旧的60
    // -column 楼层溢出 phone/SSH 终端 之前,弹性可以帮助。
    A::Model viewer_model;
    A::tool_output::Entry viewer_entry;
    viewer_entry.name = "shell";
    viewer_entry.title = "Diagnostics";
    viewer_entry.detail = "cmake --build a-very-long-target-name";
    viewer_entry.trailing = "running · 41.2s · 1 MB";
    viewer_entry.output = "line one\nline two\nline three";
    viewer_entry.is_live = true;
    viewer_entry.call = make_tool("shell", A::ToolUse::Running{});
    viewer_model.ui.panel.descend(A::ui::panel::ToolOutput{{{viewer_entry}, 0, false}});
    int list_height = -1;
    for (int width = 8; width <= 120; ++width) {
        auto viewer = U::tool_output_panel(viewer_model);
        auto constrained = maya::dsl::vstack()
            .width(maya::Dimension::fixed(width))(viewer);
        auto measured = maya::measure_element(constrained, width);
        check(measured.width.value <= width,
              "tool viewer list fits width " + std::to_string(width));
        if (list_height < 0) list_height = measured.height.value;
        check(measured.height.value == list_height,
              "tool viewer list never wraps at width " + std::to_string(width));
    }
    viewer_model.ui.panel.get<A::ui::panel::ToolOutput>()->viewing = true;
    int body_height = -1;
    for (int width = 8; width <= 120; ++width) {
        auto viewer = U::tool_output_panel(viewer_model);
        auto constrained = maya::dsl::vstack()
            .width(maya::Dimension::fixed(width))(viewer);
        auto measured = maya::measure_element(constrained, width);
        check(measured.width.value <= width,
              "tool viewer body fits width " + std::to_string(width));
        if (body_height < 0) body_height = measured.height.value;
        check(measured.height.value == body_height,
              "tool viewer body never wraps at width " + std::to_string(width));
    }

    std::vector<A::ToolUse> structured_calls;
    structured_calls.push_back(make_tool("read",
        A::ToolUse::Done{{}, {}, "first line\nsecond line\nthird line"},
        {{"path", "very-long-file-name.cpp"}}));
    structured_calls.push_back(make_tool("write", A::ToolUse::Done{},
        {{"path", "generated.cpp"},
         {"content", "a very long generated source line that must clip\nnext"}}));
    structured_calls.push_back(make_tool("edit", A::ToolUse::Done{{}, {}, "done"},
        {{"path", "edited.cpp"},
         {"edits", {{{"old_text", "old long value"},
                      {"new_text", "new long replacement value"}}}}}));
    structured_calls.push_back(make_tool("git_diff",
        A::ToolUse::Done{{}, {}, "@@ -1 +1 @@\n-old value\n+new value"}));
    structured_calls.push_back(make_tool("todo", A::ToolUse::Done{},
        {{"todos", {{{"content", "A long todo item that cannot wrap"},
                     {"status", "completed"}}}}}));

    static constexpr std::array<int, 7> viewer_widths = {8, 12, 20, 32, 48, 80, 120};
    for (auto& call : structured_calls) {
        A::tool_output::Entry entry;
        entry.name = call.name.value;
        entry.title = U::tool_display_name(entry.name);
        entry.detail = "structured output";
        entry.trailing = "ok · 1.0s";
        entry.output = call.output();
        entry.call = call;
        A::Model structured_model;
        structured_model.ui.panel.descend(
            A::ui::panel::ToolOutput{{{std::move(entry)}, 0, true}});
        int expected_height = -1;
        for (int width : viewer_widths) {
            auto viewer = U::tool_output_panel(structured_model);
            auto constrained = maya::dsl::vstack()
                .width(maya::Dimension::fixed(width))(viewer);
            auto measured = maya::measure_element(constrained, width);
            if (expected_height < 0) expected_height = measured.height.value;
            check(measured.width.value <= width
                      && measured.height.value == expected_height,
                  call.name.value + " structured body fits width "
                      + std::to_string(width));
        }
    }

    for (bool live : {false, true}) {
        A::tool_output::Entry empty;
        empty.name = "mcp__example__silent";
        empty.title = "Silent MCP Operation";
        empty.detail = "waiting for remote service";
        empty.trailing = live ? "running" : "ok";
        empty.is_live = live;
        empty.call = live
            ? make_tool(empty.name, A::ToolUse::Running{})
            : make_tool(empty.name, A::ToolUse::Done{});
        A::Model empty_model;
        empty_model.ui.panel.descend(
            A::ui::panel::ToolOutput{{{std::move(empty)}, 0, true}});
        int expected_height = -1;
        for (int width : viewer_widths) {
            auto viewer = U::tool_output_panel(empty_model);
            auto constrained = maya::dsl::vstack()
                .width(maya::Dimension::fixed(width))(viewer);
            auto measured = maya::measure_element(constrained, width);
            if (expected_height < 0) expected_height = measured.height.value;
            check(measured.width.value <= width
                      && measured.height.value == expected_height,
                  std::string{live ? "live" : "settled"}
                      + " empty body fits width " + std::to_string(width));
        }
    }

    // 长 正文 添加范围 -indicator 正常高度时加脚,
    // 扫描短小的非 -TTY 终端 并确保紧凑的铬保持完整的边界,
    // 包括旧的继承的四条边界。 -row-floor 失败时间 10/11.
    const char* old_lines_raw = std::getenv("LINES");
    const std::string old_lines = old_lines_raw ? old_lines_raw : "";
    A::tool_output::Entry long_entry;
    long_entry.name = "shell";
    long_entry.title = "Bash";
    long_entry.detail = "long output";
    long_entry.trailing = "ok · 20 rows";
    for (int i = 0; i < 20; ++i)
        long_entry.output += "output row " + std::to_string(i) + "\n";
    long_entry.call = make_tool("shell",
        A::ToolUse::Done{{}, {}, long_entry.output});
    A::Model short_terminal_model;
    short_terminal_model.ui.panel.descend(
        A::ui::panel::ToolOutput{{{std::move(long_entry)}, 0, true}});
    static constexpr std::array<int, 4> short_heights = {8, 10, 11, 12};
    for (int height : short_heights) {
        const auto height_text = std::to_string(height);
#ifdef _WIN32
        _putenv_s("LINES", height_text.c_str());
#else
        setenv("LINES", height_text.c_str(), 1);
#endif
        auto short_viewer = U::tool_output_panel(short_terminal_model);
        auto short_constrained = maya::dsl::vstack()
            .width(maya::Dimension::fixed(40))(short_viewer);
        const auto short_size = maya::measure_element(short_constrained, 40);
        check(short_size.height.value <= height,
              "long body fits a " + height_text + "-row terminal");
    }

    std::vector<A::tool_output::Entry> short_list_entries;
    for (int i = 0; i < 4; ++i) {
        A::tool_output::Entry entry;
        entry.name = "shell";
        entry.title = "Diagnostics";
        entry.detail = "operation " + std::to_string(i);
        entry.trailing = "ok";
        entry.call = make_tool("shell", A::ToolUse::Done{});
        short_list_entries.push_back(std::move(entry));
    }
    A::Model short_list_model;
    short_list_model.ui.panel.descend(
        A::ui::panel::ToolOutput{{std::move(short_list_entries), 0, false}});
    for (int height : short_heights) {
        const auto height_text = std::to_string(height);
#ifdef _WIN32
        _putenv_s("LINES", height_text.c_str());
#else
        setenv("LINES", height_text.c_str(), 1);
#endif
        auto short_list = U::tool_output_panel(short_list_model);
        auto constrained = maya::dsl::vstack()
            .width(maya::Dimension::fixed(40))(short_list);
        const auto size = maya::measure_element(constrained, 40);
        check(size.height.value <= height,
              "tool output list fits a " + height_text + "-row terminal");
    }
#ifdef _WIN32
    _putenv_s("LINES", old_lines.c_str());
#else
    if (old_lines_raw) setenv("LINES", old_lines.c_str(), 1);
    else unsetenv("LINES");
#endif
}

// 稳定后的 edit 将 ```diff 围栏渲染为 GitDiff 审查卡片。两种
// 病理病例必须处理,所以卡片保持一个干净的审查表面:
//   1. 编辑工具缩短其 OUTPUT 给模型的热预算和
//      附录a "[... N chars elided — output exceeded tool's budget; refine your request ...]" 标记。
//      标记地址是 MODEL 永远不要 渲染
//      用户的diff卡内。
//   2. 巨大的deletion/rewrite 不得倾倒数百条无捆绑的红线
//      头部的窗户 maya 消化( 减除) show_all =虚伪).
TEST_CASE("edit diff card strips model budget marker + caps huge diffs") {
    // (1) 渗入围栏的预算标志必须剥离。
    {
        std::string out;
        out += "```diff\n";
        out += "@@ -1,3 +1,3 @@\n";
        out += "-old line\n";
        out += "+new line\n";
        out += " context\n";
        out += "\n[... 4096 chars elided — output exceeded tool's budget; "
               "refine your request to see more ...]\n";
        out += "```";
        auto tc = make_tool("edit", A::ToolUse::Done{{}, {}, out},
                            {{"path", "x.cpp"}});
        const auto body = U::tool_body_preview_config(tc);
        check(body.kind == Kind::GitDiff, "settled edit is a GitDiff card");
        check(body.text.find("chars elided") == std::string::npos
                  && body.text.find("refine your request") == std::string::npos,
              "the model-facing budget marker is stripped from the diff card");
        check(body.text.find("new line") != std::string::npos,
              "the actual diff content is preserved");
    }

    // (2) 普通小的 diff 渲染 整数(show_all).
    {
        std::string out = "```diff\n@@ -1 +1 @@\n-a\n+b\n```";
        auto tc = make_tool("edit", A::ToolUse::Done{{}, {}, out},
                            {{"path", "x.cpp"}});
        const auto body = U::tool_body_preview_config(tc);
        check(body.kind == Kind::GitDiff && body.show_all,
              "a small diff still renders in full");
    }

    // (3) 高个子的diff被遮住头部,而不是被整个抛出。
    {
        std::string out = "```diff\n@@ -1,400 +1,0 @@\n";
        for (int i = 0; i < 400; ++i)
            out += "-deleted line " + std::to_string(i) + "\n";
        out += "```";
        auto tc = make_tool("edit", A::ToolUse::Done{{}, {}, out},
                            {{"path", "big.cpp"}});
        const auto body = U::tool_body_preview_config(tc);
        check(body.kind == Kind::GitDiff, "huge edit is still a GitDiff card");
        check(!body.show_all,
              "a >200-line diff windows via elision instead of show_all");
        check(body.code_head > 0 && body.code_head <= 200,
              "the windowed diff keeps a bounded head");
    }
}

TEST_CASE("tool_display_name always capitalises") {
    using agentty::ui::tool_display_name;
    // Mapped build -ins 保留其配有的品牌标签.
    check(tool_display_name("shell") == "Shell", "shell -> Shell");
    check(tool_display_name("read")  == "Read",  "read -> Read");
    check(tool_display_name("git_diff") == "Git Diff", "git_diff -> Git Diff");

    // UNMAPPED 名称( 4) MCP 工具,原始模型名称)必须仍然标
    // 有标题 -Case - 从来没有卡片 渲染 小写符号。
    // 这是修复 `Bash` / 显示无大写的工具。
    check(tool_display_name("bash") == "Bash", "bash -> Bash (fallback)");
    check(tool_display_name("Bash") == "Bash", "Bash stays Bash");
    check(tool_display_name("github_create_issue") == "Github Create Issue",
          "snake_case MCP tool -> Title Case words");
    check(tool_display_name("do-thing") == "Do Thing", "dash-separated -> words");
    check(!tool_display_name("x").empty() && tool_display_name("x") == "X",
          "single char capitalises");
}

TEST_CASE("shell card shows the plain command") {
    // 没有 "Read ..." 标签: 外壳绕行必须像外壳 调用.
    auto d = [](const char* cmd) {
        return U::tool_timeline_detail(make_tool("shell", A::ToolUse::Running{},
                                                 {{"command", cmd}}));
    };
    check(d("sed -n '10,40p' src/a.cpp") == "sed -n '10,40p' src/a.cpp", "inspection unlabelled");
    check(d("cmake --build build -j12") == "cmake --build build -j12", "build unlabelled");
}

TEST_CASE("tool output viewer: what it shows and copies") {
    A::tool_output::Entry e;
    e.name = "shell";
    e.call = make_tool("shell", A::ToolUse::Done{{}, {}, ""},
                       {{"command", "make"}, {"display_description", "build it"}});
    e.output = "build it\n```\n\x1b[32mok\x1b[0m line\n\n\n\nnext\n```\n\n[output truncated: 400 more lines]";
    const auto t = A::tool_output::display_text(e);
    check(t.find("build it") == std::string::npos, "description echo dropped");
    check(t.find("```") == std::string::npos, "fence dropped");
    check(t.find('\x1b') == std::string::npos, "ANSI dropped");
    check(t.find("ok line\n\nnext") != std::string::npos, "blank runs collapse to one");
    check(t.find("\xe2\x8b\xaf output truncated: 400 more lines") != std::string::npos,
          "a cut is shown, not hidden");

    const auto spans = A::tool_output::hit_spans("Error: error ERROR", "error");
    check(spans.size() == 3, "find ignores case");
}

TEST_CASE("tool output viewer: find, Esc order, paging") {
    using namespace agentty::app;
    A::tool_output::Entry e;
    e.name = "shell";
    e.title = "Shell";
    for (int i = 0; i < 300; ++i)
        e.output += (i % 50 == 7 ? "needle here " : "row ") + std::to_string(i) + "\n";
    e.call = make_tool("shell", A::ToolUse::Done{{}, {}, e.output});
    A::Model m;
    m.ui.panel.descend(A::ui::panel::ToolOutput{{{e, e}, 0, true}});
    auto step = [&](A::Msg msg) { m = std::move(update(std::move(m), std::move(msg)).first); };
    auto o = [&] { return m.ui.panel.get<A::ui::panel::ToolOutput>(); };
    using Op = A::ToolOutputFind::Op;

    step(A::ToolOutputFind{Op::Open});
    check(o()->typing, "/ opens the prompt");
    for (char c : std::string{"NEEDLE"}) step(A::ToolOutputFind{Op::Type, char32_t(c)});
    check(o()->query == "NEEDLE", "typing builds the query");
    step(A::ToolOutputFind{Op::Erase});
    check(o()->query == "NEEDL", "backspace erases one");
    step(A::ToolOutputFind{Op::Commit});
    check(!o()->typing && o()->query == "NEEDL", "Enter keeps the query");

    // 景则解对比而卷之.
    setenv("LINES", "30", 1);
    setenv("COLUMNS", "100", 1);
    (void)U::tool_output_panel(m);
    const int y0 = m.ui.tool_viewer_scroll.y;
    step(A::ToolOutputFind{Op::Next});
    (void)U::tool_output_panel(m);
    const int y1 = m.ui.tool_viewer_scroll.y;
    check(y1 > y0, "n moves down to the next match");
    step(A::ToolOutputFind{Op::Prev});
    (void)U::tool_output_panel(m);
    check(m.ui.tool_viewer_scroll.y == y0, "N goes back");

    // 页面为可视区域,减去1行.
    m.ui.tool_viewer_scroll.y = 0;
    step(A::ToolOutputMove{0, +1});
    check(m.ui.tool_viewer_scroll.y == m.ui.tool_viewer_scroll.viewport_bounds.h - 1,
          "PgDn moves a page");

    // Esc 清除搜索,然后离开 正文 ,然后是 面板.
    step(A::CloseToolOutput{});
    check(o() && o()->viewing && o()->query.empty(), "Esc clears the search");
    step(A::CloseToolOutput{});
    check(o() && !o()->viewing, "then goes back to the list");
}

TEST_CASE("tool output viewer: never taller than the terminal") {
    A::tool_output::Entry e;
    e.name = "shell";
    e.title = "Shell";
    e.detail = "cmake --build build --target a-very-long-target-name -j12";
    e.trailing = "failed \xc2\xb7 41.2s \xc2\xb7 1 MB";
    for (int i = 0; i < 200; ++i) e.output += "output row " + std::to_string(i) + "\n";
    e.call = make_tool("shell", A::ToolUse::Done{{}, {}, e.output},
                       {{"command", "cmake --build build " + std::string(120, 'z')}});
    for (bool viewing : {false, true})
        for (bool searching : {false, true}) {
            A::Model m;
            m.ui.panel.descend(A::ui::panel::ToolOutput{{{e, e, e}, 0, viewing}});
            if (searching) m.ui.panel.get<A::ui::panel::ToolOutput>()->query = "row 1";
            for (int rows = 6; rows <= 40; ++rows)
                for (int cols : {30, 40, 60, 80, 120}) {
                    setenv("LINES", std::to_string(rows).c_str(), 1);
                    setenv("COLUMNS", std::to_string(cols).c_str(), 1);
                    auto el = U::tool_output_panel(m);
                    // 电话 覆盖面板 坐在底部2行,4列内。
                    const auto sz = maya::measure_element(el, cols - 4);
                    check(sz.height.value <= std::max(3, rows - 2),
                          std::string{viewing ? "body" : "list"}
                              + (searching ? " (find)" : "") + " fits "
                              + std::to_string(cols) + "x" + std::to_string(rows)
                              + ": " + std::to_string(sz.height.value) + " rows");
                }
        }
}

TEST_CASE("tool output viewer: list keeps the status whole") {
    A::tool_output::Entry e;
    e.name = "shell";
    e.title = "Shell";
    e.detail = "cmake --build build --target " + std::string(150, 'q');
    e.trailing = "failed \xc2\xb7 41.2s \xc2\xb7 128 KB";
    e.output = "x";
    e.call = make_tool("shell", A::ToolUse::Done{{}, {}, "x"});
    A::Model m;
    auto live = e;
    live.is_live = true;
    live.trailing = "running \xc2\xb7 3.0s \xc2\xb7 128 KB";
    for (bool is_live : {false, true}) {
    m.ui.panel.descend(A::ui::panel::ToolOutput{{{is_live ? live : e}, 0, false}});
    for (int cols : {50, 80, 120}) {
        setenv("COLUMNS", std::to_string(cols).c_str(), 1);
        setenv("LINES", "30", 1);
        auto el = U::tool_output_panel(m);
        maya::StylePool pool;
        maya::Canvas canvas(cols - 4, 30, &pool);
        canvas.clear();
        maya::render_tree(el, canvas, pool, maya::theme::live(), true);
        std::string all;
        for (int y = 0; y < 30; ++y) {
            for (int x = 0; x < cols - 4; ++x) {
                const char32_t ch = canvas.get(x, y).character;
                all.push_back(ch > 0 && ch < 0x80 ? static_cast<char>(ch) : ' ');
            }
            all += '\n';
        }
        check(all.find("128 KB") != std::string::npos,
              std::string{is_live ? "live " : ""} + "status whole at "
                  + std::to_string(cols) + " cols");
    }
    }
}

TEST_CASE("tool output viewer: long lines wrap inside the pane") {
    A::tool_output::Entry e;
    e.name = "shell";
    e.title = "Shell";
    e.output = std::string(500, 'x') + "\nshort\n";
    e.call = make_tool("shell", A::ToolUse::Done{{}, {}, e.output},
                       {{"command", "echo " + std::string(200, 'y') + " | tail -2"}});
    A::Model m;
    m.ui.panel.descend(A::ui::panel::ToolOutput{{{e}, 0, true}});
    for (int cols : {60, 80, 120}) {
        setenv("COLUMNS", std::to_string(cols).c_str(), 1);
        setenv("LINES", "14", 1);   // 4-row viewport
        auto el = U::tool_output_panel(m);
        auto boxed = maya::dsl::vstack().width(maya::Dimension::fixed(cols))(el);
        const auto size = maya::measure_element(boxed, cols);
        check(size.width.value <= cols, "pane fits " + std::to_string(cols) + " cols");
        // 500 x 加上一个 200-char 命令比 可视区域 更需要排行:
        // 被包裹成一排,而不是在右边缘被剪断。
        const int rows = m.ui.tool_viewer_scroll.max_y + 4;
        check(rows >= 500 / cols + 200 / cols + 2,
              "long lines wrap at " + std::to_string(cols) + " cols");
    }
}
