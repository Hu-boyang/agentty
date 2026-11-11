// 线路的烟雾测试 -DP 模糊的对决者, 构建在反面 mcp-cpp fuzzy_match
// TU 直接( 无) GoogleTest 依赖性。 TU 是那个 唯一 编辑工具匹配器的
// 真伪源 – e. agentty-local 叉子被移除; 键 `edit` 工具(
// 选项) third_party/mcp-cpp/src/tools/fs_edit.cpp )用此经.
//
// 案例镜像Zed的StreamingFuzzyMatcher测试套
// 件——它们是我们希望不断通过的回归.

#include "agtest.hpp"

#include <mcp/tools/util/fuzzy_match.hpp>

#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>

using mcp::tools::util::fuzzy_find;

namespace {

void expect(const char* name, bool cond, std::string_view detail = {}) {
    std::string m = detail.empty() ? std::string{name}
                                    : std::string{name} + " — " + std::string{detail};
    CHECK_MESSAGE(cond, m);
}

}  // 名称空间 (帮助)

TEST_CASE("exact unique") {
    std::string_view file = "alpha\nbeta\ngamma\n";
    auto r = fuzzy_find(file, "beta");
    expect("exact unique",
          r.ok && r.pos == 6 && r.len == 4,
          "expected pos=6 len=4");
}

TEST_CASE("exact ambiguous no hint") {
    std::string_view file = "foo\nfoo\nfoo\n";
    auto r = fuzzy_find(file, "foo");
    expect("ambiguous without hint",
          !r.ok && r.count >= 2);
}

TEST_CASE("exact ambiguous with hint") {
    std::string_view file = "fn first(){}\nfn second(){}\nfn third(){}\n";
    auto r = fuzzy_find(file, "fn second(){}", "", 1u /* 0-based 线条 `fn second` */);
    expect("ambiguous resolved by hint",
          r.ok && r.pos == 13 && r.len == 13,
          "expected pos=13 len=13");
}

TEST_CASE("typo tolerated") {
    std::string_view file =
        "fn foo1(a: usize) -> usize {\n"
        "    40\n"
        "}\n"
        "\n"
        "fn foo2(b: usize) -> usize {\n"
        "    42\n"
        "}\n";
    // 针头有一种类型:u32而不是用益.
    std::string_view needle = "fn foo1(a: usize) -> u32 {\n40\n}";
    auto r = fuzzy_find(file, needle);
    expect("typo in keyword tolerated",
          r.ok && r.pos == 0,
          "expected the foo1 block");
}

TEST_CASE("indent drift") {
    std::string_view file =
        "class C {\n"
        "    fn m() {\n"
        "        return 1;\n"
        "    }\n"
        "}\n";
    // 针头是外向的(模式排放方法正文不设类缩进)。
    std::string_view needle = "fn m() {\n    return 1;\n}";
    std::string_view new_t  = "fn m() {\n    return 2;\n}";
    auto r = fuzzy_find(file, needle, new_t);
    expect("indent drift detected",
          r.ok,
          "expected fuzzy match");
    // 缩进 -fixup 应为每个非空格预留4个空格 -blank 线条。
    bool reindented = !r.adjusted_new_text.empty()
        && r.adjusted_new_text.find("    fn m() {") != std::string::npos
        && r.adjusted_new_text.find("        return 2;") != std::string::npos;
    expect("indent fixup applied", reindented,
          r.adjusted_new_text);
}

TEST_CASE("no match") {
    std::string_view file = "alpha\nbeta\ngamma\n";
    auto r = fuzzy_find(file, "totally unrelated content that doesnt belong");
    expect("genuine no-match rejected",
          !r.ok && r.count == 0);
}

TEST_CASE("trailing whitespace drift") {
    // 文件在每个行上都有后缀空格。
    std::string_view file =
        "fn first() {    \n"
        "    body();    \n"
        "}    \n";
    // 针是清净的版本.
    std::string_view needle = "fn first() {\n    body();\n}";
    auto r = fuzzy_find(file, needle);
    expect("trailing-ws drift matched",
          r.ok && r.pos == 0,
          "expected match from start of file");
}

TEST_CASE("crlf") {
    std::string_view file = "alpha\r\nbeta\r\ngamma\r\n";
    auto r = fuzzy_find(file, "beta");
    expect("CRLF tolerated", r.ok);
}

TEST_CASE("crlf multiline dp") {
    // 文件有 CRLF 线尾,针头有 LF 仅此而已。
    // 电话 DP 仍然应该匹配,因为 trimmed_of 条条\r。
    std::string_view file =
        "fn first() {\r\n"
        "    return 1;\r\n"
        "}\r\n"
        "fn second() {\r\n"
        "    return 2;\r\n"
        "}\r\n";
    std::string_view needle =
        "fn second() {\n"
        "    return 2;\n"
        "}";
    auto r = fuzzy_find(file, needle);
    expect("CRLF multi-line DP", r.ok,
          "DP should match CRLF file against LF needle");
}

TEST_CASE("smart quotes") {
    // U+2018 / U+2019是3-byte UTF-8卷曲单引文.
    // 聪明 -quote 实现正常化 fuzzy_eq ,
    // 这些转换为 ASCII 在列文斯提因比较之前,所以线路完全吻合.
    std::string_view file = "print(\xe2\x80\x98hello\xe2\x80\x99)\n";
    auto r = fuzzy_find(file, "print('hello')");
    expect("smart-quotes normalized", r.ok,
          "expected smart-quote normalization to match curly quotes to ASCII");
}

TEST_CASE("class methods") {
    // 赛德的 test_resolve_location_class_methods ——针跳过一条线(" ⁇ "字).
    // three() 返回 333),因此必须使用 INSERTION_COST.
    std::string_view file =
        "class Something {\n"
        "    one() { return 1; }\n"
        "    two() { return 2222; }\n"
        "    three() { return 333; }\n"
        "    four() { return 4444; }\n"
        "    five() { return 5555; }\n"
        "    six() { return 6666; }\n"
        "    seven() { return 7; }\n"
        "    eight() { return 8; }\n"
        "}\n";
    std::string_view needle =
        "two() { return 2222; }\n"
        "four() { return 4444; }\n"
        "five() { return 5555; }\n"
        "six() { return 6666; }\n";
    auto r = fuzzy_find(file, needle);
    expect("multi-line match with skipped line",
          r.ok,
          "expected DP to align query lines across an inserted line");
}

