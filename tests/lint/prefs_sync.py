#!/usr/bin/env python3
"""prefs_sync：每个 ui::Prefs 字段都必须出现在字段表中。

检查的原因
==========
settings.json 写入器、加载器和保存时的归并过去分别手写全部 Prefs 字段。
三份清单没有一致性约束，遗漏会无声地产生不同故障：写入器漏项导致无法保存，
加载器漏项导致重启后忽略设置，归并漏项则让第二个 agentty 会话在保存时覆盖配置。
最后一种就是用户看到的“设置没有持久化”。

现在三者统一遍历 include/agentty/domain/ui_prefs.hpp 中的
ui_prefs::for_each_field，不再各自枚举字段，因此不会分歧。
默认值也通过成员指针从 Prefs{} 获取，无需维护第二份默认值。

剩余唯一检查点是：结构体新增字段却没加入字段表。
这里没有 C++ 反射支持，编译器无法发现，字段就会始终无法保存。
因此比较两者，发现遗漏时点名字段并使构建失败。

用法：prefs_sync.py <ui_prefs.hpp>
"""
import re
import sys

# 出现在Prefs中的字段类型。列表而不是松散匹配,
# 所以必须在此添加一个新类型——一个无法识别
# 的类型会让字段隐蔽于此,这就是该字段存在的
# 防止的准确失败。
TYPES = (
    "ThemeName", "ColorTier", "Polarity", "Density", "Motion", "Scrollback",
    "ToolOutput", "Thinking", "Timestamps", "std::string", "int", "bool",
)


def die(msg: str) -> None:
    print(f"prefs_sync: {msg}", file=sys.stderr)
    sys.exit(1)


def struct_fields(hdr: str) -> list[str]:
    m = re.search(r"struct Prefs \{(.*?)\n\};", hdr, re.S)
    if not m:
        die("could not find `struct Prefs` in ui_prefs.hpp")
    pat = r"^\s+(?:" + "|".join(re.escape(t) for t in TYPES) + r")\s+(\w+)\s*(?:=|;)"
    out = [f for f in re.findall(pat, m.group(1), re.M) if f != "operator"]
    if not out:
        die("parsed zero fields out of struct Prefs -- the lint itself is broken")
    return out


def table_fields(hdr: str) -> list[str]:
    m = re.search(r"constexpr void for_each_field\(F&& f\) \{(.*?)\n\}", hdr, re.S)
    if not m:
        die("could not find for_each_field in ui_prefs.hpp")
    out = re.findall(r"&Prefs::(\w+)", m.group(1))
    if not out:
        die("parsed zero fields out of for_each_field -- the lint itself is broken")
    return out


def main() -> None:
    if len(sys.argv) != 2:
        die("usage: prefs_sync.py <ui_prefs.hpp>")
    hdr = open(sys.argv[1], encoding="utf-8").read()

    fields = struct_fields(hdr)
    table = table_fields(hdr)

    missing = [f for f in fields if f not in table]
    extra = [f for f in table if f not in fields]
    dupes = sorted({f for f in table if table.count(f) > 1})

    if missing or extra or dupes:
        if missing:
            print("prefs_sync: in struct Prefs but NOT in for_each_field: "
                  + ", ".join(missing), file=sys.stderr)
        if extra:
            print("prefs_sync: in for_each_field but not a Prefs field: "
                  + ", ".join(extra), file=sys.stderr)
        if dupes:
            print("prefs_sync: listed twice in for_each_field: "
                  + ", ".join(dupes), file=sys.stderr)
        print("\nThe settings writer, loader and save-time merge all walk\n"
              "for_each_field. A field missing from it is never written,\n"
              "never loaded, and clobbered when a second agentty session\n"
              "saves. Add one line to the table.", file=sys.stderr)
        sys.exit(1)

    print(f"prefs_sync: {len(fields)} ui::Prefs fields, all in the field table")


if __name__ == "__main__":
    main()
