#pragma once
// agentty::mcp —HTTP(可流式HTTP)MCP服务器提供商。
//
// 构建由通过可流式HTTP 传输层到达的远程MCP服务器支持的cap::
// CapabilityProvider (通过POST讲JSON-RPC的单个端点URL，
// 具有可选的text/event-stream响应)。它与 stdio
// 路径使用的 mcp::Connection 相同，通过 agentty 自己的
// HTTP/2 客户端（TLS、连接 池、取消）而不是生成的进程。
//
// PERF:与stdio路径一样，构造同步连接（由握手超时限制）并且仅当`http`
// /`url`服务器在.agentty/mcp.json中被命名时才被调用——没有
// 远程MCP服务器的用户无需支付任何费用。重磅mcp-cpp +
// http模板仅限于src/mcp/http_server.cpp；这个头文件是
// dependency-light（一个forward-declared上限类型+一个配置结构）。

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// 转发-declare上限提供者类型，因此该头文件不会将模板
// -heavy mcp-cpp能力层拖到每个包含器中。
namespace mcp::cap { class CapabilityProvider; }

namespace agentty::mcp {

// 一个远程 MCP 服务器的连接配置，从如下配置条目解析：
//   "deepwiki":{
//     "url":"https://mcp.deepwiki.com/mcp"，
//     "headers":{"Authorization":"Bearer ..."}
//   }
struct HttpConfig {
    std::string url;        // https://host[:port]/path（本地主机允许使用 http://）
    std::vector<std::pair<std::string, std::string>> headers;   // 额外请求头文件(授权等)
    std::chrono::milliseconds handshake_timeout{15'000};
    std::chrono::milliseconds call_timeout{60'000};
    std::string workspace_root;
};

// 连接到服务器并在任何失败时返回 CapabilityProvider 或 nullptr
// (+ `err` 中的人工消息)——从不抛出。返回的提供者拥有 HTTP 传输层
// + mcp::Client；让它在registry/pool.中保持活力
[[nodiscard]] std::shared_ptr<::mcp::cap::CapabilityProvider>
make_http_provider(const std::string& name, const HttpConfig& cfg, std::string& err);

} // namespace agentty::mcp
