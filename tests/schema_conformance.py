#!/usr/bin/env python3
"""schema_conformance.py：编解码类型代数是否与协议模式一致？

协议模式本身就是类型代数，编解码 DSL 已表达相同结构：

    record(...)            积类型        T ≅ Π fᵢ : Tᵢ
    sum_tagged("k", ...)   带标签和类型  T ≅ Σ (k = tagᵢ) × Tᵢ
    variant_codec(...)     无标签和类型
    enum_codec(...)        有限单位和    T ≅ 1 + 1 + …
    Maybe<U>               可选类型      U + 1
    List<U>                自由幺半群    U*
    required / optional / defaulted / constant   — 字段模式

因此，一致性要求类型构造逐项对应。仅搜索字段名不足以验证：
注释中提到名称会误通过，结构体声明了字段但未接入 record() 也会误通过。
后者曾实际发生：删除成员的变异仍因编解码行提到同名字段而逃过结构体搜索。

对每个已实现类型检查：

  1. 积类型结构：模式要求的键都在编解码器中，编解码器也不得凭空增加键。
  2. 字段模式：必需字段不能建模为 optional，可选字段不能建模为 required。
  3. 承载类型：C++ 成员必须编码成模式声明的 JSON 类型。
     即使键存在且必需性正确，也不能在协议要求整数时发送字符串。
  4. 和类型取值：枚举编解码器包含的字符串必须与模式完全一致。

  MCP   {"type":"string","enum":[…]}    有限字符串和的第一种写法
  ACP   {"oneOf":[{"const":"…"}, …]}    同一概念的另一种写法，均支持

用法：
    python3 tests/schema_conformance.py --mcp …/modelcontextprotocol/schema/2026-07-28/schema.json --acp …/agent-client-protocol/schema/v1/schema.json

退出 0 表示所有已实现类型的代数一致。未实现类型会跳过：
是否建模 ElicitResult 属于范围选择，错误建模才是缺陷。
"""
import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# ── 读取OUR代数 ──────────────────────────────────────────────────


def cpp_sources(*rel_dirs):
    out = {}
    for rel in rel_dirs:
        base = ROOT / rel
        if not base.exists():
            continue
        for f in base.rglob("*.hpp"):
            out[f] = f.read_text(errors="replace")
    return out


def _balanced(blob, open_idx):
    """从 open_idx 开始，通过大括号配对提取 {...} 块。"""
    depth = 0
    for j in range(open_idx, len(blob)):
        if blob[j] == "{":
            depth += 1
        elif blob[j] == "}":
            depth -= 1
            if depth == 0:
                return blob[open_idx : j + 1]
    return blob[open_idx:]


def codec_body(sources, name):
    """提取 CodecOf<name> 的编解码器主体，不读取结构体。

    字段可能已经声明为 C++ 成员，却没有接入 record()，导致传输数据中缺失。
    这正是要检测的缺陷；只读取结构体会让它通过。
    """
    for _, blob in sources.items():
        m = re.search(r"struct\s+CodecOf<\s*" + re.escape(name) + r"\s*>\s*\{", blob)
        if m:
            return _balanced(blob, m.end() - 1)
    return None


# 一个编码字段:模式+传输键.
FIELD_RE = re.compile(
    r"\b(required|optional|defaulted|constant|meta)\s*\(\s*\"([^\"]+)\"")


def our_product(body):
    """返回记录编解码器的 {key: modality}；不是记录时返回 None。

    并非所有编解码器都使用组合子，因此支持两种形式：

      record(required("k", &T::k), …)     DSL 显式声明字段模式。
      j["k"] = …;                          手写实现，需要推导。

    手写形式用于组合子难以表达的不规则结构，例如 CreateMessageResult 的 content
    可以是单个块或数组。跳过它们会漏掉 17 个已建模类型，因此必须解析。

    手写编解码器只能恢复键集合，无法可靠恢复必需性，例如条件写入和无条件写入
    在此层次难以区分。因此这些键标记为 unknown，仍检查是否存在，
    但不猜测其字段模式。
    """
    if body is None:
        return None
    if "variant_codec" in body or "sum_tagged" in body or re.search(r"\bmatch\s*\(", body):
        return None            # 一种共产物,而不是一种产品
    if "record<" in body:
        return {k: mod for mod, k in FIELD_RE.findall(body)}
    # 手提-written. 键的两种写法:
    #   j["k" =v;转让 JSON j = {{"k" ,
    #   v}, ...}; 扶手( ) -init 第二次
    # 报告缺失 PromptReference.name 即当它就在那里时就缺席
    # `{{"type", "ref/prompt"}, {"name", r.name}}`.
    keys = set(re.findall(r'\b\w+\s*\[\s*"([^"]+)"\s*\]\s*=', body))
    keys |= set(re.findall(r'\{\s*"([^"]+)"\s*,', body))
    if not keys:
        return None            # 不能推断形状; 没什么可比较的
    # 模式不是 这儿可以推断... `if (r.title) j["title"] = ...`
    # 是一个可选字段,看起来与这个级别上需要的字段相同。
    # 报告 KEY SET 仅标出未知,所以进行存在
    # 检查和模式检查时保持沉默,而不是对猜测开火.
    return {k: "unknown" for k in sorted(keys)}


def our_sum_values(body):
    """返回枚举编解码器的传输字符串；不是枚举时返回 None。"""
    if body is None or "enum_codec" not in body:
        return None
    return sorted(set(re.findall(r"EnumMapping<[^>]+>\s*\{[^,]+,\s*\"([^\"]+)\"", body)))


# ── 读取 SCHEMA 的代数 ─────────────────────────────────────────


def defs_of(schema):
    return schema.get("$defs") or schema.get("definitions") or {}


def schema_product(node):
    """返回对象类型的 (required_keys, all_keys)，或 None。"""
    if not isinstance(node, dict):
        return None
    props = node.get("properties")
    if not isinstance(props, dict):
        return None
    return set(node.get("required") or []), set(props)


def schema_sum_values(node):
    """读取有限字符串和类型的允许值，兼容两种模式写法。"""
    if not isinstance(node, dict):
        return None
    vals = node.get("enum")
    if isinstance(vals, list) and node.get("type") == "string":
        return sorted(str(v) for v in vals)
    one_of = node.get("oneOf")
    if isinstance(one_of, list):
        consts = [b["const"] for b in one_of
                  if isinstance(b, dict) and isinstance(b.get("const"), str)]
        # 只有在EVERY臂是弦状凸轮时——oneOf与物体形状
        # 混合的凸轮是标记的结合,而不是枚举.
        if consts and len(consts) == len(one_of):
            return sorted(consts)
    return None


# ── 比较它们 ───────────────────────────────────────────────────────

# 将传输层写成,而不是代码字段.
ENVELOPE = {"jsonrpc", "method", "id"}
# 模式定义 将每个混凝土类型都作为基础;每个混凝块类型都单独检查。
# 我们把它们当作一无所有,所以匹配它们只会发现一个无关的
# `using Result = R;` 模板中。
ABSTRACT = {"Result", "Request", "Notification", "PaginatedRequest",
            "PaginatedResult", "ClientRequest", "ClientNotification",
            "ServerRequest", "ServerNotification", "AgentRequest",
            "AgentNotification", "JSONRPCRequest", "JSONRPCNotification"}


def struct_members(sources, name):
    """为 struct <name> { ... }; 返回 {member: declared C++ type}。

    编解码器提供字段模式，即 required/optional/defaulted；结构体提供承载类型。
    一致性需要同时检查两者：键存在且必需性正确，也可能把应为整数的值编码成字符串，
    从而被对端拒绝。
    """
    for _, blob in sources.items():
        m = re.search(r"\bstruct\s+" + re.escape(name) + r"\b\s*(?::[^{]*)?\{", blob)
        if not m:
            continue
        body = _balanced(blob, m.end() - 1)
        out = {}
        # `Type name;` / `Type name = init;` ——每发言一名.
        for decl in re.finditer(
                r"^\s*((?:const\s+)?[\w:]+(?:\s*<[^;]*?>)?)\s+(\w+)\s*(?:=[^;]*)?;",
                body, re.M):
            out[decl.group(2)] = decl.group(1).strip()
        return out
    return None


# 模式定义 JSON 类型 - > 编码它的 C++ 载体。
# 故意使用白名单: 报告一个未识别载体,而不是假设为精细。
# 比起一个沉默的不匹配,一个可以拓宽表的注释更好。
CARRIERS = {
    "string":  ("std::string", "string"),
    "integer": ("int", "int64", "int32", "size_t", "long", "unsigned"),
    "number":  ("double", "float", "int", "int64"),
    "boolean": ("bool",),
    "array":   ("List", "vector", "array"),
    "object":  ("Json", "map", "object", "pair"),
}


def carrier_ok(schema_type, cpp_type):
    """cpp_type 是否能够编码为 schema_type？

    解包 Maybe<>/List<> 获取载荷。可选性和数量维度由其他检查负责，
    这里仅判断最终承载的数据类型。
    """
    t = cpp_type
    # 命名的新类型( N) ToolCallId, SessionId, MessageId ,...) 包着一个字符串.
    if re.fullmatch(r"\w*(Id|Name|Path|Uri|Version)", t):
        return schema_type == "string"
    inner = re.sub(r"^Maybe<(.*)>$", r"\1", t).strip()
    if schema_type == "array":
        return bool(re.match(r"(List|std::vector)\s*<", inner))
    inner = re.sub(r"^(List|std::vector)\s*<(.*)>$", r"\2", inner).strip()
    pats = CARRIERS.get(schema_type)
    if pats is None:
        return True            # 工会/不受约束——无任何可查
    low = inner.lower()
    if any(p.lower() in low for p in pats):
        return True
    # 嵌入式记录或 枚举 合法承运人, object/string
    # - 但只有这样 不。在此接受任何标识符时,
    # 请 `std::string` 传球 `type: object` ,
    # 这正是错配 值得抓住: 一种滑板,在那里, 对端 期待记录。
    SCALARS = re.compile(
        r"^(std::)?(string|string_view|bool|char|u?int\w*|int\d*_t|"
        r"uint\d*_t|size_t|double|float|long|short)$")
    if SCALARS.match(inner):
        return False
    if schema_type in ("string", "object") and re.fullmatch(r"[\w:]+", inner):
        return True
    return False


def resolve_ref(defs, node, depth=0):
    """沿 $ref 查找它引用的定义。

    MCP 中 140 个属性仅声明 {"$ref": "#/$defs/Annotations"}，没有自己的 type。
    如果只读取 type，会悄悄漏掉这些属性。解析引用后，承载类型检查才能看到目标结构。

    解析设有深度上限，因为模式可以递归，例如内容块嵌套资源再嵌套内容；
    无限追踪可能无法终止。
    """
    seen = 0
    while isinstance(node, dict) and "$ref" in node and seen < 8:
        target = node["$ref"].rsplit("/", 1)[-1]
        nxt = defs.get(target)
        if not isinstance(nxt, dict):
            return node, target
        node, seen = nxt, seen + 1
        if "$ref" not in node:
            return node, target
    return node, None


# 格式我们理解。
#   STRING 字节(base64),uri,uri-template,日期-time
#           ——所有字符串在传输上,所以承运人的检查已经覆盖了它们.
#   NUMERIC  double/float/int32/uint32/int64/uint64 -
#           这些是 WIDTH 提示( 简讯) OpenAPI 样式,而不是字符串格式,
#           它们 DO 限制承运人: `uint32` 在一个 std::int64_t 不错,
#           在 std::string 并非——承运人通过检查捕获量 `type` 反正我还是会去的
# 如此列出的未知格式是可见的,而不是默默忽略的.
KNOWN_FORMATS = {
    "byte", "uri", "uri-template", "date-time", "uuid",
    "double", "float", "int32", "uint32", "int64", "uint64",
}


# 运行时 限制 C++ 类型不能表达,而是用CHOKE
# POINT 代码执行。在这里列出,这样检查器不会被听到,
# 执行站点可以从一个地方发现 — — 没有人在
# 操作的字条就是装饰,但删除检查会完全失去合同。
#
#   <Type>.<field> : 夹子所在的地方
ENFORCED_AT = {
    # `minimum: 0` 被塞进一个TTL。 apply_cache_hint
    # (无状态服务器)和 带类型的 服务器 discover/list 路径,
    # 所以负数永远无法到达 传输 不论来电者经过什么
    "DiscoverResult.ttlMs": "server_stateless.hpp apply_cache_hint + server.hpp",
    "ListToolsResult.ttlMs": "server_stateless.hpp apply_cache_hint",
    "ListPromptsResult.ttlMs": "server_stateless.hpp apply_cache_hint",
    "ListResourcesResult.ttlMs": "server_stateless.hpp apply_cache_hint",
    "ListResourceTemplatesResult.ttlMs": "server_stateless.hpp apply_cache_hint",
    "ReadResourceResult.ttlMs": "server_stateless.hpp apply_cache_hint",
}


def semantic_notes(tname, key, prop, cpp_type):
    """检查类型本身无法表达的约束。

    这些属于运行时契约，不是结构不匹配，因此仅在承载类型尚未保证时给出提示，
    不作为失败处理。

    典型情况是 minimum: 0：C++ 没有非负 int，为 27 个字段分别包装新类型代价过高。
    但无符号承载类型或已声明 uint32 等无符号格式时，约束已经满足，无需再提示。
    否则会产生二十行无人阅读的噪声，使检查失去作用。
    """
    out = []
    lo = prop.get("minimum")
    fmt = prop.get("format")
    if isinstance(lo, (int, float)) and lo >= 0:
        unsigned_carrier = re.search(r"unsigned|size_t|uint", cpp_type, re.I)
        unsigned_format = isinstance(fmt, str) and fmt.startswith("uint")
        enforced = f"{tname}.{key}" in ENFORCED_AT
        if not unsigned_carrier and not unsigned_format and not enforced:
            out.append(f"{tname}.{key}: schema minimum={lo}, carrier "
                       f"{cpp_type} admits negatives (runtime contract)")
    if isinstance(fmt, str) and fmt not in KNOWN_FORMATS:
        out.append(f"{tname}.{key}: unrecognised format {fmt!r} — "
                   f"widen KNOWN_FORMATS after checking what it implies")
    return out


def check(label, schema_path, sources):
    schema = json.loads(Path(schema_path).read_text())
    defs = defs_of(schema)
    tags = set()
    for _, blob in sources.items():
        tags |= set(re.findall(r'sum_tagged<[^>]+>\s*\(\s*"([^"]+)"', blob))

    products = sums = 0
    fails = []
    notes = []

    for name in sorted(defs):
        if name in ABSTRACT:
            continue
        body = codec_body(sources, name)
        if body is None:
            continue                      # 未执行——范围决定
        node = defs[name]

        # ── 有限数额 ──
        want_vals = schema_sum_values(node)
        got_vals = our_sum_values(body)
        if want_vals is not None and got_vals is not None:
            sums += 1
            for v in want_vals:
                if v not in got_vals:
                    fails.append(f"{name}: enum missing {v!r}")
            for v in got_vals:
                if v not in want_vals:
                    fails.append(f"{name}: enum has {v!r}, not in schema")
            continue

        # ── 产品 ──
        want = schema_product(node)
        got = our_product(body)
        if want is None or got is None:
            continue
        want_req, want_all = want
        products += 1

        for k in sorted(want_req - ENVELOPE - tags):
            mod = got.get(k)
            if mod is None:
                fails.append(f"{name}.{k}: REQUIRED by schema, absent from codec")
            elif mod == "optional":        # "unknown" 永远不要在这里开火
                # `optional` 编码一个 Maybe 和 OMITS
                # 空时键——用于 必须 字段为 传输 因为我们的解码员
                # 容忍了这种缺席 并且我们永远也看不到这种地方的违规行为
                fails.append(f"{name}.{k}: required by schema, modelled `optional`")

        want_opt = want_all - want_req
        for k in sorted(want_opt):
            if got.get(k) == "required":   # "unknown" 永远不要在这里开火
                # 镜像:我们会 REJECT 法律信息从 对
                # 端 合法地省略了它。
                fails.append(f"{name}.{k}: optional in schema, modelled `required`")

        # ── 运载者类型 ──
        # 第四维度。 键可以存在, 正确需要, 并且仍然带错 JSON
        # 类型 —— ttlMs 作为字符串, 说, 哪个编码
        # `"3600000"` 中标 对端's 模式定义 说整数。
        # 模式和存在都通过 幸福。
        members = struct_members(sources, name) or {}
        props = node.get("properties") or {}
        for k in sorted(set(got) & set(props) & set(members)):
            prop = props[k]
            if not isinstance(prop, dict):
                continue
            # 依次为$ref 首次. 140MCP属性为赤色的$ref,
            # 没有自己的`type`;单读`type`则跳过每个属性.
            target, refname = resolve_ref(defs, prop)
            st = target.get("type")
            if isinstance(st, list):
                st = next((x for x in st if x != "null"), None)   # 无效
            if isinstance(st, str):
                if not carrier_ok(st, members[k]):
                    via = f" (via {refname})" if refname else ""
                    fails.append(
                        f"{name}.{k}: schema says {st}{via}, codec carries "
                        f"{members[k]}")
            elif refname:
                # 电话 $ref 名称为记录。承运人必须是该记录(或 Maybe/List
                # ——a) 将第1款改为第2款。 std::string 这里会
                # 编码一个标尺 在那里, 对端 正在等待对象。
                inner = re.sub(r"^Maybe<(.*)>$", r"\1", members[k]).strip()
                inner = re.sub(r"^(List|std::vector)\s*<(.*)>$", r"\2", inner).strip()
                if re.fullmatch(r"std::string|bool|int\w*|double|float", inner):
                    fails.append(
                        f"{name}.{k}: schema $ref {refname} (a record), "
                        f"codec carries scalar {members[k]}")
            notes.extend(semantic_notes(name, k, target, members[k]))

        extras = sorted(set(got) - want_all - ENVELOPE - tags - {"_meta"})
        if extras:
            # 另外一个键是VIOLATION,只有在模式定义关闭记录
            # (`additionalProperties: false`)
            # 时才打开,JSON模式定义默认打开,
            # MCP/ACP都依靠它来进行前置兼容——`tasks`和
            # `execution`是我们在稳定化前模拟的草稿特性,
            # 一个不知道它们忽略了键的对端.
            #
            # 报告的方式,因为一个额外的密钥是 ALSO 字型看起来如何
            # ( Q) "temperatuer" 沉默会隐藏它。
            closed = node.get("additionalProperties") is False
            for k in extras:
                msg = f"{name}.{k}: in codec, not in schema"
                if closed:
                    fails.append(msg + " (record is CLOSED — violation)")
                else:
                    notes.append(msg + " (record is open — extension)")

    print(f"[{label}] {products} product(s), {sums} sum(s) checked")
    for n in notes:
        print(f"  note: {n}")
    for f in fails:
        print(f"  {f}")
    return fails


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mcp")
    ap.add_argument("--acp")
    a = ap.parse_args()

    # 电话 模式定义 现场直播 上游 ,而不是卖家——把复制品贴上
    # 会打倒这一点,因为工作是注意何时 上游 MOVES.
    if not any(p and Path(p).exists() for p in (a.mcp, a.acp)):
        print("SKIP: no schema given (clone the spec repos, pass --mcp/--acp)")
        return 0

    bad = []
    if a.mcp and Path(a.mcp).exists():
        bad += check("mcp", a.mcp, cpp_sources("third_party/mcp-cpp/include"))
    if a.acp and Path(a.acp).exists():
        bad += check("acp", a.acp, cpp_sources("third_party/acp-cpp/include"))

    print()
    if bad:
        print(f"{len(bad)} conformance problem(s)")
        return 1
    # 代数的四个维度, 校验:
    #   产品形状需要存在密钥, 没有发明密钥模式
    #   required/optional 匹配该
    #   模式定义 C++ 类型编码到 模式定义's JSON
    #   类型和值 enum 所携带的正数 模式定义 字符串
    #
    # 不 值得命名而不是暗示:
    # 语义限制( e. minimum/maxLength/pattern/format),
    # $ref 追逐着巢中记录, 和任何 模式定义 以中文发表 无
    # 类型的 工会,他们需要真正的 模式定义 行者,不是形状比较.
    print("codec algebra agrees with the schema algebra")
    print("  (product shape · field modality · carrier type · sum values)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
