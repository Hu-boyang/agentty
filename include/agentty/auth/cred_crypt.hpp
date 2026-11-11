#pragma once
// agentty::auth::crypt-凭据文件的at-rest加密。
//
// SECURITY_AUDIT.md发现#1(MEDIUM):
// OAuth/API令牌作为明文JSON存储在~/.config/agentty/credentials.json
// (0600)中。备份、同步的点文件 仓库、文件的肩部 -surf
// 或任何可以读取用户主目录的进程，然后带着实时令牌离开。
//
// 该模块将JSON 载荷包装在经过验证的-encryption信封中，
// 因此on-disk 字节是不透明的。它是 DELIBERATELY
// 不能替代 OS 密钥库（libsecret/Keychain/DPAPI
// - 审计的 long-term 建议）：密钥是来自机器
// -stable 材料的 派生，未存储在 hardware/OS
// 保管库中，因此也可以在同一台计算机上以同一用户身份运行代码
// 的坚定攻击者可以重新-derive 它。 执行 在零新依赖性和零
// UX 成本的情况下防御的是更常见的暴露：离开机器的文件（备份、
// cloud-sync、屏幕截图、`cat`）仍然可读。
//
// 信封 (JSON):
//   { "v":1、"enc":"aes-256-gcm"、
//     "salt":<b64>、"nonce":<b64>、"ct":<b64>、"tag":<b64> }
//
// 加密：AES-256-GCM (OpenSSL EVP —已链接)。 256-bit 密钥是
// HKDF-SHA256(machine_seed, salt)，其中 machine_seed
// 混合了 per-machine id（/etc/machine-id，主机名，...）、
// 数字用户 id 和固定的 app-specific 上下文字符串。每个 保存 都会生成一个
// 新的随机 128-bit salt + 96-bit nonce，因此 re-encrypting
// 相同的明文永远不会产生相同的密文。
//
// ── 选择密码句模式( SECURITY_AUDIT 硬化) ──────────────────
// 上面的machine-only密钥确实NOT能够防止能够以SAME用户身份运行代码的本地攻击者
// （他们re-derive相同的种子）。设置一个密码来弥补
// 这个差距：密钥也包含在 内存-hard scrypt(passphrase,
// salt) 因子中的 归并，因此如果没有 两者 机器
// 和 用户在头脑中保存的秘密，则无法解密该文件。
//
// 通过以下任一方式启用：
//   ·导出AGENTTY_PASSPHRASE=…(非-interactive/CI)，或者
//   • 导出 AGENTTY_ENCRYPT_PASSPHRASE=1 在 /dev/tty. 上提示一次
// 当两者均未设置时，行为正是机器-only路径(v1)
// 并且现有文件继续工作。密码信封是 v2 并携带
// scrypt 参数，因此解封可以重现推导。

#include <optional>
#include <string>

namespace agentty::auth::crypt {

// 真，当且仅当已为此进程配置了密码（环境变量设置，
// 或已回答交互式 提示词）。便宜的;由想要告诉用户他们的保管
// 库密码为 -protected 的调用者使用。
bool passphrase_active();

// 将`plaintext`加密到上述JSON信封中。
// 仅在 OpenSSL 硬故障（出于 内存 / 损坏的构建）时返回
// std::nullopt - 调用者将其视为
// "couldn't secure the file" 并回退到拒绝持久而不是写入纯文本。
std::optional<std::string> seal(const std::string& plaintext);

// 解密由seal()产生的JSON信封。如果输入不是 v1 信封（e.g。
// 传统明文 credentials.json - 调用者然后尝试直接对其进行
// 解析 并在下一个 保存 上重新进行 -encrypts），
// 或者如果身份验证失败（被篡改/错误的机器/损坏），则返回 std::nullopt。
std::optional<std::string> unseal(const std::string& envelope);

// 正确当且仅当`s`看起来像密封信封（以"v"/"enc"
// 标记开始JSON对象）。用于在加密路径和旧版
// -plaintext迁移路径之间分支加载的廉价结构探针。
bool looks_sealed(const std::string& s) noexcept;

} // namespace agentty::auth::crypt
