#pragma once
#include <maya/widget/composer.hpp>
#include "agentty/runtime/model.hpp"

namespace agentty::ui {

[[nodiscard]] maya::Composer::Config composer_config(const Model& m);

// 作曲家是否会声称终端的HARDWARE 光标这个帧？
//
// 摘自composer_config，因此一个条件有一个定义。
// 它决定不同文件中必须一致的两件事：
//
//   *我们手上的配置maya (composer.cpp)，以及
//   *visual_hash是否应当跟踪眨眼阶段(program.hpp)。
//
// 对于硬件光标，终端拥有闪烁：maya不绘制光标
// 存储单元并且不调度帧，因此不存在哈希要遵循的可见步骤。
// 无论如何，混合闪烁奇偶校验的主机都会在静止屏幕上使哈希
// 翻转 ~4x/sec，并且循环 重绘 作曲家在 光标 下，
// 终端 在自己的时钟上闪烁 - 他们跳动，光标 闪烁。
// Re-deriving 手上的状况是让两人渐行渐远的原因；
// 这就是解决方法。
[[nodiscard]] bool composer_uses_hardware_caret(const Model& m) noexcept;

} // namespace agentty::ui
