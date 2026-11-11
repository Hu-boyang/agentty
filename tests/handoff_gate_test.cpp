// 信任吗? -handoff 排队真的执行什么?
//
// 档案之前的一生都没有 面板 提议 "Agent writes host-executed files"
// 与 refuse/warn/allow, 持久化 圆形 -tripped 和
// `is_host_trusted()` 外面没有呼叫器 单元测试。所以控制器会 已保存
// 无值读取 -- 同类错误 sandbox_pane_test 写为, 上一层: 不
// "the switch was set wrong" 不过
// 设置显示拒绝，但写入仍然发生。
//
// 这包含什么, 按照顺序链运行:
//
//   决定策略图路径形状+策略到裁决 拒绝默认
//   实际停止了每次交接记录的写入种子,
//   允许的包含在形状匹配上的种子 SHAPE ,
//   所以一个更名的.git仍然被抓住了 线程 种子
//   存活在同时的作者中( 它横跨) -thread)
//
// 做什麽? 不 封面是Mcp桥 分派 说 调用
// check() - 需要一个活的工具提供者。 调用 网站是三行,
// 每个真正的写作都行使;有趣的逻辑都在这里。

#include <doctest/doctest.h>

#include <atomic>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <unistd.h>   // 获得pid — 独特的符号链接 沙箱 每跑
#endif

#include "agentty/tool/util/handoff_gate.hpp"
#include "agentty/tool/util/sandbox.hpp"   // Mode::Off ,用于 "off means off" 案件数量

using namespace agentty;
namespace hg = tools::util::handoff;
using Policy = sandbox_cfg::HandoffPolicy;
using Kind = sandbox_cfg::TrustKind;

namespace {

// 每个大小写前清除: 种子是进程 -global (这是会话记录),
// 所以测试继承了别人的 事件 测试错误的事物。
struct Fresh {
    Fresh() { hg::clear_handoff_feed(); }
    ~Fresh() { hg::clear_handoff_feed(); }
};

}  // 命名空间结束。

TEST_CASE("handoff: --sandbox off turns the gate off too") {
    Fresh fresh;

    // "off" 门不是 沙箱 WALL - 它住在
    // 工具上 - 继续 调用 而不是内核中 -
    // 但是它是同一个控制的一部分, 以及一个已
    // 禁用 沙箱隔离 拒绝他们写作。
    //
    // 驱动通过check()而不是check_with(),
    // 因为模式测试IS 正在测试的东西:check_with()采取
    // 明确的政策,并且故意对模式一无所知.
    namespace sb = agentty::tools::util::sandbox;
    sb::reset_config_for_test();
    sandbox_cfg::Config cfg;
    cfg.configured = true;
    cfg.handoff = sandbox_cfg::HandoffPolicy::Refuse;   // 最严格的设置
    sb::set_config(cfg);
    (void)sb::init(sb::Mode::Off);

    auto v = hg::check("/work/.vscode/tasks.json", "write");
    CHECK(v.allowed);
    CHECK_FALSE(v.is_handoff);

    // 没有任何记录:一个失效的控制器不应该充填一个种子。
    // 面板 将 渲染 仿佛它正在看着。
    CHECK(hg::handoff_feed().empty());
}

// ── 政策 ───────────────────────────────────────────────────────────

TEST_CASE("handoff: an ordinary path is not a handoff under any policy") {
    Fresh fresh;

    // 普通的, 以及必须保持便宜和沉默的。
    // 如果这种情况开始出现, 形状表会发展出一条规则,
    // 与普通源文件匹配, 并且门会变成噪音, 用户会关闭。
    for (auto p : {Policy::Refuse, Policy::Warn, Policy::Allow}) {
        auto v = hg::check_with("/work/src/main.cpp", "write", p);
        CHECK(v.allowed);
        CHECK_FALSE(v.is_handoff);
        CHECK(v.reason.empty());
    }
    CHECK(hg::handoff_feed().empty());
}

TEST_CASE("handoff: refuse actually refuses, and says what to do instead") {
    Fresh fresh;

    auto v = hg::check_with("/work/.vscode/tasks.json", "write", Policy::Refuse);
    CHECK_FALSE(v.allowed);
    CHECK(v.is_handoff);
    CHECK(v.kind == Kind::EditorTask);

    // 信息是,我刚刚说 加载-bearing 拒绝,
    // 但只说 "no" 被模特儿重审过,然后...
    // 重试 环绕安全门比结果都差 所以它必须命名为机制
    // 和 选项。检查两者意味着未来的重写不能悄悄地掉掉
    // 结束循环的半个字。
    CHECK(v.reason.find("refused") != std::string::npos);
    CHECK(v.reason.find("executes") != std::string::npos);
    CHECK(v.reason.find("Sandbox pane") != std::string::npos);
}

TEST_CASE("handoff: warn allows the write but still reports it") {
    Fresh fresh;

    auto v = hg::check_with("/work/.git/config", "edit", Policy::Warn);
    CHECK(v.allowed);          // 写入收益
    CHECK(v.is_handoff);       // 并且它仍然是 事件
    CHECK(v.kind == Kind::GitConfig);
    CHECK_FALSE(v.reason.empty());
}

TEST_CASE("handoff: allow is silent in the verdict but NOT in the feed") {
    Fresh fresh;

    auto v = hg::check_with("/work/.envrc", "write", Policy::Allow);
    CHECK(v.allowed);
    CHECK(v.is_handoff);
    CHECK(v.reason.empty());   // 用户选择了这个, 没什么要警告的

    // 电话 加载-bearing 半。只有记录拒绝的
    // 种子在关闸时才会完全空白,而这正是用户最需要
    // 查看代理作者内容的时候。为此原因,记录发生在政策
    // 分支之前。
    auto feed = hg::handoff_feed();
    REQUIRE(feed.size() == 1);
    CHECK(feed[0].write.path == "/work/.envrc");
    CHECK(feed[0].kind == Kind::ShellInit);
}

// ── 形状匹配, 不匹配名称 ────────────────────────────────────

TEST_CASE("handoff: a git dir under another name is still caught") {
    Fresh fresh;

    // 光标 3. 0.0 的bug, 准确的说: "git directories do not have to be called .git"。
    // 精确的 -name 桌子会错过这个, 这就是为什么
    // is_host_trusted 匹配形状。
    for (auto path : {"/work/.git/config",
                      "/work/nested/.git/hooks/pre-commit",
                      "/work/sub/.git/config"}) {
        auto v = hg::check_with(path, "write", Policy::Refuse);
        CHECK_FALSE(v.allowed);
        CHECK(v.is_handoff);
    }
}

TEST_CASE("handoff: the known escape shapes are all refused") {
    Fresh fresh;

    // 每人一美元(含1美元) TrustKind 有个
    // 混凝土 CVE 或背后的咨询。 枚举 这样
    // ADDING 一种没有匹配规则的 , 而不是默默地
    // 运输一个非强制类别。
    struct Case { const char* path; Kind kind; };
    const Case cases[] = {
        {"/work/.vscode/tasks.json",        Kind::EditorTask},
        {"/work/.git/config",               Kind::GitConfig},
        {"/work/.envrc",                    Kind::ShellInit},
        {"/home/u/.config/autostart/x.desktop", Kind::SessionStart},
    };
    for (const auto& c : cases) {
        auto v = hg::check_with(c.path, "write", Policy::Refuse);
        CHECK_FALSE(v.allowed);
        CHECK(v.kind == c.kind);
    }
}

TEST_CASE("handoff: login, session and PATH files are host-trusted (audit 2026-10-17)") {
    Fresh fresh;

    // 与 工作区 时间 $HOME 这些文件是可写文件的: 命令可以将运行
    // 于下次登录时的某个东西丢弃, 或者在下面阴影显示一个真正的命令
    // PATH。现在每个都是一个值得信赖的形状。
    for (const char* p : {"/home/u/.profile", "/home/u/.bash_profile",
                          "/home/u/.zprofile", "/home/u/.zshenv",
                          "/home/u/.config/fish/config.fish",
                          "/home/u/.config/environment.d/10-x.conf",
                          "/home/u/.config/autostart/x.desktop",
                          "/home/u/.config/systemd/user/x.service",
                          "/home/u/.config/hypr/hyprland.conf",
                          "/home/u/.local/bin/git",
                          "/home/u/.codex/config.toml",
                          "/home/u/.gemini/settings.json"}) {
        INFO(p);
        CHECK_FALSE(hg::check_with(p, "write", Policy::Refuse).allowed);
    }
    // 一个普通的点文件,运行什么也不是普通的。
    CHECK(hg::check_with("/home/u/.config/kitty/kitty.conf", "write",
                         Policy::Refuse).allowed);
    CHECK(hg::check_with("/home/u/.local/share/notes.txt", "write",
                         Policy::Refuse).allowed);
}

// ── 饲料 ─────────────────────────────────────────────────────────────

TEST_CASE("handoff: the same path twice is one entry, not two") {
    Fresh fresh;

    // 交接是事实 FILE ,而不是顺序 事件 : 代理重写
    // tasks.json 转三圈还是
    // 代理创建了 tasks.json。
    // 这与经纪人的输入方式不同, 命令是信息,路径是。
    (void)hg::check_with("/work/.vscode/tasks.json", "write", Policy::Allow);
    (void)hg::check_with("/work/.vscode/tasks.json", "edit", Policy::Allow);
    (void)hg::check_with("/work/.vscode/tasks.json", "write", Policy::Allow);

    auto feed = hg::handoff_feed();
    CHECK(feed.size() == 1);
    // 最新写法赢得元数据,所以记录的工具是最后一个.
    CHECK(feed[0].write.command == "write");
}

TEST_CASE("handoff: distinct paths are kept apart and bounded") {
    Fresh fresh;

    // 过了盖子,这个检查了束缚 和 它将最古老
    // 的而不是拒绝新的-- 充斥和聋子的饲料 将
    // 隐藏最近的交割, 也就是被调试的。
    for (int i = 0; i < 80; ++i)
        (void)hg::check_with("/work/sub" + std::to_string(i) + "/.envrc", "write",
                       Policy::Allow);

    auto feed = hg::handoff_feed();
    CHECK(feed.size() <= 32);
    CHECK_FALSE(feed.empty());
    // 最新保存的:最后写出的路径必须仍然存在.
    CHECK(feed.back().write.path == "/work/sub79/.envrc");
}

TEST_CASE("handoff: concurrent writers do not corrupt the feed") {
    Fresh fresh;

    // 种子是从工具写入的 工作线程 线程 并阅读自 状态更新函数 线
    // 程 真正的十字架是用来 -thread 和经纪人的一样
    // `maya::guarded` 制造 "forgot the lock"
    // 无法形容,而这个测试 证明警卫是 加载-bearing
    // 而不是装饰 -- -- 在TSan下运行,它报告什么也没有,
    // 如果警卫被撤职,报告很多。
    std::atomic<bool> go{false};
    std::vector<std::thread> workers;
    for (int t = 0; t < 4; ++t) {
        workers.emplace_back([t, &go] {
            while (!go.load()) { /* 排队 */ }
            for (int i = 0; i < 100; ++i)
                (void)hg::check_with("/work/w" + std::to_string(t) + "_" +
                                   std::to_string(i) + "/.envrc",
                               "write", Policy::Allow);
        });
    }
    // 并读: a 快照 中场 -write 实际 竞态.
    std::thread reader([&go] {
        while (!go.load()) {}
        for (int i = 0; i < 200; ++i) (void)hg::handoff_feed();
    });

    go.store(true);
    for (auto& w : workers) w.join();
    reader.join();

    auto feed = hg::handoff_feed();
    CHECK(feed.size() <= 32);
    CHECK_FALSE(feed.empty());
    // 每条幸存的条目都完好无损:没有撕裂路径,没有部分发布的推力产生
    // 的默认-constructed记录.
    for (const auto& h : feed) {
        CHECK_FALSE(h.write.path.empty());
        CHECK(h.write.at_ms > 0);
    }
}

TEST_CASE("handoff: clearing the feed empties it") {
    Fresh fresh;

    (void)hg::check_with("/work/.envrc", "write", Policy::Allow);
    CHECK_FALSE(hg::handoff_feed().empty());
    hg::clear_handoff_feed();
    CHECK(hg::handoff_feed().empty());
}

TEST_CASE("handoff: a symlinked directory cannot hide a trusted shape") {
    Fresh fresh;

    // is_host_trusted ()是一条纯正的道路 -shape 走路,
    // 只看到拼写是手递的 一个 整个门的连锁步骤:
    //
    //     进去 -s .git/hooks 虚伪
    //     的写作 innocuous/post-merge < - 安装一个真正的 git 钩
    //
    // 固定前测量: "innocuous/post-merge" 读为
    // PLAIN 时 ".git/hooks/post-merge" 读作信任,
    // 而书写以两种方式到达钩子目录。在您自己的目录中创建一个符号链接
    // 工作区 正常的工作,所以没有别的事阻止它。
    //
    // check() 现在也尝试 RESOLVED 路径,
    // 但只有在拼写看起来无罪时,常见的情况必须保持字符串与不 I/O.
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() /
        ("agentty_handoff_symlink_" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / ".git" / "hooks");
    fs::create_directories(root / "src");

    fs::create_directory_symlink(root / ".git" / "hooks", root / "innocuous", ec);
    if (ec) return;   // 这里没有符号链接支持; 无可断言的

    // 直接拼写:固定前和固定后被捕获.
    CHECK(hg::check_with((root / ".git" / "hooks" / "post-merge").string(),
                         "write", Policy::Refuse).is_handoff);

    // 该 REGRESSION :同一目的地,通过韵母链接拼写.
    auto v = hg::check_with((root / "innocuous" / "post-merge").string(),
                           "write", Policy::Refuse);
    CHECK_MESSAGE(v.is_handoff,
                  "a trusted path reached through a symlinked directory must "
                  "still be gated -- otherwise one `ln -s` defeats the gate");
    CHECK_FALSE(v.allowed);

    // 解决不能开始指向普通文件:一检查点火一切,
    // 就跟一检查点光一分一样,是没用的。
    CHECK_FALSE(hg::check_with((root / "src" / "main.cpp").string(),
                               "write", Policy::Refuse).is_handoff);
    CHECK_FALSE(hg::check_with((root / "README.md").string(),
                               "write", Policy::Refuse).is_handoff);

    fs::remove_all(root, ec);
}
