#pragma once
// agentty::provider::kimi::KimiProvider —本地 Kimi 代码提供商。
//
// Kimi Code的推论API是OpenAI-Chat-compatible，
// 在固定基础URL（https://api.kimi.com/coding/v1),，因此这是共享openai
// 传输层上的THIN包装器：在每次转动时，它确保新的OAuth访问令牌（通过
// 来自持久设备-flow捆绑包的refresh_token的auto-refreshed），
// 构建端点，将令牌标记为请求身份验证，并将 流式传输 委托给
// openai::run_stream_sync 401（令牌提前撤销）
// 触发一次强制刷新 + 重试。
//
// 它是 LongLived 提供者（拥有代币 缓存)——与 Anthropic/ChatGPT/Copilot
// 提供者一样，在 main() 中构建一次。

#include <string>
#include <vector>

#include "agentty/domain/catalog.hpp"       // ModelInfo
#include "agentty/provider/provider.hpp"
#include "agentty/provider/openai/transport.hpp"  // openai::Endpoint
#include "agentty/provider/stream_epilogue.hpp"
#include "agentty/util/io.hpp"

namespace agentty::provider::kimi {

class KimiProvider {
public:
    KimiProvider() = default;

    provider::StreamResult stream(const maya::unlocked& cap, provider::Request req,
                                  provider::EventSink sink);

    // 构建Kimi推理端点。由 stream() 和 list_models() 共享。
    static provider::openai::Endpoint make_endpoint();
};

// 每个后端必须满足流式传输合同。 Kimi 是唯一一个缺少这条线的提供商——它
// 碰巧符合要求，但没有 PROVED，所以这里的签名漂移将作为
// main.cpp 的 分派 中的 链接 错误而不是清晰的
// "KimiProvider does not satisfy Provider" 出现。
static_assert(provider::Provider<KimiProvider>);

// 来自 Kimi's /models 的帐户的实时模型 目录 (
// 当离线/未登录时回退到小的捆绑列表)。
// 刷新OAuth令牌(rank-ordered 锁)，因此调用者不持有锁。
[[nodiscard]] std::vector<ModelInfo> list_models(Io, const maya::unlocked& cap);

// 帐户的默认模型段（第一个 目录 条目）或安全 回退。
[[nodiscard]] std::string default_model(Io, const maya::unlocked& cap);

// 删除缓存的模型目录，以便下一个list_models()重新-fetches。
void invalidate_model_cache();

} // namespace agentty::provider::kimi
