// web_search 政策( 设置 ) : 请求重写 agentty
// 适用于 分派 接口边界 ,并显示 注册表 坚持下去的排队
//
// 重写是整个背景 执行 时间 调用 时间,所以它直接被固定在这里,
// 而不是通过一个直播搜索:三种模式(自动将计数留给模型,
// 在应用用户的默认和上限时,关闭被拒绝),计数解析度在上,
// 以及 -site : 除外情形(由 DuckDuckGo ,
// 在 Brave 上观察到;使用本地域过滤器的服务将列表作为字段
// - 见 domain/web_search_config.hpp).

#include "agtest.hpp"

#include "agentty/domain/web_search_config.hpp"
#include "agentty/runtime/settings_registry.hpp"

#include <nlohmann/json.hpp>

#include <cctype>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace sc  = agentty::web_search_cfg;
namespace reg = agentty::settings::registry;
using nlohmann::json;

TEST_CASE("web search policy: exclusions normalise what a user pastes") {
    // 地址 -bar URL,一种习惯 -typed 操作员、大小写和www.
    // 全部缩小为空宿主 `site:` 接线员想要
    const auto v = sc::exclude_list(
        "https://www.Pinterest.com/pin/123, -site:w3schools.com;"
        "  medium.com:443  site:EXAMPLE.org/");
    CHECK((v == std::vector<std::string>{"pinterest.com", "w3schools.com",
                                        "medium.com", "example.org"}));
}

TEST_CASE("web search policy: junk entries are dropped, duplicates collapse") {
    // 没有点,一个空字,一个领先的连字符标签和一个非-ASCII主机,
    // 每个主机都会成为操作员,没有引擎读取用户的意思.
    const auto v = sc::exclude_list(
        "localhost foo -bad.com pinterest.com PINTEREST.COM www.pinterest.com "
        "b\xc3\xbc" "cher.de xn--bcher-kva.de");
    CHECK((v == std::vector<std::string>{"pinterest.com", "xn--bcher-kva.de"}));
    CHECK(sc::exclude_list("").empty());
    CHECK(sc::exclude_list(" , ;\t").empty());
}

TEST_CASE("web search policy: count — default, model's request, clamped") {
    sc::Config c;                 // 数十,最大20
    CHECK(sc::effective_count(c, json::object()) == 10);
    CHECK((sc::effective_count(c, json{{"count", 5}}) == 5));
    CHECK((sc::effective_count(c, json{{"count", 500}}) == 20));   // 上限
    CHECK((sc::effective_count(c, json{{"count", 0}}) == 1));      // 楼层
    CHECK((sc::effective_count(c, json{{"count", -3}}) == 1));
    // 模型会以不同的方式发送数字; 带走,或倒回去。
    CHECK((sc::effective_count(c, json{{"count", "7"}}) == 7));
    CHECK((sc::effective_count(c, json{{"count", 7.9}}) == 7));
    CHECK((sc::effective_count(c, json{{"count", "lots"}}) == 10));
    CHECK((sc::effective_count(c, json{{"count", nullptr}}) == 10));
    // 永远不要崩溃,永远不要沉默的前缀解析.
    CHECK((sc::effective_count(c, json{{"count", true}}) == 10));
    CHECK((sc::effective_count(c, json{{"count", "7abc"}}) == 10));
    CHECK((sc::effective_count(c, json{{"count", " 7 "}}) == 7));
    CHECK((sc::effective_count(c, json{{"count", 18446744073709551615ULL}}) == 20));
    CHECK((sc::effective_count(c, json{{"count", 1e300}}) == 20));

    // 用户在自己上限之上的默认会解决上限,
    // 永远无法通过.
    c.count = 15; c.max_count = 5;
    CHECK(sc::effective_count(c, json::object()) == 5);
}

TEST_CASE("web search policy: exclusions append once, model's own kept") {
    const std::vector<std::string> sites{"pinterest.com", "medium.com"};
    CHECK(sc::apply_exclusions("rust async", sites)
          == "rust async -site:pinterest.com -site:medium.com");
    // 模式(任何案件)已排除在外:不再重复。
    CHECK(sc::apply_exclusions("rust -SITE:Pinterest.com", sites)
          == "rust -SITE:Pinterest.com -site:medium.com");
    // 带有后排空间的查询不会获得双排空间。
    CHECK(sc::apply_exclusions("rust ", sites)
          == "rust -site:pinterest.com -site:medium.com");
    CHECK((sc::apply_exclusions("q", {}) == "q"));
}

TEST_CASE("web search policy: rewrite_args owns count + query, nothing else") {
    sc::Config c;
    c.mode_text = "on";   // 计数只应用在 On
    c.count = 6;
    c.exclude_sites = "pinterest.com";
    const json in{{"query", "css grid"}, {"display_description", "why"},
                  {"future_arg", 1}};
    const json out = sc::rewrite_args(c, in);
    CHECK(out["query"] == "css grid -site:pinterest.com");
    CHECK(out["count"] == 6);
    CHECK(out["display_description"] == "why");
    CHECK(out["future_arg"] == 1);

    // 无查询, 类型错误 : 留给 mcp-cpp 以自有错误拒绝。
    CHECK((sc::rewrite_args(c, json{{"query", 3}})["query"] == 3));
    CHECK(sc::rewrite_args(c, json::array()).is_array());

    // 默认是Auto:计数是模型的,所以没有为它写;
    // 查询仍然得到传输形状的排除.
    const json plain = sc::rewrite_args(sc::Config{}, json{{"query", "x"}});
    CHECK(plain["query"] == "x");
    CHECK(!plain.contains("count"));

    // 操作时, 当模型静默时, 应用用户的默认值。
    sc::Config on; on.mode_text = "on";
    CHECK(sc::rewrite_args(on, json{{"query", "x"}})["count"] == 10);
}

TEST_CASE("web search policy: auto passes the model's count through untouched") {
    sc::Config c;                       // 默认自动
    CHECK(sc::mode(c) == sc::Mode::Auto);
    // 模型要求50;自动既不夹住它,也不填充默认值.
    CHECK(sc::rewrite_args(c, json{{"query", "x"}, {"count", 50}})["count"] == 50);
    // 排除条款仍然适用于Auto。
    c.exclude_sites = "pinterest.com";
    CHECK(sc::rewrite_args(c, json{{"query", "x"}})["query"] == "x -site:pinterest.com");
    // 字典或手编辑读作 Auto, 绝不是 On 或 Out。
    sc::Config typo; typo.mode_text = "ON ";
    CHECK(sc::mode(typo) == sc::Mode::Auto);
}

TEST_CASE("web search policy: every web_search.* row round-trips through the registry") {
    // 同一合同 smart_settings_roundtrip 智能
    // 模式的针: 列 面板 能够显示必须可以写回, 不管它的控制类型。
    int seen = 0;
    for (const auto& d : reg::kSettings) {
        if (d.owner() != reg::Owner::WebSearch) continue;
        ++seen;
        sc::Config c;
        switch (d.type) {
            case reg::Type::Bool:
                CHECK(reg::set(c, d, "false"));
                CHECK(reg::get(c, d) == "false");
                CHECK(!reg::is_default(c, d));
                CHECK(reg::set(c, d, "true"));
                CHECK(reg::get(c, d) == "true");
                break;
            case reg::Type::Int:
                CHECK(reg::set(c, d, "3"));
                CHECK(reg::get(c, d) == "3");
                // 冲进来的路上,从每个来源。
                CHECK(reg::set(c, d, "100000"));
                CHECK(reg::get(c, d) == std::to_string(static_cast<int>(d.max)));
                break;
            case reg::Type::Text:
                CHECK(reg::set(c, d, "pinterest.com\nmedium.com"));
                // 粘贴的新线无法达到settings.json.
                CHECK(reg::get(c, d) == "pinterest.commedium.com");
                CHECK(reg::set(c, d, ""));
                CHECK(reg::is_default(c, d));
                break;
            case reg::Type::Enum: {
                // 每一轮宣布的选项回合 -trips (
                // web_search.mode ,三个服务槽,
                // 语言);案件折叠到犬字;其他任何内容都拒绝,使行不变.
                INFO(d.id);
                std::vector<std::string> opts;
                for (std::string_view rest = d.options; !rest.empty();) {
                    const auto bar = rest.find('|');
                    opts.emplace_back(rest.substr(0, bar));
                    if (bar == std::string_view::npos) break;
                    rest.remove_prefix(bar + 1);
                }
                REQUIRE(opts.size() >= 2);
                for (const auto& v : opts) {
                    CHECK(reg::set(c, d, v));
                    CHECK(reg::get(c, d) == v);
                }
                std::string upper = opts.back();
                for (char& ch : upper)
                    ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                CHECK(reg::set(c, d, upper));
                CHECK(reg::get(c, d) == opts.back());
                CHECK_FALSE(reg::set(c, d, "sometimes"));
                CHECK(reg::get(c, d) == opts.back());
                break;
            }
            case reg::Type::Real:
                CHECK(false);   // 今天没有搜索行使用此功能
                break;
        }
        reg::reset(c, d);
        CHECK(reg::is_default(c, d));
    }
    // 命名, 不计数( 当添加一行时, 准确的计数失败
    // ) : 列的面板 和计划所依赖的必须全部在这里。
    CHECK(seen >= 8);
    for (const char* id : {"web_search.mode", "web_search.count", "web_search.max_count",
                           "web_search.exclude_sites", "web_search.primary",
                           "web_search.secondary", "web_search.fallback",
                           "web_search.language"}) {
        const auto* d = reg::find(id);
        REQUIRE(d != nullptr);
        CHECK(d->owner() == reg::Owner::WebSearch, id);
    }
}

TEST_CASE("web search policy: the row a user sees first is the master switch") {
    // 电话 面板 将桌子按顺序排列; on/off 排行引线,
    // 所以是光标打开的。
    const reg::SettingDef* first = nullptr;
    for (const auto& d : reg::kSettings)
        if (d.owner() == reg::Owner::WebSearch) { first = &d; break; }
    REQUIRE(first != nullptr);
    CHECK((first->id == std::string_view{"web_search.mode"}));
    CHECK(first->type == reg::Type::Enum);
    // 自动第一:默认情况下的下拉打开,
    // 面板显示的顺序是测试帮助者步入的命令.
    CHECK(first->options == std::string_view{"auto|on|off"});
}

TEST_CASE("web search policy: AGENTTY_WEB_SEARCH accepts boolean spellings") {
    const auto with = [](const char* v) {
#ifdef _WIN32
        _putenv_s("AGENTTY_WEB_SEARCH", v);
#else
        ::setenv("AGENTTY_WEB_SEARCH", v, 1);
#endif
        sc::Config c;
        reg::apply_env(::agentty::IoAccess::grant(), c);
#ifdef _WIN32
        _putenv_s("AGENTTY_WEB_SEARCH", "");
#else
        ::unsetenv("AGENTTY_WEB_SEARCH");
#endif
        return sc::mode(c);
    };
    // 旧拼写:假的已关闭;真实的为"available, model picks the count"
    // ——Auto,从不On(On将开始夹住未询问的).
    for (const char* v : {"0", "false", "no", "OFF"}) CHECK(with(v) == sc::Mode::Off);
    for (const char* v : {"1", "true", "yes"})        CHECK(with(v) == sc::Mode::Auto);
    // 模式词本身。
    CHECK(with("on")   == sc::Mode::On);
    CHECK(with("On")   == sc::Mode::On);
    CHECK(with("auto") == sc::Mode::Auto);
    // 容克被忽略:默认站.
    CHECK(with("maybe") == sc::Mode::Auto);
}
