#pragma once
// agentty::provider-ONE提供者-routing 接口边界。
//
// `main.cpp`用于手握-write一个if/else梯子(种类==
// ExternalAcp/OpenAI/is_chatgpt/native_api/else)
// 来挑选具体的传输层和数据流一转。该梯子是本地提供商的路由所在的 最后 位置，
// 与 注册表 不相关，并且不可能统一 -test。
//
// 现在这是单个路由点，它是provider-AGNOSTIC：它没有命名具体的
// 传输层类型。 long-lived 本机提供程序（Anthropic
// + ChatGPT — 它们保持连接/OAuth-token 状态，
// 因此它们比单独的回合寿命更长）作为类型 -erased `StreamFn`
// 在 main() 中绑定一次； Short-lived OpenAI-compat
// / Ollama 传输层 是根据 调用 从活动 `Endpoint` 构建的廉价值类型。
// 由于 接口边界 的类型为 -erased，因此测试可以注入虚假路由并断言给
// 定的选择到达了哪一个 — 没有网络，没有具体的提供商。
//
// 路由在 `provider::active()` 的 CALL
// TIME（不是冻结分支）上调度，因此实时提供者切换（选择面板 →
// select()）会重新定位下一个请求，而无需重建 接口边界。
// 可以显式地通过 `Selection`（测试）来确定性地路由特定选择。

#include "agentty/provider/provider.hpp"
#include "agentty/provider/stream_epilogue.hpp"
#include "agentty/provider/registry.hpp"   // RouteSlot：路由SSOT

#include <cstdint>
#include <functional>
#include <string>

namespace agentty::provider {

struct Selection;  // selection.hpp

// 类型-erased Provider 接口边界：正是`Provider`概念的
// `stream`，作为可调用的——包括其StreamResult返回，
// 因此结果流经擦除的边界而不是被默默地丢弃。 绑定 的具体提供者是 `[&
// p](const maya::unlocked& c, Request r, EventSink s){
// return p.stream(c, std::move(r), std::move(s)); }`.
using StreamFn = std::function<StreamResult(const maya::unlocked&, Request, EventSink)>;

// 路由键 IS 注册表 行的 RouteSlot 字段 — 没有第二个
// 需要同步维护的枚举。long_lived_slot 直接从 Selection 的
// 解析行； registry.hpp 中的一致性 static_asserts 保证每个
// LongLived 行都有一个槽，并且没有两行共享一个，因此 分派 可以盲目索引。
// 添加 long-lived 提供程序 = RouteSlot 中的一个枚举器 + 其行
// 上的一个 `.route =` + main() 中的一个 `router.set(...)`。
// 添加 per-call 提供程序需要 NONE — 它纯粹从 注册表 行流经
// 通用 OpenAI-compat 构建器。
using LongLived = RouteSlot;

// 将选择映射到其long-lived槽：已解析的注册表行的`.route`字段。
// Row-less 选择（自定义主机），ACP 臂是
// RouteSlot::None。
[[nodiscard]] LongLived long_lived_slot(const Selection& sel);

// 路由器：长-lived提供者被擦除StreamFns(由main()拥有，
// 因此捕获的参考比maya::run/ACP服务循环寿命更长)加上ACP臂。
// 分派 从不命名具体的提供程序类型 - 它通过描述符 -derived
// LongLived 插槽对其进行索引 - 这就是路由单元 -testable 的原因，
// 并使 注册表 不受 传输层 类型的影响。 Per-call 传输层 (
// OpenAI-compat / Ollama) 是在活动端点的 分派 内部构建的，
// 因此它们在这里不需要插槽。
struct ProviderRouter {
    StreamFn long_lived[kRouteSlots]{};   // 由 RouteSlot 索引； [无] 未使用

    // Kind::ExternalAcp —驱动外部ACP代理子进程。
    // 绑定在 main() 到 stream_external_acp(agent_id,
    // …)；此处删除，因此 分派 对 acp 编译单元 具有 ZERO 依赖性，
    // 并且每次测试 链接 都存在于提供程序 objlib 中。代理 ID 来自选择。
    std::function<StreamResult(const maya::unlocked&, const std::string&, Request, EventSink)>
        external_acp;

    // 符合人体工程学的设置器使得main()读取为数据，而不是array-index噪音。
    ProviderRouter& set(LongLived slot, StreamFn fn) {
        long_lived[static_cast<std::size_t>(slot)] = std::move(fn);
        return *this;
    }
};

// Back-compat别名：`Routes`是结构在成为描述符
// -indexed之前的名称。保留现有的 调用 sites/tests 继续编译。
using Routes = ProviderRouter;

// 路线ONE向右转向传输层至`sel`。内部构造有short-lived
// OpenAI-compat / Ollama / ACP 传输层；
// 两个long-lived来自`routes`。添加本地提供者会在此处添加一只手臂（
// 如果是 long-lived，则会在路由中添加一个字段）。从运行该回合的
// 传输层 中返回该回合的 StreamResult。
StreamResult dispatch_stream(const maya::unlocked& cap, const Routes& routes,
                             const Selection& sel, Request req, EventSink sink);

// 便利性过载：按req.selection（请求所针对的提供者）路由，
// 否则为实时active()。主要的stream_fn的整个正文。
StreamResult dispatch_stream(const maya::unlocked& cap, const Routes& routes, Request req,
                             EventSink sink);

} // namespace agentty::provider
