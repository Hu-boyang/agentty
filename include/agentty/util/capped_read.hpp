#pragma once
// agentty::util::capped_read — 读取具有大小限制的用户文件，
// 其形式是忽略 "too big" 不会忽略 编译。
//
// ── 删除的错误 ─────────────────────────────────────────────────
//
// 四个文件独立编写了同一个助手：
//
//     std::string read_capped(const fs::path& p, std::size_t 上限) {
//         if (!is_regular_file(p)) return {};
//         if (size > cap)          return {};     // <-- indistinguishable
//         ...
//     }
//
// 并且每个人都带来了相同的错误，因为空字符串同时回答
// 了三个不同的问题：不存在、不可读以及太大而无法使用。
// 呼叫者写了 `if (raw.empty()) continue;`—
// —前两个是正确的，第三个是默默地错误的。测量而非猜测：
//
//   技能超大的SKILL.md随着`0 warning(s)`一起消失。
//             作者的技巧并不存在，也没有说明为什么。
//   钩子`agentty hooks`在该文件处打印了"no hooks file"，
//             因此写入BLOCK的PreToolUse
//             钩子是惰性的，而用户相信它已武装。
//   AGENTS.md在64 KiB处切割，没有标记，
//             因此靠近底部的规则从未到达模型，而作者观察他们的文件的
//             顶部采取副作用。
//
// 三个单独的发现，三个单独的修复。这就是说明：在三个文件
// 中修复了三次的错误是一种缺失类型。
//
// ── 为什么这是个 变体 而不是一个 枚举 + 字符串 ───────────────────────
//
// jaal的第一原则：
// 契约应由类型表达。规则若能写成概念、类型状态或私有构造函数，就应这样做；违反时应在编译阶段失败，并指出具体规则。
//
//
// 字符串旁边的 枚举 比它看起来要弱——无论 `c.kind` 说什么，
// `c.text` 都是可到达的，所以合并仍然是 WRITABLE，
// 并且只有当审阅者不断注意到时，该规则才存在。 变体 使结果
// 可供选择：在您说出自己所处的情况之前，没有 `.text`
// 可供读取，而省略一个替代方案的 lambda 集上的
// `visit` 是命名您忘记的类型的 编译 错误。
//
// 所以这四种状态就是四种类型：
//
//     内容字节，
//     在上限内没有任何内容。普通的。
//     存在不可读，但读取失败 TooBig 存在，
//     可读，超出限制 — 绝不 无提示
//
// ── 为什么不呢 std::expected<std::string, Error> ────────────────────────────
//
// TooBig不是"something went wrong"——文件没问题，
// agentty的限制是约束——并且每个调用者以不同的方式回答它：
// 技能保留报告存根，挂钩命名路径并退出1，AGENTS.md截断WITH标记而不是拒绝。
// `expected` 将所有三个合并为一个
// `if (!r) return;`，这正是导致该错误的崩溃。

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <variant>

namespace agentty::util {

// ── 四种结果,作为类型 ──────────────────────────────────────────

// 字节，在帽内。 空 文件位于此处，而不是缺席：
// "the user wrote an empty hooks.json" 和
// "there is no hooks.json" 是不同的事实，想要以相同方式对待它们的调用者可以这样说。
struct Content { std::string text; };

// 没有这样的文件，或者不是常规文件。调用者可以跳过而不发表
// 评论的一种情况。
//
// DIRECTORY是故意不存在的。 `std::ifstream` OPENS 在
// Linux 上成功创建一个目录，并在第一次读取时抛出异常，导致此进程崩溃一次（
// `basic_filebuf::underflow error reading the file: Is a directory`，
// 来自 `mkdir .agentty/mcp.json`）。
// 前面的 is_regular_file 检查使得它变得不可能，而不是仅仅不可能。
struct Absent {};

// 存在，但打开或统计失败。 权限，带有删除的 竞态，
// 一个消失的设备。
struct Unreadable {};

// 存在且可读以及OVER 该 LIMIT。携带实际大小，
// 以便呼叫者可以说出距离多远，这是 "your file is too big"
// 和用户可以操作的数字之间的差异。
struct TooBig { std::uintmax_t size = 0; };

// 正是四个之一。没有默认的-reachable `.text`，
// 因此调用者在没有先说明他们所处的情况的情况下无法读取字节。
using Capped = std::variant<Content, Absent, Unreadable, TooBig>;

// ── 阅读 ──────────────────────────────────────────────────────────────

[[nodiscard]] inline Capped capped_read(const std::filesystem::path& p,
                                        std::size_t cap) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (p.empty() || !fs::is_regular_file(p, ec) || ec) return Absent{};

    const auto sz = fs::file_size(p, ec);
    if (ec)        return Unreadable{};
    if (sz == 0)   return Content{};
    if (sz > cap)  return TooBig{sz};

    std::ifstream f(p, std::ios::binary);
    if (!f) return Unreadable{};
    std::string buf(static_cast<std::size_t>(sz), '\0');
    f.read(buf.data(), static_cast<std::streamsize>(sz));
    buf.resize(static_cast<std::size_t>(f.gcount()));
    return Content{std::move(buf)};
}

// ── 阅读结果 ───────────────────────────────────────────────────
//
// `bytes_or_empty`是ONE有损存取器，其命名是为了让您
// 不会意外触及它。它是为少数 调用 站点而存在的，这些站点真正以同样的
// 方式对待所有故障（仅询问 "is there anything here"
// 的探测）。在其他地方，请访问 — 编译器 不会让您忘记
// TooBig。
[[nodiscard]] inline const std::string& bytes_or_empty(const Capped& c) {
    static const std::string kEmpty;
    const auto* got = std::get_if<Content>(&c);
    return got ? got->text : kEmpty;
}

[[nodiscard]] inline bool has_content(const Capped& c) noexcept {
    return std::holds_alternative<Content>(c);
}

// 超过上限时的实际大小，否则为0。用于构建消息。
[[nodiscard]] inline std::uintmax_t oversize_bytes(const Capped& c) noexcept {
    const auto* big = std::get_if<TooBig>(&c);
    return big ? big->size : 0;
}

// 对于 CLI 行或 lint 警告，非 -Content 结果的
// one-line 解释。内容空虚、缺席——两者都无需多言。
[[nodiscard]] inline std::string capped_note(const Capped& c,
                                             std::size_t cap) {
    return std::visit([cap](const auto& v) -> std::string {
        using V = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<V, TooBig>) {
            return "is " + std::to_string(v.size / 1024) + " KB — over the "
                 + std::to_string(cap / 1024)
                 + " KB limit, so it was NOT loaded";
        } else if constexpr (std::is_same_v<V, Unreadable>) {
            return "could not be read";
        } else {
            return {};
        }
    }, c);
}

}  // namespace agentty::util
