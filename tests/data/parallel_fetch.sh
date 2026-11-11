#!/bin/sh
# 是否为 沙箱 代理服务获取 CONCURRENTLY?
#
# 第一个版本一次处理一个连接,所以N接头需要N倍长的
# 时间--这是`npm install`做的,一打套接字。
# 八个针对序列代理的平行请求取8x1请求;
# 针对一个平行请求取约1x.
N=8
start=$(date +%s%N)
i=0
while [ $i -lt $N ]; do
    curl -sS --max-time 25 -o /dev/null -w "%{http_code} " https://example.com/ &
    i=$((i + 1))
done
wait
end=$(date +%s%N)
echo
echo "parallel_ms=$(( (end - start) / 1000000 ))"
start=$(date +%s%N)
curl -sS --max-time 25 -o /dev/null -w "one=%{http_code}\n" https://example.com/
end=$(date +%s%N)
echo "single_ms=$(( (end - start) / 1000000 ))"
