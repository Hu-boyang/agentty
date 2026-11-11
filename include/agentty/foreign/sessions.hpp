#pragma once
// agentty::foreign-来自其他代理的对话（Codex，Claude代码）。
//
// 他们的会话文件是实时读取的并且从不写入。会话在 ^J 选择
// 面板 中显示为一行；打开它会将其变成具有稳定 ID 的正常
// agentty 线程，因此再次打开它会重用该 线程。

#include "agentty/domain/conversation.hpp"
#include "agentty/foreign/names.hpp"
#include "agentty/foreign/scan.hpp"
#include "agentty/util/io.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace agentty::foreign {

// agentty 线程 id会话映射到：16十六进制，每个会话稳定。
[[nodiscard]] ThreadId thread_id_for(Source, std::string_view session_id);

// 一个会话在它变成线程之前完整读取。
struct Parsed {
    std::string session_id;
    std::string cwd;
    std::string title;          // 该工具自己的标题，当它保留一个时
    std::chrono::system_clock::time_point started{};
    std::chrono::system_clock::time_point updated{};
    std::vector<Message> messages;
    std::vector<Thread::CompactionRecord> compactions;
    bool skip = false;          // 不是用户会话（子代理，SDK/ACP运行）
};

struct ParseOptions {
    bool images = true;         // false:跳过解码图像(头文件扫描)
};

// 整个.jsonl文件的文本上的纯解析器。
[[nodiscard]] Parsed parse_codex(std::string_view jsonl, ParseOptions = {});
[[nodiscard]] Parsed parse_claude(std::string_view jsonl, ParseOptions = {});

// 解析为 agentty 线程 的会话：稳定 ID、
// 出处和 选择面板 摘要。
[[nodiscard]] Thread to_thread(Source, Parsed, const std::filesystem::path& file,
                               std::uint64_t size, std::int64_t mtime);

// 每个工具保存其会话（$CODEX_HOME、$CLAUDE_CONFIG_DIR)。
[[nodiscard]] std::filesystem::path sessions_root(Source, const std::filesystem::path& home);

// 该机器上的每个用户会话。将最后一次扫描传回：大小
// 和运行时间未更改的文件不会再次读取。
[[nodiscard]] Scan list_sessions(Io, const Scan& previous);

// 阅读整个一节。当文件消失或不可读时 nullopt。
[[nodiscard]] std::optional<Thread> read_session(Io, const Thread::Import&);

// `agentty sessions`:列出它们，最新的在前。 `--dump N` 打印
// 第 N 个如何读取为 agentty 线程。
int cli(Io, const std::vector<std::string>& argv);

}  // namespace agentty::foreign
