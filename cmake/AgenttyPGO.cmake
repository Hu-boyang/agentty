# AgenttyPGO.cmake-opt-in配置文件-guided优化(two-phase)。
# 在已经-defined `agentty`目标上运行；包含在它之后。 No-op 除非
# -DAGENTTY_PGO=generate|use. 请参阅工作流程的 头文件 注释。

# ── Profile-guided 优化( opt) -in ,两个阶段) ────────────────────
# 第 1 阶段：`cmake -B build-pgogen -DAGENTTY_PGO=generate`→构建，
#          然后运行 ​​`scripts/pgo-train.sh build-pgogen/agentty`
#          ——脚本化的 PTY 工作负载，涵盖启动、键入、选择面板、重绘 和退出（
#          CPU-bound 热路径；network-free，不需要 凭据）。
# 第 2 阶段：`cmake -B build-pgouse -DAGENTTY_PGO=use`→
#          使用计数器重建 — 流式传输 上的典型增益 + 布局热路径是 8-12% wall
#          -clock，3-6% 二进制程序 上的 MSVC/GCC/Clang. 尺寸
# 档案数据目录不存在于build/之外，
# 因此切换发电机或吹走build/不会丢失您收集的档案。
set(AGENTTY_PGO "" CACHE STRING
    "Profile-guided optimization: 'generate', 'use', or empty.")
set(AGENTTY_PGO_DIR "${CMAKE_SOURCE_DIR}/pgo-data" CACHE PATH
    "Directory where PGO profile counters live across generate/use phases.")
if(AGENTTY_PGO)
    file(MAKE_DIRECTORY "${AGENTTY_PGO_DIR}")
    if(MSVC)
        if(AGENTTY_PGO STREQUAL "generate")
            target_link_options(agentty PRIVATE "/GENPROFILE:PGD=${AGENTTY_PGO_DIR}/agentty.pgd")
        elseif(AGENTTY_PGO STREQUAL "use")
            target_link_options(agentty PRIVATE "/USEPROFILE:PGD=${AGENTTY_PGO_DIR}/agentty.pgd")
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        if(AGENTTY_PGO STREQUAL "generate")
            target_compile_options(agentty PRIVATE -fprofile-instr-generate)
            target_link_options(agentty PRIVATE -fprofile-instr-generate)
        elseif(AGENTTY_PGO STREQUAL "use")
            # 期望`llvm-profdata merge -output=agentty.profdata ${AGENTTY_PGO_DIR}/*.profraw`
            # 在两个构建之间运行。
            target_compile_options(agentty PRIVATE "-fprofile-instr-use=${AGENTTY_PGO_DIR}/agentty.profdata")
            target_link_options(agentty PRIVATE "-fprofile-instr-use=${AGENTTY_PGO_DIR}/agentty.profdata")
        endif()
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        if(AGENTTY_PGO STREQUAL "generate")
            target_compile_options(agentty PRIVATE "-fprofile-generate=${AGENTTY_PGO_DIR}")
            target_link_options(agentty PRIVATE "-fprofile-generate=${AGENTTY_PGO_DIR}")
        elseif(AGENTTY_PGO STREQUAL "use")
            target_compile_options(agentty PRIVATE
                "-fprofile-use=${AGENTTY_PGO_DIR}" -fprofile-correction)
            target_link_options(agentty PRIVATE "-fprofile-use=${AGENTTY_PGO_DIR}")
        endif()
    endif()
    message(STATUS "agentty: PGO phase = ${AGENTTY_PGO} (data dir: ${AGENTTY_PGO_DIR})")
endif()

