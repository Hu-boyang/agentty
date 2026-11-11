#!/bin/sh
# A/B 编号 search_code 输油管在判断的 eval 套, BM25 仅(快速,
# 不需要嵌入器)。 tests/rag_eval/ab.sh
set -u
cd "$(dirname "$0")/../.."
B=./build/agentty
run() {  # 1美元标签, 休息: env
    label=$1; shift
    for set in agentty_code agentty_code_heldout; do
        r=$(env AGENTTY_EMBED_BACKEND=disabled AGENTTY_RAG_PERSIST=0 "$@" $B rag-eval tests/rag_eval/$set.json 2>&1 \
            | awk '/hit@1/{h1=$2} /hit@8/{h8=$2} /MRR/{m=$2} END{printf "h@1 %s  h@8 %s  MRR %s", h1, h8, m}')
        printf "%-28s %-22s %s\n" "$label" "$set" "$r"
    done
}
run "baseline"            AGENTTY_RAG_CODE_RERANK=0 AGENTTY_RAG_SPLIT_IDENT=0
run "+split identifiers"  AGENTTY_RAG_CODE_RERANK=0
run "+rerank (no split)"  AGENTTY_RAG_SPLIT_IDENT=0
run "+split +rerank"
