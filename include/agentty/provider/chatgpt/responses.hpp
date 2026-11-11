#pragma once
// agentty::provider::chatgpt-直接ChatGPT (Codex)响应-API 传输层。
//
// 这是 "works like Claude" 路径：给定本机 ChatGPT OAuth
// 凭据 (从 `agentty login` → ChatGPT)，它将 agentty
// 对话作为 OpenAI 响应-API 数据流 直接 POST 到
// https://chatgpt.com/backend-api/codex/responses，并将
// SSE 转换回 agentty 数据流* 消息 — 真实文本、推理和工具-call 卡 — NO
// `codex` 二进制程序 在 运行时。访问令牌是 auto-refreshed，与 Anthropic
// OAuth 路径完全相同。

#include "agentty/provider/provider.hpp"

#include <nlohmann/json_fwd.hpp>  // 下面仅JSON签名；全类型住在responses.cpp
#include <string>
#include <string_view>
#include <vector>
#include "agentty/util/io.hpp"

namespace agentty::provider::chatgpt {

// 来自 ChatGPT 账户的实时 `/models` 目录 的一项条目。
// 这是 相同 形状代码 x-rs 派生其模型 选择面板：服务器告诉我们签名的 -in
// 帐户可能实际使用哪些 slugs（它们随着时间的推移而变化 - e.g。
// gpt-5.4，不是我们用来硬编码的过期 gpt-5.1-codex），所以我们永远不会猜测。
struct CatalogModel {
    std::string slug;             // 传输 ID 作为 body.model 发送
    std::string display_name;     // 选择面板的人类标签
    int         context_window = 272000;
    bool        is_default      = false;
};

// GET https://chatgpt.com/backend-api/codex/models?client_version=…，配合
// 使用 ChatGPT OAuth 凭据，与 codex-rs 的 ModelsClient 保持一致。返回
// 账号当前可用的模型目录；离线、401 等任意失败均返回空向量，
// parse error) so the caller can fall back to a bundled list. Blocking; short
// 超时也安全处理，因此可以从模型选择器刷新路径调用。
[[nodiscard]] std::vector<CatalogModel> fetch_models(Io, const maya::unlocked& cap);

// 当存在已保存 ChatGPT 凭据时为真（因此提供者可以在
// app-server子进程回退上选择直接传输层）。
[[nodiscard]] bool responses_available();

// 数据流 针对 ChatGPT 响应后端一回合。发出 StreamStarted →
// text/thinking/tool 增量 → StreamFinished（或 StreamError）。
// 阻塞；在回合的 工作线程 线程 上运行并尊重 req.cancel。
// 返回 StreamResult 来命名回合如何结束。
provider::StreamResult stream_responses(const maya::unlocked& cap, provider::Request req,
                                        provider::EventSink sink);

// ── 测试接口边界( 纯, 无网络) ──────────────────────────────────────────
// 请求→响应-API JSON 正文，传输层将POST。
[[nodiscard]] nlohmann::json build_body_for_test(const provider::Request& req);
// 从 session_key 导出的稳定的 per-conversation 会话
// ID (作为 缓存-routing `session_id` 头文件 发送的值)。
// 对于给定的键来说是确定性的，因此一个对话的重复轮次会击中后端 提示词缓存。
[[nodiscard]] std::string session_id_for_test(std::string_view session_key);
// HTTP 错误响应 正文 → 可操作的用户-facing 诊断。
[[nodiscard]] std::string format_http_error_for_test(int status, std::string_view body);
// 脚本化的SSE `data:` 载荷→状态更新函数看到的agentty Msg序列。
[[nodiscard]] std::vector<Msg> parse_sse_for_test(
    const std::vector<std::string>& sse_data_lines,
    bool show_reasoning = true);

} // namespace agentty::provider::chatgpt
