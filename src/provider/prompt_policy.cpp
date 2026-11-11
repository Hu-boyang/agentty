#include "agentty/provider/prompt_policy.hpp"

#include "agentty/provider/anthropic/transport.hpp"
#include "agentty/provider/ollama/transport.hpp"
#include "agentty/provider/openai/transport.hpp"
#include "agentty/provider/prompt.hpp"
#include "agentty/provider/selection.hpp"

namespace agentty::provider {

std::string system_prompt_for(Io io, const Selection& selection) {
    // ACP后端是委托代理，而不是模型传输 传输层。它的会话拥有指令；
    // provider::Request.system_prompt 被忽略。
    if (selection.kind == Kind::ExternalAcp) return {};

    if (selection.kind == Kind::OpenAI) {
        // ChatGPT/Codex携带localhost:0哨兵端点，
        // 因为其long-lived 传输层根本不使用通用端点。
        // 其 注册表 功能必须在本地主机检测之前获胜。
        if (selection.is_oauth_native())
            return system_prompt_with_overlay(io, "chatgpt");

        // 紧凑提示是model/endpoint能力决策，而不是提供商-brand决策。
        // 将其保留用于 self-hosted 服务器（Ollama 的本机 API，或 OpenAI
        // -compatible），其中小型模型受益于简短的命令式指令。
        // "Self-hosted" 表示运行 YOU 的服务器——环回、LAN 框（192.168.
        // x.x、gpu-box、*.local）、tailnet——与窗口探测使用的规则相同。
        // 此前仅支持回环地址，因此部署在另一台机器上的 llama.cpp/vLLM
        // 会向 gpt-oss 发送较长的
        // hosted 提示词 and none of the local action rules
        // (#91).
        if (uses_local_prompt(selection))
            return local_system_prompt_for(selection);

        // 托管的 OpenAI 兼容模型使用共享基础提示词和对应的 openai 增量。
        return system_prompt_with_overlay(io, "openai");
    }

    // Anthropic(以及到达此处的任何其他传输)接收共享的agent/tool/RAG策略及其提供者覆盖面板。
    // 提供方适配器只决定该
    // string is encoded on their 传输.
    return system_prompt_with_overlay(io, "anthropic");
}

bool uses_local_prompt(const Selection& selection) {
    if (selection.kind != Kind::OpenAI || selection.is_oauth_native()) return false;
    const auto& ep = selection.openai_endpoint;
    // Ollama 的原生 API 始终对应用户自行运行的服务器。
    if (ep.native_api) return true;
    return !ep.use_tls && openai::detail::is_local_endpoint(ep);
}

std::string local_system_prompt_for(const Selection& selection) {
    return selection.openai_endpoint.native_api ? ollama::system_prompt()
                                                : openai::local_model_system_prompt();
}

} // namespace agentty::provider
