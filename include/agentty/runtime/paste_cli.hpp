#pragma once
// `agentty paste …`：`config`是agentty的（粘贴.*设置）；
// 每个其他子命令都是 richpaste 的命令行，在安装这些设置的情况下运行。

#include <span>
#include <string>

#include <maya/runtime.hpp>   // maya::unlocked

#include "agentty/util/io.hpp"

namespace agentty::paste_cli {

int run(Io io, const maya::unlocked& cap, std::span<const std::string> args);

}  // namespace agentty::paste_cli
