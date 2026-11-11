// agentty::io::compress-HTTP 正文的gzip/zlib/原始DEFLATE解码。
//
// DEFLATE核心(Huffman、BitReader、
// inflate_deflate)是从rag-cpp的OOXML加载器移植的规范RFC
// 1951解码器——显然是为了在恶意输入上限制-safe而不是为了吞吐量而编写的。
// HTTP JSON 正文 较小；正确性和安全性在这里很重要。

#include "agentty/io/inflate.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace agentty::io::compress {
namespace {

// ─── 坎诺式Huffman解码 ──────────────────────────────────────────
struct Huffman {
    std::array<std::uint16_t, 16> counts{};
    std::vector<std::uint16_t> symbols;

    bool build(const std::uint8_t* lengths, std::size_t n) {
        counts.fill(0);
        for (std::size_t i = 0; i < n; ++i) counts[lengths[i]]++;
        counts[0] = 0;
        // 拒绝超过-subscribed代码集（一个损坏的表，
        // 其会让decode()跑出符号[]的末尾）。
        int left = 1;
        for (std::size_t len = 1; len < 16; ++len) {
            left <<= 1;
            left -= counts[len];
            if (left < 0) return false;
        }
        std::array<std::uint16_t, 16> offs{};
        for (std::size_t len = 1; len < 15; ++len)
            offs[len + 1] = static_cast<std::uint16_t>(offs[len] + counts[len]);
        symbols.assign(n, 0);
        for (std::size_t i = 0; i < n; ++i)
            if (lengths[i])
                symbols[offs[lengths[i]]++] = static_cast<std::uint16_t>(i);
        return true;
    }
};

class BitReader {
public:
    explicit BitReader(std::string_view d) : d_(d) {}

    // 耗尽时返回-1而不是抛出：每个调用者都会检查，
    // 并且截断的输入是预期的，而不是例外。
    int bit() {
        if (nbits_ == 0) {
            if (pos_ >= d_.size()) { ok_ = false; return -1; }
            cur_ = static_cast<unsigned char>(d_[pos_++]);
            nbits_ = 8;
        }
        int b = cur_ & 1;
        cur_ >>= 1;
        --nbits_;
        return b;
    }

    int bits(int n) {
        int v = 0;
        for (int i = 0; i < n; ++i) {
            int b = bit();
            if (b < 0) return -1;
            v |= b << i;
        }
        return v;
    }

    void align() { nbits_ = 0; }

    int decode(const Huffman& h) {
        int code = 0, first = 0, index = 0;
        for (std::size_t len = 1; len < 16; ++len) {
            int b = bit();
            if (b < 0) return -1;
            code |= b;
            int count = h.counts[len];
            if (code - first < count) {
                std::size_t at = static_cast<std::size_t>(index + (code - first));
                if (at >= h.symbols.size()) return -1;
                return h.symbols[at];
            }
            index += count;
            first = (first + count) << 1;
            code <<= 1;
        }
        return -1;
    }

    [[nodiscard]] bool ok() const noexcept { return ok_; }
    [[nodiscard]] std::size_t pos() const noexcept { return pos_; }
    [[nodiscard]] std::size_t size() const noexcept { return d_.size(); }
    [[nodiscard]] std::string_view data() const noexcept { return d_; }

private:
    std::string_view d_;
    std::size_t pos_ = 0;
    unsigned cur_ = 0;
    int nbits_ = 0;
    bool ok_ = true;
};

constexpr std::uint16_t kLenBase[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
constexpr std::uint8_t kLenExtra[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
constexpr std::uint16_t kDistBase[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,
    4097,6145,8193,12289,16385,24577};
constexpr std::uint8_t kDistExtra[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

// 构建固定(块类型01)literal/length+距离霍夫曼表。
void build_fixed(Huffman& lit, Huffman& dist) {
    std::array<std::uint8_t, 288> ll{};
    for (int i = 0; i < 144; ++i)   ll[i] = 8;
    for (int i = 144; i < 256; ++i) ll[i] = 9;
    for (int i = 256; i < 280; ++i) ll[i] = 7;
    for (int i = 280; i < 288; ++i) ll[i] = 8;
    lit.build(ll.data(), ll.size());
    std::array<std::uint8_t, 30> dl{};
    dl.fill(5);
    dist.build(dl.data(), dl.size());
}

// 读取动态(块类型10)code-length表并构建lit/dist.
bool read_dynamic(BitReader& br, Huffman& lit, Huffman& dist) {
    int hlit  = br.bits(5);  if (hlit  < 0) return false; hlit  += 257;
    int hdist = br.bits(5);  if (hdist < 0) return false; hdist += 1;
    int hclen = br.bits(4);  if (hclen < 0) return false; hclen += 4;
    if (hlit > 286 || hdist > 30) return false;

    static constexpr std::uint8_t order[19] = {
        16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    std::array<std::uint8_t, 19> cl{};
    for (int i = 0; i < hclen; ++i) {
        int v = br.bits(3);
        if (v < 0) return false;
        cl[order[i]] = static_cast<std::uint8_t>(v);
    }
    Huffman clh;
    if (!clh.build(cl.data(), cl.size())) return false;

    std::vector<std::uint8_t> lengths;
    lengths.reserve(static_cast<std::size_t>(hlit + hdist));
    while (static_cast<int>(lengths.size()) < hlit + hdist) {
        int sym = br.decode(clh);
        if (sym < 0) return false;
        if (sym < 16) {
            lengths.push_back(static_cast<std::uint8_t>(sym));
        } else if (sym == 16) {
            if (lengths.empty()) return false;
            int rep = br.bits(2); if (rep < 0) return false; rep += 3;
            std::uint8_t prev = lengths.back();
            while (rep-- > 0) lengths.push_back(prev);
        } else if (sym == 17) {
            int rep = br.bits(3); if (rep < 0) return false; rep += 3;
            while (rep-- > 0) lengths.push_back(0);
        } else {  // 符号== 18
            int rep = br.bits(7); if (rep < 0) return false; rep += 11;
            while (rep-- > 0) lengths.push_back(0);
        }
    }
    if (static_cast<int>(lengths.size()) != hlit + hdist) return false;
    if (!lit.build(lengths.data(), static_cast<std::size_t>(hlit))) return false;
    if (!dist.build(lengths.data() + hlit, static_cast<std::size_t>(hdist)))
        return false;
    return true;
}

}  // 命名空间结束。

std::optional<std::string> inflate_deflate(std::string_view in,
                                           std::size_t max_out) {
    BitReader br(in);
    std::string out;

    for (;;) {
        int final = br.bit();
        if (final < 0) return std::nullopt;
        int type = br.bits(2);
        if (type < 0) return std::nullopt;

        if (type == 0) {
            // 存储块：对齐，然后LEN/~LEN，然后LEN原始字节。
            br.align();
            std::size_t p = br.pos();
            if (p + 4 > br.size()) return std::nullopt;
            std::string_view d = br.data();
            unsigned len = static_cast<unsigned char>(d[p]) |
                           (static_cast<unsigned>(static_cast<unsigned char>(d[p + 1])) << 8);
            unsigned nlen = static_cast<unsigned char>(d[p + 2]) |
                            (static_cast<unsigned>(static_cast<unsigned char>(d[p + 3])) << 8);
            if ((len ^ 0xFFFFu) != nlen) return std::nullopt;
            p += 4;
            if (p + len > br.size()) return std::nullopt;
            if (out.size() + len > max_out) return std::nullopt;
            out.append(d.substr(p, len));
            // 刚刚经过所存储的字节时恢复位读取器。
            BitReader nb(d.substr(p + len));
            br = nb;
        } else if (type == 1 || type == 2) {
            Huffman lit, dist;
            if (type == 1) build_fixed(lit, dist);
            else if (!read_dynamic(br, lit, dist)) return std::nullopt;

            for (;;) {
                int sym = br.decode(lit);
                if (sym < 0) return std::nullopt;
                if (sym == 256) break;          // 块结束
                if (sym < 256) {
                    if (out.size() + 1 > max_out) return std::nullopt;
                    out.push_back(static_cast<char>(sym));
                    continue;
                }
                sym -= 257;
                if (sym >= 29) return std::nullopt;
                int extra = br.bits(kLenExtra[sym]);
                if (extra < 0) return std::nullopt;
                std::size_t length = kLenBase[sym] + static_cast<std::size_t>(extra);

                int dsym = br.decode(dist);
                if (dsym < 0 || dsym >= 30) return std::nullopt;
                int dextra = br.bits(kDistExtra[dsym]);
                if (dextra < 0) return std::nullopt;
                std::size_t distance =
                    kDistBase[dsym] + static_cast<std::size_t>(dextra);
                if (distance == 0 || distance > out.size()) return std::nullopt;
                if (out.size() + length > max_out) return std::nullopt;
                std::size_t start = out.size() - distance;
                for (std::size_t i = 0; i < length; ++i)
                    out.push_back(out[start + i]);   // 可能重叠(LZ77)
            }
        } else {
            return std::nullopt;   // 保留块类型
        }

        if (final) break;
        if (!br.ok()) return std::nullopt;
    }
    return out;
}

std::optional<std::string> decode_content_encoding(std::string_view encoding,
                                                   std::string_view body,
                                                   std::size_t max_out) {
    // Case-insensitive 修剪编码令牌。
    auto lower = [](std::string_view s) {
        std::string o;
        o.reserve(s.size());
        std::size_t b = 0, e = s.size();
        while (b < e && (s[b] == ' ' || s[b] == '\t')) ++b;
        while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t')) --e;
        for (std::size_t i = b; i < e; ++i) {
            char c = s[i];
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
            o.push_back(c);
        }
        return o;
    };
    const std::string enc = lower(encoding);

    if (enc.empty() || enc == "identity")
        return std::string{body};

    if (enc == "gzip" || enc == "x-gzip") {
        // RFC 1952:10-byte 头文件，可选额外字段，DEFLATE，
        // 8-byte尾部。 字节 3 (FLG) 位选择可选字段。
        if (body.size() < 18) return std::nullopt;   // 头文件(10)+预告片(8)
        auto u8 = [&](std::size_t i) {
            return static_cast<unsigned char>(body[i]);
        };
        if (u8(0) != 0x1f || u8(1) != 0x8b || u8(2) != 8) return std::nullopt;
        const unsigned flg = u8(3);
        std::size_t p = 10;
        if (flg & 0x04) {                             // FEXTRA
            if (p + 2 > body.size()) return std::nullopt;
            std::size_t xlen = u8(p) | (static_cast<std::size_t>(u8(p + 1)) << 8);
            p += 2 + xlen;
        }
        auto skip_cstr = [&]() -> bool {              // FNAME/FCOMMENT
            while (p < body.size() && body[p] != '\0') ++p;
            if (p >= body.size()) return false;
            ++p;                                      // 消耗NUL
            return true;
        };
        if (flg & 0x08) { if (!skip_cstr()) return std::nullopt; }   // FNAME
        if (flg & 0x10) { if (!skip_cstr()) return std::nullopt; }   // FCOMMENT
        if (flg & 0x02) p += 2;                       // FHCRC
        if (p + 8 > body.size()) return std::nullopt; // 需要拖车空间
        std::string_view deflate = body.substr(p, body.size() - p - 8);
        return inflate_deflate(deflate, max_out);
    }

    if (enc == "deflate") {
        // "deflate"是每HTTP的RFC 1950 (zlib)，但是一些服务器发送RAW
        // DEFLATE。检测 2-byte zlib 头文件: CMF/FLG，其中 CM==
        // 8 和 (CMF*256+FLG) % 31 == 0。如果它看起来像 zlib，
        // 则剥离 2-byte 头文件 + 4-byte Adler32 尾部；否则对整个 正文 进行充气。
        if (body.size() >= 2) {
            const unsigned cmf = static_cast<unsigned char>(body[0]);
            const unsigned flg = static_cast<unsigned char>(body[1]);
            const bool zlib = (cmf & 0x0f) == 8 &&
                              ((cmf << 8) | flg) % 31 == 0;
            if (zlib) {
                if (body.size() < 6) return std::nullopt;   // hdr(2)+阿德勒(4)
                return inflate_deflate(body.substr(2, body.size() - 2 - 4),
                                       max_out);
            }
        }
        return inflate_deflate(body, max_out);
    }

    return std::nullopt;   // 未知编码 — 调用者保留原始 正文 + 日志
}

}  // namespace agentty::io::compress
