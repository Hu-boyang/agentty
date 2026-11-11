#!/bin/sh
# cli_arg_order_test.sh-`agentty run`按任何顺序接受其论点.
#
# 电话 解析器 用于扫描 LEADING 参数运行于 `run`
# 并且停止在第一种选择, 它没有,所以 提示词 只有在没有全球范围的
# 情况发生时,才提出索赔:
#
#     agentty run --agent coder "prompt"        worked
#     agentty run -w dir "prompt"               "unknown arg: prompt"
#     agentty run --agent coder -w dir "prompt" "unknown arg: prompt"
#
# 报告为"agent flag positioning only accepts one version".
# 该规则是真实的,但不可见的,因为错误将XX00120命名为PROMPT
# 而不是命令——所以它被读作一个被否决的提示词,而不是一个被否决的立场.
#
# 这是壳试验 因为 parse_args 内部为 main.cpp
# 值得倾注的是 BINARY 合同,这是用户实际点击的。
#
#   嘘 tests/cli_arg_order_test.sh [path/to/agentty]

set -u
BIN="${1:-./build/agentty}"
[ -x "$BIN" ] || { echo "SKIP: no binary at $BIN"; exit 0; }

fails=0

# 接受: 接受请求 解析 不得报告未知的论据。 --help 保持这个快速和侧面
# -effect 免费的——它同样锻炼 parse_args 和出口,我们离开。
ok() {
    desc="$1"; shift
    out=$("$BIN" "$@" --help 2>&1 | head -1)
    case "$out" in
        unknown\ arg*) printf 'FAIL %-44s -> %s\n' "$desc" "$out"; fails=$((fails+1)) ;;
        *)             printf 'ok   %-44s\n' "$desc" ;;
    esac
}

# 反对:真正的糟糕论点必须仍然失败。 没有这种论点,检验标准就会通过,
# 对一个真正的错误论点也必须通过。 解析器 接受一切的事物 接受一切 接受一切
rejects() {
    desc="$1"; shift
    out=$("$BIN" "$@" 2>&1 | head -1)
    case "$out" in
        unknown\ arg*) printf 'ok   %-44s (rejected)\n' "$desc" ;;
        *)             printf 'FAIL %-44s -> accepted, want reject\n' "$desc"; fails=$((fails+1)) ;;
    esac
}

# 说: 输出 CONTAINS 此信件。 用于空信件 -prompt ,
# 则 对应的 错误,不只是 解析 成功 ——
# `run ""` 用于报告空白 `unknown arg:`。
# 与整个产出匹配,因为横幅( e.g。该 沙箱 可能
# 在此之前。
says() {
    desc="$1"; want="$2"; shift 2
    out=$("$BIN" "$@" 2>&1)
    case "$out" in
        *"$want"*) printf 'ok   %-44s\n' "$desc" ;;
        *)         printf 'FAIL %-44s -> %s\n' "$desc" "$(printf '%s' "$out" | tr '\n' ' ')"
                   fails=$((fails+1)) ;;
    esac
}

echo "argument order:"
ok 'run PROMPT'                       run "p"
ok 'run --agent R PROMPT'             run --agent coder "p"
ok 'run PROMPT --agent R'             run "p" --agent coder
ok 'run PROMPT -w DIR'                run "p" -w /tmp
ok 'run -w DIR PROMPT'                run -w /tmp "p"
ok 'run --agent R -w DIR PROMPT'      run --agent coder -w /tmp "p"
ok 'run -w DIR --agent R PROMPT'      run -w /tmp --agent coder "p"
ok 'run -m MODEL --agent R PROMPT'    run -m some-model --agent tester "p"
ok 'run - (stdin)'                    run -

echo
echo "empty prompt:"
# 空位仍是 提示词 扫描 CLAIMED : 它必须产生真实的 "no prompt" 错误,
# 永远不要 `unknown arg:` 名字是空白的 名字是空的 名字写着"我"
says 'run ""'        'no prompt'      run ""
says 'run "" -w DIR' 'no prompt'      run "" -w /tmp

echo
echo "still strict:"
rejects 'run --bogus'                 run --bogus "p"
rejects 'top-level --bogus'           --totally-bogus

echo
if [ "$fails" -ne 0 ]; then
    echo "$fails failure(s)"
    exit 1
fi
echo "all argument orders accepted, bad args still rejected"
