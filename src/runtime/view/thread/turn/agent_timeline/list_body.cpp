// tool_body_preview ——通用线 -oriented 工具( ) :
// 腺肿/ 肿块/ 缩写 list_dir / web_search / git_status / git_log / git_commit.
//
// 格瑞普会得到一个 STRUCTURED 正文: agentty 滴出标记(
// 滴出) `## Matches in <path>` 头文件,
// `### [sym ›] L<s>-<e>` 块标记,栅栏的代码行)。 grep_body (
// a) 调用 -derives 百分比 -row 块标记的行号(块内的行数是毗连的,所以起始+索引
// 是准确的),并合成条形体 `path:line:text` 形状
// maya::Kind::GrepMatches 渲染 以组路径 ~ 右 -aligned
// 线条 -number 表格。 十字架( 横跨) -tool grep_hits 指数(
// tool_body_common) 解析 相同的减记 FileRead 要点。
//
// 其余工具 渲染 头+ 末尾 CodeBlock 预览为 tail_only
// OFF : 其输出为 HEAD-heavy (顶级) -ranked 文件,
// 首个条目, 最新的承诺) , 结尾为 pagination/truncation 聊天,
// 所以一个 末尾-anchored 窗口显示的准确的半个字错误。
//
// 正文 渲染 输入 text_tertiary (dim)
// ——类别颜色存在于 头文件 NAME 其带有视觉特征的地方;
// 正文 色彩也制造出噪音,与传言者竞争 -style
// 工具组上方的标记。 DONE 工具生成 正文 ;
// 否则返回错误, 以陷入共同的失败 回退.

#include "tool_body_common.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <regex>
#include <string>
#include <string_view>

#include "agentty/runtime/view/palette.hpp"
#include "agentty/runtime/view/thread/turn/agent_timeline/tool_args.hpp"

namespace agentty::ui::detail {

namespace {

// 解析 线索 `L<start>` 出处 `### [breadcrumb ›] L<s>-<e>`
// 块标记。当标记不携带一个时返回 0。
int block_start_line(std::string_view tag) {
    // 线条 -range 标记是 最后 `L<digits>`
    // 运行在线上(面包屑本身可能包含 "L" ; 范围总是 终端).
    for (std::size_t i = tag.size(); i-- > 0;) {
        if (tag[i] != 'L') continue;
        std::size_t j = i + 1;
        int v = 0;
        bool got = false;
        while (j < tag.size() && tag[j] >= '0' && tag[j] <= '9') {
            v = v * 10 + (tag[j] - '0');
            ++j;
            got = true;
        }
        if (got && (j >= tag.size() || tag[j] == '-')) return v;
    }
    return 0;
}

// 说明grep块的哪一排是命中. grep的标记降不标记它们,
// 因此每排有调用自己的图案的re-test,
// 使用相同的case/word规则和相同的字面的回退来表示一个坏的regex.
class HitTest {
public:
    explicit HitTest(const nlohmann::json& args) {
        pat_ = safe_arg(args, "pattern");
        // find_references 搜索 `symbol` 整个词
        const bool symbol = pat_.empty() && !safe_arg(args, "symbol").empty();
        if (symbol) pat_ = safe_arg(args, "symbol");
        icase_ = !symbol && !(args.is_object() && args.contains("case_sensitive")
                   && args["case_sensitive"].is_boolean()
                   && args["case_sensitive"].get<bool>());
        const bool word = symbol || (args.is_object() && args.contains("word")
                       && args["word"].is_boolean() && args["word"].get<bool>());
        if (pat_.empty()) return;
        auto flags = std::regex::ECMAScript | std::regex::optimize;
        if (icase_) flags |= std::regex::icase;
        if (auto ex = safe_arg(args, "exclude"); !ex.empty()) {
            try { ex_.emplace(ex, flags); } catch (const std::regex_error&) {}
        }
        try {
            re_.emplace(word ? "\\b(?:" + pat_ + ")\\b" : pat_, flags);
        } catch (const std::regex_error&) {
            re_.reset();   // 搜索为文本, 如工具所做的
        }
    }
    [[nodiscard]] bool usable() const { return !pat_.empty(); }

    // 匹配的图案的准确文本 `rows`, '\n'-separated ,
    // 用于 -place 显示亮度。运行真正的正则ex 意味
    // 着任何图案都亮起... `resolve\(`,
    // `foo.*bar`, `\bid\b` ——不仅仅是普通字。
    // 当它不是有效的正则x时,它会回到图案本身(工具作为文本搜索)。
    // `.` 无法交出 渲染器 针千针.
    [[nodiscard]] std::string matched_needles(std::string_view rows) const {
        if (pat_.empty()) return {};
        if (!re_) return pat_;
        std::vector<std::string> seen;
        std::size_t pos = 0;
        while (pos < rows.size() && seen.size() < 32) {
            auto nl = rows.find('\n', pos);
            if (nl == std::string_view::npos) nl = rows.size();
            std::string_view row = rows.substr(pos, std::min<std::size_t>(nl - pos, 4096));
            pos = nl + 1;
            // 行为`path:line:text`;只有文本被突出显示.
            if (auto c1 = row.find(':'); c1 != std::string_view::npos)
                if (auto c2 = row.find(':', c1 + 1); c2 != std::string_view::npos)
                    row.remove_prefix(c2 + 1);
            try {
                for (std::cregex_iterator it(row.data(), row.data() + row.size(), *re_), end;
                     it != end && seen.size() < 32; ++it) {
                    std::string m = it->str();
                    if (m.size() < 2 || m.size() > 120 || m.find('\n') != std::string::npos)
                        continue;
                    if (std::find(seen.begin(), seen.end(), m) == seen.end())
                        seen.push_back(std::move(m));
                }
            } catch (const std::regex_error&) { break; }
        }
        std::string out;
        for (const auto& s : seen) {
            if (!out.empty()) out += '\n';
            out += s;
        }
        return out;
    }

    [[nodiscard]] bool hit(std::string_view row) const {
        if (row.size() > 4096) row = row.substr(0, 4096);   // 约束正则费用
        if (ex_) {
            try { if (std::regex_search(row.begin(), row.end(), *ex_)) return false; }
            catch (const std::regex_error&) {}
        }
        if (re_) {
            try { return std::regex_search(row.begin(), row.end(), *re_); }
            catch (const std::regex_error&) { return false; }
        }
        if (!icase_) return row.find(pat_) != std::string_view::npos;
        auto lo = [](unsigned char c) { return std::tolower(c); };
        return std::search(row.begin(), row.end(), pat_.begin(), pat_.end(),
                           [&](char a, char b) { return lo(a) == lo(b); })
               != row.end();
    }
private:
    std::string pat_;
    bool icase_ = true;
    std::optional<std::regex> re_;
    std::optional<std::regex> ex_;
};

} // 命名空间结束。

bool grep_body(const ToolUse& tc, maya::ToolBodyPreview::Config& out) {
    using Kind = maya::ToolBodyPreview::Kind;
    if (!tc.is_done()) return false;
    const std::string_view raw = card_output(tc);
    if (raw.empty()) return true;

    // 步行标记下形状并合成`path:line:text`行.
    // `rows`保留每行; `hits`只保留图案匹配的图案,
    // 这就是卡片显示的(一个1-match块,有20行上下文,
    // 用于预览其前8条上下文行,而从未匹配).
    const HitTest test{tc.args};
    std::string rows, hits;
    rows.reserve(raw.size());
    std::string_view current_path;
    int  next_line = 0;
    bool in_fence  = false;

    constexpr std::string_view kPathTag  = "## Matches in ";
    constexpr std::string_view kBlockTag = "### ";

    std::size_t pos = 0;
    while (pos < raw.size()) {
        const auto nl  = raw.find('\n', pos);
        const auto end = (nl == std::string::npos) ? raw.size() : nl;
        const std::string_view line(raw.data() + pos, end - pos);
        pos = (nl == std::string::npos) ? raw.size() : nl + 1;

        if (in_fence) {
            if (line.starts_with("```")) { in_fence = false; continue; }
            if (!current_path.empty() && next_line > 0) {
                std::string row{current_path};
                row += ':';
                row += std::to_string(next_line++);
                row += ':';
                row += line;
                row += '\n';
                if (test.usable() && test.hit(line)) hits += row;
                rows += row;
            }
            continue;
        }
        if (line.starts_with(kPathTag)) {
            current_path = line.substr(kPathTag.size());
        } else if (line.starts_with(kBlockTag)) {
            next_line = block_start_line(line);
        } else if (line.starts_with("```")) {
            in_fence = true;
        }
        // 标题( 4) "Found N matches…" ),
        // 页标拖车和空白分隔器被丢弃:匹配 COUNT 已经住在 事件 头文件 线条,
        // 以及 GrepMatches ' 属于自己 "⋯ N more"
        // 标记覆盖截断。
    }

    if (rows.empty()) {
        // 无可解析的栅栏( 无) -matches 消息,大小( Y) -cap 通知,
        // 一个 MCP grep 具有不同的形状)——保持原始文本的可读性.
        out.kind = Kind::CodeBlock;
        out.text = std::string{raw};
        out.text_color = text_primary;   // 纯输出读作普通文本
        out.tail_only = false;
        return true;
    }
    out.kind = Kind::GrepMatches;
    // 没有匹配的牌行 -side (正则特性) std::regex 缺少,
    // 多行图案 : 显示每行而不是一无所有。
    out.text = hits.empty() ? std::move(rows) : std::move(hits);
    out.text_color = text_primary;   // 纯输出读作普通文本
    // 点亮每击中搜索的-for文本: 无论显示的行中实际匹配
    // 的正则正则正正则正文。 在一个事物上, 格雷普点;
    // 列也应该点。
    out.match_needle = test.matched_needles(out.text);
    return true;
}

bool generic_list_body(const ToolUse& tc, maya::ToolBodyPreview::Config& out) {
    using Kind = maya::ToolBodyPreview::Kind;
    if (!tc.is_done()) return false;
    if (tc.name.value == "git_status") {
        // 电话 头文件 已经表示分支和计数。只保留瓷器文件行,
        // 并保留两个 -letter 代码拼写为一个单词。
        std::string rows;
        const std::string_view s = card_output(tc);
        for (std::size_t p = 0; p < s.size();) {
            auto e = s.find('\n', p);
            if (e == std::string_view::npos) e = s.size();
            const auto ln = s.substr(p, e - p);
            p = e + 1;
            if (ln.size() < 4 || ln[2] != ' ') continue;
            const auto code = ln.substr(0, 2);
            const char* word =
                code == "??" ? "new      " :
                code.find('D') != std::string_view::npos ? "deleted  " :
                code.find('A') != std::string_view::npos ? "added    " :
                code.find('R') != std::string_view::npos ? "renamed  " :
                code.find('U') != std::string_view::npos ? "conflict " :
                code.find('M') != std::string_view::npos ? "modified " : nullptr;
            if (!word) continue;
            rows += word;
            rows += ln.substr(3);
            rows += '\n';
        }
        if (!rows.empty()) {
            rows.pop_back();
            out.kind = Kind::CodeBlock;
            out.text = std::move(rows);
            out.text_color = text_primary;
            out.tail_only = false;
            return true;
        }
    }
    if (!card_output(tc).empty()) {
        out.kind = Kind::CodeBlock;
        out.text = clean_output(card_output(tc));
        out.text_color = text_primary;   // 纯输出读作普通文本
        // 头 -heavy 输出: 显示 首次 线条(加上小
        // 末尾 经由 head_tail 配置),而不是 末尾
        // -anchored 窗口,只显示页脚。
        out.tail_only = false;
    }
    return true;
}

} // namespace agentty::ui::detail
