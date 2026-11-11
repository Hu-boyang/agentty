// agentty::provider-外部ACP代理注册表(配置表面)。
// 格式+解析规则参见acp_agents.hpp。

#include "agentty/provider/acp_agents.hpp"
#include "agentty/util/user_root.hpp"

#include <maya/runtime.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>

#include <nlohmann/json.hpp>

namespace agentty::provider {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

// 外部ACP代理完全是config-driven (Zed的
// `agent_servers`模型）。 ACP 是通用的 传输层； agentty
// 不授予或安装特定代理实现的权限。用户通过将其命令命名为 acp-agents.json
// 来选择使用任何外部代理。

// 解析配置文件路径+是否为工作区-local（不可信）。
// 镜子mcp::resolve_config:$AGENTTY_ACP_AGENTS > ~/.agentty > ./.agentty.
fs::path resolve_config(bool& out_project_local) {
    out_project_local = false;
    std::error_code ec;
    if (const char* e = std::getenv("AGENTTY_ACP_AGENTS"); e && e[0]) {
        fs::path p{e};
        return fs::is_regular_file(p, ec) ? p : fs::path{};
    }
    if (fs::path root = ::agentty::util::user_root(); !root.empty()) {
        fs::path p = root / "acp-agents.json";
        if (fs::is_regular_file(p, ec)) return p;
    }
    fs::path proj = fs::path{".agentty"} / "acp-agents.json";
    if (fs::is_regular_file(proj, ec)) { out_project_local = true; return proj; }
    return {};
}

[[nodiscard]] bool env_truthy(const char* name) noexcept {
    const char* e = std::getenv(name);
    return e && (e[0] == '1' || e[0] == 't' || e[0] == 'T' || e[0] == 'y' || e[0] == 'Y');
}

// 解析将整个配置文件放入由id键入的`out`中。
// 在丢失/不受信任/格式错误的配置上返回 false（并保持不变）
// -built-in 默认值仍然可以满足查找。
bool load_config_uncached(std::vector<AcpAgentSpec>& out) {
    bool project_local = false;
    fs::path cfg = resolve_config(project_local);
    if (cfg.empty()) return false;

    // 不受信任的-workspace生成门（与.agentty/mcp.json相同的策略）：
    // 项目-local配置可以骑在克隆的仓库上并生成任意命令，
    // 因此需要显式的opt-in。
    if (project_local && !env_truthy("AGENTTY_ACP_ALLOW_PROJECT")) {
        std::fprintf(stderr,
            "acp: ignoring workspace-local %s (it can spawn arbitrary\n"
            "     commands). Set AGENTTY_ACP_ALLOW_PROJECT=1 to enable it, or\n"
            "     move trusted agents to ~/.agentty/acp-agents.json.\n",
            cfg.string().c_str());
        return false;
    }

    json doc;
    try {
        std::ifstream f(cfg);
        f >> doc;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "acp: failed to parse %s: %s\n",
                     cfg.string().c_str(), e.what());
        return false;
    }

    const json* agents = nullptr;
    if (doc.contains("acpAgents") && doc["acpAgents"].is_object())
        agents = &doc["acpAgents"];
    else if (doc.contains("agents") && doc["agents"].is_object())
        agents = &doc["agents"];
    if (!agents) return false;

    auto as_str = [](const json& v) {
        return v.is_string() ? v.get<std::string>() : v.dump();
    };
    for (auto it = agents->begin(); it != agents->end(); ++it) {
        const json& spec = it.value();
        AcpAgentSpec a;
        a.id      = it.key();
        a.command = spec.value("command", std::string{});
        if (a.command.empty()) {
            std::fprintf(stderr, "acp: agent '%s' has no \"command\" — skipping\n",
                         a.id.c_str());
            continue;
        }
        if (spec.contains("args") && spec["args"].is_array())
            for (const auto& x : spec["args"]) a.args.push_back(as_str(x));
        if (spec.contains("env") && spec["env"].is_object())
            for (auto e = spec["env"].begin(); e != spec["env"].end(); ++e)
                a.env.emplace_back(e.key(), as_str(e.value()));
        a.cwd = spec.value("cwd", std::string{});
        out.push_back(std::move(a));
    }
    return true;
}

// 缓存了front-door。 Provider-picker VIEW 调用
// enumerate_acp_agents() 每次渲染 帧 一次，is_acp_agent_id
// () 位于 parse_selection 的路径上 — re-reading + JSON
// -parsing acp-agents.json 每次都是每个 帧 的磁盘工作。将解析的向量键入（解析路径、
// mtime、大小）：统计数据约为 1μs，并进行编辑、删除和 env-var 重定向等操作。
// 以纯值（路径字符串、实时时间刻度）为关键字。从视图代码中读取，
// 因此 锁-free 快照：不需要任何功能。
struct ConfigCache {
    std::vector<AcpAgentSpec> agents;
    bool                      ok = false;
    std::string               path;
    std::int64_t              mtime = 0;
    std::uintmax_t            size = 0;
};
maya::snapshot<ConfigCache>& config_cache() {
    static maya::snapshot<ConfigCache> c;
    return c;
}

bool load_config(std::vector<AcpAgentSpec>& out) {
    bool project_local = false;
    fs::path cfg = resolve_config(project_local);

    std::error_code ec;
    fs::file_time_type mtime{};
    std::uintmax_t size = 0;
    if (!cfg.empty()) {
        mtime = fs::last_write_time(cfg, ec);
        size  = ec ? 0 : fs::file_size(cfg, ec);
    }
    // 命中：将缓存列表复制出当前快照。
    const std::string  key_path  = cfg.string();
    const std::int64_t key_mtime = mtime.time_since_epoch().count();
    std::optional<std::pair<bool, std::vector<AcpAgentSpec>>> hit;
    {
        const auto c = config_cache().load();
        if (c->path == key_path && c->mtime == key_mtime && c->size == size)
            hit = std::pair{c->ok, c->agents};
    }
    if (!hit) {
        // 小姐：解析(盘+JSON)，则发布。
        ConfigCache fresh;
        fresh.ok    = load_config_uncached(fresh.agents);
        fresh.path  = key_path;
        fresh.mtime = key_mtime;
        fresh.size  = size;
        hit = std::pair{fresh.ok, fresh.agents};
        config_cache().store(std::move(fresh));
    }
    if (!hit->first) return false;
    out.insert(out.end(), hit->second.begin(), hit->second.end());
    return true;
}

} // 命名空间结束。

std::optional<AcpAgentSpec> resolve_acp_agent(std::string_view id) {
    std::vector<AcpAgentSpec> cfg;
    load_config(cfg);
    for (auto& a : cfg)
        if (a.id == id) return std::move(a);
    return std::nullopt;
}

bool is_acp_agent_id(std::string_view id) noexcept {
    std::vector<AcpAgentSpec> cfg;
    load_config(cfg);
    for (const auto& a : cfg)
        if (a.id == id) return true;
    return false;
}

std::vector<AcpAgentSpec> enumerate_acp_agents() {
    std::vector<AcpAgentSpec> out;
    load_config(out);
    return out;
}

} // namespace agentty::provider
