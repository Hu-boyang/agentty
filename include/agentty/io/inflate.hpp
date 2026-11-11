#pragma once
// agentty::io —HTTP内容-encoding解码(gzip/zlib/原始DEFLATE)。
//
// agentty的HTTP客户端向服务器请求`accept-encoding: identity`，
// 但是网关可以发送压缩的正文 ANYWAY(z.ai的GLM编码计划无论如何都会对其/models
// JSON进行gzip——问题#30)。如果没有充气机，那些 字节
// 会作为垃圾到达 JSON 解析器：模型列表返回为空，选择面板 没有
// 显示任何内容，没有错误。这对它们进行解码。
//
// Self-contained:规范的RFC 1951 DEFLATE解码器(从rag
// -cpp用于OOXML的相同bounds-safe实现移植)加上RFC 1952
// (gzip)和RFC 1950 (zlib)包装器处理。没有 zlib 依赖性 -
// 气隙路径故意避免 链接 它，并且 ~250-line self-contained
// 解码器使 I/O 层免于对 rag 的向上依赖性。

#include <optional>
#include <string>
#include <string_view>

namespace agentty::io::compress {

// 对原始 RFC 1951 DEFLATE 数据流 充气。 `max_out` 限制了输出，
// 因此 malicious/looping 数据流 无法耗尽 内存（减压炸弹）。
// 在 corrupt/oversized 输入上返回 std::nullopt。
[[nodiscard]] std::optional<std::string> inflate_deflate(
    std::string_view in, std::size_t max_out);

// 根据 `content-encoding` 值解码 HTTP 响应 正文：
//   "gzip" / "x-gzip" → 剥离 RFC 1952 头文件+trailer，
//   然后 DEFLATE "deflate" → RFC 1950 zlib 包装器（如果存在），
//   否则原始 DEFLATE "identity"
// / "" → 返回不变 未知编码返回 std::nullopt（调用者保留原始 正文
// 和日志）。 `max_out` 限制结果。编码名称上的 Case-insensitive。
[[nodiscard]] std::optional<std::string> decode_content_encoding(
    std::string_view encoding, std::string_view body, std::size_t max_out);

}  // namespace agentty::io::compress
