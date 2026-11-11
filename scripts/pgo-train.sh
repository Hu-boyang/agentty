#!/usr/bin/env bash
# pgo-train.sh-通过热路径驱动仪表化的agentty，
# 以便PGO计数器覆盖用户实际锻炼的内容：启动、欢迎动画、打字、
# 选择面板导航、线程恢复、Ctrl+L 重绘、退出。
#
# 用途:
#   cmake -B build-pgogen -GNinja -DCMAKE_BUILD_TYPE=Release -DAGENTTY_PGO=generate
#   cmake --build build-pgogen -j$(nproc) --target agentty
#   scripts/pgo-train.sh build-pgogen/agentty
#   GCC：计数器自动保存到 pgo-data/。
#   # Clang: llvm-profdata 合并 -output=pgo-data/agentty.profdata pgo-data/*.profraw
#   cmake -B build-pgouse -GNinja -DCMAKE_BUILD_TYPE=Release -DAGENTTY_PGO=use
#   cmake --build build-pgouse -j$(nproc) --target agentty
#
# 工作负载故意为网络-free（不需要凭据）：它训练输入管道、
# 状态更新函数、视图构建、markdown/layout、渲染器、
# 序列化器和持久化路径——会话的95%的CPU-bound。
set -euo pipefail

BIN="${1:?usage: pgo-train.sh <path-to-instrumented-agentty>}"

python3 - "$BIN" <<'EOF'
import os, pty, select, time, signal, sys, fcntl, termios, struct

bin_path = sys.argv[1]

def drain(fd, ms):
    end = time.time() + ms / 1000
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], max(0.01, end - time.time()))
        if not r:
            break
        try:
            os.read(fd, 65536)
        except OSError:
            break

pid, fd = pty.fork()
if pid == 0:
    os.environ['TERM'] = 'xterm-256color'
    os.execv(bin_path, [bin_path])

fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', 40, 120, 0, 0))

# Boot + welcome animation (sigil raster, RAF loop, layout).
drain(fd, 4000)

# Composer typing burst — input parse, reducer, markdown, diff serializer.
for ch in "the quick brown fox jumps over the lazy dog " * 4:
    os.write(fd, ch.encode())
    drain(fd, 12)
# Word-wise deletion + undo churn.
for _ in range(20):
    os.write(fd, b'\x17')          # Ctrl+W delete word
    drain(fd, 10)

# Model picker: open, filter, navigate, effort cycle, close.
os.write(fd, b'\x1f'); drain(fd, 400)              # Ctrl+/
for ch in "claude":
    os.write(fd, ch.encode()); drain(fd, 30)
for _ in range(6):
    os.write(fd, b'\x1b[B'); drain(fd, 30)         # down
    os.write(fd, b'\x1b[C'); drain(fd, 30)         # effort right
os.write(fd, b'\x1b'); drain(fd, 300)              # Esc

# Provider picker walk.
os.write(fd, b'\x10'); drain(fd, 400)              # Ctrl+P
for _ in range(8):
    os.write(fd, b'\x1b[B'); drain(fd, 25)
os.write(fd, b'\x1b'); drain(fd, 300)

# Thread list (exercises persistence walk + picker) then Esc.
os.write(fd, b'\x1b[106;5u'); drain(fd, 800)       # Ctrl+J (CSI-u)
os.write(fd, b'\x1b'); drain(fd, 300)

# Ctrl+L redraw storm — serializer ghost/recovery paths.
for _ in range(10):
    os.write(fd, b'\x0c'); drain(fd, 80)

# More typing after redraws.
for ch in "final burst of typing to warm the steady-state paths":
    os.write(fd, ch.encode()); drain(fd, 10)

# Quit cleanly so counters flush on exit.
os.write(fd, b'\x03')                              # Ctrl+C
drain(fd, 1500)
try:
    os.kill(pid, 0)
    os.kill(pid, signal.SIGTERM)
except ProcessLookupError:
    pass
os.waitpid(pid, 0)
print("pgo-train: workload complete")
EOF
