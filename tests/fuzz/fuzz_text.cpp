// fuzz_text.cpp — libFuzzer 调制解析器 agentty
// 纯文本 解析器 :那些不值得信赖的人 字节 (工具输出,流化工具)
// -call JSON ,贴上文本,提供商的模型代号。检查 不变量 ,
// 不仅仅是崩溃。 tests/fuzz/run.sh
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "agentty/domain/model_name.hpp"
#include "agentty/tool/util/partial_json.hpp"
#include "agentty/tool/util/utf8.hpp"
#include "agentty/util/base64.hpp"

namespace u = agentty::tools::util;

#define CHECK(c) do { if (!(c)) { __builtin_trap(); } } while (0)

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size == 0) return 0;
    // 第一届 字节 选择一个分点, 所以两个 -argument 函数得到两个输入。
    const std::size_t split = size > 1 ? data[0] % size : 0;
    const std::string_view all{reinterpret_cast<const char*>(data) + 1, size - 1};
    const std::string_view a = all.substr(0, std::min(split, all.size()));
    const std::string_view b = all.substr(std::min(split, all.size()));

    // utf8:每个输出都是有效的UTF-8,切片会停留在界限内.
    CHECK(u::is_valid_utf8(u::sanitize_utf8(all)));
    CHECK(u::is_valid_utf8(u::to_valid_utf8(std::string{all})));
    const std::size_t lim = static_cast<std::size_t>(data[0]) * 7;
    const std::size_t cut = u::safe_utf8_cut(all, lim);
    CHECK(cut <= all.size());
    CHECK(all.size() <= lim ? cut == all.size() : cut <= lim);
    if (u::is_valid_utf8(all)) CHECK(u::is_valid_utf8(all.substr(0, cut)));
    const auto stripped = u::strip_terminal_controls(all);
    for (char c : stripped) {
        const auto uc = static_cast<unsigned char>(c);
        CHECK(uc != 0x1b && uc != 0x7f && (uc >= 0x20 || c == '\n' || c == '\t'));
    }
    const auto tail = u::progress_tail(all, lim);
    CHECK(tail.size() <= all.size());
    CHECK(tail.data() >= all.data() && tail.data() + tail.size() == all.data() + all.size());

    // 部分 JSON 随它 数据流 内, 字节 由 字节 穿过一个不断增长的前缀。
    (void)u::close_partial_json(all);
    (void)u::ended_inside_string(all);
    const std::string key{a.substr(0, std::min<std::size_t>(a.size(), 12))};
    (void)u::sniff_string(b, key);
    (void)u::sniff_string_progressive(b, key);
    if (auto at = u::locate_string_value(b, key)) {
        CHECK(*at <= b.size());
        const std::string whole = u::decode_string_from(b, *at);
        std::string inc;
        std::size_t through = *at;
        bool closed = false;
        for (std::size_t end = *at; end <= b.size(); end += 1 + end % 5) {
            (void)u::decode_string_append(b.substr(0, end), &through, inc, &closed);
            CHECK(through <= end);
        }
        (void)u::decode_string_append(b, &through, inc, &closed);
        CHECK(inc == whole);   // 递增解码匹配其中的 -shot 单曲
    }

    // base64 圆行.
    const std::string enc = agentty::util::base64_encode(all);
    CHECK(agentty::util::base64_decode(enc) == all);
    (void)agentty::util::base64_decode(all);

    // 模型 id 解码(ids和显示名称来自提供者 APIs).
    // 标记-only id(":latest")在故意时解码为空名,
    // 所以检查必须始终持有的: 绑定输出,没有控制字节.
    const auto mn = agentty::model_name::decode(a, b);
    CHECK(mn.name.size() <= 64 && mn.qualifier.size() <= 64 && mn.version.size() <= 64);
    for (char c : mn.full()) {
        const auto uc = static_cast<unsigned char>(c);
        CHECK(uc >= 0x20 && uc != 0x7f);
    }
    return 0;
}
