// provider_keys_seal_test - 电话 凭据
// -homogenisation 保证: NO 提供者密钥( 主机预设, 自定义主机,
// 或无密钥的本地主机行) 可能像平话本一样停留在磁盘上。 settings.json
// 不得携带 凭据-shaped 字段; 密封的金库( $ provider-keys.json,
// crypt::seal 信封——与 credentials.json
// )是唯一一个在 -rest 和遗留的简单文字设置 正文 导入后,
// 下一页将剥离 保存.
//
// 孤立无援 credentials_test : 临时 $AGENTTY_HOME ,键盘已禁用。
#include "agtest.hpp"

#include "agentty/auth/cred_crypt.hpp"
#include "agentty/auth/keys.hpp"
#include "agentty/io/persistence.hpp"

#include <cstdlib>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <string>

using namespace agentty;
namespace fs = std::filesystem;

namespace {

struct TmpHome {
    fs::path    dir;
    std::string old_home;
    bool        had_home = false;
    TmpHome() {
        dir = fs::temp_directory_path()
            / ("agentty_keys_vault_test_" + std::to_string(::getpid()));
        fs::remove_all(dir);
        fs::create_directories(dir / "home");
        const char* h = ::getenv("AGENTTY_HOME");
        had_home = h != nullptr;
        if (had_home) old_home = h;
        ::setenv("AGENTTY_HOME", (dir / "home").c_str(), 1);
        // 模拟: 已启用 OS 钥匙托尔在外边拿着信封 沙
        // 箱 试验在试验中坚持 FILE 强迫它关闭。
        ::setenv("AGENTTY_USE_KEYSTORE", "0", 1);
    }
    ~TmpHome() {
        if (had_home) ::setenv("AGENTTY_HOME", old_home.c_str(), 1);
        else          ::unsetenv("AGENTTY_HOME");
        fs::remove_all(dir);
    }
    [[nodiscard]] fs::path settings_file() const {
        return dir / "home" / "settings.json";
    }
    [[nodiscard]] fs::path vault_file() const {
        return dir / "home" / "credentials" / "provider-keys.json";
    }
    static std::string slurp(const fs::path& p) {
        std::ifstream ifs(p, std::ios::binary);
        if (!ifs) return {};
        return {std::istreambuf_iterator<char>(ifs),
                std::istreambuf_iterator<char>()};
    }
};

} // 命名空间结束。

TEST_CASE("provider keys seal: settings.json carries no plaintext key") {
    TmpHome home;

    {
        store::Settings s;
        s.provider_keys["groq"] = "gsk-live-groq-secret-key-0000000000";
        s.provider_keys["https://chat.example.org/api"] = "sk-custom-host-key";
        s.provider_keys["localhost:8080"] = "";   // 无键本地主机行
        persistence::save_settings(::agentty::IoAccess::grant(), agtest::cap(), s);
    }

    // 设置 正文 一定是,我必须 凭据-free - 连空的都没有
    // "provider_keys" 对象,
    // 所以粘贴的bug报告/备份中没有任何内容可以攻击.
    const std::string body = TmpHome::slurp(home.settings_file());
    REQUIRE(!body.empty());
    CHECK(body.find("provider_keys") == std::string::npos);
    CHECK(body.find("gsk-live-groq-secret-key-0000000000") == std::string::npos);
    CHECK(body.find("sk-custom-host-key") == std::string::npos);

    // 保险库存在,是被封存的-bound机,没有平装文字.
    const std::string vault = TmpHome::slurp(home.vault_file());
    REQUIRE(!vault.empty());
    CHECK(auth::crypt::looks_sealed(vault));
    CHECK(vault.find("gsk-live-groq-secret-key-0000000000") == std::string::npos);
    CHECK(auth::crypt::unseal(vault).has_value());

    // 圆 -trip : 地图回来了 字节-identical ,包含无键行。
    auto loaded = persistence::load_settings(::agentty::IoAccess::grant());
    CHECK(loaded.provider_keys.size() == 3);
    CHECK(loaded.provider_keys.at("groq")
          == "gsk-live-groq-secret-key-0000000000");
    CHECK(loaded.provider_keys.at("https://chat.example.org/api")
          == "sk-custom-host-key");
    CHECK(loaded.provider_keys.at("localhost:8080") == "");
}

TEST_CASE("provider keys seal: legacy plaintext settings are migrated + stripped") {
    TmpHome home;

    // 模拟一个 -vault 预安装: settings.json 中的 plaintext keys,
    // 没有磁盘中的金库。
    {
        std::ofstream ofs(home.settings_file(), std::ios::binary | std::ios::trunc);
        ofs << R"({"model_id":"claude-opus-4-5","provider":"groq",)"
            << R"("provider_keys":{"groq":"gsk-legacy-plaintext-key"})"
            << "}";
    }
    CHECK(!fs::exists(home.vault_file()));

    // 第一届 加载 将遗留密钥导入到。 -memory 地图
    // 和 把他们封在保险库里 RIGHT THERE 整个
    // 封印点 加载 否为 保存 需要运行到简洁文本不再是唯一的复制。
    // ( 状态更新函数 将缓存设置按分钟;
    // 进程在下一个程序之前退出 保存 不得将钥匙解封。 )
    auto s = persistence::load_settings(::agentty::IoAccess::grant());
    CHECK(s.provider_keys.size() == 1);
    CHECK(s.provider_keys.at("groq") == "gsk-legacy-plaintext-key");
    REQUIRE(fs::exists(home.vault_file()));
    CHECK(auth::crypt::unseal(TmpHome::slurp(home.vault_file())).has_value());

    // ...还有第一个 保存 重新封存它们( 幂等 现在),并从中去掉简单
    // 字形对象 settings.json 完全没有。
    persistence::save_settings(::agentty::IoAccess::grant(), agtest::cap(), s);

    const std::string body = TmpHome::slurp(home.settings_file());
    CHECK(body.find("provider_keys") == std::string::npos);
    CHECK(body.find("gsk-legacy-plaintext-key") == std::string::npos);

    const std::string vault = TmpHome::slurp(home.vault_file());
    REQUIRE(!vault.empty());
    CHECK(auth::crypt::looks_sealed(vault));
    CHECK(auth::crypt::unseal(vault).has_value());
    CHECK(vault.find("gsk-legacy-plaintext-key") == std::string::npos);

    auto after = persistence::load_settings(::agentty::IoAccess::grant());
    CHECK(after.provider_keys.size() == 1);
    CHECK(after.provider_keys.at("groq") == "gsk-legacy-plaintext-key");
}

TEST_CASE("provider keys seal: sign-out clears the vault at rest") {
    TmpHome home;

    {
        store::Settings s;
        s.provider_keys["groq"] = "gsk-live-groq-secret-key-0000000000";
        persistence::save_settings(::agentty::IoAccess::grant(), agtest::cap(), s);
    }
    REQUIRE(fs::exists(home.vault_file()));

    // 空地图 已保存 具有权威性——一种标志 -out (或 -app
    // key_clear ) 不得留有任何可收回的钥匙。
    store::Settings s;
    persistence::save_settings(::agentty::IoAccess::grant(), agtest::cap(), s);

    CHECK(!fs::exists(home.vault_file()));
    auto loaded = persistence::load_settings(::agentty::IoAccess::grant());
    CHECK(loaded.provider_keys.empty());
}
