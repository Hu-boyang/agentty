# assert_static_pie.cmake-用于完全-static Linux 二进制程序的构建-time ELF
# -shape防护。使用 -DBIN=<binary> -DREADELF=<readelf>. 作为 POST_BUILD
# 步骤调用（请参阅 CMakeLists.txt 中的 AGENTTY_FULLY_STATIC 块）
#
# 原因 此 EXISTS：agentty v0.2.7 发送了 Linux x86_64
# "static" 二进制程序，实际上是*动态*-PIE — musl/Alpine GCC
# 驱动程序将 CRT 启动解析为 Scrt1.o 并发出 ET_DYN
# `NEEDED libc.musl-*.so` 但 DROPPED PT_INTERP。
# 它在 Alpine 上运行（加载程序出现在烘焙路径上），并且 SIGSEGV 立即在
# glibc/Debian 上运行（没有 interp → 内核将其映射到随机基础并跳转到*未重定位的*入口点）。
# 构建中没有任何内容捕获它：CI 仅在 aarch64 上运行 readelf，作为非 -fatal 警告，
# 并且从未执行 二进制程序。
#
# CORRECT完全-static agentty 二进制程序是EITHER:
#   * ET_EXEC（默认-static -no-pie构建），OR
#   * ET_DYN (opt-in -static-pie / Termux 版本)，
# 并且在BOTH情况下具有：
#   * NO PT_INTERP (无外部加载器) — 完全自我-contained
#   * NO NEEDED (无动态 库) — 真正静态
# v0.2.7崩溃者是ET_DYN WITH a NEEDED libc.musl并且没有INTERP：
# 没有加载器的动态二进制程序，其SIGSEGV关闭其构建映像。因此，
# 真正重要的鉴别器是 INTERP 和 NEEDED — 不
# 和 ELF 类型。 （也不需要文字 PT_PHDR 行。）
#
# 任何偏差都意味着 链接 降级并且工件将在某些外部 libc 上崩溃。
# BUILD 失败（消息 FATAL_ERROR → non-zero 退出），
# 因此损坏的 二进制程序 永远无法在发布运行程序、
# 本地 AGENTTY_FULLY_STATIC 构建或安装程序的源构建上打包、上传或安装。

if(NOT DEFINED BIN OR NOT EXISTS "${BIN}")
    message(FATAL_ERROR "assert_static_pie: binary not found: '${BIN}'")
endif()
if(NOT DEFINED READELF)
    set(READELF readelf)
endif()

# --- ELF 文件类型：必须是 DYN 或 EXEC（不是 REL/CORE）------------------------------------
execute_process(
    COMMAND ${READELF} -h "${BIN}"
    OUTPUT_VARIABLE _hdr
    RESULT_VARIABLE _hrc
    ERROR_VARIABLE  _herr)
if(NOT _hrc EQUAL 0)
    message(FATAL_ERROR "assert_static_pie: readelf -h failed on '${BIN}':\n${_herr}")
endif()
# Type行是"Type: EXEC (Executable file)"(默认
# -no-pie静态构建)或"Type: DYN (Position-Independent Executable ...)"
# (opt-in -static-pie/Termux构建)。两者都很好；
# 只有 INTERP + NEEDED（在下面检查）决定 二进制程序 是否
# 真正运行其构建映像。 REL/CORE 类型意味着 链接 产生了伪造的东西。
if(NOT _hdr MATCHES "Type:[ \t]*(DYN|EXEC)")
    message(FATAL_ERROR
        "assert_static_pie: FATAL — '${BIN}' is neither ET_EXEC nor ET_DYN; the "
        "link produced an unexpected object type.\n"
        "readelf -h said:\n${_hdr}")
endif()

# ---程序头文件:必须有PT_PHDR，必须NOT有PT_INTERP --------------
execute_process(
    COMMAND ${READELF} -l "${BIN}"
    OUTPUT_VARIABLE _phdrs
    RESULT_VARIABLE _prc
    ERROR_VARIABLE  _perr)
if(NOT _prc EQUAL 0)
    message(FATAL_ERROR "assert_static_pie: readelf -l failed on '${BIN}':\n${_perr}")
endif()
# NOTE:我们故意做NOT，这里要求文字`PT_PHDR` *program-header*行。
# 完全有效的 static-PIE（e.g.glibc 的，
# 通过 _dl_relocate_static_pie 的 self
# -relocates）通常不会在 `readelf -l` 中发出显式的 PHDR 行，
# 因此要求它会 false-fail 好 二进制程序。实际上区分 v0.2.7 损坏的动态
# -PIE 和真正的静态-PIE 的两个信号是PT_INTERP 的ABSENCE（下图）
# 和NEEDED 的ABSENCE（下图）——两者都出现在崩溃形状中，但在静态时都不存在。
if(_phdrs MATCHES "INTERP")
    message(FATAL_ERROR
        "assert_static_pie: FATAL — '${BIN}' has a PT_INTERP program header, i.e. "
        "it wants an external dynamic loader. This is the v0.2.7 crash shape "
        "(a dynamic-PIE masquerading as static): it will SIGSEGV on any host "
        "whose loader path differs from the build image.\n"
        "readelf -l said:\n${_phdrs}")
endif()

# ---动态部分：必须有NO NEEDED条目（真正静态）----------
# ---- readelf -d在没有动态部分时退出non-zero/不打印任何内容；
# 无论哪种方式，我们都需要缺少 NEEDED。
execute_process(
    COMMAND ${READELF} -d "${BIN}"
    OUTPUT_VARIABLE _dyn
    RESULT_VARIABLE _drc
    ERROR_VARIABLE  _derr)
if(_dyn MATCHES "NEEDED")
    message(FATAL_ERROR
        "assert_static_pie: FATAL — '${BIN}' has NEEDED dynamic-library entries, "
        "so libc (and friends) are NOT statically linked. This is exactly the "
        "v0.2.7 x86_64 failure: NEEDED libc.musl-*.so + no PT_INTERP → runs on "
        "the build image, SIGSEGVs everywhere else.\n"
        "readelf -d said:\n${_dyn}")
endif()

message(STATUS
    "agentty: static ELF shape OK — ET_EXEC/DYN, no PT_INTERP, no NEEDED. "
    "Runs standalone on glibc, musl, and 64-bit Pi OS.")
