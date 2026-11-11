#!/usr/bin/env python3
"""模拟 llama-server / LM Studio，用于验证上下文窗口探测。

返回的数据结构已经对照上游源码确认：
  * llama.cpp tools/server/server-context.cpp get_res_model_info()
      /v1/models -> data[].meta.n_ctx
  * LM Studio 原生 API
      /api/v1/models -> models[].loaded_instances[].config.context_length
"""
import json
import os
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer

MODE = sys.argv[1] if len(sys.argv) > 1 else "llama"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 8080

LLAMA_MODELS = {
    "object": "list",
    "data": [
        {
            "id": "qwen2.5-coder-7b",
            "object": "model",
            "created": 1700000000,
            "owned_by": "llamacpp",
            "meta": {
                "vocab_type": 2,
                "n_vocab": 152064,
                # 故意省略 n_ctx 这里: 列车只载大
                # 火车 -time 天花板。服务窗口来自下面的/道具,
                # 这是我们要守护的臭虫路径。
                "n_ctx_train": 32768,   # 建筑顶棚
                "n_embd": 3584,
                "n_params": 7615616512,
                "size": 4431389696,
                "ftype": 15,
            },
        }
    ],
}

LMSTUDIO_V1 = {
    "object": "list",
    "data": [
        # /v1 shim: 仅限 ARCHITECTURAL 最大值,
        # 且完全没有能力对象。 这就是为什么下面的本地路径必须读取。
        {"id": "qwen/qwen3-coder-30b", "object": "model",
         "max_context_length": 262144},
        {"id": "mistralai/mistral-7b-instruct-v0.3", "object": "model",
         "max_context_length": 32768},
    ],
}

LMSTUDIO_NATIVE = {
    "models": [
        {
            "key": "qwen/qwen3-coder-30b",
            "type": "llm",
            "max_context_length": 262144,
            # LM 工作室的能力对象,从其 REST 这里有两
            # 个形状很重要,而且两者都是故意的:
            #
            #   推理是一种 OBJECT 而不是一个鸟。米斯特拉尔拼写着
            #   与赤裸裸的一样的主张 `true` ,
            #   读者过去用来测试 is_boolean () 跳过其他任何
            #   事物——所以, LM 工作室模型通过推论
            #   `publisher/model` 键,推理由文件名决定。
            #
            #   一个 CANNOT 解释完全省略密钥的
            #   模型(见第二行),所以存在就是声明.
            "capabilities": {
                "vision": False,
                "trained_for_tool_use": True,
                "reasoning": {"allowed_options": ["off", "on"],
                              "default": "on"},
            },
            "loaded_instances": [
                # 已加载 16k,远低于模型支持的.
                {"instance_id": "qwen/qwen3-coder-30b",
                 "status": "loaded",
                 "config": {"context_length": 16384,
                            "eval_batch_size": 512}},
            ],
        },
        # 非 国 家 -reasoning 模型,以及一个 UNLOADED 一,两者都很重要:
        # 失踪 `reasoning` 键必须读作已
        # 声明 NO (不象 "unknown" 能力
        # 必须记录下来,即使没有 已加载 例——能力属于模型,
        # 窗口属于实例。
        {
            "key": "mistralai/mistral-7b-instruct-v0.3",
            "type": "llm",
            "max_context_length": 32768,
            "capabilities": {
                "vision": False,
                "trained_for_tool_use": False,
            },
            "loaded_instances": [],
        },
    ]
}


# Ollama:/api/tags携带NO窗口(核实:ollama api/types.go
# ListModelResponse). /api/ps报告已加载 context_length.
OLLAMA_TAGS = {
    "models": [
        {"name": "qwen2.5-coder:7b", "model": "qwen2.5-coder:7b",
         "modified_at": "", "size": 4431389696, "digest": "abc"},
    ]
}
# /api/show — Ollama 单位 -model 能力探测器。 ModelInfo
# 是吴的地图[弦],所以 encoding/json 发射 每个 数字为浮点64:
# context_length 抵达时是32768.0,不是32768. Ollama 自己的
# 测试( 4) cmd/cmd_test.go )使用浮点64字形来表示这个精确的字段.
OLLAMA_SHOW = {
    "capabilities": ["completion", "tools"],
    "details": {"family": "qwen2", "parameter_size": "7B"},
    "model_info": {
        "general.architecture": "qwen2",
        "general.parameter_count": 7615616512.0,
        "qwen2.context_length": 32768.0,
        "qwen2.embedding_length": 3584.0,
    },
}
OLLAMA_PS = {
    "models": [
        {"name": "qwen2.5-coder:7b", "model": "qwen2.5-coder:7b",
         "size": 4431389696, "digest": "abc",
         "context_length": 16384},
    ]
}

# LiteLLM: /v1/model/info 是一个 DECLARATION
# (代理的配置),不是衡量——它绝不能缩小更大的广告窗口.
#
# / 已经 v1/models 排队是故意的 BARE 除了窗户,
# 这就是股票 LiteLLM 发送,这就是为什么代理需要 /
# v1/model/info 调查过,想说什么能力
LITELLM_V1 = {
    "object": "list",
    "data": [{"id": "gpt-4o", "object": "model", "context_length": 128000}],
}
# model_info 携带 两个 独立的事实,而且长期以来,
# 这个固定装置只携带一个。 -only LiteLLM 不能宣布推理,
# 所以没有测试 站在这个固定点上,可能已经抓住 agentty
# 忽略 `supports_reasoning` ——固定编码了
# 盲点而不是暴露它。现在两个事实,所以探测器都是在两个轴上进行。
LITELLM_INFO = {
    "data": [
        {"model_name": "gpt-4o",
         "model_info": {"max_input_tokens": 8192,      # stale/conservative
                        "supports_reasoning": True}},
    ]
}


# 拉玛 -server ROUTER 模式(多 -model。经核实反对
# tools/server/server-models.cpp 的 get_router_props 返回不带外层包裹的
# / 道具返回一个假人 n_ctx ○所以网络 UI 无法打破; 真正的窗口需要吗 ? model=
# <name> . . 和 (原始内容存档于2018-09-21). models_autoload 默认为
# TRUE ,因此,不自动加载=虚假的查询将 LOAD 模型作为一个 副作用.
ROUTER_MODELS = {
    "object": "list",
    "data": [
        {"id": "qwen3-coder", "object": "model", "owned_by": "llamacpp",
         "status": {"value": "loaded", "args": [
             "llama-server", "--ctx-size", "32768"]}},
        # 一种更笨的模型, 其上下文要大得多: 用户紧凑的模型。
        # 未装入的行有 NO 元块,所以它大小的唯一写的地方是 status.args
        # (server-models.cpp,
        # get_router_models).
        {"id": "gemma3:27b",  "object": "model", "owned_by": "llamacpp",
         "status": {"value": "unloaded", "args": [
             "llama-server", "--ctx-size", "131072"]}},
    ],
}
# 每个模型分配的 已加载。路由器将 一个 一次( 时间
# --models-max 1:一个聊天请求,要求另一个模型将其互换.
ROUTER_LOADED_WINDOW = {"qwen3-coder": 32768, "gemma3:27b": 131072}
ROUTER_STATE = {"loaded": os.environ.get("ROUTER_LOADED", "qwen3-coder")}
ROUTER_PROPS_BARE = {
    "role": "router",
    "max_instances": 1,
    "models_autoload": True,
    "model_alias": "llama-server",
    "model_path": "none",
    "default_generation_settings": {"params": {}, "n_ctx": 0},
}
ROUTER_PROPS_PER_MODEL = {
    "qwen3-coder": 32768,
    # 宝石3: 27b不是 已加载 - > 错误,必须跳过,而不是默认。
}


# llama.cpp的chat_template_caps,
# 与caps.cpp中的common/jinja/caps.h+caps::
# to_map()进行校验. RUNS的服务器是该模型自己的jinja模板,
# 与探测器输入值相对应,在加载时,将输出分辨出来,看它的反应,所以这是对模板的衡量,
# 它实际上将应用于我们的要求——而不是任何元数据带类型的英寸.
#
# 这是 仅 能力 llama.cpp 任何地方的申报:其/
# v1/models 光线是光线 id + 元,这就是为什么所有的模型 都
# 通过 推论a GGUF 文件名。
LLAMA_TEMPLATE_CAPS = {
    "supports_string_content": True,
    "supports_typed_content": False,
    "supports_tools": True,
    "supports_tool_calls": True,
    "supports_parallel_tool_calls": True,
    "supports_system_role": True,
    "supports_preserve_reasoning": False,
    "supports_reasoning_effort": True,
    "supports_object_arguments": False,
}


class H(BaseHTTPRequestHandler):
    def _send(self, obj, code=200):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        # /api/show 是一个 POST 奥尔玛的 API.
        path = self.path.split("?")[0]
        if MODE == "llama131k":
            if path not in ("/v1/chat/completions", "/chat/completions"):
                return self._send({"error": "not found"}, 404)
            n = int(self.headers.get("Content-Length") or 0)
            body = json.loads(self.rfile.read(n) or b"{}")
            usage = {"prompt_tokens": 20000, "completion_tokens": 1,
                     "prompt_tokens_details": {"cached_tokens": 0}}
            if not body.get("stream"):
                return self._send({"id": "x", "object": "chat.completion",
                                   "model": body.get("model", ""),
                                   "choices": [{"index": 0, "finish_reason": "stop",
                                                "message": {"role": "assistant", "content": "ok"}}],
                                   "usage": usage})
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.end_headers()
            for frame in ({"choices": [{"index": 0, "delta": {"content": "ok"}}]},
                          {"choices": [{"index": 0, "delta": {}, "finish_reason": "stop"}]},
                          {"choices": [], "usage": usage}):
                self.wfile.write(b"data: " + json.dumps(frame).encode() + b"\n\n")
            self.wfile.write(b"data: [DONE]\n\n")
            self.wfile.flush()
            return
        if MODE == "ollama" and path == "/api/chat":
            n = int(self.headers.get("Content-Length") or 0)
            body = json.loads(self.rfile.read(n) or b"{}")
            self.send_response(200)
            self.send_header("Content-Type", "application/x-ndjson")
            self.end_headers()
            model = body.get("model", "")
            for frame in ({"model": model, "message": {"role": "assistant", "content": "ok"}, "done": False},
                          {"model": model, "message": {"role": "assistant", "content": ""},
                           "done": True, "done_reason": "stop",
                           "prompt_eval_count": 20000, "eval_count": 1}):
                self.wfile.write(json.dumps(frame).encode() + b"\n")
            self.wfile.flush()
            return
        if MODE == "ollama" and path == "/api/show":
            n = int(self.headers.get("Content-Length") or 0)
            if n:
                self.rfile.read(n)
            return self._send(OLLAMA_SHOW)
        if MODE == "router" and path in ("/v1/chat/completions", "/chat/completions"):
            # 自动装入:要求的型号替换住家型号.
            n = int(self.headers.get("Content-Length") or 0)
            body = json.loads(self.rfile.read(n) or b"{}")
            name = body.get("model", "")
            if name not in ROUTER_LOADED_WINDOW:
                return self._send({"error": {"message": "model not found"}}, 400)
            ROUTER_STATE["loaded"] = name
            return self._send({"id": "x", "object": "chat.completion", "model": name,
                               "choices": [{"index": 0, "finish_reason": "stop",
                                            "message": {"role": "assistant", "content": "ok"}}]})
        self._send({"error": "not found"}, 404)

    def do_GET(self):
        path = self.path.split("?")[0]
        if MODE == "llama131k":
            # llama.cpp始于-c 131072,位于Ollama的港口(#73):
            # 没有/api/tags,一个131k的服务窗口,n_ctx_train更大仍然.
            if path == "/props":
                return self._send({
                    "default_generation_settings": {"n_ctx": 131072},
                    "total_slots": 1,
                    "model_alias": "qwen2.5-coder-7b",
                    "chat_template_caps": LLAMA_TEMPLATE_CAPS,
                })
            if path in ("/v1/models", "/models"):
                rows = json.loads(json.dumps(LLAMA_MODELS))
                rows["data"][0]["meta"]["n_ctx_train"] = 262144
                return self._send(rows)
            if path == "/health":
                return self._send({"status": "ok"})
            return self._send({"error": "not found"}, 404)
        if MODE == "llama":
            if path == "/props":
                return self._send({
                    "default_generation_settings": {"n_ctx": 8192},
                    "total_slots": 1,
                    "model_alias": "qwen2.5-coder-7b",
                    "chat_template_caps": LLAMA_TEMPLATE_CAPS,
                })
            if path in ("/v1/models", "/models"):
                return self._send(LLAMA_MODELS)
            if path == "/health":
                return self._send({"status": "ok"})
        elif MODE == "ollama":
            if path in ("/v1/models", "/models"):
                return self._send({"object": "list", "data": [
                    {"id": "qwen2.5-coder:7b", "object": "model"}]})
            if path == "/api/tags":
                return self._send(OLLAMA_TAGS)
            if path == "/api/ps":
                return self._send(OLLAMA_PS)
        elif MODE == "router":
            if path in ("/v1/models", "/models"):
                rows = json.loads(json.dumps(ROUTER_MODELS))
                for r in rows["data"]:
                    up = r["id"] == ROUTER_STATE["loaded"]
                    r["status"]["value"] = "loaded" if up else "unloaded"
                return self._send(rows)
            if path == "/props":
                from urllib.parse import parse_qs, urlparse
                q = parse_qs(urlparse(self.path).query)
                name = (q.get("model") or [""])[0]
                if not name:
                    return self._send(ROUTER_PROPS_BARE)
                # 除非自动加载被明确禁用——否则拒绝回答——镜
                # 像真实的服务器,其中省略了模型的LOADS.
                autoload = (q.get("autoload") or ["true"])[0]
                if autoload not in ("false", "0"):
                    print("  !! would have AUTOLOADED %s" % name, file=sys.stderr)
                w = ROUTER_LOADED_WINDOW.get(name) if name == ROUTER_STATE["loaded"] else None
                if w is None:
                    return self._send({"error": {"message": "model is not loaded"}}, 400)
                # Per-model盖:每个型号在路由器上都有它的OWN
                # 聊天模板,所以上面的赤裸/道具假人没有描述它们.
                # 只有LOADED型号才到这里(除此之外,400s以上的分支,
                # 将router_validate_model与自动载荷=
                # 假物匹配),所以卸下的一个型号宣布什么也没有,
                # 必须在客户端上停留UNKNOWN,而不是"no reasoning".
                caps = dict(LLAMA_TEMPLATE_CAPS)
                caps["supports_reasoning_effort"] = name == "qwen3-coder"
                return self._send({
                    "default_generation_settings": {"n_ctx": w},
                    "chat_template_caps": caps,
                })
        elif MODE == "litellm":
            if path in ("/v1/models", "/models"):
                return self._send(LITELLM_V1)
            if path == "/v1/model/info":
                return self._send(LITELLM_INFO)
        else:
            if path in ("/v1/models", "/models"):
                return self._send(LMSTUDIO_V1)
            if path == "/api/v1/models":
                return self._send(LMSTUDIO_NATIVE)
        self._send({"error": "not found"}, 404)

    def log_message(self, *a):
        print("  %s %s" % (self.command, self.path), file=sys.stderr)


if __name__ == "__main__":
    print(f"fake {MODE} server on :{PORT}", file=sys.stderr)
    HTTPServer((os.environ.get("BIND", "127.0.0.1"), PORT), H).serve_forever()
