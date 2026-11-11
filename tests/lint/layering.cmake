# tests/lint/layering.cmake — agentty → maya → jaal 严格来说
#
# agentty 取决于 maya 永远不要 jaal : 时间 运行时 用来把它当作一个
# maya * 姓名来源 <maya/runtime.hpp>. docs/LAYERING.md.
#
# 失败者在,在 agentty's src/ include/ tests/:
#   * 包括 <jaal/...>
#   * 这个词 jaal 代码中任意位置 : jaal::x, `jaal :: x` ,`使用命名空间'
#     jaal;`, ` 名称空间 j = jaal;`
#   * 一个JAAL_宏
# maya和jaal用自己的layering.cmake检查自己的方向;agentty也运行
# 这些方向(layering_maya,layering_jaal)。
#
# 注释和字符串字节可以说任何事情。 每一个文件都被剥除,
# 块注释包含在内, 因此, 将字节包在行上本身不会被
# 误认为代码。
#
# cmake/ 不被扫描: 建筑是必须命名的地方 jaal's 源码树
# 运行中 jaal 自己买的 agentty.
#
# 运行时: CMake -DROOT=<agentty 源代码 > -P layering.cmake

if(NOT DEFINED ROOT)
    message(FATAL_ERROR "ROOT is required")
endif()

# 带有注释的 C++ 文件的代码和string/char 字节的空白,
# 新行保持了线条编号仍然匹配。 Regex 通过了,而不是字符循环 :
# a CMake 绕过每个角色 花了大部分时间 jaal's 源码树.
function(code_of path out)
    file(READ "${path}" s)
    # 文言先,故一内之一/或/*,不取以评.
    string(REGEX REPLACE "R\"([^(]*)\\([^)]*\\)[^\"]*\"" "\"\"" s "${s}")
    string(REGEX REPLACE "\"([^\"\\\\\n]|\\\\.)*\"" "\"\"" s "${s}")
    string(REGEX REPLACE "'([^'\\\\\n]|\\\\.)*'" "''" s "${s}")
    string(REGEX REPLACE "//[^\n]*" "" s "${s}")
    # / *... * / 可能横跨行: 每个块替换为尽可能多的新行.
    while(s MATCHES "/\\*")
        string(FIND "${s}" "/*" a)
        string(SUBSTRING "${s}" ${a} -1 tail)
        string(FIND "${tail}" "*/" b)
        if(b EQUAL -1)
            set(b_len -1)
            set(body "${tail}")
        else()
            math(EXPR b_len "${b} + 2")
            string(SUBSTRING "${tail}" 0 ${b_len} body)
        endif()
        string(REGEX REPLACE "[^\n]" "" nl "${body}")
        string(SUBSTRING "${s}" 0 ${a} head)
        if(b_len EQUAL -1)
            set(s "${head}${nl}")
        else()
            string(SUBSTRING "${tail}" ${b_len} -1 rest)
            set(s "${head}${nl}${rest}")
        endif()
    endwhile()
    set(${out} "${s}" PARENT_SCOPE)
endfunction()

# 扫描( C) <dirs> <word-regex> <include-regex> <label> : 附录 `hits`.
# `word` 是一个整体标识符。
function(scan dirs word inc label)
    set(out "")
    foreach(dir IN LISTS dirs)
        file(GLOB_RECURSE files ${ROOT}/${dir}/*.cpp ${ROOT}/${dir}/*.hpp ${ROOT}/${dir}/*.h
                                ${ROOT}/${dir}/*.cc ${ROOT}/${dir}/*.ipp ${ROOT}/${dir}/*.inl
                                ${ROOT}/${dir}/*.tpp ${ROOT}/${dir}/*.mm)
        foreach(f IN LISTS files)
            if(f MATCHES "/tests/lint/" OR f MATCHES "/_deps/" OR f MATCHES "/build[^/]*/")
                continue()
            endif()
            # 便宜预告 -filter :大多数文件根本没有提到过这个名字.
            file(STRINGS ${f} any REGEX "${word}|${inc}")
            if(NOT any)
                continue()
            endif()
            file(READ ${f} raw_text)                  # 用于包含的行
            string(REPLACE ";" "\\;" raw_text "${raw_text}")
            string(REPLACE "\n" ";" raw "${raw_text}")
            code_of(${f} code)
            string(REPLACE ";" "\\;" code "${code}")
            string(REPLACE "\n" ";" lines "${code}")
            file(RELATIVE_PATH rel ${ROOT} ${f})
            set(ln 0)
            foreach(l IN LISTS lines)
                math(EXPR ln "${ln} + 1")
                # 包含路径为"string",
                # 已经空白: 对照原始线检查指令。
                set(hit FALSE)
                if(l MATCHES "^[ \t]*#[ \t]*include")
                    math(EXPR k "${ln} - 1")
                    list(LENGTH raw rn)
                    if(k LESS rn)
                        list(GET raw ${k} r)
                        if(r MATCHES "${inc}")
                            set(hit TRUE)
                        endif()
                    endif()
                endif()
                if(hit OR l MATCHES "(^|[^A-Za-z0-9_])(${word})([^A-Za-z0-9_]|$)")
                    string(STRIP "${l}" s)
                    list(APPEND out "${label}: ${rel}:${ln}: ${s}")
                endif()
            endforeach()
        endforeach()
    endforeach()
    set(hits ${hits} ${out} PARENT_SCOPE)
endfunction()

set(hits "")
scan("src;include;tests" "jaal|JAAL_[A-Za-z0-9_]+"
     "#[ \t]*include[ \t]*[<\"]([^>\"]*/)?jaal/" "agentty->jaal")

if(hits)
    list(JOIN hits "\n  " msg)
    message(FATAL_ERROR
        "agentty reaches past maya into jaal:\n  ${msg}\n\n"
        "Use the maya:: name from <maya/runtime.hpp>. If maya doesn't expose "
        "it yet, add it there (docs/LAYERING.md).")
endif()
message(STATUS "layering ok: agentty uses the runtime only through maya")
