#pragma once
// agentty::tools::util::handoff — 对写入执行 trust-handoff 策略。
//
// `sandbox_cfg::HandoffPolicy`缺失的一半。
// 自从出处模块登陆以来，面板 就提供了该行，配置将其保留，
// 但没有任何内容读取它：`is_host_trusted()`
// 在其自己的测试之外有零调用者。 保存 且不执行任何操作的安全控制正是
// 该子系统存在的要防止的故障，上一层 - 交换机表示 `refuse`
// 并且写入已完成。
//
// ── 为什么这不在里面? 沙箱 ──────────────────────────────────
//
// 因为沙箱看不到它。下面的每一堵墙（landlock、
// seccomp、cgroup）都限制了 PROCESS，
// 信任切换并不是一个执行禁止操作的进程：它是代理编写一个完全
// 普通的文件，在 工作区 内允许写入，稍后由 沙箱 组件
// OUTSIDE 执行。挂载命名空间的高度错误。当 VS 代码运行
// .vscode/tasks.json 时，根本不涉及 沙箱。
//
// 因此，门位于权限实际所在的位置：在工具 调用 上，在 字节 土地之前。
// 这也是为什么它不能成为 seccomp 规则的原因——请参阅
// sandbox_broker.hpp，了解为什么系统调用参数中的 path-based
// 决定是 TOCTOU 错误而不是策略。
//
// ── 为什么要检查 TOOL 而不是说 沙箱 写入日志 ──────────────
//
// `Provenance`记录沙盒进程所写的内容，
// 对于审计跟踪来说这是正确的数据，对于门控来说是错误的数据：这是事后的。
// 代理的文件-authoring工具（write/edit/apply_patch/move)
// 位于-process中并且根本不接触沙箱，因此沙箱的日志
// 甚至不会看到Cursor-hooks情况。检查即将运行的 调用
// 的参数会捕获两者，并在写入之前而不是之后捕获它们。
//
// ── 政策,为什么拒绝是默认 ──────────────────────────
//
//   拒绝 调用 失败并出现模型可以读取和路由的指定错误（
//           先编写脚本，再请用户自行接入执行入口。
//   警告调用收益；切换被记录下来，以便 UI 可以这样说。
//   不允许有门。对于其工作流程真正需要代理来编写
//           钩子的用户，显式选择而不是继承。
//
// 拒绝是默认的，因为这不是"might be risky" 提示词。
// Pillar 的 2026 年 7 月系列中的每一次逃逸都
// 通过这种形状进行，并且没有一个需要打破 沙箱，
// 因此误报的成本是一次逃逸，而误报的成本是一个被拒绝的工具 调用，
// 并有一条消息确切说明了该怎么做。

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "agentty/domain/sandbox_provenance.hpp"

namespace agentty::tools::util::handoff {

// 门决定一写。
struct Verdict {
    bool allowed = true;

    // 每当路径IS设置信任切换时，无论其是否被
    // 允许——警告决策仍然是值得浮现的事件。
    // 普通路径时为空，这是绝大多数常见的情况。
    bool is_handoff = false;
    sandbox_cfg::TrustKind kind{};

    // 为什么，用模型和用户都读的话来说。
    // 允许时为空且普通。
    std::string reason;
};

// 决定一条路径。
//
// 除了读取密封的沙箱配置之外：没有文件系统访问，
// 因此尚不存在的路径（写入的通常情况）决定与存在的路径相同。
// `tool` 仅为消息的呼叫者命名。
[[nodiscard]] Verdict check(std::string_view path, std::string_view tool);

// 决定使用显式策略而不是实时配置。可测试的 接口边界，
// 以及奇偶校验测试驱动的那个。
[[nodiscard]] Verdict check_with(std::string_view path, std::string_view tool,
                                 sandbox_cfg::HandoffPolicy policy);

// 每次切换门都看到这个会话，最新的最后的，有界的。
//
// 与代理的被阻止的提要具有相同的形状和相同的推理：从工具工作线程
// 线程写入，从状态更新函数 线程读取，因此其在内部是
// `maya::guarded`并按值交还。
[[nodiscard]] std::vector<sandbox_cfg::TrustHandoff> handoff_feed();

// 忘记记录的一切。用于测试。
void clear_handoff_feed();

// ── 壳洞,为什么这半个 ─────────────────────────
//
// check()守卫着write/edit/apply_patch/move，
// 每人交出一个PATH我们可以检查。 `shell` 交出了一根不透明的绳子，
// 所以上面的门从来没有跑过它——而 `cat > .vscode/tasks.json`
// 直接走过了一个正确知道这条路很危险的政策。实测：`write` 到该路径被拒绝，
// 同样的 字节 通过 shell 则没有。
//
// 考虑了三种机制。
// 注释为docs/design/shell-write-gate.md；简短版本：
//
//   PARSE 该命令——作为机制被拒绝。捕获 `>` 意味着还捕获 `tee`、
//     `dd of=`、`sh -c`、`python -c`、heredocs、
//     变量中的路径 base64-then-decode。
//     mcp-cpp的shell_detour.hpp已经带着伤痕：它的第一个版本将`sed -i`
//     和`cat > f`匹配为READS。嗅探器是深度防御，而不是围墙防御。
//
//   LANDLOCK 是 -only 雕刻——真实的、经过测量的、
//     并且无法在这里发货。在一个规则集内，权利 INHERIT 和更深入、
//     更严格的规则不会相减（因此，明显的划分被忽略）；它们跨层INTERSECT，
//     这确实有效——但规则仅针对DIRECTORIES，
//     并在雕刻re-opens的途中授予祖先。因此，雕刻 .
//     vscode 会使 工作区 根目录中的松散文件读取为 -only。
//     比洞更糟糕的回归。
//
//   BIND在每个可信路径上安装了read-only——右墙，
//     以及agentty已经用来掩盖秘密的路径，
//     正是出于这个原因：否定规则没有内锁拼写，所以你使路径不是文件。
//     准确，便宜，语法-proof。需要挂载命名空间，
//     因此拒绝非特权用户的主机上不存在该命名空间。
//
// 因此，这个：DETECTION，它存在于任何地方，
// 包括那些主机。它不能阻止写入，也不能通过任何引用技巧来逃避，
// 因为它观察文件系统而不是命令。 快照与trusted
// -shape路径存在，运行调用，进行比较。任何更改都会成为与
// UI 已经 渲染 相同的源上的 事件 的切换。
//
// 预防是挂载命名空间存在的更有力保障；
// 检测是永远正确的。

// `root`下可信路径的不透明before-picture。
//
// 构造便宜：它仅统计与受信任的SHAPE匹配的路径，从不走过源码树。
// 空的 快照（不存在可信路径，或者门已关闭）使匹配的
// review() 成为 no-op。
struct TrustedSnapshot {
    struct Entry {
        std::string            path;
        sandbox_cfg::TrustKind kind{};
        std::uintmax_t         size = 0;
        std::int64_t           mtime_ns = 0;
        bool                   existed = false;
    };
    std::vector<Entry> entries;

    // 被遍历并且可能产生NEW可信文件的目录。
    // review() re-scans这些，
    // 因为`entries`仅覆盖已经存在的路径——并且新创建的钩子是更危险的情况，
    // 而不是较小的情况。
    std::vector<std::string> roots;

    [[nodiscard]] bool empty() const noexcept {
        return entries.empty() && roots.empty();
    }
};

// 快照在外壳-ish 调用之前。当策略为“允许”或 沙箱 关闭
// 时返回 {} - 没有任何可报告的内容，因此没有理由进行统计。
[[nodiscard]] TrustedSnapshot snapshot_trusted(std::string_view root);

// 最近的快照是否达到了其入场上限？由 沙箱 面板 读取。
[[nodiscard]] bool last_snapshot_truncated() noexcept;

// 在调用之后进行比较并记录每条改变、
// 出现或消失的路径的切换。返回记录的数量。
//
// NOT故意使调用不合格。当运行时，字节 已经在磁盘上，
// 因此拒绝将是戏剧性的；诚实的结果是响亮的、可归因的 事件。
// `tool` 指明消息呼叫者的名称。
std::size_t review_trusted(const TrustedSnapshot& before, std::string_view tool);

}  // namespace agentty::tools::util::handoff
