# util_layering.cmake — agentty::util 不得依赖 agentty::tools::util.
#
# 有两个`util`命名空间,而分裂是唯一使名称碰撞可以容忍的
# 东西:  ⁇   ⁇  :
#
#   agentty::tools::util 编号 TOOL BOUNDARY
#                          墙壁和 解析器 站在模型的要求和机器之间:
#                          工作区 夹、子进程产卵、
#                          沙箱 信任的交接门 辩论读物
#   agentty::util          APP-WIDE PLUMBING 没有任何工具的概念 调用:
#                          伐木业, home/root 决议, base64 撕裂。 撕裂的。
#
# 依赖必须运行 一个 WAY. tools::util 可能
# 使用乌提尔( $ fs_helpers 调用 home_dir
# 使用权绝不能退缩,因为依赖它所保护的东西的墙不是墙——
# 而且因为循环是使两个目录真正无法区分而不是仅仅被命名为类似
# 名称的原因。
#
# 两者都有记录 头文件 文档衰减,所以这才强制它。
#
# 用法: 制作( make) -DROOT=<repo root > , 转动 -P util_layering.cmake

if(NOT DEFINED ROOT)
    message(FATAL_ERROR "util_layering: -DROOT=<repo root> required")
endif()

set(_offenders "")

# 两者的一半 agentty::util : 来源和 头文件.
file(GLOB_RECURSE _util_files
     "${ROOT}/src/util/*.cpp" "${ROOT}/src/util/*.hpp"
     "${ROOT}/include/agentty/util/*.hpp")

foreach(_f IN LISTS _util_files)
    file(STRINGS "${_f}" _hits REGEX "tools::util|agentty/tool/util/")
    if(_hits)
        file(RELATIVE_PATH _rel "${ROOT}" "${_f}")
        foreach(_h IN LISTS _hits)
            # 省略第头文件项中的解释性说明,该说明中将另一个命名为
            # 命名空间正是为了区分它们而存在。
            if(NOT _h MATCHES "^[ \t]*//")
                string(STRIP "${_h}" _h)
                list(APPEND _offenders "    ${_rel}: ${_h}")
            endif()
        endforeach()
    endif()
endforeach()

if(_offenders)
    string(REPLACE ";" "\n" _report "${_offenders}")
    message(FATAL_ERROR
        "agentty::util reached into agentty::tools::util:\n\n${_report}\n\n"
        "The dependency runs ONE WAY: tools::util may use util, never the\n"
        "reverse. If this helper genuinely needs the tool boundary, it belongs\n"
        "under src/tool/util/ instead. See the notes in\n"
        "include/agentty/util/home_dir.hpp and\n"
        "include/agentty/tool/util/fs_helpers.hpp.")
endif()

message(STATUS "util layering: agentty::util is free of tools::util")
