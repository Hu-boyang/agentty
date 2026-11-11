#pragma once
// agentty::Profile —用户正在运行的权限层。
// 由工具域（`needs_permission(Profile)`)
// 和提供商域（当呈现权限-requiring工具时）读取。

#include <cstdint>
#include <string_view>

namespace agentty {

enum class Profile : std::uint8_t { Write, Ask, Minimal };

// Write、MUST保持枚举的零值。两个交付路径依赖于它：default
// -constructed store::Settings 使用
// Profile::Write，persistence::load_settings
// () 通过值（"profile", 0）解决缺少的 "profile" 键 —
// 因此全新安装 和 和旧配置都默认为 Write（自治，由 沙箱 + 工作区 边界包含）。
// 这是记录在 /docs/profiles 的有意产品决策；重新排序此 枚举 会
// 默默地翻转默认值并取消同步文档。也被 tests/settings_default_test.cpp
// 固定在 运行时 上。
static_assert(static_cast<std::uint8_t>(Profile::Write) == 0,
              "Profile::Write must remain the enum's zero value.");

[[nodiscard]] constexpr std::string_view to_string(Profile p) noexcept {
    switch (p) {
        case Profile::Write:   return "Write";
        case Profile::Ask:     return "Ask";
        case Profile::Minimal: return "Minimal";
    }
    return "?";
}

} // namespace agentty
