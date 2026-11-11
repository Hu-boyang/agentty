// 共享纯辅助读取工具参数和工具输出的纯辅助工具。参见 tool_args.hpp
// 无意的。 maya / 部件依赖性——纯数据塑造—— 所以两者都一样
// thread.cpp (时间线紧凑) -body 和。 和 简 况
// tool_card.cpp (小块) 渲染 ) 能够 链接 反之而不拖动部件 头文件.

#include "agentty/runtime/view/thread/turn/agent_timeline/tool_args.hpp"
#include <maya/core/anim_clock.hpp>   // anim_now : 数据 帧 时钟

#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace agentty::ui {

std::string safe_arg(const nlohmann::json& args, const char* key) {
    if (!args.is_object()) return {};
    auto it = args.find(key);
    return it != args.end() && it->is_string()
        ? it->get<std::string>() : std::string{};
}

std::string pick_arg(const nlohmann::json& args,
                     std::initializer_list<const char*> keys) {
    if (!args.is_object()) return {};
    for (const char* k : keys) {
        if (auto it = args.find(k); it != args.end() && it->is_string()) {
            const auto& s = it->get_ref<const std::string&>();
            if (!s.empty()) return s;
        }
    }
    return {};
}

std::string pick_streaming_string(std::string_view raw, std::string_view key) {
    if (raw.empty() || key.empty()) return {};
    // 地点 `"key"` 作为 KEY 符号: 引号 -wrapped
    // 键后(在可选的空白之后), a ':'. 重试 错误点击(与第
    // 1段相同) 值 ) ,"别处".
    std::string needle;
    needle.reserve(key.size() + 2);
    needle.push_back('"');
    needle.append(key);
    needle.push_back('"');
    std::size_t search = 0;
    while (true) {
        std::size_t k = raw.find(needle, search);
        if (k == std::string_view::npos) return {};
        std::size_t i = k + needle.size();
        while (i < raw.size() && (raw[i] == ' ' || raw[i] == '\t'
                                  || raw[i] == '\n' || raw[i] == '\r')) ++i;
        if (i >= raw.size() || raw[i] != ':') { search = k + 1; continue; }
        ++i;  // 过去 ':'
        while (i < raw.size() && (raw[i] == ' ' || raw[i] == '\t'
                                  || raw[i] == '\n' || raw[i] == '\r')) ++i;
        if (i >= raw.size() || raw[i] != '"') return {};  // 值( yet) 不是字符串
        ++i;  // 过去的开口引用
        // 收集到未脱逃的关闭引号(或) 缓冲区 切换的结束
        // 数据流 解码我们可以看到的中途 -stream.
        std::string out;
        while (i < raw.size()) {
            char c = raw[i];
            if (c == '\\') {
                if (i + 1 >= raw.size()) break;   // 正在逃跑中 正在逃逸中 正在逃跑 EOF
                char e = raw[i + 1];
                switch (e) {
                    case 'n': out.push_back('\n'); break;
                    case 't': out.push_back('\t'); break;
                    case 'r': break;              // 投放CR 数据
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    default:  out.push_back(e); break;  // \u ⁇  等: 保持原始
                }
                i += 2;
                continue;
            }
            if (c == '"') return out;             // 结束引用 + 已完成
            out.push_back(c);
            ++i;
        }
        return out;  // 中调 -value: 返回流到现在的
    }
}

std::string tool_path_arg(const ToolUse& tc) {
    // 解析的参数首先——一旦节奏的复刻得到追赶,就具有权威性.
    if (auto p = pick_arg(tc.args,
            {"path", "file_path", "filepath", "filename"}); !p.empty())
        return p;
    // 流式传输 回退:刮掉原版的JSON,所以文件名显示
    // 它的字节到达的瞬间,而不是后来的一节油门。
    const std::string_view raw{tc.args_streaming};
    for (std::string_view key : {"path", "file_path", "filepath", "filename"})
        if (auto p = pick_streaming_string(raw, key); !p.empty())
            return p;
    return {};
}

int safe_int_arg(const nlohmann::json& args, const char* key, int def) {
    if (!args.is_object()) return def;
    auto it = args.find(key);
    if (it == args.end()) return def;
    if (it->is_number_unsigned()) {
        const auto value = it->get<std::uint64_t>();
        return value <= static_cast<std::uint64_t>(std::numeric_limits<int>::max())
            ? static_cast<int>(value) : def;
    }
    if (!it->is_number_integer()) return def;
    const auto value = it->get<std::int64_t>();
    if (value < std::numeric_limits<int>::min()
        || value > std::numeric_limits<int>::max())
        return def;
    return static_cast<int>(value);
}

int count_lines(std::string_view s) {
    int n = 0;
    for (char c : s) if (c == '\n') n++;
    return n + (!s.empty() && s.back() != '\n' ? 1 : 0);
}

float tool_elapsed(const ToolUse& tc) {
    auto zero = std::chrono::steady_clock::time_point{};
    auto started = tc.started_at();
    if (started == zero) return 0.0f;
    auto finished = tc.finished_at();
    auto end = finished == zero ? maya::anim_now() : finished;
    auto dt = end - started;
    return std::chrono::duration<float>(dt).count();
}

std::string strip_bash_output_fence(std::string_view s) {
    std::string_view sv = s;
    auto drop_trailer = [&](std::string_view marker) {
        auto pos = sv.rfind(marker);
        if (pos != std::string_view::npos) sv = sv.substr(0, pos);
        while (!sv.empty() && (sv.back() == '\n' || sv.back() == '\r'
                               || sv.back() == ' '  || sv.back() == '\t'))
            sv.remove_suffix(1);
    };
    drop_trailer("\n\n[elapsed:");
    drop_trailer("\n\n[output truncated");

    auto fence = sv.find("```");
    if (fence == std::string_view::npos) return std::string{sv};
    // 允许领导 "Command …\n\n" 头文件 围栏之前
    // ——失败和 超时 树枝放在那里
    auto body_start = fence + 3;
    // 跳过语言标记(我们不放出一个,
    // 但原谅)和开栏后的新线.
    while (body_start < sv.size() && sv[body_start] != '\n') ++body_start;
    if (body_start < sv.size() && sv[body_start] == '\n') ++body_start;

    auto close = sv.rfind("```");
    if (close == std::string_view::npos || close <= body_start)
        return std::string{sv.substr(body_start)};

    auto body_end = close;
    while (body_end > body_start
           && (sv[body_end - 1] == '\n' || sv[body_end - 1] == '\r'))
        --body_end;

    std::string header{sv.substr(0, fence)};
    while (!header.empty() && (header.back() == '\n' || header.back() == '\r'
                               || header.back() == ' '))
        header.pop_back();
    std::string body{sv.substr(body_start, body_end - body_start)};
    if (header.empty()) return body;
    if (body.empty()) return header;
    return header + "\n\n" + body;
}

int parse_exit_code(std::string_view output) {
    struct Marker { const char* text; size_t skip; };
    static constexpr Marker markers[] = {
        {"failed with exit code ", 22},
        {"[exit code ",            11},
    };
    for (const auto& m : markers) {
        auto pos = output.rfind(m.text);
        if (pos == std::string_view::npos) continue;
        auto p = pos + m.skip;
        bool neg = p < output.size() && output[p] == '-';
        if (neg) ++p;
        int n = 0; bool got = false;
        while (p < output.size() && output[p] >= '0' && output[p] <= '9' && n < 100000000) {
            n = n * 10 + (output[p++] - '0'); got = true;
        }
        return got ? (neg ? -n : n) : 1;
    }
    if (output.find("timed out") != std::string_view::npos) return 124;
    return 0;
}

std::string_view card_output(const ToolUse& tc) {
    std::string_view out = tc.output();
    const auto desc = pick_arg(tc.args, {"display_description", "description"});
    if (!desc.empty() && out.starts_with(desc)
        && (out.size() == desc.size() || out[desc.size()] == '\n'))
        out.remove_prefix(std::min(out.size(), desc.size() + 1));
    // 跑者自己来的 `PASS exit=0` / `FAIL exit=1`
    // 状态回声:卡片状态图标已经说了这个.
    for (std::string_view tag : {"PASS exit=", "FAIL exit="})
        if (out.starts_with(tag))
            if (auto nl = out.find('\n'); nl != std::string_view::npos)
                out.remove_prefix(nl + 1);
    // 任何边线都别带任何东西
    while (!out.empty() && (out.front() == '\n' || out.front() == '\r')) out.remove_prefix(1);
    while (!out.empty() && (out.back() == '\n' || out.back() == ' ' || out.back() == '\r'))
        out.remove_suffix(1);
    return out;
}

std::string clean_output(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    std::size_t pos = 0;
    while (pos <= s.size()) {
        auto nl = s.find('\n', pos);
        if (nl == std::string_view::npos) nl = s.size();
        std::string_view ln = s.substr(pos, nl - pos);
        pos = nl + 1;
        // 注意工具为模型添加了它剪切的或如何翻页的功能,
        // 卡片显示的是自己的`⋯ N more`,所以它们在这里是噪声.
        auto t = ln;
        while (!t.empty() && t.front() == ' ') t.remove_prefix(1);
        const bool note = t.starts_with("[output capped") || t.starts_with("[showing lines")
            || t.starts_with("[extracted text from") || t.starts_with("[truncated")
            || (t.starts_with("[... ") && t.find(" elided") != std::string_view::npos)
            || t.starts_with("[cached —") || t.starts_with("[cached -");
        if (note) { if (nl == s.size()) break; continue; }
        // ANSI 越狱和押解 -return 重画进度栏。
        if (auto cr = ln.rfind('\r'); cr != std::string_view::npos && cr + 1 < ln.size())
            ln = ln.substr(cr + 1);
        for (std::size_t i = 0; i < ln.size(); ++i) {
            if (ln[i] == '\x1b' && i + 1 < ln.size() && ln[i + 1] == '[') {
                std::size_t j = i + 2;
                while (j < ln.size() && !(ln[j] >= '@' && ln[j] <= '~')) ++j;
                i = j;
                continue;
            }
            if (ln[i] == '\r') continue;
            out += ln[i];
        }
        if (nl == s.size()) break;
        out += '\n';
    }
    // 将空白行折叠为一,并空出边缘.
    std::string tidy;
    tidy.reserve(out.size());
    int blank = 0;
    std::size_t p = 0;
    while (p <= out.size()) {
        auto e = out.find('\n', p);
        if (e == std::string::npos) e = out.size();
        std::string_view ln{out.data() + p, e - p};
        const bool empty = ln.find_first_not_of(" \t") == std::string_view::npos;
        if (empty) ++blank; else blank = 0;
        if (!(empty && (blank > 1 || tidy.empty()))) {
            tidy.append(ln);
            tidy += '\n';
        }
        if (e == out.size()) break;
        p = e + 1;
    }
    while (!tidy.empty() && (tidy.back() == '\n' || tidy.back() == ' ')) tidy.pop_back();
    return tidy;
}

} // namespace agentty::ui
