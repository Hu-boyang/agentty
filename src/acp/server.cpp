// agentty::acp::AgentServer-实施。设计见 server.hpp。
//
// TUI转向环（src/runtime/app/cmd_factory.cpp)的无头模拟。
// 重用SAME提供者、工具、传输整形和权限策略，在工作线程 线程
// 上同步驱动并通过rpc::Peer作为session/update
// 通知发送到客户端。

#include "agentty/acp/server.hpp"
#include "agentty/version.hpp"
#include "agentty/util/digits.hpp"

#include "agentty/util/base64.hpp"
#include "agentty/util/logx.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <map>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <format>
#include <sstream>
#include <fstream>
#include <stop_token>

#include <maya/runtime.hpp>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "agentty/diff/diff.hpp"
#include "agentty/domain/catalog.hpp"
#include "agentty/domain/profile.hpp"
#include "agentty/io/persistence.hpp"
#include "agentty/io/shared_file.hpp"
#include "agentty/provider/prompt_policy.hpp"
#include "agentty/provider/auth_state.hpp"
#include "agentty/provider/catalog_sources.hpp"
#include "agentty/provider/error_class.hpp"
#include "agentty/provider/credentials.hpp"
#include "agentty/provider/provider.hpp"
#include "agentty/provider/selection.hpp"
#include "agentty/runtime/msg.hpp"
#include "agentty/runtime/view/helpers.hpp"
#include "agentty/tool/policy.hpp"
#include "agentty/tool/registry.hpp"
#include "agentty/tool/tool_loading.hpp"
#include "agentty/tool/tool_plan.hpp"
#include "agentty/runtime/app/update/stream_args.hpp"  // canonify_tool_args (`cmd`→`command`)
#include "agentty/runtime/app/cmd_factory.hpp"        // wire_messages_for，压实提示词
#include "agentty/tool/commands.hpp"
#include "agentty/tool/skills.hpp"
#include "agentty/runtime/slash.hpp"   // /name 的一个解析器
#include "agentty/tool/spec.hpp"
#include "agentty/tool/tool.hpp"
#include "agentty/tool/util/fs_helpers.hpp"
#include "agentty/tool/util/sandbox.hpp"
#include "agentty/tool/util/utf8.hpp"
#include "agentty/util/dbglog.hpp"
#include "agentty/util/background.hpp"
#include "agentty/util/self_exe.hpp"
#if AGENTTY_MCP
#include "agentty/mcp/client.hpp"
#endif


namespace agentty::acp {
namespace {

using nlohmann::json;
namespace a = ::acp;

// 向客户端抛出其RpcError的调用，用于try/catch路径。
template <jsonrpc::IsMethod M>
typename M::result call(const maya::unlocked& cap, rpc::Peer& peer, const typename M::params& p) {
    auto r = peer.call<M>(cap, p);
    if (!r) throw r.error();
    return std::move(*r);
}

// 编辑器的additionalDirectories，已检查：每个
// 都必须是绝对目录并且是现有目录（规范要求绝对目录；缺少一个是用户
// 应该看到的错误，而不是默默地授权什么都没有的根目录）。
std::vector<std::string> extra_roots_of(const a::Maybe<a::List<std::string>>& dirs) {
    std::vector<std::string> out;
    if (!dirs) return out;
    for (const auto& d : *dirs) {
        std::filesystem::path p{d};
        std::error_code ec;
        if (!p.is_absolute())
            throw a::RpcError(a::errc::InvalidParams,
                              "additionalDirectories: '" + d + "' is not absolute");
        if (!std::filesystem::is_directory(p, ec))
            throw a::RpcError(a::errc::InvalidParams,
                              "additionalDirectories: '" + d + "' is not a directory");
        out.push_back(std::filesystem::weakly_canonical(p, ec).string());
    }
    return out;
}

// Unix秒作为ISO 8601 UTC时间戳，格式为SessionInfo.updatedAt想要的。
// 不是 std::format("{:%FT%TZ}")：chrono 格式化会拉动 libstdc
// ++ 的语言环境编码转换，这需要 iconv，而 static-libstdc++ macOS
// 版本 链接 没有 iconv（libSystem 不导出它）——它破坏了 0.9.20 mac 版本。
// gmtime + strftime 是 locale-free 并且与这里相同。
std::string iso8601(std::int64_t unix_s) {
    const std::time_t t = static_cast<std::time_t>(unix_s);
    std::tm tm{};
#if defined(_WIN32)
    if (::gmtime_s(&tm, &t) != 0) return {};
#else
    if (!::gmtime_r(&t, &tm)) return {};
#endif
    char buf[32];
    const std::size_t n = std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buf, n);
}

// 一个确切的 git-style 补丁 用于更改，用于 JetBrains AIR 的 diffPatch。
// 当无法精确时（CR 字节，没有差异块），则 nullopt，
// 因此调用者按照 AIR 合约的要求发送普通 diff。
std::optional<std::string> air_git_patch(const FileChange& ch) {
    if (ch.hunks.empty()) return std::nullopt;
    if (ch.original_contents.find('\r') != std::string::npos ||
        ch.new_contents.find('\r') != std::string::npos)
        return std::nullopt;
    // 路径：无前导斜杠、正斜杠、git的默认引用。
    std::string rel = ch.path;
    std::replace(rel.begin(), rel.end(), '\\', '/');
    while (!rel.empty() && rel.front() == '/') rel.erase(rel.begin());
    if (rel.empty()) return std::nullopt;
    const auto quote = [](const std::string& prefix, const std::string& p) {
        bool needs = false;
        for (unsigned char c : p)
            if (c == '"' || c == '\\' || c < 0x20 || c >= 0x7f) { needs = true; break; }
        const std::string full = prefix + p;
        if (!needs) return full;
        std::string q = "\"";
        for (unsigned char c : full) {
            if (c == '"' || c == '\\') { q += '\\'; q += static_cast<char>(c); }
            else if (c == '\n') q += "\\n";
            else if (c == '\t') q += "\\t";
            else if (c < 0x20 || c >= 0x7f) {
                char buf[5]; std::snprintf(buf, sizeof buf, "\\%03o", c); q += buf;
            } else q += static_cast<char>(c);
        }
        return q + "\"";
    };
    const bool added = ch.original_contents.empty();
    const std::string a = quote("a/", rel), b = quote("b/", rel);
    const std::string tab = rel.find(' ') != std::string::npos ? "\t" : "";
    std::string out = "diff --git " + a + " " + b + "\n";
    if (added) out += "new file mode 100644\n";
    out += added ? "--- /dev/null\n" : "--- " + a + tab + "\n";
    out += "+++ " + b + tab + "\n";
    const bool old_nl = ch.original_contents.empty() || ch.original_contents.back() == '\n';
    const bool new_nl = ch.new_contents.empty() || ch.new_contents.back() == '\n';
    // 缺失的最后换行符需要准确放置"\ No newline"标记；
    // 与其冒着错误 补丁 的风险，不如发送简单的 diff（AIR 的 回退）。
    if (!old_nl || !new_nl) return std::nullopt;
    // Git-exact 行：内容以 '\n' 结尾，
    // 因此将其后面的空部分删除（显示差异将其保留为虚线）。
    auto lines = [](const std::string& s) {
        std::vector<std::string> out;
        std::size_t pos = 0;
        while (pos < s.size()) {
            const auto nl = s.find('\n', pos);
            out.push_back(s.substr(pos, nl - pos));
            pos = nl + 1;
        }
        return out;
    };
    const auto fc = diff::compute(rel, ch.original_contents, ch.new_contents);
    const auto a_lines = lines(ch.original_contents);
    const auto b_lines = lines(ch.new_contents);
    for (const auto& h : fc.hunks) {
        // 从其范围重建每个差异块的正文，夹紧到真实线。
        std::string body;
        int old_len = 0, new_len = 0;
        std::istringstream in(h.patch);
        std::string ln;
        int ai = h.old_start - 1, bi = h.new_start - 1;
        while (std::getline(in, ln)) {
            if (ln.empty()) continue;
            const char tag = ln[0];
            if (tag == ' ') {
                if (ai >= static_cast<int>(a_lines.size()) || bi >= static_cast<int>(b_lines.size())) break;
                body += " " + a_lines[ai++] + "\n"; ++bi; ++old_len; ++new_len;
            } else if (tag == '-') {
                if (ai >= static_cast<int>(a_lines.size())) continue;   // 幻影线
                body += "-" + a_lines[ai++] + "\n"; ++old_len;
            } else if (tag == '+') {
                if (bi >= static_cast<int>(b_lines.size())) continue;   // 幻影线
                body += "+" + b_lines[bi++] + "\n"; ++new_len;
            }
        }
        if (old_len == 0 && new_len == 0) continue;
        const int old_start = old_len == 0 ? h.old_start - 1 : h.old_start;
        const int new_start = new_len == 0 ? h.new_start - 1 : h.new_start;
        out += "@@ -" + std::to_string(added ? 0 : old_start) + "," + std::to_string(old_len) +
               " +" + std::to_string(new_start) + "," + std::to_string(new_len) + " @@\n";
        out += body;
    }
    if (out.find("\n@@ ") == std::string::npos) return std::nullopt;
    return out;
}


// 构建文本ContentBlock。
a::ContentBlock text_block(std::string s) {
    return a::ContentBlock{a::TextContent{std::move(s), a::Nothing, json::object()}};
}

// `s`的第一行，尾随CR被剥离——对于从可能-multiline
// command/body.构建的一个-line卡标题
std::string first_line(std::string s) {
    if (auto nl = s.find('\n'); nl != std::string::npos) s.resize(nl);
    if (!s.empty() && s.back() == '\r') s.pop_back();
    return s;
}

// agentty spec::Kind→acp::ToolKind。
a::ToolKind acp_tool_kind(std::string_view tool_name) {
    namespace sp = tools::spec;
    const auto* s = sp::lookup(tool_name);
    if (!s) return a::ToolKind::Other;
    switch (s->kind) {
        case sp::Kind::Read:           return a::ToolKind::Read;
        case sp::Kind::Edit:           return a::ToolKind::Edit;
        case sp::Kind::Write:          return a::ToolKind::Edit;
        case sp::Kind::Move:           return a::ToolKind::Edit;
        case sp::Kind::Remove:         return a::ToolKind::Edit;
        case sp::Kind::Bash:           return a::ToolKind::Execute;
        case sp::Kind::ProcessStart:   return a::ToolKind::Execute;
        case sp::Kind::ProcessPoll:    return a::ToolKind::Execute;
        case sp::Kind::ProcessStop:    return a::ToolKind::Execute;
        case sp::Kind::Diagnostics:    return a::ToolKind::Execute;
        case sp::Kind::Test:           return a::ToolKind::Execute;
        case sp::Kind::GitCommit:      return a::ToolKind::Execute;
        case sp::Kind::GitBranch:      return a::ToolKind::Execute;
        case sp::Kind::GitStash:       return a::ToolKind::Execute;
        case sp::Kind::GitRebase:      return a::ToolKind::Execute;
        case sp::Kind::GitCherryPick:  return a::ToolKind::Execute;
        case sp::Kind::Grep:           return a::ToolKind::Search;
        case sp::Kind::Glob:           return a::ToolKind::Search;
        case sp::Kind::FindDefinition: return a::ToolKind::Search;
        case sp::Kind::SearchDocs:     return a::ToolKind::Search;
        case sp::Kind::SearchCode:     return a::ToolKind::Search;
        case sp::Kind::RepoMap:        return a::ToolKind::Search;
        case sp::Kind::ListDir:        return a::ToolKind::Read;
        case sp::Kind::GitStatus:      return a::ToolKind::Read;
        case sp::Kind::GitDiff:        return a::ToolKind::Read;
        case sp::Kind::GitLog:         return a::ToolKind::Read;
        case sp::Kind::GitShow:        return a::ToolKind::Read;
        case sp::Kind::GitBlame:       return a::ToolKind::Read;
        case sp::Kind::WebFetch:       return a::ToolKind::Fetch;
        case sp::Kind::WebSearch:      return a::ToolKind::Fetch;
        case sp::Kind::Todo:           return a::ToolKind::Think;
        case sp::Kind::Remember:       return a::ToolKind::Other;
        case sp::Kind::Forget:         return a::ToolKind::Other;
        case sp::Kind::Wipe:           return a::ToolKind::Other;
        case sp::Kind::Task:           return a::ToolKind::Think;
        case sp::Kind::Skill:          return a::ToolKind::Read;
    }
    // 无默认值：/fall-through返回——切换对于Kind来说是详尽的，因此在此处添加新
    // 工具Kind re-triggers -Wswitch（在-Werror下构建错误）而不是
    // 默默地映射到其他。这就是 DESIGN.md
    // "sum type gains an arm → build error, not runtime ghost" 规则。
    return a::ToolKind::Other;   // 无法接通
}

// 当前 `path` 相对于会话的 工作区 根（当会话位于其下时）-
// Zed 在工具卡中显示项目-relative 路径，绝对路径是噪音。
// 镜像claude-code-acp的toDisplayPath，
// 但是我们关闭客户端实际打开的会话cwd而不是process().cwd。
std::string display_path(std::string_view path, std::string_view cwd) {
    if (path.empty() || cwd.empty()) return std::string{path};
    std::string_view c = cwd;
    while (!c.empty() && c.back() == '/') c.remove_suffix(1);
    if (path.size() > c.size() + 1 && path.substr(0, c.size()) == c
        && path[c.size()] == '/')
        return std::string{path.substr(c.size() + 1)};
    return std::string{path};
}

// 从 grep 调用 重建 ripgrep-style 命令行，使得卡读起来像 claude-code-acp
// 显示的那样：`grep -i --include="*.cpp" "pattern" path`。
// 首先是 标志，然后是 glob 的 --include，然后是引用的模式，
// 然后是路径——确切的顺序并引用 claude-code-acp 的 Grep 案例发出。
std::string grep_command(const json& args) {
    auto s = [&](const char* k) -> std::string {
        return (args.contains(k) && args[k].is_string()) ? args[k].get<std::string>() : std::string{};
    };
    auto b = [&](const char* k) -> bool {
        return args.contains(k) && args[k].is_boolean() && args[k].get<bool>();
    };
    std::string cmd = "grep";
    // Case-insensitive为默认； claude-code-acp 仅附加 `-i`（对于
    // 其 case_insensitive 标志）——没有 `-s` 臂。 agentty的标志是
    // case_sensitive，因此仅当NOT情况-sensitive时才发出`-i`。
    if (!b("case_sensitive")) cmd += " -i";
    const std::string glob = s("glob");
    if (!glob.empty()) cmd += " --include=\"" + glob + "\"";
    const std::string pat = s("pattern");
    if (!pat.empty()) cmd += " \"" + pat + "\"";
    const std::string path = s("path");
    if (!path.empty()) cmd += " " + path;
    return cmd;
}

// One-line工具调用卡的人类标题。 `cwd`（可能为空）让文件路径
// 渲染项目-relative。 分派 在 带类型的 spec::Kind 上，
// 因此新的工具臂迫使我们在这里做出决定，而不是默默地落入赤裸裸的名字。
std::string tool_title(const ToolUse& tc, std::string_view cwd = {}) {
    namespace sp = tools::spec;
    const auto& args = tc.args;
    const std::string& n = tc.name.value;
    auto str = [&](const char* k) -> std::string {
        if (args.contains(k) && args[k].is_string()) return args[k].get<std::string>();
        return {};
    };
    auto num = [&](const char* k) -> std::optional<std::int64_t> {
        if (args.contains(k) && args[k].is_number_integer())
            return args[k].get<std::int64_t>();
        return std::nullopt;
    };
    auto path_arg = [&]() {
        for (const char* k : {"path", "file_path", "filepath", "filename"}) {
            std::string p = str(k);
            if (!p.empty()) return display_path(p, cwd);
        }
        return std::string{};
    };
    auto clip = [](std::string s, std::size_t n) {
        if (auto nl = s.find('\n'); nl != std::string::npos) s.resize(nl);
        if (s.size() > n) { s.resize(n); s += "\xe2\x80\xa6"; }
        return s;
    };

    const auto* spec = sp::lookup(n);
    const sp::Kind kind = spec ? spec->kind : sp::Kind::Read;
    if (!spec) return n;

    switch (kind) {
        case sp::Kind::Read: {
            std::string p = path_arg();
            // claude-code-acp使用"File"作为no-path 回退，并将范围格式化为
            // " (offset - offset+limit-1)"/" (from line N)"。
            auto off = num("offset"); if (!off) off = num("start_line");
            auto lim = num("limit");
            auto end = num("end_line");
            std::string label = p.empty() ? std::string{"File"} : p;
            if (off && end) return "Read " + label + " (" + std::to_string(*off) + " - " + std::to_string(*end) + ")";
            if (lim && *lim > 0) {
                std::int64_t o = off ? *off : 1;
                return "Read " + label + " (" + std::to_string(o) + " - " + std::to_string(o + *lim - 1) + ")";
            }
            if (off) return "Read " + label + " (from line " + std::to_string(*off) + ")";
            return "Read " + label;
        }
        case sp::Kind::Edit: {
            std::string p = path_arg();
            return p.empty() ? "Edit" : "Edit " + p;
        }
        case sp::Kind::Write: {
            std::string p = path_arg();
            return p.empty() ? "Write" : "Write " + p;
        }
        case sp::Kind::Move: {
            auto source = display_path(str("source"), cwd);
            auto destination = display_path(str("destination"), cwd);
            return source.empty() ? "Move" : "Move " + source + " → " + destination;
        }
        case sp::Kind::Remove: {
            auto path = path_arg();
            return path.empty() ? "Remove" : "Remove " + path;
        }
        case sp::Kind::Bash:
        case sp::Kind::Diagnostics:
        case sp::Kind::Test:
        case sp::Kind::ProcessStart: {
            // 匹配claude-code-acp:命令本身IS标题，
            // 完整显示(客户端single-line-clamps用于显示，
            // 整个命令也通过command_content位于卡的内容块中)。
            // 只有第一行 - heredoc/multiline 正文
            // 会炸毁 one-line 卡。没有 96-char 夹子。
            std::string c = first_line(str("command"));
            if (!c.empty()) return c;
            std::string d = str("display_description");
            return d.empty() ? (kind == sp::Kind::Test ? "Run tests" : "Terminal") : d;
        }
        case sp::Kind::ProcessPoll:
        case sp::Kind::ProcessStop: {
            auto id = str("id");
            return (kind == sp::Kind::ProcessPoll ? "Poll process " : "Stop process ") + id;
        }
        case sp::Kind::GitCommit: {
            std::string m = str("message");
            return m.empty() ? "git commit" : "git commit: " + clip(m, 60);
        }
        case sp::Kind::Grep:
            return grep_command(args);
        case sp::Kind::Glob: {
            std::string p = str("pattern");
            std::string root = display_path(str("path"), cwd);
            std::string label = "Find";
            if (!root.empty()) label += " `" + root + "`";
            if (!p.empty())    label += " `" + p + "`";
            return label;
        }
        case sp::Kind::FindDefinition: {
            std::string s = str("symbol");
            const std::string label = "Find definition";
            return s.empty() ? label : label + " `" + s + "`";
        }
        case sp::Kind::SearchDocs:
        case sp::Kind::SearchCode: {
            std::string q = str("query");
            std::string label = (kind == sp::Kind::SearchDocs) ? "Search docs" : "Search code";
            return q.empty() ? label : label + ": \"" + clip(q, 60) + "\"";
        }
        case sp::Kind::RepoMap: {
            std::string f = str("focus");
            return f.empty() ? "Repo map" : "Repo map: " + clip(f, 50);
        }
        case sp::Kind::ListDir: {
            std::string p = display_path(str("path"), cwd);
            return p.empty() ? "List directory" : "List `" + p + "`";
        }
        case sp::Kind::GitStatus: return "git status";
        case sp::Kind::GitDiff:   return "git diff";
        case sp::Kind::GitLog:    return "git log";
        case sp::Kind::GitShow:   return "git show";
        case sp::Kind::GitBlame:  return "git blame";
        case sp::Kind::GitBranch: {
            std::string action = str("action");
            if (action.empty()) action = "list";
            std::string name = str("name");
            std::string t = "git branch " + action;
            return name.empty() ? t : t + " " + name;
        }
        case sp::Kind::GitStash: {
            std::string action = str("action");
            if (action.empty()) action = "list";
            return "git stash " + action;
        }
        case sp::Kind::GitRebase: {
            std::string action = str("action");
            std::string up = str("upstream");
            if (action == "onto" && !up.empty()) return "git rebase onto " + up;
            return action.empty() ? "git rebase" : "git rebase " + action;
        }
        case sp::Kind::GitCherryPick: {
            std::string action = str("action");
            if (action.empty()) action = "pick";
            return "git cherry-pick " + action;
        }
        case sp::Kind::WebFetch: {
            std::string u = str("url");
            return u.empty() ? "Fetch" : "Fetch " + u;
        }
        case sp::Kind::WebSearch: {
            std::string q = str("query");
            return q.empty() ? "Web search" : "\"" + clip(q, 60) + "\"";
        }
        case sp::Kind::Todo: {
            if (args.contains("todos") && args["todos"].is_array()) {
                std::string joined;
                for (const auto& t : args["todos"]) {
                    if (t.is_object() && t.contains("content") && t["content"].is_string()) {
                        if (!joined.empty()) joined += ", ";
                        joined += t["content"].get<std::string>();
                    }
                }
                if (!joined.empty()) return "Update TODOs: " + clip(joined, 80);
            }
            return "Update TODOs";
        }
        case sp::Kind::Task: {
            std::string d = str("display_description");
            if (d.empty()) d = clip(str("prompt"), 60);
            return d.empty() ? "Task" : d;
        }
        case sp::Kind::Skill: {
            std::string s = str("name");
            return s.empty() ? "Skill" : "Skill: " + s;
        }
        case sp::Kind::Remember: return "Remember";
        case sp::Kind::Forget:   return "Forget";
        case sp::Kind::Wipe:     return "Wipe memory";
    }
    return n;   // 不可达； switch 比 Kind 更详尽
}

// 工具调用触及的文件位置，用于Zed的follow-along。
// 当 调用 命名一个（读取偏移量、grep/find 行提示）时，
// 会发出带有行锚点的主路径，以便 Zed 可以将编辑器跳转到正确的位置。
a::List<a::ToolCallLocation> tool_locations(const ToolUse& tc, std::string_view cwd = {}) {
    a::List<a::ToolCallLocation> locs;
    const auto& args = tc.args;
    auto num = [&](const char* k) -> std::optional<std::int64_t> {
        if (args.contains(k) && args[k].is_number_integer())
            return args[k].get<std::int64_t>();
        return std::nullopt;
    };
    // claude-code-acp在位置中发出RAW（绝对）路径——仅卡*
    // 标题*是display-pathed。 Zed 使用位置在编辑器中打开文件，
    // 这需要真正的 on-disk 路径，而不是项目-relative
    // 路径。
    //
    // 并且它必须是ABSOLUTE，而不仅仅是原始的。 Zed 的
    // resolve_location (acp_thread.rs) 尝试
    // project_path_for_absolute_path，
    // 然后仅 `if is_absolute(path)` 回退到 open_local_buffer — 相对
    // 路径都不匹配并返回 None，因此该位置会默默地被删除，并且 follow-along 停止移动。
    // 没有任何错误；该功能会悄悄停止工作。
    //
    // 工具arg是型号带类型的的任何东西，对于`read`来说，
    // 其通常是"src/main.cpp"。因此，请坚决反对此处的会话 cwd，
    // 而不是信任它。
    auto absolutise = [&](std::string p) -> std::string {
        if (p.empty() || cwd.empty()) return p;
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::path fp{p};
        if (fp.is_absolute()) return p;
        auto joined = fs::path{cwd} / fp;
        // lexically_normal，不规范：文件可能尚
        // 不存在（新路径的 `write` 仍然需要一个位置），
        // 并且规范将为其抛出或返回空。
        return joined.lexically_normal().string();
    };
    auto add = [&](std::initializer_list<const char*> keys, bool default_line_one = false) {
        for (const char* key : keys) {
            if (args.contains(key) && args[key].is_string()) {
                std::string p = args[key].get<std::string>();
                if (p.empty()) continue;
                a::ToolCallLocation loc;
                loc.path = absolutise(std::move(p));
                auto ln = num("line"); if (!ln) ln = num("offset"); if (!ln) ln = num("start_line");
                // claude-code-acp 的 Read 发出 `line: input.offset ?? 1`，
                // 因此没有偏移的 Read 仍然将编辑器锚定在第 1 行。
                if (!ln && default_line_one) ln = 1;
                if (ln) loc.line = a::Just<std::int64_t>(*ln);
                locs.push_back(std::move(loc));
                return;
            }
        }
    };
    namespace sp = tools::spec;
    const auto* spec = sp::lookup(tc.name.value);
    if (!spec) return locs;
    switch (spec->kind) {
        case sp::Kind::Read:
            add({"path", "file_path", "filepath", "filename"}, /*default_line_one=*/true);
            break;
        case sp::Kind::Edit:
        case sp::Kind::Write:
        case sp::Kind::ListDir:
        case sp::Kind::Diagnostics:
        case sp::Kind::Grep:
        case sp::Kind::Glob:
        case sp::Kind::FindDefinition:
            add({"path", "file_path", "filepath", "filename"});
            break;
        default: break;
    }
    return locs;
}

// 从 acp ContentBlock[] 构建 提示词 文本。
// 一个提示词的内容块，被分割成模型所采取的内容。
//
// 图像曾经被扔到这里的地板上——访客对于 ImageContent
// 来说是空着手臂，并且该函数仅返回文本。因此，
// 将屏幕截图粘贴到 Zed 的代理面板中，默默地发送了
// 这些文字并丢弃了图片，该构建的 TUI 已经有几个月的愿景了。
// 没有任何报告，因为掉落的方块看起来与忽略图像的
// 模型一模一样。
struct PromptParts {
    std::string               text;
    std::vector<ImageContent> images;
};

[[nodiscard]] PromptParts prompt_parts_from_blocks(const a::List<a::ContentBlock>& blocks) {
    PromptParts out;
    for (const auto& b : blocks) {
        a::match(b,
            [&](const a::TextContent& t) { out.text += t.text; out.text += "\n"; },
            [&](const a::ResourceLinkContent& l) {
                out.text += "[resource: " + (l.name.empty() ? l.uri : l.name)
                          + " (" + l.uri + ")]\n";
            },
            [&](const a::ResourceContent& r) {
                a::match(r.resource,
                    [&](const a::TextResource& tr) { out.text += tr.text; out.text += "\n"; },
                    [&](const a::BlobResource&)    {});
            },
            [&](const a::ImageContent& img) {
                // ACP发送base64+mimeType；
                // 我们的 ImageContent 持有原始 字节 + media_type。
                // 在此解码一次，以便每个提供商的图像路径看到与 TUI 相同的形状。
                //
                // 未能解码的块是SKIPPED而不是
                // 中止回合：一个坏附件一定不能丢失它所
                // 到达的提示词。
                auto bytes = util::base64_decode(img.data);
                if (bytes.empty()) return;
                out.images.emplace_back(
                    img.mimeType.empty() ? std::string{"image/png"} : img.mimeType,
                    std::move(bytes));
            },
            // 音频还没有进入 提示词 的路径——我们提供的
            // 提供商都没有接受它，并且默默地转码比拒绝该功能
            // 更糟糕（我们这样做，所以客户端不应该发送它）。
            [&](const a::AudioContent&) {});
    }
    return out;
}

// agentty StopReason→acp::StopReason。
a::StopReason acp_stop_reason(StopReason r, bool cancelled, bool errored) {
    if (cancelled) return a::StopReason::Cancelled;
    if (errored)   return a::StopReason::Refusal;
    switch (r) {
        // ACP具有本机拒绝，因此拒绝在桥上保持其身份，
        // 而不是像正常的end_turn一样到达Zed。
        case StopReason::Refusal:      return a::StopReason::Refusal;
        // 截断是对客户端的截断：两者都将答案截断在-thought中，
        // 并且MaxTokens是ACP对其具有的信号。
        // CAUSE 有所不同（请求上限与模型窗口），
        // 这种区别对于用户应该做什么很重要，但 ACP
        // 没有单独的值，并且默默地调用截断的转弯
        // EndTurn 会告诉客户端答案已完成。
        case StopReason::ContextExceeded:
        case StopReason::MaxTokens:    return a::StopReason::MaxTokens;
        case StopReason::EndTurn:
        case StopReason::ToolUse:
        case StopReason::StopSequence:
        case StopReason::Unspecified:  return a::StopReason::EndTurn;
    }
    return a::StopReason::EndTurn;
}

// 来自 edit/write 调用 自己的参数的推测性 diff-content 块，
// 在宣布 调用 时显示，因此在工具运行之前 Zed 渲染 是 diff 卡（
// claude-code-acp 对 FileEdit/FileWrite
// 执行相同操作）。权威差异——根据真实的 on-disk before/after
// 计算——在完成更新中替换了它。
std::optional<a::ToolCallContent> announce_diff(const ToolUse& tc, std::string_view cwd) {
    namespace sp = tools::spec;
    const auto* spec = sp::lookup(tc.name.value);
    if (!spec) return std::nullopt;
    const auto& args = tc.args;
    auto s = [&](const char* k) -> std::string {
        return (args.contains(k) && args[k].is_string()) ? args[k].get<std::string>() : std::string{};
    };
    std::string path = s("path"); if (path.empty()) path = s("file_path");
    if (path.empty()) return std::nullopt;
    a::TCC_Diff diff;
    // 原始绝对路径：Zed将差异应用到这个确切的on-disk文件。
    // claude-code-acp在此未经修改地传递input.file_path。 （无效）cwd。
    (void)cwd;
    diff.path = path;
    if (spec->kind == sp::Kind::Write) {
        // Write 替换整个文件。将 oldText 宣布为空字符
        // 串（非空），因此 Zed 渲染 将其视为普通的差异卡（与
        // 编辑相同的小部件形状），而不是看起来与编辑不同的特殊
        // "new file" 可供性。完成更新带有权威的on
        // -disk before/after（当文件已经存在时，
        // 它会填充真实的先前文本）。
        diff.oldText = a::Just<std::string>(std::string{});
        diff.newText = s("content");
        return a::ToolCallContent{std::move(diff)};
    }
    if (spec->kind == sp::Kind::Edit) {
        std::string oldt = s("old_text"), newt = s("new_text");
        if (oldt.empty() && newt.empty()) return std::nullopt;
        diff.oldText = a::Just<std::string>(oldt);
        diff.newText = newt;
        return a::ToolCallContent{std::move(diff)};
    }
    return std::nullopt;
}

// 终端-kind工具调用的FULL命令文本，被包装为隔离代码块，
// 因此Zed 渲染将其逐字记录在(可扩展)卡正文中。卡片 *标题* 被
// 故意剪裁为一小行（tool_title → 剪辑（...96）），
// 并且 Zed 进一步使用单个 -line-clamps 长标题 - 因此，
// 如果没有这个，完整的 `command` 只能存在于 rawInput 中，
// 而 Zed 不会在卡片中出现。将其作为内容发出意味着未截断的命令始终可见。
// 仅适用于命令-carrying种类；否则无效。
std::optional<a::ToolCallContent> command_content(const ToolUse& tc) {
    namespace sp = tools::spec;
    const auto* spec = sp::lookup(tc.name.value);
    const auto& args = tc.args;

    // 未知/MCP工具：claude-code-acp的`Other`案例pretty-prints原始
    // 把输入放入 ```json 块，让每张工具卡片都有可读正文，避免
    // 一个空的。执行相同的操作，以便 Zed 上出现的 MCP
    // 工具显示其参数，而不是裸露的标题。
    if (!spec) {
        if (args.is_null()) return std::nullopt;
        std::string dump = args.dump(2);
        if (dump == "null" || dump.empty()) return std::nullopt;
        std::string md = "```json\n" + dump + "\n```";
        return a::ToolCallContent{a::TCC_Content{text_block(std::move(md)), json::object()}};
    }

    switch (spec->kind) {
        case sp::Kind::Bash:
        case sp::Kind::Diagnostics:
        case sp::Kind::Test:
        case sp::Kind::ProcessStart: {
            if (!args.contains("command") || !args["command"].is_string())
                return std::nullopt;
            std::string cmd = args["command"].get<std::string>();
            if (cmd.empty()) return std::nullopt;
            std::string md = "```sh\n" + cmd + "\n```";
            return a::ToolCallContent{a::TCC_Content{text_block(std::move(md)), json::object()}};
        }
        case sp::Kind::WebFetch: {
            // claude-code-acp将WebFetch的`prompt`
            // 附加为正文内容（url已经在标题中）。对其进行镜像，以便获取意图
            // 显示在卡中。
            if (!args.contains("prompt") || !args["prompt"].is_string())
                return std::nullopt;
            std::string body = args["prompt"].get<std::string>();
            if (body.empty()) return std::nullopt;
            return a::ToolCallContent{a::TCC_Content{text_block(std::move(body)), json::object()}};
        }
        default:
            return std::nullopt;
    }
}

// 任务工具的普通-text内容块，其定义输入是sub-agent
// 提示词（散文，不是命令或差异）。 claude-code-acp
// 将 sub-agent 提示词 作为内容块附加，因此即使标题被剪裁，
// 全文也可以在卡片 正文 中看到。 （WebFetch 的输入
// 是标题中已显示的裸 `url`，因此不需要额外的块。）
std::optional<a::ToolCallContent> prompt_content(const ToolUse& tc) {
    namespace sp = tools::spec;
    const auto* spec = sp::lookup(tc.name.value);
    if (!spec || spec->kind != sp::Kind::Task) return std::nullopt;
    const auto& args = tc.args;
    if (!args.contains("prompt") || !args["prompt"].is_string()) return std::nullopt;
    std::string body = args["prompt"].get<std::string>();
    if (body.empty()) return std::nullopt;
    return a::ToolCallContent{a::TCC_Content{text_block(std::move(body)), json::object()}};
}

// 转义Markdown元字符Zed的卡渲染器否则会解释，因此原始file/grep文本带有`*`、
// 反引号、`#`、`_`、`[`、`<` 渲染字面意思而不是变成bold/headers/links/HTML.
// Mirrors claude-code-acp的markdownEscape
// ()应用到 tool-result 文本。
std::string markdown_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + s.size() / 8);
    for (char c : s) {
        switch (c) {
            case '\\': case '`': case '*': case '_': case '{': case '}':
            case '[':  case ']': case '<': case '>': case '#': case '+':
            case '-':  case '.': case '!': case '|':
                out.push_back('\\');
                [[fallthrough]];
            default:
                out.push_back(c);
        }
    }
    return out;
}

// 渲染将完成的工具的结果文本插入到ACP内容块中，Zed在卡片正文中显示，
// 匹配claude-code-acp的per-tool结果整形：
//
//   ·读取→重建从以下位置开始的行-numbered视图(`<n>\t<line>`)
//     读取的偏移量，因此 Zed 渲染 是干净的 gutter-numbered 摘录，
//     而不是原始斑点。 正文 被隔离为纯代码块，因此文件 渲染 中的 tab
//     -numbered 行和任何 Markdown 元字符都是逐字记录的。
//   • 其他所有→文本为-is，但markdown-escaped所以偏离`*`/`#`/
//     输出中的反引号不会被解析为格式。
//
// 返回准备好推送完成更新的TCC_Content。 `text`是工具的
// 型号-facing输出； `tc` 提供工具标识 + args（偏移量）。
a::ToolCallContent result_content_block(const ToolUse& tc, const std::string& text) {
    namespace sp = tools::spec;
    const auto* spec = sp::lookup(tc.name.value);
    const sp::Kind kind = spec ? spec->kind : sp::Kind::Read;

    if (kind == sp::Kind::Read && !text.empty()) {
        // 起始行=读取的偏移量(1-based)，匹配claude-code-acp(
        // `structuredRead.file.startLine ?? input.offset ?? 1`)。
        // agentty 的读取工具会在前面添加一个可选的 `display_description` 行，
        // 并在后面附加一个 `[showing lines X-Y of N]` 提示；
        // 两者都是 model-facing 脚手架，而不是文件 字节，
        // 因此在编号之前去掉前导描述行和尾随括号提示，以便装订线与
        // 实际文件行保持对齐。
        std::int64_t start = 1;
        const auto& args = tc.args;
        for (const char* k : {"offset", "start_line"}) {
            if (args.contains(k) && args[k].is_number_integer()) {
                start = args[k].get<std::int64_t>();
                break;
            }
        }
        if (start < 1) start = 1;

        std::string_view body{text};
        // 删除尾随的"\n[showing lines ...]"提示（如果存在）。
        if (auto hint = body.rfind("\n[showing lines ");
            hint != std::string_view::npos)
            body = body.substr(0, hint);

        std::string numbered;
        numbered.reserve(body.size() + body.size() / 8);
        // 现在读取自己的行号（"%6d\t");这里只对旧的、
        // 未编号的结果进行编号。
        auto already_numbered = [](std::string_view s) {
            std::size_t k = 0;
            while (k < s.size() && s[k] == ' ') ++k;
            const std::size_t d = k;
            while (k < s.size() && s[k] >= '0' && s[k] <= '9') ++k;
            return k > d && k < s.size() && s[k] == '\t';
        };
        if (already_numbered(body)) {
            std::string md = "```\n" + std::string{body} + "\n```";
            return a::ToolCallContent{a::TCC_Content{text_block(std::move(md)), json::object()}};
        }
        std::int64_t ln = start;
        std::size_t pos = 0;
        while (pos <= body.size()) {
            std::size_t nl = body.find('\n', pos);
            std::string_view line = (nl == std::string_view::npos)
                ? body.substr(pos)
                : body.substr(pos, nl - pos);
            numbered += std::to_string(ln);
            numbered += '\t';
            numbered.append(line);
            numbered += '\n';
            ++ln;
            if (nl == std::string_view::npos) break;
            pos = nl + 1;
        }
        // 逐字栅栏选项卡布局和任何降价字符渲染。
        std::string md = "```\n" + numbered + "```";
        return a::ToolCallContent{a::TCC_Content{text_block(std::move(md)), json::object()}};
    }

    // 命令输出（bash/test/diagnostics/process/git)：
    // fence作为代码块，因此终端将渲染输出为等宽日志——正是Zed
    // 用户对shell卡的期望——而不是markdown-escaped散文。
    // 这是未采用 live-terminal 路径时的静态 回退。
    if ((kind == sp::Kind::Bash || kind == sp::Kind::Test
         || kind == sp::Kind::Diagnostics || kind == sp::Kind::ProcessStart
         || kind == sp::Kind::ProcessPoll || kind == sp::Kind::ProcessStop
         || kind == sp::Kind::GitCommit)
        && !text.empty()) {
        std::string body{text};
        std::string md = body.find("```") == std::string::npos
                       ? "```\n" + body + "\n```"
                       : std::move(body);
        return a::ToolCallContent{a::TCC_Content{text_block(std::move(md)), json::object()}};
    }

    // Non-Read结果：转义markdown，因此原始输出字面上的渲染。
    return a::ToolCallContent{
        a::TCC_Content{text_block(markdown_escape(text)), json::object()}};
}

// 从 `todo` 工具的参数构建的 ACP 计划（Zed 的清单小部件）。
// 本机计划UI代替不透明的"Update TODOs"工具卡。
// 当 调用 不是待办事项写入或不携带可用列表时，返回 nullopt。
std::optional<a::SU_Plan> plan_from_todos(const ToolUse& tc) {
    namespace sp = tools::spec;
    const auto* spec = sp::lookup(tc.name.value);
    if (!spec || spec->kind != sp::Kind::Todo) return std::nullopt;
    const auto& args = tc.args;
    if (!args.contains("todos") || !args["todos"].is_array()) return std::nullopt;

    auto status_of = [](std::string_view s) {
        if (s == "in_progress") return a::PlanEntryStatus::InProgress;
        if (s == "completed")   return a::PlanEntryStatus::Completed;
        return a::PlanEntryStatus::Pending;
    };

    a::SU_Plan plan;
    for (const auto& t : args["todos"]) {
        if (!t.is_object()) continue;
        std::string content = t.value("content", std::string{});
        if (content.empty()) continue;
        a::PlanEntry e;
        e.content  = std::move(content);
        e.priority = a::PlanEntryPriority::Medium;   // agentty待办事项没有优先级
        e.status   = status_of(t.value("status", std::string{"pending"}));
        plan.entries.push_back(std::move(e));
    }
    if (plan.entries.empty()) return std::nullopt;
    return plan;
}

// 助手：来自 待处理 ToolUse 的 ToolCall (通知）。
a::ToolCall make_tool_call(const ToolUse& tc, a::ToolCallStatus status,
                           std::string_view cwd = {}) {
    a::ToolCall out;
    out.toolCallId = a::ToolCallId{tc.id.value};
    out.title      = tool_title(tc, cwd);
    // PROGRAMMATIC 名字，旁边是人类头衔。
    // 在规范 1.8.0 中稳定且可选，但想要按工具标识进行分组、
    // 过滤或图标的客户端无法将 解析 退出 "Reading src/main.cpp"
    // — 因此省略它只会使这变得不可能。
    out.name       = a::Just<std::string>(tc.name.value);
    out.kind       = acp_tool_kind(tc.name.value);
    out.status     = status;
    if (!tc.args.is_null()) out.rawInput = a::Just<json>(tc.args);
    out.locations  = tool_locations(tc, cwd);
    a::List<a::ToolCallContent> content;
    if (auto d = announce_diff(tc, cwd)) content.push_back(std::move(*d));
    if (auto c = command_content(tc))    content.push_back(std::move(*c));
    if (auto pc = prompt_content(tc))    content.push_back(std::move(*pc));
    if (!content.empty()) out.content = std::move(content);
    return out;
}

// ── 贝壳与Zed的本地代理对等 ────────────────────────────────────
// 斜杠-command菜单Zed 渲染在其编辑器中。 claude-code-acp
// 在这里公开了自己的集合； agentty 公开了映射到真实回合动作的技能以及每个
// 已安装的型号 -invocable 技能，如 `/<name>`。
// 在会话 new/load 上发送，因此 Zed 的 `/` 菜单的填充方式与本机代理相同。
a::List<a::AvailableCommand> available_commands(Io io, bool air = false) {
    a::List<a::AvailableCommand> cmds;
    auto add = [&](std::string name, std::string desc, const char* hint) {
        a::AvailableCommand c;
        c.name        = std::move(name);
        c.description = std::move(desc);
        if (hint)
            c.input = a::Just<a::AvailableCommandInput>(
                a::AvailableCommandInput{a::UnstructuredCommandInput{hint}});
        cmds.push_back(std::move(c));
    };
    add("compact", "Summarize the conversation to reclaim context window", nullptr);
    add("new",     "Start a fresh conversation, clearing prior context", nullptr);
    // Zed 将这些分组并作为代理操作运行，而不是它回显的 提示词。
    for (auto& c : cmds) c.meta = json{{"command_category", "native"}};
    std::set<std::string> seen{"compact", "new"};
    // 用户自己的斜杠命令(.agentty/.claude/commands/*.md)，
    // 及其参数提示，与TUI的`/`、选择面板显示的相同。
    for (const auto& c : tools::commands::all(io)) {
        if (c.name.empty() || !seen.insert(c.name).second) continue;
        add(c.name, c.description.empty() ? "Run /" + c.name : c.description,
            c.argument_hint.empty() ? nullptr : c.argument_hint.c_str());
    }
    // 每个已安装的技能。 user_only 正是需要手动运行的，
    // 所以它们也被列出来；模型只是从来没有看到它们。
    for (const auto& sk : tools::skills::all(io)) {
        if (sk.name.empty() || !seen.insert(sk.name).second) continue;
        add(sk.name,
            sk.description.empty() ? std::string{"Run the "} + sk.name + " skill"
                                   : sk.description,
            "optional arguments");
        // AIR将技能显示为技能，其后面带有文件。
        if (air)
            cmds.back().meta = json{{"jetbrains", {{"air", {{"version", 1}, {"kind", "skill"},
                {"skillPath", (sk.dir / "SKILL.md").string()}}}}}};
    }
#if AGENTTY_MCP
    // MCP服务器提示词。 Zed 对标记为 "mcp" 的命令进行分组。
    try {
        for (const auto& pr : ::agentty::mcp::mcp_prompts()) {
            if (pr.name.empty() || !seen.insert(pr.name).second) continue;
            std::string hint;
            for (const auto& arg : pr.arguments) {
                if (!hint.empty()) hint += ' ';
                hint += arg.required ? arg.name : "[" + arg.name + "]";
            }
            const std::string desc = !pr.description.empty() ? pr.description
                                   : !pr.title.empty()       ? pr.title
                                                             : "Prompt from " + pr.server;
            add(pr.name, desc, hint.empty() ? nullptr : hint.c_str());
            cmds.back().meta = json{{"command_category", "mcp"}};
        }
    } catch (...) {}
#endif
    return cmds;
}

// `model`配置选项Zed 渲染作为下拉列表，填充有提供商的
// 型号目录。 `current` 是会话的活动模型 ID。
// 当 `auth` 没有 凭据 时，我们仍然发出静态种子列表，
// 因此在 Sign-in 之前下拉列表不为空。镜像本机代理如何宣传其
// 型号 选择面板。关闭 高频路径（仅在会话 new/load 上构建）。
// 模型选择的传输值："<provider>\t<model>"，
// 与TUI的最近-models列表使用相同的组合键，因此服务相同模型id的两
// 个提供者保持不同。
std::string choice_value(std::string_view provider, std::string_view model) {
    return std::string{provider} + '\t' + std::string{model};
}
std::pair<std::string, std::string> split_choice(std::string_view v) {
    const auto tab = v.find('\t');
    if (tab == std::string_view::npos) return {"", std::string{v}};   // 裸机型号id
    return {std::string{v.substr(0, tab)}, std::string{v.substr(tab + 1)}};
}

// 模型接受的工作量等级，从低到高，如 传输 字符串。
std::vector<Effort> effort_ladder(const std::string& model, const std::string& provider) {
    const auto caps = resolved_caps(model, provider);
    const std::uint8_t set = effort_set_of(caps);
    std::vector<Effort> out;
    for (Effort e : {Effort::Minimal, Effort::Low, Effort::Medium, Effort::High,
                     Effort::Xhigh, Effort::Max})
        if (set & effort_bit(e)) out.push_back(e);
    return out;
}

// ""（默认）| "none" | 传输级别，固定到模型所采用的水平。
// 返回要在下拉列表中显示的值。
std::string effective_effort(const std::string& want, const std::vector<Effort>& ladder) {
    if (ladder.empty()) return "none";
    if (want == "none") return "none";
    if (want.empty() || want == "auto") return "auto";
    for (Effort e : ladder)
        if (effort_wire(e) == want) return want;
    return "auto";
}

// 权限配置文件（Ask/Write/Minimal)作为配置选项，类别"mode"。
// 在旧 `modes` 字段旁边的每个连接上发送：Zed 的配置文件 选择面板
// 是类别 "mode" 的第一个 `select` 选项，一旦出现 configOptions，
// Zed 就会忽略 `modes`。与 mode_state()/
// mode_id_for() 相同的三个 id。
[[nodiscard]] a::ConfigOption mode_config_option(const char* current_mode_id) {
    auto mk = [](const char* value, const char* name, const char* desc) {
        a::ConfigSelectOption o;
        o.value       = value;
        o.name        = name;
        o.description = a::Just<std::string>(desc);
        return o;
    };
    a::List<a::ConfigSelectOption> opts;
    opts.push_back(mk("ask", "Ask",
        "Prompt before edits, commands, and network access"));
    opts.push_back(mk("write", "Write",
        "Edit files and run commands without prompting; still prompt for risky ops"));
    opts.push_back(mk("minimal", "Minimal",
        "Prompt for everything, including file reads"));

    a::ConfigOption opt;
    opt.id           = "mode";
    opt.name         = "Permission Mode";
    opt.description  = a::Just<std::string>(
        "How agentty asks before acting on your workspace");
    opt.category     = a::Just<std::string>("mode");
    opt.type         = "select";
    opt.currentValue = current_mode_id ? current_mode_id : "ask";
    opt.options      = a::ConfigSelectOptions{a::CSO_Ungrouped{std::move(opts)}};
    return opt;
}

} // 命名空间结束。

AgentServer::AgentServer(rpc::Channel      channel,
                         StreamFn          stream,
                         auth::AuthHeader  auth,
                         std::string       model_id,
                         Profile           profile,
                         Extras            extras)
    : router_(make_router()),
      peer_(std::make_unique<rpc::Peer>(std::move(channel), make_dispatch(), "acp")),
      stream_(std::move(stream)),
      stream_to_(std::move(extras.stream_to)),
      model_id_(std::move(model_id)),
      profile_(profile),
      auth_(std::move(auth)),
      reauth_(std::move(extras.reauth)) {
    // 失效的客户端不能永远楔入工作线程：我们的调用（权限，
    // terminal/*）在此之后失败，并出现超时。
    // 对于阅读 权限 对话的人来说，5 分钟已经足够了。
    peer_->set_default_timeout(std::chrono::minutes(5));
}

AgentServer::~AgentServer() {
    if (client_elicit_url_.load(std::memory_order_relaxed))
        ::agentty::mcp::set_url_elicitation({});
    // peer_与成员一起被销毁：〜对端停止并加入其reader/writer
    // （对端是joins_on_destroy，因此没有锁可以拥有它，
    // 并且该析构函数永远不会在锁 正文内运行）。
}

AgentServer::Router AgentServer::make_router() {
    namespace m = a::to_agent;
    Router r;
    // 每个处理函数通过Ctx获取阅读器线程的能力。
    r.on<m::Initialize>([](Ctx& c, const a::InitializeParams& p) { return c.self.on_initialize(c.cap, p); });
    r.on<m::SessionNew>([](Ctx& c, const a::NewSessionParams& p) { return c.self.on_new_session(c.cap, p); });
    r.on<m::SessionCancel>([](Ctx& c, const a::CancelParams& p) { c.self.on_cancel(c.cap, p); });
    r.on<a::CancelRequest>([](Ctx& c, const a::CancelRequestParams& p) { c.self.on_cancel_request(c.cap, p.id); });
    // session/prompt在路由器之前的on_call中得到应答。
    r.defer<m::SessionPrompt>();
    r.on<m::SessionLoad>([](Ctx& c, const a::LoadSessionParams& p) {
        return c.self.on_load_session(c.cap, p);
    });
    r.on<m::SessionResume>([](Ctx& c, const a::ResumeSessionParams& p) { return c.self.on_resume_session(c.cap, p); });
    r.on<m::SessionFork>([](Ctx& c, const a::ForkSessionParams& p) { return c.self.on_fork_session(c.cap, p); });
    r.on<m::SessionList>([](Ctx& c, const a::ListSessionsParams& p) { return c.self.on_list_sessions(c.cap, p); });
    r.on<m::SessionClose>([](Ctx& c, const a::CloseSessionParams& p) {
        c.self.on_close_session(c.cap, p); return a::Unit{};
    });
    r.on<m::SessionDelete>([](Ctx& c, const a::DeleteSessionParams& p) {
        c.self.on_delete_session(c.cap, p); return a::Unit{};
    });
    r.on<m::SessionSetMode>([](Ctx& c, const a::SetModeParams& p) {
        c.self.on_set_mode(c.cap, p); return a::Unit{};
    });
    r.on<m::SessionSetConfig>([](Ctx& c, const a::SetConfigOptionParams& p) {
        return c.self.on_set_config_option(c.cap, p);
    });
    r.on<m::Authenticate>([](Ctx& c, const a::AuthenticateParams& p) {
        c.self.on_authenticate(p);
        return a::Unit{};
    });
    r.on<m::Logout>([](Ctx& c, const a::Unit&) { c.self.on_logout(c.cap); return a::Unit{}; });
    return r;
}

rpc::Dispatch AgentServer::make_dispatch() {
    rpc::Dispatch d;
    d.on_call         = [this](const maya::unlocked& cap, const a::Call& c) { return on_call(cap, c); };
    d.on_notification = [this](const maya::unlocked& cap, const a::Notification& n) {
        on_notification(cap, n);
    };
    // 每个帧落在结构化日志的`acp`通道上，默认情况下在非
    // -release构建中或通过AGENTTY_LOG=acp=trace捕获。
    if (logx::enabled(logx::Channel::Acp, logx::Level::Trace))
        d.trace = [](bool inbound, std::string_view line) {
            AGT_LOG(Acp, Trace, "acp.frame", "dir={} raw={}", inbound ? "in" : "out", line);
        };
    return d;
}

std::optional<std::string> AgentServer::on_call(const Cap& cap, const a::Call& c) {
    if (c.method == a::to_agent::SessionPrompt::name) {
        Responder resp(*peer_, c.id);
        try {
            on_prompt(cap, jsonrpc::from_json<a::PromptParams>(c.params), std::move(resp));
        } catch (const std::exception& e) {
            return a::reply(c.id, std::unexpected(a::RpcError(a::errc::InvalidParams, e.what())));
        }
        return std::nullopt;
    }
    Ctx ctx{*this, cap};
    return router_.handle(ctx, c);
}

void AgentServer::on_notification(const Cap& cap, const a::Notification& n) {
    Ctx ctx{*this, cap};
    router_.handle(ctx, n);
}

int AgentServer::serve(const Cap& cap) {
    peer_->start();
    peer_->wait_closed(cap);   // 标准输入上的 EOF
    return 0;
}

void AgentServer::persist(const Cap& cap, const std::string& id) {
    // 将线程复制到锁下，写在外面。
    if (auto snapshot = with_session(cap, id, [](Session& s) { return s.thread; }))
        persistence::save_thread(IoAccess::grant(), cap, *snapshot);
}

// 编辑器的MCP服务器(session/new、加载、简历、
// 分叉)，变成mcp.json条目并交给MCP桥。仅当设置更改时，
// 网桥才会重新连接，因此每个会话发送相同的列表是免费的。
void AgentServer::adopt_client_mcp(const Cap&, const a::List<a::McpServer>& servers) {
#if AGENTTY_MCP
    json out = json::object();
    for (const auto& s : servers) {
        a::match(s,
            [&](const a::StdioMcpServer& v) {
                json e{{"command", v.command}, {"args", v.args}};
                json env = json::object();
                for (const auto& kv : v.env) env[kv.name] = kv.value;
                if (!env.empty()) e["env"] = std::move(env);
                out[v.name] = std::move(e);
            },
            [&](const a::HttpMcpServer& v) {
                json h = json::object();
                for (const auto& kv : v.headers) h[kv.name] = kv.value;
                out[v.name] = json{{"type", "http"}, {"url", v.url}, {"headers", std::move(h)}};
            },
            [&](const a::SseMcpServer& v) {
                json h = json::object();
                for (const auto& kv : v.headers) h[kv.name] = kv.value;
                out[v.name] = json{{"type", "sse"}, {"url", v.url}, {"headers", std::move(h)}};
            });
    }
    // 连接可能需要几秒钟；不要为此等待 session/new。
    // 当它登陆时，重新-send每个实时会话的/菜单，以便服务器的提示词显示。
    util::run_isolated_detached("acp.mcp_servers",
        [](const maya::unlocked& cap, std::stop_token, json servers, maya::co_owned<AgentServer> self) {
            ::agentty::mcp::set_client_servers(servers);
            self->refresh_command_menus(cap);
        },
        std::move(out), maya::co_owned<AgentServer>::of(*this));
#else
    (void)servers;
#endif
}

// /菜单在每个会话下改变（MCP服务器连接并带来
// 提示词）：向每个实时会话发送新列表。
void AgentServer::refresh_command_menus(const Cap& cap) {
    const auto ids = sessions_.read(cap, [](const Sessions& ss) {
        std::vector<std::string> out;
        for (const auto& [id, s] : ss.by_id) out.push_back(id);
        return out;
    });
    if (ids.empty()) return;
    a::SU_AvailableCommands ac;
    ac.availableCommands = available_commands(IoAccess::grant(), air_.load(std::memory_order_relaxed));
    for (const auto& id : ids) send_update(cap, id, ac);
}

void AgentServer::send_update(const maya::below<maya::leaf>& cap, const std::string& session_id,
                              a::SessionUpdate update) {
    a::SessionUpdateMsg msg;
    msg.sessionId = a::SessionId{session_id};
    msg.update    = std::move(update);
    peer_->notify<a::to_client::SessionUpdate>(cap, msg);
}

void AgentServer::notice(const maya::below<maya::leaf>& cap, const std::string& session_id,
                         std::string severity,
                         std::string title, std::string description) {
    if (!client_notices_.load(std::memory_order_relaxed) || title.empty()) return;
    a::SU_Notice n;
    n.severity = std::move(severity);
    n.title    = std::move(title);
    if (!description.empty()) n.description = a::Just(std::move(description));
    send_update(cap, session_id, std::move(n));
}

bool AgentServer::terminal_channel() const noexcept {
    return client_terminal_meta_.load(std::memory_order_relaxed);
}

json AgentServer::air_meta(json payload) const {
    if (!air_.load(std::memory_order_relaxed)) return json::object();
    payload["version"] = 1;
    return json{{"jetbrains", {{"air", std::move(payload)}}}};
}

// AIR的per-tool提示：命令自身描述为卡标题，
// 任务工具标记为子代理，技能加载命名。其他工具为空。
json AgentServer::air_tool_meta(const ToolUse& tc) const {
    if (!air_.load(std::memory_order_relaxed) || !tc.args.is_object()) return json::object();
    json p = json::object();
    const std::string& n = tc.name.value;
    if (is_command_tool(n)) {
        for (const char* k : {"display_description", "description"})
            if (auto it = tc.args.find(k); it != tc.args.end() && it->is_string()
                && !it->get<std::string>().empty()) {
                p["commandTitle"] = *it;
                break;
            }
    } else if (n == "task") {
        p["subagent"] = true;
    } else if (n == "skill") {
        if (auto it = tc.args.find("name"); it != tc.args.end() && it->is_string())
            p["skill"] = json{{"name", *it}};
    }
    return p.empty() ? json::object() : air_meta(std::move(p));
}

// 将`extra`合并到`meta`中，保留其他命名空间。
static void merge_meta(json& meta, const json& extra) {
    if (!extra.is_object() || extra.empty()) return;
    if (!meta.is_object()) meta = json::object();
    meta.merge_patch(extra);
}

// 将MCP服务器的elicitation/create(表单模式)中继到编辑器。
// 两者共享受限制的JSON 模式定义，因此表格通过； ACP 添加
// 会话和工具 调用 范围。任何我们无法转发的内容都会被拒绝。
json AgentServer::elicit_via_client(const Cap& cap, const std::string& session_id,
                                    const std::string& tool_id, const json& mcp,
                                    std::stop_token cancel) {
    const json decline{{"action", "decline"}};
    if (!mcp.is_object()) return decline;
    const std::string mode = mcp.value("mode", std::string{"form"});
    json req;
    if (mode == "url" && client_elicit_url_.load(std::memory_order_relaxed)) {
        // 编辑器显示URL并且仅在用户同意的情况下才将其打开。
        if (!mcp.contains("url") || !mcp["url"].is_string()
            || !mcp.contains("elicitationId") || !mcp["elicitationId"].is_string())
            return decline;
        req = json{{"mode", "url"},
                   {"message", mcp.value("message", std::string{"The tool needs you to open a link."})},
                   {"url", mcp["url"]},
                   {"elicitationId", mcp["elicitationId"]},
                   {"sessionId", session_id},
                   {"toolCallId", tool_id}};
    } else if (mode == "form" && client_elicit_form_.load(std::memory_order_relaxed)
               && mcp.contains("requestedSchema") && mcp["requestedSchema"].is_object()) {
        req = json{{"mode", "form"},
                   {"message", mcp.value("message", std::string{"The tool needs some input."})},
                   {"requestedSchema", mcp["requestedSchema"]},
                   {"sessionId", session_id},
                   {"toolCallId", tool_id}};
    } else {
        return decline;
    }
    // 已取消 回合撤回表格 ($/cancel_request) 并拒绝。
    rpc::CallOptions o;
    o.cancel      = cancel;
    o.notify_peer = true;
    auto got = peer_->call<a::to_client::ElicitationCreate>(cap, req, std::move(o));
    if (!got || !got->is_object() || !got->contains("action")) return decline;
    json out{{"action", (*got)["action"]}};
    if ((*got)["action"] == "accept" && got->contains("content")) out["content"] = (*got)["content"];
    return out;
}

bool AgentServer::is_command_tool(std::string_view name) {
    const auto* spec = tools::spec::lookup(name);
    return spec && spec->kind == tools::spec::Kind::Bash;
}

void AgentServer::send_terminal_output(const maya::below<maya::leaf>& cap,
                                       const std::string& session_id,
                                       const std::string& tool_id, std::string_view data) {
    a::ToolCallUpdate u;
    u.toolCallId = a::ToolCallId{tool_id};
    // Zed替换为terminal_output块；
    // 要求附加 terminal_output_delta 的客户。无论哪种方式都相同 载荷。
    const char* key = client_terminal_delta_.load(std::memory_order_relaxed)
                    ? "terminal_output_delta" : "terminal_output";
    // 进度文本已经有效 UTF-8（exec 对其进行转换）。
    u.meta = json{{key, {{"terminal_id", tool_id}, {"data", std::string{data}}}}};
    send_update(cap, session_id, a::SU_ToolCallUpdate{std::move(u)});
}

// _meta.terminal_exit 表示已完成的命令。 shell 工具的结果显示
// "failed with exit code N" 表示失败，而没有显示成功。
json AgentServer::terminal_exit_meta(const std::string& tool_id, std::string_view text,
                                     bool failed) {
    json code = failed ? json(1) : json(0);
    constexpr std::string_view kTag = "failed with exit code ";
    if (const auto at = text.find(kTag); at != std::string_view::npos) {
        int n = 0;
        const char* p = text.data() + at + kTag.size();
        const char* end = text.data() + text.size();
        bool any = false;
        while (p < end && *p >= '0' && *p <= '9') { n = ::agentty::util::push_digit(n, *p); ++p; any = true; }
        if (any) code = n;
    } else if (text.find("timed out after") != std::string_view::npos) {
        code = nullptr;   // 被杀，无退出状态
    }
    return json{{"terminal_exit", {{"terminal_id", tool_id}, {"exit_code", code},
                                    {"signal", nullptr}}}};
}

void AgentServer::emit_session_config(const Cap& cap, const std::string& session_id,
                                      const std::string& model_id) {
    // Slash-command菜单(Zed的`/`作曲家菜单)。
    {
        a::SU_AvailableCommands ac;
        ac.availableCommands = available_commands(IoAccess::grant(),   // ACP 处理函数:不是状态更新函数
                                                  air_.load(std::memory_order_relaxed));
        if (!ac.availableCommands.empty())
            send_update(cap, session_id, std::move(ac));
    }
    // 会话配置选项，作为 一个 完成 -state 通知 发出（v2 合约：
    // SU_ConfigOptions 携带 完整 集，而不是增量）。
    // 我们宣传 权限 MODE，当 目录 可用时，我们宣传 MODEL。
    // config-option-aware 客户端 渲染 作为本机选择器（Zed
    // 的 per-session 下拉列表）；它可以通过
    // session/set_config_option 实时翻转它们，而无需重新启动。
    // 权限 模式是 ALSO，由 v1 客户端的会话结果上的旧 `modes` 字段
    // 携带 - 双-surface，两种方式都没有间隙。
    emit_config_state(cap, session_id, model_id);
}

// 当前，会话的COMPLETE配置-option状态为单个SU_ConfigOptions 通知。
// 初始广告 (emit_session_config) 和每个 post
// -change 回声 (on_set_config_option)
// 的单一事实来源 - v2 规范要求每个 SU_ConfigOptions
// 携带全套，以便相关选项保持一致。读取实时会话状态（配置文件 + 模型），
// 因此回显的 `currentValue` 始终与现实相符。
void AgentServer::emit_config_state(const Cap& cap, const std::string& session_id,
                                    const std::string& fallback_model) {
    a::SU_ConfigOptions co;
    co.configOptions = config_options(cap, session_id, fallback_model);
    if (co.configOptions.empty()) return;   // 没什么可做广告的
    send_update(cap, session_id, std::move(co));
}

// 会话的完整配置状态。在每个 session/new、加载、
// 简历和 set_config_option 结果中返回（仅读取结果的客户端，
// 如 JetBrains，需要它）并回显为 config_option_update。
a::List<a::ConfigOption> AgentServer::config_options(const Cap& cap,
                                                     const std::string& session_id,
                                                     const std::string& fallback_model) {
    struct Cfg { Profile prof; std::string provider, model, effort; bool reasoning; };
    Cfg c{profile_, "", fallback_model, "", true};
    if (auto got = with_session(cap, session_id, [](Session& s) {
            return Cfg{s.profile, s.provider, s.model, s.effort, s.show_reasoning};
        })) {
        c = *got;
        if (c.model.empty()) c.model = fallback_model;
    }
    a::List<a::ConfigOption> out;
    // 权限模式始终是配置选项。获得 configOptions 的客户端
    // 会单独从它们构建选择器（一旦 configOptions 存在，
    // Zed 就会删除 `modes`，在 v1 上也是如此），因此将其排除
    // 意味着根本没有配置文件 选择面板。
    {
        auto opt = mode_config_option(mode_id_for(c.prof));
        if (auto* u = std::get_if<a::CSO_Ungrouped>(&opt.options))
            for (auto& o : u->items)
                o.meta = air_meta(json{{"kind", o.value == "write" ? "auto_review" : "standard"}});
        out.push_back(std::move(opt));
    }
    for (auto& o : model_options(cap, c.provider, c.model, c.effort, c.reasoning))
        out.push_back(std::move(o));
    return out;
}

// 用户登录的每个提供者上的每个模型，按提供者分组，
// 与TUI的融合选择面板 (ctrl+/)显示的集合相同。
// 如果没有 stream_to_，服务器只能到达活动提供商，
// 因此仅提供其型号。
std::vector<AgentServer::ModelChoice> AgentServer::model_choices(const Cap& cap) {
    if (auto have = model_choices_.current(); have && !have->empty())
        return *have;

    const Io io = IoAccess::grant();
    std::vector<ModelChoice> out;
    const auto active = provider::active();
    const std::string active_id = active.catalog_id();
    auto add_provider = [&](const std::string& pid, const std::string& label,
                            const provider::Selection& sel, const auth::AuthHeader& auth) {
        std::vector<ModelInfo> models;
        try { models = provider::list_models_for(io, cap, sel, auth); } catch (...) {}
        for (const auto& m : models)
            out.push_back({choice_value(pid, m.id.value), pid, m.id.value,
                           m.display_name.empty() ? m.id.value : m.display_name,
                           pid, label});
    };

    // 主动提供者优先：其模型是大多数用户选择的。
    add_provider(active_id, provider::provider_display_name(active), active, auth());
    if (stream_to_) {
        try {
            const auto settings = persistence::load_settings(io);
            const auto view = provider::load_auth_view(io, settings);
            for (const auto& src : provider::catalog_sources(settings, view)) {
                if (src.needs_signin || src.id == active_id) continue;
                const auto sel = provider::parse_selection(src.id);
                if (sel.kind == provider::Kind::ExternalAcp) continue;
                add_provider(src.id, src.label, sel,
                             provider::credentials::resolve(io, src.id));
            }
        } catch (...) { /* 活跃提供商的模型仍然有效 */ }
    }
    model_choices_.publish(std::make_shared<const std::vector<ModelChoice>>(out));
    return out;
}

// Model，然后是依赖于它的旋钮：推理努力（仅
// 适用于有推理努力的模型）以及是否显示其思考。
// 每次更改都要重建，因为工作量阶梯取决于模型。
a::List<a::ConfigOption> AgentServer::model_options(const Cap& cap,
                                                    const std::string& provider,
                                                    const std::string& model,
                                                    const std::string& effort,
                                                    bool show_reasoning) {
    a::List<a::ConfigOption> out;
    const auto choices = model_choices(cap);
    const std::string active_id = provider::active().catalog_id();
    const std::string pid = provider.empty() ? active_id : provider;

    if (!choices.empty()) {
        const std::string current = choice_value(pid, model);
        bool present = false;
        // 当存在多个提供者时按提供者分组，因此下拉菜单以 TUI 选择面板
        // 的方式读取 "Anthropic › Claude Opus"。
        std::vector<std::string> order;
        std::map<std::string, a::ConfigSelectGroup> groups;
        for (const auto& c : choices) {
            if (c.value == current) present = true;
            auto [it, fresh] = groups.try_emplace(c.group);
            if (fresh) {
                order.push_back(c.group);
                it->second.group = c.group;
                it->second.name  = c.group_name;
            }
            it->second.options.push_back(a::ConfigSelectOption{c.value, c.name, a::Nothing, json::object()});
        }
        a::ConfigOption opt;
        opt.id       = "model";
        opt.name     = "Model";
        opt.category = a::Just<std::string>("model");
        opt.type     = "select";
        if (!present) {
            // 不在任何目录（-m、细-tune）中的模型：将其列出，
            // 以便下拉列表显示实际运行的内容。
            a::ConfigSelectGroup& g = groups[pid];
            if (g.group.empty()) { g.group = pid; g.name = pid; order.push_back(pid); }
            g.options.insert(g.options.begin(),
                             a::ConfigSelectOption{current, model, a::Nothing, json::object()});
        }
        opt.currentValue = current;
        if (order.size() == 1) {
            opt.options = a::ConfigSelectOptions{a::CSO_Ungrouped{std::move(groups[order[0]].options)}};
        } else {
            a::List<a::ConfigSelectGroup> gs;
            for (const auto& g : order) gs.push_back(std::move(groups[g]));
            opt.options = a::ConfigSelectOptions{a::CSO_Grouped{std::move(gs)}};
        }
        out.push_back(std::move(opt));
    }

    // 推理功夫，模型自带梯子加自动和关。
    if (const auto ladder = effort_ladder(model, pid); !ladder.empty()) {
        a::List<a::ConfigSelectOption> opts;
        opts.push_back({"auto", "Auto", a::Just<std::string>("agentty picks per turn"), json::object()});
        opts.push_back({"none", "Off", a::Nothing, json::object()});
        for (Effort e : ladder) {
            std::string w{effort_wire(e)};
            std::string label = w;
            if (!label.empty()) label[0] = static_cast<char>(std::toupper(label[0]));
            opts.push_back({w, label, a::Nothing, json::object()});
        }
        a::ConfigOption opt;
        opt.id           = "effort";
        opt.name         = "Thinking";
        opt.description  = a::Just<std::string>("How hard the model reasons before answering");
        opt.category     = a::Just<std::string>("thought_level");
        opt.type         = "select";
        opt.currentValue = effective_effort(effort, ladder);
        opt.options      = a::ConfigSelectOptions{a::CSO_Ungrouped{std::move(opts)}};
        out.push_back(std::move(opt));

        // 显示思维：一个布尔值，因此只适用于渲染的客户。
        if (client_bool_config_.load(std::memory_order_relaxed)) {
            a::ConfigOption b;
            b.id          = "show_thinking";
            b.name        = "Show thinking";
            b.description = a::Just<std::string>("Stream the model's reasoning into the chat");
            b.category    = a::Just<std::string>("model_config");
            b.type        = "boolean";
            b.currentBool = show_reasoning;
            out.push_back(std::move(b));
        }
    }
    return out;
}

void AgentServer::replay_history(const Cap& cap, const std::string& session_id,
                                 const Thread& thread) {
    // Project-relative在重放的工具卡中显示路径，
    // 匹配实时回合。会话 cwd 已在重播运行时设置。
    const std::string scwd =
        with_session(cap, session_id, [](Session& s) { return s.cwd; }).value_or(std::string{});

    // 将整个重放作为一次写入来发送：重放在读取器线程上发出许多session/update
    // 通知回-to-back。
    peer_->batch(cap, [&] {
        replay_history_body(cap, session_id, thread, scwd);
    });
}

void AgentServer::replay_history_body(const Cap& cap, const std::string& session_id,
                                      const Thread& thread, const std::string& scwd) {

    // 重放是读取器线程上的传输的同步风扇-out(每个
    // 消息+每个工具调用一个阻塞写入)，因此任意大的
    // on-disk 线程将阻塞整个重放的所有其他请求。
    // 绑定它：仅恢复最近的kReplayMax消息（其余的保留在
    // thread.messages中，并且仍然在下一个提示词
    // 上发送到模型——这仅限制客户端中的re-rendered），
    // 并且限制每个助手消息发出的工具调用。
    constexpr std::size_t kReplayMax          = 200;
    constexpr std::size_t kReplayToolsPerMsg  = 100;
    const std::size_t total = thread.messages.size();
    const std::size_t start = total > kReplayMax ? total - kReplayMax : 0;
    const bool entity = client_compaction_.load(std::memory_order_relaxed);
    // 显示每次压缩发生的位置：就在其边界之后的
    // 第一条消息之前。
    std::size_t next_cmp = 0;
    auto replay_compactions_before = [&](std::size_t mi) {
        while (entity && next_cmp < thread.compactions.size() &&
               thread.compactions[next_cmp].up_to_index <= mi) {
            const auto& rec = thread.compactions[next_cmp];
            a::List<a::ContentBlock> sum;
            sum.push_back(text_block(rec.summary));
            send_update(cap, session_id, a::SU_Compaction{
                "cmp_" + session_id + "_" + std::to_string(next_cmp + 1), "completed",
                a::Just(std::move(sum)), a::Nothing, json::object()});
            ++next_cmp;
        }
    };
    for (std::size_t mi = start; mi < total; ++mi) {
        replay_compactions_before(mi);
        const auto& m = thread.messages[mi];
        if (m.role == Role::User) {
            if (m.text.empty()) continue;
            send_update(cap, session_id, a::SU_UserMessageChunk{
                text_block(m.text), a::Just(a::MessageId{m.id.value})});
        } else if (m.role == Role::Assistant) {
            if (!m.thinking.empty())
                send_update(cap, session_id, a::SU_AgentThoughtChunk{
                    text_block(m.thinking), a::Just(a::MessageId{m.id.value})});
            const std::string body = m.text.empty() ? m.streaming_text : m.text;
            if (!body.empty())
                send_update(cap, session_id, a::SU_AgentMessageChunk{
                    text_block(body), a::Just(a::MessageId{m.id.value})});

            std::size_t emitted = 0;
            for (const auto& tc : m.tool_calls) {
                if (emitted++ >= kReplayToolsPerMsg) break;
                {
                    auto call = make_tool_call(tc, a::ToolCallStatus::Pending, scwd);
                    merge_meta(call.meta, air_tool_meta(tc));
                    send_update(cap, session_id, a::SU_ToolCall{std::move(call)});
                }

                const auto status = (tc.is_failed() || tc.is_rejected())
                                  ? a::ToolCallStatus::Failed : a::ToolCallStatus::Completed;
                a::ToolCallUpdate upd;
                upd.toolCallId = a::ToolCallId{tc.id.value};
                upd.status     = a::Just(status);
                const std::string out = tc.output();
                if (!out.empty()) {
                    a::List<a::ToolCallContent> content;
                    // 匹配实时路径的per-tool结果整形（读取→line
                    // -numbered；其他markdown-escaped），
                    // 因此重放会话的卡看起来与它们第一次运行时相同。
                    if (status == a::ToolCallStatus::Failed) {
                        std::string shown = out.find("```") == std::string::npos
                                          ? "```\n" + out + "\n```" : out;
                        content.push_back(a::ToolCallContent{
                            a::TCC_Content{text_block(std::move(shown)), json::object()}});
                    } else {
                        content.push_back(result_content_block(tc, out));
                    }
                    upd.content   = a::Just(std::move(content));
                    upd.rawOutput = a::Just<json>(json{{"text", out}});
                }
                send_update(cap, session_id, a::SU_ToolCallUpdate{std::move(upd)});
            }
        }
    }
    replay_compactions_before(total);
}

a::InitializeResult AgentServer::on_initialize(const Cap&, const a::InitializeParams& p) {
    a::InitializeResult r;
    // 记住客户端是否可以为我们托管终端。 Zed 对此做了广告； bare/test 客户端
    // 可能不会。门控 run_tools 中 `bash` 的 live-terminal
    // 执行（参见 client_supports_terminal_）。
    client_supports_terminal_.store(p.clientCapabilities.terminal,
                                    std::memory_order_relaxed);
    // v1.11 会话表面：通知、压缩实体和布尔
    // 配置选项只能发送到表示 渲染 的客户端。
    client_notices_.store(p.clientCapabilities.session.notices, std::memory_order_relaxed);
    client_compaction_.store(p.clientCapabilities.session.compaction,
                             std::memory_order_relaxed);
    client_bool_config_.store(p.clientCapabilities.session.booleanConfig,
                              std::memory_order_relaxed);
    client_elicit_form_.store(p.clientCapabilities.elicitForm, std::memory_order_relaxed);
    client_elicit_url_.store(p.clientCapabilities.elicitUrl, std::memory_order_relaxed);
    // URL模式：MCP服务器可能会要求它，并且它们的"done"转到编辑器。
    if (p.clientCapabilities.elicitUrl)
        // 弱：回调是进程-wide槽，并且服务器读取器可以在〜
        // AgentServer清除它之前获取它的副本。
        ::agentty::mcp::set_url_elicitation([weak = weak_from_this()](const std::string& id) {
            // 当 URL 引出完成时，由 MCP 桥从服务器的读取器 线程 调用。
            // 线程 不包含 agentty 锁（它是 mcp-cpp 的 传输
            // 层 循环），因此通知 re-states 在这里 - 已审查的根
            // （tests/lint/allowlist.txt）。
            auto self = weak.lock();
            if (!self) return;
            try {
                maya::thread_root([&](const maya::unlocked& c) {
                    self->peer_->notify_raw(c, "elicitation/complete", json{{"elicitationId", id}});
                });
            } catch (...) {}
        });
    // Zed的终端通道：命令工具调用携带_meta.terminal_info，
    // 其输出数据流为_meta.terminal_output，以terminal_exit结尾，
    // 编辑器渲染为卡内真实的终端。与我们自己的沙盒执行一起工作，
    // 与 client-hosted terminal/create. 不同
    {
        const json& cm = p.clientCapabilities.meta;
        const auto flag = [&](const char* k) {
            return cm.is_object() && cm.contains(k) && cm[k].is_boolean() && cm[k].get<bool>();
        };
        client_terminal_meta_.store(flag("terminal_output") || flag("terminal_output_delta"),
                                    std::memory_order_relaxed);
        client_terminal_delta_.store(flag("terminal_output_delta"), std::memory_order_relaxed);
        client_terminal_auth_.store(flag("terminal-auth"), std::memory_order_relaxed);
        // JetBrains AIR 声明 {jetbrains:{air:{version>=1,capability:[...]}}}。
        // 格式错误的声明没有任何作用。
        if (cm.is_object() && cm.contains("jetbrains") && cm["jetbrains"].is_object()) {
            const json& air = cm["jetbrains"].value("air", json{});
            const bool ok = air.is_object() && air.contains("version")
                         && air["version"].is_number_integer() && air["version"].get<int>() >= 1
                         && air.contains("capabilities") && air["capabilities"].is_array();
            if (ok) {
                air_.store(true, std::memory_order_relaxed);
                for (const auto& c : air["capabilities"])
                    if (c == "diffPatch") air_diff_patch_.store(true, std::memory_order_relaxed);
            }
        }
    }
    // 协商最小（我们支持什么，客户提供什么）。我们最多发布 kMaxProtocolVersion
    // （默认为 v1；仅当 acp-cpp 版本通过 -DACP_ENABLE_V2_DRAFT
    // 选择加入草案时才会发布 v2）。 v1-only 客户端仍然登陆 v1；
    // 仅当我们为其构建时，v2 客户端才会登陆 v2。
    r.protocolVersion = a::negotiate_version(a::kMaxProtocolVersion, p.protocolVersion);
    // 锁存clean-cut config-surface门(v2_config)的商定版本。
    negotiated_version_.store(r.protocolVersion, std::memory_order_relaxed);

    r.agentInfo = a::Just<a::ImplementationInfo>(
        {"agentty", a::Nothing, a::Just<std::string>(::agentty::version())});

    auto& caps = r.agentCapabilities;
    caps.loadSession = true;
    caps.promptCapabilities.embeddedContext = true;
    // 提示词中的图像。声明是因为这些块现在是 DECODED
    // 到用户消息 (prompt_parts_from_blocks)
    // 中而不是被删除 - 在丢弃它们的同时声明这一点比不
    // 声明它更糟糕，因为客户端会显示附件已发送。
    //
    // 所选择的MODEL是否可以看到它们是提供商层已经
    // 回答的单独问题（supports_vision/org-policy权利），
    // 并且它通过保留图像而不是失败来降级。
    // 音频保持未声明状态：我们提供的任何提供商都不会接受它，
    // 并且静默转码将是关于发送内容的谎言。
    caps.promptCapabilities.image = true;
    caps.auth.logout = a::Just(a::Unit{});
    caps.sessionCapabilities.list      = a::Just(a::Unit{});
    caps.sessionCapabilities.resume    = a::Just(a::Unit{});
    caps.sessionCapabilities.close     = a::Just(a::Unit{});
    caps.sessionCapabilities.fork      = a::Just(a::Unit{});   // 不稳定，由Zed使用
    caps.sessionCapabilities.additionalDirectories = a::Just(a::Unit{});
    // JetBrains AIR:说出我们所说的它的哪个扩展。仅限AIR。
    if (air_.load(std::memory_order_relaxed))
        r.meta = json{{"jetbrains", {{"air", {{"version", 1},
                       {"capabilities", json::array({"diffPatch"})}}}}}};
#if AGENTTY_MCP
    // MCP桥讲stdio、Streamable HTTP和SSE，
    // 因此可以移交编辑器的各种服务器。
    caps.mcpCapabilities.http = true;
    caps.mcpCapabilities.sse  = true;
#endif
    caps.sessionCapabilities.deleteCap = a::Just(a::Unit{});
    // 通过 _meta 通告通用 JSON-RPC 请求 取消 ($/
    // cancel_request，2026-06-29 稳定) — 该
    // 规范在那里携带扩展功能。看到此信息的客户端可能会通过其 id 取消 发出 in
    // -flight 请求；我们尊重 session/prompt 请求。
    caps.meta["cancelRequest"] = true;

    // 宣传当我们没有 凭据 时如何登录，因此 Zed 渲染 在面板中具有适当的
    // "Sign in" 可供性，而不是在第一个 提示词 上出错。
    // 该方法仅供参考 — agentty 的真实登录时间为 -of-band
    // (`agentty login`) — 但表面显示与本机代理的标志
    // -in UX 相匹配。当已经通过身份验证时，我们不会发布任何广告，
    // 因此 Zed 会显示代理已准备就绪。
    if (auth::is_empty(auth())) {
        // 终端方法让编辑器在其自己的终端中运行`agentty login`，
        // 然后对调用进行验证，这与-reads和已保存有关。
        // Zed 将其宣传为 auth.terminal (标准) 或 _meta.terminal-auth。
        if (p.clientCapabilities.authTerminal) {
            a::AuthMethod m;
            m.id          = "agentty-login";
            m.name        = "Sign in to agentty";
            m.description = a::Just<std::string>(
                "Opens a terminal to sign in with Claude, ChatGPT, Copilot or an API key.");
            m.type        = "terminal";
            m.args        = {"login"};
            r.authMethods.push_back(std::move(m));
        } else if (client_terminal_auth_.load(std::memory_order_relaxed)) {
            a::AuthMethod m;
            m.id          = "agentty-login";
            m.name        = "Sign in to agentty";
            m.description = a::Just<std::string>(
                "Opens a terminal to sign in with Claude, ChatGPT, Copilot or an API key.");
            m.meta = json{{"terminal-auth", {{"command", util::self_exe_path().string()},
                                             {"args", json::array({"login"})},
                                             {"label", "agentty login"}}}};
            r.authMethods.push_back(std::move(m));
        } else {
            a::AuthMethod m;
            m.id          = "agentty-login";
            m.name        = "Sign in to agentty";
            m.description = a::Just<std::string>(
                "Run `agentty login` in a terminal to authenticate your provider, "
                "then reconnect.");
            r.authMethods.push_back(std::move(m));
        }
    }
    return r;
}

a::NewSessionResult AgentServer::on_new_session(const Cap& cap, const a::NewSessionParams& p) {
    auto extra = extra_roots_of(p.additionalDirectories);   // 走上坏路
    adopt_client_mcp(cap, p.mcpServers);
    ThreadId tid = persistence::new_id();
    const std::string sid = tid.value;
    const Profile session_profile = profile_;
    {
        Session s;
        s.id  = sid;
        s.cwd = p.cwd;
        s.extra_roots = std::move(extra);
        s.profile = profile_;
        s.thread.id = tid;
        s.thread.workspace = p.cwd;
        s.thread.title = p.cwd.empty() ? std::string{"ACP session"}
                                       : std::string{"ACP "} + p.cwd;
        sessions_.with(cap, [](Sessions& ss, Session v) {
            std::string k = v.id;
            ss.by_id.emplace(std::move(k), std::move(v));
        }, std::move(s));
    }
    persist(cap, sid);
    index_session(cap, sid);

    a::NewSessionResult r;
    r.sessionId = a::SessionId{sid};
    // v1客户端也获得模式为SessionModeState。
    // 下面的 `mode` 配置选项在每个连接上发送，因为 Zed 仅在不存在
    // configOptions 时读取模式。
    if (!v2_config())
        r.modes = a::Just(modes_for_client(session_profile));
    // 选择器也依赖于结果：JetBrains从中构
    // 建其模型下拉列表并且从不查看随后的更新。
    if (auto opts = config_options(cap, sid, model_id_); !opts.empty())
        r.configOptions = a::Just(std::move(opts));
    // Slash-command菜单，以及与仅观看通知的客户
    // 端的更新相同的配置。
    emit_session_config(cap, sid, model_id_);
    return r;
}

// session/fork：一个新会话，其历史记录是另一个会话、
// 编辑者的"fork from here"的副本。源可以是实时的，
// 也可以仅在磁盘上；文案保留了它的模型、思维和模式，并获得了新的ID和标题。
a::NewSessionResult AgentServer::on_fork_session(const Cap& cap, const a::ForkSessionParams& p) {
    auto extra = extra_roots_of(p.additionalDirectories);
    adopt_client_mcp(cap, p.mcpServers);
    const std::string src = p.sessionId.value;
    if (src.empty()) throw a::RpcError(a::errc::InvalidParams, "session/fork: missing sessionId");

    struct Copy { Thread thread; Profile profile; std::string model, provider, effort; bool reasoning; };
    std::optional<Copy> copy = with_session(cap, src, [](Session& s) {
        return Copy{s.thread, s.profile, s.model, s.provider, s.effort, s.show_reasoning};
    });
    if (!copy) {
        auto loaded = persistence::load_thread_by_id(IoAccess::grant(), ThreadId{src});
        if (!loaded)
            throw a::RpcError(a::errc::InvalidParams, "session/fork: no such session: " + src,
                              json{{"sessionId", src}});
        copy = Copy{std::move(*loaded), profile_, "", "", "", true};
    }

    ThreadId tid = persistence::new_id();
    const std::string sid = tid.value;
    {
        Session s;
        s.id       = sid;
        s.cwd      = p.cwd;
        s.extra_roots = std::move(extra);
        s.profile  = copy->profile;
        s.model    = std::move(copy->model);
        s.provider = std::move(copy->provider);
        s.effort   = std::move(copy->effort);
        s.show_reasoning = copy->reasoning;
        s.thread   = std::move(copy->thread);
        s.thread.id = tid;
        s.thread.forked_from = src;
        if (!p.cwd.empty()) s.thread.workspace = p.cwd;
        s.thread.title = s.thread.title.empty() ? std::string{"Fork"}
                                                : "Fork of " + s.thread.title;
        sessions_.with(cap, [](Sessions& ss, Session v) {
            std::string k = v.id;
            ss.by_id.emplace(std::move(k), std::move(v));
        }, std::move(s));
    }
    persist(cap, sid);
    index_session(cap, sid);

    a::NewSessionResult r;
    r.sessionId = a::SessionId{sid};
    if (!v2_config())
        r.modes = a::Just(modes_for_client(copy->profile));
    if (auto opts = config_options(cap, sid, model_id_); !opts.empty())
        r.configOptions = a::Just(std::move(opts));
    emit_session_config(cap, sid, model_id_);
    const std::string title = with_session(cap, sid, [](Session& s) { return s.thread.title; })
                                  .value_or(std::string{});
    if (!title.empty())
        send_update(cap, sid, a::SU_SessionInfo{a::Just(title), a::Nothing, json::object()});
    return r;
}

a::LoadSessionResult AgentServer::on_load_session(const Cap& cap, const a::LoadSessionParams& p) {
    adopt_client_mcp(cap, p.mcpServers);
    std::string sid = p.sessionId.value;
    std::string cwd = p.cwd;
    // InvalidParams，不是InternalError。 Zed 的客户端在代码
    // (agent_servers/src/acp.rs) 上分支：AuthRequired
    // 打开 Sign-in 流程，任何其他非 -InternalError 都会
    // 以 -is 的形式向用户显示，而 InternalError 则被视为 "the agent broke"
    // — 因此将客户端错误报告为 InternalError 会指责我们的错误
    // 请求并丢失可操作的消息。
    if (sid.empty())
        throw a::RpcError(a::errc::InvalidParams, "session/load: missing sessionId");

    Thread thread;
    bool   from_memory = false;
    Profile profile = profile_;

    auto extra = extra_roots_of(p.additionalDirectories);
    // Live会话：采用新的cwd和root，并将线程复制出来。
    if (auto live = with_session(cap, sid, [](Session& s, std::string new_cwd,
                                         std::vector<std::string> roots) {
            if (!new_cwd.empty()) s.cwd = std::move(new_cwd);
            s.extra_roots = std::move(roots);
            return std::pair{s.thread, s.profile};
        }, cwd, extra)) {
        thread      = std::move(live->first);
        profile     = live->second;
        from_memory = true;
    }

    if (!from_memory) {
        // load_thread_by_id读取on-disk格式（追加
        // 日志或遗留单个文档）；裸 <id>.JSON 读取会错过 线程 已
        // 保存 作为日志。
        auto loaded = persistence::load_thread_by_id(IoAccess::grant(), ThreadId{sid});
        // 子代理运行，从其父卡打开。
        if (!loaded) loaded = persistence::load_subagent_thread(IoAccess::grant(), ThreadId{sid});
        if (!loaded)
            throw a::RpcError(a::errc::InvalidParams,
                              "session/load: no such session: " + sid,
                              json{{"sessionId", sid}});
        thread = std::move(*loaded);

        Session s;
        s.id = sid; s.cwd = cwd; s.profile = profile; s.thread = thread;
        s.extra_roots = std::move(extra);
        // try_emplace：自上述检查以来，
        // 相同id的并发加载可能已使其存活（并对其进行排队）。保留那个。
        sessions_.with(cap, [](Sessions& ss, Session v) {
            std::string k = v.id;
            ss.by_id.try_emplace(std::move(k), std::move(v));
        }, std::move(s));
    }

    replay_history(cap, sid, thread);
    // 在 TUI 中产生的分叉指向 工作区 之外的其父代转录本；
    // 让读取工具准确到达该文件（#83）。
    {
        std::vector<std::string> transcripts;
        for (const auto& msg : thread.messages)
            if (msg.fork_note && !msg.fork_transcript.empty())
                transcripts.push_back(msg.fork_transcript);
        if (!transcripts.empty())
            tools::util::allow_fork_transcripts(IoAccess::grant(), persistence::threads_dir(),
                                                transcripts);
    }
    // Re-advertise 恢复会话的斜杠-command菜单
    // +模型选择面板，并推送其标题，以便Zed的侧边栏条目填充为
    // 与本机代理相同。
    emit_session_config(cap, sid, model_id_);
    if (!thread.title.empty())
        send_update(cap, sid, a::SU_SessionInfo{
            a::Just<std::string>(thread.title), a::Nothing, json::object()});

    a::LoadSessionResult r;
    if (!v2_config())
        r.modes = a::Just(modes_for_client(profile));
    if (auto opts = config_options(cap, sid, model_id_); !opts.empty())
        r.configOptions = a::Just(std::move(opts));
    return r;
}

void AgentServer::on_cancel(const Cap& cap, const a::CancelParams& p) {
    // 取消正在运行的转弯以及在其后面排队的每个提示词。
    // 在该 通知 之后发送的 提示词 不受影响。
    auto toks = with_session(cap, p.sessionId.value, [](Session& s) {
        std::vector<http::CancelTokenPtr> out;
        for (const auto& [rid, tok] : s.queued) out.push_back(tok);
        return out;
    });
    if (toks) cancel_tokens(cap, std::move(*toks));
}

void AgentServer::cancel_tokens(const maya::below<maya::leaf>& cap, std::vector<http::CancelTokenPtr> toks) {
    for (auto& t : toks) if (t) t->cancel();
    // 唤醒在 acquire_turn 中等待的 提示词，以便 已取消 离开队列。
    sessions_.with(cap, [](Sessions&) {});
}

void AgentServer::register_prompt(const maya::below<maya::leaf>& cap, const std::string& sid, const std::string& rid) {
    sessions_.with(cap, [](Sessions& ss, std::string id, std::string r) {
        auto it = ss.by_id.find(id);
        if (it == ss.by_id.end()) return;
        it->second.queued.emplace_back(r, std::make_shared<http::CancelToken>());
        ss.reqid_to_session[std::move(r)] = std::move(id);
    }, sid, rid);
}

std::optional<AgentServer::Acquired>
AgentServer::acquire_turn(const Cap& cap, const std::string& sid, const std::string& rid,
                          std::optional<Message> msg, std::string typed) {
    return sessions_.wait_with(cap,
        [](const Sessions& ss, const std::string& id, const std::string& r,
           const std::optional<Message>&, const std::string&) {
            auto it = ss.by_id.find(id);
            if (it == ss.by_id.end()) return true;
            const auto& q = it->second.queued;
            auto me = std::ranges::find(q, r, &std::pair<std::string, http::CancelTokenPtr>::first);
            if (me == q.end() || me->second->is_cancelled()) return true;
            return me == q.begin();
        },
        [](Sessions& ss, std::string id, std::string r, std::optional<Message> m,
           std::string typed_text) -> std::optional<Acquired> {
            auto it = ss.by_id.find(id);
            if (it == ss.by_id.end()) return std::nullopt;
            Session& s = it->second;
            auto me = std::ranges::find(s.queued, r,
                                        &std::pair<std::string, http::CancelTokenPtr>::first);
            if (me == s.queued.end()) return std::nullopt;
            if (me->second->is_cancelled()) {   // 等待时放弃
                s.queued.erase(me);
                ss.reqid_to_session.erase(r);
                return std::nullopt;
            }
            Acquired out{me->second, {}};
            s.cancel = me->second;
            if (m) {
                if (s.thread.messages.empty() && !typed_text.empty()) {
                    s.thread.title = persistence::title_from_first_message(typed_text);
                    out.title = s.thread.title;
                }
                s.thread.messages.push_back(std::move(*m));
            }
            return out;
        }, sid, rid, std::move(msg), std::move(typed));
}

void AgentServer::release_turn(const maya::below<maya::leaf>& cap, const std::string& sid, const std::string& rid) {
    sessions_.with(cap, [](Sessions& ss, std::string id, std::string r) {
        ss.reqid_to_session.erase(r);
        auto it = ss.by_id.find(id);
        if (it == ss.by_id.end()) return;
        Session& s = it->second;
        auto me = std::ranges::find(s.queued, r,
                                    &std::pair<std::string, http::CancelTokenPtr>::first);
        if (me == s.queued.end()) return;
        if (s.cancel == me->second) s.cancel.reset();
        s.queued.erase(me);
    }, sid, rid);
}

void AgentServer::on_cancel_request(const Cap& cap, const a::RpcId& id) {
    // 通用JSON-RPC$/cancel_request：客户端想要取消具有
    // 该id的请求。我们只跟踪 -flight session/prompt 请求
    // （唯一的 long-running 请求），因此将 id 映射回其会话和
    // 取消 — 与 副作用 到 session/cancel 相同，只是通过请求
    // id 而不是 sessionId 进行寻址。未知的ID（已经-settled轮次，
    // 或非-prompt请求）是no-op，即spec-legal。
    auto tok = sessions_.with(cap, [](Sessions& ss, std::string rid) -> http::CancelTokenPtr {
        auto mit = ss.reqid_to_session.find(rid);
        if (mit == ss.reqid_to_session.end()) return nullptr;
        auto sit = ss.by_id.find(mit->second);
        if (sit == ss.by_id.end()) return nullptr;
        for (const auto& [r, t] : sit->second.queued) if (r == rid) return t;
        return nullptr;
    }, id.dump());
    if (tok) cancel_tokens(cap, std::vector<std::shared_ptr<http::CancelToken>>{tok});
}

// ── 会话模式 ───────────────────────────────────────────────────────────
const char* AgentServer::mode_id_for(Profile p) {
    switch (p) {
        case Profile::Ask:     return "ask";
        case Profile::Write:   return "write";
        case Profile::Minimal: return "minimal";
    }
    return "ask";
}

Profile AgentServer::profile_from_mode_id(const std::string& mode_id, Profile fallback) {
    if (mode_id == "ask")     return Profile::Ask;
    if (mode_id == "write")   return Profile::Write;
    if (mode_id == "minimal") return Profile::Minimal;
    return fallback;
}

a::SessionModeState AgentServer::mode_state(Profile current) {
    a::SessionModeState st;
    st.currentModeId = a::SessionModeId{mode_id_for(current)};
    st.availableModes = {
        a::SessionMode{a::SessionModeId{"ask"}, "Ask",
            a::Just<std::string>("Prompt before edits, commands, and network access")},
        a::SessionMode{a::SessionModeId{"write"}, "Write",
            a::Just<std::string>("Edit files and run commands without prompting; still prompt for risky ops")},
        a::SessionMode{a::SessionModeId{"minimal"}, "Minimal",
            a::Just<std::string>("Prompt for everything, including file reads")},
    };
    return st;
}

// mode_state加上AIR的模式种类：Write仍然要求有风险的操作，
// 所以是auto_review；其他人先问。
a::SessionModeState AgentServer::modes_for_client(Profile current) const {
    auto st = mode_state(current);
    for (auto& m : st.availableModes)
        m.meta = air_meta(json{{"kind", m.id.value == "write" ? "auto_review" : "standard"}});
    return st;
}

void AgentServer::on_set_mode(const Cap& cap, const a::SetModeParams& p) {
    const auto applied = with_session(cap, p.sessionId.value, [](Session& s, std::string mode) {
        s.profile = profile_from_mode_id(mode, s.profile);
        return s.profile;
    }, p.modeId.value);
    if (!applied)
        throw a::RpcError(a::errc::InvalidParams,
                          "session/set_mode: unknown sessionId: " + p.sessionId.value,
                          json{{"sessionId", p.sessionId.value}});
    send_update(cap, p.sessionId.value,
        a::SU_CurrentMode{a::SessionModeId{mode_id_for(*applied)}, json::object()});
    // 为 渲染 的客户端保留 `mode` 配置选项。
    emit_config_state(cap, p.sessionId.value, model_id_);
}

a::SetConfigOptionResult AgentServer::on_set_config_option(const Cap& cap, const a::SetConfigOptionParams& p) {
    const auto unknown = [&p] {
        return a::RpcError(a::errc::InvalidParams,
                           "session/set_config_option: unknown sessionId: "
                               + p.sessionId.value,
                           json{{"sessionId", p.sessionId.value}});
    };
    if (!with_session(cap, p.sessionId.value, [](Session&) { return true; })) throw unknown();

    const auto bad = [&p](std::string why, json supported = nullptr) {
        json data{{"configId", p.configId}, {"value", p.value}};
        if (!supported.is_null()) data["supported"] = std::move(supported);
        return a::RpcError(a::errc::InvalidParams,
                           "session/set_config_option: " + why, std::move(data));
    };
    if (p.configId != "show_thinking" && !p.value.is_string())
        throw bad(p.configId + " takes a string value");
    const std::string value = p.value_string();

    if (p.configId == "model") {
        // 我们的列表中的值是"<provider>\t<model>"；裸 ID（较
        // 旧的客户端或 hand-written 客户端）表示活动提供程序上的模型。
        auto [prov, model] = split_choice(value);
        if (model.empty()) throw bad("empty model");
        const auto choices = model_choices(cap);
        const bool listed = std::ranges::any_of(choices, [&](const ModelChoice& c) {
            return c.value == value || (prov.empty() && c.model == model);
        });
        if (!listed && !choices.empty())
            throw bad("model '" + model + "' is not on any signed-in provider");
        if (!prov.empty() && prov != provider::active().catalog_id() && !stream_to_)
            throw bad("this agent can only use the active provider's models");
        if (prov == provider::active().catalog_id()) prov.clear();
        if (!with_session(cap, p.sessionId.value,
                          [](Session& s, std::string pv, std::string m) {
                              s.provider = std::move(pv);
                              s.model    = std::move(m);
                              return true;
                          }, std::move(prov), std::move(model)))
            throw unknown();
    } else if (p.configId == "effort") {
        const auto cur = with_session(cap, p.sessionId.value, [](Session& s) {
            return std::pair{s.provider, s.model};
        });
        if (!cur) throw unknown();
        const std::string model = cur->second.empty() ? model_id_ : cur->second;
        const auto ladder = effort_ladder(model, cur->first.empty()
                                                    ? provider::active().catalog_id() : cur->first);
        json ok = json::array({"auto", "none"});
        for (Effort e : ladder) ok.push_back(std::string{effort_wire(e)});
        if (std::ranges::find(ok, json(value)) == ok.end())
            throw bad("this model doesn't take effort '" + value + "'", ok);
        with_session(cap, p.sessionId.value, [](Session& s, std::string v) {
            s.effort = v == "auto" ? std::string{} : std::move(v);
            return true;
        }, value);
    } else if (p.configId == "show_thinking") {
        if (!p.is_bool()) throw bad("show_thinking takes a boolean");
        with_session(cap, p.sessionId.value, [](Session& s, bool on) {
            s.show_reasoning = on;
            return true;
        }, p.value_bool());
    } else if (p.configId == "mode") {
        // 权限配置文件，与旧的session/set_mode路径共享。
        // 针对已知的 ID 进行验证，因此错误的值是错误，
        // 而不是看起来成功的静默 Ask 回退。
        if (value != "ask" && value != "write" && value != "minimal")
            throw bad("mode must be ask|write|minimal",
                      json::array({"ask", "write", "minimal"}));
        const auto got = with_session(cap, p.sessionId.value, [](Session& s, std::string mode) {
            s.profile = profile_from_mode_id(mode, s.profile);
            return s.profile;
        }, value);
        if (!got) throw unknown();
        const Profile applied = *got;
        // v2客户端在下面的配置回显中看到变化； v1 客户端还保留
        // SessionModeState，因此也更新它。
        if (!v2_config())
            send_update(cap, p.sessionId.value,
                a::SU_CurrentMode{a::SessionModeId{mode_id_for(applied)}, json::object()});
    } else {
        // 未知的配置ID是错误，而不是看起来成功的no-op。
        throw bad("unknown configId '" + p.configId + "'",
                  json::array({"mode", "model", "effort", "show_thinking"}));
    }

    // 在结果中回显 COMPLETE 新配置状态（规范要求它；
    // Zed 和 JetBrains 从中协调他们的选择
    // 器）并作为仅观看 通知 的客户端的更新。
    a::SetConfigOptionResult r{config_options(cap, p.sessionId.value, model_id_), json::object()};
    emit_config_state(cap, p.sessionId.value, model_id_);
    return r;
}

// 编辑器调用在运行我们的终端登录方法之后(或任何它想要检查
// 的时间)。 Re-read 凭据 存储：登录只是从另一个
// 进程写入的，我们的副本是从启动开始的。
void AgentServer::on_authenticate(const a::AuthenticateParams& p) {
    if (p.methodId != "agentty-login")
        throw a::RpcError(a::errc::InvalidParams,
                          "authenticate: unknown methodId '" + p.methodId + "'");
    if (auth::is_empty(auth()) && reauth_) {
        auth::AuthHeader fresh = reauth_();
        auth_.store(fresh);
        // 模型列表取决于登录的提供商。
        model_choices_.publish(nullptr);
    }
    if (auth::is_empty(auth()))
        throw a::RpcError(a::AuthRequired,
                          "still not signed in. Run `agentty login` and try again.");
}

void AgentServer::on_logout(const Cap&) {
    auth::clear_credentials(IoAccess::grant());   // ACP 处理函数，不是 状态更新函数
    auth_.store(auth::AuthHeader{auth::ApiKeyHeader{""}});
}

// ── 会话生命周期: 列表 / 恢复 / 关闭 / 删除 ────────────────────────
// acp_sessions.json被每个ACP实例（每个编辑器窗口一个）读取
// -modify-written，因此它像线程索引一样经历with_shared_file：
// in-process车道加cross-process文件锁。
namespace {
std::filesystem::path session_index_path() {
    return persistence::threads_dir() / "acp_sessions.json";
}
persistence::Lane& session_index_lane() {
    static persistence::Lane l;
    return l;
}
json read_session_index(const char* where) {
    std::ifstream ifs(session_index_path());
    if (!ifs) return json::object();
    try { json j; ifs >> j; if (j.is_object()) return j; }
    catch (const std::exception& e) { util::dbglog(where, e.what()); }
    catch (...) { util::dbglog(where, "non-std exception"); }
    return json::object();
}
void write_session_index(const json& j) {
    // 从不可信的ACP `cwd`导出的会话title/cwd可以包含无效的UTF-8；
    // 默认的 dump() 会抛出 type_error(316)。
    // 代之以U+FFFD，因此索引永远不会使工作线程崩溃。
    (void)persistence::write_json_atomic(IoAccess::grant(), 
        session_index_path(), j.dump(-1, ' ', false, json::error_handler_t::replace));
}
}  // 命名空间结束。

json AgentServer::load_session_index(const Cap& cap) {
    return persistence::with_shared_file(cap, session_index_lane(), session_index_path(),
        [](const maya::held<lock_levels::file_lane>&, persistence::Held) {
            return read_session_index("acp.load_session_index");
        });
}

void AgentServer::index_session(const Cap& cap, const std::string& id) {
    auto meta = with_session(cap, id, [](Session& s) { return std::pair{s.cwd, s.thread.title}; });
    if (!meta) return;
    persistence::with_shared_file(cap, session_index_lane(), session_index_path(),
        [](const maya::held<lock_levels::file_lane>&, persistence::Held, std::string key,
           json entry) {
            json j = read_session_index("acp.index_session");
            j[key] = std::move(entry);
            write_session_index(j);
        }, id, json{
            {"cwd", meta->first},
            {"title", meta->second},
            {"updatedAt", std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()},
        });
}

void AgentServer::unindex_session(const Cap& cap, const std::string& id) {
    persistence::with_shared_file(cap, session_index_lane(), session_index_path(),
        [](const maya::held<lock_levels::file_lane>&, persistence::Held, std::string key) {
            json j = read_session_index("acp.unindex_session");
            if (!j.contains(key)) return;
            j.erase(key);
            write_session_index(j);
        }, id);
}

a::ListSessionsResult AgentServer::on_list_sessions(const Cap& cap, const a::ListSessionsParams& p) {
    std::string filter_cwd = p.cwd.value_or("");
    json index = load_session_index(cap);

    // 现场会话尚未在 on-disk 索引中（cwd，标题）。
    const auto live = sessions_.read(cap, [](const Sessions& ss) {
        std::vector<std::array<std::string, 3>> out;
        for (const auto& [id, s] : ss.by_id) out.push_back({id, s.cwd, s.thread.title});
        return out;
    });
    const auto now_s = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    for (const auto& [id, cwd, title] : live)
        if (!index.contains(id))
            index[id] = json{{"cwd", cwd}, {"title", title}, {"updatedAt", now_s}};

    struct Row { std::string id, cwd, title; std::int64_t at; };
    std::vector<Row> rows;
    for (auto it = index.begin(); it != index.end(); ++it) {
        const json& meta = it.value();
        std::string cwd = meta.value("cwd", "");
        if (cwd.empty() || (!filter_cwd.empty() && cwd != filter_cwd)) continue;
        const auto t = meta.find("title");
        const auto u = meta.find("updatedAt");
        rows.push_back({it.key(), std::move(cwd),
                        t != meta.end() && t->is_string() ? t->get<std::string>() : "",
                        u != meta.end() && u->is_number_integer() ? u->get<std::int64_t>() : 0});
    }
    // 最新的在前：编辑器将其显示为"recent chats"。
    std::ranges::sort(rows, [](const Row& x, const Row& y) {
        return x.at != y.at ? x.at > y.at : x.id < y.id;
    });

    // 不透明光标＝偏移到该顺序中。 100 页。
    constexpr std::size_t kPage = 100;
    std::size_t from = 0;
    if (p.cursor) {
        try { from = std::stoul(*p.cursor); } catch (...) {
            throw a::RpcError(a::errc::InvalidParams, "session/list: bad cursor");
        }
    }
    a::ListSessionsResult result;
    for (std::size_t i = from; i < rows.size() && i < from + kPage; ++i) {
        Row& r = rows[i];
        a::SessionInfo info;
        info.sessionId = a::SessionId{std::move(r.id)};
        info.cwd       = std::move(r.cwd);
        if (!r.title.empty()) info.title = a::Just<std::string>(std::move(r.title));
        if (r.at > 0) info.updatedAt = a::Just<std::string>(iso8601(r.at));
        result.sessions.push_back(std::move(info));
    }
    if (from + kPage < rows.size())
        result.nextCursor = a::Just<std::string>(std::to_string(from + kPage));
    return result;
}

a::ResumeSessionResult AgentServer::on_resume_session(const Cap& cap, const a::ResumeSessionParams& p) {
    return on_load_session(cap, p);   // 恢复+重播；相同的结果形状
}

// 丢弃会话和它所拥有的每一回合的取消（正在运行的会话和在其后面
// 排队的任何会话）。单独擦除它会留下一个正在运行的回合 流式
// 传输 并运行工具，直到其下一步边界注意到会话消失，
// 然后 session/delete 的文件删除会与该回合的 保存 竞争。
void AgentServer::drop_session(const Cap& cap, const std::string& sid) {
    auto toks = sessions_.with(cap, [](Sessions& ss, std::string id) {
        std::vector<std::shared_ptr<http::CancelToken>> out;
        auto it = ss.by_id.find(id);
        if (it == ss.by_id.end()) return out;
        for (const auto& [rid, tok] : it->second.queued) out.push_back(tok);
        ss.by_id.erase(it);
        return out;
    }, sid);
    cancel_tokens(cap, std::move(toks));
}

void AgentServer::on_close_session(const Cap& cap, const a::CloseSessionParams& p) {
    drop_session(cap, p.sessionId.value);
}

void AgentServer::on_delete_session(const Cap& cap, const a::DeleteSessionParams& p) {
    drop_session(cap, p.sessionId.value);
    persistence::delete_thread(IoAccess::grant(), cap, ThreadId{p.sessionId.value});
    unindex_session(cap, p.sessionId.value);
}

// ── 提示词 + 转圈 ───────────────────────────────────────────────────────
void AgentServer::on_prompt(const Cap& cap, const a::PromptParams& p, Responder resp) {
    std::string sid  = p.sessionId.value;
    auto parts = prompt_parts_from_blocks(p.prompt);
    std::string text = std::move(parts.text);
    const std::string typed_raw = text;   // 在任何 /command 扩展之前

    // 本地OpenAI-compatible后端(Ollama、
    // llama.cpp)不需要密钥，因此空认证对它们来说是合法的。仅要求托管 (
    // TLS) 端点或 Anthropic 路径的 凭据。
    {
        const auto& sel = provider::active();
        const bool keyless_local =
            sel.kind == provider::Kind::OpenAI && !sel.openai_endpoint.use_tls;
        const bool known = with_session(cap, sid, [](Session&) { return true; }).has_value();
        if (!known) {
            resp.error(cap, a::errc::InvalidParams, "unknown sessionId: " + sid);
            return;
        }
        if (auth::is_empty(auth()) && !keyless_local) {
            resp.error(cap, a::AuthRequired,
                "agentty has no credentials — run `agentty login` first");
            return;
        }
    }

    // 来自我们广告的菜单的斜线命令。这些是操作，而不是 提示
    // 词：将 "/compact" 发送给模型只会得到聊天回复。
    {
        std::string_view t = text;
        while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front()))) t.remove_prefix(1);
        while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back())))  t.remove_suffix(1);
        if (t == "/new") {
            // 同一会话中的新上下文：客户端保留其选项卡。
            with_session(cap, sid, [](Session& s) {
                s.thread.messages.clear();
                s.thread.compactions.clear();
                s.warned_context = false;
                return true;
            });
            persist(cap, sid);
            send_update(cap, sid, a::SU_AgentMessageChunk{
                text_block("Started a fresh conversation. Earlier messages are no longer "
                           "sent to the model."), a::Nothing});
            resp.ok(cap, a::PromptResult{a::StopReason::EndTurn, json::object()});
            return;
        }
        if (t == "/compact" || t.starts_with("/compact ")) {
            std::string rid = jsonrpc::to_json(resp.id()).dump();
            register_prompt(cap, sid, rid);
            util::run_isolated_detached("acp.compact",
                [](const maya::unlocked& cap, std::stop_token, maya::co_owned<AgentServer> self,
                   std::string sid, std::string rid, Responder r) {
                    self->run_compact(IoAccess::grant(), cap, std::move(sid), std::move(rid),
                                      std::move(r));
                }, maya::co_owned<AgentServer>::of(*this), std::move(sid), std::move(rid),
                std::move(resp));
            return;
        }
        // 用户斜杠命令扩展为其提示词模板；技能指令中的拼接；
        // MCP 提示词 渲染。一个解析器 (runtime/slash.hpp)
        // 决定使用哪一个，与 TUI 使用的解析器相同。
        if (t.starts_with('/')) {
            const Io io = IoAccess::grant();
            const slash::Catalog cat = slash::discover(io);
            bool active = false;
            if (const auto hit = slash::resolve(t, cat);
                hit.what == slash::Resolved::What::Skill) {
                const Thread snap = with_session(cap, sid, [](Session& s) {
                    return s.thread;
                }).value_or(Thread{});
                active = tools::skills::is_active_in(hit.entry->name, ::agentty::visible_text(snap));
            }
            if (auto out = slash::expand_checked(io, t, cat, active)) {
                // 提示词缺少必需的参数：告诉用户要输入
                // 什么并结束回合。发送到模型后，
                // 该注释将像问题一样得到回答。
                if (out->usage) {
                    send_update(cap, sid, a::SU_AgentMessageChunk{text_block(std::move(out->text)), a::Nothing});
                    resp.ok(cap, a::PromptResult{a::StopReason::EndTurn, json::object()});
                    return;
                }
                text = std::move(out->text);
            }
        }
    }
    // 标题来自于用户带类型的("/review src")，而不是扩展模板。
    std::string typed = typed_raw;
    Message um;
    um.role   = Role::User;
    um.text   = std::move(text);
    um.images = std::move(parts.images);

    // 现在就排队，这样在 工作线程 开始之前到达的 session/cancel
    // 或 $/cancel_request 仍然会找到这个 提示词。
    // 该消息在轮到时加入 线程，因此排队的 提示词 永远不会到达中间 -turn。
    std::string req_id_dump = jsonrpc::to_json(resp.id()).dump();
    register_prompt(cap, sid, req_id_dump);

    // 运行全程关闭读写器线程;引擎保持空闲状态以传送我们的
    // 出站 权限 响应。当回合结束时，响应者解决延迟的
    // session/prompt。作业 co-owns 服务器，
    // 因此在退出时仍在运行的轮次不能比它长。
    util::run_isolated_detached("acp.turn_worker",
        [](const maya::unlocked& cap, std::stop_token, maya::co_owned<AgentServer> self,
           std::string sid, std::string rid, Responder r, Message m, std::string typed) {
            self->run_turn(IoAccess::grant(), cap, std::move(sid), std::move(rid), std::move(r),
                           std::move(m), std::move(typed));
        }, maya::co_owned<AgentServer>::of(*this), std::move(sid),
        std::move(req_id_dump), std::move(resp), std::move(um), std::move(typed));
}

namespace {
// 调用是作为文本泄露的弱本地模型，被挽救到工具调用中。
bool is_salvaged_id(const std::string& id) {
    return std::string_view{id}.starts_with("call_salvaged_");
}
constexpr std::string_view kMemoryToolNames[] = {"remember", "forget", "wipe_memory"};
}  // 命名空间结束。

// /compact:总结到目前为止的线程并记录它，
// 以便稍后轮流发送摘要而不是完整的历史记录。记录本身被保留
// （客户端仍然显示它），与 TUI 相同。
// 当客户端 渲染 时，报告为 v1.11 压缩实体，否则报告为纯文本。
void AgentServer::run_compact(Io, const Cap& cap, std::string sid, std::string rid, Responder resp) {
  struct Release {
      AgentServer& s; const Cap& c; const std::string& sid; const std::string& rid;
      ~Release() { s.release_turn(c, sid, rid); }
  } release{*this, cap, sid, rid};
  try {
    const auto settle = [&] { release_turn(cap, sid, rid); };
    if (!acquire_turn(cap, sid, rid)) {
        settle();
        if (with_session(cap, sid, [](Session&) { return true; }))
            resp.ok(cap, a::PromptResult{a::StopReason::Cancelled, json::object()});
        else
            resp.error(cap, a::errc::InvalidParams, "unknown sessionId: " + sid);
        return;
    }
    struct Snap {
        Thread thread;
        std::string model, provider;
        http::CancelTokenPtr cancel;
    };
    auto snap = with_session(cap, sid, [](Session& s) {
        return Snap{s.thread, s.model, s.provider, s.cancel};
    });
    if (!snap) { settle(); resp.error(cap, a::errc::InvalidParams, "unknown sessionId: " + sid); return; }
    const std::size_t up_to = snap->thread.messages.size();
    if (up_to < 2) {
        settle();
        send_update(cap, sid, a::SU_AgentMessageChunk{
            text_block("Nothing to compact yet."), a::Nothing});
        resp.ok(cap, a::PromptResult{a::StopReason::EndTurn, json::object()});
        return;
    }

    const bool entity = client_compaction_.load(std::memory_order_relaxed);
    const std::string cid = "cmp_" + sid + "_" + std::to_string(snap->thread.compactions.size() + 1);
    const auto started = std::chrono::steady_clock::now();
    // AIR的压缩事实，与每个状态一起发送。
    const auto facts = [&](json extra) {
        extra["trigger"] = "manual";
        extra["durationMs"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        return air_meta(json{{"contextCompaction", std::move(extra)}});
    };
    if (entity) send_update(cap, sid, a::SU_Compaction{cid, "in_progress", a::Nothing, a::Nothing,
                                                  air_meta(json{{"contextCompaction", {{"version", 1}, {"trigger", "manual"}}}})});

    provider::Request req;
    req.model      = snap->model.empty() ? model_id_ : snap->model;
    req.max_tokens = max_output_tokens_for(req.model);
    // 与回合相同的对话身份（#118)。
    req.session_key = snap->thread.id.value;
    req.cancel     = snap->cancel;
    req.auth       = auth();
    req.messages   = app::cmd::compaction_request_messages(
        snap->thread, static_cast<int>(ui::context_max_for_model(req.model)));

    std::string summary, error;
    provider::EventSink sink = [&](Msg m) {
        std::visit([&](auto&& domain) {
            using D = std::decay_t<decltype(domain)>;
            if constexpr (std::is_same_v<D, msg::StreamMsg>) {
                std::visit([&](auto&& ev) {
                    using T = std::decay_t<decltype(ev)>;
                    if constexpr (std::is_same_v<T, StreamTextDelta>) {
                        summary += ev.text;
                        if (entity)
                            send_update(cap, sid, a::SU_CompactionSummaryChunk{
                                cid, text_block(ev.text), json::object()});
                    } else if constexpr (std::is_same_v<T, StreamError>) {
                        error = ev.message;
                    }
                }, domain);
            }
        }, m);
    };
    try {
        if (!snap->provider.empty() && stream_to_) stream_to_(cap, snap->provider, std::move(req), sink);
        else                                        stream_(cap, std::move(req), sink);
    } catch (const std::exception& e) { error = e.what(); }

    const bool cancelled = snap->cancel && snap->cancel->is_cancelled();
    summary = app::cmd::summary_from_reply(summary);
    if (cancelled || !error.empty() || summary.empty()) {
        settle();
        if (entity)
            send_update(cap, sid, a::SU_Compaction{cid, cancelled ? "cancelled" : "failed", a::Nothing,
                        cancelled ? a::Nothing : a::Just<std::string>(error.empty() ? "empty summary" : error),
                        facts(cancelled ? json{{"version", 1}}
                                        : json{{"version", 1}, {"error", error.empty() ? "empty summary" : error}})});
        if (cancelled) { resp.ok(cap, a::PromptResult{a::StopReason::Cancelled, json::object()}); return; }
        resp.error(cap, a::errc::InternalError, "compaction failed: " + (error.empty() ? std::string{"empty summary"} : error));
        return;
    }

    // 记录下来。汇总时到达的消息已超过 up_to，
    // 并逐字保留在 传输 上。
    const auto before_after = with_session(cap, sid, [](Session& s, std::size_t upto, std::string text) {
        Thread::CompactionRecord rec;
        rec.up_to_index = std::min(upto, s.thread.messages.size());
        rec.summary     = std::move(text);
        rec.tokens_before = app::cmd::estimate_wire_tokens(s.thread);
        s.thread.compactions.push_back(std::move(rec));
        s.thread.compactions.back().tokens_after = app::cmd::estimate_wire_tokens(s.thread);
        s.warned_context = false;
        return std::pair{s.thread.compactions.back().tokens_before,
                         s.thread.compactions.back().tokens_after};
    }, up_to, summary);
    settle();
    persist(cap, sid);
    index_session(cap, sid);

    if (entity) {
        // 流式传输的块是原始答复；这会将它们替换为
        // 干净的摘要（规范的权威替换）。
        a::List<a::ContentBlock> clean;
        clean.push_back(text_block(summary));
        json f{{"version", 1}};
        if (before_after) {
            f["preTokens"]  = before_after->first;
            f["postTokens"] = before_after->second;
        }
        send_update(cap, sid, a::SU_Compaction{cid, "completed", a::Just(std::move(clean)),
                                          a::Nothing, facts(std::move(f))});
    } else {
        std::string msg = "Compacted the conversation.";
        if (before_after && before_after->first > 0)
            msg += " About " + std::to_string(before_after->first / 1000) + "k tokens down to " +
                   std::to_string(before_after->second / 1000) + "k.";
        send_update(cap, sid, a::SU_AgentMessageChunk{text_block(std::move(msg)), a::Nothing});
    }
    resp.ok(cap, a::PromptResult{a::StopReason::EndTurn, json::object()});
  } catch (const std::exception& e) {
    resp.error(cap, a::errc::InternalError, std::string{"compaction failed: "} + e.what());
  } catch (...) {
    resp.error(cap, a::errc::InternalError, "compaction failed");
  }
}

void AgentServer::run_turn(Io io, const Cap& cap, std::string session_id, std::string req_id_dump,
                           Responder resp, Message user_msg, std::string typed) {
  // 将会话移交给下一个排队的提示词，但是本会话结束。
  struct Release {
      AgentServer& s; const Cap& c; const std::string& sid; const std::string& rid;
      ~Release() { s.release_turn(c, sid, rid); }
  } release{*this, cap, session_id, req_id_dump};
  // 这在 DETACHED 工作线程 线程 上运行，完全在 acp-cpp 的
  // handle_request try/catch. 之外，
  // 此处未捕获的抛出将 调用 std::terminate() 并终止进程中的每个会话；
  // 如果不解析 `resp` 就返回，则客户端的 session/prompt
  // 未来永远悬而未决（响应程序析构函数不会 auto-reply）。因此，
  // 整个 正文 都受到保护：任何转义都会用 InternalError 解析延迟的 提示词。
  // 如果我们已经出错，则 ok/error did-latch 会使尾随的 resp.ok
  // (cap, ...) 成为 no-op，因此只发送了一个回复。
  try {
    // 等待该会话中已经运行的任何回合，然后添加我们的消息。
    // 排队时 已取消：回答 已取消，从不运行。
    auto got = acquire_turn(cap, session_id, req_id_dump, std::move(user_msg), std::move(typed));
    if (!got) {
        if (with_session(cap, session_id, [](Session&) { return true; }))
            resp.ok(cap, a::PromptResult{a::StopReason::Cancelled, json::object()});
        else
            resp.error(cap, a::errc::InvalidParams, "unknown sessionId: " + session_id);
        return;
    }
    if (!got->title.empty())
        send_update(cap, session_id, a::SU_SessionInfo{
            a::Just<std::string>(got->title), a::Nothing, json::object()});

    bool cancelled = false;
    bool errored = false;
    std::string error_msg;
    StopReason last_stop = StopReason::EndTurn;

    // Doom-loop保护弱本地模型：计算整个回合中回收的(泄露的
    // -JSON)工具轮数。在 kMaxSalvged 后（或任何回收的
    // 内存-tool 调用），强制转弯以纯文本而不是循环完成（TUI 的
    // dedup_releaked_salvage_calls 类似物）。
    constexpr int kMaxSalvaged = 3;
    int salvaged_rounds = 0;
    bool force_text_retry = false;   // 下一个子-turn:用NO工具re-prompt
    bool budget_done = false;        // 最后一个工具-free 重试已发射

    for (int turn = 0; turn < 64; ++turn) {
        // 并发的session/close|删除在下一步结束回合。
        if (!with_session(cap, session_id, [](Session&) { return true; })) {
            errored = true; error_msg = "session vanished"; break;
        }

        StopReason stop = stream_completion(io, cap, session_id, cancelled, error_msg,
                                            force_text_retry);
        last_stop = stop;
        force_text_retry = false;
        if (!error_msg.empty()) { errored = true; break; }
        if (cancelled) break;
        if (stop != StopReason::ToolUse) break;

        // 检查just-streamed辅助消息的工具调用。从弱本地模型
        // 泄露的 调用 带有 call_salvaged_ id。
        // 在非 -explicit 回合中泄露的 内存 工具 (
        // remember/forget/wipe) 都是垃圾，一旦花费了抢救预算，
        // 就会陷入恶性循环。在这两种情况下，我们 FAIL 泄漏的 调用
        // 而不运行它们，将该失败作为工具结果反馈（因此 传输 tool_use
        // ↔ 结果配对保持有效），并让模型采用 一个 更多 sub-turn
        // 以纯文本回答 - 而不是返回空白气泡。
        {
            // 在锁下的实时会话中失败泄露调用，
            // 并报告哪些，以便客户端卡跟随、解锁。
            struct Salvage { bool any = false, mem_leak = false;
                             std::vector<std::string> failed; };
            const auto sv = with_session(cap, session_id,
                [](Session& cs, bool fail_now) -> Salvage {
                    Salvage out;
                    if (cs.thread.messages.empty()) return out;
                    Message& back = cs.thread.messages.back();
                    if (back.role != Role::Assistant) return out;
                    for (const auto& tc : back.tool_calls) {
                        if (!is_salvaged_id(tc.id.value)) continue;
                        out.any = true;
                        for (auto t : kMemoryToolNames)
                            if (t == tc.name.value) out.mem_leak = true;
                    }
                    if (!(fail_now || out.mem_leak)) return out;
                    for (auto& tc : back.tool_calls) {
                        if (!is_salvaged_id(tc.id.value) || tc.is_terminal()) continue;
                        tc.status = ToolUse::Failed{
                            {}, {}, "this call was not run — do NOT call tools, "
                            "answer the user in plain text now"};
                        out.failed.push_back(tc.id.value);
                    }
                    return out;
                }, salvaged_rounds + 1 > kMaxSalvaged);
            if (sv && sv->any) {
                ++salvaged_rounds;
                const bool budget_blown = salvaged_rounds > kMaxSalvaged;
                if (sv->mem_leak || budget_blown) {
                    for (const auto& id : sv->failed) {
                        a::ToolCallUpdate u;
                        u.toolCallId = a::ToolCallId{id};
                        u.status     = a::Just(a::ToolCallStatus::Failed);
                        send_update(cap, session_id, a::SU_ToolCallUpdate{std::move(u)});
                    }
                    // 超出预算：一个最终工具-free 重试，
                    // 因此模型以纯文本而不是空气泡回答。
                    // budget_done 闩锁使其只触发一次。
                    if (budget_blown) {
                        if (budget_done) { last_stop = StopReason::EndTurn; break; }
                        budget_done = true;
                        force_text_retry = true;
                        continue;
                    }
                    if (!sv->failed.empty()) { force_text_retry = true; continue; }
                }
            }
        }

        bool ran = run_tools(cap, session_id, cancelled);
        if (cancelled || !ran) break;
    }

    // 如果会话仍然存在则持续。当回合被释放时，
    // 取消 句柄和 reqid 映射将继续。
    const bool alive = with_session(cap, session_id, [](Session&) { return true; }).value_or(false);
    if (alive) {
        persist(cap, session_id);
        index_session(cap, session_id);
        // 为该聊天突出侧边栏的"last active"。
        const auto now_s = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        send_update(cap, session_id, a::SU_SessionInfo{
            a::Nothing, a::Just<std::string>(iso8601(now_s)), json::object()});
    }

    a::PromptResult result;
    result.stopReason = acp_stop_reason(last_stop, cancelled, errored);
    // 失败的转向是其原因的错误，而不是停止原因：
    // `refusal`将告诉客户模型拒绝并隐藏原因。
    // 根据规范要求，取消 仍然获胜。
    if (errored && !cancelled) {
        resp.error(cap, a::errc::InternalError,
                   error_msg.empty() ? std::string{"turn failed"} : error_msg);
        return;
    }
    resp.ok(cap, result);
  } catch (const std::exception& e) {
    util::dbglog("acp.run_turn", e.what());
    resp.error(cap, a::errc::InternalError,
               std::string{"turn failed: "} + e.what());
  } catch (...) {
    util::dbglog("acp.run_turn", "non-std exception");
    resp.error(cap, a::errc::InternalError, "turn failed (unknown error)");
  }
}

StopReason AgentServer::stream_completion(Io io, const Cap& cap, const std::string& session_id,
                                          bool& out_cancelled,
                                          std::string& out_error,
                                          bool suppress_tools) {
    provider::Request req;
    // 快照请求需要ONCE。型号变更 mid-turn 适用于 下一个 转弯；
    // 数据流 本身在没有持有 锁 的情况下运行。
    struct Snap {
        std::string model, cwd, provider, effort, thread_id;
        bool show_reasoning;
        http::CancelTokenPtr cancel;
        std::vector<Message> messages;
    };
    auto snap = with_session(cap, session_id, [](Session& s) {
        // 传输视图：在/compact之后，摘要替换了前缀。
        return Snap{s.model, s.cwd, s.provider, s.effort, s.thread.id.value,
                    s.show_reasoning, s.cancel, app::cmd::wire_messages_for(s.thread)};
    });
    if (!snap) { out_error = "session vanished"; return StopReason::Unspecified; }
    const std::string sess_model = snap->model;
    req.model         = sess_model.empty() ? model_id_ : sess_model;
    // 持久的 线程 id，在重新加载后保持稳定：提示词-cache 路由和
    // 网关会话 id (#118)，如 TUI 中一样。
    req.session_key   = snap->thread_id;
    {
        // 推理努力：会话的选择，受限于模型。
        // 取消设置意味着自动，与TUI使用的每个-model默认值相同。
        const auto caps = resolved_caps(req.model, snap->provider);
        const Effort e = snap->effort == "none" ? Effort::None
                       : snap->effort.empty()   ? Effort::Auto
                                                : effort_from_wire(snap->effort);
        req.effort         = std::string{effort_wire_for(e, caps)};
        req.show_reasoning = snap->show_reasoning && !req.effort.empty();
        req.no_reasoning   = e == Effort::None;
        req.reasoning_off  = caps.reasoning_off;
    }
    // Per-model输出-token天花板(镜子cmd_factory::launch_stream)。
    // 如果没有这个，则默认的16384在推理+工具JSON之间共享，并且大的`edit`
    // 会截断mid-input_json。 context_window 此处留为 0（
    // ACP 路径上没有 per-session /api/show 探测）；
    // 然后 Ollama 传输层 应用其安全代理 -sized num_ctx 默认值，
    // 仍然是 4x Ollama 的截断下限。
    req.max_tokens    = max_output_tokens_for(req.model);
    // 小服务窗口（vLLM 4096) 400s一个仅由max_tokens
    // 填充的请求；当 /v1/models 说出时，目录 知道窗口。
    if (const int win = catalog_context_window_for(req.model); win > 0)
        req.max_tokens = std::min(req.max_tokens, std::max(256, win / 4));
    // Product-level 提示词 策略与 TUI 请求构建器共享：
    // 托管模型获得完整的agent/tool/RAG合同，
    // 而受约束的本地端点获得紧凑的简档。
    {
        const auto sel = provider::active();
        req.system_prompt = provider::system_prompt_for(io, sel);
        // First-classweak-model支持（agent-zero样式）：
        // Ollama本机端点上的弱模型使用JSON-protocol路径（内联工具目录，
        // 单个{tool_name，tool_args}对象）而不是本机工具阵列。
        // 镜像 cmd_factory 的 launch_stream。
        req.json_protocol = is_weak_model(req.model)
            && sel.kind == provider::Kind::OpenAI
            && sel.openai_endpoint.native_api;
    }
    req.cancel        = snap->cancel;
    req.auth          = auth();
    req.messages      = std::move(snap->messages);
    // suppress_tools:刚刚泄露垃圾工具调用的弱本地模型获得
    // 工具-free 重试。由于没有宣传任何工具，它不会泄漏另一个工具，
    // 并以纯文本形式回答用户（已证明：qwen2.5-coder:7b
    // 仅当 传输 上没有工具时才会干净地打招呼）。
    if (!suppress_tools) {
        // 弱本地模型在 greetings/small 谈话中产生 `skill`
        // 和 内存 工具的幻觉——不要为它们做广告（镜像 cmd_factory）。
        const bool weak = is_weak_model(req.model);
        auto weak_hidden = [](std::string_view n) {
            return n == "skill" || n == "remember"
                || n == "forget" || n == "wipe_memory";
        };
        // 与 TUI (#98) 相同的计划：每个启用的可调用工具，
        // 该计划决定什么乘坐 传输，tool_search 找到其余的。
        const tools::Catalog cat = tools::wire_tools();
        tools::tool_loading::Request tr;
        for (const auto& t : *cat) {
            if (tools::withheld_by_policy(t)) continue;
            if (weak && weak_hidden(t.name.value)) continue;
            tr.pool.push_back(&t);
        }
        tr.loaded = tools::plan::loaded_in(req.messages);
        tr.limits = tools::tool_loading::limits_for(
            provider::active(),
            static_cast<std::size_t>(std::max(0, catalog_context_window_for(req.model))), req.model);
        tr.server_instructions = mcp::mcp_server_instructions();
        tr.mode   = tools::tool_loading::current();
        (void)tools::tool_loading::plan_into(tr, req.tools);
    }

    Message assistant;
    assistant.role = Role::Assistant;

    StopReason stop = StopReason::Unspecified;
    // 每个工具 调用 的参数 JSON，由 id 键入。提供者可能会交错
    // 几个 调用' 增量，并在回合结束时发送每个 End（OpenAI
    // 聊天会这样做），因此一个共享的 缓冲区 给出了每个 调用，但最后一个空
    // 参数（#116）。
    std::unordered_map<std::string, std::string> tool_json;
    auto find_call = [&](const ToolCallId& id) -> ToolUse* {
        for (auto it = assistant.tool_calls.rbegin(); it != assistant.tool_calls.rend(); ++it)
            if (it->id == id) return &*it;
        // id-less 事件（从未命名调用的提供商）
        // 表示最新的调用，这是旧的行为。
        if (id.value.empty() && !assistant.tool_calls.empty())
            return &assistant.tool_calls.back();
        return nullptr;
    };
    StreamUsage last_usage;
    bool have_usage = false;
    int last_err_status = 0;
    std::optional<std::chrono::seconds> last_retry_after;
    bool last_err_replayable = true;
    const std::string sid = session_id;
    const std::string scwd = snap->cwd;
    const std::string msg_id = assistant.id.value;

    auto sink = [&](agentty::Msg m) {
        std::visit([&](auto&& domain) {
            using D = std::decay_t<decltype(domain)>;
            if constexpr (std::is_same_v<D, msg::StreamMsg>) {
                std::visit([&](auto&& ev) {
                    using E = std::decay_t<decltype(ev)>;
                    if constexpr (std::is_same_v<E, StreamTextDelta>) {
                        assistant.text += ev.text;
                        send_update(cap, sid, a::SU_AgentMessageChunk{
                            text_block(ev.text), a::Just(a::MessageId{msg_id})});
                    } else if constexpr (std::is_same_v<E, StreamThinkingDelta>) {
                        // Adaptive/extended思维（
                        // 努力）：将可见摘要文本转发为ACP思维块，
                        // 以便Zed 渲染其可折叠"Thinking"部分，
                        // 匹配claude-code-acp。
                        // 不透明签名保留在内部（重播给提供者，从未向客户端显示）。
                        // 签名的块按块逐字保存，
                        // 与TUI相同：Anthropic拒绝其
                        // 思考块被丢弃或合并的工具转动。
                        auto open_block = [&]() -> Message::ThinkingBlock& {
                            if (assistant.thinking_blocks.empty())
                                assistant.thinking_blocks.emplace_back();
                            return assistant.thinking_blocks.back();
                        };
                        if (ev.block_boundary && !assistant.thinking_blocks.empty()
                            && (!assistant.thinking_blocks.back().text.empty()
                                || !assistant.thinking_blocks.back().signature.empty()
                                || !assistant.thinking_blocks.back().redacted_data.empty())) {
                            assistant.thinking_blocks.emplace_back();
                            if (!assistant.thinking.empty()) assistant.thinking += "\n\n";
                        }
                        if (!ev.redacted_data.empty())
                            open_block().redacted_data = std::move(ev.redacted_data);
                        if (!ev.signature.empty()) {
                            open_block().signature += ev.signature;
                            assistant.thinking_signature = open_block().signature;
                        }
                        if (!ev.text.empty()) {
                            open_block().text += ev.text;
                            assistant.thinking += ev.text;   // 加载重播
                            send_update(cap, sid, a::SU_AgentThoughtChunk{
                                text_block(ev.text), a::Just(a::MessageId{msg_id})});
                        }
                    } else if constexpr (std::is_same_v<E, StreamReasoning>) {
                        // Codex/Responses加密推理：
                        // 在下一个请求时重放，标记为创建它的站点。
                        if (!ev.encrypted.empty()) {
                            assistant.reasoning_site = ev.site;
                            if (!assistant.reasoning_encrypted.empty())
                                assistant.reasoning_encrypted.push_back('\n');
                            assistant.reasoning_encrypted += ev.encrypted;
                        }
                    } else if constexpr (std::is_same_v<E, StreamToolUseStart>) {
                        ToolUse tc;
                        tc.id = ev.id; tc.name = ev.name;
                        assistant.tool_calls.push_back(std::move(tc));
                        tool_json[ev.id.value].clear();
                        a::ToolCall call;
                        call.toolCallId = a::ToolCallId{ev.id.value};
                        // Args 尚未流式传输，因此这是一种 -based 回退 ("Read"、
                        // "Write"、"Terminal"，…) — 绝不是裸露的小写工具名称。
                        // StreamToolUseEnd 上的
                        // SU_ToolCallUpdate 通过 arg 细节对其进行了改进。
                        call.title      = tool_title(assistant.tool_calls.back(), scwd);
                        call.name       = a::Just<std::string>(ev.name.value);
                        call.kind       = acp_tool_kind(ev.name.value);
                        call.status     = a::ToolCallStatus::Pending;
                        // Zed从FIRST tool_call创建终端；
                        // 输出并退出，然后在以后的更新中退出 数据流。
                        if (is_command_tool(ev.name.value) && terminal_channel())
                            call.meta = json{{"terminal_info",
                                              {{"terminal_id", ev.id.value}, {"cwd", scwd}}}};
                        send_update(cap, sid, a::SU_ToolCall{std::move(call)});
                    } else if constexpr (std::is_same_v<E, StreamToolUseDelta>) {
                        if (ToolUse* tcp = find_call(ev.id)) tool_json[tcp->id.value] += ev.partial_json;
                    } else if constexpr (std::is_same_v<E, StreamToolUseSnapshot>) {
                        if (ToolUse* tcp = find_call(ev.id)) tool_json[tcp->id.value] = ev.json;   // 取代
                    } else if constexpr (std::is_same_v<E, StreamObservedToolResult>) {
                        // 外部代理已经运行它； run_tools 会跳
                        // 过 终端 调用，因此不会运行两次。
                        if (ToolUse* tcp = find_call(ev.id)) {
                            const auto now = std::chrono::steady_clock::now();
                            std::string out = ev.output;
                            if (out.empty())
                                out = ev.failed ? "External agent tool failed."
                                                : "Executed by external agent.";
                            if (ev.failed) tcp->settle_failed(now, std::move(out));
                            else           tcp->settle_done(now, std::move(out));
                        }
                    } else if constexpr (std::is_same_v<E, StreamToolUseEnd>) {
                        if (ToolUse* tcp = find_call(ev.id)) {
                            auto& tc = *tcp;
                            const std::string& raw = tool_json[tc.id.value];
                            try {
                                tc.args = raw.empty()
                                    ? json::object() : json::parse(raw);
                                // `cmd`→`command`等，
                                // 因此ACP卡title/content (tool_title，
                                // command_content)读取规范密钥——客户端
                                // 仅看到tc.args，而从未看到原始数据流 JSON。
                                (void)::agentty::app::detail::canonify_tool_args(
                                    tc.name.value, tc.args);
                            } catch (const std::exception& e) {
                                util::dbglog("acp.tool_args.parse", e.what());
                                tc.args = json::object();
                            } catch (...) {
                                util::dbglog("acp.tool_args.parse", "non-std exception");
                                tc.args = json::object();
                            }
                            a::ToolCallUpdate upd;
                            upd.toolCallId = a::ToolCallId{tc.id.value};
                            upd.title      = a::Just(tool_title(tc, scwd));
                            upd.rawInput   = a::Just<json>(tc.args);
                            upd.locations  = a::Just(tool_locations(tc, scwd));
                            // 现在args已经完全流入，
                            // edit/write可以立即显示推测的差异卡
                            // (在permission/execution之前)，
                            // 并且终端 调用可以显示其FULL
                            // 命令(标题被剪辑为一行)。
                            a::List<a::ToolCallContent> c;
                            if (auto d = announce_diff(tc, scwd)) c.push_back(std::move(*d));
                            if (auto cc = command_content(tc))    c.push_back(std::move(*cc));
                            if (auto pc = prompt_content(tc))     c.push_back(std::move(*pc));
                            if (!c.empty()) upd.content = a::Just(std::move(c));
                            merge_meta(upd.meta, air_tool_meta(tc));
                            send_update(cap, sid, a::SU_ToolCallUpdate{std::move(upd)});

                            // `todo`写入驱动Zed的本机plan/checklist小部件
                            // （claude-code-acp奇偶校验）
                            // ——除了上面的工具卡之外，在列表已知时
                            // 发出计划。
                            if (auto plan = plan_from_todos(tc))
                                send_update(cap, sid, std::move(*plan));
                        }
                    } else if constexpr (std::is_same_v<E, StreamFinished>) {
                        stop = ev.stop_reason;
                    } else if constexpr (std::is_same_v<E, StreamError>) {
                        out_error = ev.message;
                        last_err_status = ev.http_status;
                        last_retry_after = ev.retry_after;
                        last_err_replayable = !ev.non_replayable;
                    } else if constexpr (std::is_same_v<E, StreamUsage>) {
                        last_usage = ev; have_usage = true;
                    }
                }, domain);
            }
        }, m);
    };

    // 重试 在任何内容到达编辑器之前失败的请求，
    // 当失败是通过的类型（过载、速率限制、
    // 连接断开）并且重新发送不能重复工作时。与 TUI 相同的规则：
    // 一旦文本或工具调用流式传输，轮到用户的是重试。
    constexpr int kMaxAttempts = 4;
    for (int attempt = 1;; ++attempt) {
        provider::Request this_req = req;
        try {
            if (!snap->provider.empty() && stream_to_)
                stream_to_(cap, snap->provider, std::move(this_req), sink);
            else
                stream_(cap, std::move(this_req), sink);
        } catch (const std::exception& e) {
            out_error = std::string{"stream backend: "} + e.what();
        }
        if (out_error.empty() || attempt >= kMaxAttempts) break;
        if (snap->cancel && snap->cancel->is_cancelled()) break;
        const bool streamed = !assistant.text.empty() || !assistant.thinking.empty()
                           || !assistant.tool_calls.empty();
        const auto cls = provider::classify_stream_error(out_error, last_err_status);
        if (streamed || !last_err_replayable
            || (cls != provider::ErrorClass::Transient && cls != provider::ErrorClass::RateLimit))
            break;
        auto wait = std::chrono::seconds{1 << (attempt - 1)};   // 1, 2, 4 s
        if (last_retry_after) wait = std::clamp(*last_retry_after, std::chrono::seconds{1},
                                                std::chrono::seconds{60});
        notice(cap, sid, "warning",
               cls == provider::ErrorClass::RateLimit ? "Rate limited, retrying"
                                                      : "Provider error, retrying",
               out_error + " (attempt " + std::to_string(attempt + 1) + " of " +
                   std::to_string(kMaxAttempts) + " in " + std::to_string(wait.count()) + "s)");
        // 等待取消本身：停止立即结束等待。
        if (maya::delay_for(cap, snap->cancel ? snap->cancel->token() : std::stop_token{}, wait))
            break;
        out_error.clear();
        last_err_status = 0;
        last_retry_after.reset();
        stop = StopReason::Unspecified;
    }

    if (snap->cancel && snap->cancel->is_cancelled()) out_cancelled = true;

    if (have_usage) {
        const std::string model = sess_model.empty() ? model_id_ : sess_model;
        long long used = static_cast<long long>(last_usage.input_tokens) +
                         last_usage.cache_creation_input_tokens +
                         last_usage.cache_read_input_tokens +
                         last_usage.output_tokens;
        a::SU_Usage u;
        u.used = used < 0 ? 0 : used;
        u.size = static_cast<std::int64_t>(ui::context_max_for_model(model));
        // 当窗口超过 85% 时，每个会话警告一次：接下来的回合
        // 是答案被切断的地方。
        if (u.size > 0 && u.used * 100 >= u.size * 85) {
            const bool first = with_session(cap, session_id, [](Session& s) {
                return !std::exchange(s.warned_context, true);
            }).value_or(false);
            if (first)
                notice(cap, sid, "warning", "Context window is nearly full",
                       std::to_string(u.used * 100 / u.size) +
                       "% used. Run /compact, or start a new thread.");
        }
        send_update(cap, sid, std::move(u));
    }
    if (!out_error.empty() && !out_cancelled)
        notice(cap, sid, "error", "The model request failed", out_error);

    // 共享消息向量的结构突变——锁每-session 线
    // 程 互斥量，因此并发读取器-thread 快照 (
    // session/load|恢复重播，持续）无法观察半
    // -grown向量或竞态重新分配。仅用于推送，
    // 而不是上面的数据流。
    with_session(cap, session_id,
                 [](Session& s, Message m) { s.thread.messages.push_back(std::move(m)); return true; },
                 std::move(assistant));
    return stop;
}

void AgentServer::settle_tool(const Cap& cap, const TurnCtx& ctx, ToolUse& tc, ToolUse::Status st,
                              std::chrono::steady_clock::time_point executing_since) {
    if (executing_since != std::chrono::steady_clock::time_point{}) {
        if (auto* d = std::get_if<ToolUse::Done>(&st))   d->executing_since = executing_since;
        if (auto* f = std::get_if<ToolUse::Failed>(&st)) f->executing_since = executing_since;
    }
    tc.status = st;
    // Write它由(消息id，工具id)返回：会话在-turn中关闭，
    // 或移动的消息，仅意味着没有任何更新。
    with_session(cap, ctx.id, [](Session& s, std::string mid, std::string tid, ToolUse::Status v) {
        for (auto it = s.thread.messages.rbegin(); it != s.thread.messages.rend(); ++it) {
            if (it->id.value != mid) continue;
            for (auto& t : it->tool_calls)
                if (t.id.value == tid) { t.status = std::move(v); return true; }
            return false;
        }
        return false;
    }, ctx.msg_id, tc.id.value, std::move(st));
}

bool AgentServer::run_tools(const Cap& cap, const std::string& session_id, bool& out_cancelled) {
    // 一次性复制出批次和门需要的内容。该轮廓是整个批次的一个稳定值；
    // 模式更改 mid-turn 需要 副作用 下一回合。
    // 工具在没有保留 锁 的副本上运行，并且每个已确定的状态都
    // 通过 settle_tool 写回。
    struct Batch {
        TurnCtx ctx;
        Profile profile = Profile::Ask;
        std::vector<ToolUse> calls;
        std::set<std::string> grants;
    };
    auto batch = with_session(cap, session_id, [](Session& s) -> std::optional<Batch> {
        if (s.thread.messages.empty()) return std::nullopt;
        const Message& last = s.thread.messages.back();
        if (last.role != Role::Assistant || last.tool_calls.empty()) return std::nullopt;
        return Batch{TurnCtx{s.id, s.cwd, last.id.value, s.cancel, s.extra_roots,
                             s.thread.id.value},
                     s.profile, last.tool_calls, s.grants};
    });
    if (!batch || !*batch) return false;
    const TurnCtx ctx = (*batch)->ctx;
    const Profile profile = (*batch)->profile;
    auto& grants = (*batch)->grants;

    // Tool-scheduling 注意：与 TUI 路径 (cmd_factory)
    // 使用 effects::is_parallel_safe(active,
    // want) 扇出 compose-safe 工具 (Pure/ReadFs/Net)
    // 并序列化独占工具 (WriteFs/Exec) 不同，ACP 路径按发射
    // 顺序运行每个工具 调用 STRICTLY SERIALLY。
    // 串行执行是parallel-safety规则的子集，因此它永远不会违反
    // 副作用集要求的排他性；它只是放弃了重叠独立读取的延迟优势。结果按 传输
    // tool_use/result 配对的发射顺序排列，而 Zed 渲染 无论如何
    // 都是按顺序排列卡片的。
    auto set_status = [&](ToolUse& tc, ToolUse::Status st) { settle_tool(cap, ctx, tc, std::move(st)); };

    for (auto& tc : (*batch)->calls) {
        if (ctx.cancelled()) { out_cancelled = true; return false; }
        if (tc.is_terminal()) continue;   // 由外部代理结算

        // 解析工具ONCE，使得权限门和执行针对相同的定义运行（
        // mcp/tools/list_changed mid
        // -turn可以另外将-resolve名称重新命名为具有不同副作用的ToolDef）。
        const tools::Found td = tool::DynamicDispatch::resolve(tc.name.value);

        bool needs_perm =
            tool::DynamicDispatch::needs_permission_with(td, profile)
            && !grants.contains(tc.name.value);
        // 本次会议早些时候的"Always reject"：未经询问就拒绝。
        const bool denied_always = needs_perm && with_session(cap, ctx.id, [](Session& s, std::string n) {
            return s.denials.contains(n);
        }, tc.name.value).value_or(false);
        if (needs_perm) {
            PermissionOutcome outcome = PermissionOutcome::Deny;
            if (!denied_always) {
                a::ToolCallUpdate u;
                u.toolCallId = a::ToolCallId{tc.id.value};
                u.status     = a::Just(a::ToolCallStatus::Pending);
                send_update(cap, ctx.id, a::SU_ToolCallUpdate{std::move(u)});
                outcome = ask_permission(cap, ctx.id, tc,
                                         ctx.cancel ? ctx.cancel->token() : std::stop_token{});
            }
            if (ctx.cancelled()) { out_cancelled = true; return false; }
            if (outcome == PermissionOutcome::AllowAlways) {
                grants.insert(tc.name.value);
                with_session(cap, ctx.id, [](Session& s, std::string n) { s.grants.insert(std::move(n)); return true; },
                             tc.name.value);
            }
            if (outcome == PermissionOutcome::DenyAlways)
                with_session(cap, ctx.id, [](Session& s, std::string n) { s.denials.insert(std::move(n)); return true; },
                             tc.name.value);
            if (outcome == PermissionOutcome::Deny || outcome == PermissionOutcome::DenyAlways) {
                set_status(tc, ToolUse::Rejected{});
                a::ToolCallUpdate r;
                r.toolCallId = a::ToolCallId{tc.id.value};
                r.status     = a::Just(a::ToolCallStatus::Failed);
                send_update(cap, ctx.id, a::SU_ToolCallUpdate{std::move(r)});
                continue;
            }
        }

        a::ToolCallUpdate running;
        running.toolCallId = a::ToolCallId{tc.id.value};
        running.status     = a::Just(a::ToolCallStatus::InProgress);
        send_update(cap, ctx.id, a::SU_ToolCallUpdate{std::move(running)});

        // Live-terminal 快速路径：当客户端托管 终端 并且我们的 沙箱
        // 没有包装命令时，通过客户端的 ACP 终端 运行 `bash`，以便 Zed
        // 数据流 标准输出到其本机 终端 小部件（claude-code-acp UX）中，
        // 而不是我们捕获静态文本。发出自己的完成更新+设置tc.status；
        // nullopt 表示
        // 不满足条件，改用下方内部执行路径。
        if (auto tr = run_bash_via_terminal(cap, ctx, tc)) {
            if (tr->cancelled) { out_cancelled = true; return false; }
            continue;
        }

        // 在工作线程上运行该工具，使得落在-tool中部（在引擎的读取
        // 器线程上）的session/cancel使得THIS转弯循环迅速返回而不是阻塞，
        // 直到长bash/web_fetch完成。
        // 工具本身是不可中断的（其接口是同步的），因此在 取消 上，
        // 我们丢弃其结果——TUI 路径通过其 幂等 工具-output
        // 防护使用相同的 "discard the late output"
        // 合约。
        //
        // pool.submit_isolated，不是标准的异步
        // 帮助器：该帮助器的未来加入到其析构函数中，因此放弃用于
        // 需要分离的 "tool_reaper" 线程 的 已取消 工具，
        // 只是为了将未来保留在它可以阻止的地方。
        // 池 的未来彻底消失，工作仍然拥有（在拆卸时等待）。
        //
        // `wake`是该循环等待的一件事。有两个来源阻止了它：
        // 工具完成（作业在出去时请求它）和会话的取消（
        // stop_callback桥接它）。两者都是 事件，
        // 因此下面的等待会在发生任何一种情况时立即唤醒 - 它过去每
        // 50 毫秒轮询一次未来。
        std::stop_source wake;
        // 模型已经可以看到的技能，源自该会话的可见记录（
        // 在会话 锁 下复制），然后交给作业 —
        // `skill` 工具在那里运行并且没有 线程。
        // 只有技能工具才能读取它，因此只有派生才值得。
        std::vector<std::string> active_skills;
        if (tc.name.value == "skill") {
            active_skills = with_session(cap, ctx.id, [](Session& s) {
                return tools::skills::active_in(::agentty::visible_text(s.thread));
            }).value_or(std::vector<std::string>{});
        }
        tools::CallContext call_ctx;
        call_ctx.active_skills = std::move(active_skills);
        call_ctx.conversation  = ctx.thread_id;
        // 会话的取消到达运行工具本身（shell被停止），
        // 而不仅仅是这个等待。
        if (ctx.cancel) call_ctx.cancel.push_back(ctx.cancel->token());
        // 该会话的文件夹：相对路径和外壳在其cwd中开始，
        // 并且即使当代理进程在其他地方启动时，
        // 它也可以到达其cwd加上编辑器的额外根。
        if (!ctx.cwd.empty()) {
            call_ctx.project = ctx.cwd;
            if (!tools::util::is_within_workspace(ctx.cwd))
                call_ctx.extra_roots.push_back(ctx.cwd);
        }
        for (const auto& r : ctx.extra_roots) call_ctx.extra_roots.push_back(r);
        // co-own服务器下面的回调，不可借用：已
        // 取消或超时的工具被废弃在池上，并且在本轮（
        // 及其句柄）消失后仍然可以调用它们。
        const auto self = maya::co_owned<AgentServer>::of(*this);
        // MCP服务器向用户询问一些信息，
        // mid-call转到编辑器的表格UI，与该会话和工具卡相关联。
        if (client_elicit_form_.load(std::memory_order_relaxed)
            || client_elicit_url_.load(std::memory_order_relaxed))
            call_ctx.elicit = [self, sid = ctx.id, tid = tc.id.value,
                               tok = ctx.cancel ? ctx.cancel->token() : std::stop_token{}](const json& mcp) {
                // 由工具的调用 线程（MCP服务器的请求，同步中继）上的
                // MCP桥调用，其不持有agentty 锁：已审查的根
                // agentty lock: a reviewed root (tests/lint/allowlist.txt).
                return maya::thread_root([&](const maya::unlocked& c) {
                    return self->elicit_via_client(c, sid, tid, mcp, tok);
                });
            };
        // 子代理运行被保留为它自己的会话，因此Zed可以显示它嵌
        // 套在这张卡(_meta.subagent_session_info)下。
        // 由子代理完成设置一次，工具返回后读取：a 快照。
        auto sub_sid = std::make_shared<maya::snapshot<std::string>>();
        if (tc.name.value == "task")
            call_ctx.on_subagent_done = [sub_sid, parent = ctx.id](const Thread& t) {
                Thread copy = t;
                copy.id = persistence::new_id();
                copy.forked_from = parent;
                if (copy.title.empty()) copy.title = "Subagent";
                if (persistence::save_subagent_thread(IoAccess::grant(), copy))
                    sub_sid->store(copy.id.value);
            };
        // Zed的终端通道：shell命令在运行时将数据流
        // 输出到卡中真实的终端中。每次进步都会给我们带来
        // 整个缓冲区；我们发送新内容。
        const bool term = is_command_tool(tc.name.value) && terminal_channel();
        if (term) {
            auto sent = std::make_shared<std::size_t>(0);
            // 从该工具的工作线程 线程报告进度，该工具不持有agentty 锁；
            // 通知（叶子：对端的发件箱）re-states它在这个回调（
            // tests/lint/allowlist.txt）。
            call_ctx.progress = [self, sid = ctx.id, tid = tc.id.value, sent](std::string_view all) {
                if (all.size() <= *sent) return;
                maya::thread_root([&](const maya::unlocked& c) {
                    self->send_terminal_output(c, sid, tid, all.substr(*sent));
                });
                *sent = all.size();
            };
        } else {
            // 报告进度的任何其他工具（子代理的实时日志、
            // 测试运行、没有终端的编辑器中的外壳）：在其
            // 运行时在卡中显示最新的快照。每秒最多约 6 次更新；
            // 当工具稳定后，最终结果将取代它。
            struct Throttle { std::chrono::steady_clock::time_point last{}; std::size_t sent = 0; };
            auto th = std::make_shared<Throttle>();
            call_ctx.progress = [self, sid = ctx.id, tid = tc.id.value, th](std::string_view all) {
                const auto now = std::chrono::steady_clock::now();
                if (all.size() == th->sent || now - th->last < std::chrono::milliseconds(150)) return;
                th->last = now;
                th->sent = all.size();
                // 仅限末尾：卡不是日志查看器，并且每次更新1
                // 个MB 快照会淹没管道。
                constexpr std::size_t kTail = 8 * 1024;
                std::string_view tail = all.size() > kTail ? all.substr(all.size() - kTail) : all;
                a::ToolCallUpdate u;
                u.toolCallId = a::ToolCallId{tid};
                u.status = a::Just(a::ToolCallStatus::InProgress);
                a::List<a::ToolCallContent> c;
                c.push_back(a::ToolCallContent{a::TCC_Content{
                    text_block("```\n" + tools::util::sanitize_utf8(tail) + "\n```"), json::object()}});
                u.content = a::Just(std::move(c));
                maya::thread_root([&](const maya::unlocked& rc) {   // 工具工作线程:未持有锁
                    self->send_update(rc, sid, a::SU_ToolCallUpdate{std::move(u)});
                });
            };
        }
        // 作业获得其所针对的定义的其自己的副本。
        // 当该循环在最后期限放弃工具时，`abandon`停止工具本身。
        // 如果没有它，timed-out shell/process
        // 在其结果被丢弃后会继续运行，并且由于 ACP 按顺序运行工具，
        // 因此 下一个 工具（一个编辑，另一个 shell）与它在相同
        // 的文件上同时运行，这违反了下面严格的 -serial 合同。
        std::stop_source abandon;
        call_ctx.cancel.push_back(abandon.get_token());
        auto fut = util::background_pool().submit_isolated(
            [](const maya::unlocked& job_cap, std::stop_token pool_stop,
               tools::Found def,
               std::string name, nlohmann::json args, std::stop_source wake,
               tools::CallContext call_ctx) {
                struct Signal {
                    std::stop_source& s;
                    ~Signal() { s.request_stop(); }
                } on_exit{wake};
                // 进程拆卸停止池：工具也必须看到它。
                call_ctx.cancel.push_back(std::move(pool_stop));
                return tool::DynamicDispatch::execute_with(job_cap, def, name, args, call_ctx);
            },
            td, tc.name.value, tc.args, wake, std::move(call_ctx));
        std::optional<std::stop_callback<std::function<void()>>> on_cancel;
        if (ctx.cancel)
            on_cancel.emplace(ctx.cancel->token(),
                              std::function<void()>{[wake]() mutable {
                                  wake.request_stop();
                              }});

        // Belt-and-suspenders wall-clock上限：
        // 一种永远挂起的工具（e.g。没有内部超时的停滞网络读取）
        // 加上从不发送session/cancel的客户端否则将永久固定该工作线程
        // ——以及session/prompt未来。在截止日期前，
        // 我们无法使用该工具并继续前进。
        constexpr auto kToolDeadline = std::chrono::minutes(10);
        const auto tool_start = std::chrono::steady_clock::now();
        const bool woke = maya::delay_for(cap, wake.get_token(), kToolDeadline);
        on_cancel.reset();

        // 完成节拍已取消：在用户已取消的同一瞬间
        // 完成的工具具有真实结果，并且报告是诚实的。
        const bool finished = fut.ready();
        const bool tool_cancelled =
            !finished && woke && ctx.cancelled();
        const bool tool_timed_out = !finished && !woke;

        if (tool_cancelled || tool_timed_out) {
            // 放弃未来，但首先停止工具：作业在池上保持运行，
            // 直到它注意到（外壳被终止、获取中止）、
            // 在拆卸时等待，并且其后期结果被丢弃。
            // 取消 已经通过 ctx.cancel 到达了它。
            abandon.request_stop();
            const char* why = tool_timed_out ? "timed out" : "cancelled";
            if (tool_timed_out) util::dbglog("acp.run_tools.timeout", tc.name.value);
            settle_tool(cap, ctx, tc, ToolUse::Failed{{}, {}, why}, tool_start);
            a::ToolCallUpdate c;
            c.toolCallId = a::ToolCallId{tc.id.value};
            c.status     = a::Just(a::ToolCallStatus::Failed);
            send_update(cap, ctx.id, a::SU_ToolCallUpdate{std::move(c)});
            if (tool_cancelled) { out_cancelled = true; return false; }
            // 超时仅此工具失败；继续转弯，以便模型可以
            // 对失败结果做出反应。
            continue;
        }

        // 已经停止（进程拆卸）的池永远不会运行
        // 作业并违背承诺。失败这个工具而不是回合。
        std::optional<tool::ExecResult> got;
        try {
            got = fut.get(cap);
        } catch (const std::exception& e) {
            util::dbglog("acp.run_tools.submit", e.what());
            set_status(tc, ToolUse::Failed{{}, {}, std::string{"could not run tool: "} + e.what()});
            a::ToolCallUpdate f;
            f.toolCallId = a::ToolCallId{tc.id.value};
            f.status     = a::Just(a::ToolCallStatus::Failed);
            send_update(cap, ctx.id, a::SU_ToolCallUpdate{std::move(f)});
            continue;
        }
        auto result = std::move(*got);

        a::ToolCallUpdate upd;
        upd.toolCallId = a::ToolCallId{tc.id.value};

        if (result) {
            a::List<a::ToolCallContent> content;
            if (result->change) {
                const auto& ch = *result->change;
                a::TCC_Diff diff;
                // 原始绝对路径——必须匹配宣告差异的路径，以便Zed将完成与
                // 同一张卡相关联（claude-code-acp在两个阶段中通过原始file_path键差异）。
                // 权威diff才是真正的on-disk
                // before/after.
                diff.path    = ch.path;
                diff.newText = ch.new_contents;
                if (!ch.original_contents.empty())
                    diff.oldText = a::Just<std::string>(ch.original_contents);
                // 请求它的AIR客户得到一个精确的git 补丁；
                // 文本字段成为其合同指定的占位符。
                if (air_diff_patch_.load(std::memory_order_relaxed)) {
                    if (auto patch = air_git_patch(ch)) {
                        diff.oldText = a::Nothing;
                        diff.newText.clear();
                        diff.meta = air_meta(json{{"diffPatch", {{"version", 1},
                                                    {"format", "git_patch"},
                                                    {"text", *patch}}}});
                    }
                }
                content.push_back(a::ToolCallContent{std::move(diff)});

                // 将卡锚定在第一个改变的行处，以便Zed可以跳转到它（每-hunk行号，
                // 如claude
                // -code-acp的structuredPatch位置）。
                a::List<a::ToolCallLocation> locs;
                for (const auto& h : ch.hunks) {
                    a::ToolCallLocation loc;
                    loc.path = ch.path;
                    if (h.new_start > 0)
                        loc.line = a::Just<std::int64_t>(static_cast<std::int64_t>(h.new_start));
                    locs.push_back(std::move(loc));
                }
                if (locs.empty()) {
                    a::ToolCallLocation loc; loc.path = ch.path;
                    locs.push_back(std::move(loc));
                }
                upd.locations = a::Just(std::move(locs));
            }
            // 当显示差异卡时，差异IS结果——也不要将"Edited (3+ 1-)"
            // 摘要作为文本块重复（这是Zed中的噪音；
            // claude-code-acp不返回Edit/Write的额外内容）。
            // 对于该模型，摘要仍然采用rawOutput。
            if (!result->change && !result->text.empty())
                content.push_back(result_content_block(tc, result->text));

            upd.status    = a::Just(a::ToolCallStatus::Completed);
            upd.content   = a::Just(std::move(content));
            upd.rawOutput = a::Just<json>(json{{"text", result->text}});
            if (term) upd.meta = terminal_exit_meta(tc.id.value, result->text, false);
            if (auto s = sub_sid->get(); !s.empty())
                merge_meta(upd.meta, json{{"subagent_session_info",
                                           {{"session_id", s}, {"message_start_index", 0}}}});
            // tool_start、不、tc.executing_since()。
            // ACP 永远不会将工具移至运行状态，因此 executing_since
            // () 没有任何记录可供读取，并且 始终 返回其活跃度 回退，
            // 卡诞生 - 这里包括到客户端的
            // session/request_permission 往返行程。
            // tool_start 在 池 提交上被标记为该路径的实际 分派，
            // 并且 超时 分支已使用它。现在两条 终端 路径一致。
            settle_tool(cap, ctx, tc, ToolUse::Done{{}, {}, result->text}, tool_start);
        } else {
            std::string detail = result.error().render();
            a::List<a::ToolCallContent> content;
            // 隔离错误，以便 Zed 渲染 将其作为不同的代码块，
            // 而不是将其混合到散文中（claude-code-acp 对错误
            // 结果执行相同的操作）。如果细节看起来已经被围起来了，就跳过围墙。
            std::string shown = detail.find("```") == std::string::npos
                              ? "```\n" + detail + "\n```"
                              : detail;
            content.push_back(a::ToolCallContent{
                a::TCC_Content{text_block(std::move(shown)), json::object()}});
            upd.status    = a::Just(a::ToolCallStatus::Failed);
            upd.content   = a::Just(std::move(content));
            upd.rawOutput = a::Just<json>(json{{"text", detail}, {"error", true}});
            if (term) upd.meta = terminal_exit_meta(tc.id.value, detail, true);
            // 与完成分支相同：tool_start 是 分派 标记。
            settle_tool(cap, ctx, tc, ToolUse::Failed{{}, {}, detail}, tool_start);
        }

        send_update(cap, ctx.id, a::SU_ToolCallUpdate{std::move(upd)});
    }

    return true;
}

// Live-terminal执行`bash` 调用。合同见头文件。
std::optional<AgentServer::TerminalRun>
AgentServer::run_bash_via_terminal(const Cap& cap, const TurnCtx& ctx, ToolUse& tc) {
    namespace sp = tools::spec;

    // 资格：仅限`bash`工具；客户端必须托管终端；
    // 而我们的沙箱一定是NOT是包装命令（
    // 包装命令必须通过agentty自己的bwrap/sandbox-exec运行，
    // 而客户端终端对此一无所知）。当其中任何一个失败时，我们返回 nullopt，
    // 以便调用者回退到内部执行。
    const auto* spec = sp::lookup(tc.name.value);
    if (!spec || spec->kind != sp::Kind::Bash) return std::nullopt;
    if (!client_supports_terminal_.load(std::memory_order_relaxed)) return std::nullopt;
    if (tools::util::sandbox::is_active()) return std::nullopt;

    const auto& args = tc.args;
    auto str = [&](const char* k) -> std::string {
        return (args.contains(k) && args[k].is_string()) ? args[k].get<std::string>() : std::string{};
    };
    std::string command = str("command");
    if (command.empty()) command = str("cmd");
    if (command.empty()) command = str("shell_command");
    if (command.empty()) return std::nullopt;   // 没有什么可运行的；让内部路径报错

    // 以与bash工具相同的方式解析工作目录：显式的
    // `cd`/别名，否则会话cwd。传递给客户端，
    // 以便其 终端 从用户期望的位置开始。
    std::string cd = str("cd");
    if (cd.empty()) cd = str("cwd");
    if (cd.empty()) cd = str("workdir");
    const std::string& run_cwd = cd.empty() ? ctx.cwd : cd;

    auto is_cancelled = [&] { return ctx.cancelled(); };
    auto set_status = [&](ToolUse::Status st,
                          std::chrono::steady_clock::time_point since = {}) {
        settle_tool(cap, ctx, tc, std::move(st), since);
    };
    // 执行开始于HERE（终端创建是分派），
    // 而不是在卡诞生时——镜像本地bash工具的约定，
    // 因此统计面板的工具-time切片测量正在运行，而不是排队。
    const auto exec_started = std::chrono::steady_clock::now();
    auto fail = [&](const std::string& detail) -> TerminalRun {
        set_status(ToolUse::Failed{{}, {}, detail}, exec_started);
        a::ToolCallUpdate f;
        f.toolCallId = a::ToolCallId{tc.id.value};
        f.status     = a::Just(a::ToolCallStatus::Failed);
        a::List<a::ToolCallContent> c;
        c.push_back(a::ToolCallContent{a::TCC_Content{text_block(detail), json::object()}});
        f.content = a::Just(std::move(c));
        send_update(cap, ctx.id, a::SU_ToolCallUpdate{std::move(f)});
        return TerminalRun{false, false, detail};
    };

    // 1) Ask客户端在其自己的终端中催生进程。
    a::CreateTerminalParams cp;
    cp.sessionId = a::SessionId{ctx.id};
    // 通过 shell 运行，因此 pipes/redirects/globs 的行为类似于 bash 工具。
#ifdef _WIN32
    cp.command = "cmd.exe";
    cp.args    = {"/S", "/C", command};
#else
    cp.command = "/bin/sh";
    cp.args    = {"-c", command};
#endif
    if (!run_cwd.empty()) cp.cwd = a::Just<std::string>(run_cwd);
    // 将捕获的输出限制为 bash 工具的 传输 预算，
    // 因此无论执行路径如何，模型都会看到相当数量的文本。
    cp.outputByteLimit = a::Just<std::int64_t>(static_cast<std::int64_t>(spec->max_output_chars));

    std::string terminal_id;
    try {
        terminal_id = call<a::to_client::TerminalCreate>(cap, *peer_, cp).terminalId;
    } catch (const std::exception& e) {
        // 客户端在通告上限之后无法创建终端：回
        // 退到内部执行而不是使工具失败。
        util::dbglog("acp.terminal.create", e.what());
        return std::nullopt;
    }
    if (terminal_id.empty()) return std::nullopt;

    // 确保 终端 始终被释放，即使是在早期的 return/throw. 上
    struct Releaser {
        AgentServer* self; const Cap& cap; std::string sid, tid; bool armed = true;
        void disarm() { armed = false; }
        ~Releaser() {
            if (!armed) return;
            try {
                a::TerminalRef r; r.sessionId = a::SessionId{sid}; r.terminalId = tid;
                (void)call<a::to_client::TerminalRelease>(cap, *self->peer_, r);
            } catch (...) { /* 客户走了；没有什么需要清理的 */ }
        }
    } releaser{this, cap, ctx.id, terminal_id};

    a::TerminalRef ref; ref.sessionId = a::SessionId{ctx.id}; ref.terminalId = terminal_id;

    // 2)显示工具卡中的现场终端。泽德 渲染 流式传输
    //    从该内容块输出其自身，该内容块被键入终端 id。
    {
        a::ToolCallUpdate u;
        u.toolCallId = a::ToolCallId{tc.id.value};
        a::List<a::ToolCallContent> c;
        c.push_back(a::ToolCallContent{a::TCC_Terminal{terminal_id, json::object()}});
        u.content = a::Just(std::move(c));
        send_update(cap, ctx.id, a::SU_ToolCallUpdate{std::move(u)});
    }

    // 3)等待进程退出。会话 取消 缩短了等待时间，
    //    然后terminal/kill停止该过程。
    a::TerminalExitStatus exit{};
    {
        rpc::CallOptions o;
        o.timeout = std::chrono::milliseconds{0};   // 一次构建可以运行一个小时； 取消是出路
        if (ctx.cancel) o.cancel = ctx.cancel->token();
        auto waited = peer_->call<a::to_client::TerminalWaitForExit>(cap, ref, std::move(o));
        if (waited) {
            exit = std::move(*waited);
        } else if (!is_cancelled()) {
            return fail(std::string{"terminal wait failed: "} + waited.error().what());
        } else {
            try { a::TerminalKillParams k; k.sessionId = ref.sessionId; k.terminalId = ref.terminalId;
                  (void)call<a::to_client::TerminalKill>(cap, *peer_, k); }
            catch (const std::exception& e) { util::dbglog("acp.terminal_kill", e.what()); }
            catch (...) { util::dbglog("acp.terminal_kill", "non-std exception"); }
            set_status(ToolUse::Failed{{}, {}, "cancelled"});
            a::ToolCallUpdate c;
            c.toolCallId = a::ToolCallId{tc.id.value};
            c.status     = a::Just(a::ToolCallStatus::Failed);
            // 用静态文本替换活动的 终端 块：当我们返回时，
            // 释放者释放 终端，并且 Zed 渲染 将已释放
            // 的 终端 的块视为空或损坏。
            a::List<a::ToolCallContent> cc;
            cc.push_back(a::ToolCallContent{
                a::TCC_Content{text_block("(cancelled)"), json::object()}});
            c.content = a::Just(std::move(cc));
            send_update(cap, ctx.id, a::SU_ToolCallUpdate{std::move(c)});
            return TerminalRun{false, true, {}};
        }
    }

    // 4)拉取最终捕获的输出。
    std::string output;
    bool truncated = false;
    try {
        a::TerminalOutputParams op; op.sessionId = ref.sessionId; op.terminalId = ref.terminalId;
        auto out = call<a::to_client::TerminalOutput>(cap, *peer_, op);
        output    = std::move(out.output);
        truncated = out.truncated;
    } catch (const std::exception& e) {
        return fail(std::string{"terminal output failed: "} + e.what());
    }

    const std::int64_t code = exit.exitCode.has_value() ? *exit.exitCode : 0;
    const bool ok = !exit.signal.has_value() && code == 0;

    // 构建 model-visible 结果文本，镜像 bash 工具的形状：
    // 捕获的输出，加上non-zero/信号上的退出注释。
    std::string model_text = output;
    if (truncated) model_text += "\n[output truncated]";
    if (exit.signal.has_value())
        model_text += "\n[terminated by signal " + *exit.signal + "]";
    else if (code != 0)
        model_text += "\n[exit code " + std::to_string(code) + "]";

    // 5)最终卡更新：保留终端内容块（Zed保留
    //    回滚缓冲区)，为型号附加rawOutput，设置终端状态。
    a::ToolCallUpdate upd;
    upd.toolCallId = a::ToolCallId{tc.id.value};
    upd.status     = a::Just(ok ? a::ToolCallStatus::Completed : a::ToolCallStatus::Failed);
    a::List<a::ToolCallContent> content;
    content.push_back(a::ToolCallContent{a::TCC_Terminal{terminal_id, json::object()}});
    // ALSO将捕获的输出附加为受围栏文本块。 终端 在该函数
    // 返回（释放者）时被释放，并且 Zed 会丢弃已释放的
    // 终端 的实时小部件 - 因此，如果没有静态文本副本，
    // 完整的卡片将显示 无内容。受隔离的块是用户（以及重新
    // 打开的会话）始终看到的持久 回滚缓冲区。空输出（e.g。
    // 无提示的成功命令）仍然会得到一个最小的注释，
    // 因此该卡不是空白的。
    {
        std::string shown = model_text.empty() ? std::string{"(no output)"} : model_text;
        std::string md = shown.find("```") == std::string::npos
                       ? "```\n" + shown + "\n```"
                       : std::move(shown);
        content.push_back(a::ToolCallContent{
            a::TCC_Content{text_block(std::move(md)), json::object()}});
    }
    upd.content   = a::Just(std::move(content));
    upd.rawOutput = a::Just<json>(json{{"text", model_text},
                                       {"exitCode", code},
                                       {"truncated", truncated}});
    send_update(cap, ctx.id, a::SU_ToolCallUpdate{std::move(upd)});

    // 执行开始于终端创建(分派)，而不是卡的诞生——
    // 将其标记到已确定状态，因此统计数据归并测量运行时间，
    // 而不是排队。
    if (ok) set_status(ToolUse::Done{{}, {}, model_text}, exec_started);
    else    set_status(ToolUse::Failed{{}, {}, model_text}, exec_started);

    return TerminalRun{ok, false, std::move(model_text)};
}

AgentServer::PermissionOutcome
AgentServer::ask_permission(const Cap& cap, const std::string& session_id, const ToolUse& tc,
                            std::stop_token cancel) {
    const std::string cwd =
        with_session(cap, session_id, [](Session& s) { return s.cwd; }).value_or(std::string{});
    a::RequestPermissionParams req;
    req.sessionId = a::SessionId{session_id};
    // ToolCallUpdate携带用于权限对话框的工具卡。
    req.toolCall.toolCallId = a::ToolCallId{tc.id.value};
    req.toolCall.title      = a::Just(tool_title(tc, cwd));
    req.toolCall.kind       = a::Just(acp_tool_kind(tc.name.value));
    if (!tc.args.is_null()) req.toolCall.rawInput = a::Just<json>(tc.args);
    req.toolCall.locations  = a::Just(tool_locations(tc, cwd));
    a::List<a::ToolCallContent> c;
    if (auto d = announce_diff(tc, cwd))  c.push_back(std::move(*d));
    if (auto cc = command_content(tc))    c.push_back(std::move(*cc));
    if (auto pc = prompt_content(tc))     c.push_back(std::move(*pc));
    if (!c.empty()) req.toolCall.content = a::Just(std::move(c));
    // 标签命名该工具，因此"always"表示其涵盖的内容：该工具，
    // 用于本次会议的其余部分。
    const std::string tool = tc.name.value;
    req.options = {
        a::PermissionOption{"allow_once",    "Allow",                        a::PermissionOptionKind::AllowOnce,    json::object()},
        a::PermissionOption{"allow_always",  "Always allow " + tool,         a::PermissionOptionKind::AllowAlways,  json::object()},
        a::PermissionOption{"reject_once",   "Reject",                       a::PermissionOptionKind::RejectOnce,   json::object()},
        a::PermissionOption{"reject_always", "Always reject " + tool,        a::PermissionOptionKind::RejectAlways, json::object()},
    };
    // AIR的批准标题：卡片标题，加上命令本身的摘要。
    if (air_.load(std::memory_order_relaxed)) {
        json perm{{"version", 1}, {"title", tool_title(tc, cwd)}};
        if (tc.args.is_object())
            if (auto it = tc.args.find("display_description");
                it != tc.args.end() && it->is_string() && !it->get<std::string>().empty())
                perm["description"] = *it;
        req.meta = air_meta(json{{"permission", std::move(perm)}});
        merge_meta(req.toolCall.meta, air_tool_meta(tc));
    }

    // session/cancel撤回与$/cancel_request的
    // 打开对话框，如规范的取消级联要求；然后 调用 确定为拒绝。
    rpc::CallOptions o;
    o.cancel      = cancel;
    o.notify_peer = true;
    try {
        auto r = peer_->call<a::to_client::RequestPermission>(cap, req, std::move(o));   // 块 工作线程，不是读卡器
        if (!r) return PermissionOutcome::Deny;
        auto outcome = std::move(*r);
        return a::match(outcome.outcome,
            [](const a::PO_Cancelled&) { return PermissionOutcome::Deny; },
            [](const a::PO_Selected& s) {
                if (s.optionId == "allow_always")  return PermissionOutcome::AllowAlways;
                if (s.optionId == "allow_once")    return PermissionOutcome::AllowOnce;
                if (s.optionId == "reject_always") return PermissionOutcome::DenyAlways;
                return PermissionOutcome::Deny;
            });
    } catch (const std::exception&) {
        return PermissionOutcome::Deny;   // 客户端disconnected/errored
    }
}

} // namespace agentty::acp
