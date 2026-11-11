// inflate_test ——gzip / zlib / 原始 -DEFLATE 解码 HTTP 回应 正文.
//
// 30期的回溯覆盖:一种习惯 OpenAI 东道主( 5) z.ai's GLM 编码计划
// gzips 其/模型 JSON 即便当 agentty 发送`接受' -encoding:
// 身份 '. 没有充气器,压缩 字节 到达 JSON 解析器
// 作为垃圾和模型列表悄悄地返回空。这些向量是由 Python 公司生产的。
// gzip/zlib 用于准确的响应形状 选择面板 读。

#include "agentty/io/inflate.hpp"

#include <string>
#include <vector>

#include <doctest/doctest.h>

using agentty::io::compress::decode_content_encoding;
using agentty::io::compress::inflate_deflate;

namespace {
// 下方每个向量的未压缩的载荷都会解码回.
constexpr char kJson[] =
    R"({"object":"list","data":[{"id":"glm-4.5"},{"id":"glm-4.6"},{"id":"glm-4.5-air"}]})";

std::string bytes(std::initializer_list<int> v) {
    std::string s;
    s.reserve(v.size());
    for (int b : v) s.push_back(static_cast<char>(static_cast<unsigned char>(b)));
    return s;
}

// gzip.compress (kJson) - 说到这里. RFC 1952 (10-byte 头文件 + DEFLATE + 8-byte 拖车).
const std::string kGzip = bytes({
    31,139,8,0,0,0,0,0,2,255,171,86,202,79,202,74,77,46,81,178,82,202,201,44,46,
    81,210,81,74,73,44,73,84,178,138,174,86,202,76,1,10,166,231,228,234,154,232,
    153,42,213,234,160,8,152,161,11,152,234,38,102,22,41,213,198,214,2,0,105,104,
    14,198,81,0,0,0});

// zlib.compress (kJson) - 说到这里. RFC 1950 (2-byte 头文件 + DEFLATE + 4-byte 阿德勒32 (英语).
const std::string kZlib = bytes({
    120,156,171,86,202,79,202,74,77,46,81,178,82,202,201,44,46,81,210,81,74,73,44,
    73,84,178,138,174,86,202,76,1,10,166,231,228,234,154,232,153,42,213,234,160,8,
    152,161,11,152,234,38,102,22,41,213,198,214,2,0,244,234,24,77});

// 原始内容 DEFLATE (兹利布语) 正文 与2号 -byte 头文件 和4个 -byte 拖车被剥除)。
const std::string kRaw = bytes({
    171,86,202,79,202,74,77,46,81,178,82,202,201,44,46,81,210,81,74,73,44,73,84,
    178,138,174,86,202,76,1,10,166,231,228,234,154,232,153,42,213,234,160,8,152,
    161,11,152,234,38,102,22,41,213,198,214,2,0});

constexpr std::size_t kCap = 64ull * 1024 * 1024;
}  // 命名空间结束。

TEST_CASE("inflate: gzip response body round-trips") {
    auto out = decode_content_encoding("gzip", kGzip, kCap);
    REQUIRE(out.has_value());
    CHECK(*out == kJson);
    // 大小写 -insensitive 空白空间 -tolerant ,则 x 则 -gzip 化名,别名。
    CHECK(decode_content_encoding("  GZIP ", kGzip, kCap).value() == kJson);
    CHECK(decode_content_encoding("x-gzip", kGzip, kCap).value() == kJson);
}

TEST_CASE("inflate: deflate (zlib-wrapped) response body round-trips") {
    auto out = decode_content_encoding("deflate", kZlib, kCap);
    REQUIRE(out.has_value());
    CHECK(*out == kJson);
}

TEST_CASE("inflate: deflate falls back to RAW when there is no zlib wrapper") {
    // 一些服务器标签未创建 DEFLATE 以 "deflate"。
    // 解码器检测缺失的 zlib 头文件 并充气整个 正文.
    auto out = decode_content_encoding("deflate", kRaw, kCap);
    REQUIRE(out.has_value());
    CHECK(*out == kJson);
}

TEST_CASE("inflate: identity and empty encodings pass the body through") {
    CHECK(decode_content_encoding("identity", kJson, kCap).value() == kJson);
    CHECK(decode_content_encoding("", kJson, kCap).value() == kJson);
}

TEST_CASE("inflate: unknown encoding is a soft failure (nullopt)") {
    // brotli等人不支持;呼叫者保留生的正文+日志.
    CHECK(!decode_content_encoding("br", kGzip, kCap).has_value());
    CHECK(!decode_content_encoding("compress", kGzip, kCap).has_value());
}

TEST_CASE("inflate: corrupt / truncated streams fail cleanly, never crash") {
    // 短长的 gzip (% 1) 头文件 —— —— 仅此而已。
    CHECK(!decode_content_encoding("gzip", kGzip.substr(0, 12), kCap).has_value());
    // 垃圾自称是gzip.
    CHECK(!decode_content_encoding("gzip", std::string("not gzip at all"), kCap)
               .has_value());
    // 恶法.
    CHECK(!decode_content_encoding("gzip", std::string(20, '\x00'), kCap)
               .has_value());
    // 原始内容 DEFLATE 中点短线 -stream.
    CHECK(!inflate_deflate(kRaw.substr(0, 5), kCap).has_value());
}

TEST_CASE("inflate: the decompression-bomb cap is enforced") {
    // 小小的输出盖甚至可以合法 数据流 拒绝结束 -allocate.
    CHECK(!decode_content_encoding("gzip", kGzip, /*max_out=*/8).has_value());
}
