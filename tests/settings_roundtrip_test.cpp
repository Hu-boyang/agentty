// settings_roundtrip_test - 每个用户 -visible 设置存活到 保存
// 和重新装入,通过 真实 settings.json 路径。
//
// 报告是 "settings don't seem to apply and persist, like scrollback".
// 外表 外表 状态更新函数 ,表单投影和实地绘图都经过
// 检查后都很好,正因为如此,这是一次测试,而不是一次读
// 数:一个设定可以正确 内存 并且仍然未能回来,
// 唯一可以知道的方法就是把它写到磁盘上,然后读回来.
//
// 每个案件都移动一个字段 OFF 默认( 默认完全不写, 所以圆)
// -tripping 默认证明什么, 保存 ,擦干净 -process 状态,
// 重装,比较。

#include "agtest.hpp"

#include "agentty/domain/ui_prefs.hpp"
#include "agentty/io/persistence.hpp"
#include "agentty/store/store.hpp"
#include "agentty/util/io.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;
namespace up = agentty::ui_prefs;
namespace persistence = agentty::persistence;

namespace {

// 私人事业 AGENTTY_HOME 每个案件,所以 过期 settings.json
// 从另一个测试或开发者自己的安装中永远无法满足一个断言.
struct TmpHome {
    fs::path dir;
    explicit TmpHome(const char* tag) {
        dir = fs::temp_directory_path()
            / (std::string{"agentty-settings-rt-"} + tag + "-"
               + std::to_string(::getpid()));
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        ::setenv("AGENTTY_HOME", dir.c_str(), 1);
    }
    ~TmpHome() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

agentty::store::Settings save_and_reload(const agentty::store::Settings& s) {
    const auto io = ::agentty::IoAccess::grant();
    maya::thread_root([&](const maya::unlocked& c) {
        persistence::save_settings(io, c, s);
    });
    return persistence::load_settings(io);
}

}  // 命名空间结束。

TEST_CASE("settings round-trip: every appearance pref survives a reload") {
    TmpHome home{"ui"};

    agentty::store::Settings s;
    // 每个字段都移了 OFF 默认。默认被故意省略
    // settings.json ,所以值等于它圆
    // -trips 并且不会证明它应该覆盖的领域
    s.ui.theme            = "nord";
    s.ui.lang             = "de";
    s.ui.tier             = up::ColorTier::Ansi256;
    s.ui.polarity         = up::Polarity::Light;
    s.ui.density          = up::Density::Roomy;
    s.ui.prose_width      = 88;
    s.ui.compact_turns    = true;
    s.ui.thread_preview   = false;
    s.ui.foreign_sessions = false;
    s.ui.motion           = up::Motion::Reduced;
    s.ui.scrollback       = up::Scrollback::Small;
    s.ui.syntax           = false;
    s.ui.tool_output      = up::ToolOutput::Full;
    s.ui.thinking         = up::Thinking::Shown;
    s.ui.timestamps       = up::Timestamps::Relative;
    REQUIRE(s.ui != up::Prefs{});

    const auto back = save_and_reload(s);

    // 逐字段,所以一个失败命名了设置而不是"ui differs".
    CHECK(back.ui.theme            == s.ui.theme);
    CHECK(back.ui.lang             == s.ui.lang);
    CHECK(back.ui.tier             == s.ui.tier);
    CHECK(back.ui.polarity         == s.ui.polarity);
    CHECK(back.ui.density          == s.ui.density);
    CHECK(back.ui.prose_width      == s.ui.prose_width);
    CHECK(back.ui.compact_turns    == s.ui.compact_turns);
    CHECK(back.ui.thread_preview   == s.ui.thread_preview);
    CHECK(back.ui.foreign_sessions == s.ui.foreign_sessions);
    CHECK(back.ui.motion           == s.ui.motion);
    CHECK(back.ui.scrollback       == s.ui.scrollback);
    CHECK(back.ui.syntax           == s.ui.syntax);
    CHECK(back.ui.tool_output      == s.ui.tool_output);
    CHECK(back.ui.thinking         == s.ui.thinking);
    CHECK(back.ui.timestamps       == s.ui.timestamps);
    // 整个结构,它捕捉一个田地 之后加进普雷斯, 从来没有连接到
    // 保存 或 或 时 间 加载.
    CHECK(back.ui == s.ui);
}

TEST_CASE("settings round-trip: each scrollback level survives a reload") {
    // 所报字段,每个值 -- -- 包括Full(0行)和Tiny,
    // 两端 枚举 关闭时 -by-one 或夹子会咬人。
    for (auto lvl : {up::Scrollback::Tiny, up::Scrollback::Small,
                     up::Scrollback::Big, up::Scrollback::Full}) {
        TmpHome home{"sb"};
        agentty::store::Settings s;
        s.ui.scrollback = lvl;
        const auto back = save_and_reload(s);
        INFO("scrollback level " << static_cast<int>(lvl));
        CHECK(back.ui.scrollback == lvl);
    }
}

TEST_CASE("settings round-trip: one instance's save does not undo another's") {
    // 阿尤什经营着几个 agentty 中。每个进程都拥有自己的 -memory
    // 从启动开始, 绝不重设 -reads 文档,和 `ui` 合并为
    // 一个 顶端 -level 键。所以:
    //
    //   A和B都从 回滚缓冲区 亚特
    //   变化 回滚缓冲区 - > 小型和 保存.
    //   B改变一些东西 ELSE 输入 `ui` (主题)和 保存.
    //
    // B's `ui` 现在与B的基线(主题)不同,所以合并处理
    // 整个 块作为 B 编辑 - B 块仍然保留启动
    // -era 回滚缓冲区 =Auto. A的更改默默地在磁盘上恢复.
    // 就是这样 "settings don't persist" 他们做到了,直到一个兄弟 已保存.
    TmpHome home{"clobber"};
    const auto io = ::agentty::IoAccess::grant();

    // 两例加载 相同的起始文件.
    agentty::store::Settings start;
    start.ui.theme = "nord";
    maya::thread_root([&](const maya::unlocked& c) {
        persistence::save_settings(io, c, start);
    });
    agentty::store::Settings a = persistence::load_settings(io);
    agentty::store::Settings b = a;        // B 已加载 同样的 字节
    // 每个实例都记得文件 AS IT LOADED IT
    // 测试过程有一个基线,所以现在抓B,在B之前交还 保存.
    std::string b_baseline;
    {
        std::ifstream f(home.dir / "settings.json");
        b_baseline.assign(std::istreambuf_iterator<char>(f), {});
    }

    // 一项改变:回滚缓冲区。
    a.ui.scrollback = up::Scrollback::Small;
    maya::thread_root([&](const maya::unlocked& c) {
        persistence::save_settings(io, c, a);
    });
    REQUIRE(persistence::load_settings(io).ui.scrollback == up::Scrollback::Small);

    // B,从未见过A的编辑,它会改变它的主题。
    // 首先恢复B的世界观:在真正的第二个过程中,这只是它的内存。
    persistence::debug_set_settings_baseline(b_baseline);
    b.ui.theme = "gruvbox";
    maya::thread_root([&](const maya::unlocked& c) {
        persistence::save_settings(io, c, b);
    });

    const auto disk = persistence::load_settings(io);
    CHECK(disk.ui.theme == "gruvbox");                  // B 编辑器已登陆
    CHECK(disk.ui.scrollback == up::Scrollback::Small);  // 和A的幸存下来
}

TEST_CASE("settings round-trip: resetting to defaults keeps a sibling's edit") {
    // 第一次的洞 每一个 -field 合并。当每个 ui 字段默认时,
    // 刻录器会省略 整个 块,所以合并后将B视为已放弃了每个字段...
    // "we changed it" 和B的失踪 回滚缓冲区 击败A's Small。
    // 缺席意味着默认,而不是 "no opinion".
    TmpHome home{"reset"};
    const auto io = ::agentty::IoAccess::grant();

    agentty::store::Settings start;
    start.ui.theme = "nord";
    maya::thread_root([&](const maya::unlocked& c) {
        persistence::save_settings(io, c, start);
    });
    agentty::store::Settings a = persistence::load_settings(io);
    agentty::store::Settings b = a;
    std::string b_baseline;
    {
        std::ifstream f(home.dir / "settings.json");
        b_baseline.assign(std::istreambuf_iterator<char>(f), {});
    }

    a.ui.scrollback = up::Scrollback::Small;
    maya::thread_root([&](const maya::unlocked& c) {
        persistence::save_settings(io, c, a);
    });

    // B将其主题放回默认——它的ui现在是ALL默认,
    // 所以它完全没有写ui块.
    persistence::debug_set_settings_baseline(b_baseline);
    b.ui.theme.clear();
    REQUIRE(b.ui == up::Prefs{});
    maya::thread_root([&](const maya::unlocked& c) {
        persistence::save_settings(io, c, b);
    });

    const auto disk = persistence::load_settings(io);
    CHECK(disk.ui.theme.empty());                        // B的重置着陆
    CHECK(disk.ui.scrollback == up::Scrollback::Small);  // A的编辑幸存下来
}

TEST_CASE("settings round-trip: every ui field merges on its own") {
    // 合并需要每个 ui 字段及其默认的表格。
    // 表中缺少的字段不是合并的; 它只是跟任意一个会话
    // 块一起被写入。 所以走每个字段: 只有修改一个字段,
    // B 更改一个无关的字段, A 就必须存活下来。
    // 添加一个字段到 Prefs 中, 而表在此失败。
    using Set = void (*)(up::Prefs&);
    struct Case { const char* name; Set set; };
    const Case cases[] = {
        {"theme",            [](up::Prefs& p) { p.theme = "nord"; }},
        {"lang",             [](up::Prefs& p) { p.lang = "de"; }},
        {"tier",             [](up::Prefs& p) { p.tier = up::ColorTier::Ansi256; }},
        {"polarity",         [](up::Prefs& p) { p.polarity = up::Polarity::Light; }},
        {"density",          [](up::Prefs& p) { p.density = up::Density::Roomy; }},
        {"prose_width",      [](up::Prefs& p) { p.prose_width = 88; }},
        {"compact_turns",    [](up::Prefs& p) { p.compact_turns = true; }},
        {"thread_preview",   [](up::Prefs& p) { p.thread_preview = false; }},
        {"foreign_sessions", [](up::Prefs& p) { p.foreign_sessions = false; }},
        {"motion",           [](up::Prefs& p) { p.motion = up::Motion::Reduced; }},
        {"scrollback",       [](up::Prefs& p) { p.scrollback = up::Scrollback::Small; }},
        {"syntax",           [](up::Prefs& p) { p.syntax = false; }},
        {"tool_output",      [](up::Prefs& p) { p.tool_output = up::ToolOutput::Full; }},
        {"thinking",         [](up::Prefs& p) { p.thinking = up::Thinking::Shown; }},
        {"timestamps",       [](up::Prefs& p) { p.timestamps = up::Timestamps::Relative; }},
    };
    // B总是接触同一个无关的字段,每个案件都有时间戳,
    // 但自己的,它会回到原位。
    for (const auto& c : cases) {
        TmpHome home{"field"};
        const auto io = ::agentty::IoAccess::grant();
        agentty::store::Settings start = persistence::load_settings(io);
        maya::thread_root([&](const maya::unlocked& cap) {
            persistence::save_settings(io, cap, start);
        });
        agentty::store::Settings a = persistence::load_settings(io);
        agentty::store::Settings b = a;
        std::string b_baseline;
        {
            std::ifstream f(home.dir / "settings.json");
            if (f) b_baseline.assign(std::istreambuf_iterator<char>(f), {});
        }
        if (b_baseline.empty()) b_baseline = "{}";

        c.set(a.ui);
        const up::Prefs want = a.ui;
        maya::thread_root([&](const maya::unlocked& cap) {
            persistence::save_settings(io, cap, a);
        });

        persistence::debug_set_settings_baseline(b_baseline);
        if (std::string{c.name} == "timestamps") b.ui.motion = up::Motion::Off;
        else                                    b.ui.timestamps = up::Timestamps::Absolute;
        maya::thread_root([&](const maya::unlocked& cap) {
            persistence::save_settings(io, cap, b);
        });

        auto disk = persistence::load_settings(io).ui;
        // 撤销B的不相关的编辑,那么其他的必须是A的完全.
        if (std::string{c.name} == "timestamps") disk.motion = want.motion;
        else                                    disk.timestamps = want.timestamps;
        INFO("field: " << std::string{c.name});
        CHECK(disk == want);
    }
}

TEST_CASE("settings round-trip: an all-default ui writes nothing and reads back default") {
    // 三个地方是 ui 默认值: Prefs struct,
    // 装货员的 回退 ,而合并的表格。如果它们漂移,
    // 默认值会被读回作为选择(或反向),
    // 合并会错判一个会话的改变。一个原始的预览必须圆 -trip
    // 不得产生 `ui` 块。
    TmpHome home{"defaults"};
    agentty::store::Settings s;
    REQUIRE(s.ui == up::Prefs{});
    const auto back = save_and_reload(s);
    CHECK(back.ui == up::Prefs{});

    std::ifstream f(home.dir / "settings.json");
    REQUIRE(f.good());
    const std::string text{std::istreambuf_iterator<char>(f), {}};
    INFO(text);
    CHECK(text.find("\"ui\"") == std::string::npos);
}

TEST_CASE("settings round-trip: merge default table matches Prefs") {
    // 合并会填补自己表格中缺少的 ui 字段。 如果该表与
    // Prefs 默认值不一致, 将字段重置为真实默认值的
    // 兄妹看起来会选择其他的值, 而合并会错误判断哪些字段更改。
    // 直接检查 : 将每个字段重排到 Prefs 不默认值,
    // 而我们持有一个非 -default 代表 不同 字段;
    // 重置必须降落。
    TmpHome home{"table"};
    const auto io = ::agentty::IoAccess::grant();

    // 从每个字段开始,从它的默认开始,所以每个字段都有重新设置FROM
    // 的地方.
    agentty::store::Settings start;
    start.ui.theme = "nord";   start.ui.lang = "de";
    start.ui.tier = up::ColorTier::Ansi256;
    start.ui.polarity = up::Polarity::Light;
    start.ui.density = up::Density::Roomy;
    start.ui.prose_width = 88;
    start.ui.compact_turns = true;
    start.ui.thread_preview = false;
    start.ui.foreign_sessions = false;
    start.ui.motion = up::Motion::Reduced;
    start.ui.scrollback = up::Scrollback::Small;
    start.ui.syntax = false;
    start.ui.tool_output = up::ToolOutput::Full;
    start.ui.thinking = up::Thinking::Shown;
    start.ui.timestamps = up::Timestamps::Relative;
    maya::thread_root([&](const maya::unlocked& c) {
        persistence::save_settings(io, c, start);
    });
    const auto loaded = persistence::load_settings(io);
    std::string baseline;
    {
        std::ifstream f(home.dir / "settings.json");
        baseline.assign(std::istreambuf_iterator<char>(f), {});
    }

    // 将 EVERYTHING 重新定位到运出默认值和 保存。
    agentty::store::Settings sib = loaded;
    sib.ui = up::Prefs{};
    maya::thread_root([&](const maya::unlocked& c) {
        persistence::save_settings(io, c, sib);
    });

    // 我们,拿着旧的启动副本, 保存 却又不改变什么
    // 我们什么都没改变 所以每个领域都要重置兄弟的
    persistence::debug_set_settings_baseline(baseline);
    maya::thread_root([&](const maya::unlocked& c) {
        persistence::save_settings(io, c, loaded);
    });
    CHECK(persistence::load_settings(io).ui == up::Prefs{});
}

TEST_CASE("settings round-trip: a SECOND change in one session persists") {
    // 基线合并到 save_settings 比较每个顶端
    // -level 键, 文档此进程最后 已加载 或写作。
    // 第一个变化很容易;有趣的是,改变 相同 ,
    // 然后将其修改为默认值,而不会在两者之间重新启动。
    TmpHome home{"second"};
    const auto io = ::agentty::IoAccess::grant();

    agentty::store::Settings s = persistence::load_settings(io);   // 空
    s.ui.scrollback = up::Scrollback::Small;
    maya::thread_root([&](const maya::unlocked& c) {
        persistence::save_settings(io, c, s);
    });
    CHECK(persistence::load_settings(io).ui.scrollback == up::Scrollback::Small);

    // 再来一次,同一场
    s.ui.scrollback = up::Scrollback::Big;
    maya::thread_root([&](const maya::unlocked& c) {
        persistence::save_settings(io, c, s);
    });
    CHECK(persistence::load_settings(io).ui.scrollback == up::Scrollback::Big);

    // 回到运出的默认值, 整个省略 `ui` 块,合并必须读作 OUR 更改,
    // 不作为
    // 字段缺失，因此采用磁盘上已有值。
    s.ui.scrollback = up::Scrollback::Auto;
    REQUIRE(s.ui == up::Prefs{});
    maya::thread_root([&](const maya::unlocked& c) {
        persistence::save_settings(io, c, s);
    });
    CHECK(persistence::load_settings(io).ui.scrollback == up::Scrollback::Auto);
}
