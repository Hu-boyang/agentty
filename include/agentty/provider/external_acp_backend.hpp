#pragma once

// ── ExternalAcpBackend - 一个出去 -of-process ACP 代理作为 AcpBackend ─────────
//
// 通过在 rpc::Peer 上驱动“远程”ACP 代理 — `claude-agent-acp`、
// `codex-acp` 或任何符合要求的 `agentclientprotocol` 代理来实现
// `AcpBackend` 合同 (provider/acp_backend.hpp)。
// agentty是CLIENT(编辑)；子进程是AGENT（LLM侧）。
//
// 这是相同的`acp::SessionUpdate`
// 词汇agentty的INBOUND方向已经从src/acp/server.cpp.发出OUTBOUND一个`prompt()` 调用:
//
//   1.懒惰地打开会话（`session/new`一次，然后跨轮重复使用）；
//   2、发送末尾用户消息为`session/prompt`；
//   3.转发每个入站`session/update`——代理消息/想法/
//      tool_call/tool_call_update/用法——直达`TurnSink`；
//   4.以`TurnResult`结算本轮，其`StopReason`是从其解码的
//      代理的回合-level ACP `StopReason` (
//      end_turn/max_tokens/拒绝/已取消)，应用终端-event规则一次
//      ——在干净的回合中没有虚假的"cancelled"。
//
// 它驱动一个已经-connected的对端，而不是产生一个：子进程
// 和`initialize`是一个单独的生命周期。这使得它可以针对
// 连接到假代理的 in-memory 通道进行测试，并且可以在任何 字
// 节 通道上使用。 `spawn_acp_agent()`（参见 .
// cpp）生成用于生产的连接句柄。
//
// Claude参考适配器(claude-agent-acp/src/acp-agent.ts)
// 是其镜像的模型：一种瘦协议转换器，其恰好拥有一个怪异表面(其`SDKMessage`
// →`session/update`映射)并且没有任何结构。
//
// 参见docs/internal-acp-backends.md——该头文件是该计划的§6，步骤5。

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <maya/runtime.hpp>

#include <acp/methods.hpp>        // acp::StopReason、PromptParams、NewSessionParams
#include <acp/protocol.hpp>       // 方法表，jsonrpc::Router
#include <acp/updates.hpp>        // acp::SessionUpdate，SessionUpdateMsg

#include "agentty/io/http.hpp"                 // http::CancelTokenPtr
#include "agentty/provider/acp_backend.hpp"    // AcpBackend、TurnSink、TurnResult
#include "agentty/provider/provider.hpp"       // provider::Request
#include "agentty/rpc/peer.hpp"                // rpc::Peer，rpc::Dispatch
#include "agentty/util/callback.hpp"
#include "agentty/util/sendable.hpp"           // util::Callback

namespace agentty::provider {

// ── 沙箱 代表 ─────────────────────────────────────────────────────────
// 外部代理可以向CLIENT (美国)请求read/write文件并代表其运行终端
// (ACP `fs/*`和`terminal/*`)。
// agentty 已经为此拥有了 沙箱 + 权限 门；
// 后端不是从这里进入它，而是需要一个小的委托，因此接线保持在一个位置，
// 并且测试可以提供 in-memory 存根。所有钩子都是可选的
// - 未设置的钩子会拒绝该功能（代理被告知该方法不受支持），
// 这是安全的默认值：mis-declared 代理无法默默地逃脱 沙箱。
struct AcpClientDelegate {
    // fs/read_text_file→返回文件内容(可选地是行窗口)。
    std::function<std::optional<std::string>(
        const std::string& path, std::optional<int> line, std::optional<int> limit)>
        read_text_file;

    // fs/write_text_file→坚持；返回 false 拒绝（权限 拒绝）。
    std::function<bool(const std::string& path, const std::string& content)>
        write_text_file;

    // request_permission→真=允许代理将要运行的工具。
    // 未设置⇒允许（代理自己的门管辖）；返回 false 表示拒绝。
    std::function<bool(const acp::RequestPermissionParams&)> request_permission;

    // terminal/create→运行命令并返回其结果。 ACP 终端
    // 协议是一个有状态的生命周期（创建 → output/wait_for_exit
    // → kill/release），但是同步执行器通过在 create()
    // 内运行命令直至完成并为后续的 -up 调用 缓存结果来满足它—
    // —这正是 run_terminal 在这里所做的。返回值：（合并
    // stdout+stderr，退出代码）。未设置⇒终端 拒绝。
    struct TerminalResult {
        std::string output;
        int         exit_code = 0;
        bool        truncated = false;
    };
    std::function<TerminalResult(const std::string& command,
                                 const std::vector<std::string>& args,
                                 const std::optional<std::string>& cwd)>
        run_terminal;
};

// 代表连接到agentty的REAL边界：fs读取通过读门（工作区+技能
// read-allowlist，符号链接-escape被阻止），
// 通过写门写入（仅工作区），以及terminal/permission通过OS-native 沙箱。
// 这是调用者想要的委托生产；测试提供自己的 in-memory 存根。
// 在 .cpp 中定义，因此 头文件 不受 tool/sandbox
// 头文件 的影响。
[[nodiscard]] AcpClientDelegate default_sandbox_delegate();

// ── 选项 ──────────────────────────────────────────────────────────────────
struct ExternalAcpOptions {
    // 工作目录移交给`session/new`(代理的项目根目录)。
    std::string cwd;

    // 如果为 true，则在第一个 `prompt()` 上打开一个会话，
    // 并在后续的每一轮中重复使用（代理保留自己的记录 - 这就是 claude
    // -agent-acp / codex-acp 的工作方式，
    // 并且它可以让代理在工具轮次之间重播自己的推理，而无需我们 re-sending）。
    // 如果为 false，则每轮打开一个新会话（无状态；对于不持久的代理，
    // 或在测试中严格的 per-round 隔离）。
    bool reuse_session = true;

    // MCP服务器通过session/new.委托给外部代理，
    // 委托代理拥有执行权； agentty 仅观察其工具更新。
    std::vector<acp::McpServer> mcp_servers;

    // 可选的客户端-side能力代理可以回到调用。
    AcpClientDelegate delegate;
};

class ExternalAcpBackend final : public AcpBackend {
public:
    // 构造后端BEFORE，其对端存在：对端的分派来自下面的make_dispatch()，
    // 其关闭该后端。 对端 启动并初始化后，
    // 调用 connect()。 connect() 之前的
    // prompt() 彻底失败。
    explicit ExternalAcpBackend(ExternalAcpOptions opts);

    // 绑定(已经-initialized)对端。它 必须 比后端使用它的寿命更长；
    // 所有权 留在呼叫者身边（通常是 SpawnedAcpAgent）。
    void connect(rpc::Peer& peer) noexcept;

    // Non-copyable、non-movable —分派 收于 `this`。
    ExternalAcpBackend(const ExternalAcpBackend&)            = delete;
    ExternalAcpBackend& operator=(const ExternalAcpBackend&) = delete;

    ~ExternalAcpBackend() override = default;

    // AcpBackend合同。驾驶一轮； 数据流 SessionUpdates 至 `sink`；
    // 返回回合的 TurnResult。 线程-compatible：一次一轮。
    // 轮到ExternalAcpBackend：将session/prompt和数据流、
    // session/update发送到`sink`中，直到PromptResult。正好一个 终端 事件。
    // 阻止代理回复：`unlocked`。
    TurnResult prompt(const maya::unlocked&       cap,
                      const Request&              req,
                      const TurnSink&             sink,
                      const http::CancelTokenPtr& cancel) override;

    // Test/inspection钩子：由第一个提示词打开的会话id（在此之前为空，
    // 或者在per-round模式下）。
    [[nodiscard]] std::string session_id() const;

    // 代理的调用如何到达我们（session/update→主动接收器；fs/*、
    // terminal/*、request_permission→委托）。
    // 产卵工厂用它来建造 对端。它在 `this` 结束时关闭，因此该后端 必须 比 对端
    // 更长寿。
    [[nodiscard]] rpc::Dispatch make_dispatch();

private:
    // 路由器处理函数得到什么：后端，以及读取器线程
    // 针对其正在应答的调用的能力（它恰好存在于读取器的
    // 堆栈上以用于该调用）。
    struct Ctx {
        ExternalAcpBackend&   self;
        const maya::unlocked& cap;
    };
    using Router = ::jsonrpc::Router<Ctx>;
    Router make_router() const;

    std::optional<acp::SessionId> ensure_session_(const maya::unlocked& cap,
                                                  const Request& req,
                                                  std::optional<TurnError>& err);

    rpc::Peer*         peer_ = nullptr;   // 由connect()设置一次
    ExternalAcpOptions opts_;
    Router             router_;

    // 复用会话id：设置一次，每轮读取。 快照。
    maya::snapshot<std::optional<std::string>> session_;

    // 飞行中的子弹。 session/update 到达 对端 的读取器，
    // 该读取器在调用 `sink` 之前加入 `inflight`。
    // prompt() 在退出时进行一轮并等待 in-flight 调用，
    // 因此接收器（指向 提示词 的堆栈）在返回后永远不会被调用。无回合：更新下降。
    struct Round {
        const util::Callback<void(acp::SessionUpdate)> sink;   // 在构建时设置，然后仅调用
        maya::stop_group                               inflight;
    };
    maya::published<Round> round_;

    // terminal/create同步运行命令（通过委托）并将其结果
    // 保留在新的ID下，直到terminal/release.
    struct TerminalState { std::string output; int exit_code = 0; bool truncated = false; };
    struct Terminals {
        std::unordered_map<std::string, TerminalState> by_id;
        std::uint64_t                                  next_id = 1;
    };
    maya::guarded<Terminals> terminals_;
};

// ── 转身 -level StopReason 绘图(纯度、可测试) ───────────────────────────
// ACP轮-level StopReason→agentty轮-level StopReason。
// `cancelled` 和 `refusal` 是 agentty 梯子中的 不 停止原因
// - 调用者将它们变成 TurnError - 因此这仅映射 "clean finish" 臂；
// 呼叫者在呼叫前特殊-cases cancelled/refusal。
[[nodiscard]] StopReason map_acp_stop_reason(acp::StopReason r) noexcept;

// ── 子加工厂 ───────────────────────────────────────────────────────
// 连接的、初始化的代理及其背后的进程。摧毁它来摧毁代理
// （关闭管道，waits/kills孩子）。
struct SpawnedAcpAgent {
    std::unique_ptr<rpc::Peer> connection;
    // 子进程的不透明所有者。 对端 之后重置：对端 的读取
    // 器读取子设备的标准输出 数据流。
    std::shared_ptr<void> process;

    SpawnedAcpAgent() = default;
    SpawnedAcpAgent(SpawnedAcpAgent&&) noexcept = default;
    SpawnedAcpAgent& operator=(SpawnedAcpAgent&&) noexcept = default;
    SpawnedAcpAgent(const SpawnedAcpAgent&) = delete;
    SpawnedAcpAgent& operator=(const SpawnedAcpAgent&) = delete;
    ~SpawnedAcpAgent() { reset(); }

    void reset() noexcept;

    [[nodiscard]] bool ok() const noexcept { return connection != nullptr; }
};

// 生成 `argv[0]` (e.g."claude-agent-acp" 或 "codex-acp")，
// 剩余参数作为 ACP 代理子进程 (util::ChildProcess:POSIX 上的
// fork/exec/pipe，Windows 上的 CreateProcess)，
// 使用 `dispatch` 在其 stdio 上运行 rpc::Peer，运行 `initialize`，
// 并且返回连接的句柄。失败时返回 SpawnedAcpAgent，其中连接为空，`err` 已填充。
//
// PRODUCTION FLOW (解决backend/dispatch/peer循环）：
//     ExternalAcpBackend后端{选择}；
//     auto agent = spawn_acp_agent(argv, init, backend.make_dispatch(), err);
//     if (!agent.ok()) { /* handle err */ }
//     backend.connect(*agent.connection);
//     // ... backend.prompt(...) ...（在后端的生命周期中保持 `agent`
// 处于活动状态）生成 `argv` 作为 ACP 代理，在其 stdio 上运行 rpc::Peer，
// 并完成 `initialize` 握手。失败时返回 !ok() 并设置 `err`。
// 等待握手：`unlocked`。
[[nodiscard]] SpawnedAcpAgent spawn_acp_agent(const maya::unlocked&           cap,
                                              const std::vector<std::string>& argv,
                                              const acp::InitializeParams&    init,
                                              rpc::Dispatch                   dispatch,
                                              std::string&                    err);

} // namespace agentty::provider
