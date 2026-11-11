#pragma once
// agentty::tools::schema — 使 MCP 工具能够安全地发送给每个提供商。
//
// 一种不良工具用于使 每个 请求失败：提供者验证 `tools` 中的每个
// 工具（也包括延迟的工具）和 400 整个 调用。因此，每个 MCP 工具
// 都会在 加载 处经过一次：
//
//   check()什么会使提供商拒绝它。失败的工具将
//               被排除在请求之外，并显示原因；
//               其服务器的其余部分继续工作。
//   wire_name()是每个提供商都接受的名称：[A-Za-z0-9_-]，
//               最多64个，唯一的，跨运行稳定。
//   normalize()修复了仅是拼写问题的形状：
//               根anyOf/oneOf/allOf扁平化、$ref内联、
//               类型数组和重写的其他Gemini-hostile形式、
//               超大模式定义压缩。服务器接收到的内容没有改变。
//
// 在实时端点上测量：Anthropic 拒绝 ^[a-zA-Z0-9_.-]{1,64}$
// 和 模式定义 之外的属性键，这些属性键在 2020 年 12 月 JSON 模式定义 上无效；
// OpenAI 拒绝长度超过 64 的名称。Claude 代码、Codex 和 OpenCode
// 执行相同的三件事。
//
// 纯：无IO。

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace agentty::tools::schema {

// 模式定义发送时为空；否则是一个简短的原因（
// "property 'file path' has characters providers reject"）。（file path 属性名包含提供方不接受的字符。）
[[nodiscard]] std::string check(const nlohmann::json& schema);

// normalize()所做的，对于工具描述：将根
// 联合合并为一个对象，并且哪些参数组属于一起进入模型读
// 取的句子中。当什么都不需要说的时候就空了。
struct Normalized {
    nlohmann::json schema;
    std::string    note;
};
// 一种工具的模式定义的默认大小预算；较大的首先丢失嵌套
// 描述（Codex：5 KB）。
inline constexpr std::size_t kCompactBytes = 5'000;
[[nodiscard]] Normalized normalize(nlohmann::json schema,
                                   std::size_t compact_bytes = kCompactBytes);

// `mcp__<server>__<tool>`，已清理为 [A-Za-z0-9_-] 并适合 64 个字符。
// 当它不适合时，工具部分被缩短，并且附加完整原始名称的8
// -hex散列，因此两个长名称永远不会冲突，并且同一工具
// 每次运行都获得相同的名称。 `taken` 持有已发出的名字；
// 冲突会得到不同的哈希值。
inline constexpr std::size_t kMaxNameLen = 64;
[[nodiscard]] std::string wire_name(std::string_view server, std::string_view tool,
                                    std::unordered_set<std::string>& taken);

} // namespace agentty::tools::schema
