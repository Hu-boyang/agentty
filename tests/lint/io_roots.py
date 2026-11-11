#!/usr/bin/env python3
"""io_roots：只有 io_roots.txt 列出的文件可以创建 Io。

agentty::Io（include/agentty/util/io.hpp）表示执行 I/O 的权限。
接触外部世界的函数需要该令牌，状态更新函数和视图不会获得它。
为保证这一点，唯一创建途径 IoAccess 只能出现在 io_roots.txt 指定文件中。

测试代表宿主，因此豁免。未列出的文件使用 IoAccess，或已列出的文件不再使用它，
都会导致检查失败，使清单始终与实际需求一致。
"""
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1])
listed = {}
for line in (root / "tests/lint/io_roots.txt").read_text().splitlines():
    line = line.split("#", 1)[0].strip()
    if line:
        listed[line] = True

pat = re.compile(r"\bIoAccess\b")
comment = re.compile(r"//.*")
found = set()
# 测试可以自由进行:它们代表宿主。
for top in ("src", "include"):
    for p in (root / top).rglob("*"):
        if p.suffix not in (".cpp", ".hpp", ".h"):
            continue
        rel = p.relative_to(root).as_posix()
        for line in p.read_text(errors="replace").splitlines():
            if pat.search(comment.sub("", line)):
                found.add(rel)
                break

bad = sorted(found - listed.keys())
stale = sorted(listed.keys() - found)
if bad:
    print("io_roots: these files mint an Io but are not IO entry points:")
    for f in bad:
        print("  " + f)
    print("Take an Io parameter instead, or (if this really is an entry point:"
          " an effect body, a tool body, main, a test) add it to"
          " tests/lint/io_roots.txt with a comment saying why.")
if stale:
    print("io_roots: listed but no longer mint an Io (drop them):")
    for f in stale:
        print("  " + f)
sys.exit(1 if bad or stale else 0)
