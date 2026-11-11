// thread_index_test.cpp 时间 缓存 其实 缓存.
//
// 针头上的虫子: file_mtime_secs 转换 fs::file_time_type
// 读取 time_since_epoch ()直接。该时钟的时代是执行
// -defined ——在libstdc++上,它坐落在1822年左右——所以:
//
//   值从64纳秒里溢出出来 NEGATIVE as seconds no explus,
//   as as easy as 1822年时仍可测量的溢出量,
//
// 不管是哪一种数字 index.json 并非Unix时间戳。第二个形式是自我
// -consistent ,所以,请 缓存 似乎工作; 第一个意味着没有条目
// 匹配它的文件和每个启动重置 -parsed 整个历史,605年 线程 和
// 704号决议 MB 磁盘上, 离 线程 列表存在,该列表显示为
// Alt+→ 首次报告没有会话，第二次却正常。
//
//
// 两者都看不见 测试只检查 load_all_threads
// 返回右边 线程 不管是哪种方式,都慢慢来 TIMESTAMP ,
// 这是 事情是错的。
#include "agentty/io/persistence.hpp"
#include "agtest_cap.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <string>

namespace fs = std::filesystem;
using namespace agentty;

namespace {

int failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            ++failures;                                                   \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        }                                                                 \
    } while (0)

// 一例为纯净的线程 dir.
//
// 不 其所属 $AGENTTY_HOME : 独立的绳索已经是整个沙盒
// 二进制程序 (agentty_standalone_tests_main.cpp ),
// 以及 user_root ( ) 解决一次 — — 所以这里的
// 第二个设置被忽略了, 下面的每一个路径都会在绳子根部降落,
// 而这个阶层相信它拥有一个私人的。 这种不匹配就是为什么这些案例读
// mtime - 1: 文件是写在测试没有在的地方的。
//
// 所以:在绳子的根部工作, 并清理 线程 目录。
struct Sandbox {
    fs::path dir;
    Sandbox() {
        dir = persistence::threads_dir();
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    // 路上安全,马上 IN 带子是共享的 threads_dir
    // () 并且同步写作机可能仍在耗尽 a 保存 当一个
    // 大小写返回时——删除它在这里比赛了下一个小写设置,
    // 然后发现没有 线程 完全没有文件。
    ~Sandbox() = default;
};

Thread make_thread(const std::string& id, const std::string& title) {
    Thread t;
    t.id = ThreadId{id};
    t.title = title;
    t.created_at = std::chrono::system_clock::now();
    t.updated_at = t.created_at;
    // 不 选项: persistence::save_thread
    // 将一个线程 丢弃, 地板上没有消息( 空的 线程 是用户从未
    // 使用的 线程 )。 如果没有这个案例, 已保存 什么都没有,
    // 那么它就会在从未写过的文件索引上断言。
    Message m;
    m.role = Role::User;
    m.text = "hello";
    t.messages.push_back(std::move(m));
    return t;
}

/// 索引本身记录了每个文件何时被写出来.
long long indexed_mtime(const std::string& id) {
    const auto p = persistence::threads_dir() / "index.json";
    std::ifstream in(p);
    if (!in) return -1;
    std::string all((std::istreambuf_iterator<char>(in)), {});
    // 故意不和nlohmann 解释:这个测试是关于
    // 什么的 IN 查找它会隐藏一个序列错误。
    // "<id>" 然后是下一个 "mtime" 曰:后为.
    const auto at = all.find('"' + id + '"');
    if (at == std::string::npos) return -1;
    const auto key = all.find("\"mtime\"", at);
    if (key == std::string::npos) return -1;
    const auto colon = all.find(':', key);
    return std::strtoll(all.c_str() + colon + 1, nullptr, 10);
}

void the_index_records_a_unix_timestamp() {
    std::printf("the index records a real timestamp\n");
    Sandbox sb;

    persistence::save_thread(::agentty::IoAccess::grant(), agtest::cap(), make_thread("aaaa000000000001", "one"));
    persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());
    (void)persistence::load_all_threads(::agentty::IoAccess::grant(), agtest::cap());   // 构建索引

    const long long mt = indexed_mtime("aaaa000000000001");
    const long long now = std::chrono::duration_cast<std::chrono::seconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count();

    // 正面,显然——负值是原始的bug.
    CHECK(mt > 0);
    // 其实 当前 ,而不是1822年,也不是2286年:在钟
    // 后一分钟内,程序的其他部分使用。
    // 这是检查 "seconds since file_clock's epoch"
    // 版本失败,而它使数字对阅读文件的任何东西都意味着什么。
    CHECK(mt > now - 60 && mt < now + 60);

    std::printf("  mtime %lld, now %lld (%+lld s)\n", mt, now, mt - now);
}

void a_warm_index_is_reused() {
    std::printf("an unchanged thread is not re-parsed\n");
    Sandbox sb;

    persistence::save_thread(::agentty::IoAccess::grant(), agtest::cap(), make_thread("aaaa000000000002", "two"));
    persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());

    const auto first = persistence::load_all_threads(::agentty::IoAccess::grant(), agtest::cap());
    CHECK(first.size() == 1);
    const long long mt1 = indexed_mtime("aaaa000000000002");

    // 第二步浏览一个未触动的文件。 条目必须从逐字
    // 记录中幸存下来: 如果邮票不匹配, 文件是re-parsed,
    // 索引重写, 这正是一直被拍摄的慢
    // 路径。
    const auto second = persistence::load_all_threads(::agentty::IoAccess::grant(), agtest::cap());
    CHECK(second.size() == 1);
    CHECK(indexed_mtime("aaaa000000000002") == mt1);
    if (!second.empty()) CHECK(second[0].title == "two");

    std::printf("  stamp unchanged across two loads (%lld)\n", mt1);
}

void a_changed_thread_is_re_read() {
    std::printf("a modified thread IS re-parsed\n");
    Sandbox sb;

    persistence::save_thread(::agentty::IoAccess::grant(), agtest::cap(), make_thread("aaaa000000000003", "before"));
    persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());
    (void)persistence::load_all_threads(::agentty::IoAccess::grant(), agtest::cap());

    // 重写它时使用不同的标题。 缓存 不得 服务于旧的
    // ——一面从未改变的邮票,与一面从未匹配的邮票一样错误,
    // 只是朝另一个方向。
    auto t = make_thread("aaaa000000000003", "after");
    persistence::save_thread(::agentty::IoAccess::grant(), agtest::cap(), t);
    persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());

    const auto again = persistence::load_all_threads(::agentty::IoAccess::grant(), agtest::cap());
    CHECK(again.size() == 1);
    if (!again.empty()) CHECK(again[0].title == "after");

    std::printf("  the new title came back, not the cached one\n");
}

void an_index_from_an_older_version_is_discarded() {
    std::printf("an index written by an older version is dropped\n");
    Sandbox sb;

    persistence::save_thread(::agentty::IoAccess::grant(), agtest::cap(), make_thread("aaaa000000000004", "four"));
    persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());
    (void)persistence::load_all_threads(::agentty::IoAccess::grant(), agtest::cap());

    // 电话 线程 伪造索引输入前, 文件必须放在磁盘上 —— 整个案例是
    // 丢弃过期索引，并从文件重新构建。
    // 如果没有文件可以重建,则无法证明任何文件。
    // `.jsonl`: a 线程 是一个附件 -only 日志, 不为一个 JSON 文档。
    const auto thread_file =
        persistence::threads_dir() / "aaaa000000000004.jsonl";
    CHECK(fs::exists(thread_file));

    // 伪造 v3 索引: 形状正确, 但它的邮票是从
    // file_clock 时代测量的。 读它会复活错误,
    // 所以版本门必须丢弃它。
    const auto p = persistence::threads_dir() / "index.json";
    {
        std::ofstream out(p, std::ios::trunc);
        out << R"({"version":3,"threads":{"aaaa000000000004":)"
            << R"({"created_at":0,"mtime":-4650357762,"size":1,"title":"stale"}}})";
    }

    const auto loaded = persistence::load_all_threads(::agentty::IoAccess::grant(), agtest::cap());
    CHECK(loaded.size() == 1);
    // 标题来自FILE,而不是从伪造条目.
    if (!loaded.empty()) CHECK(loaded[0].title == "four");
    // 重建后的索引又印上一个正常的印章.
    CHECK(indexed_mtime("aaaa000000000004") > 0);

    std::printf("  v3 discarded; rebuilt from the files\n");
}

void old_threads_expire() {
    std::printf("threads idle past keep_days are deleted, others kept\n");
    Sandbox sb;
    const auto io = ::agentty::IoAccess::grant();
    const auto dir = persistence::threads_dir();
    // 旧文件会留下它们; 这一过程永远不会 已保存 他们。
    auto fake = [&](const std::string& id, int days_old) {
        for (const char* ext : {".jsonl", ".ofs", ".meta.json", ".transcript.md"}) {
            const auto p = dir / (id + ext);
            std::ofstream(p) << "x";
            fs::last_write_time(p, fs::file_time_type::clock::now() - std::chrono::days{days_old});
        }
    };
    fake("bbbb000000000001", 40);   // 旧
    fake("bbbb000000000002", 5);    // 最近的情况
    fake("bbbb000000000003", 40);   // 旧但开放
    std::ofstream(dir / "index.json") << "{}";
    fs::last_write_time(dir / "index.json", fs::file_time_type::clock::now() - std::chrono::days{90});

    CHECK(persistence::expire_threads(io, agtest::cap(), 0, ThreadId{}).threads == 0);   // 关
    const auto dry = persistence::expire_threads(io, agtest::cap(), 30, ThreadId{"bbbb000000000003"}, true);
    CHECK(dry.threads == 1);
    CHECK(fs::exists(dir / "bbbb000000000001.jsonl"));                  // 干线运行删除任何内容

    const auto st = persistence::expire_threads(io, agtest::cap(), 30, ThreadId{"bbbb000000000003"});
    CHECK(st.threads == 1);
    CHECK(!fs::exists(dir / "bbbb000000000001.jsonl"));
    CHECK(!fs::exists(dir / "bbbb000000000001.meta.json"));
    CHECK(!fs::exists(dir / "bbbb000000000001.transcript.md"));
    CHECK(fs::exists(dir / "bbbb000000000002.jsonl"));
    CHECK(fs::exists(dir / "bbbb000000000003.jsonl"));
    CHECK(fs::exists(dir / "index.json"));                               // 不详 线程

    // A 线程 这一进程 已保存 打开某处:从未过期。
    persistence::save_thread(io, agtest::cap(), make_thread("bbbb000000000004", "live"));
    persistence::flush_pending_saves(io, agtest::cap());
    for (const auto& e : fs::directory_iterator(dir))
        if (e.path().filename().string().starts_with("bbbb000000000004"))
            fs::last_write_time(e.path(), fs::file_time_type::clock::now() - std::chrono::days{90});
    (void)persistence::expire_threads(io, agtest::cap(), 30, ThreadId{});
    CHECK(fs::exists(dir / "bbbb000000000004.jsonl"));

    std::printf("  deleted %zu, kept the recent, open and live ones\n", st.threads);
}

}  // 命名空间结束。

int main() {
    std::printf("agentty thread index\n\n");
    the_index_records_a_unix_timestamp();
    a_warm_index_is_reused();
    a_changed_thread_is_re_read();
    an_index_from_an_older_version_is_discarded();
    old_threads_expire();
    std::printf("\n%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
