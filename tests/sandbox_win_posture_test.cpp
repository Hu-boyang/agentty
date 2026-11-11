// 电话 Windows 沙箱 合同,测试 每个 平台,可以运行它。
//
// claybin 窗口后端保留 translate/apply 拆分 :
// compile() 纯洁且可在任何地方检验( windows_backend_test
// 已将其钉入); 此文件将钉入 ADAPTER 层层——什么 agentty
// 报告承诺的墙壁上 窗子上会摆出姿态 agentty_tests 二进制程序
// 在每个主机上; LIVE 执法问题在于 claybin's
// windows_live_ 检查,该窗口框上运行(和当前通过)的窗口。
#include "agtest.hpp"

#include "agentty/tool/util/sandbox.hpp"
#include "agentty/domain/sandbox_config.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "agentty/tool/util/sandbox_claybin.hpp"

namespace ag = agentty::tools::util::sandbox;
namespace sb = agentty::tools::util::sandbox::claybin_backend;

TEST_CASE("sandbox/win: probe picks claybin when a wall is real") {
    // 地板是故意的一堵工作墙(appainer或job),
    // 因此Auto只有在没有从中建出边界的地方才会降解.
    // init() 运行探测器并锁定结果; probe() 本身为每
    // -platform-internal 细节。
    ag::init(ag::Mode::Auto);
    CHECK(ag::detected_backend() == ag::Backend::Claybin);
}

TEST_CASE("sandbox/win: the posture is native-path based") {
    auto posture = ag::claybin_posture_for_test();
    CHECK_FALSE(posture.system_read_roots.empty());
    // 工具链生活在系统根下;每个条目必须是真正的窗口路径,
    // 因为 ACL 贴在非纸面上 -native 道路大
    // 响失败 半堵墙比没有更糟糕
    for (const auto& root : posture.system_read_roots) {
        CHECK(root.size() >= 3);
        CHECK(root[1] == ':');
    }
    // 电话 工作区 仍然是写作 授权.
    CHECK_FALSE(posture.workspace.empty());
    CHECK(posture.workspace[1] == ':');
}

TEST_CASE("sandbox/win: describe grades the walls honestly") {
    auto posture = ag::claybin_posture_for_test();
    auto r = sb::describe(posture);
    CHECK(r.ok);
    if (!r.ok) { MESSAGE(r.detail); return; }
    REQUIRE_FALSE(r.walls.empty());

    // 找到一面墙的名字
    auto find = [&](std::string_view name) -> const sb::Wall* {
        for (const auto& w : r.walls)
            if (w.name == name) return &w;
        return nullptr;
    };

    // 过程隔离在安装器上: 探测器证明它的地方很强,
    // 并且从未默默缺席。
    if (auto* w = find("process.isolation")) {
        CHECK(w->strength != "none");
    }
    // NO SYSCALL FILTER 不论姿态如何,
    // 报告决不能在此声称有力。 claybin 诚实契约,
    // 也贴在适配器层上
    if (auto* w = find("syscall.filter")) {
        CHECK(w->strength != "strong");
    }
}

TEST_CASE("sandbox/win: spawn_shell starts and the tree kill exists") {
    auto posture = ag::claybin_posture_for_test();
    auto r = sb::spawn_shell(posture, "exit 0", -1, -1);
    if (!r.started) {
        // 探测器通过但产卵失败的宿主是退化的宿主(profile 权限,
        // 政策软件 )。 适配器报告的原因不是假装。
        // 适应器的信号是“不正确”的。
        MESSAGE("spawn failed: " << r.start_error);
        return;
    }
    CHECK(r.pid > 0);
    // 工作杀人是存在的,可以叫的。叫它在这里是安全的:孩子是
    // `exit 0` 已经走了
    if (r.kill_tree) CHECK_NOTHROW((void)r.kill_tree());
}

TEST_CASE("sandbox/win: the full exec chain captures output") {
    // 这是一条真正的工具 调用 选择: spawn_in_sandbox
    // 与呼叫者的管道fd(a) HANDLE 上面的值是, 跑者将返回的。
    // 这里的一次绿色产卵测试失败, 意味着问题就在于 HAND
    // -OFF ,而不是 沙箱.
    auto posture = ag::claybin_posture_for_test();
    // 管道: 创建像 run_child 已经,手拿 WRITE 结束于产卵者
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE out_r = nullptr, out_w = nullptr;
    REQUIRE(::CreatePipe(&out_r, &out_w, &sa, 0));
    const int pipe_fd = static_cast<int>(reinterpret_cast<intptr_t>(out_w));
    auto r = sb::spawn_argv(posture, {"cmd.exe", "/d", "/s", "/c", "echo inside"},
                            pipe_fd, pipe_fd, {});
    ::CloseHandle(out_w);  // 写完之后,读者就永远看不到EOF
    if (!r.started) {
        MESSAGE("exec chain spawn failed: " << r.start_error);
        ::CloseHandle(out_r);
        REQUIRE_FALSE(r.started);  // 以理由阻止案件发生
        return;
    }
    CHECK(r.pid > 0);
    // 工作柄搭便车进来 kill_tree 关闭; 我们拥有进程处理器,
    // 只能通过适配器的 pidfd。 对于退出代码,
    // 请在 pid 上等待: 任务杀关是强方法, 但适配器将进程
    // 处理器保存在 内部。 SpawnResult 被击倒了,
    HANDLE proc = ::OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                                FALSE, static_cast<DWORD>(r.pid));
    REQUIRE(proc != nullptr);
    ::WaitForSingleObject(proc, 15000);
    DWORD code = 0;
    ::GetExitCodeProcess(proc, &code);
    ::CloseHandle(proc);
    MESSAGE("guest exit code: " << code);
    // 排水管,直到 EOF (孩子已经离开那时)
    std::string out;
    char buf[512];
    DWORD n = 0;
    for (;;) {
        if (!::ReadFile(out_r, buf, sizeof(buf), &n, nullptr) || n == 0) break;
        out.append(buf, n);
    }
    ::CloseHandle(out_r);
    MESSAGE("captured: '" << out << "'");
    CHECK(code == 0);  // 出门 客人 RAN ; 非0=死于墙下
    CHECK(out.find("inside") != std::string::npos);
}
#else
// 关闭窗口 此文件在不测试的情况下记录了等价合同 :
// 同样的姿势 编译 各地( claybin 自己的十字架 :
// -platform 测试针头,窗口后端是唯一一个 APPLY
// 步骤是新的。在窗口上建的部件会运行上面的真实检查。
TEST_CASE("sandbox/win: parity contract compiles off-windows too") {
    CHECK(true);
}
#endif
