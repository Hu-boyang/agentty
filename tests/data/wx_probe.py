#!/usr/bin/env python3
# W^X : 是 (中文(简体) ). "map writable, then make it executable" 停下来?
#
# 电话 seccomp 规则检查 mmap/mprotect ARGUMENTS
# 单曲 调用 所以这里的每一步都是无辜的 -wide 财产( MDWE
# )见其序.
import ctypes, errno, mmap

l = ctypes.CDLL(None, use_errno=True)
l.mmap.restype = ctypes.c_void_p
l.mmap.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int,
                   ctypes.c_int, ctypes.c_int, ctypes.c_long]
l.mprotect.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int]
FAIL = ctypes.c_void_p(-1).value
PROT_R, PROT_W, PROT_X = 1, 2, 4
MAP_PRIVATE, MAP_ANON = 2, 0x20

ctypes.set_errno(0)
p = l.mmap(None, 4096, PROT_R | PROT_W | PROT_X, MAP_PRIVATE | MAP_ANON, -1, 0)
print("mmap W+X in one call:",
      "REFUSED" if p == FAIL else "ALLOWED (bad)")

ctypes.set_errno(0)
q = l.mmap(None, 4096, PROT_R | PROT_W, MAP_PRIVATE | MAP_ANON, -1, 0)
if q == FAIL:
    print("mmap W: refused -- cannot test the upgrade")
else:
    ctypes.set_errno(0)
    rc = l.mprotect(ctypes.c_void_p(q), 4096, PROT_R | PROT_X)
    print("mprotect W -> X (two innocent calls):",
          "REFUSED" if rc != 0 else "ALLOWED (bad)")
