// concurrency_primitives_test - 证明
// util::run_isolated_detached 提供背景工作 OWNED 三种保证,
// 只有一种保证是:
//   a. a 正文 扔的对 不 到达 std::terminate ,
//   b. 工作为: WAITED FOR 关闭时,不分离和遗忘,c.
//   正文 被递出一个 stop_token 并请求停止。
// (b)和(c) 与 maya::pool。旧手 -rolled 版本
// 是一个平原 std::thread(...).detach() 其
// 评论声称: "self-joining reaper" 不存在,
// 所以没有加入,也没有 取消 . 下面的测试会失败。
//
// (它也用来测试 util::RankedLock ,则 锁-order 绊线。
// 没有任何内容使用排序 锁 更多:每份共享 锁 是一个 maya::guarded ,
// 无俘虏 正文 无法用掉秒 锁 ) 完全没有.

#include "agtest_cap.hpp"
#include <atomic>
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstdlib>


#include "agentty/util/background.hpp"

namespace {

int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (!(cond)) { std::fprintf(stderr, "FAIL: %s\n", (msg)); ++failures; } \
        else         { std::fprintf(stderr, "ok:   %s\n", (msg)); }             \
    } while (0)

// ── 即: OWNED ───────────────────────────────────────
// 一次测试,一次关闭: 池 关闭是 终端 (后来的
// 岗位被撤),所以孤立和 取消 要求的排水口是相同的,
// 而不是两条路口。
//
// 旧版本以("even if some haven't run, the point is none terminated")
// 结束的spin-and-hope正是一个脱落的线程在测试中迫使的.
// 这里的shutdown()JOINS,所以计数器是断言
// 而不是竞态:如果工作仍然脱落,这些计算将是不可靠的,
// `stuck`将是毫无意义的.
struct Counters {
    std::atomic<int>  ran{0};
    std::atomic<bool> saw_stop{false};
};
using counters_t = maya::co_owned<Counters>;

void test_background_work_is_owned() {
    auto c = counters_t::make();

    // (a) 投掷正文必须孤立,每一枚。
    for (int i = 0; i < 8; ++i) {
        agentty::util::run_isolated_detached("test.throwing_worker",
            [](std::stop_token, counters_t c) {
                c->ran.fetch_add(1);
                throw std::logic_error("boom — must not reach std::terminate");
            }, c);
    }

    // (c) a 正文 必须通过它来阻止它。
    agentty::util::run_isolated_detached("test.cancellable_worker",
        [](std::stop_token st, counters_t c) {
            while (!st.stop_requested())
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            c->saw_stop = true;
        }, c);

    // (b) 关闭 REQUESTS STOP 财务报告和已审计财务报表 WAITS
    // 返回多少个在宽限期前仍然被卡住。
    const std::size_t stuck = agentty::util::background_pool().shutdown(agtest::cap());

    CHECK(c->ran.load() == 8,
          "every throwing body ran and was waited for (not detached)");
    CHECK(c->saw_stop.load(),
          "a background body is handed a stop_token and is asked to stop");
    CHECK(stuck == 0, "shutdown drained every job within the grace");
    CHECK(true, "8 throwing workers did not terminate the process");
}

} // 命名空间结束。

int main() {
    test_background_work_is_owned();

    if (failures == 0) {
        std::fprintf(stderr, "\nALL concurrency-primitive checks passed.\n");
        return 0;
    }
    std::fprintf(stderr, "\n%d check(s) FAILED.\n", failures);
    return 1;
}
