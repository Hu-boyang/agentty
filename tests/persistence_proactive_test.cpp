// persistence_proactive_test - 主动性强 -context 标记 +
// 信心在真实中存活 保存 盘子 加载 圆形 -trip.
//
// 递归 锁 财务报告和审定财务报表 持久化 固定在 src/io/persistence.cpp:
// proactive_context 原为 不 序列化,所以重新装入 线程 将
// 生的浮出水面 <retrieved-context> 块作为普通用户转动。
// 现在 标志 和 检索信心(驱动记录卡的栏) -trip。这个测试驱动 PUBLIC
// 持久化 API (save_thread + flush_pending_saves
// + load_thread_file )针对孤立的 $HOME 所以它接触了
// 真正的磁盘,而不是一个模拟器。

#include "agtest.hpp"

#include "agentty/io/persistence.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>

#include <unistd.h>   // 发作

namespace fs = std::filesystem;
using namespace agentty;


TEST_CASE("persistence proactive") {
    // 隔离数据目录 : persistence::data_dir () 决 定 $HOME/.agentty.
    auto tmp = fs::temp_directory_path()
             / ("agentty_persist_test_" + std::to_string(::getpid()));
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    ::setenv("HOME", tmp.c_str(), 1);
    ::unsetenv("USERPROFILE");   // data_dir () 更喜欢这个 Windows 清除它

    Thread t;
    t.id    = persistence::new_id();
    t.title = "round-trip";

    // 普通用户转动。
    Message user;
    user.id   = MessageId{"m-user"};
    user.role = Role::User;
    user.text = "how does retry backoff work?";
    t.messages.push_back(user);

    // 积极的背景正在转变——正在经受考验。
    Message ctx;
    ctx.id                   = MessageId{"m-ctx"};
    ctx.role                 = Role::User;
    ctx.proactive = Message::ProactiveContext{.confidence = 0.82};
    ctx.text = "<retrieved-context>\n[docs:http/retry.md:12]\n"
               "exponential backoff with full jitter\n</retrieved-context>";
    t.messages.push_back(ctx);

    // 助理答复( proactive_context 不得 漏入正常转弯。
    Message reply;
    reply.id   = MessageId{"m-reply"};
    reply.role = Role::Assistant;
    reply.text = "It uses exponential backoff with full jitter, capped at 30s.";
    t.messages.push_back(reply);

    // 叉子 -provenance 注释——同一标记 -message 形状( 5)
    // Role::User + 传输-inert 标志). fork_note +
    // fork_transcript 一定要绕 -trip 故复装叉子仍 渲染 编号 "\u2443 Forked" 牌照,
    // 并仍然在母版记录中指向模型.
    Message fnote;
    fnote.id   = MessageId{"m-fork"};
    fnote.role = Role::User;
    fnote.fork_note       = true;
    fnote.fork_transcript = "/tmp/threads/parent.transcript.md";
    fnote.text = "This conversation is a fork of an earlier one. Its full "
                 "transcript is saved at:\n  /tmp/threads/parent.transcript.md";
    t.messages.push_back(fnote);

    // ── 回合-trip 通过真实磁盘 ────────────────────────────────────
    persistence::save_thread(::agentty::IoAccess::grant(), agtest::cap(), t);
    persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());

    // 穿过店里 接口边界: a 已保存 线程 现在以日志格式存在,
    // <id>.JSON 核查完毕后就退休了
    auto log_path = persistence::threads_dir() / (t.id.value + ".jsonl");
    check(fs::exists(log_path), "thread written to disk");

    auto loaded = persistence::load_thread_by_id(::agentty::IoAccess::grant(), t.id);
    check(loaded.has_value(), "thread reloaded without error");
    if (!loaded) { REQUIRE(loaded.has_value()); return; }

    const auto& msgs = loaded->messages;
    check(msgs.size() == 4, "all four messages survived");
    if (msgs.size() != 4) { REQUIRE(msgs.size() == 4); return; }

    // 主动性的转弯保持了自己的身份。
    check(msgs[1].is_proactive_context(),
          "proactive-context identity survived reload");
    check(msgs[1].proactive && msgs[1].proactive->confidence
       && *msgs[1].proactive->confidence > 0.81
       && *msgs[1].proactive->confidence < 0.83,
          "proactive confidence (0.82) survived reload");

    // 普通用户和助手都没有找到标记。
    // 正常的转弯根本就没有主动的阻力——没有哨兵可以保护,这就是重点:
    // "not a proactive message" 财务报告和已审计财务报表
    // "a proactive message with an odd confidence" 已经不再是
    // 相同的形状。
    check(!msgs[0].is_proactive_context() && !msgs[2].is_proactive_context(),
          "proactive identity did not leak onto normal turns");
    check(!msgs[0].proactive && !msgs[2].proactive,
          "normal turns carry no proactive block at all");

    // 叉子笔记保留了自己的身份和笔录指针.
    check(msgs[3].fork_note, "fork_note flag survived reload");
    check(msgs[3].fork_transcript == "/tmp/threads/parent.transcript.md",
          "fork_transcript path survived reload");
    check(msgs[3].role == Role::User,
          "fork note stays a User message (provider-proof) across reload");
    check(!msgs[0].fork_note && !msgs[1].fork_note && !msgs[2].fork_note,
          "fork_note did not leak onto other turns");

    fs::remove_all(tmp);
}

TEST_CASE("persistence: a tool in flight does not fail log verification") {
    // save_thread_sync 写入日志, 重新 -reads 并比较。
    // 只有核查成功,它才会删除遗留下来的 <id>.JSON。用于无条件检查工具输出的
    // 比较——但读者故意强迫一种工具输出。 pending/running 调用 失败{
    // "interrupted" ~ 所以,这样吧 ~ output() 谓 "" 输入
    // 内存 财务报告和已审计财务报表 "interrupted" 磁盘上。
    //
    // A 保存 着火了 每个 转弯,所以任何转弯 工具仍在运行
    // 击中的区别,记录 "log verification FAILED" ,
    // 并保存遗存文件。 -message 线程 : 37次故障,
    // 使工具完成的那一刻愈合。 什么都没有丢失,
    // 但迁移从未完成, 错误也说不出什么有用的内容。
    //
    // 这驱动器 真实 保存 带有工具中间的路径 -flight
    // 并声称迁移完成反正。
    auto tmp = fs::temp_directory_path()
             / ("agentty_inflight_test_" + std::to_string(::getpid()));
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    ::setenv("HOME", tmp.c_str(), 1);
    ::unsetenv("USERPROFILE");

    Thread t;
    t.id    = persistence::new_id();
    t.title = "in flight";

    Message user;
    user.id   = MessageId{"m-user"};
    user.role = Role::User;
    user.text = "run something";
    t.messages.push_back(user);

    // 助理转过身来 状态更新函数 保存 工具为 仍然 RUNNING.
    Message asst;
    asst.id   = MessageId{"m-asst"};
    asst.role = Role::Assistant;
    asst.text = "running it now";
    ToolUse tc;
    tc.id     = ToolCallId{"call-live"};
    tc.name   = ToolName{"shell"};
    tc.status = ToolUse::Running{};
    asst.tool_calls.push_back(tc);
    t.messages.push_back(asst);

    persistence::save_thread(::agentty::IoAccess::grant(), agtest::cap(), t);
    persistence::flush_pending_saves(::agentty::IoAccess::grant(), agtest::cap());

    const auto log_path    = persistence::threads_dir() / (t.id.value + ".jsonl");
    const auto legacy_path = persistence::threads_dir() / (t.id.value + ".json");

    check(fs::exists(log_path), "log written with a tool in flight");
    // 该 ASSERTION 该 WAS FAILING IN PRODUCTION
    // : 核查通过,所以遗留文件被退役而不是永远保存.
    check(!fs::exists(legacy_path),
          "legacy document retired despite the in-flight tool");

    // 转弯仍然 装满完好,与 调用 已制作 终端 所以重装
    // 从不等待 在一个工具 它的进程已经消失。
    auto loaded = persistence::load_thread_by_id(::agentty::IoAccess::grant(), t.id);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->messages.size() == 2u);
    REQUIRE(loaded->messages[1].tool_calls.size() == 1u);
    check(loaded->messages[1].tool_calls[0].is_terminal(),
          "the in-flight call reloaded terminal, not pending");
    check(loaded->messages[1].text == "running it now",
          "the assistant text survived");

    fs::remove_all(tmp);
}
