// 是这样的 沙箱 配置从多处读取的安全性 线程 安装时,它又会安装吗?
//
// 此文件的原题已不存在,原因值得保留. 配置以前是可重印的:
// SandboxSave从状态更新函数XX线程上写下新政策,
// 而工具在task_isolated工作线程上读,
// 在struct上持有三个向量的`g_cfg = cfg`上读是读者行走
// 一个自由的缓冲区. 竞态是真实的,TSanlaged it.
//
// 现在不可能通过建筑而不是仔细出版:
// set_config ( ) 系 , ( ) SEALED 第一次
// 之后 调用 ,所以此策略在启动时写一次,而再也不写一次。
// 一个边界不得在运行过程中移动 -- -- 见 set_config 说明
// 为什么这是安全财产 不仅仅是线性方便
//
// 所以,剩下的检查范围是更窄的,但仍然值得一试:
//
//   1. 封条 HOLDS 受同时压力(许多) 线程 赛车到
//      安装; 准确的1胜, 和胜利者的政策是连贯的)
//   2. 读者采取 快照 总是看到一个整体政策,从来没有
//      半-written 1 3,6,6,7,7,6,8,7,8,6,9,7,9,6,5,7,5,6,3,7,3,610,7,4,7,2,7,1,7,10,710,716,7,11,7,12,7,0,7,
//   3. 持有的 快照 整个保留有效且不变 生命周期
//
// 运行在TSan(这是在消毒器车道);下面的检查也会在没有它的情况
// 下抓一个撕破的读物.

#include <doctest/doctest.h>

#include "agentty/tool/util/sandbox.hpp"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace sb  = agentty::tools::util::sandbox;
namespace cfgn = agentty::sandbox_cfg;

TEST_CASE("sandbox config: the seal holds under a concurrent install storm") {
    // 多个 线程 尝试同时安装不同的政策。封条是单一的交换,
    // 所以,一个完全必须赢,而且关键是,赢家的政策必须是:
    // 整个 半个 -applied 胜者会是旧的重写版的
    // 同样的窃听器,只是启动时而不是中场 -session.
    sb::reset_config_for_test();

    constexpr int kThreads = 8;
    std::vector<cfgn::Config> candidates;
    for (int i = 0; i < kThreads; ++i) {
        cfgn::Config c;
        c.configured = true;
        // 每个字段键掉 I,所以混合的结果是可察觉的:
        // 一个连贯的 快照 包含来自 一个 候选人。
        c.memory_mb  = static_cast<std::uint64_t>(1000 + i);
        c.max_procs  = static_cast<std::uint32_t>(i);
        c.read_paths = {"/cand/" + std::to_string(i)};
        candidates.push_back(std::move(c));
    }

    std::atomic<int> ready{0};
    std::vector<std::thread> installers;
    for (int i = 0; i < kThreads; ++i) {
        installers.emplace_back([&, i] {
            ready.fetch_add(1, std::memory_order_relaxed);
            while (ready.load(std::memory_order_relaxed) < kThreads) {}
            sb::set_config(candidates[static_cast<std::size_t>(i)]);
        });
    }
    for (auto& t : installers) t.join();

    CHECK(sb::config_sealed());

    // 不管谁赢了,结果就是一个候选人,而不是一个混合。
    const auto got = sb::config_snapshot();
    REQUIRE(got != nullptr);
    const int idx = static_cast<int>(got->memory_mb) - 1000;
    REQUIRE(idx >= 0);
    REQUIRE(idx < kThreads);
    const auto& want = candidates[static_cast<std::size_t>(idx)];
    CHECK(got->max_procs  == want.max_procs);
    CHECK(got->read_paths == want.read_paths);
}

TEST_CASE("sandbox config: concurrent readers always see one whole policy") {
    // 产卵路径读取了十几块田地 快照。在封印下,
    // 他们不会在进行时重写配置,所以现在这是一张检查,
    // 即读路径本身是健全的——如果有人再次使政策
    // 可以重写,那么情况就会最响亮地失败。
    sb::reset_config_for_test();

    cfgn::Config c;
    c.configured = true;
    c.memory_mb  = 4096;
    c.net_mode   = cfgn::NetMode::None;
    c.read_paths = {"/one", "/two", "/three"};
    sb::set_config(c);

    std::atomic<bool> stop{false};
    std::atomic<int>  torn{0};
    std::atomic<long> reads{0};

    std::vector<std::thread> readers;
    for (int i = 0; i < 4; ++i) {
        readers.emplace_back([&] {
            while (!stop.load(std::memory_order_relaxed)) {
                const auto snap = sb::config_snapshot();
                if (!snap) { torn.fetch_add(1); continue; }

                if (snap->memory_mb != 4096 ||
                    snap->net_mode != cfgn::NetMode::None ||
                    snap->read_paths.size() != 3) {
                    torn.fetch_add(1);
                    continue;
                }
                // 触摸那些堆积如山的成员,所以一个自由的缓冲区
                // 实际上被贬低,而不是仅仅被持有.
                std::size_t n = 0;
                for (const auto& s : snap->read_paths) n += s.size();
                if (n == 0) torn.fetch_add(1);

                reads.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // 临Τㄇ狥「 TRYING 来改变策略,
    // 设置的方式 状态更新函数。必须拒绝所有这些,因此,
    // 上面的读者绝不能观察到不同的价值。
    std::thread writer([&] {
        cfgn::Config other;
        other.configured = true;
        other.memory_mb  = 1;
        other.net_mode   = cfgn::NetMode::Full;
        other.read_paths = {"/evil"};
        for (int i = 0; i < 4000; ++i) sb::set_config(other);
        stop.store(true, std::memory_order_relaxed);
    });

    writer.join();
    for (auto& t : readers) t.join();

    CHECK(torn.load() == 0);
    CHECK(reads.load() > 0);
    // 而拒绝的确是拒绝的。
    CHECK(sb::config().memory_mb == 4096);
    CHECK(sb::config().net_mode == cfgn::NetMode::None);
}

TEST_CASE("sandbox config: a reader's snapshot stays valid and unchanged") {
    // 命令必须运行在边界内 STARTED 封印下,
    // 对于整个过程来说,这微不足道,但是 快照 合同是
    // 即使通过重置(只有测试才能做到)也能够实现的。
    sb::reset_config_for_test();

    cfgn::Config first;
    first.configured = true;
    first.memory_mb  = 777;
    first.read_paths = {"/first/path"};
    sb::set_config(first);

    const auto held = sb::config_snapshot();
    REQUIRE(held != nullptr);

    // 赖因斯托尔反复(每次破解封条,只有测试才能
    // 破解)并放下旧值.
    for (int i = 0; i < 64; ++i) {
        cfgn::Config next;
        next.configured = true;
        next.memory_mb  = static_cast<std::uint64_t>(i);
        next.read_paths = {"/other/" + std::to_string(i)};
        sb::reset_config_for_test();
        sb::set_config(next);
    }

    // 持有的快照没有变化,其成员数量仍然很多。
    CHECK(held->memory_mb == 777);
    REQUIRE(held->read_paths.size() == 1);
    CHECK(held->read_paths[0] == "/first/path");

    // 活值确实在继续 所以这不是偶然的
    CHECK(sb::config_snapshot()->memory_mb != 777);
}

TEST_CASE("sandbox config: snapshot is never null, even untouched") {
    // 调用者不检查就调用, 因此默认必须是真实的
    // 配置而不是虚无。 默认配置是正确的答案: 它
    // 正是 brap 路径所构建的姿态。
    sb::reset_config_for_test();
    const auto snap = sb::config_snapshot();
    REQUIRE(snap != nullptr);
    // 读它不能陷害。
    (void)snap->net_mode;
    (void)snap->read_paths.size();
    // 一种未变的配置也不是"configured",
    // 这就是阻止升级应用无人设定的政策的原因。
    CHECK(!snap->configured);
}
