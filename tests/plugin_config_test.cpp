// plugin_config_test.cpp - 电话 `agentty plugin`
// mcp.json 编辑器( 4) tools::plugin : 编辑合同来自 plugin.hpp。
// 最重要的财产是: ROUND-TRIP SAFETY — `plugin add`
// 绝对不能破坏用户手写配置(其他服务器的封装块, "servers" 拼写,
// 未知顶端 -level ))," 键".

#include "agtest_cap.hpp"
#include "agtest.hpp"

#include "agentty/tool/plugin.hpp"
#include "agentty/scope/scope.hpp"   // 核准——跨-process合并案
#include "agentty/mcp/client.hpp"   // plugin_model ()——合并. -config 探测器
#include "agentty/tool/util/fs_helpers.hpp"   // workspace_root 夹

#include <nlohmann/json.hpp>

#ifndef _WIN32
#include <sys/wait.h>   // 等待时间——交叉 -process 竞态 案件数量
#include <unistd.h>     // 叉, 退出(_Exit)
#endif

#include <filesystem>
#include <fstream>
#include <print>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;
namespace plug = agentty::tools::plugin;
using nlohmann::json;

namespace {


void write_file(const fs::path& p, const std::string& body) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << body;
}

[[nodiscard]] json read_json(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return json::parse(f, nullptr, /*投掷=*/false);
}

void creates_file_on_first_add(const fs::path& dir) {
    std::println("--- creates_file_on_first_add ---");
    const fs::path cfg = dir / "fresh" / "mcp.json";
    auto r = plug::add_server(::agentty::IoAccess::grant(), agtest::cap(), cfg, {"weather", "uvx", {"mcp-weather"}},
                              /*力=*/false);
    check(r == plug::EditResult::Ok, "add on a missing file succeeds");
    json j = read_json(cfg);
    check(j.is_object() && j.contains("mcpServers"),
          "new file uses the mcpServers spelling");
    check(j["mcpServers"]["weather"]["command"] == "uvx", "command stored");
    check(j["mcpServers"]["weather"]["args"] == json::array({"mcp-weather"}),
          "args stored");
    std::println("PASS\n");
}

void round_trip_preserves_foreign_keys(const fs::path& dir) {
    std::println("--- round_trip_preserves_foreign_keys ---");
    const fs::path cfg = dir / "rt" / "mcp.json";
    // 现实的手 -written 配置: 另一服务器, 带有 env +
    // 未知顶端 -level 键。
    write_file(cfg, R"({
  "mcpServers": {
    "github": {
      "command": "mcp-server-github",
      "args": ["--repo", "x"],
      "env": { "GITHUB_TOKEN": "secret" }
    }
  },
  "my_custom_toplevel": { "keep": true }
})");
    auto r = plug::add_server(::agentty::IoAccess::grant(), agtest::cap(), cfg, {"weather", "uvx", {"mcp-weather"}},
                              false);
    check(r == plug::EditResult::Ok, "add succeeds");
    json j = read_json(cfg);
    check(j["mcpServers"]["github"]["env"]["GITHUB_TOKEN"] == "secret",
          "another server's env block survives");
    check(j["mcpServers"]["github"]["args"] == json::array({"--repo", "x"}),
          "another server's args survive");
    check(j["my_custom_toplevel"]["keep"] == true,
          "unknown top-level key survives");
    check(j["mcpServers"].contains("weather"), "new entry landed");

    // 删除新条目: github + 自定义密钥必须仍然完整。
    r = plug::remove_server(::agentty::IoAccess::grant(), agtest::cap(), cfg, "weather");
    check(r == plug::EditResult::Ok, "remove succeeds");
    j = read_json(cfg);
    check(!j["mcpServers"].contains("weather"), "entry removed");
    check(j["mcpServers"]["github"]["env"]["GITHUB_TOKEN"] == "secret",
          "remove also preserves foreign keys");
    std::println("PASS\n");
}

void honours_servers_spelling(const fs::path& dir) {
    std::println("--- honours_servers_spelling ---");
    const fs::path cfg = dir / "sp" / "mcp.json";
    write_file(cfg, R"({"servers": {"a": {"command": "x"}}})");
    auto r = plug::add_server(::agentty::IoAccess::grant(), agtest::cap(), cfg, {"b", "y", {}}, false);
    check(r == plug::EditResult::Ok, "add succeeds");
    json j = read_json(cfg);
    check(j.contains("servers") && !j.contains("mcpServers"),
          "existing 'servers' spelling is kept (no split-brain)");
    check(j["servers"].contains("a") && j["servers"].contains("b"),
          "both entries under the one key");
    std::println("PASS\n");
}

void no_clobber_without_force(const fs::path& dir) {
    std::println("--- no_clobber_without_force ---");
    const fs::path cfg = dir / "cl" / "mcp.json";
    (void)plug::add_server(::agentty::IoAccess::grant(), agtest::cap(), cfg, {"t", "cmd1", {}}, false);
    auto r = plug::add_server(::agentty::IoAccess::grant(), agtest::cap(), cfg, {"t", "cmd2", {}}, false);
    check(r == plug::EditResult::AlreadyExists, "duplicate add refused");
    check(read_json(cfg)["mcpServers"]["t"]["command"] == "cmd1",
          "original untouched after refusal");
    r = plug::add_server(::agentty::IoAccess::grant(), agtest::cap(), cfg, {"t", "cmd2", {}}, /*力=*/true);
    check(r == plug::EditResult::Ok, "--force overwrites");
    check(read_json(cfg)["mcpServers"]["t"]["command"] == "cmd2",
          "forced entry stored");
    std::println("PASS\n");
}

void distinct_not_found(const fs::path& dir) {
    std::println("--- distinct_not_found ---");
    const fs::path cfg = dir / "nf" / "mcp.json";
    check(plug::remove_server(::agentty::IoAccess::grant(), agtest::cap(), cfg, "ghost") == plug::EditResult::NotFound,
          "remove on a missing FILE is NotFound");
    (void)plug::add_server(::agentty::IoAccess::grant(), agtest::cap(), cfg, {"real", "x", {}}, false);
    check(plug::remove_server(::agentty::IoAccess::grant(), agtest::cap(), cfg, "ghost") == plug::EditResult::NotFound,
          "remove of an absent NAME is NotFound");
    check(read_json(cfg)["mcpServers"].contains("real"),
          "failed remove left the file alone");
    std::println("PASS\n");
}

void refuses_broken_json(const fs::path& dir) {
    std::println("--- refuses_broken_json ---");
    const fs::path cfg = dir / "br" / "mcp.json";
    write_file(cfg, "{ this is not json");
    check(plug::add_server(::agentty::IoAccess::grant(), agtest::cap(), cfg, {"x", "y", {}}, false)
              == plug::EditResult::ParseError,
          "add refuses to rewrite an unparseable file");
    check(plug::remove_server(::agentty::IoAccess::grant(), agtest::cap(), cfg, "x") == plug::EditResult::ParseError,
          "remove refuses too");
    std::ifstream f(cfg);
    std::string body((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
    check(body == "{ this is not json",
          "broken file byte-identical after refusals");
    std::println("PASS\n");
}

void list_reads_back(const fs::path& dir) {
    std::println("--- list_reads_back ---");
    const fs::path cfg = dir / "ls" / "mcp.json";
    (void)plug::add_server(::agentty::IoAccess::grant(), agtest::cap(), cfg, {"a", "uvx", {"pkg-a"}}, false);
    (void)plug::add_server(::agentty::IoAccess::grant(), agtest::cap(), cfg, {"b", "python3", {"/x/s.py", "--flag"}}, false);
    auto servers = plug::list_servers(cfg);
    check(servers.size() == 2, "two entries listed");
    bool found_b = false;
    for (const auto& s : servers)
        if (s.name == "b" && s.command == "python3" && s.args.size() == 2
            && s.args[1] == "--flag")
            found_b = true;
    check(found_b, "listed entry round-trips command+args");
    check(plug::list_servers(dir / "absent.json").empty(),
          "missing file lists empty");
    std::println("PASS\n");
}

} // 命名空间结束。

void tool_enable_disable(const fs::path& dir) {
    std::println("--- tool_enable_disable ---");
    const fs::path cfg = dir / "tgl" / "mcp.json";
    (void)plug::add_server(::agentty::IoAccess::grant(), agtest::cap(), cfg, {"date", "/x/date", {}}, false);
    // 禁用一个工具
    check(plug::set_tool_enabled(::agentty::IoAccess::grant(), agtest::cap(), cfg, "date", "current_date", false)
              == plug::EditResult::Ok, "disable succeeds");
    check(plug::is_tool_disabled(cfg, "date", "current_date"),
          "tool recorded as disabled");
    check(read_json(cfg)["mcpServers"]["date"]["tools"]["exclude"]
              == json::array({"current_date"}),
          "tools.exclude holds the bare name");
    // 幂等 禁用
    check(plug::set_tool_enabled(::agentty::IoAccess::grant(), agtest::cap(), cfg, "date", "current_date", false)
              == plug::EditResult::Ok, "double-disable is Ok no-op");
    // re-enable 清除它( 和 prunes 空工具对象)
    check(plug::set_tool_enabled(::agentty::IoAccess::grant(), agtest::cap(), cfg, "date", "current_date", true)
              == plug::EditResult::Ok, "re-enable succeeds");
    check(!plug::is_tool_disabled(cfg, "date", "current_date"),
          "tool no longer disabled");
    check(!read_json(cfg)["mcpServers"]["date"].contains("tools"),
          "empty tools object pruned on re-enable");
    // 命令能生存下来
    check(read_json(cfg)["mcpServers"]["date"]["command"] == "/x/date",
          "server command preserved across toggles");
    // 正在丢失服务器时的正转位是 NotFound
    check(plug::set_tool_enabled(::agentty::IoAccess::grant(), agtest::cap(), cfg, "ghost", "x", false)
              == plug::EditResult::NotFound, "toggle on absent server NotFound");
    std::println("PASS\n");
}

// 来,来,来 HTTP/SSE 服务器被写为 {type, url} —— 没有命令 —— 并
// 用它的 URl 读回。 这是 传输层 内置线性 `--http`/`--sse` 添加用途。
void http_server_add(const fs::path& dir) {
    std::println("--- http_server_add ---");
    const fs::path cfg = dir / "http" / "mcp.json";
    plug::ServerSpec http;
    http.name = "remote";
    http.url  = "https://mcp.example.com/api";
    http.type = "http";
    check(plug::add_server(::agentty::IoAccess::grant(), agtest::cap(), cfg, http, false) == plug::EditResult::Ok,
          "http server add succeeds");
    json e = read_json(cfg)["mcpServers"]["remote"];
    check(e["type"] == "http", "type is http");
    check(e["url"] == "https://mcp.example.com/api", "url stored");
    check(!e.contains("command"), "no command key on a url server");

    plug::ServerSpec sse;
    sse.name = "events";
    sse.url  = "https://x.example.com/sse";
    sse.type = "sse";
    check(plug::add_server(::agentty::IoAccess::grant(), agtest::cap(), cfg, sse, false) == plug::EditResult::Ok, "sse add succeeds");
    check(read_json(cfg)["mcpServers"]["events"]["type"] == "sse", "type is sse");

    // list_servers读回了url.
    bool found = false;
    for (const auto& s : plug::list_servers(cfg))
        if (s.name == "remote") { found = true;
            check(s.url == "https://mcp.example.com/api", "list_servers reads url");
            check(s.command.empty(), "url server has no command"); }
    check(found, "http server is listed");
    std::println("PASS\n");
}

void server_enable_disable(const fs::path& dir) {
    std::println("--- server_enable_disable ---");
    const fs::path cfg = dir / "srv" / "mcp.json";
    (void)plug::add_server(::agentty::IoAccess::grant(), agtest::cap(), cfg, {"date", "/x/date", {}}, false);
    check(!plug::is_server_disabled(cfg, "date"), "new server starts enabled");
    // 生态系统可移植性:新增的服务器被标记为:stdio(新
    // 出现的跨-client公约)。
    check(read_json(cfg)["mcpServers"]["date"]["type"] == "stdio",
          "added server carries type:stdio");
    // 禁用整个服务器
    check(plug::set_server_disabled(::agentty::IoAccess::grant(), agtest::cap(), cfg, "date", true) == plug::EditResult::Ok,
          "disable server succeeds");
    check(plug::is_server_disabled(cfg, "date"), "server recorded disabled");
    check(read_json(cfg)["mcpServers"]["date"]["disabled"] == true,
          "disabled:true persisted");
    // idempotent
    check(plug::set_server_disabled(::agentty::IoAccess::grant(), agtest::cap(), cfg, "date", true) == plug::EditResult::Ok,
          "double-disable is Ok no-op");
    // re-enable 删除 标志( 启用 \\\\ n没有文件)
    check(plug::set_server_disabled(::agentty::IoAccess::grant(), agtest::cap(), cfg, "date", false) == plug::EditResult::Ok,
          "re-enable succeeds");
    check(!plug::is_server_disabled(cfg, "date"), "server enabled again");
    check(!read_json(cfg)["mcpServers"]["date"].contains("disabled"),
          "disabled key removed on re-enable (not left as false)");
    check(read_json(cfg)["mcpServers"]["date"]["command"] == "/x/date",
          "command preserved across server toggles");
    check(plug::set_server_disabled(::agentty::IoAccess::grant(), agtest::cap(), cfg, "ghost", true)
              == plug::EditResult::NotFound, "disable absent server NotFound");
    std::println("PASS\n");
}

// 一只手,你来帮忙 -edited 结构不正确 `disabled` (弦,不是嘘声) 不 抛出( E)...
// is_server_disabled 财务报告和已审计财务报表 set_server_disabled 必须失败 -open (当作假.
void malformed_disabled_no_throw(const fs::path& dir) {
    std::println("--- malformed_disabled_no_throw ---");
    const fs::path cfg = dir / "bad" / "mcp.json";
    write_file(cfg,
        R"({"mcpServers":{"date":{"command":"/x","disabled":"true"}}})");
    // 正在读取字符串 `disabled` 不得抛出和读作 "not disabled".
    bool threw = false;
    try {
        check(!plug::is_server_disabled(cfg, "date"),
              "string disabled reads as not-disabled (fail-open)");
    } catch (...) { threw = true; }
    check(!threw, "is_server_disabled does not throw on a string flag");
    // 把它弄成这样,一定要成功,而且 REPLACE 价值差的 与真正的bool。
    threw = false;
    try {
        check(plug::set_server_disabled(::agentty::IoAccess::grant(), agtest::cap(), cfg, "date", true) == plug::EditResult::Ok,
              "toggle over a malformed flag succeeds");
    } catch (...) { threw = true; }
    check(!threw, "set_server_disabled does not throw on a string flag");
    check(read_json(cfg)["mcpServers"]["date"]["disabled"] == true,
          "malformed flag replaced by a real bool");
    std::println("PASS\n");
}

// 非 国 家 -object 服务器条目( E) e.g. `"date": "just a string"` ) 不得投掷.
void non_object_entry_no_throw(const fs::path& dir) {
    std::println("--- non_object_entry_no_throw ---");
    const fs::path cfg = dir / "nonobj" / "mcp.json";
    write_file(cfg, R"({"mcpServers":{"date":"just a string"}})");
    bool threw = false;
    try {
        check(plug::set_server_disabled(::agentty::IoAccess::grant(), agtest::cap(), cfg, "date", true)
                  == plug::EditResult::NotFound,
              "non-object entry → NotFound, no throw");
        check(!plug::is_server_disabled(cfg, "date"),
              "non-object entry is_server_disabled false");
    } catch (...) { threw = true; }
    check(!threw, "non-object entry never throws");
    std::println("PASS\n");
}

// 脱落时同时发生的突变 线程 不得丢失更新或损坏文件(该
// mutation_mutex + 独特的临时担保)。
void concurrent_mutations_safe(const fs::path& dir) {
    std::println("--- concurrent_mutations_safe ---");
    const fs::path cfg = dir / "conc" / "mcp.json";
    // 种子 N 服务器.
    constexpr int N = 24;
    for (int i = 0; i < N; ++i)
        (void)plug::add_server(::agentty::IoAccess::grant(), agtest::cap(), cfg, {"s" + std::to_string(i), "/x", {}}, false);

    // 锤子:每线程同时禁用一个不同的服务器.
    std::vector<std::thread> ts;
    for (int i = 0; i < N; ++i)
        ts.emplace_back([&, i]{
            (void)plug::set_server_disabled(::agentty::IoAccess::grant(), agtest::cap(), cfg, "s" + std::to_string(i), true);
        });
    for (auto& t : ts) t.join();

    // 文件必须仍然有效 JSON -没有 torn/interleaved 写作。
    json j = read_json(cfg);
    check(!j.is_discarded(), "file is still valid JSON after concurrent writes");
    // 每个 禁用必须已经着陆( 没有丢失更新)。
    int disabled = 0;
    for (int i = 0; i < N; ++i)
        if (plug::is_server_disabled(cfg, "s" + std::to_string(i))) ++disabled;
    check(disabled == N,
          "all " + std::to_string(N) + " concurrent disables landed (got "
          + std::to_string(disabled) + ")");
    std::println("PASS\n");
}

// 两人核准 PROCESSES 必须合并,而不是覆盖。
//
// 商店由三个子系统(技能,钩子, MCP 插件)和每个
// 呼叫者都这样做 加载 - > 核准(x) - >
// 保存 二 : 将两个 agentty 批准不同事情的
// 事例 每一个都写了一张从未见过对方进入的列表 所以一个
// 批准消失了 不管它担保什么 待处理 固定前测量:4项同时批准,
// 每轮仅1项幸存。
//
// 合并是正确的对账而非最后 -writer-wins 因为批准是增长 -only 事实
// 有人对这些确切字节作出了担保。
// - 和什么都没有在 源码树 撤销一个。
void approvals_merge_across_processes(const fs::path& dir) {
    std::println("--- approvals_merge_across_processes ---");
#ifdef _WIN32
    std::println("SKIP (no fork)\n");
    (void)dir;
#else
    const fs::path home = dir / "aprhome";
    fs::create_directories(home);
    const std::string prev = std::getenv("AGENTTY_HOME")
                                 ? std::getenv("AGENTTY_HOME") : "";
    const bool had = std::getenv("AGENTTY_HOME") != nullptr;
    ::setenv("AGENTTY_HOME", home.string().c_str(), 1);

    constexpr char kLeaf[] = "race_approved.json";
    constexpr int  kWriters = 4;
    int lost = 0;

    for (int round = 0; round < 6; ++round) {
        // 从空开始每回合。 Write 结束 save_approvals
        // 而不是手摸路 -- -- 商店住下 state/
        // 并且为旧的平地播种 将会让遗留的迁徙 中途
        // -race 而不是测试 竞态.
        (void)agentty::scope::save_approvals(::agentty::IoAccess::grant(), agtest::cap(), kLeaf, agentty::scope::Approvals{});
        { std::ofstream f(home / "state" / kLeaf, std::ios::trunc); f << "[]"; }

        std::vector<::pid_t> kids;
        for (int w = 0; w < kWriters; ++w) {
            const ::pid_t pid = ::fork();
            if (pid == 0) {
                // 儿童:真正的周期 -- 加载 ,添加一个, 保存.
                //
                // 睡眠是它使这个 RACE
                // 而不是四个幸运的顺序运行。
                // 加载 等待,所以他们每个人都
                // 持有 快照 叉子没有它,
                // 它们就很少重叠,案件甚至没有
                // 固定下来。这是第一次写。
                auto store = agentty::scope::load_approvals(kLeaf);
                ::usleep(40000);   // 40毫秒:所有四个现在持有过期副本
                store.approve("hash-r" + std::to_string(round)
                              + "-w" + std::to_string(w));
                const bool ok = agentty::scope::save_approvals(::agentty::IoAccess::grant(), agtest::cap(), kLeaf, store);
                ::_exit(ok ? 0 : 1);
            }
            if (pid > 0) kids.push_back(pid);
        }
        for (::pid_t p : kids) { int st = 0; ::waitpid(p, &st, 0); }

        const auto final_store = agentty::scope::load_approvals(kLeaf);
        if (static_cast<int>(final_store.shas.size()) != kWriters) ++lost;
    }

    check(lost == 0,
          "concurrent approvals all survive (lost rounds: "
              + std::to_string(lost) + "/6)");

    // 还有 保存 千万不要马上离开店里 空。旧的表格打开
    // 了真正的路径, 并列阅读器可以把它抓在中间 -write
    // -- 且空批准文件失败 CLOSED ,
    // 意味着每个项目配置都读作对那一刻的不信任.
    check(!fs::exists(home / "state" / (std::string{kLeaf} + ".tmp")),
          "no temp file is left behind");

    if (had) ::setenv("AGENTTY_HOME", prev.c_str(), 1);
    else     ::unsetenv("AGENTTY_HOME");
    std::println("PASS\n");
#endif
}

// 两位 agentty PROCESSES 变异一个 mcp.json 不得丢失更新。
//
// concurrent_mutations_safe 上面覆盖
// 我们自己的 线程。这不是整个故事:a TUI 在一个 终端 财务
// 报告和已审计财务报表 `agentty plugin add`
// 另一只有独立的变种动物 竞态 20发:20发丢失了两台服务器
// 中的一台。
//
// fork() 而不是产下 二进制程序 ,所以案件运行在 -suite 不
// 取决于一个建筑 CLI 或 单 PATH 每个孩子都做一个
// 完整的 加载 - > 修改 - > 存储周期, 丢失的更新寿命在此。
void cross_process_mutations_safe(const fs::path& dir) {
    std::println("--- cross_process_mutations_safe ---");
#ifdef _WIN32
    std::println("SKIP (no fork)\n");
    (void)dir;
#else
    const fs::path cfg = dir / "xproc" / "mcp.json";
    fs::create_directories(cfg.parent_path());

    constexpr int kRounds  = 8;
    constexpr int kWriters = 4;
    int lost = 0;

    for (int r = 0; r < kRounds; ++r) {
        write_file(cfg, R"({"mcpServers":{}})");

        std::vector<::pid_t> kids;
        for (int w = 0; w < kWriters; ++w) {
            const ::pid_t pid = ::fork();
            if (pid == 0) {
                // 子女:增加一个,然后离开 无需 运行
                // doctest 拆卸 -- _ 退出跳过退出 处理
                // 函数 父母的测试状态没有改变
                const auto name = "r" + std::to_string(r) + "w" + std::to_string(w);
                const auto rc = plug::add_server(::agentty::IoAccess::grant(), agtest::cap(), cfg, {name, "/bin/true", {}}, false);
                ::_exit(rc == plug::EditResult::Ok ? 0 : 1);
            }
            if (pid > 0) kids.push_back(pid);
        }
        for (::pid_t p : kids) { int st = 0; ::waitpid(p, &st, 0); }

        // 每个作家都用一个鲜明的名字,所以他们都必须生存下去.
        // 一个丢失的更新显示为缺失条目,而不是一个腐败的文件 -
        // store()的原子重命名已经防止了撕裂.
        const auto servers = plug::list_servers(cfg);
        if (static_cast<int>(servers.size()) != kWriters) ++lost;
    }

    check(lost == 0,
          "cross-process adds all survive (lost rounds: "
              + std::to_string(lost) + "/" + std::to_string(kRounds) + ")");
    std::println("PASS\n");
#endif
}

// DIRECTORY的配置路径不能使进程下降。
//
// `std::ifstream` 打开目录 SUCCESSFULLY 打开 Linux
// 失败的出现是作为例外 出现在第一个读数和nlohmann的 noexcept 解析 抓不到,
// 所以我还是要抓 `mkdir .agentty/mcp.json` 已中止 agentty
// 与 `basic_filebuf::underflow error reading the file: Is a directory`
// - 一个消息,既不命名 agentty 或从
// 一条路 `run` 和配置无关
//
// 配置 用户、其他工具和脚本可以放任何东西的地方,
// 所以 "not the kind of thing I expected"
// 一定不是,我必须去 -op。文档中的所有其它错误案例已经是;这是唯一的洞。
void directory_shaped_config_is_not_fatal(const fs::path& dir) {
    std::println("--- directory_shaped_config_is_not_fatal ---");
    const fs::path cfg = dir / "dirshaped" / "mcp.json";
    fs::create_directories(cfg);          // a 配置路径的DIRECTORY
    check(fs::is_directory(cfg), "fixture really is a directory");

    // 每个读者必须活下来,在修复之前,每个都扔了
    check(plug::list_servers(cfg).empty(), "list: no servers, no crash");
    check(!plug::is_server_trusted(::agentty::IoAccess::grant(), cfg, "anything"),
          "trust probe: false, no crash");

    // 任何作家都不可能把它打碎。一个处于这种地位的目录正是
    // 我们必须拒绝而不是替换的东西。 load()报告它为现在
    // 的-but-broken, 这是这里每一个变异的门。
    check(plug::add_server(::agentty::IoAccess::grant(), agtest::cap(), cfg, {"x", "/bin/true", {}}, false)
              == plug::EditResult::ParseError,
          "add refuses to overwrite a directory");
    check(fs::is_directory(cfg), "the directory is still there, untouched");
    std::println("PASS\n");
}

// 一个项目 mcp.json 不得 隐藏用户自己的服务器。
//
// 曾经有TWO梯级. read_config_servers() 通过scope::
// plan合并了项目+用户, 所以插件面板, `agentty config mcp`和
// 代表团都列出两者。 但是, CONNECT路径(mcp_tools) 称为
// 自己的resolve_config(), 它返回了ONE胜负文件 -- 因此一个仓库
// 拥有./.agentty/mcp.json的时刻, ~/.agentty/mcp.json
// 中的每个服务器静静地停止运行, 但仍在任何地方被列出。 您看到您的服务器,
// 它从未运行。
//
// plugin_model ( ) 系 , ( ) 不 探测器: 折叠
// read_config_servers ()一直以来,所以它从未显示错误。
// 断路就是那个叫 resolve_config () -- -- 连接循环和授权,
// 这说明现在两者同意。
void project_does_not_hide_user_servers(const fs::path& dir) {
    std::println("--- project_does_not_hide_user_servers ---");
    const fs::path home = dir / "merge_home";
    const fs::path work = dir / "merge_work";
    fs::create_directories(home / ".agentty");
    fs::create_directories(work / ".agentty");

    const std::string prev_home = std::getenv("AGENTTY_HOME")
                                      ? std::getenv("AGENTTY_HOME") : "";
    const bool     had_home = std::getenv("AGENTTY_HOME") != nullptr;
    const fs::path prev_cwd = fs::current_path();
    ::setenv("AGENTTY_HOME", (home / ".agentty").string().c_str(), 1);
    // 块 -trust 编号 工作区 因此,这可以衡量 LADDER ,
    // 而不是大门(大门有其自身的案件上方)。
    ::setenv("AGENTTY_MCP_ALLOW_PROJECT", "1", 1);
    fs::current_path(work);
    // 观测站的卫星点点 util::project_root (,
    // 即 WORKSPACE-ROOT 夹- 进程 -global 状态,
    // 而不是活的 cwd. chdir 独自将它留在任何较早的测试
    // 封存的地方,然后项目源完全解决到别的地方。 (同样陷阱的擦拭适配
    // 器测试击中了。)
    const auto old_ws = ::agentty::tools::util::workspace_root();
    ::agentty::tools::util::set_workspace_root(::agentty::IoAccess::grant(), work);

    // 来,来,来 HTTP 服务器在每一个范围内 -- http需要否
    // 二进制程序 永远不存在 永远产卵 -gated 所以唯一要测试的是
    // 是否看到两者
    write_file(work / ".agentty" / "mcp.json",
               R"({"mcpServers":{"projsrv":{"url":"http://127.0.0.1:9/p"}}})");
    write_file(home / ".agentty" / "mcp.json",
               R"({"mcpServers":{"usersrv":{"url":"http://127.0.0.1:9/u"}}})");

    // plugin_model () 已经合并(它使用)
    // read_config_servers 这条路是 BROKEN 是
    // 那个叫 resolve_config () -- -- 授权和
    // 连接循环。 ACP session/new 手执 下游.
    const auto launches = agentty::mcp::configured_servers_for_delegation();
    bool saw_proj = false, saw_user = false;
    for (const auto& l : launches) {
        if (l.name == "projsrv") saw_proj = true;
        if (l.name == "usersrv") saw_user = true;
    }
    check(saw_proj, "project server is delegable");
    check(saw_user, "USER server survives a project mcp.json (the merge bug)");

    // 上市表上也同意这个协议 IS 修复。
    const auto model = agentty::mcp::plugin_model();
    bool listed_user = false;
    for (const auto& s : model.servers)
        if (s.name == "usersrv") listed_user = true;
    check(listed_user == saw_user,
          "what the pane LISTS matches what delegation can RUN");

    fs::current_path(prev_cwd);
    ::agentty::tools::util::set_workspace_root(::agentty::IoAccess::grant(), old_ws);
    ::unsetenv("AGENTTY_MCP_ALLOW_PROJECT");
    if (had_home) ::setenv("AGENTTY_HOME", prev_home.c_str(), 1);
    else          ::unsetenv("AGENTTY_HOME");
    std::println("PASS\n");
}

// 一个连结的mcp.json必须写THROUGH(目标更新),
// 而不是被常规文件(它会孤儿用户的dotfile symlink)所取代.
void symlink_written_through(const fs::path& dir) {
    std::println("--- symlink_written_through ---");
    const fs::path real = dir / "sym" / "real.json";
    const fs::path link = dir / "sym" / "mcp.json";
    write_file(real, R"({"mcpServers":{}})");
    std::error_code ec;
    fs::create_symlink(real, link, ec);
    if (ec) { std::println("  (skip: symlinks unsupported here)\nPASS\n"); return; }

    check(plug::add_server(::agentty::IoAccess::grant(), agtest::cap(), link, {"date", "/x", {}}, false) == plug::EditResult::Ok,
          "add via symlink succeeds");
    check(fs::is_symlink(link), "mcp.json is STILL a symlink after the write");
    check(read_json(real).contains("mcpServers")
          && read_json(real)["mcpServers"].contains("date"),
          "the write landed in the symlink TARGET");
    std::println("PASS\n");
}

// 范围编辑 -routing : 以一个范围为对象的编辑 mcp.json 不得
// 摸对方的,这是... 不变量 插件后面的边框 选择面板 修补的一排 agentty 读自,
// 和 remove/toggle 路径( 3) edit_target
// 在设置中 状态更新函数 ),所以摇摆着一个 PROJECT 服务器
// 离开 USER 配置未变换和阴性 -versa.
void scope_edits_are_isolated(const fs::path& dir) {
    std::println("--- scope_edits_are_isolated ---");
    const fs::path proj = dir / "proj" / ".agentty" / "mcp.json";
    const fs::path user = dir / "user" / ".agentty" / "mcp.json";
    // 两个瞄准镜中相同的服务器名称,不同的命令.
    check(plug::add_server(::agentty::IoAccess::grant(), agtest::cap(), proj, {"date", "/project/date", {}}, false) == plug::EditResult::Ok,
          "seed project-scope server");
    check(plug::add_server(::agentty::IoAccess::grant(), agtest::cap(), user, {"date", "/user/date", {}}, false) == plug::EditResult::Ok,
          "seed user-scope server");

    // 禁用 仅 工程服务器( 模拟 a) 选择面板 切换 )),在项目行上.
    check(plug::set_server_disabled(::agentty::IoAccess::grant(), agtest::cap(), proj, "date", true) == plug::EditResult::Ok,
          "disable project server");
    check(plug::is_server_disabled(proj, "date"),  "project server now disabled");
    check(!plug::is_server_disabled(user, "date"), "USER server untouched by project edit");
    check(read_json(user)["mcpServers"]["date"]["command"] == "/user/date",
          "user command preserved across a project-scope edit");

    // 删除 仅 用户服务器( 镜像) `d` )),在用户行上.
    check(plug::remove_server(::agentty::IoAccess::grant(), agtest::cap(), user, "date") == plug::EditResult::Ok, "remove user server");
    check(!read_json(user)["mcpServers"].contains("date"), "user entry gone");
    check(read_json(proj)["mcpServers"].contains("date"),
          "PROJECT entry survives a user-scope removal");
    std::println("PASS\n");
}

// 项目 -config 信任门: a 工作区-local mcp.json 在
// 获得批准前不可信 CONTENT HASH ; 编辑它再 -gates (
// MCPoison的固定) 运行在自己的 AGENTTY_HOME + 重度数 沙箱
// 所以批准书和项目文件都是孤立的
void project_trust_gate(const fs::path& dir) {
    std::println("--- project_trust_gate ---");
    const fs::path home = dir / "trust_home";
    const fs::path work = dir / "trust_work";
    fs::create_directories(home);
    fs::create_directories(work / ".agentty");

    // 重新引导批准根 + cwd( 项目配置查询)。 商店会解决
    // util::user_root (: 重新点头 AGENTTY_HOME
    // 测试的家(在 二进制程序 主机为整个过程设定了一个,所以批准土地在测试
    // 中自己 沙箱 ,而不是共享的每 -process 一条,还清除了洞穴
    // 选择 -in 所以我们在测试内容 -hash 路径。
    const std::string prev_agt_home = std::getenv("AGENTTY_HOME") ? std::getenv("AGENTTY_HOME") : "";
    const bool        had_agt_home  = std::getenv("AGENTTY_HOME") != nullptr;
    const fs::path    prev_cwd      = fs::current_path();
    ::setenv("AGENTTY_HOME", (home / ".agentty").string().c_str(), 1);
    ::unsetenv("AGENTTY_MCP_ALLOW_PROJECT");
    fs::current_path(work);

    // 种子一个工程 stdio 服务器.
    check(plug::add_server(::agentty::IoAccess::grant(), agtest::cap(), plug::config_path(::agentty::IoAccess::grant(), true), {"date", "/bin/date", {}}, false)
              == plug::EditResult::Ok, "seed project server");

    // 默认情况下不可信——一个克隆人不能自动地将-run自己的命令自动化.
    check(!plug::is_project_config_trusted(::agentty::IoAccess::grant()), "project config untrusted by default");

    // 批准THIS内容__信任.
    check(plug::approve_project_config(::agentty::IoAccess::grant(), agtest::cap()), "approve records trust");
    check(plug::is_project_config_trusted(::agentty::IoAccess::grant()), "approved project config is trusted");

    // MCPoison: 以同名交换命令。 Name 字节 改变,
    // 所以散列改变,所以旧的核准不再匹配 ~ re -gated.
    check(plug::set_server_disabled(::agentty::IoAccess::grant(), agtest::cap(), plug::config_path(::agentty::IoAccess::grant(), true), "date", false)
              == plug::EditResult::Ok, "touch the config");
    check(plug::add_server(::agentty::IoAccess::grant(), agtest::cap(), plug::config_path(::agentty::IoAccess::grant(), true), {"date", "/evil/date", {}}, /*力=*/true)
              == plug::EditResult::Ok, "swap the command (attacker edit)");
    check(!plug::is_project_config_trusted(::agentty::IoAccess::grant()),
          "changed content RE-GATES trust (MCPoison fix)");

    // 隐蔽的 Opt-in 仍然可以做为覆盖的覆盖。
    ::setenv("AGENTTY_MCP_ALLOW_PROJECT", "1", 1);
    check(plug::is_project_config_trusted(::agentty::IoAccess::grant()), "env opt-in overrides the gate");
    ::unsetenv("AGENTTY_MCP_ALLOW_PROJECT");

    // 复还.
    fs::current_path(prev_cwd);
    if (had_agt_home) ::setenv("AGENTTY_HOME", prev_agt_home.c_str(), 1);
    else              ::unsetenv("AGENTTY_HOME");
    std::println("PASS\n");
}

// 人数 -server 信任: 核准 一个 服务器不能为另一个服务器祝福,
// 并编辑一个服务器的命令 -gates 只有那个服务器
void per_server_trust(const fs::path& dir) {
    std::println("--- per_server_trust ---");
    const fs::path home = dir / "ps_home";
    const fs::path work = dir / "ps_work";
    fs::create_directories(home);
    fs::create_directories(work / ".agentty");
    const std::string prev_agt_home = std::getenv("AGENTTY_HOME") ? std::getenv("AGENTTY_HOME") : "";
    const bool        had_agt_home  = std::getenv("AGENTTY_HOME") != nullptr;
    const fs::path    prev_cwd      = fs::current_path();
    ::setenv("AGENTTY_HOME", (home / ".agentty").string().c_str(), 1);
    ::unsetenv("AGENTTY_MCP_ALLOW_PROJECT");
    fs::current_path(work);
    const fs::path cfg = plug::config_path(::agentty::IoAccess::grant(), true);

    check(plug::add_server(::agentty::IoAccess::grant(), agtest::cap(), cfg, {"alpha", "/bin/alpha", {}}, false) == plug::EditResult::Ok,
          "seed alpha");
    check(plug::add_server(::agentty::IoAccess::grant(), agtest::cap(), cfg, {"beta", "/bin/beta", {}}, false) == plug::EditResult::Ok,
          "seed beta");

    // 只批准阿尔法。
    check(plug::approve_server(::agentty::IoAccess::grant(), agtest::cap(), cfg, "alpha"), "approve alpha");
    check(plug::is_server_trusted(::agentty::IoAccess::grant(), cfg, "alpha"),  "alpha trusted");
    check(!plug::is_server_trusted(::agentty::IoAccess::grant(), cfg, "beta"),   "beta NOT trusted by alpha's approval");
    // 整个文件不只因为有一个服务器才被信任.
    check(!plug::is_project_config_trusted(::agentty::IoAccess::grant()), "whole-file gate still closed");

    // 稍后添加第三个服务器——α仍然信任,γ为待处理.
    check(plug::add_server(::agentty::IoAccess::grant(), agtest::cap(), cfg, {"gamma", "/bin/gamma", {}}, false) == plug::EditResult::Ok,
          "add gamma later");
    check(plug::is_server_trusted(::agentty::IoAccess::grant(), cfg, "alpha"), "alpha still trusted after a new add");
    check(!plug::is_server_trusted(::agentty::IoAccess::grant(), cfg, "gamma"), "gamma pending");

    // 编辑α的指令(MCPoison)——只有α re-gates.
    check(plug::add_server(::agentty::IoAccess::grant(), agtest::cap(), cfg, {"alpha", "/evil/alpha", {}}, /*力=*/true)
              == plug::EditResult::Ok, "swap alpha's command");
    check(!plug::is_server_trusted(::agentty::IoAccess::grant(), cfg, "alpha"),
          "alpha re-gated after its command changed");

    // ⁇ 是身份的一部分:批准,然后更改 ⁇   ⁇   ⁇  re-gated.
    check(plug::add_server(::agentty::IoAccess::grant(), agtest::cap(), cfg, {"delta", "/bin/delta", {"--safe"}}, false)
              == plug::EditResult::Ok, "seed delta with args");
    check(plug::approve_server(::agentty::IoAccess::grant(), agtest::cap(), cfg, "delta"), "approve delta");
    check(plug::is_server_trusted(::agentty::IoAccess::grant(), cfg, "delta"), "delta trusted with its args");
    check(plug::add_server(::agentty::IoAccess::grant(), agtest::cap(), cfg, {"delta", "/bin/delta", {"--evil"}}, /*力=*/true)
              == plug::EditResult::Ok, "change delta's args");
    check(!plug::is_server_trusted(::agentty::IoAccess::grant(), cfg, "delta"), "delta re-gated on args change");

    fs::current_path(prev_cwd);
    if (had_agt_home) ::setenv("AGENTTY_HOME", prev_agt_home.c_str(), 1);
    else              ::unsetenv("AGENTTY_HOME");
    std::println("PASS\n");
}

// 一个通过服务器写 {type:"passthrough", url,
// asswitch:[names]} – 没有命令, 没有产卵。 宣布的DISPATCH
// 的工具注册(一个代理向模型上登出他们的模式定义;agentty 转发了 url)。
void passthrough_server_add(const fs::path& dir) {
    std::println("--- passthrough_server_add ---");
    const fs::path cfg = dir / "pt" / "mcp.json";
    plug::ServerSpec pt;
    pt.name = "headroom";
    pt.url  = "http://127.0.0.1:8787/v1/retrieve";
    pt.type = "passthrough";
    pt.passthrough = {"headroom_retrieve"};
    check(plug::add_server(::agentty::IoAccess::grant(), agtest::cap(), cfg, pt, false) == plug::EditResult::Ok,
          "passthrough add succeeds");
    json e = read_json(cfg)["mcpServers"]["headroom"];
    check(e["type"] == "passthrough", "type is passthrough");
    check(e["url"] == "http://127.0.0.1:8787/v1/retrieve", "url stored");
    check(e["passthrough"].is_array() && e["passthrough"].size() == 1
              && e["passthrough"][0] == "headroom_retrieve",
          "declared tool names stored");
    check(!e.contains("command"), "no command key on a passthrough server");

    // 圆 -trip 安全性: 稍后添加的无关服务器必须保存
    // 过关块 字节-for-byte.
    plug::ServerSpec other;
    other.name = "today"; other.command = "python3";
    check(plug::add_server(::agentty::IoAccess::grant(), agtest::cap(), cfg, other, false) == plug::EditResult::Ok,
          "sibling add ok");
    json again = read_json(cfg)["mcpServers"]["headroom"];
    check(again == e, "passthrough entry survives sibling edits");
}

TEST_CASE("plugin config") {
    agtest::ScopedEnvSandbox _env_guard;
    const fs::path sandbox =
        fs::temp_directory_path() / ("agentty_plugin_test_" +
                                     std::to_string(::getpid()));
    fs::create_directories(sandbox);

    std::println("=== plugin_config_test ===");
    creates_file_on_first_add(sandbox);
    round_trip_preserves_foreign_keys(sandbox);
    honours_servers_spelling(sandbox);
    no_clobber_without_force(sandbox);
    distinct_not_found(sandbox);
    refuses_broken_json(sandbox);
    list_reads_back(sandbox);
    tool_enable_disable(sandbox);
    server_enable_disable(sandbox);
    http_server_add(sandbox);
    passthrough_server_add(sandbox);
    scope_edits_are_isolated(sandbox);
    project_trust_gate(sandbox);
    per_server_trust(sandbox);
    malformed_disabled_no_throw(sandbox);
    non_object_entry_no_throw(sandbox);
    concurrent_mutations_safe(sandbox);
    symlink_written_through(sandbox);
    project_does_not_hide_user_servers(sandbox);
    directory_shaped_config_is_not_fatal(sandbox);
    cross_process_mutations_safe(sandbox);
    approvals_merge_across_processes(sandbox);

    std::error_code ec;
    fs::remove_all(sandbox, ec);
}
