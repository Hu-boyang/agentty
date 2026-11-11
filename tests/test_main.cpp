// agentty_tests ——单试. 二进制程序.
//
// 每一个 单元测试 输入 tests/ 编译为 doctest TEST_CASE
// 链接到 此 执行文件,其中 链接 共享的 agentty 对象设定一次。
// 之前的模型构建了~70个独立的可执行文件,每个都静态重现 -linking 整个对象集——即:
// 链接 风扇 -out 曾经是主导者 CI 成本。 成本。 doctest 自动
// -registers 每隔一段时间 TEST_CASE ,因此,此文件只提供 main().
//
// ctest 仍可逐个运行和过滤用例，doctest_discover_tests()
// 为每个 TEST_CASE 注册独立条目，保留 ctest -j 并行和
// 逐用例失败报告，同时可使用 agentty_tests --test-case=X。
// runs one in isolation.
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>

#if defined(_WIN32)
#  include <process.h>   // _getpid
#else
#  include <unistd.h>    // 发作
#endif

#include <maya/core/anim_clock.hpp>
#include <maya/runtime.hpp>

int main(int argc, char** argv) {
    // (无) 渲染 策略是: 将“ ” 改为“ ” 终端-derived
    // 更多—— 将 流式传输 逐步显示 默认 ON 在所有地方,
    // tmux包括——所以套房在多路电机内外的行为都是一样的,这里没有门. )

    // ── 中央用户-root隔离 ────────────────────────────────
    // 点 整个 二进制程序 被丢弃的 ~/.agentty 测试运行前。
    // 测试会写入 凭据 ,账户注册表, 线程 设置和 缓存 ;
    // 没有这个,它们会改变开发商的真店。 (这不是假设:五)
    // 凭据 测试通过 XDG_CONFIG_HOME 默默地
    // 失去了他们 沙箱 当单身 -root 合并已移
    // config_dir (a) 改为 $AGENTTY_HOME ,
    // 套房开始覆盖 现场 凭据 + 账户。 )
    //
    // 人数 -process 独一无二的平行测试 工作线程 永远不能共享状态。
    // 测试想要他们的 自身 沙箱 仍然设置env AGENTTY_HOME
    // 本身——这只是默认的地板。 AGENTTY_UNDER_TEST
    // 将绊线插进去 util/user_root.cpp ,
    // 它会中止 如果有什么东西到达真正的根 尽管如此。
    //
    // REMOVED。这曾经是漏出: 每个进程有一个目录,
    // 测试运行此程序 二进制程序 发现困难的路程是16英里。
    // GB 电磁波和电磁波 与89 373 100%的冲击
    // `agentty_tests_home_*` 目录, 但它失败了。
    // BUILD 与 "No space left on device".
    // 测试带一定不需要清洁工
    namespace fs = std::filesystem;
    fs::path sandbox;
    {
        const auto stamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        // pid + 印章:steady_clock是per-boot,
        // 而不是per-process,所以从同一个勾起的两个测试工作线程(粗时钟,
        // -j N在一个繁忙的盒子上)得到了SAME的家,并互相扭曲了
        // 对方的线程,图像和索引——一个-j-only片片的家族的来源.
#if defined(_WIN32)
        const auto pid = static_cast<long long>(_getpid());
#else
        const auto pid = static_cast<long long>(::getpid());
#endif
        sandbox = fs::temp_directory_path() /
            ("agentty_tests_home_" + std::to_string(pid) + "_" + std::to_string(stamp));
        std::error_code ec;
        fs::create_directories(sandbox, ec);
#if defined(_WIN32)
        _putenv_s("AGENTTY_HOME", sandbox.string().c_str());
        _putenv_s("AGENTTY_UNDER_TEST", "1");
        _putenv_s("AGENTTY_NO_PREWARM", "1");
        _putenv_s("AGENTTY_NO_UPDATE_CHECK", "1");
#else
        ::setenv("AGENTTY_HOME", sandbox.string().c_str(), 1);
        ::setenv("AGENTTY_UNDER_TEST", "1", 1);
        // 预发是背景 TLS 拨打一个真正的主机。运行的测试 init() 开始一个,
        // 它可以超过 main() 财务报告和已审计财务报表 竞态 自由主义的
        // 退出撕裂( ) SIGSEGV 输入 OBJ_sn2nid。
        // 独立的绳子已经选择退出。
        ::setenv("AGENTTY_NO_PREWARM", "1", 1);
        // init()队列进行真实的释放检查,比PROJECT_VERSION
        // 更新的标记使其自动-download,并重命名一个超过
        // self_exe_path()的释放,这里是测试二进制程序.
        ::setenv("AGENTTY_NO_UPDATE_CHECK", "1", 1);
#endif
    }
    // 扫荡一切 EARLIER 已丢弃( 崩溃或 SIGKILL
    // 跳过下面的清理, 这些正是值得调查的运行—— 所以这只
    // 删除了一天以上的目录, 从来不是兄弟的测试 工作线程
    // 活的一号).
    //
    // 和整个吻合 `agentty_*` 名字空间, 不只是这个 二
    // 进制程序 属于自己的 沙箱 宽度是关键: 单个试验薄荷自己的临时底盘(
    // agentty_smarttune_, agentty_acctsw_,
    // agentty_mcp_ ,... 47 个不同的前缀,最后数时,
    // 并且大多数都从未清理。 调用 站台已经破旧不堪了 -a-mole
    // 接下来的测试会再次泄露出来 扫荡命名空间是一条涵盖所有
    // 事物的规则 包括那些还没有人写过的东西
    //
    // 在此之前测量的:3537 过期 目录,4956之上的 独立 二进制程序
    // 泄露——足以填补16个 GB tmpfs 失败, 与此无关的构造
    // "No space left on device".（设备没有剩余空间。）
    {
        std::error_code ec;
        const auto now = fs::file_time_type::clock::now();
        for (fs::directory_iterator it(fs::temp_directory_path(), ec), end;
             !ec && it != end; it.increment(ec)) {
            const auto name = it->path().filename().string();
            // 只有目录,只有我们的 A FILE 名称 agentty
            // -something 前面的字条很具体 以至于无法在
            // Dev box collides上找到其他东西
            //
            // 两种拼写,测试使用 `agentty_` 财务报告和已审计
            // 财务报表 `agentty-` 互换( E) agentty-subagent-test-,
            // agentty-test- 而这一次扫荡只让一个静静地离开另一个 --
            // -- 这就是183的滋味。 agentty-subagent 迪尔
            // 幸免于难 第一版的这个。
            if (!name.starts_with("agentty_") &&
                !name.starts_with("agentty-")) continue;
            std::error_code dec;
            if (!fs::is_directory(it->path(), dec) || dec) continue;
            std::error_code sec;
            const auto mt = fs::last_write_time(it->path(), sec);
            if (sec || now - mt < std::chrono::hours(24)) continue;
            std::error_code rec;
            fs::remove_all(it->path(), rec);
        }
    }
    // 将动画时钟全部按下 二进制程序。若干个 render/seam 测试(
    // 中途岛) turn_settle, 逐步显示 驱动器 帧 同步并坚持承诺
    // -scrollback 稳定;它们需要 maya::anim_now_ms
    // (冰冻如此) 渲染 是模型的纯函数,而不是赛车墙 -clock.
    // 之前每个测试都冻结了它 main() ;
    // 共享一个 二进制程序 我们在这里做一次。
    maya::testing::freeze_anim_clock();

    // 测试会自己做临时的底盘 temp_directory_path (。
    // 指向里面的, 指向里面, 指着里面的 沙箱 所以他们都去当 沙箱。
    // 在上方的扫描后设定,它必须查看真实的/tmp。
#if !defined(_WIN32)
    {
        const fs::path tmp = sandbox / "tmp";
        std::error_code ec;
        fs::create_directories(tmp, ec);
        if (!ec) ::setenv("TMPDIR", tmp.c_str(), 1);
    }
#endif
    // 退出时删除,包括测试 调用 std::exit 自己来
    static fs::path g_sandbox = sandbox;
    std::atexit([] {
        std::error_code ec;
        if (!g_sandbox.empty()) fs::remove_all(g_sandbox, ec);
    });

    // 准备内核的循环 线程 ,同样原因,上面的时钟被冻结: 这些测试驱动器
    // view() 直接代替运行内核,以及 view() 合法接触环 -bound
    // 状态( 状态) 渲染 缓存 输入 agent_timeline.cpp ,
    // 冻起来 -build 标志 输入
    // tool_body_preview.cpp).
    //
    // 没有这个 渲染 测试中止——这是起作用的机制,
    // 而不是假阳性:在环路之外没有符号,
    // 以及 loop_bound 中止而不是默默地交还秒 线程 空本。
    // 装甲身份说 "this thread IS the loop for this process" ,
    // 这是事实: doctest 运行主控的大小写 线程.
    //
    // 不错 不 削弱它保护的东西的保证。 WORKER 测试中
    // 仍然没有符号, 并且仍然中止, 因为身份是按 -thread
    // - 这正是 loop_affinity_test 在一个有
    // 叉子的孩子身上
    const auto loop_id = maya::arm_loop_for_tests();

    const int rc = doctest::Context(argc, argv).run();
    return rc;   // 上面的自动卸载钩将删除 沙箱
}
