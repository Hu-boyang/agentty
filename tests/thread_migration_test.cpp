// thread_migration_test — a 保存 移动 a
// 线程 永远也不可能失去它
//
// 这是触及用户数据的承诺:保存一个 线程 现在写日志和 DELETES
// 遗产 <id>.JSON. A 保存 运行每个转弯,
// 所以这里的bug是无法恢复的——它删除的文件是唯一的另外一个副本.
//
// 安全论证是验证 -before-delete :写对子,
// 从磁盘上读回,比较,然后只删除。 JSON。
// 这些测试将其中一半的试验:
//
//   * 幸福之路确实迁移(和 线程 幸存下来,
//   * 每一个失败路径都使遗留文件完全留在原处。

#include "agtest.hpp"

#include <agentty/io/persistence.hpp>
#include <agentty/io/thread_log.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace agentty;

namespace {

Thread make_thread(std::string id, std::size_t n_messages) {
    Thread t;
    t.id    = ThreadId{std::move(id)};
    t.title = "migration subject";
    for (std::size_t i = 0; i < n_messages; ++i) {
        Message m;
        m.role = (i % 2 == 0) ? Role::User : Role::Assistant;
        m.id   = MessageId{"m" + std::to_string(i)};
        m.text = "message " + std::to_string(i)
               + " with \"quotes\", a \\ backslash, \na newline, "
                 "and \xE2\x80\x94 an em dash";
        t.messages.push_back(std::move(m));
    }
    return t;
}

fs::path legacy_path(const Thread& t) {
    return persistence::threads_dir() / (t.id.value + ".json");
}
fs::path log_path(const Thread& t) {
    return persistence::threads_dir() / (t.id.value + ".jsonl");
}

// Write a 线程 输入 旧 格式,因为它将在更改运行
// 之前存在于磁盘上。
void seed_legacy(const Thread& t) {
    auto j = persistence::thread_meta_to_json(t);
    nlohmann::json msgs = nlohmann::json::array();
    for (const auto& m : t.messages) msgs.push_back(persistence::message_to_json(m));
    j["messages"] = std::move(msgs);
    persistence::write_json_atomic(::agentty::IoAccess::grant(), legacy_path(t), j.dump(2));
}

void wipe(const Thread& t) {
    std::error_code ec;
    fs::remove(legacy_path(t), ec);
    if (auto log = ThreadLog::open(t.id)) log->remove();
}

} // 命名空间结束。

TEST_CASE("migration: saving a legacy thread moves it to the log") {
    auto t = make_thread("mig_happy", 25);
    wipe(t);
    seed_legacy(t);
    REQUIRE(fs::exists(legacy_path(t)));
    REQUIRE_FALSE(fs::exists(log_path(t)));

    persistence::save_thread(::agentty::IoAccess::grant(), agtest::cap(), t);
    persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());

    CHECK_MESSAGE(fs::exists(log_path(t)),
                  "the save must write the log");
    CHECK_MESSAGE(!fs::exists(legacy_path(t)),
                  "and retire the legacy document once it verifies");

    // 电话 线程 一定要从正常的商店里完整地回来 接口边界.
    auto back = persistence::load_thread_by_id(::agentty::IoAccess::grant(), t.id);
    REQUIRE(back.has_value());
    CHECK(back->id.value  == t.id.value);
    CHECK(back->title     == t.title);
    REQUIRE(back->messages.size() == t.messages.size());
    for (std::size_t i = 0; i < t.messages.size(); ++i) {
        CHECK(back->messages[i].id.value == t.messages[i].id.value);
        CHECK(back->messages[i].text     == t.messages[i].text);
        CHECK(back->messages[i].role     == t.messages[i].role);
    }
    wipe(t);
}

TEST_CASE("migration: a thread with no legacy file just writes the log") {
    // 新的 -thread 案子,没有退休,没有损失
    auto t = make_thread("mig_fresh", 5);
    wipe(t);

    persistence::save_thread(::agentty::IoAccess::grant(), agtest::cap(), t);
    persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());

    CHECK(fs::exists(log_path(t)));
    CHECK_FALSE(fs::exists(legacy_path(t)));
    auto back = persistence::load_thread_by_id(::agentty::IoAccess::grant(), t.id);
    REQUIRE(back.has_value());
    CHECK(back->messages.size() == t.messages.size());
    wipe(t);
}

TEST_CASE("migration: saving again is idempotent") {
    // 转弯 保存。第二个 保存 没有任何遗留文件可以删除,
    // 并且不得腐败或复制任何东西。
    auto t = make_thread("mig_twice", 10);
    wipe(t);
    seed_legacy(t);

    persistence::save_thread(::agentty::IoAccess::grant(), agtest::cap(), t);
    persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());
    REQUIRE(fs::exists(log_path(t)));

    t.messages.push_back([] {
        Message m;
        m.role = Role::User;
        m.id   = MessageId{"m_extra"};
        m.text = "one more turn";
        return m;
    }());
    persistence::save_thread(::agentty::IoAccess::grant(), agtest::cap(), t);
    persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());

    auto back = persistence::load_thread_by_id(::agentty::IoAccess::grant(), t.id);
    REQUIRE(back.has_value());
    CHECK_MESSAGE(back->messages.size() == 11u,
                  "the second save must replace history, not append to it");
    CHECK(back->messages.back().text == "one more turn");
    wipe(t);
}

TEST_CASE("migration: Smart Mode routing cards are not persisted") {
    // 他们就是景色 -only 遥测,不包括在 传输 和从磁盘。
    // 核查步骤与 PERSISTABLE 信件,
    // 所以如果过滤器错误 保存 核查失败,
    // 永远不迁移——这无论怎样都给预期行为定下了界限。
    auto t = make_thread("mig_smart", 6);
    Message card;
    card.role          = Role::Assistant;
    card.id            = MessageId{"routing"};
    card.text          = "routed to a cheaper model";
    card.smart_routing = true;
    t.messages.insert(t.messages.begin() + 3, card);
    wipe(t);

    persistence::save_thread(::agentty::IoAccess::grant(), agtest::cap(), t);
    persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());

    auto back = persistence::load_thread_by_id(::agentty::IoAccess::grant(), t.id);
    REQUIRE(back.has_value());
    CHECK_MESSAGE(back->messages.size() == 6u,
                  "the routing card must not be written to disk");
    for (const auto& m : back->messages)
        CHECK(m.id.value != "routing");
    // 并且它仍然迁移, i.e 核查工作通过。
    CHECK(fs::exists(log_path(t)));
    CHECK_FALSE(fs::exists(legacy_path(t)));
    wipe(t);
}

TEST_CASE("migration: an unwritable log keeps the legacy document") {
    // 失败很重要。如果日志无法写入,则 保存 必须 倒回去,把旧文件留下,
    // 不留任何痕迹——失去转弯是不好的,失去转弯就是不好的,失去了转弯就是坏的,
    // 失去掉的就是坏的. 线程 无法接受
    //
    // 坐在日志文件所属位置的目录会使每个写入路径的文字失败,
    // 而不需要 权限 以根或根的形式进行不同行为
    // 的游戏 Windows.
    auto t = make_thread("mig_blocked", 8);
    wipe(t);
    seed_legacy(t);

    std::error_code ec;
    fs::create_directories(log_path(t), ec);
    REQUIRE(fs::is_directory(log_path(t)));

    persistence::save_thread(::agentty::IoAccess::grant(), agtest::cap(), t);
    persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());

    CHECK_MESSAGE(fs::exists(legacy_path(t)),
                  "a failed log write must leave the legacy document alone");

    // 还有 线程 仍然 加载 ,通过遗产路径。
    auto back = persistence::load_thread_by_id(::agentty::IoAccess::grant(), t.id);
    REQUIRE(back.has_value());
    CHECK(back->messages.size() == t.messages.size());

    fs::remove_all(log_path(t), ec);
    wipe(t);
}

TEST_CASE("migration: deleting a migrated thread removes every file") {
    auto t = make_thread("mig_delete", 4);
    wipe(t);
    persistence::save_thread(::agentty::IoAccess::grant(), agtest::cap(), t);
    persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());
    REQUIRE(fs::exists(log_path(t)));

    persistence::delete_thread(::agentty::IoAccess::grant(), agtest::cap(), t.id);

    CHECK_FALSE(fs::exists(log_path(t)));
    CHECK_FALSE(fs::exists(persistence::threads_dir() / (t.id.value + ".ofs")));
    CHECK_FALSE(fs::exists(persistence::threads_dir() / (t.id.value + ".meta.json")));
    CHECK_FALSE(persistence::load_thread_by_id(::agentty::IoAccess::grant(), t.id).has_value());
}

TEST_CASE("migration: the picker lists a migrated thread exactly once") {
    // load_all_threads 走路 * 走路时,你应该走 JSON
    // 迁移 线程 仅包含 <id>.meta.json 中间 -migration
    // 两者都可以存在,因此,这就是一个 线程 要么从名单中消失 要么出现两次
    auto a = make_thread("mig_listed", 3);
    wipe(a);
    seed_legacy(a);
    persistence::save_thread(::agentty::IoAccess::grant(), agtest::cap(), a);
    persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());

    const auto all = persistence::load_all_threads(::agentty::IoAccess::grant(), agtest::cap());
    std::size_t seen = 0;
    for (const auto& t : all) if (t.id.value == a.id.value) ++seen;
    CHECK_MESSAGE(seen == 1u, "a migrated thread must be listed exactly once");

    // 和它的真名,不是一根茎 -derived 艺术
    for (const auto& t : all)
        if (t.id.value == a.id.value) CHECK(t.title == a.title);
    wipe(a);
}
