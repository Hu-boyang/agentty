// web_search_form.cpp — 设置 ~ Web 搜索行模式.

#include "agentty/runtime/panel/web_search_form.hpp"

#include "agentty/runtime/settings_registry.hpp"

#include <mcp/tools/web_search.hpp>

#include <algorithm>
#include <string_view>
#include <vector>

#include <string>
#include <type_traits>
#include <variant>

namespace agentty::web_search_form {

namespace reg = settings::registry;

namespace {

constexpr std::string_view kKeyClearPrefix = "web_search.keyclear.";

bool is_slot_row(std::string_view id) {
    return id == "web_search.primary" || id == "web_search.secondary" || id == "web_search.fallback";
}

const std::string& slot_value(const web_search_cfg::Config& c, std::string_view id) {
    return id == "web_search.primary" ? c.primary
         : id == "web_search.secondary" ? c.secondary : c.fallback;
}

std::string dial_key(std::string_view service, std::string_view dial) {
    std::string k{service};
    k += '.';
    k += dial;
    return k;
}

std::string trim(std::string_view s) {
    constexpr std::string_view kSpace = " \t";
    const auto b = s.find_first_not_of(kSpace);
    if (b == std::string_view::npos) return {};
    return std::string{s.substr(b, s.find_last_not_of(kSpace) - b + 1)};
}

// 是否可存储`value`,以便拨`key`("<service>.<dial>"):
// mcp-cpp 自己的规则,所以 面板 永远 保存 搜索会减少的
bool text_dial_ok(std::string_view key, std::string_view value) {
    const auto dot = key.find('.');
    if (dot == std::string_view::npos) return false;
    const auto* svc = ::mcp::tools::find_web_search_service(key.substr(0, dot));
    if (!svc) return false;
    for (const auto& d : svc->dials)
        if (d.id == key.substr(dot + 1)) return ::mcp::tools::web_search_dial_value_ok(d, value);
    return false;
}

// 副标题为"Brave Free \xe2\x86\x92 DuckDuckGo Free \xe2\x86\x92 Tavily Free".
std::string chain_text(const web_search_cfg::Config& c) {
    std::string out;
    for (const std::string* id : {&c.primary, &c.secondary, &c.fallback}) {
        const auto* svc = ::mcp::tools::find_web_search_service(*id);
        if (!svc) continue;
        if (!out.empty()) out += " \xe2\x86\x92 ";
        out += svc->label;
    }
    return out.empty() ? std::string{"Brave Free"} : out;
}

// 片段行上的目录标签. add_row 构建一个标签为原始 ID 的
// 选择( Q) "brave-free" ); IDs 保留所储存的,
// 标签成为一个人所读的.
void label_slot_choice(form::Field& row) {
    auto* ch = std::get_if<form::field::Choice>(&row.value);
    if (!ch) return;
    ch->ids = ch->labels;
    ch->hints.clear();
    for (auto& l : ch->labels) {
        if (l == "none") { l = "None"; ch->hints.emplace_back("leave this slot empty"); continue; }
        const auto* svc = ::mcp::tools::find_web_search_service(l);
        if (!svc) { ch->hints.emplace_back(); continue; }
        l = svc->label;
        ch->hints.emplace_back(svc->needs_key ? "needs an API key" : "free, no account");
    }
}

// 悬挂在一个插槽下的行:服务拨号,然后是它的键.
void add_service_rows(form::Builder& b, const web_search_cfg::Config& c,
                      const ::mcp::tools::WebSearchService& svc,
                      const KeyOrigin& key_origin) {
    for (const auto& d : svc.dials) {
        std::string cur;
        if (auto it = c.dials.find(dial_key(svc.id, d.id)); it != c.dials.end())
            cur = it->second;
        std::string help = svc.label + (d.help.empty() ? "" : " \xc2\xb7 " + d.help);
        std::string row_id = std::string{kDialPrefix} + dial_key(svc.id, d.id);
        if (d.text != ::mcp::tools::WebSearchText::None) {
            b.text(std::move(row_id), "  " + d.label, cur, std::move(help));
        } else {
            std::vector<std::string> labels, ids;
            for (const auto& v : d.values) { labels.push_back(v.label); ids.push_back(v.id); }
            b.choice(std::move(row_id), "  " + d.label, std::move(labels), std::move(ids),
                     cur, std::move(help));
        }
        b.origin(cur.empty() ? "default" : "settings.json");
    }
    if (!svc.needs_key) return;

    const std::string origin = key_origin ? key_origin(svc.id) : std::string{};
    const bool stored   = origin == "stored";
    const bool from_env = origin.rfind("env: ", 0) == 0;
    const std::string env_var = from_env ? origin.substr(5) : std::string{};

    // 帮助线是解释一个锁上行,所以它命名了出路.
    std::string help = svc.label + " \xc2\xb7 ";
    if (stored)
        help += "key saved, stored encrypted \xc2\xb7 Remove key below to replace it";
    else if (from_env)
        help += "key from " + env_var + " \xc2\xb7 unset it to store one here";
    else
        help += "paste a key and press Enter \xc2\xb7 stored encrypted, never in "
                "settings.json \xc2\xb7 " + svc.signup;
    b.secret(std::string{kKeyPrefix} + svc.id, "  API key", "", std::move(help));

    // 排行说 一个 语句和格言是提供者的面板:我们
    // 持有的密钥读取了 "\xe2\x9c\x93 key saved" ,
    // 来自环境的1
    // "\xe2\x97\x8f key from VAR"
    // (不是这样) 已保存 键:部件本身 "not set"。
    // 在第一个答案之前, "checking..."
    // 所以这行都说不出
    if (stored)               b.placeholder("\xe2\x9c\x93 key saved");
    else if (from_env)        b.placeholder("\xe2\x97\x8f key from " + env_var);
    else if (!origin.empty()) b.placeholder(origin);
    // 被持有的 锁 键它的行: 存储的键通过先移除它而更改
    // ( 移出键, 下面), 而一个嵌入键则根本不是我们
    // 要更改的。 原因留空了 — 值列已经说了。
    if (stored || from_env) b.lock("");
    // 只有我们持有的密钥才能被移除。 环境中的一款不是我们的,
    // 上面的一行是锁的, 原因是一样的。
    if (stored)
        b.action(std::string{kKeyClearPrefix} + svc.id, "  Remove key",
                 "forget the stored " + svc.label + " key", "press Enter");
}

} // 命名空间结束。

std::string_view key_row_service(std::string_view row_id) noexcept {
    if (row_id.rfind(kKeyPrefix, 0) == 0) return row_id.substr(kKeyPrefix.size());
    return {};
}

std::string_view key_clear_service(std::string_view row_id) noexcept {
    if (row_id.rfind(kKeyClearPrefix, 0) == 0) return row_id.substr(kKeyClearPrefix.size());
    return {};
}

form::Form build_form(const web_search_cfg::Config& c, const reg::EnvSnapshot& env,
                      const KeyOrigin& key_origin) {
    form::Builder b{" Web Search "};
    // 说说环境的规范: 有效的链条,按名称排列。
    switch (web_search_cfg::mode(c)) {
        case web_search_cfg::Mode::On:
            b.subtitle("web_search \xc2\xb7 your counts apply \xc2\xb7 " + chain_text(c));
            break;
        case web_search_cfg::Mode::Off:
            b.subtitle("web_search is off \xc2\xb7 the agent cannot search the web");
            break;
        case web_search_cfg::Mode::Auto:
            b.subtitle("web_search \xc2\xb7 the model sets the count, as before \xc2\xb7 " + chain_text(c));
            break;
    }

    bool first = true;
    auto last_group = reg::Group::WebSearch;
    std::vector<std::string> shown;   // 已退出的服务
    // 每行搜索都是Basic:每个都回答一个用户实际上可以拥有的问题(
    // 为什么结果很少？为什么没有执行搜索？
    // 步行到这里而不是通过add_rows,只好让一个位位的拨号和
    // 按键直接放在它下面.
    for (const auto& d : reg::kSettings) {
        if (d.owner() != reg::Owner::WebSearch) continue;
        if (first || d.group != last_group) {
            b.header(std::string{reg::label_of(d.group)});
            last_group = d.group;
            first = false;
        }
        reg::add_row(b, c, env, d);
        if (!is_slot_row(d.id)) continue;
        const std::string& id = slot_value(c, d.id);
        const auto* svc = ::mcp::tools::find_web_search_service(id);
        if (!svc || std::find(shown.begin(), shown.end(), id) != shown.end()) continue;
        shown.push_back(id);
        add_service_rows(b, c, *svc, key_origin);
    }

    form::Form f = b.build();
    for (auto& row : f.fields)
        if (is_slot_row(row.id)) label_slot_choice(row);

    // 在当前模式下无所作为的行被锁定,有原因,仍然显示,
    // 这样用户就可以看到自己持有的. apply_form跳过被
    // 锁定的行,因此其存储的值在切换到使用它们的模式后幸存下来.
    //   自动:模型设置计数,因此两行计数是惰性的。
    //   关闭: 什么都没有被搜索, 所以每行除了开关都是惰性的。
    // 一个env 锁被单独留下:它的原因(变量)是更真实的.
    // 持有的按键的锁没有它自己的理由(其值表示它),
    // 所以它取模式的.
    auto lock = [](form::Field& row, const char* why) {
        if (row.locked && !row.locked_reason.empty()) return;
        row.locked = true;
        row.locked_reason = why;
    };
    auto lock_id = [&](const char* id, const char* why) {
        if (auto* row = f.find(id)) lock(*row, why);
    };
    switch (web_search_cfg::mode(c)) {
        case web_search_cfg::Mode::Auto:
            for (const char* id : {"web_search.count", "web_search.max_count"})
                lock_id(id, "auto: the model sets the count \xe2\x80\x94 switch to on to set it");
            break;
        case web_search_cfg::Mode::Off:
            for (auto& row : f.fields) {
                if (row.id == "web_search.mode" || row.is_header()) continue;
                lock(row, "off: web search is off \xe2\x80\x94 switch to auto or on");
            }
            break;
        case web_search_cfg::Mode::On:
            break;
    }
    return f;
}


void apply_form(const form::Form& f, web_search_cfg::Config& c) {
    reg::read_rows(c, f, reg::Owner::WebSearch);
    // 拨号行。 它们不是注册表行( 拨号的存在取决于所选服务),
    // 因此它们是由其id前缀找到的。 拨号被放回服务本身的
    // 默认值( 或文本行空) 会被擦除, 而不是存储为""。
    // 带类型的 文本 mcp-cpp 也不会被存储 :
    // 然后重建显示记录中的行。
    for (const auto& row : f.fields) {
        if (row.locked || row.id.rfind(kDialPrefix, 0) != 0) continue;
        const std::string key = row.id.substr(kDialPrefix.size());
        std::string val;
        if (const auto* ch = std::get_if<form::field::Choice>(&row.value)) {
            val = std::string{ch->id()};
        } else if (const auto* tx = std::get_if<form::field::Text>(&row.value)) {
            val = trim(tx->value);
            if (!val.empty() && !text_dial_ok(key, val)) continue;
        } else {
            continue;
        }
        if (val.empty()) c.dials.erase(key);
        else             c.dials[key] = val;
    }
}

} // namespace agentty::web_search_form
