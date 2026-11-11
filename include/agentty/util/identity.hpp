#pragma once
// agentty::util::identity
// -ONE位置agentty表明远程服务是谁。
//
// ────────────────────────────────────────────────────────────────────
// 该文件为何存在
// ────────────────────────────────────────────────────────────────────
// 应用程序归因（问题#74）将 agentty 的名称和主页从
// 修饰字符串转换为 REMOTE SERVICE 键记录的值：
// OpenRouter 将请求记入由我们发送的 URL 标识的应用
// 程序页面，并且某些模型仅为命名其自身的客户端提供服务。
// 供应商围绕这一事实建立记录需要一个确切的定义，而不是按照当天需要的
// 传输层 的字面意义 带类型的 进行定义。
//
// 范围：这是描述ITSELF的agentty——名称和URL。它不是用于 per-vendor
// 协议 头文件 的地方。这些属于提供者行 (ProviderDescriptor::attribution)，
// 或者对于像 Copilot 的编辑器块这样的 request-shaped 垃圾，
// 属于 Endpoint::extra_headers。
//
// 不在这里（故意）：User-Agent 字符串。 "agentty/<version>" 是
// agentty/version.hpp 中的 ::agentty::user_agent()，位于版本旁边。

#include <string_view>

namespace agentty::util::identity {

// 产品名称，作为人类应该在其他人的仪表板中看到的。
// 小写，因为这就是 agentty 在其他地方的拼写方式（二进制程序、
// 仓库、文档）；显示 "agentty" 的供应商排行榜是正确的，
// "Agentty" 是不存在的不同产品。
inline constexpr std::string_view name = "agentty";

// 规范主页。这是应用程序页面上的标识符 OpenRouter（以及
// 类似的标识符），因此它必须是真正的公共 URL 并且必须保持稳定 -
// 更改它不会重命名应用程序页面，它会创建一个新的应用程序页面并孤立
// 旧的应用程序页面。 https，没有尾部斜杠。
inline constexpr std::string_view url = "https://agentty.org";

// OpenRouter市场类别：终端编码代理。
inline constexpr std::string_view category = "cli-agent";

}  // namespace agentty::util::identity
