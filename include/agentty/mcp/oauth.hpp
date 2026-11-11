#pragma once
// agentty::mcp::oauth-MCP服务器的交互式OAuth
// 2.1登录（规范2026-07-28授权）。驱动完整的浏览器流程并在每个
// 服务器上保留一个颁发者-bound令牌，并在到期时刷新。
//
//   PROTOCOL LOGIC存在于mcp-cpp的<mcp/auth.hpp>(PKCE、
//   RFC 9207 iss验证、DCR/CIMD、令牌解析)中——纯+单元-tested。
//   此 模块是 agentty-side I/O + 生命周期：每个 GET/POST 的 agentty
//   的 HTTP 客户端、跨-platform 环回 回调 服务器、默认浏览器打开以及由服务器键入
//   的 at-rest-encrypted 令牌存储（auth::crypt，chmod 600）。
//
//   流量（agentty mcp-login <server>):
//     1.从.agentty/mcp.json解析<server>→端点URL
//     2.探测端点→401→RFC 9728资源-metadata URL
//        （或接受明确的 --metadata URL）
//     3、GET资源元数据→授权服务器→GET其元数据
//     4.动态客户端注册（application_type=本机）[或CIMD]
//     5.打开浏览器在PKCE处授权URL；捕获环回重定向
//     6.验证状态+RFC 9207 iss，兑换码→发行者-bound代币
//     7.密封+保留下~/.agentty/mcp_tokens/<server>.JSON
//
//   在调用时间，HTTP 传输层向bearer_for(
//   服务器)请求新的授权头文件；过期的令牌会被透明地刷新。

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include "agentty/util/io.hpp"
#include "agentty/util/lock_levels.hpp"

#include <maya/runtime.hpp>

namespace agentty::mcp::oauth {

// 服务器的一个持久的、发行者-bound令牌记录（镜像mcp::
// auth::AccessToken+刷新簿记）。
struct StoredToken {
    std::string  access_token;
    std::string  token_type = "Bearer";
    std::string  refresh_token;
    std::string  issuer;            // AS 铸造了它（重放守卫）
    std::string  resource;          // 音频
    std::string  client_id;         // 用于刷新
    std::int64_t expires_at_ms = 0; // 0 ⇒ 无已知期限

    [[nodiscard]] std::string authorization_header() const {
        return token_type + " " + access_token;
    }
    [[nodiscard]] bool expired(std::int64_t skew_ms = 60'000) const noexcept {
        return expires_at_ms != 0 &&
               now_ms() >= expires_at_ms - skew_ms;
    }
    static std::int64_t now_ms() noexcept;
};

// 登录尝试的结果。
struct LoginResult {
    bool        ok = false;
    std::string message;   // 人类-readable success/failure细节
};

// 对`server_name`运行交互式登录(必须作为http/url服务器存在于mcp.json中)。
// 如果 `metadata_url` 不是 -empty，则直接使用它而不是
// 探测 401 质询。 `client_id`（如果设置）在授权服务器
// 不提供动态客户端注册端点时使用：https:// 值被视为 CIMD
// （客户端 ID 元数据文档）URL — 2026-07-28 首选
// 路径 — 其他任何内容都被视为 pre-registered 公共
// client_id。浏览器上的阻止范围从 -trip 到 `timeout_s`。
// 成功时保留令牌。从不抛出。
[[nodiscard]] LoginResult login(Io, const std::string& server_name,
                                const std::string& endpoint_url,
                                const std::string& metadata_url,
                                const std::string& client_id = {},
                                int timeout_s = 300);

// 为 `server_name` 返回新的 "Bearer <token>"
// 头文件，刷新在到期偏差内的令牌。 std::nullopt 如果没有存储
// 令牌（调用者未经身份验证继续进行）或刷新失败且没有可用的 过期 令牌。
// 刷新在 auth_refresh 通道上一次运行一个。
[[nodiscard]] std::optional<std::string> bearer_for(const locks::below_auth_refresh& cap,
                                                    const std::string& server_name);

// 移除存储的令牌。如果文件已删除或不存在，则返回 true。
bool logout(const std::string& server_name);

// 当且仅当当前为服务器存储了令牌时为真。
[[nodiscard]] bool has_token(const std::string& server_name);

// CLI入口点(agentty mcp-login <server>/mcp-logout
// <server>)。他们自己从 .agentty/mcp.json 解析服务器的端点
// URL 并打印 human-readable 进度。返回进程退出代码（0 = 成功）。
int cmd_mcp_login(Io, const std::string& server_name, const std::string& metadata_url = {},
                  const std::string& client_id = {});
int cmd_mcp_logout(const std::string& server_name);
int cmd_mcp_status();   // 列出具有存储的令牌的服务器+

} // namespace agentty::mcp::oauth
