#!/usr/bin/env python3
"""verify_stub_against_real.py：tests/fake_local_server.py 是否忠实于真实服务？

模拟服务器表达了我们对各服务器 API 的理解。若理解有误，依赖它的测试仍会全部通过，
因为测试只能证明模拟实现与自身一致。

本脚本连接真实服务器，对比模拟实现声明的结构与实际返回结构。
仅比较键是否存在及 JSON 类型，不比较因模型或主机而合理变化的具体值。

    python3 tests/verify_stub_against_real.py ollama   http://127.0.0.1:11434
    python3 tests/verify_stub_against_real.py llama    http://127.0.0.1:8080
    python3 tests/verify_stub_against_real.py lmstudio http://127.0.0.1:1234

退出 0 表示真实服务器提供的所有路由都与声明结构一致。

--self-check 模式则将每条声明与模拟服务器比较：

    python3 tests/verify_stub_against_real.py --self-check

该模式只验证模拟实现与声明一致，无法证明它符合现实。
此前二者可能同时悄悄偏离：模拟实现新增字段却没加入 CLAIMS，或 CLAIMS 中的
字段从未由模拟实现提供，后者就会一直显示 skip。接入 ctest 后，这类遗漏会使构建失败，
确保手动对照真实服务器时有可信的检查对象。
"""
import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request

# 每个模式要求什么,作为{路由:[(JSON路径,预期类型)]]].
# 路径使用点; "[]" 表示 "first element of this array".
CLAIMS = {
    "ollama": {
        "GET /api/tags": [
            ("models[].name", str),
            ("models[].model", str),
        ],
        "GET /api/ps": [
            # 已加载窗口。 当模型为居民时才会出现。
            ("models[].context_length", int),
            ("models[].model", str),
        ],
        "POST /api/show": [
            ("model_info", dict),
            # 与SUFFIX匹配,因此测试断言后缀存在.
            ("model_info.*.context_length", (int, float)),
        ],
    },
    "llama": {
        "GET /v1/models": [
            ("data[].id", str),
            ("data[].meta.n_ctx", int),
            ("data[].meta.n_ctx_train", int),
        ],
        "GET /props": [
            ("default_generation_settings.n_ctx", int),
            # 能力对象。 llama.cpp 由"建"来建造 RUNNING
            # 模型的jinja模板与探测器输入和输出的分解(
            # common/jinja/caps.cpp, caps_get ),
            # 因此,关键名称由 caps::to_map ()在该文件中。
            # 如果 上游 重命名一个, agentty 默默地失去每个
            # llama.cpp 模型,然后回到猜测 从 GGUF
            # 文件名 — 正好是替换的bug。 请检查拼写, 大声地。
            ("chat_template_caps", dict),
            ("chat_template_caps.supports_reasoning_effort", bool),
            ("chat_template_caps.supports_tool_calls", bool),
        ],
    },
    "lmstudio": {
        "GET /api/v1/models": [
            ("models[].key", str),
            ("models[].max_context_length", int),
            ("models[].loaded_instances[].config.context_length", int),
            # 能力对象——整个领域 LM 工作室的虫子在里头 `reasoning`
            # 是一个 OBJECT 这里( { allowed_options ,
            # 默认),而不是赤裸的布卢尔·米斯特拉尔(英语:Bull Mistral)使用,
            # 读者过去也要求 is_boolean 跳过其他任何事物,
            # 然后每个模型都经过推断 `publisher/model` 键。
            #
            # 故意被检查为命令:如果 LM 工作室会把它
            # 平整成一个小鸟, 而不是物体手臂 安静地不匹配。
            # 请注意推理 -capable 行随身携带,
            # 用一个指向服务器 已加载.
            ("models[].capabilities", dict),
            ("models[].capabilities.trained_for_tool_use", bool),
            # 电话 OBJECT 与父母分开说明:这种确切类型是什么?
            # declared_reasoning()'s
            # truthy_object 组合符存在,
            # 如果 LM 工作室会把它平整成裸露的鸟眼,
            # 镜头仍然匹配(鸟臂抓住它),而这里的每个评论都变成谎言。
            # 把形状固定下来,这样变化是显而易见的,而不是沉默的。
            ("models[].capabilities.reasoning", dict),
            ("models[].capabilities.reasoning.default", str),
        ],
    },
}

BODIES = {"POST /api/show": {"model": None}}   # 模型填入时间 运行时


def fetch(base, route, model=None):
    method, path = route.split(" ", 1)
    url = base.rstrip("/") + path
    data = None
    if method == "POST":
        body = dict(BODIES.get(route, {}))
        if "model" in body:
            body["model"] = model
        data = json.dumps(body).encode()
    req = urllib.request.Request(
        url, data=data, method=method,
        headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=10) as r:
        return json.loads(r.read())


def resolve(doc, path):
    """遍历点分路径。[] 取第一个元素；* 匹配任意键，
    只要剩余部分能与余下路径匹配，用于 GGUF 架构键。"""
    cur = doc
    parts = path.split(".")
    i = 0
    while i < len(parts):
        p = parts[i]
        if p == "*":
            suffix = ".".join(parts[i + 1:])
            if not isinstance(cur, dict):
                return None, "expected object for '*'"
            for k, v in cur.items():
                if k.endswith("." + suffix) or k == suffix:
                    return v, None
            return None, f"no key ending in '.{suffix}'"
        if p.endswith("[]"):
            key = p[:-2]
            if key:
                if not isinstance(cur, dict) or key not in cur:
                    return None, f"missing '{key}'"
                cur = cur[key]
            if not isinstance(cur, list):
                return None, f"'{key}' is not a list"
            if not cur:
                return None, f"'{key}' is empty"
            cur = cur[0]
        else:
            if not isinstance(cur, dict) or p not in cur:
                return None, f"missing '{p}'"
            cur = cur[p]
        i += 1
    return cur, None


def verify(mode, base, quiet=False):
    """对 base 处的服务器验证 mode 对应的全部声明。

    返回 (checked, bad)。服务器不提供的路由跳过且不计数，
    因为检查关注已提供路由是否符合声明，例如真实 llama.cpp 并不提供 /api/ps。
    """
    claims = CLAIMS[mode]

    # /api/show需要一个模型名称;取第一个服务器列表的名.
    model = None
    if mode == "ollama":
        try:
            tags = fetch(base, "GET /api/tags")
            ms = tags.get("models") or []
            if ms:
                model = ms[0].get("model") or ms[0].get("name")
        except Exception:
            pass

    bad = 0
    checked = 0
    for route, fields in claims.items():
        try:
            doc = fetch(base, route, model)
        except urllib.error.HTTPError as e:
            if not quiet:
                print(f"  skip {route}: HTTP {e.code} (route not served here)")
            continue
        except Exception as e:
            if not quiet:
                print(f"  skip {route}: {e}")
            continue
        for path, want in fields:
            checked += 1
            got, err = resolve(doc, path)
            if err:
                # 不存在的可选性(无已加载)不是谎言。
                print(f"  MISS {route} {path}: {err}")
                bad += 1
                continue
            if not isinstance(got, want):
                wname = getattr(want, "__name__", str(want))
                print(f"  TYPE {route} {path}: stub says {wname}, "
                      f"server sent {type(got).__name__} ({got!r})")
                bad += 1
            elif not quiet:
                print(f"  ok   {route} {path} = {got!r}")
    return checked, bad


# 声称一个真正的服务器回答 但 STUB 故意不,有原因的,
# 没有这个清单 --self-check 将迫使根茎
# 为每个领域服务,这将摧毁它所要覆盖的病例。
SELF_CHECK_EXEMPT = {
    # 电话 llama.cpp 排队只载火车 -time
    # 专用天花板:服务窗口来自/道具,该缺口 IS 窗口探测
    # 器的窃听器。添加 n_ctx 这里删除测试。
    ("llama", "GET /v1/models", "data[].meta.n_ctx"),
}


def self_check():
    """对模拟服务器而非真实服务器验证全部声明。"""
    here = os.path.dirname(os.path.abspath(__file__))
    stub = os.path.join(here, "fake_local_server.py")
    total_bad = 0
    port = 18400
    for mode in CLAIMS:
        port += 1
        proc = subprocess.Popen([sys.executable, stub, mode, str(port)],
                                stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL)
        try:
            base = f"http://127.0.0.1:{port}"
            # 等待听众 而不是睡觉 猜测的数量。
            for _ in range(100):
                try:
                    urllib.request.urlopen(base + "/v1/models", timeout=0.2)
                    break
                except urllib.error.HTTPError:
                    break          # 回答,只是没有这条路线
                except Exception:
                    time.sleep(0.05)
            print(f"== {mode}")
            checked, bad = verify(mode, base, quiet=True)
            exempt = sum(1 for (m, _r, _p) in SELF_CHECK_EXEMPT if m == mode)
            bad -= exempt
            if bad > 0:
                print(f"   {bad} claim(s) the stub does not serve")
                total_bad += bad
            else:
                print(f"   {checked} claims served"
                      + (f" ({exempt} exempt)" if exempt else ""))
        finally:
            proc.terminate()
            proc.wait(timeout=5)
    if total_bad:
        print(f"\n{total_bad} claim(s) unserved by the stub")
        print("either serve the shape in fake_local_server.py, or drop the")
        print("claim from CLAIMS — an unserved claim is verified by nothing")
        return 1
    print("\nstub serves every claim")
    return 0


def main():
    if len(sys.argv) >= 2 and sys.argv[1] == "--self-check":
        return self_check()
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    mode, base = sys.argv[1], sys.argv[2]
    if mode not in CLAIMS:
        print(f"unknown mode '{mode}' (have: {', '.join(CLAIMS)})")
        return 2

    checked, bad = verify(mode, base)
    print(f"\n{checked - bad}/{checked} claims verified against the real server")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
