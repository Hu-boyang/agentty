#pragma once
// agentty::foreign-其会话agentty可以打开的工具的名称。
// 纯：对于视图和状态更新函数来说是安全的。

#include <optional>
#include <string>
#include <string_view>

namespace agentty::foreign {

enum class Source : std::uint8_t { Codex, ClaudeCode };

[[nodiscard]] constexpr std::string_view key_of(Source s) noexcept {
    return s == Source::Codex ? "codex" : "claude";
}
[[nodiscard]] constexpr std::string_view name_of(Source s) noexcept {
    return s == Source::Codex ? "Codex" : "Claude Code";
}
[[nodiscard]] constexpr std::optional<Source> parse_source(std::string_view k) noexcept {
    if (k == "codex") return Source::Codex;
    if (k == "claude") return Source::ClaudeCode;
    return std::nullopt;
}
// 存储的密钥的显示名称，或者当未知时密钥本身。
[[nodiscard]] inline std::string display_name(std::string_view k) {
    if (auto s = parse_source(k)) return std::string{name_of(*s)};
    return std::string{k};
}

}  // namespace agentty::foreign
