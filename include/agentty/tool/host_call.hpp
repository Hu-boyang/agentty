#pragma once
// mcp-cpp的Call::host指向调用 agentty的分派器所做的。
// mcp-cpp将其原封不动地交还给agentty自己的服务（Exec、
// 技能、子代理）。 头文件-only 因此执行层可以在没有桥的情况下读取它。

namespace agentty::tools {
struct CallContext;
namespace util::handoff { struct TrustedSnapshot; }

struct HostCall {
    const CallContext*                    ctx = nullptr;
    // 工作区、快照、分派器带手表壳-like、调用。
    // 沙箱重复使用它而不是再次行走源码树。空：未采取。
    const util::handoff::TrustedSnapshot* trusted = nullptr;
};

// Call::host后面的HostCall，或者对于没有通过分派器的调
// 用来说是空的（mcp-serve上的MCP客户端）。
[[nodiscard]] inline const HostCall& host_call(const void* host) noexcept {
    static constexpr HostCall kNone{};
    return host ? *static_cast<const HostCall*>(host) : kNone;
}

}  // namespace agentty::tools
