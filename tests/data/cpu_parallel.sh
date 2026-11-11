#!/bin/sh
# 默认值是否 cpu.weight 慢一点,别无他法 CPU?
#
# 不可更改: 重心只决定谁在 CONTENTION 下输掉。
# 12 个核心上的工作, 计时。 与同一个脚本相比, 运行在框外。
start=$(date +%s%N)
i=0
while [ $i -lt 12 ]; do
    python3 -c 'sum(range(12000000))' &
    i=$((i + 1))
done
wait
end=$(date +%s%N)
echo "elapsed_ms=$(( (end - start) / 1000000 ))"
