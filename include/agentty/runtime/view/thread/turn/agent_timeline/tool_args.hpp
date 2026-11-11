#pragma once
// 在线程 渲染器和工具-card 渲染器之间共享的小型、纯助手。
// 与 helpers.hpp 分开，因为它们都在 ToolUse/args
// 形状上运行 - 它们不属于通用 color/UTF-8 东西。

#include <initializer_list>
#include <string>
#include <string_view>

#include <nlohmann/json_fwd.hpp>  // 仅const JSON&以下参数；完整类型位于 .cpp 中

#include "agentty/domain/conversation.hpp"

namespace agentty::ui {

// 安全字符串读取——当密钥丢失或非-string时为空。
[[nodiscard]] std::string safe_arg(const nlohmann::json& args, const char* key);

// 选择任意列出的键下的第一个非-empty字符串。镜像工具实现的别名
// -tolerant 解析（write/edit接受
// `path | file_path | filepath | filename`、
// `display_description | description`等）。如果没有这个，
// 即使该工具运行良好，视图也会读取一个规范密钥和一个选择别名 渲染 作为空白卡的模型。
[[nodiscard]] std::string pick_arg(const nlohmann::json& args,
                                   std::initializer_list<const char*> keys);

// 工具正在作用的文件路径，被解析为可见的 AS EARLY AS 可能。
// 在 流式传输 期间，解析的 `tc.args` 滞后于 传输
// （它是在约 120 毫秒油门上从部分 JSON 重新生成的 re
// -parsed），因此即使路径 字节 已经到达，卡也可以在
// edit/write 的前几分之一秒内显示裸露的 "…"。
// 首先尝试解析的参数（路径 | file_path | 文件路径 | 文件名），
// 然后回退到直接从 tc.args_streaming 中抓取值，
// 以便文件名在其 字节 着陆时立即出现。仅当尚未传输任何路径时才为空。
[[nodiscard]] std::string tool_path_arg(const ToolUse& tc);

// 从（可能是部分的）JSON文档中刮取`key`的第一个字符串值，
// 而没有解析——找到`"key"`，然后找到下一个`: "..."`，
// 遵循反斜杠转义并在结束引号处停止（或截断的缓冲区的末尾）。
// 当键或其值尚未流式传输时返回 ""。
// 便宜：一次 left-to-right 扫描，没有 分配，
// 直到找到值。
[[nodiscard]] std::string pick_streaming_string(std::string_view raw_json,
                                                std::string_view key);

// 当类型缺失/错误时，以默认值读取 Int。
[[nodiscard]] int safe_int_arg(const nlohmann::json& args, const char* key, int def);

// 尾随非-newline行的换行计数+1。零表示空输入。
[[nodiscard]] int count_lines(std::string_view s);

// 到目前为止，在该工具上花费了 调用 秒。运行→现在-开始；
// 终端→完成-开始；当 started_at 未设置时返回 0。
// 在工具运行时调用每个Tick，以便卡实时更新。
[[nodiscard]] float tool_elapsed(const ToolUse& tc);

// 去掉 bash 输出外层的 ```…``` 围栏及末尾元数据，
// stdout/stderr in。栅栏让模型看到明确的
// "literal output"边界，但在小部件自己的帧内是视觉噪声。
[[nodiscard]] std::string strip_bash_output_fence(std::string_view s);

// 从 bash/diagnostics 工具输出中提取退出代码 — 识别
// "failed with exit code N" (Zed-style) 和 "[exit code N]" (旧版)。
// 对于 超时 标记返回 124，当这些模式都不匹配时返回 0。
[[nodiscard]] int parse_exit_code(std::string_view output);

// 卡应读取该工具的输出。大多数工具将 调用 的 `display_description`
// 回显为第 1 行；这已经是卡片标题，留在其中会破坏每个 "Found N"
// /"HTTP ..." 解析 并显示为 正文 第 1 行。
[[nodiscard]] std::string_view card_output(const ToolUse& tc);

// 输出文本作为人应该阅读的文本：丢弃工具为模型写入的注释（`[output capped …]`、
// `[showing lines …]`、`[... N chars elided]`、
// `[extracted text from …]`)、ANSI转义并\r重绘，
// 并且折叠blank-line运行。仅供展示； 解析器 保留 card_output。
[[nodiscard]] std::string clean_output(std::string_view s);

} // namespace agentty::ui
