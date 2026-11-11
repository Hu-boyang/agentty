// tool_result_budget_test — 传输-only 字节
// -budget 工具结果的截断。 单一的超大小工具输出( 500) KiB
// 格瑞普,一个大文件的垃圾) 一定要 不 逐字逐字登上 传输 ;
// 它每转弯都会重播,在汽车之前很久就烧了标志 -compaction 火势
//
// 驱动 真实 传输 序列器( 3) messages_json_string
// 在一个手持笔记的助手转弯上 tool_call 并
// 声称:
//   1. 序列式的tool_result内容有限制(++预算+松懈)
//   2. 存在消除标记
//   3. 两者 头部和 末尾 字节 存活( Zed 保持两端)
//   4. a SMALL 结果为 字节-identical (无标,无截断).
//   5. 割裂的土地在 UTF -8边界( 无摩日贝克 / 无无效) JSON)

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "agtest.hpp"

#include "agentty/domain/conversation.hpp"
#include "agentty/provider/anthropic/transport.hpp"

using agentty::Message;
using agentty::Role;
using agentty::Thread;
using agentty::ThreadId;
using agentty::ToolCallId;
using agentty::ToolName;
using agentty::ToolUse;

namespace {


// 建造一个1-turn 线程:用户提示词 + 助理转弯,因此,
// 一个单Done tool_call 携带`output`。
Thread make_thread_with_tool_output(const std::string& output) {
    Message user;
    user.role = Role::User;
    user.text = "do a thing";

    ToolUse tc;
    tc.id   = ToolCallId{"toolu_test_1"};
    tc.name = ToolName{"grep"};
    tc.status = ToolUse::Done{
        std::chrono::steady_clock::now(),
        std::chrono::steady_clock::now(),
        output,
    };

    Message asst;
    asst.role = Role::Assistant;
    asst.text = "running grep";
    asst.tool_calls.push_back(std::move(tc));

    return Thread{ThreadId{"t"}, "", {user, asst}, {}, {}};
}

// 提取 tool_result 内容字符串 tool_use_id 从 传输.
std::string tool_result_content(const std::string& wire, const char* tool_use_id) {
    auto j = nlohmann::json::parse(wire);
    for (const auto& msg : j) {
        if (msg.value("role", "") != "user") continue;
        for (const auto& block : msg.value("content", nlohmann::json::array())) {
            if (block.value("type", "") != "tool_result") continue;
            if (block.value("tool_use_id", "") != tool_use_id) continue;
            return block.value("content", "");
        }
    }
    return {};
}

// 搭建一个拥有`n`助理工具-call转弯的线程,每个转弯都携带一个
// Done tool_call,其输出为`big`. 返回线程;
// FIRST工具转弯是最古老的(最高级的复位级),LAST是最新的(排名0).
Thread make_thread_with_n_tool_turns(int n, const std::string& big) {
    std::vector<Message> msgs;
    Message user; user.role = Role::User; user.text = "start";
    msgs.push_back(user);
    for (int i = 0; i < n; ++i) {
        ToolUse tc;
        tc.id   = ToolCallId{"toolu_" + std::to_string(i)};
        tc.name = ToolName{"grep"};
        tc.status = ToolUse::Done{
            std::chrono::steady_clock::now(),
            std::chrono::steady_clock::now(),
            big,
        };
        Message asst; asst.role = Role::Assistant;
        asst.text = "call " + std::to_string(i);
        asst.tool_calls.push_back(std::move(tc));
        msgs.push_back(std::move(asst));
    }
    return Thread{ThreadId{"t"}, "", std::move(msgs), {}, {}};
}

} // 命名空间结束。

TEST_CASE("tool result budget") {
    namespace ap = agentty::provider::anthropic;

    // ── 1. 成果不大: 字节-identical 过关,没有标记 ──
    {
        const std::string small = "matched 3 lines\nfoo\nbar\nbaz\n";
        Thread t = make_thread_with_tool_output(small);
        std::string wire = ap::messages_json_string(t);
        std::string content = tool_result_content(wire, "toolu_test_1");
        check(content == small, "small result ships verbatim");
        check(content.find("bytes elided") == std::string::npos,
              "small result has no elision marker");
    }

    // ── 2. 超大结果:有界限、标记存在、头部末尾保留 ──
    {
        // 500 KiB: a HEAD_MARKER ..装填器... TAIL_MARKER
        // 三明治 这样我们就可以断言两端都幸免于难了
        std::string big;
        big.reserve(512 * 1024);
        big += "HEAD_SENTINEL_AAAA\n";
        big.append(500 * 1024, 'x');
        big += "\nTAIL_SENTINEL_ZZZZ";

        Thread t = make_thread_with_tool_output(big);
        std::string wire = ap::messages_json_string(t);
        std::string content = tool_result_content(wire, "toolu_test_1");

        // 预算是64 KiB ; 序列化内容( 之后) JSON
        // 不可逃脱)必须远小于500 KiB 输入。
        // 允许宽宏大量放松标记+边界四舍五入。
        check(content.size() < 80 * 1024,
              "oversized result is bounded near the 64 KiB budget");
        check(content.size() < big.size(),
              "oversized result is smaller than the raw output");
        check(content.find("bytes elided") != std::string::npos,
              "oversized result carries the elision marker");
        check(content.find("HEAD_SENTINEL_AAAA") != std::string::npos,
              "head bytes survive the cut");
        check(content.find("TAIL_SENTINEL_ZZZZ") != std::string::npos,
              "tail bytes survive the cut");
    }

    // ── 3. 多字节边界:切切不得分割UTF-8序列. ──
    {
        // 装满3 -byte 代码点(U+4E2D, CJK 如此天真,如此天真 字
        // 节 切会降落在中途 -sequence。该词 传输 一定要还原 解析
        // 有效时 JSON (会丢在无效的 UTF -8/分头逃跑).
        std::string mb;
        const std::string cp = "\xE4\xB8\xAD"; // 中
        mb.reserve(200 * 1024);
        for (int i = 0; i < 70 * 1024; ++i) mb += cp;

        Thread t = make_thread_with_tool_output(mb);
        bool parsed_ok = true;
        std::string content;
        try {
            std::string wire = ap::messages_json_string(t);
            content = tool_result_content(wire, "toolu_test_1");
        } catch (...) {
            parsed_ok = false;
        }
        check(parsed_ok, "multibyte oversized result yields valid JSON");
        check(!content.empty(), "multibyte result content non-empty");
    }

    // ── 4. 空 点 -bytes 图像不得 序列化 空图像块 ──
    // 图像附件草稿,其 字节 已经排水 可能泄漏到一个 线程 (中文(简体) ).
    // -fix ),并显示为用户信息,带有 ImageContent
    // 空的 字节。该词 传输 必须完全放弃,排放 `"data":""`
    // 400s 整个请求( "image cannot be empty").
    // A 真实 图像在同一信息中仍然必须传送。
    {
        using agentty::ImageContent;
        using agentty::Message;
        using agentty::Role;

        Message user;
        user.role = Role::User;
        user.text = "look at this";
        ImageContent empty_img;          // 空的 字节 — 漏出形状
        empty_img.media_type = "image/png";
        user.images.push_back(std::move(empty_img));

        Thread t{ThreadId{"t"}, "", {user}, {}, {}};
        std::string wire = ap::messages_json_string(t);
        auto j = nlohmann::json::parse(wire);
        // 没有任何图像屏蔽; 消息仍然传送到它的文本。
        bool any_image = false, any_text = false;
        for (const auto& msg : j)
            for (const auto& b : msg.value("content", nlohmann::json::array())) {
                if (b.value("type", "") == "image")  any_image = true;
                if (b.value("type", "") == "text")   any_text  = true;
            }
        check(!any_image, "empty-bytes image is dropped from the wire");
        check(any_text, "the message's text still ships");

        // 现在,一个真实的形象与一个空的:只有真正的一艘船。
        Message user2;
        user2.role = Role::User;
        user2.text = "two images";
        ImageContent empty2; empty2.media_type = "image/png";
        ImageContent real2;  real2.media_type = "image/png";
        real2.set_bytes("\x89PNG\r\n\x1a\n realbytes");
        user2.images.push_back(std::move(empty2));
        user2.images.push_back(std::move(real2));
        Thread t2{ThreadId{"t"}, "", {user2}, {}, {}};
        auto j2 = nlohmann::json::parse(ap::messages_json_string(t2));
        int image_blocks = 0;
        for (const auto& msg : j2)
            for (const auto& b : msg.value("content", nlohmann::json::array()))
                if (b.value("type", "") == "image") {
                    ++image_blocks;
                    check(!b["source"].value("data", "").empty(),
                          "shipped image block has non-empty base64");
                }
        check(image_blocks == 1, "exactly the one real image ships");
    }

    // ── 5. 年代-tiered清除:最近完全、古老的已消失 ──
    // 长工具 -burst 转弯:20 调用 ,
    // 每次倾倒 60 KiB 最新~8必须保留全额预算(活动工作套);
    // 最老者必须崩溃到紧缺的已消逝预算。 过期 垃圾场停止重播。
    {
        std::string big;
        big += "HEAD_SENTINEL_AAAA\n";
        big.append(60 * 1024, 'x');
        big += "\nTAIL_SENTINEL_ZZZZ";

        const int n = 20;
        Thread t = make_thread_with_n_tool_turns(n, big);
        std::string wire = ap::messages_json_string(t);

        // 最新数据 调用 (单位:千美元) toolu_19 ,0级)保留全额预算.
        std::string newest = tool_result_content(wire, "toolu_19");
        check(newest.size() > 40 * 1024,
              "newest tool result keeps the full (large) budget");
        check(newest.find("HEAD_SENTINEL_AAAA") != std::string::npos
           && newest.find("TAIL_SENTINEL_ZZZZ") != std::string::npos,
              "newest result keeps head+tail");

        // 旧的 调用 (单位:千美元) toolu_0 ,19级) 被淡化为紧凑的预算.
        std::string oldest = tool_result_content(wire, "toolu_0");
        check(oldest.size() < 6 * 1024,
              "oldest tool result is faded to the tight budget");
        check(oldest.find("bytes elided") != std::string::npos,
              "faded result carries the elision marker");
        check(oldest.find("HEAD_SENTINEL_AAAA") != std::string::npos
           && oldest.find("TAIL_SENTINEL_ZZZZ") != std::string::npos,
              "faded result STILL keeps head+tail (recall, not replay)");

        // 最近的窗口内的结果( id toolu_15 ,
        // 4级不得 将逐渐消失。
        std::string recent = tool_result_content(wire, "toolu_15");
        check(recent.size() > 40 * 1024,
              "a result inside the recent window stays full");
    }

    // ── 6. 错误结果NEVER已消失,不论时间如何 ──
    {
        std::string bigerr;
        bigerr += "ERR_HEAD_AAAA\n";
        bigerr.append(60 * 1024, 'e');
        bigerr += "\nERR_TAIL_ZZZZ";

        // 20个转弯; OLDEST 失败( 错误) 调用 剩下的就完成了 都结束了
        std::vector<Message> msgs;
        Message user; user.role = Role::User; user.text = "start"; msgs.push_back(user);
        for (int i = 0; i < 20; ++i) {
            ToolUse tc;
            tc.id   = ToolCallId{"toolu_e" + std::to_string(i)};
            tc.name = ToolName{"shell"};
            if (i == 0) {
                tc.status = ToolUse::Failed{
                    std::chrono::steady_clock::now(),
                    std::chrono::steady_clock::now(), bigerr};
            } else {
                tc.status = ToolUse::Done{
                    std::chrono::steady_clock::now(),
                    std::chrono::steady_clock::now(), bigerr};
            }
            Message asst; asst.role = Role::Assistant; asst.text = "c";
            asst.tool_calls.push_back(std::move(tc));
            msgs.push_back(std::move(asst));
        }
        Thread t{ThreadId{"t"}, "", std::move(msgs), {}, {}};
        std::string wire = ap::messages_json_string(t);

        // 长者 调用 是一个 ERROR 即便在19级,它也保留了全额预算。
        std::string olderr = tool_result_content(wire, "toolu_e0");
        check(olderr.size() > 40 * 1024,
              "oldest ERROR result is NOT faded (model needs it to recover)");
    }

    // ── 7. 短篇旧结果逐字逐句(无所不减) ──
    {
        const std::string small = "3 matches\n";
        Thread t = make_thread_with_n_tool_turns(20, small);
        std::string wire = ap::messages_json_string(t);
        std::string oldest = tool_result_content(wire, "toolu_0");
        check(oldest == small,
              "small old result ships verbatim (no marker, no fade)");
    }

    // 8. 折叠已被取代的读取：文件较早的一次读取，
    //       内容a LATER 转回 -read 或编辑为崩溃 -line
    //       指针 当前 ,不留至年龄 -fade. ──
    {
        using json = nlohmann::json;
        std::string big;
        big += "FILE_HEAD_AAAA\n";
        big.append(60 * 1024, 'r');
        big += "\nFILE_TAIL_ZZZZ";

        auto read_tc = [&](const char* id, const char* path,
                           const std::string& out) {
            ToolUse tc;
            tc.id = ToolCallId{id};
            tc.name = ToolName{"read"};
            tc.args = json{{"path", path}};
            tc.status = ToolUse::Done{
                std::chrono::steady_clock::now(),
                std::chrono::steady_clock::now(), out};
            return tc;
        };
        auto edit_tc = [&](const char* id, const char* path) {
            ToolUse tc;
            tc.id = ToolCallId{id};
            tc.name = ToolName{"edit"};
            tc.args = json{{"path", path}};
            tc.status = ToolUse::Done{
                std::chrono::steady_clock::now(),
                std::chrono::steady_clock::now(), "edited"};
            return tc;
        };

        std::vector<Message> msgs;
        Message user; user.role = Role::User; user.text = "work"; msgs.push_back(user);

        // 转1:读作foo.cpp(这一段被转3编辑取代)
        { Message a; a.role = Role::Assistant; a.text = "read foo";
          a.tool_calls.push_back(read_tc("toolu_r_foo1", "src/foo.cpp", big));
          msgs.push_back(std::move(a)); }
        // 转二:读 bar.cpp (绝不 触摸到再次——必须保持饱满)
        { Message a; a.role = Role::Assistant; a.text = "read bar";
          a.tool_calls.push_back(read_tc("toolu_r_bar", "src/bar.cpp", big));
          msgs.push_back(std::move(a)); }
        // 转3: 编辑foo.cpp( 取代转1读 foo)
        { Message a; a.role = Role::Assistant; a.text = "edit foo";
          a.tool_calls.push_back(edit_tc("toolu_e_foo", "src/foo.cpp"));
          msgs.push_back(std::move(a)); }
        // 转4:读 foo.cpp 重播( 重播) LIVE 读 foo ——必须保持充裕).
        { Message a; a.role = Role::Assistant; a.text = "re-read foo";
          a.tool_calls.push_back(read_tc("toolu_r_foo2", "src/foo.cpp", big));
          msgs.push_back(std::move(a)); }

        Thread t{ThreadId{"t"}, "", std::move(msgs), {}, {}};
        std::string wire = ap::messages_json_string(t);

        std::string foo1 = tool_result_content(wire, "toolu_r_foo1");
        std::string bar  = tool_result_content(wire, "toolu_r_bar");
        std::string foo2 = tool_result_content(wire, "toolu_r_foo2");

        // 被取代的转弯-1读取崩溃到短指针。
        check(foo1.size() < 512,
              "superseded earlier read is collapsed to a one-line pointer");
        check(foo1.find("content was dropped to save context") != std::string::npos,
              "collapsed read carries the superseded pointer text");
        check(foo1.find("FILE_HEAD_AAAA") == std::string::npos,
              "collapsed read does NOT ship the original body");
        // 永远 -touched-again 读取时保持满(这是最近出现的+独有的).
        check(bar.size() > 40 * 1024,
              "a read whose file is never touched again stays full");
        // 最多一个 -recent 阅读foo是现场副本——从未崩溃。
        check(foo2.size() > 40 * 1024,
              "the newest read of a file is never collapsed");
    }
}
