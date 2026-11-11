#pragma once
// agentty::tools::web_search_secret-web_search服务的API密钥所在的位置。
//
// 不在 settings.json 中，因为 embed_secret.hpp
// 给出的原因是：明文配置文件中的 凭据 是等待 screen-share 或点
// 文件提交的泄漏。与 agentty 的 凭据 处理的其余部分相同的两层：
//   1. OS密钥库，当可用并启用时（AGENTTY_USE_KEYSTORE)；
//   2.另有密封文件(auth::crypt::seal，机器-bound，AEAD)。
//
// 为什么不提供提供者-key保险库（auth::keys)：持久化在每
// 个设置保存上从Model.provider_keys批量重写该保险库，
// 这将擦除提供者密钥旁边存储的任何内容，并且其中的每个非
// -preset条目都显示为自定义提供者主机。这些密钥不是提供者。
//
// 由 SERVICE id ("exa-api") 键入，来自 mcp-cpp 的目录 id。
// 免费服务没有任何可存储的内容，并且永远不会传递到这里。
//
// 环境变量胜过存储，因此密钥可以来自 CI 或
// 秘密管理器，而无需接触磁盘：
//     AGENTTY_WEB_SEARCH_KEY_<ID>与'-'变成'_'，大写e.g。
//     AGENTTY_WEB_SEARCH_KEY_EXA_API、AGENTTY_WEB_SEARCH_KEY_BRAVE_API

#include <string>

#include "agentty/util/io.hpp"
#include "agentty/util/lock_levels.hpp"
#include <string_view>

namespace agentty::tools::web_search_secret {

// 将覆盖`service`的密钥的变量。
[[nodiscard]] std::string env_name(std::string_view service);

// 使用的密钥(""，当没有时)。首先是环境，然后是密钥库，
// 然后是密封文件。从不抛出。
[[nodiscard]] std::string load(std::string_view service);

// 当 load() 将返回某些内容时为真。
[[nodiscard]] bool has(std::string_view service);

// "env: AGENTTY_WEB_SEARCH_KEY_X"、"stored"或""-
// 用于面板的出处列。永远不会归还钥匙。
[[nodiscard]] std::string origin(std::string_view service);

// 我们愿意在请求头文件中放入的密钥：8..512 字节，没有空格，
// 也没有控制字符。在存储之前进行检查，以便在 面板 处捕获粘贴的换行符
// 或 "Bearer " 前缀，而不是稍后捕获不透明的 401。
[[nodiscard]] bool plausible(std::string_view key);

// 坚持。空键会擦除。如果无法保护秘密，则返回 false —
// 调用者必须将其视为 "not saved"，切勿写入明文。
// 当密封文件存在但无法打开（损坏或在另一台机器上
// 密封）时也为假：重写它会丢失所有其他密钥。
bool store(Io, const lock_levels::below_file_lane&, std::string_view service,
           std::string_view key);

// 从密钥库（当启用时）和密封文件中移除。
// 在 AGENTTY_USE_KEYSTORE 开启时存储并在关闭
// 时擦除的密钥保留在 OS 密钥库中：仅在设置变量时才查阅该存储，
// 否则我们无法扫描该存储。
bool erase(Io, const lock_levels::below_file_lane&, std::string_view service);

} // namespace agentty::tools::web_search_secret
