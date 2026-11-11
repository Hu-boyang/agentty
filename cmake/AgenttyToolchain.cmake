# AgenttyToolchain.cmake — 编译器 缓存，C++标准+
# 优雅的回退，默认构建类型，以及MSVC发布标志规范化。包含在 project() 之后，
# 因此它适用于每个目标（agentty + 所有 子模块）。

# ── 编译器 缓存 (夹克/夹克) 自动-detect ──────────────────────────
# 紧接在project()之后放置，因此它适用于源码树中的EVERY目标，
# 包括通过add_subdirectory (maya、mcp-cpp、rag-cpp、
# acp-cpp)拉入的子模块。这些 子模块 是 头文件-heavy：一个同步的 上游 提交，
# 广泛涉及一个 -included 头文件 (e.g.maya/widget/input.hpp)，
# 否则会重新编译所有 ~41 maya 编译单元 加上包含 maya 头文件 的 ~64
# agentty 编译单元，即使给定TU的预处理内容实际上并没有改变。 CONTENT 上的
# ccache 键，因此这些键立即成为 缓存 命中 —
# "submodules are mostly headers, make re-sync rebuilds fast" 的正确修复。
#
# 关闭是不可能出错的：如果这两个工具都没有安装，我们只是不设置启动器并且构建与以前完全相同。
# 使用 -DAGENTTY_COMPILER_CACHE=OFF.
# 选择退出 荣誉用户-set *_COMPILER_LAUNCHER。
option(AGENTTY_COMPILER_CACHE "Use ccache/sccache as the compiler launcher when available" ON)
if(AGENTTY_COMPILER_CACHE AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
    find_program(AGENTTY_CCACHE NAMES ccache sccache)
    if(AGENTTY_CCACHE)
        set(CMAKE_C_COMPILER_LAUNCHER   "${AGENTTY_CCACHE}" CACHE STRING "" FORCE)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${AGENTTY_CCACHE}" CACHE STRING "" FORCE)
        message(STATUS "agentty: compiler cache enabled (${AGENTTY_CCACHE}) — "
                       "content-hashed rebuilds; header-only submodule syncs stay fast")
    else()
        message(STATUS "agentty: no ccache/sccache found — install ccache to speed up "
                       "rebuilds after submodule syncs (brew/apt/pacman install ccache)")
    endif()
endif()

# CMake（截至 4.2）还没有 MSVC 的 /std:c++26 映射。 Ask 适用于 C
# ++23，并选择 /std:c++latest，因此 MSVC 14.50+ 公开可用的 C++
# 26 库 位（std::expected、std::format 等）。其他 编译器 获得 C++26。
if(MSVC)
    set(CMAKE_CXX_STANDARD 23)
else()
    set(CMAKE_CXX_STANDARD 26)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── C++26 + C++23 优雅 回退 (Termux / 较老的语气) ─────────────────
# C++26 的 源码树 ASKS 在非 -MSVC 编译器 上，但使用 NO C++26
# -only LIBRARY 设施 — 每个 TU 编译 在 C++23 上干净利落（MSVC
# 分支已经通过在 /std:c++23-equivalent 构建整个 源码树 证明了这一点）。
# 因此，与其在不宣传 cxx_std_26（clang
#     < 18，gcc < 14）的 编译器 上使用 CMake 的原始
# "Target ... requires the language dialect "CXX26（目标要求编译器支持 CXX26 语言标准。）
# " ... not supported" 进行硬-FAIL 配置，不如退回到 C++23 并告诉
# 用户到底发生了什么。 Termux 是这里的常见情况：新的 `pkg install clang`
# 是 18+，但较旧的安装和其他嵌入式工具链则为 17。
if(NOT MSVC AND NOT "cxx_std_26" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
    if("cxx_std_23" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
        set(CMAKE_CXX_STANDARD 23)
        message(STATUS
            "agentty: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} "
            "does not advertise C++26 — falling back to C++23 (fully supported; "
            "no C++26-only library facility is used).")
    else()
        set(_agentty_is_termux OFF)
        if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Android"
                   OR DEFINED ENV{TERMUX_VERSION}
                   OR EXISTS "/data/data/com.termux/files/usr")
            set(_agentty_is_termux ON)
        endif()
        if(_agentty_is_termux)
            message(FATAL_ERROR
                "agentty needs at least C++23, but this Termux clang "
                "(${CMAKE_CXX_COMPILER_VERSION}) is too old. Run "
                "`pkg upgrade clang` (Termux ships clang 18+, which is more "
                "than enough), then re-run cmake.")
        else()
            message(FATAL_ERROR
                "agentty needs a compiler that supports C++23 (clang >= 16, "
                "gcc >= 12). Detected ${CMAKE_CXX_COMPILER_ID} "
                "${CMAKE_CXX_COMPILER_VERSION}, which advertises neither "
                "cxx_std_26 nor cxx_std_23. Please upgrade the compiler.")
        endif()
    endif()
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ── 大小选择 ─────────────────────────────────────────
# -Os在源码树上削减了.text 31% (LTO之前的42.9-> 29.5
# MB)。全部 -Os 构建的性能工作台在噪音范围内或更快（降价 渲染 快
# 10-15%）；其中一个损失是 16 MB 线程 的第一个完整 保存，
# 慢了约 6%。 maya 在此之后仍然添加自己的 -O3 作为其 渲染 路径。
# 在任何 add_subdirectory 之前设置，以便每个 子模块 都能看到它。
option(AGENTTY_OPTIMIZE_SIZE "Release builds use -Os instead of -O3 (maya keeps -O3)" ON)
# 不是在 macOS 和 GCC 上：在 -Os 处，每个对象都定义了 libstdc
# ++ 的异常 vtables/typeinfo，并且针对 libstdc++.
# a 的静态 链接 失败，并出现 42 个重复的 符号（v0.9.44 版本，gcc 16.2）。
# 苹果的 clang 和其他平台都很好。
if(APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(AGENTTY_OPTIMIZE_SIZE OFF CACHE BOOL "" FORCE)
endif()
if(AGENTTY_OPTIMIZE_SIZE AND NOT MSVC)
    foreach(_v CMAKE_CXX_FLAGS_RELEASE CMAKE_C_FLAGS_RELEASE)
        string(REGEX REPLACE "(^| )-O[0-3s]( |$)" " -Os " ${_v} "${${_v}}")
        string(STRIP "${${_v}}" ${_v})
    endforeach()
endif()

# ── MSVC 释放: 脱光默认 / Ob2 , 这样我们的 / OB3 就不会触发 D9025 ──
#    CMake的默认MSVC版本标志是"/MD /O2 /Ob2 /DNDEBUG"。
#    我们想要使用 per-target /Ob3（积极内联）。我们还想自己处理
#    /O2，因此下面的 per-target 版本 标志 是权威的。
#    /MD 与 /MT 是通过该文件顶部的 CMAKE_MSVC_RUNTIME_LIBRARY
#    处理的 - 见下文。
if(MSVC)
    foreach(flag_var
            CMAKE_CXX_FLAGS_RELEASE
            CMAKE_CXX_FLAGS_RELWITHDEBINFO
            CMAKE_CXX_FLAGS_MINSIZEREL
            CMAKE_C_FLAGS_RELEASE
            CMAKE_C_FLAGS_RELWITHDEBINFO
            CMAKE_C_FLAGS_MINSIZEREL)
        string(REGEX REPLACE "/O2" "" ${flag_var} "${${flag_var}}")
        string(REGEX REPLACE "/Ob[0-3]" "" ${flag_var} "${${flag_var}}")
    endforeach()
endif()
