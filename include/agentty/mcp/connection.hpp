#pragma once
// agentty::mcp::Connection — 一台 MCP 服务器，作为 cap::CapabilityProvider。
//
// mcp-cpp没有自己的客户端连接：它声明方法并应答服务器的回调(
// mcp::ClientHandlers)。这是运行的部分：字节 通道上的
// rpc::Peer（子级 stdio，或作为一个通道呈现的 HTTP 传输层）、
// initialize/discover 握手、
// 缓存的 tools/resources/prompts 列表以及带有 MRTR 和 取消 的工具 调用。
//
// 链接打开通道。 reconnect() 要求它提供新鲜的，
// 因此 stdio 和 HTTP 口味的不同之处仅在于 链接。

#include <atomic>
#include <chrono>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <maya/runtime.hpp>
#include <mcp/cap/capability.hpp>
#include "agentty/util/child_process.hpp"
#include <mcp/client.hpp>

#include "agentty/rpc/peer.hpp"
#include "agentty/util/callback.hpp"
#include "agentty/util/sendable.hpp"

namespace agentty::mcp {

// 连接如何到达其服务器。
struct Link {
    // 打开新通道（生成子通道，设置 HTTP 会话）。
    // 失败时抛出。返回的持有者会保持支持通道的所有内容，
    // 并在 对端 停止后被丢弃。
    struct Opened {
        rpc::Channel          channel;
        std::shared_ptr<void> holder;
    };
    std::function<Opened()> open;
    // 支持通道的东西是否仍然正常（孩​​子活着，HTTP没有失败）？
    std::function<bool(const std::shared_ptr<void>& holder)> alive;
    // 设置传输层带外通告的协议版本(HTTP MCP
    // -Protocol-Version 头文件)。空：不发送。未设置 stdio。
    std::function<void(const std::shared_ptr<void>& holder, std::string version)> set_protocol_version;
};

// 服务器作为子进程产生，通过其stdio进行对话。
[[nodiscard]] Link stdio_link(::agentty::util::ChildProcess::Spawn spawn);

struct ConnectionConfig {
    std::string                 name;   // 原点变为"mcp:<name>"
    ::mcp::Implementation       client_info{"agentty", "0"};
    std::chrono::milliseconds   handshake_timeout{10'000};
    std::chrono::milliseconds   call_timeout{60'000};
    std::vector<::mcp::Root>    roots;
    std::function<void(const ::mcp::ResourceUpdatedParams&)> on_resource_updated;
    std::function<void(const ::mcp::LoggingMessageParams&)>  on_log;
};

class Connection final : public ::mcp::cap::CapabilityProvider {
public:
    // 现在连接；如果握手失败则抛出。
    Connection(ConnectionConfig cfg, Link link);
    ~Connection() override;

    Connection(const Connection&)            = delete;
    Connection& operator=(const Connection&) = delete;

    [[nodiscard]] std::string_view origin() const noexcept override { return origin_; }

    [[nodiscard]] std::vector<::mcp::Tool> list() const override;
    [[nodiscard]] std::string instructions() const override;
    [[nodiscard]] ::mcp::cap::Result execute(const ::mcp::cap::Request& req) override;

    [[nodiscard]] std::vector<::mcp::Resource> resources() const override;
    [[nodiscard]] std::vector<::mcp::ResourceTemplate> resource_templates() const override;
    [[nodiscard]] bool read_resource(const std::string& uri,
                                     std::vector<::mcp::ResourceContents>& out,
                                     std::string& err) override;

    [[nodiscard]] std::vector<::mcp::Prompt> prompts() const override;
    [[nodiscard]] bool get_prompt(const std::string& name,
                                  const std::vector<std::pair<std::string, std::string>>& args,
                                  ::mcp::GetPromptResult& out,
                                  std::string& err) override;

    void set_on_list_changed(ListChangedFn fn) override;

    [[nodiscard]] bool alive() const;
    // 实时连接协商的能力（当关闭时为空）。
    [[nodiscard]] ::mcp::ServerCapabilities server_capabilities() const;

    // 对端，持有者，缓存列表。 connection.cpp 外部不透明；
    // 仅公开，以便其同步声明可以命名它。
    struct Live;

private:

    // 能力-carrying核心。上面的 CapabilityProvider
    // 覆盖是入口点（它们的签名是 mcp-cpp）并在那里铸造 线程 的根；
    // 请参阅 connection.cpp 的 头文件 评论。
    ::mcp::cap::Result execute_impl(const maya::unlocked&, const ::mcp::cap::Request& req);
    std::expected<std::shared_ptr<Live>, std::string> ensure_live(const maya::unlocked&);
    std::shared_ptr<Live> connect(const maya::unlocked&);
    ::mcp::ClientHandlers handlers(std::weak_ptr<Live> live);

    void refresh_tools(const maya::unlocked&, Live& l) const;
    void refresh_resources(const maya::unlocked&, Live& l) const;
    void refresh_prompts(const maya::unlocked&, Live& l) const;

    ConnectionConfig          cfg_;
    Link                      link_;
    std::string               origin_;

    // 实时连接在重新连接时整个交换。读者拿快照；
    // 调用 在其持续时间内保持其 快照 处于活动状态。
    // 服务器的功能位于其中：重新连接会协商新的功能，
    // 而旧 Live 上的读者会继续看到旧的功能。
    maya::published<Live> live_;
    // 一次一个重新连接（原子门；参见connection.cpp)。
    std::atomic<bool>     reconnecting_{false};
    // 主机的list_changed 回调，从对端的读取器读取。
    // 与 处理函数 共享，其寿命可能比该对象长（参见 handlers()）。
    using Changed = util::Callback<void()>;
    std::shared_ptr<maya::published<const Changed>> on_changed_ =
        std::make_shared<maya::published<const Changed>>();
};

}  // namespace agentty::mcp
