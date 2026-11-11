# secret_modes.cmake - 下方的文件 credentials/ 结构上是0600的。
#
# `~/.agentty/credentials/` 是一个目录,
# 其中 权限 审计中应该没有例外,它有OAuth刷新的标志, API 键和
# 已保存-account 注册表 ;目录为0700,其中每个文件为0600.
#
# 只不过有一个没有 accounts.json 坐在644号的七位6000
# 兄弟姐妹中 因为书写着 std::ofstream -- 创建于 066 & ~
# umask -- 同时 credentials.json ,提供者 -key 保险库,
# 甚至认证锁文件都通过一个明确模式。在实时安装上测量,而不是假设。
#
# 7:00目录中包含的 毕竟是一无所有 但仍然值得修复: 封装
# 是一 `chmod 755` ,或一个 $AGENTTY_HOME
# 指向前一个 -existing 目录,远离 唯一的东西在
# 符号之间 注册表 和另一个本地账户。在深度中防御表示文件
# 模式不是 加载-bearing。在目录模式下。
#
# 这件小衣 CAUSE 而不是症状: 写作者在 author 或 帐号层中创建文件,
# 而不说它想要什么模式。 chmod -after-write 这儿很好,
# 那些 载荷 被封存的密码,所以创建 -to-chmod 窗口不暴露任何信息 -- --
# 因此规则是 "the writer names a mode somewhere" ,
# 没有 "the writer must use open()".
#
# 用法: 制作( make) -DROOT=<repo root > , 转动 -P secret_modes.cmake

if(NOT DEFINED ROOT)
    message(FATAL_ERROR "secret_modes: -DROOT=<repo root> required")
endif()

# 坚持秘密的文件。每个文件必须命名 0600(或 S_IRUSR|S_IWUSR
# ) 某处,因为每个文件都根据 credentials/.
set(_writers
    "${ROOT}/src/io/accounts.cpp"
    "${ROOT}/src/io/auth.cpp"
    "${ROOT}/src/io/keys.cpp"
    "${ROOT}/src/io/account_switch.cpp")

set(_offenders "")

foreach(_f IN LISTS _writers)
    if(NOT EXISTS "${_f}")
        continue()
    endif()
    file(RELATIVE_PATH _rel "${ROOT}" "${_f}")

    # 它是否创建了文件 ? 流 / 打开 / atomic_write 全部数到齐。
    file(STRINGS "${_f}" _creates
         REGEX "std::ofstream|::open\\(|atomic_write")
    if(NOT _creates)
        continue()
    endif()

    # 如果是这样,它必须实际 CALL 的东西设置模式。
    #
    # 匹配模式 值 还不够,而且这样做的尝试是具有启发性的。
    # `0600` 任意点击作者上面的解释性评论。删除注释 LINES
    # 仍然点击
    # `#include <sys/stat.h>   // chmod -- 0600 ...` ,
    # 这是带有后缀注释的代码。两个版本都通过了一个文件,其chmod I已删除。
    #
    # 所以,检查是在CALL, 这是必须存在的东西:
    #
    #   chmod(...) POSIX, 写法打开后(...,
    #   S_IRUSR.) POSIX, 创建 fs::permissions
    #   (...) C++, 由 keys.cpp / account_switch.cpp 使用
    #
    # 可以提到一个值;必须得出调用。
    file(STRINGS "${_f}" _mode_calls
         REGEX "::chmod\\(|[^a-z_]chmod\\(|fs::permissions\\(|O_CREAT[^)]*S_IRUSR")
    set(_modes "")
    foreach(_l IN LISTS _mode_calls)
        string(STRIP "${_l}" _stripped)
        if(NOT _stripped MATCHES "^(//|/\\*|\\*)")
            list(APPEND _modes "${_l}")
        endif()
    endforeach()
    if(NOT _modes)
        list(APPEND _offenders
             "    ${_rel}: creates a file but never sets its mode")
    endif()
endforeach()

if(_offenders)
    string(REPLACE ";" "\n" _report "${_offenders}")
    message(FATAL_ERROR
        "a secrets writer does not set a file mode:\n\n${_report}\n\n"
        "Everything under ~/.agentty/credentials/ is 0600. std::ofstream\n"
        "creates at 0666 & ~umask, so a writer that does not say otherwise\n"
        "lands 0644 -- which is what accounts.json did, among seven 0600\n"
        "siblings. Any of these counts, and all three are already in use:\n"
        "  open(path, O_CREAT, S_IRUSR | S_IWUSR)\n"
        "  chmod(tmp, S_IRUSR | S_IWUSR)        before the rename\n"
        "  fs::permissions(tmp, owner_read | owner_write, replace)\n\n"
        "The check is on the CALL, not the value -- a comment mentioning\n"
        "0600, or an #include whose trailing comment says it, does not count.")
endif()

message(STATUS "secret modes: every credentials/ writer names 0600")
