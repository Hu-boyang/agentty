// agentty_standalone_tests - 一个 二进制程序 , argv 数据
// -dispatched ,为全文 -stack 独立测试 CANNOT 将 doctest
// 共同程序中的案件(它们) fork/exec 驱动 a PTY ,运行模糊的种子循环,或产卵子进程。
// 每个子进程仍在运行中。 自身 每个测试条目的处理 : agentty_standalone_tests
// <name> [args...] ——测试的原主. 归并 ~16 重100 重号 MB+ 链接 变成一个
//
// 每一个测试 `int main(...)` 已更名为 `<name>_main` 时间 编译 时间通过每架 -source
// `-Dmain=<name>_main` (见A/C.5/61/L.48,第16段)
// cmake/AgenttyTests.cmake 测试 ORIGINAL 签名由重命名保存, 所以下面的 .def 记录每个
// 测试的 ality : NOARGS (单位:千美元) main() ) 对 ARGS (int main(int,char**))。
// ARGS 测试我们通过 argv[1..] 所以它。 解析 其所属 标志 (anthropic_md_stream:
// 2019年,第2期,第2版. `det <fixture> --cps …`.
//
// 3个消毒器 -narrow 测试( concurrency_primitives /
// cred_crypt / keytore)为 不 在这里,他们 链接 故意
// 最小源子集以保留 asan -clean ,折叠到完整的对象集中,将失败。
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>

#include <maya/runtime.hpp>

// 申报——按原main()计算,为平均值.
#define FOLD_NOARGS(name) extern int name##_main();
#define FOLD_ARGS(name)   extern int name##_main(int, char**);
#include "agentty_standalone_tests.def"
#undef FOLD_NOARGS
#undef FOLD_ARGS

int main(int argc, char** argv) {
    // 沙箱 数字 -user 整个根 二进制程序 任何测试运行前,
    // 这些都已经满了 -runtime 写作的测试 凭据, 线程 和设置;
    // 没有此选项,它们会改变开发者的真假 ~/.agentty.
    // 镜像 tests/test_main.cpp ; 查看其中的理由
    // 和绊线 util/user_root.cpp.
    //
    // 也反映了文件的 CLEANUP ,它以前没有,而且费用为12美元
    // GB 的/tmp和失败的构建。 test_main.cpp 创建一个
    // 沙箱 百分比 BINARY ; 此选项 二进制程序 重新为 -exec
    // 一次折叠试验,试产一次150次,每一次漏出一次250次 MB 症状在
    // 很久之后才出现,看起来与这个无关: "No space left on device"
    // 完全编译其他东西
    namespace fs = std::filesystem;
    fs::path sandbox;
    {
        const auto stamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        sandbox = fs::temp_directory_path() /
            ("agentty_stests_home_" + std::to_string(stamp));
        std::error_code ec;
        fs::create_directories(sandbox, ec);
#if defined(_WIN32)
        _putenv_s("AGENTTY_HOME", sandbox.string().c_str());
        _putenv_s("AGENTTY_UNDER_TEST", "1");
#else
        ::setenv("AGENTTY_HOME", sandbox.string().c_str(), 1);
        ::setenv("AGENTTY_UNDER_TEST", "1", 1);
#endif
    }
    // 扫沙盒 an EARLIER 已丢弃( 崩溃或 SIGKILL
    // 跳过出口的清理,这些运行正是值得调查的)——所以
    // 只有一天后老的目录,从不做兄弟的测试. 工作线程 活的一样,
    // 规则和推理都一样 test_main.cpp ,
    // 包括广度:整体 `agentty_*` 名称空间而不是此
    // 二进制程序 自己的前缀,因为个人测试 薄荷自己的临时迪尔,
    // 并且大多数从来没有清理。 一条规则比47。
    {
        std::error_code ec;
        const auto now = fs::file_time_type::clock::now();
        for (fs::directory_iterator it(fs::temp_directory_path(), ec), end;
             !ec && it != end; it.increment(ec)) {
            const auto name = it->path().filename().string();
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
    // 离开时把它取出来, 在EVERY路径上。
    //
    // RAII 而不是主机的结尾编码,因为 分派 下方是一堵墙
    // `return name##_main(...)` 宏 -
    // 之后没有单一的退出设置清理,并且 a `return` 下一个
    // 人会悄悄跳过它 毁灭者是不会被遗忘的
    //
    // 最佳( 最佳) -effort : 一个漏水 沙箱 是一个缓慢的漏泄,
    // 而失败的删除并不值得失败一次绿色测试结束.
    struct SandboxCleanup {
        fs::path dir;
        ~SandboxCleanup() {
            if (dir.empty()) return;
            std::error_code ec;
            fs::remove_all(dir, ec);
        }
    } cleanup{sandbox};

    // 这些测试驱动真正的应用 状态更新函数 ,其中部分发射网络预热(一个脱落的背景)
    // TLS 单项测试从未 调用 join_prewarm (),这样 线程 将会
    // 超过寿命 main() 财务报告和已审计财务报表 竞态
    // libcrypto/allocator 断断续续的关闭 SIGSEGV (e.g.
    // fork_test's ForkThread 状态更新函数 测试不需要 暂时的胜利。
#if defined(_WIN32)
    _putenv_s("AGENTTY_NO_PREWARM", "1");
    _putenv_s("AGENTTY_NO_UPDATE_CHECK", "1");
#else
    ::setenv("AGENTTY_NO_PREWARM", "1", 1);
    // 永远不要让 init() 'release check 替换此选项 二进制程序.
    ::setenv("AGENTTY_NO_UPDATE_CHECK", "1", 1);
    // 测试本身的 临时底盘进入 沙箱 清理也带走了
    {
        const fs::path tmp = sandbox / "tmp";
        std::error_code ec;
        fs::create_directories(tmp, ec);
        if (!ec) ::setenv("TMPDIR", tmp.c_str(), 1);
    }
#endif
    if (argc < 2) {
        std::fprintf(stderr,
            "usage: %s <test-name> [args…]\n\nAvailable tests:\n", argv[0]);
#define FOLD_NOARGS(name) std::fprintf(stderr, "  %s\n", #name);
#define FOLD_ARGS(name)   std::fprintf(stderr, "  %s\n", #name);
#include "agentty_standalone_tests.def"
#undef FOLD_NOARGS
#undef FOLD_ARGS
        return 2;
    }
    const std::string_view which{argv[1]};

    // 准备内核的循环 线程。与 tests/test_main.cpp
    // : 这些测试驱动器 状态更新函数 财务报告和已审计财务报表 view()
    // 直接在主机上 线程 而不是运行内核,和 view() 触摸环路
    // -bound 渲染 状态。在循环中,这是 设计中止,所以,
    // 绳子必须说 "this thread is the loop" - 这儿就是这样
    //
    // A 工作线程 试产中产物仍然没有任何标志 所以保证在它
    // 重要的地方是完好无损的
    const auto loop_id = maya::arm_loop_for_tests();
    // 构建子 -argv : 保留 argv[0] 作为 真实 可执行路径( 测试可能重新 -exec 本身 --
    // -- . external_acp_backend_test 产卵 argv[0] 假 ACP 探员,
    // 放下 分派 名称在 argv [1],其余部分作为测试的自有 标志。所以,我们想让您知道
    // `agentty_standalone_tests anthropic_md_stream det X --cps 45`
    // 以 argv = { 达到测试 <exe-path>, "det", "X", "--cps", "45"}.
    int    sub_argc = argc - 1;
    char** sub_argv = argv + 1;
    sub_argv[0] = argv[0];   // 真实的exe路径, 不是测试名称

#define FOLD_NOARGS(name) if (which == #name) return name##_main();
#define FOLD_ARGS(name)   if (which == #name) return name##_main(sub_argc, sub_argv);
#include "agentty_standalone_tests.def"
#undef FOLD_NOARGS
#undef FOLD_ARGS

    std::fprintf(stderr, "%s: unknown test '%.*s'\n",
                 argv[0], static_cast<int>(which.size()), which.data());
    (void)sub_argc; (void)sub_argv;
    return 2;
}
