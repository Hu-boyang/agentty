#!/bin/sh
# search_code / search_docs 运行评分的
# evals 进入 tests/rag_eval 关键词 -only (没有嵌入器,
# 刮伤) AGENTTY_HOME ),如果击中@8或 MRR 地板
# 在今天的数下稍稍坐落; 当变化使搜索更好时会提高。
#   使用:rag_eval_guard.sh <agentty> <repo root> 使用:
set -u
A="$1"; ROOT="$2"
S=$(mktemp -d "${TMPDIR:-/tmp}/agentty_rag_eval_XXXXXX")
trap 'rm -rf "$S"' EXIT
export AGENTTY_HOME="$S/home" AGENTTY_EMBED_BACKEND=disabled AGENTTY_DOCS_DIR="$ROOT/docs"
# 仅凭文献:内存的事实和技能,结果取决于方框。
export AGENTTY_RAG_MEMORY=0 AGENTTY_RAG_SKILLS=0 AGENTTY_RAG_MCP=0
mkdir -p "$AGENTTY_HOME"
cd "$ROOT" || exit 1

fails=0
# 设定点击@ 8 -floor  MRR-floor
while read -r set h8min mrrmin; do
    out=$("$A" rag-eval "tests/rag_eval/$set.json" 2>&1)
    h8=$(printf '%s\n' "$out" | awk '/hit@8/{print $2}')
    mrr=$(printf '%s\n' "$out" | awk '/MRR/{print $2}')
    if [ -z "$h8" ] || [ -z "$mrr" ]; then
        echo "FAIL $set: no result"; printf '%s\n' "$out" | tail -5; fails=$((fails+1)); continue
    fi
    if awk -v a="$h8" -v b="$h8min" -v c="$mrr" -v d="$mrrmin" 'BEGIN{exit !(a>=b && c>=d)}'; then
        echo "ok   $set  hit@8 $h8 (>= $h8min)  MRR $mrr (>= $mrrmin)"
    else
        echo "FAIL $set  hit@8 $h8 (want >= $h8min)  MRR $mrr (want >= $mrrmin)"
        printf '%s\n' "$out" | grep -A1 MISS
        fails=$((fails+1))
    fi
done <<'EOF'
agentty_code          0.88  0.67
agentty_code_heldout  0.90  0.67
agentty_docs          0.90  0.67
agentty_docs_heldout  0.95  0.82
EOF
exit $fails
