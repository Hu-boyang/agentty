// SPDX-License-Identifier: Apache-2.0
//
// teardown_test.cpp ——过程 -exit 关闭 注册表.
//
// 为什么存在这个: main() 用于排放 "join every background thread"
// 手牵着一只手 -written 列表,位于函数底部。 settings_cache
// 写一个停止函数, 将其记录为 "called during teardown" ,
// 没有 调用 被添加到这个名字中, 工作线程 在一个函数内可连接的坐落 -local
// 静态和~ std::thread 调用 std::terminate 出口(核实:
// 探测器以出口代码134中止)。
//
// 一只手,你来帮忙 -maintained 列表无法抓取; IS 手牵手
// -maintained 列表中已忽略。子系统在获取时进行注册。 线程 ,
// 和 main() 排水,排水, 水,水,水 注册表 由护目镜,
// 所以每次早归(~20) CLI 子命令)也覆盖。

#include "agentty/util/teardown.hpp"
#include "agtest.hpp"

#include <doctest/doctest.h>

#include <atomic>
#include <stdexcept>
#include <vector>

namespace teardown = agentty::util::teardown;

TEST_CASE("teardown: actions run LIFO and the registry drains") {
    teardown::run(agtest::cap());                       // 从干净的注册表开始

    std::vector<int> order;
    teardown::on_shutdown("first",  [&](const maya::unlocked&) { order.push_back(1); });
    teardown::on_shutdown("second", [&](const maya::unlocked&) { order.push_back(2); });
    teardown::on_shutdown("third",  [&](const maya::unlocked&) { order.push_back(3); });

    CHECK(teardown::pending() == 3);
    teardown::run(agtest::cap());

    // LIFO,像静态破坏:一个后来注册的子系统可能依赖于
    // 早期的子系统,所以它首先会撕裂.
    REQUIRE(order.size() == 3);
    CHECK(order[0] == 3);
    CHECK(order[1] == 2);
    CHECK(order[2] == 1);

    // 排水量——第二次排水是拒绝 -op 而不是双倍 -join.
    CHECK(teardown::pending() == 0);
    teardown::run(agtest::cap());
    CHECK(order.size() == 3);
}

TEST_CASE("teardown: a throwing action does not stop the others") {
    // 拆卸时的投球比它报告的失败还要糟糕 所以 run()
    // 吞下去,继续下一步行动
    teardown::run(agtest::cap());

    std::atomic<int> ran{0};
    teardown::on_shutdown("ok_after",  [&](const maya::unlocked&) { ran.fetch_add(1); });
    teardown::on_shutdown("thrower",   [](const maya::unlocked&)  { throw std::runtime_error("nope"); });
    teardown::on_shutdown("ok_before", [&](const maya::unlocked&) { ran.fetch_add(1); });

    teardown::run(agtest::cap());
    CHECK(ran.load() == 2);
    CHECK(teardown::pending() == 0);
}

TEST_CASE("teardown: cancel removes a registration") {
    // 拥有者 生命周期 短于进程必须能够退出,
    // 或者 注册表 将持有 回调 变成自由 内存 —— 交易
    // 此终止 注册表 用于预防 -after-free 出口处
    teardown::run(agtest::cap());

    std::atomic<int> ran{0};
    const auto token = teardown::on_shutdown("scoped", [&](const maya::unlocked&) { ran.fetch_add(1); });
    teardown::on_shutdown("kept", [&](const maya::unlocked&) { ran.fetch_add(1); });
    CHECK(teardown::pending() == 2);

    teardown::cancel(token);
    CHECK(teardown::pending() == 1);

    teardown::run(agtest::cap());
    CHECK(ran.load() == 1);          // 只有那个被开除的

    // 幂等 安全地用一个已经行动起来的标志
    teardown::cancel(token);
    teardown::cancel(0);
}
