#pragma once
// agentty::provider::ollama::OllamaProvider-通过将
// 抽象请求翻译成ollama::transport/api/chat 调用来满足
// `provider::Provider`概念的具体适配器。保存本地 Ollama 服务器的端点
// (host/port)。

#include <utility>

#include "agentty/provider/provider.hpp"
#include "agentty/provider/stream_epilogue.hpp"
#include "agentty/provider/ollama/transport.hpp"

namespace agentty::provider::ollama {

class OllamaProvider {
public:
    OllamaProvider() = default;
    explicit OllamaProvider(Endpoint endpoint) : endpoint_(std::move(endpoint)) {}

    // run_stream_sync今天不具备能力；
    // 上限是 Provider-concept 合约（线程 是通过 传输层 锁 的合约）。
    provider::StreamResult stream(const maya::unlocked&, provider::Request req,
                                  provider::EventSink sink) {
        Request oreq;
        provider::lower_shared(oreq, req);          // 共享核心
        oreq.context_window = req.context_window;   // num_ctx 尺寸
        oreq.json_protocol  = req.json_protocol;    // weak-model JSON工具频道
        oreq.endpoint       = endpoint_;
        return ollama::run_stream_sync(std::move(oreq), std::move(sink), std::move(req.cancel));
    }

    [[nodiscard]] const Endpoint& endpoint() const noexcept { return endpoint_; }

private:
    Endpoint endpoint_;
};

static_assert(provider::Provider<OllamaProvider>);

} // namespace agentty::provider::ollama
