// image_dims_test —— 将像素钉住 -dimension 门,
// 可以保持超大小的图像 传输 (Anthropic 四个四百块 -image 如果图像超过2000,
// 则转弯 px/side。覆盖 字节-header 维度读取器 和 编号 传输 大门(
// wire_image_sendable ),这是 SSOT 每种方言的图像选择器都使用.

#include "agtest.hpp"

#include "agentty/domain/conversation.hpp"
#include "agentty/provider/msg_shared.hpp"
#include "agentty/util/image_dims.hpp"

#include <string>

using agentty::ImageContent;
using agentty::util::image_dimensions;
namespace wire = agentty::provider::wire;

namespace {
// 最低额 PNG 带有给定的 width/height 输入 IHDR (所有这个测试的读者看。) 8
// -byte 数字+ IHDR length/type + W/H 大块 -endian ,然后填充器。
std::string png_with_dims(unsigned w, unsigned h) {
    std::string b;
    const unsigned char sig[8] = {0x89,'P','N','G',0x0D,0x0A,0x1A,0x0A};
    b.append(reinterpret_cast<const char*>(sig), 8);
    b.append("\x00\x00\x00\x0D", 4);      // IHDR长度=13
    b.append("IHDR", 4);
    auto be32 = [&](unsigned v) {
        b.push_back(char((v >> 24) & 0xFF));
        b.push_back(char((v >> 16) & 0xFF));
        b.push_back(char((v >> 8) & 0xFF));
        b.push_back(char(v & 0xFF));
    };
    be32(w); be32(h);
    b.append(64, '\x00');                 // 填充器 正文
    return b;
}
} // 命名空间结束。

TEST_CASE("image dimension reader") {
    auto d1 = image_dimensions(png_with_dims(1920, 1080));
    check(d1.known() && d1.w == 1920 && d1.h == 1080, "PNG dims parsed");
    check(d1.longest() == 1920, "longest side = width here");

    auto d2 = image_dimensions(png_with_dims(3440, 200));
    check(d2.w == 3440 && d2.longest() == 3440, "wide thin PNG parsed");

    // 断断/ 无 -image □ 未知(从未坠毁,从未作假) -positives).
    check(!image_dimensions(std::string("not an image")).known(),
          "garbage bytes → unknown dims");
    check(!image_dimensions(std::string{}).known(), "empty → unknown");
}

TEST_CASE("wire drops oversized images, keeps in-range ones") {
    // in-range:普通截图可发送.
    {
        ImageContent img;
        img.media_type = "image/png";
        img.set_bytes(png_with_dims(1600, 900));
        check(wire::wire_image_sendable(img),
              "1600x900 is well under the cap → sendable");
    }
    // 来个嗨 -DPI 抓取 必须 到达模型。 Anthropic
    // 降低模型上的任何尺寸 -edge 级服务器 -side
    // 答案通常为 – 这不是一个错误。 MANY-image 规则
    // ( > 20个块在一个请求中)错误地应用到每个图像,
    // 所以普通截图在使用时被丢弃。 "[image: ...]"
    // 标记仍然出局:模型被告知有图像存在,没有显示任何内容.
    {
        ImageContent img;
        img.media_type = "image/png";
        img.set_bytes(png_with_dims(3024, 1200));
        check(wire::wire_image_sendable(img),
              "3024 px retina capture is sent, not dropped");
    }
    {
        // 上面有这段胶片 2168x748的窗户被抢 168px
        // 在旧天花板上 默默地丢弃了
        ImageContent img{"image/png", png_with_dims(2168, 748)};
        check(wire::wire_image_sendable(img),
              "2168x748 window grab is sent");
    }
    // 真正的边界是API的硬界限:在它,允许;在它过去,
    // 被拒绝(该请求将被彻底拒绝)。
    {
        ImageContent at{"image/png",
                        png_with_dims(agentty::util::kMaxWireImageSide,
                                      agentty::util::kMaxWireImageSide)};
        ImageContent over{"image/png",
                          png_with_dims(agentty::util::kMaxWireImageSide + 1, 10)};
        check(wire::wire_image_sendable(at),    "8000 px is allowed (== cap)");
        check(!wire::wire_image_sendable(over), "8001 px is rejected");
    }
    // 空的字节仍然被拒绝(行为不变).
    {
        ImageContent img; img.media_type = "image/png";
        check(!wire::wire_image_sendable(img), "empty bytes → not sendable");
    }
    // 未知格式(不能读 dims)被允许通过——我们不以
    // 猜测的方式屏蔽;提供者自己的极限是后台.
    {
        ImageContent img;
        img.media_type = "image/png";
        img.set_bytes(std::string("\xff\xd8\xffno-valid-sof-here", 18));
        check(wire::wire_image_sendable(img),
              "unreadable dims → allowed (provider is the backstop)");
    }
}

// ── 电话 MANY-IMAGE 上限 ──────────────────────────────────────────────────
//
// 2000 px 上限是真实的, 但有条件的: 只有在
// 请求包含许多图像块时才适用。 两种常数都存在,
// 没有 调用 暂时不设 松散的帽子无条件发运 20
// -image 请求会由提供商提供400'd ,
// 没有本地的追踪。 锁 规则到两件事,可以默默漂移: 阈值算术,
// 和一个事实, 计数跨度 两者 图像块的来源。
TEST_CASE("wire image cap tightens on a many-image request") {
    using agentty::util::kManyImageMaxSide;
    using agentty::util::kManyImageThreshold;
    using agentty::util::kMaxWireImageSide;
    using agentty::util::wire_max_side;

    // 边界本身——关闭 -by-one 在此默默禁用规则,
    // 以查看它要保护的确切请求大小。
    check(wire_max_side(0) == kMaxWireImageSide, "no images → loose cap");
    check(wire_max_side(kManyImageThreshold - 1) == kMaxWireImageSide,
          "just under the threshold → still the loose cap");
    check(wire_max_side(kManyImageThreshold) == kManyImageMaxSide,
          "at the threshold → the strict 2000 px cap");
    check(wire_max_side(kManyImageThreshold + 5) == kManyImageMaxSide,
          "well past the threshold → strict cap");

    // 电话 相同 图像可以单独发送, 并投下很多 -image 请求。
    // 这就是使封顶成为参数的整个点:可
    // 发送性不是图片属性,而是请求属性.
    {
        ImageContent img{"image/png", png_with_dims(2168, 748)};
        check(wire::wire_image_sendable(img, kMaxWireImageSide),
              "2168 px screenshot ships in an ordinary request");
        check(!wire::wire_image_sendable(img, kManyImageMaxSide),
              "the same screenshot is dropped once the request is many-image");
    }

    // 计算范围包括用户图像AND tool_result。
    // 一个显示 READS 20 截图击中供应商的极限,
    // 完全像一个贴20, 所以只计算用户转弯就会错过它。
    {
        std::vector<agentty::Message> msgs;
        agentty::Message user;
        user.role = agentty::Role::User;
        user.images.push_back({"image/png", png_with_dims(64, 64)});
        user.images.push_back({"image/png", ""});   // 空: 从未计算
        msgs.push_back(std::move(user));
        check(wire::wire_image_count(msgs) == 1,
              "empty bytes don't count toward the many-image threshold");

        agentty::Message asst;
        asst.role = agentty::Role::Assistant;
        agentty::ToolUse tc;
        tc.status = agentty::ToolUse::Done{
            .images = {{"image/png", png_with_dims(64, 64)},
                       {"image/png", png_with_dims(64, 64)}}};
        asst.tool_calls.push_back(std::move(tc));
        msgs.push_back(std::move(asst));
        check(wire::wire_image_count(msgs) == 3,
              "tool_result images count toward the threshold too");
    }
}
