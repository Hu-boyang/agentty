// agentty::mcp::serve_stdio-暴露agentty的本机工具OVER MCP。
//
// bridge.cpp的逆。我们构建一个 mcp::Server（一张我们回答的表），
// 将 agentty 设置允许的每个工具（tools::withheld_by_policy）
// 注册为 tools::native_registry() 作为 MCP 工具，
// 并通过 rpc::Peer 上的 stdio 提供服务。
// 注册工具的 处理函数 round-trips 到 tool::DynamicDispatch::execute
// (name, args) — 代理循环使用的 相同 分派 — 因此输出预算、
// empty-args 防护和崩溃隔离都同样适用。结果（ExecResult =预期
// <ToolOutput, ToolError>）被投影到mcp::CallToolResult上。
//
// 副作用→注释与bridge.cpp的effects_for()相反：宣传既不写
// 入文件系统也不执行的工具readOnlyHint:true（且非破坏性）；
// writes/execs 是 destructiveHint:true 的任何内容。
// 这些是客户自己的UX-agentty的权限政策的HINTS仍然完全存在于消费方。
// 模式定义 是该工具自己的 JSON 模式定义 直接解码为
// mcp::JsonSchema。
//
// 重mcp-cpp模板保持局限于该TU(它是agentty_mcp对象库的一部分)；
// agentty-facing 头文件 是 mcp-cpp-free。

#include "agentty/mcp/serve.hpp"
#include "agentty/version.hpp"

#include "agentty/tool/tool.hpp"        // DynamicDispatch
#include "agentty/tool/registry.hpp"    // registry()，ToolDef
#include "agentty/tool/effects.hpp"
#include "agentty/domain/profile.hpp"

#include <mcp/mcp.hpp>

#include "agentty/rpc/peer.hpp"

#include <cstdio>
#include <iostream>
#include <string>

#if defined(_WIN32)
#  include <fcntl.h>
#  include <io.h>
#endif


namespace agentty::mcp {

namespace {

using ::mcp::Json;

// bridge.cpp的effects_for()的逆向：通过MCP注释向远程
// 客户端描述本机工具。 read-only 是一个不写入文件系统且不执行的工具（它
// 可能仍然是 ReadFs / Net — 那些观察，而不是变异）。
// 任何带有 WriteFs 或 Exec 的内容都被标记为具有破坏性，因此谨慎的客户可以对
// 其进行控制。 idempotentHint/openWorldHint 未设置（未知）。
::mcp::ToolAnnotations annotations_for(tools::EffectSet fx) {
    using tools::Effect;
    ::mcp::ToolAnnotations a;
    const bool mutates = fx.has(Effect::WriteFs) || fx.has(Effect::Exec);
    a.readOnlyHint    = !mutates;
    a.destructiveHint = mutates;
    // 接触网络的工具超越了本地封闭世界。
    a.openWorldHint   = fx.has(Effect::Net);
    return a;
}

// 将 agentty 的 ExecResult 投影到 MCP CallToolResult 上。
// 成功 → 该工具的文本作为单个内容块；如果该工具生成了 FileChange
// (write/edit)，则将其摘要显示为 structuredContent，
// 以便需要差异元数据的远程调用者可以读取它，而不会污染模型看到的文本。
// 失败 → isError:true，呈现的 ToolError 作为文本（工具
// -level 的规范约定，非 -protocol 错误）。
::mcp::CallToolResult to_call_result(tools::ExecResult&& r) {
    ::mcp::CallToolResult out;
    if (!r) {
        out.isError = true;
        out.content = {::mcp::text(r.error().render())};
        return out;
    }
    out.content = {::mcp::text(r->text.empty() ? std::string{"(no output)"}
                                               : std::move(r->text))};
    if (r->change.has_value()) {
        const auto& c = *r->change;
        out.structuredContent = Json{
            {"path",    c.path},
            {"added",   c.added},
            {"removed", c.removed},
        };
    }
    return out;
}

// 为一个ToolDef构建mcp::Tool规范（模式定义+注释+描述）。
::mcp::Tool spec_for(const tools::ToolDef& def) {
    ::mcp::Tool t;
    t.name        = def.name.value;
    t.description = def.description;
    // 该工具的JSON 模式定义直接解码成mcp::JsonSchema
    // (类型始终是"object"；属性+需要逐字传递)。
    if (def.input_schema.is_object())
        t.inputSchema = ::mcp::from_json<::mcp::JsonSchema>(def.input_schema);
    t.annotations = annotations_for(def.effects);
    return t;
}

} // 命名空间结束。

int serve_stdio(const maya::unlocked& cap) {
    ::mcp::Server server(
        ::mcp::Implementation{"agentty", ::agentty::version(),
                              std::string("agentty native tools"),
                              ::mcp::Nothing, ::mcp::Nothing, ::mcp::Nothing});
    server.set_capabilities(::mcp::ServerCapabilities{
        .tools = ::mcp::ToolsCapability{},
    });
    server.set_instructions(
        "agentty's native coding tools served over MCP: file read/edit/write, "
        "shell (bash), code search (grep/glob/find_definition), web fetch/search, "
        "diagnostics, and git. Filesystem tools are sandboxed to the workspace "
        "the agentty process was launched in.");

    // MCP 2026-07-28 无状态核心：通告`server/discover`可缓存性。
    // 我们的工具目录对于进程生命周期是静态的（从tools::registry()
    // 构建一次），因此现代客户端可以缓存发现结果一个小时并在重新连接时跳过re
    // -enumerating。发现 处理函数 和 per-request
    // `_meta` 读数随 mcp::Server 一起提供。
    server.set_discover_cache(::mcp::CacheHint{3'600'000, "public"});

    // 注册每个本机工具agentty自己的设置允许的。
    // agentty 是父级：为模型关闭的工具（设置 → Web
    // 搜索）也不会提供给另一个 MCP 客户端 — 未列出，
    // 因此无法通过此服务器调用。策略在此处读取一次（应用环境覆盖），
    // 与发现 缓存 通告的静态 目录 相匹配； 分派 闭合
    // 件在关闭时仍会拒绝 web_search，作为后挡板。
    // 处理函数通过代理循环使用的完全相同的DynamicDispatch
    // 进行路由，因此行为是相同的。
    //
    // 锁命令：调用在对端的读取器线程上得到应答，该读取器不持有任何东西并将
    // 其`unlocked`交给on_call (rpc/peer.hpp)。
    // 然后，mcp-cpp 的服务器在 server.handle
    // (c) 内同步调用工具闭包，并使用它修复的签名（无功能）。因此，
    // on_call 在 handle() 的持续时间内将指向其
    // 功能的指针放置在 帧-local 槽中，并且闭包将其读回：该
    // 功能永远不会比证明它的 调用 更长久，并且永远不会离开 线程。
    struct CallScope { const maya::unlocked* cap = nullptr; };
    auto scope = std::make_shared<CallScope>();
    std::size_t n = 0, withheld = 0;
    for (const auto& def : tools::native_registry()) {
        if (tools::withheld_by_policy(def)) { ++withheld; continue; }
        const std::string name = def.name.value;
        server.register_tool(spec_for(def),
            [name, scope](const Json& args) -> ::mcp::CallToolResult {
                if (!scope->cap)
                    return to_call_result(std::unexpected(tools::ToolError::unknown(
                        "tool called outside a request")));
                return to_call_result(
                    tool::DynamicDispatch::execute(*scope->cap, name, args));
            });
        ++n;
    }

    if (withheld)
        std::fprintf(stderr,
            "agentty: MCP server ready on stdio (%zu tools; %zu withheld by "
            "settings)\n", n, withheld);
    else
        std::fprintf(stderr, "agentty: MCP server ready on stdio (%zu tools)\n", n);

    // 调用在阅读器上按到达顺序一次回答一个。
    rpc::Dispatch d;
    d.on_call = [&server, scope](const maya::unlocked& c, const ::mcp::Call& call) {
        scope->cap = std::addressof(c);
        struct Clear { CallScope& s; ~Clear() { s.cap = nullptr; } } clear{*scope};
        return std::optional<std::string>(server.handle(call));
    };
    d.on_notification = [&server](const maya::unlocked&, const ::mcp::Notification& note) {
        server.handle(note);
    };
    std::cout.flush();
#if defined(_WIN32)
    // 文本模式会将帧的每个'\n'变成"\r\n"。
    _setmode(_fileno(stdin),  _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    rpc::Peer peer(rpc::fd_channel(0, 1), std::move(d), "mcp-serve");
    peer.start();
    peer.wait_closed(cap);   // 运行直到标准输入关闭
    peer.stop(cap);
    return 0;
}

} // namespace agentty::mcp
