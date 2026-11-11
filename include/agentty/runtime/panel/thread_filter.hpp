#pragma once
// agentty::ui::thread_filter — 已保存 线程 ^J 选择面板 所示。
//
// 视图和状态更新函数共享一个功能，因此您看到的行和线程
// 打开的Enter始终是同一行。

#include <string_view>
#include <vector>

#include "agentty/domain/conversation.hpp"   // 线程
#include "agentty/runtime/fuzzy.hpp"

namespace agentty::ui {

// 这个线程是否属于`workspace`？录制 工作区
// 之前的 线程 没有，仅显示在“全部”下。
[[nodiscard]] inline bool in_workspace(const Thread& t, std::string_view workspace) {
    return !workspace.empty() && t.workspace == workspace;
}

// `workspace`启动了多少线程。
[[nodiscard]] inline int workspace_thread_count(const std::vector<Thread>& threads,
                                                std::string_view workspace) {
    int n = 0;
    for (const auto& t : threads) n += in_workspace(t, workspace) ? 1 : 0;
    return n;
}

// 有多少个线程来自另一个工具（尚未打开或导入）。
[[nodiscard]] inline int foreign_thread_count(const std::vector<Thread>& threads) {
    int n = 0;
    for (const auto& t : threads) n += t.imported ? 1 : 0;
    return n;
}

// 实际有效的范围：当被要求时该文件夹具有任何线程，
// 否则全部。空文件夹永远不会显示空的 选择面板。
[[nodiscard]] inline bool effective_all(const std::vector<Thread>& threads,
                                        std::string_view workspace, bool all_workspaces) {
    return all_workspaces || workspace_thread_count(threads, workspace) == 0;
}

// 来自失败回复的旧auto-titler 已保存的标题。它没有命名任何内容，
// 因此 选择面板 显示您首先询问的内容。
[[nodiscard]] inline bool is_broken_title(std::string_view t) {
    return t.starts_with("(the model returned") || t.starts_with("(empty response")
        || t == "New thread";
}

// 选择面板显示的标题，这也是查询首先匹配的内容。
[[nodiscard]] inline std::string_view shown_title(const Thread& t) {
    if (t.title.empty() || is_broken_title(t.title))
        return t.preview.empty() ? std::string_view{"(untitled)"} : std::string_view{t.preview};
    return t.title;
}

// `threads`中的索引，按其新近顺序，在范围内并且其
// 标题或第一消息与`query`匹配。空查询 = 范围内的每个
// 线程。
[[nodiscard]] inline std::vector<int> visible_threads(const std::vector<Thread>& threads,
                                                      std::string_view query,
                                                      std::string_view workspace = {},
                                                      bool all_workspaces = true,
                                                      bool only_foreign = false) {
    const bool all = effective_all(threads, workspace, all_workspaces);
    std::vector<int> out;
    out.reserve(threads.size());
    for (int i = 0; i < static_cast<int>(threads.size()); ++i) {
        const auto& t = threads[static_cast<std::size_t>(i)];
        if (!all && !in_workspace(t, workspace)) continue;
        // 导入的线程也计数：它们来自另一个工具。
        if (only_foreign && !t.imported) continue;
        if (query.empty()
            || fuzzy::score(shown_title(t), query).matched()
            || (!t.preview.empty() && fuzzy::score(t.preview, query).matched()))
            out.push_back(i);
    }
    return out;
}

} // namespace agentty::ui
