#pragma once
// agentty::ui::login——in-app认证弹出面板的状态机。
//
// 与`runtime/panel/common.hpp`中的其他选择
// 面板 变体形状相同：封闭的和类型，因此每个状态的数据的有效性由
// 类型系统而不是手动-maintained 不变量强制执行。
//
// 五个状态覆盖整个流程：
//
//   关闭——弹出面板未显示。
//   选择 — 选择 OAuth (1) 或粘贴 API 密钥 (2)。
//   OAuthCode——浏览器打开；用户正在粘贴 回调 代码。
//                    携带提交时交换代码所需的
//                    PKCE验证者+状态。
//   OAuthExchanging-提交的代码； HTTP POST 至 /oauth/token 正在飞行。
//   ApiKeyInput —用户正在键入 `sk-ant-...` 键。
//   CustomHostInput —用户正在为 OpenAI
//                    -compatible 后端（llama.cpp、
//                    vLLM、远程盒）键入原始 `host[:port]`。没有授权；
//                    提交将提供者直接切换到该端点。
//   失败——错误提示；按任意键返回拣货。

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "agentty/auth/auth.hpp"

namespace agentty::ui::login {

struct Closed {};

// 其中Esc从子-modal登陆——一个-LEVEL父代，建模为携带其
// OWN上下文的FIRST-CLASS FRAME，而不是有损标签。
//
// 登录流程为用户走INTO的一个栈(提供者选择面板→账户列表→按键输入)；
// Esc 向后退一步 OUT，用其 完整 上下文重建父级。旧的 `Back`
// 枚举 只能命名父级的 KIND ("AccountList") - 它丢失了
// 对应的 提供商的列表，因此来自 Mistral 密钥输入的 Esc 错误地重新
// 打开了 活动 提供商的帐户。通过让每个父节点携带其数据，
// "return to the wrong provider" 变为 UNREPRESENTABLE：
// 打开 Mistral 的键输入的 帧 保存着 `Accounts{"mistral"}`，
// 因此 Esc 可以证明返回到那里。
namespace origin {
    struct Nowhere {};                            // 无父级——关闭弹出面板。
    struct Providers {};                     // 重新打开提供者选择面板。
    struct Models {};                         // 重新打开融合模型选择面板。
    struct Accounts { std::string provider; };     // 重新打开THIS提供者的帐户。
    struct Method   { std::string provider; };     // 签-in方法菜单(Picking)。
    struct HostInput { std::string spec; };        // 恢复带类型的自定义主机。
}
// 自身-contained "where I came from"。每个可输入的 sub-state 存储一个；
// LoginBack std::visits 它 - 没有 side-channel 参数，没有 active-provider 猜测。
using Origin = std::variant<origin::Nowhere, origin::Providers,
                            origin::Models, origin::Accounts,
                            origin::Method, origin::HostInput>;

// 当 Esc 应当读取 "back" (具有父项)而不是 "cancel" 时为真。
[[nodiscard]] inline bool has_parent(const Origin& o) noexcept {
    return !std::holds_alternative<origin::Nowhere>(o);
}

// 可选的提供者上下文将"add another account"流保持在
// 其来自的提供者的范围内。空表示一般first-run/sign-in菜单。
struct Picking {
    std::string provider;
    Origin origin = origin::Nowhere{};
};

struct OAuthCode {
    agentty::auth::PkceVerifier verifier;
    agentty::auth::OAuthState   state;
    std::string              authorize_url;   // 向用户显示为 回退
    std::string              code_input;
    int                      cursor = 0;
};

struct OAuthExchanging {
    // 当该登录来自客户经理的"+ Add another account…"
    // 流(origin::Accounts)、i.e时，确实如此。
    // 用户 DELIBERATELY 想要一个单独的槽，即使派生标签发生冲突。
    // 普通的sign-in会留下这个错误，并在re-auth上重用派生的-label槽
    // （没有"OAuth login 2/3/…"流失）。
    bool as_new_account = false;
};

// 本机 ChatGPT (Codex) 登录正在进行中。本地会话等待端
// 口 1455 浏览器 回调。 SSH 会话使用设备授权，
// 并且一旦 OpenAI 发出它们，就会使用 URL + one-time 代码更新相同的
// 状态。
//
// HOLDING 此 STATE IS 内容 RUNS 该
// WORKER:subscribe()在设置时返回在attempt_id上键入
// 的数据流，因此离开该状态(Esc、成功、更新的尝试)是取消轮询循环的原因。
// 没有 取消 标志 — 有一个，当 工作线程 转移到 stop_token 时，
// 它就死了，这就是 Esc 泄漏 线程 轮询的原因又持续了 900 秒。
struct ChatGptWaiting {
    std::uint64_t                      attempt_id = 0;
    bool                               device_auth = false;
    std::string                        authorize_url;
    std::string                        user_code;
};

// 本机 OAuth **device-flow** 登录正在进行中（GitHub
// Copilot、Kimi 或任何未来的 device-flow 提供商）。
// 提供商始终会发出 one-time 代码（仅限设备流），因此 DeviceCodeReady
// 消息一到达，弹出面板 就会显示验证 URL + 代码。 `provider` 是
// 规范的 注册表 id ("copilot", "kimi") — 它驱动面板标题、
// 完成 分派 和帐户标签。每个 device-flow 提供程序都有一个状态，因此面板、
// key-handling 和复制快捷方式都写入一次。
//
// 与 ChatGptWaiting 一样，保持该状态是运行 工作线程 的原因——
// 参见那里的注释。
struct DeviceWaiting {
    std::string                        provider;      // "copilot" | "kimi" | …
    std::string                        provider_label; // "GitHub Copilot" | "Kimi"
    std::uint64_t                      attempt_id = 0;
    std::string                        authorize_url;  // 面板中显示 BARE url（有一个代码字段）
    std::string                        browser_url;     // pre-filled url auto-opened + 由 `u` 复制
    std::string                        user_code;
};

struct ApiKeyInput {
    std::string key_input;
    int         cursor = 0;
    // 该密钥用于哪个后端。空 = Anthropic（已保存 至
    // credentials.json）。提供商 ID（"openai"、
    // "groq"，...）将提交路由到 Settings.provider_keys
    // + 实时提供商交换机。带有面板 头文件 的人工标签，因此视图不需要 注册表
    // 查找。
    std::string provider;        // 规范id；空=Anthropic
    std::string provider_label;  // 面板标题的显示名称
    // 服务器已经回答了401/403，所以密钥是必需的，不是可选的。
    bool        key_required = false;
    Origin origin = origin::Nowhere{};     // Esc目标（上一级）
};

// 原始OpenAI-compatible端点（"host"或"host:port")
// 的Free-text条目。从提供者 选择面板 的 "Custom host…" 行打开。
// 通过 provider::parse_selection(raw) 提交路由 — 与
// --provider 主机:端口采用的路径相同 — 因此可以从 UI 访问任何
// llama.cpp/vLLM/远程服务器，而无需接触 CLI。
struct CustomHostInput {
    std::string host_input;
    int         cursor = 0;
    Origin origin = origin::Nowhere{};     // Esc目标（上一级）
};

// just-entered自定义主机的异步connect-probe：
// 弹出面板显示"probing host…"，而后台工作线程拨打端点
// 的型号列表(配置的路径→/v1/models→Ollama/api/tags)。
// HostProbed 解决它：成功提交使用 DETECTED
// 方言的切换；失败返回到 CustomHostInput，
// 并恢复 带类型的 规范并显示原因 - 用户从未提交死端点盲注。
// `attempt_id`防止在用户Esc'd或重新提交之后
// 过期探测结果登陆（与OAuth尝试ID相同的模式）。
struct HostProbing {
    std::string   spec;          // 正在探索的规范规范
    std::uint64_t attempt_id = 0;
    Origin origin = origin::Nowhere{};     // failure/Esc返回到的地方
};

struct Failed {
    std::string message;
};

// 账户切换器中的一行。
struct AccountRow {
    std::string provider;    // 规范id
    std::string label;       // 用户-facing姓名
    bool        active = false;
};

// in-app帐户切换器：列出ACTIVE提供商的每个已保存帐户，
// 以便用户可以切换他们登录的身份——或者添加新帐户/删除帐户——
// 而无需离开agentty。选择不是活动行的行会将该帐户的 凭据
// 交换到实时存储中；最后一行始终是 "+ Add another account…"。
// ChatGPT直接启动其原生OAuth流程；
// Anthropic 的范围仅限于 API-key/OAuth 选择。
struct AccountList {
    std::string             provider;       // 这些行所属的提供者
    std::string             provider_label; // 显示名称为头文件
    std::vector<AccountRow> rows;           // 已保存账户(+合成添加行是索引==rows.size())
    int                     cursor = 0;      // 0..rows.size()（最后=add-new）
    std::string             confirm_remove; // 等待第二次按下Del/d的标签
};

using State = std::variant<Closed, Picking, OAuthCode, OAuthExchanging,
                           ChatGptWaiting, DeviceWaiting, ApiKeyInput, CustomHostInput,
                           HostProbing, AccountList, Failed>;

[[nodiscard]] inline bool is_open(const State& s) noexcept {
    return !std::holds_alternative<Closed>(s);
}

[[nodiscard]] inline bool is_input_state(const State& s) noexcept {
    // 消耗 free-text 键输入的状态（相对于挑选选择键）。
    return std::holds_alternative<OAuthCode>(s)
        || std::holds_alternative<ApiKeyInput>(s)
        || std::holds_alternative<CustomHostInput>(s);
}

} // namespace agentty::ui::login
