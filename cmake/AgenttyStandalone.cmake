# AgenttyStandalone.cmake--submodule前设置阶段：
# standalone/static 链接旋钮、CPU ISA基线、LTO/IPO门控、
# mimalloc/maya 切换和平台别名(macOS-GCC _Static_assert、
# Android/Termux部署)。其中的 全部 必须运行 之前 add_subdirectory
# (maya/...) — 它作为从根开始的一个连续块包含在内，以准确保留该顺序。

# ── 独立式的二进制程序管道 ─────────────────────────────────────────
# AGENTTY_STANDALONE=ON产生没有第三
# -party共享-library依赖性的二进制程序——将其放在任何兼容
# 机器上（匹配libc版本）并且它运行。在每个平台上，它都会强制使用正确的
# 静态 链接 旋钮：
#
#   Linux OpenSSL + nghttp2 静态链接。 libstdc++ 和 libgcc
#              通过 -static-libstdc++ / -static-libgcc 合并。
#              libc 保持动态（完全-static glibc 破坏了 NSS 解析器和
#              DNS 查找；如果您需要 100% 静态 二进制程序，
#              请使用 -DAGENTTY_FULLY_STATIC=ON 针对 musl 进行构建）。
#   macOS OpenSSL + nghttp2 静态链接。
#              libSystem 保持动态（Apple 支持分发的唯一 ABI）。
#   Windows强制AGENTTY_STATIC_RUNTIME=ON(/MT)，因此MSVC
#              CRT是静态嵌入的； nghttp2 + OpenSSL 来自 x64
#              -windows-static vcpkg 三元组。
#
# 用 `cmake -B build-rel -DCMAKE_BUILD_TYPE=Release -DAGENTTY_STANDALONE=ON` 构建。
option(AGENTTY_STATIC_RUNTIME "Link the MSVC runtime statically (/MT)" OFF)
option(AGENTTY_STANDALONE     "Produce a standalone binary with no third-party shared-library deps" OFF)
option(AGENTTY_FULLY_STATIC   "Fully static link (Linux only, requires musl toolchain)" OFF)
# 默认情况下，完全-static Linux构建是`-static -no-pie` (ET_EXEC)：
# 在每个Linux用户区(glibc、musl、Pi OS)上运行的真正独立的二进制程序。
# 仅翻转此 ON 到目标 Android/Bionic (Termux)，
# 它拒绝 ET_EXEC 并需要 PIE — 但 musl 工具链上的 仅 其 -static-pie
# 真正静态 链接 libc (Alpine 的默认 -PIE GCC 不执行；
# 它生成 v0.2.7 crasher，build-time 守卫随后拒绝）。
option(AGENTTY_STATIC_PIE      "Emit a static-PIE (ET_DYN) instead of ET_EXEC for the fully-static build — Termux/Android only" OFF)

# mimalloc覆盖-微软的产品分配器，由CMake从上游主获取。
# agentty 到处搅动 std::string（RenderOp、SSE 解析、
# JSON 和元素视图 源码树），这受益于 mimalloc 的分片空闲列表
# 和 提前加载 页面清除。静态 库 覆盖 malloc/free 和全局 C+
# + new/delete.
# 默认ON；转动 OFF 来构建普通系统 分配器。
option(AGENTTY_USE_MIMALLOC   "Route malloc/free and operator new/delete through CMake-fetched mimalloc" ON)

# CPU ISA基线。默认 avx2（Haswell+/Zen1+，~2013），涵盖~每个正在
# 使用的 desktop/laptop。对于 Sandy/Ivy Bridge (2011-
# 2012) 或更旧的 VM 主机，下降到 "avx"；对于真正古老的主机，下降到 "sse2"；
# 对于让 编译器 根据构建机器进行选择的 "native"。 MSVC 仅公开少量 /arch 值；
# GCC/Clang直接理解-march=。
set(AGENTTY_ARCH "avx2" CACHE STRING
    "CPU baseline: native, avx2 (default), avx, or sse2.")
set_property(CACHE AGENTTY_ARCH PROPERTY STRINGS native avx2 avx sse2)

if(AGENTTY_STANDALONE AND MSVC)
    # 在 Windows 上，独立意味着静态 MSVC 运行时 — 没有其他方法来传送
    # 可复制到任意机器上运行的可执行文件。
    set(AGENTTY_STATIC_RUNTIME ON CACHE BOOL "" FORCE)
endif()
if(AGENTTY_STATIC_RUNTIME AND MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# 当请求独立时，告诉find_package(OpenSSL)
# 更喜欢.a而不是.so/.dylib。我们首先尝试静态（QUIET，这样丢失的 .
# a 就不会出错）；如果失败，我们稍后会默默地回退到动态 变体 并警告用户有
# 一个 运行时 dep 漏掉了。大多数发行版（Arch、Debian 默认、
# Fedora）仅将 OpenSSL 打包为 .so； Alpine、
# vcpkg-static 或安装 openssl-static 的用户可以
# 获得完整的独立版本。
set(AGENTTY_STANDALONE_OPENSSL_FALLBACK FALSE)
if(AGENTTY_STANDALONE)
    set(OPENSSL_USE_STATIC_LIBS TRUE)
endif()

# 链接-time 优化 — 在 Release/RelWithDebInfo 中默认启用，
# 在调试中关闭（会拖慢构建时间而没有交付价值）。由 GCC、Clang、
# MSVC 和 AppleClang 支持； CMake 为每个工具链选择正确的 标志。
# 在 maya/acp-cpp/mcp-cpp 子目录之前设置，
# 以便整个 源码树 LTO-链接 在平台上作为一个单元而得到回报。
include(CheckIPOSupported)
check_ipo_supported(RESULT AGENTTY_HAS_IPO OUTPUT AGENTTY_IPO_ERR)

# macOS+GCC是一个工具链，其中LTO全是成本，没有效益。
# Apple 的 ld 无法消耗 GCC 的 LTO 字节码，因此 `-flto`
# 会跨模块优化 不 — 胖对象的 per-TU -O3 代码实际上是 链接 —
# 然而 GIMPLE IR 作为死 __GNU_LTO LOAD 段（数十个MB），
# `strip`/`strip -x` 无法删除（这是 加载 命令，而不是 符号）。
# check_ipo_supported()通过（其探针链接 "works"），
# 因此如果没有该防护装置，macOS释放装置以零速度增益运送约3倍其实际尺寸。
# 仅针对该组合禁用IPO：二进制程序下降到其真实代码大小并且是优化后的字节-for-byte。
# Linux GCC/Clang（其中构建了运输静态 二进制程序 — 完整的
# LTO + maya 的重型 标志 套装）和 MSVC (/GL+/LTCG) 不受影响。
# macOS的真正上限是AppleClang+ThinLTO，
# 还不能构建这个源码树（没有宣传cxx_std_26），所以-O3是macOS的最大值。
set(AGENTTY_IPO_OK ${AGENTTY_HAS_IPO})
if(APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(AGENTTY_IPO_OK FALSE)
    message(STATUS "agentty: LTO disabled on macOS+GCC "
                   "(Apple ld can't consume GCC LTO IR — would only bloat the binary)")
endif()
# MinGW / MSYS2 GCC (ucrt64) 是 GCC 16.x 上的 LTO 雷区：
# 链接 完整的 agentty.exe 将整个 -program GIMPLE 分区为约 200 个
# LTRANS 作业，然后 lto1.exe 在 -stream 中死亡，
#   并带有内部编译器 错误 —
# "cannot read 'LTO_section_decls' from …ltransNN.o"
# — 工具链回归，不 我们的代码（476 编译单元 编译 中的每一个都是干净的；只有最后的
# -flto=auto 整个 -program 链接 阶段崩溃）。分区跳闸的是非 -deterministic，
# 因此没有单一的 TU 可以归咎或分裂。我们实际发布的 Windows 版本 二进制程序 是
# MSVC 版本（/GL+/LTCG，不受影响）； MSYS2 作业是 *编译-only* 可移植性门。
# Per-TU -O3代码无论如何就是这里的链接（MinGW从cross
# -TU LTO获得很少），所以为这个组合放弃IPO使得通过字节
# -identical优化的门通过并且对任何运送工件的风险为零。
if(MINGW OR (WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    set(AGENTTY_IPO_OK FALSE)
    message(STATUS "agentty: LTO disabled on MinGW/MSYS2 GCC "
                   "(GCC 16 lto1 ICE 'cannot read LTO_section_decls' on the "
                   "whole-program link — toolchain bug, not code)")
endif()
# Whole-tree消毒剂构建(-DAGENTTY_SANITIZE_ALL=...)还必须禁用此处的LTO、
# 编译 maya的对象下方的BEFORE add_subdirectory(maya)。
# AGENTTY_SANITIZE_ALL 是有意提前读取的：cmake 命令
# 行上的 `-D`（这就是 CI 和每个记录的清理程序调用如何设置它的方式）从
# 脚本评估一开始就可见，远早于该文件稍后运行的其自己的
# `set(... CACHE STRING ...)` 声明 - 因此此检查
# 不是前向引用，它看到的是真实值。如果没有这个保护，这里使用 IPO=TRUE
# 编译的 maya 会在 libmaya.a 中生成 GCC slim
# -LTO 字节码对象；稍后添加的清理程序 标志 仅在 LINK 时间应用
# `-fno-lto`（没有匹配的 `-flto`），因此 链接器 无法使用
# 这些字节码对象："plugin needed to handle lto object"。
# LTO 购买的消毒剂无论如何都不会生成任何东西（它们是正确性门，
# 而不是发布工件），因此在这里将其关闭完全是一场胜利。
if(AGENTTY_SANITIZE_ALL)
    set(AGENTTY_IPO_OK FALSE)
    message(STATUS "agentty: LTO disabled for sanitizer build "
                   "(AGENTTY_SANITIZE_ALL=${AGENTTY_SANITIZE_ALL} — avoids an "
                   "LTO-bytecode/-fno-lto link mismatch in libmaya.a)")
endif()
if(DEFINED CMAKE_INTERPROCEDURAL_OPTIMIZATION)
    # packager/user在命令行上显式地设置它（e.g。AUR agentty-git
    # PKGBUILD传递OFF，因为GCC 16的LTO流媒体ICE在main.cpp中的C++26
    # decltype上："tree code 'decltype_type' is not supported in LTO streams"）。
    # 他们的话语是最终的——不要重新-derive。
    message(STATUS "agentty: CMAKE_INTERPROCEDURAL_OPTIMIZATION="
                   "${CMAKE_INTERPROCEDURAL_OPTIMIZATION} (user-set, respected)")
elseif(AGENTTY_IPO_OK AND (CMAKE_BUILD_TYPE STREQUAL "Release"
                     OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo"
                     OR CMAKE_BUILD_TYPE STREQUAL "MinSizeRel"))
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
else()
    # 明确设置为 FALSE，不能仅保留未定义状态。
    # third_party/maya/CMakeLists.txt 有自己的回退逻辑：
    #   if(NOT DEFINED CMAKE_INTERPROCEDURAL_OPTIMIZATION)
    #     set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
    #   endif()
    # 这样，外部项目单独添加 maya 时仍可以使用 LTO。
    # 若仅跳过前面的 TRUE 分支而不定义变量，maya 会重新开启 LTO。
    # 这会破坏当前构建中显式关闭 LTO 的要求，涉及 sanitizer 链接匹配、
    # macOS 与 GCC 的体积问题，或上文 MinGW 编译器内部错误。
    # libmaya.a 会因此包含精简 LTO 字节码，后续 -fno-lto 链接无法处理，
    # 并报错 plugin needed to handle lto object。
    # 在这里显式设为 FALSE，maya 才会尊重已经定义的值。
    #
    # 这里过去是 elseif(NOT AGENTTY_IPO_OK)，导致 Debug 落入相同漏洞：
    # 编译器支持 IPO，但构建类型不是发布配置时，两个分支都不执行。
    # 变量未定义导致 maya 为 Debug 开启 LTO，显著拖慢编辑构建循环，
    # 还让调试目标变成精简 LTO 字节码，从而无法使用快速链接器。
    #
    #
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION FALSE)
endif()

# ── PIE 迁移固定(问题20+ mold/objlib R_X86_64_32 失败) ──
# 在现代 Linux 发行版（Fedora、Arch 等）上，编译器 DRIVER
# 默认构建 PIE 可执行文件。 agentty 编译其共享的编译单元一次进入
# OBJECT 库和链接这些对象进入PIE exe AND每次测试。如果这些对象不是
# position-independent、链接，则 PIE 会失败并显示：
#     在制作PIE对象时，不能使用针对“<sym>”的重定位R_X86_64_32；
#     使用 -fPIC 重新编译
# 这会以几种独立的方式进行，因此对其进行狭窄的控制（就像我们对 GNU+LTO
# 所做的那样）导致真正的工具链被破坏：
#   ·GCCLTO针对隐藏的static-local防护泄漏了32-bitabs重新定位，
#     ld.bfd 无法放入 PIE (原始问题 #20)。
#   ·NON-LTO GCC/Clang + `mold` (关于abs，STRICTER比ld.bfd
#     relocs)完全拒绝objlib对象——由Fedora
#     44/GCC 16/clang 22贡献者报告的使用模具构建默认动态
#     源码树的失败。
#   ·单独的-fPIE就足够NOT:GCC/Clang发出直接32-bit重定位
#     符号 他们假设存在于主 exe 中，这会在对象在不同的
#     ​​ 链接（测试 exe）中重用或 链接器 挑剔时中断。
# 编译WHOLE程序-fPIC(不仅仅是-fPIE)强制对每个符号进行
# PIC-safe (GOT/PC-relative)重定位，
# 每个链接器(ld.bfd/金/lld/模具)在PIE中接受该重定位。
# 这是可移植的修复：不依赖于安装哪个 链接器，在 GCC 和 Clang 上工作相同，
# 并且花费 TUI 无可测量。因此，将其应用于 整个 Linux 动态 (PIE)
# 路径 — 每个 编译器、LTO 或不是。
#
# 范围保护：完全-static版本构建链接 -no-pie (ET_EXEC)并且必须保持非
# -PIC（这就是使GitHub-release独立箱保持真正静态ET_EXEC的原因），
# 所以这里是EXCLUDED。 Apple/MSVC 以自己的方式处理 PIC/PIE，
# 也被排除在外。 Belt-and-suspenders：还设置 CMake 属性，
# 以便在没有 agentty 帮助器的情况下定义的目标（imported/fetched
# 取决于我们 链接）在此路径上继承 PIC。
if(CMAKE_SYSTEM_NAME STREQUAL "Linux"
        AND NOT (AGENTTY_FULLY_STATIC AND NOT AGENTTY_STATIC_PIE))
    message(STATUS "agentty: compiling -fPIC for the Linux dynamic (PIE) build "
                   "so OBJECT-lib objects link into the PIE exe + tests under "
                   "any linker incl. mold (fixes R_X86_64_32 PIE reloc, issue #20)")
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fPIC>)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

include(FetchContent)

set(MAYA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MAYA_BUILD_TESTS    OFF CACHE BOOL "" FORCE)

# STANDALONE构建必须可跨目标架构的每个标签移植。
# Maya 的默认 `-march=native -mtune=native` 否则会将
# 生产主机的微架构烘焙为 libmaya.a (在 aarch64 上最明显 —
# Cortex-A72 上的 Graviton3-built 二进制程序 SIGILL)。
# 强制关闭 native-tuning 门，以便 maya 回落到 编译器 的默认基线 (
# `-march=x86-64` / `armv8-a`)。
if(AGENTTY_STANDALONE)
    set(MAYA_NATIVE_TUNING OFF CACHE BOOL "" FORCE)
endif()

# macOS SDK 与 GCC 的兜底情况，特别是 macOS 26 SDK。
# 的<mach/*.h> arm64 size-assert宏扩展为
# `_Static_assert(...)`，C关键字g++在C++模式下拒绝(只有
# clang接受它作为扩展)。 任意 TU 拉入马赫 头文件（subprocess.cpp
# 通过 <spawn.h>、tls.cpp 等）使其脱扣。别名目录-wide 此处 -
# 在下面的每个add_subdirectory（maya，acp-cpp，
# mcp-cpp）之前和agentty /测试目标之前 - 所以所有这些，包括未来的子模块，
# 都从一个地方继承它，而不是每个re-discovering破损。 AppleClang
# 不需要它（并且无法构建此 源码树 — 它不会宣传 cxx_std_26）。
if(APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_definitions(_Static_assert=static_assert)
endif()

# ── 机器人/ 双层 Termux :增加部署 API 改为28国 ───────────────────────
# 两个 Bionic 符号 agentty 的依赖图需求为 API-gated:
#   ·<spawn.h>/posix_spawn*→__INTRODUCED_IN(28)
#   ·strtod_l/strtof_l→__INTRODUCED_IN(26)(libc++<locale>
#     调用这些UNCONDITIONALLY，通过<iostream>/
#     <format>传递地拉进来，所以整个C++标准库无法在26以下的编译)
#
# NOT足以使-D__ANDROID_API__=28:仅移动预处理器宏，而clang的__attribute__((可用性))
# 诊断与烘焙到目标三元组的API后缀（e.g.aarch64
# -linux-android24)中的DEPLOYMENT
# TARGET进行比较。如果该级别低于 符号 的
# 介绍级别，您将获得：
#     错误：'strtod_l'不可用：在Android 26中引入致命
#     错误：即使引发了宏，也找不到'spawn.h'文件。
# 批准的 Termux 修复（termux-packages #23401）是通过
# --target=<arch>-linux-android28，以便部署目标本身向上移动。现代
# Termux 始终在 API ≥ 28 台设备上运行，因此在 28
# 台设备上部署是安全的。
#
# 从 编译器 自己的默认三元组导出 <arch>，因此这无需硬编码即可在
# aarch64/arm/x86_64 上工作。适用于 两者 编译 和 链接（CRT 对象也具有
# API 级别）。 Directory-wide + 每个 add_subdirectory 之前，
# 因此 maya / acp-cpp / mcp-cpp 和 agentty/test
# 目标都继承它。每个非 -Android 平台上都没有 -op。
if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Android"
           OR DEFINED ENV{TERMUX_VERSION}
           OR EXISTS "/data/data/com.termux/files/usr")
    # Ask 编译器为其默认目标三元组(e.g.
    # "aarch64-linux-android24")并将尾随API号重写为28。
    execute_process(
        COMMAND ${CMAKE_CXX_COMPILER} -dumpmachine
        OUTPUT_VARIABLE _agentty_android_triple
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(_agentty_android_triple MATCHES "^(.+-linux-android)[0-9]*$")
        set(_agentty_android_target "${CMAKE_MATCH_1}28")
    elseif(_agentty_android_triple MATCHES "android")
        set(_agentty_android_target "${_agentty_android_triple}")
    else()
        # 回退:如果-dumpmachine没有给我们任何android-shaped(
        # 不寻常的)，则从处理器导出。 aarch64 是压倒性的 Termux 案例。
        set(_agentty_android_target "aarch64-linux-android28")
    endif()
    message(STATUS "agentty: Android/Termux detected — deploying at "
                   "${_agentty_android_target} (exposes spawn.h + strtod_l)")
    add_compile_options("--target=${_agentty_android_target}")
    add_link_options("--target=${_agentty_android_target}")
    # 将预处理器宏保留在 锁-step 中，以便任何直接读取它的
    # 代码（首先取消定义，以便工具链自己的 -D 不会冲突）。
    add_compile_options(
        "$<$<COMPILE_LANGUAGE:C,CXX>:-U__ANDROID_API__>"
        "$<$<COMPILE_LANGUAGE:C,CXX>:-D__ANDROID_API__=28>")

    # BELT-AND-SUSPENDERS：还将目标+API宏直接烘焙到
    # CMAKE_<LANG>_FLAGS中。 add_compile_options
    # () 仅到达 此 目录中定义的目标以及添加 之后 和 调用 的子目录；
    # 任何通过不同机制（重置 标志 的 FetchContent 项目、
    # 重置 调用 project() 的 子模块 和 re-detects 编译器、
    # out-of-order add_subdirectory）都可能会错过它。
    # CMAKE_*_FLAGS无条件地是EVERY（子）目录中的字符
    # 串-prepended到EVERY 编译，因此mcp-cpp / maya
    # / acp-cpp无论自己的CMake如何构造，都将获得部署目标。
    # 幂等：防止重新配置时出现双-append。
    set(_agentty_android_flags
        "--target=${_agentty_android_target} -U__ANDROID_API__ -D__ANDROID_API__=28")
    if(NOT CMAKE_CXX_FLAGS MATCHES "--target=${_agentty_android_target}")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${_agentty_android_flags}")
    endif()
    if(NOT CMAKE_C_FLAGS MATCHES "--target=${_agentty_android_target}")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${_agentty_android_flags}")
    endif()
    if(NOT CMAKE_EXE_LINKER_FLAGS MATCHES "--target=${_agentty_android_target}")
        set(CMAKE_EXE_LINKER_FLAGS
            "${CMAKE_EXE_LINKER_FLAGS} --target=${_agentty_android_target}")
    endif()
    if(NOT CMAKE_SHARED_LINKER_FLAGS MATCHES "--target=${_agentty_android_target}")
        set(CMAKE_SHARED_LINKER_FLAGS
            "${CMAKE_SHARED_LINKER_FLAGS} --target=${_agentty_android_target}")
    endif()
endif()

