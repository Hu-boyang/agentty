// web_search_secret.cpp — API 键 web_search 服务。 头文件.

#include "agentty/tool/web_search_secret.hpp"

#include "agentty/auth/auth.hpp"
#include "agentty/auth/cred_crypt.hpp"
#include "agentty/auth/keystore.hpp"
#include "agentty/io/shared_file.hpp"

#include <nlohmann/json.hpp>

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <unordered_map>

#include <maya/runtime.hpp>

namespace agentty::tools::web_search_secret {

namespace {

namespace fs = std::filesystem;

// 服务 - > 键作为上次从密钥托尔或密封文件读取的键。
using KeyCache = std::unordered_map<std::string, std::string>;
// A 快照 ,而不是一个有守护的地图:微小,每个搜索读,写得很少,
// 以及 load()/store() 从工具调用 正文 不需要能力。
maya::snapshot<KeyCache>& key_cache() {
    static maya::snapshot<KeyCache> c;
    return c;
}
void forget(std::string_view service) {
    // 每家店都为这项服务进账:便宜,而且永远不卖过期.
    (void)key_cache().update([](const KeyCache& old, std::string s) {
        KeyCache c = old;
        const std::string tail = '\n' + s;
        for (auto it = c.begin(); it != c.end();)
            it = it->first.ends_with(tail) ? c.erase(it) : std::next(it);
        return c;
    }, std::string{service});
}

// 每个服务钥匙一个文件,封为单位.
[[nodiscard]] fs::path secrets_path() {
    return auth::config_dir() / "web_search_keys.json";
}

[[nodiscard]] std::string keystore_key(std::string_view service) {
    return "agentty:web_search:" + std::string{service};
}

// 已封存的文件, 打开。 当文件 IS 无法读回时无效。
// 损坏、 截断或封存到另一个机器上。 这与 "no keys"
// 不同 : 前进的 store() 将重写文件, 用一个键,
// 并悄悄地摧毁其他服务。 因此, 呼叫者拒绝写入。
[[nodiscard]] std::optional<nlohmann::json> parse_all(const std::optional<std::string>& file) {
    if (!file || file->empty()) return nlohmann::json::object();   // 尚未有密钥
    const std::string& raw = *file;
    // 只接受密封信封。 embed_keys.json 没有留下的
    // 简单文字表可以读,所以一只手 -written 纯文本文件不
    // 作为密钥存储处被信任。
    if (auto opened = auth::crypt::unseal(raw)) {
        try {
            auto j = nlohmann::json::parse(*opened);
            if (j.is_object()) return j;
        } catch (...) { }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<nlohmann::json> read_all() {
    return parse_all(persistence::peek_file(secrets_path()));
}

// 设置( 空密钥: 滴) 一个服务的关键, 如一个读
// -modify-write 共享文件 -file 区域中,
// 保存不同服务的窗口都保留它们的。 封存, 0600, 原子。
// 拒绝触摸无法读取的文件( 将破坏其它服务的密钥) 或写平原。
persistence::Update update_key(Io io, const lock_levels::below_file_lane& cap,
                               const std::string& service, const std::string& key) {
    return persistence::update_shared(io, cap, secrets_path(),
        {.durability = persistence::Durability::Durable, .mode = 0600},
        [](std::optional<std::string> cur, std::string svc, std::string k)
            -> std::optional<std::string> {
            auto all = parse_all(cur);
            if (!all) return std::nullopt;
            if (k.empty()) { if (all->erase(svc) == 0) return std::nullopt; }
            else (*all)[svc] = k;
            return auth::crypt::seal(all->dump());
        }, service, key);
}

[[nodiscard]] std::string from_env(std::string_view service) {
    const std::string name = env_name(service);
    if (const char* v = std::getenv(name.c_str()); v && *v) return v;
    return {};
}

[[nodiscard]] std::string from_store(std::string_view service) {
    if (auth::keystore::available()) {
        std::string out;
        if (auth::keystore::retrieve(keystore_key(service), out)
                == auth::keystore::Status::Ok && !out.empty())
            return out;
    }
    const auto all = read_all();
    if (!all) return {};
    if (auto it = all->find(std::string{service}); it != all->end() && it->is_string())
        return it->get<std::string>();
    return {};
}

} // 命名空间结束。

std::string env_name(std::string_view service) {
    std::string out = "AGENTTY_WEB_SEARCH_KEY_";
    for (char c : service)
        out.push_back(c == '-' ? '_'
                      : static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    return out;
}

std::string load(std::string_view service) {
    if (service.empty()) return {};
    if (auto e = from_env(service); !e.empty()) return e;
    // 搜索可读到每个键数最多3个 调用 ,而商店读会 产生秘密 -tool
    // 保留我们所读的,每个机密文件(不同 AGENTTY_HOME
    // 是一个不同的商店; store()/erase() 放下那个条目。
    const std::string ck = secrets_path().string() + '\n' + std::string{service};
    {
        const auto c = key_cache().load();
        if (auto it = c->find(ck); it != c->end()) return it->second;
    }
    std::string got = from_store(service);
    // 仅保存一个找到的密钥 : "none" 不得隐藏一个存储在另一个
    // agentty 窗口。
    if (!got.empty())
        (void)key_cache().update([](const KeyCache& old, std::string s, std::string k) {
            KeyCache c = old;
            c[std::move(s)] = std::move(k);
            return c;
        }, ck, got);
    return got;
}

bool has(std::string_view service) { return !load(service).empty(); }

std::string origin(std::string_view service) {
    if (service.empty()) return {};
    if (!from_env(service).empty()) return "env: " + env_name(service);
    if (!from_store(service).empty()) return "stored";
    return {};
}

bool plausible(std::string_view key) {
    if (key.size() < 8 || key.size() > 512) return false;
    for (char c : key) {
        const auto u = static_cast<unsigned char>(c);
        if (u <= 0x20 || u == 0x7f) return false;
    }
    return true;
}

bool store(Io io, const lock_levels::below_file_lane& cap, std::string_view service,
           std::string_view key) {
    if (service.empty()) return false;
    forget(service);
    if (key.empty()) return erase(io, cap, service);
    if (!plausible(key)) return false;

    if (auth::keystore::available()
        && auth::keystore::store(keystore_key(service), std::string{key})
               == auth::keystore::Status::Ok) {
        // 一个家的秘密: 放下任何 过期 封存副本。
        (void)update_key(io, cap, std::string{service}, {});
        return true;
    }
    if (update_key(io, cap, std::string{service}, std::string{key})
        != persistence::Update::Written)
        return false;
    // 键盘已启用, 但拒绝写入: an OLDER 复制到那里仍然
    // 能赢得文件( Q) load() 问键盘先,所以放下它。
    if (auth::keystore::available())
        (void)auth::keystore::remove(keystore_key(service));
    return true;
}

bool erase(Io io, const lock_levels::below_file_lane& cap, std::string_view service) {
    if (service.empty()) return false;
    forget(service);
    bool any = false;
    if (auth::keystore::available())
        any = auth::keystore::remove(keystore_key(service)) == auth::keystore::Status::Ok;
    if (update_key(io, cap, std::string{service}, {}) == persistence::Update::Written) any = true;
    return any;
}

} // namespace agentty::tools::web_search_secret
