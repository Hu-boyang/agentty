// plugin.cpp _______________________________________ MCP 插件管理器。 见 plugin.hpp 合同的。
// 编辑功能纯洁 JSON-file 手术(无试验) 终端);
// cli() 是他们的舰体壳。

#include "agentty/tool/plugin.hpp"
#include "agentty/util/user_root.hpp"

#include "agentty/scope/scope.hpp"
#include "agentty/config/inventory.hpp"   // kMcp 核准—— 一个名称
#include "agentty/io/shared_file.hpp"

#include "agentty/util/sendable.hpp"  // JSON 可发送

#include <nlohmann/json.hpp>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;
using nlohmann::json;

namespace agentty::tools::plugin {

namespace {

// 每一个配置 MUTATION (add/remove/toggle )是一(一个) 加载 修改
// 文件共享的存储 -file 节次( io/shared_file.hpp : 车道阻挡了我们自己的
// 车道 工作线程 线程 赛车,文件 锁 停止另一个 agentty (a TUI 财务报告和
// 已审计财务报表 `agentty plugin add` 在另一个 终端 20个同时计量的
// 20个以上 `plugin add` 双对丢失了一个服务器。
[[nodiscard]] persistence::Lane& mutations() {
    static persistence::Lane m;
    return m;
}

// 读条目 `disabled` 标志 无需 丢入一个错误的值。一只手 -edited
// `"disabled": "true"` (弦,而非布尔)使nlohmann的值
// ( "disabled" ,假)抛出 type_error 302  ⁇ ),将坠毁 a
// 切换 或连接环路。处理任何非 -bool 以 "not disabled" (失败)
// -open : 条纹 标志 永远不默默隐藏服务器。
[[nodiscard]] bool entry_disabled(const json& entry) noexcept {
    if (!entry.is_object()) return false;
    auto it = entry.find("disabled");
    return it != entry.end() && it->is_boolean() && it->get<bool>();
}

// 读 + 解析 文件。 区分 "absent"( 新鲜的空文件, ok= true) 从
// "present but broken" (ok=false _____) 中“ 永远不要重写我们无法重写
// 解析 的文件; 一种 typo'd hand-edit 绝不能被 `plugin add` 销毁 ”。
struct Loaded {
    json doc = json::object();
    bool ok  = true;
    bool existed = false;
};

[[nodiscard]] Loaded load(const fs::path& path) {
    Loaded out;
    std::error_code ec;
    if (!fs::exists(path, ec) || ec) return out;
    // exists() 真实的对一个 DIRECTORY ,打开后成功 Linux - 故障
    // 后来作为例外出现, 出现在第一个读数中, Nlohmann的 noexcept
    // 解析 无法捕捉。 `mkdir mcp.json` 用于中止进程
    // `basic_filebuf::underflow error reading the file: Is a directory` ,
    // 则两个名称都没有 agentty 或道路。
    //
    // 当作是 "present but broken" 而不是
    // 缺席: OK=假说是阻止了所有作者 乱打一个它无法读到的文件,
    // 而一个处于这个位置的目录 恰恰是我们不能覆盖
    // 的东西。
    if (!fs::is_regular_file(path, ec)) {
        out.existed = true;
        out.ok = false;
        return out;
    }
    out.existed = true;
    auto bytes = persistence::peek_file(path);
    if (!bytes) { out.ok = false; return out; }
    out.doc = json::parse(*bytes, nullptr, /*投掷=*/false);
    if (!out.doc.is_object()) { out.ok = false; out.doc = json::object(); }
    return out;
}

// 一个SYMLINKed mcp.json(一个点文件仓库)被写到它的目标,
// 所以重命名取代了真实文件,链接存活下来.
[[nodiscard]] fs::path write_target(const fs::path& path) {
    std::error_code ec;
    if (fs::is_symlink(path, ec)) {
        auto real = fs::weakly_canonical(path, ec);
        if (!ec && !real.empty()) return real;
    }
    return path;
}

[[nodiscard]] bool store(const fs::path& path, const json& doc) {
    return persistence::detail::write_in_section(
        write_target(path).string(), doc.dump(2) + '\n',
        {.durability = persistence::Durability::Durable, .keep_mode = true});
}

// 服务器对象密钥: 遵守已有的 "servers" 拼写(桥桥接接受两者),
// 默认为 Claude-Desktop-compatible
// "mcpServers" 用于新文件。
[[nodiscard]] const char* servers_key(const json& doc) {
    if (doc.contains("servers") && doc["servers"].is_object()
        && !doc.contains("mcpServers"))
        return "servers";
    return "mcpServers";
}

} // 命名空间结束。

fs::path config_path(Io, bool project) {
    return config_path(project, project ? fs::path{} : ::agentty::util::user_root());
}

fs::path config_path(bool project, const fs::path& user_root) {
    if (project) return fs::path{".agentty"} / "mcp.json";
    return (user_root.empty() ? fs::path{".agentty"} : user_root) / "mcp.json";
}

namespace {
// 桥面必须和两侧的 kMpPApprovalsLeaf + hashing 完全吻合
// read/write 编号 相同 用户 -root 存储键入 相同 内容散列。
constexpr std::string_view kMcpApprovalsLeaf =
    ::agentty::config::kMcpApprovals;

// 当前项目mcp.json的内容散列,如果没有文件则空.
[[nodiscard]] std::string project_config_hash(Io io) {
    const fs::path cfg = config_path(io, /*项目=*/true);
    // 和我们的原因一样 load() : 存在 -and-opens
    // 和可读性不同,这里的目录会从下面的泥浆中丢出。
    std::error_code ec;
    if (!fs::is_regular_file(cfg, ec)) return {};
    auto bytes = persistence::read_file(io, cfg);
    if (!bytes) return {};
    return scope::content_hash(*bytes);
}
}  // 命名空间结束。

bool is_project_config_trusted(Io io) {
    if (const char* e = std::getenv("AGENTTY_MCP_ALLOW_PROJECT");
        e && (e[0] == '1' || e[0] == 't' || e[0] == 'T'
           || e[0] == 'y' || e[0] == 'Y'))
        return true;
    const std::string h = project_config_hash(io);
    if (h.empty()) return false;
    return scope::load_approvals(kMcpApprovalsLeaf).approved(h);
}

// 一个服务器的产卵-identity hash: 命令 + url + args ,
// 将实际运行. Canonical + 稳定, 因此文件侧(plugin.cpp)
// 和活的侧( 桥连接环) 同意. 空命令 和 url #Q 空散列。
std::string server_spec_hash(const std::string& command,
                             const std::string& url,
                             const std::vector<std::string>& args) {
    if (command.empty() && url.empty()) return {};
    std::string blob;
    blob += "cmd";  blob.push_back('\0'); blob += command; blob.push_back('\0');
    blob += "url";  blob.push_back('\0'); blob += url;
    for (const auto& a : args) { blob.push_back('\0'); blob += a; }
    return scope::content_hash(blob);
}

namespace {
// 适应a JSON 服务器输入字符串 -based 散货
[[nodiscard]] std::string spec_hash_of_entry(const nlohmann::json& entry) {
    if (!entry.is_object()) return {};
    std::vector<std::string> args;
    if (entry.contains("args") && entry["args"].is_array())
        for (const auto& a : entry["args"])
            if (a.is_string()) args.push_back(a.get<std::string>());
    return server_spec_hash(entry.value("command", std::string{}),
                            entry.value("url", std::string{}), args);
}

// 加载 一个服务器条目的 spec hash 从配置文件, 或空的, 如果缺少的话。
[[nodiscard]] std::string server_hash_from_file(const fs::path& path,
                                                const std::string& name) {
    Loaded l = load(path);
    if (!l.ok || !l.existed) return {};
    const char* key = servers_key(l.doc);
    if (!l.doc.contains(key) || !l.doc[key].is_object()
        || !l.doc[key].contains(name))
        return {};
    return spec_hash_of_entry(l.doc[key][name]);
}
}  // 命名空间结束。

bool is_server_trusted(Io io, const fs::path& path, const std::string& name) {
    // 块 授权 第一: 隐藏选择 -in ,然后是整个 -file 批准 ——
    // —— 两者都继续工作,因此在倒退之前,没有哪个方面相信一个配置。
    if (is_project_config_trusted(io)) return true;
    const std::string h = server_hash_from_file(path, name);
    if (h.empty()) return false;   // http/no-command 服务器 QQ ” 此处未锁定
    return scope::load_approvals(kMcpApprovalsLeaf).approved(h);
}

bool approve_project_config(Io io, const lock_levels::below_file_lane& cap) {
    const std::string h = project_config_hash(io);
    if (h.empty()) return false;   // 没有什么要批准的
    scope::Approvals a;
    a.approve(h);
    return scope::save_approvals(io, cap, kMcpApprovalsLeaf, a);   // 合并
}

bool approve_server(Io io, const lock_levels::below_file_lane& cap, const fs::path& path,
                    const std::string& name) {
    const std::string h = server_hash_from_file(path, name);
    if (h.empty()) return false;   // 没有产物可以保证
    scope::Approvals a;
    a.approve(h);
    return scope::save_approvals(io, cap, kMcpApprovalsLeaf, a);   // 合并
}

namespace {
// 一个 加载 编辑 存储周期 在文件的一节。 `edit`
// 获得解析的 doc 并返回无选择的写回, 或生成一个结果
// 不写就停止。 区域将选择 锁 和声链接上 TARGET ,
// 因此通过 a 编辑 链接 编辑真实文件
enum class Need { Any, Existing };
using Edit = std::optional<EditResult>;

template <class F, class... Args>
EditResult mutate(const lock_levels::below_file_lane& cap, const fs::path& path, Need need, F edit,
                  Args... args) {
    (void)edit;   // 无捕捉器:在锁内称为FQQ
    return persistence::with_shared_file(cap, mutations(), write_target(path),
        [](const maya::held<lock_levels::file_lane>&, persistence::Held, std::string p, Need n,
           Args... a) {
            const fs::path target{p};
            Loaded l = load(target);
            if (n == Need::Existing && !l.existed) return EditResult::NotFound;
            if (!l.ok) return EditResult::ParseError;
            if (auto stop = F{}(l.doc, std::move(a)...)) return *stop;
            return store(target, l.doc) ? EditResult::Ok : EditResult::IoError;
        }, path.string(), need, std::move(args)...);
}

// 服务器对象( 如果不存在则创建) , 以及一个命名的条目或无效。
json& servers_of(json& doc) {
    const char* key = servers_key(doc);
    if (!doc.contains(key) || !doc[key].is_object()) doc[key] = json::object();
    return doc[key];
}
json* existing_entry(json& doc, const std::string& name) {
    const char* key = servers_key(doc);
    if (!doc.contains(key) || !doc[key].is_object() || !doc[key].contains(name))
        return nullptr;
    return &doc[key][name];
}
} // 命名空间结束。

EditResult add_server(Io /*io*/, const lock_levels::below_file_lane& cap, const fs::path& path, const ServerSpec& spec,
                      bool force) {
    return mutate(cap, path, Need::Any, [](json& doc, ServerSpec spec, bool force) -> Edit {
    auto& servers = servers_of(doc);
    if (servers.contains(spec.name) && !force)
        return EditResult::AlreadyExists;
    json entry;
    if (spec.type == "passthrough") {
        // 过关: url + 已宣布的工具名称。 没有命令, 没有产卵。
        entry = {{"type", "passthrough"}, {"url", spec.url},
                 {"passthrough", spec.passthrough}};
    } else if (!spec.url.empty()) {
        // HTTP/SSE 服务器: a `url` 传输层 ,没有本地命令。
        // `type` 默认为 "http" 却能遵守明确的 spec.type ("sse").
        entry = {{"type", spec.type.empty() ? "http" : spec.type},
                 {"url", spec.url}};
    } else {
        // `type: stdio` 正在出现的十字 -client 公约》
        // MCP 配置标准提案; Claude 代码 / VS 代码/ 光标标记它。
        // agentty '命令' 命令 -based 添加流量是stdio;
        // 写它明确使文件可移植到更严格的客户端,需要标记.
        entry = {{"type", "stdio"}, {"command", spec.command}};
        if (!spec.args.empty()) entry["args"] = spec.args;
    }
    // 覆盖-in-place(强制)取代条目批发(有文件记载).
    servers[spec.name] = std::move(entry);
    return std::nullopt;
    }, spec, force);
}

EditResult add_server_raw(Io, const lock_levels::below_file_lane& cap, const fs::path& path, const std::string& name,
                          const json& entry, bool force) {
    return mutate(cap, path, Need::Any, [](json& doc, std::string name, json entry, bool force) -> Edit {
    if (!entry.is_object()) return EditResult::ParseError;
    auto& servers = servers_of(doc);
    if (servers.contains(name) && !force)
        return EditResult::AlreadyExists;
    // 横线。源代码所携带的每个密钥 -- env, 头文件, timeoutMs,
    // tools.exclude - 活下来了,因为... 模式定义 同一个
    // agentty 重新构建从 a 创建条目 ServerSpec 失去他们的东西
    servers[name] = std::move(entry);
    return std::nullopt;
    }, name, entry, force);
}

EditResult update_server(Io, const lock_levels::below_file_lane& cap, const fs::path& path, const ServerSpec& spec) {
    return mutate(cap, path, Need::Existing, [](json& doc, ServerSpec spec) -> Edit {
    json* found = existing_entry(doc, spec.name);
    if (!found) return EditResult::NotFound;
    json& entry = *found;
    if (!entry.is_object()) entry = json::object();

    // 只重写 spec 拥有的; 外键( env, ) 头文件, timeoutMs
    // 工具) 留下来 字节-for-byte.
    if (spec.type == "passthrough") {
        entry["type"] = "passthrough";
        entry["url"]  = spec.url;
        if (!spec.passthrough.empty()) entry["passthrough"] = spec.passthrough;
        entry.erase("command");
        entry.erase("args");
    } else if (!spec.url.empty()) {
        entry["type"] = spec.type.empty() ? "http" : spec.type;
        entry["url"]  = spec.url;
        entry.erase("command");
        entry.erase("args");
        entry.erase("passthrough");
    } else {
        entry["type"]    = "stdio";
        entry["command"] = spec.command;
        if (spec.args.empty()) entry.erase("args");
        else                   entry["args"] = spec.args;
        entry.erase("url");
        entry.erase("passthrough");
    }
    return std::nullopt;
    }, spec);
}

EditResult remove_server(Io, const lock_levels::below_file_lane& cap, const fs::path& path, const std::string& name) {
    return mutate(cap, path, Need::Existing, [](json& doc, std::string name) -> Edit {
        if (!existing_entry(doc, name)) return EditResult::NotFound;
        doc[servers_key(doc)].erase(name);
        return std::nullopt;
    }, name);
}

std::vector<ServerSpec> list_servers(const fs::path& path) {
    std::vector<ServerSpec> out;
    Loaded l = load(path);
    if (!l.ok || !l.existed) return out;
    const char* key = servers_key(l.doc);
    if (!l.doc.contains(key) || !l.doc[key].is_object()) return out;
    for (auto& [name, entry] : l.doc[key].items()) {
        ServerSpec s;
        s.name = name;
        if (entry.is_object()) {
            s.command = entry.value("command", std::string{});
            s.url     = entry.value("url", std::string{});
            if (entry.contains("args") && entry["args"].is_array())
                for (const auto& a : entry["args"])
                    if (a.is_string()) s.args.push_back(a.get<std::string>());
        }
        out.push_back(std::move(s));
    }
    return out;
}

EditResult set_server_disabled(Io, const lock_levels::below_file_lane& cap, const fs::path& path, const std::string& name,
                              bool disabled) {
    return mutate(cap, path, Need::Existing, [](json& doc, std::string name, bool disabled) -> Edit {
        json* entry = existing_entry(doc, name);
        if (!entry || !entry->is_object()) return EditResult::NotFound;
        if (entry_disabled(*entry) == disabled) return EditResult::Ok;   // no-op-Ok
        if (disabled) (*entry)["disabled"] = true;
        else          entry->erase("disabled");   // 已启用 \\ (清除)
        return std::nullopt;
    }, name, disabled);
}

bool is_server_disabled(const fs::path& path, const std::string& name) {
    Loaded l = load(path);
    if (!l.existed || !l.ok) return false;
    const char* key = servers_key(l.doc);
    if (!l.doc.contains(key) || !l.doc[key].is_object()
        || !l.doc[key].contains(name))
        return false;
    const json& entry = l.doc[key][name];
    return entry_disabled(entry);
}

EditResult set_tool_enabled(Io, const lock_levels::below_file_lane& cap, const fs::path& path, const std::string& server,
                            const std::string& bare, bool enabled) {
    return mutate(cap, path, Need::Existing,
                  [](json& doc, std::string server, std::string bare, bool enabled) -> Edit {
    json* found = existing_entry(doc, server);
    if (!found || !found->is_object()) return EditResult::NotFound;
    json& entry = *found;

    // tools.exclude 是禁用集。允许删除;禁用添加。
    // 保留任意 tools.include/pin 用户设置。
    json& tools = entry["tools"];
    if (!tools.is_object()) tools = json::object();
    json& excl = tools["exclude"];
    if (!excl.is_array()) excl = json::array();

    // 重建使用或没有 `bare` 的阵列。
    json rebuilt = json::array();
    bool present = false;
    for (const auto& v : excl) {
        if (v.is_string() && v.get<std::string>() == bare) { present = true; continue; }
        rebuilt.push_back(v);
    }
    if (!enabled) rebuilt.push_back(bare);   // 禁用=确保存在
    // 没有 -op 如果已经处于理想状态的话
    const bool changed = enabled ? present : !present;
    if (!changed) return EditResult::Ok;

    excl = std::move(rebuilt);
    if (excl.empty()) tools.erase("exclude");
    if (tools.empty()) entry.erase("tools");
    return std::nullopt;
    }, server, bare, enabled);
}

EditResult set_tool_pinned(Io, const lock_levels::below_file_lane& cap, const fs::path& path,
                           const std::string& server, const std::string& bare, bool pinned) {
    return mutate(cap, path, Need::Existing,
                  [](json& doc, std::string server, std::string bare, bool pinned) -> Edit {
    json* found = existing_entry(doc, server);
    if (!found || !found->is_object()) return EditResult::NotFound;
    json& entry = *found;
    json& tools = entry["tools"];
    if (!tools.is_object()) tools = json::object();
    json& pin = tools["pin"];
    if (!pin.is_array()) pin = json::array();

    json rebuilt = json::array();
    bool present = false;
    for (const auto& v : pin) {
        if (v.is_string() && v.get<std::string>() == bare) { present = true; continue; }
        rebuilt.push_back(v);
    }
    if (pinned) rebuilt.push_back(bare);
    if (pinned == present) {   // 已经处于理想状态
        if (pin.empty()) tools.erase("pin");
        if (tools.empty()) entry.erase("tools");
        return EditResult::Ok;
    }
    pin = std::move(rebuilt);
    if (pin.empty()) tools.erase("pin");
    if (tools.empty()) entry.erase("tools");
    return std::nullopt;
    }, server, bare, pinned);
}

bool is_tool_disabled(const fs::path& path, const std::string& server,
                      const std::string& bare) {
    for (const auto& d : disabled_tools(path, server))
        if (d == bare) return true;
    return false;
}

std::vector<std::string> disabled_tools(const fs::path& path,
                                        const std::string& server) {
    std::vector<std::string> out;
    Loaded l = load(path);
    if (!l.ok || !l.existed) return out;
    const char* key = servers_key(l.doc);
    if (!l.doc.contains(key) || !l.doc[key].is_object()
        || !l.doc[key].contains(server))
        return out;
    const json& entry = l.doc[key][server];
    if (!entry.is_object() || !entry.contains("tools")) return out;
    const json& tools = entry["tools"];
    if (!tools.is_object() || !tools.contains("exclude")) return out;
    const json& excl = tools["exclude"];
    if (!excl.is_array()) return out;
    for (const auto& v : excl)
        if (v.is_string()) out.push_back(v.get<std::string>());
    return out;
}

namespace {

int usage() {
    std::fprintf(stderr,
        "usage: agentty plugin <verb> â€¦   (a plugin IS an MCP server entry)\n"
        "\n"
        "  add <name> --uvx <pypi-pkg> [extra argsâ€¦]\n"
        "        Python plugin from PyPI, run via uv (auto-installs,\n"
        "        isolated env): command = uvx <pkg> â€¦\n"
        "  add <name> --python <script.py> [extra argsâ€¦]\n"
        "        Local Python script: command = python3 <script> â€¦\n"
        "  add <name> --npx <npm-pkg> [extra argsâ€¦]\n"
        "        Node plugin from npm: command = npx -y <pkg> â€¦\n"
        "  add <name> --http <url>   (or --sse <url>)\n"
        "        Remote server over HTTP/SSE â€” a url, no local command.\n"
        "  add <name> -- <command> [argsâ€¦]\n"
        "        Anything else, verbatim argv.\n"
        "\n"
        "  options for add: --project (write ./.agentty/mcp.json instead of\n"
        "        ~/.agentty/mcp.json), --force (overwrite an existing name)\n"
        "\n"
        "  list  [--project]      show configured plugins (\u2713/\u2014 trust marks)\n"
        "  approve <name> --project   trust a project server (per-server)\n"
        "  remove <name> [--project]\n"
        "\n"
        "Connected at startup; restart agentty (or start a new session) to\n"
        "pick up changes. Project-scope stdio servers can spawn arbitrary\n"
        "commands, so they connect only once trusted: approve each one\n"
        "(agentty plugin approve <name> --project, or the Plugins picker), or\n"
        "set AGENTTY_MCP_ALLOW_PROJECT=1 to blanket-trust the whole config.\n");
    return 2;
}

} // 命名空间结束。

int cli(Io io, const maya::unlocked& cap, const std::vector<std::string>& argv) {
    if (argv.empty()) return usage();
    const std::string& verb = argv[0];

    // 常见 标志 ,位置(单位:千美元) -independent 动词之后。
    bool project = false, force = false;
    std::vector<std::string> rest;
    for (std::size_t i = 1; i < argv.size(); ++i) {
        if      (argv[i] == "--project") project = true;
        else if (argv[i] == "--force")   force = true;
        else rest.push_back(argv[i]);
    }
    const fs::path path = config_path(io, project);

    if (verb == "list") {
        auto servers = list_servers(path);
        if (servers.empty()) {
            std::printf("no plugins in %s\n", path.string().c_str());
            return 0;
        }
        std::printf("%s:\n", path.string().c_str());
        for (const auto& s : servers) {
            // 显示 stdio 服务器的命令行, 或 HTTP/SSE
            // 服务器的 URL (没有命令), 因此该行不是空白。
            std::string detail = s.command;
            for (const auto& a : s.args) detail += " " + a;
            if (detail.empty() && !s.url.empty()) detail = s.url;
            // 对于工程配置,请显示每个服务器是否被信任连接(
            // QQ)或已加闸 待处理 批准 ( ) ) 用户配置总是信任的,
            // 所以没有标记。 HTTP/SSE 服务器
            // 没有产生任何结果 也没有门 所以也没有标记
            const char* mark = "";
            if (project && !s.command.empty())
                mark = is_server_trusted(io, path, s.name) ? "\u2713 " : "\u2014 ";
            std::printf("  %s%-16s %s\n", mark, s.name.c_str(), detail.c_str());
        }
        if (project)
            std::printf("\n\u2713 trusted Â· \u2014 pending approval "
                        "(agentty plugin approve <name> --project)\n");
        return 0;
    }

    if (verb == "approve") {
        // 授权 百分比 -server 对工程 stdio 服务器的信任: 记录其
        // spec hash so agentty 只会给人带来结果 --project
        // (用户配置已经信任)。无头等同 选择面板's
        // "trust & enable".
        if (rest.size() != 1) return usage();
        if (!project) {
            std::fprintf(stderr, "approve applies to project configs "
                         "(pass --project); user servers are already trusted\n");
            return 1;
        }
        if (is_server_trusted(io, path, rest[0])) {
            std::printf("'%s' is already trusted\n", rest[0].c_str());
            return 0;
        }
        if (approve_server(io, cap, path, rest[0])) {
            std::printf("approved %s â€” restart agentty to connect it\n",
                        rest[0].c_str());
            return 0;
        }
        std::fprintf(stderr, "could not approve '%s' (no such server, no "
                     "spawnable command, or the approvals store is unwritable)\n",
                     rest[0].c_str());
        return 1;
    }

    if (verb == "remove") {
        if (rest.size() != 1) return usage();
        switch (remove_server(io, cap, path, rest[0])) {
        case EditResult::Ok:
            std::printf("removed %s from %s\n", rest[0].c_str(),
                        path.string().c_str());
            return 0;
        case EditResult::NotFound:
            std::fprintf(stderr, "no plugin named '%s' in %s\n",
                         rest[0].c_str(), path.string().c_str());
            return 1;
        case EditResult::ParseError:
            std::fprintf(stderr, "%s is not valid JSON â€” fix it by hand "
                         "first (refusing to rewrite a broken file)\n",
                         path.string().c_str());
            return 1;
        default:
            std::fprintf(stderr, "could not write %s\n", path.string().c_str());
            return 1;
        }
    }

    if (verb == "add") {
        // 形状 : <name> 那时,一个秘方 标志 (或 减 减 ) `--`).
        if (rest.size() < 2) return usage();
        ServerSpec spec;
        spec.name = rest[0];
        // NAME VALIDATION. 服务器名称成为每个预计工具标识符(
        // `mcp__<server>__<tool>`)和JSON对象键的一半.
        // `__` 里面会让 server/tool 分割模糊;
        // 路径分隔符/ 控制 字节 空或巨名称断开
        // 注册表 , APIs, 或配置文件本身。
        // 提前拒绝可操作消息, 而不是写入破碎的条目。
        {
            const std::string& n = spec.name;
            auto bad = [&](const char* why) {
                std::fprintf(stderr, "invalid plugin name '%s': %s\n"
                             "names: 1-64 chars of [a-zA-Z0-9_-], no '__', "
                             "must start with a letter or digit\n",
                             n.c_str(), why);
                return 1;
            };
            if (n.empty() || n.size() > 64)
                return bad("must be 1-64 characters");
            if (!std::isalnum(static_cast<unsigned char>(n.front())))
                return bad("must start with a letter or digit");
            for (unsigned char c : n)
                if (!std::isalnum(c) && c != '_' && c != '-')
                    return bad("contains a character outside [a-zA-Z0-9_-]");
            if (n.find("__") != std::string::npos)
                return bad("'__' is the server/tool separator in projected "
                           "tool names (mcp__<server>__<tool>)");
        }
        const std::string& recipe = rest[1];
        std::vector<std::string> tail(rest.begin() + 2, rest.end());

        if (recipe == "--uvx") {
            if (tail.empty()) return usage();
            spec.command = "uvx";
            spec.args = std::move(tail);
        } else if (recipe == "--python") {
            if (tail.empty()) return usage();
            spec.command = "python3";
            // 排除脚本路径 : agentty 连接时间的 cwd
            // 是用户从哪个目录发射,而不是运行在哪里 `plugin add`
            // ————”相对的道路会悄悄地打破.
            std::error_code ec;
            fs::path script = fs::absolute(tail[0], ec);
            if (!ec) tail[0] = script.string();
            spec.args = std::move(tail);
        } else if (recipe == "--npx") {
            if (tail.empty()) return usage();
            spec.command = "npx";
            spec.args.push_back("-y");
            for (auto& t : tail) spec.args.push_back(std::move(t));
        } else if (recipe == "--http" || recipe == "--sse") {
            // 远程 传输层 QQ) url, 本地命令( 所以不信任) -gated).
            if (tail.empty()) return usage();
            if (tail[0].rfind("http://", 0) != 0 && tail[0].rfind("https://", 0) != 0) {
                std::fprintf(stderr, "url must start with http:// or https://\n");
                return 1;
            }
            spec.url  = tail[0];
            spec.type = (recipe == "--sse") ? "sse" : "http";
        } else if (recipe == "--") {
            if (tail.empty()) return usage();
            spec.command = tail[0];
            spec.args.assign(tail.begin() + 1, tail.end());
        } else {
            return usage();
        }

        switch (add_server(io, cap, path, spec, force)) {
        case EditResult::Ok: {
            const std::string detail = spec.url.empty()
                ? [&]{ std::string c = spec.command;
                       for (const auto& a : spec.args) c += " " + a;
                       return c; }()
                : spec.url;
            std::printf("added %-16s %s\n  â†’ %s\n", spec.name.c_str(),
                        detail.c_str(), path.string().c_str());
            std::printf("restart agentty to connect (tools appear as "
                        "mcp__%s__<tool>)\n", spec.name.c_str());
            // url服务器产物没有本地命令,所以它从不信任-gated;
            // 只有stdio项目服务器需要批准.
            if (project && spec.url.empty())
                std::printf("note: project servers are untrusted until"
                            " approved â€” run\n      agentty plugin approve %s"
                            " --project\n", spec.name.c_str());
            return 0;
        }
        case EditResult::AlreadyExists:
            std::fprintf(stderr, "'%s' already exists in %s "
                         "(use --force to overwrite)\n",
                         spec.name.c_str(), path.string().c_str());
            return 1;
        case EditResult::ParseError:
            std::fprintf(stderr, "%s is not valid JSON â€” fix it by hand "
                         "first (refusing to rewrite a broken file)\n",
                         path.string().c_str());
            return 1;
        default:
            std::fprintf(stderr, "could not write %s\n", path.string().c_str());
            return 1;
        }
    }

    return usage();
}

} // namespace agentty::tools::plugin
