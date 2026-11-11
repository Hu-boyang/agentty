// embed_secret.cpp-嵌入API密钥所在的位置。
//
// 不在settings.json中。纯文本配置文件中的 凭据
// 是等待 screen-share、点文件提交或 bug
// -report 附件的泄漏，而此文件是由明确在锁定的 -down
// 环境中工作的用户粘贴的 - 正是那些最负担不起的人群。
//
// 两层，与agentty的凭据处理的其余部分相同的形状：
//   1.OS密钥库(libsecret/macOS钥匙串)可用时，并且
//      启用，因此秘密永远不会触及agentty自己的文件；
//   2.否则配置目录下的密封文件(auth::crypt::seal-
//      machine-bound AEAD)，这是credentials.json已经使用的。
//
// 由ENDPOINT而不是"the embeddings key"
// 加密：用户可以将不同的项目指向不同的网关，并且验证一个项目的密钥
// 不得泄漏到对另一个的请求中。

#include "agentty/rag/embed_secret.hpp"

#include "agentty/auth/auth.hpp"
#include "agentty/auth/cred_crypt.hpp"
#include "agentty/auth/keystore.hpp"
#include "agentty/io/shared_file.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>

namespace agentty::rag::embed {

namespace {

namespace fs = std::filesystem;

// 每个端点的密钥有一个文件，作为一个单元密封。
[[nodiscard]] fs::path secrets_path() {
    return auth::config_dir() / "embed_keys.json";
}

[[nodiscard]] std::string keystore_key(const std::string& endpoint) {
    return "agentty:embed:" + endpoint;
}

[[nodiscard]] nlohmann::json parse_all(const std::optional<std::string>& file) {
    if (!file || file->empty()) return nlohmann::json::object();
    const std::string& raw = *file;
    // 密封信封，或(legacy/hand-written)明文JSON。
    if (auto opened = auth::crypt::unseal(raw)) {
        try { return nlohmann::json::parse(*opened); } catch (...) { }
        return nlohmann::json::object();
    }
    try {
        auto j = nlohmann::json::parse(raw);
        if (j.is_object()) return j;
    } catch (...) { }
    return nlohmann::json::object();
}

[[nodiscard]] nlohmann::json read_all() { return parse_all(persistence::peek_file(secrets_path())); }

// 设置（或使用空密钥、删除）一个端点的密钥：整个文件在其共享
// -file部分中被读取、更改和重写，因此保存不同端点的两个
// agentty窗口都保留它们的。密封，0600，原子；
// 无法密封的秘密会被拒绝，而不是明文地写出来。当文件现在包含所
// 要求的内容时为真（删除不存在的密钥是 no-op 成功）。
bool update_key(Io io, const lock_levels::below_file_lane& cap, const std::string& endpoint,
                const std::string& key) {
    // 密封在该部分内（正文取决于现在磁盘上的内容）。
    // 不可密封的结果不会写入任何内容：秘密永远不会以明文形式存储。
    const auto r = persistence::update_shared(io, cap, secrets_path(),
        {.durability = persistence::Durability::Durable, .mode = 0600},
        [](std::optional<std::string> cur, std::string ep, std::string k)
            -> std::optional<std::string> {
            auto all = parse_all(cur);
            if (k.empty()) { if (all.erase(ep) == 0) return std::nullopt; }
            else all[ep] = k;
            return auth::crypt::seal(all.dump());
        }, endpoint, key);
    if (r == persistence::Update::Written) return true;
    // 不变是仅在丢弃不存在的密钥时成功；对于一套，
    // 这意味着密封失败。
    return r == persistence::Update::Unchanged && key.empty();
}

} // 命名空间结束。

std::string load_key(const std::string& endpoint) {
    if (endpoint.empty()) return {};
    if (auth::keystore::available()) {
        std::string out;
        if (auth::keystore::retrieve(keystore_key(endpoint), out) == auth::keystore::Status::Ok)
            return out;
    }
    const auto all = read_all();
    if (auto it = all.find(endpoint); it != all.end() && it->is_string())
        return it->get<std::string>();
    return {};
}

bool store_key(Io io, const lock_levels::below_file_lane& cap, const std::string& endpoint,
               const std::string& key) {
    if (endpoint.empty()) return false;
    if (key.empty()) return erase_key(io, cap, endpoint);

    if (auth::keystore::available()
        && auth::keystore::store(keystore_key(endpoint), key) == auth::keystore::Status::Ok) {
        // 腰带和背带：放下任何 过期 密封副本，
        // 这样这个秘密就有一个归宿。
        (void)update_key(io, cap, endpoint, {});
        return true;
    }
    return update_key(io, cap, endpoint, key);
}

bool erase_key(Io io, const lock_levels::below_file_lane& cap, const std::string& endpoint) {
    if (endpoint.empty()) return false;
    bool any = false;
    if (auth::keystore::available())
        any = auth::keystore::remove(keystore_key(endpoint)) == auth::keystore::Status::Ok;
    const bool had = read_all().contains(endpoint);
    if (had) any = update_key(io, cap, endpoint, {}) || any;
    return any;
}

std::string endpoint_key(const EmbedConfig& c) {
    if (!needs_api_key(c.backend)) return {};
    std::string out;
    out += id_of(c.backend);
    out += "://";
    out += c.host;
    out += ':';
    out += std::to_string(c.port);
    if (!c.path.empty()) out += c.path;
    return out;
}

} // namespace agentty::rag::embed
