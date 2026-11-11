#pragma once
// agentty::rag::embed-嵌入API密钥的存储。
//
// 故意与 embed_backend.hpp 分开：头
// 文件 是纯配置（可从测试、选择面板 和 传输 路径中使用，
// 没有 IO），而这个涉及密钥库和文件系统。
// 保持分割意味着可以在不拖入 凭据 IO 的情况下推理配置层。
//
// 该密钥由ENDPOINT而不是全局加密，因此将两个项目指向两个不同的网
// 关不能将一个项目的凭据泄漏到另一个请求中。它永远不会写入
// settings.json — 请参阅 embed_secret.cpp。

#include <string>

#include "agentty/rag/embed_backend.hpp"
#include "agentty/util/io.hpp"
#include "agentty/util/lock_levels.hpp"

namespace agentty::rag::embed {

// 该配置的端点的存储密钥，或者当后端不采用凭据时的""(在-process型号、
// Ollama、llama.cpp中)。
[[nodiscard]] std::string endpoint_key(const EmbedConfig& c);

// 获取端点的存储密钥（当没有时为""）。从不抛出。
[[nodiscard]] std::string load_key(const std::string& endpoint);

// 保留（或者，使用空键，擦除）。如果无法保护秘密，
// 则返回 false — 调用者必须将其视为 "not saved"，
// 而不是默默地写入明文。
bool store_key(Io, const lock_levels::below_file_lane&, const std::string& endpoint,
               const std::string& key);

bool erase_key(Io, const lock_levels::below_file_lane&, const std::string& endpoint);

} // namespace agentty::rag::embed
