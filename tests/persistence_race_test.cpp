// SPDX-License-Identifier: Apache-2.0
//
// persistence_race_test.cpp - 开车 真实 自动同步 保存 从多个队列中排入
// 线程 下,在 ThreadSanitizer.
//
// race_harness_test 模式写 -behind 形状; 此操作实际
// 代码 : persistence::save_thread 其云集的地图,
// 其懒惰 -started 工作线程, thread_index_mu ,
// 并显示 offset/blob 记账 save_thread_sync 实际的货币,
// 也就是 状态更新函数 保存 自动调试 UI 线程 时 工作线程 丝丝,和 `agentty acp`
// 保存 几届会议 工作线程 立刻来
//
// 不变量 钉住 :
//   • 没有数据竞态(即TSan的工作;这个文件只是提供压力)
//   • 每一个不同的 线程 id在同时存在的情况下存活下来 保存 风暴
//   • 冲水沟而不是截流沟
//   • a 保存 已印发 之后 冲动静态( 写) -behind 队列重新启动
//     编号 工作线程 而不是默默地放下迟到的写作——留下的形状
//     settings_cache's 工作线程 可合并,退出时中止)

#include "agentty/io/persistence.hpp"
#include "agtest_cap.hpp"

#include <atomic>
#include <barrier>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL: %s\n", what); ++failures; }
}

constexpr int kThreads = 6;
constexpr int kSaves   = 25;

agentty::Thread make_thread(int id, int gen) {
    agentty::Thread t;
    t.id    = agentty::ThreadId{"race-" + std::to_string(id)};
    t.title = "gen " + std::to_string(gen);
    agentty::Message m;
    m.role = agentty::Role::User;
    m.text = "body " + std::to_string(gen);
    t.messages.push_back(std::move(m));
    return t;
}

int count_saved() {
    int n = 0;
    for (const auto& t : agentty::persistence::load_all_threads(::agentty::IoAccess::grant(), agtest::cap()))
        if (t.id.value.starts_with("race-")) ++n;
    return n;
}

} // 命名空间结束。

int main() {
    const auto root = fs::temp_directory_path() /
        ("agentty_persistence_race_" +
         std::to_string(std::chrono::steady_clock::now()
                            .time_since_epoch().count()));
    fs::create_directories(root);
#ifdef _WIN32
    _putenv_s("AGENTTY_HOME", root.string().c_str());
#else
    ::setenv("AGENTTY_HOME", root.string().c_str(), 1);
#endif

    std::printf("=== persistence_race_test ===\n");

    // ── 同步的保存风暴 ────────────────────────────────────────────
    // 每一个 线程 锤子 自身 id,
    // 所以联合是自由 放弃中间代,但决不能完全放弃一个ID。
    {
        std::barrier sync(kThreads);
        std::vector<std::thread> ts;
        std::atomic<int> threw{0};
        for (int t = 0; t < kThreads; ++t) {
            ts.emplace_back([&, t] {
                sync.arrive_and_wait();
                for (int i = 0; i < kSaves; ++i) {
                    try { agentty::persistence::save_thread(::agentty::IoAccess::grant(), agtest::cap(), make_thread(t, i)); }
                    catch (...) { threw.fetch_add(1); }
                }
            });
        }
        for (auto& t : ts) t.join();
        agentty::persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());

        check(threw.load() == 0, "save_thread threw under concurrency");
        check(count_saved() == kThreads, "a thread id was lost in the storm");
        std::printf("  save storm: %d/%d ids landed\n", count_saved(), kThreads);
    }

    // ── A 保存 之后 冲动静地 ───────────────────────────────────
    // flush_and_stop (节) `stopping` ,
    // 所以写 -behind 队列必须重新启动 工作线程 写作迟到,
    // 而不是丢掉 保存 假设:退出 状态更新函数 问题1,以及 ACP 届会可以
    // 保存 之后 TUI 已经冲洗。
    {
        agentty::persistence::save_thread(::agentty::IoAccess::grant(), agtest::cap(), make_thread(999, 0));
        agentty::persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());
        const bool landed = std::ranges::any_of(
            agentty::persistence::load_all_threads(::agentty::IoAccess::grant(), agtest::cap()),
            [](const agentty::Thread& t) { return t.id.value == "race-999"; });
        check(landed, "a save issued after flush was dropped");
        std::printf("  post-flush save: %s\n", landed ? "persisted" : "DROPPED");
    }

    std::error_code ec;
    fs::remove_all(root, ec);

    if (failures == 0) { std::printf("PASS\n"); return 0; }
    std::printf("FAILED: %d check(s)\n", failures);
    return 1;
}
