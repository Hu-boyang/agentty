// wire_supersede_test.cpp - 读数 -collapse 永远不能拉 字节.
//
// 两层独立的解析层在传输交汇:
//
//   fs.cpp 重复读取已读取 -served 范围解答
//                   "File unchanged since last read" 财务报告和已
//                   审计财务报表 POINTS 而不是重新调整 -sending 字节.
//   superseded_read_ids
//                   () 先前读过的文件再次被触动,
//                   则在 传输 由一个人 -line 指向新结果.
//
// 每一个都只是声音。它们曾经共同打破了交易,
// 它们都依赖于:读 #1是因为读 #2的存在而崩溃的,
// 读 #2本身只是一个哨兵——所以文件的 字节 出现在 NO 工具结果。
// 修复前的现场直播;模型自己的报告是
// 两个占位标记互相指向，却没有工具结果包含文件正文。
// 然后它又回到了 `cat`。这是读数 -loop / 贝壳
// -fallback 用户报告。
//
// 不变量 被钉在这里:任何路径,如果 字节 至少一个
// 幸存的工具结果仍然能带给他们。
#include <string>
#include <vector>

#include "agentty/provider/wire_supersede.hpp"
#include "agtest.hpp"

using namespace agentty;
namespace wire = agentty::provider::wire;

namespace {

ToolUse read_call(const char* id, const char* path, std::string output) {
    ToolUse tc;
    tc.id   = ToolCallId{id};
    tc.name = ToolName{"read"};
    tc.args = {{"path", path}};
    tc.status = ToolUse::Done{{}, {}, std::move(output)};
    return tc;
}

ToolUse edit_call(const char* id, const char* path) {
    ToolUse tc;
    tc.id   = ToolCallId{id};
    tc.name = ToolName{"edit"};
    tc.args = {{"path", path}};
    tc.status = ToolUse::Done{{}, {}, std::string{"Edited (1+ 1-)"}};
    return tc;
}

// 哨兵们 fs.cpp 重复已读的 -served 范围。
std::string unchanged_sentinel() {
    return "File unchanged since last read. The content from the earlier Read "
           "tool_result in this conversation is still current.";
}

std::vector<Message> thread_of(std::vector<ToolUse> calls) {
    Message m;
    m.role = Role::Assistant;
    m.tool_calls = std::move(calls);
    return {std::move(m)};
}

// 该 不变量 :一些幸存的结果仍然带有 字节.
bool body_survives(const std::vector<Message>& msgs, std::string_view needle) {
    const auto dead = wire::superseded_read_ids(msgs);
    for (const auto& m : msgs)
        for (const auto& tc : m.tool_calls)
            if (!dead.contains(tc.id.value)
                && tc.output().find(needle) != std::string::npos)
                return true;
    return false;
}

}  // 命名空间结束。

TEST_CASE("supersede: grep or outline of the same file never drops a read") {
    // 他们既不改变文件,也不携带读数的行,
    // 所以读数是唯一的抄本,必须留下.
    for (const char* tool : {"grep", "outline", "read_filter", "find_definition"}) {
        ToolUse other;
        other.id = ToolCallId{"c2"};
        other.name = ToolName{tool};
        other.args = {{"path", "/w/f.txt"}, {"pattern", "x"}};
        other.status = ToolUse::Done{{}, {}, std::string{"Found 1 match"}};
        auto msgs = thread_of({read_call("c1", "/w/f.txt", "line 1\nline 2\n"), other});
        CHECK_FALSE(wire::superseded_read_ids(msgs).contains("c1"));
        CHECK(body_survives(msgs, "line 2"));
    }
}

TEST_CASE("supersede: a sentinel-only read never collapses the real one") {
    // 该 BUG. 读 #1为正文服务;读 #2由fsdedup
    // 以指针回1回1回,折叠 #1离开字节无处可去.
    auto msgs = thread_of({
        read_call("c1", "/w/f.txt", "line 1\nline 2\nline 3\n"),
        read_call("c2", "/w/f.txt", unchanged_sentinel()),
    });

    const auto dead = wire::superseded_read_ids(msgs);
    CHECK_MESSAGE(!dead.contains("c1"),
                  "the only result holding the bytes must survive when the "
                  "newer read is just a pointer to it");
    CHECK_MESSAGE(body_survives(msgs, "line 2"),
                  "the file's content must exist in SOME surviving result");
}

TEST_CASE("supersede: a cached read carries its own bytes and may supersede") {
    // fs层不再用光指针回答重复——它重播它服务于 a 下
    // 的文本 `[cached …]` 标记。所以 缓存 击球
    // 是 完整 COPY 并且必须像其他任何读物一样行为:它取代
    // 了旧的结果,内容得以存活。
    //
    // 这是旧设计无法持有的属性。 指针只在目标位于 传输 和紧凑,
    // 完全崩溃, 和亚剂边界 都使其失效。
    // Claude 代码打开的失败与# 53578(读环)和#
    // 60684( 过期 快照); KhazAkar 其建议
    // -- -- 服务缓存值, 注释值 -- -- 删除类,
    // 而不是添加另一个 TTL 上上上上下上上上中气.
    auto msgs = thread_of({
        read_call("c1", "/w/f.txt", "line 1\nline 2\nline 3\n"),
        read_call("c2", "/w/f.txt",
                  "[cached \xe2\x80\x94 unchanged since your last read of this file]\n"
                  "line 1\nline 2\nline 3\n"),
    });

    const auto dead = wire::superseded_read_ids(msgs);
    CHECK_MESSAGE(dead.contains("c1"),
                  "a cached result holds the bytes, so the older copy is "
                  "genuinely redundant and should fade");
    CHECK_FALSE(dead.contains("c2"));
    // 该 不变量 ,而整个观点:即使第一部读物崩溃,
    // 内容仍然可以达到.
    CHECK_MESSAGE(body_survives(msgs, "line 2"),
                  "collapsing the older read must not strand the content when "
                  "the newer one is a cache hit");
}

TEST_CASE("supersede: a legacy sentinel from an old thread file still protects") {
    // A 线程 SAVED 由一个释放了旧的无内容的哨兵
    // 用文字进行补水的建筑。警卫们留下这些记录,
    // 以便这些记录继续工作:哨兵携带的不是 字节 ,所以它不能崩溃
    // 一个结果,做。
    auto msgs = thread_of({
        read_call("c1", "/w/f.txt", "line 1\nline 2\nline 3\n"),
        read_call("c2", "/w/f.txt", unchanged_sentinel()),
    });
    CHECK_FALSE(wire::superseded_read_ids(msgs).contains("c1"));
    CHECK(body_survives(msgs, "line 2"));
}

TEST_CASE("supersede: a real re-read still collapses the older one") {
    // 值得保持的行为: 两种都写着 字节 指年纪大
    // 的人 正文 冗余,所以它会逐渐消失到指针上。
    // 这是崩溃的整个点,不能倒退。
    auto msgs = thread_of({
        read_call("c1", "/w/f.txt", "old contents here\n"),
        read_call("c2", "/w/f.txt", "new contents here\n"),
    });

    const auto dead = wire::superseded_read_ids(msgs);
    CHECK(dead.contains("c1"));
    CHECK_FALSE(dead.contains("c2"));
    CHECK(body_survives(msgs, "new contents"));
}

TEST_CASE("supersede: an edit still collapses the pre-edit read") {
    // 编辑会真正使旧的无效 正文 ,不管它包含什么,
    // 所以即使编辑结果没有文件,这种崩溃是正确的 字
    // 节 自己来的
    auto msgs = thread_of({
        read_call("c1", "/w/f.txt", "before the edit\n"),
        edit_call("c2", "/w/f.txt"),
    });
    CHECK(wire::superseded_read_ids(msgs).contains("c1"));
}

TEST_CASE("supersede: three reads keep exactly the newest body") {
    // 读, 读,读,读。中间的不得成为现场副本,
    // 也不得崩溃第一个; 第三个带 字节 ,所以到那时,
    // 第一个 IS 冗余。
    auto msgs = thread_of({
        read_call("c1", "/w/f.txt", "first body\n"),
        read_call("c2", "/w/f.txt", unchanged_sentinel()),
        read_call("c3", "/w/f.txt", "third body\n"),
    });

    const auto dead = wire::superseded_read_ids(msgs);
    CHECK(dead.contains("c1"));
    CHECK_FALSE(dead.contains("c3"));
    CHECK(body_survives(msgs, "third body"));
}

TEST_CASE("supersede: a sentinel read of a never-read file strands nothing") {
    // 脱源但可达到: fs 缓存 活过一个 线程 切换,
    // 所以第一个读 IN 此 THREAD 没有什么可以崩溃的,
    // 也没有可以声称是活的。
    auto msgs = thread_of({
        read_call("c1", "/w/f.txt", unchanged_sentinel()),
    });
    CHECK(wire::superseded_read_ids(msgs).empty());
}

TEST_CASE("supersede: different paths never interfere") {
    auto msgs = thread_of({
        read_call("c1", "/w/a.txt", "alpha body\n"),
        read_call("c2", "/w/b.txt", "beta body\n"),
        read_call("c3", "/w/a.txt", "alpha again\n"),
    });

    const auto dead = wire::superseded_read_ids(msgs);
    CHECK(dead.contains("c1"));        // 由c3取代
    CHECK_FALSE(dead.contains("c2"));  // 不同的文件
    CHECK(body_survives(msgs, "beta body"));
}

TEST_CASE("supersede: a failed read is left alone") {
    // 错误文本永远不会淡出——模型需要整个失败才能恢复.
    ToolUse bad;
    bad.id   = ToolCallId{"c1"};
    bad.name = ToolName{"read"};
    bad.args = {{"path", "/w/missing.txt"}};
    bad.status = ToolUse::Failed{{}, {}, "file not found: /w/missing.txt"};

    auto msgs = thread_of({std::move(bad),
                           read_call("c2", "/w/missing.txt", "now it exists\n")});
    CHECK_FALSE(wire::superseded_read_ids(msgs).contains("c1"));
}

// ── 正在呼求一个大文件 ────────────────────────────────────────────────────
//
// 该 第二 BUG 矿发现的矿床 线程 :3323中的121读作"回向"作为指针出现,
// 大 -file 审计是崩溃的。 崩溃是仅靠路径键, 所以窗口 # 2 被擦
// 除的窗口 # 1 , 尽管它从未持有过这些线条。 四个子代理读了 one
// 1990 -line 文件全部已报告
// "my reads returned superseded, audit incomplete".（读取结果显示已被取代，审计尚未完成。）

// 窗口读取,页脚为 `read` 实际打印。
ToolUse window_read(const char* id, const char* path, int lo, int hi,
                    int total, std::string body) {
    ToolUse tc;
    tc.id   = ToolCallId{id};
    tc.name = ToolName{"read"};
    tc.args = {{"path", path}, {"start_line", lo}, {"end_line", hi}};
    body += "\n[showing lines " + std::to_string(lo) + "-" + std::to_string(hi)
          + " of " + std::to_string(total) + "]";
    tc.status = ToolUse::Done{{}, {}, std::move(body)};
    return tc;
}

TEST_CASE("supersede: two different windows of one file both survive") {
    auto msgs = thread_of({
        window_read("c1", "/w/big.cpp", 100, 200, 1990, "the first slice\n"),
        window_read("c2", "/w/big.cpp", 900, 1000, 1990, "the second slice\n"),
    });

    const auto dead = wire::superseded_read_ids(msgs);
    CHECK_MESSAGE(dead.empty(),
                  "a read of lines 900-1000 does not carry lines 100-200, so "
                  "it cannot stand in for them");
    CHECK(body_survives(msgs, "the first slice"));
    CHECK(body_survives(msgs, "the second slice"));
}

TEST_CASE("supersede: overlapping windows that don't contain each other both survive") {
    auto msgs = thread_of({
        window_read("c1", "/w/big.cpp", 100, 300, 1990, "slice A\n"),
        window_read("c2", "/w/big.cpp", 200, 400, 1990, "slice B\n"),
    });
    CHECK(wire::superseded_read_ids(msgs).empty());
    CHECK(body_survives(msgs, "slice A"));
}

TEST_CASE("supersede: a containing window collapses the one inside it") {
    // 象征还是能赢得它的声音
    auto msgs = thread_of({
        window_read("c1", "/w/big.cpp", 150, 160, 1990, "inner\n"),
        window_read("c2", "/w/big.cpp", 100, 200, 1990, "inner plus more\n"),
    });

    const auto dead = wire::superseded_read_ids(msgs);
    CHECK(dead.contains("c1"));
    CHECK_FALSE(dead.contains("c2"));
    CHECK(body_survives(msgs, "inner plus more"));
}

TEST_CASE("supersede: a whole-file read collapses every window before it") {
    // 没有脚印意味着`read`没有扣下任何东西,所以这个结果维持了批量.
    auto msgs = thread_of({
        window_read("c1", "/w/f.txt", 1, 10, 400, "head\n"),
        window_read("c2", "/w/f.txt", 300, 310, 400, "tail\n"),
        read_call("c3", "/w/f.txt", "the entire file\n"),
    });

    const auto dead = wire::superseded_read_ids(msgs);
    CHECK(dead.contains("c1"));
    CHECK(dead.contains("c2"));
    CHECK_FALSE(dead.contains("c3"));
}

TEST_CASE("supersede: a window does NOT collapse an earlier whole-file read") {
    auto msgs = thread_of({
        read_call("c1", "/w/f.txt", "the entire file\n"),
        window_read("c2", "/w/f.txt", 300, 310, 400, "one slice\n"),
    });
    CHECK(wire::superseded_read_ids(msgs).empty());
    CHECK(body_survives(msgs, "the entire file"));
}

TEST_CASE("supersede: an outline neither collapses nor is collapsed") {
    // 大纲是符号指数,不是内容,不能站入一个行范围,
    // 一个行范围也不包含.
    ToolUse outline;
    outline.id   = ToolCallId{"c2"};
    outline.name = ToolName{"read"};
    outline.args = {{"path", "/w/big.cpp"}};
    outline.status = ToolUse::Done{{}, {},
        "This file is large, so here is its shape.\n\n# Outline of /w/big.cpp\n\n"
        "L10: void f()\nL80: void g()\n"};

    auto msgs = thread_of({
        window_read("c1", "/w/big.cpp", 100, 200, 1990, "a slice\n"),
        std::move(outline),
        window_read("c3", "/w/big.cpp", 100, 200, 1990, "the same slice\n"),
    });

    const auto dead = wire::superseded_read_ids(msgs);
    CHECK_MESSAGE(!dead.contains("c2"),
                  "a window read must not collapse an outline");
    CHECK_MESSAGE(dead.contains("c1"),
                  "the identical later window still collapses the earlier one "
                  "across the outline");
}

TEST_CASE("supersede: a past-the-end read carries nothing and collapses nothing") {
    ToolUse past;
    past.id   = ToolCallId{"c2"};
    past.name = ToolName{"read"};
    past.args = {{"path", "/w/f.txt"}, {"offset", 9000}};
    past.status = ToolUse::Done{{}, {},
        "[offset 9000 is past the end of the file, which has 400 lines "
        "\xe2\x80\x94 nothing to show. Re-read with an offset \xe2\x89\xa4 400.]"};

    auto msgs = thread_of({
        read_call("c1", "/w/f.txt", "the entire file\n"),
        std::move(past),
    });
    CHECK(wire::superseded_read_ids(msgs).empty());
    CHECK(body_survives(msgs, "the entire file"));
}

TEST_CASE("supersede: an edit collapses every live window of the file") {
    // 范围不输入:文件已经更改,所以每早一次 正文 谓
    // 过期 随便它拿的什么片子
    auto msgs = thread_of({
        window_read("c1", "/w/f.txt", 1, 10, 400, "head\n"),
        window_read("c2", "/w/f.txt", 300, 310, 400, "tail\n"),
        edit_call("c3", "/w/f.txt"),
    });

    const auto dead = wire::superseded_read_ids(msgs);
    CHECK(dead.contains("c1"));
    CHECK(dead.contains("c2"));
}

TEST_CASE("supersede: an edit collapses a read spelled with a different path") {
    // 比较是用字符串,所以两种拼写必须正常化。
    // 传输 服务于预 -edit 字节 仿佛是时空的
    auto msgs = thread_of({
        read_call("c1", "/w/sub/../f.txt", "before the edit\n"),
        edit_call("c2", "/w/f.txt"),
    });
    CHECK(wire::superseded_read_ids(msgs).contains("c1"));
}

TEST_CASE("supersede: an unparsable footer falls back to the args") {
    // 由旧建筑所写的线程,或重写脚本。这些词组仍然
    // 表示需要哪个切片。
    ToolUse odd;
    odd.id   = ToolCallId{"c1"};
    odd.name = ToolName{"read"};
    odd.args = {{"path", "/w/f.txt"}, {"start_line", 150}, {"end_line", 160}};
    odd.status = ToolUse::Done{{}, {}, "inner\n[showing lines ??? of 400]"};

    auto msgs = thread_of({
        std::move(odd),
        window_read("c2", "/w/f.txt", 100, 200, 400, "inner plus more\n"),
    });
    CHECK_MESSAGE(wire::superseded_read_ids(msgs).contains("c1"),
                  "args-derived span 150-160 sits inside 100-200");
}

TEST_CASE("supersede: a file that QUOTES the sentinel still counts as a body") {
    // 此 仓库 自己的 wire_supersede.hpp 和 此测试
    // 文件都包含指针文本。 一个叫做这样读无体的子字符串搜索,
    // 它从 -invalidation 路径中完全丢弃了它 — 因此,
    // 后期编辑在 -edit 字节 上留下的 传输 的当前图像。
    ToolUse quoting;
    quoting.id   = ToolCallId{"c1"};
    quoting.name = ToolName{"read"};
    quoting.args = {{"path", "/w/wire_supersede.hpp"}};
    quoting.status = ToolUse::Done{{}, {},
        std::string{"constexpr auto kPointer =\n  \""}
        + std::string{wire::kSupersededReadPointer} + "\";\n"};

    CHECK(wire::read_result_has_body(quoting));

    auto msgs = thread_of({std::move(quoting), edit_call("c2", "/w/wire_supersede.hpp")});
    CHECK_MESSAGE(wire::superseded_read_ids(msgs).contains("c1"),
                  "the edit must still invalidate it");
}

TEST_CASE("supersede: the pointer itself is not a body") {
    ToolUse pointer;
    pointer.id   = ToolCallId{"c1"};
    pointer.name = ToolName{"read"};
    pointer.args = {{"path", "/w/f.txt"}};
    pointer.status = ToolUse::Done{{}, {}, std::string{wire::kSupersededReadPointer}};
    CHECK_FALSE(wire::read_result_has_body(pointer));
}

TEST_CASE("supersede: a tail read has no absolute span, so it is left alone") {
    // 偏移:-50表示"the last 50 lines"——这几条线
    // 取决于 ⁇ 格不携带的长度.
    ToolUse tail;
    tail.id   = ToolCallId{"c1"};
    tail.name = ToolName{"read"};
    tail.args = {{"path", "/w/log.txt"}, {"offset", -50}};
    tail.status = ToolUse::Done{{}, {}, "the last fifty\n[showing lines ? of ?]"};

    auto msgs = thread_of({
        std::move(tail),
        window_read("c2", "/w/log.txt", 1, 2000, 9000, "the head\n"),
    });
    CHECK(wire::superseded_read_ids(msgs).empty());
    CHECK(body_survives(msgs, "the last fifty"));
}
