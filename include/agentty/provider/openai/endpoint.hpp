#pragma once
// OpenAI-compatible端点描述，就其本身而言，
// 选择(并且通过它Model)可以在不拉入传输层的情况下保持一个，
// 其包括msg.hpp以及Model本身。

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace agentty::provider::openai {

// OpenAI-compatible后端的端点描述。在启动时构造一次（
// 从settings/env/--provider）并线程化到每个请求中。
//
//   主机 — TLS SNI + Host 头文件 + 证书引脚 (e.g."api.openai.com")。
//   端口——通常为443； Ollama 默认为 11434，高于普通 HTTP。
//   路径——聊天完成路径，默认"/v1/chat/completions"。
//   models_path——型号列表路径，默认"/v1/models"。
//   use_tls —对于本地http://Ollama/llama.cpp服务器为假。
//   标签——供应商标签印在ModelInfo上(对于选择面板)。
struct Endpoint {
    std::string host        = "api.openai.com";
    std::uint16_t port      = 443;
    std::string path        = "/v1/chat/completions";
    std::string models_path = "/v1/models";
    bool        use_tls     = true;
    std::string label       = "openai";
    // 当为真时，使用Ollama的NATIVE/api/chat协议(NDJSON
    // 数据流，结构化message.tool_calls)
    // 而不是OpenAI-compat/v1/chat/completions垫片。
    // 填充程序使弱本地模型泄漏工具 调用 作为 `content` 中的原始 JSON
    // （即使在裸露的 "hi" 上）；本机端点应用模型的聊天模板并干净地回答。
    // 仅针对 "ollama" 预设进行设置。
    bool        native_api  = false;
    // 发送`prompt_cache_key`(会话-pinned 提示词缓存)。
    // 仅 对于接受该字段的主机：严格 OpenAI-compat 层（Groq，
    // Gemini）400 未知的 正文 字段并杀死回合（#93）。从注册表行（
    // ProviderPreset::accepts_prompt_cache_key）
    // 开始设置； false 对于自定义主机、本地服务器和未选择加入的每一行。
    bool        prompt_cache_key = false;
    // 对于不接受 `Authorization: Bearer` 的网关的自定义身份验证
    // 头文件 NAME (e.g."X-API-Key")。空（默认）保留旗手 头文件。
    // 设置后，密钥原始显示为 `<name>: <key>` — 无 "Bearer " 前缀。
    // 从 --auth-header 到 provider::parse_selection
    // 填充。
    std::string auth_header_name;

    // 额外的静态请求 头文件 在对此端点的 每个 请求上逐字注入（在身份验证 头
    // 文件 之后）。 GitHub Copilot 在每个 调用 上都需要其
    // 编辑器标识块 (Copilot-Integration-Id / Editor
    // -Version / …)，或者代理对于普通 OpenAI 系列返回
    // 400/403. 空。名称应为小写（头文件 名称为 case-insensitive，
    // 其余 agentty 为小写）。
    std::vector<std::pair<std::string, std::string>> extra_headers;

    // 针对常见的免费/托管后端的Built-in预设。传递裸名称（"openai"、"groq"、
    // "openrouter"、"together"、"cerebras"、
    // "ollama"）或完整的 "host[:port]" 字符串。
    [[nodiscard]] static Endpoint from_spec(std::string_view spec);

    // 结构平等，因此接口边界仅在其改变时才可以发布
    // Model的选择。
    [[nodiscard]] bool operator==(const Endpoint&) const = default;
};

} // namespace agentty::provider::openai
