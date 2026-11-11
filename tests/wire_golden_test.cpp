// wire_golden_test — 字节-identity 守护着 Anthropic 传输 正文.
//
// 电话 Anthropic 提供者从一个单体分裂出来 transport.cpp
// 进入重点模块( 提示词 / SSE / 传输。每个拆分 必须
// 做一个纯粹的重构者: 字节 我们发送到 传输 无法更改,
// 或 提示词-cache 击破和行为漂移。 EXACT 序列化输出数
// messages_json_string () 故意致富的 线程
// - 文本转动, 终端 + 非 数 -terminal 工具 调用 ,
// 一个错误的结果,一个逐渐消退的大结果,一个紧凑的简单替代,
// 一个被取代的读物会崩溃。一个稳定的内容散列使得任何漂移都立即失败。
//
// 以下列方式计算: LEGITIMATE 传输 更改土地(新田,重新排序的密钥),
// 在下面更新 kGoldenHash——但只有在确认diff是打算的后.

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "agtest.hpp"

#include <nlohmann/json.hpp>

#include "agentty/domain/conversation.hpp"
#include "agentty/provider/anthropic/transport.hpp"
#include "agentty/provider/wire.hpp"

using agentty::Message;
using agentty::Role;
using agentty::Thread;
using agentty::ThreadId;
using agentty::ToolCallId;
using agentty::ToolName;
using agentty::ToolUse;

namespace {

ToolUse done_read(const char* id, const char* path, const std::string& out) {
    ToolUse tc;
    tc.id = ToolCallId{id};
    tc.name = ToolName{"read"};
    tc.args = nlohmann::json{{"path", path}};
    tc.status = ToolUse::Done{{}, {}, out};
    return tc;
}

ToolUse done_tool(const char* id, const char* name, nlohmann::json args,
                  const std::string& out) {
    ToolUse tc;
    tc.id = ToolCallId{id};
    tc.name = ToolName{name};
    tc.args = std::move(args);
    tc.status = ToolUse::Done{{}, {}, out};
    return tc;
}

// 固定的,特性-rich 线程. 定型——无时标,随机代号,
// 或env-dependent的内容泄露到messages_json_string的输出中.
Thread golden_thread() {
    std::vector<Message> msgs;

    Message u0; u0.role = Role::User; u0.text = "start the task";
    msgs.push_back(std::move(u0));

    // 助手读取一个文件(此读取后被编辑所取代).
    Message a0; a0.role = Role::Assistant; a0.text = "reading main.cpp";
    a0.tool_calls.push_back(done_read("toolu_r1", "src/main.cpp",
        std::string("FIRST_READ_") + std::string(4096, 'a')));
    msgs.push_back(std::move(a0));

    // 助理greps(大输出一旦老化就会淡出).
    Message a1; a1.role = Role::Assistant; a1.text = "searching";
    a1.tool_calls.push_back(done_tool("toolu_g1", "grep",
        nlohmann::json{{"pattern", "foo"}},
        std::string("GREP_HEAD_") + std::string(80 * 1024, 'g') + "_GREP_TAIL"));
    msgs.push_back(std::move(a1));

    // 助理编辑 main.cpp - 替换 toolu_r1 读了
    Message a2; a2.role = Role::Assistant; a2.text = "editing";
    a2.tool_calls.push_back(done_tool("toolu_e1", "edit",
        nlohmann::json{{"path", "src/main.cpp"}}, "edit applied"));
    msgs.push_back(std::move(a2));

    // 失败的工具( errors never fade/collapse)。
    Message a3; a3.role = Role::Assistant; a3.text = "build";
    ToolUse fail_tc;
    fail_tc.id = ToolCallId{"toolu_b1"};
    fail_tc.name = ToolName{"shell"};
    fail_tc.args = nlohmann::json{{"command", "make"}};
    fail_tc.status = ToolUse::Failed{{}, {}, "make: *** no rule"};
    a3.tool_calls.push_back(std::move(fail_tc));
    msgs.push_back(std::move(a3));

    // 最新回合:再 -read main.cpp (现场复制——必须保持充裕).
    Message a4; a4.role = Role::Assistant; a4.text = "re-reading";
    a4.tool_calls.push_back(done_read("toolu_r2", "src/main.cpp",
        std::string("SECOND_READ_") + std::string(4096, 'b')));
    msgs.push_back(std::move(a4));

    Thread t{ThreadId{"golden"}, "", std::move(msgs), {}, {}};
    return t;
}

// FNV-1a 64-bit——整个platforms/compilers(std::hash不是)稳定.
std::uint64_t fnv1a(const std::string& s) {
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    return h;
}

} // 命名空间结束。

TEST_CASE("wire_golden") {
    namespace ap = agentty::provider::anthropic;

    const std::string wire = ap::messages_json_string(golden_thread(),
                                                      /*include_thinking=*/false);

    // 1. 传输必须有效JSON(从未出现过畸形的正文)。
    bool parses = true;
    try { auto j = nlohmann::json::parse(wire); (void)j; }
    catch (...) { parses = false; }
    check(parses, "golden wire is valid JSON");

    // 2. 无论确切的散列物如何,必须坚持的特征论 --
    //    这些文件 内容 黄金输出包含。 NOTE:
    //    messages_json_string () 序列 t.messages 直接;
    //    紧凑 -summary SUBSTITUTION 发生 上游 输入
    //    wire_messages_for_impl (cmd_factory ),而不是在这个
    //    层次上——所以生的 CompactionRecord 自动调试 线程 故意拒绝 -op 给
    check(wire.find("FIRST_READ_") == std::string::npos,
          "superseded read body is collapsed (not shipped)");
    check(wire.find("content was dropped to save context") != std::string::npos,
          "superseded read carries the pointer text");
    check(wire.find("SECOND_READ_") != std::string::npos,
          "live (newest) read of the file ships in full");
    check(wire.find("make: *** no rule") != std::string::npos,
          "error result is never faded or collapsed");
    check(wire.find("GREP_HEAD_") != std::string::npos,
          "recent grep result head survives");

    // 3. 字节-identity : 准确的 序列化 被钉住。一个重构
    //    连一个都变了 字节 运行此进程。 更新 kGoldenHash
    //    仅 证实变更后为故意变更.
    const std::uint64_t kGoldenHash = 0x172e2b86aae44ee2ull;
    const std::uint64_t got = fnv1a(wire);

    if (kGoldenHash == 0) {
        // 首发/靴子:打印散列,以便其被固定.
        std::fprintf(stderr,
            "GOLDEN wire hash = 0x%016llxull  (len=%zu)\n",
            static_cast<unsigned long long>(got), wire.size());
    } else {
        check(got == kGoldenHash, "wire bytes are byte-identical to golden");
        if (got != kGoldenHash)
            std::fprintf(stderr,
                "  expected 0x%016llx got 0x%016llx\n",
                static_cast<unsigned long long>(kGoldenHash),
                static_cast<unsigned long long>(got));
    }
}

// 电话 正文 逃生者复制本简写 字节 输入 RUNS 而不是一次一个。
// 这是每个请求的热循环(漫长) 线程 内容 -escapes ),
// 所以它值得速度——但运行 -batching 错误
// 会腐蚀输入的黄金 线程 净水 ASCII 从未包含 :
// 嵌入式 NUL, 孤控 字节, 无效的 UTF-8,
// 字符串的端部有反斜。 驱动 真实 编码器与这些
// 字节 并需要结果返回到 解析。
TEST_CASE("wire escaping: hostile bytes round-trip through the encoder") {
    namespace ap = agentty::provider::anthropic;

    std::vector<std::string> payloads = {
        "plain", "\"quoted\"", "trailing backslash \\",
        "nl\nrt\r\ttab", std::string("embedded\0null", 13),
        "\x01\x02\x1f control", "emoji \xF0\x9F\x98\x80 done",
        "\x7f\x7f del", "quote-at-end \"",
    };
    // 每一个 字节 价值,单独计算,并加倍计算。
    for (int b = 0; b < 256; ++b) {
        payloads.push_back(std::string(1, static_cast<char>(b)));
        payloads.push_back(std::string(2, static_cast<char>(b)));
    }

    int checked = 0, skipped = 0;
    for (const auto& p : payloads) {
        Message u; u.role = Role::User; u.text = p;
        Thread t; t.messages.push_back(std::move(u));

        const std::string wire = ap::messages_json_string(t, false);
        nlohmann::json j;
        bool parsed = true;
        try { j = nlohmann::json::parse(wire); }
        catch (...) { parsed = false; }
        if (!parsed) {
            check(false, "encoder emitted unparseable JSON for a hostile payload");
            continue;
        }
        // scrub_utf8在出厂时可以合法地修复无效的UTF-8,
        // 因此与洗涤剂形式相比,而不是原始输入.
        const std::string want = agentty::provider::wire::scrub_utf8(p);
        // A 载荷 将清除到一无所有 完全不发出
        // 内容块( 空信件被丢弃) 上游 )——无比.
        if (want.empty() || j.empty() || j.at(0).at("content").empty()) {
            ++skipped;
            continue;
        }
        const std::string got = j.at(0).at("content").at(0).at("text")
                                 .get<std::string>();
        if (got != want)
            check(false, "escaped text did not round-trip");
        ++checked;
    }
    check(checked + skipped == static_cast<int>(payloads.size()),
          "every hostile payload round-tripped through the encoder");
    check(checked > 400, "the fuzz actually exercised the escaper");
}
