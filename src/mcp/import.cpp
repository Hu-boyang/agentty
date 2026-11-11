// mcp/import.cpp —采用另一工具的MCP服务器。请参阅 import.hpp
// 了解为什么这是导入而不是第六个读取根。

#include "agentty/mcp/import.hpp"

#include "agentty/config/inventory.hpp"
#include "agentty/scope/scope.hpp"
#include "agentty/tool/plugin.hpp"
#include "agentty/util/home_dir.hpp"
#include "agentty/util/logx.hpp"
#include "agentty/util/toml_lite.hpp"
#include "agentty/util/user_root.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <set>

namespace agentty::mcp::import_ {

using json = nlohmann::json;

namespace {

// 从文档中拉出`mcpServers`/`servers`对象。
// 这两种拼写都是常见的（agentty 也接受），所以 "wrong" 也不是。
[[nodiscard]] const json* servers_of(const json& doc) noexcept {
    if (!doc.is_object()) return nullptr;
    if (auto it = doc.find("mcpServers"); it != doc.end() && it->is_object())
        return &*it;
    if (auto it = doc.find("servers"); it != doc.end() && it->is_object())
        return &*it;
    return nullptr;
}

// 这是agentty实际上可以使用的名称吗？
//
// 导入是TRUST BOUNDARY：字节来自另一工具写入的文件，
// 这意味着它们可以包含任何内容。名称不仅仅是一个标签——
// 它是用户键入的用于禁用服务器的键、它公开的每个工具的
// 前缀以及三个不同表中的 存储单元。
//
// 从敌对装置观察到：EMPTY名称渲染作为无人能够寻址的空白行，
// 并且包含NEWLINE的名称将`plugin list`分成两行，
// 因此表通过肉眼停止解析。两者都不是安全漏洞 -
// canonical_mcp_name() 清理了工具名称
// 下游 - 但两者都会产生用户无法管理的条目，这本身就是一种
// 破坏。
//
// 在边界处垃圾而不是将垃圾运入内部。故意使用 不 字符
// 允许 -list：名称合法地带有点、破折号和非
// -ASCII，并且猜测哪些是 "safe" 将拒绝真实
// 服务器以防止外观问题。
[[nodiscard]] bool usable_name(std::string_view n) noexcept {
    if (n.empty()) return false;
    if (n.size() > 128) return false;         // 标签，不是载荷
    for (unsigned char c : n)
        if (c < 0x20 || c == 0x7f) return false;   // 控制字节
    return true;
}

// 对一个外来条目进行解码以显示并验证它。
//
// ENTRY ITSELF保持完整。 `mcp.json` 是跨
// 工具的 模式定义，因此环境块、自定义 头文件、timeoutMs
// 或 tools.exclude 都是 agentty 自己的连接
// 路径读取的内容 - 从我们费心命名的少数字段中重建条目就是导入
// 静默删除使服务器工作的 API 键的方式。有损导入比被拒绝更糟糕。
[[nodiscard]] bool translate(const std::string& name, const json& e,
                             const fs::path& from, Found& out) {
    if (!e.is_object()) return false;
    if (!usable_name(name)) return false;
    out.name  = name;
    out.from  = from;
    out.entry = e;   // 逐字记录
    if (auto it = e.find("command"); it != e.end() && it->is_string())
        out.command = it->get<std::string>();
    if (auto it = e.find("url"); it != e.end() && it->is_string())
        out.url = it->get<std::string>();
    // 两者都不具备的服务器不是服务器。跳过节拍导入
    // 损坏的条目，然后无法连接且没有任何解释。
    return !out.command.empty() || !out.url.empty();
}

// Claude代码将每个-project服务器保留在一个大~/.claude.json.中的项目下。
// <abs-path>.mcpServers仅进入THIS项目
// 的条目——采用另一个结帐的服务器将是一个惊喜。
void scan_claude_nested(const json& doc, const fs::path& project,
                        const fs::path& from, std::vector<Found>& out) {
    auto projects = doc.find("projects");
    if (projects == doc.end() || !projects->is_object()) return;
    const std::string key = project.string();
    auto mine = projects->find(key);
    if (mine == projects->end() || !mine->is_object()) return;
    const json* servers = servers_of(*mine);
    if (!servers) return;
    for (auto it = servers->begin(); it != servers->end(); ++it) {
        Found f;
        if (translate(it.key(), it.value(), from, f)) out.push_back(std::move(f));
    }
}

void scan_file(const fs::path& file, const fs::path& project,
               std::vector<Found>& out, std::string& error, int& skipped) {
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) return;
    std::ifstream in(file);
    if (!in) return;
    json doc = json::parse(in, nullptr, /*投掷=*/false);
    if (doc.is_discarded()) {
        // 拒绝而不是猜测。格式错误的外部配置
        // 需要用户修复；进口一半比不进口还糟糕。
        error = "could not parse " + file.string();
        return;
    }
    if (const json* servers = servers_of(doc)) {
        for (auto it = servers->begin(); it != servers->end(); ++it) {
            Found f;
            if (translate(it.key(), it.value(), file, f))
                out.push_back(std::move(f));
            else
                ++skipped;   // 没有 传输层，或者我们无法管理的名称
        }
    }
    scan_claude_nested(doc, project, file, out);
}

// Codex将TOML中的服务器保留在[mcp_servers.<name>]下并具有其
// 自己的字段名称。将它们映射到我们的；任何没有同等内容的东西都放在 `notes` 中。
[[nodiscard]] json codex_entry(const json& c, std::vector<std::string>& notes) {
    json e = json::object();
    auto str = [&](const char* k) -> std::string {
        auto it = c.find(k);
        return it != c.end() && it->is_string() ? it->get<std::string>() : std::string{};
    };
    if (auto v = str("command"); !v.empty()) e["command"] = v;
    if (auto v = str("url"); !v.empty()) {
        e["url"]  = v;
        e["type"] = "http";   // Codex的远程服务器可流式传输HTTP
    }
    if (auto v = str("cwd"); !v.empty()) e["cwd"] = v;
    if (auto it = c.find("args"); it != c.end() && it->is_array()) e["args"] = *it;
    if (auto it = c.find("env"); it != c.end() && it->is_object()) e["env"] = *it;
    if (auto it = c.find("env_vars"); it != c.end() && it->is_array() && !it->empty())
        notes.push_back("env_vars (forwarded from your shell) not copied; "
                        "agentty servers inherit the environment");

    json headers = json::object();
    if (auto it = c.find("http_headers"); it != c.end() && it->is_object())
        headers = *it;
    // 这些有意存在于环境变量中，因此它们不会被复制到文件中。
    if (auto it = c.find("env_http_headers"); it != c.end() && it->is_object())
        for (auto h = it->begin(); h != it->end(); ++h)
            if (h->is_string())
                notes.push_back("header " + h.key() + " comes from $"
                                + h->get<std::string>() + "; add it under headers by hand");
    if (auto v = str("bearer_token_env_var"); !v.empty())
        notes.push_back("bearer token comes from $" + v
                        + "; add an Authorization header by hand, or use agentty mcp-login");
    if (!headers.empty()) e["headers"] = headers;

    auto secs = [&](const char* k) -> long {
        auto it = c.find(k);
        if (it == c.end() || !it->is_number()) return 0;
        return static_cast<long>(it->get<double>() * 1000.0);
    };
    if (long ms = secs("tool_timeout_sec"); ms > 0) e["timeoutMs"] = ms;
    long start_ms = secs("startup_timeout_sec");
    if (auto it = c.find("startup_timeout_ms"); it != c.end() && it->is_number_integer())
        start_ms = it->get<long>();
    if (start_ms > 0) e["connectTimeoutMs"] = start_ms;

    json tools = json::object();
    if (auto it = c.find("disabled_tools"); it != c.end() && it->is_array()) tools["exclude"] = *it;
    if (!tools.empty()) e["tools"] = tools;
    if (auto it = c.find("enabled_tools"); it != c.end() && it->is_array())
        notes.push_back("enabled_tools not copied; agentty only has tools.exclude");

    if (auto it = c.find("enabled"); it != c.end() && it->is_boolean() && !it->get<bool>())
        e["disabled"] = true;
    if (auto it = c.find("oauth"); it != c.end() && it->is_object())
        if (auto id = it->find("client_id"); id != it->end() && id->is_string())
            e["client_id"] = *id;
    return e;
}

void scan_codex_file(const fs::path& file, std::vector<Found>& out,
                     std::string& error, int& skipped) {
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) return;
    std::ifstream in(file, std::ios::binary);
    if (!in) return;
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    json doc;
    std::string why;
    if (!util::toml_lite::parse(text, doc, why)) {
        error = "could not parse " + file.string() + " (" + why + ")";
        return;
    }
    auto servers = doc.find("mcp_servers");
    if (servers == doc.end() || !servers->is_object()) return;
    for (auto it = servers->begin(); it != servers->end(); ++it) {
        if (!it->is_object()) { ++skipped; continue; }
        std::vector<std::string> notes;
        const json entry = codex_entry(*it, notes);
        Found f;
        if (translate(it.key(), entry, file, f)) {
            f.notes = std::move(notes);
            out.push_back(std::move(f));
        } else {
            ++skipped;
        }
    }
}

// 名称agentty已经有了，因此扫描可以标志冲突，而无需调用者re
// -reading我们自己的配置。
[[nodiscard]] std::set<std::string> our_server_names() {
    std::set<std::string> names;
    const scope::Env env = scope::current_env(config::kMcpLayout);
    for (const scope::Source& src : scope::plan(config::kMcpLayout, env)) {
        const fs::path file = (src.locus == scope::Locus::Explicit
                               && env.explicit_config)
            ? *env.explicit_config
            : src.base / fs::path{config::kMcpLayout.leaf};
        std::error_code ec;
        if (!fs::is_regular_file(file, ec)) continue;
        std::ifstream in(file);
        json doc = json::parse(in, nullptr, false);
        if (const json* s = servers_of(doc))
            for (auto it = s->begin(); it != s->end(); ++it)
                names.insert(it.key());
    }
    return names;
}

}  // 命名空间结束。

std::string_view name_of(Tool t) noexcept {
    switch (t) {
        case Tool::Junie:      return "junie";
        case Tool::ClaudeCode: return "claude";
        case Tool::Cursor:     return "cursor";
        case Tool::VsCode:     return "vscode";
        case Tool::Codex:      return "codex";
    }
    return "?";
}

std::vector<Tool> all_tools() {
    return {Tool::Junie, Tool::ClaudeCode, Tool::Cursor, Tool::VsCode, Tool::Codex};
}

bool parse_tool(std::string_view s, Tool& out) noexcept {
    for (Tool t : {Tool::Junie, Tool::ClaudeCode, Tool::Cursor, Tool::VsCode, Tool::Codex})
        if (name_of(t) == s) { out = t; return true; }
    // 接受明显的别名而不是对此迂腐。
    if (s == "jetbrains" || s == "intellij") { out = Tool::Junie; return true; }
    if (s == "claude-code")                  { out = Tool::ClaudeCode; return true; }
    if (s == "code")                         { out = Tool::VsCode; return true; }
    if (s == "codex-cli" || s == "openai")   { out = Tool::Codex; return true; }
    return false;
}

Locations locations_of(Tool t, const fs::path& home, const fs::path& project) {
    switch (t) {
        case Tool::Junie:
            return {home / ".junie" / "mcp" / "mcp.json",
                    project / ".junie" / "mcp" / "mcp.json"};
        case Tool::ClaudeCode:
            // 用户范围是一个大文件，每个项目嵌套有服务器；
            // 项目范围是 仓库 根目录下的可移植 .mcp.json。
            return {home / ".claude.json", project / ".mcp.json"};
        case Tool::Cursor:
            return {home / ".cursor" / "mcp.json",
                    project / ".cursor" / "mcp.json"};
        case Tool::VsCode:
            // 没有记录的user-level文件；仅限 工作区。
            return {{}, project / ".vscode" / "mcp.json"};
        case Tool::Codex: {
            fs::path dir = home / ".codex";
            if (const char* c = std::getenv("CODEX_HOME"); c && *c) dir = c;
            return {home.empty() && dir == fs::path{".codex"} ? fs::path{} : dir / "config.toml",
                    project / ".codex" / "config.toml"};
        }
    }
    return {};
}

Scan scan(Tool t, const fs::path& home, const fs::path& project) {
    Scan s;
    s.tool = t;
    const Locations loc = locations_of(t, home, project);
    const auto existing = our_server_names();
    for (const fs::path& p : {loc.user, loc.project}) {
        if (p.empty()) continue;
        s.looked_in.push_back(p);
        if (t == Tool::Codex) scan_codex_file(p, s.servers, s.error, s.skipped);
        else                  scan_file(p, project, s.servers, s.error, s.skipped);
    }
    for (Found& f : s.servers) f.conflicts = existing.contains(f.name);
    AGT_LOG(Persist, Debug, "mcp", "import.scan {}: {} server(s) in {} path(s)",
            name_of(t), s.servers.size(), s.looked_in.size());
    return s;
}

std::vector<Scan> scan_all(const fs::path& home, const fs::path& project) {
    std::vector<Scan> out;
    for (Tool t : all_tools()) {
        Scan s = scan(t, home, project);
        if (!s.servers.empty() || !s.error.empty()) out.push_back(std::move(s));
    }
    return out;
}

Outcome adopt(Io io, const lock_levels::below_file_lane& cap, const std::vector<Found>& servers, const Options& opts) {
    Outcome o;
    o.into = util::user_root() / "mcp.json";
    for (const Found& f : servers) {
        if (f.conflicts && !opts.force) { ++o.skipped; continue; }
        if (opts.dry_run) { ++o.imported; continue; }
        // 逐字记录，而不是来自ServerSpec的re-derived：参见Found::entry。
        const auto r = tools::plugin::add_server_raw(io, cap, o.into, f.name, f.entry,
                                                     opts.force);
        if (r == tools::plugin::EditResult::Ok) ++o.imported;
        else                                    ++o.failed;
    }
    AGT_LOG(Persist, Info, "mcp", "import.adopt: {} in, {} skipped, {} failed",
            o.imported, o.skipped, o.failed);
    return o;
}

namespace {

void print_scan(const Scan& s) {
    std::printf("%s\n", std::string{name_of(s.tool)}.c_str());
    for (const fs::path& p : s.looked_in) {
        std::error_code ec;
        std::printf("  %s%s\n", p.string().c_str(),
                    fs::is_regular_file(p, ec) ? "" : "   (missing)");
    }
    if (!s.error.empty()) {
        std::printf("  error: %s\n", s.error.c_str());
        return;
    }
    if (s.servers.empty()) { std::printf("  no servers\n"); return; }
    for (const Found& f : s.servers) {
        std::printf("    %-20s %s%s\n", f.name.c_str(),
                    f.command.empty() ? f.url.c_str() : f.command.c_str(),
                    f.conflicts ? "   (agentty already has this name)" : "");
        for (const std::string& n : f.notes)
            std::printf("      note: %s\n", n.c_str());
    }
    // 切勿默默地丢弃条目。 "3 of 5 imported" 没有
    // 任何解释是有人得出导入已损坏的结论的。
    if (s.skipped > 0)
        std::printf("    %d entry(s) skipped: no command or url, or a name"
                    " agentty cannot manage\n", s.skipped);
}

}  // 命名空间结束。

int cli(Io io, const maya::unlocked& cap, const std::vector<std::string>& argv) {
    Options opts;
    std::string want;
    for (std::size_t i = 0; i < argv.size(); ++i) {
        const std::string& a = argv[i];
        if (a == "--dry-run" || a == "-n") opts.dry_run = true;
        else if (a == "--force")           opts.force   = true;
        else if (a == "--from" && i + 1 < argv.size()) want = argv[++i];
        else if (a.rfind("--from=", 0) == 0) want = a.substr(7);
        else if (a == "--help" || a == "-h") {
            std::printf(
                "usage: agentty mcp import [--from <tool>] [--dry-run] [--force]\n\n"
                "Copy another tool's MCP servers into ~/.agentty/mcp.json.\n"
                "Known tools: junie (jetbrains), claude, cursor, vscode, codex.\n\n"
                "With no --from, lists what every known tool offers.\n"
                "A name agentty already has is left alone unless --force.\n");
            return 0;
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", a.c_str());
            return 1;
        }
    }

    const fs::path home = util::home_dir_or_empty();
    std::error_code ec;
    const fs::path project = fs::current_path(ec);

    // 无--from：报告，不予采纳。发现永远不应该是一个突变——
    // "what do you see?" 和 "take it" 是不同的问题。
    if (want.empty()) {
        const auto scans = scan_all(home, project);
        if (scans.empty()) {
            std::printf("no MCP config found for any known tool.\n"
                        "looked for: junie, claude, cursor, vscode, codex\n");
            return 0;
        }
        for (const Scan& s : scans) print_scan(s);
        std::printf("\n`agentty mcp import --from <tool>` to adopt.\n");
        return 0;
    }

    Tool t{};
    if (!parse_tool(want, t)) {
        std::fprintf(stderr, "unknown tool: %s\n\nknown:", want.c_str());
        for (Tool k : all_tools())
            std::fprintf(stderr, " %s", std::string{name_of(k)}.c_str());
        std::fprintf(stderr, "\n");
        return 1;
    }

    const Scan s = scan(t, home, project);
    print_scan(s);
    if (!s.error.empty()) return 1;
    if (s.servers.empty()) return 0;

    const Outcome o = adopt(io, cap, s.servers, opts);
    std::printf("\n%s %d server(s) %s %s\n",
                opts.dry_run ? "would import" : "imported",
                o.imported,
                opts.dry_run ? "into" : "into",
                o.into.string().c_str());
    if (o.skipped > 0)
        std::printf("skipped %d name(s) agentty already has"
                    " (--force to replace)\n", o.skipped);
    if (o.failed > 0) {
        std::printf("%d failed to write\n", o.failed);
        return 1;
    }
    return 0;
}

}  // namespace agentty::mcp::import_
