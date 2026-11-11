#pragma once
// agentty::util::self_exe_path——运行agentty 二进制程序的路径。
//
// 用在其他东西必须再次启动相同的二进制程序的地方：编辑器的终端登录方法，
// self-updater。如果 OS 不说则为空。

#include <filesystem>

namespace agentty::util {

[[nodiscard]] std::filesystem::path self_exe_path();

}  // namespace agentty::util
