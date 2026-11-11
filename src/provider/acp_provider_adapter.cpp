// agentty::provider — ExternalAcpBackend → Provider 适配器。
// 参见acp_provider_adapter.hpp。

#include "agentty/provider/acp_provider_adapter.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <maya/runtime.hpp>

#include <acp/acp.hpp>          // acp::InitializeParams，SessionUpdate武器，比赛
#include <nlohmann/json.hpp>

#include "agentty/io/http.hpp"                       // http::CancelToken
#include "agentty/mcp/client.hpp"                    // 已配置MCP委派
#include "agentty/provider/acp_agents.hpp"           // resolve_acp_agent
#include "agentty/provider/acp_backend.hpp"          // TurnSink，TurnResult
#include "agentty/provider/external_acp_backend.hpp" // ExternalAcpBackend，spawn_acp_agent
#include "agentty/provider/stream_epilogue.hpp"        // StreamResult
#include "agentty/runtime/msg.hpp"                   // 数据流* 消息
#include "agentty/tool/util/fs_helpers.hpp"          // workspace_root (对于 CWD)

namespace agentty::provider {

// 一个产生的代理+驱动它的后端。按代理 ID 缓存：子进程为
// long-lived，其 ACP 会话在请求之间重用。
//
// 由acquire()构建一次，然后发布read-only。拆解是 最后
// 持有者的：release_acp_agents() 只会删除 缓存 的引用，
// 因此仍在后端 -> prompt() 内的另一个 线程 上的转弯将使两者
// 保持活动状态，直到它返回。它用于在该回合下将它们重置到位。
struct LiveAgent {
    std::unique_ptr<ExternalAcpBackend> backend;
    SpawnedAcpAgent                     agent;   // 拥有子进程和对端；先死
    // 对端的分派指向后端，因此对端先行。
    ~LiveAgent() { agent.reset(); }
};

} // namespace agentty::provider
// 两名成员均在地图发布前由 acquire() 设置，
// 且从未重新分配；读卡器调用上的是后端的prompt()
// （一次一轮，内部守卫）和对端（同步）。加入摧毁（对端）。
MAYA_SYNC(agentty::provider::LiveAgent);
MAYA_JOINS_ON_DESTROY(agentty::provider::LiveAgent);
namespace agentty::provider {
namespace {

namespace a = acp;
using json = nlohmann::json;

// Copy-on-write：读者查找没有锁，重生发布新地图。
using AgentMap = std::unordered_map<std::string, std::shared_ptr<LiveAgent>>;
maya::published<const AgentMap>& g_cache() {
    static maya::published<const AgentMap> c;
    return c;
}

std::shared_ptr<LiveAgent> cached(const std::string& agent_id) {
    auto map = g_cache().current();
    if (!map) return nullptr;
    auto it = map->find(agent_id);
    return it == map->end() ? nullptr : it->second;
}

// 提取ContentBlock的纯文本（仅文本+文本-resource臂；
// 其他臂对流式增量没有贡献）。
std::string block_text(const a::ContentBlock& b) {
    std::string out;
    a::match(b,
        [&](const a::TextContent& t) { out += t.text; },
        [&](const a::ResourceContent& r) {
            a::match(r.resource,
                [&](const a::TextResource& tr) { out += tr.text; },
                [&](const a::BlobResource&)    {});
        },
        [&](const auto&) {});
    return out;
}

// 构建客户端的InitializeParams。我们宣传 fs + 终端 客户端功能，
// 以便 spec-conformant 代理知道它可能会将 调用 返回给我们（我们
// 的 make_handlers() 连接这些）。 acp-cpp 不强制执行此代理-side，
// 但真正的代理会检查它。
a::InitializeParams make_init() {
    a::InitializeParams init;
    init.clientCapabilities.fs.readTextFile  = true;
    init.clientCapabilities.fs.writeTextFile = true;
    init.clientCapabilities.terminal         = true;
    return init;
}

// 获取（或延迟生成）`agent_id` 的实时代理。如果启动失败，
// 则返回 nullptr 并填充 `err`。
std::shared_ptr<LiveAgent> acquire(const maya::unlocked& cap, const std::string& agent_id, std::string& err) {
    if (auto hit = cached(agent_id)) return hit;

    auto spec = resolve_acp_agent(agent_id);
    if (!spec) {
        err = "unknown ACP agent '" + agent_id +
              "' (no built-in default and no .agentty/acp-agents.json entry)";
        return nullptr;
    }

    ExternalAcpOptions opts;
    opts.cwd           = spec->cwd.empty()
                       ? tools::util::workspace_root().string()
                       : spec->cwd;
    opts.reuse_session = true;
    for (const auto& server : mcp::configured_servers_for_delegation()) {
        auto key_values = [](const auto& values) {
            a::List<a::KeyValue> out;
            for (const auto& [name, value] : values)
                out.push_back(a::KeyValue{name, value});
            return out;
        };
        switch (server.transport) {
            case mcp::ServerLaunch::Transport::Stdio:
                opts.mcp_servers.push_back(a::McpServer{a::StdioMcpServer{
                    server.name, server.command, server.args, key_values(server.env)}});
                break;
            case mcp::ServerLaunch::Transport::Http:
                opts.mcp_servers.push_back(a::McpServer{a::HttpMcpServer{
                    server.name, server.url, key_values(server.headers)}});
                break;
            case mcp::ServerLaunch::Transport::Sse:
                opts.mcp_servers.push_back(a::McpServer{a::SseMcpServer{
                    server.name, server.url, key_values(server.headers)}});
                break;
        }
    }
    opts.delegate      = default_sandbox_delegate();

    auto live     = std::make_shared<LiveAgent>();
    live->backend = std::make_unique<ExternalAcpBackend>(std::move(opts));

    std::string spawn_err;
    live->agent = spawn_acp_agent(cap, spec->argv(), make_init(),
                                  live->backend->make_dispatch(), spawn_err);
    if (!live->agent.ok()) {
        err = "failed to launch ACP agent '" + agent_id + "': " + spawn_err;
        return nullptr;
    }
    live->backend->connect(*live->agent.connection);

    // 另一个线程可能与我们赛跑；保留第一名获胜者。
    for (;;) {
        auto cur = g_cache().current();
        if (cur) {
            if (auto it = cur->find(agent_id); it != cur->end()) return it->second;
        }
        auto next = cur ? std::make_shared<AgentMap>(*cur) : std::make_shared<AgentMap>();
        next->emplace(agent_id, live);
        if (g_cache().publish_if(cur, std::move(next))) return live;
    }
}

} // 命名空间结束。

StreamResult stream_external_acp(const maya::unlocked& cap, const std::string& agent_id, Request req, EventSink sink) {
    sink(StreamStarted{});

    std::string err;
    auto live = acquire(cap, agent_id, err);
    if (!live) {
        StreamError e; e.message = err;
        sink(std::move(e));
        return StreamResult::failed(err.empty() ? "ACP agent spawn failed" : err);
    }

    // 将ACP的快照-based、agent-owned工具生命周期翻译成可见的规范卡。
    // rawInput 是替换状态，而不是追加增量。
    // 这些调用已经被委托代理人执行了；
    // StreamObservedToolResult 在 finalize_turn
    // 之前解决它们，因此主机调度程序永远不会第二次执行它们。
    std::unordered_set<std::string> open_tools;
    std::unordered_set<std::string> ended_tools;
    std::unordered_map<std::string, std::string> observed_outputs;

    auto tool_name = [](const a::ToolCall& call) {
        // Claude的ACP适配器携带元数据中的可执行名称，
        // 而标题是人类标签("Edit file …")。
        // 其他代理通常直接使用标题，因此将其保留为协议-neutral 回退。
        try {
            if (call.meta.contains("claudeCode")
                && call.meta["claudeCode"].contains("toolName")) {
                auto name = call.meta["claudeCode"]["toolName"].get<std::string>();
                if (!name.empty()) return name;
            }
        } catch (...) { /* 格式错误的可选元数据：使用标题 */ }
        return call.title.empty() ? call.toolCallId.value : call.title;
    };

    auto finish_tool = [&sink, &open_tools, &ended_tools, &observed_outputs](
                           const std::string& id, bool failed = false) {
        if (!open_tools.contains(id) || !ended_tools.insert(id).second) return;
        sink(StreamToolUseEnd{ToolCallId{id}});
        std::string output;
        if (auto it = observed_outputs.find(id); it != observed_outputs.end())
            output = std::move(it->second);
        sink(StreamObservedToolResult{ToolCallId{id}, failed, std::move(output)});
        open_tools.erase(id);
    };

    TurnSink tsink;
    tsink.update = [&sink, &open_tools, &ended_tools, &observed_outputs,
                    &tool_name, &finish_tool](a::SessionUpdate su) {
        a::match(su,
            [&](const a::SU_AgentMessageChunk& c) {
                std::string t = block_text(c.content);
                if (!t.empty()) sink(StreamTextDelta{std::move(t)});
            },
            [&](const a::SU_AgentThoughtChunk& c) {
                std::string t = block_text(c.content);
                if (!t.empty()) sink(StreamThinkingDelta{std::move(t), {}});
            },
            [&](const a::SU_ToolCall& tc) {
                const auto& call = tc.toolCall;
                const auto& id = call.toolCallId.value;
                if (!open_tools.contains(id) && !ended_tools.contains(id)) {
                    sink(StreamToolUseStart{ToolCallId{id}, ToolName{tool_name(call)}});
                    open_tools.insert(id);
                }
                if (call.rawInput.has_value() && !call.rawInput.value().is_null())
                    sink(StreamToolUseSnapshot{ToolCallId{id}, call.rawInput.value().dump()});
                if (call.rawOutput.has_value() && !call.rawOutput.value().is_null())
                    observed_outputs[id] = call.rawOutput.value().dump();
                if (call.status == a::ToolCallStatus::Completed
                    || call.status == a::ToolCallStatus::Failed)
                    finish_tool(id, call.status == a::ToolCallStatus::Failed);
            },
            [&](const a::SU_ToolCallUpdate& tc) {
                const auto& update = tc.update;
                const auto& id = update.toolCallId.value;
                // 一致的ACP 数据流首先公布调用。
                // 对 竞态 之前的适配器进行防御：
                // 开启稳定的回退卡而不是掉线更新。
                if (!open_tools.contains(id) && !ended_tools.contains(id)) {
                    const std::string name = update.title.has_value()
                        ? update.title.value() : id;
                    sink(StreamToolUseStart{ToolCallId{id}, ToolName{name}});
                    open_tools.insert(id);
                }
                if (update.rawInput.has_value() && !update.rawInput.value().is_null())
                    sink(StreamToolUseSnapshot{ToolCallId{id}, update.rawInput.value().dump()});
                if (update.rawOutput.has_value() && !update.rawOutput.value().is_null())
                    observed_outputs[id] = update.rawOutput.value().dump();
                if (update.status.has_value()
                    && (update.status.value() == a::ToolCallStatus::Completed
                        || update.status.value() == a::ToolCallStatus::Failed))
                    finish_tool(id,
                        update.status.value() == a::ToolCallStatus::Failed);
            },
            [&](const a::SU_Usage& u) {
                StreamUsage su2;
                // ACP报告单个聚合`used`；将其映射到输入标记字段，
                // 以便 context-window 仪表前进。
                // `size` 是代理的通告窗口（如果已发送）。
                su2.input_tokens = static_cast<int>(u.used < 0 ? 0 : u.used);
                sink(std::move(su2));
            },
            [&](const auto&) { /* 计划/命令/模式/信息：未流式传输 */ });
    };

    auto cancel = std::make_shared<http::CancelToken>();
    req.cancel  = cancel;   // 运行时 在 Esc 上跳闸（见下文）

    // ACP的per-round TurnResult(acp_backend.hpp)
    // 是ADJACENT层的结果； 归并 到共享提供程序 StreamResult 上，
    // 因此 ACP 臂通过与本机 传输层 相同的值类型进行报告。
    TurnResult res = live->backend->prompt(cap, req, tsink, cancel);

    // 一些适配器省略了关于cancellation/early回合结算的最终状态更新。
    // 使用最新的 快照 将剩余的每张卡关闭一次，
    // 这样就不会永远搁置 待处理 调用。
    std::vector<std::string> still_open(open_tools.begin(), open_tools.end());
    for (const auto& id : still_open) finish_tool(id);

    if (!res.ok()) {
        if (res.error && res.error->user_cancel) {
            // 用户取消是完全停止，而不是错误表面。
            sink(StreamFinished{StopReason::EndTurn});
            StreamResult sr;
            sr.end  = StreamEnd::UserCancelled;
            sr.stop = StopReason::EndTurn;
            sr.error = std::string{"cancelled"};
            return sr;
        }
        StreamError e;
        e.message = res.error ? res.error->message : "ACP agent error";
        if (res.error) e.retry_after = res.error->retry_after;
        StreamResult sr;
        sr.end         = StreamEnd::TransportError;
        sr.stop        = res.stop;
        sr.error       = e.message;
        sr.retry_after = e.retry_after;
        sink(std::move(e));
        return sr;
    }
    sink(StreamFinished{res.stop});
    StreamResult sr;
    sr.end  = StreamEnd::CleanClose;
    sr.stop = res.stop;
    return sr;
}

void release_acp_agents(const maya::unlocked&) noexcept {
    // 放下缓存。每个智能体都会被最后持有它的人撕毁（在这里，
    // 除非回合仍在运行），即使对于楔入的智能体也是如此。
    auto drained = g_cache().take();
    (void)drained;
}

} // namespace agentty::provider
