// reasoning_ssot_test — "reasoning ‹off›" 输入 选择面板 意思是离开 传输.
//
// ── 该 BUG 国 务 院 ──────────────────────────────────────────────────────────────
//
// 模型( 模型) 选择面板 工作条读作 `reasoning ‹off›`
// 每一个副驾驶请求都出门 要求推理:
//
//     responses.request :... 模型=gpt-5.4工具=45推理=1 字节=99174
//
// 用户有他们关闭的思维障碍,并支付代币。三个不同的输入
// 可以回答"do we want reasoning",
// 他们不同意:
//
//   1. 努力带( m.d.effort ——活体,每 -model 控制,以及
//                                       唯一一个绑着钥匙的
//   2. m.d.show_reasoning - 旧的 ^R 切换。该主题 状态更新函数 手臂
//                                       仍然存在,但 无内容 派遣( ^R ),
//                                       所以, 标志 被冻在任何
//                                       settings.json
//                                       恰巧抱着... `true` 给
//   3. ui.thinking 隐藏着 外表 DISPLAY 偏好
//
// 再说一遍 build_body 发送
// `reasoning: {summary: "auto"}` 那时没有分层工作,
// 那不是 "no opinion" ——这是对推理摘要的明确要求。 切换 关,关 传输 问.
//
// ── 该 RULE ─────────────────────────────────────────────────────────────
//
//     努力的条条是 SSOT 无等级 − 没有关于 传输.
//
// 模型应该投入多少思考力度？
// 是一个你所选的模型的财产。 "Do I want to look at it"
// 属于你的财产 UI ,属于外观——但 DISPLAY 选择永远不能
// 悄悄改变我们 REQUEST 因为这是支付和放弃象征的方式。

#include <string>

#include <nlohmann/json.hpp>

#include "agtest.hpp"

#include "agentty/provider/chatgpt/responses.hpp"

using namespace agentty;
namespace cc = agentty::provider::chatgpt;
using json = nlohmann::json;

namespace {

// 一条小井 -formed ; 努力级别是每个案件不同之处。
provider::Request req_with_effort(std::string effort) {
    provider::Request req;
    req.model         = "gpt-5.4";
    req.system_prompt = "You are a coding agent.";
    req.effort        = std::move(effort);

    Message u;
    u.role = Role::User;
    u.text = "list the files";
    req.messages.push_back(u);
    return req;
}

}  // 命名空间结束。

TEST_CASE("reasoning ssot: no effort tier sends no reasoning field") {
    // 错误报告的确切配置: 条形文字取消, 所以在其中可能
    // 没有关于推理的内容。 传输 发送
    // `{"summary":"auto"}` 这里的思维是 用户
    // 关闭的转弯处 产生的思想块
    const json body = cc::build_body_for_test(req_with_effort(""));

    CHECK(!body.contains("reasoning"));
}

TEST_CASE("reasoning ssot: an effort tier sends that tier plus a summary") {
    // 合同的另一半是... `off` 意思关闭只有在
    // 真正的层次仍然要求摘要文本时才有用。
    // 没有 `summary` 推理模型烧掉思维符号,
    // 返回的痕迹没有可见,而这是相反方向的同样的废物。
    for (const char* tier : {"minimal", "low", "medium", "high", "xhigh"}) {
        const json body = cc::build_body_for_test(req_with_effort(tier));

        REQUIRE(body.contains("reasoning"));
        CHECK(body["reasoning"]["effort"] == tier);
        CHECK(body["reasoning"]["summary"] == "auto");
    }
}

TEST_CASE("reasoning ssot: the field is present iff a tier is set") {
    // 说明 不变量 做为双条件,所以未来的编辑,增加第三个臂(
    // 省略力度等级，但保留摘要。
    // 原始虫子的确切形状) 而不是在某人的帐单上失败了.
    for (const char* tier : {"", "minimal", "low", "medium", "high", "xhigh"}) {
        const std::string effort{tier};
        const json body = cc::build_body_for_test(req_with_effort(effort));

        CHECK(body.contains("reasoning") == !effort.empty());
    }
}
