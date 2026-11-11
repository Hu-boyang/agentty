// subagent_lifetime_test ——提供者 接口边界 必须活下来 ABANDONED 工作线程.
//
// 该 BUG 此 PINS。漫长 -lived 提供者( Anthropic,
// ChatGPT , 副驾驶, Kimi) 曾经是堆栈对象 main() ,
// 并显示 数据流 接口边界 理由是: "main outlives maya::run" ,
// 这对每个 线程 编号 运行时 等诸人.
//
// 副剂就是 线程 等不及了,A,我等不及了 `task` 运行在 a
// Cmd::task_isolated 线程 ,和 jaal DETACHES
// 这些——其内核 池 头文件 说孤立 线程 已经
// 请求了停止，却从未等待任务结束。
// 关闭现在问( run_tool 读取其 stop_token 等待,但等待是,
// BOUNDED 目的:a 线程 放弃是设计的结果,而放弃是必然的结果。
// -reference 抓取一个废弃 工作线程 调用为自由堆栈 内存 却又无法受伤
//
// 只有在副剂正好在中场时才有问题 -stream 退出时,
// 用户会放弃慢速的, 按下逃生键。 所以它读作
// "subagents crash sometimes".
//
// 固定时间为所有权,而非时间:接口边界持有shared_ptrs,
// 所以最后的用户退出会摧毁提供者,一个废弃的工作线程只要它
// 还在触摸它,就让它活着,没有宽限期调谐,什么都没有到竞
// 态.
//
// 运行在 ASan( 这是 `sanitizer`-labelled 车道)
// 之前 形状会是一个硬堆 -use-after-scope 而不是一场概率性的坠机
// 已验证: 由 -reference 此相同的套管版本在此中止
// "AddressSanitizer: stack-use-after-scope ... WRITE of size 4 thread T1".（AddressSanitizer 报告栈变量作用域结束后，线程 T1 仍写入 4 字节。）

#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <thread>

namespace {

int g_checks = 0;
int g_fails  = 0;

void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_fails; std::printf("FAIL: %s\n", what); }
    else       std::printf("ok:   %s\n", what);
}

// 提供方的站牌: 已声明 数据流 读写都一样 并且一个破坏者
// 毒害它 如此的用途 -after-free 即便没有消毒剂,
// 也能够检测到。
struct FakeProvider {
    static constexpr int kCanary = 0x5A5A5A;
    int canary = kCanary;
    std::atomic<long long> calls{0};

    bool stream() {
        calls.fetch_add(1, std::memory_order_relaxed);
        return canary == kCanary;
    }
    ~FakeProvider() { canary = 0; }
};

}  // 命名空间结束。

int main() {
    // 电话 接口边界 形状像 main()'s stream_fn: a
    // std::function 超过构建提供者的范围, 因为子代理配置
    // 持有其进程的副本 -wide.
    std::function<bool()> seam;

    std::atomic<bool> worker_started{false};
    std::atomic<bool> worker_finished{false};
    std::atomic<bool> saw_live_provider{true};
    std::atomic<long long> post_scope_calls{0};
    std::atomic<bool> scope_exited{false};

    {
        auto provider = std::make_shared<FakeProvider>();
        // BY 值。这个单一字符是固定的: `[provider]` 而不是说
        // `[&provider]`.
        seam = [provider] { return provider->stream(); };

        // A DETACHED 工作线程 - 形状 jaal 给出一个孤立的任务。
        // 没有加入它;它只是继续呼叫 接口边界.
        std::thread([&, seam] {
            worker_started.store(true);
            const auto until = std::chrono::steady_clock::now()
                             + std::chrono::milliseconds(400);
            while (std::chrono::steady_clock::now() < until) {
                if (!seam()) saw_live_provider.store(false);
                if (scope_exited.load(std::memory_order_acquire))
                    post_scope_calls.fetch_add(1, std::memory_order_relaxed);
            }
            worker_finished.store(true);
        }).detach();

        while (!worker_started.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        // main() 这儿有风,还有... 工作线程 还在里面 接口边界.
    }
    scope_exited.store(true, std::memory_order_release);

    // 也把地方的把手放下,所以... 仅 弃置 工作线程
    // 被捕获的复制件使提供者得以存活。 这是主返回后的状态。
    seam = nullptr;

    const auto until = std::chrono::steady_clock::now()
                     + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < until && !worker_finished.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    check(worker_finished.load(),
          "an abandoned worker runs to completion after its creating scope "
          "exits");
    check(post_scope_calls.load() > 0,
          "it really did call into the provider AFTER the scope exited "
          "(otherwise this proves nothing)");
    check(saw_live_provider.load(),
          "every one of those calls saw a LIVE provider, not a poisoned one");

    std::printf("\n%d checks, %d failures (%lld post-scope calls)\n",
                g_checks, g_fails, post_scope_calls.load());
    return g_fails == 0 ? 0 : 1;
}
