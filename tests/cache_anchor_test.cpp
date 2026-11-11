// cache_anchor_test — SOTA 提示词-cache 断点位置。
//
// SOTA 提示词-cache 中断点位置。两个中断点滚动或锁定,
// 以及全部 正文 花掉其中的两块 Anthropic's 4-breakpoint
// 系统+工具的预算。此测试 锁 消息( I) -array 形状 :
//
//   • LONG 线程:一个历史深处的1-HOUR号锚+ 一个 滚动5米的钉子
//     最新消息( 第二次 -to-last 滚针被放弃,
//     因此请求从未超过4个总断点——超过4个 Anthropic 驱逐
//     系统 -prompt 针从前.
//   • 锚是QUANTIZED(一转后不动)。
//   • SHORT 线程 : 无锚; 经典滚动 PAIR ( ⁇ 二针.
//   • 信息断点不超过2个。

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "agtest.hpp"

#include "agentty/domain/conversation.hpp"
#include "agentty/provider/anthropic/transport.hpp"

using agentty::Message;
using agentty::Role;
using agentty::Thread;
using agentty::ThreadId;
using agentty::ToolCallId;
using agentty::ToolName;
using agentty::ToolUse;

namespace {

// A 线程 备注 `pairs` User/Assistant 文本转动 = 2 * 传真消息。
Thread make_thread(int pairs) {
    std::vector<Message> msgs;
    for (int i = 0; i < pairs; ++i) {
        Message u; u.role = Role::User;      u.text = "u" + std::to_string(i);
        Message a; a.role = Role::Assistant; a.text = "a" + std::to_string(i);
        msgs.push_back(std::move(u));
        msgs.push_back(std::move(a));
    }
    return Thread{ThreadId{"t"}, "", std::move(msgs), {}, {}};
}

// 逐行浏览序列化信件阵列,并收集每封已发送信件的 ttl
// cache_control (或 减 减 ) ""
// 如果无的话)。 传输 数组。
struct Pin { int msg_index; std::string ttl; };
std::vector<Pin> collect_pins(const std::string& wire) {
    std::vector<Pin> pins;
    auto j = nlohmann::json::parse(wire);
    int idx = 0;
    for (const auto& msg : j) {
        for (const auto& block : msg.value("content", nlohmann::json::array())) {
            if (block.contains("cache_control")) {
                std::string ttl = block["cache_control"].value("ttl", "5m-default");
                pins.push_back(Pin{idx, ttl});
            }
        }
        ++idx;
    }
    return pins;
}

} // 命名空间结束。

TEST_CASE("cache_anchor") {
    namespace ap = agentty::provider::anthropic;

    // ── 1. 长 线程 : 正好1h锚+两个滚5m的钉子 ──
    {
        Thread t = make_thread(40);                    // 80条信息
        std::string wire = ap::messages_json_string(t);
        auto pins = collect_pins(wire);

        int anchors = 0, rolling = 0;
        int anchor_idx = -1;
        for (const auto& p : pins) {
            if (p.ttl == "1h") { ++anchors; anchor_idx = p.msg_index; }
            else               { ++rolling; }
        }
        check(anchors == 1, "long thread has exactly one 1h anchor breakpoint");
        check(rolling == 1, "long thread keeps one rolling 5m breakpoint (2nd dropped for the 4-cap)");
        check(pins.size() <= 2, "≤ 2 message breakpoints so total ≤ 4 (system+tools spend 2)");

        // 安切尔严格坐落在滚动对(深历史)之前.
        int total = 80;
        check(anchor_idx > 0 && anchor_idx < total - 2,
              "anchor lands deep in history, before the rolling pair");
    }

    // ── 2. 锚值为STABLE:增加一个转弯不移动它. ──
    {
        Thread t1 = make_thread(40);   // 80毫升( 80毫秒)
        Thread t2 = make_thread(40);
        // 追加一个用户+辅助对(82 msgs)——仍然相同的量子.
        Message u; u.role = Role::User;      u.text = "extra";
        Message a; a.role = Role::Assistant; a.text = "extra-a";
        t2.messages.push_back(std::move(u));
        t2.messages.push_back(std::move(a));

        auto anchor_of = [](const std::string& wire) {
            for (const auto& p : collect_pins(wire))
                if (p.ttl == "1h") return p.msg_index;
            return -1;
        };
        int a1 = anchor_of(ap::messages_json_string(t1));
        int a2 = anchor_of(ap::messages_json_string(t2));
        check(a1 != -1 && a1 == a2,
              "anchor index is unchanged when one turn is appended (quantized)");
    }

    // ── 3. 短 数 线程 : 无 1h 锚( 滚动对覆盖所有内容) ──
    {
        Thread t = make_thread(3);                     // 6条信息
        std::string wire = ap::messages_json_string(t);
        auto pins = collect_pins(wire);
        int anchors = 0;
        for (const auto& p : pins) if (p.ttl == "1h") ++anchors;
        check(anchors == 0, "short thread emits no 1h anchor");
        check(pins.size() <= 2, "short thread has only the rolling pins");
    }

    // ── 4. 空 点 线程 : 无断点, 有效 JSON ──
    {
        Thread t{ThreadId{"t"}, "", {}, {}, {}};
        std::string wire = ap::messages_json_string(t);
        auto pins = collect_pins(wire);
        check(pins.empty(), "empty thread has no cache breakpoints");
    }
}
