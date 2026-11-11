// blob_gc_test ——收回. 载荷 无 线程 参考文献。
//
// 电话 GC 删除持有用户图像和工具输出的文件,
// 因此这里的测试大多是必须何时进行 不 删除:
//
//   * 引用的blob 任意 线程 包括一个共享的
//     介于两个之间 线程 (内容) -addressing 使正常情况,
//     而不是边缘情况,
//   * 每一个参考钥匙都值得尊重,包括那些作者的
//     生成为 `<field>_blob`,
//   * 一个无法读取的线程文件中止了整个扫描, 因为未知
//     不得假定没有参考文献,
//   * dry_run没有任何触摸。

#include "agtest.hpp"

#include <agentty/io/blob_gc.hpp>
#include <agentty/io/blob_store.hpp>
#include <agentty/io/persistence.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h>   // 根读任何内容,请跳过这里

namespace fs = std::filesystem;
using namespace agentty;

namespace {

// 每个测试一个私家的线程 dir,所以病例无法看到对方的blobs.
fs::path make_dir(std::string_view name) {
    auto p = fs::temp_directory_path() / "agentty_blob_gc" / name;
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p / "blobs", ec);
    return p;
}

// 直接放一个blob,绕过商店自己的路径分辨率,
// 所以测试控制它降落在哪个目录中.
std::string put_in(const fs::path& dir, const std::string& bytes) {
    const auto name = blobs::name_for(bytes);
    std::ofstream out(dir / "blobs" / name, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return name;
}

void write_file(const fs::path& p, const std::string& text) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << text;
}

std::size_t count_blobs(const fs::path& dir) {
    std::size_t n = 0;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir / "blobs", ec))
        if (e.is_regular_file(ec)) ++n;
    return n;
}

bool blob_exists(const fs::path& dir, const std::string& name) {
    std::error_code ec;
    return fs::exists(dir / "blobs" / name, ec);
}

} // 命名空间结束。

TEST_CASE("blob gc: deletes orphans and keeps everything referenced") {
    const auto dir = make_dir("basic");
    const auto kept    = put_in(dir, std::string(100, 'k'));
    const auto orphan1 = put_in(dir, std::string(200, 'a'));
    const auto orphan2 = put_in(dir, std::string(300, 'b'));
    REQUIRE(count_blobs(dir) == 3u);

    write_file(dir / "t1.json", nlohmann::json{
        {"id", "t1"},
        {"messages", nlohmann::json::array({
            nlohmann::json{{"role", "user"},
                           {"images", nlohmann::json::array({
                               nlohmann::json{{"blob", kept}}})}}})}
    }.dump());

    const auto st = blobs::collect_in(dir);
    CHECK(st.ran);
    CHECK(st.scanned_threads == 1u);
    CHECK(st.total_blobs == 3u);
    CHECK(st.referenced  == 1u);
    CHECK(st.deleted     == 2u);
    CHECK(st.bytes_freed == 500u);

    CHECK_MESSAGE(blob_exists(dir, kept), "a referenced blob must survive");
    CHECK_FALSE(blob_exists(dir, orphan1));
    CHECK_FALSE(blob_exists(dir, orphan2));
}

TEST_CASE("blob gc: a blob shared by two threads survives deleting one") {
    // 案件数量 -thread 重算错误。 内容 -addressing
    // 表示两个中相同的截图 线程 谓 一个 文件;
    // 删除 线程 不得将图像空白于 线程 B.
    const auto dir = make_dir("shared");
    const auto shared = put_in(dir, std::string(400, 's'));

    auto doc = [&](const char* id) {
        return nlohmann::json{
            {"id", id},
            {"messages", nlohmann::json::array({
                nlohmann::json{{"role", "user"},
                               {"images", nlohmann::json::array({
                                   nlohmann::json{{"blob", shared}}})}}})}
        }.dump();
    };
    write_file(dir / "a.json", doc("a"));
    write_file(dir / "b.json", doc("b"));

    // 线程 A 离开; B 仍然引用 blob。
    std::error_code ec;
    fs::remove(dir / "a.json", ec);

    const auto st = blobs::collect_in(dir);
    CHECK(st.ran);
    CHECK(st.deleted == 0u);
    CHECK_MESSAGE(blob_exists(dir, shared),
                  "a blob another thread still references must survive");
}

TEST_CASE("blob gc: honours every blob reference key") {
    // 有些密钥按字面写("blob","output_blob");
    // 有些则按put_or_inline生成为`<field>_blob`,
    // 所以一个硬编码的列表会默默停止保护新的密钥。任何以_blob计数结束
    // 的事物。
    const auto dir = make_dir("keys");
    const auto img   = put_in(dir, std::string(10, '1'));
    const auto out   = put_in(dir, std::string(20, '2'));
    const auto think = put_in(dir, std::string(30, '3'));
    const auto sig   = put_in(dir, std::string(40, '4'));
    const auto text  = put_in(dir, std::string(50, '5'));
    const auto orph  = put_in(dir, std::string(60, '6'));
    REQUIRE(count_blobs(dir) == 6u);

    write_file(dir / "t.jsonl", nlohmann::json{
        {"role", "assistant"},
        {"images",     nlohmann::json::array({nlohmann::json{{"blob", img}}})},
        {"tool_calls", nlohmann::json::array({nlohmann::json{{"output_blob", out}}})},
        {"thinking_blob", think},
        {"thinking_signature_blob", sig},
        {"thinking_blocks", nlohmann::json::array({
            nlohmann::json{{"text_blob", text}}})}
    }.dump() + "\n");

    const auto st = blobs::collect_in(dir);
    CHECK(st.ran);
    CHECK_MESSAGE(st.deleted == 1u, "only the genuinely unreferenced blob goes");
    for (const auto& n : {img, out, think, sig, text})
        CHECK_MESSAGE(blob_exists(dir, n), "every _blob key must be honoured");
    CHECK_FALSE(blob_exists(dir, orph));
}

TEST_CASE("blob gc: an unreadable thread aborts the sweep") {
    // 无法假定没有未知的引用。 腐败 线程
    // 文件正是用户最需要时 载荷 独自一人
    const auto dir = make_dir("unreadable");
    const auto orphan = put_in(dir, std::string(100, 'x'));
    write_file(dir / "broken.json", "{ this is not json at all");

    const auto st = blobs::collect_in(dir);
    CHECK_MESSAGE(!st.ran, "the sweep must report that it did not run");
    CHECK(st.unreadable == 1u);
    CHECK(st.deleted == 0u);
    CHECK_MESSAGE(blob_exists(dir, orphan),
                  "nothing may be deleted while any thread is unreadable");
}

TEST_CASE("blob gc: a torn log line aborts the sweep") {
    // 日志格式的相同规则:部分的终线表示
    // 这个线程的参考集不完整.
    const auto dir = make_dir("torn");
    const auto orphan = put_in(dir, std::string(100, 'y'));
    write_file(dir / "t.jsonl",
               "{\"role\":\"user\",\"text\":\"fine\"}\n{\"role\":\"user\",\"te");

    const auto st = blobs::collect_in(dir);
    CHECK_FALSE(st.ran);
    CHECK(st.deleted == 0u);
    CHECK(blob_exists(dir, orphan));
}

TEST_CASE("blob gc: an unlistable threads dir deletes nothing") {
    // 最恶心的版本 "unknown references cannot be assumed absent"
    // 因为它看起来一点也不像失败。
    //
    // `directory_iterator(dir, ec)` 产量
    // 无内容 当它无法打开目录和范围时 -for 无法从空目录中分辨。
    // 因此,一个无法读取的线程 dir扫描了0线程,发现0个参考文献,
    // 在0时留下`unreadable`,而扫描时却完全没有看到
    // 参考文献,删除了EVERY blob。下面的安全规则已经
    // 说过了如何处理一个不可读输入;没有告诉它这是一个。
    //
    // 根能读到任何东西,所以它会 空虚地通过那里。 Windows 无法
    // 表达 "unlistable" 结束 fs::permissions
    // 完全没有(仅指 切换 读取 -only 属性),所以先决条件不能被构建.
#if defined(_WIN32)
    return;
#else
    if (::geteuid() == 0) return;
#endif

    const auto dir = make_dir("unlistable");
    const auto blob = put_in(dir, std::string(256, 'u'));
    write_file(dir / "t.json", nlohmann::json{
        {"id", "t"},
        {"messages", nlohmann::json::array({
            nlohmann::json{{"images", nlohmann::json::array({
                nlohmann::json{{"blob", blob}}})}}})}
    }.dump());

    // 电话 blobs/ 子目录可读 - 只有 线程 列表失败,
    // 因此 GC 还可以 SEE 编号 载荷 可能会错误地删除。
    std::error_code ec;
    const auto before = fs::status(dir, ec).permissions();
    fs::permissions(dir, fs::perms::owner_exec, ec);   // 过路,不是列表

    const auto st = blobs::collect_in(dir);

    fs::permissions(dir, before, ec);                  // 恢复清理

    CHECK_MESSAGE(!st.ran, "a sweep that could not read its input did not run");
    CHECK_MESSAGE(st.unreadable > 0u,
                  "an unlistable dir counts as unreadable, not as empty");
    CHECK_MESSAGE(st.deleted == 0u, "nothing is deleted on unknown references");
    CHECK_MESSAGE(blob_exists(dir, blob),
                  "the referenced payload survives");
}

TEST_CASE("blob gc: dry_run reports without deleting") {
    const auto dir = make_dir("dry");
    const auto orphan = put_in(dir, std::string(1024, 'z'));
    write_file(dir / "t.json",
               nlohmann::json{{"id", "t"}, {"messages", nlohmann::json::array()}}.dump());

    const auto st = blobs::collect_in(dir, /*dry_run=*/true);
    CHECK(st.ran);
    CHECK_MESSAGE(st.deleted == 1u, "dry run still reports what it would do");
    CHECK(st.bytes_freed == 1024u);
    CHECK_MESSAGE(blob_exists(dir, orphan), "dry run must not delete");
    CHECK(count_blobs(dir) == 1u);
}

TEST_CASE("blob gc: metadata sidecars are scanned too") {
    // <id>.meta.json 持有压缩摘要,
    // 这些摘要将经过 put_or_inline ,因此可以带一个
    // 粗略的引用。跳过它们会删除一个仍在使用的概要。
    const auto dir = make_dir("meta");
    const auto summary = put_in(dir, std::string(500, 'm'));
    write_file(dir / "t.jsonl", "{\"role\":\"user\",\"text\":\"hi\"}\n");
    write_file(dir / "t.meta.json", nlohmann::json{
        {"id", "t"},
        {"compactions", nlohmann::json::array({
            nlohmann::json{{"up_to_index", 1}, {"summary_blob", summary}}})}
    }.dump());

    const auto st = blobs::collect_in(dir);
    CHECK(st.ran);
    CHECK(st.deleted == 0u);
    CHECK_MESSAGE(blob_exists(dir, summary),
                  "a reference in the metadata sidecar must be honoured");
}

TEST_CASE("blob gc: dangling references are reported, not fatal") {
    // 提到一个不存在真正的虫子的blob,
    // 但GC的工作是注意它而不是使它更糟糕.
    const auto dir = make_dir("dangling");
    const auto real = put_in(dir, std::string(64, 'r'));
    write_file(dir / "t.json", nlohmann::json{
        {"id", "t"},
        {"messages", nlohmann::json::array({
            nlohmann::json{{"images", nlohmann::json::array({
                nlohmann::json{{"blob", real}},
                nlohmann::json{{"blob", "0000000000000000-ff"}}})}}})}
    }.dump());

    const auto st = blobs::collect_in(dir);
    CHECK(st.ran);
    CHECK(st.dangling == 1u);
    CHECK(st.deleted == 0u);
    CHECK(blob_exists(dir, real));
}

TEST_CASE("blob gc: an empty store is a no-op") {
    const auto dir = make_dir("empty");
    const auto st = blobs::collect_in(dir);
    CHECK(st.ran);
    CHECK(st.total_blobs == 0u);
    CHECK(st.deleted == 0u);
    CHECK(st.scanned_threads == 0u);
}

TEST_CASE("blob gc: grace window spares young orphans") {
    const auto dir = make_dir("grace");
    const auto young = put_in(dir, std::string(100, 'y'));
    const auto old   = put_in(dir, std::string(200, 'o'));
    std::error_code ec;
    fs::last_write_time(dir / "blobs" / old,
                        fs::file_time_type::clock::now() - std::chrono::hours{48}, ec);
    REQUIRE(!ec);
    write_file(dir / "t.json", nlohmann::json{{"id", "t"},
        {"messages", nlohmann::json::array()}}.dump());

    const auto st = blobs::collect_in(dir, false, std::chrono::hours{24});
    CHECK(st.ran);
    CHECK_MESSAGE(blob_exists(dir, young),
                  "a fresh orphan may be mid-save; it must survive");
    CHECK_FALSE(blob_exists(dir, old));
    CHECK(st.deleted == 1u);
}

// 背景情况 GC 永远不能超过寿命 main()。该词 Windows
// CI 烟管烟雾试验(Stdin at EOF – 立即退出)
// 0xC00000005 时,行走在脱落处坠毁 线程。启动 + 立即加入。
// 启动 + 必须立即返回, 并且不运行 ; 加入必须是 幂等.
TEST_CASE("blob gc: background thread joins at once and runs nothing") {
    const auto t0 = std::chrono::steady_clock::now();
    blobs::start_background_gc();
    blobs::start_background_gc();                 // 第二首是n-op
    blobs::join_background_gc(agtest::cap());
    blobs::join_background_gc(agtest::cap());                  // idempotent
    blobs::start_background_gc();                 // 加入后: 停止
    blobs::join_background_gc(agtest::cap());
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    CHECK_MESSAGE(ms < 1000, "join must not wait out the start delay");
}
