#pragma once
// ^J窥视的内容：线程被切割成显示线的轮流。在 线程 加载 的基础上
// 构建了 状态更新函数，因此 缓存 保存的是 KB，而不是成绩单。

#include <cstdint>
#include <string>
#include <vector>

#include "agentty/domain/conversation.hpp"

namespace agentty::ui {

struct PeekLine {
    enum class Kind : std::uint8_t { User, Reply, Tool };
    std::string who;    // 扬声器； "" 在连续线上
    std::string text;
    Kind        kind = Kind::Reply;
};
using PeekTurn = std::vector<PeekLine>;

// 每回合保留的行数：足以填满任何卡，因此 2000-line 回复的
// 成本与短回复的成本相同。
inline constexpr std::size_t kPeekLinesPerTurn = 24;

// 一条消息作为查看行：逐行文本（空白运行折叠），然后每个工具
// "▸ name" 调用。没有用于注入或路由消息的内容。
// `agent` 撰写回复的姓名（"agentty" 或 "codex"
// 对于从其他工具导入的回合）。
inline void peek_lines_of(const Message& msg, PeekTurn& out,
                          std::string_view agent = "agentty") {
    const bool user = msg.typed_by_user();
    if (!user && msg.role != Role::Assistant) return;
    if (msg.smart_routing || msg.is_proactive_context()) return;
    const std::string who = user ? "you" : std::string{agent};
    bool first = true;
    int blank = 0;
    const std::string& s = msg.text;
    std::size_t start = 0;
    while (start < s.size() && out.size() < kPeekLinesPerTurn) {
        const auto nl = s.find('\n', start);
        std::string line = s.substr(start, nl == std::string::npos ? std::string::npos
                                                                   : nl - start);
        start = nl == std::string::npos ? s.size() : nl + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.find_first_not_of(" \t") == std::string::npos) {
            if (first || ++blank > 1) continue;
            line.clear();
        } else {
            blank = 0;
        }
        out.push_back({first ? who : std::string{}, std::move(line),
                       user ? PeekLine::Kind::User : PeekLine::Kind::Reply});
        first = false;
    }
    for (const auto& tc : msg.tool_calls) {
        if (out.size() >= kPeekLinesPerTurn) break;
        out.push_back({first ? who : std::string{},
                       "\xe2\x96\xb8 " + std::string{tc.name.value}, PeekLine::Kind::Tool});   // ▸
        first = false;
    }
}

// 线程轮流：每个带类型的消息开始一个。在一个回合内，每次运行都会为
// 扬声器命名一次（代理回合包含许多消息，每个工具轮一条），并且一个工具
// 的 back-to-back 调用 读作 "▸ shell ×4"。
[[nodiscard]] inline std::vector<PeekTurn> peek_turns_of(const Thread& t) {
    std::vector<PeekTurn> turns;
    // 来自另一个工具会话的回复是该工具的。
    const std::size_t foreign_upto = t.imported ? t.imported->messages : 0;
    const std::string_view other = t.imported ? std::string_view{t.imported->source} : "";
    for (std::size_t i = 0; i < t.messages.size(); ++i) {
        const auto& msg = t.messages[i];
        if (msg.typed_by_user() || turns.empty()) turns.emplace_back();
        peek_lines_of(msg, turns.back(), i < foreign_upto ? other : "agentty");
    }
    std::erase_if(turns, [](const PeekTurn& v) { return v.empty(); });
    for (auto& turn : turns) {
        PeekTurn out;
        std::string speaker;   // 最后命名的线路是谁
        int repeat = 0;        // 额外的调用折叠到最后的工具线中
        auto flush = [&] {
            if (repeat > 0 && !out.empty())
                out.back().text += " \xc3\x97" + std::to_string(repeat + 1);   // ×
            repeat = 0;
        };
        for (auto& l : turn) {
            const std::string who = l.who.empty() ? speaker : l.who;
            if (l.kind == PeekLine::Kind::Tool && !out.empty()
                && out.back().kind == PeekLine::Kind::Tool && out.back().text == l.text) {
                ++repeat;
                continue;
            }
            flush();
            if (!l.who.empty() && l.who == speaker) l.who.clear();
            if (!who.empty()) speaker = who;
            out.push_back(std::move(l));
        }
        flush();
        turn = std::move(out);
    }
    return turns;
}

}  // namespace agentty::ui
