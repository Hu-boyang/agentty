#pragma once
// agentty::provider::anthropic::AnthropicProvider
// —通过将抽象请求翻译成anthropic::transport 调用来满足
// `provider::Provider`概念的具体适配器。

#include <utility>

#include "agentty/provider/provider.hpp"
#include "agentty/provider/stream_epilogue.hpp"
#include "agentty/provider/anthropic/transport.hpp"

namespace agentty::provider::anthropic {

class AnthropicProvider {
public:
    // run_stream_sync今天不具备能力；
    // 上限是 Provider-concept 合约（线程 是通过 传输层 锁 的合约）。
    provider::StreamResult stream(const maya::unlocked&, provider::Request req,
                                  provider::EventSink sink) {
        Request areq;
        provider::lower_shared(areq, req);   // model/system/messages/tools/max_tokens/auth/retry/effort
        return run_stream_sync(std::move(areq), std::move(sink), std::move(req.cancel));
    }
};

static_assert(provider::Provider<AnthropicProvider>);

} // namespace agentty::provider::anthropic
