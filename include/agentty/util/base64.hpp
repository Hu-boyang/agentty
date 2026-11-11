#pragma once
// 标准RFC 4648 base64 (不 base64url):
// 62/63代码的`+`和`/`，`=`填充为4的倍数。
// 用在Anthropic-API写入边界(图像内容块
// 期望标准base64)和持久化-save处边界（所以-disk
// 线程 JSON可以无损承载原始图像字节）。
//
// `auth.hpp`为OAuth/PKCE流导出`base64url_no_pad`
// ——其使用URL-safe字母表(`-_`用于62/63)并且没有填充，
// Anthropic的图像API拒绝填充。所以我们保持两个编码
// 器不同。

#include <cstddef>
#include <string>
#include <string_view>

namespace agentty::util {

[[nodiscard]] std::string base64_encode(const unsigned char* data, std::size_t len);

[[nodiscard]] inline std::string base64_encode(std::string_view bytes) {
    return base64_encode(reinterpret_cast<const unsigned char*>(bytes.data()),
                         bytes.size());
}

/// 对于格式错误的输入，返回空字符串。跳过空白；
/// 允许缺失填充（向上舍入为 4 的倍数）。
[[nodiscard]] std::string base64_decode(std::string_view encoded);

} // namespace agentty::util
