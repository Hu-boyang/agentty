#!/usr/bin/env python3
"""hidden_locks：检测 jaal 锁顺序机制看不到的阻塞等待。

  spin：在循环中调用 this_thread::yield()，等待另一线程。

atomic::wait 和 std::call_once 过去也在此列出，现在已用类型约束：
maya::atomic_wait 要求 unlocked，maya::once 的主体作为叶操作执行，
内部加锁或等待会中止。禁用清单拒绝原始写法，无需再逐项人工检查。
自旋无法用同样方式封装；下方各位置已人工确认只等待系统调用级别的短窗口，
并且等待期间不持有 jaal 锁。新增位置必须完成相同检查，并在此说明原因。

用法：hidden_locks.py ROOT
"""

import os
import re
import sys

sys.path.insert(0, os.path.dirname(__file__))
from disk_writes import strip  # noqa: E402

EXTS = (".cpp", ".hpp", ".h")

RULES = {
    "spin": (
        r"\bthis_thread::yield\s*\(",
        {
            "src/util/logx.cpp",           # 只在Dup2交换机在飞行时
            "src/workspace/symbols.cpp",   # 排程器接口边界, 不等待
            "src/provider/chatgpt/codex_oauth.cpp",  # a cancel's shutdown() call
            "include/agentty/util/child_process.hpp",  # a stop/reap 循环
        },
    ),
}


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    root = sys.argv[1]
    compiled = {k: re.compile(p) for k, (p, _) in RULES.items()}
    hits, used = [], set()
    for sub in ("src", "include"):
        for dp, _, files in os.walk(os.path.join(root, sub)):
            for f in sorted(files):
                if not f.endswith(EXTS):
                    continue
                path = os.path.join(dp, f)
                rel = os.path.relpath(path, root).replace(os.sep, "/")
                raw = open(path, encoding="utf-8", errors="replace").read()
                lines = raw.split("\n")
                for no, line in enumerate(strip(raw).split("\n"), 1):
                    for rule, rx in compiled.items():
                        if not rx.search(line):
                            continue
                        if rel in RULES[rule][1]:
                            used.add((rule, rel))
                        else:
                            hits.append(f"{rel}:{no}: [{rule}] {lines[no - 1].strip()}")
    stale = [f"{rel} [{rule}]" for rule, (_, allow) in RULES.items()
             for rel in sorted(allow) if (rule, rel) not in used]
    print("hidden_locks: blocking waits outside jaal's lock order")
    if hits or stale:
        if hits:
            print(f"\n{len(hits)} unchecked site(s). Prove it never waits holding a "
                  "jaal lock (or use maya::guarded::wait_with), then allowlist it:")
            for h in hits:
                print("  " + h)
        if stale:
            print("\nallowlist entries with nothing left to allow (remove them):")
            for s in stale:
                print("  " + s)
        return 1
    print("ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
