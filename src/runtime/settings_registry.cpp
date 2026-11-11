// settings_registry.cpp — 该表的衍生操作.
//
// 这里的每个功能 WALKS kSettings.
// 其中没有一个名称是单个设置, 即整点: 添加一个旋钮就是添加一行,
// 这四个函数加 UI 免费捡起来

#include "agentty/runtime/settings_registry.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <string>

namespace agentty::settings::registry {

namespace {

// 已发运的默认值, 作为默认值 -constructed 配置。
// 与此相对应的是 `is_default` 避免二手手的出现 -maintained
// 默认列表 – Struct 的成员状态状态列表已经是唯一的真理来源。
const store::RagConfig& defaults() {
    static const store::RagConfig d{};
    return d;
}

[[nodiscard]] std::string lower(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) out.push_back(static_cast<char>(std::tolower(c)));
    return out;
}

[[nodiscard]] bool parse_bool(std::string_view s, bool& out) {
    const std::string v = lower(s);
    if (v == "1" || v == "true"  || v == "on"  || v == "yes") { out = true;  return true; }
    if (v == "0" || v == "false" || v == "off" || v == "no")  { out = false; return true; }
    return false;
}

// 固定 -decimal 格式化,不带 <format> :这些
// 是定界比率,所以整数缩放足够精确,并保持跨平台输出的稳定(
// printf的%g不同).
[[nodiscard]] std::string fmt_real(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.4g", v);
    return buf;
}

// 一个知道如何到达行存储的地方。漏斗下面的每一个访问
// 器都通过这个,所以一个后来添加的槽替代方案在准确的一
// 地无法到达编译,而不是散开.
//
// 通用于 OWNING STRUCT 排成一行,所以一半 变体
// 其替代品不适用于任何特定 `C` ;这些是 SKIPPED ,
// 而不是一个错误—— `apply_env(Settings&)`
// 将整个表格走过, 并传递每个破碎。 * 行是预定的行为。
// 返回该行是否属于我们, 所以需要知道的呼叫者可以诚实回答而不是将
// 读作真实数据的默认内容交回。
template <class C, class Visit>
bool with_slot(C& c, const SettingDef& d, Visit&& v) {
    return std::visit([&](auto member) -> bool {
        if constexpr (requires { c.*member; }) {
            v(c.*member);
            return true;
        } else {
            return false;
        }
    }, d.slot);
}

} // 命名空间结束。

// ── 读取 ─────────────────────────────────────────────────────────────────

namespace {

template <class C>
std::string get_impl(const C& c, const SettingDef& d) {
    std::string out;
    (void)with_slot(c, d, [&](const auto& value) {
        using M = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<M, bool>)
            out = value ? "true" : "false";
        else if constexpr (std::is_same_v<M, int>)
            out = std::to_string(value);
        else if constexpr (std::is_same_v<M, float> || std::is_same_v<M, double>)
            out = fmt_real(static_cast<double>(value));
        else
            out = value;   // std::string (Enum)
    });
    return out;
}

} // 命名空间结束。

std::string get(const store::RagConfig& c, const SettingDef& d) {
    return get_impl(c, d);
}

std::string get(const smart::RoleConfig& s, const SettingDef& d) {
    return get_impl(s, d);
}

std::string get(const web_search_cfg::Config& c, const SettingDef& d) {
    return get_impl(c, d);
}

std::string get(const paste_cfg::Config& c, const SettingDef& d) {
    return get_impl(c, d);
}

// ── Write ────────────────────────────────────────────────────────────────

namespace {

template <class C>
bool set_impl(C& c, const SettingDef& d, std::string_view value) {
    bool ok = false;
    const bool reached = with_slot(c, d, [&](auto& field) {
        using M = std::decay_t<decltype(field)>;

        if constexpr (std::is_same_v<M, bool>) {
            bool b{};
            if (!parse_bool(value, b)) return;
            field = b;
            ok = true;
        }
        else if constexpr (std::is_same_v<M, int>) {
            try {
                const long long n = std::stoll(std::string{value});
                // 被打得团团转,从未被拒绝 -then-forgotten : 配置不能
                // 显示 HOLD 出局时 -of-range 值。
                field = static_cast<int>(
                    std::clamp<double>(static_cast<double>(n), d.min, d.max));
                ok = true;
            } catch (...) {}
        }
        else if constexpr (std::is_same_v<M, float> || std::is_same_v<M, double>) {
            try {
                const double x = std::stod(std::string{value});
                field = static_cast<M>(std::clamp(x, d.min, d.max));
                ok = true;
            } catch (...) {}
        }
        else if (d.type == Type::Text) {
            // 自由文本:任何值都是可代表的,所以写作总是着陆.
            // Control 字符被丢弃——如果不是被贴上的新线,
            // 则会绕 -trip 输入 settings.json 上方
            // 面板 作为第二行单曲 -line 字段无法显示。
            std::string clean;
            clean.reserve(value.size());
            for (char ch : value)
                if (static_cast<unsigned char>(ch) >= 0x20 && ch != 0x7f)
                    clean.push_back(ch);
            field = std::move(clean);
            ok = true;
        }
        else {
            // 枚举 : 值必须是已宣布的选项之一,
            // 或输入一个字 settings.json
            // 默默地选择不存在的模式,引擎会倒回去而不说.
            std::string_view opts = d.options;
            const std::string want = lower(value);
            while (!opts.empty()) {
                const auto bar = opts.find('|');
                const auto one = opts.substr(0, bar);
                if (lower(one) == want) { field = std::string{one}; ok = true; return; }
                if (bar == std::string_view::npos) break;
                opts.remove_prefix(bar + 1);
            }
        }
    });
    return reached && ok;
}

template <class C>
bool is_default_impl(const C& c, const C& dflt, const SettingDef& d) {
    bool same = true;
    (void)with_slot(c, d, [&](const auto& field) {
        // 到达 相同 默认实例中的成员。
        // with_slot 解决指针对准所交对象,
        // 所以这是对类型进行的一个比较,而不是第二个切换。
        (void)with_slot(dflt, d, [&](const auto& other) {
            if constexpr (std::is_same_v<std::decay_t<decltype(field)>,
                                         std::decay_t<decltype(other)>>)
                same = (field == other);
        });
    });
    return same;
}

template <class C>
void reset_impl(C& c, const C& dflt, const SettingDef& d) {
    (void)with_slot(c, d, [&](auto& field) {
        (void)with_slot(dflt, d, [&](const auto& other) {
            if constexpr (std::is_same_v<std::decay_t<decltype(field)>,
                                         std::decay_t<decltype(other)>>)
                field = other;
        });
    });
}

template <class C>
void apply_env_impl(C& c, const EnvSnapshot& env) {
    for (const auto& d : kSettings) {
        if (d.env.empty()) continue;
        const std::string* raw = env.find(d.env);
        if (!raw) continue;
        // 一个不正确的内存值是 IGNORED ,而不是致命的,
        // 也不是无声的胁迫:运出的默认站,这是对 shell
        // 配置图中的打字机来说最不令人惊讶的行为。 OTHER
        // 配置结构被跳过 with_slot ,
        // 所以每一次超载都精确应用它拥有的行.
        (void)set_impl(c, d, *raw);
    }
}

} // 命名空间结束。

bool set(store::RagConfig& c, const SettingDef& d, std::string_view value) {
    return set_impl(c, d, value);
}

bool set(smart::RoleConfig& s, const SettingDef& d, std::string_view value) {
    return set_impl(s, d, value);
}

bool set(web_search_cfg::Config& c, const SettingDef& d, std::string_view value) {
    return set_impl(c, d, value);
}

bool set(paste_cfg::Config& c, const SettingDef& d, std::string_view value) {
    return set_impl(c, d, value);
}

// ── 默认 ─────────────────────────────────────────────────

bool is_default(const store::RagConfig& c, const SettingDef& d) {
    return is_default_impl(c, defaults(), d);
}

bool is_default(const smart::RoleConfig& s, const SettingDef& d) {
    static const smart::RoleConfig kDefaults{};
    return is_default_impl(s, kDefaults, d);
}

bool is_default(const web_search_cfg::Config& c, const SettingDef& d) {
    static const web_search_cfg::Config kDefaults{};
    return is_default_impl(c, kDefaults, d);
}

bool is_default(const paste_cfg::Config& c, const SettingDef& d) {
    static const paste_cfg::Config kDefaults{};
    return is_default_impl(c, kDefaults, d);
}

void reset(store::RagConfig& c, const SettingDef& d) {
    reset_impl(c, defaults(), d);
}

void reset(smart::RoleConfig& s, const SettingDef& d) {
    static const smart::RoleConfig kDefaults{};
    reset_impl(s, kDefaults, d);
}

void reset(web_search_cfg::Config& c, const SettingDef& d) {
    static const web_search_cfg::Config kDefaults{};
    reset_impl(c, kDefaults, d);
}

void reset(paste_cfg::Config& c, const SettingDef& d) {
    static const paste_cfg::Config kDefaults{};
    reset_impl(c, kDefaults, d);
}

// ── 环境 ─────────────────────────────────────────────

EnvSnapshot read_env(Io) {
    EnvSnapshot e;
    for (const auto& d : kSettings) {
        if (d.env.empty()) continue;
        const std::string var{d.env};
        if (const char* raw = std::getenv(var.c_str()); raw && raw[0])
            e.set.emplace_back(var, raw);
    }
    namespace t = smart::tuning;
    e.smart_enabled        = t::parse_enabled(std::getenv("AGENTTY_SMART_MODE"));
    e.smart_no_internal    = t::parse_disabled(std::getenv("AGENTTY_SMART_NO_INTERNAL"));
    e.smart_no_orchestrate = t::parse_disabled(std::getenv("AGENTTY_SMART_NO_ORCHESTRATE"));
    e.smart_no_subagents   = t::parse_disabled(std::getenv("AGENTTY_SMART_NO_SUBAGENTS"));
    return e;
}

void apply_env(store::RagConfig& c, const EnvSnapshot& env) { apply_env_impl(c, env); }
void apply_env(smart::RoleConfig& s, const EnvSnapshot& env) {
    apply_env_impl(s, env);
    s.no_internal    = env.smart_no_internal;
    s.no_orchestrate = env.smart_no_orchestrate;
    s.no_subagents   = env.smart_no_subagents;
}
void apply_env(Io io, store::RagConfig& c) { apply_env(c, read_env(io)); }
void apply_env(Io io, smart::RoleConfig& s) { apply_env(s, read_env(io)); }
void apply_env(web_search_cfg::Config& c, const EnvSnapshot& env) {
    apply_env_impl(c, env);
    // AGENTTY_WEB_SEARCH 也使用人们所接触的布尔拼写(
    // 0/1/true/false/yes/no 虚实是关闭的;真实的手段 "search available" ,
    // 它就是自动的,而不是打开(On将开始计数)。
    if (const std::string* raw = env.find("AGENTTY_WEB_SEARCH")) {
        const std::string v = lower(*raw);
        bool b{};
        if (v != "on" && v != "off" && parse_bool(v, b))
            c.mode_text = b ? "auto" : "off";
    }
}
void apply_env(Io io, web_search_cfg::Config& c) { apply_env(c, read_env(io)); }
void apply_env(paste_cfg::Config& c, const EnvSnapshot& env) { apply_env_impl(c, env); }
void apply_env(Io io, paste_cfg::Config& c) { apply_env(c, read_env(io)); }

std::string env_override(const SettingDef& d, const EnvSnapshot& env) {
    if (d.env.empty()) return {};
    return env.find(d.env) ? std::string{d.env} : std::string{};
}

} // namespace agentty::settings::registry
