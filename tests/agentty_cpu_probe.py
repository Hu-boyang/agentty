#!/usr/bin/env python3
"""在真实 PTY 中运行 agentty，越过启动画面后采样 CPU 使用情况。

用法：agentty_cpu_probe.py BINARY [SECONDS]
每 2 秒输出累计 CPU、RSS 和线程数，再输出最后一个窗口内的逐线程 CPU 数据，
便于根据名称定位繁忙线程。
"""
import os, pty, sys, time, select, signal

binary = sys.argv[1]
total = int(sys.argv[2]) if len(sys.argv) > 2 else 30
pid, fd = pty.fork()
if pid == 0:
    os.environ["TERM"] = "xterm-256color"
    os.execv(binary, [binary, "--sandbox", "off", "-w", "/tmp"])

def drain():
    while select.select([fd], [], [], 0)[0]:
        try:
            if not os.read(fd, 65536): return
        except OSError:
            return

def ticks(p):
    with open(f"/proc/{p}/stat") as f:
        s = f.read().rsplit(")", 1)[1].split()
    return int(s[11]) + int(s[12])

def status(p, key):
    with open(f"/proc/{p}/status") as f:
        for line in f:
            if line.startswith(key):
                return line.split()[1]

def thread_ticks(p):
    out = {}
    for t in os.listdir(f"/proc/{p}/task"):
        try: out[t] = ticks(f"{p}/task/{t}")
        except OSError: pass
    return out

hz = os.sysconf("SC_CLK_TCK")
start = time.time()
sent = False
prev = None
while time.time() - start < total:
    drain()
    el = time.time() - start
    if not sent and el > 1.5:
        os.write(fd, b"\r")          # 驳回 沙箱-off 喷出
        sent = True
    time.sleep(0.25)
    if int(el * 4) % 8 == 0:
        try:
            c = ticks(pid)
        except OSError:
            break
        rate = "" if prev is None else f" rate={(c - prev[1]) / hz / (el - prev[0]) * 100:5.1f}%"
        print(f"t={el:5.1f}s cpu={c / hz:6.2f}s rss={int(status(pid, 'VmRSS')) // 1024:4d}MB "
              f"threads={status(pid, 'Threads')}{rate}", flush=True)
        prev = (el, c)
        if el > total - 6 and "last" not in globals():
            last = (time.time(), thread_ticks(pid))

drain()
if "last" in globals():
    t0, before = last
    after = thread_ticks(pid)
    dt = time.time() - t0
    busy = sorted(((after[t] - before.get(t, 0)) / hz / dt * 100, t)
                  for t in after)[::-1][:5]
    print("busiest threads over the last window:")
    for pct, t in busy:
        print(f"  tid {t}: {pct:5.1f}%")
os.kill(pid, signal.SIGTERM)
time.sleep(0.5)
try: os.kill(pid, signal.SIGKILL)
except OSError: pass
os.waitpid(pid, 0)
