// 共享的帮助者( 百分比) -tool 正文-preview 制造器,
// 看 tool_body_common.hpp 合同的。

#include "tool_body_common.hpp"

#include <algorithm>
#include <cctype>

#include <maya/platform/io.hpp>

#include "agentty/runtime/view/thread/turn/agent_timeline/tool_args.hpp"

namespace agentty::ui::detail {

// ── 流式传输-card 正文 预算 ────────────────────────────────────────
//
// 该 INVARIANT (回滚缓冲区 先知, 说: write/edit
// 转弯: 转弯时 write/edit 纸牌 数据流 ,其编号为 事件
// HEADER 排队必须留在里面 可视区域 如果 正文 长得足够高,
// 可以推 头文件 变成本地 回滚缓冲区 , 解决的运行 – 唐式重排该行 (
// 亮 + 淡 ) tree/name 存储单元 — a STYLE-ONLY 重写,
// 隐身在Char垃圾堆)是执着的 -row 重写; maya 门只能用
// 破坏器恢复 HardReset 成长中 帧.
//
// 因此,预算按 真实 终端 高度而不是被钉在最
// 坏的病例上。 头文件 ,从神谕的60x18中
// 计算 可视区域 倾销 :
//   头文件(1)+空白(1)+页脚(1)+卡底边框(1)+缺口(1)
//   +作曲(6)+状态栏(3)=14行,+1懒 ⁇ =15.
// body_budget = term_rows - 15,3楼(证明)
// -safe 最少18岁 -row 终端 ——神谕的传承的正是这个配置.
//
// Tool 正文 建于( 建于) VIEW-BUILD 时间,在 RenderContext
// view() 从“安装”中安装 Model's 终端 大小,所以,( 缩写) available_height
// ()是那里的真高(24度在任何景点之外,旧的不是) -tty 回退).
int stream_body_budget() {
    const int rows = maya::available_height() > 0 ? maya::available_height() : 24;
    constexpr int kChromeBelowHeader = 15;
    return std::max(3, rows - kChromeBelowHeader);
}

int stream_card_rows_bound() {
    // 镜像 edit_body.cpp's per_side
    // 衍生 恰好 如果夹子改变, 就必须随它而改变,
    // 这就是为什么他们坐在一起 调用 而不是两个独立的常数
    const int per_side = std::clamp((stream_body_budget() - 1) / 2, 1, 6);
    const int body_rows = 1 + 2 * per_side;   // 统计 标签 + 末尾 行数
    // 人数 -card 铬: 事件 头文件, hunk/stat 线条,
    // 上+下边框,页脚状态,空白的沟槽排行,卡片坐落。
    constexpr int kCardChromeRows = 7;
    return body_rows + kCardChromeRows;
}

std::string tail_window(std::string_view s, std::size_t keep_lines) {
    if (s.empty()) return {};
    // 倒行数新行; 之后停止 keep_lines +1
    // 其中(+1锚定了第一个保留线的起始).
    std::size_t nl_seen = 0;
    std::size_t start = s.size();
    for (std::size_t i = s.size(); i-- > 0;) {
        if (s[i] == '\n') {
            if (++nl_seen > keep_lines) { start = i + 1; break; }
        }
        if (i == 0) start = 0;
    }
    return std::string{s.substr(start)};
}

std::string head_window(std::string_view s, std::size_t keep_lines) {
    if (s.empty()) return {};
    std::size_t nl_seen = 0;
    std::size_t end = s.size();
    std::size_t total_lines = 1;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\n') {
            ++total_lines;
            if (nl_seen + 1 == keep_lines && end == s.size()) end = i;
            ++nl_seen;
        }
    }
    if (total_lines <= keep_lines) return std::string{s};
    std::string out{s.substr(0, end)};
    std::size_t more = total_lines - keep_lines;
    out += "\n\xe2\x8b\xaf " + std::to_string(more)
         + (more == 1 ? " more line" : " more lines");  // ⋯
    return out;
}

std::string read_path_arg(const nlohmann::json& args) {
    if (!args.is_object()) return {};
    for (auto k : {"path", "file_path", "filepath", "filename"}) {
        if (auto it = args.find(k); it != args.end() && it->is_string())
            return it->get<std::string>();
    }
    return {};
}

// 解析 `## Matches in <path>` 财务报告和已审计财务报表
// `### L<start>-<end>` 标记从 agentty 用于 grep
// 工具输出和累积( path ~ { start lines}) 到 `out`
// 我们使用 BLOCK START 以代表匹配主播身份进行 agentty
// 输出组与周围上下文匹配,所以单个匹配的偏移不能从提供中恢复 正文.
// 块 -start 足够了 highlight_lines 锚;
// 用户的眼球降落在文件的右侧区域.
void accumulate_grep_hits(std::string_view output, GrepHits& out) {
    constexpr std::string_view kPathTag  = "## Matches in ";
    constexpr std::string_view kBlockTag = "### L";

    std::string current_path;
    std::size_t pos = 0;
    while (pos < output.size()) {
        const auto nl = output.find('\n', pos);
        const auto end = (nl == std::string::npos) ? output.size() : nl;
        const std::string_view line(output.data() + pos, end - pos);

        if (line.starts_with(kPathTag)) {
            current_path = std::string{line.substr(kPathTag.size())};
        } else if (!current_path.empty() && line.starts_with(kBlockTag)) {
            // 解析 `### L<start>-<end>` — 读数最高为'-'或结束.
            std::size_t i = kBlockTag.size();
            int start = 0;
            bool got = false;
            while (i < line.size() && line[i] >= '0' && line[i] <= '9') {
                start = start * 10 + (line[i] - '0');
                ++i;
                got = true;
            }
            if (got && start > 0)
                out[current_path].insert(start);
        }

        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
}

std::string args_match_needle(const nlohmann::json& args) {
    if (!args.is_object()) return {};
    // 明确 regex: true 表示 ⁇ 是一个图案,而不是字面.
    const auto r = args.find("regex");
    const bool regex_mode =
        r != args.end() && r->is_boolean() && r->get<bool>();
    if (regex_mode) return {};

    // `find` 以合同形式写成 当regex脱落时,
    // 所以把它当作 -is 元特征筛选会把像 `foo(bar);`。
    // 模式 -style 参数没有这种合同,
    // 所以这些仍然在下面筛选.
    if (auto it = args.find("find"); it != args.end() && it->is_string()) {
        auto needle = it->get<std::string>();
        if (needle.size() < 2 || needle.size() > 200) return {};
        if (needle.find('\n') != std::string::npos) return {};
        return needle;
    }

    std::string needle;
    for (const char* key : {"pattern", "needle", "query"}) {
        auto it = args.find(key);
        if (it != args.end() && it->is_string()) {
            needle = it->get<std::string>();
            break;
        }
    }
    if (needle.size() < 2 || needle.size() > 200) return {};
    if (needle.find('\n') != std::string::npos) return {};

    // 纯粹是文字交替的图案( `foo|bar|128000` )
    // 仍然可突出显示: 拆分开 '|' 手和手 maya 每一个选择。
    // 任何其他regex元码意味着我们不能对-match进行子字符串.
    if (needle.find_first_of("\\^$.?*+()[]{}") != std::string::npos)
        return {};
    if (needle.find('|') == std::string::npos) return needle;

    std::string out;
    for (std::size_t s = 0; s <= needle.size();) {
        const auto e = needle.find('|', s);
        const auto alt = needle.substr(
            s, e == std::string::npos ? std::string::npos : e - s);
        if (alt.size() >= 2) {
            if (!out.empty()) out += '\n';
            out += alt;
        }
        if (e == std::string::npos) break;
        s = e + 1;
    }
    return out;
}

std::string_view strip_error_tags(std::string_view s) {
    // 丢弃所有线索 `[kind] ` 标记。退出拖车,
    // 如 `[exit code 1]` 永远不要领导文本,所以他们被放任。
    for (;;) {
        if (s.empty() || s.front() != '[') return s;
        const auto close = s.find(']');
        if (close == std::string_view::npos || close < 2 || close > 32)
            return s;
        const auto inner = s.substr(1, close - 1);
        if (inner.find_first_of("[\n") != std::string_view::npos) return s;
        if (inner.starts_with("exit code") || inner.starts_with("terminated"))
            return s;
        s.remove_prefix(close + 1);
        while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
    }
}

std::string failure_body_text(const ToolUse& tc) {
    auto s = strip_error_tags(card_output(tc));
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.remove_suffix(1);
    // 电话 头文件 已经说过了;a 正文 排队只会重复它。
    if (s == "cancelled" || s == "interrupted" || s == "aborted") return {};
    return std::string{s};
}

std::string lang_for_path(std::string_view path) {
    const auto slash = path.find_last_of("/\\");
    std::string base{slash == std::string_view::npos ? path
                                                     : path.substr(slash + 1)};
    for (auto& c : base)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (base == "cmakelists.txt") return "cmake";
    if (base == "makefile" || base == "gnumakefile") return "makefile";
    if (base == "dockerfile") return "dockerfile";
    if (base == "pkgbuild") return "sh";
    const auto dot = base.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= base.size()) return {};
    return base.substr(dot + 1);
}

std::string lang_for_tool(const ToolUse& tc) {
    for (const char* k : {"path", "file_path"}) {
        auto p = safe_arg(tc.args, k);
        if (auto l = lang_for_path(p); !l.empty()) return l;
    }
    // 替换/ rewrite_structural: `glob` 喜欢 "*.cpp" 或 或 时 间 "src/** /*.ts".
    if (auto g = safe_arg(tc.args, "glob"); !g.empty()) {
        if (auto l = lang_for_path(g); !l.empty() && l.find_first_of("{}*,") == std::string::npos)
            return l;
    }
    // 阴茎 正文 命名其文件 : `+++ b/path` 或 或 时 间 `▏ path`.
    const std::string_view out = card_output(tc);
    for (std::string_view tag : {"+++ b/", "+++ ", "\xe2\x96\x8f "}) {
        if (auto p = out.find(tag); p != std::string_view::npos) {
            auto e = out.find('\n', p);
            auto path = out.substr(p + tag.size(),
                                   e == std::string_view::npos ? std::string_view::npos
                                                               : e - p - tag.size());
            if (auto l = lang_for_path(path); !l.empty()) return l;
        }
    }
    return {};
}

// 无扩展文件的最后手段:shebang或明显的第一行.
static std::string lang_from_content(std::string_view s) {
    while (!s.empty() && (s.front() == '\n' || s.front() == ' ')) s.remove_prefix(1);
    const auto line = s.substr(0, s.find('\n'));
    if (line.starts_with("#!")) {
        if (line.find("python") != std::string_view::npos) return "py";
        if (line.find("node") != std::string_view::npos)   return "js";
        return "sh";
    }
    if (s.find("#include") != std::string_view::npos
     || s.find("#pragma once") != std::string_view::npos
     || s.find("namespace ") != std::string_view::npos)
        return "cpp";
    if (s.starts_with("use ") || s.find("\nfn ") != std::string_view::npos)
        return "rs";
    if (s.starts_with("package ")) return "go";
    if (s.starts_with("import ") || s.starts_with("def ")
     || s.find("\ndef ") != std::string_view::npos)
        return "py";
    // 以 `;`, `{` 或 或 时 间 `}` 改为C -family ;
    // C++ 指示器处理 C, Java, JS 和足够好的朋友 来给他们颜色。
    int c_like = 0;
    for (std::size_t p = 0; p < s.size() && c_like < 2;) {
        auto e = s.find('\n', p);
        if (e == std::string_view::npos) e = s.size();
        auto l = s.substr(p, e - p);
        while (!l.empty() && (l.back() == ' ' || l.back() == '\r')) l.remove_suffix(1);
        if (!l.empty() && (l.back() == ';' || l.back() == '{' || l.back() == '}'))
            ++c_like;
        p = e + 1;
    }
    if (c_like >= 2) return "cpp";
    return {};
}

std::string lang_for_tool_or_content(const ToolUse& tc) {
    if (auto l = lang_for_tool(tc); !l.empty()) return l;
    for (const char* k : {"content", "new_text", "old_text"}) {
        if (auto l = lang_from_content(safe_arg(tc.args, k)); !l.empty())
            return l;
    }
    if (tc.args.is_object()) {
        if (auto it = tc.args.find("edits"); it != tc.args.end() && it->is_array())
            for (const auto& e : *it)
                if (e.is_object())
                    for (const char* k : {"new_text", "old_text"})
                        if (auto f = e.find(k); f != e.end() && f->is_string())
                            if (auto l = lang_from_content(f->get<std::string>()); !l.empty())
                                return l;
    }
    return lang_from_content(card_output(tc));
}

} // namespace agentty::ui::detail
