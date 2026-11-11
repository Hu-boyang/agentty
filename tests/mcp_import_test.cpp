// mcp_import_test 采用另一种工具 MCP 服务器 (# 60).
//
// 该 DESIGN UNDER TEST
// =====================
// #60问 agentty 改为 READ ~/.ai/mcp/mcp.json.
// 字义问是第六读位置;实际愿望(
// "all my custom MCP servers in one place" ——读它
// 就会把服务器留在 两个 具有优先规则的场所。
//
// 所以这是个导入:扫描一个外方配置,翻译,
// 写入~/.agentty/mcp.json,然后就有一个文件。
// 这些案例将属性标定在安全性上:
//
//   • 翻译忠于全程的Stdio AND远程的传输层
//   • 扫描NEVER写作(发现不是变异)
//   • 名称 agentty 除非 --force
//   • 外国配置不正确,拒绝进口其中一半
//   • Claude 单位 -project 巢积 此 仅为项目服务器

#include "agtest.hpp"

#include "agentty/mcp/import.hpp"
#include "agentty/tool/plugin.hpp"
#include "agentty/util/toml_lite.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
namespace imp = agentty::mcp::import_;
using json = nlohmann::json;

namespace {

void write_file(const fs::path& p, const std::string& body) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << body;
}

[[nodiscard]] const imp::Found* by_name(const imp::Scan& s, const char* n) {
    for (const auto& f : s.servers) if (f.name == n) return &f;
    return nullptr;
}

[[nodiscard]] fs::path sandbox(const char* leaf) {
    const auto dir = fs::temp_directory_path()
                   / ("agentty_import_" + std::string{leaf});
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

}  // 命名空间结束。

TEST_CASE("tool ids and their real locations") {
    // 问题标题中的路径不存在。 JetBrains 文档 Junie 的用户配置。
    // ~/.junie/mcp/mcp.json (项目:
    // .junie/mcp/mcp.json ); 记者记得形状,
    // 而不是目录。 将真实路径固定在这里, 就可以阻止猜测重新飘移。
    const fs::path home{"/h"}, proj{"/p"};

    const auto junie = imp::locations_of(imp::Tool::Junie, home, proj);
    CHECK(junie.user == fs::path{"/h/.junie/mcp/mcp.json"});
    CHECK(junie.project == fs::path{"/p/.junie/mcp/mcp.json"});

    // Claude 代码:每个项目有一个服务器嵌入的大用户文件,加上便携式文件。
    // mcp.json 以美元计 仓库 根节。
    const auto cc = imp::locations_of(imp::Tool::ClaudeCode, home, proj);
    CHECK(cc.user == fs::path{"/h/.claude.json"});
    CHECK(cc.project == fs::path{"/p/.mcp.json"});

    const auto cur = imp::locations_of(imp::Tool::Cursor, home, proj);
    CHECK(cur.user == fs::path{"/h/.cursor/mcp.json"});

    // VS 代码没有文档用户 -level 文件。
    // 空路径是诚实的答案;发明一个路径将使扫描报告成为永远不存在
    // 的位置。
    const auto vsc = imp::locations_of(imp::Tool::VsCode, home, proj);
    CHECK(vsc.user.empty());
    CHECK(vsc.project == fs::path{"/p/.vscode/mcp.json"});

    imp::Tool t{};
    CHECK(imp::parse_tool("junie", t) && t == imp::Tool::Junie);
    CHECK(imp::parse_tool("jetbrains", t) && t == imp::Tool::Junie);
    CHECK(imp::parse_tool("claude", t) && t == imp::Tool::ClaudeCode);
    CHECK(!imp::parse_tool("emacs", t));
}

TEST_CASE("translation keeps both transports, and skips what is neither") {
    const auto dir = sandbox("translate");
    write_file(dir / ".junie" / "mcp" / "mcp.json", R"({"mcpServers":{
        "stdio-one": {"command":"npx","args":["-y","@jetbrains/mcp-proxy"]},
        "remote-one": {"url":"https://api.example.com/mcp/","type":"http"},
        "neither":    {"description":"no command, no url"}
    }})");

    const imp::Scan s = imp::scan(imp::Tool::Junie, dir, dir / "nowhere");
    CHECK(s.error.empty(), "a well-formed config is not an error");

    const auto* stdio = by_name(s, "stdio-one");
    REQUIRE(stdio != nullptr, "stdio server found");
    CHECK(stdio->command == "npx");
    CHECK(stdio->entry["args"].size() == 2);
    CHECK(stdio->entry["args"][1] == "@jetbrains/mcp-proxy");

    const auto* remote = by_name(s, "remote-one");
    REQUIRE(remote != nullptr, "remote server found");
    CHECK(remote->url == "https://api.example.com/mcp/");
    CHECK(remote->entry["type"] == "http");

    // 既无命令又无url的服务器无法连接.
    // 输入它会生成一个在以后没有解释的情况下失败的条目;
    // 跳过它就是诚实的结果.
    CHECK(by_name(s, "neither") == nullptr,
          "an entry with no transport is not imported");
}

TEST_CASE("import is lossless — every key survives") {
    // 该 失败模式 此设计是为了避免的。 `mcp.json` 谓 一个
    // 模式定义 交流各种工具,以及 agentty 自己的连接路径已读
    // 取 `env`, `headers`, `timeoutMs`,
    // `connectTimeoutMs` 财务报告和已审计财务报表
    // `tools.exclude`。从它所担心的几块字段重新命名的输入会让
    // API 键使服务器工作——服务器将导入完整视图,然后在产卵时失败,
    // 并出现一个无名的认证错误。
    //
    // 所以逐字导入条目,这个是针头
    const auto dir = sandbox("lossless");
    write_file(dir / ".junie" / "mcp" / "mcp.json", R"({"mcpServers":{
        "rich": {
            "command": "/opt/server",
            "args": ["--port", "8080"],
            "env": {"API_KEY": "secret-value", "REGION": "eu"},
            "timeoutMs": 45000,
            "connectTimeoutMs": 9000,
            "tools": {"exclude": ["dangerous_tool"]},
            "someFutureKey": {"nested": true}
        }
    }})");

    const imp::Scan s = imp::scan(imp::Tool::Junie, dir, dir / "nowhere");
    const auto* rich = by_name(s, "rich");
    REQUIRE(rich != nullptr, "found it");

    // 一切,我都会说 agentty 本身在连接时间读取。
    CHECK(rich->entry["env"]["API_KEY"] == "secret-value",
          "the env block survives — this is the one that silently breaks");
    CHECK(rich->entry["env"]["REGION"] == "eu");
    CHECK(rich->entry["timeoutMs"] == 45000);
    CHECK(rich->entry["connectTimeoutMs"] == 9000);
    CHECK(rich->entry["tools"]["exclude"][0] == "dangerous_tool");

    // 还有一把钥匙 NEITHER 今天的工具模型。
    // 复制逐字记录意味着在明年格式中添加的字段完整地到达,没有更改代码,
    // 允许 -list 永远无法完成已知的田园。
    CHECK(rich->entry["someFutureKey"]["nested"] == true,
          "an unrecognised key is carried, not dropped");
}

TEST_CASE("the ADOPTED file keeps every key, and never clobbers") {
    // 环球旅行,不只是扫描: 实际降落在 ~/.agentty/mcp.json.
    // adopt() 写入 user_root (),因此,
    // 指出 沙箱.
    const auto dir = sandbox("adopt");
    const fs::path home = dir / "home";
    fs::create_directories(home);

    const std::string prev = std::getenv("AGENTTY_HOME")
                                 ? std::getenv("AGENTTY_HOME") : "";
    const bool had = std::getenv("AGENTTY_HOME") != nullptr;
    ::setenv("AGENTTY_HOME", home.string().c_str(), 1);

    imp::Found f;
    f.name    = "rich";
    f.command = "/opt/server";
    f.entry   = json::parse(R"({
        "command": "/opt/server",
        "env": {"API_KEY": "secret-value"},
        "timeoutMs": 45000
    })");

    const imp::Outcome o = imp::adopt(::agentty::IoAccess::grant(), agtest::cap(), {f}, {});
    CHECK(o.imported == 1, "one server adopted");

    json doc;
    {
        std::ifstream in(o.into);
        REQUIRE(in.good(), "the file was written");
        in >> doc;
    }
    const auto& got = doc["mcpServers"]["rich"];
    CHECK(got["command"] == "/opt/server");
    CHECK(got["env"]["API_KEY"] == "secret-value",
          "the env block is ON DISK, not just in the scan");
    CHECK(got["timeoutMs"] == 45000);

    // Re-adopting 的同名, 但不包含 --force
    // 的不可覆盖。 以 MCPoison 形状来搜索一个命令,
    // 无法使其安全。
    imp::Found evil = f;
    evil.entry["command"] = "/evil";
    evil.conflicts = true;
    const imp::Outcome o2 = imp::adopt(::agentty::IoAccess::grant(), agtest::cap(), {evil}, {});
    CHECK(o2.imported == 0 && o2.skipped == 1, "conflict skipped");
    {
        std::ifstream in(o.into);
        json after;
        in >> after;
        CHECK(after["mcpServers"]["rich"]["command"] == "/opt/server",
              "the original entry is untouched");
    }

    // --force取代了批发式,即有记载的逃生舱门.
    imp::Options force;
    force.force = true;
    const imp::Outcome o3 = imp::adopt(::agentty::IoAccess::grant(), agtest::cap(), {evil}, force);
    CHECK(o3.imported == 1, "--force replaces");

    if (had) ::setenv("AGENTTY_HOME", prev.c_str(), 1);
    else     ::unsetenv("AGENTTY_HOME");
}

TEST_CASE("names agentty cannot manage are refused, and counted") {
    // 导入是一个信任边界: 字节 来源于文件, 其它工具,
    // 所以名字可以是任何东西。 它不仅仅是一个标签, 它是您输入的
    // 禁用服务器的密钥, 以及它所曝光的每个工具的前缀,
    // 以及一个 存储单元 三个表格。
    //
    // 两种形状打破了它,通过喂食敌对的固定器 在一个: 空 名称
    // 渲染 和一个名字,里面有 NEWLINE 拆分( E)
    // `plugin list` 横跨两条线,所以桌子就停了
    // 解析 也不存在安全漏洞, canonical_mcp_name
    // () 疗养工具名称 下游 ),但两者都生成用户无法
    // 管理的条目.
    const auto dir = sandbox("names");
    // 注意: 已对《公约》第2条作出修正, ESCAPED \n:
    // 字面新行无效 JSON ,而关键是名称 解析 之后就把一切都弄断了
    write_file(dir / ".junie" / "mcp" / "mcp.json",
               "{\"mcpServers\":{"
               "  \"good\":      {\"command\":\"/bin/true\"},"
               "  \"\":          {\"command\":\"/bin/x\"},"
               "  \"two\\nlines\": {\"command\":\"/bin/y\"},"
               "  \"../../odd\": {\"command\":\"/bin/z\"}"
               "}}");

    const imp::Scan s = imp::scan(imp::Tool::Junie, dir, dir / "nowhere");
    CHECK(by_name(s, "good") != nullptr, "an ordinary name is kept");
    CHECK(by_name(s, "") == nullptr, "an empty name is refused");
    for (const auto& f : s.servers)
        CHECK(f.name.find('\n') == std::string::npos,
              "no control bytes survive into a name");

    // `../../odd` 只是个奇怪的事情 STRING - 这是...
    // JSON 键, 绝不加入路径。 拒绝它会把化妆品的担忧变成拒绝
    // 真正的服务器, 所以它是被刻意保存的。
    CHECK(by_name(s, "../../odd") != nullptr,
          "a path-looking name is still just a name");

    // 拒绝的就是 REPORTED. "2 of 4 imported"
    // 无法解释的是, 如何有人得出 进口被打破。
    CHECK(s.skipped == 2, "both unusable entries are counted");
}

TEST_CASE("scanning writes nothing") {
    // discovery)为禾本科 ⁇ 属下的一个变种. `agentty mcp import`
    // 没有 --from 是一个报表,而创建文件的报告是一个陷阱。
    const auto dir = sandbox("readonly");
    write_file(dir / ".cursor" / "mcp.json",
               R"({"mcpServers":{"x":{"command":"/bin/true"}}})");
    const auto before = fs::last_write_time(dir / ".cursor" / "mcp.json");

    // 区分的家和项目 dirs:为两者通过相同的路径,
    // 将使用户和项目-scope探测器降落在一个文件中,
    // 并两次找到服务器,服务器用来测量固定而不是代码.
    const imp::Scan s = imp::scan(imp::Tool::Cursor, dir, dir / "proj");
    CHECK(s.servers.size() == 1, "found it");
    CHECK(fs::last_write_time(dir / ".cursor" / "mcp.json") == before,
          "the foreign config is not touched");
    CHECK(!fs::exists(dir / ".agentty"), "nothing of ours is created");
}

TEST_CASE("a malformed foreign config refuses rather than guessing") {
    // 导入半个配置比导入无更糟糕: 用户会有一个
    // 部分服务器列表, 没有理由怀疑它。
    const auto dir = sandbox("malformed");
    write_file(dir / ".cursor" / "mcp.json", R"({"mcpServers":{broken)");
    const imp::Scan s = imp::scan(imp::Tool::Cursor, dir, dir / "proj");
    CHECK(!s.error.empty(), "the parse failure is reported");
    CHECK(s.servers.empty(), "and nothing is salvaged from it");
}

TEST_CASE("claude nesting yields THIS project only") {
    // ~/.claude.json 将每个工程的服务器保存在一个文件中。
    // 采用另一个检查退出将是一个真正的惊喜, 因此, 访问范围 -in ,
    // 在工程路径上键入。
    const auto dir = sandbox("claude");
    const fs::path mine = dir / "mine";
    write_file(dir / ".claude.json",
               json{{"projects",
                     {{mine.string(),
                       {{"mcpServers", {{"ours", {{"command", "/opt/ours"}}}}}}},
                      {"/somewhere/else",
                       {{"mcpServers", {{"theirs", {{"command", "/opt/theirs"}}}}}}}}}}
                   .dump());

    const imp::Scan s = imp::scan(imp::Tool::ClaudeCode, dir, mine);
    CHECK(by_name(s, "ours") != nullptr, "this project's server is found");
    CHECK(by_name(s, "theirs") == nullptr,
          "another project's server is NOT adopted");
}

TEST_CASE("every probed path is reported, present or not") {
    // 扫描打印它所看到的。一个用户的配置出乎意料的,
    // 需要查看被检查的路径,而不仅仅是"nothing found"
    // ——这就是死路与下一步的区别。
    const auto dir = sandbox("probed");
    const imp::Scan s = imp::scan(imp::Tool::Junie, dir, dir / "proj");
    CHECK(s.servers.empty(), "nothing is there");
    CHECK(s.looked_in.size() == 2, "but both scopes were probed");
    CHECK(s.error.empty(), "a missing file is not an error");
}

TEST_CASE("scan_all skips tools with nothing to offer") {
    const auto dir = sandbox("scanall");
    CHECK(imp::scan_all(dir, dir / "proj").empty(),
          "an empty machine yields no noise");
    write_file(dir / ".junie" / "mcp" / "mcp.json",
               R"({"mcpServers":{"only":{"command":"/bin/true"}}})");
    const auto found = imp::scan_all(dir, dir / "nowhere");
    CHECK(found.size() == 1, "exactly the tool that has something");
    CHECK(found.front().tool == imp::Tool::Junie);
}

TEST_CASE("toml_lite reads the subset Codex config uses") {
    namespace tl = agentty::util::toml_lite;
    json doc;
    std::string err;
    const bool ok = tl::parse(R"(# top comment
model = "gpt-5"   # trailing
effort = 'medium'
n = 1_000
hex = 0x1F
f = 2.5
on = true
when = 1979-05-27T07:32:00Z

[projects."/home/me/x"]
trust_level = "trusted"

[a.b]
c.d = 1
inline = { x = 1, y = [1, 2,
  3,] }
ml = """
line one
line two\
   joined"""
lit = '''C:\no\escape'''
esc = "tab\there \u00e9"

[[items]]
name = "one"
[[items]]
name = "two"
[items.sub]
k = "v"
)", doc, err);
    REQUIRE(ok, err.c_str());
    CHECK(doc["model"] == "gpt-5");
    CHECK(doc["effort"] == "medium");
    CHECK(doc["n"] == 1000);
    CHECK(doc["hex"] == 31);
    CHECK(doc["f"] == 2.5);
    CHECK(doc["on"] == true);
    CHECK(doc["when"] == "1979-05-27T07:32:00Z");
    CHECK(doc["projects"]["/home/me/x"]["trust_level"] == "trusted");
    CHECK(doc["a"]["b"]["c"]["d"] == 1);
    CHECK(doc["a"]["b"]["inline"]["y"].size() == 3);
    CHECK(doc["a"]["b"]["ml"] == "line one\nline twojoined");
    CHECK(doc["a"]["b"]["lit"] == "C:\\no\\escape");
    CHECK(doc["a"]["b"]["esc"] == "tab\there \xc3\xa9");
    CHECK(doc["items"].size() == 2);
    CHECK(doc["items"][1]["sub"]["k"] == "v");

    CHECK(!tl::parse("a = 1\na = 2\n", doc, err), "duplicate keys are an error");
    CHECK(err.find("line 2") != std::string::npos, "and the error names the line");
    CHECK(!tl::parse("a = \"open\n", doc, err), "unterminated string");
    CHECK(!tl::parse("a = 1 b = 2\n", doc, err), "junk after a value");
}

TEST_CASE("codex: config.toml servers map onto agentty's schema") {
    const auto dir = sandbox("codex");
    ::unsetenv("CODEX_HOME");
    write_file(dir / ".codex" / "config.toml", R"(model = "gpt-5"

[mcp_servers.docs]
command = "npx"
args = ["-y", "@acme/docs-mcp"]
cwd = "/srv"
env = { API_KEY = "k" }
tool_timeout_sec = 45
startup_timeout_sec = 20
disabled_tools = ["delete_all"]
enabled_tools = ["search"]

[mcp_servers.remote]
url = "https://mcp.example.com/mcp"
http_headers = { "X-Team" = "core" }
bearer_token_env_var = "REMOTE_TOKEN"
enabled = false

[mcp_servers.broken]
note = "no transport"
)");

    const imp::Scan s = imp::scan(imp::Tool::Codex, dir, dir / "proj");
    CHECK(s.error.empty(), s.error.c_str());
    CHECK(s.skipped == 1, "an entry with no command or url is skipped and counted");

    const auto* docs = by_name(s, "docs");
    REQUIRE(docs != nullptr);
    CHECK(docs->entry["command"] == "npx");
    CHECK(docs->entry["args"].size() == 2);
    CHECK(docs->entry["cwd"] == "/srv");
    CHECK(docs->entry["env"]["API_KEY"] == "k");
    CHECK(docs->entry["timeoutMs"] == 45000);
    CHECK(docs->entry["connectTimeoutMs"] == 20000);
    CHECK(docs->entry["tools"]["exclude"][0] == "delete_all");
    CHECK(docs->notes.size() == 1, "enabled_tools has no equivalent, so it is noted");

    const auto* remote = by_name(s, "remote");
    REQUIRE(remote != nullptr);
    CHECK(remote->entry["url"] == "https://mcp.example.com/mcp");
    CHECK(remote->entry["type"] == "http");
    CHECK(remote->entry["headers"]["X-Team"] == "core");
    CHECK(!remote->entry["headers"].contains("Authorization"),
          "an env-held token is never written to disk");
    CHECK(remote->entry["disabled"] == true);
    CHECK(remote->notes.size() == 1, "the bearer token is called out instead");
}

TEST_CASE("codex: a malformed config.toml refuses instead of importing part of it") {
    const auto dir = sandbox("codex_bad");
    ::unsetenv("CODEX_HOME");
    write_file(dir / ".codex" / "config.toml",
               "[mcp_servers.a]\ncommand = \"x\"\n[mcp_servers.b\n");
    const imp::Scan s = imp::scan(imp::Tool::Codex, dir, dir / "proj");
    CHECK(!s.error.empty());
    CHECK(s.servers.empty());
}
