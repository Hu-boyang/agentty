# tests/lint/elm_purity.cmake — update() 纯函数( ) Model, Msg).
#
# 该 CONTRACT 国 务 院
#
# agentty 是精灵程序,在 jaal. A 状态更新函数 - 任何在
# 下面的东西 src/runtime/app/update/ 加号 update.cpp
# - 读写 Model 财务报告和已审计财务报表 RETURNS a Cmd 它没有到达世界本身。
# 副作用 东西 状态更新函数 描述和 运行时 执行 :
#
#   说得没错
#   ───────────────────────────  ────────────────────────────────────────────
#   做吗 IO / 网络/ 拨号返回一个 cmd : 副作用 (
#   cmd_factory.cpp ) 了解时间读取 Model (由E.
#   . . 运行时 )知道一个env var读到 Model (一
#   经查封) 改变过程 -global 状态返回 cmd :
#   副作用 改变它记忆中的东西 把它放进 Model 永远不要静态
#
# 重放 re-runs 副作用, 一个过程中的两个应用例
# 分享它们不应该的状态, 测试必须让世界停滞, 而不是喂食
# Msgs. "Pure" 也不属于这里的风格偏好: 一个
# 状态更新函数 拨号,读盘或块 拖住 UI 线程 持续时间。
#
# HOW IT IS ENFORCED
#
# 形状与 jaal 其货币 禁用清单 ,故意: 命名图案列表,
# a per -file 允许清单 上面写着文件可能仍然使用的名称,
# 以及紧身的 -check (elm_purity_tight )当
# 一个 列入允许清单 名称不再使用。所以 允许清单 是那个 REMAINING
# DEBT ,将它写下来。它只能缩小:新的违规失败了建筑,
# 而修复旧的则失败了建筑直到取消其豁免。
#
# 模式是名字,而不是语义。 文本的文本不能证明纯洁;
# 它能够做的是使每一个已知的到达世界的方法都成为一个人
# 必须写下来的决定,而这正是不断倒退的部分。
#
# 运行方式: cmake -DROOT=<src/runtime/app> -DALLOW=<allowlist> [-DTIGHT=1]
#                 -P elm_purity.cmake

if(NOT DEFINED ROOT OR NOT DEFINED ALLOW)
    message(FATAL_ERROR "ROOT and ALLOW are required")
endif()

# 名称 ~ regex。每个方式都是 状态更新函数 到达 Model.
set(ban_clock       "(steady_clock|system_clock|high_resolution_clock)::now[ \t]*\\(")
# 主题探测器读作 TERM/COLORTERM/COLORFGBG ,
# 设置帮助者读取 AGENTTY_ 使用 : 使用。 Model::env.
# 终端 / Model::env . . 设置。
set(ban_env         "(std::)?(getenv|setenv|unsetenv|secure_getenv)[ \t]*\\(|detect_(tier|polarity)[ \t]*\\(|ui_prefs::detect[ \t]*\\(|terminal_is_dumb[ \t]*\\(")
# fileio:一个触摸磁盘的操作. 不 赤裸的 赤裸
# `std::filesystem::` 前缀 : `std::filesystem::path`
# 是一个值类型(字符串操纵,无音响),并显示一个 状态更新函数
# 将路径编译为 IO。只有实际读取或写入文件系统的函数。
set(ban_fileio      "std::(if|of|f)stream|(fs|std::filesystem)::(remove|rename|create_director|copy|exists|is_regular_file|is_directory|file_size|last_write_time|directory_iterator|recursive_directory_iterator|status|read_symlink|canonical|weakly_canonical|temp_directory_path|current_path|space|equivalent)\\b")
set(ban_subprocess  "run_command|::system[ \t]*\\(|popen[ \t]*\\(")
# 未列出:网络, 凭据 账户, 持久化 ,提供方和子代理设置器,
# 插件编辑器, skills/commands/hooks 检查,
# 测试和潜伏的帮助者,这些需要 agentty::Io ,
# 该 状态更新函数 从来没有,所以 编译器 拒绝 调用 (util/io.hpp
# 留下的是生的 生的 std/OS 调用 无法附着一个标志
set(ban_static      "^[ \t]+static[ \t]+(std::|auto[ \t]|bool[ \t]|int[ \t]|long[ \t]|unsigned[ \t]|double[ \t]|float[ \t]|size_t[ \t])[^(]*[=;{][ \t]*$")
set(ban_thread      "std::j?thread([^_:]|$)|\\.detach\\(\\)|std::async[^_]")
# ty:问: 终端 直接的,大小是作为 Msg (TerminalResized ,
# 从 maya's on_resize 并住在一个 Model::ui.
set(ban_tty         "platform::(query_terminal_size|stdout_handle|stdin_handle)|isatty[ \t]*\\(|ioctl[ \t]*\\(")
# disk_lookup :帮助移动磁盘或安全商店以回答一个查询.
# 状态更新函数 读取答案 Model 改为: m.ui.library (技能、
# 批准、命令、钩子), m.ui.git_repo, m.env
# (嵌入默认值, 用户根). A 工作线程 需要他们让他们在 cmd:
set(ban_disk_lookup "util::(user_root|home_dir)[ \t]*\\(")
set(ban_names clock env fileio subprocess static thread tty disk_lookup)

# 解析 编号 允许清单: `path: name name ...`, `#` 评论。
file(STRINGS ${ALLOW} allow_lines)
set(allowed_files "")
foreach(line IN LISTS allow_lines)
    string(REGEX REPLACE "#.*" "" line "${line}")
    string(STRIP "${line}" line)
    if(line STREQUAL "")
        continue()
    endif()
    string(REGEX MATCH "^([^:]+):(.*)$" _ "${line}")
    set(f "${CMAKE_MATCH_1}")
    string(STRIP "${CMAKE_MATCH_2}" names)
    string(REPLACE " " ";" names "${names}")
    string(MAKE_C_IDENTIFIER "${f}" key)
    set(allow_${key} "${names}")
    list(APPEND allowed_files "${f}")
endforeach()

# 状态更新函数 源码树,和ROOT的其他没有:cmd_factory.cpp是WHERE
# 副作用应该活下来,所以扫描它会颠倒规则.
file(GLOB_RECURSE files RELATIVE ${ROOT} ${ROOT}/update/*.cpp ${ROOT}/update/*.hpp)
list(APPEND files update.cpp)
find_program(GREP_EXE grep REQUIRED)

# 一次枪声结束 MANY 文档(与 `base` ) - > 清单
# "file\x02line\x02content" 点击数 `out`, `;` 中
# 通过\x01保存的内容。每个模式一个进程而不是每个文件一个进程:创建进程花费数十 ms
# Windows ,则每 -file 形态运行~5000产卵,吹吹阴囊 超时.
function(grep_many out base pattern)
    set(rel ${ARGN})
    set(acc "")
    if(rel)
        execute_process(
            COMMAND ${GREP_EXE} -nHE "${pattern}" ${rel}
            WORKING_DIRECTORY ${base}
            OUTPUT_VARIABLE hits OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(hits)
            string(ASCII 1 _sc)
            string(ASCII 2 _fs)
            string(REPLACE ";" "${_sc}" hits "${hits}")
            string(REGEX MATCHALL "[^\n]+" lines "${hits}")
            foreach(l IN LISTS lines)
                string(REGEX MATCH "^([^:]+):([0-9]+):(.*)$" _ "${l}")
                list(APPEND acc "${CMAKE_MATCH_1}${_fs}${CMAKE_MATCH_2}${_fs}${CMAKE_MATCH_3}")
            endforeach()
        endif()
    endif()
    set(${out} "${acc}" PARENT_SCOPE)
endfunction()

# 解开一个 grep_many 被击中 hit_file / hit_line / hit_text.
macro(unpack_hit h)
    string(ASCII 1 _sc)
    string(ASCII 2 _fs)
    string(REPLACE "${_fs}" ";" _parts "${h}")
    list(GET _parts 0 hit_file)
    list(GET _parts 1 hit_line)
    list(SUBLIST _parts 2 -1 _rest)
    list(JOIN _rest "" hit_text)
    string(REPLACE "${_sc}" ";" hit_text "${hit_text}")
endmacro()

set(errors "")
set(present "")
foreach(f IN LISTS files)
    if(EXISTS ${ROOT}/${f})
        list(APPEND present "${f}")
        string(MAKE_C_IDENTIFIER "${f}" key)
        set(used_${key} "")
    endif()
endforeach()
foreach(name IN LISTS ban_names)
    grep_many(hits ${ROOT} "${ban_${name}}" ${present})
    foreach(h IN LISTS hits)
        unpack_hit("${h}")
        set(f "${hit_file}")
        set(lineno "${hit_line}")
        set(content "${hit_text}")
        if(content MATCHES "^[ \t]*//")
            continue()
        endif()
        string(REGEX REPLACE "//.*" "" code "${content}")
        if(NOT code MATCHES "${ban_${name}}")
            continue()
        endif()
        # `static constexpr` 财务报告和已审计财务报表 `static const` 是表,不是状态。
        if(name STREQUAL "static" AND code MATCHES "static[ \t]+(constexpr|const)[ \t]")
            continue()
        endif()
        string(MAKE_C_IDENTIFIER "${f}" key)
        set(allowed "${allow_${key}}")
        list(APPEND used_${key} "${name}")
        list(FIND allowed "${name}" idx)
        if(idx EQUAL -1)
            string(STRIP "${content}" content)
            list(APPEND errors "${f}:${lineno}: [${name}] ${content}")
        endif()
    endforeach()
endforeach()

list(LENGTH files nfiles)
if(nfiles EQUAL 0)
    message(FATAL_ERROR "elm_purity scanned 0 files under ROOT=${ROOT}: "
                        "nothing was checked, so this is a failure, not a pass.")
endif()

if(errors)
    list(JOIN errors "\n  " msg)
    message(FATAL_ERROR
        "a reducer reaches outside the Model:\n  ${msg}\n\n"
        "update() must be pure. Return a cmd:: effect, read the Model, or move "
        "the state into the Model. If this really must stay for now, add it "
        "to ${ALLOW} with the reason — that file is the debt list.")
endif()

# 紧凑模式:没有人再使用过豁免 过期 ,并设置一个
# 过期 输入是一个孔,回归可以直接穿过。失败了。
if(TIGHT)
    set(stale "")
    foreach(f IN LISTS allowed_files)
        string(MAKE_C_IDENTIFIER "${f}" key)
        if(NOT EXISTS ${ROOT}/${f})
            list(APPEND stale "${f}: file is gone, drop the entry")
            continue()
        endif()
        foreach(name IN LISTS allow_${key})
            list(FIND used_${key} "${name}" idx)
            if(idx EQUAL -1)
                list(APPEND stale "${f}: '${name}' is no longer used, drop it")
            endif()
        endforeach()
    endforeach()
    if(stale)
        list(JOIN stale "\n  " msg)
        message(FATAL_ERROR "elm_purity allowlist has stale grants:\n  ${msg}")
    endif()
endif()

message(STATUS "elm_purity ok (${nfiles} reducer files)")

# ── Model::now 是那个 FOLD 时间到了... 过期 任何地点 归并 正在运行中 ─────
#
# 电话 分派 接口边界 写入 Model::now 发件人 jaal 步骤 -time
# 参数作为第一件事 归并 已经,所以里面 update/ 总是流畅的 外面,
# 它是 最后 归并 时间 : view() 财务报告和已审计财务报表 subscribe()
# 看见一个凸轮 Model 可能已经是老了,还有... wall_now (
# 从中产生)等于 过期 读者会得到一个可信的 -looking 答错无误.
# 所以,唯一允许的读者是 状态更新函数 编辑 接口边界 写着呢
if(DEFINED VIEW_ROOT)
    file(GLOB_RECURSE vfiles RELATIVE ${VIEW_ROOT} ${VIEW_ROOT}/*.cpp ${VIEW_ROOT}/*.hpp)
    # 分区一次: 文件在 a 之外 归并 ,和 view/panel 文档。
    set(nonfold_files "")
    set(view_files "")
    foreach(f IN LISTS vfiles)
        # 电话 状态更新函数 编辑 接口边界 文件是在哪里 `now` 已存在。
        # cmd_factory 从内部建立 Cmds 状态更新函数 叫做"它",
        # 所以它就在"它"的 归并 也(任务) 正文 返回时不能读 m。
        if(NOT (f MATCHES "^runtime/app/update/" OR f STREQUAL "runtime/app/update.cpp"
                OR f STREQUAL "runtime/app/init.cpp"
                OR f STREQUAL "runtime/app/cmd_factory.cpp"))
            list(APPEND nonfold_files "${f}")
        endif()
        if(f MATCHES "^runtime/(view|panel)/")
            list(APPEND view_files "${f}")
        endif()
    endforeach()

    # scan_view(<out> <pattern> <exempt-regex-or-empty>
    # <files...> : 整个地方都有一个grep <files> ,删除注释 -only 匹配豁免
    # 标记的线条和线条; 点击数为 "file:line: text".
    function(scan_view out pattern exempt)
        grep_many(hits ${VIEW_ROOT} "${pattern}" ${ARGN})
        set(acc "")
        foreach(h IN LISTS hits)
            unpack_hit("${h}")
            if(hit_text MATCHES "^[ \t]*//")
                continue()
            endif()
            if(NOT exempt STREQUAL "" AND hit_text MATCHES "${exempt}")
                continue()
            endif()
            string(STRIP "${hit_text}" c)
            list(APPEND acc "${hit_file}:${hit_line}: ${c}")
        endforeach()
        set(${out} "${acc}" PARENT_SCOPE)
    endfunction()

    scan_view(stale_reads "\\b(m|model|mdl)(\\.|->)(now\\b|wall_now\\()" "" ${nonfold_files})
    if(stale_reads)
        list(JOIN stale_reads "\n  " msg)
        message(FATAL_ERROR
            "Model::now read outside a fold (it is stale there):\n  ${msg}\n\n"
            "A view or subscription that needs the current time must get it "
            "from the runtime (an animation clock, a Tick), not from the last "
            "fold's timestamp.")
    endif()
    message(STATUS "elm_purity: Model::now read only inside folds")

    # view() 是函数 Model 也: 数据 终端 大小达到它作为
    # Model::ui.term_cols/term_rows (按 RenderContext ),
    # 所以没有查看代码询问 tty。 状态更新函数.
    scan_view(tty_reads "${ban_tty}" "" ${view_files})
    if(tty_reads)
        list(JOIN tty_reads "\n  " msg)
        message(FATAL_ERROR
            "view code asks the terminal directly:\n  ${msg}\n\n"
            "Read the size from the RenderContext view() installs "
            "(maya::available_width/height), which comes from the Model.")
    endif()
    message(STATUS "elm_purity: view reads the terminal size from the Model")

    # ..以及从... ... 帧 时钟( 小时) maya::anim_now ,
    # 可以冻结和推进测试),日期通过 Model::wall_at。
    # 在视图代码中读取的原始时钟只允许作为perf stopwatch,
    # 其结果为日志,从不为像素,行必须如此表示。
    scan_view(clock_reads "${ban_clock}" "// stopwatch:" ${view_files})
    if(clock_reads)
        list(JOIN clock_reads "\n  " msg)
        message(FATAL_ERROR
            "view code reads a clock directly:\n  ${msg}\n\n"
            "Use maya::anim_now() (and Model::wall_at for dates). A perf "
            "stopwatch that only feeds a log may stay; mark the line "
            "`// stopwatch: ...`.")
    endif()
    message(STATUS "elm_purity: view reads time from the frame clock")

    # . . 和通过环境 Model::env 一次在发射时被俘
    # 碰巧住在地下的检测助手 view/ 但只从这里逃出来 read_launch_env
    # 标记其线条 `// launch-env: ...`.
    scan_view(env_reads "${ban_env}" "// launch-env:" ${view_files})
    if(env_reads)
        list(JOIN env_reads "\n  " msg)
        message(FATAL_ERROR
            "view code reads the environment:\n  ${msg}\n\n"
            "Capture it in read_launch_env (runtime/app/env.cpp) into "
            "Model::env and read that.")
    endif()
    message(STATUS "elm_purity: view reads the environment through Model::env")
endif()
