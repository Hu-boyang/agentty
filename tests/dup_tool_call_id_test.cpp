// dup_tool_call_id_test ——重复工具. -call id摄入回归。
//
// 根因: 数 OpenAI-compatible 网关薄荷a DETERMINISTIC
// id /(工具,指数)——字面意思 "bash:0" 每一次的打击 调用
// 而不是一个独特的 ToolCallId。一个代理转弯在直播中保存着几个
// 助理消息 末尾 (kick_pending_tools 附加每个子的新占位
// 符 -turn ),所以转2's "bash:0" 已经完成,在1号线旁边
// 旧的 `with_live_tool` 匹配的 首次 在整个
// 现场中 末尾 :转2的成绩被印在转1的死卡上,
// 真实的 调用 留下来 待处理 ,它的卡挂到330步 超时 ——每一次用
// 工具回应 调用.
//
// 固定( 4) update/internal.hpp): `with_live_tool`
// 喜欢第一个 NON-TERMINAL 调用 带着身份证明,掉回 终端 只有一个,
// 当没有生命 调用 已经有了。 Result/progress/timeout/permission
// 线路都通了 `with_live_tool` ,所以单靠它就着陆 事件
// 正确(活)卡,即使更早的子 -turn 留下一个相同的 -id 已经完成
// 调用 输入 末尾.
//
// 流式传输 组装( 组装) stream.cpp `find_streaming_tool`
// 不需要特殊外壳:其范围为 messages.back () —— 国家 当前
// 子目录 -turn 助理消息——所以更早的子程序 -turn 同样的 -id'd
// 调用 完全超出范围,不能遮蔽被流的那个人。
//
// 请求 :
//   1. with_live_tool 瞄准重复的ID选择第一个
//      NON-TERMINAL 承运人——不是第一个(已经) -Done 相匹配( 匹配 ).
//   2. 完全交叉 -sub-turn 流量: 转1's 调用 已写成; 转2 重放
//      相同 传输 id;其流转的 ⁇ +观测到的结果值在2号线的直播时间 调用 ,
//      然后转动 1 的 Done 卡片保持自己的输出。
//   3. find_streaming_tool 被隔离到当前子 -turn : 三角洲用于
//      重播的ID从不泄露到之前的 -turn 的 调用 上.
//   4. 绞刑(从未) -terminated ) 首个 调用 输入 相同 信件不为
//      以后再吞下一个真正不同的 调用 结果是
//   5. 通往自己调用的独特编号路线,未受影响。

#include <string>
#include <utility>

#include "agtest.hpp"

#include "agentty/runtime/app/update/internal.hpp"
#include "agentty/runtime/model.hpp"

using agentty::Message;
using agentty::Model;
using agentty::Role;
using agentty::ToolCallId;
using agentty::ToolName;
using agentty::ToolUse;
using agentty::StreamObservedToolResult;
using agentty::StreamToolUseDelta;
using agentty::StreamToolUseEnd;
using agentty::StreamToolUseStart;
using agentty::app::Step;
using agentty::app::detail::stream_update;
using agentty::app::detail::with_live_tool;
namespace msg = agentty::msg;


static Message asst_placeholder() {
    Message m;
    m.role = Role::Assistant;
    return m;
}

static Step start(Model m, const std::string& id, const std::string& name) {
    return ::agentty::app::detail::step(::agentty::app::detail::stream_update, std::move(m),
                         msg::StreamMsg{StreamToolUseStart{
                             ToolCallId{id}, ToolName{name}}});
}

static Step delta(Model m, const std::string& id, std::string js) {
    return ::agentty::app::detail::step(::agentty::app::detail::stream_update, std::move(m),
                         msg::StreamMsg{StreamToolUseDelta{
                             ToolCallId{id}, std::move(js)}});
}

static Step end(Model m, const std::string& id) {
    return ::agentty::app::detail::step(::agentty::app::detail::stream_update, std::move(m),
                         msg::StreamMsg{StreamToolUseEnd{ToolCallId{id}}});
}

static Step observed(Model m, const std::string& id, std::string out) {
    return ::agentty::app::detail::step(::agentty::app::detail::stream_update, std::move(m),
                         msg::StreamMsg{StreamObservedToolResult{
                             ToolCallId{id}, /*失败=*/false,
                             std::move(out)}});
}

// sub_turn_start 镜像 kick_pending_tools
// : 附加下一个子 -turn 助理占位符没有冻结前一个,
// 所以都住在 末尾 重复的前提条件 -id 碰撞 撞车 撞车
static void sub_turn_start(Model& m) {
    m.d.current.messages.push_back(asst_placeholder());
}

static ToolUse make_call(const std::string& id, ToolUse::Status st) {
    ToolUse tc;
    tc.id     = ToolCallId{id};
    tc.name   = ToolName{"shell"};
    tc.status = std::move(st);
    return tc;
}

static ToolUse::Done done_status(const char* out) {
    auto now = std::chrono::steady_clock::now();
    return ToolUse::Done{now, now, out};
}

// ── 1. with_live_tool 更喜欢第一个非 -terminal 承运人 ───────────────
TEST_CASE("with live tool prefers non terminal") {
    Model m;
    // 两名助理现场留言,都带着 "bash:0" : 第一个完成(
    // 上一个子) -turn ),第二个 待处理 (当前)原始的望望
    // 必须降落在 待处理 一个
    m.d.current.messages.push_back(asst_placeholder());
    m.d.current.messages.back().tool_calls.push_back(
        make_call("bash:0", done_status("stale")));
    m.d.current.messages.push_back(asst_placeholder());
    m.d.current.messages.back().tool_calls.push_back(
        make_call("bash:0", ToolUse::Pending{}));

    bool hit_terminal = false;
    bool hit_pending  = false;
    with_live_tool(m, ToolCallId{"bash:0"}, [&](ToolUse& tc) {
        if (tc.is_terminal()) hit_terminal = true;
        if (tc.is_pending())  hit_pending  = true;
    });
    check(!hit_terminal, "L1: did not route to the Done card");
    check(hit_pending,   "L1: routed to the live (Pending) card");

    // 与现场 调用 不见了( 所有 终端 ),它可能会倒向一个 终端
    // 匹配而不是默默无声的不 -op.
    m.d.current.messages.back().tool_calls[0].status = done_status("fresh");
    bool any = false;
    with_live_tool(m, ToolCallId{"bash:0"}, [&](ToolUse&) { any = true; });
    check(any, "L1: falls back to a terminal match when none are live");
}

// ── 2. 十字记 -sub-turn :结果在现场登陆 调用 ,未调整的已写卡 ──
TEST_CASE("result routes across sub turns") {
    Model m;
    // 转1:数据流 shash:0,完成,驱动它用自己的输出完成.
    m.d.current.messages.push_back(asst_placeholder());
    Step s = start(std::move(m), "bash:0", "shell");
    s = delta(std::move(s.first), "bash:0", "{\"cmd\":\"one\"}");
    s = end(std::move(s.first), "bash:0");
    m = std::move(s.first);
    m.d.current.messages.back().tool_calls[0].status = done_status("first-out");

    // 转二:新子 -turn 占位符, 不变 传输 id重播. 爱丽丝·卡西尔·卡西姆·卡西里·卡西
    sub_turn_start(m);
    s = start(std::move(m), "bash:0", "shell");
    s = delta(std::move(s.first), "bash:0", "{\"cmd\":\"two\"}");
    s = end(std::move(s.first), "bash:0");
    // 发现2号线的试验结果 必须在2号线现场着陆 调用.
    s = observed(std::move(s.first), "bash:0", "second-out");
    m = std::move(s.first);

    const auto& t1 = m.d.current.messages[m.d.current.messages.size() - 2];
    const auto& t2 = m.d.current.messages.back();
    check(t1.tool_calls.size() == 1 && t2.tool_calls.size() == 1,
          "T2: one call in each sub-turn message");
    check(t1.tool_calls[0].output() == "first-out",
          "T2: turn 1's Done card keeps its own output");
    check(t2.tool_calls[0].is_terminal(),
          "T2: turn 2's call settled (did not hang)");
    check(t2.tool_calls[0].output() == "second-out",
          "T2: turn 2's result landed on turn 2's call");
}

// ── 3. find_streaming_tool 被限制到当前子 -turn ───────────────
TEST_CASE("streaming isolated to current sub turn") {
    Model m;
    // 转一圈:全程 -streamed 以参数击打: 0 "{A}".
    m.d.current.messages.push_back(asst_placeholder());
    Step s = start(std::move(m), "bash:0", "shell");
    s = delta(std::move(s.first), "bash:0", "{A}");
    s = end(std::move(s.first), "bash:0");
    m = std::move(s.first);
    const std::string turn1_args = m.d.current.messages.back().tool_calls[0].args_streaming;

    // 转二: 新的占位符, 相同的ID, 不同 参数 数据流.
    sub_turn_start(m);
    s = start(std::move(m), "bash:0", "shell");
    s = delta(std::move(s.first), "bash:0", "{B}");
    m = std::move(s.first);

    const auto& t1 = m.d.current.messages[m.d.current.messages.size() - 2];
    const auto& t2 = m.d.current.messages.back();
    check(t1.tool_calls[0].args_streaming == turn1_args,
          "T3: turn 1's streamed args were NOT mutated by turn 2's delta");
    check(t2.tool_calls[0].args_streaming.find("{B}") != std::string::npos,
          "T3: turn 2's delta landed on turn 2's call");
}

// ── 4. 先吊死一个 调用 以后不会吞下一个明显的 调用 ──────────
TEST_CASE("hanging first call does not swallow") {
    Model m;
    // 一、二、二、一、二、三、二、四、四、五、五、四、六、五、六、四、七、五、七、七、八、八、七、九、九、八、九、七、十、九、十二、九、十、八、十二、十二、十四、九、十四、十四、十二、十七、十四、十七、十二、十一、十四、十一、十二、十三、十四、十六、十四、十三、十二、十六、十二、十八、十二、十、十二、十五、十四、十、十四、十五、十二、十九、十四、十八、十四、十九、十二、
    // 七、十四、七、十二、二十二、十四、八、十四、四、十四、二十二、十二、四、十二、二十、十四、二十、十二、八、十七、十七、十三、十七、 DISTINCT ids( 通常平行) 调用。第一个是永不终止的;第二个是获得自己的结果。
    m.d.current.messages.push_back(asst_placeholder());
    Step s = start(std::move(m), "bash:0", "shell");   // 永远没有结束 挂
    s = start(std::move(s.first), "bash:1", "shell");
    s = end(std::move(s.first), "bash:1");
    s = observed(std::move(s.first), "bash:1", "b1-out");
    m = std::move(s.first);

    const auto& calls = m.d.current.messages.back().tool_calls;
    check(calls.size() == 2, "T4: both parallel calls present");
    check(!calls[0].is_terminal(), "T4: first (hanging) call still live");
    check(calls[1].output() == "b1-out",
          "T4: second call received its own result");
}

// ── 5. 通往自己调用的独特编号路线 ──────────────────────────────────
TEST_CASE("unique ids untouched") {
    Model m;
    m.d.current.messages.push_back(asst_placeholder());
    Step s = start(std::move(m), "call_a", "shell");
    s = start(std::move(s.first), "call_b", "read");
    s = delta(std::move(s.first), "call_a", "{\"x\":1}");
    s = delta(std::move(s.first), "call_b", "{\"y\":2}");
    m = std::move(s.first);

    const auto& calls = m.d.current.messages.back().tool_calls;
    check(calls.size() == 2, "T5: two distinct calls");
    check(calls[0].id.value == "call_a" && calls[1].id.value == "call_b",
          "T5: unique ids untouched");
    check(calls[0].args_streaming.find("\"x\":1") != std::string::npos,
          "T5: call_a got its own args");
    check(calls[1].args_streaming.find("\"y\":2") != std::string::npos,
          "T5: call_b got its own args");
}

// ── 6. A 工作线程 超越了它的寿命 调用 无法击中重用过的ID ──────────────
// 转1跑 call_0 (seq 7),用户 已取消 ,转动 2 再利用
// call_0 并运行它作为以下8. 转1's 工作线程 最后返回:必须放弃。
TEST_CASE("stale tool result is dropped by exec seq") {
    using agentty::ToolExecOutput;
    using agentty::ToolExecProgress;
    using agentty::app::detail::tool_update;
    auto now = std::chrono::steady_clock::now();

    Model m;
    m.d.current.messages.push_back(asst_placeholder());
    m.d.current.messages.back().tool_calls.push_back(make_call("call_0",
        ToolUse::Failed{now, now, "cancelled"}));
    m.d.current.messages.push_back(asst_placeholder());
    m.d.current.messages.back().tool_calls.push_back(make_call("call_0",
        ToolUse::Running{now, {}, {}, now, /*exec_seq=*/8}));

    ToolExecProgress p{ToolCallId{"call_0"}, "old progress", 7};
    auto s = ::agentty::app::detail::step(::agentty::app::detail::tool_update, std::move(m), msg::ToolMsg{std::move(p)});
    m = std::move(s.first);
    auto* r = std::get_if<ToolUse::Running>(
        &m.d.current.messages[1].tool_calls[0].status);
    check(r && r->progress_text.empty(), "T6: stale progress ignored");

    ToolExecOutput o{ToolCallId{"call_0"}, std::string{"old output"}};
    o.exec_seq = 7;
    s = ::agentty::app::detail::step(::agentty::app::detail::tool_update, std::move(m), msg::ToolMsg{std::move(o)});
    m = std::move(s.first);
    check(std::holds_alternative<ToolUse::Running>(
              m.d.current.messages[1].tool_calls[0].status),
          "T6: stale result does not settle the new call");
    check(m.d.current.messages[0].tool_calls[0].output() == "cancelled",
          "T6: the cancelled call keeps its reason");

    ToolExecOutput fresh{ToolCallId{"call_0"}, std::string{"new output"}};
    fresh.exec_seq = 8;
    s = ::agentty::app::detail::step(::agentty::app::detail::tool_update, std::move(m), msg::ToolMsg{std::move(fresh)});
    m = std::move(s.first);
    check(m.d.current.messages[1].tool_calls[0].output() == "new output",
          "T6: the matching result lands");
}
