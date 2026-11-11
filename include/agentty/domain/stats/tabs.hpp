#pragma once
// agentty::stats——选项卡表。 该 SSOT 用于面板显示的内容。
//
// `kTabs`是订单、标题、字幕、可用性和计数的单一来源。
// 其他一切都源自它：tab_title()、tab_step()、
// kTabCount。此替换的版本在一个 枚举 上有四个并行 switch 语句，
// 这是四个忘记案例的地方，并且有四个机会让他们不同意 - 同样
// 的论点将 ProviderDescriptor 和 kCommands
// 放在表上。
//
// 添加选项卡为：枚举器、此处为行、提取器。
// 编译器 列出了您忘记的任何内容。

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "agentty/domain/stats/facts.hpp"
#include "agentty/domain/stats/metric.hpp"

namespace agentty::stats {

enum class Tab : std::uint8_t {
    Session,
    Models,
    Smart,
    Tokens,
    Cache,
    Tools,
    Reasoning,
    Stream,
    Context,
    Retrieval,
};

struct TabDesc {
    Tab              id;
    std::string_view title;      // 条标签
    std::string_view subtitle;   // 面板的字幕行
    std::span<const Section> sections;
    // 隐藏无话可说的选项卡。未运行任何工具的会话不应
    // 显示“工具”选项卡，解释它未运行任何工具 -
    // 空状态是一个承认不应该绘制它的选项卡。
    bool (*available)(const Facts&);
};

// 每个选项卡的部分。声明 out-of-line，以便 kTabs 保持作为表格可读；
// 每个 `extract` 都是 extract.cpp 中的一项功能。
namespace sections {
extern const std::array<Section, 4> session;
extern const std::array<Section, 2> models;
extern const std::array<Section, 3> smart;
extern const std::array<Section, 2> tokens;
extern const std::array<Section, 3> cache;
extern const std::array<Section, 4> tools;
extern const std::array<Section, 6> reasoning;
extern const std::array<Section, 6> stream;
extern const std::array<Section, 2> context;
extern const std::array<Section, 1> retrieval;
}  // namespace sections

inline constexpr std::array<TabDesc, 10> kTabs{{
    {Tab::Session,  "Session",   "what this thread actually did",
     sections::session,
     [](const Facts&) { return true; }},
    {Tab::Models,   "Models",    "which model served each turn",
     sections::models,
     [](const Facts& f) { return !f.models.by_model.empty(); }},
    {Tab::Smart,    "Smart",     "is routing sending work to cheaper models",
     sections::smart,
     [](const Facts& f) { return f.smart.routed > 0; }},
    {Tab::Tokens,   "Tokens",    "what this thread cost in tokens",
     sections::tokens,
     [](const Facts& f) { return f.session.measured_turns > 0; }},
    {Tab::Cache,    "Cache",     "how much of the prefix was served from cache",
     sections::cache,
     [](const Facts& f) { return f.cache.turns_with_cache > 0; }},
    {Tab::Tools,    "Tools",     "what the agent did, and how long it took",
     sections::tools,
     [](const Facts& f) { return f.tools.total > 0; }},
    {Tab::Reasoning, "Reasoning", "is extended thinking earning its keep",
     sections::reasoning,
     [](const Facts& f) { return f.reasoning.turns > 0; }},
    {Tab::Stream,   "Stream",    "transport health \xe2\x80\x94 retries, stalls, throughput",
     sections::stream,
     [](const Facts& f) { return f.session.measured_turns > 0; }},
    {Tab::Context,  "Context",   "how close this thread is to the window",
     sections::context,
     [](const Facts& f) { return f.context.peak_input > 0; }},
    {Tab::Retrieval, "Retrieval", "is proactive retrieval finding anything",
     sections::retrieval,
     [](const Facts& f) { return f.retrieval.injections > 0; }},
}};

inline constexpr int kTabCount = static_cast<int>(kTabs.size());

[[nodiscard]] constexpr const TabDesc& tab_desc(Tab t) noexcept {
    for (const auto& d : kTabs) if (d.id == t) return d;
    return kTabs[0];
}
[[nodiscard]] constexpr std::string_view tab_title(Tab t) noexcept {
    return tab_desc(t).title;
}
[[nodiscard]] constexpr std::string_view tab_subtitle(Tab t) noexcept {
    return tab_desc(t).subtitle;
}
[[nodiscard]] inline bool tab_available(Tab t, const Facts& f) {
    const auto& d = tab_desc(t);
    return d.available && d.available(f);
}

// 可见选项卡，按表格顺序。面板渲染正是这个。
[[nodiscard]] inline std::vector<Tab> visible_tabs(const Facts& f) {
    std::vector<Tab> out;
    out.reserve(kTabs.size());
    for (const auto& d : kTabs)
        if (d.available && d.available(f)) out.push_back(d.id);
    if (out.empty()) out.push_back(Tab::Session);   // 会话始终可用
    return out;
}

// 在VISIBLE集合内步进，因此Tab/Shift-Tab永远不能落在未绘制的选项卡上。
// 双向缠绕。
[[nodiscard]] inline Tab tab_step(Tab cur, int delta, const Facts& f) {
    const auto vis = visible_tabs(f);
    const int n = static_cast<int>(vis.size());
    if (n == 0) return Tab::Session;
    int i = 0;
    for (int k = 0; k < n; ++k) if (vis[static_cast<std::size_t>(k)] == cur) { i = k; break; }
    int next = (i + delta) % n;
    if (next < 0) next += n;
    return vis[static_cast<std::size_t>(next)];
}

}  // namespace agentty::stats
