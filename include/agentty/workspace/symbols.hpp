#pragma once
// 工作区 符号枚举-遍历活动工作区根，应用小语言-aware正则表达式集，
// 并返回找到的所有命名定义。与 files.hpp
// 属于同一系列：运行时 的 `#` 选择面板 使用
// 的 pure-I/O 扫描仪。
//
// SymbolEntry 位于此处（不在
// runtime/panel/symbol.hpp 中），因为它的形状由扫描仪发射的
// 内容决定； UI 状态是 下游 — symbol::Open 只是保存
// 这些的向量。

#include <cstddef>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include "agentty/util/snapshot.hpp"
#include <maya/runtime.hpp>

namespace agentty {

struct SymbolEntry {
    std::string name;             // 标识符，e.g。 "submit_message"
    std::string path;             // 工作区-relative 文件路径
    int         line_number = 0;  // 1-based
};

// 遍历工作区一次，返回与语言正则表达式集匹配的所有定义。
// Cached per-process：第一个调用走磁盘；
// 后续的调用共享缓存的缓冲区。 `cap` 条目的
// 上限将 选择面板 的工作集限制在巨大的 仓库 上。
//
// 返回 SNAPSHOT 而不是 const-ref。 const-ref
// 形式是潜在的 use-after-free：它将拥有的 shared_ptr
// 复制到本地的 block-scoped 中，并返回一个比它寿命更长的引用。
// 快照 将 所有权 与数据一起带出，因此当调用者仍持有句柄时，无法释放 缓冲区。
// 参见util/snapshot.hpp.
[[nodiscard]] util::Snapshot<std::vector<SymbolEntry>>
list_workspace_symbols(std::size_t cap = 50000);

// 运行（并行）符号扫描——单次-flight，重复至调用是安全的。
// 同步：在启动时在后台任务上运行它，因此第一个`#`是即时的。
// 一旦请求 `stop`，就会提前返回，不发布任何内容。
void prewarm_workspace_symbols(const maya::unlocked& cap, std::stop_token stop = {},
                               std::size_t max_entries = 50000);

// Non-blocking:符号索引建好了吗？作曲家打开 `#` 选择面板
// INSTANTLY 并显示 "indexing…" 直到返回 true。
[[nodiscard]] bool symbols_ready();

// 单个源代码行声明的定义，或者为空。 Pure：暴露，
// 因此可以在没有文件 源码树 的情况下测试匹配器。
[[nodiscard]] std::string_view symbol_on_line(std::string_view line) noexcept;

// Case-insensitive子串过滤在符号 NAME(不是路径)上。
// 返回 `entries` 中的索引，以便 分派器 使用与
// 选择面板 呈现的相同视图解析光标 → (名称、路径、
// 行)。
[[nodiscard]] std::vector<std::size_t>
filter_symbols(const std::vector<SymbolEntry>& entries, std::string_view query);

} // namespace agentty
