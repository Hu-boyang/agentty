// 搜索服务:计划 agentty 手执 mcp-cpp ,
// 在付费服务背后的密钥存储,以及选择它们的网络搜索行.
//
// 为何每个针都值得针头:
//   * 报告内容 注册表 服务 ID 列表 IS mcp-cpp 目录,两份清单
//     同样的事情在漂移, 这样做时, 建筑失败了。
//   * 计划是仅从环境(没有IO)中制定出来的,因此,关于它的每一条规则——
//     顺序、重复、未知的ID、缺失的密钥——是一个简单的断言。
//   * 键从未到达 settings.json 或 时间 Model。它被密封起来,
//     仅显示为"stored"。
//   *拨号及按键出现在使用它们的插槽下,并免费服务
//     添加不包含密钥行。

#include "agtest_cap.hpp"
#include "agtest.hpp"

#include "agentty/auth/keystore.hpp"
#include "agentty/io/persistence.hpp"
#include "agentty/runtime/panel/web_search_form.hpp"
#include "agentty/runtime/settings_registry.hpp"
#include "agentty/tool/web_search_plan.hpp"
#include "agentty/tool/web_search_policy.hpp"
#include "agentty/tool/web_search_secret.hpp"

#include <mcp/tools/web_search.hpp>

#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

using namespace agentty;
namespace sc  = agentty::web_search_cfg;
namespace reg = agentty::settings::registry;
namespace wtf = agentty::web_search_form;
namespace sp  = agentty::tools::web_search_plan;
namespace ss  = agentty::tools::web_search_secret;
namespace fs  = std::filesystem;

// 电话 面板 并且没有有效覆盖内脏。
const reg::EnvSnapshot kNoEnv{};

namespace {

// 私人事业 AGENTTY_HOME 与 OS 钥匙被扣掉,
// 所以封上 FILE 所说的话是读的,没有留下任何临时目录。
//
// "Off"有一个捕获量:auth::keystore::available
// ()缓存它对于过程寿命的答案,所以如果在早先的测试要求时已经设定
// AGENTTY_USE_KEYSTORE,设置到0在这里,则没有改变,
// store()会降落在开发商的REAL键盘中. `real_keyring`报告说,
// 以及写一个键跳过而不是触碰它的测试.
struct TmpHome {
    fs::path    dir;
    std::string old_home, old_keystore, old_exa;
    bool        had_home = false, had_ks = false, had_exa = false;
    TmpHome() {
        dir = fs::temp_directory_path()
            / ("agentty_web_search_secret_test_" + std::to_string(::getpid()));
        fs::remove_all(dir);
        fs::create_directories(dir / "home");
        if (const char* h = ::getenv("AGENTTY_HOME")) { had_home = true; old_home = h; }
        if (const char* k = ::getenv("AGENTTY_USE_KEYSTORE")) { had_ks = true; old_keystore = k; }
        if (const char* e = ::getenv("AGENTTY_WEB_SEARCH_KEY_EXA_API")) { had_exa = true; old_exa = e; }
        ::setenv("AGENTTY_HOME", (dir / "home").c_str(), 1);
        ::setenv("AGENTTY_USE_KEYSTORE", "0", 1);
        ::unsetenv("AGENTTY_WEB_SEARCH_KEY_EXA_API");
        real_keyring = agentty::auth::keystore::available();
    }
    bool real_keyring = false;
    ~TmpHome() {
        if (had_home) ::setenv("AGENTTY_HOME", old_home.c_str(), 1);
        else          ::unsetenv("AGENTTY_HOME");
        if (had_ks) ::setenv("AGENTTY_USE_KEYSTORE", old_keystore.c_str(), 1);
        else        ::unsetenv("AGENTTY_USE_KEYSTORE");
        if (had_exa) ::setenv("AGENTTY_WEB_SEARCH_KEY_EXA_API", old_exa.c_str(), 1);
        else         ::unsetenv("AGENTTY_WEB_SEARCH_KEY_EXA_API");
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    static std::string slurp(const fs::path& p) {
        std::ifstream in(p, std::ios::binary);
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }
    [[nodiscard]] std::string all_files_text() const {
        std::string out;
        std::error_code ec;
        for (const auto& e : fs::recursive_directory_iterator(dir, ec))
            if (e.is_regular_file(ec)) out += slurp(e.path());
        return out;
    }
};

std::vector<std::string> split_bar(std::string_view s) {
    std::vector<std::string> out;
    while (!s.empty()) {
        const auto bar = s.find('|');
        out.emplace_back(s.substr(0, bar));
        if (bar == std::string_view::npos) break;
        s.remove_prefix(bar + 1);
    }
    return out;
}

std::vector<std::string> field_ids(const form::Form& f) {
    std::vector<std::string> ids;
    for (const auto& r : f.fields) ids.push_back(r.id);
    return ids;
}

bool has(const std::vector<std::string>& v, std::string_view x) {
    for (const auto& s : v) if (s == x) return true;
    return false;
}

const std::string kNoKey;
const sp::KeyLookup no_keys = [](std::string_view) { return std::string{}; };

} // 命名空间结束。

// ── 两种服务清单不能有异议 ────────────────────────────

TEST_CASE("web search services: the registry's ids are mcp-cpp's catalogue") {
    std::vector<std::string> cat;
    for (const auto& s : ::mcp::tools::web_search_services()) cat.push_back(s.id);

    // 初级:按菜单顺序排列的目录。
    CHECK(split_bar(sc::kServiceIds) == cat);

    // 中学和 回退: "none" ,那么完全一样。
    auto opt = split_bar(sc::kOptionalServiceIds);
    REQUIRE(!opt.empty());
    CHECK(opt.front() == "none");
    opt.erase(opt.begin());
    CHECK(opt == cat);
}

TEST_CASE("web search services: the shipped defaults are Brave, DuckDuckGo, Tavily - all free") {
    sc::Config c;
    CHECK(c.primary == "brave-free");
    CHECK(c.secondary == "ddg-free");
    CHECK(c.fallback == "tavily-free");
    for (const auto* id : {&c.primary, &c.secondary, &c.fallback}) {
        const auto* svc = ::mcp::tools::find_web_search_service(*id);
        REQUIRE(svc != nullptr);
        CHECK(!svc->needs_key);
    }
    // 两者等于mcp-cpp的默认计划,所以两者从不分歧.
    const auto dflt = ::mcp::tools::default_web_search_plan();
    REQUIRE(dflt.slots.size() == 3);
    CHECK(dflt.slots[0].service == c.primary);
    CHECK(dflt.slots[1].service == c.secondary);
    CHECK(dflt.slots[2].service == c.fallback);
}

TEST_CASE("web search services: the language list is valid ISO 639-1") {
    for (const auto& l : split_bar(sc::kLanguages)) {
        INFO(l);
        CHECK((l == "auto" || l.size() == 2));
    }
}

// ── build_plan ───────────────────────────────────────────────────────────

TEST_CASE("web search plan: default settings give the three free slots in order") {
    const auto p = sp::build_plan(sc::Config{}, no_keys);
    REQUIRE(p.slots.size() == 3);
    CHECK(p.slots[0].service == "brave-free");
    CHECK(p.slots[1].service == "ddg-free");
    CHECK(p.slots[2].service == "tavily-free");
    CHECK(p.language.empty());
}

TEST_CASE("web search plan: none, unknown ids and repeats are left out") {
    sc::Config c;
    c.primary = "exa-api"; c.secondary = "none"; c.fallback = "exa-api";
    auto p = sp::build_plan(c, [](std::string_view) { return std::string{"k-1234567890"}; });
    REQUIRE(p.slots.size() == 1);
    CHECK(p.slots[0].service == "exa-api");

    c.primary = "kagi-api";            // 服务,这个建筑不运送
    c.secondary = "tavily-free"; c.fallback = "none";
    p = sp::build_plan(c, no_keys);
    REQUIRE(p.slots.size() == 1);
    CHECK(p.slots[0].service == "tavily-free");

    // 无用: 键-less默认, 而不是无法回答的工具.
    c.primary = "bogus"; c.secondary = "none"; c.fallback = "none";
    p = sp::build_plan(c, no_keys);
    CHECK(p.slots.size() == 3);
}

TEST_CASE("web search plan: keys go to keyed services only, and a missing key stays visible") {
    sc::Config c;
    c.primary = "serper-api"; c.secondary = "tavily-free"; c.fallback = "none";
    std::vector<std::string> asked;
    const auto p = sp::build_plan(c, [&](std::string_view s) {
        asked.emplace_back(s);
        return std::string{"secret-key-123"};
    });
    REQUIRE(p.slots.size() == 2);
    CHECK(p.slots[0].api_key == "secret-key-123");
    CHECK(p.slots[1].api_key.empty());           // 免费服务没有密钥
    CHECK(asked == std::vector<std::string>{"serper-api"});   // 并且从未要求过

    // 没有存储密钥: STAYS 位,为空,因此报告可以表示.
    const auto q = sp::build_plan(c, no_keys);
    REQUIRE(q.slots.size() == 2);
    CHECK(q.slots[0].service == "serper-api");
    CHECK(q.slots[0].api_key.empty());
}

TEST_CASE("web search plan: dials follow their service, not their slot") {
    sc::Config c;
    c.primary = "firecrawl-free"; c.secondary = "tavily-free"; c.fallback = "none";
    c.dials["firecrawl-free.category"] = "developer";
    c.dials["tavily-free.topic"] = "news";
    c.dials["exa-api.type"] = "fast";             // 计划没有
    auto p = sp::build_plan(c, no_keys);
    REQUIRE(p.slots.size() == 2);
    REQUIRE(p.slots[0].dials.size() == 1);
    CHECK(p.slots[0].dials[0].first == "category");
    CHECK(p.slots[0].dials[0].second == "developer");
    REQUIRE(p.slots[1].dials.size() == 1);
    CHECK(p.slots[1].dials[0].first == "topic");

    // 拆掉命令会把服务移动,并一起拨号.
    std::swap(c.primary, c.secondary);
    p = sp::build_plan(c, no_keys);
    CHECK(p.slots[0].service == "tavily-free");
    CHECK(p.slots[0].dials[0].second == "news");
}

TEST_CASE("web search plan: language 'auto' sends nothing, a code is passed on") {
    sc::Config c;
    CHECK(sp::build_plan(c, no_keys).language.empty());
    c.language = "de";
    CHECK(sp::build_plan(c, no_keys).language == "de");
    c.language = "garbage";
    CHECK(sp::build_plan(c, no_keys).language.empty());
    // 两位 字节 不是一个语言: 仅低两 -case 信件发出。
    for (const char* bad : {"A&", "EN", "\n\n", "e1"}) {
        c.language = bad;
        CHECK(sp::build_plan(c, no_keys).language.empty(), bad);
    }
}

TEST_CASE("web search settings: a dial entry must look like <service>.<dial>") {
    CHECK(sc::dial_entry_ok("brave-free.freshness", "pw"));
    CHECK(!sc::dial_entry_ok("nodot", "pw"));
    CHECK(!sc::dial_entry_ok(".freshness", "pw"));
    CHECK(!sc::dial_entry_ok("brave-free.", "pw"));
    CHECK(!sc::dial_entry_ok("brave-free.freshness", ""));
    CHECK(!sc::dial_entry_ok("brave-free.freshness", "p\nw"));
    CHECK(!sc::dial_entry_ok(std::string(65, 'a') + ".x", "pw"));
    // 值值有空间,可拨出最长的免费-text拨号(一个Gogle URL),不再使用.
    const std::string url = "https://" + std::string(sc::kMaxDialValueBytes - 8, 'g');
    CHECK(sc::dial_entry_ok("brave-api.goggles", url));
    CHECK(!sc::dial_entry_ok("brave-api.goggles", url + "g"));
}

// ── 钥匙店 ────────────────────────────────────────────────────────

TEST_CASE("web search secret: a key round-trips, sealed, and is never in plaintext") {
    TmpHome home;
    if (home.real_keyring) { MESSAGE("skipped: OS keystore armed for this process"); return; }
    const std::string key = "exa-LIVE-0123456789abcdef";
    CHECK(!ss::has("exa-api"));
    CHECK(ss::origin("exa-api").empty());

    REQUIRE(ss::store(::agentty::IoAccess::grant(), agtest::cap(), "exa-api", key));
    CHECK(ss::load("exa-api") == key);
    CHECK(ss::has("exa-api"));
    CHECK(ss::origin("exa-api") == "stored");

    // 磁盘上, 家里任何地方, 都没有钥匙。
    CHECK(home.all_files_text().find("0123456789abcdef") == std::string::npos);
    // ...而另一支部队却看不到它
    CHECK(!ss::has("serper-api"));

    CHECK(ss::erase(::agentty::IoAccess::grant(), agtest::cap(), "exa-api"));
    CHECK(!ss::has("exa-api"));
}

TEST_CASE("web search secret: the environment beats the store and is named, not shown") {
    TmpHome home;
    if (home.real_keyring) { MESSAGE("skipped: OS keystore armed for this process"); return; }
    REQUIRE(ss::store(::agentty::IoAccess::grant(), agtest::cap(), "exa-api", "stored-key-0123456789"));
    ::setenv("AGENTTY_WEB_SEARCH_KEY_EXA_API", "env-key-9876543210", 1);
    CHECK(ss::load("exa-api") == "env-key-9876543210");
    CHECK(ss::origin("exa-api") == "env: AGENTTY_WEB_SEARCH_KEY_EXA_API");
    CHECK(ss::env_name("brave-api") == "AGENTTY_WEB_SEARCH_KEY_BRAVE_API");
    ::unsetenv("AGENTTY_WEB_SEARCH_KEY_EXA_API");
    CHECK(ss::load("exa-api") == "stored-key-0123456789");
}

TEST_CASE("web search secret: an unreadable key file is never overwritten") {
    TmpHome home;
    if (home.real_keyring) { MESSAGE("skipped: OS keystore armed for this process"); return; }
    REQUIRE(ss::store(::agentty::IoAccess::grant(), agtest::cap(), "exa-api", "exa-FIRST-0123456789"));
    // 找到密封文件并损坏它,因为磁盘错误或机器
    // -id更改将:它存在但不能再打开.
    fs::path file;
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(home.dir, ec))
        if (e.path().filename() == "web_search_keys.json") file = e.path();
    REQUIRE(!file.empty());
    { std::ofstream out(file, std::ios::binary | std::ios::trunc); out << "not a sealed envelope"; }
    // 存储另一服务的密钥不得替换文件(如果损坏可以收回,
    // 文件将永远丢失exa-api的密钥).
    CHECK(!ss::store(::agentty::IoAccess::grant(), agtest::cap(), "serper-api", "serper-SECOND-0123456789"));
    CHECK(TmpHome::slurp(file) == "not a sealed envelope");
    CHECK(!ss::has("exa-api"));
}

TEST_CASE("web search secret: implausible keys are refused before they are stored") {
    TmpHome home;
    CHECK(!ss::plausible("short"));
    CHECK(!ss::plausible("has a space inside it"));
    CHECK(!ss::plausible("line\nbreak-0123456789"));
    CHECK(!ss::plausible(std::string(513, 'a')));
    CHECK(ss::plausible("tvly-0123456789abcdef"));
    CHECK(!ss::store(::agentty::IoAccess::grant(), agtest::cap(), "exa-api", "two words here"));
    CHECK(!ss::has("exa-api"));
}

TEST_CASE("web search secret: the live plan source reads policy and key per call") {
    TmpHome home;
    // 政策档是-wide 和其他测试安装到它,
    // 所以在这里安装一个已知的,然后放回那里。
    const auto saved = agentty::tools::web_search_policy::current();
    struct Restore {
        sc::Config c;
        ~Restore() { agentty::tools::web_search_policy::install(c); }
    } restore{saved};

    auto source = sp::make_plan_source();
    REQUIRE(source != nullptr);

    sc::Config c;
    c.primary = "serper-api"; c.secondary = "tavily-free"; c.fallback = "none";
    agentty::tools::web_search_policy::install(c);
    ::setenv("AGENTTY_WEB_SEARCH_KEY_SERPER_API", "env-serper-0123456789", 1);
    auto p = source->plan();
    REQUIRE(p.slots.size() == 2);
    CHECK(p.slots[0].service == "serper-api");
    CHECK(p.slots[0].api_key == "env-serper-0123456789");

    // 变化是 下一个 调用 ,没有新的来源。
    ::unsetenv("AGENTTY_WEB_SEARCH_KEY_SERPER_API");
    c.primary = "firecrawl-free";
    agentty::tools::web_search_policy::install(c);
    p = source->plan();
    REQUIRE(p.slots.size() == 2);
    CHECK(p.slots[0].service == "firecrawl-free");
    CHECK(p.slots[0].api_key.empty());
}

// ── 电话 面板 线条 ──────────────────────────────────────────────────────

TEST_CASE("web search form: free defaults add dial rows and no key row") {
    const auto f = wtf::build_form(sc::Config{}, kNoEnv);
    const auto ids = field_ids(f);
    // 槽行存在,依序排列.
    auto at = [&](std::string_view id) {
        for (std::size_t i = 0; i < ids.size(); ++i) if (ids[i] == id) return static_cast<int>(i);
        return -1;
    };
    REQUIRE(at("web_search.primary") >= 0);
    CHECK(at("web_search.primary") < at("web_search.secondary"));
    CHECK(at("web_search.secondary") < at("web_search.fallback"));
    // 勇敢的自由提供新鲜感,在小学之下。
    CHECK(at("web_search.dial.brave-free.freshness") > at("web_search.primary"));
    CHECK(at("web_search.dial.brave-free.freshness") < at("web_search.secondary"));
    // DuckDuckGo 免费的2,在二级。
    CHECK(at("web_search.dial.ddg-free.region") > at("web_search.secondary"));
    CHECK(at("web_search.dial.ddg-free.safe") < at("web_search.fallback"));
    // 免费的没有要求钥匙。
    for (const auto& id : ids) CHECK(id.rfind("web_search.key.", 0) != 0);
}

TEST_CASE("web search form: a keyed service gets a Secret row that starts empty") {
    sc::Config c; c.primary = "exa-api";
    const auto f = wtf::build_form(c, kNoEnv, [](std::string_view s) {
        return s == "exa-api" ? std::string{"stored"} : std::string{};
    });
    const auto* row = f.find("web_search.key.exa-api");
    REQUIRE(row != nullptr);
    const auto* sec = std::get_if<form::field::Secret>(&row->value);
    REQUIRE(sec != nullptr);
    CHECK(sec->value.empty());            // $面板 从未给过密钥
    CHECK(sec->placeholder == "\xe2\x9c\x93 key saved");
    CHECK(row->origin.empty());           // 一次, 在值栏中
    CHECK(row->locked);                   // 把它拿下来换掉
    CHECK(wtf::key_row_service(row->id) == "exa-api");
    // 存储的密钥可以被删除。
    CHECK(f.find("web_search.keyclear.exa-api") != nullptr);

    // 尚未找到密钥: 部件本身的 "not set"( 没有占位符, 没有来源) ,
    // 打开一个密钥, 并且没有要删除的。
    const auto g = wtf::build_form(c, kNoEnv);
    const auto* empty = g.find("web_search.key.exa-api");
    REQUIRE(empty != nullptr);
    CHECK(std::get<form::field::Secret>(empty->value).placeholder.empty());
    CHECK(empty->origin.empty());
    CHECK(!empty->locked);
    CHECK(g.find("web_search.keyclear.exa-api") == nullptr);
}

TEST_CASE("web search form: a key from the environment locks its row") {
    sc::Config c; c.primary = "exa-api";
    const auto f = wtf::build_form(c, kNoEnv, [](std::string_view) {
        return std::string{"env: AGENTTY_WEB_SEARCH_KEY_EXA_API"};
    });
    const auto* row = f.find("web_search.key.exa-api");
    REQUIRE(row != nullptr);
    CHECK(row->locked);
    // 供应商小组的措辞:不是"saved",而是生活在壳体中.
    CHECK(std::get<form::field::Secret>(row->value).placeholder
          == "\xe2\x97\x8f key from AGENTTY_WEB_SEARCH_KEY_EXA_API");
    CHECK(row->origin.empty());
    CHECK(f.find("web_search.keyclear.exa-api") == nullptr);    // 是我们要删除的
}

TEST_CASE("web search form: off gives a held key's lock the mode's reason") {
    // 钥匙行的自己 锁 带无故( 其值已表示 )
    // "key saved" ) 关闭后,它必须仍然说 原因 和每行一样,
    // 是惰性的。
    sc::Config c; c.mode_text = "off"; c.primary = "exa-api";
    const auto f = wtf::build_form(c, kNoEnv, [](std::string_view s) {
        return s == "exa-api" ? std::string{"stored"} : std::string{};
    });
    const auto* row = f.find("web_search.key.exa-api");
    REQUIRE(row != nullptr);
    CHECK(row->locked);
    CHECK(!row->locked_reason.empty());
}

TEST_CASE("web search form: a service named twice shows its rows once") {
    sc::Config c; c.primary = "tavily-api"; c.secondary = "tavily-api"; c.fallback = "tavily-api";
    const auto ids = field_ids(wtf::build_form(c, kNoEnv));
    int keys = 0;
    for (const auto& id : ids) if (id == "web_search.key.tavily-api") ++keys;
    CHECK(keys == 1);
}

TEST_CASE("web search form: slot rows read as service names") {
    const auto f = wtf::build_form(sc::Config{}, kNoEnv);
    const auto* ch = std::get_if<form::field::Choice>(&f.find("web_search.primary")->value);
    REQUIRE(ch != nullptr);
    CHECK(ch->label() == "Brave Free");
    CHECK(ch->id() == "brave-free");          // 存储值仍然是 id
    // Secondary/Fallback 可能空出; 小学可能空出。
    const auto* sec = std::get_if<form::field::Choice>(&f.find("web_search.secondary")->value);
    REQUIRE(sec != nullptr);
    bool none = false;
    for (const auto& i : sec->ids) none = none || i == "none";
    CHECK(none);
    for (const auto& i : ch->ids) CHECK(i != "none");
}

TEST_CASE("web search form: a dial round-trips through apply_form, and the default erases it") {
    sc::Config c;
    auto f = wtf::build_form(c, kNoEnv);
    auto* row = f.find("web_search.dial.brave-free.freshness");
    REQUIRE(row != nullptr);
    auto* ch = std::get_if<form::field::Choice>(&row->value);
    REQUIRE(ch != nullptr);
    ch->select_id("pw");
    wtf::apply_form(f, c);
    CHECK(c.dials.at("brave-free.freshness") == "pw");

    // 从配置中重建, 行显示存储的内容。
    auto g = wtf::build_form(c, kNoEnv);
    CHECK(std::get<form::field::Choice>(g.find("web_search.dial.brave-free.freshness")->value).id() == "pw");

    // 回转"Any time":该条目被删除,而非空存储.
    std::get<form::field::Choice>(g.find("web_search.dial.brave-free.freshness")->value).select_id("");
    wtf::apply_form(g, c);
    CHECK(c.dials.find("brave-free.freshness") == c.dials.end());
}

TEST_CASE("web search form: a text dial is a Text row, checked before it is stored") {
    const std::string goggle =
        "https://raw.githubusercontent.com/brave/goggles-quickstart/main/goggles/tech_blogs.goggle";
    sc::Config c;
    c.primary = "brave-api";
    auto f = wtf::build_form(c, kNoEnv);
    auto* row = f.find("web_search.dial.brave-api.goggles");
    REQUIRE(row != nullptr);
    auto* tx = std::get_if<form::field::Text>(&row->value);
    REQUIRE(tx != nullptr);
    CHECK(tx->value.empty());

    // 带类型的带有流转空间:存储在修剪中,并在重建中再次显示.
    tx->value = "  " + goggle + " ";
    wtf::apply_form(f, c);
    CHECK(c.dials.at("brave-api.goggles") == goggle);
    auto g = wtf::build_form(c, kNoEnv);
    CHECK(std::get<form::field::Text>(g.find("web_search.dial.brave-api.goggles")->value).value
          == goggle);

    // 搜索不会发送的东西没有被存储; 旧值会保留。
    for (const char* bad : {"http://example.com/a.goggle", "$boost=3,site=dev.to",
                            "https://a.com/x y"}) {
        std::get<form::field::Text>(g.find("web_search.dial.brave-api.goggles")->value).value = bad;
        wtf::apply_form(g, c);
        CHECK(c.dials.at("brave-api.goggles") == goggle, bad);
    }

    // 嵌入:该条目被删除,因为列表拨号被设定回默认值.
    std::get<form::field::Text>(g.find("web_search.dial.brave-api.goggles")->value).value = "  ";
    wtf::apply_form(g, c);
    CHECK(c.dials.find("brave-api.goggles") == c.dials.end());
}

TEST_CASE("web search form: choosing a slot's service changes the rows under it") {
    sc::Config c;
    c.primary = "exa-api";
    const auto ids = field_ids(wtf::build_form(c, kNoEnv));
    CHECK(has(ids, "web_search.dial.exa-api.type"));
    CHECK(has(ids, "web_search.key.exa-api"));
    CHECK(!has(ids, "web_search.dial.brave-free.freshness"));
    // 用于未使用服务的过期拨号保存在配置中,不显示。
    c.dials["brave-free.freshness"] = "pd";
    CHECK(!has(field_ids(wtf::build_form(c, kNoEnv)), "web_search.dial.brave-free.freshness"));
    CHECK(c.dials.count("brave-free.freshness") == 1);
}

TEST_CASE("web search form: off locks every service row too") {
    sc::Config c; c.mode_text = "off"; c.primary = "exa-api";
    const auto f = wtf::build_form(c, kNoEnv);
    for (const auto& r : f.fields) {
        if (r.id == "web_search.mode" || r.is_header()) { CHECK(!r.locked); continue; }
        INFO(r.id);
        CHECK(r.locked);
    }
}

// ── settings.json ────────────────────────────────────────────────────────

TEST_CASE("web search settings: dials and slots persist, and a key never does") {
    TmpHome home;
    if (home.real_keyring) { MESSAGE("skipped: OS keystore armed for this process"); return; }
    store::Settings s;
    s.web_search.primary = "exa-api";
    s.web_search.secondary = "none";
    s.web_search.language = "de";
    s.web_search.dials["exa-api.type"] = "fast";
    s.web_search.dials["brave-api.goggles"] =
        "https://raw.githubusercontent.com/brave/goggles-quickstart/main/goggles/tech_blogs.goggle";
    REQUIRE(ss::store(::agentty::IoAccess::grant(), agtest::cap(), "exa-api", "exa-LEAKCHECK-0123456789"));
    persistence::save_settings(::agentty::IoAccess::grant(), agtest::cap(), s);

    const auto text = TmpHome::slurp(home.dir / "home" / "settings.json");
    CHECK(text.find("\"exa-api\"") != std::string::npos);
    CHECK(text.find("LEAKCHECK") == std::string::npos);
    CHECK(home.all_files_text().find("LEAKCHECK") == std::string::npos);

    const auto back = persistence::load_settings(::agentty::IoAccess::grant());
    CHECK(back.web_search.primary == "exa-api");
    CHECK(back.web_search.secondary == "none");
    CHECK(back.web_search.fallback == "tavily-free");        // 未受影响的默认
    CHECK(back.web_search.language == "de");
    CHECK(back.web_search.dials.at("exa-api.type") == "fast");
    CHECK(back.web_search.dials.at("brave-api.goggles").size() > 64);   // 过旧盖子
}

TEST_CASE("web search settings: a hand-edited bad value cannot reach a slot") {
    TmpHome home;
    {
        std::ofstream out(home.dir / "home" / "settings.json");
        out << R"({"web_search":{"primary":"not-a-service","secondary":"ddg-free",)"
               R"("language":"klingon","dials":{"brave-free.freshness":"pw",)"
               R"("x":7,"":"y"}}})";
    }
    const auto s = persistence::load_settings(::agentty::IoAccess::grant());
    // 枚举 行只接受一个已宣布的选项.
    CHECK(s.web_search.primary == "brave-free");
    CHECK(s.web_search.language == "auto");
    CHECK(s.web_search.secondary == "ddg-free");
    // 无 -string 空的 -keyed 拨号条目被删除;好条目被保留。
    CHECK(s.web_search.dials.size() == 1);
    CHECK(s.web_search.dials.at("brave-free.freshness") == "pw");
}
