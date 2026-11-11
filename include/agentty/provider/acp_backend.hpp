#pragma once

// ── 内部 ACP 后端边界 ────────────────────────────────────────────
//
// 统一层。每个模型后端 - 本机 in-process HTTP+SSE 适配器 (Anthropic
// / OpenAI / Ollama / Codex-Responses) OR 外部 ACP
// 代理子进程 (codex-acp, claude-agent-acp) - 实现 一个 合约：
// 驱动一轮，数据流归一化`acp::SessionUpdate` 事件，
// 以`TurnResult`结算。 运行时 仅消耗这一点，因此提供者的怪癖
// （推理重播、思考 -signature 重播、SSE-vs-NDJSON 帧、
// 终端-event "cancelled" 陷阱）恰好存在于一个适配器内，
// 并且永远不会泄漏到核心中。
//
// 这是 相同 `acp::SessionUpdate` 词汇 agentty 已经从
// `src/acp/server.cpp` 发出 OUTBOUND (agentty-as-ACP-agent
// 对于 Zed)。采用 INBOUND 使引擎在 接口边界 上都可以说 ACP。
//
// 完整的设计、映射表和迁移计划参见docs/internal-acp-backends.md。
// 此 头文件 是该文档的第 5 节。

#include <chrono>
#include <functional>
#include <optional>
#include <string>

#include <acp/updates.hpp>   // acp::SessionUpdate

#include "agentty/io/http.hpp"          // http::CancelTokenPtr
#include "agentty/provider/provider.hpp" // provider::Request
#include "agentty/runtime/msg.hpp"       // agentty::StopReason

namespace agentty::provider {

// ── 单方的 流式传输 沉没 ────────────────────────────────────────────────
// ACP的`session/update`词汇表中的一个标准化事件。
// 调用者传递一个闭包；适配器针对每个流式增量（文本、想法、工具 调用、
// 工具-call 更新、使用）触发一次。它永远不会携带 终端 事件 — 这是
// `prompt` 的返回值，因此 "emit the terminal exactly once"
// 是结构性的，而不是四个 传输层 必须各自记住的约定。
struct TurnSink {
    std::function<void(acp::SessionUpdate)> update;

    void operator()(acp::SessionUpdate su) const {
        if (update) update(std::move(su));
    }
};

// ── 终端 失败细节 ──────────────────────────────────────────────────
// 一轮可能会失败。 `user_cancel` 区分 Esc（不是故障 —
// HTTP 层将有意中止报告为 "cancelled"，并且只有适配器知道该
// 中止是 user-driven 还是其自己的早期 read-stop）。
// `auth_expired`将401/403路由到OAuth-refresh 重试路径中。
struct TurnError {
    std::string                          message;
    std::optional<std::chrono::seconds>  retry_after;   // 服务器重试-After提示
    bool                                 auth_expired = false;
    bool                                 user_cancel  = false;
    bool                                 from_stall   = false;
};

// ── 一轮怎么结束 ────────────────────────────────────────────────────────
// `stop`是agentty的ROUND-level StopReason(保留
// `ToolUse`)-代理循环读取它以决定继续-vs-finish。 ACP自己的回合
// -level StopReason仅在OUTBOUND服务器接口边界上应用，其中N回合已
// 被合并到一个用户-visible回合中。
struct TurnResult {
    StopReason               stop = StopReason::EndTurn;
    std::optional<TurnError> error;   // 订婚→本轮失败

    [[nodiscard]] bool ok() const noexcept { return !error.has_value(); }

    static TurnResult finished(StopReason s) noexcept { return {s, std::nullopt}; }
    static TurnResult failed(TurnError e) noexcept { return {StopReason::EndTurn, std::move(e)}; }
    static TurnResult cancelled() noexcept {
        TurnError e; e.message = "cancelled"; e.user_cancel = true;
        return failed(std::move(e));
    }
};

// ── 每一个后端都满足了合同要求 ─────────────────────────────────────
// Type-erased (不是概念)，因为后端是可插拔的且异构的：
// in-process HTTP适配器和out-of-process ACP子进程
// 位于同一句柄后面。每轮一个虚拟 调用 在网络轮 -trip 旁边是免费的。
class AcpBackend {
public:
    virtual ~AcpBackend() = default;

    // 驱动ONE提供者round-trip (一个模型完成）。
    // 数据流 `acp::SessionUpdate` 至 `sink`；返回回合的 `TurnResult`。
    // agentty的代理循环(kick_pending_tools)将N轮
    // 变成一轮并且本身执行任何请求的工具——`SU_ToolCall`只是
    // 工具的型号ASKING；执行时，沙箱隔离和权限保持agentty的。
    // 在没有持有 锁 的回合 工作线程 上运行，并阻止：`unlocked`。
    virtual TurnResult prompt(const maya::unlocked&       cap,
                              const Request&              req,
                              const TurnSink&             sink,
                              const http::CancelTokenPtr& cancel) = 0;
};

} // namespace agentty::provider
