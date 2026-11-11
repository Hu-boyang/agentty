// cred_crypt_test 截止 -rest 凭据 封存圆形 -trips.
//
// 取决于过程的每个方面 -global 密码句 缓存 (
// resolve_passphrase 谓 call_once )运行在自己的叉子,
// 所以一个单一的测试 二进制程序 能够行使无 -passphrase (v1)
// 和密码句(v2)独立表示。 -fork 平台中,密码句大小写被跳过。
#include "agentty/auth/cred_crypt.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

#if !defined(_WIN32)
#  include <sys/wait.h>
#  include <unistd.h>
#  define HAVE_FORK 1
#else
#  define HAVE_FORK 0
#endif

namespace cc = ::agentty::auth::crypt;

static int g_fail = 0;
#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_fail; }           \
        else         { std::printf("  ok:   %s\n", msg); }                     \
    } while (0)

static const std::string kSecret = R"({"method":"oauth","access_token":"sk-abc"})";

// ── 案件正文( 每一个运行时都有固定的密码句 env 状态) ────────────

static int case_v1_machine_only() {
    std::printf("[v1 machine-only]\n");
    CHECK(!cc::passphrase_active(), "passphrase_active() is false");
    auto env = cc::seal(kSecret);
    CHECK(env.has_value(), "seal succeeds");
    CHECK(cc::looks_sealed(*env), "looks_sealed() true");
    CHECK(env->find("\"v\":1") != std::string::npos, "envelope is v1");
    CHECK(env->find("kdf") == std::string::npos, "no kdf field");
    auto pt = cc::unseal(*env);
    CHECK(pt.has_value() && *pt == kSecret, "unseal round-trips");
    // Tamper检测:在密码区翻转一个字节.
    std::string bad = *env;
    auto ctpos = bad.find("\"ct\":\"");
    if (ctpos != std::string::npos) { bad[ctpos + 6] = (bad[ctpos + 6] == 'A' ? 'B' : 'A'); }
    CHECK(!cc::unseal(bad).has_value(), "tampered ciphertext rejected");
    // 遗产平原文字封存NOT.
    CHECK(!cc::looks_sealed(kSecret), "plaintext not mistaken for sealed");
    return g_fail;
}

static int case_v2_passphrase() {
    std::printf("[v2 passphrase]\n");
    CHECK(cc::passphrase_active(), "passphrase_active() is true");
    auto env = cc::seal(kSecret);
    CHECK(env.has_value(), "seal succeeds");
    CHECK(env->find("\"v\":2") != std::string::npos, "envelope is v2");
    const bool argon = env->find("\"kdf\":\"argon2id\"") != std::string::npos;
    const bool scrypt = env->find("\"kdf\":\"scrypt\"") != std::string::npos;
    CHECK(argon || scrypt, "kdf is argon2id or scrypt");
    std::printf("       (kdf = %s)\n", argon ? "argon2id" : "scrypt");
    auto pt = cc::unseal(*env);
    CHECK(pt.has_value() && *pt == kSecret, "unseal (same pass) round-trips");
    // 将信封放出,以便父母可以测试cross-state读取.
    if (const char* out = std::getenv("CRED_TEST_OUT")) {
        FILE* f = std::fopen(out, "w");
        if (f) { std::fwrite(env->data(), 1, env->size(), f); std::fclose(f); }
    }
    return g_fail;
}

static int case_v2_read_without_pass(const char* path) {
    std::printf("[v2 read WITHOUT passphrase]\n");
    FILE* f = std::fopen(path, "r");
    if (!f) { std::printf("  FAIL: missing env file\n"); return 1; }
    std::string s; int c; while ((c = std::fgetc(f)) != EOF) s.push_back((char)c);
    std::fclose(f);
    CHECK(!cc::passphrase_active(), "no passphrase in this process");
    CHECK(!cc::unseal(s).has_value(), "v2 file refuses without passphrase");
    return g_fail;
}

static int case_v2_read_wrong_pass(const char* path) {
    std::printf("[v2 read WRONG passphrase]\n");
    FILE* f = std::fopen(path, "r");
    if (!f) { std::printf("  FAIL: missing env file\n"); return 1; }
    std::string s; int c; while ((c = std::fgetc(f)) != EOF) s.push_back((char)c);
    std::fclose(f);
    CHECK(!cc::unseal(s).has_value(), "v2 file with wrong passphrase refuses");
    return g_fail;
}

static int case_v2_read_correct_pass(const char* path) {
    std::printf("[v2 read CORRECT passphrase]\n");
    FILE* f = std::fopen(path, "r");
    if (!f) { std::printf("  FAIL: missing env file\n"); return 1; }
    std::string s; int c; while ((c = std::fgetc(f)) != EOF) s.push_back((char)c);
    std::fclose(f);
    auto pt = cc::unseal(s);
    CHECK(pt.has_value() && *pt == kSecret, "file reopens with correct passphrase");
    return g_fail;
}

#if HAVE_FORK
// 在先应用给定的env的子运行`fn`,返回它的失败计数.
static int run_child(void (*setup)(), int (*fn)()) {
    pid_t pid = fork();
    if (pid == 0) {
        setup();
        int rc = fn();
        std::fflush(stdout);
        _exit(rc == 0 ? 0 : 1);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {}
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : 1;
}
static int run_child_arg(void (*setup)(), int (*fn)(const char*), const char* arg) {
    pid_t pid = fork();
    if (pid == 0) {
        setup();
        int rc = fn(arg);
        std::fflush(stdout);
        _exit(rc == 0 ? 0 : 1);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {}
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : 1;
}

static const char* kEnvFile = nullptr;
static void setup_none()  { unsetenv("AGENTTY_PASSPHRASE"); unsetenv("AGENTTY_ENCRYPT_PASSPHRASE"); }
static void setup_pass()  { setup_none(); setenv("AGENTTY_PASSPHRASE", "correct horse battery staple", 1); setenv("CRED_TEST_OUT", kEnvFile, 1); }
static void setup_wrong() { setup_none(); setenv("AGENTTY_PASSPHRASE", "totally wrong guess", 1); }
static void setup_scrypt(){ setup_pass(); setenv("AGENTTY_KDF", "scrypt", 1); }
#endif

int main() {
#if HAVE_FORK
    // 释放的 v2 信封的时序文件, 由孩子们共享。
    char tmpl[] = "/tmp/cred_test_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd >= 0) close(fd);
    kEnvFile = tmpl;

    int fails = 0;
    fails += run_child(setup_none,   case_v1_machine_only);
    fails += run_child(setup_pass,   case_v2_passphrase);   // 写入 kEnvFile (argon2id)
    fails += run_child_arg(setup_none,  case_v2_read_without_pass, kEnvFile);
    fails += run_child_arg(setup_wrong, case_v2_read_wrong_pass,   kEnvFile);
    fails += run_child_arg(setup_pass,  case_v2_read_correct_pass, kEnvFile);
    // 强迫 -scrypt 封号+ 读 -back 在一个新生的孩子,然后重新打开它。
    fails += run_child(setup_scrypt,    case_v2_passphrase);   // 覆盖 kEnvFile( 扭曲)
    fails += run_child_arg(setup_pass,  case_v2_read_correct_pass, kEnvFile);

    unlink(kEnvFile);
    if (fails == 0) { std::printf("\nALL CRED-CRYPT CASES PASSED\n"); return 0; }
    std::printf("\n%d CRED-CRYPT CASE(S) FAILED\n", fails);
    return 1;
#else
    // 无叉:只练习机器 -only 路径在 -process.
    int f = case_v1_machine_only();
    if (f == 0) { std::printf("\nMACHINE-ONLY CASE PASSED (fork unavailable)\n"); return 0; }
    return 1;
#endif
}
