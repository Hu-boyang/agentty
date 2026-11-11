#pragma once
// agentty::auth::keys-用于提供者API密钥的密封的-rest保险库。
//
// SECURITY_AUDIT (凭据同质化)：hosted-preset键("openai"、
// "groq"、…)和 custom-host键(主机[:端口]/URL规格——包括
// 无键本地主机行，其被持久化为空值，因此选择面板在下一个会话中具有行)，用于在下一个
// 会话中休息PLAINTEXT settings.json在"provider_keys"下，
// 而credentials.json和accounts.json则用crypt::
// seal密封。备份、同步点文件 仓库、屏幕 -share 或
// settings.json 的 bug-report 附件随每个实时密钥一起消失。
//
// 现在每个提供商密钥都持续存在THIS模块进入<credentials>
// /provider-keys.json—同一台机器-bound AES-256
// -GCM信封credentials.json用途，镜像到OS密钥库时
// AGENTTY_USE_KEYSTORE已启用（相同的密钥库-first+ 文件
// -fallback模式为auth::save/load_credentials).
// settings.json完全停止携带钥匙；首次加载遗留明文"provider_keys"
// 对象被导入到 Vault 中并从中剥离settings.json（看
// persistence::load_settings为了一个-time迁移）。
//
// RUNTIME故事不变：store::Settings.provider_keys
// 保留in-memory真相来源(选择、选择面板行、resolve()都读取它)；
// 只有 at-rest 层移动。空地图 已保存 是权威的（标牌 -out
// 必须能够清除金库），因此 save() 总是写入 — 不存在
// "skip empty" 短 -circuit。

#include <map>
#include <string>

namespace agentty::auth::keys {

// 提供商ID（或custom-host规范）→密钥。与store::
// Settings::provider_keys形状相同，所以两者直接转换。
using KeyMap = std::map<std::string, std::string>;

// On-disk位置：<credentials>/provider-keys.json，
// 旁边的credentials.json/accounts.json存储它现在匹配。
[[nodiscard]] std::string path();

// 读取保管库：首先读取密钥库（当启用时），密封文件为回退。
// 当没有存储任何内容时，或者当信封无法被认证时（错误的机器/损坏），
// 返回{}，镜像unseal()的fail
// -closed合约：没有密钥，从不猜测。
[[nodiscard]] KeyMap load();

// 密封`keys`并持久化(密钥库镜像+原子0600文件写入)。
// 当密封或写入失败时返回假——调用者应该浮出水面，
// 而不是默默地退回到明文。
bool save(const KeyMap& keys);

// Best-effort擦除：密钥库项+密封文件。
void clear();

} // namespace agentty::auth::keys
