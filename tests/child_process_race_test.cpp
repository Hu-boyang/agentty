// child_process_race_test — ChildProcess 下加的 stop/close/write.
//
// 孩子们的血清和过程本身从几个 线程 实际使用:
// 对端 作家写道: stop() 关闭 Stdin,
// 主人的毁灭器终止并收割。 这把那些 人 绑在一起。
// -tsan 任何不同步的进入,
// 普通建筑检查什么 死锁 或坠机。
#include "agentty/util/child_process.hpp"

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#if AGENTTY_HAVE_CHILD_PROCESS

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
}

agentty::util::ChildProcess::Spawn cat_spawn() {
    agentty::util::ChildProcess::Spawn s;
#if defined(_WIN32)
    s.command = "cmd";   // `more` 呼应 stdin 和 退出 EOF 猫一样
    s.args    = {"/c", "more"};
#else
    s.command = "cat";   // 呼应 stdin; EOF 上的出口
#endif
    return s;
}

// 作家竞态 close_stdin():任何写作都不得登陆封闭或重复使用的fd.
// 1个写作机,如实际使用(对端的写作机线程);一个std::ostream
// 对2个写作器不安全.
void writers_vs_close() {
    for (int round = 0; round < 100; ++round) {
        agentty::util::ChildProcess c{cat_spawn()};
        std::atomic<bool> go{false};
        std::vector<std::thread> ts;
        ts.emplace_back([&] {
            while (!go) std::this_thread::yield();
            for (int k = 0; k < 100; ++k) { c.in() << "line\n"; c.in().flush(); }
        });
        ts.emplace_back([&] {
            while (!go) std::this_thread::yield();
            c.close_stdin();
        });
        go = true;
        for (auto& t : ts) t.join();
    }
    check(true, "a writer racing close_stdin finished");
}

// alive() 民意测验和 terminate() 从不同的收获 线程
// :完全一个收获,退出代码被两者所看到.
void poll_vs_terminate() {
    for (int round = 0; round < 50; ++round) {
        agentty::util::ChildProcess c{cat_spawn()};
        std::atomic<bool> stop{false};
        std::thread poller([&] { while (!stop) (void)c.alive(); });
        c.terminate();
        stop = true;
        poller.join();
        check(!c.alive(), "child is gone after terminate");
        check(c.exit_code().has_value(), "exit code recorded");
    }
}

// 儿童自行离开(Stdin EOF),而另一第线程终止。
void natural_exit_vs_terminate() {
    for (int round = 0; round < 50; ++round) {
        agentty::util::ChildProcess c{cat_spawn()};
        std::thread closer([&] { c.close_stdin(); });
        std::thread killer([&] { c.terminate(); });
        closer.join();
        killer.join();
        check(!c.alive(), "child gone after a racing exit and terminate");
    }
}
}  // 命名空间结束。

int main() {
    writers_vs_close();
    poll_vs_terminate();
    natural_exit_vs_terminate();
    if (failures) return 1;
    std::printf("child_process_race_test: ok\n");
    return 0;
}

#else
int main() { return 0; }
#endif
