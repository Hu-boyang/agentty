// oauth_proactive_refresh_test - 主动性强 -refresh 探测器的决定逻辑。
//
// auth::oauth_proactive_refresh_token ()读读
// -disk 凭据 决定是否将A交还给 refresh_token 所以,我们 状态更新函数
// 能够预览 -refresh 很快的 很快的 {\3cH202020}A
// soon -to-lapse OAuth 证书 之前 下一个请求是401(第一次)
// -message-lag 修补到套接字预热的兄弟姐妹。 保存 探索圆形 -trip 并声明
// 窗口边界加负数( 无过期 / 无) refresh_token / API ) , 键盘).
//
// A doctest 合并案件 二进制程序 (链接 共享对象集,
// 所以 auth.cpp 免费来。它指 XDG_CONFIG_HOME 在一个
// 独特的临时目录中,并禁用密钥托尔 save/load 直接转到密封文件
#include <doctest/doctest.h>

#include "agentty/auth/auth.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace {

namespace auth = agentty::auth;
namespace fs   = std::filesystem;

std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

void save_oauth(std::int64_t expires_at_ms, const std::string& refresh) {
    auth::cred::OAuth o;
    o.access_token  = "at-xyz";
    o.refresh_token = refresh;
    o.expires_at_ms = expires_at_ms;
    (void)auth::save_credentials(::agentty::IoAccess::grant(), auth::Credentials{std::move(o)});
}

void isolate_config_dir() {
    static bool done = false;
    if (done) return;
    done = true;
    auto dir = fs::temp_directory_path() /
               ("agentty_proactive_" + std::to_string(now_ms()));
    fs::create_directories(dir);
#if defined(_WIN32)
    _putenv_s("AGENTTY_HOME", dir.string().c_str());
    _putenv_s("AGENTTY_USE_KEYSTORE", "0");
#else
    ::setenv("AGENTTY_HOME", dir.string().c_str(), 1);
    ::setenv("AGENTTY_USE_KEYSTORE", "0", 1);
#endif
}

} // 命名空间结束。

TEST_CASE("oauth proactive refresh: window + negative cases") {
    isolate_config_dir();
    const std::int64_t t = now_ms();

    SUBCASE("fresh token (30m out) → no proactive refresh") {
        save_oauth(t + 30 * 60 * 1000, "rt-fresh");
        CHECK(!auth::oauth_proactive_refresh_token(::agentty::IoAccess::grant()).has_value());
    }

    SUBCASE("near-expiry token (2m out) → returns the refresh_token") {
        save_oauth(t + 2 * 60 * 1000, "rt-soon");
        auto tok = auth::oauth_proactive_refresh_token(::agentty::IoAccess::grant());
        REQUIRE(tok.has_value());
        CHECK(*tok == "rt-soon");
    }

    SUBCASE("already-expired token → still returns the refresh_token") {
        save_oauth(t - 60 * 1000, "rt-expired");
        auto tok = auth::oauth_proactive_refresh_token(::agentty::IoAccess::grant());
        REQUIRE(tok.has_value());
        CHECK(*tok == "rt-expired");
    }

    SUBCASE("near-expiry but empty refresh_token → nothing to refresh with") {
        save_oauth(t + 60 * 1000, "");
        CHECK(!auth::oauth_proactive_refresh_token(::agentty::IoAccess::grant()).has_value());
    }

    SUBCASE("no expiry info (expires_at_ms == 0) → skip") {
        save_oauth(0, "rt-noexp");
        CHECK(!auth::oauth_proactive_refresh_token(::agentty::IoAccess::grant()).has_value());
    }

    SUBCASE("custom window boundaries") {
        save_oauth(t + 4 * 60 * 1000, "rt-window");
        // 4分钟后在1-min窗口下新鲜...
        CHECK(!auth::oauth_proactive_refresh_token(::agentty::IoAccess::grant(), 60 * 1000).has_value());
        // . 不过... 过期 10岁以下 -min 窗口中选择一个窗口。
        auto tok = auth::oauth_proactive_refresh_token(::agentty::IoAccess::grant(), 10 * 60 * 1000);
        REQUIRE(tok.has_value());
        CHECK(*tok == "rt-window");
    }

    SUBCASE("API-key credential is not refreshable") {
        (void)auth::save_credentials(::agentty::IoAccess::grant(), 
            auth::Credentials{auth::cred::ApiKey{"sk-key"}});
        CHECK(!auth::oauth_proactive_refresh_token(::agentty::IoAccess::grant()).has_value());
    }
}
