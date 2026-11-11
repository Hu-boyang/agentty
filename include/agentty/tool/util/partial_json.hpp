#pragma once
// Anthropic `input_json_delta` 高频路径的Partial-JSON帮助器。
//
// SSE 数据流将工具参数作为形成不完整的JSON的片段传送，
// 直到tool_use块关闭。两个实用程序可满足常见需求：
//
//   close_partial_json
//     (原始)将JSON值的任何前缀封闭为JSON、nlohmann::json和解析。
//     保留完整的成员；当 数据流 剪切（半个键、尚无值的键、
//     半个文字或数字）被删除时写入的内容，除了值字符串剪切 mid
//     -text 之外，它保留其文本（减去半个转义符或半个 UTF-
//     8 字符），以便实时预览显示它。
//     对 tests/partial_json_fuzz_test.cpp. 中的每个前缀进行模糊测试
//
//   sniff_string(原始，
//     密钥)-walks为`"key": "<value>"`的缓冲区。
//     仅在看到收盘价后才返回解码值； std::nullopt
//     直到那时。当完全关闭+解析太重而无法在每个刻度上运行并且您
//     只需要一个标量时很有用。
//
//   sniff_string_progressive(原始，
//     密钥)与sniff_string相同，但返回迄今为止积累的任何字节，
//     停止在缓冲区边缘上的半-escape处。对于其值使所有其他参数相形见绌的
//     字段（写入的 `content`，编辑的 `old_string`
//     / `new_string`）是必需的，因此 UI 不会等待 800
//     -line 文件上的结束引号来显示任何内容。
//
//   locate_string_value
//     (raw, key) 返回 `"key":"...` 的值字符串*内*第
//     一个字符的 字节 偏移量，如果前缀尚未完成，则返回 nullopt。
//     习惯于缓存位置一次，因此每次-tick预览从那里恢复，
//     而不是每次从字节 0重新-scanning 缓冲区。
//
//   decode_string_from(原始，
//     偏移)从`offset`开始解码JSON-escaped 字节，直到结束`"`
//     或结束-of-buffer。镜像 sniff_string_progressive
//     的 末尾 但跳过前缀 -walk，以便调用者可以重用缓存的偏移量。
//
//   ended_inside_string
//     (原始)为真，当且仅当 `raw` 停止，而 JSON 字符串值
//     仍处于打开状态（最后 `"` 是开启器，尚未匹配关闭器）。
//     close_partial_json 的同伴：当该谓词为真时，
//     该函数 SYNTHESISES 是一个结束 `"`，生成有效的 JSON，
//     其最后一个字符串在任意 字节 边界处被截断。对解析结果（`write`
//     / `edit` 内容、`bash` 命令）使用 分派 工具
//     的调用者必须在此拒绝，否则该工具将针对 half-written
//     正文 运行。参见 docs/corruption-analysis.md.
//     中的发现 3
//
// 所有四个对于空/格式错误的输入都是安全的。

#include <optional>
#include <string>
#include <string_view>

namespace agentty::tools::util {

std::string close_partial_json(std::string_view raw);

[[nodiscard]] std::optional<std::string>
sniff_string(std::string_view raw, std::string_view key);

[[nodiscard]] std::optional<std::string>
sniff_string_progressive(std::string_view raw, std::string_view key);

// 返回对应于 `"key":"...` 的 JSON 字符串值*内*第
// 一个 字节 的索引。 nullopt 直到 `"key":"`
// 完全存在于 缓冲区 中。追加-only 数据流 可以得到缓存 的结果。
[[nodiscard]] std::optional<std::size_t>
locate_string_value(std::string_view raw, std::string_view key);

// 将JSON-escaped 字节从`offset`解码到结束`"`或缓冲区的末尾。
// 镜像 sniff_string_progressive 的解码 末尾。
// `offset` 必须 <= raw.size()；通常是较早的
// locate_string_value 在相同 缓冲区 的前缀上返回的值。
[[nodiscard]] std::string
decode_string_from(std::string_view raw, std::size_t offset);

// 增量解码-原始[*通过..end)走一次，将解码的JSON-string
// 字节附加到`out`并且将`*through`推进到过去消耗的输入。
// 在该值的收盘 `"` 处停止（并在其之后前进 `*through`）或
// 在结束 -of-buffer 处停止。专为随着 `raw` 的增长而
// 重复 调用 而设计：在 数据流 上，累计工作量为 O(raw.size())，
// 而对于朴素的 re-decoding，累计工作量为 O(raw.size()²)。
// 当且仅当看到结束 `"` 时返回 true。传递 `closed`（由
// 调用者跨 调用 保留），因此稍后 调用 保持为 no-ops：`*through`
// 单独位于引用后面，并将下一个 JSON 成员解码为值。
bool decode_string_append(std::string_view raw,
                          std::size_t* through,
                          std::string& out,
                          bool* closed = nullptr);

// 当且仅当 `raw` 以未终止的 JSON 字符串结束时返回真 — 等效地，
// 当且仅当 `close_partial_json(raw)` 将
// 合成结束的 `"`。对 缓冲区 进行廉价的单一 -pass 扫描，
// 镜像 `close_partial_json` 使用的相同
// in_string/escape 状态机；没有 分配，O(raw.size())。
// 由 SSE 状态更新函数 中的抢救路径用来检测参数被截断的 mid
// -string-value（否则 解析 会成功地针对合成报价并以
// half-written 正文 静默运行工具）。
[[nodiscard]] bool
ended_inside_string(std::string_view raw) noexcept;

} // namespace agentty::tools::util
