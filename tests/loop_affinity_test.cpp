// loop_affinity_test — agentty '环 -only 状态是循环 -only 证明这一点。
//
// 此测试属于 agentty 而非 jaal 的原因。
//
// jaal 自己的测试证明 loop_bound<T> 行为,他们不说是否 此 代码库 使用它,
// 而这个空白正是用来死亡的保证: jaal 已运出 loop_bound ,则 头文件 解释它所防止
// 的错误( )
// 工作线程调用渲染器会悄悄获得独立空缓存，产生错误结果，比直接崩溃更难发现。
// 并横跨 jaal, maya 财务报告和已审计财务报表 agentty
// 数目 loop_bound<T> 生产代码中 ZERO.
// maya的thread_locals中所有68个和agentty中所有25个未爆炸,
// 每个都以评论的形式坚持了1-thread规则.
//
// 收养是零的原因 变成了缺陷,而不是冷漠: 一种象征只能由内核来铸造,
// 内核从未将一个交给 update() 或 或 时 间 view() ,
// 和 loop_key 建筑师是公开的 -and-explicit - 所以
// 唯一能拿到一个符号的方法就是 FORGE 一项(
// `loop_token t{loop_key{}}` 保证什么也不能被采纳。
//
// 所以这个测试覆盖了一半 jaal 不能这样:
//
//   1. 伪造物已消失( static_asserts 下——洞的形状).
//   2. agentty的缓存观点可以连通,而不是关闭
//   3. a 工作线程 触摸他们 DIES 而不是说 渲染 从空
//      复制,这是整个行为索赔
//
// 独立、非合并:第3号案件声称 ABORT ,所以它必须运行 在
// 儿童过程和 doctest 无法容纳。

#include <maya/runtime.hpp>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <type_traits>
#include <variant>

#if !defined(_WIN32)
#  include <sys/wait.h>
#  include <unistd.h>
#  define HAVE_FORK 1
#else
#  define HAVE_FORK 0
#endif

namespace k = maya;

// ── 1. 被钉住的洞 ─────────────────────────────────────────────
//
// 每个都对应一个通向循环的路径 -bound 州立大学,
// 他们就是 static_asserts 而不是说 compile_fail 案件
// 因为 agentty 没有 编译-fail 系系; jaal 带负数病例( CASE
// 1.7 变化中 jaal/tests/compile_fail/loop_bound.cpp
// 消费者方面也把同样的事实 都贴上了标签 所以 jaal 撞倒了它们,使它们倒闭
// agentty 建设而不是默默扩大 agentty 曝光了 已经是

// loop_key在此没有被断言,因为maya根本不出口它:铸币
// 局铸币局的密钥是jaal的,agentty甚至不能命名.
static_assert(!std::is_copy_constructible_v<k::loop_token>,
              "a copyable token could be stashed in a global and used off-loop");
static_assert(!std::is_move_constructible_v<k::loop_token>,
              "a movable token could be captured into a task body");
static_assert(!std::is_default_constructible_v<k::loop_token>,
              "a default-constructible token would be no proof at all");

// 一定没有 UNCHECKED 环球存取器。 任务 正文 无法捕捉,
// 但依然可以 CALL 什么都行,所以说 loop_token::current
// () 将手 工作线程 密码一个符号,并撤销其余。 on_loop ()是选中的表
// 单: 它会中止循环而不是返回证明。
template <class T> concept has_current = requires { T::current(); };
static_assert(!has_current<k::loop_token>);

// 循环 -bound 州不能去一个 工作线程 以作为任务论证.
static_assert(!maya::Sendable<k::loop_bound<int>>,
              "loop_bound is pinned to one thread; it must not be Sendable");
static_assert(!maya::Sendable<k::loop_token>,
              "a token is proof about ONE thread; it must not travel");

// ── 2. 立 立 论 -in 费用 agentty 视图 缓存 ──────────────────────────────
//
// 和真实的一样
// src/runtime/view/thread/turn/agent_timeline/agent_timeline.cpp:
// a 缓存 编号 渲染 路径读写时没有 锁 ,只更正了其中之一 线程 拥有它,
// 真实的 缓存 是文件 -static ,因此它反映的是它们
// 的用法而不是导入它们。
static k::loop_bound<std::string> g_render_cache{};

struct Paint {};
struct Done {};

struct App {
    using Model = int;
    using Msg   = std::variant<Paint, Done>;
    using Cmd   = maya::Cmd<Msg>;

    static Cmd init(Model& m) { m = 0; return Cmd::send(Msg{Paint{}}); }

    // 准备 view() : 绕着环线运行,触摸 缓存 没有 锁.
    static Cmd update(Model& m, Paint) {
        if (!k::on_loop()) { m = -1; return Cmd::quit(); }

        g_render_cache.with([](std::string& c) { c = "painted"; });

        // A COPY 可能离开 with() 正文 提及不得 -- --
        // jaal static_asserts 因为这上面,因为... 缓存 驱逐,
        // 握柄会缠绕在下一个 put().
        if (g_render_cache.with([](const std::string& c) { return c; }) != "painted") {
            m = -2;
            return Cmd::quit();
        }

        m = 1;
        return Cmd::send(Msg{Done{}});
    }

    static Cmd update(Model& m, Done) { m = 2; return Cmd::quit(); }
};

static int on_loop_cases() {
    // 在任何内核存在之前,此线程不是一个环.
    if (k::on_loop()) { std::puts("FAIL: armed with no kernel"); return 1; }

    // 赤裸裸的 工作线程 也不是循环。
    bool worker_armed = true;
    std::thread([&] { worker_armed = k::on_loop(); }).join();
    if (worker_armed) { std::puts("FAIL: bare worker reported on_loop"); return 2; }

    // 真正的内核武装自己 线程 财务报告和审定财务报表 整个 生命周期 不只是
    // 在内心 step()。这里很重要:一个东道主 调用 view() 步数之间,
    // 和 view() 是一个代码, 与 缓存 值得保护。
    {
        maya::headless<App> h;
        h.run_until_idle();
        if (h.model() != 2) {
            std::printf("FAIL: app ended at model=%d\n", h.model());
            return 3;
        }
    }

    // 一旦内核消失,它又被解除了 所以 池 线程
    // 事后再利用不能继承证据。
    if (k::on_loop()) { std::puts("FAIL: identity leaked past ~kernel"); return 4; }

    return 0;
}

// ── 3. 行为索赔:一份工作线程 DIES, 没有收到副本。 ───────
#if HAVE_FORK
static int worker_abort_case() {
    std::fflush(nullptr);
    const pid_t pid = ::fork();
    if (pid < 0) { std::puts("SKIP: fork failed"); return 0; }

    if (pid == 0) {
        // 儿童:a 工作线程 到达 渲染 缓存 之前的
        // loop_bound 默默地成功对抗了 工作线程 自己的空
        // 副本和 帧 涂错了。 它现在必须中止。
        std::thread([] {
            g_render_cache.with([](std::string& c) { c = "stomped by a worker"; });
        }).join();
        // 只有在担保被打破时才能到达。
        std::_Exit(0);
    }

    int status = 0;
    ::waitpid(pid, &status, 0);

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        std::puts("FAIL: a worker touched loop-bound state and lived");
        return 5;
    }
    if (!WIFSIGNALED(status)) {
        std::printf("FAIL: expected a signal, got exit %d\n", WEXITSTATUS(status));
        return 6;
    }
    if (WTERMSIG(status) != SIGABRT) {
        std::printf("FAIL: expected SIGABRT, got signal %d\n", WTERMSIG(status));
        return 7;
    }
    return 0;
}
#else
static int worker_abort_case() { return 0; }   // 无叉:1-2个案例仍在运行
#endif

int main() {
    if (const int rc = on_loop_cases())    return rc;
    if (const int rc = worker_abort_case()) return rc;
    std::puts("loop_affinity_test: ok");
    return 0;
}
