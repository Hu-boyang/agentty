#pragma once
// agentty::tools::plan——哪些工具处理请求，以及模型如何找到
// 其余的（#98）。
//
// 每个启用的工具的三种状态：
//   已加载其模式定义每回合都在传输上。
//   按需不在传输上；该模型通过 tool_search 找到它，从那时起，
//              对于 线程 的其余部分，它是 已加载。
//   用户禁用它（永远不会到达该层）。
//
// 每个启用的工具无论其状态如何都保持CALLABLE：
// 分派解析整个目录。该层仅决定广告的内容。
//
// 纯粹：没有IO，没有全局变量。呼叫者（轮次启动、子代理、
// ACP）通过 目录、线程 的 已加载 设置和限制。

#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "agentty/domain/conversation.hpp"
#include "agentty/tool/registry.hpp"

namespace agentty::tools::plan {

// 用户设置(设置→工具→MCP工具加载)。
enum class Loading : std::uint8_t {
    Auto,      // 加载 一切合适，其余按需
    All,       // 加载 一切都达到提供商的硬限制
    OnDemand,  // 加载本机、引脚以及线程使用了什么；搜索其余的
};

[[nodiscard]] std::string_view to_string(Loading l) noexcept;
[[nodiscard]] Loading loading_from(std::string_view s) noexcept;   // 未知 → 自动

// 模型如何到达此 传输 上的 on-demand 工具。
enum class Discovery : std::uint8_t {
    // Anthropic:每个工具在`tools`中，在-demand中，
    // 在defer_loading中；我们的 tool_search 用 tool_reference
    // 块和 API 加载 模式定义 本身来回答。 提示词缓存 未受影响。
    DeferredReferences,
    // 其他人：on-demand工具被遗漏； tool_search 以
    // 文本形式返回 模式定义，并且他们从下一个请求开始使用 传输。
    ClientSide,
};

// 传输可以按照提供商+型号采取什么。
struct Limits {
    // 一次请求中的工具的硬上限。 OpenAI 聊天且 Gemini 拒绝 >128；
    // 0 = 没有已知的上限。对于 DeferredReferences，这仅限制 NON
    // -deferred 工具（Anthropic 最多需要 10,000 个延迟）。
    std::size_t max_tools = 0;
    // 模型的上下文窗口以标记为单位（0=未知→视为128k)。
    std::size_t context_window = 0;
    // 窗口PLUGIN工具模式定义的共享可以在自动推迟其余部分之前进行。
    // 当地人并不反对它。 Claude 代码使用 10%。
    double schema_share = 0.10;
    Discovery discovery = Discovery::ClientSide;
    // 对于不能可靠地搜索工具的模型（小型本地模型）来说是错误
    // 的：适合的所有东西都是已加载并且没有任何东西是按需的。
    bool search_capable = true;
};

// 工具a 线程有已加载，最早使用的在前。当硬上限无法容纳
// 所有内容时，将保留最近使用的内容。
struct Loaded {
    std::vector<std::string> order;            // 最后使用，最早的在前
    std::unordered_set<std::string> names;

    Loaded() = default;
    Loaded(std::initializer_list<std::string> init) { for (const auto& n : init) touch(n); }
    Loaded(const std::unordered_set<std::string>& s) { for (const auto& n : s) touch(n); }

    void touch(std::string_view name);         // 现在使用的标记
    [[nodiscard]] bool contains(std::string_view n) const { return names.contains(std::string{n}); }
    [[nodiscard]] std::size_t size() const noexcept { return order.size(); }
};

// 一个工具在此请求中占有一席之地。
struct Placement {
    const ToolDef* tool = nullptr;
    bool deferred = false;   // 仅DeferredReferences：在传输、defer_loading上
};

struct Plan {
    std::vector<Placement> wire;   // 按 `tools`、目录 顺序排列的内容
    std::size_t loaded = 0;        // 模型现在看到的工具是模式定义
    std::size_t on_demand = 0;     // 已启用，只能通过tool_search到达
    std::size_t schema_tokens = 0; // 估计代币为已加载 模式定义
    bool search = false;           // tool_search在传输上
    // 当HARD限制（不是上下文预算）强制在Loading::All下
    // 按需使用工具时设置； UI 是这么说的，而不是隐藏它。
    bool capped_by_provider = false;
};

// 一种工具的模式定义在传输上的粗略令牌成本（名称+描述+JSON 模式定义、
// bytes/3.5，如agentty的估计器的其余部分）。
[[nodiscard]] std::size_t schema_tokens(const ToolDef& t);

// 确定请求的工具。
//
//   目录每个启用的工具，以目录顺序（本地人优先）已加载已
//   命名该线程 已加载（tool_search命中，
//   调用）搜索tool_search ToolDef，当需要任何内容时添加
//
// 规则，按顺序：
//   1.本机和固定(always_expose)工具始终是已加载。
//   2. 线程-loaded 工具紧随其后，最新的在前，超过 模式定义 预算
//      但在max_tools内；当年龄最大的人不适合时，他们会根据要求返回。
//   3.汽车：在模式定义预算和max_tools允许的情况下剩余加载。
//      全部：在max_tools允许的情况下剩余加载。
//      OnDemand:剩下的按需走。
//   4.剩余的全部按需，并且tool_search加入传输。
// max_tools之后的任何东西都不会消失，因此任何数量的工具都可以工作。
// 传输上的顺序是目录顺序，从来不计分顺序，因此工具块(提示词
// -cache前缀的头部)是字节-stable，而集合是。
[[nodiscard]] Plan make_plan(const std::vector<ToolDef>& catalog,
                             const Loaded& loaded,
                             const ToolDef* search, Loading mode,
                             const Limits& limits);

// ── 搜索 ───────────────────────────────────────────────────────────────

struct Hit {
    const ToolDef* tool = nullptr;
    double score = 0;
};

// 用于查询的排名工具。 BM25 覆盖名称、服务器和描述，标识符拆分（snake_case、
// camelCase、mcp__srv__tool）和可拼写错误的 回退，
// 因此 "decompile functon" 仍然可以找到 decompile_function。
// `select:a,b` 加载那些确切的名字(什么Claude
// 代码调用直接选择)。本地人被排除在外：他们始终是 已加载。
[[nodiscard]] std::vector<Hit> search(const std::vector<ToolDef>& catalog,
                                      std::string_view query, std::size_t k);

// ── 线程 状态 ─────────────────────────────────────────────────────────

// Tool将这个线程命名为已加载：每个工具都返回CALLED，
// 并且每个工具返回一个tool_search。源自转录本，因此它可以在
// 重新启动、分叉和恢复后继续存在，而无需新的持久状态。
[[nodiscard]] Loaded loaded_in(const Thread& thread);
[[nodiscard]] Loaded loaded_in(const std::vector<Message>& messages);

// 名称和标记tool_search写入其结果中，因此loaded_in可以
// 读回命中。每次点击一行："<marker><name>"。
inline constexpr std::string_view kSearchToolName = "tool_search";
inline constexpr std::string_view kLoadedMarker = "loaded: ";

} // namespace agentty::tools::plan
