#!/bin/sh
# 黄金 快照 如何解释 agentty 外观:每张工具卡
# 加上屏幕的铬,由 tool_gallery 带有颜色( E)
# SGR 包含),因为颜色是这些表面的大部分。
# 改变事物看起来如何显示为 diff。
#
#   tests/ui_snapshots.sh build/tool_gallery 检查
#   tests/ui_snapshots.sh build/tool_gallery --update # 接受新输出
#
# 固定宽度,主题和终端回應,因此每个机器的输出都是
# 一样的.
set -eu
gallery=${1:?usage: ui_snapshots.sh <tool_gallery> [--update]}
mode=${2:-check}
here=$(cd "$(dirname "$0")" && pwd)
dir="$here/fixtures/ui_snapshots"
mkdir -p "$dir"
export COLS=92 THEME=Srcery FREEZE_CLOCK=1 TOOL_CORPUS="$here/fixtures/tool_corpus.json"
unset TERM_FG TERM_BG TERM_PALETTE

# 名称 | 参数
cases='
cards-typical|--kind typical
cards-large|--kind large
cards-failed|--kind failed
running|--running --kind typical bash edit grep
ask|--ask --kind typical bash edit web_fetch git_commit
chrome|--chrome
prose|--prose
welcome|--welcome
panels|--panels
native|--kind typical edit grep
'
fail=0
tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT
echo "$cases" | while IFS='|' read -r name args; do
    [ -z "$name" ] && continue
    if [ "$name" = native ]; then
        THEME=native "$gallery" $args >"$tmp" 2>&1
    else
        "$gallery" $args >"$tmp" 2>&1
    fi
    snap="$dir/$name.ansi"
    if [ "$mode" = --update ]; then
        cp "$tmp" "$snap"
        echo "updated $name"
    elif [ ! -f "$snap" ]; then
        echo "MISSING $name (run with --update)"; exit 1
    elif ! cmp -s "$tmp" "$snap"; then
        echo "CHANGED $name:"
        diff "$snap" "$tmp" | sed -n '1,20p' | cat -v
        exit 1
    else
        echo "ok      $name"
    fi
done || fail=1
exit $fail
