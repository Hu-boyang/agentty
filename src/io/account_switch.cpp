// agentty::auth::accounts —提供商-specific适配器，
// 其将不透明账户注册表桥接至每个提供商的ACTIVE 凭据存储。
//
// 策略：snapshot/restore为活跃商店的原始on-disk
// 凭据 BLOB，字节-for-byte。这是provider-agnostic
// （无论文件是密封的还是legacy-plaintext都有效）并保持
// 完美的保真度——恢复的帐户与刚刚登录的帐户没有什么区别。 注册表 本身
// 是单独密封的，因此明文 blob 永远不会驻留在提供商自己的（已经是
// 0600，keystore-backed）存储之外的磁盘上。

#include "agentty/auth/accounts.hpp"

#include <filesystem>
#include <fstream>
#include <functional>
#include <cstdint>
#include <cstdio>
#include <iterator>

#include <nlohmann/json.hpp>

#include "agentty/auth/auth.hpp"
#include "agentty/auth/cred_crypt.hpp"
#include "agentty/io/persistence.hpp"       // load/save_settings（自定义主机）
#include "agentty/io/shared_file.hpp"
#include "agentty/provider/registry.hpp"      // preset_for（自定义-host检测）
#include "agentty/provider/chatgpt/codex_oauth.hpp"
#include "agentty/provider/copilot/copilot_oauth.hpp"
#include "agentty/provider/kimi/kimi_oauth.hpp"
#include "agentty/util/dbglog.hpp"

namespace agentty::auth::accounts {

namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;
namespace crypt = agentty::auth::crypt;
namespace chatgpt = agentty::provider::chatgpt;
namespace copilot = agentty::provider::copilot;
namespace kimi = agentty::provider::kimi;

// 读取整个文件；当 missing/empty. 时为 nullopt
std::optional<std::string> read_all(Io io, const fs::path& p) {
    auto s = agentty::persistence::read_file(io, p);
    if (!s || s->empty()) return std::nullopt;
    return s;
}

// 将 blob 安装为提供者的实时 凭据 文件，位于该文件的部分内，
// 因此它不能落在同一文件（此处或另一个 agentty
// 中）的刷新中间。 0600，原子。
bool write_store(const maya::unlocked& cap, const fs::path& p, const std::string& blob) {
    return agentty::persistence::with_shared_file(cap, agentty::persistence::cred_lane(p), p,
        [](const maya::held<lock_levels::cred_file>&, agentty::persistence::Held,
           std::string path, std::string b) {
            return agentty::persistence::detail::write_in_section(path, b,
                {.durability = agentty::persistence::Durability::Durable, .mode = 0600});
        }, p.string(), blob);
}

// 将存储 blob 解密为其 JSON 正文 (接受密封或明文)。
std::optional<std::string> body_of(const std::string& blob) {
    if (crypt::looks_sealed(blob)) return crypt::unseal(blob);
    return blob;
}

// CUSTOM HOST的账户提供者id IS其端点规范（其承载密钥存在于
// Settings.provider_keys中的密钥）。自定义主机没有 凭据 FILE
// — 它们的 "active credential" 是 provider_keys
// [spec] 条目 — 因此四个 file-backed 提供程序（和空→
// 人性化）不是 自定义主机；其他非 -empty 的内容均被视为规格。
bool is_custom_host_provider(const std::string& p) {
    return !p.empty() && p != "anthropic" && p != "chatgpt"
        && p != "copilot" && p != "kimi";
}

// ── 每个提供者一个后端 ────────────────────────────────────────────────
// 提供者之间的 仅 的不同之处在于（a) WHERE 活动的 凭据 存在 — 一个文件，
// 或 Settings.provider_keys[规范] — 以及 (b) 如何将
// -seal 重新放入密钥库并对其进行标记。
// 捕获一次所以snapshot_active/activate/derive_current_label共享一个，统一正文。
enum class Store : std::uint8_t { File, SettingsKey };

struct Backend {
    Store           store;
    fs::path        file;                 // 当存储==文件时
    std::function<std::string(const json&)> label_body;   // 文件：来自JSON 正文的标签
    // 文件：密钥库re-seal/缓存被破坏。获取叶子 凭据-store 锁。
    std::function<void(Io, const maya::below<maya::leaf>&)> after_activate;
};

// 读取SettingsKey(custom-host)秘密，或者为空。
std::string settings_key(Io io, const std::string& provider) {
    auto s = agentty::persistence::load_settings(io);
    auto it = s.provider_keys.find(provider);
    return it != s.provider_keys.end() ? it->second : std::string{};
}
void set_settings_key(Io io, const maya::unlocked& cap, const std::string& provider,
                      const std::string& v) {
    auto s = agentty::persistence::load_settings(io);
    s.provider_keys[provider] = v;
    agentty::persistence::save_settings(io, cap, s);
}

// 解析后端以获取提供者id。
Backend backend_for(const std::string& provider) {
    Backend b;
    if (is_custom_host_provider(provider)) {
        b.store = Store::SettingsKey;
        return b;
    }
    b.store = Store::File;
    if (provider == "anthropic" || provider.empty()) {
        b.file = agentty::auth::credentials_path();
        b.label_body = [](const json& j) -> std::string {
            std::string method = j.value("method", "");
            if (method == "api_key" || method == "apikey") return "API key";
            if (method == "oauth")   return "OAuth login";
            if (!method.empty())     return method;
            return "signed in";
        };
        b.after_activate = [](Io io, const maya::below<maya::leaf>&) {
            if (auto c = agentty::auth::load_credentials(io))
                agentty::auth::save_credentials(io, *c);   // re-seal 到密钥库
        };
    } else if (provider == "chatgpt") {
        b.file = chatgpt::codex_credentials_path();
        b.label_body = [](const json& j) -> std::string {
            std::string acct = j.value("account_id", "");
            return acct.empty() ? "ChatGPT" : "ChatGPT " + acct.substr(0, 8);
        };
        b.after_activate = [](Io, const maya::below<maya::leaf>& cap) {
            if (auto c = chatgpt::load_codex_credentials())
                chatgpt::save_codex_credentials(cap, *c);
        };
    } else if (provider == "copilot") {
        b.file = copilot::credentials_path();
        b.label_body = [](const json& j) -> std::string {
            std::string sku;
            if (j.contains("proxy") && j["proxy"].is_object())
                sku = j["proxy"].value("sku", "");
            std::string gh = j.value("github_token", "");
            std::string tag = gh.size() >= 8 ? gh.substr(gh.size() - 4) : "";
            std::string base = sku.empty() ? "Copilot" : "Copilot (" + sku + ")";
            return tag.empty() ? base : base + " \xe2\x80\xa6" + tag;
        };
        b.after_activate = [](Io, const maya::below<maya::leaf>&) {
            copilot::invalidate_cached_token();
        };
    } else if (provider == "kimi") {
        b.file = kimi::credentials_path();
        b.label_body = [](const json& j) -> std::string {
            std::string rt = j.value("refresh_token", std::string{});
            if (rt.empty()) rt = j.value("access_token", std::string{});
            std::string tag = rt.size() >= 4 ? rt.substr(rt.size() - 4) : "";
            return tag.empty() ? std::string{"Kimi"} : "Kimi \xe2\x80\xa6" + tag;
        };
        b.after_activate = [](Io, const maya::below<maya::leaf>&) {
            kimi::invalidate_cached_token();
        };
    }
    return b;
}

// 用于SettingsKey (定制-host)承载密钥的稳定的、
// 人性化的-recognisable标签——前缀…后缀不够唯一，因此WHOLE密
// 钥的6-hex散列保证不同的密钥获得不同的标签。
std::string settings_key_label(const std::string& key) {
    if (key.empty()) return {};
    std::string tail = key.size() >= 4 ? key.substr(key.size() - 4) : key;
    const std::uint32_t h = std::hash<std::string>{}(key) & 0xffffffu;
    char hx[7];
    std::snprintf(hx, sizeof(hx), "%06x", h);
    return "key \xe2\x80\xa6" + tail + " #" + std::string(hx, 6);
}

} // 命名空间结束。

bool snapshot_active(Io io, const lock_levels::below_file_lane& cap,
                     const std::string& provider, const std::string& label) {
    const Backend b = backend_for(provider);
    if (b.store == Store::SettingsKey) {
        const std::string key = settings_key(io, provider);
        if (key.empty()) return false;
        return upsert(io, cap, provider, label, key);
    }
    auto blob = read_all(io, b.file);
    if (!blob) return false;                 // 没有登录捕获
    return upsert(io, cap, provider, label, *blob);
}

bool activate(Io io, const maya::unlocked& cap, const std::string& provider,
              const std::string& label) {
    auto slot = get(io, provider, label);
    if (!slot) return false;

    // TOKEN-ROTATION SAFETY (参见
    // accounts.hpp):在覆盖活动存储之前将LIVE 凭据
    // 捕获回传出账户的注册表槽中。 Anthropic 在每次刷新时
    // 轮换刷新令牌 - 如果没有这个，我们要离开的帐户的插槽仍保留登录
    // 日的刷新令牌，服务器已将其失效；稍后切换回它，然后 FAILS 每
    // 个 REFRESH (invalid_grant)，直到用户重新
    // -logs-in。 Best-effort 的设计：如果没有任何
    // 活动（全新安装、存储擦除），那么 保存 就没有什么新鲜的了。
    if (const std::string outgoing = active_label(io, provider);
        !outgoing.empty() && outgoing != label) {
        if (!snapshot_active(io, cap, provider, outgoing))
            util::dbglog("accounts.activate.snapshot_outgoing",
                         provider + "/" + outgoing + ": no live credential");
    }

    const Backend b = backend_for(provider);
    if (b.store == Store::SettingsKey) {
        set_settings_key(io, cap, provider, slot->secret);   // 端点resolve()读取
        return set_active(io, cap, provider, label);
    }
    if (!write_store(cap, b.file, slot->secret)) return false;
    // Re-seal进入密钥库/破坏缓存的令牌，因此下一个resolve
    // ()读取切换的-to账户，而不是过期条目。
    if (b.after_activate) b.after_activate(io, cap);
    return set_active(io, cap, provider, label);
}

namespace {
std::string derive_from_file(Io io, const Backend& b) {
    auto blob = read_all(io, b.file);
    if (!blob) return {};
    auto body = body_of(*blob);
    if (!body) return "signed in";           // 密封用于另一台机器
    try {
        json j = json::parse(*body);
        if (b.label_body) return b.label_body(j);
    } catch (const std::exception& e) {
        util::dbglog("accounts.derive_label.parse", e.what());
    } catch (...) {
        util::dbglog("accounts.derive_label.parse", "non-std exception");
    }
    return "signed in";
}
}  // 命名空间结束。

std::string derive_current_label(Io io, const std::string& provider) {
    const Backend b = backend_for(provider);
    if (b.store == Store::SettingsKey)
        return settings_key_label(settings_key(io, provider));
    return derive_from_file(io, b);
}

std::string derive_current_label(Io io, const std::string& provider,
                                 const store::Settings& settings) {
    const Backend b = backend_for(provider);
    if (b.store == Store::SettingsKey) {
        auto it = settings.provider_keys.find(provider);
        return settings_key_label(it != settings.provider_keys.end() ? it->second : std::string{});
    }
    return derive_from_file(io, b);
}

} // namespace agentty::auth::accounts
