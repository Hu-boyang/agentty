#!/usr/bin/env python3
# 盒子里有盖子(2)
#
# 节点 调用 启动时此值为 15 次( 可见于一个跟踪) -c 以主机盟誓, claybin's
# seccomp 配置文件不列出它 -- -- 因此它返回 EPERM 而不是成功,
# 而不是成功。 V8 '启动断言是字面上的 `Check failed: 12 == errno`,
# 即刚执行的调用应把错误码设为 ENOMEM。
# 所以一个音响 交回错误的错 是完全的形状 移动它。
#
# 期望: EPERM在一个盒子中,0在主机上.
import ctypes
import errno

l = ctypes.CDLL(None, use_errno=True)
hdr = ctypes.create_string_buffer(8)      # cap_user_header_t
data = ctypes.create_string_buffer(24)    # cap_user_data_t [2]
# _LINUX_CAPABILITY_VERSION_3版本,pid 0 = 自
ctypes.memmove(hdr, (0x20080522).to_bytes(4, "little") + b"\0\0\0\0", 8)

ctypes.set_errno(0)
rc = l.syscall(125, ctypes.byref(hdr), ctypes.byref(data))
e = ctypes.get_errno()
print("capget rc=", rc, "errno=", errno.errorcode.get(e, e) if rc < 0 else "-")
