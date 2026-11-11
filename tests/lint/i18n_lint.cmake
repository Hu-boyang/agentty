# i18n_lint - 三个门 保持了 目录 诚实的。
#
# 运行为测试项( 见 AgenttyTests.cmake 纯净
# CMake 和纯文本:没有建造文物,所以它以毫秒的速度运行,
# 不能被一个未能跳过平台 编译 东西。
#
# 原因 A LINT AT 全部。弦扫荡是一个大型机械化的
# diff -- -- 数百升移到一个 目录 ——和
# "did we drop one" 检查者可以用眼睛回答问题。
# 这些门是机械回答的,因此可以进行扫描。
#
#   使用: 制作 -DROOT=<src> -P i18n_lint.cmake

# A `cmake -P` 脚本从每个未设定的策略开始,
# 所以 `IN_LIST` 下面是被解为普通词的下面 旧 CMP0057
# 编辑 `if` 出错 — 每次运行时将整个测试条目都变成红色。
# 这只为脚本设置了底板; 不影响项目本身的要求。
cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED ROOT)
    message(FATAL_ERROR "usage: -DROOT=<source dir> -P i18n_lint.cmake")
endif()
if(NOT IS_ABSOLUTE "${ROOT}")
    # 和其他的线条一样的陷阱: 一个亲戚 ROOT 解决
    # cwd(用于 `cmake -P` ,无论它被引用的地方),
    # 默默匹配零文件,整张支票经过绿色,没有检查过.
    message(FATAL_ERROR "ROOT must be ABSOLUTE, got: ${ROOT}")
endif()

set(_fail 0)

# ── 读英文 目录 调出 startup.cpp ──────────────────────────
#
# 电话 目录 是一个原始字符串字元, 而不是一个数据文件,
# 因为它是嵌入的( 参见下面的注释: a) 目录 读于 运行
# 时 决定什么是 权限 提示词 说,所以,用铅印读取源。
set(_startup "${ROOT}/src/i18n/startup.cpp")
if(NOT EXISTS "${_startup}")
    message(FATAL_ERROR "i18n: cannot find ${_startup}")
endif()
file(READ "${_startup}" _startup_text)

# 每一个 "id" : 在一个条目的开头。 ID 的点数
# 较低 -case 也正是这种正则性能无法匹配 VALUES.
string(REGEX MATCHALL "\"[a-z][a-z0-9_]*(\\.[a-z0-9_]+)+\"[ \t]*:" _cat_raw
       "${_startup_text}")
set(_catalog_ids "")
foreach(_m IN LISTS _cat_raw)
    string(REGEX REPLACE "^\"(.*)\"[ \t]*:$" "\\1" _id "${_m}")
    list(APPEND _catalog_ids "${_id}")
endforeach()
list(REMOVE_DUPLICATES _catalog_ids)
list(LENGTH _catalog_ids _n_catalog)

# ── 收集每个 t()/tn()/format() 用于 源码树 ──────────────────
file(GLOB_RECURSE _sources "${ROOT}/src/*.cpp" "${ROOT}/include/*.hpp")
set(_used_ids "")
set(_model_facing "")

foreach(_f IN LISTS _sources)
    file(READ "${_f}" _text)
    file(RELATIVE_PATH _rel "${ROOT}" "${_f}")

    string(REGEX MATCHALL
           "i18n::(t|tn|format|format_n)\\([ \t\n]*\"[^\"]+\"" _calls "${_text}")
    if(NOT _calls)
        continue()
    endif()

    foreach(_c IN LISTS _calls)
        string(REGEX REPLACE ".*\"([^\"]+)\"$" "\\1" _id "${_c}")
        list(APPEND _used_ids "${_id}")
    endforeach()

    # GATE 3, 收集在这里,因为我们已经打开了文件。
    #
    # Model-facing 代码不能翻译。 Tool 描述是
    # API 与提供方订立的合同; 沙箱 拒绝注释存在以更改
    # MODEL 行为和指示 -following 翻译或降级为非
    # -English 用户的代理, 却改变他们看不到的东西。
    # 见 docs/design/i18n.md §3.
    if(_rel MATCHES "^(src/provider/|src/tool/)")
        list(APPEND _model_facing "${_rel}")
    endif()
endforeach()
list(REMOVE_DUPLICATES _used_ids)
list(REMOVE_DUPLICATES _model_facing)

# ── GATE 1:源代码的每个使用都存在于 目录 ───────────────
#
# A t("foo.bar" 无条目 渲染 屏幕上的标识,
# 这是故意的(显然错误的拍子是空白的),但绝不能 SHIP ,
# 所以它失败了 建设而不是 渲染.
set(_missing "")
foreach(_id IN LISTS _used_ids)
    if(NOT _id IN_LIST _catalog_ids)
        list(APPEND _missing "${_id}")
    endif()
endforeach()
if(_missing)
    message("")
    message("i18n: ids used in code but ABSENT from the English catalog:")
    foreach(_id IN LISTS _missing)
        message("    ${_id}")
    endforeach()
    message("  Add them to kEnglish in src/i18n/startup.cpp.")
    message("  Without an entry these render as the id itself on screen.")
    set(_fail 1)
endif()

# ── GATE 2: 每一ID在其中 目录 用于 ──────────────────────────────
#
# 死弦是翻译时间 花在无物上, 它们静静地积累 -
# - 一个弦留在 目录 很久之后,用它写的行被删除,
# 和每个20名翻译 渲染 一次,我记得
#
# lang.* 和 common.* 命名空间是免责的:它们
# 是选择面板 自己的字符串和共享词汇,从可能动态构建id的视图
# 代码中引用.
set(_unused "")
foreach(_id IN LISTS _catalog_ids)
    if(NOT _id IN_LIST _used_ids)
        if(NOT _id MATCHES "^(lang|common)\\.")
            list(APPEND _unused "${_id}")
        endif()
    endif()
endforeach()
if(_unused)
    message("")
    message("i18n: ids in the catalog that NOTHING uses:")
    foreach(_id IN LISTS _unused)
        message("    ${_id}")
    endforeach()
    message("  Delete them, or use them. A dead string is translator time")
    message("  spent on nothing, twenty times over.")
    set(_fail 1)
endif()

# ── GATE 3: 模型中无翻译 -facing 代码 ──────────────────────────
if(_model_facing)
    message("")
    message("i18n: t() called from MODEL-FACING code:")
    foreach(_f IN LISTS _model_facing)
        message("    ${_f}")
    endforeach()
    message("  Strings the model reads stay English -- tool descriptions are")
    message("  an API contract, and the sandbox denial note exists to change")
    message("  model behaviour, which works best in English. Translating")
    message("  them degrades a non-English user's agent while changing")
    message("  nothing they can see. docs/design/i18n.md section 3.")
    set(_fail 1)
endif()

# ── GATE 4: 不翻译吹吹其宽度预算 ────────────────────────
#
# 该 FAILURE 此 CATCHES 而不是你期望的 结束 -long
# 标签有 不 打开 帧 -- maya 显示列中的切换, a 渲染 以拒绝故意
# 失败的方式确认的试验 71 -column 标签宽度 60.。 帧 举行;
# LABEL 默默的切成这样
# "Auch lesbar aber mit einem absichtli".
#
# 所以,我们... 渲染 闸门完全看不到这种错误,
# 宽度预算是唯一能够看到的东西。 `width_hint`
# 每一英文条目上均是栏数, 其布局设计为周遭;
# 翻译材料经过后, 将切入一个窄处 终端.
#
# 津贴是宽大的(1.6x,四舍五入),因为德语合法运行1.35
# -1.61x,我们不会让翻译写得更差的德语,以适应英语
# -shaped 框中。它捕获的是一个2.5x+ 的大小写
# -- -- 一个翻译 渲染 标签为句子——是翻译错误,
# 而不是布局错误。
#
# COUNTING. 对于拉丁文文字,一个代码点是一栏,两者同意。
# CJK they do not: 额外可读 is 4 codepoints and 8 columns, so a codepoint
# 计数分数一个中文标签 HALF 其真正的宽度 正好穿过
# 溢出的地方 这座大门就存在了
#
# 这么说吧 CJK 获得自己的计数: CJK/Hangul/Kana
# 块值2, 其他所有 1. 这不是一个通用的 Unicode
# 宽度执行( )。 maya 有个,而且 CMake 无法
# 调用 ————是狭义的说法,这三种语言的字符是双倍的.
# -width ,这是真的,也是所有这一切需要。
#
# 电话 渲染 测试是否 FRAME 持有;
# 这是是否 LABEL 也看不到
# 对方的失败
file(GLOB _catalogs "${ROOT}/src/i18n/catalog_*.hpp")
foreach(_cf IN LISTS _catalogs)
    get_filename_component(_cname "${_cf}" NAME_WE)
    string(REPLACE "catalog_" "" _lang "${_cname}")
    set(_wide 0)
    if(_lang MATCHES "^(zh|ja|ko)")
        set(_wide 1)
    endif()
    file(READ "${_cf}" _ctext)

    foreach(_id IN LISTS _catalog_ids)
        # 英文预算的这个ID。
        string(REGEX MATCH "\"${_id}\"[^}]*\"width_hint\"[ \t]*:[ \t]*([0-9]+)"
               _hm "${_startup_text}")
        if(NOT CMAKE_MATCH_1)
            continue()
        endif()
        set(_budget "${CMAKE_MATCH_1}")
        math(EXPR _max "(${_budget} * 16 + 9) / 10")

        string(REGEX MATCH "\"${_id}\"[ \t]*:[ \t]*{[ \t]*\"text\"[ \t]*:[ \t]*\"([^\"]*)\""
               _tm "${_ctext}")
        if(NOT CMAKE_MATCH_1)
            continue()
        endif()
        # COLUMNS 中,从 UTF -8码
        #
        # CMake的LENGTH是BYTES,每一种非-ASCII语言都是错误的,
        # 对每种语言都不同的错误: .
        #
        #   西里尔语 / 拉丁语 1 口音 2 字节,
        #   1 栏 CJK / 汉语 / Kana 3 字节, 2 栏
        #
        # 第一个版本将非 -ASCII 字节 数到三再加倍,
        # 这是正确的 CJK 俄语和分数 "выкл" 4个字符,
        # 4个栏)作为8. 林特向正确的翻译开火 --
        # -- 被这里的翻译抓住,而不是被一个想知道为什么
        # 他们的语言被拒绝的翻译抓住.
        #
        # 所以用他们的铅 字节 计算CODEPOINTS,
        # 然后是重量:一个编码在3+ 字节的编码码是双 -width(
        # 对于CJK 块,这是我们唯一需要它的地方),任何更短的都是单的.
        string(REGEX REPLACE "[^ -~]" "" _ascii "${CMAKE_MATCH_1}")
        string(LENGTH "${_ascii}" _n_ascii)
        string(LENGTH "${CMAKE_MATCH_1}" _n_bytes)
        math(EXPR _n_other_bytes "${_n_bytes} - ${_n_ascii}")
        if(_wide)
            # 3-byte代码点,每栏2个.
            math(EXPR _len "${_n_ascii} + (${_n_other_bytes} / 3) * 2")
        else()
            # 2-byte 代码点(Cyrilic,重音拉丁文),每栏1个.
            math(EXPR _len "${_n_ascii} + ${_n_other_bytes} / 2")
        endif()
        if(_len GREATER _max)
            message("")
            message("i18n: ${_lang}/${_id} is ${_len} chars against a budget of "
                    "${_budget} (max ${_max})")
            message("    ${CMAKE_MATCH_1}")
            message("  It will be TRUNCATED on a narrow terminal -- the frame")
            message("  holds, the label is what gets cut. Shorten it, or raise")
            message("  width_hint in src/i18n/startup.cpp if the English grew.")
            set(_fail 1)
        endif()
    endforeach()
endforeach()

if(_fail)
    message(FATAL_ERROR "i18n lint failed")
endif()

message(STATUS
    "i18n: ${_n_catalog} catalog ids, all used, none model-facing")
