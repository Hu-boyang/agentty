#!/bin/sh
# 构建和运行 agentty's libFuzzer 绳索(clang, ASan+UBSan).
#   嘘 tests/fuzz/run.sh [每条绳索秒,默认60]
# build-fuzz/crashes/<harness> /; 重运行一个 build-fuzz/fuzz_text
#   build-fuzz/crashes/fuzz_text/crash-...
# 需要串联和 nlohmann/json 系统: 头文件 ,或 ,或 NLOHMANN_INCLUDE=<dir>.
set -eu
cd "$(dirname "$0")/../.."
SECS=${1:-60}
OUT=build-fuzz
mkdir -p "$OUT"

# nlohmann/json : 系统复制需要否 标志 (增加/增加) usr/include 以
# -isystem 断开libstdc++'s # include_next ),另外还有一份被收集的cmake.
NJ=${NLOHMANN_INCLUDE:-}
if [ -z "$NJ" ]; then
    for d in build/_deps/nlohmann_json-src/include \
             third_party/rag-cpp/build/_deps/nlohmann_json-src/include; do
        [ -f "$d/nlohmann/json.hpp" ] && { NJ=$d; break; }
    done
fi
if [ -n "$NJ" ]; then NJ_FLAG="-isystem $NJ"
elif echo '#include <nlohmann/json.hpp>' | clang++ -std=c++20 -fsyntax-only -x c++ - 2>/dev/null; then NJ_FLAG=""
else echo "run.sh: nlohmann/json not found; set NLOHMANN_INCLUDE" >&2; exit 2; fi

INC="-Iinclude -Ithird_party/maya/include -Ithird_party/maya/third_party/jaal/include \
 -isystem third_party/mcp-cpp/include $NJ_FLAG"
FLAGS="-std=c++26 -O1 -g -fno-omit-frame-pointer -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined"
printf '%s\n' "$INC" > "$OUT/include_flags"   # 一人的电话 -off 重写: val clang++ $( cat...)

clang++ $FLAGS $INC tests/fuzz/fuzz_text.cpp \
    src/domain/model_name.cpp src/tool/util/partial_json.cpp \
    src/tool/util/utf8.cpp src/util/base64.cpp \
    -o "$OUT/fuzz_text"

# richpaste: 编译 整个 库 (减去其他平台) 进入了控制,
# 仪器,所以它的 解析器 被覆盖到结束。
RP=third_party/richpaste
RP_SRC=""
for f in core sha256 io io_posix wire clipboard wayland x11 image inband armor agent store term report; do
    RP_SRC="$RP_SRC $RP/src/$f.cpp"
done
clang++ -std=c++23 -O1 -g -fno-omit-frame-pointer -fsanitize=fuzzer,address,undefined \
    -fno-sanitize-recover=undefined -I$RP/include tests/fuzz/fuzz_richpaste.cpp $RP_SRC \
    -lpthread -ldl -o "$OUT/fuzz_richpaste"

# 编号 沙箱 代理服务器 解析器。这些内容改为 字节 编号 GUEST 写着,
# 这是唯一的攻击者 -controlled 解析器 位于主机侧面 沙箱 ,
# 因此它得到与剪贴板格式相同的处理。
#
# AGENTTY_NETPROXY_PARSERS_ONLY 编译
# 仅仅是文件的纯半部分:没有套接字、没有命名空间、没有记录或 线程
# 池 改为 链接 反对。 解析器 内容是敌对的, 保持他们的控制
# 使得它成为两个 -file 链接 而不是一半的 二进制程序.
clang++ $FLAGS $INC -DAGENTTY_NETPROXY_PARSERS_ONLY=1 tests/fuzz/fuzz_netproxy.cpp \
    src/tool/util/sandbox_netproxy.cpp \
    -o "$OUT/fuzz_netproxy"

# 以井为天蝎子播种 -formed 示例。
clang++ -std=c++23 -O1 -I$RP/include tests/fuzz/seeds.cpp $RP_SRC -lpthread -ldl -o "$OUT/seeds"
"$OUT/seeds" "$OUT/corpus/fuzz_richpaste" "$OUT/corpus/fuzz_text"

fails=0
for h in fuzz_text fuzz_richpaste fuzz_netproxy; do
    mkdir -p "$OUT/corpus/$h" "$OUT/crashes/$h"
    echo "== $h ($SECS s)"
    if ! "$OUT/$h" -max_total_time="$SECS" -max_len=4096 -timeout=10 \
           -artifact_prefix="$OUT/crashes/$h/" -print_final_stats=1 \
           "$OUT/corpus/$h" > "$OUT/$h.log" 2>&1; then
        echo "FAIL $h:"; grep -E "ERROR|runtime error|SUMMARY|#[0-9]+ 0x.* in " "$OUT/$h.log" | head -20
        fails=$((fails + 1))
    else
        tail -4 "$OUT/$h.log"
    fi
done
exit $fails
