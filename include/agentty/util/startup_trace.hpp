// agentty/util/startup_trace.hpp — 手动启动时间。
//
// AGENTTY_STARTUP_TRACE=<path>在每个标记处将
// "+12.3 ms  name"行附加到<path>，从处理开始（第一标记）开始测量。
// 一旦 TUI 拥有 终端，stderr 就会丢失，因此是一个文件。未设置：一个缓存的
// getenv，没有其他。
#pragma once

#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace agentty::startup {

inline std::FILE* sink() noexcept {
    static std::FILE* f = [] () -> std::FILE* {
        const char* p = std::getenv("AGENTTY_STARTUP_TRACE");
        return (p && *p) ? std::fopen(p, "a") : nullptr;
    }();
    return f;
}

inline void mark(const char* name) noexcept {
    std::FILE* f = sink();
    if (!f) return;
    using clock = std::chrono::steady_clock;
    static const clock::time_point t0 = clock::now();
    const double ms = std::chrono::duration<double, std::milli>(clock::now() - t0).count();
    std::fprintf(f, "+%7.2f ms  %s\n", ms, name);
    std::fflush(f);
}

}  // namespace agentty::startup
