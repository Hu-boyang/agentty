// agentty::ui::host ——合作 -host 融合逃脱。 头文件.
//
// 故意的无名氏 -FREE : 这是一小片叶子 TU 自动调试 状态更新函数 路径,
// 并拉 <nlohmann/json.hpp> 仅仅为了 序列化 a
// 3-field 对象会花费更多 编译 时间比它的价值。 载荷 很小,
// 完全在我们控制之下,所以我们释放 JSON 和最小的,
// 正确的字符串逃生器代替。
#include "agentty/runtime/view/host_escape.hpp"

#include <cstdio>
#include <cstdlib>

namespace agentty::ui::host {

namespace {

// AGENTTY_HOST 是第一个明确的 -class 信号( 启动编辑器) agentty 套装 AGENTTY_HOST
// =emacs) ; ==== == ; +=== (===; = = ;=== INSIDE_EMACS 是Emacs
// 自己的标志,只有它的标记 "vterm" 前端可以运行我们 OSC 钩子.
bool detect_integration_impl() {
    if (const char* h = std::getenv("AGENTTY_HOST"); h && *h) {   // launch-env: 仅由 read_launch_env 在启动时读取一次
        std::string_view v{h};
        if (v == "emacs" || v == "vterm") return true;
    }
    if (const char* ie = std::getenv("INSIDE_EMACS"); ie && *ie) {   // launch-env: 仅由 read_launch_env 在启动时读取一次
        if (std::string_view{ie}.find("vterm") != std::string_view::npos)
            return true;
    }
    return false;
}

// 附加 `s` 作为 JSON 字符串 BODY (没有周围引文)改为 `out` ,
// 摆脱七个字符 JSON 需要加所有 C0 控件为\u\\\\}够了
// for a filesystem path + a tool name; no nlohmann needed.
void append_json_escaped(std::string& out, std::string_view s) {
    static const char* kHex = "0123456789abcdef";
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out += kHex[(c >> 4) & 0xF];
                    out += kHex[c & 0xF];
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
}

} // 命名空间结束。

bool detect_integration() { return detect_integration_impl(); }

std::optional<std::string>
file_event_osc(std::string_view kind, std::string_view path,
               std::optional<int> line) {
    if (path.empty()) return std::nullopt;

    // OSC 5379 ; agentty ; {"event":"file","kind":..,
    // "path":..,"line" 我们只返回 OSC PAYLOAD (每件事之后)
    // "ESC ] <code> ;" );呼叫者手握5379+这个到 maya's cmd::emit_osc ,
    // 预留 "ESC ] 5379 ;" 附加该附件 ST 灭者.
    std::string out = "agentty;{\"event\":\"file\",\"kind\":\"";
    append_json_escaped(out, kind);
    out += "\",\"path\":\"";
    append_json_escaped(out, path);
    out += '"';
    if (line && *line > 0) {
        out += ",\"line\":";
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d", *line);
        out += buf;
    }
    out += '}';
    return out;
}

} // namespace agentty::ui::host
