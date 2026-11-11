#pragma once
// agentty::auth —凭据 域。 `Credentials` 是 和
// 类型（无 | ApiKey | OAuth）；每个替代方案都拥有仅在该状态下
// 有意义的数据。 Token-flow 操作返回
// `std::expected<OAuthToken, OAuthError>` — 无并行
// `bool ok` 标志，无字符串错误字段。
//
// 磁盘中的 加载 凭据 的适配器位于 `src/io/auth.cpp` 中，
// 并且提供者 -specific OAuth 连线（client_id、
// 令牌 URL、PKCE 交换）位于
// `agentty/provider/anthropic/oauth.hpp` 中。
//
// `resolve()`是一个编排点——它选择最好的
// -available 凭据 (CLI密钥> env > OAuth
// -from-disk)并就地刷新过期的OAuth令牌。
// 它必须位于可以将适配器层组合在一起的地方，因此它在这里声明并在
// `src/io/auth.cpp` 中定义。

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "agentty/domain/id.hpp"
#include "agentty/util/io.hpp"
#include "agentty/util/lock_levels.hpp"

#include <maya/runtime.hpp>

namespace agentty::auth {

// 传输-format 授权 头文件 样式。 OAuth 的承载者，API 的原始密钥。
// 由呼叫者(e.g.`provider::anthropic::transport`)挑选以
// 挑选正确的头文件名称(`x-api-key`与`authorization`)。
enum class Style : std::uint8_t { ApiKey, Bearer };

// 原始秘密字符串上的强新类型在 OAuth 舞蹈中穿梭。
// 所有四个都是 "some opaque hex" 和 运行时，
// 函数签名 `exchange_code(code, verifier, state)`
// 正是调用者可以交换两个参数和用于传递它的 编译器 的地方。
struct OAuthCodeTag    {};
struct PkceVerifierTag {};
struct OAuthStateTag   {};
struct RefreshTokenTag {};
using OAuthCode    = Id<OAuthCodeTag>;
using PkceVerifier = Id<PkceVerifierTag>;
using OAuthState   = Id<OAuthStateTag>;
using RefreshToken = Id<RefreshTokenTag>;

// ── 凭据: 和类型 ,没有 枚举-with-fields ──────────────────────────
// 每个备选方案恰好拥有在该状态下有效的数据。
// 读取 凭据 的代码使用 `std::visit` （或下面
// 的帮助程序），而不是打开方法 枚举 并记住填充了哪些字段。
namespace cred {
struct None {};
struct ApiKey {
    std::string key;
};
struct OAuth {
    std::string access_token;
    std::string refresh_token;
    // Unix纪元毫秒。 0表示"no expiration info"；
    // 当 上游 不包含 `expires_in` 时，刷新将被跳过。
    std::int64_t expires_at_ms = 0;
};
} // namespace cred

using Credentials = std::variant<cred::None, cred::ApiKey, cred::OAuth>;

// ── AuthHeader (typed wire credential) ─────────────────────────────
// Anthropic接受的两个头文件形状的闭合总和。 变体 臂
// IS 头文件 NAME — 没有代码路径接受承载令牌并在
// `x-api-key:` 下发出它（反之亦然），因为 头文件
// 名称是由臂上的 std::visit 选择的，而不是由可能
// 与该值不一致的旁路 `Style` 枚举 选择的。
//
// 通过`make_auth_header(creds)`构建；
// 切勿用松散的绳子手工组装。 传输层 的请求类型直接保存此值，
// 而不是历史的 `(string auth_header, Style auth_style)` 对，
// 并且提供者抽象 线程 保持不变。
struct ApiKeyHeader {
    // 原始`sk-ant-…`密钥。熄灭为 `x-api-key: <value>`。
    std::string value;
};
struct BearerHeader {
    // 仅令牌-"Bearer "在发射时被预先考虑。
    // 熄灭为 `Authorization: Bearer <token>`。
    std::string token;
};
using AuthHeader = std::variant<ApiKeyHeader, BearerHeader>;

// 将 凭据 翻译为其 传输-typed 头文件。
// 单点翻译 — 代码库 中的任何其他站点都不应该为自己选择（
// header_name，值）。
//
// `cred::None`映射到空的`ApiKeyHeader{""}`；
// 传输层 呼叫者在拨号前会拒绝并显示 "not authenticated" 错误。
[[nodiscard]] AuthHeader make_auth_header(const Credentials& c);

// 当 变体 没有携带可用的秘密时，情况如此——任一臂都带有空的 载荷。
// 传输层 使用此代替历史 `auth_header.empty()`
// 检查。
[[nodiscard]] bool is_empty(const AuthHeader& h) noexcept;

// EITHER 臂携带的原始秘密字符串，作为普通令牌 — 整个 OpenAI 系列 (
// OpenAI/Groq/OpenRouter，以及通过代理访问的 Ollama。
// 在 `Authorization: Bearer <token>` 下发出的值。
// 提取出来原来是每OpenAI-family 传输层的build_request_headers
// 里有一份-pasted `std::visit`；这些副本可能会在新手臂上漂移。
// 这是唯一的事实来源。 NOTE：Anthropic 故意让 不 使用此功能 - 它必须将
// ApiKeyHeader 路由到 `x-api-key:`（从不承载），因此它保留自己的
// arm-typed 访问。
[[nodiscard]] std::string bearer_token(const AuthHeader& h);

// 免费帮手——变体是真理，这些都是派生的观点。
[[nodiscard]] bool        is_valid(const Credentials& c) noexcept;
[[nodiscard]] bool        is_expired(const Credentials& c) noexcept;
[[nodiscard]] std::string header_value(const Credentials& c);
[[nodiscard]] Style       style(const Credentials& c) noexcept;

// 磁盘 序列化 标签。 "oauth" / "api_key" / "none" — 由
// load_credentials/save_credentials 和 `cmd_status` 使用。
[[nodiscard]] std::string_view persist_tag(const Credentials& c) noexcept;

// ── 路径 ────────────────────────────────────────────────────────────────
[[nodiscard]] std::filesystem::path config_dir();              // ~/.agentty/credentials (见 util/user_root.hpp)
[[nodiscard]] std::filesystem::path credentials_path();

// ── 预温 ──────────────────────────────────────────────────────────────
// 在分离后台线程上打开到api.anthropic.com的TCP+
// TLS+h2连接，将结果停放在http客户端的池中。当用户点击
// Enter 时，第一个实际请求会重用该连接并跳过大约 150-
// 300 毫秒的冷握手。 幂等（第一个 调用 之后没有 -op）。
void prewarm_anthropic();

// ── 磁盘I/O ─────────────────────────────────────────────────────────────
[[nodiscard]] std::optional<Credentials> load_credentials(Io);
bool save_credentials(Io, const Credentials& c);     // 在支持的情况下以 0600 权限写入
bool clear_credentials(Io);

// 便宜的缓存谓词：磁盘上是否有 Anthropic 凭据？每次渲染的 帧（e.g。
// 提供者 选择面板）对 调用 都是安全的 - 凭据 文件（mtime、
// 大小）上的 缓存 和只有 re-reads/decrypts 发生变化时。
// 在热 渲染 路径中，优先使用此方法而不是 load_credentials()。
[[nodiscard]] bool anthropic_signed_in(Io);

// ── PKCE 助手(测试时接触) ─────────────────────────────────────
[[nodiscard]] std::string random_urlsafe(Io, std::size_t n);
[[nodiscard]] std::string base64url_no_pad(const unsigned char* data, std::size_t len);
[[nodiscard]] std::string sha256_hex(const std::string& s);
[[nodiscard]] std::string code_challenge_s256(const std::string& verifier);

// ── 调试操作 ─────────────────────────────────────────────────────
struct OAuthToken {
    std::string  access_token;
    std::string  refresh_token;
    std::int64_t expires_in_s = 0;   // 服务器-reported 生命周期，0 = 无信息
};

enum class OAuthErrorKind : std::uint8_t {
    Network,        // HTTP 传输层 失败 — 服务器无响应
    BadResponse,    // 服务器已回复，但 正文 无效 JSON
    ApiError,       // 服务器返回 OAuth error_description
    MissingToken,   // 200 OK 但没有 access_token 字段
    Superseded,     // 刷新成功，但凭据存储被交换
                    // mid-exchange (账户切换）；结果被丢弃。 BENIGN：
                    // 切换的-to账户的令牌已经生效——不要
                    // 显示错误，不要重试。
};

struct OAuthError {
    OAuthErrorKind kind = OAuthErrorKind::ApiError;
    std::string    detail;
    [[nodiscard]] std::string render() const;
};

using TokenResult = std::expected<OAuthToken, OAuthError>;

[[nodiscard]] TokenResult exchange_code(const OAuthCode& code,
                                        const PkceVerifier& verifier,
                                        const OAuthState& state);
[[nodiscard]] TokenResult refresh_access_token(Io, const RefreshToken& refresh_token);

// Cross-process-serialized刷新（惊雷-herd安全）。
// 在 credentials.json 上用咨询 锁 包裹
// refresh_access_token + double-checked
// re-read：如果 对端 agentty 实例在我们等待 锁 时已经刷新，
// 则会返回 该 令牌作为成功，而不是触发冗余刷新（因为 Anthropic 会
// 轮换刷新令牌，因此会相互使实例无效并旋转multi-instance 刷新循环）。
// 在成功铸造时，它还将新的信用保留到磁盘。从任何可以跨进程并发运行的
// 刷新路径使用此功能（启动刷新、401 恢复）； 状态更新函数 的
// TokenRefreshed 处理函数 仍然将结果安装到 Deps 中。
// 在获取 锁 后，最新的 on-disk 刷新令牌获胜，
// 因此来自 对端 的轮换令牌永远不会是 re-used 过期。
[[nodiscard]] TokenResult refresh_access_token_locked(Io, const locks::below_cred_file&,
                                                      const RefreshToken& refresh_token);

// 构建claude.ai，授权URL用户必须访问授权，agentty访问。
// 纯：相同的输入，相同的 URL — 无 副作用，从 状态更新函数 到 调
// 用 是安全的。 in-app 登录 弹出面板 调用 一次，
// 当它转换 Picking → OAuthCode 时，URL 既可以显示给
// 用户（如果浏览器启动失败，则为 回退），并通过 Cmd::task
// 交给 `open_browser`。
[[nodiscard]] std::string oauth_authorize_url(const PkceVerifier& verifier,
                                              const OAuthState&   state);

// Fire-and-forget:打开用户的默认浏览器，指向`url`。
// 根据平台使用`xdg-open`/`open`/`ShellExecute`。
// 总是立即返回——系统命令以 `&` 为后台，因此从 UI
// 线程 到 调用 是安全的，但包裹在 Cmd::task
// 中也很好（并且更干净），因此楔形开启器不会阻止
// 状态更新函数 滴答声。
void open_browser(const std::string& url);

// 按照记录的优先顺序解决凭据。
// 如果非-empty，则`cli_api_key` (来自`-k`)具有最高优先级。
//
// Non-blocking:具有refresh_token的过期
// OAuth令牌被返回为-is（仍然过期）。刷新令牌通过
// `set_pending_refresh()` 隐藏，
// 因此一旦 maya 运行时 启动，in-app 状态更新函数
// 就可以启动后台刷新 — TUI 立即启动，而不是等待同步网络往返。
[[nodiscard]] Credentials resolve(Io, const std::string& cli_api_key);

// ── 同步新鲜 凭据 (用于 -reducer 工作线程 线程) ────────
// 返回ready-to-use 传输 头文件，通过阻塞网络往返
// 刷新过期的OAuth令牌IN PLACE，并将新令牌保存到磁盘，
// 以便整个过程拾取它。与 `resolve()`（非
// -blocking 并将刷新推迟到 状态更新函数）不同，
// 这是针对在 工作线程 线程 上运行的上下文，并且没有 状态更新函数
// 将刷新交给 — 主要是 `task` 子代理循环，
// 否则它将重用启动时捕获的冻结 OAuth 访问令牌以及过期后的 401。
//
// 当没有 saved/env 凭据 可用时，`fallback` 不变地返回（e.g。
// CLI -k 密钥是唯一的身份验证），因此已经持有已知 -good 头
// 文件 的呼叫者不会回归。从任何 工作线程 到 线程-safe 到 调用。
// 刷新在 auth_refresh 通道上一次运行一个。
[[nodiscard]] AuthHeader fresh_auth_header(Io, const locks::below_auth_refresh&,
                                           const AuthHeader& fallback);

// ── 待处理 OAuth 刷新交接方式 ────────────────────────────────────────
// One-shot通道从`auth::resolve()`(从main()调用
// pre-TUI)到`AgenttyApp::init()`(从maya::run()
// 内部调用)。如果需要后台刷新，resolve() 将刷新令牌写入此处；
// init() 将其构造为异步执行刷新的 `Cmd::task(...)`，
// 并在完成后分派 `TokenRefreshed` Msg。
//
// 两个函数都运行在主线程上(顺序为：主→maya::run→P::init)，
// 因此不需要原子或锁； std::optional 只是一个
// -producer / 一个 -consumer 邮箱。
void set_pending_refresh(std::string refresh_token);
[[nodiscard]] std::optional<std::string> take_pending_refresh();

// 主动-refresh探针(非-consuming)。读取 on-disk 凭据，
// 如果它是过期时间在 `window_ms`（默认 5 分钟）内的 OAuth 令牌
// — i.e。即将失效或已经失效 - 和 它携带 refresh_token，
// 返回该令牌，以便调用者可以启动后台刷新 之前，下一个请求将是 401。对于 API
// -key / env / None 信用，对于没有到期信息的 OAuth，
// 对于仍然新鲜的令牌，以及对于没有 refresh_token 的令牌（没有任何刷新），
// 返回 nullopt。与 take_pending_refresh 不同，
// 这不会触及 one-shot 邮箱 — 呼叫者可以轮询它，并且必须门控自己的
// in-flight/throttle 状态（m.s.oauth_refresh_in_flight
// 上的 状态更新函数 键）。
[[nodiscard]] std::optional<std::string>
oauth_proactive_refresh_token(Io, std::int64_t window_ms = 5 * 60 * 1000);

// 任何共享文件上的 Cross-process 加锁 经过
// persistence::with_shared_file (io/shared_file.hpp)。

// ── 交互式 CLI 流动(阻塞, stdout/stdin — 不 输入 TUI) ──────────
int cmd_login(Io, const maya::unlocked&);
int cmd_logout(Io, const maya::unlocked&);
int cmd_status(Io, const maya::unlocked&);

} // namespace agentty::auth
