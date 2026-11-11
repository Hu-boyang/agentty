#pragma once
// agentty::provider::chatgpt —本机ChatGPT (Codex) OAuth。
//
// 这是"log in with ChatGPT, just like Claude"路径：
// agentty针对auth.openai.com本身运行Authorization-Code
// + PKCE (S256)流程，铸造令牌(并且通过RFC-8693令牌-exchange，
// 可用的OpenAI API密钥)，并将它们存储在其加密的OWN下凭据 文件 — 不依赖于
// `codex` 二进制程序，也不涉及 ~/.codex 秘密。
//
// 它重用了`agentty::auth`中共享的PKCE/HTTP/加密助手；
// 只有 OpenAI-specific 接线（端点、环回 回调 服务器、
// API-key 交换、单独的令牌存储）位于此处。

#include <cstdint>
#include <expected>
#include <functional>
#include <stop_token>
#include <optional>
#include <string>

#include "agentty/auth/auth.hpp"
#include "agentty/provider/chatgpt/oauth.hpp"
#include "agentty/util/io.hpp"
#include "agentty/util/lock_levels.hpp"

#include <maya/runtime.hpp>

namespace agentty::provider::chatgpt {

// 持续存在的ChatGPT 凭据。 `api_key` 是从 id_token 铸造的 OpenAI
// 密钥 — 首选 API 调用； `access_token` 是原始 OAuth 持有者（用于
// 针对 chatgpt.com/backend-api/codex 和 `account_id`）。
struct CodexCredentials {
    std::string  access_token;
    std::string  refresh_token;
    std::string  id_token;
    std::string  account_id;   // chatgpt_account_id来自id_token的索赔
    std::string  api_key;      // 铸造的OpenAI密钥（可能为空）
    std::int64_t expires_at_ms = 0;
    // 最后一次获得令牌的时间（登录或刷新）。即使访问令牌看起来有效，
    // Codex 也会刷新超过 8 天的任何内容，
    // 因此 long-idle 登录不会在中断后的第一个请求中终止。
    std::int64_t last_refresh_ms = 0;

    [[nodiscard]] bool empty() const noexcept { return access_token.empty(); }
    [[nodiscard]] bool expired(std::int64_t skew_ms = 0) const noexcept;
    // 刷新时间：到期后5分钟内（当服务器未给出expires_in时，
    // 从令牌自身的`exp`读取），或最后刷新时间超过8天。
    // 与 codex-rs 的规则相同。
    [[nodiscard]] bool needs_refresh() const noexcept;
};

// 为什么刷新失败。永久的表示"sign in again"；
// 重试 帮不上忙。这些名称遵循身份验证服务器的错误代码。
enum class RefreshFailure : std::uint8_t {
    None,
    Expired,       // refresh_token_expired
    Reused,        // refresh_token_reused：另一个客户端先轮换了它
    Revoked,       // refresh_token_invalidated/invalid_grant
    Transient,     // 网络，5xx：值得稍后再尝试
};
[[nodiscard]] RefreshFailure classify_refresh_failure(int status, std::string_view body) noexcept;
// 用户一行，或 None/Transient. 为空
[[nodiscard]] std::string_view refresh_failure_message(RefreshFailure) noexcept;

// ── 托肯店(与AnthropicXX credentials.json号分离) ─────────────
[[nodiscard]] std::filesystem::path codex_credentials_path();
// Saves/clears取该店的叶子锁； 加载 是 锁-free（原子
// 重命名），因此视图和签名的-in 探针可以对其进行 调用，但没有能力。
[[nodiscard]] std::optional<CodexCredentials> load_codex_credentials();
bool save_codex_credentials(const maya::below<maya::leaf>& cap, const CodexCredentials& c);
bool clear_codex_credentials(const maya::below<maya::leaf>& cap);

// ── 授权URL ──────────────────────────────────────────────────────────
// 纯：相同（验证者、状态）→相同URL。指向环回重定向（端口 1455，
// 除非登录回退到 1457）。
[[nodiscard]] std::string codex_authorize_url(const auth::PkceVerifier& verifier,
                                              const auth::OAuthState&   state,
                                              std::string_view redirect_uri = OAuthConfig::redirect_uri);

// ── 交互式登录 ──────────────────────────────────────────────────────
// SSH会话自动使用OpenAI的设备-code流程：agentty
// 显示URL+one-time代码并轮询直到任何机器上的浏览器批准它。
// 本地会话保留端口 1455 上的环回 回调 流。
struct CodexDeviceCode {
    std::string verification_url;
    std::string user_code;
};

using CodexDeviceCodeSink = std::function<void(const CodexDeviceCode&)>;
// 当请求时登录停止（Esc，较新的尝试，关闭）。
using CodexCancelProbe = std::stop_token;

// 当环境显式请求设备认证或识别SSH会话时为真。
// AGENTTY_CHATGPT_DEVICE_AUTH=0/1 是对
// 异常 终端 和测试工具的覆盖。
[[nodiscard]] bool codex_device_auth_preferred() noexcept;

// 运行适当的完整流程并返回凭据而不保留它们。仅当调用方确认
// 尝试仍处于活动状态后，才必须执行 保存。
// `on_device_code` 在轮询开始前触发一次。
[[nodiscard]] std::expected<CodexCredentials, auth::OAuthError>
codex_login(Io, const maya::unlocked&, int timeout_s = 900, CodexDeviceCodeSink on_device_code = {},
            CodexCancelProbe cancelled = {});

// 显式设备流程与同方-effect-free完成合约。
[[nodiscard]] std::expected<CodexCredentials, auth::OAuthError>
codex_device_login(const maya::unlocked&, CodexDeviceCodeSink on_device_code, int timeout_s = 900,
                   CodexCancelProbe cancelled = {});

// ── 交换/刷新/薄荷(为状态更新函数 + 测试而使用) ────────────
// 将授权码(来自回调)交换为令牌。
[[nodiscard]] std::expected<CodexCredentials, auth::OAuthError>
codex_exchange_code(const auth::OAuthCode& code, const auth::PkceVerifier& verifier,
                    std::string_view redirect_uri = OAuthConfig::redirect_uri);

// 经由refresh_token (JSON 正文，根据Codex协议)刷新
// 过期的访问令牌。返回一个新的凭据； re-mints API 键。
[[nodiscard]] std::expected<CodexCredentials, auth::OAuthError>
codex_refresh(const CodexCredentials& current);

// 如果存储的访问令牌是过期，则返回有效的凭据，就地刷新（并持续）。
// 工作线程-thread 安全。当 ChatGPT 凭据 不为
// 已保存 时为空可选。刷新在 auth_refresh
// 通道上一次运行一个，然后占用（叶）存储。
[[nodiscard]] std::optional<CodexCredentials>
codex_fresh_credentials(const locks::below_auth_refresh& cap);

// 多么刷新的尝试啊。 `creds` 是接下来要使用的内容（成功则刷新，
// 否则保存存储）； `failure` 说明了为什么它不起作用。
struct RefreshOutcome {
    std::optional<CodexCredentials> creds;
    RefreshFailure                  failure = RefreshFailure::None;
};

// 服务器用该访问令牌表示401：立即刷新，无论到期时间如何，
// 除非另一个调用者已经替换了该令牌（然后采用他们的）。
// 与codex-rs的未授权恢复相同。
[[nodiscard]] RefreshOutcome
codex_force_refresh(const locks::below_auth_refresh& cap, std::string_view rejected_access_token);

// codex_fresh_credentials 看到的最后刷新失败，
// 因此请求可以说 "sign in again" 而不是发送无效令牌。成功则清除。
[[nodiscard]] RefreshFailure codex_last_refresh_failure() noexcept;

} // namespace agentty::provider::chatgpt
