#include "agentty/auth/keystore.hpp"

#include "agentty/tool/util/subprocess.hpp"

#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#if !defined(_WIN32)
#  include "agentty/tool/util/exec.hpp"   // run_child
#else
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <wincred.h>
#  ifdef _MSC_VER
#    pragma comment(lib, "advapi32.lib")
#  endif
#endif

namespace agentty::auth::keystore {

namespace {

using tools::util::run_argv_s;
using tools::util::SubprocessResult;

// 特勤局项目由稳定的(属性→值)对来标识。
// 我们使用单个 "service" 属性，因此 lookups/deletes 与
// 商店写入的内容完全匹配。标签是装饰性的（钥匙圈 UI 显示的内容）。
constexpr const char* kAttrKey   = "service";
constexpr const char* kLabel     = "agentty credentials";

// ── 真理洞门 ────────────────────────────────────────────────────
bool env_enabled() {
    const char* v = std::getenv("AGENTTY_USE_KEYSTORE");
    return v && *v && std::strcmp(v, "0") != 0 && std::strcmp(v, "false") != 0;
}

// 我们可以调用`<exe> <probe-arg>`吗？
// 用于检测秘密-tool/安全存在，无需PATH行走。退出状态无关紧要——干净的
// 生成（started==true）意味着 二进制程序 存在。
// [[maybe_unused]]：Windows后端无条件是WinCred
// 并且从不探测外部二进制程序，所以这是死的。
[[maybe_unused]] bool can_invoke(const std::vector<std::string>& argv) {
    auto r = run_argv_s(argv, 4096, std::chrono::seconds{5});
    return r.started;
}

enum class Backend { None, LibSecret, MacKeychain, WinCred };

Backend detect_backend() {
#if defined(__APPLE__)
    if (can_invoke({"security", "help"})) return Backend::MacKeychain;
    return Backend::None;
#elif defined(__linux__)
    // `secret-tool --help`返回非-zero，但当存在时干净地产生。
    if (can_invoke({"secret-tool", "--help"})) return Backend::LibSecret;
    return Backend::None;
#elif defined(_WIN32)
    // Windows 凭据管理器始终存在于受支持的Windows上。
    return Backend::WinCred;
#else
    return Backend::None;
#endif
}

Backend backend() {
    static const Backend b = detect_backend();
    return b;
}

#if !defined(_WIN32)
// 生成 argv，将 `input` 写入其标准输入，丢弃 stdout/stderr，
// 并返回退出代码（或在生成失败时返回 -1）。用于 `secret-tool store`，
// 它从 stdin 读取秘密 — 因此秘密永远不会像 `-w <secret>`
// 标志 那样出现在进程表/argv 中。最佳-effort，短-lived。
int spawn_feed_stdin(const std::vector<std::string>& argv, const std::string& input) {
    // run_child将子进程置于其自己的会话中，因此它无法控制终端：macOS
    // `security -w`通过readpassphrase()读取，
    // 当存在时打开/dev/tty并且将忽略我们的管道（并挂起）。
    tools::util::ChildRun run;
    run.argv       = argv;
    run.stdin_data = input;
    run.idle       = std::chrono::seconds{10};
    run.wall       = std::chrono::seconds{30};
    run.max_output_bytes = 4096;   // 输出被丢弃
    const auto r = tools::util::run_child(run);
    return r.started && r.exited ? r.exit_code : -1;
}
#endif // !_WIN32

#if defined(_WIN32)
// 凭据目标名称显示在Windows 凭据管理器中。每个逻辑键一个项目，
// 命名空间，因此我们永远不会与其他应用程序发生冲突。
std::wstring win_target(const std::string& key) {
    std::string t = "agentty:" + key;
    int n = MultiByteToWideChar(CP_UTF8, 0, t.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, t.c_str(), -1, w.data(), n);
    return w;
}
#endif

} // 命名空间结束。

bool available() {
    static const bool ok = env_enabled() && backend() != Backend::None;
    return ok;
}

std::string backend_name() {
    if (!env_enabled()) return "disabled";
    switch (backend()) {
        case Backend::LibSecret:   return "libsecret";
        case Backend::MacKeychain: return "macos-keychain";
        case Backend::WinCred:     return "windows-credential-manager";
        default:                   return "unavailable";
    }
}

Status store(const std::string& key, const std::string& secret) {
    if (!available()) return Status::Unsupported;
    switch (backend()) {
#if !defined(_WIN32)
        case Backend::LibSecret: {
            // Secret-tool 存储 --label=<label> <attr> <key> (标准输入上的秘密)
            int rc = spawn_feed_stdin(
                {"secret-tool", "store", std::string("--label=") + kLabel,
                 kAttrKey, key},
                secret);
            return rc == 0 ? Status::Ok : Status::Error;
        }
        case Backend::MacKeychain: {
            // 首先替换任何现有项目（否则添加会因 dup 而失败），然后添加。
            // CRITICAL：传递 `-w` 和 NO 值，因此 `security`
            // 提示词 作为标准输入上的密码（"password data for new item:"
            // 然后 "retype password for new item:"），
            // 而不是从 argv 中获取它，它在 `ps`/进程表中可见。
            // 我们将秘密提供两次（换行-separated）以满足确认提示词。
            (void)run_argv_s({"security", "delete-generic-password",
                        "-s", key, "-a", "agentty"}, 4096, std::chrono::seconds{5});
            int rc = spawn_feed_stdin(
                {"security", "add-generic-password", "-U",
                 "-s", key, "-a", "agentty", "-l", kLabel, "-w"},
                secret + "\n" + secret + "\n");
            return rc == 0 ? Status::Ok : Status::Error;
        }
#else
        case Backend::WinCred: {
            // Windows 凭据经理：每-user、DPAPI-protected通用凭据。
            // CRED_PERSIST_LOCAL_MACHINE 仅将其保留在这个盒子上。
            std::wstring target = win_target(key);
            CREDENTIALW cred{};
            cred.Type               = CRED_TYPE_GENERIC;
            cred.TargetName         = const_cast<LPWSTR>(target.c_str());
            cred.CredentialBlobSize = static_cast<DWORD>(secret.size());
            cred.CredentialBlob     = reinterpret_cast<LPBYTE>(
                const_cast<char*>(secret.data()));
            cred.Persist            = CRED_PERSIST_LOCAL_MACHINE;
            std::wstring user = L"agentty";
            cred.UserName           = const_cast<LPWSTR>(user.c_str());
            return CredWriteW(&cred, 0) ? Status::Ok : Status::Error;
        }
#endif
        default: return Status::Unsupported;
    }
}

Status retrieve(const std::string& key, std::string& out) {
    if (!available()) return Status::Unsupported;
    switch (backend()) {
#if !defined(_WIN32)
        case Backend::LibSecret: {
            auto r = run_argv_s({"secret-tool", "lookup", kAttrKey, key},
                                1u << 20, std::chrono::seconds{10});
            if (!r.started) return Status::Error;
            if (r.exit_code != 0 || r.output.empty()) return Status::NotFound;
            // Secret-tool 打印秘密，不带尾随换行符；
            // 但防御性地剥离一个，以防平台添加它。
            out = r.output;
            if (!out.empty() && out.back() == '\n') out.pop_back();
            return Status::Ok;
        }
        case Backend::MacKeychain: {
            auto r = run_argv_s(
                {"security", "find-generic-password", "-s", key,
                 "-a", "agentty", "-w"},
                1u << 20, std::chrono::seconds{10});
            if (!r.started) return Status::Error;
            if (r.exit_code != 0 || r.output.empty()) return Status::NotFound;
            out = r.output;
            if (!out.empty() && out.back() == '\n') out.pop_back();
            return Status::Ok;
        }
#else
        case Backend::WinCred: {
            std::wstring target = win_target(key);
            PCREDENTIALW cred = nullptr;
            if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &cred)) {
                DWORD e = GetLastError();
                return (e == ERROR_NOT_FOUND) ? Status::NotFound : Status::Error;
            }
            out.assign(reinterpret_cast<const char*>(cred->CredentialBlob),
                       cred->CredentialBlobSize);
            CredFree(cred);
            return Status::Ok;
        }
#endif
        default: return Status::Unsupported;
    }
}

Status remove(const std::string& key) {
    if (!available()) return Status::Unsupported;
    switch (backend()) {
#if !defined(_WIN32)
        case Backend::LibSecret: {
            auto r = run_argv_s({"secret-tool", "clear", kAttrKey, key},
                                4096, std::chrono::seconds{5});
            return r.started ? Status::Ok : Status::Error;
        }
        case Backend::MacKeychain: {
            auto r = run_argv_s({"security", "delete-generic-password",
                                 "-s", key, "-a", "agentty"},
                                4096, std::chrono::seconds{5});
            // 退出44==未找到项目；视为NotFound，否则好的。
            if (r.started && r.exit_code == 44) return Status::NotFound;
            return r.started ? Status::Ok : Status::Error;
        }
#else
        case Backend::WinCred: {
            std::wstring target = win_target(key);
            if (!CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0)) {
                DWORD e = GetLastError();
                return (e == ERROR_NOT_FOUND) ? Status::NotFound : Status::Error;
            }
            return Status::Ok;
        }
#endif
        default: return Status::Unsupported;
    }
}

} // namespace agentty::auth::keystore
