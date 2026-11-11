// fork_test.cpp - 叉子 - 叉头 状态更新函数 (fork_update / ForkThread).
//
// 叉 = 逃避完整上下文窗口 CHEAPLY。
// 它确实 不 复制记录稿,然后 不 简略(两者都仍然花费窗口)。
// FRESH 线程 载着微小的 `fork_note`
// 消息( 内容) Role::User,
// fork_note 上面写着 父母的笔录 模特儿在要求时会读到
//   1. 叉子确实 不 输入一个压缩(无摘要——旧的)
//      设计摘要,将整份记录稿寄到模型上,
//      击中"prompt too long",仍为成本背景).
//   2. 叉子就在附近 -empty : 它携带无 user/assistant 从
//      父信件—— 准确的fork_note信件( 笔录指针)。
//   3. 种子说明是: Role::User fork_note (不 系统——
//      ChatGPT/Responses 传输层 中间 -thread
//      系统信息,它携带 记录路径,及其 传输 文本提及父关系。
//   4. 新鲜 线程 身份 + 身份 属性 forked_from 来源 + 父 代号
//      不变+每架 -choice RAG 超音速 超音调

#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/model.hpp"
#include "agentty/runtime/app/deps.hpp"
#include "agtest_fx.hpp"

#include <optional>
#include <print>
#include <string>
#include <vector>

using namespace agentty;
namespace detail = agentty::app::detail;

namespace {

int g_failed = 0;
void check(bool cond, const std::string& msg) {
    if (!cond) { std::println("  FAIL: {}", msg); ++g_failed; }
}

int g_thread_ids = 0;
void install_stub_deps() {
    agentty::app::install_deps(agentty::app::Deps{
        .stream        = [](const maya::unlocked&, provider::Request, provider::EventSink) {},
        .save_thread   = [](const agentty::Thread&) {},
        .delete_thread = [](const agentty::ThreadId&) {},
        .load_threads  = [] { return std::vector<agentty::Thread>{}; },
        .load_thread   = [](const ThreadId&) { return std::optional<agentty::Thread>{}; },
        .load_settings = [] { return store::Settings{}; },
        .save_settings = [](const store::Settings&) {},
        .new_thread_id = [] { return ThreadId{"fork-" + std::to_string(++g_thread_ids)}; },
        .title_from    = [](std::string_view) { return std::string{}; },
        .auth          = {},
    });
}

Model make_parent() {
    Model m;
    m.d.current.id = ThreadId{"parent"};
    m.d.current.title = "Some conversation";
    for (int i = 0; i < 4; ++i) {
        Message u; u.role = Role::User;      u.text = "q" + std::to_string(i);
        Message a; a.role = Role::Assistant; a.text = "a" + std::to_string(i);
        m.d.current.messages.push_back(std::move(u));
        m.d.current.messages.push_back(std::move(a));
    }
    return m;
}

Model fork_with(Model m, int choice_index) {
    auto s1 = detail::step(detail::fork_update, std::move(m), OpenFork{});
    Model m1 = std::move(s1.first);
    for (int i = 0; i < choice_index; ++i) {
        auto s = detail::step(detail::fork_update, std::move(m1), ForkMove{+1});
        m1 = std::move(s.first);
    }
    const auto choice = m1.ui.panel.get<ui::panel::Fork>()
                            ? m1.ui.panel.get<ui::panel::Fork>()->choice
                            : fork_panel::Choice::RagOff;
    const std::string parent_id = m1.d.current.id.value;
    auto s2 = detail::step(detail::fork_update, std::move(m1), ForkThread{});
    // ForkThread只有 保存 的父方,并开始笔录写,
    // 如果要求它,则用它会发送的答复来播放工作线程的部分。
    if (agtest::fx::count<agentty::SaveThread>(s2.second) == 0) return std::move(s2.first);
    auto s3 = detail::step(detail::fork_update, std::move(s2.first),
        ForkTranscriptWritten{parent_id, choice, "/tmp/" + parent_id + ".transcript.md"});
    return std::move(s3.first);
}

void fork_is_an_effect() {
    std::println("--- fork_is_an_effect ---");
    auto s1 = detail::step(detail::fork_update, make_parent(), OpenFork{});
    auto s2 = detail::step(detail::fork_update, std::move(s1.first), ForkThread{});
    check(agtest::fx::count<agentty::SaveThread>(s2.second) == 1,
          "ForkThread saves the parent");
    check(s2.first.d.current.id.value == "parent",
          "the fork waits for the transcript; the parent is still current");

    // 已留下的用户的线程的回复被删除。
    auto s3 = detail::step(detail::fork_update, std::move(s2.first),
        ForkTranscriptWritten{"someone-else", fork_panel::Choice::RagOff, "/x"});
    check(s3.first.d.current.id.value == "parent",
          "a stale transcript reply does not fork");
    std::println("PASS\n");
}

void fresh_cheap_fork(int choice, const char* name) {
    std::println("--- fresh_cheap_fork: {} ---", name);
    Model forked = fork_with(make_parent(), choice);

    // 1. 无紧固物——叉子确实有 不 汇总( 窗口成本)
    //    击打 "prompt too long" )这是一个崭新的开始。
    //    具体地说:没有紧凑的飞行 和 叉子上没有 CompactionRecord
    //    (无) 旧 设计一个,倒塌 传输 复选前缀; 读取 -on-demand
    //    设计必须离开 `compactions` 空的,
    //    这样 wire_messages_for_impl 发送叉的转盘逐字记录。
    check(!forked.s.compacting, "fork does NOT summarize (no compaction)");
    check(forked.d.current.compactions.empty(),
          "fork carries NO CompactionRecord (read-on-demand, not summarize)");

    // 2. 近 处 -empty : 父母的8个转弯不是 转过来的,没错
    //    单人 fork_note 信件( 打开) -disk 笔录指针).
    check(forked.d.current.messages.size() == 1,
          "fork carries no parent turns, just the note (got " +
          std::to_string(forked.d.current.messages.size()) + " messages)");
    for (const auto& msg : forked.d.current.messages) {
        // 3. 种子说明是: Role::User fork_note — 不 a 系统
        //    信息,哪些 ChatGPT/Responses 传输层 就会掉下去
        check(msg.role == Role::User,
              "the seeded note is a User message (survives every provider)");
        check(msg.fork_note,
              "the seeded note is flagged fork_note (quiet card + wire-kept)");
        check(!msg.text.empty(),
              "the fork note carries wire text the model actually reads");
        check(msg.text.find("fork") != std::string::npos,
              "the fork note text tells the model it's a fork");
        check(!msg.fork_transcript.empty(),
              "the fork note records the parent transcript path");
    }

    // 3. 新鲜身份+来源。
    check(forked.d.current.id.value != "parent",
          "fork has a new thread id (got '" + forked.d.current.id.value + "')");
    check(forked.d.current.forked_from == "parent",
          "fork records forked_from=parent");
    check(forked.d.current.title.rfind("Fork: ", 0) == 0,
          "fork title is prefixed (got '" + forked.d.current.title + "')");
    check(forked.d.current.rag_mode_override.has_value(),
          "fork carries a RAG-mode override");
    std::println("PASS\n");
}

void distinct_rag_modes() {
    std::println("--- distinct_rag_modes ---");
    // 每一个 选择面板 选择必须映射到 不同 模式。比较为数值,而不是整数:
    // 覆盖是可选的 <RagMode> ,所以,我说 "absent" 财务
    // 报告和已审计财务报表 "a mode" 无需监控即可区分。
    const auto m0 = fork_with(make_parent(), 0).d.current.rag_mode_override;
    const auto m1 = fork_with(make_parent(), 1).d.current.rag_mode_override;
    const auto m2 = fork_with(make_parent(), 2).d.current.rag_mode_override;
    check(m0 && m1 && m2, "every choice sets an override");
    auto name = [](const std::optional<agentty::RagMode>& v) {
        return v ? std::string{agentty::to_string(*v)} : std::string{"inherit"};
    };
    check(m0 != m1 && m1 != m2 && m0 != m2,
          "each RAG choice sets a distinct override (" +
          name(m0) + "," + name(m1) + "," + name(m2) + ")");
    std::println("PASS\n");
}

void empty_thread_no_fork() {
    std::println("--- empty_thread_no_fork ---");
    Model m;
    m.d.current.id = ThreadId{"empty"};
    Model after = fork_with(std::move(m), 0);
    check(after.d.current.id.value == "empty", "empty thread does not fork");
    std::println("PASS\n");
}

} // 命名空间结束。

int main() {
    std::println("=== fork_test ===");
    install_stub_deps();
    fresh_cheap_fork(0, "RAG per turn");
    fresh_cheap_fork(1, "First-turn RAG");
    fresh_cheap_fork(2, "RAG off");
    distinct_rag_modes();
    empty_thread_no_fork();
    fork_is_an_effect();
    if (g_failed) { std::println("{} check(s) FAILED", g_failed); return 1; }
    std::println("All fork tests passed.");
    return 0;
}
