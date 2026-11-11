#include "agentty/tool/registry.hpp"

#include "agentty/mcp/client.hpp"
#include "agentty/tool/mcp_tools_bridge.hpp"
#include "agentty/tool/web_search_policy.hpp"
#include "agentty/tool/tool_loading.hpp"
#include "agentty/tool/tool_plan.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <format>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace agentty::tools {

std::string_view to_string(ErrorKind k) noexcept {
    switch (k) {
        case ErrorKind::InvalidArgs:    return "invalid args";
        case ErrorKind::NotFound:       return "not found";
        case ErrorKind::NotAFile:       return "not a file";
        case ErrorKind::NotADirectory:  return "not a directory";
        case ErrorKind::TooLarge:       return "too large";
        case ErrorKind::Binary:         return "binary";
        case ErrorKind::Ambiguous:      return "ambiguous";
        case ErrorKind::NoMatch:        return "no match";
        case ErrorKind::InvalidRegex:   return "invalid regex";
        case ErrorKind::Network:        return "network";
        case ErrorKind::Spawn:          return "spawn failed";
        case ErrorKind::Subprocess:     return "subprocess failed";
        case ErrorKind::Io:             return "io";
        case ErrorKind::OutOfWorkspace: return "out of workspace";
        case ErrorKind::Denied:         return "denied";
        case ErrorKind::Unknown:        return "unknown";
    }
    return "unknown";
}

std::string ToolError::render() const {
    return std::format("[{}] {}", to_string(kind), detail);
}

std::string_view to_string(Effect e) noexcept {
    switch (e) {
        case Effect::ReadFs:  return "ReadFs";
        case Effect::WriteFs: return "WriteFs";
        case Effect::Net:     return "Net";
        case Effect::Exec:    return "Exec";
    }
    return "?";
}

std::string to_string(EffectSet e) {
    if (e.empty()) return "Pure";
    std::string out;
    auto add = [&](Effect bit) {
        if (!e.has(bit)) return;
        if (!out.empty()) out += ", ";
        out += to_string(bit);
    };
    add(Effect::Exec);
    add(Effect::WriteFs);
    add(Effect::Net);
    add(Effect::ReadFs);
    return out;
}

namespace {

// 集合每个工具。 命令事项: 协议将集视为未排序,
// 但模型对更早的重现偏差 -listed 工具。
// 设置 `edit` 提前 `write` 最便宜的单一块块块
// 阻止模型在目标替代时 重写整个文件 —— 编辑的微小
// input_json_delta 正文 绕过长中路 -stream
// 暂停 Anthropic 边缘适用于多 -KB tool_use 内容。
// 集合本地工具集。 执行程序以 mcp-cpp 电池 -included 工具集( 选项)
// mcp::tools::make_provider): build_mcp_tool_defs (a) 调用
// -wraps 每个被公布的工具都作为 ToolDef 其 人 名 execute() 发送到提供者并读取
// 带类型的 副作用 + FileChange 关闭每个结果返回到 ToolOutput。东道主 -coupled
// 宿主保留的外壳工具，包括 remember/forget/wipe/todo/skill/search_docs/task，
// ) 由下列因素支持: agentty 通过下列途径注入的适配器 HostServices.
// mcp-cpp 是那个 SOLE 工具的真相来源——没有本土路径.
std::vector<ToolDef> build_native_registry() {
    return build_mcp_tool_defs();
}

// 连接外部 MCP 一次,第一次,我问他 目录 访问。
// 返回 ToolDefs 不属于 不可变 本土基
// 线:每一代从 MCP 池 当前的权威性 快照.
std::vector<ToolDef> connect_initial_mcp() {
    if (!mcp::mcp_config_present()) return {};
    static mcp::PoolHandle s_pool;
    return mcp::mcp_tools(s_pool);
}

} // 命名空间结束。

const std::vector<ToolDef>& native_registry() {
    static const std::vector<ToolDef> r = build_native_registry();
    return r;
}

namespace {
// 一块 目录. 不可变 已出版; 读者持有 目录
// 只要它们使用一个 ToolDef ,所以重建永远
// 不会使任何东西失效 旧建筑与它的最后一个读者死亡。
struct Snapshot {
    Catalog tools;
    std::unordered_map<std::string, std::size_t> idx;   // 名称 - > *工具中的索引
    unsigned long generation = 0;
    bool stale = false;   // 标记通过无效发布; 从未服务
};

enum class Phase { Idle, Connecting, Connected };
// 锁-free : 数据 目录 各地的教诲, 锁 正文 ,
// 工具 工作线程 ),所以连接的主张是一个原子,
// 第一个连接的结果公布一次。 目录 路径。
std::atomic<Phase>& connect_phase() { static std::atomic<Phase> p{Phase::Idle}; return p; }
// 两个槽都漏掉了: 一个工具 工作线程 被关闭的恩典所抛弃的,
// 在静态被撕裂的时候,他们仍然会读到它们。他们所要看到的,
// 是重新计算,用最后的读物来读取,所以这是其中的一本。
// 分配 而不是不断增长的链条
maya::published<const std::vector<ToolDef>>& initial_mcp() {
    static auto* s = new maya::published<const std::vector<ToolDef>>;
    return *s;
}
maya::published<const Snapshot>& catalog() {
    static auto* c = new maya::published<const Snapshot>;
    return *c;
}

std::shared_ptr<const Snapshot> make_snapshot(std::vector<ToolDef> tools, unsigned long generation) {
    // tool_search 总是可以发送的(#98)。它是否骑着 传输
    // 是计划员的 调用 ; 是否为 RUNS 一定不取决于寄出什么 或
    // 看到它最后转弯的模型 "unknown tool".
    if (std::none_of(tools.begin(), tools.end(), [](const ToolDef& t) {
            return t.name.value == plan::kSearchToolName; }))
        tools.push_back(tool_loading::search_tool());
    std::unordered_map<std::string, std::size_t> idx;
    idx.reserve(tools.size());
    for (std::size_t i = 0; i < tools.size(); ++i) idx.emplace(tools[i].name.value, i);
    return std::make_shared<const Snapshot>(
        Snapshot{Catalog::make(std::move(tools)), std::move(idx), generation, false});
}

// 服务于另一个人 线程 中间 -connect 所以没人会等着你
std::shared_ptr<const Snapshot> native_only() {
    static const std::shared_ptr<const Snapshot> s = make_snapshot(native_registry(), 0);
    return s;
}

// 是否为此 调用 可能是连接外部的 MCP (每个配置的服务器都喷发,
// 最多15秒). find() 调用自 状态更新函数 自动
// 调试 UI 线程 (工具排程, 权限 提示词 ),
// 所以绝对不是打电话者连接:它冻结了 UI 等它被打倒的时候,
// 它会很温暖 -up 任务,以获取第一个访问。 find()
// 和任何来电的中途一样 -connect。其他访问可以连接:
// 打开 ACP 财务报告和已审计财务报表 `agentty run` 第一次 第一次 wire_tools (转)
// 工作线程 是什么带来 MCP 起来
enum class Connect { May, Never };

std::shared_ptr<const Snapshot> refresh_wire_cache(Connect may = Connect::May) {
    // 第一个呼叫者连接外部 MCP ; 任何同时到达的
    // 人立即得到本地工具(可能是 UI 线程 ,
    // 而连接可达15秒。
    enum class Claim { Mine, Busy, Done };
    const Claim claim = [may] {
        Phase expect = Phase::Idle;
        if (may == Connect::Never) {
            expect = connect_phase().load();
            return expect == Phase::Connected ? Claim::Done : Claim::Busy;
        }
        if (connect_phase().compare_exchange_strong(expect, Phase::Connecting))
            return Claim::Mine;
        return expect == Phase::Connected ? Claim::Done : Claim::Busy;
    }();
    if (claim == Claim::Busy) return native_only();
    if (claim == Claim::Mine) {
        // 继续继续 任意 将权利要求和连接(连接或
        // 发布)之间丢弃,或不再拖动 调用 永远连接。
        try {
            auto ext = connect_initial_mcp();
            initial_mcp().publish(
                std::make_shared<const std::vector<ToolDef>>(std::move(ext)));
        } catch (...) {
            connect_phase().store(Phase::Idle);
            throw;
        }
        connect_phase().store(Phase::Connected);
    }

    for (;;) {
        auto cur = catalog().current();
        const unsigned long generation = mcp::mcp_generation();
        if (cur && !cur->stale && cur->generation == generation) return cur;

        std::vector<ToolDef> external;
        if (generation != 0 && may == Connect::Never && cur && !cur->stale) {
            // 仅移动的一代: 服务器已发送 list_changed ,
            // 重建将会运行 tools/list 上面,那个 UI
            // 线程 等不及了,它留着这个 快照 下一个 工作线程
            // 访问重建。 (一个明确的无效, 一个切换的工具,
            // 仍然在这里重建: 读取缓存列表, 没有网络。)
            return cur;
        }
        if (generation == 0) {
            if (auto init = initial_mcp().current()) external = *init;
        } else {
            external = mcp::mcp_tools_live();
        }

        std::vector<ToolDef> tools;
        tools.reserve(native_registry().size() + external.size());
        tools.insert(tools.end(), native_registry().begin(), native_registry().end());

        // 外部名称被命名为空格,但仍会丢掉重复,而不是将模棱
        // 两可的模式定义发送给一个提供者.
        std::unordered_map<std::string, bool> names;
        names.reserve(tools.capacity());
        for (const auto& tool : tools) names.emplace(tool.name.value, true);
        for (auto& tool : external) {
            if (!names.emplace(tool.name.value, true).second) continue;
            tools.push_back(std::move(tool));
        }
        auto out = make_snapshot(std::move(tools), generation);
        if (catalog().publish_if(cur, out)) return out;
        // 其他人发表(重建或无效);re-check.
    }
}

// 标记当前 目录 过期 所以下一个通道重建了它
void invalidate_catalog() {
    for (;;) {
        auto cur = catalog().current();
        if (!cur) return;
        auto marker = std::make_shared<const Snapshot>(
            Snapshot{cur->tools, cur->idx, cur->generation, /*stale=*/true});
        if (catalog().publish_if(cur, std::move(marker))) return;
    }
}
} // 命名空间结束。

// 遗留工具 -name 异名 – 犬科 注册表 名称。执行工具被重新命名为
// shash ~ shell, 但是 `bash` 模式培训数据中占主导
// 地位(在 Claude 代码的实际工具名称——通常作为 "Bash" ,
// 资本B),该模型可靠 调用 上面的旧名 首次 转身,在它内化我们 模式
// 定义 没有这个 调用 404s ("unknown tool: Bash" ),
// 模型烧掉了一个浪费的失败 调用 ,则 重试 以 `shell` .
// 此处的加固—— 一个 查找每个 分派 路径(代理环路、子代理 ACP ,
// mcp 数据 -serve, 权限 ——吸收误火,
// 故遗留名称才有效.
//
// 比赛是 CASE-INSENSITIVE 和空白空间 -trimmed : 模型
// 发射 "bash", "Bash" ,和 "BASH" 交替,准确的 -case
// 检查是错误让 "Bash" 滑到一个艰难的失败。
[[nodiscard]] static std::string_view canonical_tool_name(std::string_view name) noexcept {
    // 绕行白空间而不分配。
    auto b = name.find_first_not_of(" \t\r\n");
    if (b == std::string_view::npos) return name;
    auto e = name.find_last_not_of(" \t\r\n");
    std::string_view trimmed = name.substr(b, e - b + 1);
    auto ieq = [](std::string_view s, std::string_view lit) {
        if (s.size() != lit.size()) return false;
        for (std::size_t i = 0; i < s.size(); ++i) {
            char c = s[i];
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            if (c != lit[i]) return false;
        }
        return true;
    };
    if (ieq(trimmed, "bash")) return "shell";
    // 已取消名称 `mcp_` 前缀 Anthropic 拒绝;
    // 已保存 线程 习惯还原 调用 旧名.
    if (ieq(trimmed, "mcp_read_resource")) return "mcp__resources__read";
    if (ieq(trimmed, "mcp_get_prompt")) return "mcp__prompts__get";
    return name;
}

std::string unknown_tool_error(std::string_view name) {
    std::string msg = "unknown tool: " + std::string{name}
        + ". This tool is not part of agentty's toolset — if a proxy or "
          "gateway advertised it, call it through that channel, not here. "
          "Available tools:";
    for (const auto& td : *wire_tools()) {
        msg += ' ';
        msg += td.name.value;
        msg += ',';
    }
    if (msg.back() == ',') msg.pop_back();
    msg += '.';
    return msg;
}

Found find(std::string_view name) {
    const auto snapshot = refresh_wire_cache(Connect::Never);
    if (auto it = snapshot->idx.find(std::string{name}); it != snapshot->idx.end())
        return Found{snapshot->tools, &(*snapshot->tools)[it->second]};
    // 女士:重试曾经在犬科名下,所以一个遗产别名解决.
    if (auto canon = canonical_tool_name(name); canon != name)
        if (auto it = snapshot->idx.find(std::string{canon}); it != snapshot->idx.end())
            return Found{snapshot->tools, &(*snapshot->tools)[it->second]};
    return Found{};
}

Catalog wire_tools() { return refresh_wire_cache()->tools; }

bool withheld_by_policy(const ToolDef& tool, bool web_search_enabled) {
    return !web_search_enabled && tool.origin == ToolOrigin::Native
        && tool.name.value == "web_search";
}

bool withheld_by_policy(const ToolDef& tool) {
    return withheld_by_policy(
        tool, web_search_cfg::mode(web_search_policy::current()) != web_search_cfg::Mode::Off);
}

unsigned long mcp_generation() noexcept {
    return mcp::mcp_generation();
}

std::size_t reload_mcp_plugins() {
    // 一次重装一次。 每个插件 切换 自己开的 线程 重新连接需要几秒钟,
    // 所以请求 降落在中点 -reload 刚刚设置 `pending` 返回;
    // 运行中的重新加载循环以再次抓取更新的配置。
    // 正在检查 `pending` 和清除 `running`
    // 发生在一个 CAS ,所以没有丢失请求。
    // 两者 标志 在一个原子词里生活 所以每个步骤都是单数 CAS
    // 且无 锁 这条道路上需要(或能力).
    constexpr unsigned kRunning = 1u, kPending = 2u;
    static std::atomic<unsigned> state{0};

    unsigned cur = state.load();
    for (;;) {
        const unsigned want = (cur & kRunning) ? (cur | kPending) : kRunning;
        if (state.compare_exchange_weak(cur, want)) break;
    }
    if (cur & kRunning) return 0;   // 别人在重新装填,他们会循环

    // 重新装入的抛出物仍必须清除`running`,
    // 或者每次后来重新装入物时都会看到在进行中,返回时不做任何事情.
    struct Clear {
        std::atomic<unsigned>& s;
        bool armed = true;
        ~Clear() { if (armed) s.store(0); }
    } clear{state};

    std::size_t n = 0;
    for (;;) {
        state.fetch_and(~kPending);
        n = mcp::mcp_reload();
        invalidate_catalog();
        unsigned s = state.load();
        bool again = false;
        for (;;) {
            again = (s & kPending) != 0;
            if (again) break;   // 继续运行; 下一个通过 待处理
            if (state.compare_exchange_weak(s, s & ~kRunning)) break;
        }
        if (!again) break;
    }
    clear.armed = false;
    return n;
}

void invalidate_mcp_catalog(Io) {
    // 没有 -spawn : 撞伤 池 生成并标记 目录 过期.
    // project_tools 内容 -reads tools.exclude 重建,
    // 所以一个 enable/disable 切换 降落在下一个访问中,没有服务器的 churn。
    mcp::mcp_bump_generation();
    invalidate_catalog();
}

} // namespace agentty::tools
