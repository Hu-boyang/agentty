// thread_save_incremental_test — a 保存 写着改变的事物
//
// save_thread () 用于重写和重写 -verify
// 每轮都记录着整个木头 使整个木头变得很长 线程 越慢越慢。
// 它现在会切除第一个更改的信件和附件的日志。
// 这些测试将双半边标注: 末尾 路径被真正采用(在变化未
// 触碰之前的历史),并且各种变化仍然在磁盘上正确着陆.

#include "agtest.hpp"

#include <agentty/io/persistence.hpp>
#include <agentty/io/thread_log.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using namespace agentty;

namespace {

Message make_msg(std::size_t i) {
    Message m;
    m.role = (i % 2 == 0) ? Role::User : Role::Assistant;
    m.id   = MessageId{"m" + std::to_string(i)};
    m.text = "message " + std::to_string(i);
    return m;
}

Thread make_thread(std::string id, std::size_t n) {
    Thread t;
    t.id    = ThreadId{std::move(id)};
    t.title = "incremental subject";
    for (std::size_t i = 0; i < n; ++i) t.messages.push_back(make_msg(i));
    return t;
}

fs::path log_path(const Thread& t) {
    return persistence::threads_dir() / (t.id.value + ".jsonl");
}

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void spit(const fs::path& p, const std::string& s) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << s;
}

void save(const Thread& t) {
    persistence::save_thread(::agentty::IoAccess::grant(), agtest::cap(), t);
    persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());
}

Thread load(const Thread& t) {
    auto got = persistence::load_thread_by_id(::agentty::IoAccess::grant(), t.id);
    REQUIRE(got.has_value());
    return std::move(*got);
}

void check_same(const Thread& want, const Thread& got) {
    REQUIRE(got.messages.size() == want.messages.size());
    for (std::size_t i = 0; i < want.messages.size(); ++i) {
        CHECK(got.messages[i].id.value == want.messages[i].id.value);
        CHECK(got.messages[i].text == want.messages[i].text);
    }
}

// 在磁盘上标记信件 0而不更改长度,
// 因此抵消仍然有效。 留下标记的 保存 没有重写头。
void mark_head(const Thread& t) {
    auto s = slurp(log_path(t));
    const auto at = s.find("message 0\"");
    REQUIRE(at != std::string::npos);
    s.replace(at, 9, "MESSAGE 0");
    spit(log_path(t), s);
}
bool head_marked(const Thread& t) {
    return slurp(log_path(t)).find("MESSAGE 0") != std::string::npos;
}

} // 命名空间结束。

TEST_CASE("incremental save: a new message is appended, history untouched") {
    Thread t = make_thread("inc-append", 6);
    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), t.id);
    save(t);                         // 进程的第一个保存: 完全
    mark_head(t);

    t.messages.push_back(make_msg(6));
    save(t);                         // 末尾 保存:仅发文6

    CHECK_MESSAGE(head_marked(t), "the head was rewritten; the tail path was not taken");
    auto got = load(t);
    REQUIRE(got.messages.size() == 7u);
    CHECK(got.messages[6].text == "message 6");
    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), t.id);
}

TEST_CASE("incremental save: the open turn is replaced, not duplicated") {
    Thread t = make_thread("inc-open-turn", 4);
    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), t.id);
    save(t);
    mark_head(t);

    // 最后一个消息在它数据流/它的工具结算时会不断改变.
    for (int round = 0; round < 5; ++round) {
        t.messages.back().text = "streaming " + std::to_string(round);
        save(t);
    }
    CHECK(head_marked(t));
    auto got = load(t);
    REQUIRE(got.messages.size() == 4u);
    CHECK(got.messages.back().text == "streaming 4");
    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), t.id);
}

TEST_CASE("incremental save: editing an old message rewrites from there") {
    Thread t = make_thread("inc-edit", 8);
    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), t.id);
    save(t);

    // 目的长度相同:大小 -only 指纹 这会错过这个。
    t.messages[3].text = "MESSAGE 3";
    save(t);
    check_same(t, load(t));
    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), t.id);
}

// 电话 状态更新函数 '路径:它记得 FPS 其最后决定
// 保存 并且只发出自那以后的变化,而不等待作家在中间。
namespace {
bool send(const Thread& t, std::vector<std::uint64_t>& saved) {
    auto d = persistence::describe_save(t, saved);
    saved = d.fps;
    return persistence::save_thread_delta(::agentty::IoAccess::grant(), agtest::cap(), std::move(d));
}
}

TEST_CASE("delta save: back-to-back deltas merge in the queue and land whole") {
    Thread t = make_thread("delta-merge", 5);
    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), t.id);
    std::vector<std::uint64_t> saved;
    REQUIRE(send(t, saved));                  // 整体: 尚未有已保存
    // 几轮没有冲浪:每个三角洲只携带自己的末尾,
    // 一些工作的土地仍然排队。合并必须保留全部。
    for (int i = 5; i < 12; ++i) {
        t.messages.push_back(make_msg(i));
        if (i % 3 == 0) t.messages[2].text = "edit " + std::to_string(i);
        REQUIRE(send(t, saved));
    }
    persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());
    check_same(t, load(t));
    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), t.id);
}

TEST_CASE("delta save: a delta on an unknown base is refused, a whole save is not") {
    Thread t = make_thread("delta-refused", 4);
    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), t.id);
    std::vector<std::uint64_t> saved;
    REQUIRE(send(t, saved));
    persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());

    // 作家忘记了一个基础 当一个 保存 无法创建( 或删除时将
    // 其删除 )。 必须在该基底上创建的三角洲必须拒绝,
    // 而不是在无法保存的日志上写。 模拟失败 forget().
    persistence::debug_forget_saved(agtest::cap(), t.id);
    t.messages.push_back(make_msg(4));
    CHECK_FALSE(send(t, saved));

    // 或 状态更新函数 显示在 SaveRefused :放下它的基地,发送整个.
    std::vector<std::uint64_t> none;
    REQUIRE(send(t, none));
    persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());
    check_same(t, load(t));
    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), t.id);
}

TEST_CASE("delta save: a delta queued after a delete is refused, not written") {
    Thread t = make_thread("delta-deleted", 3);
    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), t.id);
    std::vector<std::uint64_t> saved;
    REQUIRE(send(t, saved));
    persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());
    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), t.id);

    // 删掉的基数,所以上面的末尾没有下面的东西.
    t.messages.push_back(make_msg(3));
    CHECK_FALSE(send(t, saved));
    persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());
    CHECK_FALSE(std::filesystem::exists(
        persistence::threads_dir() / (t.id.value + ".jsonl")));
}

TEST_CASE("incremental save: a shorter thread (rewind) truncates the log") {
    Thread t = make_thread("inc-shrink", 10);
    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), t.id);
    save(t);

    t.messages.resize(4);
    t.messages.push_back(make_msg(99));
    save(t);
    check_same(t, load(t));
    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), t.id);
}

TEST_CASE("incremental save: a replaced prefix (compaction/fork) still lands") {
    Thread t = make_thread("inc-replace", 6);
    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), t.id);
    save(t);

    Thread fresh = make_thread("inc-replace", 3);
    fresh.messages[0].text = "a completely different head";
    save(fresh);
    check_same(fresh, load(fresh));
    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), t.id);
}

TEST_CASE("incremental save: Smart Mode cards don't shift the log indices") {
    Thread t = make_thread("inc-smart-card", 4);
    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), t.id);
    save(t);

    Message card;
    card.smart_routing = true;          // 视图-only, 从未持续
    t.messages.insert(t.messages.begin() + 2, card);
    t.messages.push_back(make_msg(4));
    save(t);

    auto got = load(t);
    REQUIRE(got.messages.size() == 5u);
    CHECK(got.messages[2].id.value == "m2");
    CHECK(got.messages[4].id.value == "m4");
    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), t.id);
}

TEST_CASE("incremental save: after delete, the next save is a full write") {
    Thread t = make_thread("inc-delete", 5);
    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), t.id);
    save(t);
    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), t.id);
    CHECK_FALSE(fs::exists(log_path(t)));

    // 作者不能认为(现在缺失的)日志仍然有5行.
    t.messages.push_back(make_msg(5));
    save(t);
    check_same(t, load(t));
    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), t.id);
}

TEST_CASE("incremental save: a log damaged behind the writer's back heals") {
    Thread t = make_thread("inc-damaged", 5);
    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), t.id);
    save(t);

    // 有人把日志切成零字 末尾 保存 看见日志比
    // 想象的短;它不能从磁盘重建头部,
    // 所以它会放下账簿和 下一个 保存 满了就治好了
    spit(log_path(t), "");
    fs::remove(persistence::threads_dir() / (t.id.value + ".ofs"));
    t.messages.push_back(make_msg(5));
    save(t);
    t.messages.push_back(make_msg(6));
    save(t);
    check_same(t, load(t));
    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), t.id);
}

// 电话 指纹 是什么决定了哪些信息 a 保存 重写,
// 所以改变它 FAILS 通知是无声数据丢失:编辑保留在
// 内存 永远无法到达磁盘, 并且已经消失在下一个 加载。
// 散列混合了四个独立航道,以达到速度,这正是可以意外掉下
// 一个的改变。 字节 范围——如此针入每个持续字段,
// 以及任何差数抵消一个长的 正文 移动它。
TEST_CASE("incremental save: the fingerprint notices every persisted edit") {
    using persistence::debug_message_fingerprint;

    Message base;
    base.id   = MessageId{"fp-base"};
    base.role = Role::Assistant;
    base.text = "hello";
    const std::uint64_t h0 = debug_message_fingerprint(base);

    auto differs = [&](Message m, const char* what) {
        CHECK_MESSAGE(debug_message_fingerprint(m) != h0, what);
    };

    { Message m = base; m.id = MessageId{"fp-other"};      differs(std::move(m), "id"); }
    { Message m = base; m.role = Role::User;               differs(std::move(m), "role"); }
    { Message m = base; m.text = "hellp";                  differs(std::move(m), "text (one byte)"); }
    { Message m = base; m.text = "hello ";                 differs(std::move(m), "text (length)"); }
    { Message m = base; m.thinking = "t";                  differs(std::move(m), "thinking"); }
    { Message m = base; m.thinking_signature = "s";        differs(std::move(m), "thinking signature"); }
    { Message m = base; m.reasoning_encrypted = "e";       differs(std::move(m), "reasoning blob"); }
    { Message m = base; m.served_model = ModelId{"x"};     differs(std::move(m), "served model"); }
    { Message m = base; m.error = "boom";                  differs(std::move(m), "error"); }
    { Message m = base; m.is_compact_summary = true;       differs(std::move(m), "compact summary flag"); }
    { Message m = base; m.fork_note = true;                differs(std::move(m), "fork note"); }
    { Message m = base; m.checkpoint_id = CheckpointId{"c"}; differs(std::move(m), "checkpoint id"); }
    { Message m = base; m.timestamp += std::chrono::seconds{1};
                                                          differs(std::move(m), "timestamp"); }
    {
        Message m = base;
        Message::ThinkingBlock b; b.text = "tb";
        m.thinking_blocks.push_back(std::move(b));
        differs(std::move(m), "thinking block");
    }
    {
        Message m = base;
        ToolUse tc;
        tc.id = ToolCallId{"t1"}; tc.name = ToolName{"read"};
        tc.status = ToolUse::Done{{}, {}, "out"};
        m.tool_calls.push_back(std::move(tc));
        differs(std::move(m), "tool call added");
    }

    // 和解工具 调用 : 不变 调用 不同,但不会 status/output 每次都这样
    // 这是每张 -round 更改 末尾 保存 存在以获取。
    {
        Message running = base;
        ToolUse tc;
        tc.id = ToolCallId{"t1"}; tc.name = ToolName{"bash"};
        tc.status = ToolUse::Running{};
        running.tool_calls.push_back(tc);

        Message done = base;
        tc.status = ToolUse::Done{{}, {}, "finished"};
        done.tool_calls.push_back(tc);

        CHECK_MESSAGE(debug_message_fingerprint(running) != debug_message_fingerprint(done),
                      "a tool call settling must move the fingerprint");
    }

    // 一个 -byte 编辑时间 每个 减: 正文 包括32号线深处
    // -byte 车道环线和车道 末尾 之后,我们再谈吧。
    {
        const std::string body(4096, 'a');
        Message m = base; m.text = body;
        const std::uint64_t h = debug_message_fingerprint(m);
        int missed = 0;
        for (std::size_t i = 0; i < body.size(); ++i) {
            Message edited = base;
            edited.text = body;
            edited.text[i] = 'b';
            if (debug_message_fingerprint(edited) == h) ++missed;
        }
        CHECK_MESSAGE(missed == 0, "a one-byte edit was invisible to the fingerprint");
    }

    // 转换内容: 相同 字节 ,顺序不同。
    // 归并 被忽略的位置会在这里碰撞
    {
        Message a = base; a.text = std::string(32, 'x') + std::string(32, 'y');
        Message b = base; b.text = std::string(32, 'y') + std::string(32, 'x');
        CHECK_MESSAGE(debug_message_fingerprint(a) != debug_message_fingerprint(b),
                      "reordered content must not collide");
    }

    // 内容相同的指纹——否则每张 保存 将
    // 改写整个 线程.
    {
        Message a = base, b = base;
        CHECK(debug_message_fingerprint(a) == debug_message_fingerprint(b));
    }
}

TEST_CASE("thread log: truncate_to cuts both files and appends cleanly") {
    const ThreadId id{"inc-truncate-unit"};
    if (auto l = ThreadLog::open(id)) l->remove();
    auto log = ThreadLog::open(id);
    REQUIRE(log.has_value());
    for (std::size_t i = 0; i < 6; ++i) REQUIRE(log->append(make_msg(i)));

    REQUIRE(log->truncate_to(3));
    CHECK(log->size() == 3u);
    REQUIRE(log->append(make_msg(42)));

    auto again = ThreadLog::open(id);   // 内容 -read 编号 FILES ,没有 内存
    REQUIRE(again.has_value());
    const auto all = again->all();
    REQUIRE(all.size() == 4u);
    CHECK(all[2].id.value == "m2");
    CHECK(all[3].id.value == "m42");
    CHECK(log->truncate_to(100));       // 过尾号: no-op, 无错误
    CHECK(log->size() == 4u);
    again->remove();
}

namespace {
void retitle(const Thread& t, std::string title, std::int64_t at = 1'800'000'000) {
    persistence::rename_thread(::agentty::IoAccess::grant(), agtest::cap(), t.id,
                               std::move(title), at);
    persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());
}
} // 命名空间结束。

TEST_CASE("rename: retitles a log thread and leaves the rest alone") {
    Thread t = make_thread("inc-rename", 5);
    t.updated_at = std::chrono::system_clock::time_point{std::chrono::seconds{1'700'000'000}};
    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), t.id);
    save(t);
    const std::string log_before = slurp(log_path(t));

    retitle(t, "a better name");

    auto got = load(t);
    CHECK(got.title == "a better name");
    check_same(t, got);
    // 保持,所以行 不跳到顶端 选择面板.
    CHECK(got.updated_at == t.updated_at);
    CHECK_MESSAGE(slurp(log_path(t)) == log_before, "a rename never touches the log");

    // 电话 选择面板 书目走过的路也看到了 不只是路边 -id 加载.
    bool listed = false;
    for (const auto& meta : persistence::load_all_threads(::agentty::IoAccess::grant(), agtest::cap()))
        if (meta.id == t.id) { listed = true; CHECK(meta.title == "a better name"); }
    CHECK(listed);
    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), t.id);
}

TEST_CASE("rename: a save queued just before does not undo it") {
    Thread t = make_thread("inc-rename-queued", 4);
    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), t.id);
    save(t);

    // 队列 a 保存 与 旧 标题(何为) ^N / Alt
    // +arrow 用于 线程 中,然后立即重命名。
    // 保存 上到上到上; 无论是哪种方式, 重命名都必须获胜。
    t.messages.push_back(make_msg(4));
    persistence::save_thread(::agentty::IoAccess::grant(), agtest::cap(), t);
    retitle(t, "renamed while queued");

    auto got = load(t);
    CHECK(got.title == "renamed while queued");
    CHECK(got.messages.size() == 5u);

    // 写作员仍然知道日志:下一个 保存 是一个 末尾 附录。
    mark_head(t);
    t.messages.push_back(make_msg(5));
    t.title = "renamed while queued";
    save(t);
    CHECK_MESSAGE(head_marked(t), "history before the change was not rewritten");
    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), t.id);
}

TEST_CASE("rename: a stale copy saved later keeps the rename") {
    // 再来 agentty 曾经有这个 线程 从重命名前打开。
    // 其副本仍带有旧标题(没有重命名章),并且 保存 重命名
    // 后的新转弯。重命名必须存活下来,同时在两个位置上。
    // 末尾 路径和完整的重写。
    Thread t = make_thread("inc-rename-stale", 3);
    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), t.id);
    save(t);
    const Thread stale = t;   // 其它进程的副本

    retitle(t, "renamed here");

    Thread other = stale;
    other.messages.push_back(make_msg(3));
    save(other);   // 末尾 跳动 过期 复制
    auto got = load(t);
    CHECK(got.title == "renamed here");
    CHECK(got.messages.size() == 4u);

    // 完全重写自过期副本(作者从未见过这个id).
    persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());
    other.messages.push_back(make_msg(4));
    other.messages.front().text = "edited history forces a rewrite";
    save(other);
    got = load(t);
    CHECK(got.title == "renamed here");

    // 电话 选择面板 上市同意了 上市协议
    for (const auto& meta : persistence::load_all_threads(::agentty::IoAccess::grant(), agtest::cap()))
        if (meta.id == t.id) CHECK(meta.title == "renamed here");

    // 更新的名字仍然比老的更古老的名字要好
    retitle(t, "renamed again", 1'900'000'000);
    CHECK(load(t).title == "renamed again");
    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), t.id);
}

TEST_CASE("rename: a legacy thread is retitled and moved to the log") {
    Thread t = make_thread("inc-rename-legacy", 3);
    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), t.id);
    {
        auto j = persistence::thread_meta_to_json(t);
        nlohmann::json msgs = nlohmann::json::array();
        for (const auto& m : t.messages) msgs.push_back(persistence::message_to_json(m));
        j["messages"] = std::move(msgs);
        REQUIRE(persistence::write_json_atomic(::agentty::IoAccess::grant(),
            persistence::threads_dir() / (t.id.value + ".json"), j.dump(2)));
    }

    retitle(t, "legacy renamed");

    auto got = load(t);
    CHECK(got.title == "legacy renamed");
    check_same(t, got);
    CHECK(fs::exists(log_path(t)));
    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), t.id);
}

TEST_CASE("delete: a save queued just before does not bring the thread back") {
    Thread t = make_thread("inc-delete-queued", 4);
    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), t.id);

    // A 线程 这一过程没有 已保存 却去写作 整个 : 何为
    // ^N 队列为 a 线程 刚刚打开 从 选择面板. ^D ^D
    // 写作结束后,必须赢, 无论作者已经开始 保存 或无.
    persistence::save_thread(::agentty::IoAccess::grant(), agtest::cap(), t);
    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), t.id);
    persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());

    std::error_code ec;
    CHECK_FALSE(fs::exists(log_path(t), ec));
    CHECK_FALSE(fs::exists(persistence::threads_dir() / (t.id.value + ".meta.json"), ec));
    CHECK_FALSE(fs::exists(persistence::threads_dir() / (t.id.value + ".json"), ec));
    for (const auto& meta : persistence::load_all_threads(::agentty::IoAccess::grant(), agtest::cap()))
        CHECK_MESSAGE(meta.id != t.id, "no ghost row in the picker");

    // 一会儿 保存 的代码是新的 线程 : 写整篇,
    // 不是 末尾 附加到已删除的日志中。
    Thread again = make_thread("inc-delete-queued", 2);
    save(again);
    auto got = load(again);
    CHECK(got.messages.size() == 2u);
    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), t.id);
}

TEST_CASE("rename: an unknown id writes nothing") {
    const Thread t = make_thread("inc-rename-missing", 0);
    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), t.id);
    retitle(t, "ghost");
    std::error_code ec;
    CHECK_FALSE(fs::exists(persistence::threads_dir() / (t.id.value + ".meta.json"), ec));
    CHECK_FALSE(fs::exists(log_path(t), ec));
}

TEST_CASE("clean_title: one safe line, at most kTitleMaxBytes") {
    using persistence::clean_title;
    CHECK(clean_title("  two\nlines\there  ") == "two lines here");
    CHECK(clean_title("\x1b[31mred") == "[31mred");      // ESC 已丢弃
    CHECK(clean_title("a\xc2\x9b" "b") == "ab");          // C1 CSI 已丢弃
    CHECK(clean_title("   ").empty());
    // 切换代码 -point 边界: 59 ASCII 字节 3,然后是3,然后是一个3 -byte
    // 以时间结束的破折号 字节 62.
    const std::string long_in = std::string(59, 'x') + "\xe2\x80\x94";
    const std::string out = clean_title(long_in);
    CHECK(out == std::string(59, 'x'));
    CHECK(out.size() <= persistence::kTitleMaxBytes);
    // 合适的时候就保持整齐
    CHECK(clean_title("caf\xc3\xa9") == "caf\xc3\xa9");
}
