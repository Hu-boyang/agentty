#pragma once
// Codex (ChatGPT)OAuth接线。这里的每个常量都将 agentty
// 标识为 OpenAI 的身份验证边缘，并镜像 Codex CLI 自己的登录流程 -
// 更改其中任何一个都会破坏流程，因此它们位于一个提供商 -scoped 位置，而不是泄漏到
// `agentty::auth` 中。
//
// 与codex-rs/login/src (auth/manager.rs、server.rs、
// pkce.rs)和安装的`codex` 二进制程序相反。流程是 Authorization
// -Code + PKCE (S256)，在端口 1455 上具有 FIXED 环回重定向
// — 与 Claude 代码的 OAuth 形状相同，但 OpenAI 要求代码返回到本地
// HTTP 侦听器，而不是托管的 Past-the-code 页面。

namespace agentty::provider::chatgpt {

struct OAuthConfig {
    // Codex CLI的公共OAuth客户端ID。使用它可以让 agentty 运行用户
    // 现有的 ChatGPT (Codex) 会话，就像 Anthropic 路径运行
    // Claude.ai 会话一样。
    static constexpr const char* client_id     = "app_EMoamEEZ73f0CkXaXp7hrann";
    static constexpr const char* issuer        = "https://auth.openai.com";
    static constexpr const char* authorize_url = "https://auth.openai.com/oauth/authorize";
    static constexpr const char* token_url     = "https://auth.openai.com/oauth/token";

    // OpenAI将环回重定向固定到该确切的主机+端口+路径。
    // 本地回调服务器MUST绑定1455并服务/auth/callback.
    static constexpr const char* redirect_uri  = "http://localhost:1455/auth/callback";
    static constexpr int         callback_port = 1455;
    // 当采用 1455 时使用（Codex 登录保持运行）。同样在 OpenAI
    // 的重定向上允许-list 该客户端； codex-rs 也回归到它。
    static constexpr int         callback_fallback_port = 1457;
    static constexpr const char* callback_path = "/auth/callback";

    static constexpr const char* scopes =
        "openid profile email offline_access "
        "api.connectors.read api.connectors.invoke";

    // 作为授权查询参数逐字发送，因此 OpenAI 将我们视为 Codex
    // CLI (驱动简化同意屏幕 + org-in-id_token)。
    static constexpr const char* originator = "codex_cli_rs";

    // Codex CLI版本agentty模拟Codex后端请求。
    // CRITICAL：/backend-api/codex/models 目录
    // 端点控制 `client_version` 上返回的模型列表 — old/unknown
    // 版本获得带有 空 `{"models":[]}` 的 200（服务器从 过期
    // 客户端保留 目录）。 agentty 自己的版本 (0.2.x) 远远低于
    // 任何 Codex 最低版本，因此我们 必须 在此发送当前 Codex
    // 版本（client_version 查询参数和 codex_cli_rs/
    // <ver> 用户-agent），或者 选择面板 回退到捆绑猜测，并且帐户不再
    // 提供的 slug 上的第一个转 400 秒。当 目录 再次变空时，撞一下它。
    static constexpr const char* codex_client_version = "0.145.0";

    // RFC-8693令牌-exchange用于从签名的id_token铸造可用的OpenAI
    // API密钥——这就是让agentty 调用 API直接
    // WITHOUT存在的法典二进制程序。
    static constexpr const char* token_exchange_grant =
        "urn:ietf:params:oauth:grant-type:token-exchange";
    static constexpr const char* requested_token_apikey = "openai-api-key";
    static constexpr const char* subject_token_type_idtoken =
        "urn:ietf:params:oauth:token-type:id_token";
};

} // namespace agentty::provider::chatgpt
