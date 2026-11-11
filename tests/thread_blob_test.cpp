// thread_blob_test ——图像 字节 和大型工具产出必须圆 -trip
// 穿过布洛布商店 字节-for-byte ,必须保留 线程 JSON 细小的。
//
// 为什么存在: 一个真正的 线程 已经达到31个了 MB 其中,10.8个
// MB 曾经是 base64 图像数据和 12 MB 工具输出, 而不是 0.
// 7 MB 全部是真实的谈话文本 -read 和关于 -parsed 时值
// 线程 切换,所以打开长 线程 停止了。。 UI 超过一秒钟。 字节 现在
// 住在这里 threads/blobs/<hash> 信息会保留一个参考信息。
//
// 合同有两半 两者 问题: 载荷 必须是完全相同的
// 返回( 丢失的商店) 字节 更慢的,和 JSON
// 必须是实际缩小(或者没有购买的更改)。

#include "agtest.hpp"

#include "agentty/io/persistence.hpp"
#include "agentty/domain/conversation.hpp"
#include "agentty/util/base64.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace agentty;

// load_thread 过着幸福的生活 io::FsStore; 持久化 显示文件 -level 表格中。
static std::optional<Thread> load_t(const ThreadId& id) {
    // 通过STOREXXSEAM,不是硬码路径:一个已保存XX
    // 线程现在以日志格式存在,<id>.JSON一旦核实,即退役.
    return persistence::load_thread_by_id(::agentty::IoAccess::grant(), id);
}

namespace {

[[nodiscard]] std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string{std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>()};
}

// 与文本管道敌对的 字节: NULs, 一个单独的 0xFF,
// 以及一个无效的 UTF- 8. 序列。 blob 绝不能对这些序列进行消毒。
[[nodiscard]] std::string binary_payload(std::size_t n) {
    std::string s;
    s.reserve(n);
    s += "\x89PNG\r\n\x1a\n";              // 真正的PNG魔法
    for (std::size_t i = s.size(); i < n; ++i)
        s.push_back(static_cast<char>(i * 7919u));   // 完全 0.255 范围
    return s;
}

} // 命名空间结束。

TEST_CASE("thread blobs: images round-trip byte-exact and shrink the JSON") {
    Message user;
    user.role = Role::User;
    user.text = "look at this";
    ImageContent img;
    img.media_type = "image/png";
    img.set_bytes(binary_payload(300u * 1024u));   // 现实的截图
    const std::string original = img.bytes();
    user.images.push_back(std::move(img));

    Thread t{ThreadId{"blobtest"}, "blob round-trip", {user}, {}, {}};
    persistence::save_thread(::agentty::IoAccess::grant(), agtest::cap(), t);
    persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());

    const fs::path file =
        persistence::threads_dir() / "blobtest.json";
    const std::string json_text = read_file(file);

    // 300号 KB 载荷 不得 坐在这 线程 JSON. base64
    // 大约是400块 KB ; 参考文献有几十个 字节.
    CHECK_MESSAGE(json_text.size() < 32u * 1024u,
                  "thread JSON must not carry the image payload inline");

    auto loaded = load_t(ThreadId{"blobtest"});
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->messages.size() == 1u);
    REQUIRE(loaded->messages[0].images.size() == 1u);
    CHECK_MESSAGE(loaded->messages[0].images[0].bytes() == original,
                  "image bytes must survive verbatim, NUls and all");
    CHECK(loaded->messages[0].images[0].media_type == "image/png");

    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), ThreadId{"blobtest"});
}

TEST_CASE("thread blobs: large tool output round-trips") {
    Message asst;
    asst.role = Role::Assistant;
    asst.text = "ran it";
    ToolUse tc;
    tc.id   = ToolCallId{"call_1"};
    tc.name = ToolName{"shell"};
    // 过了8KB的内线阈值
    std::string big;
    for (int i = 0; i < 4000; ++i)
        big += "line " + std::to_string(i) + " of captured output\n";
    const std::string original = big;
    ToolUse::Done done;
    done.output = big;
    tc.status = std::move(done);
    asst.tool_calls.push_back(std::move(tc));

    Thread t{ThreadId{"blobtool"}, "tool blob", {asst}, {}, {}};
    persistence::save_thread(::agentty::IoAccess::grant(), agtest::cap(), t);
    persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());

    const std::string json_text =
        read_file(persistence::threads_dir() / "blobtool.json");
    CHECK_MESSAGE(json_text.size() < 8u * 1024u,
                  "large tool output must not be inline in the thread JSON");

    auto loaded = load_t(ThreadId{"blobtool"});
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->messages.size() == 1u);
    REQUIRE(loaded->messages[0].tool_calls.size() == 1u);
    CHECK_MESSAGE(loaded->messages[0].tool_calls[0].output() == original,
                  "tool output must survive verbatim");

    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), ThreadId{"blobtool"});
}

TEST_CASE("thread blobs: identical payloads share one file") {
    // 内容处理意味着同一截图贴了两次——或同一转盘重复试验
    // ——费用为1 blob,而不是N。
    const std::string payload = binary_payload(64u * 1024u);

    auto make = [&](const char* id) {
        Message m;
        m.role = Role::User;
        m.text = "same image";
        ImageContent img;
        img.media_type = "image/png";
        img.set_bytes(payload);
        m.images.push_back(std::move(img));
        Thread t{ThreadId{id}, "dedup", {m}, {}, {}};
        persistence::save_thread(::agentty::IoAccess::grant(), agtest::cap(), t);
        persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());
    };

    const fs::path blobs = persistence::threads_dir() / "blobs";
    std::error_code ec;
    const auto count_blobs = [&] {
        std::size_t n = 0;
        for (auto& e : fs::directory_iterator(blobs, ec))
            if (e.is_regular_file(ec)) ++n;
        return n;
    };

    make("dedup_a");
    const std::size_t after_first = count_blobs();
    make("dedup_b");
    const std::size_t after_second = count_blobs();

    CHECK_MESSAGE(after_second == after_first,
                  "an identical payload must not create a second blob");

    // 两者 线程 仍然读回他们的形象。
    for (const char* id : {"dedup_a", "dedup_b"}) {
        auto loaded = load_t(ThreadId{id});
        REQUIRE(loaded.has_value());
        REQUIRE(loaded->messages[0].images.size() == 1u);
        CHECK(loaded->messages[0].images[0].bytes() == payload);
        persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), ThreadId{id});
    }
}

TEST_CASE("thread blobs: thinking payloads round-trip") {
    // thinking_blocks + 签名为 6.8。 MB 实际32个 MB 线程.
    // 从未展示过这些内容,只是重新播放给提供者,
    // 但 Anthropic 400s a tool_use
    // 转过身来,他们必须回来 恰好 不只是约。
    Message a;
    a.role = Role::Assistant;
    a.text = "done";
    std::string reasoning;
    for (int i = 0; i < 3000; ++i)
        reasoning += "considering branch " + std::to_string(i) + "\n";
    std::string sig(12u * 1024u, 'S');   // 供应商签名不透明
    a.thinking = reasoning;
    a.thinking_signature = sig;
    a.thinking_blocks.push_back(
        Message::ThinkingBlock{reasoning, sig, ""});

    Thread t{ThreadId{"blobthink"}, "thinking blob", {a}, {}, {}};
    persistence::save_thread(::agentty::IoAccess::grant(), agtest::cap(), t);
    persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());

    const std::string json_text =
        read_file(persistence::threads_dir() / "blobthink.json");
    CHECK_MESSAGE(json_text.size() < 8u * 1024u,
                  "thinking payloads must not sit inline in the thread JSON");

    auto loaded = load_t(ThreadId{"blobthink"});
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->messages.size() == 1u);
    const auto& got = loaded->messages[0];
    CHECK(got.thinking == reasoning);
    CHECK(got.thinking_signature == sig);
    REQUIRE(got.thinking_blocks.size() == 1u);
    CHECK_MESSAGE(got.thinking_blocks[0].text == reasoning,
                  "block text must replay verbatim");
    CHECK_MESSAGE(got.thinking_blocks[0].signature == sig,
                  "a mangled signature 400s the next turn");

    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), ThreadId{"blobthink"});
}

TEST_CASE("thread blobs: a legacy inline thread still loads") {
    // 线程 在blob 商店保持内线之前写入 base64
    // "data" 字段。它们必须保持工作,没有迁移步骤,
    // 否则,将磁盘上已经存在的每个图像都静默地升级。
    const std::string bytes = binary_payload(4096);
    Message m;
    m.role = Role::User;
    m.text = "legacy";
    ImageContent img;
    img.media_type = "image/png";
    img.set_bytes(bytes);
    m.images.push_back(std::move(img));
    Thread t{ThreadId{"legacyimg"}, "legacy", {m}, {}, {}};
    // 种子 a LEGACY 整体 -document 直接存档。
    // save_thread () 向日志格式迁移,并退出。
    // JSON ,因此它不能再用来制造这个测试所涉及的旧形状.
    const fs::path file = persistence::threads_dir() / "legacyimg.json";
    {
        auto j = persistence::thread_meta_to_json(t);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& mm : t.messages)
            arr.push_back(persistence::message_to_json(mm));
        j["messages"] = std::move(arr);
        persistence::write_json_atomic(::agentty::IoAccess::grant(), file, j.dump(2));
    }

    // 重写文件以OLD形状:内置数据,无 blob 引用.
    std::string text = read_file(file);
    const auto pos = text.find("\"blob\"");
    REQUIRE(pos != std::string::npos);
    // 交换旧编剧制作的内置编码的参考.
    const auto end = text.find('"', text.find(':', pos) + 2);
    const auto close = text.find('"', end + 1);
    text.replace(pos, close - pos + 1,
                 "\"data\":\"" + util::base64_encode(bytes) + "\"");
    { std::ofstream out(file, std::ios::binary); out << text; }

    auto loaded = load_t(ThreadId{"legacyimg"});
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->messages[0].images.size() == 1u);
    CHECK_MESSAGE(loaded->messages[0].images[0].bytes() == bytes,
                  "inline legacy images must still decode");

    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), ThreadId{"legacyimg"});
}
