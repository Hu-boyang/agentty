// SPDX-License-Identifier: Apache-2.0
//
// update_ux_test - 自我 - 自我 -update's 可见 行为,而不是算术。
//
// update_check_test 已发夹 version_less /
// platform_asset / 数据 缓存 规则。这些都不是用户体验的。他们体验的是:
//
//   * a ~15 MB 下载时不能像绞刑一样,而且
//   * 完成的更新已经 不 实际拍摄 副作用 却...
//     运行过程会保留它开始的图像,所以仍然欠一个
//     重新启动。
//
// 都软弱无力 TUI 调用 perform_update ()
// 毫无进展 回调 完全(贝壳路径一直经过一个),所以状态线冻结在
// "⬆ downloading agentty…" 成功时,
// "⬆ vX.Y.Z" 标签 曾经是 CLEARED ,留下一个单一
// 状态线作为唯一的痕迹——一个繁忙的会话在几秒钟内滚动过去,之后屏幕上没有
// 任何内容说更新正在等待.
//
// 这些能驱动真正的 meta_update 状态更新函数.

#include <chrono>
#include <string>

#include "agtest.hpp"

#include "agentty/runtime/app/deps.hpp"
#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/msg.hpp"
#include "agentty/util/update.hpp"

namespace A = agentty;
namespace D = agentty::app::detail;

namespace {

// 电话 状态更新函数 武器在试验中不依赖,但 install_deps 一定是
// 跑来跑去的 Model 已经可以使用。
void install_stub_deps() {
    A::app::install_deps(A::app::Deps{
        .stream        = [](const maya::unlocked&, A::provider::Request, A::provider::EventSink) {},
        .save_thread   = [](const A::Thread&) {},
        .delete_thread = [](const A::ThreadId&) {},
    });
}

A::Model updating_model(const char* to_version) {
    install_stub_deps();
    A::Model m;
    m.s.update_latest    = to_version;
    m.s.update_in_flight = true;
    m.s.status           = "⬆ downloading agentty v" + std::string{to_version} + "…";
    return m;
}

bool has(const std::string& hay, const char* needle) {
    return hay.find(needle) != std::string::npos;
}

} // 命名空间结束。

TEST_CASE("update: finding a release starts the download without being asked") {
    install_stub_deps();
    // 我们是否可以取代 二进制程序 发射时读作 m.env.
    // 驱动两个答案,而不是询问现场安装。
    for (const bool ok : {true, false}) {
        A::Model m;
        m.env.self_update_ok = ok;

        auto [next, cmd] = D::step(::agentty::app::detail::meta_update, std::move(m),
            A::msg::MetaMsg{A::UpdateCheckDone{true, "9.9.9",
                                               "https://example/releases"}});

        CHECK(next.s.update_latest == "9.9.9");
        // 没有调色板旅行,没有键盘中键,当我们可以更新;安装时没有触动到任何不是我们的
        // (package management / nix / Windows).
        CHECK(next.s.update_in_flight == ok);
    }
}

TEST_CASE("update: a staged update does not start a second download") {
    install_stub_deps();
    A::Model m;
    m.s.update_pending_restart = "9.9.9";   // 已安装, 等待重新启动

    auto [next, cmd] = D::step(::agentty::app::detail::meta_update, std::move(m),
        A::msg::MetaMsg{A::UpdateCheckDone{true, "9.9.9", "https://example"}});

    // 调 -downloading 已经写入磁盘的版本将覆盖已
    // 播放的 二进制程序 毫无裨益,而且遵循正道, 链接 也会屡次做,
    // 因为小时民意调查 总是发现同样的释放。
    CHECK(!next.s.update_in_flight);
}

TEST_CASE("update: an in-flight download is not restarted by another check") {
    install_stub_deps();
    A::Model m;
    m.s.update_in_flight = true;

    auto [next, cmd] = D::step(::agentty::app::detail::meta_update, std::move(m),
        A::msg::MetaMsg{A::UpdateCheckDone{true, "9.9.9", "https://example"}});

    CHECK(next.s.update_in_flight);   // 仍然为 ORIGINAL 单曲
}

TEST_CASE("update: download progress reaches the status line") {
    auto m = updating_model("1.2.3");
    const std::string before = m.s.status;

    // 4 MiB 16国 MiB.
    auto [next, _] = D::step(::agentty::app::detail::meta_update, std::move(m),
        A::msg::MetaMsg{A::UpdateProgress{4u * 1024 * 1024,
                                               16u * 1024 * 1024}});

    CHECK(next.s.status != before);          // 来,来,来 MOVED - 这就是重点
    CHECK(has(next.s.status, "25%"));
    CHECK(has(next.s.status, "4.0"));
    CHECK(has(next.s.status, "16.0"));
    // 进展不得在中期到期 -download 留下一个空白的酒吧。
    CHECK(next.s.status_until == std::chrono::steady_clock::time_point{});
}

TEST_CASE("update: unknown content-length shows bytes, not a fake percentage") {
    auto m = updating_model("1.2.3");

    // 总计=0 = = 该服务器未发送Content-Length。
    auto [next, _] = D::step(::agentty::app::detail::meta_update, std::move(m),
        A::msg::MetaMsg{A::UpdateProgress{2u * 1024 * 1024, 0}});

    CHECK(has(next.s.status, "2.0"));
    // 发明 "0%" 或 或 时 间 "100%" 没有什么是 关于进步的谎言。
    CHECK(!has(next.s.status, "%"));
}

TEST_CASE("update: progress arriving after the download is ignored") {
    install_stub_deps();
    A::Model m;
    m.s.update_in_flight = false;            // 已经 finished/failed
    m.s.status           = "✓ updated to v1.2.3 — restart agentty to use it";
    const std::string before = m.s.status;

    auto [next, _] = D::step(::agentty::app::detail::meta_update, std::move(m),
        A::msg::MetaMsg{A::UpdateProgress{1u * 1024 * 1024,
                                               4u * 1024 * 1024}});

    // 晚点 -flight 回调 不得以 过期
    // "downloading…" 线条。
    CHECK(next.s.status == before);
}

TEST_CASE("update: a finished update still asks for a restart, persistently") {
    auto m = updating_model("1.2.3");

    auto [next, _] = D::step(::agentty::app::detail::meta_update, std::move(m), A::msg::MetaMsg{A::UpdateApplied{true, "1.2.3"}});

    CHECK(!next.s.update_in_flight);
    // 电话 "available" 信号已经用完...
    CHECK(next.s.update_latest.empty());
    // ..但是,我们... RESTART 还在欠下,在田间说,
    // 状态栏 渲染 每隔一段时间 帧 ——不仅在卷走的线条上.
    CHECK(next.s.update_pending_restart == "1.2.3");
    CHECK(has(next.s.status, "restart"));
    CHECK(next.s.status_until == std::chrono::steady_clock::time_point{});   // 粘着
}

TEST_CASE("update: a failed update leaves the offer in place") {
    auto m = updating_model("1.2.3");

    auto [next, _] = D::step(::agentty::app::detail::meta_update, std::move(m),
        A::msg::MetaMsg{A::UpdateApplied{false, "connection reset"}});

    CHECK(!next.s.update_in_flight);
    // 什么都没有安装,所以没有 待处理 要求以
    // 其他方式将永久停用 标签 以失败为代价
    CHECK(next.s.update_pending_restart.empty());
    // 而更新内容仍可提供至重试.
    CHECK(next.s.update_latest == "1.2.3");
    CHECK(has(next.s.status, "connection reset"));
}
