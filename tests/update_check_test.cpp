// SPDX-License-Identifier: Apache-2.0
//
// update_check_test.cpp — 锁 自我 -update 机械的纯逻辑:
// 版本比较( 用于 "is an update available" ),
// 平台资产命名,以及 缓存-staleness 规则(a) 缓存
// 由年长者撰写 二进制程序 ”), 也即是说,

#include "agentty/util/update.hpp"

#include <doctest/doctest.h>

using agentty::update::version_less;
using agentty::update::platform_asset;
using agentty::update::current_version;

TEST_CASE("version_less: dotted semver ordering") {
    // 更新一点
    CHECK(version_less("0.3.0", "0.3.1"));
    CHECK(version_less("0.3.0", "0.4.0"));
    CHECK(version_less("0.3.9", "0.10.0"));   // 数字, 而非词典
    CHECK(version_less("0.9.9", "1.0.0"));

    // 等值/年长——从未出现"available".
    CHECK(!version_less("0.3.0", "0.3.0"));
    CHECK(!version_less("0.3.1", "0.3.0"));
    CHECK(!version_less("1.0.0", "0.9.9"));

    // 双方均容忍领导 v. GitHub 标记是 vX.Y.Z).
    CHECK(version_less("v0.3.0", "v0.3.1"));
    CHECK(version_less("0.3.0", "v0.3.1"));
    CHECK(!version_less("v0.3.1", "0.3.1"));

    // 缺少组件为零。
    CHECK(version_less("0.3", "0.3.1"));
    CHECK(!version_less("0.3.0", "0.3"));

    // 数字三进制后的拖曳垃圾被忽略( 前) -release 标记
    // 与其底数比较——保守:0.4.0 -rc1 计为0.4.0。
    CHECK(version_less("0.3.0", "0.4.0-rc1"));

    // 垃圾从不坠毁 并比较为0.0。
    CHECK(!version_less("garbage", "0.0.0"));
    CHECK(version_less("garbage", "0.0.1"));
}

TEST_CASE("platform_asset matches release naming") {
    // 无论测试运行的平台是什么,资产必须是发布
    // 工作流程实际公布的名称之一.
    auto a = platform_asset();
    CHECK(!a.empty());
    CHECK(a.starts_with("agentty-"));
}

TEST_CASE("current_version is the compiled-in triple") {
    auto v = current_version();
    CHECK(!v.empty());
    // 形状 : digits.digits.digits (无) project() ),该版本.
    int dots = 0;
    for (char c : v) if (c == '.') ++dots;
    CHECK(dots == 2);
}

#include "agentty/auth/auth.hpp"
#include "agentty/util/user_root.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

TEST_CASE("cached_check: fresh cache honoured, stale/foreign rejected") {
    namespace fs = std::filesystem;
    // 沙箱 用户根 首次 : 这个案件写道: update_check.json ,
    // 没有 $AGENTTY_HOME 覆盖开发者真实的 ~/.agentty/cache.
    // (用户) -root 绊线中止 二进制程序 如果测试到达真正的根—
    // —看 util/user_root.cpp.)
    const auto sandbox = fs::temp_directory_path() /
        ("agentty_update_check_" + std::to_string(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()));
    std::error_code sec;
    fs::create_directories(sandbox, sec);
#if defined(_WIN32)
    _putenv_s("AGENTTY_HOME", sandbox.string().c_str());
#else
    ::setenv("AGENTTY_HOME", sandbox.string().c_str(), 1);
#endif

    // update_check.json 移动到 cache/ 单曲中的子目录 -root
    // 合并(见 util/user_root.hpp ); 写在哪里 cached_check
    // ()现在读,而不是旧的 config_dir 根节。
    const auto cache = agentty::util::user_cache_dir() / "update_check.json";
    auto now_s = std::chrono::duration_cast<std::chrono::seconds>(
                     std::chrono::system_clock::now().time_since_epoch())
                     .count();
    auto write = [&](const std::string& body) {
        std::ofstream f(cache, std::ios::trunc);
        f << body;
    };

    // 1. 新鲜 缓存 作者: 刘克华 此 二进制程序 —— 荣誉。
    write("{\"checked_at\":" + std::to_string(now_s) +
          ",\"current\":\"" + current_version() + "\"," +
          "\"latest\":\"99.0.0\",\"target\":\"99.0.0\",\"url\":\"u\",\"update_available\":true}");
    auto c = agentty::update::cached_check();
    REQUIRE(c.has_value());
    CHECK(c->update_available);
    CHECK(c->latest == "99.0.0");
    CHECK(c->target == "99.0.0");

    // 1b. A 缓存 从之前 `target` 已存在 —— 被拒绝。
    //     update_available 谓 "a newer tag exists" ,
    //     则显示的#119没有 "you can install it".
    write("{\"checked_at\":" + std::to_string(now_s) +
          ",\"current\":\"" + current_version() + "\"," +
          "\"latest\":\"99.0.0\",\"url\":\"u\",\"update_available\":true}");
    CHECK(!agentty::update::cached_check().has_value());

    // 1c. "still building" 继续 过期 几
    //     分钟之内,不是一天:发行即将可以安装,明天之前不得隐藏.
    write("{\"checked_at\":" + std::to_string(now_s - 30 * 60) +
          ",\"current\":\"" + current_version() + "\"," +
          "\"latest\":\"99.0.0\",\"target\":\"\",\"pending\":true,\"url\":\"u\","
          "\"update_available\":false}");
    CHECK(!agentty::update::cached_check().has_value());

    // 2. 缓存 由 A 撰写 不同 (老人) 谢天谢地,我... 二进制程序 − 驳回:在
    //    更新新文本 二进制程序 绝不能重回 -announce 三角洲刚刚关闭。
    write("{\"checked_at\":" + std::to_string(now_s) +
          ",\"current\":\"0.0.1\"," +
          "\"latest\":\"99.0.0\",\"target\":\"99.0.0\",\"url\":\"u\",\"update_available\":true}");
    CHECK(!agentty::update::cached_check().has_value());

    // 3. 过期的缓存( > 24h) − 被拒绝。
    write("{\"checked_at\":" + std::to_string(now_s - 90'000) +
          ",\"current\":\"" + current_version() + "\"," +
          "\"latest\":\"99.0.0\",\"target\":\"99.0.0\",\"url\":\"u\",\"update_available\":true}");
    CHECK(!agentty::update::cached_check().has_value());

    // 4. 腐败 JSON − 被拒绝,不扔。
    write("{not json");
    CHECK(!agentty::update::cached_check().has_value());

    // 拆掉: 放下枪 沙箱 外面没有东西被触动 所以没有真正
    // 的 缓存 恢复( 预 -isolation 此测试的版本
    // save/restored 开发商的实际更新邮票).
    fs::remove_all(sandbox, sec);
}
