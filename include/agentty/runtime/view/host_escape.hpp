#pragma once
// agentty::ui::host-合作-host(编辑器)集成逃逸。
//
// 当 agentty 的 TUI 在编辑器的 PTY (Emacs/vterm，
// 以及任何监视我们私人 OSC 的 终端) 上运行时，它可以推送
// "the agent just touched this file" 提示，
// 以便编辑器本机地遵循 — 打开文件、移动点、显示差异 — 而不是用户寻找更改的内容。
//
// 该通道是单个私有OSC，通过maya的帧-safe cmd::emit_osc
// (out-of-band与渲染、cursor-neutral)发出：
//
//     OSC 5379; agentty； <json> ST
//
// 其中 <json> = {"event":"<kind>"，...
// 字段}。 5379是私有代码，无主流终端声称； "agentty;"
// 标签让主机明确匹配我们的主机并忽略其他所有内容。不关注它的主机会看到
// 一个惰性的 OSC 并丢弃它——零成本，零损坏。
//
// 该层是PURE：它仅构建字符串+报告协作主机是否存在。
// 状态更新函数 决定何时发出并将结果包装在 maya Cmd 中。
// 门控所以正常的 终端 会话不需要支付任何费用。

#include <optional>
#include <string>
#include <string_view>

namespace agentty::ui::host {

// agentty是否在想要集成转义的协作编辑器主机下运行？当 AGENTTY_HOST
// 命名已知主机（当前为 "emacs"）时为 True，或者当使用 vterm
// 标记设置 $INSIDE_EMACS 时为 回退。读取环境，
// 因此由 read_launch_env 调用一次，
// 答案位于 Model::env.host_integration 中； 状态更新函数 门就在那里。
[[nodiscard]] bool detect_integration();

// 私有OSC告诉主机文件工具刚刚在可选的1-based `line`处作用
// 于`path` (绝对的，正向-slash)。 `kind` 是工具名称（
// "read" | "edit" | "write" | "move" | …），
// 因此主持人可以选择手势（打开、比较、逐步显示）。 Pure：调用者决定
// 是否开启集成。当 `path` 为空时返回 nullopt。
[[nodiscard]] std::optional<std::string>
file_event_osc(std::string_view kind, std::string_view path,
               std::optional<int> line = std::nullopt);

} // namespace agentty::ui::host
