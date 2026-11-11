// salvage_dedup_test ——关于 -leaked 已打捞 -tool-call 脱衣舞
//
// 当地模式薄弱, OpenAI-compat 路径( qwen2.5) -coder :
// 7b等) 漏泄工具 调用 赤裸裸的 JSON 输入 `content` ;
// 缩写 传输层 用合成的救活他们 `call_salvaged_N` id. 这些模型 RE
// -LEAK 相同的数据 调用 员额 -tool 子目录 -turn ,因此没有警卫,
// 它运行两次(重复卡片错误). dedup_releaked_salvage_calls
// 决定 a 待处理 已打捞 调用 失败 -without-side-effects 当一个
// 相同的 调用 已经运行 终端 之前在同一个特工转弯时
//
// 请求 :
//   1. 关于 -leaked 已打捞 调用 (同名+args,前作)解析 →
//      失败,从未执行。
//   2. A STRUCTURED 复制( 实数) `call_...`/`toolu_...` id)为: 不 已除去
//      ——用相同的语句两次调用读音是正当的意向.
//   3. 抢救 调用 与 不同 参数为 不 脱落。
//   4. 现 告 首次 已打捞 调用 (无先前 终端 双)是左 待处理.
//   5. 编程范围是目前的转弯——用户边界重排。

#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "agtest.hpp"

#include "agentty/runtime/model.hpp"
#include "agentty/runtime/app/cmd_factory.hpp"

using agentty::Message;
using agentty::Model;
using agentty::Role;
using agentty::ToolCallId;
using agentty::ToolName;
using agentty::ToolUse;
using nlohmann::json;

namespace {


ToolUse make_call(const std::string& id, const std::string& name,
                  json args, bool terminal) {
    ToolUse tc;
    tc.id   = ToolCallId{id};
    tc.name = ToolName{name};
    tc.args = std::move(args);
    if (terminal)
        tc.status = ToolUse::Done{ {}, {}, "ok" };
    else
        tc.status = ToolUse::Pending{ {} };
    return tc;
}

Message asst(std::vector<ToolUse> calls) {
    Message m;
    m.role = Role::Assistant;
    for (auto& c : calls) m.tool_calls.push_back(std::move(c));
    return m;
}

Message user() {
    Message m;
    m.role = Role::User;
    m.text = "go";
    return m;
}

} // 名称空间 (帮助)

// ── 1. 有关 -leaked 已打捞 调用 (同名+args) 已解析 ─────────────────
TEST_CASE("releak same turn deduped") {
    Model m;
    // 一份助理信息:先前的Done respatched读作+re-leaked
    // 待处理 respacked读作相同方格.
    json a = {{"path", "/tmp/x"}};
    m.d.current.messages.push_back(user());
    m.d.current.messages.push_back(asst({
        make_call("call_salvaged_0", "read", a, /*terminal=*/true),
        make_call("call_salvaged_1", "read", a, /*terminal=*/false),
    }));

    auto n = agentty::app::cmd::dedup_releaked_salvage_calls(m);
    check(n == 1, "exactly one call deduped");
    const auto& tcs = m.d.current.messages.back().tool_calls;
    check(tcs[0].is_done(),   "prior call stays Done");
    check(tcs[1].is_failed(), "re-leaked call resolved Failed (not run)");
}

// ── 2. 结构复制品为NOT(故意) ──────────────
TEST_CASE("structured duplicate not deduped") {
    Model m;
    json a = {{"path", "/tmp/x"}};
    m.d.current.messages.push_back(user());
    m.d.current.messages.push_back(asst({
        make_call("call_abc", "read", a, /*terminal=*/true),
        make_call("call_def", "read", a, /*terminal=*/false),
    }));

    auto n = agentty::app::cmd::dedup_releaked_salvage_calls(m);
    check(n == 0, "structured duplicate left alone");
    check(m.d.current.messages.back().tool_calls[1].is_pending(),
          "structured duplicate stays Pending");
}

// ── 3. 抢救 调用 与 不同 参数为 不 已除去 ─────────────────────
TEST_CASE("salvaged different args not deduped") {
    Model m;
    m.d.current.messages.push_back(user());
    m.d.current.messages.push_back(asst({
        make_call("call_salvaged_0", "read",
                  json{{"path", "/a"}}, /*terminal=*/true),
        make_call("call_salvaged_1", "read",
                  json{{"path", "/b"}}, /*terminal=*/false),
    }));

    auto n = agentty::app::cmd::dedup_releaked_salvage_calls(m);
    check(n == 0, "different-args salvaged call not deduped");
    check(m.d.current.messages.back().tool_calls[1].is_pending(),
          "different-args call stays Pending");
}

// ── 4. 首次抢救 调用 (没有前双)是左侧的 待处理 ──────────────────
TEST_CASE("first salvaged call runs") {
    Model m;
    m.d.current.messages.push_back(user());
    m.d.current.messages.push_back(asst({
        make_call("call_salvaged_0", "read",
                  json{{"path", "/x"}}, /*terminal=*/false),
    }));

    auto n = agentty::app::cmd::dedup_releaked_salvage_calls(m);
    check(n == 0, "first salvaged call not deduped");
    check(m.d.current.messages.back().tool_calls[0].is_pending(),
          "first salvaged call stays Pending");
}

// ── 5. 分解范围为当前转弯(用户边界重置) ───────────
TEST_CASE("prior turn not counted") {
    Model m;
    json a = {{"path", "/x"}};
    // 转一圈,打完完完结
    m.d.current.messages.push_back(user());
    m.d.current.messages.push_back(asst({
        make_call("call_salvaged_0", "read", a, /*terminal=*/true),
    }));
    // 转二: a 新 用户消息,然后相同 调用 再来一次 待处理
    // 这是一项新的、有意识的要求——不得 被拆卸到第1转弯处
    m.d.current.messages.push_back(user());
    m.d.current.messages.push_back(asst({
        make_call("call_salvaged_1", "read", a, /*terminal=*/false),
    }));

    auto n = agentty::app::cmd::dedup_releaked_salvage_calls(m);
    check(n == 0, "duplicate in a NEW turn not deduped against prior turn");
    check(m.d.current.messages.back().tool_calls[0].is_pending(),
          "new-turn call stays Pending");
}

// ── 6. 十字勋章 -sub-turn 内容 -leak (实际错误形状)已解析 ─────────────
// 真正的循环: 助手子 -turn 被抢救的一具尸体 调用 完成; 邮件 -tool
// 子目录 -turn B( 无介入用户) re -leaks 来,来,
// 来 待处理。两者都是助理信息。
TEST_CASE("cross subturn releak deduped") {
    Model m;
    json a = {{"path", "/tmp/x"}};
    m.d.current.messages.push_back(user());
    m.d.current.messages.push_back(asst({
        make_call("call_salvaged_0", "read", a, /*terminal=*/true),
    }));
    // 子类 -turn B( 继续流传的占位符) -leak).
    m.d.current.messages.push_back(asst({
        make_call("call_salvaged_1", "read", a, /*terminal=*/false),
    }));

    auto n = agentty::app::cmd::dedup_releaked_salvage_calls(m);
    check(n == 1, "cross-sub-turn re-leak deduped");
    check(m.d.current.messages.back().tool_calls[0].is_failed(),
          "cross-sub-turn re-leak resolved Failed");
}

// ── 7. 与以下系统进行径行泄漏循环: DRIFTING 参数受预算约束 ────────
// 弱型号再 -leak 相同工具,每个子的参数略有不同 -turn
// (e.g。用翻转瞄准镜记住 project/user ),
// 击败准确的 -match 脱下,在每张之后 -turn 抢救预算用完了,
// 再用下去 待处理 已打捞 调用 失败而不运行, 循环终止。
TEST_CASE("salvage budget bounds drifting loop") {
    Model m;
    m.d.current.messages.push_back(user());
    // 8个先前抢救 调用 已经运行 终端 轮到这个方向,
    // 每个方向 不同 说不定不会有人被抓到 -match 脱衣舞
    std::vector<ToolUse> calls;
    for (int i = 0; i < 8; ++i)
        calls.push_back(make_call("call_salvaged_" + std::to_string(i),
                                  "read",
                                  json{{"path", "/f" + std::to_string(i)}},
                                  /*terminal=*/true));
    m.d.current.messages.push_back(asst(std::move(calls)));
    // 第九回 说到这里 -leak : 一个品牌 -new 参数值,不是确切的重复。
    m.d.current.messages.push_back(asst({
        make_call("call_salvaged_8", "read",
                  json{{"path", "/f8-drifted"}}, /*terminal=*/false),
    }));

    auto n = agentty::app::cmd::dedup_releaked_salvage_calls(m);
    check(n == 1, "over-budget drifting salvaged call failed (loop bounded)");
    check(m.d.current.messages.back().tool_calls[0].is_failed(),
          "over-budget call resolved Failed without running");
}

// ── 8. A STRUCTURED 调用 永远不能被抢救预算限制 ──────────────
// 预算只管理人工挽救的漏泄。 在许多被拯救的漏泄之后,
// 真正的结构化工具调用是有意的,必须运行。
TEST_CASE("structured call not budget capped") {
    Model m;
    m.d.current.messages.push_back(user());
    std::vector<ToolUse> calls;
    for (int i = 0; i < 8; ++i)
        calls.push_back(make_call("call_salvaged_" + std::to_string(i),
                                  "read",
                                  json{{"path", "/f" + std::to_string(i)}},
                                  /*terminal=*/true));
    m.d.current.messages.push_back(asst(std::move(calls)));
    m.d.current.messages.push_back(asst({
        make_call("call_real_1", "read",
                  json{{"path", "/tmp/x"}}, /*terminal=*/false),
    }));

    auto n = agentty::app::cmd::dedup_releaked_salvage_calls(m);
    check(n == 0, "structured call not capped by salvage budget");
    check(m.d.current.messages.back().tool_calls[0].is_pending(),
          "structured call stays Pending despite spent salvage budget");
}

// ── 9. A SALVAGED 内存 工具被完全屏蔽( 第一个) 调用 ,没有先前) ────
// 模式薄弱漏水 remember/forget/wipe_memory 问候你,
// 打捞的 调用 改为: 内存 工具绝不是用户的意图——必须失败 无
// 需 运行,即使作为 首次 已打捞 调用 转弯(没有脱钩双胞胎,
// 预算没有用完)。
TEST_CASE("salvaged memory tool blocked") {
    for (const char* name : {"remember", "forget", "wipe_memory"}) {
        Model m;
        m.d.current.messages.push_back(user());
        m.d.current.messages.push_back(asst({
            make_call("call_salvaged_0", name,
                      json{{"text", "Hi there!"}}, /*terminal=*/false),
        }));
        auto n = agentty::app::cmd::dedup_releaked_salvage_calls(m);
        check(n == 1, "salvaged memory tool blocked");
        check(m.d.current.messages.back().tool_calls[0].is_failed(),
              "salvaged memory tool resolved Failed without running");
    }
}

// ── 10. A STRUCTURED 内存 工具为 不 被屏蔽( 故意/ 斜线路径) ──
TEST_CASE("structured memory tool allowed") {
    Model m;
    m.d.current.messages.push_back(user());
    m.d.current.messages.push_back(asst({
        make_call("call_real_1", "remember",
                  json{{"text", "durable fact"}}, /*terminal=*/false),
    }));
    auto n = agentty::app::cmd::dedup_releaked_salvage_calls(m);
    check(n == 0, "structured memory tool not blocked");
    check(m.d.current.messages.back().tool_calls[0].is_pending(),
          "structured memory tool stays Pending");
}
