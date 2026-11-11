// SPDX-License-Identifier: Apache-2.0
//
// settings_multiproc_test.cpp - 两个 agentty PROCESSES 编辑设置。
//
// persistence_race_test 驱动器很多 THREADS
// 通过 保存 队列,这就是为什么这个文件的标针被忽略: 作者们
// 从来没有处于同样的过程。 save_settings 串连呼叫
// 者的全部内容 -memory 记录和解剖式替换文档,
// 因此两例相互丢失编辑,没有失败:
//
//     A开头(主题=Harper) B开头(主题+Harper)B:挑
//     "Sumi Phosphor" - > 文件说Sumi
//     Phosphoor A: 更改 任意 其他行 - > A 写其 STALE
//     记录 - > 主题又是哈珀
//
// 真实的会话报告, 共打开5个实例: 主题选择在重启时继续回放,
// 且看起来并不确定, 因为胜者是哪个过程发生时的胜者。
// 保存 最后一个 write_json_atomic 毫无帮助,
// 它保证不 TORN 文件,它与否是不同的属性 LOST 更新。
// 这两篇文章都是完整、有效和持久的;一幅土地第二。
//
// 不变量 钉住 :
//   • 两个进程不同步编辑 两者 存活( 已报告的错误)
//   • 过期作家不会恢复从未触及的领域
//   • 并行保存者不破坏文件
//   • 推理 -off 将永远关闭, 不 内容 -read 作为自动
//
// 最后一个不是在这里 effort_auto_test 因为未设置
// -vs-off 拆分仅可观察到 ACROSS a save/load 边界,
// 以及 `""` 在那之前,天上的一切,都是由於它的一生, `none`.

#include "agentty/io/persistence.hpp"
#include "agtest.hpp"
#include "agentty/store/store.hpp"
#include "agentty/domain/catalog.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>   // 发作

#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;
namespace ps = agentty::persistence;

namespace {

// 将用户商店指向此案件的抓取根。
void use_store(const fs::path& root) {
    fs::create_directories(root);
#ifdef _WIN32
    _putenv_s("AGENTTY_HOME", root.string().c_str());
#else
    ::setenv("AGENTTY_HOME", root.string().c_str(), 1);
#endif
}

fs::path scratch(const char* name) {
    auto p = fs::temp_directory_path() /
             ("agentty-mp-" + std::string{name} + "-" + std::to_string(::getpid()));
    fs::remove_all(p);
    return p;
}

json read_raw(const fs::path& root) {
    std::ifstream ifs(root / "settings.json");
    REQUIRE(ifs.good());
    json j; ifs >> j;
    return j;
}

// 模拟 a 第二 实例: 已加载 将文件写在更早的某个点,
// 并写出自己的完整记录,完全如另一个进程所愿。写出原始文件是
// 诚实的模型——另一个进程有自己的静态,因此重用这个进程的
// 静态 save_settings 将共享合并基线,
// 但证明无任何结果。
void other_instance_writes(const fs::path& root, const json& doc) {
    std::ofstream ofs(root / "settings.json", std::ios::binary | std::ios::trunc);
    ofs << doc.dump(2);
}

}  // 命名空间结束。

TEST_CASE("settings: a stale instance does not revert another's theme") {
    // 报告虫子,形状准确
    const auto root = scratch("revert");
    use_store(root);

    // 实例A开始并读取商店。这也是A的合并基线,
    // 它从第"I merely read this"起就告诉
    // "I changed this"。
    {
        agentty::store::Settings s;
        s.ui.theme = "Harper";
        ps::save_settings(::agentty::IoAccess::grant(), agtest::cap(), s);
    }
    auto a_view = ps::load_settings(::agentty::IoAccess::grant());
    REQUIRE(a_view.ui.theme == "Harper");

    // 例B(另一个过程)选取一个新的主题和保存.
    {
        auto doc = read_raw(root);
        doc["ui"]["theme"] = "Sumi Phosphor";
        other_instance_writes(root, doc);
    }

    // 现在A 保存 a 数据为: UNRELATED 设置, 从启动时开始 -era 记录。
    // 合并前,请写 ui.theme Harper直接回到B的选手。
    a_view.model_id = agentty::ModelId{"gpt-5"};
    ps::save_settings(::agentty::IoAccess::grant(), agtest::cap(), a_view);

    const auto after = read_raw(root);
    CHECK(after["ui"]["theme"] == "Sumi Phosphor");   // B的编辑幸存下来了
    CHECK(after["model_id"] == "gpt-5");              // A 编辑器已登陆

    fs::remove_all(root);
}

TEST_CASE("settings: the writer's OWN edit still wins over a stale disk") {
    // 另一方向,所以合并不能是 "always take disk"
    // ——通过使每一次 保存 否 否 : -op.
    const auto root = scratch("mine");
    use_store(root);

    {
        agentty::store::Settings s;
        s.ui.theme = "Harper";
        ps::save_settings(::agentty::IoAccess::grant(), agtest::cap(), s);
    }
    auto mine = ps::load_settings(::agentty::IoAccess::grant());

    // 这一过程有意地改变主题.
    mine.ui.theme = "Sumi Linen";
    ps::save_settings(::agentty::IoAccess::grant(), agtest::cap(), mine);

    CHECK(read_raw(root)["ui"]["theme"] == "Sumi Linen");

    fs::remove_all(root);
}

TEST_CASE("settings: reasoning off persists as off, not as unset") {
    // `effort: ""` 自设置存在以来, 一直是默认的,
    // 以及 持久化 空值的空值——如此 "off" 写为 "" 这
    // 将是 ABSENT 磁盘上,然后读回自动,默默取消每次重启时的选择。
    // 关闭必须持续为非 -empty 哨兵 哨兵
    const auto root = scratch("effort");
    use_store(root);

    agentty::store::Settings s;
    // "off"-effort_persist(Effort::None)
    // 的PERSISTED形式是引入的固定的哨兵. 通过传输拼写写
    // Effort::None会给予"",而这正是错误.
    s.effort = std::string{agentty::effort_to_wire_setting(agentty::Effort::None)};
    ps::save_settings(::agentty::IoAccess::grant(), agtest::cap(), s);

    const auto raw = read_raw(root);
    REQUIRE(raw.contains("effort"));
    CHECK(raw["effort"].get<std::string>() == "none");   // 否 "" 数据

    // 它在往返途中以离场而不是以自動的方式贬低自己。
    const auto back = ps::load_settings(::agentty::IoAccess::grant());
    CHECK(agentty::effort_from_wire(back.effort) == agentty::Effort::None);

    fs::remove_all(root);
}

// ── web_search政策(安排_网络搜索). ─────────────────────────────
// `search` 正在离开 settings.json 直到
// 一行摆脱默认, 这使得它成为第一个可以 兄弟姐妹的块 ADD
// 一次之后 已加载。这些将连接合并的三种方式固定起来。

TEST_CASE("settings: a sibling's new web_search block survives an unrelated save") {
    const auto root = scratch("search-sibling");
    use_store(root);
    ps::save_settings(::agentty::IoAccess::grant(), agtest::cap(), agentty::store::Settings{});   // 无 `search` 键
    auto a = ps::load_settings(::agentty::IoAccess::grant());                     // A的基线:无`search`

    // B关闭网络搜索。
    {
        auto doc = read_raw(root);
        doc["web_search"] = {{"mode", "off"}};
        other_instance_writes(root, doc);
    }

    // A 保存 还有其他的,它从来没有 意见 `search`.
    a.model_id = agentty::ModelId{"gpt-5"};
    ps::save_settings(::agentty::IoAccess::grant(), agtest::cap(), a);

    const auto after = read_raw(root);
    REQUIRE(after.contains("web_search"));
    CHECK(after["web_search"]["mode"] == "off");   // B的选择保留下来了
    CHECK(after["model_id"] == "gpt-5");
    fs::remove_all(root);
}

TEST_CASE("settings: resetting web search to defaults persists") {
    const auto root = scratch("search-reset");
    use_store(root);
    {
        agentty::store::Settings s;
        s.web_search.count = 5;
        ps::save_settings(::agentty::IoAccess::grant(), agtest::cap(), s);
    }
    auto mine = ps::load_settings(::agentty::IoAccess::grant());
    REQUIRE(mine.web_search.count == 5);

    mine.web_search = agentty::web_search_cfg::Config{};   // 默认情况下每行返回
    ps::save_settings(::agentty::IoAccess::grant(), agtest::cap(), mine);

    CHECK(ps::load_settings(::agentty::IoAccess::grant()).web_search.count == 10);   // 磁盘中未显示 re-adopted
    fs::remove_all(root);
}

TEST_CASE("settings: a web search env override is never written to settings.json") {
    const auto root = scratch("search-env");
    use_store(root);
#ifdef _WIN32
    _putenv_s("AGENTTY_WEB_SEARCH_COUNT", "4");
#else
    ::setenv("AGENTTY_WEB_SEARCH_COUNT", "4", 1);
#endif
    auto s = ps::load_settings(::agentty::IoAccess::grant());
    CHECK(s.web_search.count == 4);                // 出口生效
    s.model_id = agentty::ModelId{"gpt-5"};
    ps::save_settings(::agentty::IoAccess::grant(), agtest::cap(), s);                      // 任何保存 数据
#ifdef _WIN32
    _putenv_s("AGENTTY_WEB_SEARCH_COUNT", "");
#else
    ::unsetenv("AGENTTY_WEB_SEARCH_COUNT");
#endif

    const auto after = read_raw(root);
    CHECK((!after.contains("web_search") || !after["web_search"].contains("count")));
    CHECK(ps::load_settings(::agentty::IoAccess::grant()).web_search.count == 10);   // 随导出而去
    fs::remove_all(root);
}

TEST_CASE("settings: the scrollback level survives a restart") {
    const auto root = scratch("scrollback");
    use_store(root);
    CHECK(ps::load_settings(::agentty::IoAccess::grant()).ui.scrollback
          == agentty::ui_prefs::Scrollback::Auto);   // 默认
    {
        auto s = ps::load_settings(::agentty::IoAccess::grant());
        s.ui.scrollback = agentty::ui_prefs::Scrollback::Tiny;
        ps::save_settings(::agentty::IoAccess::grant(), agtest::cap(), s);
    }
    CHECK(ps::load_settings(::agentty::IoAccess::grant()).ui.scrollback
          == agentty::ui_prefs::Scrollback::Tiny);
    // 新建建筑的值读作默认值,而不是垃圾值。
    auto doc = read_raw(root);
    doc["ui"]["scrollback"] = 9;
    other_instance_writes(root, doc);
    CHECK(ps::load_settings(::agentty::IoAccess::grant()).ui.scrollback
          == agentty::ui_prefs::Scrollback::Auto);
    fs::remove_all(root);
}
