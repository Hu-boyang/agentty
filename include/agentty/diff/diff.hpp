#pragma once
// agentty diff域——结构化编辑的值类型，
// 加上计算和渲染它们的纯函数。没有文件系统，没有 I/O.

#include <cstdint>
#include <string>
#include <vector>

namespace agentty {

struct Hunk {
    enum class Status : std::uint8_t { Pending, Accepted, Rejected };
    int old_start = 0, old_len = 0, new_start = 0, new_len = 0;
    std::string patch;
    Status status = Status::Pending;
};

struct FileChange {
    std::string path;
    int added   = 0;
    int removed = 0;
    std::vector<Hunk> hunks;
    std::string original_contents;
    std::string new_contents;
};

namespace diff {

// 根据 before/after 文本计算统一差异和结构化 差异块。
[[nodiscard]] FileChange compute(const std::string& path,
                                 const std::string& before,
                                 const std::string& after);

// 渲染统一差异字符串（用于显示/用于传递到DiffView小部件）。
[[nodiscard]] std::string render_unified(const FileChange& c);

// 在 `original_contents` 之上应用接受的 差异块，跳过拒绝的。
[[nodiscard]] std::string apply_accepted(const FileChange& c);

} // namespace diff
} // namespace agentty
