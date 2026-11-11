#pragma once
// agentty::auth::vault —每个 凭据 存储的 一个 描述符，
// 回答 每个 授权问题 agentty 询问的提供商。
//
// multi-provider认证现实：N个提供者同时登录，
// 每个提供者都有自己的存储形状，活动选择只是一个指针。
// 在此 头文件 之前，该现实被实现为五个独立的 per-provider
// if-ladders 分布在四个文件中：
//
//   account_switch.cpp backend_for() — 该斑点位于哪里？
//   login.cpp sign_out() — 如何清除它？
//   login.cpp account_provider_id() — 注册表 ID 是什么？
//   login.cpp signout_provider_id() — (^ 的 字节-level 克隆)
//   auth_state.cpp provider_is_authed() — 是 已保存 吗？
//   credentials.cpp resolve() —头文件 装在 传输 上是什么？
//
// 每个新的提供者（并且两次提供者都是RENAMED）意味着
// 找到所有五个梯子；缺少一个正好产生了我们一直在发布修复程序
// 的错误类别（sign-out 清除了错误的商店，
// ^D 留下了实时的 头文件 武装，帐户钻入了错误的提供商）。
//
// 现在：`vault::of(provider_id)`返回ONE
// 描述符——函数指针+数据的小值类型，像提供者注册表行的表
// -driven——并且五个调用站点变成一个-liners。
// 添加提供商的身份验证故事 = 添加 一个 VaultDesc 行。
// 编译器 不允许一行是部分的（使用指定的初始化器聚合 init，
// 每个问题都已回答或明确默认）。
//
// 设计注意事项（这里的"modern C++"是克制，不是聪明）：
//   ·普通函数指针的平面constexpr-friendly表——无
//     虚拟层次结构，没有std::function 分配，
//     在静态-init时间没有注册副作用。接口描述符 IS。
//   ·身份是agentty的提供者ID（capkey RULE 2的命名空间）；
//     自定义主机——其ID是它们的端点规范——落入合成的
//     SettingsKey描述符，因此`of()`是总的：每个字符
//     串产生一个可用的库。
//   ·问题与POLICY分开：保险库知道如何清除
//     商店；然后，sign-out 是否应该回退到另一个提供商
//     或打开登录 弹出面板 保留在策略所属的 状态更新函数 中。

#include <optional>
#include <string>
#include <string_view>

#include "agentty/auth/auth.hpp"

namespace agentty::auth::vault {

// 如何存储提供商的ACTIVE 凭据。
enum class Kind : std::uint8_t {
    AnthropicFile,   // credentials.json (OAuth 或 x-api-key)，密封
    OAuthFile,       // 提供商-owned令牌文件(ChatGPT/Copilot/Kimi)
    SettingsKey,     // Settings.provider_keys[id] — 托管密钥+自定义主机
    None,            // local/no-auth后端(Ollama)：没有可存储的内容
};

struct Desc {
    std::string_view id;      // agentty 提供商 ID（"" ⇒ 合成）
    Kind             kind = Kind::SettingsKey;

    // 这五个问题，统一回答。函数指针（不是
    // std::function）：无状态，没有 分配，可简单复制。
    // `provider`被传回，以便SettingsKey描述符（
    // 由每个托管-key提供者AND每个定制主机共享）知道哪个槽。
    // is_signed_in是锁-free（浏览次数到达它）。
    // clear可能会重写settings.json（一个阻塞设置保存），所以需要`unlocked`；
    // after_activate 只取叶子 凭据-store 锁。
    bool (*is_signed_in)(Io, const std::string& provider);
    void (*clear)(Io, const maya::unlocked&, const std::string& provider);  // 退出
    auth::AuthHeader (*resolve)(Io, const std::string& provider);
    // 当前 凭据（帐户切换器行）的显示标签。
    std::string (*current_label)(Io, const std::string& provider);
    // Post-activate钩子(密钥库re-seal/token-cache半身像)。可能为空。
    void (*after_activate)(Io, const maya::below<maya::leaf>&, const std::string& provider);
};

// 总查找：注册表提供者获得他们的行；其他都是自定义主机并
// 获取合成的 SettingsKey 描述符。永远不会失败。
[[nodiscard]] const Desc& of(std::string_view provider_id);

// 便利传递——调用-site词汇。
[[nodiscard]] inline bool signed_in(Io io, const std::string& p) {
    return of(p).is_signed_in(io, p);
}
inline void sign_out(Io io, const maya::unlocked& cap, const std::string& p) {
    of(p).clear(io, cap, p);
}
[[nodiscard]] inline auth::AuthHeader resolve(Io io, const std::string& p) {
    return of(p).resolve(io, p);
}

} // namespace agentty::auth::vault
