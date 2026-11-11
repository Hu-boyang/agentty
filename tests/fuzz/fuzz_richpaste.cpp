// fuzz_richpaste.cpp — libFuzzer 调制解析器
// richpaste's 解析器 每本书都读到 字节 其它控件:剪贴板( 3) BMP/DIB 图像,
// 文件 URI 列表),该 ssh 代理套接字( 传输 议定书),
// 终端 (OSC 5522份答复,也附有 ESC 字节 ,并粘贴文字(armor)。
// 构建和运行 : tests/fuzz/run.sh
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "rp/armor.hpp"
#include "rp/clipboard.hpp"
#include "rp/core.hpp"
#include "rp/image.hpp"
#include "rp/inband.hpp"
#include "rp/wire.hpp"

#define CHECK(c) do { if (!(c)) { __builtin_trap(); } } while (0)

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size == 0) return 0;
    const std::uint8_t which = data[0];
    const std::string_view text{reinterpret_cast<const char*>(data) + 1, size - 1};
    const rp::ByteView bytes{reinterpret_cast<const std::byte*>(data) + 1, size - 1};

    switch (which % 8) {
    case 0: {   // 剪贴板上的图像,以及 PNG 我们把他们变成
        for (auto img : {rp::image::decode_bmp(bytes), rp::image::decode_dib(bytes)})
            if (img) {
                CHECK(img->pixels.size() == std::size_t{img->width} * img->height * 4);
                if (std::size_t{img->width} * img->height <= 1u << 16)
                    CHECK(!rp::image::encode_png(*img).empty());
            }
        break;
    }
    case 1: {   // 代理套接字协议
        (void)rp::wire::decode_request(bytes);
        if (auto items = rp::wire::decode_response(bytes)) {
            // 解码后的反应必须重新编-encode,并解码到同一个计数.
            const auto again = rp::wire::decode_response(rp::wire::encode_items(*items));
            CHECK(again && again->size() == items->size());
        }
        break;
    }
    case 2: {   // OSC 共5522包,从 终端
        if (auto p = rp::inband::parse_5522(text)) {
            rp::inband::ReadReply r;
            (void)r.add(*p, 1u << 20);
            (void)r.items();
            (void)r.mimes();
        }
        break;
    }
    case 3: {   // a/ 丢失了5522份答复。 ESC 字节, 带类型的 作为文本
        (void)rp::inband::stripped_reply(text);
        (void)rp::inband::decode_stripped_reply(text, 1u << 20);
        break;
    }
    case 4: {   // 粘贴文本中装有装甲的文件和图像
        (void)rp::armor::contains(text);
        (void)rp::armor::progress(text);
        auto s = rp::armor::split(text);
        for (const auto& part : s.parts)
            if (const auto* it = std::get_if<rp::Item>(&part)) {
                // 随便 split() 接受必须活下来 自己的往返。
                const auto back = rp::armor::split(rp::armor::encode(*it));
                CHECK(back.errors.empty() && back.parts.size() == 1);
            }
        break;
    }
    case 5: {   // 核心文本类型
        CHECK(rp::valid_utf8(rp::repair_utf8(text)));
        CHECK(rp::valid_utf8(rp::latin1_to_utf8(text)));
        (void)rp::Mime::parse(text);
        (void)rp::FileName::parse(text);
        // 解码对一个加码组的未使用的低位("Aue="和
        // "Auc="都给出相同的字节)是宽大的,所以只检查一个
        // 解码器重-encodes到一个解码相同的东西.
        if (auto d = rp::base64_decode(text)) {
            const auto again = rp::base64_decode(rp::base64_encode(*d));
            CHECK(again && *again == *d);
        }
        break;
    }
    case 6:     // 存档: URI 列表
        (void)rp::parse_uri_list(text);
        break;
    case 7: {   // MIME 取消一个列表
        std::vector<std::string> offered;
        std::size_t at = 0;
        while (at <= text.size()) {
            const auto nl = text.find('\n', at);
            offered.emplace_back(text.substr(at, nl == std::string_view::npos ? std::string_view::npos : nl - at));
            if (nl == std::string_view::npos) break;
            at = nl + 1;
        }
        (void)rp::inband::choose_mimes(offered);
        break;
    }
    }
    return 0;
}
