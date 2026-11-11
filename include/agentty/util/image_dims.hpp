#pragma once
// image_dims —直接从图像的 头文件 字节 读取图像的 PIXEL 尺寸，
// 无需解码，也无需外部编解码器。 头文件-only 因此 传输 层和作曲家都可以查阅它。
// 对于 unrecognised/truncated 头文件 返回 {
// 0,0}（调用者将未知视为 "can't tell → allow"，
// 因为猜测 -block 会比偶尔提供的 400 更糟糕）。
//
// 为什么存在这种情况：当many-image请求中的ANY图像一侧超过2000 px (
// "image dimensions exceed max allowed size")时，
// Anthropic 400秒一整圈。一张 48 KB 的小屏幕截图很容易就有
// 3000+ px 宽（平面 UI 压缩得很小），因此文件大小没有限制——我们必须读取真实尺寸。

#include <cstddef>
#include <string_view>

namespace agentty::util {

struct ImageDims {
    unsigned w = 0;
    unsigned h = 0;
    [[nodiscard]] bool known() const noexcept { return w != 0 && h != 0; }
    [[nodiscard]] unsigned longest() const noexcept { return w > h ? w : h; }
};

// 解析尺寸由字节的前导PNG/GIF/JPEG/WebP组成。
// sniff_image_media_type 可以识别相同的格式集。从不投掷；
// 边界 - 每次访问时都会根据 `bytes.size()` 检查。
[[nodiscard]] inline ImageDims image_dimensions(std::string_view bytes) noexcept {
    const auto* p = reinterpret_cast<const unsigned char*>(bytes.data());
    const std::size_t n = bytes.size();
    auto be16 = [&](std::size_t i) -> unsigned {
        return (unsigned(p[i]) << 8) | p[i + 1];
    };
    auto be32 = [&](std::size_t i) -> unsigned {
        return (unsigned(p[i]) << 24) | (unsigned(p[i + 1]) << 16)
             | (unsigned(p[i + 2]) << 8) | p[i + 3];
    };
    auto le16 = [&](std::size_t i) -> unsigned {
        return unsigned(p[i]) | (unsigned(p[i + 1]) << 8);
    };

    // PNG:8-byte签名，然后是16/20.处的IHDR块——W/H big-endian
    if (n >= 24 && p[0] == 0x89 && p[1] == 'P' && p[2] == 'N' && p[3] == 'G')
        return {be32(16), be32(20)};

    // GIF:偏移6处的逻辑屏幕width/height、little-endian。
    if (n >= 10 && p[0] == 'G' && p[1] == 'I' && p[2] == 'F')
        return {le16(6), le16(8)};

    // JPEG:步行段标记到Start-Of-Frame (SOFn)；它的载荷是2
    // -byte长度之后的[精度(1)][高度(2 BE)][宽度(2 BE)]。
    if (n >= 4 && p[0] == 0xFF && p[1] == 0xD8) {
        std::size_t i = 2;
        while (i + 9 < n) {
            if (p[i] != 0xFF) { ++i; continue; }
            const unsigned marker = p[i + 1];
            if (marker == 0xD8 || marker == 0xD9) { i += 2; continue; }
            if (marker >= 0xD0 && marker <= 0xD7) { i += 2; continue; } // RSTn
            const unsigned seg_len = be16(i + 2);
            if (seg_len < 2) break;
            // SOF0..SOF15携带帧调暗；跳过 DHT(C4)/JPG(C8)/DAC(CC)。
            if (marker >= 0xC0 && marker <= 0xCF
                && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
                if (i + 9 < n) return {be16(i + 7), be16(i + 5)}; // {w, h}
            }
            i += 2 + seg_len;
        }
    }

    // WebP:"RIFF"???"WEBP"然后是VP8/VP8L/VP8X块。
    if (n >= 30 && p[0] == 'R' && p[1] == 'I' && p[2] == 'F' && p[3] == 'F'
        && p[8] == 'W' && p[9] == 'E' && p[10] == 'B' && p[11] == 'P') {
        // 有损 VP8:偏移 26/28. 处的 14-bit LE width/height
        if (p[12] == 'V' && p[13] == 'P' && p[14] == '8' && p[15] == ' ')
            return {le16(26) & 0x3FFF, le16(28) & 0x3FFF};
        // 扩展VP8X:24-bit LE (w-1)/(h-1)在偏移24/27.处
        if (p[12] == 'V' && p[13] == 'P' && p[14] == '8' && p[15] == 'X') {
            const unsigned w = (unsigned(p[24]) | (unsigned(p[25]) << 8)
                              | (unsigned(p[26]) << 16)) + 1;
            const unsigned h = (unsigned(p[27]) | (unsigned(p[28]) << 8)
                              | (unsigned(p[29]) << 16)) + 1;
            return {w, h};
        }
        // 无损VP8L:14-bit打包暗淡——best-effort，被视为未知。
    }
    return {};
}

// Per-side像素天花板，直接来自Anthropic的视觉文档。
//
// HARD限制是8000像素：超出该范围，API拒绝图像。在其下方，
// 超大图像是 DOWNSCALED server-side（对于模型的 long-edge 层，
// 1568 或 2576 像素）并正常回答 - 这不是错误。
//
// 2000 px是不同的，CONDITIONAL规则：它仅适用于"many-image"请求，
// i.e。一次请求中超过 20 个 image/document 块。
// 在此阈值下，2168 像素的屏幕截图完全有效。
//
// 将两者合并起来需要真实的用户数据：一侧超过 2000 px
// 的每张图像都被默默地丢弃，这是最现代的屏幕截图（2168x748
// 窗口抓取超出了它 168 px），而散文标记仍然熄灭 -
// 因此模型被告知图像存在并且没有显示任何内容。默认发送；
// 仅当请求实际为 Many-image 时才应用更严格的上限。
inline constexpr unsigned kMaxWireImageSide      = 8000;
inline constexpr unsigned kManyImageMaxSide      = 2000;
inline constexpr std::size_t kManyImageThreshold = 20;

// per-side上限适用于携带`image_count`图像块的请求。
// Anthropic 将 2000 px 限制记录为请求为
// Many-image 的条件，因此 CANNOT 上限由单独的
// 一张图像决定 - 它是整个请求的属性。呼叫者先计数，然后检票。
//
// 计数是CANDIDATE图像(非-empty 字节)，
// 故意不是可发送的：如果阈值是根据post-gate集合计算的，
// 则上限将取决于其输入的门，并且20个超大图像可以
// 通过被丢弃而将自身缩小到限制以下。对候选人进行计数
// 可以保持规则的单调和判决的稳定。
[[nodiscard]] inline constexpr unsigned wire_max_side(std::size_t image_count) noexcept {
    return image_count >= kManyImageThreshold ? kManyImageMaxSide : kMaxWireImageSide;
}

// 正确，当且仅当图像足够小，可以在 `max_side` 下发送。
// 未知维度（未解析的 头文件）通过——我们不会阻止猜测；
// 提供商自身的限制是我们无法读取的罕见格式的后盾。
[[nodiscard]] inline bool image_within_wire_limits(
    std::string_view bytes, unsigned max_side = kMaxWireImageSide) noexcept {
    const ImageDims d = image_dimensions(bytes);
    if (!d.known()) return true;
    return d.longest() <= max_side;
}

} // namespace agentty::util
