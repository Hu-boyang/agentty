#pragma once
// agentty::sandbox_cfg::provenance — AGENT 写入了哪些文件，
// 以及主机信任哪些文件。
//
// ── 威胁是... ───────────────────────────────────────────
//
// Pillar Security 的 "Week of Sandbox Escapes" (
// 2026 年 7 月)再现了跨 Cursor、Codex、Gemini CLI 和
// Antigravity 的逃逸。几乎没有一个特工破坏了 沙箱。它写入了一个受信任组件
// OUTSIDE 和 沙箱 后来运行的文件：
//
//   * .claude 钩子配置 -> CVE-2026-48124，光标，CVSS 8.5
//   * virtualenv解释器->GHSA-p9g2-cr55-cw9c，由
//                                           发现期间的Python扩展
//   * 另一个名称下的.git目录->GHSA(光标3.0.0),fsmonitor
//   * 一个.vscode任务配置->Antigravity，稍后由主机运行
//   * `git show` 调用 -> GitPwned、Codex CLI 0.95.0
//
// 他们的结论，我认为是完全正确的：
// "an agent's blast radius is not the agent process; it includes everything the agent can write that the host later trusts."
// 限制进程并让它写入可执行配置的沙箱
// 已经移动了边界，而不是构建了边界。
//
// ── 为什么 agentty 已经好一些了, 差距仍然在什么地方 ──────
//
// agentty不存在光标错误。挂钩运行 THROUGH 和 沙箱（与 bash
// 工具相同的 bwrap/claybin 包装器）并且内容-hash 门控，
// 因此任何 字节 更改都会重新-prompts。插件使用相同的批准原语。
//
// 但是哈希门无法回答 Pillar 所说的最重要的问题：
//
//     产品能否区分用户创建的项目状态与代理
//      创建的项目状态？
//
// 今天不能。 USER 写入的 hooks.json 和 AGENT 写入的
// hooks.json 产生相同的 提示词，因此 提示词 不携带任何信号，而看起来总是
// 相同的 提示词 是人们点击的 提示词。当用户需要听到的是
// "the agent just wrote the file that runs commands on your machine" 时，
// 批准会显示 "this file changed"。
//
// ── 模块的功能 ───────────────────────────────────────────────
//
// claybin的沙箱是唯一能够廉价地回答它的地方，
// 因为代理的写入全部通过我们构建的一个挂载命名空间。所以：
//
//   1. 沙箱每轮写入的每条路径都用命令记录
//      写的。这就是出处记录。
//   2、HOST后来信任的路径(hooks.json，.vscode/tasks.json，
//      .git/config、venv解释器、.envrc…)是已知的集合。
//   3、交集为告警：某主机有一个agent-authored文件
//      组件将被执行。这会从 "approve this hash"
//      升级为命名警告，并且默认情况下会拒绝。
//
// 这是层Pillar构建的产品来销售。它属于代理，
// 因为代理是唯一知道哪些写入是它自己的。

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace agentty::sandbox_cfg {

// 沙箱所写的文件，以及谁询问的。
struct WriteRecord {
    std::string path;     // 绝对的，如沙箱里面解析的
    std::string command;  // 产生它的shell命令
    std::uint64_t at_ms = 0;
    bool created = false;  // true=该文件之前不存在
};

// 为什么路径对于代理创作来说是危险的。命名机制很重要："the agent wrote .vscode/tasks.json"
// 对大多数人来说毫无意义，
// 代理写入了一个文件，打开目录时 VS Code 将执行它。
// 意味着一切。
enum class TrustKind : std::uint8_t {
    HookConfig,     // agentty hooks.json、.claude/*、生命周期自动化
    EditorTask,     // .vscode/tasks.json、launch.json——主机运行这些
    GitConfig,      // .git/config、hooks/、fsmonitor——git 运行这些
    Interpreter,    // venv/bin/python、node_modules/.bin——发现运行这些
    ShellInit,      // .envrc、.bashrc、激活脚本
    BuildScript,    // Makefile、package.json 脚本、build.rs
    DaemonSocket,   // docker.sock和朋友——第二个执行环境
    SessionStart,   // 自动启动、systemd 用户单元、WM 配置——登录时运行
};

[[nodiscard]] constexpr std::string_view explain(TrustKind k) noexcept {
    switch (k) {
        case TrustKind::HookConfig:
            return "lifecycle hooks -- agentty runs these around every tool call";
        case TrustKind::EditorTask:
            return "an editor task -- VS Code can run this when the folder opens";
        case TrustKind::GitConfig:
            return "git configuration -- git runs hooks and helpers from here";
        case TrustKind::Interpreter:
            return "an interpreter or entry point -- tool discovery executes these";
        case TrustKind::ShellInit:
            return "shell init -- runs on your next shell in this directory";
        case TrustKind::BuildScript:
            return "a build script -- runs on the next build";
        case TrustKind::DaemonSocket:
            return "a privileged daemon -- it can act outside the sandbox entirely";
        case TrustKind::SessionStart:
            return "session start -- runs at your next login, outside the sandbox";
    }
    return "";
}

// 代理-authored写入主机信任的内容。
// 值得打扰用户的事情。
struct TrustHandoff {
    WriteRecord write;
    TrustKind kind;
};

// 当检测到时该怎么做。
//
// 拒绝是默认的，这是对这里每个其他控制的工作方式的故意破坏。
// 批准 提示词 是 "this might be risky"
// 的正确形状；信任移交是 Pillar 系列中每次逃逸的形式，
// 因此默认设置为“否”，用户选择被询问。
enum class HandoffPolicy : std::uint8_t {
    Refuse,  // 默认：写入失败，面板说明原因
    Warn,    // 允许，但大声表达并记录下来
    Allow,   // 关闭。对于配置为 MEANT 的 工作区 代理进行编辑
};

[[nodiscard]] constexpr const char* to_string(HandoffPolicy p) noexcept {
    switch (p) {
        case HandoffPolicy::Refuse: return "refuse";
        case HandoffPolicy::Warn:   return "warn";
        case HandoffPolicy::Allow:  return "allow";
    }
    return "refuse";
}

// 该路径是主机稍后信任的路径吗？纯粹且表 -driven，
// 因此该集合可以在一个地方进行审查，而不是分布在 调用 站点上。
//
// 匹配是在路径SHAPE上，而不是在确切的名称上，
// 因为"Git directories do not have to be called .git"(
// Pillar，Cursor 3.0.0)正是来自精确-name匹配的错误。
[[nodiscard]] bool is_host_trusted(std::string_view path, TrustKind* out_kind);

// per-turn记录。在每回合开始时清除，
// 因此 "what did the agent just write" 无需完整审核日志即可负责。
struct Provenance {
    std::vector<WriteRecord> writes;
    std::vector<TrustHandoff> handoffs;

    void clear() {
        writes.clear();
        handoffs.clear();
    }
};

}  // namespace agentty::sandbox_cfg
