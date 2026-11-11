#pragma once
// agentty::util::env-环境变量agentty
// 读取的闭合目录。将名称集中在这里有三个小但真正的目的：
//
//   1.错字遏制。 `env::name<Var::SocksProxy>()` 是 编译 错误
//      if the enum arm doesn't exist; `std::getenv("AGENTTY_SOCKS_PROXY")`
//      如果你胖-finger的话就是运行时 "huh, my override didn't take"了。
//
//   2、一处审核。 `agentty status` 或未来的 `agentty env`
//      子命令可以迭代kCatalog并报告设置了哪些旋钮——没有
//      忘记旋钮的风险，因为它在某些.cpp中是open-coded。
//
//   3. 编译-time 不变量。底部的`proofs`块证明
//      每个Var臂恰好具有一个目录行并且每个名称都是唯一的。
//      添加不带行的 Var 或重复名称都会导致构建
//      失败。
//
// 这是故意在 std::getenv-调用 站点上的 不 和 运行时 覆盖
// 面板 仍然是 调用 `std::getenv(env::name<X>())`。
// 胜利纯粹在于字符串文字位于一个位置并且类型为-checked。

#include <array>
#include <cstdint>
#include <cstdlib>
#include <string_view>

namespace agentty::util::env {

// ── 关闭的每座环境的集合 var agentty 读作 ────────────────────────────
// 仅在注释中按子系统分组； 枚举 本身是扁平的，
// 因此它的开关必须能够控制每只手臂。
enum class Var : std::uint8_t {
    // ── 认证 ────────────────────────────────────────────────────────────
    AnthropicApiKey,         // ANTHROPIC_API_KEY
    ClaudeOAuthToken,        // CLAUDE_CODE_OAUTH_TOKEN

    // ── 网络/ 拨号超常 ────────────────────────────────────────
    SocksProxy,              // AGENTTY_SOCKS_PROXY (主机:端口)
    ApiHost,                 // AGENTTY_API_HOST (主机[:端口]覆盖)
    OAuthHost,               // AGENTTY_OAUTH_HOST (主机[:端口]覆盖)
    Insecure,                // AGENTTY_INSECURE=1(跳过TLS验证)
    NoPrewarm,               // AGENTTY_NO_PREWARM=1（抑制预热拨号）

    // ── 空格( 平顶侧) ────────────────────────────────────────────
    AirgapSsh,               // AGENTTY_AIRGAP_SSH (额外的 ssh 标志)

    // ── 调试 ─────────────────────────────────────────────────────────────────────

    // ── 智能模式会话标语 ────────────────────────────────
    SmartMode,               // AGENTTY_SMART_MODE = 0|1 (会话主机-switch引脚）
};

struct VarSpec {
    Var              tag;
    std::string_view name;     // 规范环境变量名称（getenv 看到的内容）
};

inline constexpr std::array kCatalog = {
    VarSpec{Var::AnthropicApiKey,  "ANTHROPIC_API_KEY"},
    VarSpec{Var::ClaudeOAuthToken, "CLAUDE_CODE_OAUTH_TOKEN"},
    VarSpec{Var::SocksProxy,       "AGENTTY_SOCKS_PROXY"},
    VarSpec{Var::ApiHost,          "AGENTTY_API_HOST"},
    VarSpec{Var::OAuthHost,        "AGENTTY_OAUTH_HOST"},
    VarSpec{Var::Insecure,         "AGENTTY_INSECURE"},
    VarSpec{Var::NoPrewarm,        "AGENTTY_NO_PREWARM"},
    VarSpec{Var::AirgapSsh,        "AGENTTY_AIRGAP_SSH"},
    VarSpec{Var::SmartMode,        "AGENTTY_SMART_MODE"},
};

// 编译-time名称查找。 `env::name<Var::SocksProxy>()`
// 返回文字 `"AGENTTY_SOCKS_PROXY"`。 枚举 臂拼写错误是 编译 错误；
// 将结果与 std::getenv 一起使用仍为 one-liner。
template <Var V>
[[nodiscard]] consteval std::string_view name() noexcept {
    for (const auto& s : kCatalog)
        if (s.tag == V) return s.name;
    // 在well-formed构建中无法到达——proofs::every_var_has_row将其固定。
    return {};
}

// 方便：读取var，如果不存在或为空则返回nullptr。保留 调用 站点，
// 使 "treat empty as unset" 免受 re-implementing 的检查。
template <Var V>
[[nodiscard]] inline const char* get_or_null() noexcept {
    const char* v = std::getenv(name<V>().data());
    return (v && *v) ? v : nullptr;
}

// ── 编译-time 证明 ─────────────────────────────────────────────────
namespace proofs {

// 每个 Var 枚举器在 kCatalog 中恰好只有一行。
consteval bool every_var_has_row() {
    constexpr Var kAll[] = {
        Var::AnthropicApiKey, Var::ClaudeOAuthToken,
        Var::SocksProxy, Var::ApiHost, Var::OAuthHost, Var::Insecure,
        Var::NoPrewarm,
        Var::AirgapSsh,
        Var::SmartMode,
    };
    if (std::size(kAll) != kCatalog.size()) return false;
    for (auto v : kAll) {
        int hits = 0;
        for (const auto& s : kCatalog) if (s.tag == v) ++hits;
        if (hits != 1) return false;
    }
    return true;
}
static_assert(every_var_has_row(),
              "agentty::util::env::Var and kCatalog must be in bijection — "
              "every Var arm needs exactly one row in kCatalog");

// 没有重复的环境变量名称。
consteval bool all_names_unique() {
    for (std::size_t i = 0; i < kCatalog.size(); ++i)
        for (std::size_t j = i + 1; j < kCatalog.size(); ++j)
            if (kCatalog[i].name == kCatalog[j].name) return false;
    return true;
}
static_assert(all_names_unique(),
              "duplicate env var name in agentty::util::env::kCatalog");

// 每个名称都遵循`AGENTTY_*`/`ANTHROPIC_*`/
// `CLAUDE_*`约定。捕获诸如小写或杂散-space名称之类的拼写错误。
consteval bool names_well_formed() {
    constexpr std::string_view kPrefixes[] = {
        "AGENTTY_", "ANTHROPIC_", "CLAUDE_",
    };
    for (const auto& s : kCatalog) {
        bool ok = false;
        for (auto p : kPrefixes)
            if (s.name.size() > p.size() && s.name.starts_with(p)) { ok = true; break; }
        if (!ok) return false;
        for (char c : s.name)
            if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
                return false;
    }
    return true;
}
static_assert(names_well_formed(),
              "env var names must be upper-snake-case with an "
              "AGENTTY_/ANTHROPIC_/CLAUDE_ prefix");

// 保守查找的Spot-checks。
static_assert(name<Var::SocksProxy>()       == "AGENTTY_SOCKS_PROXY");
static_assert(name<Var::AnthropicApiKey>()  == "ANTHROPIC_API_KEY");
static_assert(name<Var::ClaudeOAuthToken>() == "CLAUDE_CODE_OAUTH_TOKEN");

} // namespace proofs

} // namespace agentty::util::env
