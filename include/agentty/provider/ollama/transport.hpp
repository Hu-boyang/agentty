#pragma once
// agentty::provider::ollama —本地 Ollama 服务器专用的 传输层。
//
// Ollama是NOT，只是OpenAI-compatible端点。它的原生 /
// api/chat 协议（Ollama 本身记录和 Zed 的提供者发言）更适合：它应用模型
// 自己的聊天模板，返回 STRUCTURED `message.tool_calls`（
// 没有泄漏的 -JSON-in-content 猜测），接受 per-request
// `options.num_ctx` / `num_predict`，通过 `keep_alive`
// 和 数据流 NDJSON 保持模型驻留（每行一个 JSON 对象）而不是 SSE。
//
// 该模块以zed-industries/zed crates/ollama/src/ollama.rs为模型：
//   POST {base}/api/chat   {model, messages, stream, keep_alive, options, tools, think}
//   NDJSON: {"message":{role,content,tool_calls,images,thinking}, "done":bool,
//            "done_reason", "prompt_eval_count", "eval_count"}
//
// Tool 调用 到达 两个 频道，按型号强度：
//   1.可用模型——本机结构化`message.tool_calls`(可信为-is)。
//   2.弱型号——JSON-protocol模式（agent-zero风格）：无原生
//      发送`tools`数组；工具 目录 内联在系统 提示词 中，模型用 一个 {
//      thoughts, tool_name, tool_args} 对象进行响应，
//      宽容提取器将其变成真正的 调用。加上一个抢救网，适用于尽管有本机通道但仍将
//      工具 -call JSON 泄漏到 `content` 中的模型。
//      launch_stream 根据模型 ID (is_weak_model)
//      选择通道，因此小型本地模型 (3B/7B) 首先工作-class。
//
// 传输层重用openai::Endpoint/openai::Request(
// 因此提供者选择管道没有改变-Ollama仍然是Kind::OpenAI预设且
// native_api = true)并且调度每个其他提供者所做的SAME agentty
// 消息。

#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>  // 下面仅JSON签名；全类型住在transport.cpp

#include "agentty/io/http.hpp"
#include "agentty/provider/openai/transport.hpp"
#include "agentty/runtime/msg.hpp"

namespace agentty::provider::ollama {

using openai::Endpoint;
using openai::Request;
using EventSink = std::function<void(Msg)>;

// 在调用线程上同步运行流式传输/api/chat请求。
// 每个NDJSON 帧经由`sink`被翻译成agentty Msg。
// 返回 StreamResult 来命名回合如何结束。 `cancel` 在 帧
// 边界进行轮询。
provider::StreamResult run_stream_sync(Request req, EventSink sink,
                                       http::CancelTokenPtr cancel = {});

// 系统提示词针对由Ollama服务的本地模型进行了调整。平实、坚定，
// 有明确的"use earlier messages"回忆提醒和环境块；
// 包括用户的 CLAUDE.md 等级，但省略了冗长的 Claude
// 代理散文和大的学习 -memory / 技能转储，这些转储使 提示词
// 膨胀并使小模型感到困惑。
[[nodiscard]] std::string system_prompt();

// 从我们的 线程 构建本机消息数组。暴露进行测试。
// `json_protocol`:渲染工具调用+导致weak-model散文-JSON词汇
// (助手发出{tool_name，tool_args}对象；结果作为USER
// "TOOL RESULT (name): …"轮返回)而不是本机
// tool_calls[]/角色:"tool"形状，因此模型仅教授散文协议
// 可以在 multi-step 任务上读取自己的对话历史记录。
[[nodiscard]] nlohmann::json build_messages(const std::vector<Message>& msgs,
                                            bool json_protocol = false);

// 从请求构建Ollama `options`对象(num_ctx、
// num_predict、采样)。本地型号的单一最高-leverage稳健性杠杆：
// num_ctx防御Ollama的微小默认上下文窗口默默地截断长代理对话。
// 暴露进行测试。荣誉 AGENTTY_OLLAMA_NUM_CTX
// / _NUM_PREDICT / _TEMPERATURE 覆盖。
[[nodiscard]] nlohmann::json build_options(const Request& req);

// 上下文窗口Ollama将为`advertised`服务ACTUALLY。
//
// 本地模型的发布窗口是关于权重的声明，而不是关于机器的声明。
// Ollama 分配 KV 缓存 进行匹配，笔记本电脑上的 131k
// -token 缓存 不会正常失败 — 它会导致服务器 OOM
// 并带走整个请求。因此 传输 可以夹在天花板上。
//
// 存在这一点是因为CLAMP和GAUGE必须是一个数字。它们是两个：传输
// 发送 num_ctx=32768，而上下文量规得分为未夹紧的
// 131072，因此条形读数为真实使用量的四分之一，
// 而 auto-compaction 的发射量是 线程 的四倍。
// 早在 agentty 考虑压缩之前，模型就默默地截断了自己的 提示词——
// 确切的 "local models forget everything" 失败，
// 从内部看不到，因为每一层都是单独正确的。
//
// `advertised`是已解析的窗口（0=未知）。
// 返回服务器实际保存的内容，这是仪表必须测量的内容。
[[nodiscard]] int effective_num_ctx(int advertised) noexcept;

// Test-only:通过现场解析器馈送NDJSON 字节 缓冲区并收集每个已调度的
// Msg(无网络回合-trip)。通过 json_protocol=true 来执行
// weak-model single-object 协议（没有本机工具数组；
// 整个回复是一个 {tool_name,tool_args} 对象，可能是噪音-wrapped）。
[[nodiscard]] std::vector<Msg> parse_ndjson_for_test(
    std::string_view ndjson_bytes,
    std::vector<std::string> known_tools = {},
    bool json_protocol = false,
    bool allow_memory_salvage = false,
    // 镜子 Request::show_reasoning — 参见 openai 接口边界。
    bool show_reasoning = true);

} // namespace agentty::provider::ollama
