// agentty::tools::tool_loading - 看见了 tool_loading.hpp.

#include "agentty/tool/tool_loading.hpp"

#include <cctype>
#include <cstdlib>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>

#include "agentty/domain/catalog.hpp"
#include "agentty/io/persistence.hpp"
#include "agentty/mcp/client.hpp"
#include "agentty/provider/selection.hpp"
#include "agentty/tool/subagent.hpp"
#include "agentty/util/snapshot.hpp"
#include "agentty/util/dbglog.hpp"

namespace agentty::tools::tool_loading {

namespace {

util::AtomicSnapshot<plan::Loading>& slot() {
    static util::AtomicSnapshot<plan::Loading> s;
    return s;
}

// 默认情况下, 最多是哪个搜索返回。
constexpr std::size_t kDefaultHits = 5;
constexpr std::size_t kMaxHits = 20;

ExecResult run_search(const nlohmann::json& args) {
    std::string query;
    if (args.is_object() && args.contains("query") && args["query"].is_string())
        query = args["query"].get<std::string>();
    if (query.empty())
        return std::unexpected(ToolError::invalid_args(
            "query is required: words describing the task, or select:<name>[,<name>]"));
    std::size_t k = kDefaultHits;
    if (args.is_object() && args.contains("limit") && args["limit"].is_number_integer())
        k = static_cast<std::size_t>(std::clamp<long long>(args["limit"].get<long long>(),
                                                           1, static_cast<long long>(kMaxHits)));

    const Catalog cat = wire_tools();
    std::vector<ToolDef> searchable;
    searchable.reserve(cat->size());
    for (const auto& t : *cat)
        if (t.origin != ToolOrigin::Native && t.advertise && t.skipped.empty() && !withheld_by_policy(t)
            && t.name.value != plan::kSearchToolName)
            searchable.push_back(t);
    const auto hits = plan::search(searchable, query, k);

    ToolOutput out;
    if (hits.empty()) {
        out.text = "No tools matched \"" + query + "\". Try other words, the server's "
                   "name, or select:<exact_name>. " + std::to_string(searchable.size())
                 + " tools are searchable.";
        return out;
    }
    // 头文件 ,然后一个 `loaded:` 标记是 线程 读取回( R)
    // plan::loaded_in 保持这些工具在 传输 ,以及什么是
    // Anthropic 传输 变成 tool_reference 每一个被击中的
    // 物体都带有 模式定义 也一样,所以 传输 没办法 加载 工具中间 -turn
    // 还是让模特儿来 调用 立即用名字写出来
    out.text = std::to_string(hits.size()) + (hits.size() == 1 ? " tool" : " tools")
             + " loaded for this conversation. Call them by name.\n";
    for (const auto& h : hits) {
        const auto& t = *h.tool;
        std::string desc = t.description;
        if (const auto nl = desc.find('\n'); nl != std::string::npos) desc.resize(nl);
        if (desc.size() > 200) desc = desc.substr(0, 197) + "...";
        out.text += std::string{plan::kLoadedMarker} + t.name.value + " - " + desc + "\n";
        out.text += "  schema: "
                  + (t.input_schema.is_null() ? std::string{"{}"} : t.input_schema.dump()) + "\n";
    }
    return out;
}

} // 命名空间结束。

void seed(Io io) {
    if (slot().load()) return;
    plan::Loading l = plan::Loading::Auto;
    try {
        l = plan::loading_from(persistence::load_settings(io).tool_loading);
    } catch (const std::exception& e) {
        util::dbglog("tool_loading.seed", e.what());
    } catch (...) {
        util::dbglog("tool_loading.seed", "non-std exception");
    }
    (void)slot().store_if_empty(std::move(l));
}

plan::Loading current() {
    const auto s = slot().load();
    return s ? *s : plan::Loading::Auto;
}

void install(plan::Loading l) { slot().store(std::move(l)); }

const ToolDef& search_tool() {
    static const ToolDef def = [] {
        ToolDef d;
        d.name.value = std::string{plan::kSearchToolName};
        d.description =
            "Find and load more tools. Many MCP tools are not loaded yet to save "
            "context; search by what you want to do (any language, typos are "
            "fine), a server name, or select:<exact_name>[,<name>] to load known "
            "names. Loaded tools stay available for the rest of the conversation. "
            "Search before telling the user a capability is missing.";
        d.input_schema = {
            {"type", "object"},
            {"properties", {
                {"query", {{"type", "string"},
                           {"description", "What you need, e.g. \"decompile a function\", "
                                           "\"slack\", or \"select:mcp__gh__list_prs\"."}}},
                {"limit", {{"type", "integer"},
                           {"description", "How many tools to load (default 5, max 20)."}}},
            }},
            {"required", {"query"}},
        };
        d.origin = ToolOrigin::Mcp;      // 非本地人:计划员加的
        d.origin_id = "catalog";
        d.effects = EffectSet{};         // 纯度: 只读 目录
        d.scheduling_effects = d.effects;
        d.max_output_chars = 30'000;
        d.output_truncation = OutputTruncation::Head;
        d.timeout = std::chrono::milliseconds{10'000};
        d.execute = [](const maya::unlocked&, const nlohmann::json& args,
                       const CallContext&) -> ExecResult { return run_search(args); };
        return d;
    }();
    return def;
}

plan::Limits limits_for(const provider::Selection& sel, std::size_t context_window,
                        std::string_view model) {
    plan::Limits l;
    l.context_window = context_window;
    // 本地小模型不会可靠地寻找工具;
    // 给他们合适的工具。 同样的家庭列表会隐藏脚枪工具。
    l.search_capable = model.empty() || !is_weak_model(model);
    if (sel.kind == provider::Kind::Anthropic) {
        // Anthropic 使用延期工具(最多10 000)并扩展
        // tool_reference 块本身。 事件订阅 结束点:124个工具,
        // defer_loading 财务报告和已审计财务报表 tool_reference 全部200个
        l.discovery = plan::Discovery::DeferredReferences;
        l.max_tools = 0;
    } else {
        // OpenAI 聊天和 Gemini 拒绝 > 128个工具; OpenAI
        // -compatible 服务器复制了这个形状, 任何模型都无法通过它。
        l.discovery = plan::Discovery::ClientSide;
        l.max_tools = 128;
    }
    // AGENTTY_MCP_TOOL_BUDGET 仍然有效:它覆盖了每架
    // -request 盖上 已加载 工具(0=无上限)。不可解析或负值保留默认值。
    if (const char* e = std::getenv("AGENTTY_MCP_TOOL_BUDGET"); e && e[0]) {
        char* end = nullptr;
        const long v = std::strtol(e, &end, 10);
        if (end && *end == '\0' && v >= 0) l.max_tools = static_cast<std::size_t>(v);
    }
    return l;
}

plan::Plan plan_into(const Request& r, std::vector<provider::ToolSpec>& out) {
    // 计划员在公寓里工作 目录 由呼叫者建造它 池 (已按政策
    // 和 -caller 规则)加 tool_search.
    // 一个工具,它的 模式定义 整个请求都失败了 永远不要出门
    std::vector<ToolDef> flat;
    flat.reserve(r.pool.size());
    std::string skipped_note;
    for (const auto* t : r.pool) {
        if (!t || t->name.value == plan::kSearchToolName) continue;
        if (!t->skipped.empty()) {
            // 说吧,这样模型就可以解释一个缺失的工具而不是猜测(
            // Claude代码做同样的事情).
            if (skipped_note.size() < 1'500)
                skipped_note += "\n- " + t->origin_id + "/" + (t->origin_tool.empty() ? t->name.value : t->origin_tool)
                              + ": " + t->skipped;
            continue;
        }
        flat.push_back(*t);
    }
    ToolDef search = search_tool();
    // 一个无法善加搜索的模特儿 就能抢走一切 Claude
    // 代码和 Codex 模式上的门工具搜索。
    const plan::Loading mode = r.limits.search_capable ? r.mode : plan::Loading::All;
    auto p = plan::make_plan(flat, r.loaded, &search, mode, r.limits);
    // 跳过的工具被命名为模型,以便它能够解释一个缺失的工具:
    // 在tool_search上当它在那里时,其他的插件工具.
    bool skipped_said = false;
    if (p.search) {
        // 说出可以通过服务器找到的东西 由
        // 服务器自己指示 所以模型知道 WHEN 来
        // 搜索而不是说缺少能力。 字节 只有在服务器
        // 完成时才会更改。
        std::map<std::string, std::size_t> by_server;
        std::unordered_set<const ToolDef*> loaded_now;
        for (const auto& w : p.wire)
            if (w.tool && !w.deferred) loaded_now.insert(w.tool);
        for (const auto& t : flat)
            if (t.advertise && !loaded_now.contains(&t)) ++by_server[t.origin_id];
        std::map<std::string, std::string> notes;
        for (auto& [srv, text] : r.server_instructions) notes[srv] = text;
        std::string list;
        for (const auto& [srv, n] : by_server) {
            if (srv.empty()) continue;
            list += "\n- " + srv + " (" + std::to_string(n) + (n == 1 ? " tool)" : " tools)");
            if (auto it = notes.find(srv); it != notes.end()) list += ": " + it->second;
        }
        if (!list.empty())
            search.description += "\n\nServers with tools to find:" + list;
        if (!skipped_note.empty()) {
            search.description += "\n\nLeft out because their input schema is invalid (the "
                                  "server needs fixing):" + skipped_note;
            skipped_said = true;
        }
        for (auto& w : p.wire)
            if (w.tool && w.tool->name.value == plan::kSearchToolName) w.tool = &search;
    }
    for (const auto& w : p.wire) {
        const auto& t = *w.tool;
        std::string desc = t.description;
        if (!skipped_said && !skipped_note.empty() && t.origin == ToolOrigin::Mcp) {
            desc += "\n\n(agentty: some plugin tools were left out because their input schema "
                    "is invalid:" + skipped_note + ")";
            skipped_said = true;
        }
        out.push_back({t.name.value, std::move(desc), t.input_schema,
                       t.eager_input_streaming, w.deferred});
    }
    // 位置指向 `flat` 别把她们交回去
    for (auto& w : p.wire) w.tool = nullptr;
    return p;
}

void annotate(mcp::PluginModel& m) {
    const plan::Loading mode = current();
    m.loading = std::string{plan::to_string(mode)};

    // 新的线程将运行在它的窗口。
    std::size_t window = 0;
    if (const auto cfg = subagent::current(); cfg.installed && !cfg.model.empty()) {
        for (const auto& mi : cfg.candidates)
            if (mi.id.value == cfg.model && mi.context_window > 0) {
                window = static_cast<std::size_t>(mi.context_window);
                break;
            }
        if (window == 0)
            window = static_cast<std::size_t>(std::max(0, catalog_context_window_for(cfg.model)));
    }
    m.context_window = window;

    const Catalog cat = wire_tools();
    Request r;
    for (const auto& t : *cat)
        if (!withheld_by_policy(t)) r.pool.push_back(&t);
    std::string model_id;
    if (const auto cfg = subagent::current(); cfg.installed) model_id = cfg.model;
    r.limits = limits_for(provider::active(), window, model_id);
    r.mode = mode;
    std::vector<provider::ToolSpec> specs;
    const auto p = plan_into(r, specs);

    // 哪些(服务器,工具)对骑传输,由ToolDef
    // 自己的来源按键:传输的名称可能缩短和散列,来源不是.
    std::unordered_set<std::string> on_wire_names;
    for (const auto& s : specs)
        if (!s.defer_loading) on_wire_names.insert(s.name);
    std::unordered_set<std::string> on_wire;          // "server\0tool"
    std::unordered_map<std::string, std::string> skipped;
    for (const auto& t : *cat) {
        if (t.origin != ToolOrigin::Mcp || t.origin_tool.empty()) continue;
        const std::string key = t.origin_id + '\0' + t.origin_tool;
        if (!t.skipped.empty()) skipped[key] = t.skipped;
        else if (on_wire_names.contains(t.name.value)) on_wire.insert(key);
    }
    m.loaded_count = 0;
    m.on_demand_count = 0;
    for (auto& srv : m.servers) {
        if (srv.disabled || !srv.connected || srv.passthrough) continue;
        for (auto& t : srv.tools) {
            if (!t.enabled) continue;
            const std::string key = srv.name + '\0' + t.name;
            if (auto it = skipped.find(key); it != skipped.end()) { t.skipped = it->second; continue; }
            t.on_demand = !on_wire.contains(key);
            (t.on_demand ? m.on_demand_count : m.loaded_count) += 1;
        }
    }
    m.schema_tokens = p.schema_tokens;
    m.capped_by_provider = p.capped_by_provider;
}

} // namespace agentty::tools::tool_loading
