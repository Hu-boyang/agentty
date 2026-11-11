// agentty::provider::dispatch_stream——单个提供商-routing点。
// 请参阅 dispatch.hpp 了解为什么它存在于此而不是内联在 main.cpp 中，
// 以及为什么它是提供者 -agnostic (类型 -erased 路由)而不是命名具体
// 的 传输层。

#include "agentty/provider/dispatch.hpp"

#include <utility>

#include "agentty/provider/ollama/provider.hpp"
#include "agentty/provider/openai/provider.hpp"
#include "agentty/provider/selection.hpp"
#include "agentty/util/logx.hpp"

namespace agentty::provider {

namespace {
// 所采取路线的人类标签——该回合由 ADAPTER (瘦丑的 per-provider
// 垫片) 拥有。当模型行为不当时，这是最有用的事实：相同的模型 ID 可以通过不同
// 主机上的不同方言实现 数据流（在聚合器上通过 OpenAI-compat 实现
// claude-*，在 Copilot 上通过 /responses 实现 gpt-*，
// 但在 Azure 上实现 /chat/completions）。
// 槽位行在注册表 (slot_tag)中携带它们的标签；此处仅命名
// slot-less 臂（ACP / per-call 兼容）。
[[nodiscard]] std::string_view route_tag(const Selection& sel) noexcept {
    if (sel.kind == Kind::ExternalAcp)  return "acp";
    if (const auto s = long_lived_slot(sel); s != RouteSlot::None)
        return slot_tag(s);
    if (sel.openai_endpoint.native_api) return "ollama-native";
    return "openai-chat";
}
} // 命名空间结束。

LongLived long_lived_slot(const Selection& sel) {
    // ONE字段被读取。 注册表 的 routing_consistent
    // () static_assert 证明每个 LongLived 行都带有
    // 唯一的槽，因此无需派生任何内容，也无需漂移：行 IS 是路由决策。
    // Row-less选择(与预设不匹配的自定义主机)通过构造
    // 落入per-call 传输层，因为它们没有要读取的行。
    return sel.row ? sel.row->route : RouteSlot::None;
}

StreamResult dispatch_stream(const maya::unlocked& cap, const ProviderRouter& router,
                             const Selection& sel, Request req, EventSink sink) {
    // THE转至指纹。一条调试行命名了形成此请求 之前
    // 的每个异质性决策，任何 字节 都命中了 传输：
    // 哪个适配器、哪个型号ID、什么努力在钳制中幸存下来、
    // 工具是否被广告（以及多少）、weak-model JSON协议
    // 是否开启、是否请求推理显示。当用户报告
    // "model X does Y on provider Z" 时，
    // 这一行 + end-of-turn 结果行包含了整个故事。
    AGT_LOG(Model, Debug, "dispatch.turn",
            "route={} provider={} model={} effort={} tools={} json_protocol={} "
            "show_reasoning={} ctx_window={} max_tokens={} retry={}",
            route_tag(sel), sel.catalog_id(), req.model,
            req.effort.empty() ? "off" : req.effort, req.tools.size(),
            req.json_protocol ? 1 : 0, req.show_reasoning ? 1 : 0,
            req.context_window, req.max_tokens, req.retry_count);
    // 1)外部ACP代理子进程。通过擦除的 fn 路由（绑定在
    //    main()到stream_external_acp)，
    //    因此分派不依赖于acp TU。代理 ID 在选择上传播。
    if (sel.kind == Kind::ExternalAcp) {
        return router.external_acp(cap, sel.acp_agent_id, std::move(req), std::move(sink));
    }

    // 2)Long-lived本机提供者(Anthropic，ChatGPT/Codex)：持有
    //    穿越-turn OAuth/connection状态，
    //    在main()建造过一次。该槽源自 注册表 数据，不 来自标签梯形图。
    if (const LongLived slot = long_lived_slot(sel); slot != LongLived::None) {
        const auto& fn = router.long_lived[static_cast<std::size_t>(slot)];
        if (!fn) {
            // 槽main()从不绑定的路由行。当 main() 绑定每个枚举器时
            // 不会发生（注册表 证明 rows↔slots 1:1），
            // 但如果重构删除一个，则会以命名的 StreamError 失败 —
            // 而不是通过 工作线程 线程 展开的 bad_function_call。
            AGT_LOG(Model, Error, "dispatch.unbound_slot", "route={}",
                    slot_tag(slot));
            sink(Msg{StreamError{std::string{"internal: provider slot '"}
                                 + std::string{slot_tag(slot)}
                                 + "' is not bound", std::nullopt}});
            return StreamResult::failed("unbound provider slot");
        }
        return fn(cap, std::move(req), std::move(sink));
    }

    // 3) Per-call 传输层，从活动端点全新构建，因此
    //    host/path/tls改变在下一回合进行副作用。 Ollama 讲其母语 /api/chat
    //    NDJSON 方言；每个其他 OpenAI-compatible 端点 (
    //    openai/groq/openrouter/together/cerebras/llama.cpp/custom)
    //    都会经过共享的 OpenAI 传输层。
    // 11434是Ollama的PORT，不是其协议的证明：可以告诉llama.cpp
    // 在那里服务，然后/api/chat每轮404s（#73）。 Ask主机一次；
    // 如果没有/api/tags，则将其视为OpenAI-compatible。
    if (openai::endpoint_speaks_native(::agentty::IoAccess::grant(), sel.openai_endpoint)) {
        ollama::OllamaProvider p{sel.openai_endpoint};
        return p.stream(cap, std::move(req), std::move(sink));
    }
    openai::Endpoint ep = sel.openai_endpoint;
    if (ep.native_api) {
        ep.native_api  = false;
        ep.path        = "/v1/chat/completions";
        ep.models_path = "/v1/models";
    }
    openai::OpenAIProvider p{ep};
    return p.stream(cap, std::move(req), std::move(sink));
}

StreamResult dispatch_stream(const maya::unlocked& cap, const ProviderRouter& router,
                             Request req, EventSink sink) {
    // 请求是BUILT的提供商的路由。它的型号 id、
    // auth 和 提示词 来自该选择；实时的可能已经继续（
    // 用户在本轮运行时切换了提供商），并将其发送到那里，
    // 将一个提供商的模型和密钥与另一个提供商的端点混合在一起。
    // 没有建立的请求进入现场选择。 active() 是 by-value 快照，
    // 因此 工作线程 看不到撕裂的 mid-select() 端点。
    const Selection sel = req.selection ? *req.selection : active();
    return dispatch_stream(cap, router, sel, std::move(req), std::move(sink));
}

} // namespace agentty::provider
