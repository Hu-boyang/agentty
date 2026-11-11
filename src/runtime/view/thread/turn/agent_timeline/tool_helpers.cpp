#include "agentty/runtime/view/thread/turn/agent_timeline/tool_helpers.hpp"
#include "agentty/util/digits.hpp"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

#include "agentty/runtime/view/palette.hpp"
#include "agentty/runtime/view/thread/turn/agent_timeline/tool_args.hpp"
#include "agentty/tool/util/utf8.hpp"
#include "agentty/domain/ui_live.hpp"   // path_roots, 项目代理商

namespace agentty::ui {

// ── 显示 ─────────────────────────────────────────────────────────────

// 漂亮的标题 -case 用于显示为时间线的工具名称 事件 标签。
// 地图 agentty 将小写小写名称改为品牌 TitleCase 表格中。
std::string tool_display_name(const std::string& n) {
    if (n == "read")            return "Read";
    if (n == "write")           return "Write";
    if (n == "edit")            return "Edit";
    if (n == "move")            return "Move";
    if (n == "remove")          return "Remove";
    if (n == "shell")           return "Shell";
    if (n == "process_start")   return "Process Start";
    if (n == "process_poll")    return "Process Poll";
    if (n == "process_stop")    return "Process Stop";
    if (n == "grep")            return "Grep";
    if (n == "glob")            return "Glob";
    if (n == "list_dir")        return "List";
    if (n == "repo_map")        return "Repo Map";
    if (n == "todo")            return "Todo";
    if (n == "web_fetch")       return "Fetch";
    if (n == "web_search")      return "Search";
    if (n == "find_definition") return "Definition";
    if (n == "search_structural") return "Structural";
    if (n == "extract")         return "Extract";
    if (n == "aggregate")       return "Aggregate";
    if (n == "read_filter")     return "Read Filter";
    if (n == "json_query")      return "JSON Query";
    if (n == "outline")         return "Outline";
    if (n == "apply_patch")     return "Patch";
    if (n == "replace")         return "Replace";
    if (n == "rewrite_structural") return "Rewrite";
    if (n == "diagnostics")     return "Diagnostics";
    if (n == "test")            return "Test";
    if (n == "git_status")      return "Git Status";
    if (n == "git_diff")        return "Git Diff";
    if (n == "git_log")         return "Git Log";
    if (n == "git_show")        return "Git Show";
    if (n == "git_blame")       return "Git Blame";
    if (n == "git_commit")      return "Git Commit";
    if (n == "git_branch")      return "Git Branch";
    if (n == "git_stash")       return "Git Stash";
    if (n == "git_rebase")      return "Git Rebase";
    if (n == "git_cherry_pick") return "Git Cherry-Pick";
    if (n == "remember")        return "Remember";
    if (n == "forget")          return "Forget";
    if (n == "wipe_memory")     return "Wipe Memory";
    if (n == "task")            return "Agent";
    if (n == "skill")           return "Skill";
    if (n == "search_docs")     return "Docs Search";
    if (n == "search_code")     return "Code Search";
    if (n == "tool_search")         return "Tool Search";
    if (n == "mcp__resources__read") return "MCP Resource";
    if (n == "mcp__prompts__get")    return "MCP Prompt";
    if (n == "codex_script")         return "Codex Script";
    // MCP 工具以 `mcp__<server>__<tool>`。显示工具,然后显示它来自的服务器:
    // "Search Issues · github" ,
    // 没有 "Mcp Github Search Issues".
    if (n.rfind("mcp__", 0) == 0) {
        const auto rest = n.substr(5);
        const auto sep  = rest.find("__");
        if (sep != std::string::npos && sep + 2 < rest.size())
            return tool_display_name(rest.substr(sep + 2)) + " \xc2\xb7 "
                 + rest.substr(0, sep);
    }
    // 回退 用于任何 UNMAPPED 工具名称( 选项) ( MCP
    // -provided 工具, 模型的原始名称, 未来构建 -ins 尚未
    // 列入上表: -Case 时间线是连续的 永远没有工具 渲染 作为赤小写符号。
    // '_'/'-' / 空格, 上列每个字的第一个字母, 并将分隔符变为
    // 空格( $ e.g. "foo_bar" -> "Foo Bar").
    if (n.empty()) return n;
    std::string out;
    out.reserve(n.size());
    bool at_word_start = true;
    for (char c : n) {
        if (c == '_' || c == '-' || c == ' ') {
            if (!out.empty() && out.back() != ' ') out.push_back(' ');
            at_word_start = true;
            continue;
        }
        if (at_word_start && c >= 'a' && c <= 'z')
            out.push_back(static_cast<char>(c - 'a' + 'A'));
        else
            out.push_back(c);
        at_word_start = false;
    }
    return out;
}

// Tool类——表示颜色+统计徽章的语义分类.
// 压缩了托尔图, 长长的检查顺序 -class 工具读作平静的灰色清单,
// 以及有意义的行动(第2段)。 edit/write/bash ) 突出
// 为饱和行。状态 -axis 颜色( E) green/red/yellow
// )是保留给\\\\x/\x图标和呼吸边框的——从不作为分类,
// 所以绿色并不意味着 "execute" 和 "ok" 同时也是
//
// 每个工具都有一个亮的饱和类别色素,因此头文件(
// "✓ Name detail")行携带着真实的颜色,
// 横跨整个时间线,而不仅仅是饱和的少数. 正文(线沟,
// 管,消除标记)内含的染色体在同一色素读取——工具的
// 颜色身份带有顶端的-to-bottom.
//
//   检查( read/grep/glob/list/find/diag/web)  → code_path (
//   闪亮的青色, "code refs" 执行(bash) □ code_text (青色, ,
//   ) "active" ) 变异( edit/write)
//   → role_brand (马格塔语)vcs(git ⁇ ) ~ role_info (蓝色,
//   "context-shift" 计划(待办) □ status_warn (黄,规划)
maya::Color tool_category_color(const std::string& n) {
    if (n == "edit" || n == "write" || n == "move" || n == "remove"
        || n == "apply_patch" || n == "replace" || n == "rewrite_structural")
        return role_brand;
    if (n == "bash" || n == "shell" || n == "diagnostics" || n == "test"
        || n.rfind("process_", 0) == 0)
        return code_text;
    if (n == "todo")                  return status_warn;
    if (n == "task")                  return role_brand_alt;   // 代理身份
    if (n.rfind("git_", 0) == 0)      return role_info;
    return code_path;
}

std::string_view tool_category_label(const std::string& n) {
    if (n == "edit" || n == "write" || n == "move" || n == "remove"
        || n == "apply_patch" || n == "replace" || n == "rewrite_structural")
        return "mutate";
    if (n == "bash" || n == "shell" || n == "diagnostics" || n == "test"
        || n.rfind("process_", 0) == 0)
        return "execute";
    if (n == "todo")                  return "plan";
    if (n == "task")                  return "agent";
    if (n.rfind("git_", 0) == 0)      return "vcs";
    return "inspect";
}

// ── 状态映射 ──────────────────────────────────────────────────────

// `Approved` 归并 进入运行中,因为两者 渲染
// 相同内容 -flight 加载动画 - 部件不需要单独的舞台
maya::AgentEventStatus tool_event_status(const ToolUse& tc) {
    if (tc.is_running() || tc.is_approved()) return maya::AgentEventStatus::Running;
    if (tc.is_pending())                     return maya::AgentEventStatus::Pending;
    if (tc.is_done())                        return maya::AgentEventStatus::Done;
    if (tc.is_failed())                      return maya::AgentEventStatus::Failed;
    return maya::AgentEventStatus::Rejected;
}

// ── 详细行 ─────────────────────────────────────────────────────────

namespace {

[[nodiscard]] bool safe_bool_arg(const nlohmann::json& args,
                                 const char* key, bool fallback = false) {
    if (!args.is_object()) return fallback;
    auto it = args.find(key);
    return it != args.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

// 相对于发射的cwd,其他 "~/" 家的根子是来自 Model::env 以每笔金额
// 计算 -frame 出版( 4月 4日) ui_live.hpp ),从不从 OS.
std::string pretty_path(std::string p) {
    if (p.empty()) return p;
    const auto roots = agentty::ui_prefs::path_roots();
    const std::string& cwd = roots.cwd;
    if (!cwd.empty() && p.size() > cwd.size()
        && p.compare(0, cwd.size(), cwd) == 0 && p[cwd.size()] == '/')
        return p.substr(cwd.size() + 1);
    const std::string& h = roots.home;
    if (!h.empty() && p.size() > h.size() && p.compare(0, h.size(), h) == 0
        && p[h.size()] == '/')
        return std::string{"~/"} + p.substr(h.size() + 1);
    return p;
}

// 解析 工具输出中开始的首数 "Found N …" (grep: 读取:
// "Found N matches across M files" , 缩写: "Found N file(s):").
// 当输出不匹配时返回 -1,所以呼叫者可以退后.
// 计算已实现 LINES (旧行为)夸大了数字——grep
// 输出包括__上下文. 头文件 ,每击2条上下文线和空白分隔线,
// 所以 "· 45 matches" 显示3个真正的点击。
[[nodiscard]] int parse_found_count(std::string_view out) {
    constexpr std::string_view kTag = "Found ";
    if (out.rfind(kTag, 0) != 0) return -1;
    int n = 0;
    bool got = false;
    for (std::size_t i = kTag.size(); i < out.size(); ++i) {
        const char c = out[i];
        if (c >= '0' && c <= '9') { n = ::agentty::util::push_digit(n, c); got = true; }
        else break;
    }
    return got ? n : -1;
}

// 真实行数 `read`. count_lines(card_output (tc)
// 是指: WRONG 这里: 已读输出被装饰—— 选项 `display_description` 线条,
// 可选 符号 头文件 和一条线索 `[showing lines A-B of N ...]`
// / `[... of N ...]` 提示工具附件——所以 -counting
// 行会夸大数字, 并不同意文件。 工具已经报告文件的真长。
// `of N` 提示中( 也 `of N lines` 输入
// outline/1-KiB 回退 ; 读作权威的计数。 当没有这样的标注时
// 返回 - 1 (一个小文件读完整, 没有切换), 所以调用者会回到一个简单的内容。
// -line 数数
[[nodiscard]] int parse_read_total_lines(std::string_view out) {
    // 读取文件的正长, 以两个形状 :
    //   • 内容 / 1-KiB 回退: "[showing lines A-B of N ...]" - " of N"
    //     之后是结束 -of-number + (" lines" | "]" | ";" | " more").
    //   • 符号 大纲 : "path — K definitions across N lines:" - 电话
    //     计数在"across "之后。
    // 扫描两者; 最后的可视匹配胜负( 标记总是在后缀提示 /
    // 头文件 中, 从未更早的表达式中)。 当两个形状
    // 都没有时返回 - 1 (小文件读完整)。
    auto num_after = [&](std::size_t i, int& n) -> std::size_t {
        n = 0; bool got = false;
        while (i < out.size() && out[i] >= '0' && out[i] <= '9') {
            n = n * 10 + (out[i] - '0'); got = true; ++i;
        }
        return got ? i : std::string::npos;
    };
    int found = -1;
    for (std::size_t p = out.find(" of "); p != std::string::npos;
         p = out.find(" of ", p + 1)) {
        int n = 0;
        std::size_t e = num_after(p + 4, n);
        if (e == std::string::npos) continue;
        std::string_view rest{out.data() + e, out.size() - e};
        if (rest.rfind(" lines", 0) == 0 || rest.rfind("]", 0) == 0
         || rest.rfind(";", 0) == 0    || rest.rfind(" more", 0) == 0)
            found = n;
    }
    for (std::size_t p = out.find("across "); p != std::string::npos;
         p = out.find("across ", p + 1)) {
        int n = 0;
        std::size_t e = num_after(p + 7, n);
        if (e == std::string::npos) continue;
        std::string_view rest{out.data() + e, out.size() - e};
        if (rest.rfind(" lines", 0) == 0) found = n;
    }
    return found;
}

// 范围 a `read` 实际返回,从其 "[showing lines A-B of N"
// foder. 初解 ====没有这样的foper(一个完整的小文件)时,为0.
struct ShownRange { int first = 0, second = 0, total = 0; };
[[nodiscard]] ShownRange parse_read_shown_range(std::string_view out) {
    constexpr std::string_view kTag = "[showing lines ";
    const auto p = out.rfind(kTag);
    if (p == std::string::npos) return {};
    ShownRange r;
    std::size_t i = p + kTag.size();
    auto num = [&](int& v) {
        bool got = false;
        while (i < out.size() && out[i] >= '0' && out[i] <= '9') {
            v = ::agentty::util::push_digit(v, out[i]); got = true; ++i;
        }
        return got;
    };
    if (!num(r.first) || i >= out.size() || out[i] != '-') return {};
    ++i;
    if (!num(r.second)) return {};
    if (out.compare(i, 4, " of ") == 0) { i += 4; num(r.total); }
    return r;
}

} // 命名空间结束。

// 一个 -line "what this tool is doing"
// 时间轴。 Tool-specific 这样用户就可以一看就能读取
// 序列:fs ops的路径,shash的实际命令,grep的图案等.
// 工具确定后, 归并 员额 -completion 数据显示,
// 所以时间表作为紧凑结果日志加倍,而不扩展单个卡片.
// 简洁,人 -readable 失败工具的失败原因, 取自其输出。
// Tool 错误 渲染 以 "[kind] detail" (
// ToolError::render )我们想要的是 KIND ("invalid args",
// "out of workspace", "not found" ,（位于工作区之外，或文件未找到。）
// )加一小片细节,所以 头文件 告诉用户 原因 而不是一目了然地看它是否成功。
// "[exit code N]" / "[terminated by signal …]" 拖车,
// 我们也会嗅到。返回 "" 没有什么意义可以显示的
static std::string tool_failure_reason(const ToolUse& tc) {
    const std::string_view out = card_output(tc);
    if (out.empty()) return {};

    // "[kind] detail" 结构化 ToolError 形状。若干标记可以堆叠(
    // "[unknown] [not found] …" ); 跳过模糊 `unknown` 一个
    std::string_view rest = out;
    while (!rest.empty() && rest.front() == '[') {
        const auto close = rest.find(']');
        if (close == std::string_view::npos || close < 2 || close > 32) break;
        std::string kind{rest.substr(1, close - 1)};
        // 括号内的出口拖车("[exit code 1]")不是
        // 那种——倒到下面的沙滩嗅觉。
        if (kind.rfind("exit code", 0) == 0 || kind.rfind("terminated", 0) == 0)
            break;
        if (kind != "unknown") return kind;
        rest.remove_prefix(close + 1);
        while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
    }
    if (rest.size() != out.size()) return "failed";
    // 贝什 -style 拖车,不管它降落在什么地方
    if (out.find("[terminated by signal") != std::string::npos)
        return "terminated";
    if (auto p = out.rfind("[exit code "); p != std::string::npos) {
        auto e = out.find(']', p);
        if (e != std::string::npos)
            return std::string{out.substr(p + 1, e - p - 1)};   // "exit code N"
    }
    // 无结构错误文本:取第一短行.
    if (out.starts_with("Tool call arguments look incomplete")
     || out.starts_with("tool args truncated"))
        return "args cut off";
    std::string first{out.substr(0, out.find('\n'))};
    if (first.size() > 48) first = first.substr(0, 47) + "\xe2\x80\xa6";
    return first;
}

// 人均数 -tool 细节 正文 (成功) (中文(简体) ). -shaped).
// tool_timeline_detail 包成此内容以附加一个统一的失败摘要,
// 因此没有任何单个工具分支需要记住表露自己的失败——一个代码路径拥有它.
static std::string tool_timeline_detail_base(const ToolUse& tc) {
    // 流式传输-aware 字符串 arg:先解析(一旦 ~120
    // ms reparse 追赶后授权),然后是原始部分的碎片 JSON 因此,
    // 该值显示 INSTANT 编号 字节 到达 传输。
    // 统一应用在这里 每个 工具的 头文件 (命令、模式、查询、url、
    // ref、 符号 ,...) 期间开始直播 流式传输 而不是光着身子坐
    // 着 "…" ——同样的修复文件路径得到,现在所有参数在一个地方.
    auto safe = [&](const char* k) -> std::string {
        if (auto v = safe_arg(tc.args, k); !v.empty()) return v;
        return pick_streaming_string(tc.args_streaming, k);
    };
    auto path = tool_path_arg(tc);
    const auto& n = tc.name.value;
    const auto path_pp = pretty_path(path);

    if (n == "read") {
        auto detail = path_pp.empty() ? std::string{"\xe2\x80\xa6"} : path_pp;
        // 符号 读起来是鼓励的 idiom —— 比如 内容 被读到,不只是在什么地方。
        // 流式传输-live 就像其他的 ⁇ 一样 safe().
        if (auto sym = safe("symbol"); !sym.empty())
            detail += " \xc2\xb7 " + sym + "()";
        // 两种拼写方式都是范围起始(offset / Zed-style start_line).
        int off = safe_int_arg(tc.args, "offset", 0);
        if (off == 0) off = safe_int_arg(tc.args, "start_line", 0);
        if (tc.is_done()) {
            // 刚才是什么 SHOWN ,从工具的自 "[showing lines A-B of N]"
            // 页脚: 1 5 -line 读作349 -line 文件已读
            // "131–135 of 349" ,没有 "349 lines" 看上去是卡片的尺寸
            if (auto r = parse_read_shown_range(card_output(tc)); r.first > 0) {
                detail += "  \xc2\xb7  " + std::to_string(r.first) + "\xe2\x80\x93"
                        + std::to_string(r.second);
                if (r.total > 0) detail += " of " + std::to_string(r.total);
                return detail;
            }
            if (off > 0) detail += ":" + std::to_string(off);
            // 优先使用文件的真实长度, 以读取其中的报告
            // "[showing lines A-B of N]" 页脚;
            // 只重 -count 输出( 装饰) 当没有这样的标记( 小文件读完整)。
            int lines = parse_read_total_lines(card_output(tc));
            if (lines < 0) lines = count_lines(card_output(tc));
            if (lines > 1) detail += "  \xc2\xb7  " + std::to_string(lines) + " lines";
        } else if (off > 0) {
            detail += ":" + std::to_string(off);
        }
        return detail;
    }
    if (n == "write") {
        // LIFECYCLE-STABLE :只有路径,永远——与下文编辑的规则相同.
        // 旧的 -only "(N lines, X KB)" 重写承诺 头文件
        // 随时按行结算 流式传输 写卡已经推了 头文件 上方为 可视区域 顶端
        // ( U) 末尾-window → show_all 是净数
        // GROW 在那的时候 帧 门 门 HardReset。。 内容。
        // lines/bytes 数据 渲染 已解决的 正文 页脚( E)
        // show_footer_stats ),它画在下方 接口边界.
        return path_pp.empty() ? std::string{"\xe2\x80\xa6"} : path_pp;
    }
    if (n == "edit") {
        if (path_pp.empty()) return "\xe2\x80\xa6";
        // 头文件 细节是 LIFECYCLE-STABLE 永远的走在路上
        // "· N edits" 数和数 "(…)" 输出回声——两者均仅出现在
        // 终端 但这个细节 渲染 自动调试 事件 HEADER 横行,可以承诺本土
        // 回滚缓冲区 编辑时 数据流 (现场直播) 末尾 高; 下方的流派将卡片推高。
        // A 完成 -only 后缀 REWRITES 结账的行数 帧 ——这是净值.
        // GROW 编辑( 已关闭) 差异块 → show_all 完全的diff),
        // 所以,("")",""(")"("""),"("(","))"(("))
        // maya 只能靠破坏力才能恢复 HardReset (无)
        // scrollback_oracle write/edit 转过来,抓住这个T*
        // -e-post。。 内容。 差异块 数着住在定居的 正文's
        // "edit i/N" 带 头文件 ; 什么都没有丢失。
        return path_pp;
    }
    if (n == "move") {
        // safe() 谓 流式传输-aware ,所以,我说 source/destination 现场直播也。
        auto src = pretty_path(safe("source"));
        auto dst = pretty_path(safe("destination"));
        if (src.empty() && dst.empty()) return "\xe2\x80\xa6";
        return src + "  \xe2\x86\x92  " + dst;
    }
    if (n == "remove") {
        auto detail = path_pp.empty() ? std::string{"\xe2\x80\xa6"} : path_pp;
        if (safe_bool_arg(tc.args, "recursive"))
            detail += "  \xc2\xb7  recursive";
        return detail;
    }
    if (n == "bash" || n == "shell" || n == "diagnostics") {
        auto cmd = safe("command");
        if (cmd.empty()) {
            // args从未到达(已取消 / 断:说它是为了什么.
            auto why = safe("display_description");
            return why.empty() ? std::string{"\xe2\x80\xa6"} : why;
        }
        // 显示真实命令, 而不是 display_description。
        if (auto nl = cmd.find('\n'); nl != std::string::npos)
            cmd = cmd.substr(0, nl) + " \xe2\x80\xa6";
        if (tc.is_done()) {
            int rc = parse_exit_code(card_output(tc));
            if (rc != 0) cmd += "  \xc2\xb7  exit " + std::to_string(rc);
        }
        return cmd;
    }
    if (n == "process_start") {
        auto cmd = safe("command");
        if (cmd.empty()) return "\xe2\x80\xa6";
        if (auto nl = cmd.find('\n'); nl != std::string::npos)
            cmd = cmd.substr(0, nl) + " \xe2\x80\xa6";
        return cmd;
    }
    if (n == "process_poll" || n == "process_stop") {
        auto id = safe("id");
        if (id.empty()) id = safe("session_id");
        return id.empty() ? std::string{"\xe2\x80\xa6"} : "session " + id;
    }
    if (n == "test") {
        auto detail = safe("command");
        if (detail.empty()) detail = safe("filter");
        if (detail.empty()) detail = safe("target");
        if (detail.empty()) detail = "auto-detect";
        if (auto nl = detail.find('\n'); nl != std::string::npos)
            detail = detail.substr(0, nl) + " \xe2\x80\xa6";
        return detail;
    }
    if (n == "grep") {
        auto pat = safe("pattern");
        if (pat.empty()) return "\xe2\x80\xa6";
        std::string detail = path_pp.empty() ? pat : pat + "  in  " + path_pp;
        if (tc.is_done()) {
            int matches = parse_found_count(card_output(tc));
            if (matches == 0)     detail += "  \xc2\xb7  no matches";
            else if (matches > 0) detail += "  \xc2\xb7  " + std::to_string(matches)
                                          + (matches == 1 ? " match" : " matches");
        }
        return detail;
    }
    if (n == "glob") {
        auto pat = safe("pattern");
        if (pat.empty()) return "\xe2\x80\xa6";
        std::string detail = pat;
        if (tc.is_done()) {
            const auto out = card_output(tc);
            int hits = parse_found_count(out);
            if (out.starts_with("Found more than "))
                detail += "  \xc2\xb7  500+ hits";
            else if (hits == 0) detail += "  \xc2\xb7  no hits";
            else if (hits > 0)  detail += "  \xc2\xb7  " + std::to_string(hits)
                                        + (hits == 1 ? " hit" : " hits");
        }
        return detail;
    }
    if (n == "extract" || n == "aggregate") {
        // 模式 -projection 工具: regex 是故事;
        // 在当前时添加路径范围。 (这些以前掉到 display_description
        // — BLANK 每当模型省略它。 )
        auto pat = safe("pattern");
        if (pat.empty()) return "\xe2\x80\xa6";
        if (pat.size() > 48) {
            pat.resize(tools::util::safe_utf8_cut(pat, 47));
            pat += "\xe2\x80\xa6";
        }
        std::string detail = path_pp.empty() ? pat : pat + "  in  " + path_pp;
        // "Extracted N values." / "Aggregated N matches by ..." 数据
        if (tc.is_done()) {
            const std::string_view out = card_output(tc);
            const std::string_view lead = n == "extract" ? "Extracted " : "Aggregated ";
            if (out.starts_with(lead)) {
                int v = 0; std::size_t i = lead.size(); bool got = false;
                while (i < out.size() && out[i] >= '0' && out[i] <= '9' && v < 100000000) {
                    v = v * 10 + (out[i++] - '0'); got = true;
                }
                if (got) {
                    const char* one  = n == "extract" ? " value" : " match";
                    const char* many = n == "extract" ? " values" : " matches";
                    detail += "  \xc2\xb7  " + (v == 0 ? std::string{"none"}
                                                    : std::to_string(v) + (v == 1 ? one : many));
                }
            }
        }
        return detail;
    }
    if (n == "read_filter") {
        auto pat = safe("pattern");
        std::string detail = path_pp.empty() ? std::string{"\xe2\x80\xa6"} : path_pp;
        if (!pat.empty()) detail += "  \xc2\xb7  " + pat;
        if (tc.is_done()) {
            // 工具的吗? 头文件 拥有真实的数 : "path — N matching lines of M"。
            // 计数 正文 行将包括上下文线。
            const std::string_view out = card_output(tc);
            int kept = -1;
            constexpr std::string_view kTag = " matching line";
            if (auto at = out.substr(0, out.find('\n')).find(kTag); at != std::string_view::npos) {
                std::size_t b = at;
                while (b > 0 && out[b - 1] >= '0' && out[b - 1] <= '9') --b;
                if (b < at) {
                    kept = 0;
                    for (std::size_t i = b; i < at && kept < 100000000; ++i)
                        kept = kept * 10 + (out[i] - '0');
                }
            }
            if (kept == 0)
                detail += "  \xc2\xb7  no matches";
            else if (kept > 0)
                detail += "  \xc2\xb7  " + std::to_string(kept)
                        + (kept == 1 ? " match" : " matches");
        }
        return detail;
    }
    if (n == "json_query") {
        auto q = safe("query");
        if (q.empty()) q = "\xe2\x80\xa6";
        return path_pp.empty() ? q : path_pp + "  \xc2\xb7  " + q;
    }
    if (n == "outline")
        return path_pp.empty() ? std::string{"\xe2\x80\xa6"} : path_pp;
    if (n == "apply_patch") {
        // 镜像编辑:路径是身份; 差异块 从 补丁.
        std::string detail = path_pp.empty() ? std::string{"\xe2\x80\xa6"} : path_pp;
        auto patch = safe("patch");
        if (!patch.empty()) {
            int hunks = 0;
            for (std::size_t p = 0;
                 (p = patch.find("@@", p)) != std::string::npos; p += 2)
                ++hunks;
            hunks /= 2;   // – 差异块 头文件 携带一对  ⁇   ⁇  .
            if (hunks > 0)
                detail += "  \xc2\xb7  " + std::to_string(hunks)
                        + (hunks == 1 ? " hunk" : " hunks");
        }
        return detail;
    }
    if (n == "replace") {
        // 查找 ~ 替换到一个 glob; dry-run/apply 模式很重要。
        auto find_s = safe("find");
        if (find_s.empty()) return "\xe2\x80\xa6";
        if (find_s.size() > 32) {
            find_s.resize(tools::util::safe_utf8_cut(find_s, 31));
            find_s += "\xe2\x80\xa6";
        }
        std::string detail = find_s;
        if (auto g = safe("glob"); !g.empty()) detail += "  in  " + g;
        if (!safe_bool_arg(tc.args, "apply"))
            detail += "  \xc2\xb7  dry-run";
        return detail;
    }
    if (n == "list_dir") {
        std::string detail = path_pp.empty() ? std::string{"."} : path_pp;
        if (tc.is_done()) {
            // 仅计算真实的条目数。 list_dir 输出可以带一个主线
            // `display_description` 线条和后继
            // `[>1000 entries, …]` / `[… truncated …]` 通知;
            // 计数( 未计数) count_lines 结束了 -reports 全部。
            // 跳过描述预置和括号内的通知行。
            const std::string desc = safe_arg(tc.args, "display_description");
            int entries = 0;
            const std::string_view out = card_output(tc);
            std::size_t start = 0;
            bool first = true;
            while (start <= out.size()) {
                std::size_t nl = out.find('\n', start);
                std::string_view line{out.data() + start,
                    (nl == std::string::npos ? out.size() : nl) - start};
                const bool is_desc = first && !desc.empty() && line == desc;
                const bool is_notice = !line.empty() && line.front() == '[';
                if (!line.empty() && !is_desc && !is_notice) ++entries;
                first = false;
                if (nl == std::string::npos) break;
                start = nl + 1;
            }
            if (entries > 0) detail += "  \xc2\xb7  " + std::to_string(entries) + " entries";
        }
        return detail;
    }
    if (n == "find_definition") {
        std::string detail = safe("symbol");
        if (tc.is_done()) {
            int hits = 0;
            const std::string_view out = card_output(tc);
            for (std::size_t p = 0; (p = out.find("## Matches in ", p)) != std::string::npos; p += 14)
                ++hits;
            if (hits > 0) detail += "  \xc2\xb7  " + std::to_string(hits)
                                 + (hits == 1 ? " file" : " files");
        }
        return detail;
    }
    if (n == "search_structural") {
        auto pattern = safe("pattern");
        if (pattern.empty()) pattern = "\xe2\x80\xa6";
        if (auto nl = pattern.find('\n'); nl != std::string::npos)
            pattern = pattern.substr(0, nl) + " \xe2\x80\xa6";
        return path_pp.empty() ? pattern : pattern + "  in  " + path_pp;
    }
    if (n == "repo_map") {
        auto focus = safe("focus");
        auto root = path_pp.empty() ? std::string{"."} : path_pp;
        return focus.empty() ? root : focus + "  in  " + root;
    }
    if (n == "web_fetch") {
        std::string detail = safe("url");
        if (tc.is_done()) {
            const std::string_view out = card_output(tc);
            auto nl = out.find('\n');
            if (nl != std::string::npos && out.starts_with("HTTP ")) {
                auto sp = out.find(' ', 5);
                if (sp != std::string::npos)
                    detail += "  \xc2\xb7  " + std::string{out.substr(5, sp - 5)};
            }
        }
        return detail;
    }
    if (n == "web_search") {
        std::string detail = safe("query");
        if (tc.is_done()) {
            int hits = 0;
            const std::string_view out = card_output(tc);
            for (std::size_t p = 0; p + 1 < out.size(); ++p) {
                if ((p == 0 || out[p - 1] == '\n')
                    && std::isdigit(static_cast<unsigned char>(out[p]))
                    && (out[p+1] == '.' || (p + 2 < out.size() && out[p+2] == '.')))
                    ++hits;
            }
            if (hits > 0) detail += "  \xc2\xb7  " + std::to_string(hits)
                                 + (hits == 1 ? " result" : " results");
        }
        return detail;
    }
    if (n == "git_show") {
        auto ref = safe("ref");
        if (ref.empty()) ref = "HEAD";
        return path_pp.empty() ? ref : ref + "  \xc2\xb7  " + path_pp;
    }
    if (n == "git_blame") {
        std::string detail = path_pp.empty() ? std::string{"\xe2\x80\xa6"} : path_pp;
        auto start = safe_int_arg(tc.args, "start_line", 0);
        auto end = safe_int_arg(tc.args, "end_line", 0);
        if (start > 0) {
            detail += "  @" + std::to_string(start);
            if (end > start) detail += "-" + std::to_string(end);
        }
        return detail;
    }
    if (n == "git_commit") {
        auto m = safe("message");
        if (auto nl = m.find('\n'); nl != std::string::npos) m = m.substr(0, nl);
        if (tc.is_done()) {
            const std::string_view out = card_output(tc);
            auto open = out.find('[');
            if (open != std::string::npos) {
                auto close = out.find(']', open);
                auto sp = out.find(' ', open + 1);
                if (sp != std::string::npos && sp < close) {
                    std::string hash{out.substr(sp + 1, close - sp - 1)};
                    if (!hash.empty() && hash.size() <= 12)
                        m += "  \xc2\xb7  " + hash;
                }
            }
        }
        return m;
    }
    if (n == "git_status" && tc.is_done()) {
        const std::string_view out = card_output(tc);
        std::string branch;
        int modified = 0, staged = 0, untracked = 0, conflicts = 0;
        bool seen_branch = false;
        std::size_t lo = 0;
        while (lo < out.size()) {
            auto eol = out.find('\n', lo);
            std::string_view line{out.data() + lo,
                (eol == std::string::npos ? out.size() : eol) - lo};
            // 瓷v1:可选预留的display_description线,
            // 然后是"## branch...upstream [ahead/behind]"
            // XXXX头文件,每变一行"XY path"("?? "=无轨).
            // `## `线上的Anchor,所以一个描述不能误算为变化.
            if (!seen_branch) {
                if (line.starts_with("## ")) {
                    seen_branch = true;
                    std::string_view b = line.substr(3);
                    for (std::string_view pre : {"No commits yet on ", "Initial commit on "})
                        if (b.starts_with(pre)) b.remove_prefix(pre.size());
                    std::size_t cut = b.size();
                    if (auto d = b.find("..."); d != std::string_view::npos)
                        cut = std::min(cut, d);
                    if (auto s = b.find(' '); s != std::string_view::npos)
                        cut = std::min(cut, s);
                    if (!b.starts_with("HEAD (no branch)"))
                        branch = std::string{b.substr(0, cut)};
                }
            } else if (line.size() >= 3 && line[2] == ' ') {
                const char x = line[0], y = line[1];
                if (x == '?' && y == '?')                ++untracked;
                else if (x == 'U' || y == 'U' || (x == 'A' && y == 'A')
                         || (x == 'D' && y == 'D'))      ++conflicts;
                else {
                    if (x != ' ' && x != '?') ++staged;    // 索引侧
                    if (y != ' ' && y != '?') ++modified;  // 工作树侧
                }
            }
            if (eol == std::string::npos) break;
            lo = eol + 1;
        }
        std::string detail = branch.empty() ? std::string{"(detached)"} : branch;
        if (modified || staged || untracked || conflicts) {
            detail += "  \xc2\xb7  ";
            bool first = true;
            auto add = [&](int n_, const char* suffix) {
                if (n_ <= 0) return;
                if (!first) detail += " ";
                detail += std::to_string(n_) + suffix;
                first = false;
            };
            add(modified, "M"); add(staged, "S"); add(untracked, "?");
            add(conflicts, "U");
        } else {
            detail += "  \xc2\xb7  clean";
        }
        return detail;
    }
    if (n == "git_diff" || n == "git_log" || n == "git_status")
        return path_pp.empty() ? std::string{"."} : path_pp;
    if (n == "remember") {
        auto text = safe("text");
        if (text.empty()) return "\xe2\x80\xa6";
        if (auto nl = text.find('\n'); nl != std::string::npos)
            text = text.substr(0, nl) + " \xe2\x80\xa6";
        auto scope = safe("scope");
        if (scope.empty()) scope = "project";
        std::string detail = "[" + scope + "] " + text;
        if (tc.is_done()) {
            const std::string_view out = card_output(tc);
            if (auto p = out.find("id="); p != std::string::npos) {
                auto end = out.find_first_of(")\n ", p + 3);
                if (end != std::string::npos && end - (p + 3) <= 16)
                    detail += "  \xc2\xb7  " + std::string{out.substr(p + 3, end - p - 3)};
            }
        }
        return detail;
    }
    if (n == "forget") {
        auto id = safe("id");
        auto sub = safe("substring");
        std::string detail = !id.empty() ? ("id=" + id)
                            : !sub.empty() ? ("\xe2\x80\x9c" + sub + "\xe2\x80\x9d")
                            : std::string{"\xe2\x80\xa6"};
        if (auto nl = detail.find('\n'); nl != std::string::npos)
            detail = detail.substr(0, nl) + " \xe2\x80\xa6";
        if (tc.is_done()) {
            const std::string_view out = card_output(tc);
            if (out.starts_with("Forgot ")) {
                auto sp = out.find(' ', 7);
                if (sp != std::string::npos) {
                    std::string count{out.substr(7, sp - 7)};
                    detail += "  \xc2\xb7  " + count + " removed";
                }
            } else if (out.starts_with("No memory matched")) {
                detail += "  \xc2\xb7  no match";
            }
        }
        return detail;
    }
    if (n == "wipe_memory") {
        auto scope = safe("scope");
        if (scope.empty()) scope = "project";
        const bool confirm = safe_bool_arg(tc.args, "confirm");
        return "[" + scope + "] " + (confirm ? "confirmed wipe" : "preview");
    }
    if (n == "skill") {
        auto name = safe("name");
        return name.empty() ? std::string{"\xe2\x80\xa6"} : name;
    }
    if (n == "search_docs" || n == "search_code" || n == "tool_search") {
        auto query = safe("query");
        if (query.empty()) return "\xe2\x80\xa6";
        if (auto nl = query.find('\n'); nl != std::string::npos)
            query = query.substr(0, nl) + " \xe2\x80\xa6";
        return query;
    }
    if (n == "mcp__prompts__get") {
        auto name = safe("name");
        return name.empty() ? std::string{"\xe2\x80\xa6"} : name;
    }
    if (n == "todo") {
        if (tc.args.is_object()) {
            auto it = tc.args.find("todos");
            if (it != tc.args.end() && it->is_array() && !it->empty()) {
                int total = 0, done = 0, in_progress = 0;
                for (const auto& td : *it) {
                    if (!td.is_object()) continue;
                    ++total;
                    auto status_it = td.find("status");
                    auto st = status_it != td.end() && status_it->is_string()
                        ? status_it->get<std::string>() : std::string{"pending"};
                    if (st == "completed")        ++done;
                    else if (st == "in_progress") ++in_progress;
                }
                std::string detail = std::to_string(done) + "/" + std::to_string(total);
                if (in_progress > 0)
                    detail += "  \xc2\xb7  " + std::to_string(in_progress) + " in progress";
                return detail;
            }
        }
        return "\xe2\x80\xa6";
    }
    if (n == "task") {
        // 头文件 细节 : "type · what it's doing"。
        // 代理类型引导——平行扇形 -out 子代理是用户扫描时的歧义者。
        // 提示词 模型省略时的第一道行 display_description。
        // 一旦结算,请将报告所分析的转弯计数附加到后面 头文件 (
        // "Subagent report (type, N turns):"
        // )所以卡作为紧凑的结果记录而双倍.
        std::string type = safe("agent_type");
        if (type.empty()) type = "general";
        // 证明:a PROJECT-scoped 骑着马冲进 工作
        // 区 (克隆人可以写出其角色) 提示词 ),所以标记它,
        // 所以注射剂永远是隐形的。 -in 并且用户自己的代理没有标记
        // ——干净的普通案件。透明,不是一个门:代理仍然运行,
        // 其工具仍然通过正常 permission/sandbox 检查。
        std::string type_tag = type;
        bool project = false;
        for (const auto& a : agentty::ui_prefs::path_roots().project_agents)
            if (a == type) { project = true; break; }
        if (project) type_tag += " (project agent)";
        std::string what = safe("display_description");
        if (what.empty()) {
            what = safe("prompt");
            if (auto nl = what.find('\n'); nl != std::string::npos)
                what.resize(nl);
            if (what.size() > 60) {
                what.resize(tools::util::safe_utf8_cut(what, 57));
                what += "\xe2\x80\xa6";
            }
        }
        std::string detail = type_tag;
        if (!what.empty()) detail += "  \xc2\xb7  " + what;
        if (tc.is_terminal()) {
            const std::string_view out = card_output(tc);
            if (auto p = out.find("report ("); p != std::string::npos) {
                if (auto end = out.find(')', p); end != std::string::npos) {
                    // "(type, N turns)" - 仅保留 "N turns" 半;
                    // 该类型已经引导细节。
                    std::string inner{out.substr(p + 8, end - (p + 8))};
                    if (auto comma = inner.find(", "); comma != std::string::npos)
                        inner = inner.substr(comma + 2);
                    detail += "  \xc2\xb7  " + inner;
                }
            }
        }
        return detail;
    }
    if (n == "git_branch" || n == "git_stash" || n == "git_rebase") {
        // 行动+其目标, e.g. "switch master", "onto origin/master".
        std::string action = safe("action");
        if (action.empty()) action = "list";
        std::string target = safe("name");
        if (target.empty()) target = safe("upstream");
        if (target.empty()) target = safe("ref");
        if (target.empty()) target = safe("message");
        return target.empty() ? action : action + "  " + target;
    }
    if (n == "git_cherry_pick") {
        std::string action = safe("action");
        std::string commits;
        if (tc.args.is_object()) {
            auto it = tc.args.find("commits");
            if (it != tc.args.end() && it->is_array()) {
                for (const auto& c : *it)
                    if (c.is_string()) {
                        if (!commits.empty()) commits += " ";
                        commits += c.get<std::string>();
                    }
            } else if (it != tc.args.end() && it->is_string()) {
                commits = it->get<std::string>();
            }
        }
        if (action.empty() || action == "pick")
            return commits.empty() ? std::string{"pick"} : "pick  " + commits;
        return action;
    }
    if (n == "find_references") {
        auto sym = safe("symbol");
        if (sym.empty()) return "\xe2\x80\xa6";
        return path_pp.empty() || path_pp == "." ? sym
                                                 : sym + "  in  " + path_pp;
    }
    if (auto d = safe_arg(tc.args, "display_description"); !d.empty()) return d;
    // 无描述的工具( 多数) MCP 工具 : 显示其第一个几个短字符
    // 串的参数为 `key value` ,所以,请 头文件 仍然说什么跑。
    std::string detail;
    if (tc.args.is_object()) {
        int shown = 0;
        for (auto it = tc.args.begin(); it != tc.args.end() && shown < 3; ++it) {
            if (!it->is_string() && !it->is_number() && !it->is_boolean()) continue;
            std::string v = it->is_string() ? it->get<std::string>() : it->dump();
            if (v.empty()) continue;
            if (auto nl = v.find('\n'); nl != std::string::npos) v = v.substr(0, nl) + " \xe2\x80\xa6";
            if (v.size() > 40) { v.resize(39); v += "\xe2\x80\xa6"; }
            if (!detail.empty()) detail += "  \xc2\xb7  ";
            detail += it.key() + " " + v;
            ++shown;
        }
    }
    return detail;
}

// 公开进入:成功 -shaped 细节加失败工具的制服 "· reason" 后缀,
// 所以每个失败的工具告诉用户 原因 浏览( 失败) edit/read/bash
// 不再看起来与成功路径完全相同。 失败摘要由一个代码路径拥有。
// 单个工具分支保持成功。 -only.
// Rejected/aborted 工具是故意的 被保留在平原上,
// 它们不是错误, 并且他们的格莱弗已经说 "skipped".
// "(3+ 5-)" 从编辑/ 写入/ 开始 apply_patch 输出,
// 如 "+3 −5"。减值是 U+2212,所以 头文件 彩色不能与一个 标志.
static std::string change_stat(std::string_view out) {
    for (std::size_t p = out.find(" ("); p != std::string_view::npos;
         p = out.find(" (", p + 2)) {
        std::size_t i = p + 2, a = i;
        while (i < out.size() && out[i] >= '0' && out[i] <= '9') ++i;
        if (i == a || i + 1 >= out.size() || out[i] != '+' || out[i + 1] != ' ') continue;
        const auto add = out.substr(a, i - a);
        i += 2;
        const std::size_t r = i;
        while (i < out.size() && out[i] >= '0' && out[i] <= '9') ++i;
        if (i == r || i + 1 >= out.size() || out[i] != '-' || out[i + 1] != ')') continue;
        const auto rem = out.substr(r, i - r);
        std::string s = "+" + std::string{add};
        if (rem != "0") s += " \xe2\x88\x92" + std::string{rem};
        return s;
    }
    return {};
}

std::string tool_timeline_detail(const ToolUse& tc) {
    std::string detail = tool_timeline_detail_base(tc);
    const auto& n = tc.name.value;
    if (tc.is_done() && (n == "edit" || n == "write" || n == "apply_patch")) {
        if (auto st = change_stat(card_output(tc)); !st.empty())
            detail += "  \xc2\xb7  " + st;
    }
    if (tc.is_failed()) {
        std::string reason = tool_failure_reason(tc);
        if (reason.empty()) reason = "failed";
        // 如果一个基部已经说过的话,就避免冗余的"· failed · failed".
        if (detail.find(reason) == std::string::npos)
            detail += detail.empty() ? reason : "  \xc2\xb7  " + reason;
    }
    return detail;
}

} // namespace agentty::ui
