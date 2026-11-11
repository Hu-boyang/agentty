// capped_read_test - 盖上的那个 -read 原始的,
// 还有只有一条的规则
//
// 该 RULE
// ========
// 大小限制读取的文件通过 util::capped_read。
// 没有文件写自己的文件。
//
// 之所以存在是因为12个 -line 帮助者写了四次, 四个都运走了同样的错误: 他们
// 返回了 "" 费用 两者 "no such file" 财务报告和已审计财务报表
// "file is over the cap" ,所以每个打电话者
// `if (raw.empty()) continue;` 默然放弃了第二个案子 那要付出什么代价?
//
//   技能 超大 SKILL.md 消失与 `0 warning(s)`
//   钩子 `agentty hooks` 说 "no hooks file"
//             与文件 在那里, 和 阻断 PreToolUse 钩
//   是惰性 AGENTS.md 切到 64 KiB 没有标记,所以一个规则
//             靠近底部 从来没有到达模型
//
// 三个独立的发现,三个独立的修正,一个缺失的类型. jaal 第一条
// 原则是 "contracts are types" - 所以,
// 这个固定是 变体 其 人 名 TooBig 选项无法通过空字符串到达,
// 而此测试会防止第四份副本被写入。

#include "agtest.hpp"

#include "agentty/util/capped_read.hpp"

#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

#if defined(_WIN32)
#  include <process.h>
#  define getpid _getpid
#else
#  include <unistd.h>
#endif

namespace fs = std::filesystem;
namespace u  = agentty::util;

namespace {

[[nodiscard]] fs::path sandbox() {
    // 每条进程:ctest将每个案例作为自己的进程并行运行,
    // 一个共享目录让一个案例的remove_all删除另一个案例的文件.
    const auto d = fs::temp_directory_path()
        / ("agentty_capped_read_test." + std::to_string(::getpid()));
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

void write_file(const fs::path& p, const std::string& body) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << body;
}

[[nodiscard]] fs::path repo_root() {
    std::error_code ec;
    for (fs::path p = fs::current_path(ec); !p.empty(); p = p.parent_path()) {
        if (fs::exists(p / "cmake" / "AgenttySources.cmake", ec)) return p;
        if (p == p.parent_path()) break;
    }
    return {};
}

}  // 命名空间结束。

TEST_CASE("capped_read: the four outcomes are distinct") {
    const auto dir = sandbox();

    // 内容。
    write_file(dir / "ok.txt", "hello");
    const auto ok = u::capped_read(dir / "ok.txt", 1024);
    CHECK(std::holds_alternative<u::Content>(ok));
    CHECK(u::bytes_or_empty(ok) == "hello");

    // 缺席 -- -- 而一个MISSING文件是缺席的,而不是错误。
    CHECK(std::holds_alternative<u::Absent>(
        u::capped_read(dir / "nope.txt", 1024)));

    // TooBig携带着真实的尺寸,所以一个呼叫者可以说出有多远.
    write_file(dir / "big.txt", std::string(2048, 'x'));
    const auto big = u::capped_read(dir / "big.txt", 1024);
    REQUIRE(std::holds_alternative<u::TooBig>(big));
    CHECK(std::get<u::TooBig>(big).size == 2048);
    CHECK(u::oversize_bytes(big) == 2048);

    // 该 bug, 作为断言 : TooBig 和缺席不能看起来相似。
    // 四个被运送的帮手中,每一个都在这里失败了
    CHECK(u::capped_read(dir / "big.txt", 1024).index()
          != u::capped_read(dir / "nope.txt", 1024).index());

    fs::remove_all(dir);
}

TEST_CASE("capped_read: a directory is Absent, never a crash") {
    // std::ifstream OPENS 成功打开目录 Linux 并丢出
    // 第一个读数。 agentty 一次,
    // 从 `mkdir .agentty/mcp.json` ,则信息名称中也没有
    // agentty 也不要走这条路 is_regular_file 检查前面是什么 使它不可能。
    const auto dir = sandbox();
    fs::create_directories(dir / "shaped-like-a-file.json");
    CHECK(std::holds_alternative<u::Absent>(
        u::capped_read(dir / "shaped-like-a-file.json", 1024)));
    fs::remove_all(dir);
}

TEST_CASE("capped_read: an empty file is Content, not Absent") {
    // "The user wrote an empty hooks.json" 财务报告和已审计
    // 财务报表 "there is no hooks.json" 电话商可以把他们都一样
    const auto dir = sandbox();
    write_file(dir / "empty.txt", "");
    const auto got = u::capped_read(dir / "empty.txt", 1024);
    CHECK(std::holds_alternative<u::Content>(got));
    CHECK(u::bytes_or_empty(got).empty());
    fs::remove_all(dir);
}

TEST_CASE("capped_read: the note explains only what needs explaining") {
    const auto dir = sandbox();
    write_file(dir / "big.txt", std::string(70u * 1024u, 'x'));
    write_file(dir / "ok.txt", "small");

    const auto note = u::capped_note(u::capped_read(dir / "big.txt", 64u * 1024u),
                                     64u * 1024u);
    CHECK(note.find("70 KB") != std::string::npos);
    CHECK(note.find("64 KB") != std::string::npos);
    CHECK(note.find("NOT loaded") != std::string::npos);

    // 内容和缺席都说不出话来 -- -- 也不需要。
    CHECK(u::capped_note(u::capped_read(dir / "ok.txt", 1024), 1024).empty());
    CHECK(u::capped_note(u::capped_read(dir / "nope", 1024), 1024).empty());
    fs::remove_all(dir);
}

TEST_CASE("no file writes its own capped reader") {
    // 纪律的一半。这四个帮助者有四份,
    // 而且所有四个都一样是错误的;在复核时抓住第五份
    // 正是前四次没有发生的情况。
    const fs::path root = repo_root();
    REQUIRE(!root.empty(), "found the checkout root");

    // 本地读取的帮助者会使用一个盖。 每份副本的形状匹配: `read_capped(path, cap)`
    // / `read_all(path, cap)`。
    const std::regex decl(
        R"((read_capped|read_all)\s*\([^)]*\b(cap|kMax\w*Bytes)\b)");

    std::vector<std::string> offenders;
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(root / "src", ec)) {
        if (!e.is_regular_file(ec)) continue;
        if (e.path().extension() != ".cpp") continue;
        const std::string rel = fs::relative(e.path(), root, ec).generic_string();

        std::ifstream in(e.path());
        std::string line;
        int n = 0;
        while (std::getline(in, line)) {
            ++n;
            // 仅 DEFINITIONS ,没有 调用 进入共享的。
            if (line.find("util::capped_read") != std::string::npos) continue;
            if (line.find("//") == 0) continue;
            if (std::regex_search(line, decl)
                && line.find("[[nodiscard]]") != std::string::npos)
                offenders.push_back(rel + ":" + std::to_string(n));
        }
    }

    for (const auto& o : offenders)
        MESSAGE("defines its own capped reader: " << o);
    CHECK(offenders.empty(),
          "capped reads go through util::capped_read");
}
