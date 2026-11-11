// agentty::auth::accounts —命名为每-provider 凭据槽。
//
// 一份 JSON 文件，用 crypt::seal (机器-bound)密封。
// 旧版明文 正文 被接受，并在下次写入时重新-sealed。
//
// 计算机上的每个 agentty 都共享此文件，因此每次更改都是在其共享-file
// 部分 (io/shared_file.hpp) 内读取 -modify-write：
// 两个同时添加帐户的窗口都保留各自的帐户。

#include "agentty/auth/accounts.hpp"

#include <algorithm>
#include <chrono>

#include <nlohmann/json.hpp>

#include "agentty/auth/auth.hpp"          // config_dir()
#include "agentty/auth/cred_crypt.hpp"    // crypt::seal/开封/looks_sealed
#include "agentty/io/shared_file.hpp"

namespace agentty::auth::accounts {

namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;
namespace crypt = agentty::auth::crypt;

fs::path registry_path() { return agentty::auth::config_dir() / "accounts.json"; }

std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// 在-memory形状中：提供商→{active_label，帐户[]}。
struct Registry {
    std::vector<Account>                            all;
    std::vector<std::pair<std::string,std::string>> active;  // 提供商→标签

    [[nodiscard]] std::string active_for(const std::string& provider) const {
        for (const auto& [p, l] : active) if (p == provider) return l;
        return {};
    }
    void set_active(const std::string& provider, const std::string& label) {
        for (auto& [p, l] : active) if (p == provider) { l = label; return; }
        active.emplace_back(provider, label);
    }
};

Registry parse_registry(const std::optional<std::string>& file) {
    Registry reg;
    if (!file || file->empty()) return reg;
    std::string body;
    if (crypt::looks_sealed(*file)) {
        auto pt = crypt::unseal(*file);
        if (!pt) return reg;                // 被篡改/错机
        body = std::move(*pt);
    } else {
        body = *file;                       // 遗留明文
    }
    try {
        json j = json::parse(body);
        for (const auto& a : j.value("accounts", json::array())) {
            Account acc;
            acc.provider    = a.value("provider", "");
            acc.label       = a.value("label", "");
            acc.secret      = a.value("secret", "");
            acc.saved_at_ms = a.value("saved_at", std::int64_t{0});
            if (!acc.provider.empty() && !acc.label.empty())
                reg.all.push_back(std::move(acc));
        }
        // 不是直接在 j.value(...) 上的 .items()：value()
        // 返回临时值，items() 将悬空（GCC 14 版本删除了 注册表）。
        const json active = j.value("active", json::object());
        for (const auto& [prov, lbl] : active.items())
            reg.set_active(prov, lbl.get<std::string>());
    } catch (...) {
        return Registry{};
    }
    return reg;
}

// 密封字节，或者当密封失败时无效：秘密永远不会被明文书写。
std::optional<std::string> serialise(const Registry& reg) {
    json j;
    j["accounts"] = json::array();
    for (const auto& a : reg.all)
        j["accounts"].push_back({{"provider", a.provider}, {"label", a.label},
                                 {"secret", a.secret}, {"saved_at", a.saved_at_ms}});
    j["active"] = json::object();
    for (const auto& [p, l] : reg.active) j["active"][p] = l;
    return crypt::seal(j.dump(2));
}

Registry read_registry(Io io) { return parse_registry(persistence::read_file(io, registry_path())); }
Registry read_registry_in_section() { return parse_registry(persistence::peek_file(registry_path())); }

persistence::Lane& registry_lane() {
    static persistence::Lane l;
    return l;
}

// 对 注册表 的一项更改：在该部分中读取它，让 `edit` 更改它（假
// = 不执行任何操作），然后将其写回。仅当它发生更改且为 已保存
// 时才为真。
template <class F, class... Args>
bool mutate(const lock_levels::below_file_lane& cap, F edit, Args... args) {
    (void)edit;
    return persistence::with_shared_file(cap, registry_lane(), registry_path(),
        [](const maya::held<lock_levels::file_lane>&, persistence::Held, Args... a) {
            Registry reg = read_registry_in_section();
            if (!F{}(reg, std::move(a)...)) return false;
            const auto bytes = serialise(reg);
            if (!bytes) return false;
            return persistence::detail::write_in_section(registry_path().string(), *bytes,
                {.durability = persistence::Durability::Durable, .mode = 0600});
        }, std::move(args)...);
}

} // 命名空间结束。

std::string path() { return registry_path().string(); }

std::vector<Account> list(Io io) {
    auto reg = read_registry(io);
    // Newest-saved首先用于选择面板中稳定、有用的默认排序。
    std::stable_sort(reg.all.begin(), reg.all.end(),
                     [](const Account& a, const Account& b) {
                         return a.saved_at_ms > b.saved_at_ms;
                     });
    return std::move(reg.all);
}

std::vector<Account> list_for(Io io, const std::string& provider) {
    std::vector<Account> out;
    for (auto& a : list(io))
        if (a.provider == provider) out.push_back(std::move(a));
    return out;
}

std::string active_label(Io io, const std::string& provider) {
    return read_registry(io).active_for(provider);
}

Snapshot snapshot(Io io) {
    Registry reg = read_registry(io);
    Snapshot s;
    std::stable_sort(reg.all.begin(), reg.all.end(),
                     [](const Account& a, const Account& b) { return a.saved_at_ms > b.saved_at_ms; });
    s.all    = std::move(reg.all);
    s.active = std::move(reg.active);
    return s;
}

std::vector<std::string> Snapshot::labels_for(const std::string& provider) const {
    std::vector<std::string> out;
    for (const auto& a : all)
        if (a.provider == provider) out.push_back(a.label);
    return out;
}

std::string Snapshot::active_for(const std::string& provider) const {
    for (const auto& [p, l] : active) if (p == provider) return l;
    return {};
}

std::optional<Account> get(Io io, const std::string& provider, const std::string& label) {
    for (auto& a : read_registry(io).all)
        if (a.provider == provider && a.label == label) return a;
    return std::nullopt;
}

bool upsert(Io, const lock_levels::below_file_lane& cap, const std::string& provider,
            const std::string& label, const std::string& secret) {
    if (provider.empty() || label.empty()) return false;
    return mutate(cap, [](Registry& reg, std::string p, std::string l, std::string s) {
        const auto now = now_ms();
        bool found = false;
        for (auto& a : reg.all)
            if (a.provider == p && a.label == l) {
                a.secret = s; a.saved_at_ms = now; found = true; break;
            }
        if (!found) reg.all.push_back(Account{p, l, s, now});
        reg.set_active(p, l);
        return true;
    }, provider, label, secret);
}

bool set_active(Io, const lock_levels::below_file_lane& cap, const std::string& provider,
                const std::string& label) {
    return mutate(cap, [](Registry& reg, std::string p, std::string l) {
        const bool exists = std::ranges::any_of(reg.all, [&](const Account& a) {
            return a.provider == p && a.label == l;
        });
        if (exists) reg.set_active(p, l);
        return exists;
    }, provider, label);
}

bool remove(Io, const lock_levels::below_file_lane& cap, const std::string& provider,
            const std::string& label) {
    return mutate(cap, [](Registry& reg, std::string p, std::string l) {
        const auto before = reg.all.size();
        std::erase_if(reg.all, [&](const Account& a) { return a.provider == p && a.label == l; });
        if (reg.all.size() == before) return false;
        // 移除活动槽位：提升最新的剩余槽位（如果有的话）。
        if (reg.active_for(p) == l) {
            std::string promote;
            std::int64_t best = -1;
            for (const auto& a : reg.all)
                if (a.provider == p && a.saved_at_ms > best) { best = a.saved_at_ms; promote = a.label; }
            if (!promote.empty()) reg.set_active(p, promote);
            else std::erase_if(reg.active, [&](const auto& kv) { return kv.first == p; });
        }
        return true;
    }, provider, label);
}

} // namespace agentty::auth::accounts
