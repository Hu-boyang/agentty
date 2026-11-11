# AgenttyTestRegistry.cmake-每个测试一个声明，导出聚合。
#
# 替换旧的4-list系统（agentty_add_full_test+AGENTTY_MIGRATED_TESTS
# +hand-listed`tests`/`sanitizer_tests`聚合+显式
# agentty_tests源列表）。测试的名称现在写成 恰好 一次，在 agentty_test
# () 调用 中；每个 下游 集都是在最终确定时根据全局 注册表 计算得出的。
#
# ── API ────────────────────────────────────────────────────────────────────
#   agentty_test
#       (<name> [MODEsolidalone|standalone|
#       raw] # 默认综合 [SRCS <extra .cpp> ...] # 超出
#       tests/<name>.cpp 的额外 编译单元 [OBJS
#       <$<TARGET_OBJECTS:x>> ...] # 额外对象 库 (独立) [
#       LIBS <lib> ...] # 额外 链接 库（独立） [TIMEOUT
#       <seconds>] # 默认 60；仅限 ctest 条目 [LABELS <label> ...] #
#       `sanitizer`/`perf` 驱动器派生集 [UNIX_LIBS <lib> ...] # UNIX 上的额外
#       库 和 不 APPLE (e.g.util for openpty) [ENV
#       "K=V" ...] # ctest ENVIRONMENT 属性 [NO_TEST] # 构建但注册 NO ctest
#       条目(探针/捕获工具) [GATE <VAR>] # 仅当 ${VAR} 为真时定义 (e.g.AGENTTY_MCP)
#   )
#     - 合并→源附加到agentty_tests doctest 二进制程序（无exe）。
#     - 独立 → 拥有 EXCLUDE_FROM_ALL exe 与 完整 共享 -object 链接
#                      配方应用一次(forkers/PTY/fuzzers/benches/acp/mcp)。
#     -原始 → 调用者定义 add_executable + add_test 本身
#                      (narrow-source消毒剂测试必须NOT 链接完整共享集）；
#                      我们只记录在aggregates/labels.中
#
#   agentty_add_ctest(<name> COMMAND <argv...> [TIMEOUT n] [
#       LABELS ...]) # 运行带有参数（reveal_stream_gate Arms）的 EXISTING 目标的 ctest 条目。
#
#   agentty_finalize_tests
#       ()#从累积的合并源中定义agentty_tests，并#构建派生的
#       `tests`/`tests_gating`/`sanitizer_tests`目标。
#
# 注册表状态存在于DIRECTORY属性中（append-only)。

define_property(DIRECTORY PROPERTY AGENTTY_T_CONSOLIDATED_SRCS
    BRIEF_DOCS "sources folded into agentty_tests" FULL_DOCS "AgenttyTestRegistry")
define_property(DIRECTORY PROPERTY AGENTTY_T_STANDALONE
    BRIEF_DOCS "standalone test target names" FULL_DOCS "AgenttyTestRegistry")
define_property(DIRECTORY PROPERTY AGENTTY_T_SANITIZER
    BRIEF_DOCS "sanitizer-labelled test names" FULL_DOCS "AgenttyTestRegistry")
define_property(DIRECTORY PROPERTY AGENTTY_T_PERF
    BRIEF_DOCS "perf/probe/bench names (skippable on the fast gate)" FULL_DOCS "AgenttyTestRegistry")

# 完整技术栈的链接配置，适用于任何链接共享目标文件集合的目标。
#    对象集(独立测试+agentty_tests)。完全镜像原始
#    agentty_add_full_test 正文，因此代码生成为 字节-identical。
function(_agentty_test_link_full name)
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_definitions(${name} PRIVATE
        # 选择面板 row-style 约定保护的源路径：这些
        # 规则是 view-layer 样式常量，
        # 没有 运行时 接口边界 来断言，因此测试读取源。
        # 选择面板被分割成该目录下的几个文件；测试将它们全部连接起来。
        AGENTTY_PICKERS_SRC_DIR="${CMAKE_SOURCE_DIR}/src/runtime/view/panels"
        # 主题-discipline防护的源根：
        # "agentty has no UI of its own"是关于颜色可能是
        # SPELLED的规则，它也没有运行时 接口边界，因此测试也读取源码树。
        AGENTTY_SRC_ROOT="${CMAKE_SOURCE_DIR}"
        AGENTTY_MCP=1)
    target_link_libraries(${name} PRIVATE
        maya::maya
        nlohmann_json::nlohmann_json
        simdjson::simdjson
        nghttp2::nghttp2
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads)
    # 运行时现在是jaal程序：include/agentty/runtime/cmd.hpp拉动
    # <jaal/jaal.hpp>，并且渲染探针拉动<maya/app/inline.hpp>。
    # 两者都通过 maya::app（一个带有 jaal 的包含目录的 INTERFACE
    # 目标）到达，这就是 `agentty` exe 链接 的内容。
    # 测试仅链接maya::maya，因此每个接触运行时的TU都无法找到头文件。
    if(TARGET maya::app)
        target_link_libraries(${name} PRIVATE maya::app)
    endif()
    # libstdc++ 在实验存档中运送 std::print 的 Windows 终端 路径（
    # __open_terminal/__write_to_terminal），而不是 libstdc++ 本身。
    if(MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_link_libraries(${name} PRIVATE stdc++exp)
    endif()
    # registry.cpp通过mcp-cpp桥路由整个工具组。
    if(TARGET mcp::mcp)
        target_link_libraries(${name} PRIVATE mcp::mcp)
    endif()
    if(TARGET mcp::tools)
        target_link_libraries(${name} PRIVATE mcp::tools)
    endif()
    # claybin 沙箱后端。每个测试 二进制程序 都需要，
    # 而不仅仅是 沙箱 测试：共享对象集包括 sandbox_claybin.cpp，
    # 因此如果没有 库 任何测试，工具层将无法在 链接 时间解析
    # clay::* 的 链接。无条件，如 子模块。
    target_link_libraries(${name} PRIVATE claybin)
    # richpaste支持io/clipboard.cpp，其位于I/O objlib中。
    target_link_libraries(${name} PRIVATE rp)
    if(AGENTTY_HAS_RAGCPP)
        target_link_libraries(${name} PRIVATE ragcpp::ragcpp)
        target_compile_definitions(${name} PRIVATE AGENTTY_HAS_RAGCPP=1)
    else()
        target_compile_definitions(${name} PRIVATE AGENTTY_HAS_RAGCPP=0)
    endif()
    if(AGENTTY_HAS_MIMALLOC)
        target_link_libraries(${name} PRIVATE mimalloc-static)
        target_compile_definitions(${name} PRIVATE AGENTTY_USE_MIMALLOC=1)
    endif()
    if(WIN32)
        target_link_libraries(${name} PRIVATE
            ws2_32 crypt32 shell32 winmm user32 gdi32 gdiplus shlwapi)
    endif()
    if(APPLE)
        target_link_libraries(${name} PRIVATE
            "-framework Security" "-framework CoreFoundation")
    endif()
endfunction()

function(agentty_test name)
    cmake_parse_arguments(T
        "NO_TEST"
        "MODE;TIMEOUT;GATE"
        "SRCS;OBJS;LIBS;LABELS;UNIX_LIBS;ENV"
        ${ARGN})

    if(T_GATE AND NOT ${T_GATE})
        return()
    endif()
    if(NOT T_MODE)
        set(T_MODE consolidated)
    endif()

    set(_primary "${CMAKE_SOURCE_DIR}/tests/${name}.cpp")

    if(T_MODE STREQUAL "consolidated")
        set_property(DIRECTORY APPEND PROPERTY AGENTTY_T_CONSOLIDATED_SRCS
            "${_primary}" ${T_SRCS})
        return()
    endif()

    # 独立记录和原始记录都进入聚合+标签集。
    # NO_TEST工具（视觉转储、工作台）在ctest中不运行任何内容，
    # 因此CI没有理由链接它们；按名称构建它们。
    if(NOT T_NO_TEST)
        set_property(DIRECTORY APPEND PROPERTY AGENTTY_T_STANDALONE ${name})
    endif()
    if("sanitizer" IN_LIST T_LABELS)
        set_property(DIRECTORY APPEND PROPERTY AGENTTY_T_SANITIZER ${name})
    endif()
    if("perf" IN_LIST T_LABELS)
        set_property(DIRECTORY APPEND PROPERTY AGENTTY_T_PERF ${name})
    endif()

    if(T_MODE STREQUAL "raw")
        return()   # 呼叫者拥有add_executable+add_test
    endif()

    # ── 独立:完全共享-objectXX 链接 ─────────────────────────────────
    add_executable(${name} EXCLUDE_FROM_ALL
        ${_primary} ${T_SRCS} ${AGENTTY_SHARED_OBJECTS} ${T_OBJS})
    _agentty_test_link_full(${name})
    target_link_libraries(${name} PRIVATE ${T_LIBS})
    # 测试二进制程序从未被运送：没有LTO（更快+避免了GCC -flto=auto
    # init-order位agentty_tests的危险）。 LTO 仅保留在已发货
    # 的 `agentty` 上。
    set_target_properties(${name} PROPERTIES INTERPROCEDURAL_OPTIMIZATION OFF)
    # 并且没有模块扫描：它使得每个TU不能被ccache缓存（扫描添加-fmodule-mapper，
    # ccache拒绝）。参见 agentty_objlib()。
    set_target_properties(${name} PROPERTIES CXX_SCAN_FOR_MODULES OFF)
    if(T_UNIX_LIBS AND UNIX AND NOT APPLE)
        target_link_libraries(${name} PRIVATE ${T_UNIX_LIBS})
    endif()

    if(NOT T_NO_TEST)
        add_test(NAME ${name} COMMAND ${name})
        if(NOT T_TIMEOUT)
            set(T_TIMEOUT 60)
        endif()
        set_tests_properties(${name} PROPERTIES TIMEOUT ${T_TIMEOUT})
        if(T_LABELS)
            set_tests_properties(${name} PROPERTIES LABELS "${T_LABELS}")
        endif()
        if(T_ENV)
            set_tests_properties(${name} PROPERTIES ENVIRONMENT "${T_ENV}")
        endif()
    endif()
endfunction()

# 运行带参数的 EXISTING 目标的原始 ctest 条目（无新的 二进制程序）。
function(agentty_add_ctest name)
    cmake_parse_arguments(C "" "TIMEOUT" "COMMAND;LABELS" ${ARGN})
    add_test(NAME ${name} COMMAND ${C_COMMAND})
    if(NOT C_TIMEOUT)
        set(C_TIMEOUT 120)
    endif()
    set_tests_properties(${name} PROPERTIES TIMEOUT ${C_TIMEOUT})
    if(C_LABELS)
        set_tests_properties(${name} PROPERTIES LABELS "${C_LABELS}")
    endif()
endfunction()

# 将原始目标（由调用者定义）记录到消毒剂组中。
function(agentty_mark_sanitizer)
    foreach(name ${ARGN})
        set_property(DIRECTORY APPEND PROPERTY AGENTTY_T_SANITIZER ${name})
    endforeach()
endfunction()

# ── 折叠的独立 -test 二进制程序 ───────────────────────────────────────
# agentty_standalone_tests:一个 exe持有完整的-stack独立测试
# (forkers/PTY/fuzzers/e2e/benches)，其不能是共享进程中的doctest情况。
# 每个 ctest 条目仍然通过 argv 分派 (agentty_standalone_tests
# <name> [args]) 在其 自身 进程中运行 - 因此 fork/PTY/chdir
# 隔离被保留 - 但 ~16 个重 100 MB+ 链接 崩溃成一个。
#
#   agentty_fold_test(<name>[TIMEOUTn][LABELS..][ARGS][
# UNIX_LIBS..])记录测试； agentty_finalize_fold() 构建单个 二进制程序 +
# per-test ctest 条目。 归并 需要的额外 链接 libs/objs（mcp::mcp、
# acp::acp、agentty_acp_obj）在 AgenttyTests.cmake 中的 二进制程序 上添加一次。
define_property(DIRECTORY PROPERTY AGENTTY_FOLD_NAMES
    BRIEF_DOCS "folded standalone test names" FULL_DOCS "AgenttyTestRegistry")

function(agentty_fold_test name)
    cmake_parse_arguments(F "ARGS" "TIMEOUT;SKIP_CODE;FIXTURE" "LABELS;UNIX_LIBS" ${ARGN})
    set_property(DIRECTORY APPEND PROPERTY AGENTTY_FOLD_NAMES ${name})
    # 将这个TU的main()重命名为<name>_main，以便分派器可以调用它。
    set_source_files_properties(${CMAKE_SOURCE_DIR}/tests/${name}.cpp
        PROPERTIES COMPILE_DEFINITIONS "main=${name}_main")
    if(F_UNIX_LIBS)
        set_property(DIRECTORY APPEND PROPERTY AGENTTY_FOLD_UNIX_LIBS ${F_UNIX_LIBS})
    endif()
    # ctest 条目：通过共享测试程序，以独立进程运行本用例。
    if(NOT F_TIMEOUT)
        set(F_TIMEOUT 60)
    endif()
    # FIXTURE提供操作数的工作台需求，针对源码树解析，
    # 因此测试不依赖于ctest的工作目录。
    if(F_FIXTURE)
        add_test(NAME ${name}
                 COMMAND agentty_standalone_tests ${name}
                         ${CMAKE_SOURCE_DIR}/${F_FIXTURE})
        # 从源根运行：固定装置本身可以命名仓库-relative路径（
        # symbol_read_corpus的TSV），并且ctest的
        # 默认cwd是构建源码树，其中它们都没有解析。
        set_tests_properties(${name} PROPERTIES
                             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    else()
        add_test(NAME ${name} COMMAND agentty_standalone_tests ${name})
    endif()
    set_tests_properties(${name} PROPERTIES TIMEOUT ${F_TIMEOUT})
    if(F_ARGS AND NOT F_SKIP_CODE AND NOT F_FIXTURE)
        # ARGS标记采用OPTIONAL操作数（线程文件、
        # 语料库）并用它们手动运行的测试。 ctest 没有运行它，
        # 它会打印它的用法并退出 2. 这是 "nothing to do here"，
        # 不是失败 - 如果没有这个，测试将永远是红色的，
        # 每个人都会学会忽略它。当存在固定装置时，给它一个固定装置。
        set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 2)
    endif()
    if(F_SKIP_CODE)
        # 当其环境无法产生所需的几何图形时，测试可能会使用此退出代码退出
        # （e.g。作曲家拒绝换行的 PTY-driven 复制品）—
        # —ctest 报告 "Skipped"，而不是 FAILED。
        set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE ${F_SKIP_CODE})
    endif()
    if(F_LABELS)
        set_tests_properties(${name} PROPERTIES LABELS "${F_LABELS}")
        if("perf" IN_LIST F_LABELS)
            set_property(DIRECTORY APPEND PROPERTY AGENTTY_T_PERF ${name})
            # 针对阈值测量的wall-clock探测器不能与任何东西共享机器。
            # RESOURCE_LOCK 仅针对 EACH OTHER 序列化
            # 性能测试 - 其他约 1100 个测试仍在 -j12 并行运行，
            # 这就是 md_cache_probe 不稳定的原因：其
            # quote_fence_300 形状位于约 2.9 倍，
            # 相对于 3.0 倍的增长阈值，因此任何被盗的 CPU 或 内存 带宽
            # 都会将其翻倒，并且读取为损坏。 缓存 而不是一个忙碌的盒子。
            #
            # RUN_SERIAL是实际上意味着
            # "give this test the machine"的属性：ctest
            # 在运行中没有其他测试的情况下运行它。成本很小（性能标签总共约为 19 秒），
            # 而且它购买了一个失败的探针。
            set_tests_properties(${name} PROPERTIES RESOURCE_LOCK wallclock
                                                    RUN_SERIAL TRUE)
        endif()
    endif()
endfunction()

# 从所有记录的归并 编译单元+分派器中构建一个二进制程序。
# 额外的 libs/objs 由调用者传递（每个折叠测试需求的联合）。
function(agentty_finalize_fold)
    cmake_parse_arguments(FF "" "" "OBJS;LIBS" ${ARGN})
    get_property(_names DIRECTORY PROPERTY AGENTTY_FOLD_NAMES)
    set(_srcs ${CMAKE_SOURCE_DIR}/tests/agentty_standalone_tests_main.cpp)
    foreach(_n ${_names})
        list(APPEND _srcs ${CMAKE_SOURCE_DIR}/tests/${_n}.cpp)
    endforeach()
    add_executable(agentty_standalone_tests EXCLUDE_FROM_ALL
        ${_srcs} ${AGENTTY_SHARED_OBJECTS} ${FF_OBJS})
    _agentty_test_link_full(agentty_standalone_tests)
    target_link_libraries(agentty_standalone_tests PRIVATE ${FF_LIBS})
    get_property(_ulibs DIRECTORY PROPERTY AGENTTY_FOLD_UNIX_LIBS)
    if(_ulibs AND UNIX AND NOT APPLE)
        target_link_libraries(agentty_standalone_tests PRIVATE ${_ulibs})
    endif()
    # 测试二进制程序从不发货——没有LTO（更快+躲避GCC init-order危险，
    # 与agentty_tests的基本原理相同）。
    set_target_properties(agentty_standalone_tests PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION OFF)
    # 分派器#包括tests/agentty_standalone_tests.def.
    target_include_directories(agentty_standalone_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests)
    # 它算作独立聚合中的一个条目。
    set_property(DIRECTORY APPEND PROPERTY AGENTTY_T_STANDALONE agentty_standalone_tests)
endfunction()

function(agentty_finalize_tests)
    get_property(_srcs DIRECTORY PROPERTY AGENTTY_T_CONSOLIDATED_SRCS)
    add_executable(agentty_tests EXCLUDE_FROM_ALL
        ${CMAKE_SOURCE_DIR}/tests/test_main.cpp
        ${_srcs}
        ${AGENTTY_SHARED_OBJECTS}
        $<TARGET_OBJECTS:agentty_acp_obj>)   # acp_integration_test需要AgentServer
    _agentty_test_link_full(agentty_tests)
    # 无模块扫描：使得每个TU都无法被ccache缓存。这个 二进制程序 对
    # 编译单元 进行了约 90 次测试，因此这是命中率最重要的地方。
    set_target_properties(agentty_tests PROPERTIES CXX_SCAN_FOR_MODULES OFF)
    target_link_libraries(agentty_tests PRIVATE doctest::doctest)
    if(TARGET acp::acp)
        target_link_libraries(agentty_tests PRIVATE acp::acp)
    endif()
    # 测试二进制程序从未被运送，因此它们不需要LTO——并且GCC的-flto=auto
    # re-partitions整个共享对象集的动态-init序列，
    # 这暴露了潜在的static-init/codegen危险，即启动时CRASHED
    # agentty_tests（doctest_discover_tests运行它
    # POST_BUILD来枚举情况，因此启动崩溃破坏了整个构建 -
    # "links fine, dies at discovery"）。关闭 IPO 进行测试 二
    # 进制程序 会删除 LTO 触发器，将 LTO 保留在重要的位置（发货的 `agentty`），
    # 并更快地构建测试。
    set_target_properties(agentty_tests PROPERTIES INTERPROCEDURAL_OPTIMIZATION OFF)
    include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
    doctest_discover_tests(agentty_tests)

    # ── 衍生总量(计算,从未使用手-listed) ────────────────────
    get_property(_standalone DIRECTORY PROPERTY AGENTTY_T_STANDALONE)
    get_property(_sanitizer  DIRECTORY PROPERTY AGENTTY_T_SANITIZER)
    get_property(_perf       DIRECTORY PROPERTY AGENTTY_T_PERF)

    add_custom_target(tests DEPENDS agentty_tests ${_standalone})

    set(_gating ${_standalone})
    if(_perf)
        list(REMOVE_ITEM _gating ${_perf})
    endif()
    add_custom_target(tests_gating DEPENDS agentty_tests ${_gating})

    # sanitizer_tests故意使NOT依赖于agentty_tests。
    #
    # 每个`sanitizer`-labelled条目都是其自己的窄-source 二进制
    # 程序(concurrency_primitives、cred_crypt、
    # 密钥库、race_harness、real_subsystem_race)
    # ——它们精确地窄，因此它们保持为-clean，而没有链接 maya的un
    # -instrumented 渲染器。标签中没有一项测试来自 agentty_tests，
    # 因此根据它，ASan CI 通道 编译 和 链接 变成了 ~1.1 GB
    # doctest 二进制程序，带有仪器，然后从中什么也不运行。
    #
    # 如果 doctest 案件需要在消毒剂下运行，
    # 请为其指定狭窄的目标和 `sanitizer` 标签 —
    # 这就是这里的模式，也是阻止这条车道重建世界的原因。
    add_custom_target(sanitizer_tests DEPENDS ${_sanitizer})
endfunction()
