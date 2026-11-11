// settings_default_test - 钉住 SHIPPED 默认 权限 简介.
//
// 新的安装( 无) settings.json )和a 数据 legacy/partial 配置缺少
// "profile" 密钥必须 两者 决 议 Write - 电话 Profile 枚举 '0值'。
// 这是一个有意的产品决定:agentty是自主的-by-default,
// 由沙箱+工作区边界(网站文件在/docs/profiles,
// ACP桥分别从Ask开始)包含. 这个测试是回归的锁,
// 所以后期的枚举重排或Setro
// -struct-default的改变不能默默地翻转默认并解开文件.
//
// 它驱动PUBLIC 持久化 API(load_settings / save_settings)
// 对一个孤立的$HOME,所以它触摸到真正的磁盘,而不是一个模拟.

#include "agentty/io/persistence.hpp"
#include "agentty/store/store.hpp"
#include "agentty/domain/profile.hpp"
#include "agentty/domain/sandbox_config.hpp"
#include "agentty/runtime/panel/settings/items.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "agtest.hpp"

#include <unistd.h>   // 发作

namespace fs = std::filesystem;
using namespace agentty;

// 编译-time 双胞胎 运行时 检查(也执行项目) -wide 输入 domain/profile.hpp):
// Write 一定要留下来 枚举 '0值, 因为 load_settings
// () 默认缺失 "profile" 值的密钥( E) "profile", 0).
static_assert(static_cast<std::uint8_t>(Profile::Write) == 0,
              "Profile::Write must remain the enum's zero value — the settings "
              "loader defaults a missing 'profile' key to 0, which must == Write.");

TEST_CASE("settings defaults + persistence round-trip") {
    // 隔离数据目录 : persistence::data_dir () 决 定 $HOME/.agentty.
    auto tmp = fs::temp_directory_path()
             / ("agentty_settings_test_" + std::to_string(::getpid()));
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    ::setenv("HOME", tmp.c_str(), 1);
    // Settings/user-scope 路径解析通过 util::user_root
    // () ($AGENTTY_HOME ,其他则 $HOME/.agentty () 两者都保留在 沙箱.
    ::unsetenv("AGENTTY_HOME");   // 倒车 $HOME/.agentty
    ::unsetenv("USERPROFILE");   // data_dir () 更喜欢这个 Windows 清除它

    const auto settings_json = tmp / ".agentty" / "settings.json";

    // (1) 结构默认值——默认-constructed设置所持有的值。
    check(store::Settings{}.profile == Profile::Write,
          "Settings{}.profile defaults to Write");

    // (2) 新鲜安装:目前没有持续进行 __ 加载 倒车 Write.
    check(!fs::exists(settings_json), "no settings.json present (fresh install)");
    check(persistence::load_settings(::agentty::IoAccess::grant()).profile == Profile::Write,
          "load_settings() with no file -> Write");

    // (3) Legacy/partial 配置在日期之前的 "profile" 键 + Write.
    fs::create_directories(tmp / ".agentty");
    {
        std::ofstream ofs(settings_json, std::ios::trunc);
        ofs << R"({"model_id":"claude-x","favorite_models":[]})";
    }
    check(persistence::load_settings(::agentty::IoAccess::grant()).profile == Profile::Write,
          "load_settings() with settings.json missing 'profile' -> Write");

    // (4) 非 -default 选择仍然圆 -trips - 证明 Write
    //     默认不是掩盖破损 解析器 (Ask/Minimal 将永不动,而重装。
    {
        store::Settings s; s.profile = Profile::Ask;
        persistence::save_settings(::agentty::IoAccess::grant(), agtest::cap(), s);
    }
    check(persistence::load_settings(::agentty::IoAccess::grant()).profile == Profile::Ask,
          "save/load round-trips a non-default profile (Ask)");
    {
        store::Settings s; s.profile = Profile::Minimal;
        persistence::save_settings(::agentty::IoAccess::grant(), agtest::cap(), s);
    }
    check(persistence::load_settings(::agentty::IoAccess::grant()).profile == Profile::Minimal,
          "save/load round-trips Minimal");

    // (5)沙箱:空出端口列表保持空出.
    {
        store::Settings s;
        s.sandbox.configured = true;
        s.sandbox.allow_ports.clear();
        persistence::save_settings(::agentty::IoAccess::grant(), agtest::cap(), s);
    }
    check(persistence::load_settings(::agentty::IoAccess::grant()).sandbox.allow_ports.empty(),
          "an empty allow_ports list round-trips as empty");

    // (6) 坏 点 沙箱 值保留默认值而不是断开 加载.
    {
        std::ofstream ofs(settings_json, std::ios::trunc);
        ofs << R"({"profile":1,"sandbox":{"configured":true,"net_mode":9,)"
               R"("syscall_mode":-1,"memory_mb":"lots","wx_protect":"yes"},)"
               R"("rag":{"configured":true}})";
    }
    {
        const auto s = persistence::load_settings(::agentty::IoAccess::grant());
        const sandbox_cfg::Config def;
        check(s.sandbox.net_mode == def.net_mode, "out-of-range net_mode keeps the default");
        check(s.sandbox.syscall_mode == def.syscall_mode, "negative syscall_mode keeps the default");
        check(s.sandbox.memory_mb == def.memory_mb, "string memory_mb keeps the default");
        check(s.sandbox.wx_protect == def.wx_protect, "string wx_protect keeps the default");
        check(s.rag.configured, "a bad sandbox value doesn't drop later sections");
    }

    fs::remove_all(tmp);
}

TEST_CASE("sandbox user paths resolve to absolute") {
    using sandbox_cfg::resolve_user_path;
    check(resolve_user_path("~/.cargo", "/w", "/home/u") == "/home/u/.cargo", "~/x expands");
    check(resolve_user_path("~", "/w", "/home/u") == "/home/u", "~ expands");
    check(resolve_user_path("build/out/", "/w", "/home/u") == "/w/build/out",
          "relative resolves against the workspace, trailing / dropped");
    check(resolve_user_path("/opt/x/../y", "/w", "/home/u") == "/opt/y", "absolute is normalised");
    check(resolve_user_path("~/x", "/w", "").empty(), "~ with no home is dropped");
    check(resolve_user_path("/", "/w", "/home/u") == "/", "/ stays /");
}

TEST_CASE("settings rows harden bad input") {
    namespace S = agentty::settings;
    auto tmp = fs::temp_directory_path()
             / ("agentty_settings_harden_" + std::to_string(::getpid()));
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    ::setenv("HOME", tmp.c_str(), 1);
    // Settings/user-scope 路径解析通过 util::user_root
    // () ($AGENTTY_HOME ,其他则 $HOME/.agentty () 两者都保留在 沙箱.
    ::unsetenv("AGENTTY_HOME");   // 倒车 $HOME/.agentty
    ::unsetenv("USERPROFILE");

    // create_starter: a ':'-namespaced 命令变成一个 SUBDIRECTORY
    // 源码树 (git: 修补 - > commands/git/fixup.md ),
    // 匹配装填器,而从未写过 ':' 变成文件名( 非法使用 Windows).
    {
        auto r = S::create_starter(::agentty::IoAccess::grant(), S::Category::Commands, "git:fixup");
        check(r.ok, "create_starter nests a colon name");
        check(fs::is_regular_file(tmp / ".agentty" / "commands" / "git" / "fixup.md"),
              "colon name written as git/fixup.md subdir tree");
        check(!fs::exists(tmp / ".agentty" / "commands" / "git:fixup.md"),
              "no literal-colon filename is created");
    }
    // 普通名是单文件.
    {
        auto r = S::create_starter(::agentty::IoAccess::grant(), S::Category::Commands, "deploy");
        check(r.ok && fs::is_regular_file(tmp / ".agentty" / "commands" / "deploy.md"),
              "plain name -> deploy.md");
    }
    // 重复被拒绝,而不是默默地覆写。
    check(!S::create_starter(::agentty::IoAccess::grant(), S::Category::Commands, "deploy").ok,
          "create_starter refuses an existing file");
    // Path-escape和非法形状被拒绝在前面。
    check(!S::create_starter(::agentty::IoAccess::grant(), S::Category::Commands, "../evil").ok,
          "'..' path escape rejected");
    check(!S::create_starter(::agentty::IoAccess::grant(), S::Category::Commands, "a/b").ok,
          "raw slash rejected");
    check(!S::create_starter(::agentty::IoAccess::grant(), S::Category::Commands, ".hidden").ok,
          "leading-dot segment rejected");
    check(!S::create_starter(::agentty::IoAccess::grant(), S::Category::Commands, "").ok,
          "empty name rejected");
    check(!S::create_starter(::agentty::IoAccess::grant(), S::Category::Commands, std::string(200, 'x')).ok,
          "over-long name rejected");
    check(!S::create_starter(::agentty::IoAccess::grant(), S::Category::Commands, "a:b:c:d").ok,
          "too many nesting levels rejected");

    // add_plugin_from_line : 错误 标志 名字被拒绝,
    // 一个非 -existent --python 脚本被否决,接受好的谱.
    check(!S::add_plugin_from_line(::agentty::IoAccess::grant(), agtest::cap(), "--http http://x").ok,
          "leading-dash name (misplaced flag) rejected");
    check(!S::add_plugin_from_line(::agentty::IoAccess::grant(), agtest::cap(), "tool --python /no/such/script_xyz.py").ok,
          "non-existent --python script rejected");
    {
        auto script = tmp / "srv.py";
        std::ofstream(script) << "print('hi')\n";
        auto r = S::add_plugin_from_line(::agentty::IoAccess::grant(), agtest::cap(), "mysrv --python " + script.string());
        check(r.ok, "valid --python plugin accepted");
    }

    fs::remove_all(tmp);
}
