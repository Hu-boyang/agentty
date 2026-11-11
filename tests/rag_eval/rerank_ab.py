#!/usr/bin/env python3
# 离线调整 A/B 弃置时 search_code 游泳池 游泳池里有游泳池 泳池里有泳池
#   AGENTTY_RAG_DUMP_POOL=pool.jsonl agentty 抹布(
#   擦抹布 ) -eval cases.json 蟒蛇3 tests/rag_eval/rerank_ab.py cases.json
# pool.jsonl http://127.0.0.1:8090 [top_n [v] 打印点击@1/点击@8/ MRR
# 单对当前顺序, 重新排序, 和重新排序 code_rerank 的前科, 与当前分数混合。 需要带有串联的服务器
# -style /v1/rerank (马拉马) -server --reranking, TEI, ...).
# 排名第二的`v`打印了每个查询的排名前3。
#
# 千兆赫( B) -reranker-v2-m3 (Q8 ,拉玛 -server )在qwen3上,
# (" ⁇ ") -embedding 池:单靠它排队 headers/tests/docs 第一次(点击@1.65
# - > 38主机,80 - > 25次) -out ); 前面的它会将命中数升到@8到1.0 /.95,
# 但命中数@1不会击败当前命令。没有传输输入。
import json, sys, urllib.request

cases = {c["q"]: set(c["gold"]) for c in json.load(open(sys.argv[1]))}
pools = {}
for line in open(sys.argv[2]):
    r = json.loads(line)
    if r["q"] in cases: pools[r["q"]] = r["c"]
url = sys.argv[3].rstrip("/") + "/v1/rerank"
top_n = int(sys.argv[4]) if len(sys.argv) > 4 else 32

def rerank(q, texts):
    body = json.dumps({"query": q, "documents": texts, "top_n": len(texts)}).encode()
    req = urllib.request.Request(url, body, {"content-type": "application/json"})
    out = [0.0] * len(texts)
    for r in json.load(urllib.request.urlopen(req, timeout=120))["results"]:
        out[r["index"]] = r["relevance_score"]
    return out

def norm(xs):
    lo, hi = min(xs), max(xs)
    return [(x - lo) / (hi - lo) if hi > lo else 0.0 for x in xs]

def files_in_order(cands, scores):
    order = sorted(range(len(cands)), key=lambda i: -scores[i])
    seen, out = set(), []
    for i in order:
        p = cands[i]["path"]
        if p not in seen: seen.add(p); out.append(p)
    return out

def metrics(ranked_by_q):
    h1 = h8 = mrr = 0
    for q, files in ranked_by_q.items():
        gold = cases[q]
        rank = next((i + 1 for i, f in enumerate(files[:8]) if f in gold), 0)
        h1 += rank == 1; h8 += rank >= 1; mrr += 1 / rank if rank else 0
    n = len(ranked_by_q)
    return f"hit@1 {h1/n:.3f}  hit@8 {h8/n:.3f}  MRR {mrr/n:.3f}"

def role_weight(path, q):
    # 镜像 code_rerank 角色前置 : tests/docs/build
    # 文档数少,除非问题问到它们, 头文件 略少一点。
    p, ql = path.lower(), q.lower()
    if ("/test" in p or p.startswith("test")) and "test" not in ql: return 0.55
    if (p.endswith(".md") or p.startswith("docs/")) and not any(w in ql for w in ("doc", "readme", "guide")): return 0.55
    if (p.endswith(".cmake") or "cmakelists" in p) and not any(w in ql for w in ("cmake", "build", "lint")): return 0.6
    if p.endswith((".hpp", ".h")): return 0.9
    return 1.0

def stem(p): return p.rsplit("/", 1)[-1].rsplit(".", 1)[0]

def with_priors(q, cands, scores):
    s = [x * role_weight(c["path"], q) for x, c in zip(scores, cands)]
    hdr = {}
    for x, c in zip(s, cands):
        if c["path"].endswith((".hpp", ".h")): hdr[stem(c["path"])] = max(hdr.get(stem(c["path"]), 0), x)
    return [max(x, hdr[stem(c["path"])] * 1.02) if c["path"].endswith((".cpp", ".cc", ".c")) and stem(c["path"]) in hdr else x
            for x, c in zip(s, cands)]

base, rr = {}, {}
for q, c in pools.items():
    c = c[:top_n]
    pools[q] = c
    base[q] = [x["score"] for x in c]
    # 模型看到的同样文字:路径先,所以它知道它是什么文件.
    rr[q] = rerank(q, [f'{x["path"]}\n{x["text"][:2000]}' for x in c])

print(f"{len(pools)} queries, top {top_n}")
if len(sys.argv) > 5:
    for q in pools:
        rf = files_in_order(pools[q], rr[q])[:3]
        print(f"  {q[:60]:60}  gold={sorted(cases[q])[0]}  rr={rf}")
print(f"  current         {metrics({q: files_in_order(pools[q], base[q]) for q in pools})}")
print(f"  reranker only   {metrics({q: files_in_order(pools[q], rr[q]) for q in pools})}")
print(f"  rr + priors     {metrics({q: files_in_order(pools[q], with_priors(q, pools[q], norm(rr[q]))) for q in pools})}")
for w in (0.3, 0.5, 0.7):
    blend = {q: [w * a + (1 - w) * b for a, b in zip(with_priors(q, pools[q], norm(rr[q])), norm(base[q]))] for q in pools}
    print(f"  blend+priors w={w} {metrics({q: files_in_order(pools[q], blend[q]) for q in pools})}")
