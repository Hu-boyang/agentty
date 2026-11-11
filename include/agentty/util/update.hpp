#pragma once
// agentty::update — self-update 对照 GitHub 版本。
//
// 设计目标，依次为：
//   1. 绝不 减慢任何速度。 TUI 检查在后台 线程 上运行，
//      推迟过去的第一次油漆，将-limited的速度减至24小时后的-disk 缓存，
//      而其唯一的副作用是Msg 分派。 渲染 路径上没有网络。
//   2.使更新变得简单。来自任何 shell 的 `agentty update`； `/update` 在
//      调色板（当更新实际可用时出现ONLY）；
//      状态栏中不显眼的 "⬆ v0.3.1" 标签 作为信号。
//   3.故障安全。下载到同级临时文件，验证，原子重命名
//      在当前的二进制程序上（正在运行的进程保留其旧图像——经典
//      Unix；在Windows上，旧文件首先被移到一边）。
//      任何故障都不会影响已安装的 二进制程序。
//
// 通道：agentty-org/agentty、single-file 二进制程序的GitHub版本，命名为agentty
// -<os>-<arch>[.exe]。通过 install-path 启发式检测 Package
// -manager 安装 (rpm/pacman/ebuild)，并获取 "use your package manager"
// 消息而不是 self-replace。

#include <functional>
#include <optional>
#include <string>

namespace agentty::update {

// 版本检查的结果。
//
// 两个 版本，是故意的。 `latest` 为最新版本 GitHub 调用 为最新版本；
// `latest` 可能还不是 INSTALLABLE，因为发布工作流程 数据流：发布
// 页面和源 tarball 首先出现，然后每个平台的 二进制程序 在接下来的
// 大约 35-80 分钟内登陆（#119）。在此窗口期间，"v0.9.46 is out" 为真，
// 而 "you can update to v0.9.46" 则不然。
//
// 因此 `update_available` 和 `target` 描述了 此
// 平台实际可以安装的内容——最后几个版本中的最新版本，它运送了我们的资产—
// —而 `pending` 表示最新版本存在，但其 二进制程序 仍在构建中。
// 客户端从不提供它无法执行的更新。
struct CheckResult {
    bool        update_available = false;  // `target`比`current`更新，AND可安装
    std::string current;        // e.g. "0.3.0"
    std::string latest;         // 最新版本，是否可安装（错误时为空）
    std::string target;         // 最新INSTALLABLE发布；当没有更新时为空
    std::string url;            // 浏览器URL的`target`发布页面(或最新的)
    std::string asset_sha256;   // GitHub对目标资产的摘要，十六进制；可能为空
    bool        pending = false;  // `latest` 较新，但对我们来说它的 二进制程序 尚未启动
    std::string error;          // non-empty ⇒ 检查本身失败
};

// 比较两个带点的版本（"0.3.0" vs "0.3.1")；如果 b 较新则为 true。
// 容忍前导'v'和尾随垃圾。缺少的组件为 0。
[[nodiscard]] bool version_less(const std::string& a, const std::string& b);

// 编译后的-in版本("0.3.0")。
[[nodiscard]] std::string current_version();

// 查询GitHub以获取最新版本标签。 `force` 绕过 24 小时 缓存。
// 网络轮-trip — 调用来自工作线程 线程，绝不是UI 线程。
[[nodiscard]] CheckResult check_latest(bool force = false);

// Non-blocking:最后的CACHED检查结果，如果存在并且是新鲜的。
// 读取一个小JSON文件；安全启动。
[[nodiscard]] std::optional<CheckResult> cached_check();

// 该构建平台的release-asset名称("agentty-linux-x86_64")，
// 或者当平台没有预构建资产时为空。
[[nodiscard]] std::string platform_asset();

// 运行的二进制程序是self-replaceable吗？对于 package-manager 安装
// （rpm 布局中的 /usr/bin、/usr/local）为 False，
// 其中正确的路径是包管理器。 `reason` 解释何时为 false。
[[nodiscard]] bool self_update_possible(std::string& reason);

// 执行更新：下载`tag`的平台资产并原子地替换正在运行的二进制程序。
// `progress`（可选）用（downloaded_bytes，
// total_bytes）调用 - 未知时总计 0。
// 当 `expected_sha256` 不是 -empty 时，下载必须与其匹配，
// 否则不会安装任何内容。
// 如果成功则返回空字符串，否则返回人为错误消息。
[[nodiscard]] std::string perform_update(
    const std::string& tag,
    const std::function<void(std::size_t, std::size_t)>& progress = {},
    const std::string& expected_sha256 = {});

// 纯的，用于测试：给定GitHub `/releases` JSON
// （最新的在前）、正在运行的版本和该平台的资产名称，决定提供什么。
// 无网络，无缓存。
[[nodiscard]] CheckResult choose_release(const std::string& releases_json,
                                         const std::string& current,
                                         const std::string& asset);

} // namespace agentty::update
