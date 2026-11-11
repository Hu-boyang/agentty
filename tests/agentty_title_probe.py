#!/usr/bin/env python3
"""检查 agentty 是否在启动时保存窗口标题，并在退出时恢复。

用法：agentty_title_probe.py BINARY
在 PTY 中启动 agentty，按 ^C 退出并检查原始字节。
标题入栈序列 CSI 22;0t 必须早于应用标题 OSC 0，
退出时必须输出出栈序列 CSI 23;0t。两者顺序正确时返回 0。
"""
import os, pty, select, sys, time

binary = sys.argv[1]
pid, fd = pty.fork()
if pid == 0:
    os.environ["TERM"] = "xterm-256color"
    os.execv(binary, [binary, "--sandbox", "off", "-w", "/tmp"])


def pump(secs, buf):
    end = time.time() + secs
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.02)
        if r:
            try:
                d = os.read(fd, 65536)
            except OSError:
                return buf
            if not d:
                return buf
            buf += d
    return buf


out = pump(2.5, b"")
os.write(fd, b"\x03")          # ^C 退出
out = pump(2.0, out)
try:
    os.waitpid(pid, 0)
except ChildProcessError:
    pass

push, title, pop = out.find(b"\x1b[22;0t"), out.find(b"\x1b]0;agentty"), out.rfind(b"\x1b[23;0t")
print(f"push={push} title={title} pop={pop} bytes={len(out)}")
ok = 0 <= push < title and pop > title
print("OK" if ok else "FAIL")
sys.exit(0 if ok else 1)
