#!/usr/bin/env python3
"""约 40 行的模拟 OpenAI 兼容 /v1/embeddings 服务。

用于端到端验证嵌入后端选择器，无需 Ollama、GPU 或企业网关。
服务按请求的维度返回确定性的单位范数向量。

    python3 scripts/fake_embed_server.py --port 8099 --dim 768

随后在 agentty 中按 Ctrl+K -> "Retrieval (RAG)" -> e，配置：
    Backend  = Custom endpoint
    Host     = 127.0.0.1
    Port     = 8099
    Model    = anything
    在 "Test connection" 上按 Enter，应显示启动时设置的维度。

修改 --dim 后重新测试，可以看到身份哈希以及持久化的 .ragdb 文件名随向量空间改变。
"""

import argparse, hashlib, json, math
from http.server import BaseHTTPRequestHandler, HTTPServer

DIM = 768


def embed(text: str, dim: int):
    """确定性的伪嵌入：以哈希设置随机种子，再进行 L2 归一化。"""
    h = hashlib.sha256(text.encode("utf-8")).digest()
    vals = [((h[i % len(h)] + i * 31) % 251) / 251.0 - 0.5 for i in range(dim)]
    norm = math.sqrt(sum(v * v for v in vals)) or 1.0
    return [v / norm for v in vals]


class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("Content-Length", 0) or 0))
        try:
            req = json.loads(body or b"{}")
        except json.JSONDecodeError:
            req = {}
        inp = req.get("input", "")
        texts = inp if isinstance(inp, list) else [inp]

        print(f"  -> {self.path}  {len(texts)} text(s), auth="
              f"{'yes' if self.headers.get('Authorization') else 'no'}")

        out = {
            "object": "list",
            "model": req.get("model", "fake-embed"),
            "data": [
                {"object": "embedding", "index": i, "embedding": embed(t, DIM)}
                for i, t in enumerate(texts)
            ],
            "usage": {"prompt_tokens": 0, "total_tokens": 0},
        }
        payload = json.dumps(out).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, *_):
        pass   # 我们打印自己的、更安静的线路


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8099)
    ap.add_argument("--dim", type=int, default=768)
    args = ap.parse_args()
    DIM = args.dim
    print(f"fake embeddings server on http://127.0.0.1:{args.port}  dim={DIM}")
    print("point agentty at it:  Backend=Custom endpoint  Host=127.0.0.1  "
          f"Port={args.port}")
    HTTPServer(("127.0.0.1", args.port), Handler).serve_forever()
