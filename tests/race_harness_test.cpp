// SPDX-License-Identifier: Apache-2.0
//
// race_harness_test.cpp - 电话 THREAD-SANITIZER 车道。
//
// 此文件为何存在 : CI 运行 ASan + UBSan,
// 二者之间可捕获使用 -after-free ,漏泄,和 UB - 但是,我说,
// 不 数据 竞态。每个货币错误 代码库 实际运出 的对子看不见 :
//
//   • maya的BackgroundQueue被自己的工作线程(a)摧毁
//     shared_ptr 任务周期 载荷 ——自我 -join —— 中止。
//   • settings_cache's 工作线程 函数中左侧可合并 -local 静态 □
//     ~std::thread 出口时终止。
//   • 国家 `@` 选择面板 产卵a 线程 每按键,N级赛车
//     发布为一 git-status 地图, 最后一个作者任意获胜。
//
// 前两枚由手-written探测器发现,第三枚通过读取.
// 这不是一个进程。 此文件是进程: 它将每个共享的 接口边界 一次
// 驱动于许多 线程 , 这样 TSan 可以看到离合, 它被注册在
// `sanitizer` 标签下, 这样 CI 运行它。
//
// 这些案件是故意的 SHAPED 就像上面的虫子一样...
// publish/consume 竞态 ,拆卸 -with-backlog ,
// 同时读取器 快照 而不是作为一般的烟雾测试。

#include "agentty/util/snapshot.hpp"
#include "agentty/domain/catalog.hpp"   // 真实 目录 注册表( 头文件-only)
#include "agtest_cap.hpp"
#include "agentty/util/teardown.hpp"

#include <atomic>
#include <barrier>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using agentty::util::Snapshot;
using agentty::util::make_snapshot;
namespace teardown = agentty::util::teardown;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL: %s\n", what); ++failures; }
}

// 线程 这一切从头开始 看到的离别远多于
// 线程 每一个下方的病例先在屏障上汇合
constexpr int kThreads = 8;
constexpr int kIters   = 2000;

// ── 1. AtomicSnapshot:读者与重排作家 ─────────────────
//
// 该 使用情况 -after-free 形状。 list_workspace_symbols () 用来发放
// const-ref to a buffer whose owning shared_ptr had already died; readers
// 然后,我走了出来, 内存 而在背景行走中重新发表。
//
// 注意此驱动器 AtomicSnapshot ,没有赤裸的 快照。
// 赤裸裸的 快照 已经 shared_ptr 语义学——复制 HANDLE
// 时另一个 线程 指定到同一控件是一个纯数据 竞态 指针上,TSan正确报告
// (这就是如何找到这种区别)。 AtomicSnapshot 是一个
// producer/consumer 插槽,这是什么 缓存 使用时使用。
void snapshot_publish_race() {
    agentty::util::AtomicSnapshot<std::vector<std::string>> shared;
    std::atomic<bool> stop{false};
    std::atomic<long> reads{0};

    // 制片人:不断重排.
    std::thread producer([&] {
        for (int gen = 0; !stop.load(std::memory_order_relaxed); ++gen) {
            std::vector<std::string> v;
            v.reserve(64);
            for (int i = 0; i < 64; ++i)
                v.push_back("gen" + std::to_string(gen) + "_" + std::to_string(i));
            shared.store(std::move(v));
        }
    });

    // 读者:拿把把手,然后走。即使制片人正在
    // 下面换牌,走的必须对整个曲面有效。
    std::barrier sync(kThreads);
    std::vector<std::thread> readers;
    for (int t = 0; t < kThreads; ++t) {
        readers.emplace_back([&] {
            sync.arrive_and_wait();
            for (int i = 0; i < kIters; ++i) {
                const auto local = shared.load();   // 一个原子重计的突起
                std::size_t n = 0;
                for (const auto& s : local) n += s.size();
                // 要么没有出版(空),要么整整一代人——从来没有半个
                // -swapped 缓冲区.
                check(local.empty() || local.size() == 64,
                      "snapshot reader saw a torn buffer");
                reads.fetch_add(static_cast<long>(n != 0), std::memory_order_relaxed);
            }
        });
    }
    for (auto& r : readers) r.join();
    stop.store(true);
    producer.join();
    std::printf("  snapshot publish/consume: %ld non-empty reads\n", reads.load());
}

// ── 2. 快照:每一次重排时,手柄都会超过寿命 ─────────────────────
//
// 直接检查错误:握一个读取手柄,让制作人多次互换,
// 并断言 ORIGINAL 内容仍然完整且可读。 在旧的 Cont
// 下 -ref 合约,这是解脱的读物 内存.
void snapshot_handle_outlives_producer() {
    auto held = make_snapshot(std::vector<std::string>{"alpha", "beta", "gamma"});

    agentty::util::AtomicSnapshot<std::vector<std::string>> latest;
    latest.store(held);
    std::thread churn([&] {
        for (int i = 0; i < 500; ++i)
            latest.store(std::vector<std::string>(32, "x" + std::to_string(i)));
    });
    churn.join();

    check(held.size() == 3,        "held snapshot changed size");
    check(held[0] == "alpha",      "held snapshot corrupted (0)");
    check(held[2] == "gamma",      "held snapshot corrupted (2)");
}

// ── 3. 拆卸注册表:同时登记+排水量 ────────────────
//
// 电话 注册表 由任意一个 线程 首先启动子系统
// 工作线程 并排出从 main() 瞄准镜,
// 所以注册 竞态 建筑排水
void teardown_registration_race() {
    teardown::run(agtest::cap());                       // 清理列表

    std::atomic<int> fired{0};
    std::barrier sync(kThreads);
    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t) {
        ts.emplace_back([&] {
            sync.arrive_and_wait();
            for (int i = 0; i < 200; ++i) {
                const auto tok = teardown::on_shutdown(
                    "racer", [&](const maya::unlocked&) { fired.fetch_add(1, std::memory_order_relaxed); });
                // 一半退缩,一半退缩 排水 cancel/run
                // 将这段话与范围化的登记员的点击进行交叉。
                if (i % 2 == 0) teardown::cancel(tok);
            }
        });
    }
    // 与上述登记同时排水。
    for (int i = 0; i < 50; ++i) {
        teardown::run(agtest::cap());
        std::this_thread::yield();
    }
    for (auto& t : ts) t.join();
    teardown::run(agtest::cap());                       // 最后排水量

    check(teardown::pending() == 0, "teardown registry not drained");
    std::printf("  teardown registry: %d actions fired\n", fired.load());
}

// ── 4. Write-behind 保存 队列: 队列 竞态 排水量 竞态 停止( E) ───────────
//
// persistence::AsyncWriter和settings_cache的工作线程的形状:
// 由互斥量守护的合力图,condvar-driven排水量线程,
// 以及不得丢掉最后的写法的停站。 settings_cache
// 的流产是在那里生活的(一个工作线程留下了静态的连接),
// 所以值得倾注的不变量是:每个不同的钥匙最终会落地,
// 一个钥匙的更新值取代一个排队的旧钥匙,停止排水量而不是截流。
//
// 此处建模而不是直接通过persistence.cpp驱动,
// 因为真者会向磁盘写;货币在这个结构中,
// 而不是在fsync中.
void write_behind_queue_race() {
    struct Writer {
        std::mutex                                 mu;
        std::condition_variable                    cv;
        std::unordered_map<std::string, int>       pending;
        bool                                       stopping = false;
        std::thread                                worker;
        std::unordered_map<std::string, int>       written;
        std::mutex                                 written_mu;

        void enqueue(std::string id, int v) {
            {
                std::lock_guard lk(mu);
                // 更新的快照取代了一个仍在为同一id排队的
                // 老的 ID —— 连接真实的编剧依赖于在
                // 爆破下将队列绑定.
                pending.insert_or_assign(std::move(id), v);
                if (!worker.joinable()) worker = std::thread([this] { run(); });
            }
            cv.notify_one();
        }

        void flush_and_stop() {
            std::thread to_join;
            {
                std::lock_guard lk(mu);
                stopping = true;
                if (worker.joinable()) to_join = std::move(worker);
            }
            cv.notify_one();
            if (to_join.joinable()) to_join.join();
        }

        void run() {
            for (;;) {
                std::string key; int val;
                {
                    std::unique_lock lk(mu);
                    cv.wait(lk, [this] { return !pending.empty() || stopping; });
                    // 排水沟 -then-stop : 继续弹出,
                    // 即使停止了,所以最后 快照 总是降落。
                    if (pending.empty()) {
                        if (stopping) return;
                        continue;
                    }
                    auto it = pending.begin();
                    key = it->first;
                    val = it->second;
                    pending.erase(it);
                }
                // 外面的 锁 就像真正的FSync一样
                std::lock_guard wl(written_mu);
                written[key] = val;
            }
        }
    };

    Writer w;
    std::barrier sync(kThreads);
    std::vector<std::thread> producers;
    for (int t = 0; t < kThreads; ++t) {
        producers.emplace_back([&, t] {
            sync.arrive_and_wait();
            for (int i = 0; i < 400; ++i)
                w.enqueue("thread" + std::to_string(t), i);
        });
    }
    for (auto& p : producers) p.join();
    w.flush_and_stop();

    // 每个生产商的LAST值必须已经着陆: 聚合
    // 可能降中状态(即点), 但永远不要降中状态,
    // 停止则不能截断队列。
    std::lock_guard wl(w.written_mu);
    check(w.written.size() == static_cast<std::size_t>(kThreads),
          "write-behind dropped an entire key");
    for (int t = 0; t < kThreads; ++t) {
        const auto it = w.written.find("thread" + std::to_string(t));
        check(it != w.written.end() && it->second == 399,
              "write-behind lost the newest value for a key");
    }
    std::printf("  write-behind queue: %zu keys landed\n", w.written.size());
}

// ── 5. 连接 池 : 同时进行 acquire/release ─────────────────────
//
// http::Pool '形状: 端点地图 ~ 堆叠闲置连接,
// 与 acquire() 弹出(和丢弃) 过期 条目)
// release() 逐个要求,逐个逐个逐一个 线程 摸着它,
// 所以 不变量 关键是 所有权 : 连接由 恰好 一个 一次开派对
// 双-hand-out将意味着两个请求写一个套接字。
//
// 以实际结构为模型( unique_ptr 条目,按 -endpoint 堆叠,
// 拆迁在盖子上) 因为真正的人需要活 TLS 端点拨号.
void connection_pool_race() {
    constexpr std::size_t kPoolCap = 16;
    struct Conn { int id; std::atomic<bool> in_use{false}; };

    struct Pool {
        std::mutex mu;
        std::unordered_map<int, std::vector<std::unique_ptr<Conn>>> map;
        std::size_t cap = 16;

        std::unique_ptr<Conn> acquire(int ep) {
            std::lock_guard lk(mu);
            auto it = map.find(ep);
            if (it == map.end() || it->second.empty()) return nullptr;
            auto p = std::move(it->second.back());
            it->second.pop_back();
            return p;
        }

        void release(std::unique_ptr<Conn> c) {
            if (!c) return;
            std::lock_guard lk(mu);
            auto& stack = map[c->id % 4];
            if (stack.size() >= cap) stack.erase(stack.begin());
            stack.push_back(std::move(c));
        }
    };

    Pool pool;
    pool.cap = kPoolCap;
    std::atomic<int> double_use{0};
    std::atomic<long> acquires{0};

    // 种子数端.
    for (int i = 0; i < 32; ++i)
        pool.release(std::make_unique<Conn>(i));

    std::barrier sync(kThreads);
    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t) {
        ts.emplace_back([&, t] {
            sync.arrive_and_wait();
            for (int i = 0; i < kIters; ++i) {
                const int ep = (t + i) % 4;
                if (auto c = pool.acquire(ep)) {
                    // 如果 池 曾经将同样的连接 给两个 线
                    // 程 这个 标志 已经设置。
                    if (c->in_use.exchange(true, std::memory_order_acq_rel))
                        double_use.fetch_add(1, std::memory_order_relaxed);
                    acquires.fetch_add(1, std::memory_order_relaxed);
                    c->in_use.store(false, std::memory_order_release);
                    pool.release(std::move(c));
                } else {
                    pool.release(std::make_unique<Conn>(i));
                }
            }
        });
    }
    for (auto& t : ts) t.join();

    check(double_use.load() == 0, "pool handed one connection to two threads");
    std::printf("  connection pool: %ld acquires, %d double-uses\n",
                acquires.load(), double_use.load());
}

} // 命名空间结束。

// ── 6. 目录:散装装赛车阅读器和单装装置 ────────────
//
// 电话 真实 注册表( catalog.hpp 谓 头文件-only).
// A models.dev-sized 批量执行,而阅读器则在 渲染 路径和
// 活 -catalog 线程 写出单一的事实。每个公布的事实必须完整可读,
// 批量必须全部着陆。 -or-nothing 读者不会忘记任何文字。
namespace {
void catalog_batch_race() {
    using namespace agentty;
    std::atomic<bool> stop{false};
    std::atomic<long> reads{0}, torn{0};
    std::barrier start(kThreads + 2);

    std::thread batcher([&] {
        start.arrive_and_wait();
        for (int round = 0; round < 20; ++round) {
            CatalogBatch b;
            for (int i = 0; i < 2000; ++i) {
                const std::string id = "racebatch/m" + std::to_string(i);
                b.set_reasoning(id, true);
                b.set_context_window(id, 1000 + i);
            }
            // 同一批中的哨兵:如果读者看到,
            // 它也必须看到该批中其他每一个事实.
            b.set_reasoning("racebatch/sentinel", true);
            b.commit();
        }
    });
    std::thread single([&] {
        start.arrive_and_wait();
        for (int i = 0; i < kIters; ++i)
            set_catalog_reasoning("racesingle/m" + std::to_string(i % 50), (i & 1) != 0);
    });
    std::vector<std::thread> readers;
    for (int t = 0; t < kThreads; ++t)
        readers.emplace_back([&] {
            start.arrive_and_wait();
            while (!stop.load(std::memory_order_relaxed)) {
                if (catalog_reasoning_for("sentinel", "racebatch") == 1
                    && catalog_context_window_for("m1999", "racebatch") != 2999)
                    torn.fetch_add(1);
                (void)catalog_reasoning_for("m7", "racesingle");
                reads.fetch_add(1, std::memory_order_relaxed);
            }
        });
    batcher.join();
    single.join();
    stop = true;
    for (auto& r : readers) r.join();

    check(torn.load() == 0, "a reader saw half a catalog batch");
    check(catalog_context_window_for("m0", "racebatch") == 1000, "batch fact lost");
    check(catalog_reasoning_for("m49", "racesingle") != -1, "single-setter fact lost");
    std::printf("  %ld reads, %ld torn\n", reads.load(), torn.load());
}
} // 命名空间结束。

int main() {
    std::printf("=== race_harness_test (run me under TSan) ===\n");

    std::printf("--- snapshot publish/consume race ---\n");
    snapshot_publish_race();

    std::printf("--- snapshot handle outlives producer ---\n");
    snapshot_handle_outlives_producer();

    std::printf("--- teardown registration race ---\n");
    teardown_registration_race();

    std::printf("--- write-behind save queue race ---\n");
    write_behind_queue_race();

    std::printf("--- connection pool acquire/release race ---\n");
    connection_pool_race();

    std::printf("--- catalog batch commit vs readers and single setters ---\n");
    catalog_batch_race();

    if (failures == 0) {
        std::printf("PASS (no races, no torn reads)\n");
        return 0;
    }
    std::printf("FAILED: %d check(s)\n", failures);
    return 1;
}
