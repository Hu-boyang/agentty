#pragma once
// win_shell_encode-Ctrl+G代码
// -block运行者的Windows路径使用的纯平台-independent PowerShell命令编码器。
//
// PowerShell块经由`powershell -EncodedCommand <base64>`运行，
// 其中base64是脚本的UTF-16LE(little-endian)字节。
// 这是 PowerShell 的记录合约：-EncodedCommand 避开了
// cmd.exe / argv 引用的每一层（嵌套引号、换行符、`$vars`、
// `&|<>` 都未受影响地通过），因为整个脚本是一个不透明的 base64 参数。
//
// 这存在于 头文件 中，而不是在 codeblock.cpp 中埋藏 #if _WIN32，
// 因此编码器可以是 任意 平台上的单元 -tested。这里的一个错误（错误
// 的字节序、错误的 base64 填充）会默默地损坏 Windows 上
// 的 每个 PowerShell 块，并且该分支无法在 Linux/macOS
// dev+CI 主机上编译；使转换变得纯粹+可测试是覆盖它的诚实方法。

#include <cstddef>
#include <string>

namespace agentty::win_shell {

// base64-encode `bytes`（标准字母表，'=' 填充）。纯的;没有部门。
// `constexpr`所以整个转换是编译-time可评估的，
// 这使得该头文件底部的已知-answer self-check在构建时在EVERY平台上运行
// （参见static_assert块）——Windows运行器路径不能在Linux/macOS
// CI主机上编译，因此编译-time 证明是唯一的 cross
// -platform 防范措施，防止默默地破坏编码脚本。
[[nodiscard]] constexpr std::string base64(const std::string& bytes) {
    static constexpr char kB64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string enc;
    enc.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const unsigned b0 = static_cast<unsigned char>(bytes[i]);
        const unsigned b1 = i + 1 < bytes.size() ? static_cast<unsigned char>(bytes[i + 1]) : 0u;
        const unsigned b2 = i + 2 < bytes.size() ? static_cast<unsigned char>(bytes[i + 2]) : 0u;
        const unsigned triple = (b0 << 16) | (b1 << 8) | b2;
        enc.push_back(kB64[(triple >> 18) & 0x3F]);
        enc.push_back(kB64[(triple >> 12) & 0x3F]);
        enc.push_back(i + 1 < bytes.size() ? kB64[(triple >> 6) & 0x3F] : '=');
        enc.push_back(i + 2 < bytes.size() ? kB64[triple & 0x3F] : '=');
    }
    return enc;
}

// 将 `body` (视为原始 字节 的 数据流，一个字符 = 一个 UTF-16
// 代码单元，这对于 ASCII 脚本完全正确，对于其余的 字节-faithful)
// 扩大到 UTF-16LE 字节：低 字节 然后高 字节每个代码单元。
[[nodiscard]] constexpr std::string to_utf16le_bytes(const std::string& body) {
    std::string bytes;
    bytes.reserve(body.size() * 2);
    for (unsigned char c : body) {
        bytes.push_back(static_cast<char>(c));   // 低字节
        bytes.push_back('\0');                    // 高字节(LE)
    }
    return bytes;
}

// 完整的 `-EncodedCommand` 载荷 (UTF-16LE 的 base64)，
// 没有 `powershell` 前缀，因此测试可以直接断言 载荷。
[[nodiscard]] constexpr std::string encoded_command(const std::string& body) {
    return base64(to_utf16le_bytes(body));
}

// Windows运行者为PowerShell块生成的完整命令行。
[[nodiscard]] constexpr std::string powershell_command(const std::string& body) {
    return "powershell -NoProfile -ExecutionPolicy Bypass -EncodedCommand "
         + encoded_command(body);
}

// ---------------------------------------------------------------------------
// 编译-time已知-answer测试。这些向量是用记录的真实值 python3 -c
// 'import base64;print(base64.b64encode(S.encode("utf-16-le")))'
//     python3 -c 'import base64;print(base64.b64encode(S.encode("utf-16-le")))'
// ToBase64String([Text.Encoding]::
//     Unicode.GetBytes(S)) 因为 encoded_command
// 是 constexpr，所以这些 static_asserts FAIL 该 BUILD —
// 上Linux、macOS 和 Windows 类似 — 如果字节序或 base64 填充发生倒退。
// 无需运行 ctest；一个简单的 `cmake --build` 证明了这一点，
// 因为这个 头文件 包含在always-compiled codeblock.cpp TU 中。
namespace detail {
consteval bool kat(const char* body, const char* want) {
    return encoded_command(std::string(body)) == std::string(want);
}
static_assert(kat("", ""),                                    "empty");
static_assert(kat("A", "QQA="),                               "single char / 2-pad");
static_assert(kat("AB", "QQBCAA=="),                          "1-pad boundary");
static_assert(kat("echo hi", "ZQBjAGgAbwAgAGgAaQA="),         "ascii + space");
static_assert(kat("Write-Host \"x\"",
                  "VwByAGkAdABlAC0ASABvAHMAdAAgACIAeAAiAA=="), "quotes / cmdlet");
static_assert(kat("a\nb", "YQAKAGIA"),                        "embedded newline");
} // namespace detail

} // namespace agentty::win_shell
