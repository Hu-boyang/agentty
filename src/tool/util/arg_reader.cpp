#include "agentty/tool/util/arg_reader.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <limits>
#include <span>

namespace agentty::tools::util {

using nlohmann::json;

namespace {
// 当模型的工具发出时, 接受其通用的别名 -use 模式定义 过关
// -trained 与 Anthropic 已经建成 -in text_editor
// (`file_path` ),或缩写。查找就是例 -sensitive 速度;
// 假名是逃生舱。 保持这个桌子很小, 每个失事者都支付步行费用。
constexpr std::string_view kAliasesPath[]       = {"file_path", "filepath", "filename"};
constexpr std::string_view kAliasesFilePath[]   = {"path",      "filepath", "filename"};
constexpr std::string_view kAliasesOldString[]  = {"old_str",   "oldStr"};
constexpr std::string_view kAliasesNewString[]  = {"new_str",   "newStr"};
// Anthropic 已经建成 -in text_editor 工具使用 `file_text`
// 用于创建 -command 内容。 模式交叉 -trained 说到这个 file_text
// 即便我们 模式定义 说 "content" - 没有这个化名文件会空地降落。
constexpr std::string_view kAliasesContent[]    = {"file_text", "text",
                                                    "file_content", "contents",
                                                    "body", "data"};
// 赛德的 read_file_tool 使用 `start_line`
// / `end_line` 我们接受一个在Zed表面训练的模型,
// 不会页面 -fault 对我们来说。
constexpr std::string_view kAliasesOffset[]     = {"start_line", "start", "from_line"};
constexpr std::string_view kAliasesLimit[]      = {"end_line",   "num_lines", "max_lines",
                                                    "count",      "line_count"};
// Terminal/shell — `cd` / `cwd` / `workdir` / `working_directory` 都来了
constexpr std::string_view kAliasesCd[]         = {"cwd", "workdir", "working_directory",
                                                    "directory"};
constexpr std::string_view kAliasesCommand[]    = {"cmd", "shell_command"};

std::span<const std::string_view> aliases_for(std::string_view key) noexcept {
    if (key == "path")       return {kAliasesPath};
    if (key == "file_path")  return {kAliasesFilePath};
    if (key == "old_string") return {kAliasesOldString};
    if (key == "new_string") return {kAliasesNewString};
    if (key == "content")    return {kAliasesContent};
    if (key == "offset")     return {kAliasesOffset};
    if (key == "limit")      return {kAliasesLimit};
    if (key == "cd")         return {kAliasesCd};
    if (key == "command")    return {kAliasesCommand};
    return {};
}
} // 命名空间结束。

const json* ArgReader::raw(std::string_view key) const noexcept {
    if (!args_.is_object()) return nullptr;
    if (auto it = args_.find(std::string{key}); it != args_.end())
        return &*it;
    for (auto alt : aliases_for(key)) {
        if (auto it = args_.find(std::string{alt}); it != args_.end())
            return &*it;
    }
    return nullptr;
}

std::string ArgReader::str(std::string_view key,
                           std::string def,
                           std::string* note) const {
    const json* v = raw(key);
    if (!v) return def;
    if (v->is_string()) return v->get<std::string>();
    if (v->is_null()) return def;
    if (v->is_array()) {
        std::string out;
        for (std::size_t i = 0; i < v->size(); ++i) {
            if (i) out += '\n';
            const auto& el = (*v)[i];
            if (el.is_string()) out += el.get<std::string>();
            else                out += el.dump();
        }
        if (note) *note = " (" + std::string{key} + " was an array — joined with newlines)";
        return out;
    }
    // 数字 / bul / 对象 – 表面为字形的 repr,
    // 这样模型就可以看到它发送到重试上并正确.
    std::string out = v->dump();
    if (note) *note = " (" + std::string{key} + " was not a string — coerced)";
    return out;
}

std::optional<std::string> ArgReader::require_str(std::string_view key) const {
    const json* v = raw(key);
    if (!v || v->is_null()) return std::nullopt;
    if (v->is_string()) {
        auto s = v->get<std::string>();
        return s.empty() ? std::nullopt : std::optional<std::string>{std::move(s)};
    }
    // Numbers/bools 被胁迫通过 dump() - 表面仍为 "has a value".
    auto s = v->dump();
    return s.empty() ? std::nullopt : std::optional<std::string>{std::move(s)};
}

int ArgReader::integer(std::string_view key, int def) const {
    const json* v = raw(key);
    if (!v || v->is_null()) return def;
    if (v->is_number_integer()) {
        // 关机 -of-range JSON 整数到英寸边界,而不是让其得到 <int>()
        // narrow/wrap (模型 -supplied 永远不要 UB).
        std::int64_t n = v->get<std::int64_t>();
        if (n > std::numeric_limits<int>::max()) return std::numeric_limits<int>::max();
        if (n < std::numeric_limits<int>::min()) return std::numeric_limits<int>::min();
        return static_cast<int>(n);
    }
    if (v->is_number_float()) {
        // floo - > 英寸值为 UB,当值值在英寸范围之外; 夹在先。
        double d = v->get<double>();
        if (d >= static_cast<double>(std::numeric_limits<int>::max()))
            return std::numeric_limits<int>::max();
        if (d <= static_cast<double>(std::numeric_limits<int>::min()))
            return std::numeric_limits<int>::min();
        return static_cast<int>(d);
    }
    if (v->is_string()) {
        const auto& s = v->get_ref<const std::string&>();
        int out = def;
        auto r = std::from_chars(s.data(), s.data() + s.size(), out);
        if (r.ec == std::errc{}) return out;
    }
    return def;
}

bool ArgReader::boolean(std::string_view key, bool def) const {
    const json* v = raw(key);
    if (!v || v->is_null()) return def;
    if (v->is_boolean()) return v->get<bool>();
    if (v->is_number_integer()) return v->get<int>() != 0;
    if (v->is_string()) {
        std::string s = v->get<std::string>();
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        if (s == "true" || s == "1" || s == "yes")  return true;
        if (s == "false"|| s == "0" || s == "no")   return false;
    }
    return def;
}

} // namespace agentty::tools::util
