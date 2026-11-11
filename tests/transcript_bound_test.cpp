// transcript_bound_test — write_thread_transcript_md 谓 BOUNDED ,
// 偏重, 正确( 正确 ) src/io/persistence.cpp).
//
// 笔录是叉子上的 -disk 内存 需要时按型号读取,
// 即使父母一方仍必须保留一个小的、有用的、
// 可粘贴的文物 线程 测试针:
//   1. 巨大的 线程 生成大小 -CAPPED 记录( 不包含多种) -MB).
//   2. 审慎偏见: NEWEST 转成生存; OLDEST 以一个
//      可见标记(最可能需要最近的上下文的叉)。
//   3. 一个巨人 `text` 块被剪切头+ 末尾 (不能支配).
//   4. Tool 调用倒塌为`› tool(name)`线;工具OUTPUT从不
//      写作最重的 字节 真实的 线程).
//   5. smart_routing 视图 -only 牌被跳过。
//   6. 产出有效 UTF -8甚至来自任意输入 字节.
//   7. 一个小 线程 圆形 -trips 逐字记录(合时不取消)。

#include "agtest.hpp"

#include "agentty/io/persistence.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <unistd.h>

namespace fs = std::filesystem;
using namespace agentty;


static std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static Message user_msg(std::string text) {
    Message m;
    m.role = Role::User;
    m.text = std::move(text);
    return m;
}

static Message asst_msg(std::string text) {
    Message m;
    m.role = Role::Assistant;
    m.text = std::move(text);
    return m;
}

TEST_CASE("transcript bound") {
    auto tmp = fs::temp_directory_path()
             / ("agentty_transcript_test_" + std::to_string(::getpid()));
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    ::setenv("HOME", tmp.c_str(), 1);
    ::unsetenv("USERPROFILE");

    // ── 1+2+3:一个大 线程 上限, 调整 -biased 单位, -msg 剪辑 ──
    {
        Thread t;
        t.id = persistence::new_id();
        t.title = "huge";
        // ~2号4000条消息 KB · 每一笔8 MB 原始文本——远远超过
        // 512 KB 盖。每个信件都单独标记,以便我们证明 对应的 幸存下来了
        for (int i = 0; i < 4000; ++i) {
            std::string body = "TAG" + std::to_string(i) + " ";
            body += std::string(2000, 'x');
            t.messages.push_back(i % 2 ? asst_msg(body) : user_msg(body));
        }
        // NEWEST消息中一个单张超大小的糊(必须剪切,
        // 不丢掉,不吹预算).
        Message big = user_msg("HEAD_MARKER " + std::string(400 * 1024, 'Z')
                               + " TAIL_MARKER");
        t.messages.push_back(std::move(big));

        auto path = persistence::write_thread_transcript_md(::agentty::IoAccess::grant(), t);
        check(!path.empty(), "huge thread: transcript written");
        std::string md = slurp(path);

        // 1. 总尺寸上限(小于头文件+标记松懈)。
        check(md.size() <= 512 * 1024 + 4096,
              "huge thread: total size capped (~512 KB), got "
              + std::to_string(md.size()) + " bytes");

        // 2. 谨慎:有最新标签,最古老的标签不见了。
        check(md.find("TAG3999") != std::string::npos,
              "recency: newest content kept");
        check(md.find("TAG0 ") == std::string::npos,
              "recency: oldest content elided");
        check(md.find("older turns elided") != std::string::npos,
              "recency: elision marker present");
        check(md.find("4001 messages total") != std::string::npos,
              "header reports full message count");

        // 3. 巨面被剪头+ 末尾 而不是整个掉下来。
        check(md.find("HEAD_MARKER") != std::string::npos,
              "clip: oversized text keeps its head");
        check(md.find("TAIL_MARKER") != std::string::npos,
              "clip: oversized text keeps its tail");
        check(md.find("bytes elided") != std::string::npos,
              "clip: per-message elision marker present");
    }

    // ── 4:工具 调用 崩溃; 工具 OUTPUT 从未写 ──
    {
        Thread t;
        t.id = persistence::new_id();
        t.title = "tools";
        Message a = asst_msg("running a command");
        ToolUse tc;
        tc.name = ToolName{"shell"};
        tc.args = nlohmann::json{{"command", "ls"}};
        // 模拟一个巨大的捕获产出——必须把NOT出现在记录稿中.
        tc.status = ToolUse::Done{ {}, {}, std::string(100 * 1024, 'O') };
        a.tool_calls.push_back(std::move(tc));
        t.messages.push_back(std::move(a));

        auto path = persistence::write_thread_transcript_md(::agentty::IoAccess::grant(), t);
        std::string md = slurp(path);
        check(md.find("\xe2\x80\xba tool(shell)") != std::string::npos,
              "tool call collapses to one line");
        check(md.find(std::string(200, 'O')) == std::string::npos,
              "tool OUTPUT is never written to the transcript");
    }

    // ── 5: smart_routing 视图 -only 牌被跳过 ──
    {
        Thread t;
        t.id = persistence::new_id();
        t.title = "routing";
        Message card;
        card.role = Role::User;
        card.smart_routing = true;
        card.smart_route_note = "ROUTING_CARD_NOISE";
        t.messages.push_back(std::move(card));
        t.messages.push_back(user_msg("real question"));

        auto path = persistence::write_thread_transcript_md(::agentty::IoAccess::grant(), t);
        std::string md = slurp(path);
        check(md.find("ROUTING_CARD_NOISE") == std::string::npos,
              "smart_routing card is skipped");
        check(md.find("real question") != std::string::npos,
              "real turn still present");
    }

    // ── 6:任意输入字节 − 有效UTF-8 产出 ──
    {
        Thread t;
        t.id = persistence::new_id();
        t.title = "utf8";
        // 独续 字节 + 短线 2 -byte 铅:无效 UTF-8.
        t.messages.push_back(user_msg(std::string("bad\x80\xC3 end", 8)));
        auto path = persistence::write_thread_transcript_md(::agentty::IoAccess::grant(), t);
        std::string md = slurp(path);
        // to_valid_utf8 替换 U+ FFFD 给坏人 字节 ;
        // 断言 OUTPUT 不错 -formed UTF -8(
        // 没有单独续作/短路铅可以到达.md) 而不是猜测擦拭的准确性 字节.
        check(!md.empty(), "invalid-UTF-8 input still produces a transcript");
        auto is_valid_utf8 = [](const std::string& s) {
            std::size_t i = 0, n = s.size();
            while (i < n) {
                unsigned char c = s[i];
                std::size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2
                                : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
                if (len == 0 || i + len > n) return false;
                for (std::size_t k = 1; k < len; ++k)
                    if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
                i += len;
            }
            return true;
        };
        check(is_valid_utf8(md),
              "transcript is well-formed UTF-8 even from garbage input");
    }

    // ── 7: 一个小的线程 适合逐字记录,没有消音 ──
    {
        Thread t;
        t.id = persistence::new_id();
        t.title = "small";
        t.messages.push_back(user_msg("hello"));
        t.messages.push_back(asst_msg("hi there"));
        auto path = persistence::write_thread_transcript_md(::agentty::IoAccess::grant(), t);
        std::string md = slurp(path);
        check(md.find("hello") != std::string::npos
           && md.find("hi there") != std::string::npos,
              "small thread: both turns present verbatim");
        check(md.find("older turns elided") == std::string::npos,
              "small thread: no elision marker");
        check(md.find("2 messages total") != std::string::npos,
              "small thread: count correct");
    }

    fs::remove_all(tmp);
}
