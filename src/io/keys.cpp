// agentty::auth::keys-密封提供者-key金库的实现。
// 有关安全原理，请参阅 keys.hpp。
//
// 持久化准确地镜像auth.cpp的凭据存储：用crypt::
// seal (机器-bound AES-256-GCM)密封
// JSON 载荷，在启用时将密封的信封镜像到OS密钥库中，
// 然后在0600处原子地写入文件。加载更喜欢密钥库（它
// 持有同一个信封）并落回到文件上；遗留明文 正文 永远不会被写回
// ——调用者将其从 settings.json 中迁移出来。

#include "agentty/auth/keys.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

#include <nlohmann/json.hpp>

#include "agentty/auth/auth.hpp"        // config_dir()
#include "agentty/auth/cred_crypt.hpp"  // crypt::seal/开封/looks_sealed
#include "agentty/auth/keystore.hpp"
#include "agentty/io/shared_file.hpp"
#include "agentty/util/dbglog.hpp"

namespace agentty::auth::keys {

namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;
namespace crypt = agentty::auth::crypt;

fs::path vault_path() {
    return agentty::auth::config_dir() / "provider-keys.json";
}

// 原子+0600写入密封信封。它唯一的编写者是
// save_settings_locked，位于设置文件的部分内，
// 因此并发进程已经在这里轮流进行。
bool write_private(const fs::path& p, const std::string& body) {
    if (persistence::detail::write_in_section(p.string(), body,
            {.durability = persistence::Durability::Durable, .mode = 0600}))
        return true;
    util::dbglog("keys.save", "could not write " + p.string());
    return false;
}

// 保险库 正文 是一个普通的 JSON 对象：{ "provider-id": "key", … }。
// 拒绝非-string成员而不是信任他们——地图是传输与
// Settings.provider_keys的合同。
std::optional<KeyMap> parse(const std::string& body) {
    try {
        json j = json::parse(body);
        if (!j.is_object()) return std::nullopt;
        KeyMap out;
        for (auto& [k, v] : j.items())
            if (v.is_string()) out[k] = v.get<std::string>();
        return out;
    } catch (const std::exception& e) {
        util::dbglog("keys.load.parse", e.what());
    } catch (...) {
        util::dbglog("keys.load.parse", "non-std exception");
    }
    return std::nullopt;
}

} // 命名空间结束。

std::string path() { return vault_path().string(); }

KeyMap load() {
    // 首先是密钥库，与auth::load_credentials
    // 完全相同：当启用时，它保存相同的密封信封，并且文件变成回退。
    if (keystore::available()) {
        std::string raw;
        if (keystore::retrieve("provider-keys", raw) == keystore::Status::Ok
            && !raw.empty()) {
            if (auto body = crypt::unseal(raw))
                if (auto parsed = parse(*body)) return std::move(*parsed);
            // 验证失败的密钥库信封落入文件——解封是
            // fail-closed，因此这只能意味着
            // 过期机器绑定，并且文件副本是较新的存储。
        }
    }
    auto file = persistence::peek_file(vault_path());
    if (!file || file->empty()) return {};
    const std::string& raw = *file;
    // 未经身份验证的 字节 失败关闭：机器错误/损坏 → {}。
    auto body = crypt::looks_sealed(raw) ? crypt::unseal(raw) : std::nullopt;
    if (!body) {
        util::dbglog("keys.load", "unauthenticated vault ignored");
        return {};
    }
    auto parsed = parse(*body);
    return parsed ? std::move(*parsed) : KeyMap{};
}

bool save(const KeyMap& keys) {
    // 空映射是AUTHORITATIVE（符号-out必须不留下任何可恢复的密钥），
    // 因此它擦除保管库而不是写入空信封——没有
    // 幽灵文件，没有过期密钥库项目。
    if (keys.empty()) {
        clear();
        return true;
    }
    json j = json::object();
    for (const auto& [k, v] : keys) j[k] = v;
    // 拒绝保留我们无法加密的内容——与
    // auth::save_credentials 相同的合同。
    // in-memory 设置仍然适用于会话；调用者显示写入失败。
    auto sealed = crypt::seal(j.dump(2));
    if (!sealed) {
        util::dbglog("keys.save", "seal failed; vault left unchanged");
        return false;
    }
    if (keystore::available())
        keystore::store("provider-keys", *sealed);
    return write_private(vault_path(), *sealed);
}

void clear() {
    if (keystore::available())
        keystore::remove("provider-keys");
    std::error_code ec;
    fs::remove(vault_path(), ec);
}

} // namespace agentty::auth::keys
