#pragma once
// UTF-8验证+修复。 Windows 上的子进程输出是 cmd.exe/PowerShell
// 选择的任何代码页（通常是 OEM/CP1252），但 nlohmann::json
// 在任何非 -UTF-8 字节 上抛出 type_error.316。我们交给 API
// 的每根绳子都必须在捕获边界处穿过 to_valid_utf8，并且最好在
// 序列化 处再次穿过（belt-and-suspenders 表示绕过我们的路径）。

#include <cstddef>
#include <string>
#include <string_view>

namespace agentty::tools::util {

// RFC 3629 扫描。正确当且仅当 `s` 中的每个 字节 序列都解码为
// 有效的代码点（无超长、无代理、无 > U+10FFFF）。
[[nodiscard]] bool is_valid_utf8(std::string_view s) noexcept;

// 将 `s` 中的每个无效 字节 序列替换为 U+FFFD (0xEF 0xBF 0xBD)。
// 有效字节通过不变。
[[nodiscard]] std::string sanitize_utf8(std::string_view s);

// 返回保证UTF-8有效的`s`的副本。按顺序尝试：已有效 → 从
// GetConsoleOutputCP() 转码 → 从 CP_ACP 转码
// → 字节-level 清理。对于非 -Windows，仅适用第一步和最后一步。
[[nodiscard]] std::string to_valid_utf8(std::string s);

// 返回[0，max_bytes]中的最大位置`cut`，使得`s[0, cut)`
// 确实NOT分割了multi-byte UTF-8序列。
// 当`s.size() <= max_bytes`返回`s.size()`时（没有截断）。
// 从不抛出，从不分配。在每个 字节-granularity 截断站点使用（文件读
// 取的大小上限、HTTP 正文、捕获的标准输出、线程 标题、工具输出限制），
// 因此生成的前缀仍然可以安全地交给 nlohmann::json::dump()，
// 而不会触发其 UTF-8 type_error.316。
[[nodiscard]] std::size_t safe_utf8_cut(std::string_view s, std::size_t max_bytes) noexcept;

// 将 终端 line-discipline 应用到捕获的子流程输出，
// 以便原始控制 字节 永远无法到达 UI 存储单元 网格或模型：
//   ·ESC序列被整个消耗——CSI到其最后的字节
//     (0x40-0x7E)，OSC/DCS/SOS/PM/APC到BEL或ST，
//     两个-byte ESC+X对。 缓冲区 末尾留下的 INCOMPLETE
//     序列（进度 快照 可以剪切 -CSI 中间）将被删除，
//     而不是作为文字参数 字节 传递。
//   ·单独的\r应用OVERWRITE语义（进度条折叠到
//     它们的最终状态）； \r\n 标准化为 \n。
//   · \b 删除当前行上的前一个代码点。
//   ·每隔一个C0、字节和DEL被丢弃； \n 和 \t 被保留。
// 字节-transparent外部控制序列；
// 与 to_valid_utf8 配对以保证编码。镜像 mcp-cpp 的实现。
[[nodiscard]] std::string strip_terminal_controls(std::string_view in);

// live-output 快照工具卡的部分显示：从末尾至多`max_bytes`，
// 在一行和代码-point边界上开始。工具每次都会报告其完整的缓冲区；
// 在这里，在 工作线程 上进行切割，可以使 UI 线程 的
// per-snapshot 工作受到限制，无论输出有多长。
// 内联：exec.cpp使用它，并且对于一个小的纯函数，
// 构建exec.cpp的狭窄测试目标不应该必须是链接 utf8.cpp。
inline constexpr std::size_t kProgressKeep = 16 * 1024;
[[nodiscard]] inline std::string_view progress_tail(std::string_view all,
                                                    std::size_t max_bytes = kProgressKeep) noexcept {
    if (all.size() <= max_bytes) return all;
    std::size_t cut = all.size() - max_bytes;
    // 优选行的开头：半行（或半个转义序列，
    // strip_terminal_controls将破坏）读取为垃圾。
    if (const auto nl = all.find('\n', cut); nl != std::string_view::npos && nl + 1 < all.size())
        return all.substr(nl + 1);
    while (cut < all.size() && (static_cast<unsigned char>(all[cut]) & 0xC0) == 0x80) ++cut;
    return all.substr(cut);
}

} // namespace agentty::tools::util
