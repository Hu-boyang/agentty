// SPDX-License-Identifier: Apache-2.0
#pragma once
// agentty::fuzzy-由每个增量选择面板共享的一个评分子序列匹配
// 器（命令调色板、@-files、#-symbols、模型/提供者）。
// 按查询匹配的 HOW WELL 对候选进行排名（前导前缀 > word
// -boundary / 首字母缩略词 > 连续运行 > 分散子序列；
// 精确获胜），并返回匹配的字符偏移量，以便视图可以突出显示它们。从命令面板中提取，
// 因此每个 选择面板 都感觉同样智能，
// 因此 "why did this rank here" 恰好位于一个地方。

#include <cctype>
#include <climits>
#include <string>
#include <string_view>
#include <vector>

namespace agentty::fuzzy {

struct Match {
    int              score = INT_MIN;  // 更高=更好； INT_MIN = 不匹配
    std::vector<int> positions;        // 将 字节 偏移量匹配到候选者中
    [[nodiscard]] bool matched() const noexcept { return score != INT_MIN; }
};

// 单词边界是首字母缩略词-typable段的开始。
// 涵盖真实标签/路径/标识符使用的分隔符。
[[nodiscard]] inline bool is_word_boundary(std::string_view s, std::size_t i) noexcept {
    if (i == 0) return true;
    char p = s[i - 1];
    if (p == ' ' || p == '-' || p == '/' || p == '_' || p == '.'
        || p == '(' || p == ':') return true;
    // camelCase/PascalCase驼峰：下→上。
    char c = s[i];
    return (p >= 'a' && p <= 'z') && (c >= 'A' && c <= 'Z');
}

// 将`needle`打分(需要NOT在-lowercased之前)作为`hay`的子序列。
// Greedy left-to-right-对于病理情况来说不是最佳的，
// 但是对于实时过滤来说足够快且足够好，与建立的@-picker评分器相匹配。
// 如果 `needle` 根本不是子序列，则返回 no-match。
[[nodiscard]] inline Match score(std::string_view hay, std::string_view needle) {
    Match m;
    if (needle.empty()) { m.score = 0; return m; }
    if (needle.size() > hay.size()) return m;
    // ASCII快速-path小写。候选者是模型 ID / 提供者
    // 标签 / 路径 - ASCII - 因此分支击败 std::tolower，
    // 它会在每个字符上拖入区域设置机器，并主导对大型列表进行排名的
    // per-keystroke 成本。 Non-ASCII 字节 不变
    // 地通过（它们也从未被这里的 C-locale 有意义地小写）。
    auto lc = [](char c) noexcept -> char {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    };

    int s = 0, skipped = 0;
    std::size_t hi = 0, ni = 0;
    bool prev = false;
    m.positions.reserve(needle.size());
    while (ni < needle.size() && hi < hay.size()) {
        if (lc(hay[hi]) == lc(needle[ni])) {
            s += 16;
            if (prev)                       s += 18;  // 连续运行
            if (is_word_boundary(hay, hi))  s += 30;  // 首字母缩略词/单词开头
            if (hi == 0)                    s += 25;  // 前导前缀
            m.positions.push_back(static_cast<int>(hi));
            prev = true; ++ni; ++hi;
        } else { prev = false; ++skipped; ++hi; }
    }
    if (ni < needle.size()) return {};         // 不是子序列
    s -= skipped;                               // 轻微间隙罚分
    if (hay.size() == needle.size()) s += 40;   // 确切地说
    m.score = s;
    return m;
}

// 方便：`needle`是否模糊-match `hay`（子序列）？ Drop-in 代表
// 旧的布尔值 fuzzy_contains，但是子序列（不是子字符串），
// 因此 "sp4" 按照用户期望的方式匹配 "gpt-sonnet-4"。
[[nodiscard]] inline bool matches(std::string_view hay, std::string_view needle) {
    return score(hay, needle).matched();
}

} // namespace agentty::fuzzy
