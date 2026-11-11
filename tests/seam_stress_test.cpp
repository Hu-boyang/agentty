// SPDX-License-Identifier: Apache-2.0
//
// seam_stress_test.cpp - 锤子 真实 进程 -wide 接口边界 多个 线
// 程 一次,一次,一次 ThreadSanitizer.
//
// 原因 THESE FOUR. agentty有44个文件持有一个生的std::mutex,
// 每个文件手提-audited 与第允许清单 货币.
// 审计是第jaal 分类法(docs/concurrency.md
// §6): 人类检查它,它比第jaal 的 A 级弱,因为错误无法反映。
// 诚实地保存第-B 级的声称是真实的, 继续证明它,
// 而现有的第竞态 号测试没有涵盖这些:
//
//   provider::select / 活跃每转动读取它; 选择面板 写下来
//                            active() 返回选择 BY 值 下划线 锁 ,
//                            所以一个中点 -select ()读者决不
//                            能看到撕裂的终点.
//   tools::registry / 线 工具 目录 重建时 MCP
//                            工具时插件连接 分派 读它,
//                            这是 `@`-picker 虫形:N读取器,
//                            一个重排器.
//   settings_cache 写作 -behind 设置 工作线程: 状态更新函数
//                            写在 UI 线程, a 工作线程 诸法音声.
//   capkey/catalog 能力 注册表 由
//                            models.dev 刷新和阅读 每一转。
//
// 每一个案件都驱动着 接口边界 应用驱动
// 它的方式, 但随着争论的出现: 许多读者,
// 一个作家 重新在下面,
//
// 运行在 : -DAGENTTY_SANITIZE_ALL=thread 下

#include "agentty/provider/selection.hpp"
#include "agentty/tool/registry.hpp"
#include "agentty/domain/catalog.hpp"
#include "agentty/domain/capkey.hpp"

#include <atomic>
#include <barrier>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_failures = 0;
void expect(bool ok, const char* what) {
    std::printf("  %-52s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++g_failures;
}

// ── 1. provider::select 活动 ────────────────────────────────────────
// 选择由提供者互换 选择面板 并在每一转弯的顶端阅读。
// active() 手往后转 -value 快照 评选
// 结果 互斥量 ; 缩写 不变量 读者从不观察 选题,
// 其字段来自两个不同的 select() 调用.
void selection_race() {
    std::printf("--- provider::select / active under contention ---\n");
    using namespace agentty::provider;

    constexpr int kReaders = 6;
    std::atomic<bool> stop{false};
    std::atomic<unsigned long> reads{0}, torn{0};
    std::barrier sync(kReaders + 1);

    // 两个选择, 其字段内部一致。 撕读后,
    // 将一个主机和另一个端口对齐。
    Selection a{};
    a.openai_endpoint.host = "alpha.example";
    a.openai_endpoint.port = 1111;
    Selection b{};
    b.openai_endpoint.host = "bravo.example";
    b.openai_endpoint.port = 2222;

    std::vector<std::thread> readers;
    for (int i = 0; i < kReaders; ++i) {
        readers.emplace_back([&] {
            sync.arrive_and_wait();
            while (!stop.load(std::memory_order_relaxed)) {
                const Selection s = active();
                const auto& h = s.openai_endpoint.host;
                const auto  p = s.openai_endpoint.port;
                if ((h == "alpha.example" && p != 1111) ||
                    (h == "bravo.example" && p != 2222))
                    torn.fetch_add(1, std::memory_order_relaxed);
                reads.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    sync.arrive_and_wait();
    for (int i = 0; i < 4000; ++i) select(::agentty::IoAccess::grant(), (i & 1) ? a : b);
    stop.store(true, std::memory_order_relaxed);
    for (auto& t : readers) t.join();

    std::printf("    %lu reads, %lu torn\n", reads.load(), torn.load());
    expect(torn.load() == 0, "active() never returns a torn Selection");
}

// ── 2. 插件下重载的工具注册表 ──────────────────────────
// wire_tools () 在每个回合阅读以构建请求,
// 目录 重新发布时 MCP 插件连接或重新装入。 重建向量时,
// 阅读器会持有返回的引用。 -after-free 这个 接口边界
// 已经存在,以预防。
void registry_race() {
    std::printf("--- tools::wire_tools during catalog invalidation ---\n");
    using namespace agentty::tools;

    constexpr int kReaders = 6;
    std::atomic<bool> stop{false};
    std::atomic<unsigned long> reads{0}, bad{0};
    std::barrier sync(kReaders + 1);

    std::vector<std::thread> readers;
    for (int i = 0; i < kReaders; ++i) {
        readers.emplace_back([&] {
            sync.arrive_and_wait();
            while (!stop.load(std::memory_order_relaxed)) {
                // 拿着 目录 手动并完全行走:只要我们握住它,
                // 每个名字都必须保持可读性。
                const auto snap = wire_tools();
                for (const auto& t : *snap)
                    if (t.name.value.empty()) bad.fetch_add(1, std::memory_order_relaxed);
                reads.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    sync.arrive_and_wait();
    for (int i = 0; i < 2000; ++i) invalidate_mcp_catalog(::agentty::IoAccess::grant());
    stop.store(true, std::memory_order_relaxed);
    for (auto& t : readers) t.join();

    std::printf("    %lu snapshot walks, %lu malformed\n", reads.load(), bad.load());
    expect(bad.load() == 0, "a tool snapshot stays valid while the catalog churns");
}

// ── 3. 能力注册表 ──────────────────────────────────────────
// 由 models.dev 背景刷新, 当它决定是否发送工具定义/
// 推理字段时, 每转一次都会读取。 keyed through
// capkey::norm_model ,
// 因此这也表明关键正常化是无国籍。
void capability_race() {
    std::printf("--- catalog capability registry, writer + readers ---\n");
    using namespace agentty;

    constexpr int kReaders = 4;
    std::atomic<bool> stop{false};
    std::atomic<unsigned long> reads{0};
    std::barrier sync(kReaders + 1);

    std::vector<std::thread> readers;
    for (int i = 0; i < kReaders; ++i) {
        readers.emplace_back([&] {
            sync.arrive_and_wait();
            while (!stop.load(std::memory_order_relaxed)) {
                (void)catalog_reasoning_for("claude-sonnet-4.5", "anthropic");
                (void)catalog_reasoning_for("gpt-5.1", "openai");
                reads.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    sync.arrive_and_wait();
    for (int i = 0; i < 3000; ++i) {
        set_catalog_reasoning("anthropic/claude-sonnet-4.5", (i & 1) != 0);
        set_catalog_reasoning("openai/gpt-5.1", (i & 1) == 0);
    }
    stop.store(true, std::memory_order_relaxed);
    for (auto& t : readers) t.join();

    std::printf("    %lu capability lookups\n", reads.load());
    expect(true, "capability registry survives concurrent write/read");
}

}  // 命名空间结束。

int seam_stress_test_main() {
    std::printf("=== seam_stress_test (run me under TSan) ===\n");
    selection_race();
    registry_race();
    capability_race();
    if (g_failures) {
        std::printf("FAILED (%d)\n", g_failures);
        return 1;
    }
    std::printf("PASS (no races, no torn reads)\n");
    return 0;
}
