#!/usr/bin/env python3
"""disk_writes：agentty 统一通过 include/agentty/io/shared_file.hpp 写共享文件。

  lock     指定边界之外禁止使用 CrossProcessFileLock、flock()、LockFileEx、
           fcntl F_SETLK* 或 native_file_lock::acquire 加文件锁。
  rename   禁止通过 rename(2)、MoveFileEx 或 fs::rename 发布临时文件，
           避免两个写入方共用固定的 .tmp 名称。
  truncate 禁止通过 ofstream/fopen 截断或追加；读者可能看到半写入文件，
           两个追加方也可能把各自的行拼接在一起。
  setvbuf  禁止 setvbuf(..., _IOFBF/_IOLBF, 0)。C 标准允许空缓冲区配合
           大小 0，但 MSVC UCRT 会视为无效参数并快速终止进程，见 #115。

每条规则下方有简短允许清单，仅包含原语自身，以及迁移或 config move 等
移动整个文件或目录而非写入内容的位置。匹配时忽略注释和字符串。

用法：disk_writes.py ROOT
"""

import os
import re
import sys

EXTS = (".cpp", ".hpp", ".h")

RULES = {
    "lock": (
        r"\bCrossProcessFileLock\b|[^\w.>:]flock\s*\(|\bLockFileEx\b|\bF_(OFD_)?SETLKW?\b"
        r"|native_file_lock::(try_)?acquire\b|\bacquire_within\s*<",
        {
            "src/io/shared_file.cpp",     # 节次
            "src/util/storage_lock.cpp",  # 本届会议的报告 -long "agentty is running" 锁
        },
    ),
    "rename": (
        r"\bfs::rename\s*\(|std::filesystem::rename\s*\(|[^\w.>]::rename\s*\(|[^\w.>:]rename\s*\("
        r"|\bMoveFileExW?\s*\(|\bReplaceFileW?\s*\(",
        {
            "src/io/shared_file.cpp",           # write_atomic / create_exclusive 数据
            "src/tool/util/disk_files.cpp",     # 编辑工具的编目器(保持共鸣链接,模式)
            "include/agentty/tool/util/disk_files.hpp",
            "src/config/inventory.cpp",         # `config move`: 移动整个 dirs, 存储闲置
            "src/rag/adapter.cpp",              # 单曲 -time 移动旧索引文件到 cache/
            "src/util/user_root.cpp",           # 单曲 -time 遗留布局迁移
        },
    ),
    "truncate": (
        r"std::ofstream\b|std::fstream\b|\bios(_base)?::(trunc|app)\b|[^\w]fopen\s*\([^)]*\"[wa]"
        r"|_wfopen\s*\([^)]*L\"[wa]|\bO_TRUNC\b",
        {
            "src/io/shared_file.cpp",
            "src/tool/util/disk_files.cpp",
            "src/util/logx.cpp",                # 日志:一个进程,O_APPEND
            # AGENTTY_STARTUP_TRACE=<path> :
            # dev 数据 -only 时间日志命令行上的用户名,
            # 附加一个进程。 不是共享文件; 没有其它内容读写它。
            "include/agentty/util/startup_trace.hpp",
        },
    ),
    "setvbuf": (
        r"\bsetvbuf\s*\([^;]*_IO[FL]BF\s*,\s*0\s*\)",
        set(),
    ),
}


def strip(code):
    """将注释和字符串字面量置空，保留行结构。"""
    out, i, n = [], 0, len(code)
    while i < n:
        c = code[i]
        if code.startswith("//", i):
            j = code.find("\n", i)
            i = n if j < 0 else j
        elif code.startswith("/*", i):
            j = code.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("\n" * code.count("\n", i, j))
            i = j
        elif c == '"' or c == "'":
            # 空白字面但保留任何新线条( 线条横线)。
            q, j = c, i + 1
            while j < n and code[j] != q:
                j += 2 if code[j] == "\\" else 1
            out.append(q + "\n" * code.count("\n", i, j) + q)
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    root = sys.argv[1]
    compiled = {k: re.compile(p) for k, (p, _) in RULES.items()}
    hits = []
    for sub in ("src", "include"):
        for dp, _, files in os.walk(os.path.join(root, sub)):
            for f in sorted(files):
                if not f.endswith(EXTS):
                    continue
                path = os.path.join(dp, f)
                rel = os.path.relpath(path, root).replace(os.sep, "/")
                raw = open(path, encoding="utf-8", errors="replace").read()
                # 保留引文,这样fopen模式检查就可以看到"w"/"a".
                code = strip(raw)
                modes = raw.split("\n")
                for no, line in enumerate(code.split("\n"), 1):
                    for rule, rx in compiled.items():
                        if rel in RULES[rule][1]:
                            continue
                        probe = " " + line
                        if rule == "truncate" and "fopen" in line:
                            probe = " " + modes[no - 1]   # 需要模式字符串
                        if rx.search(probe):
                            hits.append(f"{rel}:{no}: [{rule}] {modes[no - 1].strip()}")
    print("disk_writes: shared files go through persistence::with_shared_file / write_atomic")
    if hits:
        print(f"\n{len(hits)} violation(s). Use include/agentty/io/shared_file.hpp"
              " ([setvbuf]: pass BUFSIZ, not 0):")
        for h in hits:
            print("  " + h)
        return 1
    print("ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
