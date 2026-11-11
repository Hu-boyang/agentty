#pragma once
// agentty::provider::kimi — Kimi代码OAuth(设备流程)。
//
// Kimi代码(Moonshot AI)使用OAuth 2.0设备授权授权 (RFC 8628)
// 针对`https://auth.kimi.com`来验证终端客户端，
// 然后在`https://api.kimi.com/coding/v1`处提供OpenAI-compatible聊天API。
//
// 与GitHub Copilot不同，Kimi的设备-flow access_token
// 使用DIRECTLY作为API承载——不存在单独的代理-token交换步骤。
// 当 access_token 过期时，将通过标准 `refresh_token` 授权 进行刷新。
// 因此，该模块是copilot_oauth的更简单的
// 兄弟：请求设备代码、轮询令牌包、持久化它并透明地刷新。
//
// 三个OAuth端点，所有POST形成-encoded到OAuth主机：
//   POST /api/oauth/device_authorization        → 设备码与用户码
//   POST /api/oauth/token (grant=device_code)   → 轮询获取令牌组
//   POST /api/oauth/token (grant=refresh_token) → 刷新后的令牌组
//
// 持久捆绑包存在于`config_dir()/kimi_credentials.json`中，
// 与每个其他提供商凭据一样用相同的cred_crypt 接口边界密封。

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <stop_token>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "agentty/auth/auth.hpp"   // OAuth错误
#include "agentty/util/io.hpp"
#include "agentty/util/lock_levels.hpp"

namespace agentty::provider::kimi {

// 持久化的Kimi令牌包。 `access_token`是API的承载者；
// `refresh_token` 过期后会铸造一枚新的。
struct KimiToken {
    std::string access_token;        // 发送为 `Authorization: Bearer <token>`
    std::string refresh_token;       // 与grant_type=refresh_token一起使用
    std::int64_t expires_at_ms = 0;  // skew-safe LOCAL 到期（现在 + expires_in）
    std::string token_type;          // "Bearer"
    std::string scope;               // 授予范围（信息性）

    [[nodiscard]] bool expired(std::int64_t skew_ms = 0) const noexcept {
        return expires_at_ms != 0 && now_ms() >= expires_at_ms - skew_ms;
    }
    [[nodiscard]] bool valid() const noexcept { return !access_token.empty(); }

    static std::int64_t now_ms() noexcept;
};

// UI 接口边界:agentty向用户（"go to <url> and enter <code>")显示这一点。
struct DeviceCode {
    std::string verification_uri;            // https://auth.kimi.com/device (回退)
    std::string verification_uri_complete;   // pre-filled URL，嵌入代码
    std::string user_code;                   // e.g。 "WDJB-MJHT"
    int         expires_in = 900;
};
using DeviceCodeSink = std::function<void(const DeviceCode&)>;
// 当请求时登录停止（Esc，较新的尝试，关闭）。
using CancelProbe    = std::stop_token;

// ── 设备-flow 登录 ─────────────────────────────────────────────────────
// 请求设备代码（传送到`on_device_code`以便弹出面板
// 可以显示它），然后阻止-polls令牌端点直到用户批准、
// 代码过期或`cancelled()`跳闸。成功后，令牌包将被保留并返回。
// `timeout_s` 限制整个轮询循环。
[[nodiscard]] std::expected<KimiToken, auth::OAuthError>
login(const maya::unlocked&, int timeout_s, DeviceCodeSink on_device_code,
      CancelProbe cancelled);

// ── 凭据 存储 ───────────────────────────────────────────────────────
[[nodiscard]] std::filesystem::path credentials_path();
[[nodiscard]] std::optional<KimiToken> load_token(const maya::below<maya::leaf>&);
bool save_token(const maya::below<maya::leaf>&, const KimiToken& tok);
bool clear_credentials(Io, const maya::below<maya::leaf>&);

// 选择面板视图的廉价"is a Kimi credential present?"(stat-cached，锁-free)。
[[nodiscard]] bool signed_in();

// 强制下一个fresh_token()刷新，即使缓存的令牌看起来有效（在API
// 返回401 mid-turn之后调用）。
void invalidate_cached_token();

// ── 人数 -turn 符号 ─────────────────────────────────────────────────────────
// 返回 API 的有效访问令牌，当持久化令牌过期（或请求强制刷新）时，
// 通过 refresh_token 进行刷新。
// 未登录或刷新失败时为 nullopt。
[[nodiscard]] std::optional<KimiToken> fresh_token(const locks::below_auth_refresh&);

// ── 测试接口边界 ───────────────────────────────────────────────────────────
// 解析将令牌-endpoint JSON 正文转换为KimiToken（从相
// 对于`now_ms`的`expires_in`计算的到期日）。当缺少必填字段时
// nullopt。暴露出来所以传输 解析是没有网络的unit-testable。
[[nodiscard]] std::optional<KimiToken>
parse_token_response(std::string_view json_body, std::int64_t now_ms);

// ── 设备身份头文件 ─────────────────────────────────────────────────
// X-Msh-*头文件块Kimi的服务器期望来自kimi_code_cli
// 客户端的OAuth 和 API(型号/聊天)请求：平台、版本、
// 设备名称/型号/操作系统以及稳定的per-machine设备ID。
// 保存在一个地方，以便登录流程和推理 传输层 发送一组相同的数据。
// 返回小写（名称，值）对。
[[nodiscard]] std::vector<std::pair<std::string, std::string>> device_headers();

} // namespace agentty::provider::kimi
