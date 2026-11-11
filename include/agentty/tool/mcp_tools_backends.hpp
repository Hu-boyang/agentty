#pragma once
// mcp_tools_backends-mcp-cpp的工具集拥有的主机-coupled工具SHELLS的agentty-side
// HostServices后端(remember/forget/擦除、待办事项、
// 技能、search_docs、任务)。
//
//   mcp-cpp拥有各工具的协议面； agentty 通过注入的后端提供工作。
//   install_host_backends() 构造适配器（JSONL
//   存储上的 MemoryStore、Agent-Skills 引擎上的
//   SkillResolver、RAG 管道上的 DocRetriever、
//   子代理循环上的 SubagentRunner）并将它们安装到桥传递到 make_provider
//   () 的 HostServices 中。后端为空的工具不会被广告。

#include <mcp/tools/host.hpp>

#include "agentty/tool/one_shot.hpp"
#include "agentty/util/io.hpp"

#include <optional>
#include <string>

namespace agentty::store { struct RagConfig; }

namespace agentty::tools {

// 用 agentty 后端填充 svc.memory/svc.skills/svc.retriever/svc.subagent。
// 保留 svc.todo 为空（mcp todo shell 不需要主机状态）
// 并且 svc.http 保持不变（网桥安装 HttpClient）。
void install_host_backends(::mcp::tools::HostServices& svc);

// 无头-shot (`agentty run`):参见agentty/tool/one_shot.hpp.

// 正确当且仅当`name`解析为真正的代理角色——内置-in(探索者、
// 审阅者、测试者、编码员、
// 一般)或来自.agentty/agents/*.md.的用户-defined存在，
// 因此`agentty run --agent X`可以REJECT拼写错误，
// 而不是像`general`那样静默运行：resolve_agent_type()故意
// 回退（命名不存在的角色的模型）仍然应该完成工作），但对于 CLI 的人来说，
// 回退 在没有任何警告的情况下将拼写错误变成了错误的代理。
[[nodiscard]] bool is_known_agent_type(std::string_view name);

// 该错误消息的有效名称的 Comma-separated 列表。
[[nodiscard]] std::string known_agent_types();

// 项目.agentty/agents/.中定义的子代理类型名称读取磁盘，
// 因此运行在工作线程(read_library)上。
[[nodiscard]] std::vector<std::string> project_agent_names();

// 将用户RAG配置应用于进程-wide检索器、ON 该 CALLING THREAD。
// 它探测嵌入器（网络拨号）并可能会阻塞，因此 TUI 永远不会直接执行 调用
// 操作：状态更新函数 返回 cmd::apply_rag_settings，
// 它在 jaal 工作线程 上运行此操作。调度是调用者的决定，而不是该函数
// 的决定——这就是使 update() 免受 副作用 影响的原因。从不抛出。
void rag_apply_settings_now(const store::RagConfig& cfg);

// RAG、选择面板的状态行和`agentty diagnostics`
// 的实时dense-embedder状态。当嵌入器不可用时，
// `reason` 会说明原因 - 重点是配置错误的端点是可见的，
// 而不是默默地检索永远降级为关键字 -only。
struct RagEmbedStatus {
    enum class State : std::uint8_t { Unprobed, Ready, Unavailable };
    State         state      = State::Unprobed;
    std::uint32_t dim        = 0;
    int           latency_ms = 0;
    std::string   reason;
    std::string   describe;
};
[[nodiscard]] RagEmbedStatus rag_embed_status(Io);

// 探测ARBITRARY嵌入配置而不干扰活动检索器——选择
// 面板的"Test connection"行在工作线程 线程上运行。
// 返回 MEASURED 维度，这是唯一值得信赖的来源。
struct RagProbeOutcome {
    bool          ok = false;
    std::uint32_t dim = 0;
    int           latency_ms = 0;
    std::string   error;
};
[[nodiscard]] RagProbeOutcome rag_probe_embedder(const store::RagConfig& cfg,
                                                 const std::string& api_key);

// 停止进程-wide检索器的in-flight背景热并回收其工作线程。
// 调用 EARLY 处于拆卸状态（静态销毁之前），因此 ^C 是 提示词：
// 否则，在main()返回之后，检索器的热j线程在static-dtor时间加入，
// 并阻止multi-second嵌入通道上的退出。 幂等。
void rag_shutdown();


// 智能模式创新3 (SPECULATIVE):为`query`
// 踢出一个独立的、最好的-effort code/文档检索warm-up，
// 因此工作区索引是热门的，并且在协调器委托时接地是pre
// -fetched——真正的推测性执行与领导者的思维重叠，
// 具有零浪费令牌风险(检索是本地的)。立即返回；从不抛出。


// ── 主动检索( 明确选择-in) ────────────────────────────
// 在模型的工具循环之外运行RAG管道。该应用程序在孤立的
// 工作线程 上调用阻塞表单，并仅在其稳定后启动模型，
// 因此每一轮最多执行一次检索。除非 AGENTTY_RAG_PROACTIVE=1，
// 否则自动主动注入将被禁用。
struct ProactiveHit {
    std::string block;        // 围栏 <retrieved-context> 文本为 传输
    double      confidence;   // [0,1] 清除障碍的检索置信度
    int         passages;     // 该块承载了多少个通道
    // Cross-turn对该块ACTUALLY携带的段落进行去重密钥(
    // 源：路径：行)。 proactive_retrieve 在 工作线程
    // 上构建块，该块可能会在延迟 -budget 溢出时被放弃，
    // 因此一旦命中真正返回给调用者，密钥就只有 COMMITTED 到
    // 去重 FIFO — 被放弃的 工作线程 永远不会抑制它未显示的段落。
    std::vector<std::string> dedup_keys;
};
[[nodiscard]] std::optional<ProactiveHit>
proactive_retrieve(const std::string& query, int k = 3);

// 应用程序的隔离工作线程使用相同的single-execution漏斗。
// 调用者总是injects/stages这个结果。从不抛出。
[[nodiscard]] std::optional<ProactiveHit>
proactive_retrieve_blocking(const std::string& query, int k = 3);

} // namespace agentty::tools
