# AgenttySubmodules.cmake-依赖性获取阶段：子模块 auto-pull帮助器、
# maya/json/simdjson/doctest/mimalloc的FetchContent、
# acp/mcp/rag add_subdirectory()调用及其per-submodule 切换、
# 以及nghttp2导入的-target发现。作为一个连续块包含在内，因此顺序（切换
# 之前 add_subdirectory；取决于 之前 链接 的目标）被准确保留。
# include() 将 CMAKE_CURRENT_SOURCE_DIR 保持在顶层，
# 因此 add_subdirectory(third_party/maya) 等像以前一样解析。
#
# 每个 in-tree 子模块 都存在于 third_party/ 之下 (maya、mcp-cpp、
# acp-cpp、rag-cpp — 并且 maya 在 third_party/maya/third_party/jaal
# 携带 jaal)。下面的帮助程序采用 BARE 名称（它键入目标名称，并且 per
# -submodule 覆盖它）并解析 AGENTTY_THIRD_PARTY_DIR 下的结帐。
set(AGENTTY_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

# ── 子模块 自动 -pull ────────────────────────────────────────────────────
# 当AGENTTY_AUTO_PULL_SUBMODULES是ON时，
# 每个in-tree 子模块在其库目标编译之前同步到每个构建上的其跟踪分支的尖端。
# 默认情况下是 OFF — 正常构建绝不能获取或重置 子模块。 SAFE
# 无论如何：对于任何携带本地工作的 子模块 都会跳过拉动，因此编辑永远不会被破坏。
# 如果父级不是 git checkout （释放 tarballs
# / FetchContent 路径），则完全跳过。
#
# 调用 之后 add_subdirectory(<sub>)，因此库目标存在依赖。
#   agentty_pull_submodule_latest(<dir> <branch> <library-target>)
option(AGENTTY_AUTO_PULL_SUBMODULES
       "Sync every in-tree submodule to its tracking branch on every build" OFF)
# 返回-compat:旧的maya-only开关在OFF时仍然禁用maya的拉力。
option(AGENTTY_AUTO_PULL_MAYA "(deprecated alias) pull maya on every build" OFF)

function(agentty_pull_submodule_latest sub branch lib)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
        return()
    endif()
    find_package(Git QUIET)
    if(NOT GIT_FOUND)
        return()
    endif()
    set(_sd "${AGENTTY_THIRD_PARTY_DIR}/${sub}")
    # 警卫故意偏执，因此开发人员在 子模块 中的 LOCAL
    # 工作永远不会被复位 --hard 破坏。如果其中 任意 成立，
    # 我们拒绝拉动：
    #   • 未暂存的更改 (git diff --quiet)
    #   • staged-but-uncommitted 工作（git diff --cached --quiet）
    #   ·本地提交不在源（获取后HEAD！=origin/<branch>，
    #     本地提交只能从 HEAD 到达）只有
    # 位于原点（或后面）的完全干净的 源码树 才会被快进。
    # clean-tree 检查后的 `git merge --ff-only`
    # 正是这样做的：它前进到最新的提交，但 ERRORS OUT
    # （无害的是，|| 分支吞掉了它）而不是丢弃本地历史记录。
    #
    # 该目标是`all`的NEVER部分。它仅在您请求时运行，无论是通过聚合
    # `submodules_sync` 目标（见下文），还是当 AGENTTY_AUTO_PULL_SUBMODULES
    # =ON 时，作为 ${lib} 的 per-build 依赖项。
    add_custom_target(${sub}_pull_latest
        COMMAND ${CMAKE_COMMAND} -E echo "[${sub}] checking origin/${branch}…"
        COMMAND ${GIT_EXECUTABLE} -C ${_sd} diff --quiet
            && ${GIT_EXECUTABLE} -C ${_sd} diff --cached --quiet
            && ${GIT_EXECUTABLE} -C ${_sd} fetch --quiet origin ${branch}
            && ${GIT_EXECUTABLE} -C ${_sd} merge --ff-only --quiet origin/${branch}
            || ${CMAKE_COMMAND} -E echo
               "[${sub}] local changes/commits present — keeping your tree, skipping auto-pull"
        COMMENT "Syncing ${sub}/ to origin/${branch} (safe: never discards local work)"
        VERBATIM
        USES_TERMINAL
    )
    # -demand同步上的聚合：`cmake --build <dir> --target submodules_sync`
    # (或`ninja submodules_sync`)立即拉动EVERY 子模块。
    # 在第一个 子模块 上延迟创建，因此我们不需要了解完整的设置。
    if(NOT TARGET submodules_sync)
        add_custom_target(submodules_sync
            COMMENT "Fast-forwarding all in-tree submodules to their tracking branches")
    endif()
    add_dependencies(submodules_sync ${sub}_pull_latest)
    # 每次构建可主动选择自动拉取。
    # 默认情况下 OFF：正常构建绝不能获取或重置 子模块（这会多次干扰
    # 头文件 并不必要地重建包含它的 ~64 编译单元）。当 ON 时，
    # 拉取在 库 编译 之前运行，因此新的 -pulled 源会选取相同的版本。
    # rag-cpp另外遵守其自己的AGENTTY_AUTO_PULL_RAGCPP覆盖。
    set(_auto_pull ${AGENTTY_AUTO_PULL_SUBMODULES})
    if(sub STREQUAL "rag-cpp" AND AGENTTY_AUTO_PULL_RAGCPP)
        set(_auto_pull ON)
    endif()
    if(sub STREQUAL "maya" AND AGENTTY_AUTO_PULL_MAYA)
        set(_auto_pull ON)
    endif()
    if(_auto_pull AND TARGET ${lib})
        add_dependencies(${lib} ${sub}_pull_latest)
    endif()
endfunction()

# 优选 in-tree 子模块（当存在时）；只能回落去取。
if(EXISTS "${AGENTTY_THIRD_PARTY_DIR}/maya/CMakeLists.txt")
    add_subdirectory(third_party/maya)
    # 无条件调用，使maya加入on-demand `submodules_sync`目标；
    # per-build auto-pull 在函数内部保持门控状态。
    agentty_pull_submodule_latest(maya master maya)
else()
    FetchContent_Declare(
        maya
        GIT_REPOSITORY https://github.com/agentty-org/maya.git
        GIT_TAG        master
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(maya)
endif()

set(JSON_BuildTests OFF CACHE INTERNAL "")
# FIND_PACKAGE_ARGS使得MakeAvailable尝试find_package()
# FIRST并且仅在系统没有任何东西时克隆。这对于 DISTRO PACKAGING 很重要：
# Termux、Alpine、Debian等人在沙箱中构建，
# 其中configure-time `git clone`要么被
# 彻底阻止，要么成为拒绝配方的理由。这样，安装开发包就可以满足
# 依赖关系，并且不会获取任何内容。
# 既没有安装也没有安装的开发者可以像以前一样获得固定的克隆。
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
    FIND_PACKAGE_ARGS NAMES nlohmann_json
)

# simdjson —用在 SSE 高频路径 (content_block_delta) 上，
# 其中我们在 流式传输 期间每秒 解析 数百个小 JSON 文档。恩洛曼很好
# for config + cold events; simdjson's ondemand API is 3–5× faster for the
# "open doc, read two fields, throw away"模式在这里占主导地位。
set(SIMDJSON_DEVELOPER_MODE   OFF CACHE INTERNAL "")
# STANDALONE构建不得采用系统副本。
#
# FIND_PACKAGE_ARGS 是让发行版包在没有克隆的情况下满足这一点的东西
# ——对于发行版构建来说是正确的，对于发布 二进制程序 来说是错误的，其整个合同（
# AgenttyStandalone.cmake:
# 不依赖第三方共享库。
# 是它在没有安装这些的机器上运行。 macOS 运行者发布了 Homebrew simdjson，
# 因此发布作业悄悄地链接了 /usr/local/opt/simdjson/lib/libsimdjson.33.dylib
# 并未能通过其自身的可移植性检查 - 在两个拱门上连续发布了三个版本，
# 而 Linux 和 Windows 克隆并静态构建了它并通过了。
#
# 该作业中的其他所有内容都已手动强制静态（openssl、
# 来自源的 nghttp2、通过 链接器 标志 的
# libstdc++/libgcc/libquadmath）。这关闭了仍然
# 可以动态到达的一个依赖关系，对于每个平台而不仅仅是 macOS：在安装了开发
# 包的机器上运行独立构建的任何地方都存在相同的陷阱。
#
# find_package路径特意将版本设为UNPINNED。请求 "3.10"
# 会使 CMake 拒绝系统 simdjson 4.x，因为系统不兼容（相同的
# -major 规则）并进行克隆 — 这违背了任何发行版 4.x 的要点，
# 包括 Termux。 agentty 仅使用稳定的 dom/ondemand
# 表面（解析器、文档、元素、对象、数组、padded_string），
# 在 3 和 4 中保持不变，因此无论发行版发布什么都可以。 GIT_TAG
# 仍然为任何人固定版本 无需 系统副本。
set(_agentty_simdjson_find_args FIND_PACKAGE_ARGS NAMES simdjson)
if(AGENTTY_STANDALONE)
    set(_agentty_simdjson_find_args "")
endif()
FetchContent_Declare(
    simdjson
    GIT_REPOSITORY https://github.com/simdjson/simdjson.git
    GIT_TAG        v3.10.1
    GIT_SHALLOW    TRUE
    ${_agentty_simdjson_find_args}
)

FetchContent_MakeAvailable(nlohmann_json simdjson)

# doctest —支持 single-binary 测试模型的unit-test框架。
# 选择主流C++框架中开销最低的编译，其断言宏已命名为CHECK（与每个
# 仓库的hand-rolled线束相匹配），因此迁移是机械的。
# 每个测试 TU 都会变成 TEST_CASE auto-registered
# 到 一个 `agentty_tests` 可执行文件中，链接 共享对象
# 设置一次 - 而不是每个 re-linking 大约 70 个可执行文件
# （链接 fan-out 是主要的 CI 成本）。仅在构建测试时获取。
if(AGENTTY_BUILD_TESTS)
    FetchContent_Declare(
        doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG        v2.4.11
        GIT_SHALLOW    TRUE
    )
    set(DOCTEST_WITH_TESTS OFF CACHE INTERNAL "")
    set(DOCTEST_NO_INSTALL ON CACHE INTERNAL "")
    # doctest 2.4.11 声明 cmake_minimum_required(
    # VERSION 3.0)，CMake 4.x 拒绝。仅针对此依赖项使用 3.10 策略基线，
    # 以避免当前 CMake 上出现旧版-compatibility 弃用警告。
    block(SCOPE_FOR VARIABLES)
        if(NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM
           OR CMAKE_POLICY_VERSION_MINIMUM VERSION_LESS "3.10")
            set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
        endif()
        FetchContent_MakeAvailable(doctest)
    endblock()
endif()

# 标记third-party 头文件 SYSTEM，这样他们的警告就不会污染我们的构建。
# nlohmann_json v3.11.3 的 binary_writer.hpp 使用 std::is_trivial，
# 而 GCC 15 在 C++26 下已弃用 - 上游 修复位于 master 上但未发布，
# 并且我们不希望每个包含 <nlohmann/json.hpp> 的 TU 都重新-emit 警告。
if(TARGET nlohmann_json)
    get_target_property(_nj_iface nlohmann_json INTERFACE_INCLUDE_DIRECTORIES)
    if(_nj_iface)
        set_target_properties(nlohmann_json PROPERTIES
            INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_nj_iface}")
    endif()
endif()
if(TARGET simdjson)
    get_target_property(_sj_iface simdjson INTERFACE_INCLUDE_DIRECTORIES)
    if(_sj_iface)
        set_target_properties(simdjson PROPERTIES
            INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_sj_iface}")
    endif()
endif()

# jsonrpc-cpp —JSON-RPC 2.0作为被动，头文件-only 库 (子模块):
# 一个引擎和编解码器代数mcp-cpp和acp-cpp共享。首先添加，
# 以便发现 jsonrpc::jsonrpc 已定义。没有任何类型的 运行时；
# 参见 docs/PROTOCOL_LIBRARIES.md.
set(JSONRPC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/jsonrpc-cpp)

# acp-cpp —头文件-only代理客户端协议库 (子模块)。
# 为 `agentty acp` 提供 传输 代数 + JSON-RPC 引擎 + stdio 传输层。
# Tests/examples关闭；它重用上面已经填充的 nlohmann_json 目标（当
# 已经存在时，它自己的 FetchContent_MakeAvailable 是 no-op）。
set(ACP_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(ACP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/acp-cpp)
agentty_pull_submodule_latest(acp-cpp main acp)
if(TARGET acp)
    set_target_properties(acp PROPERTIES SYSTEM TRUE)
endif()

# mcp-cpp-Model上下文协议库 (子模块)，现在是agentty本地工具实现的SOLE源。
# agentty 的整个工具集（read/write/edit/list_dir、bash、
# grep/glob/find_definition、诊断、git_*、web_* 以及
# 主机-coupled remember/forget/wipe/todo/skill/
# search_docs/task SHELLS）由 mcp-cpp 的电池-included
# 工具集和 re-wrapped 到 src/tool/mcp_tools_bridge.cpp.
# 提供服务，因此这是 HARD 构建要求 - 没有本机回退到的刀具路径。
option(AGENTTY_MCP "Build MCP integration (REQUIRED — agentty's tools live in mcp-cpp)" ON)
if(NOT AGENTTY_MCP)
    message(FATAL_ERROR
        "agentty: AGENTTY_MCP=OFF is no longer supported — the tool set is "
        "served exclusively by the mcp-cpp toolset. Configure with "
        "-DAGENTTY_MCP=ON (the default).")
endif()
if(NOT EXISTS "${AGENTTY_THIRD_PARTY_DIR}/mcp-cpp/CMakeLists.txt")
    message(FATAL_ERROR
        "agentty: the mcp-cpp submodule is missing but it now provides the "
        "entire tool set. Run `git submodule update --init --recursive`.")
endif()
# mcp-cpp 的 EXAMPLE 服务器一直运行（mcp_bridge_test 的真正生成
# +握手 e2e 需要它），但其 TEST 二进制程序 不执行：每个 仓库 运行自己的测试。
# mcp-cpp自己的CI访问其fs/search/web/toolset/codec/protocol套件。 agentty
# 这里只需要 mcp LIBRARY，而不需要 agentty 的 ctest 运行中的 mcp_tests。
set(MCP_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(MCP_BUILD_EXAMPLES ON  CACHE BOOL "" FORCE)
set(MCP_BUILD_CLIENT_EXAMPLE OFF CACHE BOOL "" FORCE)   # 仅使用服务器
add_subdirectory(third_party/mcp-cpp)
agentty_pull_submodule_latest(mcp-cpp master mcp)
if(TARGET mcp)
    set_target_properties(mcp PROPERTIES SYSTEM TRUE)
endif()

if(NOT TARGET maya::maya)
    add_library(maya::maya ALIAS maya)
endif()

# ── claybin - 进去 -process 沙箱 后端 ───────────────────
#
# REQUIRED 子模块，与 maya、acp-cpp、mcp-cpp 和
# rag-cpp 处于相同地位：始终存在、始终编译、无特征 切换。
#
# 它曾经是一个选项（AGENTTY_CLAYBIN，有一段时间默认为OFF），
# 这是一个值得命名的错误。可选后端是不存在的后端：关闭标志后，
# 整个 claybin 调度路径在人们实际运行的二进制文件中是死代码，
# 沙箱窗格呈现 "build with -DAGENTTY_CLAYBIN=ON"，
# 它保存的策略没有引擎来强制执行，并且 --sandbox-backend=claybin
# 被拒绝。对同一问题的两个compile-time答案也意味着要测试两个配置，
# 如果定义到达了某些objlib而不是其他
# objlib，则存在ODR危险。
#
# 所以它不再是build-time选择。 对应的 后端限制命令是
# RUNTIME 选择 - 沙箱 面板 或 --sandbox-backend
# - 并且两个后端始终可供选择。 bwrap 仍然是默认值，
# 因为它是用户已经拥有的并且经过了十年的强化；
# claybin 距酒店仅一步之遥。
#
# 它编译无处不在（其计划编译器是可移植的并且可以在任何主机上进行测试）。 APPLY
# 步骤以前是Linux-only；现在是 Linux 和 macOS -- 那里
# 是 namespaces/landlock/seccomp/cgroup2，
# 这里是安全带 + rlimits -- claybin 自己的 CMakeLists
# 通过有条件地添加 src/linux/* 和 src/macos/spawn.cpp
# 来处理。 Windows 仍保留 编译 政策并拒绝应用。所以这里也没有站台守卫；
# 保护它只会在不同的轴上重新引入two-configurations问题。
if(EXISTS "${AGENTTY_THIRD_PARTY_DIR}/claybin/CMakeLists.txt")
    # claybin自己的套件是完整的并且在其自己的CI中运行； agentty 需要 库，
    # 而不是在其 ctest 运行中另外 20 个测试 二进制程序。
    set(CLAYBIN_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/claybin)
    agentty_pull_submodule_latest(claybin master claybin)
    if(TARGET claybin)
        set_target_properties(claybin PROPERTIES SYSTEM TRUE)
    endif()
else()
    message(FATAL_ERROR
        "agentty: third_party/claybin/ submodule is empty. Run "
        "`git submodule update --init --recursive` to vendor the sandbox "
        "backend, then reconfigure.")
endif()

# richpaste:读取Ctrl+V(Wayland、X11、macOS、Windows、
# ssh-forwarded代理或终端)的剪贴板并返回`agentty paste`。
if(EXISTS "${AGENTTY_THIRD_PARTY_DIR}/richpaste/CMakeLists.txt")
    set(RICHPASTE_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/richpaste)
    agentty_pull_submodule_latest(richpaste master rp)
    set_target_properties(rp PROPERTIES SYSTEM TRUE)
else()
    message(FATAL_ERROR
        "agentty: third_party/richpaste/ submodule is empty. Run "
        "`git submodule update --init --recursive`, then reconfigure.")
endif()

# 将 maya 的 头文件 视为系统，以便其警告不会出现在 agentty 版本中。
set_target_properties(maya PROPERTIES SYSTEM TRUE)

# 对 simdjson 的 头文件 的处理相同 — 上游 的 `operator "" _padded`
# 在 C++23 下跳闸 -Wdeprecated-literal-operator，
# 并且这不是我们要修复的错误。 SYSTEM 抑制来自所包含的 头文件 的诊断。
if(TARGET simdjson)
    set_target_properties(simdjson PROPERTIES SYSTEM TRUE)
endif()
if(TARGET simdjson_static)
    set_target_properties(simdjson_static PROPERTIES SYSTEM TRUE)
endif()

find_package(Threads REQUIRED)

# OpenSSL:如果AGENTTY_STANDALONE请求
# 静态并且静态存档丢失，则重试与共享库和标志以及回退，
# 因此用户在配置结束时看到清晰的注释。
if(AGENTTY_STANDALONE)
    find_package(OpenSSL QUIET)
    if(NOT OpenSSL_FOUND)
        unset(OPENSSL_USE_STATIC_LIBS)
        unset(OPENSSL_LIBRARIES CACHE)
        unset(OPENSSL_CRYPTO_LIBRARY CACHE)
        unset(OPENSSL_SSL_LIBRARY CACHE)
        find_package(OpenSSL REQUIRED)
        set(AGENTTY_STANDALONE_OPENSSL_FALLBACK TRUE)
    endif()
else()
    find_package(OpenSSL REQUIRED)
endif()

# nghttp2-in-house http客户端的HTTP/2协议引擎。优先选择 上游 CMake
# 配置（vcpkg / Homebrew / nghttp2 自己的导出）；回退到 pkg-config
# （Linux 发行版），然后进行手动 find_path/find_library 扫描。
find_package(nghttp2 CONFIG QUIET)
# 跳过pkg-config进行完全-static构建：pkg-config默认返回动态库路径
# (libnghttp2.so)，下面的`-static` 链接不能接受(
# "attempted static link of dynamic object")。（试图静态链接动态目标文件。）
# 进一步向下的手动 find_library 路径（在 AGENTTY_STANDALONE 上门
# 控）明确更喜欢 libnghttp2.a，因此让它接管。
if(NOT TARGET nghttp2::nghttp2 AND NOT AGENTTY_FULLY_STATIC)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(NGHTTP2 IMPORTED_TARGET libnghttp2)
        if(TARGET PkgConfig::NGHTTP2)
            add_library(nghttp2::nghttp2 ALIAS PkgConfig::NGHTTP2)
        endif()
    endif()
endif()
# mimalloc —微软的通用-purpose 分配器。固定到 STABLE
# 发布标签（如下），由 CMake 获取并从其 C 源代码编译为静态 库。
# 这不是 头文件-only。
#
# DISABLED ON APPLE (macOS):mimalloc对macOS的
# malloc覆盖通过插入系统malloc ZONE来工作。在 macOS 26 /
# Apple clang 21 上，这被破坏了 - 启动发出
# `mimalloc: warning: unable to allocate aligned OS memory directly`，
# 然后进程因 `pointer being freed was not allocated`
# 中止：一些 分配 穿过 mimalloc，而匹配的运算符删除到 SYSTEM 分配器
# 的路由，从而损坏堆。它在正常的发布版本上复制了 25/25（甚至在固定到 v3.4.5
# 版本后也复制了 25/25，所以它不是 的 HEAD-only 回归 - 区域
# 插入本身在这里无法使用），并在启动时在 ASan 下被精确定位到 fs::path::operator
# /= 中 auth.cpp 内的 std::string 增长。
# 禁用 mimalloc 修复了 25/25. 分配器 只通过 mi_collect() 为
# 我们购买了页面回收；在 Apple 上，我们回退到系统 分配器（无论如何，
# release_to_kernel() 是 macOS 上的 no-op — 请参阅 mem.hpp）。
#
# 标签被固定而不是跟踪 `main`/HEAD，因此随机 上游
# 提交不能破坏启用 mimalloc IS 的平台上的每个构建。
set(AGENTTY_MIMALLOC_TAG "v3.4.5" CACHE STRING
    "mimalloc git tag to fetch. Pin to a stable release; do NOT track main/HEAD.")
if(APPLE AND AGENTTY_USE_MIMALLOC)
    message(STATUS "agentty: mimalloc DISABLED on Apple — its macOS malloc-zone "
                   "override corrupts the heap; using the system allocator.")
    set(AGENTTY_USE_MIMALLOC OFF CACHE BOOL "" FORCE)
endif()
# 消毒剂自带分配器。在 TSan 下，mimalloc 的 malloc 覆盖在启动
# 时崩溃（在 tsan 的 pthread hooks 存在之前，tsan 的 dlsym
# malloc 进入 mimalloc），因此请将其排除在清理程序构建之外。
if((AGENTTY_SANITIZE_ALL OR AGENTTY_SANITIZE) AND AGENTTY_USE_MIMALLOC)
    message(STATUS "agentty: mimalloc DISABLED under sanitizers.")
    set(AGENTTY_USE_MIMALLOC OFF CACHE BOOL "" FORCE)
endif()
set(AGENTTY_HAS_MIMALLOC FALSE)
if(AGENTTY_USE_MIMALLOC)
    set(MI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(MI_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(MI_BUILD_OBJECT OFF CACHE BOOL "" FORCE)
    set(MI_BUILD_TESTS  OFF CACHE BOOL "" FORCE)
    set(MI_OVERRIDE     ON CACHE BOOL "" FORCE)
    # 独立构建目标广泛的CPU兼容性；防止 mimalloc 在
    # arm64上选择armv8.1-a基线。
    if(AGENTTY_STANDALONE)
        set(MI_NO_OPT_ARCH ON CACHE BOOL "" FORCE)
    endif()
    # 我们固定到标签，因此它永远不需要re-fetching；在每个配置上保留
    # FetchContent 和 re-running git 更新步骤。
    set(FETCHCONTENT_UPDATES_DISCONNECTED_MIMALLOC ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
        mimalloc
        GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
        GIT_TAG        ${AGENTTY_MIMALLOC_TAG}
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE
    )
    FetchContent_MakeAvailable(mimalloc)
    set(AGENTTY_HAS_MIMALLOC TRUE)
endif()

# ── rag-cpp : 检索( RAG ( 发动机) ────────────────────────────────────
# agentty的检索由外部rag-cpp 库 (子模块 third_party/rag-cpp/，
# 目标ragcpp::ragcpp)提供动力——生产-grade混合引擎(上下文分块、
# BM25 + dense/HNSW、RRF融合、CRAG、HyDE、
# MMR/飞镖板重新排序，GraphRAG，.ragdb 持久化）。
# src/rag/adapter.cpp 中的瘦适配器将 agentty 的检索边界映射到
# rag::Engine，因此应用程序的其余部分永远不会看到 rag:: 类型。从 -tree
# 来源构建，因此 agentty 带有 RAG 引擎和 编译 ANYWHERE。
set(AGENTTY_HAS_RAGCPP FALSE)
if(EXISTS "${AGENTTY_THIRD_PARTY_DIR}/rag-cpp/CMakeLists.txt")
    # rag-cpp可跨GCC、Clang、MinGW和MSVC移植。其耐用层在 Windows
    # 上使用 posix_compat.hpp，而 SIMD/prefetch 内核使用 编译器
    # -specific 包装器并保留 scalar/runtime-dispatched 回退。
    # 在官方 MSVC 版本中保持检索启用：平台包不得默默地用 no-op
    # 存根替换 search_docs/search_code。
    set(RAGCPP_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(RAGCPP_BUILD_BENCH    OFF CACHE BOOL "" FORCE)
    set(RAGCPP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(RAGCPP_BUILD_CLI      OFF CACHE BOOL "" FORCE)
    set(RAGCPP_WITH_RCP       OFF CACHE BOOL "" FORCE)
    # 无 GPU 后端。 agentty的检索为CPU HNSW + BM25；
    # 它从不发出batch-score 调用 GPU 路径加速。将它们保留为 rag-cpp
    # 的默认值是一种负担，而不是一个功能：
    #   * METAL 在 Apple 上默认 ON，enable_language(OBJCXX) 拖拽 Apple
    #     通过 try_compile 探针进入构建。该探测器继承了我们的 CMAKE_EXE_LINKER_FLAGS
    #     （GCC-static 版本通过了 -static-libgcc / -static-libstdc++），
    #     而 Apple clang 拒绝了 -static-libgcc — 因此整个 macOS
    #     独立配置在 third_party/rag-cpp/CMakeLists.txt. 时失效
    #   * OPENCL auto-detects 任何系统 libOpenCL 并且会默默地添加一个
    #     对意味着独立的二进制程序的动态依赖性。
    set(RAGCPP_WITH_METAL     OFF CACHE BOOL "" FORCE)
    set(RAGCPP_WITH_OPENCL    OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/rag-cpp EXCLUDE_FROM_ALL)
    set(AGENTTY_HAS_RAGCPP TRUE)
    # Opt-in 上游跟踪，默认关闭，因此本地rag-cpp编辑编译而不需要CMake改变子模块结账。
    # 称为 UNCONDITIONALLY，因此 rag-cpp 始终是 on-demand
    # `submodules_sync` 目标的一部分； per-build auto
    # -pull 在函数内部由 AGENTTY_AUTO_PULL_SUBMODULES
    # （以及下面的 rag-specific 覆盖）保持门控。
    option(AGENTTY_AUTO_PULL_RAGCPP "Fast-forward third_party/rag-cpp/ to origin/master on every build" OFF)
    agentty_pull_submodule_latest(rag-cpp main ragcpp)
else()
    message(FATAL_ERROR "agentty: third_party/rag-cpp/ submodule is empty. Run "
                        "`git submodule update --init --recursive` to vendor "
                        "the RAG engine, then reconfigure.")
endif()

if(NOT TARGET nghttp2::nghttp2)
    find_path(NGHTTP2_INCLUDE_DIR nghttp2/nghttp2.h)
    # 独立构建更喜欢静态存档(
    # libnghttp2.a/nghttp2_static.lib)；
    # 常规构建更喜欢共享 库，因此开发人员不需要安装静态存档。
    if(AGENTTY_STANDALONE)
        if(MSVC)
            find_library(NGHTTP2_LIBRARY NAMES nghttp2_static nghttp2)
        else()
            find_library(NGHTTP2_LIBRARY NAMES libnghttp2.a nghttp2_static nghttp2)
        endif()
    else()
        find_library(NGHTTP2_LIBRARY NAMES nghttp2 nghttp2_static)
    endif()
    if(NGHTTP2_INCLUDE_DIR AND NGHTTP2_LIBRARY)
        add_library(nghttp2::nghttp2 UNKNOWN IMPORTED)
        set_target_properties(nghttp2::nghttp2 PROPERTIES
            IMPORTED_LOCATION "${NGHTTP2_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${NGHTTP2_INCLUDE_DIR}")
    else()
        message(FATAL_ERROR
            "nghttp2 not found — install libnghttp2-dev (Debian/Ubuntu), "
            "nghttp2 (Homebrew/Arch), or vcpkg install nghttp2.")
    endif()
endif()

