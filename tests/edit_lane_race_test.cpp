// edit_lane_race_test ——同时编辑 一个 文件不得相互丢失。
//
// ── 原因 ──────────────────────────────────────────────────────────────────
//
// 编辑/ apply_patch / 写作读取 -modify-write
// : 读取 字节 计算变化, write_atomic 结果。
// 父工具调度器保留自己的工具 调用 一条路是分开的,但这不是唯一
// 的呼叫者。 `task` 子剂调度为 { ReadFs , Net],
// 所以几个跑一次 和旁边的父母的作者, 每一个发送工具 直接
// 通过 DynamicDispatch。两个编辑相同的文件,
// 然后互页为读 A,读 B,写 A,写 B: B的写法是从“ ” 中
// 计算出来的。 字节 之前,和沉默地放弃了A的改变, 而 两者 调用
// 报告取得了成功。
//
// 修补是每个 -path 工具桥中的写道。此测试驱动真实的
// `edit` 工具从许多 线程 在同一文件,每个文件都会替换
// 自己的显著标记。每个替换的通道都存活下来;没有它,
// 就失去了其中一部分(文件结尾处还有仍然在其中的标记)。
#include "agtest.hpp"

#include "agentty/tool/registry.hpp"
#include "agentty/tool/tool.hpp"
#include "agentty/tool/util/fs_helpers.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

fs::path scratch(const char* tag) {
    auto dir = agentty::tools::util::workspace_root() / "build" / "test-scratch";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir / ("edit_lane_" + std::string{tag} + ".txt");
}

void put(const fs::path& p, std::string_view body) {
    std::ofstream o(p, std::ios::binary | std::ios::trunc);
    o.write(body.data(), static_cast<std::streamsize>(body.size()));
}

std::string get(const fs::path& p) {
    std::ifstream i(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(i)), std::istreambuf_iterator<char>());
}

std::string marker(int i) { return "TODO_SLOT_" + std::to_string(i) + "_END"; }
std::string done(int i)   { return "DONE_SLOT_" + std::to_string(i) + "_END"; }

} // 命名空间结束。

TEST_CASE("edit lane: concurrent edits of one file each land, none is lost") {
    constexpr int kThreads = 12;
    const auto p = scratch("same_file");

    std::string body;
    for (int i = 0; i < kThreads; ++i) body += "line " + marker(i) + "\n";
    put(p, body);

    // 排队 线程 所以他们的读数尽可能重叠
    std::atomic<int> ready{0};
    std::atomic<bool> go{false};
    std::atomic<int> ok{0};
    std::vector<std::thread> ws;
    ws.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) {
        ws.emplace_back([&, i] {
            ready.fetch_add(1);
            while (!go.load()) std::this_thread::yield();
            auto r = agentty::tool::DynamicDispatch::execute(
                agtest::cap(), "edit",
                json{{"path", p.string()},
                     {"edits", json::array({json{{"old_text", marker(i)},
                                                 {"new_text", done(i)}}})}});
            if (r) ok.fetch_add(1);
        });
    }
    while (ready.load() < kThreads) std::this_thread::yield();
    go.store(true);
    for (auto& w : ws) w.join();

    const auto after = get(p);
    CHECK(ok.load() == kThreads);
    for (int i = 0; i < kThreads; ++i) {
        CHECK_MESSAGE(after.find(done(i)) != std::string::npos,
                      "edit ", i, " reported success but its change was lost");
        CHECK_MESSAGE(after.find(marker(i)) == std::string::npos,
                      "marker ", i, " still present: an edit was overwritten");
    }
    std::error_code ec;
    fs::remove(p, ec);
}

TEST_CASE("edit lane: edits of DIFFERENT files are not serialised into failure") {
    constexpr int kThreads = 6;
    std::vector<fs::path> files;
    for (int i = 0; i < kThreads; ++i) {
        files.push_back(scratch(("distinct_" + std::to_string(i)).c_str()));
        put(files.back(), "x " + marker(i) + "\n");
    }
    std::atomic<int> ok{0};
    std::vector<std::thread> ws;
    for (int i = 0; i < kThreads; ++i)
        ws.emplace_back([&, i] {
            auto r = agentty::tool::DynamicDispatch::execute(
                agtest::cap(), "edit",
                json{{"path", files[i].string()},
                     {"edits", json::array({json{{"old_text", marker(i)},
                                                 {"new_text", done(i)}}})}});
            if (r) ok.fetch_add(1);
        });
    for (auto& w : ws) w.join();
    CHECK(ok.load() == kThreads);
    for (int i = 0; i < kThreads; ++i) {
        CHECK(get(files[i]).find(done(i)) != std::string::npos);
        std::error_code ec;
        fs::remove(files[i], ec);
    }
}

// ── 车道表本身(write_lanes.hpp) ─────────────────────────────────
#include "agentty/tool/write_lanes.hpp"

namespace wl = agentty::tools::write_lanes;

TEST_CASE("write lanes: a writer that names no file holds the whole workspace") {
    agentty::tools::CallContext ctx;
    // git_commit / 一个 MCP 工具:写,名称无。
    const auto any = wl::lanes_for("git_commit", json::object(), ctx);
    REQUIRE(any.size() == 1);
    CHECK(any[0] == std::string{wl::kWholeWorkspace});
    // 它与每个文件道相撞,所以它独自在作家之间运行.
    CHECK(wl::overlap(any[0], "/ws/src/a.c"));
    CHECK(wl::overlap("/ws/src/a.c", any[0]));
    // 非 国 家 -string 或空路径为 "unsure" 这条路也是整个车道
    CHECK(wl::lanes_for("edit", json{{"path", 7}}, ctx)[0] == std::string{wl::kWholeWorkspace});
    CHECK(wl::lanes_for("edit", json{{"path", ""}}, ctx)[0] == std::string{wl::kWholeWorkspace});
}

TEST_CASE("write lanes: overlap is by path prefix, on a separator boundary") {
    CHECK(wl::overlap("/ws/src", "/ws/src/a.c"));
    CHECK(wl::overlap("/ws/src/a.c", "/ws/src/a.c"));
    CHECK_FALSE(wl::overlap("/ws/src", "/ws/srcfoo/a.c"));
    CHECK_FALSE(wl::overlap("/ws/a.c", "/ws/b.c"));
}

TEST_CASE("write lanes: a call cancelled while queued gives up, holds nothing") {
    agentty::tools::CallContext ctx;
    const auto lanes = wl::lanes_for("edit", json{{"path", scratch("cancel").string()}}, ctx);
    std::stop_source cancel;
    agentty::tools::CallContext queued_ctx;
    queued_ctx.cancel.push_back(cancel.get_token());

    wl::Hold first{agtest::cap(), lanes, ctx};
    REQUIRE(first.held());
    std::atomic<bool> gave_up{false};
    std::thread waiter([&] {
        wl::Hold second{agtest::cap(), lanes, queued_ctx};
        gave_up = !second.held();
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(150));   // 让它排队
    cancel.request_stop();
    waiter.join();
    CHECK(gave_up.load());
    CHECK(wl::held_count(agtest::cap()) == lanes.size());   // 仅限 `first`'s
}

TEST_CASE("write lanes: crossing claims (move A->B beside B->A) never deadlock") {
    agentty::tools::CallContext ctx;
    const auto a = scratch("cross_a").string();
    const auto b = scratch("cross_b").string();
    const auto ab = wl::lanes_for("move", json{{"source", a}, {"destination", b}}, ctx);
    const auto ba = wl::lanes_for("move", json{{"source", b}, {"destination", a}}, ctx);
    std::atomic<int> done{0};
    std::vector<std::thread> ws;
    for (int i = 0; i < 16; ++i)
        ws.emplace_back([&, i] {
            for (int k = 0; k < 50; ++k) {
                wl::Hold h{agtest::cap(), (i % 2) ? ab : ba, ctx};
                REQUIRE(h.held());
            }
            done.fetch_add(1);
        });
    for (auto& w : ws) w.join();   // a 死锁 挂在这里( 测试) TIMEOUT)
    CHECK(done.load() == 16);
    CHECK(wl::held_count(agtest::cap()) == 0);
}
