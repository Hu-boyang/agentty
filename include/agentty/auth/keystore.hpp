#pragma once
// agentty::auth::keystore —可选的OS-native秘密存储。
//
// SECURITY_AUDIT long-term建议：使用操作
// 系统自己的保管库备份凭据文件，以便令牌永远不会以任何相同的
// -user进程可以解密的形式位于磁盘上。 cred_crypt.
// * 中的 -rest 机器 -bound（和可选密码）加密保持深度防御；
// 启用密钥库后，它会保存密封的信封，并且 on-disk 文件仅变为
// cache/fallback。
//
// 后端（在 运行时 选择，没有新的 链接-time 依赖项）：
//   Linux-libsecret，通过 `secret-tool` CLI (GNOME
//             密钥环/KWallet，通过特勤局 D-Bus API)。探测是否存在。
//   macOS — `security` CLI（登录钥匙串、通用密码项）。
//   Windows——尚未实施； retrieve/store
//             报告不受支持，并且调用者透明地回退到加密文件。
//
// Opt-in:仅当AGENTTY_USE_KEYSTORE为真时才进行咨询。
// 默认情况下处于关闭状态，因此现有安装不会发生变化，并且不会为不需要的用户显示
// keychain-unlock 提示词。

#include <optional>
#include <string>

namespace agentty::auth::keystore {

// 密钥库操作的结果。 `Unsupported` 表示此
// platform/build 上没有可用的后端（调用者应回退到文件）；
// `NotFound` 表示后端正常工作，但没有保存 `key` 下的项目；
// `Error` 表示后端存在但操作失败。
enum class Status { Ok, NotFound, Unsupported, Error };

// 正确，当且仅当密钥库后端在通过 AGENTTY_USE_KEYSTORE 启用的
// 该系统 和 上可用 两者 时。便宜（探测一次，缓存）。呼叫者在此门控他们的
// store/retrieve。
bool available();

// 活动后端的人类标签("libsecret"、"macos-keychain"、"disabled"、
// "unavailable")-用于`agentty status`。
std::string backend_name();

// `key`下的Store `secret`（创建或替换）。成功则返回 Ok。
Status store(const std::string& key, const std::string& secret);

// 检索存储在`key`下的秘密。正常时，`out` 保留该值。
Status retrieve(const std::string& key, std::string& out);

// 删除存储在`key`下的任何秘密。好吧，即使什么都没有也可以接受；
// NotFound 也适合呼叫者执行 best-effort 擦除。
Status remove(const std::string& key);

} // namespace agentty::auth::keystore
