// keystore_test — OS 密钥托尔圆形 -trip.
//
// 键盘是选择 -in (AGENTTY_USE_KEYSTORE )和后
// 端(后端) -dependent (库机密/) macOS 安保/安保/安保
// WinCred )),当没有后端时——无头的常见情况. CI ——考证.
// SKIPS 而不是失败 所以它永远不会阻碍一个建筑 缺少特勤/键链。
// 来练习圆圈 -trip 运行 AGENTTY_USE_KEYSTORE =1
// 在一个有未解锁的密钥环的主机上.
#include "agentty/auth/keystore.hpp"

#include <cstdio>
#include <string>

namespace ks = agentty::auth::keystore;

static int g_fail = 0;
#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_fail; }           \
        else         { std::printf("  ok:   %s\n", msg); }                     \
    } while (0)

int main() {
    std::printf("keystore backend: %s (available=%d)\n",
                ks::backend_name().c_str(), (int)ks::available());

    if (!ks::available()) {
        std::printf("SKIP: no keystore backend enabled/available "
                    "(set AGENTTY_USE_KEYSTORE=1 on a host with a keyring)\n");
        return 0;  // 优雅的跳过
    }

    const std::string key = "agentty-test-item";
    const std::string val = "sealed-{v:2,kdf:argon2id,blob:...}";

    // 清除上次中止运行中留下的任何剩余。
    ks::remove(key);

    CHECK(ks::store(key, val) == ks::Status::Ok, "store returns Ok");

    std::string got;
    CHECK(ks::retrieve(key, got) == ks::Status::Ok, "retrieve returns Ok");
    CHECK(got == val, "retrieved value matches stored");

    CHECK(ks::remove(key) == ks::Status::Ok, "remove returns Ok");

    std::string gone;
    CHECK(ks::retrieve(key, gone) == ks::Status::NotFound,
          "retrieve after remove returns NotFound");

    if (g_fail == 0) { std::printf("\nKEYSTORE ROUND-TRIP PASSED\n"); return 0; }
    std::printf("\n%d KEYSTORE CHECK(S) FAILED\n", g_fail);
    return 1;
}
