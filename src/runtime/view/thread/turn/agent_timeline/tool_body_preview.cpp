// tool_body_preview_config - 电话
// ToolUse → ToolBodyPreview::Config 适配器。
// 结构化工具名称 分派 用于目的 -built 渲染器; 一个生命周期 回退
// 保证一个有用的 正文 包括没有习惯的本地工具 渲染器 和动态发现 MCP 工具。
// 共享 parsing/windowing 帮助者居住 tool_body_common . . {hpp, cpp} (中文(简体) ).
//
// 分派 命令遗迹 加载-bearing : 未完成的待办事宜 调用 绕过
// 复选框 渲染器 所以他们的错误被显示,而 edit/write/bash/task
// 保留其具有实际意义的专门产出。

#include "agentty/runtime/view/thread/turn/agent_timeline/tool_body_preview.hpp"

#include <maya/runtime.hpp>

#include "agentty/runtime/view/palette.hpp"
#include "agentty/runtime/view/thread/turn/agent_timeline/tool_helpers.hpp"
#include "tool_body_common.hpp"

#include <cctype>

#include "agentty/tool/util/utf8.hpp"

namespace agentty::ui {

// ── 冻结-build 范围 ─────────────────────────────────────
// 保留为否 -op-by-default 阶段 标志 给那些还问话的来电者
// `building_frozen()`。该词 正文 现在 IDENTICAL
// 阶段(全部, 接口边界-safe ),所以没有 渲染器 路径分支,用于内容;
// freeze_range 仍然有清晰的空间,所以未来会有所不同 -build 路径
// 有钩。
namespace {
// 电话 渲染-phase 标志 谓 LOOP-ONLY: FrozenBuildScope
// 被带入并释放在其中之一 view() 建造,和 view() 运行在内核的环 线程.
//
// 是个 thread_local ,但没有任何强制力——a 工作线程
// 请求 building_frozen () 读它自己的未受侵犯 `false`
// 默默地拿走了错的树枝 loop_bound<bool> 使终止命名规则
// 而不是悄悄错误 渲染。见 jaal/kernel/loop.hpp.
maya::loop_bound<bool> g_frozen_build;

bool frozen_build_get() noexcept {
    return g_frozen_build.with([](const bool& v) { return v; });
}
void frozen_build_set(bool v) noexcept {
    g_frozen_build.with([v](bool& slot) { slot = v; });
}

bool text_is_visible(std::string_view text) {
    const auto stripped = tools::util::strip_terminal_controls(text);
    for (unsigned char c : stripped) {
        if (!std::isspace(c) && c >= 0x20) return true;
    }
    return false;
}

bool has_visible_body(const maya::ToolBodyPreview::Config& body) {
    using Kind = maya::ToolBodyPreview::Kind;
    switch (body.kind) {
    case Kind::None:      return false;
    case Kind::EditDiff:  return !body.hunks.empty();
    case Kind::TodoList:  return !body.todos.empty();
    // 文件中的空白是有意义的 正文 因为他们 gutter/prefix 和
    // 脚脚使它显眼(一个空的) -line-only 文件仍然是真实的输出。
    case Kind::FileRead:
    case Kind::FileWrite: return !body.text.empty();
    default:              return text_is_visible(body.text);
    }
}

bool supports_generic_text_body(std::string_view name) {
    // 土著和 MCP 注册表 名字是犬科的,你应该知道 lowercase/snake 识别器。
    // Provider-specific 遗留标签( 例如) ACP
    // 历史 "Bash" 总是 头文件-only ; 追溯给它们
    // progress/output 旧的卡片几何图形 线程 和平行 -tool 转弯
    for (unsigned char c : name)
        if (c >= 'A' && c <= 'Z') return false;
    return true;
}

// 任何工具卡都不应该是一个无法解释的空白。
// 结构化的渲染器会先被拒绝;这个生命周期 回退 覆盖
// 所有本地工具以及动态发现的工具 MCP 工具,
// 包括此代码编译后添加的工具。 终端 产出得到保留,
// 真正沉默的国家得到一个简短的明确解释。
void generic_lifecycle_body(const ToolUse& tc,
                            maya::ToolBodyPreview::Config& out) {
    using Kind = maya::ToolBodyPreview::Kind;
    if (has_visible_body(out)) return;

    const bool generic_text = supports_generic_text_body(tc.name.value);
    out.text_color = text_tertiary;
    if (tc.is_failed()) {
        if (auto body = detail::failure_body_text(tc); text_is_visible(body)) {
            out.kind = Kind::Failure;
            out.text = std::move(body);
            out.chrome_color = status_error;
        }
        return;
    }
    if (tc.is_running()) {
        if (!generic_text) return;
        // 真正的进步是 正文 ; 合成器 "waiting" 排队不。
        // Tool 插入可在长助手前缀输入本地后发生 回滚缓冲区 ,
        // 并增加一个占位符行,在任何和解之前,将承诺的内容
        // 移位 -reconciliation 窗口已存在。
        // 事件 头文件已经显示了加载动画+参数细节用于
        // 无声工具,因此它仍然有用,而不破坏帧几何.
        if (text_is_visible(tc.progress_text())) {
            out.kind = Kind::CodeBlock;
            out.text = clean_output(tc.progress_text());
            out.is_streaming = true;
        }
        return;
    }
    if (tc.is_pending() || tc.is_approved()) {
        // 投入 流式传输 / 权限 状态由 事件 头文件
        // 保留 正文 在结构化参数或真正进展到达之前,
        // 在同一承诺下, -scrollback 不变量 上节。
        return;
    }
    if (generic_text && tc.is_done() && text_is_visible(card_output(tc))) {
        out.kind = Kind::CodeBlock;
        out.text = clean_output(card_output(tc));
        // 已解决在此登陆的工具的产出( Q) repo_map ,大纲,
        // read_filter 搜索 提取 json_query,
        // MCP 工具...)是: 或是: 工具(或是工具)是: HEAD-heavy
        // 答案是顶部,是 末尾 正在拨号/ 截号聊天。 tail_only 仅对
        // LIVE 以上(最新线条下方钉住, 终端-like).
        out.tail_only = false;
        // read_filter / 搜索 / 提取所有它们搜索的东西
        // for; light it up in the output so the answer is findable at a
        // 眼看而不是需要第二个读作-through。
        out.match_needle = detail::args_match_needle(tc.args);
    }
    // 空的 success/failure/rejection 已明确在 事件 头文件.
    // 发明a 正文 仅按结算时排行即可转接已承诺的笔录行;
    // 满数 -output 覆盖面板 提供保险箱 "no output" 解释。
    return;
}
} // 命名空间结束。

FrozenBuildScope::FrozenBuildScope() noexcept
    : prev_(frozen_build_get()) { frozen_build_set(true); }
FrozenBuildScope::~FrozenBuildScope() { frozen_build_set(prev_); }
bool building_frozen() noexcept { return frozen_build_get(); }

GrepHits collect_grep_hits(std::span<const ToolUse> tool_calls) {
    GrepHits out;
    for (const auto& tc : tool_calls) {
        if (tc.name.value != "grep") continue;
        const std::string_view body = card_output(tc);
        if (body.empty()) continue;
        detail::accumulate_grep_hits(body, out);
    }
    return out;
}

maya::ToolBodyPreview::Config tool_body_preview_config(
    const ToolUse& tc, const GrepHits* grep_hits)
{
    const auto& n = tc.name.value;
    maya::ToolBodyPreview::Config out;
    // 命令/工具输出获得其路径,数字,字符串,
    // 散列和标志彩色(仅命名颜色,故原生读法也很好).
    out.colorize = true;
    // 读 / 写 / 编辑 / diff 正文 的语法颜色化语言.
    if (n == "read" || n == "write" || n == "edit" || n == "apply_patch"
        || n == "replace" || n == "rewrite_structural" || n == "git_diff"
        || n == "find_definition")
        out.lang = detail::lang_for_tool_or_content(tc);

    // 色调( 线条) -number 沟渠,管道分隔器,
    // 电离标记)读于工具的分类颜色中,所以 正文 其结构与 头
    // 文件 NAME 颜色。 正文 内容本身保留
    // text_tertiary (dim)所以彩色的铬 帧 不需要
    // 跟传教士竞争,每人 -tool 渲染器 可能覆盖
    // chrome_color (e.g 失败路径将其翻转为红色).
    out.chrome_color = tool_category_color(n);

    // 结构化渲染器优先处理。每个返回值都经过
    //    通用生命周期 回退 如此的平稳 silent/empty
    //    工具解释他们等待什么,或他们是如何定居的。
    if (n == "edit" || n == "apply_patch") {
        // apply_patch 发射 相同 细微的 -fenced 输出形状作为编辑
        // ( 两种路由都通过) diff::render_unified ) 围栏
        // -parse 路径给出了彩色的−/+ diff卡,而不是一个暗的通用代码块。
        detail::edit_body(tc, out);
        generic_lifecycle_body(tc, out);
        return out;
    }
    if (n == "bash" || n == "shell" || n == "diagnostics" || n == "test"
        || n == "process_start" || n == "process_poll"
        || n == "process_stop") {
        // 过程 3 : IS 终端 产出 - 末尾-heavy, ANSI-laden ,
        // 读取问题与bash(读取问题)相同。 "what did it print / did it fail?" ),
        // 所以它分享 BashOutput 结构 -extraction
        // 链式(试验摘要, 编译器 诊断)和 末尾 窗口中选择一个窗口。
        //
        // `bash` ITSELF在此列表中缺失,而注释将其命名
        // 为"原型",它掉到了通用的CodeBlock路径上,所以
        // 语料中使用最多的工具仍把 ``` 围栏保留为正文。
        // 连连连连都丢掉了 末尾-oriented 窗口 -- strip_bash_output_fence
        // 并且根本就没有达到。
        detail::bash_body(tc, out);
        generic_lifecycle_body(tc, out);
        return out;
    }
    if (n == "write") {
        detail::write_body(tc, out);
        generic_lifecycle_body(tc, out);
        return out;
    }

    if (n == "git_diff") {
        if (detail::git_diff_body(tc, out)) {
            generic_lifecycle_body(tc, out);
            return out;
        }
    }
    if (n == "read" || n == "find_definition") {
        if (detail::read_body(tc, grep_hits, out)) {
            generic_lifecycle_body(tc, out);
            return out;
        }
    }
    if (n == "web_fetch") {
        if (detail::web_fetch_body(tc, out)) {
            generic_lifecycle_body(tc, out);
            return out;
        }
    }
    if (n == "grep" || n == "find_references") {
        if (detail::grep_body(tc, out)) {
            generic_lifecycle_body(tc, out);
            return out;
        }
    }
    if (n == "replace" || n == "rewrite_structural") {
        if (detail::diff_preview_body(tc, out)) {
            generic_lifecycle_body(tc, out);
            return out;
        }
    }
    if (n == "glob" || n == "list_dir"
        || n == "web_search"
        || n == "git_status" || n == "git_log" || n == "git_commit") {
        if (detail::generic_list_body(tc, out)) {
            generic_lifecycle_body(tc, out);
            return out;
        }
    }

    if (n == "task") {
        detail::task_body(tc, out);
        generic_lifecycle_body(tc, out);
        return out;
    }

    // 保存故意命令: 失败的待办事宜会显示错误,
    // 而不是它的 过期 复选框列表。 所有其他待办事宜状态
    // 都会保留结构化列表。
    if (tc.is_failed()) {
        generic_lifecycle_body(tc, out);
        return out;
    }
    if (n == "todo" && detail::todo_body(tc, out)) {
        generic_lifecycle_body(tc, out);
        return out;
    }

    generic_lifecycle_body(tc, out);
    return out;
}

} // namespace agentty::ui
