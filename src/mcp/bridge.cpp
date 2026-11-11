// agentty::mcp —桥：配置→mcp::cap提供者→agentty ToolDefs。
//
// 这是 THIN 适配器。所有协议+功能机制都位于mcp-cpp 子
// 模块的功能层（mcp::cap）中：生成服务器、驱动握手、
// listing/calling工具+资源+提示词、命名空间、
// 分派和*_list_changed 通知。仅限 agentty：(1)
// 读取其配置，(2) 构建 cap::Registry 提供程序，
// 以及 (3) 将 注册表 的工具/资源/提示词 投影到 agentty
// 自己的表面上，以便模型在本地功能旁边看到 MCP 功能。
// 沉重的 mcp-cpp 模板仅限于这个 TU。
//
// 协议覆盖范围（MCP 2025-11-25）：
//   • tools/list + tools/call — 包装为 ToolDefs（始终）
//   ·工具注释 — readOnlyHint/destructiveHint→EffectSet
//   ·结构化+非-text内容——保留在渲染输出中
//   ·resources/list + resources/read —`mcp__resources__read` 工具 + 访问器
//   ·prompts/list + prompts/get —`mcp__prompts__get` 工具 + 访问器
//   · tools/list_changed (+ res/prompts) — 实时 快照 在通知时重建
//   ·分页（nextCursor)-随后在盖层中
//
// 流程：
//   mcp_tools
//     () → 解析配置（.agentty/mcp.json / $AGENTTY_MCP_CONFIG
//     / ~） → 对于每台服务器：通过其 stdio 的 mcp::Connection（同步连接） →
//     cap::Registry 在 + 命名空间冲突中将它们扇动 →
//     将 注册表 tools/resources/prompts 投影到 agentty
//   ToolDefs 注册表（及其实时服务器连接）位于process-wide
//   ConnectionPool 保持活动状态，由 shared_ptr 和 execute() 闭包捕获。

#include "agentty/mcp/client.hpp"
#include "agentty/version.hpp"
#include "agentty/mcp/connection.hpp"
#include "agentty/mcp/http_server.hpp"
#include "agentty/scope/scope.hpp"
#include "agentty/config/inventory.hpp"   // kMcpLayout — 唯一声明
#include "agentty/dirs/dirs.hpp"
#include "agentty/io/http.hpp"
#include "agentty/io/shared_file.hpp"
#include "agentty/tool/plugin.hpp"
#include "agentty/tool/schema.hpp"
#include "agentty/tool/util/fs_helpers.hpp"
#include "agentty/tool/util/utf8.hpp"
#include "agentty/util/dbglog.hpp"
#include "agentty/util/home_dir.hpp"
#include "agentty/util/user_root.hpp"
#include "agentty/util/background.hpp"

#include <maya/runtime.hpp>
#include <mcp/cap/cap.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>
#include "agentty/util/logx.hpp"
#include "agentty/util/lock_levels.hpp"

// mcp::cap::Registry，通读const：每个const
// 成员都遍历提供者列表（在池的构建之后从未写入）并且调用是提供者。
// 这里的提供者是agentty::mcp::Connection (同步：
// 每个服务器有一个守卫门，实时发布）。如此多的 线程 可能会同时读取一个
// 构建的 注册表。 jaal 看不到它：库 类型的私有字段。
MAYA_FROZEN(::mcp::cap::Registry);

namespace agentty::mcp {

namespace fs = std::filesystem;
using json   = nlohmann::json;

// keep-alive 池公共PoolHandle指向：它
// 拥有上限注册表(其拥有提供者shared_ptrs，其拥有产生的
// 服务器进程+传输层)。 互斥量 守护着 分派，因为多个工具 工作线程
// 可能同时进入 调用； 注册表 路由到已经 序列化 自己的 传输
// 层 的每个 -server 提供商。
struct ServerPolicy {
    bool trust_annotations = false;
    int max_output_chars = 30'000;
    std::unordered_set<std::string> include;
    std::unordered_set<std::string> exclude;
    std::unordered_set<std::string> pin;
};

struct ConnectionPool {
    // 外部能力总是被命名的。当启用另一个服务器并且无法
    // 检测到与 agentty 的本机 目录 的冲突时，
    // 条件命名空间会使工具的 传输 身份发生更改。
    //
    // 线程化：池是BUILT，由mcp_tools()调用生成，然后才被发布，
    // 如`const` (current_pool):该类型表示在构建之后没有任何改变。
    // 唯一的例外是 `generation`，它是 list_changed
    // 碰撞的 可变 原子； jaal 的 Sync walk 接受它。
    ::mcp::cap::Registry registry{true};
    mutable std::atomic<unsigned long> generation{0};   // 任何 *_list_changed 上的颠簸
    std::unordered_map<std::string, ServerPolicy> policies;
    // 为什么配置的服务器NOT连接此会话（空条目=连接良好）。在构建期间填充，
    // 以便 plugin_model() 可以向用户显示 "connected"
    // 与 "failed: <reason>"。
    std::unordered_map<std::string, std::string> connect_errors;
};

namespace {

// ── 进程 -wide 池 ─────────────────────────────────────────────────────
// mcp_tools()将池存储在这里，
// 因此resource/prompt/live访问器可以到达相同的连接，而无需将句柄线程穿过每个调用者。
// 实时池：整体构建，然后发布。任何线程上的读取器都会获取
// 当前句柄并使其保持活动状态以供调用；重建会发布一个新池，
// 但不会释放其下的旧池。 ConnectionPool 一旦
// 发布就可以安全地共享（请参阅其线程注释）。
maya::published<const ConnectionPool>& g_pool() {
    static maya::published<const ConnectionPool> p;
    return p;
}

// 两个调用者到达mcp_tools()：connect_initial_mcp
// ()(第一目录访问，由注册表的connect_phase原子声明)和
// mcp_reload()(由注册表的重载状态字声明)。这些是单独的声明，
// 因此如果没有 g_connecting()，两者都可以立即构建：完整
// 的服务器集生成两次，并且正在读取变异 池 plugin_model
// () 的后期构建。连接很少，因此序列化它们是免费的。
// 池被重建替换。摧毁它会停止其服务器（加入它们的 对端），
// 这在 g_connecting() 下绝不能发生；它在这里等待，
// mcp_tools() 在门后将其丢弃。
maya::published<const ConnectionPool>& g_retired() {
    static maya::published<const ConnectionPool> r;
    return r;
}

// Single-flight用于WHOLE connect-and-swap：没有两个构建同时运行。
// 不是 jaal 锁：从工具 注册表 的 惰性 构建到达
// mcp_tools()，该工具有数十个调用者且没有能力，
// 并且构建本身会阻塞几秒钟（生成服务器、RPC 握手）。
// 失败者等待的门（atomic::wait）没有任何内容，
// 因此它不可能是锁-order周期的一半；构建从不等待服务员拿着的任何东西。
std::atomic<bool>& g_connecting() {
    static std::atomic<bool> c{false};
    return c;
}

PoolHandle current_pool() {
    return g_pool().current();
}

std::chrono::milliseconds call_timeout() {
    long ms = 60'000;
    if (const char* e = std::getenv("AGENTTY_MCP_TIMEOUT_MS"); e && e[0]) {
        try { long v = std::stol(e); if (v > 0) ms = v; }
        catch (const std::exception& ex) { util::dbglog("mcp.call_timeout.env", ex.what()); }
        catch (...) { util::dbglog("mcp.call_timeout.env", "non-std exception"); }
    }
    return std::chrono::milliseconds{ms};
}

// NOTE:resolve_config()是GONE。
//
// 这是第二个梯子——环境 ▷ 项目 ▷ 用户，ONE获胜文件——位于
// scope::plan 梯子 read_config_servers
// () 使用的旁边。对一个问题有两个答案，但他们不同意：plan()
// MERGES项目+用户，因此插件面板、`agentty config mcp`
// 和委托都列出了两者，而连接路径解析了单个文件，并在项目mcp.json
// 存在时默默地删除了每个用户服务器。你看到了你的服务器；它从来没有跑
// 过。
//
// 现在一切都是归并 read_config_servers()，
// 即scope::plan + 并集幺半群。范围 头文件 自己的序言
// 将此函数命名为它所替换的东西；至此，迁移完成。

// 批准将叶子存储在~/.agentty (scope::load/save_approvals)下。
inline constexpr std::string_view kMcpApprovalsLeaf =
    ::agentty::config::kMcpApprovals;

// WORKSPACE-LOCAL 配置可以在该会话中生成其（stdio）服务器吗？
// 项目 mcp.json 骑在克隆上，并且可以在没有 per-tool
// 提示词 的情况下产生任意命令，因此在人类为其担保之前，它保持不受信任。
// 授权信任的两种方式，都保留在这里：
//   1.AGENTTY_MCP_ALLOW_PROJECT=1——原始粗环境opt-in(back-compat)。
//   2.文件的CONTENT HASH在用户-root批准存储中——
//      MCPoison-safe路径：批准THIS确切文件；
//      编辑其 字节（交换命令）并且哈希发生更改，因此批准不再匹配并且它
//      重新-gates。批准在 ~/.agentty 下生效，克隆无法访问。
// Non-project配置($AGENTTY_MCP_CONFIG/~/.agentty)始终是可信的。
[[nodiscard]] bool env_allows_project() noexcept {
    const char* e = std::getenv("AGENTTY_MCP_ALLOW_PROJECT");
    return e && (e[0] == '1' || e[0] == 't' || e[0] == 'T'
              || e[0] == 'y' || e[0] == 'Y');
}

// 从 `src` 读取的 `cfg` 处的配置是否可信生成？
//
// LOCUS的决定是scope::trust_of的，而不是我们的。
// 该函数是一个知道 Explicit/User 可信的地方，
// 因为当 Project/Local 骑在克隆上时，人类将它们放置在一个
// 地方——这曾经是一个 `bool project_local` 线程，
// 通过四个函数加上一个方便的重载，丢弃了答案（`bool ignore`），
// 当 plan() 已经返回它时，它的出处被严重重建。
//
// env opt-in 保留为 back-compat 的 pre-check，
// 仅此而已：它是一种钝器（一个变量永远信任 每个 项目配置，
// 包括您十分钟前克隆的 仓库），并且下面的 content-hash
// 路径是在编辑时实际 re-gates 的路径。
[[nodiscard]] bool source_trusted(const scope::Source& src,
                                  const fs::path& cfg) noexcept {
    if (env_allows_project()) return true;
    std::error_code ec;
    std::string bytes;
    // 目录打开成功并在读取时抛出异常，因此首先检查KIND。
    // 这里将 字节 留空也是安全的答案：下面的守卫
    // 拒绝将空哈希交给 trust_of 作为项目源。
    if (fs::is_regular_file(cfg, ec)) {
        std::ifstream in(cfg, std::ios::binary);
        if (in) bytes.assign((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    }
    // 空散列将使trust_of回答受信任（它读取"nothing to bind to")，
    // 因此不可读的项目文件一定不能到达它。
    if (bytes.empty() && src.locus != scope::Locus::Explicit
                      && src.locus != scope::Locus::User)
        return false;
    const scope::Trust t = scope::trust_of(
        src, scope::content_hash(bytes),
        scope::load_approvals(kMcpApprovalsLeaf));
    return std::holds_alternative<scope::Trusted>(t);
}

// 解析模型需要的每-server配置：命令+排除集。
// 一个地方写着mcp.json，因此每个消费者(投影、模型、
// 选择面板)都同意相同的字节。 Missing/invalid 配置 → 空地图。
//
// 现在项目+用户配置的UNION（通过agentty::scope），
// 没有一个获胜文件：项目服务器和用户服务器是BOTH可见的，
// 每个都标记有它来自的scope::Source，因此编辑器可以写回
// 正确的文件（Source::base）并且信任门可以看到其轨迹。
struct ConfigServer {
    std::string command;
    std::string url;             // HTTP/SSE 传输层（无命令）
    std::string type;            // "stdio" | "http" | "sse" | "passthrough" | "" (推断的)
    std::vector<std::string> args;   // stdio 命令参数（生成标识的一部分）
    std::unordered_set<std::string> exclude;
    std::unordered_set<std::string> pin;     // tools.pin:总是已加载(#98)
    bool disabled = false;
    scope::Source source;        // WHERE 该条目是从（出处）读取的
    // type == "passthrough":user-declared 外部
    // 工具（模型端点前面的 proxy/gateway 通告了它们；我们通过将参数
    // POSTing 到 `url` 来执行）。每个条目：名称[+描述][+广告]。
    struct PassthroughTool {
        std::string name;
        std::string description;
        bool advertise = false;  // 默认：代理拥有模式定义
    };
    std::vector<PassthroughTool> passthrough;

    // 该条目的原始JSON，以及它来自的文件。
    //
    // 因此CONNECT路径可以使用此合并图来代替re
    // -reading一个获胜文件。之前，
    // read_config_servers()（通过scope::
    // plan）合并了项目+用户，而mcp_tools()独立解析了ONE
    // 文件——两个梯子，他们不同意：项目mcp.json默默地禁用了每个用户服务器，
    // 而插件面板和`agentty config mcp`仍然列出了它们。
    // 将规范保留在这里意味着只需读取一次配置即可得到一个答案。
    json       spec;
    fs::path   file;     // 读取此条目的具体 mcp.json
};

// NOTE:json_flag()随第二梯子一起消失。它的存在是因为连接循环 re-parsed
// 原始 JSON 并需要一个 throw-safe `disabled` 读取；
// 该循环现在消耗 ConfigServer，其中 read_one_config 已经完成
// 了 type-checked 解析 一次。

// MCP配置的范围布局。在 config/inventory.hpp 中
// 声明 一次 并在此处使用，因此 `agentty config mcp`
// 打印与此相同的梯形图 - 没有第二个定义偏离它。
[[nodiscard]] const scope::Layout& mcp_layout() noexcept {
    return ::agentty::config::kMcpLayout;
}

// 解析一个mcp.json文件的服务器条目进入`out`，用`src`标记每个。
// First-writer-wins 影子：已存在的服务器名称（来自更高的 -precedence
// 源）被 不 覆盖 — 项目影子用户，与 skills/agents/commands.
// Missing/invalid 文件完全相同 → no-op。
void read_one_doc(const json& doc, const scope::Source& src, const fs::path& file,
                  std::unordered_map<std::string, ConfigServer>& out);

void read_one_config(const fs::path& file, const scope::Source& src,
                     std::unordered_map<std::string, ConfigServer>& out) {
    if (file.empty()) return;
    // is_regular_file，不只是成功开启。打开 DIRECTORY 在
    // Linux 上成功——失败稍后到达，作为第一次读取的异常，并且 nlohmann 的
    // noexcept 解析 没有捕获它。因此，`mkdir .agentty/mcp.json`
    // 使用
    // `basic_filebuf::underflow error reading the file: Is a directory`
    // 中止了整个代理，该代理既没有命名 agentty，也没有命名执行此操作的路径。
    //
    // 配置存在于用户（以及其他工具和脚本）可以放置任何内容的地方，
    // 因此 "not the kind of thing I expected"
    // 必须是 no-op 而不是崩溃——这里的所有其他格式错误的情况都已经是这样。
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) return;
    std::ifstream in(file);
    if (!in) return;
    json doc = json::parse(in, nullptr, /*投掷=*/false);
    read_one_doc(doc, src, file, out);
}

// MCP服务器以mcp.json形状发送ACP客户端和session/new
// (Zed的上下文服务器，JetBrains'MCP设置)。 Process
// -wide：一名编辑器驱动一个代理进程。序列化对象的 快照：替换为整体，
// 并从 (capability-free) 构建运行的配置步行中读取。
maya::snapshot<std::string>& client_servers() {
    static maya::snapshot<std::string> s{std::string{"{}"}};
    return s;
}
json client_servers_json() {
    try { return json::parse(*client_servers().load()); } catch (...) { return json::object(); }
}

void read_one_doc(const json& doc, const scope::Source& src, const fs::path& file,
                  std::unordered_map<std::string, ConfigServer>& out) {
    if (!doc.is_object()) return;
    const json* servers = nullptr;
    if (doc.contains("mcpServers") && doc["mcpServers"].is_object())
        servers = &doc["mcpServers"];
    else if (doc.contains("servers") && doc["servers"].is_object())
        servers = &doc["servers"];
    if (!servers) return;
    for (auto it = servers->begin(); it != servers->end(); ++it) {
        if (out.count(it.key())) continue;   // 被更高的-precedence源遮蔽
        const json& e = it.value();
        if (!e.is_object()) continue;
        ConfigServer cs;
        cs.command = e.value("command", std::string{});
        cs.url     = e.value("url", std::string{});   // HTTP/SSE服务器没有命令
        cs.type    = e.value("type", std::string{});
        cs.source  = src;
        cs.spec    = e;      // 连接路径由此产生
        cs.file    = file;   // 和 reports/approves 针对此文件
        if (e.contains("args") && e["args"].is_array())
            for (const auto& a : e["args"])
                if (a.is_string()) cs.args.push_back(a.get<std::string>());
        // 手-edited非-bool `disabled` (e.g.字符串"true")
        // 不得抛出type_error并记下整个配置读取——将任何非-bool视为
        // "not disabled"。
        if (auto dit = e.find("disabled");
            dit != e.end() && dit->is_boolean())
            cs.disabled = dit->get<bool>();
        if (e.contains("tools") && e["tools"].is_object()) {
            const json& tj = e["tools"];
            if (tj.contains("exclude") && tj["exclude"].is_array())
                for (const auto& v : tj["exclude"])
                    if (v.is_string()) cs.exclude.insert(v.get<std::string>());
            if (tj.contains("pin") && tj["pin"].is_array())
                for (const auto& v : tj["pin"])
                    if (v.is_string()) cs.pin.insert(v.get<std::string>());
        }
        // type:"passthrough"——声明的国外工具。
        // 条目可以是裸字符串（仅名称）或对象{名称，描述？，广告？}。
        // 格式错误的条目被跳过，绝不是致命的(与上面的`disabled`
        // 相同的fail-open姿势)。
        if (e.contains("passthrough") && e["passthrough"].is_array()) {
            for (const auto& pt : e["passthrough"]) {
                ConfigServer::PassthroughTool t;
                if (pt.is_string()) {
                    t.name = pt.get<std::string>();
                } else if (pt.is_object()) {
                    t.name        = pt.value("name", std::string{});
                    t.description = pt.value("description", std::string{});
                    if (auto ad = pt.find("advertise");
                        ad != pt.end() && ad->is_boolean())
                        t.advertise = ad->get<bool>();
                }
                if (!t.name.empty()) cs.passthrough.push_back(std::move(t));
            }
            if (cs.type.empty() && !cs.passthrough.empty())
                cs.type = "passthrough";
        }
        out.emplace(it.key(), std::move(cs));
    }
}

[[nodiscard]] std::unordered_map<std::string, ConfigServer>
read_config_servers() {
    std::unordered_map<std::string, ConfigServer> out;
    // 编辑器交给我们的服务器首先是ACP：用户在他们的编辑
    // 器中设置它们，这是比我们找到的文件更强有力的声明。
    {
        const json extra = client_servers_json();
        if (extra.is_object() && !extra.empty()) {
            scope::Source src;
            src.locus = scope::Locus::Explicit;   // 值得信赖：编辑担保
            json doc{{"mcpServers", extra}};
            read_one_doc(doc, src, "(editor)", out);
        }
    }
    const scope::Env env = scope::current_env(mcp_layout());
    // scope::plan按优先顺序产生源，并且mcp_layout()请求本机
    // 方言ONLY（mcp.json不是.agents/.claude约定），
    // 因此这里的每个源都是我们读取的源。优先级较高的第一个 = read_one_config
    // 中的第一个 -writer-wins 影子。
    //
    // 这用于将-filter与`if (src.dialect != Agentty) continue;`
    // 重新组合，因为plan()为每个人发出了所有三种方言。
    // 这使得梯子成为一个谎言：计划报告了六个来源，其中三个已被阅读。
    // 现在，宽度位于布局上，因此计划 IS 的行为。
    for (const scope::Source& src : scope::plan(mcp_layout(), env)) {
        // 显式源的基础是文件的父级；它的叶子是确切的 env-named 文件。
        // Project/User 将标准叶片连接到底座上。
        const fs::path file = (src.locus == scope::Locus::Explicit && env.explicit_config)
            ? *env.explicit_config
            : src.base / "mcp.json";
        read_one_config(file, src, out);
    }
    return out;
}

// 从一个服务器配置条目构建上限提供者。如果发生任何失败，则返回 nullptr
// （和日志），以便调用者可以跳过它。
//
// STDIO 传输层将服务器产生为子进程。每个平台都支持此功能：POSIX 通过 fork/exec，
// Windows 通过 CreateProcess (util::ChildProcess)。 HTTP/SSE 服务器采用
// make_http_provider 路径。
std::shared_ptr<::mcp::cap::CapabilityProvider>
make_provider(const std::string& name, const json& spec) {
    const std::string command = spec.value("command", std::string{});
    if (command.empty()) {
        std::fprintf(stderr, "mcp: server '%s' has no \"command\"\n", name.c_str());
        return nullptr;
    }
    // ── 预览 -flight : 解决命令 之前 用于执行 ────────────
    // 丢失的二进制程序否则会在子项深处失败(execvp->
    // "mcp::cap: exec '…' failed: No such file or directory")，（MCP 命令执行失败：文件或目录不存在。）
    // 其stderr可以渗入TUI并且其故障在每次连接时都会重复。
    // 预先解决它，如果不存在，则跳过带有 一个 明确行的服务器
    // - 没有 fork，没有 exec，没有 终端 损坏。
    {
        const bool has_slash = command.find('/') != std::string::npos
#ifdef _WIN32
                            || command.find('\\') != std::string::npos
#endif
            ;
        bool resolvable = false;
        std::error_code ec;
        if (has_slash) {
            // 显式路径（绝对或相对）：必须存在+是一个文件。
            resolvable = fs::exists(command, ec) && !fs::is_directory(command, ec);
        } else {
            // 裸名：搜索PATH，就像execvp一样。
            if (const char* path = std::getenv("PATH"); path && *path) {
                std::string p(path);
                std::size_t start = 0;
                while (start <= p.size() && !resolvable) {
                    std::size_t sep = p.find(':', start);
                    std::string dir = p.substr(start,
                        sep == std::string::npos ? std::string::npos : sep - start);
                    if (!dir.empty()) {
                        fs::path cand = fs::path{dir} / command;
                        if (fs::exists(cand, ec) && !fs::is_directory(cand, ec))
                            resolvable = true;
                    }
                    if (sep == std::string::npos) break;
                    start = sep + 1;
                }
            }
        }
        if (!resolvable) {
            std::fprintf(stderr,
                "mcp: server '%s' skipped — command not found: %s\n"
                "     (check the path in mcp.json; use an ABSOLUTE path for a "
                "local build)\n",
                name.c_str(), command.c_str());
            return nullptr;
        }
    }
    ConnectionConfig cfg;
    cfg.name = name;
    ::agentty::util::ChildProcess::Spawn spawn;
    spawn.command = command;
    // 强制非-string args/env元素而不是抛出：用于使
    // `.get<std::string>()`抛出json::
    // type_error并跳过WHOLE服务器的单个numeric/bool元素。
    // 字符串逐字传递；其他任何内容都通过 dump() 进行字符串化，因此 e.g。
    // `"args": ["--port", 8080]`按照用户明确的意图工作。
    auto as_str = [](const json& v) {
        return v.is_string() ? v.get<std::string>() : v.dump();
    };
    if (spec.contains("args") && spec["args"].is_array())
        for (const auto& a : spec["args"]) spawn.args.push_back(as_str(a));
    if (spec.contains("env") && spec["env"].is_object())
        for (auto it = spec["env"].begin(); it != spec["env"].end(); ++it)
            spawn.env_kv.push_back(it.key() + "=" + as_str(it.value()));
    if (auto c = spec.find("cwd"); c != spec.end() && c->is_string())
        spawn.cwd = c->get<std::string>();
    cfg.client_info  = ::mcp::Implementation{"agentty", ::agentty::version()};
    cfg.call_timeout = call_timeout();
    if (const auto ms = spec.value("timeoutMs", 0L); ms > 0)
        cfg.call_timeout = std::chrono::milliseconds{ms};
    if (const auto ms = spec.value("connectTimeoutMs", 0L); ms > 0)
        cfg.handshake_timeout = std::chrono::milliseconds{ms};

    const std::string root = tools::util::workspace_root().generic_string();
    if (!root.empty()) {
        std::string uri = "file://" + root;
#ifdef _WIN32
        if (root.size() > 1 && root[1] == ':') uri = "file:///" + root;
#endif
        cfg.roots.push_back(
            ::mcp::Root{std::move(uri), std::string{"workspace"}, json::object()});
    }

    try {
        return std::make_shared<Connection>(std::move(cfg), stdio_link(std::move(spawn)));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "mcp: server '%s' failed: %s\n", name.c_str(), e.what());
        return nullptr;
    } catch (...) {
        std::fprintf(stderr, "mcp: server '%s' failed (unknown)\n", name.c_str());
        return nullptr;
    }
}

// ── 工具说明中的副作用 ─────────────────────────────────────────
// 远程工具的注释告诉我们它有多么危险。 MCP 规范 (
// Tool.annotations) 明确指出这些是来自服务器的 UNTRUSTED
// HINTS，因此我们比较保守：当工具肯定地说 readOnlyHint:true
// 和 确实 不 也声称具有破坏性时，该工具将被视为 read-only 仅。
// 其他一切都获得完整的 副作用 集（总是询问
// 权限）——原始桥无条件应用的安全默认值。
tools::EffectSet effects_for(const ::mcp::Tool& t, bool trust_annotations) {
    using tools::Effect;
    const tools::EffectSet full{Effect::Exec, Effect::WriteFs, Effect::Net, Effect::ReadFs};
    // 注释是不可信服务器提示。只有明确信任的服务器才可以使用它来减少
    // permission/scheduling 副作用。
    if (!trust_annotations || !t.annotations.has_value()) return full;
    const auto& a = *t.annotations;
    const bool read_only   = a.readOnlyHint.has_value()    && *a.readOnlyHint;
    const bool destructive = a.destructiveHint.has_value() && *a.destructiveHint;
    if (read_only && !destructive) {
        // Read-only远程工具：它观察世界但不变异它。
        // ReadFs|Net，在Ask和Minimal下仍然是PROMPTS，
        // 因为Net在集合中——调用的参数以任何方式离开该过程。
        // （此处较早的评论将这些称为 "the two permission-free effects"，
        // 并表示该策略将它们视为 read/grep.，但它不是：
        // policy.hpp 提示词 for Net under Ask。
        // 代码始终是保守的东西；只有评论是错误的。）
        return tools::EffectSet{Effect::ReadFs, Effect::Net};
    }
    return full;
}

// ── 渲染 a cap::Result 输入 agentty 工具文本 ───────────────────────────
// 覆盖层已经将文本块展平为r.text，并且保留了r.raw+
// r.structured中的structuredContent中的原始内容块。
// 我们显示它的 全部：逐字文本，然后是模型无法看到的任何非
// -text 块 (image/audio/resource)
// 的紧凑摘要，然后是存在的结构化 JSON。这就是使结构化+多
// 模式 MCP 结果可用而不是默默丢弃的原因。
std::string render_result(const ::mcp::cap::Result& r) {
    std::string out = r.text;

    // Non-text内容块(image/audio/embedded资源)。 r.text
    // 已经为每个 via result_from_call 包含了一个 -line
    // "[type] uri" 存根，因此这里我们只附加模型受益的更丰富的细节：mime + 大小。
    if (r.raw.is_array()) {
        for (const auto& b : r.raw) {
            if (!b.is_object()) continue;
            const auto type = b.value("type", std::string{});
            if (type == "image" || type == "audio") {
                const auto mime = b.value("mimeType", std::string{});
                std::size_t bytes = 0;
                if (auto it = b.find("data"); it != b.end() && it->is_string())
                    bytes = it->get<std::string>().size();   // base64长度
                if (out.empty() || out.back() != '\n') out += '\n';
                out += "[" + type + (mime.empty() ? "" : " " + mime) + ", ~" +
                       std::to_string(bytes) + "B base64]\n";
            }
        }
    }

    // 结构化输出(MCP structuredContent)-与人类文本一起
    // 的带类型的 载荷。将其作为围栏 JSON 块发出，以便模型可以对其进行 解析。
    if (r.structured.is_object() && !r.structured.empty()) {
        if (!out.empty() && out.back() != '\n') out += '\n';
        out += "```json\n" + r.structured.dump(2) + "\n```\n";
    } else if (!r.structured.is_object() && !r.structured.is_null()) {
        if (!out.empty() && out.back() != '\n') out += '\n';
        out += "```json\n" + r.structured.dump(2) + "\n```\n";
    }
    // TRUST BOUNDARY。 MCP 服务器是任意
    // 外部进程；其 字节 进入模型读取的对话，TUI 渲染。
    // 剥离终端控制序列（
    // malicious/buggy服务器否则可以通过转录本将cursor-moves/OSC注入到终端中）
    // 并强制有效UTF-8（下一轮提供者API的无效序列
    // 400——毒害整个会话，而不仅仅是这个调用）。
    return tools::util::to_valid_utf8(
        tools::util::strip_terminal_controls(out));
}

std::string canonical_mcp_name(std::string_view exposed) {
    // 当 always_namespace = true 时，注册表 名称是 `mcp:<server>__<tool>`。
    // Provider API仅接受保守的标识符字母表，
    // 因此将标点符号映射到下划线并仅在execute()中保留原始路径。
    if (exposed.starts_with("mcp:")) exposed.remove_prefix(4);
    std::string out = "mcp__";
    out.reserve(out.size() + exposed.size());
    for (unsigned char c : exposed)
        out.push_back((std::isalnum(c) || c == '_' || c == '-')
                          ? static_cast<char>(c)
                          : '_');
    constexpr std::size_t kMaxToolName = 128;
    if (out.size() > kMaxToolName) out.resize(kMaxToolName);
    return out;
}

std::string mcp_origin_id(std::string_view exposed) {
    if (exposed.starts_with("mcp:")) exposed.remove_prefix(4);
    if (auto split = exposed.find("__"); split != std::string_view::npos)
        exposed = exposed.substr(0, split);
    return std::string{exposed};
}

std::string mcp_bare_name(std::string_view exposed) {
    if (exposed.starts_with("mcp:")) exposed.remove_prefix(4);
    if (auto split = exposed.find("__"); split != std::string_view::npos)
        exposed.remove_prefix(split + 2);
    return std::string{exposed};
}

// 名称 提示词 是 带类型的，如：`server:prompt`，来自 传输 形式（
// `mcp:server__prompt` 或规范的 `mcp__server__prompt`）。
std::string short_prompt_name(std::string_view exposed) {
    if (exposed.starts_with("mcp:")) exposed.remove_prefix(4);
    else if (exposed.starts_with("mcp__")) exposed.remove_prefix(5);
    const auto split = exposed.find("__");
    if (split == std::string_view::npos) return std::string{exposed};
    return std::string{exposed.substr(0, split)} + ":" + std::string{exposed.substr(split + 2)};
}

// 超出工具限制的结果是文件的已保存，并由预览加路径替换，
// 因此模型可以用offset/limit对其余部分进行`read`，
// 而不是将中间部分丢失到头部+末尾切割（Claude代码在25k标记处执行此操作）。
// Content-named，所以相同的结果重复使用一个文件。
std::string spill_if_large(std::string text, std::size_t limit, std::string_view route) {
    if (limit == 0 || text.size() <= limit) return text;
    std::error_code ec;
    // 项目自己的.agentty/(self-gitignored)
    // 下：在工作区里面，所以read、grep和沙盒shell都可以到达它。
    // 当没有项目目录时，回退到用户缓存。
    fs::path dir;
    if (auto p = ::agentty::dirs::resolve(::agentty::dirs::Spec{
            .root = ::agentty::dirs::Root::Project, .leaf = "tool-results"}))
        dir = p->path;
    else
        dir = ::agentty::util::user_cache_dir() / "tool-results";
    fs::create_directories(dir, ec);
    char name[48];
    std::snprintf(name, sizeof(name), "mcp-%016llx.txt",
                  static_cast<unsigned long long>(std::hash<std::string>{}(text)));
    const fs::path file = dir / name;
    bool saved = false;
    if (!ec)
        saved = persistence::write_atomic(::agentty::IoAccess::grant(), file, text,
                                          {.durability = persistence::Durability::Atomic});
    if (saved) tools::util::allow_read_root(::agentty::IoAccess::grant(), file);
    constexpr std::size_t kHead = 6'000, kTail = 2'000;
    const std::size_t head = tools::util::safe_utf8_cut(text, kHead);
    std::string tail = text.substr(text.size() - std::min(kTail, text.size() - head));
    std::string out = "<persisted-output>\n";
    out += "The result from " + std::string{route} + " is " + std::to_string(text.size() / 1024)
         + " KB, over this tool's " + std::to_string(limit / 1024) + " KB limit. ";
    out += saved ? "The full result is saved to: " + file.string()
                       + "\nUse the read tool on that path with offset/limit for the rest.\n\n"
                 : "It could not be saved, so only a preview follows.\n\n";
    out += "--- first " + std::to_string(head / 1024) + " KB ---\n" + text.substr(0, head)
         + "\n...\n--- last " + std::to_string(tail.size() / 1024 + 1) + " KB ---\n" + tail
         + "\n</persisted-output>";
    return out;
}

// 合成通过EXPOSED名称路由通过共享注册表的ToolDef。
tools::ToolDef make_tool(PoolHandle pool, const ::mcp::Tool& t,
                         const ServerPolicy& policy,
                         std::unordered_set<std::string>& taken_names) {
    tools::ToolDef def;
    const std::string exposed = t.name;
    // 每个提供者采用的名称：[A-Za-z0-9_-]，最多64个，唯一的，稳定的。
    def.name = ToolName{tools::schema::wire_name(mcp_origin_id(exposed), mcp_bare_name(exposed),
                                                 taken_names)};
    def.origin = tools::ToolOrigin::Mcp;
    def.origin_id = mcp_origin_id(exposed);
    def.origin_tool = mcp_bare_name(exposed);

    std::string desc = t.description.has_value() ? *t.description : std::string{};
    // 信任边界：DESCRIPTION位于每个回合的系统
    // -side 载荷的工具列表中——敌对服务器可以在那里
    // 注入控制字节或文本墙。擦洗 + 盖住它（有用的描述适合
    // 1 KiB；无论如何，全文从未呈现）。
    desc = tools::util::to_valid_utf8(tools::util::strip_terminal_controls(desc));
    if (desc.size() > 1024) {
        desc.resize(1024);
        desc = tools::util::to_valid_utf8(std::move(desc));   // re-seal 切割边缘
        desc += "…";
    }
    def.description = "[MCP " + def.origin_id + "] " +
        (desc.empty() ? ("Remote tool '" + def.name.value + "'.") : desc);

    json schema = ::mcp::to_json(t.inputSchema);
    if (!schema.is_object()) schema = json::object();
    // 对于每个工具，提供商将拒绝 模式定义 失败的 整个 请求。
    // 首先检查：损坏的工具不会被请求（并说明原因），
    // 并且服务器的其余部分继续工作。
    if (auto why = tools::schema::check(schema); !why.empty()) {
        def.skipped = why;
        def.advertise = false;
        def.input_schema = json{{"type", "object"}, {"properties", json::object()}};
    } else {
        // 修复仅拼写问题：根联合、$ref、类型数组、大小。
        auto n = tools::schema::normalize(std::move(schema));
        def.input_schema = std::move(n.schema);
        if (!n.note.empty()) def.description += " " + n.note;
    }

    def.effects = effects_for(t, policy.trust_annotations);
    def.scheduling_effects = def.effects;
    // 工具可以声明更大的限制（Claude代码约定），e.g。
    // DB 模式定义 转储本质上很大。超过限制，文件的结果是 已保存，
    // 并且模型获取路径（请参阅下面的溢出），而不是有损剪切。
    int limit = std::clamp(policy.max_output_chars, 2'000, 100'000);
    if (auto m = t.meta.find("anthropic/maxResultSizeChars");
        m != t.meta.end() && m->is_number_integer())
        limit = std::clamp(static_cast<int>(m->get<long long>()), 2'000, 500'000);
    def.max_output_chars = 0;   // 封闭件本身应用限制，具有溢出
    def.always_expose = policy.pin.contains(mcp_bare_name(exposed));
    def.output_truncation = tools::OutputTruncation::HeadTail;

    def.execute = [pool, exposed, limit](const maya::unlocked&, const json& args,
                                         const tools::CallContext& ctx) -> tools::ExecResult {
        try {
            ::mcp::cap::Result r;
            // 注册表 分派解析提供商通过自身生成
            // -safe路由快照；独立的服务器可以并行运行，
            // 每个提供商仅序列化自己的 传输层。
            ::mcp::cap::Request req{exposed, args, ctx.progress,
                                    [&ctx] { return ctx.cancelled(); }};
            req.elicit = ctx.elicit;   // 服务器可能询问用户mid-call
            // 连接在 req.stop 取消，而不是在轮询的 回调 取消。
            const auto merged = ctx.merged_stop();
            req.stop = merged->get_token();
            r = pool->registry.dispatch(req);
            if (r.is_error)
                return std::unexpected(tools::ToolError::subprocess(
                    r.text.empty() ? "MCP tool reported an error"
                                   : tools::util::to_valid_utf8(
                                         tools::util::strip_terminal_controls(r.text))));
            std::string text = render_result(r);
            if (text.empty()) text = "(no output)";
            return tools::ToolOutput{spill_if_large(std::move(text), static_cast<std::size_t>(limit), exposed), std::nullopt};
        } catch (const std::exception& e) {
            return std::unexpected(tools::ToolError::subprocess(
                std::string{"MCP call failed: "} + e.what()));
        } catch (...) {
            return std::unexpected(tools::ToolError::subprocess("MCP call failed"));
        }
    };
    return def;
}

// ── 已建 -in resource/prompt 访问工具 ─────────────────────────────────
// 当任何连接的服务器公开资源或提示词时，我们为每个服务器添加
// ONE通用工具，以便模型可以列出+读取资源/渲染 提示词，
// 而不需要针对URI的工具。它们经过相同的 池。

tools::ToolDef make_read_resource_tool(PoolHandle pool) {
    tools::ToolDef def;
    // 不是`mcp_read_resource`:Anthropic的事件订阅端点400s
    // 每个请求携带名为`mcp_`+字母("out of extra usage")的工具。
    // 每个MCP工具使用的`mcp__server__tool`形状都很好。
    def.name        = ToolName{"mcp__resources__read"};
    def.description =
        "[MCP] Read the contents of an MCP resource by URI. Resources are "
        "server-provided documents/data (files, DB rows, API docs). Call with "
        "no args (or {\"list\":true}) to LIST every available resource and its "
        "URI; call with {\"uri\":\"...\"} to read one. Read-only.";
    def.input_schema = json{
        {"type", "object"},
        {"properties", json{
            {"uri",  json{{"type", "string"}, {"description", "Resource URI to read. Omit to list all resources."}}},
            {"list", json{{"type", "boolean"}, {"description", "List all available resources instead of reading."}}},
        }},
    };
    def.effects = tools::EffectSet{tools::Effect::ReadFs, tools::Effect::Net};
    def.origin = tools::ToolOrigin::Mcp;
    def.origin_id = "resources";
    def.scheduling_effects = def.effects;
    def.max_output_chars = 30'000;
    def.execute = [pool](const maya::unlocked&, const json& args, const tools::CallContext&) -> tools::ExecResult {
        const std::string uri = args.is_object() ? args.value("uri", std::string{}) : std::string{};
        const bool want_list  = uri.empty() || (args.is_object() && args.value("list", false));
        try {
            if (want_list) {
                auto res = pool->registry.resources();
                auto tpls = pool->registry.resource_templates();
                if (res.empty() && tpls.empty())
                    return tools::ToolOutput{"(no resources advertised)", std::nullopt};
                std::string out = "Available MCP resources:\n";
                for (const auto& r : res) {
                    out += "  " + r.uri;
                    if (r.title.has_value() && !r.title->empty()) out += "  — " + *r.title;
                    else if (!r.name.empty())                     out += "  — " + r.name;
                    if (r.mimeType.has_value() && !r.mimeType->empty()) out += " [" + *r.mimeType + "]";
                    out += '\n';
                }
                for (const auto& tp : tpls) {
                    out += "  " + tp.uriTemplate + "  (template";
                    if (tp.name.empty()) out += ")"; else out += ": " + tp.name + ")";
                    out += '\n';
                }
                return tools::ToolOutput{out, std::nullopt};
            }
            std::vector<::mcp::ResourceContents> contents;
            std::string err;
            bool ok;
            {
                ok = pool->registry.read_resource(uri, contents, err);
            }
            if (!ok)
                return std::unexpected(tools::ToolError::subprocess(
                    err.empty() ? "resources/read failed" : err));
            std::string out;
            for (const auto& c : contents) {
                std::visit([&](const auto& rc) {
                    using T = std::decay_t<decltype(rc)>;
                    if constexpr (std::is_same_v<T, ::mcp::TextResourceContents>) {
                        out += rc.text;
                        if (!out.empty() && out.back() != '\n') out += '\n';
                    } else {
                        out += "[blob " +
                               (rc.mimeType.has_value() ? *rc.mimeType : std::string{"application/octet-stream"}) +
                               ", ~" + std::to_string(rc.blob.size()) + "B base64]\n";
                    }
                }, c);
            }
            return tools::ToolOutput{out.empty() ? "(empty resource)" : out, std::nullopt};
        } catch (const std::exception& e) {
            return std::unexpected(tools::ToolError::subprocess(
                std::string{"resource read failed: "} + e.what()));
        } catch (...) {
            return std::unexpected(tools::ToolError::subprocess("resource read failed"));
        }
    };
    return def;
}

std::string render_prompt(const ::mcp::GetPromptResult& r) {
    std::string out;
    if (r.description.has_value() && !r.description->empty())
        out += "# " + *r.description + "\n\n";
    for (const auto& m : r.messages) {
        const auto role = ::mcp::to_json(m.role).get<std::string>();
        json cb = ::mcp::to_json(m.content);
        std::string body;
        if (cb.is_object() && cb.value("type", std::string{}) == "text")
            body = cb.value("text", std::string{});
        else
            body = cb.dump();
        out += role + ": " + body + "\n\n";
    }
    return out;
}

std::optional<std::string> resolve_prompt_route(
    const ::mcp::cap::Registry& registry, std::string_view requested) {
    std::optional<std::string> match;
    for (const auto& prompt : registry.prompts()) {
        const std::string canonical = canonical_mcp_name(prompt.name);
        const bool bare_match = prompt.name.size() > requested.size() + 2
            && prompt.name.ends_with("__" + std::string{requested});
        if (prompt.name == requested || canonical == requested || bare_match) {
            if (match && *match != prompt.name) return std::nullopt;
            match = prompt.name;
        }
    }
    return match;
}

tools::ToolDef make_get_prompt_tool(PoolHandle pool) {
    tools::ToolDef def;
    def.name        = ToolName{"mcp__prompts__get"};   // 见make_read_resource_tool
    def.description =
        "[MCP] Render an MCP prompt template provided by a server. Call with no "
        "args (or {\"list\":true}) to LIST every prompt, its name, and its "
        "arguments; call with {\"name\":\"...\",\"arguments\":{...}} to render "
        "one into ready-to-use messages. Read-only.";
    def.input_schema = json{
        {"type", "object"},
        {"properties", json{
            {"name",      json{{"type", "string"}, {"description", "Prompt name to render. Omit to list all prompts."}}},
            {"arguments", json{{"type", "object"}, {"description", "name→value map for the prompt's template arguments."}}},
            {"list",      json{{"type", "boolean"}, {"description", "List all available prompts instead of rendering."}}},
        }},
    };
    def.effects = tools::EffectSet{tools::Effect::ReadFs, tools::Effect::Net};
    def.origin = tools::ToolOrigin::Mcp;
    def.origin_id = "prompts";
    def.scheduling_effects = def.effects;
    def.max_output_chars = 30'000;
    def.execute = [pool](const maya::unlocked&, const json& args, const tools::CallContext&) -> tools::ExecResult {
        const std::string name = args.is_object() ? args.value("name", std::string{}) : std::string{};
        const bool want_list   = name.empty() || (args.is_object() && args.value("list", false));
        try {
            if (want_list) {
                auto prompts = pool->registry.prompts();
                if (prompts.empty())
                    return tools::ToolOutput{"(no prompts advertised)", std::nullopt};
                std::string out = "Available MCP prompts:\n";
                for (const auto& p : prompts) {
                    out += "  " + canonical_mcp_name(p.name);
                    if (p.description.has_value() && !p.description->empty())
                        out += "  — " + *p.description;
                    out += '\n';
                    if (p.arguments.has_value())
                        for (const auto& a : *p.arguments) {
                            out += "      - " + a.name;
                            if (a.required.has_value() && *a.required) out += " (required)";
                            if (a.description.has_value() && !a.description->empty())
                                out += ": " + *a.description;
                            out += '\n';
                        }
                }
                return tools::ToolOutput{out, std::nullopt};
            }
            std::vector<std::pair<std::string, std::string>> kv;
            if (args.is_object() && args.contains("arguments") && args["arguments"].is_object())
                for (auto it = args["arguments"].begin(); it != args["arguments"].end(); ++it)
                    kv.emplace_back(it.key(), it.value().is_string()
                                                  ? it.value().get<std::string>()
                                                  : it.value().dump());
            ::mcp::GetPromptResult res;
            std::string err;
            bool ok;
            {
                auto route = resolve_prompt_route(pool->registry, name);
                if (!route)
                    return std::unexpected(tools::ToolError::not_found(
                        "MCP prompt is missing or ambiguous: " + name));
                ok = pool->registry.get_prompt(*route, kv, res, err);
            }
            if (!ok)
                return std::unexpected(tools::ToolError::subprocess(
                    err.empty() ? "prompts/get failed" : err));
            std::string out = render_prompt(res);
            return tools::ToolOutput{out.empty() ? "(empty prompt)" : out, std::nullopt};
        } catch (const std::exception& e) {
            return std::unexpected(tools::ToolError::subprocess(
                std::string{"prompt render failed: "} + e.what()));
        } catch (...) {
            return std::unexpected(tools::ToolError::subprocess("prompt render failed"));
        }
    };
    return def;
}

std::optional<std::string> resolve_tool_route(
    const ::mcp::cap::Registry& registry, std::string_view requested) {
    std::optional<std::string> match;
    for (const auto& tool : registry.tools()) {
        const std::string canonical = canonical_mcp_name(tool.name);
        const bool bare_match = tool.name.size() > requested.size() + 2
            && tool.name.ends_with("__" + std::string{requested});
        if (tool.name == requested || canonical == requested || bare_match) {
            if (match && *match != tool.name) return std::nullopt;
            match = tool.name;
        }
    }
    return match;
}

// 当前工具是否确实启用？
//
// 两个来源：在连接（tools.include/tools.exclude)
// 时烘焙的per-server策略和LIVE配置排除插件面板 切换写入。
// 一个功能，因此关闭的工具在每条路径上都是关闭的。
[[nodiscard]] std::unordered_map<std::string, std::unordered_set<std::string>>
live_exclude_map() {
    std::unordered_map<std::string, std::unordered_set<std::string>> m;
    for (const auto& [sname, cs] : read_config_servers())
        if (!cs.exclude.empty()) m[sname] = cs.exclude;
    return m;
}

// 读取池->策略，一旦发布池就固定该策略。
[[nodiscard]] bool tool_enabled(
    const PoolHandle& pool, std::string_view exposed,
    const std::unordered_map<std::string, std::unordered_set<std::string>>& live_excl) {
    const auto origin = mcp_origin_id(exposed);
    const auto bare   = mcp_bare_name(exposed);
    const ServerPolicy fallback;
    const auto it = pool->policies.find(origin);
    const auto& policy = it == pool->policies.end() ? fallback : it->second;
    if (!policy.include.empty() && !policy.include.contains(bare)) return false;
    if (policy.exclude.contains(bare)) return false;
    if (auto le = live_excl.find(origin);
        le != live_excl.end() && le->second.contains(bare))
        return false;
    return true;
}

// 为 池 构建完整的 ToolDef 向量：每个服务器工具+通用
// resource/prompt 访问工具（仅当联合公开任何工具时）。
// ── 传球工具 ────────────────────────────────────────────
// `type:"passthrough"`服务器声明一些OTHER方向模型做
// 广告的工具（典型情况：LiteLLM+headroom代理在其pre
// -call钩子中将`headroom_retrieve`注入到我们的请求中）。
// agentty 通过将参数 JSON POST 到条目的 url 来执行它们；
// 响应 正文 是工具结果。没有过程，没有 MCP 握手 - 在投影
// 时直接从配置合成，因此 add/toggle/remove 的行为
// 与 MCP 工具 切换 完全相同（配置编辑 + 缓存 失效）。
//
// "Allowing"其中之一具有具体含义：Effect::Net，
// 因此在Ask/Minimal配置文件下，
// 标准权限 提示词在调用的参数离开机器以用于配置的URL之前询问。
// 这是诚实的同意问题，而不是一揽子绕过。
std::vector<tools::ToolDef> passthrough_tools() {
    // Minimal绝对-URL 解析 (scheme/host/port/path)
    // -直通端点是普通http(s)://主机[:端口]/路径，与MCP HTTP
    // 传输层接受的形状相同。本地的，所以这个 TU 不依赖于
    // http_server.cpp 内部。
    struct PtUrl { bool ok=false, tls=true; std::string host;
                   std::uint16_t port=443; std::string path="/"; };
    auto parse_pt_url = [](const std::string& url) -> PtUrl {
        PtUrl u;
        std::string_view s{url};
        if (s.starts_with("https://"))     { u.tls = true;  u.port = 443; s.remove_prefix(8); }
        else if (s.starts_with("http://")) { u.tls = false; u.port = 80;  s.remove_prefix(7); }
        else return u;
        const auto slash = s.find('/');
        std::string_view hostport = s.substr(0, slash);
        if (slash != std::string_view::npos) u.path = std::string{s.substr(slash)};
        if (const auto colon = hostport.rfind(':');
            colon != std::string_view::npos) {
            const auto pstr = hostport.substr(colon + 1);
            unsigned long p = 0;
            for (char c : pstr) {
                if (c < '0' || c > '9') { p = 0; break; }
                p = p * 10 + static_cast<unsigned long>(c - '0');
            }
            if (p == 0 || p > 65535) return u;
            u.port = static_cast<std::uint16_t>(p);
            hostport = hostport.substr(0, colon);
        }
        if (hostport.empty()) return u;
        u.host = std::string{hostport};
        u.ok = true;
        return u;
    };
    std::vector<tools::ToolDef> out;
    for (const auto& [sname, cs] : read_config_servers()) {
        if (cs.disabled || cs.passthrough.empty()) continue;
        if (cs.type != "passthrough") continue;
        // Project-scope条目是像stdio服务器一样的trust-gated：
        // 调用 EXFILTRATES工具参数到仓库选择的URL——值得保证，
        // 即使什么也没有产生。 User/explicit 配置
        // 是用户-placed 且受构造信任。
        if (cs.source.locus == scope::Locus::Project
            && !source_trusted(cs.source, cs.file))
            continue;
        const std::string url = cs.url;
        if (url.empty()) continue;
        for (const auto& pt : cs.passthrough) {
            if (cs.exclude.contains(pt.name)) continue;   // 关闭
            tools::ToolDef def;
            def.name        = ToolName{pt.name};
            def.origin      = tools::ToolOrigin::Passthrough;
            def.origin_id   = sname;
            def.advertise   = pt.advertise;
            // 与MCP描述相同的信任边界：擦洗+上限。
            std::string desc = tools::util::to_valid_utf8(
                tools::util::strip_terminal_controls(pt.description));
            if (desc.size() > 1024) { desc.resize(1024); desc += "…"; }
            def.description = "[passthrough " + sname + "] "
                + (desc.empty() ? ("Forwarded to " + url) : desc);
            def.input_schema = {{"type", "object"}};
            def.effects  = tools::EffectSet{tools::Effect::Net};
            def.scheduling_effects = def.effects;
            def.max_output_chars   = 30'000;
            def.execute = [url, name = pt.name, parse_pt_url](const maya::unlocked&, const json& args, const tools::CallContext&)
                -> tools::ExecResult {
                const PtUrl u = parse_pt_url(url);
                if (!u.ok)
                    return std::unexpected(tools::ToolError::invalid_args(
                        "passthrough url is malformed: " + url));
                http::Request req;
                req.method    = http::HttpMethod::Post;
                req.host      = u.host;
                req.port      = u.port;
                req.path      = u.path;
                req.plaintext = !u.tls;
                req.body      = args.is_null() ? "{}" : args.dump();
                req.headers.push_back({"content-type", "application/json"});
                req.headers.push_back({"accept", "application/json"});
                auto res = http::default_client(::agentty::IoAccess::grant()).send(req);
                if (!res)
                    return std::unexpected(tools::ToolError::subprocess(
                        "passthrough '" + name + "' → " + url + " failed: "
                        + res.error().detail
                        + ". The upstream service may be down or the cached "
                          "entry may have expired — proceed without it if "
                          "possible."));
                if (res->status < 200 || res->status >= 300)
                    return std::unexpected(tools::ToolError::subprocess(
                        "passthrough '" + name + "' → " + url + " returned HTTP "
                        + std::to_string(res->status)
                        + (res->body.empty() ? std::string{}
                                             : (": " + tools::util::to_valid_utf8(
                                                   res->body.substr(0, 512))))
                        + ". Proceed without this data if possible; do not "
                          "retry with the same arguments."));
                std::string text = tools::util::to_valid_utf8(
                    tools::util::strip_terminal_controls(res->body));
                return tools::ToolOutput{
                    text.empty() ? "(empty response)" : std::move(text),
                    std::nullopt};
            };
            out.push_back(std::move(def));
        }
    }
    return out;
}

std::vector<tools::ToolDef> project_tools(PoolHandle pool) {
    std::vector<tools::ToolDef> out;
    bool any_resources = false, any_prompts = false;

    // LIVE排除：在PROJECTION时间读取当前配置的per-server
    // tools.exclude，而不是仅信任在连接时烘焙的策略。
    // 这使得 enabling/disabling 成为一个配置编辑 + 缓存 失效的工具
    // - NO 服务器 re-spawn - 因此 切换 不能挂在 reload/re-connect
    // 竞态 上。
    //
    // 读取MERGED地图而不是re-parsing一个获胜文件。
    // 旧形式解析了单个配置，因此每当项目 mcp.json 存在时，
    // 用户服务器的工具-exclude 就会被默默地忽略——插件
    // 面板 中的 切换 似乎什么也不做。
    std::unordered_map<std::string, std::unordered_set<std::string>> live_excl =
        live_exclude_map();
    // 引脚也实时读取，因此插件面板中的`p`适用于下一回合，
    // 而无需重新连接服务器。
    std::unordered_map<std::string, std::unordered_set<std::string>> live_pins;
    for (const auto& [sname, cs] : read_config_servers())
        if (!cs.pin.empty()) live_pins[sname] = cs.pin;

    {
        // 名称在整个池中分发，因此两个服务器的长名称永远
        // 不会冲突，并且永远不会与本机工具冲突。
        std::unordered_set<std::string> taken;
        for (const auto& n : tools::native_registry()) taken.insert(n.name.value);
        // 已排序，因此冲突获得的散列后缀不取决于服务器
        // 应答的顺序。
        auto listed = pool->registry.tools();
        std::sort(listed.begin(), listed.end(),
                  [](const ::mcp::Tool& a, const ::mcp::Tool& b) { return a.name < b.name; });
        for (auto& t : listed) {
            const auto origin = mcp_origin_id(t.name);
            if (!tool_enabled(pool, t.name, live_excl)) continue;
            const auto policy_it = pool->policies.find(origin);
            const ServerPolicy fallback;
            const auto& policy = policy_it == pool->policies.end()
                ? fallback : policy_it->second;
            auto def = make_tool(pool, t, policy, taken);
            if (auto lp = live_pins.find(origin);
                lp != live_pins.end() && lp->second.contains(mcp_bare_name(t.name)))
                def.always_expose = true;
            out.push_back(std::move(def));
        }
        any_resources = !pool->registry.resources().empty() ||
                        !pool->registry.resource_templates().empty();
        any_prompts   = !pool->registry.prompts().empty();
    }
    // 不是已加载的工具的发现是tools::plan的tool_search，
    // 其涵盖所有非-native工具，而不仅仅是MCP。每个 MCP 工具都可以
    // 通过自己的名称直接调用，因此不需要通用的 "call" 工具。
    if (any_resources) out.push_back(make_read_resource_tool(pool));
    if (any_prompts)   out.push_back(make_get_prompt_tool(pool));

    // 这里没有修剪。这是 CATALOG，分派 在其中解析名称；
    // 修剪它使工具超出了预算"unknown"，即使模型用它的
    // 确切名称称呼它。一个请求携带的内容由 tools::plan
    // 每回合决定。
    // 直通工具利用OUTSIDE预算削减：它们很少，
    // 用户-declared一次一个，并且大多数广告=假（零
    // 传输成本——代理已经将模式定义放在传输上）。
    for (auto& pt : passthrough_tools()) out.push_back(std::move(pt));
    return out;
}

} // 命名空间结束。

bool mcp_config_present() { return !read_config_servers().empty(); }

std::vector<ServerLaunch> configured_servers_for_delegation() {
    // 一个梯子。这用于解析 一个 获胜文件，
    // 因此项目 mcp.json 默默地隐藏了每个用户服务器的委托，
    // 而插件 面板（读取合并的地图）仍然列出它们。
    //
    // 信任现在是按SOURCE而不是按整个-config：
    // 未担保项目旁边的用户服务器保持可用而不是
    // 附带损害。
    auto string_value = [](const json& value) {
        return value.is_string() ? value.get<std::string>() : value.dump();
    };
    std::vector<ServerLaunch> out;
    for (const auto& [sname, cs] : read_config_servers()) {
        if (cs.disabled) continue;
        // 直通条目是分派-only工具声明，而不是可委托
        // 的MCP服务器。
        if (cs.type == "passthrough") continue;
        // 与 mcp_tools() 相同的门：在克隆上
        // 运行的配置仅在人类为其担保后才暴露其服务器。
        if (cs.source.locus == scope::Locus::Project
            && !source_trusted(cs.source, cs.file))
            continue;
        const json& spec = cs.spec;
        ServerLaunch launch;
        launch.name = sname;
        launch.command = cs.command;
        launch.url = cs.url;
        if (!launch.url.empty() || cs.type == "http" || cs.type == "streamable-http")
            launch.transport = ServerLaunch::Transport::Http;
        else if (cs.type == "sse")
            launch.transport = ServerLaunch::Transport::Sse;
        if (auto args = spec.find("args"); args != spec.end() && args->is_array())
            for (const auto& value : *args) launch.args.push_back(string_value(value));
        if (auto env = spec.find("env"); env != spec.end() && env->is_object())
            for (auto entry = env->begin(); entry != env->end(); ++entry)
                launch.env.emplace_back(entry.key(), string_value(entry.value()));
        if (auto headers = spec.find("headers"); headers != spec.end() && headers->is_object())
            for (auto entry = headers->begin(); entry != headers->end(); ++entry)
                launch.headers.emplace_back(entry.key(), string_value(entry.value()));
        if ((launch.transport == ServerLaunch::Transport::Stdio && launch.command.empty())
            || (launch.transport != ServerLaunch::Transport::Stdio && launch.url.empty()))
            continue;
        out.push_back(std::move(launch));
    }
    return out;
}

void set_client_servers(const json& servers) {
    // 会话联合：编辑器通常每次发送相同的列表，
    // 并且发送较少列表的会话不得拔出另一个会话的列表。
    // 再次发送新规格的服务器取代旧规格。
    if (!servers.is_object()) return;
    const std::string before = *client_servers().load();
    const auto after = client_servers().update([](const std::string& cur, const std::string& add) {
        json s = json::object();
        try { s = json::parse(cur); } catch (...) {}
        const json n = json::parse(add);
        for (auto it = n.begin(); it != n.end(); ++it) {
            auto c = s.find(it.key());
            if (c == s.end() || *c != it.value()) s[it.key()] = it.value();
        }
        return s.dump();
    }, servers.dump());
    const bool changed = *after != before;
    // 通过注册表，因此模型看到的工具目录也被重建。
    if (changed) (void)::agentty::tools::reload_mcp_plugins();
}

std::vector<std::string> configured_server_names_from_client() {
    std::vector<std::string> out;
    const json s = client_servers_json();
    for (auto it = s.begin(); it != s.end(); ++it) out.push_back(it.key());
    return out;
}

namespace {
std::vector<tools::ToolDef> build_pool(const maya::unlocked& cap, PoolHandle& out_pool);
}

std::vector<tools::ToolDef> mcp_tools(PoolHandle& out_pool) {
    // 审查的根(tests/lint/allowlist.txt)：
    // 调用者是目录访问器的连接步骤和mcp_reload()，
    // 两者都在工作线程上，并且注册表从未从锁 正文到达这里。
    // 下面的等待检查 运行时：锁 正文 内的一个中止而不是挂起。
    return maya::thread_root([&](const maya::unlocked& cap) {
    // 序列化 整个连接+交换——没有两个构建同时运行。
    // build_pool发布了池本身，因此out_pool将其读回。
    auto& gate = g_connecting();
    bool expected = false;
    while (!gate.compare_exchange_weak(expected, true, std::memory_order_acquire)) {
        // 阻塞直到持有者释放(原子等待，而不是睡眠-poll)。
        if (expected) maya::atomic_wait(cap, gate, true, std::memory_order_acquire);
        expected = false;
    }
    auto release = [&gate] {
        gate.store(false, std::memory_order_release);
        gate.notify_all();
    };
    std::vector<tools::ToolDef> defs;
    try {
        PoolHandle built;
        defs = build_pool(cap, built);
    } catch (...) { release(); throw; }
    release();
    // 被替换的池死在这里，没有保留锁。
    (void)g_retired().replace(nullptr);
    out_pool = current_pool();
    return defs;
    });
}

namespace {
std::vector<tools::ToolDef> build_pool(const maya::unlocked& cap, PoolHandle& out_pool) {
    std::vector<tools::ToolDef> out;

    // ONE梯子。这用于解析单个获胜文件，
    // 而 read_config_servers()（插件 面板、
    // `agentty config mcp`、委托）通过 scope::plan
    // 合并项目+用户。两人意见不一致，分歧是无声的和糟糕的：每个用户服务器
    // 都有一个项目 mcp.json DISABLED，而每个列出服务器的表面
    // 仍然显示它们。您看到您的服务器，但它从未运行。
    const auto config = read_config_servers();
    if (config.empty()) return out;       // 无配置 → 零工作、零工具

    // ── 不信任的-workspace产卵门(安全) ────────────────────
    // 项目-local ./.agentty/mcp.json可以骑在克隆的仓库上，
    // 并且其stdio服务器在注册表-build时间产生ARBITRARY命令，
    // 没有per-tool 权限 提示词——绕过所有其他代码路径所
    // 遵循的Exec门。因此，工作区-local 配置的 stdio
    // 服务器仅在人类为其提供担保时才会连接。
    //
    // 轨迹判决属于scope::trust_of(参见source_trusted)：
    // Explicit/User是可信的，因为人类放置了它们，
    // Project/Local骑在克隆身上。现在，信任也是按照 SOURCE 进行的，
    // 而不是按照整个 -config 进行的 — 未担保的项目文件不再会导致
    // 用户自己的服务器崩溃。在项目源中，它保留在 SERVER（在下面的
    // 循环中检查 spec-hash），因此批准一台服务器并不会祝福后来的
    // -added 一台服务器，并且仅编辑一条命令 re-gates。
    std::unordered_map<std::string, bool> source_ok;   // 记忆每个文件
    auto whole_source_trusted = [&](const ConfigServer& cs) -> bool {
        if (cs.source.locus != scope::Locus::Project) return true;
        const std::string key = cs.file.string();
        if (auto it = source_ok.find(key); it != source_ok.end()) return it->second;
        const bool ok = source_trusted(cs.source, cs.file);
        source_ok.emplace(key, ok);
        return ok;
    };

    auto pool = std::make_shared<ConnectionPool>();
    // 来自任何服务器的tools/list_changed (
    // +resources/prompts)都会碰撞池一代。调用者通过比较 mcp_generation
    // () 来知道 快照 被移动了；不管怎样，分派 路径都会继续工作（它按名称路由）。
    auto bump_on_change = [wp = std::weak_ptr<const ConnectionPool>(pool)] {
        if (auto p = wp.lock())
            p->generation.fetch_add(1, std::memory_order_relaxed);
    };

    // ── 连接每个服务器 IN PARALLEL 以全球截止日期为准 ──────────
    // 每个提供者的构造函数在握手时阻塞(直到SDK的per-server
    // 超时)。串行连接使得第一个 registry() 访问 Cold
    // -start 路径上的 O(N × 握手)，
    // 因此一台 slow/hung 服务器停滞了整个工具表面。
    // 将连接分散到 线程 上，并将整个批次与 AGENTTY_MCP_CONNECT_TIMEOUT_MS
    // 绑定（默认 15 秒）；任何在截止日期之后仍在握手的服务
    // 器都会跳过此会话（当未来分离时，其最终结果将被丢弃）。
    long connect_deadline_ms = 15'000;
    if (const char* e = std::getenv("AGENTTY_MCP_CONNECT_TIMEOUT_MS"); e && e[0]) {
        try { long v = std::stol(e); if (v > 0) connect_deadline_ms = v; }
        catch (const std::exception& ex) { util::dbglog("mcp.connect_timeout.env", ex.what()); }
        catch (...) { util::dbglog("mcp.connect_timeout.env", "non-std exception"); }
    }

    struct Pending {
        std::string name;
        // 平淡的未来，不是shared_future。它被共享只是为了让下面的
        // orphan-waiter 可以保持 timed-out 连接处于活动状态；
        // 池 现在拥有该作业，因此不需要其他任何东西来处理它。
        maya::job_result<std::shared_ptr<::mcp::cap::CapabilityProvider>> fut;
    };
    std::vector<Pending> pending;
    // 每-server信任设置一次，已加载一次。对于不是 Blank-trusted 的 工作
    // 区-local 配置，仅当 stdio 服务器自己的规范哈希位于 user-root
    // 批准存储中时才会连接（请参阅 source_trusted /
    // plugin::server_spec_hash）。
    const scope::Approvals approvals = scope::load_approvals(kMcpApprovalsLeaf);
    for (const auto& [sname_key, cs] : config) {
        const std::string sname = sname_key;
        const json spec = cs.spec;   // 副本：分离的 工作线程 比 `config` 寿命更长
        if (cs.disabled) continue;

        // 不可信-workspace生成门，PER SERVER。来自 not
        // -yet-trusted 项目配置的 stdio 服务器（有命令，无 URL）将被跳过，
        // 除非其确切的规范哈希值得到批准。 HTTP/SSE 服务器不产生本地命令，
        // 因此它们没有门控。 plugin_model() 将跳过的服务器显示为
        // "untrusted — approve to enable"。
        // type:"passthrough"条目根本就是NOT MCP
        // 服务器——没有进程，没有握手；他们的工具是从 passthrough_tools
        // () 中的配置合成的。在这里将一个交给 HTTP 传输层
        // 使得连接循环握手非 -MCP 端点（余量的 /
        // v1/retrieve）并消耗了整整 15 秒的截止时间 - 插件
        // 面板 位于 "connecting…" 上并且启动无需等待。
        if (cs.type == "passthrough") continue;
        if (!whole_source_trusted(cs)) {
            const std::string command = spec.value("command", std::string{});
            const std::string url     = spec.value("url", std::string{});
            if (!command.empty() && url.empty()) {
                std::vector<std::string> args;
                if (spec.contains("args") && spec["args"].is_array())
                    for (const auto& a : spec["args"])
                        if (a.is_string()) args.push_back(a.get<std::string>());
                const std::string h =
                    tools::plugin::server_spec_hash(command, url, args);
                if (h.empty() || !approvals.approved(h)) {
                    std::fprintf(stderr,
                        "mcp: skipping untrusted project server '%s' — approve it\n"
                        "     in the Plugins picker or set AGENTTY_MCP_ALLOW_PROJECT=1.\n",
                        sname.c_str());
                    continue;
                }
            }
        }

        ServerPolicy policy;
        policy.trust_annotations = spec.value("trustAnnotations", false);
        policy.max_output_chars = spec.value("maxOutputChars", 30'000);
        auto add_names = [&](const json& values, std::unordered_set<std::string>& out) {
            if (!values.is_array()) return;
            for (const auto& value : values)
                if (value.is_string()) out.insert(value.get<std::string>());
        };
        if (auto tools_it = spec.find("tools"); tools_it != spec.end() && tools_it->is_object()) {
            add_names(tools_it->value("include", json::array()), policy.include);
            add_names(tools_it->value("exclude", json::array()), policy.exclude);
            add_names(tools_it->value("pin", json::array()), policy.pin);
        }
        pool->policies[sname] = std::move(policy);
        // 具有"url" (或类型："http"/"sse")的服务器条目是远程
        // Streamable HTTP服务器；任何带有 "command" 的东西都是
        // 生成的 stdio 服务器。当两者都在场时，URL 获胜。
        // submit_isolated，不是标准的异步帮助
        // 程序：连接是一个可以楔入的网络拨号，并且该帮助程序的未来
        // JOINS 在其析构函数中 - 因此在下面删除一个
        // timed-out 连接将阻止在删除它的行处启动。
        // 池 的未来不会，并且该工作仍然是拥有的（在拆卸时等待）而不是
        // 分离。
        pending.push_back(Pending{sname,
            ::agentty::util::background_pool().submit_isolated(
            [](std::stop_token, std::string sname, json spec)
                -> std::shared_ptr<::mcp::cap::CapabilityProvider> {
                const std::string url  = spec.value("url", std::string{});
                const std::string type = spec.value("type", std::string{});
                const bool is_http = !url.empty() || type == "http" || type == "sse"
                                     || type == "streamable-http";
                if (is_http) {
                    HttpConfig hc;
                    hc.url = url;
                    if (spec.contains("headers") && spec["headers"].is_object())
                        for (auto h = spec["headers"].begin(); h != spec["headers"].end(); ++h)
                            hc.headers.emplace_back(h.key(), h.value().is_string()
                                                                 ? h.value().get<std::string>()
                                                                 : h.value().dump());
                    hc.call_timeout = call_timeout();
                    hc.workspace_root = tools::util::workspace_root().generic_string();
                    if (const auto ms = spec.value("timeoutMs", 0L); ms > 0)
                        hc.call_timeout = std::chrono::milliseconds{ms};
                    std::string err;
                    auto p = make_http_provider(sname, hc, err);
                    if (!p) std::fprintf(stderr, "mcp: %s\n", err.c_str());
                    return p;
                }
                return make_provider(sname, spec);
            }, sname, spec)});
    }

    const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds{connect_deadline_ms};
    for (auto& pend : pending) {
        auto remaining = deadline - std::chrono::steady_clock::now();
        if (remaining < std::chrono::milliseconds{0})
            remaining = std::chrono::milliseconds{0};
        if (!pend.fut.wait_for(cap, remaining)) {
            std::fprintf(stderr,
                "mcp: server '%s' did not connect within the deadline — skipping\n",
                pend.name.c_str());
            pool->connect_errors[pend.name] = "did not connect within deadline";
            // stderr 在 TUI 下是不可见的，所以这曾经消失：
            // 一个默默地无法连接的插件仅仅意味着它的工具 "weren't there"，
            // 没有办法从配置拼写错误中看出这一点。
            AGT_LOG(Mcp, Warn, "mcp.connect", "server={} result=timeout", pend.name);
            // 放下它。 still-handshaking 作业
            // 继续在 池 上运行，并在拆卸时等待；其结果被丢弃。
            // 这曾经需要一个独立的服务员，他唯一的工作
            // 就是在此处以外的其他地方吸收阻塞的未来析构函数。
            continue;
        }
        auto p = pend.fut.get(cap);
        if (p) {
            std::fprintf(stderr,
                "mcp: server '%s' connected (%zu tools, %zu resources, %zu prompts)\n",
                pend.name.c_str(), p->list().size(), p->resources().size(), p->prompts().size());
            AGT_LOG(Mcp, Info, "mcp.connect",
                    "server={} result=ok tools={} resources={} prompts={}",
                    pend.name, p->list().size(), p->resources().size(),
                    p->prompts().size());
            p->set_on_list_changed(bump_on_change);
            pool->registry.add(std::move(p));
        } else {
            // make_provider 返回 nullptr —
            // spawn/handshake 失败。具体原因去stderr；
            // 记录通用标记，以便模型显示该服务器发生故障，而不是默默地消失。
            AGT_LOG(Mcp, Warn, "mcp.connect", "server={} result=spawn_failed",
                    pend.name);
            if (!pool->connect_errors.count(pend.name))
                pool->connect_errors[pend.name] = "failed to start (see stderr log)";
        }
    }
    // NOTE:当 provider_count()==0 时不执行 Early
    // -return — 仅包含故障服务器的配置仍然需要发布其 池，
    // 以便 plugin_model() 可以报告故障。下面无条件安装了池。

    out_pool = pool;                       // 保持提供者存活（调用者）
    // 访问器的process-wide句柄
    g_retired().publish(g_pool().replace(pool));
    return project_tools(pool);
}
}  // 命名空间结束。

// ── 活式/动态存取器 ──────────────────────────────────────────────

std::vector<tools::ToolDef> mcp_tools_live() {
    auto pool = current_pool();
    if (!pool) return {};
    return project_tools(pool);
}

unsigned long mcp_generation() noexcept {
    auto pool = current_pool();
    if (!pool) return 0;
    return pool->generation.load(std::memory_order_relaxed);
}

void mcp_bump_generation() noexcept {
    if (auto pool = current_pool())
        pool->generation.fetch_add(1, std::memory_order_relaxed);
}

std::vector<std::string> mcp_server_tools(const std::string& server) {
    std::vector<std::string> out;
    auto pool = current_pool();
    if (!pool) return out;
    for (const auto& t : pool->registry.tools()) {
        if (mcp_origin_id(t.name) == server)
            out.push_back(mcp_bare_name(t.name));
    }
    return out;
}

std::vector<std::pair<std::string, std::string>> mcp_server_instructions() {
    // 有界：服务器的指令在每个请求上都遵循tool_search描述，
    // 因此一个详细的服务器不能吃掉上下文（Codex限制了整个列表；
    // 这限制了每个列表）。
    constexpr std::size_t kPerServer = 600;
    std::vector<std::pair<std::string, std::string>> out;
    auto pool = current_pool();
    if (!pool) return out;
    for (const auto& p : pool->registry.providers()) {
        if (!p) continue;
        std::string text = p->instructions();
        if (text.empty()) continue;
        std::string_view o = p->origin();
        if (o.starts_with("mcp:")) o.remove_prefix(4);
        text = tools::util::to_valid_utf8(tools::util::strip_terminal_controls(std::move(text)));
        for (auto& c : text) if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        if (text.size() > kPerServer) {
            text.resize(tools::util::safe_utf8_cut(text, kPerServer));
            text += "\xe2\x80\xa6";
        }
        out.emplace_back(std::string{o}, std::move(text));
    }
    std::sort(out.begin(), out.end());
    return out;
}

PluginModel plugin_model() {
    PluginModel model;
    // 本机工具总是发布。
    model.native_tool_count = tools::native_registry().size();

    // 配置：存在哪些服务器、它们的命令以及禁用的工具。
    const auto cfg = read_config_servers();

    // 直播：广告工具+连接错误，在池 锁下。
    struct LiveTool { std::string bare, desc; };
    std::unordered_map<std::string, std::vector<LiveTool>> live;
    std::unordered_map<std::string, std::string> errors;
    if (auto pool = current_pool()) {
        for (const auto& t : pool->registry.tools()) {
            const std::string origin = mcp_origin_id(t.name);
            LiveTool lt;
            lt.bare = mcp_bare_name(t.name);
            lt.desc = t.description.has_value() ? *t.description : std::string{};
            live[origin].push_back(std::move(lt));
        }
        errors = pool->connect_errors;
    }

    // 每个服务器的Last-known工具集。
    // DISABLED（或暂时断开连接）服务器不会实时发布任何内容，
    // 因此，如果没有此功能，它的整个工具列表将在您关闭它时立即
    // 消失 - 而启动禁用的服务器将不会显示任何内容，
    // 而具有单独排除的工具的服务器将仅显示这些工具。不一致+混乱。
    // 记住每个服务器最后通告的内容，以便其源码树在禁用→启用回合-trip期间保持填充
    // （显示为灰色）。 Process-lifetime、
    // 互斥量-guarded，按服务器名称键入。
    //
    // 有界+身份-checked:我们服务器的PRUNE条目不再
    // 在配置中（因此add/remove搅动不能无限制地增长它），
    // 并存储命令，以便使用DIFFERENT命令在SAME名称下
    // 的re-add驱逐过期工具（否则新服务器将显示旧服务器的工具，
    // 直到它连接）。
    struct SeenEntry {
        std::string command;
        std::vector<std::pair<std::string, std::string>> tools;  // （裸露，描述）
    };
    using SeenMap = std::unordered_map<std::string, SeenEntry>;
    static maya::snapshot<SeenMap> s_seen;
    // 缓存从配置和实时池中需要什么，作为普通值：
    // 每个配置的服务器的命令，以及每个活动服务器通告的内容。
    std::unordered_map<std::string, std::string> commands;
    for (const auto& [name, cs] : cfg) commands.emplace(name, cs.command);
    SeenMap advertised;
    for (const auto& [srv, tools] : live) {
        auto& ent = advertised[srv];
        if (auto cit = cfg.find(srv); cit != cfg.end()) ent.command = cit->second.command;
        for (const auto& lt : tools) ent.tools.emplace_back(lt.bare, lt.desc);
    }
    // 修剪不再配置的服务器（或其命令已更改：身份
    // 重用）的条目，然后记录活动服务器通告的内容。
    // 锁-free:从view-side命令读取plugin_model()。
    const SeenMap seen = *s_seen.update(
        [](const SeenMap& prev, const std::unordered_map<std::string, std::string>& cmds,
           const SeenMap& adv) {
            SeenMap m = prev;
            for (auto it = m.begin(); it != m.end();) {
                auto cit = cmds.find(it->first);
                if (cit == cmds.end() || cit->second != it->second.command)
                    it = m.erase(it);
                else
                    ++it;
            }
            for (const auto& [srv, ent] : adv) m[srv] = ent;
            return m;
        },
        std::move(commands), std::move(advertised));

    // 每个配置的服务器构建一个ServerState，统一配置+实时。
    // Per-server信任（RCE门）：工作区
    // -local配置的stdio服务器仅在得到保证后才连接。信任是按照
    // SOURCE （每个服务器知道它来自的文件），然后是其中的每个 SERVER
    // — 连接循环运行的两个 -step 相同，因此 面板 永远不会显示
    // mcp_tools() 会跳过的可用服务器。
    std::unordered_map<std::string, bool> source_ok;
    auto whole_source_trusted = [&](const ConfigServer& cs) -> bool {
        if (cs.source.locus != scope::Locus::Project) return true;
        const std::string key = cs.file.string();
        if (auto it = source_ok.find(key); it != source_ok.end()) return it->second;
        const bool ok = source_trusted(cs.source, cs.file);
        source_ok.emplace(key, ok);
        return ok;
    };
    const scope::Approvals approvals = scope::load_approvals(kMcpApprovalsLeaf);
    for (const auto& [name, cs] : cfg) {
        ServerState ss;
        ss.name    = name;
        ss.command = cs.command;
        ss.url     = cs.url;
        ss.passthrough = (cs.type == "passthrough");
        // 读取该条目的来源来自scope::Source，映射到值-header原点，
        // 因此选择面板可以标记范围并且状态更新函数可以将编辑路由
        // 到cs.source.base(右侧mcp.json)。
        switch (cs.source.locus) {
            case scope::Locus::Project:  ss.origin = Origin::Project;  break;
            case scope::Locus::Explicit: ss.origin = Origin::Explicit; break;
            default:                     ss.origin = Origin::User;     break;
        }
        ss.config_dir = cs.source.base.string();
        auto lit = live.find(name);
        ss.connected = (lit != live.end() && !lit->second.empty());
        ss.disabled  = cs.disabled;
        // 故意关闭的服务器不是 错误并且从未连接，
        // 因此它没有错误并且没有实时工具——禁用胜过来自先前会话的
        // 任何 过期 connect-error 条目。
        if (!ss.disabled && cs.type == "passthrough") {
            // （上面设置的 ss.passthrough — 禁用时也是如此，
            // 因此该行在关闭时保留其 ⇄ 徽章。）直通：无进程，
            // 无握手 — "connected" 表示
            // "declared + reachable config"。
            // 工具来自配置声明，而不是实时的 池。 Project-scope 条目
            // 是 trust-gated（参数渗漏到 URL）。
            if (cs.url.empty()) {
                ss.error = "passthrough needs a \"url\" in mcp.json";
            } else if (cs.source.locus == scope::Locus::Project
                       && !whole_source_trusted(cs)
                       && !approvals.approved(
                              tools::plugin::server_spec_hash(
                                  cs.command, cs.url, cs.args))) {
                ss.untrusted = true;
                ss.error = "untrusted project config — approve to enable";
            } else {
                ss.connected = true;
            }
            for (const auto& pt : cs.passthrough) {
                ToolState ts;
                ts.name        = pt.name;
                ts.description = pt.description.empty()
                    ? ("forwarded to " + cs.url
                       + (pt.advertise ? "" : " · proxy-advertised"))
                    : pt.description;
                ts.enabled     = !cs.exclude.contains(pt.name);
                ss.tools.push_back(std::move(ts));
            }
        } else if (!ss.disabled) {
            // 具有`url`的服务器是HTTP/SSE并且合法地没有
            // `command`——只有缺少其命令的stdio服务器（无
            // url）永远无法连接。这是对每个远程服务器曾经显示的错误
            // "no command" 错误的修复。
            if (cs.command.empty() && cs.url.empty()) {
                ss.error = "no \"command\" or \"url\" in mcp.json";
            } else if (cs.source.locus == scope::Locus::Project
                       && !cs.command.empty() && cs.url.empty()
                       && !whole_source_trusted(cs)
                       && !approvals.approved(
                              tools::plugin::server_spec_hash(
                                  cs.command, cs.url, cs.args))) {
                // OWN规范尚未批准的项目（工作区-local）stdio服务
                // 器将无法连接（per-server RCE门）。
                // 连接循环使用相同的规范哈希（command+url+args），
                // 因此 选择面板 和 传输 一致。 HTTP/SSE (url) 服务器
                // 不产生任何内容并且没有门控。
                ss.untrusted = true;
                ss.error = "untrusted project config — approve to enable";
            } else if (auto eit = errors.find(name); eit != errors.end()) {
                ss.error = eit->second;
            }
        }

        // 工具：来自LIVE广告组的权威（因此禁用的
        // 工具永远不会消失）；启用源自配置排除。
        // （直通已从上面的声明中填充了 ss.tools。）
        if (cs.type == "passthrough") {
            // 没有现场 池，没有 s_seen — 声明是全部事实
        } else if (lit != live.end()) {
            for (const auto& lt : lit->second) {
                ToolState ts;
                ts.name        = lt.bare;
                ts.description = lt.desc;
                ts.enabled     = !cs.exclude.contains(lt.bare);
                ts.pinned      = cs.pin.contains(lt.bare);
                ss.tools.push_back(std::move(ts));
            }
        } else {
            // 此时未连接（故意禁用，或在重新加载之间暂时禁用）。
            // 回退到该服务器 最后 所宣传的内容，以便其工具 源码树
            // 保持填充状态，而不是在您禁用插件时崩溃——这正是一种
            // 不一致的情况，即 just-disabled 服务器丢失
            // 了其工具，而 start-disabled 一个或单独
            // 的 -excluded 集保留了它们。
            if (auto sit = seen.find(name); sit != seen.end()) {
                for (const auto& [bare, desc] : sit->second.tools) {
                    ToolState ts;
                    ts.name        = bare;
                    ts.description = desc;
                    ts.enabled     = !cs.exclude.contains(bare);
                    ss.tools.push_back(std::move(ts));
                }
            }
        }
        // 服务器不再通告的已禁用工具（e.g。排除得太厉害，
        // 服务器将其丢弃）-从配置中显示了 归并。
        // (跳过直通：其声明通过构造完成，
        // 并且每个排除的名称已经呈现。)
        if (cs.type != "passthrough")
        for (const auto& ex : cs.exclude) {
            if (ex.empty()) continue;   // 杂散""排除→不添加空白行
            bool present = false;
            for (const auto& ts : ss.tools) if (ts.name == ex) present = true;
            if (!present) {
                ToolState ts; ts.name = ex; ts.enabled = false;
                ss.tools.push_back(std::move(ts));
            }
        }
        std::sort(ss.tools.begin(), ss.tools.end(),
                  [](const ToolState& a, const ToolState& b){ return a.name < b.name; });
        model.servers.push_back(std::move(ss));
    }
    std::sort(model.servers.begin(), model.servers.end(),
              [](const ServerState& a, const ServerState& b){ return a.name < b.name; });
    // 哪些工具加载以及哪些是按需的取决于提供商和设置；
    // tools::tool_loading::annotate 填写 UI。
    // 提示词为作曲家的`/`菜单，与ACP发送相同的提示形状。它们显示为
    // `server:prompt`，比 传输 名称 `mcp__server__prompt`
    // 读取更好，输入速度更快；两者都解析为相同的 提示词。
    for (const auto& pr : mcp_prompts()) {
        if (pr.name.empty()) continue;
        PromptState ps;
        ps.name = short_prompt_name(pr.name);
        ps.description = !pr.description.empty() ? pr.description
                       : !pr.title.empty()       ? pr.title
                                                 : "Prompt from " + pr.server;
        for (const auto& arg : pr.arguments) {
            if (!ps.hint.empty()) ps.hint += ' ';
            ps.hint += arg.required ? "<" + arg.name + ">" : "[" + arg.name + "]";
        }
        model.prompts.push_back(std::move(ps));
    }
    return model;
}

std::size_t mcp_reload() {
    // 从 当前 配置重建 池。 mcp_tools() 读取
    // 新的配置，生成每个列出的服务器，并将新的 池 安装为
    // g_pool_ref（替换旧的）。我们在这里扔掉返回的
    // ToolDefs — 注册表 在下次刷新时从 池 重新获得
    // -projects；重要的是g_pool_ref现在
    // 指向重载的池，并且它的世代不是-zero。
    //
    // 生成：新的池从0开始，但是refresh_wire_cache_locked
    // 将生成==0视为"use the cached initial_mcp snapshot"
    // ——因此重新加载的池 MUST报告非-zero生成以通过实时投影进行路由。
    // 在 mcp_tools() 连接之前将其种子设置为 1（从那里开始
    // 的任何后续 list_changed 碰撞）。
    PoolHandle throwaway;
    (void)mcp_tools(throwaway);
    if (auto p = current_pool()) {
        unsigned long g = p->generation.load(std::memory_order_relaxed);
        if (g == 0) p->generation.store(1, std::memory_order_relaxed);
        return p->registry.provider_count();
    }
    return 0;
}

std::vector<ResourceInfo> mcp_resources() {
    auto pool = current_pool();
    if (!pool) return {};
    std::vector<ResourceInfo> out;
    for (const auto& r : pool->registry.resources()) {
        ResourceInfo ri;
        ri.uri         = r.uri;
        ri.name        = r.name;
        ri.title       = r.title.has_value() ? *r.title : r.name;
        ri.description = r.description.has_value() ? *r.description : std::string{};
        ri.mime_type   = r.mimeType.has_value() ? *r.mimeType : std::string{};
        out.push_back(std::move(ri));
    }
    return out;
}

std::optional<std::string> mcp_read_resource(const std::string& uri, std::string& err) {
    auto pool = current_pool();
    if (!pool) { err = "MCP not configured"; return std::nullopt; }
    std::vector<::mcp::ResourceContents> contents;
    {
        if (!pool->registry.read_resource(uri, contents, err)) return std::nullopt;
    }
    std::string out;
    for (const auto& c : contents) {
        std::visit([&](const auto& rc) {
            using T = std::decay_t<decltype(rc)>;
            if constexpr (std::is_same_v<T, ::mcp::TextResourceContents>) {
                out += rc.text;
                if (!out.empty() && out.back() != '\n') out += '\n';
            } else {
                out += "[blob " +
                       (rc.mimeType.has_value() ? *rc.mimeType : std::string{"application/octet-stream"}) +
                       ", ~" + std::to_string(rc.blob.size()) + "B base64]\n";
            }
        }, c);
    }
    return out;
}

std::vector<PromptInfo> mcp_prompts() {
    auto pool = current_pool();
    if (!pool) return {};
    std::vector<PromptInfo> out;
    for (const auto& p : pool->registry.prompts()) {
        PromptInfo pi;
        pi.name        = canonical_mcp_name(p.name);
        pi.server      = mcp_origin_id(p.name);
        pi.title       = p.title.has_value() ? *p.title : p.name;
        pi.description = p.description.has_value() ? *p.description : std::string{};
        if (p.arguments.has_value())
            for (const auto& a : *p.arguments)
                pi.arguments.push_back(PromptArgInfo{
                    a.name,
                    a.description.has_value() ? *a.description : std::string{},
                    a.required.has_value() && *a.required});
        out.push_back(std::move(pi));
    }
    return out;
}

std::optional<std::string> mcp_get_prompt(
    const std::string& name,
    const std::vector<std::pair<std::string, std::string>>& args,
    std::string& err);

std::optional<std::string> mcp_expand_prompt(std::string_view t, bool* usage_out) {
    if (!t.starts_with('/')) return std::nullopt;
    const auto sp = t.find_first_of(" \t\n");
    const std::string typed{t.substr(1, sp == std::string_view::npos ? sp : sp - 1)};
    std::string_view rest = sp == std::string_view::npos ? std::string_view{} : t.substr(sp + 1);
    std::vector<PromptInfo> prompts;
    try { prompts = mcp_prompts(); } catch (...) { return std::nullopt; }
    // 拼写：传输名称，或者菜单显示的`server:prompt`。
    const auto it = std::ranges::find_if(prompts, [&](const auto& p) {
        return p.name == typed || short_prompt_name(p.name) == typed;
    });
    if (it == prompts.end()) return std::nullopt;
    const std::string name = it->name;
    std::vector<std::pair<std::string, std::string>> args;
    // 单词在任何空格或制表符上分割（粘贴的行可以携带其中之一）。
    constexpr std::string_view ws = " \t";
    for (std::size_t i = 0; i < it->arguments.size(); ++i) {
        while (!rest.empty() && ws.contains(rest.front())) rest.remove_prefix(1);
        while (!rest.empty() && ws.contains(rest.back())) rest.remove_suffix(1);
        if (rest.empty()) break;
        std::string_view v;
        if (i + 1 == it->arguments.size()) { v = rest; rest = {}; }
        else {
            const auto e = rest.find_first_of(ws);
            v = rest.substr(0, e);
            rest = e == std::string_view::npos ? std::string_view{} : rest.substr(e + 1);
        }
        args.emplace_back(it->arguments[i].name, std::string{v});
    }
    // 没有留下任何单词的必需参数是使用错误，而不是服务器故障。
    // 使用用法行进行回答：round-tripping 到服务器只会
    // 获得 "missing required prompt arguments"，
    // 它既没有指定参数也没有指定行的形状。一行，
    // 因为调用者只是祝酒而不是发送它。
    std::string missing, usage;
    for (const auto& a : it->arguments) {
        if (!usage.empty()) usage += ' ';
        usage += a.required ? "<" + a.name + ">" : "[" + a.name + "]";
        if (!a.required) continue;
        if (std::ranges::none_of(args, [&](const auto& kv) { return kv.first == a.name; })) {
            if (!missing.empty()) missing += ", ";
            missing += a.name;
        }
    }
    if (!missing.empty()) {
        // `typed` 是用户所写的内容（无论是拼写），因此请回显该内容。
        const std::string spelled = typed.empty() ? short_prompt_name(name) : typed;
        if (usage_out) *usage_out = true;
        return "/" + spelled + " needs " + missing + " \xe2\x80\x94 try /" + spelled +
               " " + usage;
    }
    std::string err;
    auto out = mcp_get_prompt(name, args, err);
    // 按照菜单和作曲家的方式命名 提示词，而不是 传输 名称。
    if (!out) return "The /" + short_prompt_name(name) + " prompt failed: " + err;
    return out;
}

std::optional<std::string> mcp_get_prompt(
    const std::string& name,
    const std::vector<std::pair<std::string, std::string>>& args,
    std::string& err) {
    auto pool = current_pool();
    if (!pool) { err = "MCP not configured"; return std::nullopt; }
    ::mcp::GetPromptResult res;
    {
        auto route = resolve_prompt_route(pool->registry, name);
        if (!route) { err = "prompt is missing or ambiguous: '" + name + "'"; return std::nullopt; }
        if (!pool->registry.get_prompt(*route, args, res, err)) return std::nullopt;
    }
    return render_prompt(res);
}

void release_servers() noexcept {
    // 退出时删除进程-wide 池 PROMPTLY，
    // 而不是在main()返回后将其留作静态销毁。两场胜利：
    //   1.当应用程序仍然 "alive" 且可见时发生拆卸，而不是
    //      在不透明静态-dtor阶段期间，挂起看起来像是整个过程楔入（
    //      "first Ctrl-C doesn't quit"症状）。
    //   2.子MCP服务器现在关闭其标准输入+SIGTERM；一个
    //      well-behaved stdio服务器在约1ms内于EOF退出，
    //      因此常见情况是即时的。楔形的仍受 ChildProcess 自己的 SIGTERM
    //      →SIGKILL 截止日期的限制。
    // 将句柄交换到 锁 下，然后让 注册表 析构函数运行
    // OUTSIDE 和 锁 (它终止每个子进程)。
    PoolHandle drained = g_pool().take();
    // `drained` 落入此处：注册表→提供商→ChildProcess 拆解。
}

} // namespace agentty::mcp
