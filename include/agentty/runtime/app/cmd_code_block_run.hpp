#pragma once
// cmd::run_code_block—副作用、Ctrl+G代码-block、状态更新函数返回。
//
// 从最新的助理回复运行围栏块：在真实的终端上以交互方式使用tee
// 捕获(POSIX)，或者通过捕获的子进程运行器(Windows)。
// 结束时交付 CodeBlockRunFinished。
// cmd_code_block_run.cpp年实施； 状态更新函数 从不接触 tty、
// pid 或时钟。

#include <string>

#include "agentty/runtime/cmd.hpp"
#include "agentty/runtime/panel/code_blocks.hpp"

namespace agentty::app::cmd {

[[nodiscard]] Cmd run_code_block(std::string command,
                                 code_blocks::BlockShell shell);

}  // namespace agentty::app::cmd
