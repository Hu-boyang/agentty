# prune_allowlist.cmake ——报告 允许清单 不再保留,
# 使用 BANLIST'S 自身 匹配规则,这样答案就与它
# 输入的检查不能有分歧.
#
# 两种腐烂, 两者都看不见 禁用清单 本身(它将文件放在磁盘上,
# 并且只从此 LOOKS UP 编号 允许清单 ,所以一个 过期
# 授权 无人理睬,而不是报道:
#
#   已死 —— 条目名称为已不存在的文件。
#   过期 — 文件存在,但不再使用允许的原始文件.
#
# 随便你 副作用 相同: 手 -checked 免责在代码之外,
# 证明它是正确的, 所以下一个人 添加一个 std::mutex
# 继承了一张无人颁发的通行证
#
# 用法: 制作( make) -DROOT=<src 目录 > -DALLOW=<allowlist> -P prune_allowlist.cmake

if(NOT DEFINED ROOT OR NOT DEFINED ALLOW)
    message(FATAL_ERROR "usage: -DROOT=<dir> -DALLOW=<file> -P prune_allowlist.cmake")
endif()
# 禁用清单的同样陷阱有:ROOT是一个 glob 前缀,
# 所以RELATIVE路径解决了cwd(对于`cmake -P`,
# 无论它在哪里被引用,而不是源目录),默默匹配零文件,然后每个条目看起来都死了.
# 坚持绝对,所以答案不能悄悄错误.
if(NOT IS_ABSOLUTE "${ROOT}")
    message(FATAL_ERROR "ROOT must be an ABSOLUTE path, got: ${ROOT}")
endif()
if(NOT IS_DIRECTORY "${ROOT}")
    message(FATAL_ERROR "ROOT is not a directory: ${ROOT}")
endif()

# 横幅来自jaal的banlist.cmake——如果文件有变化,请保持同步。
set(ban_thread       "std::j?thread([^_:]|$)")
set(ban_detach       "\\.detach\\(\\)")
set(ban_async        "std::async[^_]")
set(ban_mutex        "std::(shared_|recursive_|timed_)?mutex[^_]")
set(ban_atomic       "std::atomic[^_]")
set(ban_thread_local "thread_local")
set(ban_const_cast   "const_cast")
set(ban_sink_access  "sink_access")
set(ban_loop_key     "loop_key")

file(STRINGS ${ALLOW} allow_lines)
set(dead "")
set(stale "")

foreach(line IN LISTS allow_lines)
    # 放下评论, 跳过空白的任何东西。
    #
    # 1. 文档( Q) STRINGS (无) -ASCII ,所以这个部分 头文件
    #    `# ── HTTP: the turn's cancel token ──` 抵达时是
    #    ` HTTP: the turn's cancel token ` —— 数据 '#' 和框中
    #    -drawing 规则是 两者 不见了,所以是领先的... '#' 测试无法抓住它, 否则 解析 作为
    #    `path: names` 输入。
    # 2. 因此,真正的过滤器:一个条目的左侧必须看起来像一个源
    #    路径(没有空格,结束于.cpp/.hpp)。Prose从来不会。
    if(line MATCHES "^[ \t]*#")
        continue()
    endif()
    string(REGEX REPLACE "#.*" "" line "${line}")
    string(STRIP "${line}" line)
    if(line STREQUAL "")
        continue()
    endif()
    if(NOT line MATCHES "^[^ \t]+\\.(cpp|hpp):")
        continue()
    endif()
    string(REGEX MATCH "^([^:]+):(.*)$" _ "${line}")
    set(f "${CMAKE_MATCH_1}")
    string(STRIP "${CMAKE_MATCH_2}" names)
    string(REPLACE " " ";" names "${names}")

    if(NOT EXISTS "${ROOT}/${f}")
        list(APPEND dead "${f}")
        continue()
    endif()

    # Ask 查找是否在此文件中出现每个允许的名称。 原因相同 禁用清单 是否
    # 为: CMake '文件( I) STRINGS ) 崩溃 `;`-ending
    # 以众配主,以贪婪者为排行。 `//.*` 将文件的半部隐藏在下面,
    # 默默地标记实际用途为 "unused" . grep 是线 -oriented
    # 所以不能在某处丢失数据 `;`。同步进行 禁用清单 接近目标了
    find_program(GREP_EXE grep REQUIRED)
    set(unused "")
    foreach(name IN LISTS names)
        execute_process(
            COMMAND ${GREP_EXE} -E "${ban_${name}}" "${ROOT}/${f}"
            OUTPUT_VARIABLE hits
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE grep_rc
            ERROR_QUIET)
        set(found FALSE)
        if(hits)
            # 过滤评论-only行与禁用清单行相同;在流派(e.g.
            # "// once used std::mutex")中匹配
            # 的ONLY不证明保留授权是正当的.
            string(ASCII 1 _sc)
            string(REPLACE ";" "${_sc}" hits "${hits}")
            string(REGEX MATCHALL "[^\n]+" hit_lines "${hits}")
            foreach(hl IN LISTS hit_lines)
                string(REPLACE "${_sc}" ";" hl "${hl}")
                if(hl MATCHES "^[ \t]*//")
                    continue()
                endif()
                string(REGEX REPLACE "//.*" "" code "${hl}")
                if(code MATCHES "${ban_${name}}")
                    set(found TRUE)
                    break()
                endif()
            endforeach()
        endif()
        if(NOT found)
            list(APPEND unused "${name}")
        endif()
    endforeach()
    if(NOT unused STREQUAL "")
        string(REPLACE ";" " " unused "${unused}")
        list(APPEND stale "${f}: ${unused}")
    endif()
endforeach()

if(dead STREQUAL "" AND stale STREQUAL "")
    message(STATUS "allowlist is tight: every entry still earns its keep")
    return()
endif()

foreach(d IN LISTS dead)
    message(STATUS "DEAD  ${d}")
endforeach()
foreach(s IN LISTS stale)
    message(STATUS "STALE ${s}")
endforeach()
list(LENGTH dead nd)
list(LENGTH stale ns)
message(FATAL_ERROR "allowlist has ${nd} dead entr(ies) and ${ns} stale grant(s)")
