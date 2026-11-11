// thread_log_test - 附录 -only 信件日志及其折叠索引。
//
// 主人的财产是 ROUND-TRIP IDENTITY:
// a 线程 写入日志并读回必须等于 线程 其它
// 所有的东西都是为了让失败模式清晰而非发现:
//
//   * 指数是CACHE——必须删除、截断或腐蚀
//     改变一切,只有开放的时间,
//   * 撕裂的附着物会花掉部分的线,而别的东西,
//   *附件为O(1),以线程大小表示,这是默默无声的财产。
//     如果晚点有人会退步 "simplifies" 作家,
//   * 窗口读取的回执与读取的完整完全相同。
//
// 这些都与真实的编码c(persistence::message_to_json
// / message_from_json)相对立,因此它们也钉住日志和整个
// -document格式不能飘移.

#include "agtest.hpp"

#include <agentty/io/thread_log.hpp>
#include <agentty/io/persistence.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace agentty;

namespace {

fs::path scratch_dir() {
    auto p = fs::temp_directory_path() / "agentty_thread_log_test";
    std::error_code ec;
    fs::create_directories(p, ec);
    return p;
}

// 每次测试都有一个新鲜的日志路径,所以病例不能互相污染.
fs::path fresh_log(std::string_view name) {
    auto p = scratch_dir() / (std::string{name} + ".jsonl");
    std::error_code ec;
    fs::remove(p, ec);
    auto idx = p; idx.replace_extension(".ofs");
    fs::remove(idx, ec);
    return p;
}

// 字节 文本管道,因为工具输出确实携带:a UTF -8个序列,
// 字符必须是 JSON-escaped ,并且——关键是——
// 嵌入式 NEWLINE ,这是一件事 一行
// -delimited 格式可能会令人怀疑地错误。
Message make_message(int i, bool nasty = false) {
    Message m;
    m.role = (i % 2 == 0) ? Role::User : Role::Assistant;
    m.id   = MessageId{"m" + std::to_string(i)};
    m.text = "message " + std::to_string(i);
    if (nasty) {
        m.text += "\nsecond line\twith tab \"quoted\" \\ backslash "
                  "\xE2\x80\x94 em dash \xF0\x9F\x9A\x80 rocket";
    }
    return m;
}

std::vector<Message> make_messages(std::size_t n, bool nasty = false) {
    std::vector<Message> v;
    v.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
        v.push_back(make_message(static_cast<int>(i), nasty));
    return v;
}

// 比较日志负责携带的字段.
bool same(const Message& a, const Message& b) {
    return a.id.value == b.id.value
        && a.role      == b.role
        && a.text      == b.text
        && a.thinking  == b.thinking
        && a.tool_calls.size() == b.tool_calls.size()
        && a.images.size()     == b.images.size();
}

bool same_all(const std::vector<Message>& a, const std::vector<Message>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (!same(a[i], b[i])) return false;
    return true;
}

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string{std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>()};
}

} // 命名空间结束。

TEST_CASE("thread log: round-trips messages verbatim") {
    const auto path = fresh_log("roundtrip");
    const auto sent = make_messages(50, /*恶心的*/true);

    {
        auto log = ThreadLog::open_path(path);
        REQUIRE(log.has_value());
        CHECK(log->empty());
        for (const auto& m : sent) CHECK(log->append(m));
        CHECK(log->size() == sent.size());
    }

    auto reopened = ThreadLog::open_path(path);
    REQUIRE(reopened.has_value());
    CHECK(reopened->size() == sent.size());
    CHECK_MESSAGE(same_all(reopened->all(), sent),
                  "every message must survive a write/read cycle verbatim, "
                  "embedded newlines and all");
}

TEST_CASE("thread log: an embedded newline does not split a message") {
    // 分界线的明显方法 -delimited 格式化,
    // 请输入 message_to_json 逃脱了它,但这种保证值得考验而不是信任。
    const auto path = fresh_log("newlines");
    Message m;
    m.role = Role::Assistant;
    m.id   = MessageId{"nl"};
    m.text = "line one\nline two\nline three";

    auto log = ThreadLog::open_path(path);
    REQUIRE(log.has_value());
    CHECK(log->append(m));

    CHECK_MESSAGE(log->size() == 1u,
                  "a message containing newlines is still ONE log entry");
    const auto back = log->all();
    REQUIRE(back.size() == 1u);
    CHECK(back[0].text == m.text);

    // 并且文件确实有一条线。
    const std::string raw = read_file(path);
    CHECK(std::count(raw.begin(), raw.end(), '\n') == 1);
}

TEST_CASE("thread log: windowed reads match the full read") {
    const auto path = fresh_log("window");
    const auto sent = make_messages(200);
    {
        auto log = ThreadLog::open_path(path);
        REQUIRE(log.has_value());
        CHECK(log->rewrite(sent));
    }

    auto log = ThreadLog::open_path(path);
    REQUIRE(log.has_value());
    const auto all = log->all();
    REQUIRE(all.size() == sent.size());

    // 电话 线程-switch 形状:最后一个屏幕。
    const auto tail = log->range(log->size() - 60, log->size());
    REQUIRE(tail.size() == 60u);
    for (std::size_t i = 0; i < tail.size(); ++i)
        CHECK(same(tail[i], all[all.size() - 60 + i]));

    // 中间的窗户和堕落的箱子
    const auto mid = log->range(40, 60);
    REQUIRE(mid.size() == 20u);
    CHECK(same(mid.front(), all[40]));
    CHECK(same(mid.back(),  all[59]));

    CHECK(log->range(10, 10).empty());          // 空窗口
    CHECK(log->range(50, 10).empty());          // 倒转
    CHECK(log->range(9999, 10000).empty());     // 过尾端
    CHECK(log->range(190, 9999).size() == 10u); // 夹在日志上
}

TEST_CASE("thread log: the index is a cache, not truth") {
    const auto path = fresh_log("cache");
    auto idx = path; idx.replace_extension(".ofs");
    const auto sent = make_messages(120, /*恶心的*/true);
    {
        auto log = ThreadLog::open_path(path);
        REQUIRE(log.has_value());
        CHECK(log->rewrite(sent));
    }
    CHECK(fs::exists(idx));

    // 8 字节 每一个消息, 和没有其他的。
    CHECK(fs::file_size(idx) == sent.size() * 8u);

    auto expect_intact = [&](const char* what) {
        auto log = ThreadLog::open_path(path);
        REQUIRE(log.has_value());
        CHECK_MESSAGE(log->size() == sent.size(), what);
        CHECK_MESSAGE(same_all(log->all(), sent), what);
    };

    // 1. 完全删除。
    std::error_code ec;
    fs::remove(idx, ec);
    expect_intact("a deleted index must be rebuilt, losing nothing");
    CHECK_MESSAGE(fs::exists(idx), "and rewritten for next time");

    // 2. 断断(第-between-appends号坠机案)。
    {
        std::ofstream out(idx, std::ios::binary | std::ios::trunc);
        out << std::string(40, '\0');    // 5个股,全部零
    }
    expect_intact("a truncated index must be rebuilt");

    // 3. 填充的垃圾甚至没有抵消-shaped.
    {
        std::ofstream out(idx, std::ios::binary | std::ios::trunc);
        out << "this is not an index at all";
    }
    expect_intact("a corrupt index must be rebuilt");

    // 4. 良好偏差 -formed 但要在日志外指点
    {
        std::string buf(sent.size() * 8u, '\0');
        for (std::size_t i = 0; i < sent.size(); ++i) {
            const std::uint64_t bogus = 1ull << 40;
            for (int b = 0; b < 8; ++b)
                buf[i * 8 + static_cast<std::size_t>(b)] =
                    static_cast<char>((bogus >> (b * 8)) & 0xFF);
        }
        std::ofstream out(idx, std::ios::binary | std::ios::trunc);
        out.write(buf.data(), static_cast<std::streamsize>(buf.size()));
    }
    expect_intact("an out-of-range index must be rebuilt");
}

TEST_CASE("thread log: a torn append costs only the partial line") {
    const auto path = fresh_log("torn");
    const auto sent = make_messages(80);
    {
        auto log = ThreadLog::open_path(path);
        REQUIRE(log.has_value());
        CHECK(log->rewrite(sent));
    }

    // 模拟崩溃中点 -append :将文件切入其
    // 最后一行,并放下索引,以便读者自己应付.
    std::string raw = read_file(path);
    const std::size_t last_nl = raw.find_last_of('\n', raw.size() - 2);
    REQUIRE(last_nl != std::string::npos);
    raw.resize(last_nl + 1 + 20);           // 20 字节 输入最后一行
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(raw.data(), static_cast<std::streamsize>(raw.size()));
    }
    std::error_code ec;
    auto idx = path; idx.replace_extension(".ofs");
    fs::remove(idx, ec);

    auto log = ThreadLog::open_path(path);
    REQUIRE(log.has_value());
    const auto back = log->all();
    CHECK_MESSAGE(back.size() == sent.size() - 1,
                  "every complete message survives; only the torn one is lost");
    for (std::size_t i = 0; i < back.size(); ++i) CHECK(same(back[i], sent[i]));

    // 并且该日志仍然可用: 泪后附着.
    CHECK(log->append(make_message(9999)));
    auto after = ThreadLog::open_path(path);
    REQUIRE(after.has_value());
    CHECK(after->all().size() == sent.size());
}

TEST_CASE("thread log: append is O(1) in thread size") {
    // 如果作家永远沉沦 则会悄悄退缩
    // "simplified" 重写文件。
    // BYTES 写,不是时间,所以它决定了任何机器。
    const auto path = fresh_log("append_cost");
    auto idx = path; idx.replace_extension(".ofs");
    const auto sent = make_messages(500);
    {
        auto log = ThreadLog::open_path(path);
        REQUIRE(log.has_value());
        CHECK(log->rewrite(sent));
    }

    auto log = ThreadLog::open_path(path);
    REQUIRE(log.has_value());
    const auto before     = fs::file_size(path);
    const auto idx_before = fs::file_size(idx);

    Message extra = make_message(1000);
    CHECK(log->append(extra));

    const auto after = fs::file_size(path);
    const auto grew  = after - before;
    CHECK_MESSAGE(grew < 1024u,
                  "appending one message must add roughly one line, "
                  "not rewrite the log");
    // 电话 INDEX 一定是O(1),全部重写只有8个
    // 字节 每条信息——500,但400 KB 每转50
    // 千条信息,然后不再限制。 字节 整个问题。
    CHECK_MESSAGE(fs::file_size(idx) - idx_before == 8u,
                  "appending one message must add exactly one 8-byte offset");
    CHECK(log->size() == sent.size() + 1);

    auto reopened = ThreadLog::open_path(path);
    REQUIRE(reopened.has_value());
    CHECK(reopened->size() == sent.size() + 1);
    CHECK(same(reopened->all().back(), extra));
}

TEST_CASE("thread log: many appends stay O(1) each") {
    // 长对话的形状实际上是一个:每转弯一个附件,数百次。
    // 每个附件必须精确地加一条线,准确的加一八 -byte
    // 时间长短, 线程 已经是——如果两个文件中任何
    // 一个都重写到每个转弯,则整个工作都在四进制中 线
    // 程 长度,在短测试中是隐形的,在长测试中是致命的。
    // (下面的伴考直接观察重写).
    const auto path = fresh_log("append_scaling");
    auto idx = path; idx.replace_extension(".ofs");
    auto log = ThreadLog::open_path(path);
    REQUIRE(log.has_value());
    CHECK(log->rewrite(make_messages(200)));

    std::error_code ec;
    const auto before_size = fs::file_size(idx, ec);
    for (int i = 0; i < 50; ++i) CHECK(log->append(make_message(1000 + i)));
    CHECK_MESSAGE(fs::file_size(idx, ec) == before_size + 50u * 8u,
                  "50 appends must add exactly 50 offsets");

    for (int i = 0; i < 400; ++i) CHECK(log->append(make_message(2000 + i)));

    CHECK(fs::file_size(idx, ec) == log->size() * 8u);
    CHECK(log->size() == 650u);

    auto reopened = ThreadLog::open_path(path);
    REQUIRE(reopened.has_value());
    CHECK_MESSAGE(reopened->size() == 650u,
                  "and every appended message must still be there");
    CHECK(same(reopened->all().back(), make_message(2399)));
}

TEST_CASE("thread log: appending does not rewrite the index") {
    // 直接观察的O(1)保证。 write_json_atomic
    // 出版人: RENAME ,所以一个整体 -index 写入
    // 替换文件;附件修改了文件的位置。 fs::equivalent
    // 对抗一个硬 链接 事先没有时间也没有计数器 告诉了他们
    const auto path = fresh_log("append_inplace");
    auto idx = path; idx.replace_extension(".ofs");
    auto log = ThreadLog::open_path(path);
    REQUIRE(log.has_value());
    CHECK(log->rewrite(make_messages(300)));
    REQUIRE(fs::exists(idx));

    // 硬链接 将 当前 文件对象标注在 XX00120 文件中。
    // 如果附件重写索引, `idx` 将成为一个不同的对象和等效断裂。
    const auto pin = idx;
    auto pinned = scratch_dir() / "append_inplace.pin";
    std::error_code ec;
    fs::remove(pinned, ec);
    fs::create_hard_link(pin, pinned, ec);
    REQUIRE_MESSAGE(!ec, "test needs hard links on the scratch filesystem");

    CHECK(log->append(make_message(9999)));

    const bool same_file = fs::equivalent(pin, pinned, ec) && !ec;
    CHECK_MESSAGE(same_file,
                  "append must write the index IN PLACE — a rewrite is "
                  "O(messages) per turn and unbounded in thread length");

    fs::remove(pinned, ec);
}

TEST_CASE("thread log: rewrite replaces history atomically") {
    const auto path = fresh_log("rewrite");
    {
        auto log = ThreadLog::open_path(path);
        REQUIRE(log.has_value());
        CHECK(log->rewrite(make_messages(100)));
        CHECK(log->size() == 100u);
    }
    // 压缩 / 编辑 / 叉形状: 消息比以前少。
    auto log = ThreadLog::open_path(path);
    REQUIRE(log.has_value());
    const auto shorter = make_messages(10);
    CHECK(log->rewrite(shorter));
    CHECK(log->size() == 10u);

    auto reopened = ThreadLog::open_path(path);
    REQUIRE(reopened.has_value());
    CHECK_MESSAGE(reopened->size() == 10u,
                  "the index must shrink with the log, not keep stale offsets");
    CHECK(same_all(reopened->all(), shorter));
}

TEST_CASE("thread log: an empty or absent log is a valid empty thread") {
    const auto path = fresh_log("empty");
    auto log = ThreadLog::open_path(path);
    REQUIRE(log.has_value());
    CHECK(log->empty());
    CHECK(log->size() == 0u);
    CHECK(log->all().empty());
    CHECK(log->range(0, 100).empty());

    // 中断的创建留下的零-byte文件是同一回事.
    { std::ofstream out(path, std::ios::binary | std::ios::trunc); }
    auto again = ThreadLog::open_path(path);
    REQUIRE(again.has_value());
    CHECK(again->empty());
    CHECK(again->append(make_message(0)));
    CHECK(again->size() == 1u);
}

TEST_CASE("thread log: an in-flight tool call reloads as interrupted, not as itself") {
    // A 保存 着火了 每个 转动,包括当一个工具仍在运行时。
    // 读者故意胁迫 pending/running 调用 失败{
    // "interrupted" ?):一个在中途死亡的过程; -tool
    // 不可重装 调用 永远等待。
    //
    // 圆的 -trip 身份 FALSE 以未定之物发誓, 调用 故意的 故意的 我...
    // 故意的 {\cHFFFFFF}{\3cH111111}{\4cH111111}And
    // on sidely. save_thread_sync 校验门比较了它
    // 写的东西 和读回的内容,它用来比较 无条件的工具输出,所以每一个 保存 报告飞行中
    // 工具 "log verification FAILED" 并拒绝将遗留文件退役。
    // 现场观看了2400 -message 线程 ,在一阵阵子里,
    // 工具完成的那一刻, 已经痊愈了。
    //
    // 这个定点转换,所以闸门的免责保留了合理
    // 性:如果读者开始保留待处理状态,这个测试失败,
    // 免责可以被移除.
    const auto path = fresh_log("inflight");

    Message m;
    m.id   = MessageId{"m-inflight"};
    m.role = Role::Assistant;
    m.text = "running a tool";
    ToolUse tc;
    tc.id     = ToolCallId{"call-1"};
    tc.name   = ToolName{"shell"};
    tc.status = ToolUse::Running{};
    m.tool_calls.push_back(tc);

    auto log = ThreadLog::open_path(path);
    REQUIRE(log.has_value());
    REQUIRE(log->append(m));

    const auto back = log->all();
    REQUIRE(back.size() == 1u);
    REQUIRE(back[0].tool_calls.size() == 1u);

    // 内 内存 无人解决 调用 没有输出;在磁盘上它有一个。
    CHECK(m.tool_calls[0].output().empty());
    CHECK_FALSE(back[0].tool_calls[0].output().empty());
    // 并且它会回到终端, 所以重装永远不会等待它。
    CHECK(back[0].tool_calls[0].is_terminal());

    // 一切的形态 执行 答应你还是圆的 -trips 完全正确,我们没有
    CHECK(back[0].id.value == m.id.value);
    CHECK(back[0].text == m.text);
    CHECK(back[0].tool_calls[0].id.value == tc.id.value);
    CHECK(back[0].tool_calls[0].name.value == tc.name.value);

    // SETTLED 调用保持其产出的逐字记录——豁免的范围
    // 是针对未解决的案件,决不能掩盖实际损失。
    Message done;
    done.id   = MessageId{"m-done"};
    done.role = Role::Assistant;
    ToolUse fin;
    fin.id     = ToolCallId{"call-2"};
    fin.name   = ToolName{"shell"};
    ToolUse::Done d;
    d.output   = "real output";
    fin.status = d;
    done.tool_calls.push_back(fin);
    REQUIRE(log->append(done));

    const auto back2 = log->all();
    REQUIRE(back2.size() == 2u);
    CHECK(back2[1].tool_calls[0].output() == "real output");
}

TEST_CASE("thread log: metadata round-trips beside the messages") {
    // A 线程 不仅仅是它的信息。 头文件 谓 可变
    // 日志为附件 -only 因为它住在一辆侧车里 每一片
    // 地都要生存下来 或者重新装满 线程 悄悄地失去它的头衔,
    // 叉子来源,或者保持其特征的收缩记录 传输 载荷
    // 细小的。
    const auto path = fresh_log("meta");
    auto meta = path; meta.replace_extension(".meta.json");
    std::error_code ec; fs::remove(meta, ec);

    Thread t;
    t.id          = ThreadId{"meta_thread"};
    t.title       = "a thread with a title";
    t.forked_from = "some_parent_id";
    t.rag_mode_override = store::RagMode::FirstTurnOnly;
    t.messages    = make_messages(30);
    Thread::CompactionRecord rec;
    rec.up_to_index = 10;
    rec.summary     = "summary of the first ten";
    t.compactions.push_back(rec);

    {
        auto log = ThreadLog::open_path(path);
        REQUIRE(log.has_value());
        CHECK(log->store_thread(t));
    }

    auto log = ThreadLog::open_path(path);
    REQUIRE(log.has_value());
    const Thread back = log->load_thread();

    CHECK(back.id.value     == t.id.value);
    CHECK(back.title        == t.title);
    CHECK(back.forked_from  == t.forked_from);
    REQUIRE(back.rag_mode_override.has_value());
    CHECK(*back.rag_mode_override == store::RagMode::FirstTurnOnly);
    REQUIRE(back.compactions.size() == 1u);
    CHECK(back.compactions[0].up_to_index == 10u);
    CHECK(back.compactions[0].summary     == rec.summary);
    CHECK(same_all(back.messages, t.messages));

    // meta() 一个人不得 拖动记录稿——这就是
    // 拆分文件的全部要点.
    CHECK_MESSAGE(log->meta().messages.empty(),
                  "metadata must not carry messages");
    CHECK(log->meta().title == t.title);
}

TEST_CASE("thread log: a compaction past the end of history is dropped") {
    // 只能通过一个 保存 中途中断 -compaction
    // 记录将使得 传输 概括一个已不存在的前缀,
    // 因此装入器必须绑定它—— 在日志格式中读取元
    // 数据 之前 信息计数是已知的,正因为如此,
    // 夹子是单独的一步,而不是一部分 解析.
    const auto path = fresh_log("stale_compaction");
    Thread t;
    t.id       = ThreadId{"stale"};
    t.messages = make_messages(5);
    Thread::CompactionRecord good; good.up_to_index = 3;
    Thread::CompactionRecord bad;  bad.up_to_index  = 999;
    t.compactions = {good, bad};

    auto log = ThreadLog::open_path(path);
    REQUIRE(log.has_value());
    CHECK(log->store_thread(t));

    auto reopened = ThreadLog::open_path(path);
    REQUIRE(reopened.has_value());
    const Thread back = reopened->load_thread();
    REQUIRE(back.compactions.size() == 1u);
    CHECK(back.compactions[0].up_to_index == 3u);
}

TEST_CASE("thread log: remove() clears every file of the thread") {
    // 日志格式为三个文件。 删除 a 线程 全部都得拿走,
    // 或者说 线程 重新出现在下一个目录行走中。
    const auto path = fresh_log("removal");
    auto idx  = path; idx.replace_extension(".ofs");
    auto meta = path; meta.replace_extension(".meta.json");

    Thread t;
    t.id       = ThreadId{"removal"};
    t.title    = "doomed";
    t.messages = make_messages(5);

    auto log = ThreadLog::open_path(path);
    REQUIRE(log.has_value());
    CHECK(log->store_thread(t));
    REQUIRE(fs::exists(path));
    REQUIRE(fs::exists(idx));
    REQUIRE(fs::exists(meta));

    log->remove();
    CHECK_FALSE(fs::exists(path));
    CHECK_FALSE(fs::exists(idx));
    CHECK_FALSE(fs::exists(meta));
    CHECK(log->empty());
    CHECK_FALSE(log->exists());
}
