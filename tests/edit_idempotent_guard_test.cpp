// edit_idempotent_guard_test ——打错了字 old_text 绝不能将它看成是成功。
//
// ── 原因 ──────────────────────────────────────────────────────────────────
//
// `edit` 有个 "already applied" 快捷键:如果 old_text
// 不见了,而且 new_text 存在,一个 -run 编辑器,但已登陆的编辑器
// 报告成功,而不是混淆的否 -match 值得一试
//
// 信号太弱了,它接受了 new_text 发生 ANYWHERE ,有8层 字节。所以,
// 一个 MISTYPED old_text 其 人 名 new_text 恰好是一条普通
// 的线条 -- `return false;`, `    });`,
// `#include <string>` - 找到了它,我们找到了它 new_text 文件的
// 其他地方和工具都答了题 "identical content (file unchanged on disk)"
// 什么都没有写, 调用 成功,模型继续 建立在从未发生的变化之上。
//
// 这是这个工具最糟糕的缺陷:不是错误的编辑,
// 是相信的。
#include "agtest.hpp"

#include "agentty/tool/registry.hpp"
#include "agentty/tool/tool.hpp"
#include "agentty/tool/util/fs_helpers.hpp"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <string>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

// 工作区 根内的文件,因为 fs 工具拒绝其它任何文件。
// .agentty/下的NOT - 交接门将那作为host-trusted(
// hoks run from there)处理,并否认写作,这是正确的.
fs::path scratch(const char* tag) {
    auto dir = agentty::tools::util::workspace_root() / "build" / "test-scratch";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir / ("edit_guard_" + std::string{tag} + ".txt");
}

void put(const fs::path& p, std::string_view body) {
    std::ofstream o(p, std::ios::binary | std::ios::trunc);
    o.write(body.data(), static_cast<std::streamsize>(body.size()));
}

std::string get(const fs::path& p) {
    std::ifstream i(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(i)),
                       std::istreambuf_iterator<char>());
}

agentty::tools::ExecResult do_edit(const fs::path& p,
                                   std::string old_text, std::string new_text) {
    return agentty::tool::DynamicDispatch::execute(
        agtest::cap(), "edit", json{{"path", p.string()},
                     {"edits", json::array({json{{"old_text", std::move(old_text)},
                                                 {"new_text", std::move(new_text)}}})}});
}

} // 命名空间结束。

TEST_CASE("edit guard: a mistyped old_text does not pass as already-applied") {
    // `    return false;` 显示 TWICE 在同一缩影中,
    // 它不是独一无二的 -placed new_text。模型要求替换一个没有在
    // 文件中的行。在修复之前,任何地方的存在都足够:这个报告
    // "already present, move on" 写了些什么
    const auto p = scratch("mistyped");
    put(p,
        "int a() {\n"
        "    return false;\n"
        "}\n"
        "int b() {\n"
        "    return false;\n"
        "}\n");
    const auto before = get(p);

    auto r = do_edit(p, "    int totally_absent_line = 7;\n",
                        "    return false;\n");

    CHECK_MESSAGE(!r.has_value(),
                  "old_text is absent and new_text is NOT uniquely placed, so "
                  "this has to be a no-match error, not a success");
    CHECK_MESSAGE(get(p) == before, "and nothing may be written either way");
    fs::remove(p);
}

TEST_CASE("edit guard: a genuine retry still reports already-applied") {
    // 快捷键存在于: 编辑器已登陆, 模式 重试 同样的 调用.
    // new_text 与众不同,而且一次就在场
    const auto p = scratch("retry");
    put(p,
        "int a() {\n"
        "    return compute_the_answer(42);\n"
        "}\n");
    const auto before = get(p);

    auto r = do_edit(p, "    return compute(41);\n",
                        "    return compute_the_answer(42);\n");

    CHECK_MESSAGE(r.has_value(),
                  "old_text gone, new_text present exactly once -- a retry of "
                  "an edit that already landed");
    if (r) CHECK(r->text.find("No write needed") != std::string::npos);
    CHECK(get(p) == before);
    fs::remove(p);
}

TEST_CASE("edit guard: a normal edit still applies") {
    const auto p = scratch("normal");
    put(p, "int a() {\n    return 1;\n}\n");

    auto r = do_edit(p, "    return 1;\n", "    return 2;\n");

    REQUIRE(r.has_value());
    CHECK(get(p).find("return 2;") != std::string::npos);
    CHECK(get(p).find("return 1;") == std::string::npos);
    fs::remove(p);
}

TEST_CASE("edit guard: the no-op message does not tell the model to move on") {
    // 这是一种热力,所以它必须报告什么是 OBSERVED 并说如何检查。
    // "the desired state is already in place; move on" ,
    // 这正是一个错误的类型 old_text 不应告诉。
    const auto p = scratch("message");
    put(p, "int a() {\n    return compute_the_answer(42);\n}\n");

    auto r = do_edit(p, "    return compute(41);\n",
                        "    return compute_the_answer(42);\n");
    REQUIRE(r.has_value());
    CHECK(r->text.find("read the file back") != std::string::npos);
    CHECK_MESSAGE(r->text.find("move on") == std::string::npos,
                  "never instruct the model to proceed on a guess");
    fs::remove(p);
}

// ── 通过符号链接编辑 ───────────────────────────────────────────
//
// 出版步骤是 rename() ,替换 NAME 用来
// 连接一个符号 链接 自己,所以编辑一个 一次做三个错误的事情,
// 默默地:
//
//   * 符号链接成为普通文件(链接被销毁),
//   * 真正的目标保留了旧内容(编辑没有进行),
//   * 该工具报告成功,变化很大。
//
// 量度的,没有说明理由: 我做了一个 链接 ,编辑后,找到了 链接
// 目标没有被移动。 仓库 和 共线生成的文件( Q)
// compile_commands.json )在第一次编辑时点击此功能.

TEST_CASE("edit through a symlink writes the target and keeps the link") {
    const auto dir = agentty::tools::util::workspace_root() / "build" / "test-scratch";
    std::error_code ec;
    fs::create_directories(dir, ec);
    const auto target = dir / "symlink_target.txt";
    const auto link   = dir / "symlink_alias.txt";

    fs::remove(link, ec);
    fs::remove(target, ec);
    put(target, "alpha\nbeta\n");
    fs::create_symlink(target.filename(), link, ec);
    if (ec) return;   // 此主机上没有符号链接支持( N) Windows w/o 特权)
    REQUIRE(fs::is_symlink(link, ec));

    auto r = agentty::tool::DynamicDispatch::execute(
        agtest::cap(), "edit", json{{"path", link.string()},
                     {"edits", json::array({json{{"old_text", "beta"},
                                                 {"new_text", "BETA"}}})}});
    REQUIRE(r.has_value());

    CHECK_MESSAGE(fs::is_symlink(link, ec),
                  "the link must survive the edit -- replacing it with a "
                  "regular file silently breaks whatever pointed at it");
    CHECK_MESSAGE(get(target).find("BETA") != std::string::npos,
                  "and the edit has to land in the TARGET, not in a new inode "
                  "nobody is looking at");
    CHECK(get(target).find("beta") == std::string::npos);

    fs::remove(link, ec);
    fs::remove(target, ec);
}

// ── `line:` 提示 ────────────────────────────────────────────────────
//
// `line:` 现作为文件记录。 DISAMBIGUATOR ,
// 作为硬过滤器,它会伤害:行号走 过期 不断( 早先编辑时已扩展
// 文件) , 拒绝正确的编辑。 过期 数字比忽略它更糟糕。
//
// 但曾经是完全抛弃的 DP 只有一个候选人,
// 而这个案子没有其他证据。 孤独的模糊与900行相匹配,
// 而所谓的来电者却默默地使用了这些线。
//
// 规则:提示只约束我们所在的地方 ALREADY 猜的,你猜对了,我猜对了
//
//   精确匹配, 任意距离 - > 应用( 读取提示前返回) 模糊,
//   接近提示 - > 适用模糊,
//   无提示 - >应用( 无主张) 模糊 ,
//   远离给定提示 - > reference, 并指定它发现的行

namespace {

// 离顶部很远的有特殊行的文件,加填充符.
std::string spread_file(const char* needle_line, int at_line) {
    std::string s;
    for (int i = 1; i < at_line; ++i)
        s += "    int filler_" + std::to_string(i) + " = 0;\n";
    s += needle_line;
    s += "\n";
    for (int i = 0; i < 40; ++i) s += "    int tail = 1;\n";
    return s;
}

agentty::tools::ExecResult edit_at(const fs::path& p, std::string old_text,
                                   std::string new_text, int line) {
    json one{{"old_text", std::move(old_text)}, {"new_text", std::move(new_text)}};
    if (line > 0) one["line"] = line;
    return agentty::tool::DynamicDispatch::execute(
        agtest::cap(), "edit", json{{"path", p.string()}, {"edits", json::array({one})}});
}

} // 命名空间结束。

TEST_CASE("line hint: an EXACT match is applied however stale the hint") {
    // 普通案例这绝不能打破:区域移动,
    // 呼叫者记忆中的行号错误,但文字准确.
    const auto p = scratch("hint_exact");
    put(p, spread_file("    const int target = 7;", 600));

    auto r = edit_at(p, "    const int target = 7;",
                        "    const int target = 8;", /*线条=*/5);
    REQUIRE_MESSAGE(r.has_value(),
                    "an exact match must not be refused over a stale line");
    CHECK(get(p).find("target = 8;") != std::string::npos);
    fs::remove(p);
}

TEST_CASE("line hint: a FUZZY match far from the hint is refused, with the line") {
    // 只有一个大致的候选者,它坐着~600行,
    // 从呼叫者说到这里。两轴上都同时弱,这是默默错误编辑
    // 的形状。
    const auto p = scratch("hint_far");
    put(p, spread_file("    const int target = 7;   // trailing note", 600));
    const auto before = get(p);

    auto r = edit_at(p, "    const int target = 7;   // trailing note!",
                        "    const int target = 8;", /*线条=*/5);

    REQUIRE_FALSE(r.has_value());
    const auto& detail = r.error().detail;
    const bool names_the_line = detail.find("600") != std::string::npos
                             || detail.find("601") != std::string::npos;
    CHECK_MESSAGE(names_the_line,
                  "the refusal has to name where it DID find it -- 'no match' "
                  "would send the caller on a blind retry");
    CHECK_MESSAGE(get(p) == before, "and nothing is written");
    fs::remove(p);
}

TEST_CASE("line hint: a FUZZY match near the hint still applies") {
    const auto p = scratch("hint_near");
    put(p, spread_file("    const int target = 7;   // trailing note", 600));

    auto r = edit_at(p, "    const int target = 7;   // trailing note!",
                        "    const int target = 8;", /*线条=*/595);
    REQUIRE_MESSAGE(r.has_value(),
                    "within tolerance the hint corroborates rather than blocks");
    CHECK(get(p).find("target = 8;") != std::string::npos);
    fs::remove(p);
}

TEST_CASE("line hint: with NO hint a lone fuzzy match still applies") {
    // 没有任何东西可以声称,所以没有什么可以相互矛盾的。
    // 没有改变的行为—— 定律不能让提示-less编辑更加严格。
    const auto p = scratch("hint_none");
    put(p, spread_file("    const int target = 7;   // trailing note", 600));

    auto r = edit_at(p, "    const int target = 7;   // trailing note!",
                        "    const int target = 8;", /*线条=*/0);
    REQUIRE(r.has_value());
    CHECK(get(p).find("target = 8;") != std::string::npos);
    fs::remove(p);
}
