#pragma once
// 每个工具都遵循 "parse json -> typed Args -> run(Args)"
// 模式的统一管道。每个工具文件定义：
//
//   struct FooArgs { ... fields ... };
//   [[不丢弃]] std::expected<FooArgs, ToolError> parse_foo_args(const JSON&);
//   [[不丢弃]] ExecResult run_foo(const FooArgs&);
//
//   ToolDef tool_foo
//       () { ToolDef t; /* 名称、描述、模式定义、needs_permission */
//       t.execute = Adapt<FooArgs>(parse_foo_args、run_foo);
//       return t;
//   }
//
// `adapt`是原始`const json&`表面泄漏到工具分派中的
// 唯一地方——其下面的每个工具正文都看到带类型的结构。

#include "agentty/tool/registry.hpp"

#include <concepts>
#include <expected>
#include <functional>
#include <utility>

#include <nlohmann/json_fwd.hpp>  // Adapt<>仅转发const JSON&;实例化站点的完整类型

namespace agentty::tools::util {

// 工具的`parse`函数发出并且`run`消耗的任何结构。
// 作为概念表述，当工具作者连接错误的类型——e.g时，适配的签名在
// 调用站点处给出严重错误。
// 传递返回 `std::expected<Args, std::string>` 的 解析器。
template <class T>
concept ToolArgs = std::is_class_v<T> && std::movable<T>;

template <ToolArgs Args>
[[nodiscard]] auto adapt(
        std::expected<Args, ToolError> (*parse)(const nlohmann::json&),
        ExecResult (*run)(const Args&))
    -> ToolDef::ExecuteFn
{
    return [parse, run](const maya::unlocked&, const nlohmann::json& j,
                        const CallContext&) -> ExecResult {
        auto parsed = parse(j);
        if (!parsed) return std::unexpected(std::move(parsed.error()));
        return run(*parsed);
    };
}

// 阻止或夺取锁的工具正文获得工具工作线程的能力。
template <ToolArgs Args>
[[nodiscard]] auto adapt(
        std::expected<Args, ToolError> (*parse)(const nlohmann::json&),
        ExecResult (*run)(const maya::unlocked&, const Args&))
    -> ToolDef::ExecuteFn
{
    return [parse, run](const maya::unlocked& cap, const nlohmann::json& j,
                        const CallContext&) -> ExecResult {
        auto parsed = parse(j);
        if (!parsed) return std::unexpected(std::move(parsed.error()));
        return run(cap, *parsed);
    };
}

} // namespace agentty::tools::util
