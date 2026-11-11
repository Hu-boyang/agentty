// SPDX-License-Identifier: Apache-2.0
//
// workspace_index_test.cpp — 锁 作曲家 @/
// # 选择面板 逻辑:模糊文件过滤器,frecency排名(最近)
// -referenced 文件浮到顶端),和非 -blocking 准备
// -probe 合同,磁盘本身不是单位 -tested (它需要一个真实的,
// 源码树 );我们测试每个键盘上运行的纯滤波器+节俭层.

#include "agentty/workspace/files.hpp"

#include <doctest/doctest.h>
#include "agtest.hpp"

#include <algorithm>
#include <string>
#include <thread>
#include <vector>

using agentty::filter_files;
using agentty::note_file_referenced;

TEST_CASE("filter_files: fuzzy subsequence match") {
    std::vector<std::string> files = {
        "src/runtime/app/update/composer.cpp",
        "src/workspace/files.cpp",
        "include/agentty/workspace/files.hpp",
        "README.md",
    };

    // 空查询返回所有信息。
    CHECK(filter_files(files, "").size() == files.size());

    // "files" 匹配两者 files.cpp 财务报告和已审计财务报表 files.hpp ,没有 composer/README.
    auto m = filter_files(files, "files");
    CHECK(m.size() == 2);
    for (auto i : m) CHECK(files[i].find("files.") != std::string::npos);

    // 分序排列:"wsfiles"点击workspace/files.
    auto m2 = filter_files(files, "wsfiles");
    CHECK(!m2.empty());
    CHECK(files[m2[0]].find("workspace/files") != std::string::npos);

    // 找不到匹配 ~ 空。
    CHECK(filter_files(files, "zzzznotathing").empty());
}

TEST_CASE("filter_files: frecency floats referenced files to the top") {
    std::vector<std::string> files = {
        "a/alpha.cpp",
        "b/beta.cpp",
        "c/gamma.cpp",
    };

    // 尚未有节奏:空-query顺序是输入(α)顺序.
    {
        auto m = filter_files(files, "");
        REQUIRE(m.size() == 3);
        CHECK(files[m[0]] == "a/alpha.cpp");
    }

    // 参考伽玛,然后是β. β是大多数-recent → 排名第一,
    // 伽玛第二,α(从未被参考)最后.
    note_file_referenced("c/gamma.cpp");
    note_file_referenced("b/beta.cpp");
    {
        auto m = filter_files(files, "");
        REQUIRE(m.size() == 3);
        CHECK(files[m[0]] == "b/beta.cpp");    // 最近一次 时间
        CHECK(files[m[1]] == "c/gamma.cpp");   // 下一个
        CHECK(files[m[2]] == "a/alpha.cpp");   // 从未引用
    }

    // Re-referencing伽玛将其移动回前方(dedupe + hoist).
    note_file_referenced("c/gamma.cpp");
    {
        auto m = filter_files(files, "");
        REQUIRE(m.size() == 3);
        CHECK(files[m[0]] == "c/gamma.cpp");
    }

    // 频率也会在模糊的查询中断开链接: 两者都是 *. cpp 匹配 ".cpp" ;
    // 最近的情况 -referenced 一个人应该排在前面。
    {
        auto m = filter_files(files, "cpp");
        REQUIRE(m.size() == 3);
        // 伽马是最后一个 + 在.cpp 匹配中的第一个。
        CHECK(files[m[0]] == "c/gamma.cpp");
    }
}

#include "agentty/tool/util/fs_helpers.hpp"
#include "agentty/workspace/symbols.hpp"
#include "agentty/tool/util/subprocess.hpp"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>

// 电话 加载-bearing "smart" 行为: 一种 git
// -DIRTY 文档在空白中排在其它所有位置 `@` ,甚至一个节俭的条目,
// 因为你现在正在编辑的文件几乎总是你想要的。 仓库 这么说吧
// build_git_signals () (状态) --porcelain )
// 有读物.
TEST_CASE("filter_files: git-dirty files lead the working set") {
    namespace fs = std::filesystem;
    auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path repo = fs::temp_directory_path() /
                    ("agentty_gitrank_" + std::to_string(nonce));
    fs::create_directories(repo);
    auto sh = [&](const std::string& c) {
        (void)agentty::tools::util::run_command_s(
            "cd " + repo.string() + " && " + c, 8192, std::chrono::seconds{15});
    };
    auto write = [&](const std::string& rel, const std::string& body) {
        std::ofstream f(repo / rel); f << body;
    };

    sh("git init -q && git config user.email t@t && git config user.name t");
    write("clean_a.cpp", "int a() { return 1; }\n");
    write("clean_b.cpp", "int b() { return 2; }\n");
    write("hot.cpp",     "int hot() { return 3; }\n");
    sh("git add -A && git commit -qm init");
    // 现在,肮脏的ONE文件 在犯罪之后。
    write("hot.cpp", "int hot() { return 3; } // edited\n");

    // 点 project_root 固定位置上,并同步重建基特信号地图
    // ( refresh_git_signals 读取当前工程根的
    // git 状态 —— 确定性, 不预置 -ordering 竞态).
    auto prev = fs::current_path();
    const auto prev_ws = agentty::tools::util::project_root();
    fs::current_path(repo);
    agentty::tools::util::set_workspace_root(::agentty::IoAccess::grant(), repo);
    agentty::refresh_git_signals();

    std::vector<std::string> files = {
        "clean_a.cpp", "clean_b.cpp", "hot.cpp",
    };
    auto m = filter_files(files, "");
    REQUIRE(m.size() == 3);
    // hot.cpp是唯一经过修改的文件 − 它必须领先于字母顺序的
    // -earlier clean_a/clean_b.
    CHECK(files[m[0]] == "hot.cpp");
    CHECK(agentty::file_git_tag("hot.cpp") == agentty::GitTag::Modified);

    // 恢复 两者 阴性 工作区 删除固定字段前的 root —— 见下文的
    // 兄弟式测试, 用于为什么在其中留下一个已删除的路径。
    // project_root (a) 断裂 LATER 测试的包 沙箱 而不是这个
    fs::current_path(prev);
    agentty::tools::util::set_workspace_root(::agentty::IoAccess::grant(), prev_ws);
    std::error_code ec;
    fs::remove_all(repo, ec);
}

TEST_CASE("symbol matcher: one declaration per language, comments skipped") {
    using agentty::symbol_on_line;
    CHECK(symbol_on_line("class Foo {") == "Foo");
    CHECK(symbol_on_line("  enum class Color : int {") == "Color");
    CHECK(symbol_on_line("namespace agentty {") == "agentty");
    CHECK(symbol_on_line("typedef unsigned long u64;") == "u64");
    CHECK(symbol_on_line("static int helper(int a, int b) {") == "helper");
    CHECK(symbol_on_line("std::string name() const") == "");    // 末尾 岂不是 `{` 或 或 时 间 EOL
    CHECK(symbol_on_line("void run()") == "run");
    CHECK(symbol_on_line("def handle(self, x):") == "handle");
    CHECK(symbol_on_line("export async function load() {") == "load");
    CHECK(symbol_on_line("func (s *Server) Serve() error {") == "Serve");
    CHECK(symbol_on_line("type Handler interface {") == "Handler");
    CHECK(symbol_on_line("pub(crate) fn parse(input: &str) -> Ast {") == "parse");
    CHECK(symbol_on_line("    // fn not_this()") == "");
    CHECK(symbol_on_line("x = f(1);") == "");
    CHECK(symbol_on_line(std::string(600, 'a') + " f() {") == "");   // 已修改
}

TEST_CASE("prewarm walk bails promptly when cancelled") {
    namespace fs = std::filesystem;
    // 足够文件的目录,一个未取消的全扫描可以测量,
    // 所以 提示词 返回证明 取消 实际短 -circuits.
    auto root = fs::temp_directory_path() /
        ("agentty_prewarm_cancel_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    for (int i = 0; i < 3000; ++i) {
        std::ofstream(root / ("f" + std::to_string(i) + ".cpp"))
            << "int f" << i << "(){return " << i << ";}\n";
    }
    auto prev = fs::current_path();
    const auto prev_ws = agentty::tools::util::project_root();
    fs::current_path(root);
    agentty::tools::util::set_workspace_root(::agentty::IoAccess::grant(), root);

    // 停止 之前 踢走: 循环的首个检查几乎立刻看到它并保释。
    // 它不能扫描所有 3000 文件。
    std::stop_source stop;
    stop.request_stop();

    const auto t0 = std::chrono::steady_clock::now();
    agentty::prewarm_workspace_files(stop.get_token());
    agentty::prewarm_workspace_symbols(agtest::cap(), stop.get_token());
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    CHECK(ms < 1000);   // 慷慨; 完整的 3000-file 扫描速度要慢得多

    // 恢复 两者 删除前的全局状态 源码树.
    //
    // cwd 已恢复到这里; 工作区 根没有,所以这个测试用来离开
    // `project_root()` 指向目录,然后删除。任何后期的测试,
    // 如果它构建了 沙箱 从 工作区 根 (正数) hooks_gate ,
    // 然后失败
    // "bwrap: Can't find source path /tmp/agentty_prewarm_cancel_..." ,（bwrap 找不到探测用的源路径。）
    // 不过 仅 运行时 -suite 之后——这就是为什么它
    // 孤立地通过,看起来像一个无关的片段。
    fs::current_path(prev);
    agentty::tools::util::set_workspace_root(::agentty::IoAccess::grant(), prev_ws);
    std::error_code ec;
    fs::remove_all(root, ec);
}
