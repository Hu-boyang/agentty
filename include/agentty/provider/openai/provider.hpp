#pragma once
// agentty::provider::openai::OpenAIProvider-通过将
// 抽象请求翻译成openai::transport 调用来满足`provider::Provider`
// 概念的具体适配器。持有一个端点（基础 URL / 端口 / tls），因此一个进程可以
// 定位 OpenAI、Groq、OpenRouter、本地 Ollama 等。

#include <utility>

#include "agentty/provider/provider.hpp"
#include "agentty/provider/stream_epilogue.hpp"
#include "agentty/provider/openai/transport.hpp"

namespace agentty::provider::openai {

class OpenAIProvider {
public:
    OpenAIProvider() = default;
    explicit OpenAIProvider(Endpoint endpoint) : endpoint_(std::move(endpoint)) {}

    // run_stream_sync今天不具备能力；
    // 上限是 Provider-concept 合约（线程 是通过 传输层 锁 的合约）。
    provider::StreamResult stream(const maya::unlocked&, provider::Request req,
                                  provider::EventSink sink) {
        Request oreq;
        provider::lower_shared(oreq, req);          // 共享核心
        oreq.context_window = req.context_window;   // OpenAI-family：Ollama num_ctx
        oreq.session_key    = req.session_key;       // prompt_cache_key路由
        oreq.affinity_key   = req.affinity();        // x-session-id
        oreq.endpoint       = endpoint_;
        return openai::run_stream_sync(std::move(oreq), std::move(sink), std::move(req.cancel));
    }

    [[nodiscard]] const Endpoint& endpoint() const noexcept { return endpoint_; }

private:
    Endpoint endpoint_;
};

static_assert(provider::Provider<OpenAIProvider>);

} // namespace agentty::provider::openai
