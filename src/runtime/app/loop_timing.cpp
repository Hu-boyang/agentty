// loop_timing.cpp — note_slow()：命名使用 帧 的循环步骤。
//
// 故意位于状态更新函数之外：它读取真实时钟，而归并不能读取真实时钟(
// elm_purity)。 program.hpp 支架每个更新臂、
// view() 和 subscribe()；快速步骤需要一个时钟读取和比较。

#include "agentty/runtime/app/program.hpp"
#include "agentty/util/logx.hpp"

namespace agentty::app {

void note_slow(const char* what, std::chrono::steady_clock::time_point t0,
               int alt) noexcept {
    // 一个 帧，60Hz。任何较慢的速度都可以视为输入延迟。
    constexpr auto kFrame = std::chrono::milliseconds(16);
    const auto dt = std::chrono::steady_clock::now() - t0;
    if (dt < kFrame) return;
    const double ms = std::chrono::duration<double, std::milli>(dt).count();
    if (alt >= 0) AGT_LOG(Ui, Warn, "loop.slow", "{}#{} {:.1f} ms", what, alt, ms);
    else          AGT_LOG(Ui, Warn, "loop.slow", "{} {:.1f} ms", what, ms);
}

}  // namespace agentty::app
