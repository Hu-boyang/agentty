// SPDX-License-Identifier: Apache-2.0
//
// real_subsystem_race_test.cpp - 开车 真实 共享 接口边界 下三圣门.
//
// race_harness_test.cpp 手动驱动器
// -written MODELS 备注 agentty 共享 接口边界
// (a 快照 存储单元 ,一个撕裂 注册表 ,写作 -behind。
// 在模式中捕获设计错误,但在实际子系统中却没有错误:模型和代码可以漂移,
// 而船舶的代码就是它。此文件 链接 真实 编译单元 并击打他们。
//
// 每个案子都像虫子一样 代码库 实际已装运或
// 可能可能装运的:
//
//   1. 拆卸 注册表 同时拆卸  ⁇  register/cancel/run - settings_cache
//                          错误是一个缺失的注册;危险的版本是
//                          cancel()赛车run(),
//                          而让注册表持有回调的牌子变成一个死物.
//   2. 工具 注册表 并行阅读器 传输-tool 目录 时
//                          插件重装它。 这是 `@`
//                          -picker 虫形:N读物,一个出版商.
//   3. 记录每个子系统记录,从每线程;
//                          编辑预过滤器会改变一个共享的抓痕。
//   4. 快照<T> 真实类型,而不是模型: 之前被拿走的手柄
//                          重新填报必须在此之后保持有效。
//
// 运行在 : -DAGENTTY_SANITIZE_ALL=thread 下

#include "agentty/util/snapshot.hpp"
#include "agentty/util/teardown.hpp"
#include "agentty/util/logx.hpp"
#include "agtest_cap.hpp"

#include <atomic>
#include <barrier>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_failures = 0;
void fail(const char* what) {
    std::printf("  FAIL: %s\n", what);
    ++g_failures;
}

// ── 1. 在同一注册表下拆卸注册表 /取消 /运行 ───────
// 电话 注册表 是进程 -wide 每一个线性子系统在启动和退出时都会触碰它。
// 竞态 这里有一个坠机,出口, 这是唯一的最糟糕的地方 有有一个(没有堆叠,
// 没有再pro, "it hung on quit").
void teardown_race() {
    std::printf("--- teardown: concurrent register/cancel/run ---\n");
    namespace td = agentty::util::teardown;

    constexpr int kThreads = 8;
    constexpr int kEach    = 200;
    std::atomic<int> fired{0};
    std::barrier sync(kThreads);

    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t) {
        ts.emplace_back([&, t] {
            sync.arrive_and_wait();
            for (int i = 0; i < kEach; ++i) {
                auto tok = td::on_shutdown("race", [&fired](const maya::unlocked&) {
                    fired.fetch_add(1, std::memory_order_relaxed);
                });
                // 一半的登记人 取消 本身——保持边框的形状
                // -lifetime 对象从 注册表.
                if ((t + i) % 2 == 0) td::cancel(tok);
            }
        });
    }
    for (auto& th : ts) th.join();

    // run() 绝对不能 死锁 双倍 -fire a 已取消 或旅行TSan。 或旅行的TSan。
    td::run(agtest::cap());
    td::run(agtest::cap());   // idempotent
    std::printf("  teardown: %d actions fired, registry drained to %zu\n",
                fired.load(), td::pending());
    if (td::pending() != 0) fail("registry not drained after run()");
}

// ── 2. 快照<T> : 数据 真实 类型在 publish/consume ─────────────────
// 关闭的bug是返回的 const-ref,
// 它的拥有的shared_ptr在访问器的末尾死亡。这里有一个读者握
// 着一个把手,在很多重排中被使用KEEPS;它指向的字节必须保持
// 生命不变。
void snapshot_real() {
    std::printf("--- Snapshot<T>: handle survives republish ---\n");
    using agentty::util::AtomicSnapshot;
    using agentty::util::make_snapshot;

    AtomicSnapshot<std::vector<std::string>> cell;
    cell.store(make_snapshot(std::vector<std::string>{"gen0"}));

    std::atomic<bool> stop{false};
    std::atomic<unsigned long> reads{0}, torn{0};

    std::thread publisher([&] {
        for (int gen = 1; gen < 4000 && !stop.load(std::memory_order_relaxed); ++gen) {
            cell.store(make_snapshot(
                std::vector<std::string>(8, "gen" + std::to_string(gen))));
        }
        stop.store(true, std::memory_order_relaxed);
    });

    std::vector<std::thread> readers;
    for (int r = 0; r < 4; ++r) {
        readers.emplace_back([&] {
            while (!stop.load(std::memory_order_relaxed)) {
                auto h = cell.load();         // 手柄载出 所有权
                // 拿着它,再用重排的,然后验证自己 -consistency:
                // 给定的一代的每一个元素都是相同的字符串.
                if (!h.empty()) {
                    const std::string& first = h[0];
                    for (const auto& s : h)
                        if (s != first) { torn.fetch_add(1, std::memory_order_relaxed); break; }
                }
                reads.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    publisher.join();
    for (auto& t : readers) t.join();

    std::printf("  snapshot: %lu reads, %lu torn\n", reads.load(), torn.load());
    if (torn.load() != 0) fail("Snapshot handed out a torn value");
}

// ── 3. 每一组的对数 线程 一次 ───────────────────────────────────
// 每个子系统日志,从什么 线程 剪辑经过了改写
// 载荷 在它降落之前, 共同的刮伤 缓冲区 有个
// 竞态 那样会破坏日志线(或更糟的日志),
void logx_race() {
    std::printf("--- logx: concurrent emit from many threads ---\n");
    constexpr int kThreads = 8;
    std::barrier sync(kThreads);
    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t) {
        ts.emplace_back([&, t] {
            sync.arrive_and_wait();
            for (int i = 0; i < 500; ++i) {
                // 包含重排者必须扫描的 : 一个符号-ish 字符串,
                // 因此预过滤器的高频路径 被执行。
                AGT_LOG(Tool, Info, "race",
                        "thread={} i={} tok=sk-ant-{}", t, i, "0123456789abcdef");
            }
        });
    }
    for (auto& th : ts) th.join();
    std::printf("  logx: %d threads x 500 events emitted\n", kThreads);
}

}  // 命名空间结束。

int main() {
    std::printf("=== real_subsystem_race_test (run me under TSan) ===\n");
    teardown_race();
    snapshot_real();
    logx_race();
    if (g_failures) {
        std::printf("FAILED (%d)\n", g_failures);
        return 1;
    }
    std::printf("PASS (no races)\n");
    return 0;
}
