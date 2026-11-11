#!/usr/bin/env python3
"""agentty 引入的库不持有运行时，规则见 docs/PROTOCOL_LIBRARIES.md。

  submodule_purity  对各库 src/ 和 include/ 执行 R1-R5，无允许清单。
  no_duplication    R7：mcp-cpp 与 acp-cpp 不复制 jsonrpc-cpp 或彼此的实现。
  jsonrpc_leaf      jsonrpc-cpp 只包含标准库、nlohmann 和自身头文件；
                    agentty 中仅 rpc/mcp/acp 集成模块可以包含它。
  spawn_via_jaal    _WIN32 分支之外，禁止手动使用 fork、exec*、posix_spawn、
                    popen、system 或 waitpid；jaal 当时尚无 Windows 后端。
  no_sleep_poll     _WIN32 分支之外，只等待事件，禁止 sleep_for/usleep 轮询。

匹配前移除注释和字符串字面量，因此文档和错误消息可以提及任何名称。

用法：protocol_libraries.py ROOT {purity|duplication|jsonrpc_leaf|spawn_via_jaal|no_sleep_poll}
"""
import os
import re
import sys

LIBS = ["mcp-cpp", "acp-cpp", "rag-cpp", "jsonrpc-cpp", "claybin"]
EXTS = (".cpp", ".hpp", ".h", ".c", ".mm", ".cc")


def relpath(path, start):
    """为 os.path.relpath 在所有系统上统一使用 / 分隔符。
    下方前缀表采用 POSIX 写法；Windows 原本返回反斜杠，导致所有前缀无声失配。"""
    return os.path.relpath(path, start).replace(os.sep, "/")

# 规则 - > regex 超过已剥离的代码。每行都添加一个前置空间,
# 所以"not preceded by an identifier"可以写为 [ ⁇ w. >:]。
RULES = {
    "R1 thread":  r"std::j?thread\b(?!::)|\.detach\(\)|std::async\b|\bpthread_create\b|\bCreateThread\b"
                  r"|\b_beginthread(ex)?\b|#\s*pragma\s+omp\b|std::execution::par\b|\bdispatch_async\b",
    "R1 lock":    r"std::(shared_|recursive_|timed_|recursive_timed_)?mutex\b|condition_variable"
                  r"|std::(unique|scoped|shared)_lock\b",
    "R1 atomic":  r"std::atomic|\batomic_flag\b",
    "R1 header":  r"#\s*include\s*<(thread|mutex|shared_mutex|future|condition_variable|atomic"
                  r"|semaphore|latch|barrier)>",
    "R1 tls":     r"\bthread_local\b",
    "R1 wait":    r"std::(shared_)?future\b|std::promise\b|std::packaged_task\b|\bstop_source\b"
                  r"|std::latch\b|std::barrier\b|_semaphore\b",
    "R2 hook":    r"\bset_runtime\b|\bset_executor\b|\b(struct|class)\s+(Runtime|Executor)\b",
    "R4 clock":   r"\b(steady|system|high_resolution|utc|file)_clock\s*::\s*now\b|\bclock_gettime\b"
                  r"|\bgettimeofday\b|\bGetTickCount|\bQueryPerformanceCounter\b|[^\w.>:]time\s*\(\s*(nullptr|NULL|0)\s*\)",
    "R5 sleep":   r"\bsleep_for\b|\bsleep_until\b|[^\w.>:]usleep\s*\(|\bnanosleep\b|[^\w.>:]Sleep\s*\(",
    "R5 process": r"[^\w.>:]v?fork\s*\(|::v?fork\s*\(|[^\w.>]exec[lv]p?e?\s*\(|\bposix_spawn|\bCreateProcess"
                  r"|[^\w.>:]_?popen\s*\(|std::system\s*\(",
    "R5 socket":  r"[^\w.>]socket\s*\(\s*(AF_|PF_)|\bWSAStartup\b",
    "R5 env":     r"\b(secure_)?getenv\b|\b_wgetenv\b|[^\w.>]environ\b",
}

# R6 : 协议 库 没有文件 IO. mcp-cpp '工具只通过主机到达磁盘'
# FileSystem (mcp/tools/files.hpp。不适用。
# rag-cpp (一个存储引擎:其索引文件及 WAL 或 工作),或 claybin (
# 一个监狱,它能容纳东西)
FILE_IO = re.compile(
    r"std::(i|o)?fstream\b|std::filesystem::(exists|status|symlink_status|is_regular_file|is_directory"
    r"|is_symlink|is_empty|file_size|last_write_time|weakly_canonical|canonical|absolute|relative"
    r"|current_path|temp_directory_path|read_symlink|create_director(y|ies)|remove(_all)?|rename|copy"
    r"|copy_file|resize_file|permissions|space)\s*\(|std::filesystem::(recursive_)?directory_iterator\b"
    # AC 调用:非成员(.x / - >x),而非声明本身名称(其前作的回转类型,
    # 如`std::optional<Json> open(...)`).
    r"|(^|[;{(,=!&|?:]|\breturn)\s*(fopen|open|creat|unlink|mkdir|rmdir|fsync|opendir)\s*\("
    r"|(^|[^\w])::(open|unlink|mkdir|rmdir|fsync)\s*\("
    r"|\bCreateFile[AW]?\b|\bFindFirstFile|#\s*include\s*<fstream>")
FILE_IO_LIBS = {"mcp-cpp", "acp-cpp", "jsonrpc-cpp"}

# claybin 工作是: 做点什么? fork/exec 将一个环境交给客人:
# R5的流程规则(和传递环境到执行)是它的宗旨.
EXEMPT = {("claybin", "R5 process"), ("claybin", "R5 env")}

# A 可变 静态/ 命名空间 -scope 变量( 以 R3。从此开始,
# 每个语句都匹配 `static`; const/constexpr 和功能是好的。
STATIC_RE = re.compile(r"^\s*static\s+(?!(inline\s+)?(const|constexpr|consteval)\b|_assert\b)")


def strip(text):
    """将注释及字符串、字符字面量置空，保留行号。"""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("\n" * text.count("\n", i, j))
            i = j
        elif c == "R" and i + 1 < n and text[i + 1] == '"' and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == "_")):
            k = text.find("(", i + 2)
            delim = text[i + 2:k]
            end = text.find(")" + delim + '"', k)
            end = n if end < 0 else end + len(delim) + 2
            out.append('""' + "\n" * text.count("\n", i, end))
            i = end
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            out.append(c + c)
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def lib_files(root, lib, subs=("src", "include")):
    for sub in subs:
        base = os.path.join(root, "third_party", lib, sub)
        for dp, _, fs in os.walk(base):
            for f in sorted(fs):
                if f.endswith(EXTS):
                    yield os.path.join(dp, f)


def purity(root):
    hits, nfiles = [], 0
    rules = {k: re.compile(v) for k, v in RULES.items()}
    for lib in LIBS:
        if not os.path.isdir(os.path.join(root, "third_party", lib)):
            hits.append(f"{lib}: not checked out")
            continue
        for path in lib_files(root, lib):
            nfiles += 1
            rel = relpath(path, os.path.join(root, "third_party"))
            code = strip(open(path, encoding="utf-8", errors="replace").read())
            for no, line in enumerate(code.split("\n"), 1):
                if not line.strip():
                    continue
                padded = " " + line
                for name, rx in rules.items():
                    if (lib, name) not in EXEMPT and rx.search(padded):
                        hits.append(f"{rel}:{no}: {name}: {line.strip()}")
                if (lib in FILE_IO_LIBS
                        and FILE_IO.search(padded.replace(" fs::", " std::filesystem::"))):
                    hits.append(f"{rel}:{no}: R6 file io: {line.strip()}")
                # R3: a 可变 在线上的任何静态,逐个声明(第1段)
                # `{ static int n = 0;` )),曾为滑行.
                for stmt in re.split(r"[;{}]", line):
                    if STATIC_RE.match(stmt):
                        head = re.split(r"[=({]", stmt, maxsplit=1)[0]
                        is_fn = re.search(r"\w\s*\(", stmt) and "=" not in stmt.split("(")[0]
                        if not is_fn and re.search(r"\w", head.replace("static", "", 1)):
                            hits.append(f"{rel}:{no}: R3 mutable static: {line.strip()}")
                            break
    if nfiles == 0:
        return ["scanned 0 files"]
    print(f"submodule_purity: {nfiles} files in {', '.join(LIBS)}")
    return hits


# 名称 jsonrpc-cpp 拥有; mcp-cpp 财务报告和已审计财务报表 acp-cpp 一定要用它们,而不是重新定义它们。
CORE_NAMES = ["Engine", "RpcEngine", "Codec", "RpcError", "Newtype", "RequestId",
              "Router", "LineSplitter", "Message", "Phantom", "StaticString"]
DUP_WINDOW = 12   # 数为副本的行中相同的正则行


def normalise(code):
    """去掉命名空间和空白行，并折叠空格后的代码行。"""
    out = []
    for line in code.split("\n"):
        s = re.sub(r"\s+", " ", line).strip()
        s = re.sub(r"\b(jsonrpc|mcp|acp)::", "", s)
        if not s or s in ("{", "}", "};", "public:", "private:", "protected:") \
                or s.startswith(("#include", "#pragma", "namespace ", "} // namespace", "using ")):
            continue
        out.append(s)
    return out


def duplication(root):
    hits = []
    redefine = re.compile(r"\b(struct|class|using|enum\s+class|concept)\s+(" + "|".join(CORE_NAMES) + r")\b")
    windows = {}   # 散列时速 DUP_WINDOW 线条 - > ( 库 ,rel,线).
    for lib in ["jsonrpc-cpp", "mcp-cpp", "acp-cpp"]:
        for path in lib_files(root, lib, ("include",) if lib == "jsonrpc-cpp" else ("src", "include")):
            rel = relpath(path, os.path.join(root, "third_party"))
            code = strip(open(path, encoding="utf-8", errors="replace").read())
            if lib != "jsonrpc-cpp":
                for no, line in enumerate(code.split("\n"), 1):
                    m = redefine.search(line)
                    if m and not re.search(r"=\s*(::)?jsonrpc::", line):
                        hits.append(f"{rel}:{no}: redefines jsonrpc-cpp's {m.group(2)}")
            lines = normalise(code)
            for i in range(len(lines) - DUP_WINDOW + 1):
                chunk = lines[i:i + DUP_WINDOW]
                # 跳过大多是圆点或1-word线的块。
                if sum(len(x) for x in chunk) < 40 * DUP_WINDOW // 2:
                    continue
                key = "\n".join(chunk)
                prev = windows.get(key)
                if prev and prev[0] != lib:
                    hits.append(f"{rel}: {DUP_WINDOW} lines match {prev[1]}: {chunk[0][:60]}")
                    break   # 每个文件一个报告就够了
                windows.setdefault(key, (lib, rel))
    print("no_duplication: jsonrpc-cpp, mcp-cpp, acp-cpp")
    return hits


# agentty 模块集成a JSON-RPC 库 (docs/PROTOCOL_LIBRARIES.md §6).
JSONRPC_USERS = ("src/rpc/", "include/agentty/rpc/", "src/mcp/", "include/agentty/mcp/",
                 "src/acp/", "include/agentty/acp/", "src/provider/external_acp",
                 "include/agentty/provider/external_acp", "src/tool/mcp_")


# 标准头文件XXjsonrpc-cpp可使用: no 线程,期货或I/O.
# <chrono>在此为交接的时长和时点;读钟为R4.
STD_OK = {
    "algorithm", "array", "cassert", "cctype", "charconv", "chrono", "compare", "concepts",
    "cstddef", "cstdint", "cstring", "deque", "expected", "functional",
    "initializer_list", "iterator", "limits", "map", "memory", "optional",
    "set", "span", "stdexcept", "string", "string_view", "tuple", "type_traits",
    "unordered_map", "unordered_set", "utility", "variant", "vector",
}


def jsonrpc_leaf(root):
    hits = []
    inc = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')
    for path in lib_files(root, "jsonrpc-cpp", ("include", "src")):
        rel = relpath(path, os.path.join(root, "third_party"))
        for no, line in enumerate(open(path, encoding="utf-8").read().split("\n"), 1):
            m = inc.match(line)
            if not m:
                continue
            h = m.group(1)
            local = '"' in line and os.path.exists(os.path.join(os.path.dirname(path), h))
            if local or h.startswith(("jsonrpc/", "nlohmann/")) or h in STD_OK:
                continue
            hits.append(f"{rel}:{no}: includes {h}")
    for sub in ("src", "include"):
        for dp, _, fs in os.walk(os.path.join(root, sub)):
            for f in fs:
                if not f.endswith(EXTS):
                    continue
                path = os.path.join(dp, f)
                rel = relpath(path, root)
                if rel.startswith(JSONRPC_USERS):
                    continue
                for no, line in enumerate(open(path, encoding="utf-8", errors="replace").read().split("\n"), 1):
                    m = inc.match(line)
                    if m and m.group(1).startswith("jsonrpc/"):
                        hits.append(f"{rel}:{no}: includes {m.group(1)} outside the rpc/mcp/acp modules")
    print("jsonrpc_leaf: jsonrpc-cpp is a leaf; agentty uses it only in rpc/mcp/acp")
    return hits


def drop_win32(code):
    """将 #if/#ifdef _WIN32 分支，以及 #if !_WIN32 的 #else 分支内部行置空。"""
    out, stack = [], []   # 堆栈( is_win_branch_now, 类型)
    for line in code.split("\n"):
        s = line.strip()
        m = re.match(r"#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)", s)
        if m:
            d, rest = m.group(1), m.group(2)
            if d in ("if", "ifdef", "ifndef"):
                win = "_WIN32" in rest
                neg = d == "ifndef" or bool(re.search(r"!\s*defined\s*\(?\s*_WIN32", rest))
                stack.append([win and not neg, win])
            elif d in ("elif", "else") and stack:
                top = stack[-1]
                top[0] = top[1] and not top[0] if d == "else" else ("_WIN32" in rest)
            elif d == "endif" and stack:
                stack.pop()
            out.append("")
            continue
        out.append("" if any(t[0] for t in stack) else line)
    return "\n".join(out)


def spawn_via_jaal(root):
    # Windows 树枝也被扫描了: 在那里,一个赤裸的 CreateProcess 藏起来
    # ShellExecute 故意忽略: 它手握一个 URL
    # 给用户浏览器并返回, 它不给我们一个孩子拥有。
    return scan_agentty(root, re.compile(
        r"[^\w.>:]v?fork\s*\(|::v?fork\s*\(|\bexec[lv]p?e?\s*\(|\bposix_spawnp?\b"
        r"|[^\w.>]_?popen\s*\(|\bpclose\s*\(|\bwaitpid\s*\(|[^\w.>]system\s*\("
        r"|\bCreateProcess(A|W|AsUser[AW]?|WithLogonW|WithTokenW)?\s*\("
        r"|\b_w?spawn[lv]p?e?\s*\(|\bWinExec\s*\("),
        "spawn_via_jaal: every agentty child starts through jaal's native_process",
        windows=True)


def no_sleep_poll(root):
    # 等待在进行中 事件: maya::delay_for 时间: stop_token ,
    # 反应堆,或 guarded::wait_with (为_for). 循环中睡眠,
    # 重新检查 a 标志 民意调查,这是如何 取消 最终用一秒钟时间降落。
    return scan_agentty(root, re.compile(r"\bsleep_for\b|\bsleep_until\b|[^\w.>:]usleep\s*\("),
        "no_sleep_poll: agentty waits on events, not sleeps")


def setenv_in_main(root):
    # setenv 竞速在另一个游戏上获得一个 getenv 线程 未定义,
    # 所以进程环境只写在顶端 main() ,在任何时间之前 线程.
    hits = scan_agentty(root, re.compile(r"[^\w.>]_?(setenv|unsetenv|putenv|putenv_s)\s*\("),
        "setenv_in_main: only main() writes the environment, before threads start")
    return [h for h in hits if "runtime/main.cpp" not in h.split(":", 1)[0]]


def tool_boundary(root):
    # 每个工具 调用 穿过 tool::DynamicDispatch::execute_with ,
    # 以每张 -call 规则现场直播(能力、写道、例外限制、
    # 产出预算)。 tools::ToolBody 直接的 直接的 调
    # 用 a 编译 错误; 此抓取类型周围的路 : 刻录 a
    # DispatchKey 或一个保存这些东西的私人副本的模块 或一个
    # 小小小小的
    rules = (
        (r"(?<!class )\bDispatchKey\s*\{", ("include/agentty/tool/tool.hpp",)),
        (r"\bwrite_lanes::Hold\b", ("include/agentty/tool/tool.hpp",)),
        (r"\b(WriteLaneHold|write_lane_paths)\b", ()),
    )
    hits = []
    for pat, allowed in rules:
        for h in scan_agentty(root, re.compile(pat), ""):
            if h.split(":", 1)[0] not in allowed:
                hits.append(h)
    print("tool_boundary: tool bodies run only through tool::DynamicDispatch")
    return hits


def scan_agentty(root, rx, banner, windows=False):
    hits = []
    for sub in ("src", "include"):
        for dp, _, fs in os.walk(os.path.join(root, sub)):
            for f in sorted(fs):
                if not f.endswith(EXTS):
                    continue
                path = os.path.join(dp, f)
                code = strip(open(path, encoding="utf-8", errors="replace").read())
                if not windows:
                    code = drop_win32(code)
                for no, line in enumerate(code.split("\n"), 1):
                    if rx.search(" " + line):
                        hits.append(f"{relpath(path, root)}:{no}: {line.strip()}")
    if banner:
        print(banner)
    return hits


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ("purity", "duplication", "jsonrpc_leaf",
                                                 "spawn_via_jaal", "no_sleep_poll",
                                                 "setenv_in_main", "tool_boundary"):
        print(__doc__)
        return 2
    root, what = sys.argv[1], sys.argv[2]
    hits = {"purity": purity, "duplication": duplication, "jsonrpc_leaf": jsonrpc_leaf,
            "spawn_via_jaal": spawn_via_jaal, "no_sleep_poll": no_sleep_poll,
            "setenv_in_main": setenv_in_main, "tool_boundary": tool_boundary}[what](root)
    if hits:
        print(f"\n{what}: {len(hits)} violation(s) (docs/PROTOCOL_LIBRARIES.md):")
        for h in hits:
            print("  " + h)
        return 1
    print("ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
