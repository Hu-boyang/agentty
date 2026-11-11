// SPDX-License-Identifier: Apache-2.0
//
// teardown_registration_test.cpp——每个线性子系统都登记
// 自己的加入.
//
// 该 BUG CLASS. main() 以手结束 -written 加入
// 列表 —— 被人们记住的每个子系统有一行线被线化。 该列表是错误的
// 形状: 它生活在远离 线程 问题在于,它只有在下一个子系统被添加之前
// 才是正确的,而错误是沉默的。它已经错误过一次 : settings_cache
// 已运出a shutdown() 记录为 "called during teardown"
// 没有 调用 网站曾经呼叫,所以它的 工作线程 正在连接到
// 函数中 -local 静态和~ std::thread 退出
// 时终止了进程。
//
// util::teardown 以反转方式存在:子系统在开始时登记
// 自己的加入 线程 ,和 main() 调用 run() 不知是
// 谁注册的 注册表 之后又增加了一个子系统,但收养工作却停滞不前,
// 另外两个子系统仍在其中。 main() - 失去确切安排
// settings_cache.
//
// 此测试会给反转针插上针头。 它会启动每个线
// 程子系统, 并声明 注册表 成长了,
// 所以 新 子系统,生成 a 线程 而不是六个月后
// 在出口吊销

#include "agentty/util/teardown.hpp"
#include "agtest_cap.hpp"
#include "agentty/util/modelsdev.hpp"
#include "agentty/io/blob_gc.hpp"

#include <cstdio>

namespace {
int g_failures = 0;
void expect(bool ok, const char* what) {
    std::printf("  %-52s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++g_failures;
}
}  // 命名空间结束。

int teardown_registration_test_main() {
    std::printf("=== teardown_registration_test ===\n");
    namespace td = agentty::util::teardown;

    // 基线:没有仅登记 链接.
    const std::size_t base = td::pending();

    // 每个都产下一个背景 线程。每个人都必须登记自己的
    // 加入为 副作用 开始 - 这就是正在测试的财产。
    agentty::modelsdev::start_background_refresh(/*no_net=*/true);
    const std::size_t after_modelsdev = td::pending();
    expect(after_modelsdev > base,
           "modelsdev.start_background_refresh registers its join");

    agentty::blobs::start_background_gc();
    const std::size_t after_blobgc = td::pending();
    expect(after_blobgc > after_modelsdev,
           "blobs.start_background_gc registers its join");

    // 保释金出局 当前 如果缺少注册,则测试将持续到 td::run
    // () 将耗尽 注册表 从未得知未注册者 工作线程 -
    // 留下一个可加载体 std::thread 退出时销毁,
    // 并终止进程
    // "terminate called without an active exception" ,（没有活动异常时调用了终止。）
    // 吞下报告。 IS 复制错误,所以复制它是正确的;
    // 打印哪个子系统首先导致它, 使测试有用,
    // 而不是仅仅红色。
    if (g_failures) {
        std::printf("\nFAILED (%d): a threaded subsystem did not register its\n"
                    "join with util::teardown. main() cannot join what it is\n"
                    "never told about, so the worker is destroyed joinable at\n"
                    "exit and std::terminate fires. Register in the function\n"
                    "that starts the thread, not in main().\n", g_failures);
        return 1;
    }

    // 两次登记不得两次: 注册表 是一个列表,和一个子系统,
    // 以重排 -registers 时值 调用 将不受约束地
    // 生长(并退出时加入N次)。
    agentty::modelsdev::start_background_refresh(true);
    agentty::blobs::start_background_gc();
    expect(td::pending() == after_blobgc,
           "re-starting a subsystem does not re-register");

    // run() 一定要把水耗尽 注册表 加入每个 工作线程。
    // 如果加入的话 死锁 或 单 回调 触摸释放 内存 ,这里是它出现的
    // 地方 -- 而在TSan之下,任何 竞态 介于 工作线程 和它自己的
    // 加入。
    td::run(agtest::cap());
    expect(td::pending() == 0, "teardown::run() drains the registry");

    // 幂等 : 数据 TUI 路径 调用 run() 明确 和
    // 护目镜 调用 出门时又来了
    td::run(agtest::cap());
    expect(td::pending() == 0, "teardown::run() is idempotent");

    if (g_failures) {
        std::printf("FAILED (%d)\n", g_failures);
        return 1;
    }
    std::printf("PASS\n");
    return 0;
}
