// shared_file_test — persistence::SharedFile 不包括第二
// 笔 agentty PROCESS ,并显示 agentty 边 锁 实际被传输连接到 jaal.
//
// 测试覆盖的行为及明确不作保证的部分。
//
// jaal's file_lock 兼容套件拥有内核语义
// ( 一个第二个进程等待, 一个死持者什么也不要,
// 共享 vs 独家)。 此测试拥有问题 jaal 无法回答
// : AGENTTY 关键部分真的拿 锁 ,在它命名的文件的侧车上,
// 或者政策层被线到什么?
//
// 不错 不 主张 "a thread cannot vanish from the picker" 写到,
// 而这种说法是虚假的: load_all_threads () 列举
// THREADS DIRECTORY 和用途 index.json 仅
// 作为元数据 缓存 ,所以一个丢失的指数条目成本为 -parse 并自定义
// -heals。索引的诚实成本 竞态 是一个慢 加载 和简短的 过期 标题。
// 值得测试的保证如下。
//
// 叉子,别 线程。线性版本通过。 -process
// 互斥量 并且不会证明 所缺少的一半。

#include "agentty/io/shared_file.hpp"
#include <thread>
#include "agtest_cap.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>

#if !defined(_WIN32)
#  include <cerrno>
#  include <sys/wait.h>
#  include <unistd.h>
#  define HAVE_FORK 1
#else
#  define HAVE_FORK 0
#endif

namespace fs = std::filesystem;
namespace sf = agentty::persistence;

namespace {  // 归并: TU-local (捆绑成 agentty_standalone_tests)
static int g_fail = 0;
#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_fail; }           \
        else         { std::printf("  ok:   %s\n", msg); }                     \
    } while (0)

sf::Lane& test_mu() {
    static sf::Lane m;
    return m;
}

}  // 名称空间( 归并)

// 电话 归并 将它重命名为 shared_file_test_main
// 途经 -Dmain=... 因为它的叉子 需要这个测试
int main() {
    std::printf("shared_file_test\n");

    const fs::path dir =
        fs::temp_directory_path()
        / ("agentty_sharedfile_" + std::to_string(
#if HAVE_FORK
               ::getpid()
#else
               0
#endif
           ));
    fs::create_directories(dir);
    const fs::path target = dir / "guarded.json";

    // 锁必须是真实的,而不是默默的-degraded no-op.
    // Atmpfs/ext4/tmp支持锁,所以这里的一个假表示政策
    // 层完全没有达到jaal——这就是这个测试存在的用来捕捉的线虫.
    CHECK(sf::with_shared_file(agtest::cap(), test_mu(), target, [](const maya::held<agentty::lock_levels::file_lane>&, sf::Held h) { return h.cross_process; }),
          "with_shared_file took the cross-process lock (jaal is wired in)");

    // 电话 锁 生活状况 SIDECAR ,绝不在目标上。
    // 这是整个正确性论点: agentty 将一个新的inode
    // 重命名于目标上, 以此写入这些文件, 所以 锁 被扣在目标上会
    // 保护一个不再是文件的文件.
    {
        const auto [sidecar, touched] = sf::with_shared_file(agtest::cap(), test_mu(), target,
            [](const maya::held<agentty::lock_levels::file_lane>&, sf::Held, std::string t) {
                return std::pair{fs::exists(fs::path(t + ".lock")), fs::exists(fs::path(t))};
            }, target.string());
        CHECK(sidecar, "the lock is held on <target>.lock");
        CHECK(!touched, "locking does not create or touch the target itself");
    }

#if HAVE_FORK
    // 一会儿 PROCESS 儿童报告等待了多久;
    // 必须被阻断,以获得父母的一小部分控制权,
    // 否则排除并不跨越过程界限和
    // 竞态 仍然开放。
    //
    // ORDER MATTERS : 父母一方接受 锁 之前
    // 抢先抢先抢后抢先抢 竞态 测试在平行下丢失 加载 -
    // 儿童先入后出,并报告 "waited 0 ms" 看上去
    // 就像一个破碎的 锁 . (在试验中失败) -j12 独立通过,
    // 这是 标志一个racy测试,而不是racy 锁.)
    //
    // 调值 锁 已经 不 儿童使用自己的武器,
    // 而不是分享我们的财富。 互斥量 因为我们的复制品
    // test_mu () 继承 LOCKED.
    {
        // 整个父母一方运行在关键部分:叉,
        // 握kHold,然后离开,然后读孩子的判决.
        const auto child_waited = sf::with_shared_file(agtest::cap(), test_mu(), target,
            [](const maya::held<agentty::lock_levels::file_lane>&, sf::Held, std::string t) -> long long {
                constexpr auto kHold = std::chrono::milliseconds{400};
                int pipefd[2] = {-1, -1};
                if (::pipe(pipefd) != 0) return -2;
                const pid_t pid = ::fork();
                if (pid == 0) {
                    ::close(pipefd[0]);
                    const auto t0 = std::chrono::steady_clock::now();
                    // 我们的车道复制品是被锁起来的,这个也是 线程
                    // 持有 -lock 记录( jaal's 线程-local 级别。
                    // 从新开始的相交 线程 拥有自己的车道:
                    // 它开始没有东西, 像任何真正的第二个过程。
                    static sf::Lane child_lane;
                    long long v = -1;
                    std::thread([&] {
                        try {
                            sf::with_shared_file(agtest::cap(), child_lane, fs::path{t},
                                [](const maya::held<agentty::lock_levels::file_lane>&, sf::Held) {});
                            v = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - t0).count();
                        } catch (const std::exception& e) {
                            std::fprintf(stderr, "  child threw: %s\n", e.what());
                        }
                    }).join();
                    (void)!::write(pipefd[1], &v, sizeof(v));
                    ::close(pipefd[1]);
                    ::_exit(0);
                }
                ::close(pipefd[1]);
                ::usleep(static_cast<unsigned>(kHold.count()) * 1000u);
                // 离开 正文 的释放; 孩子会读到。
                // 将管道和管道打开, 将判决解锁。
                return (static_cast<long long>(pid) << 20) | pipefd[0];
            }, target.string());
        if (child_waited == -2) {
            CHECK(false, "pipe for the child's verdict");
        } else {
            const int rd = static_cast<int>(child_waited & 0xFFFFF);
            const pid_t pid = static_cast<pid_t>(child_waited >> 20);
            long long waited = -1;
            (void)!::read(rd, &waited, sizeof(waited));
            ::close(rd);
            int status = 0;
            while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
            if (waited < 0)
                std::printf("  info: child status: %s %d\n",
                            WIFSIGNALED(status) ? "signal" : "exit",
                            WIFSIGNALED(status) ? WTERMSIG(status) : WEXITSTATUS(status));
            std::printf("  info: child waited %lld ms (parent held ~400 ms)\n", waited);
            CHECK(waited >= 200, "a second PROCESS blocked until we released");
        }
    }
#else
    std::printf("  skip: no fork on this platform (cross-process case)\n");
#endif

    std::error_code ec;
    fs::remove_all(dir, ec);
    std::printf(g_fail ? "FAILED (%d)\n" : "PASSED\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
