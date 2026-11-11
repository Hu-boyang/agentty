#!/usr/bin/env python3
# 危险线条的系统 agentty 真正的姿态是否定的吗?
#
# 每个都是逃生原始的,或者接近一个:
#   io_uring_ * 提交文件 IO 穿过一个戒指 seccomp 过滤器从不看到
#                     ——经典 "filter bypass" 家庭
#   用户错误页面 -fault 在用户空间中处理: a TOCTOU 杠杆
#   process_vm_readv 读取另一个进程 内存 不显示路径
#   pidfd_open/getfd 从另一个进程中偷取描述符
#   open_by_handle_at 需求 CAP_DAC_READ_SEARCH ,
#                     但通过手柄到达任何一个inode -- -- 挂载 源
#   码树 内不包含 memfd_create 单独无害;W ^X 路径通常的第一步
import ctypes, errno

l = ctypes.CDLL(None, use_errno=True)
l.syscall.restype = ctypes.c_long

CASES = [
    ("io_uring_setup",    425, (8, 0)),
    ("io_uring_enter",    426, (0, 0, 0, 0, 0)),
    ("io_uring_register", 427, (0, 0, 0, 0)),
    ("userfaultfd",       323, (0,)),
    ("process_vm_readv",  310, (0, 0, 0, 0, 0, 0)),
    ("process_vm_writev", 311, (0, 0, 0, 0, 0, 0)),
    ("pidfd_open",        434, (1, 0)),
    ("pidfd_getfd",       438, (0, 0, 0)),
    ("open_by_handle_at", 304, (0, 0, 0)),
    ("memfd_create",      319, (0, 0)),
]

for name, nr, args in CASES:
    ctypes.set_errno(0)
    rc = l.syscall(nr, *args)
    e = ctypes.get_errno()
    verdict = "ALLOWED(reached kernel)" if rc >= 0 else errno.errorcode.get(e, str(e))
    print(f"{name:20s} rc={rc:<4} {verdict}")
