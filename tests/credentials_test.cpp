// credentials_test ——中央提供者. 凭据 层必须解决
// RIGHT 源码 : 过期 活动 AuthHeader (Anthropic
// oAuth 的代号)被发送给了另一个提供商(Mistral)
// ~ HTTP 401.
//
// 隔绝通过 XDG_CONFIG_HOME + 临时设置目录, 这样它不会触碰真正的配置;
// 密钥托尔被禁用如此。 provider_keys 圆形 -trip 穿过封口
// provider-keys.json 保险库
#include "agtest.hpp"

#include "agentty/provider/credentials.hpp"
#include "agentty/provider/registry.hpp"   // provider_for_api_key, providers()
#include "agentty/auth/auth.hpp"
#include "agentty/io/persistence.hpp"

#include <cstdlib>
#include <unistd.h>
#include <filesystem>
#include <string>

using namespace agentty;
namespace cred = agentty::provider::credentials;
namespace fs = std::filesystem;

namespace {

struct TmpHome {
    fs::path    dir;
    std::string old_home;
    std::string old_agentty;
    bool        had_home    = false;
    bool        had_agentty = false;
    TmpHome() {
        dir = fs::temp_directory_path()
            / ("agentty_cred_test_" + std::to_string(::getpid()));
        fs::remove_all(dir);
        fs::create_directories(dir);
        // COPY 以前的值:getenv()返回一个指针INTO
        // 环境块,setenv()可能重新分配——从过期的指针
        // 恢复到这个共享的二进制程序的每次以后的测试中,
        // 其内涵都损坏(并且可能留下AGENTTY_HOME的未设置,
        // 用户-root的绊线随后中止).
        if (const char* h = ::getenv("HOME"))          { old_home = h; had_home = true; }
        if (const char* a = ::getenv("AGENTTY_HOME"))  { old_agentty = a; had_agentty = true; }
        ::setenv("HOME", dir.c_str(), 1);
        ::setenv("AGENTTY_HOME", dir.c_str(), 1);
    }
    ~TmpHome() {
        if (had_home) ::setenv("HOME", old_home.c_str(), 1);
        else          ::unsetenv("HOME");
        if (had_agentty) ::setenv("AGENTTY_HOME", old_agentty.c_str(), 1);
        else             ::unsetenv("AGENTTY_HOME");
        fs::remove_all(dir);
    }
};

void set_key(const std::string& provider, const std::string& key) {
    auto s = persistence::load_settings(::agentty::IoAccess::grant());
    s.provider_keys[provider] = key;
    persistence::save_settings(::agentty::IoAccess::grant(), agtest::cap(), s);
}

} // 命名空间结束。

TEST_CASE("credentials::resolve returns the target provider's key") {
    TmpHome home;
    // 保存 DISTINCT 用于两个主机提供者的密钥。
    set_key("mistral",  "MISTRAL-KEY-32chars-xxxxxxxxDdmj");
    set_key("cerebras", "CEREBRAS-KEY-yyyyyyyyyyyyyyyyyyyy");

    // 解析误差必须返回误差键, 不 脑或其它任何问题。
    // (这正是被违反的401 错误的保证。 )
    CHECK(auth::bearer_token(cred::resolve(::agentty::IoAccess::grant(), "mistral"))
          == "MISTRAL-KEY-32chars-xxxxxxxxDdmj");
    CHECK(auth::bearer_token(cred::resolve(::agentty::IoAccess::grant(), "cerebras"))
          == "CEREBRAS-KEY-yyyyyyyyyyyyyyyyyyyy");
}

TEST_CASE("credentials: local providers need no login, hosted keys do") {
    TmpHome home;
    // 一个没有已保存密钥+没有env var的托管供应商需要登录.
    CHECK(cred::needs_login(::agentty::IoAccess::grant(), "mistral"));
    set_key("mistral", "sk-abc");
    CHECK(!cred::needs_login(::agentty::IoAccess::grant(), "mistral"));

    // 本地服务器( ollama) 是无键的—— 不需要登录。
    CHECK(!cred::needs_login(::agentty::IoAccess::grant(), "ollama"));
    CHECK(cred::add_method("ollama") == cred::AddMethod::None);

    // 所主控密钥提供者使用 API 键; Anthropic 带奥奥特去
    CHECK(cred::add_method("mistral") == cred::AddMethod::ApiKey);
    CHECK(cred::add_method("anthropic") == cred::AddMethod::OAuthDevice);
}

TEST_CASE("credentials::add_key preserves the prior account (no clobber)") {
    TmpHome home;
    set_key("mistral", "key-AAAA");
    // 加上另一个键快照,旧键首先作为账户.
    cred::add_key(::agentty::IoAccess::grant(), agtest::cap(), "mistral", "key-BBBB");
    CHECK(auth::bearer_token(cred::resolve(::agentty::IoAccess::grant(), "mistral")) == "key-BBBB");
    CHECK(cred::list(::agentty::IoAccess::grant(), "mistral").size() >= 1);   // 保存了上一个密钥
}

TEST_CASE("a pasted key names its own provider") {
    // 第68期: `agentty login` 和第一个 -run 弹出面板
    // 两者都行 "an API key (Anthropic sk-ant-..., or any provider)"
    // 然后写 每个 键 Anthropic 格鲁克的钥匙被接受,
    // 已保存 ,报告为成功,并在第一个回合以认证错误命名
    // 失败 Anthropic - 所以用户调试了 凭据 没错
    //
    // 前缀表格 位于 注册表 旁边的一行 auth_env
    // 新的供应商已经宣布了它的宿主和主厨 和另一个
    // 桌子在别处是另一个可以忘记的地方
    CHECK(provider::provider_for_api_key("gsk_abc123") == "groq");
    CHECK(provider::provider_for_api_key("xai-abc123") == "xai");
    CHECK(provider::provider_for_api_key("AIzaSyAbc123") == "gemini");
    CHECK(provider::provider_for_api_key("sk-ant-api03-abc") == "anthropic");
    CHECK(provider::provider_for_api_key("sk-ant-oat01-abc") == "anthropic");

    // 最长的比赛获胜,因此更具体的前缀从不会被同一
    // 排的松散者所阴影.
    CHECK(provider::provider_for_api_key("sk-or-v1-abc") == "openrouter");
    CHECK(provider::provider_for_api_key("sk-or-abc") == "openrouter");
}

TEST_CASE("an ambiguous key is NOT guessed at") {
    // 该 属性,使固定不成为不同的错误。 `sk-` 由 E.
    // 共享 OpenAI, DeepSeek 和任何自定义的终点,
    // 所以它识别无 - 而错误的自信答案比无更糟糕,
    // 因为用户然后调试 a 凭据 他们认为,对于他们从未选择的供应商来说,
    // 是正确的。 "ask" ; 呼叫者保持自己的默认值.
    CHECK(provider::provider_for_api_key("sk-proj-abc123").empty());
    CHECK(provider::provider_for_api_key("sk-abc123").empty());
    CHECK(provider::provider_for_api_key("ollama-local").empty());
    CHECK(provider::provider_for_api_key("").empty());
    // 比任何前缀短:不得读出界限.
    CHECK(provider::provider_for_api_key("s").empty());
    CHECK(provider::provider_for_api_key("gsk").empty());
}

TEST_CASE("every declared key prefix resolves to the row that declared it") {
    // 走路时 注册表 而不是重述,所以添加了前缀的提供者会
    // 被覆盖而不触及此测试。 编译-time
    // key_prefixes_unambiguous
    // ()已经拒绝两行重复前缀;这一检查与表格一致。
    for (const auto& p : provider::providers()) {
        for (std::string_view pre : p.key_prefixes) {
            if (pre.empty()) continue;
            const std::string probe = std::string{pre} + "ZZZ000";
            INFO("prefix " << pre << " declared by " << p.id);
            CHECK(provider::provider_for_api_key(probe) == p.id);
        }
    }
}
