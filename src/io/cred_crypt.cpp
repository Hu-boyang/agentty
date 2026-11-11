#include "agentty/auth/cred_crypt.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>
#include <maya/runtime.hpp>   // maya::once
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>

#include "agentty/util/base64.hpp"

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <termios.h>
#  include <unistd.h>
#endif

namespace agentty::auth::crypt {

namespace {

using json = nlohmann::json;

// 修复了绑定到密钥导出中的应用上下文。更改此字符串会使之前的每个
// -sealed 文件失效（它们将无法通过身份验证并被视为不可恢复
// → 用户 re-runs `agentty login`）。
constexpr std::string_view kInfo = "agentty-credentials-v1";

// libcrypto 的第一次使用否则会读取 解析 openssl.cnf (每次
// 启动约 0.4 毫秒)。这里没有什么需要它。
void init_crypto() {
    OPENSSL_init_crypto(OPENSSL_INIT_NO_LOAD_CONFIG, nullptr);
}

// ── 机器-stable种子 ────────────────────────────────────────────────
// 混合：per-machine id、当前用户 id 和应用程序上下文。
// 这不是 的秘密存储 - 它的密钥材料对于同一台（机器、
// 用户）保持不变，因此今天密封的文件明天就会解密，
// 但传送到 ANOTHER 机器（或其他用户）的文件副本则不会。
std::string machine_seed() {
    std::string seed;

#if defined(_WIN32)
    // MachineGuid是稳定的per-install标识符。
    HKEY hk{};
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "SOFTWARE\\Microsoft\\Cryptography", 0,
                      KEY_READ | KEY_WOW64_64KEY, &hk) == ERROR_SUCCESS) {
        char buf[256]; DWORD sz = sizeof(buf); DWORD type = 0;
        if (RegQueryValueExA(hk, "MachineGuid", nullptr, &type,
                             reinterpret_cast<BYTE*>(buf), &sz) == ERROR_SUCCESS
            && type == REG_SZ && sz > 0) {
            seed.assign(buf, sz - 1); // 删除尾随 NUL
        }
        RegCloseKey(hk);
    }
    if (seed.empty()) {
        char name[256]; DWORD n = sizeof(name);
        if (GetComputerNameA(name, &n)) seed.assign(name, n);
    }
    // Per-user salt:用户名是Windows上uid的合理立场-in。
    if (const char* u = std::getenv("USERNAME")) { seed += '\x1f'; seed += u; }
#else
    for (const char* path : {"/etc/machine-id", "/var/lib/dbus/machine-id"}) {
        std::ifstream f(path);
        if (f) { std::getline(f, seed); if (!seed.empty()) break; }
    }
    if (seed.empty()) {
        char host[256];
        if (::gethostname(host, sizeof(host)) == 0) {
            host[sizeof(host) - 1] = '\0';
            seed = host;
        }
    }
    // 绑定到数字uid，以便共享机器保持per-user隔离。
    seed += '\x1f';
    seed += std::to_string(static_cast<unsigned long>(::getuid()));
#endif

    if (seed.empty()) seed = "agentty-fallback-seed";
    seed += '\x1f';
    seed.append(kInfo);
    return seed;
}

// HKDF-SHA256（种子、盐、信息）→ 32-byte AES 密钥。
// OpenSSL 3的EVP_KDF是可移植路径；已弃用的 one-shot HKDF()
// 也可以工作，但 EVP_KDF 是链接的 OpenSSL 中保证存在的内容。
bool derive_key(const std::string& seed,
                const unsigned char* salt, size_t salt_len,
                std::array<unsigned char, 32>& out_key) {
    init_crypto();
    EVP_KDF* kdf = EVP_KDF_fetch(nullptr, "HKDF", nullptr);
    if (!kdf) return false;
    EVP_KDF_CTX* ctx = EVP_KDF_CTX_new(kdf);
    EVP_KDF_free(kdf);
    if (!ctx) return false;

    OSSL_PARAM params[5];
    int i = 0;
    char digest[] = "SHA256";
    params[i++] = OSSL_PARAM_construct_utf8_string("digest", digest, 0);
    params[i++] = OSSL_PARAM_construct_octet_string(
        "key", const_cast<char*>(seed.data()), seed.size());
    params[i++] = OSSL_PARAM_construct_octet_string(
        "salt", const_cast<unsigned char*>(salt), salt_len);
    params[i++] = OSSL_PARAM_construct_octet_string(
        "info", const_cast<char*>(kInfo.data()), kInfo.size());
    params[i]   = OSSL_PARAM_construct_end();

    bool ok = EVP_KDF_derive(ctx, out_key.data(), out_key.size(), params) == 1;
    EVP_KDF_CTX_free(ctx);
    return ok;
}

// ── 可选密码句( N) 内存-hard 因素) ───────────────────────────
// scrypt参数。 N=2^15 (32768), r=8,
// p=1 是来自 RFC 7914 — ~32 MiB 的交互式-login
// 配置文件，每次推导需要数十毫秒，这使得针对文件的离线猜测攻击极其缓慢，
// 同时对此处的 once-per-save/load 路径保持难以察觉。
// 存储在信封中，因此即使我们稍后重新调整，启封后也会复制它们。
constexpr uint64_t kScryptN = 1u << 15;
constexpr uint32_t kScryptR = 8;
constexpr uint32_t kScryptP = 1;

// 在禁用 终端 回显的情况下从 /dev/tty 读取一行（因此密码短语
// 不会绘制在屏幕上）。如果没有控制 tty (e.g.piped/headless)，
// 则返回 std::nullopt — 调用者将出现错误而不是提示。
std::optional<std::string> prompt_passphrase() {
#if defined(_WIN32)
    return std::nullopt;   // 此处不支持交互提示词；使用环境变量
#else
    FILE* tty = std::fopen("/dev/tty", "r+");
    if (!tty) return std::nullopt;
    int fd = ::fileno(tty);

    termios old{};
    bool restored = false;
    if (::tcgetattr(fd, &old) == 0) {
        termios noecho = old;
        noecho.c_lflag &= ~(tcflag_t)ECHO;
        ::tcsetattr(fd, TCSAFLUSH, &noecho);
    } else {
        restored = true;   // 无法 切换 回显；仍在阅读，只是回声
    }

    std::fputs("Enter agentty credential passphrase: ", tty);
    std::fflush(tty);

    std::string line;
    int c;
    while ((c = std::fgetc(tty)) != EOF && c != '\n') line.push_back((char)c);

    if (!restored) { ::tcsetattr(fd, TCSAFLUSH, &old); std::fputc('\n', tty); }
    std::fclose(tty);
    return line;
#endif
}

// 解析该进程的密码，一次。优先级：
//   1.AGENTTY_PASSPHRASE(显式秘密，e.g.CI)
//   2.AGENTTY_ENCRYPT_PASSPHRASE真实→在/dev/tty上互动提示词
//   3.既不是→也没有密码(machine-only模式)
// 结果（present-or-absent)被缓存，因此每次运行我们最多提示词一次，
// 并且会话中的每个seal/unseal都同意。
const std::optional<std::string>& resolve_passphrase() {
    static std::once_flag once;
    static std::optional<std::string> cached;
    maya::once(once, [] {
        if (const char* p = std::getenv("AGENTTY_PASSPHRASE"); p && *p) {
            cached = std::string{p};
            return;
        }
        const char* want = std::getenv("AGENTTY_ENCRYPT_PASSPHRASE");
        const bool truthy = want && *want
            && std::strcmp(want, "0") != 0
            && std::strcmp(want, "false") != 0;
        if (truthy) {
            if (auto pw = prompt_passphrase(); pw && !pw->empty())
                cached = std::move(pw);
        }
    });
    return cached;
}

// 归并通过scrypt将密码短语转换成内存-hard密钥材料的32个字节，
// 具有EXPLICIT成本参数。相同的（密码、盐、
// 参数）总是产生相同的输出，因此 unseal 从信封存储的参数中再现它。
// 当 OpenSSL 失败时返回 false（e.g.内存 限制）。
bool scrypt_derive_params(const std::string& pass,
                          const unsigned char* salt, size_t salt_len,
                          uint64_t n, uint32_t r, uint32_t p,
                          std::array<unsigned char, 32>& out) {
    EVP_KDF* kdf = EVP_KDF_fetch(nullptr, "SCRYPT", nullptr);
    if (!kdf) return false;
    EVP_KDF_CTX* ctx = EVP_KDF_CTX_new(kdf);
    EVP_KDF_free(kdf);
    if (!ctx) return false;

    // scrypt 的默认 内存 保护（~32 MiB)可以在某些构
    // 建上拒绝 N=2^15；提高上限，以便预期的参数始终运行。
    uint64_t maxmem = 256ull * 1024 * 1024;
    OSSL_PARAM full[7];
    int i = 0;
    full[i++] = OSSL_PARAM_construct_octet_string(
        "pass", const_cast<char*>(pass.data()), pass.size());
    full[i++] = OSSL_PARAM_construct_octet_string(
        "salt", const_cast<unsigned char*>(salt), salt_len);
    full[i++] = OSSL_PARAM_construct_uint64("n", &n);
    full[i++] = OSSL_PARAM_construct_uint32("r", &r);
    full[i++] = OSSL_PARAM_construct_uint32("p", &p);
    full[i++] = OSSL_PARAM_construct_uint64("maxmem_bytes", &maxmem);
    full[i]   = OSSL_PARAM_construct_end();

    bool ok = EVP_KDF_derive(ctx, out.data(), out.size(), full) == 1;
    EVP_KDF_CTX_free(ctx);
    return ok;
}

// 方便：用当前默认参数（在密封时使用）导出。
bool scrypt_derive(const std::string& pass,
                   const unsigned char* salt, size_t salt_len,
                   std::array<unsigned char, 32>& out) {
    return scrypt_derive_params(pass, salt, salt_len,
                                kScryptN, kScryptR, kScryptP, out);
}

// ── argon2id( 链接的OpenSSL 提供时首选) ─────────────
// Argon2id是现代的PHC-winner 内存-hard KDF；
// 它比 scrypt 更好地抵抗 GPU 和 side-channel 攻击。
// 它仅在 OpenSSL 3.2 中发布，因此我们在 运行时 上对其进行 PROBE
// 并回退到较旧的 库 上的 scrypt。新文件更喜欢argon2id；
// KDF 运行的信封记录因此无论当前版本支持什么，解封总是会重现正确的记录。
//
// 参数：t = 3次通过，m = 65536 KiB (64 MiB)，
// p = 1泳道。保守的交互配置文件 — ~64 MiB 和数十毫秒，
// 与 scrypt 的成本级别匹配，同时更难离线攻击。
constexpr uint32_t kArgonT = 3;         // 迭代/遍
constexpr uint32_t kArgonM = 64 * 1024; // 内存 在 KiB (64 MiB)
constexpr uint32_t kArgonP = 1;         // 并行度/通道

bool argon2id_available() {
    init_crypto();
    static const bool ok = [] {
        EVP_KDF* k = EVP_KDF_fetch(nullptr, "ARGON2ID", nullptr);
        if (!k) return false;
        EVP_KDF_free(k);
        return true;
    }();
    return ok;
}

// 通过具有 EXPLICIT 参数的 Argon2id 导出 32 字节 (因此从
// 信封中重新开封)。如果 OpenSSL 失败/不受支持，则返回 false。
bool argon2id_derive_params(const std::string& pass,
                            const unsigned char* salt, size_t salt_len,
                            uint32_t t, uint32_t m, uint32_t p,
                            std::array<unsigned char, 32>& out) {
    EVP_KDF* kdf = EVP_KDF_fetch(nullptr, "ARGON2ID", nullptr);
    if (!kdf) return false;
    EVP_KDF_CTX* ctx = EVP_KDF_CTX_new(kdf);
    EVP_KDF_free(kdf);
    if (!ctx) return false;

    OSSL_PARAM full[7];
    int i = 0;
    full[i++] = OSSL_PARAM_construct_octet_string(
        "pass", const_cast<char*>(pass.data()), pass.size());
    full[i++] = OSSL_PARAM_construct_octet_string(
        "salt", const_cast<unsigned char*>(salt), salt_len);
    full[i++] = OSSL_PARAM_construct_uint32("iter",      &t);
    full[i++] = OSSL_PARAM_construct_uint32("memcost",   &m);
    full[i++] = OSSL_PARAM_construct_uint32("lanes",     &p);
    // Argon2要求线程<=泳道；引脚 线程=1 用于确定性。
    static uint32_t threads = 1;
    full[i++] = OSSL_PARAM_construct_uint32("threads",   &threads);
    full[i]   = OSSL_PARAM_construct_end();

    bool ok = EVP_KDF_derive(ctx, out.data(), out.size(), full) == 1;
    EVP_KDF_CTX_free(ctx);
    return ok;
}

// 产生给定盐的最终 32-byte AES 密钥。配置密码短语后，
// 机器种子为 EXTENDED，密码短语上带有 内存-hard KDF，
// 因此密钥取决于 两者 因素；否则它是仅限机器的路径。
// `chosen_kdf` 报告运行的是哪个密码 KDF（"" = 无，i.e。
// machine-only v1)因此密封可以在信封上盖章并且开封可以复制它。
bool derive_final_key(const unsigned char* salt, size_t salt_len,
                      bool require_passphrase,
                      std::array<unsigned char, 32>& out_key,
                      std::string& chosen_kdf) {
    chosen_kdf.clear();
    std::string seed = machine_seed();

    const auto& pass = resolve_passphrase();
    if (pass && !pass->empty()) {
        std::array<unsigned char, 32> pk{};
        // 优选 Argon2id (如果可用)；在较旧的 OpenSSL 上回退到
        // scrypt。 AGENTTY_KDF=scrypt 强制 回退（测试/奇偶校验）。
        const char* force = std::getenv("AGENTTY_KDF");
        const bool want_scrypt = force && std::strcmp(force, "scrypt") == 0;
        if (!want_scrypt && argon2id_available()
            && argon2id_derive_params(*pass, salt, salt_len,
                                      kArgonT, kArgonM, kArgonP, pk)) {
            chosen_kdf = "argon2id";
        } else if (scrypt_derive(*pass, salt, salt_len, pk)) {
            chosen_kdf = "scrypt";
        } else {
            return false;
        }
        // 将 KDF 输出绑定到 HKDF 输入密钥材料中，
        // 因此最终密钥需要机器种子 和 密码。
        seed.push_back('\x1f');
        seed.append(reinterpret_cast<const char*>(pk.data()), pk.size());
        OPENSSL_cleanse(pk.data(), pk.size());
    } else if (require_passphrase) {
        // 信封上封有 WITH 密码，但现在没有可用的。
        return false;
    }

    bool ok = derive_key(seed, salt, salt_len, out_key);
    OPENSSL_cleanse(seed.data(), seed.size());
    return ok;
}

bool rand_bytes(unsigned char* buf, size_t len) {
    return RAND_bytes(buf, static_cast<int>(len)) == 1;
}

std::string b64(const unsigned char* p, size_t n) {
    return util::base64_encode(p, n);
}

} // 命名空间结束。

bool looks_sealed(const std::string& s) noexcept {
    // 廉价的结构探针：密封文件是JSON对象，其第一个非-space
    // 字节打开大括号并提及我们的标记。避免在 高频路径 上
    // 出现完整的 解析（每个 加载）。
    auto pos = s.find_first_not_of(" \t\r\n");
    if (pos == std::string::npos || s[pos] != '{') return false;
    return s.find("\"enc\"") != std::string::npos
        && s.find("aes-256-gcm") != std::string::npos;
}

std::optional<std::string> seal(const std::string& plaintext) {
    init_crypto();
    unsigned char salt[16];
    unsigned char nonce[12];
    if (!rand_bytes(salt, sizeof(salt)) || !rand_bytes(nonce, sizeof(nonce)))
        return std::nullopt;

    std::array<unsigned char, 32> key{};
    std::string chosen_kdf;
    if (!derive_final_key(salt, sizeof(salt), /*require_passphrase=*/false,
                          key, chosen_kdf))
        return std::nullopt;
    const bool used_pass = !chosen_kdf.empty();

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return std::nullopt;

    std::string ct;
    unsigned char tag[16];
    bool ok = false;
    do {
        if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1)
            break;
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, sizeof(nonce), nullptr) != 1)
            break;
        if (EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), nonce) != 1)
            break;

        ct.resize(plaintext.size());
        int outl = 0;
        if (EVP_EncryptUpdate(ctx,
                reinterpret_cast<unsigned char*>(ct.data()), &outl,
                reinterpret_cast<const unsigned char*>(plaintext.data()),
                static_cast<int>(plaintext.size())) != 1)
            break;
        int total = outl;
        int finl = 0;
        // GCM是数据流密码：EncryptFinal不发出额外的字节，
        // 但是调用必须运行以最终确定标签。
        if (EVP_EncryptFinal_ex(ctx,
                reinterpret_cast<unsigned char*>(ct.data()) + total, &finl) != 1)
            break;
        total += finl;
        ct.resize(static_cast<size_t>(total));
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, sizeof(tag), tag) != 1)
            break;
        ok = true;
    } while (false);

    EVP_CIPHER_CTX_free(ctx);
    // 及时从堆栈中擦除派生密钥。
    OPENSSL_cleanse(key.data(), key.size());
    if (!ok) return std::nullopt;

    json env = {
        {"v",     used_pass ? 2 : 1},
        {"enc",   "aes-256-gcm"},
        {"salt",  b64(salt, sizeof(salt))},
        {"nonce", b64(nonce, sizeof(nonce))},
        {"ct",    b64(reinterpret_cast<const unsigned char*>(ct.data()), ct.size())},
        {"tag",   b64(tag, sizeof(tag))},
    };
    // 当存在密码短语因素时，记录KDF+其参数，以便将来的解
    // 封（可能在我们重新调整成本之后，或者在没有argon2id
    // 的构建上）再现精确的推导，并且因此解封知道
    // REQUIRE密码短语。
    if (used_pass) {
        env["kdf"] = chosen_kdf;
        if (chosen_kdf == "argon2id") {
            env["kdf_t"] = kArgonT;   // 通行证
            env["kdf_m"] = kArgonM;   // 内存 KiB
            env["kdf_p"] = kArgonP;   // 车道
        } else {  // 加密
            env["kdf_n"] = kScryptN;
            env["kdf_r"] = kScryptR;
            env["kdf_p"] = kScryptP;
        }
    }
    return env.dump();
}

std::optional<std::string> unseal(const std::string& envelope) {
    init_crypto();
    json j;
    try {
        j = json::parse(envelope);
    } catch (...) {
        return std::nullopt;   // 不是 JSON → 不是我们的信封
    }
    if (!j.is_object() || j.value("enc", "") != "aes-256-gcm")
        return std::nullopt;

    std::string salt, nonce, ct, tag;
    try {
        salt  = util::base64_decode(j.value("salt", ""));
        nonce = util::base64_decode(j.value("nonce", ""));
        ct    = util::base64_decode(j.value("ct", ""));
        tag   = util::base64_decode(j.value("tag", ""));
    } catch (...) {
        return std::nullopt;
    }
    if (salt.empty() || nonce.size() != 12 || tag.size() != 16)
        return std::nullopt;

    // 该信封是否带有密码因子？ v2 信封上印有 "kdf"（
    // argon2id 或 scrypt）加上确切的成本参数。
    // 如果是这样，密码是 REQUIRED，我们使用 STORED
    // 参数 + KDF 重现推导（不是当前的默认值 - 密封版本
    // 可能使用了与此不同的 KDF 或成本）。
    const std::string kdf_name = j.value("kdf", "");
    const bool has_pass_kdf = (kdf_name == "scrypt" || kdf_name == "argon2id");

    std::array<unsigned char, 32> key{};
    const auto* salt_p = reinterpret_cast<const unsigned char*>(salt.data());

    if (has_pass_kdf) {
        const auto& pass = resolve_passphrase();
        if (!pass || pass->empty())
            return std::nullopt;   // 用密码密封；没有可用的

        std::array<unsigned char, 32> pk{};
        bool kok = false;
        if (kdf_name == "argon2id") {
            kok = argon2id_derive_params(*pass, salt_p, salt.size(),
                                         j.value("kdf_t", (uint32_t)kArgonT),
                                         j.value("kdf_m", (uint32_t)kArgonM),
                                         j.value("kdf_p", (uint32_t)kArgonP),
                                         pk);
        } else {  // 加密
            kok = scrypt_derive_params(*pass, salt_p, salt.size(),
                                       j.value("kdf_n", (uint64_t)kScryptN),
                                       j.value("kdf_r", (uint32_t)kScryptR),
                                       j.value("kdf_p", (uint32_t)kScryptP),
                                       pk);
        }
        if (!kok) return std::nullopt;   // KDF 失败/此处不支持 argon2id

        std::string seed = machine_seed();
        seed.push_back('\x1f');
        seed.append(reinterpret_cast<const char*>(pk.data()), pk.size());
        OPENSSL_cleanse(pk.data(), pk.size());
        bool dk = derive_key(seed, salt_p, salt.size(), key);
        OPENSSL_cleanse(seed.data(), seed.size());
        if (!dk) return std::nullopt;
    } else {
        // Legacy/machine-only信封：源自机器种子单独。
        if (!derive_key(machine_seed(), salt_p, salt.size(), key))
            return std::nullopt;
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return std::nullopt;

    std::string pt;
    bool ok = false;
    do {
        if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1)
            break;
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN,
                static_cast<int>(nonce.size()), nullptr) != 1)
            break;
        if (EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(),
                reinterpret_cast<const unsigned char*>(nonce.data())) != 1)
            break;

        pt.resize(ct.size());
        int outl = 0;
        if (EVP_DecryptUpdate(ctx,
                reinterpret_cast<unsigned char*>(pt.data()), &outl,
                reinterpret_cast<const unsigned char*>(ct.data()),
                static_cast<int>(ct.size())) != 1)
            break;
        int total = outl;
        // 设置预期标签BEFORE DecryptFinal——
        // GCM在此处验证真实性。被篡改的文件/错误的机器密钥在此失败并返回
        // nullopt（无明文泄漏）。
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG,
                static_cast<int>(tag.size()),
                const_cast<char*>(tag.data())) != 1)
            break;
        int finl = 0;
        if (EVP_DecryptFinal_ex(ctx,
                reinterpret_cast<unsigned char*>(pt.data()) + total, &finl) != 1)
            break;   // 认证失败
        total += finl;
        pt.resize(static_cast<size_t>(total));
        ok = true;
    } while (false);

    EVP_CIPHER_CTX_free(ctx);
    OPENSSL_cleanse(key.data(), key.size());
    if (!ok) return std::nullopt;
    return pt;
}

bool passphrase_active() {
    const auto& p = resolve_passphrase();
    return p.has_value() && !p->empty();
}

} // namespace agentty::auth::crypt
