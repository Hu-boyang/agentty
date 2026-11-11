cmake_minimum_required(VERSION 3.28...3.31)
project(agentty
    VERSION     0.9.51
    DESCRIPTION "A fast terminal coding agent (TUI) in modern C++"
    LANGUAGES   CXX)

# 项目 CMake 模块（测试 注册表，以及 — 随着重新设计落地 — — 提取的
# toolchain/standalone/hardening 包括）在 cmake/. 下运行
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# GNU ld将MinGW 加载上每个对象的完整符号表放入内存中；测试二进制程序
# 链接整个源码树，并且四个同时耗尽了63个GB盒子（
# "could not read symbols: memory exhausted"）。（无法读取符号：内存耗尽。）
# 编译 保持完全平行； 链接 轮流。使用 -DAGENTTY_LINK_JOBS=N. 覆盖
if(MINGW AND CMAKE_GENERATOR MATCHES "Ninja")
    set(AGENTTY_LINK_JOBS 1 CACHE STRING "Concurrent link jobs (Ninja pool)")
    set_property(GLOBAL APPEND PROPERTY JOB_POOLS agentty_link=${AGENTTY_LINK_JOBS})
    set(CMAKE_JOB_POOL_LINK agentty_link)
endif()

# 源码树中的任何地方都没有C++模块。打开扫描后，每个目标都会获得 ninja dyndep 文件，
# ninja 1.13 可以对这些 mid-build (
# RefreshDyndepDependents) 进行断言。全球范围内关闭，因此不会错过任何目标。
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# 工具链：编译器 缓存，C++标准+回退，默认构建类型，MSVC发布标志规范化。
# 参见cmake/AgenttyToolchain.cmake.
include(AgenttyToolchain)

# Pre-submodule设置：standalone/static旋钮、ISA基线、LTO/IPO选通、
# mimalloc+maya 切换、平台别名。参见cmake/AgenttyStandalone.cmake.
include(AgenttyStandalone)

# 依赖性获取：子模块 auto-pull、FetchContent、add_subdirectory用于acp/mcp/rag、
# nghttp2发现。参见cmake/AgenttySubmodules.cmake.
include(AgenttySubmodules)

# Per-domain源组列表(AGENTTY_*_SOURCES)。参见cmake/AgenttySources.cmake.
include(AgenttySources)
include(CheckCXXSourceCompiles)

# ── 共享 编译 标志 ────────────────────────────────────────────────
# 所有agentty 编译单元(主要二进制程序，下面共享OBJECT 库，
# 以及每次测试)必须 编译与字节-identical 标志。 arch
# 标志 (-march / /arch) 将 内建函数 选择和 ABI 烘焙到每个 .
# o 中；混合使用不同 标志 构建的对象是未定义的。集中在这里，
# 这样 OBJECT 库 和 exe 就不会漂移。
function(agentty_apply_compile_flags tgt)
if(MSVC)
    target_compile_options(${tgt} PRIVATE
        /W4
        /utf-8                 # 将源和执行字符集视为 UTF-8
        /std:c++latest         # 选择进入 /std:c++23 之外的 C++26 库 位
        /permissive-           # 严格符合
        /Zc:preprocessor       # 合格预处理器
        /Zc:__cplusplus        # 报告真实__cplusplus值
        /Zc:inline             # 在 编译 时间删除未引用的 COMDAT
        /Zc:throwingNew        # 假设::new从不返回null
        /EHsc
        /bigobj                # maya的模板超出了默认部分
        /MP                    # 平行编译穿过编译单元
        /wd4100                # 未引用的形式参数——在 lambda 中常见
        /wd4127                # 条件表达式为常数（如果constexpr路径）
        /wd4324                # 由于对齐说明符而填充的结构
        # ── 发布 -only 积极优化 ─────────────────────
        $<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:
            /O2                #  max-speed优化
            /Ob3               #  超出/Ob2的积极内联
            /Oi                #  内建函数功能
            /Ot                #  青睐速度胜过尺寸
            /Oy                #  省略帧指针(释放GPR)
            /GL                #  整体-program优化（与/LTCG配对）
            /GF                #  消除重复字符串
            /Gy                #  功能-level 链接(链接器/OPT:ICF/REF素材)
            /Gw                #  链接器到fold/strip的包全局变量
            /GS-               #  没有 stack-buffer cookies——TUI，不是守护进程
            /GR                #  保留RTTI(context.hpp使用typeid)
            /fp:fast           #  放宽FP严格性——无错误/重新关联
            $<$<STREQUAL:${AGENTTY_ARCH},avx2>:/arch:AVX2>    # Haswell+/Zen1+
            $<$<STREQUAL:${AGENTTY_ARCH},avx>:/arch:AVX>      # Sandy/Ivy桥
            # MSVC没有/arch:SSE2(这是x64上的默认值)或/arch:native；
            # 两者都映射到 "no flag" — 默认的 x64 代码生成器已经假定
            # SSE2。 MSVC 上的 `native` 降级为默认基线。
            /Qpar              #  为热循环启用auto-parallelizer
        >
    )
    target_compile_definitions(${tgt} PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        $<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:NDEBUG>
    )
else()
    target_compile_options(${tgt} PRIVATE
        -Wall -Wextra -Wpedantic
        -Wno-deprecated-declarations
        # 像`TextElement{.content=…, .style=…}`
        # 这样指定的init是跨视图层的有意模式——将缓存字段保留为默认
        # -initialized是正确的，而不是错误。 -Wmissing-field-
        # 初始值设定项（-Wextra 默认值）不会对此进行建模。
        -Wno-missing-field-initializers
    )
    # Clang 18+将部分“指定”初始化器分割成它们自己的警告，
    # -Wno-missing-field-initializers不再覆盖。
    # 相同的有意模式，相同的良性默认-init——也抑制它。
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(${tgt} PRIVATE
            -Wno-missing-designated-field-initializers)
    endif()
    # GCC -Wmaybe-uninitialized对指定-initialized聚合的std::
    # variant移动（maya的元素{TextElement{...}}模式）产生误报。
    # 警告逃逸-isystem，因为它们在后期优化期间触发。
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${tgt} PRIVATE -Wno-maybe-uninitialized)
    endif()

    # 注意：-ffunction-sections/-fdata-sections
    # + --gc-sections 在此测量并给予 ZERO 大小胜利 — 整个
    # -program LTO 链接 (默认打开) 在 GC 部分运行之前已经执行了 cross
    # -module dead-code 消除，因此部分分割是纯粹冗余的 编译 成本。
    # 故意遗漏；如果 LTO 被禁用，请重新访问。

    # macOS SDK + GCC：<mach/port.h> 在 arm64 宏中使用
    # `_Static_assert`（C 关键字），该宏从任何 TU 拉入马赫 头文件 (e.g.
    # subprocess.cpp 经由 <spawn.h>)。将 C 拼写别名为其
    # C++ 等效项，因此 GCC 的 C++ 前端下的 SDK 头文件 解析。
    if(APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_definitions(${tgt} PRIVATE
            _Static_assert=static_assert)
    endif()
endif()
endfunction()

# ── 目录 -wide 消毒剂(仪器) EVERYTHING ,包括测试) ─────────
# 下面的目标-scoped AGENTTY_SANITIZE仅检测`agentty`
# exe。但是 Rust 倡导者指出的 内存-safety 错误（use-after-free、
# 缓冲区 溢出、UB）是由 TEST SUITE 执行的，其中 链接 是共享的 OBJECT 库，
# 而不是 exe。为了捕获它们，我们还需要对对象 库 和测试 二进制程序 进行消毒。
# AGENTTY_SANITIZE_ALL 通过注入 标志 DIRECTORY
# -WIDE 来实现这一点，这里，之前 定义了任何目标，因此每个 agentty
# TU（objlibs + exe + 测试）及其 链接 都携带它。
#
#   cmake -B build-asan -DAGENTTY_SANITIZE_ALL=address,undefined \
#         -DAGENTTY_BUILD_TESTS=ON && cmake --build build-asan --target tests \
#         && ctest --test-dir 构建-asan
#
# 这是agentty的Rust-grade 内存-safety GATE:
# 借用检查器证明在编译时间不存在这些错误；我们通过在 ASan+
# UBSan 下运行整个套件来证明这一点。不同的机制，对测试覆盖的路径
# 有相同的保证。
set(AGENTTY_SANITIZE_ALL "" CACHE STRING
    "Comma-separated sanitizers applied to ALL agentty TUs incl. tests (e.g. address,undefined). Empty to disable.")
if(AGENTTY_SANITIZE_ALL AND NOT MSVC)
    message(STATUS "agentty: WHOLE-TREE sanitizer -fsanitize=${AGENTTY_SANITIZE_ALL} "
                   "(objlibs + exe + tests)")
    add_compile_options(-fsanitize=${AGENTTY_SANITIZE_ALL}
                        -fno-omit-frame-pointer -g -O1 -fno-lto)
    add_link_options(-fsanitize=${AGENTTY_SANITIZE_ALL} -fno-lto)
    # GCC的消毒器对模块-level `constexpr std::array`
    # 目录 (spec.hpp kCatalog等)进行检测，并且该检测将中毒
    # 的指针算术泄漏到对它们进行遍历的static_assert PROOFS
    # 的`consteval`评估中——然后它们无法使用
    # "not a constant expression"来编译。这是 GCC 限制，而不是证明错误：
    # SAME证明编译并通过每个正常(非-sanitizer)构建，
    # 这是主门。因此，我们定义了AGENTTY_SANITIZER_BUILD，
    # 并让少数目录-walking证明块在消毒剂构建中跳过自己的ONLY
    # ——消毒剂的工作是检查RUNTIME 内存安全性（UAF /溢出/UB），
    # 而不是重新检查已经在其他地方运行绿色的-run 编译
    # -time证明。
    add_compile_definitions(AGENTTY_SANITIZER_BUILD=1)
endif()

# ── 共享 OBJECT 库 ─────────────────────────────────────────────
# 编译将每个共享的TU 恰好 ONCE转换为OBJECT 库，然后在每次测试
# 中重用主二进制程序 AND中的对象（通过$<TARGET_OBJECTS:...>）。
# 在此之前，大约 20 个测试目标中的每一个都将完整的提供程序+工具
# +运行时 源集重新编译到自己的对象目录中 - 干净的
# `--target tests` 重新构建了整个 代码库 大约 20 次。
# 对于 OBJECT 库，分担费用一次性支付；测试重建只是它自己的.
# cpp + 链接。
#
# `agentty_objlib(NAME src...)`定义了库，
# 将包含目录+项目编译 标志和链接连接到头
# 文件-providing 库作为INTERFACE deps，因此在编译期间
# 传递#includes解析（OBJECT 库不链接，但它们DO 需要
# 头文件)。 maya/json/simdjson/nghttp2/openssl
# 都通过导入的目标公开其包含目录。
# 预编译头文件。 IMPLEMENTED + 基准测试，DEFAULT OFF：在主要开发目标
# （8-core Apple Silicon、Apple clang）上共享 PCH
# MEASURED NET- NEGATIVE — 冷 objlib 编译 89 s → 96-98 s。
# libc++ 前缀扩展为 ~19 MB PCH，其 per-TU 加载 成本 + 串
# 行锚-compile + 失去的并行性超过了 解析 节省，甚至单个 -TU 增量重建也没有显示
# 出胜利（1.70 s → 1.77 s）。保留为 opt-in: CI 具有较少核心/冷
# I/O 或不同工具链的运行者仍然可能受益 - 使用 -DAGENTTY_PCH=ON 和 re
# -measure 进行翻转。 agentty_objlib 中的 REUSE_FROM 接线
# 无论哪种方式都是正确的。
option(AGENTTY_PCH "Share a precompiled STL/maya-core header prefix across the object libraries. Measured net-negative on 8-core Apple clang (see comment); opt-in for other toolchains/CI." OFF)

# Unity (巨型)构建。这个源码树是INSTANTIATION-bound（std::variant/
# std::visit超过maya的宽元素源码树）：相同
# 模板机制在每个编译单元中都会重新实例化。Unity 将多个编译单元合并，使这些
# 实例化在一个批处理内共享——MEASURED 3.2x在最大的objlib上冷（
# runtime_obj 50.5s→15.6s）。 DEFAULT OFF 因为它
# PENALISES 增量重建（编辑一个文件会重建整个批次），所以它适用于 COLD/CI/
# 新鲜的 -clone 构建，而不是开发编辑 -loop （为此使用 `dev` 预设）。
# 批量大小8平衡实例化共享与增量成本。
option(AGENTTY_UNITY_BUILD "Unity/jumbo build of the object libraries (big COLD-build win on this instantiation-bound tree; hurts incremental — for CI/cold only)." OFF)
set(AGENTTY_UNITY_BATCH 8 CACHE STRING "TUs per unity batch when AGENTTY_UNITY_BUILD=ON")

# ── 快速调试构建 ───────────────────────────────────────────
#
# 对于ONE-FILE改变，edit-build循环是36秒。在此 源码树（
# GCC 16、12 核）上测量，分为：
#
#   编译 一个 TU (meta.cpp) ~10s，其中 ~11s 是最大 编译单元 链接
#   agentty 上的 -g ~26s 一个 827 MB 二进制程序 到 ld.bfd
#
# 所以循环是LINK-bound，并且链接是巨大的，因为来自每个objlib的
# 完整DWARF都落入其中。两个变化，都是 Debug-only：
#
#   -g1——行表+函数边界，无local-variable DWARF。
#          在最大的 TU 上测得 23.7s -> 17.9s (-g0 是
#          12.8s，所以剩余调试成本的大约一半是给出回溯的部分，
#          这是值得保留的部分)。回溯、文件：行和断点仍然有效； gdb 中的
#          `print localvar` 没有。对于一个调试绝大多数是
#          "where did it crash" 的循环来说，这是正确的交易——
#          而 `--preset debug-full` 则在其他日子里存在。
#
# 快速 链接器 上的 NOTE：它们仅在这里工作，因为调试现在具有 LTO OFF (参见
# cmake/AgenttyStandalone.cmake)。虽然调试对象是 GCC
# SLIM LTO 字节码（"__gnu_lto_slim"，没有真正的 符号 表 -
# 只有 GNU ld 通过 GCC 插件读取这些），但 lld 因明显存在的函数而
# 出现大量伪造的 "undefined symbol" 错误而失败。这就是症状；
# LTO-on-a-debug-build 就是这种疾病。
#
# 释放未受影响：它保留完整的-O3+LTO+条带。
option(AGENTTY_FAST_DEBUG
       "Debug builds: lighter debug info (-g1) + a fast linker. Big edit-loop win; costs local-variable inspection in gdb."
       ON)

if(AGENTTY_FAST_DEBUG AND NOT MSVC AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    # CMAKE_CXX_FLAGS_DEBUG硬编码的-g1、REPLACES、
    # -g；追加将使两者都留在命令行上，最后一个获胜非-obviously。
    string(REGEX REPLACE "(^| )-g( |$)" "\\1-g1\\2"
           CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REGEX REPLACE "(^| )-g( |$)" "\\1-g1\\2"
           CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")

    # 拆分 DWARF：调试信息进入 per-object .dwo 文件而不是
    # 进入对象，因此 LINKER 永远不会读取它。
    #
    # 这是在这个源码树上留下的最大的单个edit-loop胜利，因为这里的调试
    # 循环是链接-bound，而不是编译-bound。测量，GCC 16 /
    # 12 核/模具，一个 -file 因重新配置而发生变化，排除：
    #
    #   -g1 编译 22ms 链接 10.5s 二进制程序 801 MB -g1
    #   -gsplit-dwarf 编译 22ms 链接 ~4s 二进制程序 428 MB
    #
    # 由于ccache，编译已经是22ms了；剩下的每一秒都是
    # 可执行文件在 链接 时间不需要的 DWARF 的模具行走
    # 801 MB。将图像减半，链接 减半。
    #
    # 成本，两者都很小：与对象一起的〜370个.dwo文件（它们
    # 存在于构建源码树中并随之消亡），并且gdb需要存在.
    # dwo来显示源代码行——确实如此，因为没有人运送或移动调试构建。
    # `-DAGENTTY_FAST_DEBUG=OFF` (`debug-full`预设)
    # 仍然为想要 self-contained 图像的任何人提供简单的 -g 以及内联的所有内容。
    #
    # 探测而不是假设：-gsplit-dwarf是GCC/
    # Clang-only，并且采用标志但不能发出.dwo的工具
    # 链将默默地生成没有调试信息的调试构建。
    include(CheckCXXCompilerFlag)
    set(CMAKE_REQUIRED_FLAGS -gsplit-dwarf)
    check_cxx_compiler_flag("" AGENTTY_HAS_SPLIT_DWARF)
    unset(CMAKE_REQUIRED_FLAGS)
    if(AGENTTY_HAS_SPLIT_DWARF)
        string(APPEND CMAKE_CXX_FLAGS_DEBUG " -gsplit-dwarf")
        string(APPEND CMAKE_C_FLAGS_DEBUG   " -gsplit-dwarf")
        message(STATUS "agentty: split DWARF on (debug info in .dwo; halves the link).")
    endif()

    # 更快的链接器，如果安装的话。 Mold Beats lld Beats
    # ld.bfd，两者都是 drop-in for ELF。由 ACTUALLY
    # LINKING 探测，而不是通过寻找 二进制程序：当前的
    # -but-unusable 链接器 会使整个构建失败，而此块的存在是为了使构建更快，
    # 而不是更脆弱。
    include(CheckCXXSourceCompiles)
    foreach(_ld mold lld)
        set(CMAKE_REQUIRED_LINK_OPTIONS -fuse-ld=${_ld})
        check_cxx_source_compiles("int main(){return 0;}" AGENTTY_HAS_LD_${_ld})
        unset(CMAKE_REQUIRED_LINK_OPTIONS)
        if(AGENTTY_HAS_LD_${_ld})
            add_link_options(-fuse-ld=${_ld})
            set(_agentty_fast_ld ${_ld})
            break()
        endif()
    endforeach()
    message(STATUS "agentty: fast debug — -g1, no LTO, "
                   "${_agentty_fast_ld} linker")
endif()

function(agentty_objlib name)
    add_library(${name} OBJECT ${ARGN})
    target_include_directories(${name} PRIVATE include)
    # 这里没有AGENTTY_VERSION：只有src/version.cpp看到它
    # （在下面设置），因此版本碰撞重新编译一个文件而不是每个TU。
    # 没有C++20模块扫描。这个 源码树 使用 头文件，而不是模块，所以扫描什么也没
    # 发现——但是 CMake 仍然将 `-fmodule-mapper=<...>.modmap`
    # 传递给每个 编译，并且 ccache 将其分类为 UNSUPPORTED
    # COMPILER OPTION 并拒绝 缓存 和 TU。测量结果：扫描时 100%
    # 无法缓存 调用，因此 `ccache` 已安装但不执行任何操作。
    #
    # maya已经出于同样的原因这样做了（参见third_party/maya/CMakeLists.txt，
    # 其也遇到了scan-vs-archive排序错误）。
    # 在此处将其关闭会使对象 库 可缓存，否则为 no-op。
    set_target_properties(${name} PROPERTIES CXX_SCAN_FOR_MODULES OFF)
    if(AGENTTY_UNITY_BUILD)
        set_target_properties(${name} PROPERTIES
            UNITY_BUILD ON
            UNITY_BUILD_BATCH_SIZE ${AGENTTY_UNITY_BATCH})
    endif()
    # 可选预编译头文件 (-DAGENTTY_PCH=ON)。
    # src/agentty_pch.hpp 中稳定的 STL 前缀在第一个
    # objlib 上编译一次，其余的则编译为 REUSE_FROM。保留 opt-in：
    # 优势取决于 编译器、机器和工作负载。
    if(AGENTTY_PCH)
        if(NOT DEFINED AGENTTY_PCH_ANCHOR)
            # 首先objlib拥有实际的PCH 编译；设置全局锚点。
            set(AGENTTY_PCH_ANCHOR ${name} PARENT_SCOPE)
            set(AGENTTY_PCH_ANCHOR ${name})   # 本地，用于下面的复用分支
            target_precompile_headers(${name} PRIVATE
                ${CMAKE_SOURCE_DIR}/src/agentty_pch.hpp)
        else()
            target_precompile_headers(${name} REUSE_FROM ${AGENTTY_PCH_ANCHOR})
        endif()
    endif()
    # MCP积分为编译-gated。当 ON 时，每个 objlib 都会看到宏（
    # registry.cpp 分支）和 mcp-cpp INTERFACE 包含目录（因此
    # agentty-facing 头文件 链解析）；只有 agentty_mcp_obj
    # 能够真正拉入重型 <mcp/*.hpp> 模板。
    if(AGENTTY_MCP)
        target_compile_definitions(${name} PRIVATE AGENTTY_MCP=1)
        if(TARGET mcp::mcp)
            target_link_libraries(${name} PRIVATE mcp::mcp)
        endif()
        # 该工具集由mcp-cpp工具集服务； fs_helpers.cpp（在
        # agentty_tool_obj 中）现在将 工作区 根镜像到 mcp 的 util 层，
        # 因此每个 objlib 都需要 mcp::tools' include dir 可解析。
        if(TARGET mcp::tools)
            target_link_libraries(${name} PRIVATE mcp::tools)
        endif()
    else()
        target_compile_definitions(${name} PRIVATE AGENTTY_MCP=0)
    endif()
    agentty_apply_compile_flags(${name})
    # 当分配器被启用时，mem.hpp包括<mimalloc.h>。
    # OBJECT 库 需要包含路径和 编译 定义，
    # 即使最终 链接 由可执行目标执行。
    if(AGENTTY_HAS_MIMALLOC)
        target_link_libraries(${name} PRIVATE mimalloc-static)
        target_compile_definitions(${name} PRIVATE AGENTTY_USE_MIMALLOC=1)
    endif()
    # RAG适配器(agentty_rag_obj)包括<rag/rag.hpp>；
    # 其他 objlib 只需要可解析的 头文件 路径即可传递包含。 链接 目标作为
    # INTERFACE dep 给出每个 objlib ragcpp 的包含目录。
    if(AGENTTY_HAS_RAGCPP)
        target_link_libraries(${name} PRIVATE ragcpp::ragcpp)
        target_compile_definitions(${name} PRIVATE AGENTTY_HAS_RAGCPP=1)
    else()
        target_compile_definitions(${name} PRIVATE AGENTTY_HAS_RAGCPP=0)
    endif()
    # claybin 沙箱后端。需要子模块，所以它无条件地链接，
    # 并且没有定义获得半-applied——这就是旧
    # AGENTTY_HAVE_CLAYBIN守卫所冒的风险：sandbox.hpp的后端
    # 枚举和 sandbox.cpp 的分派原先都由该开关控制，因此一个定义
    # 达到了一些 objlib 而不是其他人给出了关于 枚举 的两个
    # 编译单元 不同想法。 ODR 违规，而不是 编译 错误。
    target_link_libraries(${name} PRIVATE claybin)
    # richpaste:剪贴板读取器(io/clipboard.cpp、`agentty paste`)。
    target_link_libraries(${name} PRIVATE rp)
    # 头文件-only访问链接的库(OBJECT没有实际的链接)。
    target_link_libraries(${name} PRIVATE
        maya::app
        nlohmann_json::nlohmann_json
        simdjson::simdjson
        nghttp2::nghttp2
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads
    )
endfunction()

agentty_objlib(agentty_io_obj        ${AGENTTY_IO_SOURCES})
agentty_objlib(agentty_workspace_obj ${AGENTTY_WORKSPACE_SOURCES})
agentty_objlib(agentty_airgap_obj    ${AGENTTY_AIRGAP_SOURCES})
agentty_objlib(agentty_provider_obj  ${AGENTTY_PROVIDER_SOURCES})
agentty_objlib(agentty_diff_obj      ${AGENTTY_DIFF_SOURCES})
agentty_objlib(agentty_rag_obj       ${AGENTTY_RAG_SOURCES})
agentty_objlib(agentty_tool_obj      ${AGENTTY_TOOL_SOURCES})
agentty_objlib(agentty_runtime_obj   ${AGENTTY_RUNTIME_NOMAIN_SOURCES})
# ACP胶水仅由exe+acp_integration_test需要。
# 它还需要在标准集之上包含 acp::acp 包含目录 (acp/acp.hpp)。
agentty_objlib(agentty_acp_obj       ${AGENTTY_ACP_SOURCES})
target_link_libraries(agentty_acp_obj PRIVATE acp::acp)
# src/acp/server.cpp打开`namespace agentty::acp`
# (ACP SERVER)。与其兄弟姐妹（通过裸 `acp::` 使用 GLOBAL
# `::acp`）批量合并为统一 TU，该名称将重新绑定到 agentty::acp 并中断。
# 将此文件排除在统一批处理之外 - objlib 的其余部分仍然进行批处理。
if(AGENTTY_UNITY_BUILD)
    set_source_files_properties(src/acp/server.cpp PROPERTIES
        SKIP_UNITY_BUILD_INCLUSION ON)
endif()

# MCP胶水——仅当AGENTTY_MCP开启时才构建。限制重mcp-cpp
# template instantiation to these two TUs; everything else just sees the
# 灯agentty-facing 头文件。添加到下面的共享对象集中，以便 exe（
# 以及 调用 tools::registry() 的任何完整测试）链接 它。
if(AGENTTY_MCP)
    agentty_objlib(agentty_mcp_obj   ${AGENTTY_MCP_SOURCES})
    target_link_libraries(agentty_mcp_obj PRIVATE mcp::mcp)
    if(TARGET mcp::tools)
        target_link_libraries(agentty_mcp_obj PRIVATE mcp::tools)
    endif()
endif()

# 每个"full"测试（和exe)消耗的共享对象集——整个
# 运行时减去main.cpp并减去ACP胶水。
set(AGENTTY_SHARED_OBJECTS
    $<TARGET_OBJECTS:agentty_io_obj>
    $<TARGET_OBJECTS:agentty_workspace_obj>
    $<TARGET_OBJECTS:agentty_airgap_obj>
    $<TARGET_OBJECTS:agentty_provider_obj>
    $<TARGET_OBJECTS:agentty_diff_obj>
    $<TARGET_OBJECTS:agentty_rag_obj>
    $<TARGET_OBJECTS:agentty_tool_obj>
    $<TARGET_OBJECTS:agentty_runtime_obj>
)
if(AGENTTY_MCP)
    # registry.cpp(在agentty_tool_obj中)引用mcp::
    # mcp_tools，因此MCP粘合对象必须在共享集exe+每个完整测试链接中。
    list(APPEND AGENTTY_SHARED_OBJECTS $<TARGET_OBJECTS:agentty_mcp_obj>)
endif()

add_executable(agentty
    src/runtime/main.cpp
    src/runtime/run_cli.cpp
    ${AGENTTY_SHARED_OBJECTS}
    $<TARGET_OBJECTS:agentty_acp_obj>
)

target_include_directories(agentty PRIVATE include)
# 与agentty_objlib()的原因相同：模块扫描使得每个
# 编译无法被ccache缓存。 main.cpp 直接编译到该目标中，
# 因此它也需要该属性。
set_target_properties(agentty PROPERTIES CXX_SCAN_FOR_MODULES OFF)
# 该版本存在于一个TU(src/version.cpp，在agentty_io_obj)；
# 其他一切都读取::agentty::version()。碰撞项目（VERSION）重新编译它。
set_source_files_properties(src/version.cpp PROPERTIES
    COMPILE_DEFINITIONS AGENTTY_VERSION="${PROJECT_VERSION}")
if(AGENTTY_MCP)
    # main.cpp在AGENTTY_MCP（`mcp-serve`子命令）上分支。
    # objlibs 通过 agentty_objlib() 获取这个宏； main.cpp
    # 直接编译到 exe 目标中，因此也在这里定义它。
    target_compile_definitions(agentty PRIVATE AGENTTY_MCP=1)
else()
    target_compile_definitions(agentty PRIVATE AGENTTY_MCP=0)
endif()
target_link_libraries(agentty PRIVATE
    maya::app
    acp::acp
    nlohmann_json::nlohmann_json
    simdjson::simdjson
    nghttp2::nghttp2
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)
if(AGENTTY_MCP AND TARGET mcp::mcp)
    target_link_libraries(agentty PRIVATE mcp::mcp)
endif()
if(AGENTTY_MCP AND TARGET mcp::tools)
    target_link_libraries(agentty PRIVATE mcp::tools)
endif()
target_link_libraries(agentty PRIVATE claybin rp)
if(AGENTTY_HAS_MIMALLOC)
    # 当启用MI_OVERRIDE时，静态目标包含C
    # 分配和全局C++new/delete覆盖。
    target_link_libraries(agentty PRIVATE mimalloc-static)
    target_compile_definitions(agentty PRIVATE AGENTTY_USE_MIMALLOC=1)
    message(STATUS "agentty: mimalloc allocator override enabled.")
endif()
if(AGENTTY_HAS_RAGCPP)
    # RAG发动机。适配器 TU（在 agentty_rag_obj
    # 中）已在共享对象集中； exe 链接 库 所以它的 符号 解析。
    target_link_libraries(agentty PRIVATE ragcpp::ragcpp)
    message(STATUS "agentty: rag-cpp retrieval engine enabled (vendored).")
endif()
if(WIN32)
    # ws2_32 — Winsock2 (套接字、
    # WSAPoll) crypt32 — 用于 Windows 根证书加载器 shell32 的
    # CertOpenSystemStoreW — main.cpp 中的现有代码
    # 路径 winmm — timeBeginPeriod / timeEndPeriod 已经需要（
    #            `#pragma comment(lib, "winmm.lib")` 仅适用于 MSVC）。
    # user32——之前保留的； maya/jaal 控制台代码可以使用它 advapi32 —
    # CredWriteW/CredReadW/CredDeleteW (keystore.cpp)
    #            + SetNamedSecurityInfoW/SetEntriesInAclW/OpenProcessToken
    #            （凭据 文件上的所有者 -only DACL，
    # auth.cpp）（richpaste 自带剪贴板 库。）
    target_link_libraries(agentty PRIVATE
        ws2_32 crypt32 shell32 winmm
        user32 advapi32)
    # Windows上的静态OpenSSL另外拉入这些系统库(
    # 由libcrypto的CSP/证书API使用)。在动态构建中无害 - 链接
    # 器 重复数据删除 - 但当 AGENTTY_STANDALONE=ON 时需要。
    if(AGENTTY_STANDALONE)
        target_link_libraries(agentty PRIVATE bcrypt secur32 advapi32 user32)
    endif()
    # CRITICAL (MinGW/UCRT64)：归并 libstdc++、
    # libgcc 和 winpthread 运行时 INTO exe。
    # Windows 没有 ELF-style 符号 插入，因此动态-linked
    # libstdc++-6.dll 可以保留自己的 分配器 绑定。
    # 当 mimalloc 覆盖 exe 时，跨越 DLL 边界的 std::string
    # 或文件系统 缓冲区 可能会与 分配器 不匹配并损坏堆。静态运行时将进程
    # 折叠为一个 分配器 并删除 运行时 DLL 依赖项。
    if(NOT MSVC)
        target_link_options(agentty PRIVATE
            -static-libstdc++ -static-libgcc
            -Wl,-Bstatic,--whole-archive -lwinpthread
            -Wl,--no-whole-archive -Wl,-Bdynamic
            # MinGW的静态libstdc++.a附带定义legacy-ABI
            # logic_error/runtime_error复制因子的cow-stdexcept.o，
            # 其与已发出的内联SSO-ABI复制编译单元冲突。
            # 重复项为 ABI-compatible；让链接器保持第一。
            # 这是 MinGW 上记录的 -static-libstdc++ 解决方法。
            -Wl,--allow-multiple-definition)
    endif()
endif()
if(APPLE)
    # macOS根证书加载器使用安全和CoreFoundation框架。
    target_link_libraries(agentty PRIVATE
        "-framework Security"
        "-framework CoreFoundation")
endif()

# ── 独立 链接 旋钮( 每根) -platform) ───────────────────────────────
if(AGENTTY_STANDALONE AND NOT MSVC)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # 归并 libstdc++ 和 libgcc 到 二进制程序 中，
        # 因此它可以在具有不同发行版/ABI 版本的计算机上运行。 libc 保持动态 — 完全
        # -static glibc 二进制程序 破坏了NSS 解析器（DNS、/etc/
        # nsswitch）和运行时 处的getpwuid_r。对于 100%-static，
        # 选择 AGENTTY_FULLY_STATIC 并使用 musl 工具链进行构建。
        target_link_options(agentty PRIVATE
            -static-libstdc++ -static-libgcc)
        if(AGENTTY_FULLY_STATIC)
            # HOW WE LINK A BINARY 该 RUNS EVERYWHERE（以及
            # 没有的方法的墓地，全部在 Alpine 3.21 / GCC 14.2 上得到确认）：
            #
            #   * `-static-pie` (单独，或 + 编译 标志)：Alpine 的
            #     default-PIE musl GCC 不 从存档中提取 libc。
            #     输出是 ET_DYN，带有 `NEEDED libc.musl-*.so`，
            #     没有 PT_INTERP -> 在 Alpine 构建映像上运行，SIGSEGV 在
            #     glibc/Debian. 此 IS 该 v0.2.7 CRASH 上。 （在CI中确认两次：
            #     readelf -d显示NEEDED libc.musl-{x86_64,aarch64}.so。）
            #   * `-static-pie -static`:链接错误-`-static`拖入
            #     NON-pie CRT(crtbeginT.o)，其R_X86_64_32重新定位
            #     "can not be used when making a PIE object"(collect2:ld返回1)。
            #   * `-Wl,-Bstatic --no-dynamic-linker`:驱动程序re-emits
            #     -Bdynamic为其隐含的-lc; NEEDED 幸存下来。
            #
            # 执行 的工作原理是什么，防弹：普通 `-static -no-pie`。
            # 它从静态存档中获取 链接 libc，并发出经典的 ET_EXEC 和 NO
            # NEEDED 和 NO PT_INTERP - 一个真正独立的 二进制程序，
            # 运行在每个 Linux 用户区（glibc Debian/Ubuntu/Fedora、
            # musl Alpine、64-bit Raspberry Pi OS）。
            # 它唯一能在 不 上运行的是 Android/Bionic (Termux)，
            # 它拒绝 ET_EXEC 并需要 PIE - 一个诚实默认交易的利基市场，
            # 因此 `curl | sh` WORKS 为正常 Linux 上的 99%。
            # 当您专门针对 Termux 和 时，选择 PIE 变体 和 -DAGENTTY_STATIC_PIE=ON
            # 有一个 musl 工具链，其 -static-pie 真正静态地
            # 链接 libc（POST_BUILD 防护以任何方式强制执行）。
            if(AGENTTY_STATIC_PIE)
                target_compile_options(agentty PRIVATE -fPIE)
                target_link_options(agentty PRIVATE -static-pie)
            else()
                # Non-PIE在每个阶段都需要标志，并且它们是不同的标志:
                # -fno-pie使编译器发出非-PIC代码，
                # 并且-no-pie使链接器发出ET_EXEC(不是保留NEEDED的半-PIE)。
                # -no-pie 在 编译 时间什么也不做（clang 是这么说的：
                # -Wunused-command-line-argument）。
                target_compile_options(agentty PRIVATE -fno-pie)
                target_link_options(agentty PRIVATE -static -no-pie)
                # crtbeginT.o在main之前用libgcc注册每个FDE（〜0.5毫秒，
                # `--version`的一半）。无论如何，开卷机都会通过
                # PT_GNU_EH_FRAME 找到 FDE，因此将其设置为 no-op。
                target_sources(agentty PRIVATE src/runtime/static_eh.cpp)
                target_link_options(agentty PRIVATE -Wl,--eh-frame-hdr
                    -Wl,--defsym=__register_frame_info=agentty_skip_frame_registration
                    -Wl,--defsym=__deregister_frame_info=agentty_skip_frame_registration)
            endif()
            # musl的默认new-thread堆栈是128 KiB，而glibc的默认是8 MiB。
            # OpenSSL的SSL_connect(cert
            # -chain验证)加上nghttp2会话初始化加上
            # dial_tcp中的本地自动在长链上超出128KiB，
            # 在musl-static版本二进制程序(glibc
            # -dynamic上的预热线程中作为随机SIGSEGV浮现开发构建永远不会成功）。
            # PT_GNU_STACK p_memsz是musl读取的每个
            # pthread_create的大小——通过-z stack
            # -size碰撞它修复了分离的预热线程、grep工具的工作线程 池以及
            # 我们无法控制的静态-linkedthird-party库内创建的任何pthread。
            # 8 MiB 与 glibc 的默认值匹配，因此 musl
            # -static 和 glibc-dynamic 构建的行为相同。
            target_link_options(agentty PRIVATE -Wl,-z,stack-size=8388608)
        endif()
        # 静态 OpenSSL 为某些构建引入 libdl 和
        # libpthread (两者都已隐式) 以及 libz (压缩)。
        # 防御性添加libz；不需要时无害。
        find_library(ZLIB_STATIC NAMES libz.a z)
        if(ZLIB_STATIC)
            target_link_libraries(agentty PRIVATE ${ZLIB_STATIC})
        endif()
    elseif(APPLE)
        # macOS不允许真正的-static系统框架(libSystem
        # 要求是动态的)，但是third-party 库可以是静态的。
        # OPENSSL_USE_STATIC_LIBS已处理上述OpenSSL。
        #
        # -liconv:版本归并 GCC的libstdc++静态地，以及它的format.o
        # (带有时间或区域设置的std::format/std::print)调用
        # iconv_open/iconv/iconv_close. libSystem
        # 确实在macOS上导出NOT——它们存在于/usr/lib/libiconv.2.dylib中，
        # 存在于每个macOS——所以如果没有这个，链接就会失败（它破坏了
        # 0.9.20 mac版本）。当没有任何内容引用它时无害。
        target_link_libraries(agentty PRIVATE iconv)
    endif()
endif()

# 为完全-static Linux 二进制程序建造-time ELF-shape防护装置。
# 这是针对 v0.2.7 灾难的 最后 防线，与 CI readelf
# 检查不同，它在 每个 构建路径上运行，生成发货 二进制程序：
# 发布工作流程，本地`AGENTTY_FULLY_STATIC=ON`构建，
# AND安装程序的`--build`/auto-fallback源构建。
# 正确的静态-PIE是带有PT_PHDR的ET_DYN，没有PT_INTERP，
# 也没有NEEDED动态库。如果 musl/Alpine 驱动程序默默地将 链接 降级为
# 动态-PIE（确切的 v0.2.7 x86_64 失败：NEEDED libc.musl-*.
# so + glibc 上没有 INTERP → SIGSEGV），则此处的 BUILD 失败
# - 损坏的工件永远无法再次发布，无论哪个管道构建它。
if(AGENTTY_STANDALONE AND AGENTTY_FULLY_STATIC
        AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT MSVC)
    find_program(READELF_EXE NAMES readelf llvm-readelf)
    if(READELF_EXE)
        add_custom_command(TARGET agentty POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                -DBIN=$<TARGET_FILE:agentty>
                -DREADELF=${READELF_EXE}
                -P ${CMAKE_SOURCE_DIR}/cmake/assert_static_pie.cmake
            VERBATIM
            COMMENT "Verifying static-PIE ELF shape (no NEEDED / no INTERP)")
    else()
        message(WARNING
            "agentty: readelf not found — cannot verify the static-PIE ELF "
            "shape at build time. Install binutils so a downgraded "
            "dynamic-PIE link (the v0.2.7 crash) fails the build.")
    endif()
endif()

# 诊断：在配置时打印one-liner，以便贡献者知道他们正在构建什么。
# 帮助抓获"I forgot the flag"案。
if(AGENTTY_STANDALONE)
    message(STATUS "agentty: STANDALONE build — third-party deps statically linked.")
    if(AGENTTY_STANDALONE_OPENSSL_FALLBACK)
        message(WARNING
            "agentty: STANDALONE requested OpenSSL static libs (.a) but only the "
            "shared library was found — the binary will still depend on libssl/"
            "libcrypto at runtime. To get a fully standalone exe, install the "
            "static OpenSSL package (Alpine: openssl-libs-static; Arch AUR: "
            "openssl-static; Fedora: openssl-static; or build from source).")
    endif()
elseif(AGENTTY_STATIC_RUNTIME AND MSVC)
    message(STATUS "agentty: static MSVC runtime (/MT).")
endif()

# ── 共享 编译 标志 适用于 agentty ─────────────────────────
# (agentty_apply_compile_flags定义在顶部，在OBJECT 库之前。)
agentty_apply_compile_flags(agentty)

# agentty-exe-only MSVC 链接-time代码生成器(消耗/GL对象)。
if(MSVC)
    target_link_options(agentty PRIVATE
        $<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:
            /LTCG              # 链接-time codegen（使用 /GL .obj 文件）
            /OPT:REF           # 删除未引用的functions/data
            /OPT:ICF=3         # 归并相同的COMDAT（3次）
            /INCREMENTAL:NO    # 仅完整 链接 — 较小的 exe，更好的 IPO
            /DEBUG:NONE        # 版本中没有 PDB
        >
    )
endif()

# nghttp2从 vcpkg 上静态链接MSVC;没有NGHTTP2_STATICLIB
# 它的标题标记了每个符号__declspec（dll导入）。
# http.cpp过去也使用 /Zc:preprocessor- 进行编译，
# 但这与全局 /Zc:preprocessor (D9025）并把
# maya's DSL标头，需要符合遗留预处理器的要求。什么nghttp2.
# h实际上缺乏MSVC曾是ssize_t， 哪个http.cpp现在在包含之前定义。
# 应用目录-wide，因此I/O OBJECT 库的http.cpp也得到它。
if(MSVC)
    set_property(SOURCE src/io/http.cpp APPEND PROPERTY
        COMPILE_DEFINITIONS NGHTTP2_STATICLIB)
endif()
# Opt-in消毒剂型号：`cmake -B build-san -DAGENTTY_SANITIZE=address,undefined`。
# 与CMAKE_BUILD_TYPE分开，
# 因此CI/贡献者可以翻转它而无需重新定义整个工具链。
set(AGENTTY_SANITIZE "" CACHE STRING
    "Comma-separated sanitizers for agentty (e.g. address,undefined or thread). Empty to disable.")
if(AGENTTY_SANITIZE AND NOT MSVC)
    message(STATUS "agentty: building with -fsanitize=${AGENTTY_SANITIZE}")
    target_compile_options(agentty PRIVATE
        -fsanitize=${AGENTTY_SANITIZE} -fno-omit-frame-pointer -g)
    target_link_options(agentty PRIVATE -fsanitize=${AGENTTY_SANITIZE})
endif()

# ── 安全加固+释放条 ──────────────────────────────
# SHIPPING 二进制程序的免费防御-in-depth (SECURITY_AUDIT.md#5：
# 没有明确的ASLR/DEP/RELRO)。全零-behaviour-change：
#   任何帧上的-fstack-protector-strong金丝雀，
#   具有本地array/aggregate -D_FORTIFY_SOURCE=2 编译
#                              -time + 运行时对libc str/mem/printf
#   调用进行边界检查（需要优化构建）-fPIE / -pie位置-independent exe→.
#   text的完整ASLR -Wl，-z，relro，-z，现在GOT在启动后映射read
#   -only（完整RELRO）-Wl，-z，noexecstack
# non-executable堆栈标记在消毒剂下跳过（他们拥有运行时 +想要帧指针）和MSVC
# （有自己的/GS /DYNAMICBASE /NXCOMPAT 默认）。
# RELRO/noexecstack 为 ELF-only； 链接器没有-ops，它们在别处，但门要明确。
if(NOT MSVC AND NOT AGENTTY_SANITIZE)
    target_compile_options(agentty PRIVATE -fstack-protector-strong)
    # -fPIE仅适用于实际产生PIE的构建。默认的 full-static 版本是
    # -static -no-pie (ET_EXEC)，其中 -fPIE 将与 -no-pie 战斗；
    # 跳过那里。每个其他构建（动态或 opt-in AGENTTY_STATIC_PIE
    # Termux 变体）都会获得 -fPIE 以获得完整的 ASLR。
    if(NOT (AGENTTY_FULLY_STATIC AND NOT AGENTTY_STATIC_PIE))
        target_compile_options(agentty PRIVATE -fPIE)
    endif()
    # _FORTIFY_SOURCE需要-O1+；仅定义它来优化配置，以便调试构建
    # 不会针对 TU 发出 "requires optimization" 警告。
    #
    # 强化工具链规范或发行版打包程序标志 (e.g。通过CFLAGS注入的
    # Arch makepkg的`-D_FORTIFY_SOURCE=3`，
    # 或者预定义它而根本没有-D的强化GCC规范文件)已经可以定义宏。
    # 在触发 GCC 的 "`_FORTIFY_SOURCE' redefined"
    # 警告后添加我们自己的 -D。在配置时检测预定义（编译 探针，
    # 而不是 标志-string 扫描 - spec-file 强化
    # 不会注入可见的 标志）并产生：工具链的值是打包者的强化选择。
    # （PR#33，sail3r。）
    set(_agt_req_quiet_save ${CMAKE_REQUIRED_QUIET})
    set(CMAKE_REQUIRED_QUIET YES)
    check_cxx_source_compiles("
        #ifndef _FORTIFY_SOURCE
        #error _FORTIFY_SOURCE is not predefined by the toolchain
        #endif
        int main() { return 0; }
    " AGENTTY_FORTIFY_PREDEFINED)
    set(CMAKE_REQUIRED_QUIET ${_agt_req_quiet_save})
    unset(_agt_req_quiet_save)
    if(NOT AGENTTY_FORTIFY_PREDEFINED)
        target_compile_definitions(agentty PRIVATE
            $<$<NOT:$<CONFIG:Debug>>:_FORTIFY_SOURCE=2>)
    endif()
    # 完全-static构建已经携带其自己的PIE决策（-static-pie或-no-pie)；
    # 在这里添加一个裸的 -pie 就可以解决这个问题。仅对非 -static
    # 路径添加 -pie。
    if(UNIX AND NOT APPLE)
        # RELRO+BIND_NOW+non-exec堆栈是GNU-ld/lld(ELF)特征。
        # Apple ld64拒绝-z; MSVC 已在上面处理。
        if(NOT AGENTTY_FULLY_STATIC)
            target_link_options(agentty PRIVATE -pie)
        endif()
        target_link_options(agentty PRIVATE
            -Wl,-z,relro,-z,now
            -Wl,-z,noexecstack)
    elseif(NOT APPLE AND NOT AGENTTY_FULLY_STATIC)
        # 非-ELF、非-Apple UNIX。在 Apple 上，PIE 是 链接器 默认值，
        # 而裸露的 -pie 只会引发 clang 的 "argument unused during compilation" 警告，
        # 因此请跳过它。
        target_link_options(agentty PRIVATE -pie)
    endif()
endif()

# 剥离释放二进制程序。 LTO + -O3 已经发货了 ~14 MB，
# 其中包含正在运行的应用程序永远不需要的完整 符号 表； --strip-all 删除 .
# symtab / .debug 部分（通常 >50%）。
# 切勿剥离 Debug/RelWithDebInfo（您想要 符号）或消毒剂构建（剥离破坏
# 了 ASan 符号），并跳过 Apple（macOS+GCC 保持活动的 LTO LOAD
# 段剥离无法触及 - 请参阅上面的 IPO 注释；那里的释放路径通过单独的步骤剥离）和
# MSVC（PDB 是外部的，没有什么可以剥离的.exe）。
if(NOT MSVC AND NOT APPLE AND NOT AGENTTY_SANITIZE
        AND (CMAKE_BUILD_TYPE STREQUAL "Release"
          OR CMAKE_BUILD_TYPE STREQUAL "MinSizeRel"))
    target_link_options(agentty PRIVATE -Wl,--strip-all)
endif()

# Opt-in配置文件-guided优化(-DAGENTTY_PGO=generate|use)。
include(AgenttyPGO)

# ─────────────────────────────────────────────────────────────────────
# 测试/基准测试（通过 -DAGENTTY_BUILD_TESTS=ON 的 opt-in，默认 OFF）
# ─────────────────────────────────────────────────────────────────────
#
# 默认情况下关闭，因此普通 `cmake -B build` 仅继续
# 构建运输 二进制程序。在 perf 上迭代或运行 CI 时打开。
#
# 每个测试目标链接、完整、运行时+工具堆栈（无main.cpp)，
# 因此工作台正在执行与生产二进制程序将运行的相同代码路径—
# —而不是剥离的-down机架-in。
option(AGENTTY_BUILD_TESTS "Build the test + benchmark binaries." OFF)
if(AGENTTY_BUILD_TESTS)
    enable_testing()

    # macOS SDK + GCC：与上面的 `agentty` 目标别名（参见
    # APPLE+GNU 块）相同的来自 <mach/*.h> 的
    # `_Static_assert` arm64 宏也在 test/bench 编译单元
    # 中触发 — 它们 链接 是完整的 IO/tool/runtime 堆栈（
    # subprocess.cpp 拉动） <spawn.h> → 马赫 头文件)。
    # agentty-target 别名是 PRIVATE，因此它不会到达这些单独的可执行文件；
    # 在这里应用它的directory-wide，这样下面定义的每个目标都会继承它。
    # AppleClang 不需要它（无论如何也无法构建这个 源码树 — 它不会宣传 cxx_std_26）。
    if(APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_compile_definitions(_Static_assert=static_assert)
    endif()

    # ── 测试套房住在这里 cmake/AgenttyTests.cmake ────────────────────
    # 每个测试一个agentty_test()声明；测试 / tests_gating /
    # sanitizer_tests 聚合是 注册表 中的 派生（没有 hand-listed 并行列表）。
    # 参见cmake/AgenttyTestRegistry.cmake.
    include(AgenttyTests)

    # 测试套件是针对POSIX setenv/unsetenv编写的（它
    # 在每个测试中沙箱HOME和工作区）。 UCRT 缺少它们，
    # 因此在 Windows 上，tests/ 下的每个源强制 -includes
    # 一个垫片。源属性，因此它达到了每个测试目标，但它是定义的（合并的、
    # 独立的或原始的），而不是产品。
    if(WIN32)
        file(GLOB_RECURSE _agentty_test_srcs CONFIGURE_DEPENDS
             ${CMAKE_SOURCE_DIR}/tests/*.cpp)
        set_source_files_properties(${_agentty_test_srcs} PROPERTIES
            COMPILE_OPTIONS "-include;${CMAKE_SOURCE_DIR}/tests/win_posix_compat.hpp")
    endif()
endif()
