#pragma once
// agentty::mcp::PluginModel——plugin/tool状态的值快照。
//
// 从client.hpp中分离出来，因此它可以存在于IN、TEA Model (model.hpp)中，
// 而不拖入整个MCP客户端API (其取决于tools::ToolDef并且将形成包含循环）。
// 这是一种普通数据类型 - 仅限 <string>/<vector> - 正是这样 Model
// 可以拥有它，并且 view/visual_hash 可以将插件面板视为 Model 的纯功能。
// 参见 docs/design/ plugin-model.md 和
// docs/design/plugin-model-in-model.md.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace agentty::mcp {

// 一种广告的连接服务器工具。
struct ToolState {
    std::string name;            // 裸广告名称（无 mcp__ 前缀）
    std::string description;     // 广告描述（针对UI）
    bool        enabled = true;  // 配置 tools.exclude 中的 不
    bool        pinned = false;  // 配置tools.pin：始终已加载，从不按需
    bool        on_demand = false; // 已启用，现在不是已加载； tool_search 找到了
    // Non-empty：该工具的模式定义将使提供商拒绝每个请求，
    // 因此将其排除在外。显示在其行上，以便服务器作者可以修复它。
    std::string skipped;
};

// 从何处读取服务器的配置条目——足以使 选择面板
// 标记范围并且使 状态更新函数 将编辑路由回正确的文件，
// 而不将整个范围代数拉入该值 头文件。
enum class Origin : std::uint8_t { User, Project, Explicit };

[[nodiscard]] constexpr std::string_view to_string(Origin o) noexcept {
    switch (o) {
        case Origin::User:     return "user";
        case Origin::Project:  return "project";
        case Origin::Explicit: return "explicit";
    }
    return "user";
}

// 一台已配置的MCP服务器+其实时连接状态。
struct ServerState {
    std::string name;            // 配置键
    std::string command;         // 配置命令（HTTP/SSE 为空）
    std::string url;             // 配置 url（stdio 为空）
    bool        connected = false;   // 本次握手成功
    bool        disabled = false;    // 配置 `disabled:true` — 未故意连接
    std::string error;           // 为什么未连接（如果connected/ok则为空）
    Origin      origin = Origin::User;   // 该条目来自哪个范围
    std::string config_dir;      // 保存该条目的 mcp.json 的 .agentty 目录
    bool        untrusted = false;   // 项目配置未得到保证 — 无法连接
    bool        passthrough = false; // 型号："passthrough"—分派-only国外工具
    std::vector<ToolState> tools;

    [[nodiscard]] std::size_t enabled_count() const noexcept {
        std::size_t n = 0;
        for (const auto& t : tools) if (t.enabled) ++n;
        return n;
    }
};

// MCP服务器提示词，在编译器中作为`/name`提供。
struct PromptState {
    std::string name;          // 暴露的（可能命名空间的）名称
    std::string description;
    std::string hint;          // "<arg> [opt]" 来自其参数
};

// 单一UI-facing真相：每个配置的服务器、
// 其连接状态及其广告的工具，从配置+实时池统一。
// 可以在并发 池 交换中安全保存的值 快照。
struct PluginModel {
    std::vector<ServerState> servers;
    std::vector<PromptState> prompts;  // 每个连接的服务器的提示词
    std::size_t native_tool_count = 0; // agentty自己的工具（一直发货）
    // 活动提供者上的新 线程 如何使用 加载 工具（#98）。
    std::string loading;               // "auto" | "all" | "on_demand"
    std::size_t loaded_count      = 0; // 模式定义 上 传输
    std::size_t on_demand_count   = 0; // 已启用，发现有tool_search
    std::size_t schema_tokens     = 0; // 已加载 模式定义 的成本是多少
    std::size_t context_window    = 0; // 活动模型的，0=未知
    // 即使设置是"all"，硬提供者限制(OpenAI/Gemini:
    // 128个工具)也会按需放置工具。
    bool        capped_by_provider = false;
    // 由没有现场提供者的测试或无头呼叫者设置。
    [[nodiscard]] std::size_t enabled_mcp() const noexcept { return loaded_count + on_demand_count; }
};

} // namespace agentty::mcp
