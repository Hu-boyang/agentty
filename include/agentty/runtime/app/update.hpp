#pragma once
// agentty::app::update — 状态更新函数，如 jaal 调用。
//
// jaal 由 OVERLOAD 调度，而不是通过我们编写的访问：它遍历 Msg 源码树
// 本身和 调用 `update(Model&, T)` 来查找它落在的任何内容 (D36)。
// 使用 handled_as_group (runtime/cmd.hpp) 声明
// 的域是这些登陆之一，因此每个域 (24) 存在一次过载。
//
// 形状是jaal的：就地改变模型，仅返回副作用。
//
//     Cmd更新(Model&米，msg::ComposerMsg厘米)；
//
// 旧形状返回`std::pair<Model, Cmd>`并将模型移动通过每个状态更新函数。
// 这花费了 172 个 `return {std::move(m), ...}` 站点，
// 更糟糕的是，允许 `return {std::move(m), f(m)}`
// — 在其移动旁边的参数中读取 m，未指定的评估顺序。
// update/rag.cpp. 中有一个此签名删除了整个危险。
//
// 所有 副作用 都作为命令返回，从不内联执行。

#include <utility>

#include <maya/maya.hpp>

#include "agentty/runtime/model.hpp"
#include "agentty/runtime/cmd.hpp"
#include "agentty/runtime/msg.hpp"

namespace agentty::app {

// 每个域一个，按照它们在 Msg 中出现的顺序。每个都在自己的 TU 中
// 转发到域的 状态更新函数，这使得 one-leaf 编辑的重建时间约为 1.2 秒，
// 而不是约 19 秒（jaal D36，在此应用程序上测量）。
Cmd update(Model& m, msg::ComposerMsg     cm);
Cmd update(Model& m, msg::StreamMsg       sm);
Cmd update(Model& m, msg::ToolMsg         tm);
Cmd update(Model& m, msg::ToolOutputMsg   tm);
Cmd update(Model& m, msg::ProvidersMsg    pm);
Cmd update(Model& m, msg::ModelsMsg       mm);
Cmd update(Model& m, msg::ThreadListMsg   tm);
Cmd update(Model& m, msg::PaletteMsg      pm);
Cmd update(Model& m, msg::MentionMsg      mm);
Cmd update(Model& m, msg::SymbolMsg       sm);
Cmd update(Model& m, msg::CodeBlockMsg    cm);
Cmd update(Model& m, msg::CheckpointMsg   cm);
Cmd update(Model& m, msg::RagMsg          rm);
Cmd update(Model& m, msg::StatsMsg        sm);
Cmd update(Model& m, msg::SettingsListMsg sm);
Cmd update(Model& m, msg::ForkMsg         fm);
Cmd update(Model& m, msg::TodoMsg         tm);
Cmd update(Model& m, msg::LoginMsg        lm);
Cmd update(Model& m, msg::DiffReviewMsg   dm);
Cmd update(Model& m, msg::SmartModeMsg    sm);
Cmd update(Model& m, msg::WebSearchMsg    wm);
Cmd update(Model& m, msg::PasteSettingsMsg pm);
Cmd update(Model& m, msg::PluginEditMsg   pm);
Cmd update(Model& m, msg::AppearanceMsg   am);
Cmd update(Model& m, msg::SandboxMsg      sm);
Cmd update(Model& m, msg::MetaMsg         mm);

// 旧的入口点仍然在这里，而域 状态更新函数 保留其 `Step`
// -returning 正文。上面的重载是它上面的薄包装；
// 一旦每个 状态更新函数 都占用了 Model，这种情况就会消失。
// 整个-Msg入口点，用于测试。 归并 位于 `now`，同样的方式
// jaal 归并 位于步骤时间。两个 -argument 在 kTestEpoch
// 形成 归并，是 FIXED 瞬间而不是挂钟，因此从未提及时间的测试仍然是可重现的。
// 参见 update.cpp。
inline constexpr std::chrono::steady_clock::time_point kTestEpoch{
    std::chrono::hours{24}};   // 非零，因此 "never set" (time_point{}) 保持可区分

// 在每个归并之后，在分派 接口边界处运行：导出从Model读取的状态工作线程 线程，
// 并且当它自上次发布以来移动时，将发布它的副作用附加到`c`。
// 状态更新函数 从未发布 - 请参阅 Model::published_subagent。
// 纯：读取 Model，更新其已发布内容的记录，
// 返回 Cmd。在 update.cpp 中定义。
[[nodiscard]] Cmd publish_derived(Model& m, Cmd c);
[[nodiscard]] std::pair<Model, Cmd> update(Model m, Msg msg);
[[nodiscard]] std::pair<Model, Cmd> update(Model m, Msg msg,
                                           std::chrono::steady_clock::time_point now);

} // namespace agentty::app
