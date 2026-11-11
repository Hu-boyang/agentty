#pragma once
// Anthropic-specific OAuth接线。  这里的每个常量都将 agentty
// 标识到 Anthropic 的边缘 - 更改其中任何一个都会破坏流程，因此它们被保留
// 在一个提供程序-scoped 位置，而不是泄漏到 `agentty::auth` 中。

namespace agentty::provider::anthropic {

struct OAuthConfig {
    // Claude代码的公共客户端ID——使用它可以让我们利用用户现有的Claude.ai
    // OAuth会话。
    // 请参阅 memory/project_claude_code_wire.md 了解基本原理。
    static constexpr const char* client_id     = "9d1c250a-e61b-44d9-88ed-5944d1962f5e";
    static constexpr const char* authorize_url = "https://claude.ai/oauth/authorize";
    static constexpr const char* token_url     = "https://platform.claude.com/v1/oauth/token";
    static constexpr const char* redirect_uri  = "https://platform.claude.com/oauth/code/callback";
    static constexpr const char* scopes =
        "user:profile user:inference user:sessions:claude_code "
        "user:mcp_servers user:file_upload";
};

} // namespace agentty::provider::anthropic
