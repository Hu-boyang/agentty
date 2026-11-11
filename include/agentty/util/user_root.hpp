#pragma once
// agentty::util::user_root —THE单个每-user存储根：~/.agentty
//
// ─────────────────────────────────────────────────────────────────────────
// 为什么是ONE根，为什么是NOT $XDG_CONFIG_HOME/agentty
// ─────────────────────────────────────────────────────────────────────────
// agentty的per-user足迹是配置+数据+秘密+缓存+日志，紧密耦合：设置引用线程、
// 内存提供提示词、凭据门一切。 XDG base-dir 规范会将其分散在
// FOUR 根 (config/data/state/cache) 上 - 并且
// 历史显示了 multi-root 布局对这个 代码库 的作用：由 coin-flip
// 登陆的文件（modelsdev.json - 缓存 - 在 .config 中；
// stderr.log 在 .config 中，而 agentty.log
// 位于 ~/.agentty 中；注释main.cpp 在编写
// config_dir() 的代码旁边表示 `~/.agentty`。
// 对于 "where does a new file go" 没有一个明显答案的布局肯定会产生漂移。
//
// 将所有内容放在 $XDG_CONFIG_HOME/agentty 下反而会更糟，
// 而不是更好：
//   ·~/.config是人们将BACK、UP和SYNC视为点文件。 agentty 的
//     dir 保存 OAuth 刷新令牌和完整的对话历史记录
//     — 将秘密 + 数百个 MB 的 线程 静默同步到点
//     文件 仓库 中是真正的危害，而不是风格问题。
//   ·规范本身说.config适用于CONFIG。 线程 是数据，
//     令牌是秘密，modelsdev.json是缓存，日志是状态。
//     "Technically they're all conf files" 不是真的，
//     并且遵守规范将意味着 four-way 分散，而不是 .config-for-everything。
//
// 所以 agentty 遵循其 对端 的约定（Claude 代码、Ghostty
// 的 macOS 布局、gh、aws、gcloud、cargo、rustup…）：
// 一个 应用 dotdir，仅限所有者，具有内部布局，可以准确地告诉每个未来
// 文件它所属的位置：
//
//   ~/.agentty/
//     settings.json、mcp.json、hooks.json、skills/、
//                      agents/、commands/ ← user-facing 配置：顶级，
//                        可发现，用户可能掌握的部分-edit 或 diff
//     threads/ memory.jsonl
//                      ← 数据：会话历史记录，学到的 内存 credentials/
//     ←机密（0700 目录，0600 个文件）：credentials.json、
//                        accounts.json、提供商 OAuth 文件 + 锁
//     cache/ ← 可重新获取：modelsdev.json、update_check.json
//     logs/：保存 agentty.log 和 stderr.log。
//
// 一个`rm -rf ~/.agentty`重置应用程序。一个 `chmod 0700`
// 就是整个安全故事。一个目录来记录。布局与 PROJECT-side .
// agentty（scope.hpp 的方言阶梯）相匹配，
// 因此 "the .agentty dir" 在两个基因座上的含义相同。
//
// $AGENTTY_HOME覆盖根批发（测试、容器、
// 多配置文件设置）。 $XDG_* 是故意参考 不 的——
// 固定名称使 `docs`、错误消息和支持答案保持无条件。
//
// 迁移：前-consolidation布局将凭据和缓存保留在$XDG_CONFIG_HOME
// /agentty(~/.config/agentty)中。 user_root
// () 在第一次针对旧安装运行时将这些文件迁移到 一次（复制+验证，然后删除 best
// -effort）；请参阅 user_root.cpp 中的 migrate_legacy_config
// ()。

#include <filesystem>

namespace agentty::util {

// per-user根(~/.agentty或$AGENTTY_HOME)。
// 首次使用时创建 0700；从旧版 ~/.config/agentty split-root
// 布局的迁移每个进程运行一次（幂等、best-effort，从不抛出）。
[[nodiscard]] std::filesystem::path user_root();

// 子目录访问器——每个子目录访问器在第一次使用时创建其目录。
// 使用它们而不是连接文字，以便布局保持在一处强制执行。
//
// ─────────────────────────────────────────────────────────────────────
// 将 BULK 从配置磁盘上移出
// ─────────────────────────────────────────────────────────────────────
// $AGENTTY_HOME 移动整个根，当您想要在其他地方安装 agentty 时，
// 这是正确的答案。这是用户实际提出的另一个问题的错误答案（问题＃58）：
// 配置放在系统盘，数据放在第二块磁盘。
//
//
// 这些数字说明了该问题合理的原因。在工作安装中，
// 拆分大致为设置 4 KB、凭据 8 KB — 与 线程
// 4 MB、缓存 5 MB 和日志 38 MB。
// 字节~99%都是下面这三类，而且正是那些无极限生长
// 的。
//
// 因此每个目录都有其自己的环境变量。存储仅通过环境设置；
// agentty 中没有任何内容。
//
//   $AGENTTY_THREADS_DIR对话历史记录
//   $AGENTTY_CACHE_DIR可重新获取下载
//   $AGENTTY_LOGS_DIR诊断
//   $AGENTTY_CREDENTIALS_DIR秘密，
//   强制0700 $AGENTTY_STATE_DIR批准散列
//
// 这是NOT退到XDG和-root布局上面的理由拒绝的，
// 区别是默认的。 XDG 默认分散，
// 因此 "where does a new file go" 有四个答案，
// 文件通过 coin-flip 登陆。这里默认值仍然是 一个 根，
// 每个访问者仍然通过这个 头文件，并且覆盖是一个故意的 per-install 选择，
// 对于任何没有设置它的人来说，没有任何改变。
//
// 相对路径是根据用户 root 而不是进程 CWD 解析的 -
// `AGENTTY_LOGS_DIR=logs2` 表示 ~/.agentty/logs2，
// 而不是 "wherever agentty happened to be launched from"，
// 这会将日志分散到您启动它的每个目录中。
//
// docs/website/storage.md 是所有这一切的用户-facing 地图。
[[nodiscard]] std::filesystem::path user_credentials_dir();  // 秘密，0700
[[nodiscard]] std::filesystem::path user_state_dir();        // 批准
[[nodiscard]] std::filesystem::path user_cache_dir();        // 可重新取回
[[nodiscard]] std::filesystem::path user_logs_dir();         // 诊断
[[nodiscard]] std::filesystem::path user_threads_dir();      // 对话历史

}  // namespace agentty::util
