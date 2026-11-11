// doom_loop_test — agent_loop_should_break (cmd_factory.cpp).
//
// 当地模式薄弱(qwen2.5) -coder 代码) -converging
// 工具环:选择错误的工具进行目标、错误、重新 -issue 几乎
// 完全相同的一个 调用 永远。 如果没有本地完成信号, 主代理循环会
// 旋转到用户点击时 Esc —— 后遗症 .
// "tool usage is fucked" 报告。
// agent_loop_should_break 是继续点的纯断路器
// kick_pending_tools 在使用另一个模型完成前先咨询。
//   (1) REPEAT 相同(工具,args)失败 调用 3次了 3次 3次吗?
//   (2) RUNAWAY —— 25种工具 -call 一次运行时没有文字回答。
//
// 显示断路器 FIRES 以真实的末日发誓, STAYS QUIET
// 健康进展、成功和跨越用户 -turn 边界(每个运行是新鲜的)。

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "agtest.hpp"

#include "agentty/runtime/model.hpp"
#include "agentty/runtime/app/cmd_factory.hpp"

using agentty::Message;
using agentty::Role;
using agentty::ToolCallId;
using agentty::ToolName;
using agentty::ToolUse;
using agentty::app::cmd::agent_loop_should_break;
using nlohmann::json;

namespace {


enum class Term { Done, Failed, Rejected };

ToolUse call(const std::string& name, json args, Term t) {
    static int seq = 0;
    ToolUse tc;
    tc.id   = ToolCallId{"call_" + std::to_string(seq++)};
    tc.name = ToolName{name};
    tc.args = std::move(args);
    if (t == Term::Done) tc.status = ToolUse::Done{{}, {}, "ok"};
    else if (t == Term::Failed) tc.status = ToolUse::Failed{{}, {}, "no such file"};
    else tc.status = ToolUse::Rejected{};
    return tc;
}

Message asst_call(const std::string& name, json args, Term t) {
    Message m;
    m.role = Role::Assistant;
    m.tool_calls.push_back(call(name, std::move(args), t));
    return m;
}

Message asst_text(std::string s) {
    Message m;
    m.role = Role::Assistant;
    m.text = std::move(s);
    return m;
}

Message user(std::string s = "go") {
    Message m;
    m.role = Role::User;
    m.text = std::move(s);
    return m;
}

}  // 名称空间 (帮助)

// ── 1. REPEAT : 相同的失败 调用 3x —— 间歇 ─────────────────────────
TEST_CASE("repeat failing call breaks") {
    std::vector<Message> msgs;
    msgs.push_back(user());
    json a = {{"path", "https://x.com/jokes"}};
    msgs.push_back(asst_call("read", a, Term::Failed));
    msgs.push_back(asst_call("read", a, Term::Failed));
    msgs.push_back(asst_call("read", a, Term::Failed));

    auto brk = agent_loop_should_break(msgs);
    check(brk.has_value(), "3x identical failing read → breaks");
    check(brk && brk->reason.find("read") != std::string::npos,
          "break reason names the offending tool");
}

// ── 2. 两个失败低于限度 —— 无间断(给机会) ─────────
TEST_CASE("two failures no break") {
    std::vector<Message> msgs;
    msgs.push_back(user());
    json a = {{"path", "x"}};
    msgs.push_back(asst_call("read", a, Term::Failed));
    msgs.push_back(asst_call("read", a, Term::Failed));
    check(!agent_loop_should_break(msgs).has_value(),
          "2x failing call does NOT break (1 retry allowed)");
}

// ── 3. 相同 调用 不过 SUCCEEDING 永远不中断(合法) -read) ────────────────
TEST_CASE("repeat succeeding tolerated below the limit") {
    // 再说一遍 WORKS 是一个卡住模型的薄弱证据: re
    // -reading 编辑后的文件,或重新编辑 -running 测试,
    // 是普通的,所以门槛 位于失败的1和5之上
    std::vector<Message> msgs;
    msgs.push_back(user());
    json a = {{"path", "log.txt"}};
    for (int i = 0; i < 5; ++i)
        msgs.push_back(asst_call("read", a, Term::Done));
    check(!agent_loop_should_break(msgs).has_value(),
          "five identical successful calls are still legitimate");
}

TEST_CASE("repeat succeeding breaks at the limit") {
    // 断裂器只计算了FAILINGXXXX 调用,
    // 并且用来支撑它的步盖被门套在-id Heuristic
    // (后来被完全移除)模型上。 一个私人网关(
    // `igpu/laguna-xs-2.1`)背后的
    // 模型被建造所无法识别,所以一个相同的-call循环,
    // 每一次SUCCEEDED都没有上限:在转弯预算用完前,测量到80次,
    // 在22GB居民身上,这个时间是GPU时间的分钟。
    //
    // 适用于 每个 模型 - 而现在的步盖已经消失,
    // 这个 IS 循环的后台,它继续成功。它按键在 调用 正在
    // 执行中 IDENTICAL 而不是努力工作的证据 而不是
    // 工作的证据 工作
    std::vector<Message> msgs;
    msgs.push_back(user());
    json a = {{"path", "log.txt"}};
    for (int i = 0; i < 6; ++i)
        msgs.push_back(asst_call("read", a, Term::Done));
    auto brk = agent_loop_should_break(msgs);
    check(brk.has_value(), "six identical successful calls break the loop");
    if (brk) check(brk->reason.find("read") != std::string::npos,
                   "the break names the offending tool");
}

TEST_CASE("succeeding repeats with different args never break") {
    // 歧视者: IDENTICAL 调用 是一个循环的证据,
    // 而不是工具。一个走进目录的模型读取了许多文件。
    std::vector<Message> msgs;
    msgs.push_back(user());
    for (int i = 0; i < 12; ++i)
        msgs.push_back(asst_call("read",
            json{{"path", "f" + std::to_string(i) + ".txt"}}, Term::Done));
    check(!agent_loop_should_break(msgs).has_value(),
          "distinct successful reads are a legitimate traversal");
}

// ── 4. 死亡者不相同,每次死亡时不同 调用 ─────────────────────
TEST_CASE("distinct failing calls no break") {
    std::vector<Message> msgs;
    msgs.push_back(user());
    // 3个未读,但每个 不同 路径——探索,而不是卡住.
    msgs.push_back(asst_call("read", json{{"path", "a"}}, Term::Failed));
    msgs.push_back(asst_call("read", json{{"path", "b"}}, Term::Failed));
    msgs.push_back(asst_call("read", json{{"path", "c"}}, Term::Failed));
    check(!agent_loop_should_break(msgs).has_value(),
          "distinct failing args do NOT trip the repeat cap");
}

// ── 5. NO STEP CAP : 长期 DISTINCT 工作从不中断 ─────────────
//
// 曾经有25个 -turn 盖子在这,门在模型上 -id 高压。
// 两者都不见了。 原始的数工具转动并不代表什么。
// 真正的搜索。 读 , 编辑 , 校验一个大转折
// 代码库 合法地花费了数十个独特的、富有成效的步骤,
// 并任意地以数量阻止它,使工作产出被断断绝,而毫无发现。
// 相同 记录稿停在本地模特儿身上 运行顺利
// Claude.
//
// 离家出走的仍然是重复,在下文的5c/5d中测试.
// 这个案子 锁 单凭那卷纸 什么都行
TEST_CASE("no step cap: distinct work runs as long as it needs") {
    std::vector<Message> msgs;
    msgs.push_back(user());
    for (int i = 0; i < 200; ++i)
        msgs.push_back(asst_call("shell", json{{"command", "echo " + std::to_string(i)}},
                                 Term::Done));
    check(!agent_loop_should_break(msgs).has_value(),
          "200 distinct successful tool turns do NOT break");
}

// 5b. 无论当前由哪个模型驱动，该性质都应成立。
//       旧的帽子是当一个id Heuristic 无法识别
//       模型时才执行的 所以一个不熟悉的本地名字被停了
//       `claude-opus-4` 没有,在 字节-identical 历史。
//       没有 标志 左传,即是重点:一抄本,一答.
TEST_CASE("no step cap: the answer does not depend on the model id") {
    std::vector<Message> msgs;
    msgs.push_back(user());
    for (int i = 0; i < 60; ++i)
        msgs.push_back(asst_call("read", json{{"path", "f" + std::to_string(i) + ".cpp"}},
                                 Term::Done));
    check(!agent_loop_should_break(msgs).has_value(),
          "60 distinct reads never break, whoever is driving");
}

// 5c. 连续重复失败保护适用于所有模型；取消步骤上限后，它是
//       只能阻止两件事 aider/MindStudio 应用重复的
//       -dead-call 盖到每个模型; 一个能用的模型卡住
//       -trying 相同的失败 调用 仍然停下来。
TEST_CASE("repeat failure breaks even for capable") {
    std::vector<Message> msgs;
    msgs.push_back(user());
    json a = {{"path", "/nope"}};
    msgs.push_back(asst_call("read", a, Term::Failed));
    msgs.push_back(asst_call("read", a, Term::Failed));
    msgs.push_back(asst_call("read", a, Term::Failed));
    auto brk = agent_loop_should_break(msgs);
    check(brk.has_value(),
          "3x identical failing call breaks on every model");
    check(brk && brk->reason.find("read") != std::string::npos,
          "repeat-failure reason names the tool");
}

// 5d. 仅统计当前连续失败次数，更早的失败
//       之后的成功是复苏历史,而不是末日循环。
TEST_CASE("success resets failure streak") {
    std::vector<Message> msgs;
    msgs.push_back(user());
    json a = {{"path", "flaky.txt"}};
    msgs.push_back(asst_call("read", a, Term::Failed));
    msgs.push_back(asst_call("read", a, Term::Failed));
    msgs.push_back(asst_call("read", a, Term::Done));
    msgs.push_back(asst_call("read", a, Term::Failed));
    msgs.push_back(asst_call("read", a, Term::Failed));
    check(!agent_loop_should_break(msgs).has_value(),
          "success resets identical-call failure streak");
}

// 拒绝是 终端 循环目的失败: 一再拒绝相同
// 权限 无法取得进展,因此应当获得同样的突破器。
TEST_CASE("rejection streak breaks") {
    std::vector<Message> msgs;
    msgs.push_back(user());
    json a = {{"command", "dangerous"}};
    msgs.push_back(asst_call("shell", a, Term::Rejected));
    msgs.push_back(asst_call("shell", a, Term::Rejected));
    msgs.push_back(asst_call("shell", a, Term::Rejected));
    check(agent_loop_should_break(msgs).has_value(),
          "3x identical rejection streak breaks");
}

// ── 6. 适度的健康的多种 -step 任务确实 不 换行 ───────────────────────
TEST_CASE("healthy progress no break") {
    std::vector<Message> msgs;
    msgs.push_back(user());
    msgs.push_back(asst_call("shell",  json{{"command", "ls"}}, Term::Done));
    msgs.push_back(asst_call("read",  json{{"path", "a.cpp"}}, Term::Done));
    msgs.push_back(asst_call("edit",  json{{"path", "a.cpp"}}, Term::Done));
    msgs.push_back(asst_call("shell",  json{{"command", "make"}}, Term::Done));
    check(!agent_loop_should_break(msgs).has_value(),
          "4-step search→read→edit→verify does NOT break");
}

// 7. 用户消息边界会重置运行状态；更早轮次中的死循环
//      与当前不相抵。
TEST_CASE("user boundary resets run") {
    std::vector<Message> msgs;
    // 转早点:末日循环(3次失败读).
    msgs.push_back(user("first"));
    json a = {{"path", "x"}};
    msgs.push_back(asst_call("read", a, Term::Failed));
    msgs.push_back(asst_call("read", a, Term::Failed));
    msgs.push_back(asst_call("read", a, Term::Failed));
    // 新的用户回合以一个健康的调用开始FRESH运行.
    msgs.push_back(user("second"));
    msgs.push_back(asst_call("shell", json{{"command", "pwd"}}, Term::Done));
    check(!agent_loop_should_break(msgs).has_value(),
          "doom loop in a PRIOR run doesn't break the current fresh run");
}

// ── 8. 空/无-tool历史安全 ───────────────────────────────────────
TEST_CASE("empty and text only no break") {
    check(!agent_loop_should_break({}).has_value(), "empty history no break");
    std::vector<Message> msgs;
    msgs.push_back(user());
    msgs.push_back(asst_text("Here's your answer."));
    check(!agent_loop_should_break(msgs).has_value(), "text-only turn no break");
}

// ── 9.90号:第一次旅行NUDGES和运行继续进行 ───────────────────────────
// apply_nudge 镜像 kick_pending_tools
// : 理由附于目标 调用 输出, 也就是断路器后来知道它被压抑的方式.
namespace {
void apply_nudge(std::vector<Message>& msgs,
                 const agentty::app::cmd::LoopBreak& b) {
    for (auto& m : msgs)
        for (auto& tc : m.tool_calls) {
            if (tc.id != b.target) continue;
            if (auto* f = std::get_if<ToolUse::Failed>(&tc.status))
                f->output += "\n\n" + b.reason;
            if (auto* d = std::get_if<ToolUse::Done>(&tc.status))
                d->output += "\n\n" + b.reason;
        }
}
}  // 命名空间结束。

TEST_CASE("issue 90 first failure trip nudges instead of stopping") {
    std::vector<Message> msgs;
    msgs.push_back(user());
    json a = {{"path", "app/src/ui"}};
    for (int i = 0; i < 3; ++i) msgs.push_back(asst_call("list_dir", a, Term::Failed));
    auto brk = agent_loop_should_break(msgs);
    check(brk.has_value(), "3x failing list_dir trips the guard");
    check(brk && !brk->stop, "first trip is a nudge, not a stop");
    check(brk && brk->target == msgs.back().tool_calls[0].id,
          "nudge targets the newest call of the streak");
    check(brk && brk->reason.rfind("Stopped", 0) != 0,
          "nudge text does not claim the run stopped");
}

TEST_CASE("issue 90 model changes course after nudge") {
    std::vector<Message> msgs;
    msgs.push_back(user());
    json a = {{"path", "app/src/ui"}};
    for (int i = 0; i < 3; ++i) msgs.push_back(asst_call("list_dir", a, Term::Failed));
    auto brk = agent_loop_should_break(msgs);
    check(brk && !brk->stop, "nudge first");
    if (brk) apply_nudge(msgs, *brk);
    check(!agent_loop_should_break(msgs).has_value(),
          "a delivered nudge doesn't fire again on the same streak");
    msgs.push_back(asst_call("write", json{{"path", "app/src/ui/A.java"}}, Term::Done));
    check(!agent_loop_should_break(msgs).has_value(),
          "a different follow-up call runs freely");
}

TEST_CASE("issue 90 identical call after nudge stops") {
    std::vector<Message> msgs;
    msgs.push_back(user());
    json a = {{"path", "app/src/ui"}};
    for (int i = 0; i < 3; ++i) msgs.push_back(asst_call("list_dir", a, Term::Failed));
    auto brk = agent_loop_should_break(msgs);
    check(brk && !brk->stop, "nudge first");
    if (brk) apply_nudge(msgs, *brk);
    msgs.push_back(asst_call("list_dir", a, Term::Failed));
    auto brk2 = agent_loop_should_break(msgs);
    check(brk2 && brk2->stop, "ignoring the nudge stops the run");
    check(brk2 && brk2->reason.rfind("Stopped", 0) == 0, "stop text says Stopped");
}

TEST_CASE("issue 90 rejection streak still stops at once") {
    std::vector<Message> msgs;
    msgs.push_back(user());
    json a = {{"command", "rm -rf build"}};
    for (int i = 0; i < 3; ++i) msgs.push_back(asst_call("shell", a, Term::Rejected));
    auto brk = agent_loop_should_break(msgs);
    check(brk && brk->stop, "user rejected it 3x: stop, a nudge can't help");
}

TEST_CASE("issue 90 success repeat nudges then stops") {
    std::vector<Message> msgs;
    msgs.push_back(user());
    json a = {{"path", "log.txt"}};
    for (int i = 0; i < 6; ++i) msgs.push_back(asst_call("read", a, Term::Done));
    auto brk = agent_loop_should_break(msgs);
    check(brk && !brk->stop, "6x identical success: nudge first");
    if (brk) apply_nudge(msgs, *brk);
    msgs.push_back(asst_call("read", a, Term::Done));
    auto brk2 = agent_loop_should_break(msgs);
    check(brk2 && brk2->stop, "identical read after the nudge stops");
}

// 截图环:读png,修补码,re-shoot,再读一遍.
// 每次都是一样的路,其他的 调用 中间,每次要一张新照片。
// 那是工作,不是循环,过去在六读时被停了.
TEST_CASE("re-reading a file between other calls is not a loop") {
    std::vector<Message> msgs;
    msgs.push_back(user());
    json shot = {{"path", "/tmp/k4/cmp/sbs-dcc.png"}};
    for (int i = 0; i < 10; ++i) {
        msgs.push_back(asst_call("shell", {{"command", "ninja && ./shot " + std::to_string(i)}},
                                 Term::Done));
        msgs.push_back(asst_call("read", shot, Term::Done));
    }
    check(!agent_loop_should_break(msgs), "interleaved identical reads never trip");
}

TEST_CASE("back-to-back repeats with a changing result are not a loop") {
    std::vector<Message> msgs;
    msgs.push_back(user());
    json a = {{"path", "build.log"}};
    for (int i = 0; i < 10; ++i) {
        auto m = asst_call("read", a, Term::Done);
        m.tool_calls.back().status = ToolUse::Done{{}, {}, "line " + std::to_string(i)};
        msgs.push_back(std::move(m));
    }
    check(!agent_loop_should_break(msgs), "a file that keeps changing is being watched");
}

TEST_CASE("a cached repeat of the same file still counts") {
    std::vector<Message> msgs;
    msgs.push_back(user());
    json a = {{"path", "log.txt"}};
    for (int i = 0; i < 6; ++i) {
        auto m = asst_call("read", a, Term::Done);
        if (i > 0)
            m.tool_calls.back().status =
                ToolUse::Done{{}, {}, "[cached \xe2\x80\x94 unchanged since your last read of this file]\nok"};
        msgs.push_back(std::move(m));
    }
    auto brk = agent_loop_should_break(msgs);
    check(brk && !brk->stop, "the [cached] tag is the same result: nudge");
}

