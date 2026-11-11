// SPDX-License-Identifier: Apache-2.0
//
// mcp_connection_race_test.cpp - 丢掉一个 mcp::Connection
// 当读取器在服务器内时 回调.
//
// 读者的 处理函数 运行时请在连接的Live上保持强烈的反读。
// 如果连接随后消失,读者最后会得到最后的反读,然后~ 对端
// 关闭自己的 池 读者: jaal 中止。 在一个 英寸
// -memory 频道发送 notifications/message
// 日志 回调 测试破坏连接时设置了选项。
//
// 运行在 : -DAGENTTY_SANITIZE_ALL=thread( 无问题 ) 下。

#include "agentty/mcp/connection.hpp"
#include "agentty/rpc/peer.hpp"
#include "agtest_cap.hpp"

#include <mcp/methods.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>

namespace m = ::mcp;
using namespace std::chrono_literals;

namespace {

int g_failures = 0;
void fail(const char* what) {
    std::printf("  FAIL: %s\n", what);
    ++g_failures;
}

std::atomic<bool> g_in_callback{false};
std::atomic<bool> g_dropping{false};

// 假服务器:回答server/discover和tools/list,别无其他.
agentty::rpc::Dispatch server_dispatch() {
    agentty::rpc::Dispatch d;
    d.on_call = [](const maya::unlocked&, const jsonrpc::Call& c) -> jsonrpc::Maybe<std::string> {
        if (c.method == m::method::Discover) {
            m::DiscoverResult r;
            r.supportedVersions = {std::string(m::kProtocolVersion)};
            return jsonrpc::reply(c.id, m::to_json(r));
        }
        if (c.method == m::method::ListTools)
            return jsonrpc::reply(c.id, m::to_json(m::ListToolsResult{}));
        return jsonrpc::reply(c.id, std::unexpected(jsonrpc::RpcError(
            jsonrpc::errc::MethodNotFound, "not here")));
    };
    return d;
}

// 一轮: 连接、 拥有服务器日志、 将连接在读取器仍在
// on_log 内时放下。 在修复之前, 中止。
bool drop_during_callback(int round) {
    auto [client_end, server_end] = agentty::rpc::channel_pair();
    agentty::rpc::Peer server(std::move(server_end), server_dispatch(), "fake-server");
    server.start();

    agentty::mcp::ConnectionConfig cfg;
    cfg.name = "fake";
    cfg.handshake_timeout = 5s;
    cfg.on_log = [](const m::LoggingMessageParams&) {
        g_in_callback = true;
        // 将读取器停在这里,直到所有者开始放弃连接,
        // 然后再多一点,这样投放会与回调重叠.
        while (!g_dropping) std::this_thread::sleep_for(1ms);
        std::this_thread::sleep_for(50ms);
    };
    agentty::mcp::Link link;
    link.open = [ch = std::move(client_end)]() {
        return agentty::mcp::Link::Opened{ch, nullptr};
    };

    g_in_callback = false;
    g_dropping = false;
    auto conn = std::make_unique<agentty::mcp::Connection>(std::move(cfg), std::move(link));

    server.notify_raw(agtest::cap(), "notifications/message",
                      jsonrpc::Json{{"level", "info"}, {"data", "round " + std::to_string(round)}});
    const auto give_up = std::chrono::steady_clock::now() + 5s;
    while (!g_in_callback && std::chrono::steady_clock::now() < give_up)
        std::this_thread::sleep_for(1ms);
    if (!g_in_callback) { fail("the log notification never reached on_log"); return false; }

    g_dropping = true;
    conn.reset();   // 读者仍在on_log
    server.stop(agtest::cap());
    return true;
}

// A 调用 排队于卡后 调用 在同一服务器上,
// 当停止起火时必须放弃。在等待之前,队列会忽略停止并挂起。
std::atomic<bool> g_release{false};
std::atomic<bool> g_in_call{false};

void queued_call_cancels() {
    auto [client_end, server_end] = agentty::rpc::channel_pair();
    auto d = server_dispatch();
    auto base = d.on_call;
    d.on_call = [base](const maya::unlocked& cap, const jsonrpc::Call& c) -> jsonrpc::Maybe<std::string> {
        if (c.method == m::method::CallTool) {
            g_in_call = true;
            while (!g_release) std::this_thread::sleep_for(1ms);
            m::CallToolResult r;
            return jsonrpc::reply(c.id, m::to_json(r));
        }
        return base(cap, c);
    };
    agentty::rpc::Peer server(std::move(server_end), std::move(d), "fake-server");
    server.start();

    agentty::mcp::ConnectionConfig cfg;
    cfg.name = "fake";
    cfg.handshake_timeout = 5s;
    agentty::mcp::Link link;
    link.open = [ch = std::move(client_end)]() {
        return agentty::mcp::Link::Opened{ch, nullptr};
    };
    g_release = false;
    g_in_call = false;
    auto conn = std::make_unique<agentty::mcp::Connection>(std::move(cfg), std::move(link));

    std::thread first([&] {
        m::cap::Request req; req.tool = "slow";
        (void)conn->execute(req);
    });
    const auto give_up = std::chrono::steady_clock::now() + 5s;
    while (!g_in_call && std::chrono::steady_clock::now() < give_up)
        std::this_thread::sleep_for(1ms);
    if (!g_in_call) fail("the first call never reached the server");

    std::stop_source stop;
    std::atomic<bool> second_done{false};
    m::cap::Result second_r;
    std::thread second([&] {
        m::cap::Request req; req.tool = "queued"; req.stop = stop.get_token();
        second_r = conn->execute(req);
        second_done = true;
    });
    std::this_thread::sleep_for(50ms);   // 让它排在第一个后面
    stop.request_stop();
    const auto by = std::chrono::steady_clock::now() + 2s;
    while (!second_done && std::chrono::steady_clock::now() < by)
        std::this_thread::sleep_for(1ms);
    if (!second_done) fail("a cancelled call stayed queued behind a stuck one");
    else if (!second_r.is_error) fail("a cancelled queued call reported success");

    g_release = true;
    first.join();
    second.join();
    conn.reset();
    server.stop(agtest::cap());
}

}  // 命名空间结束。

int main() {
    std::printf("--- mcp connection: dropped while its reader runs a callback ---\n");
    for (int i = 0; i < 10; ++i)
        if (!drop_during_callback(i)) break;
    std::printf("--- mcp connection: a queued call honours its stop ---\n");
    queued_call_cancels();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
