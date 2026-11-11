// mcp_reload_race_test ——同时连接+ 快照 千万别崩溃
//
// 递归 锁 财务报告和审定财务报表 SIGSEGV 插件面板连接运行于何处 task_isolated
// 工作线程: connect_initial_mcp (在《公约》之下) 注册表 缓存 (m) 和
// (c) 与 mcp_reload () (无) reload_mu ) 既可以运行
// mcp::mcp_tools (一) 一次( "date connected 4×" 并同时进行
// 赛车比赛 a plugin_model () 读取器崩溃, 没有中止消息。
//
// 固定序列化 全部 在一个桥下连接+swap 互斥量 (g_connect_mu ),
// 所以没有两层结构重叠。这个测试锤子精确地将连接:
// 数量 线程 重新装入 + 快照 同样的 池 一次,对一个真正的(
// tiny) stdio MCP 服务器。它必须用一个稳定 快照
// 并且没有崩溃/TSan投诉(套房在消毒器标签下运行).

#include "agtest.hpp"

#include "agentty/tool/registry.hpp"
#include "agentty/mcp/client.hpp"

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

namespace fs = std::filesystem;
using namespace agentty;


// 最低的阶梯 MCP 服务器作为 shell 脚本——回声罐
// JSON-RPC 足以进行连接握手(初始化+) tools/list 所以,
// 我们 池 生孩子,并列出一个工具
static fs::path write_fake_server(const fs::path& dir) {
    const fs::path sh = dir / "fake_mcp.sh";
    std::ofstream f(sh);
    f << R"SH(#!/bin/sh
# Minimal MCP stdio server: reply to initialize + tools/list, ignore the rest.
while IFS= read -r line; do
  case "$line" in
    *'"method":"initialize"'*)
      printf '%s\n' '{"jsonrpc":"2.0","id":1,"result":{"protocolVersion":"2025-06-18","capabilities":{"tools":{"listChanged":false}},"serverInfo":{"name":"fake","version":"1.0.0"}}}' ;;
    *'"method":"tools/list"'*)
      printf '%s\n' '{"jsonrpc":"2.0","id":2,"result":{"tools":[{"name":"ping","description":"ping","inputSchema":{"type":"object","properties":{}}}]}}' ;;
  esac
done
)SH";
    f.close();
    fs::permissions(sh, fs::perms::owner_all);
    return sh;
}

TEST_CASE("mcp reload race") {
    // 全球情况相同 SIGPIPE 忽略 agentty 安装——在
    // 握手时产卵服务器死亡的,不得杀死我们(这个试验产卵) /
    // usr/bin/false 用于在手握手前退出。
#ifdef SIGPIPE   // Windows 没有 SIGPIPE : 管道损坏是写错误
    std::signal(SIGPIPE, SIG_IGN);
#endif
    // 死了一个 -on-spawn 服务器(/) usr/bin/false
    // )否则会燃烧每轮全连接的最后期限——缩小其长度,使币种压力迅速变大。
    // 我们正在测试 BUILD/SWAP/READ 竞态 ,不连接延迟。
    ::setenv("AGENTTY_MCP_CONNECT_TIMEOUT_MS", "150", 1);
    auto tmp = fs::temp_directory_path()
             / ("agentty_mcp_race_" + std::to_string(::getpid()));
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    ::setenv("HOME", tmp.c_str(), 1);
    // mcp.json 被看上层 util::user_root() ($AGENTTY_HOME,
    // else $HOME/.agentty); keep both pointing at this sandbox.
    ::unsetenv("AGENTTY_HOME");   // 倒车 $HOME/.agentty
    ::unsetenv("USERPROFILE");

    const fs::path server = write_fake_server(tmp);
    // mcp.json 下级 $HOME/.agentty 这么说吧
    // mcp_config_present (找到它) 我们使用命令 EXITS
    // IMMEDIATELY (`false` :握手失败很快,但 竞态 测试是同时进行
    // 的 mcp_tools() BUILD + 池 互换 + plugin_model
    // () 读作——无论儿童是否握手,都会发生这种情况。 -lived 服务器只会让
    // 每回合都变慢; 快速 -fail 保持货币压力,并挂 -free.
    (void)server;
    fs::create_directories(tmp / ".agentty");
    {
        std::ofstream cfg(tmp / ".agentty" / "mcp.json");
        cfg << R"({"mcpServers":{"a":{"command":"/usr/bin/false"},)"
               R"("b":{"command":"/usr/bin/false"}}})";
    }

    // 竞态：N 条线程重新加载连接池，同时 M 条线程获取快照，
    //    都马上来,先 -fix 这个 SIGSEGV ”d; 员额 -fix 已经连载了
    constexpr int kReloaders = 6;
    constexpr int kSnappers  = 6;
    constexpr int kRounds    = 3;

    std::atomic<bool> go{false};
    std::atomic<int>  crashes{0};   // 逃避工作线程的任何例外
    std::atomic<std::size_t> max_servers{0};
    std::vector<std::thread> ts;

    for (int i = 0; i < kReloaders; ++i)
        ts.emplace_back([&]{
            while (!go.load()) std::this_thread::yield();
            for (int r = 0; r < kRounds; ++r) {
                try { (void)tools::reload_mcp_plugins(); }
                catch (...) { ++crashes; }
            }
        });
    for (int i = 0; i < kSnappers; ++i)
        ts.emplace_back([&]{
            while (!go.load()) std::this_thread::yield();
            for (int r = 0; r < kRounds * 4; ++r) {
                try {
                    auto m = mcp::plugin_model();
                    std::size_t n = m.servers.size();
                    std::size_t prev = max_servers.load();
                    while (n > prev && !max_servers.compare_exchange_weak(prev, n)) {}
                } catch (...) { ++crashes; }
            }
        });

    go.store(true);
    for (auto& t : ts) t.join();

    check(crashes.load() == 0,
          "no worker threw/crashed under concurrent reload+snapshot ("
          + std::to_string(crashes.load()) + " failures)");

    // 最终的快照必须一致:两个配置的服务器都存在(
    // 连接=假是好的——点不是崩溃+一个稳定的读数).
    auto final = mcp::plugin_model();
    check(final.servers.size() == 2,
          "final snapshot has both configured servers (got "
          + std::to_string(final.servers.size()) + ")");

    // Windows : 数据 池's sh.exe 孩子们可能仍然会把
    // 临时目录打开(他们的cwd或他们正在读的脚本). POSIX 删除
    // a 源码树 离开在跑步的孩子; Windows 拒绝,直到所有柄都不见了,
    // 所以给短 -lived 服务器一个要退出的节点,
    // 然后删除 BEST-EFFORT : 烂到不行的, 永远不要对什么
    // 是清理进行红色测试 竞态 ,而不是这个测试的回归 锁.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    std::error_code ec;
    fs::remove_all(tmp, ec);   // 吞:如果孩子还在,就带上它
}

// ── 销毁-during-handshake ────────────────────────────────────────────────
// 递归 锁 外地 SIGSEGV (碰撞报告 2026-08-
// 16): 线程 最后一个掉下来的 ConnectionPool
// 参考文献 (-) 提供者撕毁 线程 B还剩 内部 提供商
// 的连接握手。 -fix 摧毁者没有等待,
// 发动机被解脱 在请求 线程 脚
// 员额 -fix 销毁器在 reconnect_mu_ 并停在公园,
// 直到握手解决。
//
// 复制:a SLOW 假服务器( 在回答初始化前睡觉) ,
// 因此握手窗口宽, 加上连接期限比睡眠短。 mcp_tools
// () 放弃 待处理 未来和回归——然后我们立即放弃 池 已
// 丢弃时的控件 工作线程 还在中 -handshake 预览
// -fix 此值将死 ~ 每一次运行; 邮箱 -fix 死神,
// 将死神, 线程 保持未来的供养者的生命 以及它的毁灭者等待连接
static fs::path write_slow_server(const fs::path& dir) {
    const fs::path sh = dir / "slow_mcp.sh";
    std::ofstream f(sh);
    f << R"SH(#!/bin/sh
while IFS= read -r line; do
  case "$line" in
    *'"method":"initialize"'*)
      sleep 1
      printf '%s\n' '{"jsonrpc":"2.0","id":1,"result":{"protocolVersion":"2025-06-18","capabilities":{"tools":{"listChanged":false}},"serverInfo":{"name":"slow","version":"1.0.0"}}}' ;;
    *'"method":"tools/list"'*)
      printf '%s\n' '{"jsonrpc":"2.0","id":2,"result":{"tools":[{"name":"ping","description":"ping","inputSchema":{"type":"object","properties":{}}}]}}' ;;
  esac
done
)SH";
    f.close();
    fs::permissions(sh, fs::perms::owner_all);
    return sh;
}

TEST_CASE("mcp destroy during handshake") {
#ifdef SIGPIPE   // Windows 没有 SIGPIPE : 管道损坏是写错误
    std::signal(SIGPIPE, SIG_IGN);
#endif
    auto tmp = fs::temp_directory_path()
             / ("agentty_mcp_dtor_race_" + std::to_string(::getpid()));
    fs::remove_all(tmp);
    fs::create_directories(tmp / ".agentty");
    ::setenv("HOME", tmp.c_str(), 1);
    ::unsetenv("USERPROFILE");

    const fs::path server = write_slow_server(tmp);
    {
        std::ofstream cfg(tmp / ".agentty" / "mcp.json");
        cfg << R"({"mcpServers":{"slow":{"command":")" << server.string()
            << R"("}}})";
    }
    // 截止日期( 50ms) 远短于服务器初始化睡眠(1s) :
    // 每轮都弃置静物 -handshaking 连接 工作线程.
    ::setenv("AGENTTY_MCP_CONNECT_TIMEOUT_MS", "50", 1);

    for (int round = 0; round < 3; ++round) {
        mcp::PoolHandle pool;
        (void)mcp::mcp_tools(pool);      // 放弃了待处理握手
        pool.reset();                    // 调用手柄
        mcp::release_servers();          // 放弃进程 -wide 手柄 当前,
                                         // 时 工作线程 中间
        // -handshake 预览 -fix : 废弃的 工作线程 提供
        // 商在握手时可能被摧毁 线程 → SIGSEGV 此处( 不需要
        // 断言 ) — 空难 IS 失败。 竞态 击打到火。
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    check(true, "no crash across 3 abandon+destroy rounds");

    // 最后一个 工作线程's `sleep 1` 在删除脚本之前
    // 握手 -running 孩子,你这孩子
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    fs::remove_all(tmp);
}
