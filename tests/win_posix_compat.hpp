// tests/win_posix_compat.hpp -- POSIX 环境 调用 测试套件 Windows
// (MinGW/UCRT).
//
// 套房将过程点到临时 HOME / 工作区 带有setenv和unsetenev
// 的二维 UCRT 强迫 强迫 -included 输入每个测试 TU
// 打开 Windows (CMakeLists.txt,
// AGENTTY_BUILD_TESTS 测试时要写在 POSIX 和
// shimim住在一个地方。 src/ 启动后从不设置环境变量。
//
// 需要更多测试 POSIX 的便携式 调用 改为网站( tests/wire_pipe.hpp
// 用于管道+ Writer, #ifdef SIGPIPE, a 看门狗 线程 提醒。
// 打开的 -driven 测试是 UNIX-only 输入 AgenttyTests.cmake.
#pragma once

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <process.h>   // 明沃在此宣布(已贬值,但已生效)
#include <windows.h>

#include <cstdlib>
#include <string>

inline int setenv(const char* name, const char* value, int overwrite) {
    if (!name || !*name) return -1;
    if (!overwrite && std::getenv(name)) return 0;
    return _putenv_s(name, value ? value : "");
}

inline int unsetenv(const char* name) {
    if (!name || !*name) return -1;
    // 一个空值会删除_putenv_s下的变量.
    return _putenv_s(name, "");
}

// 测试写入 `setenv("HOME", dir.c_str(), 1)` 带 std::filesystem::path ,
// 其名称为 c_str ( ) 系 , ( ) wchar_t * 开着 Windows。相同的含义, 广义的价值。
inline int setenv(const char* name, const wchar_t* value, int overwrite) {
    if (!name || !*name) return -1;
    if (!overwrite && std::getenv(name)) return 0;
    const std::wstring wname(name, name + std::char_traits<char>::length(name));
    return _wputenv_s(wname.c_str(), value ? value : L"");
}
#endif
