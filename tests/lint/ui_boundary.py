#!/usr/bin/env python3
"""ui_boundary：agentty 描述界面，maya 负责绘制。

规则
====
agentty 视图层构建 maya::Element 值并交给 maya，
不直接接触单元格、画布或原始转义序列。布局、截断、颜色解析和光标等
所有像素层决策均归 maya，因此 maya 改变组件宽度协商方式时 agentty 仍能工作。
本检查把原先依靠习惯遵守的边界变成明确约束。

在 agentty 的 src/ 和 include/ 中禁止：

  * 直接写画布或单元格，例如 canvas.set/put、Canvas&、Cell{。
  * 自行定义组件，包括 paint/draw/render 方法或返回几何信息的 layout()。
  * 输出原始 ANSI 转义序列。

允许以下场景：

  * 测试需要将 Element 栅格化后断言，这是读取 maya 输出。
  * CLI 部分，如 agentty paste page、Ctrl+G 运行器、--version。
    此时 maya 未运行或已经交还终端。豁免逐个指定，不按整个目录放行。
  * 注释和字符串。turn.cpp 提到的转义序列用于说明 maya 的 HardReset 行为；
    扫描前会去掉注释和字面量。

用法：ui_boundary.py <agentty source root>
"""
import re
import sys
from pathlib import Path

# 合法释放出的文件,因为 maya 不在调解中
# 终端 每个人需要的是理由,而不是一条路
ALLOW = {
    "src/runtime/app/cmd_code_block_run.cpp":
        "runs a user command on the real tty after maya releases it; "
        "its progress line IS the terminal output",
    "src/tool/util/sandbox.cpp":
        "formats a sandbox denial for the plain-stderr path, before/outside "
        "the TUI",
}

# A 终端 存储单元 谓 `maya::Cell`。匹配一个赤裸裸的 `Cell{` 被抓到了
# 被抓到了吗 agentty 自己无关 存储单元 类型 -- -- a 缓存 插槽输入
# gen_cache.hpp, a 惰性-bytes 持有器 – 和屏幕无关。 认证它。
CANVAS = re.compile(r"\bcanvas\s*\.\s*(set|put|write)\b"
                    r"|\bmaya::Canvas\s*&"
                    r"|\bmaya::Cell\s*\{")
WIDGET = re.compile(r"\b(?:void|auto)\s+(?:paint|draw|render)\s*\(")
ANSI = re.compile(r"\\x1b\[|\\033\[|\\e\[")


def strip_code(text: str) -> str:
    """将注释及字符串、字符字面量置空，保留行号。"""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c == '"' or c == "'":
            q, j = c, i + 1
            while j < n and text[j] != q:
                j += 2 if text[j] == "\\" else 1
            out.append(" " * (min(j, n - 1) - i + 1))
            i = j + 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join(ch if ch == "\n" else " " for ch in text[i:j]))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def strip_comments(text: str) -> str:
    """只将注释置空，保留字符串字面量和行号。

    转义序列只会出现在字面量中，因此 ANSI 规则必须读取字面量。
    若像结构规则一样先移除它们，检查将永远不会触发。
    注释仍需移除，因为 turn.cpp 会合法地用文字说明 \\x1b[2J。
    """
    out, i, n = [], 0, len(text)
    while i < n:
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join(ch if ch == "\n" else " " for ch in text[i:j]))
            i = j
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def main() -> None:
    if len(sys.argv) != 2:
        print("usage: ui_boundary.py <agentty source root>", file=sys.stderr)
        sys.exit(2)
    root = Path(sys.argv[1])
    bad: list[str] = []
    scanned = 0

    for sub in ("src", "include"):
        for path in sorted((root / sub).rglob("*")):
            if path.suffix not in (".cpp", ".hpp") or not path.is_file():
                continue
            rel = path.relative_to(root).as_posix()
            scanned += 1
            code = strip_code(path.read_text(encoding="utf-8", errors="replace"))
            raw = path.read_text(encoding="utf-8", errors="replace")
            prose_free = strip_comments(raw)
            for lineno, line in enumerate(code.splitlines(), 1):
                if CANVAS.search(line):
                    bad.append(f"{rel}:{lineno}: writes a canvas/cell directly "
                               f"-- build a maya::Element instead")
                if WIDGET.search(line):
                    bad.append(f"{rel}:{lineno}: defines a paint/draw/render "
                               f"method -- that is a maya widget's job")
            if rel not in ALLOW:
                for lineno, line in enumerate(prose_free.splitlines(), 1):
                    if ANSI.search(line):
                        bad.append(f"{rel}:{lineno}: emits a raw ANSI escape "
                                   f"-- go through maya, or add a reason to "
                                   f"ui_boundary.py's ALLOW")

    if bad:
        print("ui_boundary: agentty must describe UI, not draw it\n",
              file=sys.stderr)
        for b in bad:
            print(f"  {b}", file=sys.stderr)
        print("\nmaya owns every pixel decision. agentty returns Elements.",
              file=sys.stderr)
        sys.exit(1)

    print(f"ui_boundary: {scanned} files, no UI drawing in agentty "
          f"({len(ALLOW)} allowed non-TUI emitters)")


if __name__ == "__main__":
    main()
