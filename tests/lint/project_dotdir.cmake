# project_dotdir.cmake ——项目. `.agentty` 拥有 KNOWN 顶级
#
# 用户根排序为( E) cache/ credentials/ logs/
# threads/ 因为平面目录没有答案 "where does this new file go"
# 并文件,然后用硬币登陆 -flip。项目根从未得到这种治疗,
# 漂移到13个条目, 跨越四个生命周期 -- -- 配置、累积数据、
# 衍生索引和锁文件 -- -- 并排。
#
# 平面布局的两件事不能表达,两者都花费了我们:
#
#   `.agentty/`忽略了团队想要的技能
#     已实施; `!.agentty/skills/` 加上越来越严重的拒绝名单是没有人
#     坚持的规矩。
#   * 没有安全 "reset the derived state". `rm -rf .agentty` 牵手 -authored
#     技能和积累反馈,以及可重建的指数。
#
# 这13个条目中,每个条目都合法地到达,一个一次承诺,
# 这正是为什么审查者不会抓住第14个条目的原因。
# 这就使得最高层成为了宣布的一组:增加一个名字是某人故意作出的决定,
# 以这个清单作为记录它属于哪个类别的地方。
#
# 不 样式检查。分类是来自
# docs/design/dot-agentty.md ,而生命周期是
# 决定文件是否被执行、可删除或需要覆盖的。
#
# 用法: 制作( make) -DROOT=<repo root > , 转动 -P project_dotdir.cmake

# `-P` 脚本从每个未设置的策略开始;没有此选项,则 IN_LIST 下图
# 旧-CMP0057 "Unknown arguments" 支票总是会失败的
cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED ROOT)
    message(FATAL_ERROR "project_dotdir: -DROOT=<repo root> required")
endif()

set(_dotdir "${ROOT}/.agentty")
if(NOT IS_DIRECTORY "${_dotdir}")
    message(STATUS "project dotdir: none here, nothing to check")
    return()
endif()

# CONFIG - 手 - 手 手 - - 手 -authored 承诺,团队的。
# 吉提格诺尔是种子 dirs.cpp 用于第一手和第一手 -edited
# 之后(它自己说) 头文件 ),它带有球队的忽略规则——所以它是配置的,
# 它和其他任何.gitignore一样属于git。
# 它完全没有出现在这个名单中,这意味着对于曾经跑过的人来说,这个插图失败了。
# agentty 在一个项目。
set(_config
    settings.json mcp.json hooks.json .gitignore
    skills agents commands knowledge)

# LOCAL CONFIG - 手-authored,  ⁇ ,你的。
set(_local settings.local.json)

# DATA -- -- 使用后积累的, 不 复制,删除会失去一些东西。
set(_data memory.jsonl rag_feedback.tsv routing_memory.tsv state)

# DERIVED/RUNTIME-可重建或处理-scoped. 安全删除.
# 以图案匹配,因为嵌入器标记和锁后缀不同.
set(_derived_globs
    "*.ragdb" "*.ragdb.meta.json" "*.lock" "cache")

set(_known ${_config} ${_local} ${_data})

file(GLOB _entries RELATIVE "${_dotdir}" "${_dotdir}/*")
set(_unknown "")
foreach(_e IN LISTS _entries)
    if(_e IN_LIST _known)
        continue()
    endif()
    set(_matched FALSE)
    foreach(_g IN LISTS _derived_globs)
        # 将光彩转换成正则 首次。匹配原始的光彩会尝试 编译
        # `^*.ragdb$` ,其中领先者 `*` 没有什么动作。
        string(REPLACE "." "\\." _re "${_g}")
        string(REPLACE "*" ".*" _re "${_re}")
        if(_e MATCHES "^${_re}$")
            set(_matched TRUE)
            break()
        endif()
    endforeach()
    if(NOT _matched)
        list(APPEND _unknown "    .agentty/${_e}")
    endif()
endforeach()

if(_unknown)
    string(REPLACE ";" "\n" _report "${_unknown}")
    message(FATAL_ERROR
        "unrecognised entries in the project .agentty:\n\n${_report}\n\n"
        "Every file here has a LIFECYCLE, and it decides whether the file is\n"
        "committed, whether it is safe to delete, and whether it needs a\n"
        "relocation override. Pick one and add the name to the matching list\n"
        "in tests/lint/project_dotdir.cmake:\n\n"
        "  config   hand-authored, committed        (skills/, mcp.json)\n"
        "  local    hand-authored, gitignored       (settings.local.json)\n"
        "  data     accumulated, NOT regenerable    (memory.jsonl)\n"
        "  derived  rebuildable, safe to delete     (*.ragdb, cache/)\n\n"
        "See docs/design/dot-agentty.md for the decision procedure.")
endif()

list(LENGTH _entries _n)
message(STATUS "project dotdir: ${_n} entr(ies), all categorised")
