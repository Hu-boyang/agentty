#pragma once
// agentty::provider::copilot —本机 GitHub Copilot OAuth + 令牌交换。
//
// 副驾驶的聊天API是OpenAI-Chat-compatible，因此推理重用通用的openai 传输层。
// 唯一的 Copilot-specific 工作是 AUTH，一个两个 -stage 流程：
//
//   1. GitHub DEVICE FLOW→长-lived GitHub用户令牌(ghu_…)，持久化
//      一次。 （普通设备流程 - 没有 PKCE，没有环回服务器，没有浏览器重定向。
//      用户在 github.com/login/device. 处键入代码）
//   2. TOKEN EXCHANGE → GET api.github.com/copilot_internal/v2/token 与
//      ghu_令牌 → SHORT-LIVED Copilot 代理令牌（约 30 分钟 TTL）加上
//      per-account 推理主机（`endpoints.api`）。 Auto-refreshed在-process到期之前，
//      具有clock-skew安全性和cross-process de-dup。
//
// 这反映了ChatGPT/Codex OAuth模块的形状；其中 Codex 使用 PKCE
// + refresh_token，Copilot 使用持久 ghu_ 令牌作为刷新源。

#include <cstdint>
#include <cstdlib>
#include <expected>
#include <functional>
#include <stop_token>
#include <optional>
#include <string>
#include <string_view>

#include "agentty/auth/auth.hpp"   // OAuth错误
#include "agentty/util/io.hpp"
#include "agentty/util/lock_levels.hpp"

namespace agentty::provider::copilot {

using auth::OAuthError;

// ── 向副驾驶介绍 Editor/plugin 身份 ───────────────────────────
// Copilot 将其 per-account MODEL ROLLOUT 键入它所看到的
// （编辑器 × 插件 × API）版本，而不仅仅是帐户 — 因此 过期 引脚会使较新的模型
// （e.g. a just-shipped Claude/GPT）从自动会话中消失，
// 即使相同的登录在 up-to-date VS 代码中看到它们。这些在 运行时 上是可覆盖的，
// 因此您可以镜像已安装的 VS Code Copilot Chat 恰好（对于 vscode/<ver>，
// 为帮助 → 关于；对于 copilot-chat/<ver>，为扩展 →
// GitHub Copilot Chat），而无需重新编译。默认跟踪最近的 VS 代码。
[[nodiscard]] inline const char* client_editor_version() {
    if (const char* e = std::getenv("AGENTTY_COPILOT_EDITOR_VERSION"); e && *e) return e;
    return "vscode/1.105.1";
}
[[nodiscard]] inline const char* client_plugin_version() {
    if (const char* e = std::getenv("AGENTTY_COPILOT_PLUGIN_VERSION"); e && *e) return e;
    return "copilot-chat/0.32.0";
}
[[nodiscard]] inline const char* client_user_agent() {
    if (const char* e = std::getenv("AGENTTY_COPILOT_USER_AGENT"); e && *e) return e;
    return "GitHubCopilotChat/0.32.0";
}

// 经久耐用的GitHub由凭据装置流向。
// 这就是`agentty login → GitHub Copilot`所坚持的；它不会自行过期（撤销会
// 强制 re-login）。
struct GithubToken {
    std::string access_token;   // ghu_…（或gho_…用于遗留）；交换源
    std::string token_type;     // "bearer"
    std::string scope;          // 授予范围（信息性）
};

// 来自交易所的short-lived Copilot代理令牌。
// 与 GithubToken 一起缓存并透明刷新。
struct CopilotToken {
    std::string token;              // 发送为 `Authorization: Bearer <token>`
    std::int64_t expires_at_ms = 0; // skew-safe LOCAL 到期（现在 + refresh_in + 松弛）
    std::string endpoint_api;       // 来自 `endpoints.api` 的推理基 URL
    std::string sku;                // e.g。 "free_limited_copilot"
    bool chat_enabled = true;       // false ⇒ 帐户没有聊天权限
    bool quota_exhausted = false;   // free-tier聊天配额达到

    [[nodiscard]] bool expired(std::int64_t skew_ms = 0) const noexcept {
        return expires_at_ms != 0 && now_ms() >= expires_at_ms - skew_ms;
    }
    [[nodiscard]] bool valid() const noexcept { return !token.empty(); }

    static std::int64_t now_ms() noexcept;
};

// ── 设备-flow 登录 ────────────────────────────────────────────────────
// UI 接口边界:agentty向用户（"go to <url> and enter <code>")显示这一点。
struct DeviceCode {
    std::string verification_uri;   // https://github.com/login/device
    std::string user_code;          // e.g. "31B0-2B8C"
    int         expires_in = 900;
};
using DeviceCodeSink = std::function<void(const DeviceCode&)>;
// 当请求时登录停止（Esc，较新的尝试，关闭）。
using CancelProbe    = std::stop_token;

// 运行完整的设备流程：请求代码，调用`on_device_code`
// 以便主机可以显示它（+打开浏览器），然后轮询直到用户授权。
// 成功时保留所得的GithubToken。阻塞；荣誉`cancelled`。
[[nodiscard]] std::expected<GithubToken, OAuthError>
login(const maya::unlocked&, int timeout_s, DeviceCodeSink on_device_code,
      CancelProbe cancelled);

// 账户的实际计划+配额权利，来自GET
// api.github.com/copilot_internal/user——账户可以使用其
// 模型类的AUTHORITATIVE源（/models 目录是全局的并且对每次
// -account访问进行NOT编码）。 premium_available 推出高级型号系列（
// Claude、Gemini、GPT-5.x、o-series）；当它为 false 时，
// 只有 base/free 型号（gpt-4o 系列）可用。
struct Entitlement {
    std::string plan;               // copilot_plan，e.g。 "individual"
    std::string sku;                // access_type_sku，e.g。 "free_limited_copilot"
    bool chat_enabled = true;
    bool premium_available = true;  // premium_interactions权利>0或无限制
    bool known = false;             // false = 无法获取（回退到许可）
};

// 获取（并简单地缓存)帐户权利。 Best-effort：任何失败
// 都会返回 {known=false}，调用者应该降级 PERMISSIVELY
// （显示所有内容），而不是在瞬态网络突发事件上隐藏模型。
// 锁订单：仅采用叶子（其缓存，信用存储）。
[[nodiscard]] Entitlement account_entitlement(const maya::below<maya::leaf>&);

// ── 自动模式选择 ────────────────────────────────────────────
// GitHub的"Auto"模式(POST {API}/models/session)
// 是Free/Limited计划达到高级模型(Claude、GPT-5)的方式：
// SERVER从每个-account集合中选择一个模型并发出聊天请求携带为
// `Copilot-Session-Token`的短-lived签名会话令牌。这是免费 -tier
// 帐户可以运行这些模型的 仅 方式 - 直接请求它们 400 秒。该请求需要当前的
// CAPI API-version 头文件 (kAutoApiVersion)。
struct AutoSession {
    std::vector<std::string> available_models;  // 本次会议可能使用的模型
    std::string selected_model;                 // 服务器的选择（"best"）
    std::string session_token;                  // 签署 JWT → 副驾驶-Session-Token
    std::int64_t expires_at_ms = 0;             // 来自 JWT exp 索赔
    std::string endpoint_api;                    // 本场推理主持人
    [[nodiscard]] bool valid() const noexcept {
        return !session_token.empty()
            && (expires_at_ms == 0 || CopilotToken::now_ms() < expires_at_ms - 30'000);
    }
};
// CAPI API-version/models/session+auto-routed聊天调用需要。
// 可覆盖 (AGENTTY_COPILOT_API_VERSION) 以匹配较新的
// VS 代码副驾驶聊天，因为 API-version 还控制对自动会话列表进行建模。
inline constexpr const char* kAutoApiVersion = "2026-08-01";
[[nodiscard]] inline const char* client_api_version() {
    if (const char* e = std::getenv("AGENTTY_COPILOT_API_VERSION"); e && *e) return e;
    return kAutoApiVersion;
}

// 根据需要返回有效的自动会话fetching/refreshing。当未登录或端点
// 不可用时为 nullopt。 Single-flight 已缓存。
// 锁顺序：取copilot.auto_session，然后(内部)auth.refresh和信用存储。
// 可赎回，持有任何排名不低于 copilot_auto 的东西。
[[nodiscard]] std::optional<AutoSession> auto_session(const locks::below_copilot_auto&);
void invalidate_auto_session(const locks::below_copilot_auto&);

// ── Persistence ──────────────────────────────────────────────────────────
// 信用存储是叶锁。
[[nodiscard]] std::optional<GithubToken> load_github_token(const maya::below<maya::leaf>&);
bool save_github_token(const maya::below<maya::leaf>&, const GithubToken&);
bool clear_credentials(Io, const maya::below<maya::leaf>&);   // 擦除ghu_令牌和缓存
[[nodiscard]] std::filesystem::path credentials_path();

// 当 GitHub 令牌位于磁盘上时为真（i.e。用户已登录）。
// 便宜的;没有网络。代理令牌可能仍需要第一次交换。
[[nodiscard]] bool signed_in();   // 锁-free;安全无视线

// ── 切换/刷新 ─────────────────────────────────────────────
// 返回VALID Copilot代理令牌，根据需要交换或刷新所存储的ghu_令牌。
// 单-flight + 交叉-process 和-duplicated。
// 当未登录或交换失败（聊天禁用、网络、撤销）时为 nullopt。
// 锁指令：取auth.refresh，然后信用存储(叶)。可赎回，
// 不持有排名等于或高于 auth_refresh 的任何内容。
[[nodiscard]] std::optional<CopilotToken> fresh_token(const locks::below_auth_refresh&);

// 在下一个fresh_token()上强制执行re-exchange
// （在401之后从推理端点调用，以防缓存的代理令牌被提前撤销）。
void invalidate_cached_token();

// ── 人数 -account 模型 -support 学习 ─────────────────────────────────
// Copilot的/models 目录宣传账户的BILLING层实际上无法运行
// 的模型——它们仅在推理时间400 "model not supported"，
// 没有可靠的目录信号（policy.state "enabled"在免费层上仍然是
// 400）。所以我们LEARN：当转400s时为不支持，记录型号id；然后，
// list_models 将其从可用集中降级。每个配置目录都保留。
void note_unsupported_model(const maya::below<maya::leaf>&, const std::string& model_id);
[[nodiscard]] bool is_unsupported_model(const maya::below<maya::leaf>&, const std::string& model_id);
// 还记录DID工作的型号，因此已知的-good型号永远不会被stale/transient
// 400降级。
void note_supported_model(const maya::below<maya::leaf>&, const std::string& model_id);
[[nodiscard]] bool is_supported_model(const maya::below<maya::leaf>&, const std::string& model_id);

// ── 纯助残者(测试前) ─────────────────────────────────────
// 解析、`/copilot_internal/v2/token`成功将JSON转换
// 为CopilotToken，应用偏斜-safe LOCAL到期（优选refresh_in+松弛；
// 回落至expires_at；否则保守的25-min默认值）并从`endpoints.api`
// 解析推理主机。 `now_ms` 是可注射的，因此到期数学在测试中是确定性的。
// 当 JSON 没有 `token` 时，返回 nullopt。
[[nodiscard]] std::optional<CopilotToken>
parse_token_envelope(std::string_view json_body, std::int64_t now_ms);

} // namespace agentty::provider::copilot
