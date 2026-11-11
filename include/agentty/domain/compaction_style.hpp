#pragma once
// agentty::CompactionStyle-在分叉 线程 时在适当
// 位置压缩 OR 时如何总结 线程 的历史。每种样式都映射到不同的摘要 提示词
// （请参阅 cmd_factory.cpp compaction_prompt_for）。
// 该套件是 "all the ways to summarize" 前叉
// 选择面板 提供的；可恢复是默认值（原始可恢复延续摘要），必须 保持为零，
// 因此遗留的 StreamState/持久默认值解析为它。

#include <cstdint>
#include <string_view>

namespace agentty {

enum class CompactionStyle : std::uint8_t {
    Recoverable = 0,  // 结构化Task/State/Discoveries/Next-Steps切换
    Brief,            // 一小段——TL;DR
    Bullets,         // 要点作为项目符号轮廓
    Decisions,       // 做出的决定+理由（为什么，而不是什么）
    Handoff,         // 仅可操作的next-steps，对于新代理
    Narrative,       // 流畅的散文回顾整个会议
};

inline constexpr CompactionStyle kCompactionStyles[] = {
    CompactionStyle::Recoverable, CompactionStyle::Brief,
    CompactionStyle::Bullets,     CompactionStyle::Decisions,
    CompactionStyle::Handoff,     CompactionStyle::Narrative,
};

[[nodiscard]] constexpr std::string_view to_string(CompactionStyle s) noexcept {
    switch (s) {
        case CompactionStyle::Recoverable: return "Recoverable";
        case CompactionStyle::Brief:       return "Brief";
        case CompactionStyle::Bullets:     return "Bullets";
        case CompactionStyle::Decisions:   return "Decisions";
        case CompactionStyle::Handoff:     return "Handoff";
        case CompactionStyle::Narrative:   return "Narrative";
    }
    return "?";
}

// One-line对选择面板行的人类描述。
[[nodiscard]] constexpr std::string_view describe(CompactionStyle s) noexcept {
    switch (s) {
        case CompactionStyle::Recoverable:
            return "structured, recoverable handoff (task · state · next steps)";
        case CompactionStyle::Brief:
            return "one short paragraph — the TL;DR";
        case CompactionStyle::Bullets:
            return "key points as a bullet outline";
        case CompactionStyle::Decisions:
            return "decisions made and why (rationale over mechanics)";
        case CompactionStyle::Handoff:
            return "actionable next steps only, for a fresh agent";
        case CompactionStyle::Narrative:
            return "flowing prose recap of the whole session";
    }
    return "";
}

static_assert(static_cast<std::uint8_t>(CompactionStyle::Recoverable) == 0,
              "Recoverable must stay zero (legacy/default resolves to it).");

} // namespace agentty
