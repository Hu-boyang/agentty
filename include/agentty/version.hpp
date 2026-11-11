#pragma once
// agentty 的版本，来自 CMakeLists.txt 的项目（VERSION）。
//
// 只有 src/version.cpp 可以看到 AGENTTY_VERSION 定义。
// 通过这些函数读取它，以便版本更新重新编译一个文件，而不是每个 TU。

#include <string>

namespace agentty {

// "0.9.38"。 "0.0.0-dev" 在绕过我们的 CMake 的构建上。
[[nodiscard]] const char* version() noexcept;

// "agentty/0.9.38"，User-Agent agentty 发送。
[[nodiscard]] const std::string& user_agent();

}  // namespace agentty
