// tool_body_preview — `read` / `find_definition` 工具 正文.
//
// FileRead。当同一路径上的前一个Grep产生命中点时,
// 继承为 highlight_lines 故读作 正文 将用户的
// 目光锁定在相关区域而不是强迫重置 -scan . 总结 头文件
// `▸ matches: N1, N2, …` 列出每次点击,
// 即使它们落在已实现的头预算之外—— 常见于长文件,
// 匹配点位于中点 -file 但读数 正文 显示顶部。
//
// A DONE 读出 FileRead 正文 (回归真实)。
// A FAILED 读取返回错误, 从而 分派器 共享失败 回退
// 渲染 错误文本为纯红色 - no FileRead 沟槽对错误消息
// 来说是有意义的。

#include "tool_body_common.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

#include "agentty/runtime/view/palette.hpp"
#include "agentty/runtime/view/thread/turn/agent_timeline/tool_args.hpp"

namespace agentty::ui::detail {

namespace {
// 返回{第一行编号, 文本无前缀}, 或{0,。
// "" 何时 正文 并非每行都编号(旧的笔录、大纲答复)。
std::pair<int, std::string> strip_line_numbers(std::string_view body) {
    std::string out;
    out.reserve(body.size());
    int first = 0;
    std::size_t pos = 0;
    while (pos < body.size()) {
        std::size_t eol = body.find('\n', pos);
        if (eol == std::string_view::npos) eol = body.size();
        std::size_t k = pos;
        while (k < eol && body[k] == ' ') ++k;
        const std::size_t d = k;
        int n = 0;
        while (k < eol && body[k] >= '0' && body[k] <= '9' && n < 100000000)
            n = n * 10 + (body[k++] - '0');
        if (k == d || k >= eol || body[k] != '\t') return {0, {}};
        if (first == 0) first = n;
        out.append(body.substr(k + 1, eol - k - 1));
        if (eol < body.size()) out.push_back('\n');
        pos = eol + 1;
    }
    return {first, std::move(out)};
}
} // 命名空间结束。

bool read_body(const ToolUse& tc,
               const GrepHits* grep_hits,
               maya::ToolBodyPreview::Config& out) {
    using Kind = maya::ToolBodyPreview::Kind;
    if (!tc.is_done()) return false;   // 失败 – 共享失败 回退

    const std::string_view raw = card_output(tc);
    if (!raw.empty()) {
        // A 缓存 击出前缀
        // "[cached — unchanged since your last read of this file]\n"。（缓存命中：文件自上次读取后没有变化。）
        // 这就是与模型交谈的工具,而不是文件文本;把它放在沟槽的1行中,
        // 并让每个真实的行号都换一个。
        std::string_view body_src{raw};
        if (body_src.starts_with("[cached"))
            if (const auto nl = body_src.find('\n'); nl != std::string_view::npos)
                body_src.remove_prefix(nl + 1);
        // 读取工具装饰其输出: 一条线索
        // "SUCCESS: `sym` defined at path:N (lines A–B).\n\n"（成功：符号 sym 定义于 path:N，范围为 A–B 行。）
        // 头文件 费用 符号 读和后继 "\n[showing lines A-B of N …]"
        // 双脚都一样 NUMBERED AS FILE CONTENT
        // 由 FileRead 沟渠, 挖下下面的每个线号,
        // 剥光这里, 矿井的 TRUE 第一行——对参数回声的权威(
        // 没有此回声) 符号 读取和负读 末尾 ,第3段)。
        std::string_view body{body_src};
        int true_start = 0;

        auto parse_int_at = [](std::string_view s, std::size_t i) -> int {
            int v = 0; bool got = false;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
                v = v * 10 + (s[i] - '0'); ++i; got = true;
            }
            return got ? v : 0;
        };

        // 页脚: 权威用于 offset/limit 财务报告和已审计财务报表 末尾 (-N )读.
        constexpr std::string_view kFooterTag = "\n[showing lines ";
        if (auto fp = body.rfind(kFooterTag); fp != std::string_view::npos) {
            true_start = parse_int_at(body, fp + kFooterTag.size());
            body = body.substr(0, fp);
        }
        // 符号标题原文："SUCCESS: `x` defined at f:N (lines A–B).\n\n"，表示符号定义位置与行范围。
        constexpr std::string_view kSymTag = "SUCCESS: `";
        if (body.starts_with(kSymTag)) {
            if (auto he = body.find("\n\n"); he != std::string_view::npos) {
                constexpr std::string_view kLines = "(lines ";
                if (auto lp = body.find(kLines); lp < he)
                    true_start = parse_int_at(body, lp + kLines.size());
                body = body.substr(he + 2);
            }
        }

        out.kind = Kind::FileRead;
        // 每一行都读作 "%6d\t" 前缀。
        // 卡片会画自己的沟槽,所以剥去它们;第一个是真正的起始线。
        // 任意头 -up 上方的编号块随它们一起丢弃。
        auto numbered = body;
        for (int guard = 0; guard < 8 && !numbered.empty(); ++guard) {
            std::size_t k = 0;
            while (k < numbered.size() && numbered[k] == ' ') ++k;
            const std::size_t d = k;
            while (k < numbered.size() && numbered[k] >= '0' && numbered[k] <= '9') ++k;
            if (k > d && k < numbered.size() && numbered[k] == '\t') break;
            const auto nl = numbered.find('\n');
            if (nl == std::string_view::npos) { numbered = {}; break; }
            numbered.remove_prefix(nl + 1);
        }
        if (auto stripped = strip_line_numbers(numbered); stripped.first > 0) {
            true_start = stripped.first;
            out.text = std::move(stripped.second);
        } else {
            out.text = std::string{body};
        }
        out.text_color = text_primary;   // 代码读作普通文本, 上面的符号
        // 锁定沟口到真实源行号。 优先级 :
        // 由工具本身的脚印 /符号 头文件(底真理——覆盖符号读
        // 和末尾读)所挖掘的号码,然后是`offset` /
        // `start_line` arg回声,然后是默认的 1。
        if (true_start >= 1) {
            out.start_line = true_start;
        } else if (tc.args.is_object()) {
            for (auto k : {"start_line", "offset"}) {
                const int v = safe_int_arg(tc.args, k, 0);
                if (v >= 1) { out.start_line = v; break; }
            }
        }
        if (grep_hits) {
            if (auto path = read_path_arg(tc.args); !path.empty()) {
                if (auto it = grep_hits->find(path); it != grep_hits->end())
                    out.highlight_lines = it->second;
            }
        }
        // 文件从顶部读取( 包括声明); 其最后一行
        // 通常关闭括号。 显示头部并显示后面的多少。
        // 使用 grep 点击, 从第一个开始。
        out.tail_only = false;
        if (!out.highlight_lines.empty()) {
            const int first_hit = *out.highlight_lines.begin();
            const int skip = std::max(0, first_hit - out.start_line - 1);
            std::size_t p = 0;
            for (int k = 0; k < skip && p != std::string::npos; ++k) {
                p = out.text.find('\n', p);
                if (p != std::string::npos) ++p;
            }
            if (p != std::string::npos && p > 0 && p < out.text.size()) {
                out.text.erase(0, p);
                out.start_line += skip;
            }
        }
    }
    return true;
}

} // namespace agentty::ui::detail
