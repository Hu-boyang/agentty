// web_search_config.cpp — web_search策略：纯请求重写。

#include "agentty/domain/web_search_config.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace agentty::web_search_cfg {

namespace {

[[nodiscard]] std::string lower(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) out.push_back(static_cast<char>(std::tolower(c)));
    return out;
}

// 主机标签集：ASCII字母、数字、'-'和'.
// '、至少一个点、无空标签、无以'-'开头或结尾的标签。 Non-ASCII 被
// 拒绝而不是猜测 - punycode 拼写（"xn--…"）有效，
// 原始 IDN 将作为 字节 进入引擎，它可能会或可能不会以同样的方式 归并。
[[nodiscard]] bool plausible_host(std::string_view h) {
    if (h.empty() || h.size() > 253) return false;
    if (h.find('.') == std::string_view::npos) return false;
    std::size_t start = 0;
    while (start <= h.size()) {
        const auto dot = h.find('.', start);
        const auto label = h.substr(start, dot == std::string_view::npos
                                               ? std::string_view::npos
                                               : dot - start);
        if (label.empty() || label.size() > 63) return false;
        if (label.front() == '-' || label.back() == '-') return false;
        for (char ch : label) {
            const auto c = static_cast<unsigned char>(ch);
            if (!(std::isalnum(c) && c < 0x80) && ch != '-') return false;
        }
        if (dot == std::string_view::npos) break;
        start = dot + 1;
    }
    return true;
}

// "https://www.Example.com:8080/path?q"→
// "example.com"。用户可能从地址栏粘贴的所有内容都会简化为 `site:`
// 操作员想要的裸主机。
[[nodiscard]] std::string normalise(std::string_view raw) {
    std::string s = lower(raw);
    if (auto p = s.find("://"); p != std::string::npos) s.erase(0, p + 3);
    // 出于习惯，领先"-site:"/"site:" 带类型的。
    for (std::string_view pfx : {"-site:", "site:"})
        if (s.starts_with(pfx)) { s.erase(0, pfx.size()); break; }
    if (auto p = s.find_first_of("/?#"); p != std::string::npos) s.resize(p);
    if (auto p = s.find('@'); p != std::string::npos) s.erase(0, p + 1);
    if (auto p = s.find(':'); p != std::string::npos) s.resize(p);
    while (!s.empty() && s.back() == '.') s.pop_back();
    if (s.starts_with("www.")) s.erase(0, 4);
    return s;
}

} // 命名空间结束。

Mode mode(const Config& c) {
    if (c.mode_text == "on")  return Mode::On;
    if (c.mode_text == "off") return Mode::Off;
    return Mode::Auto;
}

std::vector<std::string> exclude_list(std::string_view text) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size()
               && (text[i] == ' ' || text[i] == ',' || text[i] == '\t'
                   || text[i] == '\n' || text[i] == ';'))
            ++i;
        const std::size_t begin = i;
        while (i < text.size()
               && text[i] != ' ' && text[i] != ',' && text[i] != '\t'
               && text[i] != '\n' && text[i] != ';')
            ++i;
        if (i == begin) continue;
        std::string host = normalise(text.substr(begin, i - begin));
        if (!plausible_host(host)) continue;
        if (std::ranges::find(out, host) != out.end()) continue;
        out.push_back(std::move(host));
    }
    return out;
}

int effective_count(const Config& c, const nlohmann::json& args) {
    const int hi = std::max(kCountMin, c.max_count);
    int n = std::clamp(c.count, kCountMin, hi);
    if (args.is_object()) {
        if (auto it = args.find("count"); it != args.end()) {
            // 模型以整数、浮点数和字符串等形式发送数字；采取其中任何一个，
            // 并回退到其他任何值的默认值（bool、null、
            // object、"lots"、"7abc"），而不是让格式
            // 错误的值达到 mcp-cpp。永远不会抛出：超过
            // INT64_MAX 的无符号值或一个巨大的浮点钳位。
            if (it->is_number_unsigned()) {
                const auto v = it->get<unsigned long long>();
                n = v > static_cast<unsigned long long>(hi)
                        ? hi
                        : std::max(kCountMin, static_cast<int>(v));
            } else if (it->is_number_integer()) {
                const auto v = it->get<long long>();
                n = static_cast<int>(std::clamp<long long>(v, kCountMin, hi));
            } else if (it->is_number_float()) {
                const double v = it->get<double>();
                if (std::isfinite(v))
                    n = static_cast<int>(std::clamp<double>(std::trunc(v), kCountMin, hi));
            } else if (it->is_string()) {
                const std::string& s = it->get_ref<const std::string&>();
                try {
                    std::size_t used = 0;
                    const long long v = std::stoll(s, &used);
                    // 整个字符串必须是数字（周围的空格
                    // 除外）："7abc"不是7。
                    bool clean = true;
                    for (std::size_t i = used; i < s.size(); ++i)
                        if (!std::isspace(static_cast<unsigned char>(s[i]))) clean = false;
                    if (clean)
                        n = static_cast<int>(std::clamp<long long>(v, kCountMin, hi));
                } catch (...) {}
            }
        }
    }
    return n;
}

std::string apply_exclusions(std::string query,
                             const std::vector<std::string>& sites) {
    if (sites.empty()) return query;
    // 查询已携带标记，lower-cased，
    // 因此模型自身编写的排除不会被第二次附加。
    std::vector<std::string> have;
    {
        const std::string q = lower(query);
        std::size_t i = 0;
        while (i < q.size()) {
            while (i < q.size() && std::isspace(static_cast<unsigned char>(q[i]))) ++i;
            const std::size_t b = i;
            while (i < q.size() && !std::isspace(static_cast<unsigned char>(q[i]))) ++i;
            if (i > b) have.emplace_back(q.substr(b, i - b));
        }
    }
    for (const auto& s : sites) {
        std::string op = "-site:" + s;
        if (std::ranges::find(have, op) != have.end()) continue;
        if (!query.empty() && !std::isspace(static_cast<unsigned char>(query.back())))
            query.push_back(' ');
        query += op;
        have.push_back(std::move(op));
    }
    return query;
}

nlohmann::json rewrite_args(const Config& c, nlohmann::json args) {
    if (!args.is_object()) return args;   // mcp-cpp报告形状错误
    // Auto 将计数留给模型，如 面板 存在之前一样。
    // 只有“开”才能解决并限制它。
    if (mode(c) == Mode::On)
        args["count"] = effective_count(c, args);
    if (auto it = args.find("query"); it != args.end() && it->is_string()) {
        const auto sites = exclude_list(c.exclude_sites);
        if (!sites.empty())
            *it = apply_exclusions(it->get<std::string>(), sites);
    }
    return args;
}

bool dial_entry_ok(std::string_view key, std::string_view value) noexcept {
    if (key.empty() || key.size() > 64 || value.empty() || value.size() > kMaxDialValueBytes)
        return false;
    const auto dot = key.find('.');
    if (dot == std::string_view::npos || dot == 0 || dot + 1 == key.size()) return false;
    auto printable = [](std::string_view s) {
        for (char ch : s) {
            const auto u = static_cast<unsigned char>(ch);
            if (u < 0x20 || u == 0x7f) return false;
        }
        return true;
    };
    return printable(key) && printable(value);
}

} // namespace agentty::web_search_cfg
