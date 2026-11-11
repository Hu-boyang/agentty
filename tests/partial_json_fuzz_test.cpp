// partial_json_fuzz_test ——流式工具参数的每个前缀必须关闭
// 到 JSON 说 解析.
//
// Tool 参数作为碎片到达; 活卡 解析 任何经过如此
// 深深的深渊 close_partial_json。
// 一个无法解析的前缀使牌片闪烁或跳出球场。在此每个 字节
// 不同真实参数形状的前缀是关闭和解析的,完整的输入必须保持
// 不变地返回。

#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "agtest.hpp"

#include <nlohmann/json.hpp>

#include "agentty/tool/util/partial_json.hpp"

using agentty::tools::util::close_partial_json;
using agentty::tools::util::ended_inside_string;
using nlohmann::json;

namespace {

std::vector<std::string> samples() {
    return {
        R"({"path":"src/a.cpp","edits":[{"old_text":"int x = 1;","new_text":"int x = 2;"}]})",
        R"({"command":"grep -rn \"foo\\\\bar\" . | head -5","timeout":30})",
        R"({"file_path":"x.md","content":"line one\nline \"two\"\n\ttab \\ slash"})",
        R"({"a":[1,2,[3,{"b":null}],true,false,-1.5e3],"c":{}})",
        R"({"unicode":"caf\u00e9 \ud83d\ude00 日本語","esc":"\\u0041"})",
        R"({"nested":{"deep":{"deeper":[{"x":[[]]}]}}})",
        R"({"empty":"","zero":0,"neg":-0.0,"arr":[]})",
        R"([{"k":"v"},{"k":"w"}])",
        R"({"text":"ends with backslash\\"})",
        R"({"k" : [ 1 , 2 ] , "s" : "a,b" })",
    };
}

// 随机的JSON带有尴尬的字符串,因此模糊的盖子会塑造样本丢失.
json random_value(std::mt19937& rng, int depth) {
    std::uniform_int_distribution<int> kind(0, depth > 3 ? 4 : 6);
    switch (kind(rng)) {
        case 0: return nullptr;
        case 1: return (rng() & 1) == 1;
        case 2: return static_cast<int>(rng() % 2000) - 1000;
        case 3: return static_cast<double>(rng() % 100000) / 7.0;
        case 4: {
            static const char* parts[] = {"a", "\"", "\\", "\n", ",", ":", "{", "]",
                                          "é", "日", "\t", " ", "x\\\"y", "\\u"};
            std::string s;
            for (int i = rng() % 8; i > 0; --i) s += parts[rng() % std::size(parts)];
            return s;
        }
        case 5: {
            json a = json::array();
            for (int i = rng() % 4; i > 0; --i) a.push_back(random_value(rng, depth + 1));
            return a;
        }
        default: {
            json o = json::object();
            for (int i = rng() % 4; i > 0; --i)
                o["k" + std::to_string(rng() % 50)] = random_value(rng, depth + 1);
            return o;
        }
    }
}

// 如果任何前缀没有关闭到有效的JSON,则使用违法的前缀
// 测试失败.
void check_every_prefix(const std::string& full) {
    for (std::size_t n = 0; n <= full.size(); ++n) {
        const std::string prefix = full.substr(0, n);
        const std::string closed = close_partial_json(prefix);
        if (!json::accept(closed)) {
            if (const char* f = std::getenv("PARTIAL_JSON_DUMP")) {
                if (FILE* fp = std::fopen(f, "a")) {
                    const auto rep = json::error_handler_t::replace;
                    std::fprintf(fp, "%s\n%s\n\n", json(prefix).dump(-1, ' ', true, rep).c_str(),
                                 json(closed).dump(-1, ' ', true, rep).c_str());
                    std::fclose(fp);
                }
            }
            CHECK_MESSAGE(false, "prefix [" << prefix << "] closed to [" << closed << "]");
        }
    }
    // 已完成的输入已经有效, 必须返回到同样的含义。
    CHECK(json::parse(close_partial_json(full)) == json::parse(full));
    CHECK_FALSE(ended_inside_string(full));
}

}  // 命名空间结束。

TEST_CASE("partial json: every prefix of real tool args closes into valid json") {
    for (const auto& s : samples()) check_every_prefix(s);
}

TEST_CASE("partial json: every prefix of random json closes into valid json") {
    std::mt19937 rng(20261016);
    int n = 400;
    if (const char* e = std::getenv("PARTIAL_JSON_CASES")) n = std::atoi(e);
    for (int i = 0; i < n; ++i) {
        json v = json::object();
        v["args"] = random_value(rng, 0);
        check_every_prefix(v.dump());
        check_every_prefix(v.dump(1));   // 带有空白
    }
}

TEST_CASE("partial json: ended_inside_string matches the shapes it should") {
    CHECK(ended_inside_string(R"({"a":"hel)"));
    CHECK(ended_inside_string(R"({"a":"he\")"));
    CHECK_FALSE(ended_inside_string(R"({"a":"he\\")"));
    CHECK_FALSE(ended_inside_string(R"({"a":"hello")"));
    CHECK_FALSE(ended_inside_string(R"({"a":1)"));
}

// 写入预览一次解码为`content` a 几个字节 ,
// 以 数据流 的参数为单位。 一旦该值的关闭引号被消耗, 以后的 调用
// 和 字节 的 下一个 成员不能解码为该值。 由 tests/fuzz
// 找到: 预览显示 `","path":"…` 粘贴在内容上。
TEST_CASE("partial json: incremental decode stays closed once the value ends") {
    using agentty::tools::util::decode_string_append;
    using agentty::tools::util::decode_string_from;
    using agentty::tools::util::locate_string_value;
    const std::string raw = R"({"content":"hi \"there\"","path":"a.txt"})";
    const auto at = locate_string_value(raw, "content");
    REQUIRE(at.has_value());
    std::string inc;
    std::size_t through = *at;
    bool closed = false;
    for (std::size_t end = *at; end <= raw.size(); ++end)
        (void)decode_string_append(std::string_view{raw}.substr(0, end), &through, inc, &closed);
    CHECK(closed);
    CHECK(inc == decode_string_from(raw, *at));
    CHECK(inc == "hi \"there\"");
}
