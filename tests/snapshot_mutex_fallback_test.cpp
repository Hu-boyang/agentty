// AtomicSnapshot 标准 库 无需 std::atomic<shared_ptr<T>>.
//
// libc++ 尚未运出 P0718R2 (截至2026年), 类 Termux/Android
// 声明通过主原子模板,
// 但失败 "_Atomic cannot be applied to type ... which is not trivially copyable".
// snapshot.hpp 保持同样的公开 API 时间: 互斥量 那边
//
// ── 为什么这是个 SEPARATE 二进制程序 说 链接 无 ────────────────────────
//
// MAYA_FORCE_PUBLISHED_MUTEX改变AtomicSnapshot的LAYOUT:
// 一个变体持有std::atomic<shared_ptr>,另一个是互斥量加平原shared_ptr.
// 定义在一个 TU 以及,还 链接 agentty 其对象—— 与
// 原子布局一起编译的物体—— 是其中之一 -definition-rule
// 违反:在同一类中,在一个程序中有两个不同的定义。 链接器 并不抱怨,
// 它只是挑一个,结果悬在静态 -init 时间
//
// 这正是第一次作为折叠测试写出来时发生的情况,需要发布才能找到。
// 所以:没有。 agentty 对象, 没有共享对象集,
// 仅此而已 头文件。如果您想添加一个 链接 依赖这里,
// ODR 问题又来了
//
// 定义来自BUILD(AgenttyTests.cmake),
// 而不是来自本文中的#define,原因有二:归并XX编译每次
// 测试均使用-Dmain=<name>_main,这里写的一个宏会落在重命名
// 的错误一边.

#include "agentty/util/snapshot.hpp"

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace {

int failures = 0;
void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

} // 命名空间结束。

int main() {
    static_assert(MAYA_ATOMIC_SHARED_PTR == 0,
                  "this test must compile the mutex fallback, not the atomic "
                  "path — check MAYA_FORCE_PUBLISHED_MUTEX reached the "
                  "compile (it is set in AgenttyTests.cmake, not in this file)");

    using Snap = agentty::util::AtomicSnapshot<std::vector<std::string>>;

    // 空与已发布可区分 -but-empty。该词 选择面板 渲染 "indexing…"
    // 数字 "workspace is empty" 完全关闭这个。
    Snap s;
    check(!s.has_value(), "a fresh snapshot has no value");
    s.store(std::vector<std::string>{});
    check(s.has_value(), "publishing an empty vector still counts as published");
    check(s.load()->empty(), "…and the payload really is empty");

    s.store(std::vector<std::string>{"a", "b", "c"});
    check(s.load()->size() == 3, "store then load round-trips");

    // 整个类型的属性存在于: 持有一个手柄的阅读器会让自己的一代
    // 在重排时存活。 如果中断, 呼叫者会得到一个细长的视图 —
    // 被曝光的shared_ptr 邀请的bug。
    auto held = s.load();
    s.store(std::vector<std::string>{"x"});
    check(held->size() == 3, "an old handle is unaffected by a new store");
    check(s.load()->size() == 1, "a fresh load sees the new generation");

    // 读者和作家,同时 互斥量 回退 危险是撕裂的重新计数,
    // 它显示为坠机或漏水,而不是错误的价值——因此它
    // 持续了足够长的时间来击中它.
    {
        Snap hot;
        hot.store(std::vector<std::string>{"seed"});
        std::atomic<bool> stop{false};
        std::atomic<long> reads{0};
        // 读者自己宣布。 如果没有这个断言, 下面是排程器上的
        // 赌注: 编剧可以完成所有5000个商店和设置
        // `stop` 阅读器运行之前, 留下读数 \\ 和失败的测试,
        // 发现没有错误。 它独自通过, 失败了 \ 。
        // CPT -j12 ,每次签名。
        std::atomic<int> ready{0};
        constexpr int kReaders = 4;
        std::vector<std::thread> readers;
        for (int i = 0; i < kReaders; ++i) {
            readers.emplace_back([&] {
                // 一个 加载 之前 宣布,所以, "ready" 意思是
                // 这个 线程 已经行使了时间档,而不仅仅是开始。
                if (auto h = hot.load())
                    reads.fetch_add(static_cast<long>(h->size()),
                                    std::memory_order_relaxed);
                ready.fetch_add(1, std::memory_order_release);
                while (!stop.load(std::memory_order_relaxed)) {
                    auto h = hot.load();
                    if (h) reads.fetch_add(static_cast<long>(h->size()),
                                           std::memory_order_relaxed);
                }
            });
        }
        // 每个读者都读过一次,所以下面的风暴真正地与现场
        // 读者重叠,而不是赛跑他们的启动.
        while (ready.load(std::memory_order_acquire) < kReaders)
            std::this_thread::yield();

        for (int i = 0; i < 5000; ++i)
            hot.store(std::vector<std::string>(static_cast<std::size_t>(i % 8) + 1, "v"));
        stop.store(true);
        for (auto& t : readers) t.join();
        check(reads.load() >= kReaders,
              "concurrent readers saw published generations");
        check(hot.has_value(), "the slot survives a write storm");
    }

    s.reset();
    check(!s.has_value(), "reset clears the published flag");
    check(held->size() == 3, "reset does not disturb a handle taken earlier");

    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
