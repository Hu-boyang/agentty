#pragma once
// agentty/util/sendable.hpp-类型jaal看不到内部，标记为可发送。
//
// maya::guarded<T>和任务参数必须是可发送的：可以安全地
// 移动到另一个线程。 jaal 通过遍历普通结构体的字段来证明这一点；一个
// 具有私有成员或构造函数的类对该机制不可见，因此会被拒绝。
// 除非有人手动检查并选择将其放在这里。下面的每个 opt-in
// 都是该检查。
//
// 只要受保护的值或任务参数保存这些类型之一，就包括这个(
// 不是jaal)。

#include <filesystem>

#include <maya/runtime.hpp>

#include <nlohmann/json.hpp>

#include "agentty/domain/id.hpp"
#include "agentty/tool/effects.hpp"

// nlohmann::json按值拥有其整个源码树（字符串、
// 数组、对象、数字、布尔值、空值上的变体——每个分支一个拥有容器）。
// jaal 无法行走，因为 载荷 位于私人工会后面，
// 而不是因为那里有借来的东西。 Tool参数和结果是JSON，
// 因此它们不断地交叉到工作线程 线程。
//
// JSON拥有源码树值：没有视图、没有共享指针、没有可变成员，
// 因此从const JSON&可到达的任何内容都不能改变。
MAYA_SENDABLE(nlohmann::json);
MAYA_FROZEN(nlohmann::json);

// Id<Tag>是围绕ONE std::string的强新类型，从值来看，
// 没有视图也没有指针。 jaal 无法查看内部，只是因为它有 user
// -declared 构造函数。所以它是可发送的，也是可冻结的：通过 const
// Id 无法更改任何可到达的内容。
template <class Tag>
MAYA_SENDABLE_T(agentty::Id<Tag>);
template <class Tag>
MAYA_FROZEN_T(agentty::Id<Tag>);

// EffectSet是访问器后面的标志中的std::uint8_t之一。
MAYA_SENDABLE(agentty::tools::EffectSet);
MAYA_FROZEN(agentty::tools::EffectSet);
