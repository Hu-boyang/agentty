#pragma once
// agentty::provider::wire-取代-read崩溃。
//
// read-heavy编码循环重复读取相同的文件：读取foo.cpp、
// 编辑它、再次读取；或绕道后重新-read同一个切片。每一个较早的读取，
// 其 字节 和 LATER 轮次重新-served 或无效，都会在模型不再
// 需要的 传输 上携带 过期 full-fidelity 正文 — 它已经具有
// 来自新轮次的更新状态。 Age-fading (
// wire::cap_tool_result_aged) 最终会缩小它，
// 但只有在 kFullResultWindow 转动之后；在此之前，
// 每回合都会完整重播 60 KiB 条内容。将这些 过期 读取折叠为 one-line 指针
// IMMEDIATELY 是实际编码会话中最大的单个令牌回收。
//
// "Stale"是整个游戏，并且它比"same path"
// 窄：稍后读取的DIFFERENT切片不携带先前切片的行，
// 因此它不能代替它。请参阅下面的跨度逻辑。
//
// 这存在于其自己的 头文件 (不是 wire.hpp，
// 这是故意的依赖-light框架)中，因为它需要域类型。所有四个 传输层（
// Anthropic、OpenAI-compat、ChatGPT/Codex、
// Ollama）共享它，因此像淡入淡出策略这样的策略仅存在一次。

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "agentty/domain/conversation.hpp"

namespace agentty::provider::wire {

// 被取代的读取折叠到的确定性指针。 FIXED 文本（没有 字节
// 计数/位置），因此给定的被取代的读取始终以相同的方式序列
// 化 - 提示词缓存 永远不会对其进行搅动。
inline constexpr std::string_view kSupersededReadPointer =
    "(this read's content was dropped to save context: a later read covers "
    "these lines, or the file was edited since. If you need these lines and "
    "they are not in a later result, read them again.)";

// 工具调用目标的文件路径，如果有的话。对于非 -file 工具为空。
// read/edit/write/remove/move均携带`path`(或别名)中的目标。
//
// 归一化为一个绝对拼写，因为折叠将路径作为字符串进行比较：没有它，
// "/abs/src/x.cpp"上的较晚的`edit`不会取代"src/x.cpp"
// 的较早的`read`，并且传输继续服务于前-edit 字节，
// 就好像它们是当前的一样。
[[nodiscard]] inline std::string tool_target_path(const ToolUse& tc) {
    if (!tc.args.is_object()) return {};
    for (const char* key : {"path", "file_path", "filepath", "filename"}) {
        auto it = tc.args.find(key);
        if (it == tc.args.end() || !it->is_string()) continue;
        std::string p = it->template get<std::string>();
        if (p.empty()) continue;
        std::filesystem::path fp{p};
        if (fp.is_relative()) {
            static const std::filesystem::path cwd = [] {
                std::error_code ec;
                auto c = std::filesystem::current_path(ec);
                return ec ? std::filesystem::path{} : c;
            }();
            if (!cwd.empty()) fp = cwd / fp;
        }
        return fp.lexically_normal().generic_string();
    }
    return {};
}

// ── 读结果实际上给 传输 ──────────────────────────
//
// 崩溃通常集中在 PATH 上，它悄悄地假设文件的每次读取都是对
// 文件的每次其他读取的替代。事实并非如此：一个窗口读取携带一个切片，
// 并且 `read` 是为逐个切片分页大文件而显式构建的。因此，
// 读取第二个区域会擦除第一个区域，并且模型得到一个指向从未包含这些行的
// 结果的指针。这是不可恢复的 mid-turn — 它读取为
// "the read tool is broken"，并且模型要么在循环中
// re-reads，要么回退到 `cat`。最近测量了 40 多个 线
// 程：3323 次读取中的 121 次以这种方式作为指针返回。
//
// 因此，仅当稍后的读取的跨度为CONTAINS时，读取才被折叠。

inline constexpr int kWholeFile = std::numeric_limits<int>::max();

// 包括1-based在内的线跨度。 `known == false`
// 意味着 "can't tell"：这样的结果既不会取代也不会被另一
// 次读取取代，因为错过的崩溃只会花费代币，而错误的崩溃会花费 字节。
struct ReadSpan {
    bool known = false;
    int  lo    = 1;
    int  hi    = 0;
};

[[nodiscard]] inline bool covers(const ReadSpan& outer, const ReadSpan& inner) noexcept {
    return outer.known && inner.known
        && outer.lo <= inner.lo && outer.hi >= inner.hi;
}

// 事实真相，读出正文。每当 `read` 保留任何内容时，它都会打印
// `[showing lines A-B of N]` 页脚，因此它的 ABSENCE 意味着
// 整个文件都在此结果中。
//
// nullopt = 正文 没有说什么，请询问参数。
// 未知 = 正文 表示它根本不携带任何行范围（轮廓或过去的-the-end
//            偏移量）——没有什么可比较的，在此停止。
[[nodiscard]] inline std::optional<ReadSpan>
read_span_from_output(std::string_view out) {
    constexpr auto npos = std::string_view::npos;
    // 大纲是 符号 索引，而不是文件内容：它不能代表任何行范围，
    // 甚至不能代表它所要求的行范围。
    if (out.find("# Outline of ") != npos) return ReadSpan{};
    if (out.find("is past the end of the file") != npos) return ReadSpan{};

    constexpr std::string_view kTag = "[showing lines ";
    const auto at = out.rfind(kTag);
    if (at == npos) {
        if (out.empty()) return ReadSpan{};
        return ReadSpan{true, 1, kWholeFile};
    }
    const char* p = out.data() + at + kTag.size();
    const char* e = out.data() + out.size();
    int lo = 0;
    const auto r1 = std::from_chars(p, e, lo);
    if (r1.ec != std::errc{} || r1.ptr == e || *r1.ptr != '-') return std::nullopt;
    int hi = 0;
    const auto r2 = std::from_chars(r1.ptr + 1, e, hi);
    if (r2.ec != std::errc{} || hi < lo) return std::nullopt;
    return ReadSpan{true, lo, hi};
}

// 回退 代表 正文，我们无法读取（旧版本的 线程 已保存，改写的页脚）。
// 在 mcp-cpp 的 fs.cpp 中镜像 parse_read_args。
// 如果这种情况发生变化，这只会变得更加保守——上面的 正文 解析
// 是在当前版本上运行的。
[[nodiscard]] inline ReadSpan read_span_from_args(const nlohmann::json& args) {
    if (!args.is_object()) return {};
    if (auto it = args.find("symbol");
        it != args.end() && it->is_string() && !it->template get<std::string>().empty())
        return {};   // 从参数中无法得知 符号 的行

    auto num = [&](std::initializer_list<const char*> keys) -> std::optional<int> {
        for (const char* k : keys) {
            auto it = args.find(k);
            if (it != args.end() && it->is_number_integer())
                return it->template get<int>();
        }
        return std::nullopt;
    };
    const auto off = num({"offset", "start_line", "start", "from_line"});
    const auto lim = num({"limit", "num_lines", "max_lines", "count"});
    const auto end = num({"end_line", "to_line", "endLine"});

    if (off && *off < 0) return {};   // 末尾 读取：无绝对跨度
    const int lo = (off && *off > 0) ? *off : 1;
    if (end && *end >= lo) return ReadSpan{true, lo, *end};
    if (lim && *lim > 0)   return ReadSpan{true, lo, lo + *lim - 1};
    // 无结束：start-anchored读取获得聚焦窗口，裸读取(路径)
    // 获得整个-file默认值。
    if (off) return ReadSpan{true, lo, lo + 249};
    return ReadSpan{true, 1, 2000};
}

[[nodiscard]] inline ReadSpan read_span(const ToolUse& tc) {
    if (auto s = read_span_from_output(tc.output())) return *s;
    return read_span_from_args(tc.args);
}

// 该工具是否确实导致文件的 CARRY 的 字节 的结果？
//
// 大多数读取结果都是如此，包括CACHE HIT：fs层重播它之前在
// `[cached — unchanged …]`标签下提供的确切文本，
// 因此该结果是完整副本并且可以像任何其他结果一样取代旧的副本。
//
// 不能取代的是POINTER：正文仅表示"look at an earlier tool_result"的结果。
// fs 层过去常常以这种方式回答每个重复，并且这种崩溃加上哨兵相互破坏 - read
// #1 在 read #2 存在时被取代，并且 read #2 不携带 字节，
// 因此内容出现在 传输 上的 NO 工具结果中。测量；该模特自己的文字是
// 两个占位标记互相指向，却没有任何工具结果包含文件正文。
// 之后又回落到`cat`。
// Claude 代码与问题 #53578
// / #60684 存在相同的故障。
//
// fs层现在提供内容而不是指针，因此该防护不应该
// 在当前构建上触发。它保留有两个原因：旧版本的
// 线程 SAVED 用旧的哨兵文本重新水化，
// 而 不变量（指针无法替换它指向的东西）值得在
// 结构上强制执行，而不是相信下面的层不会回归。
[[nodiscard]] inline bool read_result_has_body(const ToolUse& tc) {
    std::string_view out = tc.output();
    while (!out.empty() && (out.front() == '\n' || out.front() == ' '))
        out.remove_prefix(1);
    if (out.empty()) return false;
    // 锚定，未搜索。这些哨兵取代了 整个 正文，而这个 仓库 自己的来源
    // 引用了它们——一种称为真正读取这个 头文件 "bodyless"
    // 的子字符串搜索，然后将其从 edit-invalidation
    // 路径中隐藏起来，并将 pre-edit 字节 作为当前路径。
    // `[cached`故意缺席：正文保存真实文本。
    static constexpr std::string_view kUnchanged =
        "File unchanged since last read";   // 遗留的，不再发出
    return !out.starts_with(kSupersededReadPointer)
        && !out.starts_with(kUnchanged);
}

// 改变文件的工具。只有这些才能较早地读取 过期；相同路径的 grep、
// outline 或 read_filter 会读取它并且不会改变任何内容。
[[nodiscard]] inline bool changes_file(std::string_view tool) noexcept {
    for (std::string_view w : {"edit", "write", "remove", "move", "apply_patch",
                               "multi_edit", "replace", "rewrite_structural"})
        if (tool == w) return true;
    return false;
}

// 识别较早的`read`结果，其文件在线程中再次被触及LATER（
// 被同一路径的另一个read/edit/write/remove/move）。
// 返回要折叠的 ToolCallId 字符串集。每个文件的 MOST
// RECENT 读取永远不会在集合中（这是模型原因的实时副本）。
// 仅终端，读取成功参与；错误读数保持不变（它们
// 也不会消失——模型需要完整的失败文本）。
[[nodiscard]] inline std::unordered_set<std::string>
superseded_read_ids(const std::vector<Message>& msgs) {
    struct Live { std::string id; ReadSpan span; };
    std::unordered_map<std::string, std::vector<Live>> live;   // 路径->实时读取
    std::unordered_set<std::string> superseded;

    for (const auto& m : msgs) {
        if (m.role != Role::Assistant) continue;
        for (const auto& tc : m.tool_calls) {
            const std::string path = tool_target_path(tc);
            if (path.empty()) continue;

            const bool is_read = tc.name.value == "read";
            // 路径上的grep/outline/read_filter既不覆盖读取的字节也不改变文件：
            // 不理会读取。
            if (!is_read && !changes_file(tc.name.value)) continue;
            const bool ok_read = is_read
                && tc.is_terminal() && !tc.is_failed() && !tc.is_rejected();
            // 仅服务于哨兵的读取是指向较早结果的指针，
            // 因此它既不能取代该结果也不能成为实时副本
            // ——做任何一个都会使字节完全陷入困境。
            // WRITE/EDIT/REMOVE仍然取代：它改变了文件，
            // 所以旧的正文实际上是过期，无论它包含什么。
            const bool carries_body = !is_read || read_result_has_body(tc);
            if (!carries_body) continue;

            auto& bucket = live[path];
            if (!is_read) {
                // 文件已更改：每次较早读取的文件都是 过期，
                // 无论其保存的范围如何，因此范围不会进入其中。
                for (const auto& l : bucket) superseded.insert(l.id);
                bucket.clear();
                continue;
            }
            const ReadSpan span = read_span(tc);
            std::erase_if(bucket, [&](const Live& l) {
                if (!covers(span, l.span)) return false;
                superseded.insert(l.id);
                return true;
            });
            // 未知-span读取也进入桶中：读取不能折叠它们（covers()为假），
            // 但稍后的EDIT必须折叠它们。
            if (ok_read) bucket.push_back({tc.id.value, span});
        }
    }
    return superseded;
}

[[nodiscard]] inline std::unordered_set<std::string>
superseded_read_ids(const Thread& t) {
    return superseded_read_ids(t.messages);
}

} // namespace agentty::provider::wire
