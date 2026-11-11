#pragma once
// agentty::app::read_launch_env-过程环境，作为值。
//
// 由 init() 调用一次。 状态更新函数 从来没有 调用 它：他们读的是 Model::env。

#include "agentty/runtime/model.hpp"
#include "agentty/util/io.hpp"

namespace agentty::app {

[[nodiscard]] Model::Env read_launch_env(Io) noexcept;

}  // namespace agentty::app
