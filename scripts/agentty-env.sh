#!/usr/bin/env bash
# 使用当前工作区用户目录中的编译器和依赖库运行命令。
set -euo pipefail

deps_root="${HOME}/.local/agentty-deps"
deps_prefix="${deps_root}/usr"

export PATH="${deps_prefix}/bin:${PATH}"
export LD_LIBRARY_PATH="${deps_prefix}/lib/x86_64-linux-gnu:${deps_prefix}/lib/llvm-22/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
export PKG_CONFIG_PATH="${deps_prefix}/lib/x86_64-linux-gnu/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
export PKG_CONFIG_SYSROOT_DIR="${deps_root}"
export CMAKE_PREFIX_PATH="${deps_prefix}${CMAKE_PREFIX_PATH:+:${CMAKE_PREFIX_PATH}}"
export CC="${deps_prefix}/bin/clang-22"
export CXX="${deps_prefix}/bin/clang++-22"
CMAKE_TOOLCHAIN_FILE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../cmake" && pwd)/agentty-toolchain.cmake"
export CMAKE_TOOLCHAIN_FILE

exec "$@"
