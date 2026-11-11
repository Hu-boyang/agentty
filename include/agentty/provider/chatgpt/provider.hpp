#pragma once

// ChatGPT-authenticated Codex提供者——到OpenAI响应
// 后端的本机in-process桥（与桌面应用程序中的Codex和chatgpt.com/codex相同的端点）。
// 运行时 处有 NO `codex` 二进制程序。
//
// 设计目标（与Anthropic/OpenAI/Ollama 传输层平价）：
//
//   ·反向-engineered OAuth，与Claude类似。认证是
//     ChatGPT认证-Code+PKCE流(`agentty login`→ChatGPT)；
//     令牌加密存储，auto-refreshed 存储在 -process 中。
//     没有委派给外部 CLI，没有 ~/.codex 秘密。
//   ·直接响应-API 流式传输。一回合对应一个 SSE 请求
//     chatgpt.com/backend-api/codex/responses;工具调用、
//     推理和文本显示为first-class agentty 数据流 事件。
//   ·Cross-platform+本机速度。纯 HTTPS 传输层，任何上都没有子进程
//     平台。

#include <memory>
#include <string>
#include <vector>

#include "agentty/provider/provider.hpp"
#include "agentty/provider/stream_epilogue.hpp"  // StreamResult（概念检查的完整类型）
#include "agentty/util/io.hpp"

namespace agentty::provider::chatgpt {

class ChatGptProvider {
public:
    ChatGptProvider();
    ~ChatGptProvider();
    ChatGptProvider(const ChatGptProvider&) = delete;
    ChatGptProvider& operator=(const ChatGptProvider&) = delete;
    provider::StreamResult stream(const maya::unlocked& cap, provider::Request req,
                                  provider::EventSink sink);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// 使用 ChatGPT OAuth 登录时暴露的 Codex 模型
// 线-up。从账户的`/models` 目录（镜像代码x-rs）实时获取；
// 为进程缓存。离线/未登录时，会退回到一个小的捆绑列表。
[[nodiscard]] std::vector<ModelInfo> list_models(Io, const maya::unlocked& cap);

// 已经-fetched 目录 — NEVER的快照触及网络。
// 为空，直到第一个活动list_models()成功。这是 UI-thread
// 调用 站点（提供商切换、召回验证）必须使用的：当 缓存 冷时，
// list_models() 在 目录 HTTP round-trip 上阻塞，
// 当切换到 ChatGPT 离线时，这会将 状态更新函数 冻结为完整的 超时。
[[nodiscard]] std::vector<ModelInfo> list_models_cached();

// 帐户的默认模型段（第一个目录条目）。需要具体型号 ID 的调用者
// - e.g。 provider-switch 默认 - 应该使用它
// 而不是硬编码帐户可能不提供的 slug。
[[nodiscard]] std::string default_model(Io, const maya::unlocked& cap);

static_assert(provider::Provider<ChatGptProvider>);

} // namespace agentty::provider::chatgpt
